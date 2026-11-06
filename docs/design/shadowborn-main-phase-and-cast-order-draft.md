# Shadowborn Apostle: per-card main phase + full cast order (DRAFT for the USER)

Status: **DRAFT, nothing adopted, no engine or play change.** Main-phase placement and cast order are
USER-owned decisions (CLAUDE.md). This page is the proposal for you to edit. Nothing ships until you
answer the questions in section 6.

USER, 2026-11-04 (verbatim): *"For 4, yes let's draft one up. If we don't have an order we should
make that as well."* Item 4 was this: the search offers every card in BOTH the pre-combat and the
post-combat main. That doubling is the largest search cost left on this deck. The s7 study measured
one T4 leaf turn at 4,393 first-main candidates against 8,242 second-main ones
(`analysis-Shadowborn Apostle.md`, "s7 budget miss"). `MTG_NO_M2_SOLVE=1`, used there only as an
upper bound, halved that pass (148k to 73.8k units). Fungus avoids the doubling because you assigned
each of its cards to one main phase.

## 0. What the engine does today

- `ShadowbornProvider` does not opt into any main-phase split. It has no `ClassifiesMainPhases`, no
  `MainPhaseOverride`, no `RestrictsMain2Casts` and no `RestrictsCollectedActions`.
  `DeckUsesSecondMain` is true (keyed on the Apostle's `sac_named_count`), so **every cast, every
  suspend, every activation and the land drop are offered in both mains**, in the search, the rollout
  and the executor.
- The only per-phase rule is on the Apostle ACTIVATION (`SacNamedTimingFilter`, the USER-confirmed
  INT-9 table). On an attack turn, Jetmir is offered in main 1 only, and only when it raises this
  turn's damage (lethal first). Otherwise the Overlord is offered in main 2 only. On a sick turn the
  list is Overlord then Jetmir, main 1 only. The chain exception keeps Jetmir offered in main 1 while
  a chain can still add bodies.
- Cast order: `ShadowbornProvider::CastOrderRank` overrides one card. A ripple granter (Thrumming
  Stone) gets rank 1. Every other card keeps the generic rank, and the payable-order fallback
  (`MTG_PAYABLE_ORDER`, default on) re-sequences a plan whose declared order cannot be paid, for
  example rituals before the Stone. This is the order `mtg --cast-order-report` prints for the
  shipped profile:

  | rank (today) | cards | tie inside the rank |
  |---|---|---|
  | land | the land drop (first at d0; searched, folded into the plan at depth > 0) | -- |
  | 1 | Thrumming Stone | -- |
  | 10 (creature) | Shadowborn Apostle, Jetmir, Reaper, Sire, Abhorrent Overlord | **plan order** (cheapest-first applies only to rituals) |
  | 15 (ritual) | Culling the Weak, Dark Ritual | cheapest-first, both MV 1, so **plan order** |
  | 20 (other) | Lotus Bloom, Profane Tutor (both suspend-only), Demonic Tutor, Diabolic Intent | **plan order** |

  So today the order is Stone, then Apostles, then rituals, then tutors and Intent. The Apostle
  comes before Dark Ritual, which is the opposite of every one of your reference games (section 2).
  The Demon ranks and the tutors are not ordered among themselves. The deck does not opt into
  `CastOrderTiesByName` / `CastOrderTotalByName`.

### Evidence

**Your 10 references** (`references/Shadowborn_Apostle/claude_s1..s10`, extracted with
`logs/sb-mainphase/ref_phases.py`): **every play is in main 1, and main 2 was never used.** That is
27 Swamps, 6 Lakes, 3 Blood Crypts, 24 hand-cast Apostles, 10 Stones, 9 Dark Rituals, 3 Demonic
Tutors, 3 Diabolic Intents, 2 Lotus Bloom suspends, 6 Lake cracks and 14 Apostle activations
(Overlord 5, Jetmir 9). Where you queued more than one cast, the order was:

| ref | turn | your queued order |
|---|---|---|
| s1 | T3 | Dark Ritual, Dark Ritual, Thrumming Stone, Apostle (the Stone needed the Rituals) |
| s9 | T2 | Dark Ritual, Dark Ritual, Thrumming Stone, Apostle (same) |
| s8 | T4 | Dark Ritual, Thrumming Stone, Apostle (same) |
| s5 | T5 | Dark Ritual, Thrumming Stone |
| s3, s4, s6 | T3 | (Lake) Thrumming Stone, **Dark Ritual**, Apostle (the Stone was payable without the Ritual, and the Ritual still went before the Apostle) |
| s2, s10 | T3 | (Lake) Thrumming Stone, Apostle |
| s7 | T4 | (Lake) Thrumming Stone, Apostle, suspend Lotus Bloom |
| s1 | T2 / s4, s5 T2 | Diabolic Intent alone (the victim was the T1 Apostle) |

**The search's own use of main 2** (my census: 100 games, d5 b20, seed 7001, shipped profile, 6
threads, avg 4.95; logs in `build/scratch/sb-mainphase/logs`, script
`logs/sb-mainphase/census.py`): main 2 is used rarely. **23 of 100 games** made any main-2 play.

| card | main 1 | main 2 (games) |
|---|---|---|
| Shadowborn Apostle (hand cast) | 259 | 17 (16) |
| Diabolic Intent | 46 | 5 (5) |
| Demonic Tutor | 29 | 1 (1) |
| Culling the Weak | 15 | 1 (1) |
| Apostle activation | 73 | 3 (3) |
| Dark Ritual 87, Thrumming Stone 90, Overlord 3, Reaper 6 (hard casts), every land drop | all | 0 |

Typical main-2 shapes: an Apostle cast on T2 after the T1 Apostle attacked, which ties with a main-1
cast. Diabolic Intent on T2/T3 sacrificing an Apostle that had just attacked, which is the "keep the
attack damage" harvest. Seed 7001 gi44 T2 is the same harvest with more cards: Culling, Intent and an
Apostle, all in main 2. The game log records no suspend or Lake-crack entries, so those two are not
counted.

## 1. Proposed main phase per card

**SUPERSEDED by the USER ruling below (2026-11-04).** The per-card "main 1 by default" table that follows is
kept for the record; the rule that will be built is this one.

USER 2026-11-04 (verbatim): *"Main 1 is usually okay I think. Realistically all of the good acceleration is to
land Thrumming Stone as soon as possible. The chip damage is rarely relevant, though you do make a good point
that perhaps it would make sense to play this a bit differently and do most things in main 2 instead. The only
cases main 1 is required are: 1. Sacrifice for Jetmir when you have attackers. 2. Drop a bunch of shadowborns
off Thrumming stone to enable 1. (note that this must not sacrifice all of the attackers) What it really comes
down to is that you don't need to do it main 1 unless you are deploying Jetmir with current-turn attackers. If
you aren't, then you do it main 2 instead."* and *"(or I guess if Jetmir is already out)"* *"Getting more Shadowborns to pump up his effect."* *"(would be a main 1 play)"* *"At the end of the day, though, all of the lines are connected to Jetmir so, if he isn't present or searched up, it should go in main 2."*

**The rule.** A main-1 plan (beyond the land drop) is admitted only on a **Jetmir turn**: this turn has
attack-ready creatures AND either (a) the plan deploys Jetmir (the Apostle activation), with at least one
attacker still on the battlefield after the plan's sacrifices (Culling, Intent, the six), or (b) Jetmir is
already on the battlefield, so every body added in main 1 raises its 3/6/9 count for this combat. On every
other turn, all casts, suspends and activations are made in main 2.

**Why it is sound (a dominance fold, default ON).** Off a Jetmir turn, nothing cast in main 1 changes this
combat: the Apostles and Harpies enter summoning sick, no anthem reads the count, and land / Lake / Lotus mana
is still there after combat. A main-1 sacrifice can only cost an attacker's damage. So the main-2 version of
every such plan is at least as good, and the fold removes the main-1 duplicate of the whole turn.

**Interactions.**
* The INT-9 Demon-timing table said "sick turn: Overlord then Jetmir, main 1 only". Under this rule a turn with
  no attackers is not a Jetmir turn, so that turn moves to main 2 (same result, the other main). The
  attack-turn rows (Jetmir main 1 when it raises this turn's damage; Overlord main 2) are unchanged and are
  exactly case (a).
* The land drop stays main 1 (earlier ruling), except a land fetched in main 2.
* Section 2's cast order applies inside whichever main the turn uses.
* **Build:** this is a PLAN-level test (it reads whether the plan fetches Jetmir and what survives its
  sacrifices), so the per-action `CollectedActionAllowed` hook is not enough; it needs a provider hook that
  admits or folds a whole main-1 plan, applied in the search, the rollout and the executor alike. Questions 1
  and 2 in section 6 are answered by this rule.

**Why main 1 is always at least as good** (the dominance argument the table relies on). In this
goldfish, combat creates nothing a later cast can use:

- No card in the list draws, untaps, counts damage dealt or has spectacle.
- No blocks happen, so morbid never fires in combat.
- Mana from lands and permanents (Lake crack, Lotus Bloom) is still there after combat, and
  floating mana empties between the phases.

So any (main-1 plan, main-2 plan) pair whose main-2 casts are paid from lands or permanents can be
played as ONE main-1 plan, with the main-2 casts added in rank order. That plan is at least as good,
and strictly better when the casts add bodies before Jetmir's 3/6/9 count is read in combat.

The pair is NOT always replaceable in two cases:

- **Spells whose additional cost sacrifices a creature** (Diabolic Intent, Culling the Weak). After
  combat they can eat a creature that has already dealt its damage. This is your own Second-main
  note: *"sacrificing Apostles AFTER combat keeps their attack damage"*.
- **The Apostle activation.** It is already governed by the Demon-timing rule.

| card (copies) | proposed | reason (card text + rulings) | your refs m1/m2 | search m1/m2 |
|---|---|---|---|---|
| Shadowborn Apostle (18), hand cast | **Main 1 only** | It is a sick 1/1 either way. In main 1 it raises Jetmir's count for this combat and can complete the six for a pre-combat Jetmir (the chain exception). The ripple chain and its reveals are the same in either main. | 24 / 0 | 259 / 17 |
| Thrumming Stone (4) | **Main 1 only** | It is a granter, so every later cast this turn wants it out first. Nothing about it depends on combat. | 10 / 0 | 90 / 0 |
| Dark Ritual (4) | **Main 1 only** | Its mana empties at the end of the phase. In main 2 it could only pay a {B} that the land paying for it could pay directly, so a main-2 Ritual never adds mana. | 9 / 0 | 87 / 0 |
| Demonic Tutor (1) | **Main 1 only** | It sacrifices nothing. In main 1 the fetched card can still be cast this turn, before combat. In main 2 it cannot, because casts are dropped there. | 3 / 0 | 29 / 1 |
| Diabolic Intent (4), hard cast | **Main 1; ALSO Main 2 when the victim would be an attacker** (see below) | The sacrifice is the only combat-sensitive part. When the deck's sacrifice order (Harpy > weakest Apostle > Overlord > Sire > Reaper > Jetmir) would hand main 1 a spent body (a Harpy token, or an Apostle that is summoning sick), main 1 dominates. When every victim could attack this turn, main 2 keeps that attacker's damage. | 3 / 0 | 46 / 5 |
| Culling the Weak (1) | **Main 1; ALSO Main 2 under the same victim condition** | It has the same sacrifice. Its {B}{B}{B}{B} in main 2 can pay only the activation's {B} and a main-2 Intent (seed 7001 gi44). | 0 / 0 | 15 / 1 |
| Abhorrent Overlord (1), hard cast {5}{B}{B} | **Main 1 only** | A hard cast sacrifices nothing. Its Harpies come in sick either way, and in main 1 they count toward Jetmir this combat. Your "Overlord main 2" rule is about the ACTIVATION (it eats six attackers), not this cast. | 0 / 0 | 3 / 0 |
| Reaper from the Abyss (1), hard cast | **Main 1 only** | It is a sick 6/6. Morbid is checked at the end step, which is the same in either main. | 0 / 0 | 6 / 0 |
| Sire of Insanity (1), hard cast {4}{B}{R} | **Main 1 only** | Its end-step discard is the same in either main. | 0 / 0 | 0 / 0 |
| Jetmir, Nexus of Revels (1) | **Main 1 only** (moot) | It cannot be cast (the list has no G/W source) and arrives only by the activation. | -- | -- |
| Profane Tutor (2), SUSPEND {1}{B} | **Main 1 only** | Suspending gives the same counters and the same arrival turn in either main, and its pin is chosen in main 1 anyway. The engine emits the pin only when `is_pre_combat`. | 0 / 0 | n/a |
| Lotus Bloom (1), SUSPEND {0} | **Main 1 only** | The result is identical in either main. | 2 / 0 | n/a |

**The victim condition for Intent and Culling in main 2**, stated as a test on the POST-combat state.
The cast is allowed in main 2 iff the one victim the deck's sacrifice order picks there is a
creature that was able to attack this turn: it was not summoning sick, and it is not a Harpy token
(Harpies are first in the order and enter sick). Erring wide is safe. A false "allowed" only
re-offers a main-2 line the search already compares. A false "dropped" could delete a line, which is
why the test reads summoning sickness and not "is tapped": Jetmir's vigilance at 3+ creatures leaves
attackers untapped.

The simpler alternative, **A**, puts every cast in Main 1 with no exception. It is not my
recommendation. A kill that depends on a post-combat sacrifice of an attacker would then be
unreachable at every budget, so it would fail the d8 b0 recovery bar, whatever its average.

## 2. Proposed full cast order (within a main phase)

**USER RULING 2026-11-04:** *"It should be land -> Dark Ritual -> other spells."* Dark Ritual is cast right
after the land drop, ahead of the Thrumming Stone and everything else; the table below includes it. Earlier the same day: *"Casting the Dark Ritual early in a line is totally
fine in my opinion. I sometimes cast it late, but that doesn't really matter at all."*

| rank | card | change vs today | why |
|---|---|---|---|
| land | the land drop (Lake of the Dead: tap-first, a basic Swamp before Blood Crypt, as today) | none | USER ruling: land first. |
| 1 | Dark Ritual | **moved to the front** (15 to 1) | USER ruling 2026-11-04. Also your practice in all 6 reference plans that held it. |
| 2 | Culling the Weak | **moved ahead of the Stone** (was 15, behind it) | USER 2026-11-04: *"Culling the weak needs to be earlier than Thrumming stone some of the time and that means the shadowborn for it to sacrifice also needs to be considered there."* *"(actually often)"* *"Having the culling later than Thrumming stone isn't a good idea."* *"So it should be Dark Ritual, <shadowborn sacrifice for culling if needed>, Culling, Thrumming stone..."* So Culling always precedes the Stone. Victim, USER 2026-11-04: *"If needed here is pretty much just so that you can cast culling when no shadowborn is out. If you have a good sacrifice already you can just use it and cast the apostle later. It's probably not worth worrying about the 1 damage from an attacker."* So: with a creature already on the board, Culling eats a board body and every Apostle waits for the Stone (the hand-victim variant is not offered); with no creature out, one Apostle from hand is cast first as its victim. |
| 3 | Thrumming Stone | one place later (was 1) | Granter: every later cast wants it out first. |
| 4 | Shadowborn Apostle | none in effect (10) | Starts the chain. Bodies go down before anything that sacrifices or counts them. |
| 5 | Reaper from the Abyss (hard cast) | **ordered** (tie at 10) | Body; no cast-order interaction. |
| 6 | Sire of Insanity (hard cast) | **ordered** (tie at 10) | Body; the discard is at the end step. |
| 7 | Abhorrent Overlord (hard cast) | **ordered** (was a plan-order tie at 10) | Last of the Demons: its devotion count includes the Apostles' {B} and the other Demons' pips. USER 2026-11-04: *"I guess technically Overlord should go last of the demons because of devotion, but this is such a minor detail and I doubt if it would ever come up by turn 8."* |
| 8 | Jetmir (cannot be cast) | **ordered** (tie at 10) | Listed only so the order is total. |
| 9 | Demonic Tutor | ordered (tie at 20) | After the bodies; the shuffle comes after the chain has used the top of the library. |
| 10 | Diabolic Intent | ordered (tie at 20) | Victim is a sick Apostle from this turn's chain. DT before Intent is an arbitrary tie-break (q6). |
| 11 | Profane Tutor (suspend) | ordered (tie at 20) | A suspend; position only decides which mana pays {1}{B}. |
| 12 | Lotus Bloom (suspend {0}) | ordered (tie at 20) | Costs nothing, so its position does not matter. |

Plus `CastOrderTiesByName` (or `CastOrderTotalByName`), so that no two different cards ever tie and
fall back to plan order. That follows your cross-deck ruling of 2026-10-14: *"Let's fully order the
cards in each list inasmuch as possible."* Mana activations (Lake crack, Lotus Bloom's sacrifice) are
not casts. The payer spends them as payments need, exactly as today.

## 2b. Where the activations go in the order (USER question 2026-11-04: *"Where do the activations go in the order? That is much more important."*)

**Today's engine.** Within one main, every cast of the plan runs first (section 2's order), then the plan's
activations in a trailing pass. A plan holds at most ONE Apostle sacrifice-six. A second one in the same main
is reached only through a continuation (breakpoint site 9), which re-opens when six more Apostles are on the
battlefield; that continuation can cast more before activating again. Which Demon each activation fetches
is a searched choice, so the Overlord-vs-Jetmir order between two activations is searched, not fixed.

**Your references.** Every activation came after that main's casts. In each of s3, s6 and s9 (T3) you
fetched the Overlord, then Jetmir, in the same main. s1, s10 (T4) and s8 (T5): land, Apostle(s), then one
activation. s2 T4: land, Apostle, Jetmir.

**USER 2026-11-04:** *"Overlord should go before any activations Shadowborn activations when it is cast. This
would only rarely come up, but you want to use the shadowborn devotion before they die."* Today's trailing
pass already does this inside a plan. The build must keep it true across a continuation: a hard-cast
Overlord is never left for after an activation in the same main.

**Proposed order within a main (draft):**

| step | what | why |
|---|---|---|
| 1 | land, Dark Ritual, [Apostle victim], Culling, Thrumming Stone, Apostles, Reaper, Sire, Overlord (hard cast), Demonic Tutor, Diabolic Intent, suspends | section 2 |
| 2 | Apostle activation fetching **Abhorrent Overlord** | Its devotion counts the Apostles still alive; doing it before the Jetmir sacrifice keeps the second six alive for it (more Harpies), and the Harpies then count toward Jetmir's 3/6/9. Jetmir has no {B}, so fetching it first only lowers devotion. |
| 3 | Apostle activation fetching **Jetmir** | Last before combat, on a Jetmir turn (section 1), leaving at least one attacker. |
| 4 | more Apostles (continuation) | Pump Jetmir's count (*"Getting more Shadowborns to pump up his effect"*); a further activation only if six more arrive. |
| -- | Lake of the Dead / Lotus Bloom | Not steps: the payer spends them when a cost needs them. |

**ANSWERED (USER 2026-11-04):** *"Good point, if we activate for Overlord and Jetmir they should remain in that
order."* Fixed: when one main activates for both, the Overlord fetch precedes the Jetmir fetch (the
Jetmir-then-Overlord order is pruned in the same main). Cross-main (Jetmir main 1, Overlord main 2 on an attack
turn, the INT-9 table) is unchanged. USER 2026-11-04: *"I thought about it and the Overlord after Jetmir is pretty bad on an attack turn, but
the best play on those turns is just Jetmir anyway, so we can leave it and let the search determine that."*
So the main-2 Overlord activation after a main-1 Jetmir stays offered; no prune there. And *"Yeah, in main 2 we should still allow Overlord -> Jetmir."*: the same-main
order rule applies in main 2 too, and a main-2 Jetmir fetch (after the Overlord fetch) stays offered, e.g.
the no-attacker turn the INT-9 table used to put in main 1.

## 3. Activations and the land drop (both mains, as on Fungus)

Fungus §7c: the post-combat half drops only CASTS. Activations and the land drop stay in both mains.
Dropping them as well is what made the blunt "skip main 2" version lose 0.03-0.04 turns there.

- **Apostle sacrifice-six activation**: offered in both mains, still filtered by the USER-confirmed
  Demon-timing table in section 0 (Jetmir main 1 when it raises this turn's damage; Overlord main 2;
  sick turn main 1 only; the chain exception). **No change.** All 14 of your activations were in
  main 1. Every Overlord fetch (s1, s3, s6, s9) was on a go-off turn whose board was mostly
  Apostles cast that turn, and each was followed by a Jetmir fetch in the same main. I did not check
  each one against the table's sick / attack test.
- **Lake of the Dead crack** (`SacForMana`) and **Lotus Bloom's sacrifice for mana**: **USER RULING
  2026-11-04:** *"Lotus bloom should be sacrificed on demand by the mana system. not a direct activation."*
  So Lotus Bloom stops being a plan action (today `SacForMana` with one variant per colour) and becomes a
  mana source the payer sacrifices only when a cost needs it (three of one colour). Same principle as the
  USER's Lake ruling (*"it should not be sacrificing the land unless we need it"*); both are being built
  together on branch `sb-lakeprof`.
- **The land drop**: **USER RULING 2026-11-04:** *"Land drop should happen main 1 unless we searched it up
  in main 2."* Main 1 only, except a land that entered the hand during main 2 (fetched by a main-2 tutor)
  may be played in main 2 if the land drop is still unused.
- **Implementation shape (for whoever builds it, after your answers):** use
  `ShadowbornProvider::RestrictsCollectedActions` / `CollectedActionAllowed(s, a, is_pre_combat)`,
  the Vampires precedent. When `!is_pre_combat`, drop `CastFromHand` of every card in the table above
  except Intent and Culling under the victim condition, and drop `Action::Kind::Suspend`. Everything
  else is kept. Main 1 is untouched. The rollout, the d0 runner and the executor all collect through
  the same site, so they stay in lockstep, and human play keeps the full set.
  - Do NOT opt into `SecondMainNeedsDeferredCast`. That gate skips the whole post-combat solve when
    no payable Main2 cast is in hand, which would delete the Overlord-main-2 activation.
  - `ClassifiesMainPhases` is not needed either, since nothing is Main2-only, so the pre-combat
    filter would drop nothing.
  - `RestrictsMain2Casts` alone is not enough, because it does not see suspends.
- **Searched play only?** Fungus's split stands down at d0, so the greedy depth-0 runner keeps its
  main-2 second chance (`MainPhaseFilterActive` requires `SearchedPlayActive`). I recommend the same
  scope here, so the suite's d0 rows are not changed by a search-cost lever (question 5).
- A possible later collapse, sound and measured separately: skip the post-combat solve when its menu
  is only "pass" (no affordable activation, no open land drop, no allowed Intent/Culling).

## 4. Risks: what becomes unreachable, and how we measure it

Lines the split removes:

1. **A main-2 cast paid from mana that only exists after main 1.** In this list that mana does not
   exist. Lands do not untap, and the Lake crack and Bloom sacrifice can happen in main 1 as well.
   This needs one check: a main-1 subset or plan-size cap that the main-2 half currently gets around.
   If main 1 cannot hold the union plan (say, Stone + 3 casts + 2 more), the split loses it. Watch
   for this in the A/B's slower games.
2. **Intent/Culling eating an attacker when a spent body also exists.** The condition drops these
   because main 1 has the spent body. This is only a loss if the sacrifice order would NOT pick that
   body, and the order picks the weakest first, so I believe it is sound. The unit test should state
   it.
3. **A Demonic Tutor in main 2 after combat.** It is dropped. The main-1 cast reaches the same fetch
   and can also cast what it finds, so I expect no loss.
4. **A different cast order** (Ritual before Apostle, Culling after the bodies). This is a play
   change in its own right. With the Stone out, a Ritual's reveal bottoms Apostles from the top four
   and an Apostle's reveal bottoms Rituals, so the order changes which cards the chain finds. It is
   not provably neutral, so it needs its own arm (question 3).
5. **The keep table was generated under today's search** (mulligan-profile Rule 0). Regenerating it
   is your call after adoption.

Measurement, one pooled `--batch` (`deck_compare`/heurarm-style arms, `MTG_PGO=0` on both arms for
wall time):

- Arms: (a) shipped; (b) the main-phase split only; (c) split + the draft cast order; (d) the cast
  order only. (b) and (d) separate the two changes, because they move different things.
- Held-out A/B at **d5 b20** and **d3 b10**, seeds outside every suite block, about 800-1000 paired
  games per cell (the INT-9 known-stop sizing). Report avg turn, faster/slower counts, units per
  game, and thread-ms. Section 1's unit count is the cost the split is meant to remove.
- **Every slower game at `--depth 8 --budget-ms 0`** must recover to its shipped turn or better (the
  no-greedy adoption bar). If one does not, it is a misclassification to fix, not a trade to accept.
- **`scripts/ref_bench.py --deck shadowborn_apostle`** on the 10 references: none may fall short.
  Today s7 is 1 of 10 short (T6 vs T5), the budget miss. The split should help it, since it removes
  the m2 half of the leaf turn that s7's pass 2 paid for.
- The split's fan-out: the main-2 candidates per main-2 decision, and the `[sbsite]` / `MTG_LA_HIST`
  main-2 columns at the s7 T2 root, before and after. Fungus landed at 1.23 plans per main-2
  decision.
- Then the normal flow: smoke + regression (this deck's GT rows move, so accept them), and the
  overnight rows at the next periodic run, not one per change.

## 5. Interactions with other open items

- The s7 greedy-leaf setting (your decision, still open) and this split both cut the main-2 half of
  the leaf turn. Measure the split on the shipped 1-ply leaf first, so the two effects do not hide
  each other.
- The Overlord-main-2 rule and the chain exception are unchanged. This draft only stops CASTS from
  being re-offered after combat.

## 6. Open questions for the USER

1. **Main-phase table (section 1):** every cast in main 1, with Diabolic Intent and Culling the Weak
   ALSO offered in main 2 when the sacrifice would eat an attacker. Is that the rule? Or would you
   rather have plain "all casts main 1" (option A, simpler, but it loses the post-combat sacrifice
   harvest at every budget)? My default is the conditional version.
2. **Suspends (Profane Tutor, Lotus Bloom): main 1 only?** I see no main-2 reason. Default: main 1.
3. **Dark Ritual before the Apostle** (your practice in all 6 reference plans that held both) or after it
   (today's engine)? When the Stone is out each one's reveal bottoms the other's cards, so the order
   changes what the chain finds. My default follows your references (Ritual first), and I propose
   measuring both as arms.
   **ANSWERED (USER 2026-11-04):** *"Casting the Dark Ritual early in a line is totally fine in my opinion. I
   sometimes cast it late, but that doesn't really matter at all."* Ritual first (rank 1, right after the land) stands. The user is
   indifferent to its position, so an order-only arm is a cost/quality check, not a question for the user.
4. **Culling the Weak position: ANSWERED (USER 2026-11-04)** -- land, Dark Ritual, [an Apostle from hand ONLY
   when no creature is on the board], Culling, Thrumming Stone, rest. With a body already out, Culling eats it
   (an attacker is fine: *"probably not worth worrying about the 1 damage from an attacker"*). Build: a
   provider prune of Culling's hand-victim variants (TurnSolver.cpp SAC-ANY-CREATURE post-pass) when the
   battlefield offers a victim, and a cast-order rule that puts a hand victim + Culling ahead of the Stone.
   A scenario test of "land, Ritual, Apostle, Culling (eating it), Stone, Apostles" on an empty board first.
5. **Scope:** searched play only (d0 keeps its greedy second chance, as Fungus does), or every
   depth? Default: searched only.
6. **The tutor ties:** Demonic Tutor before Diabolic Intent, both after the bodies; suspends last.
   Fine as a total order? (I found no interaction that decides it.)
7. **Order of the hard-cast Demons: ANSWERED (USER 2026-11-04)** -- Reaper, Sire, then Overlord last (devotion). Also: *"The flip side to that is that if we sacrifice things to get there then it could end up with less harpies, but overall I don't think this matters."* (Hard-cast Reaper and Sire sacrifice nothing; Diabolic Intent is ranked after the Overlord; only Culling, ranked before the Stone by ruling, eats a body first.)
## 7. Built and measured (2026-11-04, branch `sb-mainphase`; ALL THREE LEVERS DEFAULT OFF, pending the USER)

Levers (heurarm slots, so one pooled batch runs every arm):
- `MTG_SB_MAIN1_RULE` -- section 1's rule, as a plan-level fold of ROOT pre-combat plans
  (`DecisionProvider::Main1PlanFoldLive` / `Main1PlanAdmitted`, applied in `EnumeratePlansWithLandUncached`;
  searched play only, never inside a breakpoint continuation, never in human play). A main-1 plan beyond the
  land drop is admitted only with an attack-ready creature AND (Jetmir on the battlefield, or a plan that can
  still deploy him before combat: a Jetmir fetch, a library tutor, a cast with a ripple granter live, the six
  reachable from hand). A Lake of the Dead drop keeps its casts (its TAP-FIRST float empties at combat). The
  sick-turn Demon search is offered in main 2 with it.
- `MTG_SB_CAST_ORDER` -- section 2's ranks (total by name), Culling's hand victim moved to just before Culling
  (`ShadowbornProvider::ArrangeCastOrder`), and section 2b's same-main order (no Overlord fetch or hard cast
  after a Jetmir fetch in the same main).
- `MTG_SB_STONE_FIRST` -- AGENT amendment, inert without the cast order: Thrumming Stone ranks ahead of Dark
  Ritual / Culling; the payable-order fallback still puts a ritual first when the Stone needs its mana.

Pooled held-out A/B, d5 b20, max_turns 8, 400 paired games per arm (set A seeds 50001+gi, set B 60001+gi):

| arm | avg A | avg B | units A | units B | slower A / B | slower not recovered at d8 b0 |
|---|---|---|---|---|---|---|
| shipped | 4.8000 | 4.7975 | -- | -- | -- | -- |
| split | 4.8000 | 4.8000 | -3.8% | -6.8% | 12 / 11 | A: gi32, 46, 115, 328; B: gi244 |
| order (USER) | 4.7900 | 4.7650 | -0.3% | -0.9% | 13 / 8 | A: gi1, 166, 263, 265, 372; B: gi22, 94, 164 |
| split + order | 4.7675 | 4.7475 | -8.4% | -11.0% | 15 / 10 | the order's, both sets |
| order + stone-first | 4.7650 | 4.7500 | -1.2% | -0.4% | 7 / 3 | none |
| split + order + stone-first | 4.7525 | 4.7400 | -8.2% | -10.4% | 11 / 6 | none |

Root cause of the USER order's unrecovered games (each checked: shipped at d8 b0 keeps the faster turn): the
winning line is "Thrumming Stone, THEN Dark Ritual, then Apostle" -- a Ritual cast after the Stone ripples, and
its free reveal bottoms four non-Apostles in front of the Apostle chain (seed 50002 gi1: T3 chain of 18 Apostles
vs 1, T4 vs T5). "Ritual first" makes that line inexpressible at every budget. Hence the stone-first amendment.
The split-alone unrecovered games (e.g. 50047 gi46: a T1 main-2 Dark Ritual cast for nothing, one Ritual short on
T3) are NOT root-caused; none of them is slower once the order + stone-first is on. ref_bench (10 references):
shipped and split 1/10 short (s7), every order arm 0/10 short.

## 8. USER rulings on the A/B (2026-11-05, verbatim)

1. Stone-first amendment: *"I don't care about rippling to skip non-apostles. That is clear clairvoyance as you wouldn't
   do it if you didn't know what the top 4 are."* -> Stone-first is NOT adopted; the USER's order (Ritual first)
   stands. The "lost" games under the USER's order came from a Dark Ritual ripple whose value is bottoming four
   non-Apostles above an Apostle run, i.e. a ripple decision that is only good if you know the top four. OWED: find
   where the search makes the reveal choice with the library known (clairvoyance) and make it blind.
2. Culling's victim: *"That makes sense."* -> the narrow rule stays (a Harpy or an Apostle on the board, else a hand
   Apostle cast just before Culling; never a Demon or Jetmir ahead of a hand Apostle).
3. Overlord after Jetmir: *"Yeah, overall we might even find that pruning the overlord main-2 if we played Jetmir in
   main-1 is actually the correct course of action. Alternatively we could have the Jetmir activation in Main-1 cause
   us to move the Overlord activation before it? That is a better option if we want it. Overlord in Main-2 i worse in
   the case where we drop Jetmir in Main-1."* -> prune the main-2 Overlord (fetch or cast) when Jetmir was put
   in main 1; the Overlord line is represented by Overlord -> Jetmir inside main 1. This supersedes 2b's "a main-2
   Overlord after a main-1 Jetmir stays searched" and removes the edge case.
4. The main-1 rule's standalone losses (a Dark Ritual cast for nothing in T1 main 2): *"Hmm... We might want to prune
   rituals with no payoff like we have for other decks."* -> add the ritual-without-payoff prune for this deck,
   reusing the existing mechanism other decks use.

## 9. Built 2026-11-05 (branch `sb-mainphase`, local; every lever still DEFAULT OFF, pending the USER)

USER rulings received this session (verbatim, in order):
* *"I don't care about rippling to skip non-apostles. That is clear clairvoyance as you wouldn't do it if you didn't
  know what the top 4 are."*
* *"That makes sense."* (Culling's narrow victim rule)
* *"Yeah, overall we might even find that pruning the overlord main-2 if we played Jetmir in main-1 is actually the
  correct course of action. Alternatively we could have the Jetmir activation in Main-1 cause us to move the Overlord
  activation before it? That is a better option if we want it. Overlord in Main-2 i worse in the case where we drop
  Jetmir in Main-1."*
* *"Hmm... We might want to prune rituals with no payoff like we have for other decks."*
* *"We shouldn't do anything heavy for now as we have another agent running."* / *"(another agent running on a
  separate container)"*
* *"No, I don't want you to worry about making the decision blind for ripple. We should just not worry about the
  impacts of clairvoyance on our rules."*
* *"You can use non-clairvoyant search and analysis to determine whether it is clairvoyance."*
* *"My actual suggestion for Overlord is to move it wherever it needs to go to be before Jetmir. It should be in main-1
  if Jetmir is there and main-2 otherwise."*

**A. `MTG_SB_STONE_FIRST` deleted** (code, heurarm slot, test). The USER's order stands.

**B. Ripple clairvoyance: NOT built (USER: do not make the ripple decision blind).** Diagnosis kept for the record:
with Thrumming Stone in HAND, `EmitRipplePins` emits each ritual cast both as `kRippleRevealAll` (needs a granter cast
in the plan) and as the requirement-free `B = 0` twin, and a plan that casts the Stone ahead of the Ritual admits
either, so the search picks "reveal" or "don't reveal" by reading the top four. Under the shipped order (Stone rank 1,
Ritual rank 15) that is the clairvoyant Ritual ripple-dig the USER ruled out; the USER's order (Ritual first) never
reaches it.

**C. Overlord placement** (`SbJetmirArrivedThisTurn`, `SbMain1JetmirCanFollow`). Once Jetmir has arrived this turn
(`entered_this_turn`; exact in this list, nothing puts him in play outside a main) no Overlord fetch or hard cast is
offered for the rest of the turn (either lever). Under `MTG_SB_MAIN1_RULE` a MAIN-1 Overlord is offered only when
Jetmir is there in main 1 (on the battlefield with an attacker, or still deployable this main: an attacker, Jetmir in
the library, and the bodies for a further six), and on an attack turn main 1 offers the Overlord AHEAD of Jetmir when
both sixes can be had. Everything else is a main-2 Overlord. Residual (erring wide): a main-1 Overlord whose follow-up
Jetmir the search then declines is not refused, so that line can appear in both mains.

**D. Ritual without a payoff** (`MTG_SB_RITUAL_PAYOFF`, `DecisionProvider::PrunesRitualWithoutSpend`, the spend form of
the Dragonstorm payoff prune at its two lockstep sites). Drops a subset that casts a ritual (Dark Ritual, Culling, a
Lake crack) and spends mana on nothing else, plus the two phantom sinks found in held-out 50047 gi46: a ripple granter
only the ritual's own chain credit could pay (T1 `{Stone, Ritual}` on one Swamp), and a sacrifice-six activation the
six cannot reach (T2 `{Ritual, activation}` with one Apostle, no granter). Re-check of the main-1 rule's standalone
losses (d5 b20 and d8 b0, one game at a time):

| game | shipped | main-1 | main-1 + ritual prune | main-1 + order + ritual prune |
|---|---|---|---|---|
| 50033 gi32 | 3 | 4 | 4 (d8b0 4) | 3 |
| 50047 gi46 | 4 | 5 | 5 (d8b0 5) | 4 |
| 50116 gi115 | 5 | 6 | 6 (d8b0 6) | 5 |
| 50329 gi328 | 6 | 7 | 7 (d8b0 7) | 6 |
| 60245 gi244 | 5 | 6 | 6 (d8b0 6) | 5 |

The prune removes the wasted Rituals (gi46 casts none on T1/T2) but does not recover these five under the main-1 rule
ALONE: in gi46 the T3 main-2 line lands "Ritual, Ritual, Stone" without the Apostle (shipped: Ritual, Ritual, Stone in
main 1 + Apostle in main 2), so the chain starts a turn later. NOT root-caused. All five recover with the cast order on.

**Clairvoyance classification of the order's slower games (§7)**, non-clairvoyant tool = the known-respecting blind
search (`MTG_SEARCH_BLIND_UNKNOWN=1`, salts 1..16), shipped vs USER order, d5 b20, one game at a time
(`logs/sb-mainphase/blinddig.py`). "dig" = the shipped line casts Dark Ritual after Thrumming Stone (the ripple-dig):

| game | clairvoyant shipped / order | blind: salts where shipped digs | blind shipped avg (dig / no-dig) | blind order avg | verdict |
|---|---|---|---|---|---|
| 50002 gi1 | 4 (dig) / 5 | 5 of 16 | 4.00 / 5.00 | 5.00 | clairvoyance-only |
| 50167 gi166 | 4 (dig) / 5 | 3 | 4.00 / 6.46 | 7.38 | dig clairvoyant; residual gap without it -> UNRESOLVED |
| 50264 gi263 | 4 (dig) / 5 | 6 | 5.50 / 7.00 | 7.00 | clairvoyance-only |
| 50266 gi265 | 4 (dig) / 5 | 5 | 4.00 / 8.36 | 8.38 | clairvoyance-only |
| 50373 gi372 | 4 (dig) / 5 | 2 | 6.50 / 8.14 | 7.62 | clairvoyance-only |
| 60023 gi22 | 4 (dig) / 5 | 0 | - / 6.06 | 6.94 | dig never chosen blind; residual gap -> UNRESOLVED |
| 60095 gi94 | 5 (dig) / 6 | 0 | - / 8.06 | 8.06 | clairvoyance-only |
| 60165 gi164 | 4 (no dig) / 5 | 0 | - / 7.81 | 7.94 | no dig; blind tie -> inconclusive |

Caveat: the blind machinery plays far worse than the clairvoyant search here (many blind games unwon by T8), so the
blind averages are noisy; gi166 and gi22 need a look before they are called real rule losses.

**F. Prepared, NOT run** (USER: nothing heavy). Manifest `build/scratch/sb-mainphase/ab7/manifest.json` (480 jobs,
4,800 games: arms shipped / split / split+rit / order / both / both+rit, sets 50001 and 60001, 400 paired games
each, d5 b20, max 8; binary `ab7/mtg.ab` = commit in `ab7/COMMIT`). Run from the worktree root:
`build/scratch/.../ab7/mtg.ab --batch <ab7>/manifest.json --threads 32 --game-log-dir <ab7>/games > <ab7>/batch.stdout 2> <ab7>/batch.stderr`
then `python3 logs/sb-mainphase/ab5.py report <ab7>`; repro each slower game at `--depth 8 --budget-ms 0`
(`logs/sb-mainphase/recheck.py 8 0 ...`) and classify the order arm's with `blinddig.py`.

## 10. Root cause of the five unrecovered games (2026-11-05; no amendment, every lever still DEFAULT OFF)

Every game was played one at a time at `--depth 8 --budget-ms 0` per arm with one lever toggled at a time (binary
`ab7/mtg.ab`, 309ff7ec3; logs `build/scratch/sb-mainphase/rcx/<BASE>_<gi>/<arm>`). Win turns (9 = no win by T8):

| game | shipped | rit only | order | order+rit | split+rit | both | bothrit | removed by |
|---|---|---|---|---|---|---|---|---|
| 60135 gi134 | 7 | 7 | 7 | 9 | 7 | 7 | 9 | ritual prune, only with the order |
| 60104 gi103 | 7 | 7 | 7 | 8 | 7 | 7 | 8 | ritual prune, only with the order |
| 60095 gi94 | 5 | 5 | 6 | 7 | 5 | 6 | 7 | order (T5->6), then the prune (6->7) |
| 50167 gi166 | 4 | 4 | 5 | 5 | 4 | 5 | 5 | order |
| 60023 gi22 | 4 | 4 | 5 | 5 | 4 | 5 | 5 | order |

The main-1 rule removes nothing in any of the five. Every lost line is a Dark Ritual ripple-dig under Thrumming Stone:
* **Ritual alone (gi134, gi103, and gi94's second turn).** gi134 T5: Stone already out, "Dark Ritual" alone; the ripple
  reveals {Swamp, Profane Tutor, Swamp, Swamp}, all bottomed, and the {B}{B}{B} goes unspent. The only effect is that
  the T6 draw becomes Demonic Tutor instead of a Swamp. gi103 T4 is the same: two free Rituals, mana unspent, and the
  T5 draw becomes an Apostle instead of a Stone. Under the shipped order the search reaches the same dig as
  "Stone, Ritual" inside one plan (split+rit's gi134 T5 main 2), and the prune keeps that (the Stone is a paid sink).
  The USER's order casts the Ritual ahead of the Stone, which leaves only the Ritual-alone form, and the prune drops
  that form by design (§9.D: "its value as a dig is the clairvoyant one the USER ruled out"). gi134 under bothrit is
  then a COMPLETE proven no-win at T1 (`proven_no_win`; the log stops after T1).
* **Stone, then Ritual, then Apostle (gi166, gi22, and gi94's T3).** gi22 T3 shipped: Lake, Stone, Ritual (the ripple
  reveals {Swamp, Diabolic Intent, Swamp, Culling}: no Ritual, nothing cast, all four bottomed), then the Apostle chains.
  "Ritual, Stone, Apostle" costs the same and is payable under the order, but its Apostle would reveal those same four
  and stop. gi166 T3 is the same (the one free Ritual's mana is not needed: five floating left over). The first
  divergence is at T2 (shipped holds Lake for the 6-mana T3). This is exactly the USER's "rippling to skip
  non-apostles".

**Non-clairvoyant check** (`rcx/blindrit.py`: known-respecting blind search, salts 1..16, d5 b20, one game at a time;
avg win turn, unwon = 9): gi134 shipped 9.00 / bothrit 9.00; gi103 9.00 / 8.69; gi94 8.06 / 7.88; gi166 6.00 / 7.81;
gi22 6.06 / 7.56. At d5 b0 for gi166 and gi22 against the order: 6.38 / 7.38 and 6.25 / 7.69. For gi166 and gi22 the
blind gap does not come from a dig (gi22: no blind salt digs). Per salt, the first divergence is the T2 land and the
Diabolic Intent target (shipped: Swamp, fetch an Apostle; order: Lake, fetch a Ritual). Every step of shipped's line
there is legal under the order: none of the three levers touches the land, a tutor target, or {Stone, Apostle}.
So it is the determinized blind search choosing differently on one library, not a removed line. Population blind
A/B (below): no blind cost.

**Pooled A/B** (`build/scratch/sb-mainphase/ab8`, ONE batch, 3,200 games: ab7's shipped and bothrit jobs, plus the
same jobs with `blind_salt=1`). Clairvoyant rows are byte-identical to ab7.

| arm | set 50001 avg (d) | units | set 60001 avg (d) | units | faster/slower A, B |
|---|---|---|---|---|---|
| shipped | 4.7950 | -- | 4.7850 | -- | -- |
| bothrit | 4.7775 (-0.0175) | -14.2% | 4.7325 (-0.0525) | -19.6% | 24/18, 31/11 |
| shipped, blind salt 1 | 6.3575 | -- | 6.3075 | -- | -- |
| bothrit, blind salt 1 | 6.1950 (-0.1625) | +7.0% | 6.3150 (+0.0075) | +7.7% | 95/76, 94/96 |
