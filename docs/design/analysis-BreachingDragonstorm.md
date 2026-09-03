# Analysis ledger — BreachingDragonstorm

Deck: `decks/BreachingDragonstorm/BreachingDragonstorm.cod` (60 cards).
Started 2026-09-03. Status: **ANALYZED (GATE PASS)** — verify_deck all-green (coverage /
card_fields / viewer / viewer_wiring / mismatch / play_invariants 276-decision sweep /
claude_sweep); CI green on ubuntu + windows incl. determinism parity. Outstanding:
the 5c2 --blocks 48 confirmation run (first run: 0 changed of 12,000 paired — the
tie-break never fires at play settings; default ON is fine either way per the script);
user sign-off on the PROVISIONAL deferrals (batched in the session's closing report).
Mulligan/value-leaf generation NOT started (user-initiated final stage, per policy).

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

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-09-03)

`verify_deck.py decks/BreachingDragonstorm/BreachingDragonstorm.cod --no-network --write-ledger` -> **PASS**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 15 cards full (missing=0, partial=0) |
| card_costs | SKIP | yes | skipped (--no-network) |
| card_fields | PASS | yes | 304 cards match snapshot (cost/PT/types/keywords); 8 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | 2 type(s) wired (emitter + GUI): demonstrate, free_cast |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 7002] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/276 decisions: determinism+integrity+progress hold |
| claude_sweep | PASS | yes | Claude-play sweep recorded, 0 unresolved flags |

### Pending user sign-off (block the gate until fixed OR approved below)
_none_ -- every blocking gate is green or already signed off.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Maelstrom Wanderer: Haste grant is UNCONDITIONAL and SELF-INCLUSIVE ('Creatures you control', not 'Other') -> grants_haste + affects_all_creatures (NOT the mono-tribal subtypes_affected collapse -- this deck's creatures are Elemental/Dinosaur/Shapeshifter/Human Pirate). Cascade, cascade = cascade_max_mv 8 (== cmc) + cascade_count 2, each instance a Triggered{Cascade} stack entry resolving fully (walk + free cast) before the next, and BEFORE the Wanderer itself resolves (CR 601.2i/702.85a) -- so the hits enter first; haste is continuous, so they attack this turn either way. PARTIAL: exiled non-hits bottom in EXILE ORDER, not 'a random order' (deterministic shared cascade convention -- determinism-parity CI). PARTIAL: the 'may' on each free cast is provider-owned auto-YES in autonomous play (DecisionProvider::TakeFreeCast; the human free-cast chooser overrides at real resolution) -- vs a passive opponent a free nonland spell is never worse than bottoming. Legend rule modelled and LIVE (4 copies): a duplicate dies, but its two cascade triggers still fire (cascade triggers on CAST).
- coverage deferral -- Annoyed Altisaur: Reach INERT (the opponent never attacks, so this never blocks) and Trample INERT (no blockers) in goldfishing; both declared for faithful keywords. Cascade via cascade_max_mv 7 (== cmc): a cast trigger resolving BEFORE the creature enters. PARTIAL: bottoming order = exile order, deterministic (shared cascade convention). PARTIAL: the free-cast 'may' is provider-owned auto-YES (TakeFreeCast; human chooser overrides) -- declining only bottoms the card.
- coverage deferral -- Sakashima's Protege: Cascade 6 (cascade_max_mv == cmc), a cast trigger: the flipped spell fully resolves BEFORE this creature enters -- which is what makes the copy clause live (a Breaching Dragonstorm or Creative Technique flipped off the cascade is itself a this-turn entrant). Copy = enter_as_copy_of_entrant: as it enters, its card is replaced by the chosen source permanent's PRINTED card (CR 706.2 -- no counters/attachments/temp pumps), keeping this cast's m_number; enter triggers then fire for the COPIED card (a copy of Breaching Dragonstorm re-fires its exile trigger; a copy of Boarding Party is a 6/3 with haste). The source pick is a searched plan variant (one per battlefield entrant + decline; heuristic covers same-plan entrants) and a human board-click at real resolution; the heuristic skips a copy that would immediately legend-rule itself away. PARTIAL: Flash is inert -- the engine models no priority window outside a main phase with an empty stack, so instant-speed casting is unreachable for every card. PARTIAL: 'any permanent that entered this turn' is scoped to permanents STILL on the battlefield with entered_this_turn; the entered-and-since-left set is provably empty in this deck (nothing removes/bounces/sacrifices our permanents -- Breaching Dragonstorm's self-bounce needs a Dragon and the deck runs zero). PARTIAL: a copy that leaves the battlefield is recorded under the COPIED name in the graveyard; inert -- only the legend rule can kill it here and nothing reads the graveyard.
- coverage deferral -- Boarding Party: Cascade via cascade_max_mv 6 (== cmc), a cast trigger resolving BEFORE the creature enters. PARTIAL: bottoming order = exile order, deterministic (shared cascade convention). PARTIAL: the free-cast 'may' is provider-owned auto-YES (TakeFreeCast; human chooser overrides).
- coverage deferral -- Breaching Dragonstorm: CLAUSE 1 (etb_exile_until_nonland): on ENTER, exile from the top until the first NONLAND -- NO mana-value bound on the walk (unlike cascade). The exiled LANDS have no oracle disposition and REMAIN IN EXILE permanently (real cumulative library thinning, ~1.6 lands/trigger in this 37-land list) -- deliberately NOT bottomed. The found nonland may be cast FREE iff its mv <= etb_exile_free_cast_max_mv (8 -- an ORACLE-PRINTED constant, NOT this card's cmc of 5; do not extend the cascade_max_mv==cmc audit to it); declining (or a cast a restriction forbids) puts it in HAND. The 'may' is provider-owned auto-YES in autonomous play (TakeFreeCast); the human free-cast chooser overrides. Rulings absorbed: the free cast happens during the trigger's resolution (cannot be banked), type timing ignored, X=0. Fired through the universal enter path, so a Sakashima's Protege entering as a copy re-fires it. CLAUSE 2 (Dragon bounce) NOT IMPLEMENTED -- PROVISIONAL COMPOSITION-BOUND deferral: this 60 contains no Dragon-subtype permanent and creates no Dragon tokens, so the trigger can never fire. It goes LIVE the moment any Dragon is added -- any deck-screening arm introducing a Dragon MUST implement it first (self-bounce-to-hand + recast decision, lockstep both worlds).
- coverage deferral -- Call Forth the Tempest: Both cascade instances modelled (cascade_max_mv 8 == cmc, cascade_count 2), each a cast trigger resolving fully before the next and before the spell. PARTIAL: the damage clause is NOT modelled; WHY inert -- it hits ONLY creatures your opponents control (never a player), and the goldfish opponent's creatures never block or attack, so killing them cannot change the clock or the win turn; no card in this deck triggers on opponent creature death or reads opponent creature count. PARTIAL: bottoming order = exile order, deterministic (shared cascade convention). PARTIAL: the free-cast 'may' is provider-owned auto-YES (TakeFreeCast; human chooser overrides).
- coverage deferral -- Creative Technique: shuffle_reveal_freecast: the shuffle rides the CRN reshuffle (ShuffleAfterSearch) so executor and rollout walk the same library; the revealed non-hits bottom in reveal order, which off a just-shuffled library IS a uniform random order (faithful, no second shuffle). A declined/forbidden hit STAYS EXILED. demonstrate (CR 702.145): YOUR copy is fully modelled -- it resolves BEFORE the original (2021-04-16 ruling), is NOT a cast (no cast triggers, no storm/cast increments) and ceases to exist on resolution; the copy yes/no is provider-owned (DemonstrateCopy, default copy) with a human demonstrate chooser at real resolution. PARTIAL: demonstrate's opponent half ('choose an opponent to also copy it') is not modelled; WHY inert -- the goldfish opponent is never dealt a library (GoldFishRunner::DeckTouchesOpponentZones is false for this deck: no card carries exile_opponent_top_cost) and never casts, so their copy reveals, exiles and casts nothing. DECK-CONTINGENT proof: revisit if any card here gains exile_opponent_top_cost. Each free cast IS a cast (+1 storm each); the copies themselves add 0.
- coverage deferral -- Dwarven Ruins: Fallen Empires sac land -- NOT a depletion-counter land (contrast Saprazzan Skerry): an ordinary repeatable {T}: Add {R} PLUS a separate one-shot {T},Sacrifice: Add {R}{R}, mutually exclusive per turn because both tap the same permanent. Modelled with ZERO new params: basic_land produces:[R
- coverage deferral -- Svyelunite Temple: Fallen Empires sac land, exact blue twin of Dwarven Ruins: basic_land produces:[U
- card_costs SKIPPED (--no-network) -- Scryfall cost/cmc reality-diff not run
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled 
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is 
- allowlisted divergence -- Glorybringer [keywords]: 'Exert' is a real keyword but its use is OPTIONAL and provably worthless here: exerting costs the next untap step (so Glorybringer cannot attack the f
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.56); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.75); scryfall="As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.75); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.44); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
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
- oracle_text advisory -- Worthy Knight: oracle_text diverges (similarity 0.45); scryfall='Whenever you cast a Knight spell, create a 1/1 white Human creature token.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.77); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may 
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.41); scryfall='First strike (This creature deals combat damage before creatures without first strike.)\nOther Knight creature
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.\n{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins\nOther Knights you control get +2/+2 and have protection from Assassins.\nWhen Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance\nAdeline's power is equal to the number of creatures you control.\nWhenever you attack, for each opp
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)\n{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.66); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying\nRot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.\nWhenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.44); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.61); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.\nTarget
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.57); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.\n
- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.43); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.55); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.\nWhenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.11); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.32); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.\nDraw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.20); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.58); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.\nFerocious — If you control a creature with power 4 or greater, those creatures don't 
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample\nSpells you cast cost {1} less to cast for each target.\nSpells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {U}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.30); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of 
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
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.25); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.34); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.45); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.\n{T}: Add one mana of any color.'
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
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.24); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)\nEquip {0}"
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
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.41); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
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
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.\n{T}: Add {G} or {W}.\n{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.33); scryfall='{T}: Add {C}.\n{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
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
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.09); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).\n{T}: Add {C}.\n{T}, Pay {E}: Add one mana of any colo
- oracle_text advisory -- Maelstrom Wanderer: oracle_text diverges (similarity 0.34); scryfall='Creatures you control have haste.\nCascade, cascade (When you cast this spell, exile cards from the top of you
- oracle_text advisory -- Annoyed Altisaur: oracle_text diverges (similarity 0.50); scryfall='Reach, trample\nCascade (When you cast this spell, exile cards from the top of your library until you exile a 
- oracle_text advisory -- Sakashima's Protege: oracle_text diverges (similarity 0.28); scryfall='Flash\nCascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Boarding Party: oracle_text diverges (similarity 0.62); scryfall='Haste\nCascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Breaching Dragonstorm: oracle_text diverges (similarity 0.31); scryfall="When this enchantment enters, exile cards from the top of your library until you exile a nonland card. You may
- oracle_text advisory -- Call Forth the Tempest: oracle_text diverges (similarity 0.54); scryfall="Cascade, cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland
- oracle_text advisory -- Creative Technique: oracle_text diverges (similarity 0.37); scryfall='Demonstrate (When you cast this spell, you may copy it. If you do, choose an opponent to also copy it.)\nShuff
- oracle_text advisory -- Dwarven Ruins: oracle_text diverges (similarity 0.16); scryfall='This land enters tapped.\n{T}: Add {R}.\n{T}, Sacrifice this land: Add {R}{R}.'
- oracle_text advisory -- Svyelunite Temple: oracle_text diverges (similarity 0.28); scryfall='This land enters tapped.\n{T}: Add {U}.\n{T}, Sacrifice this land: Add {U}{U}.'
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- claude_sweep recorded at commit 5e7d884e (HEAD 39a64658b54c); re-run if play changed since (NOTE: BreachingDragonstorm is NOT a regression case, so NO digest tracks its play -- nothing will tell you when this record goes stale. Re-run the sweep on judgement, or add the deck to the suite).

<!-- verify_deck:end -->
