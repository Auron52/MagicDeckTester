# Prevent Damage: preserve a named line under a nonexact pain floor

The pooled mana-clean gate `codex-mana-queue-20261115` found five identical
`APPLY-DROP` flags on seed 30,058,249, turn five. The named line plays
Ancient Tomb, casts Living Wish, then Rolling Earthquake for X=2 and X=1.
After Wish, the first Earthquake can use Reflecting Pool for red and the
new Tomb for generic mana, leaving Grand Coliseum's red mode for the second.
The engine instead chose Coliseum's painless colorless mode for the first
Earthquake's generic cost and dropped the second cast.

`MTG_PD_PAIN_PAY=0` removes all five drops. The line's live unpaid cost
still includes both Earthquakes at the first payment, and its traits count
three casts. The failure is in `dmgev::PainAwarePay`'s harmful, nonexact
damage-floor branch: it minimizes damage and returns without consulting
the named-line payability callback. The exact-floor branch already checks
that callback. The new nonexact fallback retains the initial unrestricted
payment only when the cheaper payment strands the named line, the initial
payment leaves it payable, and its damage is survivable. Both payments
still pay the same printed cost. `MTG_PD_NONEXACT_LINE_FALLBACK=0` restores
the previous order (default on).

The focused oracle replay reports five drops with the switch off and zero
with it on. A fresh 1,000-game Release pilot (seeds 9,900,001–9,901,000)
was play-identical. A 5,000-game held-out pair (seeds
9,950,001–9,955,000) was also play-identical: 4,937 wins in both arms,
zero changed win turns, and all 200 job digests matched. Search units were
155,497,085 off and 155,497,005 on. Recorded game milliseconds were
2,873,987 off and 2,921,349 on (+1.6%).

Raw paired data: `build/scratch/pd_nonexact_*.{out,err}`.

The fresh PGO mana-clean block 62,000–66,999 (`codex-pd-nonexact-20261116`,
source tree `7596aacef2f9`) was oracle-on/off play-identical. It found 404
issues: four APPLY-DROP, 26 APPLY-DROP-ORDER, 171 ORACLE-GAP, ten UNMODELED,
15 CAPPED, and 178 CAPPED-DECISION. The four drops are two signatures in one
new seed, 30,064,758, where a Rolling Earthquake continuation after Dina was
not declared by the outer plan. The earlier five flags at seed 30,058,249
remain cleared. The 26 order drops are different lines, mainly a Quake cast
before Dina when Dina first is payable. The 178 decision flags come from one
budget-exhausted decision. The next fresh Prevent Damage block is
67,000–71,999. Full report: `logs/mana_clean/codex-pd-nonexact-20261116/report.md`.

The recorded-board unit fixture pays both Earthquakes and passed with the
full Release suite: 3,354 cases and 3,614,066 assertions. A deck-scoped
combined smoke/regression run had all nine row digests identical with the
switch on and off on the same Release binary. One regression row disagrees
with committed ground truth in both arms; no ground truth was accepted.
Strict viewer replay had zero play drift, board divergence, or enum gaps
across 864 references, and both required-beat references passed.
