# Execution-trace carry (reuse the prior for cells the change didn't touch)

**Status: DESIGN (2026-07-07). The lever that cuts the NEAR-THRESHOLD + BOTTOMING re-run cost — the part
statistical change-detection cannot (`docs/design/change-detection-carry.md` finding).** Same-list /
new-commit only (play-logic changes); small deck changes are a different animal (see end).

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

- **Phase A** — per-cell touched-mask in the sidecar (`MTG_KEEP_TRACE=1`) + declared `MTG_KEEP_CHANGED_CARDS`
  reuse + the fidelity gate. Smallest, proves the exact-reuse machinery on real cells.
- **Phase B** — automatic attribution via card-definition-hash diff + the engine-change guard.
- **Phase C** — mechanic/effect-level trace (a change localized to one rule, e.g. trample, reuses cells
  whose rollouts never invoked it) for localized ENGINE changes. Bigger.

## Not for small deck changes

A decklist change alters the library composition for *every* cell (a removed card is no longer drawable),
so old-deck touched-masks don't transfer cleanly. Small deck changes are better served by bucket-membership
translation + the statistical/"cells not touching the swapped card" hybrid (see
`docs/design/change-detection-carry.md` and the modified-list note). Execution-trace is specifically the
**same-list / new-commit** (play-logic) lever.
