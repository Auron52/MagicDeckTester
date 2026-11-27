# MTG_ATTACK_LORD_CAST re-test (2026-11-27) -- handoff

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/elves-lordcast`, branch `wip/elves-lordcast` (from
wip/elves-int 2f1b0bff4). Scratch `S=/workspaces/MagicDeckTester/build/scratch/elves-lordcast`. Push wip only, never
phase-1-2-deck-analyzer. Do NOT flip MTG_ATTACK_LORD_CAST's default: that is the USER's call.

USER (verbatim): "I don't know what this is, but I suppose we could try it."

## Done (commits on wip/elves-lordcast)
- b1d95fe56 apparatus: `logs/lordcast/lorddecks.py` (suite decks holding an IsLordPermanent creature = the 22 decks the
  lever can touch), `mk_lordcast.py` (one pooled manifest, arms innermost, chunk seed = BASE+gi0 and game_index = gi0),
  `report.py <err> <out> [A] [B] [--list]` (paired per-game, unwon = 9, null-arm discordance, cpu/game ratio).
- e980dbd32 TEMPORARY selector `MTG_LORDCAST_HAND` (heurarm LORDCAST_HAND_TMP), round-1 report
  `logs/lordcast/ab_report.txt`, helpers `projdiff.sh` (d0 [proj] trace diff, TR=<turn>), `lethalturn.sh`, `mk_d8.py`.
- (this commit) TEMPORARY selector `MTG_LORDCAST_VERIFY` (heurarm LORDCAST_VERIFY_TMP), round-2 manifest generator.

## Round 1 (seed 7340000; flag 1 vs 0; 2.63M games, 63 min, heartbeat 32/32; null arm 0 discordant in EVERY cell)
d0 = 50,000 pairs per deck; play = shipped value_play depth + suite play budget, 4,000 pairs (Beasts 1,000); 2HG play
1,000 (Beasts 200). net = on - off in turns. cpu ratio = on/off core-ms per game.

| deck | d0 net (fast/slow) | play net (fast/slow) | 2HG play net (fast/slow) |
|---|---|---|---|
| Angels | -27 (27/0) | 0 | 0 |
| Beasts | -1618 (1620/2) | 0 | -1 (1/0) |
| **Boars** | **+151 (43/194)** FAIL | 0 | 0 |
| Elementals | -140 | -2 (2/0) | -1 (1/0) |
| **Elves** | -3275 (3429/155) | **+6 (1/7)** FAIL, cpu 1.057 | **+1 (3/4)** FAIL, cpu 1.073 |
| Fungus v3 | -37 (48/11) | 0 | 0 |
| Giants | -333 | 0 | - |
| Goblins | 0 (192/125) (was +177) | -1 (1/0) | 0 |
| Kithkin | -136 | 0 | - |
| Knights | -2061 | 0 | 0 |
| Merfolk | -939 | 0 | - |
| Merfolk Turns | -842 | 0 | - |
| Minotaur | -621 | 0 | 0 |
| Pirates | -386 (389/3) | 0 | 0 |
| Red Elementals | -118 | 0 | 0 |
| Selesnya v2 | -285 (304/19) | 0 | 0 |
| Slivers | -1687 (1690/3) | 0 | 0 |
| Soldiers | -647 | 0 | 0 |
| Spirits | -13 | 0 | - |
| Stompy | -104 (106/2) | 0 | 0 |
| Vampires | -914 (1007/93) | 0 | 0 |
| White Knights | -1638 (1641/3) | 0 | 0 |

d8 b0 recovery of the 11 searched slower games (`$S/m_d8.json`, `$S/d8.err`): 10 recover (on = off = T4). **Elves 2HG
gi663 does NOT recover** (off T4, on T5 at d8 b0). So round 1 FAILS the bar on Boars d0, Elves play, Elves 2HG play, and
one unrecoverable searched game.

## Mechanisms found
1. **Boars d0 (both directions): the walk never takes a cast card out of the scratch HAND.** ProjectPlanWalk keeps the
   cast lord on the board while its card is still in t's hand, and Ilharg's attack-put projection
   (ApplyAttackPutProjection, inside PendingAttackDamage) puts from that hand. gi10/876/1170 T4: {Raggadragga} reads 16
   (on) vs 20 (off): a second legendary Raggadragga is "put", one dies, the read is 4 short, the T4 kill is missed.
   gi696/1996 T5: the phantom put is credited ON TOP of the cast -> a false lethal is played. Fix prototype:
   `MTG_LORDCAST_HAND=1` (the walk erases one same-named hand card per walked cast). Hand readers in the projection:
   Ilharg (Boars) and Neheb's hand-size anthem (Minotaur) only.
2. **Elves d0: the projection's payment model is colour-blind and executor-blind.** AttackTapLoss charges attackers
   only for `need = cost_mv - (supply_total - attacker_yield)`. In 5 of 6 sampled slower games the lethal-turn payment
   tapped an ATTACKING dork the projection assumed would swing (gi244: Visionary paid with Elvish Mystic while a Forest
   stayed open, 11 dealt vs 14 read; gi745: the open non-attackers were Cavern + Wirewood Lodge, {C} only, so a {G} dork
   had to tap). Pre-existing over-read; the lever's (correct) lord credit pushes more subsets over the lethal line.
   off arm in gi244 kept going (Visionary + Allosaurus Shepherd's 5/5 activation) and won T4. Proposed fix (NOT
   prototyped, not cheap): colour-aware need (per-colour pips vs per-colour non-attacker supply) and/or the executor's
   real source order in the stage-1 payment; or verify a lord-dependent projected win in Solve by ApplyPlanDirect +
   SimulateCombat (the BuildStormGoffLine verify pattern, ~TurnSolver.cpp:67990).
3. **Searched (Elves play / 2HG, gi663 unrecoverable): SolveWithLookahead returns the FIRST projected winner unverified**
   (`if (p.wins_this_turn) { report(state.turn_number, depth - 1); return p; }`, ~TurnSolver.cpp:134447, at the root AND
   every interior node, every budget). A projected over-read (mechanism 2) is therefore scored as a real this-turn win
   inside the search window. gi663 (2HG, seed 7340663, gi663) diverges at T3: off pays Leaf-Crowned Visionary's {G}
   draws (draws Archdruid + Warcaller, kicked Warcaller kills T4); on skips the draws and attacks. Fix prototype:
   `MTG_LORDCAST_VERIFY=1`: the return is taken only when LethalCutKillIsReal (apply + combat on a copy) confirms; a
   refused phantom falls through to the next winner / the full search. Sound (removes only phantom wins), but it
   changes searched play on EVERY deck and costs CPU, so it must be measured everywhere.
   Elves play slower games other than gi663 (gi604 etc.) diverge at T1 and recover at d8 b0: budget reshuffle.
   **SANITY CHECK REFUTED THIS FOR gi663** (`$S/m_663b.json`, `$S/rc/t663b.out`, build3): at d8 b0 onAV (lever + HAND +
   VERIFY) is BYTE-IDENTICAL to on (digest a13471a212d1158d, units 37615, T5); off is T4. The verify never refused, so
   the unverified first-winner return is NOT gi663's mechanism. gi663 is still open: next suspects are the other
   wins_this_turn consumers in the search window (plan sort / move tier PlanMoveOrderFront/Back at ~92885, the m2
   probe filter `if (!e.wins_this_turn && !M2FixUnfiltered())` ~115669, the plan-hold exemption ~107924), and the
   Visionary pay-draw breakpoints at T3. Off reaches T4 by paying both Visionary draws on T3 (off: units 2127 vs on 37615
   -- the on arm searches 18x more, i.e. its T3/T4 frames read a projected win somewhere and keep searching wrong lines).

## Round 2 RESULT (finished 06:38 UTC; 20,292 jobs, 3.70M games; report logs/lordcast/ab2_report.txt)
- **on vs off (fresh-seed replication):** Boars d0 **+172** (48/219), Goblins d0 **+49** (201/150; round 1 read 0),
  Elves play **+3** (3/6), Elves 2HG play **+8** (1/9), Beasts play **+1** (0/1); every other cell <= 0
  (d0: Elves -3269, Knights -2120, WhiteKnights -1645, Slivers -1618, Beasts -1597, ...). FAILS the bar again.
- **onA (lever + MTG_LORDCAST_HAND) vs off:** Boars d0 **+163** (75/237) -- the hand fix is real but explains only a
  small part of Boars' loss (+172 -> +163). Searched play identical to on (Elves +3 / 2HG +8). Goblins d0 +49 unchanged.
- **onAV vs onA:** ZERO change in every play cell (net 0, 0 faster / 0 slower everywhere). MTG_LORDCAST_VERIFY is
  inert: DELETE it.
- So the remaining Boars d0 loss (~+163) and the Goblins d0 +49 are NOT the hand mechanism; next: projdiff the onA
  slower Boars/Goblins games (mechanism-2 payment over-reads are the first suspect -- Raggadragga pumps mana
  creatures, which the payment then taps), and the Elves searched slower games still need d8 b0 (mk_d8.py fix below).

## Round 2 setup (as run)
Manifest `$S/m2.json` (generated by: `BASE=7460000 NULL=0 ARMS_D0="off:0:0,on:1:0,onA:1:1"
ARMS_PLAY="off:0:0:0,on:1:0:0,onA:1:1:0,onAV:1:1:1" python3 logs/lordcast/mk_lordcast.py $S/m2.json`), fresh seeds
7460000 (the fixes were chosen from round-1 games). 20,292 jobs / 3.70M games. Arms (flags LORD:HAND:VERIFY):
- d0: off, on, onA (lever + hand fix). VERIFY cannot touch d0 (d0 is Solve, not SolveWithLookahead).
- play + 2HG play (all 22 decks): off, on, onA (lever + hand fix), onAV (lever + hand + verify). Changed from the
  planned offAV after the gi663 refutation: onA vs on isolates the hand fix in searched play, onAV vs onA shows whether
  VERIFY changes searched play at all (if onAV == onA everywhere, delete VERIFY).
Runner `$S/run_ab2.sh` copies build/wt/elves-lordcast/Release/mtg to `$S/mtg.ab2`, runs under
`flock build/heavy.lock`, MTG_RSS_CAP_GB=20 MTG_DUMP_WINS=1, out `$S/ab2.out`, err `$S/ab2.err`, DONE marker `$S/DONE2`.
Binary: build3 of this commit (both selectors). PID: see "Running jobs".

## Remaining steps, in order
1. Wait on `$S/DONE2` (kill -0 on the PID or the marker; never pgrep -f). Check the first `[batch] heartbeat` is ~32/32.
2. Reports: `python3 logs/lordcast/report.py $S/ab2.err $S/ab2.out onA off --list` (d0 + play rows),
   `... on off --list` (a fresh-seed replication of round 1), `... onAV onA --list` (verify's own effect).
   Save to logs/lordcast/ab2_report.txt.
3. Bar per deck: d0 and play/2HG (onA vs off; onAV vs off if verify moves anything) net <= 0; every searched slower game
   of the candidate arm replayed at d8 b0: `python3 logs/lordcast/mk_d8.py $S/ab2.err $S/m2.json $S/m2_d8.json onA off` -- NOTE mk_d8.py
   hard-codes seed base 7340000 and sets only MTG_ATTACK_LORD_CAST: change it to BASE 7460000 and to copy the arm's full
   flags before using it. Run under the heavy lock.
4. Root-cause Elves 2HG gi663 (seed 7340663, gi663, 2HG, d8 b0: `$S/m_663b.json` replays it in ~1 s) per mechanism 3's
   suspects; it is the one unrecoverable searched game.
5. Expect Elves d0 still to fail on onA (mechanism 2 is untouched by both selectors). If so, report it with the
   proposed colour-aware-need / verify-in-Solve fix; prototype only if cheap.
6. Report to the parent: per-deck table, pass/fail, mechanisms, amendment numbers, commit hashes. Delete any temporary
   selector not recommended (HeuristicArm.h slots LORDCAST_HAND_TMP / LORDCAST_VERIFY_TMP + their readers in
   TurnSolver.cpp). The lever's default stays OFF; the USER decides.

## Running jobs: NONE (round 2 finished; the section below is historical)
- Round 2: runner `$S/run_ab2.sh` PID **2560820** (its flock child 2560822 was WAITING on build/heavy.lock at 04:20 UTC,
  held by another session's PID 2553479). Binary `$S/mtg.ab2` (= build3 of 87219ddc7's src, both selectors).
  Output `$S/ab2.out` / `$S/ab2.err`; DONE marker **`$S/DONE2`**. Wait with
  `until [ -f $S/DONE2 ]; do sleep 60; done` (run_in_background) or `kill -0 2560820`. Expect ~2-2.5 h once it holds the
  lock (round 1's 2.63M games with 2 searched arms took 63 min; round 2 has 4 searched arms).
- Nothing else running.
