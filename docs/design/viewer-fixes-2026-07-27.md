# Play-viewer fixes & improvements — 2026-07-27 batch

Plan of record for a 12-item batch of play-viewer (`tools/play/`) bugs and improvements the
user reported after a round of hand-play across Anti-Lifegain / Dragonstorm / Auras. Standalone
(this doc is the shared state; no agent-private notes). Status column tracked inline — update as
items land.

## Decisions taken (user, 2026-07-27)

- **Sequencing:** the agent picks the order for a batch dump (standing rule).
- **Reject/rollback integrity (#1,#2,#3):** fix via **engine reports resolution** — the apply
  path emits exactly which casts resolved and what each did, so the GUI renders *server-truth*
  instead of client-side board-diffing. This is the README's stated "Next: higher-fidelity
  resolved effects." Removes the whole client-guessing bug class and unlocks faithful
  splice/firebreathe/discard display. (The engine-side **accept payability gate** is needed
  regardless of this choice.)
- **Discard (#5):** **quick heuristic fix now** (stop pitching your only payoff), **searched
  discard as a separate regression-validated, perf-measured follow-up** (heuristic-optimization
  skill; discard search in rollouts is costly).
- **Splice (#7):** default = auto-splice-if-affordable in the committed order; add an
  off-by-default prompt toggle. **Firebreathe/Dwarven Hold (#4/#6):** surface a small +/– amount
  modal, default = current greedy max, surface-on.

## The two orthogonal validator defects (do not conflate)

The reject cluster is TWO independent bugs that happen to co-occur:

1. **Stage-1 Accept has no payability gate.** `TurnSolver::CheckLine` computes payability
   (`plan_pays` trial-applies the plan on a copy under `RevealLogPause`, `TurnSolver.cpp:10553-10570`)
   but uses it **only to rank** candidates; Accept fires on match-count alone
   (`TurnSolver.cpp:10590-10595`). An over-generated plan (optimistic `BuildPool` / wild-mana /
   Lotus-sac / tap-order) that matches the cast-name multiset but isn't fully payable is
   **accepted** → `applyAccepted` appends it → `/api/step` → `ApplyPlanDirect` casts the affordable
   subset (e.g. Reverent Silence's free alt-cost resolves, **destroying the player's own Tainted
   Remedy**) and silently drops the unpayable remainder. *Fix:* gate Accept on `plan_pays` — if the
   single matched variant isn't fully payable, fall through to Stage-2's honest sim.
2. **Stage-2 affordability color model is too strict.** `ComputeAvailableColors` +
   the restricted-color gate (`TurnSolver.cpp:10696-10733`) credit only **rocks'** colors
   (`10702: if (!pc.rock || !pc.def) continue;`); flexible **any-color** sources (Lotus Bloom,
   `{T},Sac: add three of any one color`) and multi-color **dorks** (Ignoble Hierarch
   `{T}: B/R/G`) are not credited → false `Illegal` "no source of that color" (item #9, item #1
   dorks). *Fix:* credit flexible/any-color producers in the color model.

Defect (1) errs toward **false-accept** (→ partial state); defect (2) errs toward
**false-reject** (→ spurious Illegal). Both are real; fixing one does not fix the other.

## Item map

| # | Symptom | Root cause (anchor) | Fix | Status |
|---|---|---|---|---|
| 1a | payable line rejected, yet Reverent Silence resolves (destroys own Tainted Remedy) | Stage-1 Accept no payability gate `TurnSolver.cpp:10590` | gate Accept on `plan_pays` | TODO |
| 1b/3b | after reject, turn burned / "Commit turn" plays nothing | `rollbackLine`/`notifyDropped` never clear `S.commitTurn`/set `S.undoing` (`index.html:2306-2310,2253-2263`); auto-pass `2161-2166` | clear `commitTurn`+set `undoing` in rollback path | TODO |
| 9 | Lotus Bloom line rejected "no black source" | Stage-2 color model credits only rocks `TurnSolver.cpp:10702` | credit flexible/any-color sources in `ComputeAvailableColors` | TODO |
| 3a | working line still shows "not enough mana" | client `detectDropped` heuristic false-positive (`index.html:2235-2252`) | server-truth resolution (below) | TODO |
| 2 | undo corrupts history (out-of-order/dup turns) | checkpoint↔step desync from auto-advanced decisions (`advanceTo` push `2144`, `rollbackStep` `2279`) | server-truth resolution + reconcile bookkeeping | TODO |
| 10 | committed cast order ignored (re-sorted) | `CastRankOf` canonical re-sort unless `searched_order` (`TurnSolver.cpp:5102-5121`); CheckLine only honors enumerated perms (`10530-10536`) | build candidate from exact committed order, prefer it | TODO |
| 8 | splice display out-of-order + drops Rite of Flame | variant labeler alpha-sorts sub-tokens + label from tokens only (`TurnSolver.cpp:10499,10523-10526`) | rewrite labeler to ordered cast list (or subsume in server-truth) | TODO |
| 7 | no "just splice if affordable" default / no toggle | splice = search-chosen k, no chooser (`TurnSolver.cpp:1848-1892`) | greedy-splice default + off-by-default prompt | TODO |
| 5 | discard pitches Apex of Power (only payoff) | `SelectCleanupDiscardIndex` = highest-MV non-required; `required_pieces:[]` (`SpellEffects.h:84-137`) | protect payoffs now; searched discard later | TODO |
| 4 | firebreathing mana invisible; no player control | `ApplyFirebreathing` greedy, reads pool untapped (`SpellEffects.h:1706-1786`) | new "how much" decision (4 sites); faithful display | TODO |
| 6 | Dwarven Hold burst amount auto-decided | storage-land, same mechanic as Mercadian Bazaar | sibling amount decision to #4 | TODO |
| 11 | unattached queued aura can't be dragged onto a creature | untargeted queued aura renders non-draggable `plannedThumb` (`index.html:600-609`) | make thumb draggable (reuse `retargetPlanAura`) | TODO |
| 12 | can't activate abilities same-turn before combat (Horizon Canopy sac) | only 2 main breakpoints; combat auto-runs; freshly-played land still "in hand" at decision | engine re-prompts same phase when new activatable abilities appear; rename Commit phase→Commit Line; exempt Commit turn | TODO |

## Server-truth resolution (the #2/#3 fix, chosen architecture)

Today the GUI infers "what happened" by diffing board snapshots (`detectDropped`,
`index.html:2235-2252`) and accumulates a client-side history log; both are fragile
(false "not enough mana", undo desync). Direction: the `--claude-play` apply path emits a
structured **resolution log** for the committed step (which casts resolved, in order; splice
counts; firebreathe spend; discard victim; damage/life deltas; targets) — reusing the
`tools/replay/` action-log idioms — and the GUI renders that instead of guessing. The client
stops deriving reject state from board diffs (kills #3a) and rebuilds history from the
server-emitted step log keyed to `choices`/`steps` (kills #2). Also the natural home for
faithful #8 splice display and #4 firebreathe-spend visibility.

## Verify after each change

Per `tools/play/README.md`: build with `./build.sh` (never raw cmake), then run BOTH viewer
regression checks after any engine / decision-JSON / `index.html`/`linebuild.js` edit:

```
python3 test/viewer_protocol_check.py     # engine↔protocol contract
node    test/viewer_linebuild_check.js     # browser line-building layer
```

A THIRD check now guards the previously-untested `--validate-line` / `CheckLine` path (the other
two exercise plan-index replay and linebuild, never CheckLine):

```
node test/viewer_validate_check.js                    # baseline-gated; exit 1 on a NEW regression
node test/viewer_validate_check.js --update-baseline  # rebaseline after an intended CheckLine change
```

It replays every clean reference's played line through `--validate-line` and asserts it still
validates as accept/choose (never a newly-`illegal`/`legal_not_enumerated` line). `test/viewer_
validate_baseline.txt` records 114 known v1 limitations (X/tutor/alt-cost lines it declines, plus a
few harness prefixes that land on a sub-decision). This check CAUGHT the #1a regression below.

## Progress log

- **2026-07-27 (batch 1, regression-free):**
  - **#1b/#3b DONE** — `rollbackLine` now clears `S.commitTurn`/`S.commitTurnPasses` and sets
    `S.undoing` (mirrors the manual-reject branch + `undo()`), so a rejected line no longer burns
    the turn via the commit-turn auto-pass.
  - **#9 DONE** — Stage-2 affordability sim now credits in-play untapped `sac_for_mana` sources
    (Lotus Bloom → 3 wild + all colours in the gate). s21 flips `illegal`→`legal_not_enumerated`
    (honest: board CAN pay, search just didn't enumerate). Guarded by `sac_wild>0` → byte-identical
    without a sac source. The multi-color dork half of #9 was already correct (in-play `ManaDork`
    that `CanTap()` is credited; a summoning-sick this-turn dork correctly is not).
  - **#1a REVERTED / re-scoped** — the payability accept-gate is UNSOUND via `plan_pays`: it
    trial-applies under `RevealLogPause` (choosers nulled), which cannot reproduce a
    chooser/order-dependent combo (Apex of Power add-ten-mana + exile-cast, storm, dragon-put), so
    it false-negatives real combo lines. The validate check caught it rejecting 9 payable
    Dragonstorm Apex turns. The proper fix needs an ACCURATE resolution oracle (did the committed
    plan fully resolve?) → folded into the server-truth workstream, which trial-applies with
    heuristic (autonomous) sub-decisions instead of nulled choosers.
  - **Known CheckLine gaps the new check surfaced (pre-existing, for the mana-fidelity follow-up):**
    Stage-2 does not credit **fetchland-produced colors** (a fetched dual's colours), so lines like
    Anti-Lifegain's `Tainted Remedy + Skyshroud Cutter…` read `illegal: no black source` even though
    the fetched land makes black (`PerformFetch` default pick, `PlayLandByName` at 5832-5840). Same
    family as #9. Some `NO_VALIDATION_BLOCK` entries are the harness prefix landing on a sub-decision.

Reference reproductions to sanity-check specific items: Dragonstorm s21_gi20 (Lotus Bloom
black, item 9 — see `logs/play/rejections/Dragonstorm_cod_s21_gi20_t7.json`), s9_gi8 (splice
display #8), s24_gi23 (cast order #10), s3_gi2 (discard #5); Auras s21_gi20 suboptimal
(Horizon Canopy same-turn sac #12).
