# Invigorate source-copy oracle diagnostic, 2026-11-08

Source: `6fc3eb5b0449efd1943dfbd21525a7d63d941f7c` (`0ceb1876b`), including the Grove colorless fix. Binary: `build/Release/mtg`, SHA-256 `ab4d477df8175446102d8484c6d7cba301e7116fccf30c85a1c518a96defe8b1`. This is a **Release diagnostic**, not a formal `mana_clean.py` gate, which requires a matching PGO binary.

Anti-Lifegain seed `30090033`, game index `90033`, turn 3 had an `ORACLE-GAP`: the engine tapped one Ignoble Hierarch for Tainted Remedy, and the oracle's grouped payment used its twin. Invigorate's later own-creature pump then targeted differently. A reconstructed board test went red before the source change and green after it. Saved games `90033`, `92453`, and `93502` have no `ORACLE-GAP` or `UNMODELED` flag with this source. In a paired 50-game slice (indices 90000–90049), the Grove-only baseline had four `ORACLE-GAP`s and this source had zero; both observed 376 decisions.

Fresh 5,000-game block: seed `30095000`, indices 95000–99999, one thread. Oracle-on and oracle-off both played 5,000 games, averaged 4.1970 turns, used 17,753,153 work units, and produced digest `eb17d5f4f8292b3b`. The oracle-on run still reported 47 `ORACLE-GAP`s across 31 games, so Anti-Lifegain is not clean. Forty-four gaps mention a pump difference; eight flags have no Invigorate action listed in the plan, including tutor continuations that auto-fire Invigorate from hand. No `UNMODELED` flag occurred. `DOMINATED-LAND` (2,663) and `CONTINUATION-DIFFERS` (8) were disclosed context.

Focused replay of seed `30095038`, game index `95038`, turn 4 showed that the oracle offers the engine's three-payment script (Stomping Ground `{G}`, Godless Shrine `{W}`, Birds of Paradise `{W}`); the resulting board still differs only in the Birds pump. This narrows that residual to Invigorate timing or target selection after payment enumeration.

Full local unit suite: 2,638 tests and 3,578,069 assertions passed. Files: `manifest.json`, `on.out`, `off.out`, and `issues.err` (whole issue lines). Unfiltered oracle stderr is scratch output under `/tmp/invig-fresh-on.err` in the current container.

CI run `42512372037` completed successfully on Linux and Windows, including Linux/Windows determinism parity. This validates the WIP source and focused test; the 47 residual oracle gaps above still prevent a clean gate.

## Upfront payment timing diagnostic (WIP, 2026-11-09)

The engine's whole-turn batch prepay taps the old Birds of Paradise before Invigorate chooses its own-creature target. The oracle normally pays each cast in order, so the same three taps pumped a different Bird on seed `30095038`. The new fallback replays an oracle payment script with its taps made upfront, and accepts coverage only if a full apply reaches the engine's line and core with at least its resources. It is limited to no-land plans made only of hand casts with an own-creature target; land and tutor continuations remain open work. The reconstructed seed test was red before this change and is green afterward.

Release binary SHA-256 `29f8fda7dfcc324194f0ea30406debfcd6329a6675022e716af8d0ea39423d9a`. The same 5,000-game manifest and seed range as above completed oracle-on: 5,000 played, average 4.1970, digest `eb17d5f4f8292b3b`, 17,753,153 work units, 27 `ORACLE-GAP` flags across 20 games, zero `UNMODELED` and zero `CAPPED`. This removes 20 of the prior 47 flags and introduces none. Twenty-four remaining flags still mention a pump difference; eight plans do not explicitly list Invigorate. The paired 50-game slice, indices 90000–90049, had identical oracle-on/off average 4.2200, digest `1c6ad43509e29993`, and 232,656 work units; oracle-on had zero gaps. The full unit suite passed 2,639 tests and 3,578,075 assertions. The reference safety scan passed all 186 checkouts/worktrees before this report was pushed.

Files: `timing-on.out`, `timing-issues.err`, `timing-ab-on.out`, and `timing-ab-off.out`. This is a Release diagnostic, not the formal PGO mana-clean gate. Anti-Lifegain remains unclean.

## Land-first timing diagnostic (WIP, 2026-11-09)

The upfront replay now stages a searched land-first drop before making the oracle script's taps, then applies only the casts using that float. It declines Karoo and non-first land positions, rejects a staged apply that does not play exactly one land or that casts a spell, and still requires a full final line/core/resource cover. Saved game `95012` changed from one gap to none; tutor continuation games `95031` and `95337` retain their gaps.

Release binary SHA-256 `77c28cb83edd94dc80508f4b8b7fc6f209b3e0ac5e8c4d107eea2ee3b7b9790b`. The same 5,000-game manifest completed oracle-on: 5,000 played, average 4.1970, digest `eb17d5f4f8292b3b`, 17,753,153 work units, 20 `ORACLE-GAP` flags across 14 games, zero `UNMODELED` and zero `CAPPED`. It removes seven more flags from the prior 27, with none introduced. Seventeen remaining flags mention a pump difference; eight plans do not explicitly list Invigorate. The matching play digest and units agree with the prior oracle-off 5,000-game run. The full unit suite passed 2,639 tests and 3,578,075 assertions. The reference safety scan passed all 186 checkouts/worktrees before this report was pushed.

Files: `land-timing-on.out`, `land-timing-issues.err`. This remains a Release diagnostic; the formal PGO mana-clean gate is not clean.
