#!/usr/bin/env python3
"""The tournament report: the three numbers the user asked for, per test, per life total.

    python3 scripts/tourney_report.py --err logs/tourney/run/tourney.err --games 10000

User spec (2026-08-19), verbatim: "percentage change across all games, percentage change across
games where the card was available, and better/worse totals."

  1. % change over ALL games     -- how much the swap matters at all. This is the number that
                                    licenses "I don't care about this tiny change, because
                                    <unmodelled effect> outweighs it" -- e.g. Impolite Entrance's
                                    trample, which this engine cannot see.
  2. % change over LIVE games    -- restricted to games where the swapped slot was actually drawn
                                    (and, separately, actually CAST). "The main games that matter
                                    are the ones where which cards actually matters." Undiluted.
  3. better / worse GAME COUNTS  -- which card to take. A count margin is a recommendation even
                                    when the size is negligible.

Every margin carries its sigma = margin / sqrt(divergent). That is not pedantry: under the null
each divergent game is a coin flip, so the margin's noise is sqrt(divergent). The margin grows
with N but its noise only as sqrt(N), so a FIXED count bar ("20 games", "100 games") means
completely different things at different sample sizes -- at 10,000 divergent games a margin of 20
is 0.2 sigma, pure noise.

Every comparison is a MARGINAL of the 60-arm factorial: level A vs level B of one factor, paired
game-by-game inside each of the 12-15 contexts formed by the other two factors, then pooled. The
per-context breakdown is printed too, and that is not decoration -- reporting this deck's Draught
slot as a pooled null once hid two large opposite effects (see the never-report-a-null-unstratified
lesson and mirrorwing-trick-suite-result.md). A pooled row is only readable once the contexts agree.
"""
import argparse, array, json, math, os, statistics as st, sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARMS = os.path.join(ROOT, "logs", "tourney", "arms")
ARMS2 = os.path.join(ROOT, "logs", "tourney", "arms2")

TF   = ["tf3lib0", "tf2lib1", "tf1lib2", "tf0lib3"]
AO   = ["a4o0", "a2o2", "a0o4"]
SLOT = ["scale", "draught", "entrance", "oracle", "anger"]

# The SHIPPED deck's card in each contested slot. The apparatus is the table fitted to that deck,
# so it flatters these levels -- which is what makes the cheap test ONE-SIDED and therefore usable
# (user, 2026-08-19: "Any that do better than the original with the same profile are expected to
# win out"). A challenger that wins under an apparatus tilted against it has won conservatively;
# an incumbent win, or a near-tie, is exactly what the apparatus would produce anyway and settles
# nothing. Only the latter need an expensive generation test.
INCUMBENT = {"tf": "tf3lib0", "ao": "a4o0", "slot": "scale"}

LEVEL_NAME = {
    "tf3lib0": "3 Twinflame",       "tf2lib1": "2 Twinflame / 1 Libation",
    "tf1lib2": "1 Twinflame / 2 Libation", "tf0lib3": "3 Luxurious Libation",
    "a4o0": "4 Ancestral Anger",    "a2o2": "2 Anger / 2 Oracle",
    "a0o4": "4 Oracle's Restoration",
    "scale": "2 Scale the Heights", "draught": "2 Fortifying Draught",
    "entrance": "4 Impolite Entrance (+2)", "oracle": "+2 Oracle's Restoration",
    "anger": "+2 Ancestral Anger",
}


# ---------------------------------------------------------------- loading

def numbering(arm):
    """Arms live in two directories: the map-A set (logs/tourney/arms) and the map-B set
    (logs/tourney/arms2). Resolve against whichever holds this arm."""
    p = os.path.join(ARMS, arm + ".numbering.json")
    if not os.path.exists(p):
        p = os.path.join(ARMS2, arm + ".numbering.json")
    d = json.load(open(p))
    return {n: name for name, ns in d.items() for n in ns}


def load(err, games, maxt):
    """-> {job: (wt[], seen_mask[], cast_mask[])}, arrays indexed by game index.

    Masks are 60-bit ints over CARD NUMBERS, which is why the run writes numbers rather than names:
    one 8-byte field answers every "did this game see / cast any of these slots" question the report
    asks, and the whole 1.2M-game run stays in ~20 MB instead of 24 GB of JSON traces."""
    W, S, C = {}, {}, {}

    def slot(job):
        if job not in W:
            W[job] = array.array("b", [maxt + 1]) * games
            S[job] = array.array("q", [0]) * games
            C[job] = array.array("q", [0]) * games
        return job

    def bits(s):
        m = 0
        for t in s.split(","):
            if t:
                m |= 1 << int(t)
        return m

    nwin = ncards = 0
    with open(err) as fh:
        for line in fh:
            if line.startswith("[win] "):
                _, j, g, w = line.split()
                job, gi, wt = j[4:], int(g[3:]), int(w[3:])
                # BRK_ jobs are the apparatus bracket, not arms: they run their own (smaller) game
                # count and are read by tourney_bracket.py. Counting them here made the
                # under-filled-arm guard fire on a batch that had in fact completed.
                if job.startswith("BRK_") or gi >= games:
                    continue
                slot(job)
                W[job][gi] = maxt + 1 if (wt < 0 or wt > maxt) else wt
                nwin += 1
            elif line.startswith("[cards] "):
                _, j, g, se, ca = line.split()
                job, gi = j[4:], int(g[3:])
                if job.startswith("BRK_") or gi >= games:
                    continue
                slot(job)
                S[job][gi] = bits(se[5:])
                C[job][gi] = bits(ca[5:])
                ncards += 1
    if not nwin:
        sys.exit(f"REFUSING {err}: no [win] lines -- was MTG_DUMP_WINS set?")
    dead = [j for j in W if all(x == maxt + 1 for x in W[j])]
    if dead:
        sys.exit(f"REFUSING {err}: {len(dead)} arms have no finished game ({dead[:4]}) -- the batch "
                 "did not complete (check for an OOM kill). Nothing here is a number to read.")
    short = {j: sum(1 for x in C[j] if x) for j in W}
    if ncards and min(short.values()) < games * 0.99:
        sys.exit(f"REFUSING {err}: some arms are missing [cards] lines (min {min(short.values())} "
                 f"of {games}) -- the live subset would silently be a different sample per arm.")
    return {j: (W[j], S[j], C[j]) for j in W}


# ---------------------------------------------------------------- statistics

def stats(D, better, worse):
    n = len(D)
    m = st.fmean(D) if n else 0.0
    se = (st.stdev(D) / n ** .5) if n > 1 else 0.0
    div = better + worse
    marg = better - worse
    return dict(n=n, md=m, se=se, t=(m / se if se else 0.0),
                better=better, worse=worse, margin=marg,
                sigma=(marg / math.sqrt(div) if div else 0.0))


def contrast(data, arms_a, arms_b, games, mask_bits=0, need="", maxt=8):
    """Paired A-vs-B over a list of (armA, armB) context pairs.

    `need`: ""=all games, "seen"=the swapped slot reached a hand in either arm, "cast"=it was
    actually cast (or played, for a land) in either arm. The user's rule for a case log is the
    "cast" one; "seen" is the availability number.

    Availability is `seen | cast`, not `seen` alone. The engine's hand snapshot is taken at the END
    of each phase, so a card drawn mid-phase (Ancestral Anger's draw) and cast in that same phase
    never appears in one -- and a card that was cast was, self-evidently, available. Without the
    union, 72 of 120 probe games counted a card as unavailable in the very game they cast it."""
    D, mean_a, mean_b, better, worse = [], [], [], 0, 0
    per_ctx = []
    for a, b in zip(arms_a, arms_b):
        wa, sa, ca = data[a]
        wb, sb, cb = data[b]
        d = []
        for gi in range(games):
            if need == "seen" and not ((sa[gi] | sb[gi] | ca[gi] | cb[gi]) & mask_bits):
                continue
            if need == "cast" and not ((ca[gi] | cb[gi]) & mask_bits):
                continue
            x, y = wa[gi], wb[gi]
            d.append(y - x)
            mean_a.append(x)
            mean_b.append(y)
            if y < x:
                better += 1
            elif y > x:
                worse += 1
        if d:
            per_ctx.append((a, b, st.fmean(d), len(d)))
        D.extend(d)
    s = stats(D, better, worse)
    s["mean_a"] = st.fmean(mean_a) if mean_a else 0.0
    s["mean_b"] = st.fmean(mean_b) if mean_b else 0.0
    s["pct"] = 100.0 * s["md"] / s["mean_a"] if s["mean_a"] else 0.0
    s["per_ctx"] = per_ctx
    return s


def row(tag, s):
    return (f"| {tag} | {s['n']:,} | {s['mean_a']:.4f} | {s['mean_b']:.4f} | "
            f"**{s['pct']:+.3f}%** | {s['md']:+.4f} | {s['t']:+.2f} | {s['better']:,} | "
            f"{s['worse']:,} | **{s['margin']:+,}** | {s['sigma']:+.1f} |")


HDR = ("| subset | games | mean A | mean B | % change | turns | t | better | worse | margin | s |\n"
       "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")


# ---------------------------------------------------------------- tests

def slot_bits(a, b):
    """Bit mask of the card numbers where two arms differ -- the swapped slot, by construction."""
    na, nb = numbering(a), numbering(b)
    return sum(1 << k for k in set(na) | set(nb) if na.get(k) != nb.get(k))


def verdict(factor, la, lb, s, band, seed=0):
    """Verdict for one comparison. -> (tag, sentence).

    Read SYMMETRICALLY, because the bracket measured the fit tilt at -0.0088 +/- 0.0103 (20 life)
    and -0.0187 +/- 0.0120 (30 life) -- under 2se in both cases, and if anything pointing at the
    CHALLENGER rather than the incumbent. The one-sided rule this file originally implemented
    assumed a tilt toward the incumbent; that assumption is not supported, so neither side gets to
    claim it won "against the bias".

    What the bracket DID establish is a magnitude: the apparatus can move a comparison by at most
    about `band` turns. So:

      |t| < 2                       CONFIRM   sampling-limited. Held-out seeds, cheap.
      |t| >= 2 and effect > band    CONCLUSIVE apparatus cannot account for it.
      |t| >= 2 and effect <= band   GENERATE  real and reproducible, but small enough that a
                                              different keep table could plausibly flip it. This is
                                              the only case worth an expensive generation.
    """
    inc = INCUMBENT.get(factor)
    sym = inc not in (la, lb)
    chal = None if sym else (lb if la == inc else la)
    chal_gain = 0.0 if sym else (-s["md"] if chal == lb else s["md"])
    eff = abs(s["md"])
    who = (f"{lb if s['md'] < 0 else la}") if sym else (chal if chal_gain > 0 else inc)
    if abs(s["t"]) < 2.0:
        lean = f"{who} leads by {eff:.4f}t" if eff else "dead level"
        return "CONFIRM", (f"{lean} but only t={s['t']:+.1f} -- sampling-limited, not "
                           f"apparatus-limited. Re-run on held-out seeds (--seed {seed + 500000}); "
                           f"a keep table would not help.")
    if band and eff <= band:
        return "GENERATE", (f"{who} wins by {eff:.4f}t (t={s['t']:+.1f}) -- real and reproducible, "
                            f"but inside the {band:.4f}t the apparatus itself can move a comparison. "
                            f"A different keep table could plausibly flip it; this one is worth "
                            f"generating for.")
    tag = "MIXED" if sym else ("ADOPT" if chal_gain > 0 else "INCUMBENT")
    extra = "" if band else " (no apparatus band measured -- provisional)"
    return tag, (f"{who} wins by {eff:.4f}t (t={s['t']:+.1f}), clear of the {band:.4f}t apparatus "
                 f"band{extra}. Settled without generating anything.")


def test(data, games, title, factor, levels, contexts, name_of, maxt, note="", bias=0.0,
         seed=0):
    print(f"\n## {title}\n")
    if note:
        print(note + "\n")
    print(f"up to {len(contexts)} contexts x {games:,} paired games per comparison "
          f"(each row states its own n)\n")
    print("`% change` is the change in mean win turn; **negative means the second card is better** "
          "(it wins sooner).\n")
    out = []
    for i, la in enumerate(levels):
        for lb in levels[i + 1:]:
            # Keep only contexts BOTH levels exist in. The aliased apparatus runs a 24-arm subset
            # (the flex-slot and entrance arms overflow the shipped table's count caps), so a
            # context list built from the full factorial would name arms this run never played --
            # and a KeyError at 3am is a worse failure than a smaller, honest table.
            ctxs = [c for c in contexts if name_of(la, c) in data and name_of(lb, c) in data]
            if not ctxs:
                continue
            aa = [name_of(la, c) for c in ctxs]
            bb = [name_of(lb, c) for c in ctxs]
            bits = slot_bits(aa[0], bb[0])
            print(f"\n### {LEVEL_NAME[la]}  ->  {LEVEL_NAME[lb]}\n")
            nm = numbering(aa[0])
            nb = numbering(bb[0])
            ks = sorted(k for k in range(1, 61) if bits >> k & 1)
            print(f"swapped card numbers {ks}: "
                  f"{sorted({nm.get(k, '-') for k in ks})} -> {sorted({nb.get(k, '-') for k in ks})}\n")
            print(HDR)
            s_all = contrast(data, aa, bb, games, maxt=maxt)
            print(row("all games", s_all))
            s_seen = contrast(data, aa, bb, games, bits, "seen", maxt)
            print(row("live: slot DRAWN", s_seen))
            s_cast = contrast(data, aa, bb, games, bits, "cast", maxt)
            print(row("live: slot CAST", s_cast))
            tag, why = verdict(factor, la, lb, s_all, bias, seed)
            print(f"\n**{tag}** -- {why}\n")
            print(f"pooled over {len(ctxs)} contexts")
            worst = sorted(s_all["per_ctx"], key=lambda r: r[2])
            ctx_of = lambda arm: "_".join(p for p in arm.split("_") if p not in (la, lb))
            print(f"\nper-context spread (mean turn delta, {len(worst)} contexts): "
                  f"best {worst[0][2]:+.4f} @ {ctx_of(worst[0][0])} ... worst {worst[-1][2]:+.4f} "
                  f"@ {ctx_of(worst[-1][0])}")
            signs = [1 if r[2] > 0 else -1 for r in worst if abs(r[2]) > 1e-9]
            if signs and (max(signs) != min(signs)):
                flip = sum(1 for r in worst if r[2] > 0)
                print(f"  MIXTURE WARNING: {flip} of {len(worst)} contexts favour the FIRST card. "
                      "Read the contexts, not the pooled row.")
            out.append((la, lb, s_all, s_seen, s_cast))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--err", default="logs/tourney/run/tourney.err")
    ap.add_argument("--games", type=int, required=True)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--lives", default="20,30")
    ap.add_argument("--tsv", default="")
    ap.add_argument("--seed", type=int, default=1200000,
                    help="the run's seed, so CONFIRM rows can print a held-out one")
    ap.add_argument("--bias", default="0",
                    help="apparatus band per life total, comma-separated to match --lives")
    ap.add_argument("--bias-unused", type=float, default=0.0,
                    help="measured apparatus bias in turns; sets the 'too close to call' band. "
                         "0 = unknown, verdicts then say so rather than pretending a threshold.")
    a = ap.parse_args()
    lives = [int(x) for x in a.lives.split(",") if x.strip()]
    bands = [float(x) for x in str(a.bias).split(",") if x.strip()]
    band_of = {l: (bands[i] if i < len(bands) else bands[-1]) for i, l in enumerate(lives)}
    data = load(a.err, a.games, a.max_turns)

    rows = []
    for life in lives:
        L = f"@L{life}"
        d = {k[:-len(L)]: v for k, v in data.items() if k.endswith(L)}
        if not d:
            continue
        tag = "20 life (regular)" if life == 20 else f"{life} life (2HG)"
        print(f"\n\n# Tournament report -- {tag}\n")
        print(f"- {a.games:,} games per arm, max_turns {a.max_turns} (an unwon game scores "
              f"{a.max_turns + 1})\n- 60 arms, one pooled batch, one shared apparatus "
              "(pool keep table + card scores); every pair is paired on the same seeds\n")

        rows += [(life, "T1", *r) for r in test(
            d, a.games, "Test 1 -- Twinflame vs Luxurious Libation", "tf", TF,
            [(x, y) for x in AO for y in SLOT], lambda l, c: f"{l}_{c[0]}_{c[1]}", a.max_turns, bias=band_of[life], seed=a.seed)]

        rows += [(life, "T2", *r) for r in test(
            d, a.games, "Test 2 -- Ancestral Anger vs Oracle's Restoration", "ao", AO,
            [(x, y) for x in TF for y in ("scale", "draught", "entrance")],
            lambda l, c: f"{c[0]}_{l}_{c[1]}", a.max_turns, bias=band_of[life], seed=a.seed,
            note="Contexts exclude the `oracle` and `anger` flex slots: those change the SAME two "
                 "cards' counts, so including them would compare a level against itself.")]

        rows += [(life, "T3", *r) for r in test(
            d, a.games, "Test 3 -- the Scale the Heights slot (four-way)", "slot", SLOT,
            [(x, y) for x in TF for y in AO], lambda l, c: f"{c[0]}_{c[1]}_{l}", a.max_turns, bias=band_of[life], seed=a.seed)]

    if a.tsv:
        with open(a.tsv, "w") as fh:
            fh.write("life\ttest\tA\tB\tsubset\tgames\tmeanA\tmeanB\tpct\tturns\tt\tbetter\t"
                     "worse\tmargin\tsigma\n")
            for life, t, la, lb, s_all, s_seen, s_cast in rows:
                for name, s in (("all", s_all), ("drawn", s_seen), ("cast", s_cast)):
                    fh.write(f"{life}\t{t}\t{la}\t{lb}\t{name}\t{s['n']}\t{s['mean_a']:.5f}\t"
                             f"{s['mean_b']:.5f}\t{s['pct']:.4f}\t{s['md']:.5f}\t{s['t']:.2f}\t"
                             f"{s['better']}\t{s['worse']}\t{s['margin']}\t{s['sigma']:.2f}\n")
        print(f"\n\nTSV -> {a.tsv}")


if __name__ == "__main__":
    main()
