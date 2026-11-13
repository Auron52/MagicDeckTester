# Fungus: PROPOSED full cast order (2026-11-13, for USER review, nothing built)

Deck: `decks/Fungus/Fungus.cod` (v3, adopted 2026-10-16; suite keys `fungusv3`, `fungusv32hg`). Provider:
`FungusProvider` (`src/ai/DecisionProviders.h:3084`). Card text and params for all 15 nonland names, plus the
Fungus Frolic face, come from `src/cards/data/cards.json` (Rule 0). This is a proposal. Cast order is
USER-owned, so no src was edited, nothing was built or run, and nothing is committed.

USER, verbatim (2026-11-13): *"Regarding Concordant Crossroads we should just make an order for Fungus, starting
with you or another agent suggesting a full order. You can adjust the generic order if you wish, but the root
problem is not defining an order, which is suboptimal."*

## 1. Status: **(D) generic tiers only**

* `FungusProvider` overrides none of `CastOrderRank`, `CastOrderRankLatest`, `CastOrderActionRank`,
  `ArrangeCastOrder`, `CastOrderTiesByName` or `CastOrderTierName`. It overrides `MainPhaseOverride`.
* **The only USER order on record** is USER 2026-09-17 (`docs/design/fungus-second-main-and-devour.md` §3b):
  *"Note that, order wise, Doubling Season should be dropped before any activations and in main 1 so that
  Beastmaster counters are doubled."* It was never built (§6e "Unbuilt"). It was never measured and never
  switched off. This proposal keeps it.
* **The 2026-10-14 draft** (`docs/design/cast-order-drafts-2026-10-14/Fungus.md`, Q52-Q56) was written for the
  v1/v2 list: Mycoloth, Simic Growth Chamber, Essence Warden, the Psychotrope/Sporesower/Shell-Dweller/Tukatongue
  Thallids. v3 holds none of those cards, and Q52-Q56 have no answer on record. This document replaces that
  draft for v3. The two rulings it carried that still apply are Season-first (above) and the main-phase split
  (§6).
* **Main phase today:** no v3 card has devour. `MainPhaseOverride` therefore returns Main1 for every v3 card.
  A second main exists only because of Shroofus (`combat_damage_tokens_per_damage` is on `DeckUsesSecondMain`'s
  whitelist). `SecondMainNeedsDeferredCast` (`MTG_FUNGUS_M2_GATE`, ON) skips the phase when no hand card answers
  Main2/Both (`TurnSolver.cpp:7170`). So in searched play on v3 **every cast is main 1**. Main 2 runs for one
  thing only: a Saproling Burst that ENTERED this turn still has fade counters, so the gate opens main 2 for its
  free activation (`MTG_M2_FREE_ACTIVATION`, ON, `TurnSolver.cpp:7220-7266`). Main 1 could not pop it, because
  the Burst was not on the battlefield when main 1 enumerated. **A Burst's landing-turn pops therefore happen
  after combat** (§5A.3, Q9). See Q6.

## 2. What plays today (generic tiers, `GenericProvider::CastOrderRank`, DecisionProviders.cpp:5750)

| tier | card(s) | note |
|---|---|---|
| land | land drop | First (no draw or look effect among the casts) |
| 5 | Wild Growth (land-Aura ramp), Sol Ring (rock) **TIE** | |
| 10 | Thallid, Utopia Mycon, Sporecrown Thallid, Undercellar Myconid, Shroofus Sproutsire, Brightcap Badger **TIE** | Creature tier |
| 20 | Concordant Crossroads, Saproling Migration (both modes), Fungus Frolic, Beastmaster Ascension, Saproling Burst, Doubling Season **TIE** | Other noncreature (Frolic is an Instant face) |
| -- | Burst fade pops, spore pops | Activations: they run in the trailing pass after every cast of the main |
| -- | Utopia Mycon sac-for-mana | A pre-pass, collected from the battlefield only |

**Correction to the brief's "creatures cheapest-first":** `CastOrderLessRanked` (ManaPayment.cpp:4376) breaks
a same-tier tie by mana value only when BOTH actions are rituals (`IsManaRitual`). Fungus does not opt into
`CastOrderTiesByName` either. Every tie above therefore keeps **plan order**, which is hand/enumeration order.
That is why s30060073 walked Thallid > Myconid > Crossroads (mana-clean checkpoint, round 14): plan order inside
tier 10, then Crossroads at tier 20.

**The defect:** Crossroads (20) resolves after every creature (10), so a creature cast this turn cannot tap
for mana in the plan that casts it. That covers Undercellar Myconid's own `{T}`, and every Fungus/Saproling
under Badger's granted `{T}: Add {G}`. Doubling Season (20) also lands after Undercellar Myconid's ETB Saproling,
in a random order against Migration, Frolic and Burst. The result is ~143-209 ENUM-GAP flags per 5000 games.
Round 14 found that 19 of the 23 top signatures pay only as "Crossroads > (mana creature) > ...".

## 3. Proposed order (TOTAL; all main 1; within a slot by sub-rank, never by plan order)

The ranges use the critter/NivMizzet convention: the **ideal** slot is what the rank delivers; the **range** is
how far `ArrangeCastOrder` may move the card when, and only when, the ranked order cannot pay the set and the
moved order can (§4).

| pos | card | ideal | range | why (card text) |
|---|---|---|---|---|
| land | land drop | first | -- | No draw/look among the casts, so nothing drawn can change the land. Hickory Woodlot enters tapped and Ancient Tomb is untapped `{C}{C}`; neither interacts with the cast order. Unchanged |
| 1 | **Wild Growth** | 1 | [1 .. after Badger] | *"Whenever enchanted land is tapped for mana ... adds an additional {G}"*. The host must still be untapped when the Aura resolves, so the Aura goes before every spell its host could pay for. Its `{G}` pip can't come from Sol Ring. Range: only when the sole spare `{G}` is a Badger-granted body (§4 R5) |
| 2 | **Sol Ring** | 2 | -- | `{1}` -> `{C}{C}`, net +1 this turn, generic only. It goes after Wild Growth because Sol Ring cannot pay the Aura's `{G}`, and tapping the Aura's host for Sol Ring before the Aura resolves loses the bonus. Worked case: 2 Forests + both in hand gives G+CC left with WG first and CC with Sol Ring first |
| 3 | **Brightcap Badger** (creature face, from hand or from exile) | 3 | -- | *"Each Fungus and Saproling you control has '{T}: Add {G}.'"* An ENABLER. Every UNTAPPED, non-sick (or hasted) Fungus/Saproling becomes a `{G}` for every later cast. Casting it puts no token and no counter on the battlefield (the end-step Saproling comes at end step, after everything), so going ahead of Doubling Season costs nothing. It goes ahead of Crossroads because existing bodies can then pay Crossroads' `{G}` (4 lands + 2 old Saprolings: Badger > Crossroads pays; Crossroads > Badger does not) |
| 4 | **Concordant Crossroads** | 4 | [after rocks .. 4] | *"All creatures have haste."* The `{T}` legality of everything cast AFTER it (CR 302.6; `CanTapNow` reads `HasHasteFromLords`). It goes ahead of every creature and token maker. Haste for ATTACKING does not depend on the order (it is a static that applies at combat), so the order matters only for mana. Range: ahead of Badger when a fresh, hasted Myconid has to fund the Badger (§4 R1) |
| 5 | **Doubling Season** | 5 | [5 .. after the minimal producer prefix that pays it] | USER 09-17: before any activation, in main 1. It doubles tokens at `CreateToken` and counters at the put. It must precede Undercellar Myconid's ETB token, Migration, Frolic, Burst's fade counters as it enters (7 -> 14), and every Burst/spore pop. Quest counters (Ascension) and Shroofus's tokens come at combat, so main 1 is enough for those. Range: later only when same-plan producers must fund it (§4 R2) |
| 6 | **Undercellar Myconid** | 6 | [after Crossroads .. 6] | *"{T}: Add one mana of any color"* plus an ETB 1/1 Saproling. It is the deck's only printed tap-mana creature (Fungus, so under Badger its Saproling taps too). After Season, so its ETB Saproling is doubled. First creature, because with Crossroads out it funds the rest. This is the s30060073 fix as a pure rank: {Crossroads, Thallid, Myconid} -> Crossroads > Myconid > Thallid |
| 7 | **Utopia Mycon** | 7 | [after Badger+Crossroads .. 7] | `{G}` 0/2 Fungus. *"Sacrifice a Saproling: Add one mana of any color"* (no `{T}`, so it is legal the turn it is cast). It is the cheapest mana source among the remaining creatures. **Correction to the draft:** `MTG_SAC_OUTLET_PAY` has been ON since 2026-10-31 (`SpellEffects.h:38906`). The payer eats Saprolings through any Mycon on the battlefield AT PAYMENT TIME (`LiveSacPayOutlet`, `:38953`), so at apply a Mycon cast at slot 7 is a live outlet for slots 8-15. I found no enumeration-side credit for a same-plan Mycon (grep only, unverified), so a subset whose later cast needs it may be pruned before it is tried (§5B.1). Its Badger tap is real |
| 8 | **Thallid** | 8 | [after Badger+Crossroads .. 8] | `{G}` 1/1 Fungus. An upkeep spore engine; nothing at cast. Under Badger + Crossroads it pays its own cost back (`{G}` in, `{G}` out) |
| 9 | **Sporecrown Thallid** | 9 | [after Badger+Crossroads .. 9] | `{1}{G}` lord, +1/+1 to other Fungus/Saprolings. The P/T bonus is a static that applies at combat, so its main-1 position is inert for damage. Burst pops trail every cast, so a Sporecrown cast in the same main is already out when a pop takes the tokens to 0 counters (the SBA sweep, `SweepDeadFadeTokens`, `SpellEffects.h:19170`, sees its +1 toughness). The order already enforces "Sporecrown before Burst pops that would otherwise kill" AT APPLY. **Correction to the draft:** the fade-K MENU (`FadeKLandmarks`, `TurnSolver.cpp:35546`) is priced on the node's board (`ReadFadeBoard`, `:35371`), before the plan's casts, so it does NOT see a same-plan Sporecrown (or Season; §5A.1, Q16). Under Badger + Crossroads it pays back 1 of its 2 |
| 10 | **Shroofus Sproutsire** | 10 | [after Badger+Crossroads .. 10] | `{2}{G}` legendary 1/1 Saproling. Its combat-damage tokens come at combat (doubled by a main-1 Season), so the order is inert for them. With Crossroads it attacks the turn it lands. Under Badger it taps for `{G}`, but a tap costs its attack (a PAYMENT choice, not an order one; §5.4) |
| 11 | **Saproling Migration** (unkicked) | 11 | [after Badger+Crossroads .. 11] | `{1}{G}`: two 1/1 Saprolings (4 under one Season). After Season. With Badger + Crossroads live, the tokens pay back 2 (4), so it is a net-0 (net +2) ritual. Cheapest token maker first |
| 12 | **Fungus Frolic** (Badger's adventure, Instant) | 12 | [after Badger+Crossroads .. 12] | `{2}{G}`: two 1/1 Saprolings. After Season. Same token rate as Migration for one more mana. When a plan holds both Frolic and that Badger cast from exile, the rules already force Frolic first (the card must be exiled before the creature can be cast) |
| 13 | **Saproling Migration** (kicked, `{5}{G}`) | 13 | [after Badger+Crossroads .. 13] | Four Saprolings (8 under Season). After Season. Costs the most of the token makers, so it comes last of them. It shares a hand slot with the unkicked mode, so only one of 11/13 is ever in a plan |
| 14 | **Saproling Burst** | 14 | -- | `{4}{G}`, Fading 7. Its fade counters are PUT ON as it enters, so Season must already be out (14 counters, and every pop's token doubled). It makes nothing at cast; its tokens come only from pops. A Burst cast this main is popped only in MAIN 2 (§1, §5A.3), so today it funds nothing in its own plan, and it goes after everything that can. The conditional earlier slot in §5A.3 (R6) applies only if Q9 is built |
| 15 | **Beastmaster Ascension** | 15 | -- | `{2}{G}`. *"Whenever a creature you control attacks, you may put a quest counter"*. The counters arrive at declare-attackers, so main 1 is enough, and nothing in main 1 reads it. It funds nothing and gets nothing from a later cast, so it goes last. (USER §3c listed it third; §5.1 explains why last is equivalent, and Q2 asks) |
| -- | Burst fade pops, Thallid/Mycon spore pops | trailing | see §5A | Unchanged by default. They run after every cast, so Season, Sporecrown and Crossroads cast in the same main are already out. This is the USER's "Season before any activations", which holds by structure. §5A places each activation and proposes the conditional forward slots |
| -- | Utopia Mycon sac-for-mana | payment | see §5B | A mana ability the PAYER uses, last-ranked (`MTG_SAC_OUTLET_PAY` ON). The searched pre-cast action survives only where no Saproling exists (the fused same-line pop) or where a death pays |

One line: **land > Wild Growth > Sol Ring > Brightcap Badger > Concordant Crossroads > Doubling Season >
Undercellar Myconid > Utopia Mycon > Thallid > Sporecrown Thallid > Shroofus Sproutsire > Saproling Migration >
Fungus Frolic > Saproling Migration (kicked) > Saproling Burst > Beastmaster Ascension.**

Suggested rank encoding (critter convention, slot x 64 + sub, so no tie is left to plan order): the slots
above, 1..15. Key every one on params, never names:
* WG: `is_land_aura && land_aura_extra_mana`
* Sol Ring: `mana_rock`
* Badger: `!granted_tap_mana_subtypes.empty()`
* Crossroads: `grants_haste && affects_all_creatures`
* Season: `doubles_tokens || doubles_counters`
* Myconid: creature with `produces` (a printed tap-mana creature)
* Mycon: `IsSacManaOutlet`
* the other creatures: by mana value
* token spells: `cast_creates_tokens`, with the kicked face (`kicked_creates_tokens`, via `CastOrderActionRank` or
  the synthesized face's def) after Frolic
* Burst: `fading_counters`
* Ascension: `quest_counter_per_attacker`

Anything unlisted falls back to `GenericProvider::CastOrderRank`.

## 4. Which interactions are rank-only and which need an `ArrangeCastOrder` condition

**Rank-only** (the rank alone delivers it; a weakly-dominant order, so no condition is needed):

* Wild Growth before Sol Ring and before everything (§3 rows 1-2).
* Season before Myconid / Migration / Frolic / Burst when the ranked order pays.
* Crossroads before every creature and token maker. Myconid first among the creatures. This alone fixes the
  s30060073 class: {Crossroads, Thallid, Myconid} ranks as Crossroads > Myconid > Thallid, which is exactly the
  oracle's paying order.
* Sporecrown / Season / Crossroads before Burst pops: these hold by structure at apply (pops trail the casts).
  The rank just keeps them that way. The K menu is still priced without them (§5A.1).
* Ascension anywhere in main 1: inert, so it goes last.

**Conditions** (`FungusProvider::ArrangeCastOrder`). Each one fires ONLY when the ranked order fails the
sequential payability walk and the moved order passes it. Each ADDS lines and never reorders a plan that
already pays (the round-14 recommendation). Try them in this order and take the first one that pays:

* **R1, a hasted Myconid funds the Badger.** The plan holds Crossroads + Myconid + Badger, and the board cannot
  pay Badger first. Move to Crossroads > Myconid > Badger > (rest). Cost: Myconid's ETB Saproling is cast before
  any same-plan Season (R2 then decides where Season goes).
* **R2, Season waits for its funders.** Season is unaffordable at slot 5, but becomes affordable after a
  same-plan prefix of producers: Myconid; Mycon/Thallid/Sporecrown/Shroofus under live Badger + haste; and the
  token spells under live Badger + haste. Move Season right after the SHORTEST such prefix (taken in rank
  order). Every token maker not in that prefix stays after Season. Cost: the prefix's tokens are undoubled. This
  is still better than a set that cannot be cast at all, and whether the set is worth casting is the search's
  call.
* **R3, refund sort.** Badger and haste are live (on the battlefield or earlier in the plan) and neither ranked
  nor R1/R2 pays. Re-sort the block after the enablers by the refund rule: items that pay back at least their
  cost first, cheapest first; then the rest, most payback first. Payback: Myconid 2 (own tap + ETB token), each
  1-drop/2-drop/Shroofus 1, Migration 2 (4), Frolic 2 (4), kicked Migration 4 (8). Keep Season as early as
  payability allows. An exchange argument shows this order is the most payable one when every payback is
  immediate.
* **R4, Crossroads after a funder.** Crossroads' `{G}` cannot be paid until a Badger-granted body exists, and
  the canonical order already covers that (Badger is slot 3). The remaining case is a plan with no Badger where
  Crossroads' `{G}` must come from Myconid's ETB token sacrificed to an on-board Mycon. The sac pre-pass already
  covers that, so **no rule is needed**. Listed so nobody builds one.
* **R5, Wild Growth after Badger.** The board's only spare `{G}` is a granted body (old Saproling) and Badger
  is in the plan. Move WG right after Badger. Rare. It can be folded into R3's walk or left out (Q5).
* **R6, R7 (activation positions, §5A).** R6 moves Burst forward to just after Season when its own landing-turn
  pops fund the set; it needs Q9 first. R7 moves an on-board Burst's pops to the G slot, after Season and before
  the casts they fund. Same rule as R1-R5: each fires only when the ranked order fails the walk and the moved
  one passes it.

**No two-way search is needed** (no `CastOrderingCandidates`). Wherever both orders pay, one weakly dominates:
* Season before Myconid: same mana, one more Saproling.
* Season before Burst: 14 counters vs 7.
* Badger/Crossroads before bodies: strictly more mana later.
* Ascension: inert.

The one non-dominated pair found is Badger vs Crossroads, and R1 is a payability case, not a value case.

**Walk/apply lockstep is already wired.** `ApplyDeckCastArrangement` runs before the walk (`TurnSolver.cpp:12209`,
then `SeqWalkFixedOrder` at :12240) and at every apply site: executor `AIEngine.cpp:8418/8529`, continuations
`AIEngine.cpp:6481/6495/7026/8892`, and `ManaPayment.cpp:5272/5282`. A provider rank plus arrangement therefore
reaches both the enumeration's walk and real play. That is the lockstep round 14 said the fix needed
("PayableCastOrderCore sites 0+1").

**Prerequisite the order alone does not supply:** the walk must CREDIT a mid-plan source.
* `MTG_GRANT_HASTE_CAST_CREDIT` (adopted c6a9afe3e) covers a hasted creature under a LIVE Badger.
* The Crossroads-cast-in-the-same-plan credit was `MTG_PENDING_BLANKET_HASTE`. It was dropped in round 14 *only
  because* the walk put Crossroads last. It lives on `wip/fungus-midplan-source-credit` (473d9ccdc / 90e02c290)
  and should be revived together with this order.
* Tokens made by a same-plan Migration/Frolic, tapped under Badger + haste, have **no credit at all** today.
  R2/R3's token-spell rows find nothing until that credit exists (§7 item 2).

## 5. Notes on specific interactions

1. **Ascension last vs the USER's §3c "Season > Beastmaster > Sporecrown".** Ascension's counters come at
   declare-attackers. Doubling Season doubles them as they are put on, and the +5/+5 is a static read at combat.
   Within main 1, therefore, Ascension's position changes no counter, no anthem and no token. It changes only
   payability, and casting it later can only help there, because it pays nothing back. Last is therefore
   equivalent to third whenever both pay, and better when only last pays. The deviation is still the USER's
   call (Q2).
2. **Season is slot 5, not "first among casts".** Wild Growth, Sol Ring, Badger and Crossroads create no token
   and put no counter, so casting them ahead of Season loses nothing. They are what pays for Season. The USER's
   words ("before any activations", "so that Beastmaster counters are doubled") are fully met. The 09-17 draft's
   "FIRST among casts [U]" tag goes further than the quoted words (Q1).
3. **Burst pops and the Season.** The pops trail every cast, so a pop never precedes a same-main Season. One
   exception sits outside the cast order: the whole-turn prepay can pop an ON-BOARD Burst so that its doomed
   tokens pay a cast, Season included (mana-clean rounds 15-16: `MTG_FADE_PREFLOAT_DOOMED`,
   `MTG_PAY_DOOMED_BODY_FIRST`). A token minted that way before Season is undoubled. That is a payment/activation
   trade the search prices, not a rank.
4. **Tapping attackers for mana.** Under Badger + Crossroads every fresh body is both a `{G}` and an attacker
   (damage, a quest counter, Shroofus's trigger). The order only makes the tap AVAILABLE. Which body the payer
   taps is the payment's choice (the standing mana-payment exemption). A body tapped for mana in main 1 loses
   its attack, so the canonical order should not raise how often attackers are tapped. Watch for it in the A/B
   (§8).
5. **Migration kicked vs unkicked** stays a searched variant on one hand slot. The order only places each mode.
6. **Frolic then Badger-from-exile.** They are separate casts, and the rules force the order. Frolic's tokens are
   cast-this-turn bodies, so with Crossroads already out they tap for `{G}` once the exiled Badger resolves.
7. **Sol Ring / Ancient Tomb colourless.** Every nonland spell carries exactly one `{G}` pip plus generic, so
   colourless only ever pays generic. No cast order can strand a `{G}` pip behind colourless, beyond the Wild
   Growth case in §3.

## 5A. Activated non-mana abilities in the same order (added 2026-11-13, after USER feedback)

USER, verbatim (2026-11-13): *"For the Fungus cast order this seems somewhat reasonable, though I think it
needs more detail in the form of activated non-mana abilities unless we are accepting that saprolings can be
dropped when needed to pay costs. It is a bit messy because cards like Saproling Burst are difficult to place.
In theory it could be used for gas. But I think this is a start for sure."* On the Burst's last counter, the
earlier USER rule: *"By the rules they are straight up dead before you can do anything with them."*

**Inventory (cards.json).** The 60 cards hold exactly three non-mana activated abilities:

| card (copies) | ability | cost | engine mode / count menu |
|---|---|---|---|
| Saproling Burst (4) | *"Remove a fade counter from this enchantment: Create a green Saproling ... power and toughness are each equal to the number of fade counters on Saproling Burst."* | 1 fade counter. No `{T}`, no mana | `FadeSaproling`; k from `FadeKLandmarks` (`TurnSolver.cpp:35546`, the USER-designed menu) |
| Thallid (2) | *"Remove three spore counters from this creature: Create a 1/1 green Saproling"* | 3 spore counters | `SporeSaproling`; pooled with Mycon (`FoldSporeSourceIdentity`); k from `MTG_SPORE_POP_ALL` (`:47047`) |
| Utopia Mycon (1) | the same spore ability | 3 spore counters | the same pool |

Everything else in the list is a mana ability, a trigger or a static:
* Mana abilities: Mycon's *"Sacrifice a Saproling: Add one mana of any color"* (no target, makes mana, CR
  605.1a, so it belongs to §5B), Myconid's `{T}`, the Badger-granted `{T}: Add {G}`, the lands, and Sol Ring.
* Triggers: Fading's upkeep removal, the upkeep spore counter, Shroofus, Ascension, Badger's end step, Myconid's
  ETB/dies, and Wild Growth.
* Statics: Sporecrown, Season and Crossroads.

Sporecrown Thallid has no activated ability, despite its name.

**Where an activation can sit today.** Three positions exist, and no Fungus rank chooses between them:
* **T (trailing pass):** after every cast of the plan. Activations run among themselves in
  `OrderTrailingActivations` order (`TurnSolver.cpp:65580`), a stable sort on `ActivationOrderRank`.
  `FungusProvider` does not override it (default 0, `DecisionProvider.h:1812`), so the order is battlefield order.
* **P0 (pre-cast pass):** only a searched SacForMana runs here, together with the fused same-line pop. When the
  board holds no Saproling, `MakeSameLineSacFodder` (`SpellEffects.h:16679`) fires a maker inside the
  sacrifice. The maker is the first spore or fade source in battlefield order (`SameLineSacFodderSource`,
  `:16630`).
* **M2:** a Burst cast this turn is popped only here (§1).
* There is no "after cast k" slot for these modes. Only land-search activations have one
  (`Plan::land_search_act_slot`).

**The K menu is priced on the node, not on the plan.** `ReadFadeBoard` (`:35371`) reads the battlefield plus a
few hand facts:
* Crossroads in hand counts as `haste_all`;
* an Ascension;
* a castable Mycon (a value outlet);
* devour.

Token doubling is `DoublerShift` on the battlefield (`:47379`). The consequence: a Season or Sporecrown cast in
the same plan IS realised at apply, because the pop trails it, but it is NOT in the k pricing (Q16). Nothing
below proposes a new count. Every k comes from the existing menu.

| # | activation | position | relative to the casts | why (card text and interaction) | type | status |
|---|---|---|---|---|---|---|
| A1 | Fade pops of a Burst already on the battlefield, main 1 | **T**, after every cast | After Season (5), Crossroads (4), Badger (3) and Sporecrown (9). Inert against Shroofus (10), Mycon (7) and Ascension (15) | **Season:** each pop CREATES a token, doubled at `CreateToken`. The counter removal is a cost and is never doubled. **Sporecrown:** its +1 toughness makes a pop that empties the Burst leave 1/1s instead of 0/0s that the SBA sweeps. **Crossroads:** hasted tokens attack this combat, bringing damage, one quest counter each (two under Season) and Shroofus tokens equal to the damage. **Badger:** the tokens tap for `{G}`, but nothing is cast after T, so only a continuation can spend that mana. **Ascension:** its counters come at declare-attackers, so inert | rank-only (structural) | engine today |
| A2 | **The Burst as GAS:** pops of an on-board Burst made to fund this plan's casts | proposed slot **G**: right after Season, and after Sporecrown as well when the k empties the Burst and the plan holds one; before every other cast | Never before Season (USER 09-17, *"before any activations"*). After Badger and Crossroads, because the bodies pay only through Badger's tap with haste or through Mycon | A fresh token taps for `{G}` under Badger + Crossroads, and every token is Mycon fodder. Under Season, a 14-counter Burst's single pop gives two 13/13s, i.e. `{G}{G}` and two Mycon meals. Cost: every pop shrinks each live token of that Burst by 1 (the CDA), so gas is paid for out of the bodies already on board | **conditional R7**: fires only when the ranked plan with trailing pops cannot pay its set and the plan with the pop at G can | **NEW.** Needs an activation-position axis (none exists for these modes), plus an enumeration credit for the bodies. `FadeActivationLiveBodies` (`:20471`) credits them as sac FODDER in the fodder guard only, not as Badger tap mana. The gas landmark (`argmax_wide`, `:35810`) is offered only when a Mycon is on board or castable |
| A3 | **Landing turn:** pops of a Burst cast this main | today **M2**, after combat. Proposed: **T of main 1**, right after its cast resolves | Same as A1. The Burst enters with its counters doubled only if Season resolved first (rank 5 < 14) | USER 09-26: *"We should always activate some amount on the turn it enters"*, *"to attack with haste this turn"*. In main 2 the combat is over. So, with Crossroads out, the landing turn loses the hasted attack, its quest counters and its Shroofus triggers: k_pow_now is offered when `haste_all` is set and cannot be realised. Worked case: Season + Crossroads on board, the Burst enters with 14, one pop gives two hasted 13/13s, 26 damage this turn. Today the kill comes next turn. That line cannot be found at any budget, so the completeness rule applies | structural capability, not a rank | **NEW (Q9).** If built, **R6** follows: Burst moves forward from slot 14 to right after Season when its own pops fund the rest of the set (conditional, same test as R7; Q17) |
| A4 | Spore pops (the Thallid/Mycon pool), main 1 | **T**, after every cast | After Season. Against Crossroads and Badger, as in A1 | Season doubles the token. The 3 spore counters are a cost and are not doubled. `MTG_SPORE_POP_ALL` pops the whole pool, except that it offers `{1, max}` when a Season in hand can be paid only by selling a Saproling (`:47121`, `:47177`) | rank-only (structural) | engine today |
| A5 | **The funding spore pop** (the "1" rung): its Saproling is eaten by Mycon to pay Season's last mana | **P0**, fused into a SacForMana, before Season | The one sanctioned exception to "Season before any activations". USER 2026-09-27: *"it should offer before only for the purposes of sacrificing them for mana"* | When a Saproling is already on the battlefield, the payer eats it instead. The spore counters then wait for T, after Season, where the pop's token is doubled. That is the better order, and it is what happens | conditional (it exists only when the funding is needed) | engine today: fused pop when no Saproling exists (`TurnSolver.cpp:47996`, `SpellEffects.h:16630`); otherwise the payer (§5B). Q11 confirms it |
| A6 | Spore pops vs fade pops, within T | -- | They commute | Different sources. A spore token is a fixed 1/1, a fade pop changes only its own Burst's tokens, and neither costs mana. No `ActivationOrderRank` is needed. Listed so nobody builds one | -- | -- |
| A7 | **A pop that empties a Burst**, with no Sporecrown and no Ascension online | **T**. Every mana use of that Burst's tokens comes BEFORE the pop | -- | USER: *"straight up dead before you can do anything with them"*: once the last counter leaves, nothing can use the tokens. BEFORE it leaves, they are usable: mana abilities may be activated while holding priority (CR 605.3a), and the mana floats to the end of the phase (CR 106.4). `MTG_FADE_PREFLOAT_DOOMED` (ON, `SpellEffects.h:34052`) taps each doomed token's single `{G}` face (Badger) into the float first. It does NOT sacrifice them to Mycon. Because the pop sits at T, the float reaches only a continuation; it is worth more once A2/A3 exist | rank-only | engine for the tap; **NEW** for the eat (Q15) |
| A8 | The maker inside a fused SacForMana | **P0** | -- | Today it is the first spore or fade source in battlefield order. A spore pop spends counters that have no other use. A fade pop also spends a turn of the Burst's life and one point of every future token. Neither dominates | not a rank: Q12 proposes BRANCHING on the maker kind (at most two fused actions) rather than picking one | **NEW** |

**One line, casts and activations together:** land > Wild Growth > Sol Ring > Badger > Crossroads > Doubling
Season > *[P0: the funding spore pop, only when Season needs it]* > *[G: gas pops, conditional R7]* > Myconid >
Mycon > Thallid > Sporecrown > Shroofus > Migration > Frolic > Migration (kicked) > Saproling Burst > *[its
landing-turn pops, Q9]* > Ascension > **T:** every other fade and spore pop (battlefield order; they commute).

## 5B. Saprolings as a cost (Utopia Mycon)

**1. When a Saproling may be sacrificed mid-line.**
* **This is already the engine's answer to "are we accepting that saprolings can be dropped when needed to
  pay costs", and it follows the USER's own rulings.** Mycon's sacrifice is a mana ability, usable in any
  payment from the turn Mycon lands (no `{T}`). Since 2026-10-31 it belongs to the PAYER (`MTG_SAC_OUTLET_PAY`
  ON; USER *"2 we should turn on"*):
  * every payment first tries without fodder (`PayFodderOffScope`);
  * fodder then ranks behind every tap, including Badger-granted creature taps (base 500, `SacPayFodderRank`,
    `SpellEffects.h:39080`).

  USER 09-17: *"treat it as a mana source with the highest level of deferral. i.e. It is only used if
  absolutely necessary and prioritizes saprolings with summoning sickness."*
* **The division of labour (USER 10-01):** the search decides the pops, and the payment only eats bodies that
  already exist: *"you can only pay with saprolings that the search has produced at the time you need it"*.
  The searched SacForMana survives only in two cases (`TurnSolver.cpp:47996`):
  * where no body exists (the fused pop, A5);
  * where a death pays. No v3 card makes a death pay: Myconid has a dies trigger, but it is a Fungus, not
    Mycon fodder.
* **Apply sees mid-line bodies; enumeration does not.**
  * At apply, the payer reads the live board (`LiveSacPayOutlet`, `:38953`). Bodies an earlier cast of the same
    plan made (Myconid's ETB token, Migration, Frolic), and a Mycon cast earlier in the plan, all serve later
    payments.
  * The enumeration bound counts plan-start bodies plus spore-pop fodder only (`SacPayPlanFodderCount`,
    `:39154`). `docs/design/sac-mana-outlet-as-deferred-source.md` ("Bodies created earlier in the plan")
    records the same gap for the Goblins makers.
  * **Proposed (NEW, sound in the bound's direction, since an over-count only keeps lines):** credit
    same-plan token spells, Myconid's ETB token and a same-plan Mycon into that bound. This is the
    sacrifice-side twin of §7.5, and R2/R3's token rows need it too.

**2. Which bodies go first.** In order, with the current status of each:

| order | body | why | status | type |
|---|---|---|---|---|
| 1 | **A token DOOMED in this line**: its Burst is emptied by this plan's own pops and no lord saves it (`IsDoomedPlanBody`, `ManaPayment.cpp:444`). Tap it first (Badger), then eat it: tapping does not stop a sacrifice, and the fodder count includes tapped bodies (`SacPayFodderCount`) | It is 0/0 before combat in this same line, so eating it costs nothing. It can only come from an on-board Burst (A1/A7) | **NEW on the eat side.** Today `IsDoomedPlanBody` orders only the TAP (`MTG_PAY_DOOMED_BODY_FIRST` / `_AHEAD_OF_LANDS`, `:2894`). `SacPayFodderRank` ignores it, so a hasted or non-sick doomed token sits in the DEFERRED eat tier (+10000), behind a summoning-sick token that lasts | payment order (the standing mana-payment exemption), sound dominance |
| 1a | ...with a same-plan Sporecrown credited | `IsDoomedPlanBody` reads the live board, so payments for slots 1-8 call a token doomed when a Sporecrown cast at slot 9 will save it | **NEW** (only removes false "doomed" claims) | payment order |
| 2 | **Summoning-sick tokens that cannot attack this turn** (no haste); within them, lowest effective power first | USER 09-17 *"prioritizes saprolings with summoning sickness"*. Lowest power first already gives the USER's 10-01 *"1/1 saprolings unless the Saproling Burst saprolings are the same size or smaller"* except at exact ties. `MTG_SAC_VICTIM_DOOMED` covers those ties: OFF, measured flat (`fungus-sac-victim-heuristic.md` §5). With Crossroads on board this tier is EMPTY before combat, which is correct, since every body can attack | engine (FREE tier, `SacPayFodderCostsAttack` `SpellEffects.h:39070`; `DefaultSacExpendabilityRank` `:16371`) | payment order |
| 3 | **Tokens that would attack this turn**, lowest power first | Each one costs its damage. While Ascension is under 7 it also costs a quest counter (two under Season). With Shroofus out it also costs its connect tokens (equal to its damage, doubled under Season). The payer cannot see Ascension's threshold. Whether a line that eats an attacker below the threshold is worth casting is the search's call, since it scores the plans with and without the cast. No new rule | engine (DEFERRED tier) | payment order |
| 3a | ...after combat, every attacker is tapped and free (`CanAttackFull` false) | "Attack, then eat the spent attackers" | engine, but it pays only for main-2 casts (Q6) | payment order |
| 3b | ...in main 2, a Burst token ranked by its Burst's remaining counters. A 0-counter token kept alive only by Sporecrown or Ascension dies at our next upkeep: Fading cannot remove a counter, so the Burst is sacrificed and its LTB destroys its tokens | It has no next turn. This is the USER's unbuilt *"(or 1 P/T larger in the second main)"* clause (`fungus-sac-victim-heuristic.md` §6) | **NEW**, and it matters only with Q6 (Q18) | payment order |
| 4 | **Shroofus Sproutsire** (and any non-token Saproling cast this turn) last | USER 10-01: *"Shroofus should be the last to go."* It is a non-token, so it gets no -1000 token bonus. Cast this turn, it is in the deferred tier | engine (`MTG_SAC_VICTIM_ENGINE` is redundant and OFF) | payment order |

**Never fodder:** Sporecrown, Thallid, Mycon and Myconid (Fungus) and Badger (Badger Druid; cards.json
subtypes). Mycon's filter is `Saproling`. So a "Sporecrown dependency" exists only through the bodies
Sporecrown keeps alive (rows 1a and 3b). Eating never removes the lord.

**3. Tap before eat (engine; Q14).** Every tappable body (Badger grant, with haste or without sickness) is spent
before any body is eaten:
* creature taps rank inside the payer's creature band, ordered by attack value (`ManaSourceHoldValue`,
  `MTG_ATTACK_BODY_TAP_ORDER`, `ManaPayment.cpp:2577`);
* fodder ranks at 500+.

The trade is real in both directions. A tapped attacker loses this combat (damage, a quest counter, Shroofus
tokens). An eaten summoning-sick token loses only its next turn. Neither dominates, and the USER's 09-17
"only if absolutely necessary" is the rule in force.

## 6. Main phase (unchanged by this proposal)

Every v3 card stays pinned to Main1 (USER 2026-09-23: *"Only Mycoloth should go in the second main ... Everything
else can be skipped"*). With no Mycoloth in v3, main 2 in searched play runs only for a landing Burst's pops and never casts (§1). **This is a USER question,
not a change** (Q6). Shroofus's combat tokens and Badger's grant (with Crossroads) make post-combat mana real on
v3: attack, Shroofus makes N Saprolings, then tap them (or sac them to Mycon) to cast in main 2. That line did not
exist when the 09-23 ruling was given, and today it is unfindable at any budget. By the completeness rule that is
a lossy gate.

## 7. Where the generic order differs, and what to propose separately

1. **Blanket haste granter at 20, after the creatures** (G3 class in `cast-order-review-2026-10-14.md`). Proposed
   generic rule: `grants_haste && affects_all_creatures` (and haste lords) rank ahead of tier 10 when the deck
   holds a tap-mana creature or a granted tap-mana static. This is weakly dominant: the haste static changes
   nothing at the cast, and it can only enable a later `{T}`.
2. **Tap-mana creatures tie with every other creature at 10 (plan order).** Proposed generic rule: within tier
   10, a creature with a printed `{T}`-mana ability ranks first. It is inert without haste and weakly dominant
   with haste. A granted-tap enabler (`granted_tap_mana_subtypes`) ranks before the bodies it reaches.
3. **Doublers at 20, after token makers** (G10, already raised 10-14). `doubles_tokens || doubles_counters` ahead
   of creatures that make ETB tokens or enter with counters, and ahead of token spells.
4. **Wild Growth vs Sol Ring tie at 5.** Proposed generic sub-rank: land Aura before rock, for the reason in §3
   row 2.
5. **Token-spell mana under a granted tap + haste has no walk credit** (an engine gap, not an order). Needed for
   R2/R3's token rows.

My recommendation: build the Fungus order in the provider first (this document). Propose 1-4 as a separate
generic change with its own fleet A/B, because they move every deck that holds a dork, a haste source, a doubler
or a land Aura. The USER asked for an order for Fungus; the generic fix is optional.

## 8. Measurement plan (once the USER signs the order)

* **Implementation shape.** Build `FungusProvider::CastOrderRank` / `CastOrderActionRank` / `ArrangeCastOrder` /
  `CastOrderTiesByName` / `CastOrderTierName` behind `MTG_FUNGUS_CAST_ORDER` (heurarm `FUNGUS_CAST_ORDER`, read via
  `EnvOn`; see `coding-conventions.md`). The default is the USER's call. `=0` must be byte-identical to today.
  Revive `MTG_PENDING_BLANKET_HASTE` from `wip/fungus-midplan-source-credit` alongside it.
* **Units.** Permute the plan, and check that the realised order is the same every time (critter precedent).
  Pin the repros:
  * s30060073 T3 {Crossroads + Thallid + Myconid} pays;
  * s30060029 T5;
  * s30060047 T4 and s30060142 T5 (Badger-in-plan);
  * one R1 board, one R2 board (Myconid funds Season), one R3 board (Migration ritual under Badger + Crossroads);
  * a Season + Burst board showing 14 fade counters;
  * once built, one board for each NEW §5A/§5B item: a landing-turn Burst popped in main 1 under Crossroads (Q9),
    a gas board where only the G-slot pop pays the set (Q10), and a doomed token eaten ahead of a summoning-sick
    lasting 1/1 (Q13).
* **Lever-off identity.** `MTG_FUNGUS_CAST_ORDER=0` against the committed binary on the suite's fungus rows, and
  a quick all-deck identity sample (the provider is Fungus-only, so every other deck must be byte-identical).
* **Held-out paired A/B, ONE pooled batch, both arms as a per-job heurarm lever:**
  * fungusv3 at the shipped cell (d5 b20, profile attached so the keep table and value leaf are in place),
    4000-5000 games;
  * d3 b10, about 4000;
  * d0, about 10000 (the d0 runner and the beyond-horizon playout take the canonical order, so d0 moves most);
  * fungusv32hg at shipped/d3, about 2000.
  * Seeds: a fresh block such as **8750001+** (no doc, script or test references 875xxxx as of today; 8200000 is Landfall/Snow's; re-check before use). Earlier fungus
    held-out blocks 7370001-7500001 are spent.
  * Score: loss as 9 and as 99. Report net faster/slower per cell, digest diffs, work units, and the attacker-tap
    count from §5.4.
  * Adoption bar: every slower game recovers at `--depth 8 --budget-ms 0`, and net <= 0 per deck.
* **The mana-clean gate.** `scripts/mana_clean.py run --games 5000 fungus` on the next unused block
  (**30080000-30084999**; the last recorded is 30075000), PGO binary. Expect ENUM-GAP well below 167 (the
  Crossroads class was 19/23 of the top signatures). Block-to-block variance is large (543 / 773 / 519), so
  compare the class signatures as well as the total.
* **Then the normal flow.** Smoke and regression accept for the fungus keys; `test/check_gt_logs.py`. No
  overnight run between fixes. The keep table and value leaf stay in place: a regen after a play change is the
  USER's call (mulligan-profile Rule 0).

## 9. Open questions for the USER (defaults in brackets; nothing waits on them)

* **Q1** Season at slot 5, after the enablers that pay for it (Wild Growth, Sol Ring, Badger, Crossroads), rather
  than literally first among casts? [yes]
* **Q2** Beastmaster Ascension LAST in main 1 (equivalent to your §3c "third" whenever both pay, and more often
  payable)? [yes]
* **Q3** Badger before Crossroads by default, with R1 (Crossroads > Myconid > Badger) only when that is the order
  that pays? [yes]
* **Q4** The R2 concession: when same-plan producers must fund Doubling Season, cast them first (their tokens go
  undoubled) instead of dropping the set? [yes]
* **Q5** Build R5 (Wild Growth after Badger when the only spare `{G}` is a granted body), or leave it out as
  rare? [leave out; fold into R3 only if it falls out free]
* **Q6** Main 2 on v3: your 09-23 "everything else can be skipped" predates Shroofus, Badger and Crossroads. Today
  main 2 runs only for a landing Burst's pops (§1) and never casts, so "attack, Shroofus makes tokens, tap/sac them
  post-combat to cast" is unfindable at any budget. Open main 2 (`Both`) for casts when a post-combat mana route exists (Shroofus + Badger + haste, or
  Shroofus + Mycon)? [raise and measure; no change until you rule]
* **Q7** Propose generic items §7.1-§7.4 as a separate fleet change? [yes, after Fungus lands]
* **Q8** Default for `MTG_FUNGUS_CAST_ORDER` once measured: ON if net <= 0 on every Fungus cell? [yes]

Added 2026-11-13 with §5A/§5B (activations and Saprolings as a cost):

* **Q9** Landing-turn Burst pops in MAIN 1 (§5A A3). Today a Burst cast in main 1 is popped only in main 2, after
  combat, so with Crossroads out its hasted attack, quest counters and Shroofus triggers are lost on that turn
  (e.g. Season + Crossroads + a fresh Burst: two 13/13s for 26, this turn). That line is unfindable at any budget.
  Build a main-1 activation of a permanent the same main cast (a breakpoint after the cast, or a same-plan
  pending-source activation priced off the hand card's `fading_counters` and a same-plan Season)? [yes: raise and
  measure. It is a capability gap, so the completeness rule applies]
* **Q10** The Burst as GAS (§5A A2): conditional R7, which moves an on-board Burst's pops from the trailing pass
  to just after Season (and after Sporecrown when the k empties it), only when that is the order that pays the
  set. Needs a new activation-position axis and a Badger-tap credit for the tokens. [yes, built together with the
  §5B.1 bound credit; never ahead of Season]
* **Q11** Confirm that the FUNDING spore pop (§5A A5, its token eaten by Mycon to pay Season) is the one
  activation allowed before Season, and only when no Saproling is already on the battlefield. [yes; this is your
  09-27 ruling as it stands]
* **Q12** The maker fused into a same-line SacForMana is the first spore or fade source in battlefield order
  (§5A A8). Make it a searched choice by maker KIND (at most two fused actions: spore, fade) instead of a
  battlefield-order pick? [branch by kind. I would not rank spore first in the provider, since that is a pick]
* **Q13** Eat-side DOOMED-FIRST (§5B.2 row 1): a token this line's own pop kills is tapped and then eaten before
  any other fodder. Plus `IsDoomedPlanBody` crediting a same-plan Sporecrown (row 1a). [yes to both. Both are
  sound dominance in the payment, the same shape as `MTG_PAY_DOOMED_BODY_FIRST`]
* **Q14** Tap before eat (§5B.3): today every tappable body, attackers included, is tapped before any Saproling
  is eaten. Should a summoning-sick token that cannot attack or tap be eaten before an ATTACKER is tapped? [no
  change: your 09-17 "only if absolutely necessary" stands. Raised because your feedback asks exactly this]
* **Q15** Extend `MTG_FADE_PREFLOAT_DOOMED` so that doomed tokens are also sacrificed to a live Mycon before the
  emptying pop (tap `{G}` and then sacrifice: 2 mana per doomed token)? [defer until Q9/Q10. While the pop
  trails every cast, the float only reaches a continuation]
* **Q16** Fade-K PRICING, with no count change proposed. `FadeKLandmarks` is priced on the node's board, so it
  does not see a same-plan Season (token doubling) or Sporecrown (survival at 0). It has no Shroofus term: a
  hasted X/X Saproling that connects makes X tokens. It also takes no phase input. Read from the code and
  UNVERIFIED: in main 2 its `dmg_now_lo` can still count this-turn attackers (fresh hasted tokens pass
  `CanAttackFull`), so `k_win_now` may narrow the menu to a this-turn combat kill after combat is over, and that
  k is sized for this turn rather than for next turn's smaller bodies. Diagnose with `MTG_FADE_K_DUMP=1` on a
  landing-turn Crossroads board before anything changes? [yes, diagnose first]
* **Q17** R6 (§5A A3): once Q9 exists, Burst moves forward from slot 14 to just after Season, but only when its
  own landing-turn pops fund the rest of the set. [yes, conditional]
* **Q18** Your unbuilt *"(or 1 P/T larger in the second main)"* clause (§5B.2 row 3b): rank a Burst token as
  fodder in main 2 by its Burst's remaining counters, so a 0-counter, lord-held token goes first. [build only
  if Q6 opens main-2 casts; until then main 2 casts nothing, so it is inert]

## USER answers, 2026-11-13 (verbatim)
- **Q1:** "This is a difficult one. As with many other things it should be put as early as possible relatively to anything that generates tokens while still affording the line." -> Doubling Season is placed at the EARLIEST position ahead of every token/counter event at which the line still pays (a payability walk, not a fixed slot 6).
- **Q2:** "Beastmaster last is perfectly fine. It doesn't impact anything else and only needs to be out before the attack."
- **Q3:** "Sure."
- **Q4:** "Yes."
- **Q5:** "I don't think we need this. Wild Growth can go early."
- **Q6:** "We could check this. Unfortunately the budget cost might make it worse, but maybe with a very restrictive "saprolings produce mana" + "saprolings created by Shroofus this turn"?" -> measure a main-2 cast window opened ONLY when Saprolings produce mana (Badger grant / Mycon) AND Shroofus created Saprolings this turn.
- **Q7:** "If you want, sure. I don't really like using the generic order for anything except as a stopgap, so I'm not too worried either way."
- **Q8:** "Yes."
- **Q9:** "Yes, we need this for sure when we have haste. It's weird because I'm pretty sure I created an order previously that handled this. Was that for a previous version of the deck?"
  Owner note: the capability the USER remembers is engine-side, not a cast order: breakpoint site 9 (post-entry activation) lets the search pop a Burst in main 1 on the turn it lands; docs/design/fungus-v2-fd-diverge-7021.md (v2 list, Oct) shows a T4 `Badger[adventure] + Saproling Burst x3` continuation hasted by Crossroads. §5A's "a Burst cast in main 1 is popped only in main 2" (TurnSolver.cpp:7220-7266) must be re-checked against site 9 on v3 before building anything.
- **Q10:** "Yes, I could see that, given how many tokens it can produce."
- **Q11:** "Yes"
- **Q12:** "We never search multiple, you take from one source at a time given they act the same which they do. Searching them is too expensive and unnecessary." -> keep the single maker pick (no search); the proposal's default is rejected.
- **Q13:** "If they are going to die next turn and are equivalently summoning-sick and the same size then yes. If" (message ended there) -> doomed tokens are eaten first only when they otherwise tie with the alternatives (same summoning-sickness, same size); the same-plan Sporecrown part is not yet answered.
- **Q14:** "This is a tough question. Technically the answer should probably be that we keep the attacker and sacrifice the summoning-sick token if this gives us lethal this turn. Otherwise we keep both alive. This might have to be tested heuristically. But yes, the default would be to tap and keep our critters alive." -> default: tap, keep both; candidate (heuristic, measured): eat a summoning-sick token instead of tapping an attacker only when that attacker is needed for lethal this turn.
- Q15-Q18: not yet answered.
