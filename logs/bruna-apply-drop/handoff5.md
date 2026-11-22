# Bruna APPLY-DROP: handoff 5 (2026-11-22, agent 5 -> agent 6)

Read `handoff4.md` (history, USER rulings, box notes) first; this file supersedes its "remaining" list.
Worktree `.claude/worktrees/bruna-apply-drop`, branch `wip/bruna-apply-drop`. Scratch: `build/scratch/bad/`.
Per-arm comparison script: `build/scratch/bad/cmp_arms.py <wins_dir> <batch.out> <armA> <armB>` (net = A - B, unwon = 9).

## Done (agent 5)
- **d97ac2b14** (WIP): `MTG_AURA_HOLD_MEMO` now ENGAGES. 685d4eb75 skipped every `vial_in_order` walk, and every
  OrderPaysSequentially walk is one (EDF's range projection, `EdfRangeAurasBehindUntappers`, is 99.6% of the calls).
  Now only a walk whose `sel` holds a Treasure-laying Vial put bypasses. Also bypass counters (printed under
  `MTG_PAY_PROBE_MEMO_STATS`) and `kAuraMaxIds` 40 for the Aura table (was the shared 14).
  Same commit: **`MTG_EVAL_COMBAT_TOKEN_SELF`** lever (heurarm slot EVAL_COMBAT_TOKEN_SELF, DEFAULT OFF, USER call):
  EvalCard credits a creature whose own connection fires its combat-damage token maker (Spawning Kraken, Koma,
  Millicent) with tokens x token power x own attacks in the horizon.
- **fix5** binary (build/scratch/bad/fix5/, = d97ac2b14 minus the counters/kAuraMaxIds): Bruna units pass. Memo
  VERIFY: EDF d0 200 games 502,965 hits / 64,077 misses, EDF searched 664,406 / 447,802, Jumbo 15,033 / 17,699,
  Sea searched 388,582 / 340,272; VERIFY_MISMATCH 0 everywhere. Sea d0 / Jumbo d0: 0 memo calls (their helper
  calls come from another site -- see cg5 below). EDF d0 cpu on/off (300 paired games, one thread each, user time):
  **1.19 -> 1.081**. Token A/B (below): Sea searched fix cpu 1.023-1.026, Sea d0 1.006-1.009.

## Measurements (agent 5)
- **16 d0-extension slower games at d8 b0** (ab/m_d8_d0x.json, wins ab/wins_d8_d0x, fix3): **ALL RECOVER**
  (on == off turn): brunad0 2303 9381 13795 21770 30702 32848 39037 42006 42575 42727 43004 47501 48003 49382,
  sead0 26535 28282.
- **Bruna d0 gi21770** (seed 831770, T8 -> unwon): fix 1 alone causes it (f1 9 / f2 8 / off 8 / on 9). With fix 1
  the d0 runner plays Skycloud Expanse instead of Forest on T7 (casts Glittering Wish -> Bruna, not Bruna), and on T8
  the plan {Bruna + Arcanum Wings} APPLY-DROPs Wings 8 times (MTG_MANA_ORACLE=3: the oracle has a payment with
  Sage x3 creature-only W W U + Skycloud WU; MTG_PAY_ROLLBACK does not recover it). Recovers at d8 b0 (T3 both
  arms). Logs: build/scratch/bad/b21770/ (d_on_t7.err etc.). A residual APPLY-DROP class on the fix-1 path, not
  root-caused.
- **Sea d0 root cause, confirmed** (MTG_SOLVE_PROJ_TRACE, seatrace/): T5 gi5044 {Spawning Kraken + Utopia Sprawl}
  and {Serpent of Yawning Depths + Sprawl} both score eval 700 (each six-drop 600 = 6 x 1 attack x 100; EvalCard
  never sees Kraken's 9/9 trigger); the tie goes to the lower subset mask (Serpent). Off arm: Serpent APPLY-DROPs
  (unconditional host hold), the fallback casts Kraken. gi26535 / gi28282 are the same (Serpent / Summon: Leviathan
  chosen over Kraken). Defect (a) (held filter split floats G) does not change these games.
- **Token lever A/B** (tok/, fix5, seed base 1410000; 4 Sea arms fNtM = fixes N, lever M; logs agent5/tok_ab.txt):
  | compare | sea 4000 | sea2hg 1000 | sead0 60000 | spirits 2000 | spiritsd0 20000 |
  |---|---|---|---|---|---|
  | fixes on vs off, lever off | 0/0 0 | 0/0 0 | 0/0 0 | | |
  | fixes on vs off, lever on | 0/0 0 | 0/0 0 | 0/0 0 | | |
  | lever on vs off, fixes on | 0/0 0 | 2/0 -2 | **1841/114 -1928** | 0/0 0 | 0/1 +1 |
  (faster/slower net). On these fresh seeds the fixes are neutral for Sea in every format.
- **7a / 7b all-deck held-out A/B** (ro_ab/, binary ro2 = fix3 + crit levers, REGRESSION shapes, seed base 1310000,
  200 games per searched shape, 2000 per d0 shape; logs agent5/cmp_a.txt, cmp_b.txt). REPORT ONLY.
  7a (MTG_PAY_LINE_CRIT_BEHIND_DORKS): ALL 15 faster / 16 slower, **+3**. Worse: jumbo d0 +6 (0/4), dragonstorm d0 +3,
  d3 +1, d5 +1, cyclone d3 +1, d5 +1, stompy d0 +1. Better: dragons d0 -4, bruna d0 -2, elementals d0 -2, alchemist
  d3 -1, fivecolour d0 -1, selesnya d0 -1.
  7b (MTG_PAY_LINE_CRIT_LAND + _OPENER): ALL 12 / 14, **+6**. Worse: **niv d0 +13 (1/11)**. Better: dragons d0 -4,
  elementals d0 -2, niv2hg d5 -1; bruna d0, cyclone d0, hinata d0 net 0.

## chain1 results (DONE 09:4x UTC; agent5/chain1.txt)
- **tok2: with the lever ON, fixes on vs off on the OLD Sea d0 seeds** (710000 x10k, 810000 x50k): **0 faster / 0
  slower, net 0** (the +3 is gone). So with MTG_EVAL_COMBAT_TOKEN_SELF on, the fixes are net 0 on Sea d0 over 120k
  pairs; without it, +3 over 120k (0 faster).
- **The lever's 115 slower games at d8 b0** (114 sead0 + spirits d0 gi2341, seeds 1410000+gi): **ALL RECOVER**.
- **Callgrind cg5 (fix5)**: EDF d0 200 games (seed 710000) 332.7G Ir vs fix3-off 624.1G/400 (~312G/200): ~1.066.
  AuraHostHoldKeepsLine still 5.54% (18.4G): 881,121 calls from SeqWalkFixedOrder (296,527 scratch payments 13.2G,
  160,727 LineBillPayable 3.2G), 2.5M from SubsetPayableWithFiltersImpl (0.31G, prefilter rejects). Sea d0 2000
  games: helper 1.47% (SPWF 1.04%, CastSpellFromHand 0.43%); Sea d0 cpu ratio 1.006-1.015 in the A/Bs.
- **Rejected idea (do not retry):** keying the Aura memo on the CURRENT g_line_unpaid_cost instead of the walk's
  starting total, to share verdicts across subsets. Unsound: the prefix casts are PAID under the line hold (tap-ahead
  line-priority commit), so the board at the Aura step depends on the whole order's total, and SubManaCost clamps, so
  the current hold does not determine the total.

## Running / queued (agent 5's; none USER-requested)
- **build_fix6.sh under the box lock** (`flock claude_box.lock env MTG_RSS_CAP_GB=40 build/scratch/bad/build_fix6.sh`,
  waiting behind another session's job at 10:00 UTC): full-core build of d97ac2b14 -> fix6/, Bruna units,
  VERIFY+STATS runs (edfd0 200, jumbod0 1000, sead0 2000 at 8 threads) printing the new bypass counters
  (`[pay-memo] AuraHostHoldKeepsLine bypass: ...` in fix6/verify/*.err), then paired CPU screens edfd0 300 / sead0
  30000 / jumbod0 10000 (fix6/cpu/*.time: user, wall). Log build/scratch/bad/build_fix6.log (`ALLDONE`).
  NOTE: the d97ac2b14 tree is what it builds; src is clean at d97ac2b14 (a reverted edit touched fix_edit_ts).

## What remains, in order
1. Read fix6 bypass counters (881K unmemoised SeqWalk calls per 200 EDF d0 games: misses or bypass?) + cg5 (who still calls AuraHostHoldKeepsLine unmemoised; Sea/Jumbo d0 call it
   from a non-OPS site, likely SubsetPayableWithFiltersImpl or the SPS walk outside a scope). Cut it (exact), rebuild,
   re-screen. Then a CPU/identity A/B: `ab/run_cpu5.sh <bin> <nproc> 40 cpu6` (manifest_cpu5.json = land-Aura decks
   + cheap d0, seeds = ab3's) under the box lock; on__ digests must equal ab3's, cpu on/off near noise.
2. Sea d0 bar: met ONLY with MTG_EVAL_COMBAT_TOKEN_SELF on (a USER decision). Without it +3 / 120k. Do not accept
   GT until the USER rules (or the CPU work is also done and the USER accepts Sea d0 +3).
3. GT step (handoff4 step 5) once the bar is met or the USER rules on Sea d0.

## USER questions (surface, do not settle)
- Sea d0 (+3 over 120k pairs, 0 faster): the cause is an exact EvalCard tie between Kraken and the UU six-drops.
  Proposed fix MTG_EVAL_COMBAT_TOKEN_SELF (default OFF): Sea d0 -1928 / 60k (1841 faster, 114 slower), sea2hg -2,
  searched 0, spirits d0 +1 (1 slower). Adopt?
- 7a +3 / 7b +6 all-deck (numbers above). Both break standing rules; the USER decides.

## USER rulings (verbatim, carried from the briefs)
See handoff4.md "Brief rulings" and "Agent-4 brief additions" (all still binding). Agent-5 brief additions:
"Box: big runs (A/Bs, d8 b0 sets of more than a few games, callgrind of a whole batch) go on the box lock, ONE at a
time ... Small work (single games, units, a worktree build) goes in the lane ... with N <= 2, ALWAYS. Do not run
diagnostics inline outside the lane." / "A cast-order or keep change for Sea is a USER-owned policy, so if the only
fix is a Sea cast-order rule, build it default OFF, measure it, and report it for the USER instead of adopting it." /
"The bar is every deck net <= 0 ("+1 is not neutral. 0 is neutral."), so do NOT treat it as acceptable." /
"Rule-override levers 7a ... and 7b ... break standing rules, so the USER decides; report the numbers and do NOT
adopt." / "USER: cut CPU; 3x is not a CPU licence. Prefer exact cuts (identical play)."
