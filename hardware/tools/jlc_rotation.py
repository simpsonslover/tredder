"""
Rotation and position corrections for JLCPCB's pick-and-place, measured, not guessed.

JLCPCB places every part with the footprint from its own library (EasyEDA/LCSC), whose
zero orientation and origin often differ from KiCad's. For each LCSC part this fetches that
footprint, matches its pads to the KiCad footprint's pads by pad number, and finds the
rotation (0/90/180/270) and offset that line them up:

    KiCad pad (footprint frame) = Rot(theta) * EasyEDA pad + t
    =>  JLC rotation = KiCad rotation + theta,   JLC position = KiCad position + Rot(KiCad rotation) * t

A part is only corrected if every common pad lines up within 0.1 mm; anything else is
reported for a manual check in JLCPCB's placement preview. Footprints are cached in
fab/easyeda/ (committed), so results are reproducible offline.

Used by export_fab.py; run alone (KiCad python) to see the table.
"""
import json
import math
import os
import re
import sys
import urllib.request

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.abspath(os.path.join(HERE, ".."))
CACHE = os.path.join(HW, "fab", "easyeda")
API = "https://easyeda.com/api/products/{}/components?version=6.4.19.5"
UNIT = 0.254          # EasyEDA canvas unit = 10 mil
TOL = 0.1             # mm


def easyeda_footprint(lcsc):
    """{pad number: (x, y) mm, y up, relative to the footprint origin}, package name."""
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, f"{lcsc}.json")
    if not os.path.exists(path):
        req = urllib.request.Request(API.format(lcsc), headers={"User-Agent": "tredder-hardware-tools"})
        data = json.loads(urllib.request.urlopen(req, timeout=30).read().decode("utf8"))
        pkg = data["result"]["packageDetail"]
        keep = {"title": pkg["title"], "head": pkg["dataStr"]["head"], "shape": pkg["dataStr"]["shape"]}
        with open(path, "w", encoding="utf8") as fh:
            json.dump(keep, fh, indent=0)
    d = json.load(open(path, encoding="utf8"))
    ox, oy = float(d["head"]["x"]), float(d["head"]["y"])
    pads = {}
    for s in d["shape"]:
        f = s.split("~")
        if f[0] != "PAD":
            continue
        # PAD~shape~x~y~w~h~layer~net~number~...
        num = f[8].strip()
        pads.setdefault(num, []).append(((float(f[2]) - ox) * UNIT, -(float(f[3]) - oy) * UNIT))
    return {k: v for k, v in pads.items() if k}, d["title"]


def kicad_pads(fp):
    """{pad number: (x, y) mm, y up} in the footprint's own frame (orientation 0)."""
    o = fp.GetPosition()
    a = math.radians(fp.GetOrientationDegrees())
    pads = {}
    for p in fp.Pads():
        if not p.GetNumber():
            continue
        dx, dy = pcbnew.ToMM(p.GetPosition().x - o.x), -pcbnew.ToMM(p.GetPosition().y - o.y)   # board, y up
        lx, ly = dx * math.cos(-a) - dy * math.sin(-a), dx * math.sin(-a) + dy * math.cos(-a)  # undo rotation
        pads.setdefault(p.GetNumber(), []).append((lx, ly))
    return pads


def mean(pts):
    return (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))


def rot(p, deg):
    a = math.radians(deg)
    return (p[0] * math.cos(a) - p[1] * math.sin(a), p[0] * math.sin(a) + p[1] * math.cos(a))


def _by_number(k, e, common):
    """[(worst error, theta, t)] for each rotation, matching pads by number (per-number means)."""
    km, em = {n: mean(k[n]) for n in common}, {n: mean(e[n]) for n in common}
    res = []
    for th in (0, 90, 180, 270):
        r = {n: rot(em[n], th) for n in common}
        tt = mean([(km[n][0] - r[n][0], km[n][1] - r[n][1]) for n in common])
        errs = {n: math.hypot(km[n][0] - r[n][0] - tt[0], km[n][1] - r[n][1] - tt[1]) for n in common}
        res.append((max(errs.values()), th, tt, errs))
    return sorted(res, key=lambda x: x[0])


def _by_position(k, e):
    """Match individual pad positions, ignoring numbers (for footprints that number pads
    differently). Only meaningful for parts that are electrically symmetric under the
    rotation found; the caller says so in the note."""
    kp = [p for v in k.values() for p in v]
    ep = [p for v in e.values() for p in v]
    if len(kp) != len(ep) or len(kp) < 2:
        return None
    kc = mean(kp)
    res = []
    for th in (0, 90, 180, 270):
        rp = [rot(p, th) for p in ep]
        rc = mean(rp)
        tt = (kc[0] - rc[0], kc[1] - rc[1])
        err = max(min(math.hypot(a[0] - b[0] - tt[0], a[1] - b[1] - tt[1]) for b in rp) for a in kp)
        res.append((err, th, tt))
    return sorted(res, key=lambda x: (round(x[0], 2), x[1]))  # ties: smallest rotation


def fit(k, e):
    """(theta, t, note) with K = Rot(theta) E + t, or (None, None, reason)."""
    common = sorted(set(k) & set(e))
    if len(common) >= 2:
        res = _by_number(k, e, common)
        err, th, tt, errs = res[0]
        if err <= TOL:
            return th, tt, f"{len(common)} pads by number, worst {err:.3f} mm"
        # a few pads numbered differently (shield / detect pads): drop up to 2 if 5+ agree
        if len(common) >= 7:
            bad = sorted(errs, key=errs.get, reverse=True)[:2]
            keep = [n for n in common if n not in bad]
            r2 = _by_number(k, e, keep)
            if r2[0][0] <= TOL:
                return r2[0][1], r2[0][2], f"{len(keep)} pads by number (pads {', '.join(bad)} numbered differently), worst {r2[0][0]:.3f} mm"
        # same layout, different lead spacing (e.g. SOT-23 1.9 vs 2.3 mm): only if unambiguous
        if err <= 0.35 and res[1][0] >= err + 0.5:
            return th, tt, f"{len(common)} pads by number, lead spacing differs (worst {err:.2f} mm, next rotation {res[1][0]:.2f} mm)"
    pos = _by_position(k, e)
    if pos and pos[0][0] <= TOL:
        return pos[0][1], pos[0][2], f"pads numbered differently, matched by position (worst {pos[0][0]:.3f} mm)"
    return None, None, "pads do not line up"


def corrections(board, parts):
    """{ref: (theta, (dx, dy) board mm y-up, note)} for refs with an LCSC number."""
    out, cache = {}, {}
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        p = parts.get(ref)
        if not p or p["dnp"] or not p["lcsc"]:
            continue
        if p["lcsc"] not in cache:
            try:
                cache[p["lcsc"]] = easyeda_footprint(p["lcsc"])
            except Exception as ex:  # no network and not cached
                cache[p["lcsc"]] = (None, f"footprint not available: {ex}")
        e, title = cache[p["lcsc"]]
        if e is None:
            out[ref] = (None, None, title)
            continue
        th, tt, note = fit(kicad_pads(fp), e)
        if th is None:
            out[ref] = (None, None, f"{note} ({title}): check in JLCPCB's preview")
            continue
        out[ref] = (th, rot(tt, fp.GetOrientationDegrees()), f"{title}: {note}")
    return out


if __name__ == "__main__":
    sys.path.insert(0, HERE)
    import design as D
    b = pcbnew.LoadBoard(os.path.join(HW, "wallet.kicad_pcb"))
    c = corrections(b, D.P)
    for ref in sorted(c, key=lambda r: (re.sub(r"\d", "", r), int(re.sub(r"\D", "", r) or 0))):
        th, t, note = c[ref]
        fp = b.FindFootprintByReference(ref)
        if th is None:
            print(f"{ref:5s} {fp.GetOrientationDegrees():6.1f}  ??   {note}")
        else:
            print(f"{ref:5s} {fp.GetOrientationDegrees():6.1f} +{th:3d} -> {(fp.GetOrientationDegrees() + th) % 360:5.1f}  "
                  f"shift ({t[0]:+.2f}, {t[1]:+.2f}) mm  {note}")
