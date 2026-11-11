# Extra Turns mana oracle sizing tail

The 32-game `lastweek-20261110-extra` oracle-on sizing probe did not give a
useful per-game cost estimate: job `extra_turns__p3` (seed 30000003, game index
3) took 628,597 ms, while other completed jobs varied from 125 ms to 350,687
ms. Its ordinary search work was only 70 units. A direct single-game replay on
the same PGO binary took 0.13 s with `MTG_MANA_ORACLE=0`, showing that the tail
is in the oracle audit rather than the game search.

I bounded the exact p3 replay with `MTG_MANA_ORACLE_DECISION_S=1` and enabled
`MTG_MANA_ORACLE_HEAVY=1`. Oracle-on finished in 11.69 s, used 3.2 GiB peak
RSS, and made 198,971 oracle `ApplyPlan` calls. Nine turn-four main-phase
decisions spent even this one-priced-second budget; their menus reached 980
plans (another reached 932), with thousands of mana-gate-rejected variants.
The output recorded 7,404 `CAPPED-DECISION` flags. An unmodified oracle-on
replay was still active after 12 s and was stopped by the diagnostic timeout.

The default per-decision budget is 300 priced seconds
(`ManaOracle.cpp::DecisionCapNs`). Lowering it would make the run shorter by
leaving plans unaudited. `CAPPED-DECISION` fails the mana-clean gate by design,
so that is not a valid clean-gate fix. The existing gate-rejection loop already
deduplicates by `SiblingKey`, while each menu plan is audited in full because
cast order and land position may change payment and outcome.

The concrete reuse point is `AuditDecision`'s per-plan `Realize` call: an exact
ordered plan on the same complete pre-decision state has the same payment
frontier. A cache key would need the full root state (including hidden library
order, resources and phase), the ordered actions with all choices and source
IDs, land placement, and whether the audit is pre-combat. The existing
`SiblingKey` deliberately sorts actions and cannot key this cache; it groups
plans whose different cast orders the oracle must compare. A smaller candidate
is reusing `AuditPlan`'s completed `Realization` for `RollbackCore`'s `k=0`
retry on an APPLY-DROP, but p3's many full-plan audits mean that alone is
unlikely to remove this sizing tail. Neither candidate has been implemented
or measured yet.

## Targeted profile and implementation boundary

An eight-second `perf record` of the unmodified PGO p3 replay sampled the
turn-four tail. The largest exclusive symbols were `BuildSimKeyBase` (14.08%),
`GameState` copy construction (7.79%) and assignment (3.46%); the remaining
samples spread across key construction, `ApplyPlanDirect`, payment and frontier
pruning. This is repeated full-state application and identity work, not one
isolated slow payment routine. The one-second-cap replay gives a small,
repeatable workload for evaluating a future cache: 198,971 applies, an exact
class-count target under the same cap, and an oracle-on/off win turn of four.

No reuse has been landed. `SiblingKey` is intentionally order-insensitive,
while an exact `Realize` cache must preserve every choice that can change an
apply, hidden library order, and payment-frontier order. `ActionKey` names only
a subset of `Action` fields, so treating it as that exact key would risk
silently reusing a result for a different plan. An exact key or equivalence
proof is needed before the oracle can skip any of these applies while still
claiming full audit coverage.

## Diagnosis and two lossless collapses (2026-11-11)

**Where p3's cost goes.** Uncapped, single-threaded, on the current tree (`MTG_MANA_ORACLE=2`), seed 30000003
gi3 takes 318 s and 9.62M applies (the 628 s of the sizing probe was measured under 32-way contention). It
finishes with **no CAPPED-DECISION**: the two costly decisions spend 229 and 254 priced seconds of the 300.
Of 33 decisions, two T4 main-1 decisions (menus of 980 and 932 plans) make 8.0M of the applies.
`perf` on a Profile build (1-s cap) put ~half of `Realize` in the apply's breakpoint continuation
(`BpCanonFront` -> `BpDeriveContinuationList` -> `EnumeratePlansWithLand`: Horn of Greed draws on every land
drop). The bp-enum cache serves 89% of those lookups (6.64M hits / 0.80M misses). Each miss is a different
board after a different scripted payment, so the cache is not missing anything.

New diagnostics (all default off; play is unaffected):
* `DECISION-COST ... applies=N (menu M, gate G)`: the split between the menu audits and the ENUM-GAP pass.
* `MTG_MANA_ORACLE_COST_TRACE=1`: `PLAN-COST` / `GATE-COST` lines per plan, plus an exit line for the audit's
  applies by stage (own realisation / siblings' payments / other orders).
* `MTG_MANA_ORACLE_ONLY_MENU=<n>`: audits only decisions whose menu has exactly n plans. The game plays the
  same either way, so this is a cheap repro of one heavy decision; for p3, `=980`, `=932` and `=534`.

**1. The ENUM-GAP pass proved unpayable plans the long way (FIXED, lossless).** In the 534-plan decision,
96% of the applies (737k of 768k) went to 400 gate-rejected plans such as Ulamog + Emrakul + three Walk the
Aeons + Temporal Mastery. These are rejected because they cannot be paid. Proving that takes every order of
the plan, and each order searched every payment of every cast AFTER the first one that could not be paid.
A hit needs `Outcome::Clean`, and Clean refuses any fail option, so none of that work could answer the
question. `Realize(..., hit_only)` no longer builds a fail-option script when the caller reads only the hit.
The ENUM-GAP check (`EnumGapPlan`) and the sibling-payment check (`sib_pay_covers`) are the only such callers.
Frontiers with a failure were already keyed apart in the Pareto prune, so no kept frontier changes. The
534 decision went from 768k to 225k applies (18.3 s -> 6.8 s). The ORACLE's flags were identical.

**2. Sibling payments were searched again for every question (FIXED, lossless).** Menus here come in groups
of 16-20 plans with the same actions: land-search pins, dig choices and orders (980 plans, 114 sibling keys).
`sib_pay_covers` ran a fresh hit-only search of each sibling for each uncovered board of each plan. A
`SibPayMemo` per decision now records a sibling's search when it ends without a hit. It keeps each clean,
fully scripted outcome's hand, CoreKey and Resources, read where `acc` reads them. Later questions are
answered from that record. The search's script order is fixed and it stops only at a hit, so an exhausted
search has seen every outcome any later search could see. A search that stopped at a hit records nothing, so
no search costs more than before. A record is not kept when the decision budget cut it. On 11 heavy seeds,
sibling-payment applies fell from 4.21M to 0.11M.

**Verdict identity.** Seeds 30000002/3/4/7/10/12/14/16/18/29/31 were run with the old binary (HEAD PGO) and
the new one. All 8,938 flag lines are identical once CAPPED-DECISION is set aside. Applies fell from 34.6M
to 25.7M. Because less work is wasted, budgeted decisions audit more plans before their budget runs out:
CAPPED-DECISION went from 10,686 to 10,265 plans and from 8 to 7 decisions. The 980/932/534 single-decision
repros also give identical flags.

**3. The order-proof bound (cbe0633fe, 180 -> 5,040) does not affect this deck.** Every PHANTOM in these
seeds is a plan with at most three movable actions (at most 6 orders). Every other-order search is far below
180. The "other orders" stage (3.6M applies on the heavy seeds) comes from drop plans with no full sibling.
They are the same 16-20 variants each searching their own few orders. They cannot use hit-only: that loop
reads `unscripted` from every outcome, failed ones included, to decide whether a PHANTOM claim is safe.

**What remains, and why Extra Turns is still not gate-tractable.** After both collapses, the heavy seeds'
audit is mostly each plan's own full payment search (11.1M applies). 1,428 plans pass the 2,000-apply
ceiling and finish under the 20,000 retry. Seven decisions with 412-4,738 plans still spend the 300
priced-s budget (p10 T5, p14 T5 x3, p16 T6, p29 T4, p31 T4). CAPPED-DECISION fails the gate, so a
5,000-game gate on this tree would fail on cost alone. The menus are wide because the engine offers
genuinely distinct plans: the pins choose which Islands Myriad Landscape fetches, which changes the library
and so what Horn of Greed and Howling Mine draw next. Folding them would need a proof that the pin cannot
change any payment or continuation, and that proof does not hold here. No further sound collapse was found.
Candidates that were examined and rejected:
* An upper bound on available mana to reject unpayable plans early. Extraplanar Lens, Mind Stone and draw
  continuations make it unsound without per-card modelling.
* Sharing payment prefixes across orders. `ApplyPlan` reads the whole line (reservations, the unpaid line
  cost), so equal prefixes are not proven to be equal applies.
* Treating "untapped source" as dominating "its mana floating". This is not proven. Cards that care
  whether a permanent is tapped (Lens's tap-for-mana trigger, the Islands Walk the Aeons sacrifices) would
  each need a proof.

**Gate estimate: PARKED (2026-11-11).** A sample was run with both collapses in: games 90000-90063
(seed 30090000+i, a block no gate uses), Release, 16 threads, `MTG_MANA_ORACLE=2`. It was stopped at 24 min.
By then 35 games had finished, with a mean of 365 s per game, a median of 232 s and a maximum of 1,399 s.
Those finished games are biased toward the fast ones: game 90000 alone was still running after 20 min.
Already 56 decisions in 29 games had spent their 300-priced-s budget. At 32 threads, 5,000 games come to at
least ~16 h (Release; PGO would make it ~12 h). Around half of the games would carry a CAPPED-DECISION, and
each of those fails the gate. The gate was not launched. The per-decision BUDGET-SPENT lines are in
`logs/mana-clean-chain/extra-turns-sample64-budget-spent.txt`.

Making the deck gate-able needs an audit of fewer plans, not cheaper applies. Two possible routes, neither
of them a lossless fold available to the oracle alone:
* The engine stops offering land-search pins and dig variants as separate plans when they cannot differ
  without hidden information. This is a search-side change that moves play.
* The oracle audits a pin/dig group once, with a proof that the variants share every payment. That proof
  depends on the pins never changing what a continuation draws, and in this deck they do.

Issues seen on the heavy seeds (kept for triage when the deck is next picked up). Every one involves
Extraplanar Lens:
* APPLY-DROP 45 (rollback depth 1-3+). Example: s30000012 T7 {Ulamog + Extraplanar Lens(x28) + eternalize
  Myriad Landscape}. The engine drops Ulamog, while the oracle pays the Lens with Islands #28 and #41 plus
  Landscape #18, imprints #28, and pays Ulamog with six Islands doubled by the Lens.
* ORACLE-GAP 110. Example: s30000010 T5 {Island + Ulamog + Time Warp}, where the diff names Horn of Greed
  #8 "only in engine". The oracle seems to mis-model either the Lens bonus or the board the imprint leaves.

## USER ruling 2026-11-11: fold identical-basic fetch pins (verbatim)

> Yes, I agree with your fold for extra turns. We should not be treating the islands as different for clairvoyance
> reasons.

The deck's only basic is Snow-Covered Island. Myriad Landscape's pins choose among identical Islands, and the fetch
shuffles; the only thing separating the plans is the seeded post-shuffle library (clairvoyance). Fold identical
same-name basic fetch choices into one plan, engine-wide (any deck fetching identical basics), validated as a play
change; then identify the "dig variants" and fold them only if they too differ only through hidden information;
then re-measure the oracle cost and gate if tractable. Queued as mana-clean chain item 5l.

## Item 5l result (2026-11-11): the premise was wrong; nothing to fold; PARKED

**The identical-basic fold the USER approved already exists, engine-wide, and has for a long time.**
`LandSearchClasses` (`src/core/LandSearch.h`) folds every copy and behavioural twin of a land into one class
before any pick is built. "Which Snow-Covered Island" has never been a branch. Myriad Landscape's pick list on
the 980-plan p3 decision (seed 30000003, T4 m1) is exactly three entries:
`{Island, Island}`, `{Island}`, `{}`.

**What the pins actually choose is HOW MANY Islands to fetch (2, 1 or 0), not which ones.** The section above
said otherwise, and that wording is what the 2026-11-11 ruling was given on. It was a misdiagnosis.
Myriad carries `land_search_allow_partial: true`. The deck analysis set this deliberately
(`docs/design/analysis-Extra Turns.md`, "Myriad Landscape's maximal-pick dominance is unsound in this deck:
removing both basics from a library can make an extra-turn draw engine deck us sooner"). Library size is real
information, so fetching one Island instead of two is a real decision and not a clairvoyance twin.

**The 980-plan menu, broken down by a temporary sibling-diff probe (114 sibling keys):**
* 528 plans are the Mind Stone **dig twin**: 264 base plans (`dig_choice 2`) and 264 no-dig twins (`0`).
  The dig source in this deck is Mind Stone's `{1}, {T}, Sacrifice: draw a card`. Sacrificing a rock to draw
  or keeping it is a real-information decision, not a clairvoyance twin. The search side already folds a
  twin whose base did not dig (`DigFoldMemo`). The oracle cannot reuse the base's verdict, because its own
  alternative payments can leave the `{1}` that makes the base dig.
* 452 plans are **count pins**: 244 with one Myriad activation pinned to 1 or 0 Islands, and 208 with two
  activations in a 3x3 product. "Which Myriad" is not an axis: the activation-source fold (`equiv_tag`)
  already numbers the two Myriads as one class.

**What removing the count axis would buy: not enough.** The 11 heavy seeds
(`build/scratch/et1110/heavy.json`) were run oracle-on (`MTG_MANA_ORACLE=2`, 11 threads, Release at
b8489f322 plus temporary env probes; the probes are reverted).

| arm | wall | applies (DECISION-COST) | decisions that hit the 300-priced-s budget | CAPPED-DECISION flags |
|---|---|---|---|---|
| shipped (0/1/2 picks) | 4:53 | 13.0M | 7 | 10,265 |
| drop the `{}` pick | 4:35 | 7.5M | 6 | 10,144 |
| no partial picks (2 only, i.e. `allow_partial` off) | 4:29 | 7.5M | 6 | 10,143 |

All three arms win on the same turn in all 11 games. Even dropping the whole count axis, which is a lossy
change to a deliberate deck policy, leaves about 10k CAPPED-DECISION flags on 11 games. CAPPED-DECISION fails
the gate, so a 5,000-game gate would still fail on cost. The budget-bound decisions take their full 300
priced seconds whatever the menu width, so wall falls only 8%.

* `{}` (crack Myriad and fetch nothing) is dominated in real-information terms by not activating at all in
  THIS deck. Its only upside is the seeded post-shuffle library. It is NOT dominated engine-wide: Myriad is
  also in Unpredictable Cyclone (Brainstorm/Ponder leave a known top that a shuffle clears), and
  graveyard-land watchers exist. The existing "nothing legal to find" crack clamp (TurnSolver.cpp,
  SacLandSearch) carries the same unguarded argument. Removing `{}` would be a sound collapse only with a
  per-state predicate (no known library top, no land-to-graveyard or land-leaves watcher). It buys 0.4%
  CAPPED and ~6% wall here, so it was not implemented.

**Verdict: 5l PARKED.** No identity fold is available. The gate stays untractable (the earlier ~16 h+
estimate with half the games CAPPED stands). See the checkpoint doc's 5l bullet for the USER decision.
