# treasure_hunt: cast-order draft (agent proposal, 2026-10-14; for USER review)

The list has 53 lands and three nonland cards: 4 Treasure Hunt `{1}{U}`, 2 Land's Edge `{1}{R}{R}` and
1 Throes of Chaos `{3}{R}` (Cascade, Retrace). Card text is taken from cards.json.

## 1. Status: **(D) generic tiers only** for casts; the land drop follows a USER rule that ships ON

- `TreasureHuntProvider` (DecisionProviders.h:729-793) has no `CastOrderRank`, `OrderOpaqueCastsByRank`,
  `MainPhaseOverride` or `CastOrderRankLatest`. There is no `MTG_TH_*ORDER` flag (the TH flags are
  DIG_PRUNE, DISCARD, DISCARD_WIDTH, KEEPFLOOR, LEGACY_SCRY, RETRACE_RANKED, STRICT_FLOOD and
  TOWER_SPARE), and cast-order-rankings.md has only the raw report. **No USER cast order exists, so
  none is OFF.**
- **The one binding USER ordering rule covers the land drop:** *"don't play a land BEFORE TH/Throes;
  the after-play is a search decision"* (2026-07-15/16, th-keep-model-overmulligans-th-hands.md:226).
  It ships default ON as `MTG_TH_STRICT_FLOOD` (`THStrictFlood`, DecisionProviders.cpp:6211, read in
  `ShouldCastDrawEngine` :7311). The proposal keeps it unchanged.
- History: bp-greedy-continuation-deletion.md:217 lists treasure_hunt's residual canon-vs-greedy
  differences as *"ALL pure pick-order differences (Land's Edge-vs-Treasure Hunt-vs-Throes) ... each a
  cast-order review item"*. Nobody has reviewed them until now.

## 2. What plays today

| pos | card(s) | main | rule |
|---|---|---|---|
| land | land drop | m1 | At depth > 0 the drop is folded into the search. The strict-flood gate refuses TH/Throes once the drop is spent unless Land's Edge or Reliquary Tower is in play, or TH + Land's Edge are payable this turn. At depth 0 an engine rule (AIEngine.cpp:3208-3236) defers the drop whenever TH is castable before the land and no Tower/Land's Edge is out. |
| 20 | Treasure Hunt, Land's Edge, Throes of Chaos **(3-way TIE)** | "m2" (inert) | Generic "other noncreature" tier (DecisionProviders.cpp:3184). **The rank is never read.** TH (`DrawUntilNonland`) and Throes (`cascade_max_mv`, `retrace`) are ORDER-OPAQUE (ManaPayment.cpp:2722), and the provider does not opt in, so every set holding either card keeps PLAN order, which is HAND order. Only {Land's Edge, Land's Edge} reaches the rank sort, and those are identical cards. `MTG_PAYABLE_ORDER` reorders only when hand order cannot pay. |
| last | Throes of Chaos by retrace (from the graveyard) | m1 | Fixed by the engine: cast after every hand cast (TurnSolver.cpp:42966, AIEngine.cpp:6576). |
| after casts | dig (cycling: Sandbar/Cave/Remote Isle; Fiery Islet sac-draw) | m1 | Fixed by the engine: spends the surplus after the casts (TurnSolver.cpp:44412, AIEngine.cpp:6853), pruned to the deck's gate (`MTG_TH_DIG_PRUNE`, USER 10-14). |
| end of main | Land's Edge activations | - | Fire after the casts and digs (heuristic count plus the hold hooks). |

The main column is **inert**: `uses_second_main=no`, so `MainPhaseFilterActive` returns false
(TurnSolver.cpp:20388). This is a single-main deck, and every cast happens in main 1.

## 3. Proposed order

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| land | land drop | m1 | - | **After the draws** whenever TH/Throes is payable without it. This is the USER rule, and it is unchanged: the drop can then be a Reliquary Tower that TH reveals. It goes before the draws only when the draw needs the drop's mana and an outlet makes spending it safe, which is what the gate already encodes. The search owns it at depth > 0. |
| 1 | Land's Edge (x2; tie moot) | m1 | - | It is the outlet, and "Discard a land card: 2 damage" costs no mana. TH's reveal cannot make casting it wrong, because it is the deck's only damage. Cast first, its `{1}{R}{R}` is already paid when TH opens a re-plan, so the re-plan cannot spend that mana on a revealed TH. Every post-draw hook also sees its rate on the battlefield (`HoldDeferredDropForLethal` :7420, gate clause 1, the lethal model). This is principle 3: the enabler before the engine that feeds it. The copies are identical, and a second one adds no damage. |
| 2 | Treasure Hunt (x4; tie moot) | m1 | - | The cheap draw goes before the 4-mana Throes (principle 1: information before the bigger commitment). The copies are identical and the breakpoints treat them alike. |
| 3 | Throes of Chaos (cast from hand) | m1 | - | It goes after TH. Cascade hits TH (mv 2) or Land's Edge (mv 3) and bottoms the lands it passes. Non-clairvoyantly, with 6 mana and no Land's Edge in hand, the chance of having Land's Edge online this turn is **0.70 with TH first vs 0.40 with Throes first** (assuming 3 TH + 2 Land's Edge left in the library), with the same expected land count. TH first can spend the 4 mana on a revealed Land's Edge (3) instead, and keeps Throes. Throes first strands a cascaded TH's Land's Edge in hand. |
| 4 | Throes of Chaos (retrace) | m1 | - | Engine-fixed (last). This is consistent with the order above, so no change. |

No ranges. Every position is weakly dominant on any board, so each range would collapse to a point.

## 4. What changes and why it matters

1. **The ranks start to count at all.** Today hand order picks, and with `MTG_BP_NEW_ONLY` (default ON;
   a continuation does not re-offer a card held before the draw) only ONE of {TH then Throes, Throes
   then TH} is reachable per hand, at every budget.
   - Probe B1 (T5, 6 mana, opponent at 18, library `I I LE I I I I I`): hand [TH, Throes] wins
     **T6** at d3/b10, d5/b20 and **d8 unlimited**, while hand [Throes, TH] wins **T5**. The cards
     are the same; only the hand order differs.
   - **B1 is a clairvoyant win.** It is only better because the library is known. The proposal
     (TH first) gives it up on both hand orders, which is the USER's 09-21 and 10-13 ruling
     (*"I don't care about how effective our clairvoyance is"*; a clairvoyant win "gets no fix").
   - The mirror, B2 (`I I I I I LE I I TH`), wins T5 under both hand orders today: a lone TH plus
     its continuation casts the drawn Land's Edge.
   - Expect the clairvoyant GT to read slightly worse on B1-shaped games.
2. **Land's Edge before Treasure Hunt: a weak-dominance call, with no loss found in the search.**
   Probes A (hold the drop for exact lethal), A2 (the line needs the drop's mana) and E (TH reveals a
   second TH that would eat Land's Edge's mana) give identical win turns for both hand orders at
   d3, d5 and d8. The searched continuation compensates. The value lies in the greedy layers that
   read Land's Edge off the battlefield: rollouts past the horizon, the `bp_choice -1`
   continuation, and the flood-keep fallback (AIEngine.cpp:5120). There, TH first leaves
   `HoldDeferredDropForLethal` blind. It is an ordering that cannot lose, not one that has shown a
   loss.
3. **A depth-0 land-drop defect (found by the probes; this is not a rank change).** A2 at d0, T4,
   4 lands, hand TH + Land's Edge + 3 Islands:
   - The engine rule defers the drop because TH is castable.
   - The greedy then casts **Land's Edge alone**.
   - The held drop is replayed only by the second pass, which only a cast draw engine triggers
     (AIEngine.cpp:6895). So **no land is played on T4** and the game wins **T5**.
   - d3, d5 and d8 win **T4** (land, Land's Edge, TH).
   - This is the same lost-drop class the Creature Giving tutor-defer fixed with
     `m_tutor_deferred_drop` (AIEngine.cpp:6576-6600; losing the drop outright cost Creature Giving d0 +0.32). It affects the
     shipped d0 cells.
4. **Unchanged:** the land drop, the retrace position, the dig position and Land's Edge firing.

Probes are in `build/scratch/castorder/probe_treasure_hunt/` (generated by `probe_th_make.py`, run by
`probe_th_run.sh`, read with `probe_th_show.py`). Each pair differs only in hand order. The dead
filler is Gideon `{2}{W}{W}`: it stops TH, and cascade skips it.

## 5. Questions for the USER

1. **In a main that casts both, Land's Edge before Treasure Hunt (and before Throes)?** Default:
   **yes**.
2. **Treasure Hunt before a hand-cast Throes, accepting that clairvoyant Throes-first wins (B1) become
   unreachable?** Default: **yes** (0.70 vs 0.40 for Land's Edge online, blind).
3. **Ship as `MTG_TH_ORDER` default ON (=0 restores hand order)?** It must also opt the deck into
   `OrderOpaqueCastsByRank`, or the ranks stay unread. Measure on TH's smoke/regression cells with the
   value sidecar and keep table in place. Default: **yes**.
4. **Move the d0 "TH before land drop" rule out of AIEngine into TreasureHuntProvider, and replay the
   held drop when TH is not cast?** That is a deck rule living in the engine (USER 10-14), plus the
   §4.3 defect. It would be its own measured change. Default: **yes**.
5. **Label every cast m1 (single-main deck, labels inert)?** Default: **yes**, as a cosmetic change only.

## 6. Generic-order findings

- **`OrderingOpaque` leaves the generic rank inert for a deck whose spells are all draws or cascades,**
  so hand order decides, and under `MTG_BP_NEW_ONLY` that is a hard prune between two old opaque
  cards. The global remedy was measured and parked: `MTG_ORDER_OPAQUE`, arm B,
  **-0.00236/game held-out** (cast-order-ideal-with-ranges.md, HELD-OUT table). Four providers opted in
  instead. That measurement is worth repeating for the default.
- **A draw-yield outlet (`discard_land_damage`) ties with the draws at 20.** It should precede them,
  for the mana-protection and hook-visibility reasons in §3.
- **Cascade vs a plain draw:** a cascade cast (`cascade_max_mv`) after the turn's cheap draws is the
  better blind order. The draw first gives information, and the cascade's own hit is random either
  way.
- **Report:**
  - It prints `m2` for a single-main deck. It could print "m1 (single main)".
  - The `[2..20]` range on TH/Throes is display-only while the draw promotion is off.
  - The rank is not what plays for an opaque deck, and the report does not say so.

## 7. Implementation notes

- `TreasureHuntProvider::CastOrderRank`, gated `EnvOn("MTG_TH_ORDER", true)` with a heurarm slot; off
  falls through to `DeckProvider::CastOrderRank`. Rank by params (house style):

  | rank | param test | card |
  |---|---|---|
  | 12 | `discard_land_damage > 0` | Land's Edge |
  | 14 | `tmpl == CardTemplate::DrawUntilNonland` | Treasure Hunt |
  | 16 | `cascade_max_mv > 0` (or `retrace`) | Throes of Chaos |

- `OrderOpaqueCastsByRank()` returns the same flag. It is required: ManaPayment.cpp:2755, read by both
  the executor (AIEngine.cpp:6392) and the rollout (TurnSolver.cpp:42879), so the two stay in
  lockstep. It also enters `ApplyCastOrderRangeLadder` (ManaPayment.cpp:3943) and
  `ApplyEnablerWipeRecheck`. Both are inert here: the override ignores the draw tier, so
  `CastOrderRangeOf` collapses every range to a point, and the deck has no wipe.
- Add `CastOrderTierName` labels for 12/14/16.
- **Engine limits, accepted:**
  - Retrace Throes is a `CastFromGraveyard`, which is always applied after the hand casts and is not
    rank-addressable. That is consistent with the proposal.
  - A scalar rank cannot express "Throes first when the top card is a KNOWN nonland". That happens
    when the drop played this main was a Temple/Falls whose scry or surveil kept a TH or Land's Edge
    on top, so the cascade casts it free while TH would spend 2 mana to draw one card. It is rare,
    and it only arises when the drop precedes the draw, which the USER rule mostly forbids.
- **§4.3 fix (separate change):**
  - Move the AIEngine.cpp:3208-3236 block, which keys on `DrawUntilNonland`, `no_max_hand_size` and
    `discard_land_damage`, into a provider hook.
  - Set the held-drop replay (the `m_tutor_deferred_drop` path) when the main casts no draw engine.
  - Measure it on the TH d0 cells.
- Like any order change, this ages the value leaf (`leaf: none`) and the keep table only through play,
  so measure with both sidecars in place.
