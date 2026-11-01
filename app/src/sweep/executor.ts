// Sweep execution: one transaction at a time, each simulated, signed on the
// device, broadcast, and confirmed on-chain before the next. State is saved
// after every transition so an interrupted sweep resumes where it stopped.
import { SimulationError } from '../chains/types';
import { executableSteps, type SweepPlan, type SweepStep } from './planner';

/** Chain-specific operations, injected so the executor is testable offline. */
export interface SweepOps {
  /** Build + simulate. Throws SimulationError if the transfer would fail. */
  prepare(step: SweepStep): Promise<unknown>;
  /** Device signs (user confirms on the device), then broadcast. Returns tx hash. */
  signAndBroadcast(step: SweepStep, prepared: unknown): Promise<string>;
  confirmed(step: SweepStep, txHash: string): Promise<'pending' | 'confirmed' | 'failed'>;
}

export interface RunOptions {
  save: (plan: SweepPlan) => Promise<void>;
  onUpdate: (plan: SweepPlan) => void;
  pollMs?: number;
  maxPolls?: number;
  signal?: { aborted: boolean };
}

function update(plan: SweepPlan, id: string, patch: Partial<SweepStep>): SweepPlan {
  return {
    ...plan,
    groups: plan.groups.map((g) => ({ ...g, steps: g.steps.map((s) => (s.id === id ? { ...s, ...patch } : s)) })),
  };
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

export async function runSweep(start: SweepPlan, ops: SweepOps, opt: RunOptions): Promise<SweepPlan> {
  let plan = start;
  const commit = async (p: SweepPlan) => {
    plan = p;
    await opt.save(plan);
    opt.onUpdate(plan);
  };

  for (const original of executableSteps(start)) {
    if (opt.signal?.aborted) break;
    const step = executableSteps(plan).find((s) => s.id === original.id)!;
    if (step.status === 'confirmed' || step.status === 'skipped') continue;
    // A failed token transfer must not block the rest of its chain, but a
    // failed native transfer is final for that chain.
    if (step.status === 'failed') continue;

    let txHash = step.txHash;
    if (step.status !== 'broadcast' || !txHash) {
      let prepared: unknown;
      try {
        prepared = await ops.prepare(step);
        await commit(update(plan, step.id, { status: 'simulated', error: undefined }));
      } catch (e) {
        const msg = (e as Error).message;
        if (e instanceof SimulationError && step.holding.kind === 'token') {
          // Spec: tokens that fail a simulated transfer are listed for manual handling.
          await commit({
            ...update(plan, step.id, { status: 'skipped', error: msg }),
            manual: [...plan.manual, { holding: step.holding, reason: 'simulation-failed', detail: msg }],
          });
          continue;
        }
        await commit(update(plan, step.id, { status: 'failed', error: msg }));
        continue;
      }
      try {
        txHash = await ops.signAndBroadcast(step, prepared);
      } catch (e) {
        await commit(update(plan, step.id, { status: 'failed', error: (e as Error).message }));
        break; // user rejected on device or broadcast failed: stop and let the user decide
      }
      await commit(update(plan, step.id, { status: 'broadcast', txHash }));
    }

    // Wait for on-chain confirmation before moving on.
    let polls = 0;
    for (;;) {
      if (opt.signal?.aborted) return plan;
      const st = await ops.confirmed(step, txHash!).catch(() => 'pending' as const);
      if (st === 'confirmed') {
        await commit(update(plan, step.id, { status: 'confirmed' }));
        break;
      }
      if (st === 'failed') {
        await commit(update(plan, step.id, { status: 'failed', error: 'Transaction failed on-chain' }));
        break;
      }
      if (++polls >= (opt.maxPolls ?? 120)) return plan; // still pending: resume later
      await sleep(opt.pollMs ?? 10_000);
    }
  }
  return plan;
}

export function progress(plan: SweepPlan) {
  const steps = executableSteps(plan);
  const done = steps.filter((s) => ['confirmed', 'skipped', 'failed'].includes(s.status)).length;
  return { done, total: steps.length, finished: done === steps.length };
}
