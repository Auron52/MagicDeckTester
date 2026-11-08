# Grove colorless oracle diagnostic, 2026-11-08

Source: `a2257846f4ff9a3da2fdf4b1e58e54b059331c48` (`b6d9f1414`). Binary: `build/Release/mtg`, SHA-256 `911bd2a8cb71d9d663b2092891afc2a9aef29aa32560978fe3a91f732f30dae3`. This is a **Release diagnostic**, not a formal `mana_clean.py` gate: that script requires a matching PGO build.

The original Anti-Lifegain seed `30085001`, game index `85001`, turn 2 had one `ORACLE-GAP` because the engine tapped Grove of the Burnwillows for painless `{C}` and the oracle offered only `{R}` or `{G}`. With this source, the oracle offers Grove `{C}` and the flag is absent. A paired 50-game sample (indices 85000–85049) fell from 58 `ORACLE-GAP`s to zero with the same play digest `a37e2c9d64f83bba`.

Fresh 5,000-game block: seed `30090000`, indices 90000–94999, one thread. The oracle-on and oracle-off runs both played 5,000 games, averaged 4.1922 turns, had 17,695,925 work units, and produced the same digest `a6e9201ba1c03bab`. The oracle-on run still reported 388 `ORACLE-GAP`s and 8 `UNMODELED` flags (one capped game, index 93116), so Anti-Lifegain is not clean. Of the 388 gaps, 386 have an Invigorate pump difference; the other two are games 92453 and 93502, also on plans containing Invigorate. Those residuals need a separate source-copy/payment-order investigation. `DOMINATED-LAND` (2,748) and `CONTINUATION-DIFFERS` (111) were disclosed context.

Files: `manifest.json`, `on.out`, `off.out`, and `issues.err` (whole issue lines). The original unfiltered stderr is scratch output under `/tmp/grove-fresh-on.err` in the current container.
