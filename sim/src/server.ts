// tredder simulator: protocol endpoint (WebSocket, same frames as BLE) + device window.
//
//   npx tsx src/server.ts [--port 8765] [--ui-port 8766] [--state FILE | --ephemeral]
//                         [--seed "public test words"] [--pin 1234] [--auto-pair]
//
// DEVELOPMENT ONLY. The simulated seed and PIN are stored UNENCRYPTED in the state
// file (default ~/.tredder-sim/state.json). Never put a real seed in it.
import { readFileSync } from 'node:fs';
import { createServer, type Server } from 'node:http';
import type { AddressInfo } from 'node:net';
import { homedir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { WebSocketServer, type WebSocket } from 'ws';

import { decodeMessage, encodeMessage } from '../../app/src/protocol/codec';
import { Reassembler, toFrames } from '../../app/src/protocol/framing';
import type { WireEvent, WireRequest } from '../../app/src/protocol/types';
import { Device, type Button, type DeviceOptions } from './device';

const FRAME = 244; // BLE ATT MTU 247 - 3

export interface SimHandle {
  device: Device;
  port: number;
  uiPort: number;
  close(): Promise<void>;
}

export async function startSim(o: DeviceOptions & { port?: number; uiPort?: number; quiet?: boolean } = {}): Promise<SimHandle> {
  const device = new Device(o);
  const log: string[] = [];
  const uiClients = new Set<WebSocket>();
  const say = (line: string) => {
    const l = `${new Date().toLocaleTimeString()}  ${line}`;
    log.push(l);
    if (log.length > 200) log.shift();
    if (!o.quiet) console.log(l);
    broadcast();
  };
  const snapshot = () => JSON.stringify({ view: device.view(), status: device.status(), log });
  const broadcast = () => uiClients.forEach((c) => c.readyState === c.OPEN && c.send(snapshot()));
  device.on('screen', broadcast);
  device.on('log', say);

  // ---------------------------------------------------------------- protocol endpoint
  const proto = new WebSocketServer({ port: o.port ?? 8765 });
  let phone: WebSocket | null = null;
  const sendMsg = (ws: WebSocket, msg: object) => {
    for (const f of toFrames(encodeMessage(msg as WireEvent), FRAME)) ws.send(f);
  };
  device.on('device-event', (e: WireEvent) => phone && sendMsg(phone, e));

  proto.on('connection', (ws) => {
    if (phone) {
      say('new app connection replaces the previous one (one phone at a time)');
      phone.close(4001, 'replaced');
    }
    phone = ws;
    say('app connected');
    const rx = new Reassembler();
    ws.on('message', async (data: Buffer, isBinary: boolean) => {
      if (!isBinary) return;
      let msg: Uint8Array | null;
      try {
        msg = rx.push(new Uint8Array(data));
      } catch (e) {
        say(`framing error, dropped: ${(e as Error).message}`);
        return;
      }
      if (!msg) return;
      let req: WireRequest;
      try {
        req = decodeMessage(msg) as WireRequest;
        if (typeof req !== 'object' || typeof req.id !== 'number' || typeof req.type !== 'string') throw new Error('not a request');
      } catch (e) {
        say(`malformed message dropped: ${(e as Error).message}`);
        return;
      }
      say(`→ ${req.type} #${req.id}${'wallet' in req ? ` (${req.wallet})` : ''}`);
      const res = await device.handle(req);
      say(`← ${req.type} #${req.id} ${res.ok ? 'ok' : `${res.error.code}: ${res.error.message}`}`);
      if (ws.readyState === ws.OPEN) sendMsg(ws, res);
    });
    ws.on('close', () => {
      if (phone === ws) {
        phone = null;
        say('app disconnected');
        device.onDisconnect();
      }
    });
  });

  // ---------------------------------------------------------------- device window
  const here = dirname(fileURLToPath(import.meta.url));
  const http: Server = createServer((req, res) => {
    if (req.url === '/' || req.url === '/index.html') {
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
      res.end(readFileSync(join(here, 'ui.html')));
    } else {
      res.writeHead(404).end();
    }
  });
  const ui = new WebSocketServer({ server: http, path: '/ui' });
  ui.on('connection', (ws) => {
    uiClients.add(ws);
    ws.send(snapshot());
    ws.on('message', (raw) => {
      try {
        const m = JSON.parse(String(raw)) as { button?: Button; key?: string };
        if (m.button && ['up', 'down', 'back', 'confirm'].includes(m.button)) device.press(m.button);
        else if (typeof m.key === 'string' && m.key.length <= 9) device.type(m.key);
      } catch {
        /* ignore */
      }
    });
    ws.on('close', () => uiClients.delete(ws));
  });
  await new Promise<void>((r) => http.listen(o.uiPort ?? 8766, '127.0.0.1', r));
  await new Promise<void>((r) => (proto.address() ? r() : proto.once('listening', () => r())));

  const port = (proto.address() as AddressInfo).port;
  const uiPort = (http.address() as AddressInfo).port;
  say(`protocol ws://localhost:${port}  ·  device window http://localhost:${uiPort}`);
  return {
    device,
    port,
    uiPort,
    async close() {
      proto.clients.forEach((c) => c.terminate());
      ui.clients.forEach((c) => c.terminate());
      await new Promise<void>((r) => proto.close(() => r()));
      await new Promise<void>((r) => http.close(() => r()));
      device.removeAllListeners();
    },
  };
}

// ---------------------------------------------------------------- CLI
if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const arg = (k: string) => {
    const i = process.argv.indexOf(k);
    return i > 0 ? process.argv[i + 1] : undefined;
  };
  const ephemeral = process.argv.includes('--ephemeral');
  const statePath = ephemeral ? undefined : arg('--state') ?? join(homedir(), '.tredder-sim', 'state.json');
  console.log('tredder SIMULATOR (development only): keys are NOT protected. Never use a real seed.');
  if (statePath) console.log(`state file: ${statePath}`);
  await startSim({
    port: Number(arg('--port') ?? 8765),
    uiPort: Number(arg('--ui-port') ?? 8766),
    statePath,
    seed: arg('--seed'),
    pin: arg('--pin'),
    autoPair: process.argv.includes('--auto-pair'),
  });
}
