#!/usr/bin/env python3
"""CAST-ORDER TRUTH: a plan's advertised cast order is the one it executes, and the ordering the
menu shows by default is the canonical one.

USER 2026-09-25, candidate-B Fungus seed 8 gi0 T3: "The mana usage is just bad in this line. It
fails to keep the Peat Bog despite the fact that it isn't difficult to do so."

WHY THIS IS A CHECK AND NOT A NOTE IN A DOC. Nothing else in the suite can see either half.

  * The REALISED order is human-play-only by construction. The cast-ORDERING search is what makes
    two plans differ by order at all, and it is gated on DecisionUnpruned(SearchOrder) -- open under
    the viewer, shut in autonomous play -- so the regression digests cannot witness it, and
    viewer_protocol_check.py replays plan indices and compares win turns rather than reading the
    field. `cast_order_canonical` reported the canonical SORT for plans that execute in VECTOR
    order for as long as the field existed, and every green gate stayed green.
  * The DISPLAY CAP is display-only and viewer_protocol_check.py runs UNCAPPED, so a cast set whose
    two orderings differ by a sacrificed land -- and whose good ordering falls outside the emitted
    200 -- is invisible there BY CONSTRUCTION. play_cap_coverage_check.py covers payload coverage
    (which (action, target, count) pairs survive), not which ORDERING represents a cast set.

WHAT IT DRIVES. The user's own frame. Turn 3, board = Forest / Peat Bog (one depletion counter
left) / Undercellar Myconid, hand holds Sol Ring {1} and Shroofus Sproutsire {2}{G}. Two orderings
of that pair are enumerated and they are not equivalent:

  Sol Ring first  -> its own {C}{C} pays the {2};  Peat Bog is never tapped and SURVIVES.
  Shroofus first  -> the {2}{G} takes Forest + Peat Bog's last counter; the land is SACRIFICED and
                     the Sol Ring the same line cast is left untapped.

So "which order does this plan really cast in" has a consequence a board read can settle, which is
what makes the label checkable at all. Three assertions:

  1. TRUTH -- the two orderings advertise DIFFERENT `cast_order_canonical` values (they advertised
     the same one before the fix, so nothing on the wire distinguished them);
  2. REALISED -- committing each one produces the board its label predicts (Sol Ring first =>
     Peat Bog alive and Sol Ring tapped; Shroofus first => Peat Bog in the graveyard);
  3. DEFAULT -- at the viewer's real cap the ordering emitted for that cast set is the CANONICAL
     one, i.e. the menu no longer offers only the line that throws a land away.

SKIPS (exit 0) rather than failing when the frame is not reachable -- an enumeration change may
legitimately move the decision, and a brittle red gate is worse than none. It fails only on a label
that disagrees with the apply, or on a cap that hides the canonical ordering.

Usage:  python3 test/cast_order_truth_check.py       (MTG_BIN=<path> to override the binary)
"""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MTG = os.environ.get("MTG_BIN", os.path.join(ROOT, "build", "Release", "mtg"))
DECK_DIR = os.path.join(ROOT, "decks", "Fungus", "candidate-b-2026-09")
DECK = os.path.join(DECK_DIR, "Fungus.cod")
PROF = os.path.join(DECK_DIR, "Fungus.profile.json")
CARDS = os.path.join(ROOT, "src", "cards", "data", "cards.json")

# The pick stream that walks to the reported frame (turn 3 pre-combat main). Keep the hand, then the
# recorded turn-1 / turn-2 picks; -1 passes the non-main decisions between them.
PREFIX = "1,4,-1,-1,12,-1,-1"
PAIR = ["Shroofus Sproutsire", "Sol Ring"]   # sorted
LAND = "Secluded Courtyard"

DEC_B, DEC_E = "<<<CLAUDE_DECISION>>>", "<<<END_DECISION>>>"


def skip(why):
    print(f"cast-order truth: SKIP ({why})")
    sys.exit(0)


def frame(choices, cap):
    """The decision frame the pick stream reaches, at the given display cap (0 = uncapped)."""
    env = dict(os.environ, MTG_PLAY_PLANS_CAP=str(cap))
    args = [MTG, DECK, "--profile", PROF, "--cards-json", CARDS, "--claude-play",
            "--seed", "8", "--game-index", "0", "--max-turns", "8", "--depth", "0",
            "--choices", choices]
    out = subprocess.run(args, capture_output=True, text=True, env=env).stdout
    m = re.search(re.escape(DEC_B) + r"(.*?)" + re.escape(DEC_E), out, re.S)
    return json.loads(m.group(1)) if m else None


def pair_plans(d):
    """Every plan at this frame that plays LAND and casts exactly the PAIR, with its order label."""
    out = []
    for p in d.get("plans", []):
        casts = [a.get("card") for a in p.get("actions", []) if a.get("card")]
        if p.get("land") == LAND and sorted(casts) == PAIR:
            out.append((p["index"], p.get("cast_order_canonical"), casts))
    return out


def board_after(index):
    """(is Peat Bog still on the battlefield, is Sol Ring tapped) once `index` has been committed."""
    d = frame(PREFIX + "," + str(index), 0)
    if d is None:
        return None
    bf = {p["name"]: bool(p.get("tapped")) for p in d.get("me", {}).get("battlefield", [])}
    return ("Peat Bog" in bf, bf.get("Sol Ring"))


def main():
    for path in (MTG, DECK, PROF):
        if not os.path.exists(path):
            skip(f"missing {os.path.relpath(path, ROOT)}")

    full = frame(PREFIX, 0)
    if full is None or full.get("turn") != 3 or full.get("type") != "main_phase":
        skip("the turn-3 main-phase frame is not reachable from the recorded pick stream")
    plans = pair_plans(full)
    if len(plans) < 2:
        skip(f"the frame enumerates {len(plans)} ordering(s) of {PAIR}, not the two this drives")

    fails = []

    # (1) TRUTH -- the orderings must be distinguishable on the wire.
    labels = [tuple(lab or ()) for _, lab, _ in plans]
    if len(set(labels)) < 2:
        fails.append("two orderings of the same cast set advertise the SAME cast_order_canonical "
                     f"{labels[0]} -- the realised order is not being reported")

    # (2) REALISED -- each label must predict the board the apply produces. The mana rock first means
    #     its own {C}{C} pays the creature's generic, so the depletion land is never reached.
    for idx, label, vec in plans:
        if not label or len(label) < 2:
            fails.append(f"plan {idx} casts {vec} but carries no cast_order_canonical")
            continue
        if list(label) != vec:
            fails.append(f"plan {idx} advertises {list(label)} but its action vector is {vec}")
        res = board_after(idx)
        if res is None:
            fails.append(f"plan {idx} produced no follow-up frame when committed")
            continue
        bog_alive, ring_tapped = res
        rock_first = (label[0] == "Sol Ring")
        if rock_first and not (bog_alive and ring_tapped):
            fails.append(f"plan {idx} advertises Sol Ring first, but committing it left "
                         f"Peat Bog {'alive' if bog_alive else 'SACRIFICED'} and Sol Ring "
                         f"{'tapped' if ring_tapped else 'UNTAPPED'}")
        if not rock_first and bog_alive:
            fails.append(f"plan {idx} advertises {list(label)} (creature first), but committing it "
                         "kept Peat Bog -- the label does not describe the apply")

    # (3) DEFAULT -- at the viewer's real cap, the ordering shown for this cast set is canonical.
    capped = frame(PREFIX, 200)
    if capped is None:
        skip("the capped frame is not reachable")
    shown = pair_plans(capped)
    if not shown:
        fails.append("the capped menu offers NO ordering of "
                     f"{PAIR} (it is enumerated {len(plans)} times uncapped)")
    else:
        # Asserted against the ACTION VECTOR, never the label: before the fix the capped menu showed
        # the creature-first plan under a Sol-Ring-first label, so a label-keyed assertion would have
        # passed on exactly the frame the user reported.
        for idx, label, vec in shown:
            if vec[0] != "Sol Ring":
                fails.append(f"the capped menu represents {PAIR} with plan {idx}, which casts {vec} "
                             "-- the canonical order casts the mana rock first, and this ordering "
                             "sacrifices Peat Bog")

    if fails:
        print("cast-order truth: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print(f"cast-order truth: OK  ({len(plans)} orderings uncapped, {len(shown)} shown at cap 200; "
          "labels match the apply)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
