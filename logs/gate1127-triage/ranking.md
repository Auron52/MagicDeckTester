# Gate `claude-queue-20261127a` triage: engine-miss clusters ranked (2026-11-28)

Source: the gate ran 16 decks at src `80cb93166cfb` (commit `f5f4428019e1`, PGO binary, `MTG_MANA_ORACLE=2`).
Its report is in `.claude/worktrees/land-1131/logs/mana_clean/claude-queue-20261127a/report.md`, and its raw chunks
are in `build/wt/land-1128/scratch/mana_clean/claude-queue-20261127a/on.<k>.{out,err}`.

Out of scope, as briefed:
- Blood Sun (oracle cost; another agent).
- The Dragonstorm Karrthus need-later cluster (DS round 5, `MTG_VISIBLE_CLOSER_KEEP`): 40 events / 40 games.
- Treasure Hunt s30101394 (a known payment imperfection).

**Method.** `parse.py` reads every whole ISSUE flag line of a job, counted only from the attempt that finished that job
(the gate's own rule). `events.py` and `clus.py` collapse the lines into EVENTS: one decision (deck, seed, turn, phase)
is one event, whatever its number of flag lines or menu entries. The events are then grouped by mechanism, using the
dropped action, the plan's shape and the code.

**Lower bounds.** The gate keeps only the first 5,000 whole lines per deck and class per attempt. Four classes were
truncated, so their event counts are lower bounds:
- Creature Giving DOMINATED-CREATURE: 9,727 flags, about 4,700 events.
- Beasts APPLY-DROP-ORDER: 10,098 flags, about 1.5x the events shown.
- Beasts ORACLE-GAP: 14,108 flags.
- 1 Drop Burn ORACLE-GAP: 15,853 flags, about 4,200 events.

**Impact scale** (per affected game):
- **H**: plausibly costs a turn in a sizeable share of the games.
- **M**: sometimes costs a turn.
- **L**: rarely costs a turn; usually a board of near-equal value.
- **0**: no play value.

These are code-and-board judgements. The single-game repros are listed per cluster. Section 4 says which ones ran.
**No fix was A/B-measured here.**

**Something to know first.** Several fixes landed after the gate's commit, so its counts overstate the HEAD tree:
- Oracle-only reclassification (`eb39bc311`): `MTG_ORACLE_EG_PROVIDER_PRUNE`, `MTG_ORACLE_DROP_WON`,
  `MTG_ORACLE_UNDECL_MENU_COVER`.
- Dragonstorm payer rounds 2-4: `dea141a1e`, `33963c528`, `27965f084` (`MTG_LINE_SEQ_REST`,
  `MTG_UNSEEN_SCARCE_RITUAL_NET`).
- The ritual/sacrifice order fixes: `945228291`, `37ae8b04c`, `6588deec8`.

So Treasure Hunt, the non-Karrthus Dragonstorm clusters and part of Worldfire / Shadowborn should be re-gated at HEAD
before anyone fixes them.

## 1. Ranked engine-miss clusters

Rate = affected games / the deck's gate games.

| rank | deck | cluster | events | games (rate) | impact | fix kind |
|---|---|---|---|---|---|---|
| 1 | Beasts | Urza's Incubator cast AFTER the Beasts it discounts | 1,592+ | 1,112+ / 12,000 (9.3%) | M | play change (order rescue); a rank change is USER-owned |
| 2 | Beasts | Nykthos devotion not credited by the gates or the walk | 336 ENUM-GAP + ~245 drops | ~500 / 12,000 (~4%) | M-H | play change |
| 3 | Beasts | Kaheera companion {3} and land-Aura continuation drops (Cavern's {C} and the host float) | 516 + ~150 | ~550 / 12,000 (~4.5%) | L-M | play change |
| 4 | Worldfire | continuation float drained by a ritual; Lotus Bloom crack colour; Brainstorm-first order | 269 | 261 / 12,000 (2.2%) | M on combo turns | re-gate at HEAD first; then play change |
| 5 | Stuffy Doll | Chain Reaction before Blasphemous Act strands Act (USER order slots 11/12) | 86 | 79 / 17,000 (0.46%) | M (main-1 lethal-projected plans) | play change; the fallback order is USER-owned |
| 6 | Creature Giving | Crop Rotation's victim is sacrificed untapped (Birds tapped instead), and chained Crop Rotations are under-counted | ~4,700 DOM-CREATURE + 413 ENUM-GAP | >=2,232 + 374 / 63,000 (>=4%) | L (Birds is a 0/1) | play change; tap-first is a sound dominance |
| 7 | Dragons | two-unit land / Haven generic choice; unbound continuations after Mind Stone's dig | 255 | 244 / 100,000 (0.24%) | L-M (4 events lose a main-phase kill) | play change |
| 8 | Hydras | the USER-rule fetch-between-Blooms rescue (`ArrangeCastOrder`) did not fire | 95 | 90 / 29,000 (0.31%) | L-M | correctness fix under a USER rule; play change |
| 9 | 1 Drop Burn | Vexing Devil / Goblin Guide order and Needle Drop before Skewer; Grim Lavamancer ping drops | 99 | 99 / 100,000 (0.1%) | M per game (Devil), L (Lavamancer) | play change; the Devil order is USER-owned |
| 10 | Sea Creatures | Sol Ring vs ramp land Aura tie at rank 5; Utopia Sprawl colour pin; T2 Aura chains | 150 | 143 / 28,000 (0.5%) | L | play change |
| 11 | Angels | the keep-newest second Giada: the old Giada's Angel-only {W} is held "for attack" though she dies to the legend rule | 159 | 149 / 100,000 (0.15%) | L | sound dominance; play change |
| 12 | Mill | Visions of Beyond + Surgical Extraction ENUM-GAP: an equal-value board, gate unconfirmed | 280 | 268 / 19,000 (1.4%) | ~0 (see the cluster) | likely oracle-side (cover) -> exact; else play change |
| 13 | Dragonstorm (non-Karrthus) | ENUM-GAP with one Apex + closer (`ApexPairGateCredit`); Kolaghan / Scourge / Lathliss drops | 24 + 38 | 24 + 38 / 50,000 | L | drops mostly fixed after the gate; ENUM-GAP is a play change |
| 14 | Treasure Hunt | undeclared Land's Edge drops, {TH + TH} ENUM-GAP, Throes re-plan | 314 + 274 + 89 | 312 + 274 + 89 / 82,000 | ~0 | mostly reclassified at HEAD by `eb39bc311` (oracle-only); re-gate |
| 15 | Shadowborn Apostle | undeclared Thrumming Stone tutor with a Lake of the Dead crack; Lotus Bloom keep-owed | 23 | 23 / 25,000 | L (rare) | park unless a d8 b0 repro loses a turn |

**Carried, not in this gate (no new count):**
- Hinata gi548 and gi33: the prepay floating {C} (`wip/hinata-gi548`, `logs/prepay-c/prepay-c-handoff.md`) and
  `docs/design/hinata-d4-vs-d5-2026-11-21.md`.
- Maze Summit 2HG g292 (5 -> 6 unrecovered; `wip/maze-summit`).
- Unpredictable Cyclone d6.
- Prepay step 5.

All four are tracked in `docs/design/handoff-mana-chain-to-claude-2026-11-17.md`.

**Dragons two-dig** (`docs/design/dragons-eng-2026-11-26.md:64-67`: three lines with two Mind Stones beside Sol Ring)
is NOT what this gate's Dragons cluster mostly is. Only 16 of the 255 events hold two Mind Stones, and 1 holds Sol
Ring. It stays carried as a small residue of cluster 7.

## 2. Oracle-side work (not engine misses), ranked separately

| rank | deck | cluster | events | games | mechanism | fix |
|---|---|---|---|---|---|---|
| O1 | 1 Drop Burn + Creature Burn | Fireblast ORACLE-GAP | 2,610 shown (~4,200+ with the cap) | 2,493+ / 200,000 | `Realize` builds the plan's own sources only from `sac_source_id` / `sac_victim_id` / `enchant_target` (ManaOracle.cpp:4236-4246) and never reads Fireblast's victim pin `fireblast:<n1>,<n2>` (TurnSolver.cpp:46522; parser at :13379-13385). So the pinned old Mountains fold with the new one (`AgeIsInert`, ManaOracle.cpp:736), and the engine's own payment (the two victims pay, the new Mountain stays up) is pruned as a twin. Walk the Aeons' `buyback:` pin has the same gap. | parse the pin into `plan_srcs`; exact (oracle only) |
| O2 | Beasts | ORACLE-GAP, 96% on Nykthos boards, 82% with the companion action | 361 shown (14,108 flags) | 310+ / 12,000 | The nearest oracle outcome differs only by "float engine - oracle {G}" against an untapped Cavern of Souls / Nykthos. The engine splits one multi-mana tap (a Forest carrying Wild Growth and Utopia Sprawl, or Nykthos's devotion yield) across two payments (the companion {3} and then Kaheera), which the oracle's per-payment source enumeration does not reproduce. GUESS: confirm on s30000000 T5. | oracle: model a tap's surplus carried into the next payment of the line; exact |
| O3 | Hydras | CAPPED-DECISION, 6 decisions in 6 jobs (200,038 flags; g55956 has 130,530) | 6 | 6 / 29,000 | Late-game menus of Gauntlet of Power + Nissa + 2 Vernal Bloom + Hydras with 10+ lands spend `MTG_MANA_ORACLE_DECISION_S` (1300 priced s). | oracle cost (decision-cap work), exact |
| O4 | Creature Giving | ORACLE-GAP on Crop Rotation | 73 | 71 / 63,000 | Likely the tap-then-sacrifice ordering (see cluster 6): the engine's untapped-victim sacrifice and the oracle's tapped-victim twin compare as different resources. GUESS. | oracle, exact |
| O5 | cosmetic | `CompanionToHand` has no name in the oracle's kind table (ManaOracle.cpp ~560-629 returns `"kind?"`), so every Kaheera companion plan prints `kind?:Kaheera` | -- | -- | -- | add `case Action::Kind::CompanionToHand: return "companion";`; exact |
| O6 | small | Sea Creatures ORACLE-GAP 8 / 6 games; Shadowborn 4; Dragonstorm CAPPED 1 + CAPPED-DECISION 1 (s30161857 T4) | -- | -- | -- | leave |

## 3. Fix-agent briefs (engine clusters, in rank order)

Repro shape for every cluster (game index = seed - 30000000; the `gi` printed in a flag line is a job-local index,
not the game index):

```
MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=<T> [MTG_DBG_MULTI=<T>] build/Release/mtg '<deck>' --profile '<profile>' \
  --games 1 --seed <seed> --game-index <seed-30000000>
```

Run repros through `scripts/box_lane.sh --slots 1`. Every play change below needs a held-out A/B on the deck, the
slower-game d8 b0 bar, and smoke + regression GT. Sample flag lines are in `logs/gate1127-triage/samples/`, and
`parse.py` / `clus.py` / `show.py` re-derive any count from the gate's raw chunks.

### 1. Beasts: Urza's Incubator ordered after the creatures it discounts

**Goal.** Make "Incubator first, then the discounted Beast" realisable whenever the ranked order fails.

**Events.** 1,592+ events / 1,112+ games: APPLY-DROP-ORDER 1,205, APPLY-DROP 316, and ~70 ENUM-GAP whose paying order
starts with the Incubator. T3-T5.

**Mechanism.**
- Urza's Incubator's card data is `chooses_creature_type` + `reduces_spell_subtype_amount 2` +
  `reduces_spell_subtype_creature_only`, with no `reduces_spell_subtype`.
- `GenericProvider::CastOrderRank` therefore gives it 20, a noncreature (`src/ai/DecisionProviders.cpp:9447`: only
  `reduces_spell_subtype` gets the reducer tier 8). Creatures get 10, so the canonical order casts the Beast first and
  the Incubator's {3} is then unaffordable. `BeastsProvider::CastOrderRank` (`:9749`) does not override it.
- No payable-order domain covers a choose-a-type reducer without a ritual:
  - `RitualReducerOrderDomain`: TurnSolver.cpp:8861-8880.
  - `BeastsLordReducerOrderDomain`: :8829, Kaheera + Krosan Warchief only.
  - Consumers: :15056, :15310, :92467.
- `BoarsProvider` already ranks this form as its reducer tier (`DecisionProviders.cpp:978`). Dragons has its own rank
  (`:35646`).

**Repros.**
- s30000022 T5 `{Packleader + Incubator}` on 3 Forest + 3 Sprawl: Packleader 5 + Incubator 3 > 6, but Incubator 3 +
  Packleader 3 = 6.
- s30003941 T5 and s30000004 T4 (a second Incubator).

**Fix shape.** Two options:
- (a) Add the choose-a-type creature reducer to an order domain, so the payable-order rescue prices "reducer first"
  when the ranked order fails. This is a completeness fix: an unfindable line becomes findable, and it is not an order
  policy.
- (b) Rank the Incubator at the reducer tier. This is a **cast-order change, and the cast order is USER-owned**:
  Beasts' cast-order draft is pending the USER's review (`docs/design/analysis-Beasts.md:239`, `:382`).
  `analysis-Beasts.md:257` records that an experimental Incubator-first rank did not recover a budget-tail win at
  20 ms and was removed.

Both are play changes. Recommend (a).

**Impact.** M. The lost line is an Incubator plus a Beast in the same main. This deck has ~2 Incubators, so the line
recurs.

### 2. Beasts: Nykthos devotion

**Goal.** Credit Nykthos's `{2},{T}` devotion yield, including devotion the plan's own casts add, in the gates and the
sequential walk, without creating APPLY-DROPs.

**Events.** ENUM-GAP 336 / 319 games (T4-T5 mostly), plus ~245 APPLY-DROP events on Nykthos boards.

**Mechanism** (from code reading, to be confirmed with `MTG_DBG_MULTI`). The flat pool rejects, `exec_feas_rescues`
admits, and the sequential walk fails (`ef-walk-fail`). In detail:
- The pool counts devotion from the pre-plan board only (SpellEffects.h ~37145).
- The rescue's ceiling (TurnSolver.cpp ~92647) has no devotion term.
- The walk's payer never takes the devotion mode while plain lands cover the current cost (ManaPayment.cpp ~13395 /
  ~12961).
- It judges the rest of the line at the current devotion (SpellEffects.h ~37045 returns before the credit at ~37083).

`DevotionOrderDomain` (TurnSolver.cpp:8800-8815) covers only the order, not the yield.

**Repros.** s30008773 T5, s30000008 T3 (three creatures on T3 lost), s30000924 T8.

**Fix shape.** In the shared payer, an over-pay through the fed devotion mode when the greedy payment strands the rest
of the line, plus a devotion term in the ceiling. Play change. Land both halves together: the menu and the walk use the
executor's payer.

**Impact.** M-H: a whole extra creature on T3-T5 when it hits.

### 3. Beasts: companion {3} and land-Aura continuation drops

**Goal.** The `{3}` companion fee (Kaheera, `Action::Kind::CompanionToHand`, TurnSolver.h:324) and the other generic
bills should leave the line's coloured sources.

**Events.** 516 events / 431 games. The dropped Utopia Sprawl / Wild Growth is mostly undeclared, from the continuation
after Kaheera reaches hand (T3 mostly). Another ~150 drops come with a pinned Sprawl colour (`Utopia Sprawl(x5)`).

**Mechanism.**
- s30000025 / s30000007 / s30000017 T3: the engine pays the {3} with Forest(+Aura) green while Cavern of Souls (whose
  colour is creature-only) stays untapped. The continuation's Wild Growth / Sprawl {G} then has no source. The oracle
  pays the {3} with Cavern's {C} first.
- That is exactly the USER's "pay generic with colourless first" hedge (USER ruling 11 in
  `docs/design/mana-issues-carryover-2026-11.md`: a good general hedge, adopted per deck on its own measurement).
- The pinned-Sprawl drops (s30000019, s30000353 T4) are the land-Aura host-float family: the Sprawl's {G} should come
  from the Wild Growth host's surplus.

**Fix shape.** Two parts:
- Enable or extend the colourless-first generic hedge for Beasts. It is a USER-sanctioned hedge, but it is measured per
  deck: play change, A/B.
- A host-surplus rule for the pinned-Sprawl shape, in the prepay-Aura code (`MTG_PREPAY_AURA_*`).

**Impact.** L-M: usually a ramp Aura a turn late.

### 4. Worldfire

**Goal.** Re-gate Worldfire at HEAD first. Rounds 2-4 of the Dragonstorm payer work (Lotus Bloom crack colour, the
declared ritual lines) target the same pieces.

**Events.** 269 events / 261 games: APPLY-DROP 205, APPLY-DROP-ORDER 74. Mostly T4.

**Mechanism.** Three shapes:
- **Continuation float drained by a ritual's generic** (s30023416 T3: Saprazzan Skerry's floated {U} spent on Seething
  Song's {2}). `KeepOwedFloatPay` (ManaPayment.cpp ~14239) fires only inside a bound line. Continuation plans are not
  bound: the line binds at AIEngine.cpp ~8990 and TurnSolver.cpp ~77304 only.
- **Lotus Bloom crack colour** (s30023200 T4): fixed by `33963c528`.
- **Brainstorm-first ORDER** (126 of 175 lines, s30023480 T2): GUESS, the put-back (`WorldfireProvider::PutBackRanking`,
  DecisionProviders.cpp ~40549) or the re-solved continuation drops the declared card. If so, it is an oracle false
  positive.

**Fix shape.** Bind continuation plans to the line's sequence (`Action::rec_line_hold`): a play change that Dragons,
Shadowborn and Dragonstorm's undeclared drops share. The ORDER reclassification would be exact (oracle only).

**Impact.** M on combo turns: 26 Irencrag Feat and ~32 Seething Song events. Cantrip drops are L.

### 5. Stuffy Doll: Chain Reaction before Blasphemous Act

**Goal.** Stop offering, or rescue, a plan whose USER order strands Blasphemous Act.

**Events.** 86 events / 79 games. 305 of 305 lines pay only Act-first. 249 of them are in main 1, admitted by
`MTG_STUFFY_SPLIT_LETHAL_M1` (lethal-projected).

**Mechanism.**
- The USER order puts Chain Reaction in slot 11 and Act in slot 12 (DecisionProviders.cpp:3715-3739).
- The executor reprices Act on the live board after Chain Reaction's deaths (ManaPayment.cpp ~5017).
- The enumerator's `AffinityFeedCount` (TurnSolver.cpp ~9235-9262) credits the creatures that enter but never
  subtracts those an earlier sweeper kills.

**Repros.** s30024013 T4, s30034137 T5.

**Fix shape.** Two options:
- (a) Subtract sweeper deaths when pricing Act. This is a sound correction of the menu's projection, but a play change.
- (b) Fall back to Act before Chain Reaction when the USER order cannot pay. That deviates from the USER's order, so it
  is a **USER decision** (question below).

**Impact.** M: these are lethal-projected main-1 plans.

### 6. Creature Giving: Crop Rotation

**Goal.** Tap the land being sacrificed for mana before sacrificing it, and credit the fetched land's mana.

**Events.** DOMINATED-CREATURE about 4,700 (2,414 shown) in >=2,232 games, T2-T4, always "keeps Birds of Paradise
untapped". ENUM-GAP 413 / 374, always 2+ Crop Rotations. APPLY-DROP 31.

**Mechanism.**
- `PerformSacrificeLandCost` (src/core/SpellEffects.h) removes the victim without tapping it first. The tap-first
  helper `FloatLandBeforeSacrifice` (SpellEffects.h:30845, used at :30909 and in Karoo / Lake of the Dead / Lotus Field
  paths) is not called there.
- The payer then taps Birds for the {G}.
- For the ENUM-GAP:
  - A `tutor_land_to_battlefield` cast gets no mana credit for the land it puts onto the battlefield untapped. The
    land-put credit is for `land_search_on_resolve` only (TurnSolver.cpp ~57994; see ~76456-76459 for the
    breakpoint-supply credit).
  - `sacrifice_count > total_lands` (TurnSolver.cpp ~92786) rejects N Crop Rotations on fewer than N lands, though each
    one replaces its own victim.

**Repros.**
- s30076355 T4 and s30103972 T4 (DOMINATED-CREATURE).
- s30080179 T3 and s30112891 T2 (ENUM-GAP).
- s30075038 T2 (simple: Forbidden Orchard + Birds, Windswept Heath, one Crop Rotation).

**Fix shape.** Call the tap-first helper inside `PerformSacrificeLandCost`; CR 601.2g-h allows mana abilities before
costs are paid. This is a sound dominance (it only adds mana), but play moves. Then add the fetched-land credit and
relax the land count. Land them together, or the newly admitted ENUM-GAP plans become APPLY-DROPs. Play change: GT + A/B
on every `sacrifice_land` deck (Crop Rotation, Harrow, Shard Volley in burn).

**Impact.** L. The kept body is a 0/1 Birds whose attack deals 0. The extra mana matters only if main 2 has a use, and
the ENUM-GAP lines mostly reach equal casts through the continuation. High count, low value.

### 7. Dragons

**Goal.** Fix the generic choice on two-unit lands (Gruul Turf {R}{G}) and Haven of the Spirit Dragon (Dragon-only
colour) against the line's colour demand, and bind continuations (shared with cluster 4).

**Events.** 255 events / 244 games:
- Lightning Bolt 111, Scourge 54, Atsushi 29, Dragon Tempest 27, others.
- Declared depth-2 about 90; undeclared (after Mind Stone's dig) about 165.

**Mechanism.** `KeepOwedFloatPay` returns at once when nothing floats (ManaPayment.cpp ~14242). Continuations get only
the summed line hold (AIEngine.cpp ~546-562).

**Repros.** s30200355 T4 (declared). s30206947 T5 (undeclared, the main-phase kill lost; also s30213313, s30261495,
s30267560).

**Fix shape.** A unit-aware generic choice; continuation binding. Play change.

**Impact.** L-M: usually 3-6 damage or a dragon a turn late. Combat likely still kills in the four lethal events
(GUESS; confirm with d8 b0).

### 8. Hydras: the USER-rule rescue that did not fire

**Goal.** `HydrasProvider::ArrangeCastOrder` (DecisionProviders.cpp ~57280-57308; `f6266c8c8`, USER rule 2026-11-14)
should move the fetch between the Blooms when the ranked order fails. In every sample the paying order is exactly that
move.

**Events.** 95 / 90 games, T5 mostly.

**Mechanism.** Unconfirmed. Either `OrderPaysSequentially` / `SeqWalkFixedOrder` (TurnSolver.cpp ~13406) misjudges one
of the orders, or a later `ApplyPayableCastOrder` / range-ladder step undoes the move.

**Repros.** s30045985 T4, s30044100 T6, s30045511 T5. Use `MTG_USER_ORDER_TRACE=1`.

**Fix shape.** A walk correctness fix under an existing USER rule. Play change.

**Impact.** L-M: a fetch a turn late means 2-3 less mana and a smaller X.

### 9. 1 Drop Burn

**Goal and mechanism.**
- **Vexing Devil after Goblin Guide** (APPLY-DROP-ORDER):
  - The spectacle credit decides "opponent takes 4" on the turn's starting board (TurnSolver.cpp ~65251).
  - The total cast order (DecisionProviders.cpp ~69893) casts Guide (rank 10) before Devil (12).
  - The goldfish then declines Devil's damage, so Skewer the Critics costs full price and drops.
- **Needle Drop before Skewer** (Needle has no target yet). `MTG_PO_DAMAGE_ORDER` (`5696fdcc0`, default OFF) targets
  it, but probably does not run here.
- **Grim Lavamancer "pip stranded"** (55 events):
  - The label is too broad: `NoteGraveyardDamageDrop` (TurnSolver.cpp ~13347) reports mana, fuel and target failures
    alike.
  - The `no-failed-payment` cases are probably a stale two-card fuel pin (~52061).

**Repros.** s30004966 T3 and s30021487 T3 (order); s30062005 T4 and s30043028 T4 (Lavamancer).

**Fix shape.** Play changes. The Devil-before-Guide fallback touches the USER's ruled order ("Devil rank 12"): a
**USER decision**.

**Impact.** M for the Devil case (4 + 3 damage, can move the kill turn); L for Lavamancer (2 damage, opponent life
pairs 13/11, 5/3, 4/2, 2/0).

### 10. Sea Creatures

**Goal.** Order the T2 ramp correctly.
- Sol Ring and the ramp land Auras both rank 5 (DecisionProviders.cpp:9432 and :9441-9443), so plan order breaks the
  tie.
- A Utopia Sprawl colour pin that is not green floats the wrong colour for the next {G} Aura.

**Events.** 150 / 143 games, 89 of them on T2.

**Repros.** s30046885 T2 and s30046804 T2 (order), s30045178 T2 (drop), s30046028 T3 (ENUM-GAP, Trench Gorger).

**Fix shape.** An order domain for a same-host Aura chain and a rock-before-Aura tie, modelled on
`RitualReducerOrderDomain`. Play change.

**Impact.** L: about 1-2 mana on T3.

### 11. Angels: the doomed old Giada held for attack

**Mechanism.**
- The legend-keep pass twins every second-Giada cast (TurnSolver.cpp ~57572-57632).
- In the keep-newest twin, the payer still holds the old Giada's Angel-only {W} "for her attack":
  `RestrictedMayHoldForAttack`, ManaPayment.cpp ~1834-1864, via `RestrictedIneligOwedLater` ~1786; the creature-only
  skip is at ~3050 / ~4561.
- But the old Giada dies to the legend rule.
- Five events (Serra, Sol Ring; s30241585) are a different defect: the hold test compares flat totals, not colours
  (~1863).

**Repros.** s30238235 T4, s30241585 T3.

**Fix shape.** Treat the legend-rule victim as a doomed body: no attack, no hold. `g_legend_keep_pin.v == 2`,
SpellEffects.h ~11236; the `IsDoomedPlanBody` pattern at ManaPayment.cpp ~612 / ~3529. Sound dominance, but play
moves: GT + A/B. The per-colour hold is a heuristic change and needs a real A/B.

**Impact.** L.

### 12. Mill: Visions of Beyond + Surgical Extraction

**Events.** 280 events / 268 games, T2 186.

**What every flag says.** "the menu reaches this board only with fewer resources: U:<Watery Grave | Darkslick
Shores>": a menu entry reaches the same board with one U/B land fewer untapped.

**Ruled out by code reading:**
- Surgical is stripped to {0} (life-paid) at TurnSolver.cpp ~57692, so the mana gates see a {U} bill on boards with 2-3
  blue sources.
- `SubsetPhyrexianDominated` exits for Surgical (TurnSolver.cpp ~25429-25432, the Mill `LifeOnlyPhyrexianCast` guard).

**Likely.** The covering entry is `{land + Visions}` with Surgical cast in Visions' draw continuation, differing only
in which land stays untapped or in a shockland played tapped. If so, it is an oracle-side cover question and **exact**
(oracle only). The extra untapped U/B land buys nothing: Mill's `NeverCast` drops Counterspell / Drown in the Loch /
Fatal Push.

**Repro.** s30056451 T3 / s30064787 T2 with `MTG_DBG_MULTI=<T>`: a `[dbgmulti] ... why=` line names the gate; no
line means the odometer bound cut it. See section 4.

**Impact.** ~0 if the guess holds.

### 13. Dragonstorm (non-Karrthus)

**ENUM-GAP** (24 events). One Apex + closer lines fail the flat pool, because `ApexPairGateCredit` credits Apex's ten
only with two copies in hand (TurnSolver.cpp ~13163-13176, used at ~61070). The rescue walk spends Lotus Bloom's {B}
early (~13438-13453), and the plan is dropped at ~92785. Crediting a lone Apex cost +22% CPU on 2026-11-17 and was
narrowed. Repros s30160292 T4, s30160429 T5. Play change.

**Drops** (38 events: Kolaghan / Scourge / Lathliss). Mostly the shapes rounds 2-4 fixed after the gate. Re-gate at
HEAD first. s30180484 T4 should read 0 at HEAD.

### 14. Treasure Hunt

The three shapes are those `docs/design/treasure-hunt-oracle-residuals-2026-11-27.md` reclassified as oracle false
positives, in `eb39bc311`, after this gate's commit:
- undeclared Land's Edge on a menu that wins (`MTG_ORACLE_DROP_WON`);
- `{TH + TH}` ENUM-GAP refused by the provider's flood gate (`MTG_ORACLE_EG_PROVIDER_PRUNE`);
- the Throes re-plan.

On their 20,000-game gate the levers took APPLY-DROP 326 -> 22, ORDER 62 -> 0 and ENUM-GAP 76 -> 1, with play
byte-identical. Expect the same ~93% reclassification here. Re-gate at HEAD; the residue is the doc's s30103724 /
s30108799 shapes, which cost no turn.

### 15. Shadowborn Apostle

23 events: an undeclared Thrumming Stone tutor paid by a Lake of the Dead crack plus Lotus Bloom (depth3+), and the
Lotus Bloom keep-owed shape. The latter is probably fixed by `33963c528` / `27965f084`. Repros s30150033 T5,
s30148038 T5. Park it unless a d8 b0 repro loses a turn.

## 4. Repros run for this triage

See the appendix below. The small-job lane was full the whole time (two long 2-slot jobs plus three queued jobs ahead).
Any repro not listed there did not run.

## 5. Questions for the USER (surfaced, not blocking)

1. **Stuffy Doll (cluster 5, cast order is USER-owned).** When Chain Reaction first leaves Blasphemous Act unaffordable
   in a lethal-projected main-1 plan, may the apply cast Act before Chain Reaction? Or should the menu's projection just
   subtract the sweeper's deaths, so the plan is not offered? Default: the projection fix, the USER order unchanged.
2. **1 Drop Burn (cluster 9, the USER's order "Devil rank 12").** May the apply move Vexing Devil ahead of Goblin Guide
   only when the ruled order cannot pay the plan? Default: no change; bring the measurement first.
3. **Beasts (cluster 1).** The completeness fix is the order rescue (option a). Ranking Urza's Incubator at the reducer
   tier (option b) would be part of the Beasts cast-order draft the USER has not reviewed yet. Default: (a) only.

## Appendix: the repro job

One 1-slot lane job, `build/scratch/gate1127-triage/repro1.sh` (box_lane PID 2805219), queued 11:21 UTC. It runs these
in priority order and writes `<tag>.{out,err,logs}` plus `progress.txt` and `DONE1` under
`/workspaces/MagicDeckTester/build/scratch/gate1127-triage/`:

| tag | binary | purpose |
|---|---|---|
| mill-surg.gate | gate (`build/wt/land-1128/Release`) | `MTG_DBG_MULTI` names the gate on s30056451 T3 |
| th-edge.post / th-thth.post | post-gate (`build/wt/land-1130/Release`, has `eb39bc311` + DS round 4) | confirm the TH reclassification on s30215234 T6 / s30237234 T5 |
| bst-incub.gate | gate | the game log for the Incubator event, s30003941 T5 |
| bst-enum.gate | gate | `MTG_DBG_MULTI`, the Nykthos ENUM-GAP gate, s30008773 T5 |
| wf-ponder.post | post-gate | whether Worldfire s30026866 T3 still flags at HEAD |
| cg-crop2.gate | gate | `MTG_DBG_MULTI`, the Crop Rotation x2 gate, s30080179 T3 |
| ds-enum.gate | gate | `MTG_DBG_MULTI`, the Apex gate, s30159563 T6 |
| dr-bolt.post / ang-drop.post | post-gate | whether Dragons s30239577 T5 / Angels s30241585 T3 still flag |

Results are below if the job ran before this file was committed. Otherwise read them from the scratch directory.
