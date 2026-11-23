# Prepay floating-{C} fix (MTG_PREPAY_FLOAT_C_PHYS) -- handoff for a fresh (Codex) agent

Repo rules: read /workspaces/MagicDeckTester/CLAUDE.md first (box lock, no timeout, no merge, push wip branch only).
Prior root-cause notes: /workspaces/MagicDeckTester/logs/hinata-gi548/handoff.md.

## Done
- Worktree /workspaces/MagicDeckTester/.claude/worktrees/hinata-gi548, branch wip/hinata-gi548, rebased onto
  origin/phase-1-2-deck-analyzer (94cd1e06b). Commits: 58690a9f6 (fix, default OFF + MTG_PREPAY_C_TRACE diag),
  628fcc728 (flag DEFAULT ON: `EnvOn("MTG_PREPAY_FLOAT_C_PHYS", true)`; `=0` = old illegal booking, A/B lever only).
  src tree a7c19c1db77c64aaa2b8e3abf56cc81232042caf.

## Running
- Step 1 DONE (chain1 rc=1 = keys moved, expected): tiers smoke+regression at 628fcc728, log
  build/scratch/claude_prepayc/regr.log. Moved (all digest-only except one): hinata smoke d3/d5, hinata regr d5 s2002/s3003,
  hinata2hg smoke d5, hinata2hg regr d5 s2002 5.4133 -> 5.4000 (faster), edf smoke d0, niv regr d0/d5 s2002/d5 s3003,
  niv2hg regr d5 s2002. Tier audit: searched slower=0 (smoke 0 faster, regr 1 faster), d0 slower=0.
- Step 2 RUNNING (first launch failed: flag was not a per-job lever; fixed in 26d1a31e8 = heurarm slot PREPAY_FLOAT_C_PHYS; run.sh now rebuilds -> mtg.fix2 first): A/B batch build/scratch/claude_prepayc/ab1/ (run.sh, PID 1078627, waits on box lock; status file
  `status`; DONE marker `DONE` holds rc; out ab.out/ab.err; per-job .wins/.units in ab1/wins). Binary mtg.fix (=628fcc728).
  Shapes (seed 81000, held out): hinata d3b10/d5b30/2hg-d5b30 x2000, hinata d0 x5000; niv d3b10/d5b20/2hg-d5b20 x2000,
  niv d0 x5000; edf d3b10/d5b20 x2000, edf d0 x5000. Jobs `<deck>_<shape>__on|off`.
  Report: `python3 test/paired_arms.py build/scratch/claude_prepayc/ab1/wins --base off --arm on --list-moved`.
## Next (in order)
1. Read regr.log / test/results to find which tier keys moved (smoke + regression vs GT at 94cd1e06b).
2. Held-out A/B on the decks that moved: ONE pooled `mtg --batch` manifest, binary mtg.fix, arms
   flags {"MTG_PREPAY_FLOAT_C_PHYS": true} vs false, held-out seeds (not 2002/3003 or overnight seeds; prior agent used
   63003 for Hinata), job names `<deck>__on`/`<deck>__off`, `--game-log-dir`, MTG_DUMP_UNITS=1; compare with
   `python3 test/paired_arms.py <wins_dir> --base off --arm on`. Run under the box lock with MTG_RSS_CAP_GB=40,
   `--threads $(nproc)`. Example manifest shape: build/scratch/claude_maze_summit/ab.json.
   Searched shapes at each deck's tier depth/budget (test/regression_cases.sh rows: "deck depth seed games budget"),
   **1,000-2,000 games per searched shape**, plus d0.
3. Bar: every deck aggregate net <= 0 turns vs off; every SLOWER searched game recovers at `--depth 8 --budget-ms 0`
   (escalate 4x/16x/64x budget if b0 can't settle); only CLEAR clairvoyance (MTG_NC_SEARCH=1) may stay unrecovered.
   Report CPU (core-s / units) ON vs OFF; quantify extra units where the illegal kill used to stop search.
   Single-game replays go in the lane: `bash scripts/box_lane.sh --slots 2 --cap-gb 8 -- <cmd>`.
4. If bar passes: `test/regression.sh --both --accept="<one-line note>"` (check how existing notes are worded;
   carry still-binding notes), `python3 test/check_gt_logs.py`, `python3 scripts/check_references_safe.py`,
   commit, push wip/hinata-gi548 ONLY. If it fails: root-cause slower games, report, don't accept.
5. Optional: diagnose sim/real mismatch (sim attempts Soulfire follow-up cast the real game skips).

## USER rulings (verbatim, via coordinator)
- "I'm more concerned about areas that help searched." -> size searched shapes 1,000-2,000 games per shape on moved
  decks; report searched per-deck result FIRST, then d0, then CPU. d0 still has to pass the every-deck bar.
- USER 12:35: Claude usage is at 5%, so keep running until it runs out, but keep a Codex handoff current.

## Measurements so far
- (prior agent) gi548 s63003 d8 b0: ON -> T5 legal under d5N/d5E0/d4E (~390K units); OFF byte-identical to base (T6).

## A/B RESULT (ab1 DONE rc=0, 2026-11-23 00:51 UTC, 55 min wall, 32/32 busy) -- seed 81000 held out
Searched (paired, on vs off; loss=9):
  hinata d3   2000: +0.0000 (4 better / 4 worse)   units 1.0018
  hinata d5   2000: -0.0015 (5 / 2)                units 1.0015
  hinata 2hg  2000: -0.0040 (10 / 2, p=0.039)      units 1.0041
  niv d3      2000: +0.0010 (0 / 2)  FAILS net<=0  units 1.0005
  niv d5      2000: +0.0005 (0 / 1)  FAILS         units 1.0004
  niv 2hg d5  2000: +0.0010 (0 / 2)  FAILS         units 0.9998
  edf d3/d5   2000: identical (digests equal)
d0 (5000 each): hinata, niv, edf all +0.0000 (niv d0 digest differs, scores identical).
Slower games: hinata2hg 160,746; hinata d3 518,746,1152,1979; hinata d5 518,746; niv2hg 86,207; niv d3 207,1492; niv d5 207
(seed = 81000+gi, game_index = gi).
## Running (2026-11-23)
- Lane job: d8 b0 (on+off) + 4x/16x/64x (on) for every slower game: manifest build/scratch/claude_prepayc/sg/slow.json,
  box_lane PID 1203910, out sg/slow.out, wins sg/sloww, DONE marker sg/slow.DONE.
- Traces (niv d5 gi207, hinata d5 gi518, on/off): sg/trace.json -> sg/traces (lane, foreground-ish).
Next: check whether OFF's niv gi207 T3 kill used an illegal {C}-as-coloured payment (then the "slower" is the
illegal win removed, not a regression); classify each slower game via d8b0/escalation; accept or root-cause.
## Slower-game findings so far (traces sg/traces)
- niv d5 gi207 (3->4): OFF's realised T3 is ILLEGAL. T3 board Island, Saprazzan Skerry, Sol Ring, Izzet Signet, Generator
  Servant ({T},sac: {C}{C}); no land drop. OFF casts Ponder {U} + Niv-Mizzet, the Firemind {2}{U}{U}{R}{R} + Curiosity {U}:
  needs {R}{R}, but the only red source is the Signet (one {R}). Colourless paid a red pip = exactly the bug. ON plays a
  legal T4. So that slower game is the illegal win removed (same gi in niv d3/2hg presumably).
- hinata d5 gi518 (5->6): diverges T3 (ON plays Mountain + one Preordain; OFF plays Preordain, Orchard, Preordain).
  Classification pending d8b0/escalation (sg/slow.json, lane, started 14:17 UTC 11-23 after 14 h lane queue).
## d8 b0 / escalation results (sg/slow.out, DONE 11-23)  off->on shipped; ON at x4/x16/x64 ; d8b0 off/on
- RECOVER: hinata2hg gi160 6->7 (esc 6; d8b0 5/5); hinata2hg gi746 5->6 (x4 5); hinata d3 gi1152 4->5 (x4 4);
  hinata d3 gi746 5->6 (esc 6, d8b0 ON 5); hinata d5 gi746 5->6 (x4 5); niv d3 gi1492 6->7 (x4 6; d8b0 6/6).
- NOT RECOVERED: hinata gi518 (d3,d5) 5->6 (esc 6; d8b0 off 5 / on 6); hinata d3 gi1979 4->5 (esc 5; d8b0 4/5);
  niv2hg gi86 4->5 (esc 5; d8b0 4/5); niv gi207 3->4 everywhere (d8b0 3/4) -- OFF's T3 PROVEN ILLEGAL (see above).
- Probe running: OFF executor MTG_PREPAY_C_TRACE on h518 (T5), h1979 d3 (T4), niv2hg gi86 (T4): sg/pc/run.sh via lane,
  DONE marker sg/pc/DONE, traces sg/pc/tr, stderr t5.err / t4.err. Goal: show OFF's realised win used a floating {C} as wild.
## Legality audit of the 4 unrecovered games (OFF arm realised wins; sg/pc traces)
- niv gi207 T3: needs {R}{R}, only 1 red source -> ILLEGAL (airtight).
- niv2hg gi86 T4: Niv-Mizzet, Parun {U}{U}{U}{R}{R}{R} + Generator Servant {1}{R} = 7 coloured pips; coloured sources
  Spirebluff, Sulfur Falls, Island, Ferrous Lake = 4 (Sol Ring, Servant colourless) -> ILLEGAL (airtight).
- hinata d3 gi1979 T4: coloured pips 9 (RS 2, Soulfire 3, Ponder 1, Soulfire 3) vs coloured supply <= 7 (Monastery,
  2 Orchard, re-untapped once by RS, + Reflecting Pool) -> ILLEGAL (pip count).
- hinata gi518 T5: total demand ~31 vs supply ~28 by pip count, and the executor's final prepay trace shows the bug firing
  (float0 c1, pure_c=2, c_keep=1 wild=2: a floating {C} relaunched as wild) -> ILLEGAL (strong, not airtight).
## Verdict (step 3): bar FAILS STRICTLY only on niv d3 (+0.0010 raw; +0.0005 = gi1492 6->7 after excluding the illegal
  gi207), and gi1492 recovers at x4 (churn). Every other unrecovered slower game is an illegal OFF win removed.
  Per brief, NOT accepted; reported to parent with recommendation to accept as a rules-correctness fix.
## CPU: units ON/OFF hinata d3 1.0018, d5 1.0015, 2hg 1.0041; niv ~1.000; edf identical. Searched core-s 44,575 vs
  43,607 (1.022x; noise ~+-5% per row: edf d3 identical play read +3.4%). d8b0 units where the illegal kill had stopped the
  search: niv gi207 32.7K->683K (20.8x), niv2hg gi86 18.1K->338K (18.6x), hinata gi1979 33.9K->339K (10x), gi518 31.3K->162K (5.2x).
## Remaining: GT accept (if parent/USER approves): `MTG_PGO=0 bash test/regression.sh --both --accept="<note>"` from the
  hinata-gi548 worktree -- the last --both run (chain1) was at 628fcc728; 26d1a31e8 only adds the heurarm slot (env-default
  identical), but re-run --both before accepting to be safe. Then check_gt_logs.py, check_references_safe.py, push wip.
  Step 5 (Soulfire follow-up sim/real mismatch) NOT started.

## APPROVED by coordinator 11-23 (accept as rules-correctness fix)
- chain2 RUNNING: build/scratch/claude_prepayc/chain2.sh PID 1572902 (box lock -> build -> --both; log regr2.log; DONE marker chain2.DONE).
- Then: --both --accept-with-regressions note, check_gt_logs, check_references_safe, push wip.
