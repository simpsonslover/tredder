import { useMemo, useState } from 'react';
import { Linking } from 'react-native';

import { CHAINS, explorerTx } from '../../chains/config';
import { assetKey, type Holding } from '../../chains/types';
import { prepareSend, signAndBroadcast, type Prepared } from '../../state/wallet';
import { useWallet } from '../../state/WalletContext';
import { Button, Card, Choice, Field, Row, Screen, T } from '../../ui/components';
import { formatAmount, parseAmount } from '../../util/amount';

const feeOf = (p: Prepared) =>
  p.family === 'btc' ? { v: p.fee, d: 8, s: 'BTC' } : p.family === 'evm' ? { v: p.p.maxFee, d: 18, s: CHAINS[p.chain].symbol } : { v: p.p.fee, d: 9, s: 'SOL' };

export default function Send() {
  const w = useWallet();
  const sendable = useMemo(() => (w.portfolio?.holdings ?? []).filter((h) => (h.kind === 'native' || h.kind === 'token') && h.amount > 0n), [w.portfolio]);
  const key = (h: Holding) => `${assetKey(h)}|${h.owner}|${h.scheme}#${h.account}`;
  const [sel, setSel] = useState<string>('');
  const [to, setTo] = useState('');
  const [amount, setAmount] = useState('');
  const [max, setMax] = useState(false);
  const [prep, setPrep] = useState<Prepared | null>(null);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const [sent, setSent] = useState<{ hash: string; chain: Holding['chain'] } | null>(null);
  const h = sendable.find((x) => key(x) === sel);
  const acct = h && w.accounts.find((a) => a.scheme === h.scheme && a.account === h.account);

  const review = async () => {
    if (!h || !acct) return;
    setBusy(true);
    setErr(null);
    setPrep(null);
    try {
      // Token "max" is the exact balance; native "max" is resolved after fees.
      const amt = max ? (h.kind === 'token' ? h.amount : 'max') : parseAmount(amount, h.decimals);
      if (amt !== 'max' && amt > h.amount) throw new Error('Amount exceeds balance');
      setPrep(await prepareSend({ holding: h, account: acct, net: w.settings.net, to, amount: amt, portfolio: w.portfolio }));
    } catch (e) {
      setErr((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const confirm = async () => {
    if (!prep || !w.device) return;
    setBusy(true);
    setErr(null);
    try {
      const hash = await signAndBroadcast(w.device.client, prep);
      setSent({ hash, chain: prep.chain });
      setPrep(null);
      setAmount('');
      void w.refresh();
    } catch (e) {
      setErr((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <Screen>
      <T kind="title">Send</T>
      {w.settings.net === 'test' ? <T kind="muted">Testnet mode: coins here have no value.</T> : <T kind="warn">MAINNET: real funds.</T>}
      {!sendable.length ? <T kind="muted">Nothing to send yet. Pull to refresh the portfolio.</T> : null}
      <Choice
        options={sendable.map((x) => ({ value: key(x), label: `${x.symbol} · ${CHAINS[x.chain].net[w.settings.net].label} · ${formatAmount(x.amount, x.decimals, 6)}` }))}
        value={sel}
        onChange={(v) => {
          setSel(v);
          setPrep(null);
          setSent(null);
        }}
      />
      {h ? (
        <Card>
          <Field label="Recipient address" value={to} onChangeText={(v) => (setTo(v), setPrep(null))} placeholder={h.chain === 'btc' ? 'tb1… / bc1…' : h.chain === 'sol' ? 'Solana address' : '0x…'} />
          <Field label={`Amount (${h.symbol})`} value={max ? 'MAX' : amount} editable={!max} onChangeText={(v) => (setAmount(v), setPrep(null))} keyboardType="decimal-pad" placeholder="0.0" />
          <Choice options={[{ value: 'n', label: 'Custom' }, { value: 'y', label: 'Send max' }]} value={max ? 'y' : 'n'} onChange={(v) => (setMax(v === 'y'), setPrep(null))} />
          <Button title="Review (simulate)" onPress={review} loading={busy && !prep} disabled={!to || (!max && !amount)} />
        </Card>
      ) : null}
      {prep ? (
        <Card>
          <T kind="h2">Review</T>
          <Row left="Network" right={CHAINS[prep.chain].net[w.settings.net].label} />
          <Row left="To" sub={prep.to} />
          <Row left="Amount" right={`${formatAmount(prep.amount, h!.decimals)} ${h!.symbol}`} />
          <Row left="Max network fee" right={`${formatAmount(feeOf(prep).v, feeOf(prep).d)} ${feeOf(prep).s}`} />
          <T kind="ok">✓ Simulation passed</T>
          <T kind="muted">Your device will show the same details. Only press Confirm on the device if they match exactly.</T>
          <Button title={w.device ? 'Sign on device & send' : 'Connect device to sign'} onPress={confirm} loading={busy} disabled={!w.device} />
        </Card>
      ) : null}
      {err ? <T kind="error">{err}</T> : null}
      {sent ? (
        <Card>
          <T kind="ok">Sent ✓</T>
          <T kind="mono" selectable>{sent.hash}</T>
          <Button kind="secondary" title="View in explorer" onPress={() => void Linking.openURL(explorerTx(sent.chain, w.settings.net, sent.hash))} />
        </Card>
      ) : null}
    </Screen>
  );
}
