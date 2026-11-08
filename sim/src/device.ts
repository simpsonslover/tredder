// The simulated tredder: screens, buttons, PIN, wallet lifecycle and protocol
// request handling. DEVELOPMENT ONLY: the seed and PIN are kept in a plain
// JSON file (the real device encrypts the seed under a key held by the secure element).
import { EventEmitter } from 'node:events';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname } from 'node:path';

import { sha256 } from '@noble/hashes/sha2.js';
import { bytesToHex } from '@noble/hashes/utils.js';
import { entropyToMnemonic, validateMnemonic } from '@scure/bip39';

import { SCHEMES, type Request, type WireEvent, type WireResponse } from '../../app/src/protocol/types';
import { WORDLIST, WalletKeys, isBtc } from './keys';
import { Refused, analyzeBtc, analyzeEvm, analyzeSol, sameAddress, type Analysis, type Page } from './sign';

export type Button = 'up' | 'down' | 'back' | 'confirm';
export interface View {
  title: string;
  lines: string[];
  items?: string[];
  cursor?: number;
  tone?: 'normal' | 'warn' | 'danger';
  hint?: string;
}
export interface Status {
  locked: boolean;
  initialized: boolean;
  paired: boolean;
  hasSweepSource: boolean;
  failedPins: number;
}
interface Mode {
  view(): View;
  button(b: Button): void;
  key?(ch: string): void;
}
interface Persisted {
  mnemonic?: string;
  pin?: string;
  failed: number;
  sweepSource?: string;
  blindSigning: boolean;
}

export interface DeviceOptions {
  statePath?: string; // undefined = in-memory only
  autoConfirm?: boolean; // tests: approve every prompt (all checks still run)
  autoPair?: boolean;
  seed?: string; // preload a wallet (public test phrases only!)
  sweepSeed?: string; // preload an imported seed waiting to be swept (tests)
  pin?: string;
  idleLockMs?: number;
  pinDelayMs?: number; // base delay after 3 wrong PINs (tests: 0)
}

/** EVM hex addresses are case-insensitive (EIP-55 is only a checksum); BTC/SOL are exact. */
const norm = (a: string) => (/^0x[0-9a-fA-F]{40}$/.test(a) ? a.toLowerCase() : a);

const shuffle = <T>(a: T[]): T[] => {
  const r = [...a];
  const rnd = new Uint32Array(r.length);
  crypto.getRandomValues(rnd);
  for (let i = r.length - 1; i > 0; i--) {
    const j = rnd[i]! % (i + 1);
    [r[i], r[j]] = [r[j]!, r[i]!];
  }
  return r;
};

export class Device extends EventEmitter {
  private st: Persisted = { failed: 0, blindSigning: false };
  private keys: WalletKeys | null = null;
  private sweepKeys: WalletKeys | null = null;
  private stack: Mode[] = [];
  private locked = true;
  private paired = false;
  private busy = false;
  private approvedPlan: { hash: string; steps: { to: string }[] } | null = null;
  private idleTimer: ReturnType<typeof setTimeout> | null = null;
  private lockoutUntil = 0;

  constructor(private readonly opts: DeviceOptions = {}) {
    super();
    if (opts.statePath && existsSync(opts.statePath)) this.st = { ...this.st, ...JSON.parse(readFileSync(opts.statePath, 'utf8')) };
    if (opts.seed) {
      this.st.mnemonic = opts.seed;
      this.st.pin = opts.pin ?? '1234';
    }
    if (opts.sweepSeed) this.st.sweepSource = opts.sweepSeed;
    this.loadKeys();
    this.paired = !!opts.autoPair;
    this.locked = !(opts.autoConfirm && this.keys); // tests start unlocked
    this.reset();
  }

  // ---------------------------------------------------------------- state
  private save() {
    if (!this.opts.statePath) return;
    mkdirSync(dirname(this.opts.statePath), { recursive: true });
    writeFileSync(this.opts.statePath, JSON.stringify(this.st, null, 2));
  }
  private loadKeys() {
    this.keys = this.st.mnemonic ? new WalletKeys(this.st.mnemonic) : null;
    this.sweepKeys = this.st.sweepSource ? new WalletKeys(this.st.sweepSource) : null;
  }
  status(): Status {
    return { locked: this.locked, initialized: !!this.keys, paired: this.paired, hasSweepSource: !!this.sweepKeys, failedPins: this.st.failed };
  }

  // ---------------------------------------------------------------- UI plumbing
  view(): View {
    return this.stack.at(-1)!.view();
  }
  press(b: Button) {
    this.touch();
    this.stack.at(-1)?.button(b);
    this.render();
  }
  type(ch: string) {
    this.touch();
    this.stack.at(-1)?.key?.(ch);
    this.render();
  }
  private render() {
    this.emit('screen', this.view(), this.status());
  }
  private push(m: Mode) {
    this.stack.push(m);
    this.render();
  }
  private pop() {
    this.stack.pop();
    if (!this.stack.length) this.reset();
    this.render();
  }
  private reset() {
    this.stack = [this.keys ? (this.locked ? this.lockedMode() : this.homeMode()) : this.setupMode()];
    this.render();
  }
  private touch() {
    if (this.idleTimer) clearTimeout(this.idleTimer);
    this.idleTimer = setTimeout(() => this.lock('idle timeout'), this.opts.idleLockMs ?? 5 * 60_000);
    this.idleTimer.unref?.();
  }
  lock(reason = 'locked') {
    if (!this.keys || this.locked) return;
    this.locked = true;
    this.approvedPlan = null;
    this.emit('log', `device locked (${reason})`);
    this.emit('device-event', { type: 'event', event: 'locked' } satisfies WireEvent);
    this.reset();
  }
  onDisconnect() {
    this.lock('app disconnected'); // spec: auto-lock when the phone disconnects
  }

  // ---------------------------------------------------------------- generic modes
  private menu(title: string, lines: string[], items: { label: string; run: () => void }[], back?: () => void, tone?: View['tone']): Mode {
    let cur = 0;
    return {
      view: () => ({ title, lines, items: items.map((i) => i.label), cursor: cur, tone }),
      button: (b) => {
        if (b === 'up') cur = (cur + items.length - 1) % items.length;
        if (b === 'down') cur = (cur + 1) % items.length;
        if (b === 'confirm') items[cur]!.run();
        if (b === 'back') back?.();
      },
    };
  }

  /** Review pages: Confirm only counts on the LAST page (user must scroll through everything). */
  private pages(pages: Page[], done: (ok: boolean) => void): Mode {
    let i = 0;
    return {
      view: () => {
        const p = pages[i]!;
        const last = i === pages.length - 1;
        return { title: `${p.title} (${i + 1}/${pages.length})`, lines: p.lines, tone: p.tone, hint: last ? '✓ Confirm   ✗ Back = reject' : '▼ next page   ✗ Back = reject' };
      },
      button: (b) => {
        if (b === 'up') i = Math.max(0, i - 1);
        else if (b === 'down' || (b === 'confirm' && i < pages.length - 1)) i = Math.min(pages.length - 1, i + 1);
        else if (b === 'confirm') done(true);
        else if (b === 'back') done(false);
      },
    };
  }

  /** PIN pad: fixed keys, cursor starts on a random digit and jumps to one after every entry
   *  (same as the firmware: button presses reveal nothing, the keys never move). */
  private pinMode(title: string, done: (pin: string | null) => void, lines: string[] = []): Mode {
    let pin = '';
    const items = ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '⌫', '✓ OK'];
    const randomDigit = () => Math.floor(Math.random() * 10);
    let cur = randomDigit();
    const add = (d: string) => {
      if (pin.length < 8) pin += d;
      cur = randomDigit();
    };
    return {
      view: () => ({ title, lines: [...lines, `PIN: ${'•'.repeat(pin.length) || '—'}`], items, cursor: cur, hint: 'Cursor starts on a random digit' }),
      button: (b) => {
        if (b === 'up') cur = (cur + items.length - 1) % items.length;
        if (b === 'down') cur = (cur + 1) % items.length;
        if (b === 'back') (pin ? (pin = pin.slice(0, -1)) : done(null));
        if (b === 'confirm') {
          const it = items[cur]!;
          if (it === '⌫') pin = pin.slice(0, -1);
          else if (it.startsWith('✓')) pin.length >= 4 && done(pin);
          else add(it);
        }
      },
      key: (ch) => {
        if (/^\d$/.test(ch)) add(ch);
        else if (ch === 'Enter' && pin.length >= 4) done(pin);
        else if (ch === 'Backspace') pin = pin.slice(0, -1);
      },
    };
  }

  private info(title: string, lines: string[], then: () => void, tone?: View['tone']): Mode {
    return { view: () => ({ title, lines, tone, hint: '✓ continue' }), button: (b) => (b === 'confirm' || b === 'back') && then() };
  }

  // ---------------------------------------------------------------- top-level screens
  private homeMode(): Mode {
    return this.menu(
      'tredder',
      ['Unlocked', this.paired ? 'App paired' : 'No app paired', ...(this.sweepKeys ? ['Imported seed waiting', 'to be swept'] : [])],
      [
        { label: 'Lock', run: () => this.lock('user') },
        { label: `Blind signing: ${this.st.blindSigning ? 'ON' : 'off'}`, run: () => this.toggleBlind() },
        { label: 'Import seed phrase', run: () => this.importFlow() },
        { label: 'Wipe device', run: () => this.wipeFlow() },
      ],
    );
  }

  private toggleBlind() {
    if (this.st.blindSigning) {
      this.st.blindSigning = false;
      this.save();
      this.reset();
      return;
    }
    this.push(
      this.pages([{ title: 'Enable blind signing?', lines: ['You will sign data the', 'device cannot decode.', 'Scammers rely on this.'], tone: 'danger' }], (ok) => {
        this.pop();
        if (ok) {
          this.st.blindSigning = true;
          this.save();
        }
        this.reset();
      }),
    );
  }

  private lockedMode(): Mode {
    return this.menu('tredder', ['Locked', this.st.failed ? `${this.st.failed} wrong PIN attempt(s)` : ''], [{ label: 'Unlock', run: () => this.unlockFlow() }]);
  }

  private setupMode(): Mode {
    return this.menu('Welcome', ['New device'], [
      { label: 'Create new wallet', run: () => this.createFlow() },
      { label: 'Import seed phrase', run: () => this.importFlow() },
      { label: 'Restore microSD backup', run: () => this.push(this.info('Not in simulator', ['microSD restore is', 'firmware milestone M6.'], () => this.pop())) },
    ]);
  }

  private unlockFlow() {
    const wait = Math.ceil((this.lockoutUntil - Date.now()) / 1000);
    if (wait > 0) return this.push(this.info('Please wait', [`Too many wrong PINs.`, `Try again in ${wait}s`], () => this.pop(), 'warn'));
    this.push(
      this.pinMode('Enter PIN', (pin) => {
        this.pop();
        if (pin === null) return;
        if (pin === this.st.pin) {
          this.st.failed = 0;
          this.locked = false;
          this.save();
          this.reset();
          return;
        }
        this.st.failed++;
        this.save();
        if (this.st.failed >= 10) return this.wipe('10 wrong PINs');
        this.lockoutUntil = Date.now() + (this.st.failed >= 3 ? 2 ** (this.st.failed - 3) * (this.opts.pinDelayMs ?? 1000) : 0);
        this.push(this.info('Wrong PIN', [`${10 - this.st.failed} attempts left`, 'before the device wipes', 'itself.'], () => this.pop(), 'warn'));
      }, [`Attempts left: ${10 - this.st.failed}`]),
    );
  }

  private choosePin(done: () => void) {
    this.push(
      this.pinMode('Choose PIN (4-8)', (a) => {
        this.pop();
        if (!a) return;
        this.push(
          this.pinMode('Repeat PIN', (b) => {
            this.pop();
            if (b !== a) return this.push(this.info('PINs differ', ['Try again.'], () => (this.pop(), this.choosePin(done)), 'warn'));
            this.st.pin = a;
            this.st.failed = 0;
            this.save();
            done();
          }),
        );
      }),
    );
  }

  private wipe(reason: string) {
    this.st = { failed: 0, blindSigning: false };
    this.save();
    this.loadKeys();
    this.locked = true;
    this.emit('log', `DEVICE WIPED (${reason})`);
    this.reset();
  }

  private wipeFlow() {
    this.push(
      this.pages([{ title: 'Wipe device?', lines: ['Erases the seed.', 'Only your written backup', 'can restore the funds.'], tone: 'danger' }], (ok) => {
        this.pop();
        if (ok) this.wipe('user');
      }),
    );
  }

  // ---------------------------------------------------------------- create wallet
  private createFlow(onCreated?: (mnemonic: string) => void) {
    this.push(
      this.menu('New wallet', ['How many words?'], [12, 24].map((n) => ({
        label: `${n} words`,
        run: () => {
          this.pop();
          // Simulator entropy: OS CSPRNG. The real device mixes the RP2350 TRNG + secure element RNG.
          const ent = new Uint8Array(n === 12 ? 16 : 32);
          crypto.getRandomValues(ent);
          const m = entropyToMnemonic(ent, WORDLIST);
          this.showWords(m.split(' '), () => this.quiz(m.split(' '), () => (onCreated ? onCreated(m) : this.finishNewWallet(m))));
        },
      })), () => this.pop()),
    );
  }

  private showWords(words: string[], done: () => void) {
    const pages: Page[] = [];
    for (let i = 0; i < words.length; i += 4)
      pages.push({ title: 'Write down', lines: words.slice(i, i + 4).map((w, j) => `${i + j + 1}. ${w}`) });
    pages.push({ title: 'Keep it safe', lines: ['Never type these words', 'into a phone or computer.', 'Next: a quick check.'] });
    this.push(this.pages(pages, (ok) => (this.pop(), ok ? done() : this.reset())));
  }

  private quiz(words: string[], done: () => void) {
    const positions = shuffle(words.map((_, i) => i)).slice(0, 3);
    const ask = (k: number) => {
      if (k === positions.length) return done();
      const pos = positions[k]!;
      const opts = shuffle([words[pos]!, ...shuffle(WORDLIST.filter((w) => w !== words[pos])).slice(0, 3)]);
      this.push(
        this.menu(`Check ${k + 1}/3`, [`Which is word #${pos + 1}?`], opts.map((o) => ({
          label: o,
          run: () => {
            this.pop();
            if (o === words[pos]) ask(k + 1);
            else this.push(this.info('Wrong word', ['Check your backup', 'and try again.'], () => (this.pop(), this.showWords(words, () => this.quiz(words, done))), 'warn'));
          },
        }))),
      );
    };
    ask(0);
  }

  private finishNewWallet(m: string) {
    this.st.mnemonic = m;
    this.save();
    this.loadKeys();
    const end = () => {
      this.locked = false;
      this.reset();
      this.emit('log', 'new wallet created');
    };
    this.st.pin ? end() : this.choosePin(end);
  }

  // ---------------------------------------------------------------- import
  private importFlow() {
    this.push(this.menu('Import', ['Number of words'], [12, 15, 18, 21, 24].map((n) => ({ label: `${n} words`, run: () => (this.pop(), this.enterWords(n, [])) })), () => this.pop()));
  }

  /** Letter picker with BIP39 autocomplete: only letters that lead to a real word are offered. */
  private enterWords(count: number, words: string[], editIndex?: number) {
    let prefix = '';
    let cur = 0;
    const idx = editIndex ?? words.length;
    const matches = () => WORDLIST.filter((w) => w.startsWith(prefix));
    const letters = () => [...new Set(matches().filter((w) => w.length > prefix.length).map((w) => w[prefix.length]!))];
    const accept = (w: string) => {
      this.pop();
      const next = [...words];
      next[idx] = w;
      if (editIndex !== undefined || next.length === count) this.reviewWords(count, next);
      else this.enterWords(count, next);
    };
    this.push({
      view: () => {
        const m = matches();
        const exact = m.includes(prefix) && prefix.length >= 3;
        const items = [...(m.length === 1 || exact ? [`✓ ${m.length === 1 ? m[0] : prefix}`] : []), ...letters().map((l) => prefix + l + '…')];
        cur = Math.min(cur, items.length - 1);
        return { title: `Word ${idx + 1}/${count}`, lines: [`${prefix || ' '}_`, `${m.length} matching word(s)`], items, cursor: cur, hint: '▲▼ letter  ✓ pick  ✗ delete' };
      },
      button: (b) => {
        const items = this.view().items ?? [];
        if (b === 'up') cur = (cur + items.length - 1) % items.length;
        if (b === 'down') cur = (cur + 1) % items.length;
        if (b === 'back') {
          if (prefix) prefix = prefix.slice(0, -1);
          else if (editIndex === undefined && words.length) (this.pop(), this.enterWords(count, words.slice(0, -1)));
          else this.pop();
        }
        if (b === 'confirm') {
          const it = items[cur]!;
          if (it.startsWith('✓ ')) accept(it.slice(2));
          else {
            prefix = it.slice(0, -1);
            cur = 0;
            const m = matches();
            if (m.length === 1 && prefix.length >= 4) accept(m[0]!); // unique after 4 letters
          }
        }
      },
      key: (ch) => {
        if (/^[a-z]$/.test(ch) && letters().includes(ch)) {
          prefix += ch;
          cur = 0;
        } else if (ch === 'Backspace') prefix = prefix.slice(0, -1);
        else if (ch === 'Enter') {
          const m = matches();
          if (m.length === 1 || m.includes(prefix)) accept(m.length === 1 ? m[0]! : prefix);
        }
      },
    });
  }

  private reviewWords(count: number, words: string[]) {
    const ok = validateMnemonic(words.join(' '), WORDLIST);
    this.push(
      this.menu(
        ok ? 'Review words' : 'Checksum wrong!',
        ok ? ['Check every word against', 'your paper backup.'] : ['One or more words are', 'wrong. Pick a word to fix.'],
        [
          ...words.map((w, i) => ({ label: `${i + 1}. ${w}`, run: () => (this.pop(), this.enterWords(count, words, i)) })),
          ...(ok ? [{ label: '✓ Words are correct', run: () => (this.pop(), this.importChoice(words.join(' '))) }] : []),
        ],
        () => (this.pop(), this.reset()),
        ok ? 'normal' : 'danger',
      ),
    );
  }

  private importChoice(m: string) {
    this.push(
      this.menu(
        'Was it ever typed?',
        ['A seed ever typed into a', 'phone or computer may', 'already be stolen.', 'Sweep is recommended.'],
        [
          { label: 'Import & sweep (safer)', run: () => (this.pop(), this.startSweepImport(m)) },
          { label: 'Import as-is', run: () => (this.pop(), this.importAsIs(m)) },
          { label: 'Cancel', run: () => (this.pop(), this.reset()) },
        ],
        undefined,
        'warn',
      ),
    );
  }

  private importAsIs(m: string) {
    const doIt = () => {
      this.st.mnemonic = m;
      this.st.sweepSource = undefined;
      this.save();
      this.loadKeys();
      const end = () => ((this.locked = false), this.reset(), this.emit('log', 'seed imported as-is'));
      this.st.pin ? end() : this.choosePin(end);
    };
    if (!this.keys) return doIt();
    this.push(
      this.pages([{ title: 'Replace wallet?', lines: ['The current wallet will', 'be ERASED. Only continue', 'if it is backed up.'], tone: 'danger' }], (ok) => {
        this.pop();
        if (!ok) return this.reset();
        this.push(this.pinMode('PIN to confirm', (pin) => (this.pop(), pin === this.st.pin ? doIt() : this.reset())));
      }),
    );
  }

  private startSweepImport(m: string) {
    // Imported seed becomes the sweep source; a brand-new seed becomes the active wallet.
    this.push(
      this.info('Sweep mode', ['Next: the device creates', 'a NEW seed. Write it down', 'and pass the check.'], () => {
        this.pop();
        this.createFlow((fresh) => {
          this.st.sweepSource = m;
          this.finishNewWallet(fresh);
          this.emit('log', 'sweep mode: imported seed stored as sweep source, new wallet created');
        });
      }),
    );
  }

  // ---------------------------------------------------------------- protocol
  /** Ask the user (or auto-approve in tests). Resolves true on Confirm. */
  private ask(id: number, pages: Page[], opts: { pin?: boolean } = {}): Promise<boolean> {
    if (this.opts.autoConfirm) return Promise.resolve(true);
    this.emit('device-event', { type: 'event', event: 'awaiting-user', id } satisfies WireEvent);
    return new Promise((resolve) => {
      this.push(
        this.pages(pages, (ok) => {
          this.pop();
          if (!ok || !opts.pin) return resolve(ok);
          this.push(this.pinMode('PIN to approve', (pin) => (this.pop(), resolve(pin === this.st.pin))));
        }),
      );
    });
  }

  private pair(id: number): Promise<boolean> {
    if (this.paired) return Promise.resolve(true);
    const code = String(100000 + (crypto.getRandomValues(new Uint32Array(1))[0]! % 900000));
    this.emit('log', `pairing request, code ${code}`);
    return this.ask(id, [{ title: 'Pair phone?', lines: ['Code:', code, 'Only accept if the phone', 'shows the same code.', 'One phone at a time.'] }], { pin: !!this.keys }).then((ok) => {
      this.paired = ok;
      return ok;
    });
  }

  async handle(req: Request & { id: number }): Promise<WireResponse> {
    const ok = (result: unknown): WireResponse => ({ id: req.id, ok: true, result });
    const err = (code: Extract<WireResponse, { ok: false }>['error']['code'], message: string): WireResponse => ({ id: req.id, ok: false, error: { code, message } });
    this.touch();
    try {
      if (req.type === 'get-info')
        return ok({ model: 'tredder-sim', firmware: 'sim-0.1.0', protocol: 1, initialized: !!this.keys, locked: this.locked, hasSweepSource: !!this.sweepKeys });
      if (this.busy) return err('BUSY', 'device is waiting for the user');
      if (req.type === 'lock') {
        this.lock('app request');
        return ok({});
      }
      if (!this.keys) return err('NOT_INITIALIZED', 'set up the device first');
      if (this.locked) return err('LOCKED', 'unlock the device with your PIN');
      this.busy = true;
      try {
        if (!(await this.pair(req.id))) return err('USER_REJECTED', 'pairing rejected on device');
        return await this.dispatch(req, ok, err);
      } finally {
        this.busy = false;
        this.reset();
      }
    } catch (e) {
      if (e instanceof Refused) return err(e.code, e.message);
      return err('INTERNAL', (e as Error).message);
    }
  }

  private walletKeys(w: 'active' | 'sweep-source') {
    const k = w === 'active' ? this.keys : this.sweepKeys;
    if (!k) throw new Refused('INVALID_REQUEST', `no ${w} wallet on device`);
    return k;
  }

  private checkPlanStep(wallet: string, planStep: number | undefined, a: Analysis) {
    if (wallet !== 'sweep-source') return;
    const step = planStep !== undefined ? this.approvedPlan?.steps[planStep] : undefined;
    if (!step) throw new Refused('INVALID_REQUEST', 'sweep signature without an approved plan step');
    if (!a.recipients.some((r) => sameAddress(r, step.to))) throw new Refused('INVALID_REQUEST', 'transaction does not match the approved sweep plan');
  }

  private activeSet: { keys: WalletKeys; set: Set<string> } | null = null;

  /** Is `addr` one of the ACTIVE wallet's own addresses? (sweep destinations) */
  private ownsActive(addr: string): boolean {
    const k = this.keys!;
    if (this.activeSet?.keys !== k) {
      const set = new Set<string>();
      for (const s of SCHEMES) {
        if (isBtc(s)) {
          for (const net of ['main', 'test'] as const) for (let i = 0; i < 50; i++) set.add(k.btcAddress(s, net, 0, 0, i));
        } else {
          for (let a = 0; a < (s === 'sol-bip44-root' || s === 'sol-cli' ? 1 : 5); a++) set.add(norm(k.address(s, 'main', a, 0, 0)));
        }
      }
      this.activeSet = { keys: k, set };
    }
    return this.activeSet.set.has(norm(addr));
  }

  private async dispatch(
    req: Request & { id: number },
    ok: (r: unknown) => WireResponse,
    err: (c: Extract<WireResponse, { ok: false }>['error']['code'], m: string) => WireResponse,
  ): Promise<WireResponse> {
    const reject = () => err('USER_REJECTED', 'Rejected on device');
    switch (req.type) {
      case 'get-accounts': {
        const k = this.walletKeys(req.wallet);
        if (!SCHEMES.includes(req.scheme)) throw new Refused('INVALID_REQUEST', 'unknown scheme');
        return ok({ accounts: req.accounts.map((a) => k.accountKey(req.scheme, isBtc(req.scheme) ? req.net : 'main', a)) });
      }
      case 'show-address': {
        const address = this.walletKeys(req.wallet).address(req.scheme, req.net, req.account, req.change, req.index);
        const confirmed = await this.ask(req.id, [
          { title: 'Receive', lines: [...(address.match(/.{1,21}/g) ?? []), `${req.scheme} #${req.account}${isBtc(req.scheme) ? ` /${req.change}/${req.index}` : ''}`, req.net === 'test' ? 'TESTNET' : ''] },
          { title: 'Matches the app?', lines: ['Confirm only if the', 'address on your phone', 'is exactly the same.'] },
        ]);
        return confirmed ? ok({ address, confirmed: true }) : reject();
      }
      case 'sign-btc': {
        const a = analyzeBtc(this.walletKeys(req.wallet), req.net, req.psbt);
        this.checkPlanStep(req.wallet, req.planStep, a);
        return (await this.ask(req.id, a.pages)) ? ok(await a.sign()) : reject();
      }
      case 'sign-evm': {
        const a = analyzeEvm(this.walletKeys(req.wallet), req, this.st.blindSigning);
        this.checkPlanStep(req.wallet, req.planStep, a);
        return (await this.ask(req.id, a.pages)) ? ok(await a.sign()) : reject();
      }
      case 'sign-sol': {
        const a = analyzeSol(this.walletKeys(req.wallet), req, this.st.blindSigning);
        this.checkPlanStep(req.wallet, req.planStep, a);
        return (await this.ask(req.id, a.pages)) ? ok(await a.sign()) : reject();
      }
      case 'sweep-plan': {
        if (!this.sweepKeys) throw new Refused('INVALID_REQUEST', 'no imported seed to sweep');
        for (const s of req.plan.steps) if (!this.ownsActive(s.to)) throw new Refused('INVALID_REQUEST', `destination ${s.to.slice(0, 12)}… is not your new wallet`);
        const pages: Page[] = req.plan.steps.map((s, i) => ({ title: `Sweep ${i + 1}/${req.plan.steps.length}`, lines: [`${s.amount} ${s.asset}`, s.chain, 'to YOUR new wallet', ...(s.to.match(/.{1,21}/g) ?? []), `fee ≈ ${s.feeEstimate}`] }));
        const approved = await this.ask(req.id, pages, { pin: true });
        if (!approved) return reject();
        const hash = bytesToHex(sha256(new TextEncoder().encode(JSON.stringify(req.plan))));
        this.approvedPlan = { hash, steps: req.plan.steps };
        return ok({ approved: true, planHash: hash });
      }
      case 'forget-sweep-source': {
        if (!this.sweepKeys) return ok({ erased: true });
        const yes = await this.ask(req.id, [{ title: 'Erase old seed?', lines: ['The imported seed will', 'be deleted from this', 'device forever.'], tone: 'warn' }], { pin: true });
        if (!yes) return reject();
        this.st.sweepSource = undefined;
        this.approvedPlan = null;
        this.save();
        this.loadKeys();
        return ok({ erased: true });
      }
      default:
        throw new Refused('INVALID_REQUEST', 'unknown request');
    }
  }
}
