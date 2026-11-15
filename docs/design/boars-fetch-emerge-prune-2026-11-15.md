# Boars fetch and Decimator candidate pruning (2026-11-15)

The user asked for a shock-first hard prune of Boars fetch targets and for a smaller Decimator of the Provinces emerge-victim set, including a one-victim trial. A fixed count was diagnostic only: the intended final shape was one victim when the choice is clear, with more candidates where damage or payment is uncertain.

## Fetch targets

Boars already keeps one target when a fetchland is played. `MTG_BOARS_FETCH_PRUNE` (default on, `=0` restores the full set) now also keeps the top shock-first target for a held fetch activation and the legacy immediate-fetch route. The rule takes Stomping Ground while available. When only basics remain, `MTG_BOARS_FETCH_PIPS` (default on, `=0` restores the plain order) uses the cast line's required coloured pips to choose the basic. The no-shock unit board has one red source and Ilharg needing two: Mountain wins over Forest. Human play and `MTG_UNPRUNED` still expose every target.

The paired 1,175-game smoke and 875-game held-out A/B was byte-identical with the fetch prune on and off: every average, digest, and search-unit count matched. The sample did not exercise a held activation where the extra fan mattered, so no speed gain is claimed. The unit test exercises the held route, its Forest fallback, and memo separation between the two switch settings. A separate 2,050-game Boars-only pip A/B was byte-identical in every per-game `.wins` file; only 306 search units in one 300-game cell changed. The global `MTG_FETCH_RANK_PIPS` remains off because its Creature Giving losses were not resolved. These Boars rules implement the user's fetch doctrine without changing the measured games.

With the final defaults enabled, all ten saved Boars play references pass strict replay: two exact, eight repaired menu indices, and no play drift, board divergence, or enumeration gap.

## Decimator victims

The depth-0 victim **ranking** is a separate improvement, adopted in `boars-decimator-emerge-rank-2026-11-15.md`. The searched victim **prune** was rejected after these paired trials on the rebased branch:

| Arm against ranked, unpruned victims | Games | Win-turn result | Search work / measured time |
|---|---:|---|---|
| One battlefield victim by the tested rank | 1,175 smoke + 875 held-out | Smoke searched unchanged; held-out three faster and three slower, net zero | Held-out units down 2.3%, measured time up 8.0% |
| Conditional provider set with exact paid-cast damage | 2,050 | Net one turn faster; one 2HG game slower | Units down 1.4%, measured time up 3.2% |
| Conditional provider set with cheaper damage estimate | Smoke and partial held-out | Two searched smoke games one turn slower; both recover with the victim gate opened | Search units rose in those smoke cells |

The fixed one-victim arm's three slower held-out games all changed fetch timing before Decimator was cast, then drew different cards after the shuffle. In the known-respecting blind-search replay across 20 salts, most results tied; the three games' total deltas were −2, +4, and +2 turns, rather than a stable advantage for the uncapped arm. The exact conditional arm had only 2HG seed 4004 game 62 slower, the same fetch/draw divergence. The cheap estimate lost genuinely useful searched lines: Boars smoke game 79 and 2HG smoke game 10 both recovered their earlier win when `MTG_UNPRUNED=emergevictim` reopened the victim set.

The candidate selection did not earn its runtime cost, and the cheaper version failed the searched-play check. The shipped code keeps every legal Decimator victim for searched play, including Somberwald Sage, while its depth-0 ranking uses exact payment and cast-trigger damage. Hard cast and hand-victim lines remain available.

Artifacts in this worktree: `build/boars_prune_ab/` (paired `.wins`, manifests, blind-salt results, and changed-game traces). `merged_base` and `merged_on` are the 2,050-game A/B against all three proposed Boars switches before the victim prune was removed; `merged_rank_only` is the ranked, unpruned baseline for the later prune trials.
