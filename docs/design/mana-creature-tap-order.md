# A mana CREATURE should tap after every LAND — the doctrine the tap order never implemented

**Status:** MEASURED 2026-08-23 behind throwaway scaffolding (`MTG_DORK_RANK_FLOOR`,
`MTG_DORK_RANK_OFFSET`), **not adopted**. Found while sweeping the rank for a §2a payment source
(`lump-mana-sources-as-payment-sources.md` §2b); it turned out to be a much larger, entirely
separate effect that has nothing to do with Treasures.

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
