"""Dependency-free SVG line charts (one chart = stacked panels sharing the x axis)."""
import html

import numpy as np

PALETTE = ["#2563eb", "#dc2626", "#16a34a", "#9333ea", "#ea580c", "#0891b2"]


def _nice(lo, hi, n=5):
    if hi - lo < 1e-12:
        hi = lo + 1.0
    raw = (hi - lo) / n
    mag = 10 ** np.floor(np.log10(raw))
    step = min((m * mag for m in (1, 2, 2.5, 5, 10) if m * mag >= raw), default=raw)
    a = np.floor(lo / step) * step
    b = np.ceil(hi / step) * step
    return np.arange(a, b + step / 2, step)


def _fmt(v):
    a = abs(v)
    if a == 0:
        return "0"
    if 1e-3 <= a < 1e4:
        return f"{v:.4g}"
    for div, suf in ((1e-6, "u"), (1e-3, "m"), (1, ""), (1e3, "k")):
        if a < div * 1000:
            s = f"{v / div:.3g}"
            return s + suf
    return f"{v:.3g}"


def chart(path, title, x, panels, xlabel="time (s)", width=900, panel_h=170):
    """panels: list of dict(ylabel=str, series=[(label, yarray)], limits=[(label, value)])."""
    ml, mr, mt, gap = 70, 160, 34, 26
    H = mt + len(panels) * (panel_h + gap) + 30
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{H}" '
           f'font-family="Segoe UI,Arial,sans-serif" font-size="11">',
           '<rect width="100%" height="100%" fill="white"/>',
           f'<text x="{ml}" y="20" font-size="14" font-weight="600">{html.escape(title)}</text>']
    x = np.asarray(x)
    xt = _nice(x.min(), x.max())
    pw = width - ml - mr
    for k, p in enumerate(panels):
        y0 = mt + k * (panel_h + gap)
        ys = [np.asarray(s[1]) for s in p["series"]] + [np.array([v]) for _, v in p.get("limits", [])]
        lo = min(float(a.min()) for a in ys)
        hi = max(float(a.max()) for a in ys)
        pad = 0.05 * (hi - lo or 1)
        yt = _nice(lo - pad, hi + pad)
        X = lambda v: ml + (v - xt[0]) / (xt[-1] - xt[0]) * pw  # noqa: E731
        Y = lambda v: y0 + panel_h - (v - yt[0]) / (yt[-1] - yt[0]) * panel_h  # noqa: E731
        out.append(f'<rect x="{ml}" y="{y0}" width="{pw}" height="{panel_h}" fill="#fafafa" stroke="#ccc"/>')
        for v in yt:
            out.append(f'<line x1="{ml}" x2="{ml + pw}" y1="{Y(v):.1f}" y2="{Y(v):.1f}" stroke="#e5e5e5"/>')
            out.append(f'<text x="{ml - 6}" y="{Y(v) + 4:.1f}" text-anchor="end" fill="#555">{_fmt(v)}</text>')
        for v in xt:
            out.append(f'<line x1="{X(v):.1f}" x2="{X(v):.1f}" y1="{y0}" y2="{y0 + panel_h}" stroke="#eee"/>')
            if k == len(panels) - 1:
                out.append(f'<text x="{X(v):.1f}" y="{y0 + panel_h + 14}" text-anchor="middle" fill="#555">{_fmt(v)}</text>')
        out.append(f'<text x="14" y="{y0 + panel_h / 2}" transform="rotate(-90 14 {y0 + panel_h / 2})" '
                   f'text-anchor="middle" fill="#333">{html.escape(p["ylabel"])}</text>')
        for i, (lab, v) in enumerate(p.get("limits", [])):
            out.append(f'<line x1="{ml}" x2="{ml + pw}" y1="{Y(v):.1f}" y2="{Y(v):.1f}" stroke="#999" '
                       f'stroke-dasharray="5,4"/>')
            out.append(f'<text x="{ml + 4}" y="{Y(v) - 3:.1f}" fill="#777">{html.escape(lab)}</text>')
        for i, (lab, yv) in enumerate(p["series"]):
            yv = np.asarray(yv)
            step = max(1, len(x) // 1500)
            pts = " ".join(f"{X(a):.1f},{Y(b):.1f}" for a, b in zip(x[::step], yv[::step]))
            col = PALETTE[i % len(PALETTE)]
            out.append(f'<polyline fill="none" stroke="{col}" stroke-width="1.6" points="{pts}"/>')
            out.append(f'<rect x="{ml + pw + 6}" y="{y0 + 8 + i * 16}" width="10" height="3" fill="{col}"/>')
            out.append(f'<text x="{ml + pw + 20}" y="{y0 + 13 + i * 16}">{html.escape(lab)}</text>')
    out.append(f'<text x="{ml + pw / 2}" y="{H - 6}" text-anchor="middle" fill="#333">{html.escape(xlabel)}</text>')
    out.append("</svg>")
    with open(path, "w", encoding="utf8") as fh:
        fh.write("\n".join(out))
