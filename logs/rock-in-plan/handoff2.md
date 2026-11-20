# rock-in-plan handoff 2 (2026-11-20, agent 2 at ~250K context)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/agent-ade0a1bc23ae96aa7`, branch `wip/rock-in-plan` (push ONLY
this branch; NEVER phase-1-2-deck-analyzer). Scratch `S=/workspaces/MagicDeckTester/build/scratch/claude_rock_plan/`.
Helper scripts (gitignored) in `logs/rock-in-plan/`. Read handoff.md first for the mechanism + original brief (verbatim).

## Done (commits on wip/rock-in-plan, NOT yet pushed since 846e0848d)

* `ce7d362a4` lever `MTG_LINE_KEEP_ROCK` (default OFF). `846e0848d` handoff 1.
* `dfc8a326e` exact CPU cut: tight joint test for a colourless rock's own payment + print-only phase stats
  (`[line-keep-phase]` under MTG_LINE_REDUCER_KEEP_STATS) + diag `MTG_LKR_TIGHT_CHECK=1` (prints `[lkr-tight-bad]` for any
  repair the tight test would have refused; must be 0).
* (this commit) ramp-filter (Izzet Signet) extension of the tight test; guard: no tight test on a board with an untapped
  ramp_filter / any_color_filter / mana_per_creature_feeder_generic (Three Tree City) source, any sac-for-mana /
  sac-outlet source, `deck_has_ondemand_sac`, or `deck_has_mana_untapper`; `[lkr-nofire]` sample print; design doc
  `docs/design/rock-in-plan-2026-11-19.md` (mostly written; needs final CPU numbers + d8 + verdict).

## Running job (the coordinator waits on it)

* Chain: `(sleep 200; flock $S/../claude_box.lock logs/rock-in-plan/run_ab3.sh)`, wrapper PID **3787422** (started
  ~00:00 UTC 11-20; it may wait a long time for the lock). DONE marker **`$S/ab3.done`**; status `$S/ab3.status`.
* run_ab3.sh: `./build.sh` (log `$S/ab3.build.log`; FAIL -> ab3.done=FAIL) -> diag probe
  `MTG_LKR_TIGHT_CHECK=1 MTG_LINE_REDUCER_KEEP_STATS=1 mtg --batch $S/manifest_probe_diag.json` (every rock deck x100 on
  seeds 690000+, plus merfolkturns o725/o975 on 660000) -> `$S/probe8d.err` -> plain stats probe
  (`manifest_probe.json`: niv/whiteknights/stuffy/angels x100 on 680000) -> `$S/probe8.err` -> IF `grep lkr-tight-bad
  probe8d.err` matches: writes "ab3 SKIPPED: tight refusals" and stops; ELSE full A/B `manifest_ab.json` (24,600 pairs,
  both arms) -> `$S/ab3.out/.err`, `$S/logs_ab3/`, table `$S/ab3.compare.txt`.

## How to judge ab3

1. `cat $S/ab3.status`. If SKIPPED: `grep lkr-tight-bad $S/probe8d.err | sed -E 's/#[0-9]+//g' | sort | uniq -c` --
   each line names the board; find the source the joint backtracker cannot tap but the repair (held / table) does, add
   it to `generic_fed_converter_up` in `LineReducerKeepHold` (ManaPayment.cpp), wait 200 s, rerun the chain (rm
   ab3.done ab3.status; mv probe8d.err aside). Previous rounds: Generator Servant (sac-for-mana), Three Tree City.
2. EXACTNESS: the cut must not change play. Every `logs_ab3/on__*.wins` must equal `logs_ab/on__*.wins` (the first
   A/B, original lever build ce7d362a4), and every `off__*` too:
   `for f in $S/logs_ab/*.wins; do cmp -s $f $S/logs_ab3/$(basename $f) || echo DIFF $f; done`. Expect NO DIFF.
   (ab2 = dfc8a326e build had 2 merfolkturns on-arm digest diffs: gi 743, 991 -- the Three Tree City hole, now guarded.)
   Also `grep line-keep-all $S/probe8.err`: fired must be 68607 (rock_fired 50451), as in probe2 (pre-cut).
3. CPU: the `cpu on/off` column of `$S/ab3.compare.txt` per deck (paired, interleaved in one batch, the trustworthy
   number). Compare with ab (first build) and ab2 (colourless cut) below. Rung time: `grep line-keep $S/probe8.err`.
4. If exact: flip default ON (`EnvOn("MTG_LINE_KEEP_ROCK", true)` in src/ai/EngineFlags.h:532 + its comment "DEFAULT
   OFF until measured" + HeuristicArm.h comment), wait 200 s, build under the lock, full units (`build/Release/mtg-test`),
   then smoke + regression from the worktree (`MTG_PGO=0 MTG_RSS_CAP_GB=40 bash test/regression.sh --smoke`, then
   regression), report per-deck changes for the parent to accept. Do NOT accept GT. Re-run the v1 repro batch
   (`logs/rock-in-plan/run_repro.sh`) for before/after flag counts only if wanted (already have them per arm).
5. Finish the design doc (CPU numbers, verdict), commit, `git fetch && git rebase origin/phase-1-2-deck-analyzer`,
   rebuild + smoke if src replayed, push `wip/rock-in-plan` only.

## Measurements so far

* Repro flags (MTG_MANA_ORACLE=2, v1.batch.err; off / KEEP_ALL / KEEP_ROCK): Stuffy APPLY-DROP 45/10/0,
  APPLY-DROP-ORDER 14/14/14 (Blasphemous Act cost-reduction order: a different family), ORACLE-GAP 3/3/3; Dragons
  APPLY-DROP 218/117/75 (Mind Stone plans 98->17, Sol Ring 59->4, Fire Diamond 18->18 enters tapped; 57 of 75 left
  are Haven of the Spirit Dragon creature-only {R}), ORACLE-GAP 152/152/152. Mechanism confirmed by trace: s3847 off
  arm `declined: producer (ritual/rock)` 26.8% of batch-prepay calls, `[line-reducer-keep] trials=0`; rock arm
  rock_fired=601 and 17 -> 0 APPLY-DROP.
* ORACLE-GAP s30000027 analysed (oracle-side, unchanged by the lever): the oracle reaches the engine's board at pay3
  but enumerates an extra `pay4 cost={1}` frontier, so every outcome taps Mind Stone#33. Source of the {1} not pinned.
* A/B ab (ce7d362a4, 24,600 pairs, seed base 660000): ALL faster 10 / slower 4 / net -6; every deck net <= 0 except
  niv +1 (3 faster, 4 slower). Table in the design doc. CPU on/off (first build): ALL 1.0315, niv 1.0883, stuffy
  1.0530, whiteknights 1.0521, extraturns 1.0300, dragons 1.0329; controls burn/goblins/alchemist ~1.00.
* ab2 (dfc8a326e colourless cut): same outcome table; CPU ALL 1.0120, niv 1.0760, stuffy 1.0126, whiteknights 1.0319,
  extraturns 1.0351, dragons 1.0154. Not exact (2 merfolkturns digests) -> fixed by the Three Tree City guard.
* Niv re-measure (2000 fresh pairs, base 670000): 17 faster, 3 slower, net -14 (pooled with ab: 20/7, net -13 / 3000).
* d8 b0 on the 4 niv slower games (65, 322, 373, 541): ALL recover, both arms identical turn and digest (5,5,4,4).
  They diverge at T1 (Ponder vs Preordain) / T3 (dig order), not on a payment: budgeted ranking noise.
* Rung phase timing (probe, 400 games on): before cut 21.7 s of 307.6 s CPU (57% = rock strands no repair keeps);
  colourless cut 13.2 s; remaining no-fire strands were Izzet Signet (handled by the ramp-filter extension).
* AccomplishedAlchemist: no untapped rock in the list -> lever inert; 100-pair control byte-identical.

## USER rulings (verbatim, from the brief)

* "NOTE: MTG_LINE_KEEP_ALL (the general widening) is NOT adopted. The USER asked to adopt it only if positive on
  average, and AccomplishedAlchemist measured net +2/30k. So the rock lever must stand on its own measurement and must
  not reintroduce LKA's AA regression; include AccomplishedAlchemist in the A/B if the lever is live there."
* "The bar: every slower game recovers at --depth 8 --budget-ms 0, and every deck net <= 0. Report CPU per deck. The
  USER says cut CPU cost; never justify it by headroom."
* "If the bar is met, flip it default ON. Then units (full), plus smoke + regression from the worktree; report
  per-deck changes for the parent to accept. Do NOT accept GT."
* "If the bar is NOT met, root-cause the slower games and report; do not ship default ON."
* Machine: flock on `build/scratch/claude_box.lock`, MTG_RSS_CAP_GB=40, whole machine inside a hold, ./build.sh only,
  wait ~200 s after edits, no `timeout`, waiters on PID / DONE marker, rebase never merge, push only wip/rock-in-plan,
  never touch references/.

## Open USER decision to surface

* Niv-Mizzet Combo measured +1 on the first 1,000 pairs (3 faster / 4 slower, all 4 slower recover at d8 b0) and -14 on
  2,000 fresh pairs; pooled -13 / 3,000. Recommendation: adopt (default ON) on the pooled measurement; the strict
  reading of "every deck net <= 0" on the FIRST sample alone is not met by niv.
