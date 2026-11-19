# Maze's End P5d -- P5d-3 handoff (2026-11-19 ~07:20 UTC). Predecessors: logs/maze/P5d2-handoff.md, logs/maze/P5d-handoff.md

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze-p5d`, branch `wip/maze-p5d`. Shipped-candidate code = HEAD of
the P5d commits `80f84c24b 49518ebeb 4cffb5552 46fa82d8d faf82c459 088cde51a` (088cde51a pushed to origin/wip/maze-p5d by
P5d-3; this handoff commit sits on top). Push ONLY `wip/maze-p5d`; never the shared branch. Rebase, never merge.

**THE WORKING TREE's src/ IS DIRTY WITH TEMP CODE -- NEVER COMMIT IT.** `src/ai/DecisionProviders.cpp` + `src/ai/HeuristicArm.h`
carry prune (a) and the diagnostic slots (copy: `logs/maze/P5d3-temp-idle-ev-slots.patch`, also
`build/scratch/maze/p5d/P5d3-idle-temp.patch`). build8 (below) compiles FROM this dirty tree: do not revert src until
`build/scratch/maze/p5d/build8.done` exists. Then `git checkout src/ai/DecisionProviders.cpp src/ai/HeuristicArm.h` before
building the candidate. TEMP slots (heurarm, manifest `flags`, or env): `MTG_P5D_IDLE` (prune (a)), `MTG_P5D_IDLE_MB`
(amended (a)), `MTG_P5D_EV_A/B/C1/C2/AMU1/AMUF` (eval axes, below); env `MTG_P5D_IDLELOG=1` prints each prune removal.

Scratch `build/scratch/maze/p5d/` (= `$S`). Binaries: `mtg.ab` (TEMP tree 36c56cd53: arms MTG_P5D_SHIP / _IDLE, env debug
MTG_P5D_DBG_*), `mtg.u2` (HEAD 088cde51a = ranking-only "u2"), `mtg.p3` (u2 + the TEMP slots; build8 replaces it).

## Box rules (USER, mandatory)
ONE heavy process at a time: `flock /workspaces/MagicDeckTester/build/heavy.lock <cmd>`; builds `MTG_BUILD_JOBS=12 ./build.sh`;
batches `--threads $(nproc)`, default cache, `MTG_RSS_CAP_GB=20`. d8 b0 games go in a pooled `--batch` under the lock (the
parent corrected me: xargs lanes beside the lock holder are NOT allowed; only seconds-long repros run outside the lock).
TRAP I hit twice: blind jobs of one game share a seed, and `same_seed_inflight: 1` serialises them -- a 50M-unit tail held
the lock at 2/32 busy for an hour; use same_seed_inflight 0/2 and expect blind 2HG g196 / d5 g629 to hit 50M. No timeouts;
waiters on PID or DONE markers, never pgrep -f. Context: hand off past ~250K.

## USER rulings (VERBATIM; carried from P5d2-handoff.md plus the parent's correction)
- "there is value to banking extra lands even if you have a new-name gate in hand"
- "a fetch is worth nothing only while new-name Gates in hand fill every open land drop. It ships on." How is that correct? It isn't worth nothing because it helps preserve your hand?"
- "Here is my issue with the gate-from-hand idea. Say you have 2 in hand, maze's end and exploration in play and 3 colourless. It seems like you are suggesting that the right play is to use the 2 from hand rather than take advantage of the 3 we have to preserve one for next turn. To me, that seems incorrect."
- "It's even worse if Manabond is in play, though I don't know how your rule handles that case. As a side note, you usually do not want to pass the turn without Maze's End on the field because you might need it next turn and you definitely need it to win the game."
- "Fair enough, yeah. If the maze's end is getting dropped by some effect then that would be okay."
- "I think its okay not to always fetch. Fetching costs mana and if we have a lot of land in hand (and no manabond to drop them) it can be worse than playing them out. I don't think it should be ever worse (clairvoyance aside) if we have nothing to do with the mana, though."
- "Yes, but there is a point at which we don't have enough lands in hand to keep playing them at full rate this or next turn. If that is the case it should win out over other effects."
- "Ideally all of this would be searchable, but I'm getting the impression that you see no choice but to prune heavily?"
- "We should be pruning the fetches, not just ranking them." -- then CORRECTED by the USER: "What I mean by pruning fetches is about the set of targets. Not whether to use it or not."
- "Though, if there is idle mana we should use it."
- "And that is fine, because not fetching would be dominated, but this would be assuming we can replay it."
- Parent (P5d-3 brief): prune (a) is the USER's ruling; an agent may not drop it ("A USER-OWNED DECISION IS NEVER SETTLED BY AN
  AGENT -- NOT EVEN 'LEAVE IT OFF'"; "When the user's own order/rule measures worse, ROOT-CAUSE IT and propose a fix").
  Until the USER rules: land ranking-only WITHOUT (a); keep (a) as a measured, documented PATCH under logs/maze/ (not a
  default-off lever); say so plainly. Bar: every slower game recovers at --depth 8 --budget-ms 0 (ceiling >= 50M) and per
  deck/row net <= 0 (2HG and d1b1 must be root-caused too).

## Verdicts so far
**ab2 (u2 vs ship, held-out, same seeds as ab1; `cat ab1/run.out ab2/run.out > ab12.out; python3 anab.py ab12.out ship`):**
d5 b20 x1000 -3 (46/42); 2HG x300 +1 (15/16); d3 x300 -2; d1b3 x200 -7; d1b1 x200 +2 (7/10); d0 x400 -11 (49/39).
u2 == u (ab1 ranking-only) on every searched row; d0 differs (GreedyLandFirst stand-down: d0 g237 T9 -> T6 = ship).
**d8 b0 recovery of u2's slower games (`d8b/`, `python3 and8.py ab12.out d8b/all.out ship u2`):** recovered (arm@d8 <= ship@d8)
d5 40/42, 2HG 15/16, d3 13/13, d1b3 6/6, d1b1 10/10 (so d1b1's +2 is budget noise), d0 39/39. NOT recovered: d5 g499 (5 vs 6),
d5 g635 (5 vs 6), 2HG g287 (6 vs 7) -- the same three as u. Abandoned at 50M on BOTH arms: d1b3 g4, d0 g270 (b2000 replay
queued in cmb/). 2HG's +1 needs g287 fixed.
**Attribution of the 3 (b3/, mtg.ab d8 b0):** restoring ONLY the shipped eval (MTG_P5D_DBG_SHIPEVAL) recovers all three
(5, 5, 6); NOHOLD / NOFIRST / NOTIE do not. Eval axes (`ev/run.out`, mtg.p3; each moves one axis back to ship):
EV_A (no open drop -> fetch = full 100 instead of 0) recovers g499 (6->5) and g287 (7->6); g635 stays 6 under every single
axis (A, B, C1, C2). Hypothesis being tested (AMU jobs in abb): the no-drop 0 assumes Maze's End's next-turn replay enters
tapped (an activation cycle lost), which is FALSE with Amulet of Vigor (4 in the list; g499 has two) -- AMU1 = no-drop fetch
1 while Amulet is ours, AMUF = full while Amulet is ours. g635 needs an axis combination (cmb/).
**Prune (a) losses = clairvoyance (the USER's carve-out holds).** The 9 games (d5 g81 g231 g629 g823 g892, 2HG g196 g254,
d1b3 g64, d1b1 g186). (1) No-shuffle (MTG_NO_SEARCH_SHUFFLE, `ns/`, single repros): gap closes on g81 g231 2HG g196 g254 d1b3
g64; persists on g823, g892 (g186, g629 idle arms not run). (2) BLIND ensemble (manifest blind_salt 1-4: the search does not
see unknown library order; `python3 anb3.py`): idle-u = 0 on g81 g231 g823 g892 g186 g64 (all salts), 2HG g254 idle BETTER
(-0.25), g629 tie on the salt both arms finished, 2HG g196 tie on s3 (other salts abandoned at 50M on both arms). Total -1.
Mechanism: the prune only removes plans at nodes with an untapped Maze's End (g823: 1,301 removals at T4, 30 at T5, none
at the T3 root that diverges; g892 diverges at T1 before Maze's End exists); the clairvoyant search valued the removed
"don't fetch" lines for the KNOWN draws they kept (every search shuffles deterministically, lockstep with the real game,
so fetching changes which cards the search knows it will draw). With the library unseen, (a) is never worse here.
No non-clairvoyant loss was found in the 9. One logical gap found by code review (not seen in these games): "comes back"
counts Manabond, but Manabond returns Maze's End only on a yes, and a yes DISCARDS the rest of the hand -> amended
`MTG_P5D_IDLE_MB`: Manabond counts only when P leaves no nonland card (Burgeoning stays free).

## Running / queued (all mine; waiters were in the P5d-3 session -- set your own)
- build8: bash PID 1233415 -> flock PID 1233416 (cwd maze-p5d) -> `$S/build8.log`, then `mtg.p3` atomically replaced, marker
  `$S/build8.done`; the same bash then runs `$S/run_abb.sh`.
- abb (`$S/run_abb.sh`, manifest `abb/manifest.json`, 12,006 jobs, mtg.p3, marker `abb/done`, output `abb/run.out`):
  (i) 6 d8 b0 jobs `AMU_<row>_g<g>_MTG_P5D_EV_AMU1|AMUF` on g499 g635 2HG g287 (does the Amulet tier recover them?);
  (ii) prune (a) held-out A/B on the u2 base, rows d5 b20 x1000 @201001, 2HG x300 @202001, d3 b10 x300 @203001, d1b3 x200
  @206001, d1b1 x200 @205001 (ab1 seeds; d0 excluded): arms `u2`, `idle` (a), `mb` (amended a), and the same three BLIND
  (`u2B`, `idleB`, `mbB`, blind_salt 1). Analyse: `python3 anab.py abb/run.out u2` (clairvoyant) and
  `python3 anab.py abb/run.out u2B` (blind; read only the *B arms). The USER's question is the blind columns.
- cmb (`$S/run_cmb.sh`, bash PID 1285312, marker `cmb/done`): `cmb/cmb.out` = g635 d8 b0 under every 2/3/4-axis combination
  of EV_A/B/C1/C2 (11 jobs, mtg.p3 -- whichever mtg.p3 exists when it starts; the EV slots behave the same in both);
  `cmb/b2k_ship.out` (mtg.ab + MTG_P5D_SHIP) and `cmb/b2k_u2.out` (mtg.u2) = d8 b2000 replays of d1b3 g4 and d0 g270.
- ev and b3 are DONE (`ev/done`, `b3/done`); the blind remainder was killed by me at its 50M tail (results in
  `b3/blind_rest.out`, read by anb3.py).

## Remaining, in order
1. abb AMU + cmb verdicts -> pick the eval amendment that recovers g499, g635, g287 at d8 b0 (EV_A recovers two; an Amulet
   form is the principled candidate -- it keeps the USER's "end the turn with Maze's End on the battlefield" for the no-Amulet
   case). Implement it cleanly in TapTutorPutEval (no lever), build, held-out A/B vs ship on the ab1 seeds (all six rows,
   one pooled batch; ship results from ab1/run.out are reusable: deterministic), d8 b0 recovery of every slower game, per
   row net <= 0 (2HG included).
2. abb prune verdict: report (a) and amended (a), clairvoyant and blind, to the parent FOR THE USER. Do not ship (a) on or
   off: land ranking-only without (a); save (a) + (a)-MB as a patch under logs/maze/ (P5d3-temp-idle-ev-slots.patch has
   both plus the eval slots; extract the prune part).
3. Strip TEMP (git checkout the two src files), squash the P5d commits into one, smoke + regression
   (`flock heavy.lock`, MTG_PGO=0), accept `--deck=mazesend,mazesend2hg` (only Maze keys should move),
   `python3 test/check_gt_logs.py`, push `wip/maze-p5d`.
4. Ledger `docs/design/analysis-Maze's End.md`: new "## P5d" section after P5c3/P5c4 (root cause, rule, ab1/ab2 tables, d8
   recovery, the prune (a) clairvoyance evidence above, the eval amendment, GT); refresh the comment above
   `TapTutorPutEval` with the numbers.
5. Note for P7's census owner: d0 g237 on the SHIPPED arm ran > 0.6 h at d8 b0 and was abandoned at 50M (cost outlier);
   blind 2HG g196 / d5 g629 also hit 50M at d8 b0.
