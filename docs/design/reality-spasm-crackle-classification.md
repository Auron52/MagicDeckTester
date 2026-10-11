# Reality Spasm was classified as a Crackle (`IsCrackleCountSpell`) — 2026-10-10

Lever: `MTG_CRACKLE_COUNT_EXACT` (heurarm slot `CRACKLE_COUNT_EXACT`), **default ON**; `=0` restores
the legacy predicate and is byte-identical to `ccacda4e`.

## The defect

`IsCrackleCountSpell` (`src/core/SpellEffects.h`) gates the Crackle declared-target-count model
(`crackle_targets` → derived Hinata discount + faithful 5X damage to the extras). It was

```cpp
return p.discount_targets_scale_x && p.x_damage_multiplier > 0;
```

and its comment said it "deliberately EXCLUDES Reality Spasm". It never did: `CardDatabase.cpp`
defaults `x_damage_multiplier` to **1** for every card, and Reality Spasm's params are
`{untap_x_mana_sources, discount_targets_scale_x, discount_targets_permanents}`, so RS satisfied both
conjuncts.

## Rules reading

Card texts (cards.json, which matches the printed cards; the Scryfall API answered 403 from this box):

* **Hinata, Dawn-Crowned** {1}{U}{R}{W}: "Spells you cast cost {1} less to cast for each target."
* **Reality Spasm** {X}{U}{U} instant: "Choose one — Tap X target permanents; or Untap X target permanents."
* **Crackle with Power** {X}{X}{X}{R}{R} sorcery: "deals five times X damage to each of **up to** X targets."

RS has **exactly** X targets (not "up to"), so it can only be cast with X legal targets (CR 601.2c),
and Hinata's reduction is then exactly X off the generic part (CR 601.2f): RS costs {U}{U} at any X.
The engine's auto-max path, `HinataGenericDiscount(RS, s, X)` = `min(X, avail)`, gives exactly that,
because the engine's X is the own mana-source count, which never exceeds the permanents on the
battlefield. (`HinataAvailableTargets` also adds the opposing face(s), which RS cannot target. That
over-count cannot bind while X ≤ own permanents.) Crackle's "up to X" makes its target count a real
choice, so the derived `min(X, 1 + declared)` is right for Crackle and wrong for RS.

## Site-by-site consequences (before → after)

| Site | What it gates | Reachable with RS? | Before | After |
|---|---|---|---|---|
| `DecisionProviders.cpp` `HinataProvider::XCandidates` copy count | "a second Crackle in hand frees a chip cast" | **yes** (RS in hand, incl. one staged by Soulfire) | RS counted as copy #2 → the hold-a-lone-Crackle rule was **off** whenever RS was in hand | RS is not a copy → the lone non-lethal Crackle is held; a combo-turn kill still passes (RS's net mana is already in `max_affordable`) |
| `DecisionProviders.cpp` `XCandidates` entry gate | applies the hold to `def` | no: RS is emitted by its own untap branch and never asks `XCandidates` | — | — |
| `TurnSolver.cpp` `crackle_branch` (CollectActions {X}) | the declared-count axis | no: RS's `untap_x_mana_sources` branch emits it and `continue`s, and its `custom` template is also excluded by the `!= DirectDamage → continue` guard | RS never got a derived discount | unchanged |
| `TurnSolver.cpp` 6159 / 7132 / 37321, `AIEngine.cpp` 7286 (cost sites) | `? crackle_targets : -1` | RS's `crackle_targets` is always −1 (no path sets it) | auto-max = X → {U}{U} | identical |
| `AIEngine.cpp` 7114 (stack entry) | carries `crackle_targets` | RS gets `optional(-1)` → now `nullopt` | read only by DirectDamage / damage_divided resolvers | inert |
| `EffectHandler.cpp` 662, `TurnSolver.cpp` 37889 (extra-target damage) | `CrackleHitExtraTargets` | no: both sit inside DirectDamage resolution | — | — |
| `TurnSolver.cpp` 66875 (viewer `crackle` sub) | requires `crackle_targets >= 0` | no | — | — |
| `main.cpp` 5050 (human target dialog) | Crackle target list | no: the chooser is only invoked from DirectDamage / Removal / Pump / Soulfire resolutions | — | — |

So exactly one site moved play: the hold rule's copy count. The latent hazard is pinned by a unit test.
Had any path ever carried a declared count onto an RS cast, the legacy predicate would have priced
"face + extras" (`min(3, 1)` = {1} off at X = 3) instead of X.

## The fix

`IsCrackleCountSpell(const CardDefinition&)` keys on the **template**. It requires `scale_x` **and**
`tmpl == DirectDamage`. Every caller already had the definition. Why the template:

* the count model is implemented ONLY along DirectDamage paths: the declared-count enumeration
  (TurnSolver's {X} DirectDamage block) and both resolvers. A predicate keyed on the template cannot
  disagree with the machinery it gates;
* `!untap_x_mana_sources` names one mode of one card. A modelled RS tap mode would reopen the hole;
* a "multiplier was SET" bit would need a new CardParams field and would wrongly drop a plain
  "X damage to each of up to X targets" spell that leaves the multiplier at its default 1.

Tests: `test/unit/test_hinata_crackle_count.cpp`. It covers the predicate on Crackle / RS / Rolling
Earthquake / a synthetic X burn, under both arms; XCandidates holding a lone Crackle beside an RS
(legacy arm: offered); and RS's {U}{U} with Hinata, including the legacy-arm hazard. The full unit
suite passes: 926 / 926.

## Measurement (sanity, not a verdict)

One pooled `mtg --batch` run, lever on vs off, paired by gi, fresh seeds (base 7900001 / 7910001 /
7920001 / 7930001), Release `mtg` built from this commit, unwon = 9:

| cell | n | turns on | turns off | Δ/game | t | faster | slower | search units on/off |
|---|---|---|---|---|---|---|---|---|
| d0 | 400 | 2743 | 2816 | −0.183 | −5.66 | 50 | 8 | n/a (d0 charges none) |
| d3 b10 | 150 | 841 | 845 | −0.027 | −1.64 | 5 | 1 | 0.983 |
| shipped (value_play depth, b20) | 100 | 572 | 576 | −0.040 | −0.94 | 2 | 1 | 0.926 |
| 2HG d0 (life 30, 2 heads) | 100 | 668 | 682 | −0.140 | −2.46 | 10 | 1 | n/a |

No other deck holds a `discount_targets_scale_x` card. The predicate short-circuits on that param, so
every other deck is byte-identical by construction. This was confirmed on burn, dragonstorm and stompy
(d0 50 + d3 b10 20 each, all 6 digests equal to `ccacda4e`). `=0` against the `ccacda4e` binary:
30 jobs / 750 Hinata games, 0 digest mismatches.

### The five rc3 games recover

These are the ab3 round's Hinata d0 slower games (new arm slower than old, all bisected to
`MTG_PAYABLE_ORDER_RITUALS`): seeds/gi 1330763/762, 1330874/873, 1331155/1154, 1331326/1325 and
1331550/1549. All five recover to the old arm's turns (5, 6, 5, 5, 6). With the fix, old and new arms
produce **byte-identical** games. The PAYABLE_ORDER interaction existed only because of the
misclassification. Mechanism: the chip Crackle was offered only while an RS sat in hand (in four of
the five, an RS staged by Soulfire Eruption). Whether the chip actually went out depended on cast
order and re-solve timing. In gi 1154's old arm, RS resolved first, and the post-RS re-solve saw a
lone Crackle and held it. Under `PAYABLE_ORDER_RITUALS` the chip Crackle (X = 1 or 2) went out right
after the RS, in the same committed sequence, spending the payoff. The fixed arm holds the Crackle
from the start and kills with RS + a big Crackle on the old arm's turn (gi 1154: T5 RS X=7 +
Crackle X=6).

### Every slower game, by mechanism

All 11 reproduce single-game; logs are under `logs/rs_crackle/LS/` in the worktree.

1. **No Hinata on the battlefield, so the combo cannot happen, and the hold blocks the chip clock.**
   Covers d0 gi 8, 212, 324, ship gi 36, and 2HG gi 90 (Hinata in hand but never cast). The pre-fix
   arm chipped a Crackle the turn an RS was drawn, because the RS counted as copy #2. The hold rule
   never consults whether Hinata is in play or castable. Without her, RS nets 0 mana and the combo
   the hold protects does not exist. This limit predates the fix; the fix only extends it to RS-in-hand turns.
2. **Hinata in play, the combo never reaches lethal in time, and the chip was the faster clock.**
   Covers d0 gi 34, 234 and 260: RS + a chip Crackle (X = 2–3), then Soulfire finishes the next
   turn. In gi 260 the d0 runner's fallback after the hold is a Soulfire whose own-target count takes
   Hinata for the {1} discount. Soulfire's positional flip assigns her a 9-MV card, so she dies and the
   engine is offline for three turns. That is the permitted depth-0 greedy; it cannot price the loss
   past the turn.
3. **The hold's lethal gate counts only the Crackle and the attack.** Covers d0 gi 157 and 196. The
   pre-fix kill was RS (+RS) + Crackle X = 3 + **Magma Opus** + Hinata's attack = exactly 20. The gate
   `max_x * 5 * face_hits + attack_ub >= opp_life` reads 15 + 4 = 19 and holds, so the co-cast burn
   that makes the turn lethal (Magma Opus face damage, Soulfire's flip) is invisible to it.
4. **d3 gi 69: the lookahead moved.** The first divergence is T2, Ponder-first vs Preordain-first. The
   rollouts that score that near-tie now hold a lone Crackle beside the RS drawn later. The two
   cantrip orders see different cards, and only the off arm had a real second Crackle in hand by T5
   for a legitimate chip.

## The hold rule's two follow-ups (implemented on top of the fix)

Both levers live in `HinataProvider` and share one helper, `HinataLoneCrackleHeld`. That helper is the
whole hold rule (enabled, Crackle, autonomous, fewer than two copies, and the B scope gate) **except**
the "wins THIS turn" exemption. So the arms differ only in how lethality is judged and in scope.

### A. `MTG_HINATA_HOLD_LETHAL_BURN` (heurarm `HINATA_HOLD_LETHAL_BURN`) — correctness, DEFAULT ON

The hold's lethal test was `max_x * 5 * face_hits + attack_ub >= opp_life`, an estimate that counts
the Crackle and every creature's power and nothing else. With the lever on, `XCandidates` offers the X,
and a new provider hook, `DecisionProvider::HoldCastUnlessLethal`, flags the lone Crackle. Both subset
walks (`Solve`'s consider and `EnumeratePlans`' consider, the only two consumers of `CollectActions`)
then drop every subset that casts a flagged Crackle and does not win by **their own** projection.
That projection is `projected_atk + direct_dmg >= opp life`, on a subset that already passed
payability. This is the `HoldSacLandBurn` / Irencrag-waste shape: no second estimator; the engine's
damage and payability machinery decides. A co-cast Magma Opus or Soulfire now counts. So do summoning
sickness and creatures tapped for mana, which the estimate over-credited. The base hook is `false`, so
every other deck is byte-identical (verified on burn, dragonstorm and stompy).

* Class-3 games recover: d0 gi 157 and 196 go from 7 to 6, replaying the pre-fix exact-20 lines (RS RS
  Crackle X=3 + Magma Opus; RS + Magma + Crackle X=3).
* Unit test: a board where Crackle X=4 + attack = 24 < 25 but Crackle X=4 + Magma Opus (1 face) +
  attack = 25. With the lever on, Solve and the search both cast the pair; with it off, nothing casts
  the Crackle. At life 26, neither arm casts it.

### B. `MTG_HINATA_HOLD_NEEDS_HINATA` (heurarm `HINATA_HOLD_NEEDS_HINATA`) — scope, DEFAULT OFF (USER decides)

Hold only while the combo is reachable: Hinata in play or castable from hand this turn
(`HinataCastableFromHand`, the same optimistic source-count gate as the same-subset credit).

* Class-1 games under B: d0 gi 8 (9→8), 212 (9→6), 324 (8→7) and shipped gi 36 (9→8) recover.
  2HG gi 90 does not (9). Hinata sits in hand from T2 and is never cast. On T3–T4 the board's three
  sources make 5 mana but read as "not castable", so B lifts the hold and the runner stalls: the same
  d0-runner defect as the B4 games below. On T5–T6 it chips with two Crackles, and Gamble bins her.
* **Held-out, B measures WORSE** (table below): the hold-without-Hinata is worth having on average.
  Recommendation: keep it OFF.

### Held-out measurement (one pooled batch, seeds 8100001+, three arms, paired by gi, unwon = 9)

Arms: `base` = shipped defaults (A on, B off); `aoff` = A off, which is the 2cec6ec4 behaviour;
`bon` = B on. Lever A = base vs aoff; lever B = bon vs base.

| lever | cell | n | Δ turns (on − off) | Δ/game | t | faster | slower | search units on/off |
|---|---|---|---|---|---|---|---|---|
| A | shipped b20 | 400 | 0 | 0 | — | 0 | 0 | 1.0058 |
| A | d3 b10 | 400 | +1 | +0.0025 | 1.00 | 0 | 1 | 1.0007 |
| A | 2HG d0 | 400 | 0 | 0 | — | 0 | 0 | n/a |
| A | d0 | 3000 | −2 | −0.0007 | −1.00 | 3 | 1 | n/a |
| B | shipped b20 | 400 | +3 | +0.0075 | 0.56 | 8 | 10 | 1.038 |
| B | d3 b10 | 400 | −5 | −0.0125 | −2.25 | 5 | 0 | 1.051 |
| B | 2HG d0 | 400 | +32 | +0.0800 | 3.63 | 2 | 20 | n/a |
| B | d0 | 3000 | +161 | +0.0537 | 8.75 | 15 | 117 | n/a |

Lever A is net −1 turn over 4200 games for +0.6% / +0.07% search units, a correctness change that
rarely binds. Byte-identity: with both levers `=0` (env path) against the 2cec6ec4 binary, 36 jobs and
1600 Hinata games (d0 1000, 2HG 400, d3 100, shipped 100) gave 0 digest mismatches.

**Lever A's two slower games:**

* d0 gi 1683 (6 → 7). On T5 the off arm's estimate took max X *with* the ritual credit, and every
  creature's power, and called Crackle "lethal". The real line was RS + Crackle X=3 = 15 + 4 = 19,
  leaving the opponent at 1, and Soulfire finished on T6. The walks' projection correctly calls that
  subset non-lethal, so the hold applies. The game wins T7 with RS RS Crackle X=10. The chip clock was
  faster here; that is the hold's own trade-off, no longer masked by an over-optimistic estimate.
* d3 gi 25 (7 → 8). The first divergence is T2: Ponder now (off) vs hold the cantrips to T3 (on). It is
  a near-tie, scored by rollouts whose futures now hold a non-lethal lone Crackle exactly. After that,
  the two arms draw different cards.

**Lever B's 147 slower games** (every one reproduces single-game; per-game list in the worktree's
`logs/rs_crackle/classifyB.out`, made by `classifyB.py`):

* **78 (B1):** with Hinata neither in play nor castable, B lets the d0 runner spend the lone Crackle as
  a chip (X = 1–2). The base arm holds it and later fires that same Crackle **with Hinata in play** as
  the combo finisher. (d0 68, 2HG 10.)
* **54 (B2):** the chip is spent; the base arm fires the held Crackle later, still without Hinata, at a
  bigger X (3–5) or beside a second copy. Holding converts an X=1 chip into an X=3–5 payload.
  (d0 45, 2HG 9.)
* **1 (B3):** d0 gi 2827. The chip is spent and the base arm never needs the card.
* **10 (B4, shipped):** the first divergence is T1–T3 cantrip timing. The searched ranking of early
  cantrips moves because rollout futures now chip-cast Crackle without Hinata.
* **4 (B4, d0/2HG):** 2HG gi 254 and d0 gi 195, 865 and 1773 expose a **pre-existing d0-runner defect**
  the hold had been masking. With the lone Crackle offered, the runner either casts nothing for turns
  (2HG gi 254: Hinata castable from Sol Ring + Izzet Boilerworks + Forbidden Orchard, nothing cast
  T3–T6; it reproduces with the hold disabled entirely, `MTG_NO_HINATA_HOLD_CRACKLE=1`), or casts
  Irencrag Feat and spends its one remaining cast on an Ornithopter instead of the Crackle (gi 865,
  1773). Root-caused and fixed below ("The d0-runner stall"). Also, `HinataCastableFromHand` counted
  SOURCES, not mana, so Sol Ring and a bounce land under-counted it (gi 254: 3 sources, 5 mana).

## The d0-runner stall (2HG gi 254; d0 gi 195, 865, 1773) — root cause and fix

These four games stalled only once the lone Crackle was offered (lever B, or the hold disabled
entirely with `MTG_NO_HINATA_HOLD_CRACKLE=1`). The hold had been masking two modelling defects. The
executor's cast traces (`MTG_USER_ORDER_TRACE=1`) show both.

1. **Irencrag Feat's own cost "paid" by its own float (2HG gi 254, d0 gi 195).** The d0 runner commits
   {Irencrag Feat, Crackle} turn after turn. On T3–T6 of gi 254 the trace reads Irencrag `paid=0`, then
   Crackle `paid=0`, so nothing is cast. The board is Sol Ring + Izzet Boilerworks + Forbidden Orchard:
   five mana, but only two can be red. Both subset walks credit a ritual's float to the whole subset,
   its own cost included, in the flat test and in the colour-exact gate (`PoolCredit(pool, eff)`). The
   single-accelerant check in `SequencedRitualCredit` tests the flat pool, where a bounce land is two
   wild. So {R}{R}{R} "paid" from Irencrag's own seven red.
2. **The restrictor counted in rank order, the subset payable only in another order (d0 gi 865,
   1773).** On four mana the runner commits {Irencrag, Ornithopter of Paradise, Crackle}. The legality
   test counts the casts after Irencrag by `CastOrderRank`. Ornithopter ranks before him, so it counts
   one follower and calls the plan legal. But Ornithopter can only be paid from Irencrag's float. The
   executor's payable-order fallback therefore casts Irencrag first, the Ornithopter takes his one
   remaining cast, and the Crackle is refused (`paid=0`).

Neither is a USER-owned decision. They are payability and legality modelling, not cast order or policy.

### `MTG_RESTRICTOR_ORDER_EXACT` (heurarm `RESTRICTOR_ORDER_EXACT`), DEFAULT ON (=0 → the rank-only count)

`RestrictorPrePayable` (TurnSolver.cpp) runs inside both walks' Irencrag loop, right after the
existing `after > max_casts_after` test. It is a **reject-only** test with two questions:

1. **Pre-set.** Can the casts ranked at or before the restrictor, plus the restrictor itself, be paid
   **without the restrictor's own float**? If not, every payable order casts the restrictor ahead of
   one of them, so one more cast follows it than the rank count says. The subset is rejected when
   that extra cast breaks the cap (`after + 1 > max_casts_after`).
2. **Self.** Can the restrictor alone be paid from the board plus every OTHER producer in the subset,
   in any order? If not, no order casts it, and the subset is rejected whatever the casts-after count.
   This catches a lethal-projected phantom too: Irencrag Feat + Magma Opus on the gi 254 board, where
   the Opus ranks before Irencrag.

`after` itself is not changed. The Irencrag waste gate (USER 2026-09-01) keeps judging the rank order it
was ruled on, and no subset it drops is readmitted. An earlier draft incremented `after`, which could
flip that gate. On the held-out sample both drafts were byte-identical, but the reject-only form is the
one that cannot readmit a line. Neither question can reject a payable line, because both sides err
generous:

* **Bill:** the subset's bill with every subset-level credit already folded in, minus the cost of
  every action outside the pre-set.
* **Supply:** the board plus the mana of every pre-ranked or pre-pass producer (other rituals, rocks,
  sac-for-mana), regardless of order.
* **Colours:** the walks' own colour-exact gate (`ColorFeasibility`), run on the pre-set alone. It
  knows a bounce land makes one of each colour.

It is scoped like the Irencrag waste gate: not for decks that search their cast order (Dragonstorm,
`OrderingSearchEnabled`). Anything it cannot model (hybrid, twobrid or Phyrexian pips) is left
unchanged.

### `MTG_HINATA_CASTABLE_MANA` (heurarm `HINATA_CASTABLE_MANA`), DEFAULT ON (=0 → the source count)

`HinataCastableFromHand` asked whether the SOURCE COUNT reached her mana value. That under-counts every
multi-mana source and ignores colours: gi 254's three sources make five mana and pay {1}{U}{R}{W}, yet
read "not castable". It now asks whether `AvailableManaPool` (the accounting pool every payment gate
uses) pays her printed cost with the pool's own colour-aware `CanPay`. The check feeds three things:
Reality Spasm's emission under the same-subset credit, Crackle's X sizing when she is castable this
turn, and lever B's scope gate.

### Recovery (single games, turns / failed casts in the executor trace)

| game | condition | 3d64d9a6 | both levers on | RESTRICTOR off | CASTABLE_MANA off |
|---|---|---|---|---|---|
| 2HG gi 254 | shipped hold | 4 / 1 | 4 / 1 | 4 / 1 | 4 / 1 |
| 2HG gi 254 | lever B on | 9 / 17 | 4 / 1 | 4 / 1 | 4 / 1 |
| 2HG gi 254 | hold disabled | 9 / 17 | 4 / 1 | 9 / 17 | 4 / 1 |
| d0 gi 195 | shipped hold | 7 / 0 | 7 / 0 | 7 / 0 | 7 / 0 |
| d0 gi 195 | lever B on | 8 / 5 | 6 / 0 | 8 / 5 | 6 / 0 |
| d0 gi 195 | hold disabled | 8 / 5 | 6 / 0 | 8 / 5 | 6 / 0 |
| d0 gi 865 | shipped hold | 6 / 0 | 6 / 0 | 6 / 0 | 6 / 0 |
| d0 gi 865 | lever B on / hold disabled | 9 / 1 | 9 / 0 | 9 / 1 | 9 / 0 |
| d0 gi 1773 | shipped hold | 6 / 0 | 6 / 0 | 6 / 0 | 6 / 0 |
| d0 gi 1773 | lever B on / hold disabled | 9 / 1 | 9 / 0 | 9 / 1 | 9 / 0 |

(The one failed cast that remains in 2HG gi 254 under every arm is T1 "Sol Ring, Ponder" off a single
land: Ponder's {U} is "paid" by Sol Ring's own {C}{C} -- the same-turn ROCK form of the producer-pays-
itself defect, not touched here; see "Still open".)

* **2HG gi 254:** either lever alone removes the stall. With the hold disabled, `RESTRICTOR_ORDER_EXACT`
  stops the unrealisable {Irencrag, Crackle} line. Under lever B, `HINATA_CASTABLE_MANA` reads her as
  castable, so the hold stays on and that line never forms.
* **d0 gi 195:** `RESTRICTOR_ORDER_EXACT` recovers it (8 → 6, five failed casts → 0).
* **d0 gi 865, 1773:** the failed Crackle cast is gone (Irencrag now funds the Crackle it was cast
  for). The games stay unwon under lever B and with the hold off; that slowness is lever B's own chip
  mechanism (above). With the shipped hold, all arms win on T6.

### Held-out (seeds 8300001+, one pooled batch, paired by gi, unwon = 9)

One pooled batch (`mtg --batch`, 6 threads, `lowprio.sh`), arms paired by gi. Cells: shipped =
profile depth, budget 20 ms; d3 b10; 2HG d0 (life 30, two heads); d0. **The shipped and d3 cells are
budget-ms cells measured under contention from another container**; their per-job digests reproduced
across two separate runs (and single-game repros), so the comparison is paired, but treat the wall
numbers as contended.

**`MTG_RESTRICTOR_ORDER_EXACT` on vs off** (both other levers at defaults):

| cell | n | turns on | turns off | Δ | faster | slower | search units on/off |
|---|---|---|---|---|---|---|---|
| shipped b20 (contended) | 200 | 1129 | 1129 | 0 | 1 | 1 | 1.013 |
| d3 b10 (contended) | 200 | 1116 | 1116 | 0 | 0 | 0 | 0.997 |
| 2HG d0 | 400 | 2643 | 2645 | −2 | 2 | 0 | n/a |
| d0 | 2000 | 13499 | 13502 | −3 | 3 | 0 | n/a |
| **Hinata2 net** | 2800 | | | **−5** | **6** | **1** | |

* Faster: every depth-0 one (2HG gi 13, 257; d0 gi 286, 1487, 1703) is a game whose off arm commits an
  Irencrag line the executor cannot realise. Failed casts in the trace are Soulfire Eruption after the
  cap, or d0 gi 1487's Irencrag + Soulfire committed and failed **24 times** across T4–T6. The on arm
  has none. Shipped gi 4 (6 → 5) is the same class under search: the off arm commits {Irencrag,
  Crackle} on T3, and both fail at payment in main 1 and again in main 2.
* **The one slower game, shipped gi 68 (5 → 6), reproduced twice.** The first divergence is the T3 land
  drop (Island vs Mountain), chosen by the depth-5 search. The lever rejected unrealisable
  restrictor subsets in that search's futures, and the two arms then walk different boards. On T5 the
  on arm commits {Irencrag Feat, Soulfire Eruption}. It is rank-legal (Soulfire ranks before Irencrag),
  but Soulfire is payable only from Irencrag's float. So Irencrag is cast first, Soulfire takes the
  one remaining cast, and the cards Soulfire staged (Sol Ring, Preordain) are refused (`paid=0`). The
  off arm, with Island kept, wins T5 through Soulfire → Reality Spasm ×2 → … → Irencrag → Crackle. The
  lever did not reject either T5 line; the stranded continuation is a residual of the same rank-vs-
  realised mismatch, in its after = 0 form (see "Still open").

**`MTG_HINATA_CASTABLE_MANA` on vs off:** **byte-identical in shipped play**. Every job digest matched
(2HG d0 400, d0 2000; 0 turn changes). Its consumers sit behind two default-OFF levers:
`MTG_HINATA_SUBSET_CREDIT` (Reality Spasm emission and Crackle X sizing) and lever B. The controls show
it functions where it is consumed:

* Under `MTG_HINATA_SUBSET_CREDIT=1` at d3 b10 (150 games, the search, where that credit lives), 2 of 6
  job digests move, with 0 turn changes and units 0.984. At d0 there is no change: the d0 runner never
  applies the credit.
* Under lever B (d0, 1000 games): +1 turn (1 faster, 2 slower). Both slower games (gi 322: 4 → 5;
  gi 779: 8 → 9) are boards where Hinata really is not castable this turn. For example, gi 322 T2 has
  Ornithopter as the only {U}/{W} source and needs both, while the source count said "castable". So
  lever B releases the lone Crackle as a chip a turn before she lands. That is lever B's own scope
  trade-off (default OFF, the USER's call), now judged on accurate castability.

Byte-identity: with both levers `=0`, against the 3d64d9a6 binary, Hinata (32 jobs, 1500 games: d0
1000, 2HG 400, d3 50, shipped 50) gave 0 digest mismatches. With both levers ON, against 3d64d9a6,
burn / dragonstorm / stompy (d0 100 + d3 b10 30 each) gave 0 mismatches. Unit suite: 932 / 932.

## Still open (the USER's calls; not touched)

* **Lever B** — measured above; the recommendation is to keep it OFF.
* **Staged copies.** `HinataLoneCrackleHeld` counts a Soulfire-staged Crackle as a copy, but a staged
  card is castable only until the end of the next turn. Whether that is the intended "the other stays
  for the combo" is the USER's call.
* Hinata's committed **value leaf and keep table** were fitted to play with the misclassification.
  This fix changes Hinata play (−0.18 turns/game at d0, −0.03 at d3, −0.04 shipped), so both artifacts
  are aged. Nothing was regenerated. Hinata's regression GT keys will move when the tiers are next
  re-run.

## Deferred engine follow-ups (found while fixing the stall; not done)

* **The same-turn ROCK form of "a producer pays for itself".** On T1, "Sol Ring, Ponder/Preordain" off
  one land is accepted by the subset walks. The rock credit admits Sol Ring's {C}{C} once the board can
  pay Sol Ring, and the colour-exact gate is order-free, so Sol Ring's own mana "pays" the cantrip's
  generic while the land "pays" its {U}. In reality the land pays Sol Ring and {C}{C} cannot make {U},
  so the cantrip fails at payment. Seen as the single failed cast in 2HG gi 254 and shipped gi 4,
  under every arm. The fix would be the sequenced test `RestrictorPrePayable` already applies to
  Irencrag: a same-turn producer's own cost must come from the board, before its output is credited.
* **The after = 0 form of the restrictor mismatch.** {Soulfire Eruption, Irencrag Feat} is rank-legal,
  with Soulfire ranked before Irencrag and so not counted as a follower. When Soulfire is payable only
  from Irencrag's float, the realised order is Irencrag → Soulfire. Soulfire then takes the one
  remaining cast, and the cards it stages cannot be cast that turn (shipped gi 68, T5: Sol Ring and
  Preordain `paid=0`). This is legal and realisable, but the restriction strands Soulfire's
  continuation. Whether the search's continuation scoring honours the cap there needs its own trace.
