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

## USER, 2026-11-18 ~12:40 UTC (VERBATIM)
13. "Please continue autonomously overnight. The idea is to get things as fast as possible, but you can test whatever you need to, to prepare for running the mulligan profile. You can even start it if you feel it necessary, but I will want to generate the references for the deck tomorrow."
14. "But overall, the top priority are optimizations and fixes."

15. On the eight open questions (2026-11-18 ~13:00 UTC): "1. Honestly the rule is even fine outside of goldfish, so let's turn it on. We don't have interaction of our own to cast in response even if the opponent does something. 2. Sure, let's delete it. 3. That's fine, you can close it. The result came from experimentation anyway, so it is difficult for me to give helpful rulings on its correctness. 4. That's a decent question, but we'll leave it as-is for the other decks since they actually pay a higher cost. 5. Sure, whatever timing is fine with me. I'll leave that to you. What difference would it make if we had significantly more memory? 6. I'm okay with you pulling the latest and generating this sometime overnight, since I'm going to bed very soon. 7. What does P1 do? 8. Yes, we should prune that for sure. It's silly to give your opponent an extra turn."
   -> heapstrict SIGNED OFF (premise holds outside goldfish too: no interaction of our own); delete `MTG_MAZE_COLOUR_ALT`; v5 fetch weight CLOSED; d5-for-d3 stays Maze's End-only; 5g timing is the coordinator's (after P7); generate the Maze's End overnight GT overnight (after pulling); P2 pending the P1 explanation; prune the opponent-aimed Time Warp in the Extra Turns provider.

16. P2 (2026-11-18 ~13:10 UTC): "Okay, if P2 is redundant, you can delete it." -> cleanup agent deletes `MTG_MAZE_SPEND_NEVERCAST` (byte-identical required; Gates Ablaze's NeverCast stays).
17. Box usage (2026-11-18): "We shouldn't have any cases where we use 6-8 workers." / "We should just run one process at a time." / "(one heavy process)" / "But with full numbers of threads in use." / "Then we can control the cache usage to make sure it fits in memory." / "If anything goes way beyond in cost we should probably identify it and try to fix it." -> agents serialize builds/batches/tiers via `flock build/heavy.lock`, full threads, default cache budget, lower `MTG_MEM_BUDGET_MB` only if a run nears the RSS cap; memory/cost outliers are defects.

## Running (2026-11-18 12:45 UTC)
- P6 (wip/maze-p6, `5ac60ceba`): off-main Maze's End activation; held-out A/B done, being read.
- P5c (wip/maze-p5c): fetch-defer root cause, delete `MTG_MAZE_FETCH_DEFER`.
- P7 (wip/maze-p7): b0 degeneracy + b20 starvation, census-first prunes (excludes P5b's folds).
- Next: P5b (duplicate-copy fold, Exploration land-order fold, Charm tutor-axis duplicate) once a build slot frees; integrate on wip/maze-shared.
- DONE: tie-fix audit (ACCEPTED), scoring fixes, V1, F2, F3, F4, P5/P5-2; shared push 7093daceb (CI green).

## Overnight results so far (2026-11-18 ~17:00 UTC)
- **P6** (wip/maze-p6 `5ac60ceba`, `0d4ebee50`): off-main Maze's End activation. Held-out 70/56, net -15 / 1300 (bank 16/1; respond alone 57/58). Smoke: only mazesend* move, all faster. P6b finishing the mismatch gate, the d8 replays and the accept (its regression run was killed by P5b at 16:33 and is being relaunched).
- **P5b** (wip/maze-p5b `28ddf156a`, `f8a768133`):
  - Fold 1 (duplicate hand copies) already exists. The census's 1.79x is mostly battlefield Maze's End copies, which are not interchangeable (fetch order matters).
  - Fold 2 (Exploration land order, `MTG_FOLD_LAND_ORDER`) is sound but measured worse (2/11 +9; search-only form 1/4 +3); every slower game recovers at d8 b0, so it is a budget effect. It saves ~0.3% units. Default OFF. **USER's rule: decision owed** (amendments: keep the twin the root search reaches first, or demote the twin to a later wave).
  - Fold 3 (Charm c==0) is not an identity (the hand-demand colour vs the P1 front pick).
- **P5c** (wip/maze-p5c `8a5ae0c0f`): root cause = the canon continuation skips board activations, so a two-choice winning line was unreachable at any budget. The fix adds a later-wave dev-seq slot. It FAILS the bar (Maze +11, Snow +1). P5c-2 is attributing it (defer deletion vs the new wave); if the defer did real work: the amended fetch valuation (bank only when no new-name Gate can still be played this turn).
- **P7** (wip/maze-p7): b0 cost is plain branching (dominated plans), not a loop. P5's three "unfinishable" d8 b0 games finish at 2.5-16M units (ceiling now >= 50M). Cast/land dominance fold: fold1 was unsound (a Scrying -> Maze's End line needed the {3}); fold2 + a P6 guard is being measured on top of P6 by P7-2. **Premise for USER sign-off:** a Exploration/Burgeoning/Manabond/Amulet in play is never worse than in hand when main-phase mana is otherwise unspent (off near Heap Gate, Plaza, Summit, an affordable off-main Maze's End).
- **Cleanup** (wip/maze-cleanup): COLOUR_ALT and P2 deleted, Time Warp opponent-target prune; A/B and regression in progress.

## P5 DONE (2026-11-18, P5-2 `deadd9566`, cherry-picked as 5404ebef8)
- Shipped vs pre-P5 engine: 35 better / 14 worse, net -22 turns over 550 held-out games per arm, 18% fewer units at d5 b20. Smoke vs F2: 219/223 byte-identical, only mazesend* move.
- heapstrict (Treasure at the last chance, never a search option) ON: 6/1, -5. max_lands back to 5 (6 was +16). Talon readings (a)/(b)/(c) all worse (+17/+6/+10): Talon stays always offered.
- 14 slower games: 10 recover at d8 b0; 3 hit the 2M-unit b0 ceiling on BOTH arms and recover at d8 b2000 (P7); gi13 recovers at b2000 (base cannot finish at b0).
- Cost: d5 b20 0.81x Hinata; 2HG row 1.52x Hinata's largest row (contended box).
- Open for the USER: sign off heapstrict + its goldfish premise; delete `MTG_MAZE_COLOUR_ALT` (identical to P1)?; the d8 b0 bar is unmeasurable for 3 games until P7 lands.

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

## Scoring fixes DONE and integrated (2026-11-18; wip/tie-scoring -> wip/maze-end 6eb5015ef..efc348d1e)
- Empty tutor / Tooth and Nail whiff, opponent-aimed Time Warp, and Reveillark (by outcome) all score by the state they produce. A cast that gains nothing scores -2 (below pass).
- Held-out searched: Jumbo d5 5b/3w, d3 3b/1w; Extra Turns d5 3b/1w; Melira 0/0. At d0, Jumbo improves 0.97 turns per game.
- All 7 searched slower games recover at d8 b0. Units 3436/3436 on the branch.
- Integrated build NOT yet built (TurnSolver.cpp auto-merged with F2): build, units and smoke at the shared push.
- **New C5 (queued with C1-C4):** `BpCandFingerprint` omits evoke / kicked / bestow / adventure / face_down, so an evoke plan and its hard-cast twin can share a breakpoint-cache entry (the same class as the devour_count hole). Fix + A/B.
- **Open for the USER:** prune the opponent-aimed Time Warp in the Extra Turns provider? It stays searched for the decking line.

## USER on P5's definitions (2026-11-18, VERBATIM)
8. "Leftover means that we would go to the end of the turn and have extra mana up. Technically we might even be able to prune it unless that is the case. "Lacking extra land drops" means something like what you are saying, but the situation is a bit less clear. It is certainly the case that having none of those meets the bar. It is less clear whether 1 of them does. In some situations I suppose it might, particularly if Burgeoning is the one and it is not going off."

Sent to P5-2:
- **Treasure:** offered only when the turn would end with unspent mana. Strict arm vs leftover-or-binds; mana held for an off-main Maze's End activation is not leftover.
- **Talon:** variants (a) none of the three sources, (b) at most one, (c) effective sources only (Burgeoning/Manabond count only with a land to put), each vs always-offered.
9. "Agreed, Maze's end is the one exception that we would rather activate than create a treasure. However, the cost is different, so even with Maze's end out it can be possible to be unable to afford it and able to afford the treasure." -> sent to P5-2: mana is reserved for Maze's End only when its {3}+{T} is affordable from what is left; otherwise the Treasure ({1}, {T} Heap, tap another Gate) stays offered. Unit cases for 2 spare (Treasure offered) and 3+ spare (activation preferred).
10. "But we generally just pass on the treasure unless the mana is going to be wasted, so that is the way to look at this problem. We would much rather advance the win condition in any other way we can if that is available to us." -> sent to P5-2: the Treasure is the LAST RESORT, offered only when no other affordable use of the mana is available (Maze's End activation, Talon put, a castable spell, any other win-advancing activation); this replaces the Maze's-End-only framing of item 9.
11. "Well, to be fair, there are cases where the search doesn't use the mana fully even if the option is available. In those cases it should still activate, since there is no drawback to doing so." -> FINAL Treasure rule sent to P5-2 (replaces 9 and 10). Per plan: the Treasure is never a competing branch. When the chosen plan ends our turn with untapped mana that pays it, the Treasure is ALWAYS made (no-drawback dominance), except for sources needed for an affordable off-main Maze's End activation.
12. "We can actually create the treasure at the very end of processing even after Maze's End if that would help." -> sent to P5-2: one hook at the last opportunity before our untap (end of the opponent's turn, after Burgeoning / off-main Maze's End), both worlds. The Treasure is made if the untapped sources pay it, and the Maze's End reservation logic is removed.

## USER on the owed decisions (2026-11-18 ~22:15 UTC, VERBATIM)
"I suppose P7's prune premise is fine. "Your Exploration land-order merge" I'm not sure what this refers to exactly. For Guild Summit I would prioritize the cards over the treasure here. Unpayable plans I think we could try. I assume all of the plans die at some point anyway, so the only question is whether we can stop them early when they are causing issues. For simple decks with no rituals or equivalent  (cards that add more mana then they cost) like this one it makes sense to drop plans that cannot pay a subset. Let's rescreen the budget-0 situation once all other items finished."

- P7 dominance-fold premise: SIGNED OFF.
- P5b land-order fold (`MTG_FOLD_LAND_ORDER`): explained to the USER; decision still open (default OFF stands).
- Guild Summit B1: no Heap Gate Treasure reservation; Summit's cards come first. Sent to F5-3.
- Unpayable-plan prune: build it for decks with no net-mana-positive cards (drop a plan once a subset of it is unpayable); own A/B + d8 b0 recovery. Sent to F5-3, after the leak fix and joint-pay landing.
- b0 backstop re-screen: after every other item finishes.

## USER on the spare-drop fetch rule (2026-11-18 ~22:30 UTC, VERBATIM)
"Here is my issue with the gate-from-hand idea. Say you have 2 in hand, maze's end and exploration in play and 3 colourless. It seems like you are suggesting that the right play is to use the 2 from hand rather than take advantage of the 3 we have to preserve one for next turn. To me, that seems incorrect."

Agreed: drops recur but hand Gates are finite, and Maze's End enters tapped, so a fetch without a same-turn replay loses an activation cycle. P5d (wip/maze-p5d, off 4ea2268d8) replaces the spare-drop rule with fetch + same-turn replay, root-causes the deletion's d5 b20 +11, A/Bs, accepts.
USER, follow-up (VERBATIM): "It's even worse if Manabond is in play, though I don't know how your rule handles that case. As a side note, you usually do not want to pass the turn without Maze's End on the field because you might need it next turn and you definitely need it to win the game."
(The spare-drop rule ignores Manabond entirely. Sent to P5d: with Manabond, hand Gates should not take drops; prefer ending the turn with Maze's End on the battlefield unless it returns before our next turn or the activation wins.)
USER (VERBATIM): "Fair enough, yeah. If the maze's end is getting dropped by some effect then that would be okay."
USER (VERBATIM): "I think its okay not to always fetch. Fetching costs mana and if we have a lot of land in hand (and no manabond to drop them) it can be worse than playing them out. I don't think it should be ever worse (clairvoyance aside) if we have nothing to do with the mana, though."
(Sent to P5d: the fetch ranks first when the {3} has no other use this turn; otherwise the ranking compares. The "fetch with idle mana" dominance is measured and offered as a USER-sign-off prune, not shipped.)
USER (VERBATIM): "Yes, but there is a point at which we don't have enough lands in hand to keep playing them at full rate this or next turn. If that is the case it should win out over other effects."
(Rule as now stated, sent to P5d: idle mana -> fetch first; lands in hand short of full-rate drops this or next turn -> fetch beats other uses of the mana; otherwise compare. End the turn with Maze's End deployed unless an effect returns it or the activation wins.)
USER (VERBATIM): "Ideally all of this would be searchable, but I'm getting the impression that you see no choice but to prune heavily?"
USER (VERBATIM): "We should be pruning the fetches, not just ranking them."
(Sent to P5d-2: provider prunes (a) idle mana -> fetch forced, target searched, replay if a drop is open; (b) lands short of full rate this/next turn -> no other use of the {3}; (c) no fetch that strands Maze's End in hand at end of turn unless an effect returns it or it wins (own arm, "usually"); (d) otherwise unpruned. Ranking kept for the greedy sites.)
