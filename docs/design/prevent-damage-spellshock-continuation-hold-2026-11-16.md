# Prevent Damage: keep red for a visible Spellshock after Dina

The fresh PGO mana-clean gate on games 82,000–86,999 found three copies of one
`APPLY-DROP` in seed 30,086,899, turn six. The menu plan casts Vito and reaches a
breakpoint continuation that casts Dina before Spellshock. The engine paid
Dina's `{B}{G}` with a red-producing source and then dropped Spellshock; the
oracle found a legal payment using Brushland for `{G}` that left Karplusan
Forest's `{R}` for Spellshock. A one-payment rollback also found that route,
but the dropped cast was not in the original plan, so the existing rollback
rule could not recover it.

`MTG_PD_SPELLSHOCK_HOLD` is default on (`=0` restores the old rule). When
Spellshock is visible in hand and no Rolling Earthquake hold already applies,
the Prevent Damage provider requests one later red pip. The pain-aware payer
uses that request only while paying Dina's exact `{B}{G}` bill. It prefers an
assignment leaving red available when the board can pay both bills; otherwise
it uses the prior assignment. This is a payment preference, with no new
searched action or library information.

The focused Release replay of seed 30,086,899 clears all three drops. A broad
first trial held red on every nonred payment and performed worse on 5,000
paired games (seeds 10,500,001–10,505,000): one game faster, two slower,
23 changed play digests, and +3.5% recorded game time. The Dina-only form
clears the same focused drop and is byte-identical to the baseline on all
5,000 paired games, including every play digest and win turn. Work units were
152,460,183 baseline and 152,460,250 narrowed (+0.00004%); recorded game
milliseconds were 5,961,156 and 5,843,079 (a noisy -2.0%). Raw manifests,
per-game records, and runs are under `build/scratch/pd_spellshock_ab/`.

The integrated Release binary passed all 3,355 unit cases and 3,614,078
assertions. The deck-scoped combined smoke/regression run passed strict viewer
replay across 864 references with zero play drift, board divergence, or
enumeration gaps; both require-beat references passed. The harness exit was
one because three rows differ from committed ground truth (two depth-zero
digest-only rows and one searched game one turn slower). A same-binary off
control reproduced every one of the nine deck-row `.wins` files byte for byte,
including that searched slowdown. These ground-truth differences are not
caused by this hold; no ground truth was accepted. Logs are under
`build/scratch/pd_spellshock_ab/`.

A fresh PGO mana-clean gate is still owed on games 87,000–91,999.
