# Sequential plan evaluation — fixing "all actions vs. the frozen start-of-phase state"

**Status:** DEFERRED (agreed 2026-07-23, to start *after* the Auras deck is finished).
**Trigger to pick up:** user request. This doc is the self-contained spec.

## The core defect

In Magic you cast/play one thing at a time and **state updates after each** — the next
spell's cost, castability, legal targets, and any restriction are evaluated against the
board *as it is now*, including everything resolved earlier this turn.

Our main-phase engine breaks that invariant. A **plan** is a set of actions
(`land` + N casts), and the enumerator (`CollectActions` / `EnumeratePlans` in
`src/ai/TurnSolver.cpp`) builds and legality-checks that set against **one snapshot** —
the start-of-phase `GameState`. `ApplyPlan`'s `apply_one` then executes the actions, but
the **enumeration and cost/legality decisions were already frozen** against the snapshot.
The dedup (`plan_signature`, and the `CheckLine` variant signature) keys on cast *names* +
a few per-action sub-tokens, which further erases per-action ordering context.

So any **within-turn state dependency** is mis-modeled:

- **Restriction that turns on after a prior cast.** Daybreak Coronet enchants "a creature
  with **another Aura**." Casting `Ethereal Armor → Bogle` then `Daybreak Coronet → Bogle`
  in one turn is rules-legal (Coronet sees the just-attached Ethereal Armor), but the
  enumerator checks Coronet's target legality against the *start-of-phase* Bogle (no aura),
  so the two-aura line is **never enumerated**. The viewer's line-checker classifies the
  hand-built line as `legal_not_enumerated` (real, reproduced: `logs/play/rejections/Auras_cod_s1_gi0_t3.json`).
- **Target legality / scaling that depends on prior casts.** Same root: `LegalEnchantTargets`
  and scaling counts are computed once, not after each intra-turn cast.
- **Cost reducers that turn on mid-sequence.** Partially mitigated already: `CheckLine`'s
  step-2 affordability sim *does* credit same-turn ramp (a rock cast this turn) via a pool
  independent of `BuildPool` — but that is affordability only, not target-legality or
  restriction re-checks, and it lives in the viewer path, not the search.

These keep surfacing as one-off patches (the enchant-target dedup fix in `plan_signature`
and `CheckLine`; the crackle/soulfire/splice count subs before it). Each patch re-adds a
per-action dimension the frozen-snapshot model dropped. The real fix is to evaluate a plan
by **applying its actions in sequence**, updating state between each, and deriving
cost/castability/target-legality from the running state.

## Proposed direction (agreed)

Two different consumers, two different ordering policies:

### Search (autonomous + the plan MENU)
Don't enumerate every ordering (combinatorial blow-up — the whole reason the snapshot model
exists). Instead, for each candidate action **set**, choose ONE **canonical, logical order**
and apply all rulings (cost, "can this be cast", legal targets, restrictions) by walking that
order and **updating a working `GameState` after each action**. Candidate order heuristics to
evaluate:
- mana sources / cost-reducers first, then **enablers** (auras/permanents that satisfy later
  restrictions), then **payoffs** (Daybreak Coronet, Ancestral Mask scaling, lethal);
- or a **dependency-aware** order: if action B's legality/cost depends on A's resolution,
  order A before B (topological on the intra-turn dependency graph), breaking ties by a
  stable heuristic.
The set is *kept* (enumerated) iff it is executable in that canonical order. This widens the
enumerated space to include genuinely-legal sequenced lines (e.g. Ethereal-Armor-then-Coronet)
without exploring permutations. **This changes autonomous behavior → NOT byte-identical**; it
is a capability expansion, so ground truth for affected decks must be re-measured and
rebaselined, not asserted byte-identical.

### Viewer (human play)
**Respect the user's literal order.** The human queued the casts in a specific sequence; apply
them one at a time in that order, updating state after each, and validate legality
incrementally (accept iff each step is legal against the running state). No canonical-order
guessing — the human already expressed the order. This also removes most `legal_not_enumerated`
rejects: a rules-legal human sequence just plays. `CheckLine` becomes an incremental replay of
the declared line rather than a match against snapshot-enumerated plans. (Plan-index stability
for the `--choices` replay stream needs a story here — likely the accepted human line is
recorded as its own resolved action sequence rather than an index into the snapshot enumeration.)

## Scope / risk
Touches the hot path: `CollectActions` / `EnumeratePlans` (search), `CheckLine` + the viewer
line protocol, and `ApplyPlan`. Performance matters — applying actions sequentially per
candidate set is more work than a snapshot check; measure against the search-perf budget.
Expect GT churn (more lines enumerable). Keep an `MTG_*` legacy gate so the snapshot behavior
stays A/B-comparable while validating, per the repo's byte-identical-hatch convention.

## Relationship to current band-aids
The enchant-target dedup subs (`plan_signature` + `CheckLine`, 2026-07-23), crackle/soulfire/
splice count subs, and the same-turn affordability sim are all partial compensations for the
frozen snapshot. Under sequential evaluation, target/restriction/cost legality would fall out
of the running state and several of these special-cases could collapse into the general
mechanism. MDFC land-face selection (in progress for Auras) is **separable** — it is a single
land-play choice (like shock/Frostboil `land_entry`), not an intra-turn cast dependency — so it
is being finished under the current model and is not blocked on this redesign.
