# Snow plan-payment shortfalls (2026-11-13)

Follow-up to Morph INT-14 (`docs/design/analysis-Morph.md`, "plan-wide payment shortfalls"): the
`MTG_LINE_PAY_AUDIT` found 9 Snow committed plans (payer2/t2 OFF arm, seeds 61000+, d5 b20) whose
payment came up short although the oracle said the whole bill was jointly payable, plus 3 cases on
SelesnyaLifegain / EldraziDisplacerFlicker. Worktree `.claude/worktrees/snow-contpay`, branch
`snow-contpay` (local).

USER rulings (verbatim, 2026-11-13):
- "The payment order doesn't matter that much to me but our mana handling should be smart enough to
  address shortfalls in part of the plan. Maybe that means we need adjustments."
- "Let's pull and then attempt fixing the snow shortfall."
- "we have an agent that has been working on mana for a while" (build on the prepay / wild-mana work).
- Snow's cast/activation order is USER-owned: the fix must not change how the provider ranks the Owl
  against the Augur / Sheets activations.

## 1. The audit was not honest (fix (c), measurement only)

Two classification errors in the INT-14 oracle:

1. **A K-repeat sink was billed once.** `LineJointPayOracle` priced Rimefeather Owl `x3` as one
   `{1}{S}`; the apply pays all three. Now the oracle bills `chosen_x` repeats of every {T}-less
   ActivatePermAbility (`joint=`); the old verdict prints as `joint1=`.
2. **A continuation's spend was blamed on the plan.** When a breakpoint continuation applied anything
   inside the plan's apply (`resolve_draw_breakpoint`: the searched/carried continuation or the
   executor's re-solve), the line changed mid-apply; the oracle prices only the top-level plan. Such
   plans are now class `CONT` (counter `cont=`), not YES/NO. `LINE_PAY_SHORT` prints `class=` and
   `cont=[carried:... | resolve:...]`.

Re-classification of the 12 cases (single-game repros, `build/scratch/snowcp/tools/{nine,others}.sh`):

| case | old | honest | mechanism |
|---|---|---|---|
| Snow g57 T6, g66 T5, g42 T6, g87 T7, g27 T6, g188 T6 | DEFECT | unpayable | Owl xK extras eat the Augur/Sheets mana (fix (b)) |
| Snow g42 T3 | DEFECT | CONT | re-solve offered phantom [Astrolabe, Sheets(act)] (fix (a)) |
| Snow g30 T4 | DEFECT | CONT | carried continuation cast Jorn |
| Snow g120 T5 | DEFECT | CONT | carried continuation cast Abominable Treefolk |
| Selesnya g68 T3, g156 T4; EDF g36 T3 | DEFECT | DEFECT | cast-only lines with a producer (prepay PRODUCER decline; the mana agent's area) |

So on Snow the TRUE jointly-payable count is **0** of 9; the 6 Owl cases are real shortfalls of a
different kind (the line as committed was never payable), and 3 are re-plans.

## 2. Fix (b): K-repeat extras keep what the plan still owes (`MTG_REPEAT_EXTRAS_RESERVE`, default ON)

A repeatable {T}-less sink (Owl `{1}{S}: ice counter`) asked for K activations carries ONE activation
on the subset's books; `SpendRepeatActivations` pays the K-1 extras at apply "out of what is left".
Snow's activation order (USER-owned) runs the Owl first, so its extras spent the mana and the {T}
sources of the plan's later Augur / Sheets activations. Fix: `TurnSolver::RepeatExtrasBesideOwed`
caps the extras at the most that leaves the trailing pass's LATER owed activations payable (probe:
the real repeat payer on a scratch board, then the owed {T} sources set aside and the owed bill paid
as one cost). Both apply worlds call it at the same point (lockstep). The order is unchanged.

Why this shape and not "bill K x cost on the subset's books": billing all K up front would refuse a
self-funding sink line (EDF's Drain / ExileTop extras paid from mana the turn makes that the subset
math does not credit) at every budget, which is lossy. The cap is lossless: every line the old apply
realised for an over-admitted plan is the realisation of an honest plan the enumeration still offers
(K is still branched over 1..3, and [Owl xK] alone stays on the menu).

Scenario: `test/scenarios/snow_owl_repeat_extras_keep_owed_sheets.json` (g66 T5 main 2): before,
[Sheets(act), Owl x3] fires Owl x3 and the look is unpaid (1 short plan); after, 0.

## 3. Fix (a): the phantom [Astrolabe, Sheets(act)] is the rescue's self-funding hole

Root cause: an Arcum's Astrolabe in HAND arms the filter real-payment rescue
(`SubsetPayableWithFilters`, `PendingFilterInHand`), and that rescue never applies an activation's own
{T}, so Scrying Sheets pays for its own `{1}{S}, {T}` and the unpayable subset is admitted. The fix
already exists as `MTG_RESCUE_TAP_SOURCE` (default OFF since 2026-09-25 as a USER decision; see
`snow-intractable-games.md` "The rescue lets a source pay for its own tap"). With it, the g42 T3
re-solve offers [Astrolabe] (no phantom activation). Measured below as its own arm; NOT flipped.

## 4. Measurement

### 4.1 Single-game repros (binary c711324b6; `build/scratch/snowcp/nine3.txt`, `others3.txt`)

Unit 2971/2971, scenarios 513/513; the new scenario passes with the lever and fails at `=0`.

- The six Owl xK shortfalls are gone (g57 T6, g42 T6, g87 T7, g27 T6, g188 T6, g66 T5). Win turns are
  unchanged in all eight games.
- Still present: the three continuation re-plans (class CONT: g30 T4, g42 T3, g120 T5).
- Three `unpayable joint1=no` lines remain: g30 T5 and g57 T4 (present before fix (b)) and g66 T6 (new, reached
  only once the T5 fix changed the game: `[Slumber, Sheets(act), Owl x2, Sheets(act)]`). With
  `MTG_RESCUE_TAP_SOURCE=1` all three disappear, so they are the fix (a) self-funding phantom, not a fix (b)
  residue.
- Selesnya v2 g68 / g156 and EDF g36: unchanged (cast-only producer lines; the mana agent's area).

### 4.2 Pooled A/B (binary c711324b6, play settings, `MTG_LINE_PAY_AUDIT=2`, 24 threads)

Arms: `off` = `MTG_REPEAT_EXTRAS_RESERVE=0`; `b` = fix (b) (default); `ba` = fix (b) + `MTG_RESCUE_TAP_SOURCE=1`
(measured only; it stays OFF, a USER decision). Batch 1: 3600 games (Snow 600 x3 arms; Selesnya v2, EDF, Kithkin
300 x off/b; seeds 61000+, game_index 0+, chunks of 20, arms interleaved). Heartbeat 24/24 busy. **Seeds 61000+
are the sample the nine Snow cases were found in (selection, not held out).** Batch 1 ran without
`--game-log-dir`, so batch 2 re-ran every chunk whose case digest differed from `off` (Snow 28 chunks x3, EDF c6 x2;
digests reproduced 12/12 against batch 1) plus a HELD-OUT Snow sample (seed base 81000, 300 x3) with per-game
outcomes. Chunks whose digest equals `off` are the same games.

| deck | arm | n | avg | delta | faster | slower | play-changed games | units | Snow short-plan games |
|---|---|---|---|---|---|---|---|---|---|
| Snow (61000+) | off | 600 | 5.6667 | | | | | 1.0000 | 144 |
| | b | 600 | 5.6667 | 0 | 0 | 0 | 10 | 1.0001 | 138 |
| | ba | 600 | 5.6667 | 0 | 0 | 0 | 81 | 0.9740 | 25 |
| Snow held-out (81000+) | off | 300 | 5.6267 | | | | | 1.0000 | 68 |
| | b | 300 | 5.6267 | 0 | 0 | 0 | 12 | 1.0006 | 58 |
| | ba | 300 | 5.6267 | 0 | 0 | 0 | 51 | 0.9766 | 9 |
| Selesnya v2 | b | 300 | 4.4700 | 0 | 0 | 0 | 0 | 1.0000 | (36 games, unchanged) |
| EDF | b | 300 | 4.0567 | 0 | 0 | 0 | 1 | 1.0000 | (57 games, unchanged) |
| Kithkin | b | 300 | 4.5500 | 0 | 0 | 0 | 0 | 1.0000 | 0 |

Snow shortfall lines by class (batch 1, 600 games per arm):

| class | off | b | ba |
|---|---|---|---|
| Owl xK, payable billed once (`joint1=YES`): fix (b)'s target | 10 | 0 | 0 |
| `unpayable joint1=no`: the rescue self-funding phantom (fix (a)) | 167 | 168 | 12 |
| CONT (continuation re-plan) | 18 | 18 | 13 |

Reading:
- **Fix (b)** removes every shortfall it targets and changes no win turn: 0 faster / 0 slower in 860 paired Snow
  games, 0 / 0 on the other three decks. Units 1.0001 / 1.0006. It meets the adoption bar trivially (no slower
  game, every deck net 0).
- **`ba`** (rescue tap-source) removes ~93% of Snow's remaining shortfall lines, also with 0 faster / 0 slower
  (81 + 51 games play differently, none at a different turn) and 2.5% fewer units. The win-turn effect is zero
  on both samples, so this is a correctness and work result, not a speed result. It stays OFF: a USER decision
  (`snow-intractable-games.md`).
- Selesnya v2 and EDF keep their cast-only producer `DEFECT` lines (the mana agent's area).
- No slower game, so no `--depth 8 --budget-ms 0` repros were needed. Outputs: `build/scratch/snowcp/ab/`,
  `build/scratch/snowcp/ab2/` (report.txt in each).

### 4.3 `MTG_RESCUE_TAP_SOURCE` adoption A/B -- BUILT, NOT RUN (deferred 2026-11-14)

USER asked to test the flag ("Let's test it out?"), with the searched depths as the bar ("searched depths are more
important than depth 0"). The apparatus is ready in `build/scratch/snowcp/rts/`:
- `rts.manifest.json` is the byte-identity screen. It covers the 14 suite decks that can reach the filter rescue (a filter
  source, a filter rock in hand, a mana land aura, or Three Tree City: Alchemist, Breaching Dragonstorm, Bruna, EDF,
  Fluctuator, Fungus, Galadriel, Goblins, Hinata, Jumbo Cactuar, Morph, Niv, Treasure Hunt, Unpredictable Cyclone).
  Each plays 100 games at play settings and 100 at d0, seed 95000, flag off vs on, fix (b) on in both arms.
- `mkmain.py` builds the ONE pooled main batch from the decks and axes the screen moved. Snow d5 b20 and d3 b10 get
  1500 games per arm (seeds 102000+ / 100000+), Snow d0 2 x 1000 (91000 / 92000). Each moving deck gets 600 games
  at play settings (104000+) and 500 at d0 (105000). All seeds are held out from every tier, 9001-9004, 61000 and 81000.
- Report: `build/scratch/snowcp/tools/rts_report.py <dir> main`.

The parent stopped the chain before the screen played a game, because fix (b) failed regression (Snow d5 gi78 /
gi101). This A/B waits until fix (b) is amended, and must then run on the amended binary, not `mtg.ab`.

## 5. The regression the tier found, and the amendment (`MTG_EXEC_SITE8_CONT_INLINE`, default ON)

The integrated regression tier (morph-integ-with-snow 2ae033cdb) found fix (b) slower on Snow d5 s2002 gi78 (6 -> 7)
and gi101 (8 -> LOSS), neither recovering at `--depth 8 --budget-ms 0`, and on d0 s2002 gi242/609/784 (6 -> 7,
recovering at d8 b0). Reproduced single-game on this branch (`build/scratch/snowcp/rer/`).

**Mechanism (traced, not inferred): an executor/rollout lockstep break that the cap exposed. The cap itself is
sound.** gi101 T8 (the max turn), committed line `[Scrying Sheets(act), Rimefeather Owl(act)x3, Scrying Sheets(act)]`
with `bp_choice=0@0`, verified as the T8 kill at T5 (d5 b20) and at T1 (d8 b0):

1. Both worlds cap the Owl at x1 (`want=2 ret=0`, owed = both looks), so both looks stay payable.
2. The first look finds Abominable Treefolk, which is now PLAYABLE (mana is left), so breakpoint site 8 opens in
   both worlds, and both pick the same searched continuation, candidate 0 = `[Owl x2, Scrying Sheets]`.
3. The ROLLOUT's site-8 branch applies the continuation's activations inline
   (`apply_trailing_activations(extra.actions)`): Owl +2, three ice counters, the kill.
4. The EXECUTOR (`resolve_draw_breakpoint`) routed them through `exec_continuation_activations`, which QUEUES while
   `main_trailing_done` is false -- and the main trailing pass clears that queue, unrun, when it ends. The Owl's
   two extra activations never happened; the opponent was left at 1.

gi78 T6 is the same shape (`[Owl x3, Augur, Sheets, Sheets]`, cap x1, the look finds Marit Lage's Slumber, the
continuation `[Slumber, Sheets, Owl x3]` cast the Slumber and lost its activations). With the reserve OFF the Owl
extras ate the mana, the find was not playable, no breakpoint opened, and the bug was never reached -- which is
why fix (b)'s own A/B (seeds 61000 / 81000) saw nothing. The brief's caveat is answered too: depth DID take effect
at d8 b0 (`searched_depth=8 verified=1` at T1); the search found the same kill line and the executor dropped it, so
no budget could recover the game.

So the answer to "is the cap removing a line the search wanted?" is no: the search's line was the capped line, it
was a real kill, and the realisation lost it. The cap only bites when a later named step in the same pass is owed
and payable; here it was, and the search used the freed mana deliberately (look, find, re-activate the Owl).

**Amendment.** `resolve_draw_breakpoint` captures `rdb_site_activated` on entry (set only by site 8, inside the
trailing pass; every caller sets it, and a nested cast-armed site resets it, hence the capture) and applies the
continuation's activations at once via `exec_trailing_activations`, as the rollout does. Site 9 (after the pass)
already applied at once. Reads no budget; the same at every budget. Lever `MTG_EXEC_SITE8_CONT_INLINE` (heurarm
per-job; =0 restores the queue). This is a pre-existing executor defect that also fires with the reserve OFF
whenever a committed site-8 continuation carries an activation, so it can move reserve-off play too (arm `inl`).

Checks (commit 2dbb7abe9): gi78 7 -> 6, gi101 LOSS -> 8 at d5 b20 AND at d8 b0; reserve off + inline on: both
unchanged from the committed tree (6, 8). New scenario `snow_site8_cont_acts_inline` (gi101 T8) PASSES, FAILS at
`MTG_EXEC_SITE8_CONT_INLINE=0` (no win, opponent at 1); `snow_owl_repeat_extras_keep_owed_sheets` still passes.
Unit 2971/2971, scenarios 514/514.

### 5.1 Pooled held-out A/B (binary 0618b9848, snapshot `build/scratch/snowcp/mtg.ab3`)

ONE pooled batch, 1299 jobs / 17,775 games, 24/24 workers busy, 06:18-07:07 UTC (`build/scratch/snowcp/ab3/`,
manifest `tools/mkman3.py`, report `tools/ab3_report.py` -> `ab3/report.txt`). Arms: `base` = the committed tree
(reserve 0, inline 0); `new` = reserve + inline (the shipped defaults of 2dbb7abe9); `nd` = `new` +
`MTG_D0_SNOW_PUMP_CREDIT`; `inl` = inline only (Snow d5 held-out). Paired per game against `base`; a no-win counts 99.
Held-out seeds 110000+ (d5), 112000+ (d3), 114000+ (d0), 116000-118000+ (other decks).

| cell | n/arm | base | new | nd | inl | new f/s | nd f/s | play-changed new/nd | units new/nd |
|---|---|---|---|---|---|---|---|---|---|
| **Snow d5 b20** held-out | 1500 | 5.6920 | 5.6913 | 5.6913 | 5.6913 | 1/0 | 1/0 | 43/42 (inl 7) | 0.9988/0.9985 |
| **Snow d3 b10** held-out | 1000 | 5.7270 | 5.7250 | 5.7250 | | 2/0 | 2/0 | 35/35 | 0.9995/0.9992 |
| Snow regression d5 s2002 | 150 | 5.6267 | 5.6267 | 5.6267 | | 0/0 | 0/0 | 7/7 | 1.0001/0.9999 |
| Snow regression d5 s3003 | 150 | 5.7000 | 5.7000 | 5.7000 | | 0/0 | 0/0 | 2/2 | 1.0001/1.0000 |
| Snow regression d3 s2002 | 25 | 5.6800 | 5.6800 | 5.6800 | | 0/0 | 0/0 | 1/1 | 1.0000/1.0000 |
| Snow d0 held-out | 1000 | 6.1580 | 6.1580 | 6.1560 | | 0/0 | 2/0 | 13/14 | (d0: no units) |
| Snow regression d0 s2002 | 1000 | 6.1590 | **6.1620** | 6.1590 | | 0/**3** | 0/0 | 14/12 | |
| Selesnya v2 (d6 b20) | 300 | 4.6133 | 4.6133 | | | 0/0 | | 0 | 1.0000 |
| EDF (d5 b20) | 300 | 4.0400 | 4.0400 | | | 0/0 | | 0 | 1.0001 |
| Kithkin (d6 b30) | 300 | 4.4833 | 4.4833 | | | 0/0 | | 0 | 1.0000 |

Reading:
- **Searched Snow: no slower game in any arm** (2500 held-out + 325 regression games), so no `--depth 8
  --budget-ms 0` repro was owed. d5 -0.0007 and d3 -0.0020. The d5 faster game also appears in `inl`, which
  changes only 7 games: it comes from the executor fix alone (a game the committed tree did not win now wins).
  The regression d5 s2002 cell is back at the committed GT (5.6267): gi78 and gi101 recover.
- **`new` alone fails the bar at d0**: regression d0 s2002 is +0.0030 (the three known games gi242/609/784,
  6 -> 7, nothing faster). The held-out d0 sample is 0/0.
- **`nd` clears the bar on every deck and axis**: searched as `new`, d0 held-out -0.0020 (2 faster, 0 slower),
  regression d0 0/0. Selesnya v2 / EDF / Kithkin are play-identical (0 games changed) in `new`; `nd` is inert
  there (no Owl).
- Units: 0.998-1.000 on every searched cell.

Decision: `MTG_D0_SNOW_PUMP_CREDIT` flipped to DEFAULT ON (it earned it here, and without it the package is net
+3 on the d0 regression cell). Surfaced to the parent/USER; `=0` restores the projection without the credit.
