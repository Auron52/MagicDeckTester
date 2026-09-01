# Analysis ledger — EldraziDisplacerFlicker

Per-deck ledger for the `analyze-deck` run started **2026-09-01**. Durable state for the run
(survives compaction and hand-off).

Deck: [decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod](../../decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod)
Provider: **`EldraziFlickerProvider`** ([src/ai/DecisionProviders.cpp](../../src/ai/DecisionProviders.cpp))

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
| Emiel's counter watcher | **kill B** — N iterations = N +1/+1 counters, cashed NEXT turn (a blinked creature is summoning sick, CR 400.7) |
| Stroke of Genius `{X}{2}{U}` | **the sink** — with unbounded mana this draws the library |

### The three sinks, and why the sink is the whole problem

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

## Recommended next steps

1. **Value leaf** (`bash scripts/valueleaf.sh run decks/EldraziDisplacerFlicker`) — the deck is
   rollout-bound and its d0 policy is 1.4 turns worse than the search (8.52 vs 7.12), so the leaf is
   the highest-leverage lever available. This is also the gate that decides whether the deck can
   afford mulligan generation at all.
2. **Decisive 5c2 run** at `--blocks 4`.
3. **Do NOT add to the regression suite yet** — at ~20 s/game it would dominate the smoke budget.
   Revisit after the value leaf.
4. Wire the `etb_untap_lands` chooser (below).
5. Re-run the discard-analysis stage on a frozen binary (the first run's verdict, `STATUS_QUO_OK`,
   was measured across a binary that changed mid-run, and the regeneration used a deliberately
   under-sampled 20-game evidence pass).

## Open items / PROVISIONAL decisions (need user sign-off)

1. **Mariposa Military Base's rad-counter mode is always DECLINED**, so `Player::rad_counters`
   stays 0 and the rad mill is never reached (and so is not implemented). Reasoning in the card's
   bracket note. This is a disclosed decision, not a dropped clause — but it is the one place the
   implementation is conditional on a judgement call.
2. **`etb_untap_lands` is a DISCLOSED VIEWER GAP.** "Untap up to N lands" is a real choice, is
   auto-resolved by yield order, and a human cannot override it. It demonstrably matters (see the
   bug above). Wiring it needs a NEW multi-pick decision type (the Dragonstorm `dragon` shape).
3. **Trace of Abundance's shroud** is unmodelled: the passive opponent casts nothing and no card in
   this deck targets a land (Azorius Chancery's ETB return is not targeted).
4. **Blink activation COUNT in human play** is offered as `{1,2,3, go-off}` rather than a full
   ladder — a self-funding loop has no natural maximum, so "every legal count" is not well-defined.
5. **Emiel's optional `{G/W}`** is always paid when affordable (monotone vs a passive opponent).
6. **Devoid, flying, and the `{C}`/`{G/W}` reminder texts** are inert — disclosed.

## Approved deferrals

(none yet — every proposed deferral above is PROVISIONAL until the user signs it off)
