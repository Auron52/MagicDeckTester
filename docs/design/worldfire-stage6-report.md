# Worldfire deck analysis: Stage 6 report (2026-11-22)

Deck: `decks/Worldfire/Worldfire.cod` (60 cards, blue/red). The plan suspends Rift Bolt or Keldon Halberdier, assembles nine mana, casts Worldfire, and lets the suspended threat kill after the reset. The full implementation ledger is `analysis-Worldfire.md`; the keep-order design is `worldfire-discard-buckets.md`.

## Cards and profile

Six missing cards were implemented: Worldfire (Tier 3 mass exile and life reset, including simultaneous leaves-the-battlefield triggers), Keldon Halberdier (Tier 2/3 creature suspend and off-suspend haste), Mystical Tutor and Personal Tutor (Tier 1 searched top-of-library tutors), Overmaster (Tier 1 draw), and Mistrise Village (Tier 1 check land). All 22 distinct cards in the list now have full coverage. Worldfire's two resolution parameters create no human choice; the user approved their inert viewer classification.

The committed profile uses a one-land minimum, five-land maximum, `stop_at: 4`, `curve_check: two_drop`, `bottom_order: count_first`, and no required-piece flags. The learned first-copy scores especially favor Lotus Bloom (+0.445), Mystical Tutor (+0.282), Seething Song (+0.189), Keldon Halberdier (+0.131), and Worldfire (+0.115); a second Worldfire scores -0.184. The profile's cost diagnostic was neutral, its discard diagnostic was `STATUS_QUO_OK`, and provider policy passed.

## Measured result

| Accepted case | Games | Credited mean win turn | Mean free turns/game |
|---|---:|---:|---:|
| Solo smoke, d5/b20, seed 1001 | 40 | 4.500 | 1.950 |
| Solo regression, d5/b20, seed 2002 | 60 | 4.417 | 2.083 |
| Solo regression, d5/b20, seed 3003 | 60 | 4.167 | 1.967 |
| 2HG smoke, d5/b20, seed 1001 | 20 | 4.400 | 1.650 |
| 2HG regression, d5/b20, seed 2002 | 30 | 4.500 | 1.367 |

All these cases won every game. A free turn advances the actual turn and the opponent's draw while consuming no credited turn; the raw-round number can therefore be higher than the credited result. The opponent model plays lands and scheduled spawns but does not cast spells, block, or attack. The Sire fairness check treats a castable **nonland permanent** as a virtual escape and deliberately underestimates the lock, as the user specified. Annihilator's virtual sacrifice-target fairness applies on a turn when the opponent played a land, with the user's no-land-turn exception. The free-turn means are informational only; they do not feed the search or GT digest.

The integrated build and 3,946 serial unit tests passed. A pooled smoke/regression run passed 608 scenarios and replayed 940 references with no play drift, board divergence, or enum gap. All 133 changed keys and 28 searched-depth slower games were attributed before the measured GT was accepted (`worldfire-integration-audit.md`); `check_gt_logs.py` found 1,409 consistent keys. The deck verifier passed coverage, Scryfall cost and field checks, all three suite-tier membership, viewer self-guard and surface sweep, six viewer decision types, two 120-game mismatch arms, eight play-invariant games (332 decisions), and provider policy. The earlier 16-game external-play sweep found no legality/state flag and exposed an Irencrag Feat suspend restriction bug, now fixed; its three earlier wins were budget/reach misses, including a Halberdier T5 line recovered at d8/b0. That sweep predates the final cantrip and free-turn changes; the current viewer and mismatch gates ran on the integrated source.

Two larger 500-game paired keep-order experiments found no repeatable improvement over the adopted defaults (`worldfire-followup.md`), so no follow-up arm shipped. The post-adoption d5 budget curve on 200 paired games improves from 4.380 at b20 to 4.200 at b200, at 4.4× search units; the play setting remains b20. The two formerly degenerate budget-zero backstop seeds now finish T5 in at most 39,189 units, far below the two-million-unit ceiling.

## Encoded decisions and assumptions

`DetectDecisionProvider` routes this list to `WorldfireProvider`. Its complete set of deck-specific choices is:

| Hook group | What it decides in automated play | Search effect |
|---|---|---|
| `TutorCandidates`, `TutorNeedsCandidate` | Mystical/Personal Tutor consider the missing Worldfire or suspend finisher, then acceleration when the pieces are held; an unneeded tutor is not cast. | **Prune.** A tutor-for-a-tutor or off-plan target can be missed. The old tutor-for-tutor faster game was shown to depend on the post-shuffle draw. Human play and `MTG_UNPRUNED` see every legal target. |
| `OffersCastNow`, `OffersSuspend` | Hold Rift Bolt for a Worldfire kill, releasing it for immediate lethal or the go-off suspend turn. | **Prune.** An off-plan burn race can be missed (one measured case); the user's combo hold measured better on held-out games. |
| `CleanupDiscardCandidates`, `CleanupDiscardShedOrder`, `CleanupDiscardShedStable` | Recompute a bucketed keep order after each shed: missing combo, finisher, usable mana by projected go-off turn, dig, backup, and overflow. | **Prune/rank.** Cleanup drops the lowest-ranked card; a nonstandard backup or extra land/dig line can be missed. |
| `PutBackRanking`, `PutBackRankingPerCard`, `PutBackSingleCandidate` | Brainstorm puts the two lowest-ranked cards back, with the better one on top. | **Prune.** The automated put-back fan is one choice; human play and unpruned audit retain the legal fan. |
| `TopDispositionPick`, `KeepReorderTop`, `ReorderSingleCandidate`, `ReorderKeepShuffleOnly` | Ponder orders kept cards by quota/deadline but keeps shuffle versus keep searched; Preordain keeps an unfilled quota card and aggressively bottoms nonpieces when a combo piece is genuinely missing. | **Mixed.** Ponder changes order only; Preordain fixes scry disposition. Human play exposes the legal choices. The optional Ponder one-choice prune remains off. |

The rank projects a go-off turn using battlefield mana, land drops, rituals, Blooms, tutor timing, and a suspended threat's arrival; it prefers color coverage, the next land, castable acceleration, then further land and dig quotas. It treats a Mystical Tutor cost as free before the go-off turn when a spare non-depletion blue source can pay it. Search still has finite depth/budget, reads the deterministic library and shuffle outcomes, and can miss a better line at b20; the b200 curve quantifies that limitation. The provider's win certificate remains `NotAssessed` for the nine-mana/suspend reach, so it does not claim a turn is unwinnable from a shortcut.

The user approved these goldfish deferrals: Overmaster's and Mistrise Village's uncounterable riders (nothing counters spells in this mode), Halberdier's optional off-suspend decline collapsed to casting, and first strike with no blockers. Worldfire's leaves-the-battlefield trigger deferral was withdrawn and implemented. Memory Lapse remains intentionally inert against the passive opponent. No Worldfire card's interactive human choice is silently auto-resolved: the viewer audit covers dig, land entry, reorder, scry, target, and tutor entry choices. Further engine and card-model detail is in the maintained implementation and verification sections of `analysis-Worldfire.md` and in `worldfire-sweep-deferred.md`.

The main settings opportunity is the budget: b50 improves this 200-game paired sample by 0.09 credited turns at about 2× units; b200 improves it by 0.18 at 4.4×. The remaining deferred sweep items are logging/labels and a few non-Worldfire mass-leave paths recorded in `worldfire-sweep-deferred.md`.
