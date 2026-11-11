# Handoff: Accomplished Alchemist and Cyclone play viewer reports

Updated 2026-11-11 (CLOSED). Branch `phase-1-2-deck-analyzer`. The fixes were committed as `197c318a7` and merged at `81cf4e7ca`. That merge commit broke the repo's linear-history rule, so it was replaced, before any push, by the single commit `7f2fe3f94` with the identical tree on top of `origin` `c79006605`. The merged-tree gates below then ran and their GT was accepted. Preserve the existing untracked `o1.txt`, `r1.txt`, `r2.txt`, `r2.txt.err`, and `r8_*.txt` files.

## Merged-tree closeout (2026-11-11)

- Alchemist-only smoke/regression on the merged tree matched the accepted GT byte for byte, and strict reference replay passed (807 refs, 0 drift): `logs/other_user_merged_{smoke,regression}.log`.
- The **full** tiers were run next, because `TapDeclaredAttackersBeforePump` is shared combat code that runs for any deck with a firebreathing or team-pump source, not only Alchemist.
  - Smoke: 184/187 passed and 456 scenarios passed, with 0 slower and 0 faster: `logs/other_user_full_smoke.log`.
  - Regression: 289/295 passed, with searched 0 slower/0 faster. Strict references passed again: `logs/other_user_full_regression.log`.
- Attribution was done by rerunning the moved decks with all three AA flags `=0` (`logs/other_user_{smoke,reg}_flagsoff3.log`).
  - **This fix** moved FiveColour smoke d0, regression d0 s2002, and regression d5 s3003 gi168. The only slower game is regression d0 gi932, T5 to T6. Its old T5 line used attacking Birds, Faeburrow Elder, and Deathrite Shaman to pay for Shivan Dragon's `{R}` pumps (21 damage). The best legal T5 attack is 19 damage, so the old win was illegal (`logs/other_user_gi932/`).
  - **Inherited from the Gods engine merge `f90930c9f`**, and unchanged with the flags off: Bruna2HG smoke d5 gi10, Landfall smoke d5 gi83/gi98, Gods regression d3 gi39, FiveColour regression d3/d5 s2002 gi42, and Landfall regression d5 s3003 gi43. All are same-score line changes; most are shuffle or fetch divergences.
- Focused unit tests were rerun on the merged `mtg-test`: Alchemist 119/119 and Cyclone 120/120 (`logs/other_user_merged_{aa,cyclone}_unit.log`).
- All of these were accepted with `--deck` filters and attribution notes in `test/regression_gt.txt`, and `check_gt_logs.py` is consistent. The overnight tier was left for its periodic pooled run.

## Changes made

- `src/core/SpellEffects.h`, `src/ai/AIEngine.cpp`, `src/ai/TurnSolver.cpp`: attackers now tap before in-combat Ezuri pump mana is paid, in both real combat and rollout. This fixes seed 23739's illegal use of attacking mana creatures. Default-on diagnostic `MTG_ATTACK_TAP_BEFORE_PUMP`; `=0` reproduces old behavior.
- `src/core/SpellEffects.h`: Accomplished Alchemist is preferred as Crab Umbra's host when the combo is available. This fixes seed 23743. Default-on diagnostic `MTG_AA_UMBRA_HOST`; `=0` reproduces old behavior.
- `src/ai/DecisionProviders.cpp`: Living Wish at projected 3 mana ranks the missing Alchemist ahead of Arbor Elf and mana lands when an untapper and win condition are held. Elf and lands remain legal candidates. This fixes the reported seed 23742 choice without losing a searched T6 line at regression seed 3054/gi51. Default-on diagnostic `MTG_AA_WISH_ALCH_FIRST`; `=0` reproduces old ordering.
- `tools/play/index.html`, `test/viewer_client_check.js`: declined Unpredictable Cyclone hit prompt says the card goes to library bottom in random order with the other exiled cards, rather than to hand.
- Cyclone seed 23746's inability to Remand a *cycled card* is correct: cycling is an activated ability. A spell cast through Cyclone can be Remanded. No exact blocked cast was reproducible from the seed alone.
- Four smoke and six regression ground-truth cells, with provenance, were accepted in `test/regression_gt.txt`, `test/gt_logs/`, and `test/job_cost.tsv` before the shared-branch merge.

## Evidence and running job

- Latest release `mtg` build passed: `logs/other_user_refined_build_mtg.log`.
- Full viewer client check passed: `logs/other_user_latest_viewer_check.log`.
- Latest refined smoke passed 456/456 scenarios, with all four win-turn means unchanged from the just-accepted smoke GT: `logs/other_user_refined_smoke.log`, `logs/other_user_refined_smoke_accept.log`.
- After the Wish refinement, focused Alchemist tests passed 119 cases/15,435 assertions and Cyclone tests passed 120 cases/9,121 assertions. They ran before the newer shared-branch merge.
- The all-flags-off baseline matched the prior six regression GT files exactly. The refined official regression completed at d0 6.3310, d3 seed 2002 4.8800, d3 seed 30211 5.0000, d5 seed 2002 4.9714, d5 seed 3003 5.0914, d5 seed 30211 5.0000; 456/456 scenarios passed. The 5.0914 improved on pre-refinement 5.1029. The audit and acceptance are in `logs/other_user_refined_regression.log`, `logs/other_user_refined_explain_all.txt`, and `logs/other_user_refined_regression_accept.log`.
- The combat legality fix accounts for most searched mean regressions. Pre-refinement enabled vs old GT: d0 6.3310 vs 6.3470; d3 4.880 vs 4.860; d5 seed 2002 4.9714 vs 4.9429; d5 seed 3003 5.1029 vs 5.0171. With combat flag off but other fixes on: 6.212, 4.840, 4.9257, 5.0286, respectively. Per-game audit: `logs/other_user_regression_audit.txt`.

## Resume sequence

1. Let the merged-tree release `mtg` build finish (`logs/other_user_merged_build_mtg.log`). Re-run targeted smoke/regression against accepted GT. The prior per-game audit used the verified all-flags-off baseline wrapper `/tmp/mtg_aa_baseline_off`; all 17 searched slowdowns were caused by attacker tapping. If merged-tree results move, audit before another acceptance.
2. Rebuild latest `mtg-test` if merged source changes require it; rerun focused Alchemist and Cyclone tests. Finish `node test/viewer_client_check.js` (running at this update) and strict reference replay. Run `python3 scripts/check_references_safe.py`, `git diff --check`, and inspect `git status`.
3. Commit this handoff note and any final accepted GT, fetch/sync latest origin, then push. The user authorized pushing completed work. Do not include the unrelated untracked text files.

Read root `CLAUDE.md` and the applicable `.claude/skills/` documents before resuming. The user requested updates as individual issues are fixed or found unreproducible.
