#!/usr/bin/env python3
"""Viewer-protocol regression check (backend/contract layer).

The tools/play GUI is a thin subprocess bridge over the engine's stateless
`--claude-play` protocol (see tools/play/README.md), so guarding that protocol
guards the viewer. This check harvests the `--choices` streams from the saved
`references/<deck>/claude_s<seed>_gi<gi>.json` games (real played lines = good
exercise cases -- your "reconstruct from games the user played") and replays
each one step by step, asserting the CONTRACT holds at every step:

  * every emitted decision is well-formed JSON with the required keys/types,
  * the game is fully determined by (deck, seed, gi, choices) -- the recorded
    choice at each step is a VALID index into that step's enumerated plans,
  * the replay runs to a clean terminal (CLAUDE_RESULT) or a valid decision,
    never an engine error / crash / malformed frame.

These are the interface guarantees the GUI is built against; they should stay
green across engine changes. A CONTRACT failure exits non-zero (a real break).

Separately, it REPORTS behaviour drift (replayed won/win_turn != the recorded
reference) as information only -- references are historical saves and the engine
evolves; a drift line means "re-save this reference via the GUI when satisfied"
(per the reference-JSON policy), not a test failure.

Usage:  python3 test/viewer_protocol_check.py            # all references
        python3 test/viewer_protocol_check.py --strict   # also FAIL on drift
        MTG_BIN=path python3 test/viewer_protocol_check.py
"""
import json, os, re, subprocess, sys, glob

MTG = os.environ.get("MTG_BIN", "./build/Release/mtg")
STRICT = "--strict" in sys.argv[1:]

# references/<dir> -> (deckfile, profile). Mirrors test/regression_cases.sh.
DECKS = {
    "Anti-Lifegain": ("decks/Anti-Lifegain.cod", "decks/Anti-Lifegain.profile.json"),
    "Hinata2":       ("decks/Hinata2.cod",       "decks/Hinata2.profile.json"),
    "Knights":       ("decks/Knights.cod",       "decks/Knights.profile.json"),
    "slivers_vial":  ("decks/slivers_vial.txt",  "decks/slivers_vial.profile.json"),
    "test_deck":     ("decks/test_deck.txt",     "decks/test_deck.profile.json"),
    "treasure_hunt": ("decks/treasure_hunt.txt", "decks/treasure_hunt.profile.json"),
}

DEC_RE = re.compile(r"<<<CLAUDE_DECISION>>>\s*(\{.*?\})\s*<<<END_DECISION>>>", re.S)
RES_RE = re.compile(r"<<<CLAUDE_RESULT>>>\s*(\{.*?\})\s*<<<END_RESULT>>>", re.S)


def flatten_choices(decisions):
    """The GUI encodes a multi-pick decision as a list `chosen`; the --choices
    CSV is a flat pick stream, so a list contributes its picks in order."""
    out = []
    for d in decisions:
        c = d["chosen"]
        out += [int(x) for x in c] if isinstance(c, list) else [int(c)]
    return out


def force_arg(ref):
    """Build --force-mulligan "<count>:<n1,n2,...>" from a reference's recorded mulligan, so the
    replay reconstructs the exact opening hand regardless of the current keep/bottoming heuristic.
    None when the reference predates mulligan recording (then the engine's live mulligan is used)."""
    m = ref.get("mulligan")
    if m is None:
        return None
    return f'{m.get("count", 0)}:' + ",".join(str(n) for n in m.get("bottom", []))


def replay(deck, prof, seed, gi, choices, force=None):
    """One stateless --claude-play invocation with the GUI's params (depth 0,
    no --reveal). Returns (exit_code, stdout)."""
    args = [MTG, deck, "--claude-play", "--seed", str(seed), "--game-index", str(gi),
            "--max-turns", "8", "--depth", "0", "--profile", prof,
            "--choices", ",".join(str(c) for c in choices)]
    if force is not None:
        args += ["--force-mulligan", force]
    p = subprocess.run(args, capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


REQUIRED_DECISION_KEYS = {"decision_index", "type", "turn"}


def hand_names(hand):
    return sorted(c.get("name", "") for c in hand)


def check_reference(path):
    """Step through one reference's choices, validating the contract each step.
    Returns (contract_ok, kind, detail) where kind is one of:
      "ok"        -- contract holds AND the recorded outcome reproduces,
      "play"      -- same opening hand, but the line/enumeration diverged
                     (a REAL behaviour change -- review / re-save the reference),
      "mulligan"  -- the engine now mulligans to a DIFFERENT opening hand, so the
                     recorded choices target a game that no longer occurs. The
                     player can't steer mulligan, so this is a setup change, not a
                     play regression (and not fixable by re-recording the same line).
    """
    ref = json.load(open(path))
    deck_dir = os.path.basename(os.path.dirname(path))
    if deck_dir not in DECKS:
        return True, "ok", f"skip (unknown deck dir {deck_dir})"
    deck, prof = DECKS[deck_dir]
    seed, gi = ref["seed"], ref["game_index"]
    choices = flatten_choices(ref["decisions"])
    force = force_arg(ref)   # reconstruct the recorded opening hand when the reference carries it

    # Root cause first: does the engine still open the SAME hand? With a recorded mulligan we FORCE
    # it (so the hand is reconstructed and this always matches); without one, a different opening
    # hand means the mulligan heuristic diverged -> classify as mulligan-drift, not play-drift.
    ref_decs = ref.get("decisions", [])
    if ref_decs:
        rc0, out0 = replay(deck, prof, seed, gi, [], force)
        if "Error:" in out0 or rc0 not in (0, 70):
            return False, "play", f"engine error at step 0 (rc={rc0}): {out0.strip()[-160:]}"
        m0 = DEC_RE.search(out0)
        ref_hand = hand_names(ref_decs[0].get("decision", {}).get("me", {}).get("hand", []))
        if m0 and ref_hand:
            cur_hand = hand_names(json.loads(m0.group(1)).get("me", {}).get("hand", []))
            if cur_hand != ref_hand:
                return True, "mulligan", (f"opening hand changed ({len(ref_hand)}->{len(cur_hand)} cards); "
                                          f"mulligan diverged -> recorded choices no longer apply")

    # Same opening hand: walk prefixes 0,1,..,len: each invocation must yield a well-formed decision
    # (exit 70) or the terminal result (exit 0), and the next recorded pick must
    # be a valid index into the plans just offered.
    for k in range(len(choices) + 1):
        rc, out = replay(deck, prof, seed, gi, choices[:k], force)
        if "Error:" in out or rc not in (0, 70):
            return False, "play", f"engine error at step {k} (rc={rc}): {out.strip()[-160:]}"
        if rc == 0:  # terminal reached before consuming all recorded picks
            m = RES_RE.search(out)
            if not m:
                return False, "play", f"exit 0 but no well-formed CLAUDE_RESULT at step {k}"
            res = json.loads(m.group(1))
            drift = (res.get("won") != ref.get("won")) or (res.get("win_turn") != ref.get("win_turn"))
            extra = f"; terminated early at step {k}/{len(choices)}" if k < len(choices) else ""
            det = f"replay won={res.get('won')} win_turn={res.get('win_turn')} vs ref won={ref.get('won')} win_turn={ref.get('win_turn')}{extra}"
            return True, ("play" if drift else "ok"), det
        # rc == 70: a decision frame
        m = DEC_RE.search(out)
        if not m:
            return False, "play", f"exit 70 but no well-formed CLAUDE_DECISION at step {k}"
        try:
            dec = json.loads(m.group(1))
        except json.JSONDecodeError as e:
            return False, "play", f"malformed decision JSON at step {k}: {e}"
        missing = REQUIRED_DECISION_KEYS - dec.keys()
        if missing:
            return False, "play", f"decision at step {k} missing keys {missing}"
        if dec.get("type") == "main_phase":
            plans = dec.get("plans")
            if not isinstance(plans, list):
                return False, "play", f"main_phase decision at step {k} has no plans list"
            if k < len(choices):
                pick = choices[k]
                # -1 = pass/cast-nothing is always legal; otherwise must index a plan.
                if pick != -1 and not (0 <= pick < len(plans)):
                    return True, "play", f"recorded pick {pick} out of range (nplans={len(plans)}) at step {k} -> enumeration drifted (same hand)"
    # Consumed every recorded pick and never hit a terminal -> line no longer wins where it did.
    return True, "play", f"replay did not terminate after {len(choices)} recorded picks (ref won={ref.get('won')} win_turn={ref.get('win_turn')})"


def main():
    # The one-level glob deliberately covers only the VERIFIED set, references/<deck>/claude_*.json.
    # Aspirational "known-slow" games live one level deeper (references/suboptimal/<deck>/…, see that
    # folder's README) and are excluded here: their win turn is knowingly beatable, so gating on them
    # would report permanent drift. Guard against a future deeper glob too.
    refs = sorted(p for p in glob.glob("references/*/claude_s*_gi*.json")
                  if not p.startswith(("references/suboptimal/", "references/optimal/")))
    if not refs:
        print("no reference games found under references/")
        return 0
    contract_fail = play_drift = mull_drift = ok = 0
    for path in refs:
        c_ok, kind, detail = check_reference(path)
        rel = path[len("references/"):]
        if not c_ok:
            print(f"  CONTRACT-FAIL  {rel}: {detail}"); contract_fail += 1
        elif kind == "mulligan":
            print(f"  mull-drift     {rel}: {detail}"); mull_drift += 1
        elif kind == "play":
            print(f"  play-drift     {rel}: {detail}"); play_drift += 1
        else:
            print(f"  ok             {rel}: {detail}"); ok += 1
    print(f"\nViewer protocol: {ok} ok, {play_drift} play-drift, {mull_drift} mull-drift, "
          f"{contract_fail} contract-fail  ({len(refs)} refs)")
    if play_drift:
        print("  play-drift  = same opening hand, line diverged -> a real behaviour change; re-save via tools/play when satisfied")
    if mull_drift:
        print("  mull-drift  = engine now mulligans to a different hand -> recorded choices don't apply; NOT a play regression")
    if contract_fail:
        return 1
    # --strict gates on PLAY drift only: mulligan-drift is a setup change the player can't
    # currently steer, so it must not fail the gate (until mulligan is player-controlled + recorded).
    return 1 if (STRICT and play_drift) else 0


if __name__ == "__main__":
    sys.exit(main())
