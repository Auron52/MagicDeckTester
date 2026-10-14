# RESUME -- Hinata2 settled cast order: implement + measure (paused 2026-10-14 ~13:10 UTC)

Self-contained resume note for the task "implement and measure the Hinata2 cast order the USER settled on
2026-10-14". The order, the Irencrag rule and the design are in `hinata-cast-order.md` section 0. Paused for the
night on the coordinator's request (a value-leaf generation needs the box); every process this task started was
stopped. Delete this file when the task closes and the results are folded into `hinata-cast-order.md`.

## Where things are

* **Base commit:** `ff3e205b` (tip of `work/integ-2026-10-14`). Worktree branch `worktree-agent-a4d121dd522b89155`.
* **Built:** commit `514975c9` -- the settled order behind `MTG_HINATA_ORDER_FULL` (still **default OFF**), the
  set-level Irencrag placement (`MTG_HINATA_IREN_SECOND_LAST`, new heurarm slot at the end; default ON under the
  order), `CastOrderTotalByName` / `RestrictorFollowsArrangement` root hooks, both walks counting in the arranged
  order, the waste gate reading the arranged follower (+ the pre-set accounting fix), the
  `OrderOpaqueCastsByRank` opt-in, tier labels, `MTG_HINATA_IREN_EARLY` deleted. Unit tests:
  `test/unit/test_hinata_cast_order.cpp` (6 cases). **Full unit suite 1059/1059 pass, both with the order off and
  with `MTG_HINATA_ORDER_FULL=1` in the environment.**
* **Binaries** (build volume, persistent): `build/scratch/hinata-order/bin/mtg.base` (built at `ff3e205b`),
  `build/scratch/hinata-order/bin/mtg.new2` (built at `514975c9`'s tree). Rebuild with
  `bash /workspaces/MagicDeckTester/build/scratch/build2.sh` from the worktree root if the volume was lost.
* **Scripts** (tracked): `test/tools/hinata_order/` -- `gen_ab.py` (paired A/B manifest), `run_batch.sh`
  (one pooled batch, cwd = worktree, `--cards-json` explicit, `MTG_DUMP_WINS=1 MTG_DUMP_UNITS=1`),
  `analyze_ab.py` (faster / slower / net / units per cell; `--slower-out` for the recovery step),
  `gen_recovery.py` (stage 1 `--depth max(5, old wt) --budget-ms 100`, stage 2 `--depth 8 --budget-ms 0`),
  `gen_identity.py` + `cmp_identity.py`, `gen_probe.py`, `onegame.sh` + `showgame.py` (one game with a full log,
  old vs new side by side). They were run from `logs/hinata-order/` (gitignored copies of the same files).

## Measured so far

**gi 664 re-probe (seed 6600665, the draft's case) -- DONE.** Shipped and d3 b10, one pooled probe per binary:

| arm | shipped | d3 b10 |
|---|---|---|
| generic (base binary, and new binary with the order off) | **T5** | **T5** |
| 08-28 order, Irencrag scalar 22 (base binary, `MTG_HINATA_ORDER_FULL=1`) | **unwon** | **unwon** |
| settled order (new binary, `MTG_HINATA_ORDER_FULL=1`) | **T5** | **T5** |
| settled order, scalar 18 only (`MTG_HINATA_IREN_SECOND_LAST=0`) | T5 | T5 |

The settled arm plays the generic arm's exact game there (same play digest `c94a2633065630a7`).

**Identity -- DONE except one job.** `gen_identity.py`: every suite deck except hinata at d0 50 / d3 b10 20 /
shipped 20 games (dragonstorm + dragonstorm2hg, the other Irencrag deck: 200 / 50 / 50), the new binary with
`MTG_HINATA_ORDER_FULL` ON; plus hinata and hinata2hg with the order OFF on both sides (d0 400 / d3 40 / shipped
20: "=0 restores today's behaviour exactly"). Seeds 11300001 + 1000*job. **104 of 105 jobs, 4,290 games: every
game byte-identical (win turn AND per-game play digest).** The one job not finished is `kittyv3_ship` (20 games):
its gi 14 had run > 0.3 h in BOTH binaries when stopped -- a slow KittyEquipment v3 game, not this change.

**Main A/B -- PARTIAL (1 of 6 cells complete).** `gen_ab.py --ship 1000 --d3 2000 --d0 20000` (hinata2hg at half):
one pooled batch on `mtg.new2`, old = `MTG_HINATA_ORDER_FULL` off, new = on, 36 jobs / 69,000 games, 16 threads
on a box at load 70-120. Stopped after ~22 min with:

| cell | n | old avg | new avg | faster | slower | net | play-changed | units new/old |
|---|---|---|---|---|---|---|---|---|
| hinata2hg shipped (d5 b30, value leaf + keep table) | 500 | 5.4220 | 5.4180 | 7 | 6 | **-2** | 128 | **0.969** |

(`hinata_ship_old_c00000` also finished -- 250 games, avg 5.6440 -- but its new-arm pair did not.)
The six slower 2HG games (seed = 11150001 + gi): gi 98 (4 -> 5), 290 (5 -> 6), 349 (5 -> 6), 425 (5 -> 6), 440
(4 -> 5), 484 (5 -> 6). **Recovery not run yet.** Two looked at single-game, old vs new:
* **gi 98** -- T4: the old arm casts Reality Spasm X5 -> Ponder -> (dig) Soulfire, then main 2 Spasm -> Ponder ->
  Spasm -> Gamble (Crackle) -> Spasm -> Crackle X2 for lethal. The new arm casts Ponder first (a plan of Ponder
  alone; {Spasm, Ponder} realises Spasm first in both orders), then Spasm x2, then Soulfire -> Ponder in main 2
  and wins T5. Different SEARCHED choice, not an inexpressible line -- expect it to recover at stage 1.
* **gi 440** -- T2 with Sol Ring, Gamble, Ponder, Preordain, Crackle, Soulfire in hand: the old arm casts Sol
  Ring -> Ponder (finds Hinata on top); the new arm casts Sol Ring -> Gamble, fetches Hinata, and Gamble's
  RANDOM discard throws that Hinata away (T5 instead of T4). The old line ({Sol Ring, Ponder}, Gamble later)
  is still expressible under the new order; whether the search can see the discard outcome is the question
  for the recovery stage. **This is the cost side of the agent's "Gamble before the cantrips" tie-break -- note
  it for the USER if more slower games share it.**

## To resume (in order; one pooled batch each)

1. **Re-run the main A/B in full** (the partial batch's finished 2HG-shipped jobs are reusable, but the rest
   must run anyway, so simplest is the whole manifest again -- it is deterministic):
   ```
   python3 test/tools/hinata_order/gen_ab.py --out build/scratch/hinata-order/ab1/m.json --ship 1000 --d3 2000 --d0 20000
   bash test/tools/hinata_order/run_batch.sh build/scratch/hinata-order/bin/mtg.new2 build/scratch/hinata-order/ab1/m.json build/scratch/hinata-order/ab2 16
   python3 test/tools/hinata_order/analyze_ab.py build/scratch/hinata-order/ab2/wins --slower-out build/scratch/hinata-order/ab2/slower.json
   ```
   (absolute paths under `/workspaces/MagicDeckTester/` -- the scripts hard-code the worktree path.) Seeds:
   hinata shipped 11100001, d3 11110001, d0 11120001; hinata2hg 11150001 / 11160001 / 11170001; chunks carry
   `game_index`. Measured cost on the loaded box: ~10-25 s thread-wall per shipped game, ~2 h for the batch.
   Check the `[batch] heartbeat` line reads 16/16 in the first ten minutes.
2. **Recovery** of every slower game, NEW arm, one batch:
   `gen_recovery.py <slower.json> --out r1.json --stage 1` -> run -> any that do not reach the old win turn go to
   `--stage 2` (d8 b0, unlimited -- can be slow on Hinata). Root-cause any that recover at neither (start with
   `onegame.sh` + `showgame.py`), and propose an amendment that keeps the USER's order (never the generic order).
3. **Identity's last job:** optional -- rerun `kittyv3_ship` alone on both binaries if a complete 105/105 is wanted.
4. **Bar:** per deck per cell (shipped, d3 b10, d0; hinata and hinata2hg) aggregate net <= 0 AND every slower game
   recovers at stage 1 or 2. If met: flip `MTG_HINATA_ORDER_FULL` to default ON (`EnvOn("MTG_HINATA_ORDER_FULL",
   true)` in `HinataOrderFullEnabled`, update the HeuristicArm.h comment), rebuild, rerun the identity hinata
   jobs to confirm =0 still restores today's play, then fold the table into `hinata-cast-order.md` section 0.
   If not met: leave it OFF and report the root causes + amendments. Do NOT touch `test/regression_gt.txt`.

## Open questions for the USER (surfaced, not blocking; defaults taken)

1. The two agent tie-breaks inside your order -- **Gamble before Ponder/Preordain**, **Preordain before Ponder** --
   are implemented as stated (default taken: yes). gi 440 above is one game where Gamble-first cost a turn
   (the random discard hit the Hinata it fetched); `MTG_HINATA_GAMBLE_LATE` / `MTG_HINATA_PP_STRICT` are the
   measurable reverses.
2. **Rule 3 of the Irencrag waste gate ("the payoff needed the float") is KEPT** under the position rule, with
   its "what the board has left" now charging every cast placed before the follower (so Soulfire -> Irencrag ->
   Opus is not misread as wasting the float). Default taken: keep it. Say if "always second last" was meant to
   replace rule 3 entirely.
