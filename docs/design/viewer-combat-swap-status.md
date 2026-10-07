# Human-play Arcanum Wings swap timing + CR 508.1f -- status (BOTH LANDED 2026-10-07)

USER: *"My recommendation for the viewer is that it should be automatically applied in the attack
phase rather than the 1st main"* (Wings -> Colossification onto a creature that can attack), and on the
engine half: *"We should allocate the creatures beforehand to be the sources of mana for the swap and
not attack with them."*

## What landed
1. **Human-play swap timing** (`56ea019a`, human play only, byte-identical for autonomous play):
   `TurnSolver::HumanSwapDefersToCombat / DeferHumanAuraSwapToCombat / SettleHumanDeferredSwap`,
   `GameState::scripted_combat_aura_swap_in`, `ApplyCombatAuraSwap` applies a human-deferred swap's
   named Aura without re-asking, menu summary "(in combat, after attacks)" + `swap_in_combat` /
   `in_combat` keys + viewer flash/picker text, `HumanCombatSwapOn` (`MTG_HUMAN_COMBAT_SWAP`,
   `--legacy-main-swap`), reference stamp `"combat_swap_timing": 1` and the `recording_rule_args`
   gate in `test/viewer_protocol_check.py`, scenario option `human_lines` (+ 3 fixtures), unit tests
   "Human swap timing: ...", measurement traces `MTG_TRACE=cswapwin` / `swapatkpay`.
2. **Engine half** (the former `viewer-combat-swap-engine-parked`): CR 508.1f -- `ApplyCombatAuraSwap`
   taps the non-vigilant attackers before it probes or pays (`MTG_COMBAT_SWAP_TAPPED_ATTACKERS`), and
   the combat-swap attack hold (`HoldForCombatAuraSwap`, `MTG_COMBAT_SWAP_ATTACK_HOLD`). Details and
   measurements: `docs/design/attackers-tapped-at-declaration.md`, `docs/design/analysis-Bruna.md`.

## Verification
* Swap timing on the rebased tree: mtg-test 474/474, scenarios 152/152, smoke 114/114 + regression
  160/160 byte-identical, viewer protocol `--strict` 506 refs 0 play-drift / 0 board-diverged / 0
  enum-gap, `viewer_client_check.js` + `viewer_checks.sh --line-only` pass, `audit_viewer_decisions.py`
  on Bruna: 5h PASS.
* Engine half: mtg-test 476/476, scenarios 152/152, smoke 114/114 byte-identical; regression and
  overnight move only through the known batch run-to-run nondeterminism (Selesnya / one Stompy game:
  each moved game replays identically on the pre- and post-change binaries in isolation); viewer
  protocol `--strict` 506 refs 0 drift; the 3 Bruna references replay ok.
  `references/suboptimal/Bruna/claude_s3_gi2` (outside the gate): `--legacy-main-swap` (its unstamped
  default) replays the recorded T5; under the new rule the same picks win T4.
* Bruna is out of the suite (a6ee9c7e, 3x cost rule), so its movement was measured as a paired A/B
  over every former Bruna suite row -- see `analysis-Bruna.md`.

## Measurements (Bruna d5 b20, 1200 games, seeds 9,600,000+gi)
Search hit rate on the "automatic win" (Wings on an attack-ready host, Colossification in hand,
{2}{U} payable in combat without attackers' mana, host + 20 >= opponent life): **131/131** won that
turn before the engine half; **158/158** (+ 18/18 where only non-host creatures stay home) after it.
