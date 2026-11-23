# Blood Sun: search cost and first-turn reference audit (2026-11-23)

## Reproduction and measurement

Checkout `bloodsun-analysis` at `38c911b75`, optimized Release binary, default heuristic flags.
The single-game cost samples used `decks/Blood Sun/Blood Sun.cod`, its profile, depth 5,
budget 20 ms, starting life 30, two opposing heads, one thread, and the seeds/indices below.
Diagnostics: `MTG_TURN_CENSUS`, `MTG_BF_CENSUS=1`, `MTG_NODE_KIND_ATTRIB=1`,
`MTG_LA_HIST=1`, and `MTG_PAY_REFINE_STATS=1`. The one-thread setting isolates a single
game's counters and is not the intended batch setting. Raw `census.tsv`, `err.txt`, and
`out.txt` are in `build/scratch/bloodsun/cost_profile/ship_2hg_gi{0,7,16,26}/`.

**Use deterministic work counts to compare variants.** An identical seed 2009/index 7
replay while a 12-job build contended for CPUs took 106.9 s instead of 55.7 s, but its
4,138,905 units, 936,071 scored candidates, 239,189 constructed plans, and 1,924,543
candidate-list entries were identical. The instrumented per-node times below locate
hot paths within a run; they are not a stable speed ratio between runs.

| Seed / index | Outcome | Work units | Scored candidates | Constructed plans | Revelation packages emitted | Candidate-list entries | List calls / widest | After-win entries skipped |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2002 / 0 | T3 | 276,483 | 57,508 | 86,566 | 200,797 | 116,676 | 5,941 / 1,598 | 15,042 |
| 2009 / 7 | T5 | 4,138,905 | 936,071 | 239,189 | 63,766 | 1,924,543 | 73,715 / 316 | 1,546 |
| 2018 / 16 | T5 | 1,263,287 | 375,351 | 476,834 | 1,281,599 | 22,406,569 | 11,591 / 35,103 | 21,869,276 |
| 2028 / 26 | T5 | 3,818,584 | 1,065,351 | 797,520 | 1,276,066 | 1,640,630 | 63,281 / 443 | 37,365 |

These are four different games, not paired A/B arms. `candidate-list entries` counts
every candidate in nested lists, including ones never scored. `after-win entries
skipped` counts entries a later stop declined after a same-round win; the lists had
already been built. Candidate shapes overlap: one plan can carry both a cast and a
breakpoint variant.

## Two distinct heavy regimes

Seed 2009/index 7 spends its work scoring and simulating: 936,071 candidates, 1.94
million payment calls, and 4.14 million units. Its list width is moderate (mean 26.1,
maximum 316). Seed 2028/index 26 is similar: 1.07 million scored candidates and 3.82
million units. Faster package generation alone will not remove their scoring cost.

Seed 2018/index 16 spends heavily *building* lists: 22.4 million entries, 98.46% in
lists wider than 512, but only 375,351 scored candidates. About 21.9 million entries
were already irrelevant after a same-round win. A win witness before the full list is
materialized is the main opportunity in this regime. Seed 2002/index 0 has just 16
lists wider than 512, yet those lists hold 14.64% of its candidate mass.

The list census finds literal repetition, not just two different copies of a card:

| Index | Raw entries | Distinct exact plans | Exact repeats | Repeats excluding breakpoint variants |
| --- | ---: | ---: | ---: | ---: |
| 0 | 116,676 | 94,287 | 19.2% | 3.0% |
| 7 | 1,924,543 | 1,407,092 | 26.9% | 4.4% |
| 16 | 22,406,569 | 14,761,318 | 34.1% | 34.0% |
| 26 | 1,640,630 | 1,279,016 | 22.0% | 1.5% |

This suggests an exact plan-signature dedup before list materialization, especially
for index 16. It needs a same-outcome check: the raw equality diagnostic does not by
itself establish that all source and breakpoint state is interchangeable.

The most expensive *single* decision in each sampled game was also in a bottoming
trial, which makes the trial multiplier concrete:

| Index | Decision | Work units | Scored candidates | Constructed plans |
| --- | --- | ---: | ---: | ---: |
| 0 | Trial T3, main decision | 34,798 | 12,765 | 17,575 |
| 7 | Trial T2, breakpoint resolve | 384,901 | 243,409 | 39,977 |
| 16 | Trial T2, main decision | 269,588 | 124,476 | 153,638 |
| 26 | Trial T3, main decision | 335,490 | 75,140 | 38,652 |

## Per-node cost

`MTG_NODE_KIND_ATTRIB` partitions scored nodes and deterministic work units by the
choice that made each node distinct. It splits the real game from the trial games
used to choose mulligan bottoms. In these samples, the trials account for 89.2%,
95.8%, 94.9%, and 98.1% of work units (indices 0, 7, 16, 26 respectively). That
amplifies any play-search cost inside bottom selection; it is not all spent on the
actual kept game's turns.

| Node kind, bottoming trials | Index 7 nodes | Index 7 units/node | Index 16 nodes | Index 16 units/node |
| --- | ---: | ---: | ---: | ---: |
| Ordinary casts, lands, pass | 604,375 | 3.1 | 303,303 | 1.5 |
| Breakpoint continuation variant | 335,948 | 3.6 | 74,778 | 2.9 |
| Revelation package cast | 3,966 | 2.0 | 118,370 | 2.1 |
| Extra-turn cast without package | 56,810 | 14.5 | 2,842 | 34.2 |
| Named tutor cast without package | 1,440 | 2.8 | 3,626 | 9.6 |

An extra-turn node is 4–23 times the ordinary node's *charged* work in these
samples. Index 16's Revelation package nodes occupy 20.8% of charged units but
52.4% of instrumented thread time in the trials, consistent with substantial
unpriced list-generation work. In index 7 the main unit shares are ordinary nodes
47.9%, breakpoint variants 30.6%, and extra-turn casts 20.8%.

The cards appearing in candidate entries also differ by regime. Index 7 has
Exploration in 439,695 entries, Time Warp in 161,120, and Burgeoning in 138,490.
Index 16 has Ulamog in 21.8 million entries, Emrakul in 15.8 million, Revelation
in 7.12 million, and Time Warp in 6.82 million. These counts overlap across plans;
they identify the combinations that make the lists wide, not each card's isolated
cost.

## Every saved reference at the first main phase of its winning turn

All 12 saved Blood Sun references replay to the recorded win turn. The exact human
choice prefix was replayed to the **first pre-combat main-phase decision of the
winning turn, before any extra turn**, then the default autonomous depth-5/20-ms
search took over. Raw commands, boards, and outcomes:
`build/scratch/bloodsun/ref_firstturn/result.json`.

| Reference seeds | Human win | Search from first winning-turn frame |
| --- | ---: | ---: |
| 1 | T4 | **T5** |
| 2 | T5 | T5 |
| 3 | T4 | T4 |
| 4 | T5 | T5 |
| 5 | T3 | T3 |
| 6 | T5 | T5 (reference protocol repaired one old choice) |
| 7 | T3 | **T4** |
| 8 | T7 | T7 |
| 9 | T4 | T4 |
| 10 | T4 | T4 |
| 11 | T4 | T4 |
| 12 | T4 | T4 |

Both misses win on the recorded turn at depth 5 with an unlimited budget. That
proves a same-turn line remains somewhere in the searched package waves, but not
that it is a blind-safe line or the human's exact package. Seed 1's unlimited
search drew a Time Warp after the first Warp, so its result may use future draw
knowledge. Seed 7's saved X=5 package takes Revelation, Exploration, two Lotuses,
and Time Warp at life 17; the old high-life Exploration target restriction
excluded that *exact* package, even though unlimited search found a different T3
line.

The default-on `MTG_BLOODSUN_REV_PRIORITY` recognises two visible-board bridges:
held Emrakul with Fastbond/Lotus/Time Warp and the Revelation/Exploration/two-Lotus/
Time Warp continuation. It admits the latter through a narrow Revelation-only
Exploration exception, places both in package wave 0, and skips their ordinary-wave
copies. The ordinary search still checks castability, payment, and the whole line.
The rule applies only to the current played decision turn; it leaves future-turn
lookahead and mulligan-bottom trial games on their prior package order. Early
versions changed bottoming choices and lost a Lotus or Emrakul in two games.

`test/bloodsun_reference_firstturn.py` replays every saved reference to the first
main phase of its recorded winning turn and hands off to autonomous depth-5/
20-ms search. With the new arm enabled by default, **12/12 match**, including seed 1 at T4
and seed 7 at T3. The seed-1 package is Fastbond, Lotus Field and Time Warp,
with Emrakul held and payable on the extra turn; the seed-7 package contains
Revelation, Exploration, two Lotuses and Time Warp. This checks the regular
winning turn, before any of that round's extra turns.

Paired single-head goldfish runs (same seeds and indices in both arms, d5/b20,
maximum turn 8) checked 100 tuning games and 100 held-out games. The earlier
unscoped arm had two later wins and a lost cutoff win; restricting it to the
current played turn removed those regressions. Results for the final scoped arm:

| Set | Old mean turn score | New mean turn score | Earlier / later | Wins old / new | Old / new deterministic units |
| --- | ---: | ---: | ---: | ---: | ---: |
| Tuning, seed 910000 | 5.3700 | 5.3400 | 3 / 0 | 94 / 94 | 46,012,321 / 45,899,240 |
| Held-out, seed 920000 | 5.3300 | 5.3100 | 2 / 0 | 93 / 93 | 67,911,388 / 67,808,983 |

The complete per-game `.wins` and `.units` files are under
`build/scratch/bloodsun/ref_firstturn/priority_ab/{train100_real,heldout100_real}/glog/`.
The 0.25% and 0.15% unit reductions are small; the quality result and 12/12
first-turn coverage motivate the default-on arm. Wall-clock seconds are not used
for the decision because the machine was contended.

## Next optimization checks

1. Try a pre-list same-round win witness on index 16. It can save the 21.9 million
   entries built after a win was already available, without narrowing choices in
   states where no win is proved.
2. Investigate the exact-repeat signatures in index 16 and the breakpoint twins in
   indices 0, 7, and 26. Fold only identities whose applied states and continuations
   match; compare `candidate-list entries`, scored candidates, and work units.
3. Keep separate quality/cost gates for bottoming trials and actual play. Index 7
   and index 16 have different bottlenecks, so a change that helps one can worsen
   the other.
