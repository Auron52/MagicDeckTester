# The land-Aura main-1 line (MTG_PO_LAND_AURA_DOMAIN)

Status (2026-11-17): fix on branch `sea-landaura-fix` (not pushed, GT not accepted), flag
**MTG_PO_LAND_AURA_DOMAIN, default ON** (HeuristicArm slot `PO_LAND_AURA_DOMAIN`). Smoke: lever =0 is
byte-identical to the base binary on all 223 cases; ON moves 9 cases, all land-Aura decks. Held-out A/B: Sea
Creatures -16 turns over 350 games (16 faster, 0 slower), the other five land-Aura decks 0 / 0. Found during the Sea Creatures analysis (Stage 5 A/B root cause,
`logs/sea-research/ab-rootcause.md` in the sea-creatures worktree): eleven held-out games were a turn faster
with a second-main admission, and every one had the same shape -- main 1 casts a **land Aura** (Utopia Sprawl /
Wild Growth), then the 3-drop ramp piece (Worn Powerstone / Overgrowth) paid by the newly enchanted host's
boosted tap. Main 1 could not express that line. This document is why, and the fix.

## Mechanism

Both failure modes are the same gate: the **payable-order decider** (`PayableCastOrderCoreImpl`,
`src/ai/ManaPayment.cpp`) re-orders a cast list only when its DOMAIN admits the order -- an order holding a
funding producer, a reveal-or-pay cast, a credited producer (ritual float / rock / Treasure) or a domain
widener. A land Aura is none of those. Outside the domain the decider returns the order unchanged, so "the
one order we already had" is the only order ever tried.

**Mode (b), the APPLY** -- Sea Creatures s8810001 gi64 T2. Board: Breeding Pool (carrying a Sprawl) plus the
new Forest. The enumeration admits the plan `{Utopia Sprawl -> Breeding Pool, Worn Powerstone}`: its
sequential walk pays the Sprawl first (Forest pays {G}, the Pool then taps for three). But the apply realises
casts in `CastOrderLess` order, which puts the mana rock (generic tier) first: Powerstone takes Pool + Sprawl +
Forest and nothing is left for {G}. The decider was asked to fix the order and declined (out of domain), so
BOTH apply worlds (executor `AIEngine.cpp` and rollout `TurnSolver` apply, which share the decider in lockstep)
dropped the Sprawl. The search therefore valued the pair as "Powerstone only" -- which is exactly the
"enumerated but never picked, even at b0" finding in ab-rootcause.md.

**Mode (a), the ENUMERATION** -- s8820001 gi39 T2 `{Utopia Sprawl, Overgrowth}` and s8830001 gi75 T4
`{Wild Growth, Overgrowth}`. An aura-only subset passes the `MTG_AURA_FUNDS_AURA` gate and the scalar ceiling
and reaches `SubsetPayableSequential` (TurnSolver.cpp). Its real walk tries the provider's rank order -- under
SeaCreaturesProvider that is Overgrowth first, which spends every land and leaves no {G} for the Aura -- and
then retries the decider's order at the ENUMERATION site (`ApplyPayableCastOrderAt`). Out of domain, the
decider handed back the same order, the retry was skipped, and the subset was refused (`ef-walk-fail` in
`MTG_DBG_MULTI` traces). The line never reached the menu.

**Split hosts** (same mechanism, one level down). `PoLandAuraHoldFor` -- the hold that keeps a land-Aura host
out of the walk's pool until a cast needs it, so the Aura's bonus is visible -- held ONE host only and
returned "no hold" for Auras naming two different lands. On the gi39 board `{Sprawl -> Pool, Overgrowth ->
Forest}` pays exactly like the same-host variant (the Forest pays the Sprawl and then takes Overgrowth while
tapped; the Pool, now with two Sprawls, taps for three), but with two hosts the order was neither admitted to
the domain nor walked with the Pool held, so it stayed unfindable.

## Fix

All in `src/ai/ManaPayment.cpp`, behind `PoLandAuraDomainOn()`:

1. **Domain admission.** After the existing domain checks, `if (!any && PoLandAuraDomainOn())` admits an
   order for which `PoLandAuraHoldFor` finds a land-Aura cast naming an untapped land we control. At the apply
   site (site 0) it is walked as a producer order (the host-holding plain walk, or the exact walk when that is
   in force); at the enumeration site (site 1) the reordered candidate is then priced by the REAL sequential
   walk (`run_walk`), so the admission can only ADD subsets that genuinely pay, in the order the apply will
   realise. The casts are the plan's; only their realisation order changes, and only when the given order does
   not pay. No pick is made: this is the apply realising the plan the search chose, and the enumeration
   offering a legal line it used to refuse.
2. **Several hosts.** With the lever on, `PoLandAuraHoldFor` holds every distinct host (`la.hosts`,
   per-host yield, per-Aura host index). Both walks (`FirstUnpayablePosImpl`, the exact walk
   `ExactOrderWalkOnce`) release each held host on its own, only while the current cast cannot be paid
   without it, crediting the bonus of the Auras that resolved on it while it was held. Which host taps first
   is a property of the PAYMENT, not of the plan, so "does this order pay" walks every release order
   (`PoLandAuraWalkAnyRelease`, h! walks; two hosts = two walks). One host: the single walk, byte-identical
   to before.

With the lever off (`MTG_PO_LAND_AURA_DOMAIN=0`), a land Aura admits nothing and two hosts give no hold:
byte-identical to the pre-fix engine (measured: smoke, 223 of 223 cases).

## Flag

`MTG_PO_LAND_AURA_DOMAIN`, **default ON**, read through `EnvOn` plus the HeuristicArm slot
`PO_LAND_AURA_DOMAIN` (a manifest job can pin it per arm with `"flags": {"MTG_PO_LAND_AURA_DOMAIN": false}`).
It gates both halves: the domain admission and the several-host hold. It rides on `MTG_PO_LAND_AURA_HOLD`
(default ON): with the hold off, `PoLandAuraHoldFor` returns null and this lever has nothing to admit.
Every new path starts at `PoLandAuraHoldFor`, whose first line returns null unless the deck holds a land Aura
(`state.deck_has_land_aura`, a deck constant), so a deck without one cannot reach any of it.

This is an apply / enumeration correctness fix, not a heuristic: no option is chosen for the search. The
enumeration now offers a subset that genuinely pays (the real sequential walk still decides), and the apply
realises the plan's own casts in an order that pays them, and only when the given order does not.

## Unit tests

`test/unit/test_land_aura_domain.cpp` (`-tc="Land-Aura*"`):

- **(b)** Powerstone + Sprawl: the apply-site decider reorders `{Worn Powerstone, Utopia Sprawl}` Sprawl first,
  `ApplyPlan` casts both; lever off keeps the given order.
- **(a)** the gi39 board before the land drop (Forest #9 in hand): with the lever on, `EnumerateMainPlans` offers
  `{Sprawl -> Pool, Overgrowth -> Pool}` and `ApplyPlan` casts both. The mechanism is checked at the enumeration
  site: on the post-land board the rank order `{Overgrowth, Sprawl}` fails `OrderPaysSequentially`;
  `ApplyPayableCastOrderAt` (the site-1 decider, exactly `SubsetPayableSequentialTail`'s retry) returns
  `{Sprawl, Overgrowth}`, which pays; with the lever off the order is kept.
- **(c)** split hosts `{Sprawl -> Pool, Overgrowth -> Forest}`: the same enumeration-site check, the apply-site
  reorder (on) / keep (off), and `ApplyPlan` realises both with Overgrowth on the Forest.
- `test_accomplished_alchemist.cpp`, "Cast order, PROPOSED: {Wild Growth, Living Wish} on two Forests": the probe
  asserted the old defect (the PROPOSED order strands Wild Growth in both worlds). It now checks that with the lever
  off (`on_nodomain == 0`) and that with it on the decider realises the plan Aura first (`on == 2`).

Two findings from building the tests, both left open:

- **The synthetic board's flat-pool rescue admits what the recorded game's refuses.** On the gi39 board rebuilt in
  a test, `SubsetPayableWithFilters` (TurnSolver.cpp; it pays land Auras FIRST in its pass 0 and floats the Pool's
  spare {U}) admits `{Sprawl, Overgrowth}` before the sequential walk is consulted, so the pair is enumerated even
  with the lever off. In the recorded game the same subsets fall through it to the walk (`MTG_DBG_MULTI=2` labels
  them `ef-walk-fail`; reproduced also with `MTG_SPWF_CANON_MEMO=0 MTG_PAY_PROBE_CANON=0 MTG_SPWF_SUPPLY_GATE=0`).
  Why is NOT root-caused (the board was rebuilt as the log has it). The fix acts after that gate, so it does not
  depend on the answer; the lever-off half of test (a) therefore asserts on the decider order, not on enumeration.
- **The enumerator folds host variants of equal outcome.** `EnumerateMainPlans` returns only the same-host plan
  `{Sprawl #2, Overgrowth #2}`; once that exists a split-host plan never comes back from it. Test (c) builds the
  split plan by re-targeting Overgrowth in the same-host plan.
- The two casts share `CastOrderRank` 5 under `SeaCreaturesProvider`: the walk breaks the tie by name (Overgrowth
  first), while the apply comparator `CastOrderLess` puts the Sprawl first.

Full `mtg-test`: 3432 cases, all pass.

## Smoke footprint

Three runs of the same smoke manifest (223 cases), all from this worktree's checkout:

- **base**: the binary that accepted the Sea Creatures GT (the sea-creatures worktree's `test/logs/smoke/mtg.run`,
  built from 3a21f5afb with a test-only diff; same `src` tree as base 48b56296e);
- **on**: this branch, lever default ON (`test/regression.sh --smoke`);
- **off**: this branch, `MTG_PO_LAND_AURA_DOMAIN=0` (`mtg --batch` on the harness's manifest).

| comparison | cases that differ |
|---|---|
| off vs base | **0** of 223 (byte-identical) |
| on vs base | **9**, every one a land-Aura deck: Accomplished Alchemist d0 / d5 / 2HG d5, Eldrazi Displacer Flicker d0, Jumbo Cactuar d0, Sea Creatures d0 / d3 / d5 / 2HG d5 |

Bruna and Fungus do not move in smoke. Every other deck is byte-identical between on and base.

**The other 39 smoke failures (14 decks without a land Aura) are NOT this change.** The base binary reproduces
each of them digest for digest: Anti-Lifegain (+2HG), Creature Giving (+2HG), Critter, Cyclone (+2HG), FiveColour
(+2HG), Galadriel (+2HG), LHC, Melira (+2HG), Mill (+2HG), Minotaur 2HG, PD, SelesnyaV2, Vampires (+2HG),
WhiteKnights (+2HG). So base 48b56296e's own `src` does not reproduce its own `test/regression_gt.txt` for those
rows. The Sea Creatures accept (b96973a0a) ran only the Sea cases in smoke (its `test/logs/smoke/manifest.json`
holds the 4 Sea jobs; regression ran Sea + Hinata, overnight Sea only), so those rows were never re-measured on
that branch. That drift belongs to the branch the fix sits on, and has to be settled there before any accept.

## Correction: the gi75 validator verdicts

`logs/sea-research/ab-rootcause.md` (sea-creatures worktree) recorded, for s8830001 gi75 T4, that
`--validate-line` also called `Utopia Sprawl + Overgrowth` (and `Sprawl + Sprawl`) legal on that board, and
flagged it as a probable validator false positive. It is one. The board is Island, Island and the new Forest. The
Forest is the only {G} source AND Utopia Sprawl's only legal host (Enchant Forest). Whatever pays the Sprawl's {G}
is the Forest, so after it Overgrowth ({2}{G}) has no {G} left: the line is UNPAYABLE, and the engine's refusal
is correct. The ManaOracle verdict is the defect (owed to whoever owns ManaOracle). The line the fix finds there is the one ab-rootcause.md named: `land=Forest #9; Wild Growth -> Island #17` (paid {G} by the Forest), then `Overgrowth -> Island #17`, paid {2}{G} by Island #17 with Wild Growth's {G} plus Island #18 (measured, s8830001 gi75 d5 b20: T4 casts both, win T7; lever off T8).

## Held-out paired A/B

One pooled `mtg --batch` (`--threads 24`), lever ON vs `MTG_PO_LAND_AURA_DOMAIN=0` (per-job HeuristicArm pin), 350
paired games per deck (same `seed` + `game_index` in both arms), each deck at its play setting, the six decks that
hold a land Aura. Seeds held out: deck k base `9100001 + k*100000` (Sea Creatures 9100001, Accomplished Alchemist
9200001, Bruna 9300001, Eldrazi Displacer Flicker 9400001, Fungus 9500001, Jumbo Cactuar 9600001). Loss = 9.
Units are the per-game search units (`MTG_TIER1_STATS`).

| deck | setting | n | on | off | delta (se) | faster | slower | net turns | units on/off |
|---|---|---|---|---|---|---|---|---|---|
| Sea Creatures | d5 b20 | 350 | 5.9314 | 5.9771 | -0.0457 (0.0112) | 16 | 0 | **-16** | 0.9990 |
| Accomplished Alchemist | d5 b20 (value_play) | 350 | 5.0171 | 5.0171 | 0 | 0 | 0 | 0 | 1.0013 |
| Bruna | d7 b20 (value_play) | 350 | 4.8629 | 4.8629 | 0 | 0 | 0 | 0 | 1.0003 |
| Eldrazi Displacer Flicker | d5 b20 | 350 | 4.0257 | 4.0257 | 0 | 0 | 0 | 0 | 1.0008 |
| Fungus | d5 b20 | 350 | 4.4629 | 4.4629 | 0 | 0 | 0 | 0 | 1.0003 |
| Jumbo Cactuar | d6 b20 (value_play) | 350 | 4.1314 | 4.1314 | 0 | 0 | 0 | 0 | 1.0002 |

Every deck is net <= 0 turns, and **no game is slower in any deck**, so there is no slower game to root-cause.
The play does change outside Sea Creatures (games whose units differ: Alchemist 26, Bruna 30, EDF 55, Fungus 11,
Jumbo 7, Sea 241), but in those five decks no win turn moved. Units are within +0.13% everywhere (Sea -0.10%).
Sea Creatures' 16 faster games, all by one turn (base 9100001): gi8, 16, 28, 39, 47, 75, 101, 140, 147, 171, 202,
214, 255, 263, 278, 345.

Raw: `build/scratch/sea-landaura/ab/` (manifest, err with `[win]` lines, t1.txt, result.txt), analysed with
`ab/analyse2.py`.

## Open

- Why the recorded gi39 game's `SubsetPayableWithFilters` refuses `{Sprawl, Overgrowth}` while the same board in a
  unit test admits it (see Unit tests). Does not affect the fix.
- Accomplished Alchemist's PROPOSED cast order carries an `AURAS_FIRST` amendment (`AA_CAST_ORDER_AURAS_FIRST`).
  For the `{Wild Growth, Living Wish}` shape this fix already realises the Aura first, because the given order does
  not pay. Whether `AURAS_FIRST` is still wanted as its own lever is the USER's call.
- The ManaOracle `--validate-line` false positive on gi75 (above).
- The 39 pre-existing smoke GT mismatches on the base branch (above).
