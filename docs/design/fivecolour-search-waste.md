# FiveColour search-waste census + the free-cast candidate prune

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
* MTG_5C_SSM: +9% (greedy-free interior m2) -- pending USER; fits inside the two savings.
* MTG_ROLLOUT_HORIZON=K (leaf-capped rollout tail): **REJECTED** K=2 (-4.5% for +0.065..+0.175
  red) and K=4 (-9% for +0.010..+0.020 red) -- the greedy tail does real work the leaf cannot
  replace on this deck.
