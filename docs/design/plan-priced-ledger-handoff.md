# Plan-cache priced ledger: handoff (2026-11-03)

Branch `plan-priced-ledger` (worktree `.claude/worktrees/plan-priced-ledger`, cut from `budget-rebalance`
d00d39b0f). Code commit **cf157bfb6**. Not pushed. No ground truth accepted.

## Goal
The plan caches (tier 2: the RAM-derived `MTG_PLAN_CACHE_KB` pool) changed play through the X=100 priced
decision ceiling. A cache hit skips priced work (`workctr::kEnumCall`, `kPlans`, `kOdoVisit`, `kSimKey`) that a
recompute counts into `workctr::t_priced`, and `decisionwork::PricedExceeded()` reads that counter. Evidence:
the regression tier, default vs `MTG_PLAN_CACHE_KB=1`, gave 6 job digests, 3 win turns and 14 games' units
different (`docs/design/tier2-cache-overflow.md`, last section). USER hard rule: tier-2 caches never change play.

## Design as built ("as-if-unlimited-cache" priced counting)
`namespace pledger` in `src/ai/TurnSolver.cpp` (just after `PlanCacheNoteHit`), plus `workctr::t_mute` in
`src/core/WorkCounters.h` (while it is > 0, `Inc` counts nothing).
- **Scope.** All three plan caches are cleared per game in `ClearPerGameCaches`; the enum memo is also cleared
  per decision epoch. So the unlimited-cache world is per game and does not depend on batch order. The shadows
  are cleared in the same places (`pledger::ClearGame()`, plus an epoch check on the enum shadow).
- **Shadows.** Each worker thread keeps a 64-bit key -> flag-byte map for each cache:
  - `t_enum`: the enum memo shared by the m1 and m2 hosts. 0 = first-visit marker; promoted = `kStored` plus
    the cost flags.
  - `t_bp`: `bpcache::t_plans`. Tutor-pin alias entries go under the pin-cleared key with `kPinUnread`.
  - `t_front`: `bpcanon::t_fronts`.

  Each shadow replays its cache's store rule with **no byte budget**: the same count caps (wholesale clear at
  8192 / 8192 / 16384) and no pressure clears (the real enum memo is also wiped by a bp store under pool
  pressure; the unlimited world never does that).
- **Lookup.** The shadow decides first, using `WouldServe(flags)`, which is CacheHitCharge's mode-1 test:
  - Shadow hit and a real hit: the entry is served free, as today.
  - Shadow hit but a real miss: the lookup recomputes with `pledger::Mute` armed. It counts nothing and writes
    no shadow.
  - Shadow miss: a real hit is NOT served (`g_forced` counts these). The computation runs counted and the
    shadow records it by the unlimited store rule.
  - Nested lookups inside a muted recompute (`InShadow()`): a real hit is served if there is one, otherwise
    they compute. They touch no shadow.
  - Real-cache stores are unchanged, including inside a muted recompute. This departs from the brief, which
    said nested unseen keys are not stored. It is safe because a real entry the shadow lacks is never served,
    so it cannot move a count.
- **Alias key builds** (kSimKey is priced):
  - In `BpEnumEntryFor`, the alias key is built only after the exact lookup. If the shadow served the exact
    key, the build is muted.
  - In `BpCanonFront`, the alias key is built when either cache lacks the exact key. The build is muted when
    the shadow has the exact key, because the unlimited world builds it only when it lacks that key.
- **m2 lazy shape.** For a counted m2 computation, the lazy enumeration choice follows the shadow's
  second-visit status rather than the real one.
- **When the ledger is off:**
  - The pool is unbounded (`MTG_PLAN_CACHE_KB=0`): the real cache already is the unlimited cache.
  - `MTG_CACHE_HIT_CHARGE != 1`, or `MTG_BP_ENUM_EPOCH` is set (both diagnostic modes).
  - `MTG_PLAN_PRICED_LEDGER=0` is the A/B control.
  - At X=0 nothing in play reads `t_priced`, so the ledger can only move wall time.
- **Stats.** `MTG_PLAN_LEDGER_STATS=1` (or `MTG_PLAN_POOL_STATS`) prints `[plan-ledger] on=.. muted_recomputes=..
  forced_misses=.. shadow_peak_keys=..` at exit.

## Verified
1. **Unit tests** (mini tree `build/scratch/ledger/ut`, built from this tree's data by
   `build/scratch/ledger/ut.sh`): **2443 / 2444 test cases passed, and all 3,568,616 assertions passed**. The
   single failure is environmental and unrelated to this change. `Pirates routing: every deck in decks/ ...`
   (`test/unit/test_pirates_provider.cpp:220`) walks the PRIMARY checkout's `decks/` and hit a filesystem error
   on `/workspaces/MagicDeckTester/decks/Merfolk`. To clear it, re-run with a `decks/` copy in the mini tree,
   or from a tree whose `decks/` is readable.
2. **plan5 probe** (`build/scratch/tier2/plan5.json`, X=100), default vs `MTG_PLAN_CACHE_KB=1`, run with
   `build/scratch/ledger/arm.sh`:
   - **5/5 jobs had identical digests, 0 win-turn differences and 0 unit differences.**
   - Win turns: dragonstorm gi200 T7, elementals gi121 T4, hinata2hg gi49 T5, goblins2hg gi30 T5,
     alchemist gi26 T5. These are the default-arm values the defect had moved.
   - Wall: KB=1 took 26x longer (54 s vs 1410 s summed game time). That is expected with the cache
     effectively off, and it is wall only.
   - Shadow peak: **20,416 keys per thread** across the 3 caches (64-bit key + 1 byte, roughly 1 MB of hash
     nodes).
   - Muted recomputes: 9,149 default, 429,510 KB=1. Forced misses: 130 default, 154 KB=1.
   - Outputs: `build/scratch/ledger/p5_def.{log,err}`, `p5_kb1.{log,err}`.

## Not yet run (next agent): tests 3-5
3. **Regression tier purity (the real gate).** Run both arms concurrently, each as one pooled batch with 16
   threads and `CAP=10` (two arms at a 20 GB cap could exceed the box's 23.5 GB):
   `CAP=10 build/scratch/ledger/arm.sh /workspaces/MagicDeckTester/build/wt/plan-priced-ledger/Release/mtg
   /workspaces/MagicDeckTester/build/scratch/tier2/reg_manifest.json build/scratch/ledger/reg_def 16`,
   and the same with prefix `reg_kb1` plus a trailing `MTG_PLAN_CACHE_KB=1`.
   - Compare with `scripts/cmp_runs.py <A> <B>`. The bar is 0 digest / 0 unit / 0 win-turn differences.
   - Expect the KB=1 arm to be slow: about 26x on plan-heavy games in the probe.
   - Manifest paths are repo-relative; `arm.sh` runs from this worktree.
4. **New binary vs parent (d00d39b0f) at default sizes.** A base worktree exists at
   `.claude/worktrees/plan-priced-ledger-base` (detached at d00d39b0f), but **its build FAILED** with 9p
   "Cannot allocate memory" (`build/scratch/ledger/base_build.log`). Re-run `./build.sh` there, check the
   exit code and the mtime of `build/Release/mtg`, then run the regression manifest with each binary and
   compare.
   - Do NOT reuse `.claude/worktrees/budget-rebalance/build/Release/mtg`: that tree's HEAD is now 2e64c6b5f
     (`MTG_MANA_CACHE_CHOSEN_KEY` default ON), so its binary is not the parent.
   - Expected differences: games where this box's default pool refuses or pressure-clears today. The probe's
     130 forced misses in the default arm show that the real caches do diverge from the unlimited world here.
   - Remove the base worktree afterwards with `scripts/safe_worktree_remove.sh`.
5. **Wall.** Report summed game wall for both arms of test 4.

## Risks to watch in test 3
- A muted recompute still writes OTHER memos (solvememo, TT, equip-route per-decision sets, mana cache) that
  the unlimited world never wrote. If any of those hits skips priced work later, a residual priced difference
  remains. This is the "priced side effect other than t_priced" case. If test 3 shows a few residual
  differences, check here first: suppress those memos' stores while `workctr::t_mute > 0`.
- Side counters (`BpSideReplay`: md, condemn, trunc, binds) in a muted recompute come from the recompute
  itself, not from the stored entry. They are equal if cachecharge's purity holds, which the earlier X=0
  evidence supports.
- `BpCanonFront`: when the unlimited world serves the full bp entry and the real cache serves the front entry
  (or the reverse), the replayed side counters come from different derivations. This predates the ledger.
- Open question for the coordinator: the FSL line-cache byte pool (`MTG_FSL_POOL`, also RAM-derived) was not
  examined. If its hits skip priced work, it is the same class of defect.
