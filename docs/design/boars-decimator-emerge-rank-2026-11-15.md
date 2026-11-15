# Decimator emerge ranking candidate (2026-11-15)

Status: adopted, default **on** (`MTG_BOARS_DECIMATOR_EMERGE_RANK=0` restores the old order). The user authorized pushing promising Boars improvements on 2026-11-15.

The Boars provider ranks Decimator of the Provinces emerge victims for the depth-0 runner and the viewer menu. Searched turns still enumerate every legal victim and the hard cast, so the candidate cannot remove a searched line. The payer already taps the named victim for mana before sacrificing it in both rollout and executor; the Decimator unit tests cover Sage's three mana and an exact-payment line that leaves Birds untapped.

For each battlefield victim, the solver supplies the provider with its emerge discount, the pumped attack lost to the sacrifice, the victim's later attacks, whether the hard cast can be paid without tapping an attacker, and whether that emerge produces a lethal attack. The lethal check pays on a scratch board, removes the victim, resolves on-cast triggers (including Raggadragga and Herald of Ilharg), applies Decimator's team pump, adds Decimator as a hasty attacker, and projects combat. The provider gives projected lethal the highest priority, then preserves attack damage; a legal hard cast that spends no attacker wins the close comparison.

Paired depth-0 A/B on the rebased Boars source (`boars-integration-next`, 2026-11-15; loss scored as turn 9):

| Cell | Games | Total turns saved | Faster | Slower |
|---|---:|---:|---:|---:|
| Boars smoke, seed 1001 | 1,000 | 53 | 50 | 1 |
| Boars held-out, seed 4004 | 400 | 28 | 26 | 0 |
| Boars 2HG held-out, seed 5005 | 400 | 38 | 35 | 1 |

The two slower games are both one turn: smoke gi194 (T6 to T7) and 2HG gi319 (T5 to T6). Both recover at depth 8 with unlimited budget: gi194 wins T5, gi319 wins T5. Their Decimator lines remain legal; the depth-0 score chooses a different setup cast. The earlier static candidate had twelve slower smoke games; adding an exact lethal projection recovered eleven, and resolving the other cast triggers recovered the held-out Herald and Raggadragga kills.

Focused Decimator tests pass with the flag on and off after the provider refactor (14/14 cases in each arm). A fresh 1,800-game A/B after that refactor reproduced all six prior `.wins` files byte for byte. All ten Boars references replay in strict mode with the flag on: seven exact, three repaired menu indices, no play drift, board divergence, or enumeration gap. Reference 10 retains its turn-4 win and the recorded Ignoble victim remains selectable.

A 50-game Boars depth-3, 10 ms budget searched sample (seed 1001) is identical on/off: average 4.7800, digest `a0bb208b58a253e6`, and identical `.wins` files. This checks the intended scope on one searched cell; it is not a full searched regression sweep.

On the rebased shared branch, a paired 2,050-game smoke plus held-out run reproduced the depth-0 gain: 5.417 to 5.365 average turn over 1,000 smoke games (52 turns saved). Searched play was unchanged with the rank alone. The same run tested an emerge-victim prune; its results and the decision to leave that prune out are recorded in `docs/design/boars-fetch-emerge-prune-2026-11-15.md`.

The official regression tier on the final code confirms the scope: all six searched Boars and Boars 2HG cells match ground truth exactly; the 1,000-game depth-0 cell improves 5.418 to 5.369 (51 faster, three slower, net 49 turns saved). The three slower lines have identical draws: gi93 misses a Horn-before-Decimator setup, gi406 misses Greaves haste on the following turn, and gi936 delays a second Decimator. Those are depth-0 setup choices beyond this victim rank's immediate combat projection. All 854 saved references pass strict replay, with no play drift or enumeration gap.

Artifacts: `build/decimator_provider_{build,unit_on,unit_off,refs}.log`, `build/decimator_provider_ab_{off,on}.log`, `build/decimator_provider_ab_{off,on}_wins/`, `build/decimator_provider_d3_{off,on}.log`, `build/decimator_slow2_d8.log`, and the candidate patch in `build/scratch/boars/emerge_ab/candidate_rebased_provider.patch` within this worktree.
