#!/usr/bin/env python3
"""How much is the mulligan apparatus worth on this deck, and how far does it tilt?

    python3 scripts/tourney_bracket.py --err logs/tourney/run_alias/tourney.err --games 4000

Two numbers, both needed to read the cheap tournament one-sidedly (user, 2026-08-19: "we might be
able to skip cases where the incumbent wins by a lot as well as long as we can get a measure of how
high the bias should be").

  TABLE WORTH   mean win turn with NO keep table minus with the aliased table, per decklist.
                The ceiling on anything the apparatus can possibly explain. If the whole table is
                worth 0.01t, no comparison in the report can be an apparatus artifact, and every
                verdict stands regardless of which side won.

  FIT BIAS      table worth for the INCUMBENT list minus for the CHALLENGER list. The table is
                fitted to the incumbent, so it should help the incumbent more, and that difference
                IS the tilt -- measured on this deck at this depth rather than imported from a
                different deck's screening run.

Both are difference-in-differences on the SAME game indices, so the standard error is a paired one
and the decision band is `bias + 2*se`, not a multiplier someone picked.

A NEGATIVE fit bias (the table helping the challenger more) means the tilt does not run the way the
one-sided argument assumes. That would invalidate the "incumbent wins by a lot -> skip" branch, and
it is reported loudly rather than clipped to zero.
"""
import argparse, math, statistics as st, sys

INC = "tf3lib0_a4o0_scale"
CHAL = "tf0lib3_a0o4_draught"


def load(err, games, maxt):
    W = {}
    with open(err) as fh:
        for line in fh:
            if not line.startswith("[win] "):
                continue
            _, j, g, w = line.split()
            job, gi, wt = j[4:], int(g[3:]), int(w[3:])
            if not job.startswith("BRK_") or gi >= games:
                continue
            W.setdefault(job, {})[gi] = maxt + 1 if (wt < 0 or wt > maxt) else wt
    return W


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--err", required=True)
    ap.add_argument("--games", type=int, required=True)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--lives", default="20,30")
    a = ap.parse_args()
    W = load(a.err, a.games, a.max_turns)
    if not W:
        sys.exit(f"no BRK_ jobs in {a.err} -- was the run given --bracket?")

    print("# Apparatus bracket -- what the keep table is worth, and which way it tilts\n")
    out = {}
    for life in [int(x) for x in a.lives.split(",") if x.strip()]:
        need = [f"BRK_{d}_{s}@L{life}" for d in (INC, CHAL) for s in ("tab", "not")]
        if any(n not in W for n in need):
            continue
        it, inot, ct, cnot = (W[f"BRK_{INC}_tab@L{life}"], W[f"BRK_{INC}_not@L{life}"],
                              W[f"BRK_{CHAL}_tab@L{life}"], W[f"BRK_{CHAL}_not@L{life}"])
        gis = sorted(set(it) & set(inot) & set(ct) & set(cnot))
        wi = [inot[g] - it[g] for g in gis]          # table worth, incumbent list
        wc = [cnot[g] - ct[g] for g in gis]          # table worth, challenger list
        dd = [x - y for x, y in zip(wi, wc)]         # fit bias, paired
        se = lambda v: st.stdev(v) / len(v) ** .5 if len(v) > 1 else 0.0
        mi, mc, md = st.fmean(wi), st.fmean(wc), st.fmean(dd)
        s = se(dd)
        # |bias| + 2se, not bias + 2se: the band is a CONSERVATIVE bound on how far the apparatus
        # could move a comparison, and that is a magnitude question. Signing it made the band shrink
        # when the tilt came out negative -- i.e. loosest exactly where the assumed direction had
        # just been contradicted.
        band = abs(md) + 2 * s
        print(f"## {life} life   ({len(gis):,} paired games)\n")
        print(f"| quantity | turns | se | reading |")
        print(f"|---|---:|---:|---|")
        print(f"| table worth, incumbent ({INC}) | {mi:+.4f} | {se(wi):.4f} | "
              f"{'table helps' if mi > 0 else 'table HURTS'} |")
        print(f"| table worth, challenger ({CHAL}) | {mc:+.4f} | {se(wc):.4f} | "
              f"{'table helps' if mc > 0 else 'table HURTS'} |")
        print(f"| **fit bias** (incumbent - challenger) | **{md:+.4f}** | {s:.4f} | "
              f"{'tilts to the incumbent, as assumed' if md > 0 else 'TILTS THE OTHER WAY'} |")
        print(f"\ndecision band for the one-sided rule: **{max(band, 0.0):.4f}t** (bias + 2se)\n")
        if abs(md) < 2 * s:
            print(f"> The fit tilt is NOT measurably different from zero ({md:+.4f} +/- {s:.4f}, "
                  f"{abs(md)/s if s else 0:.1f} se).\n"
                  "> The one-sided reading is therefore unsupported in EITHER direction: neither a\n"
                  "> challenger win nor an incumbent win can be credited to the apparatus. Read the\n"
                  "> report symmetrically, and treat the band above as the ambiguity zone for both.\n")
        elif md < 0:
            print("> The tilt runs toward the CHALLENGER, not the incumbent -- the opposite of what\n"
                  "> the one-sided rule assumes. An INCUMBENT win is then the conservative one, and\n"
                  "> a challenger win inside the band is the case needing a generation test.\n")
        out[life] = max(band, 0.0)
    if out:
        print("\nPass to the report as the too-close-to-call band:")
        for life, b in out.items():
            print(f"  --bias {b:.4f}   # {life} life")


if __name__ == "__main__":
    main()
