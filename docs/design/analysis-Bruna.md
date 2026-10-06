# Analysis ledger — Bruna

> **Path:** this ledger is `docs/design/analysis-Bruna.md` (renamed 2026-10-05 from `analysis-bruna.md`):
> `verify_deck.py` resolves the ledger as `analysis-<decklist stem>.md` = `analysis-Bruna.md`, so under
> the old name its `## Approved deferrals` and `## Claude-play sweep` sections were invisible to the gate.

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

### 5c2 -- horizon-honest tie-break (`leaf_tiebreak_check.py`, PLAY settings d5/b20) -- KEEP THE DEFAULT (ON)
Two independent 6-block runs (taskset 20 cpus): seeds 1.0M-1.5M -> +5 turns over 6,000 paired (13 worse
/ 9 better; the script printed OPT OUT); fresh seeds 1.6M-2.1M -> -2 turns (13 / 12; KEEP). **Pooled
12,000 paired: +3 turns (26 worse / 21 better) = +0.00025/game, half-signs +3/+2/-2/0 -- no sign.**
Binding rate 0.4%. Not opting out on a sign that flips between two samples.
Gate 2 ("does the tie-break DELETE a win?"), every one of the 26 tie-break-worse games re-run with the
tie-break ON: 17 recover at d<win turn> b100; of the 8 others, 7 recover with budget alone (b400/b1600;
gi112 even goes T5 -> T4); gi421 (s1.8M) won T4 only in the OFF arm at b20 -- the OFF arm itself plays
T5 at d5 b100/b400 and d8 b400, so that T4 is a budget accident of the off arm, not a line the tie-break
removes. **No structural deletion -> default ON stands; no provider override added.**

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

### Stage 5 gate status (verify_deck 2026-10-05, commit after d453aa4d) -- what each non-green means
* `card_costs` FAIL = **did-not-run, not a mismatch**: the fleet-wide audit (all of cards.json) hit Scryfall
  429 on 107 cards, none of them Bruna's. Deck-scoped re-run (`audit_card_costs.py --cards <Bruna's 26>
  --throttle 1.0`): **18 costed cards, 18 compared, all match.** Re-run the fleet audit when the rate limit
  clears; nothing to sign off.
* `regression_tiers` FAIL (partial) = rows are committed in all three tiers (`bruna`, `bruna2hg` smoke
  canary), GT not yet accepted -- **the orchestrator runs smoke + regression and accepts.** **Overnight GT is
  NOT to be run until the USER asks** (rows exist; the overnight tier stays PARTIAL for this deck until then).
* `claude_sweep` SKIP = **UN-RUN** -- the orchestrator fans out the 5d claude-play sweep and records it under
  `## Claude-play sweep` in THIS file (`analysis-Bruna.md`).
* `discard_policy` gate: not on this branch (lives on `wip/discard-policy-gate-2026-09-23`); the authored
  policy meets its contract (own override + `MTG_BRUNA_BUCKET_DISCARD` hatch).
* Green: coverage, card_fields, viewer, viewer_wiring, mismatch (plus my 2,400-game 5a run), play_invariants,
  suite. Cost rule (`suite_gate.py`): d5 b20 2.63 s/game pooled vs the 3.10 s budget (3x fivecolour).

### For the 5d claude-play sweep -- worth a look
1. Lightning Greaves' shroud stops later Aura SPELLS on its host (CR 303.4a) but not Bruna's gather / Wings'
   swap (puts). Check the search never strands an Aura behind an earlier Greaves equip when a different order
   would have cast it.
2. Arcanum Wings in-combat swap into Colossification (+20, tap harmless on an attacker) -- the gi266 T4 line.
3. Bruna gathering from the GRAVEYARD after a cleanup discard (the discard policy leans on it).
4. Somberwald Sage: ONE colour per tap, creature-only -- e.g. gi151 (no W+U split for Bruna).
5. Mulligan bottoms (gi266 shows the bottom choice decides T4 vs T5).

### Open questions for the user (none blocks anything; defaults taken)
1. Cast order: proposal measured NEUTRAL at play settings -> recommend keep generic (`MTG_BRUNA_ORDER` OFF).
2. Discard buckets shipped default ON (PROPOSED; bound = 0, non-inferior) -- amend/approve
   (`docs/design/bruna-discard-policy-proposal.md`, doubts listed there).
3. Mulligan-game cost (62% of wall in bottoming playouts): wait for the exhaustive table, or a per-deck
   `bottom_eval_*` policy before then?
4. The analyzer's unbudgeted-d3 cost diagnostic cannot finish on this deck (and decks like it) -- should
   `analyze_deck.py` run it budgeted?

## Claude-play sweep

commit: `a0b8e7ab` (code HEAD after every fix below; the ledger commit follows it)
seeds: 77001-77020 (one game per seed, `--claude-play --max-turns 8 --reveal 6`)
games: 20
flags: 0 unresolved

Every flag the 5d sweep raised, its verdict and the commit that closes it. Each fix carries a unit or
scenario test (`test/unit/test_bruna_sweep.cpp`, 14 cases; `test/scenarios/bruna_wings_swap_chain.json`)
and every one of those FAILS on the pre-fix tree `71893246` (14/14 unit cases fail there; the scenario
reads T5 there, and its opponent-22 control does not win T4 on the fixed tree).

| # | Finding | Verdict | Fix |
|---|---|---|---|
| A-i | 77012: Sage's creature-only surplus in a MIXED batch ended as general float | CONFIRMED -- the mixed two-stage prepay pays its creature stage with for_creature=true; surplus was booked general. Creature stage's pre-paid pool now goes to floating_creature_mana | `a80ebbb8` |
| A-ii | 77003: Sage + Colossification/Mythic Proportions + equip offered (10 mana vs 7) | CONFIRMED -- the hasted-dork credit let restricted units "pay" the enablers in the joint check. Restricted credit capped at the subset's other creature MV; credited dork colours widen the colour-presence gate (Forests + Sage -> Mother of Runes was rejected) | `a80ebbb8` |
| A-iii | (found fixing A) Greaves -> Sage never offered beside a bigger creature | CONFIRMED legal line inexpressible (width-1 haste ranking is attack-only). Best locked mana-dork host always kept | `a80ebbb8` |
| B | 77002: "Courage -> Pilgrim" priced but realised on Mother (silent retarget); 77014 equip-then-Aura | CONFIRMED -- shrouded hosts never Aura candidates; equips trail casts; and the autonomous dedup keyed creature Auras by NAME (host never searched). Inject Greaves-shrouded hosts + co-selected-move reject, shroud-release fires before the Aura (both worlds), unlock equip onto an Aura target deferred, Bruna opts into a host-keyed signature (`MTG_BRUNA_AURA_HOST_SIG`; SUPERSEDED 2026-10-06 by the shared host ranking, `49a261c6`), retargets counted/reported (`[enchant-retarget]`, `MTG_ENCHANT_RETARGET_ABORT`). Host axis measured 400 paired d5/b20: 5.0575 -> 5.0425 (7 better / 1 worse, t=-2.13; gi370 recovers at b100), +5.6% ms | `aee1aa79`, `7414e9c8` |
| C | 77001: Wild Growth stays attached to a land that left until combat | CONFIRMED (CR 704.5m). Sweep inside SacrificeDepletedLands (when it sacked) and BounceKarooLand (shared by both worlds); karoo re-located by identity | `4f803e5a` |
| D | 77001: Wild Growth on a same-turn Azorius Chancery inexpressible | CONFIRMED -- the deferred karoo is never on the battlefield at enumeration. The karoo branch publishes its card number as a land-Aura host; both worlds hold that Aura until the deferred karoo lands (same mana as the in-response tap) | `70807259` |
| E1 | 77008: Claude T4, AI T5 | (b) BUDGET STARVATION -- expressible: from the T2 handoff T4 at b1600 (T5 at b200); game start T4 at b800. Contributing: the horizon leaf never swaps in combat. A rollout swap pin (`MTG_ROLLOUT_AURA_SWAP`) found T4 at b200 and measured 1 better/0 worse over 400, but its trajectory exposed a latent executor/rollout site-9 mismatch (seed 4205) -> shipped DEFAULT OFF, deferred in `docs/design/site9-continuation-index-mismatch.md`. Final binary: T4 in the d5/b200 batch cell | `c9f2291b`, `a0b8e7ab` |
| E2 | 77001: Claude T4 by DOUBLE Wings swap, AI T5 | (a) INEXPRESSIBLE at any budget (T5 at b3200; old binary T5 at b1600) -- swap->recast->swap needs a site-9 continuation inside a site-10 continuation. Compound AuraSwap chain (chosen_x = 2..3), recast through each world's cast path, shroud-guarded. Now: T4-start handoff T4 at b20; game start T4 at b800 (b200 T5 = budget) | `c9f2291b`, `a0b8e7ab` |
| F | 77004 / 77015: plans whose canonical order cannot pay drop a cast | CONFIRMED for the SEARCH (the `drops` field on human ORDER variants is by design): the enumerator credits Wild Growth's ramp, the generic rank cast it after the creatures. Ramp land Auras ranked with rocks (5) | `787fefdd` |
| G | 77019: 8 mana tapped for 6-cost Bruna; 77012 Pilgrims | CONFIRMED (77019) -- Sage ranked last. Creature-only source first for a creature payment (shared payer). 77012 was the A-i laundering; the old Pilgrim now taps only to feed Skycloud Expanse, which the line needs | `4e14d50c` |
| H | combat-swap `heuristic_default` -1, "into your hand" note | CONFIRMED cosmetic. Default = AuraSwapPick; note rewritten (verified 77008 T4 combat: default 2 = Colossification) | `5f4d7a78` |

Re-verified after the fixes (final binary):
* Mismatch harness (Stage-5a set: seeds 1001/2002/3003/4004 x {d3 b10, d5 b20} x 300 = 2,400 games,
  `MTG_FD_ORACLE=1 MTG_FLAG_NONCONV=1`): **0 `[fd-diverge]`, 0 `[nonconv]`.** (With the E rollout pin ON it
  read 1 -- the reason that lever ships OFF.)
* play_invariants (verify_deck's seeds 7001/7002 x 4): ok, 8 games / 180 decisions. Over 77001-77020 x 1 the
  auto-follower runs away on 7 seeds -- free `equip Lightning Greaves` {0} re-points ping-ponging forever
  in a post-combat main (follower policy picks plan 0 among zero-cast plans); identical on the pre-fix
  binary (71893246), not an engine regression.
* `mtg-test`: 427/427. Bruna scenarios 4/4.
* Sweep seeds, d5/b200 batch cell, pre-fix -> fixed: 77001 5->4, 77008 5->4, 77016 4->5 (recovers T4 at
  d5 b400 -- budget churn from the wider host/chain plan space), the other 17 unchanged.

Residual, recorded (not sweep flags):
* `[enchant-retarget]` still counts in rollouts when an ENABLER cast is dropped at apply for mana (e.g. a
  Pilgrim whose {G} a karoo-as-wild pool over-credited, so the Greaves move it hosts never fires). Counted,
  never silent; no plan label lies about a legal line.
* B keeps a conservative reject in Solve's (d0) plan builder for "Aura onto the Greaves move's destination"
  (EnumeratePlans orders it legally; Solve has no such sort).

Decks other than Bruna whose play may change (for the GT verdicts):
* A: **Angels** (Giada creature-only mana in a mixed prepay; Greaves + dork unlock host / colour presence),
  **FiveColour** (Greaves + dorks: unlock host kept, colour-presence widening), **slivers_vial** (Ancient
  Ziggurat in a mixed prepay).
* C: **Fungus** (Wild Growth + Simic Growth Chamber), **EldraziDisplacerFlicker** (land Auras + a Karoo).
* D: **Fungus**, **EldraziDisplacerFlicker**.
* F: **Fungus** (generic rank; EDF has its own).
* G: **Angels**, **slivers_vial** (tap ORDER on creature payments).
* B / E / H: none (Bruna-only opt-in / no other Wings / display only). MTG_UNPRUNED oracle arms of
  creature-Aura decks (Auras) widen (diagnostic only).

Open questions for the user (none blocked anything; defaults taken):
1. ~~The autonomous dedup folds a creature Aura's HOST by name in every deck -- a heuristic substitute in the
   search window. Bruna opts in to a searched host; extend to Auras (Bogles) and the rest? Default: Bruna only.~~
   **ANSWERED (USER 2026-10-06): "not a difficult decision. It makes sense to have a heuristic for it."** One
   proven host ranking replaces both the fold and Bruna's branching -- see "Aura host ranking" at the end.
2. F changes the GENERIC cast rank (ramp land Auras with rocks) -- Fungus moves. Cast order is user-owned;
   I treated it as the same realisation rule as rocks. Approve, or restrict to Bruna?
3. ~~`MTG_ROLLOUT_AURA_SWAP` ... ships OFF until the site-9 mismatch it exposed is fixed. Want that mismatch
   prioritised?~~ **ANSWERED (USER 2026-10-05): "if it is preventing correct lines we should be fixing it."**
   Fixed (BP-NODE child numbering) and the lever is now DEFAULT ON -- see "Site-9 numbering fix" at the end.
4. G (creature-only source first) moves Angels/slivers tap order -- approve.

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-10-05)

`verify_deck.py decks/Bruna/Bruna.cod --write-ledger` -> **FAIL**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 26 cards full (missing=0, partial=0) |
| card_costs | FAIL | yes | cost audit INCOMPLETE -- 107 cost(s) UNVERIFIED (Scryfall rate-limited/unreachable) |
| card_fields | PASS | yes | 518 cards match snapshot (cost/PT/types/keywords); 14 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| regression_tiers | FAIL | yes | PARTIAL suite addition (smoke: no GT, regression: no GT, overnight: no GT) |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | 2 type(s) wired (emitter + GUI): bounce, dragon |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 7002] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/180 decisions: determinism+integrity+progress hold |
| claude_sweep | SKIP | yes | no Claude-play sweep recorded |
| suite | PASS | yes | suite gate: Bruna is regression key `bruna` |

### Pending user sign-off (block the gate until fixed OR approved below)
Add a key to `## Approved deferrals` to sign one off (only if it is a genuine, understood deferral -- not a bug):
- `card_costs:*` -- 107 card(s) could not be fetched, so their costs were never compared -- a did-not-run, not a mismatch. Re-run when the rate limit clears.
- `card_costs:Hatchery Sliver` -- Hatchery Sliver: HTTP 429
- `card_costs:Aether Vial` -- Aether Vial: HTTP 429
- `card_costs:Thrumming Hivepool` -- Thrumming Hivepool: HTTP 429
- `card_costs:Treasure Hunt` -- Treasure Hunt: HTTP 429
- `card_costs:Land's Edge` -- Land's Edge: HTTP 429
- `card_costs:Throes of Chaos` -- Throes of Chaos: HTTP 429
- `card_costs:Dauntless Bodyguard` -- Dauntless Bodyguard: HTTP 429
- `card_costs:Venerable Knight` -- Venerable Knight: HTTP 429
- `card_costs:Swords to Plowshares` -- Swords to Plowshares: HTTP 429
- `card_costs:Invigorate` -- Invigorate: HTTP 429
- `regression_tiers:partial` -- smoke: no GT, regression: no GT, overnight: no GT -- all tiers or none; never sign this off. Add the missing rows and accept each tier's GT (`bash test/regression.sh --<tier> --deck=<key>`, inspect, `--<tier> --accept --deck=<key>`)

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Eldrazi Conscription: Flat +10/+10 modelled (aura_power_bonus/aura_tough_bonus; toughness stored, inert vs the passive opponent). Colorless: protection-from-a-colour (Mother of Runes) never makes it fall off. Trample inert (the goldfish opponent never blocks). Annihilator 2 inert (user-approved 2026-10-05): the defending player sacrifices, and the goldfish opponent controls no permanents (no OpponentSpawn for this deck) -- user-approved 2026-10-05. Kindred/Eldrazi typed faithfully; no card in the pool reads Kindred or Eldrazi.
- coverage deferral -- Lightning Greaves: Equipment subsystem: Permanent::equipped_to + Action::Kind::Equip (sorcery-speed, one action per (Equipment, controlled creature), mutually exclusive per plan; Equip {0} = free re-point each turn). Haste read by CanAttackFull (attacks) and CanTapNow ({T} abilities) -- CR 302.6 lifts a single restriction covering both, so equip haste now unlocks same-turn tap abilities too (a Greaves'd fresh mana dork taps; a Greaves'd fresh Deathrite Shaman may use its graveyard-exile modes). Shroud MODELLED (equip_grants_shroud -> CreatureHasShroud): it binds OUR OWN targeting too (CR 702.18a) -- a Greaves'd creature is not a legal equip target or Aura-spell target (LegalEnchantTargets / ResolveEnchantTarget, CR 303.4a), while a non-targeting attach/put (Balan, Skyhunter, Bruna's attack trigger, Arcanum Wings' aura swap -- CR 303.4f) is unaffected. Falls off a dead host (SBA), never sacrificed.
- coverage deferral -- Somberwald Sage: mana_dork + produces WUBRG + produces_amount 3 + produces_one_color (the Accomplished Alchemist single-colour-burst shape: three units of ONE search/payer-chosen colour, NOT a Karoo one-of-each bundle -- both payers suppress the bundle rule via IsSingleColorBurstSource) + creature_mana_only (RestrictedManaUsable: refused for every noncreature spell and every activated ability, e.g. Lightning Greaves' equip, Boseiju's channel, Colossification, Eldrazi Conscription -- a KINDRED Enchantment Aura is NOT a creature spell). Unspent units of a Sage tap float in the CREATURE-ONLY reserve (floating_creature_mana), spendable only by a later creature cast this phase and emptied with floating_mana, so a Sage paying Mother of Runes {W} leaves {W}{W} for another creature, never for Colossification. Summoning sickness applies to {T}; Lightning Greaves' haste unlocks it the turn it enters (CR 302.6). In this deck the restricted mana can pay: Bruna, Light of Alabaster {3}{W}{W}{U} (incl. a wished sideboard copy), Somberwald Sage, Birds of Paradise, Avacyn's Pilgrim, Mother of Runes. Colour choice is engine-owned at payment (same as Ancient Ziggurat / Giada): a creature-only source cannot be hand pre-tapped into the general float.
- coverage deferral -- Bruna, Light of Alabaster: Attack half: attack_gather_auras -- resolved at declare-attackers (after pumps, before the damage loop) in BOTH worlds by FireAttackGatherAuras; WHICH Auras is a SEARCHED subset (Plan::bruna_gather_choice -> GameState::scripted_bruna_gather), the provider's BrunaGatherCandidates is the only narrower (exact goldfish dominance collapse: additive / keyword-only Auras always taken; host-dependent base-setting Auras such as Almost Perfect branch take/skip). Non-targeting (CR 303.4f / 701.3): shroud from Lightning Greaves does NOT block it; the Enchant restriction does (AuraCouldEnchant: Wild Growth -- enchant land -- never qualifies; protection is not modelled anywhere, Mother of Runes' activation being a user-approved deferral). Human play picks each zone's subset with the reused `dragon` multi-pick (provider subset preselected). The collapse is PROVEN: under the full-powerset control arm (MTG_BRUNA_GATHER_FULL) 248/248 committed gathers took the collapse's subset (Bruna ledger). Moved Auras do not re-enter (no ETB); put Auras ENTER (Colossification's 'tap enchanted creature' fires -- harmless on an attacker, CR 506.4/510.1). Put Auras are not CAST (no cast triggers; none in this deck). Opponent-controlled Auras: the goldfish opponent has none. Deferral (user-approved 2026-10-05): the 'or blocks' half is goldfish-inert (the passive opponent never attacks, so Bruna never blocks). Flying inert (no blockers); vigilance modelled (keyword) but inert (no tap abilities, never blocks).
- coverage deferral -- Prodigious Growth: +7/+7 modelled (aura_power_bonus/aura_tough_bonus; +T stored, inert vs the passive opponent). Trample inert: the goldfish opponent never blocks, so trample never changes damage dealt (same disclosure as Rancor/Armadillo Cloak). No cast-dependent clause: the bonus applies identically when Bruna, Light of Alabaster puts it onto the battlefield attached to her.
- coverage deferral -- Skycloud Expanse: Ramp filter, the Ferrous Lake / Mossfire Valley shape (ramp_filter): the {1} activation cost is GENERIC (any mana pays it -- a basic, a dork, Sol Ring's {C}, a Karoo's output, or floating mana) and one activation yields ONE {W} AND ONE {U} (net +1 mana). There is NO free '{T}: Add {C}' mode -- with no feeder the land makes nothing, which is exactly what ramp_filter models (the pool credits +1 wild iff HasUntappedRampFeeder or floating mana, else 0; the backtracker branches the feed off ConsumeFloatingAny and adds one of each produces colour). Does NOT enter tapped. Single clause; nothing deferred. Pool credit is 1 wild (colour-optimistic: it cannot actually make {G}); the authoritative payer is exact.
- coverage deferral -- Azorius Chancery: Karoo bounce land: enters tapped, makes 2 mana ({W}{U}, modelled as wild like other duals), and on ETB returns one of your lands to hand (BounceKarooLand prefers a tapped land so no mana is lost this turn; the returned land must be replayed, the real tempo cost).
- coverage deferral -- Seaside Citadel: Tapped tri-land, the Mystic Monastery shape; both clauses modelled, nothing deferred. (1) 'This land enters tapped' -> enters_tapped, via the shared LandEntersTapped / LandWouldEnterTapped predicate so enumeration and the real drop agree (no condition, no choice). (2) '{T}: Add {G}, {W}, or {U}' -> produces [G,W,U
- coverage deferral -- Glittering Wish: Bruna's sideboard access (4 mainboard copies). Built EXACTLY as Living Wish: a WISH is a TUTOR whose search ZONE is the sideboard (CR 400.11b), so tutor_to_hand + wish_from_sideboard ride the whole searched-tutor apparatus -- the Plan::tutor_choice index axis, the plan-signature folds, the breakpoint pin, the human tutor chooser -- with only the pool swapped. The ONE difference is the restriction: 'a MULTICOLORED card' (any card type) is tutor_types [
- coverage deferral -- Glittering Wish: PARTIAL: 'You may' -- declining is offered to a HUMAN (the tutor chooser returns -1) but is not emitted as an autonomous variant. WHY inert: the fetch costs nothing beyond the spell already cast and taking is weakly dominant -- even at 8+ cards the cleanup discard can shed the fetched card, and an Aura in the graveyard is still reachable by Bruna's attack trigger. Disclosed, not silently dropped.
- coverage deferral -- Glittering Wish: PARTIAL: 'reveal' -- the information half is inert (the passive opponent makes no decisions and nothing reads 'was revealed'); the viewer half rides PerformTutor's existing '(searched)' reveal.
- coverage deferral -- Mythic Proportions: +8/+8 modelled (toughness stored, inert vs the passive opponent like every aura_tough_bonus). Trample inert: the goldfish opponent never blocks, so trample cannot change damage dealt.
- coverage deferral -- Colossification: FULLY MODELED. +20/+20 via aura_power_bonus/aura_tough_bonus (AuraBonusFor at every combat/projection site). ETB tap via aura_etb_tap_host: a mandatory trigger resolved by the shared ResolveAuraEnterTapHost right AFTER aura_attached_to is set, at EVERY aura-enter site (cast executor + rollout apply_one, Bruna's attack-trigger put, aura swap, dig/tutor-attach, go-off apply) -- CR 603.6a, a PUT triggers it just like a cast. Tapping an ATTACKING host does not remove it from combat (CR 506.4) so Bruna's put still adds 20 this combat; re-attaching via Bruna is not an enter and does not tap. Host-in-response mana (CR 605.3a/106.4): in a MAIN PHASE an untapped mana dork that can tap now is left untapped with Permanent::etb_tap_pending -- the payer may still tap it for mana (colour and creature-only restriction chosen at payment) for the rest of the phase, it is NOT a legal attacker, and it is tapped at the beginning of combat (ApplyPendingEtbTaps, both worlds). Enchant-creature host is the searched enchant_target plan variant; a shrouded host (Lightning Greaves) is not a legal CAST target (CR 303.4a) but is a legal PUT host (CR 303.4f).
- coverage deferral -- Arcanum Wings: Enchant creature: is_aura, host = searched enchant_target. AURA SWAP MODELLED (aura_swap_cost) by the shared ApplyAuraSwap: Wings returns to hand and the hand Aura is PUT (not cast -- no cast triggers, does not target, so Lightning Greaves' shroud does NOT stop it, CR 303.4a vs 303.4f) onto the same creature and ENTERS (Colossification's ETB taps the host). If either half cannot complete nothing happens (ruling). TWO windows, both searched: the MAIN PHASE (Action::Kind::AuraSwap; a Wings cast this plan reaches it through breakpoint site 9, and the swap's return-to-hand opens site 10 for a recast) and IN COMBAT after attack triggers resolve (Plan::combat_aura_swap_choice -> ApplyCombatAuraSwap, both worlds), where Colossification's tap is free (CR 506.4). WHICH hand Aura comes in is the provider's DAMAGE-MAX pick (USER ruling 2026-10-05; AuraSwapRanking prices the swap by performing it: Almost Perfect's base set, a Colossification tap in a main phase, double strike) -- proven against the fully-branched MTG_AURA_SWAP_BRANCH control arm (Bruna ledger); human play offers every legal Aura. DeckUsesSecondMain detects aura_swap_cost (a combat swap returns Wings to hand -> main-2 recast). The opponent's-turn window is collapsed onto our combat/main-2 windows (equivalent vs a passive opponent) -- user-approved 2026-10-05. Flying grant inert (no blockers; nothing in Bruna reads Flying) -- user-approved 2026-10-05.
- coverage deferral -- Botanical Sanctum: Fastland: fastland_max_other_lands 2 -- LandWouldEnterTapped counts the lands the active player controls (the land being played is still in hand, so each counts as 'other') and it enters tapped iff that count > 2. Same predicate on the executor drop, the rollout drop and the enumerator's priced land copy. Fully modelled (exact sibling of Razorverge Thicket / Spirebluff Canal / Blooming Marsh).
- coverage deferral -- Boseiju, Who Endures: {T}: Add {G} modelled (basic_land, produces G). Legendary supertype carried so EnforceLegendRule applies (deck runs 1 copy; not reachable by Glittering Wish, which fetches only multicolored cards). CHANNEL [PARTIAL -- goldfish deferral, user-approved 2026-10-05
- coverage deferral -- Mother of Runes: Activated protection NOT modelled -- goldfish-inert deferral, user-approved 2026-10-05. Activating is weakly DOMINATED by not activating on every DEBT axis here: Damage -- nothing deals damage to our creatures (passive opponent; deck has no self-damage; Worldfire is wish-unreachable). Enchant/Equip -- protection from colour C can only HURT us: our C-coloured Auras fall off (SBA 704.5m) and can no longer be cast on / attached to it, including via Bruna's attack trigger ('Aura cards that could enchant it'); colourless Lightning Greaves and colourless Eldrazi Conscription are unaffected. Knocking an Aura into the graveyard so Bruna can return it is never better than leaving it on the battlefield, because Bruna's trigger already attaches ANY NUMBER of battlefield Auras to her at no cost; no Aura in the list has a beneficial ETB (Colossification's ETB TAPS the enchanted creature) or a leaves/dies trigger; and no Aura ever lowers power (Almost Perfect sets base 9/10, above every creature's base). Block -- the opponent never blocks (Indrik Umbra's lure has nothing to force). Targeting -- the opponent never targets; only our own targeting is affected, again harmfully. The activation also costs Mother's tap, i.e. her 1-power attack. Choosing a colour no object of ours has makes it an exact no-op, so 'never activate' loses nothing.
- coverage deferral -- Open the Armory: tutor_to_hand + tutor_types [Aura, Equipment
- coverage deferral -- Wild Growth: Land Aura: the bonus rides the enchanted land's tap, in the aura's own colour, at both the projection and the real tap. WHICH land to enchant is a searched plan variant per legal host.
- coverage deferral -- Indrik Umbra: +4/+4 modelled (aura_power_bonus/aura_tough_bonus via AuraBonusFor). First strike inert: the passive opponent never blocks, so damage-step ordering never changes damage dealt. Lure clause ('all creatures able to block it do so') inert: it binds the OPPONENT's blockers only, and the goldfish opponent has no creatures and never blocks — it imposes nothing on us. Umbra armor inert: nothing in this deck or from the passive opponent destroys our creatures (Worldfire is not Wish-reachable; Boseiju targets opponent permanents only).
- coverage deferral -- Almost Perfect: Base P/T SET (CR 613.4b, layer 7b) via aura_set_base_power/aura_set_base_toughness: AuraBonusFor returns the delta 9 - (printed + CDA base) so every +N/+N aura, counter, temp pump and lord anthem (layer 7c) still stacks on top -- e.g. Avacyn's Pilgrim + AP + Eldrazi Conscription = 19/20. Host = SEARCHED enchant_target, own creatures only (enchanting a goldfish spawn is strictly dominated: it never attacks or blocks). Deferral (user-approved 2026-10-05): indestructible not modelled -- inert, nothing in this deck or the passive opponent destroys our creatures (Boseiju hits only opponent artifacts/enchantments/nonbasic lands; sacrifice and 0-toughness SBA ignore indestructible anyway). Toughness 10 likewise inert (no damage is ever dealt to our creatures).
- coverage deferral -- Unflinching Courage: +2/+2 modelled (aura_power_bonus/aura_tough_bonus via AuraBonusFor; toughness stored, inert vs the passive opponent). Lifelink modelled (aura_grants_lifelink -> CreatureHasLifelink; our life total is not read by any card in this deck, so it is faithful but outcome-inert). Trample inert: the goldfish opponent never blocks, so all combat damage reaches the player regardless (precedent Rancor / Armadillo Cloak / Audacity).
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled 
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is 
- allowlisted divergence -- Auriok Champion [keywords]: 'Protection from black and from red' is a real keyword but inert in goldfishing on all four DEBT axes (the passive opponent never damages, targets, bl
- allowlisted divergence -- Brightcap Badger [mana_cost]: ADVENTURE CARD (CR 715), authored as TWO entries -- 'Brightcap Badger' (the {3}{G} 3/4 creature face) and 'Fungus Frolic' (the {2}{G} instant half). S
- allowlisted divergence -- Fungus Frolic [mana_cost]: The ADVENTURE HALF of Brightcap Badger // Fungus Frolic -- see the Brightcap Badger entry. Scryfall's combined mana_cost is '{3}{G} // {2}{G}'; the in
- allowlisted divergence -- Fungus Frolic [P/T]: An Instant has no power or toughness. Scryfall reports 3/4 because it describes the COMBINED card, whose creature face is the 3/4 Brightcap Badger. Gi
- allowlisted divergence -- Mycoloth [keywords]: 'Devour 2' is a real keyword and is FULLY MODELLED -- but via the `devour` CardParam, not via Keyword::Devour. The engine's Keyword enum deliberately 
- allowlisted divergence -- Brutal Cathar [keywords]: Transforming DFC (Brutal Cathar // Moonrage Brute): Scryfall reports CARD-LEVEL keywords for both faces. The front face's only keyword is Daybound, mo
- allowlisted divergence -- Moonrage Brute [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the front's Daybound and the layout's Transform; Nightbound is modelled structurall
- STALE allowlist entry -- Glorybringer [keywords] no longer mismatches; remove from scryfall_divergences.json
- oracle_text advisory -- Gideon, Ally of Zendikar: oracle_text diverges (similarity 0.10); scryfall='+1: Until end of turn, Gideon becomes a 5/5 Human Soldier Ally creature with indestructible that\'s still a pl
- oracle_text advisory -- Basri, Tomorrow's Champion: oracle_text diverges (similarity 0.15); scryfall="{W}, {T}, Exert Basri: Create a 1/1 white Cat creature token with lifelink. (An exerted creature won't untap d
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.56); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.40); scryfall="As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.20); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.27); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.\n{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.71); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.\n{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.\n{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.\nYou may choose not to untap this land during your untap step.\nAt the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.\n{T}: Put a storage counter on this land.\n{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)\nThis land enters tapped.\nWhen this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.\n{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.\nSacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.45); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may 
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.41); scryfall='First strike (This creature deals combat damage before creatures without first strike.)\nOther Knight creature
- oracle_text advisory -- Valiant Knight: oracle_text diverges (similarity 0.18); scryfall='Other Knights you control get +1/+1.\n{3}{W}{W}: Knights you control gain double strike until end of turn.'
- oracle_text advisory -- Kinsbaile Cavalier: oracle_text diverges (similarity 0.06); scryfall='Knight creatures you control have double strike.'
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.\n{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins\nOther Knights you control get +2/+2 and have protection from Assassins.\nWhen Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance\nAdeline's power is equal to the number of creatures you control.\nWhenever you attack, for each opp
- oracle_text advisory -- Accorder Paladin: oracle_text diverges (similarity 0.33); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)'
- oracle_text advisory -- Silverblade Paladin: oracle_text diverges (similarity 0.25); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Hero of Bladehold: oracle_text diverges (similarity 0.50); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)\nWhen
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)\n{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying\nRot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.\nWhenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.17); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.\nTarget
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.\n
- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.43); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.55); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.\nWhenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.11); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.32); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.\nDraw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.58); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.\nFerocious — If you control a creature with power 4 or greater, those creatures don't 
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample\nSpells you cast cost {1} less to cast for each target.\nSpells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {U}{
- oracle_text advisory -- Orzhov Basilica: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.28); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of 
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —\n• Tap X target permanents.\n• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying\n{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.10); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.05); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.\nSplice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste\nOther creatures you control have haste.\nWhenever an opponent casts a creature or planeswalker 
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste\nWhen Karrthus enters, gain control of all Dragons, then untap all Dragons.\nOther Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.25); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying\nWhenever this creature or another Dragon you control enters, it deals X damage to any target, where X 
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying\nWhenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.\nStorm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.\nIf this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.\nWhenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.60); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample.\nWhen this Aura is put into a graveyard from 
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.58); scryfall='Enchant creature with another Aura attached to it\nEnchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample.\nWhenever enchanted creature deals damage, yo
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)\nUmbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature\nEnchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature\nEnchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature\nEnchanted creature gets +1/+0 and has flying.\n{3}{W}: Return this card from your graveyard 
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)\nWhenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.77); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)\nEnchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Darkbore Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {B}.'
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)\nOther Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.\nGoblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.\n{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.\n{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.\nWhenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.08); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.40); scryfall='First strike\nWhen this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach\nWhen this creature enters, it deals 2 damage to any target.\nChannel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Three Tree City: oracle_text diverges (similarity 0.47); scryfall='As Three Tree City enters, choose a creature type.\n{T}: Add {C}.\n{2}, {T}: Choose a color. Add an amount of 
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.\nWhen this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.\nWhenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.\nWhenever a creature 
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.15); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.24); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.19); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.\n{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment, 
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.\nSearch your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.28); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.29); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.47); scryfall='Flying, first strike, trample, lifelink, haste\nAt the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste\nWhenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything\nIf Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance\nThis creature gets +1/+1 for each color among permanents you control.\n{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)\n{B}, 
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.18); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)\nEquip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.\n−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.\n−2: Gain control of target creature.\n−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")\n+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.39); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.23); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying\n{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.46); scryfall="Choose five. You may choose the same mode more than once.\n• Target permanent phases out.\n• Target player dra
- oracle_text advisory -- Disenchant: oracle_text diverges (similarity 0.21); scryfall='Destroy target artifact or enchantment.'
- oracle_text advisory -- Mirrorwing Dragon: oracle_text diverges (similarity 0.42); scryfall='Flying\nWhenever a player casts an instant or sorcery spell that targets only this creature, that player copie
- oracle_text advisory -- Zada, Hedron Grinder: oracle_text diverges (similarity 0.57); scryfall='Whenever you cast an instant or sorcery spell that targets only Zada, copy that spell for each other creature 
- oracle_text advisory -- Goblin Instigator: oracle_text diverges (similarity 0.32); scryfall='When this creature enters, create a 1/1 red Goblin creature token.'
- oracle_text advisory -- Fists of Flame: oracle_text diverges (similarity 0.36); scryfall="Draw a card. Until end of turn, target creature gains trample and gets +1/+0 for each card you've drawn this t
- oracle_text advisory -- Luxurious Libation: oracle_text diverges (similarity 0.24); scryfall='Target creature gets +X/+X until end of turn. Create a 1/1 green and white Citizen creature token.'
- oracle_text advisory -- Fortifying Draught: oracle_text diverges (similarity 0.33); scryfall='You gain 2 life. Target creature gets +X/+X until end of turn, where X is the amount of life you gained this t
- oracle_text advisory -- Gold Rush: oracle_text diverges (similarity 0.36); scryfall='Create a Treasure token. Until end of turn, up to one target creature gets +2/+2 for each Treasure you control
- oracle_text advisory -- Ancestral Anger: oracle_text diverges (similarity 0.52); scryfall='Target creature gains trample and gets +X/+0 until end of turn, where X is 1 plus the number of cards named An
- oracle_text advisory -- Oracle's Restoration: oracle_text diverges (similarity 0.10); scryfall='Target creature you control gets +1/+1 until end of turn. You draw a card and gain 1 life.'
- oracle_text advisory -- Expedite: oracle_text diverges (similarity 0.29); scryfall='Target creature gains haste until end of turn.\nDraw a card.'
- oracle_text advisory -- Impolite Entrance: oracle_text diverges (similarity 0.18); scryfall='Target creature gains trample and haste until end of turn.\nDraw a card.'
- oracle_text advisory -- Scale the Heights: oracle_text diverges (similarity 0.49); scryfall='Put a +1/+1 counter on up to one target creature. You gain 2 life. You may play an additional land this turn.\
- oracle_text advisory -- Twinflame: oracle_text diverges (similarity 0.42); scryfall="Strive — This spell costs {2}{R} more to cast for each target beyond the first.\nChoose any number of target c
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.43); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.\nWhen this land enters, you gain 1 life.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.\nEquip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.\nEquip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.\n{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.\nWhenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.\nEquipped creature gets +3/+0 and h
- oracle_text advisory -- Ancient Den: oracle_text diverges (similarity 0.02); scryfall='{T}: Add {W}.'
- oracle_text advisory -- Bone Saw: oracle_text diverges (similarity 0.29); scryfall='Equipped creature gets +1/+0.\nEquip {1} ({1}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Cathar's Shield: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +0/+3 and has vigilance.\nEquip {3} ({3}: Attach to target creature you control. Equip 
- oracle_text advisory -- Accorder's Shield: oracle_text diverges (similarity 0.23); scryfall="Equipped creature gets +0/+3 and has vigilance. (Attacking doesn't cause it to tap.)\nEquip {3} ({3}: Attach t
- oracle_text advisory -- Kite Shield: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +0/+3.\nEquip {3} ({3}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Spidersilk Net: oracle_text diverges (similarity 0.22); scryfall='Equipped creature gets +0/+2 and has reach. (It can block creatures with flying.)\nEquip {2} ({2}: Attach to t
- oracle_text advisory -- Golem-Skin Gauntlets: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+0 for each Equipment attached to it.\nEquip {2} ({2}: Attach to target creature you
- oracle_text advisory -- Skateboard: oracle_text diverges (similarity 0.19); scryfall='When this Equipment enters, tap target permanent.\nEquipped creature gets +1/+0 and has haste.\nEquip {1} ({1}
- oracle_text advisory -- Dragonfire Blade: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +2/+2 and has hexproof from monocolored.\nEquip {4}. This ability costs {1} less to act
- oracle_text advisory -- Deconstruction Hammer: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+1 and has "{3}, {T}, Sacrifice Deconstruction Hammer: Destroy target artifact or en
- oracle_text advisory -- Sram, Senior Edificer: oracle_text diverges (similarity 0.08); scryfall='Whenever you cast an Aura, Equipment, or Vehicle spell, draw a card.'
- oracle_text advisory -- Cid, Freeflier Pilot: oracle_text diverges (similarity 0.13); scryfall='Equipment and Vehicle spells you cast cost {1} less to cast.\nJump — During your turn, Cid has flying.\n{2}, {
- oracle_text advisory -- Sigarda's Aid: oracle_text diverges (similarity 0.14); scryfall='You may cast Aura and Equipment spells as though they had flash.\nWhenever an Equipment you control enters, yo
- oracle_text advisory -- Dwalin, Weaponmaster: oracle_text diverges (similarity 0.12); scryfall='First strike\nWhenever Dwalin enters or attacks, put a hone counter on each Equipment you control. (Each hone 
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.\nRemove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.49); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.\nMetalcraft — Equipment you control have equip 
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.37); scryfall='First strike\nBalan has double strike as long as two or more Equipment are attached to it.\n{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying\nWhenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.\n{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.12); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.39); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
- oracle_text advisory -- Mirri's Guile: oracle_text diverges (similarity 0.44); scryfall='At the beginning of your upkeep, you may look at the top three cards of your library, then put them back in an
- oracle_text advisory -- Call of the Wild: oracle_text diverges (similarity 0.52); scryfall="{2}{G}{G}: Reveal the top card of your library. If it's a creature card, put it onto the battlefield. Otherwis
- oracle_text advisory -- Hornet Queen: oracle_text diverges (similarity 0.41); scryfall='Flying, deathtouch\nWhen this creature enters, create four 1/1 green Insect creature tokens with flying and de
- oracle_text advisory -- Terastodon: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may destroy up to three target noncreature permanents. For each permanent put i
- oracle_text advisory -- Elderscale Wurm: oracle_text diverges (similarity 0.53); scryfall='Trample\nWhen this creature enters, if your life total is less than 7, your life total becomes 7.\nAs long as 
- oracle_text advisory -- Craterhoof Behemoth: oracle_text diverges (similarity 0.44); scryfall='Haste\nWhen this creature enters, creatures you control gain trample and get +X/+X until end of turn, where X 
- oracle_text advisory -- Worldspine Wurm: oracle_text diverges (similarity 0.39); scryfall="Trample\nWhen this creature dies, create three 5/5 green Wurm creature tokens with trample.\nWhen Worldspine W
- oracle_text advisory -- Vaultborn Tyrant: oracle_text diverges (similarity 0.47); scryfall="Trample\nWhenever this creature or another creature you control with power 4 or greater enters, you gain 3 lif
- oracle_text advisory -- Natural Order: oracle_text diverges (similarity 0.38); scryfall='As an additional cost to cast this spell, sacrifice a green creature.\nSearch your library for a green creatur
- oracle_text advisory -- Turntimber Symbiosis: oracle_text diverges (similarity 0.45); scryfall='Look at the top seven cards of your library. You may put a creature card from among them onto the battlefield.
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.24); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.\n{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.17); scryfall='First strike\n{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.\n{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.\nWhenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.35); scryfall='First strike\nOther Minotaurs you control have first strike.\nAs long as you have one or fewer cards in hand, 
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample\nOther Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.\n{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.28); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.\n{2}, Sacrifice another creatu
- oracle_text advisory -- Atsushi, the Blazing Sky: oracle_text diverges (similarity 0.43); scryfall='Flying, trample\nWhen Atsushi dies, choose one —\n• Exile the top two cards of your library. Until the end of 
- oracle_text advisory -- Inferno of the Star Mounts: oracle_text diverges (similarity 0.35); scryfall="This spell can't be countered.\nFlying, haste\n{R}: Inferno of the Star Mounts gets +1/+0 until end of turn. W
- oracle_text advisory -- Dragon Tempest: oracle_text diverges (similarity 0.34); scryfall='Whenever a creature you control with flying enters, it gains haste until end of turn.\nWhenever a Dragon you c
- oracle_text advisory -- Urza's Incubator: oracle_text diverges (similarity 0.15); scryfall='As this artifact enters, choose a creature type.\nCreature spells of the chosen type cost {2} less to cast.'
- oracle_text advisory -- Mind Stone: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.\n{1}, {T}, Sacrifice this artifact: Draw a card.'
- oracle_text advisory -- Fire Diamond: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.\n{T}: Add {R}.'
- oracle_text advisory -- Dragonspeaker Shaman: oracle_text diverges (similarity 0.15); scryfall='Dragon spells you cast cost {2} less to cast.'
- oracle_text advisory -- Glorybringer: oracle_text diverges (similarity 0.46); scryfall="Flying, haste\nYou may exert this creature as it attacks. When you do, it deals 4 damage to target non-Dragon 
- oracle_text advisory -- Haven of the Spirit Dragon: oracle_text diverges (similarity 0.32); scryfall='{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana only to cast a Dragon creature spell.\n{2}, {T}
- oracle_text advisory -- Nest Invader: oracle_text diverges (similarity 0.27); scryfall='When this creature enters, create a 0/1 colorless Eldrazi Spawn creature token. It has "Sacrifice this token: 
- oracle_text advisory -- Young Pyromancer: oracle_text diverges (similarity 0.28); scryfall='Whenever you cast an instant or sorcery spell, create a 1/1 red Elemental creature token.'
- oracle_text advisory -- Undercellar Myconid: oracle_text diverges (similarity 0.39); scryfall='Whenever this creature enters or dies, create a 1/1 green Saproling creature token.\n{T}: Add one mana of any 
- oracle_text advisory -- Frontline Heroism: oracle_text diverges (similarity 0.39); scryfall='When this enchantment enters, create a 1/1 red Soldier creature token with haste.\nWhenever you cast a spell t
- oracle_text advisory -- Adarkar Wastes: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.\n{T}: Add {W} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Caves of Koilos: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {W} or {B}. This land deals 1 damage to you.'
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Llanowar Wastes: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {B} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.\n{T}: Add {G} or {W}.\n{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
- oracle_text advisory -- Mariposa Military Base: oracle_text diverges (similarity 0.26); scryfall='You may have this land enter tapped. If you do, you get two rad counters.\n{T}: Add {C}.\n{5}, {T}: Draw a car
- oracle_text advisory -- Eldrazi Displacer: oracle_text diverges (similarity 0.47); scryfall="Devoid (This card has no color.)\n{2}{C}: Exile another target creature, then return it to the battlefield tap
- oracle_text advisory -- Emiel the Blessed: oracle_text diverges (similarity 0.48); scryfall="{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.\n
- oracle_text advisory -- Cloud of Faeries: oracle_text diverges (similarity 0.25); scryfall='Flying\nWhen this creature enters, untap up to two lands.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Peregrine Drake: oracle_text diverges (similarity 0.22); scryfall='Flying\nWhen this creature enters, untap up to five lands.'
- oracle_text advisory -- Wild Growth: oracle_text diverges (similarity 0.50); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land\nEnchanted land has shroud. (It can't be the target of spells or abilities.)\nWhenever enchanted 
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.22); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.06); scryfall="This spell can't be countered.\n{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)\n{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)\nFlash\nFlying\n{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.08); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).\n{T}: Add {C}.\n{T}, Pay {E}: Add one mana of any colo
- oracle_text advisory -- Maelstrom Wanderer: oracle_text diverges (similarity 0.34); scryfall='Creatures you control have haste.\nCascade, cascade (When you cast this spell, exile cards from the top of you
- oracle_text advisory -- Annoyed Altisaur: oracle_text diverges (similarity 0.50); scryfall='Reach, trample\nCascade (When you cast this spell, exile cards from the top of your library until you exile a 
- oracle_text advisory -- Sakashima's Protege: oracle_text diverges (similarity 0.28); scryfall='Flash\nCascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Boarding Party: oracle_text diverges (similarity 0.62); scryfall='Haste\nCascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Breaching Dragonstorm: oracle_text diverges (similarity 0.26); scryfall="When this enchantment enters, exile cards from the top of your library until you exile a nonland card. You may
- oracle_text advisory -- Call Forth the Tempest: oracle_text diverges (similarity 0.40); scryfall="Cascade, cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland
- oracle_text advisory -- Creative Technique: oracle_text diverges (similarity 0.33); scryfall='Demonstrate (When you cast this spell, you may copy it. If you do, choose an opponent to also copy it.)\nShuff
- oracle_text advisory -- Dwarven Ruins: oracle_text diverges (similarity 0.09); scryfall='This land enters tapped.\n{T}: Add {R}.\n{T}, Sacrifice this land: Add {R}{R}.'
- oracle_text advisory -- Svyelunite Temple: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {U}.\n{T}, Sacrifice this land: Add {U}{U}.'
- oracle_text advisory -- Hickory Woodlot: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped with two depletion counters on it.\n{T}, Remove a depletion counter from this land: Ad
- oracle_text advisory -- Melira, Sylvok Outcast: oracle_text diverges (similarity 0.34); scryfall="You can't get poison counters.\nCreatures you control can't have -1/-1 counters put on them.\nCreatures your o
- oracle_text advisory -- Vizier of Remedies: oracle_text diverges (similarity 0.55); scryfall='If one or more -1/-1 counters would be put on a creature you control, that many -1/-1 counters minus one are p
- oracle_text advisory -- Kitchen Finks: oracle_text diverges (similarity 0.44); scryfall="When this creature enters, you gain 2 life.\nPersist (When this creature dies, if it had no -1/-1 counters on 
- oracle_text advisory -- Murderous Redcap: oracle_text diverges (similarity 0.51); scryfall="When this creature enters, it deals damage equal to its power to any target.\nPersist (When this creature dies
- oracle_text advisory -- Carrion Feeder: oracle_text diverges (similarity 0.28); scryfall="This creature can't block.\nSacrifice a creature: Put a +1/+1 counter on this creature."
- oracle_text advisory -- Bloodthrone Vampire: oracle_text diverges (similarity 0.26); scryfall='Sacrifice a creature: This creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Recruiter of the Guard: oracle_text diverges (similarity 0.35); scryfall='When this creature enters, you may search your library for a creature card with toughness 2 or less, reveal it
- oracle_text advisory -- Ranger of Eos: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, you may search your library for up to two creature cards with mana value 1 or less,
- oracle_text advisory -- Severance Priest: oracle_text diverges (similarity 0.40); scryfall="Deathtouch\nWhen this creature enters, target opponent reveals their hand. You may choose a nonland card from 
- oracle_text advisory -- Birthing Pod: oracle_text diverges (similarity 0.25); scryfall="({G/P} can be paid with either {G} or 2 life.)\n{1}{G/P}, {T}, Sacrifice a creature: Search your library for a
- oracle_text advisory -- Chord of Calling: oracle_text diverges (similarity 0.25); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Reveillark: oracle_text diverges (similarity 0.22); scryfall="Flying\nWhen this creature leaves the battlefield, return up to two target creature cards with power 2 or less
- oracle_text advisory -- Felidar Guardian: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may exile another target permanent you control, then return that card to the ba
- oracle_text advisory -- Voice of Resurgence: oracle_text diverges (similarity 0.32); scryfall='Whenever an opponent casts a spell during your turn and when this creature dies, create a green and white Elem
- oracle_text advisory -- Scavenging Ooze: oracle_text diverges (similarity 0.23); scryfall='{G}: Exile target card from a graveyard. If it was a creature card, put a +1/+1 counter on this creature and y
- oracle_text advisory -- Ravenous Chupacabra: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, destroy target creature an opponent controls.'
- oracle_text advisory -- Reclamation Sage: oracle_text diverges (similarity 0.16); scryfall='When this creature enters, you may destroy target artifact or enchantment.'
- oracle_text advisory -- Celes, Rune Knight: oracle_text diverges (similarity 0.25); scryfall='When Celes enters, discard any number of cards, then draw that many cards plus one.\nWhenever one or more othe
- oracle_text advisory -- Drifting Meadow: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {W}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Polluted Mire: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {B}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Smoldering Crater: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {R}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Blasted Landscape: oracle_text diverges (similarity 0.30); scryfall='{T}: Add {C}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fetid Pools: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {U} or {B}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Irrigated Farmland: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {W} or {U}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Canyon Slough: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {R}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Sheltered Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {G}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Scattered Groves: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {G} or {W}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Glittering Massif: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {W}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Festering Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {G}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Capital City: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{1}, {T}: Add one mana of any color.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Forsake the Worldly: oracle_text diverges (similarity 0.22); scryfall='Exile target artifact or enchantment.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fluctuator: oracle_text diverges (similarity 0.17); scryfall='Cycling abilities you activate cost {2} less to activate.'
- oracle_text advisory -- Drannith Stinger: oracle_text diverges (similarity 0.26); scryfall='Whenever you cycle another card, this creature deals 1 damage to each opponent.\nCycling {1} ({1}, Discard thi
- oracle_text advisory -- Hollow One: oracle_text diverges (similarity 0.20); scryfall="This spell costs {2} less to cast for each card you've cycled or discarded this turn.\nCycling {2} ({2}, Disca
- oracle_text advisory -- Unearth: oracle_text diverges (similarity 0.24); scryfall='Return target creature card with mana value 3 or less from your graveyard to the battlefield.\nCycling {2} ({2
- oracle_text advisory -- Scrying Sheets: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{1}{S}, {T}: Look at the top card of your library. If that card is snow, you may reveal it and 
- oracle_text advisory -- Skred: oracle_text diverges (similarity 0.23); scryfall='Skred deals damage to target creature equal to the number of snow permanents you control.'
- oracle_text advisory -- Coldsteel Heart: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.\nAs this artifact enters, choose a color.\n{T}: Add one mana of the chosen color.
- oracle_text advisory -- Abominable Treefolk: oracle_text diverges (similarity 0.35); scryfall="Trample\nAbominable Treefolk's power and toughness are each equal to the number of snow permanents you control
- oracle_text advisory -- Arcum's Astrolabe: oracle_text diverges (similarity 0.25); scryfall='({S} can be paid with one mana from a snow source.)\nWhen this artifact enters, draw a card.\n{1}, {T}: Add on
- oracle_text advisory -- Frost Augur: oracle_text diverges (similarity 0.39); scryfall="{S}, {T}: Look at the top card of your library. If it's a snow card, you may reveal it and put it into your ha
- oracle_text advisory -- Ice-Fang Coatl: oracle_text diverges (similarity 0.21); scryfall='Flash\nFlying\nWhen this creature enters, draw a card.\nThis creature has deathtouch as long as you control at
- oracle_text advisory -- Jorn, God of Winter: oracle_text diverges (similarity 0.10); scryfall='Whenever Jorn attacks, untap each snow permanent you control.'
- oracle_text advisory -- Kaldring, the Rimestaff: oracle_text diverges (similarity 0.17); scryfall='{T}: You may play target snow permanent card from your graveyard this turn. If you do, it enters tapped.'
- oracle_text advisory -- Marit Lage's Slumber: oracle_text diverges (similarity 0.35); scryfall="Whenever Marit Lage's Slumber or another snow permanent you control enters, scry 1.\nAt the beginning of your 
- oracle_text advisory -- Rimefeather Owl: oracle_text diverges (similarity 0.26); scryfall="Flying\nRimefeather Owl's power and toughness are each equal to the number of snow permanents on the battlefie
- oracle_text advisory -- Rimescale Dragon: oracle_text diverges (similarity 0.29); scryfall="Flying\n{2}{S}: Tap target creature and put an ice counter on it. ({S} can be paid with one mana from a snow s
- oracle_text advisory -- Soul's Attendant: oracle_text diverges (similarity 0.12); scryfall='Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Auriok Champion: oracle_text diverges (similarity 0.12); scryfall='Protection from black and from red\nWhenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Ocelot Pride: oracle_text diverges (similarity 0.13); scryfall="First strike, lifelink\nAscend (If you control ten or more permanents, you get the city's blessing for the res
- oracle_text advisory -- Serra Ascendant: oracle_text diverges (similarity 0.21); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)\nAs long as you have 30 or mo
- oracle_text advisory -- Ajani's Pridemate: oracle_text diverges (similarity 0.09); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Voice of the Blessed: oracle_text diverges (similarity 0.26); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.\nAs long as this creature has four or more +1/+1
- oracle_text advisory -- Daxos, Blessed by the Sun: oracle_text diverges (similarity 0.18); scryfall="Daxos's toughness is equal to your devotion to white. (Each {W} in the mana costs of permanents you control co
- oracle_text advisory -- Archangel of Thune: oracle_text diverges (similarity 0.24); scryfall='Flying\nLifelink (Damage dealt by this creature also causes you to gain that much life.)\nWhenever you gain li
- oracle_text advisory -- Heliod, Sun-Crowned: oracle_text diverges (similarity 0.13); scryfall="Indestructible\nAs long as your devotion to white is less than five, Heliod isn't a creature.\nWhenever you ga
- oracle_text advisory -- Ranger-Captain of Eos: oracle_text diverges (similarity 0.13); scryfall="When this creature enters, you may search your library for a creature card with mana value 1 or less, reveal i
- oracle_text advisory -- Ajani, Strength of the Pride: oracle_text diverges (similarity 0.27); scryfall='+1: You gain life equal to the number of creatures you control plus the number of planeswalkers you control.\n
- oracle_text advisory -- Apex Altisaur: oracle_text diverges (similarity 0.21); scryfall="When this creature enters, it fights up to one target creature you don't control.\nEnrage — Whenever this crea
- oracle_text advisory -- World War Hulk: oracle_text diverges (similarity 0.17); scryfall='(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)\nI — The next red or 
- oracle_text advisory -- Ghalta, Stampede Tyrant: oracle_text diverges (similarity 0.17); scryfall='Trample\nWhen Ghalta enters, put any number of creature cards from your hand onto the battlefield.'
- oracle_text advisory -- Lyra Dawnbringer: oracle_text diverges (similarity 0.13); scryfall='Flying\nFirst strike (This creature deals combat damage before creatures without first strike.)\nLifelink (Dam
- oracle_text advisory -- Youthful Valkyrie: oracle_text diverges (similarity 0.08); scryfall='Flying\nWhenever another Angel you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Bishop of Wings: oracle_text diverges (similarity 0.11); scryfall='Whenever an Angel you control enters, you gain 4 life.\nWhenever an Angel you control dies, create a 1/1 white
- oracle_text advisory -- Righteous Valkyrie: oracle_text diverges (similarity 0.15); scryfall="Flying\nWhenever another Angel or Cleric you control enters, you gain life equal to that creature's toughness.
- oracle_text advisory -- Seraph Sanctuary: oracle_text diverges (similarity 0.13); scryfall='When this land enters, you gain 1 life.\nWhenever an Angel you control enters, you gain 1 life.\n{T}: Add {C}.
- oracle_text advisory -- Resplendent Angel: oracle_text diverges (similarity 0.11); scryfall='Flying\nAt the beginning of each end step, if you gained 5 or more life this turn, create a 4/4 white Angel cr
- oracle_text advisory -- Legion Angel: oracle_text diverges (similarity 0.07); scryfall='Flying\nWhen this creature enters, you may reveal a card you own named Legion Angel from outside the game and 
- oracle_text advisory -- Serra the Benevolent: oracle_text diverges (similarity 0.16); scryfall='+2: Creatures you control with flying get +1/+1 until end of turn.\n−3: Create a 4/4 white Angel creature toke
- oracle_text advisory -- Giada, Font of Hope: oracle_text diverges (similarity 0.13); scryfall='Flying, vigilance\nEach other Angel you control enters with an additional +1/+1 counter on it for each Angel y
- oracle_text advisory -- Saproling Burst: oracle_text diverges (similarity 0.13); scryfall='Fading 7 (This enchantment enters with seven fade counters on it. At the beginning of your upkeep, remove a fa
- oracle_text advisory -- Shroofus Sproutsire: oracle_text diverges (similarity 0.06); scryfall='Trample\nWhenever a Saproling you control deals combat damage to a player, create that many 1/1 green Saprolin
- oracle_text advisory -- Slimefoot, the Stowaway: oracle_text diverges (similarity 0.05); scryfall='Whenever a Saproling you control dies, Slimefoot deals 1 damage to each opponent and you gain 1 life.\n{4}: Cr
- oracle_text advisory -- Vitaspore Thallid: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Deathspore Thallid: oracle_text diverges (similarity 0.10); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Sporecrown Thallid: oracle_text diverges (similarity 0.22); scryfall="Each other creature you control that's a Fungus or Saproling gets +1/+1."
- oracle_text advisory -- Tukatongue Thallid: oracle_text diverges (similarity 0.13); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Simic Growth Chamber: oracle_text diverges (similarity 0.21); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {G}{
- oracle_text advisory -- Psychotrope Thallid: oracle_text diverges (similarity 0.22); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Concordant Crossroads: oracle_text diverges (similarity 0.01); scryfall='All creatures have haste.'
- oracle_text advisory -- Brightcap Badger: oracle_text diverges (similarity 0.12); scryfall='Each Fungus and Saproling you control has "{T}: Add {G}."\nAt the beginning of your end step, create a 1/1 gre
- oracle_text advisory -- Fungus Frolic: oracle_text diverges (similarity 0.10); scryfall='Create two 1/1 green Saproling creature tokens. (Then exile this card. You may cast the creature later from ex
- oracle_text advisory -- Doubling Season: oracle_text diverges (similarity 0.28); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Beastmaster Ascension: oracle_text diverges (similarity 0.17); scryfall='Whenever a creature you control attacks, you may put a quest counter on this enchantment.\nAs long as this enc
- oracle_text advisory -- Thallid: oracle_text diverges (similarity 0.17); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Thallid Shell-Dweller: oracle_text diverges (similarity 0.13); scryfall='Defender\nAt the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters 
- oracle_text advisory -- Sporesower Thallid: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on each Fungus you control.\nRemove three spore counters 
- oracle_text advisory -- Utopia Mycon: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Mycoloth: oracle_text diverges (similarity 0.14); scryfall='Devour 2 (As this creature enters, you may sacrifice any number of creatures. It enters with twice that many +
- oracle_text advisory -- Lightstall Inquisitor: oracle_text diverges (similarity 0.17); scryfall='Vigilance\nWhen this creature enters, each opponent exiles a card from their hand and may play that card for a
- oracle_text advisory -- Lyra, Archangel of Dawn: oracle_text diverges (similarity 0.06); scryfall='Flying\nWhenever you gain life, put a +1/+1 counter on each Angel you control.'
- oracle_text advisory -- Sunrise Sovereign: oracle_text diverges (similarity 0.10); scryfall='Other Giant creatures you control get +2/+2 and have trample.'
- oracle_text advisory -- Stinkdrinker Daredevil: oracle_text diverges (similarity 0.11); scryfall='Giant spells you cast cost {2} less to cast.'
- oracle_text advisory -- Giant Harbinger: oracle_text diverges (similarity 0.14); scryfall='When this creature enters, you may search your library for a Giant card, reveal it, then shuffle and put that 
- oracle_text advisory -- Pyroclasm: oracle_text diverges (similarity 0.05); scryfall='Pyroclasm deals 2 damage to each creature.'
- oracle_text advisory -- Hamletback Goliath: oracle_text diverges (similarity 0.09); scryfall="Whenever another creature enters, you may put X +1/+1 counters on this creature, where X is that creature's po
- oracle_text advisory -- Borderland Behemoth: oracle_text diverges (similarity 0.06); scryfall='Trample\nThis creature gets +4/+4 for each other Giant you control.'
- oracle_text advisory -- Inferno Titan: oracle_text diverges (similarity 0.13); scryfall='{R}: This creature gets +1/+0 until end of turn.\nWhenever this creature enters or attacks, it deals 3 damage 
- oracle_text advisory -- Surtland Flinger: oracle_text diverges (similarity 0.18); scryfall="Whenever this creature attacks, you may sacrifice another creature. When you do, this creature deals damage eq
- oracle_text advisory -- Tectonic Giant: oracle_text diverges (similarity 0.16); scryfall='Whenever this creature attacks or becomes the target of a spell an opponent controls, choose one —\n• This cre
- oracle_text advisory -- Metallic Mimic: oracle_text diverges (similarity 0.18); scryfall='As this creature enters, choose a creature type.\nThis creature is the chosen type in addition to its other ty
- oracle_text advisory -- Adaptive Automaton: oracle_text diverges (similarity 0.23); scryfall='As this creature enters, choose a creature type.\nThis creature is the chosen type in addition to its other ty
- oracle_text advisory -- Corsair Captain: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, create a Treasure token. (It\'s an artifact with "{T}, Sacrifice this token: Add on
- oracle_text advisory -- Dire Fleet Captain: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature attacks, it gets +1/+1 until end of turn for each other attacking Pirate.'
- oracle_text advisory -- Goblin Tomb Raider: oracle_text diverges (similarity 0.09); scryfall='As long as you control an artifact, this creature gets +1/+0 and has haste.'
- oracle_text advisory -- Forerunner of the Coalition: oracle_text diverges (similarity 0.22); scryfall='When this creature enters, you may search your library for a Pirate card, reveal it, then shuffle and put that
- oracle_text advisory -- Staunch Crewmate: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, look at the top four cards of your library. You may reveal an artifact or Pirate ca
- oracle_text advisory -- Malcolm, the Eyes: oracle_text diverges (similarity 0.26); scryfall='Flying, haste\nWhenever you cast your second spell each turn, investigate. (Create a Clue token. It\'s an arti
- oracle_text advisory -- Siren Stormtamer: oracle_text diverges (similarity 0.16); scryfall='Flying\n{U}, Sacrifice this creature: Counter target spell or ability that targets you or a creature you contr
- oracle_text advisory -- Daring Buccaneer: oracle_text diverges (similarity 0.09); scryfall='As an additional cost to cast this spell, reveal a Pirate card from your hand or pay {2}.'
- oracle_text advisory -- Kitesail Larcenist: oracle_text diverges (similarity 0.17); scryfall='Flying, ward {1}\nWhen this creature enters, for each player, choose up to one other target artifact or creatu
- oracle_text advisory -- Spirebluff Canal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {U} or {R}.'
- oracle_text advisory -- Blackcleave Cliffs: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {B} or {R}.'
- oracle_text advisory -- Battlefield Forge: oracle_text diverges (similarity 0.18); scryfall='{T}: Add {C}.\n{T}: Add {R} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Karplusan Forest: oracle_text diverges (similarity 0.43); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Tarnished Citadel: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{T}: Add one mana of any color. This land deals 3 damage to you.'
- oracle_text advisory -- Grand Coliseum: oracle_text diverges (similarity 0.53); scryfall='This land enters tapped.\n{T}: Add {C}.\n{T}: Add one mana of any color. This land deals 1 damage to you.'
- oracle_text advisory -- Ancient Tomb: oracle_text diverges (similarity 0.17); scryfall='{T}: Add {C}{C}. This land deals 2 damage to you.'
- oracle_text advisory -- Manabarbs: oracle_text diverges (similarity 0.12); scryfall='Whenever a player taps a land for mana, this enchantment deals 1 damage to that player.'
- oracle_text advisory -- Tamanoa: oracle_text diverges (similarity 0.13); scryfall='Whenever a noncreature source you control deals damage, you gain that much life.'
- oracle_text advisory -- Rhox Faithmender: oracle_text diverges (similarity 0.37); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)\nIf you would gain life, you 
- oracle_text advisory -- Vito, Thorn of the Dusk Rose: oracle_text diverges (similarity 0.16); scryfall='Whenever you gain life, target opponent loses that much life.\n{3}{B}{B}: Creatures you control gain lifelink 
- oracle_text advisory -- Dina, Soul Steeper: oracle_text diverges (similarity 0.21); scryfall="Whenever you gain life, each opponent loses 1 life.\n{1}, Sacrifice another creature: Dina gets +X/+0 until en
- oracle_text advisory -- Bilbo, Birthday Celebrant: oracle_text diverges (similarity 0.24); scryfall='If you would gain life, you gain that much life plus 1 instead.\n{2}{W}{B}{G}, {T}, Exile Bilbo: Search your l
- oracle_text advisory -- Purity: oracle_text diverges (similarity 0.24); scryfall="Flying\nIf noncombat damage would be dealt to you, prevent that damage. You gain life equal to the damage prev
- oracle_text advisory -- Spellshock: oracle_text diverges (similarity 0.09); scryfall='Whenever a player casts a spell, this enchantment deals 2 damage to that player.'
- oracle_text advisory -- Pyrohemia: oracle_text diverges (similarity 0.11); scryfall='At the beginning of the end step, if no creatures are on the battlefield, sacrifice this enchantment.\n{R}: Th
- oracle_text advisory -- Rolling Earthquake: oracle_text diverges (similarity 0.08); scryfall='Rolling Earthquake deals X damage to each creature without horsemanship and each player.'
- oracle_text advisory -- Beseech the Queen: oracle_text diverges (similarity 0.20); scryfall="({2/B} can be paid with any two mana or with {B}. This card's mana value is 6.)\nSearch your library for a car
- oracle_text advisory -- Green Sun's Zenith: oracle_text diverges (similarity 0.17); scryfall="Search your library for a green creature card with mana value X or less, put it onto the battlefield, then shu
- oracle_text advisory -- Dimir House Guard: oracle_text diverges (similarity 0.25); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)\nSacrifice a creatu
- oracle_text advisory -- Shriekmaw: oracle_text diverges (similarity 0.26); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)\nWhen this creature
- oracle_text advisory -- Timeless Witness: oracle_text diverges (similarity 0.29); scryfall="When this creature enters, return target card from your graveyard to your hand.\nEternalize {5}{G}{G} ({5}{G}{
- oracle_text advisory -- Acidic Slime: oracle_text diverges (similarity 0.25); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)\nWhen this creature enters
- oracle_text advisory -- Ageless Entity: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, put that many +1/+1 counters on this creature.'
- oracle_text advisory -- Selesnya Sanctuary: oracle_text diverges (similarity 0.06); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {G}{
- oracle_text advisory -- Blossoming Sands: oracle_text diverges (similarity 0.06); scryfall='This land enters tapped.\nWhen this land enters, you gain 1 life.\n{T}: Add {G} or {W}.'
- oracle_text advisory -- Verdant Sun's Avatar: oracle_text diverges (similarity 0.05); scryfall="Whenever this creature or another creature you control enters, you gain life equal to that creature's toughnes
- oracle_text advisory -- Feed the Clan: oracle_text diverges (similarity 0.04); scryfall='You gain 5 life.\nFerocious — You gain 10 life instead if you control a creature with power 4 or greater.'
- oracle_text advisory -- Accomplished Alchemist: oracle_text diverges (similarity 0.04); scryfall='{T}: Add one mana of any color.\n{T}: Add X mana of any one color, where X is the amount of life you gained th
- oracle_text advisory -- Blighted Steppe: oracle_text diverges (similarity 0.04); scryfall='{T}: Add {C}.\n{3}{W}, {T}, Sacrifice this land: You gain 2 life for each creature you control.'
- oracle_text advisory -- Wellwisher: oracle_text diverges (similarity 0.02); scryfall='{T}: You gain 1 life for each Elf on the battlefield.'
- oracle_text advisory -- Nykthos Paragon: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, you may put that many +1/+1 counters on each creature you control. Do this only once e
- oracle_text advisory -- Blossoming Bogbeast: oracle_text diverges (similarity 0.05); scryfall='Whenever this creature attacks, you gain 2 life. Then creatures you control gain trample and get +X/+X until e
- oracle_text advisory -- Genesis Wave: oracle_text diverges (similarity 0.08); scryfall="Reveal the top X cards of your library. You may put any number of permanent cards with mana value X or less fr
- oracle_text advisory -- Champion of the Parish: oracle_text diverges (similarity 0.11); scryfall='Whenever another Human you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Field Marshal: oracle_text diverges (similarity 0.13); scryfall='Other Soldier creatures get +1/+1 and have first strike. (They deal combat damage before creatures without fir
- oracle_text advisory -- Thalia, Guardian of Thraben: oracle_text diverges (similarity 0.06); scryfall='First strike\nNoncreature spells cost {1} more to cast.'
- oracle_text advisory -- Thalia's Lieutenant: oracle_text diverges (similarity 0.12); scryfall='When this creature enters, put a +1/+1 counter on each other Human you control.\nWhenever another Human you co
- oracle_text advisory -- Rick, Steadfast Leader: oracle_text diverges (similarity 0.16); scryfall="As Greymond, Avacyn's Stalwart enters, choose two abilities from among first strike, vigilance, and lifelink.\
- oracle_text advisory -- Esper Sentinel: oracle_text diverges (similarity 0.14); scryfall="Whenever an opponent casts their first noncreature spell each turn, draw a card unless that player pays {X}, w
- oracle_text advisory -- Coppercoat Vanguard: oracle_text diverges (similarity 0.17); scryfall='Each other Human you control gets +1/+0 and has ward {1}. (Whenever it becomes the target of a spell or abilit
- oracle_text advisory -- Recruitment Officer: oracle_text diverges (similarity 0.17); scryfall='{3}{W}: Look at the top four cards of your library. You may reveal a creature card with mana value 3 or less f
- oracle_text advisory -- Jirina, Dauntless General: oracle_text diverges (similarity 0.09); scryfall="When Jirina enters, exile target player's graveyard.\nSacrifice Jirina: Humans you control gain hexproof and i
- oracle_text advisory -- Harbin, Vanguard Aviator: oracle_text diverges (similarity 0.08); scryfall='Flying\nWhenever you attack with five or more Soldiers, creatures you control get +1/+1 and gain flying until 
- oracle_text advisory -- General Kudro of Drannith: oracle_text diverges (similarity 0.19); scryfall="Other Humans you control get +1/+1.\nWhenever General Kudro or another Human you control enters, exile target 
- oracle_text advisory -- Cathar Commando: oracle_text diverges (similarity 0.06); scryfall='Flash\n{1}, Sacrifice this creature: Destroy target artifact or enchantment.'
- oracle_text advisory -- Brutal Cathar: oracle_text diverges (similarity 0.10); scryfall='Whenever this creature enters or transforms into Brutal Cathar, exile target creature an opponent controls unt
- oracle_text advisory -- Moonrage Brute: oracle_text diverges (similarity 0.25); scryfall='First strike\nWard—Pay 3 life.\nNightbound (If a player casts at least two spells during their own turn, it be
- oracle_text advisory -- King Darien XLVIII: oracle_text diverges (similarity 0.17); scryfall='Other creatures you control get +1/+1.\n{3}{G}{W}: Put a +1/+1 counter on King Darien and create a 1/1 white S
- oracle_text advisory -- Fortified Beachhead: oracle_text diverges (similarity 0.26); scryfall='As this land enters, you may reveal a Soldier card from your hand. This land enters tapped unless you revealed
- oracle_text advisory -- Silent Clearing: oracle_text diverges (similarity 0.10); scryfall='{T}, Pay 1 life: Add {W} or {B}.\n{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Almost Perfect: oracle_text diverges (similarity 0.20); scryfall='Enchant creature\nEnchanted creature has base power and toughness 9/10 and has indestructible.'
- oracle_text advisory -- Arcanum Wings: oracle_text diverges (similarity 0.15); scryfall='Enchant creature\nEnchanted creature has flying.\nAura swap {2}{U} ({2}{U}: Exchange this Aura with an Aura ca
- oracle_text advisory -- Boseiju, Who Endures: oracle_text diverges (similarity 0.38); scryfall='{T}: Add {G}.\nChannel — {1}{G}, Discard this card: Destroy target artifact, enchantment, or nonbasic land an 
- oracle_text advisory -- Botanical Sanctum: oracle_text diverges (similarity 0.29); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {G} or {U}.'
- oracle_text advisory -- Bruna, Light of Alabaster: oracle_text diverges (similarity 0.24); scryfall='Flying, vigilance\nWhenever Bruna attacks or blocks, you may attach to it any number of Auras on the battlefie
- oracle_text advisory -- Colossification: oracle_text diverges (similarity 0.14); scryfall='Enchant creature\nWhen this Aura enters, tap enchanted creature.\nEnchanted creature gets +20/+20.'
- oracle_text advisory -- Eldrazi Conscription: oracle_text diverges (similarity 0.39); scryfall='Enchant creature\nEnchanted creature gets +10/+10 and has trample and annihilator 2. (Whenever it attacks, def
- oracle_text advisory -- Glittering Wish: oracle_text diverges (similarity 0.07); scryfall='You may reveal a multicolored card you own from outside the game and put it into your hand. Exile Glittering W
- oracle_text advisory -- Indrik Umbra: oracle_text diverges (similarity 0.46); scryfall='Enchant creature\nEnchanted creature gets +4/+4 and has first strike, and all creatures able to block it do so
- oracle_text advisory -- Mother of Runes: oracle_text diverges (similarity 0.13); scryfall='{T}: Target creature you control gains protection from the color of your choice until end of turn.'
- oracle_text advisory -- Mythic Proportions: oracle_text diverges (similarity 0.40); scryfall='Enchant creature\nEnchanted creature gets +8/+8 and has trample.'
- oracle_text advisory -- Open the Armory: oracle_text diverges (similarity 0.21); scryfall='Search your library for an Aura or Equipment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Prodigious Growth: oracle_text diverges (similarity 0.26); scryfall='Enchant creature\nEnchanted creature gets +7/+7 and has trample.'
- oracle_text advisory -- Seaside Citadel: oracle_text diverges (similarity 0.15); scryfall='This land enters tapped.\n{T}: Add {G}, {W}, or {U}.'
- oracle_text advisory -- Skycloud Expanse: oracle_text diverges (similarity 0.06); scryfall='{1}, {T}: Add {W}{U}.'
- oracle_text advisory -- Somberwald Sage: oracle_text diverges (similarity 0.12); scryfall='{T}: Add three mana of any one color. Spend this mana only to cast creature spells.'
- oracle_text advisory -- Unflinching Courage: oracle_text diverges (similarity 0.43); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample and lifelink. (Damage dealt by the creature al
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- claude_sweep SKIPPED -- no '## Claude-play sweep' section in docs/design/analysis-Bruna.md. Run the Claude-driven sweep (.claude/skills/claude-play.md, analyze-deck 5d; fan game-indices out with the Workflow engine), verify any flags against cards.json + the rules skill, then record `commit:` / `seeds:` / `games:` / `flags: N unresolved` under that heading. play_invariants (above) already guards the protocol mechanically.

<!-- verify_deck:end -->

## Suite verdicts -- smoke + regression on the rebased branch (2026-10-05)

Full smoke + regression ran on the rebased branch (results kept, NOT re-run: `logs/suite_results_2026-10-05/`).
Two defects were found and fixed, and only the decks they move were re-run (`--deck=`, one invocation per
mode: `rerun_G_fix/` angels+angels2hg+bruna+bruna2hg, `rerun_fungusb_boundfix/` fungusb). The merged
per-tier result -- full run + per-deck overlays -- is `final/` and is staged in `test/results/<mode>.env`
+ `test/logs/<mode>/wins/` for the accept. Metric: loss-penalized avg turn-to-win (loss = 9). Recovery
check is two-stage: stage 1 `--depth <GT win turn> --budget-ms 100`, stage 2 `d8b0` only for stage-1
failures. d0 = greedy, light touch per the regression skill (attributed, not root-caused one by one).

### Fixes
* **8570c689 -- G follow-up (creature-only-first skips an attack-capable source).** 4e14d50c ranked every
  `creature_mana_only` source first on a creature payment ("never worse spent now"). False for a creature
  that could attack: Giada, Font of Hope (2/2 flying vigilance) paid Righteous Valkyrie pre-combat and sat
  out combat (smoke gi4 T3: 2 damage short, T4 -> T5). Angels: 146 slower regression games / +144 turns,
  all back to GT with the rank off. The rule was under-specified, not deck-specific: it now applies only
  when the tap costs no attack (`SacPayFodderCostsAttack`). Sage (0/1) and Ziggurat (a land) keep it, so
  Bruna 77019 is unchanged (unit test G passes; Bruna rows byte-identical to the full run). **Angels and
  angels2hg are now byte-identical to GT** (turns AND play digests, every case).
* **b5b47d54 -- the search's mana bound credits Brightcap Badger's grant on DEFINED Fungi.** fungusb
  s2002 gi40 (d3 + d5, T5 -> T6, still T6 at d8b0): `UntappedManaUpperBound` credited the grant only on
  token bodies, so `PaymentManaCovers` (search side only) "proved" Saproling Burst unpayable on T4 (bound
  4 vs real 7); the committed line was scored without Burst (attack 6, T5) while the executor cast it by
  tapping the attackers (attack 3, T6). Pre-existing since the Badger grant; exposed by 787fefdd (Wild
  Growth now resolves T2, so T4 can afford Burst). `MTG_PAY_BOUND=0` restored T5; with the fix gi40 = T5
  at d3b10, d5 play settings and d8b0. Inert without a mana grant (only fungusb carries the Badger).

* **A-iii follow-up -- the mana-unlock host is a SEARCHED widening only.** a80ebbb8 kept "Greaves -> best fresh mana dork" beside the attack host in every CollectActions call, including Solve's greedy (d0 + rollout leaves), which has no haste-dork mana credit -- there it could only be taken on its DMG haste eval. Gated on `g_search_candidate_enum` (human play keeps it; `MTG_UNLOCK_HOST_GREEDY=1` hatch). Unit test `test_bruna_sweep.cpp` "the greedy Solve does not haste a mana dork..." fails under the hatch. Fixes fivecolour d0 gi188/445/953 (below). **Staged results are stale for fivecolour, fivecolour2hg, bruna, bruna2hg (every case, every tier: d0 directly, d3/d5 through their rollouts) -- re-run those decks before the accept.** Verified unchanged: fivecolour d0 s1001/s2002 = GT digests; kitty/dragons/angels/giants d0 s1001/s2002 = GT digests.

### Per-deck net (final, vs committed GT)
| Tier | Deck | net turns | better | worse | play-changed |
|---|---|---|---|---|---|
| smoke | angels / angels2hg | 0 / 0 | 0 | 0 | 0 (byte-identical) |
| smoke | fungus | -13 | 20 | 6 (all d0) | 225 |
| smoke | fungusb | -14 | 15 | 2 (all d0) | 66 |
| smoke | fivecolour / fivecolour2hg | -4 / -1 | 5 / 1 | 1 (d0) / 0 | 20 / 3 |
| smoke | slivers | 0 | 0 | 0 | 2 |
| regression | angels / angels2hg | 0 / 0 | 0 | 0 | 0 (byte-identical) |
| regression | fungus | -26 | 25 | 2 (d0) | 280 |
| regression | fungusb | -21 | 23 | 5 (3 d0, 2 d3) | 127 |
| regression | fivecolour / fivecolour2hg | 0 / 0 | 3 / 0 | 3 (2 d0, 1 d3) / 0 | 41 / 2 |
| regression | slivers | 0 | 0 | 0 | 5 |
Every deck is net <= 0. Bruna / bruna2hg are new keys (no GT): smoke d0 6.6130, d3 4.9400, d5 5.0000,
2hg 5.5000; regression d0 6.5960, d3s2002 5.1800, d3s3003 5.1000, d5s2002 5.0667, d5s3003 5.1733.

### Every worse game
| Game | GT -> now | Verdict |
|---|---|---|
| fivecolour reg d3 s3003 gi126 | 5 -> 6 | budget churn: stage 1 (d5 b100) wins T5 |
| fungusb reg d3 s3003 gi78 | 5 -> 6 | budget churn: stage 1 (d5 b100) wins T5 (subagent: different T1 land at d3b10; d3b0 and d5b0 both T5) |
| fungusb reg d3 s3003 gi44 | 4 -> 5 | budget churn: stage 1 (d4 b100) wins T4 (appeared with b5b47d54; F-off arm also 5 at d3b10) |
| fungusb reg d3/d5 s2002 gi40 | 5 -> 6 | DEFECT -> fixed by b5b47d54; now 5 (no longer worse) |
| fungus smoke d0 gi65/82/382/442/793/988, reg d0 gi226/715 | +1..+2 | 787fefdd confirmed (each = GT turn with `MTG_LAND_AURA_RAMP_FIRST=0`). One mechanism: F spends an early {G} on Wild Growth (T2-T4), and the extra mana then buys ONE big spell next turn instead of two cheap bodies -- gi382/442/793 T3 Sporesower Thallid (atk 4) over Thallid + Sporecrown (atk 6); gi82/988/226/715 T5 Mycoloth (no attack that turn) where F-off swung for 12/12/13/9 lethal-or-near; gi65 T5 Doubling Season over Psychotrope + Wild Growth, T6 Mycoloth. Cast order is user-owned; deck net -13 / -26 |
| fungusb smoke d0 gi336, reg d0 gi249/431 | +1 | 787fefdd confirmed (each = GT turn with F off). gi336: T2 WG -> T4 Mycoloth over Deathspore + Sporecrown (atk 6 lost); gi431: T2 WG over Sporecrown (Sol Ring + WG), T4 no Beastmaster Ascension (atk 9 -> 6); gi249: T3 WG -> T6 Mycoloth alone, Saproling Burst slips to T7 (swing 16 -> 12) |
| fungusb reg d0 gi296 | 7 -> loss | 787fefdd confirmed (F off = 7). T3 Wild Growth goes on Peat Bog and is tapped THAT turn to pay Tukatongue, spending a depletion counter a turn early; T4 Saproling Burst empties Peat Bog, which is sacrificed and the Aura goes with it (CR 704.5m, 4f803e5a) -- so T5 is a mana short of Mycoloth, the F-off win line (devour into a 16 swing). Without Mycoloth the deck loops Bursts and never closes. Rules-correct; the cost is F's timing on a depletion host (host choice + order are user-owned) |
| fungusb smoke d0 gi239 | 6 -> 8 | **correct bound; GT relied on the bug.** Bisected to b5b47d54 (8570c689 = 6). T6, opp 8, swing worth 11: the K-axis activation block books ONE {4} for Slimefoot x2 (eval 2; "the rest paid inside the apply loop"), so every subset projects lethal and the greedy takes max eval. The old bound (2 lands + 2 Saproling tokens; defined Fungi uncredited) refused the 2nd activation after the 1st tapped Tukatongue + Sporecrown -> 1 token, swing 11, win. The corrected bound (4 >= 4) lets the apply pay it by tapping Saproling, Utopia Mycon, Slimefoot, Saproling -> swing 3, win T8. `MTG_PAY_BOUND=0` also gives 8. Further finding (pre-existing, NOT this game's cause): the greedy's attack-tap discount is dead -- see `docs/design/attack-tap-discount-dead.md` |
| fivecolour smoke d0 gi188, reg d0 gi445/953 | 7 -> 8, 5 -> 6, 5 -> 6 | **DEFECT -> fixed by the A-iii follow-up commit** (bisected to a80ebbb8, part A-iii; no hatch existed). The mana-unlock host (Greaves -> best fresh dork) was also offered to Solve's greedy (d0 + rollout leaves), which has no haste-dork mana credit, so it was taken on its DMG haste eval: gi445/953 T3 Greaves -> Birds of Paradise (0 power) over Bloom Tender / Deathrite (attack 2 -> 1, opp ends at 1); gi188 T6 Greaves + Bloom Tender pre-combat tapped Faeburrow Elder (no attack, 3 damage lost). No line used the hasted dork's mana. Now searched scope only (`MTG_UNLOCK_HOST_GREEDY=1` hatch): all three replay byte-identical to base; fivecolour d0 s1001 + s2002 back to their GT digests; kitty/dragons/angels/giants d0 GT-identical; Bruna d0 6.6130 -> 6.6090 / 6.5960 -> 6.5880 |
| angels + angels2hg (245 slower: smoke 92+3, reg 146+4) | +1 each typ. | DEFECT (G) -> fixed by 8570c689; all back to GT |

### Not caused by this branch (blocker for a clean regression run, not for the accept)
* Reference reproducibility `--strict`: **Hinata2/claude_s1_gi0 ENUM-GAP** (Soulfire Eruption target
  label). Reproduces identically on the base build of origin `c7487fb9` (`logs/basewt`) -- pre-existing.
* Reference reproducibility `--strict`: **Mirrorwing_Dragon/v2-instigator-entrance/claude_s51_gi50
  PLAY-DRIFT** (replay wins T5 vs recorded T4; 9 decisions the ref predates answered by engine
  default). **Verdict: not this branch.** `viewer_protocol_check.py --strict --only
  Mirrorwing_Dragon/v2-instigator-entrance/claude_s51_gi50` gives the byte-identical line on a fresh
  `./build.sh` of origin `c7487fb9` (scratch worktree, removed) and on the branch build at HEAD
  `cbca43b8` (src = `ff792be6`). The branch touches neither the reference nor the checker. The ref is
  on an ARCHIVED list (v2, archived 3faf5c76); its drift belongs to whoever owns Mirrorwing, not Bruna.
* Overnight was not run (out of scope for this pass) -- Bruna's overnight GT is still owed (`regression_tiers`).

### ff792be6 re-run + ACCEPT (2026-10-05, GT commit 2faab457)
Re-ran fivecolour, fivecolour2hg, bruna, bruna2hg (smoke + regression, `--deck=`, one pooled
invocation per mode) at c70890c9 and overlaid them on the staged set (pre-overlay backup:
`logs/suite_results_2026-10-05/pre_ff792be6_staged/`; final: `.../accepted/`).
* fivecolour d0 s1001 5.5580 and s2002 5.6470: **GT digests** (the A-iii defect games gone).
  d3/d5 every case: GT turns, digest-only play changes. Only slower game left: reg d3 s3003 gi126
  5 -> 6 (budget churn, above). Deck net 0 smoke / 0 regression.
* fivecolour2hg: smoke 5.4250 (GT 5.4500, net -1, 1 better, 0 worse), regression 5.3400 = GT turns.
* Bruna (new keys): smoke d0 6.6090, d3 4.9400, d5 5.0000; 2hg 5.5000; regression d0 6.5880,
  d3 5.1800 / 5.1000, d5 5.0667 / 5.1733. Searched cases same turns as the pre-fix staged run.
* Spot check (unchanged deck): fungusb smoke d0 + d3 at HEAD = staged `.wins` byte-identical.
* Accepted smoke + regression (full-tier accepts); `check_gt_logs.py` 668 consistent, 0 stale.
* **Overnight GT for Bruna / bruna2hg is still OWED** -- rows exist, not run (USER: no overnight
  until asked). `regression_tiers` stays PARTIAL on overnight until then.

## Site-9 numbering fix + MTG_ROLLOUT_AURA_SWAP adopted (2026-10-06)

**USER DECISION (2026-10-05, relayed):** *"if it is preventing correct lines we should be fixing it"* --
re: the site-9 continuation index mismatch that blocked `MTG_ROLLOUT_AURA_SWAP`.

**Rebase.** `bruna-analysis` rebased onto origin `71d45d8b` (37 commits replayed, no merge). Conflicts:
`Dominance.h` size assert (origin's `pay_ledger` +104 and Bruna's +48 -> 1016, both notes kept; it compiles,
so the number is measured) and the GT files of `2faab457` (fungusb smoke + regression keys/`.wins` --
resolved to OUR side consistently, `check_gt_logs.py` 668 consistent / 0 stale). Origin play changes:
`1abff9bc` adopts the fungusb candidate-b value leaf (fungusb play moves -- origin re-accepted its GT in
`71d45d8b`, which the rebase replaced with our pre-leaf fungusb keys, so **fungusb smoke + regression GT
must be re-accepted** on the rebased binary); `53adc731` three mana levers, all default OFF (+ the Snow
Sheets hold, human play only; GameState size change) -- default path declared byte-identical. Every tier
needs the orchestrator's re-run anyway (two src fixes below).

**Root cause** (`docs/design/site9-continuation-index-mismatch.md`): not two boards -- at the same board the
executor's and rollout's site-9 lists are identical. The committed plan was a **BP-NODE child**, stamped
`bp_at = bp_seen` from its BASE plan, which never advances `bp_seen` and never counts site 9 (gate
short-circuits on `bp_choice >= 0`). The child was scored by RESUMING past site 9 (Wish, Wings,
swap(Almost Perfect) at the wish's deferred site: Pilgrims untapped, lethal T5); the executor applies it
from scratch, counts site 9 as occurrence 0 == `bp_at` and spent `bp_choice` 1 there (Lightning Greaves),
so the swap tapped both Pilgrims -- T6. **Fix** `f0d61eda`: `bp_seen_shadow` counts as a variant would
(every class-on occurrence + site 9 under the variant's own condition) and the node capture records it
(`MTG_BP_NODE_SHADOW`, default ON, heurarm slot, =0 reverts). Fixture
`test/scenarios/bruna_site9_node_child_numbering.json`: T5 with the fix, T6 under =0 (control).
Scenarios 144/144, `mtg-test` 429/429.

**Decks expected to move (flag-independent):** measured, every suite deck x {d5 b20, d3 b10} x 60, seed 7001:
29/31 byte-identical. **Snow** (7 digests, gi24 T6 -> T7: the old T6 was the defect -- site 10's index applied
to SITE 8's list, an unscored continuation; fixed arm re-solves the untargeted site 8 as designed; recovers
d8 b0 T6 = budget churn) and **Melira** (2 digests, same turns). Bruna moves via the lever below.

**`MTG_ROLLOUT_AURA_SWAP` -> DEFAULT ON** (`b4542a3e`). Paired, fixed tree, Stage-5a set (1001/2002/3003/4004 x
{d3 b10, d5 b20} x 300 = 2,400 per arm, `MTG_FD_ORACLE=1 MTG_FLAG_NONCONV=1`): **14 better / 10 worse, -3 turns,
0 `[fd-diverge]`, 0 `[nonconv]`.** Extra d5/b20 seed-4004 x 400: 5/2, 5.0250 -> 5.0175. Worse games, two-stage
recovery: s2002 gi166, gi282; s3003 gi11, gi55; s4004 gi319 recover at stage 1 (d<off turn> b100); s3003 gi134
(T4) and s4004 gi61 (T3) recover at stage 2 (d8 b0) -- all budget churn, none unexplained. Seed 4205 gi201:
T5. 77008: T4 at b100/b200 in both arms (T5 at b20 with the pin, recovers at b100). d0 cells byte-identical.
Only Bruna holds an `aura_swap_cost` card.

**Owed:** the orchestrator's smoke + regression re-run and accept (all tiers -- src changed; fungusb GT
re-accept from the rebase); Bruna overnight GT still owed (USER: no overnight until asked).

## Aura host ranking -- one heuristic for every creature-Aura cast (2026-10-06, `49a261c6`)

**USER DECISION (2026-10-06, relayed):** which creature gets an Aura is *"not a difficult decision. It makes
sense to have a heuristic for it."* Combined with the standing rules: a heuristic may PRUNE (pick one host)
only if PROVEN against a fully-branched control; a counterexample is root-caused and the RANKING fixed (never
a fall-back to branching); escalate only if no correct ranking exists; executor and rollout identical; no
greedy substitute elsewhere in the search window.

**What it replaced.** (a) Every deck's autonomous dedup keyed a creature Aura by NAME and kept the FIRST
enumerated host (battlefield order); (b) Bruna opted out (per-host branching, `MTG_BRUNA_AURA_HOST_SIG`, sweep
B); (c) Solve (d0 + rollout leaves) took its first candidate; (d) ResolveEnchantTarget's fallback ranked by
Aura count. Now ONE key (`core/SpellEffects.h` "AURA CAST HOST RANKING", `TurnSolver` `AuraPlanHostBase` /
`AuraPlanHostKeyOn`) decides the host at every site: EnumeratePlans' dedup keeps each plan class's best-keyed
member (ties: first enumerated); Solve re-points its chosen plan's Auras among its own candidates + creatures
the same plan casts (`RetargetSolveAuraHosts`, shroud-checked); the resolution fallback ranks with the same
board key. Both apply worlds then realise the plan's `enchant_target` -> lockstep by construction. Human play
and `MTG_UNPRUNED` (viewer / claude-play) still offer every legal host; the viewer menu is unchanged (no
reorder -- reference replays index into it). Land Auras (Wild Growth) are untouched.

**The ranking** (goldfish objective, measured on a copy): the plan's creatures enter (sick), its Equips attach
(Greaves haste, and the shroud-release move of finding B), the REAL payer (`BatchPrepayMainCasts`, the same
call both worlds open a plan with) taps what it taps, the Auras attach (Colossification's ETB tap incl. the
main-phase respond window). Key: **lethal this turn > team damage this turn + next turn > more of it this
turn > fewer Auras on mana creatures**. Next turn leaves out the mana creatures the deck must tap for the most
expensive nonland card left in hand (lands/rocks + a land drop first, then mana creatures least-powerful
first; a creature-only source pays only for a creature card). A gathering attacker (Bruna) takes the better
of gather-all / gather-all-but-base-setters / none. Evasion/trample/lifelink: goldfish-irrelevant, not in the
key; no deck-specific tie-break was needed. Legal targets only: the candidates are the enumerator's (shroud /
hexproof / Enchant restrictions / Greaves order per finding B); Solve's re-point re-checks the shroud rule.

**Control arm:** `MTG_AURA_HOST_BRANCH=1` (heurarm slot `AURA_HOST_BRANCH`, renames `BRUNA_AURA_HOST_SIG`,
default OFF) = the fully-branched per-host search; Solve keeps the ranking in both arms (it never branched).
Instruments: `MTG_TRACE=hostproof` (every committed autonomous plan's hosts vs the key's best: heur / tie /
worse), `MTG_TRACE=hostkey` (the next-turn mana estimate). Artifacts `logs/ahproof/` (gitignored).

**Proof.** One pooled batch per round, both arms, play settings (d5 = Bruna/Auras `value_play`, d3 b10):

| round | counterexamples (control wins sooner) | root cause -> ranking fix |
|---|---|---|
| 1 | Bruna d5 gi49, gi52, gi165, gi238 | gi52/gi238: the key's own tap model spent Somberwald Sage's CREATURE-ONLY mana on Almost Perfect / Eldrazi Conscription, steering the Aura onto a Pilgrim the payer really tapped -> creature-only split. gi49: Wings tie Pilgrim/Mother kept the Pilgrim, which paid for the next turn's swap -> tie-break off mana creatures. gi165 -> see below |
| 2 | gi165, gi282 (new) | gi282: an enters-tapped land drop changed which dork the payer tapped -> the key now runs the REAL payer (`BatchPrepayMainCasts`) on the copy (the model only as fall-back when the prepay declines) |
| 3-7 | gi165 only | -- |

d0 has no branched control; vs the old binary its worse games were root-caused too: gi100 (Sol Ring's same-turn
{C}{C} missing from the fall-back supply -> plan ramp counted), gi566 / gi214 (Colossification on a dork that
paid for Eldrazi Conscription NEXT turn -> next-turn mana estimate), plus the "more of it this turn" tie-break.

**Final (round 7 binary; later commits perf-only, digests re-verified identical):**

| cell | games | heur vs control (better / worse) | avg heur / control |
|---|---|---|---|
| Bruna d5 s9.1M | 400 | 2 / **1** (gi165) | 5.1275 / 5.1300 |
| Bruna d5 s9.5M (fresh) | 400 | 0 / 0 | 5.1175 / 5.1175 |
| Bruna d3 s9.2M | 200 | 0 / 0 | 5.1450 / 5.1450 |
| Bruna 2HG d3 s9.6M | 100 | 0 / 0 | 5.7100 / 5.7100 |
| Auras d5 s9.1M | 400 | 0 / 0 | 4.1100 / 4.1100 |
| Auras d5 s9.5M (fresh) | 400 | 0 / 0 | 4.0800 / 4.0800 |
| Auras d3 s9.2M | 200 | 0 / 0 | 4.0400 / 4.0400 |
| Auras 2HG d3 s9.6M | 100 | 1 / 0 | 4.5400 / 4.5500 |

Commit level (hostproof): heuristic arm 7,700 committed creature-Aura plans, **0** not the key's best (2,491
ties). Control arm 4,398 commits, 503 on a host the key ranks strictly lower -- and none of those games won
sooner except gi165. **gi165 is not a ranking counterexample:** the heuristic arm recovers the control's T4 at
d8 b0 (stage 2 of the two-stage recovery check; d4/d5 b100 stay T5), and the control's winning line committed
hosts that TIE the key (Colossification -> a Pilgrim, `verdict=tie`), i.e. the line is inside the heuristic's
space; the arms diverge at T1 (no Aura decision) from different deeper plan lists = search allocation.

**vs the old binary (HEAD 9106b0d8, Bruna branched / others first-enumerated):** Bruna d5 400: 2/2 (gi165;
gi267 recovers at d5 b100), fresh 400: 3/0; d3: 0/0; 2HG: 0/0; **d0 8,000 (4 fresh seeds): 476 better / 31
worse, -587 turns (-0.073/game)**. Auras d5 2/0 + 1/0, d3 0/0, 2HG 2/0, d0 8,000: 10/0. `[enchant-retarget]`
fallbacks on the same jobs 24,491 -> 14,174.

**Cost** (paired, same box, each arm twice; ms = summed game time): Bruna d5 1.078-1.082M vs 1.110-1.121M ms
(**-3%**), units 57.4M vs 60.7M (-5.5%); Bruna d3 -2% ms, -6.9% units; Auras d5 ~0 ms, -5.8% units; Auras d3
21.5-22.6k vs 20.2-20.6k ms (**~+7%**, ~7 ms/game: the key's board copies in Solve), -1% units.

**Decks expected to move at the suite run:** bruna, bruna2hg, auras, auras2hg (every tier, d0 included). The
other 29 suite decks are byte-identical (all decks x {d0 200, d3 40} digests, new vs old binary).

**Tests:** `test_bruna_sweep.cpp` -- one ranked host per class + control keeps all; tapped-for-mana host;
Colossification tap; Bruna gather; never a Greaves-shrouded host without the move (search + Solve); creature-
only source; next-turn mana; resolution fallback. The sweep-B expressibility cases run under the control arm.
`mtg-test` 436/436, scenarios 144/144.

Open (none blocks anything; defaults taken):
1. d0 gi681 (Bruna, d0 only): Arcanum Wings' recast host ties (flying only) and the tie-break puts it on Mother;
   next turn Solve's MAIN-phase swap brings Colossification in and its ETB taps Mother (the combat-window swap
   would have been free). A swap-timing matter of the greedy, not of the host key; left as is.
2. The viewer menu is not re-ordered to put the ranking's host first (index-stable for reference replays); the
   human still sees every host. Want the ranking's pick marked/first in the viewer? Default: unchanged.
