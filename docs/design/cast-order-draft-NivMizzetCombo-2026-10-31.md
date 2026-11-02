# NivMizzetCombo -- cast-order draft (2026-10-31, for USER review)

## 1. Status: **(D) generic tiers only**

* `NivMizzetComboProvider` (`src/ai/DecisionProviders.h:3343`) overrides none of these: `CastOrderRank`,
  `CastOrderRankLatest`, `MainPhaseOverride`, `OrderOpaqueCastsByRank`, `CastOrderTotalByName`/`TiesByName`,
  `ArrangeCastOrder` or `LandDropCastOrderRank`. Report: `provider: NivMizzetCombo`, ideal-order draw tier off
  (`build/scratch/castorder/reports/NivMizzetCombo.txt`).
* **No USER cast order exists, and none is OFF.** The deck's `MTG_NIV_ORDER_*` flags (all OFF) are LOOK orders:
  they decide how Ponder/Preordain stack the cards they keep. They do not order casts.
* **One agent-taken order rule is live and awaits your sign-off:** ledger Q11, "cantrip-first lets the Niv payoffs
  go first". `MayPrecedeCantrip`'s VALUE clause (`TurnSolver.cpp:29202`, by param) lets both Nivs, Curiosity and
  Tandem Lookout go ahead of the cantrips in searched play. It is recorded as *"TAKEN pending the user"*.
  The draft below keeps that rule and asks you to confirm it (Q2).
* Your stated expectation, *"didn't my order already have Sol Ring first for Niv? It should be"*, is not what
  plays today: §2 and §4.1.

## 2. What plays today

| pos | card(s) | main* | rule |
|---|---|---|---|
| land | land drop | m1 | Searched at depth > 0 (folded into the plan); played first at d0 |
| -- | Generator Servant **sacrifice** | -- | Not a cast. It is applied as a pre-pass before every cast, in both worlds |
| 5 | Sol Ring, Izzet Signet **TIE** | m1 | Mana rock. Rocks are not `IsManaRitual`, so cheapest-first does not apply and **hand order decides** |
| 10 | Generator Servant, Tandem Lookout, Niv-Mizzet Parun, Niv-Mizzet the Firemind **TIE** | m1 / m2 / m2 / m2 | Creature tier, hand order |
| 20 | Curiosity | m1 | Other noncreature (after its host: creatures are 10) |
| 20 | Ponder, Preordain, Brainstorm, Gitaxian Probe **TIE** | both | Other noncreature. **This rank is not what plays** (see below) |
| (X) | Remand | both | Never cast alone. It rides another cast X (`Action::remand_by`), and the combined action ranks as **X** (`CastOrderKeyOf` reads X's definition) |
| -- | Firemind `{T}: draw` | -- | An activation. It runs in the trailing activation pass after the casts |

**These ranks only sequence plans with no draw spell.** Every cantrip and Remand has `draw > 0`. Any plan
holding one is therefore `OrderingOpaque` (`ManaPayment.cpp:3860`), and Niv does not opt into
`OrderOpaqueCastsByRank`. Such plans play:

* **Searched play: the cantrip-first partition** (`ApplyCantripFirstOrder`, `TurnSolver.cpp:29233`). The groups
  are [Sol Ring, Signet, Nivs, Curiosity with its same-plan host, Lookout, sac-for-mana] -> [cantrips, Remand
  riders] -> [Generator Servant's cast, everything else]. **Each group is in hand order.**
* **d0:** hand order.
* **The safety net:** `MTG_PAYABLE_ORDER` reorders a plan only when the aggregate projection calls it unpayable.

*Main column: the report's classifier value. It is **advisory**. Niv does not opt into `ClassifiesMainPhases`, the
deck plays a second main (`MTG_NIV_SECOND_MAIN`, default ON), and both mains are searched. Nothing enforces it.*

## 3. Proposed order

| pos | card(s) | main | range | why |
|---|---|---|---|---|
| land | land drop | m1 | -- | **Stays searched**, with no pin (`LandDropCastOrderRank` -1). The default (first try) is after the draws when the board already pays for them (principle 1), and before them otherwise. Which land to play (Skerry/Needle tapped, Canal tapped once 3+ lands are out, Sulfur Falls needs an Island) is the search's job, not the rank's |
| 4 | **Sol Ring** | m1 | -- | {1} -> untapped {C}{C} this turn, net +1. Its {C}{C} pays the Signet's {2}. Your rule |
| 5 | **Izzet Signet** | m1 | -- | "{1},{T}: Add {U}{R}". Cast second, the Signet then filters a land's or Sol Ring's colourless into U+R. It nets -1 on the turn it is cast, so it never funds a later cast; it only fixes colours. "Cantrip first, then Signet" is still reachable through the cantrip's breakpoint continuation, so the Signet needs no range |
| 8 | **Niv-Mizzet, the Firemind / Niv-Mizzet, Parun** (name tie) | **m1** | -- | Ahead of every draw: each later draw pings, and Parun also draws off every later instant/sorcery cast (Q11). They tie by name. The order between them provably cannot matter: neither triggers off the other's cast, and both pings land either way |
| 9 | **Curiosity** -- when its host is a Niv | m1 | -- | Right after its host. The cantrip's draw then pings, Curiosity draws, and the loop runs (Q11; the kill "Curiosity onto Niv, then Preordain" was unfindable without it) |
| 10 | **Tandem Lookout** -- when a Niv is on the battlefield or earlier in the plan | **m1** | -- | After the Niv, so it enters with the Niv to pair with (`SoulbondPartner` ranks a Niv first). The pair then turns the Niv's pings into draws |
| 12 | Ponder | both | -- | First draw: look at 3 / keep-or-shuffle. It goes before Preordain so a shuffle never throws away a scry (the canonical cantrip order, `MTG_CANTRIP_ORDER`'s USER design 2026-08-15). Each cantrip is a breakpoint that ends the plan there, so only the **first** cantrip's slot is a real choice. The continuation picks the next |
| 13 | Preordain | both | -- | Scry 2, then draw |
| 14 | Brainstorm | both | -- | After the looks, so its draw 3 takes the cards they set up. The put-back is searched |
| 15 | Gitaxian Probe | both | -- | Last plain draw. It costs no mana (2-life twin), and it is the deck's "go off now" draw (your bucket 3), so it is the draw kept until the Niv and its enabler are down |
| 16 | **Remand** (the X+Remand action) | X's | -- | Ranked as a draw, **not at X's slot**: it costs X + {1}{U}, so it is the last draw. The target X is searched. With Parun out, "Ponder + Remand" = Parun draws twice, Remand draws, and Ponder returns to hand |
| 22 | Curiosity -- no Niv host this turn | m1 | -- | No draw fires it. Information first (principle 1) |
| 23 | Tandem Lookout -- no Niv to pair | m1 | -- | Same reasoning. "Lookout before Niv" is a cross-turn line (your bucket 2: *"it can be played before Niv"*) |
| 24 | Generator Servant (cast) | m1 | -- | Summoning-sick (`sac_for_mana_requires_tap`), so it is set-up for next turn; info first. It also goes after the Lookout so it never takes the Lookout's pairing ahead of a Niv |
| -- | Generator Servant sacrifice | -- | -- | Unchanged (a pre-pass). Your rule restricts an unneeded crack to Firemind turns (`MTG_NIV_SERVANT_FIREMIND_ONLY`, ON). See §7 for the payment limitation |
| -- | Firemind `{T}: draw` | -- | -- | Unchanged: it trails the casts, so it is the loop's opening draw after Curiosity/Lookout are down |

One line: **land (searched) -> Sol Ring -> Izzet Signet -> Niv (either) -> Curiosity on Niv -> Lookout (pairs with
Niv) -> Ponder -> Preordain -> Brainstorm -> Gitaxian Probe -> Remand rider -> [no Niv: Curiosity -> Lookout] ->
Generator Servant.**

## 4. What changes and why it matters

1. **Sol Ring before Izzet Signet. Today the tie can DROP the Sol Ring.** Probe
   `build/scratch/castorder/probe_NivMizzetCombo/p2.json`, built board, `MTG_USER_ORDER_TRACE=1`:
   * The board: T2, Island in play plus an Island drop. The hand holds Izzet Signet ahead of Sol Ring.
   * The executor casts the Signet off both Islands. **Sol Ring then goes `paid=0`**, the same at d1, d3 and d5.
     Swap the hand order (`p2r`) and both are cast, the Signet paid by Sol Ring's {C}{C}.
   * The payable-order repair does not fire. The aggregate pool credits the unfed Signet "+1 wild"
     (its card note), so Signet-first projects as payable.
   * In a cantrip plan (`p3`: + Ponder) the same hand order is rescued only because the aggregate does fail
     there. With `MTG_PAYABLE_ORDER=0` that plan drops Sol Ring **and** Ponder.
   * Already recorded as APPLY-DROP-ORDER (`mana-completeness-oracle.md` ~:1007, *"the deck's own cast order puts
     a Signet before the Sol Ring that pays for it (a USER-owned order)"*).
   * On a fuller board (`p4`) the search routes around it: Sol Ring in main 1, the Signet in main 2. That is search
     spent repairing an order.
2. **Main 1 for the Nivs and the Lookout (the report's baseline says m2).** Probe `p9`:
   * T6, six lands, an unsick unpaired Lookout in play, Parun in hand, no cantrip.
   * Parun cast in **main 1** pairs with the Lookout. The Lookout's 2 combat damage draws, and the loop kills
     **T6**.
   * Cast in main 2, the pair forms after combat, and the kill needs a cantrip the hand does not hold.
   * The same holds for a Lookout cast next to an unsick Niv, and for Curiosity on an attacking Niv.
   * Advisory today (§2). It matters the moment main-phase classification is enabled for this deck.
3. **The order binds inside cantrip plans (opt into `OrderOpaqueCastsByRank`).** Today those plans run in hand order
   inside each partition group, so a Niv can be ordered ahead of the Sol Ring that pays for it. In probe `p7` (T5,
   five lands, hand Firemind, Sol Ring, Probe) the repair reordered it (`ppays=1`). It catches only what the
   aggregate projection catches (item 1).
4. **The Curiosity/Lookout hoist becomes conditional on a Niv.** Today `MayPrecedeCantrip` hoists them over every
   cantrip unconditionally, by param. Its own charter is *"a PAYOFF whose trigger the cantrip's own cast or draw
   would then fire"*, and with no Niv out or in the plan a cantrip's draw fires neither card. Example: a
   {Lookout, Ponder} turn with no Niv commits the Lookout (and a pairing with a Servant) before seeing Ponder's
   card. Ponder first lets the continuation pick with that card known.
5. **The rest gets a fixed slot instead of a hand-order tie:**
   * Remand's slot (as the last draw, instead of its target's slot);
   * the cantrip sub-order;
   * Generator Servant's cast after the draws in clean plans too. Today it is tied with the Nivs at 10.

## 5. Questions for the USER

1. **Bind the order inside cantrip plans** (Niv opts into `OrderOpaqueCastsByRank`)? Without it, Sol Ring ->
   Signet and the rest hold only in plans with no cantrip. *Default: yes.*
2. **Sign off Q11 with one amendment:** Nivs always before the draws; Curiosity/Lookout before them only when a Niv
   is on the battlefield or earlier in the same plan, otherwise after the draws? *Default: yes (conditional).*
3. **Cantrip sub-order Ponder -> Preordain -> Brainstorm -> Probe -> Remand?** Only the first one cast is a real
   choice; the breakpoint re-plans the rest. *Default: yes.*
4. **Main 1 for both Nivs, Curiosity and Tandem Lookout** (the report says m2 for the Nivs and the Lookout)?
   *Default: m1.*
5. **Fix the Servant-haste payment hole** (§7: a Signet cast in the same line spends the Servant's haste-tagged
   {C}, so the Firemind loses haste)? A mana-payment change, measured as a play change. *Default: yes.*

## 6. Generic-order findings

* **Mana rocks tie at 5 and fall to hand order.** `CastOrderLessRanked`'s cheapest-first rule covers only
  `IsManaRitual`. A generic fix: within the rock tier, cast a net-positive producer first (`produces_amount >= 2`
  with no `ramp_filter`, i.e. Sol Ring) ahead of a `ramp_filter` rock. A filter never funds, and a producer can pay
  for it. This touches every Sol Ring + Signet list (Hinata sideboard, FiveColour, Galadriel).
* **The payable-order projection over-credits an unfed `ramp_filter`.** `AvailableManaPool` books the Signet's "+1
  wild" even when nothing is left to pay its {1}. So `MTG_PAYABLE_ORDER` reads "Signet, then Sol Ring" off two
  lands as payable, and the real payer drops Sol Ring (§4.1). Fixing the projection (charge the feed, as
  `MTG_RAMP_FEED_CHARGE` already does for the colour test) would let the existing repair catch this for every deck.
* **The m1/m2 baseline sends a sick creature to m2 even when combat damage is a draw.** A soulbond/aura
  damage-to-draw enabler (`soulbond_damage_trigger_draw`, `aura_damage_trigger_draw`), or a `draw_trigger_damage`
  creature next to one, wants m1 so that combat damage starts the loop (§4.2).
* **The Remand rider is classified two ways.** `OrderingOpaque` reads `card_name`, which is the target X, so
  "Curiosity + Remand" counts as a clean, rank-sorted set. `IsPlainCantrip` treats it as a cantrip. Pick one; the
  rider's draw is a breakpoint either way.
* **Report:** `--cast-order-report` prints ranks and `[2..20]` ranges for the four cantrips and Remand. None of
  that plays: the sets are order-opaque, and the promotion that would make the range live is OFF for Niv. Mark such
  rows "rank not applied" (same finding as the Hinata2 draft).

## 7. Implementation notes

* **Params per slot.** Reuse the deck's existing classifier `NivRoleOf` (`DecisionProviders.cpp:53133`); it already
  splits Sol Ring from Signet. It folds Ponder, Preordain and Brainstorm into `kNivCantrip`, so the sub-order reads
  params:

  | slot | param | rank |
  |---|---|---|
  | Sol Ring | `kNivSolRing` (`mana_rock`, `produces_amount >= 2`) | 4 |
  | Izzet Signet | `kNivRock` (`mana_rock && ramp_filter`) | 5 |
  | Firemind / Parun | `draw_trigger_damage > 0` | 8 |
  | Curiosity | `aura_damage_trigger_draw` | 9 / 22 |
  | Tandem Lookout | `soulbond_damage_trigger_draw` | 10 / 23 |
  | Ponder | `cast_reorder > 0` | 12 |
  | Preordain | `cast_scry > 0` | 13 |
  | Brainstorm | `put_back_from_hand > 0` | 14 |
  | Gitaxian Probe | `kNivProbe` (`draw > 0`, phyrexian) | 15 |
  | Remand rider | `!Action::remand_by.empty()` via `CastOrderActionRank` | 16 |
  | Generator Servant | `sac_for_mana_grants_haste` | 24 |

* **Code changes:**
  1. `NivMizzetComboProvider::CastOrderRank`, by the table.
  2. `CastOrderTotalByName() { return true; }`: the two Nivs share a rank, and the name breaks it.
  3. `OrderOpaqueCastsByRank() { return true; }` (Q1). The executor (`AIEngine.cpp:7692`) and the rollout twin
     (`TurnSolver.cpp:61406`) then rank-sort the opaque plans. That sort replaces the cantrip-first partition's
     group order. The ranks above therefore carry Q11's VALUE rule themselves (Niv 8 < the draws 12-16).
     Expect d0 movement too.
  4. `CastOrderActionRank`: return 16 for a cast carrying `remand_by` **when X is itself a draw**. Keep X's rank
     when X is not a draw: a Remand on a Niv returns the Curiosity's host to hand, and the search owns that.
* **The conditional slots (Curiosity 9/22, Lookout 10/23) need the plan, not just the board.** `CastOrderRank(s, def)`
  sees the battlefield, so "a Niv is already out" is a scalar test. "A Niv is cast earlier in this plan" is not.
  Implement it set-level in `ArrangeCastOrder`: move the enabler to the late slot when no Niv is on the battlefield
  or in the action list. Lockstep comes free, because both worlds call it through `ApplyDeckCastArrangement`. The
  enumeration's `MayPrecedeCantrip` VALUE clause should get the same condition, or the enumeration and the apply
  sort will disagree.
* **ENGINE LIMITATION (blocks Q5; not fixable by any rank):** the Generator Servant's haste is lost when a
  noncreature cast in the same line takes its tagged {C}.
  * Probe `p5`: T5, four Islands + Spirebluff Canal + an unsick Servant, hand Izzet Signet + Firemind, opponent at 4.
    The kill is "crack the Servant, Signet paid by two Islands, a third Island feeds the Signet for {U}{R}, Firemind
    paid with the Canal, the Signet's {U}{R}, an Island and the tagged {C}{C}, hasty Firemind attacks for 4".
  * It is enumerated (human-mode `validate_line`: accept), but it never wins:
    * at d3 b50 and at **d3 `--budget-ms 0`**, the search attacks with the Servant for 2;
    * played by hand (`human_lines`), the opponent stays at 4.
  * The same board with the Signet already in play (`p5c`) wins T5.
  * Cause: the payer spends floating mana before tapping, so the Signet takes the tagged {C}{C}. The card's own
    note discloses this residue.
  * The Signet must precede the Firemind (it supplies an {R}), and the crack is a pre-pass that cannot be ordered
    after it. So no cast order expresses the line.
  * Fix site: the shared payer (mana payment is a standing exemption). When a later creature in the same line can
    take the haste, pay a noncreature spell from lands before tagged {C}. Or allow the crack to sit at a cast-order
    position.
* **No ranges.** The `[2..20]` spans in the report are the cantrip promotion's, which is OFF for Niv. It is
  bypassed entirely once the provider overrides `CastOrderRank`.
* **Probes:** `build/scratch/castorder/probe_NivMizzetCombo/p{2,2r,3,4,5,5c,5h,5b0,7,8,8r,9}.json`, run with
  `MTG_USER_ORDER_TRACE=1 build/Release/mtg --scenario <file>`. `p8`/`p8r` show the Lookout/Firemind tie is harmless
  under a creature-only crack: both orders win.
