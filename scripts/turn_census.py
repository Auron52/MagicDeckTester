#!/usr/bin/env python3
"""Read a per-decision work census (MTG_TURN_CENSUS) and rank the turns that actually cost.

WHY THIS EXISTS.  Every other work instrument in this repo is a whole-run aggregate: `[rollout-stats]`
prints one number per counter for the process, and `cost.py` divides totals by games.  That can say
"this deck costs 2.9x" and can never say WHICH turns cost it, nor whether the memo and the pruners
engaged on those turns or sat idle.  The distinction is not academic here -- `matrix-cost-is-
abandonment-rate` established that ~90% of a Snow cell's wall clock is games pinned at the work
ceiling, so this engine's cost lives in a tail, and a lever sized against the mean is sized against
a number no real decision ever had.

USER 2026-09-30: *"it's crucial that we look at real turns... a list of something like 1000 slowest
turns in terms of branching and how our caching/cost pruning deals with them... we need to make sure
we are doing only the work that is necessary."*

    bash scripts/turn_census_run.sh <deck> ...        # produce a census (single-threaded; see below)
    python3 scripts/turn_census.py rank    <file>     # the slowest-N table
    python3 scripts/turn_census.py summary <file>     # concentration + does efficiency DECAY on the tail
    python3 scripts/turn_census.py explain <file> -n K  # one decision in full, with its repro command

READING THE OUTPUT -- the three questions, in the order they are worth asking:

  1. WHERE did the units go?  The 13 `u_*` columns are one counter per SearchBudget::Consume site
     and they sum EXACTLY to the row's `units` (the tool asserts this; a mismatch means a Consume
     site was added without a bucket, and every conclusion below would be drawn from an incomplete
     denominator).  So a slow turn NAMES the site that owns it instead of leaving it to be inferred.

  2. Did the CACHING engage?  `enum_hits` / `enum_misses` are the continuation memo.  The number
     that matters is not the hit RATE but whether that rate holds up on the expensive turns --
     `summary` prints the tail-vs-median ratio for exactly that reason.  A memo whose hit rate is
     fine at the median and collapses in the tail is a memo that is absent where it is needed.

  3. Is the work NECESSARY?  Three independent reads, and they disagree in an informative way:
       * `pay_calls / units` -- mana payment solves per charged unit.  The budget charges one unit
         per search node, so this is how much unbilled payment work each billed node drags behind
         it.  `cost-ratio-is-not-a-cost` records the lesson: +33% calls at -13% each is realised
         work, and the per-unit figure is the only form in which that is visible.
       * `(drop_tapped + drop_unpaid) / act_fired` -- the silently-dropped share.  These are lines
         the enumerator OFFERED and the executor could not run.  Work spent building and scoring
         them bought nothing.  USER standing rule: a silent drop is a defect to surface.
       * `bp_condemn_drops / bp_condemn_seen` -- condemnation's consult-to-decline ratio.  `seen`
         is carried beside `drops` deliberately: a rule asked 5,000 times that declines 40 is a
         different fact from a rule never reached, and the two are indistinguishable from `drops`
         alone.  This is condemnation's established failure signature (an inert arm dressed as a
         working one), so the census shows both halves per turn.

CONTENDED ROWS ARE NOT RANKED.  The census diffs process-global atomics, so a delta is only this
decision's work if no other thread was playing.  The engine stamps `contended=1` on any row written
while a second decision was in flight and this tool drops those rows from every ranking rather than
averaging them in -- a contaminated census reports itself instead of producing a plausible table.
"""

import argparse
import csv
import sys

# The 13 Consume-site buckets, in the engine's own order (unitsite::kNames).  Kept as a prefix test
# rather than a hard-coded list so a site added to the enum appears here without an edit -- the
# sum-check below is what guarantees the set is complete.
USITE_PREFIX = "u_"


def load(paths):
    """Return (rows, gate_note) over one or more census files.

    Several files is the NORMAL case, not a convenience: the census is a single-threaded
    instrument (it diffs process-global counters), so a full-machine census is N independent
    single-threaded PROCESSES writing N files -- see scripts/turn_census_run.sh.  Every file must
    report the same gate line; a run assembled from chunks with different instrument state is not
    one measurement, and mixing them would average a dark counter family into a live one.
    """
    if isinstance(paths, str):
        paths = [paths]
    gates = set()
    data_lines = []
    header = None
    for path in paths:
        with open(path) as f:
            for line in f:
                if line.startswith("#"):
                    gates.add(line[1:].strip())
                elif line.startswith("seed\t"):
                    if header is None:
                        header = line
                        data_lines.append(line)
                    elif line != header:
                        sys.exit(f"{path}: column set differs from the first file -- these censuses "
                                 "came from different binaries and cannot be pooled")
                else:
                    data_lines.append(line)
    if len(gates) > 1:
        sys.exit("census files disagree on their instrument state, so they are not one "
                 "measurement:\n  " + "\n  ".join(sorted(gates)))
    if len(data_lines) <= 1:
        sys.exit(f"no data rows in {len(paths)} file(s) -- was MTG_TURN_CENSUS set on a run that "
                 "reached a budgeted search decision?  A d0/greedy cell emits none by design.")
    rows = []
    for r in csv.DictReader(data_lines, delimiter="\t"):
        rows.append({k: int(v) for k, v in r.items() if v != ""})
    return rows, (gates.pop() if gates else "")


def check_and_split(rows, path):
    """Assert the units identity, then separate clean rows from contended ones."""
    usites = [k for k in rows[0] if k.startswith(USITE_PREFIX)]
    bad = [r for r in rows if sum(r[k] for k in usites) != r["units"]]
    if bad:
        r = bad[0]
        sys.exit(
            f"{path}: *** the u_* buckets do not sum to `units` on {len(bad)}/{len(rows)} rows "
            f"(first: turn {r['turn']}, buckets {sum(r[k] for k in usites)} vs units {r['units']}).\n"
            "    A SearchBudget::Consume site was added without a unitsite bucket, so the per-site\n"
            "    attribution is incomplete and nothing below should be believed.  Fix the bucket\n"
            "    (see MTG_TURN_CENSUS_FIELDS in TurnSolver.cpp) and re-run."
        )
    clean = [r for r in rows if r.get("contended", 0) == 0]
    return usites, clean, len(rows) - len(clean)


def ratio(a, b):
    return (a / b) if b else float("nan")


def fmt_pct(x):
    """`-` rather than `nan%`: a turn with no activations at all has no dropped SHARE, which is a
    different statement from a share of zero, and printing 0.0% there would invent a data point."""
    return "-" if x != x else f"{x*100:.1f}%"


def derive(r):
    """The three necessity reads plus the memo rate, per decision."""
    d = {}
    d["pay_per_unit"] = ratio(r["pay_calls"], r["units"])
    drops = r["drop_tapped"] + r["drop_unpaid"]
    d["drop_share"] = ratio(drops, r["act_fired"] + drops)
    d["memo_rate"] = ratio(r["enum_hits"], r["enum_hits"] + r["enum_misses"])
    d["condemn_rate"] = ratio(r["bp_condemn_drops"], r["bp_condemn_seen"])
    d["units_per_cand"] = ratio(r["units"], r["cand_scored"])
    # RE-WORK: the share of this decision's units that were spent and then superseded or discarded.
    # `lad_warm` is by design (iterative deepening commits one pass), `idwaste` is a pass the
    # overrun guard rolled back outright.  Kept separate because they are different claims: warm
    # units bought the depth estimate the committing pass relies on, discarded units bought nothing.
    d["waste_share"] = ratio(r.get("idwaste_units", 0), r["units"])
    d["warm_share"] = ratio(r.get("lad_warm_units", 0), r["units"])
    d["rework_share"] = ratio(r.get("idwaste_units", 0) + r.get("lad_warm_units", 0)
                              + r.get("fillin_units", 0), r["units"])
    return d


def top_site(r, usites):
    best = max(usites, key=lambda k: r[k])
    return best[len(USITE_PREFIX):], ratio(r[best], r["units"])


def cmd_rank(args):
    rows, gate = load(args.file)
    usites, clean, dropped = check_and_split(rows, args.file)
    print(f"# {gate}")
    print(f"# {len(rows)} decisions; {dropped} contended (excluded from the ranking)")
    clean.sort(key=lambda r: -r["units"])
    n = min(args.top, len(clean))
    print(f"# top {n} by units (deterministic); wall is a secondary read, single-threaded runs only")
    print()
    hdr = (f"{'#':>4} {'seed':>7} {'t':>3} {'units':>9} {'xbud':>5} {'ms':>7} {'site':>13} {'%':>4} "
           f"{'cands':>7} {'memo%':>6} {'pay/u':>6} {'drop%':>6} "
           f"{'waste%':>7} {'rework%':>8} {'cdm%':>5}")
    print(hdr)
    print("-" * len(hdr))
    for i, r in enumerate(clean[:n]):
        d = derive(r)
        site, share = top_site(r, usites)
        print(f"{i:>4} {r['seed']:>7} {r['turn']:>3} {r['units']:>9} "
              f"{ratio(r['units'], r['budget']):>5.1f} "
              f"{r['wall_us']/1000:>7.1f} {site:>13} {share*100:>3.0f}% "
              f"{r['cand_scored']:>7} {d['memo_rate']*100:>5.1f}% "
              f"{d['pay_per_unit']:>6.1f} {fmt_pct(d['drop_share']):>6} "
              f"{fmt_pct(d['waste_share']):>7} {fmt_pct(d['rework_share']):>8} "
              f"{d['condemn_rate']*100:>4.1f}%")


def pct(vals, p):
    if not vals:
        return float("nan")
    s = sorted(vals)
    i = min(len(s) - 1, max(0, int(round(p / 100.0 * (len(s) - 1)))))
    return s[i]


def cmd_summary(args):
    rows, gate = load(args.file)
    usites, clean, dropped = check_and_split(rows, args.file)
    print(f"# {gate}")
    print(f"# {len(rows)} decisions, {dropped} contended (excluded)")
    if not clean:
        sys.exit("every row is contended -- re-run the census single-threaded")
    clean.sort(key=lambda r: -r["units"])
    tot = sum(r["units"] for r in clean)
    # DARK COLUMNS. The structural safeguard against the census's worst failure mode: a column
    # family that reads 0 because its counter gate was never armed, which is indistinguishable
    # from "this never happens" and reads as a finding.  The engine's `#` gate line lists the gates
    # it KNOWS about, but that list is hand-maintained and was already wrong once (it omitted
    # MTG_DEDUP_CENSUS, MTG_BP_NSKIP_GLOBAL and the lazy leaf, so three families of zeros looked
    # like measurements).  This check needs no list: any column that is zero on EVERY row is
    # reported, and it is then the reader's job to say which of the two reasons applies.
    ident = {"seed", "turn", "pre", "root", "depth", "budget", "units", "wall_us", "contended"}
    dark = [k for k in rows[0] if k not in ident and all(r.get(k, 0) == 0 for r in rows)]
    if dark:
        print()
        print(f"*** {len(dark)} COLUMN(S) ARE ZERO ON ALL {len(rows)} ROWS -- do NOT read these as "
              "'it never happens':")
        # group by the family prefix so the message is readable rather than a wall of names
        fams = {}
        for k in dark:
            fams.setdefault(k.split("_")[0], []).append(k)
        for fam, ks in sorted(fams.items()):
            print(f"      {fam+'_*':<18} {', '.join(ks)}")
        print("    Either the behaviour did not occur, or the counter's gate was off.  Check the `#`")
        print("    gate line above and the counter's arming condition before citing a zero.")

    print()
    print("CONCENTRATION -- how much of all search work lives in how few decisions")
    for frac in (0.01, 0.05, 0.10, 0.25, 0.50):
        k = max(1, int(len(clean) * frac))
        share = sum(r["units"] for r in clean[:k]) / tot
        print(f"  slowest {frac*100:>5.1f}% of decisions ({k:>5} of {len(clean):>5}) = "
              f"{share*100:>5.1f}% of all units")
    print(f"  median decision {pct([r['units'] for r in clean], 50):>10,} units   "
          f"max {clean[0]['units']:>10,} units   "
          f"max/median = {ratio(clean[0]['units'], max(1, pct([r['units'] for r in clean], 50))):.0f}x")

    print()
    print("WHERE THE UNITS GO -- per-site share of total, whole run vs the slowest decile")
    k = max(1, len(clean) // 10)
    tail = clean[:k]
    tail_tot = sum(r["units"] for r in tail)
    print(f"  {'site':>16} {'all':>8} {'slowest 10%':>13}  {'shift':>7}")
    for s in sorted(usites, key=lambda s: -sum(r[s] for r in clean)):
        a = ratio(sum(r[s] for r in clean), tot) * 100
        b = ratio(sum(r[s] for r in tail), tail_tot) * 100
        if a < 0.05 and b < 0.05:
            continue
        print(f"  {s[len(USITE_PREFIX):]:>16} {a:>7.1f}% {b:>12.1f}%  {b-a:>+6.1f}pp")

    print()
    print("DOES EFFICIENCY DECAY ON THE TAIL?  median decision vs slowest decile.")
    print("  A ratio near 1.0 means the machinery behaves the same on a hard turn as on an easy one.")
    print("  A memo rate that FALLS and a pay/unit that RISES is the signature of a tail the caching")
    print("  does not cover -- which is the question this whole tool was built to answer.")
    names = {
        "memo_rate":     ("continuation-memo hit rate", "higher is better"),
        "pay_per_unit":  ("mana payment solves / unit", "lower is better"),
        "drop_share":    ("silently-dropped activations", "lower is better"),
        "units_per_cand":("units / candidate scored",   "lower is better"),
        "condemn_rate":  ("condemnation decline rate",  "diagnostic only"),
    }
    print(f"  {'metric':>30} {'median':>10} {'slowest 10%':>13} {'':>3} {'note':<22}")
    for key, (label, note) in names.items():
        med = pct([derive(r)[key] for r in clean if derive(r)[key] == derive(r)[key]], 50)
        tl = [derive(r)[key] for r in tail if derive(r)[key] == derive(r)[key]]
        tv = sum(tl) / len(tl) if tl else float("nan")
        print(f"  {label:>30} {med:>10.3f} {tv:>13.3f} {'':>3} {note:<22}")

    print()
    print("BUDGET DISCIPLINE -- how often a decision exceeds the budget it was given")
    print("  The proportional overrun guard caps a decision at kOverrunBudgetMult (25) x its budget")
    print("  and ROLLS THE PASS BACK, so everything above ~25x is truncation, not slowness.")
    for thr in (1, 2, 5, 10, 20, 25):
        k2 = sum(1 for r in clean if r["units"] > thr * r["budget"])
        print(f"  > {thr:>2}x budget: {k2:>6} of {len(clean):>6} decisions ({k2/len(clean)*100:>5.2f}%)")

    print()
    print("RE-WORK -- units spent and then superseded or discarded (the 'necessary work' question)")
    tot_w = sum(r.get("idwaste_units", 0) for r in clean)
    tot_warm = sum(r.get("lad_warm_units", 0) for r in clean)
    tot_fill = sum(r.get("fillin_units", 0) for r in clean)
    print(f"  overrun-DISCARDED (rolled back)   {tot_w:>14,}  {ratio(tot_w,tot)*100:>6.2f}% of all units")
    print(f"  ladder WARM-UP (superseded)       {tot_warm:>14,}  {ratio(tot_warm,tot)*100:>6.2f}%")
    print(f"  order-free FILL-IN re-search      {tot_fill:>14,}  {ratio(tot_fill,tot)*100:>6.2f}%")
    print(f"  ---- total re-work                {tot_w+tot_warm+tot_fill:>14,}  "
          f"{ratio(tot_w+tot_warm+tot_fill,tot)*100:>6.2f}%")
    tw = sum(r.get("idwaste_units", 0) for r in tail)
    print(f"  the slowest 10% of decisions hold {ratio(tw,tot_w)*100:>5.1f}% of ALL discarded units")
    resc = sum(r.get("idwaste_rescuable", 0) for r in clean)
    psw = sum(r.get("idwaste_passes", 0) for r in clean)
    print(f"  aborted passes={psw:,}  of which the discard threw away a PROVEN better win: {resc:,}"
          + ("  <-- lossy truncation, not just slow" if resc else ""))

    print()
    print("CONDEMNATION -- consultations vs declines, over the whole census")
    ds = sum(r.get("dedup_seen", 0) for r in clean)
    dd = sum(r.get("dedup_dup", 0) for r in clean)
    if ds:
        print()
        print("DEDUPLICATION -- post-apply state a sibling already reached (MTG_DEDUP_CENSUS)")
        print(f"  consultations {ds:>14,}   duplicates {dd:>12,}   dup_rate {dd/ds*100:5.1f}%")
        print(f"  of those duplicates, recognisable from the PLAN alone: "
              f"{sum(r.get('dedup_copy_perm',0) for r in clean):,}"
              f"   plan-test FALSE positives: {sum(r.get('dedup_copy_false',0) for r in clean):,}")
        print("  NOTE the denominator is dedup CONSULTATIONS, not cand_scored -- the dedup is")
        print("  consulted at two specific sites. Dividing by cand_scored understates it ~12x.")
        print(f"  casts lost to a deduped sibling {sum(r.get('dedup_drops',0) for r in clean):,}"
              f"   activations stranded {sum(r.get('dedup_strand',0) for r in clean):,}")

    cs = sum(r["bp_condemn_seen"] for r in clean)
    cd = sum(r["bp_condemn_drops"] for r in clean)
    ca = sum(r["condemn_act_drops"] for r in clean)
    print(f"  bp_condemn  seen={cs:,}  drops={cd:,}  ({ratio(cd,cs)*100:.2f}% of consultations)")
    print(f"  activation condemnation drops={ca:,}"
          + ("   (MTG_BP_CONDEMN_ACTIVATION is OFF on this run -- 0 is the flag, not a finding)"
             if ca == 0 else ""))
    turns_asked = sum(1 for r in clean if r["bp_condemn_seen"] > 0)
    turns_fired = sum(1 for r in clean if r["bp_condemn_drops"] > 0)
    print(f"  decisions that ASKED={turns_asked}/{len(clean)}   that DECLINED anything={turns_fired}")


def cmd_explain(args):
    rows, gate = load(args.file)
    usites, clean, _ = check_and_split(rows, args.file)
    clean.sort(key=lambda r: -r["units"])
    if args.n >= len(clean):
        sys.exit(f"only {len(clean)} clean rows")
    r = clean[args.n]
    d = derive(r)
    print(f"# {gate}")
    print(f"\nDECISION #{args.n}: seed {r['seed']}, turn {r['turn']}, "
          f"{'pre-combat' if r['pre'] else 'second'} main, "
          f"host={'SolveWithLookahead' if r['root']==1 else 'FullSearchLineHybrid'}, "
          f"depth {r['depth']}, budget {r['budget']:,} units")
    print(f"  cost: {r['units']:,} units ({ratio(r['units'], r['budget']):.1f}x its budget), "
          f"{r['wall_us']/1000:.1f} ms wall")
    print(f"\n  REPRO: ./build/Release/mtg <deck> --profile <prof> --games 1 "
          f"--seed {r['seed']} --depth {r['depth']} --threads 1")
    print(f"         (the decision is turn {r['turn']}'s "
          f"{'pre-combat' if r['pre'] else 'second'} main phase)")
    print("\n  UNITS BY SITE")
    for s in sorted(usites, key=lambda s: -r[s]):
        if r[s]:
            print(f"    {s[len(USITE_PREFIX):]:>16} {r[s]:>10,}  {ratio(r[s],r['units'])*100:>5.1f}%")
    print("\n  EVERY NON-ZERO COUNTER")
    skip = set(usites) | {"seed", "turn", "pre", "root", "depth", "budget", "units",
                          "wall_us", "contended"}
    for k, v in r.items():
        if k not in skip and v:
            print(f"    {k:>20} {v:>12,}")
    print("\n  DERIVED")
    print(f"    memo hit rate        {d['memo_rate']*100:>6.1f}%")
    print(f"    pay solves / unit    {d['pay_per_unit']:>6.1f}")
    print(f"    dropped activations  {d['drop_share']*100:>6.1f}%")
    print(f"    units / candidate    {d['units_per_cand']:>6.2f}")
    print(f"    condemn decline rate {d['condemn_rate']*100:>6.2f}%  "
          f"({r['bp_condemn_drops']:,} of {r['bp_condemn_seen']:,} consultations)")



def walk_funnel(r):
    """The enumeration walk's funnel for one decision, as (stage, survivors, rejected_here).

    CUMULATIVE pass counts, so each stage's rejections are the drop from the previous stage.  The
    ORDER of the stages is the whole point: a subset rejected at `flat mana` cost a cheap arithmetic
    test, while one rejected at `SubsetPayable` or `colour feas` has already run a real payment solve
    that is then thrown away.  Late rejection is therefore not the same finding as early rejection
    even at identical counts, and the `wasted pay solves` line below is the number that matters.
    """
    st = [("entered",      r.get("walk_enter", 0)),
          ("subset rules",  r.get("walk_pass_rules", 0)),
          ("flat mana",     r.get("walk_pass_mana", 0)),
          ("SubsetPayable", r.get("walk_pass_payable", 0)),
          ("colour feas",   r.get("walk_pass_color", 0)),
          ("SCORED",        r.get("walk_scored", 0))]
    out = []
    for i, (name, surv) in enumerate(st):
        rej = (st[i - 1][1] - surv) if i else 0
        out.append((name, surv, rej))
    return out


def check_walk_crosscheck(rows, path):
    """walk_enter (enumstats) and sub_entered (shapestats) count the SAME subset visits through two
    independently-gated instruments.  If they disagree, one gate failed to arm and every branching
    figure below is being read off a half-armed funnel -- which is exactly the class of defect that
    made an earlier census report a 5.2% dedup rate against a documented 64%."""
    bad = [r for r in rows if r.get("walk_enter", 0) != r.get("sub_entered", 0)]
    if bad:
        r = bad[0]
        print(f"# *** WARNING: walk_enter != sub_entered on {len(bad)}/{len(rows)} rows "
              f"(first: seed {r['seed']} turn {r['turn']}, "
              f"{r.get('walk_enter',0):,} vs {r.get('sub_entered',0):,}).\n"
              f"#     The two walk instruments disagree, so the funnel is half-armed. Do not trust "
              f"the branching block.", file=sys.stderr)
    return not bad


def cmd_heavyturn(args):
    """THE HEAVIEST TURNS -- one row per (seed, turn), not per decision root.

    This exists because the per-decision ranking MISSES the actual worst cases.  One real turn can
    contain many decision roots: snow seed 910716 turn 7 has FIFTEEN, alternating
    FullSearchLineHybrid and SolveWithLookahead, five of them above 98k units and several pinned at
    the 25x overrun ceiling -- about 1.08 M units against a 9,000-unit budget on a single turn.
    Ranked by decision it shows up as five separate rows each "only" 25x over; ranked by turn it is
    one turn at 120x.  The user asked for the heaviest TURNS, and this is the question they asked.

    READ THE `roots` COMMAND BEFORE DRAWING A SEARCH CONCLUSION FROM THIS TABLE.  A turn's root
    count is NOT a count of decisions in the game being played: on Snow, 59.6% of all charged units
    come from TRIAL GAMES played to pick a mulligan bottoming (one complete game per legal removal
    subset, up to 39 of them), and every one of those carries the real game's seed and turn number.
    So "turn 7 has fifteen roots" is really "turn 7 was played seven times, twice each".  The cost
    is real either way -- that work is genuinely spent on that turn -- but the CAUSE is the mulligan
    decision, not a search going 120x on one board.  `roots` splits it with the `probe` column.
    """
    rows, gate = load(args.file)
    usites, clean, dropped = check_and_split(rows, args.file)
    by = {}
    for r in clean:
        by.setdefault((r["seed"], r["turn"]), []).append(r)
    agg = []
    for (seed, turn), g in by.items():
        u = sum(r["units"] for r in g)
        bud = max(r["budget"] for r in g)
        agg.append(dict(
            seed=seed, turn=turn, roots=len(g), units=u, budget=bud,
            xbud=ratio(u, bud), ms=sum(r["wall_us"] for r in g) / 1000.0,
            pay=sum(r["pay_calls"] for r in g),
            cands=sum(r["cand_scored"] for r in g),
            enum_enter=sum(r.get("enum_enter", 0) for r in g),
            walk_enter=sum(r.get("walk_enter", 0) for r in g),
            odo=sum(r.get("space_odo", 0) for r in g),
            plans=sum(r.get("space_plans", 0) for r in g),
            eh=sum(r["enum_hits"] for r in g), em=sum(r["enum_misses"] for r in g),
            discarded=sum(r.get("idwaste_units", 0) for r in g),
            trunc=sum(1 for r in g if r["units"] >= 24.9 * r["budget"]),
            drops=sum(r["drop_tapped"] + r["drop_unpaid"] for r in g),
            fired=sum(r["act_fired"] for r in g)))
    agg.sort(key=lambda a: -a["units"])
    tot = sum(a["units"] for a in agg)
    # An ABSENT column must not render as 0. `enum_*` postdates the first census build, so a file
    # written by that build would otherwise print walkA=0 on every row and invite the reading "the
    # EnumeratePlans walk does nothing", which is the opposite of the truth.
    have_a = "enum_enter" in clean[0]
    have_b = "walk_enter" in clean[0]
    print(f"# {gate}")
    if not have_a:
        print("# NOTE: enum_* (walk A) columns are NOT in this file -- walkA shows 'n/a', not 0.")
    print(f"# {len(clean)} decision roots collapsed into {len(agg)} real turns "
          f"({ratio(len(clean),len(agg)):.1f} roots per turn on average)")
    print(f"\nTHE {args.top} HEAVIEST TURNS  (ranked by total units across every decision root on "
          f"that turn)")
    hdr = (f"{'seed':>8} {'t':>3} {'roots':>5} {'units':>11} {'xbud':>6} {'trunc':>5} "
           f"{'discarded':>11} {'ms':>9} {'pay solves':>12} {'cands':>9} {'walkA':>12} "
           f"{'walkB':>11} {'memo%':>6} {'drop%':>6}")
    print(hdr); print("-" * len(hdr))
    for a in agg[:args.top]:
        col_a = f"{a['enum_enter']:,}" if have_a else "n/a"
        col_b = f"{a['walk_enter']:,}" if have_b else "n/a"
        print(f"{a['seed']:>8} {a['turn']:>3} {a['roots']:>5} {a['units']:>11,} "
              f"{a['xbud']:>5.0f}x {a['trunc']:>5} {a['discarded']:>11,} {a['ms']:>9,.0f} "
              f"{a['pay']:>12,} {a['cands']:>9,} {col_a:>12} {col_b:>11} "
              f"{fmt_pct(ratio(a['eh'],a['eh']+a['em'])):>6} "
              f"{fmt_pct(ratio(a['drops'],a['drops']+a['fired'])):>6}")
    print(f"\ntop {args.top} turns = {sum(a['units'] for a in agg[:args.top])/tot*100:.1f}% of all "
          f"units;  `trunc` = roots pinned at the 25x overrun ceiling (work done then rolled back)")
    print("`walkA` = EnumeratePlans subset visits, `walkB` = SolveUncached subset visits -- "
          "separate walks, never summed.")
    nm = sum(1 for a in agg if a["roots"] > 1)
    print(f"\n{nm} of {len(agg)} turns ({nm/len(agg)*100:.1f}%) re-enter the solver more than once; "
          f"max {max(a['roots'] for a in agg)} roots on one turn.")
    if "probe" in clean[0]:
        pu = sum(r["units"] for r in clean if r["probe"])
        tu = sum(r["units"] for r in clean)
        print(f"*** {ratio(pu,tu)*100:.1f}% of these units are TRIAL GAMES played to decide a "
              f"mulligan bottoming, not decisions in the game being played.")
        print("    Run `roots` before attributing any of this table to search shape.")


def cmd_heavy(args):
    """THE HEAVIEST INDIVIDUAL DECISIONS, each with its own branching / cache / cost breakdown.

    USER 2026-09-30: *"I'm looking for the heaviest turns in a set of games and a breakdown on the
    branching (what are we trimming, how the cache interacts and such) even better if we can also
    include the cost of each so that we can diagnose why it is slow"*, and *"I don't want
    summaries"*.

    So this prints per-DECISION detail, not an aggregate: one block per heavy turn carrying the
    actual counts.  `turns` gives the per-turn curve and `summary` the concentration; neither can
    show why one specific turn was slow, which is what this is for.
    """
    rows, gate = load(args.file)
    usites, clean, dropped = check_and_split(rows, args.file)
    check_walk_crosscheck(clean, args.file)
    clean.sort(key=lambda r: -r["units"])
    tot = sum(r["units"] for r in clean)
    print(f"# {gate}")
    print(f"# {len(rows)} decisions, {dropped} contended (excluded). "
          f"Ranked by `units` (deterministic); wall_us is INSTRUMENTED -- see the gate line.")
    print(f"\n{'='*100}")
    print(f"THE {args.top} HEAVIEST DECISIONS of {len(clean)}  "
          f"(these {args.top} = {sum(r['units'] for r in clean[:args.top])/tot*100:.1f}% of all units)")
    print(f"{'='*100}")
    for i, r in enumerate(clean[:args.top]):
        d = derive(r)
        site, share = top_site(r, usites)
        ent = r.get("walk_enter", 0)
        print(f"\n--- #{i}  seed {r['seed']}  TURN {r['turn']}  "
              f"{'pre-combat' if r['pre'] else 'second'} main  "
              f"(d{r['depth']}, budget {r['budget']:,}u) "
              f"{'-'*12}")
        print(f"  COST      {r['units']:>12,} units = {ratio(r['units'],r['budget']):>5.1f}x budget"
              f"   {r['wall_us']/1000:>9.1f} ms   top site: {site} ({share*100:.0f}%)")
        print(f"            {r['pay_calls']:>12,} mana-payment solves "
              f"({d['pay_per_unit']:.1f} per charged unit)")
        print(f"            {r['rollout_steps']:>12,} rollout steps over "
              f"{r['rollout_calls']:,} rollouts")
        # ---- BRANCHING ----
        # TWO SEPARATE WALKS, never summed: EnumeratePlans (the main-phase decision list) and
        # SolveUncached (the rollout leaf). A decision can put all its branching in either, and the
        # heaviest Snow decisions put it entirely in the first while the second reads exactly 0.
        print(f"  BRANCHING odometer space {r.get('space_odo',0):>14,}  "
              f"-> plans emitted {r.get('space_plans',0):>12,}  "
              f"-> scored by search {r['cand_scored']:>10,}")
        een = r.get("enum_enter", 0)
        if een:
            est = [("entered", een), ("saturation", r.get("enum_pass_sat", 0)),
                   ("subset rules", r.get("enum_pass_rules", 0)),
                   ("EMITTED", r.get("enum_emitted", 0))]
            print(f"    walk A -- EnumeratePlans (builds the decision list): {een:,} subset visits "
                  f"over {r.get('walk_calls',0):,} enumerations")
            print(f"    {'stage':>14} {'survivors':>13} {'rejected here':>14} {'%of entered':>12}")
            for i2, (nm, surv) in enumerate(est):
                rej = (est[i2 - 1][1] - surv) if i2 else 0
                print(f"    {nm:>14} {surv:>13,} {rej:>14,} "
                      f"{('' if rej == 0 else f'{rej/een*100:>11.1f}%'):>12}")
        elif r.get("walk_calls", 0):
            print(f"    walk A -- EnumeratePlans: {r['walk_calls']:,} enumerations "
                  f"[VISIT FUNNEL NOT IN THIS FILE -- rebuild for enum_* columns]")
        if ent:
            print(f"    walk B -- SolveUncached (the rollout leaf): {ent:,} subset visits")
            print(f"    {'stage':>14} {'survivors':>13} {'rejected here':>14} {'%of entered':>12}")
            for name, surv, rej in walk_funnel(r):
                print(f"    {name:>14} {surv:>13,} {rej:>14,} "
                      f"{('' if rej == 0 else f'{rej/ent*100:>11.1f}%'):>12}")
            f = {n: (s, j) for n, s, j in walk_funnel(r)}
            wasted = f["SubsetPayable"][1] + f["colour feas"][1]
            print(f"    WASTED PAY SOLVES: {wasted:,} subsets "
                  f"({ratio(wasted,ent)*100:.1f}% of walk-B visits) were rejected AFTER a payment "
                  f"solve -- flat-mana rejects are cheap, these are not")
        elif r.get("cand_scored", 0):
            print(f"    walk B -- SolveUncached: ZERO subset visits "
                  f"(this decision's branching is entirely in walk A)")
        if r.get("walk_resc_call", 0):
            print(f"    filter RESCUE (copies the board per call): {r['walk_resc_call']:,} calls, "
                  f"{r.get('walk_resc_ok',0):,} rescued "
                  f"({ratio(r.get('walk_resc_ok',0), r['walk_resc_call'])*100:.1f}%)")
        for nm, key in (("wasteSacMana", "sub_rej_sacmana"), ("overFodder", "sub_rej_fodder")):
            if r.get(key, 0):
                print(f"    shared-resource reject {nm}: {r[key]:,} "
                      f"({ratio(r[key],ent)*100:.1f}% of visits)")
        # The one-use-per-source predicate, BY CLAUSE. Its function name (SubsetHasDuplicateSacSource)
        # is about sacrifice, but eight of its nine clauses are not, so an unattributed total here is
        # not actionable -- name the clause or say it is unattributed.
        cl = [(k[len("dupc_"):], v) for k, v in r.items() if k.startswith("dupc_") and v]
        if r.get("sub_rej_dupsrc", 0):
            # DENOMINATOR: the dupc_* clause counters span BOTH subset walks (site 0 and site 1)
            # while sub_rej_dupsrc is site 0 alone, so the clause share must divide by the sum or it
            # prints >100% -- which the first run did (102.1%), and which is the same class of error
            # as the mislabelled dedup column. Sites are summed here; the site-0 share of the walk
            # keeps its own line because that is the one `entered` covers.
            den = r["sub_rej_dupsrc"] + r.get("sub_rej_dupsrc_s1", 0)
            print(f"    one-use-per-source reject: {r['sub_rej_dupsrc']:,} at this walk "
                  f"({ratio(r['sub_rej_dupsrc'],ent)*100:.1f}% of its visits)"
                  + (f" + {r['sub_rej_dupsrc_s1']:,} at the second walk"
                     if r.get("sub_rej_dupsrc_s1", 0) else "")
                  + ("" if cl else "  [CLAUSE UNATTRIBUTED -- rebuild with the dupc_* columns]"))
            for nm, v in sorted(cl, key=lambda kv: -kv[1]):
                print(f"        clause {nm:>18} {v:>13,}  "
                      f"{ratio(v, den)*100:>5.1f}% of both walks' rejects")
        # Only when the columns are actually PRESENT: printing .get(...,0) defaults here produced
        # "situation: 0.00 option groups, board 0.0 avg" for every decision in a file that simply
        # predated these columns -- an absent column rendered as a measurement, which is the exact
        # failure this tool's dark-column check exists to prevent.
        if r.get("walk_calls", 0) and "walk_groups" in r:
            print(f"    situation: {ratio(r['walk_groups'], r['walk_calls']):.2f} option "
                  f"groups, {ratio(r.get('walk_ind',0), r['walk_calls']):.2f} independent, "
                  f"board {ratio(r.get('walk_board',0), r['walk_calls']):.1f} avg")
        # ---- CACHE ----
        eh, em = r["enum_hits"], r["enum_misses"]
        print(f"  CACHE     continuation memo {eh:,} hits / {em:,} misses = "
              f"{fmt_pct(ratio(eh,eh+em))} hit rate"
              + (f"   (nested {r['enum_nested_hits']:,}/{r['enum_nested_misses']:,})"
                 if r.get("enum_nested_misses", 0) or r.get("enum_nested_hits", 0) else ""))
        if r.get("space_dedup", 0):
            print(f"            walk dedup removed {r['space_dedup']:,} of "
                  f"{r.get('space_plans',0)+r['space_dedup']:,} emitted plans "
                  f"({ratio(r['space_dedup'], r.get('space_plans',0)+r['space_dedup'])*100:.1f}%)")
        if r.get("dedup_seen", 0):
            print(f"            candidate dedup {r['dedup_dup']:,} dups of {r['dedup_seen']:,} "
                  f"consultations ({ratio(r['dedup_dup'],r['dedup_seen'])*100:.1f}%)")
        # ---- TRIMMING at the search layer, and what it declined ----
        if r.get("bp_condemn_seen", 0):
            print(f"  TRIMMING  condemnation consulted {r['bp_condemn_seen']:,} times, dropped "
                  f"{r['bp_condemn_drops']:,} ({d['condemn_rate']*100:.2f}%)")
        if r.get("newonly_seen", 0):
            print(f"            bp_newonly dropped {r['newonly_dropped']:,} of "
                  f"{r['newonly_seen']:,} candidates "
                  f"({ratio(r['newonly_dropped'],r['newonly_seen'])*100:.1f}%)")
        drops = r["drop_tapped"] + r["drop_unpaid"]
        if drops:
            print(f"            SILENT DROPS {drops:,} of {r['act_fired']+drops:,} activations "
                  f"({fmt_pct(d['drop_share'])}): {r['drop_tapped']:,} tapped, "
                  f"{r['drop_unpaid']:,} unpaid")
        # ---- RE-WORK ----
        rw = r.get("idwaste_units", 0) + r.get("lad_warm_units", 0) + r.get("fillin_units", 0)
        if rw:
            print(f"  RE-WORK   {rw:,} units ({rw/r['units']*100:.1f}%) spent then thrown away: "
                  f"{r.get('idwaste_units',0):,} discarded by the overrun guard, "
                  f"{r.get('lad_warm_units',0):,} superseded ladder warm-up")
    print()


def cmd_turns(args):
    """Per-TURN breakdown: the game's cost curve, not a ranking of individual decisions.

    `rank` answers "which decisions cost most"; this answers "where in a GAME does the cost
    arrive".  They are different questions and the second is the one that says whether a deck is
    expensive early (deployment branching), late (a wide board of activations), or only in a tail
    of runaway turns -- and a lever aimed at the wrong end of that curve is inert.
    """
    rows, gate = load(args.file)
    usites, clean, dropped = check_and_split(rows, args.file)
    print(f"# {gate}")
    print(f"# {len(rows)} decisions, {dropped} contended (excluded)")
    by = {}
    for r in clean:
        by.setdefault(r["turn"], []).append(r)
    tot_units = sum(r["units"] for r in clean)
    print()
    print("PER-TURN BREAKDOWN  (pre = pre-combat main decisions, 2nd = second main)")
    hdr = (f"{'turn':>4} {'decisions':>9} {'pre/2nd':>9} {'units':>13} {'%tot':>6} "
           f"{'u/decision':>11} {'cands':>10} {'memo%':>6} {'pay/u':>7} {'drop%':>6} "
           f"{'>1xbud':>7} {'worst ms':>9}")
    print(hdr)
    print("-" * len(hdr))
    for t in sorted(by):
        g = by[t]
        u = sum(r["units"] for r in g)
        pre = sum(1 for r in g if r["pre"])
        cs = sum(r["cand_scored"] for r in g)
        eh = sum(r["enum_hits"] for r in g); em = sum(r["enum_misses"] for r in g)
        pc = sum(r["pay_calls"] for r in g)
        af = sum(r["act_fired"] for r in g)
        dr = sum(r["drop_tapped"] + r["drop_unpaid"] for r in g)
        over = sum(1 for r in g if r["units"] > r["budget"])
        print(f"{t:>4} {len(g):>9} {pre:>4}/{len(g)-pre:<4} {u:>13,} {u/tot_units*100:>5.1f}% "
              f"{u/len(g):>11,.0f} {cs/len(g):>10,.0f} {ratio(eh,eh+em)*100:>5.1f}% "
              f"{ratio(pc,u):>7.2f} {fmt_pct(ratio(dr,af+dr)):>6} "
              f"{over/len(g)*100:>6.0f}% {max(r['wall_us'] for r in g)/1000:>9.0f}")
    print()
    print("READING IT: `u/decision` is the cost curve. `pay/u` rising with the turn number means")
    print("the board is accumulating activation sources faster than the budget accounts for them.")
    print("`>1xbud` is the share of that turn's decisions that blew their budget -- the turn at")
    print("which the budget stops binding is where a cost lever has to act.")


SITE_NAMES = {1: "main", 2: "bp_resolve", 3: "m2_postdraw", 4: "pod_bp"}


def cmd_roots(args):
    """WHO ASKS FOR A ROOT, and how much of a turn's cost re-solves a state it already solved.

    `heavyturn` established that one turn holds up to 40 decision roots, each handed a FULL fresh
    `DecisionBudget()`, so a turn can cost 120x its per-decision budget while every individual root
    looks merely 25x over.  That raises two questions the host column (`root` = 1 SolveWithLookahead
    / 2 FullSearchLineHybrid) structurally cannot answer:

      1. WHO ASKED?  A root is either the executor's own decision for the phase, or one of the
         executor's searched RE-SOLVES -- the unsearched-breakpoint continuation (AIEngine.cpp,
         "a COMMITTED line's unsearched breakpoint continuation is a REAL DECISION"), the mode-2
         post-draw re-solve, or the pod trailing-pass twin.  The `site` column names it.
      2. IS IT THE SAME STATE?  Every root carries `skey`, the engine's own full-state dedup key
         (BuildDedupKey).  Two roots on one turn with the same (skey, pre, depth) are solving the
         SAME position from scratch -- the second one's units are not a deeper search or a wider
         one, they are the first one's search run again.  That is waste in the strict sense: neither
         quality nor performance, so removing it costs nothing and frees the budget to be SPENT.

    A duplicate is counted against the FIRST occurrence on its turn, which is the conservative
    direction: it never claims the original as removable.
    """
    rows, gate = load(args.file)
    usites, clean, dropped = check_and_split(rows, args.file)
    if "skey" not in clean[0]:
        sys.exit("this census has no `skey`/`site` columns -- it predates the root accounting.\n"
                 "    Re-run the census on a current binary; without the state key there is no way\n"
                 "    to tell a re-solve of the same position from a genuinely new decision.")
    print(f"# {gate}")
    print(f"# {len(rows)} roots, {dropped} contended (excluded)")
    tot_u = sum(r["units"] for r in clean)

    # ---- THE FIRST QUESTION: is this root even for the game being played? ----------------------
    # Asked before any per-site or per-turn breakdown, because it reframes all of them.  A trial
    # game (`probe=1`) is a COMPLETE game played out to label one decision -- which bottoming
    # subset to take, whether to keep.  Its roots carry the same seed and turn numbers as real
    # play, so every earlier reading of this census that said "turn 2 has 34 decision roots" was
    # really "turn 2 was played 17 times, twice each".
    if "probe" in clean[0]:
        pu = sum(r["units"] for r in clean if r["probe"])
        pn = sum(1 for r in clean if r["probe"])
        print(f"\nTRIAL-GAME WORK (probe=1): {pn:,} of {len(clean):,} roots "
              f"({ratio(pn,len(clean))*100:.1f}%), {pu:,} of {tot_u:,} units "
              f"({ratio(pu,tot_u)*100:.1f}%)")
        print("  These roots decide which cards to BOTTOM / whether to KEEP, by playing the whole")
        print("  game out once per candidate at the deck's real depth and budget.  They are not")
        print("  decisions in the game being played, and they carry its seed and turn numbers --")
        print("  so a per-turn root count that does not split on this column counts one turn's")
        print("  single decision once per trial game.")
        # Trial games per phase, from the per-(seed, turn, phase) root multiplicity on probe rows.
        mult = {}
        for r in clean:
            if r["probe"]:
                mult[(r["seed"], r["turn"], r["pre"])] = \
                    mult.get((r["seed"], r["turn"], r["pre"]), 0) + 1
        if mult:
            vals = sorted(mult.values())
            print(f"  trial games per phase: median {pct(vals,50):,}, p90 {pct(vals,90):,}, "
                  f"max {max(vals):,}  (x2 roots each where the hybrid falls through)")

    # ---- per-site accounting -------------------------------------------------------------------
    by_turn = {}
    for r in clean:
        by_turn.setdefault((r["seed"], r["turn"]), []).append(r)
    # TWO KINDS OF REPEAT, and they are NOT the same finding.
    #   _dup   -- same state, SAME host: the identical search run twice.  Pure waste.
    #   _xdup  -- same state, DIFFERENT host: FullSearchLineHybrid searched this position, committed
    #             nothing, and the executor then paid a second FULL budget for the
    #             SolveWithLookahead fallback on the identical state (AIEngine.cpp's "no committed
    #             play for this phase" branch).  The second search is a different algorithm, so the
    #             units are not literally recomputed -- but one phase was decided at two budgets.
    #             Reported separately because collapsing the two would overstate the removable part,
    #             which is the error the duplicate-APPLY table made on this deck once already.
    # `probe` is part of the identity: a trial game's root and a real-play root can land on the same
    # position, and pairing them would claim the real decision as a duplicate of a hypothetical.
    for g in by_turn.values():
        seen = {}
        for r in g:
            ident = (r["skey"], r["pre"], r["depth"], r.get("probe", 0))
            prev_hosts = seen.setdefault(ident, set())
            r["_dup"] = 1 if r["root"] in prev_hosts else 0
            r["_xdup"] = 1 if (prev_hosts and not r["_dup"]) else 0
            prev_hosts.add(r["root"])

    sites = {}
    for r in clean:
        s = sites.setdefault((r["site"], r["root"], r.get("probe", 0)),
                             dict(n=0, u=0, dn=0, du=0, xn=0, xu=0, c=0))
        s["n"] += 1; s["u"] += r["units"]; s["c"] += r["cand_scored"]
        s["dn"] += r["_dup"];  s["du"] += r["_dup"] * r["units"]
        s["xn"] += r["_xdup"]; s["xu"] += r["_xdup"] * r["units"]
    print("\nWHO ASKED FOR THE ROOT  (host 1 = SolveWithLookahead, 2 = FullSearchLineHybrid)")
    hdr = (f"{'site':>12} {'host':>4} {'probe':>5} {'roots':>8} {'%roots':>7} {'units':>14} "
           f"{'%units':>7} {'u/root':>10} {'cands':>13} {'same-host dup':>14} {'cross-host':>12}")
    print(hdr); print("-" * len(hdr))
    for key in sorted(sites, key=lambda k: -sites[k]["u"]):
        sid, host, probe = key
        s = sites[key]
        print(f"{SITE_NAMES.get(sid, f'site{sid}'):>12} {host:>4} {probe:>5} {s['n']:>8,} "
              f"{s['n']/len(clean)*100:>6.1f}% {s['u']:>14,} {s['u']/tot_u*100:>6.1f}% "
              f"{s['u']/s['n']:>10,.0f} {s['c']:>13,} "
              f"{s['dn']:>6,}/{fmt_pct(ratio(s['du'],s['u'])):>7} "
              f"{s['xn']:>5,}/{fmt_pct(ratio(s['xu'],s['u'])):>6}")

    # ---- duplicate accounting ------------------------------------------------------------------
    dn = sum(r["_dup"] for r in clean)
    du = sum(r["_dup"] * r["units"] for r in clean)
    dc = sum(r["_dup"] * r["cand_scored"] for r in clean)
    xn = sum(r["_xdup"] for r in clean)
    xu = sum(r["_xdup"] * r["units"] for r in clean)
    tot_c = sum(r["cand_scored"] for r in clean)
    print(f"\nSAME STATE, SAME HOST -- the identical search run again: {dn:,} of {len(clean):,} roots "
          f"({ratio(dn,len(clean))*100:.1f}%)")
    print(f"  units      {du:,} of {tot_u:,} ({ratio(du,tot_u)*100:.1f}% of all charged work)")
    print(f"  candidates {dc:,} of {tot_c:,} ({ratio(dc,tot_c)*100:.1f}%)")
    print(f"\nSAME STATE, OTHER HOST -- one phase decided at two full budgets: {xn:,} roots "
          f"({ratio(xn,len(clean))*100:.1f}%), {xu:,} units ({ratio(xu,tot_u)*100:.1f}%)")
    print("  This is the hybrid's \"no committed play for this phase\" fallback: the commit-the-line")
    print("  search pays a whole budget to find nothing, then SolveWithLookahead pays another on the")
    print("  identical position.  NOT the same units twice -- a different algorithm -- so it is a")
    print("  double CHARGE rather than a recomputation, and it is the budget's problem, not a memo's.")

    # Are the duplicates doing the SAME work?  If a repeat's units differ from the original's, the
    # state key agrees but something outside it (a memo already warm, a budget partly spent) made
    # the second pass cheaper -- which bounds how much is really removable.  Identical units is the
    # strong signature: same position, same search, same cost, twice.
    same, diff = 0, 0
    for g in by_turn.values():
        first = {}
        for r in g:
            ident = (r["skey"], r["pre"], r["depth"], r["root"], r.get("probe", 0))
            if ident not in first:
                first[ident] = r
            elif r["units"] == first[ident]["units"]:
                same += 1
            else:
                diff += 1
    print(f"\n  of the same-host repeats, {same:,} cost EXACTLY the original's units and {diff:,} "
          f"differ ({fmt_pct(ratio(same, same+diff))} exact)")

    # ---- the turns where it concentrates -------------------------------------------------------
    agg = []
    for (seed, turn), g in by_turn.items():
        agg.append(dict(seed=seed, turn=turn, roots=len(g),
                        units=sum(r["units"] for r in g),
                        dup=sum(r["_dup"] + r["_xdup"] for r in g),
                        dup_u=sum((r["_dup"] + r["_xdup"]) * r["units"] for r in g),
                        bud=max(r["budget"] for r in g),
                        sites="/".join(f"{SITE_NAMES.get(k,k)}:{v}" for k, v in sorted(
                            {s: sum(1 for r in g if r["site"] == s) for s in {r["site"] for r in g}}
                            .items()))))
    agg.sort(key=lambda a: -a["dup_u"])
    print(f"\nTHE {args.top} TURNS WITH THE MOST REPEATED-POSITION WORK  (both kinds together)")
    hdr = (f"{'seed':>8} {'t':>3} {'roots':>5} {'dup':>4} {'units':>12} {'dup units':>12} "
           f"{'%dup':>6} {'xbud':>6}  sites")
    print(hdr); print("-" * len(hdr))
    for a in agg[:args.top]:
        if a["dup_u"] == 0:
            break
        print(f"{a['seed']:>8} {a['turn']:>3} {a['roots']:>5} {a['dup']:>4} {a['units']:>12,} "
              f"{a['dup_u']:>12,} {ratio(a['dup_u'],a['units'])*100:>5.1f}% "
              f"{ratio(a['units'],a['bud']):>5.0f}x  {a['sites']}")
    print("\nREADING IT: `dup units` is work that bought nothing -- the position was already solved")
    print("on this turn.  It is removable without any soundness argument about search quality,")
    print("which is what distinguishes it from every pruning lever: a prune trades quality for")
    print("cost, this trades nothing.  `sites` says which caller produced the turn's roots.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("rank", help="the slowest-N decisions")
    p.add_argument("file", nargs="+"); p.add_argument("--top", type=int, default=40)
    p.set_defaults(fn=cmd_rank)
    p = sub.add_parser("summary", help="concentration + tail-vs-median efficiency decay")
    p.add_argument("file", nargs="+")
    p.set_defaults(fn=cmd_summary)
    p = sub.add_parser("heavyturn", help="the heaviest TURNS (all decision roots on one turn summed)")
    p.add_argument("file", nargs="+"); p.add_argument("--top", type=int, default=25)
    p.set_defaults(fn=cmd_heavyturn)
    p = sub.add_parser("heavy", help="the heaviest decisions, each with its own branching/cache/cost")
    p.add_argument("file", nargs="+"); p.add_argument("--top", type=int, default=20)
    p.set_defaults(fn=cmd_heavy)
    p = sub.add_parser("turns", help="per-TURN breakdown: where in a game the cost arrives")
    p.add_argument("file", nargs="+")
    p.set_defaults(fn=cmd_turns)
    p = sub.add_parser("roots", help="who asks for each root, and how much re-solves the same state")
    p.add_argument("file", nargs="+"); p.add_argument("--top", type=int, default=25)
    p.set_defaults(fn=cmd_roots)
    p = sub.add_parser("explain", help="one decision in full, with its repro command")
    p.add_argument("file", nargs="+"); p.add_argument("-n", type=int, default=0, help="rank index (0 = slowest)")
    p.set_defaults(fn=cmd_explain)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
