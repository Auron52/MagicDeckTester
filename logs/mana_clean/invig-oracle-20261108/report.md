# Invigorate source-copy oracle diagnostic, 2026-11-08

Source: `6fc3eb5b0449efd1943dfbd21525a7d63d941f7c` (`0ceb1876b`), including the Grove colorless fix. Binary: `build/Release/mtg`, SHA-256 `ab4d477df8175446102d8484c6d7cba301e7116fccf30c85a1c518a96defe8b1`. This is a **Release diagnostic**, not a formal `mana_clean.py` gate, which requires a matching PGO binary.

Anti-Lifegain seed `30090033`, game index `90033`, turn 3 had an `ORACLE-GAP`: the engine tapped one Ignoble Hierarch for Tainted Remedy, and the oracle's grouped payment used its twin. Invigorate's later own-creature pump then targeted differently. A reconstructed board test went red before the source change and green after it. Saved games `90033`, `92453`, and `93502` have no `ORACLE-GAP` or `UNMODELED` flag with this source. In a paired 50-game slice (indices 90000–90049), the Grove-only baseline had four `ORACLE-GAP`s and this source had zero; both observed 376 decisions.

Fresh 5,000-game block: seed `30095000`, indices 95000–99999, one thread. Oracle-on and oracle-off both played 5,000 games, averaged 4.1970 turns, used 17,753,153 work units, and produced digest `eb17d5f4f8292b3b`. The oracle-on run still reported 47 `ORACLE-GAP`s across 31 games, so Anti-Lifegain is not clean. Forty-four gaps mention a pump difference; eight flags have no Invigorate action listed in the plan, including tutor continuations that auto-fire Invigorate from hand. No `UNMODELED` flag occurred. `DOMINATED-LAND` (2,663) and `CONTINUATION-DIFFERS` (8) were disclosed context.

Full local unit suite: 2,638 tests and 3,578,069 assertions passed. Files: `manifest.json`, `on.out`, `off.out`, and `issues.err` (whole issue lines). Unfiltered oracle stderr is scratch output under `/tmp/invig-fresh-on.err` in the current container.
