# One bounded treasure_hunt game costs hours — a REGRESSION in the depth curve

**Status: ROOT-CAUSED, NOT FIXED (2026-08-06).** Handing off. The user's read was right at every
step: a `max_turns=8` goldfish game must never cost hours, and this is a regression, not the honest
price of a hard position.

## The headline: the depth curve used to be FLAT

Same game (`--seed 9010 --game-index 1`, i.e. base seed 9009 game index 1), V arm, one core:

| depth | pre-07-28 (`MTG_BP_SEARCH=0`) | today |
|---|---|---|
| d3 | 0.44 s | 5.2 s |
| d4 | 0.43 s | 89.9 s |
| d5 | 0.41 s | 1300.6 s |
| d6 | 0.42 s | never finished |
| d7 | 0.41 s | never finished |
| d8 | **0.44 s** | never finished |

**Flat at every depth before the breakpoint search; ×12–15 per ply after.** This is a regression in
the EXPONENT, not a constant factor. Extra depth used to cost nothing on this game, and correctly so:
the game ends on turn 3–4, so once the horizon covers the remaining game there is nothing left to
search. That property is what broke.

The regression is a *quality* feature working as designed — `MTG_BP_SEARCH=0` also gives a WORSE
answer (turn 4 vs turn 3) — so this is not "revert it". It is "the same quality must cost vastly
less".

## Mechanism

Breakpoint continuations recurse with `depth - 1`, so **breakpoints consume depth plies inside a
single turn**. `--depth 8` stops meaning "look 8 turns ahead" and becomes "explore 8 nested
decisions", which on a flood turn subdivides a turn whose game is already won. The 40-wide plan
enumeration (below) is then re-paid at every one of those plies, where before 2026-07-28 it was paid
about once per turn.

## The measurements (all on the one game, d4, `-DMTG_PROFILE=ON` build)

Arm comparison:

| arm | wall | nodes | answer |
|---|---|---|---|
| baseline | 89.9 s | 1,349,112 | T3 |
| `MTG_BP_WAVES=0` | 26.8 s | 494,142 | T3 — identical |
| `MTG_BP_SEARCH=0` | 0.72 s | 554 | T4 — worse |

Search tree shape:

```
FSLineWin nodes : 17,411   candidates: 707,266   (40.6 branching)
by game turn    : t3=73  t4=215  t5=1385  t6=10341  t7=3926  t8=4068
by depth left   : d0=110  d1=17905  d2=1860  d3=138  d4=8
```

Wave phase:

```
[bp-waves] nodes=28044 slots=286714 scored=1070393 rolled=422434
           improved=0  max-rank=48  nested-scored=42328  max-at=2
```

Breakpoint sites:

```
[bp-cands] DrawUntilNonland (Treasure Hunt)  n=177044 mean=5.71 max=49
           capped=41719 (23.6%)  unreachable=779910 (77.1% of all continuations)
   len: 1=122579  2=12746  3=1280  5-8=214  9-16=14889  17-32=23426  33-64=1910
```

### What those say

* **Branching is 40.6 candidates per node** — ~14 distinct land names × ~3 cast options.
* **`improved=0`.** Not one of 1,070,393 scored wave candidates ever beat its node's incumbent.
* **`max-at=2`.** Nesting never exceeds depth 2; nested candidates are 4% of the total.
* **177,044 Treasure Hunt breakpoint evaluations** in a game that casts Treasure Hunt 2–3 times.
* **69% of breakpoints (122,579) have exactly ONE continuation** — not a decision at all.
* **77.1% of continuations are never reached.**

## RULED OUT (each measured, not reasoned)

* **The retrained STAGED value model** — 91.2 s vs baseline 89.9 s. Not the cause.
* **`MTG_FS_NOWIN_CACHE`** — 92.5 s with it off. Not the cause.
* **Cycling lands / Fiery Islet opening breakpoints** (the intuitive suspect, and the user's own
  hypothesis). The site probe shows the dig-through-lands class firing **zero** times; only
  DrawUntilNonland appears. Cycling in the autonomous search is not branched at all —
  `SelectDigSource` returns a single card NAME, so copies can never split — and `ShouldConsiderDig`
  already declines to cycle when a draw engine is in hand.
* **Copy-level duplication** (Lonely Sandbar 1 vs 2, Fiery Islet 1 vs 2). Already deduped: dig is
  name-keyed, land plays collapse through `land_sig`, whose signature is identical across copies.
  The 40.6 branching is 14 DISTINCT land names, not 53 copies.
* **Nesting depth.** A `MTG_BP_NEST_MAX` cap was written and reverted: `max-at=2` means any cap ≥ 3
  never binds, so it was pure reachability risk for zero speedup. **Measure the axis before capping
  it.**
* **The leaf TT no-win cache** (`MTG_TT_NOWIN_CACHE`, committed `1612bc0`, default off) — sound and
  real (44,970 memo hits where there were zero) but worth only 8%. Not related to this.

## What is implemented but NOT shipped

Two sound cutoff prunes, currently uncommitted in the working tree, worth **1.63× together**
(89.9 → 55.0 s, same T3 answer):

1. The wave loop handed its continuation `cutoff = best.win_turn` while the acceptance test is
   strictly `<`, so every line winning exactly AT the incumbent turn was searched and discarded.
   Tightened to `best.win_turn - 1`.
2. `FSLineTail` was missing the `state.turn_number > cutoff` guard its sibling `FSLineWin` has at
   entry, so it enumerated and recursed to reach a no-win it would return anyway.

The same tightening was applied to the main loop too. **Caveat:** the main loop records
`tail.win_turn` into `node_vals` for the beam reorder, so coarsening a non-improving value to
`max_turns + 1` makes previously-distinguishable plans tie — a RANKING change that can move play.
Smoke: **26 passed, 1 failed** ("2 searched play-changed at same score" on analyze). That is the
expected budget-reallocation effect (a work-saving prune frees budget the search spends elsewhere,
exactly as `MTG_FS_NOWIN_CACHE` did), but it means these need an **unbounded identity check** plus a
GT rebaseline before shipping — not a byte-identical claim.

They are a constant factor. They do not fix the exponent.

## The two proposed fixes (NOT implemented — this is the handoff)

**Fix 1 — don't emit variants past the continuation list's length.** Wave 0 emits `W=2` variants
*blind*, before knowing `n`. For the 69% of breakpoints with `n == 1`, the second variant re-applies
the whole plan, discovers it is past the end, falls back to greedy and is discarded as a copy of its
own base. The length is already memoised per breakpoint state (`EnumerateBreakpointPlans`, keyed on
`BuildBreakpointKey`) and the wave walker already learns it — wave 0 simply does not consult it.
Emitting `min(W, n)` is strictly lossless: the dropped variant was provably a duplicate. Removes a
wasted `ApplyPlanDirect` on more than two thirds of all breakpoints.

**Fix 2 — 177k breakpoint derivations for ~3 real casts.** The same breakpoint state is re-derived
across the tree. The memo that should collapse this (`EnumerateBreakpointPlans`) is `thread_local`,
capped at `kBpEnumCacheCap = 8192`, and **cleared wholesale on overflow** rather than evicting. This
is the one that plausibly restores the flat curve.

A third, lower-confidence direction: the 61% post-apply dedup rate (`rolled` 422,434 of `scored`
1,070,393) means most continuations converge to states already seen — they are applied and rolled
out *before* being recognised as duplicates. Deduping at enumeration by outcome-equivalence (not
`land_sig`'s static signature, which does not collapse TH's 14 differently-shaped lands) would remove
work that provably cannot change an answer.

## Method notes / traps hit in this investigation

* **A counter on one `return` of a multi-return function lies.** "2.4 plans/call" was measured on
  `EnumeratePlansWithLand`'s final `return all`, but the `!drop_available` early return takes most
  traffic. True branching is **40.6**. The wrong number made the enumeration look narrow and sent the
  investigation at the nesting axis instead.
* **`Search nodes` is not a work proxy here** — `PROF_ADD_NODES(budget.Used())` fires once per
  DECISION (`AIEngine.cpp`), not per rollout. Use `EnumeratePlans` / `ApplyPlanDirect` / the
  tree-shape counters added in this session.
* **A prune is never byte-identical under a budget.** Freed budget gets respent; the soundness test
  is the UNBOUNDED identity check.
* The Bash tool's 120 s default timeout kills probes — background them.

## Instrumentation added (uncommitted, keep it)

* `Profiler.h`: per-DECISION cost (turn, pre/post, nodes); search tree shape (`fsw_by_turn`,
  `fsw_by_depth`, `fsw_nodes`, `fsw_cands` ⇒ mean branching).
* `TurnSolver.cpp`: `PROF_ADD(plans_generated, all.size())`, and the tree-shape bumps at
  `FSLineWin`'s entry / candidate loop.

## Exact repro

```
MTG_VALUE_MODEL=1 MTG_VALUE_PROFILE=logs/eval/treasure_hunt.value.STAGED.json \
MTG_VALUE_MIN_DEPTH=0 MTG_VALUE_STARTGATE_ALPHA=8 \
build/Release/mtg decks/treasure_hunt/treasure_hunt.txt \
    --profile decks/treasure_hunt/treasure_hunt.profile.json \
    --seed 9010 --game-index 1 --games 1 --max-turns 8 --threads 1 \
    --ignore-play-profile --depth 4
```

Every sibling game in its batch costs ~2.3 s, so the contrast is stark and any instrumentation is
cheap to set up. Probe flags: `MTG_BP_WAVE_PROBE=1`, `MTG_BP_CANDS_PROBE=1`, and the
`-DMTG_PROFILE=ON` build in `build-instr/`.

## Why it mattered operationally

It is what stalled phase C of the value-leaf regeneration: TH V7/V8 cells sat at `games=0` for 13
hours. Note the deep cells are NOT decision-relevant — **V6–V8 are already complete at 400 games for
seven of eight decks**, and TH's V column has converged by d4 (V 4.0838 vs H 4.0713). The reason to
fix this is the engine defect itself, not the table.

## Related

- `post-breakpoint-search.md` — the searched continuation (`52d7faa`, 2026-07-31, "nested
  breakpoints are searched by default") whose cost this is.
- `breakpoint-width-deferred-waves-2026-07-29` / `abdecb4` — the wave phase; `improved=0` here is one
  game, not a verdict on the feature (held-out −0.00228, 21 better / 0 worse).
- `bound-qualified-nowin-memo.md` — the leaf/interior no-win memos, measured ~1.00× on TH.
- `depth-matrix-should-use-batch-pooling.md` — the harness half of the phase-C stall.
