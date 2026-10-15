#!/usr/bin/env python3
"""Phase E.5's pick among the staged value leaf and the leafless menu shapes (valueleaf.sh).

  usage: vlq_adopt_pick.py <m_<key>_shape.log | m_<key>_ab.log> [--tol T] [--candidates staged nlesc nlfit]

Each candidate arm is judged against `live` by vlq_ab_report.py --verdict, i.e. by the USER's adoption rule
(2026-10-11): ADOPT iff CHEAPER and quality NOT NOTABLY WORSE. With the shape arms (USER 2026-10-14, the
no-leaf screen) more than one candidate can pass, and the rule does not say which to take, so:

  * none passes      -> no adoption; every verdict is printed (a TRADE stays the user's call).
  * one passes       -> that one.
  * several pass     -> the CHEAPEST, unless another passing candidate is NOTABLY BETTER than it (paired
                        against it: mean delta < -T at t < -2), in which case the best-quality such one.
                        "Better is primarily a performance metric" (the user's rule) decides by cost, and a
                        notable quality gain is not thrown away to save cost the rule already accepted.

Output: one `VERDICT arm=...` line per candidate present in the log, then
`PICK arm=<arm>|none why=<...>`. Exit 0 = adopt the picked arm, 2 = adopt nothing, 1 = error.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPORT = os.path.join(HERE, "vlq_ab_report.py")
LINE = re.compile(r"(?:[\w]+-)?(\w+?)_s(\d+): played=")


def verdict(log, baseline, arm, tol):
    r = subprocess.run([sys.executable, REPORT, log, baseline, "--verdict", arm, "--tol", str(tol)],
                       capture_output=True, text=True)
    v = next((l for l in r.stdout.splitlines() if l.startswith("VERDICT")), None)
    if v is None or r.returncode not in (0, 2):
        raise SystemExit(f"vlq_ab_report failed for {arm} vs {baseline} (rc={r.returncode}):\n{r.stdout}{r.stderr}")
    f = dict(re.findall(r"(\w+)=([^ ]+)", v.split(" why=")[0]))
    return {"line": v, "adopt": r.returncode == 0, "delta": float(f.get("delta", "nan")),
            "t": float(f.get("t", "0")), "cost": float(f.get("cost", "inf").rstrip("x"))}


def main(argv):
    tol, cands = 0.002, ["staged", "nlesc", "nlfit"]
    if "--tol" in argv:
        i = argv.index("--tol"); tol = float(argv[i + 1]); del argv[i:i + 2]
    if "--candidates" in argv:
        i = argv.index("--candidates"); cands = argv[i + 1:]; del argv[i:]
    if len(argv) != 1:
        print(__doc__); return 1
    log = argv[0]
    arms = {m.group(1) for m in (LINE.match(l) for l in open(log)) if m}
    if "live" not in arms:
        print(f"PICK arm=none why=no live arm in {log}"); return 1
    present = [c for c in cands if c in arms]
    vs = {}
    for c in present:
        vs[c] = verdict(log, "live", c, tol)
        print(vs[c]["line"])
    ok = [c for c in present if vs[c]["adopt"]]
    if not ok:
        print("PICK arm=none why=no candidate is cheaper than live with quality not notably worse")
        return 2
    c0 = min(ok, key=lambda c: vs[c]["cost"])
    better = []
    for c in ok:
        if c == c0:
            continue
        p = verdict(log, c0, c, tol)
        if p["delta"] < -tol and p["t"] < -2.0:
            better.append(c)
            print(f"  {c} is NOTABLY BETTER than the cheapest passing arm {c0}: {p['line']}")
    if better:
        pick = min(better, key=lambda c: vs[c]["delta"])
        why = f"notably better than the cheapest passing arm {c0}; cost {vs[pick]['cost']:.2f}x vs live"
    else:
        pick = c0
        why = (f"the cheapest of {len(ok)} passing arm(s) ({vs[pick]['cost']:.2f}x vs live, "
               f"delta {vs[pick]['delta']:+.5f})")
    print(f"PICK arm={pick} why={why}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
