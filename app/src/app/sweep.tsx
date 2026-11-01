import { useEffect, useRef, useState } from 'react';
import { Linking } from 'react-native';

import { CHAINS, explorerTx, type ChainId } from '../chains/config';
import { btcAddress } from '../chains/btcDerive';
import { progress, runSweep } from '../sweep/executor';
import { estimateFees, sweepOps } from '../sweep/ops';
import { executableSteps, planSummary, planSweep, skipGroup, type SweepPlan } from '../sweep/planner';
import { scanWallet } from '../state/scan';
import { loadJson, removeJson, saveJson, secureKV } from '../state/storage';
import { accountId, fetchAccounts, loadPortfolio, type Portfolio, type WalletAccount } from '../state/wallet';
import { useWallet } from '../state/WalletContext';
import { Button, Card, Row, Screen, T } from '../ui/components';
import { formatAmount, formatUsd } from '../util/amount';

const KEY = 'sweep.active';
const STATUS_ICON: Record<string, string> = { pending: '○', simulated: '◔', signed: '◑', broadcast: '◕', confirmed: '✓', failed: '✗', skipped: '–' };

export default function Sweep() {
  const w = useWallet();
  const [plan, setPlan] = useState<SweepPlan | null>(null);
  const [approved, setApproved] = useState(false);
  const [source, setSource] = useState<{ accounts: WalletAccount[]; portfolio: Portfolio } | null>(null);
  const [msg, setMsg] = useState('');
  const [busy, setBusy] = useState(false);
  const [running, setRunning] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const planRef = useRef<SweepPlan | null>(null);
  const abort = useRef({ aborted: false });
  useEffect(() => {
    planRef.current = plan;
  }, [plan]);

  useEffect(() => {
    void loadJson<{ plan: SweepPlan; approved: boolean }>(secureKV, KEY).then((s) => {
      if (s && s.plan.net === w.settings.net) {
        setPlan(s.plan);
        setApproved(s.approved);
      }
    });
    return () => {
      abort.current.aborted = true;
    };
  }, [w.settings.net]);

  const guard = async (f: () => Promise<void>) => {
    setBusy(true);
    setErr(null);
    try {
      await f();
    } catch (e) {
      setErr((e as Error).message);
    } finally {
      setBusy(false);
      setMsg('');
    }
  };

  const scanAndPlan = () =>
    guard(async () => {
      const client = w.device!.client;
      const net = w.settings.net;
      const scanned = await scanWallet(client, net, 'sweep-source', setMsg);
      setSource({ accounts: scanned.accounts, portfolio: scanned.portfolio });

      setMsg('Getting your NEW wallet addresses from the device…');
      const dest = await fetchAccounts(client, net, ['btc-p2wpkh', 'evm-bip44', 'sol-bip44-change'], [0], 'active');
      const btcAcct = dest.find((d) => d.family === 'btc')!;
      const btcState = (await loadPortfolio([btcAcct], net, { btcGap: 10 })).btc[accountId(btcAcct)];
      const evm = dest.find((d) => d.family === 'evm')!.key;
      const sol = dest.find((d) => d.family === 'sol')!.key;
      const destinations: Partial<Record<ChainId, string>> = {
        btc: btcAcct.key.kind === 'xpub' ? btcAddress(btcAcct.key, 'btc-p2wpkh', net, 0, btcState?.nextReceive ?? 0) : undefined,
        sol: sol.kind === 'address' ? sol.address : undefined,
      };
      for (const c of Object.values(CHAINS)) if (c.family === 'evm' && evm.kind === 'address') destinations[c.id] = evm.address;

      setMsg('Estimating fees…');
      const chains = [...new Set(scanned.portfolio.holdings.map((h) => h.chain))];
      const feePerTx = await estimateFees(net, chains);
      const p = planSweep({ holdings: scanned.portfolio.holdings, destinations, feePerTx, dustUsd: w.settings.dustUsd, net });
      setPlan(p);
      setApproved(false);
    });

  const approve = () =>
    guard(async () => {
      if (!plan) return;
      const r = await w.device!.client.request({ type: 'sweep-plan', plan: planSummary(plan) });
      if (!r.approved) throw new Error('Plan not approved on device');
      setApproved(true);
      await saveJson(secureKV, KEY, { plan, approved: true });
    });

  const execute = async () => {
    if (!plan || !w.device) return;
    setRunning(true);
    setErr(null);
    abort.current = { aborted: false };
    try {
      let src = source;
      if (!src) {
        // Resuming after a restart: rescan the old wallet for fresh UTXOs / balances.
        const s = await scanWallet(w.device.client, w.settings.net, 'sweep-source', setMsg);
        src = { accounts: s.accounts, portfolio: s.portfolio };
        setSource(src);
        setMsg('');
      }
      const end = await runSweep(plan, sweepOps(w.device.client, w.settings.net, src.accounts, src.portfolio, () => planRef.current!), {
        save: (p) => saveJson(secureKV, KEY, { plan: p, approved: true }),
        onUpdate: setPlan,
        signal: abort.current,
      });
      setPlan(end);
    } catch (e) {
      setErr((e as Error).message);
    } finally {
      setRunning(false);
    }
  };

  const forgetOld = () =>
    guard(async () => {
      const r = await w.device!.client.request({ type: 'forget-sweep-source' });
      if (r.erased) {
        await removeJson(secureKV, KEY);
        setPlan(null);
        setApproved(false);
      }
    });

  const prog = plan ? progress(plan) : null;
  const net = w.settings.net;

  return (
    <Screen>
      <T kind="h2">Import and sweep to a new wallet</T>
      <Card>
        <T>1. On the device: import the old seed and choose “Import and sweep”.</T>
        <T>2. The device creates a brand-new seed. Write it down and pass the word check (or make a microSD backup).</T>
        <T>3. Here: scan the old seed, review the plan, approve it on the device with your PIN.</T>
        <T>4. Each transfer is simulated, confirmed on the device, and confirmed on-chain before the next one starts.</T>
      </Card>
      {!w.device ? <T kind="warn">Connect your device first.</T> : !w.device.info.hasSweepSource ? <T kind="warn">The device has no imported seed to sweep.</T> : null}

      {!plan ? (
        <Button title="Scan old wallet & build plan" onPress={scanAndPlan} loading={busy} disabled={!w.device?.info.hasSweepSource} />
      ) : null}
      {msg ? <T kind="muted">{msg}</T> : null}
      {err ? <T kind="error">{err}</T> : null}

      {plan ? (
        <>
          <Card>
            <T kind="h2">Plan</T>
            <T kind="muted">
              {executableSteps(plan).length} transfers{net === 'main' ? ` · ≈ ${formatUsd(plan.totalUsd)}` : ''} · dust threshold {formatUsd(w.settings.dustUsd)}
            </T>
            {plan.groups.map((g, gi) => (
              <Card key={gi} style={{ padding: 10 }}>
                <T kind="h2">{CHAINS[g.chain].net[net].label}</T>
                <T kind="muted">from {g.owner.length > 20 ? `${g.owner.slice(0, 8)}…${g.owner.slice(-6)}` : g.owner}</T>
                {g.status === 'needs-top-up' ? (
                  <>
                    <T kind="warn">
                      Not enough {CHAINS[g.chain].symbol} to pay fees for the token transfers (short by {formatAmount(g.shortfall ?? 0n, CHAINS[g.chain].decimals)} {CHAINS[g.chain].symbol}). Top up the old address and rescan, or skip this chain.
                    </T>
                    {!approved ? <Button kind="secondary" title="Skip this chain" onPress={() => setPlan(skipGroup(plan, gi))} /> : null}
                  </>
                ) : null}
                {g.steps.map((s) => (
                  <Row
                    key={s.id}
                    left={`${STATUS_ICON[s.status]} ${s.holding.symbol}${s.holding.kind === 'native' ? ' (native, last)' : ''}`}
                    sub={s.error ?? `to ${s.to.slice(0, 10)}…${s.to.slice(-6)}`}
                    right={s.amount === 'max' ? `all ≈ ${formatAmount(s.holding.amount, s.holding.decimals, 6)}` : formatAmount(s.amount, s.holding.decimals, 6)}
                    onPress={s.txHash ? () => void Linking.openURL(explorerTx(s.chain, net, s.txHash!)) : undefined}
                  />
                ))}
              </Card>
            ))}
          </Card>
          {plan.manual.length ? (
            <Card>
              <T kind="h2">Handle manually</T>
              <T kind="muted">Never moved automatically: staked funds, DeFi/LP positions, NFTs, and tokens whose transfer failed simulation.</T>
              {plan.manual.map((m, i) => (
                <Row key={i} left={`${m.holding.symbol} on ${CHAINS[m.holding.chain].name}`} sub={m.detail ?? m.reason} right={formatAmount(m.holding.amount, m.holding.decimals, 6)} />
              ))}
            </Card>
          ) : null}
          {plan.ignored.length ? (
            <Card>
              <T kind="h2">Ignored</T>
              {plan.ignored.map((m, i) => (
                <Row key={i} left={`${m.holding.symbol} on ${CHAINS[m.holding.chain].name}`} sub={m.reason === 'dust' ? 'below dust threshold' : m.reason === 'no-price' ? 'no price (likely spam)' : 'worth less than the fee'} />
              ))}
            </Card>
          ) : null}

          {!approved ? (
            <>
              <Button title="Approve plan on device (PIN + Confirm)" onPress={approve} loading={busy} disabled={!executableSteps(plan).length} />
              <Button kind="secondary" title="Discard plan" onPress={() => setPlan(null)} />
            </>
          ) : prog && !prog.finished ? (
            <>
              <T kind="muted">Progress: {prog.done} / {prog.total}</T>
              <Button title={running ? 'Sweeping…' : prog.done ? 'Resume sweep' : 'Start sweep'} onPress={execute} loading={running} />
              {running ? <Button kind="secondary" title="Pause after current step" onPress={() => (abort.current.aborted = true)} /> : null}
            </>
          ) : (
            <Card>
              <T kind="ok">Sweep finished ✓</T>
              <T>Check that everything arrived in your new wallet. Then remove the old seed from the device: it will not be kept.</T>
              <Button kind="danger" title="Erase old seed from device" onPress={forgetOld} loading={busy} />
            </Card>
          )}
        </>
      ) : null}
    </Screen>
  );
}
