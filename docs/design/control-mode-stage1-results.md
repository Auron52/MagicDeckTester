# Control-scoring mode, stage 1: the full delay table

The run finished 2026-11-28 at 17:19 UTC. It was 32,773 jobs and 206,590 games over 65 gauntlet decks, 20 games
per deck, seeds `8100001+gi`, horizon 15, at 12 threads. It took about 6.2 h of wall time against the 8.9 h plan.
The batch exited with rc 0. The binary was built from src tree `b19659c2271141c2e0c7bee1171533ded62ed24e`, clean,
which is the same tree as the baseline. The table was made by `scripts/control_gauntlet.py table make`, run by
`mtg --batch`, and summarized by `control_gauntlet.py report`.

* Full per-deck report: `docs/design/control-mode-stage1-full-table.txt` (also `logs/control-mode/full-table-report.{txt,json}`).
* Per-game rows: `build/scratch/control-mode/full-table-rows.jsonl` (82 MB, build volume only, can be regenerated with `report`).

## Delay per effect, equal weight over the 65 decks

A delay is in rounds and is paired per game against the stored baseline; a game unwon by round 15 counts as 16.
"Fixed" is the default target policy. "Clairvoyant" tries every candidate target and keeps the largest delay.
The standard error is in brackets.

| arm | fixed | clairvoyant |
|---|---|---|
| land2+3+4 | +3.32 (0.05) | - |
| ctr3+4+5 | +2.41 (0.05) | - |
| land3+4+5 | +2.28 (0.04) | - |
| land2+3 | +2.27 (0.04) | - |
| crt3+4+5 | +1.81 (0.04) | - |
| ctr3+4 | +1.81 (0.04) | - |
| ctr2+3 | +1.81 (0.04) | - |
| disc2+3+4 | +1.70 (0.05) | - |
| land3+4 | +1.67 (0.03) | - |
| ctr3+land4 | +1.64 (0.04) | - |
| sweep4 | +1.49 (0.04) | - |
| disc2+land3 | +1.49 (0.04) | - |
| land3+crt4 | +1.47 (0.03) | - |
| crt3+ctr4 | +1.42 (0.04) | - |
| disc2 | +0.67 (0.03) | +1.35 (0.04) |
| disc2+3 | +1.31 (0.04) | - |
| ctr3+3 | +1.24 (0.04) | - |
| crt3+4 | +1.24 (0.03) | - |
| ctr3 | +1.04 (0.04) | +1.19 (0.04) |
| tax2 | +1.18 (0.03) | - |
| land2 | +1.09 (0.03) | +1.09 (0.03) |
| disc3 | +0.64 (0.03) | +1.09 (0.03) |
| sweep3 | +1.04 (0.03) | - |
| xuall4 | +1.02 (0.02) | - |
| skip2 | +1.00 (0.00) | - |
| skip3 | +1.00 (0.00) | - |
| tapland3+land4 | +0.99 (0.03) | - |
| crt2+3 | +0.97 (0.03) | - |
| land3 | +0.89 (0.02) | +0.96 (0.02) |
| ctr2 | +0.85 (0.03) | +0.92 (0.03) |
| tax3 | +0.89 (0.03) | - |
| skip4 | +0.89 (0.01) | - |
| ctr4 | +0.71 (0.03) | +0.84 (0.03) |
| crt3+life4 | +0.82 (0.02) | - |
| ctrtop3 | +0.75 (0.02) | +0.81 (0.02) |
| sweep5 | +0.80 (0.03) | - |
| crt4 | +0.64 (0.02) | +0.72 (0.02) |
| ctrhand3 | +0.63 (0.02) | +0.70 (0.02) |
| ctrtop2 | +0.67 (0.02) | +0.70 (0.02) |
| xuall3 | +0.69 (0.02) | - |
| crt3 | +0.61 (0.02) | +0.67 (0.02) |
| ctrtop4 | +0.56 (0.02) | +0.64 (0.02) |
| xuall5 | +0.60 (0.02) | - |
| disc4 | +0.37 (0.02) | +0.59 (0.02) |
| ctrhand4 | +0.49 (0.02) | +0.58 (0.02) |
| land4 | +0.47 (0.02) | +0.55 (0.02) |
| xu4 | +0.55 (0.02) | - |
| xu3 | +0.50 (0.02) | - |
| skip5 | +0.48 (0.01) | - |
| ctrhand2 | +0.44 (0.01) | +0.47 (0.01) |
| tax4 | +0.46 (0.02) | - |
| sweep2 | +0.42 (0.02) | - |
| tapland3 | +0.40 (0.01) | +0.42 (0.01) |
| ctr5 | +0.36 (0.03) | +0.42 (0.03) |
| crt2 | +0.41 (0.02) | +0.41 (0.02) |
| life3+4 | +0.40 (0.01) | - |
| tapland2 | +0.34 (0.01) | +0.34 (0.01) |
| life5r3 | +0.34 (0.01) | - |
| ctrtop5 | +0.30 (0.02) | +0.34 (0.02) |
| crt5 | +0.30 (0.02) | +0.33 (0.02) |
| tapcrt4 | +0.29 (0.01) | +0.33 (0.01) |
| life5r4 | +0.33 (0.01) | - |
| ctrhand5 | +0.26 (0.02) | +0.30 (0.02) |
| life5r2 | +0.30 (0.01) | - |
| xuall2 | +0.29 (0.01) | - |
| tapland4 | +0.26 (0.01) | +0.29 (0.01) |
| xu2 | +0.29 (0.01) | - |
| xu5 | +0.28 (0.01) | - |
| disc5 | +0.17 (0.02) | +0.27 (0.02) |
| tapcrt3 | +0.22 (0.01) | +0.25 (0.01) |
| land5 | +0.21 (0.02) | +0.25 (0.02) |
| life3r3 | +0.23 (0.01) | - |
| life3r4 | +0.23 (0.01) | - |
| tax5 | +0.20 (0.02) | - |
| life3r2 | +0.20 (0.01) | - |
| tapcrt5 | +0.15 (0.01) | +0.16 (0.01) |
| life5r5 | +0.16 (0.01) | - |
| tapland5 | +0.13 (0.02) | +0.14 (0.02) |
| tapcrt2 | +0.12 (0.01) | +0.12 (0.01) |
| life3r5 | +0.11 (0.01) | - |

## Stacking (joint delay minus the sum of its parts, equal weight over the 65 decks)

| interaction | mean (se) |
|---|---|
| land: (3+4) - 3 - 4 | +0.31 (0.03) |
| land: (2+3) - 2 - 3 | +0.28 (0.04) |
| crt: (3+4) - 3 - 4 | -0.01 (0.03) |
| crt: (2+3) - 2 - 3 | -0.05 (0.02) |
| ctr: (3+4) - 3 - 4 | +0.06 (0.03) |
| ctr: (2+3) - 2 - 3 | -0.08 (0.04) |
| disc: (2+3) - 2 - 3 | -0.00 (0.04) |
| ctr: two in round 3 - 2 x one | -0.84 (0.05) |
| life3: (3+4) - 3 - 4 | -0.07 (0.02) |
| land3 + crt4 - parts | -0.06 (0.03) |
| crt3 + ctr4 - parts | +0.11 (0.03) |
| disc2 + land3 - parts | -0.07 (0.02) |
| ctr3 + land4 - parts | +0.12 (0.03) |
| crt3 + life3@4 - parts | -0.03 (0.01) |
| tapland3 + land4 - parts | +0.12 (0.02) |
| sweep3 - crt3 | +0.43 (0.02) |
| land: 3rd increment - 2nd | -0.17 (0.03) |
| crt: 3rd increment - 2nd | -0.06 (0.04) |
| ctr: 3rd increment - 2nd | -0.17 (0.05) |
| disc: 3rd increment - 2nd | -0.25 (0.04) |

## Findings

1. **Effects across rounds mostly add.** Every cross-kind interaction is within ±0.12 of additive. That supports
   the prefix-sum shape of the score (S1). The exceptions follow.
2. **Land destruction compounds.** Two land destructions in consecutive rounds are worth about 0.3 more than their
   sum. Three (rounds 2, 3, 4) give +3.32, the largest delay in the table.
3. **A second counter in the same round is mostly wasted.** Two counters in round 3 are worth 0.84 less than twice
   one counter (+1.24 against 2 x 1.04). Usually only one spell per round is worth countering.
4. **The third copy of a kind gives less than the second** (land, counter, discard: -0.17 to -0.25).
5. **The choice of target matters most for discard.** For round-2 discard the fixed policy gives +0.67 and the
   clairvoyant pick gives +1.35. For counters the gap is small (round 3: +1.04 against +1.19), and for land
   destruction and creature removal it is about zero. The discard target policy is the one to improve before
   stage 3.
6. **A round-3 counter (+1.04) beats a round-3 land destruction (+0.89)**, and a round-2 tax (+1.18) is about as
   good as a round-2 land destruction (+1.09). Life gain is small everywhere (+0.1 to +0.4).
7. **The analytic skip check was 4310/4383 (98.3%) in this table, and is 4324/4383 (98.65%) after the
   harness fix.** (The 999/1000 quoted earlier is the harness validation probe, d3 b10 on five decks. The pilot
   TABLE scored 927/929 on the same five decks, and its two exceptions, Burn gi12 skip2/3, recur here. The gap
   is deck coverage, not a change of binary.) Root-caused 2026-11-28 (harness doc section 9). The 73
   exceptions had three causes:
   * **A harness defect, now fixed.** Heuristics read the round (`turn_number`) as a development clock:
     `ExpectedAttacks` in the greedy chooser, and 30 provider rules ("ramp ranks high through turn 2", Gods'
     Magus before turn 4, the Grave-Troll admission). A skip advances the round but not the board, so the arm
     played the same position as a later-round one. Cyclone gi0 is an example: the fit pass's rollouts said
     6 against 8, and the root played Cascade Bluffs, not Myriad Landscape. These sites now read
     `OwnRound(state)` = `turn_number - ctrl_skipped_turns`, which IS `turn_number` in every game without a skip.
     That keeps the change byte-identical outside the skip arms: smoke 255/255 with play-changed 0, and the
     fresh baseline 560/560 per-game digests equal to the stored one. **16 exceptions are gone** (Cyclone 0, 13
     and 14, Gods 5 and 16, Hinata 17 and 19 in every arm, Landfall skip2, Merfolk Turns 10, Shadowborn 4). The
     non-skip arms of this table are unchanged by construction. Only the skip column moves, and the numbers
     above it are the re-run.
   * **The passive opponent's own turns (38, tagged `opp-turn`).** This is correct play. Its turns keep their
     per-round schedule while the skip moves our development a turn later against it. A fetch or shock costs
     it life (Burn gi12), and Seraph Sanctuary gains it life (Sea gi0). Tainted Remedy turns that gain into the
     last point (Anti-Lifegain gi0). Burgeoning on its land drop gives a Maze's End win on its turn (Maze's End
     gi0 and gi19), and its draws help a mill kill (Mill gi13). The report tags these from the new record
     fields (`kill_on_opp_turn`, `rounds[].opp_turn_life`).
   * **Budgeted search variance (21, tagged `other`).** This is a property of the search, not a harness
     defect. Most is cross-decision reuse within a game: the base's turn-2 search reuses its turn-1 subtrees,
     which a skip makes unusable (Gods 12 and skip4 16, Prevent Damage 11, Landfall 10, Hydras 5; each is
     exact +1 under `MTG_P17_ISOLATE=1`). The rest is a b20 search missing a line. Gods gi13: the base passes with
     Magus castable on rounds 4-6, and at b80 and b320 the pair is exact, base 8 and skip 9. Two of the 21
     appeared with the fix (Hinata skip3 gi18, Merfolk Turns skip3 gi1). Morph gi17 is Blistering Firecat's
     end-step sacrifice in the opponent's extra turn, which the life screen cannot see, so it is tagged
     `other`.
   So **every remaining exception is correct play or search variance; none is a rules or harness error.**
   Per-deck lists: `logs/control-mode/skipcheck-report.{txt,json}` (`skip`) and
   `skipcheck-exceptions.json` (all 59, each with its tag).
8. **Games unwon at round 15 are rare.** In any one arm, at most a few of the 65 decks have even one such game (the
   `u` counts in the hit-rate section). The most is land 2+3+4 with 8 decks, followed by the joint counter arms
   and the joint discard arms. So the S2 lock-credit rule moves the score only a little at these strengths.
