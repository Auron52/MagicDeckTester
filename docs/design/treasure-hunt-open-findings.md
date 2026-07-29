# Treasure Hunt: open findings (2026-07-29)

Deferred items surfaced while rewriting the TH scry rule (`685be48`). All are reproducible against
that commit; the suite is green (smoke 24/24, regression 40/40, overnight 96/96).

## 1. The search spends its land drop before Treasure Hunt (live, metric-invisible)

Seed 2142 (`--seed 2142 --games 1 --game-index 140`), **shipped budgets**, d3 and d5:

```
PLAY_LAND Ferrous Lake        <- drop spent BEFORE the engine
CAST Treasure Hunt  x2        -> 34 cards drawn, incl. TWO Reliquary Towers
cleanup: DISCARD x25          -> both Reliquary Towers binned
```

d0 plays it correctly: `CAST Treasure Hunt -> PLAY the drawn Reliquary Tower -> CAST Treasure Hunt`,
**zero discards**. So the correct line exists and the greedy path finds it; only the search misses it.

`THStrictFlood` (default ON) already encodes the force-defer rule, and `MTG_TH_LEGACY_SCRY`-style
A/B shows `MTG_TH_STRICT_FLOOD=0` changes only *which* land is played — so the gate is not what
governs the search's choice here. Root cause not yet isolated.

**Both lines still win on T4**, so avg-win-turn cannot see this and no regression case will ever
flag it. It needs a targeted assertion (e.g. "never cleanup-discard a `no_max_hand_size` land while
a land drop was available this turn"), not a win-turn test.

## 2. Reveal-log duplication (corrupts saved references / viewer)

One real game log contained **183 REVEAL events with only 4 distinct payloads** — an identical
Surveil entry repeated 139 times, another 42 times. A single surveil looking at one card cannot
occur 139 times, so rollout reveals are leaking into the real game log despite `RevealLogPause`.
Independent of the scry work. Affects `references/` JSONs and the play viewer's history.

## 3. REJECTED: earliest-castable-turn scry test (measured)

The shipped rule keeps a land only for a named reason (see `TreasureHuntProvider::ScryKeepOnTop`).
A more principled variant was implemented and measured: instead of counting colour sources, ask the
mana system how many additional land drops each target costs with vs without the top card, over
targets `{TH, TH+TH, TH+LE, LE}` (the same-turn pairs the deck wins with), using `AddSourceToPool`
so `produces_amount`, filters and ramp costs are all handled.

Motivation was sound — the land base is not uniform:

| land | timing | mana |
|------|--------|------|
| Sandstone Needle | enters tapped | `{R}{R}` per tap — a whole Land's Edge off one land |
| Saprazzan Skerry | enters tapped | `{U}{U}` per tap — a whole Treasure Hunt off one land |
| Steam Vents | untapped **only if you pay 2 life** | `{U}` or `{R}` |
| Frostboil Snarl | untapped **only if you reveal Island/Mountain** | `{U}` or `{R}` |
| Cascade Bluffs / Ferrous Lake | untapped | need `{U/R}` / `{1}` input to produce anything |

so neither "untapped" nor a raw source count is the right axis. **But it measured WORSE and much
slower:** +0.0055 avg across all 20 TH cases (every overnight case regressed), and the overnight
makespan went 269s -> 477s (+77%) because the drop-counting loop runs per scry. It does fix the
gi385 case below, but pays for that one game everywhere else. Do not re-adopt without a cheaper
formulation.

## 3b. The V2 scry sweep: six corrections, two harmful, four inert (2026-07-29)

A second pass took the six most concrete remaining gaps in `TreasureHuntProvider::ScryKeepOnTop` and
measured each one in isolation (`MTG_TH_SCRY_OFF=<letters>`, leave-one-out and one-at-a-time, on the
held-out overnight seeds). The `OFF=abcdef` arm reproduces ground truth exactly, so the gating is
verified, and only Treasure Hunt moved -- the other 86 suite configs are untouched.

| # | correction | alone (searched) | alone (d0) | verdict |
|---|-----------|------------------|------------|---------|
| a | filter lands need a feeder (Cascade Bluffs `{U/R}`, Ferrous Lake `{1}`) | 0.0000, 0 games | 0.0000 | inert |
| b | count MANA not CARDS (`produces_amount`: Needle taps `{R}{R}`) | 0.0000, 0 games | 0.0000 | inert |
| c | keep depletion lands as double-spell enablers | **+0.0060, 6 slower / 0 faster** | +0.0010 | **REJECTED** |
| d | conditional untap via `LandWouldEnterTapped` (Frostboil Snarl's reveal) | 0.0000, 0 games | 0.0000 | inert |
| e | Land's Edge in play does not end the colour problem | 0.0000, 0 games | -0.0030 | inert (see below) |
| f | target 2 blue / 4 mana for a two-Treasure-Hunt turn | 0.0000, 0 games | +0.0050 | **REJECTED** |

All six together measured **+0.0200 across the 8 searched overnight cases, 14 games slower and 0
faster** -- every case regressed. Removing (c) alone collapses that to **exactly 0.0000 with no game
changed**, so the depletion clause is the whole regression and (a)/(b)/(d)/(e)/(f) only ever mattered
by changing how often it fired.

**Why (c) loses -- the reusable lesson.** A depletion land ENTERS TAPPED, so keeping one converts a
spell-casting turn into a do-nothing turn, and this deck's clock (T3-T4 wins) leaves no room to ramp.
All three isolated slowdowns are the same shape, and in each the designed double-spell turn really
does happen, one turn too late. s6006 gi188:

```
T1  play Temple of Epiphany, scry -> Saprazzan Skerry on top
    V1:  bottom it  -> draw Reliquary Tower -> T2 Tower + Treasure Hunt -> WIN T3
    (c): keep it    -> draw Skerry -> T2 plays a TAPPED land, casts nothing
                    -> T3 Tower + Treasure Hunt + Treasure Hunt -> WIN T4
```

(f) is the same lesson one level up: *building toward* the bigger turn costs more than the turn buys,
and it is also what armed (c) most often (dropping (f) cut (c)'s damage from +0.0200 to +0.0160).
Depletion lands are good when a spare tapped-land turn exists anyway -- not something to seek out.

**Why (a)/(b)/(d) are inert.** They are accuracy fixes to counts that never reach a decision
boundary: the colour thresholds are 1 blue / 2 red, and 36 of the 53 lands make red with ~30 making
blue, so `count_sources < want` essentially never fires once two lands are down. They make the rule
say what it means but change no decision in 20 cases.

**(e) does not survive all the seeds.** On the overnight seeds alone it looks like the one winner
(-0.0030, 2 d0 games faster / 0 slower). Across all 8 d0 cases it nets **+0.0010**: smoke s1001
+0.0040 (1 game slower), regression s2002 0.0000, overnight -0.0030. Three games out of 9000 move.
It is still the correct model -- Land's Edge's damage ability costs no mana, so red really is
worthless once it is on the battlefield while `{1}{U}` is still needed -- but it is not a measured
gain, and adopting it costs a 10-case ground-truth rebaseline for score-identical play.

**Adopted: (a), (b), (d) and (e)** -- the four corrections that are not measurably harmful. They ship
as accuracy, not as a win: not one searched game's score moves across all 20 Treasure Hunt cases in
either direction, and d0 nets +0.0010 (3 games out of 9,000, 2 better and 1 worse). The rule now
models the land base correctly, which matters for the next person tuning it and for the keep-model
regeneration in section 5. Ground truth was rebaselined for the score-identical digest churn.
(c) and (f) are recorded as rejected in the code comment so they are not re-proposed.

### Deferred: depletion lands restricted to the T1 drop

The rejection of (c) is about TIMING, not about depletion lands being bad. A Skerry played on T3 is
too slow because T4 is usually the win; a Skerry played on **T1** costs nothing, because T1 has no
spell to cast anyway. So the clause might survive if restricted to the first land drop -- keep a
depletion land on top only on turn 1. Not pursued: the trigger is rare (the top card must be one of
8 depletion lands, on turn 1, with the target unaffordable), so the measurable effect would be a
handful of games in 20,000, and the sweep above shows this rule family sits well inside the noise
floor. Worth revisiting only if a cheaper high-volume signal than win-turn becomes available.

## 4. Residual slower games from the scry rewrite (accepted)

11 searched games are slower against 452 faster + 9 new wins (zero to-unwon). Two distinct causes,
confirmed by tracing:

- **Window slide** (gi194, gi382, gi385): Treasure Hunt reveals until a nonland, so the pile is the
  run of lands before the next nonland. Bottoming a land slides that window forward one and
  permanently loses the front land — pile exactly one card shorter.
- **Tempo/colour** (gi762, gi350): pile is *bigger* (+5, +12) but the win is a turn later, because
  the bottomed land was a source needed on curve. gi385 is the clean example: Steam Vents (untapped
  U/R dual) was bottomed because the colour test counted Temple of Epiphany, Forgotten Cave (both
  enter tapped) and Ferrous Lake (needs `{1}`) as three full red sources.

## 5. TH keep model is now train/serve mismatched

`treasure_hunt.keepmodel.exhaustive` (R=41, `bottoming_enabled=True`, commit `1b3c94f`) was
generated against the OLD scry policy, under which the scry was a guaranteed no-op in the early
turns (both legacy clauses fire on turn 1 with an empty hand). That is why its buckets prefer
Forgotten Cave over Temple of Epiphany at a mull-to-one: Temple's scry was literally worth zero, so
the cycler dominated. Under the new rule that preference should be re-derived. Note also that R=41
is low and the mulligan-profile skill flags low-R bottoming as noise-limited, yet bottoming ships
enabled here.

TH also ships **no `card_scores` at all** (its profile is just `{mulligan, version}`; Auras has 24,
Dragonstorm 19, Knights 15), so there is no secondary land-quality signal anywhere in its keep path.

## 6. Smaller items

- **antilife `discard_protect: hand` is now inert** — measured -0.0009 before merging origin,
  exactly 0.0000 after. Candidate to revert to the `all` default, leaving only dragonstorm opted in.
- **Auras `value_trust_depth = 5`** is provisional per its own note ("within 2-seed noise of
  UNSET"), from a 2-seed/400-game run, and is now baselined into the suite. Cheap to re-derive at
  4 seeds.
- **Value leaf at unbounded budget** (reviewed and accepted as not worth chasing): at d5 with
  budget 0 the leaf preferred banking a cycling land as a tapped land drop over cycling it, losing a
  mull-to-one game it otherwise wins on T7. Confined to a pathological state at a non-shipped
  budget, self-corrects at d6+, and TH measures neutral-to-better with the leaf on at shipped
  budgets (+0.0003 with it off). Latent if TH's budget is ever raised.
