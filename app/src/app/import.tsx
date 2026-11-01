import { router } from 'expo-router';
import { useState } from 'react';
import { Switch } from 'react-native';

import { SCHEME_LABEL } from '../chains/config';
import { scanWallet, type FoundAccount } from '../state/scan';
import { accountId } from '../state/wallet';
import { useWallet } from '../state/WalletContext';
import { Button, Card, Row, Screen, T } from '../ui/components';
import { formatAmount, formatUsd } from '../util/amount';

export default function ImportScan() {
  const w = useWallet();
  const [found, setFound] = useState<FoundAccount[] | null>(null);
  const [pick, setPick] = useState<Set<string>>(new Set(w.accounts.map(accountId)));
  const [msg, setMsg] = useState('');
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);

  const scan = async () => {
    if (!w.device) return;
    setBusy(true);
    setErr(null);
    try {
      const r = await scanWallet(w.device.client, w.settings.net, 'active', setMsg);
      setFound(r.found);
      setPick((cur) => new Set([...cur, ...r.found.filter((f) => f.used).map((f) => accountId(f.account))]));
    } catch (e) {
      setErr((e as Error).message);
    } finally {
      setBusy(false);
      setMsg('');
    }
  };

  const save = async () => {
    const all = [...w.accounts, ...(found ?? []).map((f) => f.account)];
    await w.setAccounts(all.filter((a) => pick.has(accountId(a))));
    await w.refresh();
    router.back();
  };

  return (
    <Screen>
      <T kind="h2">Find accounts</T>
      <T kind="muted">
        After importing a seed phrase on the device, scan every derivation path that popular wallets use (Bitcoin legacy, SegWit, Taproot; MetaMask and Ledger Live; Phantom, Solflare and older Solana paths) and choose which accounts to show.
      </T>
      <Card>
        <T kind="warn">
          ⚠ If this seed phrase was ever typed into a phone or computer, it may already be exposed. Use “Sweep to new wallet” instead: it moves everything to a fresh seed created on the device.
        </T>
        <Button kind="secondary" title="Sweep to new wallet instead (recommended)" onPress={() => router.replace('/sweep')} />
      </Card>
      <Button title={w.device ? 'Scan all paths' : 'Connect device first'} onPress={scan} loading={busy} disabled={!w.device} />
      {msg ? <T kind="muted">{msg}</T> : null}
      {err ? <T kind="error">{err}</T> : null}
      {found ? (
        <Card>
          {found.map((f) => {
            const id = accountId(f.account);
            return (
              <Row
                key={id}
                left={`${SCHEME_LABEL[f.account.scheme]} · account ${f.account.account}`}
                sub={
                  f.holdings.length
                    ? f.holdings.map((h) => `${formatAmount(h.amount, h.decimals, 6)} ${h.symbol}`).join(', ')
                    : f.used
                      ? 'used, empty now'
                      : 'empty'
                }
                right={
                  <>
                    <T kind="muted">{w.settings.net === 'test' ? '' : formatUsd(f.usd)}</T>
                    <Switch
                      value={pick.has(id)}
                      onValueChange={(v) =>
                        setPick((cur) => {
                          const n = new Set(cur);
                          if (v) n.add(id);
                          else n.delete(id);
                          return n;
                        })
                      }
                    />
                  </>
                }
              />
            );
          })}
          <Button title={`Show ${pick.size} selected accounts`} onPress={save} />
        </Card>
      ) : null}
    </Screen>
  );
}
