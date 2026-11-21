# Bruna APPLY-DROP: handoff (2026-11-21 ~04:00 UTC, context cap)

## Status in one line
Static analysis only. No code changed, nothing built, no repro has run yet. The lane has been congested
(rock agent's chain3 held all 4 slots for ~3 h, then the mana-clean gate's ~30 GB RSS memory-blocked every lane job).

## Worktree / branch
- Worktree `/workspaces/MagicDeckTester/.claude/worktrees/bruna-apply-drop`, branch `wip/bruna-apply-drop`, base
  origin/phase-1-2-deck-analyzer c3853c32f, `HEAD:src` = 9cf4511809181f0cd0ae54aba836797288ef56db (= the gate's src).
- Only this handoff file is committed (git add -f; logs/ is gitignored).

## Queued jobs (mine; leave them running)
- BUILD: `bash scripts/box_lane.sh --slots 4 --cap-gb 2 -- ./build.sh` (lane pid 4157769, from the worktree).
  Log `/workspaces/MagicDeckTester/build/scratch/bad_build1.log`; finished when it holds a line `rc=<n>`.
  (The earlier `bad_build0.log` attempt was killed by me while memory-blocked; ignore it.)
- BASELINE REPROS: `bash scripts/box_lane.sh --slots 3 --cap-gb 2 -- build/scratch/bad/repros1.sh <base> <out>`
  (lane pid 4157785). Log `/workspaces/MagicDeckTester/build/scratch/bad/repros_base.log`; finished when it holds `DONE`.
  - Binary: `build/scratch/bad/base/mtg` = a copy of batch-1120b's `build/PGO/mtg` (src 9cf45118, the gate's binary;
    decks/Bruna and src identical to this worktree).
  - Script `build/scratch/bad/repros1.sh` runs 11 top APPLY-DROP signatures in three arms, 4 at a time, through
    `build/scratch/bad/repro.sh` (MTG_MANA_ORACLE=3 + MTG_MANA_ORACLE_TURN): `r_base/plain` (as shipped),
    `r_base/tap` (MTG_TAPDBG=1: the engine apply's taps), `r_base/lka` (MTG_LINE_KEEP_ALL=1). Per-run files
    `r_base/<arm>/s<seed>_T<t>.{out,err}`; the log prints `APPLY-DROP=<n>` per run.
  - Seeds (turn seed gi): 2 30051968 51968 | 2 30054921 54921 | 4 30054069 54069 | 2 30051257 51257 |
    4 30051794 51794 | 2 30053757 53757 | 2 30051119 51119 | 2 30051713 51713 | 3 30051079 51079 |
    3 30051277 51277 | 5 30051111 51111.
- Classifier: `python3 build/scratch/bad/cls.py <oracle .err> [N]` (families, depth/rescued, top drop keys).
- Gate raw flags (read-only, do not touch that worktree):
  `/workspaces/MagicDeckTester/build/wt/batch-1120b/scratch/mana_clean/claude-queue-20261120c/on.0.err`.

## Census of the gate's Bruna APPLY-DROP (1,891 flags, ~95 games, ~207 (game,turn) decisions)
By game, the family keys: opener(Wish/Armory)+Sage 67, Sage (no opener) 56, Sol Ring in plan 35, Wild Growth 25,
opener 25, opener+Sol Ring 20, plain 11, others small. So about 45% of the dropping games are tutor lines.
Depth: depth1 1,488 (344 rescued by MTG_PAY_ROLLBACK forced on), depth2 367, depth3 33, capped 3.
Top keys (flags/games): Wings dropped from {Bruna, Wings} 299/21; Wish dropped from {Bruna, Wish} 163/33;
Wings from {Wings, Wild Growth} 107/9; Armory from {Armory + Wings} 93/14; Wild Growth from {Wings, WG} 82/7;
Wish from {Wish, Sol Ring} 81/14; Wings from {Wings, Sol Ring} 64/6; Sage from {Sage, Sol Ring} 52/22.

## Findings (static analysis)
1. **Tutor lines lose the line-aware generic order.** Glittering Wish and Open the Armory are `tutor_to_hand`, which
   `TurnSolver::ParamKeyedDrawClass` counts as a breakpoint opener (`CastOpensUnseen` / `LineOpensUnseenCasts`,
   TurnSolver.cpp ~70766-70777). That sets `g_line_opens_unseen` for the whole line, and `LineGenericOrderLive()`
   (src/core/SpellEffects.h ~1635: `HumanPlayActive() || (AutoLineGenericOn() && !g_line_opens_unseen)`) is then false.
   The float drain's surplus-first order (SpellEffects.h ~49267), ConsumeFloatingAny's line order and
   LineDemandAnyPipColor (SpellEffects.h ~48825/48896) all stand down, so a generic pip spends WUBRG-first.
   The guard exists for TreasureHunt s3003 gi96 ({Hunt, Hunt}: keeping the second Hunt's {U} lost Land's Edge's {R}).
   `MTG_UNSEEN_KEEP_SCARCE` (SpellEffects.h ~49329, `UnseenScarceNamedMask`) only covers state.floating_mana with NO
   untapped source of the colour, and only the pre-payment float drain, not the payment's own local float.
   - Example s30051277 gi51277 T3, `{land=Razorverge Thicket + cast:Glittering Wish + cast:Bruna}`. Board: Sol Ring,
     Somberwald Sage, Azorius Chancery, plus the Thicket drop. Eight mana for an eight-mana line, so every unit must
     go to the right place. Hypothesis, NOT yet traced: Bruna is paid first. `burst_lead` (ManaPayment.cpp ~4312)
     keeps Sage = W (shortfall 0 either way). Sage {W}{W}{W} pays W W plus 1 generic, the Chancery's {U} pays U and its
     {W} floats, and the WUBRG generic drain spends that {W}. The last generic taps Sol Ring and a {C} floats. The Wish
     then has Thicket plus {C} for {G}{W}: dropped. The oracle's line spends Sol Ring's {C}{C} on the generic and keeps
     Thicket {G} + Chancery {W} for the Wish. Confirm with `r_base/tap/s30051277_T3.err`.
   - The same shape fits {Wish, Sol Ring} (T2 s30051968: Sol Ring's {1} off the Forest, not Remote Farm's {W}{W}).
     That one is a source choice, not a float order.
   - Prior art: docs/design/handoff-bruna-mana.md says fix 2 (`MTG_PAY_LINE_CRITICAL_LAND`, dropped) was INERT on the
     Wish + Sol Ring target for exactly this reason. It called a "certain-prefix" line hold (casts up to and including
     the first opener) USER-sized, bound at ~20 executor + rollout sites.
2. **Somberwald Sage lines with no opener: OPEN.** Example s30054069 T4 {Bruna, Wings}, and the gi59/gi11 boards in
   the gate. On paper burst_lead picks {U} correctly and Pilgrim (rank 10) taps before Birds (rank 50, dork band), so
   the drop is not explained yet. Most likely it is the payment's LOCAL float order: ConsumeFloatingAny spends a
   bounce land's {U} or {W} on generic, which is gated by LineGenericOrderLive and so live here, or it is a
   land-before-creature tap. Read `r_base/tap/s30054069_T4.err` first.
3. **The general machinery exists but is parked.** `MTG_LINE_KEEP_ALL` (LineReducerKeepHold, ManaPayment.cpp
   ~10819; doc docs/design/line-keep-all-2026-11-18.md) trial-pays, detects a strand of the declared rest, and re-pays
   with a hold-out or an oracle-table option. It is exact (check_bad=0). It is default OFF by USER ruling: adopt only
   if positive on average on AccomplishedAlchemist; 36,000 pairs measured net +3 at CPU +3%. In that A/B Bruna
   showed 13 digest-only changes per 1,000 games. Its gate does not read g_line_opens_unseen, so it should reach the
   tutor lines; the `r_base/lka` arm measures how much of Bruna's family it covers.

## Candidate fixes (to choose after the traces)
- (a) A narrow, exact opener-line rule: keep the line-aware generic order in an opener line for the casts BEFORE the
  first opener, with demand restricted to the "certain prefix" (casts up to and including the first opener). This
  needs per-cast knowledge of the realised order; check whether one bind point (the per-cast SubManaCost of
  g_line_unpaid_cost: AIEngine.cpp 11346, TurnSolver.cpp 12602/75435/138270/139065) can carry it in lockstep.
  TreasureHunt gi96 must stay unchanged: a cast that is itself the first opener has empty certain demand, so keep
  historical behaviour there.
- (b) A Hall / matching generalisation of UnseenScarceNamedMask: a floating colour the named line owes is
  "irreplaceable" when the untapped sources cannot cover the line's remaining coloured demand without it. Applied to
  the payment's local float as well.
- (c) The LKA rung restricted to what the trace proves (for example creature-only burst lines, or opener lines). This
  re-touches a USER-parked lever: SURFACE to the USER, do not adopt unilaterally.
- For each: unit tests on the gate boards (pattern test/unit/test_line_keep_all.cpp), the repros at MTG_MANA_ORACLE=3
  going to 0, a held-out pooled A/B in one `mtg --batch` manifest with per-job flags (format:
  build/scratch/claude_linekeep_all/manifest_ab.json, compare script build/scratch/claude_linekeep_all/compare.py),
  scoped by which decks' play moves (Bruna at least; check TreasureHunt, Dragonstorm, Cyclone and EDF for any
  opener-line change).

## Remaining steps, in order
1. When `bad_build1.log` has `rc=0` and `repros_base.log` has DONE: read the `tap`/`plain`/`lka` arms; confirm or
   refute findings 1 and 2.
2. Root-cause each family (Wish/Armory generic order; Sage; Sol Ring in plan; Wild Growth host hold) and pick the
   fix shape. Check git log for earlier fixes of the same shape before writing new machinery.
3. Implement behind an `EnvOn("MTG_X", true)` lever (`.claude/skills/coding-conventions.md`; heurarm slot so the A/B
   can pin it per job); =0 byte-identical. Add unit tests.
4. Wait ~200 s after edits, delete stale .o files for touched sources (`build/wt/bruna-apply-drop/...`), rebuild in the
   lane, run units in the lane, and run the repros (`repros1.sh <new bin> <out>`).
5. Held-out pooled A/B under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock` with
   MTG_RSS_CAP_GB=40 and MTG_PGO=0. Every slower game re-run at --depth 8 --budget-ms 0 must recover. Every deck must
   be net <= 0. Measure CPU and cut any added cost ("3x is not a CPU licence").
6. `bash test/regression.sh --both` under the box lock, not in the lane. Audit every changed game, accept GT in this
   worktree, then `python3 test/check_gt_logs.py`.
7. Commit and push wip/bruna-apply-drop only. Report the mechanisms, commits, units, A/B per deck, d8 b0 verdicts,
   GT, CPU and USER questions.

## Brief rules (verbatim intent)
- ONE big engine process at a time, serialised on `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock`:
  A/B batches and regression/smoke tiers go there. Small work (single-game repros, d8 b0 replays, unit runs, the
  worktree build) goes through `bash scripts/box_lane.sh --slots N [--cap-gb G] -- <cmd>` (max 4 slots; the script
  is not executable on 9p, so call it with bash). Never run a full tier inside the lane. MTG_RSS_CAP_GB=40 for big
  runs. Use a small --cap-gb (2-3) for builds and repros, or the job memory-blocks while the gate holds ~30 GB.
- Build with ./build.sh only. Wait ~200 s after edits; delete stale .o for touched files (9p clock lag). Never use
  timeout. Wait on a PID (kill -0) or a DONE marker in a run_in_background waiter; never pgrep -f / pkill -f.
- No greedy pick inside the search window. Do not change src in a tree a gate or tier runs from; do not touch
  .claude/worktrees/batch-1120b. Do NOT push phase-1-2-deck-analyzer; push wip/bruna-apply-drop freely. Rebase, never
  merge. Never touch references/ except to commit them. USER-owned decisions (cast order, keep policy,
  MTG_LINE_KEEP_ALL adoption, ...) are surfaced, never settled.
- Context rule: hand off past ~250K (end 250-350K, never >400K) with a handoff file like this one.
- Adoption bar: every slower game recovers at d8 b0; every deck net <= 0 on a large held-out sample ("+1 is not
  neutral. 0 is neutral.").

## Update 08:40 UTC
- BUILD DONE: `bad_build1.log` ends `rc=0`. The worktree has build/Release/{mtg,mtg-test} of src 9cf45118.
- Repros admitted 08:36 UTC (3 slots; note that repros1.sh runs `xargs -P 4`, one more process than the slots it
  holds -- pass -P 3 next time). Baseline `plain` arm, APPLY-DROP counts (all reproduce): s30054921 T2 9 (+ORDER 10),
  s30051257 T2 8, s30053757 T2 8, s30051968 T2 3, s30051119 T2 3, s30054069 T4 12, s30051713 T2 30,
  s30051079 T3 16, s30051277 T3 8. The tap/lka arms were still running; marker `DONE` in repros_base.log.

---
# Handoff 2 (2026-11-21 10:25 UTC, successor 1 -> successor 2; coordinator-requested)

## Done
- Baseline repros finished (`build/scratch/bad/repros_base.log` has DONE). Counts plain/tap/lka:
  s30054921 T2 9/9/0, s30051257 T2 8/8/8, s30053757 T2 8/8/8, s30051968 T2 3/3/0, s30051119 T2 3/3/3,
  s30054069 T4 12/12/3, s30051713 T2 30/30/22, s30051079 T3 16/16/16, s30051277 T3 8/8/2, s30051794 T4 20/20/12,
  s30051111 T5 60/60/36. (LKA = MTG_LINE_KEEP_ALL=1, USER-parked lever; it does NOT reach the Wild Growth shape.)
- **Commit c8f363ac6** (WIP, both fixes in one commit, default ON, heurarm slots, UNBUILT / UNMEASURED):
  1. **MTG_CREATURE_LEFTOVER_EXACT** (`CreatureLeftoverExactOn`, EngineFlags.h; slot CREATURE_LEFTOVER_EXACT).
     MECHANISM (confirmed from the tap traces): commit_leftover in TapForCostSharedOnceImpl (ManaPayment.cpp ~4205)
     booked a creature payment's over-produced unit into `floating_creature_mana` as min(restricted produced,
     leftover), i.e. it treated the Sage's restricted units as spent LAST. s30054069 T4 {Bruna, Wings}: Sage {U}{U}{U}
     + Forest + Chancery + Thicket pay Bruna with one unit over; the unit became creature-only {U} and Wings {1}{U}
     dropped. s30051277 T3 {Wish, Bruna}: same, creature-only {W}, Wish {G}{W} dropped. This explains the ~56
     "Sage, no opener" games of the census AND part of the opener+Sage family (the opener was a red herring there).
     FIX: book only restricted_made - (max matching of restricted units to the bill's pips: own colour or generic,
     {S} excluded). Colours known on the greedy path (tap_source records the creature_mana_only, non-subtype taps;
     used only if the counts sum to the tap-diff yield and no wild unit); backtracker paths use the colour-blind
     bound min(R, generic). Booked units drawn from colours a restricted unit can still hold, else legacy pick.
     Sound dominance (general unit >= restricted unit of the same colour); the twin of MTG_SUBTYPE_LEFTOVER_EXACT.
     Decks it can move: Bruna(+2HG), Boars (Somberwald Sage), Slivers(+2HG) (Ancient Ziggurat) -- the only
     creature_mana_only non-subtype cards in cards.json.
  2. **MTG_AURA_HOST_LINE_KEEP** (`AuraHostLineKeepOn`; slot AURA_HOST_LINE_KEEP; helper `AuraHostHoldKeepsLine`,
     ManaPayment.cpp just after the LineBillPayable/PayHoldingSet namespace, declared in ManaPayment.h).
     MECHANISM: the unconditional "reserve the Aura's own host" at AIEngine.cpp ~11238, TurnSolver.cpp ~11395
     (SubsetPayableWithFilters), ~12575 (SubsetPayableSequential), ~75345 (ApplyPlanDirect) pushes Wild Growth's {G}
     onto the line's only {U} source. s30053757 T2: Thicket held, Sanctum paid {G}, Wings {1}{U} dropped (oracle:
     Thicket pays WG, Pilgrim + Sanctum pay Wings). Same in s30051079 T3 (Seaside Citadel).
     FIX: hold unless (held payment on a scratch copy, Aura attached, leaves rest = g_line_unpaid_cost - bill
     unpayable by LineBillPayable as one noncreature bill) AND (unheld payment leaves it payable). Only evaluated when
     the rest owes a coloured pip. All four sites call the same helper (lockstep). The whole-turn prepay's
     MTG_PREPAY_AURA_HOST (TurnSolver ~66351) is NOT changed. Moves any deck with Wild Growth / Fertile Ground /
     land Auras with extra mana (Bruna, EDF, others: grep cards.json is_land_aura + land_aura_extra_mana, then decks).
  - Unit tests added in test/unit/test_bruna.cpp: "a creature payment's over-produced unit stays GENERAL..." (3
    subcases) and "the Wild Growth host hold yields when it strands the rest of the line" (2 subcases, incl. the EDF
    seed 2 Conservatory case that must still HOLD).

## Running job (mine; leave it)
- Lane build `scripts/box_lane.sh --slots 4 --cap-gb 3 -- ./build.sh` from the worktree, PID **83321** (parent bash
  81801), log `/workspaces/MagicDeckTester/build/scratch/bad_build2.log`, DONE marker = a line `rc=<n>` in that log
  (the log is empty while queued). Queued since 08:49 UTC behind a congested lane (FIFO; a 2-slot ninja and several
  4-slot jobs ahead). Last src edit was ~10:21:50 UTC. **9p clock lag: if the build started before ~10:25:10 UTC,
  check it picked up the edits** -- the Wild Growth unit test and the leftover test failing would show a stale
  object; if in doubt delete `build/wt/bruna-apply-drop/CMakeFiles/mtg-core.dir/Release/src/ai/{ManaPayment,
  AIEngine,TurnSolver}.cpp.o` and `.../mtg-test.dir/Release/test/unit/test_bruna.cpp.o` and rebuild.
  HeuristicArm.h / EngineFlags.h changed, so this is a near-full rebuild.

## Next steps, in order
1. Build rc=0 -> copy `build/Release/mtg` to `build/scratch/bad/fix12/mtg` (so later rebuilds cannot touch a
   running measurement). Units in the lane: `bash scripts/box_lane.sh --slots 4 --cap-gb 3 -- build/Release/mtg-test`
   (all pass; the new Bruna cases pass; spot-check the leftover case FAILS with MTG_CREATURE_LEFTOVER_EXACT=0).
2. Repros: `bash scripts/box_lane.sh --slots 3 --cap-gb 2 -- build/scratch/bad/repros2.sh <bin> build/scratch/bad/r_fix 3`
   (11 seeds; arms `plain` = both fixes on, `leg` = MTG_CREATURE_LEFTOVER_EXACT=0; `-P 3` matches the slots; ends with
   DONE). Add an arm with MTG_AURA_HOST_LINE_KEEP=0 if attribution is needed. Expected: s30054069/s30051277/s30051111
   (creature-float families) and s30053757/s30051079 (WG) drop sharply; s30051257/s30051119/s30051968/s30051794/
   s30051713 (Sol Ring in plan / Wish+Sol Ring) are NOT addressed (see residue below).
3. Gate slice (BIG run, under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock`):
   `build/scratch/bad/gate/run.sh <bin>` runs the gate's 5,000 Bruna games (manifest `build/scratch/bad/gate/
   manifest.json`, both arms of MTG_CREATURE_LEFTOVER_EXACT per job, names onbruna__/offbruna__) at
   MTG_MANA_ORACLE=2, MTG_RSS_CAP_GB=40, all cores; DONE marker `build/scratch/bad/gate/DONE`. NOTE the manifest pins
   only fix 1; both arms have fix 2 ON -- regenerate it with flags {fix1, fix2} both true/false for a clean
   before/after (the off/off arm should reproduce the gate's 1,891 Bruna APPLY-DROPs exactly: identity check).
   Count: `grep 'APPLY-DROP ' err.txt | grep -c 'job=onbruna'` etc.; classify with `build/scratch/bad/cls.py`.
4. Held-out A/B (BIG run, under the box lock, MTG_PGO=0 is irrelevant here since both arms are one binary):
   `build/scratch/bad/ab/mk_ab.py <FLAG> 710000 <manifest>` currently pins ONE flag over boars 3000 / bruna 10000 /
   slivers 6000 pairs. Extend it for fix 2: pin BOTH flags per arm (or one arm per flag) and add the land-Aura decks
   (EDF at least). `build/scratch/bad/ab/run.sh <bin>` (DONE marker ab/DONE), `python3 build/scratch/bad/ab/compare.py`.
   Bar: every deck net <= 0; every slower game recovers at --depth 8 --budget-ms 0 (lane, 1 slot each; clear
   clairvoyance only may stay, test with MTG_NC_SEARCH=1); report cpu on/off and cut fix 2's scratch cost if visible.
5. `bash test/regression.sh --both` under the box lock; audit changed games; `--both --accept`;
   `python3 test/check_gt_logs.py`. Then split c8f363ac6 into two commits (fix 1, fix 2) if both are kept, fix the
   "about half of its 1,891" comment in ManaPayment.cpp with the measured number, push wip/bruna-apply-drop only.

## Residue NOT addressed (report these)
- **Sol Ring in plan / dork-vs-land** (s30051257 {Wings, Sol Ring}, s30051119 {Sage, Sol Ring}): the engine casts
  Sol Ring first and pays its {1} with the land that is the line's only {U}/{G} instead of Avacyn's Pilgrim. Fixing
  needs a creature ahead of a land: the standing "no creature ever ranks ahead of any land" law (six variants
  refuted, docs/design/handoff-bruna-mana.md). Left as re-pay-only; MTG_PAY_ROLLBACK recovers these at depth 1.
- **Wish/Armory + Sol Ring land-vs-land** (s30051794 T4, s30051968 T2: Sol Ring's {1} off Forest, the only {G}, where
  the Chancery/Remote Farm should pay): needs BOTH a line-critical-land rung (MTG_PAY_LINE_CRITICAL_LAND, DROPPED
  2026-11-04 for Cyclone +3) AND the line order inside an opener line (g_line_opens_unseen turns
  LineGenericOrderLive off for tutor_to_hand lines). The "certain prefix" hold was judged USER-sized (~15 bind sites:
  grep LineOpensUnseenScope). Surface to the USER; not attempted.
- Somberwald Sage "no opener" (~56 games): EXPLAINED by fix 1's mechanism (creature-only leftover booking), pending
  the gate measurement to confirm.
- USER-owned: MTG_LINE_KEEP_ALL stays parked (not touched).

## Update 11:58 UTC
- BUILD DONE: `bad_build2.log` ends `rc=0`; it was admitted to the lane at 11:27 UTC, more than an hour after the last
  src edit (10:21:50), so the 9p clock-lag concern does NOT apply. `build/Release/{mtg,mtg-test}` hold c8f363ac6's src.
  Start at step 1 (copy the binary aside, units, repros).

---
# Agent 3 progress (2026-11-21)
- fix12 binary (c8f363ac6) at build/scratch/bad/fix12/{mtg,mtg-test}. Units 3805/3805 pass; the leftover and WG tests
  FAIL with their flag =0 (as designed).
- Repros (r_fix, plain/leg): s30054069 12->3, s30051277 8->2, s30051111 60->28 (fix 1; back to base with it off);
  s30053757 8->0, s30051079 16->0 (fix 2). Sol Ring / tutor residue unchanged (s30051257 8, s30051968 3, s30051794 20,
  s30051713 30, s30051119 3, s30054921 9).
- GATE slice (gate/manifest.json: the gate's bruna+boars+sea_creatures jobs, on = both fixes, off = neither,
  MTG_MANA_ORACLE=2, 20 threads beside the Hinata tail at cap 16): IDENTITY off arm = gate exactly (Bruna 1,891,
  Boars 12, Sea 37 APPLY-DROP). On: Bruna 1,891 -> 1,221 (-35%), ORDER 112 -> 92, DOMINATED-CREATURE 668 -> 623;
  Sea 37 -> 29; Boars 12 -> 12 (Boars play digest-identical in all 87 jobs; its DOMINATED-CREATURE 1,606 -> 1,947
  is the oracle's own what-if payer now booking the Sage surplus general -- engine play unchanged).
- A/B ab2 (manifest_ab2.json, seed 710000, fix12, NO per-game wins: --log-dir was the wrong flag, use
  --game-log-dir). Job-level nets (agg2.py): all decks <= 0 except sead0 +1; bruna -8, brunad0 -8, bruna2hg -1,
  edf2hg -1, alchemistd0 -1. CPU (units unchanged): edfd0 1.38, edf 1.12, edf2hg 1.10, jumbo 1.09, sea 1.05.
- 7b621e156: fix-2 CPU cut (pre-filter + unheld-first). Build queued: build/scratch/bad/build_fix3.log (copies to
  build/scratch/bad/fix3/). Then re-run the A/B with --game-log-dir on fix3.
- Rule-override measurement levers (default OFF): branch wip/bruna-rule-override, worktree
  .claude/worktrees/bruna-ro (MTG_PAY_LINE_CRIT_LAND / _OPENER / _BEHIND_DORKS + unit test); build queued
  build/scratch/bad/build_ro2.log.
