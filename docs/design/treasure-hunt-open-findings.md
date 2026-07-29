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
