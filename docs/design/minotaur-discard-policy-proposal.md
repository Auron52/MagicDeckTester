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

~~**The skill's rule-vs-searched zero-regret check is UN-RUN** (it needs a FAN lever this provider
does not have), exactly as for Dragons.~~ **Both halves of that sentence were wrong** — see
`per-deck-discard-analysis-phase.md`. No FAN lever was ever needed (this provider returns the full
hand already); the real obstacle is that the labeller probes only the CR 514.1 cleanup, which this
deck reaches ZERO times. Its 118 real discards per 200 games happen at Burning-Fist's activation
cost and Neheb's trigger. The axis was BOUNDED instead: best-vs-worst is +0.00025t at d3 and
+0.00042t at d5 (nothing to fix at shipped depth), +0.01075t at d0.

## Revised doctrine (user, 2026-09-01) — implemented, measured, NOT adopted

> "When you have enough mana we should ditch mana sources... enough sources between hand and board
> that is. Aether vial is a good choice given that we will be later than turn 1 when we do so.
> Otherwise we should focus on dropping threats in order of playability and effectiveness."

The user also ruled out searching this decision: *"I would rather not do that where we can come up
with an effective heuristic"* — which the bound above independently agrees with, since a searched
axis could not beat +0.00025t at d3.

1. **"Enough mana → ditch mana sources"** is ALREADY what ships. `land_need` keeps at most
   `kLandTarget` sources counting **board and hand together**, so hand lands past the quota are
   surplus and shed first at S1. No change made.
2. **Aether Vial sheds with the mana** (`MTG_MINOTAUR_DISCARD_VIAL`): drop the `S_VIAL` slot from the
   ladder, so a Vial is never quota-protected and falls to the surplus shed behind the lands. V1 kept
   one at ladder slot 3, above land2 and threat2.
3. **Threats by playability, then effectiveness** (`MTG_MINOTAUR_DISCARD_PLAY`): replace V1's binary
   far-flag (which fired only at `reach<=3 && eff_mv>=5`, i.e. deficit >= 3, so at reach 4 a 5-drop
   and a 2-drop counted equally playable) with a graded `deficit = eff_mv - reach` bucket.
   `MTG_MINOTAUR_DISCARD_PLAY2` is the same with a looser slack (2 instead of 1).

**It changes the right decisions.** 9 of 291 genuine choices (3.1%) flip, and the first divergence is
the doctrine working exactly as stated: at `lip=2`, V1 sheds Boros Reckoner (MV 3, one land short) to
keep Fanatic of Mogis (MV 4, two short) on deck-rank alone; the playability rule sheds the Fanatic.

**But every arm measures WORSE, and the gradient is monotone in how hard playability is weighted:**

| arm (d0, 80,000 games, 16 seeds, paired) | mean vs V1 | t | changed | faster | slower |
|---|---|---|---|---|---|
| Vial sheds with the mana | +0.00010 | +2.31 | 12 | 2 | 10 |
| playability, slack 1 | +0.00015 | +2.83 | 18 | 3 | 15 |
| playability, slack 2 | +0.00089 | +5.46 | 157 | 45 | 112 |

(positive = slower = worse; d3 is 0 or 2 changed games out of 4,000 for every arm, as the bound
requires). The slack-2 row is the informative one: loosening the threshold makes the rule fire ~9x
more often and get ~6x worse, so this is a direction, not a calibration accident. My first guess was
the opposite — that slack 1 was too harsh — and the measurement refuted it.

**Read the size before reading the sign.** The worst arm costs 0.0009 turns/game at d0 on 157 games
in 80,000, and **at shipped depth every arm is null by construction** — the axis's whole headroom
there is 0.00025t. So this is not "the doctrine is wrong"; it is "the metric cannot endorse it, and
where it can see anything at all it leans the other way."

**Status: levers exist, all default OFF, engine byte-identical.**

### Round 2 — the EV model, and a correction to the Vial claim

The user refined the doctrine across several passes, ending at the right formulation:

> "Essentially we want something like expected value = probability of playing * value of playing."
> ... "dropping Kragma Warcaller because we can't quite cast it with our existing mana would be
> wrong, but shedding a warcaller when we have 2 mana total and no Ragemonger is much more likely to
> be right." ... "Warcaller is also very high value, so almost being able to play it means it should
> be kept." ... "we don't want multiple Warcallers, since they aren't that easy to play and end the
> game quickly with 1."

This diagnosed the round-1 failure exactly. A lexicographic "playability, then effectiveness" sort
is the wrong SHAPE: it sheds the deck's best card the moment it is two sources short. Multiplying
keeps it, because a high value survives a modest probability discount. Three new levers:

- `MTG_MINOTAUR_DISCARD_EV` / `_EVHARD` — `EV = P(play) x value`, value = inverted `threat_rank`,
  `P` decaying in sources-still-needed. Two decay curves, because the steepness is a judgment call.
- `MTG_MINOTAUR_DISCARD_DUPES` — the k-th copy's mana requirement is CUMULATIVE (to cast the second
  Warcaller you pay for both), so P collapses on its own. Needs no per-card constant and leaves
  cheap bodies alone: two Raiders at four sources are both castable, two Kragmas need ten.
- `MTG_MINOTAUR_DISCARD_REDUCER` — a Ragemonger we HOLD and can deploy discounts the curve, not
  only a resolved one. V1 counted `board_reducers` only, so it judged Kragma unreachable at three
  sources even with a Ragemonger in hand.

**Every arm is clearly worse at d0 and a wash at d3** (160,000 / 16,000 games per arm, paired,
fresh seeds; positive = slower):

| arm | d0 mean | t | changed | sign p | d3 mean | t | sign p |
|---|---|---|---|---|---|---|---|
| ev | +0.000894 | +6.52 | 412 | <0.0001 | -0.000125 | -0.41 | 0.84 |
| evhard | +0.000362 | +2.87 | 351 | 0.025 | -0.000063 | -0.28 | 1.00 |
| ev+dupes | +0.001000 | +6.77 | 506 | <0.0001 | -0.000375 | -1.22 | 0.31 |
| evhard+dupes | +0.000562 | +3.80 | 516 | 0.0005 | -0.000188 | -0.77 | 0.61 |
| ev+dupes+reducer | +0.000994 | +6.70 | 511 | <0.0001 | -0.000375 | -1.22 | 0.31 |
| evhard+dupes+reducer | +0.000619 | +4.24 | 499 | 0.0001 | -0.000188 | -0.77 | 0.61 |

Unlike round 1 these fire often enough to be properly measured (351-516 changed games, not 12).
The d0 verdict is unambiguous and the d3 column is null in every row. Note the signs are OPPOSITE
across depths — d0 prefers V1, d3 leans (insignificantly) toward EV. Do not build a story on the
d3 column; it is not significant anywhere.

### Round 3 — EV2: the value term WAS the weak link. ADOPTED.

Fixing `value` turned the whole model positive. `MTG_MINOTAUR_DISCARD_EV2` replaces the 6-bucket
`threat_rank` with **the position in V1's FULL value order** (rank, then power/devotion/mv), so
value is a total order with no ties left to collapse into.

**Why the bucket version had to lose.** Rank 5 is a huge bucket — every plain body (Gnarled
Scarhide, Deathbellow Raider, Burning-Fist, Slaughter-Priest, Boros Reckoner) *plus* a surplus
Ragemonger. Inside it `value` was CONSTANT, so `EV = P x const` degenerated to sorting on distance
alone — exactly the playability-first arm that measured worst in round 1. A behavioural diff caught
it red-handed: plain EV sheds **Ragemonger**, the card the user said to keep for mana.

Two properties fall out of the total-order fix: with P equal the sort reproduces V1 EXACTLY (so it
degrades gracefully instead of scrambling), and distance can only reorder cards of genuinely
different value. It stays aligned with V1 for 834 decisions where plain EV diverged after 44, and
changes 15 of 516 genuine choices.

**Adopted combination: `EV2` + HARD decay + `DUPES`**, behind `MTG_MINOTAUR_EV_DISCARD`
(default ON; `=0` restores V1 exactly, digest-verified). Measured on THREE independent seed blocks,
the third of which was a pre-registered held-out confirmation of a single named arm:

| block | d0 | d3 | d5 |
|---|---|---|---|
| selection (32/16 seeds) | -0.000294 (t=-1.73) | -0.000375 (t=-1.50) | — |
| held-out (100/64 seeds) | -0.000110 (t=-1.16) | **-0.000313 (t=-2.46, p=0.019)** | — |
| third (64 seeds) | — | **-0.000422 (t=-2.80, p=0.0067)** | -0.000130 (t=-0.69) |

Pooled d3 ~ **-0.00037 turns/game**, negative in all three blocks; d0 neutral (it was +0.00089
before EV2 — the regression is gone); d5 neutral. Per-seed at d3: 18/10 and 31/12 better.

Suite: **net 0 turn-units on smoke AND on regression**, with searched depths **2 faster / 0 slower**
across both tiers; the handful of d0 slowdowns are single-seed noise against a 500,000-game held-out
d0 read of -0.000110. GT accepted for both tiers, 320 gt_logs consistent.

**Note this exceeds the earlier best-vs-worst bound (+0.00025t at d3) — legitimately.** That bound
was measured with `MTG_NONCLEANUP_SHED_WORST`, which only reaches
`ChooseNonCleanupDiscardIndex` (the cost/trigger site). These levers change
`CleanupDiscardCandidates`, which ALSO feeds the ROLLOUT cleanup, so their reach is strictly larger
than what that bound covered. The bound was never wrong; it just bounded one of the two channels.

**Still the honest next step: the `value` term is a proxy.** `value` is currently the
authored `threat_rank` inverted, i.e. a linear 6..1 guess. The deck's profile carries LEARNED
`card_scores`, which is what the user's `value(playing)` actually means — but they live on the
profile (`AIEngine`), not `GameState`, so the provider cannot reach them without plumbing the
profile into the hook. Until that is done, "EV measured worse" is really "EV *with a guessed value
scale* measured worse", and the model itself is not fairly tested.

### CORRECTION — the round-1 Vial claim was over-read

Round 1 reported the Vial change as measuring worse (+0.00010, t=2.31). **That does not hold up.**
Re-run at 4x the sample (160,000 d0 games, 32 seeds): mean +0.000081, **t=1.94** — it went DOWN —
39 changed games of 160,000, exact sign test p=0.024. At d3: 3 changed games of 16,000, p=1.00.
A behavioural diff finds the rule firing in **1 of 462 decisions**, and that one swap is the rule
doing exactly what the doctrine asks: V1 sheds **Ragemonger** to protect the Vial, the new rule
sheds the Vial and keeps the Ragemonger — the card the user specifically said to keep for mana.
So shedding the Vial is NOT established as incorrect; it is barely measurable in either direction.

### Round 4 — is the LEARNED `card_scores` order the better `value`? (measurement in flight)

Round 3 closed with "the `value` term is a proxy", and named the deck's learned `card_scores` as
what `value(playing)` ought to mean. Round 4 plumbs them through and measures it. Two things had to
be built first, and both are reusable:

**1. `GameState::m_card_scores`.** A borrowed pointer to `MulliganProfile::card_scores`, stamped
beside `m_required_pieces` in `AIEngine::HandleMulligan` and at all seven analyzer rollout-harness
sites, propagated through every deep copy. Stamping *every* site is the point, not tidiness: if play
saw the scores and the search rollouts did not, the rollout would be evaluating a different policy
than the one the game executes. Verified inert with the arms off — smoke 48/48, **0 configs changed,
0 play-changed**. The `Dominance.h` size tripwire fired on the new field and is answered there: the
whole family of borrowed profile pointers is a per-game constant and cannot distinguish two futures.

**2. A trace-tearing fix, which invalidated the first attempt at this round's own diff.** `TRACE()`
emitted each line as **three separate `stderr` calls** (prefix, body, newline). Every runner here is
multi-threaded, so concurrent tracers interleaved MID-LINE: the first behavioural diff of these arms
reported 40–52% of decisions changed, built on records with other records spliced into them. With
one buffered write per line the same comparison reports 3–22%. This was never Minotaur-specific —
**all 17 trace streams in the repo emitted torn lines under any multi-game run**, so any past
analysis that parsed a trace stream from a threaded run deserves a second look. The discard line
also now leads with `g<game_seed>`, without which decisions cannot be attributed to a game at all.

**READ THE UNITS BEFORE BELIEVING THE PREMISE.** `card_scores[c][k]` is
`avg_win_turn(k copies of c in the OPENING HAND) - avg_win_turn(k+1 copies)` — an unadjusted
group-mean difference (`AnalyzerEngine::ComputeCardScores`). So it is *not* "the value of playing
this card on turn 5": it is an opening-hand quantity, and it is confounded with castability, since a
hand holding a five-drop holds one fewer cheap card. `AIEngine`'s own consumer clamps the negative
half to zero and its comment calls that half selection bias. Using it as `value` therefore risks
**double-counting mana** — `P(play)` already discounts what is hard to cast, and `card_scores`
discounts it again. The measurement is designed around that risk rather than ignoring it.

The learned order over this deck's creatures, for reference (first-copy marginal):

    Scarhide .335 > Ragemonger .233 > Kragma .140 > Burning-Fist .069 > Fanatic .008
      > Slaughter-Priest -.081 > Rageblood -.087 > Neheb -.088 > Raider -.128
      > Reckoner -.175 > Sethron -.193

It is *not* simply cost-ordered — it puts Kragma (5 mana) third and demotes Rageblood, Neheb and
Sethron, which the authored order ranks 1/2/3. So it genuinely disagrees with the shipped policy
about which cards are the payoffs, and the disagreement is not reducible to curve position.

**Three arms, chosen to discriminate** (a mechanism that fits is not one that separates):

| arm | what it changes | what a win would mean |
|---|---|---|
| `MTG_MINOTAUR_DISCARD_CSVAL` | value ORDER from `card_scores`, `P` kept, same `0.85^pos` spread | the learned order is better information |
| `MTG_MINOTAUR_DISCARD_CSNOP` | the same order with `P` DROPPED | `card_scores` were already an EV; multiplying double-counted mana |
| `MTG_MINOTAUR_DISCARD_CSDUP` | adopted order kept; the learned per-copy marginal decides whether the duplicate penalty applies at all | the learned MARGINAL is usable even if the learned LEVEL is not |

`CSVAL` vs `CSNOP` is the discriminating pair — either alone would be a number that cannot tell the
two explanations apart. Only the ORDER is swapped, never the spread, so a result attributes to the
ordering and cannot be confounded by a scale change.

`CSDUP` is the targeted one, and the argument for it is that a *marginal* is a cleaner learned
quantity than a *level*: both of its groups contain the card, so the castability confound largely
cancels. It also expresses something no cost-shaped rule can. The shipped `DUPES` rule charges the
k-th copy cumulative mana, which gets Kragma right (5 mana; learned 2nd copy **-0.275**) but must
get Rageblood wrong (3 mana; learned 2nd copy **+0.038** — a second lord is genuinely fine).

**Behavioural diff first** (8,000 d0 games, `test/tools/discard_behaviour_diff.py`, paired within a
game and stopped at each game's first divergence — after that the arms are playing different games):

| arm | changed | signature |
|---|---|---|
| `CSVAL` | 312 / 5,725 (5.5%) | sheds Rageblood/Neheb/Sethron more; keeps Slaughter-Priest, Burning-Fist, Ragemonger |
| `CSNOP` | 1,112 / 4,980 (22.3%) | overwhelmingly **keeps Kragma** — with no `P`, its cost never discounts it |
| `CSDUP` | 98 / 5,911 (1.7%) | **keeps a second Rageblood** — and, after the fix below, nothing else |

Every arm fires, and each signature is the mechanism doing what it was built to do — so the outcome
A/B is measuring something real in all three cases. Outcome numbers to follow: one pooled batch,
256 jobs / 1.28M games, arms × {d0, d3} × 32 seeds, on selection seeds 5.0M–5.31M (disjoint from
every prior Minotaur sweep; 6.0M+ held back for confirmation).

**The diff earned its keep before any outcome number existed: `CSDUP` was measuring a bug.**
`learned_marginal` indexed the marginals vector with `AIEngine::CardScore`'s CLAMP. That clamp is
right for SCORING — marginals diminish, so reusing the last known one understates safely — and
wrong for the question this arm asks. `ComputeCardScores` stops the vector at the first copy count
with under 30 samples, so **a one-entry vector means no evidence about the second copy**, and
clamping answers with the FIRST copy's value. For a card whose first copy is good, that says "a
second one is fine" on the strength of nothing.

On this decklist the 3-ofs and 2-ofs (Fanatic, Neheb, Boros Reckoner, Sethron) all stop at one
entry, and **Fanatic's is positive** — so the arm waived Fanatic's duplicate penalty outright,
which the diff caught happening **30 times in 172 changes**. About a sixth of the arm was measuring
the defect rather than the mechanism. Fixed to require a real entry at index `k` and otherwise fall
through to the shipped cumulative-mana rule; after the fix **all 98 changes are
`Rageblood Shaman -> ...`**, which is exactly and only what the arm exists to test — Rageblood being
the one card here with a recorded, positive second-copy marginal.

The selection batch was **stopped and relaunched clean** rather than re-running the one arm later:
a per-arm re-run is the split-pool pattern this repo forbids, and it was my own experiment to stop.

**A caveat found while the batch ran, and it reframes what a LOSS here would mean.** This deck's
`card_scores` were computed by its **first** analysis, `a43ef60f` (2026-08-23), and that is the only
commit that has ever touched `Minotaur.profile.json`. **127 commits have touched `src/` since** —
including `MinotaurProvider` itself, the value leaf, the exhaustive mulligan profile and the whole
EV discard model. So the learned order is a fingerprint of an engine that no longer exists, and
specifically of one in which this deck had **no archetype provider at all**: every number in it was
measured while the generic max-MV fallback was making these very discards.

It went unnoticed because `card_scores` are nearly inert for this deck today —
`hand_score_threshold` is `-1e+18`, so the hand-score gate never binds, and their only live effect
is `AIEngine::CardScore`'s bottoming tie-break. Nobody thinks of them as a generated artifact, but
they are one, and Rule 0 for artifacts applies: they are an engine-state fingerprint.

This does not invalidate the measurement, it bounds the conclusion:

* if the stale learned order **wins**, that is a strong result — it beat the authored order while
  handicapped by describing a different engine;
* if it **loses**, that establishes only that *this* order is worse, NOT that learned value is worse
  than authored value. The honest follow-up is to regenerate `card_scores` on the current engine and
  re-measure. (Regeneration is a generation stage, so it must run alone on the box — it cannot be
  folded into this run.) Precedent cuts both ways: the value-leaf work found regeneration staleness
  to be neutral on five decks, so staleness is not automatically fatal.
