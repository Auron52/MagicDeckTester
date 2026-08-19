#!/usr/bin/env python3
"""Tournament report: the three numbers the user asked for, per comparison, per life total.

User spec (2026-08-19): "percentage change across all games, percentage change across games
where the card was available, and better/worse totals."

  1. % change over ALL games      -- how much the swap matters overall. This is the number that
                                     says "I don't care about this tiny change, because <some
                                     unmodelled effect> outweighs it" (e.g. Impolite Entrance's
                                     trample, which this engine cannot see).
  2. % change over LIVE games     -- restricted to games where BOTH arms drew the swapped card
                                     number, i.e. the games where the change could actually act.
                                     "The main games that matter are the ones where which cards
                                     actually matters." Undiluted by games the card never saw.
  3. better / worse GAME COUNTS   -- which card to take. A count margin is a recommendation even
                                     when the size is negligible.

Each count margin is printed with its sigma. That is NOT pedantry: under the null each divergent
game is a coin flip, so the margin's noise is sqrt(divergent). The margin grows with N but its
noise only as sqrt(N), so a FIXED count bar ("20 games", "100 games") means completely different
things at different sample sizes -- at 10,000 divergent games a margin of 20 is 0.2 sigma, pure
noise. Reporting both keeps the count interpretable.

Arms are paired on the same seeds and share one apparatus, so every comparison is paired.
Games with no win by max_turns score max_turns+1.

Usage:
  python3 scripts/tourney_report.py --wins logs/.../batch.err --numbering-dir logs/.../arms \
      --games N [--max-turns 8] [--label "20 life"] [--tsv OUT.tsv]
"""
import argparse, collections, json, math, os, re, statistics as st, sys


def load_wins(path, arms, n, maxt):
    out = {a: {g: maxt + 1 for g in range(n)} for a in arms}
    seen = collections.Counter()
    pat = re.compile(rb"^\[win\] job=(\S+) gi=(\d+) wt=(\d+)")
    with open(path, "rb") as fh:
        for line in fh:
            m = pat.match(line)
            if not m:
                continue
            a = m.group(1).decode(); gi = int(m.group(2))
            if a in out and gi < n:
                out[a][gi] = int(m.group(3)); seen[a] += 1
    dead = [a for a in arms if seen[a] == 0]
    if dead:
        sys.exit(f"REFUSING {path}: arms {dead} have zero win lines -- the batch did not finish "
                 f"(check for an OOM kill). Per-arm: {dict(seen)}")
    return out


def load_numbering(d, arm):
    """{card number -> card name} for one arm."""
    p = os.path.join(d, arm, "numbering.json")
    if not os.path.exists(p):
        p = os.path.join(d, f"{arm}.numbering.json")
    if not os.path.exists(p):
        return {}
    raw = json.load(open(p))
    out = {}
    for name, nums in raw.items():
        for x in nums:
            out[int(x)] = name
    return out


def live_slots(numA, numB):
    """Card NUMBERS whose card differs between the two arms -- the swapped slot."""
    return {k for k in set(numA) | set(numB) if numA.get(k) != numB.get(k)}


def drew(trace_dir, arm, gi):
    raise NotImplementedError


def stats(wa, wb, gis):
    d = [wb[g] - wa[g] for g in gis]
    mean_a = st.mean([wa[g] for g in gis])
    mean_b = st.mean([wb[g] for g in gis])
    md = st.mean(d)
    se = st.stdev(d) / len(d) ** .5 if len(d) > 1 else 0.0
    better = sum(1 for x in d if x < 0)     # B finishes sooner
    worse = sum(1 for x in d if x > 0)      # A finishes sooner
    div = better + worse
    marg = better - worse
    sigma = marg / math.sqrt(div) if div else 0.0
    pct = 100.0 * md / mean_a if mean_a else 0.0
    return dict(n=len(gis), mean_a=mean_a, mean_b=mean_b, md=md, se=se,
                t=(md / se if se else 0.0), better=better, worse=worse,
                margin=marg, sigma=sigma, pct=pct, div=div)


def fmt(tag, s):
    return (f"| {tag} | {s['n']:,} | {s['mean_a']:.4f} | {s['mean_b']:.4f} | "
            f"**{s['pct']:+.3f}%** | {s['md']:+.4f} | {s['better']:,} | {s['worse']:,} | "
            f"**{s['margin']:+,}** | {s['sigma']:+.1f}s |")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wins", required=True)
    ap.add_argument("--numbering-dir", default="")
    ap.add_argument("--arms", required=True, help="comma-separated, in tournament order")
    ap.add_argument("--games", type=int, required=True)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--label", default="")
    ap.add_argument("--drawn", default="", help="JSON {arm: {gi: [numbers drawn]}} for live subset")
    ap.add_argument("--tsv", default="")
    a = ap.parse_args()
    arms = [x.strip() for x in a.arms.split(",") if x.strip()]
    W = load_wins(a.wins, arms, a.games, a.max_turns)
    numb = {arm: load_numbering(a.numbering_dir, arm) for arm in arms} if a.numbering_dir else {}
    drawn = json.load(open(a.drawn)) if a.drawn else {}

    hdr = f"# Tournament report{(' -- ' + a.label) if a.label else ''}"
    print(hdr + "\n")
    print(f"- games per arm: {a.games:,}   max_turns {a.max_turns} (unwon scores {a.max_turns+1})")
    print("- every pair is PAIRED on the same seeds and shares one apparatus\n")
    print("`% change` is the change in mean win turn, **negative = the second arm is better**.")
    print("`margin` = better - worse; `s` = margin / sqrt(divergent), the margin's own noise "
          "scale (a fixed count bar means different things at different N).\n")

    rows = []
    for i, A in enumerate(arms):
        for B in arms[i + 1:]:
            print(f"\n## {A}  vs  {B}\n")
            slots = live_slots(numb.get(A, {}), numb.get(B, {})) if numb else set()
            if slots:
                names_a = sorted({numb[A][k] for k in slots if k in numb[A]})
                names_b = sorted({numb[B][k] for k in slots if k in numb[B]})
                print(f"swapped slot: {', '.join(names_a) or '-'}  ->  {', '.join(names_b) or '-'}"
                      f"   ({len(slots)} card numbers)\n")
            print("| subset | games | " + A + " | " + B +
                  " | % change | turns | better | worse | margin | s |")
            print("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
            allg = list(range(a.games))
            s_all = stats(W[A], W[B], allg)
            print(fmt("all games", s_all))
            s_live = None
            if slots and drawn:
                lg = [g for g in allg
                      if slots & (set(drawn.get(A, {}).get(str(g), []))
                                  | set(drawn.get(B, {}).get(str(g), [])))]
                if lg:
                    s_live = stats(W[A], W[B], lg)
                    print(fmt("live (drew the slot)", s_live))
            rows.append((A, B, s_all, s_live))

    if a.tsv:
        with open(a.tsv, "w") as fh:
            fh.write("armA\tarmB\tsubset\tgames\tmeanA\tmeanB\tpct\tturns\tbetter\tworse\t"
                     "margin\tsigma\tt\n")
            for A, B, s_all, s_live in rows:
                for tag, s in (("all", s_all), ("live", s_live)):
                    if not s:
                        continue
                    fh.write(f"{A}\t{B}\t{tag}\t{s['n']}\t{s['mean_a']:.5f}\t{s['mean_b']:.5f}\t"
                             f"{s['pct']:.4f}\t{s['md']:.5f}\t{s['better']}\t{s['worse']}\t"
                             f"{s['margin']}\t{s['sigma']:.2f}\t{s['t']:.2f}\n")
        print(f"\nTSV -> {a.tsv}")


if __name__ == "__main__":
    main()
