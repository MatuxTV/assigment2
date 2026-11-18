// Entry point.
//   npm start -- --port /dev/cu.usbmodem1101      real Arduino
//   npm run mock                                   built-in simulator
//   options: --http 3000   --baud 115200   --list

import { existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { RadarModel } from './model.js';
import { MockTransport } from './mock.js';
import { SerialTransport, type Transport } from './transport.js';
import { startServer } from './server.js';

function arg(name: string): string | undefined {
  const i = process.argv.indexOf(`--${name}`);
  return i >= 0 ? process.argv[i + 1] : undefined;
}
const flag = (name: string): boolean => process.argv.includes(`--${name}`);

/** package root = nearest parent holding package.json (works from server/ and dist/server/) */
function packageRoot(): string {
  let dir = dirname(fileURLToPath(import.meta.url));
  while (!existsSync(join(dir, 'package.json'))) {
    const up = dirname(dir);
    if (up === dir) throw new Error('package.json not found');
    dir = up;
  }
  return dir;
}

async function main(): Promise<void> {
  if (flag('list')) {
    const { SerialPort } = await import('serialport');
    for (const p of await SerialPort.list()) console.log(`${p.path}\t${p.manufacturer ?? ''}`);
    return;
  }

  let transport: Transport;
  if (flag('mock')) {
    transport = new MockTransport();
    console.log('Using the built-in RSU simulator (no hardware).');
  } else {
    const path = arg('port');
    if (!path) {
      console.error('Missing --port <serial port>. Use --list to see the ports, or --mock for the simulator.');
      process.exit(1);
    }
    transport = new SerialTransport(path, Number(arg('baud') ?? 115200));
  }

  const httpPort = Number(arg('http') ?? 3000);
  const publicDir = resolve(packageRoot(), 'public');
  startServer({ transport, model: new RadarModel(), publicDir, port: httpPort });
  console.log(`Console running at http://127.0.0.1:${httpPort}`);
}

main().catch((e) => { console.error(e); process.exit(1); });
