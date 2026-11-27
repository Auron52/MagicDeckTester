# MTG_ATTACK_LORD_CAST -- handoff 2 (2026-11-27 ~12:45 UTC)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/elves-lordcast`, branch `wip/elves-lordcast` (pushed; wip only,
never the shared branch). Scratch `S=/workspaces/MagicDeckTester/build/scratch/elves-lordcast`. Predecessor handoff:
`logs/elves/lordcast-handoff.md` (Q1 mechanisms A/B/C, Q2 Brigade 814).

## USER rulings (verbatim)
- 2026-11-27, on MTG_ATTACK_LORD_CAST: "I would leave it off for now, but we probably should figure out why it doesn't work."
- 2026-11-27, on Brigade 814: "Why would you play 2 Galadhrim Brigades when you could play one and Squad it? I guess the
  only case I can think of would be if we played it for three and gained mana as a result, but it seems to me like the
  other option would almost always win out."
- (round 1) "I don't know what this is, but I suppose we could try it."
So the lever stays DEFAULT OFF. Deliverable: mechanism + amended version + measurement.

## Commits (this session, on top of 184a7cb71 / db88f5194)
- `aef990034` amendment D; delete MTG_LORDCAST_VERIFY (inert) + the gi663 print-only diags (MTG_LORDCAST_DIAG_TURN,
  MTG_LORDCAST_SOLVE_DIAG_TURN); [fsw] traces print the kick count (#xK); temporary attribution mask; helper scripts.
- `82f9d2a91` delete the temporary MTG_LORDCAST_AMEND_DIAG mask (A-D unconditional under the lever); lever reader comment
  names A-D + the USER ruling; run_ab3.sh on FRESH seeds (BASE 7700000).
- (this file) handoff 2.
Final binary = build/Release/mtg of 82f9d2a91 (snapshot `$S/mtg.r5`). Unit tests 4330/4330 pass (aef990034 tree; 82f9d2a91
differs only by the mask, and r1 on mtg.r5 vs the mask-15 binary: 1140/1140 digests identical).

## The amended lever (all under MTG_ATTACK_LORD_CAST=1 + MTG_LORDCAST_HAND=1; off arm byte-identical)
- A ProjectPlanWalk: a creature the pre-plan attack intent HELD whose lord bonus the kept lords raise is re-asked.
- B AttackTapLoss: a live sac-for-mana outlet's implicit fodder payment is charged (dorks first, eaten bodies leave).
- C ProjectPlanWalk: a legendary lord already on our battlefield is not kept a second time.
- D Solve consider(): a subset that reads lethal, casts a kept lord, and would replace `best` is admitted as a win only
  if LethalCutKillIsReal (apply on a copy + combat) confirms; a refused one competes on value like the off arm.
- MTG_LORDCAST_HAND (Ilharg hand fix): still a TEMPORARY selector; fold into the lever if the lever is ever adopted.

## Mechanism classes (round-2 d0 slower games, onA vs off), with the amended arm's recovery
Replay `$S/cls/r1.json` (off vs onB, every round-2 d0 slower game). Report: `python3 logs/lordcast/r1_report.py <err>
$S/cls/c1.err $S/cls/feat.txt`. A+B+C = `$S/cls/rs.err` (mtg.r3); A+B+C+D = `$S/cls/r1_m15.err` (mtg.r4 == mtg.r5).
| deck | class | n | fixed A+B+C | fixed A-D | status |
|---|---|---|---|---|---|
| Boars | A frozen intent | 206 | 191 | 193 | CONFIRMED (INTENT=0 recovers 206/237 in round 2; A recovers 193) |
| Boars | C legend kept twice | 23 | 23 | 23 | CONFIRMED |
| Boars | other (Herald phantom x3, Somberwald x2, Horn, empty cast x2) | 8 | 0 | 0 | not examined |
| Goblins | B implicit fodder | 136 | 134 | 136 | CONFIRMED (gi314 traced; B recovers) |
| Goblins | C | 3 | 3 | 3 | CONFIRMED |
| Goblins | other | 11 | 11 | 11 | (recovered anyway) |
| Elves | D phantom lethal | 183 | 29 | 170 | see below |
Still slower under A-D: Boars A 13 (3295 18277 18413 20574 21480 23407 29609 32413 35163 38277 44568 46454 46844),
Boars other 8, Elves 13 (1083 5479 9245 19420 22095 23369 26516 32733 41071 46086 46809 46987 47991). Not examined.

Elves d0 mechanism (sample of 39 of the 154 still slower under A+B+C; `logs/lordcast/dcls.sh` + `dfeat.py`, out
`$S/cls/dcls_elves.txt`): 36/39 PHANTOM LETHAL -- the lever arm's chosen m1 subset reads lethal only with the lever (e.g.
gi13999 Brigade squad 1: read 15 with the lever, 7 without, 7 dealt) and in all 36 the real payment tapped a creature the
projection counted as an attacker. Solve's comparator then lets it DOMINATE every non-winning subset ("winning
dominates"), so the off arm's better line (often the same casts plus continuations: gi8772 dealt 30 vs 16) is lost.
Sub-causes of the over-read (the lord multiplies each one by its count): (i) CONFIRMED gi13999: Wirewood Lodge's untap
of Priest of Titania counted as NON-attacker supply (supply 10, "non-attacker" 5 from 2 Forests + a {C} Lodge); (ii)
HYPOTHESIS, 19/36: executor payment order -- an attacking dork (Archdruid / Priest / Llanowar) tapped while a Forest
stays open; (iii) HYPOTHESIS: colour (round-1 note). 1 non-lethal reorder (gi157), 2 unmatched multi-solve turns.
D does not fix the payment model; it refuses the phantom where it matters (a claimed lord-credited lethal).

## d0 preview (A-D, off vs onB, gi 0-9999 of the ROUND-2 seeds -- NOT held out; `$S/cls/pv.err`)
Boars -64 (67 faster / 3 slower), Elves -750 (757 / 8), Goblins -54 (47 / 0). d0 cpu/game Elves 0.2 -> 0.3 ms (D's apply).
Round 2 onA on the full 50k: Boars +163, Goblins +49, Elves -3269.

## Searched slower games (round-2: Elves p20 6, Elves 2HG p20 10, Beasts p20 1; plus round-1 gi663)
`$S/cls/s3.json` -> `$S/cls/rs.err`. At d8 b0 EVERY one recovers under onB (Elves 6/6, 2HG 10/10, Beasts 1/1), incl. the
previously unrecoverable gi1552, 2HG gi480 and round-1 gi663. At p20: Elves 4/6, 2HG 6/10, Beasts 0/1 still slower,
all d8-b0-recovered (budget reshuffle; the bar is met).
ROOT CAUSE of the three unrecoverables: NOT the amendments, and NOT this lever's projection -- every mask (A-D off, i.e.
round-2 onA) recovers them on the rebased binary, while the pre-rebase round-2 binary `mtg.ab2` still plays them slower
(gi1552 T5, gi663 T5 digest a13471a212d1158d = round 1, gi480 T6; `$S/cls/s4old.err`). So an UPSTREAM commit in the
rebase a35c0ccc0..f2e307fe3 fixed them. Elimination: at bf58ca704 they already recover with MTG_PLAYOUT_PAY_DRAW=0 AND 1
(so NOT 85e09656e's Visionary pay-draw nor 887a691a6); on the final binary MTG_ELVES_BUCKET_DISCARD=0 (c39ff9e5d),
MTG_CCO_RESERVE_JOINT=0 + MTG_LINE_NC_LEFT=0 (dea141a1e) and MTG_ELVES_SCALING_PRUNE=0 do not revert them.
ATTRIBUTION RESULT (c39ff9e5d build = 5e8e09120^, d8 b0; `$S/c39.res`): lever on is STILL SLOWER there (gi1552 5 vs
off 4, gi663 5 vs 4, gi480 6 vs 5), and recovered at bf58ca704 even with PAY_DRAW=0. So the fix is in 5e8e09120 (Elves
scaling prune: holdall hold-back band) or the kick-count cap 16 -> 20 of 85e09656e. HYPOTHESIS (strong): 5e8e09120 --
a T4 board never reaches K > 16, and MTG_ELVES_SCALING_PRUNE=0 also recovers them on the final binary, i.e. it is the
prune band, not the lever's read, that decided those lines. Confirm with one build at 5e8e09120 (not run).

## Running jobs
None of mine (the c39 chain finished; see the result above). Lane / heavy lock: nothing held. Worktree `.claude/worktrees/lordcast-paydraw` (detached at
c39ff9e5d, clean, scratch for the attribution builds): remove with `scripts/safe_worktree_remove.sh` after
`python3 scripts/check_references_safe.py`.

## Remaining, in order
1. Round-3 A/B -- READY, NOT LAUNCHED (the coordinator sequences heavy-lock time; Elves mulligan generation has priority):
   `bash logs/lordcast/run_ab3.sh` (run_in_background, from the worktree). It copies build/Release/mtg (= 82f9d2a91) to
   `$S/mtg.ab3`, writes `$S/m3.json` (2,366 jobs / 330,000 games: Boars / Goblins / Elves, d0 50k, play 4000, 2HG 1000,
   arms off / onB, FRESH BASE 7700000), runs under `flock build/heavy.lock` with MTG_RSS_CAP_GB=20 MTG_DUMP_WINS=1.
   Out `$S/ab3.out` / `$S/ab3.err`, start stamp `$S/ab3.STARTED`, PID `$S/run_ab3.pid`, DONE marker `$S/DONE3`.
   Expected wall on 32 cores: ~15-25 min (round 2 spent 6.85 core-h on exactly these cells, off + onA). Check the [play]
   provider= lines and the first heartbeat (~32/32). Report: `python3 logs/lordcast/report.py $S/ab3.err $S/ab3.out
   onB off --list`. Then d8 b0 of every searched slower game: `python3 logs/lordcast/mk_d8.py $S/ab3.err $S/m3.json
   $S/m3_d8.json onB off` (check its seed base / flag copy first, see handoff 1 step 3).
2. If the bar holds on the three decks: the other 19 lord decks were not re-measured since round 2 (they all read <= 0
   there); a full-suite A/B is the USER's call before any adoption. The lever stays OFF until the USER decides.
3. Unexamined residue: Boars A 13 / other 8, Elves 13 still slower under A-D.
