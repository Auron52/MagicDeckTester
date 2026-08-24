# Hinata smoke gi61: the one win eaccc120 cost, and why budget recovers it

**Status:** OPEN, root-caused, not fixed. Found 2026-08-24 while asking why commit `72226ff1`
"made things worse" — it did not (see §1); this is the regression it made visible.

## 1. It is not `72226ff1`

`72226ff1` contains no engine source: a checker, a harness warning, and 25 repaired
`test/gt_logs/*.wins`. It cannot change play. What it changed is the *record* — those logs had
drifted a commit behind their own fingerprints, so the per-game half of the ground truth had never
been updated for `eaccc120`.

Repairing them is net BETTER, not worse: across the 10,825 games in the touched cells, **19 games
improved and 8 regressed, net −0.00129**. But five cells did move the wrong way, and three of them
are Hinata's smoke cells — which is what a reader sees first.

`hinata_smoke_d5_s1001`'s fingerprint was last changed **at `eaccc120`**, 5.8400 → 5.8533. The
repair recorded exactly that +0.0133 in the log half. The worsening was accepted eleven commits
ago; only its evidence was missing.

## 2. `eaccc120`'s real ledger

The adoption note for `eaccc120` said "net −0.039 across the ten moved cells, better on seven".
That is arithmetically correct **and scoped to the regression tier only**. The full picture across
all three tiers is 15 moved keys:

| deck | cells better | cells worse | sum |
|---|---|---|---|
| stompy | 6 | 0 | **−0.0380** |
| th | 2 | 2 | 0.0000 |
| goblins | 0 | 1 | +0.0010 |
| **hinata** | 1 | **3** | **+0.0159** |
| | | | **−0.0211** |

The smoke tier, where Hinata is uniformly worse, was outside the headline. At HEAD (the Land's Edge
reline having since returned `th_regression_d3_s3003` to its pre-`eaccc120` value) the net is
**−0.0251**, still driven almost entirely by stompy.

Hinata's entire +0.0159 is **one game**: smoke gi61, a turn-8 win that becomes a loss, counted once
in each of the d0/d3/d5 cells (+0.0010, +0.0067, +0.0133 — the same game over 1000, 150 and 75
games). Every other changed Hinata game is a play-digest change at an unchanged score, or a pair
that cancels (regression d0 gi388 7→8, gi966 7→6).

## 3. Repro

```
BIN decks/Hinata2/Hinata2.cod --seed 1062 --game-index 61 --games 1 \
    --depth 0 --budget-ms 0 --ignore-play-profile --threads 1 \
    --profile decks/Hinata2/Hinata2.profile.json
```

`eaccc120^` wins on turn 8 at d0, d3 and d5; HEAD loses at all three. Identical opening hand,
identical mulligans, and **identical play through turn 7** — the two lines diverge only on the
turn that was supposed to win.

## 4. Root cause: a cast ORDER, exposed by a better mana position

Hinata's kill is an untap engine: `Reality Spasm {X}{U}{U}` (`untap_x_mana_sources`, X free via
`discount_targets_scale_x`) untaps X sources, which funds the next Spasm, and the chain ends on
`Crackle with Power {X}{X}{X}{R}{R}`. `Soulfire Eruption {6}{R}{R}{R}` is a pure mana SINK.

Turn 8, after casting Hinata (`MTG_BP_TRACE=1`):

| | sources left untapped after Hinata | next cast |
|---|---|---|
| `eaccc120^` | Forbidden Orchard, Reflecting Pool | **Reality Spasm `{U}{U}`** → untaps 7 → Soulfire off the float → Spasm ×2 → Crackle X=9, **win** |
| HEAD | Mountain, Forbidden Orchard, Reflecting Pool | **Soulfire Eruption `{R}{R}{R}`** → all three spent → no `{U}{U}` → chain never starts, **loss** |

The difference is `eaccc120` working as designed: **Sol Ring** (`produces: ["C"]`) is the one card
whose rank it moved, and at HEAD its `{C}` pays Hinata's generic `{1}`, so a Mountain survives that
previously did not. HEAD therefore reaches the decision with **one more** untapped source.

That extra source is what breaks it. With two sources the engine could only cast Spasm; with three,
Soulfire Eruption also became affordable and was cast first, consuming the blue. Note that
`{U}{U}` was still payable at HEAD — from Forbidden Orchard + Reflecting Pool, the exact pair the
control arm paid Spasm with — so **the strictly better line was available and was not taken.** This
is a cast-order defect (a mana-positive untap engine must precede a mana sink), not an affordability
one. `eaccc120` did not create it; it moved this game into its reach.

## 5. Budget recovers it, which bounds the claim

d5 on HEAD:

| budget | 20 (the gate) | 50 | 100 | 200 | 500 | 1000 | 2000 | 20000 |
|---|---|---|---|---|---|---|---|---|
| win turn | **loss** | loss | 8 | 8 | 8 | 8 | 8 | 8 |

`MTG_UNPRUNED=1` also wins, in both arms. So the line is not pruned away and is not lost — the
search finds it at 5x the gate budget. What regressed is how easily it is found: at the suite's gate
budget the search commits to the Soulfire-first plan, and at **d0 there is no search to save it**,
which is why the d0 cell moved too. The d0 case is the honest core of the defect; d3/d5 at gate
budget are the same misordering surviving a starved search.

## 6. What a fix would be — and why it is not made here

The rule the engine is missing: **cast a mana-POSITIVE effect before a mana SINK in the same main
phase.** Reality Spasm nets mana (it untaps more sources than it costs whenever X >= 3); Soulfire
Eruption and Crackle are terminal sinks. This is the same shape as the adopted
`CastCheapestFirstWithinTier` accelerant ordering, one category up: accelerants already sort before
payoffs, but Reality Spasm is not classified as an accelerant.

Not done here because **cast order is USER-REVIEWED per deck** (see the process gate of that name),
and because the change would touch the shared cast-order comparator that every deck sorts with, so
it needs the full suite as its A/B rather than a one-game fix. Recorded rather than attempted.

Related: `docs/design/cast-order-ideal-with-ranges.md`, `docs/design/th-colourless-first-s3003-gi301.md`
(the same session's Land's Edge bug, and the same lesson about instrumenting the decision site).
