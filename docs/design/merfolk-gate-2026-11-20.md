# Merfolk Turns: the first complete mana-clean gate (2026-11-20)

Gate `claude-queue-20261119i` (5,000 games, gi 0-4999, seed 30000000 + gi, src `8deb2c9336d1`, PGO,
`MTG_MANA_ORACLE=2`; report `logs/mana_clean/claude-queue-20261119i/report.md` in the primary checkout). Merfolk Turns
counts: APPLY-DROP 6,952, ORACLE-GAP 7,903, ENUM-GAP 937, APPLY-DROP-ORDER 161; oracle-off play byte-identical.
Branch `wip/merfolk-gate`. Repros were traced with the gate's own binary on the gate commit's deck files (the keep
table landed after the gate's commit, so the current tree plays different hands on the same seeds).

## 1. APPLY-DROP: Three Tree City's {C} spent on a generic pip (PLAY defect)

Mechanism (s30002188 T5, traced; s30000073 T7 has the same shape). Lands Island, Island, Three Tree City, six Merfolk;
plan {Sol Ring, Stonybrook Banneret, Wanderwine Prophets, Greaves equip}. The engine paid Sol Ring's {1} with City's
plain {C} (engine-ended board: City tapped, Island #11 untapped, `{C}` floating, Prophets DROPPED). Prophets then met
Island + Sol Ring's {C}{C}. The line pays as Island for the Ring, then the Ring's {C}{C} into City for six {U}. No
line-aware rung reached it: the ramp route is for ramp filters, the any-colour route for any-colour filters, the
line-keep rung only for reducer lines (and it did not credit the rock being cast, which is City's feed).

Fix: `MTG_SCALED_LAND_ROUTE` (DEFAULT ON), `ScaledLandRouteHold` in `src/ai/ManaPayment.cpp`, the any-colour route's
hold-out shape. While a scaled land of ours is untapped and the line still owes mana, the payment is tried on a
scratch board; only when the trial strands the rest of the line (hold net of this payment and of the line's reducer
credit, judged by the complete backtracker with an untapped rock being cast credited) while the board pays this bill
and the rest together, is one of the trial's sources held out, the first that keeps the rest payable. Shared payer,
so executor, rollout and payability walks move together. Unit: `Sol Ring's {1} keeps Three Tree City for Wanderwine
Prophets (MTG_SCALED_LAND_ROUTE)`.

## 2. ORACLE-GAP: "1/1 Merfolk Token only in oracle" (PLAY defect, same root)

The oracle is right. Emperor Mihail II (cards.json: "Whenever you cast a Merfolk spell, you may pay {1}. If you do,
create a 1/1 blue Merfolk creature token.") is a plan axis (`MihailPayChoices`); on the pay variant the engine's
Prophets payment had already tapped every source, so `TapForCostDirect` for the {1} failed and no token was made.
s30001289 T5 (traced): Prophets' {3}{U}{U} (Banneret's discount) off four Islands + City's {C}, exactly, nothing
floating. City fed by two Islands makes {U}{U}{U}{U} (four Merfolk); with one more Island that pays the bill and gives
an Island back for Mihail's {1}. The backtracker tries City's plain {C} first and keeps the first covering set, and
`MTG_PAY_REFINE` -- whose dominance test is exactly "a subset of these taps, at least the engine's float" -- never
looked, because the payment neither cost life/energy nor floated a surplus.

Fix: `MTG_PAY_REFINE_SCALED` (DEFAULT ON): the refine also runs after an exact payment of four or more taps that tapped
a scaled land whose fed mode makes at least {feeder} + 2 (`PayRefineAfter`). Sound dominance (the refine adopts only a
strict subset of the engine's own taps), no new decision. Unit: `Three Tree City's fed mode refines an exact payment
that tapped its plain {C}`.

## 3. ENUM-GAP: {Seahunter + Rishadan Dockhand} kept off the menu

s30000762 T4: Islands x2, City, three Merfolk; hand Seahunter, Dockhand, Island x2. Dockhand first makes four Merfolk
and two Islands into City pay Seahunter. FIXED by option (a), section 5.

Mechanism (traced with `MTG_DBG_MULTI=4` on a unit-test board, the Island already played). The plan never reaches a
per-subset gate: EnumeratePlans' odometer prunes on a scalar mana bound, `[dbgmulti-bound] t4 bound=4 pool=4
cands=[Seahunter(4), Rishadan Dockhand(1), Tidal Warrior(0)]`, and {Seahunter, Dockhand} = 5 > 4. The bound already
carries a same-turn GROWTH addend for scaled DORKS (`growth_bound`, Priest of Titania) but none for a scaled LAND, so
City's yield is priced at its current three Merfolk. The per-subset rescue (`SubsetPayableWithFilters`) would admit it:
its `scaled_land_growth` order puts Dockhand first, and the same two payments made by hand on that board both pay.

Why it is not fixed here: admitting the subset (a land-growth addend on the bound -- an upper bound, so it can only
prune less) moves the hole from the menu to the APPLY. Both apply worlds order the casts by `CastOrderLess`: Seahunter
and Dockhand tie (both creature rank 10, no cheapest-first tie-break outside rituals), so plan order stands, Seahunter
is paid first (two Islands into City for three {U} plus the third Island) and Dockhand finds nothing: an APPLY-DROP.
The payable-order decider (`PayableCastOrderCoreImpl`) re-orders only producers, reveal casts and domain wideners. The
complete fix is the bound addend plus EITHER a decider domain for "a creature that grows an untapped scaled land"
(walk the growers first when the given order does not pay) OR a Merfolk Turns cast-order rule (cheaper City-growing
Merfolk first). The second is a deck cast-order policy, which is the USER's call; the first is the established
"only when the given order does not pay" shape but changes the realised order of real games.

## 4. Measurements

### 4.1 The gate's repro games (oracle level 2, gate deck files, new binary, both levers on vs both off)

| seed | turn | class (off -> on) |
|---|---|---|
| 30002188 | T5 | APPLY-DROP 1,206 -> 0 |
| 30000073 | T7 | APPLY-DROP 3,028 -> 0 (DOMINATED-LAND 758 -> 578) |
| 30001289 | T5 | ORACLE-GAP 60 -> 4 |
| 30002784 | T6 | ORACLE-GAP 452 -> 0 |
| 30000557 | T6 | ORACLE-GAP 564 -> 452 |
| 30000762 | T4 | ENUM-GAP 4 -> 4 (not addressed by these levers) |

(30000557 and 30001289 read 290 / 0 after the prepay half of `MTG_PAY_REFINE_SCALED`, below; 452 / 4 before it.)

### 4.2 Held-out A/B (both levers on vs both off, one pooled batch, small-job lane, 3 threads)

Seeds 33000000 + gi, gi 0-1999 Merfolk Turns and Goblins, 0-399 Maze's End, play settings from each profile (d5
b20; the current tree, keep tables live), loss = 9. `build/scratch/claude_merfolk_gate/ab/` (manifest, ab.out, logs).

| deck | pairs | faster | slower | net turns | digest-only | units on/off | cpu on/off (order-balanced) |
|---|---|---|---|---|---|---|---|
| Merfolk Turns | 2,000 | 0 | 0 | 0 | 8 | 0.9999 | 1.018 |
| Goblins | 2,000 | 0 | 0 | 0 | 0 | 1.0000 | 0.984 (byte-identical: noise floor) |
| Maze's End | 400 | 1 | 1 | 0 | 13 | 1.0010 | 1.074 |

Whole batch (`scripts/ab_wall.py`, BALANCED): ms off/on 0.976, cpu 0.970, units 1.000. The one slower game is Maze's
End gi126 (T5 -> T6). **At `--depth 8 --budget-ms 0` both arms win T5** (`build/scratch/claude_merfolk_gate/d8b0/`):
the slower game recovers, so the bar holds.

### 4.3 The oracle-on gate slice (Merfolk Turns, gate deck files, both arms; `slice_s1/`, `gap_shapes.py`)

| class | off | on |
|---|---|---|
| APPLY-DROP | 3,093 | 4 |
| ORACLE-GAP | 87 | 44 |
| ENUM-GAP | 120 | 118 |
| DOMINATED-LAND | 7,239 | 5,470 |
| APPLY-DROP-ORDER | 4 | 4 |
| DROP-COVERED / CONTINUATION-DIFFERS | 6,827 / 4,482 | 6,828 / 4,487 |

APPLY-DROP off is 2,692 Wanderwine Prophets + 357 Deeproot Pilgrimage + 44 others; on it is 2 Deeproot Pilgrimage + 2
Seahunter. The residual ORACLE-GAP is the "1/1 Merfolk Token only in oracle" shape (24 + 6 + 6 of 44).

### 4.4 Smoke + regression (`THREADS=4 MTG_PGO=0 regression.sh --both`, pre-rebase tree a121d871f)

Only Maze's End, Maze's End 2HG and Merfolk Turns rows moved. Searched: smoke slower 0 / faster 3 / play-changed 6;
regression slower 0 / faster 1 / play-changed 7. d0: smoke 1 slower (mazesend d0 gi830 7 -> 8, beside gi772 7 -> 6,
net 0), 1 faster; regression 0 slower, 2 faster. Scenarios 594/594, units 3,780/3,780.

Reading: the levers realise plans the menu already offered (the oracle's APPLY-DROP / ORACLE-GAP counts fall sharply on
the repro games) but the search rarely CHOSE those plans, so win turns do not move on Merfolk Turns. Maze's End pays
for Baldur's Gate's trials: ~+7% CPU for no measured gain (USER rule: a fix's CPU cost is to be cut, not justified).

### 4.5 The Maze's End CPU cut (2026-11-21, rebased onto 69af1be5f)

Instructions (`perf stat -e instructions:u`, Maze's End held-out gi 0-59, 1 thread each; `build/scratch/claude_merfolk_gate/cpu/`).
Pre-rebase split: both on +5.76% vs off, route only +0.62%, `MTG_PAY_REFINE_SCALED` only +5.16% -- the refine trigger
was ~90% of the cost. Counters (rebased, uncut): the scaled trigger ran 66,456 refines; payments with Baldur's Gate's
PLAIN {C} (48,657) found a cheaper payment 48,650 times, payments that had already taken the fed mode (17,799) found
one 57 times.

| arm (rebased tree) | instructions vs levers off |
|---|---|
| levers on, uncut | +8.15% |
| levers on, cut | +3.94% |

Two cuts: (A) the route solves the rest of the line ON its trial board instead of a second copy (exact); (B) the scaled
trigger requires the plain tap, its own premise (not a dominance proof: the 57 fed-mode finds are dropped; the trigger
never existed before 2026-11-20, so nothing the committed tree did is lost). `.wins` and digests are byte-identical cut vs
uncut on Maze's End (60), Merfolk Turns (150) and Goblins (150). What is left is the plain-tap refine, which finds a
better payment 99.99% of the time (its work is the play change), plus the search's own +0.8% units on the changed play.


**Cut (B) is a NARROWING of the new lever, not an exact identity.** The refine trigger now fires only when Baldur's Gate
/ Three Tree City was tapped for its plain {C}; the 57 cheaper payments (per 60 games) that the uncut trigger found after
a FED-mode tap are no longer found. Nothing the committed tree did is lost (the trigger is new), but the cut lever is a
strictly smaller version of what was A/B'd uncut; the tier run below measures the cut form.

### 4.6 Smoke + regression on the rebased, cut tree (2026-11-21, accepted ddc28771c)

`MTG_BIN=t2/mtg.final` (built from b8f076da0, clean src), `MTG_PGO=0 regression.sh --both`, tier wall 1,984 s. Only
`mazesend*` and `merfolkturns*` rows moved (no 2HG row, no Goblins row). Searched: 0 slower in both tiers (smoke 1
faster + 3 play-changed at the same score, regression 1 faster + 7). d0: smoke `mazesend_smoke_d0_s1001` gi592 T5 -> T7 and
gi781 loss -> T6; regression d0 4 faster, 0 slower. gi592 (seed 1593) reproduces at d0 (off T5, on T7) and recovers at
`--depth 8 --budget-ms 0` (T4 on both arms). Every deck is net <= 0. Units 3,805/3,805; `check_gt_logs.py` 1,358
consistent.

## 5. ENUM-GAP: resolved by option (a), the generic decider (shipped default ON, 2026-11-22)

**USER ruling 2026-11-21 (verbatim), on the section 3 ENUM-GAP cast order:** *"I'm fine with trying either option as
needed. Whatever does better can win out. We are looking for the cheapest that corrects the issue."* Both options were
built behind a temporary selector and measured; option (a) won and ships, option (b) is deleted. No USER cast-order
decision is owed (option (b), the deck cast-order rule, lost).

**What ships (both default ON, `=0` restores the old behaviour):**
- `MTG_SCALED_GROWTH_BOUND`: a scaled LAND's same-turn growth is credited where the mana is priced. EnumeratePlans'
  scalar bound adds +1 per City-matching creature cand per untapped scaled land with a creature subtype (an upper bound,
  so it can only prune less), and so does `SpwfProvablySupplyShort` (the filter rescue's supply pre-gate). Under the same
  flag the scaled-land route (`MTG_SCALED_LAND_ROUTE`) credits a cast trigger's entrants (Emperor Mihail II's "may pay
  {1}" is paid in FireOnCastTriggers while the cast Merfolk is on the stack and before its token exists; the route
  priced City at the board's count, saw no hold-out, and the greedy paid {1} with City's plain {C}) and a creature
  spell's own body to its rest-of-line solves only (`PendingScaledEntrants`, `ScaledEntrantCreditArmed`); the payment
  itself is made at the real count. Fixes option (a)'s two new APPLY-DROPs (s30000034 T5 Wanderwine Prophets, s30000075
  T5 Seahunter). Each emitted selection is priced with its own growers (3218cc593; exact, measured to save nothing).
- `MTG_SCALED_GROWER_DECIDER`: the apply's payable-order decider, when the given order pays in neither the whole-turn
  prepay nor the sequential walk (after the unlock and damage-order domains), tries the order with City-growing
  creature casts cheapest-first and takes it only when the sequential walk pays it. Generic: not Merfolk-specific.
- Deleted: option (b) (`MerfolkTurnsProvider::ArrangeCastOrder` override, `MTG_MERFOLK_GROWER_ORDER` and its heurarm
  slot).

**Gate slice** (Merfolk Turns gi 0-399, gate deck files, `MTG_MANA_ORACLE=2`; distinct game-turns):

| class | off | (a) first cut | (b) first cut | (a) shipped | (b) + entrant credit |
|---|---|---|---|---|---|
| ENUM-GAP | 27 | 17 | 17 | **15** | 16 |
| APPLY-DROP | 1 | 3 | 6 | **1** (s30000006 T4, same as off) | 4 |
| ORACLE-GAP | 9 | 8 | 20 | 8 | 20 |

**Held-out A/B** (fresh seeds 35000000+, 2,000 games per deck, off vs (a); net = loss-penalized turns, negative = better):

| deck | searched net | searched slower | d0 net | d0 slower | units (a)/off |
|---|---|---|---|---|---|
| Merfolk Turns | 0 | 0 | -5 (5 faster) | 0 | 1.0071 |
| Goblins | 0 | 0 | 0 | 0 | 1.0089 |

No slower game anywhere, so no d8 b0 recovery was owed. A bound-only arm measured units 1.0072 / 1.0097, so the decider
costs ~0 and the whole +0.7-0.9% units is the bound: the selections it admits and the supply-credit rescue, i.e. the
cost of the correction itself. The per-selection pricing cut (3218cc593) was byte-identical over all 8,000 games and
removed nothing measurable.

**Smoke + regression on the rebased ship tree** (2026-11-22; d8e910d97 on 4a7f65983, Release, `MTG_PGO=0`, box lock;
605 scenarios pass, units 3890/3890). Only Three Tree City decks move, and nothing between the prior GT's base and
4a7f65983 touches src or decks, so every move is this change:
- Merfolk Turns: searched 3 faster (smoke d3/d5 gi6 6->5, 2HG d3 gi6 6->5), d0 5 faster (smoke gi353 7->6, gi626 / gi771
  / gi859 6->5; regression gi609 6->5), **0 slower**. Same-score line changes are the admitted growth casts (an extra
  Rishadan Dockhand or Silvergill Douser in the turn City pays for): smoke d3/d5 gi12 gi32, regression d5 gi80 gi114,
  d0 smoke 11 and regression 7.
- Goblins (plays Three Tree City): digest-only at the same score (regression d5 s2002 gi174 gi184, s3003 gi151 gi220,
  d0 gi371).
- Net: Merfolk Turns searched -3, d0 -5; Goblins 0 / 0; every other deck byte-identical. No slower game, so no d8 b0
  recovery owed. Accepted (GT commit below); overnight GT left for the periodic overnight run.
