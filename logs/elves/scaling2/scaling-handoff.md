# Elves scaling-sink band refinement: handoff (2026-11-27, ~07:10 UTC)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/elves-scal2`, branch `wip/elves-scal2` (from `wip/elves-int`
2f1b0bff4). Scratch `/workspaces/MagicDeckTester/build/scratch/elves-scal2/`. Adoption is the USER's decision.

## USER rulings (verbatim)
* 2026-11-26: "There are multiple scaling effects like Genesis Wave's X and Joraga Warcaller, but generally you want to
  pick just one of those and scale it up fully, minus perhaps a few 1-mana producing attackers. So there might be a
  couple different relevant options, (i.e. do I tap this Llanowar Elves to scale it further or keep it as an attacker,
  which can either be searched or heuristically pruned depending on the situation) but we shouldn't try every
  possibility."
* 2026-11-27, on the "hold-all" amendment: "Why would we want to hold back all of our mana dorks? Some of them make a
  ton of mana that can go toward a Genesis Wave or otherwise. I don't disagree with there being a consideration there,
  though, but maybe we should use a heuristic to be smarter about this? I suspect the benefit you are finding are in
  some specific situations?"
* Coordinator machine rules: every full build as `MTG_BUILD_JOBS=14 flock /workspaces/MagicDeckTester/build/heavy.lock
  ./build.sh`; every engine run `MTG_RSS_CAP_GB=20`; A/B batches under the same lock; single games via
  `scripts/box_lane.sh --slots <=2`; no `timeout`; waiters on PID / DONE marker, never `pgrep -f` / `pkill -f`.
  Do NOT run regression tiers or accept GT. Never push phase-1-2-deck-analyzer (wip/elves-scal2 may be pushed).

## Done (commits on wip/elves-scal2)
* `c8aad7f49` holdall band re-applied on wip/elves-int (15ca40efb + 8de45a937 from wip/elves-scal; conflicts resolved
  onto elves-int's memory-lean Row + `elf_casts`; AMEND/HOLD_ALL selectors not re-added).
* `d87aa960c` A/B arms v1: temporary heurarm selectors `MTG_ELVES_SCAL_LUMP`, `MTG_ELVES_SCAL_SINKAWARE` (HeuristicArm.h,
  last two slots), helper `ElvesHoldBackOffsets` (DecisionProviders.cpp/.h), unit test "hold-back offsets".
* `d4c1f6c65` A/B arms v3 (current semantics): lump = subset-sum lump points with scaled yields as a RANGE
  [now, now + plan Elf casts]; sink = a Wave row frees only flat dorks; BOTH apply only with NO FOLLOW-UP this phase
  (no nonland card in hand beyond the plan's casts, no team_pump / team_base_pt / tap_tutor_put / untap_creature
  activation on our board). Units 4316/4316.
* WIP commit (this handoff): diagnostic census counters in `MTG_ELVES_SCALING_CENSUS` (rows and kept-below-top rows by
  follow-up x wave/anthem). NOT YET COMPILED.
* Scripts: `logs/elves/scaling2/{manifest.py,abstat.py,run.sh}`; stats `round1_stats.txt`, `round2_stats.txt`.

## Measurements
Round 1 (seeds 9790001+gi, binary = d87aa960c v1 arms; `build/scratch/elves-scal2/ab/round1.out`), units x full:

| arm | std net/full | std slower | std units | 2HG net/full | 2HG slower / faster | 2HG units |
|---|---|---|---|---|---|---|
| holdall | +0.0000 | 0 | x0.989 | +0.0000 | 0 / 0 | x0.982 |
| lump v1 | +0.0005 | 1361 | x0.944 | +0.0020 | 579, 626, 719 / 902 | x0.929 |
| sink v1 | +0.0005 | 1361 | x0.932 | +0.0030 | 572, 579, 626, 719 / 902 | x0.922 |
| combo v1 | +0.0005 | 1361 | x0.928 | +0.0030 | 572, 579, 626, 719 / 902 | x0.917 |

d8 b0 (v1): only 626 recovers; 1361/579/719 (and 572 for sink/combo) UNFINDABLE. Mechanism = WAVE-FIRST GROWTH
SEQUENCING, not attackers: a lower Wave X leaves the scaled dorks untapped, the Wave's Elves grow them, and they (plus a
lump's floating leftover) pay a follow-up in the breakpoint continuation (std 1361 T4 Wave X4 -> Wave X15; 2HG 719 T5
Wave X4 + 2 floating -> Wave X5; 2HG 572 T4 Wave X6 -> Brigade squad; 579 via look-ahead to Wave X12 + Shepherd).
v2 (range yields / sink-card follow-ups) fixed some, not all; v3 (any follow-up keeps the full band) recovers all 5 at
d5 b20 (626 at d8 b0).

Round 2 (FRESH seeds 9850001+gi, binary = d4c1f6c65 v3 arms; `build/scratch/elves-scal2/ab2/ab.out`):

| arm | std net/full | std slower | std units | 2HG net/full | 2HG slower | 2HG units |
|---|---|---|---|---|---|---|
| holdall | +0.0000 | 0 | x0.992 | +0.0010 | 814 | x0.981 |
| lump v3 | +0.0000 | 0 | x0.992 | +0.0010 | 814 | x0.978 |
| sink v3 | +0.0000 | 0 | x0.992 | +0.0010 | 814 | x0.978 |
| combo v3 | +0.0000 | 0 | x0.992 | +0.0010 | 814 | x0.977 |

No arm faster in round 2; every v3 arm is +0.0000 vs holdall (digest != holdall: 3/5/7 std, 4/0/4 2HG).
2HG 814: holdall T5 vs full T4 at d5 b20 AND d8 b0 (UNFINDABLE). NOT the band: full's T4 casts TWO Galadhrim Brigades
(squad 3 + squad 2) -- two sinks scaled together, removed by the USER's no-split rule (1); T3 differs by look-ahead.
First such loss in 6,000 fresh holdall games. Surface to the USER as a question (does a second copy of the SAME squad
card count as a split?).

Verdict so far: the round-1 savings (5-8% units) live in turns WITH a follow-up, where the hold-back is genuinely
needed (the USER's "a ton of mana that can go toward a Genesis Wave"); gated to no-follow-up the refinements save only
~0.3-0.4% in 2HG and 0 in std. Tentative recommendation: keep holdall as the band (or holdall + lump v3, which is a
near-sound fold: neutral on 3,000 games, ~0.3% 2HG); drop sink-aware (heuristic, its premise refuted in round 1).

## Remaining, in order
1. Build the census WIP under the lock (`MTG_BUILD_JOBS=14 flock .../heavy.lock ./build.sh`, check exit), copy
   `build/Release/mtg` to `build/scratch/elves-scal2/ab/mtg4`, run `build/scratch/elves-scal2/census/run.sh`
   (holdall arm, 400 std + 200 2HG on 9850001+gi, under the lock; DONE marker `census/DONE`; read the
   `[elves-scaling-census] follow=... sink=... rows=... kept_below_top=...` lines in `census/{std,2hg}.err`).
   This is the "which situations the hold-back matters in" census.
2. Decide the default (holdall, or holdall + lump v3); delete BOTH temporary selectors (HeuristicArm.h slots + names,
   `ElvesScalLumpOn` / `ElvesScalSinkAwareOn`, the bools in `ElvesHoldBackOffsets`), update the unit test, update the
   comment block at `ElvesScalingAdmit` and `docs/design/analysis-Elves.md` "Provider heuristics".
3. Build under the lock, units via `scripts/box_lane.sh --slots 2 -- build/Release/mtg-test`, then a digest-identity
   check of the final binary (default flags) vs the round-2 arm it ships (e.g. the ~50 games where that arm differs from
   full plus 50 others, from `ab2/ab.out` digests) via a small batch.
4. Write `logs/elves/scaling2/results.md` (tables above + census + mechanism), commit, push `wip/elves-scal2`.
5. Report: commits, table, census, recommendation, and the 2HG 814 split question to the USER; adoption is theirs.

## Running jobs
None. The census build (flock PID 2616149) was cancelled while still queued on the heavy lock; nothing is compiling.
Binaries: `ab/mtg3` = d4c1f6c65 (v3 arms, round-2 binary). Replay helper: `build/scratch/elves-scal2/rep1.sh
<fmt> <gi> <arm> <depth> <budget>` with env BIN, SUF, SEEDBASE (default 9790001), TRACE.
