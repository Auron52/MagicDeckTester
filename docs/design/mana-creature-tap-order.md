# A mana CREATURE should tap after every LAND — the doctrine the tap order never implemented

**Status:** BUILT, fully measured on all three seed tiers, and **DEFAULT OFF** — opt in with
`MTG_DORK_TAP_LAST=1`. Found while sweeping the rank for a §2a payment source
(`lump-mana-sources-as-payment-sources.md` §2b); it turned out to be a much larger, separate effect.

**It is not adopted, and the blocker is a REFERENCE, not an aggregate.** The numbers are strong
(held-out overnight NET −1.3682; searched 545 faster / 54 slower; blind d0 1132 faster / 108 slower;
and it is CHEAPER). But with it on, the hand-played
`references/Mirrorwing_Dragon/claude_s26_gi25.json` replays to **T8 against a recorded T5** — a
`play-drift`, which the reference reproducibility gate fails outright. With it off: **0 play-drift
across all 208 refs**.

**Why, in one line:** making the dork expensive to tap does not make the payment cheaper, it makes it
reach for the ONE-SHOT instead — a Gold Rush Treasure on Mirrorwing. The dork untaps next turn; the
Treasure is gone for good, and the turn that needed it loses the kill (traced in §6b). USER,
2026-08-23: *"We need to keep the treasures for sure."*

**The prerequisite, therefore, is the one-shot rule, not this rank:** prefer the REPEATABLE source
whenever the turn can be paid either way (§2b's "waste is the trigger"). That lives at the PLAN level,
where the sac is actually chosen — with `MTG_TREASURE_PAY_SOURCE` off, a Treasure is a `SacForMana`
action picked by the enumerator and never reaches `ManaSourceRank` at all. Adopting this half alone
trades a Treasure for a tap, and the reference says that is a losing trade.

Note also that **stompy (−0.774) and fivecolour (−0.265) carry most of the measured gain and neither
is Mirrorwing**, so holding this back costs the Mirrorwing generation nothing. USER: *"Other parts can
potentially wait. Especially if they don't impact mirrorwing."*

Revisit together with the deferred items in §5c — the standing goal is a genuinely high-quality mana
engine rather than another static constant.

## 1. The defect

The engine states the doctrine outright, one level above the tap order, in `BatchPrepayMainCasts`:

> *"Every untapped mana CREATURE (the "hold your beater" rule generalised — see `DorkReserveEnabled`):
> a land has no use but its mana, a creature does, so pay off the lands whenever the whole turn can
> be. A mana ROCK is deliberately NOT reserved: it has no other use, so holding it would only make
> the solve fail and cost a second backtrack."*

That is implemented as a **whole-turn reserve** with a rung ladder — hold every dork, and if the
turn cannot be paid that way, release them. It is sound and it is doing real work.

But `ManaSourceRank` — which decides which source pays each pip *within* a payment, once the reserve
has released them — never implemented the same doctrine. It ranks a mana creature **by colour, exactly
like a land**:

```cpp
const int ncol = static_cast<int>(prod.size());
int rank = ncol <= 1 ? 10 : ncol * 10;      // mono=10 dual=20 tri=30 rainbow=50
```

So Elvish Mystic (mono green, rank **10**) taps *before* Game Trail or Rootbound Crag (dual, rank
20). A body that could attack, block, be a Zada copy target or take a Fists of Flame pump is spent
ahead of a land whose only use is its mana. The two mechanisms disagree, and the reserve only covers
the case where the whole turn is payable without the creature.

The ladder already carries this idea for *other* sources — a `{C}`-only manland is rank 60 explicitly
because "it has marginal mana but real attack value, so SAVE it". Mana creatures were simply never
given the same treatment.

## 2. The instrument

`MTG_DORK_RANK_FLOOR=<n>` raises every `CardTemplate::ManaDork` to at least rank `n`
(0/unset = off = byte-identical). `MTG_DORK_RANK_OFFSET=<n>` instead *adds* `n`, which preserves the
tuned order among creatures. Both are throwaway; only the winner, if any, ships — in the shape the
measurement justifies.

Blast radius is narrow and checkable. Of 11 `mana_dork` cards, 5 declare a single colour
(Arbor Elf, Elvish Archdruid, Elvish Mystic, Llanowar Elves, Priest of Titania) and so sit at rank 10;
the rest are tri (Ignoble Hierarch, 30) or rainbow (Birds of Paradise, Bloom Tender, Deathrite Shaman,
Faeburrow Elder, Ornithopter of Paradise, 50).

**A static card scan under-predicts the radius, and the suite caught it.** FiveColour's dorks all
*declare* five colours, so a by-the-card reading says it cannot be affected — yet all three FiveColour
cases moved. The reason is that `ManaSourceRank` calls `EffectiveProduces`, and a SCALING dork
(Faeburrow Elder, Bloom Tender) produces only the colours currently on the battlefield: early, with
one or two colours in play, it *is* a mono/dual source at rank 10–20. Predict blast radius from the
effective produces, not the card data.

## 3. Measured — smoke suite (seed 1001, 39 cases, 13 decks)

Net = sum of per-case (got − expected) loss-penalized avg win turn. **Negative is better.**

| arm | cases moved | NET | searched slower/faster | d0 slower/faster | worst single case |
|---|---:|---:|---|---|---:|
| floor 11 | 6 | −0.1730 | 0 / 12 | 4 / 72 | +0.0000 |
| floor 21 / 27 | 9 | −0.1947 | 1 / 14 | 2 / 84 | +0.0000 |
| floor 31 / 41 | 9 | −0.2073 | 2 / 16 | 4 / 93 | +0.0000 |
| **floor 51** | 14 | **−0.2594** | 2 / 20 | 6 / 107 | +0.0000 |
| floor 60 | 14 | −0.2594 | 2 / 20 | 6 / 107 | +0.0000 |
| floor 64 / 70 | 14 | −0.3607 | 2 / 28 | 8 / 137 | +0.0000 |
| offset 4 | 11 | −0.2140 | 1 / 17 | 8 / 77 | +0.0000 |
| offset 14 | 11 | −0.2243 | 3 / 19 | 7 / 93 | +0.0000 |
| offset 54 | 11 | −0.2330 | 2 / 19 | 8 / 96 | +0.0000 |

**No case is worse at any value tested.** The improvement is monotone in how late the creatures tap,
which is the signature of a doctrine rather than a tuned constant — contrast the generic-tier sweep
recorded in the heuristic-optimization skill, where reordering traded ±2 wins in opposite directions
per deck and produced no clean winner.

Per-case at floor 51 (every other case byte-identical):

| case | delta |
|---|---:|
| mirrorwing d0 / d3 / d5 | −0.0550 / −0.0200 / −0.0267 |
| fivecolour d0 / d3 / d5 | −0.0190 / −0.0334 / −0.0400 |
| stompy d0 / d3 / d5 | −0.0310 / −0.0333 / 0 (digest-only) |
| creature_giving d0 | −0.0010 |
| antilife d0 / d3 / d5 | 0 (digest-only) |

## 4. Why floor 51 and not floor 64

Floor 64 is worth a further −0.10, and **every bit of it is the three stompy cases** (d0 −0.031 →
−0.059, d3 −0.033 → −0.067, d5 0 → −0.040); nothing else in the suite moves between 51 and 64.

It buys that by collapsing tiers that were themselves deliberate, measured adoptions:

* the SCALED mana dork tier (**61** — "every payment that lands another Elf first makes its eventual
  burst bigger"), and
* its position relative to the untap-burst land (**63**, Wirewood Lodge) and the storage land (**62**).

At floor 64 a Priest of Titania ranks *after* the Lodge, reversing that ordering. That is a real
question about stompy's Lodge/Elf interaction, but it is a **different** question from "creatures tap
after lands", it is currently supported by one seed, and it overrides two prior measured decisions.
Floor 51 is the general doctrine and leaves the 60–63 reserve tiers untouched.

## 5. RESOLVED: collapsing the creatures beats ordering them — because colour is the wrong axis

The floor form (`max(rank, 51)`) collapses every mana creature onto ONE rank, leaving battlefield
order to break the tie. That looked like a defect against a form that keeps the tuned order
(mono < dual < tri < rainbow < scaled), so both were measured.

**The first comparison was confounded and must not be quoted.** `MTG_DORK_RANK_OFFSET=54` puts a mono
dork at 64 — past the storage land (62) and the untap-burst land (63) as well — so it changed the
intra-creature order AND the creature-vs-reserve-tier order at once. `MTG_DORK_RANK_BAND=<n>` is the
clean form: `n + rank/10`, which preserves the creature order while keeping every creature below 60.

| | smoke NET | regression NET |
|---|---:|---:|
| **floor 51** | **−0.2594** | **−0.4701** |
| band 51 | −0.2340 | −0.4081 |

Floor wins on both disjoint seed sets, and on **every** case where the two differ. The cases carrying
the gap are fivecolour and mirrorwing — neither has a scaled dork or any 60–63-tier source, so there
the two forms differ *only* in intra-creature ordering. (Stompy, the one deck where the scaled-dork
tier could confound it, measures identical under both.) The comparison is clean.

**Why ordering them is worse.** The ladder's scarcity doctrine — spend the least flexible first so the
flexible sources stay up — is about *colour*, and among creatures colour is not what is at stake. What
a tap costs is the **body**: Elvish Mystic is a 1/1 and Ignoble Hierarch is a 0/1 exalted, so on
Mirrorwing (where creatures are Zada copy targets and pump recipients) the Mystic is the one worth
keeping — and the flexibility ordering taps the mono Mystic FIRST, i.e. exactly backwards. The floor
does not fix this; it merely stops applying the wrong axis, and that is enough to win.

So the finding is **collapse, do not order** — with the honest caveat that neither form models body
value. Ordering creatures by what the body is worth this turn (power, copy-target eligibility, whether
it can attack at all) is unexplored and is the natural follow-up.

## 6. Not shuffle clairvoyance

The standing trap for any measured win here (heuristic-optimization skill): a delta that rides on
diverged draws is the search exploiting a library it can see and a blind rule cannot.

The largest effect is on **Mirrorwing, which contains no fetchland and no library search of any
kind** — Forest, Mountain, Game Trail (reveal-from-hand), Rootbound Crag, Gruul Turf, Sandstone
Needle. Tap order there cannot reshuffle anything, so the win cannot be draw manipulation. The
independent batch measurement on the SHIPPED Mirrorwing list (the suite runs the archived
`v1-twinflame-anger` list) gives **−0.0342 turns over 10,240 games, t = −16.07, 375 better / 45
worse**, and −0.0350 with `MTG_TREASURE_PAY_SOURCE` off, i.e. the effect is independent of §2a.

FiveColour's deltas ARE partly fetch-mediated (Wooded Foothills, Windswept Heath, Verdant Catacombs,
Misty Rainforest) and several of its changed games show diverging draws; discount those. Stompy's and
Mirrorwing's carry the result.

**`explain_game.py`'s divergence hint is misleading on a cantrip deck, and it misled me first.** It
reports "DRAWS DIVERGE ... -> a fetch/shuffle resolved differently" for `mirrorwing_smoke_d5_s1001`
gi64 — but Mirrorwing contains no fetch, no tutor and no shuffle effect. Every one of its tricks is a
`cast_draw` cantrip (Ancestral Anger, Expedite, Scale the Heights, Impolite Entrance, Oracle's
Restoration), so a line that casts a different NUMBER of cantrips reaches a different card on the same
fixed library order. The draws diverge because the game diverged, not the other way round.

**The decisive control is d0.** The blind greedy runs no search at all, so it cannot see or exploit
any future card. At floor 51 the d0 arm is **107 faster / 6 slower** across the suite, and Mirrorwing
d0 alone is −0.0550. A clairvoyance artifact cannot appear at d0.

## 5b. The two SCALING classes measure OPPOSITELY, and the shape that wins

A mana creature whose yield can still GROW is worth holding longer than a fixed one. There are two
such classes and the engine only recognised one:

| class | param | cards | current rank |
|---|---|---|---|
| subtype scaler | `mana_per_creature_subtype` | Priest of Titania, Elvish Archdruid | 61 (`IsScaledManaDork`) |
| **colour (domain) scaler** | `domain_mana` | Faeburrow Elder, Bloom Tender | **none — ranked by colour like Birds** |

USER, 2026-08-23: *"Same with Faeburrow Elder and Bloom Tender actually. They also kind of have the
ability to scale if you don't have all of the colors out. So, it's better to hold them then a birds
or equivalent."*

**And the ladder currently gets the domain scaler exactly backwards.** `ManaSourceRank` ranks off
`EffectiveProduces`, which for a domain source returns only the colours CURRENTLY in play — so at two
colours out Bloom Tender ranks **20 (dual)** and is spent AHEAD of a fixed Birds of Paradise at 50.
It is cheapest to spend precisely when it has the most room to grow.

Measured on smoke (floor 64 in every arm; `MTG_SCALED_DORK_BUMP` holds a class one rank later):

| arm | NET | stompy d0/d3/d5 | fivecolour d0/d3 |
|---|---:|---|---|
| floor 64 (both classes tied with the flat dorks) | −0.3607 | −0.059 / −0.067 / −0.040 | −0.0190 / −0.0334 |
| + hold the SUBTYPE scalers back | −0.2584 | −0.030 / −0.033 / 0 | −0.0190 / −0.0334 |
| + hold BOTH classes back | −0.2660 | −0.030 / −0.033 / 0 | −0.0200 / −0.0400 |
| **+ hold the DOMAIN scalers only** | **−0.3683** | −0.059 / −0.067 / −0.040 | −0.0200 / −0.0400 |

So the two classes want **opposite** treatment:

* **Domain scalers: hold them back.** Confirms the USER's read, and it is the only thing that has
  moved fivecolour in this whole arc.
* **Subtype scalers: do NOT hold them back** — that costs −0.10, all on stompy, and it is the tier-61
  `DorkGrowth` doctrine ("tap it last so its burst is bigger") being wrong on this deck. The reason is
  BODY COUNT: Priest of Titania taps for N off ONE body, so forcing it last burns N Llanowar Elves
  for the same mana and loses N bodies — and on an Elf deck the bodies are the win condition. That is
  the same exactness argument the Treasure ordering rests on, pointing the other way.

Both effects are real; which dominates is a per-deck fact, and the static rank buys the average.

## 5c. What a SITUATIONAL order would need (deferred)

USER, 2026-08-23: *"It's the typical problem. If we want a smarter order we'll need situational
awareness. For example: We are attacking for lethal this turn or the creatures will be pumped vs a
turn where attacks are low impact."*

That is the correct framing and it is out of reach of this hook as written. The signals wanted are
per-TURN and per-PERMANENT:

* is this turn a lethal alpha strike (then a body is worth its damage, not its mana)?
* is a pump / copy-magnet trick castable this turn (then a body is a multiplier)?
* or are attacks low-impact (then a body is just mana and should be spent freely)?
* and, for the yield question: does this source's yield MATCH the remaining need, so one big body
  pays what several small ones would?

None of it is visible to `ManaSourceRank`, which takes a `CardDefinition` and cannot see counters,
temp pump, animation, or current yield — the same wall `TapPowerOrderEnabled` documents ("Extending
the scarcity path the same way needs that signature widened").

### The right framing (USER, 2026-08-23): the payment depends on WHAT THE PLAN DOES

> *"Mirrorwing Dragon has the same problem with the treasure situation you mentioned... Sometimes we
> aren't buffing our creatures and we should just sit back and use the dorks for mana and other times
> we need to go off and in those cases the treasures are better so we have more attackers. So, in a
> sense, the right way to pay the mana is actually dependent on what the plan does."*

This is the generalisation of the gi81 loss in §6b, and it is the correct one. The tap order is not a
property of the CARD, it is a property of the PLAN being paid for:

* plan contains a pump / copy-magnet trick targeting your own creatures -> a body is a MULTIPLIER;
  spend the one-shot Treasure and keep the dorks untapped;
* plan is a plain durdle turn -> a body is just mana; tap the dorks and KEEP the Treasure, because
  a Treasure held is worth exactly one mana on any later turn while a dork untaps anyway.

Both halves are visible today. **`PlanContext` (`ai/PlanContext.h`) already exists for precisely this
purpose** — *"what else this turn's plan is going to do, visible to a DecisionProvider while it is
being asked about ONE action inside that plan"* — exposing the plan's action list, the land it plays,
and the rest of the plan after the current action, via the free function `CurrentPlanContext()`. So a
plan-aware tap order does NOT require widening `ManaSourceRank`'s signature after all; a provider can
read the context directly.

**The one thing to verify before building it:** `PlanContext` is set on the plan-ENUMERATION path, and
the tap decisions happen during payment/application. Whether a `PlanContextScope` is live at
`ManaSourceRank` time (and at `TapForCostBacktrackWorker` time) has not been checked. If it is not,
the work is to extend the scope over payment — which is far smaller than a signature change, and
`CurrentPlanContext()` returning null already has a defined "behave exactly as before" contract, so it
degrades safely.

This supersedes the "widen the signature" note above as the recommended route.

### The cheapest situational signal of all: MAIN 2 (USER, 2026-08-23)

> *"If we are in the second main, the 'reserve creatures for attacks' rule is no longer relevant. So,
> the situational awareness is potentially how we could construct a more general solution."*

Combat is over by main 2, so a body held back post-combat buys nothing **in a goldfish** — it cannot
attack this turn and there is no opponent attack to block. The whole-turn reserve
(`BatchPrepayMainCasts`) currently gates on `DorkReserveEnabled() && g_scripted_tapmode != 1 &&
n <= 64` and carries **no phase test**, so it holds dorks back in main 2 for a combat that has
already happened. `AttackerReserveEnabled`'s "hold your beater" has the same shape.

This is the cheapest possible version of the situational rule and it needs nothing new:
`BatchPrepayMainCasts(GameState& state, const std::vector<Action>& acts)` already receives BOTH
signals it would need — the phase (from `state`) and what the plan does (from `acts`). Expected to be
free-or-better in the goldfish; it must be revisited for 1v1, where holding a blocker in main 2 is
real value (same caveat as `untap-land-burst-net-cancellation-limits.md`).

Caveat to check before measuring: a VIGILANT mana source (Faeburrow Elder) attacks and stays
untapped, so it is already tappable in main 2 at no cost — the FAEBURROW DOCTRINE
(`BoardHasVigilantManaScalerAttacker`) covers that case, and a main-2 release must not double-count
it.

**Ranked follow-ups, best first:**

1. **Phase-gate the reserve** (main 2 releases the bodies). Smallest change, both inputs already in
   hand, expected free-or-better.
2. **Plan-aware tap order** via `acts` / `CurrentPlanContext()`: pump-or-copy-trick in the plan ->
   keep bodies and spend the one-shot; plain turn -> tap bodies and keep the one-shot. This is what
   fixes §6b's gi81 properly.
3. **Yield-vs-need matching** for the subtype scalers (§5b) — one big body instead of N small ones.

## 6b. The one REAL same-draws loss, traced — it reaches for the one-shot instead of the dork

Of the 30 searched-depth SLOWER games on the held-out overnight run, `classify_turn_later.sh` scores
**16 as churn** (they recover at 4x/16x budget) and 14 as PERSISTS. Of those 14, fivecolour (5) and
stompy (5) both search their libraries — fetchlands, Natural Order, Call of the Wild — so a persisting
change there is a physically different game. Mirrorwing does not shuffle, so its 4 (two games seen at
both d3 and d5) are the ones that can be real. One is a cantrip-count divergence (§6). The other,
`mirrorwing_overnight_d5_s6006 gi81`, has an **identical kept hand and identical draws** and loses a
turn (T4 -> T5). Traced:

```
T3, casting Zada {3}{R} = 4 mana.
Board: Forest, Rootbound Crag, Mountain, Elvish Mystic, + a Treasure token from T2's Gold Rush.
  OLD  taps 3 lands + ELVISH MYSTIC  -> exactly 4.  Treasure SURVIVES.
  NEW  taps 3 lands + cracks the TREASURE -> Mystic stays untapped and attacks for 1.
T4:  OLD casts Twinflame x2, Ancestral Anger, Gold Rush x2 -> lethal.
     NEW casts Twinflame x2 only  -> opp at 13, wins T5 instead.
```

Ranking the dork late made the payment reach for the **one-shot** resource instead of the
**repeatable** one. The dork untaps next turn; the Treasure is destroyed, and T4's lethal needed it.
The floor bought 1 damage on T3 and lost the T4 kill.

Note this game runs with `MTG_TREASURE_PAY_SOURCE` **off**, so the Treasure is still a `SacForMana`
ACTION chosen by the plan enumerator, not a ranked payment source — the floor changed which plan the
affordability path preferred, not a tap order between the two.

**This is the known cost of a static rank, not a defect in it** (heuristic-optimization skill,
lesson 3: a static heuristic optimises the AVERAGE and loses situational edges — record the loss,
do not hide it). It is also exactly the case §2b's consumption axis is meant to govern, and it is in
tension with §9's "Treasures before creatures": on THIS turn the Treasure should have been held. The
principled fix is plan-level and is follow-on work, not a blocker — prefer the REPEATABLE source
whenever the turn can be paid either way, which is §2b's "waste is the trigger" rule applied where
the sac decision is actually made.

## 7. It is also cheaper

Deterministic counters (`MTG_ROLLOUT_STATS`, contention-proof — wall clock on this box is not; see
`lump-mana-sources-as-payment-sources.md` §12), 10,240 Mirrorwing games at gen settings:

| arm | rollout calls | turn_steps |
|---|---:|---:|
| baseline (§2a on) | 30,127,634 | 60,868,943 |
| + creature floor 27 | 29,294,827 (−2.8 %) | 58,550,753 (−3.8 %) |
| + floor 27 + Treasure rank 26 | 28,982,805 (−3.8 %) | 57,869,559 (−4.9 %) |

Better play wins earlier, so each rollout simulates fewer turns — the same mechanism §12 of the lump
doc records for the scarcity fix. **There is no quality-for-speed trade to weigh here.**

## 8. Interaction with §2b (the Treasure rank)

The two are complementary, measured on the same 10,240-game Mirrorwing set:

| arm | delta | t | better / worse |
|---|---:|---:|---|
| Treasure rank 25/26 alone | −0.0050 | −5.24 | 68 / 18 |
| creature floor 27 alone | −0.0342 | −16.07 | 375 / 45 |
| both | −0.0485 | −19.19 | 512 / 55 |

Treasure-before-creature is worth −0.0050 on its own but −0.0143 once the creatures are pushed late,
which is what the §9 doctrine predicts: the ordering only bites when both sources are live in the
same payment.

Held-out confirmation of the Treasure rank alone (disjoint seeds 950000, 20,480 games):
**−0.0044, t = −6.77, 127 better / 39 worse** vs −0.0050 on train. No overfit.

## 9. Remaining gates before adoption

1. Regression mode (seeds 2002 / 3003) — *in progress*.
2. `classify_turn_later.sh` on every searched-depth SLOWER game — *in progress*.
3. Held-out overnight seeds (4004 / 5005 / 6006 / 7007).
4. Resolve §5 (floor vs offset) rather than shipping the unexplained gap.
5. Decide the SHAPE: a magic constant is the wrong artifact. The rule wants to be expressed as a
   creature BAND above the land tiers, in `GenericProvider` (it is param/template-gated and inert for
   every deck with no mana creature), not as a per-deck override.
6. USER approval, then GT rebaseline for smoke + regression + overnight.

## Related

* `lump-mana-sources-as-payment-sources.md` — §2a/§2b, where this was found
* `.claude/skills/heuristic-optimization.md` — the loop this followed, and the clairvoyance test
* `slivers-restricted-mana-tap-order-bug.md` — the last time a rank tier collision cost a deck real damage
