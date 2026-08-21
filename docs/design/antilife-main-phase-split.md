# Anti-Lifegain: the enforced main-1/main-2 split (MTG_AL_PHASE) — built, measured, DECISION OPEN

**Status 2026-08-21:** all levers BUILT and DEFAULT OFF (off-state byte-identical, per-deck
regression 5/5 PASS). Train is green; **held-out is slightly red with a named residual class**,
so the USER's adoption condition ("if my preference works better, let's stick with it") is not
yet met. The decision — adopt at the measured cost, iterate the classification, or stay with
re-evaluation — is the USER's. Everything below is measured on the engine at/after commit
a16074a (the payment-rollback fix this arc surfaced).

## Context

USER ruling (2026-08-21): *"There should be a real split between the two, not a re-evaluation"*
— the m1/m2 classification should be ENFORCED (Main2-classified casts withheld from the
pre-combat enumeration), not labels-plus-leftover-re-offer. Anti-Lifegain is the only
approved-order deck besides FiveColour with a second main at all (`uses_second_main=yes` via
`lifegain_to_loss`); CG/Knights/MW/slivers are single-main decks (verified byte-identical under
`MTG_SEARCH_SECOND_MAIN=1`, 20/20 keys — no interior m2 exists for them).

## The levers (all in AntiLifegainProvider / GoldFishRunner)

| lever | default | what it does |
|---|---|---|
| `MTG_AL_PHASE` | OFF | opt AL into the pre-combat Main2 filter (`ClassifiesMainPhases`), the 5C-shape per-deck adoption |
| `MTG_AL_DORK_M1` | ON (within the split) | mana dorks stay Main1 (the base sick-body rule sent Birds m2; measured below) |
| `MTG_AL_PHASE_ROOT` | OFF (**measured rejection**) | root-turn authority for the filter (condemnation-arc shape) |
| `MTG_AL_SSM` | OFF | searched interior second main, scoped to the split being live |
| `MTG_AL_SINGLE_MAIN` | OFF (**measured rejection**) | drop the deck's second main entirely |
| `MTG_M2_SEARCH_DEPTH` | unset (inert) | cap the interior m2 solve depth; measured NEVER-BINDS at gate budgets |

## The classification under the split (base rules + overrides)

m1: Tainted Remedy, Plague Drone, Ignoble Hierarch (`MTG_AL_DORK_M1`: Birds too), Aria of
Flame, both tutors, Invigorate, Swords to Plowshares.
m2: Birds of Paradise (without DORK_M1), Fiery Justice, Skyshroud Cutter, Reverent Silence
(USER 2026-08-18 override). Goldfish never blocks, so pure-damage m2 is safe (no
blocker-removal value pre-combat).

## Measurements (all per-game vs committed GT / fixed control)

**Train (per-deck regression, seeds 2002/3003):**
* PHASE: net −0.0033, 3 games faster, 1 churn-worse (recovers 4×), d0 byte-identical,
  ~880 searched games digest-only at identical scores.
* PHASE+SSM: +0.0107 — the SSM churn tax returns (all recover 4–16×; one fetch-draw-divergence
  game). SSM alone verified inert (scoping works).
* SINGLE_MAIN: red every key incl. d0 (+0.018..+0.028).

**Held-out (per-deck overnight, seeds 4004–10010, 8 searched keys / 8000 games):**
* PHASE (filter-everywhere): **+22 turns net, 53 worse : 29 faster**; escalation battery (both
  arms, 4×/16×): 13 churn, **38 budget-flat persistent** (4/4/4 vs 5/5/5 — the filtered-line
  signature), mostly +1-turn slips on fast T3/T4 kills, identical at d3 and d5.
* PHASE+DORK_M1: **+20 net, 37:16** — the dork rule recovers the Birds subclass (gi8-class:
  T2 "dump Remedy+Invigorate now" over "cast Birds, hold for the T3 kill") but trades away
  faster-games nearly 1:1.
* PHASE+ROOT (root-turn authority): battery recovers 39/53 of the old class but the full
  held-out **collapses to +83 net, 82:2** — filtered root candidates scored by unfiltered
  future projections is unsound mixed semantics. NOT cache poisoning (memos-off battery
  identical). Default flipped OFF, kept as instrument.
* SINGLE_MAIN vs fixed control: still +0.016..+0.028/key, 47:5 — the deck's "attack into
  range, then payload post-combat" lines degrade when everything must commit pre-combat.
  The USER's mechanical point is CONFIRMED though: nothing is rules-prevented (traces show the
  m1 unload reaching the same totals); the losses are the engine's pre-combat gates/attack
  decisions — a heuristic class, budget-immune, in principle fixable with attack-projection
  credit in the payload gates, unfunded because the split beats it anyway.

**The residual class (PHASE arms):** hold-vs-dump misvaluation. Diverging turns pick an early
Remedy/Invigorate dump over the control's hold-for-the-big-turn line even when the winning line
is fully expressible under the split (verified per-game: gi244's T4 Aria(+10-flip)+Invigorate
kill is legal in both arms). Zero nonconvergence (MTG_FD_ORACLE clean) — the search honestly
prefers the worse line. Mechanism: the filter runs inside rollout projections, and the greedy
playout tail plays deferred casts badly, deflating hold-lines. Budget-, depth-, SSM- and
dork-classification-immune; root-authority (the only precedented repair) measured worse.

## What this arc also surfaced (committed separately)

The USER's off-by-one audit of a T3 main found the **payment-fallback opponent-life rollback
bug** (fixed + GT rebaselined at a16074a): PayCost's greedy-fail fallback restored
battlefield/own-life/graveyard but not opponent life, so a Grove drip paid in the failed greedy
arrangement fired AGAIN under the backtracker — one cast, two drips.

## Open decisions (USER)

1. Adopt PHASE(+DORK_M1) at ~+0.0025/searched-game held-out cost for the doctrine preference —
   or keep re-evaluation (status quo) — or fund the residual-class dig further (next untried
   angle: value-model treatment of the playout tail, i.e. value-leaf territory; or per-card
   classification shrink — each m2→m1 move shrinks the filter toward re-evaluation).
2. MTG_AL_SSM: greedy interior m2 measures BETTER than searched for AL either way; the
   doctrine-complete form costs the churn tax. (5C's financing — the enum memo — does not bite
   here: 11 hits vs 1802 misses on a churn game; m2-search-memo 27%.)
3. Delete the measured rejections (`MTG_AL_SINGLE_MAIN`, `MTG_AL_PHASE_ROOT`,
   `MTG_M2_SEARCH_DEPTH`) or keep as instruments.

## Repro notes

Single-game repro needs BOTH `seed = base+gi` AND `game_index = gi` in the batch manifest —
the opponent-spawn schedule keys on `gi % 10`; a wrong game_index reproduces a different game
silently. Arm runs + escalation batteries + traces under `logs/ssm_sweep/` (gitignored).
