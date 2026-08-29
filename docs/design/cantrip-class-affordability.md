# The plain-cantrip breakpoint class: why it is worth having and why nothing has made it affordable

**Status: OPEN, restarting from scratch (USER, 2026-08-29).** The USER's framing: *"I'm looking to
figure out why this falls short in either quality or performance. The latter is especially
confusing."* This doc is the self-contained state of that question. Read it before re-deriving
anything; several of the attempts below were made twice already.

## The one-paragraph statement

Opening the plain-cantrip breakpoint class (site 3) lets the search decide what to cast AFTER a
cantrip's draw instead of guessing. It is a **genuine quality win** and it costs **~3x the search
work**. Under the shipped 20 ms budget that 3x is rationed, the win inverts into a loss, and every
attempt to make the class cheaper has returned ~1% or less. The open question is not whether the
class is correct -- it is -- but why nothing recovers its cost.

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

## The next concrete step

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
