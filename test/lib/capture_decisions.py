#!/usr/bin/env python3
"""Capture the RAW decision-JSON bytes this binary emits, for every reference and every decision.

    python3 test/lib/capture_decisions.py <out-file> [--jobs N] [--limit N] [--no-sweep]

WHY. The `--claude-play` decision JSON is a wire protocol: the play viewer parses it, and
test/viewer_protocol_check.py replays 138 saved references against it anchored on
`(kind, index, source)`. Renaming a field, reordering keys, or changing a `source` string has
already turned a recorded turn-8 win into a loss (docs/design/rollout-executor-lockstep.md §4).

So any refactor of the emitters in src/main.cpp needs a stronger check than "the suite passes":
capture every frame's bytes before, capture after, and diff. That is what this does.

It reuses viewer_protocol_check.py's own choice-flattening and replay helpers rather than
re-deriving them -- the side-channel and forced-mulligan rules are subtle, and getting them wrong
would silently capture a different game and make the diff meaningless.

Two passes, because reference replay alone does not reach every emitter:

  1. REFERENCE REPLAY -- every saved reference, every prefix. Under --force-mulligan the engine
     resolves keep/bottom internally, so a second unforced pass covers those two emitters.
  2. SELF-DRIVING SWEEP -- play decks forward taking the engine's OWN default at each decision
     (`heuristic_default`, else `ai_choice`). This reaches the emitters no saved reference
     exercises: echo, lackey_put, expressive_iteration, retrace_discard, firebreathe,
     storage_hold. Without it a refactor could break those six silently.

Output: one record per frame, sorted for a stable diff:

    ### <deck>/<ref> | prefix=<n>
    <the raw JSON object, byte for byte as emitted>
"""
import sys, os, json, glob, subprocess, concurrent.futures

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import viewer_protocol_check as V  # noqa: E402

OUT = None
JOBS = os.cpu_count() or 4
LIMIT = None
SWEEP = True
args = sys.argv[1:]
i = 0
while i < len(args):
    a = args[i]
    if a == "--jobs":
        JOBS = int(args[i + 1]); i += 2
    elif a == "--limit":
        LIMIT = int(args[i + 1]); i += 2
    elif a == "--no-sweep":
        SWEEP = False; i += 1
    else:
        OUT = a; i += 1
if not OUT:
    print(__doc__); sys.exit(2)


# Goblins has no saved references yet but is the only deck with Goblin Lackey, so it is swept
# from its deck file directly rather than via V.DECKS.
SWEEP_DECKS = dict(V.DECKS)
SWEEP_DECKS.setdefault("Goblins", ("decks/Goblins/Goblins.cod", "decks/Goblins/Goblins.profile.json"))
SWEEP_DECKS = {k: v for k, v in SWEEP_DECKS.items() if os.path.exists(v[0]) and os.path.exists(v[1])}


def _replay_prefixes(deck, prof, seed, gi, choices, force, extra, max_turns):
    """Capture the frame at every prefix length of a known choice stream."""
    out = []
    for n in range(len(choices) + 1):
        rc, txt = V.replay(deck, prof, seed, gi, choices[:n],
                           force=force, extra=extra, max_turns=max_turns)
        if rc != 70:
            break                      # game ended (rc 0) or errored -- no more frames
        m = V.DEC_RE.search(txt)
        if not m:
            break
        out.append((n, m.group(1)))
    return out


def frames_for(path_and_forced):
    """Replay one reference prefix by prefix, returning (ident, [(prefix, raw_json), ...]).

    A frame is captured at every prefix length, so a change to ANY emitter on ANY decision of
    ANY reference shows up -- not just the ones a passing suite happens to exercise.

    forced=False re-runs WITHOUT --force-mulligan. Under force the engine resolves keep/bottom
    internally and never calls the external chooser, so those two emitters are invisible to the
    forced pass. The unforced replay diverges from the recorded game once the live mulligan
    differs -- that is fine and even useful here: we are capturing emitter BYTES, not asserting
    the reference reproduces (viewer_protocol_check.py is what asserts that).
    """
    path, forced = path_and_forced
    deck_dir = os.path.basename(os.path.dirname(path))
    if deck_dir not in V.DECKS:
        return "", []
    deck, prof = V.DECKS[deck_dir]
    ref = json.load(open(path))
    decisions = ref.get("decisions", [])
    force = V.force_arg(ref) if forced else None
    choices = V.flatten_choices(decisions, drop_mulligan=force is not None)
    extra = V.side_channel_args(decisions)
    mt = max(8, int(ref.get("win_turn") or 0))
    tag = "" if forced else " (unforced)"
    ident = f"{deck_dir}/{os.path.basename(path)}{tag}"
    return ident, _replay_prefixes(deck, prof, ref["seed"], ref["game_index"],
                                   choices, force, extra, mt)


def _default_pick(frame):
    """The engine's own default for a frame: heuristic_default, else ai_choice.

    Both can be a scalar or an object/list (a multi-pick decision encodes a set). The --choices
    stream is flat, so a list contributes its picks in order -- same rule as flatten_choices.
    """
    for key in ("heuristic_default", "ai_choice"):
        if key not in frame:
            continue
        v = frame[key]
        if isinstance(v, bool):
            return [1 if v else 0]
        if isinstance(v, (int, float)):
            return [int(v)]
        if isinstance(v, dict) and "index" in v:
            return [int(v["index"])]
        if isinstance(v, list):
            return [int(x) for x in v]
    # main_phase carries no default field -- it offers a `plans` list and the reply is a plan
    # index. Take plan 0 (the enumerator's first, which is what a "just keep going" driver wants);
    # this is a COVERAGE driver, not a play-quality claim.
    if frame.get("type") == "main_phase" and frame.get("plans"):
        return [0]
    return None


def sweep_frames(job):
    """Self-drive one (deck, seed, gi) forward, always taking the engine's own default.

    This is what reaches the emitters no saved reference exercises. It cannot desync: each step
    re-reads the frame the engine just produced and answers it, so the choice stream is by
    construction the one the engine asked for.

    `prompt` adds --firebreathe-prompt / --storage-hold-prompt. Those two decisions ride a KEYED
    side channel and are answered silently (greedy default) unless prompting is on, so without
    this their emitters are never reached at all.
    """
    deck_dir, seed, gi, prompt = job
    deck, prof = SWEEP_DECKS[deck_dir]
    extra = ["--firebreathe-prompt", "--storage-hold-prompt"] if prompt else None
    choices, out = [], []
    for step in range(400):            # generous cap; a game is far shorter
        rc, txt = V.replay(deck, prof, seed, gi, choices, force=None, extra=extra, max_turns=9)
        if rc != 70:
            break
        m = V.DEC_RE.search(txt)
        if not m:
            break
        out.append((step, m.group(1)))
        try:
            pick = _default_pick(json.loads(m.group(1)))
        except Exception:
            break
        if pick is None:
            break                      # no default to echo -- stop rather than guess
        choices += pick
    return f"sweep{'+prompt' if prompt else ''}:{deck_dir}/s{seed}_gi{gi}", out


refs = sorted(glob.glob("references/*/claude_*.json"))
if LIMIT:
    refs = refs[:LIMIT]

# Forced replay (the faithful one) + unforced (reaches the mulligan/bottom emitters).
jobs = [(r, True) for r in refs] + [(r, False) for r in refs]

# Self-driving sweeps over every deck the protocol check knows, on seeds chosen to hit the
# archetype-specific emitters (Goblins lackey_put, Hinata expressive_iteration, Dragonstorm
# storage_hold/firebreathe, TH retrace_discard). Several seeds each, since a given emitter only
# fires on the games that draw the card.
sweeps  = [(d, s, s - 1, False) for d in sorted(SWEEP_DECKS) for s in range(1, 25)]
sweeps += [(d, s, s - 1, True)  for d in sorted(SWEEP_DECKS) for s in range(1, 13)]

records = []
with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as ex:
    for ident, frames in ex.map(frames_for, jobs):
        for n, raw in frames:
            records.append((ident, n, raw))
    if SWEEP:
        for ident, frames in ex.map(sweep_frames, sweeps):
            for n, raw in frames:
                records.append((ident, n, raw))

records.sort(key=lambda r: (r[0], r[1]))
with open(OUT, "w", encoding="utf-8") as f:
    for ident, n, raw in records:
        f.write(f"### {ident} | prefix={n}\n{raw}\n")

types = {}
for _, _, raw in records:
    try:
        t = json.loads(raw).get("type", "?")
    except Exception:
        t = "<unparseable>"
    types[t] = types.get(t, 0) + 1
print(f"captured {len(records)} frames ({len(refs)} refs x2 passes"
      + (f" + {len(sweeps)} sweeps" if SWEEP else "") + f") -> {OUT}")
print("decision types covered: " + ", ".join(f"{t}={n}" for t, n in sorted(types.items())))

# Every emitter in src/main.cpp should appear. A type at 0 means this capture would NOT catch a
# regression in its emitter -- say so loudly rather than let a green diff imply full coverage.
EXPECTED = {"bottom", "bounce", "dig", "discard", "divide", "dragon", "echo",
            "expressive_iteration", "firebreathe", "lackey_put", "land_entry", "lightpaws",
            "main_phase", "mulligan", "reorder", "replicate", "retrace_discard", "sacrifice",
            "scry", "storage_hold", "surveil", "target", "vial_charge"}
missing = sorted(EXPECTED - set(types))
if missing:
    print("UNCOVERED emitters (a byte-diff over this capture proves nothing about them): "
          + ", ".join(missing))
