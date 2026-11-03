# Tier-2 overflow -- HANDOFF (2026-11-03 ~10:55 UTC)

Branch `tier2-overflow`, worktree `.claude/worktrees/tier2-overflow`, cut from `budget-rebalance` a18ae02bf.
Not pushed. Full write-up: `docs/design/tier2-cache-overflow.md` (read it first). Scratch:
`/workspaces/MagicDeckTester/build/scratch/tier2/`. NOTHING is in flight.

## Goal (USER brief via coordinator)
When a fixed per-thread tier-1 cache fills, overflow into a RAM-sized, shared-budget tier 2 instead of losing
entries; a tier-2 hit must charge exactly what a recompute would, so tier-2 residency never moves play.

## Built (commits, oldest first; a patch series on a18ae02bf; none touches ManaOracle.cpp or the keep-table cache)
- 2098054aa `src/core/Tier2Overflow.h/.cpp` (per-thread `tier2::Overflow<Map,Bytes>` over the consumer's map,
  node-handle spill; ONE shared `tier2::Pool` byte budget = `membudget::DefaultTier2OverflowKb()` = min(workers x
  32 MB, cache_mb/8) ~502 MB here, own slice apart from the plan pool; `[tier2-overflow]` exit line, `t2=`
  heartbeat token), `MemBudget.*`, CMake, `test/unit/test_tier2_overflow.cpp` (5 cases). Mana cache
  (`SpellEffects.cpp`): wholesale clear -> spill (`McTier1Clear`), tier-1 miss -> tier-2 lookup -> PROMOTE via
  the shared store `McTier1Insert`; entries record pre-solve payer context + plan traits, tier-2 serves only on a
  match. Flags: `MTG_TIER2_OVERFLOW` (default ON, =0 old behaviour), `MTG_TIER2_OVERFLOW_KB` (0 = unbounded).
- 776c09830 tier-2 match also folds (index, chosen_color) of the active player's permanents (`McT2PayWord`).
- 71a656d57 `MTG_TIER2_MANA_CTX=0` diagnostic arm (drops payer-context/traits halves, keeps the board fold).
- 0c68706a8 `MTG_MANA_CACHE_CHOSEN_KEY` (default OFF): same fold into the TIER-1 key (play change, user's call).
- (this commit) docs.

## Verified (regression tier = 264 jobs / 75,244 games, one pooled batch per arm; jobcmp.py = digest + unit total per job)
- Purity (mana tier-1 1 MB, tier-2 KB=1 vs unbounded, binary 71a656d57 = t2c.mtg): 264/264 jobs identical in
  digest and units; 41,763 per-game rows, 0 win-turn / 0 unit diffs. (First attempt without the chosen-colour
  fold: 7 games' units differed -- fixed.)
- Folds needed: `MTG_TIER2_MANA_CTX=0` -> 2 Niv games' units differ (niv d5 s2002 gi135, s3003 gi140).
- Isolation at default sizes: a18ae02bf-equivalent (base.mtg + MTG_TIER1_MANA_MB=24) == OVERFLOW=0 == default:
  264/264 jobs, 0 diffs; per game 0 / 0. Only Snow d5 s2002 gi80 overflows at defaults (tier-2 peak 65.4 MB).
- Unit tests 2,436/2,436 (run the test binary with data reachable by walking up from the EXE; see the doc).
- Wall: within noise (Snow gi80 ~ -5%..0%; tier 1.2% either way). Memory: +56 MB RSS on gi80.

## Unresolved (surface to the USER)
1. TIER-1 mana key hole: `chosen_color` (Coldsteel Heart / Utopia Sprawl) not in the key; Snow d5 s3003 gi27
   has 292 wrong tier-1 hits at defaults; mana allocation therefore moves Snow units (gi76: 16 MB 898,452 vs
   24 MB 898,450), contrary to a18ae02bf's note. Lever `MTG_MANA_CACHE_CHOSEN_KEY=1` closes it -> needs an A/B
   on Snow + AccomplishedAlchemist and the user's adoption call.
2. PLAN CACHES ARE IMPURE under the X=100 priced ceiling (details in the doc's last section): default vs
   `MTG_PLAN_CACHE_KB=1` -> 6 digests, 3 win turns, 14 games' units; at `MTG_DECISION_PRICED_X=0` the 5 probed
   games are identical (manifest `build/scratch/tier2/plan5.json`, outputs `p5_x*_kb*.err`).
3. m1memo / fsl / tt / gamelc NOT wired (a free hit's recompute cost is warm-cache dependent; replaying stored
   units = cachecharge mode 2, measured overcharge). solvememo unchanged. Reasons in the doc.

## Next steps
- Coordinator: cherry-pick 2098054aa..HEAD onto budget-rebalance (rebased on origin 12a476cda); rebuild; smoke.
- Fix item 2: store per-kind `workctr::t_n` deltas in `cachecharge::Cost`, replay on `CacheHitCharge`; rerun
  default vs `MTG_PLAN_CACHE_KB=1` on the regression tier (expect 0 diffs) -- i1/pk1 logs in scratch are the
  "before".
- Item 1: A/B `MTG_MANA_CACHE_CHOSEN_KEY` (held-out seeds, Snow + Alchemist), report to the USER.
