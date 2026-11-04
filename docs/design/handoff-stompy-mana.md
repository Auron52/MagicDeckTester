# StompySurprise mana-clean -- working checkpoint (wip/mc-stompy)

Branch `wip/mc-stompy` (worktree `.claude/worktrees/mc-stompy`), based on origin/phase-1-2-deck-analyzer bac8b2acb
(src ac353031b3ab). The gate pass being fixed: `logs/mana_clean/pass1-stompysurprise/report.md` (src 7ec55bb505a8,
31,000 games). NOTE: src moved ~50 commits since that pass; play differs (seed 30000581 wins T3 now, T4 then), so
"before" numbers on the current src come from the oracle toggles below, not from the pass.

## Mechanisms found

1. ORACLE-GAP (pass 9,463) -- the oracle could not reproduce the engine's joint-prepay outcome when Natural Order
   sacrifices an Elf that untapped scaled dorks (Priest of Titania, Elvish Archdruid) count: the prepay taps every
   scaled dork BEFORE the sacrifice (one Elf more each) and pays the next cast off the float; the oracle's table
   offered at most one tap past a paying set. FIX (oracle-only): the frontier records the cast's victim
   (`Hook::f_sac_victim` <- `g_pay_sac_victim`, ManaPayment.cpp oracle hook), and `PayEnum::ExtendScaledAhead`
   (ManaOracle.cpp) extends every paying set with any number of further taps of scaled dorks counting the victim.
   Toggle `MTG_ORACLE_SCALED_AHEAD` (default ON). Seed 30004080 T4: ORACLE-GAP 26 -> 0.
2. ENUM-GAP (pass 459; 302 involve Turntimber Symbiosis) -- the paying realisation rests on a creature Turntimber
   Symbiosis put off the top seven (hidden), counted by a Priest's tap. `TapsHiddenCard` only knew taps OF a
   hidden card, and its top-run test stopped at the first looked-at card that went to the bottom. FIX
   (oracle-only): the hook records every creature a scripted scaled tap counted (`Hook::scaled_counted`), and the
   top-run also takes the root library's leading cards above the depth where the end library resumes unbroken.
   Toggle `MTG_ORACLE_HIDDEN_SCALED` (default ON). Seed 30008581 T4: ENUM-GAP 6 -> 2, HIDDEN-INFO 4.
3. APPLY-DROP (pass 19,032, depth1 16,542) -- (a) Natural Order's victim left untapped while a land paid
   (seed 31000062 T3, `[vicdbg]`: victim=11 published, payment = Forest + Archdruid); under investigation with the
   unit test "payment: Natural Order's victim pays before a land". (b) scaled dorks not tapped ahead of the
   sacrifice by the sequential payer (seed 31000152 T4 {NO + Craterhoof + Llanowar}) -- the engine-side twin of 1.
4. DOMINATED-CREATURE (pass 15,487) -- the sequential payer taps lands, then one-mana Elves, then the scaled dork
   last, so the scaled dork's surplus floats unused while attackers were tapped (seed 30004002 T3); most involve a
   breakpoint continuation's casts. Not fixed.
5. CAPPED (pass 4,392) -- 10-source boards: seed 30003677 T4 {Call of the Wild + NO}: 257 pay1 options x ~56 pay2
   options vs the 2000-apply cap. Not fixed (see report for the candidate prune).

## State
- TEMP diagnostics to delete before landing: `[vicdbg]` (TurnSolver.cpp apply_one, MTG_VICDBG), `MTG_TAPDBG_ALL`
  (ManaPayment.cpp TapDbgReal).
- Scratch: /workspaces/MagicDeckTester/build/scratch/mc-stompy/. Sample run: logs/mana_oracle/stompy-s1 (240 games,
  seed 31000000, both oracle fixes ON): APPLY-DROP 87, APPLY-DROP-ORDER 13, DOMINATED-CREATURE 53, ORACLE-GAP 0,
  CAPPED 0, ENUM-GAP 0; play identity byte-identical.

## Handoff (2026-11-04 03:33 UTC, context cap)

Running: a build of this worktree queued on `build/scratch/buildq.sh` (log
`/workspaces/MagicDeckTester/build/scratch/mc-stompy/build2.log`; it waits for a global build slot, then builds
`build/Release` = `/workspaces/MagicDeckTester/build/wt/mc-stompy/Release`). It compiles the toggles, the TEMP
`MTG_TAPDBG_ALL`, and two unit tests. Leave it; if it died, rerun
`cd .claude/worktrees/mc-stompy && bash /workspaces/MagicDeckTester/build/scratch/buildq.sh`.

Code locations (this branch):
- `src/ai/ManaOracle.cpp`: `PayEnum::ahead` + `ExtendScaledAhead()` (just before `ExtendFed`); `ScaledAheadClasses()`
  (just before `MinimalPayments`); `MinimalPayments(..., int sac_victim)`; `FrontierRec::sac_victim` (folded into the
  frontier prune key, `"v"+victim`); `TapsHiddenCard` (scaled_counted loop + the unbroken-run top test).
- `src/ai/ManaOracle.h`: `Hook::f_sac_victim`, `Hook::scaled_counted`, `MinimalPayments` default arg.
- `src/ai/ManaPayment.cpp`: oracle hook frontier capture `h.f_sac_victim = g_pay_sac_victim`; scripted-tap loop records
  `h.scaled_counted`.
- Tests: `test/unit/test_mana_oracle.cpp` last two cases. The "scaled dorks ... tap ahead" case is WEAK (the plain
  table already contains {Archdruid, Priest, Priest} on a unit board via ExtendSurplus on {Priest, Priest}; why the
  real seed-30004080 table lacked it is not explained -- the plan-source keying `t_plan_srcs` of the victim is the
  first suspect). The "victim pays before a land" case is the APPLY-DROP 3(a) diagnostic: if it FAILS, the
  sac-fodder-first rank (-1000, ManaPayment.cpp ~2250 `g_pay_sac_victim`) is being bypassed for a scaled-dork bill;
  run it with `MTG_TAPDBG=1 MTG_TAPDBG_ALL=1` to see the `[nddbg]` candidate keys and find the path that picks the Forest.

Next steps, in order:
1. When build2 lands: `build/Release/mtg-test -tc='*mana oracle*,*ENUM-GAP*,*victim pays*'`. Fix 3(a) if the victim test
   fails (engine; needs smoke + scoped regression + held-out A/B per CLAUDE.md).
2. Measure the oracle fixes on one binary: `scripts/mana_oracle.py stompysurprise --games 2000 --seed 31000000 --threads 8
   --bin build/Release/mtg` with `MTG_ORACLE_SCALED_AHEAD=0 MTG_ORACLE_HIDDEN_SCALED=0` vs default (both arms; report
   ORACLE-GAP / ENUM-GAP / HIDDEN-INFO / CAPPED and oracle ms). Play must stay byte-identical (the script checks).
3. 3(b) engine tap-ahead before a sacrifice: LINE_REPAY (wip/mc-breaching, `LineRepayTry`) re-makes the previous payment
   over `MinimalPayments`; passing the earlier cast's victim into that table would offer the tap-ahead -- coordinate
   with the LINE_REPAY owner rather than adding a second mechanism.
4. Delete the TEMP diags (`[vicdbg]` in TurnSolver.cpp apply_one after `_esp.End(paid_ok)`, `MTG_TAPDBG_ALL`) before landing.
5. CAPPED: candidate sound prune = reachability dominance in the frontier Pareto prune (`ReachesByTapping`), valid only
   with the tap-ahead extension present; unproven, do not land without a soundness argument + test.

## For the LINE_REPAY owner (wip/mc-dragons): what a re-pay needs for the Stompy tap-ahead (APPLY-DROP cause 2)

Not built here (one re-pay mechanism, owned on wip/mc-dragons). Shape: seed 31000152 T4, board {Turntimber, Forest#15,
Priest of Titania #42/#43/#44}, plan {Natural Order + Craterhoof Behemoth + Llanowar Elves}. The engine pays Natural
Order's {2}{G}{G} exactly (the victim, a Priest, first: MTG_SAC_FODDER_PAYS) and Craterhoof's {5}{G}{G}{G} is short:
the surviving Priests each tap for one Elf fewer once the cost has eaten the victim. The oracle's depth-1 rollback
pays it by re-making Natural Order's payment as pay2[Priest #42 + Priest #43] (tap ahead of the sacrifice, the
surplus floats -- CR 106.4) and then pay3[Forest + Priest #44]. MTG_PAY_ROLLBACK (forced on) does not recover it.
What LineRepayTry needs so that its depth-1 re-make covers this:
1. **Record the victim.** `LineRepayRec` gets `int sac_victim` (= `g_pay_sac_victim` when `LineRepayRecord` runs, inside
   the cast's `PaySacVictimScope`), its battlefield index in `pre`, and the victim's Permanent as it stood.
2. **Find the record across the sacrifice.** Between the two payments the cast's cost removed the victim from the
   battlefield (and Natural Order's resolution added a creature past `n`). `post` digests the first `n` permanents,
   so the victim's removal shifts the prefix and `LineRepayFind` never matches: the digest must skip the victim's slot
   (`n - 1` permanents on the later board, the victim's index known) and every other field must still match.
3. **Re-make on the board WITH the victim.** `LineRepayOnPastBoard` must re-insert the victim at its index (it is what
   the scaled taps count -- Priest of Titania / Elvish Archdruid read the live Elf count), make the option, then remove
   it again (as the cost did) before trying the failing bill.
4. **Re-make under the victim scope and with the victim's table.** Run the re-make inside
   `PaySacVictimScope(rec.sac_victim)` and build its table with `MinimalPayments(..., sac_victim)` (this branch's
   argument; `ManaOracle.cpp` `ScaledAheadClasses` / `PayEnum::ExtendScaledAhead`, gated `MTG_ORACLE_SCALED_AHEAD`): it
   offers any number of further taps of scaled dorks that count the victim, which is exactly pay2 above. NOTE the
   extension is skipped under `whole_float` (pass 1); either allow it there for a victim record or let pass 2 carry it.
5. **Signature clash on rebase:** wip/mc-dragons added `ProbeCharge* charge` as MinimalPayments' 8th parameter; this
   branch added `int sac_victim` there. Put `sac_victim` after `charge` when the two meet.
6. Check that a record whose cast sacrificed a creature is not pushed as a BARRIER (`undoable=false`) for that reason.

## CAPPED (task 5, successor 2): left as is, no lossless prune found

1,000 games seed 31000000 (current src): CAPPED 631, of which 480 are ONE game (seed 31000265 T4, board {Forest,
Turntimber, Llanowar Elves, Fyndhorn Elves, Elvish Mystic, Priest of Titania x2, Elvish Archdruid}: plans over
{Turntimber Symbiosis, World War Hulk, Natural Order}) and 35 another (seed 31000742, the same shape with Sol Ring).
MTG_ORACLE_SCALED_AHEAD does not cause it (631 with it on and off). The width is three mana-identical flat Elves under
three NAMES (each `{T}: Add {G}`, a 1/1 Elf Druid) beside two scaled classes: the table enumerates them as three
classes. The candidate sound collapse is a cross-name source class for mana-identical flat dorks, valid only if every
outcome comparison downstream (CoversBoards' fast path and Resources keys compare permanents by m_number/name) treats a
tapped Llanowar and a tapped Fyndhorn as the same resource -- they are not the same board for the engine's own state
key, so this is NOT provably lossless as the comparisons stand. Not built (stop rule: no proof in hand). The previous
candidate (reachability dominance in the frontier Pareto prune via ReachesByTapping) is likewise unproven.

## Successor 2 progress (2026-11-04 ~04:00 UTC)

- Task 1, oracle switches, same binary (head 0df9103fc), 1,000 games seed 31000000, threads 8:
  switches ON: ENUM-GAP 7, HIDDEN-INFO 5, ORACLE-GAP 0, CAPPED 631, APPLY-DROP 527, APPLY-DROP-ORDER 52,
  DOMINATED-CREATURE 504; switches OFF: ENUM-GAP 12, HIDDEN-INFO 0, ORACLE-GAP 0, everything else identical.
  Play digest identical across the switches (12544728d86155a3, avg 4.0620) and oracle on vs off (identity=True).
  So MTG_ORACLE_HIDDEN_SCALED moves 5 ENUM-GAP -> HIDDEN-INFO; MTG_ORACLE_SCALED_AHEAD moves NOTHING on this sample
  (ORACLE-GAP 0 either way; seed 30004080 also has ORACLE-GAP 0 with it off on the current src -- the pass's 26 were
  removed by something else in that commit or upstream, most likely the victim folded into the frontier prune key).
  The extension is kept as the table a re-pay across a sacrifice needs (LINE_REPAY section); its unit test now
  exercises a case the plain table really lacks (two DIFFERENT scaled classes tapped ahead beside a paying set).
- Task 2 FIXED (engine): MTG_REFINE_VICTIM_SPENT (heurarm lever, default ON) -- RefinePaymentImpl read the victim's
  untapped state as a released source; the refine kept {Forest, Archdruid} over {Mystic (victim), Archdruid}.
  Seed 31000062 T3: MISS 9 -> 0, engine_drops 9 -> 0. Unit test "victim pays before a land" (needs
  deck_has_mana_untapper=false, as Stompy) fails with =0, passes with =1.
- Task 3 attempt 1 (engine): MTG_SCALER_COVERS_LINE (heurarm lever, default ON) -- DecisionProviders.cpp
  ScalerCoversLine: a scaled dork whose current yield covers everything the line still owes (>= 2) ranks first
  among creatures, ahead of the plan bias's food-last band. Unit test "a scaler whose tap covers the rest of the line".
- A/B tool: build/scratch/mc-stompy/ab_lever.py (pooled, one game per job, per-arm heurarm flags).

## Handoff (successor 2 -> 3, 2026-11-04 04:30 UTC, context cap)

Code (uncommitted before this WIP commit; all in it now):
- Task 2 FIX: `src/ai/ManaOracle.cpp` `RefineVictimSpentOn()` (just above `RefinePaymentImpl`), `victim_idx` block after
  `pe.Dfs(0, col, total)`, and `consider()`'s `vi` handling; memo key bit `1 << 13` in `RefinePayment`. Lever slot
  `heurarm::REFINE_VICTIM_SPENT` (`src/ai/HeuristicArm.h`, end of the enum + kNames). Unit test "payment: Natural
  Order's victim pays before a land" (test/unit/test_mana_oracle.cpp) fails at =0, passes at =1. Seed 31000062 T3
  oracle: MISS 9 -> 0, engine_drops 9 -> 0. NOT yet smoke/regression/A-B tested.
- Task 3 attempt 1: `src/ai/DecisionProviders.cpp` `ScalerCoversLine()` (just above `GenericProvider::ManaSourceRank`),
  called only inside the `if (pt)` scaled branch; slot `heurarm::SCALER_COVERS_LINE`. Unit test "a scaler whose tap
  covers the rest of the line" fails at =0, passes at =1. BUT on seed 30004002 T3 the flag does not move (oracle
  DOMINATED-CREATURE 1 at =0 and =1): plan {Elvish Mystic + Elvish Archdruid#9}, real play pays Archdruid#9's
  {1}{G}{G} with Llanowar #36 THEN Archdruid #8 (3 Elves), and the payment refine then re-makes it as Archdruid alone
  (`[paydbg] ... taps:` empty line after it); Llanowar then pays Worldly Tutor. Open question being traced: is
  CurrentPlanTraits() null at that payment (the rule sits inside `if (pt)`), or g_pay_need_live false, or is the
  oracle's engine-side apply missing the refine? TEMP trace `MTG_SCLDBG` (3 sites, DecisionProviders.cpp, marked
  `TEMP DIAG`) answers it.
- Unit suite at build6 (before MTG_SCLDBG): 2547/2547 pass.
- Tap-ahead unit test rewritten (two different scaled classes beside a paying set); fails at
  MTG_ORACLE_SCALED_AHEAD=0, passes at 1.

Running at handoff (leave it; mine, cheap): background build8 + trace,
`build/scratch/mc-stompy/build8.log` then `build/scratch/mc-stompy/scl.txt` (MTG_SCLDBG=1 MTG_TAPDBG=1 seed 30004002).

TEMP diagnostics to delete before any landing commit: `[vicdbg]` (TurnSolver.cpp apply_one, MTG_VICDBG),
`MTG_TAPDBG_ALL` (ManaPayment.cpp TapDbgReal), `MTG_SCLDBG` (DecisionProviders.cpp, 3 sites).

Next steps, in order:
1. Read scl.txt. If `[scldbg] NO pt` at Archdruid#9's payment: move the ScalerCoversLine test out of `if (pt)` (pass
   pt nullable for the act pips) and return a rank strictly below the flats in both branches (with pt: F vs flats
   F+1; without pt flats are F, so it needs F-1 = 63 -- check no land rank 63 collides in practice, see
   DecisionProviders.cpp ~5909). If pt is present and the rule fires but the oracle still flags, the flag is the
   oracle's engine-side apply lacking the refine -- check `PayRefineAfter`'s `refine` predicate under the oracle hook.
   Stop rule: this is attempt 1; one more concrete attempt, then document and move on.
2. Delete MTG_SCLDBG; rebuild; full unit suite; `THREADS=8 bash test/regression.sh --smoke` (list moved decks, do NOT
   accept); scoped `--deck=` regression on moved decks.
3. Held-out A/B on moved decks, pooled: `python3 build/scratch/mc-stompy/ab_lever.py --out build/scratch/mc-stompy/ab2
   --seed 32000000 --games 300 --bin <mtg copy> --threads 8 --decks stompysurprise,... --arm on
   MTG_REFINE_VICTIM_SPENT=1 MTG_SCALER_COVERS_LINE=1 --arm vic0 MTG_REFINE_VICTIM_SPENT=0 --arm scl0
   MTG_SCALER_COVERS_LINE=0` (per deck: turns, units, ms, slower/faster games listed). Bar: net <= 0 per deck; every
   slower game recovers at `--depth 8 --budget-ms 0` (MTG_NC_SEARCH=1 for clairvoyance); perf stat instructions per
   deck with MTG_PGO=0 both arms; justify > ~1%.
4. Final gate: `scripts/mana_clean.py` on StompySurprise, fresh seed block, >= 5,000 games; squash; do NOT push.

## Successor 3 progress (2026-11-04 ~05:30 UTC)

- Task 3 settled with attempt 2. Trace (`[applydbg]`/`[refdbg]` TEMP, now deleted) of seed 30004002 T3, plan {Elvish
  Mystic, Elvish Archdruid} on {Forest, Llanowar Elves, Elvish Archdruid#8}: the per-cast payer never ran -- the
  WHOLE-TURN PREPAY (TurnSolver::BatchPrepayMainCasts) tapped all three sources before Mystic resolved (Archdruid for
  2), so Mystic's and Archdruid#9's payments came off the float. Attempt 1 (MTG_SCALER_COVERS_LINE, a per-cast rank)
  therefore could not reach it, and is REVERTED (no repro it fixes; less branching in the payer).
  FIX: MTG_PREPAY_SCALER_GROWN (heurarm lever, default ON, PP_SCALER_GROWN) -- the prepay declines when the batch
  casts a creature an untapped subtype-scaled dork counts (Priest of Titania, Elvish Archdruid; not the life-scaled
  Alchemist, which has its own decline) and the board without those dorks cannot pay the batch; the per-cast payer
  then taps the dork after the food lands. Seed 30004002 T3: DOMINATED-CREATURE 1 -> 0 (DOMINATED-LAND 5 unchanged).
  Unit test "prepay: declines when the batch grows a scaled dork it needs". Only StompySurprise and
  SelesnyaLifegain hold such dorks.
- TEMP diagnostics all deleted ([vicdbg], MTG_TAPDBG_ALL, MTG_SCLDBG, [applydbg], [refdbg]).
- Narrowed (build13): the decline fires only when the joint solve (scratch copy, check stood down) would tap a
  creature that is not a scaled dork; a prepay whose hold ladder already keeps the flat bodies up runs as before
  (test_stompy_order's HOLD_RELEASE_YIELD tests; the first pins the lever off since it tests the ladder's off arm).
  Units 2547/2547. Repros: 30004002 T3 DOMINATED-CREATURE 1 -> 0; 31000062 T3 APPLY-DROP 9 -> 0 (vic lever).
- Smoke (build13, THREADS=8, NOT accepted): 8 rows moved, all StompySurprise / SelesnyaLifegain:
  stompy d0 4.8280->4.8170, d3 4.98 = (play differs), d5 4.00 =, stompy2hg d5 4.30->4.26; selesnyav2 d0
  5.0750->5.0810, d3/d5/2hg-d5 = (play differs).
- Scoped regression (--deck=stompy,stompy2hg,selesnyav2,selesnyav22hg, NOT accepted): stompy d0 s2002 4.871->4.858,
  d3 s2002 4.16->4.14, d5 s2002 4.118->4.100, d5 s3003 4.068->4.056, s30001 d3/d5 =; selesnyav2 d0 s2002
  5.044->5.046, every other selesnya row = (play differs).
- A/B running: build/scratch/mc-stompy/ab2 (seed 32000000, 300 games, arms on / vic0 / grown0).
- A/B ab2 (build13, seed 32000000, 300 games/deck, play settings, pooled 1800 jobs): StompySurprise on 1215 turns;
  vic0 1215 (2 moved, net 0); grown0 1223 (36 moved, 8 slower / 0 faster with the lever OFF: the fix is 8 turns
  better, never worse). SelesnyaLifegain on 1376; vic0 1376 (0 moved); grown0 1376 (33 moved, net 0).
  Units on vs grown0: Stompy 991,930 vs 1,004,065; Selesnya 1,184,981 vs 1,186,532.
- Slower games: none in the A/B. The smoke/regression selesnyav2 d0 slower games (smoke s1001 gi51/130/174,
  regression s2002 gi5/349/514) all reach their old turn at --depth 8 --budget-ms 0 (build/scratch/mc-stompy/rec).
- perf stat instructions:u (150 games/deck, seed 32000000, same binary both arms): build13 (probe) Stompy +2.66%,
  Selesnya +3.66%; build15 (decline judged at the end of the solve) Stompy +1.19%, Selesnya +2.70%, play identical
  to build13. Selesnya: 12/150 games moved, units equal, 7,466 of 569k prepay calls declined -> the cost is the
  per-call check. build16 makes the check lazy (one battlefield scan unless a flat creature was tapped).
- build16 (lazy check): Stompy +0.89% instructions, Selesnya +2.18%; play identical to build13; units 2547/2547.
  The Selesnya residual is the declines themselves (wasted joint solve + per-cast payments, 7,466 of 569k calls),
  for net 0 turns on 300 games -> surfaced to the USER (scope or accept), not decided here.

## Handoff (successor 3 -> 4, 2026-11-04 ~10:00 UTC, context cap)

Branch squashed into reviewable commits on bac8b2acb (backup of the pre-squash WIP: branch `wip/mc-stompy-presquash`;
trees identical except one unused test include):
1. 8a03c7820 fix(mana oracle): MTG_ORACLE_SCALED_AHEAD (`PayEnum::ExtendScaledAhead`, ManaOracle.cpp:1386) +
   MTG_ORACLE_HIDDEN_SCALED (ManaOracle.cpp:2639, `TapsHiddenCard`). Oracle-only, play identical.
   1,000 games seed 31000000: ENUM-GAP 12 -> 7, HIDDEN-INFO 0 -> 5, all else identical.
2. ed6b143d8 fix(payment refine): MTG_REFINE_VICTIM_SPENT (`RefineVictimSpentOn`, ManaOracle.cpp:1765; memo bit 13).
   Seed 31000062 T3 APPLY-DROP 9 -> 0. A/B: Stompy 2 moved net 0; Selesnya 0 moved.
3. b1994b5aa fix(prepay): MTG_PREPAY_SCALER_GROWN (TurnSolver.cpp ~58617-58700, end of BatchPrepayMainCasts, before
   MTG_PREPAY_COLOURS). Seed 30004002 T3 DOMINATED-CREATURE 1 -> 0. A/B (seed 32000000, 300 g/deck): Stompy lever-on
   1215 vs off 1223 turns (8 slower with it off, 0 faster); Selesnya 1376 = 1376 (33 moved). perf instructions:u
   (150 g/deck): Stompy +0.89%, Selesnya +2.18%.
4. docs commit (this file).
MTG_SCALER_COVERS_LINE reverted; every TEMP diag deleted. Units 2547/2547 (build16 = this src tree).
Smoke/regression (NOT accepted; rows listed above) moved only stompy* and selesnyav2* rows. Slower games: none in the
A/B; the selesnyav2 d0 smoke/regression slower games all recover at --depth 8 --budget-ms 0.

Running: nothing. build/PGO is built and stamped for HEAD:src 63c23d7044e2 (trained on decks/StompySurprise only).

Remaining, in order:
1. Gate (task 4): `cd <worktree> && MTG_RSS_CAP_GB=43 python3 scripts/mana_clean.py run stompysurprise --games 5000
   --threads 8 --state /workspaces/MagicDeckTester/build/scratch/mc-stompy/mana_clean_state.json --name stompy-mc-s3`
   (do NOT set MTG_PGO=0: the gate refuses without a fresh PGO; the scratch --state is a copy of the primary's
   test/mana_clean.json, next block games 31000-35999). Wait on its PID. Report the class table vs the 1,000-game
   sample (ON column of "Successor 2 progress") from logs/mana_clean/stompy-mc-s3/report.md.
2. Rebuild Release once and run the unit suite (the squash dropped an unused `#include "ai/PlanContext.h"` from
   test/unit/test_mana_oracle.cpp; not yet compiled).
3. Do NOT push; the coordinator lands it.

USER questions to surface: (a) MTG_PREPAY_SCALER_GROWN costs SelesnyaLifegain +2.18% instructions for net 0 turns
(300 games); Stompy +0.89% for -8 turns. Accept as is, or scope it (e.g. to providers where it measures a gain)?
(b) APPLY-DROP-ORDER (cast order is the USER's) was not analysed this round.
