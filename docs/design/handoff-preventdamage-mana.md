# Handoff: Prevent Damage mana-clean gate (wip/mc-pd)

Checkpoint 2026-11-04 08:00 UTC. Branch `wip/mc-pd` in `.claude/worktrees/mc-pd`, rebased onto origin
`23d73e505`, which includes "oracle: model subtype-restricted mana exactly". NOT pushed. Findings and mechanisms:
`docs/design/mana-clean-prevent-damage.md` (read it first).

## Commits
- `827a594da` oracle: the end-of-main-1 sweep's taps are not unscripted payments; the opponent's life is a resource
  of the check (+ unit test `test_prevent_damage.cpp`, "the end-of-main-1 sweep names the lands it tapped").
- (this commit) docs: findings + this handoff.

## What is established
- **Taps outside any payment: deliberate and legal, not a bug.** `TapPainSourcesIfUseful`
  (`src/core/SpellEffects.h:45701`) fires each leftover pain land at the end of main 1 for its Tamanoa gain and
  the Vito / Dina drain. It runs after the last payment, nothing is paid with that mana, and it is applied on the
  engine's line and on every oracle outcome alike. Fix: an observer sink (`g_endm1_sweep_tap_sink`,
  `SpellEffects.h:1824-1829`, called from both sweeps) and its exclusion in `Realize`
  (`src/ai/ManaOracle.cpp:2262-2325`). Games 0-395: UNMODELED 19,193 → 213. The 213 left are all "payment
  enumeration capped".
- **Grand Coliseum:** the same sweep. The oracle already drives its colour at a payment.
- **Opponent life keyed** in the oracle's covers (`ManaOracle.cpp:674`, scoped on at `:3624` / `:3718`, held off
  for the engine payer's `RefinePayment` at `:2004`): zero counts moved on 396 games. It is a guard, and
  DOMINATED-LIFE was not drain-blind.
- **DOMINATED-LIFE (1,132 on games 0-395, unchanged):** the oracle is right, and the engine's pain-useful payer is
  not value-maximising. Two traced mechanisms (doc §DOMINATED-LIFE): (1) the chain shrinks after the payment (a
  sac outlet eats a Tamanoa); the payer pays City (1 pain) instead of Citadel (3) while both Tamanoas are out.
  This is `PainAwarePay` "damaging mode first", `DamageEvents.h:1000`; `DeferredPainPay :951` only models
  growth. (2) A painland's `{C}` face spent on a payment gives up its pain for the turn. Deferral off moves it only
  to 1,075 (diagnostic). The fix is a play change to the payer's assignment policy, which needs a held-out A/B.
  Not attempted.
- **CAPPED (174 / 396 games):** `PayEnum` records supersets with non-contributing picks and every colour of the
  floating unit. In this deck an early land tap is a damage event at a different chain strength (Manabarbs,
  Pyrohemia killing Vito / Dina), so it is not provably redundant. No lossless reduction found (STOP-RULE park).
- **APPLY-DROP 23 / ORDER 12 / ORACLE-GAP 6 (games 0-395):** census depth 1. Suspected cause, not verified tap by
  tap: the useful-mode payer has no rest-of-line check (`line_ok` exists only in harmful mode). Example: seed
  30006387 T4.
- Play identity: oracle-on digests are identical with the opp-life key on and off (396 games). The sink is
  nullptr in play.

## Running jobs
- `buildq.sh` rebuild of the rebased tree (PID ~3226160, log `build/scratch/mc-pd/build3.log`). Leave it running.
  No gate run is in flight.

## Next steps
1. When build3 finishes: run `build/Release/mtg-test -tc="*Prevent Damage*,*mana oracle*"`, then
   `THREADS=8 bash test/regression.sh --smoke`. The change is oracle-only, so expect no moved rows.
2. Gate run on a FRESH block. Copy the primary checkout's untracked `test/mana_clean.json` (it holds PD games
   0-11999) to `build/scratch/mc-pd/state.json`, then run
   `python3 scripts/mana_clean.py run prevent_damage --games 5000 --threads 32 --state build/scratch/mc-pd/state.json --name pass2-prevent_damage`
   from the worktree. It builds PGO first. Expect about DOMINATED-LIFE 14k, CAPPED 2k, UNMODELED 2.7k (capped),
   APPLY-DROP ~300: still "issues".
3. Engine work for the USER to schedule: a value-maximising pain-useful assignment (DOMINATED-LIFE), and a
   useful-mode rest-of-line check (APPLY-DROP). Both are play changes and need held-out A/B.
4. Scratch: `build/scratch/mc-pd/` (`mk.py` manifest maker, `cnt.py` class counter, `a_on/a_off/b_nodefer.*`
   sample outputs, the `tmptrace_payment.diff` engine-payment trace patch).
