# Snow's phase-A degeneracy: it is BREAKPOINTS, and three plausible causes it is NOT

Self-contained. Written 2026-09-21 after the user cancelled Snow's phase A at ~24 h
(12,731 rows banked, `A_rows` deliberately unmarked, queue resumable).

The user's framing that drove this, and which turned out to be the right one:
*"If we are doing something the other decks are not, we should fix it"*, and
*"we plan out the full rest of the turn unnecessarily with breakpoints... evaluating mana on a
turn segment basis doesn't make sense. We need to evaluate it preferably on a turn basis."*

## What was happening

Phase A ran 32/32 workers for 24 h. 18 of 32 in-flight games were over 10 h; nine sat at 23 h;
five of six sampled zero-row games emitted **nothing**. It was not starved (`3171%` CPU) and not
a scheduling fault.

**The proximate cause is a unit-scaling accident.** `SearchBudget::NODES_PER_VIRTUAL_MS = 900`
(`src/ai/SearchBudget.h`) and the labeller's budget is
`EnvInt("MTG_VALUE_LABEL_BUDGET_MS", 1000000)` (`TurnSolver.cpp`, ~43850), so the default is
**900,000,000 units per POSITION**. Snow converts units to real time at 11-28 units/ms, so one
position is permitted **9 to 23 hours**. `scripts/valueleaf.sh` (~753) never sets the knob, so
phase A runs at that default.

The fingerprint is unmistakable -- six of the worst games stop within 0.2% of exactly 900,000,000:

| gi | units | wall | u/ms |
|---|---|---|---|
| 33 | 900,108,836 | 22.38 h | 11.2 |
| 13 | 900,599,879 | 19.49 h | 12.8 |
| 195 | 901,963,405 | 18.86 h | 13.3 |
| 176 | 901,415,552 | 17.03 h | 14.7 |
| 224 | 900,423,590 | 14.87 h | 16.8 |
| 84 | 900,588,436 | 14.69 h | 17.0 |

They did identical WORK; the 8-hour spread between them is purely throughput. And a position that
trips the ceiling bumps the truncation watermark, so `EmitEvalRows` drops it -- *"their true win
turn is unknown, so no row was written"* (`AIEngine.cpp` ~453). Rows are emitted per position in
turn order, so one hung position takes the rest of that game's rows with it. **23 hours, zero rows.**
The same comment names the precedent: *"slivers/treasure_hunt/Knights produced zero rows in 34
hours: not slow decks, one hung position per game."*

**Direct proof the ceiling is what held the game:** seed 901283 (the 22.38 h game) completes in
**80 seconds** at `MTG_VALUE_LABEL_BUDGET_MS=3000` -- 2.8M units instead of 900M, same win turn
(7), 6 of 7 positions kept.

## Where the cost actually is

One game, seed 901283, budget-bounded (`MTG_BP_PROBE=1`):

| breakpoint site | consultations | share | searched | overrun | untarget |
|---|---|---|---|---|---|
| `snow_look_top` (Scrying Sheets / Frost Augur) | 1,605,062 | 54.4% | 75.7% | 94,076 | 295,605 |
| `put_in_hand` (any cast whose resolution drew) | 908,930 | 30.8% | **19.3%** | 149,633 | 583,681 |
| `deferred_cantrip` | 417,963 | 14.2% | 100% | 0 | 0 |
| `post_entry_act` | 16,333 | 0.6% | 81.5% | 1,631 | 1,390 |
| **total** | **2,948,288** | | 61.8% | **245,340 (8.3%)** | **880,676 (29.9%)** |

Against `interior_nodes = 2,753,762` -- **the breakpoints ARE the search**. Unit sites agree:
`fs_bp_wave` 58.9%, `fs_pre` 39.2%, `rollout_step` **0.48%**. Almost no unit reaches a rollout.

**What Snow does that no other deck does:** it runs 16 permanents that put cards into hand, 8 of
them REPEATABLE -- 4 Scrying Sheets and 4 Frost Augur (`{1}{S}`/`{T}`: look at top, if snow put it
in hand), 4 Arcum's Astrolabe (ETB draw), Ice-Fang Coatl. Every card arriving in hand mid-turn
opens a re-plan, and they chain. `snow_look_top` is bp site 8; the code notes *"Snow reaches site 8
on every consultation."* No other deck in the suite has one.

## Why condemnation cannot help

Condemnation's premise, stated at the mana-site exemption: *"the pre-breakpoint section CONSIDERED
a card and declined it."* **A card Scrying Sheets just put in hand was never considered and never
declined -- it was not there.** So for 85% of Snow's breakpoint work the mechanism is structurally
inapplicable. Measured: `bp_condemn_seen=1,249,500 drops=5,447` = **0.44%**, all of them `searched`
(no waste, just no reach). Two explicit exemptions compound it:

* `MTG_BP_CONDEMN_ACTIVATION` = **0, off entirely** -- no activated ability is ever condemned, and
  Snow's cost is entirely activations.
* `MTG_BP_CONDEMN_MANA_SITE_EXEMPT` = **on** -- if the card that opened the breakpoint ADDED mana,
  condemn nothing there. (Corrected 2026-09-21: an earlier version of this line said the exemption
  fires because "Scrying Sheets is a mana land / Astrolabe is a mana rock". It does not --
  `BpSiteAddedMana` keys on a ritual float, an untap-X effect or a Treasure mint only, and the
  whynot census reads `manasite=0` on Snow. The exemption is inert here; the two live blockers are
  the ones the census names below: `noplancast` and `managrew`.)

There is no condemnation tuning that fixes this.

## Three causes this is NOT (each proposed, each killed by its own control)

1. **Global plan-cache ratchet.** Both memo pools sat pinned (`fsl=1605M/1605M`,
   `plan=3199M/3200M`) and the code warns the symptom of a pool leak is *"pool full forever =>
   recompute everywhere => a uniform silent slowdown with BYTE-IDENTICAL play"*. But throughput
   over the run dips and RECOVERS (median u/ms by sixth: 24.0, 26.0, 27.8, 19.9, 18.8, 23.7) --
   not a ratchet -- and the bp cache self-clears on full. Not a leak; at most thrash.
2. **Mana-side enumeration / Arcum's Astrolabe.** Snow is genuinely the ONLY deck that reaches the
   mana side (116,049 enumeration calls vs **0** for Melira Pod and slivers_vial, 108 for
   FiveColour), and its affordability filters prune nothing (`SubsetPayable` and `ColorFeasibility`
   each drop **0** of 72,461). Looked decisive. **`MTG_NO_ROCK_RAMP=1` drives the mana side to
   exactly 0 and the cost does not move** -- units 1.002x / 1.018x / 1.003x on three games,
   identical win turns. Slightly WORSE without it. The unique thing is real and is not the cause.
3. **Prefix-scoped mana prepay.** `MTG_BP_PREFIX_PREPAY` was already BUILT, MEASURED and
   **REJECTED on quality** (+0.0200 hold, t 6.20; +0.0214 train, t 6.41; paired 5000/cell) -- see
   `bp-node-partition.md`. Its lesson is the important part and it CONFIRMS the user's principle:
   *"the 'wasted' float is NOT waste. The mana a base plan taps for its tail is chosen jointly
   across the whole turn, and that joint allocation is better for the node's CONTINUATIONS than a
   prefix-only payment is."* Scoping mana to a segment loses. (It is also inert unless
   `MTG_BP_NODE` arms `bp_capture` -- an A/B on Snow came back byte-identical, 1.000x.)

A methodological note worth keeping: the control table in (2) was initially presented with Snow's
number **censored** by the very ceiling under investigation while the controls were uncensored.
Capped and uncapped numbers must not share a column.

## The open design (the user's, and the doc that asks for it)

`bp-node-partition.md` root-caused the cross-dupe bucket and found it is **not** a transposition
problem: *"the cross bucket is the base-plan enumerator enumerating PAST the breakpoint and the
node then covering the same ground again. That is exactly what the USER's direction at the top of
this doc forbids (**'Enumerating ahead without the drawn or staged cards doesn't make sense either
way'**): the doctrine was implemented for the node but **never enforced on the enumerator feeding
it**."* Sized: cross 1,192,267 of 1,541,982 dupe children, ~10% of node units.

So the user's 2026-09-21 framing is a restatement of their own earlier directive, still only
half-implemented. The named, unbuilt fix is **truncate-at-emission** (truncate AFTER
`eval_and_push`'s filters; a naive pre-filter drop is lossy and already confirmed so).

And the payment model the rejection explicitly asks for is the user's other proposal --
*"only figure out which state we want later when we need different mana"*:

> *"Do not re-propose this without a payment model that KEEPS THE JOINT ALLOCATION while dropping
> only the truly-dead tail."*

Deferring commitment is the one move that gets both: no float in the pend state (so the existing
prefix dedup collapses the duplicates) **and** a whole-turn joint allocation when it is finally
resolved (so nothing strands). Clairvoyance makes deferral safe -- the engine already runs a
*"clairvoyant search over a known, deterministically-shuffled library"*, and there is a precedent
for the shape: Call of the Wild's `activated_reveal_top_cost`, *"repeatable; the search chooses how
many activations (clairvoyant top)"* -- one segment, N as a decision variable, no breakpoint.
Scrying Sheets and Frost Augur instead ride `tap_draw_cost` + `tap_draw_requires_top_supertype`,
which puts a card in hand and therefore opens a breakpoint.

## Two facts that do not depend on any of the above

* Snow does not need a value leaf at all. The 2026-09-20 shape probe ran 6,000 games and died
  before printing its verdict; recovered from the surviving `wins/`+`units/` artifacts:
  heur 375,921,463 units / 5.8725 avg; `esc_nl` 1.443x / z=+1.80 (tie); **`fit_nl` 1.160x /
  z=+2.35 (BETTER play)**. Leafless is fine on Snow; it is not cheaper, but choosing it means
  phase A never runs. StompySurprise and Goblins already ship `leaf: none` with a model-less
  `.value.json` carrying only the shape and `mull_gen_depth`.
* The `g_bp_enum_depth` counter does two jobs. Its stated purpose in
  `BpDeriveContinuationList` is *"suppress the fan-out"*, but the enum memo requires
  `g_bp_enum_depth == 0`, so every breakpoint continuation is also disqualified from the
  enumeration memo. Measured hit rates on Snow: 0.07% (`hits=8 misses=10,839`), 5%, 7.5%.
  Separating the two concerns is a small change worth measuring on its own.
