# FiveColour gen tractability — state of play, and what a fresh pair of eyes should do

2026-08-17. **Self-contained handoff.** Read this file and
`fivecolour-payment-query-fold.md` (its sibling, which holds the full measurement record) and you can
start cold. You do not need any prior conversation.

---

## 1. The goal, in one ratio

We want to generate FiveColour's exhaustive mulligan profile. At K=27 (fetch cycle merged, already
by construction in `DiscoverEquivalence`) the size-7 phase is 1,977,898 cells; at R=10 — the floor
for a shippable runtime profile — that is 39.6M rollouts.

| rollout rate | wall clock (23 cores) |
|---|---|
| 110/s/core (the skill's guide, i.e. a normal deck) | **~4.3 h** |
| 4.8/s/core (FiveColour, measured) | **~100 h** |

**Neither K nor R needs to change.** The entire question is that ~23x rate gap. Close it and the run
lands in hours; don't, and the run is impractical (the user's own read, 2026-08-16: "It doesn't seem
like we'll be able to run Five Colour over the weekend").

## 2. What has been measured, so you do not re-do it

Six things have now been killed by measurement, not by argument. Details and raw numbers are in
`fivecolour-payment-query-fold.md`; the one-line versions:

| # | candidate | verdict |
|---|---|---|
| 1 | cache MANA across plans | **1.00x collapse** — 173,616 combos -> 173,526 distinct at the exact key. No duplication exists to cache. |
| 2 | colour-exact frontier rejecting plans pre-application | **1.1%** of plan applications are unpayable. The 92.2% prepay "decline rate" that motivated it is **89.0% `<2 casts`** — a single-cast turn, where folding one cost into one cost is a no-op. Never a failure signal. |
| 3 | `MTG_SOLVE_MEMO` (greedy-Solve memo) | **worth ZERO at HEAD** (18.42s vs 18.46s, identical node counts) despite being live at a 42% hit rate. Its recorded −21% was measured at d3/200ms; shipped is d6/20ms `value_play`. *(Verdict unchanged for THIS config. The flag was nonetheless ADOPTED default-ON on 2026-08-19 — it is 1.111x suite-wide and 1.3x on KittyEquipment, whose greedy second main is 55% of all considerations at 86.5% duplication; see `analysis-KittyEquipment.md`. FiveColour's shipped config simply is not where it pays: this table's row is the reason nobody should expect it to help here.)* |
| 4 | `MTG_ENUM_MEMO` (enumeration memo) | sound but **measured NEGATIVE** — the `BuildBreakpointKey` walk costs as much as the enumeration it saves. Its own revisit condition is an INCREMENTAL key, not a better cache. |
| 5 | `EnumGroupCap` / plan breadth per node | **inert** — byte-identical node counts at cap 10 and 12; cap 6 buys 2.5%; cap 4 costs quality (5.1867 vs 5.1133). |
| 6 | payment/backtracker cost per answer | ~1–2% of runtime, and a flow-guided tap order already cut nodes 13.6x for ~1%. `d8d7da10`'s scalar emission prune shipped and saved nothing. |

**The one thing that DID pay** (shipped `d00d65c6`, −4%): the plans genuinely differ, so there is no
*answer* to reuse — but every plan loop copy-CONSTRUCTED its scratch board inside the loop, so each of
1.48M plan applications freed the previous plan's buffers and malloc'd near-identical new ones.
Hoisting the board and copy-ASSIGNING reuses capacity. **What repeats across plans is the
ALLOCATION, not the answer.** That framing is the single most useful thing to carry forward.

## 3. What the 23x actually is

Not wide nodes — **many** nodes. The evidence, from `MTG_ENUM_STATS` / `MTG_BRANCH_STATS`
(12 games, seed 1001):

* 2,090,441 enumeration calls; 31,258,644 odometer positions (~15 per call).
* The dominant branch bucket is `groups=0-4 board=7-10`: **1,147,215 calls at an average odometer of
  just 10.1**. The wide bucket `groups=9-12` is **102 calls** out of 2.09M.
* `interior_nodes` (= plans applied) 1,480,179 per 12 games, vs Goblins' 317 per game.

So the search visits far more states, each modestly wide. That is what a five-colour deck with many
permanents and many activatable mana sources genuinely produces. **Treat "make the search cheaper per
node" as largely exhausted.**

## 4. Where a fresh pair of eyes should look

Ranked, with the honest size of each. Nothing here is started.

1. **The NUMBER of nodes (depth / beam / horizon policy).** This is where the remaining order of
   magnitude is, and it is the one lever nobody has swept for FiveColour. It is a QUALITY trade, so
   it must clear BOTH seed sets per `.claude/skills/heuristic-optimization.md`, and adoption is the
   user's call. Note FiveColour ships `value_play` d6/budget-20 (`decks/FiveColour/FiveColour.value.json`)
   — check whether d5 or a beam at near-leaf depth holds avg win turn; the depth matrix machinery
   (`.claude/skills/value-leaf.md`) already exists to answer exactly this.
2. **Accept the rate and size K/R around it.** Genuinely on the table given #1 is a quality trade.
   The feasibility maths at the top of this doc is all that is needed to re-scope.
3. **`SimulateEndAndStartNextTurn` (24.8% of profile).** Never attacked. Unlike the caches above it
   is a straight per-turn cost, and cleanup-discard inside it is profile item 1
   (`engine-cost-profile-2026-08-16.md`) — recomputed per card shed, returns a fully ranked vector
   when the caller reads only index 0, and allocates two vectors per call. Byte-identical to fix.
   **Caveat: that doc's own correction banner says its perf MAGNITUDES are unreliable in this
   container** — size it first (`profile-before-optimizing`).
4. **Land-fan sharing.** `EnumeratePlansWithLand` re-runs the whole spell odometer once per land
   candidate (~4.3x on 5c). NOT cacheable by equality — `land_sig` already dedups interchangeable
   lands, so surviving candidates genuinely produce different enumerations. The route is structural
   (the land-after-draws timing doctrine in `main-phase-classification.md`), which deletes the fan
   rather than caching it.

## 5. How to measure on this machine — this part is load-bearing

The box is SHARED and contended (another agent works in parallel; load average hit 22). This
invalidated a measurement mid-session: the same arm ranged **18.7s to 39.7s**.

* **Hardware counters are `<not supported>` in this container.** `perf stat -e instructions` returns
  nothing, so there is no cheap contention-proof instruction count. Do not plan around it.
* **Prefer DETERMINISTIC metrics.** `MTG_ROLLOUT_STATS=1` gives `interior_nodes` (plans applied) and
  `turn_steps`; avg win turn is deterministic too. Both are immune to load *entirely*. The
  `EnumGroupCap` sweep in §2 was decided this way under heavy contention and is fully trustworthy.
* **If you must use wall clock**: many SHORT alternating runs, and require FULL DISTRIBUTION
  SEPARATION, not a better mean or a single pair. The −4% in §2 was accepted because all 10 `on`
  samples beat all 10 `off` samples; the combined-change follow-up was REJECTED as unresolvable
  because the arms overlapped once the box got busy.

Useful instruments, all already in the shipped binary (no rebuild): `MTG_PREPAY_PROBE`,
`MTG_ENUM_STATS`, `MTG_BRANCH_STATS`, `MTG_CONSIDER_STATS` (slow), `MTG_ROLLOUT_STATS`,
`MTG_TAP_STATS`.

Baseline command used throughout:

```
build/Release/mtg decks/FiveColour/FiveColour.cod \
  --profile decks/FiveColour/FiveColour.profile.json --games 12 --seed 1001 --threads 1
```

## 6. Constraints a fresh agent will otherwise trip over

* **Do not touch the GREEDY path.** The user has a separate agent removing greedy parts from the
  search entirely (user, 2026-08-16: "I don't want greedy stuff"). That is why `MTG_SOLVE_MEMO` is
  not being pursued even aside from measuring zero — it memoizes the greedy second main.
* Everything in `CLAUDE.md` applies, especially: never `timeout`; never a merge commit (rebase);
  one pooled `mtg --batch` queue for long runs (waves are a loop); only the USER cancels a
  user-requested run past ~10 min.
* ~~Three antilife smoke keys FAIL against GT.~~ **RESOLVED 2026-08-17** by another agent's overnight
  rebaseline (`0cb6807b`, net −0.1889 across 36 keys). GT now matches byte-for-byte. Smoke is
  **36 passed / 0 failed, ALL PASS**.

## 6a. One loose end, closed by someone else (read this before re-investigating)

Late on 2026-08-16 a smoke run showed a NEW failure — `reference reproducibility (--strict): a
recorded human game no longer replays` — immediately after 12 hand-played Mirrorwing references were
committed. The obvious inference (the new references drifted, or a perf change moved play) was
**wrong on both counts**: the digests were all green, and the replay was not drifting, it was
**exploding**. Root-caused and fixed independently by another agent as `767c0c6a`, *"the SacColor
DEMAND filter is not a prune -- unpruning it cost 10 GB per replay"*
(`docs/design/claude-play-unprune-blowup.md`). `5aeffecf` then moved the reference sweep to
REGRESSION mode only. Do not re-open this; it is closed, and the references were never at fault.

## 7. Current validated state

Re-verified 2026-08-17 on a REBUILT binary after rebasing onto the OOM fix (`767c0c6a`), which
touched `src/ai/TurnSolver.cpp` — the repo's post-rebase rebuild-and-recheck rule:

* Branch `phase-1-2-deck-analyzer`, all work pushed. Working tree clean.
* Smoke: **36 passed, 0 failed — ALL PASS.** Byte-identity of this arc's changes was separately
  proven by 36/36 digests identical across flag-on / flag-off / audit arms.
* Regression: **60 passed, 0 failed — ALL PASS.** Reference sweep clean:
  `156 ok, 52 repaired, 0 play-drift, 0 shuffle-dead, 0 enum-gap, 0 mull-drift, 0 contract-fail`
  over 208 refs (includes the 12 new hand-played Mirrorwing games).
* Shipped this arc: `d00d65c6` per-plan state reuse (−4%, `MTG_NO_STATE_REUSE=1` hatch);
  `b5c3de95` RevealLogPause sticky flag + a real `g_play_soulfire_chooser` leak fix
  (`MTG_PAUSE_HOOK_FLAG=0`, `MTG_PAUSE_HOOK_AUDIT=1` hatches); `e25fb644` the EnumGroupCap finding.
* FiveColour has **no mulligan profile** and is **not in the regression tiers** — both user-initiated.
  *(2026-09-03 correction: FiveColour has been in ALL THREE tiers since 7cfbd079, 2026-08-14 —
  before this line was written; only the "no mulligan profile" half holds.)*
