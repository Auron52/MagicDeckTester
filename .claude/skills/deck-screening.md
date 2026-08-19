# Deck-combination screening skill

Comparing **combinations of a card pool** — 3 Hatchery vs 4, cut the Vials, swap 2 Crystalline for 2
Striking, try 4 Lava Spike over the Skullcracks. Card implementation, heuristic tuning, the play
profile, the mulligan table and the value leaf are **one-time costs per pool**; this skill is the
*per-combination* loop, and it costs one batch, not the hours a per-deck artifact regeneration costs.

It is NOT the route for adopting a combination as a deck (mulligan-profile + value-leaf) or tuning a
decision heuristic (heuristic-optimization). A card the engine does not implement *is* in scope, but
only through analyze-deck — see **The new-card route** below.

## Rule 0a — "UNION DECK" IS A BANNED WORD. This overrides everything below.

**Do not construct a superset/"union" DECKLIST containing every candidate card and generate a
mulligan table for it. Not ever, for any deck, at any size, for any reason.** This is a standing
user directive, given twice:

> *"What is this union deck nonsense. It seems like a waste of time and energy."* — 2026-08-18
> *"I never want a union deck!" ... "Absolutely never."* — 2026-08-19

### Vocabulary — say "union TABLE", never "union deck"

The user has asked that **"union deck" be treated as a banned word** (2026-08-19), because the two
things it gets used for are opposites:

| term | meaning | status |
|---|---|---|
| ~~union deck~~ | a superset DECKLIST holding every candidate at once | **BANNED — never say it, never build it** |
| **union table** | one keep table whose cells are the union of what several REAL 60-card decklists can hold | **the correct artifact** |

Use "union table over the N combinations we are considering". If you catch yourself writing "union
deck", you are almost certainly about to build the banned thing.

**The distinction that matters — read this before concluding anything is forbidden.** The banned
object is a *decklist nobody would play*. A **union TABLE whose coverage is the set of plausible
60-card combinations under test is fine, and is the correct way to share an apparatus** (user,
2026-08-19: *"A union table based on a number of different plausible combinations that we want to
test is different."*, and *"Let's do the union table with real cells from 60-deck combinations we
are considering."*). Concretely:

| | verdict |
|---|---|
| One table covering the arms `TF3/Lib0`, `TF2/Lib1`, `TF1/Lib2`, `TF0/Lib3` — every one a real 60-card deck we intend to measure | **ALLOWED** — this is a union of plausible combinations |
| A 74-card decklist holding Twinflame 3 *and* Libation 4 *and* Anger 4 *and* Oracle 4 *and* Draught 4 *and* Scale 2 *and* Entrance 4 at once | **FORBIDDEN** — no arm plays that; its extra cells are unreachable |

The test is not "does the table cover more than one decklist" (that is the point of a shared
apparatus). The test is **"is every cell reachable by some decklist we are actually going to
run?"** If yes, generate it. If the coverage only exists because you merged cards that never
coexist in any arm, you have built the forbidden thing.

**Why it is not merely wasteful but wrong.** Cells scale as `C(K+6,7)`, so each extra card bucket
is superlinear. Worse, most of a superset's cells are **unreachable**: no arm ever holds a hand
mixing cards that no single 60-card decklist plays. Measured once at **31.7% unreachable**. You
spend hours producing apparatus nothing can ever query, and you raise K — which is the one thing
to avoid, because K is what makes generation expensive.

**What happened when this was ignored (2026-08-19).** An agent rationalised "but every card in the
union is under test", built a 74-card / K=20 / **1,167,340-cell** table, and burned **5+ hours** of
a saturated 32-core box before the user stopped it. The rationalisation is the failure mode: the
user's one narrow carve-out — *"unioning the table between some number of decklists we want to
test makes sense"* — does **not** license a superset over every candidate across every future test.

### The mechanism: `MTG_KEEP_ARM_DECKS` (built 2026-08-19)

```bash
MTG_KEEP_ARM_DECKS=arm1.txt,arm2.txt,...   # <=64 REAL 60-card decklists
```

The generator takes the per-bucket **MAX** across arms as its envelope (never their sum), then
**drops every composition no arm can hold** and stamps each surviving cell with a 64-bit **arm
mask** (bit a = "arm a can hold this hand"). Unset => old single-deck behaviour, byte-identical.
The decklist argument then only supplies the bucket space; the CELLS come from the arms. Measured:

| shape | kept / envelope | dropped |
|---|---|---|
| ratio sweep, 4 arms (Twinflame/Libation) | 289,064 / 291,738 | 0.9% |
| one 2-card slot, 4-way swap | 330,087 / 407,114 | **18.9%** |
| all 60 plausible tournament arms | 529,868 / 574,405 | 7.8% |

It also settles one-table-vs-append: ONE filtered table over all 60 arms is 529,868 cells against
~1,240,000 for four per-test tables — **2.3x cheaper, serves every test, no append machinery**.

**Do this instead.** Cover exactly the arms you will run — nothing more. Either generate a table
per REAL 60-card decklist and pool them with **`scripts/keepstore.py`** (the cell-by-arm store the
overnight campaign used, which shares cells wherever compositions coincide), or generate ONE table
whose card counts are the per-card MAX across *only the arms in this test* — which for a ratio
sweep like `TF3/Lib0 ... TF0/Lib3` is a legitimate union of plausible combinations. Always reuse
existing per-deck tables first (base / trick / libonly already exist). What you must never do is
widen the coverage to candidates that no arm in the current test plays, "so it will serve later
tests too" — that is precisely the rationalisation that produced the 1,167,340-cell waste. Scope
the table to the test in front of you; if a later test needs more, generate then, or ASK.

This supersedes the "pool table" guidance in *Never run without a table* below, and
`deck_compare.py`'s automatic pool-table path must be disabled (`"pool_table": false`) unless the
user has explicitly approved that specific table.

## Rule 0 — one command, one apparatus, one pooled batch

```bash
python3 scripts/deck_compare.py <spec.json>                    # screen every combination vs base
python3 scripts/deck_compare.py <spec.json> --preflight        # only the checks that need you; no games
python3 scripts/deck_compare.py <spec.json> --with-floor A,B   # screen AND bracket A,B in ONE batch
python3 scripts/deck_compare.py <spec.json> --floor A,B        # the brackets on their own
python3 scripts/deck_compare.py <spec.json> --confirm TAG      # re-measure the winner on held-out seeds
```

**Prefer `--with-floor`.** A bracket's shared cells *are* the screen's arms — a separate `--floor`
re-runs the identical games (verified by digest), so folding them into one batch is free.

Comma-separate the tags rather than invoking it per tag: the cells overlap (`base` under the
shared apparatus is one job however many combinations you bracket), so T tags cost **2T+2 cells in
one batch** instead of 4T in T batches — and T separate invocations strand cores on each one's tail,
which is what CLAUDE.md's pooling rule is about. One run at a time per deck is enforced with a lock:
both would rewrite that deck's arm decklists and `numbering.json` while the other's jobs are still
reading them.

```json
{
  "base":          "decks/slivers_vial/slivers_vial.txt",
  "profile":       "decks/slivers_vial/slivers_vial.profile.json",
  "value_profile": "decks/slivers_vial/slivers_vial.value.json",
  "games": 20000, "seed": 910000,
  "play": "quality",
  "combinations": {
    "more_leeching": {"Hatchery Sliver": 2, "Leeching Sliver": 4},
    "cut_vial":      {"Aether Vial": 0, "Muscle Sliver": 6}
  }
}
```

A combination is `card -> NEW COUNT`. Absent = unchanged, `0` = removed, a card not in the base deck
is introduced by naming it. An optional top-level `"replace": {"<tag>": {"<cut>": "<added>"}}` says
which new card inherits which departing card's slots. It changes no counts and no estimate — measured
+0.0003 ±0.0038 between two pairings of the same combination — but it changes PRECISION: the
better-matched pairing measured se ±0.0042 / 75.5% identical against ±0.0046 / 72.9%, i.e. the wrong
pairing costs ~20% more games. It only matters when two names are added at once; without the map the
freed numbers go out in sorted-name order, which is deterministic but arbitrary.

Every arm shares one apparatus and every game of every arm goes into ONE `mtg --batch`. `profile` and `value_profile` default to the deck's siblings; the driver **refuses** a
screen with no play profile (`"allow_no_profile": true` is the hatch — see the trap section).

`base` may be `.txt` **or** Cockatrice `.cod` — 14 of the 17 decks in `decks/` are `.cod`, and the
driver used to feed those to a split-on-space text parser, which yields nonsense card names rather
than an error. Both formats now go through `analyze_deck.py`'s parser; arms are always written as
`<stem>.txt` (the stem is what `mtg-analyze` names a generated keep table after).

**Do not regenerate per-deck artifacts per combination.** Sharing one keep table across arms is not a
cheap approximation, it is the *better measurement*: two different tables mulligan differently on
hands that have nothing to do with the edit, and that divergence lands in the comparison. Measured,
sharing one table **halves the standard error**.

Design and every measurement: `docs/design/deck-combination-screening.md`.

## What needs you, and what does not

The driver measures; it does not judge, and it cannot write a card. Four things are yours:

1. **Proposing the combinations.** A spec is a deckbuilding hypothesis. The tool ranks what you hand
   it and has no opinion about what is worth trying.
2. **Implementing a card the engine does not have** — `.claude/skills/analyze-deck.md` +
   `.claude/skills/mtg-rules.md`, then review it. The pre-flight refuses and prints the route.
3. **Noticing when the engine's MODEL of a card makes the comparison meaningless.** This is the
   judgement no measurement can make: `Skullcrack`'s oracle text carries
   `[Life-gain lock and damage-prevention lock not modelled]`, so screening Skullcrack → Lava Spike
   compares a fully-modelled card against a partly-modelled one and flatters the newcomer. The
   driver now *surfaces* every `[bracket note]` on every card an edit touches (both sides), plus
   which name-keyed levers (profile fields, provider source) reference an edited card — but
   connecting a note to the question being asked is still a reading task. Say it in the report — do
   not let the number stand alone.
4. **Deciding.** The driver prints an effect, a floor and a verdict; adoption is the user's call, and
   an adopted combination still owes its own artifacts (bottom of this file).

Everything else — numbering, apparatus selection, pooling, pairing, the floor bracket — is mechanical
and already in the driver. Do not hand-roll it.

## The new-card route

A card that is not in the base deck at all costs a one-time implementation, then joins the pool.
Run `--preflight` first: it runs no games, reads no binary, and tells you which of these you are in.

```bash
python3 scripts/deck_compare.py <spec.json> --preflight
```

| pre-flight says | what happened | what to do |
|---|---|---|
| `NOT IMPLEMENTED: X` | `cards.json` has no `X`; a batch would die on `Unknown card template` | implement it (rules skill), review it, re-run |
| `GAPS in X` | `analyze_deck.py`'s own oracle-text scan found an unimplemented clause | finish it, or record the deliberate simplification as a `[bracket note]` in `oracle_text` |
| | *the scan over-reports*: 4 of the 197 cards in `cards.json` are flagged `partial` today (Monastery Swiftspear, Leeching Sliver, Thrumming Hivepool, Ignoble Hierarch) purely for having an un-bracketed `Whenever`, and all four are implemented. **Read the card before believing the flag**; `"allow_card_gaps": true` is the hatch once you have. Only *introduced* cards are scanned, so a base deck's pre-existing flags never block a screen. |
| `NO card_scores ENTRY: X` | the profile has never seen `X` | nothing — the screen pools one automatically (~20 s) |
| `OK` | count changes over known cards | run the screen |

**The card_scores case is the one that would have been silent.** `ComputeHandScore` *skips* a name
that is not in `card_scores` (`AIEngine.cpp`), so a hand holding the new card is scored as if that
slot were empty, falls below `hand_score_threshold` more often, and gets mulliganed away — in the one
arm that plays the card. So the driver derives a **pool profile**: one `mtg-analyze` run over the
union of every arm's cards (~20 s), merged into a copy of the shipped profile, adding only the
entries it lacks. Merging rather than replacing is what keeps the base arm untouched — measured
**byte-identical, 0 of 11,967 games** — which is what makes it safe to give to every arm.

**Measured, it is real but small and most decks are structurally immune.** Two 4-cell paired runs:

| deck | introduced card | bias (pooled − shipped) | why |
|---|---|---|---|
| burn, 20,000 paired | Lava Spike (+0.213) | **−0.0001 ±0.0000** | `burn.profile.json` has a **`keep_model`**, which owns the keep decision at every hand size — the `card_scores` gate under it is unreachable, and the model's features never read it |
| slivers, 6,000 paired | City of Brass (+0.024) | **+0.0022 ±0.0010** (t=+2.1) | legacy static path: the gate is live, and 0.4% of games changed |

Of the 12 committed profiles only **slivers_vial and Knights** have a live gate: three carry keep
models, five have `hand_score_threshold = -1e18` (`NO_GATE`, which is what the analyzer emits today),
one has no `card_scores`. And slivers' +0.0022 is for a *land*; a Lava Spike-sized score in an
exposed deck would move more hands. Treat the pooling as **cheap insurance, not a rescue** — and if
a screen's headline turns on 0.002t, the apparatus floor already says it is unresolved.

## Never run without a table — but NEVER a union deck (see Rule 0a)

> **Superseded in part by Rule 0a.** The pool-table mechanism described in this section builds a
> table over a UNION decklist, which is now PROHIBITED by standing user directive. Keep the
> section's cost measurements (they are why a table matters at all), but get coverage by
> generating per-decklist tables and pooling them with `scripts/keepstore.py`, not by unioning
> the decks. `"pool_table": false` is the setting that disables the automatic union path.


Dropping the table is symmetric, and symmetric is not cheap. It costs **~0.063t of play quality on
both arms**, **~22x per-game wall** (slivers 9.8 → 254.8 ms/game), and it **regresses bottoming to
the lookahead** — `bottoming_enabled` is now baked ON at generation with no off switch, so **all 9
committed sidecars use table bottoming**. The 22x is *entirely* bottoming: with the table's keep
still live and `MTG_EXHAUSTIVE_BOTTOM=0`, slivers already costs 233.1 ms/game, because `BottomCards`
falls back to a rollout per candidate.

So when the shipped table cannot answer every arm, the driver generates one for the **pool**:

```
keep table     the shipped table does not bucket City of Brass
               generating a POOL table (R=10) over the 65-card union -> logs/deckcmp/pooltable/...
               verified: every arm fully covered (no heuristic keep, no lookahead bottoming)
```

- **One-time per pool**, not per combination — the same cost class this skill has always quoted for
  a mulligan table. Measured: **881 s** on 24 threads for a 65-card slivers union (10,231 size-7
  cells vs the shipped table's 7,758), after which the screen ran at **21–38 ms/game** instead of
  254.8. It pays for itself inside one 20k-game screen. Sized by cells: a merged new card is 0.60x
  the base table, a raised small bucket 1.06x, a new 4-of in its own bucket 1.72x.
- **Both halves are asserted, not assumed.** `verify_coverage()` checks every arm resolves;
  `verify_bottoming()` checks the artifact can bottom every hand, because `DecideBottom` fails
  independently of `Decide` (no bottoming block, flag off, or a missing target row). 0 of 10,231
  pool entries lacked one.
- **A screening apparatus, never a sidecar.** `pool_R` defaults to 10, which plays ~0.032t weaker
  than a shipped R=60 table — but *symmetrically* (own/foreign fit among R=10 tables is 0.004t), and
  it replaces something strictly worse on both axes. Adoption still goes through
  `mulligan-profile.md`.
- **Measured on three cases: the union deck does not bias the comparison.** Each is a
  difference-of-differences with R controlled, 20,000 paired games, one pooled batch:

  | case | pool grid vs base | union bias | t |
  |---|---|---|---|
  | slivers, count change inside a ≥7 bucket | unchanged | −0.0026 ± 0.0033 | −0.77 |
  | burn, 4 Skullcrack → 4 Lava Spike (card **merges**) | +0.5% | −0.0075 ± 0.0055 | −1.36 |
  | burn, 2 Mountain → 2 Mutavault (**new bucket**) | +63% (K 10→11) | +0.0010 ± 0.0049 | +0.21 |

  Both signs, a grid that grew 0% / 0.5% / 63%, all consistent with zero (~±0.01 at 2se, R=10 noise).
- **An introduced card usually MERGES into an existing bucket — do not assume a new one.** Lava Spike
  landed in *Lightning Bolt's* bucket, growing the grid by 55 cells (one 4-of's cap rising 4→7 at
  hand size 7); forcing a real new dimension took a colourless land in a mono-red deck. Two
  consequences: cutting a card **removes** a dimension from that arm's own table (the Lava Spike arm's
  is K=9/6,120 vs the base's K=10/10,945 — *coarser*), and the pool table, holding every arm's cards,
  is a **refinement** of them all. That is why it can play an arm better than that arm's own table.
- `"pool_table": false` restores the old symmetric-drop behaviour if you want the comparison.

**Not yet built:** topping up the base table instead of regenerating the union. The generator already
has whole-pool warm-start (`MTG_KEEP_PRIOR_RAW`), but it is for the same decklist on a new commit and
refuses otherwise — *"a changed decklist needs bucket translation, not yet built"*. The cell counts
say that would pay: a raised bucket needs only ~6% new cells.

## The four things that make it fast, and why none is optional

1. **Inherited numbering.** The opening shuffle is a positional Fisher–Yates, so a count edit
   re-permutes the whole game and the arms share nothing but the seed. The driver assigns `m_number`
   per copy (unchanged copies keep theirs, a replacement **inherits** the number it replaced) and the
   engine keys the shuffle off it (`deck_numbering` per job). Measured on burn: **4,594 → 215 games**
   to resolve a 0.03t effect, point estimate unchanged.
   `replace` is a PRIMITIVE, never `remove` + `add` — remove-then-add frees one number and inserts at
   another, shifting everything between (measured 1.4x worse).
2. **A shared, symmetric apparatus** — one `profile`, one `value_profile`, ladder mode
   (`value_model: false` + `ladder_value_leaf: true`) so the leaf accelerates warm-up passes and the
   heuristic decides the committed one. Verified byte-identical under a deliberately *wrong* model at
   unbounded budget; the residual budget coupling at `budget_ms: 20` measured 0.0008t.
3. **A coverage pre-flight on BOTH cliffs, and a POOL TABLE rather than a drop.** A hand the table
   cannot answer does not get a biased answer, it gets **no** answer — `Decide` returns
   `present=false` and the caller falls through to the generic heuristic *and* to lookahead
   bottoming, silently. It happens two ways, and only the first was ever checked:
   - **an unbucketed card** — any introduced card;
   - **an untabled composition** — compositions are enumerated capped at the *base* deck's bucket
     counts (`cap[b] = min(count[b], H)`), so **raising** a small bucket makes hands reachable that
     were never tabled, on that arm alone. Raising a 1-of to a 4-of on slivers is **6.32% of hands**,
     ≈0.004t of one-sided bias. Cuts are always safe; a bucket already at ≥7 copies cannot overflow.

   Either way the driver now **generates a table for the POOL** — the union of every arm's cards at
   the highest count any arm plays them — instead of dropping the table from every arm. Coverage is
   total by construction (union counts ≥ every arm's, for every card and every bucket), and
   `verify_coverage()` asserts it before a game runs. See the cost note below for what the drop was
   costing. Small composition overflows below `max_fallback` (default 1% ≈ 0.0006t, a tenth of the
   measured floor) just keep the shipped table and print the rate — regenerating for 0.008% would
   cost far more than the bias it avoids.

   Dropping the table is `MTG_EXHAUSTIVE_PROFILE=none` on the batch, **not** dropping the profile.
4. **An introduced-card pre-flight** — the part that needs you, above.

## The trap that has already cost a day: attach the play profile

`profile` is not decoration. Without it the engine loads `MulliganProfile::DefaultProfile()` and plays
a deck nobody ships. On slivers that zeroes `vial_target_mv`, and cutting 2 Aether Vial measured
**−0.078t instead of −0.030t — the effect inflated 2.6x**, because a Vial the engine cannot use is
free to cut.

It bites in **three** places. Each is now guarded, and each guard exists because the failure is silent:

- **the measurement** (`profile` in the manifest). The spec's `profile` used to be optional and now
  defaults to the deck's sibling; a screen with no profile anywhere **refuses**.
- **the keep-table generation**, which resolves the profile and value sidecar *directory-relative off
  the decklist*. A gen run from a scratch directory silently fits the table to the same wrong deck.
  It changes the artifact: the play digest moves, and on slivers bucket discovery **merged Ancient
  Ziggurat into the land bucket** (9 buckets instead of 10, ~1.8x fewer cells) because a deck that
  never casts Vial cannot notice that Ziggurat is the one land that cannot pay for it. With the
  profile present, R=10 discovery reproduces the committed R=60 bucketing exactly. `mtg-analyze` now
  **refuses** a keep-gen with no `<stem>.profile.json` beside the decklist
  (`MTG_KEEP_ALLOW_NO_PROFILE=1` is the hatch), and `--floor` copies both sidecars in first.
- **the table-drop path** — the one that bit the new-card route specifically. Dropping the table used
  to be implemented as "pass no profile", and `BatchRunner::ParseJob` auto-detects
  `<deck>.profile.json` *beside the deck*: the arm decklists live in a scratch directory, so every
  arm silently got `DefaultProfile()`. Introducing a new card is exactly what drops the table, so the
  route this skill exists to support was profile-less by construction. It is now
  `MTG_EXHAUSTIVE_PROFILE=none` on the batch, which suppresses the sidecar and nothing else.

A gen that finishes suspiciously fast is a symptom, not a win.

## The winner of a multi-arm screen is selection-biased — confirm it

A screen reports the **max of N noisy deltas** and calls it the winner. That is optimistic even when
every individual estimate is honest: the arm that wins is partly the arm whose noise pointed the
right way, and it gets worse with more arms. The screen prints a direct pairwise comparison of every
combination (the top two are usually the interesting pair, and that was the one comparison the tool
never printed) and then names the confirmation command:

```bash
python3 scripts/deck_compare.py <spec> --confirm <winner>
```

That re-runs base vs that one combination on a **disjoint block of games** (`confirm_seed`, default
`seed + 500000`) under the **same apparatus** — the pool table and pooled card scores are still built
from every arm of the spec, so the two numbers differ in their games and nothing else. It prints both
blocks and their difference; if the held-out block does not reproduce the screen's effect, report the
**held-out** number, because the screen's is the one that was selected on. This is the same
train/held-out discipline `.claude/skills/heuristic-optimization.md` applies to a swept heuristic.

The driver fingerprints each run's apparatus (arms, profile content, table, play settings) and
**refuses to compare** two blocks that did not share one — otherwise a spec edited between the runs
shows up as "shrinkage" and selection bias becomes indistinguishable from an apparatus change.

## The bracket is REWEIGHTED by default, and that is what makes it mean anything

A count-only edit needs **zero rollouts** to bracket. `BuildPolicyFromTables` takes the deck's
per-bucket `count` separately from the cell values, so retargeting the shipped raw to the arm's counts
is a re-weighting — **1.5 s instead of ~20 minutes, and at the shipped R (60) instead of R=10.**

That matters far more than the time. Three tables generated for the *same deck* at the *same R=10*,
differing only in generation seed, report a bracket "floor" of **0.0075 on average (max 0.0091) with
no fit difference to find** — and two of the three pairs read as significant at |t| ≈ 2. The real
generated floors on that deck were 0.0051 and 0.0057, i.e. **below the noise**. A generated R=10
bracket was measuring its own sampling lottery. The reweighted one is deterministic, so it has no
such lottery:

| bracket | floor | effect/floor | null vs shared | build |
|---|---|---|---|---|
| reweighted from R=60 | **0.0010** | 22.95x | −0.0015 / −0.0017 | 1.5 s |
| generated at R=10 | 0.0057 | 4.22x | +0.0604 / +0.0615 | ~20 min |

**What it bounds.** Fit has two halves: the hand weights and D_opt from `count` (reweighting
reproduces these exactly, at high R) and the per-cell rollout values (estimated on the source deck's
library, not re-estimated). So it bounds the weighting half properly and says nothing about the
rollout half — for which the generated R=10 bracket substitutes noise. Bounding both honestly needs
R≥40, which is the hours-scale route. `"bracket": "generate"` forces the old behaviour.

**When it is unavailable:** an introduced card, or a bucket raised past the source grid — the cells do
not exist to reweight. The driver checks that exactly (it is the `fallback_rate` condition) and falls
back to generating, saying which and why.

## Reading the output — the floor is the whole judgement

The screen prints, per combination: `delta` (negative = faster), `se`, `t`, `% identical`, and the
games needed for 3σ on a 0.03t effect. Report **avg win turn**, never win/loss language.

A shared apparatus carries *some* bias, so `t` alone does not settle anything — at 20,000 paired
games a 0.02t effect is wildly "significant" and may still be apparatus. The question is always
**does the effect clear the apparatus bias floor?** The one edit measured correctly end to end
(slivers, 2 Aether Vial -> 2 Muscle Sliver, 60,000 paired games) came in at **−0.0303 ± 0.0014
against a floor of 0.0068–0.0079 — a margin of 3.8–4.5x**, not the 5–9x an earlier profile-less run
suggested. Expect single-digit margins, and treat anything under 3x as unresolved.

`--floor TAG` measures that floor instead of predicting it: it generates a throwaway low-R table for
that one combination and re-measures the same delta under it, four cells in one pooled batch off the
same game indices, so the difference-of-differences is fully paired. It brackets **whatever the
screen actually runs** — the shipped table when that covers every arm, otherwise the pool table. (It
used to refuse an introduced card outright, which left the edit kind with the biggest apparatus
question with no bracket at all.) When the shared arm is itself a pool table, both arms are low-R, so
the "the bracket plays ~0.032t weaker" asymmetry below does **not** apply and what is left is the
union-vs-combination fit difference; the output says so.

```
bias  = delta(under the combination's own table) − delta(under the shared table)
floor = |bias| + 2·se
```

When the two arms hold **different cards** (any introduced card, any card cut to 0), each deck is
bracketed on *its own* table rather than both on the variant's. One table for both is right when only
counts differ, and wrong here: the variant's table has no bucket for the card the base still plays, so
that cell would lose the table on **40.0% of hands** for a 4-of swap — ~0.025t of one-sided damage,
an order of magnitude more than the bias being measured. With per-arm tables no cell falls through,
and the bias is exactly the difference of two **within-deck** nulls, which the driver prints:

```
per-arm nulls, own table vs the shared one (positive = the OWN table plays weaker)
  base        +0.00435    <- bias = null(variant) − null(base)
  mutavault   +0.00335
```

**Read the nulls, not just the bias.** A bracket is only worrying when the two nulls *differ*; two
large equal nulls are a level difference that cancels.

Two things to hold onto when reading it:

- **The bracket OVERSTATES the floor**, because its arm plays a low-R table that is simply weaker
  (measured ~0.032t at R=10) on top of any fit difference. That is the right direction for a safety
  check and the wrong direction for an accuracy claim — never treat the own-table arm as "the
  accurate one".
- **On the pool route there is no expected sign.** "Each table flatters the deck it was fit to" is a
  *shipped-table* reading. The union holds every card any arm plays, so the pool table **refines**
  every arm's partition and can play each arm *better* than that arm's own table (all four burn nulls
  came out that way). The driver prints no expected sign there — read the nulls.
- **Do not "regenerate for accuracy" at R=10.** Measured on slivers: an R=10 table plays **0.032t
  worse** than the shipped R=60 one — as large as the whole effect being screened — while the
  own-vs-foreign *fit* difference among R=10 tables is only 0.004t. Regeneration only pays at high R
  (R=40 regret ~0.0006t). So an effect that does not clear its floor is **unresolved**, not refuted,
  and re-running the bracket bigger will not settle it.

## Cost, and where it actually goes

| stage | cost | frequency |
|---|---|---|
| screen (N arms x 20k games, pooled) | **deck-dependent, see below** | per combination set |
| pool a `card_scores` entry for an introduced card | ~20 s | once per new card |
| `--floor A,B` bracket (1 R=10 table per arm + (2T+2) x 20k games, one batch) | tens of minutes, dominated by the table generations | only for results near their floor |
| `--confirm TAG` on held-out seeds (2 x 20k games, no new table) | one batch | on the winner of a multi-arm screen |
| a shippable table / value leaf for an adopted combination | hours | once, at adoption |

**Read the ms/game the driver prints per ARM, not a probe.** A 300-game probe is dominated by fixed
startup: the same burn job measured 42.5 and 114 ms/game on two 300-game runs against **59.3** at
20,000. And arms are not equally expensive — slivers' base arm cost 1,632,530 ms against `cut_vial`'s
280,522 for the same 20,000 games (**5.8x**, Aether Vial's enumeration), so a screen's wall clock is
set by its priciest arm, not the average. The driver prints the per-arm figure and flags a >2x spread.

**Play settings come from the DECK, never from a constant.** Each deck's value model carries a
`value_play` block that is its adopted, measured policy — burn `d6/b20` (fitted jointly with
`escalation_cap 6` and `value_trust_depth 5`), Goblins `d6/b40` — and several carry a cheaper
`mull_gen_*` pair already sanctioned for rollout work. `"play": "quality"` (default) uses the adopted
policy; `"play": "speed"` uses the mulligan-gen one (Goblins d3/b10). Omit `depth`/`budget_ms` from
the spec and the engine resolves it, printing `[play] <job> depth=.. budget=..ms source=value_play`
per job. Pinning them is an override and is labelled as one — the driver used to pin `d5/b20` on
every job behind `ignore_play_profile`, which screened burn one depth below the depth burn ships.

**Do not sweep depth on its own.** The screened effect is depth-invariant on burn (−0.0307 at d3, d5
and d7 alike), but the cost swing across those runs (148.2 / 49.2 / 17.8 ms per game) is *not* a fact
about search: `escalation_cap` and `value_trust_depth` stayed at the deck's fitted values while only
`depth` moved, which is the confound burn's own `value_play` note records being caught by. Move the
policy as a unit, or not at all.

**Per-game cost varies by two orders of magnitude, so quote the deck AND the apparatus, never
"minutes".** Measured at d5 / `budget_ms: 20` / `max_turns: 8`:

| deck | keep table | ms/game | a 4-arm x 20k screen |
|---|---|---|---|
| slivers | shared (count changes only) | 9.8 | ~1 min |
| slivers | dropped (a card was introduced) | 254.8 | ~30 min |
| burn | dropped | ~1,900 | ~1.8 h |

The `ms=` field on each job's `=== BATCH ===` line is that per-game figure (a *sum of per-game wall
times*, so it inflates under load — compare runs on a quiet box). **Read it off a 300-game probe
before committing to a long run.**

Per CLAUDE.md, everything goes through ONE pooled `mtg --batch`. The driver **tees** the batch's
`[batch] heartbeat: N/M workers busy` and `SLOW-GAME` lines to stdout as they arrive, so the
first-ten-minutes utilisation check needs no second terminal — they used to land in a log that also
carries one `[win]` line per game (the last floor run's only heartbeat was line 80,006 of 80,006).
A job that does not finish every game is a **refusal**, not a smaller `n`: a truncated run must not
read as a result.

## When a combination graduates

Screening ranks combinations under one apparatus. It does not produce a shippable deck. Once a
combination is chosen, it earns its own artifacts through the normal routes —
`.claude/skills/mulligan-profile.md` then `.claude/skills/value-leaf.md` — and its own regression
ground truth. The screen's number is a *ranking*, not that deck's measured strength.

## The provider is INHERITED, never re-detected (user directive 2026-08-13)

Archetype detection (`SelectDecisionProvider`) is by card params, so an edit can cross a signature —
cut burn's 4 Searing Blaze (`landfall_damage` is the whole signature) and detection routes that arm
to `Generic`, handing it a different engine's heuristics. In the screening context that error has
**no room to exist**: every arm is a *declared* modification of the base deck, so its identity is
given by the spec, not re-derived from the edited list. The driver sets `MTG_PROVIDER_DECK=<base
decklist>` on every batch **and** every table/score generation subprocess; the engine honours it at
the single `SelectDecisionProvider` choke point all callers funnel through, so no path can miss it.

Detection still runs per arm for REPORTING: the batch's `[play]` line prints
`provider_detected=<X> (pinned via MTG_PROVIDER_DECK)` on a crossing and the driver prints a NOTE.
Hooks keyed on a card the edit removed are inert (they fire on card params present in play); an
introduced card only another archetype has heuristics for is played generically — if it
underperforms expectation, the *provider tweak* is the likely next step, not a bigger screen. A
modification that is really meant as a NEW deck should be analysed as one (`analyze-deck.md`), where
detection applies as always. An arms-ran-under-different-providers refusal still exists but can now
only mean the pin failed to reach the engine — a bug, not a property of the edit.

## Guards that refuse rather than warn

Each of these exists because the failure it catches is *silent* — the run finishes and prints an
ordinary-looking number. None of them costs a game.

| guard | what it catches |
|---|---|
| entry count vs decklist | the shipped table was generated for a **different decklist** — every coverage answer here assumes they match, and `fallback_rate`'s caps come from the decklist. The count of size-7 cells implied by the decklist must equal the table's entries (exact on all 10 committed tables, K=10..21) |
| two tables beside one deck | a stale sidecar that **out-ranks** the one you meant. The engine resolves `.gz` before `.json`; `decks/slivers_vial/` holds a gitignored R=1 table beside the committed R=60 `.gz` right now (same buckets, different keeps — that was luck) |
| per-job completion | a batch that did not finish every game. Pairing on the intersection would just shrink `n` |
| apparatus fingerprint | a `--confirm` whose screen ran under a different apparatus |
| per-deck lock | two runs rewriting one deck's arm decklists and `numbering.json` while the other's jobs read them |

The apparatus also reports its own provenance now — `K`, cells, `effective_R` and the `commit` the
table was generated on (burn's is `52d0a58`, several hundred commits back). Within one screen that is
symmetric and harmless; a `--floor` **mixes** a shipped table with one generated at HEAD, and the
output says so, because artifacts are engine-state fingerprints (the Rule 0 every other skill here
enforces).

## Open

- A high-R (R≥40) confirmation tier is unvalidated: every floor measured so far uses R=10 tables
  whose own noise exceeds the bias they bound.
- **The pool apparatus is a property of the whole spec.** The union spans every arm, so adding or
  removing a combination rebuilds the table and silently changes what the other arms were measured
  under — numbers do not carry across spec edits. The screen says so; nothing enforces it.
- ~~A screen and a `--floor` run the shared cells twice.~~ **Built**: `--with-floor TAG[,TAG]` runs
  both in ONE batch (validated — it reproduces the standalone probe to the digit). Cross-run reuse
  stays rejected as too easy to get silently wrong.
- `n@3sig/0.03t` sizes a run against a 0.03t effect, but decisions are made against the measured
  floor (~0.005–0.01) — the column can read as "well powered" when it is not for the actual
  threshold.
- ~~Reweighting an existing table is specified, not built.~~ **Built for the BRACKET** (the default
  `--floor` route: free, deterministic, R=60, 5.7x tighter than a generated R=10 bracket). Still
  open: using a reweighted table as the *screen's* shared apparatus (today the screen shares the
  shipped table raw — the base's `count` vector on every arm), and the multi-source cell library.
- A 2-Headed Giant format axis would need its own table *and* its own value leaf — the leaf's
  transfer argument holds across combinations (generic, non-card-indexed features) but NOT across
  formats, where `OppLife`/`OppCreatures` stop being single-opponent scalars.
