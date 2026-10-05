# Analysis ledger — Bruna

Deck: `decks/Bruna/Bruna.cod` (Bant Auras/Equipment voltron around Bruna, Light of Alabaster;
Glittering Wish into an 8-card sideboard). Branch/worktree: `bruna-analysis` @ `/home/vscode/wt/bruna`
(cut from origin c34312e8). Worktrees live OUTSIDE /tmp (a /tmp wipe lost work on 2026-10-05).

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells or blocks; opponent-facing triggers are inert, but
  SYMMETRIC / self-binding clauses bind us. Model the full card; never drop a self-affecting clause.
  Goldfish-inert deferrals are PROVISIONAL until the user signs off.
- No greedy / heuristic substitute inside the search window (user hard rule). Choices a card
  introduces (targets, auras to attach, modes, X, wish picks) are SEARCHED or surfaced to the viewer.
- A legal line the engine cannot express at any budget is a DEFECT, never a disclosed gap.
- Cast order / range / main-split are USER-OWNED: author and present, never adopt unilaterally.
- Memory safety (OOM on 2026-10-05): one heavy job (build or batch) at a time, --threads 20.

## Stage 1 — coverage (2026-10-05)
Missing (20): Eldrazi Conscription, Somberwald Sage, Bruna Light of Alabaster, Prodigious Growth,
Skycloud Expanse, Seaside Citadel, Glittering Wish, Mythic Proportions, Colossification, Arcanum
Wings, Botanical Sanctum, Avacyn's Pilgrim, Boseiju Who Endures, Mother of Runes, Open the Armory,
Indrik Umbra, Almost Perfect, Unflinching Courage, Elgaud Shieldmate, Worldfire.
Present: Lightning Greaves, Birds of Paradise, Forest, Azorius Chancery, Remote Farm, Sol Ring,
Razorverge Thicket, Wild Growth.

SCAN BUG: the coverage tool marks the whole sideboard reachable via Glittering Wish, but Glittering
Wish fetches only MULTICOLORED cards. Reachable: Bruna, Indrik Umbra, Almost Perfect, Unflinching
Courage. NOT reachable: Mythic Proportions, Avacyn's Pilgrim (both also mainboard), Elgaud Shieldmate,
Worldfire (sideboard-only, never castable in a game here -> not implemented; analyze_deck.py's
wish detection to be fixed to honour the wish's restriction).

## Stage 2 — cards (integrated 2026-10-05, branch `bruna-analysis`)

Drafts: `logs/bruna_drafts/*.json` (18 cards). Elgaud Shieldmate and Worldfire are NOT implemented --
unreachable (Glittering Wish fetches only multicolored cards; both are mono-coloured), now proven by
the restriction-aware coverage scan (below).

### USER RULINGS (signed off 2026-10-05, relayed by the orchestrator)
1. **All provisional deferrals SIGNED OFF**: trample (Mythic Proportions, Prodigious Growth, Unflinching
   Courage, Eldrazi Conscription), annihilator 2 (Conscription), flying (Arcanum Wings, Bruna), Indrik
   Umbra's first strike / lure / umbra armor, Almost Perfect's indestructible, Boseiju's channel, Bruna's
   "or blocks" half, Wings' opponent's-turn window collapse, Glittering Wish's autonomous-decline /
   reveal partials. **Mother of Runes' protection ability: DEFERRED for now** (no viewer wiring).
2. Colossification's tap is mitigated by IN-COMBAT entry (Wings' combat swap, Bruna's gather): both lines
   must be expressible and their +20 must count. Done + scenario-tested (both).
3. Wings swap pick: **rank on damage, take the top one** (provider-owned prune) -- PROVEN against the
   fully-branched control arm; counterexamples must be root-caused and the ranking fixed, never left
   branched. Same policy for Bruna's dominance collapse. Results below.
4. Viewer choices are mine (recorded below); user will give feedback.
5. KeepModel's power feature: NOT changed here -- open question with options (below).

### Card table

| Card | Tier | C++ / data summary | Deferrals (status) |
|---|---|---|---|
| Bruna, Light of Alabaster | 3 | `attack_gather_auras` -> `FireAttackGatherAuras` (both combat worlds, after pumps, before power is read): battlefield Auras + hand/graveyard Aura cards that COULD enchant her (`AuraCouldEnchant`, non-targeting -- Greaves' shroud does not stop it, Wild Growth never qualifies); attach first (no enter), then put (enters; Colossification taps her, harmless on an attacker). Subset = provider `BrunaGatherCandidates` (exact dominance collapse, take/skip only for base-setters), searched pin `Plan::bruna_gather_choice`; projection twin `CountAttackGatherPump` in `PendingAttackDamage`. Routing signature for `BrunaProvider`. | "or blocks" -- signed off. Flying -- signed off. |
| Eldrazi Conscription | 2 | `CardType::Kindred` (CR 308); flat +10/+10 aura. | trample, annihilator 2 -- signed off. |
| Somberwald Sage | 3 | `produces_one_color` (IsSingleColorBurstSource); `GameState::floating_creature_mana` (restricted leftover, creature payments drain it first, noncreature never; batch prepay too); colour-exact gate brute-forces a burst's ONE colour; never feeds a filter's {1}. | none |
| Prodigious Growth / Mythic Proportions | 1 | flat auras (+7/+7, +8/+8). | trample -- signed off. |
| Skycloud Expanse | 1 | `ramp_filter`. Integrator checks: (1) greedy ramp step now credits a land Aura (Wild Growth) -- REAL, fixed; (2) the pool no longer credits Wild Growth on an UNFEEDABLE ramp filter -- REAL, fixed; (3) creature-only mana can no longer feed the {1} (greedy `feeding` flag + `restricted_in_float`; backtracker `g_bt_restricted`; `HasUntappedRampFeeder` excludes creature-only) -- REAL, fixed. | none |
| Seaside Citadel / Botanical Sanctum / Avacyn's Pilgrim | 1 | existing templates. | none |
| Glittering Wish | 2 | `wish_requires_multicolored` (TutorNumericFilterOk conjunct + PerformTutor guard); `exiles_self_on_resolve`. | autonomous decline / reveal partials -- signed off. |
| Colossification | 2 | `aura_etb_tap_host` -> `ResolveAuraEnterTapHost` at EVERY aura-enter site (cast executor+rollout, Light-Paws, Skyhunter dig-put, Bruna put, Wings swap). Main-phase respond window: an able mana-dork host keeps its mana ability this phase (`Permanent::etb_tap_pending`, not an attacker, tapped at beginning of combat in both worlds). | none |
| Arcanum Wings | 3 | `aura_swap_cost` + `Keyword::AuraSwap`. Main phase: `Action::Kind::AuraSwap` (autonomous: damage-max Aura re-picked AT RESOLUTION; human: every legal Aura), site-9 key (cast-then-swap) + site-10 route (swap-then-recast). In combat: `Plan::combat_aura_swap_choice` -> `ApplyCombatAuraSwap` (both worlds, pays via TapForCostDirect, repairs atk_idx). `DeckUsesSecondMain` detects it. | flying; opponent's-turn window collapsed -- signed off. |
| Boseiju, Who Endures | 1 | G land, legendary. | channel (no legal target in goldfish) -- signed off. |
| Mother of Runes | 1 | vanilla 1/1 body. | protection {T} ability -- DEFERRED (user, 2026-10-05). Viewer cannot activate it (disclosed). |
| Open the Armory | 1 | tutor_to_hand Aura/Equipment; `BrunaProvider::TutorSearchWidth` 8 (7 distinct library names). | none |
| Indrik Umbra | 1 | +4/+4 aura. | first strike, lure, umbra armor -- signed off. |
| Almost Perfect | 2 | `aura_set_base_power/_toughness` -> layer-7b delta inside `AuraBonusFor` (Sage + AP + Conscription = 19/20, unit-tested); feeds_combat detects it. | indestructible -- signed off. |
| Unflinching Courage | 1 | +2/+2 + `aura_grants_lifelink`. | trample -- signed off. |

### Engine rules fixes (cross-deck) and decks expected to move at the next suite run
Each is its own commit; per-game verdicts are owed at the next smoke/regression/overnight.
* **Shroud vs Aura spells** (`CreatureTargetableByAuraSpell` in LegalEnchantTargets / ResolveEnchantTarget;
  hatch `MTG_LEGACY_SHROUD=1`). Deck scan: no suite deck pairs Lightning Greaves with a creature Aura ->
  expected byte-identical outside Bruna.
* **CR 704.5m orphaned-Aura SBA** (`SweepOrphanedAuras`: inside EnforceLegendRule + beginning of combat +
  turn start, both worlds; bestow faces exempt). Decks that can orphan an Aura and MAY MOVE: **Auras**
  (14 creature Auras, legendary Light-Paws -- an Aura on a doomed copy now hits the graveyard; Ethereal
  Armor's enchantment count drops), **Fungus** (Wild Growth + Simic Growth Chamber bounce),
  **EldraziDisplacerFlicker** (four land-Aura types + a Karoo). Disclosed approximation: between a detach
  and the next checkpoint an orphan still sits on the battlefield (it contributes no P/T).
* **AttackPowerOf counts Aura/Equipment power.** Deck scan: no exalted deck carries attachments, and the
  Hinata / Goblins / CreatureGiving readers' decks carry none -> expected byte-identical outside Bruna.
* **Creature-only leftover** (any `creature_mana_only` source): **Angels** (Giada) and **slivers_vial**
  (Ancient Ziggurat) may move only where a payment over-taps one (rare; previously the leftover went
  to the general float -- laundering). **SelesnyaLifegain** (Accomplished Alchemist): the colour-exact
  gate now models its X mana as ONE colour -- expected inert (no two-colour cost in that list).
* **Armored Skyhunter's dig-put uses the non-targeting host list** -- KittyEquipment has no Auras -> inert.

### Proofs for the two provider prunes (USER rulings 3 + clarification)
Method: same 400 seeds (7000+, default play d5/b20, `--threads 20`), the shipped arm vs a CONTROL arm
with the prune removed; `MTG_TRACE=swapproof,gatherproof` records every COMMITTED choice (real
resolutions only) against the pruned pick.
* **Bruna gather (control `MTG_BRUNA_GATHER_FULL=1`, full powerset, subset lists up to 64 wide):
  248 committed gathers, 248/248 took the collapse's subset (k = 0).** Avg 5.2625 (control) vs 5.2575;
  3 games differ (gi 14, 62, 334) and none can be the gather choice (k = 0 in every commit) -- they are
  the wider variant set re-allocating the fixed b20 budget.
* **Arcanum Wings swap (control `MTG_AURA_SWAP_BRANCH=1`, every legal Aura branched, main + combat).**
  Three iterations, each divergence root-caused and the ranking FIXED (never left branched):
  1. Run 1: 181 committed swaps in the control arm, 178 = the damage-max pick. Two were the
     ENUMERATION-time pick diverging from the APPLY-time ranking after an earlier cast in the same plan
     changed the hand (Glittering Wish -> Almost Perfect) -> FIXED: the autonomous main-phase swap
     re-takes the damage-max pick AT RESOLUTION (`Action::hand_index = -2`, both worlds), as the combat
     window already did. (Shipped arm: game 316 then won T5 instead of T6.)
  2. The third: a T6 main-phase swap where the branched search took Colossification over Indrik Umbra --
     the this-turn-only ranking undervalued Colossification (its ETB tap costs this turn's attack, but
     +20 next turn beats +4 now when nothing is lethal) -> FIXED: rank lethal-this-turn first (team
     attack on the post-swap board vs opponent life), else the host's damage over this turn + next.
     Unit-tested both ways.
  3. **Final run (same 400 seeds): shipped arm avg 5.2525, control 5.2575. Control arm: 181 committed
     swaps, 179 = the shipped ranking's pick; the 2 others (seeds 7218, 7377) are the control arm's own
     enumeration-time names (Mythic Proportions) where the resolution-time damage-max is Almost Perfect
     -- the shipped arm takes Almost Perfect; both games end on the same turn in both arms.** 4 games
     differ: the shipped arm is better in 3 (gi 6, 62, 316), the control in 1 (gi 197), whose winning
     line (traced) is the SAME rank-0 damage-max combat swap (Colossification) one turn earlier -- a line
     inside the shipped arm's own space that its b20 search did not reach: budget allocation, not the
     ranking. **No remaining ranking counterexample.**
  Artifacts: `logs/proof/{A,B,C,A2,B2,A3,B3}` (gitignored), `MTG_TRACE=swapproof,gatherproof`.

### Viewer wiring (2c-ter) -- my choices, user feedback pending
* Bruna's gather: the generic **`dragon` multi-pick**, asked once per zone (battlefield / hand /
  graveyard), provider subset preselected; a battlefield Aura's host is named in the source line.
* Arcanum Wings, main phase: an **`auraswap=<Aura>` plan line** (LineSpec verb, CheckLine always-own-verb
  matching, board activation on the Wings permanent, picker/flash/chip labels). Human play offers every
  legal hand Aura (never narrowed).
* Arcanum Wings, in combat: the reused **`dig`** modal at `ApplyCombatAuraSwap` (hand shown, legal = Auras
  that could enchant the host, decline = no swap), asked only when the {2}{U} is affordable (probed).
  (USER: "not the dig chooser" referred to the SEARCH pick -- the search uses the damage ranking; the
  human still needs a prompt and the dig shape is the closest existing modal. Say if a dedicated
  `aura_swap` type is wanted.)
* DECISIONS.md rows + `audit_viewer_decisions.py` manifest entries added (`attack_gather_auras -> dragon`,
  `aura_swap_cost -> main_phase` / `verb:auraswap`, inert classifications for the rest). Static audit
  rc 0; the live sweep is Stage 5h.

### Stage 2d-bis / 2e / Stage 3 results
* `audit_card_costs.py`: the full run 429'd on 15 cards; a slow (1 s) re-run of those + every new card:
  **22/22 compared, all match**. `audit_card_fields.py` (snapshot refreshed for the 18 new cards):
  **all hard fields match** (one pre-existing STALE allowlist entry, Glorybringer, not ours).
* Build: `./build.sh` clean (no warnings). `mtg-test`: all pass (incl. 10 new `test_bruna.cpp` cases).
  Scenarios: all pass incl. 3 new Bruna fixtures, each with a control (one more opponent life must NOT
  be won) that fails as required.
* **Coverage: `missing` = [] (0); 26/26 scanned cards `full`.** Every bracket note is a signed-off
  goldfish-inert deferral or a description of modelled behaviour.
* Scanner fix: `scripts/analyze_deck.py` sideboard reachability is restriction-aware (Scryfall cache under
  `logs/scryfall_cache/`, `--offline`).

### Defects recorded for later (docs/design)
* `antilifegain-tutor-single-pick-defect.md` -- AntiLifegainProvider's tutor target is a ONE-option pick
  whose ranking lives in an ENGINE file (SpellEffects.h `::TutorCandidates`), untested, with a
  shuffle-order `any_name` fallback: a violation of the 2026-09-30 no-greedy rule. Not changed here.
* `wish-restriction-in-finisher-scans.md` -- the EDF finisher-wish sideboard scans apply no wish filter
  (inert today).
* Greedy legacy filter step (3) / scarcity kind-2 omit the land-Aura credit for an `is_filter` land
  (sibling of the fixed ramp-filter case; no suite deck pairs them -- not changed to avoid moving GT).

### Open user questions (none block anything)
1. **KeepModel power feature** (`ExtractMidGameFeatures` sums bare EffectivePower -- blind to Auras,
   Equipment, lords, CDA). For Bruna the value leaf would read Bruna+Conscription as 5 power. Options:
   (a) leave it (disclose for Bruna's value-leaf stage); (b) add a NEW feature (aura/equip-inclusive power)
   beside the old one -- refit only decks that adopt it; (c) change the existing feature -- every fitted
   value model must be regenerated. Default taken: (a) until you choose.
2. Colossification's in-response mana credit: implemented as a deferred tap (mana abilities only, this
   phase). OK as modelled?
3. Combat-swap viewer prompt reuses `dig` -- want a dedicated decision type instead?
4. Cast order / range / main split for Bruna: still generic (user-owned; to be proposed at Stage 4).
5. Discard policy (§5i, gated): not yet authored for Bruna (a later stage); the Bruna payload Auras
   (Mythic Proportions, Indrik Umbra, Colossification) should not be max-MV-first discards.

## Stage 4 -- baseline profile + routing (2026-10-05, rebased onto origin c7487fb9, rebuilt)
* `analyze_deck.py decks/Bruna/Bruna.cod --no-rebuild --analyzer-seed 20261005 --offline`: profile
  written (`decks/Bruna/Bruna.profile.json`): card scores only (1000 games d5), no hand-score gate,
  default land window 1..5, stop_at 4, `required_pieces []`. Top scores: Open the Armory +0.59,
  Avacyn's Pilgrim +0.53, Sol Ring +0.37, Somberwald Sage +0.35.
* **Cost-aggregate diagnostic (analyzer step 7): STOPPED by me (my own run), recorded UN-RUN in the
  analyzer's form.** It plays 200 games at d3 with NO budget (`budget=0`), twice. On Bruna that config
  does not finish: 86 games over 30 s, the slowest finished at 446 s (8.5 M units), and two games were
  still running after ~25 min at 2 cores. Cause: see "Slow games" below -- an unbudgeted search
  multiplied by the clairvoyant-bottoming playouts. The discard-evidence step (8) therefore did not run
  either; the 5i measurement below replaces it (pooled bound, `MTG_SHED_WORST`).
* **4a routing:** `provider_audit.py --check` rc 0 -- `Bruna -> Bruna` (own provider, cert NotAssessed),
  no deck shares it, no deck rides Generic. Routing pin `test_pirates_provider.cpp` holds `Bruna -> Bruna`.

### Stage 4 -- PROPOSED cast order (USER-OWNED; shipped DEFAULT OFF as `MTG_BRUNA_ORDER`)
Generic today: Sol Ring 5, creatures 10, every other spell 20 (tutors, payloads, Wild Growth, Wings,
Greaves tied, plan order). Proposal (params only, `BrunaProvider::CastOrderRank`):
`4` same-turn mana (rock / land Aura) -> `6` tutor_to_hand -> `10` creatures -> `15` Arcanum Wings ->
`20` payload Auras -> `22` Colossification (ETB taps host) -> `25` Lightning Greaves LAST (its shroud
stops later Aura SPELLS targeting the host). Range / main split: none proposed (generic).
**Measurement (batch A, seed 8,900,000, paired per game):** d0 2000 g -0.0060 (t=-1.58, 20 better /
11 worse); d3 b10 400 g +0.0000 (4/3); **d5 b20 (play) 400 g +0.0025 (4 better / 4 worse)**. Every
d5b20 difference is BUDGET ALLOCATION: re-run at d5 b100/b400 both arms reach the same turn on all 8
(order-worse 43/13/155/168 = 5/5/5/7 in both arms at b100; order-better 51/125/161/276 are b20
starvation the base arm also recovers by b100-b400). Under `MTG_UNPRUNED` the order changes 1 game.
**Verdict: no measurable effect at play settings -> recommend keeping the generic order (OFF).** The
Greaves-last rule is a correctness-flavoured edge, but the search already finds those lines.

### Stage 5i -- discard policy (AI-AUTHORED, PENDING USER REVIEW; shipped default ON)
`docs/design/bruna-discard-policy-proposal.md` -- MANA (lands to 7 any-spell mana, max 3 kept; accel 1
below 6) + KILL (gatherer / payload / haste / host, each 1, net of board) + value-ordered overflow;
Bruna-on-board makes hand payloads graveyard-equivalent (her trigger gathers from the graveyard);
dead legend, blank Karoo. `MTG_BRUNA_BUCKET_DISCARD=0` = generic max-MV hatch. 5 unit cases.
**Measurement (batch A):** shed census real=720 / rollout=12,341,905 per 12,000 games (97% under 4
lands, all at hand size 8). **BOUND = 0:** `MTG_SHED_WORST` (worst-ranked shed) vs base: d0 0 changed
(digest identical), d3 b10 +0.0075 (1/2, t=0.9), d5 b20 +0.0025 (1/1). Authored vs generic max-MV:
d0 +0.0020 (8/11, t=0.69), d3 b10 0 (1/2), d5 b20 -0.0025 (1/1). Non-inferior; no ranking is worth a
measurable turn on this deck, so the axis is closed. Discard gate note: the `discard_policy` gate
(`verify_deck.py`) and `scripts/discard_policy_audit.py` live only on `wip/discard-policy-gate-2026-09-23`
(not on this branch) -- the policy satisfies their contract (own override, `MTG_*_BUCKET_DISCARD` hatch,
not inherited).

## Stage 5 -- verification (2026-10-05)
All runs pooled `mtg --batch --threads 20`, one heavy job at a time. Logs `logs/bruna_s5/` (gitignored).

### 5b -- multi-depth sanity (batch A, seed 8,900,000, MTG_DUMP_WINS + MTG_FLAG_NONCONV)
| cell | games | win% | avg (unwon=9) | core-ms/game |
|---|---|---|---|---|
| d0 | 2000 | 83.9 | 6.6050 | ~0 |
| d3 b10 | 400 | 98.0 | 5.0700 | 1,564 |
| d3 b20 | 400 | 98.25 | 5.0575 | 2,465 |
| d5 b20 (play) | 400 | 98.25 | 5.0575 | 2,625 |
| d5 b40 | 400 | 98.25 | 5.0475 | 4,332 |
Monotone: d3b10 vs d0 304 faster / 0 slower; d5b40 vs d5b20 4 faster / 0 slower; d3b20 == d5b20
per game. ONE inversion: gi320 T5 at d3b10, T6 at d3b20/d5b20 -- recovers to T5 at d5 b100 (budget
churn, not a defect). Clock: T3 17 / T4 121 / T5 152 / T6 66 / T7 27 / T8 10 at play settings.
**The 7 unwon games (gi 92, 147, 151, 172, 270, 366, 397) are unwon in EVERY arm** (d0..d5b40, order,
discard, MTG_UNPRUNED, d5 b100, d5 b400). Read from the logs: no creature ever available as a host
(92, 366), no payload Aura drawn (147), stuck on 2 lands T2-T6 and Bruna uncastable with Sage's ONE
colour (151), 5 lands + only a shrouded Mother of Runes (172), no green source for the dorks (270),
three lands by T8 (397). Structural -- draw/mana screw, not play.
`[nonconv]`: 0 lines over 12,000 games.

### 5a -- mismatch harnesses (batch B)
Seeds 1001/2002/3003/4004 x {d3 b10, d5 b20} x 300 = 2,400 games, `MTG_FD_ORACLE=1 MTG_FLAG_NONCONV=1`
(full-depth commit-the-line is the DEFAULT engine; the skill's `MTG_FULL_DEPTH` is a deleted flag --
the binary warns it is unread): **0 `[fd-diverge]`, 0 `[nonconv]`.**

### 5c / 5e / 5f -- budget starvation + the full-search oracle (batch C, MTG_UNPRUNED=1, same seeds)
Unpruned d5b20 avg 5.0750 vs shipped 5.0575 (oracle diluted at fixed budget: 11 slower, 6 faster).
The 6 oracle-faster games, each re-run on the SHIPPED config (two-stage recovery: d<win turn> b100,
then b400): gi51 (b100), gi169 (b400), gi359 (b400), gi161 (d6 b400), gi320 (b100) -> **budget
starvation at b20**, recovered by budget alone. **gi266 is a MULLIGAN difference, not play:** the
two arms bottom different cards from the same 7 (shipped keeps Open the Armory + Bruna, unpruned keeps
Colossification + Glittering Wish and wins T4 by Wings on Mother of Runes + in-combat swap to
Colossification). No single `MTG_UNPRUNE=<gate>` reproduces it (30 gates tried); only the whole set,
which moves the clairvoyant-bottoming playouts. Belongs to the mulligan stage (exhaustive keep/bottom
table), not to a play prune. **No prune counterexample.**

### Slow games -- characterised (MTG_TURN_CENSUS, d5 b20, 200 games, seeds 8,950,000+, single-threaded)
* **The 30-60 s games are MULLIGAN games, not Wings/Bruna branching.** 48 of 200 games mulliganed;
  for each, the clairvoyant lookahead bottoming plays EVERY candidate bottom out as a full d5/b20 game
  before the real game (7-8 playouts for a 1-card bottom, 17-22 for 2 cards: C(7,2)=21 + the real
  game). Those pre-game playouts are **68.7% of all search units and 62% of wall**. Every game over 10 s
  single-threaded (22.1, 18.2, 15.2, 12.2, 11.5, 10.8 s) is one with 6-22 playouts; its REAL game costs
  0.1-1.4 s. Real-game wall/game: median 0.51 s, max 4.8 s.
* Real-game decisions: median 4,545 units, max 184,785 (10x budget; 0 decisions above 20x; the 25x
  overrun guard never fired). Site shares are flat across the tail -- la_cand 34%, rollout_step 29%,
  the d0 horizon leaf 27% (`greedy_fallback` = the depth<=0 leaf, the allowed rollout policy), bp waves
  6%. No Bruna-specific site; the continuation memo holds 0.84 median / 0.64 on the slowest decile.
* The unbudgeted d3 pathology (analyzer step 7) is the same multiplier with no per-decision budget.
* **Proposed fixes (none implemented here).** The machinery a fix would reach for already exists: the
  candidate playouts share ONE transposition table (`shared_tt`, AIEngine.cpp), same-name bottoms are
  folded (`MTG_BOTTOM_NAME_DEDUPE`, default on -- why 17 not 22 playouts), and 2-card bottoms use the
  legal-subset table (C(7,2)=21 trials). So: (1) the pipeline's own answer -- once the exhaustive
  keep/bottom table exists (mulligan stage, user-initiated) the bottom is a table read and the playouts
  disappear; (2) until then, a per-deck `bottom_eval_depth / _budget_ms / _topk / _units` profile
  policy (cheaper candidate playouts; the Fluctuator precedent) -- a measured quality-vs-cost trade the
  USER would adopt, not a truncation inside the play search; (3) nothing Bruna-specific (Wings swap,
  Bruna gather) shows up in the cost: their branching is already collapsed by the proven prunes.
