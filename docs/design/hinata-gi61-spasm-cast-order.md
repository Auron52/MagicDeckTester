# Hinata smoke gi61: the one win eaccc120 cost, and why budget recovers it

**Status:** OPEN, root-caused, not fixed. Found 2026-08-24 while asking why commit `72226ff1`
"made things worse" — it did not (see §1); this is the regression it made visible.

> **§7 CORRECTS §2.** Measured properly afterwards — 9,800 games per arm against the two sides of
> `eaccc120` — the rank change is **exactly neutral on Hinata** (total turn-delta +0; 11 games
> better, 8 worse). The +0.0159 in the ground truth is a small-sample artifact of gi61 landing in
> the smoke seed and being counted three times. Read §7 before quoting §2's per-deck table.

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

## 7. Measured: the rank change is a WASH on Hinata, not a regression

§2 read Hinata's +0.0159 off the ground truth. That is four cells, two of them 150 and 75 games —
denominators small enough that one game is worth +0.0067 and +0.0133. So it was measured properly:
**9,800 games per arm**, built from the two sides of `eaccc120` itself (`/tmp/ec_pre` = `eaccc120^`,
`/tmp/ec_post` = `eaccc120`), across d0 x 8 seeds x 1000, d3 x 4 x 300, d5 x 4 x 150.

| | |
|---|---|
| pooled total turn-delta | **+0** (avg +0.000000) |
| games whose PLAY changed | 76 / 9,800 = **0.78%** |
| of those, games whose SCORE changed | 19 |
| better / worse | **11 / 8** |

Exactly neutral. Hinata runs **one** Sol Ring, and it is the only card in the deck the `{C}`-only
branch applies to (Izzet Signet produces `U`/`R`), which is why the binding rate is under 1%. This
is a high-variance, low-frequency lever: it re-rolls a handful of games in both directions and nets
zero. `eaccc120` is carried by stompy (-0.0380 over 6 cells, none worse) and costs Hinata nothing.

**Attribution is exact, not inferred.** gi61 flips at `eaccc120` itself — turn 8 to a loss at d0,
d3 AND d5 — against its own parent build. The commit's only code change is one line of
`ManaSourceRank` (`{C}`-only 10 -> 5); the rest is ground truth and a viewer test.

### 7.1 The games the suite never saw are the interesting ones

The two largest moves in the whole sweep are both outside the suite's cells:

| game | | |
|---|---|---|
| `d0 s8008 gi939` | 8 -> **6** | biggest gain |
| `d0 s4004 gi752` | loss -> **8** | an unwon game rescued |
| `d0 s5005 gi322` | 4 -> **loss** | **biggest regression — larger than gi61** |

gi322 is worth reading, because it is §4's defect in a second costume. Turn 2, same hand in both
arms; both play Mountain, cast Sol Ring `{1}` and Ornithopter `{2}`. The pre arm then **stops** —
out of mana. The post arm has a red source spare (Sol Ring's `{C}` absorbed the generic pips) and
spends it on **`Gamble {R}`**, which tutors Reality Spasm and then, per its
`discard_random_after_tutor`, discards at random — hitting **Hinata, Dawn-Crowned**, the deck's
namesake payoff. Turn-4 win becomes a loss.

So the two worst regressions share a cause, and it is not the tap order:

> `eaccc120` leaves MORE coloured mana unspent, and the engine spends it on a marginal cast that is
> net negative — a mana SINK before the untap ENGINE (gi61), or a tutor whose random-discard cost
> it does not price (gi322).

That is a "cast everything affordable" bias, not an affordability bug. The rank change is only the
thing that hands it the extra mana. Both instances are at d0, where no search exists to reject the
marginal cast; gi61 also survives a gate-budget search (§5).

### 7.2 What this changes about the fix

§6 proposed a cast-order rule (mana-positive before mana-sink). That still addresses gi61, but gi322
shows the class is wider than ordering: Gamble is not mis-ORDERED, it should not be cast at all with
the only Hinata in hand. Pricing `discard_random_after_tutor` against what is in hand is a separate,
smaller fix and is probably the cheaper of the two to validate.

Neither is attempted here. Both touch shared, all-deck machinery (the cast-order comparator; the
tutor value model), and the deck-level measurement says there is **no aggregate regression to chase**
— these are two latent defects worth fixing on their own merits, not a reason to revisit `eaccc120`.
