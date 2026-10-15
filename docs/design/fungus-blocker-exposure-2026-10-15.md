# Fungus: how much of the recommendation rests on the opponent never blocking (2026-10-15)

The round-27 recommendation was reached with a simulator that **cannot block**. This doc measures how
much of the list's case depends on that, because the user raised it as a condition on adopting the
list:

> *"there is a real risk of overcounting cards that rely on your opponent not blocking like
> Beastmaster or Shroofus. We may need to tweak around that."*
> *"Beastmaster may still be okay, but this is only certain if you can attack with enough creatures
> to enable it in one attack. That is 7 creatures or 4 with doubling season."*
> *"The problem for beastmaster is if we add counters from free attacks. In real games those attacks
> are often not a good idea because our 1/1 can be squished by a blocker."*
> *"Once the creatures get +5/+5 it is totally fine."*

That model is correct and it localises the defect exactly. This doc confirms it with numbers, and
then corrects two things I got wrong along the way.

## The engine has no blocker path at all, and the opponent's board is not empty

`src/ai/Combat.cpp` has no declare-blockers step; `Step::DeclareBlockers` (`GameState.h:104`) is an
unused enum value; the attack policy is a hard-coded *"goldfish default: attack with everything that
can attack (no blockers)"* (`DecisionProviders.cpp:2327`). So 100% of every attacker's power always
reaches the player, and attacking is free.

The opponent **does** have creatures. `GoldFishRunner::PopulateOpponentSpawns` schedules ten patterns
on `game_index % 10` and eight of them put bodies out (pattern 2 is six 1/1s by turn 3, pattern 3 is
three 2/2s, pattern 8 is a 6/6 behind a 1/1). `GameEngine.cpp:332` names each one
`"<power>/<toughness> Creature"`, so **every available blocker's exact P/T is a logged fact**. That
is what makes everything below a measurement rather than a thought experiment: the bodies were there,
they simply never blocked.

No existing lever changes this. `mtg --list-flags` has nothing that penalises a wide attack
(`MTG_COMBAT_SWAP_ATTACK_HOLD` is a Bruna aura-swap mana hold), so there is no cheap apparatus proxy.

## The measurement: the user's one-attack test, run exactly

Quest counters come from DECLARED attackers (`ApplyAttackQuestCounters`, CR 508.2) and each trigger
is its own event, so a Doubling Season doubles each separately — the user's "7 creatures or 4 with
doubling season" is precisely the engine's arithmetic. **The final list runs zero Doubling Season, so
the one-attack bar is the full seven**, and the quest-counter delta per turn is therefore *identically*
the declared-attacker count. No new instrumentation was needed: counters are already serialised per
permanent (`GameLogger.cpp:768`).

800 games of the recommendation at d7/b1000, seed 1230000 (fresh block):

| | |
|---|---|
| mean win turn | 4.763, 800/800 won by turn 8 |
| Beastmaster resolved | 371 (46.4%) |
| anthem switched on | 328 (41.0% of games) — **and it was the killing attack in 327 of 328** |
| **enabled in ONE attack (robust)** | **123 / 328 = 37.5%** |
| accrued over several attacks (exposed) | 205 / 328 = 62.5% |
| games that never turned it on | 472 (59.0%) — all won too, though note every game in this run won inside 8 turns, so the content of this row is "the deck has a clock that does not need Beastmaster in 59% of games", not a surprising survival rate |

Blocker-resilience floor on the enabling attack — with the anthem live every attacker deals at least
5 (a 0/0 Saproling Burst token at +5/+5), and the worst case is the opponent blocking the biggest
attackers, so `N - B >= 4` suffices from 20 life and `>= 6` from 30:

| | lethal through every body the opponent actually had |
|---|---|
| from 20 life (std) | 216/328 = **65.9%** |
| from 30 life (2hg) | 102/328 = **31.1%** |

Damage on that attack: median 41, p10 24, p90 53. Every percentage above **understates** the true
resilience, because the floor prices each attacker at 5 when the real board holds 6/6 and 5/5 Burst
tokens and lorded Saprolings.

**The exposure is concentrated in 2hg** — 31.1% against 65.9% — which is the format where
Beastmaster's measured edge is *largest* (−0.0993 more in 2hg, the screen's biggest format split,
round 13). Since std and 2hg are co-equal for the user, the overcount lands hardest on the half of
the case that is weaker in absolute terms.

## The counters that were bought with attacks a real opponent would punish

All of the exposure is in the pre-anthem banking attacks, exactly as the user said: blocking cannot
stop the anthem switching on, and once it is on every body is a 6/6. Banking attacks have the counter
total under seven by definition, so they are all made with un-pumped bodies.

Of the 289 attacks that actually banked counters, judged against the biggest body the opponent
actually had (free kill = their blocker's power ≥ our toughness and its toughness > our power):

| verdict | attacks | counters banked |
|---|---|---|
| **FREE KILL for the opponent** | 131 (45.3%) | 308 (44.3%) |
| **TRADE** (both die — still bad for the aggressor) | 51 (17.6%) | 112 (16.1%) |
| SAFE (blocker too small) | 47 (16.3%) | 120 (17.2%) |
| no blocker available | 60 (20.8%) | 156 (22.4%) |

**63.0% of banking attacks were punishable, carrying 60.3% of the counters.** The attackers are the
bodies the user named: **93.1% are 1/1 or 2/2** (54.7% / 38.4%). Worst on turn 3 at 72.7% punishable.
Only 38% of accrual games had no punishable banking attack.

Toughness is estimated as equal to power (`damage / attackers`), which is exact for this deck's
square bodies — 1/1 and 2/2 Saprolings, 2/2 Sporecrown, 2/2 Sapherd, and Burst tokens whose P/T are
both the fade counters remaining. The two non-square bodies (Brightcap Badger 3/4, Undercellar
Myconid 1/2) are *tougher* than they are powerful, so the approximation errs toward over-stating how
easily our attackers die: a "safe" verdict is trustworthy and a "punishable" verdict carries the
approximation.

## How much of it a blocker-aware player would get back for free

A real player facing blockers holds back and swings once with seven. In the 205 accrual games, bodies
available to attack with on the enabling turn (creatures present at the end of the previous turn):

| | |
|---|---|
| **≥7 — the robust route was available and the search took the free one anyway** | **72 (35.1%)** |
| short by 1 | 62 (30.2%) |
| short by 2 | 40 (19.5%) |
| short by 3–4 | 31 (15.1%) |

So about a third of the accrual games convert to the robust route at **no cost** — the bodies were
already there and the search simply had no reason to wait. The rest are short, but mostly by one or
two bodies, i.e. one more turn of development. Chaining it: 41% of games use the anthem × 62.5%
accrual × 64.9% genuinely short ≈ **17% of all games would see the anthem arrive about a turn later.**

That is a large distortion in the ABSOLUTE win turn — far larger than the screen's −0.0761 margin —
but it is **common-mode for the comparison**, because `z_m24` also runs 4 Beastmaster. Separating the
two is what round 28 is for.

## Shroofus is far LESS exposed than it looks, and that is the session's most useful correction

Structurally Shroofus looks worse than Beastmaster: its tokens are created in the combat-damage step,
so by its own card note they *"enter summoning-sick and cannot join the combat that made them — and a
haste grant does NOT change that"*, and they *"bank zero counters toward their own combat"*. The
payoff is strictly next-turn, it needs a second connect, and the token COUNT is the damage dealt,
which is precisely what a blocker reduces. Multiplicative exposure.

The magnitude refutes the concern. The pre-combat and post-combat board snapshots bracket exactly the
tokens created during combat, and Shroofus is the only card in this list that makes any:

| | |
|---|---|
| games where it produced tokens in combat | 170 (21.2%) |
| tokens created, total | 4,011 (mean **23.6** per producing game) |
| **…of which no later attack ever happened** | **3,556 = 88.7%** |
| T5 production | 2,825 tokens, **99.0% wasted** |
| games where every token it made was wasted | 51/170 (30.0%) |

At a mean win turn of 4.76 the exponential engine fires almost entirely **on the turn the game
ends**. Only ~11% of its production ever attacks. So the goldfish is not being materially credited
for Shroofus's compounding, and a blocker model would mostly tax tokens that are already decorative.
The same logic applies to the anthem itself: both the +5/+5 and the token blow-up peak on a turn the
game was already ending.

## Two things I got wrong, recorded because the retractions are the useful part

**1. Concordant Crossroads is NOT the lever.** Association made it look like the engine of the
exposed route — on turn-4 enables it is out for 86.1% of exposed enables against 24.3% of robust
ones. Direct intervention refuted it. Cutting it moved the one-attack rate **37.5% → 40.5% (3→1) →
37.4% (3→0)**, with the `+Forest` isolation control — which adds no bodies at all — scoring the
**highest at 41.9%**. At n≈330 the se on a 38% rate is ~2.7%, so these are all within ~1.5 se.
And across all 471 free attacks before an enable, Crossroads was out for only ~34%, flat by turn. The
deck does not need haste to attack on turns 2–3; a Sporecrown Thallid cast on turn 1–2 just attacks.
**The exposed route is ordinary curve attacks, which is not a card you can cut.** The exposure is
diffuse and archetype-level.

Crossroads is still worth pricing, for the different and pre-existing reason the screen doc already
named: its haste grant is printed for *all* creatures but `HasHasteFromLords` is controller-scoped,
so the opponent never gets it. That is a real paper cost no goldfish can see.

**2. Doubling Season cannot buy the robust route, twice over.** The user's arithmetic is right — one
Season drops the one-attack bar from 7 to 4 — but the lever is unreachable:

| | Season resolved in time | one-attack rate when it IS out |
|---|---|---|
| 1 copy | 49/800 games (6.1%) | **11.5%** (vs 38.0% with none out) |
| 2 copies | 83/800 (10.4%) | **15.6%** (vs 38.3%) |

Season costs {5} in a deck that wins on turn 4.8, and it makes **no bodies**. A board that paid for a
Season is a board with fewer attackers, so the 4-bar ends up *harder* to clear than the 7-bar is on a
board that spent that mana on Saproling Burst. This vindicates the screen's cut of Season to zero for
a second, independent reason the screen never knew about.

## What DOES buy the robust route: Saproling Burst, and only Saproling Burst

On turn-4 enables, Burst is on the battlefield for **84.3%** of the robust enables and **2.8%** of the
exposed ones. The mechanism is its own text: Fading 7 with a **free, untapped, repeatable** "remove a
fade counter: create a Saproling whose P/T equals the counters remaining" — seven bodies in one turn
at no mana and no tap. Nothing else in the pool can go from zero to a seven-wide attack. This agrees
with the screen's independently-measured "NEVER trim Saproling Burst".

The separation weakens by turn 5 (Burst 30.8% vs 23.1%), so the claim is specifically about the fast
enables; stated as such rather than generalised.

## Apparatus caveats that must travel with these numbers

* **The keep table cannot tell Shroofus from Brightcap Badger.** The user's 2026-10-02 bucket ruling
  merges `{Brightcap Badger, Shroofus Sproutsire, Psychotrope Thallid}`, and the file itself says that
  group *"should be the FIRST group revisited if these cards survive into the final list."* They did —
  Badger at 4, Shroofus at 2. So any arm that changes the Shroofus count changes a bucket count the
  table reads as though the changed card were a Badger, biasing Shroofus's measured value toward
  "as good as a Badger". Amending a bucket policy is a USER-only ruling.
* **The apparatus floor is still estimated from `ctl1`, never measured.** `--with-floor` needs per-arm
  R=10 tables, which the standing "no new tables during this process" rule forbids, and it would
  additionally die on the `keep_apart` entry naming `Secluded Courtyard` — a card the mono-green list
  does not contain. Recommendation: leave it estimated. The margin under test is 8–12× the estimate
  and the decisive readings are arm-vs-arm, where most shared-table bias is common-mode.
* **Saproling Migration's measured value is a LOWER bound** and this doc raises its priority: kicker
  is unmodelled anywhere (`grep kicker src/` is empty) and `EvalCard` has no `cast_creates_tokens`
  branch, so both errors push the same way. The real card makes four tokens for {5}{G}, which is a
  one-attack enabler in its own right — i.e. it feeds exactly the channel that survives blocking.

## The user's rulings, and the Season question I could not settle

After reading the above the user ruled:

| ruling | status |
|---|---|
| *"keeping Shroofus at 1 makes sense"* | **ADOPTED** — Shroofus is 1 in every arm from round 30 on |
| *"Beastmaster - 1 is possible. I agree that we shouldn't go lower than that either way"* | Beastmaster ∈ {3, 4}; **2 is ruled out** |
| *"I would see if we can keep at least 1 or 2 [Doubling Season]"*, *"Without a big cost to our win turns"* | round 30 measures the price |
| *"perhaps 4 is too many"*, *"(but 2 multiples would be rare) (with 2)"* | ladder runs 0/1/2/3 |
| *"We should also check 3 Doubling Season numbers just in case"* | in round 30 |

**The multiples worry is empirically moot at 2 copies.** Most Seasons ever simultaneously on the
battlefield, 400 games: at 20 life, **0 games had two** (89.2% none, 10.8% exactly one); at 40 life,
one game in 400 (0.2%). The user's reasoning was right.

### My Season refutation was invalid, and the reason matters

I had refuted Season twice: it resolves in time in only 6.1%/10.4% of games, and when it IS out the
one-attack rate FALLS to 11.5%/15.6%. Both numbers are real. Neither is evidence, for two reasons the
user identified:

1. **It is circular.** "Too slow" is measured against a 4.76-turn clock that the blocker overcount
   itself shortens — ~17% of games would see the anthem a turn later against a real opponent.
2. **The goldfish DECLINES the card; it is not unable to cast it.** With 2 copies at 40 life over 400
   games: 58 (14.5%) resolved, **52 (13.0%) were drawn into hand and never cast**, 290 (72.5%) never
   drawn. And in **52 of 52** declined games there were at least 9 potential mana on board (median
   ~13; lands plus creatures, since Brightcap Badger gives every Fungus and Saproling "{T}: Add {G}")
   against a cost of 5. So the search had the mana and chose bodies instead — because width has no
   value to it beyond total damage. **A blocker-aware search would face a different choice.**

So the apparatus's verdict on Doubling Season is not evidence against the user's position. The
position itself — *"it gives you a crazy board with Saproling Burst or other effects"*, *"generically
better against actual decks in which the board size really matters"* — concerns a quantity the
apparatus does not score. Season is under-credited for **exactly** the reason the free banking
attacks are over-credited: the goldfish prices damage, a blocker taxes width. Two sides of one gap.

**What the clock sweep did establish**, taking it for what it is rather than as a Season verdict
(400 games/cell, `MTG_START_LIFE`, max-turns 16 at 40 life):

| @ 40 life | win turn | max board width | Season deployed | attackers at enable | one-attack | floor (needs 8) |
|---|---|---|---|---|---|---|
| Shroofus 1, Season 0 | 5.35 | 17.4 | — | 6.51 | 48.5% | 7.7% |
| Shroofus 1, Season 1 | 5.36 | 18.4 | 6.8% | 6.41 | 47.6% | 7.0% |
| Shroofus 1, Season 2 | 5.37 | 18.1 | 14.5% | 6.36 | 46.9% | 7.0% |
| **Beast 3, Shroof 1, Season 2** | 5.51 | 18.4 | **18.8%** | 6.43 | **51.0%** | 8.2% |

Two things worth keeping. **The longer clock makes the robust route MORE common** — one-attack rises
from 36.8% at 20 life to 48.5% at 40 on the same list, which independently supports the user's
general point that the goldfish's fast clock distorts this archetype's readings. And **the user's
composed edit (Beastmaster 3 / Shroofus 1 / Season 2) posts the highest one-attack rate of any cell
measured.** But Season does not raise the attacker count at the enable (6.51 → 6.41 → 6.36) or the
blocker floor, so the width mechanism is not visible *through a search that cannot see blockers* —
which is the limitation above, not a result.

**Draw rate bounds how much any Season count can matter.** Hypergeometric by turn 5: a Season appears
in ~20% of games at 1 copy, ~36% at 2, ~50% at 3 (measured 27.5% at 2 copies, lower because the deck
wins on 5.37 and bottoming eats draws). So at 1–2 copies the card is absent from roughly three
quarters of games however strong it is when present. That is the real argument for 3, and why the
user's *"check 3 just in case"* was the right instinct.

**Mycoloth is a bias in the OPPOSITE direction and the question is closed.** USER: *"my concerns do
not apply as much to mycoloth. The card is potentially good, but also pretty slow. Even more
importantly it has major issues with removal, so it might be worse in real games."* Correct, and the
engine makes it stronger than that: the passive opponent never casts, so **no removal is modelled
anywhere**. Mycoloth is the pool's most removal-vulnerable card — devour collapses the board into one
creature — so the apparatus OVER-credits it. The screen cut it to zero regardless, which makes that
cut doubly safe.

## On building a blocker model

The hook exists and is better than expected: the opponent's board is already scheduled and
deterministic, and `ResolveCombatDamage` is the one shared combat core that both
`GameEngine::CombatPhase` and `TurnSolver::SimulateCombat` call, so executor/rollout lockstep would be
by construction. A blocking policy would be the OPPONENT's policy, so it does not engage the
no-greedy-in-search-window rule.

The cost is not the patch, it is the consequence: **it turns "attack with everything" from a constant
into a searched decision**, which is a large expansion of the search space on every deck, and it
changes the estimand for every GT row, keep table and value leaf in the repo (default-OFF keeps them
byte-identical, but nothing measured under it is comparable to anything measured before). It is not
the way to settle this list. It is a separate project with its own budget, and the user's call.
