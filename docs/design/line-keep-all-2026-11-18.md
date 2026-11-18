# The line-keep rung on every multi-cast line (`MTG_LINE_KEEP_ALL`), 2026-11-18

Follow-up to `dragons-incubator-drop-2026-11-18.md` (`MTG_LINE_REDUCER_KEEP`), whose Residual section named this
change: the same greedy strand on lines with NO same-turn reducer credit.

## The family (mana-clean gate `claude-regate-20261118`)

`logs/mana_clean/claude-regate-20261118/report.md`: Dragons 242 APPLY-DROP, Treasure Hunt 255 (+40 APPLY-DROP-ORDER).
The leading signatures:

* Dragons x28, s30086708 gi86708 T4 `{cast:Atsushi + cast:Mind Stone}`, Urza's Incubator ALREADY in play.
* Dragons x14, s30086484 gi86484 T5 `{cast:Scourge of Valkas + cast:Mind Stone}`, same board shape.
* Treasure Hunt x24, s30090713 gi90713 T4 `{land=Cascade Bluffs + cast:Land's Edge + cast:Treasure Hunt}`.

## Mechanism (verified with `MTG_TAPDBG=1` on the oracle's engine apply; the same one, all three)

An earlier cast's greedy payment leaves the rest of the admitted line unpayable, while the board pays both together.
None of these lines has a same-turn reducer credit (the Incubator is already in play, so Atsushi / Scourge are priced
reduced at enumeration and the hold is exact), so `MTG_LINE_REDUCER_KEEP`'s rung never ran.

* s30086708 T4. Board Mountain#41, Mind Stone#30, Urza's Incubator#57, Gruul Turf#13. The payable-order decider casts
  Mind Stone first: `TFCD cost g2` -> `tap Mind Stone#30`, `tap Mountain#41`; then Atsushi `{R}{R}` meets Gruul Turf's
  `{R}{G}` alone and is dropped. Oracle: Mind Stone + Gruul Turf pay the `{2}` (`{C}{G}`, `{R}` floats), the Mountain
  makes the second `{R}`.
* s30086484 T5. Mountain x2, Mind Stone, Gruul Turf, Incubator: Mind Stone's `{2}` off Mind Stone + Mountain#32, then
  Scourge's `{R}{R}{R}` meets Mountain#33 + Turf's `{R}{G}`. Oracle: Mind Stone + Turf, both Mountains kept.
* s30090713 T4. Thundering Falls (`{U}`/`{R}`), Cascade Bluffs#4, Sandstone Needle (`{R}{R}`), land drop Cascade
  Bluffs#1. Land's Edge `{1}{R}{R}` off the Needle + Thundering Falls -- the only `{U}`; two unfed Bluffs make `{C}{C}`
  and Treasure Hunt `{1}{U}` is dropped. Oracle: Needle + a Bluffs' `{C}`, then Falls' `{U}` + the other Bluffs.

## The change (`MTG_LINE_KEEP_ALL`)

* **Seed.** The five sites that bind `LineReducerCreditScope` (`ApplyPlanDirect`, `AIEngine::TakeTurn`,
  `SubsetPayableAsExecutedImpl`, `SeqWalkFixedOrder`, `OrderPrepaysWholeTurnImpl`) now also bind `g_line_keep_bound`
  (`LineKeepBound`: the lever is on and the line holds two or more held casts) and, when bound, the line's noncreature
  bill. `LineUnpaidCostScope` clears it, so a nested apply / continuation is inert, exactly like the credit.
* **Rung.** `TapForCostSharedCore` runs `LineReducerKeepHold` when the line has a reducer credit (unchanged) OR is
  bound. With no credit the rest is the hold net of this payment. Everything after is the existing rung: trial on a
  scratch board; only when the trial strands the rest while the board pays bill + rest together, the fewest greedy
  sources held out, then the oracle payment table's first keeping option.
* **Cheap judge first (sound).** On a no-credit line the trial is judged by a WITNESS before the backtracker: the same
  payer pays the rest on the trial's own board. A paid rest is a payable rest, so the backtracker could only agree and
  the rung cannot fire: the filter skips only trials that provably could not fire. Only a failed witness re-makes the
  trial board for the complete judge. `MTG_LINE_KEEP_ALL_CHECK=1` re-runs the complete judge after every paid witness
  and counts disagreements.
* **Lever.** `MTG_LINE_KEEP_ALL` (`LineKeepAllOn`, `src/ai/EngineFlags.h`; heurarm slot `LINE_KEEP_ALL`, read per call).
  `=0` is byte-identical: the seed binds `bound = false` and the noncreature bill exactly as before, and the rung's gate
  reads `credit > 0 ? LineReducerKeepOn() : g_line_keep_bound`. Counters: `MTG_LINE_REDUCER_KEEP_STATS=1` prints
  `[line-keep-all] entries trials witnessed stranded fired check_bad ms` at exit.
* **Diagnostics (print-only / bisection, default off).** `MTG_LKA_FIRE_LIMIT=N` repairs only the first N no-credit
  strands of a thread (the rest pay as the greedy); `MTG_LKA_FIRE_DUMP=N` prints the N-th strand's board, bill, rest
  and the greedy's taps. Used below to find which repair moves a game.
* **No-greedy rule.** Payment-level only (the standing mana-payment exemption): it chooses sources for a cast the plan
  already names, never a plan; the search still branches. Applies at every budget (no unbudgeted gate).
* **Unit tests.** `test/unit/test_line_keep_all.cpp`: the s30086708 board (on: Atsushi pays after Mind Stone; off: it
  does not), the s30090713 board (same), and an inert control (a second Mountain: the greedy already keeps the line,
  taps identical on both arms; a one-cast line is never bound). The three `test_line_reducer_keep.cpp` cases still pass.

## Measurement (binary = this change, Release, `MTG_RSS_CAP_GB=40`, `MTG_PGO=0`)

### Exact repro seeds (`MTG_MANA_ORACLE=3`, APPLY-DROP at the named turn)

| seed / gi | turn | line | off | on |
|---|---|---|---|---|
| 30086708 / 86708 | T4 | Atsushi + Mind Stone | 7 | 0 |
| 30086484 / 86484 | T5 | Scourge of Valkas + Mind Stone | 7 | 0 |
| 30090713 / 90713 | T4 | Land's Edge + Treasure Hunt | 8 | 0 |

### Oracle gate slices (`MTG_MANA_ORACLE=2`, one pooled batch, lever per job, `MTG_LINE_KEEP_ALL_CHECK=1`)

Dragons gi 86000-87999 (seed 30086000+), Treasure Hunt gi 90000-91999 (seed 30090000+), the record's own blocks played
in a scratch batch (`build/scratch/claude_linekeep_all/manifest_oracle.json`), so `test/mana_clean.json` is untouched.

| deck | arm | APPLY-DROP | APPLY-DROP-ORDER | ENUM-GAP | ORACLE-GAP |
|---|---|---|---|---|---|
| Dragons | off | 93 | 0 | 0 | 44 |
| Dragons | on | 34 | 0 | 0 | 44 |
| Treasure Hunt | off | 139 | 15 | 5 | 0 |
| Treasure Hunt | on | 45 | 0 | 5 | 0 |

`[line-keep-all] entries=4330800 trials=882965 witnessed=842330 stranded=5971 fired=5971 check_bad=0 ms=10159.7` (the
on arms, search included): the witness settles 95.4% of trials and never disagreed with the complete judge; every
strand found a keeping payment. The rung's clocked time over those 4,000 oracle games was 10.2 s.

**Residual, NOT this mechanism (left out):**

* Dragons `{Dragon + Sol Ring}` / `{Mind Stone + Glorybringer}`: the rest is paid partly by the mana rock the line
  casts. The rung judges the rest on the board after this payment but BEFORE the rock resolves, so "the board pays
  both together" is false and it (correctly, by its own contract) does not fire. A ramp-aware rest board is a separate
  change.
* Dragons Haven of the Spirit Dragon lines (creature-only `{R}`) -- the known residual of the reducer rung.
* Treasure Hunt `{no land + cast:Treasure Hunt}` x42: the dropped action (Land's Edge) is drawn by Treasure Hunt and
  cast in the continuation -- not declared in the plan (`need_later`), so not a line payment at all.

### Held-out paired A/B (one pooled `mtg --batch`, 5,160 jobs of 25 games, seed = 610000 + off, gi = off)

Manifest `build/scratch/claude_linekeep_all/manifest_ab.json`: every regression-suite deck (58) at its play setting
(profile attached, no depth/budget override), per-job pin `{"MTG_LINE_KEEP_ALL": true|false}`, both arms in ONE batch
on one binary (`MTG_PGO=0`, Release). Dragons and Treasure Hunt 5,000 pairs; Maze's End, Sea Creatures, Gods 500 (the
costliest); every other deck 1,000. Unwon = 9. Heartbeat 32/32 throughout. `cpu on/off` is the pooled batch's per-job
cpu sum, not an isolated timing; prior byte-identical controls in this harness have read up to 1.17 on one deck, so a
single deck's ratio is noise-bound -- the systematic part is that 54 of 58 decks read above 1.00.

| deck | pairs | faster | slower | net turns (on-off) | digest-only | units on/off | cpu on/off |
|---|---|---|---|---|---|---|---|
| alchemist | 1000 | 0 | 1 | +1 | 4 | 1.0029 | 1.0238 |
| angels | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0101 |
| antilife | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0081 |
| auras | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0129 |
| boars | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0101 |
| breaching | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0164 |
| bruna | 1000 | 0 | 0 | +0 | 13 | 0.9996 | 1.0359 |
| burn | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0153 |
| colossus | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0069 |
| creature_giving | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0164 |
| critter | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0188 |
| cyclone | 1000 | 2 | 2 | -1 | 16 | 0.9931 | 1.0062 |
| dragons | 5000 | 1 | 0 | -1 | 11 | 0.9993 | 1.0179 |
| dragonstorm | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0200 |
| edf | 1000 | 0 | 0 | +0 | 20 | 0.9881 | 1.0255 |
| elementals | 1000 | 0 | 0 | +0 | 3 | 0.9999 | 1.0657 |
| extraturns | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0545 |
| fivecolour | 1000 | 0 | 0 | +0 | 2 | 0.9998 | 1.0122 |
| fluctuator | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0020 |
| fungusv3 | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0200 |
| galadriel | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0087 |
| giants | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0059 |
| goblins | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0062 |
| gods | 500 | 0 | 0 | +0 | 0 | 1.0001 | 1.0244 |
| hinata | 1000 | 1 | 0 | -1 | 2 | 1.0002 | 1.0142 |
| hydras | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0343 |
| jumbo | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0333 |
| kithkin | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0091 |
| kittyv3 | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0062 |
| knights | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0040 |
| landfall | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0084 |
| lhc | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0083 |
| martyr | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0006 |
| mazesend | 500 | 0 | 0 | +0 | 0 | 1.0000 | 1.0038 |
| melira | 1000 | 0 | 0 | +0 | 1 | 0.9992 | 1.0102 |
| merfolk | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0077 |
| merfolkturns | 1000 | 1 | 0 | -1 | 3 | 1.0001 | 1.0074 |
| midrangeburn | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0059 |
| mill | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0096 |
| minotaur | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0040 |
| mirrorwing | 1000 | 0 | 0 | +0 | 5 | 0.9976 | 1.0114 |
| modernrwburn | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0066 |
| morph | 1000 | 0 | 0 | +0 | 1 | 1.0000 | 1.0098 |
| niv | 1000 | 2 | 1 | -1 | 36 | 0.9971 | 1.0547 |
| pd | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 0.9987 |
| pirates | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0169 |
| redelem | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0017 |
| sea | 500 | 0 | 0 | +0 | 0 | 1.0002 | 1.0116 |
| selesnyav2 | 1000 | 1 | 0 | -1 | 22 | 0.9966 | 1.0156 |
| shadowborn | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0519 |
| slivers | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0108 |
| snow | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0068 |
| soldiers | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 0.9989 |
| spirits | 1000 | 0 | 0 | +0 | 9 | 0.9999 | 1.0094 |
| stompy | 1000 | 0 | 0 | +0 | 2 | 1.0000 | 1.0185 |
| th | 5000 | 0 | 0 | +0 | 13 | 1.0001 | 1.0107 |
| vampires | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0047 |
| whiteknights | 1000 | 0 | 0 | +0 | 0 | 1.0000 | 1.0158 |
| ALL | 64500 | 8 | 4 | -5 | | 0.9994 | 1.0178 |

Totals: 64,500 pairs, 8 faster / 4 slower, net -5 turns; units 0.9994 (searched units essentially unchanged: the rung
re-pays, it does not widen the search); pooled cpu 1.018 (about +1.8%, the largest decks +5-7%: Landfall Elementals,
Extra Turns, Niv-Mizzet, Shadowborn).

**Confirmatory re-measure, AccomplishedAlchemist** (the only deck with net > 0), 5,000 further fresh pairs
(gi 1000-5999, `manifest_alc.json`): 1 faster / 1 slower / net 0, units 1.0017, cpu 1.023. Combined 6,000 pairs:
**1 faster, 2 slower, net +1.**

### Slower games: all recover at `--depth 8 --budget-ms 0` (both arms identical turn AND digest)

| deck | gi | off | on | d8 b0 off | d8 b0 on |
|---|---|---|---|---|---|
| Accomplished Alchemist | 470 | 6 | 7 | 6 | 6 |
| Accomplished Alchemist | 4163 | 5 | 6 | 5 | 5 |
| Unpredictable Cyclone | 348 | 4 | 5 | 4 | 4 |
| Unpredictable Cyclone | 507 | 4 | 5 | 4 | 4 |
| Niv-Mizzet Combo | 174 | 7 | 8 | 7 | 7 |

### Root cause of the Alchemist slower games (USER rule: a worse result is root-caused, never just reported)

* **gi 470** diverges at T1: with the lever on, the root `Forest, no cast` gets a turn-8 rollout tail while
  `Forest + Arbor Elf` keeps 9 (`MTG_FS_ROOT_DUMP=1`); off, both read 9 and Arbor Elf wins on value. Two pessimistic
  tails (the game is won T6) flip on one rollout payment.
* **gi 4163** diverges at T4 (Alchemist + Feed the Clan instead of Alchemist alone; off then wins T5 with
  `{Feed the Clan, Nourish, Feed the Clan, Ezuri}`). Bisecting the rung's repairs (`MTG_LKA_FIRE_LIMIT`, diagnostic)
  puts the flip on repair #7, inside the T4 search: Stroke of Genius `{3}{U}` paid by the greedy with Forest+Utopia
  Sprawl, the other Forest and Elvish Harbinger, leaving only Arbor Elf for Nourish's `{G}{G}` -- a true strand by the
  engine's complete judge, repaired correctly. The repaired lookahead line re-ranks the budgeted root.
* A first hypothesis -- that a source whose yield grows within the turn (the Alchemist's X = life gained) makes the
  rung's pre-resolution judgement unsound -- was tested with a guard and **refuted**: neither game recovered, and the
  flipping repair has no Alchemist on the board. The guard was removed (no complexity on a wrong root cause).

So both Alchemist losses are budgeted-search ranking noise from a more faithful payment in one branch; neither is a
wrong payment, and both recover at depth 8 unbudgeted. By the bar ("every deck net <= 0"; "+1 is not neutral") the
lever stays **default OFF**; adopting it anyway is the USER's call.

## Cost

* Cheap judge: a WITNESS (the greedy pays the rest on the trial board) settles 95.4% of trials; `check_bad=0` over
  842,330 witnessed trials with `MTG_LINE_KEEP_ALL_CHECK=1` (the complete judge never disagreed).
* Remaining cost per trial: one GameState scratch copy and two greedy payments (plus the complete judge on the ~5%
  candidate strands). Pooled cpu +1.8% overall at the play settings; searched units unchanged (0.9994).
* Not built: a pre-trial filter that skips the scratch copy. A sound one needs a LOWER bound on what every payment
  of the bill leaves (Hall's condition over single-unit sources), and the engine's source helpers are all
  over-counting upper bounds (`SourceMaxNetLive`, `SourceColorCapLive`); a hand-written "simple source" classifier
  over `CardParams` is where an unsound skip would hide. Left as the next step if the USER adopts the lever.

## Open for the USER

1. Adopt `MTG_LINE_KEEP_ALL` default ON despite AccomplishedAlchemist's +1 / 6,000 (both slower games recover at d8
   b0; root cause above), or keep it off.
2. The residual Dragons drops whose rest needs a rock the line itself casts (Sol Ring / Mind Stone): a ramp-aware rest
   board is a separate change.
