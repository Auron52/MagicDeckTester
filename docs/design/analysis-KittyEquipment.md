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
