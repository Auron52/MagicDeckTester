# Minotaur — cleanup-discard BUCKET policy (PROPOSAL, awaiting user approval)

Authored per the analyze-deck skill's Stage 5i ("the AI gives it its BEST SHOT and reports to the
user"; user ruling 2026-08-21). **Nothing here is implemented.** The deck currently uses the
generic max-MV fallback ranking, which is the thing the user called "too arbitrary" — but see the
honesty note at the bottom before deciding whether it is worth shipping.

## Evidence (`analyze_deck.py --discard-analysis`, 400 games d3)

| | |
|---|---|
| cleanup discards observed | **10 decisions in 400 games** |
| hand sizes seen | hand8 only (i.e. exactly one card over the limit) |
| status-quo optimal rate | 0.90 (mean regret 0.10) |
| a flat derived order | 1.00 optimal (mean regret 0.00) |
| verdict | `STATUS_QUO_OK` |

The deck almost never sheds: it curves out, empties its hand by ~T4, and wins ~T5. That is the
single most important fact for sizing this work.

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
* *Lands.* Quota = enough to reach 4 mana counting the battlefield first; colour coverage (B and R)
  comes first, but is nearly free here because 9 of the 18 lands make any colour.
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

**3 — ENABLER: Ragemonger.** Kept only after a floor of 2 threats (it does nothing on an empty
board), per the user's "deprioritize enablers to ensure at least 2 threats" rule.

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
fidelity". On this deck the measurable upside is close to zero: **10 sheds in 400 games**, and the
status quo already picks the searched-optimal card 9 times out of 10. Shipping this would be for
doctrine correctness (and for the rollout, which always sheds heuristically), not for the metric.

My recommendation: **approve the KAROO CAVEAT as a MULLIGAN rule** (that one is worth real turns —
it cost a whole game outright), and treat the rest of the bucket policy as **optional**. If you do
want it shipped, it goes in as `MinotaurProvider::CleanupDiscardCandidates` behind a default-on
`MTG_MINOTAUR_BUCKET_DISCARD` flag, validated per the skill (rule-vs-searched labels with zero
regret, then smoke + regression through the accept flow).
