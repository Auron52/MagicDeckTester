# Minotaur — cleanup-discard BUCKET policy (SHIPPED 2026-08-30)

Authored per the analyze-deck skill's Stage 5i ("the AI gives it its BEST SHOT and reports to the
user"; user ruling 2026-08-21), amended by the user, and shipped as
`MinotaurProvider::CleanupDiscardCandidates` behind a default-on `MTG_MINOTAUR_BUCKET_DISCARD`.
Shipping it promoted the deck off `GenericProvider` onto a provider of its own, which is the rule
the routing comment there had been anticipating since the 2026-08-21 misroute fix.

See "What shipped, and where it differs from this document" at the bottom for the two places the
implementation had to decide something this document left open.

## Evidence

### The real-play census below is the WRONG DENOMINATOR (user, 2026-08-30)

The original version of this document sized the work on discards seen in finished games and
concluded the upside was "close to zero". **That reasoning was wrong by three orders of magnitude.**
The played game is not what the rule serves — the SEARCH is, and the search sheds constantly inside
its rollouts. Measured with `MTG_SHED_STATS=1`, 200 games at the shipped d5/b40:

| | sheds in REAL play | sheds inside the SEARCH | ratio | taken with <4 lands |
|---|---|---|---|---|
| **Minotaur** | 99 | **250,265** | **2,528x** | **100%** |
| (Dragons, for scale) | 66 | 661,269 | 10,020x | 99.6% |

Index 0 of this ranking decides every one of those **with no search above it**, so a low real-play
count does not make the rule inert — it makes it invisible. And every single Minotaur rollout shed
is taken with fewer than four lands out, which is exactly the state where "shed the most expensive
card" is least defensible, because at 2-3 lands the 5-drops it pitches are the cards the deck is
trying to reach.

### The real-play census (`analyze_deck.py --discard-analysis`, 400 games d3)

Kept because it is still the right measure of one narrow thing — how often a shed decides a game
that was actually played:

| | |
|---|---|
| cleanup discards observed | 10 decisions in 400 games |
| hand sizes seen | hand8 only (i.e. exactly one card over the limit) |
| status-quo optimal rate | 0.90 (mean regret 0.10) |
| a flat derived order | 1.00 optimal (mean regret 0.00) |
| verdict | `STATUS_QUO_OK` |

The deck curves out, empties its hand by ~T4 and wins ~T5, so in real play it sheds rarely. That
bounds the *direct* metric upside; it says nothing about the 250k rollout decisions.

## The deck's roles

| Role | Cards |
|---|---|
| **Mana — lands** | 5 Mountain, 2 Blood Crypt, 2 Rakdos Carnarium (Karoo), 4 Unclaimed Territory, 4 Secluded Courtyard, 1 Cavern of Souls, 2 Bloodstained Mire |
| **Mana — enabler** | 2 Aether Vial (free creature deployment; `vial_target_mv` 3) |
| **Force multipliers** | 4 Kragma Warcaller (haste + "a Minotaur attacks → +2/+0"), 4 Rageblood Shaman (+1/+1), 3 Neheb (≤1-hand anthem +2/+0), 2 Sethron (2/3 token per nontoken Minotaur) |
| **Cost enabler** | 4 Ragemonger ({B}{R} off every Minotaur spell) |
| **Reach** | 3 Fanatic of Mogis (ETB damage = devotion to red — the deck's ONLY non-combat damage) |
| **Bodies** | 2 Boros Reckoner 3/3, 4 Slaughter-Priest 2/2, 4 Deathbellow Raider 2/3, 4 Burning-Fist 2/1, 4 Gnarled Scarhide 2/1 |

## Proposed buckets (quota-first, net of board, distance-to-playable)

**1 — MANA.**
* *Lands.* Quota = enough to reach **5 mana** counting the battlefield first. **The deck never needs
  more than 5** (user, 2026-08-30) — Sethron and Kragma Warcaller at 5 are the top of the curve, and
  Ragemonger takes {B}{R} off every Minotaur below them, so a sixth mana source buys nothing and is
  overflow. Colour coverage (B and R) comes first but is nearly free here, because 9 of the 18 lands
  make any colour.
* **KAROO CAVEAT (deck-specific, and the one real finding).** A Rakdos Carnarium counts toward the
  land quota **only if** the board already has another land, or the hand holds another non-Karoo
  land. With no other land it must bounce itself (CR: "return a land you control" — it is the only
  one), so it is a blank, not a land. This was found for real: regression seed 1001 gi=27 mulliganed
  to 5, kept two Karoos and no other land, and played **zero lands in eight turns**. The engine's
  play was correct; the hand was dead on keep.
* *Aether Vial.* Quota 1. A second Vial is close to dead once the first is online.

**2 — THREATS (the catch-all).** Deck-specific value order, best kept first:
1. **Kragma Warcaller** — haste turns every future Minotaur into immediate damage, and its attack
   trigger is the single largest damage swing in the deck.
2. **Rageblood Shaman** — +1/+1 across the whole board including Sethron's tokens.
3. **Sethron** — a 4/4 plus a 2/3 per nontoken Minotaur, and a mana sink.
4. **Neheb** — the ≤1-hand anthem, which this very shed can switch on (see the promotion below).
5. **Fanatic of Mogis** — the only reach; the card that wins a stalled board.
6. **Boros Reckoner** — 3 power AND 3 red devotion for Fanatic.
7. Slaughter-Priest / Deathbellow Raider / Burning-Fist — 2-power bodies.
8. **Gnarled Scarhide** — cheapest body, shed first among threats.

**3 — MANA (again): Ragemonger.** Keep 1 while none is resolved — *"Ragemonger is also quite
helpful when dealing with mana problems. It's usually a good idea to keep 1 of them"* (user,
2026-08-30).

**This supersedes what this document originally said**, which was that Ragemonger should be kept
only *after* a floor of 3-4 threats, on the reasoning that a discount is worthless without Minotaurs
to discount. That reasoning treats the card as a payoff-multiplier. It is better read as MANA: it
takes {B}{R} off every Minotaur spell, which is worth about two lands, and the state it fixes —
land-light, threats stranded in hand — is the state **100% of this deck's sheds are taken in**. A
card that answers the problem cannot be the last thing kept while facing it.

The threat floor of 3-4 still holds; the two only compete in a hand that has exactly the floor's
worth of threats plus a Ragemonger, and there the Ragemonger now wins the second slot.

## State promotions (where this deck is genuinely unusual)

* **The Neheb inversion — discarding is a BENEFIT, not a loss.** With Neheb on the battlefield,
  getting to ≤1 card in hand gives EVERY Minotaur +2/+0. With three Minotaurs out that is +6 damage,
  which beats essentially any single card in hand. So when Neheb is in play, the shed should be
  taken without hesitation and the ranking should simply pick the lowest-value card. (The *cast*
  side of this — preferring to empty the hand — belongs to the search, not to us.)
* **Burning-Fist ammunition.** A hand card is also fuel: `{1}{R}` + discard = +2/+0. A card that is
  nowhere near castable is worth more as fuel than as a dead card — again a cast decision, but it
  means an uncastable card in hand is not simply waste.
* **Devotion promotion.** With a Fanatic of Mogis in hand and the opponent within reach, Boros
  Reckoner promotes above the other 3-drops: it is worth 3 devotion, i.e. 3 extra Fanatic damage.
* **Distance-to-playable.** Kragma / Sethron (5 mana) shed before a 2-drop when board+hand mana is
  ≤3 — *unless* a Ragemonger is already online, which takes {B}{R} off every Minotaur and erases the
  distance.

## Honest assessment — read this before approving

The bar the skill sets is **non-inferiority**, and the value is "doctrine quality plus rollout/gen
fidelity".

**This section previously recommended treating the policy as optional, on the grounds that there
are only 10 sheds in 400 games. That recommendation is WITHDRAWN** — the count was the wrong
denominator (see the evidence section). The rule runs a quarter of a million times per 200 games
inside the search, 100% of them in the land-light state, and decides each with no search above it.
"It barely fires" was an artefact of measuring the played game instead of the rollout.

What has NOT changed is the expected effect on the metric. The status quo still picks the
searched-optimal card 9 times out of 10 in real play, so avg win turn is unlikely to move much; the
case for shipping is doctrine correctness and rollout fidelity, not a number. The difference is that
those are now the *stated* payoff rather than a consolation for a payoff that was never really
absent.

Recommendation:
* **Approve the KAROO CAVEAT as a MULLIGAN rule regardless.** That one is worth real turns — it cost
  a whole game outright — and it is independent of everything else here. It applies to Dragons' three
  Gruul Turf as well; see `dragons-discard-policy-proposal.md`.
* **Ship the bucket policy** as `MinotaurProvider::CleanupDiscardCandidates` behind a default-on
  `MTG_MINOTAUR_BUCKET_DISCARD`, validated per the skill: rule-vs-searched labels with zero regret,
  then smoke + regression through the accept flow. Note this promotes Minotaur from `GenericProvider`
  to a provider of its own, which the routing comment in `SelectDecisionProvider` already anticipates
  ("If the proposed cleanup-discard bucket policy is ever approved and measured, THAT is when this
  becomes a MinotaurProvider").

## What shipped, and where it differs from this document

Two things the buckets above did not pin down, both settled in the implementation:

**1. The quotas are INTERLEAVED, not filled bucket-by-bucket.** "Five mana sources" and "3-4
threats" are both constraints on the same seven-card hand, so the ranking has to say which land
beats which threat. The shipped keep priority is:

    land1 > threat1 > Vial > land2 > Ragemonger > threat2 > land3 > threat3 > land4 > land5 > threat4

Read forwards it is the quota fill; read backwards it is the order the quota-protected cards give
way in, which matters because the ranking names the WHOLE hand (see below). A bucket-at-a-time fill
gets that tail wrong in a way that showed up immediately in an `MTG_TRACE=discard` probe: it
protected a fifth land ahead of a castable Boros Reckoner at four lands in play.

**2. The list names every card in the hand.** Anything it omits falls through to the shared tier B —
descending mana value — which is the ranking this provider exists to overturn. Naming everything
also means the Neheb inversion needs no special case: index 0 is always the least valuable card, so
"with Neheb out, just shed the cheapest thing" is already what happens.

Two state promotions from the section above are deliberately NOT implemented: Burning-Fist's
"a dead card is firebreathing ammunition" and the full form of the devotion promotion ("...when the
opponent is within reach"). Both need a damage projection, which makes them CAST decisions for the
search rather than facts a cleanup ranking can establish. The devotion tie-break among equal bodies
did ship, and on this decklist it reproduces the worked example (Boros Reckoner ahead of the other
bodies) without the reach term.

## Measured

| run | result |
|---|---|
| smoke, all three Minotaur cells | d0 5.4890 → 5.4780, d3 4.9640 → 4.9520, d5 4.9733 → 4.9600 (every cell faster) |
| direct A/B, 250 games d3 s1001 | 4.9520 with the policy vs 4.9640 with the generic fallback |
| shed census, same 250 games | Kragma shed 30x vs 44x under generic; Fanatic 11x vs 17x; dead Rakdos Carnarium 10x vs 4x; cheap bodies (Scarhide, Deathbellow Raider) 11x vs 1x |

The shed census is the more informative half: the policy stops pitching the deck's best card and
starts pitching dead Karoos and spare 2-drops, which is the behaviour change the doctrine asked for.
It does NOT stop shedding the 5-drops entirely, because the distance-to-playable rule deliberately
demotes them while reach is 3 or less — and 100% of this deck's sheds happen land-light.

**The skill's rule-vs-searched zero-regret check is UN-RUN** (it needs a FAN lever this provider
does not have), exactly as for Dragons. Recorded as un-run, not as passed.
