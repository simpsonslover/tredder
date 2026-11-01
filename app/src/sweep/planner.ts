// Sweep planning (pure): turn the holdings found on an imported seed into an
// ordered list of transfers to the new wallet, following the spec:
//  - per chain and source wallet: tokens first, native coin LAST (balance - fees)
//  - never auto-move staked funds, LP positions, NFTs -> "manual" list
//  - skip dust / spam / unpriced tokens below the user's USD threshold
//  - flag chains whose native balance can't pay for the token transfers
import type { ChainId } from '../chains/config';
import { CHAINS } from '../chains/config';
import type { Holding } from '../chains/types';
import type { Net, SweepPlanSummary } from '../protocol/types';
import { formatAmount } from '../util/amount';

export type StepStatus = 'pending' | 'simulated' | 'signed' | 'broadcast' | 'confirmed' | 'failed' | 'skipped';

export interface SweepStep {
  id: string;
  chain: ChainId;
  holding: Holding; // what moves (amount = full balance at scan time)
  amount: bigint | 'max'; // 'max' = native: balance minus fees at execution time
  to: string;
  feeEstimate: bigint; // native base units, worst case
  status: StepStatus;
  txHash?: string;
  error?: string;
}

export interface ChainGroup {
  chain: ChainId;
  owner: string; // source address (BTC: scheme/account label)
  steps: SweepStep[];
  nativeBalance: bigint;
  feesNeeded: bigint; // for the token steps (+ native step if any)
  status: 'ok' | 'needs-top-up' | 'skipped';
  shortfall?: bigint;
}

export type ManualReason = 'staked' | 'lp' | 'nft' | 'simulation-failed';
export type IgnoredReason = 'dust' | 'no-price' | 'below-fee';

export interface SweepPlan {
  id: string;
  net: Net;
  createdAt: number;
  groups: ChainGroup[];
  manual: { holding: Holding; reason: ManualReason; detail?: string }[];
  ignored: { holding: Holding; reason: IgnoredReason }[];
  totalUsd: number;
}

export interface PlanInput {
  holdings: Holding[];
  destinations: Partial<Record<ChainId, string>>; // new wallet's address per chain
  feePerTx: Partial<Record<ChainId, bigint>>; // worst-case fee per transfer, native units
  dustUsd: number;
  net: Net;
  id?: string;
  now?: number;
}

const groupKey = (h: Holding) => `${h.chain}|${h.chain === 'btc' ? `${h.scheme}/${h.account}` : h.owner}`;

export function planSweep(input: PlanInput): SweepPlan {
  const plan: SweepPlan = {
    id: input.id ?? `sweep-${(input.now ?? Date.now()).toString(36)}`,
    net: input.net,
    createdAt: input.now ?? Date.now(),
    groups: [],
    manual: [],
    ignored: [],
    totalUsd: 0,
  };
  const byGroup = new Map<string, Holding[]>();
  for (const h of input.holdings) {
    if (h.amount <= 0n) continue;
    if (h.kind === 'staked' || h.kind === 'lp' || h.kind === 'nft') {
      plan.manual.push({ holding: h, reason: h.kind });
      continue;
    }
    if (h.kind === 'token') {
      // Testnet tokens have no price: don't treat them as spam there.
      if (h.usdValue === undefined && input.net === 'main') {
        plan.ignored.push({ holding: h, reason: 'no-price' });
        continue;
      }
      if (h.usdValue !== undefined && h.usdValue < input.dustUsd) {
        plan.ignored.push({ holding: h, reason: 'dust' });
        continue;
      }
    }
    const k = groupKey(h);
    byGroup.set(k, [...(byGroup.get(k) ?? []), h]);
  }

  let n = 0;
  for (const [k, hs] of byGroup) {
    const chain = hs[0]!.chain;
    const to = input.destinations[chain];
    const fee = input.feePerTx[chain];
    if (!to || fee === undefined) throw new Error(`No destination / fee estimate for ${chain}`);
    const native = hs.find((h) => h.kind === 'native');
    const tokens = hs.filter((h) => h.kind === 'token');
    const nativeBalance = native?.amount ?? 0n;
    const tokenFees = fee * BigInt(tokens.length);
    const steps: SweepStep[] = tokens.map((h) => ({
      id: `${plan.id}-${n++}`,
      chain,
      holding: h,
      amount: h.amount,
      to,
      feeEstimate: fee,
      status: 'pending',
    }));
    const group: ChainGroup = { chain, owner: k.split('|')[1]!, steps, nativeBalance, feesNeeded: tokenFees, status: 'ok' };

    if (nativeBalance < tokenFees) {
      group.status = 'needs-top-up';
      group.shortfall = tokenFees - nativeBalance;
    } else if (native && nativeBalance - tokenFees > fee) {
      // Native coin goes last: whatever is left after every token transfer's fee.
      steps.push({ id: `${plan.id}-${n++}`, chain, holding: native, amount: 'max', to, feeEstimate: fee, status: 'pending' });
      group.feesNeeded += fee;
    } else if (native) {
      plan.ignored.push({ holding: native, reason: 'below-fee' });
    }
    if (steps.length || group.status === 'needs-top-up') plan.groups.push(group);
  }
  plan.totalUsd = plan.groups.flatMap((g) => g.steps).reduce((s, st) => s + (st.holding.usdValue ?? 0), 0);
  return plan;
}

/** User chose to skip a chain that needs a top-up. */
export function skipGroup(plan: SweepPlan, index: number): SweepPlan {
  const groups = plan.groups.map((g, i) =>
    i === index ? { ...g, status: 'skipped' as const, steps: g.steps.map((s) => ({ ...s, status: 'skipped' as const })) } : g,
  );
  return { ...plan, groups };
}

export function executableSteps(plan: SweepPlan): SweepStep[] {
  return plan.groups.filter((g) => g.status === 'ok').flatMap((g) => g.steps);
}

/** What the device shows for PIN + Confirm (protocol `sweep-plan`). */
export function planSummary(plan: SweepPlan): SweepPlanSummary {
  return {
    id: plan.id,
    steps: executableSteps(plan).map((s) => ({
      chain: CHAINS[s.chain].net[plan.net].label,
      asset: s.holding.symbol,
      amount: s.amount === 'max' ? `all (≈${formatAmount(s.holding.amount, s.holding.decimals)})` : formatAmount(s.amount, s.holding.decimals),
      to: s.to,
      feeEstimate: `${formatAmount(s.feeEstimate, CHAINS[s.chain].decimals)} ${CHAINS[s.chain].symbol}`,
    })),
  };
}
