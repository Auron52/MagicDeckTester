# Mana-clean gate: Prevent Damage (pass 1 → pass 2)

Status 2026-11-04, branch `wip/mc-pd` (not pushed). Pass 1: `logs/mana_clean/pass1-prevent_damage-r2/report.md`
(src `644270cdc4ce`, games 0-11999, 617,563 issues).

## Fixed (oracle only; play byte-identical)

**UNMODELED "engine tapped a mana source outside any payment" (574,601 of 579,440).** Every one was the
end-of-main-1 pain sweep, `TapPainSourcesIfUseful` (`src/core/SpellEffects.h`), which fires each leftover pain
land for its Tamanoa gain (and Manabarbs hit) after the plan's last payment. It is deliberate and legal: the deck
turns its own damage into lifegain (Tamanoa, doubled by Rhox Faithmender) and the lifegain into opponent life loss
(Vito, Dina). It is not a payment (the mana empties with the phase, so no action can be dropped by it), it is a
deterministic function of the board the payments leave, and it runs on the engine's line and on every oracle
outcome alike, so the oracle's end-board comparisons already include it. Both end-of-main sweeps (also the Grove
drip, `TapDripLandsIfUseful`) now report their taps to an observer sink (`g_endm1_sweep_tap_sink`, keyed to the
oracle's copy, nullptr in play) and `Realize` (`src/ai/ManaOracle.cpp`) no longer counts them as unscripted.
Grand Coliseum's lines were the same sweep: the oracle already drives its colour choice at a payment (every useful
face is in the table). Games 0-395: UNMODELED 19,193 → 213.

**Opponent life as a resource of the check.** `Resources` / `CoversBoards` now key the opponent's life (lower is
better), but only inside `AuditDecision` / `AuditLine`. The engine's shared payer (`RefinePayment`) keeps it off,
so play cannot move. Measured: no count moved on games 0-395. The pass-1 DOMINATED-LIFE lines do not depend on
ignoring the drain, so this is a guard, not a fix.

## Residual (engine), not fixed in this pass

Class counts on games 0-395 after the fix (pass 1 on the same games in brackets): DOMINATED-LIFE 1,132 (1,132),
UNMODELED 213 (19,193), CAPPED 174 (174), APPLY-DROP 23 (23), APPLY-DROP-ORDER 12 (12), ORACLE-GAP 6 (0;
these six were hidden behind the sweep's UNMODELED before).

### DOMINATED-LIFE: the oracle is right, and the engine's pain-useful payer is not value-maximising

The deck wants damage, so the question was which side is wrong. The oracle compares END boards after the sweep:
own life, opponent life (now), untapped permanents. A flagged line has more of our life, no more opponent life
and the same or more untapped. On those terms the engine's line really is worse. Two traced mechanisms:

1. **The chain shrinks after the payment.** Seed 30000020 T6, `{sacoutlet: Dina}` at life 8, two Tamanoas.
   The engine pays Dina's `{1}` with City of Brass (1 pain, +1 net), and the sweep fires Tarnished Citadel after
   Dina has sacrificed a Tamanoa. The oracle pays with Citadel `{W}` (3 pain) while both Tamanoas are still out
   (+3 net), so it ends with +2 life and the same opponent life. The useful mode in `dmgev::PainAwarePay`
   (`src/core/DamageEvents.h`) means "damaging mode first", not "the most damage while the chain is strongest".
   The deferral (`DeferredPainPay`) only models casts that GROW the chain. 286 of 1,132 flags on games 0-395
   are sac-outlet plans.
2. **Painless faces spent on a payment.** Seed 30000004 T6, `{land=Ancient Tomb + cast: Vito}`. The engine pays
   `{2}{B}` with Grand Coliseum `{B}`, Tarnished Citadel `{C}` and Battlefield Forge `{C}` (one point of pain).
   A land tapped for `{C}` gives up its pain for the turn, because the sweep cannot tap it again. The oracle pays
   with Coliseum, Ancient Tomb and Forge `{W}` and keeps Citadel for the sweep after Vito. It ends with +1 life
   and no less drain.

Pain deferral off (`MTG_PD_PAIN_DEFER=0`, diagnostic only) moves DOMINATED-LIFE only from 1,132 to 1,075, so the
deferral is not the main cause. A fix belongs in the payer's mode and assignment policy, measured as a play
change: for example, choose among the pain-safe assignments the one with the greatest realised chain value,
counting the sweep. That needs a held-out A/B and is the USER's call to schedule.

### CAPPED / "payment enumeration capped": supersets that are distinct lines here

Seed 30000031 T7 `{act: Pyrohemia x7}` and seed 30000048 T8 `{cast: Living Wish}` (Manabarbs x2 out). `PayEnum`
records tap sets with picks that pay no pip. For example, for `{R}` it records Grand Coliseum `{W}` + Tarnished
Citadel `{R}`, and each floating unit's colour is a separate option. In most decks a superset only taps early what
could be tapped later. In this deck an early land tap is a damage event at a different chain strength (Manabarbs,
painlands, and Pyrohemia killing Vito / Dina mid-plan), so the superset is a different line. Pruning it is not
provably lossless. No lossless reduction found. Raising `MTG_MANA_ORACLE_NODES` adds budget but removes no work.

### APPLY-DROP (538) / APPLY-DROP-ORDER (183)

The rollback census puts them at depth 1 (511 of 538): the payment before the failing one had a way. Example:
seed 30006387 T4 `{Ancient Tomb, Dina, Rolling Earthquake X=2}`, Tamanoa out, life 16. The engine's Dina payment
leaves no `{R}` for the Quake, while City `{B}` + Brushland `{G}` for Dina and Karplusan `{R}` + Tomb for the Quake
pays both. Likely mechanism (NOT verified tap by tap): the useful-mode attempt in `PainAwarePay`
(`PaymentPainSafe` branch) is the plain pain-first attempt. Only the harmful mode carries the exact rest-of-line
check (`line_ok`, `MTG_PD_LINE_OK_EXACT`). Next step: trace the engine's payments on the oracle's copy (a temporary
`[tmp-pay]` print around `TapForCostShared` for `EngineApply`'s state was used for the traces above; it is not
committed).

## Reproduce

`MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=<T> build/Release/mtg 'decks/Prevent Damage/Prevent Damage.cod' --profile
'decks/Prevent Damage/Prevent Damage.profile.json' --games 1 --seed <S> --game-index <S-30000000>`
