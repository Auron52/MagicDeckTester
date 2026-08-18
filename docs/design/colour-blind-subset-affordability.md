# The enumeration admits subsets the board cannot pay: multi-colour sources collapse to `wild`

Self-contained record (2026-08-18). Found while reviewing Anti-Lifegain's cast order; it is NOT an
ordering bug and NOT specific to that deck.

**STATUS: BUILT and measured, behind `MTG_COLOR_EXACT` (default off). Adoption is the USER's call —
see "What was built" at the end for the numbers and the soundness evidence.**

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

## What was built (2026-08-18)

`MTG_COLOR_EXACT`, default off. One new unit in the shared `ManaPayment.{h,cpp}` (so the executor and
the rollout read the same model), called from the two subset enumerators in `TurnSolver.cpp`
immediately after `SubsetPayable`.

* `BuildColorFeasibility(state)` -> `ColorFeasibility{cover[32], usable}`. State-only, so it is built
  ONCE per enumeration exactly like `ComputeAvailableColors`; `cover[S]` is the number of mana units
  that can pay a coloured pip of some colour in the 5-bit set `S`.
* `ColorFeasibility::Payable(cands, sel, credit)` -> Hall's condition over the 31 non-empty colour
  sets. Each coloured pip is a demand carrying the mask of colours that may pay it; a hybrid pip is
  un-baked into its two halves the way `SubsetPayable` already does (without that, `{B/G}` demands
  BLACK and a green board is false-rejected). Generic and `{C}` pips are not modelled at all — any
  unit pays them, and the flat `CanPay` that ran first already checked the total, so Hall on the
  coloured pips is the exact remaining condition.
* `PoolCredit(base, eff)` supplies the subset's same-turn credit (ritual float, rock production,
  haste-dork unlock, domain widening) as extra units with the colours the pool recorded.

Soundness is by over-approximation, never by guessing: a Karoo's two fixed colours become two free
choices, a domain source becomes any colour, credit `wild` pays anything. The one shape no colour set
can express is a CONVERSION source — Cascade Bluffs turns one `{U}` into `{R}{R}` — so a board
holding a filter / ramp-filter / scaled land sets `usable = false` and the whole test stands down,
leaving `SubsetPayableWithFilters` as the only authority there (the same three shapes
`HasUntappedFilterSource` already routes to it).

### The bug is gone

`MTG_AFFORD_AUDIT`, Anti-Lifegain d0, 200 games: **14 drops / 876 attempts -> 0 / 863**. Every
colour-short drop is eliminated and no `total-short` drop appears in its place. On the gi=697 repro
the turn-5 subset now excludes the unpayable second white cast instead of enumerating it and letting
cast order pick the victim.

### Soundness measured, not asserted

`MTG_COLOR_EXACT_PROBE` re-tests every rejection against the real payment path (`TapForCostDirect`
on a copy — the executor's own code) and prints any rejection the board could actually have paid.

| run | rejections | false rejects |
|---|---|---|
| full smoke suite, all decks, d0/d3/d5 (probe=1) | — | **0** |
| Anti-Lifegain d0 x200 (probe=2) | 17 | **0** |
| Anti-Lifegain d3 x40 (probe=2) | 2,608 | **0** |
| FiveColour d0 x200 (probe=2) | 16 | **0** |
| FiveColour d3 x40 (probe=2) | 34,733 | **0** |

The probe has two levels precisely because a gate that never fires is indistinguishable from a gate
that fires cleanly (the lesson the inert cast-order ladder taught). Level 2 prints the agreed
rejections too, which is what makes the zeros above evidence rather than silence.

### Measured effect (suite = the A/B, GT = the lever-off control)

| tier | seeds | net summed delta | play-changed | worse |
|---|---|---|---|---|
| smoke | 1001 | **-0.0800** / 36 keys | 10 | 2 (fivecolour d3 +0.0067, d5 +0.0133) |
| regression | 2002 + 3003 | **-0.1353** / 60 keys | 16 | **0** |
| overnight (HELD OUT) | 4004/5005/6006/7007/8008/10010 | **-0.3699** / 144 keys | 47 | 3, none over +0.0015 |

Negative is better, and all three seed sets are disjoint. Almost all of it is `creature_giving` d0
(-0.34 summed on the held-out tier); per deck on overnight: creature_giving -0.3395, fivecolour
-0.0294, antilife -0.0020, knights -0.0005, hinata +0.0015, every other deck exactly 0. Twenty-six of
36 smoke keys are byte-identical, which is the check that the gate only bites boards that actually
hold the phantom.

**Every searched-depth slowdown recovers with budget** — the test the skill prescribes before calling
one real (re-run BOTH arms at `--budget-ms 0`; a large number is not unlimited, `<= 0` is):

| game | at case budget | 4x | 16x | unlimited |
|---|---|---|---|---|
| fivecolour d3/d5 s1001 gi44 | 5 -> 6 | d3 recovers | d5 recovers | **5 = 5** |
| creature_giving d3 s3003 gi221 (draws also diverge at T4) | 4 -> 5 | recovers | recovers | **4 = 4** |
| antilife d5 s7007 gi194 | 7 -> 8 | — | — | **5 = 5** |
| creature_giving d3 s7007 gi162 | 4 -> 5 | — | — | **4 = 4** |

So no line is lost to the gate; the search simply spends a fixed budget differently. Note the antilife
game is T5 on BOTH arms at unlimited budget, i.e. the recorded T7 baseline was itself budget-limited.

Adopted 2026-08-18: default ON, off-switch `MTG_COLOR_EXACT=0`. The flip was verified play-identical
to the env-driven arm before any ground truth was promoted — smoke (36 keys) and regression (60 keys)
reproduced every fingerprint exactly.

### Coverage is PARTIAL, and the shape of the gap is the point

The gate stands down whenever it cannot model a source exactly, so how much of the phantom it removes
depends entirely on what a deck's mana base is made of. `MTG_AFFORD_AUDIT`, 150 games per cell at
seed 7007, colour-short drops OFF -> ON:

| deck | d0 | d3 | why |
|---|---|---|---|
| creature_giving | 36 -> **0** | 15 -> **0** | plain duals/triomes/fetches — the model is exact |
| antilife | 8 -> **0** | 4 -> **0** | same |
| knights | 0 -> 0 | 0 -> 0 | no phantom in the sample |
| fivecolour | 54 -> 46 | 62 -> 49 | Bloom Tender / Faeburrow Elder are `domain_mana`, opened to all five colours under `MTG_DOMAIN_WIDEN` |
| hinata | 17 -> 17 | 26 -> 26 | **Cascade Bluffs (`is_filter`) + Izzet Signet (`ramp_filter`)** — an untapped conversion source switches the whole test off |

So the entire measured gain comes from the decks whose sources the model covers exactly, and hinata —
which has a real colour-short rate — gets **nothing**. That is by construction, not by accident: a
conversion source is precisely the case where an over-approximation would false-reject, so standing
down is the only sound move available to a per-source colour set.

The follow-up this points at: `SubsetPayableWithFilters` already pays for real, and is already the
authority on filter boards. Running it as the exact test when `usable == false` would close the hinata
gap at the cost of a real payment per surviving subset — a perf question, not a soundness one.

### Not done

* **The non-creature pool.** `eff_nc.CanPay(noncreature_combined)` gets the same flat treatment and
  the same phantom is possible there (it needs a `creature_mana_only` source, e.g. Ancient Ziggurat).
  Only the main pool is gated today.
* **Held-out validation.** `--overnight` (seeds 4004/5005/6006/7007) has not been run.
* Adoption, per standing doctrine, is the USER's call — and adopting means a GT rebaseline across all
  three tiers, since the gate changes play on five decks.

## Related

* `MTG_AFFORD_AUDIT` — the instrument that already reports this (`colour-short` column).
* `docs/design/dragonstorm-plan-execution-fidelity-bug.md` — the storage-land analogue: planner
  promised mana the executor could not deliver, silently dropping a legal cast.
* `ManaPruneBound` / the SELECTION-EXACT gate (TurnSolver.cpp ~6737-6800) — prior art for adding a
  sound tightening next to a loose bound.
* `docs/design/cast-order-ideal-with-ranges.md` — the Anti-Lifegain review this surfaced from.
