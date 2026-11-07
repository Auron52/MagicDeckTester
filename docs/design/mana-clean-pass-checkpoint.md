# Checkpoint: mana-clean per-deck pass (pass 1) and what follows it

Shared state for whoever picks this up (Claude or Codex). Last updated 2026-11-06 ~12:50 UTC.
Related: `docs/design/mana-clean-followups-2026-11-04.md` (USER rulings, queued fixes; sections 7-8 are 11-06),
`docs/design/mana-oracle-memory-time.md` (the oracle memory + time fixes), `scripts/mana_clean.py`.

## Standing program (USER)
Analyse every deck with the mana-clean analyzer (`scripts/mana_clean.py`), fix every issue, mark each deck. A deck
leaves beta only after a clean analysis of >= 5,000 games on a FRESH seed block after its fixes. Fixes must be
mechanism-level, lossless, and respect NO-GREEDY (CLAUDE.md). Rules in force: ONE engine job at a time (USER 11-04,
after an out-of-memory container kill; check `free -g`; `MTG_RSS_CAP_GB=40` is per process); no `timeout`; wait on
PIDs with `kill -0`, never `pgrep -f`/`pkill -f`; rebase, never merge; `python3 scripts/check_references_safe.py`
before every push. "Feel free to delay any iterations that seem to be getting stuck for further discussion" (USER).

## What is running (all scratch on the build volume)
1. **Pass driver** `build/scratch/mana_clean/pass1/drive2.sh`, PID in `pass1/driver2.pid`. Queue `pass1/QUEUE2`
   (consumed from the top), log `pass1/status.log` (START / END / DEFERRED / DONE2), per-deck log `pass1/<deck>.log`,
   reports `logs/mana_clean/pass1-<deck>[-rN]/report.md`. `touch pass1/PAUSE2` stops it before the next deck.
   Snow END 09:45Z (7,000 games, 27,293 issues). Niv DEFERRED 3x at 10:38Z (sizing probe 63/64, gi 47 stalled again, same as 11-04; added to stall.json). LFE DEFERRED 12:37Z (4 games ~2 h, all inside the oracle: followups s9). DONE2 12:37Z; rescored records committed + pushed (4c0ace610). Stall diagnosis: with the analyzer OFF all 5 stall games (EDF 19 61, AA 29 59, Niv 47) finish in 7 s total -> EVERY stall is analyzer-side (followups s9). The ON arm (`postpass/stall.o2.*`) + sampler are left running only to collect oracle stacks per deck; they are my own runs, safe to kill by the PIDs of the `mtg` under chain.pid and `sampler.pid`.
   Each `mana_clean.py run` calls `pgo_ensure` at its start, so Niv will first rebuild build/PGO on the current
   `HEAD:src` (7bd2b9ac28ff; the pass so far ran on 2878388e7f18). Each record names its src.
2. **Post-pass chain** `build/scratch/mana_clean/postpass/chain.sh`, PID in `postpass/chain.pid`, log
   `postpass/status.log`. Waits for the driver to exit; if the last status line is DONE2 it runs
   `mana_clean.py rescore` and COMMITS `test/mana_clean.json` (not pushed: push it after the references check +
   `git pull --rebase`). Then the EDF / Alchemist stall diagnosis: `postpass/stall.json` (EDF gi 19, 61; Niv gi 47;
   Alchemist gi 29, 59; seed 30000000+gi) as one batch with `MTG_MANA_ORACLE=0` (`stall.o0.*`), then `=2`
   (`stall.o2.*`), under `/usr/bin/time -v`. Reading: if the OFF arm is slow too, the stall is play-side (a search
   blow-up, not the analyzer); if only ON is slow, it is another oracle path.
   `postpass/sampler.sh` (PID in `sampler.pid`) takes gdb stacks + a 60 s `perf` profile of each running stall-batch
   mtg every ~5 min into `postpass/samples/a<arm>-<time>.{bt,perf}`. USER 11-06: sort out the stalls so no deck can
   get indefinitely stuck (suspects the analyzer, which has had little optimisation).

## Pass verdicts (this segment; earlier ones are in test/mana_clean.json and status.log)
- galadriel: 71,000 games, 372,073 issues (MISS 453 = 425 APPLY-DROP + 28 ENUM-GAP "cast both Galadriels",
  ORACLE-GAP 6, UNMODELED 357,908 mostly "scripted payment did not pay {1}{U}{G}" / "set-aside exceeds the floating
  pool"). First complete run: the oracle time fix c1cba3e87 took it from a stall to ~1 h.
- selesnyalifegain: 97,000 games, 197,104 issues. Also first complete run.
- DEFERRED 3x (in `pass1/DEFERRED` with repros): eldrazidisplacerflicker (gi 19, 61), accomplishedalchemist
  (gi 29, 59). NOT cured by the oracle time fix -> the post-pass diagnosis above.
- Triage of finished decks' issues is PARKED (budget); route them per deck to fix work next week.

## s9 work in progress (2026-11-06 ~23:00Z, UNCOMMITTED in the primary checkout; survived a power cut)
- Stall ON arm finished 3 of 5 before I killed it: EDF g61 3.0 h, Niv g47 1.1 h, AA g59 7.6 h (digests == OFF arm, so play
  is unaffected; pure analyzer cost). Each stall is ONE decision (EDF T3 m1 240 plans, AA T4/T6, Niv T5 3,188 plans).
- Not cache thrash: the bp continuation memo had clears=0 on g61 and a 16 GB plan-cache budget changed nothing.
- An APPLY cap cannot bound it: g61 T3 made 60k applies in 34 s, then the next 140k took ~97 min (later applies reach
  new breakpoint boards that each derive a huge continuation list). So the per-decision budget is in PRICED WORK
  (`workctr::t_priced`, deterministic): `MTG_MANA_ORACLE_DECISION_S` (default 300 priced s, 0 = off). Past it every
  remaining verdict of the decision is `CAPPED-DECISION` (new class, FAILS mana-clean like CAPPED; scripts updated).
  `MTG_MANA_ORACLE_HEAVY=<applies>` prints `DECISION-COST` lines (sizing).
- Next: verify on g61 + the 5-game stall.json, commit, rerun the 4 deferred decks. The continuation-menu memo is moot
  (the engine's own bp memo already serves it); the real per-node cost is the derivation on NEW boards.

## 2026-11-07
- s9 fix COMMITTED 94bfbd7b1. Pass driver `pass1/drive2.sh` relaunched (PID `pass1/driver3.pid`) on EDF, AA, Niv, LFE.
- Queued behind it: LINE_REPAY funnel + price measurement (`build/scratch/mcdragons/lrprice/chain.sh`, DONE marker
  there), per the USER's 11-07 price ruling (followups s7a).
- Hinata gi27 (c-wild) dispositioned recoverable by the USER's 11-07 ruling (followups s10): c-wild is clear to land.

## After the chain (in order, one engine job at a time)
1. Read the stall diagnosis (LFE already shown ANALYZER-side, followups s9); build the s9 fix (memo + per-decision cap), rerun the 4 deferred decks.
2. Hinata2 regression game 27 (seed 3003, gi 27) at `--depth 8 --budget-ms 0` on the `fix/c-wild` binary (old
   manifest `build/scratch/cwild/regslow/new.json`; the 11-04 run was lost to the OOM kill). Wins on T8 -> no
   ruling needed; otherwise ask the USER (accept as budget churn vs root-cause). c-wild is parked on `fix/c-wild`.
3. Mirrorwing g662 (seed 73000662), followups s8 Q25a: replay base vs `wip/mc-mirrorwing`, plain and
   `MTG_NC_SEARCH=1`, with game logs; show the USER where the base's T4 win depends on the draw order.
4. Fix work (followups s7-s8): Dragonstorm 2HG gi37 root cause (hold must only order spending, never make a payable
   line look unpayable); Mirrorwing H1 + H2 (build both, A/B); port `ShuffleUnseen` (cc3b47aea) onto main's
   `m_known_top`, fix the non-clairvoyant search to keep known cards (Niv gi887, seed 1650001), switch `EmitEvalRows`
   labels; LINE_REPAY (`wip/mc-dragons` 06d079365): fix the land-Aura host-hold find mismatch, price 12 + price pass 1
   (default, measured), fresh EDF A/B.
5. Remove the worktree `.claude/worktrees/oracle-time` with `scripts/safe_worktree_remove.sh` (content is pushed).

## Still open for the USER
- Two unit failures on the branch are another session's: `test_landfall` fetchland (Beast count 1 != 2) and
  `test_pirates_provider` (deck folders 47 != 46, after the RW Burn deck).
