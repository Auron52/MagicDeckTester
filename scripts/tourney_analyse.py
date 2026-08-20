#!/usr/bin/env python3
"""Turn a comparison's case logs into MECHANISM: where each side wins, and what made the loser
fall short.

    python3 scripts/tourney_analyse.py logs/tourney/cases/<tag>/cases.json

User spec (2026-08-19): "The AI shouldn't just look for problems in the analysis, but also report
where they are better and worse and why. Looking particularly about what caused the other
combination to fall short."

This does the mechanical part so the written analysis is grounded in counted facts rather than in
a few traces someone skimmed. It reads both arms' traces for every case and reports, split by
direction:

  * WHEN the two lines first diverge, and what each side did at that point
  * the BIGGEST ATTACK each side managed, the board it was made with, and the damage
  * for the loser: how much opponent life was LEFT -- the "fell short by N" distribution, which
    separates "never got there" from "missed lethal by 2"
  * whether the contested card was cast, on what turn, and for how much
  * the casts present on one side and absent on the other at the divergence turn

Every number is a count over the cases file, and the per-case detail for the largest swings is
printed so a human can open those traces in tools/replay/index.html.
"""
import argparse, collections, json, os, statistics as st, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load(p):
    return json.load(open(os.path.join(ROOT, p))) if os.path.exists(os.path.join(ROOT, p)) else None


def features(g):
    """Per-game mechanism features from one trace."""
    # result.turn is None (not -1) for an unwon game, so normalise here rather than at each use.
    _w = (g.get("result") or {}).get("turn")
    f = {"win": (_w if isinstance(_w, int) and _w > 0 else -1), "casts": [], "attacks": [],
         "lands": 0, "opening": len(g.get("openingHand") or []), "mulls": 0}
    f["mulls"] = max(0, len(g.get("mulliganSequence") or []) - 1)
    board_at = {}
    for t in g.get("turns", []):
        turn = t.get("turn")
        for a in t.get("actions", []):
            ty = a.get("type")
            if ty == "CAST_SPELL":
                f["casts"].append((turn, a.get("cardName"), a.get("manaPaid", "")))
            elif ty == "PLAY_LAND":
                f["lands"] += 1
            elif ty == "ATTACK":
                f["attacks"].append((turn, a.get("damage", 0), a.get("oppLife", 0)))
        b = t.get("boardAfter") or {}
        if b:
            board_at[turn] = b
    f["board"] = board_at
    last = max(board_at) if board_at else None
    f["opp_life_end"] = (board_at[last] or {}).get("opponentLife", None) if last else None
    f["best_attack"] = max((d for _, d, _ in f["attacks"]), default=0)
    f["best_turn"] = next((t for t, d, _ in f["attacks"] if d == f["best_attack"]), None)
    if f["best_turn"] is not None and f["best_turn"] in board_at:
        bf = board_at[f["best_turn"]].get("battlefield") or []
        f["creatures_at_best"] = sum(1 for p in bf if not p.get("isLand"))
    else:
        f["creatures_at_best"] = 0
    return f


def phase_key(t):
    return (t.get("turn"), t.get("phase"))


def divergence(ga, gb):
    """First (turn, phase) whose action list differs, plus each side's actions there."""
    pa = {phase_key(t): t.get("actions", []) for t in ga.get("turns", [])}
    pb = {phase_key(t): t.get("actions", []) for t in gb.get("turns", [])}

    def sig(acts):
        return [(a.get("type"), a.get("cardName"), a.get("damage")) for a in acts]

    for k in sorted(set(pa) | set(pb)):
        if sig(pa.get(k, [])) != sig(pb.get(k, [])):
            return k, sig(pa.get(k, [])), sig(pb.get(k, []))
    return None, [], []


def hist(vals, label, width=40):
    if not vals:
        return f"  {label}: (none)"
    c = collections.Counter(vals)
    top = max(c.values())
    out = [f"  {label}  (n={len(vals)}, median {st.median(vals):g})"]
    for k in sorted(c):
        out.append(f"    {k:>4}  {'#' * max(1, round(width * c[k] / top)):<{width}} {c[k]:,}")
    return "\n".join(out)


def side_summary(name, F):
    n = len(F)
    if not n:
        return f"### {name}: no cases\n"
    return (f"- casts/game {st.fmean(len(f['casts']) for f in F):.2f}   "
            f"lands/game {st.fmean(f['lands'] for f in F):.2f}   "
            f"mulligans/game {st.fmean(f['mulls'] for f in F):.2f}\n"
            f"- biggest attack: mean {st.fmean(f['best_attack'] for f in F):.1f}, "
            f"median {st.median(f['best_attack'] for f in F):g}, "
            f"max {max(f['best_attack'] for f in F)}\n"
            f"- creatures on board at that attack: mean "
            f"{st.fmean(f['creatures_at_best'] for f in F):.2f}\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cases")
    ap.add_argument("--detail", type=int, default=8, help="per-case detail for the N largest swings")
    a = ap.parse_args()
    idx = json.load(open(a.cases))
    cmp_ = idx["comparison"]
    A, B = cmp_["A"], cmp_["B"]
    names = idx.get("swapped_names", {})
    print(f"# Mechanism -- {A} vs {B} at {cmp_['life']} life\n")
    print(f"swapped slot: {', '.join(names.get('A', []))}  ->  {', '.join(names.get('B', []))}   "
          f"(card numbers {idx.get('swapped_numbers')})\n")
    print(f"{len(idx['cases'])} case games, each replayed in BOTH arms on the same seed. "
          f"Traces: `{idx['traces']}` -- drag one onto tools/replay/index.html.\n")

    rows = []
    for c in idx["cases"]:
        ga, gb = load(c["traceA"]), load(c["traceB"])
        if not ga or not gb:
            continue
        fa, fb = features(ga), features(gb)
        k, sa, sb = divergence(ga, gb)
        rows.append((c, fa, fb, k, sa, sb))
    if not rows:
        sys.exit("no traces could be read -- was the replay run?")

    # BOTH DIRECTIONS POOLED. The per-direction blocks below are conditioned on which side won, so
    # "the winner had a bigger attack / played fewer lands" is just the selection restated. Because
    # the cases are picked in equal numbers from each direction, pooling them cancels most of that
    # and leaves a comparison that is about the CARDS.
    A_lbl, B_lbl = cmp_["A"], cmp_["B"]
    print(f"\n## Both directions pooled ({len(rows)} cases, balanced by construction)\n")
    print("| quantity | " + A_lbl + " | " + B_lbl + " |")
    print("|---|---:|---:|")
    for label, f in (("biggest attack, mean", lambda x: x["best_attack"]),
                     ("biggest attack, max", None),
                     ("creatures at that attack", lambda x: x["creatures_at_best"]),
                     ("spells cast per game", lambda x: len(x["casts"])),
                     ("lands played per game", lambda x: x["lands"]),
                     ("win turn (9 = unwon)", lambda x: x["win"] if x["win"] > 0 else 9)):
        if f is None:
            print(f"| {label} | {max(r[1]['best_attack'] for r in rows)} | "
                  f"{max(r[2]['best_attack'] for r in rows)} |")
            continue
        print(f"| {label} | {st.fmean(f(r[1]) for r in rows):.2f} | "
              f"{st.fmean(f(r[2]) for r in rows):.2f} |")
    print("\n_The per-direction blocks that follow are selection-conditioned; read them for the "
          "divergence turn, the shortfall distribution and the individual traces, not for "
          "side-vs-side aggregates._")

    for direction, sel in (("B wins sooner", lambda r: r[0]["delta"] < 0),
                           ("A wins sooner", lambda r: r[0]["delta"] > 0)):
        R = [r for r in rows if sel(r)]
        print(f"\n## {direction}  ({len(R)} cases)\n")
        if not R:
            continue
        print(f"### {A}\n" + side_summary(A, [r[1] for r in R]))
        print(f"### {B}\n" + side_summary(B, [r[2] for r in R]))
        print(hist([r[3][0] for r in R if r[3]], "first divergence, by turn"))
        loser = 1 if direction.startswith("B") else 2
        # "Fell short by N": the slower side's OPPONENT LIFE at the turn the faster side won.
        # Not the life at the slower side's own final turn -- by then it has won too, so that
        # number is post-lethal and negative, and says nothing about the gap. Measured at the
        # faster side's win turn it is exactly "how much damage the loser was still missing".
        short, boards = [], []
        for r in R:
            los, win = (r[loser], r[2 if loser == 1 else 1])
            wt = win["win"]
            if wt and wt > 0 and wt in los["board"]:
                short.append(los["board"][wt].get("opponentLife"))
                bf_l = los["board"][wt].get("battlefield") or []
                bf_w = (win["board"].get(wt) or {}).get("battlefield") or []
                boards.append((sum(1 for p in bf_l if not p.get("isLand")),
                               sum(1 for p in bf_w if not p.get("isLand"))))
        print("\n" + hist([x for x in short if x is not None],
                          "damage the SLOWER side still needed on the turn the faster side won"))
        if boards:
            print(f"\n  creatures on board at that same turn: slower side "
                  f"{st.fmean(b[0] for b in boards):.2f}, faster side "
                  f"{st.fmean(b[1] for b in boards):.2f}   (compared at the SAME turn -- comparing "
                  "at each side's own win turn would just measure the win turn)")
        # What each side cast that the other did not -- the "why", both directions.
        gap, rgap = collections.Counter(), collections.Counter()
        for r in R:
            win, los = (r[2], r[1]) if loser == 1 else (r[1], r[2])
            cw = collections.Counter(n for _, n, _ in win["casts"])
            cl = collections.Counter(n for _, n, _ in los["casts"])
            for n, k2 in (cw - cl).items():
                gap[n] += k2
            for n, k2 in (cl - cw).items():
                rgap[n] += k2
        print("\n  cards the FASTER side cast and the slower side did not (total copies):")
        for n, k2 in gap.most_common(12):
            print(f"    {n:28s} {k2:,}")
        print("  ...and what the SLOWER side cast instead:")
        for n, k2 in rgap.most_common(12):
            print(f"    {n:28s} {k2:,}")
        print(f"\n  largest swings (open these in the viewer):")
        seen_gi = set()
        picks = []
        for r in sorted(R, key=lambda r: -abs(r[0]["delta"])):
            if r[0]["gi"] in seen_gi:      # one row per GAME: contexts share seeds, so the same
                continue                   # game recurs in up to 15 contexts and would fill the list
            seen_gi.add(r[0]["gi"])
            picks.append(r)
            if len(picks) >= a.detail:
                break
        for r in picks:
            c, fa, fb, k, sa, sb = r
            print(f"    {c['ctx']:24s} gi={c['gi']:<6} {A} wt={c['wtA']} vs {B} wt={c['wtB']}"
                  f"   diverge at {k}")
            print(f"      {A}: {[x[1] for x in sa if x[1]]}  best attack {fa['best_attack']} "
                  f"with {fa['creatures_at_best']} creatures, opp left {fa['opp_life_end']}")
            print(f"      {B}: {[x[1] for x in sb if x[1]]}  best attack {fb['best_attack']} "
                  f"with {fb['creatures_at_best']} creatures, opp left {fb['opp_life_end']}")
            print(f"      repro: {c['repro']}")


if __name__ == "__main__":
    main()
