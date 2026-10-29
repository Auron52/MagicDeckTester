# Canonical no-win sharing is not order-free (found 2026-10-29, deferred to the USER)

## What

`FSLineWin` stores and serves NO-WIN answers under the CANONICAL sim key (`MTG_CANON_SIMKEY`: hand and
battlefield folded as multisets), in both the per-decision `FSLineCache` and the game-persistent line cache
(`MTG_GAME_LINE_CACHE`). Only WIN lines carry the order-exact key (`MTG_FSL_SPLIT_KEYS`: `key_win` folds
`FsOrderSig`). The lookup states the premise: *"NO-WIN on the canonical key: order-free and sound (no line;
the bound is a property of the multiset)"*.

The premise is false whenever the search itself is order-dependent, and it is: the enumeration emits
candidates in hand / battlefield order, heuristic picks break ties by vector position (the sacrifice victim,
the canon continuation), and a breakpoint continuation's RANK -- which decides what the one-deviation walk and
the uniform arm can reach -- follows the same order. So a zone-permuted twin can have no win in reach while
this position has one, and the twin's refutation is served here.

## The witness

Goblins seed 1330145 gi144, `--depth 8 --budget-ms 0`, `MTG_SUBSET_SAC_OUTLETS=1 MTG_BP_DEV_SEQ=0`
(`MTG_PAY_REFINE=1`): T4. Each of these alone restores T3:

* `MTG_CANON_SIMKEY=0`;
* `MTG_GAME_LINE_CACHE=0 MTG_FS_NOWIN_CACHE=0` together (either one alone: still T4 -- each cache serves the
  same stale refutation);
* an experiment that keys the no-win entries order-exactly (`key = key_win` in `FSLineWin`, both caches).

The T3 line those find uses no breakpoint deviation at all: T1 Mountain + Goblin Lackey; T2 Mountain + Goblin
Lackey, main 2 Skirk Prospector; T3 Mountain + Rundvelt Hordemaster, main 2 Siege-Gang activation + Skirk
sacrifices. With the order-exact keys the T3 node under (T1 Lackey; T2 Lackey; main 2 Skirk) answers T3
(`MTG_M2T_TRACE` `sub=3`); with the canonical ones it answers no-win (`sub=9`). The twin is presumably the
(T2 Mountain + Skirk + Lackey) branch, which reaches the same multiset with Skirk ahead of the second Lackey.

(`MTG_BP_DEV_SEQ`, which ships ON, recovers this game by another line, so the game no longer shows it. The
hole is independent of that lever.)

## Options (the USER's call -- each moves budgeted play)

1. **Order-exact no-win keys** (fold `FsOrderSig` into the no-win key of both caches). Sound by construction;
   costs the permutation sharing the canonical key was adopted for (2026-08-14, `MTG_CANON_SIMKEY`: treasure_hunt
   x12/ply sequences). Needs a held-out A/B and a units measurement.
2. **Make the search order-invariant** where it feeds a no-win: canonical tie-breaks for victim picks and
   continuation ranks. Larger, and every site has to be found.
3. **Verify instrument first**: `MTG_GAME_LINE_CACHE_VERIFY=1` had no power on this game (all 267 no-win hits
   early-returned before verification), and `MTG_NOWIN_VERIFY` covers only the rollout TT. A per-decision
   `FSLineCache` no-win verify (re-search a hit fresh, compare) would measure how often this fires.
