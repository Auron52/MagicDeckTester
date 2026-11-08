# Handoff: Dragons mana-clean fixes (branch wip/mc-dragons)

## 2026-11-08 validation update (Codex)

The candidate at `25d75bf49` was validated while the three gi2535 tails remained CPU active. Do not cancel those
measurements: `hostfix-ab.json` still has 65/66 jobs (final `edf_on_2400`), and the pre-patch price18/price4 single-game
controls are still running. The separate seven-slower-game d8/b0 batch finished: EDF2HG gi926 recovers to T8 on both
arms with identical digest, as did the five previously reported Standard games; Standard gi2069 is T7 on both arms
with different digests. The shipped-setting gi926 slowdown remains a budget-sensitive plan choice.

The focused find-side regression is now `test/unit/test_line_repay.cpp`, "a held Aura host remains findable for a
previous payment". A Forest pays the first {G}; Razorverge Thicket is then temporarily held as the second Aura's
host, leaving an untapped Plains but no available green. The previous payment's record is found only when the held
host is read as untapped; it is re-made on Thicket, freeing Forest for the second {G}, and the Thicket's real tap is
preserved. The same board without the held-host scope cannot repay. Focused test: 61/61 assertions; full suite:
2,438/2,438 tests and 3,568,575/3,568,575 assertions (`/tmp/dragon-host-units.log`). This covers the find-side
case that the earlier gi323 unit (record-side) did not.

Completed independent checks on this candidate:

* Dragons mana oracle gate, same 10,000-game slice as the November 3 base: `gate_hostfix32.{out,err}` (32 threads),
  exit 0, ENUM-GAP 1 / APPLY-DROP 142 / ORACLE-GAP 6 = 149 flags. Base `gate_base.err` had 247 / 564 / 316 = 1,127;
  the earlier WIP `gate_new.err` had 1 / 142 / 28 = 171. The old base output is reused, not a new base run.
* Fresh paired host-path A/B, 600 games each arm for Fungus and Accomplished Alchemist, seeds 2880001 and 2890001,
  `lrprice/hostfix-aura-ab.{json,out,err}` plus `hostfix-aura-ab-wins/`: Fungus 600/600 paired, zero play digests and
  zero win-turn changes; Alchemist 600/600 paired, two play digests, zero win-turn changes. Both used 32 threads.
* Scoped smoke on Dragons, EDF/EDF2HG, Bruna, Alchemist, Fungus: `lrprice/hostfix-smoke.log`. 392 scenarios pass.
  Against shipped ground truth, one searched slowdown (Alchemist gi99 T5->T6) recovers to T5 at 4x and 16x budget
  (`lrprice/hostfix-smoke-classify.log`); one searched faster, 12 searched play changes at equal score. No GT accepted.
* Scoped regression on the same decks: `lrprice/hostfix-regression.log`. Searched games: zero slower, two faster,
  26 play changes at equal score. Strict reference replay: 747 refs, 288 ok / 458 repaired / one beaten, zero play
  drift, board divergence, enum gap, shuffle dead, mull drift, or contract fail. Both suboptimal references beaten.
  No GT accepted, per the worktree brief. The run returns mismatch against current shipped GT as expected for WIP.

Still required before landing: let the three gi2535 measurements finish and inspect their paired result; review the remaining smoke/regression play
changes and the gate residue; update `mana-completeness-oracle.md` with final measurements. Branch adoption waits on
the final EDF A/B tail and the test, not on another broad CPU run. The remaining tails are effectively serial, so
their low total CPU utilization does not mean a ready validation run was left unstarted.

## Successor 12 (2026-11-07, in progress): find-side Aura host hold

The prior branch commit `e8ae5aa3b` prices LINE_REPAY at 18 probes/unit (measured 3.95 us/probe versus 71.15 us/ordinary unit;
900-game price A/B no win-turn changes, held-out 1,500 EDF net two turns faster). This candidate edit in
`ManaPayment.cpp/.h`, `AIEngine.cpp`, and `TurnSolver.cpp` virtually untaps an Aura's temporarily held host for the
initial LineRepayFind digest, leaves earlier re-payments free to use the host, and taps it only for the current Aura's
payment. Every Aura cast site scopes the held host; `LineRepayHostUntap` preserves a real earlier-payment tap.
`./build.sh` passed; full units 2437/2437. Scratch: `build/scratch/mcdragons/lrprice/`.

The four saved slower games at both shipped settings and unlimited d8/b0: standard EDF gi3280, gi3295, gi4144 now
match the off arm's T5/T4/T4 win and play digest. Pre-patch at the same price18 still takes T6/T5/T5. EDF2HG gi1194
remains T5 on versus T4 off, byte-identical to pre-patch at d8/b0. Its on arm commits a T1-predicted T4 line through
Living Wish/Emiel, then the T4 breakpoint continuation casts Wish/Cloud/Trace and wins T5; the off arm commits a
Trace-first line and wins T4 (`host-1194-{on,off}-fd.err`). The old hold mismatch does not explain that fourth game.

Fresh pooled A/B `hostfix-ab.json` (seed 2860001, 5,000 EDF Standard; seed 2870001, 1,500 EDF2HG; price18) is still
running. At 4,800 Standard pairs: 6 slower / 5 faster, net zero turns; 1,500 2HG: 1 slower / 1 faster, net zero.
The final Standard on-arm chunk `edf_on_2400` is a CPU-active tail at gi2535 (>10 minutes; off gi2535 took 33 s).
Do not cancel this user-requested measurement. A 15 s perf sample `hostfix-gi2535.perf.data` is dominated by plan
key construction, copies and payer walks, with LINE_REPAY a smaller direct share. Pre-patch price18 and price4
single-game controls (`hostfix-2535-base.json`) are running to attribute the tail. Unlimited d8/b0 replays of the seven fresh-seed
slower games (`hostfix-slow-d8.json`) are running: standard gi812, gi1312, gi2627, gi359, gi4575 recovered to the
off arm's win turn and digest; standard gi2069 wins T7 in both arms (different digests), and 2HG gi926 remains in flight. Decide on the branch only after these tails
finish; then add a focused find-side test, smoke/scoped regression, and a mana-clean gate slice before landing.

The shipped-budget 2HG gi926 slowdown is T8 -> unwon, with the first different plan on T1 (off plays Yavimaya Coast
and Training Grounds; on plays Kitchen only). It is byte-identical on the pre-host-fix binary at both 4 and 18
probes/unit (`hostfix-926-prepatch.out`, `hostfix-926-price4.out`), so neither the host patch nor probe price caused
it. At d5 b80 and b320 both arms win T8 (`hostfix-926-budget.out`); this is a budget-sensitive plan choice, not an
Aura payment mismatch. The high-depth d8/b0 replay is still in flight.

Task (from the coordinator): fix the mana defects the mana-clean gate found on Dragons
(`/workspaces/MagicDeckTester/logs/mana_clean/pass1-dragons/report.md`: 71,000 games, ENUM-GAP 1,691, APPLY-DROP
4,164, ORACLE-GAP 3,074), measure on a fresh 10k slice with both binaries, A/B the play change, unit test + scenario +
doc section. Do NOT accept GT and do NOT push to phase-1-2-deck-analyzer; `git push origin wip/mc-dragons` is allowed.
Full write-up: `docs/design/mana-completeness-oracle.md` section 14 (drafted; numbers below still to be added there).

Worktree: `/workspaces/MagicDeckTester/.claude/worktrees/agent-a9dab9fbcae918660`, branch `wip/mc-dragons`, based on
origin/phase-1-2-deck-analyzer 980616e55. Scratch: `/workspaces/MagicDeckTester/build/scratch/mcdragons/`. Helper
scripts (gitignored): `logs/mcdragons/{mk.py,gate.sh,batch.sh,repro.sh,abrep.py,scprobe.sh}`.

## READ FIRST: Successor 11 (2026-11-04) -- host-untap fix VERIFIED on 323/365/935; larger EDF A/B still FAILS (+11 / +3)
Scratch `build/scratch/mcdragons11/` (binaries `mtg.p13`/`mtg-test.p13` = 3d0b1f9ba with traces, `mtg.p14`/`mtg-test.p14`
= 3eedb3e0c, traces removed; `src`/`decks` symlinks there point at this worktree -- see TEST-PATH TRAP below).
DONE:
  * Unit suite p13 and p14: 2437/2437. New test "a land Aura paid under its host hold can still be re-made (EDF gi323
    T2)" FAILS with the fix reverted (`mtg-test.rev`, CHECK(line_pays(true) == 1) 0 == 1) and passes with it.
  * 323 / 365 / 935 (`mcdragons11/d8`, d8 b0): off 3/4/4, on 3/4/3 (was 4/5/5). Shipped settings (`ship/`): off
    3/4/4, on 3/4/3. Executor now fires LR on 323 T2 (`tr323.err`: [lr-fire] after "before Eldrazi Displacer", ED cast).
  * 3eedb3e0c: TEMP diagnostics removed (MTG_LINE_REPAY_TRACE, MTG_EDF_ROUTE_SIGDUMP). p14 == p13 digests+units on 13
    jobs (1-thread vs 8-thread too). Price lever lr_probes_per_unit still in place (USER).
  * A/B ab11 (`mcdragons11/ab11`, ONE pooled batch p13, 640 jobs, 8 threads, shipped settings, off vs on@4):
      deck     paired dig slow fast net   units  wall(contended)
      bruna      1500  33    0    2  -2  1.0146  0.9906   (seed 2870001+off; only ab9 deck with Wild Growth)
      edf        5000 195   13    2 +11  1.2681  1.1965   (seed 2810001+off)
      edf2hg     1500  61    4    1  +3  1.2460  1.1508   (seed 2830001+off, starting_life 30, opponent_heads 2)
    instructions:u (perf stat, p14, 1 thread, same jobs per arm; ab11/ins_*.perf): EDF 100 g on/off 2.288x
    (units 1.325x), EDF2HG 50 g 1.919x (units 1.561x), Bruna 50 g 1.065x (units 1.033x).
  * Slower-game recovery at d8 b0 (`d8s`, p14): 13 of 17 recover (identical digest). NOT recovered plain: edf 3280
    (5->6), 3295 (4->5), 4144 (4->5), edf2hg 1194 (4->5). With MTG_NC_SEARCH=1 (`d8nc`): 3280 5/5, 3295 4/4,
    4144 4/4, 1194 4/4 -- all four recover (identical digests on/off; NC on wall 1.6-2.3x off). By the memo test they are NOT clear clairvoyance.
ROOT CAUSE CANDIDATE for the 4 (traces `g3295/`, `gx/`, p13 + MTG_EXEC_TAP_TRACE + MTG_LINE_REPAY_TRACE): every one
  casts several land Auras (Fertile Ground / Trace of Abundance / Overgrowth / Wild Growth) in the decisive turn and the
  executor ends with every land tapped, so Eldrazi Displacer's {2}{C} / {C} blink fails (line_ctx=0) and the combo
  never starts. On 3280 T5 the executor's Overgrowth payment misses LR with find=null: the HOLD on Overgrowth's host
  (Conservatory, also Fertile Ground's host) taps it BEFORE the payment, so the board no longer matches the previous
  record's post (ring-1 Fertile Ground post 78fbbbea.. vs now 7b0ad7ca..; the printed perms are identical except the
  held host). It is the INVERSE of successor 10's defect: the hold perturbs the digest at the FIND, not at the
  RECORD. The executor then pays Overgrowth with its own host after the release, consuming the mana the search kept
  for the blink. Not yet fixed or tested. Candidate fix: LineRepayFind/LineRepayDigest read a currently-held host as
  untapped (thread_local held-host number set across the hold), keeping the host unavailable to the re-made payment.
TEST-PATH TRAP (new): mtg-test resolves src/cards/data/cards.json and decks/ by walking up from the EXECUTABLE
  (ResolveHeuristicDefaultsPath); a binary under /workspaces/MagicDeckTester/build/... finds the PRIMARY checkout's
  cards.json (has "Morbid" -> 5 Line repay tests throw) and decks/ (45 folders -> Pirates routing fails). Run test
  binaries from `mcdragons11/` (symlinks src, decks -> this worktree). mtg itself reads cards.json CWD-relative (ok).
REMAINING before LINE_REPAY could land: (1) fix + unit-test the find-side hold mismatch, re-run d8s on the 4, then
  a fresh-seed EDF A/B (std + 2HG); EDF must be net <= 0; (2) USER: price (4 vs 12) and pricing pass 1 -- EDF on costs
  1.27x units / ~2x instructions; (3) remove lr_probes_per_unit or replace the constant; (4) smoke, scoped regression,
  mana-clean gate slice, squash. Fungus (3 Wild Growth) and AccomplishedAlchemist (Utopia Sprawl, Wild Growth) also
  take the host-hold path and were not in any LR A/B.

## READ FIRST: Successor 10 (2026-11-04) -- EDF slower games ROOT-CAUSED: a lockstep defect, fix built, NOT yet measured
Stopped at the 250K context cap. Scratch `build/scratch/mcdragons10/` (diag binaries `mtg.diag1..4` = p12 + traces).
ROOT CAUSE (all three EDF slower games, 323 / 365 / 935; class (a) LOCKSTEP, not clairvoyance, not budget):
  The land-Aura HOST HOLD taps the Aura's declared host across the Aura's own payment and untaps it right after, at
  all four cast sites: executor `src/ai/AIEngine.cpp:~9952-9982` (RESERVE THE AURA'S OWN HOST), ApplyPlanDirect
  apply_one `src/ai/TurnSolver.cpp:~61430-61464`, and the two payability walks `TurnSolver.cpp:~10153-10179` and
  `~10871-10890`. LINE_REPAY records the payment INSIDE TapForCostShared (`ManaPayment.cpp` LineRepayRecord, called
  at the end of TapForCostShared), i.e. while the host is still held tapped: the record's `post` digest and `pre`
  board have the host TAPPED. The caller then untaps it, so no later board matches that record and LineRepayFind
  cannot re-make the Aura's payment. The search still credits such lines from records its other paths pushed
  without the hold (9 such records at t2 on gi323), so it commits a line the executor cannot pay.
  Evidence (gi323 on, d8 b0, `mcdragons10/s323/lb1.err`): the executor's record of Wild Growth's {G} shows
  `35T` (Mariposa held), the Displacer's miss shows `35` untapped; ring-1 post 5955553aad5c7c40 vs now
  5955553aa8360c02, so `find=null` and Eldrazi Displacer {2}{W} is dropped. In the search LR fired 4x for the same
  ED payment (`[lr-fire] t2 p1 depth=1 failing={2}{W} for=Eldrazi Displacer rec0={G}(Wild Growth)`).
  Per game (FD trace `MTG_FD_TRACE=1`, `mcdragons10/s<gi>/fd1.err`; executor taps `MTG_EXEC_TAP_TRACE=1`):
   * 323 (seed 1870324): T1 line win=3 with T2 = Mariposa + Eldrazi Displacer + Wild Growth(on Mariposa). Legal with
     4 mana (Brushland {W}+old WG {G}: {G} pays the new WG, {W} floats; Mariposa {C}+new WG {G}+{W} pay ED). Executor
     taps Brushland for {G}, ED dropped -> T3 Drake, win T4 (off arm wins T3 on another line).
   * 365 (seed 1870366): T3 line = Mariposa + ED + WG; same shape on Conservatory; executor drops ED -> win T5 not T4.
   * 935 (seed 1880936): T2 line = Mariposa + Training Grounds + Living Wish + WG; executor drops Training Grounds
     ({U}) -> T3 "Combo Off" is a no-op (the route's T3 WIN needs Training Grounds in play) -> win T5 not T4.
  The d8 b0 games play the same decisive lines as shipped settings (mcdragons10/d8, digests in d8/w/*.wins), and
  the off arm never chooses these lines because without LR the walk finds them unpayable.
FIX BUILT (in the WIP commit carrying this section; NOT yet compiled into a tested binary, NOT measured):
  `LineRepayMark()` + `LineRepayHostUntap(s, mark, host)` (ManaPayment.cpp, before TapForCostShared; decl
  ManaPayment.h above "THE SCARCE HOLD OUTRANKS"): replaces the bare `SetPermTapped(..., reserved_host, false)` at
  all four sites. It untaps the host and, if exactly one record was pushed since `mark` (new thread_local counter
  `t_lr_pushes`) and its digest still reads the held board, re-states that record on the released board: `post`
  recomputed, `pre.bf[host].tapped = false`, `content` recomputed (LineRepayContentOf, factored out of
  LineRepayRecord). Unit test added: test/unit/test_line_repay.cpp "a land Aura paid under its host hold can still
  be re-made (EDF gi323 T2)" (ApplyPlan of {WG on Mariposa, ED}: on == both cast; off != both). UNRUN.
  TEMPORARY DIAGNOSTICS in the same commit (env-gated, REMOVE before landing): ManaPayment.cpp LrTraceFireTmp
  ([lr-fire]), LrBoardDumpTmp ([lr-board], record + miss), the [lr-miss] block before the LR hook in
  TapForCostShared (all MTG_LINE_REPAY_TRACE=1); DecisionProviders.cpp EdfSigDumpTmp (MTG_EDF_ROUTE_SIGDUMP=1).
  The p12 TEMP lever lr_probes_per_unit is still there too (remove or USER-priced).
RUNNING: nothing. build5 FINISHED OK (`build/scratch/mcdragons10/build5.log`, 82/82, "done:") -- this worktree's
  build/Release/{mtg,mtg-test} now hold the committed fix + the TEMP traces. Copy it aside before testing
  (e.g. `cp build/Release/mtg build/scratch/mcdragons10/mtg.fix1`).
REMAINING, IN ORDER:
  1. When build5 is done: `build/Release/mtg-test -tc="Line repay*"` then the whole unit suite. If the new test's
     off-arm CHECK is wrong (the menu may not offer the line), adjust the test, not the fix.
  2. Re-measure 323 / 365 / 935 at d8 b0 both arms: manifest `mcdragons10/d8/m.json` (one pooled batch, 6 jobs,
     `--game-trace-dir`), expect on == off (3/4/4). Also shipped settings (single games, seeds above). Confirm the
     executor now fires LR (`MTG_LINE_REPAY_TRACE=1`, look for [lr-fire] after `[exec-tap] T2 before Eldrazi`).
  3. Remove the TEMPORARY diagnostics, rebuild, re-run the unit suite.
  4. The larger held-out EDF A/B (USER: "If it is recoverable I would be curious to see if this works better on a
     larger set."): LINE_REPAY off vs on, price 4 unchanged, ONE pooled `mtg --batch`, THREADS <= 8, shipped play
     settings (no depth/budget in the jobs). Fresh seed blocks (checked unused across build/scratch manifests and the
     suite): EDF standard 5,000 games seed = 2810001 + off, game_index = off; EDF 2HG 1,500 games seed = 2830001 + off
     (2HG job keys: "starting_life": 30, "opponent_heads": 2 -- test/regression.sh:361). Pattern: mcdragons9/ab9/mk.py
     + rep.py. Report net, slower/faster, units; instructions:u by `perf stat -e instructions:u` on a fixed per-arm
     single-thread sample (e.g. 100 games each arm), MTG_PGO irrelevant (run the binary directly). Every slower game
     must recover at d8 b0; only clear clairvoyance is exempt (MTG_NC_SEARCH=1 test).
  Still open for the USER (unchanged): LR price (4 vs 12), pricing pass 1.

## READ FIRST: Successor 9 (2026-11-04) -- LINE PARKED FOR THE USER (stop rule hit on EDF)
Worktree head = the commit carrying this section (wip/mc-dragons, NOT pushed, no GT accepted, no smoke run).
Scratch `build/scratch/mcdragons9/`. Binary under test = `mcdragons9/mtg.p12` / `mtg-test.p12` (= HEAD src).
WHAT P12 CHANGED (on top of p11):
  * `LineRepayClearPerGame()` (ManaPayment.cpp), called from TurnSolver::ClearPerGameCaches: resets t_lr_ring (all 16
    records), t_lr_next and t_lr_memo. KEEP THIS -- it is a correctness/determinism fix in principle (the ring is found
    by content, so an earlier game's record could be re-made from the other game's pre-board).
  * TEMPORARY lever `lr_probes_per_unit` (manifest key; ValueArm.h field; env MTG_LR_PROBES_PER_UNIT) overriding
    kLineRepayProbesPerUnit = 4. REMOVE before landing (or replace the constant if the USER picks a price).
DETERMINISM (task 1): the suspected ring leak is NOT observed. p11 1-thread (`mcdragons8/id11s`) == p11 4-thread
  (`mcdragons8/id11`) on all 20 id jobs, digests AND units; the 8 unit diffs vs p7 are p11's own charging change
  (deterministic). p12 1-thread (`id12s`) == p11 1-thread (20/20 digests+units: the clear is play/unit-neutral there),
  and p12 1-thread == p12 8-thread (`id12m`) 20/20 digests AND units. Units p12 2,436/2,436 (`ut12.log`).
EDF +15% UNITS -- ROOT CAUSE: the priced pass 2's full table at depth 1 ("full1"). ab7 job off25 (25 g, p11,
  MTG_LINE_REPAY_STATS): full1 1,818 entries / 219,685 probes (wf2 3,649, full2 20,000), found_p2 = 0, p2_cut 164;
  243,334 probes / 4 per unit = ~61k units = the whole +58.8k units delta (133,391 -> 192,208). p2_ms 804 ms ->
  3.3 us a probe on EDF, i.e. the price (4 probes a unit, against 40-77 us a unit) over-charges EDF ~3-6x. Pass 1
  (unpriced) is where EDF's re-pays are found (found_p1 182 / 25 g; game 935 alone fired 474, found_p1 326).
A/B ab9 (p12, held-out seed block 1880001, ONE pooled batch, arms off / on@4 / on@12; `mcdragons9/ab9/report.txt`):
  deck     arm   paired dig slow fast net  units   wall
  bruna    on      900   15   0    2  -2  1.0102  1.0038 | on12  900  14  0  2  -2  1.0027  0.9685
  dragons  on     1000    4   0    1  -1  1.0664  1.0969 | on12 1000   4  0  1  -1  1.0211  1.0744
  edf      on     1000   23   1    0  +1  1.1496  1.1374 | on12 1000  19  1  0  +1  1.0918  1.1704
  hinata   on      600   14   0    3  -3  1.0037  1.0451 | on12  600  11  0  3  -3  0.9999  1.0534
  niv      on      425   49   1    7  -6  1.0072  1.0872 | on12  425  48  1  7  -6  0.9964  1.0971
  Slower: edf 935 4->5 (both arms), niv 341 4->5 (both arms). (wall is contended: other agents' runs shared the box.)
  Hinata on fresh seeds is -3 (ab7 was +2 at 1,200 g): Hinata passes. EDF fails on both A/Bs: ab7 +2 / 1,000 g, ab9
  +1 / 1,000 g, and ZERO faster EDF games in 2,000 held-out games -- LR never helps EDF and occasionally hurts.
d8 b0 recoveries (`mcdragons9/d8`, `d8b`; seed = base + gi, game_index = gi):
  * niv 372, 419 (ab7) and niv 341 (ab9): RECOVER (identical digests on/off).
  * edf 323 (ab7): plain d8 b0 off 3 / on 4 (NOT recovered); MTG_NC_SEARCH=1 off 3 / on 3 (recovers).
  * edf 365 (ab7): plain off 4 / on 5 (NOT recovered); NC off 4 / on 4 (identical digest). NC on took 1,092 s vs
    361 s off (LR's unpriced unlimited-budget completeness on EDF: ~3x wall).
  * edf 935 (ab9): plain off 4 / on 5 (NOT recovered); NC off 4 / on 4 (identical digest; NC on 941 s vs 393 s off).
  So the EDF slower games recover only without clairvoyance. By the memo test ("if NC finds it -> honest loss") they
  are NOT clear clairvoyance: the clairvoyant on-arm search misses a win a non-clairvoyant search finds. USER call.
EDF per-game causes (not fully root-caused): 323 diverges at T2 (off: Aether Hub + Eldrazi Displacer {2}{W}; on:
  Mariposa + 2nd Wild Growth -> T3 Peregrine Drake, no Cloud of Faeries loop). The EDF combo route
  (MTG_EDF_COMBO_ROUTE_TRACE) says TRIAL no on every t2/t3 trial in BOTH arms, so the route is not what picks the
  T2 line; it is the search's own T2 comparison. LR memo VERIFY on 323: 0 mismatches. 935 diverges at T2 (off:
  Training Grounds {U} + Living Wish; on: Wild Growth + Living Wish -> T3 "Combo Off" without the win).
STOP RULE HIT: one concrete fix attempted (re-price 4 -> 12 probes a unit: EDF units +15% -> +9%, net unchanged +1).
  EDF is still net > 0 and I have no clear next lever. NOT run: smoke, scoped regression, mana-clean gate slice,
  squash. NOT pushed.
OPTIONS FOR THE USER:
  (1) Re-price kLineRepayProbesPerUnit 4 -> 12 (legit: a price, inert at budget 0). Measured: units Dragons
      1.066 -> 1.021, EDF 1.150 -> 1.092, Bruna 1.010 -> 1.003, Hinata 1.004 -> 1.000, Niv 1.007 -> 0.996; nets
      identical. Does not fix EDF's net.
  (2) Price pass 1 too (currently unpriced at depth 1): EDF's re-pays are all pass-1 finds and the unlimited-budget
      NC run shows LR at ~3x wall on EDF; an honest price would make LR compete with the search for the budget. Not
      measured.
  (3) Root-cause the EDF T2 divergences (323, 935) further: compare the T2 root scores on/off at d8 b0 (clairvoyant),
      i.e. which T3 line the on arm's search believes; the NC recovery says the line is findable.
  (4) Accept EDF at +1/+2 per 1,000 (fails the bar: "+1 is not neutral").
  Out of scope (proposals only, unchanged): engine-wide SubsetPayableSequential verdict memo; Bruna symmetric-split fold.
STILL RUNNING (not mine, left alive): ab7 on p7 (pid 2856514), successor 7's identity verify (pid 2871154).

## READ FIRST: Successor 8 (2026-11-04; stopped at the context cap -- successor 9 continues)
Worktree head = the commit carrying this section (wip/mc-dragons, not pushed, no GT accepted). Scratch
`build/scratch/mcdragons8/`. Binary under test = `mcdragons8/mtg.p11` / `mtg-test.p11` (= HEAD src).
WHAT P9-P11 CHANGED (ManaPayment.cpp, LINE_REPAY only):
  * p9 (exact): rec[1] found IN PLACE (PayRefineSnap Take -> LineRepayUndo -> LineRepayFind -> Restore) so a memo hit
    copies no board; the undone RampScratch is built only past the memo. Digest = Finish(prefix, n, LineRepayDigestBoard)
    with the board-wide fields hashed once per Find; Find hands the full digest to LineRepayMemoKey (no 2nd pass).
  * p10 (exact): LineRepayArmFold caches the heurarm fold (memcmp against the last arm) -- the per-byte fold was half of
    LineRepayTry's self instructions.
  * p11: LineRepayMemo.fail is key -> CHARGED. Charged = a priced probe ran / cut / VERIFY / ran off the p1 memo
    (PricedNote sets it; NoteFail reads it). An UNCHARGED hit does not NoteEpochMemoHit and its insert does not bump the
    memo generation (a recompute anywhere runs the same unpriced stages to the same false, charging nothing). fail_p1 is
    written only on the pass-2 CUT path (`p1_pending`). Stats: `stage(entered/probes) wf2/full1/full2`.
MEASUREMENTS (perf stat instructions:u, MTG_PGO=0, single-thread):
  * Hinata 3 games (seed 1870011 gi10): off 40.87 G; p8 52.28; p9 51.68; p10 51.00 (p11 not re-run on this sample).
  * Hinata per game gi12..21 (`mcdragons8/pg/`, off = p10 LR off): ALL p10 +21.7% -> p11 +8.0%. Same-tree games
    +1.0..2.6% (gi11 +2.2%). Excess sits in games whose tree changes: gi12 +49% -> +19% (136k tries, 125k memo hits:
    residual = per-try MidTurnStateKeyOf ~11k instr + 12.8k fruitless priced probes, full1 1,190 entries), gi18 +139% ->
    +30% (bp-cont computations 4,033 / 8,386 p10 / 4,924 p11; hits_served_as_misses 10,309 -> 2,326; the rest is ok_p2
    hits = genuinely budget-bound successes, found_p2 242).
  * Same outcome on all 10 games p10 = p11; MTG_BP_ENUM_VERIFY on gi12/18/20 with p11: 76,517 hits, CONTENT-DIFF 0.
  * Unit suite p11: 2,436/2,436 (`mcdragons8/ut11.log`).
  * IDENTITY p11 (`mcdragons8/id11/plain.out`, 4 threads) vs p7 (`mcdragons7/id7/plain.out`, 1 thread): 20/20 digests
    equal; 8 jobs differ by 1-59 UNITS. Suspect: the LINE_REPAY record ring (t_lr_ring) is NEVER cleared between games, so
    a record from an earlier game on the same thread can match (same TurnSeq/digest) -> units depend on thread history.
    Single-thread p11 re-run RUNNING (`mcdragons8/id11s/plain.out`, ~45 min from 03:40 UTC; parent shell pid 2968600):
    if its units equal p7's exactly, p9-p11 are exact and the 4-thread diffs are the ring leak. FIX TO MAKE either way:
    store `game_seed` in LineRepayRec, skip a record whose game_seed differs in LineRepayFind (determinism; may move play
    slightly -- smoke will show).
  * FiveColour units +2.2%: caches equal on/off; LR's own priced probes 401 (~100 units) of ~10k units delta per 20 games;
    fired 70 -> the delta is search over re-paid lines (justified). Bruna +1.2%: priced pass 2 = 23,575 probes / 20 games
    (~5.9k units, ~2% of units) for found_p2 = 2; by stage wf2 546/5,560, full1 671/13,485, full2 125/4,530 -- full1 (the
    full table at depth 1: Sol Ring / Wild Growth leftover splits) is the cost; a sound fold of symmetric leftover splits
    is the trim lever (not attempted).
A/B ab7 (p7; STILL RUNNING, pid 2856514, `python3 mcdragons6/abrep.py mcdragons7/ab7`), at ~55%: net Angels 0, Bruna -1,
Dragons -1, FiveColour -1, Goblins 0, Stompy 0, Melira 0, TreasureHunt 0, Colossus 0, Breaching 0, Alchemist 0 (750 g),
EDF +2 (875 g, units 1.149!), Hinata +2 (1,200 g). Slower: bruna 539 (recovers d8 b0, succ 7), hinata 1021 6->7 and 1055
5->7 (BOTH RECOVER at d8 b0 on p11: `mcdragons8/d8/nc0.out`), edf 323 3->4, edf 365 4->5 (d8 b0 NOT yet run).
Hinata 1055 divergence: T1 Ponder keep/shuffle (off shuffles, on keeps) -- a search-evaluation difference, LR fired 364
in that game; no payment on the line itself. Not root-caused.
OPEN for successor 9, in order:
  1. Wait for id11s; then add the game_seed ring guard (above), rebuild p12, rerun the id set 1-thread vs p11 (expect only
     the leak-affected units to move).
  2. When ab7 ends: EDF (+2, units +15%) and Hinata (+2) violate "net <= 0". Run d8 b0 (mkd8.py, nc0 then nc1 under
     MTG_NC_SEARCH=1) for edf 323/365; get EDF LR stats (stage probes) on a 20-game sample -- EDF's units +15% is the
     priority (EDF had the 45-min memo history). Consider re-running the EDF/Hinata/Bruna arms of the A/B on p12 (p11's
     cache fix changes budgeted play only through charges; Hinata on p11 still reproduces 1021/1055).
  3. Residual Hinata cost options (USER call; none exact AND cheap found): (a) SubsetPayableSequential verdict memo
     across enumerator scopes -- pay census: 67% of SPS calls repeat an earlier identical question (285k of 424k, all
     false), which would cut LR tries ~2/3 and also the OFF arm; engine-wide change. (b) a cheaper LR memo key than
     TurnSolver::MidTurnStateKeyOf (needs a proof of what the payer reads). (c) Bruna/Hinata full1 split fold.
  4. Then task 4 of the brief: smoke (THREADS=8), scoped regression, mana_clean gate slice on Dragons, squash.

## READ FIRST: Successor 7 (2026-11-04)
Worktree head = the commit carrying this section (branch wip/mc-dragons; not pushed, no GT accepted). Scratch
`build/scratch/mcdragons7/`. Binary under test = `mcdragons7/mtg.p6` / `mtg-test.p6` (= HEAD src, commit d0149cac8).

SUCCESSOR 6's JOBS: vprobe5 (VERIFY on p5, 7 decks x 50 games) is CLEAN -- memo/ok/joint/inner/p1_memo mismatch all 0,
no `[lr-mismatch]` lines (`mcdragons6/vprobe5.err`). Its EDF off30 gi37 took 1,401 s under VERIFY only; plain p5 is a
few seconds (`mcdragons7/edf37_p5.err`). Hinata gi403 on p4d (re-price only): STILL 876 s (`mcdragons6/hin403_p4_1.err`).

THE HINATA 1.48x ROOT CAUSE (gi403 = --seed 1870404 --game-index 403): not LR's own work (LR ms 80 s of 876 s). p2=5.02M
priced stages entered, p2_cut=5.02M -- nearly every one cut at once because the DECISION BUDGET WAS ALREADY SPENT --
and every entry called TurnSolver::NoteChargingSubsearch, as did every fail-memo hit (2.0M). Under MTG_CACHE_HIT_CHARGE
mode 1 that makes every enclosing cacheable computation UNSTORABLE (unstorable=27,957 vs 0 off), so the game-scoped
bp-continuation cache re-derived every lookup, recursively (perf: EnumeratePlans/BpDeriveContinuationList/ApplyPlanDirect).
FIX (commit d0149cac8, lossless, budgeted and unbudgeted alike): two NARROWER cache-charge notes.
  * TurnSolver.cpp:4055-4064 (Rec.epoch_memo / spent_cut, t_memo_gen, DecisionSpentNow), :4077 (Cost.bound /
    needs_spent / bound_epoch / bound_gen), Scope::Done(side_replayed) stores a bound computation only when its caller
    replays side counters (bp + front derivations pass true: a spent cut bumps budgetbind::t_binds, which BpSideReplay
    replays; m1/m2 enum caches do not, so they stay unstorable), and only if neither the epoch nor t_memo_gen moved while
    it ran. CacheHitCharge (:4200) serves a bound entry only in the SAME decision epoch, with t_memo_gen unchanged, and
    (needs_spent) with the decision budget still spent; a served bound hit re-notes the enclosing computations (:4209).
  * ManaPayment.cpp: fail-memo hit -> NoteEpochMemoHit (:8625); ok_p2 hit -> NoteEpochMemoHit (:8654); memo writes that
    change an answer bump the generation (fail insert :8637, ok_p2 insert :8787, a clear of a non-empty fail/ok_p2 :8614);
    PricedNote (:8697) on every exit: a priced stage that admitted a probe -> NoteChargingSubsearch (as before); cut at
    its first probe (0 probes) -> NoteSpentCutSubsearch; no probe and no cut -> nothing (the budget was never read).
  Why exact: within one epoch with the memo unchanged a recompute takes the same memo path, and with the budget spent
  every priced stage cuts at its first Admit (one bind each, same as the original). Pass 1 (st 0) is unpriced and
  deterministic, so pass-1 ok inserts and fail_p1 inserts need no generation bump.
  RESULT: Hinata gi403 on-arm 876 s -> 18 s (= the off arm's 18 s), same win turn 6, unstorable 27,957 -> 1,419
  (`mcdragons7/hin403_p6on.err`).
  Coordinator's data point (Breaching's mtg.v2 hung >16 h on Hinata seed 31000423 gi 423, job hinata_on__g400): on p6 it
  FINISHES -- on 440 s wall / 231 s user, units 77,634, win T7; off 228 s / 102 s user, units 48,855, win T7
  (`mcdragons7/hin423_p6{on,off}.err`, script `hin423.sh`). Still a 2.26x CPU / 1.59x units game: LR fired 18,914
  re-pays (4,936 depth 2), so the trees differ; p2_probes 116,429 (= ~29k units at 4 a unit, ~ the whole units delta),
  memo_hit 1.42M, LR ms 37 s. Not investigated further (coordinator: no separate investigation).
  SOUNDNESS of the epoch-bound hits: MTG_BP_ENUM_VERIFY=1 (re-derives EVERY bp hit and diffs it) on p6, Hinata gi403 +
  8 Hinata games: 269,074 hits re-derived, CONTENT-DIFF 0, order-only 0 (`mcdragons7/bv2.err`).

P6 PARTIAL A/B (`mcdragons7/ab6_partial.txt`, killed ~45% for p7): units on/off Angels 1.0039 (was +3.3% on p3), Bruna
1.0153, Dragons 1.0046, FiveColour 1.0225, Goblins 1.0000, Hinata 1.0054 (375 g; wall 1.097), Melira 1.0003, Stompy
1.0175, TreasureHunt 1.0000. Slower: bruna 539 (5->6) and 837 (4->5), stompy 1868 (4->5); net +1 only on Stompy.
d8 b0 on p6 (`mcdragons7/d8a/nc0.out`; valid for p7/p8 too -- at b0 nothing is priced and the p7/p8 changes are exact):
ALL THREE RECOVER (bruna 539 5/5, 837 4/4, stompy 1868 4/4, identical digests on/off).
STOMPY 1868 ROOT CAUSE: LR fired 0 times; 3,974 fruitless priced probes (~1k units) displaced search. 60% of them were
FULL-table stages repeating the whole-float search exactly (Sol Ring paid by the only Forest, then a {G}: no float, no
multi-unit leftover -> the full table IS the whole-float table).
P7 (commit b534d1277), exact: manaoracle::t_full_table_differs (ManaOracle.h after ProbeCharge; set in MinimalPayments
on a non-empty floating pool or a capped walk, and in PayEnum::Verify on a multi-unit leftover the full form would
split). LR stage loop (ManaPayment.cpp, `wf_equiv` / `fulleq_check`): a whole-float stage that searched to its end with
the flag unset sets bit d-1; the full stage at that depth is skipped (VERIFY runs it unpriced and counts
`full_eq_mismatch`). The bits ride in the p1 memo (fail_p1 is now a map key -> bits). The p1 memo hit is now
NoteEpochMemoHit and its insert NoteEpochMemoWrite (with the skip, a p1 hit can leave nothing priced, so the
computation would otherwise be stored across decisions while a recompute there charges st1's probes). Stompy 1868 on
p7: wins T4 (recovered at budget), probes 3,974 -> 1,244, full_eq_skip 422. Unit suite p7: 2,436/2,436 pass
(`mcdragons7/ut7.log`).
HINATA LR CPU (3 games seed 1870011 gi10, same digest both arms, units +1.5%): perf on-vs-off cycles +19% (+7.9 G of
41 G): LineRepayDigest +0.98 G (LineRepayFind: up to 16 records, a digest each), BuildSimKeyBase +0.86 G
(LineRepayMemoKey -> MidTurnStateKeyOf, once per try past the amount check; ~200k tries / 3 games, fired 28),
TapForCostShared self +0.48 G, memmove +0.40 G (record snapshots), ManaCacheKey +0.18, LineRepayRecord +0.15.
P8 (commit carrying this section; perf only, play-identical to p7: same digest + units on the Hinata 3-game sample): LineRepayDigest split into
Seed/Step/Finish, LineRepayFind hashes every record's prefix in ONE pass (thread_local prefix array) and skips a record
whose TurnSeq/active player differ (both are in the digest).
  Instructions (perf stat -e instructions:u, the same 3 Hinata games): p7 on 53.67 G, p8 on 52.28 G (-2.6%), p8 OFF
  40.87 G -> LR still costs +28% INSTRUCTIONS on Hinata at +1.5% units. Caches are NOT the cause (bp/m1 computations
  equal on/off, unstorable 102 vs 2). Remaining named costs: LineRepayMemoKey's MidTurnStateKeyOf (BuildSimKeyBase,
  once per try past the amount check), record snapshots in TapForCostShared (PayRefineSnap::Take, SnapPayFields,
  memmove), ManaCacheKey; ~200k tries / 3 games for 28 fires. Wall is useless on this box (load ~50/32: the same arm
  varies 22.6-32.1 s); use instructions:u. THIS IS THE OPEN WALL ITEM (USER: justify or trim anything above ~1%).

RUNNING AT HANDOFF (all mine; kill freely):
  * A/B on p7 (play-identical to p8): pid in `mcdragons7/ab7/ho.pid`, `ab7/ho.{out,err}`, logs `ab7/w`, 6 threads,
    1,168 jobs. Report: `python3 mcdragons6/abrep.py mcdragons7/ab7`. Partial at ~30%: Angels units 1.0051, Bruna 1.0123
    net -1, Dragons 1.0002 net -1, FiveColour 1.0222 net -1, Goblins 1.0000, Stompy 1.0071 net 0; slower: bruna 539 only
    (already recovers at d8 b0).
  * Identity on p7: `mcdragons7/id7/{verify,plain}.{out,err}` (pids in `id7/*.pid`, 1 thread each,
    mcdragons3/id/id.json = 1,600 games, 7 decks). Pass = every job's digest equal between verify.out and plain.out, and
    in verify.err's LINE REPAY line memo/ok/joint/inner/p1_memo/full_eq mismatch all 0.

NEXT (successor 8)
1. When ab7 finishes: abrep; every deck net <= 0? `mcdragons6/mkd8.py <abrep out> <prefix>` for any NEW slower game, run
   `<prefix>.nc0.json` (and `.nc1.json` under MTG_NC_SEARCH=1 if a game does not recover) on mtg.p8, one pooled batch.
2. Units above ~1% (FiveColour ~+2.2%, Bruna ~+1.2%): get `fired` and `p2_probes` per deck (MTG_LINE_REPAY_STATS=1 on
   a 20-game sample); justify as found re-pays or trim like p7 (find the fruitless priced stage with a temporary
   LineRepayDumpMismatch call after LineRepaySearch when probes>0 && !found; DELETE it after).
3. Hinata +28% instructions (+11.17 G of 40.23 G; `mcdragons7/hi8_{0,1}.perf`, per-symbol `i{0,1}.sym`): BuildSimKeyBase
   +2.06 G (LineRepayMemoKey -> TurnSolver::MidTurnStateKeyOf, every try past the amount check), LineRepayTry self
   +1.39 G (inlined: the `RampScratch undone_scr(state)` full board copy + LineRepayUndo + the depth-2 LineRepayFind,
   all done BEFORE the memo lookup because the key folds rec[1]; memmove +0.34, new +0.16), digests +1.05 G
   (DigestFinish +0.49 re-mixes the board-wide pools per record: make the digest Combine(prefix[n], n, board_hash) with
   board_hash computed once per Find), ManaCacheKey +0.31, SnapPayFields +0.23, TapForCostShared(+Once) +0.41; search
   growth ~1.5 G (units +1.5%). Ideas, all exact: (a) key the memo on rec[0] first and look up a rec[1]-free "fail for
   every rec[1]" entry before building the undone board; (b) a cheaper exact key prefix (e.g. a two-level map: the
   LineRepayDigest + cost + records first, MidTurnStateKeyOf only on a first-level hit); (c) board_hash above.
4. Gate slice `mcdragons3/gate.sh mcdragons7/mtg.p8 s8 8`; smoke `THREADS=8 bash test/regression.sh --smoke` (needs the
   8 threads: run after ab7 + id7); unit suite on p8 (`mcdragons7/mtg-test.p8`; p7 passed 2,436/2,436).
5. Squash 24c31d4da..HEAD into landable commits; do not push, do not accept GT.

## READ FIRST: Successor 6 (2026-11-03, stopped at the context cap; successor 7 continues)
Worktree head = the commit carrying this section (branch wip/mc-dragons; not pushed, no GT accepted). Scratch
`build/scratch/mcdragons6/`. Binary under test = `mcdragons6/mtg.p5` / `mtg-test.p5` (= HEAD src).

WHAT SUCCESSOR 5's JOBS ACTUALLY PRODUCED
- A/B `mcdragons5/ho`: DID NOT RUN -- died at manifest load (d0 jobs on value_play decks need `ignore_play_profile`).
  Fixed generator `mcdragons5/ho2/mk.py`; rerun on mtg.p3 as `ho2`, KILLED at ~60% as superseded by the fixes below.
  Partial p3 table `mcdragons6/ab_p3_partial.txt` (report script `mcdragons6/abrep.py <dir>`): Angels units +3.3% with
  0 digests moved; Dragons 2000 paired 18 dig 1 slow/1 fast units +7.5%; Bruna 1800 41 dig 5/4 (net +1) +4.2%;
  FiveColour 800 8 dig 0/1 +3.2%; Stompy 2000 8 dig 2/0 (net +2) +5.1%; Hinata 575 11 dig 1/0 wall 1.48x (one game,
  off400 gi403 = --seed 1870404 --game-index 403: on 1,105 s vs off 18 s); Goblins/Melira/TreasureHunt ~identical.
  Slower p3 games: bruna 539/574/837/1166/1613, dragons 1678, hinata 129, stompy 1868/1987 (seed_base 1870001).
  NO d0 cells finished; no d8 b0 run yet (`mcdragons6/mkd8.py <abrep out> <manifest>` writes .nc0/.nc1 manifests;
  MTG_NC_SEARCH is env-only, so run the .nc1 manifest under MTG_NC_SEARCH=1).
- idv3 (VERIFY on p3) NOT clean: joint_mismatch=9443 inner_mismatch=3504 (memo/ok/p1 mismatches 0). Plain-vs-base
  digests: 6 of 1,600 differ (alchemist gi145 8->7, niv gi121 5->6, niv gi147 4->5, 3 same-turn).
- Gate slice p3 (`mcdragons3/gate_s5p3.err`): ENUM-GAP 1, APPLY-DROP 147 (need_later 144 = mechanism 3, USER
  decision; need_in_plan 3), ORACLE-GAP 6, CAPPED 7 (same as base), UNMODELED 0. Base 247/564/316/CAPPED 7; s1 new
  1/142/28. Unit suite p3 (`mcdragons5/ut3.log`): 2,435/2,435 pass.

FIXES (commit 30a5e901b + the one carrying this section), all in src/ai/ManaPayment.cpp unless noted:
  (a) `LineRepayChain::FlatBills` (:8293): the joint prune skips hybrid/phyrexian/twobrid/snow/X bills (AddManaCost
      sums flat ints; Vexing Shusher {R/G}{R/G} joined as {R}{R}). Prune exactness only.
  (b) PLAY BUG: `LineRepayOnPastBoard` / `LineRepayRemakeAt` (:8318/:8334, used at :8439 and in commit): entrants are
      OFF the battlefield while an earlier payment's table is built and it is re-made (Wild Growth paid for itself
      with its own extra {G}). Unit test "Wild Growth does not pay for itself" (test_line_repay.cpp; bite NOT verified).
  (c) PLAY BUG: `LineRepayRemake` (:8231) rejects a re-make that changes battlefield/graveyard size (Generator
      Servant sacrificed, NivMizzet).
  (d) PLAY BUG: depth 2 dropped when rec[1]->n > rec[0]->n (:8609; a bounced land replayed into its slot matched the
      digest, Bruna Boseiju/Sol Ring/Wild Growth).
  (e) RE-PRICE: `kLineRepayProbesPerUnit = 4` (:8358) via `manaoracle::ProbeCharge::Admit` (ManaOracle.cpp:185,
      per-decision accumulator; null budget still notes CacheChargeNoteNull). Probes cost 5-21 us vs 40-77 us a unit.
  Diagnostic `LineRepayDumpMismatch` (:8372, VERIFY only, 40 lines max, `[lr-mismatch]`). EngineFlags.h/HeuristicArm.h
  LINE_REPAY descriptions updated (staged passes).

RUNNING (my experiments; kill freely): VERIFY probe on p5 pid 2760953 -> `mcdragons6/vprobe5.{out,err}` (7 decks x
50 games; answers: are joint/inner_mismatch now 0? grep `LINE REPAY` and `lr-mismatch`). Hinata gi403 on-arm repro on
p4d (= p5 minus (c),(d)) pid 2758560 -> `mcdragons6/hin403_p4_1.{out,err}` (off arm took 18 s; answers: is the 9x wall
gone after the re-price? p3 LR stats: tried 8.08M, memo_hit 6.2M, p2 736k of which cut 735k, LR ms only 63 s -> the
rest is likely the cache-charge NoteChargingSubsearch on every fail-memo hit / priced entry making enclosing caches
unstorable; compare `[cache-hit-charge]` vs the off arm: unstorable=0).

NEXT
1. vprobe5 counters all 0 (else read `[lr-mismatch]` dumps: they print board, chain n, joint parts, picks).
2. Hinata gi403: if still slow, candidate lossless trim: note the charging subsearch on a fail-memo hit only when that
   failure entered a priced stage (track a `fail_priced` set); measure.
3. A/B p5: `mcdragons6/ab5/ho.json` (copied, 17 decks, 1,168 jobs, ~12 nominal core-h) ->
   `cd <worktree>; MTG_RSS_CAP_GB=43 mcdragons6/mtg.p5 --batch mcdragons6/ab5/ho.json --threads 8 --game-log-dir
   mcdragons6/ab5/w > ab5/ho.out 2> ab5/ho.err`; report with abrep.py (searched and d0 tags separately).
4. d8 b0 (+NC) on every slower game, one pooled batch per NC setting; net <= 0 per deck; units/wall per deck.
5. Identity: rerun `mcdragons3/id/id.json` under VERIFY on the final binary (all mismatch counters 0); gate slice
   `mcdragons3/gate.sh <bin> s7 8`; unit suite; `THREADS=8 bash test/regression.sh --smoke`.
6. Squash 24c31d4da..HEAD (and the earlier WIP) into landable commits; do not push or accept GT.

## READ FIRST: Successor 5 (2026-11-03, stopped at the context cap; successor 6 continues)
Branch wip/mc-dragons; WIP commit "two-pass LINE_REPAY" on top of 3d62e85ec. Scratch `build/scratch/mcdragons5/`.
Binary under test = `mcdragons5/mtg.p3` (= HEAD src). Not pushed, no GT accepted.

DESIGN (src/ai/ManaPayment.cpp): staged admission in `LineRepayTry` (:8445; stage loop after `p1_empty` :8568).
Stages in order: (whole-float, d1) UNPRICED; then PRICED on TurnSolver::DecisionBudget(): (whole-float, d2), (full, d1),
(full, d2). Price `kLineRepayProbeUnits = 1` (:8307) per MinimalPayments probe and per re-made-option trial, via
ChargeDecisionUnits; cut when Exhausted()/Overrun(); a cut is never memoised (`note_fail.armed = false`). Unlimited
budget -> null -> complete. `LineRepayPass` (:8311), `LineRepaySearch(..., pass)` (:8329). Priced MinimalPayments:
`manaoracle::ProbeCharge` (ManaOracle.h:209; charge in PayEnum::PayAndRecord, ManaOracle.cpp:1254).
Memos: `fail` (complete failures only), `ok` + `ok_p2` (hit on a priced success -> NoteChargingSubsearch),
`fail_p1` (pass 1 proven empty -> repeat tries skip it). NoteChargingSubsearch on fail hits and on entering a priced stage.
Sound prune: `LineRepayScopeFree` (:8237) -- JointPays (top level AND per option at d2) only on a board with no
context-restricted mana (creature-only/subtype/big-spell/colored_creature_only/ability_ok/restricted floats).
VERIFY (`MTG_LINE_REPAY_MEMO_VERIFY=1`) re-checks memo hits, fail_p1 and both prunes unpriced; play-identical.
DEVIATION from the brief, for the coordinator/USER: the depth-2 WHOLE-FLOAT stage is priced too. With only pass 2
priced (mtg.p2) smoke63 was still 18.0 s vs 5.7 s off (3.2x): all 12 s of re-pay cost was pass-1 d2 (1,365 searches,
66k inner prunes); every pass 2 there was cut at entry (budget already spent). Pricing it: 3.66 vs 2.82 s (1.30x), but
EDF g137 fired 391 -> 43 (401 d2 stages cut post-budget; still T5). Both shapes are complete at b0.
Levers deleted: MTG_LINE_REPAY_DRAW (barrier logic kept), MTG_LR_INNER_JOINT, MTG_LR_MAX_PRECHECK, MTG_LR_WHOLE_FLOAT.

MEASURED: Bruna s1830069 gi68 fired 220, 86bc663936b9ccc5 (= v2) on p2 (prune ON; joint_scoped=785 -> skipped there).
Wall (`mcdragons5/wall/run.sh <bin> <tag>`, .res files; box load ~57, noise +-2x on 1-5 s games):
p3 g137 4.8/3.2 s, smoke63 3.66/2.82, Dragonstorm gi45 0.46/0.46, EDF s1930046 gi45 noise (LR ms 0).
(NB the predecessor's "ds45" ran the EDF deck; run.sh now runs both.) Units on p2: 2,435/2,435 (`ut.log`); p3 running (`ut3.log`).
Identity baseline `mcdragons5/idbase` = mtg.v2 draw-ON over `mcdragons3/id/id.json`. p2 vs base (1,500 of 1,600; EDF
unfinished): 3 differ -- AA off100 gi145 8->7 (faster), Niv off100 gi147 4->5 (slower; recovers: T4 on AND off at
--depth 8 --budget-ms 0, `mcdragons5/d8/`), Niv off50 gi69 same turn. Verify counters 0 (p1/p2 runs).

RUNNING (leave them): A/B `mcdragons5/ho` pid 2634771 (ho.json from ho/mk.py: 17 decks = brief's 8 + Hinata, FiveColour,
Galadriel, Colossus, Stompy, Melira, Pirates, Angels, Cyclone; play settings 400-2,000 games/arm + d0 2,000/arm,
seed 1870001 / d0 1870001+500000; ~12 nominal core-h, started 20:52 UTC). Identity verify p3 `mcdragons5/idv3` pid
2634772. Gate slice p3 pid 2635177 -> `mcdragons3/gate_s5p3.{out,err}`. Units p3 -> `mcdragons5/ut3.log`.
Wait with `while kill -0 <pid>`. No 2-h CronCreate timer was set (not available to the subagent) -- coordinator please.

NEXT: (1) idv3: `python3 mcdragons5/cmpw.py mcdragons5/idbase/w mcdragons5/idv3/w`; grep `LINE REPAY` in idv3/id.err:
memo_mismatch/ok_mismatch/ok_stale/joint_mismatch/inner_mismatch/p1_memo_mismatch must be 0; list changed games.
(2) A/B report per deck, searched and d0 separately (abrep.py-style pairing; add ms per arm from ho.out); every slower
searched game -> --depth 8 --budget-ms 0 on/off, then MTG_NC_SEARCH=1 for clairvoyance; net <= 0 per deck.
(3) Gate: `python3 mcdragons3/nl/split.py mcdragons3/gate_s5p3.err` (draw-narrowing counts), CAPPED count; rerun CAPPED
plans with a larger MTG_MANA_ORACLE_NODES. (4) `THREADS=8 bash test/regression.sh --smoke` (EDF row normal time).
(5) Update EngineFlags.h:896 / HeuristicArm.h:828 descriptions (two passes, depth <= 2); squash; don't push/accept GT.
BREACHING HOOKS (coordinator list): amount shortcut in LineRepayTry unchanged; LineRepayRemake + PerformPayTaps path
unchanged; LineRepayRecord unchanged. CHANGED: LineRepaySearch gained a 6th param `const LineRepayPass&`, and
MinimalPayments a trailing `ProbeCharge*` (default null). Multi-unit subsets: pass 1 is whole-float (no leftover
split); the full table (with splits) only in priced stages -- a cut can drop them under a finite budget (th 30101568).

## Mechanisms (evidence)

1. **ORACLE-GAP, 2,880 of 3,074 -- the oracle was wrong.** Haven of the Spirit Dragon's rebuy ("{2}, {T}, Sacrifice")
   -- the oracle's frontier table offered the Haven's own {C} toward its {2}; the engine holds it (SelfTapSourceHoldBit),
   so the engine's "rebuy unpaid, {G} still floating" outcome was never reached. Fix: `manaoracle::AbilityTapSrcScope`
   set at both rebuy sites (`src/ai/TurnSolver.cpp:65748`, `src/ai/AIEngine.cpp:6536`), captured at the frontier
   (`src/ai/ManaPayment.cpp:8443`, `Hook::f_self_src` in ManaOracle.h), excluded in `BuildSources`
   (`src/ai/ManaOracle.cpp:921,970,2123`). Play reads none of it. Repro gi315 T3: ORACLE-GAP 5 -> 0.
2. **APPLY-DROP + ENUM-GAP -- per-cast payer strands the next cast's colour around Gruul Turf ({R}{G}).**
   {Mind Stone, Scourge}: Mind Stone's {2} took a Mountain; {Urza's Incubator, Dragon}: the Incubator's {3} took the
   Mountain and the Dragon (discounted to {R}{R}) met Gruul alone. ENUM-GAP is the same failure inside the enumerator's
   sequential walk (`SeqWalkFixedOrder`, gate `ef-walk-fail`). Fix: `MTG_LINE_REPAY` (DEFAULT ON, heurarm
   `LINE_REPAY`, `src/ai/EngineFlags.h:901`): every committed outermost payment pushes a record (digest + pre-board,
   `LineRepayRecord` `src/ai/ManaPayment.cpp:8048`, `LineRepayDigest` :7980); when a line's payment fails, the matching
   previous payment (depth <= 2) is re-made over the oracle's `MinimalPayments` until the failing one pays
   (`LineRepayTry` :8177; hook in `TapForCostShared` :8274 / :8304). Refuses across a draw (library size in digest),
   human play, tap-pref replay, oracle copies, armed boards, sacrifice/graveyard/drip/haste payments (barriers).
3. **Not fixed -- need-later APPLY-DROP (USER decision).** A continuation chose a cast AFTER a draw (top: T3
   {land=Gruul Turf}, Mind Stone's {1} draw paid with the floating {R}, post-draw Bolt has no {R}). Re-paying across
   the draw is clairvoyant; the lossless shape is a searched payment variant at a breakpoint opener (oracle doc
   section 2.3 / section 5 question 1). Surface to the USER; do not settle.

## Commits
- WIP commit on `wip/mc-dragons` (this handoff): all of the above + tests. Not yet pushed.

## Measurements

**Gate slice** (Dragons games 100000-109999, `MTG_MANA_ORACLE=2`; outputs `build/scratch/mcdragons/gate_{base,new}.{out,err}`):

| binary | ENUM-GAP | APPLY-DROP | ORACLE-GAP | total issues | census |
|---|---|---|---|---|---|
| base (980616e55, `basesrc/build/Release/mtg`) | 247 | 564 | 316 | 1,127 | depth1 440, depth2 124, off-menu 247, need-later 129, rollback-rescued 253/564 |
| new (lever on) | 1 | 142 | 28 | 171 | depth1 125, depth2 17, need-later 138, need-in-plan 4 |

Residue on the new binary: APPLY-DROP 138 need-later (mechanism 3) + 4 need-in-plan (seed 30102617 gi2617 T5
{Haven, Bolt, Lathliss}: census depth2 through a 4-decision chain, likely a barrier/zero-cost record in between --
not chased); ENUM-GAP 1 (seed 30101370 T4, 4-action plan with Greaves equip -- likely depth 3); ORACLE-GAP 28: 20 on
{Mind Stone + another cast} (pre-existing shape, nearest outcome "same core" with the other Mind Stone tapped -- the
engine's board appears among the oracle's options, cause not yet found) and 8 on {creature + Haven rebuy} with two
Havens (one Haven taps for the creature, the other is the rebuy source). A diagnostic now prints every outcome a gap
was judged against at level 3 (`gap-outcome` lines, `src/ai/ManaOracle.cpp:2892`); repro e.g.
`MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg decks/Dragons/Dragons.cod --profile decks/Dragons/Dragons.profile.json --games 1 --seed 30004558 --game-index 4558`.

**Identity**: base vs new with `MTG_LINE_REPAY` off, 6 decks x 100 games (`ab/ident_{base,new}.out`): byte-identical
digests and units.

**A/B (RUNNING)** `ab/ab1.json`, one pooled batch, per-job `MTG_LINE_REPAY` off/on: Dragons 6,000/arm (seed 1810001),
Dragons 2HG 6,000/arm (1820001, life 30, 2 heads), every other suite deck 60-300/arm (1830001). Interim (139 of 356
jobs): Dragons 6,000 paired -- 57 digests changed, 0 slower, 1 faster, units 1.0011; Bruna 150 paired: 0 slower / 1
faster; Angels, Fluctuator, Goblins, Soldiers, FiveColour byte-identical so far. Report:
`python3 logs/mcdragons/abrep.py` (prints per-deck paired/digest/slower/faster/net/units and the slower games).

**Unit tests**: `test/unit/test_line_repay.cpp` (3 cases, each shape fails with the lever off) + the Haven case at the
end of `test/unit/test_mana_oracle.cpp`: 4/4 pass. Full suite RUNNING. **Scenario**:
`test/scenarios/dragons_incubator_lathliss_gruul_repay.json` passes with the lever on, fails with it off
(`expect_log_sequence` T4 Incubator + Inferno). Full scenario suite RUNNING.

**A/B FINAL** (ab1 finished; full table `build/scratch/mcdragons/ab/ab1_report.txt`): Dragons 6,000 paired: 57 digests,
0 slower / 1 faster (net -1), units 1.0011. Dragons 2HG 6,000: 65 digests, 0 slower / 4 faster (net -4), units 1.0012.
Other decks that move (all screened at 60-300 games, so each needs >= 5,000/arm before adoption): NivMizzetCombo 150
(18 digests, 0/2, net -2, units 0.9975), SelesnyaLifegain 300 (14, 0/1), Bruna 150 (6, 0/1, units 1.0096), EDF 150
(10 digests, 1 slower / 1 faster, net 0, units 1.0165), AccomplishedAlchemist 150 (1 digest, 1 slower, net +1),
Pirates 300 (1, 0/1), Galadriel (2 digests, no turn change), FiveColour (2), Stompy (1). Every other suite deck
byte-identical. SLOWER GAMES to check at d8 b0 (both arms): alchemist seed 1830001 + gi 245 (7 -> 8, i.e. --seed
1830246 --game-index 245), edf seed 1830002 gi 1 (4 -> 5). EDF's slower game may share a cause with the
test_edf_goff_verify failure below.

## Jobs still running (none now -- all finished)
- A/B batch: PID 1832202 (`build/Release/mtg --batch .../ab/ab1.json --threads 8`), wrapper 1832200. Output
  `build/scratch/mcdragons/ab/ab1.out`, `ab1.err`, per-game `ab/ab1_w/*.wins`. NOTE: build/Release/mtg was relinked
  after it started (diagnostic print only, level >= 3); the running process keeps the old image.
- Full unit suite: FINISHED (`build/scratch/mcdragons/r/ut_all.txt`): 2,435 cases, 2,430 pass, **5 FAIL** -- must be
  resolved before landing. Read so far (not yet verified against a base mtg-test, which was never built):
  * `test_dragonstorm_followups.cpp:949,1001-1002` (MTG_LINE_SPARE_GENERIC_FIRST) and `test_niv_payment_holes.cpp:307-309`
    (Niv hole 2, MTG_RAMP_FEED_ROUTE): the failures are in the LEVER-OFF arms -- the line re-pay now rescues the drop
    those arms pin. Likely fix: pin `heurarm::LINE_REPAY` off in the off arm of each (the tests isolate their own lever).
  * `test_mana_oracle.cpp:242-245` (viewer line check, EDF seed 3000 T4): the engine's own apply now pays the line
    ("Training Grounds, then Eldrazi Displacer"), so `engine_full` is true where the test expects the drop. Likely the
    same: pin LINE_REPAY off for that case (it pins the historical payer, as its neighbours do), and consider adding a
    case that the search now pays it.
  * `test_edf_goff_verify.cpp:119-124` (EDF d0 unprojected go-off, ON arm): the go-off no longer kills on application
    (opponent at 19). NOT understood -- may be a real regression from the re-pay in EDF's blink loop (an untap effect
    between payments should already block it via the digest; check the ring/barrier logic and the d0 runner). Root-cause
    it before landing; run the test with LINE_REPAY off to confirm attribution.
- Scenario suite: FINISHED -- 345 passed, 0 failed, 47 ERROR. All 47 are `./build/Release/mtg: Permission denied`
  (the bruna_human_wings_swap... through cyclone_cycle_seed_red_float_legacy_misses block), from the mtg relink that
  ran in the middle of the suite -- not real results. Re-run those 47 (or the whole suite) on the current binary.
  Old line, kept for reference: PID 1866354 (`bash test/scenarios.sh`), output `build/scratch/mcdragons/r/sc_new.txt`
  (base comparison for any FAIL: `MTG_BIN=/workspaces/MagicDeckTester/build/scratch/mcdragons/basesrc/build/Release/mtg bash test/scenarios.sh`).

## Next steps
1. When ab1 finishes: `python3 logs/mcdragons/abrep.py`. For every slower game (deck, seed_base, game_index):
   per-game seed = seed_base + game_index; run both arms at `--depth 8 --budget-ms 0 --ignore-play-profile`
   (`MTG_LINE_REPAY=0` / `=1`; 2HG adds `MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2`), e.g.
   `MTG_LINE_REPAY=1 build/Release/mtg decks/Dragons/Dragons.cod --profile decks/Dragons/Dragons.profile.json --games 1 --seed <s> --game-index <gi> --depth 8 --budget-ms 0 --ignore-play-profile`.
   If not recovered, check clairvoyance with `MTG_NC_SEARCH=1`. Per-deck net must be <= 0. Any other deck whose
   digests move needs >= 5,000 held-out games per arm.
2. Check `ut_all.txt` (all must pass) and `sc_new.txt` (compare any FAIL against the base binary).
3. Fill section 14 of `docs/design/mana-completeness-oracle.md` with the gate table, A/B table, unit/scenario
   counts; delete `docs/design/mc-dragons-checkpoint.md` (superseded by this file) or keep one of the two.
4. Optional: chase the 28 ORACLE-GAPs with the `gap-outcome` diagnostic, and the 4 need-in-plan / 1 ENUM-GAP residue.
5. Amend/commit (end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`), `git push origin wip/mc-dragons`,
   watch CI (Windows) in the background. Report to the coordinator; surface mechanism 3 as a USER decision.

## Successor 2 status (2026-11-03, final)
- Commits: 65d6c2525 (blink-loop scope + untap count + 4 test rewrites), then the doc/handoff commit on top.
  Not pushed, GT not accepted.
- Unit 2435/2435, scenarios 392/392 (`build/scratch/mcdragons/r/{ut_fix2,sc_fix2}.txt`, binary `mtg.fix2`).
- A/B final = ab1 OFF vs ab2 ON (`bash logs/mcdragons/mkabx.sh`): table in mana-completeness-oracle.md section 14.
  Slower: Alchemist s1830246 gi245 (7->8), EDF s1830002 gi1 (4->5); both T4/T4 at d8 b0. Units on unchanged-play
  jobs x1.0003. Alchemist net +1 on 150 games -> the >= 5,000/arm held-out run decides it.
- NEXT 1: held-out >= 5,000 games/arm for Niv, SelesnyaV2, Bruna, Pirates, EDF, AccomplishedAlchemist (one pooled
  batch, THREADS <= 8 while the box is loaded, new seed base; Bruna ~2.4 s/game-thread).
- NEXT 2 (coordinator, 2026-11-03): the 138 need-later APPLY-DROPs. Claim: in every APPLY-DROP line of
  `build/scratch/mcdragons/gate_new.err` the dropped card is in hand= on the pre-plan board (e.g. seed 30100059 gi59
  T4: Bolt x2 in hand, Mind Stone's {1} draw paid with the floating {R}). (1) Verify the split across all 138:
  failing card in hand before the draw vs drawn in between. (2) Narrow LineRepayTry's draw barrier (the library size
  in LineRepayDigest): allow the re-pay across a draw when the failing cast's card was in hand before the draw; keep
  refusing when it was drawn. (3) Re-run the gate slice + the A/B with units. Optional: payer preference "spend the
  floating colour the hand needs least on a generic pip" (a payment-order change, same A/B). Only the drawn-card
  cases remain a USER question.

## Successor 3 (2026-11-03, stopped at the context cap) -- READ THIS FIRST
State: all code below is in WIP commit a56a429ef. BUILT (build1.log rc=0; `build/Release/mtg` and `mtg-test` carry the memo/DRAW strings), NOT TESTED -- start at Task 1 NEXT (1). A build of it is queued:
`buildq.sh` PID 2193594 (cwd = this worktree), log `/workspaces/MagicDeckTester/build/scratch/mcdragons3/build1.log`
(prints `rc=` when done; the queue was ~9 deep at 12:25 UTC). If that PID is gone without `rc=`, rebuild:
`cd <worktree> && bash /workspaces/MagicDeckTester/build/scratch/buildq.sh`. No other jobs of mine are running.
Scratch: `/workspaces/MagicDeckTester/build/scratch/mcdragons3/` (`mtg.cur` = binary of HEAD 3d3b067c1, i.e. the
pre-fix line re-pay; same bytes as `mcdragons/mtg.fix2`).

### Task 1 (held-out A/B) -- BLOCKED on a wall-time defect, now (hopefully) fixed in code
- FOUND: the line re-pay costs EDF ~20-40x WALL with units unchanged. ab1 (same batch, same box): EDF ms ON/OFF
  = 26x, units 1.0165; every other deck ~1x. Per game: EDF seed 1830038 gi 37 3,611 s (7,526 units), seed 1830138
  gi 137 2,737 s ON vs ~100 s OFF. Section 14's "units x1.0003" never saw it (the work is outside the units).
- perf (`mcdragons3/edfslow/perf3.data`): 99% in `manaoracle::MinimalPayments` under `LineRepaySearch`, reached from
  `EdfRangeAurasBehindUntappers` (EDF provider) -> `OrderPaysSequentially` -> `SeqWalkFixedOrder` inside
  `BpDeriveContinuationList`/`BpEnumEntryFor`. Inside MinimalPayments: `PayEnum::Verify`'s leftover-split recursion
  and `ExtendSurplusNow` (Aura'd lands = multi-unit taps; up to 4,096 verified probes per set-aside, per try).
- FIX (in the WIP commit, ManaPayment.cpp, lossless by construction, to be verified):
  (a) a decision-scoped memo of FAILED re-pays (`LineRepayMemoKey`: MidTurnStateKeyOf + re-pay digest + library
      size + both chain records' content hash (`LineRepayRec::content`, computed at record) + the bill + every
      payer thread-local I found + the heurarm arms; scope `TurnSolver::DecisionEpoch()`, cap 65,536, successes
      never memoised). `MTG_LINE_REPAY_MEMO_VERIFY=1` re-runs every hit; a disagreement counts `memo_mismatch`.
  (b) depth 2 checks the joint bill per option before recursing (`LineRepayChain::JointPays`, the same necessary
      condition the top level already used); the chain's record 2 is now found up front (needed by the key).
  (c) `MTG_LINE_REPAY_STATS=1` now prints searched d1/d2, MinimalPayments calls/options, inner-joint prunes,
      memo hits/mismatches, total ms, and a `[line-repay] slow try` line for any try > 100 ms.
- NEXT: (1) when built, copy the binary to `mcdragons3/mtg.new`. (2) Identity: run `mcdragons3/id/id.json` (ab2's ON
  jobs for the 6 movers + 2 Dragons jobs, renamed id_*) with `MTG_LINE_REPAY_DRAW=0 MTG_LINE_REPAY_MEMO_VERIFY=1
  MTG_LINE_REPAY_STATS=1 MTG_DUMP_WINS=1`, threads 8, `--game-log-dir mcdragons3/id/w`; per-game digests must equal
  `mcdragons/ab/ab2_w/on_*.wins` (same seeds; ab2 ran mtg.fix2) and memo_mismatch must be 0. (3) Wall: EDF seed
  1830138 gi 137 single game with the new binary must be ~100 s, not ~45 min (command in the edfslow dir:
  `cd <worktree> && MTG_LINE_REPAY_STATS=1 <bin> decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod --profile
  decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json --games 1 --seed 1830138 --game-index 137`). If
  the memo is not enough, the stats line says where the time is (d1 vs d2, options per call). (4) Then the held-out
  A/B: `python3 mcdragons3/ho/mk.py mcdragons3/ho/ho.json` (already generated: 1,100 jobs) -- SEED BLOCK 1840001
  (fresh: prior A/Bs used 1810001 / 1820001 / 1830001, the gate 30000000+); 5,000 games/arm for Niv, EDF,
  Alchemist, Bruna, Pirates, SelesnyaV2, Dragons, Dragons 2HG (Dragons added because the draw narrowing below moves
  it again), arms paired by (seed, game index), per-job MTG_LINE_REPAY off/on, ONE pooled batch, threads 8,
  MTG_DUMP_WINS=1 MTG_PGO=0 MTG_RSS_CAP_GB=43, `--game-log-dir mcdragons3/ho/w`; report with
  `python3 <worktree>/logs/mcdragons/abrep.py mcdragons3/ho ho` (expects ho.out and ho_w/; name the dirs to match).
  Compare WALL per deck too (ms on/off), not only units. Estimated ~36 core-h (~5-9 h at 8 threads on this box).
  2-hourly status timer per CLAUDE.md if it runs into 12:30-20:00 UTC.

### Task 2 (NEXT 2, the 138 need-later drops)
- Split (`mcdragons3/nl/split.py` over `mcdragons/gate_new.err`, by the dropped card's name in the pre-plan hand,
  net of the plan's own casts): 138 need-later = 115 in hand before the draw, 23 drawn mid-line (Lathliss,
  Atsushi, ...). 107 of the 115 have Mind Stone on the board: the draw is Mind Stone's crack ({1},{T},sacrifice).
- Code (WIP commit, TEMPORARY lever `MTG_LINE_REPAY_DRAW`, default ON, =0 restores the old barrier exactly; delete
  the lever before landing): library size moved out of `LineRepayDigest`; records keep `lib` and the hand's name
  hashes; `LineRepayFind` accepts a record across a draw only when `LineRepayAcrossDraw` holds (the failing spell's
  name in the record's hand, net of the copies that record and the chain's newer records paid for; activations
  refused).
- Expectation to CHECK, not assume: the Mind Stone crack sacrifices its source, so the battlefield shrinks and
  `LineRepayFind` skips that record (`s.battlefield.size() < r.n`) -- the draw barrier may not be the binding one
  for 107 of the 115. Measure: `bash mcdragons3/gate.sh <bin> new2 8` (gate slice, keeps CAPPED/UNMODELED lines
  too) and `... new2d0 8 MTG_LINE_REPAY_DRAW=0`; rerun split.py on each `.err`. Report count + mechanism; the 23
  drawn cases stay a USER question.

### Task 3 (remaining gate flags) -- two oracle-side fixes in the WIP commit (play-neutral), rest classified
- ORACLE-GAP x8 {creature + Haven rebuy}, two Havens (seed 30103105 T5): ORACLE defect. MinimalPayments dedupes
  results by MidTurnStateKeyOf, which on attachment-free boards forgets WHICH identical copy is tapped; the rebuy's
  own Haven (a plan source, keyed apart in BuildSources) then merged with its twin, the kept option tapped the
  rebuy's Haven and the engine's line was dropped. Fix: `ResultKey` (ManaOracle.cpp) folds each plan source's
  tapped state into the dedupe key (unchanged when t_plan_srcs is null, i.e. every play path).
- ORACLE-GAP x20 {Mind Stone + another cast} (seed 30100045 T4): ORACLE defect. The plan apply's dig (Mind Stone
  crack) taps the first untapped Mind Stone, then pays {1}, and untaps it on failure; the oracle's frontier did not
  know which copy's {T} it was, so the boards after tapping #29 or #30 read alike, the Pareto prune kept one, and the
  rollback then left the other copy up -- the engine's board was never reached. Fix: `AbilityTapSrcScope` around the
  dig's payment at the three sites (TurnSolver.cpp two, AIEngine.cpp one), so the frontier key carries the source.
- Both need the gate slice to confirm (expect ORACLE-GAP 28 -> ~0).
- ENUM-GAP x1 (seed 30101370 T4, {Mountain, Incubator, Atsushi, Greaves, equip}): ENGINE, a depth-3 chain
  (Incubator's {3} must float {R} off Gruul Turf and leave Sol Ring for Greaves); kLineRepayMaxDepth = 2. Raising it
  to 3 is the lossless fix but only after the memo has made the re-pay cheap; measure.
- need-in-plan x4 (seed 30102617 T5 {Haven, Bolt, Lathliss}, Haven's creature-only {R}): not yet chased.
- CAPPED (7 in the gate slice, 31 in pass1; now a gate FAIL): the oracle's per-plan apply cap
  (`MTG_MANA_ORACLE_NODES`, default 2000) -- a CHECKER limit. Next: rerun the capped plans with a larger cap and see
  whether they finish clean; then decide between a larger default and better pruning. UNMODELED: 0 on Dragons.

### Task 4 -- not started
- `src/ai/EngineFlags.h:896` comment and `src/ai/HeuristicArm.h:828` description say depth-1; the code is depth <= 2.
- `MTG_LINE_REPAY` stays (heurarm A/B arm, DEFAULT ON, like MTG_RAMP_FEED_ROUTE). Remove `MTG_LINE_REPAY_DRAW`
  (temporary). Keep or drop `MTG_LINE_REPAY_MEMO_VERIFY` (diagnostic verify gate, the enum-memo precedent) -- say
  which in its comment. Then units (`build/Release/mtg-test`), scenarios, `THREADS=8 bash test/regression.sh --smoke`
  (report moved rows, do not accept).

### USER questions to carry
- (RESOLVED, successor 4) The "23 need-later drops whose card was drawn mid-line" do not exist: `nl/split.py` split
  the hand on ", " and so broke every name containing a comma (Lathliss, Dragon Queen; Atsushi, the Blazing Sky).
  With the hand matched as a string (fixed in `mcdragons3/nl/split.py`), ALL 138 need-later drops on
  `gate_new.err` have the failing card in hand before the draw (115 non-comma names + 17 Atsushi + 6 Lathliss),
  so every one is a mistake-repair case under the USER ruling of 2026-11-03 (the rollback compensates for
  mistakes, never uses a drawn card; drawn cards stay behind the barrier -- LineRepayAcrossDraw already enforces
  it). There is therefore no hedge-rule candidate from this set.
- "Pay generic with colourless first" (USER ruling 2026-11-03: good EXCEPT EDF) ALREADY IS the shared default:
  GenericProvider's ManaSourceRankBase ranks a {C}-only source 5, ahead of every coloured land (StompySurprise s9
  T2, 2026-08-24; Sol Ring and Mind Stone fall on that rung). EDF's {C} holds (MTG_C_SOURCE_HOLD -> 59, and
  EldraziFlickerProvider's MTG_EDF_C_CONSERVE +40) are HUMAN-PLAY ONLY, so autonomous EDF also pays generic with
  {C} first today. An explicit EDF opt-out in autonomous play is a play change for EDF (EDF_C_CONSERVE's comment
  records that demoting {C} in the autonomous loop measured worse: edf_blink_loop_cashes_deckout t4 -> t5). Not
  built: USER question whether to A/B it.

## Successor 4 (2026-11-03) -- state
Scratch `/workspaces/MagicDeckTester/build/scratch/mcdragons4/`. Binary of a56a429ef: `mcdragons4/mtg.new`.
- Units (a56a429ef): 2,435/2,435 pass.
- Identity (`mcdragons4/id`, MEMO_VERIFY=1, DRAW=0, 1,600 games vs ab2's ON arm): 19/20 jobs byte-identical,
  memo_mismatch 0 over 213,929 failure-memo hits. ONE game moved: Bruna seed 1830069 gi68 (T6 both, digest differs).
  The new binary's ON game equals the OFF game; mtg.cur (pre-WIP) fires 220 re-pays (170 depth-2) on it, the WIP 60
  (10). Prime suspect: the WIP's per-option joint prune at depth 2 (lossy). Now behind a TEMP lever
  `MTG_LR_INNER_JOINT` (default OFF) for attribution: `mcdragons4/b68/runb.sh <bin> <tag> [ENV]` (1-game batch, prints
  the digest; ab2 ON = 86bc663936b9ccc5, OFF = 273ccb2f789b92f9).
- WALL (a56a429ef, `mcdragons4/wall/run.sh <bin> <tag>`, ON vs MTG_LINE_REPAY=0, concurrent, loaded box):
  EDF g137 (s1830138) 2,153 s vs 5.0 s; smoke63 (s1064 d5 b20) 4,368 s vs 4.1 s; Dragonstorm ab3 gi45 (s1930046)
  7.7 s vs 5.0 s. The failure memo does NOT fix it:
  * g137: 391 tries, EVERY one a depth-2 SUCCESS of ~6 s ({2}{W}{W} failing, chain {2}{W}/{2}{W}); successes were
    not memoised -> success memo added (re-make from the stored options; VERIFY compares options: ok_mismatch).
  * smoke63: 16,616 tries, 2,044 searches (d1 679, d2 1,365), only 19 fired; ~1.5 s per MinimalPayments call
    (3,265 calls, 110 options each; perf: PayEnum::Verify 14%, BuildSimKeyBase 19%, memmove 10%,
    LandTypeChangedToIsland 8%). Distinct keys, so no memo helps -> "freest board" precheck added: the failing bill
    paid by the REAL payer with the whole chain undone and not re-made; no pay -> no re-making pays (VERIFY counts
    max_mismatch). The raw joint check passes these because it does not see the real payer's holds.
  * With the inner joint prune OFF, smoke63 would also lose its 86,520 inner prunes (each a recursion with a
    MinimalPayments call) -- MEASURE before keeping it off.
- Build of the above queued: log `mcdragons4/build2.log` (target mtg + mtg-test).
- Next: id run again (VERIFY: memo/ok/max mismatches 0; gi68 back to 86bc...), wall run, then decide the inner prune
  (find why it is not necessary on gi68, or drop it), then the held-out A/B.
- The 23 "drawn" need-later drops do not exist (split.py comma bug; see USER questions above).

### Successor 4 -- results after v2/v3 (READ THIS FIRST, successor 5)
Branch head: the WIP commit "success memo; inner joint prune OFF; precheck OFF; temp whole-float probe lever" (on top
of f0d43afc9 / 484acb761). Binaries: `mcdragons4/mtg.v2` (success memo, prune off, precheck on),
`mcdragons4/mtg.v3` (= HEAD src: precheck behind MTG_LR_MAX_PRECHECK, whole-float behind MTG_LR_WHOLE_FLOAT).
TEMP levers still in the code (delete before landing): MTG_LINE_REPAY_DRAW, MTG_LR_INNER_JOINT, MTG_LR_MAX_PRECHECK,
MTG_LR_WHOLE_FLOAT.

Correctness (identity slice `mcdragons3/id/id.json`, 1,600 games vs ab2 ON, DRAW=0):
- v2 under MEMO_VERIFY: 20/20 jobs byte-identical; memo_mismatch 0 (213,832 hits), ok_mismatch 0 (58,002 success
  hits), ok_stale 0. Bruna gi68 back to ab2's 86bc663936b9ccc5.
- Inner joint prune (MTG_LR_INNER_JOINT=1) is LOSSY: alone it moves Bruna gi68 to the OFF game (fired 220 -> 60).
  Likely mechanism (unverified): JointPays runs in the FAILING spell's pay scope, so mana only an earlier chain bill
  may spend (Troyan's big-spell pool, MV>=5; subtype-restricted sources) is invisible to it. The TOP-LEVEL joint
  check uses the same function, so it may be lossy the same way (pre-existing; check with a verify counter).
- Freest-board precheck (MTG_LR_MAX_PRECHECK=1) is LOSSY: 83 verify mismatches (the real payer is not monotone in
  free sources). Off.
- Whole-float (MTG_LR_WHOLE_FLOAT=1, no set-aside enumeration in the re-made payment's options) is LOSSY: 1 of 1,600
  games moved, AccomplishedAlchemist seed 1830146 gi145 (job off100) T7 -> T8 (slower).

Wall (`mcdragons4/wall/run.sh`, ON vs MTG_LINE_REPAY=0, same batch):
| build | g137 on/off | smoke63 on/off | ds45 on/off |
| a56a429ef (fail memo + inner prune) | 2,153 / 5.0 s | 4,368 / 4.1 s | 7.7 / 5.0 s |
| v2 (+ success memo, prune off) | 1,666 / 41.5 s (box load ~68) | 6,214 / 38 s | 41 / 50 s |
| v3 + MTG_LR_WHOLE_FLOAT=1 | 5.0 / 4.1 s | 16.7 / 3.9 s | 4.2 / 4.4 s |
Identity-slice EDF wall: whole-float 354 s vs ab2 ON 9,900 s; line-repay ms 58 s vs 10,566 s (v2, verify).
smoke63 with whole-float is still 4.3x: 2,034 failing searches (d1 669, d2 1,365), 69,678 MinimalPayments calls --
the depth-2 recursion with no sound inner prune. g137 (391 depth-2 successes, 185 served by the success memo) is fixed.

WHERE THE COST IS: MinimalPayments' set-aside / leftover-split enumeration (PayEnum::Verify, BuildSimKeyBase, board
copies) -- an oracle-grade table used in play; ~0.4-1.5 s per call on Aura'd-land EDF boards, ~1,000x the
whole-float table.

RECOMMENDATION (successor 4): a budget-staged admission order, the CLAUDE.md "waves" shape. Wave 1 = the whole-float
options (cheap, always run). Wave 2 = the full set-aside table, only if wave 1 found nothing, with its work CHARGED
through TurnSolver::ChargeDecisionUnits to DecisionBudget() and cut when that budget is spent. At an unlimited budget
DecisionBudget() is null: nothing is charged or cut, so it is complete. A cut is never memoised as a failure. Plus a
SOUND inner prune: JointPays evaluated per bill's own scope, or skipped when the board holds a scoped source. That
removes most of smoke63's 69,678 recursion calls.
OPEN for the USER/coordinator: the "finish the turn first" rule says a cut must not fall on the decision's own turn
(the AA route precedent: charge always, cut only on later turns). If wave 2 is never cut on the own turn, smoke63's
own-turn failures still cost the full table. That is a policy call: report it, do not settle it.
Option (a), an exact necessary condition for the failing class, was tried twice and both attempts were lossy (the
precheck and the inner prune). Neither is a candidate as built.
Not done: held-out A/B (blocked on wall), draw-narrowing gate slice, CAPPED rerun, ENUM-GAP depth-3 write-up, smoke.
Units on a56a429ef: 2,435/2,435 pass (`mcdragons4/ut1.log`); `ut2.log` = v2.

## Successor 5 (2026-11-03) -- two-pass LINE_REPAY (checkpoint; final results below when done)
Scratch: `build/scratch/mcdragons5/`. Code (uncommitted until the squash): ManaOracle `ProbeCharge` (priced
MinimalPayments: 1 unit/probe via ChargeDecisionUnits, cut when DecisionBudget() is spent); ManaPayment
`LineRepayPass` (pass 1 whole-float unpriced, pass 2 full table priced; a cut is never memoised; `fail_p1` memo
skips a proven-empty pass 1 on repeat; pass 2 skipped when every depth's joint is short), `LineRepayScopeFree`
(the joint prune, top-level and per-option at depth 2, only on a board with no context-restricted mana -- exact;
Bruna gi68 fired 220 / 86bc663936b9ccc5 with it ON). All four temp levers deleted; the draw barrier kept.
Baseline for identity: `mcdragons5/idbase` = mtg.v2 (full table, draw ON) over `mcdragons3/id/id.json`.
