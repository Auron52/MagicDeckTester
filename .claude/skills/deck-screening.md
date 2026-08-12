# Deck-combination screening skill

Comparing **combinations of a card pool** — 3 Hatchery vs 4, cut the Vials, swap 2 Crystalline for 2
Striking, try 4 Lava Spike over the Skullcracks. Card implementation, heuristic tuning, the play
profile, the mulligan table and the value leaf are **one-time costs per pool**; this skill is the
*per-combination* loop, and it costs one batch, not the hours a per-deck artifact regeneration costs.

It is NOT the route for adopting a combination as a deck (mulligan-profile + value-leaf) or tuning a
decision heuristic (heuristic-optimization). A card the engine does not implement *is* in scope, but
only through analyze-deck — see **The new-card route** below.

## Rule 0 — one command, one apparatus, one pooled batch

```bash
python3 scripts/deck_compare.py <spec.json>              # screen every combination vs base
python3 scripts/deck_compare.py <spec.json> --preflight  # only the checks that need you; runs no games
python3 scripts/deck_compare.py <spec.json> --floor TAG  # measure ONE combination's bias floor
```

```json
{
  "base":          "decks/slivers_vial/slivers_vial.txt",
  "profile":       "decks/slivers_vial/slivers_vial.profile.json",
  "value_profile": "decks/slivers_vial/slivers_vial.value.json",
  "games": 20000, "seed": 910000, "depth": 5, "budget_ms": 20,
  "combinations": {
    "more_leeching": {"Hatchery Sliver": 2, "Leeching Sliver": 4},
    "cut_vial":      {"Aether Vial": 0, "Muscle Sliver": 6}
  }
}
```

A combination is `card -> NEW COUNT`. Absent = unchanged, `0` = removed, a card not in the base deck
is introduced by naming it. Every arm shares one apparatus and every game of every arm goes into ONE
`mtg --batch`. `profile` and `value_profile` default to the deck's siblings; the driver **refuses** a
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
   bracket notes in `cards.json` are where that is recorded; connecting one to the question being
   asked is a reading task. Say it in the report — do not let the number stand alone.
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

## Never run without a table — the pool table

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
| `--floor` bracket (1-2 R=10 tables + 4 x 20k games) | tens of minutes | only for a result near its floor |
| a shippable table / value leaf for an adopted combination | hours | once, at adoption |

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

Per CLAUDE.md, everything goes through ONE pooled `mtg --batch`; check the
`[batch] heartbeat: N/M workers busy` line in the first ten minutes of any long run.

## When a combination graduates

Screening ranks combinations under one apparatus. It does not produce a shippable deck. Once a
combination is chosen, it earns its own artifacts through the normal routes —
`.claude/skills/mulligan-profile.md` then `.claude/skills/value-leaf.md` — and its own regression
ground truth. The screen's number is a *ranking*, not that deck's measured strength.

## Open

- A high-R (R≥40) confirmation tier is unvalidated: every floor measured so far uses R=10 tables
  whose own noise exceeds the bias they bound.
- Reweighting an existing table to a new combination is **zero rollouts** for count-only edits
  (`BuildPolicyFromTables` takes `count` separately from the rollout values, and `Comb(n,k)=0` for
  `k>n` drops unreachable cells automatically). Specified, not built.
- A 2-Headed Giant format axis would need its own table *and* its own value leaf — the leaf's
  transfer argument holds across combinations (generic, non-card-indexed features) but NOT across
  formats, where `OppLife`/`OppCreatures` stop being single-opponent scalars.
