# Fungus cleanup discard: the generic rule sheds Mycoloth and Doubling Season first

**Status: OPEN.** Found 2026-09-23 when the USER asked *"Does fungus have a proper discard
heuristic? If it is searched there could be some issues"* and added *"(though there isn't that much
draw in the list, but our Psychotrope kept showing up in the slowest hands)"*.

## 1. It is NOT searched, and that half is fine

`DecisionProvider::CleanupDiscardSearchWidth()` returns the base **1**, and only
`TreasureHuntProvider` overrides it. `FungusProvider` overrides **nothing** discard-related, so:

* the rollout's plan enumeration emits no `discard_choice` variants (width 1 == no branch, and is
  byte-identical to not having the axis);
* the executor's searched cleanup pass (`AIEngine::ChooseDiscard`) trials exactly the indices
  `CleanupDiscardCandidates` returns, and the base implementation returns ONE.

So there is no search explosion here to worry about. That is also deliberate: a blind width > 1 was
swept and REFUTED on 2026-08-06 (monotonically worse at W=2/4/8 -- budget dilution, because the
decision fires on a tiny fraction of turns while every base plan pays a variant). If this axis is
ever widened it must be bp-style -- fan only the plans whose simulation actually reaches an
over-limit cleanup. See `searched-discard-as-search-node.md`.

## 2. The heuristic it DOES use is wrong for this deck

With no provider override, no `DiscardLandsFirst`, and **no `required_pieces` in
`decks/Fungus/Fungus.profile.json`** (its keys are only `card_scores`, `hand_score_threshold`,
`mulligan`, `version`), the base rule collapses to a single sentence: **shed the highest mana value.**

On Fungus that orders the hand exactly backwards:

| MV | card | shed order |
|---|---|---|
| 5 | **Mycoloth** | FIRST |
| 5 | **Doubling Season** | second |
| 4 | Sporesower Thallid | |
| 3 | Psychotrope Thallid, Beastmaster Ascension | |
| 2 | Thallid Shell-Dweller, Sporecrown Thallid | |
| 1 | Wild Growth, Utopia Mycon, Tukatongue Thallid, Thallid, Essence Warden | |
| 0 | Simic Growth Chamber, **Forest** | LAST |

A flooded hand therefore pitches the deck's two payoff cards and keeps the 19th Forest, in a deck
running **22 lands in 60**. Mycoloth is the card the whole second-main/devour strand
(`fungus-second-main-and-devour.md`) exists to cast.

## 3. Why it may still be small -- MEASURE BEFORE FIXING

The base rule's own note records that **five of nine suite decks never reach a cleanup discard at
all** and three more are under 40 per 400 d0 games. Fungus empties its hand onto the board, so the
over-seven cleanup may be rare; the deck's only draw is Psychotrope Thallid's sac-a-Saproling outlet.
**Do not write a provider override before counting how often the shed actually fires**, and count it
on a sample that contains the wide Doubling Season games -- §8c of `fungus-second-main-and-devour.md`
records that a 30-game Fungus sample inverts every cost/volume comparison the 150-game one gives.

Two distinct effects must not be conflated:
* a wrong shed costs **win turn** (quality);
* Psychotrope showing up in the slowest hands is a **cost** observation, and the likelier mechanism
  there is that each draw is a BREAKPOINT opening search nodes, not the discard rule.

## 4. FREQUENCY, MEASURED

`MTG_TRACE=discard` (real resolutions only -- every rollout scope clears `g_real_resolution`, so
this counts the discards the GAME made, not the millions the search imagined), 400 games at d1/b3
with the adopted mulligan profile live:

**ONE cleanup discard in 400 games**, and it shed **Doubling Season** -- the predicted failure,
observed. So the rule is wrong but almost never consulted: this is a correctness wart with a very
small expected value, and any fix must be judged on that basis rather than on how bad the ordering
looks. Fungus empties its hand; it does not flood past seven.

## 5. THE USER'S BUCKETING (ruling, 2026-09-24)

> *"Rather than just an order, the best way to deal with it is to start with buckets. One for mana,
> one for threats and perhaps one for enablers. (doubling season + beastmaster) Though you only
> really want 1 Doubling Season and 1 Beastmaster at the most. Mana we should aim to have the lesser
> of 3-4 spots and 5 total mana (counting the board) We should keep a mix of Fungus, but especially
> one Sporecrown, one Mycoloth, one Sporesower and a 1-drop, preferably Utopia Mycon."*

**A RETENTION TARGET PER BUCKET, not a shed ranking.** The shed list is then derived: whatever
exceeds its bucket's target is surplus, and surplus is what gets discarded. That inverts the base
rule's question from "which card is worst" to "which bucket is over quota", which is the right
question for a deck whose hand is a mix of interchangeable pieces.

| bucket | members | retention target |
|---|---|---|
| **MANA** | Forest, Simic Growth Chamber, Wild Growth | to a total of **5 mana with the board**; <=3 lands, <=1 accelerator |
| **ENABLERS** | Doubling Season, Beastmaster Ascension | **1 of each, at most** |
| **THREATS** | the Fungus creatures (+ Essence Warden) | a MIX, with a protected core |

* **MANA**, stated by the USER over three messages and reconciled here:
  *"the lesser of 3-4 spots and 5 total mana (counting the board)"*, then *"only 3 lands at most and
  maybe one accelerator. If we have any mana on board, probably keep it at 3"*, then
  *"(or whatever we need to reach 5 total mana)"*. Together that is

      need        = max(0, 5 - board_mana)          // 5 = the deck's top of curve
      keep_lands  = min(3, lands_in_hand, need)      // hard cap 3
      keep_accel  = min(1, accels_in_hand, need - keep_lands)

  **The 5 INCLUDES the accelerator's mana** (USER: *"To be clear the max 5 includes the accelerator
  mana"*) -- a kept Wild Growth counts as one of the five, it is not a bonus on top. So the caps
  (3 lands, 1 accelerator) bound the bucket, and the 5-mana total is what actually stops it: at
  `board_mana = 2` we keep three mana cards in total, not three lands PLUS an accelerator.

  -- a cap of 3 that TIGHTENS as the board fills, not a flat 3. **5 is the deck's top of curve**
  (Mycoloth `{3}{G}{G}`, Doubling Season `{4}{G}`), so once the board alone makes 5 the target is
  ZERO and every land in hand is surplus. Board at 1-2 mana is the common case at a cleanup
  discard, which is why "probably keep it at 3" is the usual answer. The 3 counts Forest and Simic
  Growth Chamber together; `board_mana` is mana the board makes in a turn, so a Chamber counts 2
  and each Wild Growth adds 1 (the engine already has `OptimisticTurnMana` for exactly this).
* **THREATS protected core:** one Sporecrown Thallid, one Mycoloth, one Sporesower Thallid, and one
  one-drop -- **preferably Utopia Mycon** (it is the free sac outlet: "Sacrifice a Saproling: add
  one mana of any color", which is the deck's real mana engine once a swarm exists).

Cross-bucket shed order follows from the targets: surplus MANA, then surplus ENABLERS (the 2nd+
Doubling Season / Beastmaster), then surplus THREATS beyond the core.

Adopt via the heuristic-optimization route (train seeds, then held-out), behind a `heurarm` slot,
like every other Fungus lever -- though see the frequency in section 4 before spending a sweep on it.

## 6. Settled gaps (2026-10-09)

The section-5 buckets were documented 2026-09-24 but never implemented (no Fungus discard code
existed; the generic max-MV ranking still ran). On 2026-10-09 the USER filled the remaining gap --
which bucket gives way when every bucket is within target and the hand is still over seven:

### Fungus: ALREADY USER-RULED 2026-09-24 -- docs/design/fungus-cleanup-discard.md §5 (on main)
Card data verified (mono-G Thallids / Saprolings; 19 Forest + 3 Simic Growth Chamber; required_pieces
empty). Real play: 1 cleanup shed in 400 games (it shed Doubling Season). The ruling: MANA to 5 total
counting the board (<= 3 lands, <= 1 accelerator, the 5 includes the accelerator); ENABLERS Doubling
Season 1 + Beastmaster Ascension 1 at most; THREATS a mix with a protected core of one Sporecrown, one
Mycoloth, one Sporesower and one 1-drop (Utopia Mycon preferred); shed surplus mana -> surplus enablers
-> threats beyond the core. GAPS asked 2026-10-09: the order among protected cards (needed for a total
order: 3 mana + 2 enablers + 4 core = 9 > 7) and the order of threats beyond the core.
Agent draft (SUPERSEDED where the SETTLED gaps below differ): KEEP land -> Utopia Mycon (else a 1-drop) -> land -> Sporecrown -> land -> Sporesower ->
Doubling Season -> Mycoloth -> Beastmaster -> accelerator (within the 5) -> other threats (variety
first; then 2nd Sporecrown > Tukatongue = Thallid > Psychotrope > Shell-Dweller > Essence Warden --
Warden is not a Fungus and its lifegain is inert in a goldfish). Dead Simic Growth Chamber (no other
land to return) shed first (the Karoo rule).


### Fungus answer to the USER's "Do we not have a proper bucketing approach going on here?":
the 09-24 buckets were DOCUMENTED but NEVER IMPLEMENTED (no Fungus discard code; generic max-MV still
runs). The buckets stand as ruled; the gap is only which bucket gives way when all are within target
and the hand is still over 7 (re-asked in bucket terms).
USER 2026-10-09: "I would keep things that combo with doubling season (like Mycoloth, Saproling Burst)
along with the season itself unless they are uncastable for the foreseeable future."
#### Fungus gaps: SETTLED (USER, 2026-10-09) -- append as §6 of fungus-cleanup-discard.md
- DOUBLING SEASON + its combo partners (param-keyed: Mycoloth's devour / per-counter upkeep tokens;
  Saproling Burst's fade-counter tokens -- Burst is in decks/Fungus/candidate-b-2026-09, not the
  shipped 60) are kept TOGETHER, above Beastmaster and the rest of the core, unless uncastable for the
  foreseeable future (agent reading: 5 mana not reachable within 2 turns counting lands in play + kept
  land drops + the accelerator -- surfaced).
- Then (agent default, surfaced): Beastmaster gives way first, then the remaining core creatures
  (Sporesower, Sporecrown, the 1-drop), mana within the 5 kept longest. Creatures beyond the core: one
  of each name first, Essence Warden last. Dead Simic Growth Chamber shed first.

#### Implementation notes (2026-10-09)
Implemented as `FungusProvider::CleanupDiscardCandidates` (src/ai/DecisionProviders.cpp) behind
`MTG_FUNGUS_BUCKET_DISCARD` / heurarm `FUNGUS_BUCKET_DISCARD`, default ON (`=0` restores the generic
max-MV ranking exactly). Tests: `test/unit/test_fungus_discard.cpp`. Every role is keyed on card
PARAMS, so the same code serves the shipped 60 and `candidate-b-2026-09` (Saproling Burst, Sol Ring,
Slimefoot...).

* KEEP LADDER (most kept first): kept lands > kept accelerator > [Season tier, if castable] > core
  1-drop > Sporecrown > Sporesower > Beastmaster > [Season tier, if NOT castable] > beyond the core
  (one of each name, then the duplicates, Essence Warden last).
* SHED ORDER: dead (Karoo with no other land to return, then a legend already in play / a 2nd hand
  copy) > surplus lands > surplus accelerators > surplus Beastmaster > surplus Season > the ladder
  backwards. Every non-staged hand card is named. Prefix-stable (checked by the tests).

Agent readings (not USER-ruled; each is a one-line change if overturned):
1. ACCELERATOR = a land Aura with `land_aura_extra_mana` (Wild Growth) OR a `mana_rock` (candidate B's
   Sol Ring). A land Aura with no host (no land in play, none kept) is surplus.
2. `board_mana` = lands by `produces_amount` + land-Aura extras + rocks + creature mana dorks. NOT
   counted: Utopia Mycon's sac-for-mana and a Brightcap Badger grant (both spend the swarm).
3. ~~A hand land fills one unit of `need` whatever it taps for.~~ REVIEW FIX (2026-10-09): a hand
   land fills `need` by the MANA it makes, exactly as `board_mana` counts the board (USER: "MANA to 5
   total counting the board") -- candidate B's 2-mana depletion lands are two of the five; a Karoo
   counts +1 (its drop returns a land). Lands are kept best-first, at most 3, until their mana covers
   `need`; one accelerator only while `need` is still unmet. Land keep order: non-Karoo > Karoo,
   untapped > enters-tapped, more mana, hand order.
4. Core 1-drop = a FUNGUS creature of MV 1 (Essence Warden never fills it): a hand Mycon-type first,
   else another hand Fungus 1-drop. ~~Filled from the board by a Mycon in play~~ -- superseded by the
   USER's 2026-10-10 creature-quota ruling (see "Implementation notes -- USER rulings 2026-10-10").
5. The Season tier keeps ONE Season (net of board: "at most 1") and ONE copy of each partner kind --
   devour / per-counter upkeep tokens (Mycoloth), fade-counter tokens (Saproling Burst) -- from the HAND
   (the 09-24 "one Mycoloth"; a partner is a threat, so since 2026-10-10 a copy in play does not fill
   it). Extra copies are beyond-core threats. The tier holds its position even with no Season anywhere
   (Mycoloth alone). Inner keep order Season > devour partner > fade partner.
6. "Uncastable for the foreseeable future" -- **INTEGRATOR READING (2026-10-09), surfaced; it replaces
   the agent reading below**: the Season tier is castable when `board_mana` + ALL the kept mana (every
   kept land and the kept accelerator, by the mana they make) reaches 5. The USER sized the MANA bucket
   to exactly 5, so a hand that fills it can cast the Season. The literal 5 is the bar for the whole
   tier, so it moves as one unit.
   ~~5 mana within 2 turns, counting the first two kept land drops~~ (the agent's first reading): on
   an ordinary board -- 2 Forests in play, 3 Forests kept, 5 lands with no draw -- it marked the Season
   "uncastable" and shed Mycoloth and the Season exactly as the generic rule does, inverting the
   USER's ruling.
7. An uncastable tier gives way right after the beyond-core cards and BEFORE Beastmaster.
8. Surplus enablers: the 2nd+ Beastmaster before the 2nd+ Season (one in play makes every hand copy
   surplus).
9. Beyond the core, keep order lord (2nd Sporecrown) > Sporesower / partner copies > other bodies
   (Thallid = Tukatongue, extra Mycons, candidate-B Fungi, unclassified spells such as Concordant
   Crossroads) > the sac-draw outlet (Psychotrope) > defender (Shell-Dweller); "Essence Warden last" =
   a non-Fungus, non-Saproling creature carrying `any_creature_enters_lifegain`, kept after even the
   duplicates. "One of each name" counts hand copies only.

## 7. USER rulings (2026-10-10)

Cross-deck (verbatim):
- *"Colour beats the land count is correct."* -- a land that gives a KEPT spell a colour nothing on the
  board or already kept provides is kept even past the land quota (Creature Giving, Melira Pod and
  EldraziDisplacerFlicker already do this; every deck follows it).
- *"Creature quotas are less worried about the board than land quotas. This is because you can always add
  more creatures and sometimes this speeds up the clock."* -- creature / threat / payoff quotas are filled
  from the hand WITHOUT subtracting creatures already in play. Land and mana quotas still count the board;
  engine, combo-piece and duplicate-legend rules keep the board rules their design states.
- *"For Distance to castable I think it makes sense to have a smarter model if we can."* -- the shared
  model in docs/design/discard-distance-model.md replaces the per-deck crude proxies.

#### Implementation notes -- USER rulings 2026-10-10
The creature-quota ruling (*"Creature quotas are less worried about the board than land quotas ..."*) is
implemented; exactly what changed in `FungusProvider::CleanupDiscardCandidates`:
* **Sporecrown** core slot: filled from the hand even with a Sporecrown in play (was: a lord in play
  filled it and the hand copy fell beyond the core).
* **Sporesower** core slot: the same.
* **The 1-drop** core slot: a hand Mycon-type, else another hand Fungus 1-drop -- a Mycon (or any
  Fungus 1-drop) in play no longer fills it.
* **Mycoloth / Saproling Burst** (the Season tier's partners, threats): one hand copy of each is kept
  in the tier even with a copy in play.
* **Unchanged** (the design's own board rules): Doubling Season and Beastmaster stay "at most 1"
  counting the board (a copy in play makes every hand copy surplus); the MANA bucket still counts the
  board (need = 5 - board_mana); a legend already in play still makes the hand copy dead.
* Not implemented here: the "colour beats the land count" cross-deck ruling -- the coordinator's
  2026-10-10 brief for this deck did not list it; it is inert on the shipped 60 (mono-green; the
  Chamber's U pays nothing) and would matter only on candidate B (black for Slimefoot / Deathspore).
* The shared distance model is not used: the deck's only distance notion is the Season tier's exact
  5-mana threshold over the kept hand (listed as unchanged in docs/design/discard-distance-model.md).
* Tests: `test/unit/test_fungus_discard.cpp` 17 cases -- F6 re-derived, F12 (Sporesower + Mycoloth in
  play), F13 (Beastmaster in play still makes the hand copy surplus); F6 and F12 fail on the
  pre-ruling code.
