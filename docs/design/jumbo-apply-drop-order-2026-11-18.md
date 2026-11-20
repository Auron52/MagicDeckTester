# Jumbo Cactuar: unlock-order domain and trailing equips in the walk (`MTG_PO_UNLOCK_ORDER`, `MTG_SEQ_TRAILING_EQUIP`), 2026-11-18

Status: IN PROGRESS (measurements below are filled in as they land).

Follows `docs/design/jumbo-tooth-and-nail-enum-2026-11-18.md` ("Not fixed" items 1 and 2).

## Family 1: APPLY-DROP-ORDER (gate 291; slice gi 115000-116999: 92 flags / 32 decisions / 24 games)

### Mechanism (verified)

Both apply worlds realise a plan's casts in the order `ApplyPayableCastOrder` returns: the given order (rank sort,
the deck's arrangement, the range ladder) unless the payable-order decider `PayableCastOrderCoreImpl`
(ManaPayment.cpp) finds it unpayable and finds a payable permutation. The decider

* admits an order only inside a producer domain (Treasure makers, rituals, domain wideners, land Auras, sac fodder);
* walks an aggregate pool (`FirstUnpayablePosImpl`) that knows nothing about a haste-unlock equip; and
* fails fast on an order-free bound (`bound.CanPay(total)`) that credits no unlock either.

A Jumbo line whose middle cast is paid by a mana creature that a same-line haste unlocks (Lightning Greaves / Chariot
of Victory equipped onto a fresh Arbor Elf / Voyaging Satyr, fired by `TurnSolver::ApplyManaUnlockEquips` the moment
both pieces are on the battlefield) is therefore either outside every domain, or inside the land-Aura domain and
refused by the bound or the walk. The decider returns false, the rank order stands, and the cast the unlock should
have paid is dropped. Examples (slice, `MTG_MANA_ORACLE=2`):

* s30115214 T3: board Forest#10 + Wild Growth, Lightning Greaves; plan {GSZ(X=1) -> Arbor Elf, Wild Growth -> #10,
  equip Greaves -> the fetched Elf}. The rank order casts Wild Growth first and GSZ is one short; GSZ first pays off
  the Forest's {G}{G}, the Elf enters, Greaves hastes it, and its re-tap pays Wild Growth.
* s30115140 T3: {Overgrowth -> #10, Arbor Elf, equip Chariot}: Overgrowth first takes all three lands and the Elf is
  dropped; Elf first, Chariot equip, the hasted Elf re-taps the Wild Growth Forest for Overgrowth.

The enumeration's sequential walk (`SeqWalkFixedOrder`) already models the unlock (it fires the same
`ApplyManaUnlockEquips` at the apply's points; `MTG_SEQ_UNLOCK_EQUIP`), which is why the plan reached the menu at all.
The hypothesis in the earlier doc (an unlock-piece producer domain plus `ApplyManaUnlockEquips` semantics in the
order walk) is right in substance; the change below reuses the enumeration's own walk rather than teaching the
aggregate walk a second model of the unlock.

### Change: `MTG_PO_UNLOCK_ORDER` (heurarm `PO_UNLOCK_ORDER`, default ON)

The UNLOCK-ORDER domain: a line holding a same-line haste for a mana creature -- a plan unlock equip
(`SeqWalkUnlockEquip`), or an enters-haste watcher cast (Surrak and Goreclaw) beside a mana-dork cast -- with only hand
casts in its order (no Vial put, no reveal surcharge). In that domain orders are priced by `SeqWalkFixedOrder` with the
plan's unlock (and trailing) equips in its `sel`: the given order when it pays, else every permutation nearest-first
(Kendall distance to the given order; all permutations for n <= 7, single moves beyond), with the walk's prefix rule
skipping every permutation that shares a failed prefix.
Orders are taken over CLASSES of indistinguishable casts (same definition, kind, cost and cost mode --
`UnlockOrderSameCast`), so two Voyaging Satyrs are one order, not two: an identity fold (the walk cannot tell them
apart; Jumbo digests were byte-identical with and without it over 1000 games) that removes ~10% of the search's cost.

* Apply site: `TurnSolver::UnlockPayableOrder`, called by `PayableCastOrderCore` only when the decider found nothing
  (so every order the decider settles is byte-identical).
* Enumeration site: `SubsetPayableSequentialTail`, after the rank and decider orders failed, so the menu admits the
  subsets the apply then realises.

A realisation of the plan the search chose: its casts and equips are the plan's; only the order moves, and only when
the given order does not pay. The USER's Jumbo cast-order rules (`JumboCactuarProvider::ArrangeCastOrder`) stand
whenever their order pays.

## Family 2: `ef-walk-fail` Surrak (s30119376 T5 x19)

### Mechanism (verified -- NOT an order problem)

`MTG_DBG_SEQ=1 MTG_DBG_MULTI=5` on s30119376: the subset {Tooth and Nail, Surrak, Satyr, Satyr} IS walked and
offered (Surrak first by `JumboSurrakEarlyIfPaid`; both Satyrs enter hasted and re-tap Forest#18). The flagged subset
adds `equip Chariot of Victory` ({1}, onto a creature whose mana it does not unlock), and for it the walk never
starts: `SubsetPayableSequentialImpl` refuses any equip that is neither an unlock nor a free attach as an "unmodelled
kind". Both apply worlds pay such an equip in the trailing equip pass, after the casts, off the mana they left (here
the {1} left after Tooth and Nail).

### Change: `MTG_SEQ_TRAILING_EQUIP` (heurarm `SEQ_TRAILING_EQUIP`, default ON)

`SeqWalkTrailingEquip`: a paid equip (no X) that is not an unlock is skipped by the three lockstep loops
(`SubsetHasUnmodelledSeqKind`, `SubsetPayableSequentialImpl`, `OrderPaysSequentiallyImpl`) and paid at the end of
`SeqWalkFixedOrder` at `EquipActionCostNow`, as the trailing pass pays it. A planned unlock that never fired because
its host no longer needed it (hasted by Surrak, or already tapped) joins the same end payment instead of refusing the
subset; one still locked is refused as before. Moving a haste Equipment off a fresh creature whose {T} it enables
stays refused (the free-attach carve-out). Such subsets are kept out of the cast-keyed memos.

Reach: decks with a PAID equip -- Jumbo (Chariot), Boars (Loxodon Warhammer), Colossus Hammer, Kitty Equipment,
Martyr Lifegain (Jitte). `MTG_PO_UNLOCK_ORDER` reaches decks with a haste Equipment and a mana creature: Jumbo,
Angels (Giada), Boars, Bruna, Five Colour.

## Measurement (2026-11-19, worktree `wip/jumbo-order`, Release)

Unit tests: `mtg-test -tc="*Jumbo*"` 20/20. The s30115214 board: `ApplyPayableCastOrder` moves GSZ ahead of Wild
Growth with `PO_UNLOCK_ORDER` on, not off. The s30119376 board: with both levers off no plan holds
{Tooth and Nail, Surrak, Satyr, Satyr} plus an equip; `SEQ_TRAILING_EQUIP` alone offers Chariot onto Surrak (#30);
`PO_UNLOCK_ORDER` alone offers Chariot onto Satyr #54 (an unlock, in an order that fires it) but not onto Surrak.

### Repros (oracle level 2, one pooled batch, arms off / u = unlock only / on = both)

| seed, turn | off | u | on |
|---|---|---|---|
| 30115140 T3 | APPLY-DROP-ORDER 1 | ORACLE-GAP 1 (*) | ORACLE-GAP 1 (*) |
| 30115214 T3 | APPLY-DROP-ORDER 5 | -- | -- |
| 30115251 T3 | APPLY-DROP-ORDER 10 | -- | -- |
| 30115377 T3 | APPLY-DROP-ORDER 6 | -- | -- |
| 30115733 T3 | APPLY-DROP-ORDER 6 | -- | -- |
| 30116038 T4 | APPLY-DROP-ORDER 5 | -- | -- |
| 30116848 T3 | APPLY-DROP-ORDER 1 | -- | -- |
| 30115578 T4 | APPLY-DROP-ORDER 1 | APPLY-DROP-ORDER 1 | APPLY-DROP-ORDER 1 (residual) |
| 30119376 T5 | ENUM-GAP 19 | -- | -- |

(*) Analyzer side: the engine now realises {Overgrowth, Arbor Elf, equip Chariot -> Elf} with the Elf hasted and
re-tapping the Forest ({G} left floating); the oracle has no outcome with the haste re-tap, so it reports ORACLE-GAP.
Residual s30115578 T4 ({Tooth and Nail x2, Chariot, Satyr, equip Chariot}; the oracle pays it in the order
T&N > Satyr > Chariot > equip): unchanged in every arm, a different mechanism, not chased here.

### Slice (gi 115000-116999, both levers 0 vs 1; flags / decisions / games)

| class | levers off | levers on |
|---|---|---|
| ENUM-GAP | 43 / 42 / 26 | 28 / 27 / 18 |
| APPLY-DROP | 7 / 3 / 3 | 2 / 2 / 2 |
| APPLY-DROP-ORDER | 92 / 32 / 24 | 1 / 1 / 1 |
| ORACLE-GAP | 42 / 15 / 11 | 42 / 15 / 12 |
| PHANTOM | 4681 / 851 / 365 | 3452 / 632 / 291 |

### Held-out A/B (one pooled `mtg --batch`, seed = 30000000+gi, game_index = gi, play settings, gi from 650000)

Arms: off, u = `MTG_PO_UNLOCK_ORDER` only, ut = both. Jumbo 5000 pairs (all three arms); Angels, Bruna, Colossus
Hammer, Kitty Equipment, Five Colour 1000 pairs, Martyr Lifegain and Boars 500 pairs (off vs ut). Slower/faster
count games whose win turn moved (a non-win scored 9).

| deck | comparison | faster | slower | net | digest-only | mean turn d | units | CPU |
|---|---|---|---|---|---|---|---|---|
| Jumbo Cactuar | u vs off (unlock lever) | 2 | 0 | -2 | 104 | -0.0004 | x0.994 | **x1.445** |
| Jumbo Cactuar | ut vs u (trailing lever) | 0 | 0 | 0 | 38 | 0 | x1.002 | x0.989 |
| Angels | ut vs off | 0 | 0 | 0 | 0 | 0 | x1.000 | x0.987 |
| Boars | ut vs off | 0 | 0 | 0 | 1 | 0 | x1.000 | x1.028 |
| Bruna | ut vs off | 0 | 0 | 0 | 13 | 0 | x1.004 | x1.016 |
| Colossus Hammer | ut vs off | 0 | 0 | 0 | 0 | 0 | x1.000 | x0.982 |
| Five Colour | ut vs off | 0 | 0 | 0 | 13 | 0 | x1.000 | x1.023 |
| Kitty Equipment | ut vs off | 0 | 0 | 0 | 0 | 0 | x1.000 | x1.004 |
| Martyr Lifegain | ut vs off | 0 | 0 | 0 | 0 | 0 | x1.000 | x0.994 |

Faster Jumbo games: gi 653146 and 653783 (T5 -> T4). No slower game in any deck, so no d8 b0 recovery run was owed.
Every deck's net is <= 0. The class fold is an identity: all 290 jobs had the same digests as the pre-fold binary.

Lever-off identity: `MTG_PO_UNLOCK_ORDER=0 MTG_SEQ_TRAILING_EQUIP=0 bash test/regression.sh --smoke` ALL PASS
(231 passed, 0 play changes vs committed GT). Smoke/regression GT with the levers ON is not accepted yet (Jumbo rows
move: digest-only and two faster games).

### CPU of the unlock-order search

The search's walks are not charged to the budget (units x0.995), so its cost is pure extra CPU. Counted over 1000
Jumbo games before any of the work below: 517k searches, 5.4M walks, 36k orders found (7%); by order length
n = 2..8: 167k / 176k / 101k / 55k / 16k / 2.1k / 50.

**Shipped (both exact, both default ON; `wip/jumbo-perf` 68a4f528b, d9dfa1b7e):**
* `MTG_UO_DFS` -- the orders are walked as ONE prefix-sharing tree over class sequences
  (`UnlockOrderDfs` / `UnlockOrderDfsRec`): SeqWalkFixedOrder does its setup once, each distinct prefix is paid
  once, and a node with two or more live children snapshots (GameState, `g_line_unpaid_cost`, pending unlocks),
  with the last child taking the snapshot by swap. Exact because a walk is a pure function of the board after the
  prefix, the line's unpaid cost and the pending unlocks, while its set-level setup reads only the cast multiset.
  (A same-line cost reducer's credit is order-sensitive, so those lines keep the old search, as do n > 7.) The
  visit is lexicographic with the old ranking (fewest inversions, ties to the earlier candidate) plus
  inversion-floor pruning, so the apply site returns the same order. The enumeration site stops at the first
  payer.
* `MTG_UO_FOLD` -- in the unlock-order domain the enumeration site's verdict is an OR (the given order, the ETB
  order, or any other class order), so the given order becomes a leaf of the same tree, visited first: one setup,
  its prefixes shared. The ETB order is then walked unless it is the tree's own realisation of its class sequence.
  The OrderPaysSequentially memo route keeps the old sequence.
* Diagnostic: `MTG_UO_DFS_VERIFY=1` runs old and new side by side. Verify set (Jumbo 1000 + Angels / Boars /
  Bruna / Five Colour 200 each): **0 mismatches** over 580k+ searches (tree) and 400k+ sites (fold).

**Pooled digest A/B** (seed = 30000000+gi, game_index = gi, play settings; Jumbo gi 650000-652999, 1000 games each
for the others; new = d9dfa1b7e, old = 2f67da724; CPU = summed per-job cpu):

| deck | differing games (on / off) | CPU new on/off | CPU old on/off | new-on / old-on |
|---|---|---|---|---|
| Jumbo Cactuar (3000) | 0 / 0 | **x1.201** | x1.447 | x0.831 |
| Angels (1000) | 0 / 0 | x1.013 | x1.009 | x0.996 |
| Boars (1000) | 0 / 0 | x1.037 | x1.035 | x0.986 |
| Bruna (1000) | 0 / 0 | x1.020 | x1.016 | x0.991 |
| Five Colour (1000) | 0 / 0 | x1.017 | x1.017 | x0.951 |

`MTG_UO_DFS` alone measured Jumbo x1.208; the fold adds only ~0.7 points. The given walk turned out to be a small
share of a search that fails.

**Where the remaining cost is** (Profile build, Jumbo 256 games, lever on vs off, plus temporary counters over
1000 games). The ENUMERATION site is ~80% of it (UnlockOrderSearchTree 11.5% of all on-arm CPU; the apply site's
UnlockPayableOrder 2.1%). That site ran 411k searches, of which only 3.5% found an order, taking 8.5M steps (60%
of them failing), 4.8M snapshot copies and 2.7M swaps. Average n there is 3.4. Timing over 300 games: successful
steps 55%, snapshots 17%, failing steps 14%, setup and overhead ~14%. (An earlier reading here said failures land
mostly at the last cast; the 2026-11-19 counters below show they do not.) The cost is PROVING INFEASIBILITY, and
it lives in the successful intermediate payments. Top extra symbols: TapForCost* ~14%, memmove 8.7%, GameState
copy/assign/move ~10%, SeqWalkFixedOrder self 5%.

**Tried and not shipped:**
* An order-free necessary bound at the root (SpwfProvablySupplyShort on a board with every line haste
  pre-granted). Sound (0 violations) but caught only 16% of searches (-3% CPU). The tighter form (real haste
  grants only) was UNSOUND (40 violations).
* An exact-id memo of the enumeration verdict in the paymemo scope: 0 hits over 415k searches.
* A canonical (copy-fold) repeat probe. 59% of searches repeat up to renaming hand copies, but the payer reads
  card numbers asymmetrically (ManaPayment.cpp `m_number` range checks and number bitmasks), so the fold is not
  provably exact: 680 disagreements over 80k in a cross-scope cache. Rejected.
* Skipping the restore after an atomic payment failure: it saves mostly the cheap swaps, because failures are at
  the leaves.
* Subset monotonicity ("S unpayable in every order, so S + x is too, for a non-mana x"): NOT exact for this walk.
  The payer is a heuristic tapper whose context is set-level (`g_line_unpaid_cost` = the whole line's cost, the
  line-keep rung, LineReducerCredit), so adding x changes how every prefix is paid. A greedy payer with more
  sources is also not provably monotone.

**Exact candidates measured 2026-11-19 (`wip/jumbo-perf2`; none shipped).** USER, on the three options: *"For 4 I
would like to start with c and then decide if we want to take either of the other options."* Before building
either open candidate, its CEILING was measured with temporary counters (`MTG_UO_STATS`, kept out of the tree as
`logs/jumbo-perf2/uo-stats-tp-bound-measurement.patch` in that worktree). Enumeration site, Jumbo 300 games
(gi 660000-660299, lever on, release build), 180k searches (the fold's given leaf included), 12.6% found:
* **The failure profile, corrected.** 3.34M steps, 2.01M of them failing (60%), but only 256k of those fail at the
  LAST cast. By depth (0 = first cast): 98k / 554k / 736k / 493k / 124k / 9k. So the tree is not walked "nearly in
  full to n-1": most dead branches die one or two casts in, and the expensive part is the 1.33M SUCCESSFUL steps
  (each ~6x a failing one, which mostly stops at the payer's own `UntappedManaUpperBound` gate).
* **(b) Transposition of identical prefix states -- ceiling ~6% of steps.** An approximate state signature
  (battlefield in vector order with number / tapped / haste / sickness / aura host, the three floating pools, the
  line's unpaid cost, the pending unlocks, exile, tokens, life, zone sizes) over the class MULTISET of the prefix:
  62k repeats among 988k interior nodes, whose subtrees hold 197k steps (5.9%). That is an UPPER bound -- an exact
  version needs a complete GameState equality, which does not exist (a defaulted `operator==` would have to reach
  Player, Card, Permanent, Library, StackEntry, PayRollbackLedger ...), and it pays a stored board per interior
  node. Transpositions are rare because AB and BA push their joined permanents in a different order and the payer
  taps differently. Not worth building.
* **(a) A per-node necessary bound -- ceiling ~13% of tree time.** At each node with two or more casts left:
  supply = the three floating pools + `UntappedManaUpperBound` (the payer's own sound over-count) + one best-land
  tap per pending unlock; each remaining cast's gain = its land-Aura bonus x (1 + untappers that could re-tap the
  host) + one best-land tap (Aura bonuses included) for a mana dork or a tutored untapper host; the node is dead
  if some cast still costs more than supply + every other cast's net-positive gain, or if the remaining total
  exceeds supply + every gain. 145k of 1.07M nodes pruned (9.8k of them roots, 5.5% of searches), 671k steps
  skipped (20%), but only 167k of them successful steps (12.6% of those); 0 subtrees that would have paid. Weighted
  by step cost that is ~13% of the tree, ~1.3% of Jumbo's game CPU: x1.20 -> ~x1.185 at best. Shipping it needs
  a gain term that is provably an upper bound for EVERY mechanism the walk models (rituals, ETB untaps, Lens,
  Treasures, land-puts, scaled lands, land-tap bonuses, cost reducers and taxes, the Fireblast/convoke/emerge
  cost paths, the unlocks), with "not provable" for anything outside a whitelist. The 2026-11-18 root-bound
  attempt shows how easily the tight form goes unsound. Too much soundness surface for ~1.3%.
* **(c) Field-subset snapshots.** Not built: snapshots are 17% of the tree (~1.7% of game CPU), and skipping a
  field (the libraries are most of the bytes) needs a proof that no function reachable from the walk's step
  writes it (TapForCostShared and everything under it). No such proof is cheap, and a VERIFY compare costs as much
  as the copy it would save.
* **Where the rest of the lever's x1.20 is.** On the on arm, the enumeration tree is ~9.6% of game CPU and the apply
  site ~2.1%: deleting both outright would leave ~x1.06, the play change itself (more lines found, so other
  searches). So the exact headroom left in the walk is a few points at most. What remains is the USER's call:
  price the walks against the decision budget (below), or accept x1.20.
* USER option (not exact, a play change): price the walks against the decision budget
  (`TurnSolver::DecisionBudget()`, as the equip route does). It is inert at unlimited budget but moves budgeted
  play, so it needs its own A/B.


## Pricing the walks: `MTG_UO_BUDGET_PRICE` (USER option (a)), 2026-11-20

USER, 2026-11-20: *"I'm okay with trying (a), but we will reserve judgement until we see the results. 20% extra is
kind of painful"*. Branch `wip/jumbo-uo-budget` (bfac80e72). **PROPOSED, DEFAULT OFF; adoption is the USER's call.**

### The change

* `MTG_UO_BUDGET_PRICE` (heurarm `UO_BUDGET_PRICE`, default OFF): the unlock-order search's walks charge
  `TurnSolver::DecisionBudget()` one unit per `MTG_UO_BUDGET_PRICE_W` (default 16) cast steps, at a new unit site
  `unlock_order` (census `u_unlock_order`). A step is one cast paid and resolved by the walk (`UnlockOrderDfsRec`'s
  `step()`, or one cast of an order-by-order walk); a search is charged on its steps 0, W, 2W, ... (ceil, no
  residual carried between searches).
* Only the lever's own work is charged: at the ENUMERATION site the given order's path through the tree (inversion
  floor 0 under `MTG_UO_FOLD`) is pre-lever work, never charged and never cut, so its verdict is always reached. The
  other orders, and the APPLY site's given walk and nearest-order search, are charged.
* `MTG_UO_BUDGET_CUT` (heurarm `UO_BUDGET_CUT`, default ON, inert while the price is off): the enumeration site's
  search stops, before a charge, once the budget is `Exhausted() || Overrun()` (the equip route's per-link test). The
  subset then stands or falls on the given order and the ETB order, as with the unlock lever off. The apply site is
  charged and never cut (it realises a plan the search chose; the AA route's rule). The predicates count their binds,
  so a plan list derived across a cut is never stored (cachecharge read guard).
* Inert at an unlimited root by construction: `DecisionBudget()` is nullptr there, so the meter holds no budget and
  nothing is charged, read or cut. (Deliberately no `CacheChargeNoteNull()` at a null budget: the cache's hit test
  reads that note against the greedy budget too, which can be live at an unlimited root.)
* Never a pick: a cut withholds an order the search did not reach; it chooses nothing.
* Unit tests (`*Jumbo*` 22/22, full suite 3776/3776): the s30115214 apply board realises GSZ first with nothing charged
  at an unlimited root, is charged at a finite one, and is still realised with the budget spent; on the s30119376
  board the Satyr-#54 unlock line is cut from the menu only with the price on AND the budget spent.

### The price (W = 16), by measurement

`MTG_UO_PRICE_STATS=1`, Jumbo 400 games (gi 719000-719399, seed 30000000+gi, play settings), lever on, 2 threads:

| site | searches | chargeable steps | exempt (given-path) steps | wall in the searches |
|---|---|---|---|---|
| enumeration | 147,719 | 2,535,367 | 342,881 | 4.48 s |
| apply | 58,046 | 234,221 | 0 | 0.71 s |

1,668 ns a step, setup included; ~4.62 s of it is lever work (the exempt share removed). The lever-off arm of the
same batch ran 15,663 ns of game CPU per budget unit (5,689 units, 89.1 ms a game; lever on 100.1 ms), so the walks
are worth ~295k units. Ceil-per-search pricing charges 456k at W=8, 350k at W=12, **304k at W=16**, 259k at W=24
(beyond that the one-unit-a-search floor dominates). W = 16 bills the walks at their measured cost; the whole arm's
CPU delta (11.0 ms a game, ~281k units) agrees. That is ~760 units a game on a ~5,750-unit game, +13%.

### Inertness at `--depth 8 --budget-ms 0`

Jumbo gi 719500-719599 (100 games), arms on / price+cut / price only: **100/100 byte-identical** (win turn, digest,
units).

### Held-out A/B (ran 2026-11-20, finished 16:23 UTC, rc=0, heartbeat 32/32)

One pooled `mtg --batch` (`build/scratch/claude_jumbo_uo_budget/ab.json`, binary `mtg.w16` = bfac80e72, `MTG_PGO=0`,
`MTG_RSS_CAP_GB=40`, 32 threads), seed = 30000000+gi, game_index = gi, play settings, **gi from 720000** (no earlier
Jumbo A/B used 719000+; the earlier ones used 640000-660299 and 115000-116999). Arms: off = `MTG_PO_UNLOCK_ORDER=0`;
on = unlock lever, price off (the shipped play); pc = price + cut; p = price only. Jumbo 10,000 games a arm; Angels,
Boars, Bruna, Five Colour, Kitty Equipment 2,000 each (80,000 games).

Per deck, each arm against `on` (the shipped play). faster / slower = games whose win turn moved (a non-win scored 9);
net = summed turn delta (`+` = worse); CPU = summed per-job CPU, ratio to `on` and to `off`; ms/g = CPU per game.

| deck | games | arm | faster | slower | net | digest-only | units vs on | CPU vs on | CPU vs off | ms/g |
|---|---|---|---|---|---|---|---|---|---|---|
| Jumbo | 10,000 | off | 1 | 4 | **+3** | 180 | x0.995 | x0.839 | 1 | 179 |
| | | on | - | - | 0 | - | 1 | 1 | x1.192 | 213 |
| | | pc | 0 | 2 | **+2** | 29 | x1.080 | x0.924 | x1.101 | 197 |
| | | p | 0 | 2 | **+2** | 12 | x1.103 | x0.946 | x1.128 | 201 |
| Bruna | 2,000 | off | 0 | 1 | +1 | 31 | x1.000 | x0.992 | 1 | 705 |
| | | pc | 0 | 1 | **+1** | 3 | x1.025 | x1.004 | x1.012 | 713 |
| | | p | 0 | 1 | **+1** | 3 | x1.026 | x1.003 | x1.011 | 713 |
| Angels | 2,000 | pc / p | 0 | 0 | 0 | 0 / 0 | x1.013 | x1.002 | x1.001 | 46 |
| Boars | 2,000 | pc / p | 0 | 0 | 0 | 0 / 0 | x1.010 / x1.002 | x0.969 / x0.953 | x0.999 / x0.984 | 264 / 260 |
| Five Colour | 2,000 | pc / p | 0 | 0 | 0 | 0 / 0 | x1.025 | x1.008 / x1.004 | x1.014 / x1.010 | 1754 / 1746 |
| Kitty Equipment | 2,000 | pc / p | 0 | 0 | 0 | 0 / 0 | x1.000 | x0.995 / x0.996 | x0.996 / x0.997 | 678 |

(Angels / Boars / Five Colour / Kitty: `off` vs `on` is also 0/0/0; Boars' `off` CPU x0.969 with 8 digest-only moves
shows the CPU noise floor between arms of a deck the lever barely touches is about +-3%.)

The slower games are the SAME games on both price arms: Jumbo gi 722372 (8 -> 9, a lost win) and gi 727984 (4 -> 5),
Bruna gi 720338 (6 -> 7). No price arm made any game faster. Against `off`, both price arms keep all four of the
unlock lever's Jumbo wins (gi 724404, 725337, 727698, 729291) and its one Bruna win (720747), and add the three
slower games above (the lever alone loses one, gi 727106, 4 -> 5): Jumbo net vs off on -3 / pc -1 / p -1, Bruna
on -1 / pc 0 / p 0.

**CPU.** Jumbo at play settings: off 179 ms a game, on 213 (+19.2%), pc 197 (+10.1% vs off, -7.6% vs on), p 201
(+12.8% vs off, -5.4% vs on). The price therefore recovers ~60% (pc) / ~33% (p) of the lever's cost; the cut is
worth a further ~2 points. Every other deck is within the +-3% noise floor. Against Hinata: `test/suite_cost.json`
has Jumbo at 3,391.5 ms a game (the d5 b20 regression shape) and Hinata at 6,434.4, 0.527x, measured before the
unlock lever. Scaling by the play-settings ratios above (a projection, not a suite-shape measurement): on ~0.63x,
pc ~0.58x, p ~0.59x Hinata.

**d8 b0 recovery** of every slower game (`rec.json`, arms on / pc / p; recovered = d8 b0 turn <= `on`'s budgeted
turn), on the 4-core lane, binary `mtg.w16`:

| deck | gi | on (budgeted) | pc / p (budgeted) | d8 b0 on / pc / p | recovered |
|---|---|---|---|---|---|
| Bruna | 720338 | 6 | 7 / 7 | 6 / 6 / 6 | yes |
| Jumbo | 722372 | 8 | 9 / 9 | 8 / 8 / 8 | yes |
| Jumbo | 727984 | 4 | 5 / 5 | 4 / 4 / 4 | yes |

All three recover, and the three arms are byte-identical at d8 b0 (digest and units). This confirms again that the
price is inert at an unlimited root. Each loss is the finite budget being re-spent: the price bills the walks, so the
rest of that decision's search gets less.

**Verdict against the bar.** Every slower game recovers at d8 b0, but the per-deck net against the committed tree is
**Jumbo +2 and Bruna +1 on BOTH price arms** ("+1 is not neutral. 0 is neutral."). As measured, **neither arm meets
the adoption bar**. The lever stays default OFF; the USER decides.

**On the open question (cut on `Exhausted() || Overrun()` vs `Overrun()` only).** On this sample the two arms
have the same turn outcomes: the same three slower games, no faster games, and net turns identical in every deck.
The cut arm is cheaper (Jumbo CPU x0.924 vs x0.946 of `on`; units x1.080 vs x1.103) but moves more digests without
changing a turn (29 vs 12 Jumbo digest-only moves; both are small). On the evidence, pc matches p on quality and beats it on CPU.

