# Beasts analysis report

The analyzed list is [`decks/Beasts/Beasts.cod`](../../decks/Beasts/Beasts.cod), with 26 matched card definitions. The play viewer exposes Beasts from the primary checkout at `http://localhost:8080`. The implementation history, card rulings, and per-batch evidence are in [`analysis-Beasts.md`](analysis-Beasts.md).

## Results

| Sample | Result |
| --- | ---: |
| Standard overnight, depth 5 / budget 20, four seeds | 1,192 wins / 1,200 games (99.33%); 4.959 turns among wins |
| Two headed overnight, depth 5 / budget 20, four seeds | 200 wins / 200 games; 5.285 turns |
| Standard regression, depth 5 / budget 20, two seeds | 100 wins / 100 games; 4.970 turns |
| Official cost gate, worst searched two headed shape | 2.62 times same-run Hinata, below the 3 times limit |

The profile was generated from 1,000 card-score games at seed 221122. It keeps one to five lands, has no required card, and uses depth 1 / 1 ms evaluation for mulligan bottoms, refining five finalists. That policy preserved all 215 paired win turns in admission and fresh-seed screens. An exhaustive keep and bottom table is a separate future profile stage.

## Implementation and verification

This run implemented or completed Herald of Ilharg, Thragtusk, Contested Cliffs, Garruk's Packleader, Cragplate Baloth, Krosan Warchief, Questing Beast, Elder Gargaroth, Garruk's Harbinger, Kaheera, Migloz, Nykthos, Bloated Contaminator, and Woodland Bellower, with their relevant mana, combat, trigger, tutor, poison, and play-viewer paths. The ledger gives each card's exact modeled clauses and approved exceptions.

All three Beasts and two headed regression tiers were accepted without changing existing deck play. The full verifier passed coverage (26/26), live card costs, card fields, regression tiers, mismatch harnesses (zero nonconvergence or full-depth divergence), and play-protocol determinism. Fifteen Codex-play games raised zero verified flags. The 40-game live viewer audit surfaced every expected interactive type and found no unverified or unmodeled choice phrase. The reference safety scan covered 58 checkouts and worktrees; every changed reference was protected.

## Encoded assumptions and search limits

| Rule | Source | Classification and effect |
| --- | --- | --- |
| Passive opponent, deterministic known library | Simulator | The reported goldfish result assumes no opponent blocks, casts, counters, gains life, or prevents damage. Search can see the deterministic library order. |
| Depth 5 / virtual 20 ms decision budget | Regression matrix | Search limit. A cast-order oracle found one turn-four win where the shipped search won on turn five; shipped search finds a turn-four Incubator line at 100 ms. Full cast-order expansion worsened two of 50 matched games at 20 ms, so it was not adopted. |
| Cheap-first mulligan bottom evaluation, then five full finalists | Beasts profile | Pruning for a measured hot path. A discarded finalist could hold a better hand, though 215 paired win turns matched full bottoming. |
| Per-turn second main after combat creates a hand card or usable counter resource | `BeastsProvider` | Search shape. It keeps the full second-main menu on resource turns and avoids a second solve on other turns. The deck-wide comparison is in the ledger. |
| Skip a postcombat *kill-only* search when no postcombat effect can kill | `BeastsProvider::PostCombatCannotKillOptIn` | Correctness shortcut backed by the stock list's absence of postcombat burn, poison, mill, extra combat, and instant-win effects. |
| Kaheera before Warchief when Packleader is watching | `BeastsProvider::CastOrderRank` | Cast-order preference that realizes the Packleader draw; a payable Warchief-first rescue remains searchable. |
| Same-name ETB dig targets fold to one rank | `BeastsProvider::EtbDigCandidates` | Identity fold: two copies produce the same hand name and remaining library multiset. The human menu still shows legal targets. |
| Migloz vigilance/menace grant and artifact/enchantment destroy targets narrow in autonomous play | `BeastsProvider` oil and target hooks | Pruning accepted by the user. Human play and the unpruned gates retain the complete legal menu. |
| Poison progress in the no-win leaf; extremely narrow no-win certificate | `BeastsProvider` | Search evaluation and proof. The certificate only proves an empty action state winless and refuses poison-bearing states. |
| All-beneficial proliferate subset collapses to all candidates | Shared provider | Dominance prune for own beneficial counters and opponent poison; mixed or harmful kinds keep subset search. Human play retains every subset. |
| Gargaroth mode and combat-dig product cap of 27 | Shared solver | Branch limit: later ordinals can take the provider's default when the Cartesian product exceeds 27. The stock list has one Harbinger and at most four Gargaroths; token/draw on four Gargaroths is width 16. |
| Packleader draw before Bellower search when both trigger on entry | Card model | User-approved trigger-order collapse. The alternative order is not searched. |
| Cavern type choice treated as any creature; fetch activation restricted to main-phase slots | Card model | Simplifications recorded in `cards.json`. Cavern's creature-only mana restriction is enforced; its uncounterable clause is inert against the passive opponent. |

The exact bracket annotations in [`cards.json`](../../src/cards/data/cards.json) are the authoritative per-card disclosure. User-approved deferred clauses include Warchief regeneration; Questing Beast's small-creature block restriction, damage prevention and planeswalker redirect; Migloz's two oil branches; Gargaroth's gain-three mode; and the Packleader/Bellower trigger order. These are also listed with context in the [analysis ledger](analysis-Beasts.md).

## Remaining observations

The overnight tier met its time ceiling but contained two unusually slow, unwon standard games (seed 7138 index 131 and seed 7252 index 245). The leaf tie-break is provisionally on: 2,000 fresh paired games found zero earlier or later wins, too few changed games for the tie-break harness's significance threshold. Neither observation blocks the accepted deck analysis; both remain recorded for future performance work.
