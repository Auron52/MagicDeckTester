# Viewer payment steering (opt-in) — DEFERRED

**Status:** deferred, not built. Requested by the user 2026-09-07 ("It might make sense to have it
in the viewer opt-in") after the EldraziDisplacerFlicker seed-1 reference re-play showed a turn-3
kill that exists *only* under a particular payment assignment.

## The problem, from the incident that found it

EDF seed 1, turn 3. The board is Aether Hub + Conservatory (both enchanted with Wild Growth) +
Mariposa; the hand holds Trace of Abundance and Living Wish. The human wants to cast both.

A land Aura's bonus is **mandatory and rides the host's tap** (`LandAuraAddToPool`), so a
Trace-enchanted Conservatory taps for base + `{G}` + WILD = 3 mana. Paying a 2-cost with it
therefore **floats the wild**, and that floating wild is the only blue source on the board — it is
what casts Cloud of Faeries, which untaps two lands and bootstraps the whole blink loop into a
turn-3 win.

Whether that wild floats depends entirely on **which land pays which spell**:

| assignment | what happens |
|---|---|
| `Trace → Conservatory` then Wish paid by Conservatory | the Trace bonus is consumed paying the Wish; nothing floats; **no turn 3** |
| `Trace → Aether Hub` (paid by Conservatory), Wish then forced onto the Hub | the Hub's Trace bonus floats as wild; **turn 3** |

The engine's payer is **min-waste**: it picks the assignment that spends the least, which is
precisely the one that forecloses the line. The human cannot express the other one. In the re-play
I steered it only by an indirect trick — attaching the Trace to the Hub so that the Hub became the
board's only remaining green source, which *forced* the Wish's payment onto it. That is a puzzle,
not an interface.

## Why it is not just "a better heuristic"

Deliberately overpaying to bank a colour is not globally better — it is better *only* when a later
spell in the same turn needs that colour, which is exactly the lookahead the payer does not have
(and should not grow: `docs/design/mana-source-reservation.md` and the tap-order work both measured
that making the tap decision searched costs far more than it returns — `MTG_UNPRUNE=tapreserve`
measured **3.08x search time for +0.1001 WORSE play**). The human, however, knows their own line.
So this is a *human-play* affordance, not an engine policy change — the same shape as every other
verb the viewer has grown (`blink=`, `equip=`, `pod=`).

## Sketch of the design

**Opt-in, because it is noisy.** Payment happens on essentially every cast; surfacing it always
would make the viewer unusable. It belongs in the existing ⚙ Options menu
(`AUTO_RESOLVABLE` / `surfaceEnabled` in `tools/play/index.html`), defaulting to **off** — the
`land_entry` precedent exactly.

Three layers, in increasing cost:

1. **Line verb only (cheapest, no new decision type).** Extend the LineSpec with a
   `pay=<spell>@<land m_number>[,<land>...]` token pinning which sources fund a named cast, honoured
   by `CheckLine`'s `plan_pays` trial and by `ApplyPlanDirect`. The viewer offers it as a
   drag/click on the land row while a cast chip is selected. No engine decision is emitted, nothing
   fires in rollouts, and the autonomous search is untouched by construction.
2. **A real `payment` decision type** emitted at cast time under `HumanPlayActive()`, listing the
   payable source subsets (already computable — `SubsetPayableWithFilters` enumerates them). More
   faithful, but the subsets can be large and it needs the usual dedup/labelling work so two
   assignments that differ only in an irrelevant tap do not both appear.
3. **Float-preserving assignments in the payer itself** (rejected for now): teach
   `TapForCostBacktrack` to prefer an assignment that banks a colour a queued-but-uncast spell
   needs. This is the searched-tap-decision that has already been measured and rejected twice; do
   not re-derive it (see `mana-source-reservation.md`, and the
   `read-the-flag-doc-before-changing-it` lesson).

**Start with (1).** It answers the user's request, is human-play-only, and cannot move GT.

## Traps to respect when building it

* **Plan indices are not stable across binaries.** Any new plan variant SHIFTS the human menu, and
  saved references replay by `plan_index` — the same class of break that adding the FINISH plan had
  to be gated for (`MTG_PLAY_FINISH_PLAN`), and that the 2026-09-07 COMBO OFF gate hit for real
  (it invalidated a reference saved the same day). Ship behind an env lever and re-run
  `test/viewer_protocol_check.py` before committing.
* **The mana cache keys on what the DFS reads.** A pin that changes which sources are eligible must
  be folded into `ManaCacheKey`, or a stale entry will answer for the unpinned board — the exact
  defect fixed on 2026-09-07 for land auras (`mana-cache-stale-hit-diagnosis`).
* **Scope to `HumanPlayActive()`.** The 2026-09-07 painland tap-ahead is the cautionary tale: the
  same change scoped rollout-wide broke a scenario (`edf_blink_loop_cashes_gorge`, win T4 → T6),
  and unscoped it broke references via ENUM-GAP.
