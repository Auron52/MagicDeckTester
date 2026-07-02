# Treasure Hunt T4-combo misplay (gi627) — FIXED

Self-contained finding + the shipped fix (2026-07-02). A genuine search-quality misplay on the
Treasure Hunt deck, verified end-to-end via human-play. **Not** a clairvoyance artifact.

## FIX SHIPPED (2026-07-02)

Root cause of the missed T4: the search's win model (`TreasureHuntProvider::ExtraLethalDamage`) did
NOT recognize that casting a **cascade card (Throes of Chaos)** whose cascade target is **Land's
Edge** brings Land's Edge onto the battlefield for free, making the lands already in hand lethal
ammunition THIS turn. So the search never valued setting up the T4 Throes combo, and picked a
develop-now line that burned the depletion charges the combo needs.

Fix: `ExtraLethalDamage` now credits `lands_in_hand * rate` when a plan casts a cascade card and the
first cascade-hittable nonland in the library (first nonland with MV < `cascade_max_mv`, skipping
lands) is a Land's Edge. Simulation stays the win arbiter, so the optimistic projection only steers
move-ordering / the win-now check toward the line — it never commits a phantom win. Gated
`MTG_NO_CASCADE_LETHAL` (default on). Only the TH deck has cascade + Land's Edge, so every other deck
is byte-identical.

Effect: gi627 T5→T4 (now holds the T2 Hunt to preserve Saprazzan's depletion charge → T4
TH+Throes → cascade Land's Edge → lethal — a reasonable, non-clairvoyant line). Validated strictly
positive: smoke d0 = 2 loss→win + 28 faster + 0 worse; regression = 5 searched faster + 16 d0 faster
+ 0 worse / 0 lost; all other decks byte-identical; audit shows no searched win→loss or turn-later.
Needs GT rebaseline (TH fingerprints improved) at smoke/regression/overnight.

The original diagnosis (below) framed the fix as "defer the T3 land for Reliquary"; the shipped
win-model fix reaches T4 by a different (also-correct) line, so the land-defer / no-redundant-dig
work is no longer required for gi627. Kept for context.

---

Original finding + fix direction:

## The game

`treasure_hunt`, overnight seed 5005, game 627 (single-game repro: `--seed 5632 --game-index 627
--depth 3 --budget-ms 80`). Autonomous engine wins **T5**; a competent human line wins **T4**.

The T4-win line (proven with `--claude-play` on the real base hand via
`--force-mulligan "3:8,28,38"`, choices `0,0,3,3,7,10` → `win_turn: 4`):

| turn | play |
|---|---|
| T1 | Saprazzan Skerry |
| T2 | cast Treasure Hunt; play Sandstone Needle |
| T3 | **defer the land**; cast Treasure Hunt ×2 (affordable off Saprazzan {U}{U} + Sandstone {R}{R} = 4 mana, no land needed); the 2nd Hunt draws Reliquary Tower; **play Reliquary Tower** as the deferred drop → no-max-hand-size → the ~28-card flood is KEPT (no cleanup discard) |
| T4 | play a land (Steam Vents); cast Throes of Chaos ({3}{R}) → cascade hits Land's Edge (first nonland MV<4 below Throes) → discard 10 lands → 20 damage → win |

## The engine's misplay

The autonomous line instead: T3 **plays Steam Vents up front** then casts 2×Hunt → draws 25 → the
drop is spent, so a drawn Reliquary can't be played → **discards 20 at cleanup** (throwing away
Land's Edge ammo AND Throes-combo pieces). Then T4 it **re-casts Treasure Hunt** (redundant — it
already flooded) which eats the mana Throes needs, so it can't assemble the T4 kill.

Two independent errors, both needed for the fix:
1. **Land sequencing** — it should DEFER the T3 land (the 2×Hunt is affordable without it, off the
   two depletion lands) so the post-draw keep-land logic (`play_drawn_flood_keep_land`, already
   present) can play the drawn Reliquary and keep the flood.
2. **Redundant re-dig** — once the flood is in hand (Reliquary in play, Throes castable for the
   cascade→Land's-Edge kill), it should NOT cast another Treasure Hunt on T4; that mana belongs to
   Throes.

## Why the search doesn't find it

- The defer plan IS enumerated (`add_for_land("", "")`), and the rollout DOES model the cleanup
  discard (`SimulateEndAndStartNextTurn`) and the flood-keep (`play_drawn_flood_keep_land`).
- But the `EnumeratePlansWithLand` "develop" tiebreak orders play-a-land ahead of defer, and
  FSLineWin commits the first in-horizon win in move order. The leaf ESTIMATES defer→T4, but the
  verified commit-the-line simulation of the defer line still casts the redundant T4 Hunt and
  therefore verifies defer→T5 — so no line commits T4. (Confirmed by an FSLineWin trace: `land=''
  -> tail_win=4` at the leaf, but deep verified search d5/b1280 = T5.)
- Root trigger is `BatchPrepayMainCasts` (commit f154d64): `MTG_NO_BATCH_PAY=1` → T4;
  depletion-reserve is irrelevant here. Batch-pay's whole-turn front-loading changes the mulligan
  bottoming AND the in-game sequencing so the T4 combo is never assembled. (Batch-pay is net
  strongly positive overall — this is one game it costs.)

## Fix direction (not yet scoped/implemented)

The correct fix is in the search VALUATION, not a forcing heuristic (per the mtg-ai skill's
"harden the valuation, don't pre-decide" rule — it even calls this exact Treasure-Hunt case out):
make the verified rollout prefer the defer+keep-flood line by (a) not casting a redundant
draw-until-nonland once a no-max-hand-size land is in play and the hand is already flooding, and/or
(b) valuing kept flood (Land's Edge ammo) over a wasted cleanup discard at equal win turn. Any
change here touches the whole TH archetype — validate with full smoke+regression and the per-game
audit gate (`test/audit_changed_games.py`), watching the cited counter-examples gi=881 / gi=67.

Interim: gi627's T5 is a legal, minor cost of a net-positive change; acceptable to leave until the
valuation fix is scoped. But it is a REAL misplay, not an artifact — record it as such.
