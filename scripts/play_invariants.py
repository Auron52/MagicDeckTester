#!/usr/bin/env python3
"""Deterministic claude-play INVARIANT sweep (workstream 4a, the mechanical half).

Drives the `--claude-play` stateless-replay protocol auto-following the engine's own
exposed default at every decision (a develop-greedy policy at `main_phase`, where the
protocol deliberately exposes no engine plan-pick), and asserts protocol/engine
invariants the autonomous smoke can't -- WITHOUT needing Claude to play well:

  HARD (blocking):
    - determinism   : same (deck, seed, gi, CSV) -> byte-identical decision block
                      (the property analyze-deck Stage 5d and the Workflow sweep rely on)
    - integrity     : every emitted block is valid JSON with a KNOWN `type`; main_phase
                      plan indices are contiguous 0..n-1 (strictly increasing when the menu is
                      truncated -- plans_total > emitted, a diversity-sampled subset); exit 70 (a
                      decision) until a single exit-0 `<<<CLAUDE_RESULT>>>`; no crash
    - progress      : decision_index strictly increases, turn is non-decreasing, and the
                      game reaches a RESULT within a decision cap (no infinite exit-70 loop)

  ADVISORY (reported, never blocks -- mana math / hidden state can't be verified from the
  protocol, and the skill warns false positives are the main failure mode):
    - a main_phase plan whose `casts` name a card not visible in hand / retrace_gy (could
      be a legitimate cascade / Aether-Vial deploy / staged-from-exile line) -> uncertain

This is NOT a play-quality judge -- that is the Claude judgment sweep (workstream 4b).
It is a regression guard for the oracle machinery + the engine's decision emission.

Usage:
    python scripts/play_invariants.py <deck.txt> --profile <deck.profile.json> \
        --seeds 8001 --games 4 [--max-turns 8] [--reveal 6] [--json]
Exit 1 on any HARD invariant violation (or harness/binary error).
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "build/Release/mtg"
if not BIN.exists():
    BIN = ROOT / "build/Release/mtg.exe"

DECISION_CAP = 400            # a >8-turn game never needs this many decisions; a runaway does
KNOWN_TYPES = {
    "main_phase", "mulligan", "bottom", "vial_charge",
    "scry", "surveil", "reorder", "divide", "target", "bounce",
    "dig", "discard", "expressive_iteration", "retrace_discard", "replicate", "land_entry",
    # sac-land additional cost (Shard Volley / Crop Rotation): single-int, engine default exposed.
    "sacrifice",
    # multi-consume put overrides (one 0/1 int per candidate): Dragonstorm put + Defense of the
    # Heart upkeep sac-tutor. Replied via the candidates' `def` flags (the AI's subset).
    "dragon", "sac_tutor",
}
D_START, D_END = "<<<CLAUDE_DECISION>>>", "<<<END_DECISION>>>"
R_START, R_END = "<<<CLAUDE_RESULT>>>", "<<<END_RESULT>>>"


def invoke(deck, profile, seed, gi, max_turns, reveal, choices):
    """One protocol step. Returns (rc, stdout). choices is a list[int]."""
    cmd = [str(BIN), str(deck), "--claude-play", "--seed", str(seed),
           "--game-index", str(gi), "--max-turns", str(max_turns), "--reveal", str(reveal)]
    if profile:
        cmd += ["--profile", str(profile)]
    cmd += ["--choices", ",".join(str(c) for c in choices)]
    # No timeout (CLAUDE.md): a single claude-play step on a hard board can legitimately
    # run for minutes, and killing it surfaces as an ERROR that reads like an engine defect.
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stdout


def extract(out):
    """Return ('decision', obj, raw) | ('result', obj, raw) | ('none', None, '')."""
    if D_START in out and D_END in out:
        raw = out[out.index(D_START) + len(D_START):out.index(D_END)].strip()
        return "decision", json.loads(raw), raw
    if R_START in out and R_END in out:
        raw = out[out.index(R_START) + len(R_START):out.index(R_END)].strip()
        try:
            obj = json.loads(raw)
        except json.JSONDecodeError:
            obj = {"raw": raw}
        return "result", obj, raw
    return "none", None, ""


def develop_index(dec):
    """main_phase policy: play a land and cast as much as possible (deterministic).
    Prefer a plan that plays a land, then the most casts, tie-break lowest index. -1 = pass."""
    plans = dec.get("plans", [])
    if not plans:
        return -1
    def key(p):
        return (1 if p.get("land") else 0, len(p.get("casts", [])), -p.get("index", 0))
    return max(plans, key=key).get("index", 0)


def reply_for(dec):
    """Ints to append to the choice stream for this decision (1 for most; `need` for divide)."""
    t = dec.get("type")
    if t == "main_phase":
        return [develop_index(dec)]
    if t == "mulligan":
        return [int(dec.get("ai_choice", 1))]
    if t == "bottom":
        ai = dec.get("ai_choice", {})
        return [int(ai.get("index", 0) if isinstance(ai, dict) else ai)]
    if t == "vial_charge":
        return [int(dec.get("heuristic_default", 0))]
    if t in ("dragon", "sac_tutor"):
        # Multi-consume put override: ONE 0/1 int per candidate (in candidate order), following
        # the AI's default subset (each candidate's `def` flag). Like `divide`, the engine reads
        # len(candidates) ints from the stream, so supplying them all at once is required.
        cands = dec.get("candidates", [])
        return [1 if c.get("def") else 0 for c in cands] if cands else [0]
    if t == "divide":
        # One int per legal target (in order), each = its engine default (all-to-face).
        # This is the ONLY multi-consume decision: it reads `need` ints from the stream,
        # so supplying fewer makes the engine correctly re-emit the same decision.
        legal = dec.get("legal_targets", [])
        return [int(x.get("default", 0)) for x in legal] if legal else [0]
    # generic single-int sub-decisions: follow the engine's exposed default.
    for k in ("heuristic_default", "heur_option", "default", "def_index"):
        if isinstance(dec.get(k), int):
            return [dec[k]]
    ai = dec.get("ai_choice")
    if isinstance(ai, int):
        return [ai]
    if isinstance(ai, dict) and isinstance(ai.get("index"), int):
        return [ai["index"]]
    return [0]  # safe fallback: first option


def cast_availability_advisory(dec):
    """ADVISORY only: a main_phase plan casting a card not visible in hand/retrace_gy.
    Not a hard flag -- cascade / Vial deploy / staged-from-exile legitimately do this."""
    me = dec.get("me", {})
    hand = {c.get("name") for c in me.get("hand", [])}
    yard = set(me.get("retrace_gy", []))
    # Battlefield-permanent ABILITY activations (sac outlets, Call of the Wild's reveal-top,
    # Wirewood Lodge's untap) ride the casts list under the SOURCE permanent's name -- visible on
    # the battlefield, never in hand. Same rationale as the Token skip below: activation noise,
    # not a hand-desync signal.
    bf = {c.get("name") for c in dec.get("battlefield", me.get("battlefield", []))
          if isinstance(c, dict)}
    notes = []
    for p in dec.get("plans", []):
        for c in p.get("casts", []):
            # Sac-outlet activations ride the casts list (references match on that multiset) and
            # name a battlefield PERMANENT; a token source ("Treasure Token") is never hand-castable
            # by definition, so it is noise for this advisory, not a hand-desync signal.
            if c.endswith(" Token"):
                continue
            if c in bf:
                continue
            if c not in hand and c not in yard:
                notes.append(f"turn {dec.get('turn')} plan {p.get('index')}: casts '{c}' "
                             f"not in hand/retrace_gy (cascade/vial/staged? -> verify)")
    return notes


def drive_game(deck, profile, seed, gi, max_turns, reveal, determinism_samples=3):
    """Drive one game to completion. Returns {seed, gi, decisions, result, flags, advisories}."""
    flags, advisories = [], []
    choices = []
    transcript = []          # (prefix_choices_tuple, raw_block) for determinism re-checks
    last_di, last_turn = -1, -1
    result = None

    for step in range(DECISION_CAP + 1):
        rc, out = invoke(deck, profile, seed, gi, max_turns, reveal, choices)
        kind, obj, raw = extract(out)

        if kind == "result":
            if rc != 0:
                flags.append(f"result block but exit={rc} (expected 0)")
            result = obj
            break
        if kind == "none":
            flags.append(f"no decision/result block at step {step} (exit={rc}); stdout head={out[:200]!r}")
            break
        # a decision:
        if rc != 70:
            flags.append(f"decision block but exit={rc} (expected 70) at step {step}")
        t = obj.get("type")
        if t not in KNOWN_TYPES:
            flags.append(f"unknown decision type {t!r} at step {step} (unwired in play_invariants?)")
            break
        di = obj.get("decision_index", last_di)
        if isinstance(di, int):
            if di < last_di:
                flags.append(f"decision_index went BACKWARDS: {di} after {last_di}")
            elif di == last_di and step > 0:
                # Same decision re-emitted: the engine is correctly asking for more input
                # for a multi-consume decision the driver under-supplied (coverage gap),
                # NOT an engine bug -> advisory, and the loop will supply the rest.
                advisories.append(f"driver under-supplied a {t} decision at di {di} "
                                  f"(multi-consume coverage gap; re-emitted)")
            last_di = di
        turn = obj.get("turn", last_turn)
        if isinstance(turn, int) and turn < last_turn:
            flags.append(f"turn went backwards: {turn} after {last_turn}")
        last_turn = turn if isinstance(turn, int) else last_turn
        if t == "main_phase":
            plans = obj.get("plans", [])
            # A TRUNCATED menu (plans_total > emitted) is a diversity-sampled SUBSET of the full
            # plan list -- one representative per distinct cast set first -- so its indices are
            # strictly increasing but legitimately non-contiguous. An untruncated menu must still
            # be exactly 0..n-1.
            truncated = isinstance(obj.get("plans_total"), int) and obj["plans_total"] > len(plans)
            prev_idx = -1
            for i, p in enumerate(plans):
                idx = p.get("index")
                bad = (not isinstance(idx, int)) or (truncated and idx <= prev_idx)                       or (not truncated and idx != i)
                if bad:
                    flags.append(f"plan indices not {'increasing' if truncated else 'contiguous'} "
                                 f"at turn {obj.get('turn')}: index {idx} at position {i}")
                    break
                prev_idx = idx if isinstance(idx, int) else prev_idx
            advisories += cast_availability_advisory(obj)

        transcript.append((tuple(choices), raw))
        choices += reply_for(obj)
    else:
        flags.append(f"did not reach a result within {DECISION_CAP} decisions (runaway?)")

    # Determinism: re-invoke a sample of prefixes and assert byte-identical decision blocks.
    if transcript:
        n = len(transcript)
        idxs = sorted(set([0, n // 2, n - 1]))
        for i in idxs:
            pref, raw = transcript[i]
            _, out2 = invoke(deck, profile, seed, gi, max_turns, reveal, list(pref))
            k2, _, raw2 = extract(out2)
            if k2 != "decision" or raw2 != raw:
                flags.append(f"NON-DETERMINISTIC replay at prefix#{i} (len {len(pref)}): "
                             f"block differs on re-invocation")

    return {"seed": seed, "gi": gi, "decisions": len(transcript),
            "result": result, "flags": flags, "advisories": advisories}


def run_sweep(deck, profile, seeds, games, max_turns, reveal):
    games_out = []
    for seed in seeds:
        for gi in range(games):
            games_out.append(drive_game(deck, profile, seed, gi, max_turns, reveal))
    hard = [(g["seed"], g["gi"], f) for g in games_out for f in g["flags"]]
    adv = [(g["seed"], g["gi"], a) for g in games_out for a in g["advisories"]]
    total_dec = sum(g["decisions"] for g in games_out)
    return {"ok": not hard, "games": len(games_out), "decisions": total_dec,
            "hard": hard, "advisories": adv, "per_game": games_out}


def main():
    ap = argparse.ArgumentParser(description="Deterministic claude-play invariant sweep.")
    ap.add_argument("deck")
    ap.add_argument("--profile", default=None)
    ap.add_argument("--seeds", default="8001", help="comma-separated base seeds")
    ap.add_argument("--games", type=int, default=4, help="game-indices 0..games-1 per seed")
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--reveal", type=int, default=6)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    if not BIN.exists():
        print(f"FAIL: claude-play binary not built at {BIN}", file=sys.stderr)
        return 1
    seeds = [int(s) for s in args.seeds.split(",") if s.strip()]
    res = run_sweep(args.deck, args.profile, seeds, args.games, args.max_turns, args.reveal)

    if args.json:
        print(json.dumps({k: res[k] for k in ("ok", "games", "decisions", "hard", "advisories")}, indent=2))
        return 0 if res["ok"] else 1

    print(f"Drove {res['games']} game(s), {res['decisions']} decisions "
          f"({len(seeds)} seed(s) x {args.games} gi).")
    if res["hard"]:
        print(f"\nHARD INVARIANT VIOLATIONS ({len(res['hard'])}):")
        for s, gi, f in res["hard"]:
            print(f"  seed {s} gi {gi}: {f}")
    else:
        print("All hard invariants hold (determinism, integrity, progress).")
    if res["advisories"]:
        print(f"\nADVISORIES ({len(res['advisories'])}) -- verify (cascade/vial/staged are legitimate):")
        for s, gi, a in res["advisories"][:20]:
            print(f"  seed {s} gi {gi}: {a}")
        if len(res["advisories"]) > 20:
            print(f"  ... and {len(res['advisories']) - 20} more")
    return 0 if res["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
