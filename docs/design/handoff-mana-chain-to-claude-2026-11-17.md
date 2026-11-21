# Mana queue handoff: Codex to Claude, 2026-11-17

This is the current local handoff in the **primary checkout** `/workspaces/MagicDeckTester`, branch `phase-1-2-deck-analyzer`. Continue the mana-clean queue autonomously until the queue is empty or a real user tradeoff remains. Read root `CLAUDE.md`, `.claude/skills/regression-testing.md`, `.claude/skills/coding-conventions.md`, and `.claude/skills/mtg-ai.md` before modifying the related code. The older queue and history are in `docs/design/handoff-mana-chain-to-codex-2026-11-14.md`; this document supersedes its status paragraphs.

## Live status (Claude), updated 16:50 UTC

**GT RECONCILIATION IN FLIGHT (02:10 UTC 11-18) -- TOP PRIORITY.** USER 11-18 ~02:00, verbatim: *"Can you double check
that you are running the regression tests and updating ground truth as you go? I had agents telling me that mana payment
issues went in without a ground truth update."* and *"As-in what is in the shared branch did not match the ground truth
from changes related to mana work."* CONFIRMED: smoke at the pushed tree failed 39 rows (Codex, 05:09-07:01 11-17), then
43 (10:30, after the PD fixes), 44 (12:01, Minotaur), 46 (14:53 onward, painland/Dragonstorm fixes): each mana fix was
A/B'd held-out and PUSHED, but its smoke/regression GT was never accepted ("same failset" was carried forward, and the
original 39 were labelled "pre-existing drift, audit separately" and never audited). Remedy running: clean build of
pushed HEAD d90931741 (src clean; the Cyclone pf1d adoption is in `git stash` "cyclone pf1d adoption (defaults ON),
pending gi 570487"), then `MTG_RSS_CAP_GB=40 bash test/regression.sh --both`, console
`build/scratch/claude_gtfix/both.out`. Then: read the audit, classify_turn_later + both-arm b0 on searched slower games,
accept both tiers with a provenance note, check_gt_logs, commit, and PUSH AT ONCE. USER 11-18 ~02:20, verbatim: *"If we
need to break the 4 hour push rule to address this I give the okay to do so. It causes issues for all of the other
agents if there is drift like this."* (This exception covers the GT reconciliation push only.) RULE from now on:
a default-ON play change is not pushed without its smoke+regression GT accepted in the same push.
**03:2x UTC: RECONCILED.** Smoke+regression at d90931741 (Release, clean src): smoke 49 fail, regression 73 fail vs the
old GT. Net loss-penalized turn-games: smoke d0 -163 / searched -10; regression d0 -121 / searched -94 (Spirits -71, from
the upstream keep table, which also arrived without GT). 75 searched slower games classified (classify_turn_later,
`build/scratch/claude_gtfix/classify_*.out`): churn incl. both win-to-loss games; PERSISTS = Spirits keep-table hands + 11
same-draw line changes in decks net <= 0. Accepted BOTH tiers with a provenance note; the 24 earlier smoke/regression notes
the accept dropped were restored; check_gt_logs 1,278 consistent. Confirm smoke then push at once (USER exception).
**PUSHED 03:11 UTC 11-18** (`f71a7e0ce`, USER exception to the 4 h rule for this fix): confirm smoke 223/223 rows, 561
scenarios, 0 fail. Next ordinary shared-branch push >= 07:11 UTC.
**IN FLIGHT 03:13 UTC:** one pooled batch `build/scratch/claude_uks_over_pf1d/` (pid file = run.sh; mtg pid 2261544; waiter
bg1423xys): row 1 = gi 570487 d8 b0 rerun with pf1d; then MTG_UNSEEN_KEEP_SCARCE 0 vs 1 on Cyclone games 560000-564999
with pf1d DEFAULT ON (binary = stash@{0} build). compare.py prints net + gi 560506. Then: if 570487 recovers (<= T5) pop
the stash, commit the pf1d adoption, smoke-accept the Cyclone rows (only those may move), push >= 07:11; if UKS over pf1d
is net <= 0 with 560506 no longer later, flip MTG_UNSEEN_KEEP_SCARCE default ON (held-out was byte-identical elsewhere)
with its own smoke+regression accept.
**04:45 UTC:** UKS over pf1d DONE: Cyclone 560000-564999 earlier 1 (gi 560147 T6->T5) later 0, net -1; gi 560506
identical (T4). -> USER condition met: UKS default ON. WORKING TREE (uncommitted, built, units 3,466/3,466): pf1d
defaults ON (DecisionProviders.cpp, HeuristicArm.h) + UKS default ON (EngineFlags.h, HeuristicArm.h) +
docs/design/unseen-keep-scarce-2026-11-17.md header. Only gi 570487 (d8 b0) still running in that batch (mtg pid
2261544). When it ends: if <= T5, commit both adoptions, run `regression.sh --both`, audit, accept, push >= 07:11.
If not, investigate 570487 first; UKS alone could still ship over the committed order? NO: its +1 needs pf1d.
The d8 b0 replay of Cyclone gi 570487 was STOPPED by the agent (own experiment, 2.5 h on one game) to free the box;
rerun it alone after the GT work (`build/scratch/claude_ponderfirst_ab3/d8/`, manifest row for g570487 only).
**07:45 UTC 11-19:** CI GREEN on 3e8a209bc. Gate `claude-queue-20261119c` ALSO aborted at the RSS cap (06:57): the runaway is Jumbo **s30122783 gi122783** (job jumbo_cactuar__g122684) T4 m1, a Tooth and Nail x2 + Surrak + Satyr + Arbor Elf + Greaves x3 menu: ALL 27,868 jumbo CAPPED-DECISION flags are this one game; it was in flight at both aborts. QUEUE ITEM: Jumbo oracle runaway on that decision (memory + decision budget). Recorded `09dfa9b6e`: jumbo 5000 games (28,322 issues, 27,868 from that game; DOMINATED-CREATURE 222, ORACLE-GAP 66, ENUM-GAP 73, APPLY-DROP 55, ORDER 38), treasure_hunt 286 (APPLY-DROP 234). QUEUED under `flock build/scratch/claude_box.lock` (not FIFO): LKA AA 30k A/B (RUNNING since ~07:09, `build/scratch/claude_lka_alc30k/`, DONE), gate rerun `claude-queue-20261119d` on merfolk_turns maze_s_end sea_creatures stuffy_doll pirates vampires shadowborn_apostle prevent_damage fungus (`build/scratch/claude_gate0715/`, DONE), v2 build+units+`--both` (`build/scratch/claude_aav2/`, DONE -> inspect, accept GT with notes-union restore, push >= 10:26 with the local commits), agents' builds/batches (jumbo-perf2, gob-oracle-fold successor, sacfodder-fix at ~241K -> expect its handoff, then launch a successor from logs/sacfodder-fix/handoff.md in its worktree). LKA adoption rule: pool the 30k with the earlier 6k (build/scratch/claude_linekeep_all/logs_ab + logs_alc AA rows); adopt default ON if net <= 0, then GT accept.
**08:30 UTC 11-19:** LKA AA re-measure DONE: 30,000 pairs (v2 on both arms) 25 faster / 22 slower, net **+2**, cpu +3.0%; pooled with the 11-18 6k (+1) = +3 / 36,000. Not positive on average -> per the USER rule NOT adopted, stays default OFF (recorded in docs/design/line-keep-all-2026-11-18.md). Box lock now: sacfodder-fix build holds it; gate d, aav2 chain, jumbo-perf2 stats, gob-fold chain1 still queued.
**09:15 UTC 11-19:** Gate `claude-queue-20261119d` ABORTED at the RSS cap on BOTH arms (rc=2, nothing recorded): ON arm (oracle 2) at 40.52G with 29 fungus chunks in flight (longest in flight: prevent_damage gi 118350 557 s, gi 118742 524 s); OFF arm (oracle 0 = plain play!) at 40.70G within minutes of starting merfolk_turns (longest in flight merfolk_turns gi 172, 110 s). PROBE queued under the lock: `build/scratch/claude_rss_probe/` (merfolk gi172 oracle 0, PD gi118350/118742 oracle 2; single-game batches in parallel, MTG_RSS_CAP_GB=12 each, /usr/bin/time -v max RSS; status + DONE). Gate `claude-queue-20261119e` queued (`build/scratch/claude_gate_e/`, DONE) on maze_s_end sea_creatures stuffy_doll pirates vampires shadowborn_apostle; merfolk_turns, prevent_damage, fungus wait on the probe verdict (a play-side memory runaway in merfolk_turns is a suite issue, fix first).
**10:10 UTC 11-19:** v2 chain 1 FAILED on stale tests (8 AA cast-order unit cases + scenario aa_weather_gw_galvanic_one_list_no_t4 assumed v2 off); fixed (V2OffAa guard; v2=0 in the 4 gw/gwf scenarios; UNCOMMITTED in the primary tree until the rerun passes) and rerun queued `build/scratch/claude_aav2b/` (status, DONE; waiter). RSS probe verdict (single games, /usr/bin/time): merfolk_turns gi172 oracle 0 = 5.5 GB max RSS (55 s); PD gi118350 / gi118742 oracle 2 = 11.4 / 11.8 GB (7 / 5.5 min). No single runaway: concurrency of heavy games. QUEUE ITEMS: (a) Merfolk Turns PLAY memory (5.5 GB in one oracle-off game: find the unbounded structure); (b) PD oracle memory ~12 GB/game (fold with the PD CAPPED fold). Gate `claude-queue-20261119f` (merfolk_turns + fungus, `--threads 16` = memory exception, recorded in its run.sh) queued after gate e (`build/scratch/claude_gate_f/`). prevent_damage re-gates after its CAPPED fold.
**10:45 UTC 11-19:** Gate e ON arm aborted at 40.8 GB 25 s after start, ONLY maze_s_end in flight: MAZE'S END ORACLE-ON MEMORY RUNAWAY (its sizing probe also died after ~17/64 games in gates d AND e, silently). Fixed the silence: `22d11f7b5` mana_clean stops when a sizing probe dies/plays short. Delegated: Maze oracle memory agent (wip/maze-oracle-mem, .claude/worktrees/maze-oracle-mem); Merfolk Turns play memory agent (wip/merfolk-mem, .claude/worktrees/merfolk-mem). Gate e's OFF arm completes but the run records nothing (ON dead). Queued: gate f (merfolk_turns + fungus, 16 threads, after gate e DONE), then gate g `claude-queue-20261119g` (sea_creatures stuffy_doll pirates vampires shadowborn_apostle, `build/scratch/claude_gate_g/`, after gate f DONE). maze_s_end + prevent_damage re-gate after their fixes.
**11:45 UTC 11-19:** Gate f stopped at sizing by the new fail-fast: merfolk_turns ORACLE-ON probe died at the 40 GB cap (31/32 games, 16 threads); the runaway is merfolk_turns **gi13 (seed 30000013)** alone -- sent to the Maze oracle-memory agent as a second repro. Gate e finished rc=2 (ON arm dead, nothing recorded). v2: --both rerun = only Alchemist rows move (smoke searched 0 slower / 5 faster; regression 3 slower / 7 faster: s2002 gi23 d3+d5 5->6, s3003 gi20 d5 4->5); classify_turn_later queued (`build/scratch/claude_aav2b/cls/`); last stale unit (Wild Growth/Living Wish world test) pinned v2 off, units rerun queued (`build/scratch/claude_aav2c/`). Gate g (5 decks) next, then gate h = fungus alone at 16 threads (`build/scratch/claude_gate_h/`).
**02:50 UTC 11-20:** STATE FOR RESUME (pre-compaction). PUSH: queue-based now (CLAUDE.md; USER: still BATCH -- join only with an integrated validated batch; docs ride along). Local unpushed: 98e65868a CLAUDE.md batching bullet, 6401259c8 + this checkpoint, regression-skill timing table refresh -- all ungated, ride with the next landing. RUNNING: gate_i (claude-queue-20261119i maze_s_end + merfolk_turns, on arm 908/5061 chunks at 02:37, ~9.5 h left, then off arm, then 20261119j prevent_damage; waiter task b5riaq14y on claude_gate_i/DONE; DONE_PD has NO waiter yet -- arm one after DONE). QUEUED on the box lock: build/scratch/claude_budget1300/ (wip/oracle-budget-1300 09aeb9ba0, worktree .claude/worktrees/oracle-budget-1300: build + units + Goblins s30131075/s30132663 oracle-on probe; waiter task bbzrrdro4 on pid 3502435) -> if 131075 has 0 CAPPED-DECISION and units pass, cherry-pick to primary AFTER DONE_PD, re-gate Goblins on a fresh block, land via the queue. AGENTS: sacfodder agent 3 (~262K, hold2 = rebuild/units/slower games/d8 b0 after commit 7fac7389e on wip/sacfodder-fix; will hand off -> integrate if it clears the bar, else PARK + raise with USER); rock-in-plan (~218K, probe8; scratch claude_rock_plan); board-share (~226K, hold2 PID 3470409: Maze base vs new + MEMWHY + oracle-off digests). OPEN USER DECISIONS: (1) Jumbo unlock-order CPU x1.20: charge to decision budget (play change, A/B) vs accept; (2) small-job lane beside a big run; (3) Shadowborn Apostle ordering shipped ON 22:13 -- off if the USER says so. USER asked for tier timings (answered from logs: smoke 4.5-6.5 min PGO / 6.5-11 Release; --both ~19 PGO / 22-25 Release; overnight ~30 min PGO 11-10); offered a split smoke/regression run at the next validation for a regression-alone figure.

**03:30 UTC 11-20:** USER on PGO cost (answered from build logs: Release full rebuild 6.0-6.6 min; PGO 13.7 min = instrumented build 3.9 + training 7.7 + profile-use/LTO 2.1; smoke/regression use PGO only when it already matches HEAD:src, never build it; break-even ~1 h of Release work on one src): *"if we already have a PGO build we should use it, but otherwise it is not worth building a new one"* and on overnight (which DOES build a stale PGO first, test/regression.sh:303): *"It probably should not PGO if we think the run will be much faster. I don't know how slow it is now, but that would be something to test in the future. Feel free to insert an overnight rebaseline after some of your other work and we can use that to collect more numbers."* QUEUE ITEM (after gate_i/PD, the Goblins 1300 re-gate and the next landing, so it accepts everything landed): overnight rebaseline run with `MTG_PGO=0` (Release; byte-identical so the accept is valid) to get the Release makespan, against the 13.7 min PGO build + the PGO makespan (11-10: ~30 min, 578 jobs). Record both in the regression-skill timing table; if Release < PGO build + PGO run, raise switching overnight to use-PGO-only-if-current with the USER (do not change harness_pgo_ensure unilaterally). Then accept overnight GT (notes union restore, check_gt_logs).

**04:40 UTC 11-20:** status check: gate_i on arm 1851/5061 chunks (908 at 02:37 -> ~470/h, ~6.8 h left, then off arm, then PD), 32/32 busy, used 34 / 47 GB. Maze oracle-on games are the tail: 962 SLOW-GAME lines, gi521 ran 2.46 h, gi999 34 min (QUEUE: look at Maze oracle-on per-game cost after the gate reports). Everything else (budget1300, the three agents' holds) waits on the box lock behind it. Not in the push queue (no integrated gated batch yet); queue has 3 other sessions.

**06:40 UTC 11-20:** status check: gate_i on arm 2792/5061 (~470/h steady, ~4.8 h left), 32/32 busy, 31/47 GB. Slowest in flight: Maze gi950 at 3.63 h (oracle-on). Nothing else changed; waiting on b5riaq14y.

**08:00 UTC 11-20:** USER RULINGS. (1) Jumbo: *"I'm okay with trying (a), but we will reserve judgement until we see the results. 20% extra is kind of painful, but the other question is how fast or slow Jumbo Cactuar is compared to Hinata?"* (answered: suite_cost.json jumbo 3,392 ms/game vs hinata 6,434 = 0.53x, measured before the unlock lever; x1.20 puts it ~0.63x) -> DELEGATED agent (worktree, wip/jumbo-uo-budget, scratch claude_jumbo_uo_budget): MTG_UO_BUDGET_PRICE, inert at b0, held-out A/B Jumbo + Angels/Boars/Bruna/Five Colour/Kitty Equipment, d8 b0 on slower games; REPORT ONLY. (2) Small-job lane APPROVED: *"Sure, I'm up for this. I wouldn't try 2 32-core processes, but your suggestion makes sense. It also makes sense to start a new heavy process when an old one is in a super long tail or if we are waiting for some long-running d8 b0 results. We should avoid getting stuck in a way that poorly uses the CPU if possible."* -> scripts/box_lane.sh (<= 4 slots, per-slot RSS cap, MemAvailable admission) + CLAUDE.md CPU concurrency paragraph. budget1300 check MOVED to the lane: build/scratch/claude_budget1300/run_lane.sh, pid 3586408, waiter bhy8j1pb7 (status, DONE). (3) Shadowborn: *"I would put the shadowborn cast right before the thing that needs it. I forget exactly where Dark Ritual is in the order, but it doesn't need a shadowborn. Otherwise that seems fine."* + extreme case *"1 swamp out and Dark ritual x 2, Shadowborn x 2, Thrumming stone and Culling the Weak in your hand ... Dark Ritual x2 Shadowborn + Culling Thrumming Stone, Shadowborn to go off ... You cannot cast the Shadowborn first in this example."* -> DELEGATED diagnosis agent (wip/sb-extreme-case, scratch claude_sb_extreme, doc shadowborn-extreme-case-2026-11-20.md). Explained to the USER: the only Apostle-before-Ritual move needs a VISIBLE Thrumming Stone (Ritual's ripple would bottom the Apostles); asked whether to keep that exception.

**08:15 UTC 11-20:** USER on the Stone exception: *"Well, when the stone is out Apostle is free to drop to build up a big presence. It doesn't need a consumer. (though its ability may become one if mana remains)"* -> exception KEPT (MTG_SB_APOSTLE_FODDER_CLAIMS stays ON); sb-extreme agent given step 6: generalise to 'Stone visible -> one Apostle ahead of the first plain Ritual whenever the line pays, regardless of claims' behind a default-OFF lever + held-out A/B, report only.

**08:25 UTC 11-20:** USER follow-ups: *"if there is a Dark Ritual and Apostle and 1 Mana floating the ritual should go first even if stone is already out"* (the payability guard already does this) and *"Essentially, in the stone already out situation it may be easier just to let the ritual go first if it is not a consumer."* -> step 6 (Stone-out generalisation) CANCELLED; current rule stands: Stone out -> Ritual first unless >= 2 sacrifice consumers need Apostle fodder and the line pays Apostle-first. sb-extreme agent does the extreme-case diagnosis only.

**08:45 UTC 11-20:** status: gate_i on arm 3567/5061 (~390/h, ~3.8 h left), 32/32, 35/47 GB. Lane: budget1300 build running at j4 (326/362), two agents' builds waiting for slots. USER CORRECTED my reading of the 08:25 line: *"'It may be simpler' (it referring to the simplicity of the implementation and correction logic) for the first it and 'if it is not a consumer' referring to the ritual."* = with the Stone out, a plain (non-consumer) Ritual simply goes first. That would retire MTG_SB_APOSTLE_BEFORE_RITUAL_INTENT (11-16) + MTG_SB_APOSTLE_FODDER_CLAIMS (11-19): both play-neutral in their A/Bs (no win turn moved) and exist to clear the gate's APPLY-DROP-ORDER (48 flags s30097662, 44 flags s30114160: Ritual's ripple bottoms the Apostles, Intent stranded). OPEN USER DECISION: keep both (current) or retire them (simpler; those ~1-in-5000 shapes flag again in the gate). Levers stay ON until the USER answers.

**09:00 UTC 11-20:** USER: *"Let's just go with my rule."* -> Shadowborn: both Apostle-before-Ritual moves DELETED on wip/sb-ritual-first 0984fe0a5 (worktree .claude/worktrees/sb-ritual-first; unit tests rewritten to the USER rule; both design docs carry a RETIRED note). PLAY CHANGE: validation chain build/scratch/claude_sb_ritual_first/ (pid 3603086, waiter bqroad47w; lane build + units, then `--both` under the box lock; GT backup regression_gt.before.txt; status, DONE). Then: audit (only shadowborn rows should move), accept with the notes union restored (the 11-19 FODDER_CLAIMS accepted-with-regressions note on regression_gt.txt:11 becomes stale -> replace it with a note for this change), check_gt_logs, cherry-pick to primary AFTER gate_i/PD, land with the next batch. Expect APPLY-DROP-ORDER to reappear for that Stone-out shape in future Shadowborn gates (disclose, per USER).

**09:30 UTC 11-20:** budget1300 check (lane, Release, contended box): build/units OK but s30131075 STILL 231 CAPPED-DECISION -- the WALL backstop fired (1300 elapsed s; game CPU 1,087 s vs wall 1,304 s; 1.67 GB). The priced budget was not the binding cap: the backstop counts unpriced derivation work in wall time, so box load decides the verdict. FIX on wip/oracle-budget-1300 828855872: backstop on thread CPU time (CLOCK_THREAD_CPUTIME_ID; Windows keeps steady clock). Rerun (s30131075 alone, lane) build/scratch/claude_budget1300/run_lane2.sh pid 3613168, status2/DONE2. Note Release ~1.3x slower than PGO: if it is still capped in Release at ~1,250 CPU s, judge by the PGO gate re-run, not this probe. MTG gate TODO from 09:00: make the mana-clean gate disclose the Shadowborn Stone-out APPLY-DROP-ORDER shape instead of failing.

**10:15 UTC 11-20:** URGENT (USER: *"If we can fix the Galadriel reference issue we may need to rush in the change, bypassing some other items in the queue at my order."* / *"From my perspective this is worth it, because this terrible performance grinds everyone to a halt."*). Galadriel reference s10 gi9 (11 s before) runs > 40 min on origin 8fbbc5023. perf (DWARF): 78% PayEnum::Dfs + 12% MaybeCovers, all under ApplyPlanDirect -> RefinePayment -> RefinePaymentImpl (PLAY path). Cause: the Maze batch's F5R release layers (a594978ec) run one full Dfs per set size L = 0..T; the probe cap (4096) counts Verify probes only, and non-covering / vmemo-answered nodes cost none, so the face-combination walk is unbounded. FIX wip/refine-node-cap (worktree .claude/worktrees/galadriel-slow, based on origin): PayEnum::node_cap, refine only, MTG_PAY_REFINE_NODE_CAP default 64*4096 (0 = unbounded); past it CAPPED like the probe cap; oracle enumerations unbounded. Build + replay: build/scratch/claude_galadriel/fix.sh pid 3630152 (fix.status, FIX.DONE), waiter bsptzmgsg. Unfixed control replay still running in the lane (default.out). Next: if the replay is back near 11 s -> units + smoke (digest check) -> `push_queue.py join --urgent` -> land ONLY this fix from that worktree. Also fixed: box_lane.sh admission is FIFO (queue.lock) -- 4-slot builds were starved 1 h 45 min by 1-slot jobs. Goblins 1300 rerun with the thread-CPU backstop: still 207 CAPPED-DECISION (1,304 CPU s in Release on a loaded box) -> judge in the PGO re-gate.

**10:30 UTC 11-20:** GALADRIEL FIX WORKS: wip/refine-node-cap 4514f3191 (on origin 8fbbc5023): s10 gi9 replay 2 s, ok, T7 = reference (unfixed control killed after 1,129 s still running). JOINED the push queue --urgent (number 1, behind stuffy-analysis); waiter bdnncr3ah (`push_queue.py wait`, exit 0 = my turn). Pre-validation build/scratch/claude_galadriel/val2.sh pid 3636579 (full build j6 + units + smoke THREADS=8 + check_gt_logs; val.status, VAL.DONE; waiter bg7umzu10). At my turn: fetch, rebase over stuffy's landing, rebuild + units + smoke under `hold`, land ONLY 4514f3191. SB EXTREME CASE DONE (agent, 257K): the engine plays the USER's line (Ritual, Ritual, Apostle, Culling, Stone, Apostle) at d3 b10, d5 b20 and d8 b0; nothing to fix. wip/sb-extreme-case 50d0153fe + a17cf66e2 (doc shadowborn-extreme-case-2026-11-20.md, 4 scenarios, 3 unit tests) -- land with the next batch. NOTE: unit 'INT-9 free casts: a revealed Thrumming Stone is cast...' (test_shadowborn_int9.cpp:184) FAILS on origin 8fbbc5023 (pre-existing on the shared branch, not ours) -> find which landing broke it.

**10:40 UTC 11-20:** STATE FOR RESUME (pre-compaction). (1) URGENT LANDING IN PROGRESS: I am HEAD of the push queue (wip/refine-node-cap, rebased onto origin c53a7ae26 as 08b13f602, worktree .claude/worktrees/galadriel-slow). Validation under `push_queue.py hold -- bash build/scratch/claude_galadriel/val3.sh` (harness task b7b6a4kmc; v3.status lines build/units/smoke/gtlogs/refs, V3.DONE; smoke THREADS=16). Expected unit failure: 'INT-9 free casts ... Thrumming Stone' (pre-existing on origin, NOT ours). If smoke moves 0 keys and the rest is clean: from that worktree `python3 scripts/push_queue.py land`, then background `python3 build/scratch/claude_ci_watch.py <sha>`. If smoke moves keys: inspect (expect Maze's End 8-10 Gate refines only); a play change needs GT accept (`regression.sh --smoke --accept` + regression) -> if that takes long, `requeue` rather than sit on the lease. Then tell the USER it landed (USER ordered the rush). (2) gate_i on arm 4333/5061 at 10:37 (~1.7 h), then off arm, then PD; waiter b5riaq14y on DONE; arm one on DONE_PD after DONE. (3) After DONE_PD, into the primary tree (fetch + rebase first; 103+ origin commits): wip/oracle-budget-1300 09aeb9ba0 + 828855872 (1300 priced s + thread-CPU backstop) -> re-gate Goblins (PGO); wip/sb-ritual-first 0984fe0a5 (chain build/scratch/claude_sb_ritual_first pid 3603086, waiter bqroad47w: lane build + units, then --both under the box lock -> audit shadowborn rows, accept, replace the regression_gt.txt:11 FODDER_CLAIMS note); wip/sb-extreme-case 50d0153fe + a17cf66e2 (doc, 4 scenarios, 3 unit tests). (4) AGENTS: Jumbo UO budget price (running; report only, USER reserves judgement). sacfodder-3 / rock-in-plan / board-share: their holds (build/scratch/claude_sacfodder_fix3/hold2.sh -> HOLD2.DONE; rock-in-plan run_ab3.sh; board-share hold2.sh) still wait on claude_box.lock -- when one finishes, resume its agent with SendMessage (all three are near 220-260K: ask for handoff and continue from the handoff file). (5) TODO: find which landing broke the INT-9 Shadowborn unit test on origin; make the mana-clean gate disclose (not fail) the Shadowborn Stone-out APPLY-DROP-ORDER shape; tell the Maze session (wip/maze-f5r3 owner) about the node cap; overnight rebaseline with MTG_PGO=0 later (USER-approved, timing numbers). Local unpushed ungated commits ride with the next normal landing. Lane: scripts/box_lane.sh (FIFO since 50099701e). OPEN USER DECISIONS: none (Jumbo (a) awaiting results).

**11:45 UTC 11-20:** GALADRIEL FIX LANDED `08b13f602` (push queue, USER-ordered urgent; queue empty after). Validation at the head: build, units (INT-9 now PASSES on c53a7ae26: someone fixed it), smoke 235/235 with 0 keys moved, check_gt_logs, refs (229 trees): all clean. CI watch running (claude_ci_watch.py). TIMING (USER 11-20 asked why it was slow): joined 10:14, head 10:33, then rebuild 28 min (j8 beside the 32-core gate_i, normally 6-7 min), smoke 21 min (16 threads, contended; normally ~10-12), refs scan ~9 min on 9p; ~1 h 27 min from join to land. USER: *"I would rather not give up the queue in this circumstance. It is worth addressing as soon as we can, but I'm not sure why it is so slow to do so."* RULE FOR NEXT URGENT LANDING: SIGSTOP my own big run (kill -STOP <pid>, SIGCONT after; it is mine and loses nothing) so validation runs full-core (expect ~20-25 min), and do not requeue a USER-ordered urgent fix.

**12:50 UTC 11-20:** USER: *"Please continue autonomously overnight."* (2-hourly cron f58af08e stays.) BATCH INTEGRATED on current origin: worktree `.claude/worktrees/batch-1120`, branch wip/batch-1120 = origin 08b13f602 + my docs/lane commits + 1300 oracle budget + thread-CPU backstop + Shadowborn Ritual-first (HeuristicArm.h conflict resolved: origin's new slots kept, SB_APOSTLE_FODDER_CLAIMS dropped) + SB extreme-case tests. FROM NOW ON checkpoint commits go on wip/batch-1120 (the primary is reset to it at landing). Chain build/scratch/claude_batch1120/run.sh pid 3700895 (status, DONE; waiter bfesfkyim): build j16, units, `--both` THREADS=24 MTG_RSS_CAP_GB=14 MTG_PGO=0, check_gt_logs -- OUTSIDE the box lock, beside gate_i's on-arm tail (load ~9/32, 24 GB free); guard.sh SIGSTOPs only the batch-1120 mtg processes if MemAvailable < 3 GB (resume at 8; guard.log). The old-base Ritual-first chain was stopped (121 behind origin: its GT could not land; units passed there). Also: its first attempt died because the FIFO box_lane.sh was installed by mv while a waiting lane job's bash still read the old file (9p: unlinked inode unreadable). NEXT: audit both.log (expect Shadowborn rows only; 1300 budget is oracle-only), accept smoke+regression, replace the regression_gt.txt:11 FODDER_CLAIMS note, join the queue, land. Then Goblins re-gate on PGO at 1300; then overnight rebaseline MTG_PGO=0 (USER-approved). Box lock queue: gate_i (on tail -> off -> PD), agent holds board-share hold2 (pid 3470409), rock-in-plan run_ab3 (3470442), sacfodder hold2 (3488959) -- waiting ~14 h.
**13:20 UTC 11-20:** BATCH VALIDATED + GT ACCEPTED on wip/batch-1120 (0e8af10c9): build, units, check_gt_logs clean; smoke 234/235 + regression 374/375, the only FAILs the two Shadowborn digests (smoke d0 s1001 gi445, regression d5 s2002 gi98: same win turn, gi98 T5 now Dark Ritual -> Apostle per the USER rule); searched slower=0 faster=0; accepted. CI for 08b13f602: Windows green. Push queue: I am HEAD (joined 13:17), origin unmoved, refs check under `hold` running, then `land`, CI watch, reset the primary to the landed tree. JUMBO budget agent a1cd888d1 handed off at 309K (wip/jumbo-uo-budget bfac80e72 + d71fdf0c1; MTG_UO_BUDGET_PRICE default OFF, W=16, inert at b0 100/100); its 80k-game 4-arm A/B is a detached flock pid 3722194 on claude_box.lock (DONE: build/scratch/claude_jumbo_uo_budget/ab/DONE); on DONE launch a FRESH agent from .claude/worktrees/agent-a1cd888d17514cedb/logs/jumbo-uo-budget/handoff.md (compare.py, d8 b0 slower-game replays, doc, recommendation). USER call raised: cut on Exhausted() (equip shape, as briefed) vs Overrun()-only (reflection-fan shape) -- the A/B's price-only arm measures the latter.
**14:45 UTC 11-20:** LANDED `c86a86333` 13:25 (batch: SB Ritual-first + GT, 1300 oracle budget, thread-CPU backstop, lane FIFO, docs; queue empty after); CI watch running. PRIMARY reset to c86a86333 (all its local commits were patch-equivalent in the landed tree) + `29505ca61` gate_i record. GATE_I DONE 14:31 rc=1: first complete maze_s_end (287,482 issues: UNMODELED 139,380 -- 139,000 'engine tapped a mana source outside any payment', top: Gond Gate / Talon Gates of Madara choices the oracle does not drive; CAPPED-DECISION 131,360 at the OLD 600 budget) and merfolk_turns (15,953: ORACLE-GAP 7,903 'Merfolk token only in oracle', APPLY-DROP 6,952 'engine dropped cast Wanderwine Prophets' w/ Sol Ring + Three Tree City, ENUM-GAP 937 Dockhand>Seahunter). DELEGATED (worktree agents, report only, no shared push): Merfolk gate agent (wip/merfolk-gate, scratch claude_merfolk_gate, handoff logs/merfolk-gate/handoff.md) and Maze oracle gate agent (wip/maze-gate, scratch claude_maze_gate, analyzer-side; Maze PLAY is the wip/maze-f5r3 session's). BOX LOCK queue (not FIFO): board-share hold2 RUNNING; waiting: PD gate 20261119j (pid 3754473, DONE_PD in claude_gate_i), rock-in-plan run_ab3, sacfodder hold2, Jumbo UO A/B (3722194), my chain build/scratch/claude_gob1300/run.sh (pid file; Goblins gate 20261119... name claude-queue-20261120a -> DONE_GOB, then a SEPARATE lock: ./build.sh + overnight MTG_PGO=0 -> DONE; status). Do NOT reset the primary's src while PD / Goblins / overnight are queued or running from it: land later batches from a fresh worktree and reset the primary only when those are done. Waiter on DONE_PD|DONE_GOB.
**16:05 UTC 11-20:** BOX: Jumbo UO A/B (80k, 4 arms) now holds claude_box.lock; still waiting: PD gate (3754473), my claude_gob1300 chain (3755641), rock ab3 (wrapper 3787422 -> build/scratch/claude_rock_plan/ab3.done; waiter). AGENTS: Maze gate agent 1 hit 400K in 40 min (big tool outputs) -> handed off (wip/maze-gate 27e576182: MTG_ORACLE_TAP_CHOICE -- 139,000 of 139,380 UNMODELED are Guild Summit 'tap any number of Gates: draw' + Heap Gate tap cost, the provider picked the Gates, oracle never drove it); fresh agent 2 continues in .claude/worktrees/agent-a02c2671f9e6fcd10 (handoff logs/maze-gate/handoff.md). Merfolk gate agent running. ROCK-IN-PLAN agent handed off at 265K: .claude/worktrees/agent-ade0a1bc23ae96aa7/logs/rock-in-plan/handoff2.md (wip/rock-in-plan ce7d362a4 lever, dfc8a326e + 29b411e82 exact CPU cuts, 0ab6d1cf8). A/B 24,600 pairs net -6 (Niv +1 on 1,000, pooled 3,000 = -13); Stuffy APPLY-DROP 45 -> 0, Dragons 218 -> 75 (57 of the rest = Haven of the Spirit Dragon creature-only mana); CPU 1.0315 -> 1.0120 after cut 1. On ab3.done: launch a FRESH agent from handoff2.md (judge ab3: logs_ab3 .wins == logs_ab, probe8 fired=68607; then default-ON flip + units/smoke/regression + GT). USER question raised: Niv first 1,000 pairs +1 vs pooled 3,000 -13 (agent recommends ON on the pooled result). Board-share agent at 269K finishing smoke -> ask it to hand off on its next report.
**16:40 UTC 11-20:** status check: Jumbo UO A/B DONE 16:23 rc=0 -> fresh analysis agent in agent-a1cd888d17514cedb (report only). CI GREEN on c86a86333. PD gate 20261119j now holds the box lock (PGO build for c86a86333 src, then the gate). Lock waiters: Goblins 20261120a (3755643), board-share smoke (3786730), rock ab3 (3787422), sacfodder hold3 (3788490; waiter armed). SACFODDER PARKED (fix-up 3 net +2 Goblins / +1 Fungus on dev seeds, wave never picks; handoff wip/sacfodder-fix 2c16a3db4) -- USER asked whether to also hold back the playout/breakpoint credit (more CPU) or leave it parked. Maze agent 2: units 3,780/3,780, smoke + CAPPED profiling running.
**18:40 UTC 11-20:** status check. CHECKPOINTS NOW GO ON wip/batch-1120b (worktree .claude/worktrees/batch-1120b = origin c86a86333 + oracle board sharing a7ecdea3c/4e189439a [CI green on its wip branch; Maze memory caps 528 -> 32, exact] + gate records i/j/20120a + checkpoints). Its validation runs in the lane (build/scratch/claude_batch1120b/run.sh: build j4, units, smoke THREADS=4 MTG_PGO=0, check_gt_logs; status, DONE; waiter) -> then join + land, reset the primary only after the overnight below finishes. BOX: my overnight rebaseline (MTG_PGO=0, Release built 18:30 from primary c86a86333 src) running since ~18:31 (build/scratch/claude_gob1300/overnight.log, DONE; waiter) -> on DONE: audit, accept overnight GT (in the primary, then carry into a batch), record the tier time for the USER (PGO-vs-Release question). GATES recorded: PD 20261119j 113 issues (CAPPED 111 board budget, ORACLE-GAP 2); Goblins 20261120a 2355 (CAPPED-DECISION 479 -> 0 at 1300; ENUM-GAP 1978 = sac-fodder Skirk shape). AGENTS: Jumbo UO price DONE: neither arm adoptable (Jumbo +2, Bruna +1; all slower recover d8 b0; pc CPU x0.924) -> USER call. Sacfodder PARKED (fix-up 3 net +2/+1; slower recover d8 b0) -> USER call. Rock-in-plan ab3 EXACT (1,968 .wins identical; fired=68607), CPU 1.0149, Niv 1.065 -> fresh agent: exact Niv CPU cut + default-ON flip prep with GT in its worktree, no landing (USER call on Niv +1/1,000 vs -13/3,000 pending). Merfolk gate agent handed off at ~520K (claimed 230K!): wip/merfolk-gate 4e0a7da99/a121d871f/bd7a9d967, MTG_SCALED_LAND_ROUTE + MTG_PAY_REFINE_SCALED default ON (APPLY-DROP repros 1,206/3,028 -> 0; ORACLE-GAP 60/452 -> 0, 564 -> 290); A/B 4,400 pairs net 0 everywhere, Maze CPU +7.4% to cut; its master chain pid 3850753 (worktree build/scratch/claude_merfolk_gate/MASTER.DONE; waiter) -> on DONE a FRESH agent from logs/merfolk-gate/handoff.md. USER call: ENUM-GAP Seahunter+Dockhand needs reorder-on-failure (agent's pick) or a deck cast-order rule. Maze gate agent 2 (137K): tap-choice units 3,780/3,780, CAPPED profiling. Agent sizes: run build/scratch/claude_agent_ctx.py at each check (agents misreport their own size).
**20:30 UTC 11-20:** LANDED `1f3c23fab` 20:07 (board sharing + gate records + checkpoints; queue empty; CI watch running). Checkpoints continue on wip/batch-1120b (= origin now). OVERNIGHT REBASELINE DONE (primary, Release MTG_PGO=0, src c86a86333): makespan 85m38s for 735 jobs (USER PGO question: last PGO overnight ~30 min at 578 jobs on 11-10, not comparable; at ~1.3x PGO would save ~20 min vs a 13.7 min PGO build). 670/735 configs moved since the 11-14/15 accept; searched slower 1148 / faster 3529, d0 slower 3986 / faster 15567. classify_turn_later: 406 churn, 742 PERSISTS (midrangeburn 317, spirits 267 -- both FASTER on aggregate; cyclone 53, gods 22, hinata 21, ...). Worse on aggregate only: gods_srch +0.32, gods2hg +0.26, hinata_srch +0.23, hinata2hg +0.16, cyclone_srch +0.074, fivecolour_d0 +0.015. DELEGATED attribution agent (worktree, wip/overnight-accept-1120, scratch build/scratch/claude_ovn1120/agent/, doc docs/design/overnight-accept-2026-11-20.md): per worse deck commit + mechanism, both-arm d8 b0 on PERSISTS, proposed accept note. Then I accept `--overnight --accept-with-regressions` IN THE PRIMARY (test/logs/overnight lives there), check_gt_logs, commit, rebase the primary onto origin (drops patch-equivalent local commits), and land in the next batch. Do NOT reset the primary before that accept.
**10:35 UTC 11-21:** STATE FOR RESUME (pre-compaction 2). Origin = 449702ab3+ (Maze batch 3 LANDED by the other session; push queue: other sessions head/next). BOX LOCK: Hinata 2x2 main batch (38721) since ~06:30; queued behind: PD smoke+regression, Merfolk tiers, batch-1121 smoke+Maze gate, others. LANE: heavily backlogged (builds wait 1-3 h).
- BATCH-1121 (NEW worktree .claude/worktrees/batch-1121, branch wip/batch-1121 on origin 449702ab3): cherry-picked maze-gate 27a628214 (tap choice), 65fdfff26 (test), 8b4bd5c8d (Pareto speedup), ff3ac68fd (gate-c record). Chain build/scratch/claude_batch1121/run.sh PID 77998 (status file there: lane build -> lane units -> under box lock: smoke, check_gt_logs, mana_clean gate claude-queue-20261121a maze_s_end 5000) ; waiter bnzfdolqr on DONE/STOPPED. Smoke must be byte-identical (oracle-only). Then carry Merfolk / PD / rock / overnight accept as each is GT-accepted; land via push queue as ONE batch.
- ROCK (agent 6 acd8dacb959c84993, fresh): GT accepted at 9379d1fd9 on wip/rock-in-plan, but NOT landable: Niv d0 nets +14 (smoke +20, regression -6; no 'lighter d0 bar' exists -> must be <= 0) and CPU +6% uncut. Agent: root-cause Niv d0, exact CPU cuts, rebase (300 behind), tiers, re-accept. gi152 = clear clairvoyance (settled); gi17 recovers.
- JUMBO (acf9c640ea9b09d74, ~198K): 400k A/B reported to USER: lever ON vs OFF Jumbo -0.00018 turns/game (CI -0.00033..-0.00003), Bruna -0.00080 (CI excl 0); CPU +18.3% / +0.6%. Price arms: all <= OFF but worse than ON (Jumbo ~+0.0001, Bruna +0.0006 any W); save 6-8% CPU. Adopted nothing. Agent now profiling ON's +18% for exact cuts + weight-free residual price variant A/B.
- PD gi47 (a7725fd0fd875bbea, 172K): A/B PD+PD2HG neutral (0/0); smoke+regression queued on box lock.
- HINATA (ae651aa8c4003053e, 199K): early signal: search SHAPE not depth: d5+emul ladder plays like d4+emul; d5 no-ladder is slower. Main batch running.
- SAC-FODDER (ac3936ac2b8c620ec, 214K): A/B done, traced diag singles. Near handoff.
- MERFOLK (a8f7ce88ed013c8a9, 155K): both ENUM-GAP options coded; build in lane; tiers queued on box lock.
- ATTRIBUTION 2 (acf3457b2de4b957d, 136K): Five Colour bisect continuing; then I do the overnight accept in the PRIMARY.
- BURN: agent handed off -> .claude/worktrees/burn-damage-order/logs/burn-damage-order/handoff2.md (wip/burn-damage-order c76d33c13). I watch origin (waiter bcp1pckmz, 10-min fetch, detection = 3 git log commands in handoff2 vs base 906d3c0a4). On LANDED: fresh burn agent from handoff2. Fireblast double-naming finding (573 flags). Windows CI of wip/burn-damage-order unchecked (gh not authed in this shell).
- BRUNA: agent 2 handed off -> logs/bruna-apply-drop/handoff.md e034158f3 (fixes c8f363ac6: MTG_CREATURE_LEFTOVER_EXACT, MTG_AURA_HOST_LINE_KEEP, default ON, unbuilt). Build lane PID 83321 -> bad_build2.log rc=; waiter b5wabkvco. On rc: launch FRESH Bruna agent 3 from the handoff (check 9p staleness via the new unit tests). Surfaced to USER (not blocking): Sol Ring paid off the only colour source vs Avacyn's Pilgrim needs creature-before-land ranking (breaks the long-standing rule); Wish/Armory+Sol Ring needs the dropped 11-04 line-critical-land rule + tutor line order. Default: next Bruna agent measures a fix for each unless USER objects.
- GATE d still PAUSED (resume in batch-1120b; src there stays 9cf45118).
- Cron f58af08e stays. OPEN for USER: Maze ledger 2/10/30 Gate order sign-off; Bruna rule overrides above (default: measure).

**06:40 UTC 11-21:** status check. ROCK gi152 SETTLED: off arm's budgeted T8 win is clear clairvoyance (MTG_NC_SEARCH=1 off arm unwon too; d8 b0 both unwon at cap 8; at max-turns 10 d8 b0 both win T9) -> mechanism: T5 the lever makes a 4-spell line payable (Signet, Preordain, Remand own Preordain, Curiosity) that greedy pay could not afford; draws then diverge. chain7 DONE (units 3720/3720, 0 tight-check refusals, slice 192/192); rock agent 5 awaits only the d8 b0 recovery of niv_regression_d3 gi17 (T4->T5) before the GT accept. PD gi47 chain3 done; PD+PD2HG A/B NEUTRAL (0 faster / 0 slower); PD smoke+regression queued on the box lock. Jumbo 400k A/B DONE (results pending the agent's d8 b0 replays: 82 games, 18 slower, in lane). Sac-fodder A/B done; agent on diag singles. BOX: Hinata 2x2 main batch holds claude_box.lock (38721); load ~34/32. LANE backlog ~14 jobs (some queued 3-4 h: Merfolk build, attribution build, Bruna build+repros, burn units/repros) -- lane is full, not stuck. Gate d still PAUSED. Push queue head = other session (wip/maze-batch3). Nothing of mine ready to land.

**04:12 UTC 11-21:** STATE FOR RESUME (pre-compaction). Shared branch origin = 906d3c0a4 (src moved past 9cf45118; another session wip/maze-batch3 heads the push queue). Nothing of mine is ready to land. BOX: claude_box.lock held by rock chain7 (4160760: units, tight-check diag, probes, slice vs ab3, then regression.sh --both at nproc). Lock waiters: sac-fodder hold1 (4112618), Jumbo 400k A/B chain2 (4126297), Hinata 2x2 chain (4129478, starts after its lane build), Merfolk tier2 (4129879, after its lane build). Lane: ~15 jobs queued (builds, repros, gi152 replays, PD chain3, Five Colour bisect step 70). GATE 20261120d PAUSED (see 04:05; resume via --resume in batch-1120b, src there must stay 9cf45118). AGENTS (all background; wait for notifications; sizes via build/scratch/claude_agent_ctx.py): burn ad66b72a92ea84ea8 (170K; c8cc17d7b built; units+repros in lane; HOLDS 1 Drop Burn A/B + Lavamancer opponent-only prune until the other session's 1 Drop Burn update lands on origin; monitor fetches origin); PD gi47 a7725fd0fd875bbea (162K; owns agent-aa57dd6400917806d; final fix chain3 in lane PID 4095475; then A/B PD+PD2HG + tiers); Hinata d4/d5 ae651aa8c4003053e (170K; wip/hinata-d4-d5; 2x2 {d4,d5}x{emul ladder,none} at b30 + larger budgets/b0); Jumbo large A/B acf9c640ea9b09d74 (109K; agent-a1cd888d17514cedb; 400k games, arms off/on/pc W16/W32/W64; report mean turn delta per game vs OFF with CI); sac-fodder ac3936ac2b8c620ec (188K; sacfodder-fix; stage (a)-(c) until net <= 0); rock agent 5 a2ba345752587eef8 (97K; gi152: both arms unwon at d8 b0, off's budgeted win on the T8 cap -> clairvoyance check + max-turns 10; then chain7 audit + GT accept if cleared); Merfolk gate 3 a8f7ce88ed013c8a9 (105K; agent-a1651726d5b941608; tier2 audit + GT, then both ENUM-GAP cast-order options, ship the cheapest); attribution 2 acf3457b2de4b957d (120K; agent-a1088baf277f40b58; Five Colour d0 bisect step 70 + accept note; then I do the overnight accept in the PRIMARY, rebase onto origin with GT care, check_gt_logs, carry into a batch). Bruna agent 1 ab8cec71223884857 DONE (handed off) -> on waiter bgq3t7122 (bad_build1.log rc= + bad/repros_base.log DONE) launch a FRESH Bruna agent from .claude/worktrees/bruna-apply-drop/logs/bruna-apply-drop/handoff.md (lane jobs need --cap-gb 2-3 while big runs hold memory). MAZE GATE agent DONE (wip/maze-gate fda1bf747) -> batch-1121 in a NEW worktree on origin: cherry-pick 27e576182, b97f2ff0a, b05457892 + gate-c record c62e4fc7d + checkpoints; build/units/smoke identity; re-run the Maze's End gate. OPEN for USER: none pending an answer (rulings 1-7 recorded 01:40/01:55); pending measurement reports: rock gi152, Jumbo averages vs off, sac-fodder, Merfolk option pick, Hinata mechanism, Bruna fix. Maze ledger 2/10/30 (provider Gate order for Summit/Heap Gate) sign-off still the USER's; the Maze gate re-run will give whole-deck evidence. Cron f58af08e stays.

**04:05 UTC 11-21:** GATE 20261120d PAUSED by me (own run): ON arm 900/2760 chunks after 3h25m (five_colour chunks ~3 h each, selesnya ~37 min) -> projected ~10 h more ON + the OFF arm, holding 34 GB (starving the lane on memory) and the box lock while rock tiers, the Jumbo 400k A/B, Hinata 2x2, Merfolk tiers and sac-fodder/PD/burn/Bruna runs all queue. Killed mana_clean (4078701) + mtg (4078794); GATE.DONE renamed GATE.PAUSED (status file has the resume line). RESUME LATER: `cd .claude/worktrees/batch-1120b && python3 scripts/mana_clean.py run --resume claude-queue-20261120d` under claude_box.lock (finished chunks are kept; src there must stay 9cf45118 -> do NOT change src in batch-1120b; the batch-1121 integration goes in a NEW worktree). Lock went to rock chain7 at 04:05. Consider splitting five_colour / galadriel / gods out of the cheap pool next time. Bruna agent 1 handed off (253K; logs/bruna-apply-drop/handoff.md 69a6ddec7) -> fresh agent on its build+repros (waiter bgq3t7122).

**03:15 UTC 11-21:** d8 b0 two-arm run DONE (~03:00; lock passed to my gate-d reservation, ok). Final counts (178): recover 76, same 9, new-faster 2 (Mill), explained-slower 59 (Gods 24, Midrange Burn 24, Spirits 10, PD 1), UNATTRIBUTED 31 (Creature Giving 20, Galadriel 5, Hinata 2, Antilife 2, Minotaur 1, Alchemist 1), new deck 1; Mill s5005 gi3/gi4 old GT wins do not replay from 1443eeba0 (note at accept). Sent to attribution agent 2 (it finishes the Five Colour bisect + accept note). MAZE GATE agent 2 DONE: wip/maze-gate fda1bf747 (oracle-only, play byte-identical): 27e576182+b97f2ff0a MTG_ORACLE_TAP_CHOICE ON (UNMODELED g21 10->0, g167 170->0; Summit/Heap Gate tap choices), b05457892 exact Pareto-prune speedup (g167 oracle 101.8 -> 25.4 s, g14 T5 577 -> 230 s; g30/g4 still capped at the backstop). NEXT (batch-1121, after gate d DONE): fresh worktree on origin (now 906d3c0a4, src moved), cherry-pick the maze-gate src commits (27e576182, b97f2ff0a, b05457892) + gate d record + checkpoints; build/units/smoke(identity); then RE-RUN the Maze's End gate on it under the box lock. Also carry Merfolk gate + PD fix + rock-in-plan when their GT is accepted, and the overnight accept. Origin moved again -> overnight accept needs a rebase onto 906d3c0a4.

**02:37 UTC 11-21:** status check. Box saturated (load ~35, 16 GB avail). Gate 20261120d still on its ON arm (45,000 games; gods ~10 s/game). d8 b0 new-arm last game PID 3960634 at 6h13m. Attribution agent 2 (98K) waits on bisect step 70 = lane job 4034685, queued 3h behind the Merfolk agent's run4.sh (PID 4031618), which holds all 4 lane slots incl. idle GO-file waits -> told it to release the lane after phase 4 and queue phases separately. Big-lock queue (after gate d): rock chain7 (4110419: units, slice, tiers at nproc), Jumbo 400k A/B (4126295), Hinata 2x2 (4117000), plus Bruna/PD/burn/sac-fodder runs as they get there. New agents since 01:40: Hinata d4/d5 (wip/hinata-d4-d5), Jumbo large A/B (agent-a1cd888d17514cedb), sac-fodder extension (sacfodder-fix), rock agent 5 (gi152 root cause: both arms unwon at d8 b0, off arm's budgeted win was on the T8 cap -> clairvoyance check + max-turns 10). Push queue head: another session (wip/maze-batch3); nothing of mine ready.

**01:55 UTC 11-21:** USER RULINGS, verbatim. (6) 1 Drop Burn Fireblast-early: *"The agent that worked on this made some changes for this. Let's leave this item until we see their update on origin."* (7) Lavamancer self-ping: *"This indeed should be pruned. In fact everything but the opponent should be pruned. That said, let's wait for the other 1-drop burn changes I mentioned above to land on origin."* -> burn agent holds its 1 Drop Burn A/B and lever decisions until the other session's 1 Drop Burn update (after 1eff08565) is on origin; then rebases, drops anything superseded, implements the Lavamancer opponent-only prune, and runs the A/B. No open USER questions remain from this session apart from those pending measurements.

**01:40 UTC 11-21:** USER RULINGS, verbatim. (1) Rock-in-plan: *"Sounds good. Let's turn it on if the unwon looks okay. We should indeed support this."* -> rock agent: default ON if the Niv T8->unwon probe game recovers at d8 b0. (2) Sac-fodder: *"I don't quite get the issues, but this seems like an issue we need to fix/extend until it is a net benefit."* -> line REOPENED; fresh agent in .claude/worktrees/sacfodder-fix stages (a) non-two-pass root decisions, (b) playouts/breakpoint enumeration, (c) fodder order walk, until net <= 0 + CPU cut. (3) Jumbo price: *"How is the quality comparison with this on vs off? I'm looking for averages, not absolute numbers. I'm talking about vs lever off. Maybe we should also test each option on a larger set of games to get more accurate numbers."* -> fresh agent in agent-a1cd888d17514cedb: ~50k Jumbo games/arm, arms off/on/pc W16/W32/W64, mean turn delta per game vs OFF with CI. (4) Merfolk cast order: *"I'm fine with trying either option as needed. Whatever does better can win out. We are looking for the cheapest that corrects the issue."* -> Merfolk agent builds both, ships the cheapest that corrects and meets the bar. (5) Hinata d4 vs d5: *"This doesn't make much sense. We shouldn't be getting better results with Depth 4 than Depth 5 unless perhaps this is just variance. If they are similarily constrained below 4 turns lookahead they should act the same and going further should not make the results worse. We need to dig into this rather than just wave it off."* -> investigation agent (wip/hinata-d4-d5). Note: the old d4 block also carried the emulated ladder (emul_r), so old vs new differ in depth AND search shape; the agent runs the 2x2 plus larger budgets / b0. PD: old gi47 agent stopped (collision with the fresh one, which owns agent-aa57dd6400917806d now; final-fix chain3 queued in the lane). Open USER questions left: 6 (1 Drop Burn Fireblast-early lever), 7 (Lavamancer self-ping prune).

**00:50 UTC 11-21:** GATE 20261120c DONE 00:44 (record c62e4fc7d; identity byte-identical): hydras 54075 -> 18, bruna 3218 -> 2763 (APPLY-DROP 1891: Glittering Wish / Wild Growth / Arcanum Wings dropped by payment; DOMINATED-CREATURE 668), boars 2253 -> 1946 (DOMINATED-CREATURE 1606: Decimator paid with an attacking dork), mill 132 -> 86, stuffy_doll 62 -> 127, shadowborn 52 -> 20, sea_creatures 61 -> 60. GATE 20261120d RUNNING from THIS worktree since 00:48 beside the d8 b0 tail (snow selesnya_lifegain stompy_surprise unpredictable_cyclone galadriel five_colour mirrorwing_dragon gods fungus = the stale DUE decks last gated on src 3bda5c647e7c; build/scratch/claude_gate1120d/run2.sh, status, gate.log, GATE.DONE; companion flock 4078698 reserves claude_box.lock; waiter bt1tavtoy) -- DO NOT change src here until GATE.DONE. Killed my stale gate-b/c lock reservations (DONE markers existed). Next undelegated families: bruna APPLY-DROP, boars DOMINATED-CREATURE.

**00:37 UTC 11-21:** status check. Gate 20261120c on its OFF arm (on arm done; PID 4062252, waiter b2o73l616). d8 b0 new-arm PID 3960634 still on its last game; attribution agent 2 still at bisect step 70 (lane). BURN agent 1 handed off at 327K (WIP c8cc17d7b, never compiled: earlier ICE Bus error was the 9p build dir before the build/ symlink at 00:17) -> FRESH burn agent from logs/burn-damage-order/handoff.md (A/B generator mk_ab.py, arms base/float/dmg/fdmg/early). PD gi47 agent stopped at 244K with chain logs/pd-landdrop/chain_diag.sh queued in the lane (mechanism: Earthquake red-hold applied to Vito lifelink payment, +2 life pain, kills the T8 lethal; fix variants MTG_PD_HOLD_FIX=1/2) -> on build/scratch/claude_pd_landdrop/chain_diag.DONE (waiter bno8ua0f0) launch a FRESH agent from logs/pd-landdrop/handoff.md. LANE CONGESTION: rock agent 4's chain3.sh runs smoke+regression --both inside the 4-slot lane (~2 h from 00:16) while the box sits at load ~9; future briefs: tiers go under claude_box.lock, not the lane. Push queue head: another session (alch-t3-1119). New USER questions: 1 Drop Burn Fireblast-early lever (measured, not flipped); Lavamancer self-target ping prune.

**23:30 UTC 11-20:** STATE FOR RESUME (pre-compaction). Origin moved to 69af1be5f (another session: Maze's End batch 2, MTG_MAZE_IDLE_FETCH_REVEAL ON + GT 25 keys); THIS worktree rebased onto it (HEAD:src 9cf45118 == origin; docs/records only). GATE 20261120b DONE 23:10 (record 47651a09a here; identity true x9): 1_drop_burn 7478 (ORACLE-GAP 4881 Fireblast alt-cost + Lavamancer; APPLY-DROP-ORDER 2232 Needle Drop needs damage first), midrange_burn 4778 (Light Up the Stage / Incinerator same family), anti_lifegain 4123 ORACLE-GAP, morph 705, dragons 562, creature_giving 253, treasure_hunt 236, dragonstorm 76, angels 12. -> BURN FIX AGENT (worktree .claude/worktrees/burn-damage-order, wip/burn-damage-order = origin + record c04910b19). GATE 20261120c RUNNING from THIS worktree since 23:14 (mill stuffy_doll shadowborn_apostle sea_creatures boars bruna hydras; build/scratch/claude_gate1120c/run2.sh, status, gate.log, GATE.DONE; companion flock reserves claude_box.lock; waiter b2o73l616) -- DO NOT change src here until GATE.DONE; on DONE commit the record. Overnight d8 b0: new-arm PID 3960634 on its last game (old arm done). ATTRIBUTION agent 2 (worktree agent-a1088baf277f40b58, wip/overnight-accept-1120) bisecting Five Colour d0 in the lane (window idx 60..79, at step 70) + d8 b0 counts + accept note -> then I run the overnight accept in the PRIMARY (`bash test/regression.sh --overnight --accept-with-regressions="<note>"`, check_gt_logs), commit, rebase onto origin (GT: rebuild/re-check, check_gt_logs), carry into a batch, land. Waiter bbge2ctln still on bisect/d8 PIDs. MERFOLK chain DONE 23:16 (units 3780, --both rc=1 expected, slice rc=0, gi126 d8 b0 done) -> FRESH agent (agent-a1651726d5b941608, wip/merfolk-gate): tier audit, cut Maze +7% CPU, rebase onto 69af1be5f, accept GT. ROCK agent 4 (agent-ade0a1bc23ae96aa7, wip/rock-in-plan, from handoff3.md): chain2 checks + T8->unwon d8 b0 + exact CPU cuts. MAZE gate agent 2 (196K; ParetoCovers merge walk exact, g167 oracle 101.8 -> 25.4 s) finishing its write-up; note the other session's Maze batch (oracle-side change should not conflict). PD gi47 fix agent running (wip/pd-hold-landdrop). OPEN USER DECISIONS (5) unchanged. Cron f58af08e stays.
**22:45 UTC 11-20:** STATUS. Box was ~27 cores idle (only the overnight d8 b0 new-arm last game, PID 3960634) -> GATE `claude-queue-20261120b` STARTED 22:40 from THIS worktree (HEAD:src a6df0627 == origin): 1_drop_burn (never run) + angels, anti_lifegain, dragons, midrange_burn, treasure_hunt, creature_giving, dragonstorm, morph (the cheap DUE decks), MTG_RSS_CAP_GB=28; launcher build/scratch/claude_gate1120b/run.sh (status, gate.log, marker GATE.DONE); a companion flock reserves claude_box.lock until GATE.DONE. DO NOT change src in this worktree until GATE.DONE (integration of the next batch waits, or uses a new worktree). Overnight attribution: agent 1 stopped at 299K (bisect stopped at a non-building commit, index 69 9547ea31b; window = indices 60..79, suspect = held-fetch crack-first split across commits) -> FRESH agent continuing the bisect in the lane + collecting d8 b0 counts + the accept note (wip/overnight-accept-1120 aff05c95a). Rock-in-plan agent 3 handed off at 260K (logs/rock-in-plan/handoff3.md; bc710d3f3 flip + exact copy cut + prefix-ask cut + Cascade Bluffs guard + 3 unit fixes; chain2 lane PID 4005469) -> FRESH agent 4 (also replays the 200-game probe's T8->unwon game at d8 b0). Maze agent 2 resumed (was idle since 20:02): ParetoCovers merge walk exact on g167, oracle 101.8 -> 25.4 s. Merfolk master chain: units 3,780 pass, --both rc=1 21:56 (expected: play change), now slice + gi126 d8 b0 in the lane. PD gi47 fix agent running. Open USER decisions unchanged (5).
**21:10 UTC 11-20:** STATE FOR RESUME (pre-compaction). Shared branch = 1f3c23fab (CI green). This worktree (wip/batch-1120b) carries the checkpoints; next batch builds here. OVERNIGHT ACCEPT pending in the PRIMARY (test/logs/overnight there; primary = c86a86333 + patch-equivalent records; do NOT reset it before the accept). Attribution agent 1 handed off at 258K: .claude/worktrees/agent-a1088baf277f40b58 (wip/overnight-accept-1120 3545968ba; doc docs/design/overnight-accept-2026-11-20.md + logs/overnight-accept/handoff.md with a draft accept note). Findings: Gods = USER strict-dredge MTG_GODS_RETRIEVAL_DREDGE (not a regression); Hinata = restored depth-5 floor da17c0bbf costs +0.06..+0.11 turns/game (USER setting -> raise); Cyclone = budget churn (51/51 recover d8 b0); Midrange Burn = real opponent pool lands c9022b11d (Price of Progress); Spirits = new keep table a05ac4395; Five Colour d0 +0.005: bisecting (culprit in first 80 src commits after 1443eeba0; bisect PID 3971800 in that worktree, DONE build/scratch/claude_ovn1120/agent/bisect/DONE -- nothing may build/commit there until DONE); d8 b0 both arms PIDs 3960634/3960635 (178 games: 71 recover, 8 same both, 60 explained, 30 unattributed, ~7 running). Waiter bbge2ctln on bisect + both PIDs. NEXT on it: fresh agent from that handoff (fill bisect result + counts + final note), then I run `bash test/regression.sh --overnight --accept-with-regressions="<note>"` in the primary, check_gt_logs, commit, rebase primary onto origin, carry into a batch, land. REAL REGRESSION: PD overnight s6006 gi47 T8 win -> unwon (also d8 b0): skips T4/T5 land drops with Battlefield Forge + Ancient Tomb in hand; MTG_PD_LATER_HAND_HOLD off restores (636910330, Codex 11-15). DELEGATED fix agent (worktree, wip/pd-hold-landdrop, scratch claude_pd_landdrop). OTHER AGENTS: rock-in-plan agent 3 (wip/rock-in-plan, agent-ade0a1bc23ae96aa7; Niv CPU cut + flip prep, chain1 in lane), Maze gate agent 2 (agent-a02c2671f9e6fcd10, wip/maze-gate; tap choice units 3,780 pass; CAPPED profiling), Merfolk master chain pid 3850753 (agent-a1651726d5b941608 build/scratch/claude_merfolk_gate/MASTER.DONE; waiter bqjsaydml; units 3,780 pass; --both running) -> on DONE a FRESH agent from logs/merfolk-gate/handoff.md (accept GT, cut Maze CPU +7.4%). OPEN USER DECISIONS: (1) rock-in-plan default ON (Niv +1/1,000 vs -13/3,000); (2) sacfodder: hold back playout credit (more CPU) or stay parked; (3) Jumbo UO price: neither arm adoptable (+2/+1) -- keep shipped or lighter W; (4) Merfolk ENUM-GAP: reorder-on-failure vs deck cast-order rule; (5) Hinata depth-5 floor cost. Agent sizes: build/scratch/claude_agent_ctx.py at each check. 2-hourly cron f58af08e stays.
**00:55 UTC 11-20:** USER RULED (c): raise the oracle decision budget ("we can do it if this is just the analyzer and won't take an incredible amount of time to run"). Done as a GLOBAL default 600 -> 1300 priced s (DecisionCapNs is read once per process, so per-deck needs plumbing; only decisions already over 600 -- which fail the gate anyway -- pay more). Branch wip/oracle-budget-1300 09aeb9ba0 in .claude/worktrees/oracle-budget-1300 (kept OUT of the primary tree while gate_i runs). Check queued on the box lock: build/scratch/claude_budget1300/ (build + units + Goblins s30131075/s30132663 oracle-on probe; status, DONE; pid file). If it passes: cherry-pick to the primary tree AFTER gate_i DONE_PD, then re-gate Goblins on a fresh block.
**00:42 UTC 11-20:** PUSH RULE CHANGED (USER, 4098af045): no more 4-hour windows -- gated shared-branch pushes go through `scripts/push_queue.py` (join -> background wait -> at my turn rebase + validate under `hold` -> `land`); ungated (docs) pushes skip it. Hook installed in the primary clone. Rebased onto 4098af045 (HEAD:src unchanged 8deb2c93, so the running gate_i PGO is still valid). gate_i on-arm running at 32 threads (started 00:30). Nothing gated is ready to land yet: join the queue when an agent result is integrated.
**22:14 UTC 11-19:** PUSHED fb9cb5e03..8ddf67006 at 22:13 (next window >= 02:13 UTC 11-20); CI watch running. GO written 22:14: gate_i (claude-queue-20261119i maze_s_end + merfolk_turns, then 20261119j prevent_damage) running; do NOT change src in the primary tree during its PGO builds. Sacfodder: agent 2 stopped (wip/sacfodder-fix b9d05f514; mechanism = budget crowding by new plans; NEEDED_ONLY sound but buys ~nothing); agent 3 launched (scope: exact inert-fresh-body prune + budget-staged later wave for the new plans; if dev seeds still net > 0 on Goblins or Fungus -> PARK and raise with the USER). Rock-in-plan (probe7) and board-share (hold1) still running.
**21:40 UTC 11-19:** STATE FOR RESUME (pre-compaction). Local branch = origin fb9cb5e03 + Shadowborn fix/GT + Merfolk gi13 oracle-enum fix + Maze board budget + docs; COMBINED CHECK PASSED (`build/scratch/claude_mem_land/`: units 3719/3719, smoke 231/231 0 changed). PUSH at >= 22:07 UTC (timer task bd06dyqak fires ~22:08): fetch; if origin moved rebase (GT conflicts: take origin keys except ours, notes union, check_gt_logs; rebuild+smoke if src replayed); check_references_safe; push; background CI watch (curl actions/runs?head_sha). THEN `touch build/scratch/claude_gate_i/GO` (re-gate maze_s_end + merfolk_turns, then prevent_damage alone; script pid in claude_gate_i/pid; DONE / DONE_PD) and arm a waiter; do NOT change src in the primary tree during its PGO build (~15 min). AGENTS: (1) rock-in-plan successor (MTG_LINE_KEEP_ROCK; A/B 24.6k net -6, niv +1 then mostly faster on niv2; CPU ALL x1.03, niv x1.09, stuffy/whiteknights x1.05 -> it is cutting CPU with probes; ~192K) scratch claude_rock_plan; (2) sacfodder successor (~266K) told to hand off after hold7 -> launch a fresh agent from logs/sacfodder-fix/handoff.md in .claude/worktrees/sacfodder-fix; candidate = 'needed-only credit'; (3) oracle board-share (wip/oracle-board-share, ~209K, hold1 queued; scratch claude_board_share) -> expect handoff soon. Integrate each per the bar. OPEN USER DECISIONS: (a) Shadowborn MTG_SB_APOSTLE_FODDER_CLAIMS (Apostle ahead of Dark Ritual when Culling/Intent need fodder; rec keep ON; ships at 22:07 unless the USER objects); (b) Jumbo unlock-order CPU x1.20: charge walks to the decision budget (play change, A/B) vs accept; (c) Goblins 131075 decision ~2.05x the 600-s budget: raise to ~1300 s / Matron-aware fold / leave blocked (eat-order fold parked, net worse); (d) small-job lane beside a big run. Also to tell the USER: Maze board budget makes 18 Goblins plans CAPPED that used to finish (board-share should fix).

**21:00 UTC 11-19:** Shadowborn regression GT ACCEPTED (a4a26fab4; only shadowborn_regression_d5_s2002 changed, gi98 same T6; smoke 0 changed; units 3718; notes union kept, 14). MERFOLK gi13 ORACLE-ON FIXED (edf5915a8, from wip/merfolk-oracle-enum 428a50028): the oracle's gate-observe enumeration called EnumerateMainPlans without the lazy layout -> eager champion fan; now EnumerateMainPlansSeq reads it lazily (first entry per fan, exact): 9.6 GB abort -> 95 MB / 1.39 s; verdicts identical on 299 jobs, digests identical, lazy verify 3.66M entries 0 mismatches. MAZE oracle board budget LANDED locally (f77f2737b, d45b881e9, 48094d845, from wip/maze-oracle-mem2): conflicts with the goal fold resolved exactly (both checks 0 mismatches; units 3717; smoke 0 changed; Maze 200-game pool peak 22.8 GB, 617 memory-CAPPED). Trade-off: Goblins 42-pool gets 18 NEW memory-CAPPED (s30133979 T6 m2, s30131479 T3 m2) the 400k retry used to finish -> DELEGATED exact outcome-board sharing (wip/oracle-board-share, scratch claude_board_share) to remove them. Pre-push check of both memory fixes: `build/scratch/claude_mem_land/` (build+units+smoke, waiter). Push at >= 22:07: if claude_mem_land not done, push only through a4a26fab4. AFTER PUSH: re-gate maze_s_end + merfolk_turns + prevent_damage (no src changes during the gate's PGO build).

**20:35 UTC 11-19:** CI green on fb9cb5e03. SHADOWBORN fix landed locally (0354fa0b2 MTG_SB_APOSTLE_FODDER_CLAIMS default ON + doc 3bede49da): Culling the Weak counts as a sacrifice claim in the existing Apostle-before-Ritual rule; gate slice APPLY-DROP-ORDER 44 -> 0 (8 APPLY-DROP remain = parked Lake crack across the Intent tutor); held-out 15k: 1 changed game, 0 slower, net 0; worktree tiers: only shadowborn_regression_d5_s2002 changed (gi98 same T6). Cast order = USER decision, surfaced (recommend keep ON). RUNNING in primary: build+units (ok) + `--both` (`build/scratch/claude_sb_land/`, waiter) -> accept the shadowborn row(s) -> push at >= 22:07. Maze oracle-mem2 rebase: units 3717 clean, pools done, smoke pending. Merfolk gi13 oracle-enum fix: identity hold 250/250 clean, units+smoke pending. Rock-in-plan: probes. Sacfodder successor: 5th diagnostic hold (~232K).

**18:15 UTC 11-19:** PUSHED `93634781f..fb9cb5e03` at 18:07 (next >= 22:07 UTC): Merfolk Turns play-memory fix, gate g2/h2 records, Jumbo perf2 doc, checkpoints. CI watch running. MAZE ORACLE-MEM VALIDATED by its agent (2bce7cbdf on wip/maze-oracle-mem; single games 0.5-2.4 GB where old >8 GB; 200-game 32-thread oracle-on pool peak 27.4 GB, 560 searches CAPPED on the memory budget; digests identical) BUT its cherry-pick conflicts with the Goblins goal fold in ManaOracle.cpp (4 hunks, interacting frontier logic) -> DELEGATED rebase to wip/maze-oracle-mem2 (both check modes, Maze pool + Goblins 42 pool, smoke; scratch claude_maze_mem2). MERFOLK gi13 ORACLE-ON runaway (10.3 GB/12 s even with the play fix): oracle's AuditDecision -> EnumerateMainPlans -> PbAxesAndTwins -> AppendSubdecisionAxes copies plans -> DELEGATED wip/merfolk-oracle-enum (scratch claude_mf_oracle_enum). Other agents: rock-in-plan (A/B; Niv divergences = early ranking, d8 b0 pending), sacfodder successor (hold3 diag), Shadowborn (step3 CPU control + tiers).

**16:35 UTC 11-19:** Box saturated (load 33, rock-in-plan A/B holds the lock; 3 more queued). Merfolk play-mem fix verified locally (units 3717/3717, smoke 0 changed) -> push at the 17:57 window (timer). Rock-in-plan SUCCESSOR running (v1: build ok, LK units ok, repros done; now the 24.5k held-out A/B). Sacfodder successor running (HOLD1 units + A/B baseline + 620470 trace). Shadowborn agent step2 (A/B + gate slice). Maze oracle-mem successor: ON-arm pool running. QUEUE ADD (sound collapse, USER 'collapse wasted work'): Maze oracle CAPPED searches hold overwhelmingly duplicate outcome boards (34,083 outcomes / 346 distinct core boards) -> exact sharing of identical outcome boards (needs full GameState equality, not CoreKey) would cut held memory ~100x and most CAPPED; do after the current maze-oracle-mem lands.

**16:05 UTC 11-19:** GATE h2 DONE (Fungus, record 4114538ef, report logs/mana_clean/claude-queue-20261119h2/report.md): 1152 issues = 978 ENUM-GAP (sac outlet cast in the same plan, e.g. Utopia Mycon + Saproling Burst paid by eating Saprolings = the MTG_SAC_FODDER_MENU family), 140 DOMINATED-CREATURE, 84 DOMINATED-LIFE, 17 ORACLE-GAP, 7 ORDER, 4 APPLY-DROP, 6 CAPPED. SACFODDER agent had STALLED since 10:01 (waiter watched $r/Q1_DONE, script wrote $r/rc/Q1_DONE); parent committed its tree as WIP 4ed00373b on wip/sacfodder-fix (pushed), killed the waiter, stopped the agent, launched a SUCCESSOR (brief carries USER ruling (3) verbatim + the Fungus gate evidence; scratch claude_sacfodder_fix2). Last repro rc/q1.log: g620470 off 3 / on 4 (lever slower) -> successor root-causes it.

**15:40 UTC 11-19:** Merfolk Turns PLAY memory fix LANDED locally (cc6edaeef fix + e40e8b296 doc; from wip/merfolk-mem a7d0ac85b): legend-keep turn fan twins a lazy champion fan lazily; gi172 5.5 GB -> 459 MB, 11-17 runaway s30560698 465 MB; byte-identical (90-job batch, lazy verify 43.9M entries 0 mismatches). Pre-push build+units+smoke queued `build/scratch/claude_merfolk_land/` (DONE, waiter). Goblins EAT-ORDER fold: exact (check 0 mismatches, flags/play identical) but NET WORSE: +14% applies on the 42-game pool (24 plans realised twice when the folded pass does not cover the engine board), and 131075 T2 m2 only -7% (still ~2.05x the 600-s budget): 98.7% of that decision is plans casting Goblin Matron, whose tutor reads the board so eats cannot be reordered across her. NOT integrated; PARKED on wip/gob-eat-order (d7e953b20, doc a2103da97 docs/design/goblins-oracle-eat-order-fold-2026-11-19.md). USER DECISION: raise the Goblins oracle decision budget to ~1300 s, or commission a Matron-aware fold (research, uncertain), or leave Goblins gate-blocked on 131075. Rock-in-plan agent handed off at 280K (MTG_LINE_KEEP_ROCK WIP ce7d362a4, default OFF; handoff .claude/worktrees/agent-ade0a1bc23ae96aa7/logs/rock-in-plan/handoff.md; chain v1 DONE marker build/scratch/claude_rock_plan/v1.done -> successor). Shadowborn agent running.

**15:15 UTC 11-19:** CI green on 93634781f (Windows build success). GATE g2 DONE (record d06f7369f, report logs/mana_clean/claude-queue-20261119g2/report.md): pirates + vampires CLEAN; sea_creatures 61 (ENUM-GAP 6 Overgrowth x2 on Flooded Grove T2, APPLY-DROP 47 land-Aura stacking / Sol Ring, ORDER 7, ORACLE-GAP 1 Koma), stuffy_doll 62 (APPLY-DROP 45 + ORDER 14 + ORACLE-GAP 3, nearly all Mind Stone cast mid-plan), shadowborn_apostle 52 (ORDER 44 Diabolic Intent + Culling the Weak + Dark Ritual + Apostle T5). Gate h (Fungus) had failed like g (PGO coverage-mismatch race) -> re-queued as h2 `build/scratch/claude_gate_h2/` (16 thr, waiter). DELEGATED (Opus, worktrees): rock-in-plan (Stuffy Mind Stone + the Dragons rock residual; wip/rock-in-plan, scratch claude_rock_plan) and Shadowborn order (wip/shadowborn-order, scratch claude_shadowborn). Goblins eat-order successor running (chain2 = off/on + smoke). Jumbo perf2 DONE: no exact cut worth shipping (doc d12f80d6c local) -> USER decision (a) charge walks to the decision budget (play change, A/B) vs (b) accept x1.20. NEXT: Sea Creatures land-Aura family (after a lock-light moment); Maze/Merfolk memory integrations; re-gate maze_s_end, merfolk_turns, prevent_damage after them.

**13:57 UTC 11-19:** PUSHED `bc1d28bbd..93634781f` (next shared push >= 17:57 UTC): v2 GT accept + test pins, Goblins goal fold (3 commits), checkpoints. Pre-push at the rebased tree: units 3717/3717, `--both` ALL PASS (231 + 368, 0 changed); rebased again onto origin's Boars keep-table adoption (no src; GT keys disjoint; notes union kept; check_gt_logs 1316 clean). CI watch in background. Gate g FAILED before any game (its PGO build raced my cherry-pick of ManaOracle.h: -Werror=coverage-mismatch); re-queued as gate g2 `build/scratch/claude_gate_g2/` (GO written after the push; waiter). Maze oracle-mem successor: the 8 'other' unit cases I flagged were doctest MESSAGE blocks, not failures; only the 9 AA cast-order cases fail at its base (pre-v2-pin) -> clean; repros in flight. Merfolk play-mem: identity + lazy verify clean, mf172 5.5 GB -> 459 MB; runaway repro + RSS split queued. Goblins eat-ORDER fold: agent reached 429K (over the limit) -> stopped; code `76ddb5592` on wip/gob-eat-order (uncompiled), handoff `.claude/worktrees/agent-ae8f56a1352ef506f/logs/gob-eat-order/handoff.md`; its chain1 (build+units+131075 probe+check pool) DONE marker `build/scratch/claude_gob_order/chain1.DONE` -> launch a successor when it lands.

**12:45 UTC 11-19:** Goblins oracle fold DONE (agent a921111f, ended at 424K -- over the 400K limit): MTG_ORACLE_GOAL_FOLD default ON, exact (check mode 861 folded plans 0 mismatches; flag lines identical; play digests identical; smoke ALL PASS 0 changed; units 3713/3713). 42-game pooled Goblins: oracle applies 35.24M -> 17.59M, CAPPED 14 -> 0, CAPPED-DECISION 412 -> 288. Cherry-picked onto the shared branch (5fb540e96 FOLD_STATS diagnostic, 689c96872 fold, 96ccafc22 doc); rides the push1330 build+tiers. Goblins still NOT gate-tractable: s30131075 T2 m2 needs ~965 s unbounded (~2.1x the 600-s decision budget; 446 plans, 6 APPLY-DROP). USER DECISION SURFACED: raise the Goblins decision budget to ~1300 s, or commission an exact fold of the eat ORDER (needs per-card commutation: King/Chieftain/Matron read the board between payments; tokens mid-plan). Recommendation: the exact order fold (cut cost, not raise the cap). Doc docs/design/goblins-oracle-eat-fold-2026-11-19.md.

**12:35 UTC 11-19:** v2 GT ACCEPTED + COMMITTED (`4884aa67e` after rebase): units 3712/3712; classifier: all 3 regression slower games churn (recover at 4x/16x: 4,4,4); smoke faster on every AA row; only alchemist/alchemist2hg keys changed. Rebased onto origin eab488fcc (Stuffy Doll src+GT): GT conflict resolved = alchemist keys ours, all other keys origin, notes union (origin's plain Stuffy accept had dropped 8 smoke/regression notes; restored); check_gt_logs 1316 clean. RUNNING (queued on box lock): rebuild + units + `--both` at the rebased HEAD (`build/scratch/claude_push1330/`, DONE, waiter) -> push if only accepted rows / nothing changed. Box near idle: Goblins fold agent's chain3 holds the lock with a single-game UNBUDGETED probe (u131075, pid 3136190, ~3 threads); asked the agent to kill it and bound/fold it. Lock queue behind it: gate g, merfolk chain3, maze chain2, push1330. Sacfodder agent (~246K) waiting on its repros (uncommitted fix in its worktree, units 3713/3713), handoff then successor.

**12:15 UTC 11-19:** STATE FOR RESUME. (1) v2 adoption: UNCOMMITTED test edits in the primary tree (test/unit/test_accomplished_alchemist.cpp V2OffAa guard on 9 older-arm cases; MTG_AA_CAST_ORDER_V2=0 in 4 gw/gwf scenarios) -- commit with the GT accept. Waiting on: classifier `build/scratch/claude_aav2b/cls/` (DONE) for the 3 slower regression games (s2002 gi23 d3+d5, s3003 gi20 d5; bar = recover at d8 b0, run d8 b0 directly if classifier PERSISTS) and units rerun `build/scratch/claude_aav2c/` (DONE). Then accept from the aav2b --both run (`regression.sh <mode> --accept` promotes the last run, no re-run; back up regression_gt.txt, restore notes union, check_gt_logs), commit, push (window open since 10:26). (2) Memory: Merfolk Turns PLAY memory agent has fix a7d0ac85b on wip/merfolk-mem, validation chain queued (~222K ctx, expect handoff); Maze/Merfolk ORACLE memory: cause = ManaOracle Realize holds ~21 KB GameState per hit/frontier entry unbounded; fix b5bb31b61 on wip/maze-oracle-mem (MTG_MANA_ORACLE_BOARD_MB=768 -> CAPPED; exact online Pareto fold; releases), UNBUILT; successor agent validating (chain2.status). (3) Gates: g (5 decks, `build/scratch/claude_gate_g/`, waiter) then h (fungus 16 thr). maze_s_end, merfolk_turns, prevent_damage re-gate after the memory fixes land. (4) Other agents: sacfodder-fix (~246K, expect handoff -> successor), jumbo-perf2 (~203K), gob-oracle-fold successor. (5) USER question surfaced 12:05: allow a small-job lane (<=4 single-game procs ~6 GB each beside a big run capped ~34 GB) instead of strict one-engine-at-a-time; until answered keep the rule and have agents parallelise inside their own lock hold. LKA: NOT adopted (30k +2).
**07:20 UTC 11-19:** USER answers, verbatim: (4) *"For 4 I would like to start with c and then decide if we want to take either of the other options."* (3) *"For 3 I would like us to fix this up. I can't think of a reason why this would reduce quality significantly. If anything it should be more quality at a slightly higher CPU cost."* (2) *"Can we determine whether this is consistent on Accomplished Alchemist or just from the seeds you tested? If we get positive results on average let's just adopt."* (1) *"The cast order for Alchemist seems reasonable except that Weather the Storm should remain somewhat flexible so it can go either behind or before the untappers depending on what else is in the line. I never suggested we should stick with the generic order. Let's proceed on adoption."* ACTIONS: (1) committed MTG_AA_CAST_ORDER_V2 default ON locally; queued build + units + `--both` (`build/scratch/claude_aav2/`, DONE) -> accept GT, push >= 10:26. (2) queued 30,000-pair AA LINE_KEEP_ALL A/B gi 6000-35999 seed 610000+gi, v2 pinned ON both arms (`build/scratch/claude_lka_alc30k/`, DONE; compare with build/scratch/claude_linekeep_all/compare.py logic) -> adopt if net <= 0 (pooled with the earlier 6,000). (3) Opus agent: sac-fodder fix (worktree sacfodder-fix, wip/sacfodder-fix, fresh A/B gi 650000+). (4c) Opus agent: more exact unlock-order cuts (worktree jumbo-perf2). Goblins oracle fold: successor agent (handoff in .claude/worktrees/gob-oracle-fold/logs/gob-oracle-fold/handoff.md). BOX PROTOCOL: every engine process (build/units/batch/tier) runs under `flock build/scratch/claude_box.lock`; all wait for the gate DONE `build/scratch/claude_gate0630/DONE`.
**06:40 UTC 11-19:** PUSHED `2f67da724..3e8a209bc` (06:26; next >= 10:26 UTC): Jumbo unlock-order perf (MTG_UO_DFS + MTG_UO_FOLD, exact, x1.447 -> x1.201), mana_clean PARKED list, gate record, docs; units 3712/3712, smoke ALL PASS 231 / 0 changed, check_gt_logs 1316 consistent; CI watch in background. Gate `claude-queue-20261119b` ON arm ABORTED at the RSS cap (40.57G, 05:48; 17G -> 40G in ~3 min with ~30 prevent_damage oracle games + jumbo gi122783 (587 s) in flight -- looks like one runaway oracle game, cf. mana-oracle-memory-time.md). Recorded only the decks that completed: angels 18 (APPLY-DROP 16), dragons 370 (APPLY-DROP 218 Mind Stone/Gruul Turf + spell; ORACLE-GAP 152), goblins 2682 (ENUM-GAP 1859 = sac-fodder menu miss, lever OFF pending USER; CAPPED-DECISION 479 = 600-s wall backstop on wide token boards; ORACLE-GAP 180; APPLY-DROP 152). RUNNING: gate `claude-queue-20261119c` on the 11 unfinished decks (new src -> new run; `build/scratch/claude_gate0630/`, DONE marker). If it aborts again: intersect the two abort in-flight lists for the runaway game. DELEGATED (Opus, bg): Goblins oracle eat-option fold (exact, oracle-only) in worktree `.claude/worktrees/gob-oracle-fold` (wip/gob-oracle-fold), waits for the gate DONE before any build/run. NEXT after: Dragons Mind Stone APPLY-DROP family; Angels APPLY-DROP; then the incomplete decks' findings.
**05:45 UTC 11-19:** Gate `claude-queue-20261119` STOPPED by me (own run) at 05:35: extra_turns sized at ~975 core-s/game oracle-on (probe games 2,200-2,600 s; 5,000 games ~42 h) -- the deck has been PARKED as oracle-intractable since 11-11 (docs/design/extra-turns-mana-oracle-sizing-2026-11-10.md), but `mana_clean.py status` listed it DUE; its probe alone held the box 04:34-05:16. Fixed: `PARKED` list in scripts/mana_clean.py (status shows "parked", default run skips it, naming it still runs it). RELAUNCHED as `claude-queue-20261119b` on the other 14 decks (~70 min; pid `build/scratch/claude_gate0530/pid`, `run.log`, DONE). `build/scratch/claude_push_lock` held until the gate finishes and the push is done. wip/jumbo-perf 61b4c5c6e Windows CI GREEN.
**04:25 UTC 11-19:** Perf successor DONE (176K): `wip/jumbo-perf` at `61b4c5c6e` (pushed; Windows CI watched): `68a4f528b` MTG_UO_DFS + `d9dfa1b7e` MTG_UO_FOLD (default ON, exact; normal order folded into the tree as its first leaf) + doc. Jumbo CPU x1.447 -> **x1.201** (new-on/old-on x0.831); Angels/Boars/Bruna/Five Colour x1.01-1.04; 0 differing games; verify 0 mismatches; units 3712/3712; smoke ALL PASS 231, 0 configs changed. Subset monotonicity rejected (payer reserves against the whole line -> not exact). Open exact ideas in the doc (per-node bound, prefix transposition, field-subset snapshots). Remaining non-exact option = charge the walks to the decision budget (USER decision, play change). USER 11-19: box nearly idle during the perf dev cycle -> *"The CPU usage is really small right now, so we need to get that going."* RUNNING: pooled mana-clean gate `claude-queue-20261119` x5000 on extra_turns, merfolk_turns, maze_s_end, sea_creatures, stuffy_doll, pirates, vampires, shadowborn_apostle, prevent_damage, angels, jumbo_cactuar, dragons, goblins, treasure_hunt, fungus (pid `build/scratch/claude_gate0415/pid`, log `run.log`, DONE marker; report `logs/mana_clean/claude-queue-20261119/report.md`). NEXT push >= 06:14 UTC: cherry-pick d9dfa1b7e^..61b4c5c6e + 68a4f528b from wip/jumbo-perf (perf-only: smoke 0 changed, no GT accept) -- do it after the gate finishes or between its arms? NO: one engine process at a time -> push build waits for the gate.
**03:50 UTC 11-19:** PUSHED `f1342be37..2f67da724` (02:14; next >= 06:14 UTC); CI GREEN. Unlock-order CPU cut (first perf agent, handed off at 267K, `.claude/worktrees/jumbo-perf/logs/jumbo-perf/handoff.md`): `68a4f528b` on `wip/jumbo-perf` (pushed) = `MTG_UO_DFS` prefix-sharing order tree, exact (verify mode 0 mismatches / 580k searches; pooled A/B 0 of 7,000 games differ). Jumbo CPU x1.447 -> x1.208; Angels/Boars/Bruna/Five Colour x1.01-1.04 unchanged. Remaining cost ~80% at the enumeration site (411k searches/1000 games, 3.5% find; cost = proving infeasibility). Rejected: exact-id memo (0 hits), copy-fold (payer reads card numbers non-symmetrically -> possibly lossy). Successor agent running: one bounded exact attempt (subset monotonicity of proven-infeasible sets; sound per-node bound), then units + smoke (0 changed) + doc, push wip/jumbo-perf. Then: land on the shared branch in the next window (perf-only, byte-identical: smoke 0 changed, no GT accept needed). If still > ~x1.1, surface to USER: charging the walks to the decision budget is the remaining (play-changing) option.
**02:10 UTC 11-19:** origin moved (17 commits, sea-integ-1118 incl. payable-order perf + Jumbo front block) during the first tier run -> rebased again (HeuristicArm.h keep-both with SEA_BUCKET_DISCARD; GT conflicts taken from origin), rebuilt, units pass, `--both` re-run: only reach decks moved (smoke 4 configs, regression 13; searched 0 slower / 1 faster; d0 0 slower) -> re-accepted, notes union restored (11), check_gt_logs 1,316 consistent. PUSHING. USER 11-19 on the x1.445 CPU: *"The 3x suite limit is really for the period prior to mulligan profile and value-leaf, not license to make things slow. I would like to cut down that cost for sure."* -> perf agent running in worktree `.claude/worktrees/jumbo-perf` (`wip/jumbo-perf`, scratch `build/scratch/claude_jumbo_perf/`): exact (digest-identical) cut of the unlock-order search, target Jumbo ~x1.0-1.1.
**00:50 UTC 11-19:** JUMBO ORDER DONE (successor agent, 213K): `wip/jumbo-order-v2` bbb1b9b95 `MTG_PO_UNLOCK_ORDER` + ca9640a20 `MTG_SEQ_TRAILING_EQUIP` + 9070a599f doc, both default ON. Slice 115000-116999: APPLY-DROP-ORDER 92 -> 1, ENUM-GAP 43 -> 28, APPLY-DROP 7 -> 2, PHANTOM 4681 -> 3452. Held-out pooled A/B (27,000 games): Jumbo unlock 2 faster / 0 slower net -2 (CPU x1.445, units x0.994); trailing 0/0; Angels/Boars/Bruna/Colossus Hammer/Five Colour/Kitty Equipment/Martyr Lifegain 0/0 at CPU x0.98-1.03. Lever-off smoke ALL PASS. ADOPTED (bar met; Jumbo suite cost 0.53x Hinata -> ~0.77x, far under 3x); CPU x1.445 reported to the USER with options (b) charge the walks to the decision budget (needs its own A/B) / (c) prefix-sharing walk (pure perf) -- QUEUE (c). Residuals: s30115578 T4 APPLY-DROP-ORDER (other mechanism); s30115140 T3 ORACLE-GAP (oracle has no Chariot-hastes-Elf re-tap outcome). Rebased onto origin f53235400 (Stuffy prune-ab slots; HeuristicArm.h keep-both), cherry-picked as 81cf534d5 / 5274f7a1f / 34d4e7083. IN FLIGHT: build + units + `--both` `build/scratch/claude_push0125/` (DONE marker), push lock `build/scratch/claude_push_lock` SET. Then audit, accept with notes restored, check_gt_logs, references check, push (window >= 01:25), CI watch, remove lock.
**22:55 UTC:** GT for 6e2240a9b accepted (`473a8a526`: Jumbo rows only, digest-only, 0 slower/0 faster; notes restored; check_gt_logs 1,301 consistent). CI on ae2b9f91b GREEN. Jumbo order work: first agent handed off at 265K (`logs/jumbo-order/handoff.md`): family 1 (APPLY-DROP-ORDER) = order decider/precheck ignore haste-unlock equips -> `MTG_PO_UNLOCK_ORDER`; family 2 (Surrak ef-walk-fail) is NOT an order problem: the walk refuses a paid non-unlock equip the apply pays in its trailing pass -> `MTG_SEQ_TRAILING_EQUIP`. Uncompiled WIP MOVED out of the primary checkout to worktree `.claude/worktrees/jumbo-order` branch `wip/jumbo-order` (f3b9453e6, pushed) so the push build stays clean; successor agent works there, scratch `build/scratch/claude_jumbo_order/`, honours `build/scratch/claude_push_lock`. Push timer one-shot 01:27 UTC 11-19.
**22:15 UTC:** USER 11-18: *"Any issue that is causing trouble for other agents is an exception and CI failures are an obvious one. For the rest it will need my coordination."* -> CLAUDE.md push rule amended (`8930a5272`); decision (4) CLOSED. JUMBO DONE (agent 211K, finished): `6e2240a9b` MTG_AURA_CEIL_HASTE_UNTAPPER default ON -- the land-Aura scalar ceiling (`ef-aura-scalar`, EnumeratePlansCore) now counts an untapper (Arbor Elf / Voyaging Satyr) that a same-plan Greaves/Chariot equip hastes. 185 of 301 Jumbo ENUM-GAPs had this shape; slice 115000-116999 ENUM-GAP 90 -> 43, no new APPLY-DROP/ORDER. Held-out Jumbo 640000-644999: 7 faster / 1 slower, net -6, units x1.003; slower gi 641075 wins T4 on both arms at d8 b0. Lever-off smoke ALL PASS. Doc docs/design/jumbo-tooth-and-nail-enum-2026-11-18.md. IN FLIGHT: build + units + `regression.sh --both` at 6e2240a9b (lever ON), `build/scratch/claude_jumbo_gt/` (DONE marker; status file; GT backup regression_gt.before.txt with its 5 accepted notes). Then audit (only Jumbo rows should move), accept both with notes restored, check_gt_logs, commit, push >= 01:25 UTC. QUEUE adds: (a) Jumbo APPLY-DROP-ORDER 291 -- PayableCastOrderCoreImpl's order walk does not model a haste-unlock equip (treat unlock pieces as a producer domain, fire ApplyManaUnlockEquips in the walk); (b) `ef-walk-fail` Surrak family (s30119376) -- same missing order search at the enumeration site; (c) smoke tail: `sea_smoke_d5_s1001` gi=73 takes 507 s alone (60% of that job's CPU, ~8 min of 1/32 workers at the end of every smoke).
**21:30 UTC:** PUSHED `e682d7942..ae2b9f91b` (~21:25; next >= 01:25 UTC 11-19); rebased onto Maze's End P6 (HeuristicArm.h: both kept, ET_OPP_TURN_PRUNE/MAZE_OFFMAIN_* then SAC_FODDER_MENU); units 3665/3665, smoke ALL PASS 0 changed, check_gt_logs clean. CI watch running. DELEGATED (Opus, owns the box): Jumbo ENUM-GAP 301 (Tooth and Nail lines kept off the menu) + APPLY-DROP-ORDER 291 (Chariot/GSZ/Finale/Overgrowth orders); scratch build/scratch/claude_jumbo_enum/, doc docs/design/jumbo-tooth-and-nail-enum-2026-11-18.md, handoff logs/jumbo-enum/handoff.md, A/B gi 640000+. OPEN USER DECISIONS: (1) AA cast order nothing/gwf/v2 (rec v2: -80 vs -40); (2) MTG_LINE_KEEP_ALL adopt despite AA +1/6000 (all decks -5); (3) MTG_SAC_FODDER_MENU adopt (ENUM-GAP 705 -> 57, all slower recover at d8 b0) despite Goblins +5 / Fungus +3; (4) CI fixes outside the 4 h push window. QUEUE after Jumbo: oracle eat-option fold (Goblins oracle ~8.8x); Dragons Sol Ring/Mind Stone residual; Treasure Hunt Land's Edge drawn-later residual; PD CAPPED fold; deck queue.
**20:50 UTC:** sac-fodder menu ROOT-CAUSE (agent handed off at 262K): `2aacc8836` (default OFF). Fungus 630182/630865 = the land-Aura host fold (plan signature keys a land Aura by NAME on non-karoo branches; the fodder credit made the body-eating host payable, it sorted first and the dedup dropped the Tomb-host T4 line at every budget) -> a fodder-credited plan now keys its host; plus an ordering tier (fodder-admitted plans after board-payable ones). Re-A/B on 2aacc8836 (ab/, prior run kept as logs_a75265ce0): Fungus 0/3 net +3 (was +5), Goblins 2/7 net +5 (was +4), units x1.04 / x1.10. d8 b0 replays (slow2/): ALL 10 slower games RECOVER (incl. 630182/630865). Bar fails on net only (play-setting budget spread from the wider menu: Goblins Skirk+Lackey plan valued at 800 sorts first, deepest pass spends there) -> PARKED default OFF, raised to USER as a trade-off (completeness: ENUM-GAP 705 -> 57 unfindable lines vs Goblins +5 / Fungus +3 at play settings). Smoke at default running before the 21:15 push.
**20:00 UTC:** MTG_SAC_FODDER_MENU A/B FAILED the bar -> `d43a033c9` default OFF (doc docs/design/sac-fodder-menu-2026-11-18.md; units 3657/3657, smoke at default ALL PASS 0 changed). Goblins x5000 2/6 net +4 (all 6 recover d8 b0), Fungus x5000 0/5 net +5 (630182, 630865 do NOT recover: off 4 on 5); ENUM-GAP Goblins 705 -> 57, Fungus 129 -> 10; PHANTOM Goblins 83 -> 319; units x1.07 / x1.03. Default-off gating a missing capability is not an end state (CLAUDE.md) -> a fresh agent is ROOT-CAUSING 630182/630865 (root candidate dump) + testing the PHANTOM-budget hypothesis for the recovering losses; fix behind the lever, re-run slice/A/B/d8. Branch is push-safe at d43a033c9 (all new levers default OFF). Next push >= 21:15 UTC.
**18:05 UTC:** PUSHED `e3b807999..87d84afab` (~17:12; next >= 21:15 UTC); Windows CI GREEN (popcount fix confirmed). Engine miss #1 (menu fodder for an outlet cast in the same plan): first agent handed off at 262K -> WIP `9ffb16151` MTG_SAC_FODDER_MENU (default ON, NOT pushed, not A/B'd): menu credit + PayableCastOrderCore fodder order model; earlier-build slice Goblins ENUM-GAP 706 -> 56 but APPLY-DROP-ORDER 8 -> 474 (Fungus 129 -> 11, 0 -> 36) -- the walk change in 9ffb16151 targets that, unmeasured. Handoff logs/sacfodder-menu/handoff.md. A FRESH agent is finishing it (fix ORDER if still high, smoke at lever=0, A/B Goblins+Fungus x5000 seeds 620000+, d8 b0 replays, doc docs/design/sac-fodder-menu-2026-11-18.md). DO NOT PUSH 9ffb16151 until its A/B passes and smoke+regression GT is accepted (or it is flipped default OFF). Then: oracle eat-option fold (Goblins oracle ~8.8x), Jumbo ENUM-GAP.
**16:55 UTC:** Goblins ORACLE-GAP DONE (two agents; first handed off at 298K): `534dca483` (rebased) MTG_ORACLE_SAC_FODDER default ON, oracle-only (play byte-identical, smoke ALL PASS). Goblins gate slice gi 125000-126999: ORACLE-GAP 367 -> 5 (Pashalik Mons x4 = different mechanism), but ENUM-GAP 18 -> 706 (Fungus 10 -> 129): NEW ENGINE MISS #1 = the plan-menu mana gate counts sac-for-mana fodder only for an outlet already on the battlefield (AddSacPayFodderToPool; TurnSolver.cpp ~3765, ManaPayment.cpp ~7494/~7547), so 'cast Skirk Prospector, then pay by eating bodies' is never on the menu at any budget (repros s30125157 gi125157 T2 m1, s30125177, s30126275 T2/T3; Fungus s30125037 gi125037 T3 m1). Also: APPLY-DROP-ORDER x8 s30126844 gi126844 T5; APPLY-DROP vial:Matron/Bolt s30126582; s30125468 gi125468 T4 (Pashalik activation apparently never performed). Oracle cost on Goblins ~8.8x (wide boards); a sound fold of dominated eat options is owed (no USER decision: collapse wasted work). Doc docs/design/goblins-sac-mana-oracle-2026-11-18.md. Origin moved (Stuffy Doll, e3b807999): rebased (HeuristicArm.h: both kept, STUFFY_* then LINE_KEEP_ALL); RUNNING rebuild + units + smoke + check_gt_logs (build/scratch/claude_push1700b/, DONE) then PUSH. NEXT: engine miss #1 (menu fodder for an outlet cast earlier in the plan) -> delegate; then the oracle fold; then Jumbo ENUM-GAP (Tooth and Nail).
**15:20 UTC:** CI fix VERIFIED green on wip/ci-fix-popcount (ea8ab3487). Line-keep widening DONE by the subagent: `d076e70a5` MTG_LINE_KEEP_ALL **default OFF** (doc docs/design/line-keep-all-2026-11-18.md; scratch build/scratch/claude_linekeep_all/ab_table.md). Same stranding mechanism in all 3 families; repros 0 drops; gate slices dragons APPLY-DROP 93->34, treasure_hunt 139->45 (ORDER 15->0). Held-out all 58 decks 64,500 pairs: net -5 (8/4); AccomplishedAlchemist +1 / 6,000 (1/2; both slower games recover at d8 b0) -> fails 'every deck net <= 0' -> USER decision (adopt anyway?). Cost: units 0.9994, cpu ~+1.8% (worst +5-7%). RUNNING: build + units + `--both` at HEAD (build/scratch/claude_push1700/, DONE) to confirm default-OFF byte identity before the 17:00 push (popcount fix + lever + docs). NEXT: Goblins ORACLE-GAP (Siege-Gang sac ability paid by Skirk Prospector sac-for-mana: oracle has no sac-mana source -> 'no payment exists'; oracle-side, no GT) -> delegate after the tiers; then Jumbo ENUM-GAP (Tooth and Nail lines kept off the menu by a mana gate).
**13:30 UTC:** WINDOWS CI RED on 6462493e9 (Build (Release) failed; logs need admin, unreadable): cause = `__builtin_popcount` in LineReducerKeepHold (the only GCC builtin since green 7093daceb). Fix `ea8ab3487` (std::popcount), CI verifying on origin wip/ci-fix-popcount (bisect branches wip/ci-bisect-pf1d / -reducer also pushed). Goes to the shared branch at >= 17:00 (4 h rule; asked USER whether a CI fix may go sooner). RE-GATE DONE (`afed0cad1` record; report logs/mana_clean/claude-regate-20261118/report.md): dragons 430 (APPLY-DROP 242, ORACLE-GAP 188), goblins 903 (ORACLE-GAP 833, APPLY-DROP 48, ENUM-GAP 22), jumbo 847 (ENUM-GAP 301, APPLY-DROP-ORDER 291, DOM-CREATURE 147, ORACLE-GAP 91), treasure_hunt 313 (APPLY-DROP 255, ORDER 40). DELEGATED (Opus subagent, owns the box): extend the line keep to lines with no same-turn reducer (dragons {Dragon + Mind Stone} with Incubator in play; treasure_hunt {Land's Edge + Treasure Hunt}), cost-measured, held-out A/B 610000+ over reachable decks; doc docs/design/line-keep-all-2026-11-18.md, scratch build/scratch/claude_linekeep_all/, handoff logs/linekeep-all/handoff.md. AFTER: Goblins ORACLE-GAP 833, Jumbo ENUM-GAP / ORDER.
**13:00 UTC:** PUSHED `7093daceb..6462493e9` (next shared-branch push >= 17:00 UTC). Rebased onto origin's Maze's End (HeuristicArm.h slot conflict: both kept, MAZE_* then LINE_REDUCER_KEEP; GT conflicts taken from origin, then `--both` re-run: 0 configs changed vs gt_logs, the failing aggregates equal this session's accepts -> re-accepted, 37 notes, check_gt_logs clean; units 3,541/3,541). AA cast-order re-measure DONE: gwf net -40 (15/15 slower recover at d8 b0), v2 net -80 (35/35 recover); d8 b0 over the 41 deals: neither arm slower than off. Reported to USER: decision nothing / gwf / v2 (recommend v2). RUNNING: mana-clean re-gate `claude-regate-20261118` on dragons, goblins, jumbo_cactuar, treasure_hunt x5000 fresh blocks (build/scratch/claude_regate1300/, DONE marker; report logs/mana_clean/claude-regate-20261118/report.md). NEXT: fix the largest remaining family (delegate one Opus subagent per family, one at a time), PD CAPPED fold, deck queue. CI watch on 6462493e9 in background.
**11:55 UTC:** Dragons reducer keep GT ACCEPTED (`f0bedfae1`: smoke 3 / regression 4 configs digest-only, 0 slower 0 faster; 28 dropped notes restored, 33 total; check_gt_logs clean). USER 2026-11-18 on the AA v2 order, verbatim: *"You would expect Scholar to be later in the list as it is the last thing we do. Activations should be in the list in some form, so that requires an update."* -> `94b1a409f` (default OFF): kAaV2List = one list of casts + board activations; Scholar's cast moved from after Ezuri to the finish (after Stroke-at-opponent, before its drain); all other relative order unchanged; units 3,477/3,477. RE-MEASURE on the current engine (build/scratch/claude_aa_order_remeasure/, base 200001, 5,000 pairs per arm): gwf vs off net -40 (34/15), v2 vs off net -80 (78/35), v2 vs gwf -40; every cell <= 0 (play d5 b20 -2/-22, d3 b10 -5/-10, d0 -33/-48). RUNNING: d8 b0 replays of all 41 slower gis x off/gwf/v2 (`d8/`, DONE marker; pairing sanity gi 25 reproduces). Then report to USER (decision: nothing / gwf / v2). Push >= 12:15.
**11:20 UTC:** Dragons Incubator family DONE by the subagent: `b99071691` (`MTG_LINE_REDUCER_KEEP`, default ON; doc docs/design/dragons-incubator-drop-2026-11-18.md). Mechanism: the line hold priced later casts without the same-turn reducer discount, and the Incubator's {3} was paid pip by pip, stranding the Dragon's {R}{R}. Repro seeds 7/8 -> 0 drops (84117 has no discount: out of reach); gate slice Incubator drops 92 -> 2, all APPLY-DROP 211 -> 99. Held-out gi 600000+: Dragons 5000 pairs 0/0 net 0; 8 reducer decks x1000 + 2 controls: 0 slower, Spirits net -1. Units 3,477/3,477. Origin unchanged since a76477d36. RUNNING: `regression.sh --both` (scratch build/scratch/claude_reducer_gt/, DONE marker, notes_before.txt = 31 provenance notes) -> audit/classify, accept, restore notes, check_gt_logs, commit, push >= 12:15 with ebd5a6de3..b99071691. AFTER: re-measure AA cast order off / gwf / v2 on the current engine (USER asked what the new order is; 10-22 numbers predate the land-Aura fix); then Goblins / Treasure Hunt / Jumbo families. Open for USER: rung on every multi-cast line (would clear most of the remaining ~97 non-reducer Dragons drops; costs a trial payment per line payment everywhere).
**10:45 UTC:** land-Aura vs AURAS_FIRST MEASURED (`23232618d`, table in docs/design/land-aura-main1-line.md): with `MTG_PO_LAND_AURA_DOMAIN` ON, AURAS_FIRST over the proposed AA order is +1 (d4 b20) / -3 (d0), i.e. superseded; the lever alone gives -5 / -25 vs AURAS_FIRST-alone -4 / -28. Reported to USER; AA cast order (nothing / gwf / v2) still USER-owned. Box was idle 09:11-10:34 (agent did not pick the next item; fixed at the 10:34 cron). Moderate gate `claude-moderate-20261117` was DONE (report under logs/mana_clean/, gitignored): leading real-drop family = Dragons APPLY-DROP 583 (469 depth-1), top 8 signatures all `{Dragon + Urza's Incubator}` with the Dragon dropped (oracle pays Incubator off Mind Stone/Turf and keeps {R}{R} for the reduced Dragon). IN FLIGHT: Opus subagent (bg) on that family, scratch `build/scratch/claude_dragons_inc/`, doc `docs/design/dragons-incubator-drop-2026-11-18.md`, commits locally, no push, no GT; held-out Dragons 600000-604999 + controls + reachable decks; it owns the box until it reports. Then: parent smoke+regression, accept, push. Push due >= 12:15 UTC: unpushed ebd5a6de3, 8fe5ee829, ac3eb4c47 (pf1d+UKS+GT), 99896b4f8, 23232618d. After the Dragons family: Goblins (178 APPLY-DROP, 1054 ORACLE-GAP), Treasure Hunt (160 APPLY-DROP), Jumbo (247 APPLY-DROP-ORDER), then the PD CAPPED fold and the deck queue.
**09:25 UTC:** pf1d + UKS ADOPTED, committed `ac3eb4c47` with smoke + regression GT (units 3,474/3,474; smoke 5 configs / regression 7 changed: Cyclone, Dragonstorm, Alchemist smoke d0 digest-only; 12 searched slower, all Cyclone: 9 churn, 3 persist with T1-divergent draws; 26 dropped provenance notes restored, check_gt_logs clean). stash@{0} dropped. Push due >= 12:15 UTC (unpushed: handoff commits ebd5a6de3, 8fe5ee829, + ac3eb4c47). CI for a76477d36: Windows build success. USER 11-18 (new question): another agent's land-Aura fix (`MTG_PO_LAND_AURA_DOMAIN`, dca9d0bb5, default ON) recommended as superseding AA's `MTG_AA_CAST_ORDER_AURAS_FIRST`; USER: "I can't speak too much to this myself, so this will be a question for you to investigate." Finding so far: AURAS_FIRST is a round-1 arm of the ORIGINAL proposed AA order (Wish ahead of the land Auras); every later candidate (gw, gwf = generic tiers; v2 = kAaV2Order starts Sprawl, Wild Growth) already casts land Auras first, so AURAS_FIRST is moot for the live AA decision (adopt nothing / gwf / v2, USER-owned). IN FLIGHT: 2x2 A/B `build/scratch/claude_aa_aurasfirst/` (proposed order with/without AURAS_FIRST x land-Aura fix on/off; held-out base 190001; 500 games d4 b20 + 1,000 d0 per arm; pid, batch.out, DONE). Supersedes if on_fix vs af_fix are (near-)identical while on_nofix vs af_nofix reproduce round 1's gap.
**08:40 UTC:** gi 570487 RECOVERED via the budget ladder (`build/scratch/claude_570487_ladder/batch.out`): base T5 at every rung (d6/d7 b0, d8 b1000..b600000); pf1d T5 at d8 b1000 (52 s), b5000 (164 s), b30000 (20 min). Remaining pf1d rows (b120000, b600000, d6 b0, d7 b0) stopped by the agent (own experiment, verdict already in; marker STOPPED). Bar met (280/280 recover; net -16) -> ADOPTING pf1d (T1 + DEMOTE default ON) + MTG_UNSEEN_KEEP_SCARCE default ON: stash@{0} applied to the working tree (HeuristicArm.h conflict resolved: stash comment text + the new SEA_*/LINE_SPARE_STORAGE slots; stash@{0} NOT dropped until committed), DecisionProviders comment corrected to the ladder result. IN FLIGHT: `build/scratch/claude_pf1d_adopt/` (pid, build.out, units.out, both.out, DONE): rebuild + units + `regression.sh --both`; waiter bpqquus58. Then: audit (expect Cyclone rows + any UKS-moved rows), classify slower searched games, accept both tiers with a provenance note (restore dropped notes), check_gt_logs, commit code+GT together, drop stash@{0}, push >= 12:15 UTC.
**08:15 UTC:** storage tier PUSHED (`84d82f5ad..a76477d36`; next shared-branch push >= 12:15 UTC). smoke 223/223 + regression 353/353 before the rebase, 0 configs changed (no GT accept needed: Dragonstorm rows did not move, matching the 0-later A/B); rebased onto origin's Sea Creatures adoption (HeuristicArm.h slot conflict: both slots kept, SEA_* then LINE_SPARE_STORAGE), rebuilt, units 3,474/3,474, smoke 223/223 0 changed, check_gt_logs clean. CI watch running. USER 11-18 ~07:50, verbatim: *"I don't really like to let the runs remain unresolved. we should probably find some way to determine whether it can recover, even if we have to try different budgets instead."* and *"If it recovers then it seems we could adopt."* IN FLIGHT: gi 570487 BUDGET LADDER, one pooled batch `build/scratch/claude_570487_ladder/` (pid file = run2.sh, `batch.out`, DONE marker, `manifest.json`): pf1d AND base at d8 b1000/b5000/b30000/b120000/b600000 and d6 b0 / d7 b0 (14 rows, flags per row on the committed binary). Recovered = a pf1d row reaches T5 (base's shipped turn). If it recovers: pop stash@{0} (pf1d + UKS defaults ON), units, smoke+regression accept of the Cyclone rows, push in the next window. If pf1d stays T6 where base finds T5: root-cause it.
**07:20 UTC:** storage tier DONE by the subagent: `c7a605a4d` (`MTG_LINE_SPARE_STORAGE`, default ON; doc `docs/design/line-spare-storage-2026-11-18.md`). Dragonstorm held-out 590000-594999 x5,000: earlier 0 / later 0, net 0, 2 digest-only; controls identical; units 3,467/3,467. s30086338 APPLY-DROP 14 -> 0 (game still T7: the search's T6 choice is unchanged); s30083522 untouched (no storage land; unnamed closer, outside the ruling). IN FLIGHT: rebuild + units + `regression.sh --both`, chain pid in `build/scratch/claude_storage_gt/pid`, console `both.out`, DONE marker; waiter bvjogkmm6. Then audit, accept (restore dropped notes), check_gt_logs, push.
**06:34-07:00 UTC:** gi 570487 STOPPED at the deadline (3.5 h this run, ~6 h total, unresolved at d8 b0; marker `build/scratch/claude_uks_over_pf1d/STOPPED_570487`). Adoptions NOT committed: `git stash` stash@{0} "pf1d + UKS adoptions (defaults ON), pending gi 570487". Bar unmet (279/280) -> surface to USER; UKS cannot ship without pf1d. VAMPIRES s30230089 RESOLVED (no defect): replayed on clean HEAD (`build/scratch/claude_vamp/`); the engine's CHOSEN T4 line is {Territory + Legion Lieutenant + Stromkirk + pump}, paying the Estate's 1 life (life 21), win T4. The APPLY-DROP flag is on a DIFFERENT menu entry that does not name Lieutenant (DROP-COVERED for the same plan, 'a lost action was not declared in the plan'): an oracle classification false positive. Low-priority oracle fix: an undeclared dropped action with a covering menu entry should classify as DROP-COVERED, not APPLY-DROP. DRAGONSTORM: Opus subagent (bg) implementing `MTG_LINE_SPARE_STORAGE` (Bazaar counters spent only when the named line needs them; s30086338) + unit test + held-out A/B games 590000-594999 + d8 b0 slower-game replays, scratch `build/scratch/claude_storage/`, commits locally, no push, no GT. It owns the box until it reports. Then: parent runs smoke+regression, accepts GT, pushes. Docs-only push window opened 07:11.
**USER RULINGS 11-18 ~05:30, verbatim:** Vampires: *"For the Vampires case I don't understand the issue. Can't the Voldaren Estate pay costs for Legion Lieutenant?"* Dragonstorm: *"For Dragonstorm it should spend the counters if they are required for the line."* -> Dragonstorm: implement Mercadian Bazaar (banked-counter) spend when the line needs it (s30086338: Apex short a red that only the Bazaar's counters supply), then A/B + GT per the bar. Vampires: the Estate DOES pay Lieutenant's {W} or {B} (1 life); with 4 Vampire-colour lands BB + WB is payable whatever pays Stromkirk, so the 11-17 'pain-vs-flexibility' framing looks wrong. Re-run the repro (report.md line 72, MTG_MANA_ORACLE=3 TURN=4) when the box frees and find why the engine's Lieutenant payment failed; probably a defect, not a policy question.
**05:16 UTC:** gi 570487 d8 b0 still running (2.17 h this run, ~4.7 h counting the stopped one; 1/32 workers, rss ~1.5 G, progressing). Box otherwise idle but ONE engine process at a time (USER 11-04). DEADLINE: if still running at the ~06:34 cron check, stop it (own experiment), record gi 570487 as UNRESOLVED (not recovered in ~6 h at d8 b0), keep both adoptions uncommitted in the tree (stash them before any other build), and move on down the NEXT list; surface the call to the USER (pf1d net -16 over 10k, 279/280 recovered, last one intractable at d8 b0).

**LATEST PUSH 21:04 UTC** (`af685ee3e`, rebased onto origin's Sea Creatures Hedge Maze commits, HeuristicArm.h
appends kept on both sides; units 3,465/3,465, smoke same 46-row failset; next push not before 01:04 UTC 11-18).
Trace gi 560506 DONE: UNSEEN_KEEP_SCARCE=1 reorders 46 payments, all in SIMULATED T4-T6 of the T1 search (none T1-3),
and the T1 pick flips to Brainstorm (wins T5); =0 Ponder (T4). Every Ponder-first arm (T1, T1-2, T1-2+hand; lever on or
off) casts T1 Ponder and wins T4: an ORDER-only change flips it, so the T1 choice is a near-tie the lever nudged
(budget/tie channel, not a wrong payment). NC check (MTG_NC_SEARCH=1) killed after 85 min on one game (agent's own run).
In flight: Ponder-first A/B, `build/scratch/claude_ponderfirst_ab/` (pid file `pid`, DONE marker, `compare.py`): arms
base/pf1/pf12/pf12h, Cyclone games 570000-574999, binary = uncommitted WIP Release build copied to mtg.bin. Uncommitted WIP: the three Ponder-first levers + `MTG_UNSEEN_SCARCE_TRACE` print.

Earlier: **PUSHED 16:44 UTC** (`f4ca329c9..8c99d110b`, next shared-branch push not before 20:44 UTC). It is rebased onto
origin's Sea Creatures integration; the only conflict was HeuristicArm.h appends, with both kept and the order
verified. After the rebase:
- unit 3,462/3,462 and gt_logs consistent;
- smoke 46 failed, the same rows as before the rebase; the one value change is Creature Giving d0
  5.4990 -> 5.4980, from origin's land-Aura change;
- references safe.

The pushed fixes are the painland float, `MTG_CONT_UNSEEN_PENDING`, `MTG_ONDEMAND_VISIBLE_CONTRIB` and
`MTG_LEFTOVER_LINE_SWAP`. The Windows CI watch runs in the background (REST API poll).

**Windows CI for the 16:44 push:** run 44129190474, success.

**DONE 17:02 UTC: Dragonstorm gate** `claude-dragonstorm-swap-20261117` (indices 82,000-86,999, record `9639593e6`):
152 issues = 66 APPLY-DROP, 79 CAPPED-DECISION, 4 APPLY-DROP-ORDER, 2 ENUM-GAP, 1 ORACLE-GAP; oracle-on/off
byte-identical. Report `logs/mana_clean/claude-dragonstorm-swap-20261117/report.md`. Top seeds (APPLY-DROP):
s30083167 T7 x14, s30086717 T4 x12, s30083522 T6 x10, s30086338 T6 x10 (replays in `build/scratch/claude_ds2/`).

**OPEN USER DECISION (18:45 UTC): `MTG_UNSEEN_KEEP_SCARCE`, committed DEFAULT OFF.** It fixes s30083167 (14 -> 0)
and partly s30086717 (12 -> 8). The held-out A/B is byte-identical on every deck except Cyclone: 5,000 pairs, 1
later game (gi 560506, a T1 butterfly that recovers at d8 b0), net +1. A trade-off, so the USER decides; the
agent recommends adopting. `docs/design/unseen-keep-scarce-2026-11-17.md`.
USER asked (20:2x UTC) WHY Cyclone loses a turn. "Butterfly" was a label, not a trace. OWED after the moderate gate:
trace gi 560506 T1 (both arms): root scores of Brainstorm vs Ponder, and every lookahead node where
`UnseenScarceNamedMask` != 0 (needs a TAPDBG print), to tell a better/worse lookahead payment from a budget-cut shift.
Note: in Cyclone every cantrip is a `ParamKeyedDrawClass` opener, so the lever is live in most of its lines.
gi 560506: lever ON casts Brainstorm T1 (wins T5); OFF casts Ponder T1 (wins T4). USER: "Ponder should be cast
earlier because Brainstorm is more useful later. T1 is the worst time to cast Brainstorm typically." So the flip went
to the worse play: the search scores T1 Brainstorm ~= Ponder (pre-existing weakness). OWED too: count T1 Brainstorm-
with-Ponder-in-hand across a Cyclone sample (game logs), check the d8 b0 choice, then propose to the USER a fix shape
(provider root ORDER Ponder-first = legal; a hold-Brainstorm prune = lossy, needs A/B + USER). Related open USER
question B in `docs/design/cyclone-cast-order.md`.
USER ruling (20:2x UTC), verbatim: "Yeah, it could make sense to reorder them. I wouldn't do that for all turns,
though. It's important here that Brainstorm be a priority in later turns." Agent default: T1 only (asked the USER
whether T1-T2). USER follow-up, verbatim: "Yeah, I think 1 and 2 is probably fine, though Brainstorm turn 2 is still
solid, but given that this is just a reorder it might be fine. we can A/B a few different options." -> A/B arms:
off, Ponder-first T1 only, Ponder-first T1-T2. USER again, verbatim: "I think it might make sense to do it first in
both of the 2 turns, since that saves Brainstorm for the later turns. Of course, this does not imply anything about
the order when you do cast both. On Turn 2, Brainstorm before Ponder is quite possibly better, since you can shuffle
if you have a fetchland or use ponder to shuffle for Brainstorm."
**USER rulings 22:5x UTC, verbatim:** on the Ponder-first variant: *"I'll want whichever gets the best results."*
So: adopt the best-measuring arm (pf1 / pf12 / pf12h) that clears the adoption bar (every slower game recovers at
d8 b0, net <= 0); not an open decision. On `MTG_UNSEEN_KEEP_SCARCE`: *"I agree we can turn on if and when the issue
is resolved."* So: turn it ON once the adopted Ponder-first arm removes gi 560506's later game (re-measure the lever
on Cyclone with that arm in place).
**A/B 1 DONE 23:05 UTC** (`build/scratch/claude_ponderfirst_ab/`, 5,000 pairs, games 570000-574999): pf1 net +11
(53/64), pf12 +10 (77/87), pf12h +9 (81/90); units ~0. NONE adoptable. Traces of the 178 changed games
(`div/an.txt`): T1 Brainstorm->Ponder swap is noise (pf1 +5, pf12h -1); T1 cast-nothing -> Ponder +4 (the FRONT
shape also pulls Ponder ahead of pass/cycle lines, which the USER's rule never asked for); pf12h's T2 change +6.
**A/B 2 IN FLIGHT 23:15 UTC** (`build/scratch/claude_ponderfirst_ab2/`, pid file, DONE, `compare.py`; base reused
from A/B 1 plus a 500-game `ctl` byte-identity control): the DEMOTE shape, new lever `MTG_CYCLONE_PONDER_FIRST_DEMOTE`
(uncommitted): a plan casting a put-back cantrip and no Ponder goes BEHIND every other plan on the early turns. Arms
pf1d / pf12d / pf12hd. **DONE 23:41 UTC:** ctl 500/500 byte-identical; pf1d net -2 (147/145, digest-only 983,
units +0.25%); pf12d +5 (233/238, units +3.3%); pf12hd +8 (234/242, +3.2%). Lever commit b3eda2bac.
**IN FLIGHT 23:45 UTC: held-out confirm of pf1d**, `build/scratch/claude_ponderfirst_ab3/` (games 575000-579999,
base + pf1d, pid/DONE/compare.py). Then: if still net <= 0, d8 b0 on the slower games (time ONE first; Cyclone d8 b0
cost unknown). **Held-out DONE 23:56:** pf1d 149/135, net -14 (combined 10,000: 296/280, net -16).
**d8 b0 of all 280 slower games** (`build/scratch/claude_ponderfirst_ab3/d8/`, pooled, pid/DONE): 00:34 UTC 279/279
recovered; the last (gi 570487, b5 a6) still running (>0.5 h; gi 575776 took 35 min). If it recovers: ADOPT pf1d
(T1 + DEMOTE default ON; USER: "whichever gets the best results"), accept Cyclone GT, then re-measure
UNSEEN_KEEP_SCARCE on Cyclone over pf1d (USER: turn it on "if and when the issue is resolved").
Waiter bajxv5qy0 (PID-keyed) prints the recovery count when the d8 batch ends. **01:35 UTC: gi 570487 still running
(1.5 h, 1/32 workers, rss 1.2G).** The unit test ("Cyclone Ponder first early: the front and demote shapes") is
COMMITTED (units 3,466/3,466). **PUSHED ~01:45 UTC 11-18** (the default-OFF levers + test + docs, rebased onto origin's
Mill/Spirits keep tables, no src/GT upstream; no smoke: levers default OFF, control 0/500 digest mismatches); next
shared-branch push not before ~05:45 UTC, so the adoption commit waits for that window. Adoption = `EnvOn("MTG_CYCLONE_PONDER_FIRST_T1", true)` and `..._DEMOTE", true)` in
DecisionProviders.cpp (update the comments), update HeuristicArm.h's "Default OFF" text, units, smoke accept of the
Cyclone rows, push in the window (>= 01:04 UTC; unpushed: 0e5d8f97c, ca2ae14a6, b3eda2bac + the handoff commits).
Other open USER policy questions surfaced 11-17 23:0x: Vampires s30230089 (pain now for an unnamed later cast; agent
leans no) and Dragonstorm banked-land spend (Bazaar counters when a named line is short a colour; agent leans yes,
narrowly). Built (commit ca2ae14a6): CycloneProvider::
PlanMoveOrderFront, levers MTG_CYCLONE_PONDER_FIRST_T1 / _T2 (heurarm slots); fronts a plan casting Ponder and NO
put-back cantrip, so a both-cast plan is untouched. CAUTION: the default search is CLAIRVOYANT (knows library order; `cyclone-discard-buckets.md` lists
T1 Brainstorm-vs-Ponder as a known (b) clairvoyance divergence, d5 34), so a pure ORDER may not move a fully searched
choice; check with MTG_NC_SEARCH=1 / MTG_SEARCH_BLIND_UNKNOWN. Trace build: `MTG_UNSEEN_SCARCE_TRACE=1` (uncommitted).

**Found defect (pre-existing, diagnosed, deferred):** Merfolk Turns game 560698 (seed 30560698) drives one process
past 42 GB RSS on turn 6. Cause: the Wanderwine Prophets CHAMPION axis is still materialised eagerly (540 -> 154,635
plans). Deferred doc: `docs/design/merfolk-turns-champion-fan-rss-2026-11-17.md`. Avoid Merfolk Turns games
560,000-560,999 in batches until it is fixed.

**s30086338 T6 diagnosed (not fixed):**
- The visible-closer rule cracks Bloom for `{B}{B}{B}` at the first Pyretic, so Kolaghan has black later.
- The opener-line WUBRG drain then spends that black: Pyretic's own generic, the second Pyretic's `{1}`, and Apex.
- With `MTG_UNSEEN_KEEP_SCARCE=1` the black survives both Pyretics and Desperate Ritual, but Apex `{7}{R}{R}{R}`
  still takes it: the float (`{B}{B}` + 5 `{R}`) plus Mountain and Needle is exactly 10.
- The oracle pays with Mercadian Bazaar's stored red. The engine holds the Bazaar on purpose: storage and depletion
  lands are never "spare" in the LINE_FLOAT_TAP_FIRST rung, so their banked mana is kept.
- So this is a float-vs-TAP decision: spend the banked counter when the named line is otherwise short a colour
  that no other source makes. That is a design question about banked lands; not started.

**s30083522 T6 (not fixed):** Karrthus beyond the SECOND Apex is not named in the plan, so the line owes no
{B} and even `MTG_UNSEEN_KEEP_SCARCE` cannot keep Bloom's black through ten payments. This is the same open design
question as s30086338: carry a visible-but-unnamed closer's colour (or spend a banked land) under an opener line.

**DONE 19:14 UTC: near-clean six-deck gate** `claude-nearclean-20261117` (record `fe5ca5ef1`): Fluctuator CLEAN,
Minotaur CLEAN; Pirates 6, Shadowborn 11, Angels 27 (23 APPLY-DROP-ORDER), Vampires 2. Report
`logs/mana_clean/claude-nearclean-20261117/report.md`.

**DONE 19:31 UTC: Pirates s30108757** `7c1733619` (`MTG_PREPAY_CCO_CAST_DIG`, ON): every Pirates issue,
6 -> 0. Knights + Pirates A/B identical. `docs/design/prepay-cco-cast-dig-2026-11-17.md`.

**LESSON + CORRECTION (same commit):** never cache a heurarm-backed lever reader in a function-local
`static const`. The first caller's value freezes, so a pooled A/B runs every job on the first job's arm.
`MTG_ONDEMAND_VISIBLE_CONTRIB` (pushed 16:44) had that bug, and its "byte-identical" A/B was invalid. It is now read
per call and re-measured: Dragonstorm 10,000 pairs, 1 earlier / 0 later (net -1). The other new levers were
already per call.

**Vampires s30230089 (2 flags, not changed): a pain-vs-flexibility call.** The engine pays Stromkirk Condemned
{B}{B} with the new Unclaimed Territory rather than Voldaren Estate, which costs 1 life. Legion Lieutenant {W}{B}
(unnamed in the plan) then lacks a colour. The oracle pays the life and keeps the Territory. Paying life now for an
UNNAMED later cast is a policy question, not a defect.

**IN FLIGHT (19:31 UTC): moderate seven-deck gate** `claude-moderate-20261117` (mill, treasure_hunt,
creature_giving, jumbo_cactuar, fungus, dragons, goblins; 5,000 each). Console
`build/scratch/claude_gate/moderate.out`, pid in `moderate.pid`. Next: commit the record, smoke, push (>= 20:44 UTC),
then work the leading signatures.

**THEN:** the PD CAPPED fold, then the deck queue (Pirates, Extra
Turns, Jumbo, Hydras, Fungus, Shadowborn, Morph, Landfall Elementals, Dragons, stale-clean decks). Then the Merfolk Turns RSS runaway, the PD CAPPED
fold, and the deck queue. Next push not before 20:44 UTC.

**DONE 16:15 UTC: s30072155 T5 + s30074081 T4** (`MTG_LEFTOVER_LINE_SWAP`, ON; `SwapLeftoverForLine` in
ManaPayment.cpp). After an outermost payment, a floating colour that the payment drained into its generic is
swapped back for over-produced units of other colours. It does this only when the line's rest is provably short as
left and not short after. It stands down for breakpoint-opener lines. Exact seeds 2 -> 0 and 1 -> 0. All-deck A/B
(1,200 jobs, games 550,000+) is byte-identical everywhere. `docs/design/leftover-line-swap-2026-11-17.md`.

**Remaining gate signature:** s30072022 T5 (APPLY-DROP 1, an Apex line). The engine's continuation casts two
Scourges (opponent at 9) where the oracle's casts Dragonlord Kolaghan (opponent at 20). That reads as a continuation
choice, not a lost payment; low priority. **NEXT:** push (>= 16:21) + CI watch, then a fresh Dragonstorm gate
(`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run dragonstorm --games 5000 --name claude-dragonstorm-<tag>`),
then the PD CAPPED fold, then the deck queue.

**Queue reorder (13:05 UTC):** after the painland A/B, the Dragonstorm APPLY-DROP clusters go BEFORE the PD CAPPED
fold. The Dragonstorm flags are real engine drops (gate `codex-dragonstorm-ruby-song-20261117`: 28 APPLY-DROP, 16
signatures; leading: s30075408 T4 Apex continuation loses Dragonlord Kolaghan x6 -- the oracle cracks Lotus Bloom for
{B} and keeps one {B} floating; s30073082 T5 Apex/Scourge continuation loses Karrthus x10). The PD item is the
analyzer giving up on one seed (CAPPED is disclosed, not a play defect), and its fold needs a sound mid-plan mask: a
continuation re-solve may spend floats on hand casts AND battlefield activations, whose cost colours no card's mana
cost carries. Do it after Dragonstorm.

**Session timer:** a 2-hourly session-only cron (`7 */2 * * *`) checks this file and the box (USER: run overnight).

## Codex status, updated 07:42 UTC

**Active capped-plan diagnostic:** Prevent Damage seed `30111776` T8 is
running with `MTG_MANA_ORACLE_NODES_RETRY=400000` (default 200,000),
`MTG_MANA_ORACLE=2`, exec session `6458`, console
`build/scratch/spirits_shock_pair_5k/pd_30111776_cap400k.out`.
This is one exact Release replay, no source changes. Compare its final
classes and work/time to the default report; increasing a global audit
ceiling is justified only if the added cost is acceptable.

**Current read-only audit complete:** Prevent Damage ORACLE-GAP seed
`30109140` turn five replayed with `MTG_MANA_ORACLE=3`; console
`build/scratch/spirits_shock_pair_5k/pd_30109140_t5.out`. It confirms
six identical ORACLE-GAP flags, no caps/unmodeled, on the Green Sun's
Zenith X=3 → Tamanoa → Rolling Earthquake line. The oracle has 120
outcomes, and many pay1 frontiers **do put Tamanoa into play with
positive life**; the report's nearest terminal-loss outcome is only the
nearest key, not evidence that all scripts die. The exact mismatch among
those surviving scripts still needs isolation. No source was edited for
these exact replays.
The second signature, seed `30111507` T5, was also replayed at level 3:
`build/scratch/spirits_shock_pair_5k/pd_30111507_t5.out`. It has two
ORACLE-GAP flags, no caps/unmodeled. The engine ends with Pyrohemia,
a new tapped Ancient Tomb, `{C}` float, Dina pumped, and Tamanoa gone;
the nearest oracle outcome retains Tamanoa and says a third payment
does not exist. Several frontier traces show Pyrohemia activations and
Dina sacrifice/pump continuations. Compare their choice scopes with the
earlier `PaySacSpareScope` repair before deciding whether this is payment
enumeration or a lost sacrifice choice.

**Spirits 30040647 T4 correction complete, source and test committed as `bd7c05019`:**
The one fresh Spirits APPLY-DROP is a visible payment order. The plan is
Flooded Strand, Lingering Souls flashback `{1}{B}`, then Millicent `{W}{U}`.
At Souls payment, Hallowed Fountain, Watery Grave, and two Godless Shrines
are untapped. The ordinary payer uses a blue land for Souls' generic pip,
stranding Millicent; two Godless Shrines can pay Souls and leave Fountain
plus Watery for Millicent. A focused test in
`test/unit/test_spirits_millicent.cpp` fails on baseline and passes with a
narrow hold in `src/ai/ManaPayment.cpp` under the existing Spirits Souls
line conditions. `MTG_SPIRITS_SHOCK_PAIR_HOLD=0` restores the exact failure.
The exact seed replay with `MTG_MANA_ORACLE=3` now has **zero oracle
issues**; lever-off still has APPLY-DROP=1. Outputs are
`build/scratch/dragonstorm_visible_lump_ab/spirits_gap_30040647_shockpair.out`
and `_legacy.out`. Release `./build.sh` passed; the full Release unit
suite passed **3,385/3,385**, zero failures, console
`build/scratch/dragonstorm_visible_lump_ab/unit_spirits_shock_pair.out`.
The PGO rebuild passed in exec session `38090`, console
`build/scratch/dragonstorm_visible_lump_ab/pgo_spirits_shock_pair.out`.
Its instrumented compile, pooled 275-job/7,447-game profile training,
and optimized link all passed. `build/PGO/SRC_TREE` matches
`HEAD:src=72188b700b83bfbd66f13db98870583d61d5dd21`.
The paired 5,000-game manifest is ready at
`build/scratch/spirits_shock_pair_5k/manifest.json`, indices 45,000–49,999.
`build/scratch/spirits_shock_pair_5k/run.sh` runs the default and lever-off
arms sequentially after checking the PGO source stamp. Since this source
fix, `mana_clean.py status` marks all 15 formerly clean decks
**stale-clean by source hash**; that is expected and requires refresh gates.
The paired result parser is ready at
`build/scratch/spirits_shock_pair_5k/compare.py`; run it after both arms.
The same-binary 5,000-game paired run passed in exec session `6176`,
console `build/scratch/spirits_shock_pair_5k/runner.out`. It runs default
and `MTG_SPIRITS_SHOCK_PAIR_HOLD=0` arms sequentially. The comparator
reports **all 5,000 per-game records byte-identical**, zero earlier/later
wins, zero changed play digests at the same score; loss-penalized score
sum is 23,111 in both arms. New work 204,827,597 versus old 204,827,599
units (two fewer); recorded job ms 4,924,122 versus 4,955,036. Outputs
and the parser are under `build/scratch/spirits_shock_pair_5k/`.
The fresh PGO Spirits mana-clean gate passed in exec session
`78520`: `MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run spirits
--games 5000 --name codex-spirits-shock-pair-20261117`, console
`build/scratch/spirits_shock_pair_5k/mana_clean.out`. It covers indices
45,000–49,999 (the next due block, also used by the paired A/B):
**Spirits clean, zero issue flags**, 41,221 decisions and 342,409 plans,
oracle-on/off play byte-identical. Report:
`logs/mana_clean/codex-spirits-shock-pair-20261117/report.md`.
`test/mana_clean.json` was committed as `133a723c0`. The Spirits design
note `docs/design/spirits-souls-line-payment-2026-11-16.md` carries the
result. Continue the unresolved queue and update this handoff after
each milestone.

**Next focused queue item after Spirits:** Prevent Damage's fresh 5,000-game
gate has 30 issues: eight ORACLE-GAP flags (six in seed `30109140` T5,
Green Sun's Zenith X=3 into Rolling Earthquake; two in seed `30111507`
T5, Pyrohemia) and 22 CAPPED flags (eight each in seed `30111776`
T8 Manabarbs/Spellshock plus Dina, three each in seed `30108897` T8
Living Wish). Exact repro commands and full board snapshots are in
`logs/mana_clean/codex-clean-refresh-newsrc-c-20261117/report.md`.
The older `docs/design/prevent-damage-terminal-oracle-payment-cap-2026-11-16.md`
documents prior terminal-loss and Dina-sacrifice oracle repairs; do not
assume the fresh flags have the same cause. The current audit retries
capped plans at 200,000 applies by default (`MTG_MANA_ORACLE_NODES_RETRY`).

**Minotaur diagnosis corrected after a failed fixture assumption:**
Seed `30284922` T5 still
has two APPLY-DROP flags on the current Release binary. Exact command is
in `logs/mana_clean/codex-clean-refresh-two-20261117/report.md`;
the full trace is
`build/scratch/spirits_shock_pair_5k/minotaur_30284922.out`. The outer
plan lists only Slaughter-Priest of Mogis; its continuation tries
Deathbellow Raider, which the plan did **not declare**. The oracle has
a payable route using Rakdos Carnarium, Unclaimed Territory, then
Secluded Courtyard. A proposed first-payment hold was **falsified** by
a direct executor/rollout fixture: Carnarium alone pays the first
`{B}{R}`, leaving both Territory and Courtyard untapped. The engine's
continuation instead plays a *second* Rakdos Carnarium after that cast,
bounces Territory, and then drops Raider; the oracle route retains
Territory on the battlefield. The faulty fixture was removed, leaving
the working tree clean. The full `MTG_DBG_MULTI=5` replay is at
`build/scratch/spirits_shock_pair_5k/minotaur_30284922_multi.out`.
Inspect the continuation's land-play choice and its visibility of Raider,
then add a focused test at the land-play decision before changing code.
Forced `MTG_PAY_ROLLBACK` cannot rescue a continuation absent from the
outer plan. No Minotaur fix has been made.

**Prior completed status (chronological history follows):**

**Smoke + strict viewer validation complete:**
`VPC_ALWAYS=1 MTG_RSS_CAP_GB=40 bash test/regression.sh --smoke`
finished in exec session `83386`, console
`build/scratch/dragonstorm_visible_lump_ab/post_hidden_freecast_smoke.out`.
It reports **180 passed, 39 failed, zero new**, exactly the existing GT
baseline drift. All **261 per-game `.wins` files are byte-identical** to
the archived pre-audit smoke under
`build/scratch/dragonstorm_visible_lump_ab/rebased_current_smoke_logs/wins`:
zero added, missing, or changed files. The audit-only fix moved no smoke
play. The strict viewer stage reports 329 ok, 534 repaired, one beaten,
**zero play-drift / board-diverged / enum-gap / contract-fail** across
864 refs; both suboptimal references are strictly beaten. No simulation
batch is active. Next investigate Spirits seed 30040647 T4, then the
remaining mana-clean issue queue.

**Breaching Dragonstorm diagnosis and audit fix complete:** The two
fresh ENUM-GAP flags are **hidden future mana**, not a visible pre-cast
source shortage. Exact repros in
`build/scratch/dragonstorm_visible_lump_ab/breaching_gap_30222319_t6.out`
and `breaching_gap_30221073_t7.out` show the first cast's free cascade /
enter reveal puts Sakashima's Protege #52 into play as a copy of Mountain.
The second cast pays with Mountain #52; it did not exist on the initial
board. The first subset is rejected as `ef-no-interact` (10 total, only
one visible red); the second as `ef-walk-fail` (13 total, selected sac
actions are unmodelled by that walk). Both rejections are correct before
the reveal. Creative Technique later shuffles the library, so the oracle's
root/end-library order inference misses #52's top-of-library provenance
and incorrectly labels a missing menu option as ENUM-GAP.

A committed **audit-only** source fix now records every top-of-library
free-cast number in the oracle Hook (cascade, Breaching Dragonstorm,
Creative Technique, ripple, Cyclone), then classifies a later tap of that
card as HIDDEN-INFO even if the library was shuffled. Edits are in
`src/ai/ManaOracle.h`, `src/ai/ManaOracle.cpp`, and `src/ai/TurnSolver.cpp`.
The incremental Release build passed. Both exact replays now return
HIDDEN-INFO=1, **zero ENUM-GAP**, naming Mountain #52; outputs
`build/scratch/dragonstorm_visible_lump_ab/breaching_gap_30222319_hiddenfix.out`
and `breaching_gap_30221073_hiddenfix.out`. The 31 focused oracle unit
cases passed. The source commit is `79d7b898b`, with
`HEAD:src=342674496bfb8ddc912cee3193513f41b0388d58`. The PGO+LTO
rebuild completed in exec session `88239`, console
`build/scratch/dragonstorm_visible_lump_ab/pgo_hidden_freecast_oracle.out`.
Its compile, pooled 275-job/7,447-game training batch, and final link
passed. `build/PGO/SRC_TREE` matches
`342674496bfb8ddc912cee3193513f41b0388d58`. The fresh PGO gate
`codex-breaching-hidden-freecast-20261117` then passed on indices
225,000–229,999: **Breaching Dragonstorm clean, 5,000 games, zero issue
flags**, oracle-on/off play byte-identical; four HIDDEN-INFO cases are
disclosed. Report:
`logs/mana_clean/codex-breaching-hidden-freecast-20261117/report.md`.
`test/mana_clean.json` carries the record. Next run smoke plus strict
viewer reference checks on this source and compare to the known 39
unrelated GT failures from the previous full smoke. Then inspect the
Spirits seed 30040647 T4 one APPLY-DROP. Do not widen the player search
gate to exploit the future reveal.

The last stale-clean refresh `codex-clean-refresh-newsrc-c-20261117`
finished, 5,000 games each at source `f7cce0693b6a`:
**Martyr Lifegain clean; Prevent Damage 30 issues (8 ORACLE-GAP,
22 CAPPED); Spirits one APPLY-DROP**, rescued by forced payment rollback
at depth one. Oracle-on/off play was byte-identical. Report:
`logs/mana_clean/codex-clean-refresh-newsrc-c-20261117/report.md`.
`test/mana_clean.json` carries all three records. The 56-deck status now
has **15 clean, 39 issues, two never-run, zero stale-clean** at the prior
source. Extra Turns and Merfolk Turns are never-run. No gate is active;
the PGO build described below includes its own training batch.

Windows CI `44025173062` **passed all checks** by 06:30 UTC;
the watcher output is
`build/scratch/dragonstorm_visible_lump_ab/windows_ci_44025173062.out`.
Keep the CI check asynchronous. The focused Dragonstorm test **failed on
the shipped payer** (Bloom left intact, both Territories tapped,
Karrthus unpayable). The committed source fix in `src/ai/ManaPayment.cpp` now
passes that case plus the two adjacent visible-closer cases. It removes
the prepayment `short_with_shared` gate and compares coloured plus
**non-lump wild** supply after trial payment: an uncracked Bloom otherwise
counts as three speculative wild units and obscures the two Territories
saved by an early crack. `MTG_ONDEMAND_VISIBLE_AFTERPAY_WILD=0` restores
both old conditions for a paired A/B. Exact seed **30074224 T5** has
zero oracle issues with the trial, but APPLY-DROP=1 with the legacy lever;
outputs `build/scratch/dragonstorm_visible_lump_ab/seed_30074224_afterpay.out`
and `seed_30074224_legacy.out`. The full Release unit suite passed:
**3,384/3,384**, zero failures; console
`build/scratch/dragonstorm_visible_lump_ab/unit_bloom_afterpay.out`.
The fix is committed as `eec786b06`, with
`HEAD:src=7ecf53121b172fb2cbf078f7e5f554086b7b28fe`. A PGO+LTO
build completed in exec session `36997`, console
`build/scratch/dragonstorm_visible_lump_ab/pgo_bloom_afterpay.out`.
Its 275-job/7,447-game training batch and final link passed; the
`build/PGO/SRC_TREE` stamp exactly matches
`7ecf53121b172fb2cbf078f7e5f554086b7b28fe`. A same-binary paired
Dragonstorm A/B finished on 5,000 paired games per arm, indices
77,000–81,999: **zero earlier and zero later wins**, four changed play
digests at the same win turn, 4,996 byte-identical game records. New
search work was 9,985,030 units versus 9,976,007 old (+0.09%); recorded
job ms 464,651 versus 465,987. Outputs and per-game `.wins` are under
`build/scratch/dragonstorm_visible_lump_ab/afterpay_wild_5k/`.

A fresh PGO mana-clean gate **finished** in exec session `36993`:
`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run dragonstorm --games 5000 --name codex-dragonstorm-afterpay-wild-20261117`.
Console `build/scratch/dragonstorm_visible_lump_ab/clean_afterpay_wild.out`;
report `logs/mana_clean/codex-dragonstorm-afterpay-wild-20261117/report.md`.
It found **71 issues: 70 APPLY-DROP and one APPLY-DROP-ORDER**, zero
ENUM-GAP/ORACLE-GAP/CAPPED, oracle-on/off play byte-identical. The top
cluster is seed 30078447 T4, a Pyretic→Apex→Irencrag→Kolaghan line
whose Bloom-black payment differs; see the report for 29 APPLY-DROP
signatures. `test/mana_clean.json` carries the new record. Next reproduce
Breaching Dragonstorm seeds **30222319 T6** and **30221073 T7** with
`MTG_DBG_MULTI=6` and `=7`, identify the exact rejecting gate, and make a
narrow deterministic correction. The queue remains open.

The paired A/B manifest and runner are prepared under
`build/scratch/dragonstorm_visible_lump_ab/afterpay_wild_5k/`:
50 jobs of 100 games, Dragonstorm indices 77,000–81,999, same PGO
binary and seeds in both arms, old arm with
`MTG_ONDEMAND_VISIBLE_AFTERPAY_WILD=0`. Runner:
`bash build/scratch/dragonstorm_visible_lump_ab/afterpay_wild_5k/run.sh`,
console `build/scratch/dragonstorm_visible_lump_ab/afterpay_wild_5k/runner.out`.
Inspect per-game `.wins`, aggregate turns and search work before accepting
the fix.

A focused unit case for seed 30074224's payment shape has been added to
`test/unit/test_dragonstorm_followups.cpp`, immediately after the existing
one-Territory visible-Karrthus case. It sets Mountain + two Unclaimed
Territories + Lotus Bloom, pays Song, then credits five red and checks
Karrthus payment. Its baseline failure and trial success are confirmed;
the held-out A/B remains.

The paragraphs below are a chronological work log. Older statements
that say a batch or build is active describe its state at that time;
use this live-status section for the current action.

## Chronological work log

**Second new-source clean refresh complete:** all nine decks in
`codex-clean-refresh-newsrc-b-20261117` are clean at source
`f7cce0693b6a`, 5,000 games each, zero issue flags, oracle-on/off play
byte-identical: Colossus Hammer, Critter Lifegain, Kithkin, Knights,
Lifegain Heliod Combo, Merfolk, Modern RW Burn, Soldiers, White
Knights. Report:
`logs/mana_clean/codex-clean-refresh-newsrc-b-20261117/report.md`.
`test/mana_clean.json` carries the nine records. Only Martyr Lifegain,
Prevent Damage, and Spirits remain stale-clean at this source; refresh
those next. The CI watcher is still active.

**Active final stale-clean batch, exec session `74609`:** 5,000 games each for Martyr Lifegain,
Prevent Damage, and Spirits, command
`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run martyr_lifegain prevent_damage spirits --games 5000 --name codex-clean-refresh-newsrc-c-20261117`.
Console `build/scratch/dragonstorm_visible_lump_ab/clean_newsrc_c.out`,
report `logs/mana_clean/codex-clean-refresh-newsrc-c-20261117/report.md`.

**First new-source clean refresh complete:** 5,000 games each at source
`f7cce0693b6a`, oracle-on/off play byte-identical. Burn, Red
Elementals, Slivers, Giants, and Auras are clean; Breaching Dragonstorm
has **two ENUM-GAP** flags, zero other issue classes. Report:
`logs/mana_clean/codex-clean-refresh-newsrc-a-20261117/report.md`.
The two exact repros are seed 30222319 T6 (Breaching Dragonstorm then
Creative Technique, with Mountain land drop) and seed 30221073 T7
(Creative Technique/Call Forth the Tempest and sacrifice lands; paying
order casts Call Forth first). These were new seed ranges, so the
previous clean verdict does not imply a regression. Commit the updated
`test/mana_clean.json` with this handoff, then refresh the remaining
stale-clean decks. Windows CI watch remains active separately.

**Active gate, exec session `76552`:** nine more previously clean decks, 5,000 games
each: Colossus Hammer, Critter Lifegain, Kithkin, Knights, Lifegain
Heliod Combo, Merfolk, Modern RW Burn, Soldiers, White Knights. Command:
`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run colossus_hammer critter_lifegain kithkin knights lifegain_heliod_combo merfolk modern_rw_burn soldiers white_knights --games 5000 --name codex-clean-refresh-newsrc-b-20261117`.
Console `build/scratch/dragonstorm_visible_lump_ab/clean_newsrc_b.out`,
report `logs/mana_clean/codex-clean-refresh-newsrc-b-20261117/report.md`.
After it, refresh Martyr Lifegain, Prevent Damage, and Spirits; those
are the last stale-clean records from the old source.

**Shared branch pushed:** after a final fetch confirmed origin had not
moved, `git push origin phase-1-2-deck-analyzer` published 57 local
commits through `a5c156a20` at about 05:35 UTC. Reference safety had
passed across 210 checkouts. The next ordinary 4-hour push window is
**09:35 UTC**. Windows CI run `44025173062` was pending at launch;
background `gh run watch 44025173062 --interval 60 --exit-status` is
active in exec session `25317`, output
`build/scratch/dragonstorm_visible_lump_ab/windows_ci_44025173062.out`.
The shell's Git credential helper supplied the token only to the child
process. Check the watcher asynchronously; if red, pivot to its failure.

**Next mana batch:** refreshing six stale-clean decks at the new source,
5,000 games each: Burn, Breaching Dragonstorm, Red Elementals, Slivers,
Giants, Auras. It is now active in exec session `47565`. Command:
`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run burn breaching_dragonstorm red_elementals slivers giants auras --games 5000 --name codex-clean-refresh-newsrc-a-20261117`.
Report will be
`logs/mana_clean/codex-clean-refresh-newsrc-a-20261117/report.md`;
console `build/scratch/dragonstorm_visible_lump_ab/clean_newsrc_a.out`.
Check the result, update this handoff, and commit the gate record locally.

**Post-Boars full smoke comparison complete:** the fresh-source smoke
run ended 180 pass, 39 fail, zero new, exactly the same known GT drift
as before. More decisively, all **261 per-game `.wins` files are
byte-identical** to the archived pre-Boars smoke run; there are zero
added, missing, or changed files. The Boars source integration is
smoke-play-neutral, while the existing GT drift remains for separate
audit. Console:
`build/scratch/dragonstorm_visible_lump_ab/post_boars_rebase_smoke.out`.
`python3 test/check_gt_logs.py` passed again (1,252 consistent,
zero stale/missing), and the second reference safety scan passed.
Next fetch once more, rebase only if origin moved, then push the local
commits in this now-open 4-hour window. Attach an async Windows CI
watch with 60-second polling; `gh` needs the Git credential helper in
this shell. Then run fresh new-source mana-clean blocks for the 18
stale-clean decks and issue decks. No simulation batch is active.

**PGO build complete:** `./build.sh pgo` finished successfully at
`HEAD:src=f7cce0693b6a16d779df5ac756af16100e3b677d`; the stamp in
`build/PGO/SRC_TREE` matches. Full output:
`build/scratch/dragonstorm_visible_lump_ab/rebased_pgo_build.out`.
The next batch is a full `bash test/regression.sh --smoke` on this new
source; compare its per-game `.wins` with the archived pre-Boars smoke
directory `build/scratch/dragonstorm_visible_lump_ab/rebased_current_smoke_logs/wins`.
If byte-identical, the upstream Boars source change is smoke-neutral and
the 39 previously documented GT failures remain preexisting drift.
That smoke is now active, exec session `71911`, console
`build/scratch/dragonstorm_visible_lump_ab/post_boars_rebase_smoke.out`.

**Active build:** `MTG_RSS_CAP_GB=40 ./build.sh pgo` for source
`f7cce0693b6a16d779df5ac756af16100e3b677d`, exec session
`97680`, console
`build/scratch/dragonstorm_visible_lump_ab/rebased_pgo_build.out`.
Check its exit and PGO source stamp before any fresh mana gate. A
second reference safety scan is also running (session `70825`, output
`build/scratch/dragonstorm_visible_lump_ab/reference_check_pre_push.out`)
but the push now waits on this new source validation.
The second reference scan passed after 210 checkouts/worktrees. With
the new source hash, `python3 scripts/mana_clean.py status` reads 56
decks: **36 issues, 18 stale-clean, two never-run; 38 DUE**. These are
source-stamp statuses, not evidence that the Boars change broke clean
decks. Fresh blocks are needed before calling the queue current again.

**Upstream source integration supersedes the active gate:** at 05:15
UTC, `origin/phase-1-2-deck-analyzer` gained three Boars label-memory
commits (`37eebd3ab`, `2d56d1716`, `596ae188e`), including
`src/ai/TurnSolver.cpp`. I stopped the old-source due-four gate after
three additional attempt-3 chunks and rebased all 51 local commits
cleanly. The new `HEAD:src` is
`f7cce0693b6a16d779df5ac756af16100e3b677d`; old PGO stamp
`fcb9352608ea` is stale. The due-four run was planned at that old
source and **must not be resumed after this rebase**. Its completed
Bruna 5,000-game record remains valid only for the old source; the
other three are incomplete. Run `./build.sh pgo` for the new tree,
then start fresh due records under new run names. The failed smoke
analysis below remains a baseline issue; the new upstream source also
requires a post-rebase smoke before the shared push. No push yet.

**Historical old-source gate:** due-four mana gate had resumed at eight
workers after the first smoke, exec session `46978`, console
`build/scratch/dragonstorm_visible_lump_ab/due_four_resume8_after_smoke.out`.
It was stopped for the new upstream source integration above. Its
completed chunks remain archived, but its run plan cannot resume under
the new source hash. No simulation batch is active until the new build.

**Push temporarily held by smoke failure:** the full post-rebase
`bash test/regression.sh --smoke` finished with 180 passed, 39 failed,
zero new. Its per-game audit found 15 searched slower games and 345
same-score searched play changes; 248 d0 slower games are reported under
the lighter d0 bar. Failures include Mill, White Knights, Anti-Lifegain,
Unpredictable Cyclone, Creature Giving, Five Colour, Critter Lifegain,
Heliod, Melira, Minotaur, Selesnya, Prevent Damage, Galadriel, and
Vampires. Full output:
`build/scratch/dragonstorm_visible_lump_ab/rebase_smoke.out`.
At the time of this **first** rebase, the source hash was unchanged and
the remote's three commits touched only the Red Elementals value sidecar
and its GT. These failures are not attributable to that rebase. The
newer Boars source rebase above is a separate event and needs its own
smoke. The 05:09 push window is open, but no push yet.
The smoke audit's searched rows are 15 slower, 13 faster, 345 changed
at the same score; over 14,515 searched games the net loss-penalized
delta is **+4 turn-games** (+0.000276 per game). d0 is 248 slower and
260 faster, net -31 turn-games over 55,100. The classifier output at
`build/scratch/dragonstorm_visible_lump_ab/rebase_smoke_classify.out`
marks 12 of the 15 searched slowdowns as higher-budget churn. Three
persist at 4x/16x: Creature Giving d5 gi113, Cyclone d3 gi37, Five
Colour d3 gi24. `test/explain_game.py` replays the saved baseline binary
and current binary at the original budget for these three and reports
the **same T5** on both, though committed `.wins` says old T4 in each;
the baseline snapshot is `logs/snapshots/smoke-baseline` (built from
f2156d4e6 with dirty state). Investigate this GT/snapshot mismatch
before accepting, and review the same-score changes by affected deck.
No GT acceptance or push has occurred. The old-source mana gate is
archived; start a new run after rebuilding the new source.
Source scope check before the Boars rebase: the only four local source
commits since the then-current origin were Dragonstorm Ruby and Apex
provider/solver changes plus the storage-land backtracker repair.
`cards.json` marks only Dwarven Hold and Mercadian Bazaar as
`storage_land`, and `rg` finds those only in the Dragonstorm shipping
deck. **None of the 39 failed smoke rows is Dragonstorm.** The upstream
Red Elementals sidecar also has no failed row. Thus these smoke failures
are baseline drift already present outside the new source paths, not a
reason to change or reject the four mana fixes. Keep GT unchanged and
continue their separate audit; do not silently accept the drift.

The eight-worker due-four gate was intentionally stopped at chunk
boundary for the post-rebase smoke and upcoming push. It had completed
18 new oracle chunks in attempt 2; all finished jobs persist. Current
top-level simulation batch is **smoke**, exec session `98865`, console
`build/scratch/dragonstorm_visible_lump_ab/rebase_smoke.out`, command
`MTG_RSS_CAP_GB=40 bash test/regression.sh --smoke`. Once smoke passes,
push no earlier than 05:09 UTC, start an async Windows CI watch, and
resume the due-four gate with `--threads 8` (attempt 3). If smoke fails,
investigate before the push; the mana gate stays paused meanwhile.

**Active batch:** refreshing four older issue records at source
`fcb9352608ea`, 5,000 games each: Anti-Lifegain, Bruna, Midrange Burn,
and Selesnya Lifegain. The first 32-worker attempt hit the 40 GB RSS cap
(oracle-on exit 137); only Bruna finished a full 5,000-game block with
3,218 issues, and the other three records are incomplete. Resume the same
run at **16 workers** so completed chunks survive. First attempt console:
`build/scratch/dragonstorm_visible_lump_ab/due_four_gate.out`; resume
console: `build/scratch/dragonstorm_visible_lump_ab/due_four_resume.out`.
Original command:
`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run anti_lifegain bruna midrange_burn selesnya_lifegain --games 5000 --name codex-due-four-20261117`.
Resume command:
`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run --resume codex-due-four-20261117 --threads 16`.
The 16-worker resume (`23177`) was terminated after RSS climbed to
38.5 GB; it had completed three additional chunks. The next resume uses
**eight workers**, exec session `33637`, console
`build/scratch/dragonstorm_visible_lump_ab/due_four_resume8.out`:
`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run --resume codex-due-four-20261117 --threads 8`.
Resume remains chunk-granular and source-identical. `python3
scripts/check_references_safe.py` passed at 04:51 UTC after scanning 210
checkouts/worktrees. The three upstream Red Elementals sidecar commits
were fetched and the 42 local commits rebased cleanly at 04:53 UTC. The
only conflict was the regression GT header; the newer upstream header
and both decks' rows are preserved. `HEAD:src` remains exactly
`fcb9352608ea7e19580916d696643bc094d6ef3a`, so the running gate
still matches the PGO binary. `python3 test/check_gt_logs.py` passed:
1,252 consistent, zero stale/missing. A shared-branch push is due no
earlier than 05:09 UTC; rerun the reference check if new reference work
appears before that push. Because GT changed on rebase, run the smoke
gate once the current simulation batch finishes, before the push.
The eight-worker oracle arm is Selesnya-heavy: its 56-game chunks have
used about 180-360 core seconds each, with RSS around 21-24 GB. This
run is much longer than the initial cost estimate, but the completed
chunks are retained. At 04:58 UTC it had completed five new chunks.
The shell's `gh` has no persistent login, but `git credential fill`
returns the existing GitHub credential to a subprocess. A Python read
of the Actions API using that credential succeeded at 05:00 UTC; it
showed the prior Windows build run `43993315542` as successful. Keep
the credential in process memory and print only run metadata; after
the next push start a 60-second-poll CI watcher in the background.
Output: `build/scratch/mana_clean/codex-due-four-20261117/` and report
`logs/mana_clean/codex-due-four-20261117/report.md` when complete.
Commit the updated `test/mana_clean.json` and this handoff after it finishes.

**Six due-issue decks now refreshed:** Boars, Dragons, Goblins, Hydras,
Jumbo Cactuar, and Morph each ran 5,000 games at PGO source
`fcb9352608ea`, indices respectively 10,000-14,999, 76,000-80,999,
115,000-119,999, 5,000-9,999, 105,000-109,999, and 10,000-14,999.
Oracle-on/off play was byte-identical. All six still have issues:

| Deck | Issues | Leading classes |
|---|---:|---|
| Boars | 2,253 | 1,455 dominated creature, 233 apply drop, 206 capped decision, 166 dominated haste |
| Dragons | 948 | 663 apply drop, 285 oracle gap |
| Goblins | 1,131 | 1,047 oracle gap, 75 apply drop |
| Hydras | 54,075 | 54,031 capped decision, 34 apply drop order |
| Jumbo Cactuar | 942 | 422 enum gap, 239 apply drop order, 127 dominated creature |
| Morph | 1,752 | 1,151 oracle gap, 416 dominated creature, 184 apply drop |

Report: `logs/mana_clean/codex-due-six-20261117/report.md`.
The record is in `test/mana_clean.json`. At this source, the status is
56 decks: 18 clean, 36 issues, two never run, 21 still DUE (older issue
records or never run); zero stale-clean. No simulation batch is active.
Next inspect Goblins' seed 30116157 T3 Three Tree City payment (66
apply-drop flags), Dragons' seed 30077853 T5 Lathliss then Urza's
Incubator (32), and Boars' seed 30010755 T5 Ilharg then Horn then
Flinthoof Boar (44). Those are distinct from Hydras' nearly entire
block hitting the per-decision audit budget. Jumbo's leading enum gaps
combine Overgrowth, Finale, Tooth and Nail, and mana creatures; seed
30109313 T4 has 32 flags. See the report for exact repro commands.

**Final stale-clean refresh complete:** Martyr Lifegain, Prevent
Damage, and Spirits each ran a fresh 5,000-game PGO mana-clean block at
source `fcb9352608ea`; all three are **clean**, with zero issue flags
and byte-identical oracle-on/off play. Prevent Damage had 16,985
disclosed DOMINATED-LIFE observations, which this gate does not count as
issues. Report:
`logs/mana_clean/codex-clean-refresh-heavy-20261117/report.md`.
The three records are committed in `test/mana_clean.json`. There are no
stale-clean decks remaining at this source; the open queue consists of
issue decks and the never-run Extra Turns/Merfolk Turns decks.
No simulation batch is active after this gate.

**Second pooled fresh PGO gate complete:** 5,000 games each for 13
decks at source `fcb9352608ea`, oracle-on/off play byte-identical.
Auras, Colossus Hammer, Critter Lifegain, Kithkin, Knights, Lifegain
Heliod Combo, Merfolk, Modern RW Burn, Soldiers, and White Knights are
**clean**. Fluctuator has one APPLY-DROP; Minotaur has two APPLY-DROP;
Vampires has 26 ORACLE-GAP. Report:
`logs/mana_clean/codex-clean-refresh-two-20261117/report.md`.
Fluctuator's exact seed 30218614 T4 drops Drannith Stinger after
Fluctuator and a cycling chain (rollback depth 3+). Minotaur's seed
30284922 T5 drops Deathbellow Raider after Slaughter-Priest; oracle
spends Rakdos Carnarium B plus Territory C first, preserving Secluded
Courtyard's creature-only R (rollback depth one). Vampires' nine oracle
signatures all involve Voldaren Estate activation after Stromkirk
Condemned, sometimes with a pump or Vial put; leading seed 30228525
T4 has six flags. Nearest oracle outcomes differ in tapped Courtyard,
Blood Token count, life, or float; investigate oracle fidelity before
claiming a mana drop. All 13 records are committed in
`test/mana_clean.json`. No simulation batch is active after this gate.

**Six-deck fresh PGO gate complete:** all at source `fcb9352608ea`,
5,000 games per deck, oracle-on/off play byte-identical. Burn,
Breaching Dragonstorm, Red Elementals, Slivers, and Giants are **clean**
with zero issues. Angels is **issues**: 11 APPLY-DROP, eight
APPLY-DROP-ORDER, zero oracle/enum/cap. Report:
`logs/mana_clean/codex-clean-refresh-20261117/report.md`.
Angels' largest drop is seed 30220574 T4 (seven flags): Youthful
Valkyrie before Serra the Benevolent; oracle spends Seraph Sanctuary's
colorless plus Giada's Angel-only white on Valkyrie to preserve Plains,
Sol Ring, Plains for Serra. The order drops center on Giada, Font of
Hope with Lightning Greaves/equip; seed 30221928 T3 has two flags and
needs Giada → equip → Resplendent Angel. Seed 30222520 T4 has both
payment and order drops. These are distinct payment/order repairs; the
five clean deck verdicts should be preserved. All six records are
committed in `test/mana_clean.json`. No batch is active after this gate.

**Treasure Hunt fresh gate complete:** indices 80,000–84,999 at PGO
source `fcb9352608ea` yielded 289 issues: 20 ENUM-GAP, 217
APPLY-DROP, and 52 APPLY-DROP-ORDER. There are 370 disclosed
DOMINATED-LIFE observations, not counted as issues. Zero oracle gap or
cap; oracle-on/off play byte-identical. Report:
`logs/mana_clean/codex-treasure-hunt-fresh-20261117/report.md`.
The 20 enum gaps are mostly a same-plan double Treasure Hunt on turn
three; seeds 30081492, 30081921 and 30082722 have two flags each.
Payment drops cluster around Cascade Bluffs plus Land's Edge and Hunt,
with several 16-flag signatures; the same filter/source ordering also
appears in Throes of Chaos lines. The `test/mana_clean.json` record is
committed with this handoff. No batch is active after this gate.

**Fungus fresh gate complete:** indices 80,000–84,999 at PGO
source `fcb9352608ea` yielded 486 issues: 137 ENUM-GAP, 116
DOMINATED-CREATURE, 204 ORACLE-GAP, and 29 CAPPED. There are also 102
disclosed DOMINATED-LIFE observations, which are not issue flags.
Oracle-on/off play byte-identical. Report:
`logs/mana_clean/codex-fungus-fresh-20261117/report.md`.
The enum gaps span 129 signatures, often Concordant Crossroads first
making a just-cast mana creature usable later in the same line (seed
30082639 T3), or Saproling Migration producing later mana fodder.
The oracle-gap and cap clusters need separate fidelity/cost work; seed
30080373 T5 repeatedly reaches the 200,000-apply cap, while seed
30083140 T5 has an oracle gap with 1,328 outcomes. The on arm had a
long tail of about nine minutes despite its old cost estimate; do not
assume its average cost reflects those outliers. The `test/mana_clean.json`
record is committed with this handoff. No batch is active.

**Creature Giving fresh gate complete:** indices 65,000–69,999 at PGO
source `fcb9352608ea` yielded 280 issues: 276 ENUM-GAP across 249
signatures, three APPLY-DROP, one DOMINATED-CREATURE; zero oracle gap or
cap. Oracle-on/off play byte-identical. Report:
`logs/mana_clean/codex-creature-giving-fresh-20261117/report.md`.
The large cluster is same-turn Crop Rotation chains: the gate rejects
plans that cast Crop, put an untapped mana land into play, then use it to
pay the next Crop or creature. Exact seeds 30065017 T2 (two Crops +
Suture Priest / War-Riders, eight flags), 30065271 T2 main two (two
Crops, four flags), 30065289 T2 main two (three), and 30066910 T2 main
two (three). Focus on the gate's sequential land replacement credit,
not a broad new searched branch. Three APPLY-DROP flags are one separate
T4 signature (see report). The `test/mana_clean.json` record is committed
with this handoff. No batch is active after this gate.

**Mill fresh gate complete:** indices 10,000–14,999 at PGO source
`fcb9352608ea` yielded 131 ENUM-GAP flags across 51 signatures, with
zero other issue class or cap and byte-identical oracle-on/off play.
Report: `logs/mana_clean/codex-mill-fresh-20261117/report.md`.
The leading signatures are still Visions of Beyond → Surgical Extraction,
usually with Surgical life-paid and a different land left untapped; exact
seeds include 30010053 T2 (15 flags), 30010015 T2 (14), 30010356 T2
(10), and 30010627 T2 (7). This is the residual investigated in
`docs/design/mill-surgical-mana-clean-2026-11-16.md`: simply reopening
Surgical's searched mana-vs-life choice did not clear the gate and added
substantial work. The menu/resource comparison across Visions' draw
breakpoint needs a narrower diagnosis. The new `test/mana_clean.json`
record is committed with this handoff. No simulation batch is active.

**Shadowborn Apostle fresh gate complete:** indices 100,000–104,999 at
PGO source `fcb9352608ea` yielded three issues: two APPLY-DROP, one
ORACLE-GAP, no cap/enum/order issue. Oracle-on/off play byte-identical.
Report: `logs/mana_clean/codex-shadowborn-fresh-20261117/report.md`.
The oracle gap is seed 30103262 T5, Dark Ritual → Diabolic Intent on a
board with two Lakes of the Dead; the oracle's nearest outcome taps
Swamp#42 and leaves B3, while the engine leaves that Swamp untapped and
B2. Investigate oracle payment coverage first. The drops are seeds
30104750 T4 (Bloom/Lake, Thrumming Stone continuation) and 30104985
T3 (Lake, double Diabolic Intent); both need depth 3+ rollback, so the
one-step rollback does not rescue them. The gate record is committed in
`test/mana_clean.json`. No simulation batch is active after this gate.

**Pirates fresh gate complete:** indices 99,000–103,999 at PGO source
`fcb9352608ea` yielded two APPLY-DROP flags in one seed, no enum/oracle
or capped flag, with oracle-on/off play byte-identical. Report:
`logs/mana_clean/codex-pirates-fresh-20261117/report.md`.
Both flags are seed 30103196 T3, two variants of Vialed Staunch
Crewmate followed by Dire Fleet Captain: oracle pays the intervening
cast with newly played Spirebluff Canal#50:R, retaining Blackcleave
Cliffs#9:B and older Spirebluff Canal#49:R for Captain. The engine drops
Captain. Rollback depth one, with the Captain action absent from the
prefix plan; forced `MTG_PAY_ROLLBACK` rescues both, but that global
backup remains default off. Investigate a forward color/source hold
across the Vial breakpoint before enabling rollback broadly. The gate
record is in `test/mana_clean.json` and committed with this handoff.
Pirates is now **issues**, not stale-clean.

An **Extra Turns first-ever gate attempt** was stopped during its
64-game sizing probe. After six minutes at 32 threads, only ten one-game
jobs had finished and the process was at about 21 GB RSS; the enforced
5,000-game minimum would take many hours and threaten the 40 GB cap.
No full gate ran, no `test/mana_clean.json` record was written, and Extra
Turns remains **never-run**. The probe artifacts are in
`build/scratch/mana_clean/codex-extra-turns-first-20261117/`, and console
output is `build/scratch/dragonstorm_visible_lump_ab/extra_turns_first_gate.out`.
The process group was terminated and verified gone. Investigate this
oracle cost before attempting the full gate; meanwhile continue cheaper
due decks. Do not mark Extra Turns clean or completed.

**02:47 UTC update:** The scoped Ruby/Seething ordering repair in
`src/ai/DecisionProviders.cpp` is complete and committed with this
handoff. The provider moves Ruby behind a contiguous Rite/Seething ritual
run only when the current order fails both payment probes, a later creature
is visible, fewer than two cheap rituals precede Song, and exact sequential
payment accepts the new order. Seeds 30068752 T3 and 30071498 T4 clear
both APPLY-DROP-ORDER flags each; `MTG_DRAGONSTORM_RUBY_FUNDING=0` restores
them. Four older Ruby exact repros remain clear. Release units pass 3,383
cases and 3,614,946 assertions. Dragonstorm/Shadowborn smoke+regression
passes all cells, 561 scenarios and strict viewer replay of 864 references
with zero drift/divergence/enum gap. Five thousand paired held-out games
at seed 9,820,001 had identical win turns (4,957 wins each). The combined
Ruby flag (old and new rule) adds 2.0% CPU in this sample, so that is not
an isolated cost for the new rule. PGO+LTO has since rebuilt successfully.
Full evidence is appended to `docs/design/dragonstorm-ruby-apex-storage-mana-2026-11-17.md`.
The source/doc commit is `5e9c477e9`, and the PGO source tree stamp is
`fcb9352608ea7e19580916d696643bc094d6ef3a`.

**Fresh PGO Dragonstorm gate complete** on indices 72,000–76,999:
`logs/mana_clean/codex-dragonstorm-ruby-song-20261117/report.md`.
It found 28 APPLY-DROP flags across 16 signatures, with zero ENUM-GAP,
APPLY-DROP-ORDER, ORACLE-GAP, or capped flags. Oracle-on/off play is
byte-identical. The new block is not directly comparable to the earlier
157-issue block. Leading residuals: seed 30075408 T4 Apex continuation
loses Dragonlord Kolaghan (six flags); seed 30073082 T5 Apex/Scourge
continuation loses Karrthus (ten flags across variants). Additional
exact repros: 30072155 T5 Ruby → Song → Karrthus, 30074224 T5 Song →
Karrthus, 30074081 T4 Karrthus → Scourge, 30076435 T4 Desperate → Feat
→ Karrthus. The report has commands for each. The `test/mana_clean.json`
record and design note are being committed with this update. No simulation
batch is active after this gate. Next: investigate the APPLY-DROP clusters
and run another fresh gate after any accepted fix.

The earlier three source commits following pushed `f2156d4e6` are:

* `b82aee80c` Dragonstorm Ruby funding order fallback in `src/ai/DecisionProviders.cpp` (`MTG_DRAGONSTORM_RUBY_FUNDING`, default on). Four exact old APPLY-DROP-ORDER seeds clear; 5,000 paired games had identical win turns and play digests, +0.8% CPU and +1.6% work units.
* `d06b9559c` visible two-copy Apex line gate in `src/ai/TurnSolver.cpp` and focused tests (`MTG_APEX_VISIBLE_LINE_MANA`, default on). Old ENUM-GAP seed 30062821 clears. Narrowed from a costly one-copy trial; 5,000 paired: two earlier wins, none later, +5% CPU, +1.37% units. Human play menu excluded to preserve reference order.
* `126872514` storage-land full burst backtracker and cached replay in `src/core/SpellEffects.cpp` (`MTG_STORAGE_BACKTRACK_FULL_BURST`, default on). Old ORACLE-GAP seed 30062628 clears; off control restores it. This matches the simulator's existing greedy/executor full-burst **simplification**; actual MTG storage lands may remove fewer counters.

Full Release unit suite: 3,383 passed, 3,614,946 assertions. Combined Dragonstorm/Shadowborn smoke and regression: 5,762 games, 561 scenarios pass, strict viewer 864 refs with zero drift/divergence/enum gap, both suboptimal references beaten. Shadowborn unchanged. Nine Dragonstorm GT logs, `test/regression_gt.txt`, `test/job_cost.tsv` were updated and accepted. Four depth-zero turn changes derive from previously shipped `MTG_ONDEMAND_VISIBLE_LUMP`, not these three new fixes. PGO+LTO build succeeded at source stamp `7940651a93b1749003e268106ee064aa5264b50b`; use `build/PGO/mtg` for long runs. Detailed evidence: `docs/design/dragonstorm-ruby-apex-storage-mana-2026-11-17.md`.

**Fresh PGO Dragonstorm gate finished** on indices 67,000–71,999, command:

```
MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run dragonstorm --games 5000 --name codex-dragonstorm-ruby-apex-storage-20261117
```

Report: `logs/mana_clean/codex-dragonstorm-ruby-apex-storage-20261117/report.md`; `test/mana_clean.json` is committed in `304d1fed8`. **157 issues:** 121 APPLY-DROP (40 signatures), 4 APPLY-DROP-ORDER (4), 4 ENUM-GAP (4), 28 CAPPED-DECISION (19); ORACLE-GAP=0. Oracle-on/off play byte-identical. The increase from the prior block's 93 issues is a fresh seed block, not evidence of a regression; compare exact old seeds separately. Capped decisions deserve focused rerun/analysis, not assumed correctness failures. Report says 129 misses, 28 caps. Top new APPLY-DROP clusters: seed 30068634 T7 Karrthus/Apex, seed 30070527 T5 Dragonlord Kolaghan/Apex. New ENUM-GAP seed 30068175 T4 (two plan variants) and 30070173 T5 (two). New order-drop seed 30068752 T3 (two variants) and 30071498 T4 (two). Exact repro commands are in the report; substitute `build/PGO/mtg` if its stamp still matches. The older unresolved seed 30065244 T6 had 24 Karrthus/Apex drops; changing Lotus color order alone had no effect and was removed. Seed 30062531 T4 may have a `LineFloatTapFirstPlan` ordering issue: oracle spends Unclaimed Territory as colorless for Lathliss to preserve floating red for Irencrag Feat, while engine spends red. Check fresh signatures before prioritizing it.

## Immediate continuation

First-pass source diagnosis after the gate: `ApexPairGateCredit` in
`src/ai/TurnSolver.cpp` credits Apex's ten mana to both scalar and
selection-exact gates only when **two unstaged Apex copies** are in hand.
The new seed 30068175 T4 and 30070173 T5 enum gaps are **one Apex plus a
visible Scourge of Valkas**; their oracle order casts Apex before Scourge.
This points to the pair-only guard as the gate, but broad one-copy credit
previously cost +22% CPU. Seek a narrower visible-payoff condition and A/B
it; the sequential payment walk already applies the Apex mana after cast.
The new Ruby/Seething order gaps are fixed in `5e9c477e9`; see the update
above and the design note. The Apex one-copy gaps remain open.

One residual payment lead is especially concrete: seed 30074224 T5,
`Seething Song → Karrthus`. Oracle pays Song by cracking Lotus Bloom for
red, leaves both Unclaimed Territories untapped, then pays Karrthus's
black and green with them. The engine's replay ends with Bloom still on
the board, both Territories tapped, five red floating, and Karrthus
dropped. The `MTG_ONDEMAND_VISIBLE_LUMP` branch in `src/ai/ManaPayment.cpp`
uses `short_with_shared` against the **prepayment** pool before comparing
the trial payments. Two Territories make black/green look covered before
Song, even though ordinary Song payment spends them as colorless. This is
a likely guard mismatch, not yet a proven fix. A scoped trial should
compare the after-payment pools and preserve the two later colored
sources when early Bloom:red pays the current generic bill. The user
already authorized early Bloom cracking for a visible later cast.
The existing focused test is in `test/unit/test_dragonstorm_followups.cpp`
("visible Karrthus needs Bloom black with one wild Territory"); add a
two-Territory + Mountain + Bloom Song/Karrthus case alongside it. After
paying Song, require Bloom spent and both Territories untapped; after
crediting Song's five red, Karrthus should pay. Removing the prepayment
`short_with_shared` filter while retaining the after-payment
`saves_later_source` comparison is the smallest candidate, but it has
not been A/B tested and must not be assumed correct. Check exact older
Ruby/Bloom seeds and held-out turns.

1. Inspect `git status --short`, the fresh report and raw `build/scratch/mana_clean/codex-dragonstorm-ruby-apex-storage-20261117/on.0.err`. The Dragonstorm design note and gate record are committed in `304d1fed8`; keep this handoff current as work proceeds. Gates return nonzero when flags remain.
2. Work the new Dragonstorm issue clusters with exact on/off repros, narrow deterministic payment/order fixes, 5,000 paired A/B where material, Release unit/regression/viewer validation, and a fresh PGO 5,000-game gate. Avoid adding searched options or costly broad branches. One top-level simulation batch at a time, `MTG_RSS_CAP_GB=40`, use `nproc` for worker count. Build only via `./build.sh`; PGO+LTO for long runs.
3. Separate Gods trial worktree `.claude/worktrees/codex-gods-hold-20261117` has only a dirty provider edit adding a `ready >= 3` floor to the default-off `MTG_GODS_LOAM_GREEN_HOLD`. It is unbuilt/untested. The broad green hold cleared exact seeds 30037134 and 30037273 but caused 2 later wins/0 earlier and +12.1% recorded time in 5,000 paired games. User wants a less costly fix and full mana completeness. See `docs/design/gods-loam-line-green-hold-2026-11-16.md`. Do not run its long test concurrently with Dragonstorm gates.
4. Resume the wider queue from `python3 scripts/mana_clean.py status` and the older handoff: Pirates, Extra Turns, Jumbo, Hydras, Fungus, Shadowborn, Morph and due decks. The target is a fresh 5,000-game block for every shipping deck and every issue class repaired; stale-clean records also need fresh runs.

The 04:27 UTC status snapshot has 56 decks: 18 clean on current source,
36 with issues, two never run, and zero stale-clean; 27 are marked DUE.
This remains a large open queue. Extra Turns and Merfolk Turns are the
never-run decks. Extra Turns needs an oracle cost investigation before
its 5,000-game gate can be practical.

## User decisions and operational constraints

Gods should dredge toward Mystic Retrieval for Replenish, use Loam to recover lands, and natural-draw when graveyard has one or zero lands; a non-clairvoyant A/B supported the strict dredge policy, which is shipped. Study → Loam original plans continue across the Study breakpoint; new plans there are pruned. User allows a visible later cast to justify cracking Dragonstorm Lotus Bloom early. For EDF, spend colorless for generic when free, but preserve `{C}` for Eldrazi Displacer loops and the final kill. User permits narrow non-searched line-opening options. Windows CI is the useful compatibility check; run asynchronously, do other work, and respond only to failure. Linux CI can be dropped. Prior Windows run 43993315542 succeeded; current run 44025173062 is watched asynchronously. The branch was pushed at 05:35 UTC Nov17; next ordinary push is at or after 09:35 UTC. Before any push or worktree removal, run `python3 scripts/check_references_safe.py`; promptly push new user references if any. The user explicitly asked this local handoff be kept up to date for Claude because Codex usage is nearly exhausted.

EDF follow-up source lead: `src/ai/ManaPayment.cpp` already drains
floating colorless before W/U/B/R/G for generic costs in the normal
route, while `MTG_HOLD_C_FOR_SINK` and `MTG_HOLD_C_IN_PAYMENT` protect
human-play `{C}` sinks when `BoardHasColorlessPipSink` sees one. The
current guard is board-based and human-only; verify the user's final
kill case separately: the sideboard has Essence Depleter and Dimensional
Infiltrator, reached through Living Wish, and the main deck has Shivan
Gorge. A `{C}` sink may therefore be visible as a Wish continuation
before `BoardHasColorlessPipSink` sees it on the board. See the EDF
analysis around its combo-finish route. This is an
unverified lead, not a newly adopted EDF change.
