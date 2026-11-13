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

## Follow-up fixes (2026-11-11, after re-checking the other player's reports)

A replay of every original report found 23739, 23743 and the Cyclone decline text fixed, but 23742 (both Wish
suggestions) and Cyclone 23746 (no Remand on a free-cast Ulamog) still broken. Fixed in the follow-up commit:
the two Living Wish amendments (`MTG_AA_WISH_COMBO_FIRST`, `MTG_AA_WISH_UNTAP_NET`; ledger
`analysis-Accomplished Alchemist.md`, "Other player's reports on seed 23742"), and human play now skips
`CycloneProvider::AdmitRemandOnFreeCast` (a search prune), so the viewer offers Remand on the Ulamog
(`analysis-Unpredictable Cyclone.md`, Remand rulings). Replayed on the seeds: T2 suggests Brindle Boar, T5 Galvanic
Alchemist, and 23746 offers Remand on Ulamog, the Infinite Gyre (it returns to hand, Remand draws).

## CLOSED (2026-11-12): Living Wish land rules + ALCH_FIRST_AT2 ON + viewer "searched" marks

PUSHED 2026-11-12 ~10:00 UTC as `828c1a45e` (AT2 ON + LAND_FIRST_AT3 + ONE_LAND + STRANDED_ELF + viewer marks) and
`80d17fc85` (LAND_FIRST_AT2), after two rebases onto origin; each rebase was rebuilt and the alchemist smoke +
regression cells reran byte-identical to the accepted GT (`check_gt_logs.py` 1108 consistent). Backup branch deleted.
Nothing open for the USER on this thread. The alchemist OVERNIGHT GT moves with these rules and is left to the
periodic overnight run.

Committed and pushed: `cd0368564`, the AT2 / SPARE levers DEFAULT OFF, measured (ledger
`analysis-Accomplished Alchemist.md`, "Two follow-up Wish levers"). Rebased onto origin `01a3adbc1` (clean).
UNCOMMITTED in the primary checkout (all USER-requested, all DEFAULT ON):

* `MTG_AA_WISH_ALCH_FIRST_AT2` flipped ON. USER: "Let's enable 1, either way." SPARE_MANA_FIRST stays off (neutral).
* `AccomplishedAlchemistProvider::TutorMarksSuggested` (Living Wish + Elvish Harbinger) and `tools/play/index.html`
  tutor panel: "searched" badge on the narrowed set, the AI pick reads "recommended", a one-line explanation. USER:
  "show which options are searched and the top one as the recommendation. That way the user knows that, even if their
  preferred option is not "recommended", that it is picked up by the search." Verified on seed 23742 T2 (Boar
  recommended, Boar + Arbor Elf searched); `test/viewer_client_check.js` passes (new testTutorSearchedMarks).
* `MTG_AA_WISH_LAND_FIRST_AT3`: at 3 projected mana the land leads Arbor Elf unless the wished land cannot be played
  this turn or next (hand lands >= drops left this turn + 1). USER, in order: "Frankly when at 3 mana the land seems
  often like it would be better." / "If you have a land in hand the elf is probably better." / "If you are out of
  lands the land is almost certainly better." / "Because you need to have 4 mana as soon as possible for the
  Alchemist." / "A wished land beats the elf handily when it can be dropped for free this turn or next turn. The elf
  needs to drop this turn to match (to match the land played next turn)." / "The only time the elf wins is when the
  land cannot be played for 2 turns. Then the elf is pretty clearly better."
* `MTG_AA_WISH_ONE_LAND`: only one of Forest / Island is offered: the Forest until two Forests are held (play + hand),
  then the Island. USER: "I think we should choose only one of Forest or Island, but why would you want to drop both?
  That seems dicey unless we know we will play the elf this turn." / "We should only recommend one land, also?" / "We
  need at least 2 forests the way I see it. After that you can freely pick the Island if you want."
* `MTG_AA_WISH_STRANDED_ELF`: the 2026-10-20 "the elf preferred" rule (Aura'd Forest -> Elf only, no land) applies
  only when the Elf is castable THIS turn (an untapped green source, or a green land in hand with a drop left); else
  one land comes back, ordered by LAND_FIRST_AT3. USER: "we need that fix, because an elf stranded in hand is pretty
  bad. I would rather a land if that can get us to 4 mana next turn."
* Unit expectations in `test/unit/test_accomplished_alchemist.cpp` updated for all of the above (NOT yet run against
  the final two-Forests rule).

Running: background `bash logs/aa_alch2/land_ab2.sh 2785154`. It waits on PID 2785154 (the AT2-only GT chain:
smoke done -> `build/scratch/aa_land/smoke_A`, regression in progress -> copied to `regression_A`), then builds
(`logs/aa_alch2/build4.log`), runs the AA units (`units4.log`), then smoke B, regression B (the land rules ON, the
LAST runs of those tiers, so `--accept` promotes them), overnight A (`MTG_AA_WISH_LAND_FIRST_AT3=0
MTG_AA_WISH_ONE_LAND=0 MTG_AA_WISH_STRANDED_ELF=0`) and overnight B. Logs `logs/aa_alch2/land_<mode>_<arm>.log`;
wins `build/scratch/aa_land/<mode>_<arm>/`; DONE marker `logs/aa_alch2/LAND_DONE`. Session cron 5a74f861 checks it
every 2 h. NOTE: smoke_A/regression_A were run BEFORE the rebase? No -- after it (binary rebuilt at 05:1x UTC on
`01a3adbc1` + AT2 ON + marks); the AT2 smoke reproduces the 11-11 A/B arm 10 digests exactly.

Progress 05:50 UTC: units4 flagged 3 Wish cases, all fixture/expectation (fixed, 122/122 pass, uncommitted):
nothing-held and Elf-fetched now expect ONE land; `hl3` needed `lands_played_this_turn = 0` (FreshAa spends the
drop, so it was the `used` case twice). `ExpectWishAa` now logs the list it got. Smoke B vs A, paired: searched
n=300, 0 slower / 5 faster (-0.0167/game); d0 -2 turns.

DONE 06:20 UTC: overnight held-out searched n=2535 net -26 (2 slower / 24 faster); both slower games recover at
b100/b400. Smoke + regression GT accepted, full units 2842/2842, ledger entry in `analysis-Accomplished Alchemist.md`
("The Living Wish land rules (2026-11-12)"). Committed locally; shared-branch push held to >= 09:26 UTC (4-hour
rule), backup branch pushed meanwhile. Remaining: push + CI watch; open USER question below.
Then MTG_AA_WISH_LAND_FIRST_AT2 (USER 2026-11-12: land before the Elf at 2 mana unless a land Aura is held): A/B vs
6e958bb6a searched 0 slower / 1 faster held-out, d0 strongly better; smoke + regression GT accepted; committed on top.
The open question below is ANSWERED. All six of the other player's reports re-verified on this binary (11-12).

(Old) Next: `python3 logs/aa_alch2/compare_land.py` (A = AT2 only, B = + land rules; paired, loss = 9; weigh overnight). Accept alchemist smoke (`--deck=alchemist,alchemist2hg`) + regression
(`--deck=alchemist`) GT with a note naming AT2 + the three land rules; `python3 test/check_gt_logs.py`; ledger entry
in `analysis-Accomplished Alchemist.md`; commit, `git pull --rebase`, push, watch CI in the background. Report the
numbers to the USER. Open USER question: should LAND_FIRST apply at 2 mana too (now 3 only)?
Lessons this round: never `pgrep -f` / `pkill -f` with a pattern that appears in the waiter's own command line (it
hung a waiter overnight on 11-11 and killed a shell on 11-12); wait on a PID with `kill -0`.

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
