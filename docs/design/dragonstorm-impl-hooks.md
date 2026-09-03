# Dragonstorm — implementation hook map + session resume state

Companion to `analysis-Dragonstorm.md` (the ledger). Captures the research hook-map for the
remaining spell mechanics and the mid-build resume state so a compaction/handoff loses nothing.
**Line numbers drift** (concurrent edits) — anchors below are by FUNCTION/PATTERN, which are stable.

## Session resume state (2026-07-18, updated) — READ FIRST on resume

**(Updated 2026-09-03: this resume state is HISTORICAL.** The 2026-07-18 background gen is long
gone; `MTG_SKIP_GRID` was deleted when card-scores-only became the analyzer's only mode
(8d4bdefb); and the Bucket-B viewer items — target/dragon/firebreathe/storage_hold — have all
since been wired. Do not resume from here.)
- **BUILD COMPLETE: all 16 cards implemented, full coverage (`missing=[]`), byte-identity clean (full smoke
  18 passed / 0 new / EXACT digests).** Branch `phase-1-2-deck-analyzer`. Commits (in order):
  `f50de75` Knights references · `ea2c3cb` 12 cards + inert splice scaffolding · `5529bfb` Desperate Ritual
  splice (search-chosen k, copies-scale-before-floor) · `406966f` Dragonstorm engine (storm counter
  `spells_cast_this_turn` + `PerformTutorToBattlefield` + shuffle; FireEtbWatchers wired for puts) ·
  `5437427` DragonstormProvider (selection + single deterministic put-order; A/B 7.20 vs 7.40 library-order) ·
  `4483f9f` Apex of Power (impulse-exile-7 + cast-from-hand 10-color float, staged-land block `m_impulse_no_land`) ·
  `ccb578e` Storm keyword tag + refreshed Scryfall snapshot (`scryfall_reference.json`) · `dfe3fff` executor
  Medallion cost-reduction lockstep fix.
- **DATA AUDIT GREEN:** `audit_card_fields` ok=True, 0 hard mismatches (Storm keyword tag added, inert like
  Suspend/Splice); oracle-text divergences are advisory (our entries carry bracket impl notes). Snapshot committed.
- **STAGE 5a (correctness) — CONVERGED: nonconv=0, fd-diverge 13→0.** PRIMARY cause FIXED+committed (`dfe3fff`):
  `AIEngine::EffectiveCost` (executor) was MISSING the Ruby Medallion `reduces_spell_color` reduction that
  `TurnSolver::EffectiveCost` (planner) had → executor over-paid Medallion-funded combos → unpayable → win
  never realized. Fix mirrors the block (copies-scale-before single floor). **RESIDUAL 2 FIXED+committed
  (`b169781`):** NOT the hypothesized payment-path divergence (instrumentation REFUTED it — untapped sources
  matched). Real cause: `TurnSolver::BuildPool`/`BuildNonCreaturePool` credited **storage lands** (Mercadian
  Bazaar / Dwarven Hold) at their STATIC per-tap yield instead of the **LIVE** `storage_counters` the executor
  uses via `PermanentManaYield` → a dead `sc=0` storage land added phantom firebreathing mana in the rollout →
  combat kill projected a turn early. Fix: pass `PermanentManaYield(p,*def)` to `AddSourceToPool` at both
  `BuildPool`-family sites (as `BuildAvailableMana` already does); gated by `storage_land` (Dragonstorm-only) →
  byte-identical for every non-storage deck. Verified: fd-diverge 2→0, nonconv 0, smoke 18/0/0 exact digests,
  goldfish avg 6.36→6.12, won 41→43, 0 win→loss. See ledger "RESIDUAL FIXED" section.
- **VERIFIED (user-flagged):** spliced Desperate Ritual cost = **{1}{R}{R}** with one Ruby Medallion, **{R}{R}**
  with two — correct in BOTH EffectiveCost fns after `dfe3fff` (copies-scale then single floor).
- **PERF FINDING:** full baseline profile gen ran **4.5 h / ~67 core-hours** (STOPPED). Not a hang — per-game
  ~1.45 s at d5 (combo-high), and the analyzer's land×threshold grid = hundreds-of-thousands of games. This is
  the "engine too slow → mulligan infeasible" showstopper → a **Stage-5f pruning heuristic** (behind
  `MTG_UNPRUNED`) is the gate before the deferred mulligan stage. Likely branch drivers: Lotus/Apex color-float
  variants + splice-count + storage-burst. NUANCE: Apex's black float CAN legitimately hard-cast a drawn
  Kolaghan, so naive color-pruning isn't safe — propose to user, don't auto-adopt.
- **CARD-SCORES-ONLY (user proposal) — GATE BUILT, GEN RUNNING.** `MTG_SKIP_GRID` env gate added in
  `AnalyzerEngine.cpp` (default OFF → byte-identical): skips `GridSearchLands` (Phase 3b, the ~4.5h grid) and
  emits `working_profile` (default land window + the DISCOVERED `required_pieces` + `min_color_sources` from
  Phases 1/2b, which still run) + the cheap `card_scores` + `hand_score_threshold=NO_GATE`. NO_GATE is
  deliberate: absent the joint grid we do NOT ship an unvalidated keep gate (the bolt-on gate was the old
  over-mulligan regression; the grid itself always included NO_GATE as the "decline" option). card_scores still
  drive the threshold-independent `HeuristicBottomPick` bottoming tiebreak. Runtime consumers confirmed by grep:
  `ComputeHandScore`→KeepHand gate (`AIEngine.cpp:539-542`) + `HeuristicBottomPick`→BottomCards (551-606) —
  both mulligan-only; NOT the in-turn play search. Gen running: `MTG_SKIP_GRID=1 analyze_deck.py --no-rebuild`
  (bg `be8nuwobn`, log `logs/dragonstorm_cardscores/`), ~1–3h. On done: verify profile + commit gate+profile.
  Open question for user: make card-scores-only the Stage-4 DEFAULT (skill update) vs Dragonstorm-only.
- **REMAINING Stage 5 (after fd-diverge=0 + card_scores):** multi-depth sanity (5b) + `verify_deck.py Dragonstorm`
  gate + reduced claude-play legality backstop (5d, sized down from 100 — disclose) + viewer-decision audit
  (`audit_viewer_decisions.py`, 5h).
- **USER-REVIEW ITEMS (for the wake-up report; do NOT silently resolve):** (a) VIEWER-WIRING Bucket-B choosers
  recorded-not-wired — Scourge ETB `target`, Lotus/Apex color, Dragonstorm tutor multi-pick, splice count,
  firebreathing count, storage burst — a substantial build; present as a decision list. (b) Irencrag Feat +
  Reality Spasm float-color fidelity (wild→colored; may rebaseline Hinata — verify impact FIRST, scope, flag).
  (c) Stage-5f pruning proposal (above). (d) [RESOLVED — firebreathing fix `b169781` IS byte-identical, fd=0.]
- **FLOW PREFS:** user reviews closely + corrects by TYPING (Stop cancels subagents — memory
  `user-stop-cancels-subagents`); relay corrections to running agents via SendMessage; autonomous overnight;
  mulligan generation is a SEPARATE deferred user-initiated stage (NOT part of analyze). Nothing at risk — all
  work committed. No subagents running; bg card-scores gen `be8nuwobn` in flight.

## Desperate Ritual — SPLICE (search-chosen count k) hook map
- **Plan variant:** add `int splice_count` to the `Action` struct (`src/ai/TurnSolver.h`, sibling to
  `chosen_x`). In `CollectActions` (copy the {X}-candidates or tutor-candidates enumeration loop): emit one
  Action per k = 0..(count of OTHER same-named "Desperate Ritual" in hand), all sharing the base hand_index
  (mutually exclusive in the subset enumerator), each with cost `(k+1)·{1}{R}` and ritual_float `(k+1)·3`.
- **Signature:** fold `splice_count` into the HUMAN-PLAY sub-decision block of `plan_signature`
  (`TurnSolver.cpp`, beside the chosen_x line) so human-play keeps each k distinct; autonomous dedup keys on
  cast-NAMES so distinct k collapse to one representative (fine — same as chosen_x precedent).
- **Scale cost AND float by (k+1) in ALL THREE paths (lockstep or fd-diverge):** ENUM (`a.cost=EffectiveCost`,
  `a.ritual_float=RitualFloatAmount`), ROLLOUT (`apply_one`: `EffectiveCost` + `ApplyRitualFloat`), EXECUTOR
  (`CastSpellFromHand` in AIEngine + `EffectHandler` `ApplyRitualFloat`). Thread k via a new optional
  `splice_count` on `StackEntry` (`src/core/GameState.h`, beside chosen_x). Give `ApplyRitualFloat`/
  `RitualFloatAmount` (`src/core/SpellEffects.h`) a multiplier arg (or pass pre-scaled) so all callers scale identically.
- **Keep-in-hand = FREE:** both cast paths remove ONLY the base copy; the k spliced copies are different hand
  entries, never touched → they stay in hand automatically.
- **Storm:** the (future) `spells_cast_this_turn` increment fires ONCE per cast invocation (the base cast),
  NOT per (k+1) — do not increment inside any k-loop. Each later hard-cast of a leftover copy is its own increment.
- **New param:** `bool splice_onto_arcane` (CardDatabase.h/.cpp). Cost/float derive from the card's own
  {1}{R} + `ritual_floating_mana:3` (+ `ritual_float_color:"R"`) — no extra cost/float params.
- **Viewer:** Bucket A — splice_count surfaces through the main_phase plan menu (emit beside chosen_x in
  `main.cpp`; `index.html` already renders variant params). No chooser.
- **Gotcha:** k computed from the hand snapshot at ENUM time; if a plan both splices and hard-casts the same
  copies, keep ENUM+apply consistent (compute k against start-of-phase hand).

## Apex of Power — impulse-exile-7 (this-turn) + conditional 10-of-one-color float
- **Impulse primitive:** copy the `stages_cards` exile-N-into-hand loop — EXECUTOR `EffectHandler`
  ResolveDrawSpell (staged, `expiry=turn_number+1`) / ROLLOUT `TurnSolver` apply_one DrawSpell branch — but
  exile **7** with `expiry = turn_number` (this-turn-only, like `ResolveExpressiveIteration` in SpellEffects.h).
  Apex is a custom spell → its resolution lives in the EffectHandler custom-else-branch + the TurnSolver
  apply_one custom branch (OR model it as a stages_cards DrawSpell so the existing draw-breakpoint re-solve fires).
- **Staged-LAND block:** staged lands are currently PLAYABLE (land enumeration collects hand lands with no
  m_is_staged filter; PlayLandByName even prefers the staged copy). To block Apex-staged lands WITHOUT
  regressing Light Up / Expressive Iteration / Soulfire (their staged lands MUST stay playable), add a
  DEDICATED per-instance marker (a new Card bit e.g. `m_impulse_no_land`, set only in Apex's exile loop) —
  do NOT key on `m_is_staged` or `expiry==turn_number`.
- **Conditional 10-of-one-color float (cast-from-hand only):** reuse the **chosen-color-float dimension Lotus
  builds** (`chosen_float_color` on Action; color→floating_mana.<c> switch already in `ApplyRitualFloat`). Add
  10 of the chosen color (NOT wild) in the custom resolvers, lockstep. **Cast-from-hand gate:** no cast-source
  flag on StackEntry today, BUT the effective flag exists on the Card — a hand Apex has `m_is_staged=false`; an
  Apex cast off staged-exile (Apex-off-Apex) has `m_is_staged=true`. So "cast from hand" == `!castcard.m_is_staged`
  at the cast site; stamp a `bool cast_from_hand` onto StackEntry = `!m_is_staged`, gate the 10-float on it.
- **Planner:** the 7 staged cards become castable via the draw-breakpoint re-solve (model like stages_cards so
  it fires); credit the 10 float as within-turn combo mana (like `a.ritual_float` / add to floating_mana before
  the re-solve). Eval fast-path (`wins_this_turn`) must NOT project the 10 mana + 7 exiles (over-project →
  fd-diverge); rollout finds kills.
- **New params:** `impulse_exile`(=7), `impulse_expiry_this_turn`(=true), `impulse_float_amount`(=10); the color
  is the searched `chosen_float_color`, not static.
- **Viewer:** which exiled cards to cast = Bucket A (`m_is_staged` surfaced in main.cpp; cast via plan menu).
  The 10-mana color = Bucket B (note only).
- **Gotcha:** read the cast-from-hand gate at the SHARED cast site; Apex-off-Apex is the only case the 10 is
  withheld. Staged-land block must not regress other stagers. 3-way lockstep on exile-7 + 10-color-float.

## Cross-cutting anchors (function names — stable)
- `Action` struct `src/ai/TurnSolver.h`; `StackEntry` `src/core/GameState.h`; `ApplyRitualFloat` + colored-float
  switch `src/core/SpellEffects.h`; `plan_signature` `src/ai/TurnSolver.cpp`.
- Three cast paths: ENUM = `CollectActions` (TurnSolver.cpp), ROLLOUT = `apply_one` (TurnSolver.cpp), EXECUTOR
  = `CastSpellFromHand` (AIEngine.cpp) + `EffectHandler` custom resolve (EffectHandler.cpp).
- CardParams declared `src/cards/CardDatabase.h`, parsed `src/cards/CardDatabase.cpp`.
- Storm counter (`spells_cast_this_turn`) does NOT exist yet — Dragonstorm introduces it; reset in
  `GameEngine::UntapStep`, increment ONCE per cast at the shared cast site (so Lotus off-suspend + all spells count).
