# "Cast nothing more" is never scored after a deferred breakpoint site (found 2026-10-13)

Status: ROOT-CAUSED, prototype measured on a small sample, NOT on the branch. Needs the per-deck held-out
A/B before adoption. Found while root-causing the integration's final A/B slower game Hinata shipped gi 137
(docs/design/final-ab-2026-10-13.md).

## The game

Hinata2, shipped settings (value_play depth 5, `--budget-ms 20`), seed 6320138, game index 137. The tip wins
T8; with `MTG_RESTRICTOR_ORDER_EXACT=0` (and on the committed tree) T7; T8 at `--depth 8 --budget-ms 0` too.

## What the restrictor does here (correctly)

Two rejects matter, both at the enumeration site, T7 main 2, at Soulfire Eruption's staging breakpoint, in
the search's projection (the T3 search at shipped b20; the depth-7 pass of the T1 search at d8 b0):
`{Irencrag Feat + Magma Opus}` and `{Soulfire Eruption (2nd copy) + Irencrag Feat}`, both failing both tests.
After Soulfire the executor's tap trace leaves Reflecting Pool, Forbidden Orchard and two Islands: four mana,
at most two red. Irencrag costs {1}{R}{R}{R} and nothing in either subset makes mana before it; the =0 arm
admitted them only by letting Irencrag's own seven red pay for Irencrag (defect 1 of the d0-runner stall in
reality-spasm-crackle-classification.md). In the real game Irencrag was castable only after Reality Spasm
untapped the lands.

## How the committed tree won T7 anyway, and why no budget recovers it on the tip

At that T7 node -- the deepest turn of the pass, a turn the breakpoint node does not host, its site 3 kept out
of wave 0 by `MTG_BP_SITE3_DEFER` -- the {Soulfire} plan's continuation is `MTG_BP_BASE_CANON`'s single pick,
the FIRST entry of the continuation list.

* =0 arm: that entry was the phantom `{Irencrag + Magma Opus}`. Applying it cast nothing (payment fails), so all
  four sources stayed untapped, and the main-2 fixpoint re-searched and found Ponder -> Preordain -> {Reality
  Spasm, Irencrag, Crackle}. The executor played exactly that line ("Soulfire", then
  "Ponder + Magma Opus | bp[Preordain] bp_choice=2@0", every cast paid, the phantom never attempted).
* Tip: the first entry is now `{Magma Opus}`, a real cast that spends all four mana; the fixpoint has nothing
  left, T7 is dead ("Soulfire | bp[k0:Magma Opus]").

Nothing else ever SCORES "cast nothing more after Soulfire": wave 0 adds that option only for the sites it
covers; both wave walkers apply the past-the-end (EMPTY) rank but `continue` without scoring it (their comment
says it is a copy of the base plan -- no longer true since BASE_CANON shipped); the main-2 walker lacks the
fixpoint step. In the depth-8 pass T7 takes the same pick and the T8 kill is found first, ending the search.
With `MTG_BP_BASE_CANON=0` the tip wins T7 at shipped b20 and at d8 b0. This breaks BASE_CANON's own condition
(legitimate only while the alternatives stay reachable) and the USER's 2026-09-30 rule that "done with this
phase" is always a scored option.

## Prototype: `MTG_BP_EMPTY_ARM_DEFERRED`

In `AppendBreakpointVariants`: for every base plan that opens a breakpoint in the full site mask and that wave 0
did not already give an EMPTY arm, emit the EMPTY arm (`bp_choice = kBpEmptyChoice`) at every breakpoint depth.
gi 137 -> T7 at shipped b20 and d8 b0 (restrictor on or off). Paired, Hinata2, 100 games per arm per cell (shipped
b20 seeds 9100001+, d3 b10 9200001+): win turns identical in all 200 games across tip / tip + prototype /
restrictor =0; play digests differ in every chunk; work units +2.6% (shipped) and +3.5% (d3).

The patch (prototype + the investigation's diagnostics `MTG_RESTRICTOR_SPARE`, `MTG_RESTRICTOR_TRACE2`,
`MTG_BP_REACH_TRACE`, extra `MTG_BP_CONT_TRACE` / `MTG_M2T_TRACE` fields) was saved at
logs/discard_impl/hinata137/investigation_only.patch (gitignored scratch); the prototype hunk is the
AppendBreakpointVariants one. Its heurarm slot must be appended after `ATTACK_FREECAST_DAMAGE`.

## To adopt

It is a search-structure change that closes a gap at unlimited budget (not a heuristic), so the shape is right,
but it touches every deck with breakpoint plans. Owed: a movers check at defaults, then the held-out per-deck
A/B on every moving deck (bar: net <= 0, every slower game recovers), then smoke / regression GT. Alternative
shapes worth comparing: score the walker's past-the-end EMPTY rank instead of skipping it, and give the main-2
walker the fixpoint step.

Also surfaced, not touched (USER-owned): under the shipped generic Hinata order (`MTG_HINATA_ORDER_FULL` off)
Ponder, Preordain, Magma Opus and Soulfire all rank after Irencrag, so `{Irencrag + Ponder}` counts Ponder as
Irencrag's one follower.
