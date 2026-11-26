# Elves Stage 2 integrator -- HANDOFF (context limit reached, ~250K)

Worktree: /workspaces/MagicDeckTester/.claude/worktrees/elves, branch wip/elves-analysis (base a825e81f4).
Decklist decks/Elves/Elves.cod. Ledger docs/design/analysis-Elves.md (Stage 2 / Viewer sections NOT yet filled).
State at handoff: all edits below are in the working tree; see `git log` on wip/elves-analysis for the WIP commit
(if the build was green it was committed; if not, the tree is uncommitted -- run `git status`).

## Done (in the tree)
cards.json: 8 new entries inserted as TEXT before "Arbor Elf" (Magistrate, Warcaller, Shepherd, Taunting Elf,
Clancaller, Brigade, Visionary, Marwyn). NOTE Marwyn's real oracle (Scryfall): the counter goes on MARWYN, not on
the entering Elf (the brief was wrong).
CardParams (CardDatabase.h, near kicker_cost) + loader (CardDatabase.cpp after kicker_cost): multikicker_cost,
squad_cost, multikick_enter_counters, kick_times (face-only), lord_per_own_plus_counter,
tap_target_counters_per_subtype, team_base_pt_{cost,power,tough,subtype}, cast_subtype_pay_draw_cost,
mana_per_own_power. Helpers kMaxMultikick=16, MultikickFaceName, HasRepeatKicker, RepeatKickerCost (CardDatabase.h
above struct CardDefinition).
1. MULTIKICKER/SQUAD faces: CardDatabase.cpp synthesizes "<name> (Kicked xK)" K=1..16 (cost printed+K*rep,
   kick_times=K, kicked_base_name set). Action::kick_count (int8, TurnSolver.h) + CastRiders::kick_count (+Of).
   KickedFaceFor(def,kicked,kick_count) in SpellEffects.h; used by apply_one swap, AIEngine CastSpellFromHand swap,
   KickedFaceOr (now 3-arg; callers pass kick_count). Enumeration block in CollectActions just before "Awaken is an
   alternative cost" (kmax from turn pool + floating + DorkGrowthXPotential + pending land drop, clamp 16;
   provider hook DecisionProvider::MultikickCounts, reopened by UnprunedGate::Replicate -- the 64-gate mask is FULL). kick_count threaded into:
   action hash MixA (<<8), FoldMix, diff/equality "kick_count", Chandra serialization, labels, dedup key "|kN",
   plan signatures "#MK<k>" (both sites), copy-twin probe, HumanStepToken "+kickedx<K>" parse+match, LineSpec
   kicked= names "<name> xK". main.cpp plan JSON emits kick_count + kicked_cost_mv (K*rep). Viewer: linebuild.js
   toggleKicker(plan,name,kind,kmax) cycles K; index.html kickerMaxK/kickedKFor + badge "[k/kmax kicks]".
   Payloads: Warcaller K counters in pre-push window of EffectHandler::EnterBattlefield and apply_one (after the
   Ballista X-counter line); Squad: ResolveSquadCopies (SpellEffects.h, before KickedFaceFor) called after
   FireDeferredDevourDeaths in both worlds.
2. Warcaller lord: ComputeLordBonus branch lord_per_own_plus_counter (PlusCountersOn(lord) multiplier).
3. Magistrate: TapTargetPump resolution branch tap_target_counters_per_subtype (PutPlusCounters N, N =
   CountOtherControlledWithSubtype(...,INT_MIN)) + label. Emission reuses TapTargetPump (targets = OWN creatures
   autonomously, identity-folded; human gets board click).
4. Marwyn: IsScaledManaDork + ScaledDorkCount (CombatPowerOf of the controlled Marwyn) ; ScanDorkGrowthGroups
   groups it under enters_watch_subtypes[0] (growth +1/feeder).
5. Visionary: FireOnCastTriggers block (after Mihail's token block) -- shared pin scripted_mihail_pay_choice,
   unpinned = decline; human via g_play_dig_chooser. Fan block in the plan post-pass right after Mihail's fan
   (before "Militia's Pride"): prefix-fold (1 + sum_j s_j variants) when BpPutInHandEnabled, full 2^bound else; runs
   in continuations too; skipped when a Mihail-type source exists. Site-10 watched_cast_subtypes absorbs it.
6. Clancaller: wish_requires_name name filter added to TapTutorPutLibraryCandidate and PerformTutorToBattlefield.
7. World rule: EnforceWorldRule (SpellEffects.h, before EnforceLegendRule, called at its top); enter-site gates
   take Supertype::World (EffectHandler + 3 TurnSolver sites).

## REMAINING, in order
A. Fix the build if red (log: /workspaces/MagicDeckTester/build/scratch-elves-build.log). Likely suspects:
   `Player& vp` name clash in FireOnCastTriggers; CombatPowerOf ordering vs ScaledDorkCount; INT_MIN include.
B. Allosaurus Shepherd TeamBasePT (NOT STARTED beyond params): new PermAbilityMode::TeamBasePT appended LAST in
   Permanent.h; add to PermAbilityTaps exclusion list; add Permanent bit `temp_base_set : 1` in the second bit byte
   (next to unearthed) -- reset at every cleanup site that resets temp_haste (GameEngine.cpp ~1596, SpellEffects.h
   ~37509 and ~48390, TurnSolver.cpp ~83627, check Combat.cpp:812) and fold wherever destroy_at_end_if_attacked is
   folded (Dominance.h 888, TurnSolver 701/3730/21088/108195/110552, SpellEffects 10312/11870/...). Resolution:
   for each own Elf creature w/o temp_base_set: temp_power_bonus += P - (m_power + DynamicBasePower), same for T,
   set bit. Mirror FigureTwo at every site: SpellEffects label (~40039), cost-selector chain (~41637), resolution
   switch (~40531); TurnSolver ModeSpec tables (~6918, ~34414, ~50795, single emission -> add to the K-block
   exclusion at ~50947), names list (~141701), cap lists (~34264, ~34822, ~93623 add team_base_pt_cost);
   main.cpp verb; audit_viewer_decisions.py MANIFEST rows (~420, ~1029). VERIFY the activation is applied AFTER
   the plan's casts (check how ApplyPlanDirect orders ActivatePermAbility vs CastFromHand); if not, it must be.
C. Unit tests test/unit/ (copy test_worldfire.cpp shape): executor-vs-rollout agreement for multikick counters,
   squad copies (+ copies are lords), Warcaller per-counter lord, Magistrate counters, Marwyn yield, Visionary
   pay/decline pins, world rule, Clancaller name filter, Shepherd. Register in CMake like siblings.
D. Viewer 2c-ter: Magistrate target = bucket A (TapTargetPump board click); multikick/squad K = bucket A-ish
   plan variants + NEW GUI count cycling (done); Visionary pay = Mihail's dig-chooser yes/no (bucket A reuse);
   Clancaller tutor = TapTutorPut (bucket A); Shepherd = main_phase activation (bucket A). Add DECISIONS.md rows /
   audit_viewer_decisions.py MANIFEST entries for the new params (multikicker_cost, squad_cost,
   tap_target_counters_per_subtype, cast_subtype_pay_draw_cost, team_base_pt_cost).
E. Done-criteria gates: coverage-only missing [] ; audit_card_costs.py rc 0 ; audit_card_fields.py --update then
   clean (commit snapshot) ; audit_viewer_decisions.py --no-sweep ; MTG_PGO=0 bash test/regression.sh --smoke
   (expect play-changed=0; shared-code risk points: ComputeLordBonus branch, KickedFaceOr signature, hash bit
   <<8 (0 for all old actions -> identical), EnforceLegendRule world pass (no-op w/o 2 World perms), Marwyn group
   change in ScanDorkGrowthGroups (gsub==mana_per_creature_subtype for every old card)). Do NOT accept GT.
F. Fill the ledger Stage 2 + Viewer sections; commit on wip/elves-analysis.

## Known gaps / PROVISIONAL decisions to report
* Taunting Elf lure: PROVISIONAL inert (opponent never blocks: PopulateOpponentSpawns comment + no blocker path
  in Combat.cpp -- verified).
* Shepherd: uncounterable x2 inert; Dinosaur type not applied (no reader) -- PROVISIONAL.
* World-rule TIE (simultaneous entry) not modelled; inert here (Genesis Wave put set is a choice).
* Multikick faces capped at 16 kicks (33 mana for Warcaller) -- a model bound, disclose.
* Visionary unpinned = decline (Mihail semantics) => playouts beyond the horizon and the d0 runner never pay:
  undervalues Visionary at leaves -- Stage 5 quality item.
* Magistrate target set = the PRE-PLAN board: a creature cast earlier in the SAME plan cannot be targeted in that
  main (no main 2 by default). Same holds for Kessig/Merfolk tap-target pumps. Completeness gap to raise.
* Visionary + Mihail in one list: fan skipped (shared ordinal stream) -- no list runs both.
* Search-cost watch: multikick fan (up to 16 variants per Warcaller/Brigade copy in hand), Visionary fan
  (1+sum s_j per plan), Magistrate targets (identity-folded own creatures).

## Rules (verbatim from the brief)
- NO GREEDY IN THE SEARCH WINDOW. Multikicker count, squad count, Magistrate's target, and Visionary's pay-or-not are
  searched decisions. The engine may not pick one. A DecisionProvider may only ORDER/RESTRICT, and human play
  (HumanPlayActive() / MTG_UNPRUNED) must be offered EVERY legal option. If you add a restriction it must be a
  provider hook with MTG_UNPRUNED reopening it, disclosed in the ledger.
- "Do nothing more" must stay an option at every search root. Never remove the idle pass.
- New flags go through EnvOn("MTG_X"). Never use a presence-only getenv.
- NEVER rewrite cards.json with json.dumps. Edit it as TEXT, anchored on the card's entry.
- Build ONLY with ./build.sh (Release) inside the worktree. Never raw cmake. Never wrap a command in timeout, never
  pass the Bash timeout parameter. Long builds/tests with run_in_background, wait on PID/marker. export
  MTG_RSS_CAP_GB=20 on every engine run.
- Viewer (2c-ter): classify every interactive choice into bucket A or B. Do NOT start the play server.
- Never touch references/. Do not push phase-1-2-deck-analyzer. Commit locally on wip/elves-analysis (may push that
  wip branch). Commit messages end with: Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
- Never block on a question; pick the recommended option, record PROVISIONAL, keep going.
- Context handoff at ~250K (this file), never exceed 400K.

## Running jobs
- ./build.sh (background) started by the previous agent; log /workspaces/MagicDeckTester/build/scratch-elves-build.log
  ends with "EXIT <rc>".

## NEW USER RULINGS (relayed by the coordinator after the handoff was first written) -- VERBATIM
1. Taunting Elf: the USER APPROVED the deferral. Quote: "Yes, Taunting Elf's ability does nothing in goldfish."
   Write its bracket note as an approved inert deferral (not PROVISIONAL). Do NOT edit the ledger's "Approved
   deferrals" section for this; the coordinator does that. (cards.json Taunting Elf note still says PROVISIONAL ->
   change to "[PARTIAL, USER-APPROVED 2026-11-26: ...]".)
2. Scaling-count branching. USER, verbatim: "There are multiple scaling effects like Genesis Wave's X and Joraga
   Warcaller, but generally you want to pick just one of those and scale it up fully, minus perhaps a few 1-mana
   producing attackers. So there might be a couple different relevant options, (i.e. do I tap this Llanowar Elves to
   scale it further or keep it as an attacker, which can either be searched or heuristically pruned depending on
   the situation) but we shouldn't try every possibility."
   Coordinator's how-to (verbatim): Build this as a PROVIDER restriction in a new deck provider for Elves
   (src/ai/DecisionProviders.cpp; derive it from whatever the deck routes to today, per analyze-deck.md Stage 4a;
   `scripts/provider_audit.py` tells you). It must NOT live in generic enumeration. It covers every scaling sink in
   the list: Genesis Wave X, Joraga Warcaller multikicker count, Galadhrim Brigade squad count, and any other "pay N
   times" sink you find (Ezuri's pump, Clancaller/Shepherd activations if they repeat). Candidate set: one sink per
   main phase is scaled to the max affordable (after the plan's other casts). Add a few variants that hold back
   1-mana dorks as attackers (max-1 .. max-k, where k is the number of untapped 1-mana producers that could actually
   attack this turn: not summoning-sick, or hasted by Concordant Crossroads). Keep k small, and you may prune the
   hold-back variants when attacking cannot matter. Do not enumerate splits of mana across two scaling sinks with
   partial values. A minimal unscaled cast (Warcaller with 0 kicks, Brigade with 0 squad) as part of another sink's
   plan is fine to keep. The search still branches over the candidates the provider returns. The engine never picks
   one. MTG_UNPRUNED must reopen the full range, and human play (HumanPlayActive()) must still be offered EVERY legal
   count. The viewer is never pruned. Record it in the ledger as a USER-directed pruning heuristic (quote the
   ruling), stating what lines it could miss. It still gets the with/without A/B later (Stage 5e/5f); the
   coordinator runs that.
   Implementation hooks already present: DecisionProvider::MultikickCounts(state, def, kmax) (empty = all K) and
   XCandidates for Genesis Wave; MTG_UNPRUNED=replicate reopens the multikick fan (the UnprunedGate mask is full).
   "Max affordable AFTER the plan's other casts" is a per-PLAN property, so a per-action candidate hook alone cannot
   express "one sink per main phase"; a plan-level post-filter in the provider (or ReplicateTopCountOnly-style
   per-plan hook -- see the replicate fan's ReplicateTopCountOnly) is the likely shape.

## Build status at handoff
The rebuild after the gate fix failed only with "Cannot allocate memory" reading SpellEffects.h from 9p (the
known 9p order-4 alloc flake -- retry, do not cap jobs). A retry was started; see the log's last "EXIT" line.

## Status at final stop (second commit)
* Build GREEN (./build.sh Release; the 9p "Cannot allocate memory" flake needed several retries -- retry, never cap).
* Unit tests: 3948/3948 pass (build/Release/mtg-test). Fixes in this commit: Keyword::Multikicker / Keyword::Squad
  inert tags (Card.h + KeywordFromString) -- KeywordFromString THROWS on an unmapped Scryfall keyword; the Pirates
  routing table (test/unit/test_pirates_provider.cpp) now pins {"Elves", "SelesnyaLifegain"} = the CURRENT
  routing. Elves routes to SelesnyaLifegainProvider today -> the USER ruling 2 says derive an ElvesProvider; update
  that table row when it exists.
* Taunting Elf bracket note rewritten as USER-APPROVED (quote included).
* NO NEW UNIT TESTS for the Elves mechanics yet (item C), NO smoke run yet (item E), Shepherd not wired (item B),
  ElvesProvider scaling-sink restriction not started (USER ruling 2).

## SECOND INTEGRATOR -- items B-F DONE (2026-11-26)
* d2cd4c39e: Allosaurus Shepherd PermAbilityMode::TeamBasePT (appended LAST, = 43) + Permanent::temp_base_set bit
  (reset at GameEngine cleanup, TurnSolver SimulateEndAndStartNextTurn, EotWindowEndOfTurnReset, Combat.cpp
  next-turn probe; folded beside `unearthed` everywhere; Dominance boundary refuses it); ResolveTeamBasePT
  (SpellEffects.h, before ApplyPermAbility); all three ModeSpec tables, K-block exclusion, PlanDumpModeName
  (also added the missing "TapPing"), UntapFloatSpendCap / S9SacManaDemand lists, PostEntryActivationPending.
  test/unit/test_elves.cpp (9 cases, executor vs rollout), registered in CMakeLists.txt. Concordant Crossroads'
  stale world-rule bracket rewritten. DECISIONS.md rows + audit_viewer_decisions.py MANIFEST / board-activation /
  INERT_PARAMS rows. Units 3957/3957.
* Gates: coverage missing [] ; audit_card_costs rc 0 (707/707) ; viewer audit rc 0 clean ; smoke 243/243,
  play-changed 0 (searched and d0), 608 scenarios ; field audit offline rc 0 (snapshot gained the 8 Elves cards; --update rc 1 only from 429s on already-snapshotted cards).
* Ledger Stage 2 + Viewer sections filled (docs/design/analysis-Elves.md).
* NOT DONE (by design): the ElvesProvider / scaling-sink restriction (USER ruling 2) -- the next agent's.
