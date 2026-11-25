# Heath fetchland hint: why Morph and Modern RW Burn play slower at play settings (2026-11-25)

Branch under test: `wip/land-heath` 886231bd9 (`new`), against origin `05e2e0030` (`base`, built in
`.claude/worktrees/land-heath-base`). Binaries frozen in `build/scratch/heath-morph/` (`mtg.new` = the land-heath
Release build, `mtg.origin` = the base build used for the land-heath report). Every run was one pooled `--batch` per
binary under the box lock (`MTG_RSS_CAP_GB=40 MTG_PGO=0`, 32 threads). Games are paired on (seed, game_index). `net`
is the summed turn delta over games both arms won; + means new is slower.

**Verdict: no fix is needed. The play-setting cost on Morph and Burn is a clairvoyance artifact of the lockstep
search.** Nearly every changed game (158 of 178) diverges at the turn-1 fetch-vs-land drop, and the rest at
reshuffle timing inside the search's rollouts. Under honest search (`MTG_NC_SEARCH=1`, and the
decoupled-reshuffle rows) the difference disappears, or new comes out slightly ahead. Every slower game recovers at
d8 b0. No branch carries a code change. `wip/heath-morph` holds only this report and its scripts.

## 1. The n=10000 nets (fresh seeds 14200000+, never used before)

Batch 1: `build/scratch/heath-morph/big/` (`report.txt` from `report.py`, `changed_base_new.txt`). Batch 2:
`build/scratch/heath-morph/big2/`, seeds 14300000+, to replicate the 2HG row. Play setting = profile depth 5,
budget 20 (as in `test/regression_cases.sh`). `_dec` rows set `shuffle_salt_search 1`: the search plans against a
reshuffle that the real game does not deal, so after a reshuffle it cannot see the real library.

| row | n | changed | faster | slower | lost/gain | net | avg base | avg new | units/game base -> new |
|---|---|---|---|---|---|---|---|---|---|
| morph_play | 10000 | 12 | 7 | 5 | 0/0 | **+1** | 4.0161 | 4.0162 | 4356 -> 4410 (+1.2%) |
| modernrwburn_play | 10000 | 17 | 6 | 11 | 0/0 | **+5** | 3.8118 | 3.8123 | 8372 -> 8369 |
| morph2hg_play | 4000 | 37 | 11 | 26 | 0/0 | **+20** | 4.6358 | 4.6409 | 15946 -> 16302 (+2.2%) |
| morph2hg_play (replica) | 8000 | 69 | 24 | 42 | 3/0 | **+18** | 4.6347 | 4.6361 | 16183 -> 16402 (+1.4%) |
| modernrwburn2hg_play | 4000 | 22 | 9 | 12 | 1/0 | **+3** | 4.8020 | 4.8020 | 30136 -> 30147 |
| morph_dec | 10000 | 9 | 7 | 1 | 0/1 | **-7** | 4.0131 | 4.0127 | 4316 -> 4351 |
| modernrwburn_dec | 10000 | 36 | 20 | 16 | 0/0 | **-4** | 3.8037 | 3.8033 | 8439 -> 8462 |
| morph2hg_dec | 8000 | 63 | 30 | 31 | 1/1 | **+1** | 4.6332 | 4.6332 | 16688 -> 16784 |
| modernrwburn2hg_dec | 4000 | 44 | 20 | 24 | 0/0 | **+4** | 4.8397 | 4.8407 | 30632 -> 30696 |

CPU per game: Morph 196 -> 186 ms, Burn 280 -> 272 ms, Morph 2HG 814/840 -> 793/800 ms, Burn 2HG 1293/1200 ->
1196/1192 ms. The arms ran back to back, so these wall figures include drift between runs. The units column is
deterministic: +1.2% to +2.2% on Morph and flat on Burn.

What the table shows:
* **1HG Morph is not one-directional at scale.** The 4/0 slower in rwf (n=2000) becomes 7 faster / 5 slower, net +1,
  on 10000 fresh seeds. Its honest row (`morph_dec`) is net -7.
* **Morph 2HG is the real coupled-search cost.** It replicates on two seed blocks: 12000 games, 35 faster / 68
  slower, 3 lost, net +38 (about 0.003 turn/game). The decoupled row on the same deck and setting is **+1 (30/31)**
  over 8000 games.
* Burn: coupled +5 / +3, decoupled -4 / +4. That is at the noise level.

## 2. The one mechanism: the turn-1 fetch-vs-land tie breaks the other way

I traced every changed game in the rows above (109 in batch 1 plus the rwf/dec2 games), plus the 69 changed games of
the 2HG replica, on both binaries at the play setting (`t2/`, `t3/`). **In 158 of the 178 changed games (103 of 109 in batch 1, 55
of 69 in the replica), the first diverging decision is the land drop on turn 1 (a few on turn 2 or 3), with a
fetchland on one side.** new plays the fetchland (Wooded Foothills in Morph; Arid Mesa or Foothills in Burn) where
base plays a dual or basic (Stomping Ground, Forest, Sunbaked Canyon, Sacred Foundry, Mountain). In a few faster games
it goes the other way. The other 20 (6 + 14, nearly all Morph 2HG) first diverge at a different early choice with no
fetch involved: Stomping Ground vs Forest, the Legion Leadership land face vs a land, or no land at all. Those come
from the search's leaf, not its tie-break (below). Per-game lines: `trace_divergence.txt`, `t3_divergence.txt`.

The brief's games:

| game | base / new | first divergence (T1 main 1) |
|---|---|---|
| rwf morph gi29 | 5 / 7 | base Stomping Ground + Birds; new Wooded Foothills (-> Stomping Ground) + Birds |
| rwf morph gi857 | 4 / 5 | base Stomping Ground + Birds; new Foothills + Birds; the reshuffle deals Den Protector instead of Birds on T2 |
| rwf morph gi1021 | 4 / 5 | base Stomping Ground + Birds; new Foothills + Birds |
| rwf morph gi1520 | 5 / 6 | base Forest + Birds; new Foothills + Birds |
| dec2 morph gi354 | 4 / 5 | base Forest + Ignoble Hierarch; new Foothills + Hierarch |
| dec2 morph gi1021 | 4 / 5 | as rwf gi1021 |
| rwf burn gi1627 / 1419 / 1421 | 3 / 4 each | base Sunbaked Canyon; new Arid Mesa / Arid Mesa / Wooded Foothills |
| dec2 burn gi1619 / 1627 / 1421 | 6/7, 3/4, 3/4 | T2 Canyon vs Mesa; T1 Canyon vs Mesa; T1 Canyon vs Foothills |

**Which reader flips it.** It is the greedy land ranker's multi-colour pass (`LandPlay.cpp` `GreedyLandRank`,
`is_multi = HandFixingProduces(...)`). The search uses it as its last-resort tie-break between land plans of equal
value (`TurnSolver.cpp` `greedy_land_name` in the plan sort) and as the move order of the budget-bound search. In
base a fetch in hand produced `[]`, so it sat in the mono pass behind every dual and basic. With the deck-derived
hint, Foothills reaches R+G in Morph (Stomping Ground, Forest, Mountain) and Mesa reaches R+W in Burn (Sacred Foundry,
Mountain). The fetch then ties the dual in the multi pass, where hand order decides, and it beats every basic. The
`derived` and `wf` arms are identical on Morph (rwf report: derived>wf 0 changed), so `MTG_ROLLOUT_FETCH_STATIC_HINT`
plays no part here. This is the same flip Round 5 of `docs/design/heath-produces-2026-11-22.md` found on its
seeds.

**The leaf reader, for the 20 non-fetch divergences.** The playout's land drop (`SimulateLandPlayOne`, via
`PlayoutLandColours`) now puts a Foothills drawn in a rollout in its multi-colour pass: static W/B/R/G with
`MTG_ROLLOUT_FETCH_STATIC_HINT` on, and the same with it off, since the derived reach is R/G. In base it went in the
mono pass, because `produces` was `[]`. The rollout cracks it on play (`PlayLandCore`; Morph INT-9 never holds a fetch), so the
rollouts reshuffle at a different point. Under the lockstep search each rollout then sees a different but fully
known library, and the T1 plan values move. Example, Morph 2HG gi80 (seed 14220080): base's T1 search projects
`win=7` and plays Stomping Ground + Ignoble Hierarch (wins T5). new's projects `win=9`. Its best T1 plan is to do
nothing, and on T2 it plays only a Forest (wins T7). Under NC both arms win T6; at d8 b0 both win T5. A cut to
`--max-turns 2` plays identically on both, so the difference comes from the deep rollouts. Same family as the
Worldfire leaf mechanism of Round 6, but here the playout is the more faithful of the two: in this deck a Foothills
is a Stomping Ground.

**Why the choice itself does not matter honestly.** In Morph, INT-9 cracks the Foothills on play for Stomping
Ground. Foothills on T1 therefore gives exactly the mana of Stomping Ground on T1. The two lines differ only in (a)
1 more life paid, which a goldfish does not feel, (b) one land thinned from the library earlier, a small honest
gain, and (c) **a library reshuffle on T1 instead of later**. An honest player does not know the library order, so a
reshuffle of it is distributionally neutral. The one known part is the cards bottomed by a mulligan. I checked:
slower games are not concentrated in mulliganed games (2HG: 15 slower / 4 faster with bottoms, 11 / 7 without), and
the decoupled rows, which keep that effect, are neutral. Only a search that knows the library order can prefer
"keep this library" over "reshuffle it". The default search is lockstep (`shuffle_salt_search` = `shuffle_salt`; the
fetch's shuffle is seeded by `game_seed` / `search_count`, `GameState.h`), so it knows both the unshuffled order and
the exact post-fetch order. On a T1 plan-value tie, base commits to the original order and new to the reshuffle. The
realised gap between the two known libraries is then one draw-luck sample. It runs against new more often in coupled
play (68 vs 41 over the 109 games). A likely reason, not proven: a fetch held in hand keeps an option that only the
clairvoyant search can use, namely cracking it later on the turn where it can see the reshuffle helps. 2HG games run
about 0.6 turn longer, so that option is worth more there, which fits Morph 2HG's larger cost.

## 3. Clairvoyance test: MTG_NC_SEARCH=1 on both arms, plus d8 b0

Each changed game was replayed on both binaries at the play setting with `MTG_NC_SEARCH=1` (the non-clairvoyant
search). Each slower or lost game was also replayed at `--depth 8 --budget-ms 0` (abandon at 20M units).
Classification of a game where new is slower in coupled play (base turn b, new turn n):
* **R**: new's NC turn <= b. The honest new reaches base's turn.
* **C**: base's own NC turn > b (base's win needed clairvoyance), and new's NC turn <= base's NC turn.
* **V**: new's NC turn > base's NC turn. The honest search still differs, by reshuffle luck.

Batch 1 + rwf + dec2 (`logs/heath/heath-morph/trace_classification.txt`):

| set | slower | C | R | V | d8 b0 recovers |
|---|---|---|---|---|---|
| rwf Morph (29, 857, 1021, 1520) | 4 | 2 (29, 857) | 1 (1021) | 1 (1520) | 4/4 |
| dec2 Morph (354, 1021) | 2 | 0 | 2 | 0 | 2/2 |
| rwf Burn (1627, 1419, 1421) | 3 | 1 | 1 | 1 (1419) | 3/3 |
| dec2 Burn (1619, 1627, 1421) | 3 | 2 | 1 | 0 | 3/3 |
| Morph play n=10000 | 5 | 2 | 3 | 0 | 5/5 |
| Burn play n=10000 | 11 | 10 | 0 | 1 (3043) | 11/11 |
| Morph dec n=10000 | 1 | 0 | 1 | 0 | 1/1 |
| Morph 2HG n=4000 | 26 | 15 | 10 | 1 (2050) | 26/26 |
| Burn 2HG n=4000 | 13 (1 lost) | 9 | 2 | 2 (2530, 2453) | 12/13 + gi2117 |
| **total (batch 1)** | **68** | **41** | **21** | **6** | **67/68 + 1 equal** |
| Morph 2HG replica n=8000 | 45 (3 lost) | 32 | 4 | 9 | 45/45 |

Burn 2HG gi2117 (base T8, new lost) loses on BOTH arms at d8 b0 (abandoned at the cap) and under NC. base's T8 was
budget luck, not something the change took away. Write it as equal at d8 b0.

**The unbiased NC read: all 109 changed games, both directions.** Coupled: new 41 faster / 68 slower. NC:
**92 equal, 8 new faster, 9 new slower** (NC net over both-won games: Morph +2/0/-1, Burn +1/-1, Morph 2HG -1,
Burn 2HG +3). The honest search plays 84% of the changed games to the same turn on both binaries. What is left is
symmetric. The 6 V games are that symmetric residue: under NC the two binaries' land lines still differ (for example
gi1520: both play Forest on T1, and new plays Foothills on T3), and the reshuffle deals them different cards.

2HG replica (seeds 14300000+, all 69 changed games; `build/scratch/heath-morph/t3/`, `t3_classification.txt`):
45 slower or lost (42 + 3 lost). **C 32, R 4, V 9. d8 b0 recovers 45/45**, including the three lost games: gi2618
(base 6, new lost: d8 6/6; under NC new wins T8 and base loses), gi4612 (base 8: d8 8/8, both lose under NC), and
gi5725 (base 7: d8 7/7, NC 7/8). NC over all 69: **45 equal, 12 new faster, 12 new slower** (NC net -3,
2 lost / 2 gained). This is the same symmetric picture as batch 1, on the row whose coupled cost replicated.

Combined over the 178 changed games traced: coupled 65 new-faster / 113 new-slower. Under NC: **137 equal, 20 faster,
21 slower**. Of the 113 slower or lost games, 112 recover at d8 b0, and the last (Burn 2HG gi2117) ties there.

## 4. Fix: none, and why

No reader of the new hint makes a worse choice for an honest player:
* The search's land menu is unchanged. The fetch line and the dual line are both searched. Only their order and the
  last-resort tie-break move, at the permitted greedy sites (the depth-0 runner, the playout drop, the plan-sort
  tie-break).
* Putting a fetch behind a dual again is the deleted `MTG_FETCH_AFTER_DUAL`. r8 measured it worse than the derived
  hint (Worldfire +5, Five Colour +8). It would also only re-buy base's clairvoyant edge on Morph and Burn, which the
  decoupled rows (-7, -4, +1, +4) and the NC replays say is not worth anything honestly.
* By the USER's rules (*"Either you show the reason is clairvoyance or you find that other seeds are better"*;
  *"ignore seed-noise and clairvoyant losses"*), the Morph and Burn play-setting cost is a clairvoyant loss. Every
  slower game recovers or ties at d8 b0. 1HG Morph is +1 on fresh seeds (seed noise). The repeatable Morph 2HG +38 /
  12000 disappears under the honest-reshuffle search (+1 / 8000) and under NC.

## 5. Is Heath net better at play settings now?

At play settings in coupled (lockstep) play, Heath is **not net better on Morph 2HG** (+38 turn-games over 12000,
3 lost) and is neutral to slightly worse on Morph 1HG and Burn (+1, +5, +3). Those slower games have one cause:
the fetch is now played (or, in the leaf, cracked) at a different time, and the reshuffle replaces a library the
lockstep search could see with another one. In the honest measurements (decoupled rows on 32000 games: -7, -4, +1,
+4; NC replays of all 178 changed games: 137 equal, 20 faster vs 21 slower) it is neutral to better. Every slower
game recovers at d8 b0 (112 of 113; the last ties there). The other fetchland decks keep their gains from the
land-heath report: suite play +8 all recovering, rwf Worldfire -1, d0 -239.

So the evidence supports adopting Heath under the stated bar: the Morph and Burn costs are clairvoyance. The one
open item for the USER is how much weight a repeatable coupled-search cost on one row (Morph 2HG, about 0.003
turn/game) deserves once it is shown to be clairvoyant. By the USER's rulings it is ignored, and this report
proceeds on that reading.

## Files

* `logs/heath/heath-morph/`: generators (`big_gen.py`, `big2_gen.py`, `trace_gen.py`), runners, `report.py`,
  `cls.py`, `tab.py`, and the outputs (`big_report.txt`, `big_changed.txt`, `trace_classification.txt`,
  `trace_divergence.txt`, `big2_report.txt`, `big2_changed.txt`, `t3_manifest.json`, `t3_classification.txt`,
  `t3_divergence.txt`).
* Scratch (volume): `build/scratch/heath-morph/{big,big2,t2,t3}/` (wins, per-game traces `*.cp.tr` / `*.nc.tr`,
  batch out/err).
