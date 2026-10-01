# Fungus: the fodder guard credits spore pops but not FADE pops

**Status: FIXED 2026-10-01, `MTG_SAC_FODDER_FADE_CREDIT` DEFAULT ON.** Found while checking a user
observation about Mycoloth. The fix removes **486 of the 510** measured over-rejects and leaves the
sound ones; it moves **no play in the suite** (140 regression cases byte-identical, including five
Fungus cases at d0/d3/d5). So this is a latent-correctness fix, not a measured win — see "Severity,
stated honestly" at the end.

## The defect in one line

`SubsetOversubscribesSacFodder` counts the Saprolings a plan's **spore pop** will create, and counts
**zero** for the Saprolings the same plan's **Saproling Burst fade activation** will create — so it
rejects "drain the Burst, feed the outlets", which is the line this deck is built around.

## Where

`plan_fodder_credit` (the `MTG_SAC_FODDER_RESERVE` path, DEFAULT ON) in `src/ai/TurnSolver.cpp`
credits a body for:

* `Action::Kind::CastFromHand` of a matching creature — `+1`
* `AbilityMode::SporeSaproling` whose `spore_token_subtypes` match —
  `+ max(1, chosen_x) * max(1, spore_creates_tokens)`

and sets `unbounded` (→ allow) for a match on any of `dies_`, `sac_outlet_`, `etb_created_`, `tap_`,
`cast_`, `attack_token_subtypes`.

`fade_token_subtypes` **is in neither list.** So an `AbilityMode::FadeSaproling` activation:

* takes no credit (it is not the spore branch), **and**
* does not set `unbounded` (it is not in the six-list),

i.e. it contributes exactly nothing, and the guard then judges the plan against the pre-activation
board. `plan_can_add` (the reserve-off path) has the same omission.

## The measurement

`MTG_SAC_FODDER_AGG=0 MTG_FODDER_TRACE=1` on a Fungus keep-rollout, hand
`Saproling Burst x2; Utopia Mycon x1; Forest x2; Doubling Season x1; Mycoloth x1`, first 4,000 traced
rejects:

| | count |
|---|---|
| rejects naming a Burst **fade activation** (`k26` + `/xN`) — **over-rejects** | **510** |
| rejects naming only a Burst **cast** (`k0`, creates no bodies — correct) | 311 |
| sampled rejects total | 4,000 |

Action kinds in the trace: `k0` = `CastFromHand`, `k7` = `SacForMana` (Utopia Mycon),
`k9` = `SacCreatureOutlet` (Psychotrope / Vitaspore / Deathspore), `k26` = `ActivatePermAbility`
(the fade pop); `/xN` is `chosen_x`, the number of activations.

The clearest over-reject, 36 times in the sample:

```
[fodder] REJECT filt=Saproling sup=1 cr=0 dem=2 | Saproling Burst/k26/x5 Utopia Mycon/k7 Utopia Mycon/k7
```

Drain the Burst five times (five Saprolings), then activate two Utopia Mycon. Supply 1, credit **0**,
demand 2 → rejected. The plan is physically legal and is the deck's bread and butter.

**510/4,000 is a sample, not a rate** — these are the first 4,000 rejects of one hand's rollout, which
biases toward early turns. The global share is not measured.

## Why this direction matters

The guard's own comment states the rule it has to obey:

> *"Being conservative here is the safe direction: a missed reject leaves the pre-existing
> (documented, executor/rollout-shared) apply-time degradation exactly as it was, whereas an
> over-reject would delete a line the deck can really play."*

This is the forbidden direction.

## The fix, and the trap in it

Add a `FadeSaproling` branch to `plan_fodder_credit` mirroring the spore branch:

```
credit += max(1, chosen_x) * max(1, fade_creates_tokens)
```

**But it must be conditional on the tokens surviving, and that is where the lords come in.** A fade
token's P/T is a characteristic-defining ability reading the Burst's remaining counter total
(`RefreshFadeTokens`), so an activation count that empties the Burst mints `0/0`s that die to the
toughness state-based action on the spot — **unless a Saproling lord is out.** USER 2026-10-01:
*"even 0 would work if a lord is on board … usually the saprolings would die when it hits zero, but
with the extra toughness they survive."*

So the credit is `k` bodies iff `remaining_counters + saproling_anthem_toughness > 0`. Crediting
unconditionally would introduce an over-credit (a missed reject) — the safe direction, but still
wrong, and it would hide the apply-time degradation this guard exists to prevent.

`FadeKLandmarks` already has exactly this arithmetic and already gets the lord case right:

```
k_fodder = (C + A_t - 1) / c      // largest k whose bodies are alive RIGHT NOW
```

With `A_t = 0` that is `C-1` (drain to one counter); with one Sporecrown Thallid (`A_t = 1`) it is
`C` — drain to **zero** counters, because fading sacrifices the Burst only on the upkeep where it
*cannot* remove a counter, so a Burst on zero survives the whole turn cycle. `plan_fodder_credit`
should read `sap_anthem_t` the same way, or share the helper.

## Related: what the second main does to the devour axis

USER 2026-10-01, reasoning about Mycoloth: *"unless they can attack this turn putting the saprolings
into Mycoloth is indeed better … by the time we sacrifice to Mycoloth they would have already
attacked (second main) … so it is actually safe to always pull them in goldfishing."*

The argument is sound and worth acting on separately. Mycoloth is summoning-sick whichever main it is
cast in, so casting it **post-combat** costs its devour fodder nothing: the bodies attack first, then
get eaten. Beastmaster Ascension quest counters land on attack **declaration** and are permanent, so
they are banked before the sacrifice too.

Consequences for `FungusProvider::DevourCountCandidates`:

* **The rungs that protect THIS turn's attack become dead weight** — `free_k` in the landmark menu
  (the prefix that eats no attacker) and the `atk`-based this-turn quest rung in the ladder. Both
  exist only because devour was assumed pre-combat.
* **The NEXT-turn quest rung must stay** (`kn = own + 1 - need_attackers`). USER, immediately after:
  *"Hmm… unless we need them for Beastmaster I guess. (numbers that is)"* — exactly right, and it is
  the next-turn attacker *count*, not this turn's counters. Devour repays 2 Saprolings per body at the
  next upkeep and those are summoning-sick, so they cannot fill next turn's combat.
* **The outlet rungs must stay.** A body spent on Psychotrope (a card) or Utopia Mycon (mana) genuinely
  competes with a body spent on counters. The second-main argument settles the *attack* axis only.

**Precondition to check before any of this: is Mycoloth actually enumerated in the second main?**
`fungus-second-main-adopted.md` says the searched second main is adopted for this deck, but the
dominance argument above is only available if the Mycoloth cast really is offered there. If it is not,
the pre-combat rungs are load-bearing and none of the above applies.

## And the natural next rung: a lethal projection

USER: *"it should be easy to figure out when Mycoloth will be lethal, and it normally would be. That
would be an easy choice there."*

This is the same shape `FadeKLandmarks` already ships for the Burst: `k_win_now` / `k_win_next`
collapse the menu to **one** entry once a kill is provable on the conservative projection, because
past the winning k every extra counter is spent for nothing. Devour wants the mirror image — project
whether `k` makes Mycoloth lethal next turn (it enters with `2k` counters, doubled under a Doubling
Season, as a `(4+2k)/(4+2k)` that attacks next turn and mints a Saproling per counter at the upkeep),
and if so narrow to the smallest winning `k`. Judge it on the **conservative** projection, since it is
a narrowing — the asymmetry `FadeKLandmarks` documents at length (`_lo` to suppress, `_hi` to admit).

## What was built, and what it measured

`ProjectedTokenAnthemToughness` + `FadeActivationLiveBodies` in `TurnSolver.cpp`, called from the three
places that now cannot disagree: `plan_fodder_credit` (the reserve path), `plan_can_add` (the
reserve-off path), and `BuildFodderIndex` (the aggregate form). `MTG_SAC_FODDER_AGG_VERIFY=1` runs the
aggregate and the original on every subset and reports the first disagreement — 0 mismatches on the
Fungus replay and across the smoke suite.

Same hand, same trace, before and after:

| | fade-activation rejects | of sampled |
|---|---|---|
| before | **510** | 4,000 |
| after | **24** | 4,000 |

And the 24 survivors are *correct*, which the arithmetic now shows on its face:

```
before:  sup=1 cr=0 dem=2 | Saproling Burst/k26/x2  Vitaspore Thallid/k9  Utopia Mycon/k7
after :  sup=1 cr=2 dem=4 | Saproling Burst/k26/x2  Vitaspore Thallid/k9  Utopia Mycon/k7 x3
```

The credit is now 2 — the two bodies the drain really makes — so supply+credit = 3, and the surviving
reject is a *wider* selection: four outlet activations against three bodies, which genuinely
over-promises by one. The enumeration reaches those deeper plans only because the shallow ones are no
longer killed.

**The anthem is computed ONCE per enumeration, lazily, and only if a fade activation is a candidate**
(it costs a `GatherBoardSources` + `ComputeLordBonus`). It is never per subset — that is the whole
point of the aggregate it rides on.

## Severity, stated honestly

* **In the enumeration it is real and large**: 486 of 510 traced rejects were deleting legal plans.
* **In the suite it is invisible**: smoke 101/101 and regression 140/140 byte-identical, Fungus
  included (d0/d3/d5). The over-reject needs ≥ 2 sac-outlet activations co-selected with a fade drain,
  and the suite's searched play apparently does not reach that shape — it was found in a
  **keep-generation rollout at d1/b3**, hand
  `Saproling Burst x2; Utopia Mycon x1; Forest x2; Doubling Season x1; Mycoloth x1`.
* **So the place it can still pay is mulligan generation, and that is UNMEASURED.**
  `MTG_SAC_FODDER_FADE_CREDIT=0` exists as the arm for exactly that measurement.

## Correction to the older doc

`sac-fodder-created-in-the-same-line.md` says of this guard: *"The subset guard understands mid-plan
replenishment; the candidate emitter does not."* That was true of **spore** pops and false of **fade**
pops, which is the hole above. The adopted same-line fusion itself handles fade correctly
(`SameLineFodderKind::Fade`), and `MakeSameLineSacFodder` re-checks the board afterwards for precisely
the reason this fix needed a survival test — its own comment: *"a 0/0 fade token can die to the
toughness SBA on arrival."*

## Order of remaining work

1. ~~Fix the fade credit.~~ **DONE** (above).
2. ~~Confirm Mycoloth is enumerated in the second main.~~ **CONFIRMED — and it was already built for
   the user's own reason.** `GoldFishRunner::DeckUsesSecondMain` whitelists `devour > 0` and
   `FungusProvider::MainPhaseOverride` returns `MainPhase::Main2` for it and `Main1` for everything
   else, both behind `MTG_FUNGUS_M2_DEVOUR` (default ON). Recorded there from the user on 2026-09-23:
   *"doing mycoloth in the second main is important, because you want to attack with existing
   creatures and then sacrifice them."* That comment already names the Beastmaster consequence too.
3. **Revisit `DevourCountCandidates` — but the analysis above needs correcting.** Because Mycoloth is
   cast in the SECOND main, the bodies that attacked are already **tapped** when the devour axis is
   enumerated, so `CanAttackFull` is false for them and the attack-protecting machinery is **already
   self-inert**: `free_k` collapses to `own` and is then not pushed (the push requires
   `free_k < own`), and the ladder's this-turn quest rung never fires because no `bodies[i].atk` is
   true. So there is probably nothing to *remove* — which also explains why the landmark menu's
   measured narrowing came from dropping the `big`-body rungs rather than from `free_k`. **Verify with
   `MTG_DEVOUR_TRACE=1`, which prints `free=` and `outlet=`, before acting on it.**
4. The **lethal projection** is then the one real addition left, and it is the user's point: *"it
   should be easy to figure out when Mycoloth will be lethal, and it normally would be. That would be
   an easy choice there."* Mirror `FadeKLandmarks`' `k_win_now` / `k_win_next` collapse — judged on the
   CONSERVATIVE projection, because narrowing to one entry may only be done on a `k` that can be
   *proven* to win.
5. Smaller, same family, not fixed here: the **spore** branch does not apply the token doubler, so it
   under-credits under a Doubling Season — the same defect, in the same forbidden direction.
