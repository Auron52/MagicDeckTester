# DMG-2 handoff: Gods dredge deck-out widening (gods reg d3 s2002 gi16)

Stood down 2026-11-20 ~21:30 UTC by the coordinator (USER 21:27: "At this point I want to wrap up Maze's
End as soon as we can." / "So having other nonsense in the way doesn't make sense."). The box is reserved
for Maze's End batch-2. Nothing of mine is running: chain2 was killed by the parent, and I killed my own
chain3 (queued, never started).

Worktree `.claude/worktrees/maze-dmg`, branch `wip/maze-dmg`, tip **fc04fac0e** (pushed; base 8fbbc5023).
Mechanism, fix and the earlier user rulings: `logs/maze/DMG1-handoff.md` (still valid).
Scratch: `/workspaces/MagicDeckTester/build/scratch/maze/dmg/`. Binary: `.claude/worktrees/maze-dmg/build/Release/mtg`
(built from fc04fac0e, rc=0). NOTE: the fix commit message still says "UNBUILT". It is now built and tested.

## Done
- Build rc=0. **Windows CI on fc04fac0e: success** (run 44695224564).
- Units: `mtg-test` all pass; the new dredge case passes; the dredge units also pass with MTG_GODS_DREDGE_DECKOUT=0.
- Repro (b0, `rp/` in scratch). Values are deck-out fix ON vs OFF:
  - forced-Magus hand-off: d4 **T5** vs loss; d5 **T5** vs loss; d8 **T5** vs loss
  - full game s2018 gi16: d3 b0 **T5** vs loss(9); d8 b0 **T5** vs T7
- Smoke (`smoke.txt`): 232 pass, 3 FAIL, all gods and all FASTER. Moved keys:
  - gods_smoke_d3_s1001: 5.4800 -> 5.4600 (digest 191a495134f27aea)
  - gods_smoke_d5_s1001: 5.4200 -> 5.3600 (digest 282413dab408af00)
  - gods2hg_smoke_d5_s1001: 5.5600 -> 5.3600 (digest dc76130d4e1b7eff)
  - Searched: 4 slower, 10 faster, 29 play-changed; d0 unchanged.
  - Slower smoke games: 2hg d5 gi17 8->loss, d3 gi7 5->6, d3 gi17 7->loss, d5 gi35 7->loss.
- `test/check_gt_logs.py`: consistent (1345, 0 stale). No GT accepted.
- **Held-out A/B COMPLETE** (seed base 7007, arms innermost and alternated; `ab/main.err`, scored by
  `ab/score.py`, copied to `logs/maze/DMG2-ab-main.score`). Values are fix ON vs OFF, unwon = 9:
  | cell | n | net turns | mean (se) | faster/slower | wins |
  |---|---|---|---|---|---|
  | gods d0 | 1000 | 0 | 0 | 0/0 | 139->139 |
  | gods d3 b10 | 600 | **-64** | -0.107 (0.021) | 65/15 | 585->582 |
  | gods d5 b20 (play) | 600 | **-86** | -0.143 (0.022) | 74/11 | 587->591 |
  | gods2hg d5 b20 | 600 | **-79** | -0.132 (0.029) | 78/18 | 577->575 |
  Per-deck net <= 0 holds for std and for 2HG. 44 slower games are listed in DMG2-ab-main.score.
- d8 b0 (fix ON) partial, from the chain2 rows: smoke d5 gi35 -> **T6** (recovers; it was T7);
  smoke d3 gi7 -> **T6** (baseline T5: NOT yet recovered. Its OFF arm at d8 b0 is unmeasured).
  The d8 rows for smoke 2hg gi17 and d3 gi17 were still running at the kill.

## Remains, in order
1. d8 b0 on every slower game, BOTH arms (an OFF arm tells "the old faster result is not findable at d8 either"
   apart from "the fix lost it"). Manifest READY: `ab/d8.json` (96 jobs = 44 A/B slower + 4 smoke slower, off/on).
   The chain also runs the info-only rule 3/5 batches (`ab/ab_r3.json`, `ab/ab_r5.json`: gods d5 + gods2hg d5,
   200 games each, deck-out ON; compare with the `on__` arm of main.err on the same gi). Relaunch, detached
   (a harness-tracked background task got SIGTERM at ~1h45m):
   `cd .claude/worktrees/maze-dmg && setsid nohup bash /workspaces/MagicDeckTester/scripts/heavy_run.sh
    /workspaces/MagicDeckTester/build/scratch/maze/dmg/chain3.sh > .../dmg/chain3.log 2>&1 < /dev/null &`
   then wait on that PID with `kill -0`. Score d8 with: `grep '\[win\]' ab/d8.err` (pair off__/on__ by name).
   Any slower game where ON at d8 b0 is worse than the OFF baseline score must be root-caused.
2. (Only if a rerun of the A/B is wanted: chain2 = `chain2.sh`, manifest `ab/ab_main.json`, same launch
   form. It is NOT needed. The A/B above is complete: all 5600 games, every job.)
3. Push wip/maze-dmg (already pushed at fc04fac0e) and report the moved keys to the batch-2 integrator.
   Do NOT accept GT on the shared branch.

## SURFACE (USER decision, not measured yet)
On the forced-Magus board, d4 b0 (DMG-1): rule 3 won T4, rule 5 won T5, and the default rule 4 lost (55 s).
With the deck-out fix, rule 4 now also wins T5 there. Which rule ships is the USER's call. The rule 3/5 batches
in chain3 provide information only.
