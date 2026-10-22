# Snow H cells: four EXACT unit reductions (2026-10-14)

Status: **built, default ON, at EVERY budget since 2026-10-15** (see "Budgeted" below). Every lever
has an `=0` hatch read through `EnvOn`. None of them can change the line the search commits at an
unlimited budget, ties included. They first shipped gated to unbudgeted play (the `g_unbudgeted_play`
latch plus an `Unlimited()` budget check) so budgeted fingerprints stayed still. The user ruled that
shape out (*"I don't like changes that only impact unbudgeted"*): a change the tiers never run is a
change nobody tests.

Context: Snow's value-leaf matrix stopped because ~18% of games hit the 40M-unit per-game ceiling.
The earlier work (`snow-leaf-lookahead-cost.md` on `snow-rollout`, `snow-look-cap-and-defer-levers.md`
on `snow-window`, `snow-matrix-leaf-fidelity.md`) was all lossy or restrictive. This is the exact route.

## Where the units went (s11011 gi13, H3, 2,833,833 units, `MTG_TT_PROBE=1`)

* 3,966 depth-1 leaf rollouts (the 1-ply horizon leaf) carried essentially all of it: 2.80M units,
  ~700 each. Each simulated leaf turn scores the whole candidate list with a greedy rollout per
  candidate (310k depth-0 rollouts, 1.37 turn-steps each).
* Unit sites: greedy_walk 32%, la_bp_wave 21%, la_cand 16%, rollout_step 15%, greedy_fallback 15%.
* 1,480 of the leaf rollouts (38%) reported a win EXACTLY at their cutoff, a tie the caller discards.
* Leaf transpositions are rare: the leaf table answered 12% of depth-1 lookups, and a mid-rollout
  memo found 3.6% repeats. Duplicate post-apply candidate states were already folded (bp-variant
  dedup, w0 nobp/unif collapses); what remained was the B&B tail, not duplication.

## The levers

| flag | where | mechanism | why it is exact |
|---|---|---|---|
| `MTG_FS_STRICT_CUT` | FSLineWin / FSLineTail child calls (`FsChildCut`) | a child gets cutoff `min(cutoff, best - 1)` once `best` is a real win | every plan loop keeps a child only on a STRICTLY earlier win, so a child win at `best` is discarded; a cutoff is always some ancestor's best or the root's `max_turns + 1`, so a win at exactly a cutoff never reaches the root. Each node still searches "wins <= its cutoff" exactly (cache bounds stay true); WIN lines are cutoff-independent (first strict improvement in plan order). The dork-release "release wins ties" call keeps its inclusive cut at `tail.win_turn`. Off under the value beam's score recording and the dominance census/prune. |
| `MTG_LA_TIE_PRUNE` | SolveWithLookahead candidate loop + both deferred-wave loops | skip a candidate's rollout when the best already wins on the candidate's next turn (the earliest it could return) and it cannot win the tie | the tie-break chain (inf-life proof, graded leaf, `plan.value`) is decidable without the rollout: a win publishes no leaf grade; inf-life only if the best lacks it AND the decklist holds a persist+ETB-lifegain card (`GameState::inf_life_possible`, stamped at setup); value must be STRICTLY higher. Waves accept only strictly earlier wins. Not at the enforcing root (its escalation reads every score), not under a trace or the dominance census. |
| `MTG_LA_TIE_TIGHTEN` | same sites | the same non-tie-winning candidate is rolled out under cutoff `best - 1` | a win at exactly `best` would be discarded by the comparison; the stored no-win bound is the cutoff really used |
| `MTG_NOWIN_STORE_UNGRADED` | SimulateToEnd leaf table | store a FULL-RUN no-win for a deck that does not grade no-win leaves (Snow) | the refusal existed because the ungraded natural exit published nothing (t_tb was a sibling's leftover). The exit now publishes kInvalid, so every exit of a full run is a function of its inputs; on Snow t_tb was already always kInvalid, so the publish is a no-op there |
| `MTG_ROLLOUT_M1_MEMO` | SimulateToEndImpl per-turn pre-combat lookahead | memoize `SolveWithLookahead(depth >= 1)` per decision epoch, key = BuildBreakpointKey + depth/max_turns/second_main + leaf-mode overrides + condemn host tag | the pre-combat twin of the shipped m2 searched memo; in unbudgeted play the plan is a pure function of the key. `MTG_ROLLOUT_M1_MEMO_VERIFY=1` recomputes every hit: 315 of 315 identical on gi13 |

Diagnostics, default OFF: `MTG_TT_PROBE` (leaf-table calls by depth/outcome/units, depth-1 slack
histogram); the new counters print under `MTG_ROLLOUT_STATS`.

## Measurements (units; every pair has the identical per-game play digest and win turn)

gi13 attribution (one game, flags added in order):

| arm | units | x base |
|---|---:|---:|
| all `=0` (== pre-change binary) | 2,833,833 | 1.000 |
| + NOWIN_STORE_UNGRADED | 2,786,559 | 0.983 |
| + ROLLOUT_M1_MEMO (alone: 2,768,945) | 2,721,875 | 0.960 |
| + LA_TIE_PRUNE (alone: 1,813,686) | 1,704,749 | 0.602 |
| + LA_TIE_TIGHTEN | 1,436,421 | 0.507 |
| + FS_STRICT_CUT | 597,508 | 0.211 |

Mid set (games that complete at base), all on vs all `=0`:

| game (cell seed, gi) | base | all on | ratio |
|---|---:|---:|---:|
| 8008, 107 | 6,190,266 | 90,570 | 0.015 |
| 10010, 4 | 8,262,704 | 734,741 | 0.089 |
| 8008, 32 | 15,123,854 | 1,238,125 | 0.082 |
| 9009, 41 | 35,240,804 | 3,006,671 | 0.085 |

Heavy H3 games that ABANDON at 40M at base, now (all on): s8008 gi23 543,740 units (wt 6),
s8008 gi143 523,881, s8008 gi127 7,602,871, s9009 gi48 8,733,201, s11011 gi43 875,083 -- every one
completes. (No base digest exists for them because base never finishes; the identity evidence is
the games below, which complete in both arms.)

## Identity validation (all on vs all `=0`, same binary; per-game win turn AND play digest)

| set | paired games | identical | units, all `=0` -> all on |
|---|---:|---:|---|
| Snow H3: gi13 + the four mid games | 5 | 5 | 67.65M -> 5.67M |
| Snow H5 (seed 12012, 12 games, 10M ceiling) | 8 | 8 | 4.10M -> 0.33M (4 more abandon only at base) |
| Snow V5 (same games, value model on) | 4 | 4 | 0.61M -> 0.24M (2 more abandon only at base) |
| Snow d3/b10 BUDGETED, 60 games, vs the PRE-CHANGE binary too | 60 | 60 | identical units (1,841,101) |
| Hinata2 H3 (seed 13013) | 9 | 9 | 10.84M -> 2.46M |
| KittyEquipment H3 | 9 | 9 | 0.47M -> 0.065M |
| FiveColour H3 | 10 | 10 | 1.21M -> 0.40M |
| Melira Pod H3 (inf_life_possible = true) | 10 | 10 | 0.32M -> 0.25M |
| Prevent Damage H3 (own-death deck) | 2 | 2 | 0.23M -> 0.06M |
| treasure_hunt H4 | 8 | 8 | 27.0k -> 16.4k |

The all-`=0` arm of the final binary reproduces the pre-change binary exactly (gi13: 2,833,833
units, digest 680220f1e2379ff7). `MTG_ROLLOUT_M1_MEMO_VERIFY=1`: 315/315 hits identical. Unit tests
596/596. NOT finished when this was written: the 50-game H3 light set, H1/H2/H4/V3 pairs, and the
base run of gi23 to completion (400M ceiling) -- the fleet-wide identity check covers the rest.

Pooled-batch note (pre-existing, not caused by these levers): the budgeted Snow job read 1,841,084
units inside a mixed pool and 1,841,101 standalone, with the same play digest -- units depend
slightly on what a worker thread ran before. Standalone arms agree to the unit.

## Budgeted (2026-10-15)

The latch is gone from all four sites: `FsChildCut`, the `tie_prune_here` skip/tighten in
SolveWithLookahead's candidate loop and its two wave loops, the no-win store in `SimulateToEnd`
(`NoWinLeafMemoArmed`), and `RolloutPreCombatSolve`. Under a budget each one RE-PRICES work: a
decision's units fall, so the start gate admits deeper passes and the committed line can move. None
changes a comparison. Strict `<` stays strict, the tie-break chain is evaluated in full wherever it
can decide, and the attacking-dork "release wins ties" keeps its inclusive cut. Two exclusions stay,
for reasons that have nothing to do with the budget: the tie prune stays off at the ENFORCING root
(its evaluated-candidate pool feeds the escalation, which reads every score, and its overrun guard
averages per candidate). `FsChildCut` also stays off under the value beam's score recording, and the
tie prune under a trace. Both stay off under the dominance census/prune.

**What makes a memo exact under a budget.** A memo entry must be the budget-FREE answer, or a hit
would replay a cut as if it were a result. A budget changes an answer only by CUTTING work. The
truncation counter (`g_fs_trunc_events`) was the watermark, and it misses three cuts that stop
without booking one: the charged greedy walk (Snow's `MTG_SOLVE_CHARGE` weight 16) stopping on an
exhausted budget, the equip route's per-link cut, and the enumeration-bound tranche skip in
`EnumerateMainPlansExhaustive`. So there is now a second watermark, `budgetbind::t_binds`
(`SearchBudget.h`). It is counted inside `SearchBudget::Exhausted()` and `Overrun()` whenever they
return true, and at the enumeration-bound refusal, so no cut site can forget it. All three memo
stores require it unchanged beneath the entry, along with no booked truncation and no decision-work
exit:

* the leaf no-win memo (`SimulateToEnd`);
* the rollout main-1 memo (`RolloutPreCombatSolve`, also bypassed while the pass overrun is tripped);
* the FS no-win cache (`FSLineWin`).

The FS no-win cache has stored under a budget from the start, so the bind test also closes a hole it
always had. An unlimited budget never binds, so unbudgeted play is unchanged.

**A key hole the verifier found on the way, not budget-specific.** `BuildSimKey` folded every dedicated
counter field except `fade_counters`, `verse_counters` and `ice_counters`. It also never saw which
Saproling Burst a token tracks, or the token's live P/T (`RefreshFadeTokens` writes it onto the
token's Card; the name stays "0/0 Saproling Token"). Two candidate-b Fungus boards with Bursts at
different fade totals shared a key and rolled out to different win turns (`MTG_NOWIN_VERIFY`: 5 stale
hits on one game, s502005 turn 6). The same key feeds the leaf table's WIN half and, through
`BuildBreakpointKey`, the m1/m2/enum memos, so the fix also corrects existing budgeted play on that
deck. The three counters now fold when nonzero. On a board holding a counted fade source, the
source's copy id and each token's link and live P/T fold too. Every other board keeps its exact prior
key.

**Exactness, measured under a budget.** 715 games: every held-out job at 5 games, d3/b10 and play
depth/b20, all suite decks. Both verifiers recompute UNBUDGETED with no table:

| harness | checks | bad |
|---|---|---|
| `MTG_NOWIN_VERIFY`, at each hit | 286,799 | 0 |
| `MTG_NOWIN_VERIFY`, at each store (new) | 401,911 | 0 |
| `MTG_ROLLOUT_M1_MEMO_VERIFY` | 205,043 | 0 |
| positive control `MTG_NOWIN_VERIFY_POISON=1` (111 games) | 33,064 | 2,234 (must light) |

The store-time half and a key-hole/drift classifier were added to `MTG_NOWIN_VERIFY` to diagnose the
fade hole. At a stale hit the classifier replays the state the entry was stored from. Still no win
means a KEY HOLE (two states share a key); a win means DRIFT (something outside the key changed
between store and hit).

**Held-out A/B under a budget** (143 jobs, 28,915 games, the regression seeds +500000; d3/b10 and play
depth/b20; OLD = the latched tree, NEW = this change; paired per game):

| | turns (NEW-OLD) | paired t | games better / worse | units NEW/OLD |
|---|---|---|---|---|
| all decks | -0.0017 | -6.00 | 56 / 8 | 0.9875x |

No deck's mean is worse except burn (+0.0005, one game in 2,000). All 8 slower games replay
identically as single games and recover at a larger budget (6 at 4x, FiveColour gi2 and Hinata gi97 at
16x). They are budget churn, not lost lines. Units fall on most decks (WhiteKnights 0.41x, Dragons
0.57x, Snow 0.79x) and RISE on nine: Melira 1.56x, Bruna 1.46x, FiveColour 1.42x, FiveColour 2HG 1.25x,
PD 1.20x, Giants 1.19x, Kitty 1.15x, fungusb 1.10x, burn 1.06x. Attribution on those decks:

* NEW with the four cuts at `=0` vs OLD: identical play and identical units (fungusb +1.4%, the key
  fix's extra misses). So the store guards and the key fix cost nothing measurable.
* NEW vs NEW with the cuts at `=0`: the whole rise. Cheaper passes let the iterative-deepening start
  gate admit a deeper pass that the old search did not begin, and that pass spends budget the old
  search left unused.

That is freed budget re-spent on depth. Per CLAUDE.md, an exact collapse is not withheld for that
reason, and whether the depth is worth its cost is a per-deck SETTINGS question. For example, Melira
buys no quality for its 1.56x at these seeds.

GT tiers: smoke searched 21 faster / 3 slower / 79 same-score line changes; regression 58 faster / 5
slower / 201 same-score; d0 unchanged; references 580 with 0 drift. Every slower game recovers at a
larger budget, or (Fluctuator d3 gi7, gi21) matches OLD at unlimited budget at d3 and d8. The full
list is in the GT notes.

**Does the re-spent depth pay? (2026-10-22, settings follow-up.)** Bruna and FiveColour at their play
depth (d5 and d6), with budgets of 20 (shipped), 14 and 10 ms. The run used `d3a269ba4`, three held-out
seeds (502002, 503003, 504004) and 450 / 600 games per arm, paired against b20:

| deck | arm | turns vs b20 | paired t | faster / slower | units vs b20 |
|---|---|---|---|---|---|
| Bruna | b14 | +0.0022 | 1.00 | 0 / 1 | 0.83x |
| Bruna | b10 | +0.0044 | 0.71 | 3 / 5 | 0.69x |
| FiveColour | b14 | +0.0050 | 1.34 | 1 / 4 | 0.82x |
| FiveColour | b10 | +0.0117 | 2.34 | 1 / 8 | 0.72x |

Neither lower budget is net <= 0, so the depth the cuts admit does buy quality and both decks keep b20.
Most of the 10-15 premium is also gone on this tree. Per game, b20 now spends about 1.1x the pre-cut
units on both decks (Bruna about 110k vs 100k units per game, FiveColour about 24k vs 23k), where the
10-15 run measured about 2x. This is an approximate cross-binary comparison over overlapping game
sets. Melira was left out: its budget was re-chosen on 10-21 (the emulated ladder at b60, with b33
and b45 measured) on a tree that already had the cuts.

Before the latch was dropped, the unbudgeted-only shape was measured inert under a budget: Snow
d3/b10, 60 games (seed 701000), pre-change binary vs all-on vs all-`=0` gave identical per-game
digests and identical units (1,841,101) in all three.

One coupling to unit COUNTS exists inside unbudgeted play and is worth knowing: the value-leaf
hybrid's escalation predictor prices passes from measured probe costs and leaf counts
(`g_probe_cost` / `g_probe_leaves`, with `esc_units = FromVirtualMs(max(1, budget_ms))` when the
budget is unlimited). These levers change those measurements, so a V cell whose escalation runs
could in principle pick a different escalation depth. The V-cell pairs below are the check.

## Cache size vs units

Units DO depend on cache capacity, a little: a cache hit skips work that is otherwise billed (the
greedy walk is billed on Snow at 1 unit / 16 visits; a leaf-table hit skips a rollout). Measured with
every exact lever off:

* gi13: solve memo 16,384 -> 4,194,304 entries: 2,833,833 -> 2,826,242 (-0.27%), digest identical.
* s8008 gi32 (completes): default 15,123,854 -> generous 14,982,066 (-0.94%), digest identical,
  peak RSS ~0.8 GB -> ~9.5 GB.
* gi23 (abandons): default caps vs generous (TT unbounded, solve memo 4M, enum memo 1M, plan cache
  16 GB, FSL unbounded): both abandon at 40M; peak RSS 0.56 GB -> 7.0 GB. The TT peaked at 1.43M
  entries against a 3.19M cap (never refused a store); the FSL cache is unused in H cells; the
  binding caps are the solve memo (156 clears) and the m2 search memo (10 clears).

So bigger caches buy well under 1% of units and cannot rescue an abandoned game; the abandoned set
is a property of the search, not of the memory budget (TT cap is RAM/worker-derived but was never
reached here).
