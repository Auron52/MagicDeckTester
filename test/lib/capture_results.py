#!/usr/bin/env python3
"""Capture the --claude-play TERMINAL output: the <<<CLAUDE_RESULT>>> frame and the --log-dir trace.

    python3 test/lib/capture_results.py <out-file>

WHY THIS EXISTS SEPARATELY FROM capture_decisions.py. That script captures every DECISION frame --
11,511 of them across all 23 wire types -- and it is the right check for the emitters. But it stops
at decisions: it never runs a game to completion, so it never reaches the terminal CLAUDE_RESULT
block or the reference file a --log-dir run writes. Those are a different set of emitters
(WriteClaudePlayResult / WriteClaudePlayTrace), and a refactor can break them while all 11,511
decision frames stay byte-identical. Run BOTH.

Method: replay every saved reference to completion using its own recorded choices, writing traces to
a temp dir, and record the result frame plus the trace bytes. Reuses viewer_protocol_check.py's
choice-flattening / side-channel / forced-mulligan helpers rather than re-deriving them -- those
rules are subtle and getting them wrong silently replays a different game.

Output: one record per reference, sorted for a stable diff. Capture before, capture after, `cmp`.
Not every reference produces a result frame (a replay can stop at a decision the reference predates);
the count is printed so a drop in coverage is visible rather than silent.
"""
import sys, os, glob, json, subprocess, concurrent.futures, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
os.chdir(REPO)
sys.path.insert(0, os.path.join(REPO, "test"))
import viewer_protocol_check as V  # noqa: E402

OUT = sys.argv[1] if len(sys.argv) > 1 else None
if not OUT:
    sys.exit(__doc__)


def one(path):
    ref = json.load(open(path))
    deck_dir = os.path.basename(os.path.dirname(path))
    deck, prof = V.DECKS.get(deck_dir, (None, None))
    if deck is None:
        return None
    choices = V.flatten_choices(ref["decisions"])
    extra   = V.side_channel_args(ref["decisions"])
    force   = V.force_arg(ref)
    with tempfile.TemporaryDirectory() as td:
        args = [V.MTG, deck, "--claude-play", "--seed", str(ref["seed"]),
                "--game-index", str(ref["game_index"]), "--max-turns", "9", "--depth", "0",
                "--profile", prof, "--choices", ",".join(str(c) for c in choices),
                "--log-dir", td]
        if force is not None:
            args += ["--force-mulligan", force]
        if extra:
            args += extra
        p = subprocess.run(args, capture_output=True, text=True)
        written = sorted(glob.glob(os.path.join(td, "*.json")))
        traces  = "".join(open(f).read() for f in written)
        names   = " ".join(os.path.basename(f) for f in written)
    marker = "<<<CLAUDE_RESULT>>>"
    res = p.stdout[p.stdout.find(marker):] if marker in p.stdout else "(no result frame)"
    return f"### {path} rc={p.returncode} files={names}\n{res}\n--- trace ---\n{traces}\n"


refs = sorted(glob.glob("references/*/claude_s*_gi*.json"))
with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as ex:
    recs = [r for r in ex.map(one, refs) if r]
open(OUT, "w").write("".join(sorted(recs)))
n_res = sum(1 for r in recs if "<<<CLAUDE_RESULT>>>" in r)
print(f"{len(recs)} references replayed to completion; {n_res} produced a RESULT frame -> {OUT}")
