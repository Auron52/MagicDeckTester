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
| 5 | discard pitches Apex of Power (only payoff) | `SelectCleanupDiscardIndex` = highest-MV non-required; `required_pieces:[]` (`SpellEffects.h:84-137`) | protect payoffs now; searched discard later | **DONE** (GT rebaselined) |
| 4 | firebreathing mana invisible; no player control | `ApplyFirebreathing` greedy, reads pool untapped (`SpellEffects.h:1706-1786`) | new "how much" decision (4 sites); faithful display | TODO |
| 6 | Dwarven Hold burst amount auto-decided | storage-land, same mechanic as Mercadian Bazaar | sibling amount decision to #4 | TODO |
| 11 | unattached queued aura can't be dragged onto a creature | untargeted queued aura renders non-draggable `plannedThumb` (`index.html:600-609`) | make thumb draggable (reuse `retargetPlanAura`) | TODO |
| 12 | can't activate abilities same-turn before combat (Horizon Canopy sac) | only 2 main breakpoints; combat auto-runs; freshly-played land still "in hand" at decision | engine re-prompts same phase when new activatable abilities appear; rename Commit phase→Commit Line; exempt Commit turn | **DONE** |

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
- **2026-07-27 (batch 2, GT-neutral):**
  - **#8 DONE** — CheckLine variant label now bases on the ordered `LineSummaryOfPlan` (every cast,
    in order) + sub-decisions in cast order; the dedup `sig` stays sorted/byte-identical. Fixes the
    alpha-scrambled `splice+0; splice+1` and the dropped plain Rite of Flame. Viewer-only.
  - **#11 DONE** — an untargeted queued aura now renders as a draggable `.aura-plan[data-pi]` thumb
    (reuses `wireBoard` dragstart + `retargetPlanAura`; no bounce). Client-only.

  **GT-BATCHING NOTE:** everything landed so far (#1b, #9, #8, #11) is viewer/CheckLine/client →
  GT-neutral (no regression-suite rebaseline needed). The remaining **#5 (discard required_pieces),
  #7 (splice default), #4/#6 (firebreathe/Dwarven amount)** change AUTONOMOUS play → they shift
  ground truth and must be grouped into ONE regression rebaseline (`regression.sh` + `--accept`).
  Do them together, not piecemeal. #10's viewer half and #12 and server-truth's CheckLine parts are
  GT-neutral; only their autonomous-touching parts (if any) join the GT batch.

  **#5 concrete plan (prepped 2026-07-27):** `required_pieces` loads from the profile's JSON array
  into `m_profile.required_pieces` (`src/ai/MulliganProfileIO.h:283`) and stamps `state.m_required_pieces`,
  which `SelectCleanupDiscardIndex` protects from the highest-MV cleanup discard (both autonomous
  rollout `AIEngine.cpp:3593` and interactive `main.cpp:1102`). Dragonstorm's profile currently has NO
  `required_pieces` key (→ empty → Apex MV 10 always pitched). Quick fix = add
  `"required_pieces": ["Apex of Power", "Dragonstorm"]` to `decks/Dragonstorm/Dragonstorm.profile.json`
  (the two irreplaceable payoffs; Dragons are redundant, so pitching an excess one stays correct).
  GT-affecting → part of the batch rebaseline. The searched-discard follow-up (`MTG_SEARCHED_DISCARD`
  path, `AIEngine.cpp:3608`) is the general solution deferred per the user's "search later".

- **2026-07-27 (batch 3, server-truth increment 1 — GT-neutral):**
  - **#3a DONE + #1a resolved via server-truth.** The engine now emits `dropped_casts` — the declared
    casts of the just-committed plan the executor could not pay (recorded at the `apply_one`
    `!TapForCostDirect` drop site, `TurnSolver.cpp:4137`, top-level only via `sink_stack.empty()`).
    Carried by a new `g_play_dropped_cast_sink` (`GameLogger.h`, nulled in `RevealLogPause` → autonomous
    byte-identical, verified 0 play-drift), installed/cleared/read in `main.cpp` `RunClaudePlay` +
    `WriteDecisionJson` + the result emitter. The browser reads `decision.dropped_casts` in `step()`
    and retires the false-positiving `detectDropped` board-diff (removed, with its pre-commit snapshot
    bookkeeping). Verified: `dropped_casts` fires exactly on the over-generated unpayable plans
    (Rite of Flame / Apex of Power at s14_gi0 T5); working lines emit nothing → no false "not enough
    mana" (#3a). The #1a partial-state (Reverent Silence resolving on a rolled-back line) is now handled
    correctly by accept → authoritative drop report → reliable rollback (with #1b, no turn burn) — NOT
    the unsound `plan_pays` gate.
  - **STILL OPEN from the reject cluster:** **#2 undo history corruption** is a SEPARATE client
    checkpoint↔step desync (auto-advanced dead-opp/commit-turn passes each push a checkpoint AND a
    step), not fixed by server-truth; needs the bookkeeping reconcile. The FULLER server-truth vision
    (render faithful per-action resolution — splice/firebreathe/discard/targets from an emitted action
    log) is a later increment; increment 1 covers only the drop signal (the #3a/#1a fix).

  - **Known CheckLine gaps the new check surfaced (pre-existing, for the mana-fidelity follow-up):**
    Stage-2 does not credit **fetchland-produced colors** (a fetched dual's colours), so lines like
    Anti-Lifegain's `Tainted Remedy + Skyshroud Cutter…` read `illegal: no black source` even though
    the fetched land makes black (`PerformFetch` default pick, `PlayLandByName` at 5832-5840). Same
    family as #9. Some `NO_VALIDATION_BLOCK` entries are the harness prefix landing on a sub-decision.

- **2026-07-27 (batch 4 — #12 Commit Line, GT-neutral):**
  - **#12 DONE.** The claude-play main-phase segment loop (`AIEngine.cpp` `use_external` branch, the
    human-play chooser path — **GT-neutral by construction**, the autonomous search never enters it)
    now re-prompts the SAME phase not only on a DRAW (the existing breakpoint) but also when a committed
    line puts a NEW, AFFORDABLE sac-to-draw source into play — e.g. play Horizon Canopy this turn and
    the engine offers its same-turn `{1},{T},Sacrifice: draw` (previously forced to the next turn since
    playing a land draws nothing). Trigger is the INTERSECTION of two per-segment signals: `inplay_sac`
    (untapped sac-to-draw permanents in play — a source appears here the segment you play it untapped; a
    STANDING source is present at phase start so never counts as "new" → no per-turn spam) ∩ `offer_sac`
    (the sac dig actually OFFERED among enumerated plans, which `AppendHumanPlayDigPlans` already gates on
    the `{1}` cost being affordable this phase). Requiring BOTH excludes (a) a standing source whose sac
    merely became affordable when you played a second land (spam, not a just-played ability) and (b) a
    source you played untapped but can't yet pay to sac (pointless re-prompt). Using the ability drops it
    from the signature (sac removes the source; a draw shrinks the library → the draw breakpoint takes
    over) and a pass breaks, so it cannot spin. GUI: rename "Commit phase" → "Commit Line" (+ tooltip);
    "Commit turn" needs NO change — its existing auto-pass (`index.html` advanceTo, fires on every
    same-turn `main_phase` decision) already swallows the new breakpoint, so it stays exempt.
  - **Verified:** build clean; `viewer_linebuild_check` 0 FAIL and `viewer_protocol_check` **0
    play-drift** (autonomous byte-identical, confirming GT-neutrality); the mechanism fires (treasure_hunt
    Fiery Islet — identical `sacrifice_draw_cost` code path to Horizon Canopy). Auras s12 correctly does
    NOT trigger (its Horizon Canopy is a *standing* untapped source, and its affordability-flip is
    excluded by the intersection — the earlier plan-based-only prototype wrongly fired there).
  - **Validate baseline +2 (documented, NOT a CheckLine regression):** adding a human-play decision point
    inherently SHIFTS the positional `--choices` stream for PRE-#12 recordings that trigger it. Exactly
    one reference does — `treasure_hunt/claude_s4_gi3` plays Fiery Islet untapped+affordable, so the new
    same-turn-sac breakpoint now inserts a decision at ~T4, desyncing its post-T4 replay; T7
    (`land=Thundering Falls`) and T8 (`pass`) surface as validate fails (others post-T4 replay-shift but
    happen to still validate). These are choice-stream shifts, VERIFIED non-CheckLine (stash-test: 0
    regressions with #12 reverted), so they were added to `viewer_validate_baseline.txt` (now 116).
    **This does NOT corrupt the reference:** saved games are VIEWED in `tools/replay/` from their stored
    `decisions` (no engine re-drive), and new recordings are self-consistent (the breakpoint is recorded
    as played); only a `--choices` RE-DRIVE of a pre-#12 recording (which only the validate test does)
    shifts. **Deferred robustness:** make `viewer_validate_check.js` drive the engine decision-by-decision
    and auto-pass any extra (breakpoint) decision the recording lacks — then no baseline entry is needed
    and the check is robust to ANY future added human-play breakpoint. Left for later (the current cost is
    2 trivial lines in 1 game).

- **2026-07-27 (batch 5 — #5 discard payoff protection, GT-affecting → rebaselined):**
  - **#5 DONE.** Set `decks/Dragonstorm/Dragonstorm.profile.json` `mulligan.required_pieces` =
    `["Apex of Power", "Dragonstorm"]` (was `[]`). `SelectCleanupDiscardIndex` (`SpellEffects.h:102-115`)
    now skips these two irreplaceable payoffs when picking the highest-MV cleanup discard, in BOTH the
    autonomous rollout (`AIEngine.cpp:3644`) and interactive play (`main.cpp:1117`) — the search rollout
    reads the identical set via `GameState::m_required_pieces`. Dragons are redundant, so pitching an
    excess Dragon stays correct; only Apex (MV 10, previously always pitched) and Dragonstorm are held.
  - **GT rebaselined (smoke + regression, scoped `--deck=dragonstorm`, ACCEPTED).** Net loss-penalized
    delta is strongly POSITIVE — every regression-mode case improved (d0/d3/d5 × s2002/s3003:
    5.574→5.549, 4.463→4.437, 4.573→4.563, 4.452→4.420, 4.560→4.544) and smoke d0 (5.563→5.524); across
    the 5 regression cases **7 losses recovered, 4 wins lost, 19 faster, 4 slower-still-win**. The deck
    stops stranding its only wincon, so screwed-but-recoverable games now win. The few win→loss games
    (gi190 etc.) are MANA-SCREWED (stuck on 2 lands) where protecting an uncastable payoff pitched a
    needed land — the known blunt-heuristic tradeoff the user deferred to "searched discard later"
    (`AIEngine.cpp` `MTG_SEARCHED_DISCARD`). smoke d3 dipped +0.013 (gi78/gi110, same screw class),
    dwarfed by the regression gains → net positive, accepted. **OVERNIGHT GT (seeds 4004-7007) still
    OLD** — Dragonstorm will show a diff on the next `--overnight` run; rebaseline it there (expected
    same-direction improvement, NOT a regression).

Reference reproductions to sanity-check specific items: Dragonstorm s21_gi20 (Lotus Bloom
black, item 9 — see `logs/play/rejections/Dragonstorm_cod_s21_gi20_t7.json`), s9_gi8 (splice
display #8), s24_gi23 (cast order #10), s3_gi2 (discard #5); Auras s21_gi20 suboptimal
(Horizon Canopy same-turn sac #12).
