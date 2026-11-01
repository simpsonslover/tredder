import type { Holding } from '../../chains/types';
import { SimulationError } from '../../chains/types';
import { progress, runSweep, type SweepOps } from '../executor';
import { executableSteps, planSummary, planSweep, skipGroup, type SweepPlan } from '../planner';

const H = (p: Partial<Holding> & Pick<Holding, 'chain' | 'kind' | 'amount'>): Holding => ({
  net: 'main',
  scheme: p.chain === 'btc' ? 'btc-p2wpkh' : p.chain === 'sol' ? 'sol-bip44-change' : 'evm-bip44',
  account: 0,
  owner: p.chain === 'sol' ? 'SoLOwner' : '0xOLD',
  asset: p.kind === 'native' || p.kind === 'staked' ? { kind: 'native' } : { kind: 'token', address: `0x${p.symbol ?? 'tok'}`, standard: 'erc20' },
  symbol: p.kind === 'native' ? 'ETH' : 'TOK',
  name: 'x',
  decimals: 18,
  usdValue: 100,
  ...p,
});

const base = {
  destinations: { eth: '0xNEW', sol: 'SoLNew', btc: 'bc1qnew', base: '0xNEW' },
  feePerTx: { eth: 1000n, sol: 5000n, btc: 2000n, base: 10n },
  dustUsd: 1,
  net: 'main' as const,
  id: 'p',
  now: 1,
};

describe('planSweep', () => {
  it('orders tokens first and native last, per chain', () => {
    const plan = planSweep({
      ...base,
      holdings: [
        H({ chain: 'eth', kind: 'native', amount: 10_000n }),
        H({ chain: 'eth', kind: 'token', amount: 5n, symbol: 'USDC' }),
        H({ chain: 'eth', kind: 'token', amount: 7n, symbol: 'PEPE' }),
      ],
    });
    const steps = executableSteps(plan);
    expect(steps.map((s) => s.holding.kind)).toEqual(['token', 'token', 'native']);
    expect(steps[2]!.amount).toBe('max');
    expect(plan.groups[0]!.feesNeeded).toBe(3000n);
    expect(steps.every((s) => s.to === '0xNEW')).toBe(true);
  });

  it('flags a chain whose native coin cannot pay token fees', () => {
    const plan = planSweep({
      ...base,
      holdings: [H({ chain: 'eth', kind: 'native', amount: 1500n }), H({ chain: 'eth', kind: 'token', amount: 5n }), H({ chain: 'eth', kind: 'token', amount: 6n, symbol: 'B' })],
    });
    expect(plan.groups[0]).toMatchObject({ status: 'needs-top-up', shortfall: 500n });
    expect(executableSteps(plan)).toHaveLength(0);
    const skipped = skipGroup(plan, 0);
    expect(skipped.groups[0]!.status).toBe('skipped');
  });

  it('drops the native step when only fees remain', () => {
    const plan = planSweep({ ...base, holdings: [H({ chain: 'eth', kind: 'native', amount: 1500n }), H({ chain: 'eth', kind: 'token', amount: 5n })] });
    expect(executableSteps(plan).map((s) => s.holding.kind)).toEqual(['token']);
    expect(plan.ignored.map((i) => i.reason)).toEqual(['below-fee']);
  });

  it('never auto-moves staked, LP or NFTs; ignores dust and unpriced spam', () => {
    const plan = planSweep({
      ...base,
      holdings: [
        H({ chain: 'sol', kind: 'staked', amount: 5n }),
        H({ chain: 'eth', kind: 'lp', amount: 5n }),
        H({ chain: 'eth', kind: 'nft', amount: 1n }),
        H({ chain: 'eth', kind: 'token', amount: 5n, usdValue: 0.3 }),
        H({ chain: 'eth', kind: 'token', amount: 5n, usdValue: undefined, symbol: 'SPAM' }),
      ],
    });
    expect(plan.manual.map((m) => m.reason).sort()).toEqual(['lp', 'nft', 'staked']);
    expect(plan.ignored.map((i) => i.reason).sort()).toEqual(['dust', 'no-price']);
    expect(executableSteps(plan)).toHaveLength(0);
  });

  it('keeps separate groups per source account and chain; BTC per scheme/account', () => {
    const plan = planSweep({
      ...base,
      holdings: [
        H({ chain: 'btc', kind: 'native', amount: 100_000n, scheme: 'btc-p2wpkh' }),
        H({ chain: 'btc', kind: 'native', amount: 50_000n, scheme: 'btc-p2tr' }),
        H({ chain: 'base', kind: 'native', amount: 1000n }),
        H({ chain: 'eth', kind: 'native', amount: 9000n, owner: '0xOTHER', account: 1 }),
      ],
    });
    expect(plan.groups.map((g) => g.chain).sort()).toEqual(['base', 'btc', 'btc', 'eth']);
  });

  it('summarises for the device', () => {
    const plan = planSweep({ ...base, holdings: [H({ chain: 'eth', kind: 'native', amount: 10_000_000_000_000_000n })] });
    const s = planSummary(plan);
    expect(s.steps[0]).toMatchObject({ chain: 'Ethereum', asset: 'ETH', to: '0xNEW' });
    expect(s.steps[0]!.amount).toMatch(/^all/);
  });
});

describe('runSweep', () => {
  const mkPlan = () =>
    planSweep({
      ...base,
      holdings: [
        H({ chain: 'eth', kind: 'native', amount: 10_000n }),
        H({ chain: 'eth', kind: 'token', amount: 5n, symbol: 'GOOD' }),
        H({ chain: 'eth', kind: 'token', amount: 6n, symbol: 'TAX' }),
      ],
    });

  const run = (plan: SweepPlan, ops: Partial<SweepOps>, saved: SweepPlan[] = []) =>
    runSweep(
      plan,
      {
        prepare: async () => ({}),
        signAndBroadcast: async (s) => `tx-${s.holding.symbol}`,
        confirmed: async () => 'confirmed',
        ...ops,
      },
      { save: async (p) => void saved.push(p), onUpdate: () => {}, pollMs: 0 },
    );

  it('confirms each tx before the next, native last', async () => {
    const order: string[] = [];
    const end = await run(mkPlan(), {
      signAndBroadcast: async (s) => {
        order.push(s.holding.symbol);
        return `tx-${s.holding.symbol}`;
      },
    });
    expect(order).toEqual(['GOOD', 'TAX', 'ETH']);
    expect(progress(end)).toEqual({ done: 3, total: 3, finished: true });
  });

  it('moves transfer-blocking tokens to the manual list and continues', async () => {
    const end = await run(mkPlan(), {
      prepare: async (s) => {
        if (s.holding.symbol === 'TAX') throw new SimulationError('transfer blocked');
        return {};
      },
    });
    expect(end.manual).toEqual([expect.objectContaining({ reason: 'simulation-failed', detail: 'transfer blocked' })]);
    expect(executableSteps(end).map((s) => s.status)).toEqual(['confirmed', 'skipped', 'confirmed']);
  });

  it('stops when the user rejects on the device', async () => {
    const end = await run(mkPlan(), {
      signAndBroadcast: async () => {
        throw new Error('Rejected on device');
      },
    });
    expect(executableSteps(end).map((s) => s.status)).toEqual(['failed', 'pending', 'pending']);
  });

  it('resumes an interrupted sweep without repeating confirmed or broadcast steps', async () => {
    const saved: SweepPlan[] = [];
    let polls = 0;
    // First run: first tx confirms, second gets broadcast then the app "dies".
    await runSweep(
      mkPlan(),
      {
        prepare: async () => ({}),
        signAndBroadcast: async (s) => `tx-${s.holding.symbol}`,
        confirmed: async (s) => (s.holding.symbol === 'GOOD' ? 'confirmed' : (polls++, 'pending')),
      },
      { save: async (p) => void saved.push(p), onUpdate: () => {}, pollMs: 0, maxPolls: 2 },
    );
    expect(polls).toBe(2); // gave up waiting after maxPolls, state persisted
    const persisted = saved.at(-1)!;
    expect(executableSteps(persisted).map((s) => s.status)).toEqual(['confirmed', 'broadcast', 'pending']);

    const signed: string[] = [];
    const end = await run(persisted, {
      signAndBroadcast: async (s) => {
        signed.push(s.holding.symbol);
        return `tx-${s.holding.symbol}`;
      },
    });
    expect(signed).toEqual(['ETH']); // TAX was already broadcast: only polled, not re-signed
    expect(progress(end).finished).toBe(true);
  });
});
