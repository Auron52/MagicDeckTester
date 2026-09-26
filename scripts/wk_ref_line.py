#!/usr/bin/env python3
"""Print the FULL turn-by-turn line the engine played on a reference game, beside the human's.

Answers "what exactly did the search do to win a turn earlier?" -- which a win-turn column cannot.

Sources, and they are DIFFERENT SHAPES, which is the only fiddly part:

  ENGINE  logs/<bench-log-root>/<deck>__<ref-stem>_gi<N>.json  -- written by ref_bench.py's
          --game-trace-dir. A list of per-PHASE records: {turn, phase, actions[], boardAfter{}}.
          Attackers are not named in the ATTACK action (it carries only damage/oppLife), so they are
          recovered by diffing the tapped flags on the battlefield between MAIN_1 and COMBAT. That
          diff CANNOT see a vigilant attacker (it never taps) and the trace has no `attacking`
          flag, so vigilance candidates are named separately -- see the comment at the ATTACK arm.
  HUMAN   references/<Deck>/claude_s<S>_gi<G>.json -- the viewer's saved decision stream:
          {decisions[{decision{type,turn,plans[]}, chosen}], win_turn, mulligan}. The human's play is
          recorded as the PLAN INDEX chosen, so the line has to be read back out of the plan summary.

Usage:
  python3 scripts/wk_ref_line.py                      # every reference the engine beat
  python3 scripts/wk_ref_line.py claude_s1_gi0        # one game
  python3 scripts/wk_ref_line.py --all                # all references
"""
import argparse
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REFDIR = os.path.join(ROOT, "references/WhiteKnights")
BENCH = os.path.join(ROOT, "logs/wk_refbench")


def human_line(path):
    """-> (win_turn, mulligan, [(turn, summary)]) from the viewer's decision stream."""
    h = json.load(open(path))
    out = []
    for d in h.get("decisions", []):
        dec = d.get("decision") or {}
        if dec.get("type") != "main_phase":
            continue
        ch = d.get("chosen")
        if ch is None or ch < 0:          # -1 on main_phase means "cast nothing"
            continue
        plans = dec.get("plans") or []
        pick = next((p for p in plans if p.get("index") == ch), None)
        if pick is None and 0 <= ch < len(plans):
            pick = plans[ch]
        out.append((dec.get("turn"), (pick or {}).get("summary", "?")))
    return h.get("win_turn"), (h.get("mulligan") or {}), out


def engine_line(path):
    """-> (win_turn, [(turn, [lines])]) from the bench's per-phase trace."""
    e = json.load(open(path))
    per = {}
    order = []
    prev_tapped = {}
    cast_this_turn = set()
    cur_turn = None
    for rec in e.get("turns", []):
        t = rec.get("turn")
        if t != cur_turn:
            cur_turn, cast_this_turn = t, set()
        if t not in per:
            per[t] = []
            order.append(t)
        ph = rec.get("phase")
        board = rec.get("boardAfter") or {}
        bf = board.get("battlefield") or []
        tapped_now = {c.get("card"): c.get("tapped") for c in bf}
        names = {c.get("card"): c.get("cardName") for c in bf}

        for a in rec.get("actions") or []:
            ty = a.get("type")
            nm = a.get("cardName") or ""
            if ty == "DRAW":
                per[t].append(f"draw {nm}")
            elif ty == "PLAY_LAND":
                per[t].append(f"land {nm}")
            elif ty == "CAST_SPELL":
                mana = a.get("manaPaid")
                cast_this_turn.add(nm)
                per[t].append(f"cast {nm}" + (f"  [{mana}]" if mana else ""))
            elif ty == "ATTACK":
                # Attackers = creatures that became tapped crossing into COMBAT. The ATTACK action
                # itself names none, so this diff is the only way to report WHO swung.
                swung = [names.get(cid, "?") for cid, tp in tapped_now.items()
                         if tp and not prev_tapped.get(cid) and not _is_land(bf, cid)]
                who = ", ".join(sorted(swung)) if swung else "(attackers not recoverable)"
                # A VIGILANT ATTACKER NEVER TAPS, so the diff above cannot see it -- and the trace
                # records only `tapped`, never an `attacking` flag. WhiteKnights hits this on every
                # Adeline game: she has vigilance AND power equal to your creature count, so she is
                # routinely the largest attacker while being invisible here (T4 of claude_s1_gi0:
                # 8 of the 15 damage). Name the candidates rather than print a list that reads
                # complete -- untapped non-land creatures that were already out before this combat,
                # excluding anything cast this turn (summoning-sick, so it cannot have attacked).
                vig = sorted({names.get(cid, "?") for cid in prev_tapped
                              if cid in tapped_now and not tapped_now[cid]
                              and not _is_land(bf, cid)
                              and names.get(cid) not in cast_this_turn})
                line = f"ATTACK for {a.get('damage')}  -> opp {a.get('oppLife')}   [{who}]"
                if vig:
                    line += f"  (+ any of these if vigilant: {', '.join(vig)})"
                per[t].append(line)
            elif ty:
                per[t].append(f"{ty.lower()} {nm}".strip())
        if ph != "DRAW":
            prev_tapped = tapped_now
    res = e.get("result") or {}
    return res.get("turn"), [(t, per[t]) for t in order]


def _is_land(bf, cid):
    for c in bf:
        if c.get("card") == cid:
            return bool(c.get("isLand"))
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("games", nargs="*", help="reference stems, e.g. claude_s1_gi0")
    ap.add_argument("--all", action="store_true")
    args = ap.parse_args()

    stems = args.games
    if not stems:
        refs = sorted(glob.glob(os.path.join(REFDIR, "claude_s*.json")))
        stems = []
        for r in refs:
            stem = os.path.basename(r)[:-5]
            gi = re.search(r"_gi(\d+)$", stem)
            tr = os.path.join(BENCH, f"whiteknights__{stem}_gi{gi.group(1)}.json")
            if not os.path.exists(tr):
                continue
            hw, _, _ = human_line(r)
            ew, _ = engine_line(tr)
            if args.all or (ew is not None and hw is not None and ew < hw):
                stems.append(stem)

    if not stems:
        # Reaching here with traces present is the GOOD outcome, not an error: the default selection
        # is "games the engine beat the human on", and after the 2026-09-26 re-play that set is empty.
        have = glob.glob(os.path.join(BENCH, "whiteknights__*.json"))
        if have:
            print(f"the engine beat the human on NO reference ({len(have)} traces checked) -- "
                  "pass --all or a stem to print a line anyway")
            return 0
        print("no traces found -- run scripts/ref_bench.py --deck whiteknights --log-root logs/wk_refbench first")
        return 1

    for stem in stems:
        ref = os.path.join(REFDIR, stem + ".json")
        gi = re.search(r"_gi(\d+)$", stem).group(1)
        tr = os.path.join(BENCH, f"whiteknights__{stem}_gi{gi}.json")
        if not (os.path.exists(ref) and os.path.exists(tr)):
            print(f"!! {stem}: missing reference or trace"); continue
        hw, mull, hl = human_line(ref)
        ew, el = engine_line(tr)
        keep = 7 - int(mull.get("count") or 0)
        print("=" * 78)
        print(f"{stem}   human won T{hw}   engine won T{ew}   "
              f"(mulligan to {keep}, bottomed {len(mull.get('bottom') or [])})")
        print("=" * 78)
        print("\n-- ENGINE (shipped search, same forced opening hand) " + "-" * 24)
        for t, lines in el:
            if not lines:
                continue
            print(f"  T{t}")
            for l in lines:
                print(f"      {l}")
        print("\n-- HUMAN (your saved line; main-phase picks only) " + "-" * 27)
        for t, s in hl:
            print(f"  T{t}  {s}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
