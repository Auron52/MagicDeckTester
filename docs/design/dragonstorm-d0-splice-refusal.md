# Dragonstorm d0: never commit a spliced plan no legal order can pay (`MTG_DRAGONSTORM_D0_SPLICE_REFUSE`)

Status: IMPLEMENTED, **default ON** (heurarm slot `DRAGONSTORM_D0_SPLICE_REFUSE`, appended); `=0` is
byte-identical to the parent commit 77e094a2. 2026-10-14. USER: *"Sure, let's fix it."* (the open proposal at
the end of `dragonstorm-user-cast-order.md` "Round 3", and `final-ab-2026-10-13.md`'s Dragonstorm follow-up:
*"at d0 the runner can still commit to a spliced plan no legal order pays in full and waste the turn -- refuse
such plans behind their own lever, measured at d0."*)

## The defect

With the USER's cast order on, `DragonstormProvider::OrderedSubsetRealisable` judges a subset holding a
spliced Desperate Ritual on its realised order (comparator, `ArrangeCastOrder`'s splice legality, then the
payable-order fallback) with the exact walk, which enforces splice legality (a revealed copy must still be in
hand). But it refused the subset only when the walk failed **at the spliced cast**. The common depth-0 shape
fails **earlier**:

{Seething Song, Desperate+splice, Desperate+splice, Desperate, Dragonstorm} on two lands. The order-free totals
admit it, because the only order that funds Seething casts a plain Desperate first and then splices a copy that
has already left the hand. The legal arrangement (spliced casts before the plain one) fails at its first cast,
Seething; the fallback finds nothing legal either; the subset passed; the depth-0 runner (a permitted greedy
site, `GreedyPermit(D0Runner)`) committed it; and the executor cast the one payable spell, a plain Desperate,
floating three red that nothing spent. The copy was gone and the turn was wasted.

## The games (Dragonstorm `--depth 0 --ignore-play-profile`, `MTG_USER_ORDER_TRACE=2`)

Repro: `mtg decks/Dragonstorm/Dragonstorm.cod --profile decks/Dragonstorm/Dragonstorm.profile.json --games 1
--seed <s> --game-index <gi> --depth 0 --ignore-play-profile`. "d8 b0" is the legal optimum the earlier rounds
measured at `--depth 8 --budget-ms 0`.

| game | parent (77e094a2) | lever on | d8 b0 | the wasted plan on the parent |
|---|---|---|---|---|
| regression d0 s2857 gi855 | T5 | **T3** | T3 | T2 (Mountain + Territory): `Seething, Desperate:s1, Desperate:s1, Desperate, Dragonstorm`, `pays=0` (Seething) -- casts `Seething paid=0, Desperate splice=1 paid=0 (x2), Desperate paid=1, Dragonstorm paid=0`. T3 again: `Seething x2, Desperate:s1, Desperate, Dragonstorm`, again only the plain Desperate. Two Desperates thrown away. Lever: T2 casts nothing, T3 `Desperate x3, Seething x2, Dragonstorm` |
| final-ab d0 s6190581 gi580 | T3 | **T2** | T2 | T1 (one land): `Rite, Seething x2, Desperate:s1, Desperate, Dragonstorm`, `pays=1` -- casts Rite and a plain Desperate only. Lever: T2 `Rite, Pyretic, Desperate x2, Seething x2, Dragonstorm` |
| final-ab d0 s6190701 gi700 | T5 | **T4** | T4 | T3 (two lands): `Seething x2, Desperate:s1, Desperate, Dragonstorm`, `pays=0` -- the plain Desperate only. Lever: T4 `Pyretic, Desperate x2, Seething x2, Dragonstorm` |
| final-ab d0 s6191309 gi1308 | T6 | **T5** | T5 | T4: the same shape, the plain Desperate only. Lever: T5 `Desperate x2, Seething x2, Dragonstorm` |
| regression d0 s2759 gi757 | T5 | T5 | T3 | not this class on the parent: T2-T4 cast nothing (its old T2 kill was the illegal splice); a different d0 gap |

The lever reaches the d8 b0 legal optimum on all four games of the class.

## The change

`src/ai/DecisionProviders.cpp`, `DragonstormProvider::OrderedSubsetRealisable`: after the existing tests, a
subset holding a spliced cast whose realised order AND payable fallback both fail the exact walk is refused
**wherever** the walk fails -- only while `TurnSolver::InD0Runner()` (a `GreedyPermit(D0Runner)` is live) and
only with the lever on. Both subset evaluators already call this hook (Solve's `consider` and `EnumeratePlans`),
so the d0 runner simply moves to its next candidate. No second estimator: the verdict is the existing realised
order + `ExactOrderFirstUnpayable` + `ApplyPayableCastOrder`. Reader `DragonstormD0SpliceRefuseOn()`
(`src/ai/EngineFlags.h`, `EnvOn(.., true)` + heurarm). Diagnostic: under `MTG_USER_ORDER_TRACE` each refusal
prints `[uo-d0-splice-refuse] gs= t= pool= fail= card= seq=`.

**The search window is untouched.** A rollout (HorizonLeaf) and every searched frame keep the at-the-splice
test, so d3 / shipped play is byte-identical (measured below). The no-greedy rule is not engaged: the depth-0
runner is a permitted greedy site and the change only removes a candidate it could not legally play in full.

**Is it a pure legality fix?** In intent yes: the refused plan cannot be cast in full in any legal order. In
practice the verdict is the exact walk's, and the walk has one known pessimism (below) -- in the audit, 23 of
the 24 refused-class plans the parent committed failed at execution exactly as the walk said; 1 of 24 the
executor paid in full. Default ON on that basis and on the measurement.

Unit cover (`test/unit/test_dragonstorm_followups.cpp`, 2 cases): the gi855 subset is admitted outside the d0
runner on both arms, refused inside it with the lever on, admitted with it off; a legally payable spliced set
(four Mountains, spliced first) and a non-spliced unpayable set stay admitted; the USER order off disables it;
and at the Solve level the d0 runner on that hand commits the unpayable spliced plan with the lever off and does
not with it on. Full suite 1036 / 1036.

## Measurement (ONE pooled batch, 276 jobs / 107,400 games, `--threads 6`, fresh seeds 10300001+)

Arms: lever on (default) vs `MTG_DRAGONSTORM_D0_SPLICE_REFUSE=0`, paired by (deck, cell, game index); seed of
game gi = 10300001 + 200000 x deck index + 50000 x cell index + gi; unwon = 9. Builder / analyser:
`logs/ds_splice/make_ab.py` (scratch; the shape of `scripts/dig_choice_ab/make_dig_ab.py`). Cells: d0
(`--depth 0`), d3 b10, shipped (no depth key, budget 20). Only Dragonstorm holds a splice card (Desperate
Ritual is the only `splice_onto_arcane` card in `cards.json`, and no other deck list holds it), so its 2HG case
is the other splice cell and five non-splice decks are the identity set.

| deck | cell | games | lever on | =0 | on - off /game | t | faster | slower | net | chunk digests identical | search units on/off |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Dragonstorm | d0 | 40000 | 5.0236 | 5.0270 | -0.0034 | -7.74 | 95 | 4 | **-135** | 1/40 | -- |
| Dragonstorm | d3 b10 | 500 | 4.3600 | 4.3600 | 0 | -- | 0 | 0 | 0 | 20/20 | 1.000 |
| Dragonstorm | shipped b20 | 500 | 4.3860 | 4.3860 | 0 | -- | 0 | 0 | 0 | 20/20 | 1.000 |
| Dragonstorm 2HG | d0 | 10000 | 5.1994 | 5.2010 | -0.0016 | -2.67 | 11 | 1 | **-16** | 1/10 | -- |
| Dragonstorm 2HG | d3 b10 | 200 | 4.4900 | 4.4900 | 0 | -- | 0 | 0 | 0 | 8/8 | 1.000 |
| Hinata2 | d0 / d3 b10 | 400 / 100 | 6.8150 / 5.6700 | same | 0 | -- | 0 | 0 | 0 | 4/4, 4/4 | 1.000 |
| BreachingDragonstorm | d0 / d3 b10 | 400 / 100 | 4.7775 / 3.1500 | same | 0 | -- | 0 | 0 | 0 | 4/4, 4/4 | 1.000 |
| Mirrorwing Dragon | d0 / d3 b10 | 400 / 100 | 5.8950 / 4.2000 | same | 0 | -- | 0 | 0 | 0 | 4/4, 4/4 | 1.000 |
| Goblins | d0 / d3 b10 | 400 / 100 | 4.0475 / 3.7300 | same | 0 | -- | 0 | 0 | 0 | 4/4, 4/4 | 1.000 |
| Soldiers | d0 / d3 b10 | 400 / 100 | 4.4650 / 4.1900 | same | 0 | -- | 0 | 0 | 0 | 4/4, 4/4 | 1.000 |

Per-deck net (all cells): Dragonstorm **-135**, Dragonstorm 2HG **-16**, every other deck 0 (byte-identical).
Identity of the `=0` arm against the parent commit's binary (a separate batch, the parent binary, the same job
names and seeds: Dragonstorm d0 5,000 / d3 100 / shipped 100, 2HG d0 2,000 / d3 50, each identity deck d0 100 +
d3 25): **27 / 27 jobs byte-identical, 7,875 games**.

### Every slower game, both recovery stages (lever-on binary)

| deck | cell | gi | seed | on | =0 | stage 1 (`--depth W --budget-ms 100`) | stage 2 (`--depth 8 --budget-ms 0`) |
|---|---|---|---|---|---|---|---|
| Dragonstorm | d0 | 1480 | 10301481 | 3 | 2 | 2 (d2) | 2 |
| Dragonstorm | d0 | 12048 | 10312049 | 9 | 6 | 5 (d6) | 5 |
| Dragonstorm | d0 | 13508 | 10313509 | 6 | 5 | 5 (d5) | 5 |
| Dragonstorm | d0 | 19866 | 10319867 | 6 | 4 | 4 (d4) | 4 |
| Dragonstorm 2HG | d0 | 6681 | 10506682 | 7 | 6 | 5 (d6) | 5 |

All five recover at stage 1 (three of them to a turn better than the `=0` arm's).

Root causes:

* **gi 1480, 12048, 13508, 19866 -- the `=0` arm won off a lucky phantom.** gi1480 (T1), gi13508 (T2) and
  gi19866 (T1) commit `Rite of Flame, Ruby Medallion, Seething Song, Desperate:s1, Desperate, Scourge` on one land
  (`pays=2`: the legal order fails at Seething); executed, it casts exactly **Rite of Flame -> Ruby Medallion**
  and nothing else. gi12048 (T4) commits `Rite, Seething, Desperate:s1, Desperate, Apex, Irencrag` (`pays=1`);
  executed: Rite, a plain Desperate, the Apex unpaid, then the continuation casts the Medallion off that float.
  Either way the partial execution is the right play, which the d0 runner cannot choose on its own: the payoff prune (`SubsetWastesAccelerant`) drops a ritual with no same-turn payoff, and
  `MTG_PAYOFF_PRUNE_REDUCER_SINK` admits "ritual into Medallion" in the search branch list only, because admitting
  it in the greedy policy measured worse at d0 (final-ab-2026-10-13.md). The same phantom route the final A/B
  found behind d3 gi 289 / shipped gi 188. With the lever the phantom is refused and the Medallion comes down a
  turn or more later (gi1480: T2 kill -> T3; gi19866: T1 Medallion -> a T3 Medallion, two Scourges T3/T4).
* **2HG gi 6681 -- an exact-walk pessimism, NOT a legality refusal.** T4, pool `r4 *1` (Mountain, Sandstone Needle,
  Mercadian Bazaar, and the Unclaimed Territory just played), hand Kolaghan, Lathliss, Seething x2, Desperate x2. The `=0` arm commits
  `Seething x2, Desperate:s1, Desperate, Kolaghan, Lathliss` and the executor **pays every cast** (kill T6). The
  walk reads it `pays=4` (Kolaghan unpaid): under `MTG_EXACT_WALK_CCO` the Territory's unit is `{C}` for a
  noncreature cast, and `PayFromPoolLineAware` pays the generic of the first noncreature spell out of it (its
  "surplus" is computed against `future.colorless = 0`), so Kolaghan's {B} -- payable only by the Territory, a
  Dragon being its chosen type -- is gone. Every sub-plan with Kolaghan is refused the same way; the lever arm
  casts Desperate + Kolaghan and kills T7. Recovers at stage 1 (T5). Found, not fixed: see below.

### Audit: what the lever refuses (Dragonstorm d0, the first 5,000 games of each arm, one thread, traced)

Every committed plan (`[uo-order]`) holding a spliced cast, by the walk's verdict and what the executor paid
(`logs/ds_splice/audit.py`):

| arm | walk pays | walk fails, a cast went unpaid (the waste) | walk fails, executor paid all (walk pessimism) | subset refusals |
|---|---|---|---|---|
| `=0` | 236 | 23 | 1 (s10303467 T4: `Medallion, Pyretic, Desperate:s1, Desperate, Kolaghan` -- the Territory / {B} shape; same win turn on both arms) | -- |
| lever on | 238 | **0** | **0** | 112 |

## Found, not fixed

1. **Exact-walk CCO pessimism (2HG gi6681, s10303467).** The line-aware generic treats a creature-only unit
   converted to `{C}` as spare, although a later creature cast owes a colour only that unit can pay. Proposed: in
   `ExactOrderWalkImpl`, give the converted units a `need` equal to the coloured pips later CREATURE casts owe
   beyond what the pool's own colours cover, so `PayFromPoolLineAware` spends real surplus first -- the CCO twin
   of the s7000261 line-aware fix. It changes the exact walk everywhere the USER order reads it (search included),
   so it needs its own lever and a suite A/B; not done here.
2. **Rite of Flame -> Ruby Medallion at d0.** With the phantom route gone, the d0 runner has no way to cast a
   ritual into the Medallion (the four Dragonstorm slower games). `MTG_PAYOFF_PRUNE_REDUCER_SINK` in the greedy
   policy measured +68 at d0 on 2,000 games before this lever existed -- partly, presumably, because the phantoms
   were already doing that job. Worth re-measuring in the d0 runner only (a d0-policy lever, own A/B).
3. **The rollouts keep the at-the-splice test.** The horizon playout can still value a position by committing a
   spliced plan no legal order pays. Extending the refusal to `HorizonLeaf` would change searched play and needs
   the d3 / shipped A/B; not done (this job is confined to the d0 runner).

## Not run

No smoke / regression / overnight tier (the integrator's). The Dragonstorm d0 GT rows will move (regression d0
s2002 gi855 is 5 -> 3); the searched rows should not.
