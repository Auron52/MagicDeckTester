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
re-anchored by plan CONTENT, decision points a reference predates are answered
from the engine's own heuristic_default, and recorded points the engine no
longer reaches are skipped when the human declined them. This is what keeps the
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

Usage:  python3 test/viewer_protocol_check.py                 # all references, serial
        python3 test/viewer_protocol_check.py --threads 12    # full sweep in seconds
        python3 test/viewer_protocol_check.py --strict        # FAIL on play-drift / enum-gap
        python3 test/viewer_protocol_check.py --sample        # one ref per deck (quick sanity)
        MTG_BIN=path python3 test/viewer_protocol_check.py    # against a specific binary

test/regression.sh runs the full sweep --strict (threaded) in REGRESSION mode -- one mode, not all
three: this is a whole-corpus replay, so re-running it in smoke and overnight re-measured the same
thing (VPC_ALWAYS=1 forces it in any mode, VPC_SKIP=1 skips it).
"""
import json, os, re, subprocess, sys, glob

MTG = os.environ.get("MTG_BIN", "./build/Release/mtg")
STRICT = "--strict" in sys.argv[1:]

# §2a REPLAY COMPAT (MTG_TREASURE_PAY_SOURCE -- lump-mana-sources-as-payment-sources.md §2a, and
# mana-order-and-reserve-overhaul.md). With the flag on, an amount-1 pay-sac source (Treasure
# Token) is a PAYMENT source, not a per-colour SacForMana plan action -- so references recorded
# under the action fan encode plans ("cast: Zada, Treasure Token: sacrifice for {R}x1") and
# re-prompt frames (offered again because crackable Treasures were still actions) that can no
# longer occur verbatim. Two env-gated repairs keep those recordings replayable WITHOUT weakening
# the gate -- the drift test (same won + win_turn as recorded) still decides pass/fail
# (USER 2026-08-25: permission conditional on reproducing the same win turn):
#   1. find_plan tier 3: match a recorded plan MODULO its Treasure-sac actions (the sac is now
#      implicit in payment; the colour choice moves to the payment solver, the same class of
#      information the stale-index repair already re-derives).
#   2. Frame alignment: skip a recorded main-phase frame that (a) answered PASS and (b) sits
#      strictly BEFORE the engine's current frame -- the fan-era re-prompt that no longer fires
#      because there is no crack action left to offer. A stale frame that recorded a REAL cast is
#      never skipped: losing it is genuine content loss and must surface as drift.
# Scope: card name "Treasure Token" -- the only amount-1 pay-sac source (`sac_count` cannot
# distinguish it: Lotus Bloom also records sac_count 1 but is amount 3 and KEEPS its action fold).
_tps = os.environ.get("MTG_TREASURE_PAY_SOURCE")
TREASURE_PAY_COMPAT = bool(_tps) and _tps != "0"
PAY_SAC_NAMES = {"Treasure Token"}

# Per-replay ADDRESS-SPACE cap, in MB (MTG_REPLAY_AS_CAP_MB; 0 disables). A replay is an ordinary
# few-MB run -- the whole 208-reference sweep peaks around 90 MB per process -- but the plan
# enumerator's fan-out is combinatorial in the number of untapped fungible sac sources, and this
# sweep runs `--threads` of them AT ONCE. When one went unbounded (a Mirrorwing board with 9
# Treasures: 10,077,696 materialised plans, see docs/design/claude-play-unprune-blowup.md) at
# --threads $(nproc) it did not fail a reference, it OOM'd the machine and killed the user's
# session. The cap turns that back into one reference reporting a bad_alloc. Applied via `sh -c
# ulimit` rather than subprocess's preexec_fn, which is not safe under a ThreadPoolExecutor.
AS_CAP_MB = int(os.environ.get("MTG_REPLAY_AS_CAP_MB", "4096"))


def capped(args):
    """`args`, wrapped so the child runs under the address-space cap (unwrapped when disabled)."""
    if AS_CAP_MB <= 0:
        return args
    return ["/bin/sh", "-c", f'ulimit -v {AS_CAP_MB * 1024}; exec "$@"', "sh"] + args

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
    "Goblins":       ("decks/Goblins/Goblins.cod",             "decks/Goblins/Goblins.profile.json"),
    # The reference dir is Creature_Giving; the deck folder has a SPACE. That mismatch is why this
    # deck was never added -- and why 40 references (30 Goblins + 10 Creature Giving) sat unchecked.
    "Creature_Giving": ("decks/Creature Giving/Creature Giving.cod",
                        "decks/Creature Giving/Creature Giving.profile.json"),
    "FiveColour":    ("decks/FiveColour/FiveColour.cod",       "decks/FiveColour/FiveColour.profile.json"),
    # Reference dir uses an underscore; the deck folder has a SPACE (same shape as Creature_Giving).
    "Mirrorwing_Dragon": ("decks/Mirrorwing Dragon/v1-twinflame-anger/Mirrorwing Dragon.cod",
                          "decks/Mirrorwing Dragon/v1-twinflame-anger/Mirrorwing Dragon.profile.json"),
    # Added 2026-08-23: f0a6d9e7 committed 4 hand-played StompySurprise references, and an unmapped
    # reference dir is a CONTRACT-FAIL here (not a skip) -- so the whole gate went red the moment
    # they landed. Any new references/<dir> needs a row the same day.
    "StompySurprise": ("decks/StompySurprise/StompySurprise.cod",
                       "decks/StompySurprise/StompySurprise.profile.json"),
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


def recorded_attackers(decisions):
    """turn -> the attacker names the RECORDING's combat declared that turn, parsed from the
    play-viewer combat events ("⚔ attacked: A (2), B (3) — 5 to opponent (20→15)"). The attack
    declaration is engine-automatic and its heuristic reads the spare-mana pools, so mana-model
    work legitimately re-decides WHO attacks -- which can tap a creature the recorded post-combat
    line needed as a mana source and kill the replay (FiveColour s9_gi8: the tap-order bundle sent
    Deathrite Shaman in on T4 and the recorded Cannons+Faeburrow+Oko line became unpayable). The
    events pin the recorded attack, so the replay passes it back via --force-attackers.

    Only turns WITH a combat event are pinned: the event is emitted only when combat dealt damage,
    so "no event" cannot distinguish "did not attack" from "attacked for 0" -- and the final
    lethal swing often has no later frame to carry its event at all. Unpinned turns declare
    naturally, exactly as before."""
    by_turn = {}
    for d in decisions:
        for ev in (d.get("decision", {}).get("events") or []):
            if ev.get("kind") != "combat" or "attacked: " not in ev.get("text", ""):
                continue
            body = ev["text"].split("attacked: ", 1)[1].rsplit(" — ", 1)[0]
            body = re.sub(r" \+ \d+ \(attack triggers\)$", "", body)
            # Attackers are "Name (power)" joined by ", "; split on '), ' because names themselves
            # can carry commas ("Oko, Thief of Crowns (3)"), then strip the power suffix.
            names = [re.sub(r" \(\d+\)?$", "", piece) for piece in body.split("), ")]
            by_turn[ev.get("turn")] = names
    return by_turn


def recorded_tap_prefs(decisions):
    """(turn, phase) -> battlefield idxs the recording's committed plan TAPPED in that main phase,
    read from the tapped-delta between two consecutive same-(turn, phase) main_phase frames (the
    engine re-prompts after committing a line, so the pair brackets exactly that line's payment
    and activations). Which sources pay is engine-automatic, and no static tap order reproduces
    every recording -- FiveColour s13_gi12's T4 spent Deathrite and KEPT Bloom Tender (its
    multi-yield paid the post-combat casts) while s9_gi8's T3 needed Deathrite spared (its
    graveyard fuel paid T4) -- so where the recording witnessed the taps, --tap-pref pins them.
    Restricted to permanents already present in the FIRST frame (a fetched land arriving tapped
    between the frames is an ETB state, not a payment tap). Turns/phases without a pair replay on
    the engine's own order, exactly as before."""
    prefs = {}
    prev = None
    for d in decisions:
        dec = d.get("decision", {})
        if dec.get("type") != "main_phase":
            continue
        if prev is not None and prev.get("turn") == dec.get("turn") \
                and prev.get("phase") == dec.get("phase"):
            def field(fr, want_tapped):
                return {p["idx"] for p in fr.get("me", {}).get("battlefield", [])
                        if isinstance(p.get("idx"), int)
                        and (bool(p.get("tapped")) or not want_tapped)}
            delta = (field(dec, True) - field(prev, True)) & field(prev, False)
            if delta:
                key = (dec.get("turn"), dec.get("phase"))
                prefs[key] = sorted(set(prefs.get(key, [])) | delta)
        prev = dec
    return prefs


def side_channel_args(decisions):
    """Reconstruct the keyed side-channel args a reference used, so a saved reordered/held game replays
    faithfully: --firebreathe "turn:count", --storage-hold "turn:num:val", --cast-order "ord:A|B" (the
    applied cast order recorded on the main-phase entry), --force-attackers "turn:A|B" (the recorded
    combat's attackers -- see recorded_attackers; pins the engine-automatic declaration to the game
    the human actually saw). All keyed (turn / land# / main-ordinal), so passing the full set for
    every prefix is safe -- the engine applies each only when it reaches that turn/ordinal. Empty
    (a reference recording none of these) => no extra args => identical to before."""
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
    fa = recorded_attackers(decisions)
    tp = recorded_tap_prefs(decisions)
    extra = []
    if fb:
        extra += ["--firebreathe", ",".join(fb)]
    if sh:
        extra += ["--storage-hold", ",".join(sh)]
    if co:
        extra += ["--cast-order", ";".join(co)]
    if fa:
        extra += ["--force-attackers",
                  ";".join(f"{t}:" + "|".join(ns) for t, ns in sorted(fa.items()))]
    if tp:
        extra += ["--tap-pref",
                  ";".join(f"{t}:{'post' if ph == 'post_main' else 'pre'}:"
                           + ",".join(str(i) for i in idxs)
                           for (t, ph), idxs in sorted(tp.items()))]
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
    # Uncapped plan list: recorded references index into the FULL enumerated plan list, and the
    # stale-index repair content-matches against the emitted "plans" array -- the viewer's display
    # cap (MTG_PLAY_PLANS_CAP, default 200; ENOBUFS fix) would make any recorded pick beyond the
    # cap look unrepairable and read as play-drift (FiveColour s8 gi7 records index 304).
    env = dict(os.environ, MTG_PLAY_PLANS_CAP="0")
    p = subprocess.run(capped(args), capture_output=True, text=True, env=env)
    return p.returncode, p.stdout + p.stderr


REQUIRED_DECISION_KEYS = {"decision_index", "type", "turn"}


def hand_names(hand):
    return sorted(c.get("name", "") for c in hand)


def frame_ident(d):
    """WHAT a decision frame is, independent of which option was picked. Alignment is done on
    this, never on stream position: position is what made references fragile (a decision point
    added later shifts every downstream pick; see the module docstring).

    `phase` is part of the identity, and load-bearing. Without it a turn's PRE-combat and
    POST-combat main phases share an ident, so an extra pre-combat frame (the engine now re-prompts
    after every committed line rather than ending the phase) aligned with the recorded POST-combat
    pick and tried to play it a phase early -- reported as a bogus ENUM-GAP ("recorded plan 'cast:
    Light Up the Stage' no longer enumerated", nplans 5->2, hand identical) or as play-drift on the
    decks with a real second main. They are different decisions; identity must say so."""
    return (d.get("type"), d.get("turn"), d.get("phase"), d.get("source"))


def plan_key(p):
    """Content identity of a plan, independent of its index: the land played plus the multiset of
    casts. Used as the order-insensitive fallback behind an exact `summary` match."""
    return (p.get("land"), tuple(sorted(p.get("casts") or [])))


def plan_key_sans_pay_sac(p):
    """§2a compat key (see TREASURE_PAY_COMPAT): the recorded plan's identity with its Treasure-sac
    entries removed -- one cast instance per recorded sac ACTION (a plan can crack several). None
    when the plan has no such action (then this tier adds nothing over plan_key)."""
    sacs = [a.get("card") for a in (p.get("actions") or [])
            if a.get("sacout") and a.get("card") in PAY_SAC_NAMES]
    if not sacs:
        return None
    casts = list(p.get("casts") or [])
    for name in sacs:
        if name in casts:
            casts.remove(name)
    return (p.get("land"), tuple(sorted(casts)))


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
    if not hits and TREASURE_PAY_COMPAT:
        # §2a compat tier: the recorded plan's Treasure-sac actions are implicit payment now, so
        # match the plan MINUS them. The sac colour is re-derived by the payment solver, the same
        # way a stale index is re-derived by content.
        want = plan_key_sans_pay_sac(recorded)
        if want is not None:
            hits = [i for i, p in enumerate(plans) if plan_key(p) == want]
    if not hits:
        return None
    if recorded_index is not None and recorded_index in hits:
        return recorded_index
    return hits[0]


# A target label's VOLATILE tail: the printed power/toughness and the "(yours)" marker, which
# restate board facts rather than naming the target. "Goblin Guide (2/2, yours)" and "Goblin Guide
# (yours)" are the same creature; a pump between recordings must not make them different targets.
# "(face)" / "(self)" are NOT stripped -- those distinguish real targets ("You (face)" is a player).
_LABEL_VOLATILE = re.compile(r"\s*\((?:\d+/\d+)?(?:,\s*)?(?:yours)?\)$")


def option_key(o):
    """STABLE content identity of an auxiliary-decision option, or None when the option has no
    order-independent identity. Three shapes are stable across engine evolution:
      * library placements (scry / reorder / surveil): what goes on top, what goes away, shuffle --
        pure card names, deterministic;
      * card picks (discard): the card name;
      * TARGET SETS: the option's `label`, normalised -- "Zada, Hedron Grinder (yours)",
        "Opponent (face) + 1/1 Spirit Token". This one used to be excluded ("no stable content
        key") and replayed by verbatim INDEX, on the reasoning that its `targets` array embeds
        battlefield indexes that legitimately shift. True of `targets`; NOT true of the label,
        which names the creatures. Index-replay means any change to the legal-target LIST silently
        re-points every recorded pick: fixing CollectOwnCreatureTargets to stop dropping tokens
        added one option to Mirrorwing's boards and drifted 8 references to a target the human
        never chose. The label is the recorded intent; positions are not.
    """
    if not isinstance(o, dict):
        return None
    if "top" in o or "away" in o or "shuffle" in o:
        return ("placement", tuple(o.get("top") or []), tuple(o.get("away") or []),
                bool(o.get("shuffle")))
    if "name" in o:
        return ("name", o.get("name"))
    if isinstance(o.get("label"), str) and o["label"]:
        parts = [p.strip() for p in o["label"].split(" + ")]
        return ("label", tuple(_LABEL_VOLATILE.sub("", p) for p in parts))
    return None


def find_option(recorded, options, recorded_index=None):
    """Index of the recorded option in the CURRENT options list, matched by stable CONTENT -- the
    enumeration order of scry/reorder/discard options is not part of the contract, so a verbatim
    index silently performs a different placement when the order changes. Prefers the recorded
    index among equal matches (duplicate contents, e.g. two identical discard names). Returns the
    verbatim index when the option shape has no stable key (see option_key)."""
    if recorded is None:
        return None
    want = option_key(recorded)
    if want is None:
        return recorded_index if (recorded_index is not None
                                  and 0 <= recorded_index < len(options)) else None
    hits = [i for i, o in enumerate(options) if option_key(o) == want]
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


def free_cast_intent(dec, kept, ri):
    """Answer an unaligned `free_cast` frame from the RECORDED INTENT, not from a blind default.

    Maelstrom Archangel's "you may cast a spell without paying its mana cost" used to be spent as a
    `#FREE` variant folded into the main-phase plan list, so a reference saved then recorded the free
    cast as an ordinary `cast: <card>` pick -- indistinguishable from a paid cast (the `(free)` marker
    did not exist yet). Moving it to its own one-time decision inserts a frame those references
    predate, and NO fixed default is right for all of them: seed 6 passed (declining is faithful)
    while seeds 4 and 7 free-cast Unite the Coalition to win a turn earlier (declining costs them
    that turn). So read the reference's own line for the phase this trigger fires in -- the
    post-combat main of the same turn -- and cast whatever it cast, else decline.

    Returns (pick, source) or None when the reference has nothing to say (caller falls back).
    """
    turn = dec.get("turn")
    cands = dec.get("candidates") or []
    if not cands:
        return None
    by_name = {c.get("name"): c.get("index") for c in cands}
    for rec in kept[ri:]:
        rd = rec.get("decision") or {}
        if rd.get("type") != "main_phase" or rd.get("turn") != turn:
            continue
        if rd.get("phase") != "post_main":
            continue
        chosen = rec.get("chosen")
        if not isinstance(chosen, int) or chosen < 0:
            return -1, "recorded-intent(pass)"        # the reference declined this phase
        plan = next((p for p in (rd.get("plans") or []) if p.get("index") == chosen), None)
        if not plan:
            return None
        for nm in (plan.get("casts") or []):
            if nm in by_name:
                return by_name[nm], f"recorded-intent({nm})"
        return -1, "recorded-intent(no free cast)"
    return None


def target_intent(dec, kept, ri):
    """Answer an unaligned `target` frame from the RECORDED INTENT, not from a blind default.

    A solo-target trick's target used to be a PLAN variant ("cast: Scale the Heights → Goblin
    Instigator"), so references saved then recorded it inside the chosen plan's action
    (`enchant_target_name`) and carry no `target` frame at all. It is now asked at RESOLUTION off
    the board -- the plan no longer fans one variant per target -- so those references hit a frame
    they predate, and the engine default aims somewhere else (Mirrorwing s3_gi2: the reference
    pumped Goblin Instigator, the default picks another 1/1 and the win slips a turn). The
    reference does record what it wanted; read it. Same shape as free_cast_intent.

    Returns (pick, source) or None when the reference says nothing (caller falls back).
    """
    src, turn = dec.get("source"), dec.get("turn")
    if not src:
        return None
    for rec in reversed(kept[:ri]):                 # the plan this cast came from, most recent first
        rd = rec.get("decision") or {}
        if rd.get("type") != "main_phase" or rd.get("turn") != turn:
            continue
        chosen = rec.get("chosen")
        if not isinstance(chosen, int) or chosen < 0:
            continue
        plan = next((p for p in (rd.get("plans") or []) if p.get("index") == chosen), None)
        if not plan:
            continue
        want = next((a.get("enchant_target_name") for a in (plan.get("actions") or [])
                     if a.get("card") == src and a.get("enchant_target_name")), None)
        if not want:
            continue
        for o in (dec.get("options") or []):
            if (o.get("label") or "").startswith(want):
                return o.get("index"), f"recorded-intent({want})"
        return None
    return None


def check_reference(path, collect=None):
    """Replay one reference by INTENT, validating the contract at every step.

    collect: optional dict; on return it holds the replay ingredients ('deck', 'prof', 'seed',
    'gi', 'force', 'side', 'mt', 'resolved' = the content-resolved pick stream actually sent).
    scripts/ref_regenerate.py uses this to re-write a repaired reference as a fresh engine trace.

    A reference records each pick as a positional index into that step's plan list, but it also
    records the full description of the plan it chose (summary/land/casts). Indices are fragile --
    they silently re-point whenever the plan set changes or a new decision point is inserted
    upstream -- so this walk aligns on decision IDENTITY and resolves each pick by CONTENT:

      * a frame the reference recorded  -> replay its recorded pick, re-resolved to whatever index
        now carries the same plan summary (an index shift is repaired, not reported as drift),
      * a frame the reference does NOT have (a decision point added after it was saved) -> answer
        from the engine's own heuristic_default/ai_choice and consume NO recorded pick, so the rest
        of the recorded line stays aligned,
      * a frame the reference HAS that the engine no longer emits (a decision point removed after
        it was saved) -> skip it, provided the human declined it, so the rest of the recorded line
        stays aligned. Insertion and deletion are the same problem from opposite ends; handling
        only one of them strands every pick after the first removed frame.

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
        # LOUD, not `ok`. This used to return ok/"skip", so a reference dir missing from DECKS was
        # counted as verified while never being replayed at all -- 30 Goblins + 10 Creature Giving
        # references sat in that blind spot, including the deck a 13-issue viewer batch was built
        # against. A reference we cannot resolve a deck for is UNVERIFIED, and must say so.
        return False, "play", (f"unknown deck dir {deck_dir!r} -- NOT replayed; add it to DECKS "
                               f"in this file (note references/<dir> need not match decks/<dir>)")
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
    # Aligned MAIN-PHASE frames, in walk order: where each one sits in `resolved`. This is the
    # alignment that viewer_validate_check.js needs and used to approximate on its own -- it lets
    # that check replay the exact prefix that lands the engine on a given recorded frame, instead
    # of assuming the recording's positional indices still line up. Sharing this walk is the point:
    # two independent alignment implementations is how the validate check silently drifted to 141
    # stale failures (docs/design/viewer-validate-stream-alignment.md).
    frames = []
    if collect is not None:
        collect.update(deck=deck, prof=prof, seed=seed, gi=gi, force=force, side=side, mt=mt,
                       resolved=resolved,   # 'resolved' is THIS list; it fills in as the walk runs
                       frames=frames)
    ri = 0               # how many of the reference's own decisions have been consumed
    inserted, shifted, vanished, skipped = [], [], [], []
    hand_checked = False

    def stale_pass_frame(entry, dec):
        """§2a compat (TREASURE_PAY_COMPAT): is this recorded frame a PASS on a main-phase decision
        that sits strictly BEFORE the engine's current frame -- i.e. a fan-era re-prompt that can
        no longer occur (no crack action left to offer), safe to skip because passing it changed
        nothing? A stale frame with a REAL recorded cast is never skipped."""
        d = entry.get("decision") or {}
        if d.get("type") != "main_phase":
            return False
        rec = entry.get("chosen")
        rec0 = rec[0] if isinstance(rec, list) and rec else rec
        try:
            if int(rec0) != -1:
                return False
        except (TypeError, ValueError):
            return False
        rank = {"pre_main": 0, "post_main": 1}
        dt, ct = d.get("turn"), dec.get("turn")
        if not isinstance(dt, int) or not isinstance(ct, int):
            return False
        if dt < ct:
            return True
        return (dt == ct and d.get("phase") in rank and dec.get("phase") in rank
                and rank[d.get("phase")] < rank[dec.get("phase")])
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
            if vanished:
                repairs.append(f"{len(vanished)} declined decision(s) the engine no longer offers "
                               f"skipped (e.g. {vanished[0]})")
            if skipped:
                repairs.append(f"{len(skipped)} recorded pass-frame(s) collapsed by §2a skipped "
                               f"(e.g. {skipped[0]})")
            if ri < len(kept):
                repairs.append(f"terminated with {len(kept) - ri}/{len(kept)} recorded decisions unused")
            det = (f"replay won={res.get('won')} win_turn={res.get('win_turn')} "
                   f"vs ref won={ref.get('won')} win_turn={ref.get('win_turn')}")
            if repairs:
                det += "; " + "; ".join(repairs)
            if drift:
                return True, "play", det
            return True, ("repaired" if (shifted or inserted or vanished or skipped) else "ok"), det
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
        if not aligned and ri < len(kept):
            # DELETION, the mirror of the insertion case below. A frame the reference recorded may
            # simply STOP OCCURRING -- an engine fix can remove the only option a re-prompt existed
            # to offer, and then there is nothing to prompt for. Aligning strictly left-to-right
            # cannot express that: the walk holds `ri` on the vanished frame, mis-reads the NEXT
            # real frame as an insertion, passes it, and every later recorded pick is stranded.
            # That is not drift, it is the checker losing the thread -- and it cost a user their
            # whole T5 line (Dragonstorm s26_gi25: the two T4 re-prompts whose entire plan list was
            # the illegal `Lotus Bloom (other)` suspend of an Apex-staged card, removed with that
            # bug, made the reference read as a T5->T6 regression the engine never had).
            #
            # So skip forward to the next recorded frame with this identity -- but ONLY over frames
            # the human DECLINED. A pass carries no intent, so dropping it is a no-op; a recorded
            # frame with a real pick is user work, and silently stepping over it would fabricate a
            # line the human never played. That stays loud (falls through to the insertion branch,
            # and the stranded picks surface as drift), which is the whole point: repair what is
            # provably lossless, report what is not.
            def _declined(rec):
                return (rec["decision"].get("type") == "main_phase" and rec.get("chosen") == -1)
            j = next((k for k in range(ri + 1, len(kept))
                      if frame_ident(kept[k]["decision"]) == frame_ident(dec)), None)
            if j is not None and all(_declined(kept[k]) for k in range(ri, j)):
                for k in range(ri, j):
                    vanished.append(f"{frame_ident(kept[k]['decision'])}(declined)")
                ri = j
                aligned = True
        if not aligned and TREASURE_PAY_COMPAT:
            # §2a compat: consume recorded pass-frames the engine can no longer present (the
            # fan-era re-prompts) so the alignment pointer does not jam and default every
            # subsequent real decision. See stale_pass_frame for the (deliberately narrow) test.
            while ri < len(kept) and stale_pass_frame(kept[ri], dec):
                skipped.append(f"{frame_ident(kept[ri]['decision'])}")
                ri += 1
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
            elif dec.get("type") == "free_cast":
                # A "may" trigger the reference predates: replay what its own line did (see
                # free_cast_intent). Falls back to the engine default (decline) when the reference
                # says nothing about that phase.
                intent = free_cast_intent(dec, kept, ri)
                pick, src = intent if intent else engine_default(dec)
            elif dec.get("type") == "target":
                # A trick target that used to ride the PLAN and is now asked at resolution: aim it
                # where the reference's own plan aimed it (see target_intent).
                intent = target_intent(dec, kept, ri)
                pick, src = intent if intent else engine_default(dec)
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
            # Record the prefix BEFORE this frame's own pick: replaying resolved[:prefix_len]
            # leaves the engine offering exactly this frame.
            frames.append({"turn": dec.get("turn"), "phase": dec.get("phase"),
                           "prefix_len": len(resolved), "ri": ri, "recorded_index": p})
            if p == -1:                      # pass / cast-nothing is always legal
                resolved.append(-1)
            else:
                rplans = rd.get("plans") or []
                # Anchor by each stored plan's own "index" FIELD, not array position: the viewer's
                # display cap (MTG_PLAY_PLANS_CAP) can save a TRUNCATED list while the recorded
                # pick keeps its REAL engine index (a hand-assembled line validated engine-side can
                # sit beyond the cap -- Mirrorwing s7_gi6 picks 223 of 412 with 200 saved), and a
                # repaired reference may carry that one beyond-cap plan appended. Positional lookup
                # is the fallback for pre-"index"-field references only.
                recorded = next((pp for pp in rplans if pp.get("index") == p), None)
                if recorded is None and rplans and "index" not in rplans[0]:
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
                    # BOARD-AWARE classification (2026-08-25): "same state" cannot be asserted
                    # from hand equality alone -- an upstream payment that TAPPED differently
                    # (exactly what the mana-order overhaul changes) leaves an identical hand on
                    # a different board, where the recorded plan is legitimately unofferable
                    # (StompySurprise s11_gi10: the Lodge-untap plan gone because the Lodge is
                    # already tapped). That is the shuffle-dead shape, not an enumeration gap:
                    # only re-playing can restore the game. The gating ENUM-GAP class is reserved
                    # for a truly identical visible state.
                    def board_key(state):
                        return sorted((pp.get("name"), bool(pp.get("tapped")))
                                      for pp in state.get("me", {}).get("battlefield", []))
                    if board_key(dec) != board_key(rd):
                        return True, "shuffle-dead", (
                            f"{where}; hand identical but the BOARD differs (tap state / "
                            f"permanents) -> an upstream payment or decision diverged the game; "
                            f"only re-playing can restore this reference")
                    if TREASURE_PAY_COMPAT and (inserted or skipped):
                        # §2a compat: this replay is already LOOSE (defaults / collapsed frames
                        # upstream -- hidden state may have diverged invisibly). The pre-compat
                        # walk skated past such frames by defaulting them; hard-failing here
                        # would make the compat STRICTER than baseline on old references. Answer
                        # the engine default (pass) and let the walk complete -- the drift test
                        # on the final outcome (same won + win_turn) remains the arbiter.
                        inserted.append(f"{frame_ident(dec)}<--1(pass; recorded plan "
                                        f"unmatchable in loose replay)")
                        resolved.append(-1)
                        continue
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
            # Auxiliary decision (scry / reorder / discard / target / ...). These carry an
            # `options` list whose ENUMERATION ORDER is not part of the contract -- a reference
            # stores each option's full content, so re-anchor by content exactly like plans
            # (verbatim index only when either side lacks options). Hinata2 s12_gi11's T5
            # Preordain scry died on exactly this: pick 1 verbatim placed a different card on top
            # after the option order changed, and the drawn card diverged with NO shuffle involved.
            cur_opts = dec.get("options")
            ref_opts = rd.get("options")
            if isinstance(cur_opts, list) and isinstance(ref_opts, list) and cur_opts and ref_opts:
                for x in rec_list:
                    x = int(x)
                    rec_opt = ref_opts[x] if 0 <= x < len(ref_opts) else None
                    q = find_option(rec_opt, cur_opts, recorded_index=x)
                    if q is None:
                        cur_hand = hand_names(dec.get("me", {}).get("hand", []))
                        ref_hand = hand_names(rd.get("me", {}).get("hand", []))
                        where = (f"recorded option {option_key(rec_opt)!r} no longer offered at "
                                 f"{frame_ident(rd)} (noptions {len(ref_opts)}->{len(cur_opts)})")
                        if ref_hand and cur_hand != ref_hand:
                            return True, "shuffle-dead", (
                                f"{where}; hand differs -> a mid-game reshuffle moved the draws; "
                                f"only re-playing can restore this game")
                        return True, "unresolvable", (
                            f"{where}; the option content (looked cards / targets) is no longer "
                            f"available -> upstream hidden state diverged")
                    if q != x:
                        shifted.append(f"{frame_ident(dec)} {x}->{q}")
                    resolved.append(q)
            else:
                resolved += [int(x) for x in rec_list]
        ri += 1
    return True, "unresolvable", f"replay did not terminate within 400 decisions ({len(resolved)} picks sent)"


def emit_resolved(paths):
    """--emit-resolved <ref.json>...: print this walk's ALIGNMENT as JSON, one object per line.

    Consumed by test/viewer_validate_check.js so that check replays the same content-resolved
    stream this one does rather than the recording's raw positional indices. Fields: the replay
    invariants (deck/prof/seed/gi/force/side/mt), `resolved` (the full pick stream), and `frames`
    (each aligned main_phase frame's turn/phase and its prefix_len into `resolved`).
    """
    for path in paths:
        c = {}
        ok, kind, detail = check_reference(path, collect=c)
        print(json.dumps({"path": path, "ok": ok, "kind": kind, "detail": detail,
                          "deck": c.get("deck"), "prof": c.get("prof"), "seed": c.get("seed"),
                          "gi": c.get("gi"), "force": c.get("force"), "side": c.get("side", []),
                          "mt": c.get("mt"), "resolved": c.get("resolved", []),
                          "frames": c.get("frames", [])}))
    return 0


def main():
    if "--emit-resolved" in sys.argv[1:]:
        i = sys.argv.index("--emit-resolved")
        return emit_resolved([a for a in sys.argv[i + 1:] if not a.startswith("--")])
    # The one-level glob deliberately covers only the VERIFIED set, references/<deck>/claude_*.json.
    # Aspirational "known-slow" games live one level deeper (references/suboptimal/<deck>/…, see that
    # folder's README) and are excluded here: their win turn is knowingly beatable, so gating on them
    # would report permanent drift. Guard against a future deeper glob too.
    refs = sorted(p for p in glob.glob("references/*/claude_s*_gi*.json")
                  if not p.startswith(("references/suboptimal/", "references/optimal/")))
    if not refs:
        print("no reference games found under references/")
        return 0
    # SAMPLE mode (--sample / VIEWER_PROTOCOL_SAMPLE): one reference per deck dir. Historical: the
    # serial per-step replay was multi-minute, so regression sampled. With --threads the FULL sweep
    # is seconds and test/regression.sh runs it strict in regression mode; sample mode remains for
    # the quickest possible ad-hoc sanity. Picks the first ref per deck for determinism.
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
    # --threads N: references are independent (each is its own chain of engine spawns), so the
    # sweep parallelises trivially. Results are collected and printed in ref order, so output and
    # exit code are identical at any thread count. Default 1 preserves the historical behaviour;
    # the regression harness passes its own thread count to fit the full sweep in its budget.
    threads = 1
    for i, a in enumerate(sys.argv[1:]):
        if a == "--threads" and i + 2 <= len(sys.argv[1:]):
            threads = max(1, int(sys.argv[1:][i + 1]))
        elif a.startswith("--threads="):
            threads = max(1, int(a.split("=", 1)[1]))
    counts = {k: 0 for k in ("ok", "repaired", "play", "shuffle-dead", "unresolvable",
                             "mulligan", "contract")}
    LABEL = {"ok": "ok            ", "repaired": "repaired      ", "play": "play-drift    ",
             "shuffle-dead": "shuffle-dead  ", "unresolvable": "ENUM-GAP      ",
             "mulligan": "mull-drift    "}
    if threads > 1:
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(max_workers=threads) as ex:
            results = list(ex.map(check_reference, refs))
    else:
        results = [check_reference(p) for p in refs]
    for path, (c_ok, kind, detail) in zip(refs, results):
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
