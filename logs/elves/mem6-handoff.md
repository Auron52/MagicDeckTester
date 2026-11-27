# Elves mulligan-gen memory #6 -- handoff (2026-11-27 ~18:40 UTC, wip/elves-mem6)

Worktree /workspaces/MagicDeckTester/.claude/worktrees/elves-mem6, branch wip/elves-mem6 (base origin/phase-1-2-deck-analyzer
36e9ce5b2). Push ONLY wip/elves-mem6; rebase, never merge; never touch references/. S = /workspaces/MagicDeckTester/build/scratch/elves-mem6.
decks/Elves/Elves.keepmodel.gencache.json in this worktree is an (ignored) COPY of elves-int's, so replays skip the 30-min
discovery (MTG_KEEP_BENCH=1). Predecessors: ../elves-mem4/logs/elves/{mem4,mem5}-handoff.md.

## Mechanism (found: gdb all-thread backtraces at RSS 2/3/4/6/8 GB, S/diag/g1.{bt,log})
The hog (size5 draw r=5 seed=13064056893581132306; Genesis Wave, Taunting Elf, Priest, Marwyn, Forest) reaches, at T7 in the
EXECUTOR (AIEngine::TakeTurn), a breakpoint continuation of 236,031 real plans (1.52M actions, ~1.17 GB; 43 s to derive) whose
memo entry is LAZY (pay-draw / put-back fans held as prototypes). The executor's four replay sites took
TurnSolver::EnumerateBreakpointPlans[AtWave] BY VALUE = CandSeq::Materialised: every lazy clone built as a full Plan. 4 -> 8 GB
in ~7 s inside CandSeq::Materialised -> At -> Plan::operator=; the whole ~10 GB of the 14 GB peak. Those sites only need the
size, the chain sentinel's walk and ONE entry.

## Commit
* 9b9e82fa4 perf(executor): TurnSolver::PickBreakpointPlan -- the same memo entry read through its VIEW (size,
  BpChainCandIndexView, one entry via CandSeq::At), used at AIEngine's four sites (resolve_draw_breakpoint, site-9 chain loop,
  pod twin, site-9 executor). Play-identical by construction (Materialised IS position-by-position At; Plan holds only
  vectors/strings so `extra = pick` == `extra = cands[k]`; demoted lookup still follows the main one). Also
  MTG_BP_DERIVE_HIST=1 (diagnostic, print-only, with MTG_BP_DERIVE_SLOW_MS): a big continuation list's shapes.

## Results
* DIGEST: S/dig (1000 std + 1000 2HG, d5 b20, seeds 9750001+gi, committed profile frozen at S/dig/Elves.profile.json), each arm
  ONE pooled 32-thread batch, base = elves-int build/PGO (stamp 2b645125 = landed HEAD:src) vs bin_new:
  `python3 S/dig/cmp.py S/dig/base.out S/dig/new.out` -> std 1000 paired digest!= 0 winturn!= 0 units!= 0; 2hg 1000 paired
  digest!= 0 winturn!= 0 units!= 0; missing 0/0. PASSES. (ms 479->724 / 1237->1805 is PGO vs Release + lane contention, not a
  signal.) Batch maxrss 5.31 -> 4.91 GB.
* HOG PEAK: 14.0 GB (abort; replayed at a 6 GB cap: S/diag/t1) -> 3.32 GB, rc=0, 57 s wall, one core (S/diag/n1.err).
* UNITS: the first two runs (S/unit.log, S/unit2.log) had 4 failures (3 Zombies discard + Pirates routing), ENVIRONMENTAL: a
  binary run from S/bin_new resolves src/ through ResolveHeuristicDefaultsPath's exe-relative walk to the PRIMARY checkout,
  which has no Zombies cards/deck; elves-int's landed mtg-test passes the same 5 cases. heuristic_defaults.env is identical in
  the primary and mem6 (checked), so engine replays are unaffected (cards.json passed explicitly). Re-run from the REAL build
  dir (build/wt/elves-mem6/Release/mtg-test, which has src/decks links) is RUNNING (below) -- expect 4342 passed.
* What the remaining 3.3 GB is: the T7 list itself. MTG_BP_DERIVE_HIST (S/diag/n1.err): 236,031 plans in 623 shapes, the largest
  2,646 each = Galadhrim Brigade squad count 0..20 (21; the USER's lethal-20 kick cap) x Immaculate Magistrate {T} target
  (21 identity classes, already folded by LandfallTargetFoldKey) x 6, beside Ezuri, Allosaurus Shepherd, Wirewood Lodge and
  1-3 "Elvish Clancaller > (decline search)" activations (library holds no Clancaller: pay {4}{G}{G} + tap for only a shuffle).
  The scaling band keeps all 21 counts here because H (every untapped hasted scaled dork, Crossroads in play) is huge.
  None of these is a play-identical cut: the decline-search activation is dominated EXCEPT for its shuffle (changes future
  draws in the clairvoyant engine), Magistrate targets are distinct classes, the band is the USER's rule. Candidates for the
  coordinator (each a provider prune -> A/B + d8 b0, possibly USER-owned): (1) no Clancaller tutor activation when the library
  holds no Clancaller; (2) restrict Magistrate's target set (TapTargetPumpTargets hook). A play-identical route would be a LAZY
  kick-count axis (hold the 21 count variants as one prototype, like kFanPayDraw) -- a design change.

## UPDATE 18:42 UTC -- both jobs FINISHED
* UNITS (S/unit3.log, rc=0): 4342/4342 test cases, 3,687,790 assertions, 0 failed. The commit's "units pass" stands.
* MASSIF (S/massif/new_hog.txt, rc=0, done 18:39) is NOT REPRESENTATIVE: under valgrind the derivation meter fired
  ("[bp-derive-priced-stop] subset visits refused=502225" in S/massif/new_hog.err), so the replay played a cheaper game in ~3 min.
  To break down the 3.3 GB, use the gdb RSS-threshold sampler on the native binary instead (S/diag/hog_gdb.sh <bin> <list>
  <outprefix>; thresholds in the script; run in box_lane --slots 2 --cap-gb 4), or massif with the meter's wall dependence
  removed (find what MTG_BP_DERIVE_METER / the priced stop keys on).

## Jobs as of the handoff (now finished, see the update above)
* MASSIF of the hog on bin_new: box_lane PID 2993643 -> S/massif/run.sh PID 2993652 -> valgrind PID 2993653 (started 18:34,
  replay phase from 18:36; ~45 min). Outputs S/massif/new_hog.{massif,out,err}, ms_print -> S/massif/new_hog.txt; marker
  S/massif/DONE ("rc=N time"). Read the peak snapshot's tree: what besides the 236K list (base actions / filter / axes /
  ElvesScalingAdmit rows) makes the 1.7 -> 3.3 GB transient; any in-place, play-identical cut there is the next lever.
* UNITS from the real build dir: box_lane PID 2994879 -> mtg-test PID 2994890. Output S/unit3.log, marker S/unit3.DONE
  ("rc=N"). Command: `bash scripts/box_lane.sh --slots 2 -- S/unit.sh` (S/unit.sh cds to mem6 and execs
  build/wt/elves-mem6/Release/mtg-test). If it passes, the commit message's "units pass" stands; if not, investigate.

## Remaining, in order
1. Units verdict (S/unit3.DONE / S/unit3.log).
2. Break down the 3.3 GB peak NATIVELY (massif under valgrind tripped the priced stop -- see the update).
3. THE 32-THREAD REPLAY -- massif is DONE, so it can launch now (heavy.lock):
   `S/r32/run.sh` with run_in_background (takes heavy.lock itself). ONE process, all 32 threads, list S/r32.log = 37 lines
   (S/slow8.log = the 8 slowest pre-abort rollouts from elves-int logs/Elves_mullgen/gen.log, 267 s ... 125 s; then the 28
   lines of build/scratch/elves-int/replay_wave.log; then the hog), MTG_RSS_CAP_GB=20, bin_new. Markers S/r32/STARTED,
   S/r32/DONE ("rc=N time"); output S/r32/out (/usr/bin/time -v "Maximum resident set size", "Elapsed (wall clock)";
   "[replay-list] DONE ... wall="). Report peak RSS + wall; it must finish without tripping the cap.
4. Further (optional) memory work per the massif + the candidates above; any provider prune needs the A/B + d8 b0 bar.
5. Do NOT run smoke/regression or accept GT (the coordinator integrates). Push wip/elves-mem6 only.

## USER rulings (verbatim, as relayed)
* Scaling-sink rule (DecisionProviders.cpp, ElvesProvider): "There are multiple scaling effects like Genesis Wave's X and
  Joraga Warcaller, but generally you want to pick just one of those and scale it up fully, minus perhaps a few 1-mana
  producing attackers. So there might be a couple different relevant options, (i.e. do I tap this Llanowar Elves to scale
  it further or keep it as an attacker, which can either be searched or heuristically pruned depending on the situation)
  but we shouldn't try every possibility."
* Kick cap (USER 2026-11-27): "I think Kick counts should be capped at lethal size of 20".
* Band (USER 2026-11-27, on the narrower round-2 menus): "Okay, that's fine. We'll leave it."
* Bar (CLAUDE.md): every slower game must recover at --depth 8 --budget-ms 0, and every deck's aggregate net <= 0
  ("+1 is not neutral. 0 is neutral.").
* Coordinator's constraint (2026-11-27): no fold may drop an in-band Wave X on "equivalent count" grounds; "same reveal,
  strictly more leftover mana" dominance survives as long as nothing else reads X. After a Genesis Wave resolves the turn is
  re-planned and leftover mana matters (std 2273 needs Wave X=6 for Allosaurus Shepherd).
* CLAUDE.md binding: one heavy process at a time (heavy.lock), small jobs via box_lane (<= 2 slots), MTG_RSS_CAP_GB=20 on
  engine runs, no timeout, waiters on PID / DONE markers (never pgrep -f), never push the shared branch, the engine is
  heuristic-free (prunes only in the provider; "do nothing more" stays available), a budget may re-price work but never make
  a line unfindable, builds via ./build.sh with MTG_BUILD_JOBS=14 under flock heavy.lock, handoff at ~250K context.
