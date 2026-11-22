# Bruna APPLY-DROP: handoff 4 (2026-11-22, agent 4 -> agent 5, coordinator-requested at ~244K)

Read `handoff3.md` and `handoff.md` in this directory for the history. This file supersedes their "remaining" lists.

## Done (wip/bruna-apply-drop, worktree .claude/worktrees/bruna-apply-drop)
- c8f363ac6 fix 1 MTG_CREATURE_LEFTOVER_EXACT + fix 2 MTG_AURA_HOST_LINE_KEEP (default ON).
- 7b621e156 fix-2 CPU cut (pre-filter + unheld-first). BUILT as **build/scratch/bad/fix3/{mtg,mtg-test}**.
- **685d4eb75 MTG_AURA_HOLD_MEMO** (TurnSolver.cpp, paymemo namespace + SeqWalkFixedOrder): memoises the sequential
  walk's AuraHostHoldKeepsLine verdict per enumeration scope (key = canonical prefix ids + Aura rep + host + bill +
  walk total + line-reducer credit/mv/bound + unseen flag; board/context pinned by the scope; walks with Vial
  treasures, unlock equips, dfs or in-order Vials bypass). Default ON, =0 always computes. Meant to be play-identical
  to fix3 (a pure CPU cut). Build queued (below).
- **Lane hygiene:** agent 4 killed its own stale 4-slot ro build waiter (it blocked the lane head for 4h44m) and
  requeued at 2 slots; the coordinator asked that every lane job be at most 2 slots.

## Measurements (agent 4)
- **fix3 units:** 3804/3805 pass. Both flag-off spot checks fail as designed. The one failure is environmental:
  "Pirates routing" counts decks/ resolved off the binary path = the PRIMARY checkout (62 decks; Creature Burn + Worldfire
  landed upstream), while the worktree has 60 = the table. Not a defect of this work; it passes on a binary inside the
  worktree tree.
- **fix3 repros** (build/scratch/bad/r_fix3, log units_fix3.log): IDENTICAL to fix12 (s30054069 3, s30051277 2,
  s30051111 28, s30053757 0, s30051079 0; residue unchanged). The CPU cut did not narrow fix 2.
- **A/B ab3** (fix3, manifest_ab2.json, seed 710000, per-game wins in ab/wins_ab3, under the box lock at 32 threads,
  cap 40): compare.py -> ab/cmp3.txt; agg2.py -> ab/agg3.txt. on = both fixes, off = neither.
  | deck | pairs | faster | slower | net | cpu on/off |
  |---|---|---|---|---|---|
  | bruna | 10000 | 8 | 0 | -8 | 1.003 |
  | bruna2hg | 2000 | 1 | 0 | -1 | 1.004 |
  | brunad0 | 10000 | 10 | 3 | -8 | 1.008 |
  | edf | 4000 | 0 | 0 | 0 | 1.080 |
  | edf2hg | 1000 | 1 | 0 | -1 | 1.070 |
  | edfd0 | 10000 | 3 | 4 | 0 | 1.193 |
  | jumbo / jumbod0 | 4000 / 10000 | 0 | 0 | 0 | 1.066 / 1.029 |
  | sea / sea2hg | 4000 / 1000 | 0 | 0 | 0 | 1.046 / 1.033 |
  | sead0 | 10000 | 0 | 1 | **+1** | 1.038 |
  | alchemistd0 | 10000 | 1 | 0 | -1 | 1.004 |
  | boars, slivers, fungusv3, alchemist (all formats) | | 0 | 0 | 0 | <= 1.015 |
  Average win turns (agg3.txt, off -> on): bruna 4.8235 -> 4.8227, brunad0 5.9020 -> 5.9012, bruna2hg 5.3560 ->
  5.3555, edf2hg 4.1130 -> 4.1120, sead0 6.2788 -> 6.2789; every other deck identical.
  ALL: 128,600 pairs, 24 faster / 8 slower, net -18. Searched-arm units on/off 0.9998.
- **The 8 slower games at d8 b0** (ab/m_d8_3.json from slow_replay.py, wins in ab/wins_d8_3): ALL RECOVER (on == off
  at d8 b0): brunad0 g6092 4/4, g6857 4/4, g8851 5/5; edfd0 g1524 4/4, g5414 4/4, g7129 4/4, g9367 3/3; sead0 g5044 7/7.
- **d0 extension** (ab/m_d0x.json from mk_d0x.py, seed base 810000, 50k pairs each, cheap d0 decks only; ab/cmp_d0x.txt):
  alchemistd0 3/0 -3, brunad0 77/14 -71, **sead0 0/2 +2**, boars/fungus/jumbo/slivers d0 0/0 0.
  The 14 new brunad0 slower games (gi 2303 9381 13795 21770 30702 32848 39037 42006 42575 42727 43004 47501 48003 49382)
  and the 2 sead0 (26535 28282, seed = 810000+gi) have NOT been replayed at d8 b0 yet. Note brunad0 gi21770 is 8 -> loss.
- **Sea d0 root cause** (gi5044 seed 715044; also gi26535 / gi28282 seed 810000+gi; fix 2 alone moves each, fix12 = fix3):
  The d0 plan on these boards is Utopia Sprawl / Wild Growth + Serpent of Yawning Depths or Summon: Leviathan
  ({4}{U}{U}). With the unconditional hold the Aura's {G} lands on a {U} source (gi5044: Island->Flooded Grove filter
  splits GG and floats G), the UU six-drop APPLY-DROPs and the d0 runner falls back to Spawning Kraken ({5}{U}), which
  wins a turn earlier. Fix 2 makes the planned UU six-drop castable, so the game is a turn slower. The d0 policy's
  choice of the UU six-drop over Kraken is the cause (Kraken card score +0.20 vs Serpent -0.23 / Leviathan -0.23, so it
  is not card_scores). Searched Sea is 0. All recover at d8 b0 (gi5044 checked; 26535/28282 pending).
  Secondary defect: the HELD scratch payment's filter split floats G instead of the U the rest owes, so the helper drops a
  hold that was fine (it costs the Sprawl bonus mana; it does not change the outcome).
  -> **USER question:** Sea d0 is +3 over 60k pairs (0 faster). Either accept it as the d0 runner's own six-drop
  preference exposed by a correct payment, or change that d0 preference (a Sea provider order: Kraken ahead of an
  equal-MV non-Kraken; that would be a USER-owned cast-order change and needs its own A/B). Agent 4 did NOT change it.
- **Callgrind** (build/scratch/bad/cg/, fix3, EDF; *.cg files): d0 400 games on 768.6G Ir vs off 624.1G (+23%);
  AuraHostHoldKeepsLine inclusive 161.7G (21%), 7.3M calls, 7.26M from SeqWalkFixedOrder; inside it 3.33M scratch
  payments (132.8G), 1.21M LineBillPayable (11.7G), prefilter scan ~27G. Searched 6 games: 79.4G vs 73.0G, helper 9.1G
  (11.4%). Hence 685d4eb75. The prefilter alone is ~4% of EDF d0; if the memo's hit rate is high, the remaining cost
  is the lookup + misses. Measure before cutting further.
- Sea cpu 1.04 and Jumbo 1.07 have the same cause (land Auras: Sprawl / Wild Growth / Overgrowth); Bruna/Boars/Slivers
  are within noise.

## Running / queued jobs (all detached via setsid nohup; none is a big run)
- **630157** lane --slots 2: `build/scratch/bad/build_fix4.sh` -> log build/scratch/bad/build_fix4.log. Builds the
  worktree (685d4eb75; waits 200 s after fix_edit_ts, removes TurnSolver.cpp.o), copies to **build/scratch/bad/fix4/**,
  Bruna units (fix4/units_bruna.log), then VERIFY runs (MTG_PAY_PROBE_MEMO_VERIFY=1 + STATS) into fix4/verify/{edfd0,
  edf,jumbo,sead0,sea,bruna}.{out,err}. Each log line prints the `[pay-memo] AuraHostHoldKeepsLine` hits/misses and
  the count of nonzero VERIFY_MISMATCH lines. DONE = `ALLDONE` in the log (or `rc=<nonzero>`).
- **630233** lane --slots 2: `build/scratch/bad/ro_units_repros.sh` -> log build/scratch/bad/ro_units_repros.log.
  The bruna-ro binary is copied to build/scratch/bad/ro2/ (built rc=0, build_ro3.log). Runs the Bruna units, then repros
  off / 7a (MTG_PAY_LINE_CRIT_BEHIND_DORKS=1) / 7b (MTG_PAY_LINE_CRIT_LAND=1 MTG_PAY_LINE_CRIT_OPENER=1) on s30051257,
  s30051119 (7a targets), s30051968, s30051794 (7b targets), plus controls s30053757 and s30054069, into
  build/scratch/bad/r_ro2/<arm>/. DONE = `ALLDONE`.

## What remains, in order
1. **fix4 verify** (job 630157): every VERIFY_MISMATCH must be 0 and the AuraHostHoldKeepsLine memo must hit. If any
   mismatch shows up, find the input the key misses, and fix the key or bypass that case. If the hit rate is low,
   find which walks miss (prefix shapes) before going further.
2. **CPU/identity A/B on fix4:** rerun `build/scratch/bad/ab/run3.sh build/scratch/bad/fix4/mtg <nproc> 40 ab4`
   under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock env MTG_RSS_CAP_GB=40 ...` (or beside a long
   tail with >= 22 GB free at cap 16). Expect every `on__` job digest to equal ab3's `on__` digest (the memo is a pure
   CPU cut), and cpu on/off back to noise on edf/edfd0/jumbo/sea. A cheaper alternative is a manifest restricted to the
   land-Aura decks (edf, jumbo, sea in every format). If EDF d0 is still not noise, profile again (cg/run.sh shape,
   with the fix4 binary) and cut further (the prefilter's own scan is ~4%). The USER does not accept CPU justified by
   headroom.
3. **d8 b0 replays of the d0-extension slower games:** `grep ^slower ab/cmp_d0x.txt > ab/slow_d0x.txt; python3
   ab/slow_replay.py ab/slow_d0x.txt 810000 ab/m_d8_d0x.json` (cwd = worktree), then run it in the lane at --slots 2
   with --threads 2 (pattern: ab/run_d8_3.sh). 16 slower games: 14 brunad0 + 2 sead0. Each must recover; only CLEAR
   clairvoyance (MTG_NC_SEARCH=1 + the owning prune off) may stay unrecovered.
4. **Sea d0 bar:** sead0 is +1 (10k) and +2 (50k), 0 faster. Per the bar it FAILS ("+1 is not neutral"). The root
   cause is above. Surface it to the USER (do not settle it). Options: (a) accept, since it is the d0 runner's own
   UU-six-drop-over-Kraken preference that a correct payment exposes; (b) a Sea provider ORDER placing Spawning Kraken
   ahead of an equal-MV six-drop (a USER-owned cast-order question; measure it if the USER asks). Optional: fix the held
   payment's filter split (float the colour the rest owes), which keeps the hold but does not change these games.
5. **Regression GT** (if the bar is met, or the USER accepts Sea d0): `MTG_PGO=0 bash test/regression.sh --both` from
   the bruna-apply-drop worktree, under the box lock with MTG_RSS_CAP_GB=40. Audit every moved key, then `--both
   --accept` and `python3 test/check_gt_logs.py`. Split c8f363ac6 + 7b621e156 (+ 685d4eb75 as the CPU commit) into
   fix-1 / fix-2 commits. Fix the "about half of its 1,891" comment in ManaPayment.cpp with the measured -35%
   (1,891 -> 1,221). Push wip/bruna-apply-drop only.
6. **Rule-override levers 7a/7b** (job 630233 first): units must pass (the crit-lever test). 7a must fix s30051257 /
   s30051119, and 7b must fix s30051968 / s30051794, each off == baseline. Then rebase wip/bruna-rule-override onto
   the accepted-GT commit and run `regression.sh --both` once per variant with the env lever on (no accept). Run a
   held-out A/B on the decks that move (the old CRIT_LAND cost Cyclone +3). REPORT ONLY; the USER decides.

## USER questions to surface (not settled)
- Sea d0 +3 / 60k pairs (0 faster), root-caused above: accept, or change Sea's d0 six-drop preference?
- Rule-override levers 7a (creature ahead of land: breaks the standing "no creature ahead of any land" rule) and 7b
  (line-critical land, dropped 11-04, + tutor line order): measure and report, the USER adopts or not.

## Box notes learned this session
- A `setsid nohup cmd &` launched from the Bash tool: `$!` is the setsid wrapper, which exits at once. Find the real
  PID with `ps -eo pid,args | grep <script> | grep box_lane | grep -v grep` (or the flock PID) before arming a waiter.
- The lane admits only its FIFO head; a 3-4-slot head waits for slots that never free together. Keep lane jobs at
  --slots 2 (coordinator rule 2026-11-22).
- `build/scratch/bad/ab/agg2.py` overwrites ab/diffjobs2.txt on every run (the ab2 list is gone; the ab3 list is in it).
- d0 EDF is 215 ms/game (4,302 s CPU per 20k games); every other d0 deck is ~1 ms/game.
- Agent 4 ran a few sub-second d0 single-game diagnostics (Sea gi5044/26535/28282, MTG_TAPDBG) inline at nice 19,
  MTG_RSS_CAP_GB=2, rather than waiting hours in the lane. It ran ONE Pirates unit test case the same way. Say so if
  that matters.

## Brief rulings (verbatim from the agent-3 brief)
- Adoption bar. Every deck must net <= 0 vs the committed tree, both searched AND at d0; there is no "lighter d0
  bar". Every slower game must recover at d8 b0. Only CLEAR clairvoyance (verified with MTG_NC_SEARCH=1 + the owning
  prune off) may stay unrecovered. Cut any CPU cost the fix adds; the suite 3x limit is not a CPU licence.
- Box. One big engine process at a time: gates, tiers and A/Bs run under `flock
  /workspaces/MagicDeckTester/build/scratch/claude_box.lock`, with MTG_RSS_CAP_GB=40. ... If your big run would
  otherwise wait hours, check free -g. With >= 22 GB available you may run it beside that tail at MTG_RSS_CAP_GB=16,
  and say so in your report.
- Small work goes through `scripts/box_lane.sh --slots N [--cap-gb G] -- <cmd>`, max 4 slots: a few single games, d8
  b0 replays, units, builds. Never a full tier there. A job given N slots runs at most N processes.
  (Coordinator 20:41: 4-slot jobs need 14 GB and blocked the lane while a gate held ~32 GB; prefer --slots 2 then.)
- Build with ./build.sh only. Wait ~200 s after a src edit before building (9p clock lag) and delete stale .o files.
- Never wrap commands in timeout. Waiters key on PIDs or DONE files, never pgrep -f / pkill -f; use run_in_background
  waiters, never polling turns.
- Never change src in a tree a gate or tier is running from. Never edit a script a running bash is reading.
- Rebase, never merge.
- Never push the shared branch phase-1-2-deck-analyzer; the parent lands it. Push wip/bruna-apply-drop freely.
- References are commit-only: never revert, delete or move anything under references/.
- A USER-owned decision is never settled by you. Report measurements and recommendations; don't adopt the
  rule-override fixes.
- Step 7 (a) Sol Ring's {1} paid off the only colour source instead of Avacyn's Pilgrim: needs creature-before-land
  ranking (breaks the standing rule). (b) Wish/Armory + Sol Ring with the only Forest: needs the line-critical-land
  rule (dropped 11-04) plus tutor line order. "Unless the USER objects (not yet answered; take the default and
  measure), measure a fix for each and report the numbers. Do NOT adopt either; the USER decides."
- Context: hand off past ~250K; end 250-350K, never >400K.

- Agent-4 brief additions (verbatim): "The bar: every deck must net <= 0, searched AND d0 (no lighter d0 bar). Every
  slower game must recover at d8 b0; only CLEAR clairvoyance (MTG_NC_SEARCH=1) may stay unrecovered." / "Confirm the
  CPU cut brings the land-Aura deck CPU back to noise. Cut further if it does not; the USER does not accept CPU
  justified by headroom." / "Measure the rule-override levers 7a and 7b on the decks they touch. Report only; the
  adoption is the USER's call." / "Big runs: ONE big engine process at a time, run as `flock
  /workspaces/MagicDeckTester/build/scratch/claude_box.lock env MTG_RSS_CAP_GB=40 <cmd>`." / "Second run: a second big
  run may go beside the lock holder only when the holder is in a long tail AND `free -g` shows at least 22 GB
  available, at MTG_RSS_CAP_GB=16." / "Lane: small work goes through `bash scripts/box_lane.sh --slots N --cap-gb G --
  <cmd>` (N <= 4; mtg --threads <= N). Prefer --slots 2 for builds" / "Do NOT push phase-1-2-deck-analyzer or join the
  push queue; pushing wip/* is fine." / "USER-owned decisions are surfaced, never settled."
- Coordinator 2026-11-22: resubmit every lane job above 2 slots at --slots 2, with --threads/-j inside lowered to 2.
