# Deck-combination screening skill

Comparing **combinations of an already-implemented card pool** — 3 Hatchery vs 4, cut the Vials, swap
2 Crystalline for 2 Striking. Card implementation, heuristic tuning, the play profile, the mulligan
table and the value leaf are **one-time costs per pool**; this skill is the *per-combination* loop,
and it is meant to cost **minutes**, not the hours a per-deck artifact regeneration costs.

It is NOT the route for: adding a card the engine does not implement (analyze-deck), adopting a
combination as a deck (mulligan-profile + value-leaf), or tuning a decision heuristic
(heuristic-optimization).

## Rule 0 — one command, one apparatus, one pooled batch

```bash
python3 scripts/deck_compare.py <spec.json>              # screen every combination vs base
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
is introduced by naming it (it must exist in `cards.json`). Every arm shares one apparatus and every
game of every arm goes into ONE `mtg --batch`.

**Do not regenerate per-deck artifacts per combination.** Sharing one keep table across arms is not a
cheap approximation, it is the *better measurement*: two different tables mulligan differently on
hands that have nothing to do with the edit, and that divergence lands in the comparison. Measured,
sharing one table **halves the standard error**.

Design and every measurement: `docs/design/deck-combination-screening.md`.

## The three things that make it fast, and why none is optional

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
3. **A coverage pre-flight.** A hand holding a card the table never bucketed does not get a biased
   answer, it gets **no** answer — `ExhaustiveKeepPolicy::Keep()` returns `present=false` and the
   caller falls through to the generic heuristic, silently. So if any combination introduces such a
   card the driver drops the table from **every** arm: deck-independent, therefore symmetric
   (measured −0.0003 error on a −0.20 effect), instead of silently lop-sided.

## The trap that has already cost a day: attach the play profile

`profile` is not decoration. Without it the engine loads `MulliganProfile::DefaultProfile()` and plays
a deck nobody ships. On slivers that zeroes `vial_target_mv`, and cutting 2 Aether Vial measured
**−0.078t instead of −0.030t — the effect inflated 2.6x**, because a Vial the engine cannot use is
free to cut.

It bites in **two** places, and the second is easy to miss:

- the measurement (`profile` in the manifest — `deck_compare.py` does this);
- **the keep-table generation**, which resolves the profile and value sidecar *directory-relative off
  the decklist*. A gen run from a scratch directory silently fits the table to the same wrong deck.
  It changes the artifact: the play digest moves, and on slivers bucket discovery **merged Ancient
  Ziggurat into the land bucket** (9 buckets instead of 10, ~1.8x fewer cells) because a deck that
  never casts Vial cannot notice that Ziggurat is the one land that cannot pay for it. With the
  profile present, R=10 discovery reproduces the committed R=60 bucketing exactly.

`mtg-analyze` now **refuses** a keep-gen with no `<stem>.profile.json` beside the decklist
(`MTG_KEEP_ALLOW_NO_PROFILE=1` is the deliberate hatch), and `--floor` copies the profile and value
sidecar into the arm directory before generating. Both guards exist because a warning at minute 0 of
a 90-minute gen is not read.

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
same game indices, so the difference-of-differences is fully paired.

```
bias  = delta(under the combination's own table) − delta(under the shared table)
floor = |bias| + 2·se
```

Two things to hold onto when reading it:

- **The bracket OVERSTATES the floor**, because its arm plays a low-R table that is simply weaker
  (measured ~0.032t at R=10) on top of any fit difference. That is the right direction for a safety
  check and the wrong direction for an accuracy claim — never treat the own-table arm as "the
  accurate one".
- **Do not "regenerate for accuracy" at R=10.** Measured on slivers: an R=10 table plays **0.032t
  worse** than the shipped R=60 one — as large as the whole effect being screened — while the
  own-vs-foreign *fit* difference among R=10 tables is only 0.004t. Regeneration only pays at high R
  (R=40 regret ~0.0006t). So an effect that does not clear its floor is **unresolved**, not refuted,
  and re-running the bracket bigger will not settle it.

## Cost, and where it actually goes

| stage | cost | frequency |
|---|---|---|
| screen (N combinations x 20k games, pooled) | minutes | per combination set |
| `--floor` bracket (R=10 table + 4 x 20k games) | tens of minutes | only for a result near its floor |
| a shippable table / value leaf for an adopted combination | hours | once, at adoption |

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
