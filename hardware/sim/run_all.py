"""
Run every hardware simulation + static check and write sim/report.md.

    "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" hardware\\sim\\run_all.py [name ...]

Exit code = number of failing tests (0 = all pass).
"""
import datetime
import os
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import scenarios  # noqa: E402
import static_checks  # noqa: E402


def fmt(v):
    if isinstance(v, float):
        return f"{v:.4g}"
    return str(v)


def badge(c):
    if c["kind"] in ("FAIL", "WARN"):
        return "PASS" if c["ok"] else c["kind"]
    return c["kind"]


def main(only, tests=None, report=os.path.join(HERE, "report.md"), title="Hardware simulation report",
         source="`sim/run_all.py` from `tools/design.py`"):
    tests = tests or static_checks.ALL + scenarios.ALL
    if only:
        tests = [t for t in tests if any(o in t.__name__ for o in only)]
    results = []
    for t in tests:
        t0 = time.time()
        try:
            res = t()
        except Exception as e:  # keep going; report the crash as a failure
            res = scenarios.Result(t.__name__[2:], t.__name__, "")
            res.error = f"{type(e).__name__}: {e}\n{traceback.format_exc()}"
        res.secs = time.time() - t0
        status = "ERROR" if res.error else ("FAIL" if res.failed else ("WARN" if res.warned else "PASS"))
        res.status = status
        print(f"{status:5s} {res.name:22s} {res.secs:6.1f}s  {res.title}")
        for c in res.checks:
            if badge(c) in ("FAIL", "WARN"):
                print(f"        {badge(c)}: {c['desc']} = {fmt(c['val'])} {c['unit']} (need {c['limit']})")
        if res.error:
            print("        " + res.error.splitlines()[0])
        results.append(res)
    write_report(results, report, title, source)
    nfail = sum(r.status in ("FAIL", "ERROR") for r in results)
    print(f"\n{len(results) - nfail}/{len(results)} tests passed, "
          f"{sum(r.status == 'WARN' for r in results)} with warnings. Report: {os.path.relpath(report, os.path.join(HERE, '..'))}")
    return nfail


def write_report(results, report, title, source):
    L = [f"# {title}", "",
         f"Generated {datetime.datetime.now():%Y-%m-%d %H:%M} by {source} "
         "(ngspice from KiCad + behavioural models; see `sim/README.md` for what is and isn't modelled).", "",
         "| Status | Test | What it checks |", "|---|---|---|"]
    for r in results:
        L.append(f"| **{r.status}** | [{r.name}](#{r.name.replace('_', '-')}) | {r.title} |")
    L.append("")
    for r in results:
        L += [f"## {r.name}", "", f"**{r.title}** - {r.status} ({r.secs:.1f} s)", "", r.what, ""]
        if r.error:
            L += ["```", r.error, "```", ""]
        if r.checks:
            L += ["| Result | Check | Value | Limit |", "|---|---|---|---|"]
            for c in r.checks:
                L.append(f"| {badge(c)} | {c['desc']} | {fmt(c['val'])} {c['unit']} | {c['limit']} |")
            L.append("")
        for n in r.notes:
            L.append(f"- {n}")
        if r.notes:
            L.append("")
        for p in r.plots:
            L += [f"![{r.name}](out/{p})", ""]
    with open(report, "w", encoding="utf8") as fh:
        fh.write("\n".join(L))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
