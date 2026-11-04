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
| 2 | Thrumming Stone | one place later (was 1) | Granter: every later cast wants it out first. The payable-order fallback still pulls Culling ahead when the Stone needs its mana. |
| 3 | Shadowborn Apostle | none in effect (10) | Starts the chain. Bodies go down before anything that sacrifices or counts them. |
| 4 | Abhorrent Overlord (hard cast) | **ordered** (was a plan-order tie at 10) | After the Apostles: devotion counts each Apostle's {B}, so more Harpies. |
| 5 | Reaper from the Abyss (hard cast) | **ordered** (tie at 10) | Body; no cast-order interaction. |
| 6 | Sire of Insanity (hard cast) | **ordered** (tie at 10) | Body; the discard is at the end step. |
| 7 | Jetmir (cannot be cast) | **ordered** (tie at 10) | Listed only so the order is total. |
| 8 | Culling the Weak | **moved after the creatures** (15 to 8) | After the chain its victim is a fresh sick Apostle. The payable fallback pulls it ahead when its {B}{B}{B}{B} is needed for the Stone or a Demon. |
| 9 | Demonic Tutor | ordered (tie at 20) | After the bodies; the shuffle comes after the chain has used the top of the library. |
| 10 | Diabolic Intent | ordered (tie at 20) | Victim is a sick Apostle from this turn's chain. DT before Intent is an arbitrary tie-break (q6). |
| 11 | Profane Tutor (suspend) | ordered (tie at 20) | A suspend; position only decides which mana pays {1}{B}. |
| 12 | Lotus Bloom (suspend {0}) | ordered (tie at 20) | Costs nothing, so its position does not matter. |

Plus `CastOrderTiesByName` (or `CastOrderTotalByName`), so that no two different cards ever tie and
fall back to plan order. That follows your cross-deck ruling of 2026-10-14: *"Let's fully order the
cards in each list inasmuch as possible."* Mana activations (Lake crack, Lotus Bloom's sacrifice) are
not casts. The payer spends them as payments need, exactly as today.

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
4. **Culling the Weak after the Apostle chain** (so it eats a fresh sick Apostle), with the payable
   fallback pulling it ahead only when its mana is needed for the Stone or a Demon? Default: yes.
5. **Scope:** searched play only (d0 keeps its greedy second chance, as Fungus does), or every
   depth? Default: searched only.
6. **The tutor ties:** Demonic Tutor before Diabolic Intent, both after the bodies; suspends last.
   Fine as a total order? (I found no interaction that decides it.)
7. **Order of the hard-cast Demons:** Overlord (after the Apostles, for devotion), then Reaper, then
   Sire. Fine? It rarely matters (Overlord 3 and Reaper 6 hard casts in 100 games).
