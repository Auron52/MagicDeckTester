#!/usr/bin/env python3
"""Viewer-protocol regression check (backend/contract layer).

The tools/play GUI is a thin subprocess bridge over the engine's stateless
`--claude-play` protocol (see tools/play/README.md), so guarding that protocol
guards the viewer. This check replays each saved
`references/<deck>/claude_s<seed>_gi<gi>.json` game (real played lines = good
exercise cases) step by step, asserting the CONTRACT holds at every step:

  * every emitted decision is well-formed JSON with the required keys/types,
  * the replay runs to a clean terminal (CLAUDE_RESULT) or a valid decision,
    never an engine error / crash / malformed frame.

These are the interface guarantees the GUI is built against; they should stay
green across engine changes. A CONTRACT failure exits non-zero (a real break).

Replay is by INTENT, not by raw index (see check_reference): recorded picks are
re-anchored by plan CONTENT, and decision points a reference predates are
answered from the engine's own heuristic_default. This is what keeps the
user-owned references stable across engine evolution (per
docs/design/decision-indexed-choice-protocol.md: absent answer -> engine
default). What the walk can then still surface, as information:

  repaired      the line reproduces after index repair / default answers
                (re-save via the GUI to make it permanent),
  play-drift    the line replays mechanically but ends in a DIFFERENT
                outcome -- a real behaviour change to review,
  shuffle-dead  a recorded plan is gone AND the hand differs: a mid-game
                reshuffle moved the draws (the ACCEPTED class; re-play by hand),
  ENUM-GAP      a recorded plan is gone with an IDENTICAL hand: the engine
                stopped offering a plan for the same state -- investigate,
  mull-drift    the engine opens a different hand; the recorded game no longer
                occurs (references without a recorded mulligan only).

Usage:  python3 test/viewer_protocol_check.py            # all references
        python3 test/viewer_protocol_check.py --strict   # also FAIL on drift
        MTG_BIN=path python3 test/viewer_protocol_check.py
"""
import json, os, re, subprocess, sys, glob

MTG = os.environ.get("MTG_BIN", "./build/Release/mtg")
STRICT = "--strict" in sys.argv[1:]

# references/<dir> -> (deckfile, profile). Mirrors test/regression_cases.sh (per-deck folder
# layout, docs/design/per-deck-folder-layout.md).
DECKS = {
    "Anti-Lifegain": ("decks/Anti-Lifegain/Anti-Lifegain.cod", "decks/Anti-Lifegain/Anti-Lifegain.profile.json"),
    "Hinata2":       ("decks/Hinata2/Hinata2.cod",             "decks/Hinata2/Hinata2.profile.json"),
    "Knights":       ("decks/Knights/Knights.cod",             "decks/Knights/Knights.profile.json"),
    "slivers_vial":  ("decks/slivers_vial/slivers_vial.txt",   "decks/slivers_vial/slivers_vial.profile.json"),
    "burn":          ("decks/burn/burn.txt",                   "decks/burn/burn.profile.json"),
    "treasure_hunt": ("decks/treasure_hunt/treasure_hunt.txt", "decks/treasure_hunt/treasure_hunt.profile.json"),
    "Auras":         ("decks/Auras/Auras.cod",                 "decks/Auras/Auras.profile.json"),
    "Dragonstorm":   ("decks/Dragonstorm/Dragonstorm.cod",     "decks/Dragonstorm/Dragonstorm.profile.json"),
}

DEC_RE = re.compile(r"<<<CLAUDE_DECISION>>>\s*(\{.*?\})\s*<<<END_DECISION>>>", re.S)
RES_RE = re.compile(r"<<<CLAUDE_RESULT>>>\s*(\{.*?\})\s*<<<END_RESULT>>>", re.S)


# Decision types that ride a KEYED SIDE-CHANNEL, not the positional --choices stream: their recorded
# `chosen` must NOT be folded into --choices (that would desync the positional replay). They are
# reconstructed separately in side_channel_args() and passed as --firebreathe / --storage-hold.
SIDE_CHANNEL_TYPES = {"firebreathe", "storage_hold"}


# Decision types that --force-mulligan resolves INTERNALLY (keep/mull answers and the bottoming
# picks both come from the forced spec), so the engine never calls the external chooser for them
# and they consume no --choices slot under force.
FORCED_MULLIGAN_TYPES = {"mulligan", "bottom"}


def flatten_choices(decisions, drop_mulligan=False):
    """The GUI encodes a multi-pick decision as a list `chosen`; the --choices
    CSV is a flat pick stream, so a list contributes its picks in order. Side-channel
    decisions (firebreathe / storage_hold) consume NO --choices slot and are skipped here.

    drop_mulligan: under --force-mulligan the engine resolves the mulligan AND the bottoming
    internally and NEVER calls the external chooser for them, so those recorded picks consume no
    --choices slot either. Folding them in shifts the whole positional stream (the turn-1 main
    phase then eats the keep/mull answer) and every downstream pick reads as "enumeration drift".
    Must mirror force_arg(): drop iff the reference is being forced."""
    out = []
    for d in decisions:
        t = d.get("decision", {}).get("type")
        if t in SIDE_CHANNEL_TYPES:
            continue
        if drop_mulligan and t in FORCED_MULLIGAN_TYPES:
            continue
        c = d["chosen"]
        out += [int(x) for x in c] if isinstance(c, list) else [int(c)]
    return out


def side_channel_args(decisions):
    """Reconstruct the keyed side-channel args a reference used, so a saved reordered/held game replays
    faithfully: --firebreathe "turn:count", --storage-hold "turn:num:val", --cast-order "ord:A|B" (the
    applied cast order recorded on the main-phase entry). All keyed (turn / land# / main-ordinal), so
    passing the full set for every prefix is safe -- the engine applies each only when it reaches that
    turn/ordinal. Empty (existing references use none) => no extra args => identical to before."""
    fb, sh, co = [], [], []
    for d in decisions:
        dec = d.get("decision", {})
        t = dec.get("type")
        if t == "firebreathe":
            fb.append(f'{dec.get("turn")}:{int(d["chosen"])}')
        elif t == "storage_hold":
            sh.append(f'{dec.get("turn")}:{dec.get("land_idx")}:{int(d["chosen"])}')
        elif t == "main_phase" and d.get("cast_order"):
            co.append(f'{dec.get("main_ordinal")}:' + "|".join(d["cast_order"]))
    extra = []
    if fb:
        extra += ["--firebreathe", ",".join(fb)]
    if sh:
        extra += ["--storage-hold", ",".join(sh)]
    if co:
        extra += ["--cast-order", ";".join(co)]
    return extra


def force_arg(ref):
    """Build --force-mulligan "<count>:<n1,n2,...>" from a reference's recorded mulligan, so the
    replay reconstructs the exact opening hand regardless of the current keep/bottoming heuristic.
    None when the reference predates mulligan recording (then the engine's live mulligan is used)."""
    m = ref.get("mulligan")
    if m is None:
        return None
    return f'{m.get("count", 0)}:' + ",".join(str(n) for n in m.get("bottom", []))


def replay(deck, prof, seed, gi, choices, force=None, extra=None, max_turns=8):
    """One stateless --claude-play invocation with the GUI's params (depth 0,
    no --reveal). `extra` carries reconstructed keyed side-channel args (--firebreathe /
    --storage-hold / --cast-order); safe for every prefix (keyed, applied only when reached).
    max_turns must cover the reference's recorded win turn, else a real saved win (e.g. the
    Hinata2 T9 game) can never replay. Returns (exit_code, stdout)."""
    args = [MTG, deck, "--claude-play", "--seed", str(seed), "--game-index", str(gi),
            "--max-turns", str(max_turns), "--depth", "0", "--profile", prof,
            "--choices", ",".join(str(c) for c in choices)]
    if force is not None:
        args += ["--force-mulligan", force]
    if extra:
        args += extra
    p = subprocess.run(args, capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


REQUIRED_DECISION_KEYS = {"decision_index", "type", "turn"}


def hand_names(hand):
    return sorted(c.get("name", "") for c in hand)


def frame_ident(d):
    """WHAT a decision frame is, independent of which option was picked. Alignment is done on
    this, never on stream position: position is what made references fragile (a decision point
    added later shifts every downstream pick; see the module docstring)."""
    return (d.get("type"), d.get("turn"), d.get("source"))


def plan_key(p):
    """Content identity of a plan, independent of its index: the land played plus the multiset of
    casts. Used as the order-insensitive fallback behind an exact `summary` match."""
    return (p.get("land"), tuple(sorted(p.get("casts") or [])))


def find_plan(recorded, plans, recorded_index=None):
    """Index of `recorded` in the current `plans`, or None. Exact summary first (keeps cast-order
    variants distinct), then land+casts (tolerates a summary-format change or a dropped order
    variant). This is how a recorded pick survives an enumeration change: the reference stores WHAT
    it chose, so the index it was stored under is recoverable rather than load-bearing.
    (Shared with scripts/ref_line_replay.py, which imports it from here.)

    Deliberately NO casts-only/any-land tier: for a shuffle-moved hand the substituted land is a
    different hidden choice (a fetch fetches something else, a shock pays differently), the
    substitutions compound turn over turn, and the measured result is a false loss rather than a
    recovery. A reference that played a post-shuffle draw is dead (see
    docs/design/antilife-reference-shuffle-alignment.md); the checker's job is to SAY so, not to
    approximate the line.

    recorded_index: when several current plans MATCH equally, prefer this index. Plans can be
    visibly IDENTICAL yet distinct -- an MDFC land's two faces ('Branchloft Pathway' {G} vs its
    Boulderloft {W} back) emit the same summary and land name, and only the index separates them.
    Auras s3_gi2 records pick 2 of two identical 'Branchloft' summaries; taking the FIRST match
    silently flips the face to {G} and starves every downstream {W}{W} cast."""
    if recorded is None:
        return None
    hits = [i for i, p in enumerate(plans) if p.get("summary") == recorded.get("summary")]
    if not hits:
        want = plan_key(recorded)
        hits = [i for i, p in enumerate(plans) if plan_key(p) == want]
    if not hits:
        return None
    if recorded_index is not None and recorded_index in hits:
        return recorded_index
    return hits[0]


def engine_default(d):
    """The answer the engine itself would give for a frame the reference never recorded.
    Every emitted frame carries `heuristic_default` (= the AI's pick); the mulligan/bottom
    frames carry `ai_choice` instead. Answering from these makes a NEWLY ADDED decision point
    non-destructive: the replay proceeds exactly as the unattended engine would, and the
    reference's own recorded picks stay aligned to the decisions it actually recorded."""
    ac = d.get("ai_choice")
    if isinstance(ac, int):
        return ac, "ai_choice"
    if isinstance(ac, dict) and isinstance(ac.get("index"), int):
        return ac["index"], "ai_choice"
    hd = d.get("heuristic_default")
    if isinstance(hd, int):
        return hd, "heuristic_default"
    return 0, "fallback"


def check_reference(path):
    """Replay one reference by INTENT, validating the contract at every step.

    A reference records each pick as a positional index into that step's plan list, but it also
    records the full description of the plan it chose (summary/land/casts). Indices are fragile --
    they silently re-point whenever the plan set changes or a new decision point is inserted
    upstream -- so this walk aligns on decision IDENTITY and resolves each pick by CONTENT:

      * a frame the reference recorded  -> replay its recorded pick, re-resolved to whatever index
        now carries the same plan summary (an index shift is repaired, not reported as drift),
      * a frame the reference does NOT have (a decision point added after it was saved) -> answer
        from the engine's own heuristic_default/ai_choice and consume NO recorded pick, so the rest
        of the recorded line stays aligned.

    Returns (contract_ok, kind, detail) where kind is one of:
      "ok"            -- contract holds AND the recorded outcome reproduces,
      "repaired"      -- outcome reproduces, but only after repairing stale indices / answering
                         decision points the reference predates. NOT a regression: the played line
                         is intact. Re-save the reference to make the repairs permanent.
      "play"          -- the line replays to a terminal with a DIFFERENT outcome: a real behaviour
                         change to review,
      "shuffle-dead"  -- a recorded plan is gone AND the hand at that frame differs from the
                         recorded one: a mid-game reshuffle (fetch crack, cantrip shuffle) moved
                         the draws, so the line targets cards that are no longer there. The
                         ACCEPTED drift class -- only re-playing the game by hand restores it,
      "unresolvable"  -- a recorded plan is gone while the hand is IDENTICAL: the engine stopped
                         offering a plan it used to offer for the same state. An enumeration gap
                         worth investigating -- this is the loud category,
      "mulligan"      -- the engine opens a genuinely DIFFERENT hand (compared like-for-like against
                         the same decision frame), so the recorded game no longer occurs.
    """
    ref = json.load(open(path))
    deck_dir = os.path.basename(os.path.dirname(path))
    if deck_dir not in DECKS:
        return True, "ok", f"skip (unknown deck dir {deck_dir})"
    deck, prof = DECKS[deck_dir]
    seed, gi = ref["seed"], ref["game_index"]
    side = side_channel_args(ref["decisions"])   # --firebreathe / --storage-hold / --cast-order the ref used
    force = force_arg(ref)   # reconstruct the recorded opening hand when the reference carries it
    # The reference's own decisions, in the order the positional stream used to address them. Under
    # --force-mulligan the engine resolves keep/bottom internally, so those carry no answer here.
    kept = [d for d in ref.get("decisions", [])
            if d["decision"].get("type") not in SIDE_CHANNEL_TYPES
            and not (force is not None and d["decision"].get("type") in FORCED_MULLIGAN_TYPES)]
    # A reference that won later than the default horizon (Hinata2 s12_gi11 wins T9) needs a
    # horizon that covers it, else the replay can never reach its recorded terminal.
    mt = max(8, ref.get("win_turn") or 0)

    resolved = []        # the pick stream actually sent (content-resolved + defaults filled in)
    ri = 0               # how many of the reference's own decisions have been consumed
    inserted, shifted = [], []
    hand_checked = False
    # One invocation per decision; bounded well above any real game so a protocol change that
    # loops cannot hang the suite (that is why these checks live outside smoke/regression).
    for _ in range(400):
        rc, out = replay(deck, prof, seed, gi, resolved, force, side, mt)
        if "Error:" in out or rc not in (0, 70):
            return False, "play", f"engine error after {len(resolved)} picks (rc={rc}): {out.strip()[-160:]}"
        if rc == 0:  # clean terminal
            m = RES_RE.search(out)
            if not m:
                return False, "play", f"exit 0 but no well-formed CLAUDE_RESULT after {len(resolved)} picks"
            res = json.loads(m.group(1))
            drift = (res.get("won") != ref.get("won")) or (res.get("win_turn") != ref.get("win_turn"))
            repairs = []
            if shifted:
                repairs.append(f"{len(shifted)} stale index/indices repaired (e.g. {shifted[0]})")
            if inserted:
                repairs.append(f"{len(inserted)} decision(s) the ref predates answered by engine "
                               f"default (e.g. {inserted[0]})")
            if ri < len(kept):
                repairs.append(f"terminated with {len(kept) - ri}/{len(kept)} recorded decisions unused")
            det = (f"replay won={res.get('won')} win_turn={res.get('win_turn')} "
                   f"vs ref won={ref.get('won')} win_turn={ref.get('win_turn')}")
            if repairs:
                det += "; " + "; ".join(repairs)
            if drift:
                return True, "play", det
            return True, ("repaired" if (shifted or inserted) else "ok"), det
        # rc == 70: a decision frame -- contract checks first.
        m = DEC_RE.search(out)
        if not m:
            return False, "play", f"exit 70 but no well-formed CLAUDE_DECISION after {len(resolved)} picks"
        try:
            dec = json.loads(m.group(1))
        except json.JSONDecodeError as e:
            return False, "play", f"malformed decision JSON after {len(resolved)} picks: {e}"
        missing = REQUIRED_DECISION_KEYS - dec.keys()
        if missing:
            return False, "play", f"decision after {len(resolved)} picks missing keys {missing}"
        if dec.get("type") == "main_phase" and not isinstance(dec.get("plans"), list):
            return False, "play", f"main_phase decision after {len(resolved)} picks has no plans list"

        aligned = ri < len(kept) and frame_ident(dec) == frame_ident(kept[ri]["decision"])
        if not aligned:
            # A decision this reference never recorded (a point added after it was saved, or an
            # extra frame because the line ran longer). Consume no recorded pick. For a MAIN_PHASE
            # frame the faithful answer is PASS: the reference's own decisions already express
            # everything the human cast that turn, so an extra re-prompt (added by later engine
            # work) must add nothing -- answering it with a plan would cast cards the recorded
            # line needs later. For every other type, answer as the unattended engine would.
            if dec.get("type") == "main_phase":
                pick, src = -1, "pass"
            else:
                pick, src = engine_default(dec)
            inserted.append(f"{frame_ident(dec)}<-{pick}({src})")
            resolved.append(pick)
            continue

        rd = kept[ri]["decision"]
        # Like-for-like opening-hand check, on the FIRST aligned frame only: comparing a mulligan
        # frame (hand at top level) against a post-draw main_phase frame (hand under me.hand) is
        # what used to report a bogus "8->0 cards" mulligan divergence.
        if not hand_checked and dec.get("me", {}).get("hand") and rd.get("me", {}).get("hand"):
            hand_checked = True
            cur_hand, ref_hand = hand_names(dec["me"]["hand"]), hand_names(rd["me"]["hand"])
            if cur_hand != ref_hand:
                return True, "mulligan", (
                    f"hand differs at {frame_ident(dec)} ({len(ref_hand)}->{len(cur_hand)} cards); "
                    f"the recorded game no longer occurs")

        rec = kept[ri]["chosen"]
        rec_list = rec if isinstance(rec, list) else [rec]
        if dec.get("type") == "main_phase":
            p = int(rec_list[0])
            if p == -1:                      # pass / cast-nothing is always legal
                resolved.append(-1)
            else:
                rplans = rd.get("plans") or []
                recorded = rplans[p] if 0 <= p < len(rplans) else None
                if recorded is None:
                    return True, "unresolvable", (
                        f"recorded pick {p} is not a plan in the reference's own list at "
                        f"{frame_ident(rd)} -- the reference itself is inconsistent")
                cur_plans = dec.get("plans") or []
                q = find_plan(recorded, cur_plans, recorded_index=p)
                if q is None:
                    # Root-cause the miss before reporting it. If the hand at this frame is not
                    # the hand the reference recorded, a mid-game reshuffle (fetch/cantrip) moved
                    # the draws and the recorded line targets cards that are no longer there --
                    # the ACCEPTED shuffle-dead class (see antilife-reference-shuffle-alignment.md):
                    # only re-playing the game by hand can restore it. If the hand IS identical,
                    # the engine stopped offering a plan it used to offer for the same state --
                    # an ENUMERATION GAP, i.e. a real engine change to investigate.
                    cur_hand = hand_names(dec.get("me", {}).get("hand", []))
                    ref_hand = hand_names(rd.get("me", {}).get("hand", []))
                    where = (f"recorded plan {recorded.get('summary')!r} no longer enumerated at "
                             f"{frame_ident(rd)} (nplans {len(rplans)}->{len(cur_plans)})")
                    if ref_hand and cur_hand != ref_hand:
                        gone = sorted(set(ref_hand) - set(cur_hand))
                        new = sorted(set(cur_hand) - set(ref_hand))
                        return True, "shuffle-dead", (
                            f"{where}; hand differs (ref-only {gone} vs now {new}) -> a mid-game "
                            f"reshuffle moved the draws; only re-playing can restore this game")
                    caveat = ""
                    if inserted:
                        # A defaulted answer can diverge HIDDEN state (library order, exile) while
                        # leaving the hand identical -- then the gap may be that divergence, not
                        # the enumerator. Surface it so investigation starts at the right place.
                        caveat = (f"; CAVEAT: {len(inserted)} decision(s) the ref predates were "
                                  f"answered by default upstream (e.g. {inserted[0]}) -- a "
                                  f"differing default can diverge hidden state without changing "
                                  f"the hand")
                    return True, "unresolvable", (
                        f"{where}; hand IDENTICAL -> the engine no longer offers this plan for "
                        f"the same state (enumeration gap){caveat}")
                if q != p:
                    shifted.append(f"{frame_ident(dec)} {p}->{q}")
                resolved.append(q)
        else:
            resolved += [int(x) for x in rec_list]
        ri += 1
    return True, "unresolvable", f"replay did not terminate within 400 decisions ({len(resolved)} picks sent)"


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
    # SAMPLE mode (--sample / VIEWER_PROTOCOL_SAMPLE): one reference per deck dir. The full per-step
    # replay is multi-minute (an engine spawn per decision x every ref), too heavy for the <45min
    # regression budget, so regression runs a quick one-per-archetype CONTRACT sanity and overnight
    # runs the full sweep (the contract doesn't vary by seed set; sampling still hits every deck's
    # decision shapes). Picks the first ref per deck for determinism.
    if "--sample" in sys.argv[1:] or os.environ.get("VIEWER_PROTOCOL_SAMPLE"):
        # The sample = one ref per deck (archetype coverage) + every PINNED ref. PROMOTE-ON-CATCH:
        # if the OVERNIGHT full sweep ever flags a contract-fail on a ref the sample missed, add that
        # ref's relative path here so regression catches it early. Paths are relative to references/.
        PINNED = set()   # e.g. {"Hinata2/claude_s1_gi0.json"}  -- grows as overnight surfaces gaps
        seen, sampled = set(), []
        for p in refs:
            deck = p.split("/")[1]
            rel = p[len("references/"):]
            if deck not in seen or rel in PINNED:
                seen.add(deck); sampled.append(p)
        refs = sampled
        print(f"[sample mode: {len(refs)} refs (one per deck + {len(PINNED)} pinned) "
              f"-- full sweep runs in overnight]")
    counts = {k: 0 for k in ("ok", "repaired", "play", "shuffle-dead", "unresolvable",
                             "mulligan", "contract")}
    LABEL = {"ok": "ok            ", "repaired": "repaired      ", "play": "play-drift    ",
             "shuffle-dead": "shuffle-dead  ", "unresolvable": "ENUM-GAP      ",
             "mulligan": "mull-drift    "}
    for path in refs:
        c_ok, kind, detail = check_reference(path)
        rel = path[len("references/"):]
        if not c_ok:
            print(f"  CONTRACT-FAIL {rel}: {detail}"); counts["contract"] += 1
        else:
            print(f"  {LABEL[kind]}{rel}: {detail}"); counts[kind] += 1
    print(f"\nViewer protocol: {counts['ok']} ok, {counts['repaired']} repaired, "
          f"{counts['play']} play-drift, {counts['shuffle-dead']} shuffle-dead, "
          f"{counts['unresolvable']} enum-gap, {counts['mulligan']} mull-drift, "
          f"{counts['contract']} contract-fail  ({len(refs)} refs)")
    if counts["repaired"]:
        print("  repaired     = recorded line REPRODUCED after repairing stale indices / answering decision")
        print("                 points the reference predates. Not a regression; re-save to make it permanent")
    if counts["play"]:
        print("  play-drift   = the recorded line replays to a DIFFERENT outcome -> a real behaviour change")
    if counts["shuffle-dead"]:
        print("  shuffle-dead = a mid-game reshuffle moved the draws; the recorded line targets cards no")
        print("                 longer drawn. The accepted class -- restore only by re-playing by hand")
    if counts["unresolvable"]:
        print("  ENUM-GAP     = the hand is IDENTICAL yet a previously-offered plan is no longer enumerated:")
        print("                 the engine changed under the same state. Investigate before re-saving anything")
    if counts["mulligan"]:
        print("  mull-drift   = engine now opens a different hand -> recorded choices don't apply; NOT a play regression")
    if counts["contract"]:
        return 1
    # --strict additionally gates on the categories that indicate the ENGINE moved under a
    # recorded game: play-drift and enum-gap. shuffle-dead and mull-drift are accepted classes
    # the player can't steer, so they never gate.
    return 1 if (STRICT and (counts["play"] or counts["unresolvable"])) else 0


if __name__ == "__main__":
    sys.exit(main())
