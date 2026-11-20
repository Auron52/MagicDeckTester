# IDG-2 handoff (worktree .claude/worktrees/idgate, branch wip/idgate). Written at ~215K context on the parent's request.

Read logs/idgate/IDG-handoff.md first (design, census, tooling). This file covers what IDG-2 did after that.

## USER rulings (verbatim, as given in the briefs)
- "the top priority are optimizations and fixes"
- "A budget may re-price work, never make a line unfindable"
- "Naturally, I'm expecting that this means we are matching the references and all other bars. If not, we should focus on doing so first."
- On search: "Do NOT add complexity whose purpose is to dodge a depth increase". A depth/budget-per-deck change is a SETTINGS change, so it is the USER's.
- Adoption bar: per-deck net <= 0 ("+1 is not neutral. 0 is neutral."), and every slower game recovers at `--depth 8 --budget-ms 0`.
- "Draws Diverge is not an acceptable explanation, since at d8 b0 we should still be able to find the line."
- Do NOT accept GT, do NOT push the shared branch, push wip/idgate only. Do not push scratch/idg-f7.
- Machine: one heavy job via scripts/heavy_run.sh at full nproc, MTG_RSS_CAP_GB=20, MTG_BUILD_JOBS=12. No timeouts. Waiters on a PID or a DONE marker.
- Parent 05:18 UTC: do NOT relaunch the held-out batch as-is. Find which jobs blow RSS, decide a fix (per-job cache budget, trimming, or a separate small pass; NOT a worker cap), and relaunch only after the Maze queue clears. Tell the parent if it would hold the lock for many hours.

## Commits
- 68f5cf2ad (IDG-1): the trial + census, scoped to rollout-leaf ladders (`!vl_active`).
- b75a2cc0e-amended (IDG-2, local, see `git log -2`): sr1 results, d8 attribution data, tooling (d8.py, slower.py, trialstats.py, interleave.py, resume_loop.sh), and the doc draft docs/design/id-start-gate-trial.md (Results section TODO).
- This handoff commit. All of it is pushed to wip/idgate only.

## Done
1. **Units on the scoped build (build2):** mtg-test 3716/3716 with the trial on and off (logs/idgate/unit2_*.log). Run it IN PLACE. A copy under build/scratch reads the primary checkout's cards.json, and the Sea "Sin dies" tests then fail spuriously.
2. **Smoke+regression A/B (build/scratch/idgate/sr1):** 599 cases x 2 arms, same_seed_inflight 0 (the default here). The off arm equals GT on all 599 (0 mismatches).
   90 jobs moved by digest, over 34 deck keys: antilife boars boars2hg bruna creature_giving critter elementals fivecolour gods gods2hg
   hinata hydras hydras2hg kittyv3 kittyv32hg knights martyr martyr2hg mazesend mazesend2hg merfolkturns mill morph morph2hg pd pd2hg sea
   shadowborn shadowborn2hg spirits stuffy stuffy2hg vampires vampires2hg (build/scratch/idgate/moved_keys.txt).
   Per-game (loss=9; NOTE: .wins encodes a no-win as -1): **82 faster, 6 slower, net -82 turns. No deck is net positive**: mazesend -28,
   shadowborn -19, stuffy -5, elementals/gods/gods2hg/hinata/shadowborn2hg -3, ... (logs/idgate/sr1_slower.txt, sr1_cmp.txt).
   Units x1.130, wall x1.099 (order-balanced, order factor 0.97). Trials (logs/idgate/sr1_trialstats.txt): 11,633 trials. 43.6% finish
   (367 with a better win); 6,564 abort (387 rescued). Of the refused-pass budget (96.4M units), **18.2% now goes into finished passes**
   and 50.0% into aborted trials.
3. **d8 b0 on the 6 slower sr1 games** (logs/idgate/d8_sr1/): 3 recover (gods d3 s2002 gi34, mazesend d5 s2002 gi76/gi80). 3 do NOT:
   gods_regression_d3_s2002 gi16 (off 6 / on 7 / d8b0 7), martyr_regression_d5_s2002 gi149 (5/6/6), mazesend_regression_d5_s3003 gi73 (5/6/6).
   Attribution: all three are PRE-EXISTING, and the trial is not their cause. The trial is inert at b0, and d8 b0 with MTG_ID_GATE_TRIAL=0 gives the same 7/6/6.
   - martyr gi149 and mazesend gi73: b0 gives T5 at d4 and d5 but T6 at d6, d7 and d8, with or without --ignore-play-profile. At d6 or more no `[search] Tn pass=` ladder
     lines appear at all (a different search path). At d5 b0 the ladder finds T5 at T1 (pass 4). So depth 6+ at b0 cannot find a line d5 finds: a
     depth non-monotonicity to report to the USER / route to an engine agent.
   - gods gi16: d3 b0 is a LOSS (9), d4-d8 b0 give 7, and the budgeted d3 b10 off arm gives 6. That is worse at an unlimited budget than at b10.
   - Mechanism of the slower b20 game (mazesend gi73, logs/idgate/d8_sr1/mz73_b20_t*.txt): the T1 trial aborts, and its warmed memos make T2 pass 1 cost
     10 units. The 6.0 bootstrap then admits pass 2 (est 60), which runs to 46,669 units (2.6x the 18,000 budget; T3 runs 92,474). Every pass predicts
     win=6; the off arm happens to land T5. **Follow-up worth building:** an admitted pass is bounded only by the 25x proportional ceiling
     (kOverrunBudgetMult x EffectiveLimit), so a cache-warm pass 1 makes the next estimate meaningless. Options: bootstrap on a cold-cost basis, or give
     admitted passes the same alpha x remaining ceiling. That would cut the +13% units / +10% wall.
4. **s3 check (scratch/idg-f7 = wip/idgate + F7 c7af5242a cherry-picked; the HeuristicArm.h conflict was resolved keeping both slots; worktree
   .claude/worktrees/idg-f7, NOT pushed):** Maze's End s3 from the USER's T4 position at d5 b20 gives **T5 with the trial and T6 with MTG_ID_GATE_TRIAL=0**.
   The trace shows `T4 pass=2 done win=5 cost=735` only with the trial (build/scratch/idgate/s3/). PASS.

## Held-out A/B: NOT finished (the blocker)
- Manifest: logs/idgate/ovn.json (142 jobs: overnight searched budgeted rows of the 34 moved keys, plus kittyv32hg / mazesend2hg / shadowborn2hg held-out
  clones at seeds 4004/6006, 100 games each, since those have no overnight rows). Then `mkab.py --chunk 25` -> logs/idgate/ovn_ab.json (5,692 jobs,
  71K games/arm; regenerate it: it is not committed). Projected ~34 core-h of compute, about 1-1.5 h of wall at 32 threads, if memory allowed.
- Output: build/scratch/idgate/ovn1/ (wins/ holds the finished jobs' .wins plus *.partial files; out.part*/out.loop*/err.* are each attempt's
  stdout/stderr). The finished jobs' .wins are VALID; resume with `--resume` reuses them (the partials ride on the play identity, and weight is a non-play key).
- **Problem: RSS.** Each attempt self-aborts on the RSS cap (18 GB used here; the first attempt at 20 GB hit a GLOBAL OOM at 20.8 GB anon). With the
  default ordering (profile-grouped), 32 merfolkturns d5 games end up in flight together. With logs/idgate/interleave.py (cost-proportional weights)
  it dies within about 2 min anyway. Decks in flight at the aborts (tally over err.loop*): hinata_d5_s4004 51, vampires_d5 12, martyr_d5 12,
  stuffy_d5 11, merfolkturns_d3 10, mazesend_d5 10, sea_d5 9, gods_d3 9, boars_d5 9, elementals_d5 8, fivecolour_d5 7. Hinata d5 dominates.
- **Next (parent's instruction, 05:18):** (1) replay one Hinata d5 s4004 chunk alone at full threads under `/usr/bin/time -v` (through heavy_run)
  and read peak RSS, and likewise one vampires/boars d5 chunk. Also check whether the ON arm alone is the hog: the trial runs extra passes that fill the
  game-line / rollout caches. Compare an on-only and an off-only chunk. (2) Pick the fix: a per-job cache budget, or run the heavy jobs (hinata d5,
  merfolkturns d5) in a separate small pass. NOT a worker cap. (3) Relaunch through heavy_run when the Maze queue clears; tell the parent the expected lock hold.
- Then: `python3 logs/idgate/slower.py logs/idgate/ovn_ab.json build/scratch/idgate/ovn1/wins` for the per-deck net and slower list, `ab_wall.py`
  on the concatenated out files for wall, trialstats.py on the err files (no census env in ovn, so trialstats needs MTG_IDG_CENSUS=1: it was NOT set,
  so take the conversion figures from sr1), d8.py on every slower game (`python3 logs/idgate/d8.py <slower.txt> <outdir> --par 6`; options
  --depth/--budget own|N, --env K=V, --noipp, --only, --tag), and attribute every unrecovered game with lever flips.
- Then fill in the Results section of docs/design/id-start-gate-trial.md, commit, push wip/idgate, and report.

## Running jobs
None of mine. The parent stopped the resume loop (2132040 / mtg 2135710) at 05:18 UTC. Scratch worktree .claude/worktrees/idg-f7 (branch scratch/idg-f7, local
only) is kept for the s3 re-check; it holds no references. Remove it later with scripts/safe_worktree_remove.sh.
