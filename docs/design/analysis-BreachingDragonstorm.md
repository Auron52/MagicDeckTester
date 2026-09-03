# Analysis ledger — BreachingDragonstorm

Deck: `decks/BreachingDragonstorm/BreachingDragonstorm.cod` (60 cards).
Started 2026-09-03. Status: **IN PROGRESS — Stage 2 (per-card research fan-out running)**.

## Decklist

| # | Card | Role |
|---|------|------|
| 4 | Maelstrom Wanderer | 8mv cascade×2 + haste anthem |
| 4 | Annoyed Altisaur | 7mv cascade beater |
| 2 | Sakashima's Protege | clone (flash) |
| 4 | Boarding Party | 6mv cascade beater |
| 4 | Breaching Dragonstorm | exile-until-MV≥N free-cast enchantment |
| 1 | Call Forth the Tempest | (research pending) |
| 4 | Creative Technique | demonstrate + exile-until-nonland free-cast |
| 4 | Dwarven Ruins | FE sac land (R) |
| 4 | Ferrous Lake | filter dual U/R — **already implemented** |
| 4 | Mossfire Valley | filter dual R/G |
| 9 | Mountain | **already implemented** |
| 4 | Peat Bog | depletion land (B — off-color) |
| 4 | Remote Farm | depletion land (W — off-color) |
| 4 | Saprazzan Skerry | depletion land (U) — **already implemented** |
| 4 | Svyelunite Temple | FE sac land (U) |

## Stage 1 — Coverage (2026-09-03)

12 missing: Maelstrom Wanderer, Annoyed Altisaur, Sakashima's Protege, Boarding Party,
Breaching Dragonstorm, Call Forth the Tempest, Creative Technique, Dwarven Ruins,
Mossfire Valley, Peat Bog, Remote Farm, Svyelunite Temple.
Full: Ferrous Lake, Mountain, Saprazzan Skerry. No sideboard.

## Engine infra survey (pre-fan-out)

- Cascade exists: `cascade_max_mv` (CardDatabase.h ~220; EffectHandler::ResolveCascade ~668;
  TurnSolver rollout twin ~18363; leaf valuation ~3639). Only prior cascade card is Throes of
  Chaos (sorcery). **Open questions for integration:** does cascade fire on CREATURE casts;
  no multi-cascade count param (Maelstrom Wanderer needs cascade×2).
- Depletion lands exist: `enters_tapped_with_depletion` + `produces_amount` (Saprazzan Skerry,
  Sandstone Needle, Dwarven Hold, Mercadian Bazaar).
- Filter duals exist: `ramp_filter` (Ferrous Lake, Cascade Bluffs).
- Fallen Empires sac lands (Dwarven Ruins, Svyelunite Temple): model TBD from research.

## Stage 2 — Cards (fan-out launched 2026-09-03)

| Card | Tier | Status | Notes |
|------|------|--------|-------|
| Maelstrom Wanderer | 2 | draft ready | {5}{G}{U}{R} Legendary 7/5, `cascade_max_mv:8` + `cascade_count:2`, haste grant = `grants_haste`+`affects_all_creatures` (NOT the Karrthus subtype collapse — heterogeneous creatures) + HasHasteFromLords one-liner (~2526) + EvalCard self-haste blind spot (~3608 + 4 attack-planning sites). keywords MUST be [] (audit MODELED_ELSEWHERE). Legend rule live (4 copies; dup's cascades still fire). EmitReveal gap in ResolveCascade flagged |
| Annoyed Altisaur | 3 | draft ready | {5}{G}{G} 6/5 Reach+Trample (both inert-noted), `cascade_max_mv:7`. Owns the creature-cascade wiring plan: EffectHandler ResolveImpl top hook (~103) + `inline_resolve` param on ResolveCascade; TurnSolver hoist cascade before `else if (is_creature)` (18089); d0 valuation credit at ~3583/3612. Flags: cascade-cast sub-decisions all defaulted (core-invariant, own once deck-wide); GG only from 4x Mossfire Valley |
| Sakashima's Protege | 3 | draft ready | {4}{U}{U} 3/1 Flash Shapeshifter, `cascade_max_mv:6` + NEW `enter_as_copy_of_entrant`. Copy machinery: `copy_target` on StackEntry+Action, plan variants (dedup key!), `CopyTargetCandidates` provider hook (no narrowing), `ApplyEnterAsCopy` helper reusing CreateTrickCopyToken mechanism, viewer = existing `target` type at shared site. Flash inert (no non-main priority window). Cascade-before-enter ordering is LOAD-BEARING (copy targets = this turn's entrants) |
| Boarding Party | 2 | draft ready | {5}{R} 6/3 Haste, `cascade_max_mv:6`. Confirms both creature-cascade wiring sites; verified byte-identical placement for Throes. Keyword convention decision at integration: keep `keywords` minus "Cascade" (Throes/audit MODELED_ELSEWHERE convention) vs new enum. Ordering (cascade after creature enters) proposed inert-deferred |
| Breaching Dragonstorm | 3 | draft ready | {4}{R} Enchantment. NOT inverse-cascade: ETB exile until FIRST nonland (no MV bound on the loop), MV≤8 gates only the free cast, non-hit LANDS STAY IN EXILE (permanent thinning, ~1.6 lands/trigger — load-bearing), decline → HAND. NEW `etb_exile_until_nonland` + `etb_exile_free_cast_max_mv:8` (oracle constant, NOT cmc — audit hazard noted). Dragon-bounce clause = PROVISIONAL composition-bound deferral (zero Dragons in the 60). Cast-vs-hand must be provider-owned/searched, never an inline if (core invariant) |
| Call Forth the Tempest | 2 | draft ready | {5}{R}{R}{R} sorcery, `cascade_max_mv:8` + NEW `cascade_count:2`. Damage clause (opponent creatures only) = PROVISIONAL inert deferral (goldfish opp creatures never block; + would fd-diverge on stack-vs-inline cascade MV timing). Leaf valuation ~3638 scales by count |
| Creative Technique | 2/3 | draft ready | {4}{R} Sorcery, NEW `demonstrate` + `shuffle_reveal_freecast` (shuffle via `ShuffleAfterSearch` CRN — highest-risk line; ad-hoc shuffle desyncs worlds). Bottom-in-reveal-order is FAITHFUL (just-shuffled prefix is already uniform). Demonstrate copy: payload ×2, copy is NOT a cast (no storm/cast increment), re-entrancy depth cap. Opponent copy = tree-proven inert deferral (opponent never dealt a library; deck-contingent proof). 8 draw-engine disjunction sites must all gain the new param. Found LATENT bug: executor cascade free-cast never increments spells_cast_this_turn (rollout does) — inert for this deck, documented not fixed |

## USER DIRECTIVE (2026-09-03, mid-run)

"Implement the deck the best you can. We will need to actually make things more involved,
possibly to the level of the actual magic stack, so we can get this deck to work as intended.
We will need to restore exiled cards to the bottom of the library after cascade."

→ Full-faithfulness build authorized (Tier 3 machinery where needed, no scope-shyness).
→ Cascade non-hits: bottom of library after each cascade instance (CR 702.85a) — matches plan.
→ Breaching Dragonstorm distinction kept (oracle gives no disposition → non-hits STAY exiled);
  flagged to user in case they want otherwise.

## Integration design decisions (settled by orchestrator, 2026-09-03)

1. **Cascade ordering = CR-correct (target resolves BEFORE the permanent enters), both worlds.**
   Load-bearing for Protege's copy clause. Executor: cascade hook at top of ResolveImpl gated on
   `cascade_max_mv>0 && !IsInstantOrSorcery()`, inline resolution; sorcery site (line ~336) kept
   for Throes byte-identity, but `cascade_count>1` iterations resolve sequentially inline
   (stack-push double-cascade would do both exiles before either hit resolves — lockstep break).
   Rollout: hoist body to `do_cascade` lambda; permanents call it BEFORE `battlefield.push_back`.
2. **`cascade_count`** (default 1) — one shared param; leaf valuation scales by it and the
   creature branch gains the cascade credit (extends the existing generic `3*DMG` estimate).
3. **Keywords convention**: "Cascade"/"Demonstrate" NOT in `keywords` arrays (param-modelled;
   MODELED_ELSEWHERE_KEYWORDS strips them audit-side — verify demonstrate is in that set, add if not).
4. **Wanderer haste**: `grants_haste` + `affects_all_creatures`; `HasHasteFromLords` gains the
   `affects_all_creatures` arm (byte-identical elsewhere: only Benalish Marshal has the param, no
   grants_haste); EvalCard + attack-planning self-haste blind spots audited together.
5. **Free-cast "may" choices** (cascade / BD / CT): search side = provider-owned always-cast
   (named, disclosed 6a; Generic default), human side = real chooser (`free_cast` reuse for BD;
   new `demonstrate` yes/no type for CT; cascade decline disclosed as inert gap, not built).
6. **BD non-hits stay in exile** (never bottomed) — the single most important fidelity point.
7. **Storm-count executor/rollout divergence on cascade free-casts**: pre-existing, latent here
   (nothing in this deck reads `spells_cast_this_turn` once CFT's damage clause is deferred);
   NOT fixed this run (fix could shift Throes-deck GT) — surfaced to user as follow-up.
8. **Cascade `EmitReveal`** added (viewer shows the exile walk + hit).
| Dwarven Ruins | 1 | draft ready | NOT a depletion land: enters tapped, {T}:{R} + sac one-shot {R}{R} via existing `sac_for_mana_amount:2`/`sac_for_mana_color:R` (Lotus Bloom machinery). First basic_land+sac_for_mana combo — verify no double-count at integration |
| Mossfire Valley | 1 | draft ready | mirrors Ferrous Lake (`produces:[R,G]`, `ramp_filter`) |
| Peat Bog | 1 | draft ready | mirrors Saprazzan Skerry (depletion 2, `produces:[B]`, amount 2) — off-color B, generic-only in practice |
| Remote Farm | 1 | draft ready | mirrors Saprazzan Skerry (depletion 2, `produces:[W]`, amount 2) — off-color W, generic-only in practice |
| Svyelunite Temple | 1/2 | draft ready | Same shape as Dwarven Ruins: `produces:[U]` + `enters_tapped` + `sac_for_mana_amount:2`/`sac_for_mana_color:U`. Both agents independently verified SacForMana composes with basic_land tap (mutual exclusion via `!p.tapped`); known SAFE over-count at two feasibility-ceiling sites (TurnSolver ~8032, ~9460) — under-prunes only, not a bug. Smoke-test the combo after build |

## Integration DONE (2026-09-03) — the real-stack build

Implemented per the user's stack directive:
- **StackEntry::TriggerKind** {Cascade, EtbExileFreeCast, Demonstrate} + `is_copy`;
  cast = push spell + push cast triggers above + count (CastSpellFromHand / retrace site /
  `EffectHandler::PushFreeCast`). Free casts are REAL casts (counted, fire on-cast/prowess,
  push their own triggers) — this also closed the executor-vs-rollout storm-count divergence.
- Shared walk helpers (SpellEffects.h): `WalkCascadeExile` (bottoms non-hits, CR 702.85a),
  `WalkExileUntilNonland` (BD: non-hit lands STAY exiled), `WalkRevealUntilNonland` (CT);
  pending-ETB queue drained by executor (Triggered entries) + rollout (inline, apply_one).
- Rollout twin: cascade/demonstrate sequenced BEFORE the spell's own effect in apply_one
  (hit enters first — load-bearing for Protege); CT payload lambda shared by copy+original;
  CT shuffle via ShuffleAfterSearch CRN.
- Protege: `enter_as_copy_of_entrant`, copy-source on reused `enchant_target` (variants: one
  per battlefield entrant + decline(-1) + heuristic(0); signature folds it — lossless);
  `ChooseCopyEntrantIndex` (legend-rule-aware heuristic + human board-click); legend gate
  extended to the param in both worlds.
- Wanderer: `grants_haste`+`affects_all_creatures` + HasHasteFromLords arm + EvalCard
  self-haste; cascade credit added to the creature branch (scaled by cascade_count).
- Provider hooks: `TakeFreeCast` / `DemonstrateCopy` (Generic = always yes; disclosed 6a).
- Classification sites patched: OrderingOpaque, note_draw_engine, is_draw_engine, KeepModel
  ×3, IsDrawEnginey(DP), flood gate; EmitReveal added to all three walks.
- Viewer: FreeCastChooser gained `source` (labels cascade/BD/CT vs Archangel); NEW
  `demonstrate` type (4 sites: GameLogger.h chooser + EffectHandler call + main.cpp emitter
  + index.html panel); DECISIONS.md rows; audit manifest rows (cascade_max_mv moved
  DEFERRED→MANIFEST/free_cast; cascade_count + etb_exile_free_cast_max_mv INERT;
  sac_for_mana_color INERT-constant [PROVISIONAL classification, needs sign-off]).

## Gates run

- Build green; 59/59 unit tests (816 assertions).
- **Smoke regression: 45/48 byte-identical; the 3 treasure_hunt configs play-changed at
  IDENTICAL scores (slower=0 faster=0, hands/draws identical) — the cascade-ordering +
  cast-counting change on Throes of Chaos. Analyzed + ACCEPTED into GT.**
- Cost audit: all 12 new cards verified verbatim vs Scryfall (bulk audit 429-limited on old
  cards — transients); cascade_max_mv == cmc for all five cascade cards.
- Static viewer audit: clean ("no oracle-text choice phrase left unmodeled").
- d0 10-game probe: avg 5.0, one unwon (gi6); d3 single-game probe: legal T3 kill via
  double-Svyelunite sac burst → Creative Technique demonstrate chain → 55 damage. BD
  non-hits verified staying in exile; Wanderer double-cascade duplicate walks verified
  consistent (second instance walks the library the first bottomed).

## Viewer classifications (2c-ter)

| Choice | Surface | Status |
|---|---|---|
| Cascade hit | not a decision (rules-forced) | n/a |
| Cascade/BD/CT free-cast "may" | `free_cast` decision (source-labelled) at trigger resolution | WIRED |
| CT demonstrate copy | NEW `demonstrate` yes/no type | WIRED |
| Protege copy source | main_phase plan variants + `target` board-click at resolution | WIRED |
| Sac-land crack timing | main_phase plan action (SacForMana) | existing |
| Depletion/filter lands | no choice | n/a |

## Approved deferrals

(none yet — all deferrals PROVISIONAL until user sign-off)

## Verification verdicts (Stage 5)

- **5a mismatch harnesses: CLEAN.** 100 games seed 3003: 0 `[nonconv]` (d3 b200),
  0 `[fd-diverge]` (d5 b200 + MTG_FULL_DEPTH). Executor and rollout realise identical states
  through the cascade/demonstrate/ETB chains.
- **5b multi-depth (100 games, seed 3003, b200): monotonic + plausible.**
  d0 avg 5.31 (2 unwon: gi1, gi12); d3 avg 3.57; d5 avg 3.57 — d3 and d5 per-game IDENTICAL
  (search plateaus at d3; the burst lines are shallow). 62% T3 kills at searched depth —
  matches the deck's real clock (double sac-land burst = 5 mana on T3).
- **4a provider audit: Generic (intended for a new deck).**
- **5c2 leaf tie-break check: running.**
- **5d claude-play sweep: 18 games seed 7100 gi0-17 fanned out (Opus wave all lost to
  API 529 Overloaded; full relaunch on Sonnet — the skill's default model).**
- **Profile (Stage 4):** DISCARD_INERT (no cleanup shed ever reached); NO_COST_INTERACTIONS;
  min_playable 0 / min_lands 1 (nothing castable early ever — correct for a 5-drop-floor deck).

## 5d sweep findings — queued fixes (implement AFTER the sweep completes; no mid-sweep rebuild)

1. **REAL (gi0 flag 3): free_cast/demonstrate choosers dead in claude-play.** Wired at the
   executor's EffectHandler trigger resolvers, but a claude-play plan executes via
   TurnSolver::ApplyPlan's apply_one — the exact 2026-07-01 retrace-chooser trap. Fix:
   consult g_play_free_cast_chooser / g_play_demonstrate_chooser (null-gated) in apply_one's
   cascade block, ETB drain, and CT payload lambda. Search/rollout byte-identical
   (RevealLogPause nulls them).
2. **REAL (gi0 flag 2): Protege cascade-flip target list truncated to creatures** — the
   emitted candidate list omitted the 4 entered Breaching Dragonstorms ("any permanent").
   Fix: the target-decision emission for the copy choice must enumerate ALL this-turn
   entrants (truncated list = surfacing bug per the skill).
3. **DISMISSED class (gi0 flag 1, gi3 flag 1): unpayable plan offered then server-truth
   dropped** (`dropped_casts`) — the documented engine-wide benign-optimism + afford-drop
   policy; rollout scores the plan without the phantom cast, so search pricing is consistent.
   Pre-existing, not deck-specific. Disclosed 6a (viewer UX note).

## Claude-play sweep

commit: 5e7d884e (working tree: the BreachingDragonstorm onboarding, pre-commit)
seeds: 7100
games: 18 (gi 0-17; Opus wave lost to API 529, full relaunch on Sonnet)
flags: 0 unresolved

Results: **AI won T3 in all 18 games** (d5 b200). Claude matched T3 in 10, T4 in 7, T5 in 1
(gi2, self-attributed misplays). `claude_win < ai_win` nowhere -> zero AI-misplay candidates.
Zero state-corruption flags; every mana-math / combat-total / legend-rule / chain audit the
agents ran came back consistent with cards.json.

All raised flags fell into three classes, each resolved:
1. **FIXED — free_cast/demonstrate choosers dead in claude-play** (gi0/1/7): wired at
   EffectHandler's stack resolvers but the real game executes via apply_one (the retrace-chooser
   lesson). Choosers now consulted (null-gated) in apply_one's cascade block, ETB drain, and CT
   payload. Verified live: replaying gi0's chain now surfaces 20 free_cast + 4 demonstrate
   decisions.
2. **FIXED — Protege copy-choice used the generic damage-target enumerator**
   (gi0/4/5/6/8/9/10/11/12/14/15/16/17): offered faces + stale creatures (silent no-ops),
   hid noncreature entrants, no decline. New `copy_entrant` branch in main.cpp's
   target_chooser lists EVERY this-turn entrant (both sides, any permanent type) +
   decline (min_targets 0 -> enter as printed; ChooseCopyEntrantIndex maps an empty pick to
   decline, not the heuristic). Verified live: legal_targets now lists the 4 Breaching
   Dragonstorms + a Ferrous Lake entrant, `copy` flag + note attached.
3. **DISMISSED (pre-existing class) — unpayable plans offered then server-truth dropped**
   (gi0/1/2/3/10/15/16/17): the engine-wide benign-optimism + afford-drop policy; the rollout
   scores such plans WITHOUT the phantom cast, so search pricing is consistent, and gi16
   verified the graceful re-prompt with floating mana. ESCALATED as a viewer follow-up
   (below): on this deck the drop can land after an irreversible one-shot sac-land crack
   (gi2 lost a real turn to it), so the human-facing plan list ideally should not include
   plans whose own bundled mana cannot pay their advertised casts.
   Also dismissed: gi13's "filter-land activation not itemized in the plan summary" —
   engine-wide display convention (mana payment is engine-managed, never an action item).

## Open questions surfaced to user (non-blocking)

(none yet)
