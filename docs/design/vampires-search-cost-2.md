# Vampires search cost, round 2 (2026-10-21)

Three agents on one brief (`build/scratch/vampires/bench/BRIEF_PERF.md`), base `1a1cf9b9e`, one section each:
R (the pass / overrun structure), P (per-node CPU, byte-identical), B (sound branch cuts). The USER's goal:
*"I want solutions that speed it up without doing mulligan stuff. Look for proper optimizations or branch cuts."*
The decision census and the USER's play rules are `docs/design/vampires-decision-census.md`; the integration is
the last section.

## Agent R -- the pass / overrun structure

Branch `vampires-pass-r` (backup `backup/2026-10-21/vampires-pass-r`). Scratch
`build/scratch/vampires/pass-r/`. All numbers are deterministic work units unless marked.

**Summary.** The pass structure itself wastes little on Vampires (warm-up passes 7%, overrun aborts 3%); the work
thrown away is INSIDE the horizon leaves: 65% of all units were one-ply rollouts whose cutoff is their own start
turn, i.e. that can only ask "is there a kill this turn?" yet ranked every candidate by a full rollout. Four
changes, all default ON, each with a `=0` hatch and a heurarm slot:

1. `MTG_LEAF_KILL_ONLY` -- on a rollout's last allowed turn, the per-turn solves skip every candidate's next-turn
   rollout (exact; generic: every deck that searches a second main without the m2 fixpoint);
2. `MTG_EXH_LEAF_MEMO` -- once a pass has spent its budget, depth-1 no-wins are memoised for the rest of the pass
   (exact for that mode; generic; inert at an unlimited budget);
3. `MTG_KO_M2_NO_REACH` and 4. `MTG_FSL_EDGE_NO_REACH` -- where only a this-turn kill matters, the second main is
   not searched for a deck that vouches it cannot kill after combat (Vampires only, provider opt-in).

Six regression rows: **61.25M -> 20.90M units (0.341x)**, worst row 2HG d5 248,047 -> **78,070** units/game
(0.315x), every row 0.315-0.377x, **0 slower games**, 3 faster, identical play at an unlimited budget (0 verifier
mismatches in 144,209 rollouts). Other decks (changes 1-2): smoke 125/128 and regression 171/178 byte-identical,
0 slower; units 0.83-0.97x on the decks that search a second main. Units are not milliseconds: the remaining work
is candidate APPLIES rather than rollouts, so per-unit CPU may differ -- a callgrind or quiet-box wall check
(agent P's instruments) should confirm the cost-gate effect before anyone quotes a ms figure. Indicative only
(contended box; the arms were interleaved in one pooled batch per step, so the ratio is fairer than the seconds):
core-ms per step 0.574 (1+2) x 0.737 (3) x 0.677 (4) ~= **0.29x** of base -- per-unit CPU fell, not rose.

### R.1 The census: where a decision's units go

Instruments: `MTG_ROLLOUT_STATS` + `MTG_TT_PROBE` over the six searched regression rows (one pooled batch,
600 games, 61,245,932 units, digests identical to the brief's baseline), `MTG_TURN_CENSUS` + `MTG_TRACE=search`
on the worst game (2HG d5 s2002 gi3, 1,992,060 units), and a new `MTG_EXHAUST_PROBE` (below).

**Passes per decision, units per pass.** One FullSearchLine root per turn (the executor replays the committed
line's second main, so main 2 is never re-searched); the ladder runs passes 1..k:

| (2HG gi3, 71 roots) | pass 1 | pass 2 | pass 3 | pass 4 |
|---|---|---|---|---|
| passes run | 71 | 71 | 32 | 1 |
| mean units | 219 | 4,982 | 50,708 | 7 |

Over the 600 games (8,192 ladder decisions): warm-up passes are **7.2%** of ladder units (4.28M of 59.4M), the
committing pass 92.8%. Overrun aborts: **20 of 19,194** pass starts, 1.81M units (**3.0%**; 17 of the 20 kept
their proven line through the anytime rescue). So on Vampires the waste is NOT passes thrown away at the 25x
ceiling (NivMizzetCombo's Q16 shape does not recur here) and NOT the ladder re-deriving earlier passes: pass k+1
can re-derive at most the 7% the warm passes cost.

Why the passes look the way they do: a decision's pass 2 is the subtree the PREVIOUS turn's pass 3 already
searched, and inside a bottoming trial game the leaf table is shared across the trial's decisions
(`m_shared_tt`), so pass 2 is mostly leaf-table hits (4,982 units) -- and the start gate, reading that cheap pass,
admits pass 3, which is genuinely new depth and runs to 10-20x its 18,000-unit budget (the brief's "committed d3
pass running to 10-20x"). The census's `pre` column on a FullSearchLine row is `second_main ? 0 : 1`, i.e. "this
deck searches a second main", not "this is the second main": every root is a pre-combat decision that searches
the whole turn. (Round 1's "second-main roots" is that label.)

**Where the units are.** 90% of all units (55.1M) are inside the depth-1 horizon leaves (`SimulateToEnd` at
`search_leaf_depth` 1: every simulated turn is a one-ply `SolveWithLookahead` that rolls every candidate out
greedily). Of those:

| depth-1 leaf bodies | n | units |
|---|---|---|
| no-win, stored in the leaf table | 182,248 | 26.2M |
| no-win, NOT stored (a truncation or budget bind beneath) | 223,383 | 26.9M |
| win | 11,375 | 2.1M |
| by slack (cutoff - start turn) = 0 | 343,155 | **39.5M (65% of ALL units)**, 2,361 wins (0.7%) |

**Why the memos miss.** `g_decision_epoch` is bumped at every FSLineWin ROOT, i.e. at every PASS, so the enum
memo, the searched-m2 memo, the m1 rollout memo and the breakpoint-enumeration cache are all per-PASS, not
per-decision. Within one pass a state is reached at one remaining depth and FSLineCache / the leaf table catch the
exact repeats first, so the per-pass memos see few repeats: enum memo 118k hits / 7.48M misses (it also promotes
only on a SECOND visit, so a state seen twice is two misses), m1 rollout memo 3,021 / 507,390. Making them
per-decision would recover at most the warm passes' 7%, and FSLineCache keys cannot cross passes at all (turn +
remaining depth is a per-pass constant). The leaf table's keys fold the FULL ordered library, so two bottoming
trials (different cards on the bottom) never share an entry -- cross-hand sharing is impossible by key, and the
only identical-hand case (same bottomed names) is already deduped (`MTG_BOTTOM_NAME_DEDUPE`).

**Spent-budget work.** `MTG_EXHAUST_PROBE` (quick manifest, 13.0M units): about half of the depth-1 leaf and
FSLineWin interior units are computed after the decision budget is already spent (a pass running on toward its
25x ceiling). From that point nothing below binds-free, so the leaf table and FSLineCache store no no-win at all:
65,820 depth-1 leaves (6.09M units) computed in that mode were unstorable, and 47,584 of them were exact repeats of
a no-win the same pass had already computed in that mode (1.22M units, every one with the same answer); 1,458
FSLineWin interior repeats (1.44M units, subtree-inclusive).

### R.2 Change 1 -- `MTG_LEAF_KILL_ONLY` (default ON): the last turn of a rollout needs only a kill

**The thrown-away work.** `SimulateToEndImpl` returns a no-win as soon as the turn passes `cutoff_turn`, so on the
loop iteration where `turn == cutoff_turn` the only thing that can change its answer is a kill THIS turn. That
turn's plan is still chosen by the full per-turn solve (`RolloutPreCombatSolve` -> `SolveWithLookahead`), which
ranks every candidate by rolling it out to the end of the game -- and every searched second main under it does
the same. With `MTG_FS_STRICT_CUT` (a child must beat the incumbent strictly) most horizon leaves are exactly this
case: 343,155 of the 465k depth-1 leaves start ON their cutoff turn, 65% of all units, and 0.7% of them win.

**The identity.** With the scope armed, a `SolveWithLookahead` at that turn (not an enforcing root) scores each
candidate exactly as before up to and including its this-turn kill test (apply, combat, the searched m2) and
returns the first candidate that kills -- which is what the full solve returns, since it returns at the first
killer in candidate order and nothing before that point reads a rollout. A candidate that did not kill skips
`SimulateEndAndStartNextTurn` and its rollout (main loop, breakpoint wave, group wave). If no candidate kills, the
full solve's pick and the kill-only solve's pick are both non-killers, and the rollout replays the pick's turn
through the same apply / combat / searched m2 its own evaluation ran, so neither kills and both return
`max_turns + 1` at the next loop head. Same answer, same leaf-table entry (`bound = cutoff`).

Gates, each where replay parity would otherwise not be exact: the deck searches its second main (candidate
evaluation and rollout then run the same depth-1 `SearchedSecondMainMemoized`); no m2 fixpoint (it re-scans after
the PICK's apply -- Hinata only); no honest teacher; no human play; `cutoff_turn < max_turns` (the natural horizon
exit grades the final state, so there the pick matters). Own death needs no gate: below the horizon the published
grade is kInvalid or kOwnDeath and is never read (the leaf table stores kInvalid for a non-full run, and
`SolveWithLookahead` passes a cutoff below the horizon only when its incumbent already wins, so a no-win candidate
loses on the win turn alone). The first build gated own death and never armed on Vampires, which arms it
(Asylum Visitor, Voldaren Estate).

Memos: a kill-only plan is not the solve's pick, so the m1 rollout memo and the searched-m2 memo store it under a
kill-only namespace; a normal entry answers a kill-only query exactly and is probed first. Code: `LeafKillOnlyOn`,
`KillOnlyScope` (SimulateToEndImpl), the `ko` reads in `SolveWithLookahead`, the two memo hosts. heurarm slot
`LEAF_KILL_ONLY`. `MTG_LEAF_KILL_ONLY_VERIFY=1` re-runs every armed rollout with the scope suppressed and counts
result mismatches.

**Against the no-greedy rule.** Kill-only does not pick among options it did not branch on: every candidate is
still enumerated and run through the only test that can change the answer (its this-turn kill). Among the
non-killers it returns the list's front instead of the rollout-ranked best, and that is an identity fold, not a
heuristic: the rollout's result is `max_turns + 1` whichever non-killer it plays, so all of them are one
equivalence class for the leaf's answer, at every budget. The same holds for change 3 (R.3b). Both are still
raised with the USER (R-Q1, R-Q7) because they sit inside the rollout's searched second main, which the USER ruled
"searched at every depth > 0".

### R.3 Change 2 -- `MTG_EXH_LEAF_MEMO` (default ON): spent-budget no-wins are memoised for the pass

Once a pass has spent its decision budget every later `Exhausted()` test binds (the wave phases ask it at every
solve), so the leaf table stores no depth-1 no-win from then on and every transposition onto one is rolled out
again (R.1). A call that STARTS with the budget spent and not overrun sees every budget test inside it answer the
same way a later identical call would (spent stays spent; Remaining() is 0; the overrun ceiling, checked at exit,
did not trip and the use count only grows), and every memo it reads returns what a recompute returns. So its
result is a function of (state, depth, cutoff) for the rest of the pass: the memo keeps it under its own
namespace, scoped to the pass epoch and the budget object, read only while that budget is still spent and not
overrun, and replays the call's truncation / bind / condemnation deltas so a caller's watermark reads exactly what
a recompute would show. Depth >= 1 only (a depth-0 rollout binds nothing and is stored already). Never active at
an unlimited budget. heurarm slot `EXH_LEAF_MEMO`.

Not extended to FSLineWin interior nodes (1,458 interior repeats, 1.44M units on the quick rows before change 1):
there the order-free reuse wave reads a twin's hint whenever `Remaining()` is 0, and hints accumulate during the
pass, so a later recompute of an interior node can legitimately differ from the first -- the identity above does
not hold.

### R.3b Change 3 -- `MTG_KO_M2_NO_REACH` (default ON, provider-vouched): no m2 search in a kill-only turn

With rollouts gone (R.2), a kill-only turn's cost is its kill test, and most of THAT is the searched second main
under every m1 candidate: on std d3 s2002 gi7 the kill-only turns applied 1,931 m1 candidates and 23,013 m2
candidates (53% of the game's 43,047 units). Inside a kill-only turn that m2 search -- under each candidate's
evaluation and after the pick -- is never ranked for the future; it exists only to find a post-combat kill. A
deck that has none needs no search there: `DecisionProvider::PostCombatCannotKillOptIn` (default false) lets a
deck vouch that nothing it can cast, activate or trigger after combat makes an opponent lose, and with it
`SolveSecondMainInSearch` returns the empty plan in a kill-only turn without enumerating. Every m2 plan's kill
test was false; so is the empty plan's; the leaf answer is unchanged. Vampires opts in (its mainboard reaches the
opponent only through combat damage; reading in `VampiresProvider`). This is a deck-vouched dominance in the shape
of round 1's rule 9 -- squarely next to agent B's lane -- kept as its own lever and its own question (R-Q7).
heurarm slot `KO_M2_NO_REACH`.

### R.3c Change 4 -- `MTG_FSL_EDGE_NO_REACH` (default ON, same vouch): the full-line search's edge nodes

The full-line search's twin of R.3b. `FSLineTail` at a node on its cutoff turn (`turn >= cutoff`, with
`cutoff <= max_turns`): every m2 plan there either kills NOW or hands to `FSLineWin` at turn+1, which the
`turn > cutoff` guard refuses outright, so the branching can only return a this-turn kill or `{max_turns + 1, {}}`
(FSLineTail's `best` starts there and improves only on a strict win). A cutoff below the horizon exists only
because some ancestor already holds a win (each pass starts at `max_turns + 1`; `FsChildCut` tightens only from a
found win), so the no-win line is never adopted -- which is why this is not the winless certificate's
play-quality hole (that one fires where no incumbent exists). Under the deck's vouch no post-combat plan kills, so
the node's answer is `{max_turns + 1, {}}` and it is returned without the branching. After R.3b, `fs_main2` was
36% of all units. Gated off with the m2 fixpoint and in human play. heurarm slot `FSL_EDGE_NO_REACH`.

### R.4 The benchmark (one pooled batch, 1,800 games, three arms per row; control = `1a1cf9b9e` behaviour)

| row | base u/g | kill-only u/g | + memo u/g | ratio | digest (both changes) | win turns |
|---|---|---|---|---|---|---|
| 2HG d5 s2002 x50 (worst) | 248,047 | 190,626 | **173,811** | 0.701 | SAME `45ce2008fa2885e3` | 50/50 identical |
| 2HG d3 s2002 x100 | 140,298 | 113,885 | **102,434** | 0.730 | SAME `81b004286de687c8` | 100/100 identical |
| std d5 s3003 x75 | 124,147 | 99,831 | **92,043** | 0.741 | moved `cf2f06007179661c` | 1 faster (gi40 5->4), 74 identical |
| std d5 s2002 x75 | 82,078 | 63,413 | **60,619** | 0.739 | SAME `2cdc42ec48de0ce6` | 75/75 identical |
| std d3 s3003 x150 | 71,370 | 59,342 | **55,153** | 0.773 | SAME `20c06430dede6629` | 150/150 identical |
| std d3 s2002 x150 | 57,609 | 48,270 | **45,164** | 0.784 | moved `c76d71d9d6d87ec9` | 150/150 same turn; gi90 play differs at T4 |
| all six | 61,245,932 | 49,304,882 (0.805) | **45,431,203 (0.742)** | | | **0 slower** |

The control arm reproduces the brief's baseline digests and units exactly. The memo on top of kill-only is
byte-identical on all six rows (-7.9% units). Kill-only alone moves two rows (the freed units are re-spent).

**With change 3 (`MTG_KO_M2_NO_REACH`)**, a second pooled batch (the changes-1+2 arm re-run in it reproduces the
table above exactly, digests and units):

| row | base u/g | all three u/g | vs base | vs 1+2 | digest vs base | win turns vs base |
|---|---|---|---|---|---|---|
| 2HG d5 s2002 x50 (worst) | 248,047 | **98,889** | **0.399** | 0.569 | SAME | 50/50 identical |
| 2HG d3 s2002 x100 | 140,298 | **63,177** | **0.450** | 0.617 | moved | 100 same turn (gi28 other line) |
| std d5 s3003 x75 | 124,147 | **97,983** | 0.789 | 1.065 | moved | 2 faster (gi40, gi12), 73 same |
| std d5 s2002 x75 | 82,078 | **53,792** | 0.655 | 0.887 | SAME | 75/75 identical |
| std d3 s3003 x150 | 71,370 | **43,282** | 0.606 | 0.785 | moved | 1 faster (gi12), 149 same |
| std d3 s2002 x150 | 57,609 | **34,013** | 0.590 | 0.753 | moved | 150 same turn |
| all six | 61,245,932 | **34,239,548** | **0.559** | 0.754 | | **0 slower, 3 faster** |

std d5 s3003 is the one row whose units ROSE over changes 1+2 (1.065): the freed units let the start gate admit
deeper passes there (one more game faster). It is now the deck's most expensive row by a hair (97,983 vs 2HG d5's
98,889 per game). Unlimited budget, std d3 s5005 x24, changes 1+2 vs all three: digest `04fe772eda2ac241` both
(= base); units 7.75M -> **3.97M** (base 9.11M: so 0.44x of base at an unlimited budget too).

**With change 4 (`MTG_FSL_EDGE_NO_REACH`)**, a third pooled batch (the changes-1-3 arm in it reproduces the
table above exactly):

| row | base u/g | all four u/g | **vs base** | vs 1-3 | digest vs 1-3 | win turns vs base |
|---|---|---|---|---|---|---|
| 2HG d5 s2002 x50 | 248,047 | **78,070** | **0.315** | 0.789 | SAME | 50/50 identical |
| 2HG d3 s2002 x100 | 140,298 | **47,506** | **0.339** | 0.752 | SAME | 100 same turn |
| std d5 s3003 x75 | 124,147 | **39,781** | **0.320** | 0.406 | SAME | 2 faster, 73 same |
| std d5 s2002 x75 | 82,078 | **29,550** | **0.360** | 0.549 | SAME | 75/75 identical |
| std d3 s3003 x150 | 71,370 | **25,240** | **0.354** | 0.583 | SAME | 1 faster, 149 same |
| std d3 s2002 x150 | 57,609 | **21,744** | **0.377** | 0.639 | moved (gi90) | 150 same turn |
| all six | 61,245,932 | **20,901,552** | **0.341** | 0.633 | | **0 slower, 3 faster** |

Change 4 is byte-identical to changes 1-3 on 5 of 6 rows; its one mover, std d3 s2002 gi90, now plays at the
shipped budget exactly the line every arm plays at `--budget-ms 0` / 200 (`1da7cd836415eeff`). Unlimited budget,
std d3 s5005 x24: digest `04fe772eda2ac241` (= base), units 136,804/game (base 379,464: **0.36x**). Every row is now
under a third of its base cost but one (0.377); the worst row is 2HG d5 at 78,070 u/g (base 248,047).

**The 11 games change 3 moved, at large budget** (single-game jobs, changes 1+2 vs all three; std at `--budget-ms
0`, the 2HG one at 200): all 11 play the IDENTICAL line in both arms (per-game play digests equal). The faster one,
gi12 s3003 (T5 -> T4 at both d3 and d5), plays exactly the unlimited-budget line `bda23e51fee34871` at the shipped
budget with change 3; without it the shipped budget stops at T5. (gi133 d3 s3003 is T5 at b10 in BOTH arms and T4
at b0 in both -- budget-starved either way.)

### R.5 Soundness evidence

**Unlimited budget (`--budget-ms 0`), d3, one pooled batch, both changes vs off, with
`MTG_LEAF_KILL_ONLY_VERIFY=1`:** std s5005 x24 digest `04fe772eda2ac241` both arms, 2HG s5105 x24 digest
`d4b1c792305389c5` both arms -- 48/48 games identical in win turn AND play digest; the verifier re-ran all
**144,209** armed rollouts with the scope suppressed: **0 mismatches**. (The arm's units there include the
verifier's reference runs, so they are not a cost figure.)

**Other decks -- smoke tier with both changes ON vs the committed GT (`1a1cf9b9e`; NOT accepted):** 125 of 128
configs byte-identical; 3 moved, all at equal or better score: antilife d3 4.1800 -> 4.1760 (1 faster),
bruna d5 4.9200 -> 4.9067 (1 faster), fivecolour2hg d3 5.4250 unchanged (gi37 same T5, other line). Searched:
0 slower, 2 faster, 2 play-changed at the same score; d0: nothing moved (the changes need a depth-1 rollout).
The kill-only scope arms only on decks that search a second main (`GoldFishRunner::DeckUsesSecondMain`) without
the m2 fixpoint; the memo arms on any deck once a pass has spent its budget.

**Other decks -- units, one pooled two-arm batch over the 62 searched smoke rows (d3 s1001, 14,370 games):**
60 of 62 rows byte-identical play between the arms (the two movers are the smoke movers above); units 60.27M ->
57.92M (0.961). The decks that search a second main drop 9-17% (antilife 0.894, antilife2hg 0.911, bruna 0.910,
bruna2hg 0.896, burn 0.863, fivecolour 0.908, fivecolour2hg 0.935, fungusv2 0.831, fungusv22hg 0.893, giants
0.909, goblins 0.880, goblins2hg 0.869); Hinata (m2 fixpoint, so kill-only never arms) 0.97-0.98 from the memo
alone; every other deck 0.994-1.000.

Changes 3 and 4 engage only behind `PostCombatCannotKillOptIn` (Vampires). The smoke tier re-run on the final
binary (all four) is row-for-row identical to the changes-1+2 smoke above (125/128, the same 3 movers). Full unit
suite on the final tree: **1665/1665 pass** (the 8 new cases in `test/unit/test_leaf_kill_only.cpp`).

**The two benchmark movers at large budget** (single-game jobs, both arms, `--budget-ms 0` and 200):
std d5 s3003 gi40 (base T5 -> both changes T4): at b0 AND b200 both arms win T4 with play digest
`4fc6697f47ee23f2` -- exactly the line the changed arm finds at the shipped b20, i.e. the base was budget-starved
and the freed units reached the large-budget line. std d3 s2002 gi90 (same T4, different line): both arms play the
identical line `1da7cd836415eeff` at b0 and b200; at the shipped b10 neither arm plays it (base `8f23b8a55427584b`,
changed `7e67096009935a49`), so the shipped-budget difference is churn at an equal score.

### R.6 What is left (post-change census, same six rows, both changes on)

45.4M units (was 61.2M). `la_cand` is now **70%** (31.9M; one unit per candidate APPLIED in a one-ply solve, m1 or
m2), `fs_main2` 11%, rollout turn-steps + greedy fallback 9% (were 32%). The slack-0 leaves still hold **24.3M
units (53%)**: with their rollouts gone, what is left there is every m1 candidate's apply + combat + searched m2,
whose kill test needs the full candidate list -- only a sound "no kill this turn is reachable" bound (a damage
ceiling) could remove that, which is a branch cut (agent B's lane; a question below). Overrun aborts: 9 of 19,540
pass starts (2.0% of units). Warm-up passes: 10.9% of ladder units. Units spent after the decision budget was
already used up: **14.2M (31%)**. Memos: spent-budget leaf memo 119,615 hits / 66,856 stores; kill-only
namespace 5.46M memo entries; m1 rollout memo still ~0.8% hits (per-pass epoch, R.1).

Two candidates measured and dropped: a RESUMABLE rollout (a later query with a wider cutoff resuming where a
narrower no-win stopped) would save at most 0.6% -- `MTG_TT_PROBE`'s new STALE counter: 52,588 depth-0 recomputes,
282k units, 58% of their turns a re-walk; 29 at depth 1. And the m2 empty plan inside a kill-only turn (2.32M of
the 22.08M m2 candidates) is NOT provably a no-op (an empty plan's apply still runs the dig loop), so it is not
skipped on its own -- R.3b removes the whole m2 search where the deck vouches instead.

**After change 3** (same rows, 34.2M units): `fs_main2` is now the largest site, **36%** (12.5M; the full-line
search's second-main branching, which the freed budget lets deeper passes do more of), `la_cand` 34% (11.5M),
rollout turn-steps + greedy fallback 14%; slack-0 leaves 6.6M (19%); kill-only turns skipped 7.57M m2 searches;
0 overrun aborts in 20,672 pass starts; warm-up passes 16% of ladder units; units after the budget was spent
6.9M (20%). The fs_main2 share is what change 4 (R.3c) goes after.

### R.7 The regression tier (both changes ON vs the committed GT; NOT accepted)

171 of 178 configs byte-identical. 7 moved, none worse: fungusv2 d3 s3003 4.5750 -> 4.5700 (1 faster); antilife
d3/d5 s2002, fivecolour d3 s2002/s3003, bruna d3 s2002/s3003 same avg, digest moved. Searched: **0 slower**, 1
faster, 8 play-changed at the same score (antilife gi119/gi284/gi46, bruna gi59/gi65/gi64, fivecolour gi62/gi78);
d0: nothing. Reference replay: 219 ok, 410 repaired, **1 play-drift = CritterLifegain/claude_s6_gi5**, the one
listed as pre-existing in round 1 (ledger question 27) -- see R.8 for the base-binary check.

The CritterLifegain drift reproduces identically with the base binary (`1a1cf9b9e`, `--only claude_s6_gi5`:
replay T4 vs ref T5, 11/26 decisions unused), so it is pre-existing and not caused here.

### R.8 Open questions for the USER (work continued on the stated default)

- **R-Q1 -- `MTG_LEAF_KILL_ONLY`, an exact identity shipped DEFAULT ON.** Same leaf answer by construction
  (R.2), 0 mismatches over 144,209 verified rollouts and identical play on 48 unlimited-budget games; under a
  budget it frees units (-19.5% on the six rows). It touches every deck that searches a second main without the
  m2 fixpoint (smoke: antilife, bruna, burn, fivecolour, fungusv2, giants, goblins, vampires). [default: ON]
- **R-Q2 -- `MTG_EXH_LEAF_MEMO`, DEFAULT ON.** Byte-identical on all six rows on top of R-Q1 (-7.9% more), inert
  at an unlimited budget. [default: ON]
- **R-Q7 -- `MTG_KO_M2_NO_REACH` + `MTG_FSL_EDGE_NO_REACH`, two DECK-VOUCHED cuts shipped DEFAULT ON (Vampires
  only).** Both rest on one provider claim, `VampiresProvider::PostCombatCannotKillOptIn`: nothing in the
  mainboard makes the opponent lose outside combat (reading in the provider; the same sign-off shape as round 1's
  rule 9). Under it each is an exact identity (R.3b, R.3c). Together -54.0% units on top of changes 1+2 (45.43M -> 20.90M), 0 slower,
  1 more faster, identical play at an unlimited budget. They sit next to agent B's lane (deck-vouched dominance);
  any card added to the list with non-combat reach must turn the opt-in off. Sign-off on the reading? [default: ON]
- **R-Q3 -- a third of the units are spent after the decision budget is gone (a SETTINGS question, nothing
  changed).** After both changes, 14.2M of 45.4M units (31%) are consumed by passes that already used their
  budget. The mechanism is the start gate reading a cheap pass 2 (mostly leaf-table hits from the previous
  turn's search inside a bottoming trial) and admitting pass 3, which then runs to 10-20x its budget before the
  25x ceiling; aborts are rare (9 of 19,540 passes) because the passes finish under 25x. Options are budget
  semantics (the growth estimate, the ceiling), so per "A BUDGET MAY RE-PRICE WORK..." they are the USER's: e.g.
  pricing a pass's cache hits into the gate's growth estimate, or a lower overrun multiple for passes whose
  predecessor was mostly hits. Not measured. [default: unchanged]
- **R-Q4 -- the next 53% is a branch-cut question (agent B's lane).** With rollouts gone, the slack-0 leaves'
  remaining cost is the this-turn kill test itself (every m1 candidate's apply + combat + searched m2). A sound
  ceiling on this turn's reachable damage would let a leaf answer "no kill" without the list. [default: not
  built]
- **R-Q5 -- per-pass memo scope.** `g_decision_epoch` advances at every pass, so the enum / searched-m2 / m1
  rollout memos never cross passes. Widening them to the decision is bounded by the warm passes' 7-11% and keys
  that fold the remaining depth cannot cross passes anyway; not pursued. [default: unchanged]
- **R-Q6 -- a disclosure, not a change.** The WIN halves of the leaf table and FSLineCache store a win found by a
  spent-budget (truncated) computation, and inside a bottoming loop the leaf table lives across a trial game's
  decisions, so a later decision with a fresh budget can be served a win a full search might have beaten. Same
  shape round 1 lived with; noted because it is the one memo path here that is not budget-free. [default:
  unchanged]


## Agent P -- per-node CPU (byte-identical speedups)

Branch `vampires-perf-p` (backup `backup/2026-10-21/vampires-perf-p`), scratch
`build/scratch/vampires/perf-p/`. Every change here is BYTE-IDENTICAL by construction: the same selections
reach the same predicates in the same order, the same keys are built, the same plans come out -- only fewer
instructions are spent getting there. Units are therefore unchanged on every row, and the digests are the
proof. Each change has an `EnvOn(..., true)` hatch (`=0` restores the old path); none changes play, so
none has a heurarm slot.

### P.1 Where the CPU goes (callgrind Ir, Profile build, single thread, base `1a1cf9b9e`)

Three reference games, one per row shape (`scripts/cg.sh`; seeds are `base + gi`):

| game | units | Ir | Ir / unit |
|---|---|---|---|
| std d3 b10 s3003 gi48 (seed 3051) | 63,081 | 10.54 G | 167 k |
| 2HG d5 b20 s2002 gi10 (seed 2012) | 234,051 | 25.35 G | 108 k |
| std d5 b20 s3003 gi23 (seed 3026) | 247,604 | 55.77 G | 225 k |

The profile is flat in the generic engine (the simulation itself -- ApplyPlanDirect, combat, mana payment --
is ~30% and spread over hundreds of functions), but four Vampires-shaped costs stand out:

1. **The subset walks' per-position bookkeeping** (EnumeratePlans' and the greedy Solve's odometer):
   self cost 7.3% / 2.3% (2HG), 12.0% / 4.8% (d3), 1.2% / 13.4% (d5). Falkenrath Pit Fighter's ~100-member
   activation group multiplies every other digit; the walk summed the selected options' mana values per
   position (an O(groups) loop), scanned the digits again for the radix cut on every refused position, and
   rebuilt `sel` from scratch per independent bitmask.
2. **SubsetVampOutletInvalid**, the Vampires subset rules: **29.1% of the d5 game** (10.9M calls, 10.4M of
   them from the greedy rollout walk), 10.3% of d3, 2.3% of 2HG. Almost none of it was the rules'
   arithmetic: rule 8 copied every selected discard outlet into a `vector<Action>` and stable-sorted the
   copies (12.5% of d5 by itself), rule 3 resolved the provider and both actions' ranks per PAIR through
   TrailingRunsBefore and copied the subtype string per call (7.0%), rule 1 built a vector of heap strings
   per selection, rules 7 / 10 re-walked the battlefield and re-looked-up madness pins by name.
3. **SubsetHasDuplicateSacSource**: 5.3% of d5, 2.6% of d3 -- nine clause-opening tests per selected
   action on scattered Action fields, for a deck where only one clause (the {T}-ability one) can fire.
4. **CollectActions' Vampires emission**: 25.6% of the 2HG game inclusive. Per discard-cost source it re-ran
   the provider's cleanup ranking (ActivationDiscardClasses, 2.5%), per pin it copied the whole GameState
   for the madness fan's probe and re-derived the turn's optimistic mana (MadnessFanFor 3.3%), interned
   the pin name once per (victim class x pin) variant (1.4M interns, 0.7%), looked definitions up by string
   (1M hashes, 0.7%), copied every madness variant twice (PushWithMadnessFan 2.6%), and read one env flag
   per call (0.2%). Captivating Vampire's steal fan recursed through a `std::function` (3.5% of d3).

Plus three generic ones the Vampires games exposed: BpEnumBuildKey re-hashed the 175-slot heurarm array
through its TLS wrapper on every key (29M `__tls_init` calls, 1.8% of 2HG); ApplyPlanDirect re-derived two
pre-plan activation snapshots for every sibling candidate of one root (3.5% of 2HG); MoveOrderPlans
shuffled whole Plans through a merge sort (~1.5% of 2HG in Plan moves).

### P.2 The changes (all byte-identical; flag `=0` restores the old path)

All in `src/ai/TurnSolver.cpp` unless named; the soundness argument is in the code comment at each site.

| # | change | flag | why it is the same computation |
|---|---|---|---|
| 1 | **The plain subset walk as a pruned depth-first walk** (`PlainOdometerDfs`, both walkers' `!(any_ritual \|\| any_rock)` branch) | `MTG_WALK_DFS` | The carry walk visits positions in lexicographic order, last digit most significant; a DFS that fixes digit G-1 first visits them in the same order. The high part's sum is kept per depth (O(1) per step) and a subtree whose high part already exceeds the bound is skipped -- every option costs >= 0, so every position in it is refused (exactly what the radix cut jumps over or refuses one at a time). Surviving positions run the carry walk's independents loop verbatim (same `sel` layout). Only the plain scalar walk; any per-position predicate (equip copy / piece dependency / fold prefix / metalcraft bound / verify or diagnostic mode) keeps the carry walk. The greedy walk stops once its budget is spent (every later consider() returned at `walk_exhausted`). Unit test: 400 random instances, identical sequences. |
| 2 | **VampRulePre**: SubsetVampOutletInvalid's frozen-board facts cached per enumeration (one `VampCandFacts` per candidate: def, trailing rank / band, outlet draws / cost, pin hand counts, per-subtype arrival deltas, rule-4 fold keys; rule 7's board bodies per colour), the rules evaluated on them | `MTG_VAMP_RULE_PRE`, `MTG_VAMP_RULE_PRE_LAZY=K` (8), `MTG_VAMP_RULE_PRE_VERIFY` | Every rule is a pure predicate of (state, cands, sel) and the filter returns their OR, so evaluation order and where the inputs come from cannot change the answer; each cached field names the exact expression it caches (rule 8's stable sort of Action copies becomes a stable insertion sort of indices on the same keys). Built lazily on the K+1-th call of an enumeration (a 2HG rollout's greedy Solve averages ~3 calls, where the build would cost more than it saves). VERIFY runs both paths on every selection; unit tests compare 24k random selections over real CollectActions lists. |
| 3 | **SubsetHasDuplicateSacSource entry skip**: per-candidate bits for each clause's opening test, built lazily | `MTG_DUP_SRC_SKIP` | A candidate that opens no clause is skipped; clauses, their order and their `dupclause::Hit` attribution are unchanged. Built during the walk, it reads the tags after RenumberFoldOrds (which only clears tags). |
| 4 | **BpEnumBuildKey's arm hash cached per thread** on the arm's bytes | `MTG_BP_ARM_HASH_CACHE` | The same fold of the same bytes; the loop had read the TLS-wrapped `heurarm::t_arm` 175 times per key. |
| 5 | **CollectMemo** (per-CollectActions-call): ActivationDiscardClasses per source definition, OptimisticTurnMana, the madness fan per (pin, outlet cost, source, sacrifice, scope), MadnessFanFor's probe board copied once per call (erase / re-insert per pin) | `MTG_COLLECT_MEMO` | Pure functions of the call's const state and their arguments; a helper uses the memo only when handed that call's state object; `g_axis_madness_vars` is bumped exactly as the uncached path bumps it. |
| 6 | **By-name lookups on the interned index** (`CollectedActionDef`) | `MTG_LOOKUP_INTERNED` | `LookupInterned` is the pointer-keyed index over the same `m_cards`, rebuilt at every load / Register; interning is canonical, so it returns exactly `Lookup(name)`. |
| 7 | Falkenrath Pit Fighter's pins / victims interned once per source, not per variant | -- | The same canonical pointers. |
| 8 | **EmitStealVariants**: the count-distribution recursion as a direct lambda (was `std::function`) that skips a subtree whose remaining classes cannot absorb `left` bodies | `MTG_STEAL_REC_PRUNE` | Every pruned leaf fails `left == 0`; leaves and their order are unchanged. |
| 9 | `stealspec::Intern` memoised per thread in front of the shared mutex (`src/core/SpellEffects.h`) | -- | A spec's id never changes once assigned; keys fold the spec CONTENT. |
| 10 | `PushWithMadnessFan` builds the LAST variant in place and moves it | -- | The same actions in the same order, one copy fewer per fan. |
| 11 | **The pre-plan facts share**: ApplyPlanDirect's two pre-plan activation snapshots computed once per sibling loop (lookahead pre/post-combat, FSLineTail main 2, FSLineWin) | `MTG_PREPLAN_SHARE`, `MTG_PREPLAN_SHARE_VERIFY` | The siblings are fresh copies of one root board; a loop arms the share only for the copy it is about to apply, the apply takes it at entry, and each fact is computed by the first sibling at the point it always was. VERIFY recomputes and aborts on a difference. |
| 12 | **MoveOrderPlans sorts (keys, index)**, then moves each Plan once (not at all when already in order) | `MTG_MOVE_ORDER_KEYS` | The comparator reads five scalars, so the stable sort of keys is the stable sort's permutation. |
| 13 | small: `enumstats::Enabled()` read once per walk; `MTG_EQUIP_ALL_HOSTS` cached; CreatureConvokeArms' vectors reserved; Action-name lookups (`Lookup(x.card_name)`, 149 sites) on the interned index; VampiresProvider's discard tiers computed once per name before its stable sort (`src/ai/DecisionProviders.cpp`) | -- | Process-constant reads; capacity only; `LookupInterned` == `Lookup` (row 6); the same stable permutation. |
| 14 | **The walkers' Vampires prefilter**: once an enumeration's VampRulePre is built, both DFS walks ask SubsetVampOutletInvalid themselves and do not hand a refused selection on | `MTG_VAMP_PREFILTER` | The receivers' chains are pure rejection predicates (an order-free OR) that this rule belongs to; what consider() does before reaching a refusal is replayed (its abandon-poll cadence; the greedy walker sorts first exactly as consider() does) or fenced off (walk budget charge, per-decision meter, every census / trace / funnel instrument, the payoff prune, human play, the VERIFY mode). |

### P.3 Evidence

**Byte identity.** Every round re-ran, on the round's own binary: `mtg-test` (1,660 cases, including the new
`test/unit/test_vampires_perf_p.cpp`: 400 random walks DFS == carry walk; 24k random selections over real
CollectActions lists, fast rules == original rules); the `quick.json` manifest (110 games) with
`MTG_VAMP_RULE_PRE_VERIFY=1 MTG_PREPLAN_SHARE_VERIFY=1 MTG_COLLECT_MEMO_VERIFY=1` (every selection / sibling
apply / memo hit checked against a recompute -- no abort); the seven `regression_rows.json` rows (digests,
units AND the per-game `.wins` files identical to the brief's baseline); and `test/regression.sh --smoke`
against the committed GT with the binary pinned (`MTG_BIN`): **128 passed, 0 failed** -- the smoke tier is
the check that the generic changes (the DFS walk, the dup-source path, the arm hash, the pre-plan share,
MoveOrderPlans, the interned lookups) leave every other deck byte-identical too.

**Instructions per game (callgrind Ir, Profile build, single thread; units identical in every column).**

| game (row shape) | units | base `1a1cf9b9e` | v3b `b1eccb0d9` | v4 `77f338450` | v5 `a70065613` | v6 `dcb857488` |
|---|---|---|---|---|---|---|
| std d3 b10 s3003 gi48 | 63,081 | 10.54 G | 8.61 G (-18.3%) | 8.39 G (-20.4%) | 8.19 G (-22.3%) | **8.12 G (-23.0%)** |
| 2HG d5 b20 s2002 gi10 | 234,051 | 25.35 G | 22.15 G (-12.6%) | 22.10 G (-12.8%) | 21.98 G (-13.3%) | **21.92 G (-13.5%)** |
| std d5 b20 s3003 gi23 | 247,604 | 55.77 G | 41.54 G (-25.5%) | 38.60 G (-30.8%) | 37.52 G (-32.7%) | **36.88 G (-33.9%)** |

Where the d5 game's instructions went (self Ir, base -> v5): the greedy walk 7.48 G -> 0.38 G (+ the DFS and its leaf
4.6 G, which now also carries the prefilter); SubsetVampOutletInvalid and its helpers ~16.2 G inclusive -> 4.2 G (SubsetVampOutletInvalidPre);
SubsetHasDuplicateSacSource 2.95 G -> 0.29 G (the compact path, and the prefilter now refuses most selections before it);
consider() 10.4M calls -> 1.6M (the prefilter). The 2HG game moved least because its cost is the generic
engine: BuildSimKey (7.5% of it), CollectActions' own body, the simulation -- see P.4.

**Per row** (units/game are unchanged by construction; the Ir change is the reference game of the row's
shape -- one game per shape, so read it as an estimate for the row; expected wall change ~ the Ir change,
since nothing here trades instructions for memory traffic):

| row | units/game | digest (unchanged) | Ir, reference game | est. speedup |
|---|---|---|---|---|
| vampires2hg_regression_d5_s2002 (the worst) | 248,047 | 45ce2008fa2885e3 | -13.5% (2HG d5 gi10) | ~1.16x |
| vampires2hg_regression_d3_s2002 | 140,298 | 81b004286de687c8 | ~-13% (2HG shape) | ~1.15x |
| vampires_regression_d5_s3003 | 124,147 | da10a7163b53fc9a | -33.9% (std d5 gi23) | ~1.5x |
| vampires_regression_d5_s2002 | 82,078 | 2cdc42ec48de0ce6 | ~-30% (std d5 shape) | ~1.4x |
| vampires_regression_d3_s3003 | 71,370 | 20c06430dede6629 | -23.0% (std d3 gi48) | ~1.3x |
| vampires_regression_d3_s2002 | 57,609 | c1dcca8497cb0507 | ~-23% (std d3 shape) | ~1.3x |

A paired WALL A/B (base and v6 binaries running the seven rows concurrently, 10 threads each, the box shared
with another container) gave per-row core-ms ratios from 0.85 to 1.17 with no consistent sign -- the
brief's warning, measured: wall is not a measurement on this box, Ir is.

### P.4 What remains, and what was not done

- **2HG is now generic-engine bound.** After the changes the 2HG d5 game's top items are BuildSimKey (7.5%,
  the TT / memo / dedup keys -- a 128-bit fold chain whose key values must not change), CollectActions' own
  body (3.6%), FinalizeFoldTags + ActionFoldSig (2.3%), CreatureConvokeArms (1.3%), the simulation itself.
  No Vampires-shaped cost above 1.6% is left there.
- **The provider's discard-pin ORDER costs ~2.7% of a 2HG game.** ActivationDiscardClasses asks
  VampiresProvider::ActivationDiscardCandidates, which starts from VialProvider's cleanup ranking -- the
  Slivers bucket policy (`vialcurve::Fill`, a three-turn curve fill) inherited through VialProvider -- once
  per discard-cost source per CollectActions call. It only ORDERS the pins (every pin is still branched), so
  a cheaper order is a provider decision (USER-owned) and was not touched; see the question below.
- **The greedy walk's refused leaves (d5).** A d5 game's rollout walk still builds ~10M selections, 86% of
  which the Vampires rules refuse (the prefilter refuses them before consider(), but each still costs the
  selection build, a sort and the rule pass, ~1,000 instructions). The next step would be to not generate
  them: rule 1 (over-pinned discards) is monotone in the selection -- a high part of the odometer that
  already over-pins a card refuses every completion -- so the DFS could skip that subtree, with the same
  fencing as the prefilter. Not built (time; it is the one change here that alters the walk's shape).
- `MTG_VAMP_RULE_PRE_LAZY` (K = 8) was set from the call-per-enumeration counts (2HG greedy ~3, d5 greedy
  ~125, search walks 10-85), not swept.

### P.5 Commits (branch `vampires-perf-p`, backup `backup/2026-10-21/vampires-perf-p`)

Checkpoint commits on top of `1a1cf9b9e`, each built and verified as a round (P.3): `bbb5574ee` + `46a26262d`
+ `cc3418dcb` (round v2: DFS walk, VampRulePre, dup skip, arm hash, collect memo, steal recursion, fan
moves, pre-plan share), `fa13b35e1` .. `b1eccb0d9` (v3b: MoveOrderPlans keys, fan memo, lazy pre, interned
lookups, tier keys, memo verify), `77f338450` (v4: compact dup path, single-pass fast rules, lazy rule 4,
DFS leaf by index), `a70065613` (v5: the walkers' prefilter, high digits cached), `dcb857488` (v6: put-name
filter, high part by index), then this document. Every round: digests, units and per-game `.wins` identical on all seven rows,
`quick.json` identical with every VERIFY mode on, `mtg-test` 1,660/1,660, smoke 128/128 PASS.


## Agent B -- branch cuts (branch `vampires-cuts-b`, commits `e8f300134` code + this doc)

### B.1 Where the units are

Units are deterministic; wall on this shared box is not a measurement. Per-decision census
(`MTG_TURN_CENSUS`) on 2HG d5 s2002 gi22 (seed 2024), the worst row's typical game:

- **99% of the units are mulligan-bottoming trial games** (`probe=1`), and inside them the **second main of
  turns 1-3** (`FullSearchLineHybrid`, `pre=0`): T3 55%, T2 25%, T1 15%.
- By unit site: `la_cand` 51% (one unit per candidate the lookahead scores), `rollout_step` 20%,
  `greedy_fallback` 18% (the depth-0 greedy solve inside a playout), the rest < 5% each.
- **Almost all `la_cand` candidates are scored INSIDE a playout.** The horizon rollout plays every
  future turn with a 1-ply `SolveWithLookahead` (the engine default `search_leaf_depth` = 1), which scores
  every candidate with a nested greedy playout. `MTG_DEDUP_CENSUS` on the 2HG d5 row reads the breakpoint
  wave-0 arms as in-playout 444,500 vs searched 87.

`MTG_VAMP_CAND_CENSUS` on 2HG gi29 (no breakpoint variants in that game): main 2 plain 28.6%, main 1 plain
20.7%, main 1 convoke 13.8%, main 2 Blood 12.2%, main 1 Blood 7.4%, every convoke class together 29.5%.
11% of the charged candidates land on a post-apply state an earlier sibling already reached
(`dup_rate 0.111`). Those duplicates are not breakpoint arms (`dup_bp=0`), so the only live post-apply skip,
the breakpoint one, never sees them.

### B.2 `MTG_BP_PO_W0_FOLD` -- the playout wave-0 identity (sound identity fold, DEFAULT ON)

This is ledger question 41's EMPTY-arm collapse, built for the case where it provably holds: inside a playout.

*Mechanism.* Both breakpoint canon defaults stand down inside a playout (`BpUnbranchedCanon` requires
`g_rollout_nest == 0`), so there a BASE plan resolves every breakpoint it reaches to the unconditional EMPTY
fallback. Each breakpoint-opening base plan still got its wave-0 arms (rank 0, rank 1, EMPTY, CHAIN, ...).
On the 2HG d5 row they re-applied the base's own line: EMPTY 100%, CHAIN 98.6%, rank 74% of applies landed
on a state a sibling had already reached. The post-apply dedup then threw each one away, after paying a
unit, a `GameState` copy and an `ApplyPlanDirect`.

*The fold* (`src/ai/TurnSolver.cpp`, namespace `pofold` beside `w0collapse`; host in `SolveWithLookahead`'s
candidate loop, `po_here`). The base plan's apply is MEASURED for the three control-flow asymmetries a
`bp_choice` carries in `ApplyPlanDirect`: (1) the node partition (`node_owns_site` holds only for a
variant); (2) site 9, whose gate short-circuits on `bp_choice >= 0`; (3) site 8's look continuation, which
branches on `bp_searched_plan`'s return value. It also checks, defensively, that every occurrence the base
resolved was resolved by the fallback. Rank 0's apply measures the targeted continuation list's length `n0`
and its chainable indices. Then, before the unit charge and the apply, a variant whose family is clean is
declined when it provably re-applies a twin:

- the EMPTY arm (it is the base);
- rank k >= n0 at bp_at 0 (it overruns onto the fallback);
- the chain slot j when the apply reached exactly one class-on occurrence and the list has fewer than
  j+1 chainable continuations (EMPTY again);
- the chain slot whose j-th continuation is a rank < W whose own apply reached one class-on occurrence
  (it IS that rank).

What the skip leaves behind is what the post-apply dedup's skip left: the candidate is still counted for
the overrun guard, and a rank variant's two records (`BpWaveWalker::W0Len`, the uniform collapse's class-on
count) are written as its twin's. "No evidence, no skip": a family the memo knows nothing about is never
declined. Searched applies (`g_rollout_nest == 0`) are untouched. `MTG_BP_PO_W0_FOLD=0` restores every
apply (heurarm slot `BP_PO_W0_FOLD`). `MTG_BP_PO_W0_VERIFY=1` applies every arm the fold would decline and
compares its post-apply dedup key with the twin's; play is then the fold-off play.

*Soundness evidence.*
- VERIFY: 296,414 declines ok / **0 MISMATCH** (2HG d5 gi22); 280,365 ok / **0 MISMATCH** (std d5 s3003 gi1).
- Unit tests `test/unit/test_po_w0_fold.cpp`: the decision table (every arm, every "no evidence" case, the
  asymmetric family, bp_at 1, uniform, demoted, chain past W, a rank that saw two occurrences), and the real
  apply on a Pit Fighter draw, an empty continuation list included.
- The benchmark below: every row's play digest identical to the baseline, 600 / 600 win turns identical.

*Benchmark* (the six searched regression rows, one pooled batch, fold ON vs `=0`; the `=0` arm reproduces
the brief's baseline to the unit on every row):

| row | units/game off -> on | change | digest | win turns |
|---|---|---|---|---|
| 2HG d5 s2002 x50 (worst) | 248,047 -> 236,956 | **-4.5%** | identical | 50/50 same |
| 2HG d3 s2002 x100 | 140,298 -> 132,967 | -5.2% | identical | 100/100 same |
| d5 s3003 x75 | 124,147 -> 123,275 | -0.7% | identical | 75/75 same |
| d5 s2002 x75 | 82,078 -> 82,827 | +0.9% | identical | 75/75 same |
| d3 s3003 x150 | 71,370 -> 67,358 | -5.6% | identical | 150/150 same |
| d3 s2002 x150 | 57,609 -> 54,778 | -4.9% | identical | 150/150 same |
| all six | 61,245,932 -> 58,922,423 | **-3.8%** | | 600/600 same |

*Why one row went UP.* The per-decision census on gi22 shows the mechanism. The fold makes four decisions
(turns 1-4 of trial games) cheap enough that the ladder's CLIMB starts a deeper pass the fold-off arm did not
start. That pass costs up to 369K units against an 18K budget, because most of its work is in-playout
scoring that the decision budget does not bill, so the climb's estimate cannot see it. The game's units go
1.52M -> 1.69M with identical play. This is freed budget re-spent on depth (CLAUDE.md: a collapse is judged on
work removed, not on wall). The size of the climbed pass is a pass / overrun question, handed to agent R
(repro: `MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2 MTG_TURN_CENSUS=<f> mtg decks/Vampires/Vampire.cod --profile
<bench>/Vampire.profile.json --seed 2024 --game-index 22 --games 1 --depth 5 --budget-ms 20`, census rows
64-67 with the fold on vs `MTG_BP_PO_W0_FOLD=0`).

*Other decks.* Engine-wide breakpoint code, but only inside playouts and only for a base plan that opens a
breakpoint variant family. See B.5 for the smoke tier.

### B.3 Measured for the USER, not adopted: `search_leaf_depth: 0` (a settings lever, changes play)

Landfall and Melira Pod ship `"search_leaf_depth": 0`. The horizon rollout then plays every simulated
turn greedily instead of with a 1-ply lookahead that scores every candidate with a nested greedy playout.
Landfall's held-out A/B was -0.141 turns at 0.47-0.65x units. This is NOT a sound cut: it changes the leaf
evaluator at every budget, so it is out of scope for this round's own changes and is put to the USER.
Measured on the six searched regression rows (one pooled batch, fold ON; per-job profiles carrying only
the added key; per-game win turns vs the 1a1cf9b9e baseline):

| row | baseline units/game | leaf 0 | leaf 0 + first turn 1 |
|---|---|---|---|
| 2HG d5 s2002 x50 | 248,047 | 225,084 (-9.3%), 50/50 same | 478,186 (+93%) |
| 2HG d3 s2002 x100 | 140,298 | 102,702 (-26.8%), 1 slower (gi79 4->5) | 285,399 (+103%), 1 slower |
| d5 s3003 x75 | 124,147 | 120,467 (-3.0%), 1 faster (gi12 5->4) | 200,831 (+62%), 1 slower |
| d5 s2002 x75 | 82,078 | 79,912 (-2.6%), 75/75 same | 154,567 (+88%) |
| d3 s3003 x150 | 71,370 | 57,187 (-19.9%), 2 slower (gi62, gi73 4->5), 1 faster (gi12 5->4) | 155,364 (+118%), 2 slower |
| d3 s2002 x150 | 57,609 | 39,042 (-32.2%), 150/150 same | 100,002 (+74%) |
| all six | 61,245,932 | 50,987,161 (**-16.8%**; -13.5% vs the fold alone), net +1 turn / 600 games | 117,408,929 (+92%) |

Read with care: the d5 rows are BUDGET-BOUND. A cheaper leaf lets the ladder search more leaves inside the
same billed budget, so the d5 units barely move. The d3 rows, whose tree is depth-capped, show the leaf's own
saving. Leaf 0 + first-turn 1 costs MORE, not less (the outermost rollout's searched first ply, at every leaf).

**CPU on the gate row, same batch** (2HG d5 s2002 x50, default vs leaf 0, both arms interleaved in one
`--batch` while this agent held the box lock; `ms` is summed per-game wall): default 552,775 ms
(11.1 s/game, 11,625,913 units) vs leaf 0 325,024 ms (6.5 s/game, 11,254,206 units) = **0.59x the wall at
-3.2% units**, 50/50 win turns identical. A leaf-0 unit is far cheaper than a 1-ply-leaf unit, which units
alone hide. This is the largest lever found in this lane, and it is the USER's: it changes play, so adopting
it needs a held-out A/B (Landfall's was -0.141 turns). It was NOT adopted here.

### B.4 `MTG_BP_PO_W0_BASE_LEN` -- the measured base reads its first continuation list (sound, DEFAULT ON)

`MTG_DUP_DIFF` + `MTG_DEDUP_CENSUS` on 20 games of the worst row with the fold ON (`build/scratch/vampires/
cuts-b/dd1`): 10.2% of the 1.72M charged candidates were still post-apply duplicates. The biggest class was
in-playout rank 0 landing on its OWN base's state, 87,644 of 121,016 (72%). The fold applies rank 0 because
rank 0 is what measures the targeted list. When that list is EMPTY, rank 0 overruns onto the fallback,
which is the base's own line.

The measured base reaches the same occurrence first, at the same state (identical statements up to it is
what a clean family means). So it now reads the list there: `BpEnumEntryFor` by reference, sizes only,
memoized on state. When the list is not empty, rank 0's own read is the memo hit this read paid for.
`pofold::AdoptBaseLen` uses only the EMPTY case: n0 = 0 and no chainable index, which is exactly what rank
0 would have measured. Rank 0 then becomes a kRank twin, and the whole family collapses onto the base
without rank 0's apply. A non-empty list is left to rank 0, whose chainable-index walk the chain arm needs.
If the read moved the class-on counter, the family is marked asymmetric (no skip). Heurarm slot
`BP_PO_W0_BASE_LEN`; `=0` leaves rank 0 as the measurer (the control arm reproduces the fold-only binary's
units and digest exactly).

*Evidence.* VERIFY with the read ON: 354,419 declines ok / **0 MISMATCH** (2HG d5 gi22; the read collapsed
58,005 families), 336,442 / **0 MISMATCH** (std d5 s3003 gi1; 56,077). Unit tests: the empty-list family
now declines rank 0 as well, the non-empty family's read equals rank 0's, and `=0` restores the measurer.
`mtg-test` 1662/1662.

*Benchmark* (one pooled batch, the six searched rows, fold + read ON; per-game win turns vs the 1a1cf9b9e
baseline):

| row | baseline | fold only | fold + read | win turns |
|---|---|---|---|---|
| 2HG d5 s2002 x50 (worst) | 248,047 | 236,956 | **232,518 (-6.3%)** | 50/50 same, digest identical |
| 2HG d3 s2002 x100 | 140,298 | 132,967 | 131,238 (-6.5%) | 100/100 same, digest identical |
| d5 s3003 x75 | 124,147 | 123,275 | 121,635 (-2.0%) | 75/75 same, digest identical |
| d5 s2002 x75 | 82,078 | 82,827 | 83,888 (+2.2%) | 75/75 same, digest identical |
| d3 s3003 x150 | 71,370 | 67,358 | 66,619 (-6.7%) | 150/150 same; ONE play hash moved (gi133, T5 both) |
| d3 s2002 x150 | 57,609 | 54,778 | 54,511 (-5.4%) | 150/150 same, digest identical |
| all six | 61,245,932 | 58,922,423 (-3.8%) | **58,333,499 (-4.8%)** | 600/600 same |

d5 s2002 is up for the same reason as in B.2 (freed work re-spent by the ladder's climb). The one moved line
(d3 s3003 gi133, seed 3136) is checked at a large budget in B.5.

### B.5 Other decks, and the moved games at a large budget

The fold is engine-wide breakpoint code, but it acts only inside playouts, and only for a base plan that
opened a breakpoint variant family. **Smoke tier** (fold + read ON, `THREADS=20 MTG_PGO=0`, never
`--accept`): 126 of 128 configs byte-identical. Two configs moved, with 0 slower games: mirrorwing d3 gi29
6 -> 5 (faster) and gi58 a different line at the same T4; soldiers d3 gi82 a different line at the same T4.
Every other deck's play is unchanged, so the GT re-accept that landing needs covers exactly those two smoke
rows (plus whatever the regression tier shows; it was not run here).

**The three moved smoke games at a large budget** (one pooled batch, single-game chunks, fold ON vs
`MTG_BP_PO_W0_FOLD=0`; `smk/` in this agent's scratch dir):

| game | b10 off | b10 on | b100 and unlimited, BOTH arms | units at unlimited, off -> on |
|---|---|---|---|---|
| mirrorwing d3 gi29 (seed 1030) | T6 | **T5** | T5, digest identical (`fd62edd5`) | 34,960 -> 32,004 (-8.5%) |
| mirrorwing d3 gi58 (seed 1059) | T4 | T4, = the large-budget line | T4, digest identical (`e6010b12`) | 20,504 -> 19,137 (-6.7%) |
| soldiers d3 gi82 (seed 1083) | T4 | T4, another line | T4, digest identical (`6dbb7020`) | 14,800 -> 14,654 (-1.0%) |

At b100 and at unlimited budget the fold is byte-identical in play on all three games, and cheaper. At the
shipped b10 the freed work moved two of the three onto the line a large budget finds. This is the "budget
spent better" case, not a loss.

**The moved Vampires game at a large budget** (d3 s3003 gi133, seed 3136; single-game repros, read ON vs
`MTG_BP_PO_W0_BASE_LEN=0`):

| budget | read ON | read =0 |
|---|---|---|
| b10 (shipped) | T5, a different line | T5 |
| b100 | T4 (1,239,569 units) | T4 (1,094,571) |
| unlimited | T4 (3,064,996 units) | T4 (3,135,484) |

At unlimited budget the two arms run the same search apart from the declined applies: identical enum /
solve / m2 memo hit and miss counts, identical leaf-eval counters, 2.2% fewer units. The b10 line change is
the budget re-pricing a finite budget is allowed (CLAUDE.md: a budget may re-price work, never make a line
unfindable).

### B.6 Measured and NOT changed: `MTG_CAND_DEDUP` (default OFF)

The 11% plain post-apply duplicates of B.1 (`dup_bp=0`) still get a full rollout, because the only live
post-apply skip is the breakpoint one. `MTG_CAND_DEDUP` (left OFF in 2026-09 on wall-clock grounds, before
the USER's 10-01 "collapse unconditionally" ruling) skips exactly those rollouts. It is sound: an identical
state gives an identical rollout, and the later copy cannot take a tie, since the tie-breaks are win turn,
then the leaf grade, then `plan.value` over a value-sorted list. So it was given a heurarm slot (`CAND_DEDUP`)
and pooled into the B.4 batch. Result: **inert on Vampires**, within 21 units of ~58M on every row, every
digest identical. The duplicate candidates' rollouts are already cut short by the tie-prune / branch-and-bound
cutoff (they would need a strictly earlier win than an identical sibling). Nothing to adopt; the slot stays
for future A/Bs.

### B.7 What remains in this lane, and where the cost is

After the fold and the base read, the post-apply duplicates are small: rank k >= 1 twins about 15K, copy
permutations ("SAMEACTS" 35.6K, identical Voldaren Estates 15K), and the uniform arms 1.7K, out of 1.72M
candidates per 20 games. No further identity fold of this kind is worth more than ~1-2%.
The cost that is left is DISTINCT work. Every leaf rollout plays every future turn with a 1-ply lookahead
that scores every candidate with a nested greedy playout (B.1), inside mulligan-bottoming trial games that
are 99% of the units. Sound branch cuts cannot reach a 2x on the worst row from here. The levers that could
are (a) the leaf fidelity (B.3, a USER settings question) and (b) the pass / overrun structure (agent R: a
climbed pass costs 20x its billed budget because in-playout scoring is not billed).

### B.8 Open questions for the USER (work continued on the stated default)

- **B-Q1. The two identity folds ship DEFAULT ON** (`MTG_BP_PO_W0_FOLD`, `MTG_BP_PO_W0_BASE_LEN`). They are
  sound by the measured-asymmetry argument in B.2 and VERIFY's 0 mismatches, and they are engine-wide
  breakpoint code (playouts only). They move two smoke rows (mirrorwing d3: 1 faster, 1 same-score line;
  soldiers d3: 1 same-score line) and one Vampires line (gi133, same win turn). [default: ON; GT for the two
  smoke rows NOT accepted here; the regression tier was not run]
- **B-Q2. `search_leaf_depth: 0` for Vampires** (B.3): 0.59x the wall on the gate row (same batch), -16.8% units
  across the six rows, net +1 turn over 600 regression games (3 slower, 2 faster). It is a play change, like
  Landfall / Melira Pod, which adopted it on held-out A/Bs. [default: NOT adopted; recommended next step: a
  held-out A/B at play settings, standard + 2HG]
- **B-Q3. `MTG_CAND_DEDUP`** measured inert on Vampires (B.6). [default: unchanged, OFF]
