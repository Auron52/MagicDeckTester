# Analysis ledger — AccomplishedAlchemist

Deck: `decks/AccomplishedAlchemist/AccomplishedAlchemist.cod` (mono-green + Living Wish sideboard).
Started 2026-10-18 on branch `accomplished-alchemist` (worktree `.claude/worktrees/accomplished-alchemist`,
forked from `phase-1-2-deck-analyzer` @ 2176032d).

## RESUME POINT

Chunks A, B, C1, C2a, C2b, P landed (P: the performance chunk -- three sound identity folds + two lossless
payer deferrals; the deck's per-game cost is now ~4.1 s (d3 b20) / ~7.1 s (d5 b40) contended, under the 3x
line's 8,907 ms). Next: Stage 5 verification (claude-play sweep, mismatch harnesses), the suite entry
(regression_cases.sh + suite_gate --cost), then profile -> mulligan -> value leaf. Open USER items are listed
under "Open questions surfaced to USER".

## The deck

Main (60): 3 Accomplished Alchemist, 4 Living Wish, 4 Crab Umbra, 4 Utopia Sprawl, 4 Stroke of Genius,
18 Forest, 4 Nourish, 4 Staff of Domination, 4 Elvish Harbinger, 1 Ezuri Renegade Leader,
1 Seedcradle Witch, 3 Arbor Elf, 1 Wild Growth, 1 Weather the Storm, 4 Feed the Clan.

Side (15, ALL reachable via Living Wish — creature or land): 1 Accomplished Alchemist, 2 Galvanic Alchemist,
1 Trufflesnout, 2 Scholar of Athreos, 1 Forest, 1 Island, 1 Eternal Witness, 1 Arbor Elf, 1 Brindle Boar.

### Intended engine (to be confirmed against Scryfall oracle text, never recall)

Accomplished Alchemist taps for X = life gained this turn (one colour). Lifegain (Nourish, Feed the Clan,
Weather the Storm, Staff of Domination's lifegain, sideboard lifegain creatures) pumps X. An untapper
(Crab Umbra on the Alchemist, Staff of Domination's untap-creature ability, Galvanic Alchemist) turns
X >= the untap cost into unbounded mana. Payoffs: Stroke of Genius aimed at the OPPONENT (deck them,
the opponent has a 53-card library when `opponent_library_dealt`), Ezuri's repeatable Elf pump,
Staff's draw. Elvish Harbinger tutors an Elf (Alchemist / Ezuri) to the top.

## Decisions carried into every subagent (do not re-litigate)

* Stroke of Genius's existing note ("target player = self") is WRONG now that the opponent has a library —
  `docs/design/passive-opponent-no-library.md` says so explicitly. Targeting the opponent is the deck's kill.
  Any forced-draw-on-opponent param must be added to `GoldFishRunner::DeckTouchesOpponentZones`.
* Drawing from an empty library loses at the next SBA check (CR 704.5b) — not at the next draw step.
* Everything faithful; a deferral is PROVISIONAL until the user signs it off.

## Cards (Stage 2)

| card | board | status | tier | notes |
|---|---|---|---|---|
| Nourish | main | implemented (chunk A) | 1 | cast_lifegain 6 (ApplyCastLifegain); no viewer decision; opponent's-turn cast PROVISIONAL inert |
| Eternal Witness | side | implemented (chunk A) | 1 | etb_return_gy_to_hand (Timeless Witness); viewer: dig (bucket A); PROVISIONAL 'may'->mandatory |
| Staff of Domination | main | implemented (chunk C2a) | 2+3 | self_untap_cost {1} (never its own action: paid ahead of each repeat of a {T} ability while tapped) + tap_lifegain_flat 1 (TapLifegain, K repeats via SpendSelfUntapRepeats) + untap_target_creature_cost {3} (ActivateUntap shape 3, SEARCHED target, K) + tap_draw_cost {5} (TapDraw, K repeats); {4},{T} tap target = PROVISIONAL inert |
| Crab Umbra | main | implemented (chunk C2a) | 3 | untap_enchanted_cost {2}{U} (ActivateUntap shape 1, K searched); host = searched enchant_target; umbra armor PROVISIONAL inert |
| Trufflesnout | side | implemented (chunk C1) | 3 | NEW etb_modal_self_counters 1 + etb_modal_lifegain 4; mode = SEARCHED cast axis (ETB CHOICE AXIS, chosen_x 1 counter / 2 life, etb_kx channel, both worlds); unpinned = EtbModalDefault; viewer: attack_mode generalised (trigger "etb") |
| Scholar of Athreos | side | implemented (chunk A) | 2 | drain params + NEW drain_each_opponent (x heads on loss AND gain; DrainLossPerActivation / DrainGainPerActivation at every drain site); lethal drain COUNT needs the chunk-C provider (ManaSinkActivationCounts) |
| Weather the Storm | main | implemented (chunk A) | 2 | cast_lifegain 3 + NEW storm bool (one GainLife per instance); ProjectPlanWalk now counts the plan's casts; cast order = USER decision (generic kept, storm-last only proposed) |
| Ezuri, Renegade Leader | main | implemented, fair pump (chunk A) | 1 | team_pump_* +3/+3 Elves (combat converter); main-phase lethal-K pump PENDING chunk C; regenerate/trample PROVISIONAL inert |
| Galvanic Alchemist | side | implemented (chunk C2a) | 3 | soulbond + soulbond_grants_untap_self_cost {2}{U} on BOTH halves (SoulbondGrantedUntapCost; shape 2); PARTNER SEARCHED on the ETB CHOICE AXIS (one variant per legal partner + decline, SoulbondPartnerPinScope, both worlds); viewer = existing attach_host soulbond chooser |
| Elvish Harbinger | main | implemented (chunk A) | 1 | Giant Harbinger tutor_to_top Elf + 5c dork; tutor axis AND the 'may' decline are searched variants (tutor_optional) -- no deferral needed |
| Seedcradle Witch | main | implemented (chunk C2a) | 3 | untap_pump_cost {2}{G}{W} + 3/3 (shape 4, SEARCHED target incl. untapped own creatures, K); {G}{W} off a single-colour Alchemist needs a second source (exact payer, pinned) |
| Utopia Sprawl | main | implemented (chunk C1) | 2/3 | NEW land_aura_host_subtype "Forest" (LandAuraHostOk at every host site) + etb_choose_color on a land aura (menu = land_aura_produces; locked colour credited per permanent via LandAuraProducesFor); colour = SEARCHED cast axis restricted by the provider to G/U/W/B; viewer: choose_color (full list) |
| Brindle Boar | side | implemented (chunk A) | 2 | self-sac outlet + NEW sac_outlet_lifegain 4; gate = SelfSacOutletUseful (death payoff OR read lifegain); self-only victim now baked correctly (CanonicalSacVictim self_only) |
| Stroke of Genius | main | implemented (chunk B) | 3 | NEW draw_target_player (+ targeting player): searched (target, X) cast variants via DrawXCandidates (opp = minimal lethal X only; self = cap/half/1..3 capped at library; human = all X); opponentdeck::ForcedDraw deck-out (decked at n >= library); shared PerformDrawX (executor ResolveDrawX + rollout); self = plain cantrip (site 3, IsSelfDrawXCast); DeckTouchesOpponentZones trait; fixtures aa_stroke_decks_opponent / aa_stroke_short_of_lethal. OPEN (user): the opponent prune drops non-lethal multi-turn deck-outs |

## Chunk P (2026-10-18) -- landed: performance (the 3x cost rule)

Goal: per-game cost under `suite_gate.py`'s line (3 x galadriel's 2,969 ms = **8,907 ms/game**, the max over
the deck's searched cases of case-ms / games). Measured on one pooled batch, 60 games x {d3 b20, d5 b40},
seed 9100, 32 workers (contended wall), before = C2b HEAD 0875a951.

* **Where the time was** (perf, d3 gi55 / d5 gi53): the GREEDY subset walk (`SolveUncached`, the rollout
  leaf) was 56-81% of CPU, and inside it `SubsetPayableAsExecuted` -- the land-Aura payer check (a whole-turn
  prepay + a per-cast walk on two GameState copies) -- was ~46%. It answered "payable" on 6,239,728 of
  6,239,848 calls.
* **Fold 1 -- `MTG_HAND_COPY_CLASS_FOLD` (default ON, heurarm slot).** The Aura-copy class
  (`AppendFungibleAuraClasses`, digits non-increasing in group order) generalised to ANY hand-cast group
  (`AppendFungibleHandCopyClasses`) and to battlefield activation groups of identical non-mana, non-creature
  permanents (`AppendFungibleActivationClasses`: two Staffs of Domination). WHY THE EXISTING FOLDS MISSED
  THEM: `MTG_FOLD_HAND_CASTS`'s CONDITION 2 refuses any slot with >1 action (Stroke's (target, X), Sprawl's
  host x colour, Galvanic's partner, Harbinger's tutor, Living Wish), the canonical-prefix rule reaches the
  search walk only on Snow (`FoldSearchOdometerOptIn`), and the plan-signature dedup catches the copies only
  AFTER each was walked, priced and scored (the greedy walk has no dedup at all). Identity bar: card content
  (`HandCardContentHash`) + m1 side, option lists equal member for member under `ActionFoldSig` (+ the fields
  it omits); refuses mana-side/accelerant/splice/restrictor casts, free casts, Equipment spells and Magnetic
  Theft (MTG_EQUIP_HOST_FOLD's restricted-growth string reads those in card order -- claiming them failed
  test_equip_host_fold.cpp's mirror cases), a copy another candidate names by number, and a class whose group
  order disagrees with candidate order (the equiv_tag prefix keeps the earliest candidates).
* **Fold 2 -- `MTG_LAND_AURA_HOST_CLASS_FOLD` (default ON, heurarm slot).** (a) a land carrying only land
  Auras joins a (name, tapped, sorted (Aura, locked colour) multiset) class instead of always keeping its own
  slot; (b) `AppendLandAuraHostPrefix`: the odometer keeps only positions whose used hosts are a PREFIX of
  each class (relabelling identical lands is a symmetry of the whole option set; compatible with the copy
  class -- relabel first, then sort the copies' digits). The candidate fold still keeps `keep` = land Auras
  in hand members, so a SPREAD and a STACK both survive.
  **NOT built: one host per class.** C2b priced it at 3.7x, but (1) that pricing collapsed every candidate of
  an Aura group to one host, i.e. it also priced Sprawl's COLOUR axis away (unsound); (2) one-per-class forces
  two Auras to stack, which is a dominance only within one phase -- a stacked host's surplus floats away at a
  phase end where a spread host's stays on the battlefield (main 1 needs {G}{U}, main 2 needs {G}{B}). Lossy,
  so not an identity fold. (Moot for play anyway: the autonomous plan dedup already folds land-Aura hosts by
  name except on the karoo branch -- the prefix removes walk positions that dedup would discard.)
* **Lossless payer work.** (1) `MTG_SOLVE_AURA_PAYER_DEFER` (default ON): the greedy keeps ONE subset, so the
  land-Aura payer is asked only of a subset that would REPLACE the best (a rejected subset and a non-improving
  one are both discarded); stands down while the walk's mana-verdict cache is armed (its one non-local
  consumer). 6.24 M -> 231 k calls on d5 gi53. (2) `MTG_SOLVE_RESCUE_DEFER` (default ON): the same for the
  filter-board real-payment rescue of a flat-unpayable subset (3.95 M calls, 33% rescued) -- only when a filter
  makes it the expensive kind. (3) `MTG_PAY_PROBE_INPLACE` (default ON): after a successful prepay the
  per-cast walk runs on the prepay's own scratch board instead of copying it again (the only reads of the
  original board precede the first mutation when the self-funding taps are off; falls back otherwise).
  `MTG_PAY_PROBE_STATS=1` prints the probe counts.
* **Work removed** (single-threaded census, before -> after): d3 gi55 greedy visits 11.83 M -> 6.84 M, search
  walk 6.95 M -> 4.59 M, payment calls 53.9 M -> 7.9 M, wall 50.1 s -> 9.8 s; d5 gi53 visits 19.6 M -> 16.0 M,
  payment calls 55.5 M -> 4.3 M, wall 65.8 s -> 16.9 s. Distinct plans after dedup unchanged (89,595 on gi55).
* **Deck cost** (pooled, contended): d3 b20 total 545.1 -> 244.5 core-s (0.449x), mean 9.1 -> 4.1 s, p90 29.4
  -> 10.3 s, max 62.3 -> 22.8 s; d5 b40 895.2 -> 424.5 core-s (0.474x), mean 14.9 -> 7.1 s, p90 55.0 -> 19.2 s,
  max 85.4 -> 54.8 s. **Play byte-identical**: both digests unchanged (c8103c26751617f1, 52fbfad72e5b0a2c),
  avg 5.6500 / 5.5333, 0 games faster, 0 slower -- so no slower game to classify.
* **Smoke** (not accepted): 124/128; the three known moves carry their C2a digests (selesnya d3 a1937cded0ab6f58,
  d5 9bd0e14ec876faa9, critter d0 ccb5b47d5ef529c9); ONE new move, `melira_smoke_d0_s1001` (avg 6.3380
  unchanged, 14 games play-changed at the SAME win turn, 0 slower / 0 faster, digest 0f633c1012b4fb11). Traced
  (gi230, gi463): the first diverging greedy call scores the IDENTICAL set of distinct subsets (gi463: 38 = 38,
  best rank 4 both), the fold only halves the duplicates, and the pick differs among tied subsets -- the
  greedy's tie-break `mask |= 1 << j` overflows past 31 candidates (Melira's Chord variants), so which of
  several equal-rank plans wins depends on which positions exist. Tie-break churn on an isomorphic board, all
  on d0. Searched cases: no other digest moved.
* **Residual** (the tail, not the mean): d5 gi40 (54.8 s, 5.09 M units) is NODE-bound, not walk-bound -- three
  decisions (T4/T5, `root=2`) spend ~900 k units each against a 36,000-unit budget, 95% of it in `u_la_cand`
  (SolveWithLookahead's candidate loop): a budget-honouring question at that site, not branching. The rest
  of the per-node cost is the exact payment DFS (`TapForCostShared*`) at the search walk's payability checks
  (EnumeratePlans emits every feasible plan, so its checks cannot be deferred).
* **Measured, NOT adopted (USER's call, not needed for the line):** `MTG_SOLVE_CHARGE_W=16` (bill the greedy
  walk, Snow's setting) on top of the folds: d3 0.52x core-ms at avg 5.6500 (1 faster / 1 slower), d5 0.55x at
  5.5333 -> 5.6000 (2 faster / 4 slower).
* Tests: 4 new cases (Stroke copies + a soulbond-named copy refused; Sprawl + Wild Growth prefix -- stack AND
  spread kept, mirrors dropped; decorated-host classes, blue/blue folds and blue/green does not; two Staffs,
  enumeration + greedy pick identical up to relabelling). mtg-test 1486/1486, scenarios 225/225.

## Chunk C2b (2026-10-18) -- landed: the closed-form untap loop and its payoffs

* **Shape.** No new Action kind: the loop is `ActivateUntap` with `chosen_x = kUntapLoopToNeed` (1<<20), so
  every ActivateUntap site (family key, trailing pass, dedup sig, line hold, viewer) already carries it. ONE
  dispatch `ApplyUntapAction` (rollout trailing pass + AIEngine twin) -> `ApplyUntapLoop` (SpellEffects.h),
  which drives ONLY the C2a primitives: `ApplyActivatedUntap` one activation at a time, `SpendSelfUntapRepeats`
  (the Staff's gain-life bootstrap and its draws), `TapSourceIntoFloat`. Safety ceiling 256 steps (never a lever).
* **Sizing = the exact need** (`ComputeUntapLoopNeed`): the cheapest this-turn kill -- Stroke of Genius from hand
  at the opponent (X = minimal lethal), a drain source ON the battlefield (Scholar, lethal count), Ezuri's team
  pump to lethal on the creatures that can attack (the loop's Alchemist counted untapped); failing all, the
  Staff's draws to the first Stroke in the library (never the last card). The loop stops when the FLOAT pays it
  EXACTLY (`UntapLoopFloatPays`: the payer's own float drain, payment by payment -- K separate drains can strand a
  {B} that a summed CanPay passes). Seedcradle Witch: its own +3/+3 on an attacking target is a kill by itself
  (`UntapLoopPumpIterations`). Every K >= need is outcome-equivalent (same board + mana each iteration, no burn,
  passive opponent), so the stop is a dominance fold; one variant per (untapper, target), not per payoff (every
  win this turn scores the same).
* **Colour per tap** (`UntapPreTapColour`, scoped `g_untap_loop_owed`): a pip of THIS untap the float lacks
  first (the Witch's {G}{W} alternates), else the colour with the largest remaining deficit (U for Stroke, B for
  the drain, G for Ezuri), else C2a's rule. Null outside the loop: C2a byte-identical.
* **Staff bootstrap** (`UntapLoopBootOp`): an exact BFS over (P, X, Staff tapped, target tapped) with ops gain /
  tap / untap -> the first op of a shortest path to "target untapped, X > steady cost"; re-run off the REAL state
  before every op. Minimal P matches the hand formula (X = 4/3/2 -> P = 2/5/8; unit-pinned). The loop ends by
  untapping the Staff out of the float's SURPLUS colour (never the payoff's pip).
* **Funding the payoff: a breakpoint, not a credit.** When the loop stops, the new site-9 key
  `kActUntapLoopFunded` (need MV in bits 40+) arms the continuation, which prices the payoff against the real
  float. PlanOpensBreakpoint marks any loop plan. Provider recognition (`UntapActivationCounts` override ->
  `UntapLoopWorthOffering`): the bootstrap search with the HAND's lifegain projected onto X (so "cast Nourish,
  then loop" is one plan; autonomous only -- human play gets no projection, the Wirewood rule), and only when the
  board cannot already pay the need. A loop on an UNTAPPED mana creature is priced at zero plan mana (its first
  activation is paid by its own first tap). Enumerate-optimistic, the resolver validates.
* **THE KILL FOLD** (`MTG_UNTAP_LOOP_KILL_FOLD`, DEFAULT ON, =0 hatch) in `BpDeriveContinuationList`: when the
  float pays the need, the continuation list is the proven kill only (CollectActions under
  `g_untap_loop_kill_only`). Without it the continuations ran to 9k-360k plans (every hand subset affordable);
  seed 9113 d5 went 9 s / 148 MB -> 175 s / 4.7 GB and the first pooled probe tripped the RSS cap. A win this turn
  dominates every alternative in a goldfish: sound.
* **Payoff hooks.** Chunk B's `DrawXCandidates` now also offers the opponent the MAX-affordable X when the lethal X
  is not affordable (the two-turn split deck-out; dominated when lethal is affordable, so not added then).
  `AccomplishedAlchemistProvider::ManaSinkActivationCounts`: Scholar's lethal drain count; the Staff's draw-to-find
  depth. New `DecisionProvider::MainPhaseTeamPumpCounts` (generic empty; the engine asks only while FLOATING mana
  exists) -> Ezuri's main-phase ActivatePump mode 2 with the lethal K; `ApplyActivatePump` mode 2 now adds
  `team_pump_tough` too (0 for Sethron / Valiant Knight / Vito).
* **Not reachable THIS turn (engine structure, not a card deferral):** Scholar from hand / via Living Wish and
  Ezuri from hand -- each needs a second post-entry re-plan after its own cast, and site 9 opens once per apply.
  Their two-turn lines stay searched.
* **Proven-loop memo marker: not built** (post-loop states are folded to the kill; folding life_gained would be
  unsound while a need is still being funded off a specific X).
* **Viewer.** `untaploop=<source>[#n][@target]` (`UntapActSpec::loop`; plan JSON `"untap_loop": true` on the
  untapact verb; linebuild writes the verb; index.html lists it in the ordinary picker beside the single
  activation, never the board-click arm). DECISIONS.md row; audit manifest note (rides the four untapper params).
  New scenario key `life_gained_this_turn` (mid-turn frames). Scenarios `aa_loop_umbra_stroke` (autonomous:
  Nourish -> loop -> Stroke X = 40 T4) and `aa_human_untaploop_crab_umbra`; linebuild case.
* **Tests** (test_accomplished_alchemist.cpp): Umbra loop to Stroke's lethal X (need, exact stop, offered as one
  more variant, both worlds win); thresholds (Umbra X = 3 no loop / X = 4 loop; Staff minimal P 2/5/8; tapped
  target); Staff bootstrap from X = 3 with P = 5 (and no loop at P = 4), Staff left untapped, both worlds win;
  Galvanic pair -> Scholar drains (black floated, both worlds win); Ezuri main-phase pump (lethal K = 3, +9/+9,
  hook float-gated, both worlds); Witch alternating G/W, its own +3/+3 as the kill (6 iterations); Staff draws to
  a Stroke at depth 3 inside the loop then the kill (both worlds). mtg-test 1482/1482; scenarios 225/225;
  viewer linebuild 0 FAIL.
* **Deck measurement** (60 games seed 9100, pooled, contended wall): d3 b20 avg 6.3167 -> 5.6500 (24 faster,
  1 slower: gi30 T7 -> T8), d5 b40 6.2667 -> 5.5333 (27 faster, 1 slower: gi40 T6 -> T7); job ms d3 1,991,498 ->
  551,414, d5 2,804,299 -> 898,106; slowest game 1,161 s (gi54, T8) -> 82 s (d5 gi53, T5); C2a's unwon seed 9118
  now T5; still unwon gi24, gi48 (both arms). The two slower games recover at 10x budget (d3 gi30 b200 T6,
  d5 gi40 b400 T5; C2a HEAD T7 / T7 there). Smoke: every non-AA case byte-identical to C2a HEAD (selesnya /
  critter_d0 carry only their known earlier moves).
* **Remaining search cost (diagnosed, engine-wide, NOT changed):** per-node cost in the greedy subset walk
  (`SolveUncached`, d3 gi55: 11.8 M subset visits, 7.9 M payment calls, 250-800 payment solves per unit) and the
  EnumeratePlans odometer; MTG_BRANCH_SHAPE prices an interchangeable-copy fold at 7.26x odo and a one-per-class
  land-aura host fold at 3.7x (Utopia Sprawl x 18 Forests x its colour axis). Both move other decks' GT: USER.

## Chunk C2a (2026-10-18) -- landed: the four untappers, Galvanic's searched partner, the lifegain breakpoint

* **ONE action kind, ONE resolver.** `Action::Kind::ActivateUntap` (appended last): shape in `gy_exile_mode`
  (1 Crab Umbra `untap_enchanted_cost`, 2 Galvanic grant `soulbond_grants_untap_self_cost`, 3 Staff
  `untap_target_creature_cost`, 4 Seedcradle Witch `untap_pump_cost` +3/+3), `sac_source_id` = source,
  `sac_victim_id` = TARGET (searched: one variant per legal creature -- never the Wirewood Lodge auto-pick),
  `chosen_x` = K (`DecisionProvider::UntapActivationCounts`, generic 1..min(3, affordable)), `cost` = ONE
  activation (the ActivateBlink self-funding convention). `ApplyActivatedUntap` (SpellEffects.h) is called by
  the rollout's trailing pass AND AIEngine's twin; it re-reads the ability through the oracle each activation
  (`CollectCreatureUntapAbilities` / `UntapUnitCost` / `SoulbondGrantedUntapCost`), pays through the unified
  `TapForCostDirect`, stops at the first unpayable or no-op activation. An UNTAPPED target is a no-op unless it
  is a live mana creature, which is tapped for mana into the float first (dominant: no mana burn, untapped
  again at the end); colour = the cost's first coloured pip it makes, else G (mana-payment exemption).
  Enumeration drops only DOMINATED targets (untapped + nothing this main can tap it; opponent creatures) --
  the Witch keeps every own creature (the pump is never a no-op). Human play: only targets helpable now.
* **Staff of Domination.** `self_untap_cost {1}` is never an action: the resolver pays it ahead of each repeat
  whenever the Staff is tapped -- shape 3's K is "untap target, {1} untap Staff, untap target again ..." as ONE
  action (the one-activation-per-source family rule therefore costs nothing), and the Staff's {T}
  PermAbility modes (`tap_lifegain_flat 1` on TapLifegain, `tap_draw_cost {5}` on TapDraw) get a K axis whose
  repeats pay {1}+cost (`SpendSelfUntapRepeats`, both worlds; `ManaSinkActivationCounts` with max_affordable =
  1 + (total - cost)/(cost + 1)). Different Staff abilities in ONE main = the site-9 continuation (the Staff's
  family is shared). {4},{T} tap target = PROVISIONAL inert.
* **Galvanic Alchemist's partner is SEARCHED** on the ETB CHOICE AXIS (`CastCarriesEtbChoice` +
  `SoulbondPartnerPinCandidates`): one variant per own unpaired creature on the battlefield / creature card in
  hand + `kSoulbondDeclinePin`; the pin rides the cast's chosen_x into soulbond Trigger A in both worlds
  (`SoulbondPartnerPinScope` at the two cast sites); an illegal pin falls back to the provider pick. Excluded
  from the Plan::soulbond_decline twin. Silverblade Paladin (no grant) is untouched (unit-pinned; smoke
  byte-identical for knights/whiteknights).
* **THE LIFEGAIN BREAKPOINT = site 9, new route (not a new site number).** `CollectActivationKeys` emits
  `kActLifeYield` for an untapped, able-to-tap `mana_per_life_gained` source once >= 1 life was gained, with the
  YIELD in the key's bits 40+, so the delta rule fires exactly when a resolution grew it (or an untap re-offers
  it); `PlanOpensBreakpoint` gets a state-keyed lifegain clause (an Alchemist that can tap + a plan action that
  may gain life -- `PlanActionMayGainLife`, a superset -- or untaps). Both worlds already share the gate, the
  snapshot and the continuation list, so lockstep is by construction; the Khalni kActQuestLandSearch precedent.
  WHY NOT A NEW SITE NUMBER: site 9's gate is exactly "an option exists now that did not at plan start",
  evaluated after the trailing pass -- where Brindle Boar / Staff gain-life / Scholar drains land -- and a new
  number would need renumbering BpSiteMask / kBpSites / the executor's counted classes for no behavioural gain.
  Also `kActUntapper` (a cast untapper re-plans with its ability) + CardHasPostEntryActivation terms.
  Fixture `aa_nourish_then_alchemist_six` (Nourish -> Alchemist for 6 -> Stroke X=6 decks the opponent T4;
  FAILS with MTG_POST_ENTRY_BP=0). Scoped by VALIDITY (no tappable Alchemist -> no key, no fan-out), applies to
  budgeted and unbudgeted play alike.
* **Alchemist bracket note corrected**: Seedcradle Witch's {2}{G}{W} and blue untap costs + green payoffs DO ask
  a single-colour burst for two colours; sound because the exact payer refuses (unit-pinned: Alchemist alone
  cannot pay {2}{G}{W}, + a Forest can). The note also records that the same-main conversion is now reachable.
* Provider signature OR-s the untappers' params (routing test: any single untapper keeps the provider).
* Viewer: `untapact=<source>[#num][@target]` (blink shape, target chooser on the board) and `tapdraw=<source>`
  (the Staff's draw; `taplife=` already covered its gain); Galvanic = existing attach_host soulbond chooser.
  DECISIONS.md rows; audit manifest BOARD_ACTIVATIONS + INERT_PARAMS (`self_untap_cost`, `untap_pump_power/
  _tough`, `tap_lifegain_flat` -- sign-off owed). Scenarios aa_human_untapact_crab_umbra /
  aa_human_untapact_staff_target / aa_human_tapdraw_staff. `PermAbilityLabelFor`: the Staff's gain reads
  "gain 1 life" (Wellwisher's label unchanged).
* **Smoke**: everything byte-identical except the known critter_smoke_d0 4-digest move and SelesnyaLifegain
  (the lifegain breakpoint is a real play change there -- it runs Accomplished Alchemist): selesnya d3
  5.2000 -> 5.1800, d5 5.2533 -> 5.2267; per game FASTER d3 gi9/gi25/gi130 and d5 gi9/gi25 (T5 -> T4),
  SLOWER none, same-score play changes d3 gi22/gi98/gi132, d5 gi22. NOT accepted. The reference bench's
  selesnyalifegain search score moved 5.5 -> 5.4 (human 5.6) in the harness re-stamp (reverted, not accepted).
* **Probe** (20 games, seed 9100, d3 b20): avg 5.95, 1 unwon (seed 9118); real Alchemist taps 17, 12 for
  more than 1 mana (6 of them for 6; max 12). Slow games: gi5 30 s, gi9 83 s, gi17 90 s (1.7M units) --
  repro `--seed 9100+gi --game-index gi --games 1`; a perf question to settle before the suite-cost gate.

### Hand-off to C2b (closed-form loop + payoff sizing)

1. Drive the SAME primitives: `ApplyActivatedUntap` (any K, any shape), `SpendSelfUntapRepeats` (Staff {T}
   modes), the oracle `CollectCreatureUntapAbilities` + `UntapUnitCost` (one activation's effective cost incl.
   the Staff's {1} when tapped) + `SoulbondGrantedUntapCost`. Never a second resolver.
2. The count hooks are where the loop size goes: `UntapActivationCounts` (generic caps at 3) and
   `ManaSinkActivationCounts` for the Staff's gain/draw K. An `UntapLoop` action that sequences
   untap -> payoff in ONE action is still needed: today a plan holds one action per SOURCE family, the
   Staff's gain-life/untap/draw share one family, and the K-loop's float is only spendable by a later
   site-9 continuation (once per apply) -- so "untap K times THEN Stroke for X" in one main needs either the
   loop action to size and cast the finisher (the SpendSurplusOnExile shape) or the continuation to see the
   float (it does: the continuation's pool includes floating mana).
3. Float COLOUR: the pretap colour rule (cost's first pip, else G) is fixed. A blue-cost untapper (Umbra,
   Galvanic) floats blue -- right for Stroke, wrong for Ezuri's {G}{G}{G}; the Staff's generic {3} floats
   green -- wrong for Stroke's {U}. The closed form must choose the colour per payoff (colour of the LAST
   tap only matters, each tap is one colour).
4. No ritual_float credit: the enumerator prices one activation and does not credit the mana a K-loop
   makes (the MTG_EDF_BLINK_CREDIT precedent measured neutral). The lifegain-breakpoint continuation sees the
   post-loop pool, which is how a payoff gets sized today.
5. A Staff that is TAPPED at enumeration still cannot offer its gain-life / draw PermAbility modes
   (PermAbilitySourceLive wants it untapped); only shape 3 handles a tapped Staff. Wire the {1} prefix into
   the PermAbility enumeration if the loop needs "gain-life from a tapped Staff".
6. Loop thresholds unchanged from alchemist-untap-loop.md: Umbra / Galvanic net X-3 (X >= 4), Witch X-4
   needing a second colour source, Staff X-4 per {1}+{3}.

## Chunk C1 (2026-10-18) -- landed: deck provider, Utopia Sprawl, Trufflesnout

* **Provider (Stage 4a).** Before: `SelesnyaLifegain` (Accomplished Alchemist's `mana_per_life_gained`).
  After: `AccomplishedAlchemistProvider : SelesnyaLifegainProvider` (derived from what it routed to, so
  play-neutral by construction), signature `draw_target_player` (Stroke of Genius) OR `storm` (Weather the
  Storm), routed ABOVE `selesnya`. Certificate NotAssessed (what a bound must price is in the class).
  **INHERITED judgement hooks the USER must see:** SelesnyaLifegain's USER-settled cleanup-discard buckets
  (`CleanupDiscardCandidates` + `CleanupDiscardShedStable`, `MTG_SELESNYA_BUCKET_DISCARD` default ON) --
  settled for ANOTHER deck's cards; this list mostly falls into no role. Kept (not changed) until this
  deck's own discard rule is decided. Everything else is DeckProvider/Generic. `TutorSearchWidth` = 6
  (inherited) >= the five Elf names. ONE override: `EtbChoiceCandidates` restricts Sprawl's colour to
  G/U/W/B (a restriction; still searched).
* **ETB CHOICE AXIS** (CollectActions post-pass, decision space only, never human play): a cast whose
  as-enters/ETB choice is searched is replaced by one variant per `DecisionProvider::EtbChoiceCandidates`
  value, carried as `chosen_x >= 1` down the existing etb_kx channel (the Felidar Guardian vehicle):
  rollout apply_one -> FireOwnEtbTriggers; executor CastSpellFromHand records the positive chosen_x on the
  stack entry -> EnterBattlefield -> FireOwnEtbTriggers. chosen_x already rides every plan signature;
  the autonomous name dedup keys it as `#EC`. Rollout leaves keep the unpinned base (provider default).
* **Utopia Sprawl.** `land_aura_host_subtype` enforced by `LandAuraHostOk` in LegalEnchantTargets,
  ResolveEnchantTarget (target + fallback, `land_pp`), AuraCouldEnchant, the in-loop land-aura deploy, the
  same-turn karoo host (`KaroosHostAdmitted`), and a provider-narrowed host list is intersected with the
  legal set. Colour: `etb_choose_color` on a land aura reads `land_aura_produces` as its menu; credited via
  `LandAuraProducesFor` (pool credit, LandAuraColorMask, BoardCanProduceColor, the payability-cache key);
  `EffectiveProducesFor` ignores land auras (not a source of their own).
* **Trufflesnout.** Modal ETB in FireOwnEtbTriggers (PutPlusCounters / one GainLife event).
* Viewer: Sprawl = existing `choose_color` (full five-colour menu); Trufflesnout = `attack_mode` generalised
  (emitter adds `"trigger":"etb"` + the card's option texts; Tectonic Giant byte-identical; GUI sub-heading).
  DECISIONS.md rows; audit manifest: `etb_modal_self_counters` -> attack_mode, `etb_modal_lifegain` /
  `land_aura_host_subtype` -> INERT_PARAMS (sign-off owed).

## Chunk B (2026-10-18) -- landed: Stroke of Genius

* `draw_target_player` (bool, new) + `targeting: player`. The TARGET PLAYER and X are both searched cast
  variants (`Action::draw_target` 0 self / 1 opponent; CastRiders -> rollout record + executor Player target).
  Every signature folds both (`#DX<x>/<t>` in the autonomous dedup; plan hash / fingerprint / PlansEqual /
  human sig fold draw_target only when nonzero, so no other deck's key moves).
* `opponentdeck::ForcedDraw(state, n)`: decked when the draw EMPTIES their library (n == L) or over-asks it
  (n > L, the CR 704.5b loss). n == L is flagged too, on TakeFromTop's argument: their simulated end-of-turn
  draw then fails on THIS turn, so the reported win turn is identical and only the within-turn win checks
  differ (they now see it). Minimal lethal X = max(1, L) (`ForcedDrawLethalX`). X = 0 decks nobody.
* `PerformDrawX` (SpellEffects.h) is the ONE resolver for both worlds; a self over-draw sets
  `player_lost_on_draw` (the old ResolveDrawX drew what was left and carried on).
* `DecisionProvider::DrawXCandidates` (new hook, default in DecisionProviders.cpp): opponent = the minimal
  lethal X when affordable, else nothing; self = {cap, ceil(cap/2), 1, 2, 3} with cap = min(max, own
  library); MTG_UNPRUNED=xspell opens 1..max; human play gets 0..max for both targets.
  **USER QUESTION (surfaced, default taken):** a non-lethal forced draw is a no-op THIS turn but not across
  turns (4 Strokes + a large finite pool can split a deck-out over two turns). Default = prune it (the brief's
  "dominated" reading); MTG_UNPRUNED=xspell is the A/B arm.
* Self-draw X is a PLAIN CANTRIP for the breakpoint machinery (site 3) via `IsSelfDrawXCast` -- the rollout's
  DrawSpell branch, IsPlainCantrip, BpSiteMask, the executor's equip_bp_truncates and cand_draws all read it;
  the opponent variant opens nothing. `ParamKeyedDrawClass` now claims DrawX (so site 10 never double-arms).
* Viewer: a `main_phase` plan variant labelled "(X=n) -> you draw" / "-> opponent draws", plan JSON
  `"draw_target"`; DECISIONS.md row + `draw_target_player` -> main_phase in audit_viewer_decisions.py.
  No resolution-time board click (the target fixes X's meaning, so it is chosen with the plan).
* Braingeyser / Unite the Coalition notes reworded to name the limitation (FiveColour deals no opponent
  library), param NOT added.
* Mana: the AA fixture pays {6}{U} with the Alchemist's one blue through the exact payer. The pre-combat
  pool credits the Alchemist exactly 1 (no lifegain-growth credit), so a Nourish -> Alchemist -> big-X Stroke
  line in ONE main is not sized by the enumerator yet -- chunk C.

## Chunk A (2026-10-18) -- landed

Nourish, Eternal Witness, Elvish Harbinger, Ezuri (fair pump), Scholar of Athreos, Brindle Boar,
Weather the Storm. New params: `drain_each_opponent`, `sac_outlet_lifegain`, `storm` (tests:
test/unit/test_accomplished_alchemist.cpp + F4c in test_attack_projection.cpp). Also:
`deck_reads_lifegain_in_play` now scans a wish-reachable sideboard; ProjectPlanWalk counts the plan's
casts (storm); stale "MTG_SL_SECOND_MAIN default ON" notes corrected (Alchemist, Feed the Clan,
Bogbeast, HeuristicArm.h) -- the default stays OFF. Separate commit: a self-only sac outlet now bakes
itself as the victim (was a silent no-op) -- moves critter_smoke_d0 4 digests, win turns unchanged;
smoke GT for that NOT accepted yet. ROUTING measured: the deck rides SelesnyaLifegainProvider today
(pinned in test_pirates_provider.cpp until chunk C's own provider).

## Open risks

* ROUTING: mana_per_life_gained + cast_lifegain_ferocious are terms in the selesnya signature (DecisionProviders.cpp ~21151) -> deck likely rides SelesnyaLifegainProvider. Stage 4a must give it its own provider derived from what it routes to today.

* ROUTING (2): Arbor Elf's mana_requires_land_subtype sets the stompy signature (checked before anti) -> may ride StompyProvider. tutor_to_top -> anti (TutorSearchWidth 2). Own provider, width >= 5.
* Alchemist bracket note 'no cost asks for two different colours' is FALSE in this deck (Seedcradle Witch {2}{G}{W}); also blue (Umbra/Galvanic/Stroke), black (Scholar).
* Utopia Sprawl colour + Galvanic soulbond partner are provider picks today = greedy-in-window concern; prefer searched axes.
* SEQUENCING (central): Alchemist's bracket note says DeckUsesSecondMain is ON for Alchemist decks, but MTG_SL_SECOND_MAIN went DEFAULT OFF 2026-09-30 (measured on SelesnyaLifegain, GoldFishRunner.cpp:234). Sac-outlet / tap-lifegain activations are TRAILING (after casts) and Alchemist is credited 1 pre-gain -> same-main lifegain->Alchemist conversion likely unreachable. Architecture design must settle this; the m2 lever for THIS deck is a USER-visible decision (measure both).

## Viewer decisions (2c-ter)

## Verification (Stage 5)

## Open questions surfaced to USER (defaults taken)

* Stroke at opponent offered only at minimal lethal X (chunk B). Lossy vs completeness (a 2-turn split deck-out). Default taken: chunk C2 adds a max-affordable opponent variant too; MTG_UNPRUNED=xspell is the full arm.
* Second main for this deck: global default OFF kept; m2 arm to be measured.
* Weather the Storm cast order: generic kept; storm-last proposed.
* Utopia Sprawl colour: made SEARCHED (chunk C1), provider-restricted to G/U/W/B. Galvanic partner: C2.
* Inherited SelesnyaLifegain cleanup-discard buckets on AccomplishedAlchemistProvider (chunk C1): kept
  for play-neutrality; this deck's own discard rule is the USER's to settle.
* critter_smoke_d0_s1001 moves 4 digests (avg unchanged) from the chunk-A self-only sac-outlet victim fix -> accept at landing.

* Chunk C2a INERT_PARAMS classification (`self_untap_cost`, `untap_pump_power`, `untap_pump_tough`,
  `tap_lifegain_flat` -- each the payload of a verb, never a separate choice): sign-off owed.
* SelesnyaLifegain play moved by the lifegain breakpoint (5 games faster, 0 slower in smoke) -- GT not
  accepted; acceptance is the USER's call through the normal flow.
* Chunk C2b: (1) the continuation KILL FOLD (MTG_UNTAP_LOOP_KILL_FOLD default ON) -- a sound dominance fold
  (a proven win this turn), but it is what made the deck tractable; (2) Stroke at the opponent now also offers
  the max-affordable X when lethal is unaffordable (your completeness point from chunk B, taken as the default);
  (3) two engine-wide folds priced but NOT built -- interchangeable hand copies (7.26x odo on the slow games) and
  one-per-class land-aura hosts (3.7x) -- they move every deck's GT, so they are yours to call; (4) Scholar via
  Living Wish / Ezuri from hand cannot kill the SAME turn as the loop (site 9 opens once per apply) -- an engine
  structure question, not a card deferral.

* Chunk P: (1) the three folds and the two payer deferrals are DEFAULT ON engine-wide (sound identity
  folds / lossless reorders; the collapse doctrine). Note the hand-copy class also folds ONE-option copies on
  the SEARCH walk for every deck -- the thing `FoldSearchOdometerOptIn` kept per deck (Snow only) after the
  2026-10-01 fleet screen; smoke shows no searched digest moved. (2) `melira_smoke_d0_s1001` moves 14 d0 games
  at the same win turn (tie-break churn, traced) -> accept through the normal flow. (3) a latent defect worth a
  decision: the greedy tie-break `mask |= 1 << j` in SolveUncached is undefined past 31 candidates, so ties
  there are arbitrary on Chord/Pod-sized menus; fixing it moves GT wherever it bites. (4) the one-host-per-class
  land-Aura fold was NOT built (a dominance that fails across a phase boundary -- lossy); C2b's 3.7x pricing of
  it was unsound. (5) `MTG_SOLVE_CHARGE_W=16` measured (0.55x, d5 +0.067 turns), not adopted -- not needed.

## Approved deferrals

(none yet)

## PROVISIONAL deferrals (awaiting user sign-off)

* Eternal Witness: 'you may return' collapsed to mandatory (returning is weakly dominant; no graveyard-count/discard payoff in deck).
* Staff of Domination: {4},{T}: Tap target creature -- inert (opponent creatures never attack/block; nothing reads tapped state; tapping own creature is never better).
* Ezuri: regenerate inert (nothing destroys our creatures); trample inert (no blockers).
* Crab Umbra: Umbra armor inert (nothing destroys our creatures).
* Seedcradle Witch: targeting an opponent's creature not offered autonomously (inert -- nothing reads their P/T or tapped state).
* Instant-speed activity on the OPPONENT's turn (Crab Umbra untap, Nourish) not modelled -- engine acts in our phases; an end-step kill lands strictly later than the same line in our main.
