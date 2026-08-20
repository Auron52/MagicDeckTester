#!/usr/bin/env python3
"""Regenerate the measured sections of the Mirrorwing card-tournament record.

    python3 scripts/tourney_ledger.py                 # refresh docs/design/mirrorwing-card-tournament-results.md
    python3 scripts/tourney_ledger.py --only runB     # refresh one section

The record is a mix of two things that must not be maintained the same way:

  * MEASUREMENTS -- every option tried, with its numbers. Auto-generated, always straight from the
    run's own stderr, never hand-transcribed. Transcription is where a number silently becomes
    wrong, and this project has already had one conclusion reversed because a figure was quoted
    from memory instead of recomputed.
  * REASONS -- why a card wins, what the case logs showed, what we got wrong. Prose, written by
    hand, and left completely alone by this script.

So the script rewrites ONLY the regions between `<!-- AUTO:name -->` and `<!-- /AUTO:name -->` and
preserves everything else byte-for-byte. Re-run it whenever a batch lands; append prose freely.
"""
import argparse, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import tourney_report as tr  # noqa: E402

DOC = os.path.join(ROOT, "docs", "design", "mirrorwing-card-tournament-results.md")

RUN_A = os.path.join(ROOT, "logs", "tourney", "run_alias", "tourney.err")
RUN_B = os.path.join(ROOT, "logs", "tourney", "run_B", "tourney.err")

TF = ["tf3lib0", "tf2lib1", "tf1lib2", "tf0lib3"]
TFN = {"tf3lib0": "3 Twinflame", "tf2lib1": "2 Twin / 1 Lib",
       "tf1lib2": "1 Twin / 2 Lib", "tf0lib3": "3 Libation"}

# Run A -- alias map A (Oracle -> Anger's cap-4 bucket, Draught -> Scale's cap-2 bucket)
A_COMBO = [("a4o0", "scale", "4 Anger + 2 Scale"), ("a4o0", "draught", "4 Anger + 2 Draught"),
           ("a2o2", "scale", "2 Anger/2 Oracle + 2 Scale"),
           ("a2o2", "draught", "2 Anger/2 Oracle + 2 Draught"),
           ("a0o4", "scale", "4 Oracle + 2 Scale"), ("a0o4", "draught", "4 Oracle + 2 Draught")]
# Run B -- alias map B (Draught -> Anger's cap-4 bucket, Oracle -> Scale's cap-2 bucket)
B_COMBO = [("a4s2", "4 Anger + 2 Scale"), ("a4o2", "4 Anger + 2 Oracle"),
           ("a2d2o2", "2 Anger + 2 Draught + 2 Oracle"), ("d4s2", "4 Draught + 2 Scale"),
           ("d4o2", "4 Draught + 2 Oracle")]

# Every combination measured in EITHER run, under one roof. The bridge arm is identical game-for-game
# across the two alias maps (section 2), which is what licenses ranking run-A arms against run-B arms
# on the same seeds -- so this is a single standings table, not two tables side by side.
ALL_COMBO = [("a4o0_scale", "4 Anger + 2 Scale"), ("a4o0_draught", "4 Anger + 2 Draught"),
             ("a4o2", "4 Anger + 2 Oracle"),
             ("a0o4_scale", "4 Oracle + 2 Scale"), ("a0o4_draught", "4 Oracle + 2 Draught"),
             ("d4s2", "4 Draught + 2 Scale"), ("d4o2", "4 Draught + 2 Oracle"),
             ("a2o2_scale", "2 Anger/2 Oracle + 2 Scale"),
             ("a2o2_draught", "2 Anger/2 Oracle + 2 Draught"),
             ("a2d2o2", "2 Anger/2 Draught/2 Oracle")]

TRICKS = ["tf3lib0", "tf2lib1", "tf1lib2", "tf0lib3"]
TRICK_LABEL = {"tf3lib0": "3 Twin", "tf2lib1": "2 Twin / 1 Lib",
               "tf1lib2": "1 Twin / 2 Lib", "tf0lib3": "3 Libation"}

HDR = ("| option | life | games | mean turn | vs baseline | t | better | worse | margin | s |\n"
       "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")


def row(label, life, s, mean):
    return (f"| {label} | {life} | {s['n']:,} | {mean:.4f} | {s['md']:+.4f} | {s['t']:+.1f} | "
            f"{s['better']:,} | {s['worse']:,} | **{s['margin']:+,}** | {s['sigma']:+.1f} |")


def combo_table(data, games, base_arms, combos, arm_of, lives=(20, 30)):
    out = [HDR]
    for life in lives:
        for key in combos:
            label = key[-1]
            aa = [f"{arm_of(base_arms, t)}@L{life}" for t in TF]
            bb = [f"{arm_of(key, t)}@L{life}" for t in TF]
            if any(x not in data for x in aa + bb):
                continue
            if aa == bb:
                m = sum(sum(data[a][0]) / games for a in aa) / len(aa)
                out.append(f"| **{label}** (baseline) | {life} | {games * len(aa):,} | {m:.4f} "
                           f"| -- | -- | -- | -- | -- | -- |")
                continue
            s = tr.contrast(data, aa, bb, games)
            out.append(row(label, life, s, s["mean_b"]))
    return "\n".join(out)


def ladder_table(data, games, arm_of, fixed, lives=(20, 30)):
    """Twinflame -> Libation dose response, pooled over the trick configurations."""
    out = ["| step | life | games | turns | t | margin | s |", "|---|---:|---:|---:|---:|---:|---:|"]
    for life in lives:
        for i in range(len(TF) - 1):
            for j in range(i + 1, len(TF)):
                aa = [f"{arm_of(k, TF[i])}@L{life}" for k in fixed]
                bb = [f"{arm_of(k, TF[j])}@L{life}" for k in fixed]
                if any(x not in data for x in aa + bb):
                    continue
                s = tr.contrast(data, aa, bb, games)
                out.append(f"| {TFN[TF[i]]} -> {TFN[TF[j]]} | {life} | {s['n']:,} | "
                           f"{s['md']:+.4f} | {s['t']:+.1f} | **{s['margin']:+,}** | "
                           f"{s['sigma']:+.1f} |")
    return "\n".join(out)


def bridge_table(dA, dB, games):
    """The unaliased baseline, measured under BOTH alias maps on the same seeds.

    It holds no aliased card, so the two runs should agree game-for-game. If they do not, the two
    runs cannot be chained through it and every cross-map statement in this document is void."""
    out = ["| arm | life | games | run A mean | run B mean | identical games |",
           "|---|---:|---:|---:|---:|:--:|"]
    for life in (20, 30):
        for a, b in (("tf3lib0_a4o0_scale", "tf3lib0_a4s2"),):
            ka, kb = f"{a}@L{life}", f"{b}@L{life}"
            if ka not in dA or kb not in dB:
                continue
            wa, wb = dA[ka][0], dB[kb][0]
            same = sum(1 for i in range(games) if wa[i] == wb[i])
            out.append(f"| {b} | {life} | {games:,} | {sum(wa)/games:.4f} | {sum(wb)/games:.4f} | "
                       f"{'YES' if same == games else f'NO ({same:,}/{games:,})'} |")
    return "\n".join(out)


def standings_table(D, games, lives=(20, 30)):
    """Every combination ranked, each measured AGAINST THE LEADER rather than against the baseline.

    Ranking on the mean alone would invite reading two arms as separated when the pair-difference
    says otherwise, so the leader contrast is carried on the same row: an arm is only eliminated
    when both its mean gap and its count margin put it behind, and neither is inside the
    apparatus band from section 3."""
    out = []
    for life in lives:
        ms = []
        for k, lab in ALL_COMBO:
            aa = [f"{t}_{k}@L{life}" for t in TF]
            if any(x not in D for x in aa):
                continue
            ms.append((sum(sum(D[x][0]) for x in aa) / (games * len(TF)), k, lab))
        if not ms:
            continue
        ms.sort()
        lead_k, lead_lab = ms[0][1], ms[0][2]
        out.append(f"\n**{life} life** — leader: **{lead_lab}**\n")
        out.append("| # | combination | mean turn | behind leader | t | margin | s |")
        out.append("|---:|---|---:|---:|---:|---:|---:|")
        for i, (m, k, lab) in enumerate(ms, 1):
            if k == lead_k:
                out.append(f"| {i} | **{lab}** | {m:.4f} | — | — | — | — |")
                continue
            s = tr.contrast(D, [f"{t}_{lead_k}@L{life}" for t in TF],
                            [f"{t}_{k}@L{life}" for t in TF], games)
            out.append(f"| {i} | {lab} | {m:.4f} | {s['md']:+.4f} | {s['t']:+.1f} | "
                       f"**{s['margin']:+,}** | {s['sigma']:+.1f} |")
    return "\n".join(out)


def scale_table(D, games, lives=(20, 30)):
    """Scale the Heights against its replacement, in every context it was measured in."""
    ctx = [("4 Anger", "a4o0_scale", "a4o0_draught", "2 Draught"),
           ("4 Anger", "a4s2", "a4o2", "2 Oracle"),
           ("2 Anger / 2 Oracle", "a2o2_scale", "a2o2_draught", "2 Draught"),
           ("4 Oracle", "a0o4_scale", "a0o4_draught", "2 Draught"),
           ("4 Draught", "d4s2", "d4o2", "2 Oracle")]
    out = ["| context | 2 Scale becomes | life | turns | t | margin | s | Scale |",
           "|---|---|---:|---:|---:|---:|---:|:--:|"]
    for life in lives:
        for name, ka, kb, repl in ctx:
            aa = [f"{t}_{ka}@L{life}" for t in TF]
            bb = [f"{t}_{kb}@L{life}" for t in TF]
            if any(x not in D for x in aa + bb):
                continue
            s = tr.contrast(D, aa, bb, games)
            # Scale only "wins" a context if BOTH numbers say so; one number alone settles nothing.
            win = "**wins**" if (s["md"] > 0 and s["margin"] < 0) else "loses"
            out.append(f"| {name} | {repl} | {life} | {s['md']:+.4f} | {s['t']:+.1f} | "
                       f"**{s['margin']:+,}** | {s['sigma']:+.1f} | {win} |")
    return "\n".join(out)


# ---------------------------------------------------------------------------------------------
# THE FINAL GRID. Every legal list, once the 4-copy limit and the user's floors (Entrance >= 2,
# Draught >= 2) are applied: 9 draw/pump shapes x 4 trick splits x 2 life totals, no holes.
#
# Arm keys differ per run because runs A and B were hand-built and runs D/E carry a map suffix
# (they measure the same decklist under more than one alias map). The mapping is spelled out here
# ONCE so nothing downstream has to know which run a cell came from -- the bridge arms are verified
# game-identical across maps, which is what licenses treating them as one table.
SHAPES = [
    ("4 O + 2 D + 2 E", {"tf3lib0": "a0o4_draught", "tf2lib1": "a0o4_draught",
                         "tf1lib2": "a0o4_draught", "tf0lib3": "a0o4_draught"}),
    ("3 O + 3 D + 2 E", {t: "o3d3e2_A" for t in TRICKS}),
    ("2 O + 4 D + 2 E", {t: "d4o2" for t in TRICKS}),
    ("3 O + 2 D + 3 E", {t: "o3d2e3_A" for t in TRICKS}),
    ("2 O + 3 D + 3 E", {t: "o2d3e3_B" for t in TRICKS}),
    ("1 O + 4 D + 3 E", {t: "o1d4e3_B" for t in TRICKS}),
    ("2 O + 2 D + 4 E", {t: "e4" for t in TRICKS}),
    ("1 O + 3 D + 4 E", {t: "o1d3e4_C" for t in TRICKS}),
    ("0 O + 4 D + 4 E", {t: "o0d4e4_B" for t in TRICKS}),
]


def cell(D, trick, pat, life, games):
    k = f"{trick}_{pat}@L{life}"
    return sum(D[k][0]) / games if k in D else None


def grid_table(D, games):
    """The whole space, one row per shape, one column per trick split, per life total."""
    out = []
    for life in (20, 30):
        best = min((c for _, m in SHAPES for t in TRICKS
                    for c in [cell(D, t, m[t], life, games)] if c is not None), default=None)
        out.append(f"\n**{life} life** — mean turn to kill, lower is better. "
                   f"Bold = best in the whole grid.\n")
        out.append("| shape | " + " | ".join(TRICK_LABEL[t] for t in TRICKS) + " | best |")
        out.append("|---|" + "---:|" * (len(TRICKS) + 1))
        for lab, m in SHAPES:
            cs = [cell(D, t, m[t], life, games) for t in TRICKS]
            txt = []
            for c in cs:
                if c is None:
                    txt.append("—")
                elif best is not None and abs(c - best) < 1e-9:
                    txt.append(f"**{c:.4f}**")
                else:
                    txt.append(f"{c:.4f}")
            ok = [c for c in cs if c is not None]
            out.append(f"| `{lab}` | " + " | ".join(txt) + " | "
                       + (f"{min(ok):.4f}" if ok else "—") + " |")
    return "\n".join(out)


def entrance_table(D, games):
    """Best list at each Entrance count -- the shape of the Entrance response."""
    out = ["| Entrance | best at 20 life | best at 30 life | best two-total sum |",
           "|---:|---|---|---|"]
    for e in (2, 3, 4):
        rows = [(lab, m) for lab, m in SHAPES if lab.endswith(f"{e} E")]
        picks = {}
        for life in (20, 30):
            cand = [(cell(D, t, m[t], life, games), lab, t)
                    for lab, m in rows for t in TRICKS if cell(D, t, m[t], life, games) is not None]
            picks[life] = min(cand) if cand else None
        sums = [(a + b, lab, t) for lab, m in rows for t in TRICKS
                for a in [cell(D, t, m[t], 20, games)] for b in [cell(D, t, m[t], 30, games)]
                if a is not None and b is not None]
        bs = min(sums) if sums else None
        def f(p, fmt="{:.4f}"):
            return "—" if p is None else f"{fmt.format(p[0])} `{p[1]}` {TRICK_LABEL[p[2]]}"
        out.append(f"| **{e}** | {f(picks[20])} | {f(picks[30])} | {f(bs)} |")
    return "\n".join(out)


def splice(text, name, body):
    a, b = f"<!-- AUTO:{name} -->", f"<!-- /AUTO:{name} -->"
    pat = re.compile(re.escape(a) + r".*?" + re.escape(b), re.S)
    if not pat.search(text):
        sys.exit(f"marker {a} not found in the document -- refusing to guess where it goes")
    return pat.sub(a + "\n" + body + "\n" + b, text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=10000)
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    if not os.path.exists(DOC):
        sys.exit(f"{DOC} does not exist -- write the prose scaffold first")
    text = open(DOC).read()

    def load(path, label):
        """An IN-FLIGHT run is absent, not a failure. tr.load refuses a partial batch on purpose --
        that guard is what stops a half-finished run being read as a result -- so here it just means
        'not ready yet' and the document keeps its previous content for that section."""
        if not os.path.exists(path):
            return {}
        try:
            return tr.load(path, a.games, 8)
        except SystemExit as e:
            print(f"  {label}: not ready ({str(e).split(':')[-1].strip()[:60]}...) -- section left "
                  f"as-is")
            return {}

    dA = load(RUN_A, "run A")
    dB = load(RUN_B, "run B")
    dC = load(os.path.join(ROOT, "logs", "tourney", "run_C", "tourney.err"), "run C")
    dD = load(os.path.join(ROOT, "logs", "tourney", "run_D", "tourney.err"), "run D")
    dE = load(os.path.join(ROOT, "logs", "tourney", "run_E", "tourney.err"), "run E")
    ALL = {}
    for d in (dA, dB, dC, dD, dE):
        ALL.update(d)

    if dA and a.only in ("", "runA"):
        text = splice(text, "runA", combo_table(
            dA, a.games, ("a4o0", "scale", ""), A_COMBO,
            lambda k, t: f"{t}_{k[0]}_{k[1]}"))
        text = splice(text, "runA_ladder", ladder_table(
            dA, a.games, lambda k, t: f"{t}_{k[0]}_{k[1]}",
            [(x, y) for x in ("a4o0", "a2o2", "a0o4") for y in ("scale", "draught")]))
    if dB and a.only in ("", "runB"):
        text = splice(text, "runB", combo_table(
            dB, a.games, ("a4s2", ""), B_COMBO, lambda k, t: f"{t}_{k[0]}"))
        text = splice(text, "runB_ladder", ladder_table(
            dB, a.games, lambda k, t: f"{t}_{k[0]}", [(c[0],) for c in B_COMBO]))
    if dA and dB and a.only in ("", "bridge"):
        text = splice(text, "bridge", bridge_table(dA, dB, a.games))
    if dA and dB and a.only in ("", "standings"):
        D = dict(dA)
        D.update(dB)
        text = splice(text, "standings", standings_table(D, a.games))
        text = splice(text, "scale", scale_table(D, a.games))

    if ALL and a.only in ("", "grid"):
        text = splice(text, "grid", grid_table(ALL, a.games))
        text = splice(text, "entrance", entrance_table(ALL, a.games))

    open(DOC, "w").write(text)
    print(f"refreshed {os.path.relpath(DOC, ROOT)}"
          f"  (run A: {'yes' if dA else 'absent'}, run B: {'yes' if dB else 'absent'})")


if __name__ == "__main__":
    main()
