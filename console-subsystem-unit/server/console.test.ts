import { test } from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocket } from 'ws';

import { encodeCommand, parseLine } from './protocol.js';
import { DEFAULT_CONFIG, RadarModel } from './model.js';
import { startServer } from './server.js';
import type { Transport } from './transport.js';
import type { ServerMessage, Snapshot } from '../shared/types.js';

// ---------- protocol ----------
test('parseLine: valid messages from the firmware', () => {
  assert.deepEqual(parseLine('STATE,2,PREALARM'), { kind: 'state', state: 'PREALARM' });
  assert.deepEqual(parseLine('SEC,4,67,55,1\r'), { kind: 'sec', sector: 4, angle: 67, cm: 55, detected: true });
  assert.deepEqual(parseLine('SEC,0,7,0,0'), { kind: 'sec', sector: 0, angle: 7, cm: 0, detected: false });
  assert.deepEqual(parseLine('TEMP,235'), { kind: 'temp', tenths: 235 });
  assert.deepEqual(parseLine('TEMP,-12'), { kind: 'temp', tenths: -12 });
  assert.deepEqual(parseLine('HB,3,1'), { kind: 'hb', state: 'ALARM', remote: true });
});

test('parseLine: garbage never throws, returns null', () => {
  for (const bad of ['', 'hello', 'STATE', 'STATE,9,X', 'STATE,x,IDLE', 'SEC,1,2', 'SEC,1,200,5,1',
    'SEC,-1,5,5,1', 'TEMP,abc', 'HB,1', '\u0000\u0001']) {
    assert.equal(parseLine(bad), null, JSON.stringify(bad));
  }
});

test('encodeCommand validates', () => {
  assert.equal(encodeCommand({ cmd: 'START' }), 'START');
  assert.equal(encodeCommand({ cmd: 'ANGLE', angle: 90 }), 'ANGLE,90');
  assert.equal(encodeCommand({ cmd: 'ANGLE', angle: 181 }), null);
  assert.equal(encodeCommand({ cmd: 'ANGLE', angle: -1 }), null);
  assert.equal(encodeCommand({ cmd: 'ANGLE', angle: 1.5 }), null);
  assert.equal(encodeCommand({ cmd: 'ANGLE', angle: NaN }), null);
  assert.equal(encodeCommand({ cmd: 'FORMAT' } as never), null);
});

// ---------- model ----------
test('model: sectors, state transitions, alive timeout', () => {
  const m = new RadarModel();
  m.setConnected(true);
  m.apply({ kind: 'state', state: 'WORKING' }, 1000);
  m.apply({ kind: 'sec', sector: 3, angle: 52, cm: 60, detected: true }, 1100);
  m.apply({ kind: 'sec', sector: 4, angle: 67, cm: 0, detected: false }, 1200);
  m.apply({ kind: 'temp', tenths: 235 }, 1250);
  let s = m.snapshot(1300);
  assert.equal(s.state, 'WORKING');
  assert.equal(s.tempC, 23.5);
  assert.equal(s.sectors[3]?.cm, 60);
  assert.equal(s.sectors[3]?.ageMs, 200);
  assert.equal(s.sectors[4]?.detected, false);
  assert.equal(s.sectors[0], null);
  assert.equal(s.currentSector, 4);
  assert.ok(s.rsuAlive);

  // out-of-range sector from a mis-configured console is ignored
  assert.equal(m.apply({ kind: 'sec', sector: 99, angle: 1, cm: 1, detected: true }, 1300), false);

  // alarm keeps the picture; reset -> working keeps it as well
  m.apply({ kind: 'state', state: 'ALARM' }, 1400);
  m.apply({ kind: 'state', state: 'WORKING' }, 1500);
  assert.equal(m.snapshot(1500).sectors[3]?.cm, 60);

  // stop -> blank picture, remote released
  m.apply({ kind: 'hb', state: 'WORKING', remote: true }, 1600);
  assert.equal(m.snapshot(1600).remote, true);
  m.apply({ kind: 'state', state: 'IDLE' }, 1700);
  s = m.snapshot(1700);
  assert.equal(s.remote, false);
  assert.ok(s.sectors.every((x) => x === null));

  assert.equal(m.snapshot(1700 + DEFAULT_CONFIG.aliveTimeoutMs + 1).rsuAlive, false);
});

// ---------- end to end through the real server ----------
class FakeTransport implements Transport {
  lineCb: (l: string) => void = () => {};
  statusCb: (c: boolean) => void = () => {};
  written: string[] = [];
  onLine(cb: (l: string) => void) { this.lineCb = cb; }
  onStatus(cb: (c: boolean) => void) { this.statusCb = cb; }
  write(l: string) { this.written.push(l); }
  open() { setImmediate(() => this.statusCb(true)); }
  close() {}
}

test('server: pushes snapshots and forwards only valid commands', async () => {
  const t = new FakeTransport();
  const publicDir = join(dirname(fileURLToPath(import.meta.url)), '..', 'public');
  const port = 38000 + Math.floor(Math.random() * 1000);
  const srv = startServer({ transport: t, model: new RadarModel(), publicDir, port });
  try {
    await once(srv.server, 'listening');
    const ws = new WebSocket(`ws://127.0.0.1:${port}/ws`);
    const snaps: Snapshot[] = [];
    ws.on('message', (d) => {
      const m = JSON.parse(d.toString()) as ServerMessage;
      if (m.type === 'snapshot') snaps.push(m.snapshot);
    });
    await once(ws, 'open');
    await new Promise((r) => setTimeout(r, 50));
    assert.ok(snaps.length >= 1);

    t.lineCb('STATE,1,WORKING');
    t.lineCb('SEC,2,37,25,1');
    await new Promise((r) => setTimeout(r, 50));
    const s = snaps.at(-1)!;
    assert.equal(s.state, 'WORKING');
    assert.equal(s.sectors[2]?.cm, 25);
    assert.equal(s.connected, true);

    ws.send(JSON.stringify({ cmd: 'ANGLE', angle: 120 }));
    ws.send(JSON.stringify({ cmd: 'ANGLE', angle: 500 }));     // rejected
    ws.send(JSON.stringify({ cmd: 'DROP TABLE' }));            // rejected
    ws.send('not json');                                        // ignored
    ws.send(JSON.stringify({ cmd: 'RESET' }));
    await new Promise((r) => setTimeout(r, 50));
    assert.deepEqual(t.written, ['ANGLE,120', 'RESET']);

    // static page is served
    const res = await fetch(`http://127.0.0.1:${port}/`);
    assert.equal(res.status, 200);
    assert.match(await res.text(), /Smart Radar Console/);
    assert.equal((await fetch(`http://127.0.0.1:${port}/../package.json`)).status === 200, false);
    ws.close();
  } finally {
    srv.close();
  }
});
