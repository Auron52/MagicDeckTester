# Prevent Damage: keep a known Earthquake red pip

## Mechanism

In the fresh mana-clean block 47,000–51,999, all six APPLY-DROPs share seed
30050876 turn 5. The initial plan casts Dina; a continuation then casts Living
Wish and Rolling Earthquake. Dina's payment succeeds. The later Wish payment
uses Tarnished Citadel for {G} and Battlefield Forge for generic mana, leaving
Ancient Tomb untapped but no source for Earthquake's {R}. The oracle instead
uses the Tomb for Wish's generic pip, leaving Forge's {R} and a spare {C} float.
That line survives the extra pain and pays Earthquake.

The old armed payment line check only considers casts named in the plan. The
continuation's Earthquake was already in hand, but absent from that initial
plan. `PreventDamageProvider::PaymentHoldUp` now names one red pip while a
Rolling Earthquake is in hand. `MTG_PD_LATER_HAND_HOLD` (default on, `=0`
control) adds that pip to the armed payer's existing line feasibility check
for a nonred payment. The harmful-pain repair can then retry a source hold,
but accepts only a payable current bill whose damage leaves the player alive
and whose remaining sources cover red. The preference does not force a cast or
remove a plan; if no alternate payment pays, the original payment stands.

## Evidence

The same rebuilt binary on seed 30050876 reports six APPLY-DROPs with the
lever off and zero with it on. A focused unit fixture starts after Dina, with
City of Brass and Reflecting Pool tapped, Wish and Earthquake in hand, and
life eight. The off arm spends Forge on Wish and cannot pay Earthquake; the on
arm uses Tomb, floats its spare {C}, keeps Forge for {R}, and survives. The
full unit suite passed 3,178 cases and 3,601,226 assertions;
`test/check_gt_logs.py` found 1,205 consistent logs, none stale or missing.

The 5,000-game paired held-out A/B used one pooled Release batch and fresh
seeds 7,700,001 onward, with only `MTG_PD_LATER_HAND_HOLD` changed. At the
suite's unwon score of nine, both arms averaged 5.4698 and net turn delta
was zero: four one-turn slowdowns, three one-turn improvements, and one
previously unwon game winning on turn eight. All four slower games recovered
their old turn at depth eight with no time budget. Forty-one play digests
changed. Work units were 154,427,399 off and 154,631,062 on (+0.13%).
The batch manifest, per-game logs, and comparison are under
`build/scratch/pd_handhold_ab/`.

After rebasing onto the Boars integration (`f92bb3b7c`), the rebuilt Release
binary again reports six APPLY-DROPs on seed 30050876 with the lever off and
zero with it on. All 3,304 unit cases and 3,612,554 assertions pass;
`test/check_gt_logs.py` finds 1,231 consistent logs, none stale or missing.
A fresh pooled 5,000-game paired batch used seeds 7,800,001 onward. At the
unwon score of nine, the off arm averaged 5.4614 and the on arm 5.4612, a net
one-turn improvement. Three games were slower by one turn; three were faster,
including one new turn-seven win. All three slower games matched or beat their
old turn at depth eight with no time budget. Fifty play digests changed. Data
and the exact comparison are in `build/scratch/pd_handhold_rebased_ab/`.

The pooled Release smoke/regression tier completed 544 cases plus the scenario
and viewer checks. The strict viewer protocol reports 321 matched, 532 repaired,
one beaten, and zero play drift, board divergence, or enumeration gaps across
854 references. The tier reported 118 mismatches against committed ground truth
across many decks after the Boars integration; they were not accepted. A focused off-control
run on Prevent Damage and its 2HG variant showed that the new hold accounts for
the changed Prevent Damage d0 and 2HG digests. In the depth-five regression
cell, game index 14 changes from a turn-six win to turn seven at the shipped
budget. The on arm recovers turn six at depth eight with zero time budget and
`--ignore-play-profile`. With the hold off, the same cell has the old win turns
but an unrelated digest difference from committed ground truth. The full tier
and control logs are `build/scratch/pd-handhold-rebased-regression.log` and
`build/scratch/pd-handhold-rebased-off-target.log`; the recovery is in
`build/scratch/pd-handhold-regression-gi14-d8.log`.

The first fresh PGO mana-clean gate on game indices 52,000–56,999 reports 532
issues: 201 APPLY-DROP, 121 ORACLE-GAP, 18 UNMODELED, and 192 CAPPED. Oracle-on
and oracle-off play are byte-identical. Of the 201 drops, 197 have a named
continuation already in the plan; the broad later-hand hold reserves red for an
Earthquake in hand even while paying a multi-cast plan that needs that source.
The same-seed off control clears the representative seed 30052809 T4 drops.

The follow-up `MTG_PD_NAMED_LINE_PRIORITY` (default on, `=0` reproduces the
broad hold) lets a named plan with at least two mana casts take priority over
that speculative Earthquake hold. Focused oracle replays clear the new
30052809 T4 and 30054696 T5 drop clusters while preserving zero drops on the
original 30050876 T5 repro. The targeted Earthquake unit passes. A fresh
same-binary pooled A/B on 5,000 paired games (seeds 8,000,001 onward) compared
broad hold (`=0`) with named priority (`=1`). Both arms averaged 5.4604 win
turns: zero games faster, zero slower, seven play digests changed. Work units
were 157,091,885 broad and 157,113,275 narrow (+0.014%). The manifest, game
logs, and exact comparison are in `build/scratch/pd_named_priority_ab/`. The
current-binary full unit suite passed 3,304 cases and 3,612,554 assertions.
The next fresh PGO gate is still in progress.
