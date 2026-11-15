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

The pooled smoke/regression tier and the fresh PGO mana-clean gate are still
running or owed; append their audit here before landing the change.
