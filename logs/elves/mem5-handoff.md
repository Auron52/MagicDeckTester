# Elves mulligan-gen memory #5 -- handoff (2026-11-27 ~15:35 UTC, wip/elves-mem4)

Worktree /workspaces/MagicDeckTester/.claude/worktrees/elves-mem4, branch wip/elves-mem4 (push ONLY this branch, never the
shared one; rebase, never merge; never touch references/). S = /workspaces/MagicDeckTester/build/scratch/elves-mem4.
decks/Elves/Elves.profile.json shows modified (a COPY of elves-int's, mull_gen d1 b1): NEVER commit it.
Predecessor context: logs/elves/mem4-handoff.md (read its Measurement + dominance sections).

## Commits
* f28d2045b MTG_ELVES_WAVE_XCAP (Genesis Wave X cap, default ON) -- coordinator ADOPTS it.
* 0a2e89273 perf(elves): in-place compaction (play-identical by construction). Built (Release, rc=0) and snapshotted at
  S/bin_sav/{mtg,mtg-analyze,mtg-test}. S/bin_cap = f28d2045b.
  - ApplyProviderPlanListFilterLazy uniform branch, BpDeriveContinuationList M1 compaction (+ its no-fan ordered path:
    in-place cycle gather), the non-lazy new-only survivors filter: no new kept vector while the old is alive.
  - Identity argument: elements MOVE-CONSTRUCTED (PbMoveSlot; move-assign would keep a target's heap buffer for a short
    source string, and ApproxPlanBytes reads land_to_play/fetch_target/land_face capacity); every POOL-PRICED vector ends
    at the old capacity (filter's list = pre-filter size; prototypes = push_back growth of kept count, PbPushBackCapacity
    probes this STL). The returned continuation list's capacity is never priced (store shrink_to_fit; scratch unpriced).

## Results so far
* Units (S/bin_cap/mtg-test, f28d2045b): 4332/4332 cases passed, 0 failed (S/unit.log). NOT yet re-run on bin_sav.
* Wave line 10 (seed 11400714887989404418), the one slower rollout under the cap at d1 b1 (T6 -> T7): replayed at d8 b0
  (MTG_KEEP_BENCH_DEPTH=8 MTG_KEEP_BENCH_BUDGET=0), BOTH arms win T6 (XCAP=0: 426 ms, XCAP=1: 496 ms) -> RECOVERS (cap
  arm no later than base). Sanity: cap arm with no depth override reproduces T7 (9.1 s). Outputs S/l10/arm{0,1}.out.
  Note: MTG_KEEP_BENCH=1 only relaxes the gencache play-digest gate (the replay list returns before the bench code), so
  a replay never re-runs the 30-min discovery; backup of the cache at S/gencache.capbin.bak.
* Peaks (one core, gen d1 b1, /usr/bin/time maxrss; S/rep2/sav/summary.txt), cap (f28d2045b) -> savings (0a2e89273):
  Wave line 1 1.62 -> 1.45 GB, line 16 1.98 -> 1.87, line 9 1.54 -> 1.47, slow8 #8 2.21 -> 2.07; win turns 7/7/7/5 equal.
  Process floor ~0.77 GB. (Pre-cap base for line 1 was 3.81 GB.)
* 64-game play-digest battery (rollout-config digest, d1/b1) under bin_sav: 9568cd0c409bb7e9 = bin_cap's. (Necessary,
  not the 2000-game check.)
* MASSIF slow8 #8 under bin_sav (S/massif/sav_s8.{massif,txt}; heap peak 1.85 GB, snapshot 68): pay-draw fan PROTOTYPE
  action vectors 556 MB (AppendSubdecisionAxes PdFan lambda) + the prototype array 208 MB (262,144 capacity) = ~0.76 GB;
  base plans' actions (EnumeratePlansCore) 564 MB; EnumeratePlansCore's reserve 133 MB; ElvesScalingAdmit's own Plan
  copies (CopyPlanView) inside ApplyProviderPlanListFilter ~0.2 GB (102 + 59 + 26 + 19 MB).
* FINDING on saving (b): the Visionary pay-draw fan is ALREADY LAZY on the breakpoint-continuation path (worst Wave
  rollout T7: bp-derived 321,514 real + lazyP 1,183,116 in 277,252 fans = the post-axes 1,504,630; S/diag/probe_l1.out).
  The "602 MB PdFan" in mem4's massif is the one-PROTOTYPE-PER-FAN copy (pd_entry(f,1) copies all[pi]), not entries.
  NEXT LEVER: a kFanPayDraw prototype is its base plan with only mihail_pay_choice differing (Edit overwrites it), so
  sharing it with the base (L[fan_base]) removes ~0.6-0.76 GB at these peaks. NOT built: ~25 protos readers, fan_base is
  not maintained by compactions, and later axes edit prototypes independently (bp_wave0, legend_keep_turn) -- a design
  change, not an in-place tidy.

## Queued / running jobs (all mine)
* DIGEST CHECK (step 2's bar): S/dig/run.sh, PID 2895452 (flock 2895453, waiting on heavy.lock behind another session's
  ./build.sh, PID 2895011). Runs S/dig/dig.json (1000 std + 1000 2HG, seeds 9740001+gi, game_index gi, d5 b20, profile
  S/ab/Elves.profile.json = committed HEAD profile, shipped cap default) under bin_cap then bin_sav, each one pooled
  32-thread batch. Outputs S/dig/{cap,sav}.{out,err}; markers S/dig/{cap,sav}.DONE ("rc=N time"), S/dig/DONE.
  Verdict: `python3 S/dig/cmp.py S/dig/cap.out S/dig/sav.out` -> must read digest!= 0, winturn!= 0 (units!= should be 0
  too) for std and 2hg, missing 0/0. If it fails: the identity argument has a hole -- bisect the three sites (filter,
  compaction, survivors).
* Nothing else running (line-10 replays, peak replays and massif all DONE).

## Remaining, in order
1. Digest verdict (above). Then units on bin_sav: `bash scripts/box_lane.sh --slots 2 -- S/bin_sav/mtg-test` from the mem4
   worktree (expect 4332 passed).
2. STEP 3, the memory proof: `S/r32/run.sh` (run_in_background; takes heavy.lock itself; ONE process, all 32 threads,
   the 28 lines of build/scratch/elves-int/replay_wave.log under MTG_RSS_CAP_GB=20, MTG_KEEP_BENCH=1 for the cache hit,
   bin_sav). Output S/r32/out (/usr/bin/time -v: "Maximum resident set size", "Elapsed (wall clock)"; "[replay-list]
   DONE ... wall=") ; markers S/r32/STARTED, S/r32/DONE ("rc=N"). Must finish without tripping the cap. Report peak RSS
   + wall. Launch only after the digest check passes (else use bin_cap and say so).
3. Push wip/elves-mem4 (git push origin wip/elves-mem4), short report to the coordinator. No smoke/regression/GT.

## USER rulings (verbatim, as relayed)
* Scaling-sink rule (DecisionProviders.cpp, ElvesProvider): "There are multiple scaling effects like Genesis Wave's X and
  Joraga Warcaller, but generally you want to pick just one of those and scale it up fully, minus perhaps a few 1-mana
  producing attackers. So there might be a couple different relevant options, (i.e. do I tap this Llanowar Elves to scale
  it further or keep it as an attacker, which can either be searched or heuristically pruned depending on the situation)
  but we shouldn't try every possibility."
* Kick cap (USER 2026-11-27): "I think Kick counts should be capped at lethal size of 20".
* Bar (CLAUDE.md): every slower game must recover at --depth 8 --budget-ms 0, and every deck's aggregate net <= 0
  ("+1 is not neutral. 0 is neutral.").
* Coordinator's constraint (2026-11-27): no fold may drop an in-band Wave X on "equivalent count" grounds; "same reveal,
  strictly more leftover mana" dominance survives as long as nothing else reads X.
* CLAUDE.md binding: one heavy process at a time (heavy.lock), small jobs via box_lane (<= 2 slots), MTG_RSS_CAP_GB=20 on
  engine runs, no timeout, waiters on PID / DONE markers (never pgrep -f), never push the shared branch, the engine is
  heuristic-free (prunes only in the provider; "do nothing more" stays available), builds via ./build.sh with
  MTG_BUILD_JOBS=14 under flock heavy.lock, handoff at ~250K context.
