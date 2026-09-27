# Acquired-card same-turn deploy is DEPTH-0-ONLY (Acclaimed Contender)

**Status:** open defect, reproduced deterministically. Found 2026-09-27 while verifying a report from
another agent. **It affects a settled deckbuilding decision** (see "Why this matters" below), which is
why it is written up rather than filed as a curiosity.

## The claim, and the verdict

Another agent reported *"a bug with Acclaimed Contender where the search cannot deploy creatures received
the same turn."* An earlier pass over this (round L in `knights-copy-count-screen.md`) concluded the report
was **"stale, not false"** — that site 10's general rule covered the card and the impact was 1.8% of casts.
**That conclusion was wrong on the cause.** The report is correct, and the mechanism is not what round L
said it was.

The user's own framing was the right test: *"a breakpoint should open when we add the card. If that is
happening it should be working."* It is **not** happening at searched depth.

## Deterministic reproduction

```
./build/Release/mtg --scenario test/scenarios/whiteknights_contender_same_turn_deploy.json
```

That fixture is pinned at `depth: 0`, where the behaviour is CORRECT. Raise the depth to reproduce the
defect — copy it, set `depth` to 1, 3 or 5, and read `logs/play/…json` for turn 4:

| depth | turn-4 casts | same-turn deploy |
|---|---|---|
| 0 | `Acclaimed Contender`, `Venerable Knight` | **yes** |
| 1 | `Acclaimed Contender` | no |
| 3 | `Acclaimed Contender` | no |
| 5 | `Acclaimed Contender` | no |

Board is 4 untapped Plains + one Venerable Knight (required — `etb_dig_requires_subtypes` means no other
Knight, no dig at all), hand is Contender alone, and the library is stacked so a `{W}` Knight sits inside
the top-5 dig window rather than being drawn. After casting Contender for `{2}{W}` there are **2 untapped
Plains left**, so the `{W}` Knight is plainly castable.

**It is not a search-budget artifact.** Held at depth 5 the same-turn deploy is still declined at
`budget_ms` 50, 500, 2000 and 5000, and at depth 7 and depth 9. Verified.

**The control isolates mid-phase acquisition as the cause.** Put the *identical* Venerable Knight in the
opening hand instead of behind the dig, same board, same depth 5: the engine casts **both** on turn 4.
So nothing about mana, evaluation or the 2/1 itself is responsible — only whether the card arrived
mid-phase.

## Root cause: two mechanisms, and Contender falls between them

1. **Site 10 — the general rule (`MTG_BP_PUT_IN_HAND`, default ON) explicitly EXCLUDES this card.**
   Both the executor half (`AIEngine.cpp`) and the rollout half (`TurnSolver.cpp`) gate arming on
   `!ParamKeyedDrawClass(state, def)`, and `TurnSolver::ParamKeyedDrawClass` returns true for
   `p.etb_dig_count > 0`. Acclaimed Contender has `etb_dig_count: 5`. The carve-out is deliberate and
   load-bearing — its comment records that arming site 10 for a class another site already claims cost
   Mirrorwing d3 **0.1266** at every budget — so the fix is *not* to simply delete the condition.
   Round L's error was reading "site 10 is card-agnostic" as "site 10 covers Contender"; it is
   card-agnostic only *within* the set of casts no param-keyed class claims.

2. **The class that does claim it, `MTG_ACQ_DIG`, is depth-0-only BY DESIGN.** It is
   `EnvOn("MTG_ACQ_DIG", true)` — default ON — but `AIEngine.cpp` states *"MTG_ACQ_DIG is deliberately
   ABSENT here: the lever is depth-0-only"*, and `EngineFlags.h` records its adoption as *"d0 4/4 keys
   green (−0.0035..−0.0075), **searched byte-identical by construction**"*. So at every searched depth the
   lever does nothing, by construction rather than by accident.

Round L also dismissed the "SCOPE = d0 ONLY" note as *"dated 2026-08-19 and superseded"*. It is **not
superseded** — the d0-only scope is stated twice in current code, and the measured depth ladder above is
exactly what that scope predicts.

This is also why `dug_probe.py` found **0 of 3,000 games** casting a mid-phase-acquired card. That was read
at the time as a thin sample with "something downstream not converting". It was neither thin nor mysterious:
the decks are played at searched depth, where the mechanism is switched off.

## Why this matters

**Every Contender measurement in the WhiteKnights campaign was taken at searched depth (d3 in the suite,
d5 in the screens), i.e. with the card's same-turn deploy disabled.** Acclaimed Contender was cut from
WhiteKnights on a measured **~−0.0095**. An under-modelled card flatters the arm that cuts it — the
standing lesson of `bracket-notes-are-the-judgement-call`, which the user has had to point out twice in
this campaign — so **the Contender cut rests on a handicapped Contender and should be re-opened once this
is fixed**, not treated as settled.

Magnitude is bounded but not tiny-by-proof. The round L probe counted **16 of 876 casts** (1.8%) where a
1-mana Knight was dug, an untapped Plains remained, and the turn was not already lethal. That is a floor
on the frequency, not a ceiling: it counted only **1-mana** Knights, while a 2- or 3-mana Knight is equally
deployable when more mana is spare, and it says nothing about the value of each conversion.

## Fix directions (not attempted)

* **Preferred:** extend the `MTG_ACQ_DIG` deferred post-cast re-solve to searched depths, which means the
  rollout must arm the etb-dig class's own breakpoint the way site 10 arms its own. The plumbing already
  exists (`deferred_put_armed` / `deferred_site_index`); the work is giving the etb-dig class a site number
  of its own so both worlds agree on the site INDEX, which is the invariant the site-10 comments warn about
  ("a class one world counts and the other does not shifts every later `bp_at` index and silently changes
  play").
* **Do NOT** just remove `etb_dig_count` from `ParamKeyedDrawClass`. That hands the cast to site 10 and is
  precisely the collision whose cost is already recorded at −0.1266 on Mirrorwing d3.
* Either way this moves GT for any deck with an `etb_dig` card (WhiteKnights, Knights) and needs the
  regression accept flow, not a rebaseline over the top.
* `whiteknights_contender_same_turn_deploy.json` is pinned at d0 and guards the working path; a fix should
  make a d5 copy of it pass with `expect_opponent_life: -3` as well, and that copy should then be committed.
