#!/usr/bin/env python3
"""Per-click latency workload for the play viewer, measured in CPU time.

Replays a saved reference's first k picks through the engine exactly as
tools/play/server.js's buildArgs() would (depth 0, no --reveal, the recorded
--cast-order side channel, --firebreathe-prompt / --storage-hold-prompt) and
reports the engine's USER+SYS CPU time for each k.

WHY CPU AND NOT WALL: the dev box is shared, so wall time is noise. The number
the human feels is the engine's own work, which is what getrusage(CHILDREN)
reports for a synchronously-waited child.

This is the ENGINE half of the viewer's per-click cost. The SERVER half -- whether
a click can reuse the persistent --interactive child at all, or has to respawn and
re-simulate the whole prefix -- is test/viewer_click_latency.js, and it is the half
that dominated before 2026-09-11. Run both.

Its helpers (flatten_choices / side_channels / deck_paths) are the one definition of
"what argv does the viewer send for this reference", and are imported by
test/interactive_parity_check.py's late-pin layer.

Usage:
  python3 test/viewer_step_latency.py --ref references/<Deck>/claude_s12_gi11.json \
      --ks 0,10,20,30,40,50,59 [--reps 3] [--bin build/Release/mtg] [--dump-dir DIR]

--dump-dir writes each k's raw stdout+stderr to <dir>/k<k>.out, which is what the
byte-identity check diffs across binaries.

MTG_PLAY_STEP_TIMING=1 (--env MTG_PLAY_STEP_TIMING=1) makes the engine print a
per-replay attribution line to stderr -- enumerate vs combo-off rules vs projection
vs trial apply vs apply. See src/ai/EngineFlags.h playtiming.
"""
import argparse
import json
import os
import re
import resource
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIDE_CHANNEL_TYPES = {"firebreathe", "storage_hold"}


def flatten_choices(decisions):
    """The viewer's --choices stream: every decision's pick in order, except the
    keyed side-channel types (they ride their own flags, not a positional slot).
    Mirrors test/viewer_protocol_check.py:flatten_choices(drop_mulligan=False)."""
    out = []
    for d in decisions:
        if d.get("decision", {}).get("type") in SIDE_CHANNEL_TYPES:
            continue
        c = d["chosen"]
        out += [int(x) for x in c] if isinstance(c, list) else [int(c)]
    return out


def side_channels(decisions):
    """The keyed side channels tools/play/server.js actually passes: --cast-order,
    --firebreathe, --storage-hold. Keyed, so passing the full set for a PREFIX is
    safe (the engine applies each only when it reaches that turn/ordinal)."""
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


def deck_paths(ref_path):
    deck = os.path.basename(os.path.dirname(os.path.abspath(ref_path)))
    d = os.path.join(ROOT, "decks", deck)
    for ext in (".cod", ".txt"):
        p = os.path.join(d, deck + ext)
        if os.path.exists(p):
            return p, os.path.join(d, deck + ".profile.json")
    raise SystemExit("no decklist for " + deck)


def run_once(bin_path, args, env):
    """Spawn and return (cpu_seconds, wall_seconds, stdout+stderr, rc).
    CPU is the delta of RUSAGE_CHILDREN around the synchronous wait."""
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    t0 = os.times().elapsed
    p = subprocess.run([bin_path] + args, cwd=ROOT, capture_output=True, text=True, env=env)
    t1 = os.times().elapsed
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
    return cpu, t1 - t0, p.stdout + p.stderr, p.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", required=True)
    ap.add_argument("--ks", default="0,10,20,30,40,50,59")
    ap.add_argument("--reps", type=int, default=1)
    ap.add_argument("--bin", default=os.path.join("build", "Release", "mtg"))
    ap.add_argument("--dump-dir", default=None)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--env", action="append", default=[], help="K=V passed to the engine")
    args = ap.parse_args()

    ref = json.load(open(args.ref))
    decisions = ref["decisions"]
    choices = flatten_choices(decisions)
    side = side_channels(decisions)
    deck, prof = deck_paths(args.ref)
    cards = os.path.join(ROOT, "src", "cards", "data", "cards.json")

    env = dict(os.environ)
    for kv in args.env:
        k, _, v = kv.partition("=")
        env[k] = v

    if args.dump_dir:
        os.makedirs(args.dump_dir, exist_ok=True)

    ks = [int(x) for x in args.ks.split(",") if x != ""]
    print(f"# ref={args.ref} decisions={len(decisions)} choices={len(choices)} bin={args.bin}")
    print(f"{'k':>4} {'cpu_s':>9} {'wall_s':>9} {'rc':>4}  tail")
    for k in ks:
        base = [deck, "--profile", prof, "--cards-json", cards, "--claude-play",
                "--seed", str(ref["seed"]), "--game-index", str(ref["game_index"]),
                "--max-turns", str(args.max_turns), "--depth", "0",
                "--choices", ",".join(str(c) for c in choices[:k]),
                "--firebreathe-prompt", "--storage-hold-prompt"] + side
        best = None
        for _ in range(max(1, args.reps)):
            cpu, wall, out, rc = run_once(args.bin, base, env)
            if best is None or cpu < best[0]:
                best = (cpu, wall, out, rc)
        cpu, wall, out, rc = best
        m = re.search(r"<<<CLAUDE_DECISION>>>\s*(\{.*?\})\s*<<<END_DECISION>>>", out, re.S)
        tail = ""
        if m:
            try:
                d = json.loads(m.group(1))
                tail = f'idx={d.get("decision_index")} t{d.get("turn")} {d.get("phase")} ' \
                       f'{d.get("type")} plans={len(d.get("plans") or [])}'
            except Exception as e:
                tail = "bad JSON: " + str(e)
        elif "<<<CLAUDE_RESULT>>>" in out:
            tail = "RESULT"
        else:
            tail = "no markers"
        print(f"{k:>4} {cpu:9.3f} {wall:9.3f} {rc:>4}  {tail}")
        if args.dump_dir:
            with open(os.path.join(args.dump_dir, f"k{k}.out"), "w") as fh:
                fh.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
