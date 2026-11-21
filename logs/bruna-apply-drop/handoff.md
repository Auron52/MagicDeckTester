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
