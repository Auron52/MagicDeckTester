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

## Follow-ups (deferred, not implemented; each would need its own paired A/B)

* **Lethal gate (class 3).** Add the same-turn face damage of the other burn in hand (Magma Opus
  scaled face, Soulfire's top-card face flip) to the hold's lethal test. This is lockstep-safe because
  the test is a pure function of the GameState.
* **Gate the hold on the combo being reachable (class 1).** Hold only when Hinata is in play or
  castable this turn (`HinataInPlay || HinataCastableFromHand`). Without her, a lone Crackle is just
  burn.
* **Staged copies.** `XCandidates` counts a Soulfire-staged Crackle as a copy, but a staged card is
  castable only until the end of the next turn. Whether that is the intended "the other stays for the
  combo" is a separate question.
* Hinata's committed **value leaf and keep table** were fitted to play with the misclassification.
  This fix changes Hinata play (−0.18 turns/game at d0, −0.03 at d3, −0.04 shipped), so both artifacts
  are aged. Nothing was regenerated. Hinata's regression GT keys will move when the tiers are next
  re-run.
