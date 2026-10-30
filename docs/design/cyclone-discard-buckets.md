# Unpredictable Cyclone: bucketed cleanup discard (DESIGN, for USER review, 2026-10-28)

Status: **proposed, not built.** Discard buckets are USER-owned (analyze-deck Stage 5i); this is the AI's best shot.
It needs the user's amendments and approval before it ships ON.

## Today

`CycloneProvider` overrides no discard hook, so the deck runs the generic rule (`CleanupDiscardRankingWithOrder`,
SpellEffects.h): no required pieces, so the shed is **highest mana value first, ties to the earlier hand card**.
With no Eldrazi in hand that sheds the sole Unpredictable Cyclone (MV 5) or a Street Wraith (MV 5) first. That is
the d5 gi5 defect (ledger Q44(2)). The real game's cleanup trials the whole hand, so it can recover. The rollouts
always take index 0, so they shed it. The same order is also the tie-break behind the put-back scorer
(`PutBackRanking` reads the shed order), which is why ties put back the sole Cyclone (Q44(1)'s mechanism).

Evidence already in hand: the Stage 4 discard analysis (ledger Q42), 125 search-labelled cleanups at d3.
- Its derived flat order, most expendable first, was Steam Vents, Toxic Deluge, Cascade Bluffs, Remand, Ulamog the
  Ceaseless Hunger, Myriad Landscape, Xander's Lounge, Ulamog the Infinite Gyre.
- The cards it found most costly to discard were Street Wraith, Unpredictable Cyclone, Seething Song, Scalding Tarn
  and Ponder.
- 97% of those cleanups had several optimal picks, so the stakes per decision are small. The cost is in the
  rollouts, which shed the Cyclone.

## Design: one model, two consumers

The cantrip scorer already computes a per-card **keep need** from the go-off estimate (`CycloneGoOff`,
DecisionProviders.cpp; tiers `kCyc*`). Those tiers ARE buckets with quotas, netted against the board:
- one Cyclone, counted across hand and battlefield;
- lands up to the {3}{R}{R} need, net of the lands on the battlefield and of a Seething Song in hand;
- up to 3 creature cyclers, Wraith first;
- Song while it is needed.

The discard should read the same model, so the put-back and the discard can never disagree about what is
expendable. The additions below are what a discard needs and a put-back does not.

### Buckets (quota-first; only the overflow is sheddable; net of the battlefield)

| bucket | members | quota | inside an over-full bucket, shed first |
|---|---|---|---|
| ENGINE | Unpredictable Cyclone; **Long-Term Plans stands in while no Cyclone is found** (USER 2026-10-28: "the only tutor in the deck. Perhaps it can take the Cyclone slot in that case") | 1 across hand + battlefield | a surplus copy is DEAD (cycling it for {2} walks to the next enchantment, i.e. only another Cyclone) |
| MANA: lands | every land | the go-off need: 5 minus lands on the battlefield, minus 2 with Song in hand; red covered first (the shared red count). **After Cyclone is out: up to 4 lands on the battlefield** (USER Q-D1: "4 on board is good") | keep order, best first (USER Q-D6 and the earlier rulings): **Scalding Tarn (the fetches) > the first Myriad Landscape > Steam Vents** ("taps for the two important colours") **> the other lands by colour need > Xander's Lounge (enters tapped) > a second Myriad** ("I wouldn't keep 2 Myriad"). One Myriad is kept "even if we are short on a colour". The Tarn is also the shuffle that clears junk we put on top. Shed from the bottom of that order |
| MANA: extra lands | lands beyond the quota | **2 more, kept below the cyclers and Brainstorm / Brainsurge but above Remand**; a third extra land and beyond sheds BEFORE Remand (USER Q-D3: "keep some number of extra land (maybe 2?) and then Remand. Keeping more land than that is likely worse even than remand"; Q-D1: more lands are "not useless", and with Cyclone down there is usually room) | the same land keep order |
| MANA: ritual | Seething Song | **1 whenever Cyclone is not out yet** (USER: "a big help for keeping mana up"; not conditional on the lands) | a **second Song is DEAD** (USER: "essentially useless"); once Cyclone is out, the one Song ranks with the extra lands |
| CYCLERS | Street Wraith, Curator of Mysteries (creature cyclers in hand) | 3 (USER: "1-3 cycling cards, preferring Street Wraith") | Curator before Wraith, ALWAYS (USER: "Always keep Wraith over Curator except in the low-life case, but that is pretty much nonexistent in goldfish"); a Wraith with life <= 2 (cannot be cycled) counts as dead |
| DIG / SETUP | Brainstorm, Brainsurge, Ponder, Long-Term Plans (when it is not standing in for the Cyclone) | **ALWAYS 2**, before and after Cyclone (USER Q-D2: "We should always keep 2. Mainly because the dig sets up cyclone cycles, but it also helps get us get more cyclers and dump useless cards") | Fill order: **at least ONE put-back spell: slot 1 = the best of Brainstorm > Brainsurge > LTP. Slot 2 = the best remaining of Brainstorm > Ponder > Brainsurge > LTP** (Brainstorm and Ponder are the top priorities). Example: Ponder, Ponder, Brainsurge keeps Ponder + Brainsurge. VARIANT (USER: "We could potentially prioritize higher the Brainstorm/Brainsurge effects after cyclone is out, but having a ponder is not a bad thing either"): after Cyclone, slot 2 = Brainstorm > Brainsurge > Ponder > LTP. That is measured as an A/B arm, not chosen here. Overflow sheds the reverse of the fill order |
| PAYLOAD | Emrakul, Ulamog the Ceaseless Hunger, Ulamog the Infinite Gyre (in hand) | **one Emrakul, kept whenever every crucial (quota) piece still fits; never at the expense of one** (USER 2026-10-28: "essentially we keep it if we can fit all of the other crucial pieces in hand, but it isn't an absolute requirement. We can technically win without it being in hand. Because we can blind cycle or find it with a ponder etc."; Q-D4: "it is fine to drop Emrakul unless you happen to have space in hand"). So it ranks ABOVE every non-crucial card (Remand, extra lands, spare cyclers, spare dig) and is the FIRST card shed once only quota cards are left. The earlier "promote it with a put-back spell in hand" is subsumed: it is already above everything non-crucial | **Emrakul is kept over both Ulamogs** (USER: "Emrakul is actually better"); a SECOND Eldrazi is kept too, lower (USER 2026-10-28: "I would keep a second after extra 2 lands and Remand. Maybe even before remand. We don't need more than 2, but 2 can be useful if we can set them up"); a third is never kept. The line it serves (USER): *"Brainstorm -> Cycle Cycle would be a pretty good play with two Eldrazi to drop on top."* Which ones: Emrakul > Ulamog the Ceaseless Hunger > Ulamog the Infinite Gyre (the reverse of the unknown-top discard order); see the known-top rule below |
| REMAND | Remand | none | shed fairly early (USER Q-D3: "I would discard Remand fairly early on when we can only cycle it") |
| DEAD | Toxic Deluge (goldfish_inert), a surplus Cyclone, a second Song | 0 | always first |

### Global shed order of the overflow (most expendable first)

1. **Dead:** Toxic Deluge, then a surplus Cyclone, then a second Seething Song.
2. **Eldrazi beyond two:** Ulamog the Infinite Gyre, then Ulamog the Ceaseless Hunger, then Emrakul. The known-top
   rule can reorder this tier.
3. **Lands beyond the quota plus 2.**
4. **The second Eldrazi** (default: below Remand in keep priority). VARIANT arm: it is kept over Remand, so it sheds
   after Remand ("maybe even before remand").
5. **Remand.**
6. **The 2 extra lands** (the one Song joins them once Cyclone is out).
7. **Surplus creature cyclers** (Curator before Wraith).
8. **Dig beyond its quota of 2** (the reverse of the fill order).
9. **The first Eldrazi** (Emrakul when held): the last non-crucial card, shed only when the alternative is a quota
   card.

When every card left is within quota (an eight-card hand of needed cards), shed in reverse importance: the first Eldrazi has already gone (tier 9),
then dig beyond the put-back slot, then lands beyond the minimum, then the one Song, then cyclers. **The last Cyclone (or Long-Term
Plans standing in for it) is never shed while any other card is in hand.**

The cantrip scorer's tiers (Dead 0 < Eldrazi 10 < CyclerSurplus 15 < LandSurplus 20 < Song 25 < Remand 30 <
Selection 60 < pieces) are re-aligned to this order, so the put-back and the discard stay one model. The changes:
- Remand sits between the far-surplus lands and the 2 extra lands.
- The extra lands rank below the cyclers and the dig.
- The dig quota of 2 holds after Cyclone.
- One Myriad and Steam Vents are preferred.
- One Eldrazi (Emrakul first) is kept above every non-crucial card, and a second just below Remand.
- LTP takes the Cyclone's protection while no Cyclone is found.

### The known-top rule (discard is sometimes a SHUFFLE)

Discarding Emrakul or Ulamog the Infinite Gyre shuffles the graveyard into the library. That puts the card back
where the walk can find it, and it **shuffles the whole library**. USER Q-D5: *"we need to be a bit careful with
discarding Eldrazi that can mess up our topdeck."* So, by what we legitimately know about the top:
- **Top is a known SETUP.** This means we put a creature or Eldrazi back this turn with Brainstorm or Brainsurge,
  Ponder kept one on top, or Long-Term Plans placed Emrakul third. A shuffling Eldrazi is shed only after every
  non-shuffle card in tiers 1-4. The shuffle destroys the setup.
- **Top is known JUNK.** We put back cards meant to be shuffled away (the user's junk-top plan). A shuffling Eldrazi
  that is not kept (the Gyre first, then a surplus Emrakul) is a free shuffle, so it ranks first in tier 2, ahead of
  the Ceaseless Hunger.
- **Top is unknown.** The tier-2 order stands: **the Gyre, then the Ceaseless Hunger**, then Emrakul. USER
  2026-10-28: *"The Ceaseless Hunger should be discarded after The Infinite Gyre if we don't know anything about how
  this will impact our topdecks. The shuffle into library is minor, but still a real positive if we end up cycling
  blindly."* Emrakul is still kept last, ahead of both Ulamogs, simply because it is the better card (USER: *"It is
  just that Emrakul is actually better."*).

"Known" means legitimately known only, never the engine's library order. This reads the known-top tracking that
the prunes agent is building (Phase A). Until that lands, the proxy is `top_stacked_turn == this turn` plus the
recorded stacked card.

### Width

The rule returns ONE index, per the Stage 5i doctrine: the rule is the decision, and a single entry skips the
executor's trial fan. The one exception worth searching is the user's "options that are vastly different" case: a
shuffle shed (Emrakul / Gyre) against a non-shuffle shed, with the top known. Lever
`MTG_CYCLONE_DISCARD_SHUFFLE_PLAN` (default OFF) returns those two plans instead of one. The search decides; each
plan's card is the bucket rule's pick.

## Build (after the running agents land; DecisionProviders.cpp is in use)

- `CycloneProvider::CleanupDiscardCandidates` builds the order from a `CycloneGoOff` census, routes it through
  `CleanupDiscardRankingWithOrder` (so the staged-card and required-piece protections stay engine-enforced) and
  returns `resize(1)`.
- `CleanupDiscardShedStable()` returns false: quotas change after each shed, so the rule is asked again per shed.
- Lever `MTG_CYCLONE_BUCKET_DISCARD` with a heurarm slot, so the A/B arms pool in one batch. It is **default OFF
  until the USER approves**; `=0` restores the generic rule.
- Lockstep comes for free: the executor's `ChooseDiscard` and the rollout cleanup both call this hook. Human play
  is unchanged (the human chooses).
- The put-back tie-break (`PutBackRanking` reads the shed order) moves with it. These are paired heuristics, so
  they are measured JOINTLY, and the report says how much of the change is the put-back.
- Unit tests:
  - the sole Cyclone is never shed;
  - one Emrakul is kept over Remand / extra lands / spare cyclers and dig, and shed before any quota card;
  - the Emrakul shed is first under a junk top and last under a setup top;
  - Tarn is the last land shed;
  - the shed is re-asked after each card.

## Validation

1. **Bound the stakes first:** generic vs its inverse (`MTG_SHED_WORST`), one pooled batch. If best-vs-worst is ~0,
   the change is doctrine plus rollout fidelity only, and that is the report.
2. **Labels:** re-run the Stage 4 evidence config. Every rule-worse decision must classify as churn or
   clairvoyance, never as a visible-information miss.
3. **A/B on fresh held-out seeds** (d3 b10 x400, d5 b20 x200), one pooled batch, five arms: generic / buckets / buckets + the post-Cyclone put-back-first dig variant (Q-D2) / buckets + the second Eldrazi above Remand /
   buckets + the shuffle plan.
4. **Classify every slower game** with the known-top blind mode: clairvoyance loss, actual line lost, or
   butterfly.
5. Units are reported too. This should be cheaper: no trial fan, and the rollouts stop shedding the engine.

## USER rulings (2026-10-28)

All six questions are answered and folded into the tables above.
- **Q-D1:** 4 lands on board; more is "not useless" but ranks below the cyclers and Brainstorm / Brainsurge.
- **Q-D2:** always keep 2 dig spells. Prioritising the put-back effects after Cyclone is a possible variant, A/B'd.
- **Q-D3:** 2 extra lands, then Remand, then more lands.
- **Q-D4:** keep one Emrakul whenever every crucial piece fits, never at their expense (it can be won without:
  blind cycling, or Ponder finds it).
- **Q-D5:** careful with shuffling discards; keep Emrakul over the Ceaseless Hunger. Follow-up: with the top unknown,
  the Gyre is discarded BEFORE the Ceaseless Hunger (the shuffle back into the library is a small real plus for blind
  cycling).
- **Q-D6:** Steam Vents is kept ahead of every land but the fetches and the first Myriad.

Earlier the same day: Myriad (at most 1 kept), Song (1 until Cyclone is out, a second is dead), Wraith over Curator
always, LTP takes the Cyclone slot, the dig fill order. *"The rest of the discard bucketing looks sensible."*

## Open (agent readings, for the user to confirm)

- After Cyclone is out, the one Song ranks with the 2 extra lands.
- With no Emrakul in hand, the first Eldrazi slot goes to the best Ulamog (the Ceaseless Hunger over the Gyre),
  under the same rule as Emrakul.
- The second Eldrazi is kept unconditionally, not only when a put-back spell is held ("2 can be useful if we can
  set them up").
