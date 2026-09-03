# Color-aware subset mana gate (deferred)

**Status: deferred improvement (2026-08-14).** Surfaced by the KittyEquipment Stage 5d
claude-play sweep (ledger `analysis-KittyEquipment.md`, gi=6 confirmed flag + gi=10/15
uncertain repeats), but the mechanism is engine-wide and pre-existing.

## The behavior

The plan enumerator's subset-payability check is a **flat total-mana pool bound**
(`ManaPruneBound` / the `ManaGateTerm` machinery in `src/ai/TurnSolver.cpp`): it credits a
same-turn mana rock's output into the pool TOTAL and requires the combined total to cover
the subset's total cost. Per-COLOR feasibility of an *interacting* subset is deliberately
not checked there (documented in the ManaGateTerm comment block; the legacy bound's own
unsoundness corner is why `MTG_SEL_MANA_GATE` exists, default-off).

Concrete case (KittyEquipment T1, one Plains in play): plan "cast: Kor Duelist, Sol Ring"
is offered. No ordering pays it — Plains {W} on Sol Ring leaves only {C}{C} for the {W}
pip; Plains on Duelist leaves nothing for Sol Ring. Totals pass (1+2 credited ≥ 2 needed),
so the flat pool accepts.

## Why it is currently tolerable

- The **exact payment solver at execution rejects** the unpayable tail and drops it with
  the visible `dropped_casts` field — no cards are lost, no illegal state arises.
- Executor and rollout drop **in lockstep** (0 fd-diverge over 50 full-depth games on the
  deck that surfaced it), so the search never banks phantom mana/casts.
- Cost is bounded: a wasted search branch plus a misleading plan summary in the viewer
  (the human picks "cast A, B" and gets A with `dropped_casts: [B]`).

## The deferred fix

Make the subset gate color-aware for the rock-credit path: when crediting a same-turn
rock, split its produced colors into the per-color pool (the way `AddSourceToPool`
already models it for filters) and require per-color `CanPay` of the combined subset cost
rather than totals. Constraints:

- Must stay a **sound bound** (never prune a payable subset): sequencing means a rock's
  colors are only available to spells payable after it; a conservative-but-sound form is
  "totals must pass AND the colored-pip demand must be coverable by (real sources' colors
  + only the WILD portion of credited rocks)". A colorless-only rock (Sol Ring) then
  correctly fails to cover a {W} pip.
- Byte-identity discipline: this CHANGES which plans are offered (drops the doomed
  branches), so it is GT-affecting for any deck where the corner fires — same adoption
  route as `MTG_SEL_MANA_GATE` (flag first, measure, user-approved rebaseline).
- Related pre-existing approximation, out of scope here: multi-color sources are treated
  as wild (the dual-land approximation noted in the Knights deck's bracket notes).
  *(2026-09-03: since fixed and adopted at the subset sites — `MTG_COLOR_EXACT` default ON,
  e4690c37; the rock-credit colour gate that is this doc's own deferred fix is still deferred.)*
