"""
Generate firmware/tests/host/vectors.h: test vectors for the wallet core.

Two kinds of vectors:
  1. OFFICIAL  copied from the specs (BIP39 vectors.json, BIP49/84/86 test
               vectors, SLIP-10 ed25519 vectors). These anchor everything.
  2. DIFFERENTIAL  addresses computed by an independent implementation
               (bip_utils) for every derivation scheme, several seeds,
               passphrases, accounts, change/index and networks. Paths are
               written here from each wallet's documented layout, NOT taken
               from the C code, so a path bug in C shows up as a mismatch.
The generator also checks bip_utils itself against the official vectors
before trusting it.

Usage (needs `pip install bip_utils`; Python 3.11 works):
    python gen_vectors.py <dir with vectors.json bip-0049.mediawiki bip-0084.mediawiki
                           bip-0086.mediawiki slip-0010.md>
Sources:
    https://github.com/trezor/python-mnemonic/blob/master/vectors.json
    https://github.com/bitcoin/bips (bip-0049/0084/0086.mediawiki)
    https://github.com/satoshilabs/slips/blob/master/slip-0010.md
"""
import hashlib
import hmac
import json
import os
import re
import sys

from bip_utils import (Base58Encoder, Bip32Slip10Ed25519, Bip32Slip10Secp256k1, Bip39SeedGenerator,
                       EthAddrEncoder, P2PKHAddrEncoder, P2SHAddrEncoder, P2TRAddrEncoder, P2WPKHAddrEncoder,
                       SolAddrEncoder)

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "host", "vectors.h")
ABANDON = "abandon " * 11 + "about"


def c(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


# ---------------------------------------------------------------- official vectors
def bip39_official(src):
    v = json.load(open(os.path.join(src, "vectors.json"), encoding="utf8"))["english"]
    return [(e, m, s) for e, m, s, _x in v]


def bip_addresses(src, name):
    """Return [(path, address)] from 'first receiving address = m/..' + 'address = ..' pairs."""
    txt = open(os.path.join(src, name), encoding="utf8").read()
    paths = re.findall(r"//\s*Account 0, (?:first|second) (?:receiving|change) (?:address|private key) = (m/[0-9'/]+)", txt)
    addrs = re.findall(r"^\s*address\s*=\s*(\S+)", txt, re.M)
    assert len(paths) == len(addrs) and paths, (name, paths, addrs)
    return list(zip(paths, addrs))


def bip49_official(src):
    txt = open(os.path.join(src, "bip-0049.mediawiki"), encoding="utf8").read()
    path = re.search(r"first receiving private key = (m/[0-9'/]+)", txt).group(1)
    h = re.search(r"addressBytes\s*=\s*HASH160\(scriptSig\)\s*=\s*0x([0-9a-f]{40})", txt).group(1)
    payload = bytes([0xC4]) + bytes.fromhex(h)
    chk = hashlib.sha256(hashlib.sha256(payload).digest()).digest()[:4]
    return path, Base58Encoder.Encode(payload + chk)


def slip10_ed25519(src):
    txt = open(os.path.join(src, "slip-0010.md"), encoding="utf8").read()
    out = []
    for block in re.split(r"### Test vector \d+ for ed25519", txt)[1:]:
        block = block.split("###")[0]
        seed = re.search(r"Seed \(hex\): ([0-9a-f]+)", block).group(1)
        for m in re.finditer(r"\* Chain (m[^\n]*?)\s*\n(?:.*\n){0,6}?\s*\* private: ([0-9a-f]{64})\s*\n\s*\* public: ([0-9a-f]{66})", block):
            out.append((seed, m.group(1).replace("<sub>H</sub>", "'"), m.group(2), m.group(3)))
    assert len(out) >= 10, out
    return out


# ---------------------------------------------------------------- reference derivation
SCHEMES = ["BTC_LEGACY", "BTC_NESTED", "BTC_NATIVE", "BTC_TAPROOT", "EVM_STANDARD", "EVM_LEDGER_LIVE",
           "SOL_STANDARD", "SOL_BIP44", "SOL_BIP44_ROOT", "SOL_CLI_SEED"]


def ref_path(scheme, net, acct, change, idx):
    coin = 1 if net else 0
    p = {"BTC_LEGACY": 44, "BTC_NESTED": 49, "BTC_NATIVE": 84, "BTC_TAPROOT": 86}
    if scheme in p:
        return f"m/{p[scheme]}'/{coin}'/{acct}'/{change}/{idx}"
    return {"EVM_STANDARD": f"m/44'/60'/0'/0/{acct}",
            "EVM_LEDGER_LIVE": f"m/44'/60'/{acct}'/0/0",
            "SOL_STANDARD": f"m/44'/501'/{acct}'/0'",
            "SOL_BIP44": f"m/44'/501'/{acct}'",
            "SOL_BIP44_ROOT": "m/44'/501'",
            "SOL_CLI_SEED": None}[scheme]


def ref_address(seed, scheme, net, acct, change, idx):
    path = ref_path(scheme, net, acct, change, idx)
    if scheme == "SOL_CLI_SEED":
        from bip_utils import Ed25519PrivateKey
        return SolAddrEncoder.EncodeKey(Ed25519PrivateKey.FromBytes(seed[:32]).PublicKey()), "seed[0:32]"
    if scheme.startswith("SOL"):
        pub = Bip32Slip10Ed25519.FromSeed(seed).DerivePath(path).PublicKey().KeyObject()
        return SolAddrEncoder.EncodeKey(pub), path
    pub = Bip32Slip10Secp256k1.FromSeed(seed).DerivePath(path).PublicKey().KeyObject()
    hrp = "tb" if net else "bc"
    if scheme == "BTC_LEGACY":
        return P2PKHAddrEncoder.EncodeKey(pub, net_ver=b"\x6f" if net else b"\x00"), path
    if scheme == "BTC_NESTED":
        return P2SHAddrEncoder.EncodeKey(pub, net_ver=b"\xc4" if net else b"\x05"), path
    if scheme == "BTC_NATIVE":
        return P2WPKHAddrEncoder.EncodeKey(pub, hrp=hrp, wit_ver=0), path
    if scheme == "BTC_TAPROOT":
        return P2TRAddrEncoder.EncodeKey(pub, hrp=hrp), path
    return EthAddrEncoder.EncodeKey(pub), path


def main(src):
    b39 = bip39_official(src)
    b84 = bip_addresses(src, "bip-0084.mediawiki")
    b86 = bip_addresses(src, "bip-0086.mediawiki")
    b49 = bip49_official(src)
    s10 = slip10_ed25519(src)

    # Trust-but-verify the reference implementation against the official vectors.
    seed_ab = Bip39SeedGenerator(ABANDON).Generate()
    for path, addr in b84:
        _, _, _, b, ch, ix = path.split("/")
        assert ref_address(seed_ab, "BTC_NATIVE", 0, int(b[:-1]), int(ch), int(ix))[0] == addr
    for path, addr in b86:
        _, _, _, b, ch, ix = path.split("/")
        assert ref_address(seed_ab, "BTC_TAPROOT", 0, int(b[:-1]), int(ch), int(ix))[0] == addr
    assert ref_address(seed_ab, "BTC_NESTED", 1, 0, 0, 0)[0] == b49[1]
    for e, m, s in b39:
        assert Bip39SeedGenerator(m).Generate("TREZOR").hex() == s

    from bip_utils import Bip39MnemonicGenerator
    # 15- and 21-word phrases are not in vectors.json: make them from fixed entropy.
    m15 = Bip39MnemonicGenerator().FromEntropy(bytes(range(1, 21))).ToStr()
    m21 = Bip39MnemonicGenerator().FromEntropy(bytes(range(200, 228))).ToStr()
    mnemonics = [ABANDON, b39[3][1], b39[7][1], b39[11][1], b39[20][1], b39[23][1], m15, m21]
    mnemonics = list(dict.fromkeys(mnemonics))
    assert {len(m.split()) for m in mnemonics} == {12, 15, 18, 21, 24}
    passphrases = ["", "TREZOR", "hidden wallet: 25th word!"]

    diff = []
    for mi, m in enumerate(mnemonics):
        for pi, pw in enumerate(passphrases):
            if mi > 1 and pi == 1:
                continue
            seed = Bip39SeedGenerator(m).Generate(pw)
            for scheme in SCHEMES:
                btc = scheme.startswith("BTC")
                nets = [0, 1] if btc else [0]
                accts = [0] if scheme in ("SOL_BIP44_ROOT", "SOL_CLI_SEED") else [0, 1, 7]
                ci = [(0, 0), (0, 1), (1, 0), (0, 19)] if btc else [(0, 0)]
                for net in nets:
                    for a in accts:
                        for ch, ix in ci:
                            addr, path = ref_address(seed, scheme, net, a, ch, ix)
                            diff.append((scheme, net, a, ch, ix, mi, pi, addr, path))

    # Account-level public keys (watch-only export for the app)
    acct_keys = []
    purpose = {"BTC_LEGACY": 44, "BTC_NESTED": 49, "BTC_NATIVE": 84, "BTC_TAPROOT": 86}
    for mi, pi in ((0, 0), (0, 2), (5, 0)):
        seed = Bip39SeedGenerator(mnemonics[mi]).Generate(passphrases[pi])
        master = Bip32Slip10Secp256k1.FromSeed(seed)
        mfp = int.from_bytes(master.FingerPrint().ToBytes(), "big")
        for scheme, pur in purpose.items():
            for net in (0, 1):
                for a in (0, 1):
                    node = master.DerivePath(f"m/{pur}'/{net}'/{a}'")
                    acct_keys.append((scheme, net, a, mi, pi, node.PublicKey().RawCompressed().ToHex(),
                                      node.ChainCode().ToHex(), mfp))

    # Entropy-mixer known answer (the construction documented in entropy.h)
    trng = bytes((i * 37 + 11) & 0xFF for i in range(512))
    se = bytes(range(100, 132))
    extra = b"dice:3,6,1,4,4,2"
    dom = b"tredder-wallet seed entropy v1"
    h = lambda b: hashlib.sha512(b).digest()  # noqa: E731
    ent_kat = hmac.new(h(trng), dom + h(se) + h(extra), hashlib.sha512).digest()
    ent_kat_noextra = hmac.new(h(trng), dom + h(se) + h(b""), hashlib.sha512).digest()

    L = ["/* GENERATED by firmware/tools/gen_vectors.py - do not edit. */",
         "#ifndef WALLET_TEST_VECTORS_H", "#define WALLET_TEST_VECTORS_H", "#include <stdint.h>", "",
         "/* BIP39 official vectors (trezor/python-mnemonic vectors.json), passphrase \"TREZOR\" */",
         "typedef struct { const char *entropy_hex, *mnemonic, *seed_hex; } bip39_vec_t;",
         "static const bip39_vec_t BIP39_VECTORS[] = {"]
    L += [f"  {{{c(e)}, {c(m)}, {c(s)}}}," for e, m, s in b39]
    L += ["};", "",
          "/* Official BIP84 / BIP86 (mainnet) and BIP49 (testnet) addresses for \"abandon x11 about\" */",
          "typedef struct { int scheme, net; uint32_t account, change, index; const char *addr; } official_addr_t;",
          "static const official_addr_t OFFICIAL_ADDRESSES[] = {"]
    for name, lst in (("BTC_NATIVE", b84), ("BTC_TAPROOT", b86)):
        for path, addr in lst:
            _, _, _, acc, ch, ix = path.split("/")
            L.append(f"  {{WL_SCHEME_{name}, WL_NET_MAIN, {acc[:-1]}, {ch}, {ix}, {c(addr)}}}, /* {path} */")
    L.append(f"  {{WL_SCHEME_BTC_NESTED, WL_NET_TEST, 0, 0, 0, {c(b49[1])}}}, /* {b49[0]} */")
    L += ["};", "",
          "/* SLIP-10 ed25519 official vectors */",
          "typedef struct { const char *seed_hex, *path, *priv_hex, *pub_hex; } slip10_vec_t;",
          "static const slip10_vec_t SLIP10_ED25519[] = {"]
    L += [f"  {{{c(s)}, {c(p)}, {c(k)}, {c(pk)}}}," for s, p, k, pk in s10]
    L += ["};", "", "/* Differential vectors (reference: bip_utils) */",
          "static const char *const DIFF_MNEMONICS[] = {" + ", ".join(c(m) for m in mnemonics) + "};",
          "static const char *const DIFF_PASSPHRASES[] = {" + ", ".join(c(p) for p in passphrases) + "};",
          "typedef struct { int scheme, net; uint32_t account, change, index; int mnemonic, passphrase;"
          " const char *addr, *path; } diff_vec_t;",
          "static const diff_vec_t DIFF_VECTORS[] = {"]
    L += [f"  {{WL_SCHEME_{s}, {('WL_NET_TEST' if n else 'WL_NET_MAIN')}, {a}, {ch}, {ix}, {mi}, {pi}, {c(addr)}, {c(path)}}},"
          for s, n, a, ch, ix, mi, pi, addr, path in diff]
    L += ["};", "", "/* Account-level public keys (BIP32 account node), reference: bip_utils */",
          "typedef struct { int scheme, net; uint32_t account; int mnemonic, passphrase; const char *pub_hex, *chain_hex;"
          " uint32_t master_fp; } acct_vec_t;",
          "static const acct_vec_t ACCOUNT_KEYS[] = {"]
    L += [f"  {{WL_SCHEME_{sc}, {('WL_NET_TEST' if n else 'WL_NET_MAIN')}, {a}, {mi}, {pi}, {c(pk)}, {c(cc)}, 0x{fp:08x}u}},"
          for sc, n, a, mi, pi, pk, cc, fp in acct_keys]
    L += ["};", "",
          "/* Entropy mixer known answers: trng[i] = (37*i + 11) & 0xFF (512 bytes), se = 100..131 */",
          f"static const char ENTROPY_KAT_EXTRA[] = {c(extra.decode())};",
          f"static const char ENTROPY_KAT_HEX[] = {c(ent_kat.hex())};",
          f"static const char ENTROPY_KAT_NOEXTRA_HEX[] = {c(ent_kat_noextra.hex())};",
          "", "#endif", ""]
    with open(OUT, "w", encoding="utf8", newline="\n") as fh:
        fh.write("\n".join(L))
    print(f"wrote {OUT}: {len(b39)} BIP39, {len(b84) + len(b86) + 1} official addr, {len(s10)} SLIP-10, "
          f"{len(diff)} differential, {len(acct_keys)} account keys")


if __name__ == "__main__":
    main(sys.argv[1])
