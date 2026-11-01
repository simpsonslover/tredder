// Test targets: the TypeScript behaviour simulator (src/) and the real firmware
// running in the emulator (firmware/build-emu/tredder-emu.exe). The same e2e
// tests run against both.
import { spawn, type ChildProcess } from 'node:child_process';
import { existsSync } from 'node:fs';
import { createConnection, createServer } from 'node:net';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { startSim } from '../src/server';

export const EMU_EXE = join(dirname(fileURLToPath(import.meta.url)), '..', '..', 'firmware', 'build-emu', 'tredder-emu.exe');
export const TARGETS: ('sim' | 'emu')[] = existsSync(EMU_EXE) ? ['sim', 'emu'] : ['sim'];

export interface TargetOptions {
  seed?: string;
  sweepSeed?: string;
  pin?: string;
  /** Approve every prompt (the emulator's robot finger presses the buttons; all checks still run). */
  autoConfirm?: boolean;
  /** Emulator only: keep flash + secure element in this directory instead of memory. */
  stateDir?: string;
  extraArgs?: string[];
}

export interface Target {
  kind: 'sim' | 'emu';
  port: number;
  info: { model: string; firmware: string };
  debug?: DebugLink;
  close(): Promise<void>;
}

export async function freePort(): Promise<number> {
  return new Promise((resolve, reject) => {
    const s = createServer();
    s.listen(0, '127.0.0.1', () => {
      const p = (s.address() as { port: number }).port;
      s.close(() => resolve(p));
    });
    s.on('error', reject);
  });
}

/** Line protocol of the emulator's debug link (platform/emu/dbg.c). */
export class DebugLink {
  private buf = '';
  private waiters: ((l: string) => void)[] = [];
  private constructor(private readonly sock: ReturnType<typeof createConnection>) {
    sock.setEncoding('utf8');
    sock.on('data', (d: string) => {
      this.buf += d;
      let i;
      while ((i = this.buf.indexOf('\n')) >= 0) {
        const line = this.buf.slice(0, i);
        this.buf = this.buf.slice(i + 1);
        this.waiters.shift()?.(line);
      }
    });
  }
  static async connect(port: number): Promise<DebugLink> {
    for (let i = 0; i < 100; i++) {
      try {
        const s = await new Promise<ReturnType<typeof createConnection>>((res, rej) => {
          const c = createConnection(port, '127.0.0.1', () => res(c));
          c.on('error', rej);
        });
        return new DebugLink(s);
      } catch {
        await new Promise((r) => setTimeout(r, 50));
      }
    }
    throw new Error('debug link not reachable');
  }
  cmd(c: string): Promise<string> {
    return new Promise((res) => {
      this.waiters.push(res);
      this.sock.write(c + '\n');
    });
  }
  async screen(): Promise<Screen> {
    return JSON.parse(await this.cmd('screen')) as Screen;
  }
  async press(b: 'up' | 'down' | 'back' | 'ok') {
    await this.cmd(`press ${b}`);
  }
  async type(s: string) {
    await this.cmd(`type ${s}`);
  }
  async enter() {
    await this.cmd('enter');
  }
  close() {
    this.sock.destroy();
  }
}

export interface Screen {
  kind: 'menu' | 'pages' | 'pin' | 'word';
  title: string;
  lines: string[];
  items: string[];
  cursor: number;
  page: number;
  npages: number;
  initialized: boolean;
  locked: boolean;
  triesLeft: number;
}

export async function waitFor(d: DebugLink, pred: (s: Screen) => boolean, ms = 10_000): Promise<Screen> {
  const t0 = Date.now();
  for (;;) {
    const s = await d.screen();
    if (pred(s)) return s;
    if (Date.now() - t0 > ms) throw new Error(`timeout waiting for screen, last: ${JSON.stringify(s)}`);
    await new Promise((r) => setTimeout(r, 20));
  }
}

export async function startEmu(o: TargetOptions & { debug?: boolean } = {}): Promise<Target> {
  const port = await freePort();
  const dport = await freePort();
  const args = ['--headless', '--port', String(port), '--debug-port', String(dport), ...(o.stateDir ? ['--state', o.stateDir] : ['--ephemeral'])];
  if (o.extraArgs) args.push(...o.extraArgs);
  if (o.seed) args.push('--seed', o.seed, '--pin', o.pin ?? '1234');
  if (o.sweepSeed) args.push('--sweep-seed', o.sweepSeed);
  if (o.autoConfirm) args.push('--auto-approve');
  const proc: ChildProcess = spawn(EMU_EXE, args, { stdio: ['ignore', 'pipe', 'pipe'] });
  let log = '';
  proc.stdout?.on('data', (d) => (log += d));
  proc.stderr?.on('data', (d) => (log += d));
  const debug = await DebugLink.connect(dport).catch((e) => {
    proc.kill();
    throw new Error(`${(e as Error).message}\n${log}`);
  });
  // Tests that auto-confirm expect an unlocked device: wait for the robot to unlock it.
  if (o.autoConfirm && o.seed) await waitFor(debug, (s) => !s.locked);
  return {
    kind: 'emu',
    port,
    info: { model: 'tredder', firmware: '0.2.0-emu' },
    debug,
    async close() {
      debug.close();
      if (proc.exitCode === null && proc.signalCode === null) {
        const exited = new Promise((r) => proc.once('exit', r));
        proc.kill();
        await exited;
      }
    },
  };
}

export async function startTarget(kind: 'sim' | 'emu', o: TargetOptions = {}): Promise<Target> {
  if (kind === 'emu') return startEmu(o);
  const sim = await startSim({ port: 0, uiPort: 0, seed: o.seed, sweepSeed: o.sweepSeed, pin: o.pin, autoConfirm: o.autoConfirm, autoPair: true, quiet: true });
  return { kind, port: sim.port, info: { model: 'tredder-sim', firmware: 'sim-0.1.0' }, close: () => sim.close() };
}
