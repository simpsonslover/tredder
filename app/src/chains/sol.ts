// Solana: SOL + all SPL tokens (Token and Token-2022), stake detection,
// unsigned v0 messages for the device, simulateTransaction, broadcast.
import {
  createAssociatedTokenAccountIdempotentInstruction,
  createTransferCheckedInstruction,
  getAssociatedTokenAddressSync,
  TOKEN_2022_PROGRAM_ID,
  TOKEN_PROGRAM_ID,
} from '@solana/spl-token';
import {
  Connection,
  PublicKey,
  StakeProgram,
  SystemProgram,
  TransactionMessage,
  VersionedTransaction,
  type TransactionInstruction,
} from '@solana/web3.js';

import type { Net, Scheme } from '../protocol/types';
import { CHAINS } from './config';
import { SimulationError, type Holding, type TxRecord } from './types';

const conns = new Map<Net, Connection>();
export const solConnection = (net: Net) => {
  let c = conns.get(net);
  if (!c) {
    c = new Connection(CHAINS.sol.net[net].rpc!, 'confirmed');
    conns.set(net, c);
  }
  return c;
};

/** Valid base58 32-byte public key. Off-curve (program-derived) addresses are valid recipients too. */
export function isSolAddress(a: string) {
  try {
    new PublicKey(a);
    return true;
  } catch {
    return false;
  }
}

export async function solHoldings(net: Net, scheme: Scheme, account: number, owner: string): Promise<Holding[]> {
  const c = solConnection(net);
  const pk = new PublicKey(owner);
  const base = { chain: 'sol' as const, net, scheme, account, owner };
  const out: Holding[] = [
    { ...base, asset: { kind: 'native' }, kind: 'native', symbol: 'SOL', name: CHAINS.sol.net[net].label, decimals: 9, amount: BigInt(await c.getBalance(pk)) },
  ];
  for (const [programId, standard] of [
    [TOKEN_PROGRAM_ID, 'spl'],
    [TOKEN_2022_PROGRAM_ID, 'spl-2022'],
  ] as const) {
    const r = await c.getParsedTokenAccountsByOwner(pk, { programId });
    for (const a of r.value) {
      const info = a.account.data.parsed.info as { mint: string; tokenAmount: { amount: string; decimals: number } };
      const amount = BigInt(info.tokenAmount.amount);
      if (amount === 0n) continue;
      const nft = info.tokenAmount.decimals === 0 && amount === 1n;
      out.push({
        ...base,
        asset: { kind: 'token', address: info.mint, standard },
        kind: nft ? 'nft' : 'token',
        symbol: shortMint(info.mint),
        name: 'SPL token',
        decimals: info.tokenAmount.decimals,
        amount,
      });
    }
  }
  // Stake accounts where we are the withdraw authority: shown, never auto-swept.
  const stakes = await c
    .getParsedProgramAccounts(StakeProgram.programId, { filters: [{ memcmp: { offset: 44, bytes: owner } }] })
    .catch(() => []);
  for (const s of stakes)
    out.push({ ...base, owner: s.pubkey.toBase58(), asset: { kind: 'native' }, kind: 'staked', symbol: 'SOL', name: 'Staked SOL', decimals: 9, amount: BigInt(s.account.lamports) });
  return out;
}

const shortMint = (m: string) => `${m.slice(0, 4)}…${m.slice(-4)}`;

export async function solHistory(net: Net, owner: string): Promise<TxRecord[]> {
  const sigs = await solConnection(net).getSignaturesForAddress(new PublicKey(owner), { limit: 20 });
  return sigs.map((s) => ({
    chain: 'sol',
    hash: s.signature,
    time: s.blockTime ?? undefined,
    direction: 'self', // direction needs the parsed tx; shown as activity
    status: s.err ? 'failed' : s.confirmationStatus === 'finalized' || s.confirmationStatus === 'confirmed' ? 'confirmed' : 'pending',
  }));
}

export interface SolPrepared {
  net: Net;
  payer: string;
  tx: VersionedTransaction;
  message: Uint8Array; // bytes the device signs
  fee: bigint; // lamports
}

/** Build + simulate a SOL or SPL transfer. 'max' for SOL leaves only the fee (closes the system account). */
export async function prepareSolTransfer(args: {
  net: Net;
  from: string;
  to: string;
  asset: Holding['asset'];
  amount: bigint | 'max';
  decimals: number;
  reserveLamports?: bigint;
}): Promise<SolPrepared> {
  const c = solConnection(args.net);
  const from = new PublicKey(args.from);
  let to: PublicKey;
  try {
    to = new PublicKey(args.to);
  } catch {
    throw new Error('Invalid Solana address');
  }
  const ixs: TransactionInstruction[] = [];
  if (args.asset.kind === 'token') {
    if (args.amount === 'max') throw new Error('token max must be resolved by caller');
    const programId = args.asset.standard === 'spl-2022' ? TOKEN_2022_PROGRAM_ID : TOKEN_PROGRAM_ID;
    const mint = new PublicKey(args.asset.address);
    const srcAta = getAssociatedTokenAddressSync(mint, from, true, programId);
    const dstAta = getAssociatedTokenAddressSync(mint, to, true, programId);
    ixs.push(
      createAssociatedTokenAccountIdempotentInstruction(from, dstAta, to, mint, programId),
      createTransferCheckedInstruction(srcAta, mint, dstAta, from, args.amount, args.decimals, [], programId),
    );
  } else {
    ixs.push(SystemProgram.transfer({ fromPubkey: from, toPubkey: to, lamports: 1n }));
  }

  const build = async (instructions: TransactionInstruction[]) => {
    const { blockhash } = await c.getLatestBlockhash();
    const msg = new TransactionMessage({ payerKey: from, recentBlockhash: blockhash, instructions }).compileToV0Message();
    return new VersionedTransaction(msg);
  };

  let tx = await build(ixs);
  const fee = BigInt((await c.getFeeForMessage(tx.message)).value ?? 5000);
  if (args.asset.kind === 'native') {
    const lamports =
      args.amount === 'max' ? BigInt(await c.getBalance(from)) - fee - (args.reserveLamports ?? 0n) : args.amount;
    if (lamports <= 0n) throw new SimulationError('Balance does not cover the network fee');
    tx = await build([SystemProgram.transfer({ fromPubkey: from, toPubkey: to, lamports })]);
  }
  const sim = await c.simulateTransaction(tx, { sigVerify: false, replaceRecentBlockhash: true });
  if (sim.value.err) throw new SimulationError(`Simulation failed: ${JSON.stringify(sim.value.err)} ${(sim.value.logs ?? []).slice(-2).join(' ')}`);
  return { net: args.net, payer: args.from, tx, message: tx.message.serialize(), fee };
}

export async function broadcastSol(p: SolPrepared, signature: Uint8Array, signer: string): Promise<string> {
  if (signer !== p.payer) throw new Error('Device signed with an unexpected key');
  p.tx.addSignature(new PublicKey(signer), signature);
  return solConnection(p.net).sendRawTransaction(p.tx.serialize());
}

export async function solConfirmed(net: Net, sig: string): Promise<'pending' | 'confirmed' | 'failed'> {
  const s = (await solConnection(net).getSignatureStatuses([sig])).value[0];
  if (!s) return 'pending';
  if (s.err) return 'failed';
  return s.confirmationStatus === 'confirmed' || s.confirmationStatus === 'finalized' ? 'confirmed' : 'pending';
}
