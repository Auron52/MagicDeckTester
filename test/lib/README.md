# `test/lib/` — shared harness primitives

Source `harness.sh` from any script under `test/` or `scripts/` instead of re-deriving how to
find the binary, build a manifest, run a batch, or read the metric.

```bash
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh

BIN=$(harness_bin) || exit 1
DECK=$(h_deck burn); PROF=$(h_profile burn)
h_require "$DECK" "$PROF" || exit 1

OUT=test/logs/my_ab
{ for s in $(h_seeds_train); do
    h_job "burn_d3_s$s" "$DECK" "$PROF" 600 "$s" depth=3 budget_ms=100 ignore_play_profile=true
  done
} | h_manifest "$OUT/manifest.json" >/dev/null

           h_batch "$BIN" "$OUT/manifest.json" "$OUT" base    >/dev/null
MTG_MY_FLAG=1 h_batch "$BIN" "$OUT/manifest.json" "$OUT" variant >/dev/null

h_delta     "$OUT/base.log" "$OUT/variant.log" base variant
h_wins_diff "$OUT/base"     "$OUT/variant"
```

## Why this exists

`test/regression.sh` already got pooled-batch execution, seed disjointness, fingerprint parsing
and per-game diffing right. Every one-off A/B script re-implemented a subset — 55 of them invoke
the binary directly, 16 re-derive the metric, 26 hand-roll an A/B skeleton — and each
re-implementation is a fresh chance to measure the wrong thing.

That is not hypothetical. Found while extracting this library:

- `fd_quick_ab.sh` and `fd_overnight_ab.sh` hard-coded `build/Release/mtg.exe` with no fallback
  and the pre-folder-move flat deck paths. Neither could run on Linux at all.
- 11 more scripts still name decklists that moved into per-deck folders.
- The repo has already burned a session on "engine nondeterminism" that was a repro-config
  mismatch.

And in the primitives themselves, before they were tested:

- `h_job` emitted job objects without a trailing newline, so `h_manifest` silently dropped the
  last job. A manifest one job short still runs and still prints an average.
- A `paste`-based per-game diff read the `.wins` file's third column (the play digest) as a win
  turn and mis-paired the files if either was written out of order.
- Scoring win turns numerically ranks a loss (recorded as `-1`) as the *fastest* possible win.

Each of those produces a plausible-looking number rather than an error. That is the failure mode
the library exists to remove.

## What it gives you

| Primitive | Purpose |
|---|---|
| `harness_bin`, `harness_analyze_bin` | Resolve the binary. Honours `MTG_BIN`, handles the Windows/Linux multi-config split, and tells you to run `./build.sh` if nothing is built. |
| `h_deck <name>`, `h_profile <name>` | Resolve `decks/<name>/<name>.{txt,cod}` and its profile. One place knows the layout. |
| `h_require <path>...` | Assert inputs exist **before** a long run, rather than getting a bogus average hours later. |
| `h_seeds_smoke/train/heldout` | The disjoint seed sets. Tune on train, validate on held-out — never both on one set. |
| `h_job`, `h_manifest` | Build a batch manifest, including the `depth` / `ignore_play_profile` pairing that the `value_play` block requires. |
| `h_batch` | Run **one** pooled batch. Streams live, captures the log, records `H_BATCH_SECONDS`. |
| `h_avg`, `h_digest`, `h_jobs`, `h_delta` | Read the metric: mean turn-to-win, unwon scored `max_turns+1`. Lower is better; negative delta is an improvement. |
| `h_wins_diff` | Per-game slower/faster/play-changed, indexed by game index and scored loss-penalized — the same semantics as `test/audit_changed_games.py`. |
| `h_repro_cmd` | Print the command that replays one game. The per-game seed is `base_seed + gi` **and** `--game-index gi` is still required (it picks the opponent spawn pattern). Getting one without the other reproduces a different game and looks like a flapping engine. |

## Rules the library encodes

These come from [CLAUDE.md](../../CLAUDE.md) and are not style preferences:

- **One pooled work queue.** `h_batch` runs exactly one `--batch`. Do not loop it per seed, deck
  or arm — each invocation strands cores on its own load-imbalance tail, and any serial step
  between them idles the machine. Bake per-job settings into one manifest. The one legitimate
  reason for a second `h_batch` is an arm needing a different process-level `MTG_*` env, which a
  manifest cannot express.
- **Never wrap a run in `timeout`.** A truncated run reads as a result and corrupts an A/B.
- **Logs under `logs/` or `test/logs/`**, never the repo root.

## `check_paths.sh`

```
bash test/lib/check_paths.sh            # report dead deck/reference/binary paths
bash test/lib/check_paths.sh --strict   # exit 1 if any are dead
```

Greps literal paths out of every script and asks the filesystem. It cannot see paths built from
variables, so a clean report is **not** proof a script runs — it is proof of the absence of one
specific, recurring rot. Currently 52 dead paths across 11 scripts, all from the per-deck folder
move; those scripts are unverified one-off A/Bs, so they are reported rather than blind-edited.

## `capture_decisions.py` + `capture_results.py`

```
python3 test/lib/capture_decisions.py /tmp/before.txt   # 11,511 decision frames, 23 wire types
python3 test/lib/capture_results.py   /tmp/before_r.txt # 106 result frames + the --log-dir traces
# ...change src/main.cpp, ./build.sh...
python3 test/lib/capture_decisions.py /tmp/after.txt  && cmp /tmp/before.txt /tmp/after.txt
python3 test/lib/capture_results.py   /tmp/after_r.txt && cmp /tmp/before_r.txt /tmp/after_r.txt
```

**Run both.** `capture_decisions.py` covers every decision emitter but stops at decisions — it never
runs a game to completion, so it never reaches the terminal `<<<CLAUDE_RESULT>>>` frame or the
reference file a `--log-dir` run writes. Those are different emitters, and a refactor can break them
while all 11,511 decision frames stay byte-identical. `capture_results.py` is the other half.

## `keepgen_check.sh`

```
bash test/lib/keepgen_check.sh base     # capture a baseline with the current binary
# ...change code, ./build.sh...
bash test/lib/keepgen_check.sh mine     # byte-diff every artifact against base
```

Byte-exact check for the exhaustive keep/bottom generator and the offline merge tool. **The
regression suite drives `mtg`, not `mtg-analyze`, so it cannot see a change to profile generation at
all** — a broken generator ships a subtly-worse mulligan policy that surfaces days later as a drifted
win turn. Ten runs, ~4 minutes, 38 compared artifacts: a below-floor generation (which exercises the
profile REFUSAL branch), two disjoint-seed chunks that clear the floor (so `BuildPolicyFromTables`
and the profile write run), their merge, the synthetic adaptive-bottom reconstruction, the offline
regret simulator, an execution-trace generation, the change-detection carry against a prior pool, the
same with trace-based cell reuse, and a cross-run prune set emitted then consumed.

It **asserts each opt-in path engaged**, by grepping for the line that path prints. An opt-in path
that is entered and skipped compares equal and proves nothing, which is the failure mode this kind of
harness is most prone to.

Four things that silently invalidate a comparison, all handled — and all found the hard way:

- `--seed` is mandatory. Unseeded, the analyzer randomizes `meta.seed_base` per run.
- `meta.engine_fp` is a build-time hash over the engine source, so it moves on a pure code move.
- The raw path is echoed into the report, so it is rewritten before comparing.
- **Each run needs its own raw path.** The generator reads an existing `out_raw` as a resume
  checkpoint, so a shared path couples the runs — and the first run of a fresh invocation sees no
  file while the second sees one. Caught by running the harness twice against one build, which is
  the only way to know a baseline is a baseline.
- stderr carries wall-clock progress (percent sampled by elapsed time, throughput). Those lines are
  dropped and timings blanked; semantic stderr (a PRIOR-RAW refusal, a PRUNE-SET carry count) is not.
