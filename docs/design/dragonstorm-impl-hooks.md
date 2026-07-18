# Dragonstorm — implementation hook map + session resume state

Companion to `analysis-Dragonstorm.md` (the ledger). Captures the research hook-map for the
remaining spell mechanics and the mid-build resume state so a compaction/handoff loses nothing.
**Line numbers drift** (concurrent edits) — anchors below are by FUNCTION/PATTERN, which are stable.

## Session resume state (2026-07-18) — READ FIRST on resume
- **Progress: 12/16 Dragonstorm cards implemented, build green, byte-identity hinata/slivers/th = ALL PASS.**
  DONE: Pyretic+Seething (Tier1 cards.json), Kolaghan+Karrthus (Tier2 cards.json), `ritual_float_color`
  (rituals→red), Ruby Medallion (`reduces_spell_color`), Rite of Flame (gy self-scaling + triangular
  planner credit), **kill-engine** (Scourge ping / Lathliss ETB token / Utvara attack tokens /
  firebreathing / `is_token`; shared `OnDragonEnters`; token-first ping verified 3+3+5+5=16), **storage
  lands** (variable PARTIAL burst + exact-off-by-one charge; template `basic_land`).
- **REMAINING (4):** Lotus Bloom (agent RUNNING as of this note), Desperate Ritual splice, **Dragonstorm**
  (storm counter + tutor-to-battlefield + shuffle + provider heuristic), Apex of Power. Coverage: 4 missing.
- **CRITICAL Dragonstorm wiring:** the tutor-to-battlefield PUT must route each Dragon through the shared
  `OnDragonEnters` (or the same `EffectHandler::EnterBattlefield` the executor uses) so put-in Dragons ping
  Scourge / trigger Lathliss. Hook exists, wired only at hard-cast + `CreateToken` today. Provider put-order
  + selection heuristic is fully spec'd in the ledger ("DRAGONSTORM tutor-to-battlefield SELECTION HEURISTIC"
  + "ORDER RULE": Lathliss → Scourges → other Dragons; Karrthus>Kolaghan; the no-Lathliss / no-haste cases).
- **Servers:** isolated play server on **:8081** using `logs/play_snapshot/mtg` (a FROZEN copy of the
  kill-engine binary — insulated from agent rebuilds of `build/Release/mtg`). A second, collision-prone
  server on **:8080** uses the live `build/Release/mtg` (likely user-launched). Snapshot = `logs/play_snapshot/mtg`.
- **Flow prefs:** user reviews closely + corrects mid-flight by TYPING (pressing Stop cancels running
  subagents — see memory `user-stop-cancels-subagents`). Relay user corrections to running agents via
  SendMessage. Keep momentum (don't halt the build), stay light/responsive.
- **End-of-build follow-ups:** Irencrag Feat + Reality Spasm float-color fidelity (wild→red/colored; would
  rebaseline Hinata — do scoped, after convergence); `scripts/audit_card_fields.py --update` + commit the
  scryfall snapshot for the new cards; **Stage 5** (nonconv/fd-diverge, multi-depth, 100-game claude-play,
  5h viewer surface) + full smoke; wire 5h viewer choosers (Scourge ETB `target`, firebreathing count,
  storage burst confirmed Bucket-A, Lotus/Apex color, splice count Bucket-A).

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
