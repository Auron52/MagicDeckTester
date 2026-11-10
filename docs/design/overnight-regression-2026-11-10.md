# Periodic overnight regression audit — 2026-11-10 UTC

The full overnight tier ran once at source commit `b416c3cf9` on PGO+LTO with 29 workers while the three Dragons `gi2535` tails continued. All 430 scenario checks passed. Of 578 regression cases, 492 matched ground truth and 86 changed; no case was new or missing. The batch took 30m18s. Extra Turns cases added concurrently after this run were preserved when the audit was integrated; they were measured by their own accepted runs at `3baf22ffd`.

## Per-game review before acceptance

The committed `test/gt_logs/` and this run's `test/logs/overnight/wins/` were compared by game index, win turn, and play digest. All 572 changed games were classified: searched depth had 24 faster, 3 slower, and 449 same-score play changes; depth zero had 8 faster, 2 slower, and 86 same-score play changes. No win became unwon. The searched net was **21 turns faster** across the changed games; depth zero was 6 turns faster. Every one of the 572 changed games was replayed once with `MTG_FLAG_NONCONV=1 MTG_FD_ORACLE=1`; all 572 win turns and play digests reproduced exactly. There were zero `[nonconv]` flags and two `[fd-diverge]` flags, both TH games with realized T8 versus predicted T7. Neither was a severe divergence.

| Deck | Faster | Slower | Same-score play changes | Net turns |
| --- | ---: | ---: | ---: | ---: |
| Breaching Dragonstorm | 0 | 0 | 1 | 0 |
| Bruna | 0 | 0 | 2 | 0 |
| Dragons | 0 | 0 | 16 | 0 |
| Goblins, including 2HG | 3 | 1 | 49 | -2 |
| Jumbo Cactuar | 6 | 0 | 91 | -6 |
| Kithkin | 13 | 1 | 72 | -12 |
| Midrange Burn, including 2HG | 2 | 1 | 57 | -1 |
| Modern RW Burn, including 2HG | 3 | 2 | 191 | -1 |
| Niv, including 2HG | 0 | 0 | 16 | 0 |
| Pirates, including 2HG | 5 | 0 | 34 | -5 |
| TH | 0 | 0 | 6 | 0 |
| **Total** | **32** | **5** | **535** | **-27** |

These groups account for every changed game. The many unchanged-score play digests are line changes without a changed win turn; the instrumented replay checked each resulting line. The searched improvements are in the mana and cast-order decks touched since the prior full baseline. The three searched slowdowns received individual checks:

- `goblins_overnight_d3_s4004 gi95`: T4 to T5 with identical hand and draws. The new 10 ms search plays Aether Vial before attacking on T2. The 4× and 16× budget replays both return to T4, so this is budget churn.
- `modernrwburn_overnight_d5_s5005 gi114`: committed log T6, current T7. The 4× and 16× replays return to T6. More decisively, the Release binary built at the exact Modern RW Burn acceptance commit `0c75aaa2d` produces the **current T7 and identical play digest** with the committed deck and profile files.
- `modernrwburn2hg_overnight_d5_s7007 gi58`: committed log T4, current T5, and both higher-budget replays remain T5. The same exact-commit binary produces the **current T5 and identical play digest** at the original budget. It is therefore a stale committed game record, not a slowdown introduced after that acceptance.

For completeness, all **196** changed Modern RW Burn and 2HG games were replayed with the exact `0c75aaa2d` source and its committed deck/profile files. All 196 match this run's win turn and digest; none matches its committed ground-truth record. `git diff 0c75aaa2d..HEAD` has no Modern RW Burn deck/profile changes. The previous Modern RW Burn ground-truth promotion captured results that the acceptance commit cannot reproduce; this full run corrects those records. The old overnight trace snapshot predates that adoption, which is why its inline Modern RW Burn trace comparisons displayed the same current line on both sides despite the `.wins` differences.

The two depth-zero slowdowns were spot-checked. Midrange Burn's T6-to-T7 example changes the T4 burn-spell order; Kithkin's old trace snapshot does not reproduce its committed T5 record, so the committed `.wins` remains the comparison source. Depth zero is a greedy sanity check rather than the searched quality bar.

The three Dragons `gi2535` processes were still consuming CPU at the end of this audit. This regression run did not duplicate or stop them.
