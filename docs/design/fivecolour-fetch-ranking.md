# FiveColour fetch-target ranking: a land that enters TAPPED cannot cast anything this turn

**Status: ADOPTED 2026-08-18 — user-directed doctrine, measured.** Held-out **−0.6713** summed over
12 cases; train −0.3784 over 8. Every cell improves at every depth. Only FiveColour moves
(`FetchCandidates` is an archetype override); the other eleven suite decks are byte-identical.

## How it was found

The reference sweep (`scripts/ref_bench.py`) left FiveColour with 1 shortfall in 6:
`claude_s7_gi6`, human turn 4 vs search turn 5. The whole delta was turn 1 — the human plays a
fetchland and casts Birds of Paradise; the search plays a fetchland and casts nothing.

`MTG_UNPRUNE=fetch` alone recovered turn 4, which located it in the fetch candidate set rather than
in evaluation. (Confirmed it was NOT a legality or data problem first: `GenericProvider::FetchCandidates`
correctly filters the library by `m_subtypes` against the fetchland's `fetch_land_types`, and every
dual in the deck carries its subtypes — Stomping Ground is `[Mountain, Forest]`, so it IS a legal
Scalding Tarn target that produces `{G}`.)

## The defect

`MTG_FETCHRANK=1` printed the ordered candidate list. For Scalding Tarn on turn 1:

```
1. Jetmir's Garden (a2 s1 b3 u0 c3)   <- enters TAPPED
2. Zagoth Triome   (a1 s2 b3 u0 c3)   <- enters TAPPED
3. Breeding Pool   (a1 s1 b2 u1 c2)   <- untapped, makes G
4. Stomping Ground (a1 s1 b2 u1 c2)   <- untapped, makes G
```

`FetchSearchCap()` is 2, so the search branched on **two lands that both enter tapped**. Neither can
cast a one-drop on turn 1, so the turn-1 Birds of Paradise line was never branched on at all.

The cause is the sort order: `untapped` sat **fifth**, below `breadth`, so a triome's extra colour
always won before entering-untapped was ever consulted. For a deck whose turn-1 play is a `{G}`
accelerant that then fixes every colour, that ordering is backwards — the extra colour is worthless
if it arrives a turn late.

## The rule (user doctrine, 2026-08-18)

> "The priority is getting to 5 colours, starting with Green T1 and prioritizing other colours we
> might need in our hand after that." … "When possible we should get 2 colours we are missing." …
> "White is a good bet for T2 if we have Faeburrow Elder in hand." … "T1 green land is a priority.
> If we can't play T1 green, we should play T1 black for Deathrite."

Encoded as a new top-priority key, `enables_now`:

```
enables_now = enters_untapped ? max over produced-and-still-missing colours of accel_hits[colour] : 0
```

where `accel_hits[c]` counts how many **distinct accelerants in hand** colour `c` would turn on.
Sorted above every other key; everything below it is unchanged.

Two properties worth noting:

- **It is not a hardcoded colour preference.** Deathrite Shaman is `{B/G}` (hybrid) and Birds of
  Paradise is `{G}`, so green turns on *two* one-drop dorks and black turns on *one*. "Green first,
  black second" therefore falls out of the card costs. The same term produces "White is good on T2"
  once Faeburrow Elder (`{1}{G}{W}`) is the accelerant in hand and green is already covered.
- **It generalises past turn 1** — it fires whenever a fetch is what would turn on a mana source,
  which is the situation the doctrine describes.

Measured against the simpler boolean form (`accel_new > 0 && untapped`, i.e. untapped-first without
the accelerant count): the count version is better on BOTH seed sets (train −0.3784 vs −0.3524,
held-out −0.6713 vs −0.6540), so it is the one adopted.

## Why the cap was NOT raised

Raising `FetchSearchCap` from 2 to 3 also fixes the reference game (Breeding Pool is rank 3), and a
lever for it was built and measured. It was **discarded in favour of the ranking fix**: reordering
costs nothing, while a wider cap pays extra branching on every fetch for the whole game. The cap
stays at the generic 2. Note the solver already carries a related observation — the cap is overridden
to unlimited under `MainPhaseFilterActive` because it measured worse for 5c there (d3 s2002: 5.0150
capped vs 4.9250 uncapped), which is the same defect showing through a different door.

## Results — sum of per-case Δ avg win turn

| tier | seeds | cases | sum Δ |
|---|---|---|---|
| smoke (train) | 1001 | 3 | −0.1374 |
| regression (train) | 2002, 3003 | 5 | −0.2240 |
| **overnight (HELD-OUT)** | 4004–10010 | 12 | **−0.6713** |

Held-out is stronger than train, so this is not seed overfitting. FiveColour's 6 references go from
1 shortfall to **0** — exact parity with the hand-played line on every one.

`MTG_FETCHRANK=1` is kept as a diagnostic (prints the ordered candidate list with its keys); it is
what located this and costs nothing when unset.

---

# Part 2 — the full doctrine (ADOPTED 2026-08-18, held-out −0.1575 on 12/12 cells)

The rules above were the first pass. The user then stated the doctrine in full (rule 5 in the queue
below), which decomposed into five separable claims. Each was built as an independent bit of a
temporary `MTG_FC_FETCH2` bitmask — one binary, all arms — and measured singly and in combination.
Four were adopted; the fifth was measured inert and deliberately NOT adopted.

## What the diagnostic found first: the ranking was dead after turn 2

`MTG_FETCHRANK=1` mid-game showed **every key at zero from about turn 3 onward**, so fetches were
being decided by the alphabetical backstop:

```
[fetchrank T4 ... src=W6U5B7R6G6 land=W2U1B3R2G2 ...]
  Breeding Pool(e0 a0 s0 b0 f0 u0 d0 c2) Overgrown Tomb(e0 a0 s0 b0 f0 u0 d0 c2) ...
```

`src_cnt` read 6–7 sources per colour while the deck held 1–3 LANDS of each. The cause is that a
single Birds of Paradise adds +1 to all five colours, so once one dork is out every colour is
"covered" and once two are out every colour is "doubled" — `breadth`, `accel_new`, `spell_new` and
`depth` all die at once. Confirmed by instrument: `name` (alphabetical) was the **top decider at
43.3%** of 3,452 executor picks.

That is exactly what "counting dorks on board and eventually not counting them" fixes.

## The four adopted terms

| bit | term | the user's clause | what it does |
|---|---|---|---|
| SOFT | `soft_new`, land-keyed `depth` | "counting dorks on board and eventually not counting them" | a colour only a DORK covers is *soft*-covered: not breadth (it counts today), but a land for it outranks a second land for a colour the lands already make. Redundancy keys off `land_cnt`. |
| SUM | `want_deep` sums pips | "Oko + a Five Colour spell"; "extra red for Mana Cannons" | the redundancy want SUMS pips across the hand (cap 2) instead of one card's max. Mana Cannons `{2}{R}` + a five-colour spell needs RR at 8 mana; Oko `{1}{G}{U}` + one needs GG/UU. No single cost shows this. |
| HORIZON | `mv <= bf_sources + 2` | "seeing what we are likely to play from hand" | only plausibly-castable cards feed `want_deep`. Without it an uncastable Progenitus (`{W}{W}{U}{U}{B}{B}{R}{R}{G}{G}`) asks for two of every colour on turn 1 — a uniform ask, therefore no signal. |
| HYBRID | second hybrid colour counts | (implicit in "Green is a good first bet") | `{B/G}` is stored as pure black (`ManaCost` bakes a hybrid pip into its FIRST colour), so **Deathrite Shaman was invisible to green**. Every mana-PAYMENT path decodes `hybrid_pair`; this ranking site was the one that read raw flat pips. |

Note the HYBRID entry corrects Part 1's comment, which claimed green already counted Deathrite. It
did not — the conclusion "green first" happened to survive on Birds/Bloom Tender/Faeburrow alone.

## SUM is the reason single-lever screening is not enough

SUM and HORIZON feed only `depth`, and `depth` was dead before SOFT. Measured alone they look bad;
measured on top of SOFT they are most of the win:

| mask | levers | smoke | regression | total | cells better/worse |
|---|---|---|---|---|---|
| 2 | SUM alone | +0.0201 | — | +0.0201 | 0 / 2 |
| 8 | HORIZON alone | +0.0174 | — | +0.0174 | 0 / 2 |
| 1 | SOFT | −0.0066 | −0.0140 | −0.0206 | 3 / 3 |
| 16 | HYBRID | −0.0289 | −0.0050 | −0.0339 | 4 / 0 |
| 17 | SOFT+HYBRID | −0.0356 | −0.0170 | −0.0526 | 5 / 1 |
| 11 | SOFT+SUM+HORIZON | −0.0106 | −0.0740 | −0.0846 | 6 / 1 |
| 19 | SOFT+SUM+HYBRID | −0.0280 | −0.0580 | −0.0860 | 5 / 1 |
| **27** | **SOFT+SUM+HORIZON+HYBRID** | **−0.0616** | **−0.0680** | **−0.1296** | **7 / 0** |

**SUM moves from +0.0201 alone to −0.0616 inside mask 27.** A one-lever-at-a-time sweep would have
discarded the Mana Cannons clause on a measurement taken against a term that did not exist yet.

## Held-out

Mask 27 only was taken to held-out, so the holdout stayed a check rather than a second selection.

| tier | seeds | cells | sum Δ |
|---|---|---|---|
| smoke (train) | 1001 | 3 | −0.0616 |
| regression (train) | 2002, 3003 | 5 | −0.0680 |
| **overnight (HELD-OUT)** | 4004–10010 | 12 | **−0.1575 — 12 better, 0 worse** |

Held-out is stronger than train and every single cell improves. References stay at 0/6 shortfalls.
The unconditional build was verified play-identical to the mask-27 arm (same avgs AND same play
digests on all three smoke cells) before the GT was rebaselined.

## Why `breadth` looks inert — and why it is still correct where it is

Promoting `breadth` above `spell_new` was built and measured: **byte-identical on all 8 train
cells**. That is not evidence the tiebreak is useless; it is evidence it is *masked*. Instrumented
over 3,452 executor picks (`MTG_FETCHKEY=1` reports which key first separates the top two, and
which keys differ at all):

| key | differs between top-2 | actually decides | masked |
|---|---|---|---|
| `spell_new` | 12.1% | 7.3% | 168 |
| **`breadth`** | **12.0%** | **0.9%** | **382** |
| `untapped` | 11.6% | 2.1% | 328 |
| `depth` | 19.9% | 10.5% | 322 |

`breadth >= accel_new + spell_new` by construction, and in a five-colour deck the hand usually wants
every colour — so "a colour we are missing" is nearly always *also* "a colour something in hand
wants", and the higher rule fires first with the same answer. The 0.9% where breadth does decide is
exactly the case the user described: picking a dual's SECOND colour once a higher rule fixed the
first. Of those 31 picks, 5 have the hand naming both candidates equally (`Jetmir's Garden(b2 s1)`
over `Blood Crypt(b1 s1)` — the Mana Cannons shape) and 26 have the hand naming neither (pure
toward-5 coverage).

So breadth stays a TIEBREAK below the hand-want rules, which is where the user placed it. The
promotion variant is recorded here as measured-and-rejected so it is not re-derived.

## What the adopted change did to the decision mix

Same 3,452 picks, before → after:

| decided by | mask 0 | mask 27 |
|---|---|---|
| `name` (alphabetical backstop) | **43.3%** | **29.3%** |
| `depth` | 10.5% | **31.5%** |
| `soft_new` | — (did not exist) | 7.8% |
| `colours` | 27.0% | 11.6% |
| `breadth` | 0.9% | 0.9% |

The doctrine went from silent on nearly half of all fetches to silent on under a third. The residual
29.3% is the headroom rules 6/7 (triomes, plan-awareness) would attack.

`MTG_FETCHKEY=1` is kept alongside `MTG_FETCHRANK=1` as a permanent diagnostic — it is what turned
"breadth does nothing" into "breadth is masked 92% of the time", which are different facts with
different remedies.

## The doctrine, in full — and what is still open

Recorded verbatim so it survives context loss; this is the queue for further fetch rules.

> 1. "The priority is getting to 5 colours, starting with Green T1 and prioritizing other colours we
>    might need in our hand after that."
> 2. "When possible we should get 2 colours we are missing." … later clarified (2026-08-18): "it is
>    a tie-break for cases that tie in the other rules … not so much get 2 colours we are missing,
>    but prioritize the other rules and break ties by filling in more colours we are missing or
>    later are missing 2-of." … "Say you need Red for extra Mana Cannons, you can get a shockland
>    that has Red and another colour. The choice of the second colour might be made by that rule."
> 3. "White is a good bet for T2 if we have Faeburrow Elder in hand."
> 4. "As usual, a good bet is to play a shock from hand T1 if it has green. T1 green land is a
>    priority. If we can't play T1 green, we should play T1 black for Deathrite."
> 5. (2026-08-18) "seeing what we are likely to play from hand and getting those colours and
>    otherwise pulling any other colour we need to have all 5, counting dorks on board and
>    eventually not counting them … Once we have all 5 in lands, we should move toward 2 of each
>    while ensuring that we can play spells like Dorks, Mana Cannons, Oko + a Five Colour spell or
>    Bolas with our extra mana." … "Sometimes you need the extra red for Mana Cannons … Mana
>    Cannons + a five colour spell is pretty great" (at 8 mana).

Status:

| # | rule | status |
|---|---|---|
| 1 | 5 colours, green first on T1 | **DONE** — the `enables_now` key above |
| 2 | tie-break by missing colours / missing-2nd | **DONE** — `breadth` then `soft_new` then `depth`, all below the hand-want rules exactly as the clarification asks. See "why breadth looks inert" below. |
| 3 | white on T2 for Faeburrow | **DONE** — falls out of `accel_hits` (no separate rule) |
| 4a | green T1 > black T1 (Deathrite) | **DONE** — and now actually correct, see the hybrid fix |
| 5 | dorks count then stop counting; 2 of each; multi-spell turns | **DONE** — `soft_new` + land-keyed `depth` + summed/horizon `want_deep` |
| 6 | prefer a TRIOME when we can prove the land need not enter untapped | **DONE** — see Part 3 |
| 4b | play a SHOCK FROM HAND on T1 if it makes green | **NOT DONE** — a different decision (which land to PLAY, not which to fetch); lives in the land-drop choice, not `FetchCandidates`. |
| 7 | let the ranking consult the CURRENT PLAN | **NOT DONE** — user, 2026-08-18: "the heuristic should take advantage of what we know about the current plan if possible". Would replace both the `bf_sources + 2` horizon proxy and Part 3's hand-based unlock proxy with the plan's actual spend. `PlanContext.h` already exposes exactly this (`CurrentPlanContext()`, `PlanContextRest()`). **Read that header's warning first:** making one input plan-accurate while the rest stayed calibrated to the old proxy measured WORSE on the tutor axis (+18 held-out, `MTG_TUTOR_AXIS_POSTLAND`) — the cluster has to move together. |

Rule 4b remains a separate hook (the land-drop choice).

---

# Part 3 — triomes when the mana is not needed this turn (ADOPTED 2026-08-19, held-out −0.1114)

> "We probably should have a way to get triomes when we can determine there is no need for the land
> to enter untapped. That's a trickier rule, but likely worthwhile … because triomes make coverage
> very very easy as long as you can be confident that you don't need the colours." (user, 2026-08-18)

## The rule needs no triome term

The doctrine already prefers a triome on raw coverage: a triome covers three colours to a dual's
two, so it wins `breadth` outright. What was beating it was the pair of untapped-preferring terms —
and **both fire without ever asking whether the mana could be spent this turn**:

- `enables_now` (the TOP key) fires on "an untapped land makes a missing colour that an accelerant
  in hand wants". On T1 holding only Faeburrow Elder (`{1}{G}{W}`, MV 3), a green dual scored as if
  it deployed a dork — with one mana available, it deploys nothing.
- `untapped` (a tiebreak) fires on "enters untapped while a wanted colour is missing", regardless
  of whether that extra mana buys anything.

So the rule is implemented as a *precondition on the existing terms*, not a new triome bonus:

```
unlocks[c] := some nonland card in hand is NOT castable from AvailableManaPool(s),
              but IS castable from that pool plus one mana of colour c
```

computed once per fetch (hand x 6 colours, short-circuited), in two flavours — `accel_unlocks`
(the accelerant question, gating `enables_now`) and `any_unlocks` (gating `untapped`). When nothing
unlocks, both terms fall silent, `breadth` decides, and the triome wins on its own merits.

Birds of Paradise (`{G}`) still scores `enables_now` on T1, because one green really does cast it —
which is exactly the Part 1 behaviour that must not regress. Reference `claude_s7_gi6`, the game the
`enables_now` key was built for, still matches the hand-played line.

## Measured

| arm | d0 train (60k games) | searched train (8.4k) | searched cells better |
|---|---|---|---|
| ENCAST only (gate `enables_now`) | −0.0061 | −0.0045 | 6 / 6 |
| UNCAST only (gate `untapped`) | −0.0082 | −0.0026 | 5 / 6 |
| **BOTH (adopted)** | **−0.0126** | −0.0039 | 5 / 6 |

**HELD-OUT (overnight 4004–10010): cell-sum −0.1114, 9 better / 1 worse / 2 unchanged;
games-weighted −0.0128 turns/game over 10,800 games** — which reproduces the d0 train estimate
(−0.0126) to within 0.0002. All four held-out d0 cells improve. The one worse cell is +0.0025 on
400 games: a single game, a single turn.

## Two methodology notes worth keeping

**The suite's cell-sum metric nearly reversed the decision.** Summing per-cell deltas weights a
75-game cell the same as a 1000-game cell. On that metric the arms read smoke +0.0597 / regression
−0.0370 — the two train tiers disagreeing in SIGN — because smoke's d3/d5 cells hold 150 and 75
games, where ONE game moves the average by 0.0067 and 0.0133 respectively. Games-weighted, all
three arms were improvements. **The smoke tier still gets worse under the adopted change** (d3
5.0200→5.0467, d5 5.1600→5.2000) and that is expected: those two cells cannot resolve a 0.01 effect.
Do not read them as a regression.

**The fix was to buy resolution, not to guess.** d0 costs ~24 ms/game, so 60,000 train games across
the three train seeds cost minutes and settled the sign outright: per-seed deltas of −0.0130 /
−0.0123 / −0.0124, a spread of 0.0007 against an effect of 0.0126. The held-out run then reproduced
that number independently. Held-out was used ONCE, on the arm train had already chosen.
