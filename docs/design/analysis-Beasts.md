# Analysis ledger — Beasts

Started 2026-11-21. Worktree `.claude/worktrees/beasts`, branch `beasts-analysis`, based on
`origin/phase-1-2-deck-analyzer` @ c9a2e1ff6. Deck file `decks/Beasts/Beasts.cod` (copied from the
primary checkout's untracked `decks/Beasts/`, original preserved under
`logs/beasts/primary-deck-backup-20261122-1703/`). H+I landed on the shared branch as `c841cd59c`;
batch J remains local while its analysis and verification finish.

## Checkpoint (update after every milestone)

* **Last completed stage:** Stage 3 coverage recheck clean; Stage 2 research drafts 13/13 (ALL) in
  `logs/beasts/drafts/*.md` (Thragtusk, Contested Cliffs, Packleader, Cragplate Baloth, Krosan
  Warchief, Questing Beast, Elder Gargaroth, Garruk's Harbinger, Kaheera(+Cavern recheck), Migloz,
  Nykthos).
* **Baseline build:** done, rc 0 (Release, worktree base c9a2e1ff6).
* **Batch A integrated:** A1 `375a50399`, A2 `53e68dcd3` (handoff `logs/beasts/integrator-A-handoff.md`;
  14/14 batch-A unit tests pass, cost audit rc 0). Includes the Herald dup-legend upside in
  `DuplicateCastHasUpside`. A tail still owed: full unit suite, `audit_card_fields.py --update`,
  smoke byte-identity (no accept).
* **Batch A tail DONE (integrator B, 2026-11-21):** full unit suite (2 batch-A census failures fixed:
  kicker count 5 with Cragplate Baloth; decks/Beasts pinned to its INTERIM Boars route in the Pirates
  routing table -- batch J re-pins it), `audit_card_fields.py --update` + offline rc 0, snapshot committed,
  `audit_viewer_decisions.py --no-sweep` clean.
* **Batch B integrated (Contested Cliffs):** NEW PermAbilityMode::TapFight + shared ResolveFight (deathtouch
  honoured -> Questing Beast's bracket is true; immediate SbaKillDeadCreatures pass fires Thragtusk's LTB and
  the Packleader draw), both targets searched, emitted in main 1 and any main 2, NOT in DeckUsesSecondMain.
  6 new unit tests. See the integrator B commits on this branch.
* **Open gap (batch B, surfaced):** a Packleader draw off a fight-death token in main 1 lands in the
  trailing pass, so that card cannot be cast the same main (no breakpoint re-solve for a trailing-activation
  draw). Covered by the per-turn second-main gate planned for the provider batch.
* **Smoke byte-identity (batches A + B, at 27e1f4892, no accept):** 235/235 PASS, per-game audit 0 configs changed, play-changed 0 (searched and d0). Log `build/scratch/beasts_smokeB.log` (build volume).
* **Batch C integrated (Elder Gargaroth, integrator C 2026-11-21; see the batch C commit on this branch):** NEW
  `attack_choose_one` + `attack_choose_token_*` / `attack_choose_lifegain` / `attack_choose_draw` (NOT
  `attack_trigger_modal`). Shared `ApplyAttackChooseOneTriggers` (SpellEffects.h) called right after
  `ApplyAttackModalTriggers` in GameEngine::CombatPhase AND the rollout's SimulateOneCombat (lockstep), per
  DECLARED copy. Searched pin `Plan::attack_choose_mode_choice` -> `GameState::scripted_attack_choose_mode` (int16,
  2 bits/ordinal, spent after the loop, cleared at both turn starts). Axis on the Tectonic pattern in
  AppendSubdecisionAxes (after Thunderkin), product cap 27, skipped under HumanPlayActive and on a dead post-combat
  list. Life fold = root `DecisionProvider::AttackChooseOneModes` (token, draw, life; life dropped when the NEW deck
  stamp `deck_life_inert` && !own_death_live; `MTG_UNPRUNE=attackchooselife` restores it). Folded into
  Dominance::Build, BuildDedupKey combat pins (new LAST slot), plan-cache scalar key (value-gated), BpCandFingerprint,
  solvememo, PlanDomCensus, RouteSig, every axis skip list. sizeof(GameState) 1712 -> 1720, Plan 784 -> 792 (asserts updated).
  Viewer: attack_mode widened to 3 options (main.cpp clamp 0..2 for an attack_choose_one source + Gargaroth JSON
  branch; index.html default reads any listed index); DECISIONS.md row; audit manifest. 8 new unit tests, all pass;
  full unit suite 3843/3843. Viewer audit `--no-sweep` clean. Smoke NOT run (next batch runs one pass for C + D).
* **Open gap (batch C, surfaced):** a card drawn by Gargaroth's draw mode (or Packleader off its token) arrives in
  combat; the deck has no searched second main (NOT added to DeckUsesSecondMain, USER unsettled), so it waits a
  turn. Covered by the per-turn second-main gate planned for the provider batch (J).
* **Batch C integrated:** `2f1375982` (Elder Gargaroth; 3843/3843 unit tests; smoke for C ran with D below: 235/235 byte-identical).
* **Batch D integrated (Garruk's Harbinger, integrator D2 2026-11-21): `afa007b00`** (replaces the WIP commit
  7227280c1, which a 9p mount loss cut off). NEW `combat_damage_dig_per_damage` / `combat_damage_dig_types`; shared
  `FireCombatDamageDig` in BOTH damage-step shapes of the combat core (count = damage actually dealt, Creature/Garruk
  filter, rest to the bottom via `BottomInRandomOrder`, no dig once the opponent is dead). Searched RANK pin
  `Plan::combat_dig_choice` -> `GameState::scripted_combat_dig` (4 bits/ordinal, 3 ordinals, cap 27; a rank, not a
  card number -- DESIGN DEPARTURE from the draft, because the window is the top N after this turn's draws). Axis
  `harbingeraxis` width = min(15, matching library cards, power + 2); no decline variant (dominated; the viewer offers
  it). NOT in DeckUsesSecondMain. Sizes unchanged (1720 / 792, padding). Viewer: `dig` reuse, DECISIONS.md row,
  audit mapping. Keyword divergence (Scryfall Hexproof / 'Hexproof from') allowlisted. 7 new unit tests; full unit
  suite 3850/3850; audit_card_costs rc 0, audit_card_fields offline rc 0 (`--update` hit 5 HTTP 429s, old snapshot
  entries kept), audit_viewer_decisions `--no-sweep` clean.
* **Smoke byte-identity (batches C + D, at afa007b00 + ledger, no accept):** 235/235 PASS, per-game audit 0 configs
  changed, play-changed 0 (searched and d0). Log `build/scratch/beasts_smokeD.log` (build volume).
* **Owed to batch J (BeastsProvider), from D:** (1) the dig does NOT record into `combat_hand`; the per-turn
  second-main gate must add that record and its state fold together; (2) the root `EtbDigCandidates` does not merge
  same-name copies, so duplicate names give wasted (identical-state) variants -- add a same-name fold in the provider;
  (3) Adaptive Omnitool's combat dig can now reuse this pin (`docs/design/omnitool-combat-dig-greedy.md`).
* **Batch E integrated (Migloz, Maze Crusher; integrators E + E2 2026-11-21/22): `7f1a76269`** (replaces the WIP
  commit 95bb47e3b). NEW `Permanent::oil_counters` (sizeof(Permanent) 328 -> 336, no padding was left), folded
  nonzero-gated into every sim / board / dominance / legend key, `DomAxis::OilCounters` (MoreDominates), and listed in
  `ForEachDedicatedCounterField`. DESIGN DEPARTURES from the draft: (1) the pump is ActivatePump **mode 7** (the draft's
  mode 4 is taken; modes 1-6 are used) and the vigilance grant is **mode 8**; (2) the grant is pruned through a NEW
  `UnprunedGate::OilGrant` (`MTG_UNPRUNE=oilgrant`, root `OffersOilVigilanceMenace` = no), while the destroy's
  own-target prune REUSES Steelbane's existing `counterdestroy` gate (same route, same "own targets" restore) instead
  of a new `oildestroy` gate; (3) UnprunedGate was full at 64, so the gate masks widened to a **128-bit**
  `std::bitset` (OilGrant = gate 65); (4) **menace is not modelled** (the grant gives temp_vigilance only; inert vs
  the goldfish, USER-approved 2026-11-22). The pump is SEARCHED (one variant per K <= min(3, oil/2, mana/2), legal while
  sick, main 1 and any main 2, site-9 key `kActOilPump`, both pay sites oil-capped). NOT in DeckUsesSecondMain.
  Tests: 8 new Migloz cases (one WIP assertion was wrong -- it expected a pass plan inside EnumerateMainPlans, which
  offers none once the land drop is spent; the host appends the pass -- fixed in the test); full unit suite
  3858/3858. audit_card_costs rc 0; audit_card_fields `--update` (232 HTTP 429s, all re-fetched on a per-card retry
  at 1 s throttle; only Migloz enters the snapshot) + offline rc 0; audit_viewer_decisions `--no-sweep` clean
  (DECISIONS.md main_phase row covers all three abilities).
* **Smoke byte-identity (batch E, at 7f1a76269, no accept):** 235/235 PASS, per-game audit 0 configs changed,
  play-changed 0 (searched and d0); scenarios 596/596. Log `build/scratch/beasts_smokeE.log`. Units are identical to
  batch D (190,918,240), but CPU is up: smoke makespan 1596 s vs D's 1482 s (+7.7%), summed job CPU +6.6%, on a box
  with an outside load (load avg ~25 before start, ~40 during). An interleaved D/E A/B (D E D E, 40 mid-size smoke
  jobs, Release on both arms, same box, digests identical on every arm) measured **CPU +3.9%** (D 2.140M / 2.191M ms,
  E 2.263M / 2.234M ms) and **wall +3.1%** (183.2 s vs 188.9 s) at identical units; the same-arm spread was 1.3-2.3%.
  So batch E probably costs ~3-4% per unit of search on EVERY deck. The likely causes are the Permanent growth (every
  GameState copy) and the 128-bit gate mask. NOT root-caused; owed as a perf follow-up (a Profile-build perf diff).
  Log `build/scratch/beastsE/ab_summary.txt`.
* **Owed to batch G (proliferate):** `oil_counters` must be a proliferate kind (it is already in
  ForEachDedicatedCounterField). **Owed to batch J (BeastsProvider):** the two Migloz prunes live in the ROOT
  (`OffersOilVigilanceMenace`, the oil branch of `RestrictCounterDestroyTargetsFor`); move them if wanted, and consider
  a provider prune of the pump when Migloz cannot attack and nothing reads its power (no Garruk -3 live) -- today every
  K is searched each main.
* **Batch E perf (agent E-perf 2026-11-22): `8907da8a9`.** PROFILE FINDING: batch E does almost no extra WORK.
  Callgrind exact Ir on a 7-job single-thread subset (Profile builds, digests identical) was D 88.228G vs E 88.309G
  (**+0.09%**). The grown functions are DecisionUnpruned (+34M Ir, the bitset test and the static guard) and the
  per-permanent oil param tests in CollectActions (~45M). perf `cpu-clock` sampling of the 40-job manifest (no HW PMU
  under WSL) put E at +3.4% CPU over D. The growth is spread over hundreds of symbols (CollectActions,
  EnumeratePlansCore, AppendSubdecisionAxes, GameState copy, memmove), with no new hot spot, which points to
  memory/layout rather than code. Cachegrind with simulated caches, same subset: I1 misses **+1.54%**, D1 misses
  **+0.35%**, LL negligible. FIX (layout only, no field added or removed, no key changed):
  `Permanent::oil_counters` is now an `int16_t` in the 2-byte pad before `paired_with`.
  `loyalty_activated_this_turn` and `colored_cast_lifegain_used_this_turn` became 1-bit fields in the
  `storied_draw_used_this_turn` run (2 bits were free), which opens that pad. **sizeof(Permanent) 336 -> 328** (back
  to D). `PutOilCounters` clamps to INT16_MAX, and the ForEachDedicatedCounterField writers take `auto&`. The
  unprune mask test became plain uint64 words (shift + AND). AFTER, cachegrind on Release binaries, same subset,
  vs D: **Ir E +0.07% / F -0.04%; D1 misses E +0.35% / F -0.01%; I1 misses E +1.54% / F +0.81%**; LL E +0.4% /
  F -1.2% (about 0.75M misses, noise-level). So the fix restores D's data-side behaviour exactly.
  TIMING (the box carried a heavy OUTSIDE load the whole time, load avg 30-75 on 24 cpus, from another container):
  (1) sequential interleaved 3-arm A/B, 40 smoke jobs, 24 threads, 4 rotated rounds (DEF FDE EFD DEF), Release,
  MTG_PGO=0. CPU vs D: E +1.46%, F +2.57%. Wall vs D: E +3.99%, F +0.95%. Same-arm spread **17% CPU, 25-48% wall**,
  per-round E-vs-D CPU from -1.2% to +6.0%, so this measures nothing at the 1-4% level. (2) CONCURRENT Latin
  square: D, E and F run at the same time, each pinned to its own 8-cpu set with --threads 8, the sets rotated over 3
  rounds. Arm sums vs D: CPU E +1.26%, F +0.29%. Wall E +2.0%, F -0.8%. The cpu-set effect alone was 25-35% (0-7
  fastest), so the band is about +/-2%. Units 28,615,548 and digests identical on every arm of every run. The batch
  E "+3.9% CPU / +3.1% wall" (2 reps, load ~40) is NOT reproducible above this box's noise. By the deterministic
  counters, E's real extra cost was well under 1%, and F is within noise of D. SMOKE at `8907da8a9`'s code (no
  accept): **235/235 PASS, per-game audit 0 configs changed, play-changed 0** (searched and d0), scenarios 596/596,
  makespan 1305 s. Log `build/scratch/beastsEperf/smoke.log`. Units 3858/3858. **OWED (unattributed residual):**
  F's I1 misses are still +0.81% over D (about 22M on 2.76G). This is the code footprint the new Migloz branches add
  to CollectActions / ActivatePump / the pay sites, at an estimated ~0.2% of cycles. It is not attributable further
  without a HW PMU. A clean wall A/B also needs a quiet box (the outside load made both designs above +/-2% at
  best). Scratch, all on the build volume: `build/scratch/beastsEperf/` (`ab3_*`, `abc_*`, `cg_*`, `cache_*`,
  `rc_*`, `perf_*.data`).
* **Batch F integrated (Nykthos, Shrine to Nyx; integrators F, F2, F3 2026-11-22): `63aa2fe6b`** (squash of WIP
  5ac71f59f + a272ebc63 + F3; the two orchestrator ledger commits replayed on top, tree diff vs the pre-squash tip
  empty). Units **3870/3870** (16 Nykthos cases). `audit_card_costs.py` 696/696 match; `audit_card_fields.py` offline
  rc 0; `audit_viewer_decisions.py --no-sweep` clean. **SMOKE (no accept) 235/235 PASS, per-game audit 0 configs
  changed, play-changed 0 (searched and d0), scenarios 596/596**, makespan 1270 s; log `build/scratch/beastsF/smoke.log`.
  GATE VERDICTS: **(a) colour gate UNSOUND, FIXED** (ComputeAvailableColors credited only board devotion; it now also
  credits a hand permanent's colours; residual: a permanent put from the LIBRARY inside the line, e.g. Bellower's put,
  is not credited -- moot for this list). **(b) payability UNSOUND, FIXED** (four refusals: the enumerator odometer
  bound and SpwfProvablySupplyShort read Nykthos at board devotion -> DevotionGrowthBound addend; a single walk order
  -> DevotionOrderDomain joins the payable-order search; and THE PAYER, below). **(c) kicked Cragplate devotion: erred
  high (sound), FIXED anyway** (DevotionCardOf pushes the printed card). PAYER OUTCOME: FIXED, not deferred. The
  previous hold sat behind MTG_SCALED_LAND_RANK (default OFF), so it never ran; it is now its own
  MTG_DEVOTION_LAND_HOLD (default ON, devotion land only): Nykthos ranks 61 while board devotion + the line's owed
  pips beats {feeder} AND (inside a payment) a later cast exists AND the other sources left after this bill can still
  feed it (otherwise its {C} pays, e.g. three Forests, a {1}{G} then a {G}{G}). Wild Growth failure was a SEPARATE
  cause: SourceColorCapLive's scaled-land per-colour cap was N without the land Aura's units, so the colour gate
  refused {G}{G}{G}{G} off Nykthos (devotion 3) + Wild Growth; it now adds LandAuraBonus (0 without a land Aura).
  **Three Tree City has the same two holes** and they are NOT changed here (would move Goblins / Maze's End / Merfolk
  Turns play): its rank hold is MTG_SCALED_LAND_RANK, default OFF pending its adoption A/B, and even ON it reads the
  CURRENT net yield, not the creatures the line adds; the Aura-cap fix applies to it too but no suite deck enchants
  one. Gaps for batch J: library-put routes do not credit devotion colours; W-dominated-by-G colour prune not done
  (Kaheera gives W devotion); the hold is a payer PREFERENCE (standing mana-payment exemption), so a line needing the
  opposite choice at the first cast is not realised by the walk.
* **Batch G integrated (Bloated Contaminator, integrator G 2026-11-22): `e2838e47a`.** NEW CardParams `toxic` (int) and
  `combat_damage_self_proliferate` (bool). Rules verified against the CR text (2026-06-19 edition; NOTE proliferate is
  **CR 701.34**, not 701.27): toxic 702.164c / 120.3g (poison is a RESULT of the combat damage, so it is on before any
  trigger resolves); proliferate 701.34a/b (2HG: only one poison counter per team); loss 704.5c (10) and, in 2HG,
  704.6b / 810.8d / 810.10 (the TEAM loses at 15, poison shared).
  - **Toxic:** shared `ApplyToxicPoison` (SpellEffects.h) at the `power > 0` connector point of BOTH damage-step shapes
    of Combat.cpp (executor + rollout lockstep). Not scaled by OpponentHeads (team poison).
  - **Proliferate:** `FireCombatDamageProliferate` (SpellEffects.cpp), one trigger per connecting damage event, LAST of
    each step's combat-damage triggers (after Drana / Lackey / Shroofus / the Harbinger dig; the controller orders
    simultaneous triggers and proliferating last weakly dominates), skipped once the opponent has lost (a lethal
    toxic). Kinds: +1/+1 (doubler-aware), loyalty (p.loyalty + Counter mirror in step), oil (doubler-aware), every other
    dedicated kind and CounterList kind, player poison / energy / rad. First hit = toxic 1 then proliferate 2; every
    later hit +2.
  - **The choice is SEARCHED:** the PROLIFERATE HALF of the combat-time trigger pin -- bits 16-30 of
    `Plan::combat_dig_choice` / `GameState::scripted_combat_dig` (widened int16 -> int32 and MOVED into the hole after
    `scripted_athreos_coin`, so sizeof(GameState) stays 1720; Plan 792 and Permanent 328 unchanged). DESIGN DEPARTURE
    from the draft: one pin for both combat-time triggers instead of a new field, because every one of the ~55
    fold / key / compare sites of the dig pin then covers the proliferate ranks by construction (a field of 0 resolves
    exactly like -1; each half is read and shifted only by its own trigger; `CombatPinMerge` keeps an inherited pin's
    other half on both axes). 5 bits per ordinal, 3 ordinals, ranks 0..30 into `ProliferateMenu` at resolution.
  - **Menu + prune:** `ProliferateMenu` = every subset (identical permanents folded k-of-m by controller +
    RemovalSelfTargetFoldKey; default first, then by counters added; generation capped at 256 entries), or the one
    dominant subset when the ROOT `DecisionProvider::ProliferateCollapse` says so: collapse iff EVERY candidate is
    `ProliferateCandBeneficial` (own permanent with only +1/+1 / loyalty / oil; our energy; the opponent's poison). Any
    other kind (-1/-1, charge, lore, age, depletion, coin, rad, our poison, any counter on an opponent's permanent)
    REFUSES the collapse and the subsets are searched (never auto-added). NEW gate `UnprunedGate::Proliferate`
    (`MTG_UNPRUNE=proliferate`). Axis `prolifaxis` in AppendSubdecisionAxes after Harbinger's: fans only when the
    collapse refuses on the board now; width min(31, 2^(candidates now + plan casts + 1)); product cap 27; skipped in
    human play and on a dead post-combat list. DISCLOSED: an unproven kind that APPEARS inside the plan (none can in
    Beasts) resolves at the default rank; ranks past 30 are not branched.
  - **2HG:** `Player::HasLost` now `life <= 0 || PoisonLost()`, PoisonLost = `>= 10 && (>= 15 || heads < 2)` (the heads
    read only at >= 10 poison). Toxic and proliferate each add ONE team counter (players[1] is the team).
  - **Deck stamps** (StampDeckTraits, mainboard): `deck_has_combat_proliferate` (gates the fire walk + axis);
    `deck_poison_hit_bound` (uint8: sum of toxic + proliferate per copy; 0 for every other deck; 0xFF unstamped = count
    live). Both fit the bytes the old int16 pin left.
  - **Viewer:** main.cpp emits `poison_counters` (+ `poison_loss_at` for the opponent) only when nonzero; the lifebar
    shows "poison N/10"; the proliferate is the `dragon` multi-pick (source "<name> (proliferate) -- choose any number
    of (k: name: counters; ...)"), every candidate incl. PLAYER tiles (a Card named Opponent / You, number < 0), the
    pinned / default subset preselected, an empty reply stands; dragonPanelHtml proliferate branch (counter label per
    tile, player tile); WriteDragonDecisionJson note; DECISIONS.md row; audit_viewer_decisions.py manifest
    (`combat_damage_self_proliferate` -> dragon, `toxic` inert, sweep-unreached pin to the human-chooser unit test).
  - **POISON SOUNDNESS AUDIT (every life-only win / no-win site; a read-only Opus audit of TurnSolver, DecisionProviders,
    AIEngine, GameEngine, DamageEvents, DamageReflection, Dominance, ManaPayment, Combat, runner):**
    - CHANGED (1) `BuildSimKeyBase` (TurnSolver.cpp): folds `poison_counters` per player, nonzero-gated. THE live hole:
      every search memo keys on it (FSLineWin nowin bound + win lines, gamelc / inwin, the TT NoWin entries,
      BuildDedupKey, SeenKey, BuildBreakpointKey, the winless-develop collapse, twin reuse, the solve memo), and the
      header comment said poison was deliberately omitted. A refutation at one poison count could be served to another
      (e.g. Contaminator T2 + 3-drop T3 vs the reverse: same board from T4, different poison).
    - CHANGED (2) `EtChainStateKey`: folds opponent poison when nonzero (Extra Turns' chain replay guard).
    - CHANGED (3) EnumeratePlansCore's four lethal-exempt drops (Irencrag / X-waste, sac-land burn hold,
      HoldCastUnlessLethal, SacForManaOnlyIfNeeded) now exempt `wins || PoisonWinReachableThisTurn` (an O(1)
      over-approximation: opponent poison + 2 x deck_poison_hit_bound (x4 more with an extra combat) >= threshold),
      hoisted once per call. `plan.wins_this_turn` stays the LIFE projection (a positive claim feeding sort / move tier;
      a poison guess must not claim a win).
    - CHANGED (4) the X-waste early proof `xw_nowin` (EnumeratePlansCore): `&& !poison_reach`.
    - CHANGED (5) Solve's greedy twin: the same three mirrored drops (Irencrag, sac-burn, held-cast) exempt poison
      reach (playout quality; kept in step with the enumerator).
    - SAFE (a) the five provider no-win certificates: Snow / Eldrazi Flicker / Fungus decline on `opp.poison_counters >
      0` and on any card outside their pool; Maze's End is name-gated; Gods' has no poison check but its pool has no
      poison source. Beasts routes to BoarsProvider (Certificate NotAssessed) -> no certificate runs. Batch J's
      BeastsProvider must decline on poison if it adds one.
    - SAFE (b) `proven_no_win` (AIEngine/GameEngine): reads no life itself; trusts search completeness, which (1)
      restores.
    - SAFE (c) every board-lethal / pump / loop / equip-route short-circuit (positive detection only, each verified by
      LethalCutKillIsReal -> OpponentHasLost before it fires); FSLineWin / FSLineTail decide wins by applying and
      checking OpponentHasLost. CollectActions damage-sized emission (sac demand, Slimefoot, Hinata) cannot deliver
      poison (toxic is combat only).
    - SAFE-LATENT (d) the main-2 stranded-kill scan (ApplySecondMainInSearch, FSLineTail fixpoint) filters on the life
      projection: a main-2 poison kill needs a NONCOMBAT poison source or an extra combat after main 2 -- none in the
      pool. Re-check if one is added.
    - SAFE-LATENT (e) deck-specific narrowings (Anti-Lifegain SubsetHasUnbackedAltPayload, Fungus FadeKLandmarks,
      Vampires PostCombatCannotKillOptIn, Gods ProvenWinlessThisTurn): deck-gated, no poison source in those lists.
    - SAFE (f) open-coded life-only game-over guards (Prophets gate, incinerator, SpendSurplusOnDrain,
      ComboFinishFromHand, ping loops, a few provider early-outs): cost only wasted work after a poison win.
      DamageReflection's no-progress rule counts life + deaths only (a poison-only trigger loop: latent, none exists).
    - Dominance.h (exact fold) and RouteSig already folded poison.
  - **Gaps for batch J (BeastsProvider):** (1) proliferating Garruk's loyalty (3->4 opens -3 a turn earlier; 5->6
    the -6) or Herald's counters is a MAIN-2 reason -- feed the per-turn second-main gate (NOT added to
    DeckUsesSecondMain, USER unsettled); (2) the no-win leaf grade (`leafeval::Quantity`) and the value leaf price
    opponent life only, so poison progress is invisible at the horizon (quality; hook `NoWinLeafProgress` exists);
    (3) a provider certificate, if added, must decline on poison > 0 / a poison source; (4) the scenario loader cannot
    set poison (tests set the state directly).
  - Tests: 8 new Contaminator cases in test_beasts_cards.cpp. Units 3878/3878 (8 new). Audits: audit_card_costs 697/697 match; audit_card_fields --update at 1 s throttle (0 HTTP 429; the card enters scryfall_reference.json; the Toxic / Proliferate keyword divergence allowlisted in scryfall_divergences.json) + offline rc 0; audit_viewer_decisions --no-sweep clean (`dragon` now expected).
  - **SMOKE (no accept):** **235/235 PASS, per-game audit 0 configs changed, play-changed 0 (searched and d0), scenarios 596/596**, makespan 702 s; log `build/scratch/beastsG/smoke.log`. Units 190,918,240 = batch F exactly; summed job CPU 15.31M ms vs F 16.83M ms (box less loaded; not a controlled A/B). The new hot-path terms for every other deck are one-byte / one-compare tests (deck_poison_hit_bound == 0, poison_counters > 0, the heads read only at >= 10 poison), so no per-deck cost was expected and none showed.
* **Batch H integrated (Woodland Bellower, Codex 2026-11-22): `c279a5ff3`.** Creature ETB uses the shared library-put route with `tutor_nonlegendary`, optional decline (no shuffle), and human `main_phase` named `tutor_target` choices. Soundness: put targets are credited in `DevotionGrowthBound`, `CountSameTurnReducers`, `ManaPruneBound`, `ManaGateWouldHelp` / `BuildManaGateIndex`, both payment walks, and the sequential order domain for a put Warchief before a later Beast cast. The colour gate needs no new credit for the mono-green Bellower targets; poison and lethal projections remain safe for this list. Gaps for J: re-ask if Packleader's pre-search draw takes the human's named target; a later Craterhoof pump omits a put body from damage projection (live in Beasts: the list has one Craterhoof); rank Bellower in the Beasts cast order. Bellower tests 10/10; full units 3888/3888; card costs 698/698; offline field and viewer decision audits clean; smoke 235/235, 0 configs changed, 0 play-changed. `build/scratch/beastsH/` holds logs.
* **Batch I integrated (Kaheera companion, Codex 2026-11-22): `cd1ea5116`.** An eligible starting deck selects one sideboard Kaheera; its once-per-game main-phase `{3}` action consumes that copy and moves it to hand. Autonomous search offers hand-only and fee-plus-cast variants, with a post-entry site-9 continuation for casts before and after the fee; rollout and committed executor replay agree. Human play offers the fee action, then an ordinary hand cast at its exact payable cost on the next main decision. Coverage marks the selected sideboard companion reachable; the viewer exposes the fee choice and matching line verb. Four autonomous Kaheera tests (261 assertions) and a human-mode test (60 assertions, including three- and six-Forest cases) pass. Full units 3892/3892; card cost/field and static viewer audits clean; pre-follow-up smoke 235/235, 596/596 scenarios, 0 changed configs, 0 play changes. The human-only menu follow-up passed its targeted test and the full pre-push regression below. Logs: `build/scratch/beastsI-*`.
* **Viewer readiness achieved (Codex 2026-11-22):** H+I landed as `c841cd59c`; the primary checkout pulled it, rebuilt Release, and started the viewer at `http://localhost:8080` with the persistent reference-backup directory. The untracked original deck copy, identical after normalizing line endings, is preserved under `logs/beasts/primary-deck-backup-20261122-1703/`. A baseline profile was generated locally in the primary checkout (standard analyzer seed 221122, optional cost diagnostic skipped); `/api/decks` reports Beasts `hasProfile: true`, `binExists: true`, tier `alpha`, and `/api/step` returned a real seed-1 mulligan decision. The USER was told immediately. Batch J does not block human play; regenerate the baseline after J settles.
* **Pre-push regression (Codex 2026-11-22):** full `bash test/regression.sh` on the viewer-ready H+I branch passed 375/375 configs, 0 changed configs, 0 searched or d0 play changes, and 930 viewer references with 0 play drift or protocol failure (100,707 games, 41m32s batch). This is the local pre-rebase gate; run the queue-head validation after replaying shared changes.
* **Batch J code validated locally (Codex 2026-11-22):** provider routing and poison-safe certificate, per-turn combat-resource second main, same-name dig fold, Migloz-only prunes, kicked Baloth printed MV, Omnitool rank pin, Bellower human re-ask and Craterhoof projection. Kaheera-before-Warchief with Packleader is now the default cast order when both pay; a mana-tight reducer-first rescue stays searchable. 4029/4029 unit cases and 243/243 smoke configurations passed, with zero changed play. Stage 3 coverage reports no missing or partial cards; Kaheera's companion sideboard copy is reachable. Static viewer and offline card-field audits are clean. The cap-27 Gargaroth/dig and Nykthos W-versus-G prunes remain broad pending a sound proof; the draft in `logs/beasts/drafts/beasts-provider.md` records counterexamples. The cast-order draft is for USER review after Stage 4 measurement.
* **Urgent shared CI repair:** H+I Windows CI failed because test variables `far` and `near` collided with Windows macros. Isolated test-only fix `138f60c83` landed on the shared branch after 4022/4022 units, 243/243 smoke configurations, 1409 consistent GT logs and reference safety. Windows rerun `45060515289` passed every build, unit, viewer and reference check.
* **Stage 4 baseline (seed 221122):** `Beasts.profile.json` regenerated and committed as `9ab9ec838`; 1000 card-score games completed. Full diagnostics: `NO_COST_INTERACTIONS`, discard `STATUS_QUO_OK` (the current ranking is at or near the searched optimum), provider policy `PASS`. Full log `build/scratch/beastsJ-analyzer.log`.
* **Second-main measurement (seed base 522211, 200 games, d5/b20/max12):** per-turn and deck-wide arms had identical digests and units in all 20 paired chunks; each averaged win turn 5.060 and 266,534.27 units/game. Per-turn took 20,313.265 wall-ms/game and 20,209.025 cpu-ms/game; deck-wide took 20,443.975 wall-ms/game and 20,332.890 cpu-ms/game. The same-seed no-main-2 arm averaged win turn 5.055 and 218,378.555 units/game, with 11/20 chunks changing digest. It used 22.1% fewer deterministic units than per-turn; separate-run wall times are not a reliable A/B. Per-turn remains provisional pending the pinned-Hinata cost gate. Logs: `build/scratch/beastsJ-m2-paired.log`, `build/scratch/beastsJ-m2-none.log`.
* **Stage 4-bis cost gate (first run):** `suite_gate.py --cost` measured Beasts at 27,281.600 ms/game against same-run Hinata 7,070.063 ms/game: **3.86×, FAIL** against the pinned 3× limit. The worst Beasts shape was `beasts2hg_regression_d5_b20` (25 games); the standard d5/b20 shapes were 22,569.940 and 16,989.020 ms/game. Provisional regression rows remain local only. The first optimization probe reruns the same two-headed seed with no searched second main, because the 200-game standard comparison found a 22.1% unit reduction there without losing average win turn. A valid admission needs a same-run gate pass and all three tiers with accepted GT; no partial rows will land.
* **Two-headed cost probe:** same 25 seed-2002 games with `MTG_NO_M2_SOLVE=1` averaged win turn 5.200 (same as the gate), 266,746.84 units/game (8.85% fewer than the 292,629.24-unit gate run), with a changed digest. Its 15,889.04 cpu-ms/game versus the separate-run gate's 26,908.04 cpu-ms/game is load-sensitive and cannot itself satisfy the same-run admission rule. The narrower Beasts-only `PostCombatCannotKillOptIn` vouches that the deck has no post-combat action that damages, poisons, mills, or otherwise defeats the opponent, so a cutoff turn that asks only for an immediate kill can skip its second-main solve. A corrected paired 25-game two-headed run measured 5,498,406 units and 462,104 cpu-ms with the cutoff versus 6,927,620 units and 525,867 cpu-ms without (20.6% fewer units, 12.1% less CPU); average win turn was 5.200 in both. Four of five chunk digests matched; the changed chunk retained its 5.400 average turn and needs per-game inspection. The official same-run Hinata gate is running.
* **Stage 4-bis cost gate (cutoff optimization, one 2HG seed):** Beasts fell to 20,401.880 ms/game versus same-run Hinata 6,746.123 ms/game, but remained **3.02×, FAIL**. The worst case was again the 25-game 2HG seed 2002. The ordinary searched cases were below the threshold. A second independent 25-game 2HG seed 3003 is being pooled under the gate's documented shape-and-seed metric to reduce sample-tail sensitivity; this row is provisional pending a fresh same-run verdict.
* **Stage 4-bis cost gate (two 2HG seeds):** the pooled s2002+s3003 result measured Beasts at 25,429.100 ms/game against same-run Hinata 7,592.993 ms/game: **3.35×, FAIL**. The 2HG shape remains the limiter; the second seed was retained because it better samples the tail. The Beasts cutoff proof passed 4029/4029 unit tests and was committed locally as `a341d8036`. A fresh PGO build is in progress to measure the normal long-run engine before further search changes.
* **Stage 4-bis PGO cost gate (two 2HG seeds):** a fresh PGO+LTO build trained on 306 pooled jobs / 8,212 games and including Beasts still measured 33,347.300 ms/game versus same-run Hinata 9,634.030 ms/game: **3.46×, FAIL**. The cost gate needs deterministic work reduction; a faster build alone did not solve it.
* **Bottoming hotspot (PGO 2HG seed 2012, game index 10):** of 73.529 seconds of game CPU, 73.098 seconds were spent in one two-card mulligan-bottom decision. It ran 16 candidate whole-game rollouts (five name-deduped) and used 979,077 search units. A matched-seed pilot is comparing per-deck cheap-first bottom evaluation and selective full-search refinement; any profile change must be screened for per-game play quality before adoption.
* **Bottoming policy screen (PGO 2HG seeds 2002 and 3003, 25 games each):** first-stage depth 1 / 1 ms, refining the two cheapest subsets, reduced CPU to 448,324 ms over 50 games but delayed seed 3003 game 22 from turn four to five. Refining five finalists recovered that win, matched all 50 baseline win turns, and changed four play digests (all in seed 2002). The five-finalist arm used 800,231 ms CPU and 10,167,140 units in those 50 games; the baseline official gate measured about 33,347 ms/game over the same 2HG shape, although the process timing is load-sensitive. Three and four finalists independently failed to recover the turn-four game. The depth-zero/three-finalist pilot delayed a separate game from turn six to seven and is rejected. A further 100 standard d5/b20 seed-2002/3003 games with five finalists matched all 100 baseline win turns and changed one play digest; total candidate CPU was 688,134 ms. The five-finalist setting is now in the Beasts profile pending the same-run cost gate and fresh-seed quality screen.
* **Stage 4-bis cost gate PASS (PGO+LTO, five-finalist bottom policy):** Beasts measured 27,896.420 ms/game in its worst searched two-headed shape against same-run Hinata 10,660.450 ms/game: **2.62×, under the pinned 3× limit**. The full six-job Beasts regression subset used 3,344.7 core-seconds. The gate ran 13 jobs / 2,659 games with Beasts, Hinata and two-headed counterparts; `suite_gate.py --cost decks/Beasts` returned 0. The profile policy still has an independent seed quality check in progress before final adoption.
* **Independent bottom-policy quality check (fresh seed 73003, PGO d5/b20):** 40 standard and 25 two-headed games were paired between the original full bottoming and the adopted depth-one / 1 ms / five-finalist policy. All 65 paired win turns matched; one standard play digest changed and no two-headed digest changed. Candidate CPU was 186,361 ms versus baseline 406,469 ms, a 54.1% reduction in the matched process. With the earlier 150 matched gate-seed games, 215/215 paired win turns match. The policy is adopted in `Beasts.profile.json`; a future exhaustive keep/bottom table remains a separate user-initiated stage.
* **Stage 5b multi-depth sanity (accepted smoke and regression keys):** at matched seed 1001 / 30 comparable games, d0 averaged 5.867 and d3/d5 both 4.900; 21 games improved from d0 to d3, none slowed, and d3/d5 matched every turn. At matched seed 2002 / 50 comparable games, d0 averaged 5.920, d3 5.100, d5 5.080; 35 improved from d0 to d3, one more improved from d3 to d5, none slowed. The deck's turn-four/five searched clock is plausible and there is no adverse depth outlier or budget-starvation signal in these matched sets.
* **Stage 4-bis three-tier admission COMPLETE:** smoke added four Beasts/2HG keys in 65 seconds, regression six keys in 207 seconds with 940 viewer references clean and both suboptimal references beaten, and overnight 16 keys in 17m27s. Each tier had zero failures and zero existing play changes, then was accepted with `--deck=beasts,beasts2hg`. `test/check_gt_logs.py` reports 793 consistent GT logs, zero stale or missing. The overnight entry sample uses d0 4×2000, d3 4×400 at b10, d5 4×300 at b20, plus 2HG 4×50 at b20. One d5 seed-7138 game index 131 spent 785 seconds and ended unwon; seed-7252 index 245 spent 520 seconds and ended unwon. Both are pinned performance outliers for Stage 5 profiling, but the tier remains well inside its eight-hour ceiling.
* **Stage 5 verifier:** the full `verify_deck.py decks/Beasts/Beasts.cod` run passed every blocking check: coverage 26/26, live Scryfall costs, 906 snapshot fields, all three accepted regression tiers, viewer decision sweep and all ten wiring types, both mismatch arms across seeds 7001/8001 ×60 with zero nonconv/fd-diverge, and eight play-protocol games / 192 decisions with integrity and determinism intact. Its Codex-play record initially appeared SKIP because the ledger lacked the checker’s exact `## Codex-play sweep` heading; that heading now records the completed 15-game, zero-unresolved-flags sweep, and the static verifier recheck reports it PASS. `clause_ledger` is a disclosed not-yet-built skip.
* **Stage 5c2 leaf tie-break checks:** the quick pilot (two fresh blocks, 400 paired games) had zero win-turn changes; the requested eight fresh blocks (seeds 1,200,000–1,900,000, 1,600 paired games) also had **zero better and zero worse in each half**. The script correctly reports `NO SIGN AT THIS SAMPLE` because the minimum 20 changed games was not met, and recommends `--blocks 32` before concluding the rule never binds. Keep the default ON provisionally; there is no measured quality harm. The worst expanded seed 1,500,166 remained unwon in both arms but took 1,760 seconds baseline and 989 seconds with the leaf; the run's 1,600 pairs are complete, not censored.
* **Stage 5e unpruned oracle pilot (PGO d5/b20, regression seed 2002):** `MTG_UNPRUNED=1` completed all 50 matched games. The oracle found **zero earlier wins** than the shipped provider and played three games one turn later (indices 33, 34, 43; turn four to five), as a wider search spreads the fixed budget across more branches. It changed 44 play digests. Its 25,446,063 units and 1,892,779 CPU-ms compare with the shipped job's 6,035,008 units and 343,419 CPU-ms in a separate run; the deterministic unit reduction is 76.3%. The slow oracle game index 36 took 507 seconds but still won turn six, matching shipped play.
* **Stage 5e cast-order oracle (PGO d5/b20, regression seed 2002):** `MTG_SEARCH_ORDER=1` completed all 50 matched games, averaged 5.100 versus shipped 5.080, and used 6,791,695 versus 6,035,008 units. It found one earlier win (index 19, turn five to four) and two later wins (indices 33 and 43, turn four to five), with 27 changed digests. Replay of index 19 found an Incubator on turn two, Herald and Migloz on turn three, and lethal on turn four. The shipped search also finds a turn-four Incubator line at budget 100 ms instead of 20 ms, with or without an experimental Incubator-first cast rank. That rank did not recover the win at 20 ms and was removed. The oracle's wider candidate order changes budget allocation and exposes a search-budget tail, not a missing legal cast line. The two later oracle games illustrate its fixed-budget dilution. No cast-order policy was adopted from this pilot.
* **Stage 5h viewer decision audit:** full 40-game live sweep passed, with all ten expected interactive types surfaced; no HARD MISS, UNVERIFIED choice, self-guard failure, driver failure, or unmodeled oracle-text choice phrase. `build/scratch/beastsJ-viewer-full.out`.
* **Running:** all three Beasts suite tiers have accepted GT and the full Stage 5 verifier passed. Finish final reference/GT checks and the queue-head rebase/push. The two overnight slow tails and the one cast-order budget tail remain documented performance outliers. No question is currently waiting on the USER.
* **Stage 4a provider audit:** `scripts/provider_audit.py --check` reports `Beasts -> BeastsProvider`, `cert=Implemented`. Its global exit is 1 because Landfall and Selesnya Lifegain still use engine-default policies for Genesis Wave/Nykthos Paragon; those unrelated pre-existing red flags do not indicate a Beasts misroute. Log: `build/scratch/beastsJ-provider-audit.log`.
* **Stage 5 static preflight:** `verify_deck.py --no-network --no-sweep` passes coverage (26/26), card fields (906 snapshot entries), viewer static surface/wiring (10 decision types) and Beasts provider policy. Its expected failures are regression-tier/suite absence while Stage 4-bis cost is pending; runtime gates were deliberately skipped in this preflight.
* **Orchestrator decisions taken (PROVISIONAL, surfaced to USER):** implement Kaheera companion
  (Tier 3) + make the coverage scan treat a met companion as a reachable sideboard; deathtouch is
  honoured in the shared Cliffs fight helper (not deferred); deck-wide searched second main;
  Gargaroth life mode = provider dominance fold; provisional deferrals listed per card in the
  table below.
* **Next:** (1) save the last two drafts; (2) serial integration — ONE integrator agent at a time
  (cards.json + shared C++ can't take concurrent edits), grouped by subsystem, each building,
  adding unit/scenario tests, running `scripts/audit_card_costs.py` + `audit_card_fields.py
  --update`, committing locally; suggested batches: A) Tier-1/2 data cards (Packleader, Warchief,
  Questing Beast, Thragtusk, Baloth, Kaheera lord+vigilance); B) Contested Cliffs fight (+
  deathtouch) ; C) Elder Gargaroth modal axis + viewer; D) Garruk's Harbinger dig pin; E) Migloz
  oil + pump; F) Nykthos devotion mana; G) Bloated Contaminator toxic/proliferate; H) Woodland
  Bellower; I) Kaheera companion; J) BeastsProvider (route above `boars`, own signature,
  Certificate, dup-legend upside params). Then Stage 3 coverage → Stage 4 profile → 4-bis suite
  rows (3x Hinata gate) → Stage 5 → Stage 6 report.
* Brief every integrator with: the 250K handoff rule, no shared-branch push, no `cd X &&`,
  ONE heavy job at a time, read the draft file + this ledger.

## Deck

60 cards, G/r Beast tribal ramp: 23 lands (incl. 2 Contested Cliffs, Nykthos, 2 Cavern of Souls,
1 Gruul Turf, 7 fetches), 8 land Auras (Utopia Sprawl, Wild Growth), 2 Urza's Incubator, 4 Krosan
Warchief, 4 Herald of Ilharg, payoffs (Elder Gargaroth, Thragtusk, Questing Beast, Craterhoof,
Garruk, Packleader, …). Sideboard: 1 Kaheera (companion residue — see open questions).

**Every creature is a Beast** (Scryfall-verified 2026-11-21; Bloated Contaminator and Migloz are
Phyrexian Beasts, Kaheera a Cat Beast, Herald a Boar Beast). So Cavern of Souls / Urza's Incubator
(chosen type Beast), Krosan Warchief, Kaheera's lord and Contested Cliffs' "target Beast" cover the
whole creature base. (An earlier draft of this ledger wrongly called Contaminator a Rat.)

## Stage 1 — coverage (2026-11-21)

Missing (13): Contested Cliffs, Thragtusk, Krosan Warchief, Elder Gargaroth, Questing Beast,
Woodland Bellower, Cragplate Baloth, Garruk's Packleader, Garruk's Harbinger, Migloz Maze Crusher,
Nykthos Shrine to Nyx, Bloated Contaminator, Kaheera the Orphanguard.

Already implemented (13): Forest, Mountain, Gruul Turf, Stomping Ground, Wooded Foothills,
Windswept Heath, Garruk Primal Hunter, Craterhoof Behemoth, Herald of Ilharg, Urza's Incubator,
Utopia Sprawl, Wild Growth, Cavern of Souls.

Bracket notes to re-check for THIS deck:
* **Cavern of Souls** — "ETB creature-type choice simplified to 'any creature' — exact for a
  tribal list whose creatures share the named type". Exact here (all creatures are Beasts).
* Fetch-hold review (Wooded Foothills, Windswept Heath): hold is modelled; the deck has
  landfall-free fetch use, so hold mostly matters for shuffle/Garruk's Harbinger dig timing. To
  review at Stage 5.

## Stage 2 — cards

| card | tier | status | notes |
|---|---|---|---|
| Thragtusk | 2 | integrated 53e68dcd3 | new ltb_self_creates_tokens on FireLeavesBattlefieldTriggers |
| Contested Cliffs | 3 | integrated (batch B, see Checkpoint) | NEW PermAbilityMode::TapFight (Kessig shape), shared ResolveFight (deathtouch + immediate SBA death pass); both targets searched (own class x opp class); emitted in main 1 and any main 2; NOT added to DeckUsesSecondMain (USER: second main unsettled) |
| Cragplate Baloth | 2 | integrated 53e68dcd3 | new kicked_enters_with_plus_counters (pre-entry kicked branch) |
| Kaheera companion (sideboard) | 3 | integrated cd1ea5116 | companion {3} put-into-hand, once/game, sorcery speed; site-9 searched continuation; human fee then ordinary cast |
| Krosan Warchief | 1 | integrated 375a50399; approved | reduces_spell_subtype Beast; regenerate deferral approved |
| Questing Beast | 1 | integrated 375a50399; approved | deathtouch via fight helper; D2-D4 deferrals approved (no blocks / no prevention / no PWs); dup-legend prune risk |
| Elder Gargaroth | 3 | integrated (batch C, see Checkpoint); approved | NEW attack_choose_one 3-mode trigger, searched per-ordinal axis (Tectonic-style, cap 27), shared executor+rollout helper; life mode = root provider fold on deck_life_inert (USER approved); viewer attack_mode widened to 3; 'or blocks' deferral approved; NOT in DeckUsesSecondMain (combat draw uses the per-turn gate) |
| Garruk's Harbinger | 3 | integrated afa007b00 (batch D, see Checkpoint); approved | NEW combat_damage_dig_per_damage + FireCombatDamageDig (both damage-step shapes, shared executor+rollout; count = damage dealt), shared PerformLookTakeDig (Creature/Garruk, random bottom); searched RANK pin per ordinal (cap 27, width min(15, library matches, power+2)); no decline variant (dominated, viewer offers it); NOT in DeckUsesSecondMain (dug card uses the per-turn gate); hexproof-from-black + PW deferrals approved |
| Kaheera | 2 | lord+vigilance integrated 53e68dcd3; companion integrated cd1ea5116 | lord_effect multi-subtype + grants_vigilance; eligible sideboard companion uses searched {3} hand entry |
| Migloz | 3 | integrated 7f1a76269 (batch E, see Checkpoint) | NEW Permanent::oil_counters (sizeof 328 -> 336), folded into every sim/board/dominance/legend key + DomAxis::OilCounters; pump SEARCHED as ActivatePump mode 7 (one variant per K <= oil/2, mana/2; main 1 + any main 2; site-9 key kActOilPump; both pay sites oil-capped); vigilance grant = mode 8, root-pruned (`MTG_UNPRUNE=oilgrant`, menace unmodelled -- USER approved 2026-11-22); destroy = Steelbane route paid in oil, own targets root-pruned (reused `MTG_UNPRUNE=counterdestroy`); NOT in DeckUsesSecondMain |
| Cavern of Souls (recheck) | - | no change | exact for this list |
| Nykthos | 2 | integrated 63aa2fe6b (batch F, see Checkpoint) | Three Tree City scaled-land shape + NEW mana_per_devotion (per-colour yield, all five devotions in the mana cache key); per-colour fed faces in the viewer; gate soundness (a)/(b)/(c) fixed; payer hold MTG_DEVOTION_LAND_HOLD (default ON, devotion land only) |
| Bloated Contaminator | 2 (+3) | integrated e2838e47a (batch G, see Checkpoint) | NEW toxic + combat_damage_self_proliferate; subset SEARCHED on the proliferate half of the combat-time pin, root dominance collapse (MTG_UNPRUNE=proliferate); poison folded into BuildSimKeyBase, lethal-exempt drops poison-aware; 2HG team loses at 15; viewer dragon multi-pick with player tiles; NOT in DeckUsesSecondMain; no deferrals |
| Woodland Bellower | 2 | integrated c279a5ff3; approved | Natural Order put route wired into creature ETB + NEW tutor_nonlegendary + optional decline (no shuffle); fail-to-find + trigger order approved |
| Garruk's Packleader | 1 | integrated 375a50399; approved | own_creature_enters_draw + creature_enters_min_power 3; "may" always-draw collapse approved |

Drafts: `logs/beasts/drafts/*.md` (gitignored, host drive).

## Viewer classifications (2c-ter)

* Contested Cliffs (batch B): bucket A. ONE unpinned main-phase entry under human play; both fight targets (our Beast, then theirs) are `target` board clicks at activation via `g_play_loyalty_chooser` (Voracious precedent). `audit_viewer_decisions.py`: `tap_fight_cost` -> target / activate, `tap_fight_requires_subtype` inert. Registry: tools/play/DECISIONS.md "Targeted tap fight".

* Elder Gargaroth (batch C): bucket A (reuse, widened). `attack_mode` with THREE options (0 token, 1 life, 2 draw), asked per declared copy at declare-attackers from `ApplyAttackChooseOneTriggers` over the full legal menu (the search's life fold never narrows the human). `audit_viewer_decisions.py`: `attack_choose_one` -> attack_mode, payload params inert. Registry: tools/play/DECISIONS.md attack_mode Gargaroth row.

* Bloated Contaminator (batch G): bucket A (reuse). The proliferate is the generic `dragon` multi-pick, asked from `FireCombatDamageProliferate` (both damage-step shapes, shared executor+rollout) whenever something has a counter after the hit: EVERY candidate permanent (either side) and PLAYER (a candidate card named `Opponent` / `You`, numbered < 0, rendered as a player tile with its counter label), the pinned subset or the dominant default preselected, an empty reply stands; the search's collapse never narrows the human. Toxic is automatic (no decision). The lifebar shows the opponent's poison (`poison_counters` / `poison_loss_at`, emitted only when nonzero). `audit_viewer_decisions.py`: `combat_damage_self_proliferate` -> dragon, `toxic` inert. Registry: tools/play/DECISIONS.md Bloated Contaminator `dragon` row.

* Woodland Bellower (batch H): bucket A. Human `main_phase` cast variants carry named `tutor_target` choices plus `(decline search)`; the shared ETB put route resolves each. `audit_viewer_decisions.py`: `tutor_to_battlefield_single` and `tutor_optional` -> main_phase, `tutor_nonlegendary` inert. Registry: tools/play/DECISIONS.md optional ETB tutor row.

## Verification verdicts (Stage 5)

* **Codex-play (5d):** 15 fresh seeds, `231001`–`231015`, each benchmarked and replayed with the frozen pre-optimization binary and the committed Beasts profile. All 15 completed with **zero verified legality, invariant, or missing-decision flags**. The Codex player matched or trailed the search's win turn; differences came from mulligan and play choices, with no earlier-win search-misplay candidate. The sweep exercised Bellower's put, Harbinger's dig, Gargaroth's three-option attack prompt, Bloated's toxic/proliferate, Migloz's oil/pump, Kaheera and land-entry/color choices. Later profile bottoming changed a few play digests but preserved all 215 paired win turns in its quality screen; the protocol is independently covered by Stage 5 play invariants.

## Codex-play sweep

commit: `9ab9ec838` (source and generated baseline profile frozen for the external play sweep)
seeds: `231001`–`231015`
games: 15
flags: 0 unresolved
Driver: Codex agents, matching the parent session type per AGENTS.md. The per-game findings and exercised choices are summarized in the Stage 5 verdict above.

## USER rulings (2026-11-21, verbatim, on the open questions)

1. Companion: *"Yes, Kaheera is a companion in the deck."* -> implement (batch I).
2. Second main: *"I'm not sure about this for cost reasons at least if we search everything this
   way. This will require some thought."* -> NOT settled. Build the cheap per-turn shape
   (`ProviderSecondMainLive` in BeastsProvider: open a searched main 2 only on a turn where a
   combat-time event put a card in hand -- Harbinger dig, Gargaroth draw mode, a Packleader draw off a
   combat-created token), keep deck-wide reachable via `MTG_FORCE_USES_M2`, and at Stage 4 measure
   none / per-turn / deck-wide (units, ms/game, avg win turn) for the USER. Per-turn is the
   provisional shape in the suite rows.
3. Gargaroth life mode: *"Yes, we don't need this in the search."* -> provider fold approved.
4. Packleader/Bellower order: *"That's fine."* -> disclose as ordering collapse (approved).
5. Merfolk Turns Cavern/Seahunter: *"That should be fixed, but only when there is time."* -> deferred,
   `docs/design/cavern-of-souls-creature-type.md`.
6. Omnitool dig: *"If choosing the top is built into the search this is a notable problem, yes.
   However, if it is just that the provider returns one item it is not an issue."* -> checked: it IS
   the former (EtbDigCandidates returns the legal set unpruned; the combat fire site takes the top,
   no searched pin). Unreachable in shipped play (Omnitool is sideboard on the shipped list, deck
   stamp is mainboard-only). Fix = reuse Harbinger's combat-dig pin (batch D) after it lands; deferred
   doc `docs/design/omnitool-combat-dig-greedy.md`.
7. Deferrals: *"For Migloz we should include the power pump mode for sure. The other two can be
   heuristically pruned."* -> Migloz +2/+2 pump searched; vigilance/menace and destroy pruned in the
   provider (APPROVED). Warchief regenerate, Questing Beast D2-D4, Gargaroth "or blocks", Harbinger
   hexproof-from-black/PW, Packleader "may": at this 2026-11-21 checkpoint awaited an explicit word; resolved in the 2026-11-22 ruling below. Bellower: asked what the
   issue is -- answered (it is the fail-to-find-while-legal-cards-remain option only).
8. Cast order: *"Okay, I'll try to look over what you draft tomorrow."*

### USER rulings (2026-11-22, verbatim)

On the seven deferral questions (Warchief regenerate; Questing Beast's three clauses; Gargaroth
"or blocks"; Harbinger hexproof-from-black + planeswalker damage; Packleader "may" -> always draw;
Bellower deliberate fail-to-find while a match remains; Migloz menace unmodelled, vigilance only):
*"All questions 1-7 sound good to me."* -> all seven APPROVED; drop PROVISIONAL from their notes.

On the ordinal-product cap (Gargaroth attack-mode axis and Harbinger dig axis, cap 27; past it the host
falls back to a default for the extra ordinals -- a possible completeness/greedy defect): *"We should
probably prune those cases with a heuristic."* -> batch J: the BeastsProvider restricts the per-ordinal
options (same-name dig fold, heuristic top-k per ordinal, Gargaroth mode order) so the admitted product
fits the cap, and the host then branches over everything admitted; no host-side default for an
over-cap ordinal. Audit every other cap-27 axis the Beasts cards reach for the same shape.

Follow-up on HOW to prune: *"And certainly one of the ways to prune is looking for leathal. For example
with Gargaroth it is obvious we should drop the beast if it will speed up lethal. Otherwise we can at
least consider the draw."* -> BeastsProvider Gargaroth rule: if making the 3/3 Beast token brings
lethal sooner (the token cannot attack this turn, so compare the earliest lethal turn with vs without
it), that ordinal is pruned to token only; otherwise the draw stays in the menu (token + draw searched,
life already folded). Lethal-awareness is the first pruning key for the other over-cap axes too
(e.g. Harbinger: keep the dig ranks that enable an earlier lethal). Interpretation surfaced to the
USER 2026-11-22; check the draft provider against it.

Refinement (USER 2026-11-22): *"I guess there are likely also cases where it makes sense to consider the
draw only. If we can see the beast won't end the game faster we might be able to, but generally this is
diffcult to fully predict, so the typical default can be search both."* -> Gargaroth per-ordinal rule:
token ONLY when the token provably brings lethal sooner; draw ONLY when we can see the token cannot make
the game end faster (a sound, conservative test -- e.g. lethal is already on board next turn without it,
or the game cannot last until the token can attack); otherwise (the typical case) search BOTH.

## Decisions and follow-ups (none awaiting a USER reply)

1. **Kaheera companion (sideboard).** Implemented in batch I: the once-per-game {3} hand move is
   searched and available in the viewer. Stage 3 confirms the sideboard copy is reachable.

## Provider routing (Stage 4a, found in Stage 2)

Batch J routes the deck to **BeastsProvider** above BoarsProvider. Its signature ORs Beasts-only
params from multiple cards, so removing Herald or a single signature card retains the route;
`test_pirates_provider.cpp` and Beasts unit tests pin this result.

2. **Second main (USER-owned main-1-vs-main-2).** Contested Cliffs (fight after attacking),
   Elder Gargaroth (draw mode / token -> Packleader draw) and Garruk's Harbinger (combat-damage
   dig) all create combat-time resources, so the deck needs a searched post-combat main.
   The adopted batch-J shape is a per-turn gate after an actual combat-time hand arrival
   or a proliferated Garruk/Herald resource. Deck-wide main 2 remains available through
   `MTG_FORCE_USES_M2`; none/per-turn/deck-wide cost and outcome measurement is in the Checkpoint above. The pinned-Hinata admission gate passed at 2.62×.
3. **Elder Gargaroth's life mode** is folded away in the provider as a sound dominance (nothing
   reads our life; max self-damage 13 < 20). The viewer still offers all three modes.

4. **Packleader + Bellower trigger order (CR 603.3b).** When Bellower enters, Packleader's draw
   and Bellower's search trigger together; the engine always resolves the draw first (pre-shuffle
   top card). Default: disclose as an ordering collapse; the alternative is a searched order axis.
5. **Other deck (Merfolk Turns):** Cavern of Souls' "any creature" simplification lets its
   coloured mana pay 4 Seahunter (Human Mercenary, not Merfolk). Fixing it moves Merfolk Turns GT.
6. **Adaptive Omnitool** (another deck) appears to take its combat dig as the provider's top
   pick with no searched pin — a possible in-window greedy (found while drafting Harbinger).

## Approved deferrals

* Migloz {1}/-1 oil vigilance+menace: pruned (USER 2026-11-21).
* Migloz {3}/-3 oil destroy artifact/enchantment: provider-pruned (USER 2026-11-21).
* Elder Gargaroth gain-3 mode: provider fold (USER 2026-11-21).
* Packleader/Bellower trigger order collapse (USER 2026-11-21).
* Krosan Warchief regenerate: unmodelled (USER 2026-11-22).
* Questing Beast: no-blocks-by-power<=2, no damage prevention, planeswalker redirect unmodelled (USER 2026-11-22).
* Elder Gargaroth "or blocks": attack trigger only (USER 2026-11-22).
* Garruk's Harbinger hexproof from black + planeswalker-damage dig: unmodelled (USER 2026-11-22).
* Garruk's Packleader "may": always draws (USER 2026-11-22).
* Woodland Bellower: deliberate fail-to-find while a match remains not offered; decline still offered (USER 2026-11-22).
* Migloz menace: unmodelled, oil grant gives vigilance only (USER 2026-11-22).
