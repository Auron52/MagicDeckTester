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

Faster *and* 0.17 turns better on the primary objective.

### Held out — seven fresh seeds, both arms and both depths, ONE pooled batch

| regime | menu alone | + the m2 gate |
|---|---|---|
| labelling d1/b3, 2,400 games | -0.00417 t (t = -1.39), **2.04x** | **-0.12625 t (t = -18.42), 2.01x** |
| play d5/b20, 1,750 games | -0.00400 t (t = -3.24), **1.50x** | **-0.13886 t (t = -21.31), 1.37x** |

Better *and* faster in both regimes, so both are ADOPTED default ON (`81c8e863`). Gates: scenarios
103/103, smoke 93/93 with **0 configs changed** (the shipped Fungus list runs no Saproling Burst, so
both levers are inert there), regression 129/129 including reference reproducibility, CI green on
Linux and Windows including determinism parity.

### What it does to GENERATION, which is what this was for

Candidate B, `mullgen.sh run` on a copy under `logs/`, alone on the box. Against the two earlier
runs recorded in `slow-rollout-tail-and-the-uncharged-greedy-walk.md`:

| stage | baseline | + M2 shrink-sac | + old fixed window | **+ this work** |
|---|---|---|---|---|
| d1/b3 play-digest battery (64 games) | 27 s | — | — | **7 s (3.9x)** |
| equivalence discovery | 702 s | 614 s | 533 s | **471 s (1.49x)** |
| floor-pass journal at the 900 s mark | 6,978 | — | 9,667 | **20,543 (2.94x)** |

Bucket count is unchanged at K=22, so none of this came from a coarser table. The play digest moved
to `36a65944fd138cd0`, which discards the banked journal — already accepted by the user when
`MTG_FUNGUS_SHRINK_SAC_M2` was adopted, and re-discovery was forced regardless.

## The sibling axis: spore counters

**Status:** built, behind `MTG_SPORE_POP_ALL` / `heurarm::SPORE_POP_ALL` (default OFF pending the
held-out measurement), with three sub-mode arms. **Code:** the `spore_saproling_cost` emission block
in `CollectActions`. **Instrument:** `MTG_SPORE_K_DUMP=1` — width histogram split by doubler, **plus
a decision-branch tally**.

The same question — how many bodies to make — on the deck's other counter. It gets the opposite
answer, and for a reason that is a property of the card rather than a judgement call.

### Why this one is exact and the fade one is not

Saproling Burst's counter is *contested*: spending it shrinks every body already out and the
counters evaporate at upkeep anyway. A spore counter has none of that.

1. **Exactly ONE sink, and it is the same sink on every member.** *"Remove three spore counters from
   this creature: Create a 1/1 green Saproling creature token"* — on Thallid, Thallid Shell-Dweller,
   Sporesower, Psychotrope, Vitaspore, Deathspore and Utopia Mycon alike. Every *other* ability on
   those cards costs a Saproling **sacrifice**, not a counter, so popping forecloses nothing.
2. **Nothing is spent by popping.** Spore counters do not decay, the token is a flat 1/1 whatever the
   count, and a body already on the battlefield does not shrink (contrast `RefreshFadeTokens`).
3. So earlier is **weakly better**: more turns of the body, and more fodder for devour, Utopia
   Mycon's mana, Psychotrope's draw, Vitaspore's haste and Slimefoot's drain.

That leaves exactly one reason to wait, and the user named it:

> *"To be clear with the fungus counters there is no benefit to waiting unless we are likely to play
> a doubling season next turn. Arguably the turn after could make sense to consider as well if that
> doesn't work well."*

### The rule, and the one case that stays searched

| board / hand | decision | why |
|---|---|---|
| a doubler already **resolved** | **POP**, forced | every `k` is dominated by the maximum: the same 2 bodies per 3 counters, a turn sooner |
| a doubler **in hand and reachable** — payable now, or within `MTG_SPORE_HOLD_TURNS` turns of land drops | **SEARCH, narrowed to `{1, all}`** | the two options the user named are real questions, and "pop them all" deletes one of them |
| a doubler in hand but **out of reach**, or none at all | **POP**, forced | nothing to wait for, and popping everything dominates *including* for mana — more bodies is more fodder |

The searched row is the user's correction to a first version that made it a rule:

> *"I guess 'hold for a season' would not be a hard-fast rule, but perhaps would need to be searched.
> If it looks like we can play one next turn there is a real question about whether we need that
> saproling early, either because it loses summoning sickness or because we need to sacrifice it for
> something. If we are just doing chip damage to the opponent at this point (they have most of their
> life remaining) holding for the season is probably better."*
>
> *"I think pop them all is okay most of the time, but you do have a point that popping one for say
> extra mana can be important to consider sometimes."*

Note **"payable now" is deliberately in the searched row.** An earlier version forced the maximum
there, on the reasoning that the line is cast-then-pop and ordering belongs to the plan search. That
deleted a real line: pop **one**, sacrifice it to Utopia Mycon for the mana that **casts** the
Season, then pop the rest. The projection is the Goblin Matron one the user sanctioned
(`GoblinsProvider`'s `mana_next`): board yield with `tapped` ignored because everything untaps, plus
one land drop per future turn while lands remain in hand. A drop is credited **1**, not the land's
own yield, because Hickory Woodlot and Peat Bog tap for 2 but enter *tapped*.

**The searched case does not give the cost back.** It is the *resolved* Season that makes the ladder
wide — it doubles the upkeep counters as well as the tokens — and that is the forced-pop row. While
the Season is still in hand the pool holds few enough counters that the ladder is 1–3 rungs.

### The lesson: tally the branch, or a rule can be dead and look adopted

The first version of this rule fired its searched branch **zero times in 4,550 held-out games** —
`hold=0`, `hold=1` and `hold=2` produced byte-identical digests on every seed, which is only visible
if you compare digests and not averages. There is now a branch tally in `MTG_SPORE_K_DUMP`. On 40
games at d1/b3 the restructured rule reads:

| branch | count |
|---|---|
| no decision to make (`max_k <= 1`) | 77,986 |
| **POP** — doubler already resolved | 15,982 |
| **SEARCHED** — doubler payable now | 1,300 |
| **SEARCHED** — doubler reachable in the window | 522 |
| **POP** — doubler in hand, out of reach | 272 |
| **POP** — no doubler in hand | 3,788 |

So of the 21,864 real decisions, 90% are forced pops and 8.3% stay searched.

### Arms, including one that tests a user question

* `MTG_SPORE_HOLD_WIDE` — the window is two turns, the user's *"arguably the turn after"*.
* `MTG_SPORE_HOLD_NONE` — never search; prices the searched exception itself.
* `MTG_SPORE_HOLD_SECOND` — a **resolved** doubler also opens the searched branch when a *second* one
  is in hand. The user: *"if you have a season on board and one in hand, it is not clear to me that
  it is ever worth waiting for the second season to drop. That is something we can test. But
  realistically deploying the saprolings is probably almost always the better option there."* The
  expectation is therefore the **default** (pop), and this arm measures the alternative. It is also
  the expensive direction, since with a Season out the counters are already doubled.

### Measurement

100 games, seed 90000, d1/b3, serial, alone on the box:

| arm | wall | avg turns |
|---|---|---|
| ladder | 34.67 s | 5.3400 |
| pop-all | 30.08 s (**1.15x**) | **5.3300** |

Held out over the same 7 seeds and both depths as the fade work, five arms in ONE pooled batch
(`logs/fadek2/ab_spore3.json`, 70 jobs):

| regime | Δ avg turns | wall |
|---|---|---|
| labelling d1/b3, 2,800 games | **−0.00036** (t = −1.00) | **1.10x**, all 7 seeds faster (1.05–1.25) |
| play d5/b20, 1,750 games | **0.00000** (t = 0.00) | 1.04x over 70 jobs; 1.13x over 28, per-seed 0.91–1.54 |

Quality flat, cost real, so **ADOPTED default ON** — a cost-only adoption, unlike the two fade levers
which also moved the objective. Both the pre- and post-correction rules measure the same, which the
tuning block predicted.

**Do not quote the play wall figure precisely.** Its per-seed spread is 0.91x–1.54x and the pooled
number moves with job count (1.036x over 70 jobs, 1.131x over 28) — one slow seed carries it. The
labelling number is the trustworthy one: every seed faster, in both runs, and that is the regime
generation cost lives in.

**Every sub-mode arm was byte-identical to the default on 7/7 seeds at both depths.** That is a
result, not a null: it says that whenever the search is *free* to take the middle of the ladder or to
wait, it deploys anyway.

* `HOLD_WIDE` (two-turn window) — identical. The extra window buys nothing.
* `HOLD_NONE` (never search) — identical in outcome, and ~2.4% faster at labelling than keeping the
  searched branch. So the searched branch costs about 2.4% of the labelling gain and buys nothing
  measurable *here*. It is kept because the user asked for the option explicitly, and because the
  case it protects is by nature a longer game than these.
* `HOLD_SECOND` (wait for a second Season) — identical, which answers the user's question
  (*"it is not clear to me that it is ever worth waiting for the second season"*) in the direction
  they expected.

**Scope that honestly.** `max_turns` is 8 and the opponent does nothing, so the *"just doing chip
damage, they have most of their life remaining"* board that motivates holding barely occurs. These
results say the middle of the ladder is never preferred **in ≤8-turn goldfish games**; they do not
say it never is.

A prior, narrower version of the lever (collapse only when a doubler is already out or payable now)
measured **completely inert** on those seeds: byte-identical digests on 7/7 labelling seeds and 5/7
play seeds, 0.0000 turns. Its apparent "1.14x" on the tuning block was noise. That is why the rule
now covers the no-doubler case, which is where the 90% of forced pops actually live.

### A pre-Season pop is a FUNDING decision, and nothing else

The rule above shipped one branch too wide, and the user caught it on the invariant output:

> *"That makes no sense. The search shouldn't even offer both... it should offer before only for the
> purposes of sacrificing them for mana. Because if you need the extra mana to cast doubling season,
> then we may need to do this. Otherwise you always should take them after."*

The offending turn cast Doubling Season **off lands alone**, so nothing was gained by holding a body
back — yet the menu still offered `{1, 2}` because the doubler was "payable this turn". The fix needs
**two** affordability reads, which mean different things:

* `savail` — the engine's accounting pool, which **includes** bodies a sac-for-mana outlet could eat
  (`AddSacPayFodderToPool`).
* `mana_only` — what the untapped board pays **without selling a Saproling**.

If `mana_only` covers the doubler there is no funding question and the pop belongs *after* it (forced
maximum). Only when `savail` covers it and `mana_only` does not is a pre-Season pop a real decision,
and then it is a *funding* decision. That moved **1,218 of 1,300** cases from searched to forced; only
**82** genuinely need a sacrifice. Searched cases fell from 8.3% to 2.8% of real decisions, and
emitted ladder widths are now 1 or 2 only.

### The "defect" this turned up was a misread, and the retraction is the lesson

Narrowing that branch made game 11 stop popping on T6 entirely, which looked like the search refusing
a free option — the same shape as the Saproling Burst idle. It was not. The full line:

```
ladder     T6 Season + pop Vitaspore              saps=2
           T7 Season + pop Deathspore             saps=6   win T7
corrected  T6 Season, no pop                      saps=0
           T7 Season + pop, pop (pool rollover)   saps=8   win T7
```

The search **held one turn on purpose** and popped twice under *two* Seasons, ending with 8 bodies
instead of 6 at the same win turn. Two claims had to be withdrawn: that the pooled two-payer
activation fails to apply (it works — those are the two `ABILITY` entries, `SpendSporeActivations`
rolling over to the next canonical payer), and that a free upside was declined (it was a real trade —
2 bodies plus 2 damage now against 4 bodies next turn). Across the 25 games the arms have identical
activation counts (15) and the corrected rule ends with slightly **more** bodies (192 vs 190).

**The checker was what misled me**, and it was wrong in a specific way: it treated "a Season is
already out" as making a hold indefensible. Doubling Season **doubles again**, so a second copy in
hand is a live reason to wait. `logs/fadek2/spore_invariant.py` now classifies on *"is there another
Season to come"*, not on whether one has landed. This is also a mild counterexample to the
`HOLD_SECOND` result: waiting for a second Season did pay here in bodies, if not in win turn.

## Deferred

* **The Mycotyrant** (user, 2026-09-26): not in `cards.json` today. Its power/toughness equals the
  number of Saprolings and Fungus you control, so it is another body-count payoff and would want the
  max-bodies landmark armed the way `devour` arms it. When the card lands, gate it on a param, not a
  name.
* **The go-off itself.** Per the user, this heuristic only has to put the option on the menu —
  executing the dig-into-haste line is a separate heuristic if it is ever wanted.
