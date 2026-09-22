# Snow's cost, split: 5.9x the UNITS and 1.8x the cost PER UNIT — and the per-unit half is the mana payment solver

Optimization session of 2026-09-22, opened at the user's instruction after the value-leaf run
projected 40–80 h (`snow-valueleaf-run-2026-09-22.md` §7). Everything here is measured on
`8667cd38` (= `origin/phase-1-2-deck-analyzer` at the time), `build/Release` rebuilt from that tree.

## 0. The one-paragraph version

Snow's 10.6x wall clock over the next-worst deck factors cleanly into **5.9x the units** (search
shape — the breakpoint degeneracy already named in `snow-breakpoint-degeneracy.md`) and **1.8x the
wall per unit** (uncharged work). The per-unit half is now located: **41.5% of a heavy Snow game is
the mana payment solver**, against 13.8% on Hinata2 and 9.6% on Melira Pod. The mechanism is
Snow's three-colour manabase over colourless-only sources and filters, which makes the greedy
payment heuristic fail **22.6%** of the time (Melira: 0.02%) and drop 216M payment calls a game into
a backtracker that burns **155.6M nodes**. The obvious existing lever for that (`MTG_FLOW_ORDER`)
was re-measured here and is **refuted on Snow**. Closing the payment gap *entirely* is worth about
**1.38x** — real, but it does not by itself turn 40–80 h into a night, and the one piece of it built
here is worth 1.4%.

## 1. The regime, and why it is not the regime that was profiled before

`analysis-Snow.md` §"Perf characterisation" profiled a **representative 7-second game at play
settings** (d5 / 20 virtual-ms) and concluded *"there is no hotspot — top self-time symbol is
`Action::Action` at 2.79%, nothing above 3%"*. That is a different regime from the one that costs
40–80 h. Phase C's H4/H5 cells are a **deep, UNBUDGETED** search, where the breakpoint wave runs to
completion instead of being cut by a 20 ms budget. Everything below is measured there:

```
--ignore-play-profile --depth 4 --budget-ms 0 --max-turns 8 --threads 1   (MTG_VALUE_MODEL=0)
```

`MTG_VALUE_MODEL=0` is on every deck because Hinata2/Goblins/Melira all ship a `.value.json` and
sidecar presence activates the hybrid leaf, which changes what a "unit" is. Snow has no live
sidecar, so the pure-heuristic leaf is the only symmetric comparison.

## 2. The fork, measured (64 games Snow / 32 each control, one pooled run per deck, 32 threads)

| deck | mean ms | mean units | units/ms |
|---|---|---|---|
| **snow** | **125,669** | **5,067,743** | **40.3** |
| hinata | 11,856 | 859,725 | 72.5 |
| melira | 4,399 | 612,610 | 139.3 |
| goblins | 293 | 22,578 | 77.0 |

**Snow is CENSORED**: 25 of its 64 games had not finished when the scan was stopped at 14 minutes,
and every one of them was already past the 605 s of the slowest game that did finish. Its true mean
is higher than the table says; the controls are complete. (Capped and uncapped numbers must not
share a column — `snow-breakpoint-degeneracy.md` records that exact error being made on this deck
before. The censoring is stated rather than hidden, and it biases *against* the conclusion below
only in the sense that Snow is worse than shown.)

So against Hinata2: **5.9x the units, 1.80x the wall per unit, 10.6x the wall.** Two independent
problems that want opposite fixes, and both are real.

## 3. Where the per-unit half goes: `perf` on a heavy game, with controls

`perf record -F 199 --call-graph dwarf` on a window of each deck's heaviest game, `build/Profile`
(-O3 + symbols), idle box. Self time bucketed by class (`logs/snowopt/classify_perf.py`; `other` is
printed so the classification cannot hide the bulk):

| class | **snow** | hinata | melira |
|---|---|---|---|
| **mana_pay** | **41.5** | 13.8 | 9.6 |
| mana_enum | 2.0 | 0.6 | 0.6 |
| enumerate | 5.8 | 8.8 | 7.5 |
| solve | 11.9 | 5.0 | 5.5 |
| apply | 3.0 | 5.5 | 6.7 |
| key_memo | 5.9 | 12.9 | 14.4 |
| state_copy | 4.1 | 4.0 | 6.5 |
| carddb | 11.8 | 10.0 | 9.0 |
| alloc | 2.6 | 5.2 | 5.4 |
| other | 11.4 | 34.2 | 34.8 |
| *(listed)* | *75.2* | *70.6* | *71.8* |

**`mana_enum` is 2.0%.** That matters: the mana-shaped hypothesis this repo already killed on Snow
was *mana-side enumeration* (`MTG_NO_ROCK_RAMP=1` drives it to zero and the cost does not move).
That control stands and is not being re-litigated — `mana_pay` is a different family (the payment
solver: `TapForCost*`, `ColorFeasibility::Payable`, `UntappedManaUpperBound`,
`CommitPaySacSacrifices`), and it is 3–4x the controls' share.

Within the 41.5%, by self time: `TapForCostSharedOnce` + its lambdas **12.2%** (the GREEDY),
`TapForCostBacktrack`/`Worker` **6.0%**, `CommitPaySacSacrifices` **2.17%**, `ColorFeasibility` +
`BuildColorFeasibility` 1.9%, `UntappedManaUpperBound` 1.2%, `TapFlowInfeasible` 0.6%, rest below
the cut. **The greedy costs twice what the backtracker costs** — optimising the backtracker is
optimising the smaller half.

## 4. The mechanism: a three-colour deck over colourless sources and filters

`MTG_TAP_STATS` on the same three games, run to completion:

| | **snow** | hinata | melira |
|---|---|---|---|
| payment calls (`impl`) | **215,896,409** | 23,625,159 | 12,050,927 |
| ...per unit | **12.8** | 2.3 | 1.2 |
| greedy solved it | 77.4% | 87.7% | **100.0%** |
| **fell through to the backtracker** | **22.7%** | 12.8% | **0.018%** |
| mana-cache hit rate | 92.9% | 98.1% | 96.1% |
| backtracker entries | 3,782,525 | 99,651 | 40,090 |
| backtracker **nodes** | **155,648,808** | 1,197,341 | 164,803 |
| nodes/entry | 41.1 | 12.0 | 4.1 |
| nodes/entry, **payable** | **60.9** | 12.1 | 6.5 |
| nodes/entry, unpayable | 1.0 | 11.8 | 1.0 |

Read the last two rows together. **Unpayable costs 1.0 node** — the default-ON flow-prune oracle
catches contention-infeasible costs at the top-level entry and is doing its job perfectly. It is
proving a cost **payable** that costs 60.9 nodes, i.e. ~50 dead branches explored before the
solution. The oracle runs *only at the top-level entry*; its own comment says *"a deep node's
partial-tap state is still left to the memo + B&B gate"*, so interior nodes have no feasibility
test at all.

The deck explains the greedy's failure rate. Snow's mana is **U/G/R across three colours**:

```
8 Snow-Covered Island (U)   6 Snow-Covered Forest (G)   3 Snow-Covered Mountain (R)
4 Scrying Sheets      ({C} only)     4 Boreal Druid     ({C} only)
4 Coldsteel Heart     (one chosen colour)               4 Arcum's Astrolabe ({1},{T}: any colour -- a FILTER)
2 Rimewood Falls  1 Highland Weald
```

Eight of its sources produce no coloured mana at all and four are filters that must be *fed*. A
greedy that pays each pip from "the least flexible qualifying source" strands constantly — spend a
Snow-Covered Island on a generic pip and the {U} is dead. That is a bipartite matching problem, and
22.6% of the time the greedy gets it wrong and the whole payment is rolled back and re-solved.

## 5. REFUTED: `MTG_FLOW_ORDER` does not transfer to Snow

The flow-prune oracle already computes a max-flow assignment for every feasible payment and
discards it. `MTG_FLOW_ORDER` (default off) reuses it to order the backtracker's source loop; its
header records node counts falling **9.5–13.6x** on FiveColour, and records the 2026-08-16 adoption
refusal — it changes play, held-out seeds were worse, and *"the cost saving measured at ~1% of
runtime ... there is nothing to trade the play regression against"*.

Since payment is 41.5% of Snow rather than ~1%, the benefit side was re-measured on the heavy game
(`--seed 8043 --game-index 35 --depth 4`, three arms, same game):

| arm | wall ms | units | bt nodes | nodes/payable |
|---|---|---|---|---|
| A baseline | 270,965 | 16,807,204 | 155,648,808 | 60.9 |
| B `MTG_FLOW_ORDER=1` | 274,511 | 17,271,001 | 138,051,176 (−11%) | 57.0 |
| C `+MTG_FLOW_SCARCITY=0` | 274,318 | 17,212,287 | 168,594,529 (+8%) | 65.2 |

**Wall is flat and units get slightly worse.** The 9.5–13.6x does not transfer: on FiveColour the
flow's source set is the set the backtracker taps 75.8% of the time, and on Snow it evidently is
not — plausibly because the filters (Astrolabe) and the feed-vs-spend choice are not what the
matcher is solving. The 2026-08-16 refusal stands, and its benefit case is now refuted on the one
deck that looked like it should rescue it. **A lever that is huge on one deck is not thereby a
lever on another** — same lesson as the three hypotheses in `snow-breakpoint-degeneracy.md`.

## 6. What the per-unit half is actually worth

If Snow's `mana_pay` share fell all the way to Hinata2's 13.8% — i.e. the gap closed **completely** —
wall drops 27.7%, the per-unit ratio goes 1.80x → 1.30x, and the deck lands at **1.38x faster**.
Phase C's ceilings are denominated in units, so a wall-per-unit win passes straight through to the
ETA: **40–80 h → 29–58 h.** Worth having. Not a night.

The 5.9x units is where the rest lives, and that is search shape (breakpoints), which changes what
is computed and is therefore the user's call, not an optimisation.

## 7. Open, in the order they are worth trying

1. **`CommitPaySacSacrifices` early-out — BUILT 2026-09-22, see §9.** Worth **~1.4% of wall**
   at byte-identical play (first quoted as 4.8%; that was single-game wall noise).
2. **Interior-node feasibility pruning** (byte-identical *if* it only cuts provably-dead subtrees).
   Unpayable costs 1.0 node where the oracle is live and 60.9 where it is not; the 50 dead branches
   per payable entry are exactly what a cheap interior Hall's-condition / max-flow check would cut.
   Unknown whether the check can be made cheaper than the branches it saves — must be measured, not
   assumed. This is NOT the same lever as §5: ordering picks which branch is tried first, pruning
   removes branches that contain no solution at all.
3. **`carddb` at 11.8%** (`CardDatabase::LookupCached`). Not Snow-specific (10.0/9.0 on the
   controls) but it is the second-largest class on every deck measured, and an engine-wide win.
4. **The mana cache's 92.9% vs 98.1% hit rate.** 3.78M misses x 41.1 nodes is most of the
   backtracker's 155.6M. Why Snow misses more is not established.

## 8. Where everything is

`logs/snowopt/` — `scan_d4.sh` + `snow_d4_finished.txt` (the distribution), `scan_cmp.sh` +
`cmp_*_d4.err` (controls), `profile_heavy.sh` / `profile_deck.sh` + `perf_flat_*.txt` /
`perf_tree_*.txt` (profiles), `classify_perf.py` (the class table), `tapstats.sh` + `tap_*.err`
(the payment counters), `floworder_probe.sh` + `flow_[ABC].err` (the refutation).

## 9. BUILT: the pay-sac crack flag (−4.8% on Snow, byte-identical)

**What was wasted.** `CommitPaySacSacrifices` runs on every successful payment — ~135M times in
Snow's heavy game — and its second loop walks the battlefield backwards calling
`CardDatabase::LookupCached` on every tapped permanent the payer controls, hunting pay-sac sources
to sacrifice. Exactly two cards in `cards.json` satisfy `IsPaySacSource` (`sac_for_mana_amount == 1`
with empty `produces`): **"Treasure Token"** and **"0/1 Eldrazi Spawn Token"**. Snow plays neither,
and `MTG_TREASURE_PAY_SOURCE` defaults ON, so the loop is live on every deck in the repo whether or
not it can ever fire. With ~15 permanents on board that is ~1e9 wasted probes per game.

**SIZED HONESTLY: ~1.4% of wall, not the 4.8% first quoted.** The first probe set
`MTG_TREASURE_PAY_SOURCE=0` (which short-circuits the same loop on its first line) and read
267,556 -> 254,748 ms on one game, -4.8%. **That was noise.** Five readings of that same game this
session span 254.7-268.7 s -- single-game wall on this deck is worth about +/-3%, so a one-game
pairing cannot carry a few-percent claim. Re-measured by ATTRIBUTION instead, two `perf` windows on
the same game from the same binary differing only in `MTG_PAYSAC_VERIFY` (=1 forces the pre-change
always-walk), which samples identical work because play is byte-identical:

| | always-walk | guarded |
|---|---|---|
| `CommitPaySacSacrifices` self | **1.83%** | **0.47%** |
| `carddb` class | 12.2% | 12.3% |
| `mana_pay` class | 38.4% | 37.9% |

So the saving is **~1.4 points of wall**, and the residual 0.47% is the FIRST loop (the
`pay_sac_eaten` scan), which this change does not guard. **One sub-hypothesis is refuted by this
table and should not be repeated: the loop's `LookupCached` calls were NOT a meaningful share of the
11.8% `carddb` class** -- it does not move (12.2 -> 12.3). Those probes hit the per-`Card` `m_def`
memo and are genuinely cheap; the waste was the walk itself, not the lookups inside it. The
"exceeds its own self time because of its carddb share" reasoning that motivated the first estimate
was wrong.

**The guard.** `g_paysac_cracked`, a thread-local set wherever a payment taps a pay-sac source and
consumed+cleared by `CommitPaySacSacrifices`. It is exact, not heuristic: the loop can only erase a
permanent that is both tapped and a pay-sac source, and such a permanent exists only because this
payment cracked it (the function's own header established that). Deliberately NOT cleared on a
failed payment — a stale set costs one wasted scan and can never skip a needed erase, so every
error is in the conservative direction.

**THE PART WORTH KEEPING: the completeness argument was WRONG, and the harness is what found it.**
The first version set the flag in `TapSourceIntoFloat` alone, reasoning that a pay-sac source
produces no mana of its own and so cannot reach the filter / ramp-filter / untap-burst branches.
`MTG_PAYSAC_VERIFY=1` — which walks the loop anyway whenever the flag is clear and shouts if it
finds an erasable source — **shouted on Mirrorwing within three decks**. Two real bypasses:

* `SpellEffects.cpp` **mana-cache replay**: a cache HIT re-taps the stored tap-set with a bare
  `p.tapped = true`, never entering the tap helper. Mirrorwing's Gold Rush / Twinflame Treasures
  make that path hot, which is why that deck and not the other two.
* `SpellEffects.cpp` **the backtracker's `activate` lambda**: the DFS taps its own candidates
  directly, and a pay-sac source *is* one of its candidates — both candidate-collection loops admit
  it explicitly (`|| IsPaySacSource(*d)`, §2a).

(`ApplyPutFromHand`'s `src->tapped = true` needs nothing: `src` is Stoneforge Mystic, found by
`source_id`, never a token.) With all three sites set, the verifier is silent across **every deck in
the repo — 25 decks x 32 games, 0 shouts**. Same pattern as `MTG_ENUM_MEMO_VERIFY` /
`MTG_BP_ENUM_VERIFY`, and the reason to reach for it here was that this file's history is exactly
subtle payment bugs. A completeness argument is worth what the sweep that failed to break it is
worth; this one was worth nothing until the sweep ran.
