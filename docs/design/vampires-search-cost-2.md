# Vampires search cost, round 2 (2026-10-21)

Three agents on one brief (`build/scratch/vampires/bench/BRIEF_PERF.md`): P (per-node CPU), B (branch cuts),
R (the pass / overrun structure -- work the search does twice or throws away). Base `1a1cf9b9e`. One section
per agent.

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
