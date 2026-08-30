# Dragons — cleanup-discard BUCKET policy (SHIPPED 2026-08-30; REPAIRED the same day)

Authored per the analyze-deck skill's Stage 5i, approved by the user, and shipped as
`DragonsProvider::CleanupDiscardCandidates` behind a default-on `MTG_DRAGONS_BUCKET_DISCARD`.
Buckets and quotas below follow user direction given 2026-08-30.

> **The first implementation did not run this policy.** It shipped in `c0399210` with a
> classification bug that made the measured result (metric-neutral) a measurement of something else
> entirely. See "The classifier bug" below before reading the rest as a description of shipped
> behaviour.

## Why the usual "it barely fires" argument does NOT apply here

The obvious way to size this work is to count cleanup discards in finished games. **That is the
wrong denominator, and it is wrong by four orders of magnitude** (user, 2026-08-30). Measured with
`MTG_SHED_STATS=1`, 200 games at the shipped d5/b40:

| deck | sheds in REAL play | sheds inside the SEARCH | ratio | taken with <4 lands |
|---|---|---|---|---|
| **Dragons** | 66 | **661,269** | **10,020x** | 99.6% |
| Minotaur | 99 | 250,265 | 2,528x | 100% |

Index 0 of this ranking decides every one of those rollout sheds **with no search above it**. So the
rule is not a rare tiebreak that fires twice a game — it is a hot-path heuristic that shapes which
plans the search believes are good, and therefore which line it commits to in games where no
discard is ever actually taken. A real-play census cannot see any of that.

The second column of that table matters as much as the first: essentially every rollout shed happens
with **fewer than four lands on the battlefield** — the screwed/flooding shape, where the ranking is
choosing among cards the player cannot yet cast. That is precisely where "shed the most expensive
card" is most likely to be wrong, because at 2-3 lands nearly every payoff in this deck is uncastable
and the fallback simply pitches them largest-first.

## The classifier bug (found 2026-08-30, one day after shipping)

`is_reducer` was written as `params.reduces_spell_subtype_amount > 0`. **That field defaults to 1 on
every card in the database** (`CardDatabase.cpp`: `params.value("reduces_spell_subtype_amount", 1)`),
so the test was true for every nonland card in hand. Measured on 20 games, 32,087 calls: hands read
as `lands=1 red=7 pay=0 enab=0 rest=0` — seven "cost reducers" in an eight-card hand.

Everything downstream followed from that:

* the PAYOFF bucket was always empty, so the "keep at least 2 Dragons" floor never protected a
  Dragon and the `payoff_need <= 0` guard permanently blocked the enabler slot;
* the shed order collapsed to "surplus lands, then every nonland card in REVERSE HAND ORDER";
* a second, independent bug hid inside the first: the `rocks` bucket was never filled (the partition
  sent rocks to `enablers`), so the Sol-Ring-first yield sort and the rock contribution to the land
  quota were both dead code.

So the policy that was measured as **metric-neutral** was not this policy. With the bug in place,
`MTG_DRAGONS_BUCKET_DISCARD=0/1` is byte-identical on a 20-game probe; repaired, the same A/B moves
the d3 average (250 games, s1001: 5.8360 ON vs 5.8440 OFF) and changes the d0 digest.

Two things generalise out of this, both now written into the code:

1. **Check a param's DEFAULT before trusting a `> 0` test.** Several `CardParams` ints default to 1
   or -1 rather than 0. This is the same class as the `EnvOn` rule in the coding-conventions skill —
   a presence test that reads as "off" but means "on".
2. **Classify against the ENGINE's own predicate.** `ManaPayment.cpp` already decides what a subtype
   cost reducer is (`!reduces_spell_subtype.empty() || chooses_creature_type`, plus the coloured-pip
   twin). That is now the shared `IsSubtypeCostReducer` helper both bucket policies call, rather than
   each provider re-deriving it.

A third defect in the same function was found the same way and is worth recording separately: the
payoff subtype test read `CardHasSubtype(ap.hand[i], tribe)` **on the hand card**. A hand card is a
name-only placeholder (`DeckLoader::MakePlaceholder`) with no subtypes, so that test was false for
every card whenever a Dragonspeaker Shaman was visible to supply `tribe`. Characteristic reads
outside the battlefield must go through the definition — the note at the top of `SpellEffects.h`
says exactly this, and the code did it correctly two lines above for `IsCreature`.

## What the deck actually does today

Generic `CleanupDiscardRanking` tier B is **descending mana value**. On this decklist that sheds:

    Utvara Hellkite (8) -> Lathliss (6) / Inferno (6) -> Glorybringer (5) / Scourge (5)
      -> Atsushi (4) -> Urza's Incubator (3) / Dragonspeaker Shaman (3) -> ...

i.e. it discards the deck's payoffs in almost exactly descending order of importance, and keeps
Lightning Bolt and Sol Ring. This is the "most expensive = most expendable" inversion the skill
warns about, and Dragons is the purest example of it in the fleet: the expensive cards ARE the deck.

## The deck's roles

| Role | Cards |
|---|---|
| **Mana — lands** | 18 Mountain, 3 Gruul Turf (Karoo), 2 Haven of the Spirit Dragon |
| **Mana — rocks** | 1 Sol Ring, 2 Mind Stone, 1 Fire Diamond |
| **Cost reducers** | 4 Urza's Incubator ({3}, -2 on creature spells of the chosen type), 2 Dragonspeaker Shaman ({1}{R}{R}, -2 on Dragon spells) |
| **Force multipliers** | 4 Dragon Tempest ({1}{R} — haste to entering Dragons + ETB ping), 1 Lightning Greaves |
| **Dragons (payoffs)** | 3 Utvara Hellkite (8), 3 Lathliss (6), 3 Inferno (6), 2 Glorybringer (5), 4 Scourge of Valkas (5), 3 Atsushi (4) |
| **Reach / removal** | 4 Lightning Bolt |

## Proposed buckets (quota-first, net of board, distance-to-playable)

**1 — MANA.**
* *Lands.* Quota = **enough to have 4-5 lands on the battlefield**, counting the board first (user,
  2026-08-30). Beyond a fifth land, further lands are overflow and sheddable. Red coverage is
  effectively free (18 of 23 lands are Mountains).
* **KAROO CAVEAT — applies here exactly as it does to Minotaur.** A Gruul Turf carries
  `etb_bounce_land`, so with no other land it must return **itself** and is a blank, not a land. It
  counts toward the land quota **only if** the board holds another land or the hand holds another
  non-Karoo land. This was found on Minotaur's Rakdos Carnarium (regression seed 1001 gi=27 kept two
  Karoos and no other land and played zero lands in eight turns); Dragons runs three of the same
  effect, so the caveat transfers without needing to be rediscovered.
* *Rocks.* Sol Ring first (it is the only one that nets +2 and it costs {1}), then Mind Stone, then
  Fire Diamond (enters tapped). Rocks are how a 6-8 drop gets cast a turn early, so they sit ABOVE
  surplus lands once the land quota is met.

**2 — COST REDUCERS. Quota 1-2** (user: "keep some number of cost reducers"). These are what make
the top of the curve reachable at all: Urza's Incubator and Dragonspeaker Shaman each take **2 off
every Dragon**, which turns Utvara from 8 to 6 and Lathliss from 6 to 4. Net of board — a reducer
already in play satisfies the quota. My proposed quota is **2 while no reducer is on the battlefield,
1 once one is**, and I flag it as the number I am least sure of.

**3 — DRAGONS. Quota 2 minimum, and deliberately FEWER than a threats-bucket instinct suggests**
(user, 2026-08-30: "you might keep fewer dragons, since they can be expensive... 2 at minimum").
A hand of five Dragons and two lands is a hand that does nothing; the third and fourth Dragon are
worth less than the land or reducer that would let the first one resolve. Order, best kept first:

1. **Scourge of Valkas** (5) — ETB ping scaling with Dragon count, and a firebreathing sink.
2. **Glorybringer** (5) — haste, so it demands the least support to matter.
3. **Lathliss** (6) — every other Dragon entering brings a 5/5 with it.
4. **Atsushi** (4) — cheapest Dragon, and its death gives value back.
5. **Inferno of the Star Mounts** (6).
6. **Utvara Hellkite** (8) — the biggest payoff and the hardest to cast; first to shed among Dragons
   whenever mana is short, which is the common case.

**4 — A 2-MANA ENABLER, IF THERE IS SPACE** (user, 2026-08-30). This is a SOFT quota, not a hard
one: it is filled from overflow after mana, reducers and the Dragon floor are satisfied, and it
never displaces them. The candidates are all MV 2 and all cheap enough to deploy on a turn that
would otherwise be blank:

1. **Dragon Tempest** (`{1}{R}`) — the best of them, and the reason this bucket exists. It converts
   every later Dragon into immediate damage (haste) plus an ETB ping, so it retroactively improves
   every Dragon still to come. A second copy is close to dead.
2. **Mind Stone** (`{2}`) — ramp now, a card later.
3. **Lightning Greaves** (`{2}`) — haste for the Dragons that lack it; overlaps Tempest, so it is
   worth much less when a Tempest is already kept or on board.
4. **Fire Diamond** (`{2}`) — enters tapped, so it is the weakest of the four.

"If there is space" is the operative clause: a hand that is short on lands or holding only two
Dragons should not be keeping a Tempest over the card that makes the deck function.

**5 — REACH: Lightning Bolt.** Kept behind the Dragon floor; it is the deck's only interaction and
its cheapest possible play, so it rarely competes for a shed slot.

## State-dependent rules

* **Distance-to-playable, reducer-aware.** Order the shed inside an over-full Dragon bucket by
  distance to castable across board+hand — and a resolved cost reducer **erases 2 of that distance
  for every Dragon**, which can promote Utvara above a 5-drop. This is the one place where the
  generic max-MV rule accidentally agrees with us (shed the biggest when mana is short) and the one
  place it must stop agreeing once a reducer is online.
* **Haven of the Spirit Dragon is not a red source for spells.** It produces any colour but is
  `colored_creature_only`, so it cannot pay for Lightning Bolt. It counts toward the land quota for
  Dragons and not for reach.
* **A shed Dragon is not fully lost.** Haven can return a Dragon from the graveyard for `{2}`, so
  with a Haven on board the first Dragon shed is partially recoverable. Weak effect, listed so the
  ranking does not double-count the loss.

## Honest assessment

Unlike Minotaur, I do **not** think this one is optional. The three things that make it worth
shipping:

1. The fallback is actively inverted on this deck (it sheds payoffs largest-first), rather than
   merely arbitrary.
2. The rule fires 661k times per 200 games inside the search, ~100% of them in the land-light shape
   where the choice is hardest.
3. Dragons ships a value leaf trained on rollouts that used this ranking, so the ranking is baked
   into the model's idea of what a position is worth.

What I cannot promise is a metric win. The bar the skill sets is **non-inferiority**, and the
honest expectation is that avg win turn moves little — the payoff is doctrine correctness and
rollout fidelity.

## What was actually measured

| run | result |
|---|---|
| `c0399210`, the BUGGED implementation | smoke+regression −4 turns/3,500 games; overnight (held out, 4x larger) +3/14,000; combined −1 over 17,500 — i.e. metric-neutral, and measuring the classifier bug rather than this policy |
| repaired, direct A/B (250 games, d3, s1001) | 5.8360 with the policy vs 5.8440 without (−0.008 turns); d0 avg unchanged, digest differs |
| repaired, suite | rebaselined with Minotaur's — see the git log for the accepted tiers |

**The skill's rule-vs-searched zero-regret check is still UN-RUN** for this deck (it needs a FAN
lever this provider does not have). That was true when the policy shipped and it is still true; it
is recorded as un-run, not as passed.

Dragons was the first deck to earn its own provider **for a discard rule** — it was routed to
`GenericProvider` in the provider fix precisely because it had no measured hook to hold. Minotaur
followed the same route the next day.
