// HTTP (static UI) + WebSocket (live data / commands) in one small server.

import { createServer, type Server } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join, normalize } from 'node:path';
import { WebSocketServer, type WebSocket } from 'ws';

import type { ClientCommand, ServerMessage } from '../shared/types.js';
import { RadarModel } from './model.js';
import { encodeCommand, parseLine } from './protocol.js';
import type { Transport } from './transport.js';

const MIME: Record<string, string> = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.svg': 'image/svg+xml',
};

export interface ConsoleServer {
  server: Server;
  close(): void;
}

export function startServer(opts: {
  transport: Transport;
  model: RadarModel;
  publicDir: string;
  port: number;
  host?: string;
  now?: () => number;
}): ConsoleServer {
  const { transport, model, publicDir, port } = opts;
  const host = opts.host ?? '127.0.0.1';          // local only: the console can drive hardware
  const now = opts.now ?? Date.now;

  // ---------- static files ----------
  const server = createServer(async (req, res) => {
    try {
      const url = new URL(req.url ?? '/', 'http://localhost');
      const rel = normalize(decodeURIComponent(url.pathname === '/' ? '/index.html' : url.pathname));
      if (rel.includes('..')) { res.writeHead(400).end(); return; }
      const data = await readFile(join(publicDir, rel));
      res.writeHead(200, { 'Content-Type': MIME[extname(rel)] ?? 'application/octet-stream', 'Cache-Control': 'no-store' });
      res.end(data);
    } catch {
      res.writeHead(404).end('not found');
    }
  });

  // ---------- websocket ----------
  const wss = new WebSocketServer({ server, path: '/ws' });
  const broadcast = (m: ServerMessage): void => {
    const text = JSON.stringify(m);
    for (const c of wss.clients) if (c.readyState === c.OPEN) c.send(text);
  };
  const pushSnapshot = (): void => broadcast({ type: 'snapshot', snapshot: model.snapshot(now()) });

  transport.onStatus((connected) => { model.setConnected(connected); pushSnapshot(); });
  transport.onLine((line) => {
    broadcast({ type: 'log', dir: 'rx', line });
    const msg = parseLine(line);
    if (msg && model.apply(msg, now())) pushSnapshot();
  });

  wss.on('connection', (ws: WebSocket) => {
    ws.send(JSON.stringify({ type: 'snapshot', snapshot: model.snapshot(now()) } satisfies ServerMessage));
    ws.on('message', (data) => {
      let cmd: ClientCommand;
      try { cmd = JSON.parse(data.toString()) as ClientCommand; } catch { return; }
      const line = encodeCommand(cmd);
      if (line === null) return;                 // invalid input is dropped, never forwarded
      transport.write(line);
      broadcast({ type: 'log', dir: 'tx', line });
    });
  });

  // "RSU alive" depends on time passing, not only on messages
  const aliveTimer = setInterval(pushSnapshot, 1000);

  server.listen(port, host);
  transport.open();

  return {
    server,
    close() {
      clearInterval(aliveTimer);
      transport.close();
      wss.close();
      server.close();
    },
  };
}
