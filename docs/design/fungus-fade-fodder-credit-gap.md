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
3. **`DevourCountCandidates`: DO NOT drop the attack-protecting rungs. Measured, and it refutes two
   earlier readings of mine — both of them wrong in the same direction.**

   I claimed first that those rungs were *dead weight* (because devour is post-combat) and then that
   they were *already self-inert* (because the attacked bodies would be tapped). `MTG_DEVOUR_TRACE`
   says neither:

   | | emissions |
   |---|---|
   | devour axis emitted for **main 1** | **34,782 (87%)** |
   | devour axis emitted for **main 2** | 5,218 (13%) |

   and `free_k = 0` in 2,112 of 2,275 landmark emissions — i.e. the most expendable body on the
   ladder *can* attack, so eating anything costs an attack. The bodies are not tapped.

   **Why**, and it is structural: `ClassifyMainPhase`/`MainPhaseOverride` is applied by a `remove_if`
   *after* `CollectActions` has already built the variants, and `MainPhaseFilterActive` requires
   **`SearchedPlayActive()`** — *"SEARCHED play only (USER doctrine: no greedy solve within the
   search). At depth 0 the deferred casts would be decided by the greedy second main — the forbidden
   pairing."* So at d0 and in the greedy playout tails the filter **stands down** and Mycoloth really
   is offered pre-combat. That is deliberate: the same function carries *"NO PLAYOUT CARVE-OUT HERE,
   and that is a USER RULING rather than an omission."*

   So the user's dominance argument is right about the **searched** turn, where the pin is live, and
   would be wrong as a blanket narrowing of the hook — which serves both phases, with the pre-combat
   emissions in the majority.

   **And do not re-derive the obvious cost fix either.** Standing the filter down inside a playout is
   recorded in-code as REJECTED BY THE USER, 2026-09-23, measured at 1.07x: *"I don't want to cast it
   in main 1 in either situation. That makes no sense whatsoever. The purpose of casting it second
   main is to have the attack phase in-between."* The rollout's second main must stay a second main;
   only its PRICE is negotiable.

   `MTG_DEVOUR_TRACE` now prints the phase (`m1`/`m2`) for exactly this reason — without it a `free=`
   reading cannot be attributed to a phase, which is how both wrong inferences happened.
4. ~~The **lethal projection**.~~ **BUILT 2026-10-01** — `MTG_FUNGUS_DEVOUR_LETHAL`, default ON. See
   §4 below.
5. **Extend the lethal collapse to the PRE-combat main.** Not attempted, and the reason is in §4: the
   proof that `k = own` dominates relies on this turn's combat being over. Pre-combat it needs an
   *upper* bound on this turn's swing with lords and anthems included, and an undercount there deletes
   a win. Worth doing only if the m1 emissions are shown to cost real wall at d0 / in playout tails,
   where the phase pin stands down.
6. Smaller, same family, not fixed here: the **spore** branch does not apply the token doubler, so it
   under-credits under a Doubling Season — the same defect, in the same forbidden direction.

---

## §4 The lethal collapse: `MTG_FUNGUS_DEVOUR_LETHAL` (default ON)

The user's words, and the whole chain that produced them:

> *"If our mycoloth would be lethal next turn it might make sense to bring Saproling Burst down to one
> counter and sacrifice all of them."*
> *"That is a real case for maxing it out."* *"i.e. It can turn into 10 counters on Mycoloth."*
> *"unless they can attack this turn putting the saprolings into Mycoloth is indeed better"*
> *"And by the time we sacrifice to Mycoloth they would have already attacked. (second main)"*
> *"So It is actually safe to always pull them in goldfishing."*
> *"it should be easy to figure out when Mycoloth will be lethal, and it normally would be. That would
> be an easy choice there."*

**What it does.** In `FungusProvider::DevourCountCandidates`, a proven kill collapses the whole devour
menu to the single entry `own` — eat everything.

**Why the entry is `own` and not the smallest winning `k`, which is the OPPOSITE of the fade axis.**
On the fade axis, spending a counter past the minimum *shrinks every body already out*, so the
smallest winner strictly dominates and `FadeKLandmarks` takes it. Here there is no such cost:
`k = own` is simultaneously the maximum on the counter axis (more counters → a bigger Mycoloth and
more Saprolings at every later upkeep) **and** on the drain axis (every body dies, so a Slimefoot sees
the most deaths it can see). It dominates rather than ties. That is the user's *"a real case for
maxing it out"*.

**POST-COMBAT ONLY, and that restriction is the entire soundness proof.** In the second main this
turn's combat is already over, so the devour count cannot change this turn's combat damage *at all* —
the only this-turn damage left that `k` moves is the sacrifice drain, which is monotone in `k`. So no
`k` wins sooner than `k = own`, and "`k = own` wins next turn" is therefore sufficient to collapse.
Pre-combat the same collapse is **unsound**: there every body eaten is an attacker removed from *this*
combat, so a smaller `k` can win a whole turn earlier.

The restriction costs **reach, not value**. `MainPhaseOverride` pins Mycoloth to Main2, so the m2
emission is the one searched play really plays; the m1 emissions (87% of the total, §3) are built and
then removed again by the phase filter's `remove_if`, so narrowing them would save the `Action`
construction and nothing downstream. The exception is d0 and greedy playout tails, where the filter
stands down — item 5 above.

**The projection is deliberately the crudest sound one:** Mycoloth's own body, alone, next turn.

```
counters  = (devour x own) << DoublerShift(state, me, /*for_tokens=*/false)
myco_next = printed_power + counters          // 4 + counters
collapse iff  myco_next >= opp_life
```

It counts **nothing else**: no lord bonus (at `k = own` every lord has been eaten), no anthem, no
surviving attacker, and none of the Saprolings the upkeep mints — those arrive summoning-sick and
first attack the turn *after*. Every omission is pessimistic, which is the only direction a narrowing
is allowed to be wrong in. The doubler is read live and applied to the counters because devour's
enters-with counters route through `PutPlusCounters`, which is where `DoublerShift` lives.

**It fires, and not only trivially.** 40 games of the shipping Fungus deck at d3/b150, single-threaded,
`MTG_DEVOUR_TRACE=1`:

| | count |
|---|---|
| `DevourCountCandidates` calls with `GameState::phase == PostCombatMain` | 8,019 |
| …of those, **`LETHAL-HIT` → menu collapsed to `{own}`** | **3,246 (40%)** |
| …`LETHAL-short` (gate reached, projection fell short) | 4,773 |

spread across opponent life **1 through 15** (551 hits at 11, 606 at 12, 347 at 1), and the
Doubling-Season rungs the user named show up explicitly — `own=8 ctr=32`, `own=11 ctr=44`,
`own=5 ctr=20`.

### What it is worth: 0.2% of the engine's work, and nothing in play

**Quality: byte-identical.** Smoke 101/101, `configs changed 0`, **`play-changed 0`** at both
`[searched]` and `[d0]`, scenarios 118/118, unit SUCCESS. The avg-turn figure matches to four decimals
in every cell measured below. So on every board where the collapse fires, the search was already
choosing `k = own` — the narrowing deletes options it was rejecting anyway.

**Wall could not answer the question, and saying so is the result.** A paired, interleaved,
single-threaded wall A/B on a box at load ~26/24 gave per-pair ratios of 0.82, 0.82, 1.01, 1.19, 1.36
on ONE cell — ±30% noise around a real effect two orders of magnitude smaller. The user had already
said it: *"Wall is not super reliable right now because o contention."* Abandoned mid-run in favour of
the engine's own meter.

**`units` is the right instrument and it is contention-immune.** `GoldFishRunner` says so at the
SLOW-GAME site: *"ms is wall on a shared box and cannot carry an A/B, units is the deterministic work
meter."* `MTG_SLOW_GAME_MS=1` prints one per game. One run per arm is enough, because the meter is
deterministic.

| cell | units ON | units OFF | OFF/ON | games whose units differ | avg turns |
|---|---|---|---|---|---|
| d3 b150 x40  | 1,944,623 | 1,949,201 | **1.0024x** | 4 / 40 | 5.3250 both |
| d3 b10 x150  | 1,940,677 | 1,946,581 | **1.0030x** | 16 / 150 | 5.3800 both |
| d5 b20 x75   | 1,233,080 | 1,233,652 | **1.0005x** | 4 / 75 | 5.3867 both |
| d0 x300      | — (no units armed on the d0 path) | — | — | 0 / 3 | 5.6400 both |

**0.05%–0.30%. The honest reading: the devour axis was never the cost.** The second-main solve is
gated by `SecondMainNeedsDeferredCast` — it runs only on turns holding a payable Main2 cast — and its
subset space is a handful of actions, so collapsing a five-entry menu to one entry collapses a sliver.
The best single game moved 1.088x (d3/b10 gi=39) and the aggregate is noise-floor.

This is now the **third** independent measurement saying the same thing about this axis: the landmark
menu (`MTG_FUNGUS_DEVOUR_LANDMARKS`) was built for it and never earned adoption; the devour-count
narrowing that *is* adopted was priced at ~6.6% of d1/b3 and ~1% of searched play and its own comment
warns *"It is NOT a fix for the slow rollouts... Do not cite it as one"*; and now the lethal collapse
buys 0.2%. Anyone arriving here looking for Fungus wall should go to `operator new` (2.29%) and
`~vector<Action>` (1.37%), not to the devour menu.

**So why keep it, and default ON?** Because it is not a perf lever. It is the user's ruling —
*"it should be easy to figure out when Mycoloth will be lethal... That would be an easy choice there"*
— expressed as a provable dominance collapse that cannot cost a line, and it is the piece the
pre-combat extension (item 5) would build on. `MTG_FUNGUS_DEVOUR_LETHAL=0` is the hatch. If the
0.2% does not justify the code to the user, turning it off is a one-character change and loses nothing
measured.

**A diagnostic lesson, paid for twice in this session.** The first two probe runs reported **0**
collapses, which read as a dead hook. Both were measuring a **stale binary**: I had run
`./build.sh 2>&1 | tail -20`, and the pipe returns *`tail`'s* exit code, so a compile error
(`def.card.m_power` is a `std::optional<int>`) came back as success. That is the repo's own
*never pipe a gate's output* rule applied to a build. The trace now also prints the phase on every
`[devour-cands]` line **and prints `LETHAL-short` as well as `LETHAL-HIT`**, because "the gate was
reached and the projection fell short" is otherwise indistinguishable from "the gate was never
reached" — which is exactly the ambiguity that made the stale-binary runs look like a design problem.
