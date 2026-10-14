# treasure_hunt: the d0 "Treasure Hunt before the land drop" rule moves into its provider, and stops losing the drop

Two local commits on 2026-10-14, as asked: a byte-identical move, then a measured fix. Source of the
finding: `docs/design/cast-order-drafts-2026-10-14/treasure_hunt.md` §4.3 and §5 Q4.

## 1. The rule and where it lived

treasure_hunt is 53 lands, 4 Treasure Hunt `{1}{U}`, 2 Land's Edge `{1}{R}{R}`, 1 Throes of Chaos. The
USER's land rule for it is *"don't play a land BEFORE TH/Throes; the after-play is a search decision"*
(2026-07-15/16, `th-keep-model-overmulligans-th-hands.md`). At depth > 0 the search owns it: the land fold
carries a defer option, and `ShouldCastDrawEngine`'s strict flood gate (`MTG_TH_STRICT_FLOOD`) refuses the
flood engine once the drop is spent. At depth 0 there is no search, so the rule was a block of deck logic
inside `AIEngine::TakeTurn`'s pre-combat land code (since 2026-06-10): when a Treasure Hunt in hand is
castable without the drop and no Reliquary Tower / Land's Edge is out, hold the drop, so a land the dig
reveals can be the drop. The second TakeTurn pass plays it.

## 2. Commit 1: the move (byte-identical)

* `DecisionProvider::LandDropAfterDrawEngine(state, controller)`: base `false`, so every deck plays its
  drop first. It is asked at the same point as the old block, ahead of `LandDropAfterHandLandTutor`
  (Creature Giving's Sylvan Scrying defer, the precedent).
* `TreasureHuntProvider::LandDropAfterDrawEngine` returns today's rule. The engine block priced the cast
  as a literal `{1}{U}`. The provider reads the card's own cost, which is the same `{1}{U}`: Treasure
  Hunt is the only `DrawUntilNonland` card in cards.json, and only treasure_hunt runs it.
* **Identity proof.** I ran the new binary against `84682b0a`, one pooled `--batch` per binary. That
  covered all 33 suite decks x {d0, d3 b10, shipped}, where shipped means no depth key, so `value_play`
  owns the depth at its own budget. Seeds were 10600001+, 28,160 games per arm, with treasure_hunt at
  4000 / 400 / 200. **99/99 jobs identical**: the aggregate digest, and every per-game win turn and game
  digest in the `.wins` logs. No job averages 9, so cards.json loaded. Unit tests 1031/1031.

## 3. The lost drop

Cast-order probe A2 (`build/scratch/castorder/probe_treasure_hunt/`), T4, d0:
- Board: four lands (Island, Steam Vents x2, Thundering Falls).
- Hand: Land's Edge, Treasure Hunt and four Islands. The opponent is on 18.
- The rule holds the drop, because Treasure Hunt is castable.
- The d0 greedy then casts **Land's Edge alone** (3 of 4 mana).
- The held drop was only ever replayed by the draw engine's second TakeTurn pass, which only a CAST
  draw engine requests (`return cast_draw_engine`). So **no land is played on T4**, and the game wins
  **T5**. d3, d5 and d8 play land, Land's Edge, Treasure Hunt and win **T4**.

Census on the committed tree (2,000 d0 games, seeds 10900001+): **109 turns** started main 1 with a land
in hand, played no land all turn, and did not win. **All 109** cast Land's Edge alone. **Zero** were the
other conceivable shape, where Treasure Hunt is cast and the drop is still lost; there the breakpoint
continuation or the second pass plays it.

## 4. Commit 2: the fix

`m_draw_deferred_drop` is set when the hook holds the drop. After the main's casts, beside Creature
Giving's `m_tutor_deferred_drop` replay (the precedent, and the same lost-drop class: losing that drop
cost Creature Giving +0.32 at d0), the held drop is **played** if:
- this pass cast **no** draw engine,
- the drop is still open, and
- the provider does not hold it as the marginal Land's Edge ammo for a lethal this turn
  (`HoldDeferredDropForLethal`, the deck's existing hold).

A played drop requests the second pass, which re-solves with its mana. In A2 that pass casts the
Treasure Hunt the land now pays for.

A main that DID cast a draw engine is untouched (today's route). `MTG_DRAW_HELD_DROP_REPLAY`
(`EnvOn(..., true)`, heurarm slot `DRAW_HELD_DROP_REPLAY`) is default ON. `=0` is the old behaviour,
proved byte-identical below. The fix is depth-0 executor code, so it is the permitted greedy (the d0
runner) and not a search-window pick. It only realises a drop the provider's hold had promised to play
later.

Unit (`test/unit/test_th_held_drop.cpp`, 4 cases):
- The hook's four verdicts.
- A2 with the lever on: land played, Treasure Hunt cast on T4.
- A2 with the lever off: the defect.
- The lethal-ammo hold stays held.
- A main that casts Treasure Hunt is identical in both arms.

On the commit-1 tree the A2 case FAILS (2 assertions: `lands_played_this_turn == 1`, Treasure Hunt not
in hand). After the fix, 1035/1035 pass. The probe itself, end to end: A2 d0 wins **T4** (lever on) vs
**T5** (lever off).

## 5. Measurement

Everything ran in **one pooled `mtg --batch`** on the new binary: 362 jobs, 268,320 games, 7 workers,
heartbeat 7/7. Both arms ride the per-job lever (`"flags": {"MTG_DRAW_HELD_DROP_REPLAY": false|true}`).
The batch had three parts:

**Lever identity (against the old binary's identity run above, same 99 jobs):**
- Lever OFF: **99/99 identical**. The `=0` arm is the old code.
- Lever ON (the shipped default): **98/99 identical**. Only `th_d0` moved. Every other deck, and
  treasure_hunt's own d3 b10 and shipped cells, are byte-identical.

**Paired A/B, treasure_hunt.** Held-out seeds: d0 from 10700001, d3 from 10800001, shipped from
11100001. Each chunk's base is `S0 + i*N`, so no seed repeats. A loss counts as turn 9, and delta =
on − off.

| cell | games/arm | off avg | on avg | delta/game | net turns | faster | slower | play-changed, same turn | t |
|---|---|---|---|---|---|---|---|---|---|
| d0 | 100,000 | 5.5266 | 5.5216 | **−0.00500** | **−500** | 1074 | 337 | 4196 | −8.83 |
| d3 b10 | 4,000 | 4.1100 | 4.1100 | 0 | 0 | 0 | 0 | 0 | — |
| shipped (value_play d5, b20) | 2,000 | 4.0820 | 4.0820 | 0 | 0 | 0 | 0 | 0 | — |

d3 b10 and shipped: 16/16 chunk digests are identical. The fix is inert above depth 0: the rule's block
runs only when the land is not folded into the search, and the replay checks `m_lookahead_depth == 0`.
Cost at d0 is noise (307 vs 288 thread-s on a contended box).

**What the replayed drop bought.** I logged 24,000 of the d0 games in the ON arm and joined them with
the pooled deltas:

| the replayed land, that main | faster | slower | net turns |
|---|---|---|---|
| funded a cast after it (the A2 shape) | 115 | 12 | **−90** |
| funded nothing that main | 137 | 83 | **+15** |

The gain is the A2 shape. A replay that buys nothing this main is roughly neutral: it adds mana for
later turns, but it costs one land of Land's Edge ammo (§6, §7 Q1).

## 6. Slower games and their recovery

All 337 slower games are d0. By old turn → new turn:

| old → new | 4→5 | 4→9 | 5→6 | 5→7 | 5→8 | 5→9 | 6→7 | 6→9 | 7→8 | 7→9 |
|---|---|---|---|---|---|---|---|---|---|---|
| games | 4 | 1 | 123 | 56 | 7 | 60 | 48 | 33 | 2 | 3 |

**Recovery (USER two-stage check, one pooled batch per stage, the fix ON):**
- **Stage 1** (`--depth 5 --budget-ms 100`, 5 = the shipped depth): **336/337 recover**, i.e. the
  search wins at or before the old arm's turn. Searched win turns: T3 128, T4 158, T5 47, T6 3.
- **Stage 2** (`--depth 8 --budget-ms 0`): the remaining game, **seed 10723385, gi 23384** (d0 old T7,
  new T8), gives **T8**. Root cause below.

**The slowdown mechanism (the same in every game read: seeds 10700050, 10700429, 10723385).**
- The rule held the drop on a turn where Treasure Hunt was castable.
- The greedy cast Land's Edge with every untapped land, so the replayed land funded nothing that main.
- With Land's Edge out, a land in hand is 2 damage. The old lost drop had kept it as ammo by accident.
  One or two turns later, Treasure Hunt / Throes reached *exactly* lethal with it, and is now one land
  (2 damage) short.
- Examples:
  - **10700429:** the T5 kill becomes T6.
  - **10700050:** T5 becomes a loss. The board is left at 16 life with a 7-land hand. The d0 Land's
    Edge policy (`LandsEdgeHeuristicFireCount`) fires everything only when lethal, otherwise only the
    excess over 7 cards. Meanwhile the d0 runner keeps playing a land every turn (T6-T8: three lands,
    each worth 2 damage). So the hand never grows past 14 damage, and the game is lost.
- Measured over the 31 win-to-loss games inside the 24,000 logged ones: **all 31** end with Land's
  Edge out and the hand's lands short of lethal, and **17** of them hold a full 7-card hand (the stall
  above). See §7 Q2.

**Seed 10723385, the one game the search does not recover.**
1. d0: on T4, Land's Edge took all three lands, and the replayed Frostboil Snarl funded nothing. On T7,
   Throes cascaded into Treasure Hunt for 14 against 16, where the old arm had exactly 16.
2. Why the search misses the T7 line:
   - d5 b100 and d8 b0 both win **T8**, identical in both arms (the fix is inert above d0).
   - With the provider prunes opened (`MTG_UNPRUNED=1`), d8 b0 wins **T7**. I bisected the gates
     (`drawengine`, `dig`, `digresolve`, `digchain`, `ponder`, `fetch`, `xspell`, `mainphase`,
     `groupcap`, `tapreserve`, `altpayload` all stay T8): **`MTG_UNPRUNE=searchorder` alone wins T7**.
   - So the T7 line needs a cast order the shipped search never tries. treasure_hunt's casts are
     order-opaque (Treasure Hunt, Throes) and the deck does not opt into `OrderOpaqueCastsByRank`, so
     every set keeps HAND order at every budget. That is the cast-order draft's §4.1 / §6 limitation
     (its Q3, `MTG_TH_ORDER`), not something this change introduced. The d0 runner found T7 only
     through the lost drop's accidental ammo.

## 7. Open questions for the USER (defaults taken, nothing blocked)

1. **Replay the held drop only when it funds a cast that main?** (Land timing, USER-owned.) The fix
   ships as asked: the held drop is always played when no draw engine was cast. The exceptions are the
   provider's lethal-ammo hold, and an empty hand or a drop already used. The 24,000-game split (§5)
   says the whole gain comes from replays that fund a cast that main (−90). Replays that fund nothing
   are roughly neutral (+15: 137 faster, 83 slower). An amended rule, "with Land's Edge out, hold a
   drop that funds no cast this main as ammo", would likely add a little more. I have not built or
   measured it. **Default taken: play it** (the requested behaviour).
2. **The d0 runner plays a land every turn even with Land's Edge out and nothing to cast.** Found, not
   fixed. On seed 10700050, T6-T8 played three lands while holding a 7-land hand against 16 life, and
   lost. `TreasureHuntProvider` does not override `PreferHoldLandDrop` (BurnProvider does, for its own
   reason). A deck rule there ("a land that funds nothing is 2 damage in hand") is the USER's call.
   **Default: unchanged.**
3. **Throes of Chaos is not in the d0 hold.** The USER's rule names both ("don't play a land BEFORE
   TH/Throes"). The depth > 0 strict-flood gate covers both, but the d0 rule (2026-06-10) holds the
   drop for Treasure Hunt only. **Default: unchanged** (Treasure Hunt only, byte-identical to the
   move).
4. **The lethal-ammo exemption is my addition.** The replay honours `HoldDeferredDropForLethal`, the
   deck's existing post-Treasure-Hunt hold, so a drop that is the marginal Land's Edge ammo for a lethal
   this turn stays held (unit-tested). **Default: on.**
5. **Seed 10723385 is a concrete case for the cast-order draft's Q3.** The shipped search loses T7 to
   hand order (`MTG_TH_ORDER` + `OrderOpaqueCastsByRank`). No action here.

**Ground truth.** Not touched. Every treasure_hunt **d0** cell will move (smoke `th_smoke_d0_s1001`, the
regression d0 cells, the overnight d0 cells). Every other deck, and treasure_hunt's d3/d5 cells, are
byte-identical. Smoke and regression need a run and an accept by whoever integrates. The overnight GT
is left for the periodic overnight run.

Scratch (build volume): `build/scratch/th-land-provider/`, with binaries `bin/mtg.{base,c1,c2}`,
`identity.json`, `pool.json` (+ `ab.json`), `pool_c2/`, `rec1/`, `rec2/`, and the probe
`probe/A2_*.json`.
