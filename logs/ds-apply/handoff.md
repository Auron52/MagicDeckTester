# Dragonstorm APPLY-DROP handoff 2 (2026-11-26 ~21:05 UTC)

Worktree `.claude/worktrees/ds-apply`, branch `wip/ds-apply`. Push ONLY wip/ds-apply; the parent lands through the push
queue. Never touch references/. Scratch `/workspaces/MagicDeckTester/build/scratch/ds-apply/` ($S). Design doc with all
measurements: `docs/design/dragonstorm-apply-drop-2026-11-26.md`.

## Commits
- 893647990: MTG_ONDEMAND_VISIBLE_LUMP_EXACT + MTG_LEFTOVER_SWAP_WIDE (first version).
- e07c7849c: LUMP_EXACT narrowed. It refuses the early Bloom crack only when the crack STRANDS the later bill (the
  ordinary board pays and the early one does not). The first version also refused it when both paid. That cost 2HG
  s5131993 gi1993 its T5 kill (5 -> 6, persisted at d8 b0): the crack keeps Sandstone Needle's {R}{R} for Scourge's
  firebreathing, which a leftover {B} cannot pay. The no-strand control test now asserts equal mana left (the swap
  re-attributes which unit floats). New both-pay unit test.
- 20cd98179: docs only (gate2 + ab2 tables).
- ca4ee7d53: MTG_LINE_FLOAT_RITUAL_NET (handoff-1 item 4b; heurarm slot at the END of both lists). The line-float
  rung's spare test nets the paying ritual's float out of later[]. Unit test on/off. UNVALIDATED: its play A/B is
  running.

**LANDABLE: 20cd98179** (= e07c7849c + docs). Units 4205/4205. Both levers are covered by A/Bs:
- First A/B (893647990, base 5130000, all 63 decks 400 std + 200 2HG/arm, Dragonstorm 3000 + 3000): every
  non-Dragonstorm deck net 0, 0 slower / 0 faster (units move only in Niv and Sea, <0.01%, from the swap). The
  narrowing touched only the Dragonstorm-only lump rule, so this covers SWAP_WIDE for other decks.
- Held-out A/B (e07c7849c, Dragonstorm, base 5160000, 3000 std + 3000 2HG/arm): net 0, 0 slower, 0 faster; units
  -0.00% std / +0.00% 2HG; cpu +0.06% / -0.56% (wall, noisy).
- The only slower game (first version) is byte-identical to off at play settings and d8 b0 on e07c7849c.

## Running (mine; the coordinator may stop it for its landing tiers and relaunch after TIERS.DONE)
- chain3 ($S/chain3.sh, pid 567832; flock child 599172) under claude_box.lock, now in its last stage: the all-deck
  held-out play A/B of ALL THREE levers off vs on, `$S/bin_new3/mtg` (= ca4ee7d53), manifest $S/ab3.json
  (`mk_ab.py --n 400 --hg 200 --wf 3000 --base 5190000`, 86,400 games, about 1 h), out $S/ab3_out, DONE marker
  $S/ab3_out/DONE (chain marker $S/chain3.DONE). Report: `python3 logs/ds-apply/ab_report.py $S/ab3_out --list-slower`.
  Relaunch if stopped: `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock env MTG_RSS_CAP_GB=40
  MTG_PGO=0 bash logs/ds-apply/run_ab.sh $S/bin_new3/mtg $S/ab3.json $S/ab3_out`.

## Remaining, in order
1. ab3 report. Replay every slower game at d8 b0 (template $S/slower.json: depth 8, budget_ms 0,
   ignore_play_profile true, abandon_units 30000000; 2HG starting_life 30, opponent_heads 2). If any persists, find
   the lever by leave-one-out and root-cause it (method that worked: `--claude-play --choices ... --choices-then-auto`
   to force the off arm's early choices, then diff game logs from `--log-dir`; 2HG CLI = env MTG_START_LIFE=30
   MTG_OPPONENT_HEADS=2). ca4ee7d53 is landable only once that is clean.
2. Next mechanisms (handoff 1 item 4): a (two Unclaimed Territories: an earlier ritual's generic takes a Territory's
   {C}), c (exact swap shortage test via LumpAwareBillPayable), d and e (low priority).

## Gate (Dragonstorm slice of claude-queue-20261125a, 72,000 games, MTG_MANA_ORACLE=2; logs/ds-apply/gate_report.py
on $S/gate_m, which merges gate_out off/on, gate2 on as on2, gate3 on as on3)
| arm | events decl | events need-later | AD flags decl/undecl | ENUM-GAP | turns |
|---|---:|---:|---|---:|---:|
| off | 105 | 47 | 415 / 299 | 33 | 313,645 |
| on (893647990) | 62 | 56 | 288 / 363 | 41 | 313,643 |
| on2 (e07c7849c) | 64 | 48 | 296 / 304 | 33 | 313,643 |
| on3 (ca4ee7d53) | 62 | 48 | 291 / 306 | 33 | 313,647 |
LOO (first version, 18k games): lump 12 of 16 fixed events, swap 4. New need-later events in the first version came
from earlier-turn search divergence (new boards), not worse payments of a shared board.

## USER rulings (verbatim from the brief)
- "spend the counters if they are required for the line" (Dragonstorm).
- Adoption bar: judge the average at play settings. Adopt if many decks gain, mitigate the few that get worse
  afterwards, ignore seed noise and clairvoyant losses; d0 barely counts.
- "cut CPU"; "suite headroom never justifies a fix's CPU cost".
- A sound collapse defaults ON; a lossy narrowing needs its A/B; everything must stay findable at unlimited budget.
- USER-owned decisions are surfaced in the report, never settled.
- Box: ONE big engine process at a time under claude_box.lock with MTG_RSS_CAP_GB=40 MTG_PGO=0; small jobs via
  box_lane.sh, at most 2 slots; no `timeout`; rebase, never merge; HeuristicArm.h slots at the END of both lists.

## Traps found this round
- The lane can be blocked for hours by other agents' builds holding slots while they wait on the build lock. When
  the box lock is free, run small jobs under it instead.
- `pgrep -f <pattern>` inside a waiter matches the waiter itself: wait on a DONE file or `kill -0 <pid>`.
- Changing HeuristicArm.h / EngineFlags.h recompiles about 386 objects. Build under the box lock at full width when
  the box is free.
