# Human-play Arcanum Wings swap timing -- status (2026-10-06, session closed at deadline)

USER: *"My recommendation for the viewer is that it should be automatically applied in the attack
phase rather than the 1st main"* (Wings -> Colossification onto a creature that can attack).

## Branches
* `viewer-combat-swap` (pushed): the HUMAN-PLAY-ONLY change, intended to be byte-identical for
  autonomous play. Contents: `TurnSolver::HumanSwapDefersToCombat / DeferHumanAuraSwapToCombat /
  SettleHumanDeferredSwap`, `GameState::scripted_combat_aura_swap_in`, `ApplyCombatAuraSwap` applies
  a human-deferred swap's named Aura without re-asking, menu summary "(in combat, after attacks)" +
  `swap_in_combat` / `in_combat` keys + viewer flash/picker text, `HumanCombatSwapOn`
  (`MTG_HUMAN_COMBAT_SWAP`, `--legacy-main-swap`), reference stamp `"combat_swap_timing": 1` and the
  `recording_rule_args` gate in `test/viewer_protocol_check.py`, scenario option `human_lines`
  (+ 3 fixtures `test/scenarios/bruna_human_wings_swap_*.json`), unit tests "Human swap timing: ...",
  measurement traces `MTG_TRACE=cswapwin` / `swapatkpay`, DECISIONS.md section.
* `viewer-combat-swap-engine-parked` (pushed): the above PLUS two engine-side changes that move
  Bruna GT (see `attackers-tapped-at-declaration.md`).

## Verified on `viewer-combat-swap`
mtg-test 460/460; all 11 Bruna scenarios; Bruna references `--strict` 3 ok; suboptimal claude_s3_gi2
replays T5 with and without `--legacy-main-swap`. On the earlier full-branch smoke/regression, every
moved game reproduced GT with the two engine levers `=0` (attribution, 23 smoke games) -- i.e. the
human-only part did not move anything there.

## LEFT TO DO (in order)
1. On `viewer-combat-swap` rebased onto origin: `./build.sh`, `THREADS=20 bash test/regression.sh
   --smoke` and `--regression` -- must be ALL PASS (byte-identical). Not run on the split tree
   because of the session deadline.
2. `bash test/viewer_checks.sh` (2 pre-existing viewer_client_check failures on origin: 'loop macro',
   'ability tag affordance') and `python3 scripts/audit_viewer_decisions.py --check`.
3. Squash, commit, `git push origin HEAD:phase-1-2-deck-analyzer`, watch CI (Linux + Windows).
4. Engine part (parked): user decision -- see the parked doc; its suite verdicts are recorded there.

## Measurements (Bruna d5 b20, 1200 games, seeds 9,600,000+gi; logs/vcswap/hitrate*)
Search hit rate on the "automatic win" (Wings on an attack-ready host, Colossification in hand,
{2}{U} payable in combat without attackers' mana, host + 20 >= opponent life): **131/131** won that
turn on the current engine; with only non-host creatures staying home 13/13.
