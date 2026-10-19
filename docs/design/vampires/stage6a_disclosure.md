# Vampires -- Stage 6a: encoded heuristics & assumptions disclosure

Compiled at `f15b8a493` (branch `vampires-analysis`), UPDATED 2026-10-19 for everything resolved since (D1-D3 fixed,
5c2 / 5i measured, the viewer sweep run). Deck `decks/Vampires/Vampire.cod`,
profile `decks/Vampires/Vampire.profile.json`, provider **Vampires** (`VampiresProvider : VialProvider`), play
settings **d5 b20** (source=default; no value sidecar, no keep table). "Q<n>" = open question n in
`docs/design/analysis-Vampire.md`; "S5f-Q<n>" = `docs/design/vampires-search-cost.md` §4. Paths: TS =
`src/ai/TurnSolver.cpp`, SE = `src/core/SpellEffects.h`, DP = `src/ai/DecisionProviders.cpp`, DPh =
`src/ai/DecisionProviders.h`, AI = `src/ai/AIEngine.cpp`, GFR = `src/runner/GoldFishRunner.cpp`.
Compiled read-only from the code (an Opus agent; verified against the code by the orchestrator where it mattered).

## 1. Global engine assumptions in force

| assumption | source | classification | what it costs / why safe |
|---|---|---|---|
| Passive opponent: never casts, blocks, attacks, gains or prevents life. It DOES get scheduled creature spawns in **8 of 10** game slots. Slot 2: six 1/1s to 2/2s on T1-T3. Slot 3: three 2/2s. Slots 4-6: one 1/1, 2/2 or 3/3 on T1. Slot 7: a 4/4 on T3. Slot 8: a 6/6 and a 1/1 on T3. Slot 9: 1/1, 1/1, 2/2. | GFR:778-803 | global model | This is what makes Captivating Vampire's steal LIVE (a stolen 6/6 attacks as an 8/8). Slots 0-1 have no targets, so the steal's value depends on the game index. Everything that only matters against blockers or removal is inert (section 2). |
| Clairvoyant search over a known library (deterministic CRN shuffle) | engine-wide | global model | The search, the bottoming trial games, and Pit Fighter / Blood draw choices all know the draws. Every result is an upper bound on non-clairvoyant play. |
| Searched **second main: ON**. Four independent `DeckUsesSecondMain` clauses fire. | GFR:76 `activate_only_if_opp_lost_life` (Falkenrath Pit Fighter); GFR:84-85 `etb_each_other_own_creature_counters_if_opp_lost_life` / `opp_lost_life_other_enters_counters` (Vampire Socialite); GFR:86 `attack_one_or_more_tokens` (Mavren Fein); GFR:101 `convoke` (Markov Baron, the Melira Pod / Chord clause) | deck-derived setting | Both mains are searched for every card. The drafted main-1/main-2 view (`MainPhaseOverride`) is display-only, because the provider opts into neither `ClassifiesMainPhases` nor `CondemnsPassedMainPhase`. |
| Main-2 land drop = newly arrived lands only (USER 2026-09-30), plus a Vampires exemption while a Bloodghast is in the graveyard or can be put there | TS:2953-3004; `MTG_VAMP_GY_LANDFALL_HELD_LAND` default ON (`src/ai/EngineFlags.h:78`) | USER prune + widening exemption | Without the exemption, "Pit Fighter sacrifices Bloodghast post-combat, then play the held land" is unfindable at any budget. **Q1** (USER). Human play is exempt from the bar. |
| Depth 5, budget 20 | profile has no `value_play` -> default | setting | Budget-bound churn; the d8 b0 recoveries are in the GT commit notes. |
| Mulligan: **STATIC keep, no keep table.** 1-5 lands; `curve_check two_drop` (at least 2 lands and a spell of MV <= 2, first two hands only); `min_playable 0`; `stop_at 4`. `card_scores` are present but `hand_score_threshold` is -1e18, so it never mulligans on score. | profile; AI:1131-1160 | generic analyzer keep | **Q28** (`min_playable 0`; USER-owned; the keep table replaces it). |
| Lookahead **bottoming** is on whenever depth > 0. Every candidate bottom is played out as a full clairvoyant game at play settings (`bottom_eval_*` unset). | `src/ai/AIEngine.h:160`; AI:1924-1932 (`MTG_BOTTOM_ROLLOUTS` is a sizing probe, default ON) | clairvoyant search | 99.7% of a slow game's units are these trial games (`vampires-search-cost.md` §1). The keep table removes them. |
| **Own-death checks LIVE** (`own_death_live`). Armed by Asylum Visitor's `hellbent_upkeep_life_loss` and Voldaren Estate's `tap_blood_cost`; no other deck carries either. | GFR:1505-1522; checks at `src/core/GameEngine.cpp:214-234`, TS:67016/67147/67226 | correctness | **Q6**. A loss to 0 at upkeep is detected at the post-main-1 check, because there is no upkeep SBA checkpoint (**Q19**). |
| **First-strike two-step combat damage** (CR 510.4). Unconditional whenever an attacker has first or double strike. Drana's counters land between the steps; there is no priority window between them. | `src/ai/Combat.cpp:180`, gate :358-375 | correctness fix (all decks) | **Q2** (Critter and Angels moved). **Q27**: one Critter reference now replays T4 vs the recorded T5. Under a Stromkirk Captain, Drana's counters add nothing this combat. |
| Graded no-win leaf: `GradesNoWinLeaf` inherited ON; `NoWinLeafPricesOwnLife` OFF | `src/ai/DecisionProvider.h:1217, 1227` (no override in Vampires or Vial) | leaf tie-break (fleet default) | Own life (Estate pain, the Visitor loss) is not priced at the leaf. **5c2 measured: NO SIGN** -- 0 win turns changed of 12,000 paired games at play settings (the lever fires, never binds; the deck wins inside the horizon). Q44. |
| **Aether Vial charge = the root hand-aware heuristic** (`WantVialCharge`). The searched charge axis `MTG_VIAL_AXIS` is default OFF by USER ruling 2026-08-30, which was measured on other decks. Deck charge target = **3**: MV 2 and MV 3 tie at 12 creatures each, and ties go up. | SE:20159; `src/ai/EngineFlags.h:1556-1604`; GFR:478-497 | USER-sanctioned one-option heuristic in the window | "Deploy a cheap creature now vs climb" is not searched. The Vial PUT is searched: one action per (Vial, creature), TS:30034. |
| Attack declaration and mana payment | engine-wide | standing greedy exemptions | Payer quality is governed by the payer levers in section 3. |

## 2. Card-modelling simplifications (bracket notes in `src/cards/data/cards.json`)

Stage 3 coverage is 20/20. Every other note is an implementation description. **PROVISIONAL** = a goldfish-inert
deferral awaiting the USER's sign-off (**Q8**).

| card | simplification (short quote) | classification | why inert / what it costs |
|---|---|---|---|
| Falkenrath Pit Fighter, Stromkirk Condemned | "instant-speed timing collapsed to the main phases' trailing passes" | PROVISIONAL | A post-damage combat activation reaches the same state as one at the start of main 2. A combat pump buys nothing a main-1 pump does not. |
| Bloodghast | "can't block is inert"; "The 'may' is resolved YES by a dominance argument" | PROVISIONAL (both) | It never needs to block a non-attacker. YES is a one-option rule in autonomous play (**Q7**); human play is asked (`revive`). The audit found no case where declining is better. |
| Voldaren Estate | "'Vampire spell' is simplified to 'creature spell'" | exact for this list | Every creature is a Vampire. The only noncreature spell (Vial, {1}) is paid identically by {C}. |
| Unclaimed Territory, Secluded Courtyard | ETB "choose a creature type" simplified to "any creature"; Courtyard: "Ability sources are simplified to battlefield creatures" | exact for this list | All 14 creature cards are Vampires; there are no any-zone creature abilities. Territory's search-side pool counts it as any colour for an ability pip, so an unfundable variant is a duplicate, never an illegal play (**Q32**). |
| Aether Vial | "[AI heuristic: adds counters each upkeep until reaching the most common creature MV in hand ...]" | ~~stale note~~ FIXED (a537d4e88) | The note now describes `WantVialCharge` (hand-aware hold/climb, then pre-charge to target 3); puts are searched. |
| Captivating Vampire | own creatures not offered as targets; no opponent-turn window; attached Auras/Equipment unmodelled; one steal per Captivating per main phase | PARTIAL, PROVISIONAL | Stealing your own creature is a legal no-op (**Q20**). The end-step window is equivalent to the post-combat main. Spawns carry no attachments. A second steal needs ten untapped Vampires. |
| Bloodline Keeper | no opponent-turn window for the token tap; Flying inert | PARTIAL, PROVISIONAL | Equivalent to tapping in our main when the Keeper does not attack. |
| Lord of Lineage | "its mana value on the battlefield reads 0 ... where the rules give the front face's 4"; Flying inert | PARTIAL, PROVISIONAL | Nothing in the list reads a battlefield permanent's mana value (**Q25**). |
| Markov Baron | convoke: "the no-convoke cast is dropped only while a free body exists"; costly bodies "lowest power first" | weak-dominance prune + order | **Q16** and **Q5** (true power). Every k is a searched arm. Madness arms: defects D1/D2, FIXED (section 5). |
| Vampire of the Dire Moon | "Deathtouch is parsed ... but has no reader" | PARTIAL, PROVISIONAL | It only ever damages the opponent's face. |
| Vampire Socialite | "Menace is inert"; a Vampire entering simultaneously with Socialite does not get the static's counter | PARTIAL, PROVISIONAL | No blockers. No card puts two Vampires onto the battlefield in one event (D-C3). |
| Drana | "Flying is inert"; "no priority window between the two damage steps" | PARTIAL, PROVISIONAL | The missing window is **not strictly inert**: it costs a Vial put or a madness-cast Socialite between the steps, about 1% of games or fewer (needs Drana + Socialite + an enabler + no Captain). |
| Gorger, Lieutenant, Captain, Asylum Visitor, Mavren, Blackcleave Cliffs, Blood Token, Swamp | implementation notes only | fully modelled | Lieutenant's "Other Vampires" applies to creatures only, which is exact here (Blood tokens are untyped). |

## 3. VampiresProvider heuristics (authoritative list)

**Routing:** the `vampires` signature is an OR over Vampires-only params on six cards (Pit Fighter's discard
outlet and its lost-life restriction, Condemned, the Estate's Blood ability, Bloodghast, Gorger's grant keyed on
"Vampire", the steal, Keeper's transform). It returns at the top of the chain, above `melira_pod`, `goblin` and
`vial` (DP:21765-21785, 22241). Its own hooks are behind `MTG_VAMP_DRAFT_ORDERS` (DP:19156, **default ON**,
AGENT-DRAFTED, USER review pending: **Q11**) unless noted otherwise.

| hook / policy | what it decides | source | classification | flag (default) | what it could miss / why safe |
|---|---|---|---|---|---|
| `ActivationDiscardCandidates` | Order of the searched discard pins for Pit Fighter, Condemned and the Blood crack. Bloodghast first when a land in hand will return it; then lands past 4 (a draft constant); then cards with MV above the available mana; then the rest; then kept lands. Stable over the inherited slivers ranking. | DP:19174-19231 | **Ordering only** (every name returned) | DRAFT_ORDERS (ON) | Move ordering and the beyond-horizon pick only. Q11. |
| `SacOutletVictimCandidates` | Order of Pit Fighter's victims: returnable Bloodghast, then a sick nontoken body, then Pit Fighter itself, then tokens, then others, lords last; lowest power within a tier | DP:19233-19260 | **Ordering only** (every class kept) | DRAFT_ORDERS (ON) | Q11. |
| `ActivationOrderRank` + engine count bands | The **execution sequence** of the trailing pass: token tap 0, Blood creation 1, Condemned 2, crack 3, Pit Fighter 4. When a steal or transform is present: band 0, then the steal, then the transform, then sacrifices. | DP:19262-19271; TS:45849-45925 (`OrderTrailingActivations` / `TrailingRunsBefore`) | **Pruning (fixed order), not ordering-only.** Each subset gets exactly one execution order. The class header (DPh:1091-1101) calls all three hooks "ordering, never removes one"; that is wrong for this one. | DRAFT_ORDERS (ON); =0 leaves the bands and plan order | Unexpressible within one main: a madness Vampire from Condemned or the crack followed by a cheaper Blood (**Q34**); a steal followed by a cheaper Blood (**Q43**); crack-then-pump-the-drawn-card works only through the crack's breakpoint (Q34). **New from this audit, not in the ledger:** Keeper token -> hand-cast Baron convoking that token (casts run before activations); steal -> Baron convoking the stolen body; steal -> Pit Fighter sacrificing the stolen creature (victims are the pre-plan board plus hand names). Main 2 or a breakpoint recovers some of these. |
| `MadnessCastDefault` | The unpinned madness answer. Base: cast option 0 (cheapest cost, every free convoke body). Vampires override: **decline** for Bloodghast in a main phase while the land drop is open and a land is in hand. | DP:19273-19284 (base DP:736) | In the window: **ordering** (front of the searched fan). At cleanup: a **one-option dominance rule** (the override does not apply at cleanup, so cleanup uses the base rule). | DRAFT_ORDERS (ON) | **Q18, Q4 (PROVISIONAL).** Cleanup madness is never branched (GameEngine.cpp:1002, TS:52434). The audit found a gap in Q4's argument: casting Asylum Visitor at cleanup risks a 1-life hellbent upkeep loss, and with `own_death_live` that can be lethal at 1 life. Theoretical. |
| `PostCombatPumpDiscardFoldOptIn` = true | Engages subset rule 9 (`MTG_VAMP_M2_PUMP_DISCARD_FOLD`) | DPh:1127; TS:11976-12053 | **Dominance fold** (deck half vouched by the provider) | ON | **Q39 / S5f-Q1.** d5 b100: 60/60 win turns identical. Boundary defect D3 FIXED (section 5). |
| `CastOrderRank` / `CastOrderTierName` / `CastOrderTotalByName` / `MainPhaseOverride` | AI-drafted total cast order (Socialite > Gorger > Captain > Lieutenant > Captivating > others cheapest-then-name > Baron > Vial) and the main-split view | DP:19314-19405 | Ordering (USER-owned) | `MTG_VAMPIRES_ORDER` **OFF** | **The deck plays the GENERIC order today**: VialProvider's Knights clauses are param-inert here, so GenericProvider tiers apply (every creature tied at 10 in plan order, Vial at 20). USER review is pending (`vampires-cast-order-2026-10-18.md`). |
| `CleanupDiscardCandidates` / `CleanupDiscardShedStable` (Vampires) | AI-drafted 5i buckets (FREE madness / returnable Bloodghast > dead > lands past 4 > ... ). When ON, they also re-order the outlet discard pins. | DP:19443-19732 | Provider discard rule (USER-owned) | `MTG_VAMPIRES_BUCKET_DISCARD` **OFF** | Pending USER (same doc). |
| **Inherited** `VialProvider::CleanupDiscardCandidates` (slivers_vial buckets) | **Which card the cleanup sheds**, in both worlds. The provider's front pick, unbranched: `CleanupDiscardSearchWidth` = 1; the executor takes `cand.front()` (AI:8860-8861). | DP:10632-10640ff | Provider discard heuristic (USER: discard is always a provider heuristic) | `MTG_SLIVERS_BUCKET_DISCARD` **ON** | **Q12.** Scaled from `vial_target_mv` = 3: hard target 4 lands, soft up to 6, 3 coloured. "Lords first" = the four Vampire lords. Body value = 100 x printed power. Vial kept on turn 1 only. **It knows nothing about madness or Bloodghast's return.** Executor cleanup sheds: 0 in 400 d3 games; rollout sheds: 64,621. **5i bound measured ~0** (`MTG_SHED_WORST`: d3 -1 / 3,000, d5 0 / 1,500) -- the ranking is worth nothing at play settings. |
| Inherited `VialProvider::CastOrderRank` (Knights order) | Worthy Knight / Contender slots | DP:9744-9784 | Ordering | `MTG_KNIGHTS_ORDER` ON | **Inert:** no Vampires card carries `cast_trigger_creates_tokens` or `etb_dig_*`. |
| Inherited Generic root hooks | `WantVialCharge` (above), the attack shortcut, `ManaSourceRank`, the default `SacExpendabilityRank` (the base under the victim order) | DP:523, 3932 | root defaults / standing exemptions | -- | As section 1. |
| `MTG_VAMP_OUTLET_NOT_GROWTH` | A discard-cost sac outlet no longer disarms `OptimisticTurnMana`'s ceiling | TS:24948-24960 | **Sound** (the ceiling prunes only never-payable lines) | ON | Engine-wide by params, **not** a provider opt-in (only Pit Fighter matches). S5f §2.3. |
| `MTG_VAMP_MADNESS_COLOR_FOLD` | Drops a madness cast variant that no colour of the optimistic pool can pay | TS:26939-26969 | **Identity fold** (it would resolve as the decline) | ON | Engine-wide for any madness deck, **not** a provider opt-in. Exact for this list. |
| Payer levers `MTG_PAY_PAIN_LAST`, `MTG_PAY_NARROW_RESTRICTED_FIRST` | Pain-costing colour as the last resort; spend Territory or Estate colour before Courtyard colour | `src/ai/EngineFlags.h:244, 259` | Payment ordering (standing exemption), engine-wide | ON | Held-out A/B: every deck net <= 0, zero slower searched games. Also: `MTG_SAC_OUTLET_PAY_SCOPE` (rules fix, :60) and `MTG_ACT_LINE_HOLD`'s `act_pips_cab` reserve for Pit Fighter's and the transform's pips (SE:30108). **Q36/Q37** (payment-quality residue). |
| Beyond-horizon playout policies | Rollout leaves emit only the front madness variant, convoke arm 0 (no plan-body arms) and the front steal variant | TS:26977, 35865/35917, 26817/26827 | **Permitted playout policy.** Gate `!(g_search_candidate_enum \|\| g_condemn_root_turn < 0)` verified as remaining depth <= 0 (only `SolveUncached` under a `GreedyPermit`, TS:39865-39896). Never fires on the root turn's main 1 or main 2. | -- | **Q15, Q21, Q35.** The steal variant ORDER is an engine-neutral eval, not a provider hook (**Q22**). |
| Other in-engine prunes / folds | Convoke dominance (**Q16**); within-arm bodies free first, then ascending true power (**Q5**, SE:15426); combat-transform fold (**Q23**); no combat-transform mana hold (**Q24**); optimistic madness gate (**Q14**); identity folds (discard per name, pooled Pit Fighter source `MTG_SAC_OUTLET_POOL`, canonical Blood, canonical identical-Condemned assignment, steal body classes lowest-numbered, identical Estates) | various | identity / weak dominance | ON | Settings not taken: `FoldSearchOdometerOptIn` (**Q33/Q42**), `BpChainSlotOptIn` (**Q40**), EMPTY-arm fold (**Q41**, not built). |

## 4. Play-viewer auto-resolved decisions

Static audit: `audit_viewer_decisions.py --no-sweep` rc 0. Expected types {dig, discard, madness, revive, target,
vial_charge}; oracle cross-check clean. **Every card-specific choice is surfaced in claude-play**, with these exceptions:

| card / choice | how it resolves | why inert / follow-up |
|---|---|---|
| Which Blood is cracked; which Pit Fighter / Condemned / Estate copy | First live copy; duplicate menu entries hidden | Provably inert: identical copies, no {T} on the creatures. |
| Estate / Territory / Courtyard: {C} vs pay-1-life colour; which restricted land pays which pip | Engine payer. The tap override offers only the {C} face of these lands. | Standing payment exemption. Not strictly inert (life feeds the own-death checks; Courtyard must stay free for Pit Fighter's {R}). |
| Convoke bodies **inside** an arm (madness and hard cast) | Free bodies first, then (true power, card number) | Not inert among equal-power attackers (Drana / Mavren vs Gorger). **Q5.** |
| **Markov Baron hard-cast convoke arm in the GUI** | `CheckLine`'s signature has no convoke token (TS:83470-83476, dedup ~83605), so arms with the same casts collapse to the first payable one. Claude-play does list every arm as a separate plan. | **Viewer-wiring follow-up:** add a convoke sub-choice to `CheckLine`. |
| Trailing activation order | Claude-play: fixed (listed in executed order, `main.cpp:360-386`). GUI: the click order is applied as-is. | The menu's legality filters assume the default order, so a custom-order line can take two lines (**Q34**). |
| Attack declaration | Automatic | Viewer-wide follow-up, not specific to this deck. Not strictly inert: tapped attackers cannot pay a main-2 steal, token tap or convoke (do those in main 1). |
| Unclaimed Territory / Courtyard creature type | Not modelled ("any creature") | Provably inert (all creatures are Vampires). |
| Between-damage-steps window | Does not exist (search or human) | D-C4, PROVISIONAL, about 1% of games or fewer. |

Auditor state:
- Asylum Visitor's three INERT rows and cluster C's combat INERT rows are **PENDING USER OK** (**Q31**).
- `sacrifice` is never an expected type: Pit Fighter's `sac_creature_outlet` is classed INERT, so the sweep never checks the sacrifice prompt.
- The oracle patterns cannot match "Sacrifice a Vampire" or "choose a creature type".
- The dynamic viewer sweep was run 2026-10-19 (section 5): clean once the two STATE-GATED types were recognised.
- Verdict: **viewer-ready on the card side in claude-play**, with one GUI follow-up (the Baron convoke arm).

## 5. In-window defects this audit found -- all three FIXED (2026-10-19)

Found by reading the code, then reproduced on the pre-fix engine (every new unit test failed there) and fixed on
`vampires-fixn` (cherry-picked 3cd49a776 / 1b1d2d0e9): **D1** madness convoke arms priced on the pre-plan board and
pinned by index (now pinned by COVER, realised at the trigger; subset rule 10); **D2** no convoke contention on the
madness path (mirrors the hand-cast exception); **D3** rule 9's hand-size exemption off by one (`h + 1 > 7`) and the
land-after clone's held land. Melira Pod / Knights byte-identical; 90 paired Vampires games, 0 moved. Ledger section
"Stage 6a disclosure defects D1-D3".

**Viewer sweep (run 2026-10-19):** the auditor's dynamic sweep surfaced discard / madness / sacrifice / target /
vial_charge; `revive` (Bloodghast) and the combat-transform `dig` (Bloodline Keeper) are STATE-GATED -- they arise
only with a Bloodghast in the graveyard at a land drop, and a fifth Vampire at declare-attackers -- each pinned by a
unit test that drives the human chooser plus a hand-built `--claude-play` repro (revive: seed 91011, choices
`0,1,1,0,-1,-1,0,0,1,-1,-1,38`). The auditor's unverified-tail escalation now honours the STATE-GATED list (it did
not, which made Keeper's state-gated type read as a HARD MISS).

## 6. Things the user may find surprising

1. **The deck plays the generic cast order.** The AI-drafted Vampires order and buckets ship OFF pending your
   review (USER 2026-10-14: "the generic order is never a good option"). Measured 2026-10-19: the order is win-turn
   neutral (0 of 16,000 paired games moved); the buckets net -14 / -2 / 0 / -2 / -3 with 0 slower searched games.
2. **The cleanup shed uses slivers_vial's buckets** (Q12). Scaled to this deck's charge target of 3, that means 4
   hard lands and up to 6 soft. The rule has no notion of madness (a madness-castable creature is not preferred)
   or of Bloodghast's free return. It is unbranched in both worlds.
3. **The trailing activation order is a fixed execution sequence**, not move ordering. Q34 and Q43 cover two
   cheaper-Blood lines; this audit found three more (token -> Baron convoke, steal -> Baron convoke,
   steal -> Pit Fighter sacrificing the stolen creature).
4. **The Vial charge is not searched in this deck** (USER ruling 2026-08-30, measured on other decks).
5. **The steal is live only in game slots 2-9**, so the steal's value depends on the game index.
6. **Drana's missing between-steps window is not strictly inert** (about 1% of games or fewer).
7. **Three in-window defects (D1-D3)**, found by reading the code -- since reproduced and FIXED: two in Baron's
   madness-convoke arms and one at rule 9's hand-size boundary.
8. The "must read 0" in-window counters only see search frames, not the executor's real play. No path was found
   where real play applies an unpinned outlet or steal.
