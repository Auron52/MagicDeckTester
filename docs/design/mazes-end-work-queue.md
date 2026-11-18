# Maze's End: work queue from the USER rulings of 2026-11-18

Checkpoint for the Maze's End analysis (ledger: `docs/design/analysis-Maze's End.md`). USER rulings VERBATIM,
in the order given, then the queue they produce. Owner: the Maze's End coordinator session.

## USER rulings (VERBATIM)
1. Tie fix: "For 1 I'm not too worried about d0, but the searched is a little more troubling. Have we looked at the results and determined it was just ties breaking one way or the other? I would analyze them and 100% make sure they are recoverable. If alll of those things are try then we can accept."
2. Greedy scoring ties: "Those sound reasonable to fix. For Reveillark evoking can be worse or better depending on the context."
3. C1-C4: "Yikes we should fix all of these problems and A/B them."
4. Defaults table + P4 prunes: "2-4 Yes, Talon Gates is potentially a worthy candidate if we are lacking in extra land drops. The rest of the table. For d3 I would consider playing more d5 games instead of the majority of d3 if they are faster. No point in playing d3 if we can test play settings instead at a cheaper price. Maze's End "find nothing" should be heuristically pruned. I thought we already did the duplicate merge, but if not then yes, let's do it. Burgeoning choice is the same as playing a land. If we need to we can prune it, but otherwise just treat it as a land-drop. Archdruid's Charm, yes it should go to the Battlefield in our heuristic. 5. We should either play it and pay the 1 or not play it. Sacrificing is a dominated option. 6. It is also worth it when we have literally nothing else to use the mana for. In fact, this is the main use-case. 7. True, the order doesn't matter much here unless the second land is played after other things or we are paying for a cost like Gateway Plaza."
5. Fetch defer + max_lands: "2-5 I'm not entirely sure that is 100% the right solution given that there is value to banking extra lands even if you have a new-name gate in hand. That said, I don't have recommendations as to how to value this while still not breaking anything. 2-6 Not a huge deal either way because it is temporary. We just want to get the deck in a position that we can run the mulligan profile generation. However, if you would feel a lot better about it you can make the tweak."
6. Off-main activation, b0, b20: "2-7 I think we should allow something like this. It's especially easy if we can respond to a trigger from Manabond or Burgeoning and activate in response. I cannot think of a reason why that would not be good, so that could be heuristically pruned to always search and deploy. The searched decision would be whether it is worth getting Maze's end back in hand without being able to redeploy. Ideally we would support that as well and see what we can do to minimize the cost. 3-8 We should address this at some point or we won't be able to do the value-leaf. I'll leave the timing to you. 3-9 This means we probably need to work more on performance to start which fits with 3-8. We'll need more effective prunes to be applied. Higher budget is not out of the realm of consideration, but only after we have exhausted other avenues."

## Running (2026-11-18)
- Tie-fix slower-games audit -> `logs/maze/tie/slower-games-audit.md`. Accept the tie fix only if all 11 searched slower games are ties AND recover at d8 b0.
- Scoring fixes (empty tutor, Time Warp on the opponent, Reveillark by outcome), branch `wip/tie-scoring` -> `logs/tie-scoring/results.md`.
- V1 (Stage 5 non-sweep) and F2 (sweep defects 2-3, Stompy A/B), branch `wip/maze-fix`.

## Queue (after F2 lands and `wip/maze-fix` is integrated onto `wip/maze-end`)
One narrow change set per agent; at most two concurrent worktree builds.
- **P5 Maze's End provider prunes:**
  - "find nothing" pruned while a legal Gate target exists;
  - Burgeoning's menu = the land-drop menu;
  - Archdruid's Charm land -> battlefield only;
  - Gateway Plaza = pay or don't play (no sacrifice; caveat to state: a sacrificed Plaza still draws off Guild Summit);
  - Heap Gate Treasure offered when the mana is otherwise unused, or when colour / next-turn mana binds;
  - Exploration back-to-back land order folded (not after other actions or with an entry cost);
  - A1 weights -> v5 and delete `MTG_MAZE_WSEL_*`;
  - Talon target refinement measured;
  - `max_lands` 5 -> 6 measured;
  - suite rows: most d3 games -> d5 play-setting games at about equal tier cost.

  One pooled held-out A/B.
- **P5b engine duplicate-copy fold** (identical hand copies, about 1.79x positions on 91021 gi20). Every deck that moves gets the A/B.
- **P5c fetch-defer root cause.** The P1c unit lost a turn at d3 budget 0 because the plain land play sat at continuation rank 3 while wave 0 admits ranks 0..W-1. Make every rank reachable as budget grows, delete `MTG_MAZE_FETCH_DEFER`, keep the fetch value for banking a Gate.
- **P6 off-main Maze's End activation:**
  - In response to a Manabond / Burgeoning trigger, activate and redeploy: a provider prune to the single option (state its premises, e.g. what Burgeoning's put would otherwise have been).
  - At the end step / on the opponent's turn with no redeploy: a SEARCHED frame (activate or not, fetch target via the P3 menu), cost minimized and measured.
- **P7 performance:** b0 degeneracy (171/246 screened games hit the 2M-unit cap; needed before the value leaf) and b20 starvation (sweep gi0/gi7/gi13 one turn slow at b20, found at b100-500). Prunes and census first ("keep pruning the unreasonable"); a higher play budget only after other avenues are exhausted (USER).
- **C1-C4:**
  - C1 Seahunter target tag (Merfolk Turns);
  - C2 multi-drop route for Scale the Heights decks;
  - C3 start/end-of-turn first-option shortcut (echo, Defense of the Heart, morph face-up timing, upkeep tutors);
  - C4 every tutor in a plan searched.

  Each gets its own A/B.
- **Then:** re-check the sweep flags, `verify_deck.py` green, Stage 6 report, then the mulligan profile (`mullgen.sh run`). The value leaf comes after P7's b0 work.

## USER on the B disclosures (2026-11-18, VERBATIM)
7. "4-10 1 We might want to prune further then that with an A/B. It would be nice if there was no extra branching given how easy the mana is in this list. 3. Yes, these should be heuristically pruned. 13. Yes, this should be pruned heuristically in the search. 16-21. That's fine. 25. I'm not sure what this means. 28. Yes, manabond yes with no land should be pruned. Similarily "no" with only lands should be pruned heuristically. 31-32 Okay, sounds good."

Added to P5 (same pooled A/B):
- **Disclosure 1:** chosen colours (Thran Portal, CLB Gates) pruned to ONE colour per entry (no branching), e.g. the P1 rule's next colour (G, then U, then G); A/B against the current 1-2-wide menu.
- **Disclosure 3:** Basilisk Gate's pump pruned in autonomous play (check whether it is ever offered with no creature; the Charm mode 3 half is already withheld).
- **Disclosure 13:** confirmed as built (Charm mode 3 withheld in the search).
- **Disclosure 28 extension:** Manabond "no" pruned when the hand is ALL lands ("yes" puts them all and discards nothing). Premise to check and state: an all-land hand where every land enters without a loss. Gateway Plaza entering at the end step without {1} available would be sacrificed; keep "no" offered in that case, or verify it cannot happen.
- **Disclosures 16-21 and 31-32:** approved.
- **Disclosure 25:** explained to the USER. The census over 90 games shows 0 overflow; no action unless the USER asks.

## Tie fix: ACCEPTED (2026-11-18), the USER's conditions met
Audit `logs/maze/tie/slower-games-audit.md`: all 11 searched slower rows (9 distinct games) come from one exact tie inside a greedy playout beyond the horizon, never from the root's options.
- `MTG_TIE_OLD_CANON` alone reproduces the old game 11/11; `MTG_TIE_OLD_CACHE` plays no part.
- Every game recovers at d8 b0, where new and old are byte-identical.

The ties are the empty Tooth and Nail and Time Warp at the opponent, the scoring fixes already running. The 7 smoke GT moves are accepted at the next shared push, after integration, rebase and a smoke run. Diagnostic levers stay on `wip/maze-tie` c2fc9b328 (DO NOT MERGE).

## V1 (Stage 5 non-sweep) DONE 2026-11-18, `1538f469a`
- **Gate:** verify_deck fails ONLY on regression_tiers (overnight GT awaits the periodic run) and claude_sweep (4 flags owned by F1/F2). Every other gate passes.
- **Added to the fix queue (after F2, with P5):** fd-diverge s121065 gi64 d5 b20. The executor paid Sylvan Scrying through Baldur's Gate's filter with Maze's End's {C}, so the bp-replayed Maze's End activation found it tapped. Repro in the ledger. Also re-check gi49/gi122 (the gi2 class) after F2's `5ea2d6be7`.
- **Open for the USER:** keep or delete P2 (inert on top of P1); full 5g mining (100-150 core-h; recommend after P5/P7); `MTG_UNPRUNED` 100-game oracle on a quiet box or after lazy axis generation.
- **Stage 6 TODO:** write the bucket discard policy (STATUS_QUO_OK) for the USER.
- **Shared push:** the `audit_viewer_decisions.py` fix `9ffcb7820` also fixes a bug that is live on origin.
