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
  Main2/Both (`TurnSolver.cpp:7170`). So in searched play on v3 **every cast is main 1 and main 2 never runs**.
  See Q6.

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
| 7 | **Utopia Mycon** | 7 | [after Badger+Crossroads .. 7] | `{G}` 0/2 Fungus. *"Sacrifice a Saproling: Add one mana of any color"* (no `{T}`, so it is legal the turn it is cast). It is the cheapest mana source among the remaining creatures. Caveat: the engine collects sac-for-mana from the battlefield in a pre-pass, so a Mycon cast this main does not sac this main (`MTG_SAC_OUTLET_PAY` OFF). Its Badger tap is real |
| 8 | **Thallid** | 8 | [after Badger+Crossroads .. 8] | `{G}` 1/1 Fungus. An upkeep spore engine; nothing at cast. Under Badger + Crossroads it pays its own cost back (`{G}` in, `{G}` out) |
| 9 | **Sporecrown Thallid** | 9 | [after Badger+Crossroads .. 9] | `{1}{G}` lord, +1/+1 to other Fungus/Saprolings. The P/T bonus is a static that applies at combat, so its main-1 position is inert for damage. Burst pops trail every cast, so a Sporecrown cast in the same main is already out when the fade-K menu (`FadeKLandmarks`, `TurnSolver.cpp:35546`, which reads the anthem) is priced and when a pop takes the tokens to 0 counters. The order already enforces "Sporecrown before Burst pops that would otherwise kill". Under Badger + Crossroads it pays back 1 of its 2 |
| 10 | **Shroofus Sproutsire** | 10 | [after Badger+Crossroads .. 10] | `{2}{G}` legendary 1/1 Saproling. Its combat-damage tokens come at combat (doubled by a main-1 Season), so the order is inert for them. With Crossroads it attacks the turn it lands. Under Badger it taps for `{G}`, but a tap costs its attack (a PAYMENT choice, not an order one; §5.4) |
| 11 | **Saproling Migration** (unkicked) | 11 | [after Badger+Crossroads .. 11] | `{1}{G}`: two 1/1 Saprolings (4 under one Season). After Season. With Badger + Crossroads live, the tokens pay back 2 (4), so it is a net-0 (net +2) ritual. Cheapest token maker first |
| 12 | **Fungus Frolic** (Badger's adventure, Instant) | 12 | [after Badger+Crossroads .. 12] | `{2}{G}`: two 1/1 Saprolings. After Season. Same token rate as Migration for one more mana. When a plan holds both Frolic and that Badger cast from exile, the rules already force Frolic first (the card must be exiled before the creature can be cast) |
| 13 | **Saproling Migration** (kicked, `{5}{G}`) | 13 | [after Badger+Crossroads .. 13] | Four Saprolings (8 under Season). After Season. Costs the most of the token makers, so it comes last of them. It shares a hand slot with the unkicked mode, so only one of 11/13 is ever in a plan |
| 14 | **Saproling Burst** | 14 | -- | `{4}{G}`, Fading 7. Its fade counters are PUT ON as it enters, so Season must already be out (14 counters, and every pop's token doubled). It makes nothing at cast; its tokens come only from pops, which trail every cast. So it funds nothing in its own plan, and it goes after everything that can |
| 15 | **Beastmaster Ascension** | 15 | -- | `{2}{G}`. *"Whenever a creature you control attacks, you may put a quest counter"*. The counters arrive at declare-attackers, so main 1 is enough, and nothing in main 1 reads it. It funds nothing and gets nothing from a later cast, so it goes last. (USER §3c listed it third; §5.1 explains why last is equivalent, and Q2 asks) |
| -- | Burst fade pops, Thallid/Mycon spore pops | trailing | -- | Unchanged. They run after every cast, so Season, Sporecrown and Crossroads cast in the same main are already out. This is the USER's "Season before any activations", which holds by structure |
| -- | Utopia Mycon sac-for-mana | pre-pass | -- | Unchanged |

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
* Sporecrown / Season / Crossroads before Burst pops: these hold by structure (pops trail the casts). The rank
  just keeps them that way.
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

## 6. Main phase (unchanged by this proposal)

Every v3 card stays pinned to Main1 (USER 2026-09-23: *"Only Mycoloth should go in the second main ... Everything
else can be skipped"*). With no Mycoloth in v3, main 2 never runs in searched play (§1). **This is a USER question,
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
  * a Season + Burst board showing 14 fade counters.
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
  main 2 never runs, so "attack, Shroofus makes tokens, tap/sac them post-combat to cast" is unfindable at any
  budget. Open main 2 (`Both`) for casts when a post-combat mana route exists (Shroofus + Badger + haste, or
  Shroofus + Mycon)? [raise and measure; no change until you rule]
* **Q7** Propose generic items §7.1-§7.4 as a separate fleet change? [yes, after Fungus lands]
* **Q8** Default for `MTG_FUNGUS_CAST_ORDER` once measured: ON if net <= 0 on every Fungus cell? [yes]
