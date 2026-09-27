# Acquired-card same-turn deploy is DEPTH-0-ONLY (Acclaimed Contender)

**Status (updated 2026-09-27):** defect reproduced deterministically, **and the fix is already in the tree,
switched off** — `MTG_BP_HAND_ENTRY=1` closes it at every depth tested (d1/d3/d5/d7) with 105/105 scenarios
passing. What remains is an **adoption decision plus a multi-deck GT rebaseline**, not engine work. See
"THE FIX ALREADY EXISTS" below; the fix directions this doc originally proposed were wrong and are marked
superseded. It affects a settled deckbuilding decision (see "Why this matters").

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

## THE FIX ALREADY EXISTS IN THE TREE AND IS SWITCHED OFF (2026-09-27)

**`MTG_BP_HAND_ENTRY=1` fixes this completely, and needs no new code.** Verified on this document's own
depth ladder — the table above inverts at **every** rung:

| depth | flag `0` (current default) | flag `1` |
|---|---|---|
| 1 | Knight never cast, opp life −1 | **cast on turn 4**, opp life **−3** |
| 3 | Knight never cast, opp life −1 | **cast on turn 4**, opp life **−3** |
| 5 | Knight never cast, opp life −1 | **cast on turn 4**, opp life **−3** |
| 7 | Knight never cast, opp life −1 | **cast on turn 4**, opp life **−3** |

−3 is exactly what the d0 fixture asserts as correct, so searched play now matches d0 at every depth
tested. **All 105 scenarios pass with the flag on**, including this fixture and Gideon's.

**Why no new plumbing is needed — and why my own "preferred direction" below was WRONG.** I proposed giving
the etb-dig class *its own site number*. That would have re-created the exact renumbering hazard the tree
calls *"the single largest hazard in this change"*. `MTG_BP_HAND_ENTRY` (EngineFlags.h, default OFF) is
already **"THE REST OF THE GENERAL RULE"**: it takes one section-level hand snapshot in `ApplyPlanDirect`,
asks `HandGainedACard` immediately before the deferred re-solve loop, and — gated on
`!deferred_cantrip_resolve`, i.e. **only when no class armed at all** — arms the **EXISTING site 10**. So it
catches precisely the hole `ParamKeyedDrawClass` carves out and **renumbers nothing**. Both worlds already
have twins (`TurnSolver.cpp:26890`/`30909`, `AIEngine.cpp:2205`/`5762`), so the `bp_at` lockstep holds by
construction. Its own census already counted this deck's class: **`knights dig 4,096 of 21,566 = 19%`**.

**The superseded directions, kept for the record:**

* ~~Preferred: give the etb-dig class a site number of its own.~~ **Unnecessary and harmful** — see above.
* **Do NOT** remove `etb_dig_count` from `ParamKeyedDrawClass`. Still true: that hands the cast to site 10's
  cast window and is the collision already measured at −0.1266 on Mirrorwing d3.

## What adoption actually costs — the honest picture

The flag is off for **measured** reasons, not because it is wrong. From
`breakpoints-should-key-on-hand-entry.md` (verdict 2026-09-22, **NOT ADOPTED**):

* Train A/B looked mildly positive (fungus −0.0150, melira −0.0034) but **the held-out confirm did not
  confirm**: fungus shrank an order of magnitude to −0.0017, and **melira flipped sign** to +0.0063. Not a
  budget artifact — re-running at 4x budget reproduced the flatness.
* **Cost ~2–6% wall.**
* The mechanism fires hard (Fungus site 10: **0 → 37,682** armings) but **95% of the new continuations are
  resolved GREEDILY** (`empty-default` 35,834, of which `untarget` 25,890 and `overrun` 9,944), because the
  node hosts site 3 (or 3|5|6 under `MTG_BP_NODE_D56`) and **never site 10**. `BpNodeSites()` is hardcoded
  with no env knob. Site 10 *is* in `BpSiteMask` (bit 10, via `BpPutInHandEnabled`) and does get wave-0
  fan-out, so "nothing waves it" overstates it — what it lacks is **node hosting**.

**THE USER'S STANDING RULING ALREADY COVERS THIS CLASS, and it is why site 10's cast-window half is default
ON:** *"same-turn playability of the found card is a correctness requirement (USER 2026-09-06, 'we need to
be able to play it'), not a search lever"* and *"We do need to open the breakpoints regardless"* (USER
2026-09-18), with the codebase's own gloss: **"THE COST IS NOT AN ARGUMENT AGAINST OPENING THEM"**, because
*"leaving the class shut because it is cheaper is the 'narrow the rule until it is free' move this arc has
already had to undo twice."* A line the engine cannot express at any depth or budget is a correctness gap,
and the asymmetry is stated in the design doc itself: *a missing arm is unreachable at any budget and
silent; an unhelpful arm is only cost.*

**One of the design doc's three owed items is now STALE.** It recorded *"Fungus is not in the regression
suite, so neither smoke nor regression covers its play."* **Fungus IS in the suite now** — `[fungus]` in
both `DECK_FILE` and `DECK_PROF`, with live cases at d0/d3/d5 in smoke plus the regression tier, and it is
noted there as *"the suite's heaviest"* case. So the deck with the largest measured hole (100%) now has GT
coverage, which removes the blind spot that owed item named.

## Adoption plan (blocked only on the box)

1. **A/B with the env var, NO rebuild** — `MTG_BP_HAND_ENTRY=1` against current GT on smoke, then
   regression. This is measurable without touching `build/Release`, which matters because a rebuild
   mid-screen makes later jobs run a different binary than earlier ones.
2. **Then flip the default** in `EngineFlags.h` and rebuild.
3. **GT moves broadly** — every digest moved on all five decks the design doc tested, so this is a
   multi-deck rebaseline through the **accept flow**, never a rebaseline over the top.
4. **Commit a d5 twin of this fixture** asserting `expect_opponent_life: -3`. It must land *with* the
   default flip, not before — under the current default it would fail.
5. **Follow-up, separable:** give site 10 **node hosting**. The design doc calls it *"the one change with a
   reason to expect a different answer; everything else is re-rolling the same dice."* That is an
   optimisation of the fix, not the fix, and should not gate the correctness change.

## CLOSED 2026-09-27

Reachable at every searched depth now. Two things closed it: origin's `d4ef1f81` (`MTG_BP_ETB_DIG`,
default ON -- `etb_dig_count` no longer claimed by `ParamKeyedDrawClass`, so site 10's outcome-keyed
arming fires for the dig in both worlds, and the route is named in `PlanOpensBreakpoint` so the
continuation is fanned rather than a canon default) and `7ef0734b` (`MTG_BP_RECORD_VIAL`: a
continuation's Vial put is recorded for the replay -- the executor deployed nothing before). The
Vial-put Contender (no cast window) is covered by `MTG_BP_HAND_ENTRY`, default ON the same day.
Knights s1115 d3 b0 (smoke gi114) wins T4 at d3/d5/d7/d9 at shipped defaults; the searched-depth
fixture `whiteknights_contender_same_turn_deploy_d5.json` asserts `opponent_life: -3` at depth 5.
The Mirrorwing -0.1266 collision this doc warned about was the OLD route (deleting the claim with
no clause); the shipped fix adds the clause, and Mirrorwing's smoke/regression/overnight digests
are unchanged under it (2026-09-27 rebaseline).
