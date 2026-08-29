# The plain-cantrip breakpoint class: why it is worth having and why nothing has made it affordable

**Status: DIAGNOSED 2026-08-29 (was OPEN).** The USER's framing was *"I'm looking to figure out why
this falls short in either quality or performance. The latter is especially confusing."* It is
answered below, and the answer is that **the premise of the last seven attempts was wrong**: at play
settings the class does not cost more work at all. Read "THE DIAGNOSIS" first; the historical
attempt table is kept because it now has an explanation.

## THE DIAGNOSIS (2026-08-29)

At the shipped budget the two arms spend **the same units** -- 16,082,034 (site 3 closed) vs
16,220,810 (open) over 300 games at d5/20 ms, **+0.9%**. The budget is the binding constraint in
both arms, exactly as TRAP 1 says it must be. What changes is what that budget BUYS:

| committed iterative-deepening depth (300 games, d5, 20 ms) | closed | open |
|---|---|---|
| mean | **3.233** | **2.819** |
| committed at depth 1 | 10.2% | **19.7%** |
| committed at depth 5 | 22.1% | 15.3% |

**Opening the class costs 0.41 plies of committed search depth**, and nearly doubles the share of
decisions that get only one ply. It buys breadth at the breakpoint and pays in depth, and on Hinata
depth is worth more -- which is the whole of the budgeted regression (quality over those same 300
games: 5.7233 closed, 5.7533 open, matching the paired -0.023 / -0.031).

So the class is not EXPENSIVE at play. It is **STARVED**. "Make it cheaper" was never going to work,
because it is not the thing consuming the budget -- and that is why condemnation, key-narrowing,
peer-splitting, the DEFER lever and the apply-time partition each returned ~1% or nothing.

## THE CROSSOVER (the price of the class)

If the class is starved, a crossover budget must exist. `gen_site3_budget_manifest.py`, one pooled
batch, 2 arms x 4 budgets x 2 blocks x 1200 games = 19,200 games, paired on the game index:

| budget | hold delta | t | train delta | t | mean per-game work |
|---|---|---|---|---|---|
| 20 ms (shipped) | **-0.0233** | -2.15 | **-0.0292** | -2.68 | 1.49x / 1.51x |
| 40 ms | -0.0117 | -1.17 | -0.0067 | -0.69 | 1.64x / 1.70x |
| **80 ms** | **+0.0025** | +0.27 | **+0.0017** | +0.18 | 1.87x / 1.93x |
| 160 ms | **+0.0267** | +3.69 | **+0.0133** | +1.75 | 1.99x / 2.16x |
| unbudgeted | +0.0492 | +6.59 | +0.0350 | +5.31 | 2.93x / 2.82x |

(delta > 0 = the class wins SOONER = better.)

**The crossover is 80 ms -- 4x the shipped budget -- and the two blocks agree to the cell.** The
curve is monotone in budget on both blocks across five points, which is about as clean as this
harness gets.

It is not merely that more budget helps: the control arm SATURATES and the class does not. Raw avgs,
hold block: control 5.6658 / 5.6258 / 5.6100 / 5.6042 across 20/40/80/160 ms (gaining 0.062 over the
whole range, nearly all of it by 80 ms), class 5.6892 / 5.6375 / 5.6075 / 5.5775 (gaining 0.112 and
still falling). At 160 ms the class beats the control's best measured play by 0.027.

**Two work columns, and conflating them is how this arc lost time.** `units` is the BLOCK TOTAL,
which the tail games dominate and the budget caps -- at 20 ms it is -2.2% / -2.9%, i.e. the class
consumes no more of the budget than the control. `ratio` is the MEAN PER-GAME ratio -- 1.49x at
20 ms, which is the "budgeted 1.49x" quoted earlier in this arc. Both are correct and they measure
different things; the per-game figure is higher because the class's games run longer (more decisions,
each capped) while the monster games, which own the block total, are capped identically in both arms.

## Where the work actually goes (per-site unit partition)

New instrument (`MTG_ROLLOUT_STATS`, `units.*` lines): one counter per `SearchBudget::Consume()`
site, charged only when the unit is charged, so **the buckets sum exactly to the units `cost.py`
reports** -- verified to the unit against the batch `.units` on four games. Before this, `units` was
one opaque scalar and every diagnosis in this arc was guess-and-check.

Unbudgeted d5, single games reproduced from the s3q blocks, all IDENTICAL PLAY in both arms:

| game | arm | total units | fs_main2 | fs_pre | fs_bp_wave | rollout |
|---|---|---|---|---|---|---|
| hold gi=92 | closed | 2,639,297 | 60.0% | 39.9% | -- | 0.03% |
| | open | 35,037,851 | 40.5% | 20.3% | 39.2% | 0.004% |
| hold gi=697 | closed | 4,971,794 | 68.3% | 31.7% | -- | 0.008% |
| | open | 41,749,134 | 44.6% | 22.0% | 33.4% | 0.002% |
| train gi=724 | closed | 7,919,071 | 55.6% | 34.6% | 9.8% | 0.006% |
| | open | 65,052,233 | 45.4% | 25.2% | 29.5% | 0.001% |
| hold gi=268 | closed | 5,780,126 | 68.1% | 31.9% | 0.02% | 0% |

Four things fall out, and they retire several standing beliefs:

1. **Rollouts are 0.0-0.03% of the work.** The value sidecar made the horizon leaf O(1), so
   essentially every unit is an interior plan application. The 2026-07-31 framing "+113% interior
   nodes for +2% rollout calls" was therefore never a trade-off to be balanced: interior nodes ARE
   the cost and rollout calls are free. **This is the measurement the previous session's handoff
   named as the one to run, and it does explain the arc -- just not the way it predicted.**
2. **`g_interior_nodes` -- the counter every prior diagnosis was read off -- covers only `fs_pre`**,
   i.e. 20-40% of interior nodes. It is blind to the second-main loop, the LARGEST bucket. "Interior
   nodes flat while rollout calls rose 22%" was read off a counter that could not see 60% of the
   tree, and off a rollout that is 0.03% of the cost.
3. **The second main is the biggest bucket in the control arm** (55.6-68.3%). It is not site-3
   specific and it is the largest single affordability target on this deck.
4. **61-70% of the class's growth is OUTSIDE the breakpoint wave.** Opening site 3 adds `fs_bp_wave`
   (29-39% of the new total) but also multiplies the ORDINARY main-phase loops 5.5-9.0x, because a
   breakpoint variant is itself a plan in `pre` and everything below it multiplies. A prune that
   deletes breakpoint CANDIDATES cannot reach that. **This is the mechanical answer to "condemnation
   deletes 41.5% of payable re-offers and returns ~1%".**

## REFUTED here, so it is not re-tried

* **The enum-memo cap is not the cost.** Site 3 open collapses the plan-enumeration memo on gi=92
  (hit rate 57.1% -> 33.3%, clears 34 -> 489) -- the exact thrash signature
  `fivecolour-search-waste.md` records, whose note says the unbounded case "remains unmeasured".
  Measured now: raising `MTG_ENUM_MEMO_CAP` 8192 -> 1,048,576 lifts the hit rate to 56.9% and drops
  clears to 2, and total units are **byte-identical** (35,037,851 at every cap). The collapse is
  also not universal -- on gi=697 the hit rate IMPROVES with site 3 open (72.9% -> 79.8%).
* ...which exposes an instrument caveat worth its own line: **units count plan APPLICATIONS, so
  neither `cost.py` nor `SearchBudget` charges for ENUMERATION.** An enumeration blow-up is
  invisible to both, and cannot be measured in units at all -- only in wall time on a quiet box.

## OPEN -- a measurement-integrity question raised by this work

The open arm's single-game units are **systematically below** the same game's units inside a
1,200-game batch: gi=92 35,037,851 vs 38,748,236 (-9.6%), gi=697 41,749,134 vs 50,058,929 (-16.6%).
The closed arm matches **to the unit** on all four games. `ClearPerGameCaches()` clears the three
thread-local memos, so the documented cross-game-residue cause should be closed. Until this is
explained, per-game units may carry an ARM-DEPENDENT batch bias, and every ratio in this arc rests
on them.

## The one-paragraph statement (historical)

Opening the plain-cantrip breakpoint class (site 3) lets the search decide what to cast AFTER a
cantrip's draw instead of guessing. It is a **genuine quality win** and it costs **~3x the search
work** *when both arms are allowed to run to depth 5*. Under the shipped 20 ms budget neither arm
gets that far; the class simply commits shallower. Every attempt to make the class cheaper has
returned ~1% or less. The open question is not whether the class is correct -- it is -- but what
budget it needs.

## The measurements that matter

Hinata2, revised cast order (`MTG_HINATA_ORDER_FULL`), greedy continuation deleted
(`MTG_BP_NO_GREEDY_CONT`), 1,200 games per cell, paired on the game index. `ng` = site 3 closed,
`s3_ng` = site 3 open.

| | quality delta (+ = better) | t | work |
|---|---|---|---|
| budgeted (d5, 20 ms), hold | **-0.0233** | -2.15 | 1.49x |
| budgeted (d5, 20 ms), train | **-0.0308** | -2.84 | 1.52x |
| **unbudgeted** (d5, budget_ms=0), hold | **+0.0492** | **+6.59** | **2.93x** |
| **unbudgeted** (d5, budget_ms=0), train | **+0.0350** | **+5.31** | **2.82x** |

**The sign reverses.** Unbudgeted the class is 60 games faster : 6 slower (hold). This reproduces
the 2026-07-31 result in `cantrip-first-collapse.md` (100 games: class OFF 5.8900, class ON 5.8400)
on the current engine at 12x the sample. There is no ranking defect and no search bug at the site;
the budgeted regression is pure dilution.

Note the budgeted work figure (1.49x) is itself an artifact -- the budget caps it. The real cost of
the class is the unbudgeted **~2.9x**.

## What has been tried, and what each returned

| attempt | result | why it fell short |
|---|---|---|
| **Breakpoint condemnation** (`MTG_BP_CLASSIFY`) | **-0.53% / -0.58%** work | Fires hard -- 41.5% of payable re-offers -- but the class's cost is not re-offers |
| Condemnation, leaf leak fixed (`MTG_BP_CONDEMN_SEARCHED_ONLY`, now default ON) | 0% -> ~1% | The leak was consuming the entire saving; correct, but the total is still ~1% |
| Finer cast order (peer splitting) | ceiling **~2.3x** drops | Would take ~1% to ~2-3%. Not the missing order of magnitude |
| Cache-key narrowing (`MTG_BP_KEY_NARROW`) | enum misses -5.9% | Recovers fragmentation condemnation itself causes. Small, and has an unresolved hazard (below) |
| **Segments / partition** (`MTG_BP_PARTITION_CANTRIP`) | quality neutral, **work +15%** | Truncates at APPLY time; the enumerator still derives every tail and the result is discarded |
| `MTG_BP_SITE3_DEFER` | **provably inert** (identical digest) | Site 3's cost is `class_on` off the FULL mask; DEFER only clears a wave-0 bit |
| Width / `MTG_BP_MAXBASE` / wave ranks | measured NOT the answer (2026-07-31) | Width is a cost prune only; at unlimited budget W=2,4,8 converge |

## Why the performance side is confusing -- the honest state

Condemnation deletes **41.5% of payable re-offered candidates** and returns **~1% of work**. Those
two numbers are hard to hold together, and no explanation offered so far has survived:

* *"It is cache fragmentation"* -- partly true and measured (condemnation folds the whole pre-draw
  hand into the breakpoint enum key, taking full derivations 218,586 -> 240,439, **+10%**), but
  narrowing the key recovers only ~6% of enumerations, not the missing order of magnitude.
* *"It is the budget reinvesting the saving"* -- true and important (see TRAPS), but the ~1% figure
  is from the identical-play subset of an UNBUDGETED run, so it survives that correction.
* *"The plan space is re-derived per continuation"* -- the memo already absorbs **86.4%** of that
  (hits 1,392,167 / misses 218,586).
* *"Segments will flatten it"* -- built and measured; apply-time truncation costs 15% MORE work.

**The unexplained gap is the live question.** A candidate framing not yet tested: the class's cost
may not be in the continuation's plan LIST at all but in the nested re-solve it triggers -- the
2026-07-31 measurement was **+113% interior nodes for +2% rollout calls**. If the cost is interior
expansion, then pruning CANDIDATES is attacking the wrong axis, and no amount of condemnation or
ordering will move it. That would explain every result in the table above with one mechanism, and it
is directly testable: measure interior nodes per continuation, not candidates per continuation.

## Where the work actually is (do not skip this)

The unbudgeted per-game work distribution on Hinata is extraordinarily skewed:

* median game **51K units**; worst game **780M units** (~15,000x)
* **top 10 games = 70.2% of the hold block's entire work** (61.3% on train)
* work scales ~3x per win turn; turn-7 games are 8% of games and 50% of hold's work
* one game (hold gi=717) ran **3.93 HOURS** and was 27.1% of the block by itself

Condemnation's ~1% is spread over the median game and does essentially nothing on the ten games that
ARE the work (per-game ratios 0.98-1.00 on the top eight). **Any affordability fix should be judged
on the tail, not the mean.** gi=717 is 110x its own class median even at d3, so the outlier is
structural, not a depth artifact -- and it has never been root-caused. That may be the highest-value
unexamined lead in this whole arc.

## The next concrete step -- A USER DECISION, not a measurement

The measurement is finished. What is left is a choice only the USER can make, because it trades a
deck's play budget against its play quality:

* **Adopt the class and raise Hinata's play budget to 80-160 ms.** At 160 ms it is +0.027 (hold,
  t=+3.69) and +0.013 (train) over the control at the SAME budget, and 0.088 better than today's
  shipped play. Cost: ~2x the mean per-game search work, and a play-policy change for this deck.
* **Leave the class closed at 20 ms.** Today's behaviour. Correct under the current budget: the
  class loses 0.0233 / 0.0292 there and no prune will change that, because it is not work the class
  is wasting, it is depth it cannot afford.

Do NOT spend more effort pruning the class to fit 20 ms. Five separate levers have now been measured
against a premise this doc shows is false.

If the budget is NOT to be raised, the affordability work that would move the crossover downward is
aimed at the **second main** -- 55.6-68.3% of ALL units on this deck, and not site-3-specific, so it
would make every Hinata decision cheaper rather than making one class cheaper. That is a separate
project and it has never been examined.

## The old next concrete step (kept for the exemption list)

The enumeration-level partition, i.e. the **cantrip-first collapse** of
`cantrip-first-collapse.md`, which was specified in 2026-07-31 and never built. Apply-time
truncation failed because the enumerator still derives the tails; the collapse stops emitting casts
after the turn's first cantrip so they are never derived. It needs the three exemptions that doc
records, or it deletes real lines, because cantrip-first is NOT pure dominance:

1. **mana acceleration** (a ritual/rock whose float funds the cantrip -- Reality Spasm),
2. **cost reduction** (Hinata makes the cantrip or a later spell affordable),
3. **cast-triggered payoffs** (Guttersnipe/Vivi-style: they must precede the cantrip or the trigger
   is wasted). Not live on Hinata2, but the rule must not assume that.

Before building it, run the interior-nodes-per-continuation measurement above. If the cost is
interior expansion rather than enumeration, the collapse will return as little as everything else
did, and the effort belongs on the tail games instead.

## Instruments built for this (all default OFF, all byte-identical unset)

| flag / tool | what it answers |
|---|---|
| `MTG_BP_CONDEMN_WHYNOT` | why each consultation did NOT drop; prints a realistic + hard-upper-bound ceiling |
| `bp_condemn` three-way split | searched / exec / rollout drops. `exec=0` on Hinata: the executor always had a searched continuation |
| `MTG_BP_KEY_NARROW` | narrows the breakpoint enum cache key to the snapshot's intersection with the current hand |
| `MTG_BP_PARTITION_CANTRIP` | the apply-time partition; carries the deferred-resolve loop |
| `MTG_BP_ENUM_PROBE` | breakpoint enum hits/misses/clears -- the memo's real hit rate |
| `test/tools/kitty_ab/prune_cost.py` | paired work units over the IDENTICAL-PLAY subset |
| `test/tools/kitty_ab/ordv_report.py` | paired quality+work for `<arm>.<deck>.<block>` manifests |
| `test/tools/kitty_ab/gen_condemn_unbudgeted_manifest.py` | prices a prune at `budget_ms=0` |

## TRAPS -- every one of these produced a wrong published number in this arc

1. **Never price a prune under a budget.** `SearchBudget` is denominated in the same GameWorkMeter
   units `cost.py` reports and iterative deepening reinvests anything freed, so a budgeted search
   never RETURNS a saving. Condemnation read +0.01% budgeted and -1% unbudgeted. The USER caught
   this from first principles TWICE (2026-08-26, 2026-08-29): *"How can it do anything but less
   work?"*
2. **Never compare per-game units across games whose PLAY DIVERGED.** Once a prune changes a
   decision the arms play different games. On hold, ONE game (gi=717) was 27.1% of block units and
   turned a real -0.53% saving into a reported +7.98% COST. Split on the `.wins` digest and read the
   identical-play subset (`prune_cost.py`).
3. **A chunked manifest job needs `seed = base_seed + game_index`**, not the block base. Setting the
   base replays game 0; a depth sweep silently compared one game to itself six times.
4. **Mirror a rollout predicate CLAUSE FOR CLAUSE.** The partition's executor twin dropped
   `tmpl == CardTemplate::DrawSpell` and so truncated after every cast rather than every cantrip --
   46 of 60 games worse, and five successive hypotheses were tested and refuted before one game log
   showed it in a minute. **When an A/B moves more than the mechanism can plausibly explain, read
   the line before theorising about the search.**
5. **`MTG_BP_KEY_NARROW` has an unresolved correctness hazard** and must not be defaulted on without
   digest verification: "not in hand now" is not "unreachable". Hinata plays Distorting Wake, Memory
   Lapse, Remand and Izzet Boilerworks, all of which return cards to hand, and a returning snapshot
   entry would make two states wrongly share a cache entry.
6. **Grep `docs/design/` before proposing anything here.** "The searched continuation at site 3 is
   worse than the greedy one it replaces" was asserted in this arc and had already been measured
   false in 2026-07-31; the unbudgeted run above only re-confirmed it.

## Settled, do not re-litigate

* Deleting the greedy continuation is FREE on its own (`ng` vs `ord`: +0.0000, t=0.00, 4 fast/4
  slow on hold). The cost is opening site 3, not deleting greedy.
* Condemnation must not fire in the ROLLOUT LEAF (USER: *"it is intended for search"*). Fixed,
  `MTG_BP_CONDEMN_SEARCHED_ONLY` default ON, byte-identical on every shipped config.
* The surviving site exemption (`BpSiteAddedMana` / `MTG_BP_CONDEMN_MANA_SITE_EXEMPT`, "bug 7") is
  provably INERT on Hinata -- breakpoint sites are draw effects and neither Reality Spasm nor
  Irencrag Feat draws. It is a Mirrorwing/Gold Rush rule. It should still be deleted for
  consistency with "no general exemptions", but it is not Hinata's limit.
* All four candidate-side TYPE exemptions are deleted, replaced by the card-agnostic
  `BpTurnManaSettled` (re-admit once the mana base grows). Kitty and Mirrorwing smoke-tested clean.
