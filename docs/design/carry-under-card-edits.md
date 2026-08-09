# Incremental mulligan-gen carry under card edits (forward design)

**Status:** future design, nothing built here. This is the dedicated home for *how change-detection
carry should evolve* once we are iterating on decklists (the phase-1-3 environment: frequent small card
edits, and rollouts that draw specific cards). It builds on `docs/design/change-detection-carry.md`
(the SHIPPED Phase-1 statistical mechanism) and is referenced from
`docs/design/continuous-only-keepgen.md` (which only needs to know that carry stays and is flagless).

## Why this needs its own design

A full exhaustive mulligan gen is multi-day. When we start swapping cards to compare decklists, we do
NOT want a full regen per edit. Carry reuses a prior run's rollouts and re-does only what the edit
actually changed. The shipped statistical detector helps but has a known ceiling (below); phase-1-3
stresses it in ways that may justify a finer, more expensive instrument — but only if measurement says
so. This doc frames the problem so that decision is made on evidence, not intuition.

## The core difficulty: an edit perturbs a cell three different ways

A "cell" is an opening-hand composition over the K equivalence buckets; its value is the mean win-turn
over rollouts from that hand. A card swap can move a cell's value — or move the set of cells — through
three distinct channels, which need different handling:

### (A) Composition / decision-structure change — cheap, exact, no rollout inspection
The swap changes which opening hands exist, or flips a hand's keep decision by its *own contents*
(classic: adding a land turns a former 1-land auto-mull into a keepable hand). The affected cells are a
deterministic function of the deck diff + bucketing, so **regenerate exactly those cells from scratch**.
Identifiable up front, always sound.

### (B) Draw-quality change — the hard case
The *same* opening hand (same cell) is now better or worse because card X vs Y sits in the library and
gets **drawn mid-game**. The cell's composition is unchanged, so (A)'s structural diff is blind to it —
the value moved purely through draws. This is what statistical detection or the game-level trace
instrument must handle, and what has to be A/B'd.

### (C) K / cell-space change — decides how (A) is realized, and its direction sets the cost
An edit can change the equivalence-bucket structure, so the cell *space* itself is not fixed and
`bucket_fp` shifts. Carry must therefore **remap** prior→new buckets rather than assume matching cell
indices. Direction matters:
- **Merge / removal (K↓)** — e.g. drop a singleton, add another Mountain. Cells keyed on the vanished
  bucket **collapse**. They aren't regenerated — they fold away, and where two buckets genuinely merge
  into one equivalent bucket their prior rollouts **aggregate** into the merged cell (reuse, not rerun).
  The cheap direction.
- **Split / addition (K↑)** — e.g. drop a Mountain, add a singleton. Genuinely **new** cells appear with
  no prior → must be generated fresh. The expensive direction.

**(B) persists regardless of (C).** Any change to deck contents changes the draw distribution, so the
mid-game-draw work still has to be done across the surviving cells; K only decides how the cell space is
remapped underneath, not whether (B)'s work disappears. A single swap is often several channels at once
(spell→land = new keepable hands (A) + shifted draws (B) + a possible K move (C)); the mechanisms
compose — remap the space (C), regenerate the structurally-changed cells (A), then handle the residual
draw effect on the survivors (B).

## Remap / aggregate mechanics (sketch, for the (C) merge case)

When two prior buckets b1,b2 merge into one new bucket b* (they tested equivalent after the edit), a new
cell that is `{… , n×b*}` draws its members from the union of b1,b2. Its prior estimate can be
reconstructed by **pooling** the prior rollouts of the cells that map onto it under the merge, weighted
by each source cell's rollout count — no new rollouts, provided the merge is a true equivalence (which is
exactly the bucketing predicate's job). This is only sound for (C)-merge; it does NOT absolve (B) — if
the *same* edit also changed draw quality, the pooled estimate is still stale on the (B) channel and
must be topped up. So the safe order is: (C)-remap (aggregate merges / mint splits) → mark every
surviving cell for the (B) top-up → let (A)'s from-scratch cells and (B)'s rerolls run.

## (B): what's built, why it struggles, and the finer instrument

- **Built — statistical detection** (`change-detection-carry.md`). A thin floor batch re-samples every
  cell and reuses the prior only where the shift can't flip the decision (margin = distance-to-threshold).
  Honest ceiling: it **cannot cheaply clear near-threshold or bottoming cells** (certifying "didn't move"
  there costs ~as much as refining). Measured ~21% refine saving on a tiny deck.
- **Why phase-1-3 stresses it.** Because rollouts draw specific cards, a change to a *commonly-drawn*
  card perturbs at least one rollout in almost every cell → statistical carry clears little → it may not
  earn its keep exactly when we lean on it most.
- **Execution-trace detection — the finer instrument.** Know which rollouts hit the changed card/code and
  reuse the rest with ZERO fresh samples.
  - *Cell-level (partially built):* `MTG_KEEP_CHANGED_CARDS` + a *traced* prior whose cells carry a
    "touched" set → a cell no rollout touched reuses its prior exactly, even near-threshold. But a cell
    with *any* touching rollout is fully redone.
  - *Game-level (the harder, ideal build):* within a cell, **replay only the specific games that draw the
    changed card, and keep every untouched rollout.** The rollout seed for game *r* of cell *w* is a pure
    function of seed/r/w/pd, so those games are identifiable and reproducible — *if* the prior run
    recorded per-rollout touch sets (extra storage + a traced prior). This is where the big saving lives
    when edits touch common cards, and it is the natural answer to the (B) channel.

## Plan (measure before building)

1. Land the cheap, always-sound pieces first: (A) from-scratch regen of structurally-changed cells, and
   (C) remap with merge-aggregation / split-mint. These need no rollout tracing and are correct by
   construction.
2. For (B): once phase-1-3 is live, **measure** the built statistical carry's real clear-rate on actual
   card edits (A/B: statistical carry vs full regen — saving vs fidelity). Only if it fails to earn its
   keep, build the **game-level execution-trace** instrument (per-rollout touch sets in the prior +
   selective game replay). Do not build it on intuition.
3. Keep it flagless throughout (per `continuous-only-keepgen.md`): the analyzer auto-detects the prior
   sidecar and auto-diffs `cards.json`; no `MTG_KEEP_*` carry knobs survive.

## Cross-refs
- `docs/design/change-detection-carry.md` — the shipped statistical + partial cell-level-trace mechanism.
- `docs/design/continuous-only-keepgen.md` — carry stays and is flagless; the forward design is *here*.
