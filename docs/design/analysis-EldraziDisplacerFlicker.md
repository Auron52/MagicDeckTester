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
