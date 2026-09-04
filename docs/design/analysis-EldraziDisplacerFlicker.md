# Analysis ledger — EldraziDisplacerFlicker

> **2026-09-02: THE DECKLIST CHANGED. Everything below the "PLAN" section describes the OLD list and
> is retained only for the engine findings it records.** The user supplied a corrected list (list 3),
> now canonical at `decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod`; lists 1 and 2 are
> deleted but preserved in commit `7d8bddc5`. Every MEASUREMENT below (7.20 avg, the win-turn
> distribution, the A/B deltas) is for a deck we no longer play. The ENGINE work below is still
> valid and carries over.

## What the real deck is, and why the old numbers were meaningless

`EldraziDisplacerFlicker` is a **Living Wish toolbox combo deck**. 4x Living Wish maindeck fetches an
8-card creature/land sideboard, and BOTH real win conditions live in that sideboard:

| sink | cost | note |
|---|---|---|
| Essence Depleter | `{1}{C}`: opponent loses 1 life, you gain 1 | ~20 activations to kill |
| Dimensional Infiltrator | `{1}{C}`: opponent exiles the top card of their library | ~53 to deck them |

**Neither has `{T}` in its cost**, so neither is once-per-untap: they are pure mana sinks, exactly
what an unbounded-mana deck wants. Both floor to `{C}` under Training Grounds. Contrast the old
list's Shivan Gorge (`{2}{R}`, **`{T}`**), which needed a fresh untap for every one of ~20
activations and rode the blink loop's untap priority.

**Why the old list measured 7.20 with a hard floor at turn 5 and 14% never winning:** its intended
fast kill was Stroke of Genius decking the opponent, and that was impossible TWICE OVER — the card
was implemented as a no-op (`template: draw_x`, empty `parameters`), and the opponent has no library
to deck (see [passive-opponent-no-library.md](passive-opponent-no-library.md)). Fixing either half
alone would still have produced zero wins from that line. The user's estimate for the real deck is
**4-5 avg win turn** with a mulligan profile.

## PLAN (2026-09-02) — agreed with the user

Dependency order. Step 0 is a PREREQUISITE for step 4, on the user's instruction: the standard
analyzer must do the card work, and it cannot analyze cards it cannot see.

0. **DONE** (`2055631a`) — **Coverage/analyzer scans REACHABLE SIDEBOARD cards.** `analyze_deck.py
   --coverage-only` scanned the mainboard only: on this deck it reported `missing: [Living Wish,
   Aether Hub]` and stayed SILENT on all four toolbox cards, including both win conditions. In a
   wish deck the sideboard IS the deck, so this was a correctness bug. Reachability is a real gate
   (three detectors: a `wish_from_sideboard` param, "outside the game" in the oracle text, then a
   name list — the name list is load-bearing because at Stage 1 the wish itself is usually one of
   the MISSING cards). Verified the five decks with vestigial sideboards are unaffected; EDF now
   reports all five missing cards.
1. **DONE** (`9f56f5de`) — **Shared `OpponentHasLost(state)` predicate**, routed through BOTH
   worlds. The executor centralised it (`CheckWinCondition`); the ROLLOUT open-coded
   `Opponent().life <= 0` at **30 sites**, all now routed. Deck-out landing only in `HasLost()`
   would have let the executor see the win while the search never pursued it — the exact shape of
   the bug that made the Gorge go-off execute as a kill ZERO times.
   Gated on `opponent_library_dealt`: `players[1].library` was **empty rather than absent**, so a
   naive `library.empty()` test would read as an instant win in every game of every deck.
2. **DONE** (`9f56f5de`) — **Fixed "realish" opponent library** (`src/core/OpponentDeck.h`). 60
   cards, 24 lands / 20 creatures / 16 spells, 7-card opening hand, all from existing `cards.json`
   entries. Fixed rather than a mirror so deck-out depth is a CONSTANT (53) and comparable across
   decks. Shuffled from a **DERIVED seed**, and that is why it cost nothing: `Library::Shuffle`
   takes an explicit seed rather than drawing from a shared stream, so player 0's permutation cannot
   move. **Smoke came back 48/48 byte-identical** — same digests, 0 play changes — so NO GT
   rebaseline. Regression tier running.
3. **DONE** (`9f56f5de`) — **Deck-out win turn = OUR LAST TURN** (user: "because the [opponent]
   doesn't get any main phases"). Falls out for free from firing the simulated draw at the end of
   our turn, before the turn increment — no special-casing anywhere. Their upkeep could technically
   act, which is a faithful simplification, not an oversight.
   Covered by `test/scenarios/opponent_deckout.json`, verified DISCRIMINATING: 2 cards →
   `win_turn=3` with `opponent_life=20` (so it is the deck-out, not damage); 100 cards → no win.
4. **DONE** — **The five cards, VIA THE STANDARD ANALYZER** (user: "I recommend strongly that
   we use the standard analyzer to handle it"): Living Wish, Aether Hub, Vexing Shusher, Essence
   Depleter, Dimensional Infiltrator. Oracle text fetched and verified from Scryfall (below);
   per-card research fanned out across four Opus agents per the skill's Stage 2 protocol.
   `--coverage-only` is now **CLEAN: 0 missing, 0 partial, 23 cards** (19 mainboard + 4
   sideboard-only), down from five missing. Every clause is implemented or carries a bracket note
   naming **which modelling limitation** makes it inert — never "it doesn't matter for a goldfish",
   which is a claim about the card wearing the costume of a claim about the engine.
5. **DONE** — **Sideboard zone + wish mechanic.** `Player::sideboard` is a real per-game zone, and
   Living Wish is modelled as a TUTOR whose search zone is the sideboard, so it inherits the entire
   searched-tutor apparatus (index axis, five signature folds, pin, viewer chooser) rather than
   growing a parallel one. Verified on real games: the wish fires and picks DIFFERENT targets
   across games, so the axis is genuinely searched.
6. **DONE** — **Go-off recognizer learns both `{C}` sinks**, plus `ProjectsAlternateWin` for the
   deck-out (which `ExtraLethalDamage` structurally cannot express) and
   `ManaSinkActivationCounts` so the search can propose the finishing count.
7. **DONE** — **Profile regenerated** against the corrected list (19 `card_scores`, no hand gate)
   and the measurement stages re-run. **The headline number is below, and it does not meet the
   user's estimate.**

## THE FIRST HONEST MEASUREMENT OF THE CORRECTED LIST (2026-09-02)

4 seeds x 100 games, ship settings, the regenerated profile:

| depth | avg win turn | unwon | floor | mode |
|---|---|---|---|---|
| d0 (greedy) | 8.777 | 340/400 (85.0%) | t6 | — |
| d3 | **7.206** | 37/399 (9.3%) | **t5** (2 games) | t7 (192) |
| d5 | **7.205** | 37/400 (9.2%) | t5 (2 games) | t7 (193) |

**The corrected list measures 7.21 against the old list's 7.12 — i.e. essentially unchanged, and
2+ turns short of the user's 4-5 estimate.** The combo itself is not broken: the full chain fires
end-to-end (a logged turn-6 win assembles Emiel + Cloud of Faeries, wishes for Dimensional
Infiltrator on T5, lands Training Grounds on T6 and DECKS the opponent with `oppLife=9` — the new
win condition carrying a game on its own). It is the AVERAGE that is slow, not the ceiling.

## RE-MEASURED after the two mana fixes (2026-09-03) — the first thing that moved the number

The land-Aura fix (`b8d5f2e`-series, four of the five payment sites were blind to Aura mana) and
the hybrid fix (`5bd5aaab`, a hybrid pip was unpayable with its SECOND colour — Trace of Abundance
is `{R/W}{G}` and this deck has no red source, so every Trace needed the white half) both add
LEGAL LINES the deck previously could not take. Re-ran the **identical manifest** — same 4 seeds,
same 100 games each, same 3 depths, same profile, same `budget_ms`, `ignore_play_profile` — on the
fixed binary, so the engine is the only thing that differs. `logs/edf_remeasure/`.

| depth | pre | post | paired delta | t (n=4 seeds) |
|---|---|---|---|---|
| d0 (greedy) | 8.777 | **8.548** | **−0.230** | −8.9 |
| d3 | 7.206 | **7.068** | **−0.138** | −6.1 |
| d5 | 7.205 | **7.070** | **−0.135** | −6.6 |

Every seed moved the same direction at every depth. Unwon games fell **37/400 (9.2%) → 27/400
(6.8%)**, the t5 floor went from 2 games to 6, and the mode is still t7 (183/400).

> **CORRECTION (same day).** This section first cited "all twelve job digests changed" as
> supporting evidence. **That is not evidence and the claim is withdrawn** — the play digest of this
> deck is not stable run to run *at all*: the same binary on the same manifest diverges in 6 of 8
> jobs (see `docs/design/batch-run-to-run-nondeterminism.md`, reproduced this session). Digests
> would have "changed" with no code change whatsoever. What survives, and what the conclusion
> actually rests on, is the **averages**: four seeds moving the same direction at three depths,
> against a measured run-to-run noise floor of ~0.01 turns on a single job. −0.135 clears that
> floor by an order of magnitude; the digest line never carried any weight.

**This is the largest effect measured on this deck, and it is a CORRECTNESS fix, not a tuning
knob.** For scale: 9x the search budget bought +0.063 turns (the wrong way), and the adopted wish
ranking bought −0.030, inside the noise. That ordering is the point — `heuristic-optimization.md`'s
Rule 0 says a modelling bug is a bug to fix rather than a heuristic to tune, and here the two
modelling bugs outweighed every knob tried on this deck combined. It shifts where the remaining
gap should be hunted: **look for more modelling gaps before reaching for another lever.**

**It does NOT close the headline gap.** 7.07 against the user's 4–5 estimate is still 2+ turns
short, and the untested candidate remains the learned exhaustive keep table (rule-based mulligans
were already refuted).

**UPDATE, same day: a THIRD mana fix landed and the prediction held.** Another agent's
recoverability audit (`46e8efb4`) found a *fifth* land-Aura blind spot — the branch-and-bound
max-mana bound (`SourceMaxNet` / the `source_max_net` lambda) never carried the Aura bonus, so a
gate documented as "deliberately over- (never under-) counts" could prune a payable cost. Measured
on these same 4 seeds x 100 games: **7.068 → 6.92, another −0.145.**

Cumulative for the day: **7.206 → 6.92 (−0.29 turns), from three independent mana-modelling fixes,
found by two agents working separately.** Each is larger than any tuning lever ever tried on this
deck. The "hunt modelling gaps, not levers" call was made after the first two and immediately paid
out a third — treat it as the standing direction for this deck, not a one-off observation.

**Side effect worth recording: the depth knob is partially live again.** Pre-fix, d3 and d5 were
byte-identical on **all four** seeds — total starvation, `--depth` completely inert (next section).
Post-fix, **three of four seeds diverge** (only s4101 still matches). The averages are still the
same to 0.003 turns, so depth still buys nothing — but it is no longer literally the same
computation. *The mechanism is NOT measured*: the plausible story is that visible Aura mana lets
the payment solver settle subsets sooner, leaving budget for a second ID pass, but that is a
hypothesis that fits, not one that discriminates, and it has not been tested.

Thread-wall roughly halved (d3 10,856s → 5,447s, d5 8,515s → 5,387s), but **that comparison is
confounded** — batch `ms` is wall, not CPU, and the two runs saw different box load. Treat it as
suggestive of a real tractability gain and re-measure on a quiet box if the number is ever needed.

### Tried and REJECTED: yield-ordering the ETB-untap tap-ahead (`MTG_ETB_TAP_YIELD`)

`EtbUntapTapAheadIntoFloat` taps lands ahead of a Drake/Faeries cast so the mana comes back as
float. It is bounded by the untap count, so on a wide board it is a CHOICE — and it made that choice
by battlefield order. On this deck land yields differ 3x (an Overgrowth land yields 3, a plain land
1) and the tapped land's mana is banked *and* the land returns untapped, so the order is worth real
mana. Worked example, 6 lands (one Overgrowth) and count=5: battlefield order floats 5 for 13
available; yield order floats 7 for 15. The gap is exactly the aura bonus, every blink iteration.
The **untap** half of the same mechanic (`EtbUntapLands`) already sorts by yield, so the asymmetry
looked like an oversight.

Measured as one pooled batch, both arms, 4 seeds x 100 games at d5/b20: **−0.0025 turns, t=1.0.**
Not adopted. But the honest verdict is *inconclusive*, not *falsified* — the effect is smaller than
the apparatus's own run-to-run noise (below). Kept default-OFF so it can be re-measured on a fixed
apparatus rather than re-derived from scratch.

### THE APPARATUS ITSELF IS NOT DETERMINISTIC ON THIS DECK (2026-09-03)

Found while trying to use the A/B's OFF arm as a byte-identity control. **The same binary on the
same manifest diverges in 6 of 8 jobs**, and one job moved its average by 0.01 turns. A job run
ALONE is stable (3/3); the divergence needs the pooled batch and scales with pool pressure.

This is the repo's open `batch-run-to-run-divergence` (2026-08-25), recorded since as *cause unknown,
never reproduced* — now reproducible on demand in ~13 minutes on this deck. Full write-up, including
the leading mechanism (a cache surviving across games makes the WORK priced against a deterministic
unit budget vary, which flips the depth reached at the starvation edge) and the next test, is in
**`docs/design/batch-run-to-run-nondeterminism.md`**.

Two consequences for everything above:
* **Play digests are not a valid identity check on this deck** — they change with no code change.
  Conclusions here rest on averages, and the one claim that leaned on digests has been withdrawn.
* **The noise floor for a single 100-game job is ~0.01 turns.** The −0.135 mana-fix result clears it
  by 10x across four seeds and three depths; the −0.0025 tap-yield result does not clear it at all.

### d3 and d5 are BYTE-IDENTICAL — the search never gets past depth 1

Every seed returns the same play digest at both depths (`2edc534107c13e8f`, `6c6e46c03c301bf3`,
`82b255f7a7334649`, `ef6777f6715da5ed`). That is not convergence, it is **starvation**:
`MTG_ROLLOUT_STATS` on a profiled game reports `id_depth hist=1:8` — all eight top-level decisions
committed depth **1** — and `units_total=449881` against a b20 budget of 8 x 18,000. One
un-abortable first pass overruns the whole decision budget 3x, so no deeper pass is ever started
and `--depth` is inert. **The ledger's earlier "converged by depth 3" for the old list was the same
artifact read the other way round.**

### But un-starving it by brute force buys ~nothing (the discriminating test)

The starvation hypothesis FITS; it does not DISCRIMINATE. Paired ladder, 4 seeds x 20 games, d3:

| seed | b20 | b60 | b180 |
|---|---|---|---|
| 4101 | 7.30 | 7.30 | 7.25 |
| 4202 | 7.45 | 7.40 | 7.30 |
| 4303 | 7.00 | 7.05 | 7.00 |
| 4404 | 7.25 | 7.20 | 7.20 |
| **mean** | **7.250** | **7.238** | **7.188** |

**A 9x budget buys 0.063 turns for ~6x the wall clock**, and even then the profiled game only
reaches depth 2 on 2 of 7 decisions (`id_depth hist=1:5 2:2`). So the gap to 4-5 is **not** search
depth, and pouring budget at it is refuted, not merely unattractive.

**The remaining hypothesis is the one the user's estimate is explicitly conditioned on** — "4-5 avg
win turn *with a mulligan profile*". There is no keep table on this deck; the profile is
card-scores-only with `hand_score_threshold = -1e18`, i.e. no hand gate at all. A 4-card combo deck
is exactly the shape where hand selection should dominate.

### But a RULE-BASED hand selection buys nothing either (800 games)

Cheap upper-bound probe before committing hours to generation: four `mulligan` rule variants, pooled
in ONE batch (the manifest takes a per-job `profile`, so all four arms share one tail), 4 held-out
seeds x 50 games each.

| profile arm | s5101 | s5202 | s5303 | s5404 | mean | vs base |
|---|---|---|---|---|---|---|
| P0 base — `min_lands` 1-5, no required pieces | 6.86 | 7.20 | 7.04 | 6.96 | **7.015** | — |
| P1 any combo piece, lands 2-5, `stop_at` 3 | 6.98 | 7.10 | 7.06 | 6.94 | 7.020 | +0.005 |
| P2 payload required, lands 2-5 | 7.08 | 7.04 | 7.30 | 7.02 | 7.110 | +0.095 |
| P3 outlet required, lands 3-4 | 6.98 | 7.26 | 7.04 | 7.04 | 7.080 | +0.065 |

**Every selective rule is neutral or worse** — the extra mulligans cost more than the selection
gains. **This does NOT refute the mulligan stage**, and the distinction matters: `required_pieces` is
a name filter applied to a 7-card hand, whereas the exhaustive keep table is a learned per-hand
keep/bottom decision with bottoming on. It does mean the "a mulligan profile will find the missing
2 turns" story now has a negative datapoint against its cheap proxy, and the generation should be
entered with that in mind rather than as a foregone conclusion.

**So all three cheap explanations for the 7.2 are now measured and none of them is it**: search
depth (+0.063 turns for 9x budget), wish targeting (the ranking tripled win-condition fetches and
changed nothing), and rule-based mulligans (neutral-to-worse). What is left is a genuine modelling
gap, the learned keep table, or an optimistic estimate — and those are distinguished by the Stage 5
verification and the generation stages, not by more measurement of the current configuration.

### TRACTABILITY: the wish axis is ~72% of the cost, and it is a RANKING problem

This deck is now far too slow for the generation stages: 58% of d5 games and 63% of d3 games exceed
5 s, with single games at 65 s, 194 s and one at **801 s**. Attribution on one profiled game
(`--seed 4269 --game-index 67`), each arm a separate process because the levers are process-wide:

| arm | ms | note |
|---|---|---|
| base (width 8) | 90,940 | |
| `MTG_NO_ELDRAZI_GOFF=1` | 100,934 | **slower** — the go-off recognizer is not the cost |
| `MTG_TUTOR_WIDTH=1` | 26,061 | |
| both | 18,824 | |

and the width curve: w1 26.1 s, w2 26.8, w3 38.1, w4 48.5, w6 56.6, **w8 90.9**. The tutor axis is
a straight multiplier on the root plan set (it emits `k-1` extra plans per base plan), so **~72% of
this game's cost is the wish axis alone** — and width 8 is only required because the candidate list
is UNRANKED (see the trap section: the two win conditions sit at ranks 6 and 7 of the sideboard's
decklist order). Ranking converts a width problem into an order problem, which is this repo's
standing lesson on this axis: **narrow, do not widen.**

Built and NOT yet adopted: `EldraziFlickerProvider::TutorCandidates`, a board-aware ranking behind
`MTG_EDF_TUTOR_RANK` (0 off = shipped, 1 flat tiers, 2 tiers with the kill gated on an assemblable
loop; default off, so the shipped path is byte-identical — smoke **48/48, 0 configs changed**). It
ranks by card **params** — `drain_cost`/`exile_opponent_top_cost` = the kill, `blink_cost` = the
outlet, `etb_untap_lands` = the payload — never by name, and demotes any piece already on the
battlefield or in hand, so the head of the list is always what the combo is MISSING.

**Why a gated variant exists at all.** Under the flat tiers the wish takes a sink on turn 2, several
turns before there is any mana to pour into it; the unranked engine took a karoo land that ramps
immediately. Mode 2 therefore only promotes the kill once an outlet AND a payload are already in
play or in hand.

Train A/B, 4 seeds x 20 games, d3/b20. Cost is `units_total` under `MTG_ROLLOUT_STATS` — the
DETERMINISTIC meter, not wall clock, because five arms sharing a box makes ms meaningless (an
earlier ms-based pass had the narrow arm reading *more expensive* than the wide one):

| arm | avg win turn | units | vs base |
|---|---|---|---|
| base (unranked, w8) — shipped | 7.2500 | 25,694,320 | 1.000x |
| rank 1, w8 | 7.2625 | 24,826,247 | 0.966x |
| rank 1, w3 | 7.2750 | 20,040,610 | 0.780x |
| **rank 0 (unranked), w3** | **7.4375** | 21,442,441 | 0.835x |
| rank 2, w8 | **7.2375** | 23,570,597 | 0.917x |
| rank 2, w3 | 7.3125 | 19,804,794 | 0.771x |

**The one effect that is bigger than the noise is the CONTROL.** Narrowing to width 3 *without* a
ranking costs **+0.1875 turns** — 3 to 7 times every other delta here, and the direct measurement of
the coverage trap that was previously only argued. With a ranking the same narrowing costs +0.025 to
+0.06 for ~22% fewer units.

Held-out confirmation, **disjoint seeds 5101-5404 x 50 games** (200 games/arm), replicates all of it:

| arm | avg win turn | vs base | units | vs base |
|---|---|---|---|---|
| base (unranked, w8) | 7.0450 | — | 60,738,202 | 1.000x |
| **rank 0 (unranked), w3** | **7.3250** | **+0.2800** | 50,974,798 | 0.839x |
| **rank 2, w8 — ADOPTED** | **7.0150** | **−0.0300** | 58,752,520 | 0.967x |
| rank 2, w3 | 7.0900 | +0.0450 | 47,669,434 | 0.785x |

### ADOPTED: rank mode 2 at width 8 — and the reason is the CONTROL, not the delta

`MTG_EDF_TUTOR_RANK` now defaults to **2** (`=0` restores the old zone-order list). Smoke **48/48,
0 configs changed** — no other deck routes through this provider and this deck is not in the
regression suite.

**−0.0300 turns is inside the noise and is not the case for shipping it** (t≈1.6 on 4 seeds; it is
better on 3 of the 4, and −0.0125 on train, so the sign is at least consistent). The case is the
control arm: unranked narrowing costs **+0.2800 turns held-out / +0.1875 train**, measured twice on
disjoint seeds. The ranking is what converts the width from a coverage cliff into a priced dial —
width 3 is now a measured option at +0.045 turns for −21.5% units, available if generation cost
demands it.

**And the behaviour change is worth reading, because it did not do what it was supposed to.** Over
12 logged games the wish went from taking a LAND 7 times of 12 (and a win condition twice) to taking
a win condition 6 times of 11 — exactly the intent — **and the deck did not get faster.** So owning
the sink is not this deck's bottleneck; having the loop to feed it is. The logged turn-6 kill was
gated on Training Grounds landing, not on the Infiltrator.

### Open items carried forward

* ~~**A 30 s SLOW-GAME appeared**~~ — **DIAGNOSED** (see the tractability section). The wish axis is
  ~72% of the pathological game's cost; the go-off recognizer is not implicated (disabling it made
  the game *slower*). Adopting the ranking took the arm to 0.967x units and puts a measured
  0.785x on the table via width 3. **The deck is still expensive in absolute terms** — the full
  measurement saw single games at 65 s, 194 s and one at **801 s** — so a feasibility check is still
  required before the value-leaf and mulligan generations, and that is now the open item.
* **THE HEADLINE GAP IS NOT CLOSED — but it narrowed, and the thing that narrowed it says where to
  look next.** Now **7.07** (was 7.21) against the user's 4-5 estimate. Three cheap explanations are
  refuted by measurement: more search budget buys +0.063 turns (the wrong way), pointing the wish at
  the win condition buys −0.030 (inside noise), and rule-based mulligans buy nothing. The ONE thing
  that moved it was fixing two **modelling** bugs (−0.135 at ship depth, t=6.6) — bigger than every
  tuning knob tried on this deck combined. **So the next hunt is for more modelling gaps, not another
  lever.** The remaining untested candidate is the learned exhaustive keep table, which is what the
  user's estimate was conditioned on and which has not been generated.
* **The pool projections over-credit energy** (three Hubs sharing one energy project 3 wild).
  Over-credit only, so a line is enumerated and then dropped rather than played illegally; the
  ceiling is 3 energy for the whole game. Threading an energy budget through the seven pool
  builders is the `gy_fuel` pattern and is deferred.
* **`g_play_land_rad_chooser` is dead** — the hook and call site exist, nothing sets the pointer.
* **`EnumeratePlans` does not add `ExtraLethalDamage`** where its twin in `Solve` does. Pre-existing
  asymmetry, its own question; the alternate-win check was added to both.
* ~~**The wish target ranking is generic (decklist order).**~~ — **CLOSED, and it was a measured
  question with a measured answer.** Ranked (mode 2) and ADOPTED; the width stays 8 because
  narrowing is now a priced choice rather than a forced one.

### THE TRAP the Stage-2 fan-out found: the wish cannot reach either win condition

Two independent agents landed on the same defect, and it is the third instance of this exact shape in
this deck's history.

`DecisionProvider::TutorSearchWidth()` defaults to **6**. `GenericProvider::TutorCandidates` returns
candidates in **zone order**, and for a sideboard that is **decklist order, identical in every game
of every seed**:

```
0 Azorius Chancery  1 Adarkar Wastes  2 Cloud of Faeries  3 Eldrazi Displacer
4 Mariposa Mil.Base 5 Vexing Shusher  6 Essence Depleter  7 Dimensional Infiltrator
```

`EldraziFlickerProvider` overrides neither hook, and the axis fan-out emits ranks `0 .. width-1`. So
**ranks 6 and 7 — Essence Depleter and Dimensional Infiltrator, i.e. BOTH win conditions — would be
unreachable by the search at every depth and every budget, deterministically, forever.** The deck
would measure as though it had no kill, and nothing in any report would say why.

The precedents, all in this repo: Natural Order, where "neither the greedy default nor any searched
variant could reach the deck's win condition, so no projection anywhere priced the T4 kill"; Stroke
of Genius shipped as a no-op; the Shivan Gorge the loop untapped exactly zero times. The fix is a
**coverage** fix, not a heuristic one: `EldraziFlickerProvider::TutorSearchWidth() → 8` (the pool
size), plus a provider ranking so file order stops deciding it — and falling through to
`GenericProvider` under `UnprunedGate::Tutor` so human play still sees every legal name.

### Other findings from the fan-out worth keeping

* **`rad_counters` was folded into NEITHER `BuildSimKey` NOR `Dominance`.** A key-hole I introduced
  with the rad mode: two states differing only in rad counters shared a TT/dedup entry, and rad is
  future-determining twice over (it cheapens Mariposa's draw AND fires a mill + life loss at every
  precombat main). Identical to the defect `storage_counters` once had, which canon exposed on
  dragonstorm. **Fixed**, gated on nonzero so every other deck keeps its exact prior key.
* **`PerformTutor` calls `ShuffleAfterSearch` UNCONDITIONALLY** — it is not gated on
  `tutor_shuffle_after`. So "just don't set the shuffle param" is not enough for a wish: a wish does
  not search a library, CR 701.19c never triggers, and letting it advance `search_count` would
  perturb every later fetch's deterministic reshuffle. The call site needs an explicit gate.
* **`EffectiveProduces` is NOT a complete choke point.** The payment DFS
  (`SpellEffects.cpp` `TapForCostBacktrackWorker`) and the flow oracle (`TapFlowInfeasible`) each
  inline their own `produces` resolution and bypass it. An energy gate added only to
  `EffectiveProduces` would look right in every pool projection and every rank ladder while the
  actual solver tapped three Hubs off one energy.
* **Aether Hub's energy must be metered, and this is not a nicety.** Peregrine Drake and Cloud of
  Faeries untap lands on every blink iteration, so a Hub is untapped and re-tapped an unbounded
  number of times per turn. Modelled as a plain 6-colour land it becomes an **infinite any-colour
  source**, switching on the `{2}{R}` Gorge kill for free and making the deck look far faster than
  it is. Energy is also a **player** resource, not a permanent's: under the untap loop, correct play
  is to tap ONE Hub three times spending all three Hubs' energy.
* **Vexing Shusher is not worth zero.** With no blocker path it is an unopposed 2/2 clock — legal
  but dominated, not dead. So no `card_scores` entry (those are opening-hand marginals and a
  sideboard card can never be in an opening hand) and no provider exclusion: let the search price it.
* **`g_play_land_rad_chooser` is DEAD** — the hook and the call site exist, but nothing ever sets the
  pointer (no `WriteLandRadDecisionJson`, no GUI branch, no registry row). A 2c-ter gap, open.
* **Human play is the OOM risk on the wish, not the search.** `--claude-play` forces `MTG_UNPRUNED`,
  and 8 names x up to 4 castable Living Wishes is `8^k` plan variants on the deck that already
  OOM-killed the box at 46 GB. Use the Turntimber route: emit ONE empty-target cast and defer the
  pick to `g_play_tutor_chooser` at resolution.

### Verified oracle text (Scryfall, 2026-09-02)

| card | cost | text |
|---|---|---|
| Living Wish | `{1}{G}` Sorcery | "You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living Wish." |
| Aether Hub | Land | "When this land enters, you get {E} (an energy counter). / {T}: Add {C}. / {T}, Pay {E}: Add one mana of any color." |
| Vexing Shusher | `{R/G}{R/G}` 2/2 | "This spell can't be countered. / {R/G}: Target spell can't be countered." |
| Essence Depleter | `{2}{B}` 2/3 Devoid | "{1}{C}: Target opponent loses 1 life and you gain 1 life." |
| Dimensional Infiltrator | `{1}{U}` 2/1 Devoid, Flash, Flying | "{1}{C}: Target opponent exiles the top card of their library. If it's a land card, you may return this creature to its owner's hand." |

Infiltrator's optional bounce is **always declined**, and unlike Mariposa's rad mode that is
provably dominant rather than a judgement call: it has no ETB to re-trigger, there is no removal to
dodge, and returning it mid-combo just costs `{1}{U}` to recast.

### In the working tree (builds clean, NOT yet measured)

Essence Depleter's engine half: `drain_cost` / `drain_amount` / `drain_self_gain` params,
`PermAbilityMode::Drain` + its resolver, and `SpendSurplusOnDrain` — which carries an inner loop
where `SpendSurplusOnDamageSinks` does not, because a `{T}`-less sink should spend EVERYTHING at
once rather than fire once per untap.

---

# (BELOW: the OLD list's analysis, retained for its engine findings)

## What the deck is

An **infinite-mana flicker combo** deck. A blink outlet plus a creature whose ETB untaps lands
nets mana on every activation, because neither outlet's cost contains `{T}` and neither is
once-per-turn — so an outlet can go off the turn it lands.

| piece | role |
|---|---|
| Eldrazi Displacer `{2}{W}` 3/3 | outlet — `{2}{C}: Exile another target creature, then return it **tapped**` |
| Emiel the Blessed `{2}{W}{W}` 4/4 | outlet — `{3}: Exile another target creature **you control**, then return it`; also a `{G/W}` +1/+1-counter watcher on every other creature ETB |
| Peregrine Drake `{4}{U}` 2/3 | payload — ETB **untap up to five lands** |
| Cloud of Faeries `{1}{U}` 1/1 | payload — ETB **untap up to two lands**; Cycling `{2}` |
| Training Grounds `{U}` | your creatures' activated abilities cost `{2}` less, floor one mana → Emiel `{3}`→`{1}`, Displacer `{2}{C}`→`{C}` |
| Wild Growth / Fertile Ground / Overgrowth / Trace of Abundance | land Auras: enchanted land taps for **+{G} / +any / +{G}{G} / +any** |
| Shivan Gorge (Legendary Land) | **kill A** — `{2}{R}, {T}: 1 damage to each opponent`, re-untapped every iteration |
| Emiel's counter watcher | VALUE ONLY — **not** a kill. CR 400.7 makes the returned creature a NEW OBJECT, so each blink WIPES the previous counter: a 13-iteration loop leaves ONE counter, not 13 (measured, Stage 5d). |
| Stroke of Genius `{X}{2}{U}` | **the sink** — with unbounded mana this draws the library |

### The sinks, and why the sink is the whole problem

Unbounded mana wins nothing by itself. The go-off heuristic therefore sizes the loop to whichever
sink the board supports — Gorge damage, Emiel counters, or an `{X}` draw — and proposes **no**
go-off at all when none is available, because a loop whose mana cannot be cashed is pure cost.

## Stage 1 / 3 — coverage

Stage 1: 17 missing cards, 2 with bracket notes. **Stage 3 is CLEAN: 0 missing, 0 partial, 0 gaps.**

### Reclassified bracket note — Brushland's `{C}` mode was NOT inert

Brushland shipped as a G/W painland noting *"the free `{C}` mode is not modelled — our life loss is
inert for the goldfish clock"*. That reasoning does not survive a deck with a **colourless PIP** in
an activation cost (`{2}{C}`): no coloured mana pays `{C}` (CR 107.4c). Modelled now, for Brushland
and the two new painlands, with the `{C}` tap correctly **painless** (they are separate abilities).

## Stage 2 — what was built

All engine additions are param-gated; no other deck sets any of them.

| mechanic | where |
|---|---|
| `ManaPool::wild_c` — a `{C}` pip needs real colourless | `src/core/ManaPool.h` |
| `etb_untap_lands` — real untap + tap-ahead + enumeration credit | `SpellEffects.h`, `ManaPayment.cpp`, `TurnSolver.cpp`, `AIEngine.cpp` |
| Land Auras (`is_land_aura`) — first channel by which one permanent changes another's mana yield | `SpellEffects.h` (`LandAuraBonus` / `LandAuraAddToPool`), `AddSourceToPool` gains a `const Permanent*` |
| `Action::ActivateBlink` — repeatable non-tap activation; target and count are separate searched axes | `TurnSolver.h/.cpp`, `AIEngine.cpp`, shared `ApplyBlinkLoop` |
| `Action::ActivatePermAbility` — Gorge damage / Investigate + Clue / Mariposa draw | same |
| `EffectiveActivationCost` (Training Grounds) — reduces ACTIVATION costs, floor one mana | `SpellEffects.h` |
| Emiel's optional `{G/W}` watcher + `g_etb_optional_payer` hook | `SpellEffects.h` |
| Painless `{C}` painland mode | `ManaPayment.cpp` |

## Stage 5 — findings

### The performance problem, and what actually fixed it

The deck was **1.38 × 10⁹ odometer positions and 78 s for ONE d3 game** (Dragonstorm, the next
heaviest combo deck, is 553 units). `MTG_ENUM_STATS` pinned it on two fan-outs, both narrowed in
the provider with `MTG_UNPRUNE` gates (`blinktarget`, `landaurahost`):

* land-Aura host — one cast variant per legal land, 7³ across three auras in hand;
* blink target × count — 12 variants per outlet, multiplying across outlets.

Now **~1.5 s/game at d5/b20**. Profiling (not intuition) drove this: the first guess was that
`LandAuraBonus`'s battlefield scan inside `PermanentManaYield` was the cost — `perf` showed it was
not even in the top 20, and the odometer was 58%.

**Measurement hygiene note:** `MTG_ENUM_STATS` / `MTG_ROLLOUT_STATS` are NOT free — the same game
timed 48 s with them on and 19 s off. Time without them. Host load also varies (`loadavg` 15–23
with an idle container), so repeat every timing.

### THE BUG THE GO-OFF HEURISTIC HID: the sink could only ever fire once

`EtbUntapLands` untaps the **highest-yield** lands. Shivan Gorge makes `{C}` — yield 1 — so on a
board of Overgrowth'd lands it is never in the top five and **the loop untapped it exactly never**.
The go-off was recognised and proposed on 708 nodes of a 3-game sample and executed as a kill zero
times, because one Gorge activation was all any loop could buy.

Fixed with a scoped `g_etb_untap_priority` (set only by `ApplyBlinkLoop`) plus a reordering of the
loop body so the sink fires **before** the tap-ahead — otherwise the tap-ahead taps the Gorge for
its one `{C}` and the damage ability finds it already tapped.

Measured over 20 games at d5/b20 (paired seeds):

| arm | avg turn | unwon | blinks | go-offs (K>3) |
|---|---|---|---|---|
| A baseline | 7.200 | 2 | 25 | 7 |
| B + blink mana credit | 7.250 | 3 | 22 | 7 |
| C + untap priority & reorder | **6.850** | **1** | 27 | **12** |

The combo does fire: `blink x20`, `blink x14`, `blink x12` appear in the logs, alongside Clue
tokens being made and cracked.

## Stage 5 — verification results (2026-09-01)

| gate | result |
|---|---|
| Stage 3 coverage | **CLEAN** — 0 missing, 0 partial, 0 gaps |
| 2d-bis Scryfall cost audit | **CLEAN** — "All mana costs match Scryfall" (218 costed cards) |
| field/oracle diff (this deck's 19) | **ALL MATCH** verbatim: cost, P/T, types, supertypes, subtypes, keywords, oracle text |
| 5a `[nonconv]` | **0 lines / 1,200 games** (d3, 4 seeds x 100) |
| 5a `[fd-diverge]` | **0 lines / 1,200 games** (`MTG_FULL_DEPTH` + `MTG_FD_ORACLE`) |
| 5b multi-depth | **monotonic**: d0 8.52 -> d3 7.12 -> d5 7.12 (converged) |
| 5c2 horizon-honest tie-break | **NO SIGN AT THIS SAMPLE** — 15 changed of 400 paired (3.75%), net -5 turns (10 better / 5 worse). Directionally helping, below the 20-game threshold. Decisive run (`--blocks 4`) queued. |
| 5h viewer self-guard | **PASSES** — every new param classified; one DISCLOSED GAP (below) |
| 5d claude-play sweep | 16 games x2 (pre- and post-fix). **6 real bugs found and FIXED**; re-sweep on a frozen binary reproduced none of them |
| `verify_deck.py` (the one-command gate) | **GATE PASS** — every blocking check green |
| suite | smoke **48/48**, regression **80/80**, 0 configs changed |
| CI | ubuntu + windows + **Linux/Windows determinism parity** all green |

### Multi-depth detail (4 seeds x 100 games)

| depth | s4101 | s4202 | s4303 | s4404 | mean |
|---|---|---|---|---|---|
| d0 | 8.42 | 8.43 | 8.67 | 8.56 | 8.52 |
| d3 | 7.16 | 7.09 | 7.20 | 7.04 | **7.12** |
| d5 | 7.15 | 7.10 | 7.20 | 7.04 | **7.12** |

d5 == d3 to two decimals on every seed: the search has **converged by depth 3** on this deck.

### COST — the honest number, and a measurement trap

**~20-23 s/game wall at d3/b20 and d5/b20** (100-game batches, 24 threads). A single-game probe
said 1.5 s and was badly unrepresentative: the distribution has a heavy tail, with individual
`SLOW-GAME` lines at **40-100 s**. Do not size anything off one game.

Also: `MTG_ENUM_STATS` / `MTG_ROLLOUT_STATS` are NOT free -- the same game timed 48 s with them on
and 19 s off. Time without them, and repeat (host `loadavg` ran 15-23 with an idle container).

This is a **rollout-bound** profile (164k rollout calls / 2,326 interior nodes, i.e. ~96% rollout),
which is the shape the VALUE LEAF exists for -- the documented next stage, not an enumeration
problem.

## Stage 5d — the claude-play sweep found four real bugs

Sixteen Sonnet agents, one game each, base seed 8801. **The sweep paid for itself**: every finding
below was flagged independently by several agents, reproduced by hand, and traced to code written
in this branch. Full write-up in the commit `dc60cabe`.

| # | bug | how it was caught |
|---|---|---|
| 1 | **An ETB refund paid for itself.** The Drake/Faerie cast credited `EtbUntapLandsCredit` as `ritual_float`, which is summed into the pool the subset's combined cost is checked against — so "untap up to N lands" funded the very creature whose ETB does the untapping. | 12 of 16 agents flagged "Drake offered as castable with 2-4 mana"; once the ONLY plan for 11 consecutive decisions, once a decision loop escapable only by passing. Repro: turn 1, EMPTY board, plan 0 = "land=Yavimaya Coast; cast: Cloud of Faeries". |
| 2 | **A `{cost},{T}` permanent tapped ITSELF** toward its own mana cost (a permanent taps once, CR 602.2a). | Mana arithmetic cross-checked against the LIFE TOTAL — only one painland tap had occurred, so the 4th mana had to have come from the Conservatory paying its own `{4}`. |
| 3 | **The tap-ahead floated `wild` mana** = free colour fixing. `AddSourceToPool` books a multi-colour land as wild; `RitualTapAheadIntoFloat` deliberately commits a colour and mine did not. | An agent watched Emiel resolve `{W}{W}` off floating `{G:1, wild:2}` with **no white source ever tapped**. |
| 4 | **Emiel's counters do NOT accumulate.** CR 400.7 — the returned permanent is a NEW OBJECT, so each blink wipes the previous counter. My heuristic and card note both claimed otherwise. | An agent ran a 13-iteration Emiel loop and counted **one** counter, not 13. |

### What the fixes cost, and the split that matters

Paired, 4 seeds x 100 games at d5/b20:

| arm | avg turn |
|---|---|
| before any fix (using illegal mana) | 7.12 |
| the two illegal-mana fixes, credit ON | 7.37 (+0.25) |
| **shipped** (also credit removed) | **7.80** (+0.43) |

* **+0.25 is a CORRECTION, not a regression** — the deck losing mana it should never have had.
* **+0.43 is enumeration breadth** from dropping the ETB credit. The credit's *reasoning* was right;
  only its implementation was unsound. **That 0.43 is a measured improvement still on the table**
  and is this deck's most valuable follow-up (see below).

`MTG_EDF_ETB_CREDIT=1` restores the unsound credit for attribution only. Default OFF; keep it that way.

### Disclosed methodology defect (caught by an agent, not by me)

I **rebuilt the binary while the first sweep was running**, violating the stateless-replay
protocol's one-fixed-binary assumption. The four findings were each reproduced independently
afterwards, but the sweep was re-run on a binary frozen at `f5f664bf` before recording the gate.

## Stage 5d re-sweep (frozen binary `f5f664bf`) — one root cause, two symptoms

All 16 agents re-ran on a binary that did not move. **None of the four earlier bugs reproduced**
(several agents checked each explicitly). One systemic defect remained, reported by every agent and
independently traced to source by two of them — and it turned out to be ONE root cause with two
symptoms, both from reusing `is_aura` for land auras:

| symptom | site | effect |
|---|---|---|
| land auras offered targeting a CREATURE ("Wild Growth → Emiel the Blessed") | `AppendCreatureTargetAuraCandidates` gated on `is_aura` without excluding `is_land_aura` | the plan shown to the player described a play CR 303.4 forbids (execution stayed legal — `ResolveEnchantTarget` silently redirected to a land) |
| **standalone land-aura casts never enumerated at all** | `SubsetHasAuraOnUncastCreature`'s `on_bf` looks for a **creature** with that m_number; a land aura's host is a LAND, so it never matched and the guard **rejected every subset containing one** | the deck's entire ramp package was unreachable — the engine's own `CheckLine` said `legal_not_enumerated`, which three agents quoted |

Also fixed, from the same sweep: `ActivateBlink` had no `SummarizePlan` case and never emitted its
target, so every blink variant rendered as an identical `"Eldrazi Displacer (other)"`. An agent
declined every blink option rather than guess — for a deck whose engine IS a targeted repeatable
activation, that made the combo unverifiable through the protocol. Now emits `blink_target` /
`blink_target_name` / `blink_count`, and `EnchantTargetName` resolves any controlled permanent (not
just creatures) so a land host reads "Kitchen" instead of "#31".

### The full measurement arc

| arm | avg turn |
|---|---|
| before any fix (using illegal mana, ramp unreachable) | 7.12 |
| after the four correctness fixes | 7.80 |
| **after the land-aura fixes (shipped)** | **7.26** |

So the net cost of every correctness fix is **+0.14 turns**, not +0.68: the land-aura repair gave
back 0.54 of it by making the deck's ramp usable for the first time. Wall time also fell ~20%.

## The OOM — and why fixing the auras is what caused it

**The box was OOM-killed** (`dmesg` 2026-09-01 19:17:19: `mtg` at 46 GB anon-rss on a 47 GB host),
taking the user's VSCode session with it. Same class as
[claude-play-unprune-blowup.md](claude-play-unprune-blowup.md).

`--claude-play` does `setenv("MTG_UNPRUNED","1")` for the whole session, so **both** of this deck's
provider narrowings stand down and the blink fan-out becomes (every creature) × (every count) **per
outlet**, squared across two outlets: 21 variants each, an 8.67e5 odometer bound, **278,783 plans
and 3.2 GB RSS for ONE decision**. At the reference sweep's 24 concurrent replays that is 77 GB.

**The land-aura fix is what made it bite.** Before it, `SubsetHasAuraOnUncastCreature` rejected every
subset containing a land aura, which incidentally kept the product small. Repairing the auras removed
that accidental brake and the real fan-out appeared — a fix exposing a latent degeneracy, exactly the
pattern the skill's core-invariant note describes for the removed plan-dedup limiter.

Two folds, **human-play/unpruned only**, both lossless in reachable lines:

* **count → 1.** K is a REPETITION of a decision, not a separate one: the main phase re-prompts after
  each activation (verified — `main_ordinal` increments), so a human reaches "blink three times" by
  choosing it three times, which is strictly *more* expressive than a fixed `{1,2,3}` menu. The
  autonomous search keeps its counts and its go-off (it commits a whole turn at once).
* **duplicate targets fold** on every property a blink can read (name, tapped, summoning sickness,
  P/T, counters, controller). The deck runs 4-ofs.

| | before | after |
|---|---|---|
| peak RSS, one decision | 3.2 GB | **16.6 MB** (193×) |
| elapsed | 9.08 s | **0.09 s** (100×) |
| `play_invariants` | OOM | **PASS** — 8 games, 1168 decisions, 18 MB peak |

The decision is **preserved and now legible**: the human is offered "Eldrazi Displacer: blink Cloud
of Faeries" vs "… blink Peregrine Drake" as two distinct labelled plans — precisely the choice a
sweep agent said it could not make, and declined every blink rather than guess. The autonomous combo
is untouched (21 blink activations over 20 games, including `blink x25` / `x26` / `x17`).

**Lesson for the next deck with a wide targeted activation:** measure the HUMAN-PLAY plan count, not
just the autonomous one. `MTG_UNPRUNED` makes them completely different numbers, and only the
autonomous one is covered by the regression suite.

## Sequenced ETB rescue — MEASURED and ADOPTED (2026-09-02)

**`MTG_EDF_SEQ_ETB`, default ON. −0.0338 avg win turns, paired t = −3.97 over 800 paired games,
8/8 seeds better, and 6.3% CHEAPER.**

Recommended-next-step #1, done the sound way. The version the Stage 5d sweep refuted credited the
ETB refund into the flat subset pool, which let a Peregrine Drake pay for itself. This one leaves
the flat gate honest and routes the chain to the **sequenced walk** that already existed:
`EnumeratePlans`' `exec_feas_rescues()` runs `SubsetPayableSequential` when a mana gate is about to
reject an "interacting" subset. That predicate knew about `ritual_float` / `rock_mana` /
`untap_x_mana_sources` but **not** `etb_untap_lands`, so a Drake chain was rejected and dropped.

`SubsetPayableSequential` is the right home because it pays each cast in `CastOrderRank` order
against a real `GameState`, firing the untap between casts — it answers "is this chain payable IN
ORDER" instead of "is the total big enough". It is **conservative by construction** (it only ever
RESCUES a subset a gate already rejected, never rejects one a gate accepted), and
`EldraziFlickerProvider::CastOrderRank` already ranks the payload (6) before the outlet (7), so the
Drake is paid first and its untap funds what follows.

### The finding that changed the design: it was DEAD CODE as first written

The clause was originally a sub-clause of `MTG_EXEC_FEAS` — and **that flag is default OFF**, so at
ship settings the rescue never ran and the clause could not have done anything. A 3-arm scout (25
games, seed 4101) proved it and also priced the alternative:

| arm | avg | digest | cost |
|---|---|---|---|
| baseline | 7.32 | `0bb2a78ada094405` | 282.9 s |
| `MTG_EXEC_FEAS` alone | 7.32 | `0bb2a78ada094405` — **byte-identical** | 281.3 s |
| `MTG_EXEC_FEAS` + the clause | 7.24 | `0a76a030c1e94d81` | 275.6 s |

So turning the general rescue on buys this deck **exactly nothing** on its own; the whole effect is
the ETB clause. The two admissions are therefore now **independent** in `exec_feas_rescues()`: with
`MTG_EXEC_FEAS` off, the only subsets that reach the walk are ones holding an `etb_untap_lands`
cast. No other deck in the repo has one, which is why this ships without moving any other deck.

*(This is the "sweep levers in COMBINATION" lesson from the other direction — a lever whose entire
effect is invisible unless you also measure the gate it hides behind.)*

### The measurement

One pooled 16-job batch (2 arms × 8 seeds × 100 games, `MTG_DUMP_WINS=1`), 23.9 of 24 cores start to
finish. Paired on `(seed, gi)` — the arms share deck, profile and shuffle, so the same `gi` is
literally the same library. Losses scored at `max_turns+1`.

| | avg win turn |
|---|---|
| `off` | 7.2375 |
| `on` | **7.2038** |
| **delta** | **−0.0338** (se 0.0085, **t −3.97**) |

- **8/8 seeds better**, seed-level t −8.04.
- **26 games better : 4 worse : 770 identical** — it fires rarely, and when it fires it usually helps.
- Cost 0.937× the baseline (7,890,693 vs 8,417,165 job-ms): a *widener* that is **cheaper**, because
  the lines it unlocks end games sooner and a shorter game is less search.
- The `off` arm reproduced the shipped **7.26** on the original four seeds, and every digest matched
  across two independent runs — baseline and determinism both confirmed.

**Correction to the earlier estimate.** The "~0.43 turns" recorded as this item's value came from
the *refuted* flat-pool credit, and most of it was the unsound part — a Drake paying for its own
cast is mana the deck does not have. The sound version is worth **−0.034**. Real, free, and small.

## Two USER-CAUGHT correctness bugs, and a refuted generalisation (2026-09-02)

Both bugs were found by the user reading the PROVISIONAL list, not by any harness. Worth recording
because the two misses have the same shape: I checked a card's clause against the *opponent* and
concluded "inert", when the clause actually constrains **our own** plays.

### 1. Trace of Abundance's shroud — I called it inert, and it was a live illegal play

I wrote off shroud because "the passive opponent casts nothing and no card in this deck targets a
land". The user: *"The shroud is actually important, since it can get in the way of us putting more
enchantments on it."* Correct, and the rule is not subtle — **an Aura spell targets its host as it
is cast** (CR 303.4a), and shroud (CR 702.18a) is symmetric, so it stops *our* auras too. A land
carrying a Trace can take no further auras.

It is not a rare corner. The deck runs **~23 land auras**, and `LandAuraHostCandidates` scores hosts
by yield — so the Trace's own +1 makes the shrouded land the **highest-scoring** host. The ranking
aimed directly at the illegal play.

Verified by A/B on the card parameter (`cards.json` is runtime, so no rebuild needed), on a fixture
with a Trace'd Yavimaya Coast plus a clean Brushland and Forest:

| `land_aura_grants_shroud` | Overgrowth attaches to |
|---|---|
| off (pre-fix) | **Yavimaya Coast** — the shrouded land, **illegal** |
| on (fixed) | Brushland — legal |

Life totals and win turn are **identical** either way, which is why no existing assertion could see
it. So `--scenario` gained **`expect_attachment`** (`{"<aura>": "<host>"}`), and
`test/scenarios/edf_shroud_blocks_second_aura.json` guards it — confirmed to PASS with the fix and
FAIL without it. Enforced in `LegalEnchantTargets`, `ResolveEnchantTarget` (both arms) and
`LandAuraHostCandidates`; the last one matters because that hook *narrows*, so proposing only
shrouded hosts would drop an aura that has a legal home elsewhere.

Cast order also splits: a shroud-granting land aura now ranks **last among land auras** (3 vs 4), so
the legal Overgrowth-then-Trace stack stays available while Trace-then-Overgrowth (illegal) does
not. That also keeps enumeration honest — its frozen pre-shroud snapshot is correct under this
order and wrong under any other.

Still disclosed as inert **vs the opponent**: that half genuinely is.

### 2. Emiel's `{G/W}` was paid EVERY blink iteration — which can un-make the combo

The user: *"5 is potentially okay, but this would only work if we never prevent any other casting.
As you mentioned this is silly to do when we plan to flicker the creature, when we are going off
being the biggest example."*

The loop installed its payer for every pass, so `{G/W}` was paid on each one. Two costs, and the
second is the serious one:
* CR 400.7 makes the returned permanent a NEW OBJECT, so pass k+1 **wipes** the counter pass k paid
  for — K payments buy exactly one surviving counter;
* `{G/W}` per iteration comes straight off the loop's **per-iteration margin**. A loop netting +1
  mana a pass is unbounded; the same loop paying `{G/W}` every pass nets 0 and **is not a combo at
  all**. An always-pay resolution heuristic could silently delete the deck's win condition.

Now paid on the **final** iteration only (a *declining* payer is installed for the others — a null
one would fall through to the turn float and pay anyway).

### 3. Untap priority: the mechanism generalised, the policy REFUTED by measurement

The user: *"It's okay to untap by per-tap yield by default most of the time. However, we should have
the ability to do something different when we go infinite."* The override existed but was hardcoded
to a single damage sink, so `g_etb_untap_priority` is now an **ordered set** — the capability, stated
generally: once mana is unbounded, mana stops being the objective.

The obvious policy to put in it — also promote draw/investigate sinks — **loses**, and the
measurement is decisive: **+0.0437 turns, t +5.99, 8/8 seeds worse** (paired, 800 games a side).
The mechanism explains the sign: a promotion is only worth its displaced yield if the loop can
**cash** the promoted permanent, and the loop's per-iteration spend is `SpendSurplusOnDamageSinks` —
damage and nothing else. Untapping a draw land instead of an Overgrowth'd one converts real loop
mana into an ability no iteration activates. So the policy is damage-only, and the code says plainly
that adding a sink class is only correct alongside a matching per-iteration spend for it.

### What the correctness fixes cost

| | avg win turn |
|---|---|
| before (shipped) | 7.2038 |
| after shroud + Emiel-last | **7.2200** |

**+0.0163 turns** (se 0.0092, t +1.76, 8 seeds x 100 games paired). That is the honest price of no
longer making an illegal play, and it is the right trade: the previous number was partly bought with
auras the rules do not allow.

## Mariposa's rad mode — now a SEARCHED decision, and it WINS (2026-09-02)

The user rejected the hardcoded always-decline: *"We probably shouldn't always decline the rad
counters, since it draws more cheaply with them out."* Correct on both counts — it was a greedy
heuristic standing where a searched decision belongs, and declining was leaving value on the table.

**It was never really "declined", it was never implemented.** `etb_optional_tapped_rad` was parsed
and then read by nothing, and `Player::rad_counters` only ever appeared in the draw discount. So this
was a build, not a flag flip.

**Modelling the MILL is the precondition, not an extra.** The rad rule is inherent to the counter
type rather than printed on the card: *at the beginning of your precombat main, mill that many; for
each NONLAND milled, lose 1 life and remove a rad counter.* Without it the mode is strictly upside
and the search takes it every time — the over-acceptance class that later shows up as `[fd-diverge]`.
`ApplyRadMill` fires from `GameEngine::MainPhase` and the rollout's per-turn main, at the same point
relative to the draw, so the executor realises what the search scored.

Shaped as a fourth plan-level land sub-decision alongside `fetch_target` / `land_face` /
`scry_choice`: `Plan::rad_mode`, one variant per mode, carried into the drop through
`LandPlayOptions::rad_mode` by both worlds. Human play gets its own chooser
(`g_play_land_rad_chooser`), gated on its own pointer rather than `honor_entry_chooser` so it does
not drag the unrelated shock/reveal axis onto the executor's drop.

| | avg win turn |
|---|---|
| always-decline (hardcoded) | 7.2200 |
| **searched rad mode** | **7.2037** |

**−0.0163 turns, se 0.0062, t −2.60**, 5 seeds better : 3 unchanged : **0 worse** (8 x 100 paired).
That almost exactly cancels the +0.0163 the shroud and Emiel correctness fixes cost.

**Verified as a real choice, not a hidden default** — the lesson from the `MTG_EXEC_FEAS` dead-code
miss was applied deliberately here. Temporary instrumentation showed the accept variant **emitted**
(13x on one fixture) and **simulated** (`mode=1 take=1`, 16x) alongside the decline arm, and the mill
firing at 2 counters. All instrumentation removed before commit.

**And it is guarded.** The mill is unreachable from a fixture while the search prefers to decline, so
`--scenario` gained a `rad_counters` staging field (the same argument as the existing
`storage_counters` / `charge_counters`) and `test/scenarios/edf_rad_mill.json` pins it: 2 counters,
a nonland library, **exactly 2 life lost over 4 turns (20 -> 18)**. The decay is the assertion's real
content — a mill that failed to remove counters would keep draining well past 18.

The timing that makes the trade close, and worth keeping in mind if this is ever revisited: the land
enters tapped, so the discounted draw cannot be activated the turn it arrives, and the mill fires at
the head of the **next** precombat main — before that draw ever becomes activatable.

## Recommended next steps

1. ~~**Credit the ETB refund toward SUBSEQUENT casts only**~~ — **DONE 2026-09-02**, adopted at
   −0.0338 turns; see the section above.
2. **Value leaf** — **(2026-09-03 observation: NOT live on this box — no `mtg`/valueleaf
   process, no `logs/vlq_eldrazidisplacerflicker/` queue, no `logs/edf/valueleaf_run.out`, and
   no `EldraziDisplacerFlicker.value.json`. Either it died or it runs elsewhere; `valueleaf.sh
   run` resumes incrementally if the src tree still matches the freeze. Items 3/6 below should
   not be treated as blocked on a run that is not running.)** Original record: **RUNNING since
   2026-09-02 01:26**, frozen at `e5924a43` / src-tree
   `1e7d7bebdd14`, play fingerprint `52b9ec620b92`. `bash scripts/valueleaf.sh status
   decks/EldraziDisplacerFlicker` for progress; driver log `logs/edf/valueleaf_run.out`, queue
   `logs/vlq_eldrazidisplacerflicker/`. The deck is rollout-bound and its d0 policy is 1.4 turns
   worse than the search (8.52 vs 7.12), so the leaf is the highest-leverage lever available. This
   is also the gate that decides whether the deck can afford mulligan generation at all.
   **It owns the box** — nothing else may run alongside it (value-leaf skill / CLAUDE.md), which is
   why items 3 and 6 below are queued behind it rather than run in parallel.
3. **Decisive 5c2 run** at `--blocks 4`.
4. **Do NOT add to the regression suite yet** — at ~20 s/game it would dominate the smoke budget.
   Revisit after the value leaf.
5. Wire the `etb_untap_lands` chooser (below).
6. Re-run the discard-analysis stage on a frozen binary (the first run's verdict, `STATUS_QUO_OK`,
   was measured across a binary that changed mid-run, and the regeneration used a deliberately
   under-sampled 20-game evidence pass).

## Open items / PROVISIONAL decisions (need user sign-off)

1. ~~**Mariposa Military Base's rad-counter mode is always DECLINED**~~ — **BUILT AND ADOPTED
   2026-09-02**, and the user's instinct was right: it is a **measured win**. Full write-up below.

2. **`etb_untap_lands` is a DISCLOSED VIEWER GAP.** "Untap up to N lands" is a real choice, is
   auto-resolved by yield order, and a human cannot override it. It demonstrably matters (see the
   bug above). Wiring it needs a NEW multi-pick decision type (the Dragonstorm `dragon` shape).
3. ~~**Trace of Abundance's shroud** is unmodelled~~ — **USER CAUGHT A REAL BUG, 2026-09-02; now
   MODELLED.** It was not inert: an Aura spell targets its host (CR 303.4a) and shroud is symmetric,
   so a Trace'd land can take no further auras. See the section above. Only the opponent-facing half
   remains disclosed as inert.
4. ~~**Blink activation COUNT in human play** is offered as `{1,2,3, go-off}`~~ — **STALE, and
   already resolved by the OOM fix; nothing to sign off.** The count menu in human play was
   collapsed to **1** (`TurnSolver.cpp`, fold (a)), because K is a *repetition* of a decision rather
   than a separate one: the main phase re-prompts after each activation, so a human reaches "blink
   three times" by choosing it three times — strictly more expressive than a fixed `{1,2,3}` menu.
   The `{1,2,3, go-off}` ladder survives only in the **autonomous** search
   (`EldraziFlickerProvider::BlinkActivationCounts`), which needs it because it commits a whole turn
   at once. This entry was written before that fix and outlived it.
5. ~~**Emiel's optional `{G/W}`** is always paid when affordable~~ — **USER CAUGHT, 2026-09-02.**
   Paying every blink iteration bought one counter for K payments (CR 400.7) and, worse, ate the
   loop's per-iteration margin. Now paid on the FINAL iteration only. The general "never let it deny
   another cast" requirement outside the loop is still OPEN (see next steps).
6. **Devoid, flying, and the `{C}`/`{G/W}` reminder texts** are inert — disclosed.

## Approved deferrals

(none yet — every proposed deferral above is PROVISIONAL until the user signs it off)

### Pending the user's sign-off: 5 viewer param classifications (2026-09-03)

`audit_viewer_decisions.py`'s self-guard requires the user's OK before a param is recorded as
creating no decision. Per CLAUDE.md these are **collected, not blocked on** — the viewer gate is
paperwork that gates nothing else, and blocking on exactly this kind of question once cost the user
a weekend of an idle box. All five are classified as details riding an already-mapped decision; the
gate is green on that basis, and the classification is reversible in one edit if the user disagrees.

| param | card | classified as | rides |
|---|---|---|---|
| `land_aura_grants_shroud` | Trace of Abundance | static targeting restriction | `is_land_aura` → main_phase |
| `wish_from_sideboard` | Living Wish | tutor search-ZONE redirect | `tutor_to_hand` → tutor |
| `exiles_self_on_resolve` | Living Wish | mandatory zone replacement, no "you may" | — |
| `etb_energy` | Aether Hub | mandatory resource gain | — |
| `energy_per_colored_tap` | Aether Hub | mode resolved inside the payment DFS (backtracked) | the cast → main_phase |

The one worth a second look is `energy_per_colored_tap`, and the reason it is *not* a surfaced
choice is that the payment DFS snapshots and restores `energy_counters` as it backtracks, so both
branches are genuinely explored rather than picked by a heuristic. **Disclosed sub-choice:** Aether
Hub is the deck's only energy sink, so the sole cost of spending is not holding the {E} for a later
turn, and the DFS optimises the cast in front of it rather than across turns — a line payable
without the energy may still burn it. Ceiling is 3 {E} for the whole game.

## User viewer batch, 2026-09-03 (hand-played seeds 1 and 2)

Five items reported. Two are closed; three are with agents at the time of writing. The user's
framing of the headline item is worth keeping verbatim, because it is a feature request, not a bug:
*"This is what I mean by a combo heuristic. We need a way to determine 'combo is active, let's
follow the line'."*

| # | Item | State |
|---|---|---|
| 1 | Seed 1: a spelled-out **T3 win** the engine does not find | **CAUSE FOUND AND VERIFIED** (below); design in `docs/design/combo-line-recognition.md` |
| 2 | "I should be able to attach to a land in the plan" | engine-side cause found (below); fix in progress |
| 3 | "Auras cannot be unattached. That option should not be offered by dialogs" | **FIXED** (`8a517c71`) |
| 4 | `logs/play/rejections/..._s2_gi1_t4.json` — "Drake should allow untap followed by displacer" | investigating |
| 5 | Seed 2: cannot activate Eldrazi Displacer to go off; board should allow a T4 win | investigating |

### Item 1's cause: the recogniser exists, and it only looks at the BOARD

The missed T3 win is **not** a depth or budget failure, and it is not the iterative-deepening start
gate — that gate governs cross-*turn* lookahead, and this combo resolves inside one turn. It is a
**width** limit, and the code already documents it. `DecisionProvider.h`'s generic
`BlinkActivationCounts` and `ManaSinkActivationCounts` both enumerate `1..min(3, max_affordable)`,
and say why that is fatal here in their own words:

> "A generic cap of 3 does not merely play the deck badly, it makes the deck's only win line
> invisible to the search: the kill needs ~20 iterations and the enumerator would never offer one.
> A combo provider recognises the loop and proposes the go-off count as a further candidate."

So the sanctioned design is *provider proposes, search picks* — and `EldraziFlickerProvider` does
implement that recogniser (`RecogniseFlickerLoop` / `FlickerGoOffCount` / `ExtraLethalDamage`).
**The gap is that the recogniser is purely retrospective:** every one of its scans walks
`s.battlefield`, so the outlet AND the payload AND the sink must already be permanents in play.

The user's line assembles the loop *this turn* — Living Wish fetches Cloud of Faeries, and a later
Living Wish fetches Essence Depleter, the actual kill. At the moment the decision is made the sink
is not on the battlefield, so `RecogniseFlickerLoop` returns nothing, the generic cap of 3 applies,
and a ~20-activation kill cannot be expressed at any depth or budget. **Verified by reading both
sites, not inferred.** The prospective (assembly) case is what
`docs/design/combo-line-recognition.md` proposes extending; it is deliberately not implemented yet,
because a prospective check needs a scratch-state re-simulation rather than the cheap board scan the
assembled case gets away with.

**A second, independent gap found on the way, and it is exactly the user's own aside.** They wrote:
*"if black is not available, we would get blue instead of black and use Dimensional Infiltrator."*
`TutorCandidates` tie-breaks candidate sinks by raw mana value, blind to colour castability — so it
would rank Dimensional Infiltrator ahead of Essence Depleter regardless of which colours the board
can actually produce. That is the opposite of the generic-by-construction discipline the rest of
that function follows, and it is the deck-independent half of this item: the payoff must be chosen
from what is REACHABLE, never hardcoded.

**Item 3's root cause is more interesting than the report suggests, and it is shared with item 2.**
`TurnSolver::CheckLine` drops the `enchant` sub whenever it cannot NAME the host — its lookup scans
battlefield *creatures*, then the hand. So an "Enchant land" aura on a land **already in play**
carries no sub, while the same aura on the land being played *this* turn does. The choose-dialog
then rendered the sub-less variant as *"leave it unattached"* — which did not mean unattached at
all, it silently meant "enchant Aether Hub". That is 16 of the deck's 60 cards (Wild Growth,
Fertile Ground, Overgrowth, Trace of Abundance).

The viewer fix recovers the host from the plan's own `enchant_target_name`, so the dialog asks the
real question. The engine half is NOT fixed: because the missing sub is also what CheckLine's dedup
signature is built from, two land hosts that are **both already in play** produce an identical empty
signature and the variants **collapse** — so the human is never asked which land an aura enchants,
and no client-side change can recover a collapsed variant. The fix location is the `enchant`
`addSub` branch, which must accept a non-creature host. Written up in `tools/play/DECISIONS.md`.

**Item 3's other half was checked, not assumed:** the board `attach_host` prompt's
*"Leave unattached"* button is gated engine-side on `is_equipment`, so it is correct and was left
alone. An Equipment genuinely can be moved; an Aura cannot.

Separately, the cycling-land click (reported on FiveColour, same batch) was **not** a missing
`stopPropagation`: the `↻ cycle` badge had no click handler at all, and the hand thumb's own
listener cycled anything with a cycling ability — so no gesture could play the card as a land. That
is worse than a mis-click, because the dig **commits** immediately rather than queueing an
undoable plan entry. Fixed in `acd01b6e`.

## Claude-play sweep
- commit: `f5f664bf` (re-sweep, frozen binary; first sweep ran across a moving binary — disclosed)
- seeds: 8801 games: 16 (x2 sweeps, 32 agent-games total)
- flags: 0 unresolved

> **STALE, AND THE RE-SWEEP IS UN-RUN (2026-09-02).** The record above predates the corrected
> decklist, the sideboard/wish mechanic, both `{C}` sinks, the opponent library, energy, and the
> adopted wish ranking — i.e. essentially all of this deck's current play. It must be re-run before
> it means anything.
>
> It has **NOT** been re-run, and the reason is a harness constraint, not a judgement call: this
> session is instructed not to use the Agent or Workflow tools unless the user asks. Stage 5d's
> documented mechanism is a Workflow fan-out of one player agent per game. Per CLAUDE.md this is
> recorded as **UN-RUN** rather than as "deferred" — nobody has signed anything off. The mechanical
> half of the verification (`play_invariants`, determinism/integrity/progress) is unaffected and is
> still enforced.

All flags from both sweeps were verified against `cards.json` + the rules skill and resolved:
**six fixed** (ETB-refund self-funding, `{cost},{T}` self-tap, tap-ahead wild mana, the Emiel-counter
documentation error, the land-aura creature-target leak, the land-aura subset reject), **two fixed as
protocol gaps** (blink target not surfaced, land host rendered as `#31`), and the remainder dismissed
with reasons — chiefly painland tap-order (legal, a known unshipped `MTG_PREPAY_SHRINK` lever),
`attached_to` m_number-vs-idx misreads, and unaffordable `{4}` Investigate plans being OFFERED but
cleanly no-oping (the enumerator deliberately carries no affordability gate on activations — the
Wirewood Lodge precedent — and the payment path is atomic).

## User viewer batch items 1/2/4/5 — CLOSED (2026-09-03)

Picked up from a WIP commit another agent left mid-fix ("wiring the opt-in from
`EnumeratePlans` only, `Solve` stays byte-identical") after it was killed by an upstream API
error. Reviewed the inherited diff critically, kept it (it was correct and precisely targeted
the Drake→Displacer report), then closed the remaining three items. Full working notes:
`docs/design/edf-task-a-b-c-d-notes.md`.

**Item 4 (Drake→Displacer, `logs/play/rejections/..._s2_gi1_t4.json`) — category (b): the ETB
untap was modelled, cast ORDER was already right, but the RESOLUTION-time credit from the untap
was never given back to the scratch board that prices the next cast** (`SubsetPayableSequential`,
TurnSolver.cpp), and separately the odometer's own upper bound (`ManaPruneBound`/
`BuildManaGateIndex`) didn't credit the refund either, so the candidate was pruned before the
sequential rescue ever saw it. Both fixed (inherited WIP). Verified directly: the exact
`--validate-line` from the rejection log flips `illegal` → `accept`.

**Item 5 (can't activate Eldrazi Displacer to go off) — the VIEWER, not the engine.** Verified
directly that `EnumeratePlans` already offers `Eldrazi Displacer: blink Peregrine Drake` with a
correctly-evaluated `{2}{C}` payment once Displacer is on the battlefield. The bug is
`src/main.cpp`'s `WriteDecisionJson`: its `"activate": true` flag list (which
`tools/play/index.html`'s `activatableSources()` reads to make a battlefield permanent
clickable) never listed `Action::Kind::ActivateBlink` or `::ActivatePermAbility` — the
14th/15th instance of a pattern already fixed 13 times at the same call site for other kinds.
Fixed by adding both kinds to that list. This also explains the T4-kill half of the report:
before Depleter/Infiltrator are fetched, Shivan Gorge's damage tap (`ActivatePermAbility`) is
the only kill on board, and it rode the same missing flag. `CanTapNow` was investigated and
ruled out — Displacer's `{2}{C}` has no `{T}` in its cost, so it was never gated on that
predicate at all. Scope-checked inert for `--batch`/autonomous play (human-play-only emitter).

**Item 2 ("I should be able to attach to a land in the plan") — engine half CLOSED.**
`TurnSolver::CheckLine`'s "Aura enchant TARGET" `addSub` block required `IsCreature()`, so a
land host already in play (Aether Hub, an in-play Yavimaya Coast, …) got no sub — and because
that sub is what the dedup signature is built from, two already-in-play land hosts collapsed to
one candidate. Fixed: resolve the host by `m_number` against ANY permanent, not just creatures.
Directly reproduced DECISIONS.md's own named repro (seed 1, T2, `cast=Wild Growth` onto Aether
Hub) on the pre-fix and post-fix binaries: before, `"subs": []`/`"cards": []`; after, the
`enchant` sub and `Aether Hub` art resolve correctly. Scope-checked inert for `--batch` play
(`CheckLine` is human-play/`--validate-line`-only; the autonomous plan dedup's own
`enchant_target` key is separately gated on `HumanPlayActive()`).

**Item 1 (seed 1 T3, "Trace of Abundance on Aether Hub via Conservatory… seems rejected") —
not a new payment bug.** This is the same shape item 2 fixes (an Aura targeting a land already
in play, Aether Hub), and the doc already ties them to one root cause. NOT independently
replayed end-to-end for the exact seed-1/turn-3 board (the saved seed-1 rejection log is a
DIFFERENT, correctly-illegal turn-2 line and doesn't record what was actually played next, so
the turn-3 state couldn't be reconstructed) — flagging this honestly rather than presenting an
invented replay as verification. The item-2 mechanism fix is the answer; whether this exact
instance also involved a mana shortfall on top of the collapse is unconfirmed.

**Measurement (Task A's fix; B and C are provably inert for batch play).** Paired before
(commit `56ba0979`, built in a throwaway worktree) / after (this fix), 4 seeds × 100 games,
d5/budget_ms=20, `ignore_play_profile=true`, one pooled `mtg --batch` per arm:

| seed | before | after | delta |
|---|---|---|---|
| 6101 | 6.68 | 6.48 | −0.20 |
| 6202 | 6.59 | 6.42 | −0.17 |
| 6303 | 6.81 | 6.55 | −0.26 |
| 6404 | 6.77 | 6.59 | −0.18 |
| **mean** | **6.7125** | **6.51** | **−0.2025** (t ≈ −9.85) |

All four seeds move the same direction, an order of magnitude past the ~0.01-turn single-job
noise floor, and comparable to the three earlier mana-modelling fixes — another data point for
"hunt modelling gaps, not levers" on this deck. Still 2+ turns short of the user's 4-5 estimate
(now ~6.5 vs the pre-this-fix ~6.7-6.9 measured here); the learned exhaustive keep table remains
the untested candidate.

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-09-03)

`verify_deck.py decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod --write-ledger` -> **PASS**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 23 cards full (missing=0, partial=0) |
| card_costs | PASS | yes | all mana costs match Scryfall (cost/cmc only) |
| card_fields | PASS | yes | 292 cards match snapshot (cost/PT/types/keywords); 8 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | no card-driven decision types in this deck |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 7002] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/454 decisions: determinism+integrity+progress hold |
| claude_sweep | PASS | yes | Claude-play sweep recorded, 0 unresolved flags |

### Pending user sign-off (block the gate until fixed OR approved below)
_none_ -- every blocking gate is green or already signed off.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Eldrazi Displacer: Blink outlet. No {T} in the cost, so it activates the turn it lands and repeats freely -- that is what makes this a combo. The {C} is a real colourless PIP (ManaPool::wild_c): 'one mana of any colour' from a Fertile Ground cannot pay it. Targets ANY creature (no 'you control'). Devoid is inert: nothing in this deck or the goldfish opponent reads a card's colour.
- coverage deferral -- Emiel the Blessed: Both abilities modelled. The blink returns the creature UNTAPPED (unlike Displacer) and is restricted to creatures you control. The counter trigger is VALUE, not a win condition, and the arithmetic is the opposite of what it looks like: CR 400.7 makes the returned permanent a NEW OBJECT, so each blink WIPES the counter the previous iteration added (and resets summoning sickness). A 13-iteration loop leaves ONE counter, not 13 -- measured in the Stage 5d claude-play sweep. Counters accumulate only across blinks of DIFFERENT creatures. The optional {G/W} is paid whenever affordable (a resolution heuristic; monotone against a passive opponent -- disclosed).
- coverage deferral -- Cloud of Faeries: All three clauses modelled. The untap is a REAL untap of the two highest-yield tapped lands, so with a cheap blink outlet it is a second combo engine (Displacer under Training Grounds costs {C} for two untaps). Flying is inert -- the passive opponent never blocks. Cycling is deliberately NOT tagged in `keywords`: this engine models it structurally via cycling_cost, and every other cycling card (Lonely Sandbar, Forgotten Cave, the Triomes) leaves the tag off too -- see audit_card_fields.py's MODELED_ELSEWHERE_KEYWORDS.
- coverage deferral -- Peregrine Drake: The deck's engine. A REAL untap of the five highest-yield tapped lands (yield counts any land Aura on them), so each blink of the Drake refunds five lands' worth of mana against the outlet's cost -- net positive, hence unbounded. Casting it is also mana-neutral via the tap-ahead. Flying is inert vs the passive opponent.
- coverage deferral -- Wild Growth: Land Aura: the bonus rides the enchanted land's tap, in the aura's own colour, at both the projection and the real tap. WHICH land to enchant is a searched plan variant per legal host.
- coverage deferral -- Overgrowth: Land Aura -- see Wild Growth. Two GREEN mana, not two wild: an Overgrowth'd land cannot pay Eldrazi Displacer's {C} off the bonus.
- coverage deferral -- Fertile Ground: Land Aura -- see Wild Growth. 'Any color' is credited as WILD and deliberately not as wild_c: a colour cannot pay a {C} pip. This and Trace of Abundance are the deck's only red sources, so they are what turn Shivan Gorge's {2}{R} on.
- coverage deferral -- Brushland: Painland, both modes modelled -- see Adarkar Wastes. The {C} mode was previously bracket-noted as unmodelled on the grounds that our life loss is inert for the goldfish clock; that reasoning does not survive a deck with a {C} PIP in an activation cost, so it is modelled now. Also used by decks/Auras.
- coverage deferral -- Yavimaya Coast: Painland, both modes modelled -- see Adarkar Wastes.
- coverage deferral -- Adarkar Wastes: Painland, BOTH modes modelled: produces W/U/C, and tap_self_damage applies only to the COLOURED modes -- a {C} tap is painless (CR: they are separate abilities). The {C} mode is live here rather than cosmetic: Eldrazi Displacer activates for {2}{C} and no coloured mana can pay a {C} pip.
- coverage deferral -- Mariposa Military Base: FULLY MODELLED as of 2026-09-02. The mana ability, the draw ability and its per-rad-counter discount, the optional enters-tapped RAD MODE, and the rad rule itself (inherent to the counter type, not printed: at the beginning of your precombat main, mill that many; for each NONLAND milled, lose 1 life and remove a counter -- ApplyRadMill, fired in both worlds). The mode is a SEARCHED decision (Plan::rad_mode, one variant per mode) after the USER rejected the previous hardcoded always-decline: 'we probably shouldn't always decline the rad counters, since it draws more cheaply with them out'. Measured -0.0163 avg win turns (t -2.60, 5 seeds better : 0 worse). Modelling the mill is what makes the choice real -- without its cost the mode is strictly-upside and the search would take it every time. Note the timing that makes it close: the land enters tapped so the discounted draw is not activatable that turn, and the mill fires at the head of the NEXT precombat main, before it ever is.
- coverage deferral -- Trace of Abundance: Mana clause modelled as a Land Aura -- see Fertile Ground. SHROUD IS MODELLED (land_aura_grants_shroud): an Aura spell TARGETS its host as it is cast (CR 303.4a) and shroud stops that (CR 702.18a, symmetric -- it applies to our own spells too), so a land already carrying a Trace can take NO further auras. That is a legality filter on this deck's own plays, and it bites: the deck runs ~23 land auras and concentrating them on one host is a real line, so the host ranking steered straight into the illegal play. Cast order puts a shroud-granting land aura LAST among land auras, which keeps the legal Overgrowth-then-Trace stack available. Still inert vs the OPPONENT (passive, casts nothing) -- that half remains disclosed.
- coverage deferral -- Conservatory: All three clauses modelled. Investigate creates the 'Clue Token' named token def, whose own {2}-sacrifice-to-draw is a real searched ActivatePermAbility source.
- coverage deferral -- Kitchen: All three clauses modelled -- see Conservatory.
- coverage deferral -- Shivan Gorge: Both modes modelled. The damage ability is this deck's same-turn kill: a blink loop untaps the Gorge every iteration, so N iterations are N activations. 'Each opponent' is one player in a goldfish. Legendary, so the legend rule caps the board at one.
- coverage deferral -- Training Grounds: Reduces ACTIVATION costs (a new axis: every other reducer in this engine is keyed on the card being cast). Emiel's {3} becomes {1}; Eldrazi Displacer's {2}{C} becomes {C} -- the generic half goes and the colourless pip stays, so a {C} source is still required. The one-mana floor is enforced.
- coverage deferral -- Living Wish: THE deck's engine -- 4 copies, and BOTH win conditions live in the sideboard it fetches (Essence Depleter, Dimensional Infiltrator). A WISH is mechanically a TUTOR whose search ZONE is the sideboard rather than the library (CR 400.11b: in constructed, a sideboard card is 'outside the game'), so it reuses the entire searched-tutor apparatus -- tutor_to_hand + tutor_types read Player::sideboard when wish_from_sideboard is set, and the Plan::tutor_choice INDEX AXIS, the five plan-signature folds, the breakpoint pin and the viewer chooser all apply unchanged. Building a parallel wish axis instead would have re-created the dedup bug that once left the tutor target unsearched. The type filter goes through the card DEFINITION: a zone Card is a bare placeholder with an EMPTY type mask, so a raw IsCreature()/IsLand() on it is always false. Each sideboard card is a SINGLETON consumed on fetch, which is why the pool is per-game state and not a static read of the decklist -- four Living Wishes must see it shrink. NO shuffle: a wish does not search a library, so CR 701.19c never triggers, and PerformTutor's ShuffleAfterSearch is explicitly skipped (that call is unconditional, so leaving tutor_shuffle_after false would NOT have been enough, and letting it run would advance search_count and silently re-order every later fetch). The provider widens TutorSearchWidth to 8 so the whole pool is reachable -- at the default 6, candidates arriving in decklist order would leave both win conditions unsearchable in every game. [PARTIAL: 'You may' -- declining is offered to a HUMAN (the tutor chooser returns -1) but is not emitted as an autonomous variant. A to-hand fetch costs nothing beyond the spell already cast, and since a later wish would want a different singleton anyway, taking the best card now forecloses nothing a decline would preserve. Disclosed, not silently dropped.
- coverage deferral -- Living Wish: PARTIAL: 'reveal' -- the information half is inert; the limitation is that the passive opponent makes no decisions and nothing reads 'was revealed'. The VIEWER half is implemented, riding PerformTutor's existing '(searched)' reveal.
- coverage deferral -- Eladamri's Call: Finds whichever combo piece is missing. WHICH creature is a searched tutor_target axis (one plan variant per candidate).
- coverage deferral -- Aether Hub: All three clauses modelled. ENERGY is a PLAYER resource (Player::energy_counters), like rad counters and unlike a permanent's counters -- three Hubs pool three {E} and any ONE of them may spend all three. That is not pedantry here: Peregrine Drake and Cloud of Faeries untap lands every blink iteration, so correct play is to tap one Hub three times through the whole pool, which a per-permanent counter cannot express. MODELLING THE SPEND IS WHAT KEEPS IT HONEST. Under that same untap loop a Hub is re-tapped an unbounded number of times per turn, so an unmetered any-colour mode would make it an INFINITE rainbow source -- switching on the {2}{R} Shivan Gorge kill for free and making the deck look far faster than it is. Modelled as produces [C,W,U,B,R,G
- coverage deferral -- Aether Hub: PARTIAL: the pool PROJECTIONS (AvailableManaPool and friends) credit each Hub its full colour set independently, so three Hubs sharing one energy project 3 wild where reality is 1 wild + 2 colourless. Over-credit only, which is the engine's standard optimistic-bound direction: the PAYMENT is exact, so an over-projected line is enumerated and then dropped as unpayable rather than played illegally. Threading an energy budget through the seven pool builders is the gy_fuel pattern and is deferred; the ceiling here is 3 energy for the whole game, so the over-credit is at most 2.
- coverage deferral -- Azorius Chancery: Karoo bounce land: enters tapped, makes 2 mana ({W}{U}, modelled as wild like other duals), and on ETB returns one of your lands to hand (BounceKarooLand prefers a tapped land so no mana is lost this turn; the returned land must be replayed, the real tempo cost).
- coverage deferral -- Vexing Shusher: Vanilla 2/2 Goblin Shaman for goldfishing -- a SIDEBOARD card, reachable off Living Wish (a creature, so a legal wish target). The two {R/G} HYBRID pips are real either-colour pips (ManaCost::hybrid_pair): the deck runs NO red source and casts it off {G}{G}, and CR 202.2b makes the card RED AND GREEN (derived from the pips, so no explicit colors array). ManaCost::ToString renders a hybrid as its FIRST colour, so logs show {R}{R} -- deliberate, for digest stability; payment is unaffected. NOT a dead card: with no blocker path it is an unopposed 2/2 clock, just one badly dominated by the sideboard's two {C} mana sinks. [PARTIAL: the static "This spell can't be countered" is inert. It is a stack-functioning ability (CR 113.6a) that only removes an option from a would-be COUNTERER; unlike shroud (CR 702.18a) it is NOT symmetric, so it places no restriction on our OWN plays, and having no cost it consumes nothing. The passive opponent casts nothing, and the engine's only counter path (EffectHandler::ResolveCounterSpell) is an explicit no-op. Disclosed deferral D1.
- coverage deferral -- Vexing Shusher: PARTIAL: the activated "{R/G}: Target spell can't be countered" is inert AND NEVER LEGALLY ACTIVATABLE -- implementing it would ADD an illegal play, not restore a real one. There is no spell/stack target type (GameState::Target is Player|Permanent) and no priority window with a non-empty stack (ResolveStack drains after every cast), so a legal target can never exist and CR 601/602 forbid activating it. Carrying NO param is also what keeps this {T}-less repeatable mana sink out of the go-off: SpendSurplusOnDrain, SpendSurplusOnDamageSinks and TurnSolver's ActivatePermAbility ModeSpec table are each keyed on specific params, so ApplyBlinkLoop's per-iteration spend cannot latch onto it and eat the loop's margin (the Emiel {G/W} lesson). Disclosed deferral D2.
- coverage deferral -- Essence Depleter: WIN CONDITION 1 of 2, reachable only off Living Wish. STRUCTURALLY UNLIKE Shivan Gorge: there is NO {T} and no sacrifice in the cost, so it is not once-per-untap -- it is a pure mana sink whose activation COUNT is bounded only by available mana, and it can be activated the turn it lands and on a turn it attacks (CR 302.6 restricts {T} abilities and attacking, neither of which applies). That is what converts the flicker loop's unbounded mana into a kill, ~20 activations, with no untap required. Under Training Grounds {1}{C} -> {C}: EffectiveActivationCost removes the GENERIC half and the colourless PIP survives, so a real {C} source is still required -- no coloured mana pays a {C} pip (CR 107.4c), and neither does Fertile Ground's or Trace of Abundance's 'one mana of any colour' (credited as wild, never wild_c). The life LOSS is not damage: no prevention, no lifelink, no damage triggers (PermAbilityMode::Drain, shared by the executor and the rollout). 'Target opponent' is the single passive opponent, so there is no target choice. [PARTIAL: 'you gain 1 life' is applied faithfully but is INERT FOR THE CLOCK; the limitation is that the passive opponent never attacks or burns us, so nothing reads our life total -- if a damaging opponent is ever modelled this note is void.
- coverage deferral -- Essence Depleter: PARTIAL: Devoid is an inert keyword TAG; the limitation is that no card in this decklist reads a card's COLOUR. "colors" is set to [
- coverage deferral -- Dimensional Infiltrator: WIN CONDITION 2 of 2, reachable only off Living Wish, and it is a DECK-OUT rather than a damage kill: the opponent has a real 60-card library and a simulated draw at the end of each of our turns (core/OpponentDeck.h), so emptying that zone loses them the game on OUR turn (CR 104.3c; user win-turn rule 2026-09-02). Depth is 53 after their opening seven, one less for every turn they have drawn. Like Essence Depleter there is NO {T} and no sacrifice, so the activation COUNT is bounded only by mana and it works the turn it lands. Under Training Grounds {1}{C} -> {C} (generic half only; the {C} pip survives, CR 107.4c). The exiled card's LAND-ness is read from its CardDefinition, never from the zone card: a card outside the battlefield carries EMPTY type masks, so a raw IsLand() would be false for every library card -- the same trap ApplyRadMill documents. [PARTIAL: 'you may return this creature to its owner's hand' is modelled as ALWAYS DECLINED. It is a may, so declining is always legal, and it is dominant in every line this engine can realise: the creature has no ETB to re-trigger and there is no removal to dodge, while bouncing costs {1}{U} plus a fresh summoning-sick turn AND -- decisively -- removes the ability from the battlefield mid-loop on the ~40% of activations that hit a land (24 lands in the 60-card opponent deck), which is the difference between the deck-out existing and not. The one corner where bouncing buys something is re-casting it to re-fire Emiel's enters-trigger, and that is itself dominated by simply BLINKING it for {C}/{1} without leaving the battlefield. Auto-resolved; a bucket-B choice deliberately not surfaced.
- coverage deferral -- Dimensional Infiltrator: PARTIAL: Flash is inert; the limitation is that this engine models no priority window outside a main phase with an empty stack, so instant-speed casting is unreachable for every card.
- coverage deferral -- Dimensional Infiltrator: PARTIAL: Flying is inert; the limitation is that NO BLOCKING IS MODELLED AT ALL -- there is no blocker path anywhere in src/ -- and the engine's one Flying reader is Dragon Tempest's haste_on_flying_enter watcher, which this deck does not run.
- coverage deferral -- Dimensional Infiltrator: PARTIAL: Devoid is an inert keyword TAG for the same reason as Essence Depleter; "colors" is set to [
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled 
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is 
- allowlisted divergence -- Glorybringer [keywords]: 'Exert' is a real keyword but its use is OPTIONAL and provably worthless here: exerting costs the next untap step (so Glorybringer cannot attack the f
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.56); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.75); scryfall="As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.75); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.44); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.\n{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.71); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.\n{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.\n{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.\nYou may choose not to untap this land during your untap step.\nAt the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.\n{T}: Put a storage counter on this land.\n{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)\nThis land enters tapped.\nWhen this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.\n{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.\nSacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Worthy Knight: oracle_text diverges (similarity 0.45); scryfall='Whenever you cast a Knight spell, create a 1/1 white Human creature token.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.77); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may 
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.41); scryfall='First strike (This creature deals combat damage before creatures without first strike.)\nOther Knight creature
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.\n{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins\nOther Knights you control get +2/+2 and have protection from Assassins.\nWhen Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance\nAdeline's power is equal to the number of creatures you control.\nWhenever you attack, for each opp
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)\n{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.66); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying\nRot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.\nWhenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.44); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.61); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.\nTarget
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.57); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.\n
- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.43); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.55); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.\nWhenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.11); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.32); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.\nDraw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.20); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.58); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.\nFerocious — If you control a creature with power 4 or greater, those creatures don't 
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample\nSpells you cast cost {1} less to cast for each target.\nSpells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {U}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.30); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of 
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —\n• Tap X target permanents.\n• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying\n{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.10); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.05); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.\nSplice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste\nOther creatures you control have haste.\nWhenever an opponent casts a creature or planeswalker 
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste\nWhen Karrthus enters, gain control of all Dragons, then untap all Dragons.\nOther Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.25); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying\nWhenever this creature or another Dragon you control enters, it deals X damage to any target, where X 
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying\nWhenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.\nStorm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.\nIf this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.\nWhenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.60); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample.\nWhen this Aura is put into a graveyard from 
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.58); scryfall='Enchant creature with another Aura attached to it\nEnchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample.\nWhenever enchanted creature deals damage, yo
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)\nUmbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature\nEnchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature\nEnchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature\nEnchanted creature gets +1/+0 and has flying.\n{3}{W}: Return this card from your graveyard 
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)\nWhenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.77); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)\nEnchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)\nOther Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.\nGoblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.\n{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.\n{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.\nWhenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.08); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.40); scryfall='First strike\nWhen this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach\nWhen this creature enters, it deals 2 damage to any target.\nChannel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Three Tree City: oracle_text diverges (similarity 0.47); scryfall='As Three Tree City enters, choose a creature type.\n{T}: Add {C}.\n{2}, {T}: Choose a color. Add an amount of 
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.\nWhen this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.\nWhenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.\nWhenever a creature 
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.25); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.34); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.45); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.\n{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment, 
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.\nSearch your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.28); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.29); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.47); scryfall='Flying, first strike, trample, lifelink, haste\nAt the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste\nWhenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything\nIf Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance\nThis creature gets +1/+1 for each color among permanents you control.\n{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)\n{B}, 
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.24); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)\nEquip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.\n−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.\n−2: Gain control of target creature.\n−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")\n+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.39); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.23); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying\n{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.46); scryfall="Choose five. You may choose the same mode more than once.\n• Target permanent phases out.\n• Target player dra
- oracle_text advisory -- Disenchant: oracle_text diverges (similarity 0.21); scryfall='Destroy target artifact or enchantment.'
- oracle_text advisory -- Mirrorwing Dragon: oracle_text diverges (similarity 0.42); scryfall='Flying\nWhenever a player casts an instant or sorcery spell that targets only this creature, that player copie
- oracle_text advisory -- Zada, Hedron Grinder: oracle_text diverges (similarity 0.57); scryfall='Whenever you cast an instant or sorcery spell that targets only Zada, copy that spell for each other creature 
- oracle_text advisory -- Goblin Instigator: oracle_text diverges (similarity 0.32); scryfall='When this creature enters, create a 1/1 red Goblin creature token.'
- oracle_text advisory -- Fists of Flame: oracle_text diverges (similarity 0.36); scryfall="Draw a card. Until end of turn, target creature gains trample and gets +1/+0 for each card you've drawn this t
- oracle_text advisory -- Luxurious Libation: oracle_text diverges (similarity 0.24); scryfall='Target creature gets +X/+X until end of turn. Create a 1/1 green and white Citizen creature token.'
- oracle_text advisory -- Fortifying Draught: oracle_text diverges (similarity 0.33); scryfall='You gain 2 life. Target creature gets +X/+X until end of turn, where X is the amount of life you gained this t
- oracle_text advisory -- Gold Rush: oracle_text diverges (similarity 0.36); scryfall='Create a Treasure token. Until end of turn, up to one target creature gets +2/+2 for each Treasure you control
- oracle_text advisory -- Ancestral Anger: oracle_text diverges (similarity 0.52); scryfall='Target creature gains trample and gets +X/+0 until end of turn, where X is 1 plus the number of cards named An
- oracle_text advisory -- Oracle's Restoration: oracle_text diverges (similarity 0.10); scryfall='Target creature you control gets +1/+1 until end of turn. You draw a card and gain 1 life.'
- oracle_text advisory -- Expedite: oracle_text diverges (similarity 0.29); scryfall='Target creature gains haste until end of turn.\nDraw a card.'
- oracle_text advisory -- Impolite Entrance: oracle_text diverges (similarity 0.18); scryfall='Target creature gains trample and haste until end of turn.\nDraw a card.'
- oracle_text advisory -- Scale the Heights: oracle_text diverges (similarity 0.49); scryfall='Put a +1/+1 counter on up to one target creature. You gain 2 life. You may play an additional land this turn.\
- oracle_text advisory -- Twinflame: oracle_text diverges (similarity 0.42); scryfall="Strive — This spell costs {2}{R} more to cast for each target beyond the first.\nChoose any number of target c
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.43); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.\nWhen this land enters, you gain 1 life.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.\nEquip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.\nEquip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.\n{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.\nWhenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.\nEquipped creature gets +3/+0 and h
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.\nRemove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.49); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.\nMetalcraft — Equipment you control have equip 
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.37); scryfall='First strike\nBalan has double strike as long as two or more Equipment are attached to it.\n{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying\nWhenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.\n{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.12); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.39); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
- oracle_text advisory -- Mirri's Guile: oracle_text diverges (similarity 0.44); scryfall='At the beginning of your upkeep, you may look at the top three cards of your library, then put them back in an
- oracle_text advisory -- Call of the Wild: oracle_text diverges (similarity 0.52); scryfall="{2}{G}{G}: Reveal the top card of your library. If it's a creature card, put it onto the battlefield. Otherwis
- oracle_text advisory -- Hornet Queen: oracle_text diverges (similarity 0.41); scryfall='Flying, deathtouch\nWhen this creature enters, create four 1/1 green Insect creature tokens with flying and de
- oracle_text advisory -- Terastodon: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may destroy up to three target noncreature permanents. For each permanent put i
- oracle_text advisory -- Elderscale Wurm: oracle_text diverges (similarity 0.53); scryfall='Trample\nWhen this creature enters, if your life total is less than 7, your life total becomes 7.\nAs long as 
- oracle_text advisory -- Craterhoof Behemoth: oracle_text diverges (similarity 0.44); scryfall='Haste\nWhen this creature enters, creatures you control gain trample and get +X/+X until end of turn, where X 
- oracle_text advisory -- Worldspine Wurm: oracle_text diverges (similarity 0.39); scryfall="Trample\nWhen this creature dies, create three 5/5 green Wurm creature tokens with trample.\nWhen Worldspine W
- oracle_text advisory -- Vaultborn Tyrant: oracle_text diverges (similarity 0.47); scryfall="Trample\nWhenever this creature or another creature you control with power 4 or greater enters, you gain 3 lif
- oracle_text advisory -- Natural Order: oracle_text diverges (similarity 0.38); scryfall='As an additional cost to cast this spell, sacrifice a green creature.\nSearch your library for a green creatur
- oracle_text advisory -- Turntimber Symbiosis: oracle_text diverges (similarity 0.45); scryfall='Look at the top seven cards of your library. You may put a creature card from among them onto the battlefield.
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.24); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.\n{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.17); scryfall='First strike\n{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.\n{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.41); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.\nWhenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.35); scryfall='First strike\nOther Minotaurs you control have first strike.\nAs long as you have one or fewer cards in hand, 
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample\nOther Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.\n{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.28); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.\n{2}, Sacrifice another creatu
- oracle_text advisory -- Atsushi, the Blazing Sky: oracle_text diverges (similarity 0.43); scryfall='Flying, trample\nWhen Atsushi dies, choose one —\n• Exile the top two cards of your library. Until the end of 
- oracle_text advisory -- Inferno of the Star Mounts: oracle_text diverges (similarity 0.35); scryfall="This spell can't be countered.\nFlying, haste\n{R}: Inferno of the Star Mounts gets +1/+0 until end of turn. W
- oracle_text advisory -- Dragon Tempest: oracle_text diverges (similarity 0.34); scryfall='Whenever a creature you control with flying enters, it gains haste until end of turn.\nWhenever a Dragon you c
- oracle_text advisory -- Urza's Incubator: oracle_text diverges (similarity 0.15); scryfall='As this artifact enters, choose a creature type.\nCreature spells of the chosen type cost {2} less to cast.'
- oracle_text advisory -- Mind Stone: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.\n{1}, {T}, Sacrifice this artifact: Draw a card.'
- oracle_text advisory -- Fire Diamond: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.\n{T}: Add {R}.'
- oracle_text advisory -- Dragonspeaker Shaman: oracle_text diverges (similarity 0.15); scryfall='Dragon spells you cast cost {2} less to cast.'
- oracle_text advisory -- Glorybringer: oracle_text diverges (similarity 0.46); scryfall="Flying, haste\nYou may exert this creature as it attacks. When you do, it deals 4 damage to target non-Dragon 
- oracle_text advisory -- Haven of the Spirit Dragon: oracle_text diverges (similarity 0.32); scryfall='{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana only to cast a Dragon creature spell.\n{2}, {T}
- oracle_text advisory -- Nest Invader: oracle_text diverges (similarity 0.27); scryfall='When this creature enters, create a 0/1 colorless Eldrazi Spawn creature token. It has "Sacrifice this token: 
- oracle_text advisory -- Young Pyromancer: oracle_text diverges (similarity 0.28); scryfall='Whenever you cast an instant or sorcery spell, create a 1/1 red Elemental creature token.'
- oracle_text advisory -- Undercellar Myconid: oracle_text diverges (similarity 0.39); scryfall='Whenever this creature enters or dies, create a 1/1 green Saproling creature token.\n{T}: Add one mana of any 
- oracle_text advisory -- Frontline Heroism: oracle_text diverges (similarity 0.39); scryfall='When this enchantment enters, create a 1/1 red Soldier creature token with haste.\nWhenever you cast a spell t
- oracle_text advisory -- Adarkar Wastes: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.\n{T}: Add {W} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.\n{T}: Add {G} or {W}.\n{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.33); scryfall='{T}: Add {C}.\n{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
- oracle_text advisory -- Mariposa Military Base: oracle_text diverges (similarity 0.26); scryfall='You may have this land enter tapped. If you do, you get two rad counters.\n{T}: Add {C}.\n{5}, {T}: Draw a car
- oracle_text advisory -- Eldrazi Displacer: oracle_text diverges (similarity 0.47); scryfall="Devoid (This card has no color.)\n{2}{C}: Exile another target creature, then return it to the battlefield tap
- oracle_text advisory -- Emiel the Blessed: oracle_text diverges (similarity 0.48); scryfall="{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.\n
- oracle_text advisory -- Cloud of Faeries: oracle_text diverges (similarity 0.25); scryfall='Flying\nWhen this creature enters, untap up to two lands.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Peregrine Drake: oracle_text diverges (similarity 0.22); scryfall='Flying\nWhen this creature enters, untap up to five lands.'
- oracle_text advisory -- Wild Growth: oracle_text diverges (similarity 0.50); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land\nEnchanted land has shroud. (It can't be the target of spells or abilities.)\nWhenever enchanted 
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.22); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.06); scryfall="This spell can't be countered.\n{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)\n{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)\nFlash\nFlying\n{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.09); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).\n{T}: Add {C}.\n{T}, Pay {E}: Add one mana of any colo
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- claude_sweep recorded at commit f5f664bf (HEAD d2d70091d516); re-run if play changed since (play_invariants + smoke digests track play live).

<!-- verify_deck:end -->

### Independent A/B confirmation of the 2026-09-03 batch (coordinator, held-out seeds)

The deck is **not in `test/regression_cases.sh`**, so the regression suite cannot see it and "80/80
green" is not evidence about EDF. (Adding it is wanted, but deliberately deferred — the user's
reason: the deck was so far from working that a baseline would have frozen a broken deck into GT.
Revisit once these fixes settle.) The honest instrument is therefore an A/B, run on seeds chosen
independently of the implementing agent's:

| seed | before | after | delta |
|---|---|---|---|
| 5101 | 6.7500 | 6.5900 | −0.1600 |
| 5201 | 6.8900 | 6.5700 | −0.3200 |
| 5301 | 6.7600 | 6.5500 | −0.2100 |
| 5401 | 6.6900 | 6.5400 | −0.1500 |
| **all** | **6.7725** | **6.5625** | **−0.2100**  (n=400, se=0.0238, t=−8.83) |

Arms differ ONLY in the binary (before = `e093d3e4`, after = the integrated tree); one pooled batch
per arm, deck profile attached, 4 seeds x 100 games. All four seeds move the same direction and the
effect is ~21x the ~0.01 noise floor. The implementing agent measured −0.2025 (t≈−9.85) on its own
seeds, so this is two independent measurements agreeing.

**What is NOT in that number.** Only Task A (the ETB-untap credit reaching a later cast in the same
plan) can move batch play. The Displacer-activation fix is in `WriteDecisionJson` and the
aura-on-a-land fix is in `CheckLine` — both human-play-only paths, so they are inert here by
construction, not by luck.

**Regression on the integrated tree: 80/80, `configs changed: 0`, `play-changed=0`.** The Task A fix
touches `SubsetPayableSequential` / `ManaPruneBound`, which `EnumeratePlans` uses for EVERY deck, so
this was a real check rather than a formality — no suite deck casts an ETB-untap permanent ahead of
another cast in the same plan, so none of them can reach the changed branch.

## The T3 line: `legal_not_enumerated` (2026-09-03) — VERIFIED, NOT FIXED

The user hand-played seed 1 and spelled out a T3 win the engine takes until T5 to find. Chasing
their step (a) produced a precise, reproducible finding, and a correction to an earlier claim of
mine that was wrong three ways.

**The finding.** At seed 1 / gi 0 / T3 (reproduce with
`--claude-play --seed 1 --game-index 0 --choices "1,0,0"`), the engine's own line validator says:

```
land=Mariposa Military Base;cast=Trace of Abundance;cast=Living Wish
  verdict = legal_not_enumerated
  reason  = rules-legal in your cast order (a same-turn cost reducer makes it payable),
            but the search never enumerated this line
```

At that decision the enumerator produces **40 plans and not one of them casts two spells.**

**Why it is payable and the enumerator cannot see it.** Sources are Conservatory (1), Aether Hub
(1 -- its only energy was spent on T1), the Wild Growth on the Hub (1 G) and Mariposa (1): 4 mana.
Trace of Abundance `{R/W}{G}` consumes the W and the ONLY green, so Living Wish's `{G}` looks
unpayable. It is payable because Trace lands ON Aether Hub and grants it +1 of any colour, which
restores the green. That is a same-turn mana interaction of exactly the ETB-untap's shape -- it
arrives AFTER its own cast resolves -- and the flat gate prices the pool as it stood before the
Aura existed.

**RESOLVED 2026-09-04 -- and the cause was none of the above.** The line was never a mana problem
at all. The board reading in the paragraph above is wrong: at that decision Aether Hub already
carries **two** Wild Growths, so it taps for `{C}{G}{G}` and the pool is 4 before the land drop and
5 after -- more than enough for Trace `{R/W}{G}` plus Living Wish `{1}{G}`. The subset was
affordable all along.

**The plan was BUILT and then DELETED.** Instrumenting the two layers separately settles it:
`EnumeratePlans` returned 16 `Trace of Abundance + Living Wish` plans at that decision, and
`add_for_land` received 11 with **none** of them. The deletion is in the cast-ORDERING search:

* `IsAuraOnNewCreature` scans the battlefield for a **creature** carrying the Aura's
  `enchant_target` m_number. An "enchant LAND" Aura's host is a **land**, so the scan never matches
  and the predicate returns TRUE for every land Aura.
* `OrderingPlacesAuraBeforeCreature` then rejects **every** ordering of the set, and the loop
  `continue`s past all of them -- so the whole plan is dropped with **no fallback**.

This is the **third symptom of the same `is_land_aura` root cause** as commit `34abe486` ("one root
cause, two symptoms", which guarded `AppendCreatureTargetAuraCandidates` and
`SubsetHasAuraOnUncastCreature`). It is the worst of the three: the other two mis-offer or
over-reject a subset, this one deletes a plan outright. Fix is the same one-line guard, applied at
the root predicate so all three call sites inherit it.

**Why no suite ever caught it, and why it looked like an enumeration gap.** The deletion happens
only when the cast-ordering search runs, which is gated on `UnprunedGate::SearchOrder` -- **default
OFF**. `--claude-play` sets `MTG_UNPRUNED=1` (`src/main.cpp`), which opens that gate. So the bug was
reachable **only through the play viewer / the claude-play oracle**: the human was refused a line
the autonomous search kept. That is also why every autonomous win turn is unmoved by the fix, why
smoke reports `configs changed: 0`, and why the `legal_not_enumerated` verdict was so misleading --
`CheckLine` runs under the same widened gates as the viewer, so it reported the *viewer's*
enumeration, not the engine's. Regression test: `test/scenarios/edf_land_aura_multicast_offered.json`
(verified discriminating: `legal_not_enumerated` before, `accept` after; it needs the `env` block,
because without `MTG_UNPRUNED` the fixture passes on the unfixed binary).

Verdict on the real seed-1 T3 board after the fix: **`choose`**, offering `Trace of Abundance ->
Aether Hub` with `Living Wish -> Cloud of Faeries` -- the user's line exactly.

**A same-turn land-Aura ramp credit was built for the wrong diagnosis, and REVERTED.** Before the
above was found, four coordinated gaps were closed on the theory that Trace's +1 was needed to
afford the Wish: a CastOrderRank tier-5 slot beside the mana rocks, a `LandAuraBoundCredit` twin of
`EtbUntapBoundCredit` for the total-mana bound, an independent rescue-walk admission, and Aura
attachment inside `SubsetPayableSequential` (which resolves rituals, ETB-untaps and rocks but never
attached an Aura). Every one of those is a real modelling gap and the work is recoverable from this
note -- but none of them was this bug: with the lever on, the T3 line still verdicted
`legal_not_enumerated` and both target games stayed at T5. Reverted rather than shipped as an
unmeasured lever carrying a rationale now known to be false. If it is ever revived it needs its own
motivating case and its own A/B.

**Still open: the two target games are unchanged at T5.** The user's ask was to *reach* T3 (seed 1)
and T4 (seed 2). The line is now offerable and playable; the autonomous search still does not pick
it. Those are separate problems and only the first is fixed here.

**Corrections to the record.** An earlier note in this session claimed the user's step (a) was
"correctly rejected". That was wrong on every count and is withdrawn:
* the saved artifact `logs/play/rejections/..._s1_gi0_t2.json` is turn **2** and its line is
  `cast=Wild Growth`, not Trace of Abundance -- a different card on a different turn;
* that line is **legal** on the current binary AND on one built before any of this session's work,
  so the recorded `illegal` verdict does not reproduce anywhere;
* the user was right that it was a valid line: Aether Hub plus the Wild Growth already attached to
  it taps for `{C}{G}`, so green WAS available.

**Tooling gap this exposed.** A rejection artifact records `version: null` and no engine
fingerprint, so a saved verdict cannot be tied to the binary that produced it -- attribution here
degenerated into an inconclusive bisect across binaries. The build already computes
`MTG_ENGINE_FP`; stamping it and the `HEAD` sha into `/api/reject-artifact` would make this a
two-second question. Worth doing before the next viewer session.

**Viewer nit, separately:** the decision dump renders Trace of Abundance's cost as `{R}{G}`, losing
the hybrid. The engine pays it correctly (its refusal message says "red or white"), but a human
reading the dump is told the card needs red.

### Prospective go-off recognition -- measured, BELOW the adoption bar (2026-09-04)

`RecogniseFlickerLoopProspective` (commit `9577e6d4`, `MTG_EDF_PROSPECTIVE`) lets `ExtraLethalDamage`
see a loop the plan being scored ASSEMBLES, instead of only one already on the board -- the user's
"it's fine to use a heuristic once it has been determined that we can go off", read as: the
determination may come from the plan, not just the battlefield.

Two paired A/Bs at play settings (d5/20ms), negative = better, pairing on (seed, gi):

| run | pairs | delta | se | t | seeds better |
|-----|-------|-------|----|----|--------------|
| 4 seeds x 100 | 400 | -0.0225 | 0.0134 | -1.68 | 4/4 |
| 8 seeds x 100 (4 fresh) | 800 | **-0.0137** | 0.0084 | **-1.64** | 6/8 (1 worse, 1 tied) |

Consistently favourable in direction, never significant, and **well under the bar this repo has been
using** -- `MTG_EDF_SEQ_ETB` was adopted at t = -3.97 on the same deck and the same 800-pair design.
Doubling the sample moved t not at all (-1.68 -> -1.64), which is what a small real effect and a
weak one look like alike at this power. It does buy one concrete thing: seed 2 gi 1 goes T6 -> T5.

**SETTLED 2026-09-04 -- REFUTED on held-out seeds.** Given a `heurarm` slot (so both arms ride ONE
pooled batch of 3200 games instead of two sequential per-arm batches), 16 seeds x 2 arms x 100:

| block | delta | se | t | seeds | games |
|-------|-------|----|----|-------|-------|
| **HELD-OUT (8 fresh)** | **-0.0088** | 0.0099 | **-0.88** | 3 better / 3 worse | 31 faster : 23 slower : 746 same |
| prior 8 (the seeds that suggested it) | -0.0237 | 0.0091 | -2.62 | 6 / 2 | 36 : 17 : 747 |
| pooled 16 | -0.0163 | 0.0067 | -2.42 | 9 / 5 | 67 : 40 : 1493 |

The held-out block is the confirmation and it is **nothing** -- t = -0.88, 3 seeds better and 3
worse. The pooled t = -2.42 is carried entirely by the seeds that generated the hypothesis, which is
textbook regression to the mean; quoting the pooled number as the result would be the
[[valueleaf-phaseE-8-seeds-underpowered]] mistake with the sign reversed. It also did NOT deliver
what it was built for -- seed 1 stays T5 and seed 2 stays T5.

VERDICT: not established. Flip `MTG_EDF_PROSPECTIVE` to DEFAULT OFF and keep it as a measured lever
(the `heurarm` slot stays -- it is what made the honest measurement affordable).

### Why the SEARCH still does not take the T3 line (2026-09-04)

With the deletion bug fixed, the autonomous search **does** enumerate `Trace of Abundance + Living
Wish` at the turn-3 root -- 24 scanned tails. It rejects it on score. `MTG_FS_ROOT_DUMP=3` on
seed 1 gi 0, best tail per distinct plan at ship settings (d5/20ms):

| best tail | value | plan |
|-----------|-------|------|
| **win=5** | 1200 | Emiel the Blessed |
| win=6 | 1300 | Living Wish + Peregrine Drake + Peregrine Drake |
| win=6 | 700 | Living Wish + Peregrine Drake |
| **win=9** | 200 | **Trace of Abundance + Living Wish** |
| **win=9** | 100 | **Trace of Abundance** |

Every plan that casts Trace scores **9 = max_turns+1 = unwon**, the maximal penalty, so it can never
compete with Emiel's win=5. That verdict is not credible on its face: hand-playing the T3 plan
reaches turn 4 with `Cloud of Faeries + Emiel the Blessed + Peregrine Drake + Peregrine Drake` all
castable in one menu -- the entire combo -- and "no win in the five remaining turns" from there is
not a plausible read of the position.

**It is a horizon artifact, and it is NON-MONOTONIC in budget**, which is the tell:

| depth / budget | best plan | Trace lines | game win turn |
|----------------|-----------|-------------|---------------|
| d5 / 20ms (ship) | Emiel (5) | **9** | T5 |
| d5 / 200ms | **Trace + Wish (6)** | **6** | **T6** |
| d5 / 1000ms | Emiel (5) | **9** | T5 |
| d6 / 20ms | Emiel (5) | **9** | T5 |
| d6 / 200ms | **Trace + Wish (6)** | **6** | **T6** |

The axis is BUDGET, not depth -- both depths agree at each budget -- and it is NON-MONOTONIC: 20ms
and 1000ms both score the Trace line 9, while 200ms scores it 6. That is the iterative-deepening
START GATE's knife-edge (`fits = estimate <= gate_alpha * remaining`): which pass commits decides
the answer, not how much total budget there was. And note
the one setting that DOES take the Trace line lands on **T6, worse than the T5 it gets by ignoring
it** -- so at least as the engine plays the continuation, the search is not obviously wrong to
decline it.

**The deck has NO value-leaf sidecar** (`ls decks/EldraziDisplacerFlicker/` -- no `.value.json`) and
runs at the bare `depth=5 budget=20ms source=default`. Per `value-leaf.md` the value leaf exists
precisely to replace the horizon rollout with an O(1) evaluator, and a missing model costs
1.35-84.8x. A combo deck whose payoff lies several casts beyond the horizon is the worst case for
the rollout leaf, and the win=9 above is what that looks like from the inside. **Generating the
value leaf is the principled next step** (`bash scripts/valueleaf.sh run decks/EldraziDisplacerFlicker`),
and per the serial-stages rule it must run ALONE on the box, after any in-flight measurement, on a
frozen commit.

OPEN, and being measured separately: whether the user's T3 kill is available on this board AT ALL.
The T3 sources are Aether Hub ({C} + {G}{G} from two Wild Growths), Conservatory (one G-or-W) and
Mariposa ({C}) -- and Cloud of Faeries is `{1}{U}`. Whether any blue exists there decides it.

### CORRECTION (user, 2026-09-04): the T3 line IS live -- and it dies at the PAYMENT, not the search

Everything above about "no blue on the board" was analysed on the WRONG BOARD. The reproduction
prefix `--choices "1,0,0"` puts BOTH Wild Growths on Aether Hub. The user's game puts the second one
on **Conservatory** (`--choices "1,0,1"`, T2 plan 1 = `land=Conservatory; cast: Wild Growth ->
Conservatory`), which is a different and much better board:

```
Aether Hub  (num 3)   + Wild Growth 53          -> {C} + {G}          (+ wild once Trace lands)
Conservatory (num 13) + Wild Growth 54          -> {W|G} + {G}        = 2 mana, self-sufficient
Mariposa Military Base (the T3 drop)            -> {C}
```

**Conservatory alone pays Trace of Abundance `{R/W}{G}`** ({W} for the hybrid, its Wild Growth's {G}
for the green) -- so Aether Hub is NEVER tapped for it, which is exactly what the user said and what
the earlier note got wrong. Trace then lands on Aether Hub, which taps for `{C}` + `{G}` + one WILD,
and Hub(3) + Mariposa(1) = 4 covers Living Wish `{1}{G}` plus Cloud of Faeries `{1}{U}`.

**Confirmed live:** on this board the T3 main phase does NOT end after the plan -- the engine
re-prompts within turn 3, holding Cloud of Faeries in hand. (On the wrong board it ended, which is
what made "not castable" look true.)

**And here is where it dies.** State after plan 13 (`Trace -> Aether Hub, Living Wish -> Cloud of
Faeries`):

```
Aether Hub    tapped:true      Conservatory  tapped:true      Mariposa  untapped
floating_mana: {"C": 1}
plans offered: ["cycle Cloud of Faeries to draw"]      <-- casting it is NOT offered
```

The payment produced 5 and spent 4. The leftover was booked as **`{C}`**. Demand was one R-or-W, two
G and one GENERIC; the only assignment that matters is which source covers the generic. Spending
Aether Hub's `{C}` there leaves the WILD floating -- and wild + Mariposa's `{C}` pays Cloud of
Faeries `{1}{U}` exactly. The engine did the reverse: it spent the WILD on the generic and floated
the `{C}`, and `{C}` cannot pay `{U}`. That wild is the ONLY blue on the board (Aether Hub's own
coloured mode is energy-gated and the energy is spent), so the choice is the whole game.

**This is a mana-payment ASSIGNMENT defect, not an enumeration or search-depth one**, and it is
general rather than EDF-specific: when several sources can cover a GENERIC pip, spend the least
flexible one and retain the flexible (any-colour) mana. The engine has no such preference here.

Neither existing lever fixes it -- `MTG_PREPAY_SHRINK=1`, `MTG_COLOR_RESERVE=1` and both together
all still float `{C}` and still offer only the cycle. So the fix is a new spend-ordering preference
(the `heuristic-optimization.md` route: mana-source tap/spend order is exactly its worked domain),
not a knob that already exists.

**Status of the two claims that started this:** the T3 CONTINUATION is real and the engine loses it
at the payment step. Whether a T3 KILL then exists still has to be replayed on this corrected board
-- the earlier "no T3 kill" analysis is void because it used the wrong one.



## WORK QUEUE OUTCOMES (2026-09-04)

All five queued items are resolved. Two turned out to be REAL bugs with wider blast radius than the
deck; one was a diagnosis I got wrong twice before measuring it properly; two were paperwork.

### 1. The T3 blocker: WILD MANA WAS SPENT ON A GENERIC PIP -- FIXED

`SpendFloatingTowardCost` (SpellEffects.h) drains generic pips **wild first**. At its original
caller that is right and load-bearing: after `BatchPrepayMainCasts` the reserve's `wild` IS the
turn's own generic requirement, with every coloured pip pinned to its colour, so draining generic
from wild is exactly what stops an earlier cast's generic pip from eating a later cast's pinned
colour.

It is exactly backwards at the OTHER caller -- `TapForCostBacktrackWorker`'s `out_leftover`
(SpellEffects.cpp). **That call is not paying anything.** The tap set is already chosen and the cost
is already known payable (`floating.CanPay(cost)` is the branch it sits in); all it decides is which
of the produced mana counts as spent, and therefore what the rest of the main phase inherits.
Wild-first there spends the one unit that can pay any colour and banks the one that can pay almost
nothing.

On the user's board: Aether Hub carrying a Wild Growth and a Trace of Abundance taps for `{C}{G}`
plus one wild. Living Wish `{1}{G}` took the `{G}` for its coloured pip and the **wild** for its
generic, leaving `{C}`. With Mariposa's `{C}` that is two colourless against Cloud of Faeries'
`{1}{U}` -- unpayable, and the turn stops. Spending the `{C}` leaves the wild, which pays the `{U}`
while Mariposa pays the `{1}`.

`MTG_KEEP_FLEXIBLE_FLOAT` (default ON) inverts the order for that one call. Measured live: turn 3
now offers `cast: Cloud of Faeries` where it previously offered only `cycle Cloud of Faeries to
draw`, and the line runs on to the Emiel + double-Drake menu. Pinned by
`test/scenarios/flexible_float_retained.json` (opponent_life 19 vs 20).

**Two wrong diagnoses preceded this, and both are worth recording because the second cost real work.**

* *"The payment taps the Aura's own future host."* Plausible -- an aura ramping a land it enchants is
  worthless if the land is already tapped -- and I built `LandAuraHostReserve` (a third producer for
  `g_plan_reserved_sources`) on it. **It is false.** A Trace-only plan leaves Aether Hub untapped on
  both arms: Conservatory already pays Trace by itself, because its `{W}` is the only white on the
  board for the hybrid pip. The reservation was measured INERT and REVERTED rather than shipped --
  the same call as the `MTG_LAND_AURA_RAMP` episode earlier in this file, and for the same reason.
  The one-line A/B that settled it (cast Trace ALONE, look at the board) cost a minute and should
  have come first.
* *"The leftover mana is not floated at all."* I read `floating_mana` as absent from the decision
  JSON. It is not absent -- it is emitted **inside the `me` object**, next to `land_drops_left`, and
  I was reading the top level. `MTG_FLOAT_TRACE` (added with this fix, ManaPayment.cpp) is what
  settled it: it prints what each payment LEFT BEHIND, which is the quantity that actually matters,
  and it showed `cost={1}{G} leftover{c1}` immediately. It also showed **no tap lines** for that
  payment, which is how the backtracker rather than the greedy was identified as the decision-maker.

### 2. Mariposa's `{5}, {T}: Draw a card` offered but unpayable -- FIXED, AND IT IS NOT A TEMPO BUG

`{T}` is part of the cost, so a source that also taps for mana **cannot fund its own ability**.
`AvailableManaPool` counts every untapped source, this one included, so the ability priced as
payable off its own mana. The apply was never wrong (AIEngine taps the source first, then pays, then
untaps on failure), which is exactly why the symptom was a silent no-op that re-prompted to the
`seg < 64` cap.

`PermAbilityTapDebitOf` (TurnSolver.cpp) debits the source's own yield per selection, computed as
the difference between the real pool and `AvailableManaPool(state, &source)` so it cannot drift from
that function's accounting. Applied after every same-turn credit, so a plan that casts a rock and
then activates still gets the rock's mana -- the distinction an emission-time gate would destroy
(and the reason the human-play trial-pay was removed from the Wirewood Lodge untap).

**All four `{cost}, {T}` cards in cards.json are affected**, because all four are mana sources whose
own yield covers part of the cost: Mariposa (`{5}` draw), Shivan Gorge (`{2}{R}` ping), Conservatory
and Kitchen (`{4}` Investigate). **Shivan Gorge is why this is correctness and not tempo** -- it is
this deck's lethal outlet, so an over-claimed activation is over-claimed DAMAGE, the `[fd-diverge]`
shape where the search projects a kill the game never deals.

The viewer half was fixed with it. `CheckLine` charged battlefield activations **nothing at all**, on
a stale assumption that every ability reached through the `cast=` verb is free -- so an unpayable
Mariposa draw came back `legal_not_enumerated`, i.e. the viewer told the user the search had a
reachability bug when the line simply could not be paid for. It now verdicts `illegal` with
`can't pay {5} for 'Mariposa Military Base' with the mana available this phase`.

Pinned by the boundary PAIR `tap_ability_self_funding_unpayable.json` (4 Forests, must be `illegal`)
and `tap_ability_self_funding_payable.json` (5 Forests, must stay `accept`). The positive half is the
one that would catch an over-tight fix, and it is deliberately non-discriminating.

### 3. `Plan::rad_mode` invisible to claude-play -- FIXED

The mode is a real searched axis, so 62 of turn 3's 168 plans were byte-identical TWINS of the other
106 and picking one over the other silently changed the game. `SummarizePlan` now appends
`(enters tapped, +rad)` and the plan JSON carries a structured `rad_mode` when the plan has the
choice. Emitted only then, so every deck without such a land serialises byte-identically.

**No chooser was installed, deliberately.** `g_play_land_rad_chooser` exists but nothing installs it
-- not claude-play, not the GUI. Installing it would OVERRIDE the plan's `rad_mode` (it takes
precedence in `LandPlay.cpp`), asking the human a second time for something they just answered by
picking the plan, and contradicting it. Surfacing the axis in the plan is the whole fix.

**Left open, one tier down:** turn 3 still shows 19 summaries duplicated 8x each, differing only in
`Living Wish`'s `tutor_target`. That is NOT the same defect -- the information IS in the JSON
(`actions[].tutor_target`), so the GUI renders it and only the summary STRING collapses. Adding it
would change every tutor deck's summaries and drop 262 references from the checker's exact-summary
tier to its `(land, casts)` fallback, which is a real cost for a display nicety. Worth doing
alongside a reference refresh, not on its own.

### 4. `MTG_EDF_PROSPECTIVE` -- FLIPPED TO DEFAULT OFF

Refuted on held-out seeds (t = -0.88). The `heurarm` slot is kept: the recognizer is correct in what
it claims, it just does not pay at this deck's depth/budget, and if EDF ever gets a value leaf the
horizon that made it inert moves. Re-measuring should cost one flag, not one reimplementation.

### 5. Is there a T3 KILL on the corrected board? **YES -- my "NO" was WRONG (RETRACTED 2026-09-04)**

I wrote, and pushed, that no turn-3 kill exists, "and it needs no search to see". That was wrong,
and the USER corrected it: *"No, T3 happens in a straightforward manner."*

**The claim, and why it was wrong.** I derived a turn-3 mana ceiling of about 4 after the Living
Wish line, concluded that it "does not cast Emiel {2}{W}{W} AND Peregrine Drake {4}{U}, so the blink
loop cannot start", and stopped there. Walking the ACTUAL engine to that point takes one command and
shows the opposite: the line **does** assemble the whole combo, and it does so a turn earlier than
the mana ceiling allowed for.

**CORRECTION (2026-09-04): the retraction's headline sentence was itself wrong.** It read "on turn 3
the board reaches Emiel the Blessed + BOTH Peregrine Drakes + Cloud of Faeries, with Aether Hub and
Conservatory still UNTAPPED and only Mariposa spent". No line reaches that board on turn 3, and the
arithmetic says so plainly: turn 3 has at most three lands (Conservatory, Aether Hub with its two
Wild Growths, Mariposa) for about five mana, while Emiel + Drake + Drake + Cloud is sixteen. The
ETB-untap refund cannot bridge it either — a Drake untaps at most the three lands that exist, so it
is roughly break-even rather than profitable, and it still has to be paid for at five up front.

Here is what the engine ACTUALLY plays, read off the game log rather than derived (seed 1 gi 0, ship
settings d5/20ms, profile attached):

| turn | play |
|------|------|
| T1 | Conservatory |
| T2 | Aether Hub, Wild Growth, Wild Growth (both on the Hub) |
| **T3** | **Mariposa Military Base + Emiel the Blessed** — the whole turn |
| T4 | Yavimaya Coast, Trace of Abundance, Peregrine Drake, Peregrine Drake, Living Wish → Essence Depleter |
| T5 | Trace of Abundance, then Emiel blinks and Essence Depleter drains **nine times** — win |

So the combo assembles across T3–T4 and cashes on T5. Turn 3 is one land and one four-drop, which is
what five mana buys.

**A methodology note, because I got this wrong twice in one session.** The second wrong version came
from walking the game under `--claude-play` and taking plan index 0 at each main phase, which is NOT
the engine's line — it is a hand-picked one, and on that line turn 3 casts `Trace → Aether Hub,
Living Wish` and Emiel never appears. Reading a menu tells you what is OFFERED; only the game log
tells you what is PLAYED. Both of this section's errors are the same mistake in different clothes:
asserting a board state from something adjacent to it instead of reading the state itself.

I mis-modelled the ETB-untap chain. Each Drake untaps five lands as it resolves, and the payment
tap-aheads into the float (`EtbUntapTapAheadIntoFloat`), so a Drake cast very nearly refunds itself
and the board comes back near-full between casts. I priced each cast against a pool that had already
paid for the previous one, which is simply not how the sequence runs.

**The loop is live on turn 3, and it has a draw sink.** With five mana up, Emiel blinks a Drake for
{3}; the Drake's ETB untaps all three lands, so the pool returns to six -- the iteration is free and
repeatable. Crucially the untap also re-arms **Mariposa Military Base**, whose `{5}, {T}: Draw a
card` is a {T} ability and therefore once-per-untap: every blink buys another draw. Verified by
walking it -- turn 3, library 50 -> 47 over the first iterations, blinks still offered. Draw far
enough and Shivan Gorge arrives, and its `{2}{R}, {T}` ping is re-armed by the same untap, which is
the kill.

**The lesson is the one this file already records twice.** A mana ceiling is a MODEL, and I asserted
it instead of running the line. The falsifier was the same single command that settled the other two
wrong diagnoses in this session. See [[hypothesis-before-measurement-mirrorwing]].

### 6. The turn ENDING after Living Wish -- the ordering search was offering a lie

USER: *"The problem is that it chooses to go to the next turn after living wish, which is
incorrect."* Reproduced exactly, and it is a third distinct bug, upstream of both mana fixes.

The cast-ordering search expands each action set into its distinct orderings, applies each on a copy,
and dedups by end-of-phase state. It never asked whether an ordering **dropped one of its own
casts**. A drop changes the end state, so it survives the dedup and ships as a distinct plan whose
summary advertises a cast it never makes.

Turn 3 offered both orderings of `{Trace of Abundance -> Aether Hub, Living Wish}` as adjacent menu
entries with the same cast set and the same `cast_order_canonical`:

* **Trace first** pays it off Conservatory alone -- whose `{W}` is the board's only white for Trace's
  `{R/W}` pip -- leaving Aether Hub untapped to receive the Aura and tap for three. The line runs.
* **Living Wish first** spends Conservatory on ITS pips, so Trace has no white left, is DROPPED
  (`dropped_casts: ["Trace of Abundance"]`), and the turn dies with Cloud of Faeries uncastable.

The dropping ordering sorts FIRST, so it is the one a player naturally picks. `MTG_ORDER_DROP_GUARD`
(default on) rejects any ordering that drops one of its own casts; if no ordering survives, the base
plan is kept rather than deleted -- deleting a whole plan when no variant survives is exactly the
failure mode of `OrderingPlacesAuraBeforeCreature` (34abe486's third symptom) and is not worth
reintroducing one loop later. Turn 3's menu goes from 163 plans to 99, and the only surviving variant
of that pair is the one that works.

### 7. Emiel's blink: the viewer could activate it but not TARGET it

USER: *"Emiel's ability is bugged in the viewer. I should be able to choose just to pay 3 with it
and what to target, but no options are given."*

The engine was fine -- it fans out one plan variant per legal blink target and tags each
`activate: true` with `blink_target`. The VIEWER collapsed them. `activatableSources()` keys a
permanent's activation options by `(verb, mode, payload)`, and for a blink all three are identical
across targets (`cast`, null, the outlet's own name) -- so every variant deduped to ONE unlabelled
entry, the picker never opened (it opens only at `opts.length > 1`), and the committed line read
`cast=Emiel the Blessed`, which carries no target at all. CheckLine then matched whichever variant
sorted first: the human's choice was not overridden so much as never collected.

Fixed with a `blink=<outlet>[@<target m_number>]` LineSpec verb, the `equip=`/`gyreturn=` precedent:
the engine emits `"verb": "blink"`, the viewer keys and labels options by target (showing the
TARGET's art -- four rows of the same Emiel picture is the undifferentiated menu this fix exists to
end), and `BlinksMatch` pairs declared targets to plan actions with `0` as the any-target wildcard.
EMPTY `spec.blinks` keeps the legacy cast-multiset matching, so every saved reference is unaffected
by construction. Verified: `@42` resolves to the Drake plan, `@200002` to the Cloud of Faeries plan,
a bare `blink=` and a legacy `cast=` both still accept, and a nonexistent target verdicts `illegal`.

### 8. USER HEURISTIC: the search always blinks the BEST UNTAPPER when one is available

USER: *"In the search Drake should always be targeted if available. (a heuristic rule)."*
USER, clarifying: *"we should target Cloud of Faeries if Drake is not there, it is just that the
drake has the strictly better ETB ability."*

`EldraziFlickerProvider::BlinkTargetCandidates` already ranked the Drake first -- `best_payload` is
the highest `etb_untap_lands` on the board and Peregrine Drake's 5 is the deck's maximum -- but it
returned a SECOND candidate too, the best attacker to grow with Emiel's counters. Now, when an
untapper is available, the attacker branch is dropped and the whole blink budget goes to the line
that actually goes off.

**The rule is "best untapper", not "Drake".** That distinction is the user's clarification and it is
what the code has always computed: the ranking is by the `etb_untap_lands` param, never by name, so
Drake (5) outranks Cloud of Faeries (2) only because 5 > 2, and on a board with no Drake the Cloud
is the candidate. The deck holds exactly these two untappers, so the two readings differ on exactly
one board -- Cloud present, Drake absent -- which is why that case is the one the tests exist for.
The flag was named `MTG_EDF_BLINK_DRAKE_ONLY` and is now `MTG_EDF_BLINK_UNTAPPER_ONLY`, because a
name that asserts a card the rule does not mention is the same class of defect as the misleading
plan label in §6: the behaviour was right and the label was wrong.

The rule holds up on its own terms, which is why it is safe as a hard preference rather than a score
nudge: blinking the untapper is the only target that refunds part of its own activation cost, so it
is the only one that can be REPEATED. Every other target spends {3} for one ETB. Growing an attacker
is real but cashes NEXT turn (a blinked creature returns summoning-sick), whereas the untap loop can
win this one -- and if the loop is live, its iterations put those same counters on anyway.
`MTG_EDF_BLINK_UNTAPPER_ONLY=0` restores the two-candidate set.

**Pinned by `test/unit/test_edf_blink_target.cpp`** (4 cases): Drake preferred over a Cloud sharing
the board; **Cloud is the target with no Drake**; the attacker branch survives when there is no
untapper at all; and `blink_own_only` excludes an opponent's Drake, so "best untapper" is read
within the legal target set rather than naming an untargetable one and suppressing the attacker in
favour of nothing. A unit test rather than a scenario fixture on purpose -- what is under test is
the CANDIDATE SET, and blinking a Cloud is mana-negative on its own ({3} for two untaps), so no
reachable win turn or life total separates "offered" from "offered and correctly declined". The
cases were checked to DISCRIMINATE, not merely to pass: with `MTG_EDF_BLINK_UNTAPPER_ONLY=0` the two
that carry the rule fail and the other two are unaffected.

Still not A/B'd: EDF is not in the regression suite, so the *value* of the preference needs a
deck-specific pooled run, which is queued rather than done. The unit tests fix the behaviour, they
do not measure it.

### 9. The go-off count was gated on BATTLEFIELD INSERTION ORDER (found by testing §8, not reported)

Writing the §8 tests surfaced a second, unreported bug in the same Drake-vs-Cloud choice — and it
was found by *running* the pair of heuristics against each other rather than by reading them.

**Two heuristics have to agree and did not.** `BlinkActivationCounts` proposes the one big "finish
it" iteration count only when `loop.payload_id == target`, where the target is whatever
`BlinkTargetCandidates` offered. So the go-off recognizer and the target preference must name the
same untapper. `FlickerTopLandYields` sums the **top-N** land yields, which means the refund
**saturates at the land count**: on a two-land board a Peregrine Drake (untap 5) and a Cloud of
Faeries (untap 2) hand back the same mana and tie on `net`. `RecogniseFlickerLoop` broke that tie
with `net <= best.net`, i.e. it kept whichever creature sat **earlier in `s.battlefield`** — so with
the Cloud earlier it named the Cloud as payload while the preference offered only the Drake. The
go-off count was then offered for **neither**: the Drake is the only target but is not the payload,
and the Cloud is the payload but is never a target.

**Measured, on one board, changing nothing but the order the two creatures were added:**

| battlefield order | target offered | counts from `BlinkActivationCounts` |
|---|---|---|
| Cloud, then Drake | Drake | `1 2 3` |
| Drake, then Cloud | Drake | `1 2 3` **`40`** |

The deck's kill was reachable or unreachable according to insertion order. Nothing aggregate could
have found this: both arms are legal play, the difference is one absent candidate, and it is
invisible in a win-rate or an avg-win-turn until it happens to matter.

**Fix:** on an exact `net` tie both recognizers now prefer the **larger untap count** (the board one
and `RecogniseFlickerLoopProspective`, which had the identical tie-break and would have drifted from
it otherwise). That makes them agree *by construction* rather than by coincidence: for a fixed
outlet `net` is monotone nondecreasing in N, so the max-net payloads always contain the max-N one,
and max-N is exactly what `BlinkTargetCandidates` returns.

Pinned by `test/unit/test_edf_blink_target.cpp`, which asserts **both orders** — a single-order test
would have passed for the whole life of the bug. Checked to discriminate: restore `net <= best.net`
and the Cloud-first order fails.

**Method note.** §5 of this ledger records a claim I made by hand-derivation that the user had to
correct, and the queue above records a ~60-line reservation feature built on a hypothesis that one
A/B refuted. This bug is the same lesson from the other side: the two heuristics *read* as though
they agreed, and one probe that ran them against each other showed they did not. On this deck,
derive nothing about mana or lines that a command could tell you instead.

### 10. "Does it work properly on T3 now?" — the BUG yes, a turn-3 KILL no (measured 2026-09-04)

Two different questions live in that one, and they have different answers.

**The reported T3 defect is fixed.** On the line where Living Wish is cast on turn 3, the menu now
offers both orderings of `{Trace of Abundance → Aether Hub, Living Wish}`, and the one that silently
drops its own Trace carries `"drops": ["Trace of Abundance"]` so it is labelled rather than played
blind. Turn 3 no longer ends after Living Wish.

**There is no turn-3 kill, and the evidence is a sample rather than an argument.** 800 games, 8
seeds × 100, ONE pooled `mtg --batch` at ship settings (d5 / 20 ms, profile attached):

| win turn | T4 | T5 | T6 | T7 | T8 | unwon |
|---|---|---|---|---|---|---|
| games | 1 | 51 | 368 | 284 | 85 | 11 |

Mean 6.54. **One** game in 800 wins on turn 4 and **none** on turn 3. The fastest line the engine
actually finds is the seed-1 shape above: combo assembled across T3–T4, cashed on T5 for nine
Essence Depleter drains.

This does NOT say a turn-3 kill is impossible — it says the engine does not find one in 800 games,
and that the mana arithmetic above makes one look unreachable from a normal opener. Recorded as a
measurement, not as the impossibility proof I wrongly wrote the first time.

Utilisation note: the pool drained to 5/24 workers near the end, because 8 coarse 100-game jobs
leave a long tail (one seed-2 game ran 0.26 h). Correct as one pooled batch per the repo rule, but
finer jobs would have kept the box fuller.

### 11. THE T3 BLOCKER, root-caused: a land Aura cast THIS TURN does not unlock its colour

USER, on the 800-game measurement in §10: *"The deck should be killing on turn 3 a decent chunk of
the time and turn 4 a great deal of the time."* And: *"Seed 1 is a T3 win and Seed 2 a T4 ... if
done properly."*

They are right, and the engine's own line validator names the reason in one sentence.

**The T3 kill on seed 1.** T1 Aether Hub, tap it for {G} off its ETB energy, Wild Growth on it.
T2 Conservatory, Wild Growth #2 on the Hub (Hub now taps for C+G+G). T3 play Mariposa; tap
Conservatory for {W} and the Hub for three; cast **Trace of Abundance on Mariposa** ({R/W} off the
Conservatory white — the board's only white); Mariposa now taps for C + *any*, which is the deck's
only blue. Living Wish → Cloud of Faeries → cast it → its ETB untaps the Hub and Mariposa → cast
Peregrine Drake ({4}{U} off the Trace) → its ETB untaps all three lands → second Drake → Emiel →
Emiel blinks a Drake for {3} and the untap returns six. **Net +3 per iteration: infinite mana on
turn 3**, with Mariposa's `{5},{T}: draw` re-armed by every blink to dig to a second Living Wish for
Essence Depleter, then twenty drains.

Every step is exact — several have zero slack — which is why the engine's miss is a single specific
gap rather than a scoring preference.

**The gap.** Ask CheckLine directly for the line, on a board with six mana and the Drake exactly
affordable after the Trace:

```
validate_line "land=Mariposa Military Base;cast=Trace of Abundance;cast=Peregrine Drake"
  verdict=illegal
  reason="can't pay for 'Peregrine Drake': no untapped source produces blue mana
          (a multi-color land makes only its own colors)"
```

The colour gate is computed against the board **as it stands before the plan**, so a land Aura being
cast *in that very line* — whose entire function is "add one mana of any colour" — is invisible to
it. Blue is declared absent and every plan casting a Drake, a Cloud of Faeries or anything else blue
is pruned before the payer ever prices it.

**This is the same family as the bug fixed in `edf_land_aura_pays_color`, one level deeper.** That
one was "the colour gates never look at Auras at all". This one is "they look at ATTACHED Auras, but
not at one the plan is about to cast". Confirmed by A/B on one board: with the Trace already
attached, turn 3 casts Drake + Drake + Emiel + Living Wish x2; with the identical Trace in hand,
turn 3 casts the Trace and **stops**, and the Drake is cast on turn 4.

It is a MODELLING gap, not a heuristic — 16 of the 60 cards are land Auras, and Trace of Abundance
and Fertile Ground both add mana of ANY colour, so this is the deck's primary colour fixing being
unavailable on the turn it is deployed.

**Not yet fixed.** The gate is a pre-filter, so widening it is the safe direction (an
over-approximation costs a rejected plan at apply time; an under-approximation deletes the line
forever, which is what happens today). Four sites were blind in the earlier fix and would need the
same treatment. Recorded here rather than attempted at the end of a long session.

**A second, separate gap, also unfixed.** Even handed the assembled board — Emiel + both Drakes with
the loop live — the engine wins on turn 6 **by attacking**, activating Emiel exactly once per turn.
It never converts the loop into a kill. `MTG_EDF_PROSPECTIVE=1` does not change it (win 6 either
way), so the go-off recognizer is not the missing piece. Root-causing this is separate work from the
colour gate above, and both must land before the deck plays to the user's expectation.

#### 11a. Work plan for the colour gate (written 2026-09-04 to survive a context compaction)

**Where the gate is built.** `ComputeAvailableColors` (`src/ai/TurnSolver.cpp:2456`) and
`BuildColorFeasibility` (`src/ai/ManaPayment.cpp:1395`) are each evaluated **once per enumeration**
off the pre-plan `state` — `TurnSolver.cpp:14674-14679` inside `Solve`, and `22345-22350` inside
`EnumeratePlans`. That "once, up front" is the defect: a land Aura in the candidate set has not
resolved yet, so its colours are not in the gate, and every later cast needing them is pruned.

**Proposed shape.** Widen both gates by the colours any land-Aura cast *in the candidate set* could
add: `land_aura_produces` when non-empty, otherwise ALL colours (an empty list means "any colour" —
Trace of Abundance and Fertile Ground both). The gate is a PRE-FILTER, so over-approximating is the
safe direction: a wrongly-admitted plan is priced by the real payer and dropped at apply time, while
under-approximating deletes the line permanently, which is what happens today.

Two more sites were blind in the earlier Aura fix and must be re-checked under the same lens, since
either can still veto a line the widened gate admits: `TapFlowInfeasible`
(`src/core/SpellEffects.cpp:1103`, the flow oracle, which can declare a cost infeasible before the
DFS runs) and `TapForCostBacktrackWorker` (`src/core/SpellEffects.cpp:1527`). `AddSourceToPool`
(`src/core/SpellEffects.h:7988`) was correct both times. `LandAuraBonus`
(`src/core/SpellEffects.h:7593`) is the existing helper for an ATTACHED aura's yield.

**Expected blast radius: none outside this deck.** The only mana-producing land Auras in
`cards.json` are Wild Growth, Overgrowth, Fertile Ground and Trace of Abundance, and all four are
EDF-only; EDF is not in the regression suite. So smoke/regression should come back
`configs changed: 0` and byte-identical. If they do NOT, the widening is reaching further than
intended and that is the signal to stop, not to rebaseline.

**Repro, before and after** (`--scenario`, board = Aether Hub under two Wild Growths, Conservatory,
Brushland; hand = Trace of Abundance, Peregrine Drake, Mariposa; six mana, Drake exactly affordable
after the Trace):

```
validate_line "land=Mariposa Military Base;cast=Trace of Abundance;cast=Peregrine Drake"
  NOW:   verdict=illegal  "no untapped source produces blue mana"
  AFTER: verdict=accept (or choose over the Trace host)
```
Fixture skeletons are in `logs/edf_t3/fx/` (`p7.fx` plays it, `p8.fx` validates the line); promote
one into `test/scenarios/` as the permanent pin, with the discrimination check recorded.

**Then re-measure, do not assume.** Re-run the §10 batch (8 seeds x 100, ONE pooled `mtg --batch`,
d5/20ms, profile attached) and compare the win-turn distribution against
`T4:1 T5:51 T6:368 T7:284 T8:85 unwon:11, mean 6.54`. The colour gate alone is NOT expected to
produce turn-3 kills, because gap two below is independent.

**Gap two, still un-root-caused.** Handed the assembled board (Emiel + both Drakes, loop live) the
engine wins on turn 6 BY ATTACKING, activating Emiel exactly once per turn, and
`MTG_EDF_PROSPECTIVE=1` does not change it. Start from `logs/edf_t3/fx/p3_trace_preplaced.json`,
which reproduces it in one command. Suspects, in the order worth testing: whether
`BlinkActivationCounts` ever proposes its go-off count in real play here (the §9 coherence fix makes
the payload agree with the target, but nothing has verified the count is USED); whether the wish
ranking fetches Essence Depleter once a loop is live; and whether `FlickerGoOffCount` returns 0 for
want of a recognised sink on this board.

### 12. GAP ONE FIXED AND MEASURED; GAP TWO ROOT-CAUSED IN THREE LAYERS (2026-09-04)

#### 12a. The colour gate — FIXED, measured, shipped (`d01f76c7`)

Both colour gates and the flat mana pool are built ONCE per enumeration off the pre-plan board, so
all three were blind to the one mana source a plan can INSTALL mid-turn. The turn-3 line was rejected
three separate ways: `ComputeAvailableColors` and `BuildColorFeasibility` said "no untapped source
produces blue mana", and the flat pool read the line as one mana short of a board that pays it
EXACTLY (six lands' worth, seven needed, the Trace supplying the seventh).

The two colour gates take a widening because they are PRE-FILTERS — an over-admitted subset is priced
by the real payer and dropped at apply time, an under-admitted one is gone for good. The mana COUNT
deliberately does NOT become an optimistic `a.rock_mana`-style stamp: an Aura's bonus is same-turn
supply only if a land survives the Aura's own cost still untapped (one Forest, {Wild Growth, Llanowar
Elves} — the Forest pays for the Growth and there is nothing left to enchant-and-tap). That condition
is exactly what a flat pool cannot express, so the credit lives in `SubsetPayableWithFilters`, the
real-payment simulation, where taps persist across the casts. Both halves are pinned:
`test/scenarios/edf_land_aura_unlocks_color_same_turn.json` (the line is admitted AND every surviving
variant really casts the Drake) and `edf_land_aura_credit_is_not_free.json` (the lone-Brushland board
where the second Wild Growth must stay unpayable — restore the enumeration stamp and it goes red).

**Measured**, 8 seeds x 100 games, ONE pooled batch, d5/20ms, profile attached, paired against the
§10 baseline:

| | dist | mean |
|---|---|---|
| before | `unwon:11 T4:1 T5:51 T6:368 T7:284 T8:85` | 6.5425 |
| after  | `unwon:12 T4:1 T5:42 T6:393 T7:281 T8:71` | 6.5187 |

**-0.0238 turns, se 0.0092, t = -2.57, better on 7 of 8 seeds.** Real, and small. **Still ZERO turn-3
kills and still T4:1** — as predicted, because gap two is independent. Suite: smoke 51/51 and
regression 85/85 both at `configs changed: 0`, scenarios 61/61, references 201 ok / 65 repaired / 0
play-drift. All four mana-producing land Auras are EDF-only, which is why the blast radius is nil.

#### 12b. A tighter, PER-SUBSET version of the same widen — MEASURED AND REFUTED

The shipped widen reads the HAND, so on a deck running 16 land Auras in 60 cards it switches
blue/black/red on for nearly every enumeration, including subsets that cast no Aura. The obvious
improvement is to widen per-SUBSET (the shape `MTG_SUBSET_ROCK_COLOR` already uses), which is more
precise and looked certain to be cheaper. It was built in full — `SubsetLandAuraColors` +
`AnyLandAuraCandidate`, both gates and the rescue made subset-aware — and measured on the same 800
games.

It is **worse on both axes**: quality **-0.0125 (t = -1.93, 6/8)** against the shipped widen's
**-0.0238 (t = -2.57, 7/8)**, and *more* work, not less — 51.2M solve-memo misses against 49.4M. The
cost hypothesis that motivated it is simply false: the broad gate wins games sooner, so the games are
shorter, and it memo-hits far more often (55.7M vs 47.0M) because the gate is uniform across subsets.
Not kept behind a flag — it has its verdict, and a lever behind a default-off gate is dead code.
(Wall-clock was useless here and must not be read into: the box swung between ~23 and ~5 usable cores
across the three runs. `loadavg-is-the-hosts-not-ours` again — the memo counters are the honest work
proxy because they are deterministic.)

#### 12c. Gap two: the drain kill, root-caused in THREE layers

Isolating fixtures in `logs/edf_t3/g2/` — identical turn-4 boards (Emiel + Peregrine Drake, loop live,
4 lands worth 7 mana, net +4/iteration), differing only in the sink:

| fixture | sink | result |
|---|---|---|
| `c.fx` | Shivan Gorge (board) | **win turn 4** — the go-off fires |
| `a.fx` | Essence Depleter (board) | win turn 5, blinking ONCE and draining 3, then attacking |
| `b.fx` | Living Wish in hand, sink in sideboard | win turn 7 |

The go-off count is NOT the problem: `MTG_EDF_GOFF_DEBUG` (added in this commit) shows
`ok=1 outlet=8 payload=9 net=4 drain=1/2 kmax=2 n=10` — the count is computed correctly and offered.
Forcing it to be the ONLY offered count still wins on turn 5, so the failure is in the APPLY. Three
distinct defects, each real, each verified:

1. **`SpendSurplusOnDrain` has no caller.** It shipped complete on 2026-09-02 (`6e860c22`, "NOT
   measured and NOT suite-checked") and was never wired into `ApplyBlinkLoop`, which calls only
   `SpendSurplusOnDamageSinks`. That alone is why the Gorge route works and the drain route does not.
   The function's own doc says the rule: *"Adding a class of sink here is only correct alongside a
   matching per-iteration SPEND for it — do both, or neither."*

2. **Wired at the END of the loop, the surplus is in the wrong COLOUR.** With the loop forced to 10
   iterations the float ends at **{G:29, C:0}** and buys **three** drains — 29 mana, 3 life. Essence
   Depleter costs `{1}{C}`; the blink's `{3}` is all generic, so the activation eats each pass's
   colorless as fast as the tap-ahead makes it, and the bank ends up entirely in the one colour the
   sink cannot spend. (`EtbUntapTapAheadIntoFloat`'s colour commit is need-aware only of the HAND's
   remaining pips — with an empty hand every choice-limited land floats `prod[0]`, which is green for
   both Conservatory and Brushland.)

3. **Wired PER ITERATION before the activation, the `keep_payable` guard fails to protect the loop.**
   This is the placement that *should* work — grab the colorless while it exists, under the same guard
   the damage sink uses. Traced: the guard's projection is `AvailableManaPool(state) + floating` vs
   `CanPay(drain + keep_payable)`, i.e. a TOTAL, and it mixes float with board mana that cannot
   actually be realised in the needed colours. It admits three drains, leaves the pool at 2, and the
   blink's `{3}` then fails — `done=0`, the loop never runs at all.

**So the real fix is a colour-aware reservation**, not a call site: the loop must genuinely reserve
its own activation cost before the sink spends, or the sink must be restricted to surplus above a
real reservation. That is a piece of engineering with its own measurement, and it is where to resume.
Nothing from layers 1-3 is committed — the tree carries only the `MTG_EDF_GOFF_DEBUG` diagnostic,
because not having it is what made this take an afternoon.

**Not a payment bug — checked.** The trace shows a `{1}{C}` drain leaving the float one lower rather
than two, which looks like a colour paying a `{C}` pip. It is not. Brushland carries
`tap_self_damage`, so `EtbUntapTapAheadIntoFloat` deliberately SKIPS it (a pain land is not free to
tap ahead) and it is still untapped when the drain runs; `pay` taps it for the `{C}`, so the float
goes 2 + 1 − 2 = 1. Everything balances.

That is worth knowing for the fix, though, because it says where the colorless actually comes from.
The deck's `{C}` sources are Aether Hub, Mariposa and Brushland, and Brushland is precisely the one
the tap-ahead will not bank — so it is the drain's only per-iteration colorless that the blink has not
already had first crack at. A colour-aware reservation should start there.

#### 12d. The isolating fixtures, inline (`logs/` is gitignored — this is the durable copy)

All three are the SAME turn-4 board and differ only in the sink, which is what makes the pair
decisive: `c` (Gorge) wins turn 4, `a` (Depleter) wins turn 5. Board, shared by all three — four
lands worth 7 mana a tap, loop net +4/iteration:

```json
{ "deck": "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod",
  "turn": 4, "on_the_play": false, "active_life": 20, "opponent_life": 20,
  "energy_counters": 0, "library_filler": "Forest", "library_size": 40,
  "depth": 5, "budget_ms": 20, "max_turns": 8,
  "battlefield": [
    { "name": "Aether Hub",             "controller": 0, "tapped": false },
    { "name": "Wild Growth",            "controller": 0, "tapped": false, "equips": "Aether Hub" },
    { "name": "Wild Growth",            "controller": 0, "tapped": false, "equips": "Aether Hub" },
    { "name": "Mariposa Military Base", "controller": 0, "tapped": false },
    { "name": "Trace of Abundance",     "controller": 0, "tapped": false,
      "equips": "Mariposa Military Base" },
    { "name": "Conservatory",           "controller": 0, "tapped": false },
    { "name": "Brushland",              "controller": 0, "tapped": false },
    { "name": "Emiel the Blessed",      "controller": 0, "tapped": false },
    { "name": "Peregrine Drake",        "controller": 0, "tapped": false }
  ],
  "hand": [] }
```

* **`a`** — append `{ "name": "Essence Depleter", "controller": 0, "tapped": false }` to the
  battlefield. **Currently win_turn=5.** This is the one the fix must turn into 4; promote it into
  `test/scenarios/` with `expect_win_turn: 4` at that point, NOT before (a fixture asserting the
  buggy 5 is a test of the bug).
* **`c`** — append `{ "name": "Shivan Gorge", "controller": 0, "tapped": false }` instead.
  **win_turn=4 today**, and it must STAY 4 — it is the control that says the loop and the go-off
  count are both fine, and any reservation change that breaks it has broken the working route.
* **`b`** — no extra permanent; set `"hand": ["Living Wish"]`. **win_turn=7.** The wish chain, which
  is a further hop and should not be touched until `a` is green. **RETRACTED — see 12g. That 7 was a
  HARNESS artefact: `--scenario` never dealt the wish pool, so the Living Wish fetched nothing. With
  a real pool the same board wins on turn 5 and there is no "further hop" here at all.**

Run with `./build/Release/mtg --scenario <file>`, and add `MTG_EDF_GOFF_DEBUG=1` for the count trace.

Both fixtures are now permanent: `test/scenarios/edf_blink_loop_cashes_drain.json` (the `a` board,
`expect_win_turn: 4`) and `edf_blink_loop_cashes_gorge.json` (the `c` control). `b` is untouched.

#### 12e. Gap two — FIXED. The bug was the drain's POSITION IN THE ITERATION, not a reservation

The conclusion recorded in 12c — "the real fix is a colour-aware reservation" — was **wrong about the
mechanism**, and the way it was wrong is worth keeping. All three failing layers were variations on
*where in the iteration the drain gets to spend*, and once the sink is put in the one position that
is actually safe, no reservation is needed at all and no colour logic changes.

`ApplyBlinkLoop`'s iteration is: damage sinks -> tap-ahead -> `pay(c)` -> `ApplyBlink`. The two sinks
want OPPOSITE ends of it, and for reasons that follow from their costs:

* **Shivan Gorge has `{T}` in its cost**, so it is once-per-untap and it must fire FIRST — before the
  tap-ahead, which would otherwise take the very tap the ability needs. Its value comes from the
  loop's untap, not from the loop's mana.
* **Essence Depleter has no `{T}`**, so it is bounded by mana alone and it must fire LAST — after
  `pay(c)` has already funded this iteration and after `ApplyBlink`'s ETB has untapped the board
  again. Both halves of that matter:
  - the current iteration is already paid, so nothing the drain spends can retroactively break it;
  - the next iteration is covered by `keep_payable = c` **and** by the untap that just ran, so
    `AvailableManaPool` at this point really is the board the next tap-ahead will draw on. That is
    what makes the guard's projection honest here and dishonest anywhere earlier — layer 3 ran it
    over a board the tap-ahead had already emptied into the float, so it admitted drains against mana
    the blink was about to spend.

That is the entire change: one call after `++done` with `keep_payable = c`, and one unguarded call
after the loop beside the existing damage-sink pass. The colorless-vs-green split described in layer
2 is real — `SpendFloatingTowardCost` step 3 does spend generic pips from `colorless` before any
colour, so the blink's `{3}` does eat the float's `{C}` — but it turns out not to be load-bearing:
with the drain running while the board is fully untapped it simply taps a `{C}` land (Hub, Mariposa,
or the Brushland the tap-ahead deliberately skips) for the pip. **Re-ordering that shared payment
step was NOT done**, because it is a hot path used by every deck and there is now no measurement
asking for it.

**Fixture:** `a` 5 -> **4** (`blink x5`, 15 drains, then swing for 6); `c` unchanged at 4; `b` still 7.

**Measured**, same 800-game pooled manifest, d5/20ms, profile attached, paired against 12a:

| | dist | mean |
|---|---|---|
| 12a (colour gate only) | `unwon:12 T4:1 T5:42 T6:393 T7:281 T8:71` | 6.5187 |
| + drain cashed         | `unwon:10 T4:1 T5:71 T6:395 T7:253 T8:70` | 6.4375 |

**-0.0812 turns, se 0.0116, t = -7.03, better on 8 of 8 seeds** — 3.4x the size of the colour-gate
fix and unambiguous. The movement is where the mechanism predicts: **T5 42 -> 71**, T7 281 -> 253,
unwon 12 -> 10. And it costs nothing: solve-memo misses 49.38M -> 49.08M with hits 55.69M -> 55.76M
(shorter games, same work per node), and the >30 s slow-game count fell 105 -> 77. Per-seed, all
eight moved the same way: -0.070, -0.040, -0.080, -0.130, -0.120, -0.050, -0.060, -0.100.

**T4 is still 1 in 800.** That is expected and is not this fix's job: reaching turn 4 needs the loop
ASSEMBLED by turn 4, which is a draw problem, not a cashing one. What this fix bought is that a loop
which does assemble now converts, which is the T5 column.

**Still open, and the same defect class:** `FlickerGoOffCount` also sizes a go-off for the LIBRARY-
EXILE sink (Dimensional Infiltrator, `{1}{C}`, also `{T}`-less), and `ApplyBlinkLoop` has no spend for
it either — so the count model believes in a route the apply cannot fire, exactly the over-claim shape
layer 1 was. It is far less reachable than the drain (sideboard-only, needs Living Wish AND the loop
AND `opponent_library_dealt`), so its quality effect should be nil; it wants a scenario fixture rather
than a measurement.

#### 12f. The library-exile sink — closed too, and it is NOT the drain's twin

`ApplyBlinkLoop` now cashes Dimensional Infiltrator (`{1}{C}`, `{T}`-less, deck-out) as well. Before,
a loop + Infiltrator board with 12 cards left in the opponent's library won on turn **6** by attacking;
it now wins on turn **4** by emptying the library, and the threshold is where the mana says it should
be — turn 4 up to `opponent_library_size` 16, turn 5 at 20.

The wiring position is the drain's (after `pay(c)`, after `ApplyBlink`, plus a final pass), but the
SPEND RULE has to be different, and that is the interesting part:

**Exiling has a step payoff, not a linear one.** Every point of drain is progress toward zero life,
so spending surplus on it is monotone. Exiling 11 of 12 wins exactly as often as exiling 0. So the
naive twin — "spend whatever is spare each iteration" — is not the conservative choice here, it is a
way to convert a real post-loop cast into nothing. `SpendSurplusOnExile` is therefore **all or
nothing**: it pays for the whole remaining library in ONE scaled payment or spends not a single mana.
That makes it non-negative by construction — when it spends, the game ends this turn, so there is no
line it can take mana away from — which is also why it needs no measurement and carries no threshold
to tune. (The looser policy, accepting a deck-out that lands a turn or two later off the opponent's
own draws, is real and possibly better. It IS a policy, and it would need a measurement on a route
this deck reaches almost never. Deliberately not done.)

**Two traps, both hit and both fixed:**

1. **A flat-pool projection is not a good enough all-or-nothing test.** The first version guarded with
   `AvailableManaPool + floating` vs `CanPay(cost * left)`. It passed on a board holding six colourless
   *in the flat sense*, and the sequential payments then realised only five — so it exiled 5 of 6 and
   threw ten mana away for nothing, the exact waste the rule exists to prevent. This is the same trap
   12a documents in the enumerator: a flat pool cannot express "this land's one tap is either the `{C}`
   or the `{G}`". The fix is to stop projecting and let the payer decide — one scaled `pay()`, whose
   failure path is an atomic rollback, so it can neither half-exile nor leak a tapped land.

2. **The colourless bank really does need protecting here — unlike the drain.** Twelve `{C}` pips are
   needed *simultaneously* and this board makes three colourless per untap, so they must survive as
   float across iterations. `SpendFloatingTowardCost` step 3 spends generic pips from `colorless`
   before any colour, so the blink's own `{3}` eats them as fast as the tap-ahead banks them.
   `g_hold_colorless_for_pips` moves colourless to the back of the generic order for the duration of a
   loop that has this sink on board. **Isolated by probe**: with the hold, library 6/12/16 all win on
   turn 4; without it, 6 -> turn 5 and 12/16 -> turn 6.

   So 12c's "colour-aware reservation" instinct was right about a mechanism — just about the wrong
   sink. It is gated on an exile sink being on the battlefield precisely so the **drain route keeps
   the payment order its measurement was taken under**, and so every other deck is untouched.

**Measured, and the point of measuring was to show it changes nothing.** Same 800-game pooled
manifest, exile change against the drain-only arm: **-0.0037 turns, se 0.0046, t = -0.81** — squarely
inside the run-to-run noise floor, with work unchanged (solve-memo misses 49.084M -> 49.055M). That
is the expected and wanted result: Dimensional Infiltrator is a sideboard 1-of reachable only through
Living Wish, so a measurement here could only ever confirm the blast radius is nil. The fixture is
what asserts the behaviour. Cumulative against 12a's baseline: 6.5187 -> 6.4337.

#### 12g. The third fixture was never testing the wish — `--scenario` deals no sideboard

`b` (the loop on board, Living Wish in hand, no sink anywhere) read **win_turn 7**, and 12d recorded
that as "the wish chain, a further hop". It was nothing of the kind. `RunScenario` hand-builds its
`GameState` and **never dealt the outside-the-game pool**, so `players[0].sideboard` was empty in
every fixture ever written: Living Wish resolved and fetched *nothing*. The game log says it plainly
once you look — the wish is cast on turn 4 and the hand is empty afterwards.

This is the `StampDeckTraits` hole one field over, and the header comment on that function already
warned about the shape: *"every OTHER path that hand-builds a GameState MUST call it too, or
deck-gated machinery silently reads defaults."* The wish pool had exactly the same requirement and no
such call. `GoldFishRunner::DealWishPool` is now factored out of `SetupGame` and called from
`RunScenario` as well — one definition, two callers, so they cannot drift.

**With a real pool, `b` goes 7 -> 5**, and the line is the intended one: turn 4 casts Living Wish,
**fetches Essence Depleter**, blinks twice to pay for it, and deploys it; turn 5 converts. Turn 5 is
also what the same board does with the Depleter already in HAND, so on this board the wish now costs
nothing at all. Pinned as `test/scenarios/edf_wish_fetches_the_sink.json`.

Two things follow that are worth stating plainly:

* **The tutor ranking was right the whole time.** `EldraziFlickerProvider::TutorCandidates`
  (`MTG_EDF_TUTOR_RANK=2`) scores a sink 100 when the loop is assemblable against 40 for a land, and
  with a real pool the wish takes the Depleter exactly as designed. It had simply never been handed a
  pool to rank inside a fixture. Nothing about it changed here.
* **`edf_wish_first_keeps_trace.json` moved from 3 variants to 24** — 3 Trace hosts x 8 wish targets.
  That is the fixture becoming *more* correct, not a regression: the wish's target is a genuine
  sub-decision that a human is entitled to be offered, and it contributed none before because it had
  no legal target. The property that fixture exists for (every surviving variant is Trace-FIRST) is
  untouched, which is exactly what its `expect_variants_all_contain` was written to guarantee against
  a count that moves.

**Reach for this the next time a fixture reports a surprisingly bad number.** The failure was not a
wrong result, it was a fixture that quietly did not exercise the thing it named — and it cost a
conclusion in this very document. A fixture that stages a card whose effect touches a zone the
harness does not build is testing the harness, not the engine.
