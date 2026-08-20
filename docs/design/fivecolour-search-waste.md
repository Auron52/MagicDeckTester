# FiveColour search-waste census + the free-cast candidate prune

> ## STATUS 2026-08-20b (verify class SOLVED = find #4; memo through its FULL adoption gate,
> awaiting USER call)
>
> All four key holes closed; the enum memo's key is now COMPLETE under verification. Gate
> evidence, all on the epoch-clear build unless noted:
> * Verify: **604,817 hits / 0 MISMATCHES** on the heavy fivecolour game (slow44), clears=0
>   at cap 262144; digest + avg unchanged vs baseline.
> * Identity: smoke 36/36 levers-off (fuel fold byte-inert) AND smoke 36/36 with memo ON;
>   **regression mode 60/60 with memo ON, 0 configs changed, reference gate clean** (132 ok /
>   76 repaired / 0 play-drift / 0 contract-fail). The memo is byte-identical at suite scale.
> * Perf (interleaved off/on pairs, quiet box, slow44): pre-epoch-clear 37.81 -> 34.50
>   (**-8.7%**); with epoch-clear 38.76/38.76 -> 35.35/35.12 (**-9.0%**). Arm variance ~0.3%.
> * Epoch-clear added: cross-epoch entries were unreachable garbage (hits require epoch
>   equality) retaining promoted plan vectors; the cache now clears on decision change, so
>   memory is bounded by ONE decision and the cap is a same-decision backstop. At the 8192
>   default cap the heavy game shows 45 clears yet only -5% hits and wall within ~1% of the
>   262144 config -- the cap barely matters post-epoch-clear.
> * **ADOPTED default-on at 763bb22** (=0 hatch) under the user's standing no-regression
>   adoption directive (2026-08-20) -- the memo is a pure caching layer with zero behavior
>   delta, so it is not one of the user-reviewed judgment classes. Pre-push gates re-run on
>   the flipped binary with pure defaults: smoke 36/36 + regression 60/60, 0 changed,
>   reference gate clean. Cap default stays 8192 (post-epoch-clear ~1% wall vs 262144 on the
>   heaviest game; MTG_ENUM_MEMO_CAP to raise).
> * MTG_5C_SSM financing: DELIVERED. **SSM ADOPTED default-on at 5efeb1d** (USER 2026-08-21:
>   "change it to searched if we can do so without much additional cost"; =0 hatch). Post-memo
>   the lever's cost on the heavy game is +0.7% (interleaved quiet-box pairs, was +9% pre-memo)
>   -- the memo caches the extra m2 enumerations. Suite A/B: 4 regression keys digest-only at
>   IDENTICAL averages (30 games re-lined at the same per-game score, d0 untouched); smoke
>   d3_s1001 IMPROVED 4.9600 -> 4.9533 (one game a turn faster). GT accepted from the inspected
>   runs; final gate smoke 36/36 + regression 60/60 on the flipped binary. The interior second
>   main is now searched -- the last greedy step inside the search window is gone (user core
>   bar). Overnight keys rebaseline on the next overnight run.

> ## STATUS 2026-08-20 (items 1+5 dig: THREE REAL KEY HOLES found via MTG_ENUM_MEMO_VERIFY;
> memo re-opened but blocked on ONE residual verify class)
>
> USER direction: pursue (1) the fs-nest duplication and (5) the slow-game tail. They are ONE
> problem: the tail is fivecolour d5/d6 (15 SLOW-GAMEs in the post-adopt overnight, all
> fivecolour, 30-107s CONTENDED = ~37s quiet; census of the worst: 1.87M harvests, 71% dup,
> fs5/fs6 78-88%). MTG_ENUM_TIME (new instrument, committed): 36% of that game's wall is
> FSLine enumeration+MoveOrder (1.28M calls, ~28us each).
>
> KEY HISTORY CORRECTION: the 2026-08-14 MTG_ENUM_MEMO rejection ("1% hit rate at d5") was
> mis-attributed. Real causes, measured today: (a) the 8192 clear-on-full cap thrashed
> (env-ized: MTG_ENUM_MEMO_CAP; at 256k, 0 clears); (b) COVERAGE -- the memo wrapped only
> EnumeratePlansWithLand, and FSLineTail's m2 enumeration (the hottest sites, ~1.1M calls)
> calls EnumeratePlans directly when Main2Drop is off (the shipped config). M2 twin added
> (EnumeratePlansM2Memoized): hits 15.7k -> 609.7k (47%), wall 37.2 -> 33.3s (**-10.4%**) on
> the heavy game. STILL DEFAULT OFF: adoption blocked on the residual verify class below.
>
> THREE REAL KEY HOLES the verify harness caught (all LIVE for FSLineCache/TT before today,
> independent of the memo; all fixed in BuildSimKey/BuildBreakpointKey):
> 1. **Planeswalker loyalty + once-per-turn flag** (dedicated fields, never folded; Dominance.h
>    folded both all along). Jared's auto-resolved -3 with no targets changes nothing else the
>    key sees -> "L5 unused" and "L2 used" collided. Killed 297 of 311 mismatches.
> 2. **Live-library ordered digest**: fetch REMOVALS and scry-bottom REORDERS break the
>    "size => content" clairvoyance argument (spent fetchland sits in a graveyard folded only
>    when retrace is live). One polynomial pass, one Fold.
> 3. **Archangel free-cast bank** (this-turn counter, same-life states can differ) + **live
>    condemnation stamp** in BuildBreakpointKey (the m2 filter changes the harvest) + **active
>    scripted pins** (top/etbdig/tutor/reorder/tapmode -- a live pin steers nested resolution).
>
> ~~OPEN: 14/609k verify mismatches remain~~ **SOLVED 2026-08-20 (find #4): Deathrite
> graveyard-land FUEL.** A stored-vs-fresh fingerprint of the un-keyed inputs at the mismatch
> site named it directly: stored `gy0=Misty Rainforest` / pool wild=7 vs fresh `gy0=` /
> wild=6 -- same battlefield, hand, and library. A land in the OWN graveyard is +1 live mana
> source for a gy_land_exile_mana permanent (Deathrite), and that extra wild flips
> FetchCandidates' untapped/spendable ranking (triome vs shock order) -- but the graveyard is
> folded only when RETRACE is live, so the two states shared a key. Fixed in BuildSimKey: fold
> the graveyard LAND COUNT (colours never matter; consumption is count-driven), gated on a land
> being there AND a gy_land_exile_mana card of that player on battlefield or in hand -- every
> deck without such a card keeps the exact prior key. Like finds #1-#3 this hole was LIVE for
> FSLineCache/TT before the memo existed. Method note for the next residual class: the
> fingerprint (store a string of the un-keyed suspects at memo promotion, diff at verify
> mismatch) beat five rounds of fold-and-guess in one run.

USER direction (2026-08-20): "analyze to make sure we aren't doing more work than absolutely
necessary in the search. Cases where we re-search for a particular card should be flagged as
potential waste. It might also make sense to prune what we consider in the Maelstrom Archangel
free-cast. In particular, cheap cards seem like they should be at a disadvantage."

## The census (MTG_CONSIDER_STATS, 20 fivecolour games at the production d6 tier)

Duplicate harvests -- CollectActions calls at a state some earlier call at the SAME site already
harvested (BuildSimKey identity), i.e. the directly-memoizable duplication:

| site           | calls   | distinct states | duplicate calls | dup % |
|----------------|---------|-----------------|-----------------|-------|
| enum.m2.fs4    | 826,547 | 348,965         | 477,582         | 58%   |
| enum.m2.fs5    | 712,173 | 171,883         | 540,290         | 76%   |
| enum.m2.fs6    | 422,180 | 67,665          | 354,515         | 84%   |
| enum.m2.fs3    | 467,947 | 272,130         | 195,817         | 42%   |
| solve.m2.fs1.m2solve | 229,393 | 134,924    | 94,469          | 41%   |

The deep FSLine nests re-harvest the SAME post-combat states at 58-84% duplication; the by-card
view says whose re-consideration it is (Unite the Coalition alone: 1.05M considerations at fs6,
650k at fs5; then Archangel, Jared, Garth, Bolas). ~2.3M of ~3.9M total harvest calls are
duplicates on this sample.

**Why this is recorded rather than fixed:** the obvious fix is a memo, and MTG_SOLVE_MEMO was
MEASURED AND RETRACTED (2026-08-19: BuildSimKey cost > its then-28% hit-rate savings, +12% wall).
The census says the ENUM-harvest hit rate at the deep nests is 58-84%, not 28% -- the economics
may differ at the right site with a cheaper key. A revisit should memo the HARVEST (CollectActions
product), key it on something cheaper than full BuildSimKey, and hold the same bar: quiet-box
perf pair + byte-identity. Open item, deliberately unbuilt.

## The free-cast prune (MTG_FREECAST_PRUNE, **ADOPTED DEFAULT-ON 2026-08-20**, =0 hatch)

With N banked Archangel charges, every CastFromHand candidate used to gain one free variant per
slot -- the candidate set doubles at exactly the states the census says are hottest. The lever
emits free variants only for the top (slots + 2) candidates by mana value (ties kept; human play
exempt -- the viewer keeps every legal option). Rationale: inside any subset that casts both,
giving the free slot to the dearer card weakly dominates (same board, >= leftover mana); the +2
margin covers colour-tight cases where the MV swap is not payable.

Train (fivecolour, per-deck regression vs fresh GT at fb72b24):
* d3 x2 + d5 x2: **byte-identical** (digest PASS) -- the search never actually chose a below-floor
  free cast; the variants were pure enumeration cost.
* d0: avg identical (5.8490), digest-only (greedy line reorder at equal score).
* Wall: searched sum 1863s vs ~2000s baseline (**~-7%**), same box, back-to-back.

Pending before adoption (USER call): held-out overnight keys; on adoption flip default-on with
=0 hatch + d0 digest rebaseline.

## Related levers measured this session (fivecolour searched-wall, same box)

* MTG_5C_CONDEMN (root-turn authority build): **-16.3%**, searched keys byte-identical to the
  measured fix arm; 3 known +1-turn residuals / 2800. Pending USER adoption.
* MTG_FREECAST_PRUNE: **~-7%**, searched byte-identical (above). Pending held-out + USER.
* MTG_5C_SSM: +9% (greedy-free interior m2) -- ADOPTED 2026-08-21 at 5efeb1d, +0.7% post-memo
  (see the 2026-08-20b STATUS above).
* MTG_ROLLOUT_HORIZON=K (leaf-capped rollout tail): **REJECTED** K=2 (-4.5% for +0.065..+0.175
  red) and K=4 (-9% for +0.010..+0.020 red) -- the greedy tail does real work the leaf cannot
  replace on this deck.
