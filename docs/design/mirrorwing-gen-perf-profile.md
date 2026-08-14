# Mirrorwing phase-A generation: where the time goes (profile, 2026-08-12)

Profiling pass driven by the value-leaf regen plan (phase-A label runs are the wall-clock cost:
K=3 de-clairvoyed `EnumerateEarliestWins` full searches per real pre-combat main). Data below is
the Mirrorwing label config (`MTG_DUMP_VALUE_ROWS` + `MTG_EVAL_ROWS_K=3`, shipped play), current
engine (post tap-backtrack sibling collapse + mana-cache extensions, which already removed the
old payment blow-up class).

## Method notes (hard-won)

- **Callgrind DIVERTS this config's games** (a repro that wins t5 natively goes unwon under
  callgrind, both build/Release and build/Profile — natively both reproduce exactly). Cause not
  yet isolated (something wall-clock- or environment-sensitive in the value_play path). Do NOT
  trust callgrind attribution for label-config games; suite configs (explicit `--depth
  --budget-ms`) have profiled faithfully in the past.
- **perf record is broken in this WSL2 env** ("Bad address" even on `sleep`).
- What works: `build-instr` (`cmake -B build-instr -DMTG_PROFILE=ON -DCMAKE_BUILD_TYPE=Release`)
  deterministic counters, and repeated gdb stack sampling of a native heavy game. Wall A/Bs need
  `taskset` pinning (unpinned runs on this box are bimodal ±50%) and a worktree control build.
- Repro of a batch slow-game: `--seed <base+gi> --game-index <gi> --games 1` + the SAME env; the
  engine's SLOW-GAME line's args are correct.

## The shape (MTG_PROFILE counters, 60 games, seeds 900000+)

- 96% of search nodes at game turns 6–8 (fan-out boards); **90% of nodes at remaining depth 1**.
- Mean branching 31.9 candidates/node; 10.8M EnumeratePlans calls → 113M plans → 114.8M
  ApplyPlanDirect; 12.8M GameState deep copies (2.14/node); FSLine no-win memo hits 69%.
- The tail is extreme: 1 game of 60 took 951s of the 952s batch wall (16 min on one worker).

## Adopted this pass (byte-identical; pinned heavy-game A/B −4.9% combined)

1. `CardDatabase::LookupInterned` — canonical-interned-pointer → def index; kills the string
   hash+equals per fresh token Card (fan-out applies recreate tokens every speculative apply).
2. `Action::card_name/tutor_target/chosen_float_color` → `InternedName` — Actions are copied,
   sorted, destroyed ~10/plan × 113M plans; names over SSO (17-char card names) heap-allocated
   on EVERY copy.
3. `RevealLogPause` no-op fast path — nested pauses (per-EnumeratePlans, millions) found all 26
   hook globals already null and still did ~104 loads+stores; now one null-check pass.

Suite-level: neutral (smoke 40s both arms; suite wall is search-dominated there too).

## Deferred levers, ranked (from gdb samples of the real heavy game)

> **STALE post-site-5 (2026-08-12).** This ranking predates the trick-class site-5 split
> (2abb1c7). On the current binary, in-process MTG_WAVE_TIME timers on the gi69 label repro
> show the label wall is dominated by **deferred-wave ApplyPlanDirect replays (~54% of wall;
> 63.5M full applies for 6.0M wave slots, 75% deduped post-apply)** — GameState deep copies
> are now only ~3.5% of the label wall. The top lever today is the prefix-resume cache
> (capture the GameState at the deferred breakpoint once per base plan, resume per rank),
> not apply/undo. The ranking below still describes the pre-site-5 shape of the d1 fan-out.
>
> **UPDATE 2026-08-13 — prefix-resume cache BUILT (61a08ca) and it settles the attribution.**
> Byte-identical, default-on, but only ~4% of the gi69 label wall (1342→1290s pinned-idle):
> the redundant prefix apply is ~1-2µs of each ~14µs wave walk (this deck's base plans are
> 1-3 actions), so "63.5M full applies" was the right count but the wrong villain. The walk's
> real cost is (a) the CONTINUATION re-solve + apply per rank — genuinely different work per
> rank, not redundancy — and (b) the FSLineTail rollouts of the ~26% of walks that survive
> post-apply dedup. The remaining honest levers for label-path cost, in order of leverage:
> 1. ~~Wave policy for the LABEL path~~ REJECTED by user (2026-08-13): "not an honest lever" —
>    an empirical improved=0 is not a structural guarantee across decks/scale. Only provable
>    folds qualify. (And see the update below: the improved=0 itself was partly a bug.)
> 2. Pre-apply equivalence prediction for the 75% post-apply-dedup class (hard in general;
>    provable subsets — identical targets — are already folded at enumeration).
> 3. Tail-rollout cost (FSLine no-win memo already absorbs 69% of revisits).
>
> **UPDATE 2026-08-13 (2) — the duplicate class was a DEFECT, not a fold opportunity.**
> A temp classification probe (MTG_BP_DUP_PROBE, stripped after diagnosis) split gi69's 53M
> scored wave walks: 14.0M fresh / 15.3M dup-vs-main / 13.6M dup-intra-slot / 9.4M dup-cross.
> The dominant intra class (10.75M of 13.6M) was "one extra cast, same post-state", and the
> extra cast was overwhelmingly **SacForMana(Treasure Token)**. Root cause: `apply_plan_actions`
> handles only vial/hand/sac-land/graveyard casts; site 2 (staging) grew a SacForMana/Suspend
> pre-loop for the staged Dragonstorm rituals, but the deferred trick site 5 (and sites 0/1/4)
> shipped without it — **the Gold Rush Treasures the site-5 deferral exists to spend never
> actually cracked in the rollout** (the executor's live fallback `resolve_draw_breakpoint`
> does crack: a rollout/executor divergence). Every crack-carrying continuation rank silently
> collapsed onto its crack-less sibling. Fixed with one shared `apply_continuation_precasts`
> loop at all six continuation sites (recorded into the sink; `replay_recorded` already applies
> SacForMana, so committed lines stay lockstep). gi17 after the fix: intra diff-casts
> 29,806→1,752, fresh walks 18.8K→33.0K (treasure-funded lines are now real reachability), and
> wave `improved` 1→5 — the gi69 improved=0 that motivated the rejected wave-off lever was
> partly this bug. Remaining duplicate classes are honest post-apply convergence (crack colour
> variants; unaffordable-cast greedy fallback) and stay with the dedup set. Label-path cost
> must be RE-MEASURED on the fixed binary (more fresh states ⇒ more tails, but real ones).

1. **GameState deep copy (~26% of samples pre-site-5; 12.8M copies/batch).** The real fix is
   apply/undo at the FSLine d1 leaves (the backtracker's pattern) or a pooled/arena Permanent
   storage. Big, behavioral-risk-free in principle but architecturally invasive; needs its own
   session. (Post-site-5: demoted — see note above.)
2. **d1-leaf dominance (90% of nodes).** A d1 node enumerates ~10 plans, deep-copies + applies
   each, and simulates the turn end. Any structural saving here (plan-enumeration memo keyed by
   BuildSimKey, or a slimmer d1-only evaluate path) multiplies across 10M nodes. Behavioral risk:
   must stay byte-identical; the FSLine memo already absorbs 69% of revisits.
3. **BuildSimKey (~8% Ir).** O(state) fold per memo probe (11.3M/batch); incremental keying would
   be a large change — only worth it after (1)/(2).
4. **CleanupDiscardRanking (~6% Ir).** Runs only when a rollout hand exceeds 7 (real on
   Mirrorwing: Fists draws + staged cards), re-ranks per shed with per-compare
   LookupCached+ManaValue. Cache the rank per (hand multiset) or hoist MVs before the sort.
5. The label K=3 cost itself is irreducible per design (independent reshuffled futures are what
   de-clairvoys the label); `MTG_VALUE_LABEL_BNB` + the label ladder are already on.

## The 2026-08-14 degenerate-case investigation (post phase-A, pre-matrix)

Phase A completed on `230765d6` (2,500 games, 12,570 unique rows; ~20 h wall across an OOM split).
The census (`logs/vlq_mirrorwing_dragon/rows.batch.log`, machine-local): ~45 games >30 min, worst
finished 6.4 h (900813/gi63); distribution measured as 14 >1h / 17 30-60m / 58 10-30m over the
first 60% of games. Findings, each measured:

- **The deck's degenerate cost has TWO classes.** Class A: mulligan games with NO exhaustive
  keep/bottom table (Mirrorwing is the only suite deck without one) pay a full search-driven game
  rollout per candidate per bottom step -- killing them collapsed gi=14 from 57 s to 130 ms (H5)
  and 163 s to 94 ms (H6), and the FiveColour precedent in `AIEngine.cpp` records 90.4% of runtime.
  The fix is the deck's mulligan profile (its own skill), NOT search work. Class B: kept fan-out
  hands genuinely exhaust the T1 committed pass (gi=17: 99.1% of 140.9M rollout-steps in ONE
  decision, 1.02M candidates, 50-way branching; >400 s with bottoming off). Class B's root is the
  search/play mismatch -- see `mirrorwing-search-play-mismatch.md` (fix planned, blocks the matrix
  by choice).
- **Cache caps: sized from measurement, and SKEWED.** FSL line-cache entries measured ~600 B (not
  the 8 KB first guessed -- that guess was the dominant cost of the resumed phase-A tail: 3-5x wall
  on mid-tier heavies, and the 6.4 h monster re-ran in 1:57 UNLIMITED at only **924 MB peak
  appetite**). Usage is heavily skewed (typical game ~100 MB, monsters ~900 MB, observed
  concurrent monsters ~7/32), so a uniform per-worker share strangles exactly the games that
  matter: the cap should be sized ABOVE the uniform share (~1.5M entries on the 23 GB box), not at
  it. The 28 GB single-decision analyzer measurement is not representative of these games.
- **The label budget ceiling binds on monsters**: the 900359 counter run dropped 1 of 5 positions
  at `MTG_VALUE_LABEL_BUDGET_MS` -- phase A's rows have a small systematic gap at exactly the most
  degenerate positions (by design; no row rather than a poisoned one).
- **Unwon games audited, no play bug**: 900248 wins unbounded (budget starvation, priced);
  900369 is genuinely dead on its true order (`MTG_DUMP_EWINS` earliest=9; the label's 5.7 was
  reshuffle optimism).
- **Matrix first-chunk probe** (25 games x 14 cells, seed 8008, `logs/mwprof/matrix_probe.log`):
  H ladder 0.4/3.3/17.2/56.6/78.9/65.1 s/game (H1..H6), V ladder 0.005..4.8 s/game (V1..V8) --
  whole-matrix estimate ~106 core-h as-is, BEFORE the keep table and the mismatch fix, both of
  which attack its dominant terms. H6<H5 in the mean is a 2-game artifact (win verifying at a
  leaf-priced warm-up rung); per-game H6>=H5 everywhere else -- see the mismatch doc for why.

## Mana-payment solver EXONERATED in the Class B monsters (2026-08-14, tap-stats probe)

Stack-sampling the two live seed-603017 monster games (adoption A/B, ~4.5 h each) showed both
threads permanently inside `TapForCostBacktrackWorker` — raising the question whether the payable-
mana cache (`MTG_MANA_CACHE`, extended for this deck 2026-08-12) was gate-disabled on explosion
boards. **It is not.** `MTG_TAP_STATS` on the gi=17 monster repro (seed 8025, unbounded d5):

- `max board n = 18`, `memo-off(n>64) = 0` — no board ever approaches the 64-permanent SHAPE gate
  (the user's prior: this list cannot reach 64 permanents without lethal — confirmed).
- 5.33M top-level entries, 14.6M nodes, **2.7 nodes/entry** — every solve is trivial; flow-prune
  kills 43% of entries up front; unpayable proofs cost 1.0 node each.
- On an ordinary 30 s Class B game (seed 623262) the cache absorbs 94% of entries (22.5K reach the
  solver, vs 385K with `MTG_MANA_CACHE=0`).

The payment solver shows in every stack sample by VOLUME, not unit cost: it is the innermost loop
of the Class B plan-tree exhaustion (993M `ApplyPlanDirect` calls in the gi=17 census). No per-call
cache fixes a caller making 10^8-10^9 calls. The levers for the monsters remain: the win-break /
search-play mismatch fix (`mirrorwing-search-play-mismatch.md`), branching-factor reduction, and
matrix-side degenerate-game avoidance.
