// Real chain operations for the sweep executor + fee estimates for the planner.
import { feeRates } from '../chains/btc';
import { CHAIN_LIST, type ChainId } from '../chains/config';
import { evmClient } from '../chains/evm';
import type { DeviceClient } from '../protocol/client';
import type { Net } from '../protocol/types';
import { accountId, prepareSend, signAndBroadcast, txStatus, type Portfolio, type Prepared, type WalletAccount } from '../state/wallet';
import type { SweepOps } from './executor';
import { executableSteps, type SweepPlan, type SweepStep } from './planner';

const SOL_TX_FEE = 5_000n;
const SOL_ATA_RENT = 2_039_280n; // creating the destination token account

/** Worst-case native fee per transfer, per chain (planner input). */
export async function estimateFees(net: Net, chains: ChainId[]): Promise<Partial<Record<ChainId, bigint>>> {
  const out: Partial<Record<ChainId, bigint>> = {};
  for (const id of new Set(chains)) {
    if (id === 'btc') out.btc = BigInt((await feeRates(net)).normal) * 250n; // ~2 inputs + 1 output, generous
    else if (id === 'sol') out.sol = SOL_TX_FEE + SOL_ATA_RENT;
    else if (CHAIN_LIST.find((c) => c.id === id)?.family === 'evm') {
      const f = await evmClient(id, net).estimateFeesPerGas();
      out[id] = f.maxFeePerGas * 100_000n; // ERC-20 transfer gas, with margin
    }
  }
  return out;
}

export function sweepOps(client: DeviceClient, net: Net, source: WalletAccount[], portfolio: Portfolio, plan: () => SweepPlan): SweepOps {
  const accountFor = (s: SweepStep) => {
    const a = source.find((x) => x.scheme === s.holding.scheme && x.account === s.holding.account);
    if (!a) throw new Error(`source account ${accountId(s.holding)} not found`);
    return a;
  };
  return {
    async prepare(step) {
      return prepareSend({ holding: step.holding, account: accountFor(step), net, to: step.to, amount: step.amount, portfolio });
    },
    async signAndBroadcast(step, prepared) {
      const index = executableSteps(plan()).findIndex((s) => s.id === step.id);
      return signAndBroadcast(client, prepared as Prepared, 'sweep-source', index);
    },
    async confirmed(step, hash) {
      return txStatus(step.chain, net, hash);
    },
  };
}
