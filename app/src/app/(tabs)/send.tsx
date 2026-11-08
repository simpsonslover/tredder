/*
 * Send, one step at a time: asset -> recipient -> amount -> review -> sent.
 * The app prepares and simulates the transaction; the tredder shows it and signs it.
 * Nothing here can move funds without the button press on the device.
 */
import Ionicons from '@expo/vector-icons/Ionicons';
import * as Clipboard from 'expo-clipboard';
import { router, useFocusEffect, useLocalSearchParams } from 'expo-router';
import { useCallback, useEffect, useMemo, useState } from 'react';
import { ActivityIndicator, BackHandler, Linking, Pressable, Text, TextInput, View } from 'react-native';

import { CHAINS, explorerTx } from '../../chains/config';
import { assetKey, type Holding } from '../../chains/types';
import { DeviceError } from '../../protocol/types';
import { loadRecents, rememberRecipient, sameAddress, type Recent } from '../../state/recents';
import { familyOf, prepareSend, signAndBroadcast, txStatus, type Prepared } from '../../state/wallet';
import { useWallet } from '../../state/WalletContext';
import { assetLogo, assetSeed, chainLogo } from '../../ui/coinLogos';
import { Badge, Button, Card, Choice, CoinIcon, Row, T, useTheme } from '../../ui/components';
import { AddressText, IconPill, Keypad, Scanner, StepScreen } from '../../ui/flow';
import { formatAmount, formatUsd, parseAmount } from '../../util/amount';
import { asNumber, coinToUsd, pressKey, unitsToInput, unitsToNumber, usdToCoin } from '../../util/amountInput';
import { checkRecipient, parseRecipientInput, shortAddress } from '../../util/recipient';

type Step = 'asset' | 'to' | 'amount' | 'review' | 'done';
type Speed = 'slow' | 'normal' | 'fast';

const keyOf = (h: Holding) => `${assetKey(h)}|${h.owner}|${h.scheme}#${h.account}`;
const feeOf = (p: Prepared) =>
  p.family === 'btc' ? { v: p.fee, d: 8, s: 'BTC' } : p.family === 'evm' ? { v: p.p.maxFee, d: 18, s: CHAINS[p.chain].symbol } : { v: p.p.fee, d: 9, s: 'SOL' };

function friendly(e: unknown): string {
  if (e instanceof DeviceError && e.code === 'USER_REJECTED') return 'You cancelled on the device. Nothing was sent.';
  const m = (e as Error)?.message ?? String(e);
  if (/insufficient|not enough|exceeds/i.test(m)) return `${m}. Leave some for the network fee, or use a smaller amount.`;
  if (/network|fetch|timeout/i.test(m)) return `Network problem: ${m}. Check your connection and try again.`;
  return m;
}

export default function Send() {
  const w = useWallet();
  const t = useTheme();
  const net = w.settings.net;
  const [step, setStep] = useState<Step>('asset');
  const [sel, setSel] = useState('');
  const [query, setQuery] = useState('');
  const [toInput, setToInput] = useState('');
  const [scan, setScan] = useState(false);
  const [uriAmount, setUriAmount] = useState<string | null>(null);
  const [amount, setAmount] = useState(''); // coin units, as typed
  const [usd, setUsd] = useState(''); // when typing in USD
  const [usdMode, setUsdMode] = useState(false);
  const [max, setMax] = useState(false);
  const [speed, setSpeed] = useState<Speed>('normal');
  const [prep, setPrep] = useState<Prepared | null>(null);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const [recents, setRecents] = useState<Recent[]>([]);
  const [sent, setSent] = useState<{ hash: string; chain: Holding['chain']; status: 'pending' | 'confirmed' | 'failed' } | null>(null);

  useEffect(() => {
    void loadRecents().then(setRecents);
  }, []);

  // Opened from a coin on the Wallet screen: start with that coin chosen
  const { asset: assetParam, at: atParam } = useLocalSearchParams<{ asset?: string; at?: string }>();

  const sendable = useMemo(
    () => (w.portfolio?.holdings ?? []).filter((h) => (h.kind === 'native' || h.kind === 'token') && h.amount > 0n).sort((a, b) => (b.usdValue ?? 0) - (a.usdValue ?? 0)),
    [w.portfolio],
  );

  const h = sendable.find((x) => keyOf(x) === sel);
  const acct = h && w.accounts.find((a) => a.scheme === h.scheme && a.account === h.account);
  const fam = h ? CHAINS[h.chain].family : undefined;
  const chainLabel = (c: Holding['chain']) => CHAINS[c].net[net].label;
  const check = h ? checkRecipient(h.chain, net, parseRecipientInput(toInput).address) : null;
  const to = check?.ok ? check.address : '';
  const price = h?.usdPrice;
  const nativePrice = (c: Holding['chain']) => w.portfolio?.holdings.find((x) => x.chain === c && x.kind === 'native' && x.usdPrice)?.usdPrice;

  const reset = useCallback(() => {
    setStep('asset');
    setSel('');
    setQuery('');
    setToInput('');
    setUriAmount(null);
    setAmount('');
    setUsd('');
    setUsdMode(false);
    setMax(false);
    setSpeed('normal');
    setPrep(null);
    setErr(null);
    setSent(null);
  }, []);

  // (once per tap: `at` makes each navigation unique; state set during render, React's
  // pattern for reacting to new props, so no effect is needed)
  const [handled, setHandled] = useState<string | undefined>();
  const nav = assetParam ? `${assetParam}@${atParam ?? ''}` : undefined;
  if (nav && nav !== handled && sendable.length) {
    setHandled(nav);
    const match = sendable.filter((x) => assetKey(x) === assetParam);
    reset();
    if (match.length === 1) {
      setSel(keyOf(match[0]!));
      setStep('to');
    } else if (match.length) setQuery(match[0]!.symbol); // held in several accounts: let the user pick which
  }

  const back = useCallback(() => {
    setErr(null);
    if (step === 'to') setStep('asset');
    else if (step === 'amount') setStep('to');
    else if (step === 'review') {
      setPrep(null);
      setStep('amount');
    } else {
      if (step === 'done') reset();
      router.navigate('/');
    }
    return true;
  }, [step, reset]);

  // Android back button walks back through the steps
  useFocusEffect(
    useCallback(() => {
      const sub = BackHandler.addEventListener('hardwareBackPress', back);
      return () => sub.remove();
    }, [back]),
  );

  // ---------------------------------------------------------------- amount helpers
  const coinStr = usdMode ? usdToCoin(usd, price ?? 0, h?.decimals ?? 0) : amount;
  let parsed: bigint | null = null;
  try {
    parsed = h && coinStr ? parseAmount(coinStr, h.decimals) : null;
  } catch {
    parsed = null;
  }
  const over = !!(h && parsed !== null && parsed > h.amount);
  const amountOk = max || (parsed !== null && parsed > 0n && !over);

  const prepare = async (sp: Speed = speed) => {
    if (!h || !acct || !to) return;
    setBusy(true);
    setErr(null);
    try {
      const amt = max ? (h.kind === 'token' ? h.amount : 'max') : parsed!;
      const p = await prepareSend({ holding: h, account: acct, net, to, amount: amt, portfolio: w.portfolio, feeLevel: sp });
      setPrep(p);
      setStep('review');
    } catch (e) {
      setErr(friendly(e));
    } finally {
      setBusy(false);
    }
  };

  const confirm = async () => {
    if (!prep || !w.device || !h) return;
    setBusy(true);
    setErr(null);
    try {
      const hash = await signAndBroadcast(w.device.client, prep);
      setSent({ hash, chain: prep.chain, status: 'pending' });
      setStep('done');
      setRecents(await rememberRecipient(familyOf(prep.account.scheme), net, prep.to));
      void w.refresh();
    } catch (e) {
      setErr(friendly(e));
    } finally {
      setBusy(false);
    }
  };

  // follow the transaction until it confirms (about 4 minutes at most)
  useEffect(() => {
    if (!sent || sent.status !== 'pending') return;
    let n = 0;
    const id = setInterval(async () => {
      n++;
      try {
        const s = await txStatus(sent.chain, net, sent.hash);
        if (s !== 'pending') setSent((cur) => (cur && cur.hash === sent.hash ? { ...cur, status: s } : cur));
      } catch {
        /* keep trying */
      }
      if (n > 40) clearInterval(id);
    }, 6000);
    return () => clearInterval(id);
  }, [sent, net]);

  // ---------------------------------------------------------------- steps
  const netBanner =
    net === 'test' ? null : (
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
        <Badge label="MAINNET" color={t.ok} />
        <T kind="muted">Real funds</T>
      </View>
    );

  if (step === 'asset')
    return (
      <StepScreen title="Send" onBack={back}>
        {netBanner}
        {!w.accounts.length || !sendable.length ? (
          <Card style={{ alignItems: 'center', gap: 12, paddingVertical: 28 }}>
            <Ionicons name="wallet-outline" size={34} color={t.muted} />
            <T kind="h2" center>{w.accounts.length ? 'Nothing to send yet' : 'Connect your tredder'}</T>
            <T kind="muted" center>{w.accounts.length ? 'Receive some coins first, then pull down on the Wallet tab to refresh.' : 'Pair your device on the Device tab to see your balances.'}</T>
            <Button kind="secondary" title={w.accounts.length ? 'Receive' : 'Go to Device'} onPress={() => router.navigate(w.accounts.length ? '/receive' : '/device')} />
          </Card>
        ) : (
          <>
            <SearchBox value={query} onChange={setQuery} placeholder="Search" />
            {sendable
              .filter((x) => !query || `${x.symbol} ${x.name} ${chainLabel(x.chain)}`.toLowerCase().includes(query.toLowerCase()))
              .map((x) => (
                <Row
                  key={keyOf(x)}
                  onPress={() => {
                    setSel(keyOf(x));
                    setPrep(null);
                    setAmount('');
                    setUsd('');
                    setMax(false);
                    setStep('to');
                  }}
                  icon={<CoinIcon symbol={x.symbol} logo={assetLogo(x)} seed={assetSeed(x)} small={x.kind === 'token' ? CHAINS[x.chain].symbol : undefined} smallLogo={x.kind === 'token' ? chainLogo(x.chain) : undefined} />}
                  left={<T kind="h2">{x.name || x.symbol}</T>}
                  sub={`${formatAmount(x.amount, x.decimals, 6)} ${x.symbol} · ${chainLabel(x.chain)}`}
                  right={x.usdValue !== undefined ? <T>{formatUsd(x.usdValue)}</T> : undefined}
                />
              ))}
          </>
        )}
      </StepScreen>
    );

  if (!h) return null;

  if (step === 'to') {
    const own = w.accounts
      .filter((a) => a.family === fam && a.key.kind === 'address' && !(a.scheme === h.scheme && a.account === h.account))
      .map((a) => (a.key.kind === 'address' ? a.key.address : ''))
      .filter((a, i, all) => a && all.indexOf(a) === i && !sameAddress(fam!, a, h.owner));
    const recent = recents.filter((r) => r.family === fam && r.net === net);
    const input = parseRecipientInput(toInput);
    const pick = (a: string) => setToInput(a);
    const onText = (raw: string) => {
      const p = parseRecipientInput(raw);
      setToInput(raw);
      if (p.amount) setUriAmount(p.amount);
    };
    return (
      <StepScreen
        title={`Send ${h.symbol}`}
        onBack={back}
        footer={
          <Button
            title="Next"
            disabled={!to}
            onPress={() => {
              setErr(null);
              if (uriAmount && !amount && !max) {
                setAmount(uriAmount); // amount from the QR code, shown on the next step for the user to check
                setUsdMode(false);
              }
              setStep('amount');
            }}
          />
        }
      >
        <View style={{ backgroundColor: t.card, borderRadius: 16, padding: 14, gap: 10, borderWidth: 1, borderColor: toInput && check && !check.ok ? t.danger : t.card }}>
          <TextInput
            value={toInput}
            onChangeText={onText}
            placeholder={`Recipient's ${chainLabel(h.chain)} address`}
            placeholderTextColor={t.muted}
            autoCapitalize="none"
            autoCorrect={false}
            multiline
            style={{ color: t.text, fontSize: 16, fontFamily: toInput ? 'monospace' : undefined, minHeight: 44 }}
          />
          <View style={{ flexDirection: 'row', gap: 8 }}>
            <IconPill icon="clipboard-outline" label="Paste" onPress={async () => onText(await Clipboard.getStringAsync())} />
            <IconPill icon="qr-code-outline" label="Scan" onPress={() => setScan(true)} />
            {toInput ? <IconPill icon="close" label="Clear" onPress={() => (setToInput(''), setUriAmount(null))} /> : null}
          </View>
        </View>
        {toInput && check ? (
          check.ok ? (
            <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
              <Ionicons name="checkmark-circle" size={18} color={t.ok} />
              <T kind="ok">Valid {chainLabel(h.chain)} address</T>
            </View>
          ) : (
            <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
              <Ionicons name="alert-circle" size={18} color={t.danger} />
              <T kind="error">{input.address === '' && input.scheme ? 'This QR code asks for a contract call, not a plain payment' : check.reason}</T>
            </View>
          )
        ) : null}
        {to && sameAddress(fam!, to, h.owner) ? <T kind="warn">This is the address you are sending from.</T> : null}
        {uriAmount ? <T kind="muted">The code asks for {uriAmount} {h.symbol}. You can change it on the next step.</T> : null}
        {own.length ? (
          <View style={{ gap: 2 }}>
            <T kind="section">Your accounts</T>
            {own.map((a) => (
              <Row key={a} onPress={() => pick(a)} icon={<Avatar label="Me" />} left={<T>{shortAddress(a, 6)}</T>} sub="Another account on this tredder" />
            ))}
          </View>
        ) : null}
        {recent.length ? (
          <View style={{ gap: 2 }}>
            <T kind="section">Recent</T>
            {recent.slice(0, 8).map((r) => (
              <Row key={r.address} onPress={() => pick(r.address)} icon={<Avatar label={r.address.replace(/^0x/, '').slice(0, 2).toUpperCase()} />} left={<T>{shortAddress(r.address, 6)}</T>} sub={`Sent ${r.count}× · ${new Date(r.last).toLocaleDateString()}`} />
            ))}
          </View>
        ) : null}
        <Scanner
          visible={scan}
          onClose={() => setScan(false)}
          onScan={(text) => {
            setScan(false);
            onText(text);
          }}
        />
      </StepScreen>
    );
  }

  if (step === 'amount') {
    const big = usdMode ? `$${usd || '0'}` : `${max ? formatAmount(h.amount, h.decimals, 8) : amount || '0'}`;
    const other = usdMode ? `${coinStr || '0'} ${h.symbol}` : price ? `≈ ${formatUsd((max ? unitsToNumber(h.amount, h.decimals) : asNumber(amount)) * price)}` : '';
    const onKey = (k: Parameters<typeof pressKey>[1]) => {
      setErr(null);
      if (max) {
        setMax(false);
        if (usdMode) setUsd(pressKey('', k, 2));
        else setAmount(pressKey('', k, h.decimals));
        return;
      }
      if (usdMode) setUsd((v) => pressKey(v, k, 2));
      else setAmount((v) => pressKey(v, k, h.decimals));
    };
    return (
      <StepScreen
        title={`Send ${h.symbol}`}
        onBack={back}
        footer={
          <>
            {err ? <T kind="error">{err}</T> : null}
            <Button title="Review" disabled={!amountOk} loading={busy} onPress={() => void prepare()} />
          </>
        }
      >
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
          <CoinIcon symbol={h.symbol} logo={assetLogo(h)} seed={assetSeed(h)} size={28} />
          <T kind="muted">To {shortAddress(to, 6)}</T>
        </View>
        <View style={{ alignItems: 'center', gap: 6, paddingVertical: 18 }}>
          <View style={{ flexDirection: 'row', alignItems: 'baseline', gap: 8 }}>
            <Text adjustsFontSizeToFit numberOfLines={1} style={{ color: over ? t.danger : t.text, fontSize: 48, fontWeight: '700', maxWidth: '85%' }}>
              {big}
            </Text>
            {usdMode ? null : <Text style={{ color: t.muted, fontSize: 22, fontWeight: '600' }}>{h.symbol}</Text>}
          </View>
          {price ? (
            <Pressable onPress={() => (usdMode ? (setAmount(coinStr), setUsdMode(false)) : (setUsd(coinToUsd(max ? unitsToInput(h.amount, h.decimals) : amount, price)), setUsdMode(true)))} style={{ flexDirection: 'row', alignItems: 'center', gap: 6, padding: 6 }}>
              <Text style={{ color: t.muted, fontSize: 15 }}>{other}</Text>
              <Ionicons name="swap-vertical" size={16} color={t.muted} />
            </Pressable>
          ) : null}
          {/* fixed height: a message appearing here must not move the keypad under the user's thumb */}
          <View style={{ minHeight: 20, justifyContent: 'center' }}>
            {over ? <T kind="error">Not enough {h.symbol}</T> : max && h.kind === 'native' ? <T kind="muted">The network fee is taken from this amount.</T> : null}
          </View>
        </View>
        <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', backgroundColor: t.card, borderRadius: 14, paddingHorizontal: 14, paddingVertical: 10 }}>
          <View>
            <T kind="muted">Available</T>
            <T>
              {formatAmount(h.amount, h.decimals, 8)} {h.symbol}
            </T>
          </View>
          <Pressable onPress={() => (setMax(true), setErr(null))} style={{ backgroundColor: max ? t.accent : t.card2, borderRadius: 999, paddingHorizontal: 16, paddingVertical: 8 }}>
            <Text style={{ color: max ? t.onAccent : t.text, fontWeight: '700' }}>Max</Text>
          </Pressable>
        </View>
        <Keypad onKey={onKey} decimals={usdMode ? 2 : h.decimals} />
      </StepScreen>
    );
  }

  if (step === 'review' && prep) {
    const fee = feeOf(prep);
    const feeUsd = nativePrice(prep.chain);
    const amtUsd = price ? unitsToNumber(prep.amount, h.decimals) * price : undefined;
    const sameAsset = h.kind === 'native';
    const known = recents.some((r) => r.family === fam && r.net === net && sameAddress(fam!, r.address, prep.to));
    return (
      <StepScreen
        title="Review"
        onBack={back}
        footer={
          <>
            {err ? <T kind="error">{err}</T> : null}
            {w.device ? (
              <Button title="Confirm on tredder" icon="hardware-chip-outline" loading={busy} onPress={() => void confirm()} />
            ) : (
              <Button title="Connect your tredder to sign" icon="bluetooth" onPress={() => router.navigate('/device')} />
            )}
            <Button kind="secondary" title="Cancel" onPress={() => (reset(), router.navigate('/'))} />
          </>
        }
      >
        <View style={{ alignItems: 'center', gap: 8, paddingVertical: 10 }}>
          <CoinIcon symbol={h.symbol} logo={assetLogo(h)} seed={assetSeed(h)} size={56} small={h.kind === 'token' ? CHAINS[h.chain].symbol : undefined} smallLogo={h.kind === 'token' ? chainLogo(h.chain) : undefined} />
          <Text style={{ color: t.text, fontSize: 34, fontWeight: '700' }}>
            {formatAmount(prep.amount, h.decimals, 8)} {h.symbol}
          </Text>
          {amtUsd !== undefined ? <T kind="muted">{formatUsd(amtUsd)}</T> : null}
        </View>
        <Card>
          <T kind="muted">To</T>
          <AddressText address={prep.to} />
          {!known ? (
            <View style={{ flexDirection: 'row', gap: 8, alignItems: 'flex-start' }}>
              <Ionicons name="information-circle-outline" size={18} color={t.warn} />
              <View style={{ flex: 1 }}>
                <T kind="warn">First time sending to this address. Double-check it with the recipient.</T>
              </View>
            </View>
          ) : null}
        </Card>
        <Card>
          <Row left={<T kind="muted">Network</T>} right={chainLabel(prep.chain)} />
          <Row
            left={<T kind="muted">Network fee</T>}
            right={
              <>
                <T>
                  {formatAmount(fee.v, fee.d, 8)} {fee.s}
                </T>
                {feeUsd ? <T kind="muted">{formatUsd(unitsToNumber(fee.v, fee.d) * feeUsd)}</T> : null}
              </>
            }
          />
          {prep.family === 'btc' ? (
            <Choice
              options={[
                { value: 'slow', label: 'Slow' },
                { value: 'normal', label: 'Normal' },
                { value: 'fast', label: 'Fast' },
              ]}
              value={speed}
              onChange={(v) => {
                setSpeed(v);
                void prepare(v);
              }}
            />
          ) : null}
          {sameAsset ? (
            <Row
              left={<T kind="muted">Total</T>}
              right={
                <T kind="h2">
                  {formatAmount(prep.amount + fee.v, h.decimals, 8)} {h.symbol}
                </T>
              }
            />
          ) : null}
          {prep.family === 'evm' ? <T kind="muted">The fee shown is the most it can cost; the actual fee is usually lower.</T> : null}
        </Card>
        <View style={{ flexDirection: 'row', gap: 10, backgroundColor: t.accent + '1A', borderRadius: 14, padding: 14 }}>
          <Ionicons name="shield-checkmark-outline" size={20} color={t.accent} />
          <View style={{ flex: 1 }}>
            <T>Your tredder will show the amount and address.</T>
            <T kind="muted">Check that the highlighted first and last 6 characters match before you press Confirm on the device.</T>
          </View>
        </View>
        {net === 'test' ? <T kind="muted" center>Testnet: these coins have no value.</T> : null}
        {busy && !w.awaitingUser ? <ActivityIndicator color={t.muted} /> : null}
      </StepScreen>
    );
  }

  if (step === 'done' && sent) {
    const icon = sent.status === 'failed' ? 'close-circle' : sent.status === 'confirmed' ? 'checkmark-circle' : 'time';
    const color = sent.status === 'failed' ? t.danger : sent.status === 'confirmed' ? t.ok : t.accent;
    return (
      <StepScreen
        title=""
        footer={
          <>
            <Button kind="secondary" title="View on explorer" icon="open-outline" onPress={() => void Linking.openURL(explorerTx(sent.chain, net, sent.hash))} />
            <Button title="Done" onPress={() => (reset(), router.navigate('/'))} />
          </>
        }
      >
        <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center', gap: 14, paddingTop: 40 }}>
          <Ionicons name={icon} size={88} color={color} />
          <T kind="title" center>{sent.status === 'failed' ? 'Transaction failed' : sent.status === 'confirmed' ? 'Sent' : 'Sending…'}</T>
          {prep ? (
            <T kind="muted" center>
              {formatAmount(prep.amount, h.decimals, 8)} {h.symbol} to {shortAddress(prep.to, 6)}
            </T>
          ) : null}
          <T kind="muted" center>
            {sent.status === 'pending' ? 'Waiting for the network to confirm it. You can leave this screen.' : sent.status === 'confirmed' ? 'Confirmed on the network.' : 'The network rejected it. Your funds did not move; check the explorer.'}
          </T>
        </View>
      </StepScreen>
    );
  }

  return null;
}

function SearchBox({ value, onChange, placeholder }: { value: string; onChange: (v: string) => void; placeholder: string }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8, backgroundColor: t.card, borderRadius: 12, paddingHorizontal: 12 }}>
      <Ionicons name="search" size={18} color={t.muted} />
      <TextInput value={value} onChangeText={onChange} placeholder={placeholder} placeholderTextColor={t.muted} autoCapitalize="none" autoCorrect={false} style={{ flex: 1, color: t.text, paddingVertical: 12, fontSize: 15 }} />
    </View>
  );
}

function Avatar({ label }: { label: string }) {
  const t = useTheme();
  return (
    <View style={{ width: 40, height: 40, borderRadius: 20, backgroundColor: t.card2, alignItems: 'center', justifyContent: 'center' }}>
      <Text style={{ color: t.text, fontWeight: '700', fontSize: 13 }}>{label}</Text>
    </View>
  );
}
