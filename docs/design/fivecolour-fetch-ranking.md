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

## The doctrine, in full — and what is still open

Recorded verbatim so it survives context loss; this is the queue for further fetch rules.

> 1. "The priority is getting to 5 colours, starting with Green T1 and prioritizing other colours we
>    might need in our hand after that."
> 2. "When possible we should get 2 colours we are missing."
> 3. "White is a good bet for T2 if we have Faeburrow Elder in hand."
> 4. "As usual, a good bet is to play a shock from hand T1 if it has green. T1 green land is a
>    priority. If we can't play T1 green, we should play T1 black for Deathrite."

Status:

| # | rule | status |
|---|---|---|
| 1 | 5 colours, green first on T1 | **DONE** — the `enables_now` key above |
| 3 | white on T2 for Faeburrow | **DONE** — falls out of `accel_hits` (no separate rule) |
| 4a | green T1 > black T1 (Deathrite) | **DONE** — falls out of `{B/G}` vs `{G}` in `accel_hits` |
| 2 | prefer covering TWO missing colours | **PARTIAL** — `breadth` exists but sorts 4th, below `accel_new`/`spell_new`, so a land covering one wanted colour can still beat one covering two missing colours. Not yet measured as its own lever. |
| 4b | play a SHOCK FROM HAND on T1 if it makes green | **NOT DONE** — a different decision (which land to PLAY, not which to fetch); lives in the land-drop choice, not `FetchCandidates`. |

Rule 2 is the natural next lever on this function; rule 4b needs a different hook and should be
scoped separately.
