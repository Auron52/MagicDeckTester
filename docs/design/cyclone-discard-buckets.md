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

## USER rulings on the open readings (2026-10-29)

- **Song after Cyclone:** *"Let's rank all Seething Song where the 2nd copy is."* Once Cyclone is out, EVERY Song is
  dead-tier, shed right after Toxic Deluge and a surplus Cyclone. Before Cyclone, one Song is kept (unchanged).
- **The first Eldrazi slot:** *"We prefer Emrakul, then the Ceaseless Hunger which doesn't shuffle in, then Gyre."*
  Confirmed as built.
- **The second Eldrazi:** *"The second Eldrazi can be kept even without brainstorm and such if there is nothing better
  ... Maybe we could throw it if we have none of those but multiple cyclers and the Eldrazi is a type that shuffles to
  increase our odds of hitting when blind cycling."* It is kept unconditionally, as built. The USER calls the exception
  "a bit arguable", so it is its own sub-arm, `MTG_CYCLONE_DISCARD_ELDRAZI2_SHUFFLE_EARLY` (default OFF). It applies when
  there is no put-back effect (Brainstorm / Brainsurge) in hand, two or more live creature cyclers are in hand, and the
  second Eldrazi shuffles the graveyard in (Emrakul, the Infinite Gyre). Under those conditions that Eldrazi sheds with
  the Eldrazi beyond two, ahead of the far lands. That placement is the agent's reading of "throw it".

## Open (agent readings, for the user to confirm) -- superseded by the rulings above

- After Cyclone is out, the one Song ranks with the 2 extra lands.
- With no Emrakul in hand, the first Eldrazi slot goes to the best Ulamog (the Ceaseless Hunger over the Gyre),
  under the same rule as Emrakul.
- The second Eldrazi is kept unconditionally, not only when a put-back spell is held ("2 can be useful if we can
  set them up").

## As built (2026-10-29, branch `cyclone-discard`; DEFAULT OFF, the USER's adoption call)

Code: `src/ai/DecisionProviders.{h,cpp}` (CycloneProvider overrides + the shared model), `src/ai/HeuristicArm.h`
(six per-job slots), unit tests `test/unit/test_cyclone_discard_buckets.cpp`.

- **The shared model:** `CycloneBucketRankOf(state, me, opts)` returns every hand index's bucket tier
  (`CycShedTier`, in shed order) and the full shed order. It reads only the hand, our battlefield, life and the
  legitimately known top. Named and exposed in `DecisionProviders.h` so the cantrip scorer (and the prunes agent's
  put-back plans, when they land) read the same ranking. `CycloneKnownTopOf` is the known-top reading, and
  `CycloneBucketOptsNow` gives the flags' reading.
- **The discard:** `CycloneProvider::CleanupDiscardShedOrder` routes the bucket order through
  `CleanupDiscardRankingWithOrder`, so the staged-card and required-piece protections stay engine-enforced.
  `CleanupDiscardCandidates` is that order with `resize(1)`. `CleanupDiscardShedStable()` is false while the lever is
  on, so the rule is asked again per shed. The put-back tie-break (`PutBackRanking`) reads the full shed order, so it
  moves with the discard.
- **Levers** (heurarm slots, so every arm pools in one batch):

| lever | default | what it does |
|---|---|---|
| `MTG_CYCLONE_BUCKET_DISCARD` | OFF | the bucket discard (everything below is inert without it) |
| `MTG_CYCLONE_DISCARD_DIG_PUTBACK_FIRST` | OFF | Q-D2 variant: after Cyclone, dig slot 2 = Brainstorm > Brainsurge > Ponder > LTP |
| `MTG_CYCLONE_DISCARD_ELDRAZI2_OVER_REMAND` | OFF | the second Eldrazi is kept over Remand |
| `MTG_CYCLONE_DISCARD_ELDRAZI2_SHUFFLE_EARLY` | OFF | the USER's "arguable" exception: with no put-back effect in hand and 2+ cyclers, a shuffling second Eldrazi sheds with the Eldrazi beyond two |
| `MTG_CYCLONE_DISCARD_SHUFFLE_PLAN` | OFF | with a known top: two candidates, the rule's pick plus the best shed of the other kind (shuffle vs non-shuffle) |
| `MTG_CYCLONE_BUCKET_KEEPNEED` | OFF | "one model": the scored put-back's keep needs come from the bucket tiers (`CycloneBucketKeepNeed`) instead of the `kCyc*` tiers |
| `MTG_CYCLONE_BUCKET_PUTBACK_TIEBREAK` | ON under the lever | decomposition only: `=0` keeps the GENERIC shed order as the put-back tie-break while the discard uses the buckets |

- **The tier numbers** (shed first = lowest): dead 10, Eldrazi beyond two 20, lands beyond quota + 2 30, second Eldrazi
  40 (45 = a shuffling Eldrazi under a known SETUP top), Remand 50 (55 = second Eldrazi under the variant), the 2 extra
  lands 60, surplus cyclers 70, surplus dig 80, first Eldrazi 90.
  Then the quota cards: dig slot 2 100, quota lands 110, the Song 120, cyclers 130, dig slot 1 140, the minimum land
  150, and the engine 200.
- **KEEPNEED mapping:** the go-off pieces keep their `kCyc` values: lands 300 (the minimum land 310), Song 300, cyclers
  250 / 240, engine 1000. These are all >= `kCycPiece`, so the scorer still voids the go-off when one of them is put
  back below the walk. The dig stays below `kCycPiece` (75 / 70), because it is not a go-off piece. The non-crucial
  tiers are re-spaced into the bucket order: first Eldrazi 65, surplus dig 50, surplus cyclers 35, extra lands 25,
  second Eldrazi (over Remand) 22, Remand 20, second Eldrazi 12, far lands 8, Eldrazi beyond 5, dead 0.

### Open readings, taken at the doc's default (or the agent's), for the USER

1. **RULED 2026-10-29:** after Cyclone is out, every Song is dead-tier. The first A/B batch below measured the
   pre-ruling reading: the one Song ranked with the 2 extra lands and shed first among them.
2. **RULED (confirmed):** with no Emrakul in hand, the first Eldrazi slot goes to the Ceaseless Hunger over the Gyre.
3. **RULED (confirmed):** the second Eldrazi is kept unconditionally. The USER's exception is the sub-arm
   `MTG_CYCLONE_DISCARD_ELDRAZI2_SHUFFLE_EARLY`.
4. **"Lands beyond the minimum"** in the all-quota shed order: the minimum is ONE land, the next drop (agent). The
   order of the quota cards is: dig slot 2 < the quota lands beyond the minimum < the Song < the cyclers (Curator
   first) < dig slot 1 < the minimum land < the engine.
5. **Myriad net of the battlefield:** a Myriad already on the battlefield makes any hand Myriad the "second" (agent).
6. **"The other lands by colour need":** the board is short on red with fewer than two red sources, and short on blue
   with none. A needed red ranks first, then a needed blue. A filter land (Cascade Bluffs) ranks after the lands that
   make a colour themselves. "Red covered first": when the board is short on red and no land inside the quota is
   red-capable (it makes red, or it is a fetch that finds a Mountain; Myriad does not count), the best red-capable land
   takes the last quota slot (agent).
7. **The lands count** is every land on our battlefield, fetches and Myriad included (agent).
8. **Known top:** today's proxy is a deliberate top stack THIS turn whose card is still on top. A creature there is a
   SETUP; anything else is JUNK. In this deck the proxy is **never set**: only a `tutor_to_top` resolution writes
   `top_stacked_turn` (SpellEffects.cpp PerformTutor), and Long-Term Plans is `tutor_library_position` 3 (not a top
   stack). So the known-top rule and `MTG_CYCLONE_DISCARD_SHUFFLE_PLAN` are built and unit-tested, but they are inert
   in play until the known-top tracking (the prunes agent's Phase A) lands.
9. A card the model does not name is kept just above Remand (none exists in the list).

## Measurement (2026-10-29)

### Identity with the lever off

- **Smoke** (`THREADS=6 MTG_PGO=0 bash test/regression.sh --smoke`, build 86d5c5192): **149 / 149 ALL PASS, searched
  play-changed 0, d0 play-changed 0.**
- **Cyclone** is not yet a suite deck, so smoke cannot see it. It was checked against the pre-change HEAD binary
  (9b384907d, sha 19d45226), using the same manifest for both, an unbounded plan cache, and one thread each:
  - d5 b20 x8: job digest `54f2bf49dc1943b1` on both;
  - d3 b10: 12 games, every one identical in win turn and work units.
  - That is 20 / 20 games identical. The probe was stopped there, at 12 of its 16 d3 games, to free the threads.
- **Across builds:** the A/B's GEN arm on the ruled build (de0b9cfb7) reproduced the 25 GEN d5 games of the first
  (stopped) batch, which ran on 86d5c5192, game for game.
- **Tests:** unit 2115 / 2115 and scenarios 333 / 333 on both builds. On the integration tree (cyclone-int b991ec733
  plus these commits) the unit suite is 2128 / 2128.

### The A/B (held-out, paired, ONE pooled batch)

- **Setup:** one frozen binary (de0b9cfb7, sha ea2011ec), 8 threads, `MTG_PLAN_CACHE_KB=4000000`, profile attached.
  The arms are per-job flags, interleaved in 25-game chunks.
- **Seeds:** FRESH seeds 111000 (d3 b10 `--ignore-play-profile`, x100) and 112000 (d5 b20 at the play settings, x50).
- **Arms:**
  - GEN: lever off;
  - BKT: the buckets with the USER's rulings;
  - DIG: + the post-Cyclone put-back-first dig;
  - EL2: + the second Eldrazi over Remand;
  - E2S: + the USER's "arguable" shuffle-early second Eldrazi;
  - KN: + the one-model keep needs.
- **Reading the table:** delta = arm - GEN in average win turn (unwon = 9; + = the arm is slower). Units are work units
  per game. Wall is per-game thread-ms; it is unreliable on this box, with load 35-45 from other agents, and the GEN
  arm's d5 games happened to run in the heaviest stretch.
- **Size and the restart:** this is SMALLER than the design's x400 / x200. A first batch at x200 / x100 (seven arms)
  was stopped after 35 minutes on the coordinator's instruction, because the USER's rulings changed the BKT rule. It
  was restarted at a size that fits before the generation freeze. Its 25 completed d5 games per arm are kept in
  `build/scratch/cyclone/discard/ab_v1_stopped/` and are not used below.

| arm | d3 avg | d3 delta vs GEN (se) | d3 slower / faster | d5 avg | d5 delta vs GEN (se) | d5 slower / faster | units d3 / d5 vs GEN |
|---|---|---|---|---|---|---|---|
| GEN | 5.300 | -- | -- | 5.000 | -- | -- | -- |
| BKT | 5.190 | **-0.110 (0.071)** | 10 / 16 | 5.080 | **+0.080 (0.069)** | 8 / 4 | x0.997 / x0.955 |
| DIG | 5.190 | -0.110 (0.071) | 10 / 16 | 5.080 | +0.080 (0.069) | 8 / 4 | x0.997 / x0.955 |
| EL2 | 5.120 | **-0.180 (0.076)** | 9 / 20 | 5.080 | +0.080 (0.069) | 8 / 4 | x0.978 / x0.968 |
| E2S | 5.190 | -0.110 (0.071) | 10 / 16 | 5.080 | +0.080 (0.069) | 8 / 4 | x0.997 / x0.955 |
| KN | 5.230 | -0.070 (0.077) | 16 / 18 | 5.060 | +0.060 (0.072) | 8 / 5 | x1.015 / x0.959 |

Against BKT, the sub-arms behave as follows:
- **DIG and E2S are byte-equal to BKT in all 150 games.** Their conditions (Cyclone out with 3+ dig spells at a shed;
  no put-back spell with 2+ cyclers and a shuffling second Eldrazi) never changed a shed that mattered.
- **EL2 vs BKT:** d3 -0.070 (se 0.033), 0 slower / 5 faster; d5 identical.
- **KN vs BKT:** d3 +0.040 (0.045), 9 / 5; d5 -0.020 (0.053), 3 / 4.

**Where the change acts.** The bucket rule moves three things:
- the real cleanup shed;
- the search's MODEL of every future cleanup (the rollouts' shed);
- the put-back tie-break order.

It removes nothing from the candidate set of any in-window search decision. The one decision it narrows is the real
cleanup itself, where GEN's executor trials the whole hand and BKT takes the rule's single pick (the 5i doctrine).

### The 18 BKT-slower games, root-caused

**Method.**
1. Each game was replayed as a single game for both arms with full logs and `MTG_TRACE=discard`, on the A/B binary.
   All 18 reproduce the batch's win turns. The first divergence of the two action logs was found, and the REAL
   cleanup sheds (CLEANUP-phase `DISCARD`) were listed.
2. Each game then ran a **known-top BLIND** ensemble. This is the prunes branch's `MTG_SEARCH_BLIND_UNKNOWN`: every real
   decision searches a copy whose UNKNOWN library positions are re-randomised. Put-backs, Ponder keeps and LTP's third
   position stay known. It ran on the integration build (cyclone-int b991ec733 + these commits, sha 9db0823b), salts
   1-2, both arms.
3. Verdict rule: a game is **(b) clairvoyance** when BKT's blind mean is <= GEN's. Two salts is a thin ensemble; read
   the per-game numbers, not the label alone.

| depth gi | win turn BKT / GEN | first divergence | real cleanup sheds BKT / GEN | blind BKT / GEN | class |
|---|---|---|---|---|---|
| d3 4 | 6 / 5 | T1 Ponder: keep (BKT) vs shuffle (GEN) | - / - | 9,9 / 9,6 | (c) butterfly |
| d3 33 | 5 / 4 | T3 GEN cycles Street Wraith, BKT does not | - / - | 7,7 / 7,6 | (c) butterfly |
| d3 42 | 7 / 5 | T2 land: Mountain vs Myriad | - / - | 8,9 / 9,9 | (b) |
| d3 45 | 5 / 4 | T3 draw order after the Brainsurge put-back | T3 surplus Cyclone / **T3 Emrakul** | 5,7 / 7,6 | (b) |
| d3 52 | 8 / 7 | T2 cycle vs draw | T4 Deluge / **T4 Emrakul, T5 Gyre** | 9,9 / 9,9 | (b) |
| d3 56 | 6 / 5 | T3 Brainsurge vs LTP | T4 second Myriad / - | 9,9 / 9,9 | (b) |
| d3 57 | 6 / 5 | **T1 GEN skips its land drop to shed Emrakul at cleanup** | - / **T1 Emrakul** | 9,9 / 9,9 | (b) |
| d3 58 | 6 / 5 | T1 land: Xander's Lounge vs Steam Vents | - / - | 8,6 / 7,7 | (b) |
| d3 70 | 7 / 6 | T5 land vs draw | T5 second Emrakul, T6 Remand / - | 9,8 / 9,9 | (b) |
| d3 93 | 5 / 4 | T4 land vs draw | T4 Remand / - | 6,9 / 9,9 | (b) |
| d5 14 | 5 / 4 | T1 Ponder: keep vs shuffle | T4 second Emrakul / - | 7,5 / 6,5 | (c) butterfly |
| d5 15 | 4 / 3 | T1 land: Steam Vents vs Scalding Tarn | - / - | 5,9 / 4,9 | (c) butterfly |
| d5 21 | 5 / 4 | T1 Brainstorm vs cycle Street Wraith | T4 Deluge / - | 9,9 / 9,9 | (b) |
| d5 24 | 5 / 4 | T1 Ponder order | - / - | 8,7 / 6,6 | (c) butterfly |
| d5 25 | 6 / 5 | T1 Ponder vs cycle Curator | T3 LTP, T5 Remand / - | 9,9 / 7,9 | (c) butterfly |
| d5 33 | 5 / 4 | T1 | - / - | 9,5 / 5,9 | (b) |
| d5 34 | 8 / 7 | T1 Brainstorm vs Ponder | T5 Deluge, T6-7 two surplus Cyclones / T5 Gyre, T6 Wraith | 9,9 / 9,9 | (b) |
| d5 39 | 7 / 6 | T3 land: Island vs Myriad | - / T5 Hunger | 7,8 / 8,9 | (b) |

**Reading.**

- **(b) clairvoyance: 12 of 18.** These include all three games where GEN's faster line was built on a shed:
  - The generic max-MV rule sheds Emrakul (MV 15). Emrakul's graveyard shuffle puts it back into the library, and the
    clairvoyant search knows where it lands. d3 57 is the extreme case: GEN skips its T1 land drop to keep 8 cards and
    pitch Emrakul at cleanup.
  - Under the blind, these lines do not hold up (d3 57: 9,9 vs 9,9).
  - So the USER's Q-D5 caution about shuffling Eldrazi is borne out. The bucket rule keeps the one Emrakul, and what
    it gives up there is clairvoyance.
- **(c) budget butterflies: 6 of 18** (d3 4, 33; d5 14, 15, 24, 25).
  - Each first divergence is a T1-T3 root decision (Ponder keep vs shuffle, cycle or not, which land, which cantrip).
    It is taken among the SAME candidates, before either arm's real cleanup differs. The search's model of later
    cleanups (the rollouts' shed) and the put-back tie-break are what move it.
  - They stay slower under the blind by a turn or less on a two-salt mean, so they are not recognisable as
    clairvoyance. They are not lost lines either: the bucket rule removes nothing from any in-window candidate set.
  - **Not run:** a large-budget recovery check (`--depth 8 --budget-ms 0` is intractable on these games; ledger Q44).
- **(a) lost line: 0. (d) rule to amend: 0.** Every real BKT shed in these games follows the USER's rules:
  - the second Emrakul is shed below Remand (d3 70, d5 14 -- the default reading; EL2 would shed Remand first);
  - LTP is shed as dig beyond two, with a Cyclone in hand (d5 25);
  - the second Myriad is shed as a land beyond quota + 2, with the Song kept (d3 56);
  - the dead surplus Cyclones and Toxic Deluge go first.

### The d5 extension (the arms that move)

After the first d5 x50, every bucket arm read +0.080 (se 0.069). The same 8 slower games drove that reading, and all
8 are classified above. So GEN / BKT / EL2 got 100 more d5 games: FRESH seeds 112050-112149, the same binary, one
pooled batch, 9 threads. Combined with the first 50, that gives d5 x150:

| arm | d5 avg (n = 150) | delta vs GEN (se) | slower / faster | units vs GEN |
|---|---|---|---|---|
| GEN | 5.207 | -- | -- | -- |
| BKT | 5.180 | **-0.027 (0.038)** | 14 / 18 | x0.970 |
| EL2 | 5.161 (n = 149) | **-0.040 (0.038)** | 13 / 19 | x0.966 |

EL2 vs BKT at d5: -0.013 (se 0.010), 0 slower / 2 faster. The six new BKT-slower games were classified by mechanism
only; a d5 blind ensemble costs about 200-600 s a game, more than the time left:
- d5 53: T2 cycle vs draw; no sheds.
- d5 58: T4 draw order; BKT then sheds a surplus Cyclone.
- d5 81: T1 Tarn vs Myriad.
- d5 83: T3 draw vs Brainstorm.
- d5 111: T1, the put-back itself. BKT's tie-break puts back [Remand, Remand] where GEN puts back [Blood Crypt, the
  Hunger]. These are the same candidates in a different order. EL2 is FASTER than GEN on this game.
- d5 132: T1 Ponder keep vs shuffle.

All six are (c): a root decision among unchanged candidates, before any real cleanup differs. None is (a) or (d).

## Verdict against the USER's adoption bar (2026-10-29)

**(1) Quality: net <= 0 at d3 AND d5, and better in aggregate.**

| arm | d3 (n = 100) | d5 | pooled d3 + d5 | (1) |
|---|---|---|---|---|
| BKT | -0.110 (se 0.071) | -0.027 (se 0.038, n = 150) | -0.060 over 250 | passes |
| **EL2** | **-0.180 (se 0.076)** | **-0.040 (se 0.038, n = 149)** | **-0.096 over 249** | **passes; the best arm** |
| DIG, E2S | identical to BKT | identical to BKT at n = 50 | | inert at this sample |
| KN | -0.070 (se 0.077) | +0.060 (se 0.072, n = 50) | | FAILS at d5 (not extended) |

The d3 effect of EL2 is about 2.4 se. Every d5 effect is within 1.1 se, so "<= 0 at d5" holds on the point estimate,
not with significance.

**(2) No slower game is a lost line that is not recognisable as clairvoyance.**
- 24 BKT-slower games: 18 from the main batch plus the 6 from the d5 extension.
  - 12 are clairvoyance under the known-top blind.
  - 12 are budget butterflies: a root decision among unchanged candidates, moved by the rollout's cleanup model or the
    put-back tie-break.
  - 0 are lost lines, and 0 are rule defects.
- EL2's slower games are a strict subset of BKT's (21 of the 24). EL2 is never slower than BKT.
- The caveats:
  - The blind ensemble is 2 salts.
  - The 6 extension games have no blind run.
  - No large-budget recovery check was run (intractable on Cyclone; ledger Q44).

**(3) No performance cost.**
- Work units per game: BKT x0.997 (d3) / x0.970 (d5); EL2 x0.978 / x0.966.
- Wall is unmeasurable on this box (load 35-45). On a matched reading it is no worse.

**Recommendation (the USER decides):** adopt **EL2**, i.e. `MTG_CYCLONE_BUCKET_DISCARD=1` +
`MTG_CYCLONE_DISCARD_ELDRAZI2_OVER_REMAND=1`, the USER's own "maybe even before remand".
- It passes all three criteria on this sample.
- DIG, E2S and KN stay OFF:
  - DIG and E2S never changed a game;
  - KN failed d5.
- A separate commit flips EL2's two defaults ON, so the integrator can take or drop it.

What would firm this up: the same A/B at the design's x400 / x200, which is a ~6 h pooled run on a quiet box.
