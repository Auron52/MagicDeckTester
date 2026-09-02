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

**What has NOT been tested is the one thing the user's estimate is explicitly conditioned on** —
"4-5 avg win turn *with a mulligan profile*". There is no keep table on this deck yet; the profile
is card-scores-only with `hand_score_threshold = -1e18`, i.e. no hand gate at all. A 4-card combo
deck is exactly the shape where hand selection dominates, so that stage is the live hypothesis and
it has not run. See the tractability item below — it is what stands between here and running it.

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
* **THE HEADLINE GAP IS NOT CLOSED.** 7.21 measured against the user's 4-5 estimate, and the two
  things that would most obviously explain it are both refuted: more search budget buys 0.063 turns,
  and pointing the wish at the win condition (which the adopted ranking does — 6 of 11 wishes vs 2
  of 12) buys nothing. **The untested candidate is the mulligan profile**, which is exactly what the
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
2. **Value leaf** — **RUNNING since 2026-09-02 01:26**, frozen at `e5924a43` / src-tree
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

## Claude-play sweep
- commit: `f5f664bf` (re-sweep, frozen binary; first sweep ran across a moving binary — disclosed)
- seeds: 8801 games: 16 (x2 sweeps, 32 agent-games total)
- flags: 0 unresolved

All flags from both sweeps were verified against `cards.json` + the rules skill and resolved:
**six fixed** (ETB-refund self-funding, `{cost},{T}` self-tap, tap-ahead wild mana, the Emiel-counter
documentation error, the land-aura creature-target leak, the land-aura subset reject), **two fixed as
protocol gaps** (blink target not surfaced, land host rendered as `#31`), and the remainder dismissed
with reasons — chiefly painland tap-order (legal, a known unshipped `MTG_PREPAY_SHRINK` lever),
`attached_to` m_number-vs-idx misreads, and unaffordable `{4}` Investigate plans being OFFERED but
cleanly no-oping (the enumerator deliberately carries no affordability gate on activations — the
Wirewood Lodge precedent — and the payment path is atomic).
