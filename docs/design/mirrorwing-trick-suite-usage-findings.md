# Mirrorwing trick-suite: what the new cards actually do in play, and why the pool table is the wrong apparatus

**Status:** measurement notes, 2026-08-17. Produced after cancelling the `complete` mulligan
generation for `Mirrorwing Trick Suite` (journal preserved at
`logs/mullgen_trick/staged/...raw.json.journal`, 424k cell-sides, resumable).

The swap under test is like-for-like, 11 cards, mana and creature base untouched:

| out | in |
|---|---|
| Ancestral Anger x4, Expedite x2, Scale the Heights x2, Twinflame x3 | Fortifying Draught x4, Impolite Entrance x4, Luxurious Libation x3 |

## 1. None of the three new cards is inert

300 games, seed base 920000, trick deck on its play profile (no keep table):

| card | casts / 300 games | mean cast turn |
|---|---|---|
| Fortifying Draught | 231 | 4.14 |
| Impolite Entrance | 224 | 4.17 |
| Luxurious Libation | 172 | 3.65 |

For comparison the deck's staples land at Gold Rush 251, Fists of Flame 202, Zada 163, Mirrorwing
128. All three newcomers are being cast at staple frequency. The `{X}`-enumeration fix
([[x-spell-tricks-dropped-from-enumeration]]) is doing its job.

## 2. Luxurious Libation is cast at X=0 in 100% of casts

> **SUPERSEDED 2026-08-18.** The 100%-X=0 reading below was correct as a measurement and wrong as a
> conclusion: a THIRD defect sat behind the candidate set — the search enumerated and stored the
> large-X plan and never evaluated it, because `Action::eval` did not scale with X. See
> [[x-variant-invisible-to-plan-ordering]]. After that fix the split is 55.6% X=0 / 44.4% X=1..8,
> with X rising with the turn, and the mean win turn improves 5.0600 → 5.0275. The analysis of the
> candidate set below still holds and its fix is still required; it was simply not sufficient.


172 of 172 casts are X=0. The card's second role — the late "final pump to close the game" at
X = maximum board power — **never fires**.

Reading the log requires one caveat: `AIEngine.cpp` logs `logged_x = (has_x && chosen_x > 0) ? chosen_x : -1`,
and `GameLogger` emits `chosenX` only when `>= 0`. So **X=0 is indistinguishable from "not logged"**
in a game log. That ambiguity was resolved empirically: under `MTG_UNPRUNED` one cast logged
`chosenX: 2`, proving the field does flow on the trick path, so absence really does mean X=0.

Under `MTG_UNPRUNED` (full `1..max` range instead of the pruned pair) the split is 74 X=0 / 1 X=2 —
so even given every value, the search almost never wants X>0.

**Why.** The pruned candidate set is `{0, max_affordable}`, and `max_affordable` comes from
`AvailableManaPool` (`src/ai/ManaPayment.cpp:600`), which counts untapped **mana dorks** and rocks as
well as lands. So the only X>0 option ever offered is "tap every land *and* every Elvish Mystic and
Ignoble Hierarch" — which is close to always wrong here, because a tapped dork cannot attack *and*
wastes its own +X/+X. The value the user actually wants (the X that maximises total board power,
usually tapping no dorks) is **not in the candidate set at all**.

Note `MTG_UNPRUNED` is not a clean A/B for this: it widens *every* prune, so the 20 ms budget spreads
over a far larger tree and play gets worse overall (5.82 vs 5.09 mean win turn on 98 matched seeds).
It establishes reachability and logging, nothing more.

**Fix belongs in a provider.** Per the core invariant only a deck/archetype provider may narrow the
search, so the candidate rule (`{0, power-maximising X}`, lands first, a dork only where tapping it
raises total power) goes in a Mirrorwing `XCandidates` override, not in `TurnSolver`.

## 3. The archetype shed doctrine does not name the new cards

`MirrorwingProvider`'s discard/shed ordering (`src/ai/DecisionProviders.cpp:7390-7455`) builds its
pump tier from hardcoded names:

```cpp
copies_of("Gold Rush"); copies_of("Twinflame"); copies_of("Fists of Flame");
copies_of("Ancestral Anger"); copies_of("Expedite"); copies_of("Scale the Heights");
```

Fortifying Draught, Impolite Entrance and Luxurious Libation appear nowhere. The list's own comment
states the requirement it is now violating:

> The list names EVERY card (the gi295 lesson: an under-covering list hands the decision to the
> shared ranking's highest-MV fallback, which sheds the magnet)

Three of the four departing cards (Ancestral Anger, Expedite, Scale the Heights, Twinflame) are named
here, so the swap **removes named cards and adds unnamed ones** — an asymmetry that falls entirely on
the arm being evaluated. `deck_compare.py --preflight` surfaces this automatically ("existing logic
that names an edited card" / "introduced cards NO archetype heuristic or profile lever names").

Observed discards over the same 300 seeds: trick 22 discards (12 magnets), base 5 discards (4
magnets). **This is confounded** — the trick deck has no keep table and the base deck has its adopted
one, so hand quality differs — and it is not evidence on its own. Two things are worth noting anyway:
the code-level under-coverage is a fact independent of the measurement, and the base deck sheds a
magnet in 4 of its 5 discards, which suggests magnet-shedding is a pre-existing issue in both decks
rather than something the swap introduced.

## 4. Impolite Entrance cannot be screened on its merits

From the pre-flight's bracket notes: trample is not modelled (the goldfish opponent never blocks) and
sorcery-vs-instant is not modelled (casts resolve in MAIN_1). Under this engine Impolite Entrance is
**parameter-identical to Expedite**. Any measured difference between them is noise, and the card's
real merit — the extra trample the user is interested in — is out of scope for the goldfish entirely.

Corroborated independently by equivalence discovery on the union deck: `Impolite Entrance ~ Expedite,
dist=0.0225`. Two cards the engine models identically measure 0.0225 apart on 400 CRN probes, i.e.
0.0225 is this deck's **noise floor** for the discovery metric, not a signal.

## 5. Why the screening pool table is the WRONG apparatus here

`deck-screening.md` prescribes a pool table over the union of every arm's cards. Measured for this
pool, that is more expensive than the per-deck table it replaces:

| grid | K | size-7 cells | vs trick deck |
|---|---|---|---|
| trick deck alone | 16 | 144,630 | 1.0x |
| union (base + trick) | **20** | C(26,7) = 657,800 | **4.5x** |

Nothing merged at threshold 0.01 — the union is 20 distinct cards in 20 classes. Forcing the
provably-correct Impolite Entrance = Expedite merge only gets K=19 (480,700 cells, 3.3x).

Worse, this archetype's rollouts are far slower than the skill's planning constant. The cancelled run
sustained **~241 rollouts/s on 32 threads**, against the skill's `~110/s/core` guide (~3,500/s
expected) — a ~14x shortfall, with individual keep-rollouts of 3–10 s. Combo fan-out is what makes it
expensive, and it is unaffected by lowering R.

At that throughput a union pool table is **~12–15 h**, against a target of "a few hours". The pool
table is the right default for an ordinary pool and the wrong choice for this one.

## Related

- [[x-spell-tricks-dropped-from-enumeration]] — the enumeration bug that made Libation castable at all
- [[mirrorwing-trick-suite-screen]] — the screen this feeds
- [[journal-r-guard-discards-valid-floor-work]] — why the cancelled run's journal is still resumable
