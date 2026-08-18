# The enumeration admits subsets the board cannot pay: multi-colour sources collapse to `wild`

Self-contained record (2026-08-18). Found while reviewing Anti-Lifegain's cast order; it is NOT an
ordering bug and NOT specific to that deck. **Nothing is fixed yet — this is the spec for the fix.**

## The bug, in two lines

`AddSourceToPool` (src/core/SpellEffects.h, ~6181):

```cpp
if (prod.size() == 1)      { pool.Add(prod[0], amt); }   // Forest -> green
else if (!prod.empty())    { pool.wild += amt; }         // ANY multi-colour source -> wild
```

and `ManaPool::CanPayFlat` (src/core/ManaPool.h) treats one `wild` as satisfying **any** single pip.
So the accounting pool knows *how many* flexible sources there are but not *which colours they can
actually make*. A dual that produces {R}{G} counts as able to pay {W}.

That is the "flat wild-pool approximation" `MTG_AFFORD_AUDIT` already names when it classifies a
dropped cast as `colour-short`.

## Worked repro (exact)

```
mtg "decks/Anti-Lifegain/Anti-Lifegain.cod" --profile "decks/Anti-Lifegain/Anti-Lifegain.profile.json" \
    --depth 0 --ignore-play-profile --budget-ms 0 --seed 1698 --game-index 697 --games 1 --threads 1
```

Turn 5 board: Godless Shrine (W/B), Grove of the Burnwillows (R/G), Ignoble Hierarch (B/R/G),
Forest (G).

* Pool as built: `green 1, wild 3`.
* Committed plan (`MTG_ORDER_TRACE=1`): **Fiery Justice `{R}{G}{W}` + Enlightened Tutor `{W}`**
  -> combined `W2 R1 G1`.
* `CanPayFlat`: deficit = (2-0)+(1-0)+(1-1) = 3, `wild` = 3, so **3 <= 3 -> affordable**.
* Reality: only Godless Shrine makes white. Two white pips are needed. **The set is unpayable.**

The subset is admitted, both casts are declared, the first one to be sequenced takes the single white
and the other is **silently dropped** (`CastSpellFromHand` returns void; the drop is by design, see
the note in `CastOrderLess`).

## Why it matters, and why it looked like an ordering problem

Which cast survives is decided by cast ORDER, so the defect masquerades as an ordering regression:

| arm | T5 order | outcome |
|---|---|---|
| base | `Fiery Justice, Enlightened Tutor` | Fiery Justice resolves, opp 11 -> 1, **wins T5** |
| reviewed order | `Enlightened Tutor, Fiery Justice` | tutor resolves, opp stays 12, **wins T6** |

Base is not playing better here — it wins by luck of plan order. Same shape in antilife gi=777
(Idyllic Tutor vs Swords to Plowshares). USER, correctly: *"The ordering just tells us the order to
cast spells if they are in the plan. It doesn't tell us which spells we should cast"* and *"If we
can only afford Enlightened Tutor or Fiery Justice, then search should find the Fiery Justice and
win."*

**Do not "fix" this by giving tutors a cast-order range.** That was the first proposal and it is
treating the symptom: it makes the order dodge an unsound affordability check rather than making the
check sound.

## Measured frequency (base, no levers)

`MTG_AFFORD_AUDIT=1`, Anti-Lifegain:

* **d0, 200 games:** 12 real drops / 832 attempts (1.4%) — Birds x4, Ignoble Hierarch x5, Swords x2,
  Fiery Justice x1. **Every one `colour-short`, zero `total-short`.**
* **d3, 100 games:** 3 real drops / 381 attempts, plus 3,566 / 342,296 inside rollouts.

The search largely absorbs it: across 400 searched-depth games (d3 250 + d5 150) the reviewed order
changed **zero** outcomes, while d0 moved 3. So the damage is concentrated in greedy play and in
rollout leaves — but the rollout count above shows the leaves are scoring lines they could not
realise, which is the fd-diverge failure mode.

One correction to the audit's own wording: it labels `colour-short` drops "order-independent". The
*count* is order-independent — one cast is lost either way. The *victim* is not, and that is what
costs the turn.

## The fix

Add an **exact colour-feasibility test** and apply it as a SOUND post-filter after the existing
optimistic `CanPay`, so it can only ever prune a subset, never admit one:

1. Build the per-source colour sets for the untapped sources (`EffectiveProduces` already returns
   exactly this, and `ProducesForPayment` is the payment-legal variant that handles
   `colored_creature_only` lands).
2. Feasibility = a bipartite matching between sources and the combined cost's COLOURED pips; generic
   pips then absorb any leftover sources. Hall's condition / Kuhn's algorithm is ample at this size
   (<= ~12 sources, <= ~8 coloured pips).
3. Call it where the subset is admitted: `mana_ok` at TurnSolver.cpp ~8197 and its mirror at ~13094.
   Keep `pool.CanPay(combined)` as the cheap pre-filter and only run the matching when it passes AND
   `pool.wild > 0` AND the cost has >= 2 coloured pips — otherwise the flat check is already exact.

Constraints to respect:

* **Soundness direction.** The new test may only reject subsets the old one accepted. Same discipline
  as the SELECTION-EXACT gate documented above `ManaPruneBound` (TurnSolver.cpp ~6768).
* **Lockstep.** Executor and rollout must agree, so it belongs in the shared unit
  (`ManaPayment.{h,cpp}`) beside `AvailableManaPool`, not in one world's copy.
* **Filters.** `SubsetPayableWithFilters` is the existing rescue path for colour conversion; the new
  test must run with the same filter awareness or it will prune legal filter lines. Karoo /
  domain / Reflecting Pool / storage sources all have bespoke crediting in `AddSourceToPool` —
  each needs a colour set that matches what `tap_source` will really produce.
* **Perf.** This sits in the odometer hot path. Gate it as above, and measure with the standard
  worktree-baseline method before adopting.

Expect a broad play change (it prunes subsets that were previously admitted), so: default-off lever
first (suggested `MTG_COLOUR_EXACT`), measure smoke -> regression -> overnight, and a GT rebaseline
on adoption. Adoption is the USER's call.

## Related

* `MTG_AFFORD_AUDIT` — the instrument that already reports this (`colour-short` column).
* `docs/design/dragonstorm-plan-execution-fidelity-bug.md` — the storage-land analogue: planner
  promised mana the executor could not deliver, silently dropping a legal cast.
* `ManaPruneBound` / the SELECTION-EXACT gate (TurnSolver.cpp ~6737-6800) — prior art for adding a
  sound tightening next to a loose bound.
* `docs/design/cast-order-ideal-with-ranges.md` — the Anti-Lifegain review this surfaced from.
