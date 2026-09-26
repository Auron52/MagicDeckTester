# Saproling Burst: replacing the activation-count ladder with a projected landmark menu

**Status:** built, behind `MTG_FADE_K_WINDOW` / `heurarm::FADE_K_WINDOW`, plus a separate arm
`MTG_M2_FREE_ACTIVATION` for the post-combat gate defect this work uncovered.
**Code:** `src/ai/TurnSolver.cpp` — `FadeBoardRead`, `ReadFadeBoard`, `FadeKLandmarks`, the
`fade_saproling_cost` emission block in `CollectActions`, and `SecondMainUnproductive`.
**Instrument:** `MTG_FADE_K_DUMP=1` — a true ladder-width histogram plus one deduped line per
distinct situation showing the board read, every landmark and the menu produced.

## The problem

Saproling Burst reads *"Remove a fade counter: create a green Saproling token whose power and
toughness are each equal to the number of fade counters on Saproling Burst."* The enumerator offered
one candidate per activation count `k = 1 .. counters`, and `counters` is not bounded by Fading 7:
Doubling Season doubles counters **as they are put on**, so four Seasons take a Burst to `7 · 2⁴ =
112` and the ladder offers 112 candidates from one permanent. `MTG_BF_CENSUS` on candidate B's worst
game put **82% of all candidate mass** on the chosen-X shape, one Burst contributing 393,475
activation actions.

A naive cap is wrong — `k` is a genuine interior optimum — so what replaces the ladder is a set of
landmarks, each the argmax of an objective this board actually has, with membership decided by a
projection against the real board and hand.

## What the card actually is

Five properties drive every number below. Each was got wrong at least once on the way.

1. **The tokens are a CDA pointing at the SOURCE.** `RefreshFadeTokens` re-sizes *every* token the
   Burst ever made on each activation. A later activation does not add a body to a board — it adds a
   body and **shrinks every body already there**.
2. **Fading ticks at upkeep, for free.** Counters you decline to spend evaporate anyway, so there is
   no such thing as "banking a bigger body later": the body you make later tracks the same falling
   counter as the body you make now. The old comment at the emission site claimed otherwise and was
   simply wrong.
3. **The anthems move the death line.** Sporecrown Thallid (+1/+1) and a live Beastmaster Ascension
   (+5/+5) lift an X/X at X=0 off zero. Every threshold is computed against the measured anthem via
   `ComputeLordBonus` — the engine's own reader.
4. **Death is a damage source.** Slimefoot drains per Saproling death, and with a free sac outlet
   every Saproling converts, not only the ones that hit zero toughness.
5. **Haste exists in this pool, by two routes priced differently.** Concordant Crossroads arms every
   token free; Vitaspore Thallid pays a Saproling per grant.

## The policy (USER, 2026-09-26)

> *"We should always activate some amount on the turn it enters."* … *"To get the saprolings to not
> be summoning sick next turn or to attack with haste this turn."*
>
> *"After that turn we would only rarely activate, maybe for sacrifice targets or maybe to enable
> slimefoot."* … *"Because doing so weakens the saprolings we dropped."*
>
> *"There should be zero first turns with no Saproling activations and also none where you have
> fewer than 2 saprolings out by the end of the turn."*
>
> *"2 saprolings deal 20 over a number of turns 8, 6, 4, 2 but that is pretty slow. 3 is faster at
> 9,6,3 but only totals 18."*
>
> *"Any number of activations with doubling season is a kill next turn and this turn with haste."* …
> *"It is a trivial decision. And should definitely be narrowed to one option."* … *"Narrowing it
> means the heuristic should still be looking for the fastest win."*
>
> *"If we have haste or slimefoot on board (or in hand with enough mana or utopia mycon available) we
> should kill this turn. Otherwise we should aim to kill next turn."*
>
> *"Our heuristic just needs to look for existence. It doesn't need to do the actual go off."*

### The menu

**On the turn it lands** (`own == 0` — no live bodies from this source):

| landmark | objective |
|---|---|
| `k_lifetime` | most damage over the Burst's whole life — `bodies · Σ(counters remaining)`. Off a Fading 7 this is k=2: 8+6+4+2 = 20 |
| `k_pow_next` | most damage in one combat next turn. k=3: 9+6+3 = 18, but **sooner** |
| `k_pow_now` | ...this turn, offered only when something can grant haste |
| `k_presence` | most bodies that survive the upkeep tick and live to attack ("counters-2") |

Floored at **two Saprolings**, except when Vitaspore is the haste source (one hasted attacker is a
real play). Both peaks are exact argmax scans, not closed forms — the closed form maximised one
combat, and this card's payoff is a stream.

**On every later node the base menu is empty.** Doing nothing is expressed by omitting the action,
and each of the following has to name its payoff:

| landmark | when |
|---|---|
| feed the outlets | a sac outlet has no fodder — a **shortfall**, not a max: one body to feed a hungry Utopia Mycon, not five spare ones |
| the gas landmark | a **value** outlet is live (Utopia Mycon = mana, Psychotrope = a card, either resolved or castable from hand). The balanced point, tie-broken toward more bodies: off 14 counters that is k=7 → 14 bodies at 7/7, *"enough gas to attempt to go off while still ensuring you leave up some to finish the opponent next turn"* |
| max bodies | a devour body in hand — devour reads a count and banks it as permanent counters |
| the Ascension threshold | a Beastmaster on board or in hand, below threshold. Asks the user's question directly — can we field 7 attackers, this turn with haste or next turn — using live counters, so a Beastmaster on 4 counters asks for 3 attackers, not 7 |

**When a kill is projected the menu collapses to one entry**: the smallest `k` that provably wins,
plus a free margin (+1, or +2 when the haste has to be bought by sacrificing a Saproling), preferring
a this-turn kill over a next-turn one. The single exception is Slimefoot's drain, which is an
**independent clock** — it needs the bodies to *die*, not to survive summoning sickness — so when the
only provable kill is a combat one next turn, a drain line that might close it this turn stays on.

### The asymmetry that makes it sound

A landmark is *suppressed* when the base already wins, so the base is judged by `dmg_now_lo` /
`dmg_next_lo` — what it can **prove** (no sac-outlet conversion, no haste we must buy, no anthem we
must still fill, no Slimefoot still in hand). A landmark is *admitted* when it might win, so it is
judged by the optimistic `dmg_now` / `dmg_next`. Scoring both sides with one optimistic estimator
makes the base look like a winner it is not and deletes the real line: measured at **t = +2.24
against** over 1,250 held-out games before the split.

## The defect the user's invariant found

The invariant *"zero first turns with no Saproling activations"* is checkable, so it was checked
(`logs/fadek2/invariant.py`, real game logs via `--log-dir`). Over 25 games, **9 of 13 Saproling
Bursts got zero activations on the turn they landed** — 69%. Every one looked like this:

```
MAIN_1 [PLAY_LAND, CAST_SPELL Saproling Burst]   COMBAT [ATTACK]   MAIN_2 []
```

The cause is not the menu. `SecondMainUnproductive` skips the post-combat main unless a deferred
**cast** in hand is payable — and a Burst that entered in main 1 has a **free, counter-paid**
activation that no `PaymentManaCovers` test can see, on a permanent that was not on the battlefield
when main 1 enumerated. Two holes intersecting on the deck's best card. The counters then tick away
at upkeep, so the delay is not merely tempo: every token it eventually makes is permanently smaller.

Opening the gate for a free counter-paid activation on a permanent that **entered this turn** takes
the violation rate to **1 of 15 (7%)**, and the activation counts land exactly where the user said
they would: 5× two, 8× three, one 7 (a doubled-Burst kill).

**Doctrine note.** USER 2026-09-08: *"we don't have extra mains to search"* — never add a searched
second main to paper over a main-1 expressibility hole; fix the apply instead
(`EdfAutoGoOffAfterCasts` is the shipped precedent). This is not that: Fungus already runs a
post-combat main and nothing here adds a phase. It does raise the m2 *solve* count, which is the cost
that doctrine is about, so it carries its own arm (`MTG_M2_FREE_ACTIVATION`) and its own measurement
rather than riding in on the menu work.

## Measurement

Serial, single-threaded, candidate B, alone on the box. 100 games, seed 90000, d1/b3 — the labelling
depth, where generation cost lives.

| arm | wall | avg turns |
|---|---|---|
| ladder (both levers off) | 73.65 s | 5.5100 |
| + fade landmark menu | 37.14 s (**1.98x**) | 5.4700 |
| + free-activation gate | **34.00 s (2.17x)** | **5.3400** |

Faster *and* 0.17 turns better on the primary objective. Held-out validation across seven fresh
seeds at both depths: see the results block below.

## Deferred

* **The Mycotyrant** (user, 2026-09-26): not in `cards.json` today. Its power/toughness equals the
  number of Saprolings and Fungus you control, so it is another body-count payoff and would want the
  max-bodies landmark armed the way `devour` arms it. When the card lands, gate it on a param, not a
  name.
* **The go-off itself.** Per the user, this heuristic only has to put the option on the menu —
  executing the dig-into-haste line is a separate heuristic if it is ever wanted.
