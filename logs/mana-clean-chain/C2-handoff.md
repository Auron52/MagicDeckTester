# Queue item C2 handoff (2026-11-14) -- pirates residual Bolt APPLY-DROPs

## Diagnosis
Both shapes were INPUT bugs in item C's `CcoHandDemandScan` (src/ai/ManaPayment.cpp), so the MTG_CCO_HAND_DEMAND rung
(the USER's "the inflexible spell needs to be considered early") never fired:
1. T3 Crewmate-vial continuation (s30064129 / s30067717 / s30064439): the continuation casts the dug Daring Buccaneer
   {R} (reveal paid). The scan excludes the card being paid by COST IDENTITY; the Bolt {R} sat before the Buccaneer in
   hand, so the Bolt was skipped as "itself", no noncreature demand was seen, Spirebluff Canal paid the Buccaneer
   and the Bolt was left with only Secluded Courtyard (creature-only).
2. T4 Forerunner + Bolt (s30066580, 27 flags): the scan's castability cap counted lands/rocks/dorks but not a
   spendable Treasure, so castable = 0, the Bolt was not live, Treasure paid {B}, Mountain + Canal the generic.
No cast-order change was needed (PiratesProvider::CastOrderRank untouched; no USER DECISION raised).

## Done (LOCAL commits, not pushed)
- 4dfa91f10 MTG_CCO_HAND_SCAN (heurarm slot CCO_HAND_SCAN, default ON, =0 -> historical scan): self-skip only among
  hand cards of the payment's own kind; PaySacSpendableNow one-shots count as supply. Ordering only.
  Units +2 cases in test/unit/test_hand_demand_pay.cpp (lever on/off); 3070/3070. Repros: 47 APPLY-DROP -> 0 (on),
  reproduced (off).
- aa77361ea SMOKE GT: only pirates_smoke_d0_s1001 moved (digest only, same avg; lever=0 reproduces 139a655bda2320bc).
  Regression 322/322 unchanged (nothing to accept). check_gt_logs clean (1170). Scenarios 530/530.
- Held-out A/B seed 89400000, lever off vs on, 32,000 games (pirates + pirates2hg 3000/arm; dragonstorm, elementals,
  goblins, knights, merfolk, minotaur, slivers, soldiers, spirits, vampires 1000/arm -- every deck holding a
  colored_creature_only land): ZERO changed outcomes, every net 0, searched slower 0; only pirates2hg's digest moved.
  logs/mana-clean-chain/itemC2/ab_report.txt, ab_jobs.txt.
- PGO 33405742710d; gate pirates 69000-73999 -- see the checkpoint bullet for the result.

## USER rulings (verbatim)
- Pirates (2026-11-13): "What does our Pirate order look like? Maybe it would be simple to just adjust the order to
  admit Lightning Bolt earlier or possibly the better solution would be to just ensure that in mixed lines with a
  creature-only source we figure out the bolt payment earlier. In those situations the inflexible spell needs to be
  considered early, not late as seems to be happening now."

## Round 2 (context cap handoff, ~05:50Z)
- Gate pirates-C2-20261114 (69000-73999): 39 APPLY-DROP, all Bolt, 4 games (logs/mana_clean/pirates-C2-20261114/report.md).
- 54b16a9b8 MTG_CCO_HAND_SCAN part 3: the creature-colour keep counts the OTHER untapped creature-only lands
  (cco_sup / cco_def from the scan; use site subtracts the candidate's own share). Repros s30069061 T3, s30073153 T3,
  s30069077 T4 -> clean on, flags off; the 4 round-1 repros stay clean. Units 3071/3071.
- Round 2 smoke: 203/203 PASS, nothing moved (no GT to accept).
- RUNNING: chain PID 2425317, script build/scratch/c2/chain2.sh: `regression.sh --regression` -> build/scratch/c2/regr2.log,
  then the A/B (build/scratch/c2/ab2/ab.json, binary ab2/mtg.ab = 54b16a9b8 Release, seed 89500000, old =
  MTG_CCO_HAND_SCAN false) -> ab2/ab.out/.err, report ab2/report.txt; DONE marker build/scratch/c2/chain2.DONE.
  Wait with `until [ -f build/scratch/c2/chain2.DONE ]`.

## Remaining, in order
1. regr2.log: if any row FAILs, attribute it (lever=0 reproduces? searched slower must recover at d8 b0), then
   `bash test/regression.sh --accept` + commit; `python3 test/check_gt_logs.py`.
2. ab2/report.txt: every deck net <= 0, searched slower 0 (else single-game d8 b0 repro). Copy it to
   logs/mana-clean-chain/itemC2/ab2_report.txt.
3. `./build.sh pgo`, then `python3 scripts/mana_clean.py run pirates --games 5000 --name pirates-C2r2-20261114`
   (block 74000-78999); record goes to test/mana_clean.json via the script; commit it.
4. Finish the C2 bullet at the end of docs/design/mana-clean-pass-checkpoint.md (round 2 numbers, gate result) and add a
   QUEUE.md line (build/scratch/claude1110/QUEUE.md). Do NOT push the shared branch.
5. PARKED (do not start without the owner): s30070533 -- the CCO LINE RESERVE's cco_short_mask holds every cco land; the
   same other-cco-land count would apply there.

## Chain finished (after handoff was written)
- Round-2 regression: 322/322 PASS, nothing moved (no GT to accept). Steps 1 and 2 of "Remaining" are DONE.
- Round-2 A/B (seed 89500000, 32,000 games): zero changed outcomes, every deck net 0, searched slower 0
  (logs/mana-clean-chain/itemC2/ab2_report.txt).
- Next: step 3 (`./build.sh pgo`, gate pirates 74000-78999 `--games 5000`), then step 4.
