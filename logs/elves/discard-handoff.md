# Elves 5i discard bucket policy -- HANDOFF (2026-11-26, context ~235K)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/elves-discard`, branch `wip/elves-discard`, base `ce2cddf5d`.
Nothing is pushed. Nothing is running.

## Done

* **29633f152**: `ElvesBucketShedOrder` + `ElvesProvider::CleanupDiscardCandidates / CleanupDiscardShedOrder /
  CleanupDiscardShedStable` in `src/ai/DecisionProviders.cpp` (after `ElvesProvider::PlanListAdmitAt`), decls in
  `DecisionProviders.h`, heurarm slot `ELVES_BUCKET_DISCARD` (`MTG_ELVES_BUCKET_DISCARD`, EnvOn default OFF) in
  `HeuristicArm.h`, testing-only `MTG_ELVES_DISCARD_FAN=1` (returns the full order). 4 new unit cases in
  `test/unit/test_elves_provider.cpp` (section 4: off == inherited Selesnya exactly; surplus land first; far spell
  first + one sink kept; dead legend / 2nd Crossroads).
* Units: full default suite **3968/3968 pass**; `test_elves_provider.cpp` under `MTG_UNPRUNED=1` and
  `MTG_HUMAN_PLAY=1` **11/11 pass** each.
* Today's Elves discard = the INHERITED SelesnyaLifegain buckets (Elves derives from SelesnyaLifegainProvider). That
  rule sends every lord / Warcaller / Ezuri / Shepherd / Magistrate / Crossroads / Taunting Elf to its `other`
  list, which is shed FIRST.
* Shed census at d5 b20 (flag off): real cleanup sheds 2 / 600 games (1 std of 400, 1 2HG of 200); rollout
  64,100 (~107/game, 99.85% low-land). Details and the drafted buckets:
  `logs/elves/discard-bucket-draft.md` (uncommitted when this was written -- commit it with this file).

## Remaining, in order

1. **The pooled A/B + bound batch** (ONE batch, heavy lock):
   ```
   cd /workspaces/MagicDeckTester/.claude/worktrees/elves-discard
   python3 logs/elves/discard/gen_manifest.py 400 9600000 > build/scratch/elves-discard/ab.json
   export MTG_RSS_CAP_GB=20
   MTG_DUMP_WINS=1 flock /workspaces/MagicDeckTester/build/heavy.lock build/Release/mtg --batch \
     build/scratch/elves-discard/ab.json > build/scratch/elves-discard/ab.out 2> build/scratch/elves-discard/ab.err
   ```
   8 jobs x 400 games (arms off / on / worst / ncw x std / 2hg), ~3200 games; the census ran 600 games in ~280 s
   wall per job, so expect roughly 25-40 min. Report with
   `python3 logs/elves/discard/pair.py build/scratch/elves-discard/ab.err off on worst ncw` (win sums, faster /
   slower, delta, t, every moved game with its repro seed) and the per-job digests from `ab.out`.
   * `worst` (MTG_SHED_WORST) brackets the rollout cleanup on the SHIPPED full ranking -- the bound. It is valid
     because the inherited Selesnya rule returns the full order; on the new policy it would be structurally zero
     (one index) unless `MTG_ELVES_DISCARD_FAN=1` is set.
   * `ncw` (MTG_NONCLEANUP_SHED_WORST) should be byte-identical to `off`: no card in the list has a discard cost or
     trigger. Confirm with the digest.
2. **Classify every slower game** of `on` vs `off` (and of `worst`): re-run each as a single game at
   `--depth 8 --budget-ms 0` (`--seed <9600000+gi> --game-index <gi> --games 1`, `starting_life 30
   opponent_heads 2` for 2HG via a manifest), both arms, through `scripts/box_lane.sh --slots 2`. Churn = recovers;
   PERSISTS = attribute it (which shed, which card).
3. **Rule-vs-searched labels**: real cleanup sheds are ~1 per 400 games, so this needs `MTG_DISCARD_NODE=0
   MTG_DISCARD_TRACE=1 MTG_ELVES_DISCARD_FAN=1` with `MTG_ELVES_BUCKET_DISCARD=1` over a few thousand games at
   d3 b10, single-threaded shards on disjoint seed bases (the trace is unsynchronised). Weigh it per game: at
   0.0025 sheds/game even a perfect oracle is worth ~nothing; the bound from step 1 is the real ceiling. If the
   bound is zero, say the check is moot and why.
4. Fill the A/B table and recommendation into `logs/elves/discard-bucket-draft.md`, commit on `wip/elves-discard`
   (pushing wip/elves-discard is fine; NEVER the shared branch), report.
5. If any rebuild is needed: `MTG_BUILD_JOBS=14 flock /workspaces/MagicDeckTester/build/heavy.lock ./build.sh`
   (coordinator: MemAvailable hits 0 during 32-job builds; the ENOMEM failures here were real RAM exhaustion).

## USER rulings (verbatim)

* "Yes, Taunting Elf's ability does nothing in goldfish."
* "There are multiple scaling effects like Genesis Wave's X and Joraga Warcaller, but generally you want to pick just
  one of those and scale it up fully, minus perhaps a few 1-mana producing attackers. So there might be a couple
  different relevant options, (i.e. do I tap this Llanowar Elves to scale it further or keep it as an attacker, which
  can either be searched or heuristically pruned depending on the situation) but we shouldn't try every possibility."
* The policy is a USER decision: author, measure, report; never adopt. The flag stays default OFF.

## Brief rules carried forward

flock heavy.lock for every heavy process; `export MTG_RSS_CAP_GB=20` on every engine run; small jobs through
`bash scripts/box_lane.sh --slots 2 -- <cmd>` (the script is not executable: call it with `bash`); never `timeout`;
waiters on a PID or DONE marker, never `pgrep -f`; run engine jobs from this worktree; never touch `references/`;
never edit cards.json with json.dumps; hand off at ~250K context.

## Measurements so far

| run | n | avg | digest |
|---|---|---|---|
| census std d5 b20 seed 9700000 (flag off) | 400 | 4.4125 | f3c39ead7a34e7e5 |
| census 2HG d5 b20 seed 9800000 (flag off) | 200 | 4.7150 | d1979697ecb7a769 |

SHED STATS (pooled over both): real=2, rollout=64,100 (low-land 64,004), hand at entry 8:63,989 / 9:12, inherited
policy idx0 differs from generic on 74.8% of calls.
