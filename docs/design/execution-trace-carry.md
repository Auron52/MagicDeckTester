# Execution-trace carry (reuse the prior for cells the change didn't touch)

**Status: PHASE A BUILT + validated 2026-07-07. The lever that cuts the NEAR-THRESHOLD + BOTTOMING re-run
cost — the part statistical change-detection cannot (`docs/design/change-detection-carry.md` finding).**
Same-list / new-commit only (play-logic changes); small deck changes are a different animal (see end).

## 2026-08-12: the recording was DEAD on the only generation path, and the CELL-UNION saving is ~0

Two findings, one a defect and one a measurement that reframes the whole scheme.

**The defect.** `MTG_KEEP_TRACE=1` recorded nothing at all on the continuous generator — the only
generation path since `keepgen-no-off-switches.md` deleted the uniform one. A traced burn run produced
**18,853 entries with zero non-empty touched sets** while still stamping `meta.traced=true`. Two causes,
both the rewrite leaving the feature behind (the same way it retired the size-7 statistical
classification): the continuous worker never called `SetTouchIndex`, and its size-7 rollouts go through
`run_one`, which never passed the hit vector (only `run_batch`, on the superseded wave path, did).

The failure direction is what makes it serious: **an empty touched-set is DISJOINT from every changed-card
set**, so the consume path would have marked every cell provably-untouched and reused the ENTIRE prior —
shipping a stale profile while reporting a successfully scoped re-run. Nothing shipped was corrupted (no
committed raw carries `traced`), so it was latent. Fixed at both sites, plus a consume-side guard: a prior
claiming `traced=true` with no non-empty touched set anywhere is demoted to untraced rather than trusted.
Re-validated behaviour-neutral — trace on vs off, same seed, **18,853/18,853 cells identical** on
`sum`/`sumsq`/`count`.

**The measurement.** With recording restored, per-card touch rates on burn (hand-mass weighted, R=2 traced
gen so each recorded set is 4 games; `p` inverted off that):

| card | copies | p(one game) | per-GAME carry saves | CELL-UNION carry @R=40 |
|---|---|---|---|---|
| Mountain | 24 | 100% | 0% | 0.00% |
| Goblin Guide / Monastery Swiftspear | 4 | 49.0% | 51% | **0.00%** |
| Lightning Bolt | 4 | 47.5% | 52% | **0.00%** |
| Shard Volley | 4 | 42.6% | 57% | **0.00%** |
| Skullcrack | 4 | 42.0% | 58% | **0.00%** |
| Eidolon of the Great Revel | 4 | 38.9% | 61% | **0.00%** |
| Light Up the Stage | 4 | 35.0% | 65% | **0.00%** |
| Searing Blaze | 4 | 14.8% | 85% | 0.16% |
| Searing Blood | 4 | 1.2% | **99%** | 62% |

A cell is reusable only if NONE of its R rollouts touched the card, so the union saves `(1-p)^R` while
per-rollout reuse saves `(1-p)`. At R=22 the union needs `p < 3%` before even half the cells survive.
**The shipped cell-granularity carry therefore saves 0.00% for eight of ten burn cards** — only Searing
Blood, near-inert in a goldfish, survives it. This contradicts the "on a singleton combo deck most cells'
rollouts never involve that piece" claim below: that holds only for cards at single-digit `p`, and the
draw arithmetic does not put a 1-of there (a 1-of is seen in ~25% of games over a 15-card look).

**So the open lever is PER-GAME masks** (user proposal, 2026-08-12): store the played-card bitfield per
rollout rather than a union per cell, and reuse individual rollouts. A deck's ~60 distinct cards fit one
64-bit word, and win turns are small integers, so the marginal storage is ~9 bytes/rollout. Two design
constraints found while scoping it:

- **"Played" is the right mask for a COMMIT change; "encountered" is required for a DECK change.** A game
  that drew but never cast the changed card executed none of the changed code (safe to skip), but under a
  card swap it held a different card and can diverge. The sink records realized plays only, so it is sound
  for the former and unsound for the latter.
- **Clairvoyance is what makes the sound version affordable.** The search shares the game's shuffle
  (`state.shuffle_salt`; `shuffle_salt_search` is only the opt-in decouple instrument), so every branch
  reads the SAME library order and the union over branches is a *prefix*, not the whole library. A search
  that re-shuffled per branch would make every game sensitive to every card. Mid-game shuffles (Ponder,
  fetches) do break the prefix, and an unrestricted tutor's candidate walk reads every library name —
  though a hard-narrowed candidate list (Hinata's Gamble → {Hinata}) provably cannot pick an excluded card,
  so only *ranked truncation* (top-`TutorSearchWidth` by `SituationalCardRank`) forces marking.
- **Verify the skips permanently.** Re-run 1–2% of skipped rollouts and assert identity. In a paired
  comparison a wrongly-skipped game contributes ZERO difference, so a missed touch channel shrinks every
  measured effect toward null — the worst possible direction for a screening tool.

## Phase A — what shipped (2026-07-07)

- **Recording** (`MTG_KEEP_TRACE=1`, off by default → byte-identical): a per-rollout thread-local sink
  (`src/core/RolloutTouch.h`, defined in `EffectHandler.cpp`) records every card whose effect ran, hooked
  at `EffectHandler::Resolve` + `EnterBattlefield` (spells/ETB/permanents) and `AIEngine::TryPlaySpecificLand`
  (the rollout's land path — lands bypass the stack). The analyzer collects a per-cell UNION over its
  rollouts (`SizeTable::touched`) and serializes it as card NAMES in the raw sidecar (`"touched":[...]`,
  `meta.traced=true`). Records only REALIZED plays (the executor), not the per-turn search's hypotheticals.
- **Consume** (`MTG_KEEP_CHANGED_CARDS="a,b"` + `MTG_KEEP_PRIOR_RAW=<traced pool>`): a cell whose prior
  touched-set is DISJOINT from the changed cards is marked `resolved` → reuses the prior value EXACTLY
  (0 refine), reusing the change-detection machinery. Composes with statistical detection for cells that
  DID touch a changed card (those fall through to the margin test). Needs a traced prior; else it warns
  and falls back to statistical only.
- **Validated** (tiny deck): recording is behavior-neutral (trace on/off → byte-identical sum/count/sumsq);
  captures land + creature + burn spell; a not-in-deck changed card → all cells provably untouched → the
  re-run reproduces the PRIOR profile exactly (0 differ); an all-touching changed card (a land) → 0 reused,
  statistical fallback. The 3-card deck can't show partial saving (every rollout uses all 3 cards) — that
  needs a real singleton deck.

**Remaining for real use — the fidelity gate (below): after a real card change, re-sample a sample of
"untouched" cells and confirm they match the prior.** This certifies the hook set is complete for that
change (catches a too-narrow "touch" or a shared-state leak). The recording captures the common paths
(cast/ETB/permanent/land); rare channels (graveyard/hand statics, cast-but-countered, fetched basics via
`PerformFetch`) are the known candidates a gate failure would point to → widen "touch" or reclassify as an
engine change. Phase B (auto card-def-hash attribution + engine guard) and Phase C (mechanic-level) remain.

## The wall it breaks

Statistical detection can't cheaply certify a near-threshold cell didn't cross a threshold — that costs
about as many samples as refining it. But if we *know* a cell's rollouts execute **byte-identical code**
under the old and new commit (because they never ran the code the commit changed), the cell **definitely**
didn't move — reuse the prior value with **zero** fresh samples, near-threshold and bottoming cells
included. A play-logic fix is almost always scoped to a **specific card or mechanic**; on a singleton combo
deck like Hinata most cells' rollouts never involve that piece, so most of the table is reusable for free.

## Core mechanism

1. **Record a touched-set per cell** during generation. Across a cell's R rollouts, OR together a bitmask
   of the **real cards** the rollouts actually exercised (cast / activated / triggered / static applied /
   in a zone a static scanned — define "touch" BROADLY; over-inclusive is safe). Store the mask per cell
   in the raw sidecar next to `sum/sumsq/count` (a deck has ~60 distinct cards → one 64/128-bit word;
   ~10–20% larger sidecar). Cost to record: a bitmask OR per card-event — negligible beside the rollout.
2. **Compute the changed-card set** for old→new commit (see "attribution" below).
3. **Re-run:** for each cell, `touched ∩ changed == ∅` → **reuse the prior value, 0 fresh rollouts**;
   otherwise re-sample (fresh full, or hand to statistical change-detection / normal adaptive refine).

Why it's exact: a rollout is deterministic given seed + play logic. If none of the changed code runs, every
branch and outcome is identical → identical win-turn. Valid **iff** the change is localized to the changed
cards' own handlers (no shared-state leak — that's an engine change, below).

## Attribution: what changed? (layered, cheapest gate first)

- **`play_digest` unchanged** → this deck's play didn't move at all → reuse the **entire** prior (0 fresh).
  This is already the same-commit pooling case; execution-trace subsumes it as the trivial layer.
- **`play_digest` changed, only specific cards' DEFINITIONS changed, engine unchanged** → changed-set =
  those cards. Detect automatically by a **card-definition hash diff** (hash each card's JSON; reuse the
  def-hash primitive proposed in `structural-bucket-merge.md`) between the prior's cards and the current
  `cards.json`. Reuse cells whose touched-mask is disjoint from the changed cards.
- **`play_digest` changed but NO card definition changed** → it's an **engine change** (shared code) → the
  card-level trace is NOT valid (a shared helper can move any cell) → **refuse** card-level reuse; fall
  back to statistical change-detection or a full run. This guard is the safety catch: never do card-level
  reuse when the change isn't card-attributable.
- **Declared override** (`MTG_KEEP_CHANGED_CARDS="Invigorate,Searing Blood"`) for the human-in-the-loop
  case — trusts the operator's scope. Cheapest to build; must be validated (below) because an
  under-declaration silently keeps stale cells.

## Fidelity gate (the acceptance test — do this before trusting a re-run)

After a real card-scoped change, take a random sample of cells the trace calls **untouched**, fully
re-sample them on the new commit, and confirm the fresh values **match the prior** (within noise). If any
untouched cell moved, the "touch" definition is too narrow or the change wasn't as localized as claimed →
widen "touch" or reclassify as an engine change. This gate is cheap (a sample) and catches the only real
failure mode.

## Interface sketch

- Generation records `touched` per cell (behind a flag, e.g. `MTG_KEEP_TRACE=1`; off = today's byte-identical
  behaviour) and writes it into the raw sidecar.
- Re-run: `MTG_KEEP_PRIOR_RAW=<pool>` (reuses the change-detection loader) + attribution:
  `MTG_KEEP_CHANGED_CARDS=<list>` (declared) or auto (def-hash diff vs the prior's stored per-card hashes).
  Cells with `touched ∩ changed == ∅` are marked `resolved` and reuse the prior value — the SAME
  `resolved`/`apply_prior_override` machinery change-detection already added; execution-trace just supplies
  a *provably-exact* resolved set instead of a statistical one, and covers near-threshold + bottoming cells.
- The two compose: use the trace to resolve the provably-untouched cells (free, exact), then run
  statistical change-detection on the remainder (cells that DID touch the changed cards but may still not
  have moved much). Trace first, statistics for the rest.

## Cost / benefit

- Record: negligible. Store: +one word/cell. Attribution: a JSON diff.
- Benefit: a re-run after a one-card fix re-samples only the cells whose rollouts touch that card — a small
  fraction on a singleton combo deck — **including their near-threshold and bottoming sub-cells**, which is
  exactly what change-detection and confident-cell carry cannot do. Plausibly week → hours for a localized
  Hinata fix. Degrades safely to statistical/full when the change is an engine change.

## Open questions / risks

- **Defining "touch" broadly enough** to capture every channel a card influences a rollout (the fidelity
  gate is the backstop). Start over-inclusive.
- **Engine-change detection** must be reliable — the `play_digest`-changed-but-no-card-def-changed guard is
  the primary signal; consider also an engine-source fingerprint stored in the sidecar.
- **Shared-state leaks** from a card's C++ handler that also edits shared code → must classify as engine
  change (conservative: if unsure, don't card-attribute).
- **Determinism** of the touched-mask across machines (it is, if derived from the deterministic rollouts).

## Phasing

- **Phase A — DONE (d461b02).** Per-cell touched-mask in the sidecar (`MTG_KEEP_TRACE=1`) + declared
  `MTG_KEEP_CHANGED_CARDS` reuse + the fidelity gate. Proves the exact-reuse machinery on real cells.
- **Phase B — DONE (2026-07-07).** Automatic attribution via card-definition-hash diff + the engine-change
  guard. Removes the manual declaration for the common case (card-data fixes). See "Phase B — what shipped"
  below; the original plan is kept after it for reference.
- **Phase C — DEPRIORITIZED (user dubious, 2026-07-07).** Mechanic/effect-level trace for *localized engine*
  changes. Big investment (taxonomy + per-site instrumentation + manual mechanic attribution) for the
  less-common, harder-to-attribute case. Revisit only if routine engine work on an expensive deck makes
  the "engine change → full re-run" fallback the bottleneck. Not planned.

## Phase B — what shipped (2026-07-07)

Removes the manual `MTG_KEEP_CHANGED_CARDS` declaration for the common case — the re-run now derives the
reuse scope automatically from provenance stamped in the prior sidecar.

- **Def-hash primitive** (`CardDatabase::DefHash(name)`, `src/cards/CardDatabase.{h,cpp}`): FNV-1a over the
  card's canonical `cards.json` entry with the COSMETIC fields (`name`, `oracle_text`) removed — i.e. every
  field the engine reads (`mana_cost`, `types`, `keywords`, P/T, `parameters`). Hashes the raw JSON
  (parse-then-`dump()` normalizes key order/whitespace) rather than walking `CardParams`, so a newly-added
  parameter can never be silently missed. Computed once at load, deterministic across machines.
- **Engine fingerprint** (`MTG_ENGINE_FP`): a build-time SHA-256 over every C++ source under `src/`
  (generated by `cmake/EngineFingerprint.cmake` into `build/generated/engine_fp.h`, regenerated every build,
  rewritten only on change). The card DATA (`cards.json`) is naturally excluded (not `.h/.cpp`), so "engine
  changed" is separable from "card data changed" — which `MTG_COMMIT` (bumps for ANY commit) cannot do.
  Falls back to `""` if a tree is built without the generator → engine treated as unavailable → refuse (safe).
- **Sidecar provenance** (`ExhaustiveKeep.cpp` 6b raw emit): `meta.engine_fp` + `meta.card_defs`
  {name → def-hash} for every distinct deck card. Always written (cheap; unknown keys ignored by old readers).
- **Auto-attribution + engine guard** (the `MTG_KEEP_PRIOR_RAW` consume, only when `MTG_KEEP_CHANGED_CARDS`
  is unset — manual always wins):
  1. `play_digest` UNCHANGED → byte-identical play → **reuse the ENTIRE prior pool** (0 fresh rollouts).
  2. `play_digest` changed, `engine_fp` MATCHES, some card DEFS differ → changed-set = those cards → feed
     the existing disjointness path (card-level trace reuse).
  3. `play_digest` changed, `engine_fp` DIFFERS/unavailable → a shared-code change can move any cell →
     **REFUSE** card-level reuse → fall back to statistical / full. (This is the safety catch.)
  `play_digest` is now computed once early (the carry needs it) and reused at the writes.
- **Validated** (`logs/prune_val/phaseb.sh`, tiny deck, 4/4): (a) no change → reuse-all → the re-run profile
  reproduces the prior exactly (0 keep-flag diffs, 64/64 cells reused); (b) one card def changed → auto-names
  exactly that card, reuse count == the prior's own touched-set prediction; (c) engine_fp bumped → refuse,
  0 trace reuse; (d) an inert card (Remand, `goldfish_inert` → never touched) changed → all cells reused via
  the CARD path (non-zero reuse end-to-end). Backward-compat: an OLD prior with no `card_defs` does NOT
  trigger auto-attribution → identical to pre-Phase-B statistical change-detection. Partial (some-cells)
  reuse is deck-limited on the tiny 3-card deck (every rollout touches every card) — real singleton combo
  decks show it; the disjointness arithmetic itself is Phase A, validated there.

**Known limitation / backstop:** if an engine change AND a card-data change land in the SAME commit,
`defs_changed` is non-empty so case 2 fires and could wrongly reuse cells the engine change moved. The
independent `engine_fp` closes this (it would differ → case 3 refuse) — so the guard catches it as long as
the engine actually recompiled. The **fidelity gate** (re-sample a sample of "untouched" cells, confirm they
match the prior) remains the empirical acceptance test before trusting any card-attributed re-run.

### Phase B build plan (concrete)  *(original plan, kept for reference — implemented above)*

1. **Def-hash primitive** (shared with `structural-bucket-merge.md`): a function hashing a card's
   behaviorally-relevant `CardDefinition` — `tmpl`, `params`, cost, types, P/T — EXCLUDING name/oracle-text.
   Prefer hashing the raw `cards.json` entry (minus cosmetic fields) over a hand-written field walk (so a
   new param can't be silently missed). Expose per-card at load from `CardDatabase`.
2. **Store in the traced sidecar** (`MTG_KEEP_TRACE=1` path in `ExhaustiveKeep.cpp` raw emit): `meta.card_defs`
   = { card name -> def-hash } for every distinct deck card, plus an **engine fingerprint**
   (`meta.engine_fp`) — a build-time hash of the non-cards.json engine sources (inject via CMake, or a
   generated header) so "engine changed" is separable from "card data changed". `MTG_COMMIT` alone is NOT
   enough (it bumps for any commit incl. docs/card-data).
3. **Auto-attribution at re-run** (extend the `MTG_KEEP_PRIOR_RAW` consume): diff current per-card def-hashes
   vs `meta.card_defs` → the changed-card set (feeds the existing `MTG_KEEP_CHANGED_CARDS` disjointness path,
   so most of the consume is already built). `MTG_KEEP_CHANGED_CARDS` stays as a manual override.
4. **Engine guard** (the safety gate): do card-level trace reuse ONLY if `engine_fp` is unchanged. If
   `engine_fp` changed → refuse trace reuse, fall back to statistical change-detection / full (an engine
   change can move any cell). Also: if `play_digest` unchanged → reuse everything (trivial top layer,
   already the pooling case); if `play_digest` changed but 0 card defs changed AND engine_fp unchanged →
   contradiction (shouldn't happen) → conservative refuse.
5. **Validate** on the tiny deck: (a) no change (identical hashes) → reuse all → reproduce prior; (b) flip
   one card's def-hash synthetically → only that card in the changed set → cells touching it re-sampled,
   others reused; (c) flip engine_fp → refuse, full statistical. Reuse `logs/prune_val` harness.

## Not for small deck changes

A decklist change alters the library composition for *every* cell (a removed card is no longer drawable),
so old-deck touched-masks don't transfer cleanly. Small deck changes are better served by bucket-membership
translation + the statistical/"cells not touching the swapped card" hybrid (see
`docs/design/change-detection-carry.md` and the modified-list note). Execution-trace is specifically the
**same-list / new-commit** (play-logic) lever.
