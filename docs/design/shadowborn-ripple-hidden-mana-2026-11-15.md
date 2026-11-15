# Shadowborn: ripple ritual provenance in the mana oracle

The 2026-11-15 preliminary mana-clean block produced five new Shadowborn
`ENUM-GAP` reports: seeds 30080603, 30080812, 30081763, 30082365, and
30084855. Every paying oracle line cast a Dark Ritual under Thrumming Stone.
The ripple revealed and cast another physical Dark Ritual from the library;
that free copy supplied mana for later actions. In several lines Diabolic
Intent then shuffled the library, so comparing its starting and ending order
could no longer identify the hidden copy. The earlier `TapsHiddenCard` check
already handled top-deck ritual copies when the library order survived.

The oracle's apply hook now records physical cards cast by ripple. Its hidden
information check recognizes a recorded ritual copy even if a later tutor
shuffled, while preserving the exception for a visible top card. This is an
oracle classification change; normal play and the mana gate are unchanged.
The five single-game reproductions now each report one `HIDDEN-INFO` and zero
`ENUM-GAP`. Their logs and manifest are in `build/scratch/shadowborn_hidden_five.*`.

The same preliminary block still has Shadowborn `APPLY-DROP` at seeds
30082827, 30083550, and 30083947, plus `APPLY-DROP-ORDER` at 30081583. The
five repros alone are not a clean gate. A fresh 5,000-game block remains due
after the other misses are fixed.

## Activation-before-Culling trial

A default-off trial added a searched plan with the Apostle activation moved
before Culling the Weak when both actions were present. It changed the play
on seed 30081583 before the flagged turn and that run no longer emitted an
`APPLY-DROP-ORDER`, but it did not directly replay the original turn-four
state. On 1,000 fresh paired Shadowborn games (seeds 8,600,001 onward), the
trial changed seven 50-game job digests but **zero win turns** in all 1,000
paired games. Search units rose from 17,221,104 to 17,810,531 (+3.4%) and
summed thread time rose from 895,135 to 957,710 ms (+7.0%). The trial was
removed; the original order issue remains open. Data is in
`build/scratch/shadowborn_actfirst_pilot_*`.
