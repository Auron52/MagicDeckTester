# Analysis ledger — KittyEquipment

In-flight per-deck ledger for the analyze-deck workflow (see `.claude/skills/analyze-deck.md`,
"The per-deck ledger"). Continuously updated; survives compaction and handoffs.

## Deck

`decks/KittyEquipment/KittyEquipment.cod` — mono-white (splash via Boros Garrison) cat/kor
equipment goldfish. 60 cards:

| # | Card | Status |
|---|------|--------|
| 4 | Colossus Hammer | missing |
| 4 | Puresteel Paladin | missing |
| 4 | Kor Duelist | missing |
| 2 | Balan, Wandering Knight | missing |
| 4 | Armored Skyhunter | missing |
| 4 | Bonesplitter | missing |
| 1 | Loxodon Warhammer | missing |
| 2 | Unexpectedly Absent | missing |
| 1 | Swords to Plowshares | full (pre-existing) |
| 1 | Sol Ring | full (pre-existing) |
| 22 | Plains | full (pre-existing) |
| 2 | Shadowspear | missing |
| 2 | Kemba, Kha Regent | missing |
| 1 | Boros Garrison | missing |
| 1 | Grafted Wargear | missing |
| 1 | O-Naginata | missing |
| 1 | Lightning Greaves | full (pre-existing; the equipment-subsystem exemplar) |
| 2 | Umezawa's Jitte | missing |
| 1 | Stoneforge Mystic | missing |

## Existing equipment infrastructure (pre-analysis survey)

`src/cards/CardDatabase.h` ~545-557: `is_equipment`, `equip_cost_generic` (int; header note says
promote to full ManaCost when a costed Equipment arrives — this deck's equips are all generic, so
int may suffice), `equip_grants_haste`, `equip_grants_shroud`. Attach state =
`Permanent::equipped_to`; re-equip = `Action::Kind::Equip` (sorcery-speed). NO support yet for:
equip P/T bonuses, granted keywords (trample/lifelink/double strike), equip-count conditionals,
equipment ETB triggers (Puresteel draw), metalcraft, upkeep token triggers keyed on attached
equipment (Kemba), equipment tutors (Stoneforge), attack-trigger dig-and-attach (Skyhunter),
charge counters (Jitte).

## Stage progress

- [x] Stage 1 coverage check — 15 missing, 0 partial; Swords/Sol Ring/Plains/Greaves full.
- [x] Stage 2 implement — research fan-out (15 agents) DONE; serial integration DONE (2026-08-13):
  * **CardParams**: equip_power/tough_bonus, equip_grants_lifelink, equip_min_power,
    equip_sacrifices_prior_host, equip_combat_damage_charges + charge_pump/minus/lifegain,
    double_strike_while_equipped, double_strike_min_equipment, attach_all_equipment_cost
    (ManaCost), draw_on_equipment_etb, metalcraft_equip_zero_artifacts,
    upkeep_tokens_per_equipment, attack_dig_attach_count, tap_put_from_hand_cost/types,
    tuck_to_library, allow_self_target; `Targeting::NonlandPermanent`; Keyword Metalcraft
    (inert tag) + "First strike"/"Double strike" Scryfall-case variants.
  * **SpellEffects**: EquipBonusFor, CountEquipmentAttachedTo, HasDoubleStrikeFromEquipment,
    FindAttachedChargeEquip + JitteDamageMath (shared closed forms), CountControlledArtifacts +
    EquipCostGenericNow + EquipActionCostNow (payment recompute), EquipGatePowerOf, extended
    ApplyEquip (min-power gate + Wargear re-host sac), CreatureHasLifelink equipment scan,
    Puresteel draw at the universal enter cascade, FireAttackDigAttach (.cpp) +
    ApplyAttachAllEquipment/ApplyPutFromHand/ApplyJitteMode (.cpp), doomed-legend
    equipped_to/aura zeroing fix.
  * **Combat/projections**: EquipBonusFor + ds-from-equipment + Jitte math at ALL THREE sites
    (Combat.cpp ResolveCombatDamage, TurnSolver attacking-mana-source scorer,
    PendingAttackDamage); SBA toughness += EquipBonusFor; FireAttackDigAttach called in both
    CombatPhase and SimulateCombat before damage.
  * **TurnSolver actions**: equip gate WIDENED (haste ranking width 1 unchanged; rider ranking
    by realized delta, width MTG_EQUIP_RIDER_WIDTH default 3; Wargear = all benefiting hosts;
    `UnprunedGate::EquipHost` + HumanPlayActive opens every LEGAL pair; min-power = legality,
    never bypassed); metalcraft cost at enumeration + all 3 payment sites; new kinds
    AttachAllEquipment / PutFromHandAbility / JitteModeAbility (enumeration, apply_one,
    AIEngine executor mirrors, ActivationFamilyKey, plan_signature BALAN#/SFPUT#/JITTE#,
    SummarizePlan labels, LineSpec verbs attachall=/sfput=/jittemode= + CheckLine); UA tuck
    (enumeration gate, X candidates {0} pruned / full unpruned+human, executor + rollout tuck
    resolution, NonlandPermanent targeting); Kemba upkeep in both lockstep blocks.
  * **Providers**: AttackDigPutCandidates + AttackDigAttachHost (delta-greedy) +
    JitteSpendCount (default -1 greedy) base implementations.
  * **Viewer**: `attach_host` (BounceChooser shape, positional --choices) + `jitte`
    (FirebreatheChooser shape, turn-keyed --jitte/--jitte-prompt side-channel) choosers,
    emitters, GUI branches (index.html), server.js plumbing, DECISIONS.md rows, auditor
    MANIFEST rows; Skyhunter put reuses `dig`, UA target reuses `target`.
  * **DeckUsesSecondMain**: attack_dig_attach + draw_on_equipment_etb combo detection.
- [x] Stage 3 coverage clean — missing=[], 0 partial, all bracket notes approved deferrals.
- [~] Stage 2d-bis audits — first cost-audit pass: NO mismatches reported, but 15 cards
  (incl. most new ones: Shadowspear, Grafted Wargear, O-Naginata, Umezawa's Jitte, Kor
  Duelist, Puresteel Paladin, Balan, Armored Skyhunter) hit HTTP 429 rate-limits =
  UNVERIFIED transients. **MUST re-run `python3 scripts/audit_card_costs.py` (spaced out)
  until the new cards verify**, plus `python3 scripts/audit_card_fields.py` offline diff once
  the `--update` snapshot lands (was running in background; commit the refreshed
  `src/cards/data/scryfall_reference.json`). Costs were pasted from the research agents'
  live Scryfall fetches (ledger table above), so mismatches are unlikely but the mechanical
  gate must still pass.
- [~] Stage 4 baseline profile — `scripts/analyze_deck.py decks/KittyEquipment/KittyEquipment.cod
  --no-rebuild` running in background; writes decks/KittyEquipment/KittyEquipment.profile.json.

## RESUME HERE (post-compaction checklist)

1. Check background results: field-audit snapshot (`audit_card_fields.py` offline diff must be
   clean for the 15 new cards), re-run cost audit until 429s clear, confirm the profile JSON
   exists and parse `analysis.card_scores` / `mulligan_flags`.
   [2026-08-13 AUDITS DONE: cost audit exit 0, NO mismatches, no 429s (throttled 1.0s re-run,
   logs/kitty_audits/audits_rerun.log). Field audit: snapshot refreshed, offline diff exit 0 —
   sole hard mismatch was PRE-EXISTING Lava Spike missing subtypes=['Arcane'] (goldfish-inert;
   fixed in cards.json). All 15 new cards' structured fields match Scryfall; their oracle-text
   advisories are the expected bracket-note divergences.]
   [Earlier status: first field-audit --update pass 429'd on 8 cards (incl.
   Bonesplitter, Colossus Hammer); both audits re-run serially with --throttle 1.0 →
   logs/kitty_audits/audits_rerun.log. 5h STATIC part DONE: the 17 new cards.json params are
   classified in scripts/audit_viewer_decisions.py INERT_PARAMS (KittyEquipment section, each
   with reason — all payload/legality detail riding an already-MANIFEST-mapped decision, plus
   Puresteel may-draw = always-taken, disclosed in 6a); static run exits 0, oracle-text
   advisories all accounted for (equip "target" = host pick, Jitte target/modal =
   JitteModeAbility main-phase variants). The SWEEP form of 5h (with profile) still pending.]
### Stage 5 findings so far (2026-08-13, post-compaction session)

Sweeps ran on seed 300001 (suite-disjoint), 100 games d0/d3/d5 pooled batch + 50-game
full-depth fd-oracle + equip-width arms. THREE REAL DEFECTS found and FIXED:

1. **[fd-diverge] seed=300018 → rollout missed Puresteel draws on CAST equipment (FIXED).**
   The rollout's noncreature-permanent cast branch (TurnSolver ~9147) never fired the
   universal enter cascade (`OnDragonEnters`), so a cast Equipment did not trigger Puresteel
   Paladin's draw in projections while the executor (EffectHandler:34) did. Concretely: real
   T5 Greaves cast drew 2 cards (2 Puresteels) incl. the Colossus Hammer it then cast; the
   projection kept those cards in the library and its Skyhunter attack-dig put a phantom
   Hammer (+10) off the top → predicted_win=5, realized=6. Fix = fire the cascade in that
   branch (find the entered permanent by cast_number; no-op for decks without enter-watchers
   → byte-identical elsewhere). After the fix the game genuinely wins T5 (search now uses the
   drawn Hammer). Diagnosis trail: MTG_FD_TRACE [fd-pred] libtop/libsize vs the realized log
   (predicted libsize 49 vs real 47 at T5 = the two missed draws).
2. **`--jitte-prompt` / `--firebreathe-prompt` silently dropped when LAST on the command
   line (FIXED).** The claude-play arg parser's else-if chain is gated on `i+1 < argc` (value
   flags); value-less booleans inside it vanish in last position — `--storage-hold-prompt`
   had already been moved out with a comment saying exactly this; the other two prompts were
   still trapped. 5h caught it as "Jitte cast but `jitte` decision never surfaced"; a debug
   print showed Install() saw prompt=0. Both moved to the value-less pre-chain section.
3. **audit_viewer_decisions.py sweep drove the SAME game n_games times (FIXED, pre-existing)**
   — step() passed a constant `--seed` with varying `--game-index`, but the engine uses
   --seed verbatim as the shuffle seed (game-index is labels only). Fixed to seed+gi. Also
   added: a repeat-state pass rule (equip {0} = free repeatable action → the engine faithfully
   re-offers "move the equipment"; a driver that always takes a plan toggles forever — pass
   -1 when the same (turn, plan-summaries) state recurs), and jitte side-channel driving
   (--jitte-prompt + "turn:count" replies, mirroring server.js).

5h now PASSES with all six types surfaced live: attach_host, bounce, dig, discard, jitte,
target. First-chain numbers (pre-fix, superseded): d0 5.72 / d3 5.09 / d5 5.10.

FURTHER DEFECTS found by the re-swept chain (all FIXED, 2026-08-13/14):

4. **Provider MISROUTE: the deck ran under AntiLifegainProvider (FIXED).** Stoneforge
   Mystic's `tutor_to_hand` trips the anti-lifegain signature — the exact Goblin-Matron
   misroute class already documented in DetectDecisionProvider. Fix = new
   `EquipmentProvider` (GenericProvider + overrides below) + an equipment signature keyed
   on the deck's new gated params (attack_dig_attach_count, equip_combat_damage_charges,
   tap_put_from_hand_cost, ...), detected BEFORE anti. `[play] ... provider=Equipment` is
   the tell it works.
5. **d5 PATHOLOGICAL GAME (seed 300003 gi=2): 40+ min unfinished (FIXED via provider).**
   Once the rollout learned Puresteel draws (fix #1), deep-simulated hands grow (a Greaves
   cast draws 2), the board piles Kemba cats + 5-8 equipment whose metalcraft {0} equips
   the mana bound cannot prune, and EVERY rollout-leaf Solve walks ~1M subsets (odometer
   position product stays < 1e7 per node — MTG_ENUM_STATS silent — it is VOLUME of leaves,
   ~250M+ consider() calls). Budget can't fire mid-enumeration. Fix = EquipmentProvider
   opts into `UseLethalShortCircuit()` (the Goblins-proven wide-board cut, off-switch
   MTG_NO_LETHAL_CUT): the game finishes in 2m52s and WINS T4. A/B (d3, 100 games,
   MTG_NO_LETHAL_CUT=1): 99/99 finished games byte-identical win turns; the 100th is
   gi=2 again, unfinishable without the cut (killed after 30+ min). Quality-neutral,
   rescues the tail.
6. **5e equip-width regression EXPLAINED + FIXED: haste-host width 1 → provider-owned 2.**
   gi=39: the T5 kill is "cast Balan, equip Greaves→Balan ({0}, haste), attack; Skyhunter
   dig puts Loxodon Warhammer→Balan (2 equips → double strike) = 18 ≥ 14". Width-1 haste
   ranking never offers Greaves→the-same-subset-cast Balan. New provider hook
   `EquipHostWidth()` (base default 1 = byte-identical everywhere incl. FiveColour;
   EquipmentProvider returns 2; MTG_EQUIP_HOST_WIDTH still overrides). 100-game d3 A/B:
   width2 == MTG_EQUIP_ALL_HOSTS == MTG_UNPRUNE=equiphost exactly (avg 5.02, sole diff
   gi=39 T6→T5), zero nonconv → the pruned width is now EXACTLY as good as fully open on
   this sample.

**Cross-deck safety: regression smoke 33/33 PASS, all play digests byte-identical**
(logs/kitty_s5/smoke_after_fixes.log, run after fixes #1/#2/#4/#5; the width hook default
keeps #6 byte-identical for existing decks — FiveColour smoke digests unchanged).

FINAL-ENGINE sweep numbers (chain3, provider + lethal cut, width 1): 5a CLEAN — 0 nonconv
anywhere, 0 fd-diverge in 50 full-depth d5 games. 5b monotone + plausible: d0 5.59 /
d3 5.03 / d5 5.02 (97/100 wins each, seed 300001); every depth better than pre-fix. Sole
d3-vs-d5 mover gi=5 moves the right way (d5 T6→T5). fd_full_d5 avg 4.98. chain4
(batchA4/fd_oracle4/viewer_sweep7) re-verifies on the ADOPTED config (width 2).

Stage 4 profile was generated on the PRE-fix binary under the WRONG provider →
REGENERATE on the final engine (mandatory).

2. Stage 5 with the new profile (suite-disjoint seeds; deck path auto-resolves profile):
   * 5a: `MTG_FLAG_NONCONV=1` d3 + `MTG_FULL_DEPTH=1 MTG_FD_ORACLE=1` d5 sweeps, grep
     [nonconv]/[fd-diverge] — zero required; root-cause any hit (Jitte projection and
     metalcraft enumeration-vs-payment conservatism are the likely suspects).
   * 5b: d0/d3/d5 MTG_DUMP_WINS monotonicity + plausibility (equipment aggro ~T4-6 expected);
     read outlier games.
   * 5e/A-B: pruned vs MTG_EQUIP_ALL_HOSTS=1 (and MTG_UNPRUNE=equiphost) per-game diff — the
     new rider-width-3 narrowing must be net-neutral-or-better with every regression explained.
   * 5h: `python3 scripts/audit_viewer_decisions.py decks/KittyEquipment/KittyEquipment.cod
     decks/KittyEquipment/KittyEquipment.profile.json` (+ --verify-card for Skyhunter/Jitte/
     Stoneforge/UA/Balan); expect types: main_phase variants, dig, attach_host, jitte, target,
     bounce, scry-none.
   * 5d: ~15-20 game Sonnet claude-play sweep (base seed disjoint from suite), aggregate
     {ai_win, claude_win, flags}.
3. Stage 6 report + 6a disclosure table (sources: this ledger's Approved deferrals +
   Integration bullets; heuristics to disclose: equip host rankings + widths, Skyhunter
   put/host provider picks, JitteSpendCount greedy, UA X={0} + opponent-only pruned targets,
   metalcraft enumeration conservatism, always-take Puresteel draw).
4. Regression suite: deck is NOT in test/regression_cases.sh yet — adding it is a user call.
- [ ] Stage 5 verification (5a nonconv/fd-diverge, 5b multi-depth, 5d claude-play sweep, 5h viewer audit)
- [ ] Stage 6 report
- Smoke: 3 games d3/b200 seed 42 → avg 5.67 turn-to-win; engine plays the deck.

## Integration map (orchestrator survey, pre-drafts)

- Attack power is computed at THREE mirrored sites — `src/ai/Combat.cpp` `ResolveCombatDamage`
  (~line 59-67), `src/ai/TurnSolver.cpp` ~810-814 (attacking-mana-source scorer) and ~864+
  (`PendingAttackDamage`). Each already sums `EffectivePower + lord + exalted + AuraBonusFor`.
  Equipment P/T bonuses = a new `EquipBonusFor(creature, state)` sibling of `AuraBonusFor`
  (`src/core/SpellEffects.h:1273`) summing `equip_power_bonus/equip_tough_bonus` over permanents
  with `equipped_to == creature.m_number`, added at all three sites in lockstep.
- Keyword grants: lifelink already has `CreatureHasLifelink` (keyword + aura grants,
  `SpellEffects.h:1305`, applied in Combat.cpp:80) — extend with equipment grants. Double strike
  already computed in Combat.cpp:55-58 (keyword + lords); equipment/conditional sources OR in
  there + the two TurnSolver projection sites. Haste-from-equip exists (`HasHasteFromEquip`).
- Equip enumeration already rich (TurnSolver ~4076-4240: one Equip action per (equipment, host)
  pair, stranded-equip subset rejection, mana-unlock haste equips). Equip cost =
  `equip_cost_generic` int — metalcraft equip-{0} (Puresteel) hooks wherever that cost is read.
- Karoo lands fully templated (Izzet Boilerworks exemplar); `bounce` viewer decision exists.
- **CRITICAL (from Bonesplitter draft): the Equip plan-candidate gate in TurnSolver's equip
  block (~L4154-4242) is haste-only today** — `grants_something` fires only for
  `equip_grants_haste` onto a fresh no-haste host. A pure P/T equip would silently never be
  offered as a plan (the code's own MAINTENANCE BREADCRUMB warns exactly this). Integration must
  widen the gate to P/T-bonus (and any other rider) equips, rank hosts by attack benefit, and
  scale `Action.eval` by granted power. Verify post-build that an Equip-Bonesplitter plan
  actually appears (claude-play / log check).
- SBA toughness re-check (`GameEngine.cpp` ~L699) consults only `ComputeLordBonus` — add
  `EquipBonusFor(...).second` when any toughness-granting equipment ships (Hammer +10/+10).
- Equip COST is baked into the Action at enumeration (`a.cost.generic = equip_cost_generic`,
  TurnSolver ~L4234) — Puresteel metalcraft equip-{0} must be applied there (and anywhere the
  cost is re-read at apply time). Host selection is `MTG_EQUIP_HOST_WIDTH` (default 1 = single
  heuristic pick, score = power + mana-yield + attack-payoff; `MTG_EQUIP_ALL_HOSTS=1` A/B
  lever). For an equipment-centric deck the width-1 narrowing is a real 5e/6a scrutiny item —
  the score must at least reflect the EQUIP's own granted power (Bonesplitter draft's point)
  and conditional payoffs (Kor Duelist ds, Kemba upkeep cats, Jitte counters).

## Cards done

- **Boros Garrison** — draft ready (Tier 1, exact Karoo sibling of Izzet Boilerworks;
  `produces [R,W] x2, enters_tapped, etb_bounce_land`; viewer `bounce` already wired; no C++).
- **Kor Duelist** — draft ready (Tier 2: new creature-side param `double_strike_while_equipped`
  + `HasDoubleStrikeWhileEquipped` helper; OR into Combat.cpp:55-58 and TurnSolver ~807/~861
  non-animated ds branches; no viewer decisions).
- **Colossus Hammer** — draft ready (Tier 2: `{1}`, equip {8}, `equip_power_bonus 10 /
  equip_tough_bonus 10` — same shared fields as Bonesplitter; eval at ~L4239 must scale with
  granted power, not the flat "nicety" 1). Proposed deferral (user sign-off pending): "loses
  flying" — inert, the skill's own flagship example (nothing blocks in goldfish).
  **PRE-EXISTING GAP it found: `MTG_EQUIP_HOST_WIDTH`/`MTG_EQUIP_ALL_HOSTS` are plain env
  reads, NOT opened by `MTG_UNPRUNED` — human play in the viewer sees only the single
  heuristic host, violating the "human-play must not narrow" rule. Fix in integration: make
  the equip host filter consult `DecisionUnpruned()` so it self-opens for human play.**
- **Shadowspear** — draft ready (Tier 2: `{1}`, equip {2}, +1/+1, `equip_grants_lifelink`
  [extend `CreatureHasLifelink` with an equipment scan]; trample + the {1} strip ability
  proposed-deferred). NAMING RECONCILE: its `equip_bonus_power/tough` → integrator uses
  `equip_power_bonus/equip_tough_bonus` (Bonesplitter/Hammer naming, mirrors aura fields).
- **Kemba, Kha Regent** — draft ready (Tier 2: `{1}{W}{W}` 2/4 legendary; ONE new bool
  `upkeep_tokens_per_equipment` riding the existing upkeep-token machinery (Thrumming Hivepool
  precedent) — lockstep blocks in GameEngine.cpp ~305-321 AND TurnSolver ~9816-9831; snapshot
  count+m_number BEFORE CreateToken (push_back invalidates the ref). Token WHITE color
  proposed-deferred (CreateToken has no color; nothing reads it). Legend-keep choice for a
  human = disclosed auto-decision (keep-oldest weakly dominant). LATENT EDGE found:
  EnforceLegendRule's doomed-removal path doesn't zero `equipped_to` of equipment on the
  doomed legend — inert under keep-oldest, fix one line while in the area.)
- **Puresteel Paladin** — draft ready (Tier 3: `{W}{W}` 2/2; `draw_on_equipment_etb` hooked at
  the UNIVERSAL enter cascade `OnDragonEnters` (NOT on-cast — Stoneforge/Skyhunter puts must
  draw), always-take-may (6a disclosure), copy draw_on_aura_cast body;
  `metalcraft_equip_zero_artifacts=3` via new shared `EquipCostGenericNow` helper +
  `CountControlledArtifacts`, applied at enumeration ~L4234 AND payment sites (apply_one
  ~9291, ApplyManaUnlockEquips ~1421, AIEngine executor ~2880) — recompute at apply, since
  metalcraft can flip mid-plan; wire a SameTurnReducerGenericCredit-style optimistic credit
  (Ruby Medallion precedent) so "cast artifact #3 → free equip" lines aren't dropped at
  enumeration. Equipment are artifacts: 13 equips + Sol Ring → metalcraft on from 3rd
  artifact; Paladin itself doesn't count. No deferrals.)
- **Loxodon Warhammer** — draft ready (Tier 2: `{3}`, equip {3}, +3/+0,
  `equip_grants_lifelink`; trample proposed-deferred — engine has NO blocking anywhere;
  Armadillo Cloak precedent. Confirms gate-widening + human-play `MTG_EQUIP_ALL_HOSTS`/
  host-width fix must land once, first.)
- **Unexpectedly Absent** — draft ready (Tier 2: `{X}{W}{W}` Instant, `removal` template +
  new `tuck_to_library` + `allow_self_target`; resolution = Library::insert at min(X, size)
  (generalizes tutor_to_top's insert-at-front) in EffectHandler ~455-497 + rollout mirror
  ~8577-8645; opponent-target tuck = erase-from-battlefield (opponent has no library;
  spawns are tokens, CR 111.7 — faithful, not a simplification); own-side targets are REAL
  branches (Stoneforge reset line) via new `TuckTargetCandidates` provider hook; card-aware
  XCandidates override — pruned path X={0} (higher X provably never better for either use),
  unpruned/human falls through to full range. Scope flag: recommend new
  `Targeting::NonlandPermanent` (one-line `!IsLand()` widening) instead of creature-only —
  own equipment becomes a legal self-tuck (marginal Puresteel redraw payoff).)
- **Umezawa's Jitte** — draft ready (Tier 3: `{2}`, legendary, equip {2};
  `equip_combat_damage_charges=2` + `charge_pump_power/tough=2` reusing
  `Permanent::charge_counters` (Vial — already deep-copied, sim-key-folded L13363, viewer
  badge); trigger + spend modeled IN the shared `ResolveCombatDamage` (executor+rollout
  lockstep by construction): non-DS closed form dmg = P+2·spend, then +2 counters if event>0;
  DS closed form dmg = 2P+4C+4 ending at 2 counters (first-strike half earns counters
  spendable on regular half — the Kor Duelist/Balan interaction). Provider hook
  `JitteSpendCount` (FirebreatheActivations shape), default greedy spend-all; real
  approximation = never saving counters for a future DS turn (5e A/B item). Lethal-projection
  sites (~L2549/6683/11086 + attack-power stamps) must add projected pump — gate-unsafe
  otherwise. Viewer: Bucket-B combat decision replicating the `firebreathe` row (turn-keyed
  side-channel). Legend-keep should prefer the copy with more counters. Deferrals proposed:
  -1/-1 mode + gain-2-life mode (both near-zero payoff vs passive opponent); "deals combat
  damage" collapses to "to a player" (nothing blocks).)
- **Stoneforge Mystic** — draft ready (Tier 2: `{1}{W}` 1/2; ETB tutor = existing
  `tutor_to_hand + tutor_types [Equipment] + tutor_shuffle_after` (Goblin Matron precedent —
  zero new code, TutorCandidates fans out every distinct library Equipment); activated put =
  NEW `tap_put_from_hand_cost {1}{W}` (full ManaCost, NOT int — colored) +
  `tap_put_from_hand_types` and a new Action kind `PutFromHandAbility` copying
  ActivateVial/GraveyardExileAbility patterns (CanTapNow sickness gate, cost on a.cost so
  subset math reserves it, put enters UNATTACHED via shared enter cascade → Puresteel draw
  fires); plan_signature must key the put-card name (enchant_target dedup lesson); LineSpec
  verb + SummarizePlan label; executor mirror. Deferrals proposed: "reveal it" unobservable;
  instant-speed activation collapse. 6a note: cast-path has no explicit "decline search"
  variant (near-inert).)
- **Grafted Wargear** — draft ready (Tier 3-lite: `{3}`, equip {0}, +3/+2 shared fields, new
  `equip_sacrifices_prior_host` — WotC ruling 2020-11-10 fetched: sac only DOES something on a
  genuine re-host to a different creature; all other unattach cases resolve as no-ops, so the
  logic lives ONLY in ApplyEquip's re-host branch (via `SacrificePermanentAt`, re-look-up `eq`
  after erase reallocates). Host-width note: with sac-on-rehost the static host score can't
  price the loss — open full host width (bounded to legal/benefiting hosts, NOT global
  ALL_HOSTS) when the flag is set and let the search weigh it; each (equip,host) already has a
  distinct dedup signature `EQ#id#host`. AttachAllEquipment (Balan) must route through
  ApplyEquip so the sac fires there too. No deferrals.)
- **Armored Skyhunter** — draft ready (Tier 3: `{3}{W}` 3/3 Flying; `attack_dig_attach_count=6`
  → new shared `FireAttackDigAttach` fired in CombatPhase AFTER attack self-pumps and BEFORE
  ResolveCombatDamage (lockstep GameEngine::CombatPhase + TurnSolver::SimulateCombat; copy the
  `PerformLightPawsAttach` shape) — attach bypasses equip cost via direct `equipped_to`
  assignment and MUST route through the shared equipment-enters path so Puresteel draw + Kemba
  count fire. Provider hooks `SkyhunterPutCandidates` (may decline) + `SkyhunterHostIndex`
  (rank by realized damage delta this combat; ds hosts double it). Second-main: relevant IF
  Puresteel present (mid-combat draw = combat resource) → extend `DeckUsesSecondMain` to
  detect attack_dig_attach + equipment-ETB-draw combo. Viewer: TWO Bucket-B decisions
  (put-card pick, attach host) on a firebreathe-style turn+ordinal side-channel (mid-combat,
  can't ride positional --choices); null in RevealLogPause; add DECISIONS.md + auditor
  manifest rows. Deferrals proposed: Aura half structurally dead (zero Auras in deck);
  multi-Skyhunter trigger order collapsed to battlefield order; deterministic bottoming
  (etb_dig precedent). Audit the fast attack projection ~L889 — rank-safe, gate-unsafe.)
- **O-Naginata** — draft ready (Tier 2: `{1}`, equip {2}, +3/+0 via shared fields, new
  `equip_min_power=3` — legal-target gate in the candidate loop AND ApplyEquip's host_ok
  (defense in depth; host's effective power counts already-attached equip bonuses, excluding
  the one being placed). Proposed deferrals: trample (no blocking in engine) + skipping the
  continuous CR 704.5p SBA re-check (dead code for this deck: its own +3 self-sustains the
  threshold; nothing reduces our power). Also flags host-width HumanPlayActive() bypass.)
- **Balan, Wandering Knight** — draft ready (Tier 3: `{2}{W}{W}` 3/3 First Strike, legendary;
  `double_strike_min_equipment=2` via `CountEquipmentAttachedTo` folded into the shared ds
  check at all three sites; new `Action::Kind::AttachAllEquipment` ({1}{W}, colored cost on
  Action::cost, bypasses equip costs — the Colossus Hammer line; gate on ≥1 equipment not
  already on Balan; description/serialization switches + AIEngine executor mirror; Wargear
  unattach-sac trigger must live in the SHARED attach path). Proposed deferrals: first strike
  inert; instant-speed collapse to main phase; ds modeled as ×2 damage (all precedented).

## Approved deferrals (USER SIGN-OFF 2026-08-13)

- **Group 1 — inert combat keywords (APPROVED, condition: Jitte counter gain still modeled):**
  Colossus Hammer "loses flying"; trample grants (Shadowspear, Loxodon Warhammer, O-Naginata);
  Balan first strike (his conditional double strike IS modeled ×2); Skyhunter flying; Jitte
  "deals combat damage" collapses to "to a player" (counter gain itself fully modeled).
- **Group 2 — PARTIALLY APPROVED: "Implement Jitte modes."** Jitte's -1/-1 mode AND gain-2-life
  mode must be IMPLEMENTED as real choices (not deferred). Only Shadowspear's "{1}: opponents'
  permanents lose hexproof and indestructible" stays deferred (inert — spawns never carry
  either keyword; deck removal is exile/tuck).
- **Group 3 — conventions APPROVED:** Kemba token color white unmodeled; Stoneforge "reveal
  it"; instant-speed→main-phase collapses (Stoneforge put, Balan attach-all); Skyhunter
  multi-copy trigger order = battlefield order + deterministic bottoming; O-Naginata
  equip-time-only power gate (no continuous SBA re-check); Skyhunter Aura-half resolution
  path dead (filter still implemented). All bracket-noted + 6a-disclosed.
- **Unexpectedly Absent: full `Targeting::NonlandPermanent` APPROVED**, with the user note
  "I don't think there is much upside to searching this right now" → pruned-path provider
  candidates stay LEAN (opponent creatures + at most a curated own-side pick); the full legal
  set surfaces under MTG_UNPRUNED / human play as always. Disclose in 6a.

## Verbatim Scryfall card data (from research fetches — integration source of truth)

| Card | Cost | Type | P/T | Oracle (verbatim) |
|---|---|---|---|---|
| Colossus Hammer | {1} | Artifact — Equipment | — | Equipped creature gets +10/+10 and loses flying.\nEquip {8} |
| Puresteel Paladin | {W}{W} | Creature — Human Knight | 2/2 | Whenever an Equipment you control enters, you may draw a card.\nMetalcraft — Equipment you control have equip {0} as long as you control three or more artifacts. |
| Kor Duelist | {W} | Creature — Kor Soldier | 1/1 | As long as this creature is equipped, it has double strike. |
| Balan, Wandering Knight | {2}{W}{W} | Legendary Creature — Cat Knight | 3/3 | First strike\nBalan has double strike as long as two or more Equipment are attached to it.\n{1}{W}: Attach all Equipment you control to Balan. |
| Armored Skyhunter | {3}{W} | Creature — Cat Knight | 3/3 | Flying\nWhenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equipment card from among them onto the battlefield. If an Equipment is put onto the battlefield this way, you may attach it to a creature you control. Put the rest of those cards on the bottom of your library in a random order. |
| Bonesplitter | {1} | Artifact — Equipment | — | Equipped creature gets +2/+0.\nEquip {1} |
| Loxodon Warhammer | {3} | Artifact — Equipment | — | Equipped creature gets +3/+0 and has trample and lifelink.\nEquip {3} |
| Unexpectedly Absent | {X}{W}{W} | Instant | — | Put target nonland permanent into its owner's library just beneath the top X cards of that library. |
| Shadowspear | {1} | Legendary Artifact — Equipment | — | Equipped creature gets +1/+1 and has trample and lifelink.\n{1}: Permanents your opponents control lose hexproof and indestructible until end of turn.\nEquip {2} |
| Kemba, Kha Regent | {1}{W}{W} | Legendary Creature — Cat Cleric | 2/4 | At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba. |
| Boros Garrison | (land) | Land | — | This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{W}. |
| Grafted Wargear | {3} | Artifact — Equipment | — | Equipped creature gets +3/+2.\nWhenever this Equipment becomes unattached from a permanent, sacrifice that permanent.\nEquip {0} |
| O-Naginata | {1} | Artifact — Equipment | — | This Equipment can be attached only to a creature with power 3 or greater.\nEquipped creature gets +3/+0 and has trample.\nEquip {2} |
| Umezawa's Jitte | {2} | Legendary Artifact — Equipment | — | Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.\nRemove a charge counter from Umezawa's Jitte: Choose one —\n• Equipped creature gets +2/+2 until end of turn.\n• Target creature gets -1/-1 until end of turn.\n• You gain 2 life.\nEquip {2} |
| Stoneforge Mystic | {1}{W} | Creature — Kor Artificer | 1/2 | When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand, then shuffle.\n{1}{W}, {T}: You may put an Equipment card from your hand onto the battlefield. |

## Verification verdicts

Final engine = enter-cascade fix + EquipmentProvider (lethal cut, host width 2) +
--jitte-prompt parse fix, at the post-38e1fe2 uncommitted tree. Seeds 300001+ (suite-disjoint).

| Check | Verdict | Evidence |
|---|---|---|
| 5a nonconv (d0/d3/d5 ×100 + fd d5 ×50) | **CLEAN — 0 flags** | logs/kitty_s5/batchA4.log, fd_oracle4.log |
| 5a fd-diverge (MTG_FULL_DEPTH d5 ×50) | **CLEAN — 0 flags** (1 pre-fix hit root-caused → cascade fix) | logs/kitty_s5/fd_oracle4.log |
| 5b multi-depth | **monotone + plausible**: d0 5.58 / d3 5.02 / d5 5.01, 97/100 wins each; sole d3→d5 mover improves (gi=5 T6→T5); fd-full d5 4.96 | batchA4.log |
| 5e equip-width A/B | **pruned == fully open** (width2 vs MTG_EQUIP_ALL_HOSTS vs MTG_UNPRUNE=equiphost: identical per-game, avg 5.02) | equip_allhosts3/equip_unprune3/hostw2 logs |
| 5e lethal-cut A/B | **quality-neutral**: 99/99 finished games byte-identical; 100th (gi=2) unfinishable without the cut | no_lethal_cut3.log |
| 5h viewer decisions | **PASS** — attach_host, bounce, dig, discard, jitte, target all surfaced live through the real protocol | viewer_sweep7.log |
| Cross-deck byte-identity | **smoke 33/33 PASS**, all play digests unchanged | smoke_after_fixes.log |
| Stage 4 profile (final engine) | **DONE** — regenerated under EquipmentProvider: baseline 4.78 avg d3 (200 games, seed 90001), COST_NEUTRAL reframe, DISCARD_INERT (0 cleanup decisions in 400 games), card_scores top: Sol Ring .59, Kor Duelist .55, Puresteel .35, Greaves .25, Stoneforge .22; bottom: StP −.50, Garrison −.28 | decks/KittyEquipment/KittyEquipment.profile.json |
| Re-verify under regenerated profile | **CLEAN** — 0 nonconv, 0 fd-diverge (50 fd games), d0 5.58 / d3 5.02 / d5 5.01 (97/100), sole mover gi=5 improves at d5, 5h PASS | logs/kitty_s5/batchA5.log, fd_oracle5.log, viewer_sweep8.log |
| FINAL regression smoke (all edits in) | **33/33 ALL PASS**, play digests byte-identical | logs/kitty_s5/smoke_final.log |
| 5d claude-play sweep | **DONE — see "Claude-play sweep" below**: 16/16 games, 0 unresolved flags, 2 misplay candidates → measured cost of the DISCLOSED metalcraft enumeration conservatism | workflow wf_776fe815-7b9 |

## Claude-play sweep
- commit: `38e1fe2` (+ this uncommitted tree — final engine: cascade fix, EquipmentProvider, width 2)
- seeds: 70001 games: 16
- flags: 0 unresolved
- Detail: 16 Sonnet agents (workflow wf_776fe815-7b9, frozen profile copy). Win turns:
  11/16 exact match with the d5 search; 3 Claude-slower (gi=0,12,15 — play-quality, expected);
  **2 Claude-FASTER (gi=13, gi=14, both T4 vs search T5)** — both wins stack Colossus
  Hammers after flipping metalcraft ON mid-turn (cast 2-3 artifacts, then equip {0}); the
  search's one-shot enumeration prices equips at start-of-phase artifact count ({8},
  unaffordable) so the line is never offered — this is the 6a-disclosed metalcraft
  enumeration conservatism, now MEASURED at ~2/16 games ≈ 1 turn each. Improvement
  (post-subset artifact-count equip pricing) noted as deferred heuristic work.
- Flag resolutions:
  * CONFIRMED (gi=6, repeats gi=10/15 uncertain): T1 plan "cast: Kor Duelist, Sol Ring"
    off one Plains is jointly unpayable (colored-pip vs colorless rock output). ROOT-CAUSED
    to the pre-existing, documented flat-pool total-mana subset gate; execution's exact
    payment drops the tail in executor/rollout LOCKSTEP with visible `dropped_casts` (0
    fd-diverge corroborates). Deferred fix: docs/design/color-aware-subset-mana-gate.md.
  * Cosmetic (gi=0/5/10): shared dig-decision note said "into your hand" for the Skyhunter
    attack-dig (which puts onto battlefield) — FIXED in WriteDigDecisionJson (destination-
    aware note).
  * UX gap (gi=15, uncertain): plan summaries do not disambiguate same-named equip hosts
    ("equip Greaves → Kor Duelist" ×2 duelists) — the actions ARE distinct variants; noted
    as a viewer UX improvement (needs care: summary strings feed validate-line matching).
  * gi=13 (uncertain): metalcraft-flip turn re-offered an identical equip menu with a
    stale battlefield view before the equips landed — the free-equip re-offer trap the
    driver/pass rule already handles; observability note only.

## Stage 6 — final report (2026-08-14)

**The deck is analyzed and viewer-ready; the convergence loop is CLOSED.** Coverage clean
(19/19 defs), audits clean, profile regenerated on the final engine, every Stage 5 check
green under that profile, claude-play sweep clean (0 unresolved flags), regression smoke
33/33 byte-identical for every other deck.

**Performance**: baseline **4.78 avg turn-to-win** (d3, 200 games, seed 90001) /
**5.02** on the 100-game seed-300001 battery (d5 5.01, full-depth 4.96); 97/100 win rate
within 8 turns. Clock: T4-5 kills via Kor Duelist/Balan double-strike + stacked equipment;
the 3 non-wins are mulligan-crippled hands. Depth is worth ~0.56 turns over greedy (d0
5.58 → d3 5.02); d3→d5 nearly flat (this deck's lines resolve within 3 plies).

**Card notes from card_scores** (deltas, drawn-vs-not): Sol Ring (+.59) and Kor Duelist
(+.55) carry the deck; Puresteel (+.35), Greaves (+.25), Stoneforge (+.22), Bonesplitter
(+.19) form the engine. Swords to Plowshares (−.50) is the worst slot — pure removal is
nearly dead vs the passive opponent (only hits spawned blockers... which never block);
Boros Garrison (−.28) taxes tempo. UA (−.18), Shadowspear (−.15), O-Naginata (−.11) are
below par. If the user wants deck IMPROVEMENT screening later, StP/Garrison/UA are the cut
candidates (deck-screening skill, separate opt-in).

**Known play-quality gap (measured)**: the metalcraft enumeration conservatism (equip
costs priced at start-of-phase artifact count) cost 1 turn in 2 of 16 claude-play games —
the only systematic gap between the search and an informed human. Deferred improvement:
price equips at post-subset artifact count (heuristic-optimization route).

**Not done / user calls**: regression-suite addition (test/regression_cases.sh) — user
decision; commit of this tree — nothing committed yet; value-leaf + exhaustive mulligan
profile — separate skills, on request (generate LATE, post-commit-freeze per their Rule 0).

## USER HEURISTIC DOCTRINE (2026-08-14, post-push directives) — IN FLIGHT

User-directed equip/Jitte/UA doctrine to encode as provider prunes + one RULES fix.
Verbatim intent, then the implementation plan. STATUS 2026-08-14: IMPLEMENTED (all sites
below coded, builds clean); measurement battery in flight — see "Doctrine measurement"
at the end of this section. Residual known gap: the autonomous solo-target-trick target
pickers (Mirrorwing) and pump-target heuristics do not consult CreatureHasShroud — inert
today (no deck co-occurs Greaves with own-creature-targeting spells; the human-facing
target collectors DO filter), but if such a deck arrives those sites need the same check.

**Directives (user):**
1. Jitte: search should ALWAYS use +2/+2 (combat greedy spend — already the default);
   never enumerate the -1/-1 / gain-2-life modes in autonomous search (no goldfish value).
2. Unexpectedly Absent: just don't cast it in autonomous search, for now.
3. Equip consolidation: stack extra power on ONE creature, preferring creatures with or
   that GAIN double strike (Kor Duelist while equipped, Balan at 2+). Re-equips (moving an
   attached equipment) only from a non-double-strike host to a double-strike(-potential)
   host **or to Kemba** (amendment 2026-08-14: "a re-equip to Kemba from a
   non-doublestriker is also reasonable, particularly if it is free. Essentially, Kemba
   and the Doublestrikers have higher than normal equip value"). Grafted Wargear: never
   move it except to a ds creature for lethal ("I don't think it's worth it otherwise,
   though we can test this"). Lifelink/trample exceptions don't matter in goldfish.
   Exception: Kemba vs double-striker stays a SEARCHED decision (or a kills-this-turn
   heuristic) — enumerate both candidates. Amendment (2026-08-14): "Most likely on the
   whole the doublestrikers outrank Kemba because they finish the game so quickly, but
   Kemba is still higher value than other creatures in the list for equipping and in slow
   games may beat doublestrike, especially when the equipment doesn't add much power." →
   ds-vs-Kemba stays searched (never a fixed rank); Kemba ranks above all other non-ds
   creatures when no ds-potential host exists.
4. Lightning Greaves: (a) may need to MOVE to unshroud a host so another equipment can be
   equipped (SHROUD MUST BLOCK targeted equip — currently NOT enforced, see rules fix);
   matters mostly at exactly ONE creature (with more you park Greaves elsewhere);
   (b) use to enable Stoneforge Mystic's tap-put by granting haste (CanTapNow ALREADY
   handles equip-granted haste — no engine change needed); (c) otherwise haste the largest
   (ds-weighted) summoning-sick creature; (d) Greaves goes LAST after all other equipment
   lands on the host (because its shroud blocks later equips); (e) amendment 2026-08-14:
   "Lightning greaves should always be equipped to Kemba by end of turn if possible" —
   the parking spot is Kemba (user: "that is literally a free 2/2 next turn", and "the
   2/2 cat becomes a target for the greaves so you can equip other things to Kemba next
   turn" — the park is self-unblocking; equip {0} so moving it off and back is free).
   Encoded as an always-offered Greaves→Kemba candidate
   (any main; end-of-turn parking realizes in the second main) — the search confirms it;
   if measurement shows the search declining the park, escalate to a forced default and
   report.
5. User confirmed: Balan's attach-all and Skyhunter's attach-dig do NOT target → shroud
   does not block them. Regular equip DOES target → blocked.

**Implementation plan (sites identified):**
- RULES FIX — shroud blocks targeted attach/targeting:
  * `CreatureHasShroud(p, state)` helper in SpellEffects.h (mirror CreatureHasLifelink:
    scan attached equipment for equip_grants_shroud; Greaves is the only source).
  * Equip enumeration legality (TurnSolver equip block ~L4276-4378, host loop ~4336):
    exclude E→X when X is phase-start shrouded, UNLESS a move of X's shroud equipment is
    also enumerable — then offer it plus a subset guard `SubsetHasShroudBlockedEquip`
    (twin in consider ~L6556 area + eval_and_push ~L11055 area, beside
    SubsetHasStrandedEquip) rejecting subsets that equip→X without co-selecting the
    Greaves-off-X move. Set-level achievability: a legal sequential order always exists
    (move Greaves first / equip destination before Greaves arrives).
  * Order the `equips` vector (built ~L4185-4202): non-shroud-granting first,
    shroud-granting LAST → plans apply Greaves last (directive 4d) since subset apply
    order = candidate order.
  * Jitte -1/-1 target loop (~L4644) + UA human-play target enumeration: exclude shrouded
    creatures (autonomous unaffected — opponent spawns are never shrouded).
  * Legality enforced even under open_all/human play (a rule, not a prune — the
    equip_min_power precedent at ~L4340). Legacy hatch MTG_LEGACY_SHROUD=1 = old
    (unenforced) behavior, EnvOn per conventions.
  * RISK: FiveColour also plays Lightning Greaves — enforcement may change its plans →
    smoke may legitimately move (a rules fix, GT rebaseline = user call). Watch the smoke.
- PRUNES (EquipmentProvider-owned; open under HumanPlayActive / gates):
  * Jitte modes: skip the whole JitteModeAbility enumeration (~L4634-4680) unless
    HumanPlayActive() or a new UnprunedGate::JitteMode ("jittemode" — verb already exists
    in CheckLine). Combat spend stays greedy +2/+2 (JitteSpendCount default -1).
  * UA: skip its hand-cast enumeration (Removal+tuck_to_library sites: TurnSolver L3117
    (X-variants) — the cast-collection site) unless HumanPlayActive() or new
    UnprunedGate::UACast ("uacast"). Rollout resolution branch (L8831) stays (needed for
    human-play replay).
  * Consolidation (new provider hook, e.g. EquipConsolidation() default false, Equipment
    true): rider candidates = top ds-potential host (ds now, or double_strike_while_equipped,
    or double_strike_min_equipment reachable) + Kemba (upkeep_tokens_per_equipment) when
    present (searched pair, directive 3); no ds-potential host → top rider_delta host.
    MOVES (attached_to != 0) only when current host NOT ds-now AND destination is
    ds-potential OR Kemba (high-equip-value set, amendment 2026-08-14) — applies to
    Wargear too for ds destinations (replaces its rider_open=all-hosts under the
    consolidated policy; rider_open stays under open_all); Wargear→Kemba stays excluded
    (its "free" equip sacrifices the prior host — user's stricter never-move rule stands,
    testable via the open arm). Haste-granting equips (Greaves) EXEMPT from the
    moves-only-to-high-value rule (shroud-dance/parking).
  * Greaves haste ranking under consolidation: score boost for (i) a summoning-sick
    untapped Stoneforge with a matching Equipment in hand (enables the tap-put — directive
    4b), (ii) ds-weighted largest sick creature (pw doubled if ds-potential). Width stays 2.
- MEASURE (after implementation): seed-300001 battery d0/d3/d5 + MTG_UNPRUNE=equiphost +
  MTG_EQUIP_ALL_HOSTS arms (per-game diff; user's Wargear/Kemba claims are the testable
  deltas), wall-time comparison (expect FASTER — fewer equip branches), pathological-seed
  probes (300003 gi=2 d5; 300040 gi=39 d3; 300018 gi=17 fd), 5h sweep, full smoke
  (FiveColour shroud watch). Report per-game regressions with explanations; adoption =
  user call if anything moves outside the doctrine's predictions.

**Doctrine measurement (2026-08-14, seed 300001, 100 games/depth, logs/kitty_doctrine/):**
| arm | d3 avg (wall) | d5 avg (wall) |
|---|---|---|
| OLD engine (pre-doctrine, stage-5 batchA5) | 5.02 (68 min) | 5.01 (82 min) |
| A = doctrine (rules fix + prunes) | 5.03 (5.5 min) | 5.03 (11 min) |
| B = MTG_UNPRUNE=equiphost,jittemode,uacast (rules fix on) | 5.03 (107 min) | 5.02 (151 min) |
| C = doctrine + MTG_LEGACY_SHROUD=1 (rules fix off) | 5.02 (5.2 min) | 5.02 (9.8 min) |

Perfectly additive decomposition, per-game diffs razor-thin:
- **Shroud rules fix: exactly ONE game** (gi=44, T5→T6 at both depths). The old T5 win was
  ILLEGAL — Greaves went to Puresteel Paladin on T2 and both Bonesplitters equipped onto the
  shrouded Paladin on T3. Legal play holds Greaves unequipped on T2 (one creature — the
  dance is impossible, the user's exactly-one-creature case) and equips everything T3,
  Greaves last. The legal T5 (equip all three on T2 pre-combat, swing hasted for 6) is
  blocked only by the KNOWN metalcraft enumeration conservatism (Paladin in hand at
  enumeration → Bonesplitter equips price at printed {1} → subset dies on mana): that
  already-deferred improvement now has a concrete +1-turn cost attached.
- **Doctrine narrowing: ZERO games at d3; ONE game at d5** (gi=5, T5→T6): a T1 tie-churn
  (cast-vs-hold Shadowspear) around sequencing Shadowspear after Puresteel for the
  equipment-ETB draw — holding is not pruned, so this is rollout value shift, not a
  doctrine-rule misplay. The user's Wargear/Kemba restrictions cost NOTHING measurable
  (open arm B == doctrine arm A at d3; d5 delta is the sequencing game above).
- **Wall time: ~12x faster at d3, ~7.4x at d5**; worst d5 game now 40 s (was 17–21 min —
  the old gi=32 pathological tail is gone; it is still pathological in the OPEN arm, which
  proves the doctrine prunes are what tames it).
- fd-diverge: **0** over 50 full-depth d5 games (same single unwon game 25 as stage 5).
- d0: avg identical 5.58; digest changed (prunes legitimately narrow the greedy path).
- Doctrine behavior verified in game logs: gi=29 shuttles Greaves (haste Duelist T2 →
  Kemba T3 main-1 → Balan T4 pre-combat → PARK ON KEMBA T4 main-2 → Skyhunter T5 for the
  kill); gi=4 ends parked on Kemba.

**Branching-factor work (2026-08-14, post-doctrine; USER-directed constraints):** the residual
cost is TREE VOLUME (one heavy game: 17k interior nodes → 291k rollouts → 339k simulated
turn-steps → 1.7M greedy Solve enumerations, ~90% free-equip powerset walks). USER BAR set
during this work: **no lossy truncation of the search** — a rollout-candidate beam
(`MTG_ROLLOUT_BEAM`, 2x for 1 game/100 a turn later) and an EnumGroupCap tightening (cap 6,
1.35x for a different game) were both REJECTED and the beam DELETED from source; the full
counterexample + the infinite-budget test now live in
`.claude/skills/heuristic-optimization.md` Rule 0b — read that before any future speed work
here. The escalation beam (`value_play.beam_width`) is NOT that: it is budget ordering
(search the beam to the end, then escalate to the rest) and stays default-on where
configured. ADOPTED (USER-owned directive — "we can literally only have to choose one creature for
equipping ... I specifically wanted it to cut down the enumeration space"): the AUTO-EQUIP
collapse, BOTH scopes — greedy Solve AND the decision-node enumerator (EnumeratePlans; placed
before CapGroupsBySituationalRank so the wave/tranche machinery never sees the collapsed
families) — a dominance prune (a {0}-cost equip of an unattached battlefield equipment is
strictly ≥ skipping; Wargear/Greaves/moves/hand-side/costed all stay enumerated; human play
and unpruned fully open). Measured per-game LOSSLESS (200 battery games IDENTICAL at d3+d5,
both scopes), d3 354s→280s / d5 660s→475s for the 100-game battery (~1.3x aggregate, 1.29x
on the pathological tail), fd 0/50, smoke 33/33 byte-identical (provider-gated). Off-switch
MTG_NO_AUTO_EQUIP=1. Remaining heuristic road
(USER direction — "look for heuristics"): (1) second-main dominance (post-combat candidates =
combat-acquired resources + equip repositioning only), (2) the deferred metalcraft
enumeration pricing (recovers gi=44 T5 AND improves mana-bound pruning), (3) then value-leaf
generation once per-game cost is sane.

**Viewer/log visibility fix (same session, digest-moving):** Equip / Jitte mode /
Stoneforge put / Balan attach-all were applied SILENTLY — no game-log action, no
attachment info in board snapshots, so the play viewer could not render an equipment
deck's turns at all (equipment floated free) and the play digest was blind to equip
destinations. Now: LogAbility entries for all four (folds into the digest — deliberate
fingerprint improvement; digest-moving for any deck that equips → FiveColour GT),
`attachedTo` in PermSnapshot/game JSON + `is_equip`+`attached_to` in the decision-JSON
battlefield, and both viewers (tools/play, tools/replay) group attached equipment under
its host like auras.

## Stage 6a — heuristics & assumptions disclosure

Per the search-primary core bar: the search decides; these are the PRUNES/defaults that
narrow its candidate set (each with its open switch), plus modeling assumptions.

**Search-narrowing heuristics (provider-owned, EquipmentProvider):**
| Heuristic | Narrowing | Open switch |
|---|---|---|
| Equip haste-host width | top **2** fresh/no-haste hosts per haste equipment (base default 1; measured equal to fully-open on 100 games); under consolidation Greaves ALSO always offers the Kemba park + Stoneforge tap-put enable + (when attached) its best alternative host (the shroud-dance unpark) | MTG_EQUIP_HOST_WIDTH / MTG_EQUIP_ALL_HOSTS / MTG_UNPRUNE=equiphost / human play |
| Equip consolidation (USER doctrine 2026-08-14) | rider candidates collapse to the searched pair {top ds-potential host, Kemba} (single best host when neither exists); MOVES of an attached equipment only from a non-ds host to ds-potential or Kemba (Wargear: ds only); REPLACES Wargear's all-hosts opening; Greaves exempt from the move rule | MTG_UNPRUNE=equiphost / MTG_EQUIP_ALL_HOSTS / human play |
| Jitte non-combat modes (USER doctrine 2026-08-14) | -1/-1 and gain-2 mode enumeration pruned entirely from autonomous search (combat greedy +2/+2 spend is the only outlet) | MTG_UNPRUNE=jittemode / human play |
| Unexpectedly Absent cast (USER doctrine 2026-08-14) | hand-cast enumeration pruned entirely from autonomous search | MTG_UNPRUNE=uacast / human play |
| Equip rider width | top **3** hosts by P/T-lifelink-charge delta per equipment; Grafted Wargear opens ALL benefiting hosts (search must weigh the re-host sacrifice) — superseded by consolidation for this provider | MTG_EQUIP_RIDER_WIDTH / same opens as above |
| Board-lethal short-circuit | when attack-all already kills this turn, skip the cast-subset odometer (win-turn-invariant; 99/99 A/B identical) | MTG_NO_LETHAL_CUT=1 |
| Skyhunter dig put | provider ranks examined cards by granted power, puts top | human play: full `dig` chooser (any legal card or decline) |
| Skyhunter attach host | delta-greedy over ATTACKERS (ds-aware, min-power filtered) | human play: `attach_host` chooser over ALL creatures, or decline |
| Jitte combat spend | greedy spend-all (incl. double-strike mid-step earnings) | `--jitte "turn:count"` / `--jitte-prompt` (viewer) |
| Jitte -1/-1 targets | opponent creatures only in autonomous search (own-creature targets strictly bad vs passive opponent) | human play enumerates own creatures too |
| UA (Unexpectedly Absent) X | X=0 only, opponent-creature targets only (user: "not much upside to searching this") | human play / MTG_UNPRUNED: 0..max X, any nonland permanent incl. own (allow_self_target) |
| Stoneforge put | one PutFromHandAbility variant per distinct Equipment name in hand | (complete by construction — name variants cover the space) |
| Balan attach-all | enumerated only when ≥1 movable equipment exists | (legality gate, not a narrowing) |

**Modeling assumptions / conventions (user-approved 2026-08-13):**
- Inert combat keywords vs the passive opponent: flying/trample/first-strike carry no
  goldfish effect; Jitte "combat damage" collapses to "to a player" (counter gain IS
  modeled, incl. the double-strike first-half-earns/regular-half-spends closed form).
- Shadowspear's {1} strip ability deferred (opponent has no hexproof/indestructible).
- Puresteel "may draw" always taken (strictly good; fires in BOTH executor and rollout via
  the universal enter cascade — the rollout side was the fd-diverge fix).
- Metalcraft equip cost is baked at enumeration and RECOMPUTED at every payment site;
  same-turn artifact-count flips under-offer (never overcharge) — conservative.
- Kemba token color/type, Stoneforge reveal, instant-speed collapses, Skyhunter trigger
  order + random bottoming, O-Naginata SBA skip, dead-Aura path: per approved conventions.
- Opponent 4/4 spawns exist as removal targets only (standard goldfish apparatus).
- Shroud (RULES FIX 2026-08-14, not a heuristic): equip_grants_shroud now blocks the
  controller's own TARGETED attach/targeting — equip hosts (CR 702.6b), Jitte -1/-1
  targets, removal retargets, and the human-facing own-creature target lists. Balan
  attach-all / Skyhunter attach-dig do not target (user-confirmed) and stay legal.
  A shrouded host stays enumerable only alongside the Greaves-off move
  (SubsetHasShroudBlockedEquip, both subset walkers); shroud-granting equipment
  enumerates LAST so multi-equip plans linearize legally (doctrine 4d). Enforced even
  under open_all/human play. Off-hatch MTG_LEGACY_SHROUD=1.

## Cost re-baseline at HEAD + the greedy-Solve memo (2026-08-19)

Picking the "remaining heuristic road" back up five days later, step 0 was to re-measure the deck
at HEAD rather than trust the numbers above — 299 engine commits had landed in between.

**Play is unchanged; cost drifted +27%.** d3 battery (100 games, seed 300001), HEAD vs the last
kitty commit `ce18c788` built in a worktree:

| | avg | play digest | per-game wall sum | search units |
|---|---|---|---|---|
| ce18c788 (2026-08-14) | 5.0300 | 3e6ea44e9c15d572 | 631,894 ms | 31,658,412 |
| HEAD (2026-08-19) | 5.0300 | 3e6ea44e9c15d572 | 815,566 ms | 40,303,111 |

Byte-identical play across 299 commits — every verdict in this ledger still stands. The +27% is
spread broadly and lands hardest on the tail (gi=86 1.77x, gi=72 1.60x, gi=4 1.73x); no single
commit was bisected for it because the *level* turned out to be the actionable thing, not the
drift. Two flags suspected up front were measured INERT here (`MTG_ACQ_RESOLVE=0` and
`MTG_COLOR_EXACT=0` both reproduce the digest at the same cost). The equip prunes are alive and
load-bearing: `MTG_EQUIP_ALL_HOSTS=1` costs **6.7x** on d3 gi=16 (76.5 s -> 512.6 s), while
`MTG_NO_AUTO_EQUIP=1` is inert on that particular game.

**Where the cost is (MTG_CONSIDER_STATS, d3 gi=16, one game):** 2,917,908 harvests /
24,229,640 action considerations, and the leader is not main 1 —

| site | calls | actions | distinct states | dup calls |
|---|---|---|---|---|
| `solve.m2.fs3.m2solve` | 1,848,874 | 13,382,396 (55%) | 249,476 | **1,599,398 (86.5%)** |
| `solve.m1.fs3` | 769,288 | 8,290,293 (34%) | 59,404 | 709,884 |

The **greedy second main inside the search** is the single biggest consumer on this deck, and
86.5% of its calls re-solve a state a sibling line already solved in the same decision. Its
per-card table is the deck's equipment being re-enumerated post-combat over and over (Bonesplitter
4.24M considerations, Colossus Hammer 3.22M, Balan 1.81M).

**ADOPTED TRANSITIONALLY — `MTG_SOLVE_MEMO` default-ON** (the collapse #1 memo built 2026-08-14
and parked default-off in `single-consideration.md` "awaiting the user's call"; `=0` restores the
old path). USER, 2026-08-19: *"not the end of the world to adopt temporarily, but the goal is to
drop greedy entirely"* — every call this memo saves is a GREEDY solve inside the search, so it
buys time on today's cost and dies with the path it serves. It is not evidence for keeping the
greedy second main; the deck-level question that matters is the one below (can Kitty run on a
SEARCHED second main at all):

* Identity: VERIFY mode on d3 gi=16 recomputed **all 813,149 hits** uncached — **0 mismatches**.
  The d3 battery reproduces avg 5.0300 / digest 3e6ea44e9c15d572 in every arm (off, on, cap
  16k/64k/256k, six repeat runs). Suite smoke 36/36, 0 configs changed, 0 play-changed, twice.
* It cannot move a BUDGETED search by construction: `SearchBudget` meters rollout turn-steps, and
  greedy-Solve re-enumeration spends none. Measured — the per-game unit totals are identical to
  the byte (40,303,111 in both arms).
* Cost: KittyEquipment d3 **1.28-1.30x** cheaper (minima over six alternating runs). Suite smoke,
  per-case minima over two alternating reps: **1.111x** aggregate — hinata d3 1.79x, hinata d5
  1.26x, dragonstorm d3 1.26x, mirrorwing d5 1.14x, fivecolour d5 1.10x, against 0.91-0.95x on
  th d3 / creature_giving d3 / auras d3.
* Cap sweep (`MTG_SOLVE_MEMO_CAP`, new lever): 16k = 340 MB peak RSS, 64k = 950 MB, 256k =
  2.83 GB — and the extra memory buys nothing (256k was the slowest memo arm). Default stays
  16384.
* This does NOT contradict `fivecolour-payment-query-fold.md`'s "worth ZERO at HEAD": that was
  FiveColour at its shipped d6/budget-20 `value_play` config. At explicit search depths, and on a
  deck whose greedy second main dominates, it pays.

**TRAP recorded for the next agent — the work meter is blind to this cost.** `GameWorkMeter`
units did not move at all across a change worth 1.3x of wall clock, because greedy Solve
enumeration is unmetered. Anything that sizes, abandons, or condemns a game by units (the batch
per-game ceiling, the depth matrix) will therefore under-price a greedy-Solve-dominated game.
Wall clock is the only meter for this class — and on this box it is contended (the same battery
measured 806 s and 1,528 s in the same session), so **alternate the arms and compare minima**.

**Still open on the road:** (1) second-main dominance (the memo removes the *repeats*; the 249k
distinct m2 states per decision are still enumerated over the whole hand), (2) the deferred
metalcraft enumeration pricing, (3) value-leaf generation. New datapoint for (3): d5 gi=20
(`--seed 300021 --game-index 20 --depth 5`) ran **over 80 minutes without finishing** at HEAD,
both with and without the memo — a pathological game the 2026-08-14 tail-taming does not cover,
and the reason a d5 battery needs an abandon ceiling before it is run again.

## Road item (1) — second-main dominance: MEASURED, and the answer is NO (2026-08-19)

USER direction for this pass: *"we need to limit the search to productive options and skip it for
unproductive ones"* — and, on the searched second main, *"not practical without the sorted pruning
strategy I have been working through with the other agent"*. So the question asked here was not
"greedy or searched" but "is the post-combat main productive at all on this deck".

**Nothing about the second main changes how this deck plays.** Four d3 arms, 100 games, seed
300001 — control, `MTG_SEARCH_SECOND_MAIN=1`, `MTG_PHASE_CLASSIFY=1`, and both — all returned
avg **5.0300** and play digest **3e6ea44e9c15d572**. Identical. Not greedy-vs-searched, not the
pre-combat classification filter. (Classification being inert is expected: almost every card in
this deck is `is_equipment` or a body, i.e. Main1 by the base rules.)

**What the second main PRODUCES** (`MTG_M2_YIELD_STATS`, new diagnostic, d3 gi=16): 1,963,498
second-main solves, **960,454 of them (48.9%) return an empty plan**, 1,297,615 actions returned in
total. Of those actions ~70% are hand casts (Kor Duelist 325,907, Bonesplitter 266,248, Colossus
Hammer 177,957) and ~30% equips (Bonesplitter equip 326,573 is the single largest). Zero land
drops (the m2 drop lever is off).

**Upper bound — delete the in-search second main entirely** (`MTG_NO_M2_SOLVE=1`, a measurement
lever): **exactly ONE game of 100 changes** (gi=7, T5 -> T6); the other 99 are digest-identical.
And gi=7 is not a lost line — the two arms diverge at **T1** (control holds Shadowspear, the arm
casts it), i.e. a rollout-value shift, the "disposition flip" class already documented in
`main-phase-classification.md`. The winning T4 main-2 Colossus Hammer cast in the control line
happens three turns after the divergence.

**The gate, built and measured** (`DecisionProvider::SkipsUnproductiveSecondMain`,
`MTG_M2_PRODUCTIVE=1`, default OFF everywhere including EquipmentProvider): skip the in-search
post-combat main on a turn where combat created no resource — hand and battlefield both unchanged
across combat (`GameState::hand_size_at_combat` / `battlefield_at_combat`, stamped by
SimulateCombat, reset at turn start, folded exact into `Dominance.h`'s comparator per its
maintenance-hazard discipline).

On this deck the gate is **exactly equivalent to deleting the second main** — per-game wins and
digests identical to the `MTG_NO_M2_SOLVE` arm across all 100 games. Combat never creates a
resource here: the Armored Skyhunter attack-dig-attach that makes this a `DeckUsesSecondMain` deck
in the first place effectively never fires in these games.

Cost, three alternating reps on a quiet box (minima): base **598,196 ms** vs gate **569,329 ms** =
**1.051x**. **VERDICT: NOT ADOPTED.** 5% of runtime is not worth a real game, and it runs against
the standing USER bar on lossy truncation. The machinery stays default-off with this verdict
attached, because the gate is deck-agnostic and the decks where combat DOES create a resource
(Goblins' Lackey put, burn's spectacle, Two-Headed Hellkite's attack draw) are exactly where it
could pay — it has simply never been measured there.

> **SCOPE CORRECTION (2026-08-19, same day, after the USER's ability-order ruling below).**
> Everything above is measured against the doctrine AS IT STOOD, in which the post-combat main has
> essentially no job on this deck. The Kemba park ruling gives it a recurring one — park the free
> gear on Kemba at the end of main 2, every turn she is out — and those are precisely the turns on
> which combat created nothing. So `SkipsUnproductiveSecondMain` would skip exactly the turns the
> park needs, and **it must not be adopted**; the "one game in 100" figure is a fact about the
> pre-ruling doctrine, not a standing property of the deck. Re-measure it after the park lands if
> anyone wants the number again.

**Why the consideration counts pointed the wrong way — and the profile that settles it.** The
`MTG_CONSIDER_STATS` table above makes the second main look like the whole problem: 55% of all
action considerations, 50% even after the memo. It is worth **5% of wall time**. A per-call cost
difference (m2 harvests average 7.3 candidates against main 1's 10.5, with no land axis and no
breakpoint machinery) is the whole gap. This is `profile-before-optimizing` in one deck: a
component-internal ratio is not a share of runtime.

The actual profile (`perf record -e cpu-clock -F 999`, d3 gi=16, self time):

| symbol | self |
|---|---|
| `TurnSolver::SolveUncached` | **38.85%** |
| `EnumeratePlans` | 6.71% |
| `CollectActions` (+ its lambda) | 6.72% |
| `operator new` | 2.66% |
| `BuildSimKey` | 2.23% |
| `ComputeLordBonus` | 1.88% |
| `SubsetHasStrandedEquip` | 1.33% |

So greedy `Solve` really is the deck's cost centre at ~39% — but the second main is only ~5 points
of it. **The remaining ~34 points are the greedy MAIN-1 solve in rollout interiors**
(`solve.m1.fs3`: 631,338 calls / 6.66M considerations even with the memo on). That is where the
"productive options" question should be aimed next on this deck, and it is the same greedy the
no-greedy-in-the-search directive targets.

**PROFILING TRAP (cost me one useless run):** `mtg` resolves `src/cards/data/cards.json` relative
to the CWD and **silently proceeds with an EMPTY card database** if it is missing. Profiling from
`/tmp` (because perf cannot write into the 9p-mounted `/workspaces`) produced a game that finished
in 6,099 solve calls instead of 2.79M and looked like a successful run. Run from the repo root and
send only perf's OUTPUT to /tmp (`-o /tmp/x.perf`). Also: perf hardware counters are `<not
supported>` under WSL2, but the `cpu-clock` software event samples fine.

## The USER-reviewed ORDERED-SEARCHED package (review held 2026-08-19) — BUILT, NOT MEASURED

Direction, verbatim: *"we drop the greedy solves entirely and follow the proper design for
KittyEquipment"*, *"we need to limit the search to productive options and skip it for unproductive
ones"*, and on the searched second main *"not practical without the sorted pruning strategy I have
been working through with the other agent"*.

**LIVE (default-on, changes play):** `EquipmentProvider::SearchesSecondMain()` — the greedy
post-combat solve is dropped for this deck via a new per-deck hook, the adoption route the
suite-wide `MTG_SEARCH_SECOND_MAIN` lever never earned. Free here: four d3 arms x 100 games
(greedy / searched / classified / both) all return avg 5.0300 and digest 3e6ea44e9c15d572. Kill
switch `MTG_NO_SEARCH_SECOND_MAIN=1`. **Its battery + smoke re-verification was cut short to free
the box for another agent — argued and built, not re-verified.**

**DEFAULT OFF, byte-identical off, NOT YET MEASURED:**

| lever | what it turns on |
|---|---|
| `MTG_KE_ORDER` | the reviewed cast order (Paladin 6 -> Stoneforge 7 -> equipment 8 -> hosts 10 -> removal 30/m2) + `OrderOpaqueCastsByRank` |
| `MTG_KE_PARK` | the Kemba loop: park free gear on Kemba in main 2, un-park it onto the double-striker in main 1 |
| `MTG_EQUIP_MINPOWER_LAST` | O-Naginata equips last-but-before-Greaves, and the emission veto measures REACHABLE power |

The cast-order half and the ruling behind it live in `cast-order-rankings.md` under
KittyEquipment. The ability-order half is here:

**Ability order (USER ruling).** *"For most equipping it can go just before the attack phase. Only
Lightning Greaves has some special rules where it can be used to activate abilities. The Kemba park
is another special case at the end of Main 2, and technically you want to equip her with everything
that is free to equip and doesn't have a drawback like Grafted Wargear. Lightning Greaves should be
last, because of shroud."* Three of the four already held and were verified rather than rebuilt:
equips run in a trailing pass after every cast (so main-1 equips land immediately before combat with
the final board and final metalcraft count known); the equips vector is sorted shroud-granting last;
and `ApplyManaUnlockEquips` already fires a haste equip mid-casts when it unlocks a later action.

**The park is half a LOOP, and forcing only that half would be a trap.** USER: *"it also will often
mean that we need to re-equip a creature the next turn ... at least if there is a double striker on
board"*, and *"in goldfish there is literally no drawback to doing this"*. Kemba's upkeep makes a Cat
per Equipment attached, so: main 2 park (Cats at upkeep) -> main 1 un-park onto the double-striker
(damage in combat), both {0} under metalcraft, and in this apparatus artifacts never leave so
metalcraft once on stays on. A parked Bonesplitter that never returns is a double-striker attacking
NAKED — strictly worse than never parking. The un-park would otherwise be a SEARCHED move (the
auto-equip collapse excludes attached equipment, because a PRE-combat move trades away a rider about
to attack), droppable by a breadth cap and adding one move-group per equipment to every main-1
enumeration. So both halves are forced on one flag: enumeration stays flat, and the failure mode
cannot occur. One predicate (`KembaLoopKind`) serves the emission keep-list and both subset walkers'
auto-take, so the three cannot drift.

**Un-park guard, both halves of the USER's condition** (*"a creature that can attack with all of the
equipment on it ... which might be any doublestriker if Lightning Greaves is out"*): the target must
be a double-striker AND able to swing. `CanTapNow` alone is WRONG here — it only sees a Greaves
already attached, and under this very doctrine the Greaves spent the night parked on Kemba — so a
summoning-sick double-striker still qualifies when a free haste granter can reach it this turn.

**O-Naginata** (*"should be equipped before Lightning Greaves, but last otherwise, so the power is
okay"*): needed TWO changes, because the ordering alone is a no-op. The equips vector gained a third
class (ordinary -> power-gated -> shroud), AND the emission veto in `rider_delta` — which refused the
pair outright against the host's CURRENT power, so the late slot could never be used — now measures
against the power the host can still REACH this turn. That optimism is safe because `ApplyEquip`
re-checks `equip_min_power` at attach time and silently declines: worst case a wasted no-op, never
an illegal attach. The rule is not bypassed, it is evaluated where it can be known.

**THE MEASUREMENT OWED.** One pooled battery over the four arms plus control, d3 (and d5 with an
abandon ceiling — see the gi=20 note above). And per the standing lesson, the park arm CANNOT be
judged on avg turns alone: **verify the loop ROUND-TRIPS**, park then un-park, by turn. A park that
never un-parks would barely move the mean while being a clear misplay — exactly the failure the
"measure the BEHAVIOUR, not just the outcome" rule exists to catch.

## The ordered-searched package — MEASURED (2026-08-19). Nothing moves; one lever is defective.

ONE pooled batch, 13 jobs / 1,812 games, 23.8 of 24 cores busy start to finish. Six arms (control,
`greedy`, `order`, `park`, `nagi`, all-three) x two disjoint seed blocks x 150 games, plus a 12-game
identity cell. The levers ride the batch as per-job `flags` (`src/ai/HeuristicArm.h`) rather than
process env, which is what let all six arms share one queue and one tail instead of six waves.

**Apparatus checks passed before any arm was read.** A manifest with no `flags` block reproduces the
known HEAD fingerprint exactly (d3 x100 seed 300001 -> avg 5.0300, digest `3e6ea44e9c15d572`); and
each lever set per job gives the *same digest* as the same lever set as process env (12 games each:
`MTG_KE_ORDER` `b6566d9ae30fb845`, `MTG_EQUIP_MINPOWER_LAST` `e64637d930194efc`, `MTG_KE_PARK` and
`MTG_NO_SEARCH_SECOND_MAIN` both `a8343b3dae5fdeca`). That second check is the one that matters: a
per-job override whose name does not match its `EnvOn()` call site would parse, set, and shadow
nothing, so the arm would run the BASELINE while the report said the lever was on.
`ValidateHeuristicArmNames()` now aborts at startup on exactly that mismatch.

### The play result: every lever changes play, none changes the clock

| arm | plays differ (train / hold) | games faster | games slower | delta avg win turn |
|---|---|---|---|---|
| `greedy` (force greedy m2) | 0 / 0 | 0 | 0 | +0.0000 |
| `order` (`MTG_KE_ORDER`) | 32 / 33 | 0 | 0 | +0.0000 |
| `nagi` (`MTG_EQUIP_MINPOWER_LAST`) | 9 / 5 | 0 | 0 | +0.0000 |
| `park` (`MTG_KE_PARK`) | 4 / 0 | 0 | 0 | +0.0000 |
| `pkg` (all three) | 41 / 37 | 0 | 0 | +0.0000 |

Not "no significant difference" — **no difference**. The per-game win-turn column is byte-identical
to control for every arm on both blocks, verified by diffing the `.wins` files directly, while the
digest column differs in up to 41 of 150 games. The levers demonstrably change how this deck plays
and demonstrably never change when it wins. Distribution (control, train): 4 games T3, 47 T4, 65 T5,
25 T6, 5 T7, 1 T8, 3 unwon. The goldfish clock here is set by the mana and draw curve, and equipment
ordering does not touch it.

**This closes the re-verification owed for the LIVE flip.** `SearchesSecondMain()` is inert on play:
0 games differ over 300, digest-identical on both blocks. Dropping the greedy post-combat solve for
this deck costs nothing, as argued.

### The cost result, in deterministic units (wall clock cannot answer this here)

Job `ms` is WALL, and this box has measured the same workload at 16.5 s and 48.9 s depending on load,
so the ~5-15% swings in per-job ms are noise. Work units are the search's own node counter: identical
inputs give identical units at any load, so the paired per-game ratio below is exact.

| arm | train ratio | hold ratio | reading |
|---|---|---|---|
| `greedy` | 0.9879 +/- 0.0014 (134 cheaper / 0 dearer) | 0.9897 +/- 0.0016 (130 / 1) | ~1.1% fewer nodes, near-unanimous |
| `order` | 0.9945 +/- 0.0042 (69 / 59) | 0.9997 +/- 0.0008 (68 / 69) | wash, sign flips |
| `park` | 0.9984 +/- 0.0015 | 0.9967 +/- 0.0013 | ~0.2-0.3%, mostly inert |
| `nagi` | 1.0024 +/- 0.0014 (16 / 28) | 1.0033 +/- 0.0034 (24 / 30) | consistently ~0.3% DEARER |
| `pkg` | 0.9954 +/- 0.0047 | 0.9996 +/- 0.0037 | wash |

So the cast order buys no tractability either — which matters, because tractability was its stated
purpose (*"the searched second main is not practical without the sorted pruning strategy"*). On this
deck, at this depth, it is not needed: the searched second main is affordable without it.

**CAVEAT on the `greedy` row, and it is not a small one.** Units count INTERIOR NODES, not CPU per
node. `greedy` vs control is the one cross-STRATEGY comparison in the table -- a greedy solve and a
searched second main do different work *inside* a node -- so the meter prices only part of the
difference, and it under-counts precisely the greedy arm's own work. Read it as "the searched second
main opens ~1.1% more nodes", NOT as "greedy is 1.1% cheaper". The earlier `perf` profile is the
better guide to real cost: the second main is ~5% of runtime either way, and the deck's actual cost
centre is the greedy MAIN-1 solve in rollout interiors (~34 of `SolveUncached`'s 38.85%).

### `MTG_KE_PARK`: CORRECTED — the loop is RIGHT, it just almost never gets a turn

**This supersedes the "DEFECTIVE" verdict committed in e6897884, which was wrong.** That call rested
on 4 replayed games: 8 park events, 0 round-trips, and a Kor Duelist on board in both games where
gear was still on Kemba the next turn. What it did not check is whether those events had any
RUNWAY — and none did. In all 8 the following turn was either the turn the game was won or did not
exist. A loop cannot close on a turn that does not happen, and closing it on a turn already won
changes nothing, so those 8 events were evidence about neither half of the loop.

Re-measured on a sample big enough to contain runway — 600 games, seed block 500001, d3, every game
logged and its board snapshots walked:

| | park events |
|---|---|
| total | 212 (in 66 of 600 games) |
| **no runway** (next turn is the win, or absent) | **190 (90%)** |
| with runway | 22 |

And of the 22 with runway, cross-tabulated against whether a double-striker was on board — the
USER's stated condition (*"at least if there is a double striker on board"*):

| | double-striker out | none out |
|---|---|---|
| round-tripped | 4 | 4 |
| stayed parked | **0** | 14 |

**The misplay category is empty.** Every time a double-strike host was available the gear came back;
every time it stayed on Kemba there was nothing to return it to, which is the correct play — the
Cats are free and no rider is being starved. The un-park half fires, and the guard discriminates on
exactly the condition it was built to discriminate on.

So the lever is doctrinally correct and it is also nearly pointless HERE, for a structural reason
worth recording: parks land on T4-T6 (53/90/53 of 212) and the deck wins on T4-T6, so 90% of the
time the game ends before the loop can close. The Kemba park needs a game that lasts at least one
turn past the park, and this deck usually does not provide one.

The park half is genuinely working where it does fire, and pays: seed 300050 parks Bonesplitter +
Lightning Greaves on Kemba in T5 main 2 and collects **2 Cat tokens** at the T6 upkeep.

`MTG_KE_PARK_STATS` (kept) reports the predicate and both walkers separately, which is what showed
the mechanism was live before the runway question was settled: on seed 300050, predicate PARK 51,052
/ UNPARK 27,049, forced greedy PARK 35,188 / UNPARK 3,663, forced ENUM PARK 1,333 / UNPARK 987. The
un-park is recognised and force-selected in both the greedy solver and the search's candidate plans.

**Method note, because this cost a wrong verdict.** "0 of 8 round-trips" and "0 of 8 round-trips
where the loop could possibly have closed" are different claims, and only the second is a finding.
Any loop-closure instrument must report the denominator it is entitled to use FIRST; the analyzer in
`test/tools/kitty_ab/park_roundtrip.py` now excludes no-runway events from the rate and prints them
as their own line.

### Verdicts

| lever | verdict |
|---|---|
| `SearchesSecondMain()` (LIVE) | **re-verified, keep.** 0 of 300 games differ; digest-identical both blocks. |
| `MTG_KE_ORDER` | **free, but buys nothing measurable** -- no play effect, no cost effect. Adoption is a doctrine call (the USER reviewed and endorsed the order), not a measurement one; the apparatus cannot distinguish it. |
| `MTG_EQUIP_MINPOWER_LAST` | **no play effect, ~0.3% dearer in nodes on both blocks.** Nothing argues for default-on. |
| `MTG_KE_PARK` | **CORRECT but worthless here.** Loop verified: 0 misplays in 22 runway events, gear returns whenever a double-striker is out. But 90% of parks have no runway and it changes 0 of 300 games. Adopt only as doctrine, not for value. |

Reproduce: `logs/kitty_ab/gen_manifest.py` (manifest), `compare.py` (paired win turn), `cost.py`
(paired units), `park_roundtrip.py` (loop closure from game JSON).

## Do the levers DO what was ruled? (behavioural check, 2026-08-20)

The value question is settled — nothing moves the clock — so the only question left for the two
order levers is whether they implement the USER's rulings. That is a behavioural question, and the
park taught the lesson that "changes play" is not an answer to it. 600 games per arm, seed block
500001, d3, every game logged; `test/tools/kitty_ab/order_behaviour.py`.

### Cast order (`MTG_KE_ORDER`): fully enforced

Counting adjacent cast pairs within a turn+phase and asking whether a later-ranked card was ever
cast before an earlier-ranked one:

| | control | arm |
|---|---|---|
| adjacent cast pairs | 575 | 573 |
| order INVERSIONS | 114 | **0** |
| inversion rate | 19.83% | **0.00%** |

The reviewed order is strict, exactly as the ruling asks (*"isn't the point that it should be a
strict ordering"*). One in five adjacent casts was previously out of the reviewed order; now none is.

### O-Naginata (`MTG_EQUIP_MINPOWER_LAST`): the ruling's INTENT is fully met

Grouped per host per turn, since the ruling is about the order gear lands on ONE creature:

| | control | arm |
|---|---|---|
| host-turns equipping O-Naginata | 73 | 85 |
| ...after the ordinary gear (WANT) | 9 | **41** |
| ...before it | 13 | **4** |
| ...before Lightning Greaves (WANT) | 5 | 7 |
| ...after Greaves (violates) | **0** | **0** |

It is also equipped MORE often (73 -> 85 host-turns), which is the reachable-power veto relaxation
doing its job: slots that the old CURRENT-power veto refused are now usable.

**The 4 residual "violations" were inspected individually and are cosmetic.** In every one the host
already carried gear from an earlier turn (e.g. game_597: Bonesplitter went on the 2-power Puresteel
Paladin on T3, so by T4 its power was 4), so O-Naginata going on first still satisfies the ruling's
stated reason — *"so the power is okay"*. This is guaranteed, not just observed: `ApplyEquip`
no-ops below `equip_min_power`, so an attachment that shows up in the board snapshot is legal by
construction. Net: **0 cases where O-Naginata landed on a host under power 3, and 0 where it landed
after Greaves, in either arm.**

The lever also removed the single O-Naginata phantom equip present in the control (1 -> 0).

### Side finding: a dropped cast can strand a co-selected equip

Not a lever issue — identical in control, order arm and park arm — but found by the same sweep and
written up in `docs/design/equip-host-not-on-battlefield.md`. 2 of 1,270 equips (0.16%), 2 games in
600; signature in a game log is an ability string `equip -> #<number>` instead of a host name.

The first diagnosis (a missing guard) was wrong: `SubsetHasStrandedEquip` already covers this and
passed correctly, because the subset DID contain the host's cast. What happens is that the cast is
then dropped at apply time as unpayable — which is deliberate, documented behaviour — and the equip
it was co-selected against runs anyway, pays, and no-ops. `MTG_AFFORD_AUDIT` on this deck: **33 of
4,006 executed casts (0.82%) are dropped, every one COLOUR-short and none total-short**, so no cast
order can fix them. The audit's existing STRANDED detector covers stranded ACCELERANTS (0 here,
correctly) and does not cover a stranded equip host — extending it is the cheap next step.

## Road item (2) — metalcraft enumeration pricing: FIXED, and the ledger's diagnosis was half of it (2026-08-20)

The deferred item read *"price equips at post-subset artifact count"*, against the measured
claude-play gap (2 of 16 games, ~1 turn each: cast 2–3 artifacts to flip metalcraft ON, then equip
Colossus Hammer for `{0}`). That pricing fix is now built — and **on its own it changes nothing at
all**: 300 paired d3 games came back digest-identical, 0 of 300 differing in any decision.

The reason is a second mechanism the ledger did not name. `ManaPruneBound` — the odometer's scalar
mana ceiling — skips a subset position whose summed `cost.ManaValue()` exceeds the turn's mana
BEFORE `consider()` prices anything, and that sum charges the Hammer its printed `{8}`. The line was
never mispriced at the gate; **it never reached the gate.** The funnel, on reproducer seed 70014
(`MTG_METALCRAFT_STATS`):

| | with pricing only | with both halves |
|---|---|---|
| enumerations offering a Colossus Hammer equip | 4,092 / 4,092 | 614 / 614 |
| ...its group surviving the breadth cap | 4,092 | 614 |
| subsets `consider()` built holding one | **0** of 89,492 | 32,506 of 109,612 |
| ...surviving the legality rejections | 0 | 6,665 |
| ...passing the affordability gate | 0 | 1,302 |
| win turn | 5 | **4** |

`ManaPruneBound` documents this hazard on itself (*"any FUTURE cost reducer that is credited
per-subset in `consider()` rather than baked into `a.cost`"*) and the haste-dork unlock already
carries the matching `extra_credit` addend — so the fix is two lines, and was already written down
as an obligation by the function that needed it.

**Both claude-play reproducer games (seeds 70014 / 70015, d5) now win on T4 instead of T5** — the
pilot's own result — on verbatim the human's line (`Colossus Hammer + Shadowspear + EQUIP Colossus
Hammer`). Paired d3, 150 games per block:

| block | control | arm | delta | se | t | faster | slower | plays differ |
|---|---|---|---|---|---|---|---|---|
| train (300001) | 4.9667 | 4.8533 | **−0.1133** | 0.0260 | −4.36 | 17 | **0** | 51 |
| hold (900001) | 4.9600 | 4.8200 | **−0.1400** | 0.0300 | −4.67 | 22 | 1 | 58 |

The held-out block improves MORE than train, which is what a real effect looks like rather than a
selection artifact. `MTG_METALCRAFT_CREDIT`, default OFF pending the adoption call; only Puresteel
Paladin carries the param and only this deck plays it, and this deck is not in the regression tiers,
so **no suite ground truth can move**. Full write-up: `docs/design/metalcraft-enumeration-credit.md`.

**Method note worth keeping.** When the price half measured *exactly* zero — not small, zero — the
useful question was not "is the credit worth anything" but "are the subsets it rescues ever built at
all". The answer was 0 of 89,492, and it pointed straight at the prune. A lever that fires
constantly (320,672 credited subsets per 20 games) can still be rescuing only the shape that does
not matter — here `Sol Ring{1} + EQUIP Bonesplitter{1}`, never the Hammer.
