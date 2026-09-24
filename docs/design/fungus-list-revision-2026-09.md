# Fungus list revision — candidate B vs the shipped list (opened 2026-09-24)

## ⇥ RESUME HERE (state as of 2026-09-24, post-screen-1)

**SCREEN 1 IS DONE AND REPORTED** — see "SCREEN 1 RESULT" below. Saproling Burst is committed
(`72f72d30`): Release 152/152 doctest, 103/103 scenarios, smoke ALL PASS with `play-changed=0`
on both searched and d0.

**THE NEXT STEPS, in order:**
1. **Brightcap Badger — USER SAID BUILD IT (2026-09-24).** Screen 1 independently agrees: the
   Crossroads axis is the one clean winner, which is the evidence the sequencing call below said
   to wait for. **The user also reframed the card:** *"The adventure is a useful part of the badger
   when you don't have a 3-drop so it has upside… In fact, without that I wouldn't have even
   considered it."* So **Fungus Frolic is load-bearing, not a rider** — the sequencing call below
   costed this card as "the mana grant, 2-3 days" and barely weighed the adventure. Re-cost it.
2. **Screen 2** (design below): base + candidate **B′** + per-card marginals off B′, read with
   `screen_marginals.py --ref final`. Second direction of the user's question. **Revise the B′
   bucket assignment** — screen 1 says B′ should probably not be measured as written (below).
3. `--confirm crossroads4` on disjoint seeds. It is the selection-biased winner of a 9-arm screen.

**Already committed:** `476df8c3` (Hickory Woodlot, Concordant Crossroads, the Doubling Season ×
depletion fix), `c7c75010` (Shroofus Sproutsire), `1ff33a02` (Slimefoot), `9fdbced1` (Vitaspore +
Deathspore), `72f72d30` (Saproling Burst).

**USER SIGN-OFF RECEIVED (2026-09-24) on the provisional deferrals** — Shroofus's inert Trample
and "to a player"; Saproling Burst's "can't be regenerated" and instant-speed activation;
Concordant Crossroads' opponent-half and the unmodelled World rule; Slimefoot's inert lifegain
and the 2-copies + Doubling Season loop. **Conditional on one thing, which holds:** *"as long as
Crossroads still provides haste for the current player."* It does — both halves of CR 302.6,
attacking and `{T}`, tested at `test/unit/test_fungus_revision.cpp:267-278`. The deferred half is
only the opponent's side, inert against a goldfish opponent that never attacks or blocks.

## SCREEN 1 RESULT — the control arm reversed the reading

20,000 paired games per cell, d5/20 ms, one pooled batch at 24/24 workers.
`logs/fungus2/screen1.json` → `screen1.out`. **Levels are apparatus-relative; read the ranking.**

**The headline is about the SHIPPED list, not the new one.** Cutting the four Thallid
Shell-Dwellers for basic Forests is worth **−0.1163** (t = −33.5) on its own — a larger effect
than any new card in either direction. Six arms donated that slot, so against `base` seven of
eight arms "work"; against `cut_shell` almost none do.

Four arms kept some Shell-Dwellers and are therefore confounded by the donor, not by their card.
Corrected below at `0.1163/4` per retained copy — **an assumption of linearity across the four
copies, not a measurement**, hence `est`:

| package | contribution vs. the Forest it displaced | basis |
|---|---|---|
| **Cut 4 Thallid Shell-Dweller** | **−0.1163** | exact |
| Undercellar Myconid ×4 (over Sporesower ×4) | **−0.0575** | exact — never touched the donor |
| **Concordant Crossroads ×4** | **−0.0425** | exact, *on top of* the cut (t = −11.04) |
| Hickory Woodlot ×4 (over Forest ×4) | −0.0261 | exact — never touched the donor |
| Concordant Crossroads ×1 | −0.0174 | est |
| Sol Ring ×1 | −0.0034 | est — noise |
| Shroofus Sproutsire ×4 | **+0.0471** | exact — worse than a Forest |
| Mycoloth 2→4 | **+0.0527** | est — worse than a Forest |
| Slimefoot 1 + Deathspore 2 + 4 Blooming Marsh | **+0.0631** | est — worse than a Forest |

**What this settles.**
* **Thallid Shell-Dweller loses its slot** to a basic land. So does **Sporesower Thallid**, to
  Undercellar Myconid. Two shipped cards cut on exact, unconfounded measurements.
* **Concordant Crossroads ×4 is the one clear addition**, and the ×1 arm shows the axis is
  quantity-dependent: the donor-corrected per-copy value barely moves (−0.0174 for the first vs
  −0.0106 averaged over four), so it is roughly linear and mildly diminishing — the apparent
  cliff between `crossroads1` (+0.0698) and `crossroads4` (−0.0425) is **entirely the donor**,
  not a threshold effect. Do not report it as one.
* **Shroofus ×4 and Mycoloth 2→4 are refuted** as additions to the shipped shell.
* **The black package's verdict is NOT settled and must not be read off this screen.** That arm
  pays for 4 Blooming Marsh and adds exactly one sac outlet, so it measures Slimefoot *without*
  the engine that makes Slimefoot good (Vitaspore/Deathspore in numbers, Mycoloth devour, Utopia
  Mycon). The structural finding — that candidate B wins by non-combat drain, not by attacking —
  predicts the black package only pays inside B's shell. Screen 2 is what tests it.

**Consequence for screen 2's B′ assignment (below): it is now suspect.** B′ as written keeps
Shroofus 1 and Mycoloth 4, both refuted here, and drops to 4 Forest. Reconsider before launching.

## BRIGHTCAP BADGER — BUILT (2026-09-24). The sequencing call's cost model was inverted.

USER: *"yes we should build Brightcap Badger"*, plus the reframing that changed the work:
*"The adventure is a useful part of the badger when you don't have a 3-drop so it has upside…
In fact, without that I wouldn't have even considered it."*

Committed as `95609224` (adventure + grant foundation) and `c216d983` (the grant's second half).

**THE LEDGER HAD THE COST BACKWARDS, and had missed an ability.** Scryfall-verified: the Badger has
**three** abilities, not two — the mana grant, *and* "At the beginning of your end step, create a
1/1 green Saproling creature token", which the sequencing call never mentions. And the split:

| | sequencing call | verified |
|---|---|---|
| the mana grant | "2–3 days", framed as the whole cost | **3–5 days** |
| the adventure (Fungus Frolic) | barely weighed | **~0.5–1 day** |

None of the three things that make the grant expensive — ~20 open-coded mana sites, `ManaCacheKey`,
tokens having no `CardDefinition` — touches the adventure. **The half the user says the card exists
for is the cheap half, and it was never what was blocking this.**

**Three recorded claims did not survive verification:**
* `ManaCacheKey` is **not** a new stale-hit hole. It already walks the same source predicate, so
  fixing the predicate fixes the key — no new hash term. It must move in the SAME commit, though.
* **Badger × Crossroads needed no new code.** `CanTapNow` already consults the shared haste oracle.
* **Reason 3 of the sequencing call ("the risk is not contained to this deck") is FALSE.**
  `GameState::deck_has_mana_grant`, stamped from the decklist, makes every deck that plays no grant
  source byte-identical *by construction*. Smoke confirms: `play-changed=0`. Reasons 1, 2 and 4 stand.

**Two costs nobody had budgeted, both now handled:** the backtracker's failure memo switches off
above 64 sources (the guard against a documented 14-hour blow-up), so the candidate build admits at
most `cost.ManaValue()` granted **Saprolings** — lossless, because a payment taps at most that many
and the tokens are interchangeable; granted **Fungi** are not capped, being real cards with their
own abilities. And `CanTapNow` goes O(board²) under Crossroads, hoisted into the grant's one board
walk via `blanket_haste`.

### The three defects the TESTS caught, all the same mistake

**One rule with two readers that must agree.** None was found by review.

1. **The grant reached only HALF its population.** The design is built on "tokens have no
   `CardDefinition`" — the fact the card hinges on — and that turned into treating *having* a
   definition as "already handled". It is not: **Thallid, Sporecrown Thallid, Mycoloth, Sporesower
   Thallid and Utopia Mycon are all Fungi with real definitions and no mana ability at all**, and
   the resolver skipped every one. Utopia Mycon is the sharpest: its mana ability costs a
   *sacrifice*, not `{T}`, so the granted tap does not even compete with it.
2. **A segfault in the payer.** The selection loop learned the grant; the execution half re-derived
   the def with a bare `LookupCached` and dereferenced null on the first payment off a token.
3. **The §2b fodder branch needed the REAL definition.** The payer now carries two pointers — what
   the permanent *is*, and what it is *for mana*. The synthetic face is not a creature, so feeding
   it there would have silently stopped a tapped Fungus being legal fodder for Utopia Mycon.

### An unrelated bug this turned up — NOT fixed, and LOWER priority than first stated

**Correction to this ledger's first draft, and to what was reported to the user:** I called this
a *live* bug. It is not. `SacOutletPayEnabled()` reads `EnvOn("MTG_SAC_OUTLET_PAY")` with no
default argument (`EnvFlags.h:24` -> `dflt = false`), so **§2b is default OFF and nothing shipping
runs it.** The defect is real but its blast radius is "a default-off lever would do nothing if
anyone enabled it for this deck", which is consistent with the existing record that this lever is
INCOMPLETE and not a ship path. It does not compete for attention with the screens.

`MTG_SAC_OUTLET_PAY` (§2b) is **dead on Fungus when enabled**, for exactly the reason the grant would
have been. Utopia Mycon's *"Sacrifice a Saproling: Add one mana of any color"* cannot see a single
legal body, because three separate sites bail on `def_absent` / `!def` before the fodder test runs
(`LiveSacPayOutlet` `SpellEffects.h:18996`, `SacPayFodderCount` `:19090`, the payer's own branch
`ManaPayment.cpp:660`) — and every Saproling is a token. **Code-read, not yet measured**; one
`MTG_SAC_TRACE=1` run on a Fungus game would confirm it in minutes. Fixing it here would make a perf
or quality movement in either change impossible to bisect, so it is its own item.

Also found and **not** fixed: unguarded `1ull << i` shifts on boards wider than 64
(`SpellEffects.cpp:2043`, `:2303`, `:3219`, `SpellEffects.h:23253`, plus six mask *builds*). On x86
the shift count is taken mod 64, so a reservation meant for index 70 silently reserves index 6. Live
on Fungus today at 364 permanents whenever `reserved_mask != 0`. Its own ~30-minute item.

**Left as measured-heuristic follow-ups, classified rather than forgotten:** the prepay reservation
masks (AttackerReserve / DorkReserve) and the filter-feeder chain. Those only decide *which* source
to spare, so a stale answer costs ranking, not correctness — whether a granted Saproling should be
held back as an attacker is a provider question for `heuristic-optimization.md`, not part of the grant.

## SCREEN 1b RESULT — the estimates become measurements, and the winners STACK

`logs/fungus2/screen1b.json`, 20,000 paired games × 10 cells, **disjoint seeds** (940000+, against
screen 1's 910000–929999), same apparatus. Every arm has Thallid Shell-Dweller 0, so the control
`sd0` IS the corrected deck and there is no donor left to confound anything.

| arm | vs `sd0` | se | t |
|---|---|---|---|
| **stack3** (Crossroads 4 + Undercellar 4 + Woodlot 4) | **−0.1265** | 0.0044 | −28.92 |
| Undercellar Myconid ×4 | −0.0481 | 0.0019 | −25.06 |
| Concordant Crossroads ×4 | −0.0474 | 0.0038 | −12.48 |
| Concordant Crossroads ×2 | −0.0427 | 0.0027 | −15.77 |
| Concordant Crossroads ×1 | −0.0263 | 0.0020 | −13.43 |
| Hickory Woodlot ×4 | −0.0254 | 0.0020 | −12.79 |
| Sol Ring ×1 | −0.0067 | 0.0017 | −3.96 |
| Mycoloth 2→4 | **+0.0398** | 0.0025 | +15.79 |
| black package | **+0.0580** | 0.0031 | +18.95 |
| base (4 Shell-Dwellers) | +0.1106 | 0.0034 | +32.39 |

**1. Crossroads is CONFIRMED on held-out seeds** — −0.0474 here vs −0.0425 in screen 1. The
selection bias of a 9-arm screen did not inflate it. That closes the `--confirm` obligation.

**2. THE COUNT AXIS IS SHARPLY DIMINISHING, and screen 1 could not see it.** Marginal value per
copy: 1st **−0.0263**, 2nd **−0.0164**, 3rd and 4th **−0.0023 each**. The first copy is worth ~11×
the fourth, and **two copies capture 90% of what four buy**. Candidate B's 4-of is two dead slots.
Screen 1's apparent cliff between `crossroads1` and `crossroads4` was *entirely* the donor, and is
now replaced by the real shape, which is the opposite: strongly concave, not threshold.

**3. THE THREE WINNERS STACK, essentially additively.** Sum of the parts −0.1208; `stack3` measured
−0.1265; interaction **+0.0057** (very slightly super-additive). This is the result that matters
for adoption, because this repo has a measured case of a lever going +0.0201 alone → −0.0616 in
combination — so "each beats a Forest" does NOT imply "all three beat three Forests". Here it does.
They can be taken as a set without re-measuring each other.

**4. EVERY DONOR-CORRECTED ESTIMATE WAS WRONG IN THE SAME DIRECTION.** The linear
`0.1163/4`-per-copy correction was systematically too conservative:

| package | screen 1 `est` | screen 1b measured | error |
|---|---|---|---|
| Concordant Crossroads ×1 | −0.0174 | −0.0263 | −0.0089 |
| Sol Ring ×1 | −0.0034 | −0.0067 | −0.0033 |
| Mycoloth 2→4 | +0.0527 | +0.0398 | −0.0130 |
| black package | +0.0631 | +0.0580 | −0.0052 |

All four moved the same way, by up to 50% of the estimate. **The lesson is not "the correction was
bad"** — it was directionally right every time and it correctly identified which arms were worth
re-measuring. The lesson is that a linear correction across four copies of a card with a real
curve cannot be reported as a measurement, which is why those rows carried `est`.

**5. The black package is refuted IN THIS SHELL and that is still not a verdict on candidate B.**
Both screens graft Slimefoot onto the shipped green shell, which holds exactly one sac outlet. The
structural claim is that B wins by non-combat drain, which needs Deathspore and Vitaspore in
numbers plus Mycoloth devour. Screen 2 is the only thing that tests the plan.

### Screen 2 bucket assignment (worked out, not yet written)

Candidate **B′** = candidate B with the Brightcap Badger slot returned to a 3rd Tukatongue Thallid,
still exactly 60. Aliases that keep every bucket total within base's cap — note these DIFFER from
screen 1's, so **the two screens' levels are not comparable** (their deltas are each self-contained):

| bucket (base count) | B′ contents |
|---|---|
| Forest (19) | Forest 4 + Hickory Woodlot 4 + Secluded Courtyard 4 + Peat Bog 4 + Blooming Marsh 4 = 20 — safe, the bucket is already cap-saturated at `min(19,7)=7` |
| Thallid (4) | **Saproling Burst 4** |
| Thallid Shell-Dweller (4) | Slimefoot 1 + Deathspore 2 + Concordant Crossroads 1 |
| Sporesower Thallid (4) | Undercellar Myconid 4 |
| Beastmaster Ascension (4) | Beastmaster 2 + Vitaspore 2 |
| Essence Warden (1) | Shroofus Sproutsire 1 |
| Simic Growth Chamber (3) | Sol Ring 1 |
| Mycoloth (2) | Mycoloth 4 — the one raise, ~0.4% fall-through |

The constraint that forced this shape: **no single bucket had 4 free slots for Saproling Burst**
until Vitaspore was moved off Thallid's bucket and onto Beastmaster's.


Per-deck ledger for the in-flight work, per `.claude/skills/analyze-deck.md` ("the per-deck ledger
— durable state across compaction AND handoffs"). Context is disposable; this file is the memory.

## The ask

> *"Let's focus on updates to the fungus list. Here is a hastily assembled potential list. As usual
> I want to go through some serious work to determine which cards in this list and in the original
> Fungus list should win out."* — USER, 2026-09-24
>
> *"We may need to analyze the new list as one of the first steps, given that it adds black."*
> *"Noting that we should share the provider with the existing list if possible."*
> *"Note that this new list wasn't seriously tested. The goal here will be to do that work after
> the analysis etc."*

So: the deliverable is **per-card marginal value**, in both directions, not a single list-vs-list
verdict. Candidate B is explicitly a rough draft — it is the hypothesis, not the answer.

## The two lists

Candidate B is parked at `logs/fungus2/FungusB.txt` (gitignored). Per CLAUDE.md
(*"NEVER COMMIT SCREENING / CANDIDATE LISTS"*) it does **not** get a `decks/` folder unless adopted.

| slot | shipped `decks/Fungus/Fungus.cod` | candidate B |
|---|---|---|
| Thallid | 4 | — |
| Thallid Shell-Dweller | 4 | — |
| Sporesower Thallid | 4 | — |
| Essence Warden | 1 | — |
| Utopia Mycon | 4 | 4 |
| Sporecrown Thallid | 4 | 4 |
| Tukatongue Thallid | 4 | 2 |
| Psychotrope Thallid | 1 | 1 |
| Mycoloth | 2 | **4** |
| Doubling Season | 4 | 4 |
| Beastmaster Ascension | 4 | **2** |
| Wild Growth | 2 | 2 |
| Undercellar Myconid | — | **4** |
| Vitaspore Thallid | — | **2** |
| Deathspore Thallid | — | **2** |
| Slimefoot, the Stowaway | — | **1** |
| Shroofus Sproutsire | — | **1** |
| Brightcap Badger // Fungus Frolic | — | **1** |
| Saproling Burst | — | **4** |
| Concordant Crossroads | — | **1** |
| Sol Ring | — | **1** |
| **lands** | 19 Forest + 3 Simic Growth Chamber = **22** | 4 Forest + 4 Hickory Woodlot + 4 Secluded Courtyard + 4 Peat Bog + 4 Blooming Marsh = **20** |

Both are exactly 60.

## Stage 1 — coverage (run 2026-09-24)

`python3 scripts/analyze_deck.py logs/fungus2/FungusB.txt --coverage-only`

**Missing at open (8):** Brightcap Badger // Fungus Frolic, Shroofus Sproutsire, Vitaspore Thallid,
Slimefoot the Stowaway, Deathspore Thallid, Saproling Burst, Concordant Crossroads, Hickory Woodlot.

Already implemented and needing nothing: Sol Ring, Undercellar Myconid, Secluded Courtyard,
Peat Bog, Blooming Marsh, and every carried-over Fungus card.

### Card status

| card | tier | status |
|---|---|---|
| Hickory Woodlot | 1 | **DONE** — the green Peat Bog, byte-identical params with the colour letter changed. No C++. |
| Concordant Crossroads | 1 | **DONE** — `grants_haste` + `affects_all_creatures`, the Maelstrom Wanderer shape. **Zero C++**. |
| Shroofus Sproutsire | 2 | **DONE** — `FireCombatDamageTokens`, the first combat-damage token trigger |
| Slimefoot, the Stowaway | 3 | **DONE** — new `PermAbilityMode::PayToken` + 2 dies-watcher params |
| Vitaspore Thallid | 2 | **DONE** — `sac_outlet_grants_haste`, shared targeted-outlet resolution |
| Deathspore Thallid | 2 | **DONE** — `sac_outlet_minus_power/-tough` + `ShrinkCreatureUntilEot` |
| Saproling Burst | 3 | **DONE** — the engine's first fading card |
| **Brightcap Badger // Fungus Frolic** | 3 | **NOT STARTED — deferred to last, see the sequencing call below** |

### Saproling Burst — what shipped, and the three things it would have got silently wrong

* **The off-by-one.** Fading counts DOWN and the sacrifice is on **failure to remove** (CR 702.32),
  so Fading 7 takes seven decrements and dies on the **eighth** upkeep. Reading it as "sacrifice
  when removing one reaches zero" costs the card a whole turn of life. Pinned by a test that walks
  all eight upkeeps.
* **The token P/T tie** is a CDA (CR 604.3) pointing at *another permanent's* counter total, so all
  of a Burst's tokens shrink together and an activation at seven leaves six and mints a **6/6**.
  Modelled as a **refresh at the two sites the count can change** (the activation and the fading
  upkeep) rather than a read-time computation — equivalent, because those are the only two
  mutations, and it avoids threading a battlefield lookup into `EffectivePower` (the hottest
  function in the engine) on the deck with 40-token boards. The token keeps a printed 0/0 so
  `CreateTokenOnce` names it once and the name never goes stale, and it stays **definition-less**,
  preserving the `def_absent` short-circuit this deck depends on. That also dissolves the perf risk
  the research draft raised about giving the token a `cards.json` definition.
* **The LTB clause is the card's real cost.** Without it the card reads as eight free Saprolings.
  In a goldfish the fading sacrifice is the ONLY way the Burst leaves, so every token it made dies
  with it — and each death routes through `OnCreatureDies`, so **a Slimefoot on board drains for
  all seven**: the Burst expiring is a burst of DAMAGE, not a board wipe. It is largely redundant
  with the 0/0 SBA *except* under a Sporecrown Thallid or a live Ascension, which lift the tokens
  off zero and make this clause the thing that kills them — which is the case the test covers.

Two consequences worth carrying into the screen: **Doubling Season is worth 4x on this card**
(fourteen counters AND two tokens per activation), and **K is a genuine interior optimum** — K
activations off C counters leave K bodies of `(C-K)/(C-K)`, maximised near `C/2`, so "pop
everything" mints a pile of 0/0s that die on the spot. That is why K is searched and why the two
Bursts are **not** pooled the way the five spore outlets are (different counter totals mint
different-sized tokens, so they are genuinely distinct sources).

A maintenance guard earned its keep mid-build: `Dominance.h`'s `sizeof(Permanent)` static assert
fired the moment the two new fields landed. `fade_counters` is folded as a directional axis
(`MoreDominates`); `created_by_number` deliberately is **not** an axis — it is identity, not a
resource, and a board is not better for its tokens having come from a particular Burst.

### Drafted implementation specs

**Slimefoot, the Stowaway** — `{1}{B}{G}` Legendary Creature — Fungus 2/3.
* Drain half is **Tier 2 on existing machinery**: `dies_watch_subtype: "Saproling"` + `dies_trigger_damage: 1`, fired from `OnCreatureDies` (`SpellEffects.h:6266-6274`), applied at `:6348-6352` which already hits the opponent's face and sets `opponent_lost_life_this_turn`. Two new params: `dies_trigger_damage_each_opponent` (multiply by `gamesetup::OpponentHeads()` — *not* inert, heads can be 2) and `dies_trigger_self_gain` (through the shared `GainLife` hook, and **not** head-multiplied).
* **THE HIGH-RISK ITEM CAME BACK CLEAN.** The worry was that the death-watcher reads subtypes via a `CardDefinition`, which is null for tokens — that would kill the card on ~95% of its triggers. **Verified it does not**: `CreateTokenOnce` (`SpellEffects.h:4325`) writes `m_subtypes` onto the token's `Card`, and the filter is `CardHasSubtype(dead_card, ...)` (`:4556`), a plain string scan over `Card::m_subtypes` with **no `LookupCached` on the dead creature's path**. The `dead_card` is a copy of the battlefield `Permanent`'s `Card` (`GameEngine.cpp:911`), so the empty-mask `ZoneCard` hazard does not apply. Corroborated by Sporecrown's lord and both `sac_creature_requires_subtype` outlets already reading token subtypes every turn.
* `{4}: Create a Saproling` is **Tier 3**: no existing param fits. `tap_token_cost` (Sliver Hive) includes `{T}`, which would wrongly make it once-per-untap and illegal on a summoning-sick body. Needs a new `PermAbilityMode::PayToken` — no `{T}`, no sacrifice, repeatable, K bounded by mana (the `drain_cost` / Essence Depleter shape). Must be added to **both** ModeSpec tables (`TurnSolver.cpp:18260` emission *and* `:3882` `BpAvailablePermAbilityModes`) or the breakpoint machinery cannot see it.
* Provider routing verified safe: Slimefoot's `dies_watch_subtype` trips the `goblin` signature at `DecisionProviders.cpp:10076`, but `if (fungus)` returns first at `:10231` and `fungus` is set by six params this list already carries.

**Saproling Burst** — `{4}{G}` Enchantment, Fading 7. First fading card in the repo.
* New `Permanent::fade_counters` (the `spore_counters`/`quest_counters` pattern, not a `Counter{}` entry — this is the 40-token deck and `EffectivePower` iterates the counters vector).
* **Fading counts DOWN and the sacrifice is on failure to remove** (CR 702.32): Fading 7 takes seven decrements and is sacrificed on the **eighth** upkeep.
* **Doubling Season doubles the fade counters** (CR 121.6/614.1c, the double-loyalty ruling) — the Burst enters with **14** under one Season. Must route through a `PutFadeCounters` chokepoint or the card is badly under-rated. The token half doubles independently, so one Season is **4x throughput**.
* Token P/T is a **CDA referencing the source's counters**, so the token needs its own `cards.json` definition (`0/0 Saproling Token`, the Eldrazi Spawn / Treasure Token precedent) and `DynamicBasePower`/`DynamicBaseToughness` must take the `Permanent` (defaulted arg keeps all 8 call sites compiling).
* **The LTB clause is the card's real cost and must be modelled.** Nothing else in a goldfish removes the Burst, so every token it ever made dies on the eighth upkeep. Without it the card is badly over-rated. Needs `Permanent::created_by_number` + a defaulted `created_by` arg on `CreateToken`.
* K activations is a real searched axis with a genuine optimum: K activations leave `C-K` counters, so the turn yields K bodies of `(C-K)/(C-K)` — total power `K*(C-K)`, maximised near `K = C/2`. Popping everything mints a pile of 0/0s that die on the spot. **Must NOT be pooled** the way `FoldSporeSourceIdentity` pools the five spore outlets: two Bursts at different counts mint different-sized tokens.
* Perf risk to measure, not guess: giving the token a definition flips `def_absent` to false, so Burst tokens re-enter the full `LookupCached` path on hot board walks — partially undoing a deliberate optimisation added because a 150-game Fungus run walks 2.9 billion permanents.

**Shroofus Sproutsire** — `{2}{G}` Legendary Creature — **Saproling** 1/1 Trample. **Tier 2**, ~2 h.
* `FireCombatDamageTokens` modelled line-for-line on the existing `FireUtvaraAttackTokens`
  (`SpellEffects.h:9106-9152`), called once from `ResolveCombatDamage` (`Combat.cpp`) — the **one
  shared combat core** the executor (`GameEngine.cpp:667`) and the rollout (`TurnSolver.cpp:30343`)
  both call, so lockstep is by construction.
* The damage loop already computes what is needed but throws it away: `damaging_idx`
  (`Combat.cpp:185`) records *which* attackers connected, not for *how much*. Needs a parallel
  `damaging_pw` captured where `power` is live and already post-lord / post-anthem.
* **The count must be the post-anthem `power`, not printed P/T.** With Beastmaster Ascension online
  and a Sporecrown out, a 1/1 Saproling connects for 7 and makes **seven** tokens; reading
  `EffectivePower()` instead would silently under-produce by 5-6x on exactly the turns the card
  matters.
* Watch the attacker's `card.m_subtypes`, never its `CardDefinition` — same token-nullity trap as
  Slimefoot, and here it would zero out the entire watched population.
* Shroofus **is a Saproling**: it triggers on its own damage (no self-exclusion, the Utvara shape),
  Sporecrown pumps it, Sporesower does *not* spore it, and it is **legal fodder for its own deck's
  sac outlets** — the search may eat its own engine, which is rules-correct and a real trade-off.

#### Correction to a working assumption I had recorded

I had it that Shroofus's tokens would be unlocked by Concordant Crossroads. **They are not.** The
tokens are created in the combat-damage step — *after* the turn's only combat step — so haste buys
them nothing: with or without Crossroads they first attack next turn, when they could have attacked
anyway. Crossroads is live for Shroofus's own body the turn it lands, and for tokens made in the
**pre-combat** main (spore pops) which then connect and feed Shroofus. **Credit the pair for
accelerating the board that FEEDS Shroofus, not for accelerating Shroofus's own output.**

#### …and a search gate that would under-rate Shroofus if left alone

`FungusProvider::SecondMainNeedsDeferredCast` (`DecisionProviders.cpp:20422`, `MTG_FUNGUS_M2_GATE`,
**adopted, default ON**) makes the search **skip the post-combat solve entirely** on any turn whose
hand holds no payable Main2 cast — i.e. no castable Mycoloth. It is asked first, at
`TurnSolver.cpp:4413`, before the productivity rule.

That was measured on a deck where combat creates nothing, and it was priced as losing "a leftover
Psychotrope draw or Utopia Mycon mana". **Shroofus falsifies the premise**: its tokens land in the
combat-damage step and are on the battlefield for the post-combat main, where three of this deck's
outlets consume them (Utopia Mycon turns them into mana that provably did not exist pre-combat,
Psychotrope into cards, Mycoloth devours them). With 2 Mycoloth in 60, the phase is skipped on most
turns — so most of Shroofus's value would be **invisible to the search**, and the screen would
under-rate it for a reason that has nothing to do with the card.

**The fix is half-built and cheap**: `SecondMainUnproductive` (`TurnSolver.cpp:4458-4462`) already
reads `state.battlefield_at_combat` and calls a turn productive when the board *grew* across
combat — exactly the Shroofus signal. Let the deferred-cast gate stand down on that condition.
Byte-identical on every deck that makes no combat tokens. **Both halves must ship on ONE heurarm
lever** (the `FUNGUS_M2_DEVOUR` precedent): a gate change reaching only one half would skip a phase
a filter had already deferred a cast into, *deleting* the cast.

This is a Stage-5 measured A/B, not a Stage-2 card gap — but it must land **before** Shroofus is
screened, or the screen measures the gate rather than the card.

## SEQUENCING CALL — Brightcap Badger goes LAST, and may not make this round

**The finding.** *"Each Fungus and Saproling you control has '{T}: Add {G}'"* is a layer-6 ability grant, and the engine's "is this a mana source" test is a **template test on the `CardDefinition`, repeated ~60 times across 9 files** (`CardTemplate::ManaDork` appears 109 times). Worse, **every Saproling is a token**, `LookupCached` returns null for it, and essentially every one of those loops opens with `if (!d) continue;`. So a param-only implementation is **100% dead on the entire population it is meant to affect** — the exact "half-wired version the projection never sees" failure. Doing it properly means restructuring ~20 load-bearing sites (`TapForCostSharedOnce`, `TapForCostBacktrackWorker`, `AvailableManaPool`, `BuildColorFeasibility`, `UntappedManaUpperBound` and the other prune bounds, `HumanPreTapFaces`, `KeepModel`'s features) **plus** `ManaCacheKey`, which is a documented stale-hit hole — on the heaviest deck in the regression suite, which reaches 364 permanents in rollouts.

Estimated at **2-3 days for the grant alone**, before the artifact regeneration it forces.

**The call: implement everything else first and screen without the Badger; build the Badger afterwards as its own project.** Reasons, in order:

1. It is a **1-of**. Seen by turn 4 in ~13% of games — the same weak-measurement problem as Sol Ring. Even a large per-game effect is a small effect on the average, so the *measurement* this build would feed is the least informative in the set.
2. Building it blocks the eight cards that carry the actual signal (Slimefoot, Saproling Burst, the manabase, Mycoloth 2→4, Undercellar Myconid, Concordant Crossroads).
3. The risk is not contained to this deck: it touches the mana system's hottest paths and a cache whose stale hits are a known hazard.
4. The partial-implementation option is **rejected outright** — a Badger without its grant is a 4-mana 3/4 that makes one token per end step, which is not the card, and shipping it would be the asymmetric-modelling failure this whole ledger is guarding against.

**What that means concretely:** no arm in the screen contains a Brightcap Badger. Candidate B is measured as **B′** — B with that slot returned to a third Tukatongue Thallid, still exactly 60 — and the Badger is screened later, on its own, once built. **This is a sequencing decision, not a deferral of a clause; the card is not being half-shipped.**

*(Related signal worth watching in the meantime: Concordant Crossroads also unlocks `{T}` abilities on fresh tokens. If the screen says that axis matters, that is the evidence the Badger build is worth its cost.)*
| Saproling Burst | TBD | research fan-out out — **fading is unsupported (0 hits in `src/`)** |

### Scryfall corrections already caught (2a — never trust recall)

* **Slimefoot, the Stowaway** is `{1}{B}{G}`, *not* `{2}{B}{G}`.
* **Saproling Burst** is `{4}{G}`, *not* `{3}{G}{G}`.
* **Brightcap Badger** is an **adventure**: creature face `{3}{G}` 3/4 Badger Druid, adventure face
  **Fungus Frolic** `{2}{G}` Instant "Create two 1/1 green Saproling creature tokens."
* **Deathspore Thallid** is `{1}{B}` and a **Zombie Fungus** (two subtypes).
* **Shroofus Sproutsire** is a **Saproling**, not a Fungus — so Sporecrown Thallid pumps it, but
  Secluded Courtyard naming "Fungus" could not cast it on the real card.

## Provider — SHARED with the shipped list (USER, 2026-09-24)

Candidate B rides **`FungusProvider`**, not a new class. It is a revision of the same deck, not a
new one, and the screen pins the provider anyway (`MTG_PROVIDER_DECK=<base decklist>` on every
batch and every apparatus subprocess — `deck-screening.md`, "The provider is INHERITED, never
re-detected").

### The winless certificate is SAFE BY CONSTRUCTION, and also DEAD on candidate B

`FungusProvider::ProvenWinlessThisTurn` (`src/ai/DecisionProviders.cpp:20436`) bounds **combat
damage only**, and its `attackers` term is commented *"creatures that can attack RIGHT NOW (nothing
gains haste)"*. Candidate B breaks **both** premises:

* **Slimefoot, the Stowaway** is a NON-COMBAT route to the opponent's life (1 damage per Saproling
  death, and this deck sacrifices Saprolings all game).
* **Concordant Crossroads** grants haste to everything, so "can attack right now" is no longer the
  attacker set — exactly the hole `AngelsProvider`'s certificate note already names for Lightning
  Greaves (`src/ai/DecisionProviders.h:285`).

**This is not a soundness bug, because `FungusCertKnownDef` (`:19962`) is a NAME WHITELIST** — any
card not in `kPool` makes the certificate return `false` (= "not proven winless"), which is the
admissible direction. A candidate-B board therefore never gets certified.

The consequence is a **cost** one, and it must be stated in any timing comparison: the certificate
is worth **1.9x on label generation** (`docs/design/fungus-token-search-cost.md`), and candidate B
forfeits all of it until the bound is extended to price Slimefoot's drain and a hasted attacker set.
**Do not read a candidate-B-vs-base wall-clock number as an engine fact.**

## Stage 2 — what is mine vs what is measured

Mine (per `deck-screening.md`, "What needs you, and what does not"):

1. Proposing the combinations.
2. Implementing the 7 missing cards faithfully + reviewing them.
3. **Noticing where the engine models one side better than the other** — see below.
4. Reporting; adoption is the user's call.

### Asymmetric-modelling watch list (item 3 — the judgement no measurement can make)

* **Secluded Courtyard** is bracket-noted in `cards.json` as *"the ETB type choice is simplified to
  'any creature'"*. On the real card, naming Fungus cannot cast **Shroofus Sproutsire** (a
  Saproling) or **Brightcap Badger** (a Badger Druid). The engine's simplification **flatters
  candidate B's manabase** on 2 of its 26 creatures. Disclose in the report.
* **Simic Growth Chamber** (base only) is bracket-noted as *"Fungus runs no blue CARDS, so the {U}
  can never pay a coloured pip"* — correct for the shipped list and still correct for B (B runs no
  blue either), so it is symmetric.
* Any card that ships with a `[PARTIAL: ...]` note is on one side only. Enumerate the full set
  before quoting a headline.

### THE STRUCTURAL FINDING: candidate B is not a faster Fungus deck, it is a DIFFERENT WIN CONDITION

The shipped Fungus deck wins **only through combat** — 1/1 bodies, a Sporecrown lord, and
Beastmaster Ascension's +5/+5 on a wide attack. Every token has to survive a turn of summoning
sickness before it does anything.

Candidate B adds **Slimefoot, the Stowaway**: *"Whenever a Saproling you control dies, Slimefoot
deals 1 damage to each opponent and you gain 1 life."* This deck's outlets sacrifice Saprolings for
free and at will:

| outlet | cost | Saproling deaths per activation |
|---|---|---|
| Utopia Mycon — "Sacrifice a Saproling: Add one mana of any color" | free, no `{T}` | 1 (**and it pays for itself**) |
| Psychotrope Thallid — "{1}, Sacrifice a Saproling: Draw a card" | `{1}` | 1 |
| **Deathspore Thallid** — "Sacrifice a Saproling: Target creature gets -1/-1" | **free, no `{T}`** | **2** — sacrifice one, then shrink a *second* 1/1 Saproling to 0/0 and it dies to SBA |
| Mycoloth devour | as-enters | k |

So with Slimefoot on the battlefield **every Saproling is one damage to the face, on demand,
ignoring summoning sickness and ignoring combat entirely.** Deathspore Thallid's "-1/-1" clause —
which looks inert against a passive goldfish opponent that never blocks — is *not* inert: it is a
**free two-for-one sacrifice outlet** pointed at our own board.

And the engine already scales that arbitrarily: Mycoloth devouring 4 under one Doubling Season
enters with 16 `+1/+1` counters and makes **32 Saprolings at the next upkeep** (the Season doubles
the counters placed *and* the tokens created — see Mycoloth's `cards.json` note). Thirty-two
Saprolings is thirty-two damage with no attack step.

**Consequences for this work:**

1. **Slimefoot is the single highest-value card to measure**, and it must be measured *with* its
   enablers, not alone — a lone Slimefoot in the shipped list with no free sac outlet beyond Utopia
   Mycon is a different card from Slimefoot alongside 2 Deathspore Thallid.
2. **Deathspore Thallid must NOT be implemented as "the -1/-1 is goldfish-inert".** That was my
   first reading and it is wrong. It is a sac outlet, and bracket-noting it away would delete the
   combo the card is in the list for.
3. It is why the winless certificate had to be checked (above) — a non-combat damage route breaks
   a bound that only counts attackers. It declines safely; it does not mislead.
4. Expect candidate B's win-turn **distribution** to differ in shape, not just in mean. Use
   `screen_marginals.py --dist` rather than reading averages alone.

### The manabase is the biggest single axis, and it is a-priori the weakest part of B

Counted by hand, candidate B's green sources that can pay a `{G}` pip: 4 Forest + 4 Hickory Woodlot
+ 4 Blooming Marsh = **12 of 20 lands**. Secluded Courtyard's coloured mana is creature-only
(`colored_creature_only`), so it pays **no** `{G}` pip on the list's 13 noncreature spells
(4 Doubling Season, 4 Saproling Burst, 2 Beastmaster Ascension, 2 Wild Growth, 1 Concordant
Crossroads); Peat Bog makes only `{B}`. The shipped list has **22 of 22**.

Against that, B's black requirement is **3 cards / 3 pips total** (1 Slimefoot, 2 Deathspore
Thallid), and 8 of its 20 lands (Peat Bog, Hickory Woodlot) enter **tapped** and **sacrifice
themselves after two taps**.

That is a real ramp plan, not obviously a bad one — but it is a *separate hypothesis* from the card
changes, and the screen must be able to separate them. **Hypothesis to test, not a conclusion.**

### …and it is inseparable from the curve, which B moves hard

| mana value | shipped | candidate B |
|---|---|---|
| 1 | 15 (Thallid 4, Utopia Mycon 4, Tukatongue 4, Wild Growth 2, Essence Warden 1) | 10 (Utopia Mycon 4, Tukatongue 2, Wild Growth 2, Concordant Crossroads 1, Sol Ring 1) |
| 2 | 8 (Shell-Dweller 4, Sporecrown 4) | 8 (Sporecrown 4, Vitaspore 2, Deathspore 2) |
| 3 | 5 (Psychotrope 1, Beastmaster 4) | 9 (Undercellar Myconid 4, Beastmaster 2, Psychotrope 1, Shroofus 1, Slimefoot 1) |
| 4 | 4 (Sporesower 4) | 1 (Brightcap Badger — or `{2}{G}` as the Fungus Frolic adventure) |
| **5** | **6** (Mycoloth 2, Doubling Season 4) | **12** (Mycoloth 4, Doubling Season 4, Saproling Burst 4) |
| lands | 22 | 20 |

**Twelve five-drops on twenty lands**, eight of which enter tapped and sacrifice themselves after
two taps. The depletion lands are evidently *meant* to be the acceleration (Hickory Woodlot on T1 →
three mana on T2), so the two halves are one design, not two independent choices — a screen arm
that swaps the manabase while leaving twelve five-drops in place is testing a deck nobody built.
**Pair the manabase arm with a curve arm**, or the mana result will be an artefact.

By contrast the shipped list's 15 one-drops on 22 lands is a deliberately low curve that gets spore
counters ticking on turn 1. These are two different decks wearing the same tribe.

### Sol Ring, 1-of

Worth stating plainly because the measurement will be weak on it either way: a single copy in 60
cards is seen by turn 3 about 12% of the time. Even a large effect on the games that draw it is a
small effect on the average, so expect this arm to come back unresolved rather than refuted, and do
not read "no significant difference" as "it does nothing". If it matters, it matters as a 4-of, and
that is a different arm.

## Stage S — the screening design (per `.claude/skills/deck-screening.md`)

The two lists differ in ~everything, so a single head-to-head answers "which list" and **not** the
question asked. The plan is **one pooled screen, read twice**, using
`scripts/screen_marginals.py --ref <arm>`, which re-centres a finished screen on any arm from the
per-game data already in its log — no extra games.

* `--ref base` → *does each candidate-B card earn a slot in the SHIPPED deck?*
* `--ref final` → *does each shipped card earn its slot back in CANDIDATE B?*

Both readings are needed because these cards are synergy-gated: Slimefoot wants sac outlets,
Shroofus wants a wide board, Concordant Crossroads wants summoning-sick tokens. A card can fail
additively and win in the new shell.

Apparatus rules that bind here:

* **Rule 0a — no union deck.** A union *table* over the real 60-card arms is fine; a superset
  decklist is not, ever.
* **The approved route for a card the shipped table does not bucket is
  `scripts/alias_card_into_bucket.py`** (USER 2026-09-02) — no mulligan regeneration. With ~9 new
  names this is the mechanism that keeps the apparatus shared; each alias must preserve its
  bucket's total so every arm's composition space stays identical to base's.
* **Copy `Fungus.value.json` next to any aliased profile** — the engine resolves sibling models
  directory-relative off the profile path, and detaching the value leaf is worth 1.35–84.8x.
* **The floor goes unmeasured on the alias route** (`--with-floor` generates). Say so; do not
  report `t` as a verdict.

## Screen 1 — the additive marginals, run on the five landed cards

`logs/fungus2/screen1.json`. Question: **does each candidate-B package earn a slot in the SHIPPED
deck?** Every arm is a small declared edit to `decks/Fungus/Fungus.cod`, so the shared-apparatus
premise actually holds — unlike a head-to-head against candidate B, which differs in 33 of 60 cards
and is two decks rather than an edit.

Run now rather than held for the last two cards, because five of seven are in and the result
de-risks the remaining build: if the Slimefoot package or the depletion mana comes back flat, that
changes what is worth paying for next.

### Apparatus

The shipped R=30 table with **8 names aliased into existing buckets**
(`scripts/alias_card_into_bucket.py`, the USER-approved 2026-09-02 no-regeneration route). K stays
**14** and entries stay **55,262**:

| bucket | after aliasing |
|---|---|
| Forest | `Blooming Marsh, Forest, Hickory Woodlot` |
| Sporesower Thallid | `Sporesower Thallid, Undercellar Myconid` |
| Thallid Shell-Dweller | `Concordant Crossroads, Deathspore Thallid, Shroofus Sproutsire, Slimefoot, Sol Ring, Thallid Shell-Dweller` |

Every arm preserves its host bucket's **total**, so each arm's composition space is identical to
base's and coverage is exact. The single exception is `mycoloth4`, which raises a 2-bucket to 4 —
the driver measured **0.39% of hands**, well under the 1% default.

**Forest is the universal filler** because its bucket sits at 19, so `cap = min(19, 7) = 7` is
already saturated: adding or removing Forests cannot create an untabled composition. Any other
card raised past 4 would.

### The arms, and why each is there

| arm | edit | question |
|---|---|---|
| `cut_shell` | −4 Thallid Shell-Dweller, +4 Forest | **the control.** Six arms cut the 0/5 defender as their donor, so if it is itself bad they are ALL flattered by its removal. Every other arm must be read against this, not against base. |
| `black_pkg` | +1 Slimefoot, +2 Deathspore, −3 Shell-Dweller; −4 Forest +4 Blooming Marsh | the non-combat clock, **measured with its enablers and with the black mana it actually costs** — not as a free splash |
| `crossroads4` | +4 Concordant Crossroads, −4 Shell-Dweller | is haste-for-tokens worth anything? At 4, not B's 1, so the effect can resolve at all |
| `shroofus4` | +4 Shroofus Sproutsire, −4 Shell-Dweller | the exponential combat engine, likewise at a resolvable count |
| `mycoloth4` | Mycoloth 2→4, −2 Shell-Dweller | B's single biggest count change |
| `undercellar4` | +4 Undercellar Myconid, −4 Sporesower Thallid | B's other 4-of swap, and the deck's only other black source |
| `woodlot` | −4 Forest, +4 Hickory Woodlot | the depletion-ramp thesis **in isolation from the curve** |
| `solring` | +1 Sol Ring, −1 Shell-Dweller | B has it; expect unresolved (see the 1-of note above) |

**Counts deliberately differ from candidate B's** on three arms. B runs Crossroads and Shroofus as
1-ofs, which cannot resolve against any realistic floor — a large per-game effect on 12% of games
is a small effect on the average. Screening them at 4 answers *"does this axis matter at all"*,
which is the prior question; if it does, how many is a follow-up.

### What this screen cannot say

* **The floor is unmeasured.** `--with-floor` generates a table, which the approved route forbids,
  so `t` is not a verdict here. The mitigation is an argument, not a measurement: every arm runs
  the same table over the same composition space, so the apparatus asymmetry the floor exists to
  bracket is far smaller by construction than under a pool table.
* **Nothing about Saproling Burst or Brightcap Badger**, which are not implemented. Candidate B
  itself is therefore not an arm. Screen 2 adds both.
* **Nothing about the wall-clock cost of the retired certificate** — that lands on the value-leaf
  label ladder (`WinlessCertificateActive` is gated on `LabelSearchScopeActive()`), which a screen
  never runs. A clean screen must NOT be read as "cheap to adopt".

## Open questions (surfaced, NOT blocking — CLAUDE.md)

1. **If adventure (Brightcap Badger) or fading + dynamic token P/T (Saproling Burst) turn out to be
   multi-day engine builds, do we build them or screen without them?**
   *Default taken: BUILD them.* A half-modelled card makes the comparison meaningless in the exact
   way `deck-screening.md` item 3 warns about, and both cards are 1-of/4-of slots the user
   explicitly wants judged. Sequencing: land the five cheap cards first, design the screen, then
   land the two hard ones **before** any measurement.
2. **Is the manabase meant to be judged as part of this, or held fixed?** *Default taken: judged as
   its own axis*, with at least one arm that gives candidate B the shipped 22-land green base, so
   the card changes and the mana changes can be read apart.
3. **Deferrals** on any clause the fan-out proposes to bracket-note are PROVISIONAL until signed
   off, per the skill. They are listed under `## Approved deferrals` below as they arrive.

## Engine bugs found along the way

### BUG 1 — Doubling Season does not double DEPLETION counters (real, and it biases THIS comparison)

`src/ai/LandPlay.cpp:103-108` stamps the depletion counter **directly** into `perm.counters`,
bypassing every `Put*Counters` chokepoint where `Doubling Season`'s `doubles_counters` replacement
lives. Under CR 121.6 / 614.1c "enters with N counters" is a replacement effect and Doubling Season
applies to it (the same rule as the well-known double-loyalty planeswalker interaction), so under
one Season **Peat Bog and Hickory Woodlot should enter with four depletion counters, not two** — a
four-tap land, i.e. twice the mana.

**Why it must be fixed before anything is measured:** the error runs **one way**. Candidate B plays
8 depletion lands alongside 4 Doubling Season; the shipped list plays **zero** depletion lands. So
the bug silently *under-rates candidate B's manabase* — which is precisely the "the engine models
one side of the comparison more completely than the other" hazard `deck-screening.md` puts on the
agent rather than the driver.

**The fix is free.** Checked every committed decklist: the depletion cards are Remote Farm,
Sandstone Needle, Saprazzan Skerry, Peat Bog, Hickory Woodlot (Angels, BreachingDragonstorm,
CritterLifegain, Dragonstorm, Mirrorwing Dragon, treasure_hunt), and the only counter-doubler in
`cards.json` is Doubling Season, which only Fungus plays. **No shipped deck holds both**, so routing
the stamp through the doubling chokepoint is byte-identical everywhere and changes no ground truth.
Verify with smoke's `play-changed=0` regardless.

## Decisions taken (surfaced, not blocked on)

### The winless certificate is NOT being extended in this phase

The Saproling Burst draft is right that whitelisting the new cards would recover the certificate's
measured **1.9x label speedup** — and also right that doing so requires three non-optional
soundness edits (`base_damage` under-counts a `0/0` CDA token to zero; `fodder` must include
tokens a live Burst can still mint; plus Slimefoot's non-combat drain and Concordant Crossroads'
haste, from the other cards). Each is a way to certify a lethal board as winless.

**Default taken: leave `FungusCertKnownDef`'s `kPool` alone.** An unlisted card makes the
certificate decline, which is the admissible direction, so candidate B is *correct but slower*. The
cost is bounded and measurable; the risk of a half-extended proof is not, and extending it is
cleanly separable work that can follow adoption. This keeps a soundness proof off the critical path
of a deckbuilding question.

**Stated consequence:** do not compare candidate B's wall clock to the shipped list's and call the
difference an engine fact. It is mostly the certificate being off.

## Approved deferrals

*(none yet — nothing has been proposed for sign-off)*

Proposed so far, all PROVISIONAL:

| card | clause | proposed reason |
|---|---|---|
| Saproling Burst | "They can't be regenerated" | no regeneration mechanic exists anywhere in the engine — cannot change an outcome |
| Saproling Burst | instant-speed activation | engine enumerates activations in main phases only; passive opponent offers nothing to respond to, and a token minted between turn N and N+1 first attacks on N+1 either way (identical to the note already shipped on Thallid) |

## Run log

| date | what | outcome |
|---|---|---|
| 2026-09-24 | Stage 1 coverage on candidate B | 8 missing, listed above |
| 2026-09-24 | Hickory Woodlot implemented (Tier 1) | done, no C++ |
| 2026-09-24 | Stage 2 per-card research fan-out (7 cards, 6 agents) | all returned |
| 2026-09-24 | **BUG 1 fixed** — `PutDepletionCounters` chokepoint; `LandPlay.cpp` routed through it | built clean |
| 2026-09-24 | **Concordant Crossroads implemented (Tier 1, zero C++)** | `grants_haste` + `affects_all_creatures` |
| 2026-09-24 | `test/unit/test_fungus_revision.cpp` — 5 new cases | **130/130 doctest pass** |
| 2026-09-24 | `bash test/regression.sh --smoke` | **93 passed / 0 failed, `play-changed=0` on both tiers** — the depletion fix is byte-identical for every shipped deck, as predicted |

### Verified by unit test (not assumed)

* Depletion counters now double: Peat Bog 2 → **4** under one Doubling Season, **8** under two
  (copies multiply — each is its own replacement effect), unchanged by the *opponent's* Season,
  and `n = 0` is not a counter event.
* Unchanged for the six committed decks that play depletion lands — asserted through
  `deck_has_counter_doubler == false`, the per-game stamp that short-circuits the walk.
* Hickory Woodlot and Peat Bog assert as a pair, so a future edit to one cannot silently diverge.
* Concordant Crossroads grants haste **from an enchantment** (the shared `HasHasteFromLords` scan
  applies no `IsCreature()` test — true, but nothing depended on it before, so it is now asserted),
  lifts **both** halves of CR 302.6 (`CanAttackFull` *and* `CanTapNow`), is correctly **not** a P/T
  lord, and is controller-scoped.

## An enumeration risk the drafts did not price — pool identical targets

Both Vitaspore ("target creature gains haste") and Deathspore ("target creature gets -1/-1") were
drafted as **one plan variant per legal target**, copying Umezawa's Jitte's mode-1 loop. That
precedent comes from KittyEquipment, which has a handful of creatures. **This deck routinely has
thirty-plus Saprolings**, and they are *identical*.

A naive per-target fan is therefore a ~30-way branch per activation, on the deck whose documented
cost centre is exactly this shape — CLAUDE.md's own lesson records *"Greedy walk = a POWERSET over
identical outlets — 4 Mycons → 8 bits for 9 outcomes; pool at EMISSION"*, and the count-pool fix
adopted 2026-09-23 was worth 2.29x on the heaviest cell.

**Requirement for the integrator: pool interchangeable targets at emission.** Hasting one of N
identical summoning-sick Saprolings is ONE distinct outcome, not N. The precedent to copy is
`FungusProvider::FoldSporeSourceIdentity` / the sac-outlet count pool, not Jitte. The targets that
must stay distinct are the ones that differ in a way the game reads: a fresh Mycoloth, a Sporecrown,
a body that has already attacked, a Saproling vs a Fungus.
