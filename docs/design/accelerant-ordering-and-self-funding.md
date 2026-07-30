# Same-turn accelerants: one ordering rule, one credit model, one detector

**Status: ADOPTED (2026-07-30).** Three changes, all deck-agnostic and driven off card parameters
rather than per-archetype code:

1. the accelerant **cast-order tiers** moved from two hand-written providers to the root;
2. `consider()`'s ritual credit is now **sequenced** at the leaf, so a ritual can no longer fund its
   own cost;
3. a standing **stranded-accelerant detector** so this defect class is findable on any deck.

Prior context: `docs/design/plan-odometer-factorization.md` ("the cast ORDER was dropping
accelerants") fixed the *execution* half of this on Dragonstorm. This doc covers the generalisation
and the *enumeration* half.

---

## 1. The re-implementation burden (why this was a per-deck cost)

`DecisionProvider::CastOrderRank` orders a turn's casts. The root (`GenericProvider`) knew about
mana rocks (5), creatures (10), noncreatures (20) and on-cast self-damage (30) — but **not about
rituals**. So every ritual deck had to rediscover the same three tiers in its own provider:

| tier | test | why |
|---|---|---|
| 18 | `params.max_casts_after >= 0` | a cast-RESTRICTING ritual (Irencrag Feat, "you can cast only one more spell this turn") must be the LAST ritual, so the only spell after it is the payoff |
| 16 | `!params.reduces_spell_color.empty()` | a cost REDUCER (Ruby Medallion) after the rituals that fund it, before the restrictor — cast as the one spell allowed *after* Irencrag it discounts nothing and wastes the slot |
| 15 | `IsManaRitual(def)` | a ritual must resolve BEFORE the payoff so its float can pay for it |

`HinataProvider::CastOrderRank` and `DragonstormProvider::CastOrderRank` were near-identical
transcriptions of this, down to the comments. Worse than the duplication: a deck **without** a
provider gets *no* ritual tier, so its rituals rank 20 — **tied with the payoff** — and the canonical
order may cast the payoff first and strand them. `decks/Unpredictable Cyclone` holds Seething Song on
the generic provider today and has exactly that hole.

All three tests are pure card-parameter predicates with no archetype knowledge, so they now live in
`GenericProvider::CastOrderRank`, checked first (same precedence the derived providers had). Both
overrides are deleted. **Byte-identical**: smoke 24/24, regression 40/40, 0 configs changed.

The within-tier rule (`DecisionProvider` Hook 30, cheapest-first among same-tier accelerants) is now
the root default too, with `MTG_LEGACY_CAST_TIER_ORDER=1` as the global hatch. The earlier note that
this "regressed slivers/Hinata/Anti-Lifegain/Knights at the ROOT" predates the `IsManaRitual`
restriction in `CastOrderLess` and was really measuring the tie-break reordering *creatures*;
restricted to accelerants it is inert for any deck that never holds two at once.

## 2. A ritual could fund its own cost

`consider()` / `eval_and_push` build the credited pool as

```
eff.wild += Σ_selected ritual_float  (+ the Rite-of-Flame triangular term)
mana_ok   = eff.CanPay(combined)      // `combined` already contains the rituals' own costs
```

That asks only whether the subset is affordable **simultaneously** — `pool + Σfloat >= Σcost` — with
no check that any ritual is castable *in sequence*. So **a ritual pays for itself**: Irencrag Feat
(`{1}{R}{R}{R}`, floats 6) looks castable off a single land, and Hinata plans a **turn-one Irencrag**
that the executor must silently drop.

The mana-**rock** branch a dozen lines below already has precisely this guard —
`if (sel_rock && pool.CanPay(rock_costs))`, commented *"a rock never funds its own cost"*. This was an
inconsistency inside one function, not a deliberate asymmetry.

`SequencedRitualCredit()` credits an accelerant only once the board plus the floats of the
accelerants **before** it can pay for it.

- **Order = cheapest-first by the action's own cost.** That is what a self-funding chain needs and
  what the executor does (`CastOrderLess`'s within-tier rule), so the enumeration's feasibility model
  and the executor's cast order are the same sequence.
- **Ordering by `CastOrderRank` instead is WRONG** and cost a measured Dragonstorm regression. A Lotus
  Bloom `SacForMana` carries `ritual_float` with cost `{0}` (tap + sacrifice), so it is always
  reachable and bootstraps everything else — but it ranks as a plain noncreature (20), behind Seething
  Song (15). The rank-ordered version tested Seething Song against a lone Mountain, gave up, and threw
  away 6 free mana: `dragonstorm d3 gi129`'s T4 win (sac both Blooms → 7 → Seething Song → 9 →
  Dragonstorm) became a T6.
- **Runs to a fixpoint**, not "stop at the first unaffordable one": `CanPay` is colour-aware, so a
  cheap accelerant needing an absent colour must not discard the dearer ones behind it.

### Leaf-only, and why

`MTG_RITUAL_SEQ_CREDIT`: `0` = off (byte-identical hatch), **`1` = leaf only (default)**, `2` = both.

Optimistic enumeration is *safe inside the search*, which discards unpayable lines by rolling them
out — but at depth 0 there is no search to filter them. Mode 1 fixes exactly the site with no safety
net (`Solve::consider`) and leaves `EnumeratePlans`' branch list wide. (The same-turn reducer credit
is deliberately `EnumeratePlans`-only for the same reason, in the opposite direction.)

Held-out overnight (4 seeds × 3 depths × 2 decks + 6 unaffected decks):

| | NET | better / worse | searched slower / faster |
|---|---|---|---|
| mode 1 (leaf) | **−0.1816** | **12 / 1** | **4 / 13** |
| mode 2 (both) | −0.2149 | 15 / 7 | 20 / 37 |

Mode 2 wins on aggregate and loses on the bar that matters — it puts seven cases in the red including
*every* Dragonstorm searched depth. Mode 1 leaves Dragonstorm strictly better with none.

### The invariant this broke

A surplus-ritual check downstream recomputed `full_credit = flat_sum + gy*(gy-1)/2` and commented
`// == the wild credited into eff`. Under the sequenced model that is no longer true, and assuming it
made the check over-subtract and wrongly call load-bearing rituals surplus. It now uses the credit
that was really folded in, and asks `SequencedRitualCredit(..., exclude=r)` for the without-`r`
figure. Guarded by `seq_bit` — where the sequenced model withheld nothing, the closed form is exact
and those subsets keep the pre-change path (and its cost) untouched.

### Cost

The walk is applied **lazily**: the sequenced credit is never *larger* than the simultaneous one, so a
position the cheap model already rejects would be rejected by the sequenced model too — only the
survivors need the walk. Most odometer positions are rejected.

Measured (callgrind Ir; this box has no PMU and wall clock drifts ~3%):

| | naive | + lazy | + `seq_bit` |
|---|---|---|---|
| Dragonstorm d5 | +3.2 % | +1.5 % | **+1.8 %** |
| Hinata2 d5 | +0.07 % | +0.2 % | **+0.15 %** |

The other six suite decks hold no ritual card, so `any_ritual` is false and they pay nothing.

## 3. The detector (so this is not rediscovered by accident)

`MTG_AFFORD_AUDIT=1` now instruments the **real executor's** dropped casts, not just the retrace path,
and reports them by card:

```
AFFORD_AUDIT  real drops: STRANDED accelerants=698  other=926
AFFORD_AUDIT    Irencrag Feat    total-short=548  colour-short=22   <-- accelerant
```

Two distinctions carry all the diagnostic value:

- **accelerant vs not.** A dropped cast is normally benign — enumeration is deliberately optimistic
  and a suite run drops thousands. A dropped *ritual or rock* is not: the plan committed to it
  precisely to fund a later spell.
- **total-short vs colour-short.** Colour-short means enough total mana was available and the wrong
  colours — ordering cannot help, that is the flat `wild`-pool approximation
  (`exact-mana-enumeration.md`). Total-short is the class a cast order can strand or save.

Baseline over 400 d0 games per deck, before the fix: six of eight suite decks report **zero**
stranded accelerants; Hinata 698 (548 of them total-short Irencrag Feat) and Dragonstorm 133. After:
Hinata 56, Dragonstorm 76. One env var replaced what previously took three wrong theories to
diagnose.

## Dead ends and corrections

- **"HinataProvider has the same defect as Dragonstorm."** It does not. The cheapest-first tie-break
  is inert for Hinata — its tier-15 accelerants are Reality Spasm copies whose post-discount costs are
  equal, and Irencrag sits alone in tier 18. Hinata's problem was the self-funding credit, a different
  bug that the detector found in one command.
- **Ordering the sequenced walk by `CastOrderRank`** — see above, discards free `SacForMana` float.
- **A fast path for "the board pays for every accelerant outright"** measured ~0 %: on the go-off
  turns that dominate the hot path the board *cannot*, which is the whole point. Superseded by the
  lazy application, which does work.
