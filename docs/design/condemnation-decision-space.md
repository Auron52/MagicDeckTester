# Order condemnation belongs in the decision space, not the leaf evaluator

**Status:** ADOPTED 2026-08-21 as the default. Decision-space authority replaces root-turn
authority; `MTG_CONDEMN_ALL_TURNS`, `MTG_CONDEMN_SEARCHED_ONLY` and `MTG_CONDEMN_M1_BP` all
ship ON with `=0` hatches. Measured on FiveColour — the **pilot** deck for the condemnation
design, not the only one that will carry it.

## The symptom

Turning condemnation on in more places made the engine do **more** work, not less:

| arm | drops | calls | turn_steps | cands |
|---|---|---|---|---|
| condemnation OFF (`MTG_5C_CONDEMN=0`) | 0 | 247,814 | 404,997 | 341,932 |
| root-turn only (ship default) | 688 | 247,812 | 404,993 | 341,932 |
| every turn (`MTG_CONDEMN_ALL_TURNS=1`) | 212,367 | **258,615 (+4.4%)** | **421,355 (+4.0%)** | 341,171 (−0.2%) |

20 games, seed 1001, `--depth 2 --budget-ms 0`, `MTG_ROLLOUT_STATS=1`. `avg` is 5.3000 in
every arm. That is condemnation running **backwards**: a quarter-million deletions bought a
0.2% candidate reduction and cost 4% more rollout work.

## What it was NOT

Both ruled out by measurement before the real cause was found — recorded so they are not
re-investigated:

* **Not the condemnation escalation.** The lossless beam re-scores K+1 finalists with the
  filter suppressed whenever a candidate's subtree was filter-touched, so "more drops → more
  unfiltered re-evaluation" was the obvious amplifier. `MTG_CONDEMN_ESCALATE=0` is
  **byte-identical in every arm** (247,812 / 258,615 / 260,358 unchanged). The escalation
  never fires on this configuration.
* **Not the new first-main-breakpoint site.** `MTG_CONDEMN_M1_BP=1` alone is byte-identical
  to baseline. It only ever looked implicated because it had only been measured *together*
  with `MTG_CONDEMN_ALL_TURNS`.

## The cause

Split the drops by which action collector was running when the filter fired
(`SolveUncached` = the greedy path, *"d0 decision + every rollout leaf"*; `EnumeratePlans` =
the searched path):

| arm | drops | **greedy** | searched |
|---|---|---|---|
| root-turn only (ship) | 688 | **0** | 688 |
| every turn | 212,367 | **205,110 (96.6%)** | 7,257 |

**96.6% of the added drops fire inside the greedy leaf evaluator.**

Condemnation's premise is *"the m1 search saw this card and passed, so m2 must not
re-litigate it"* — a restriction on what the **decision** is allowed to choose. The rollout
leaf is not deciding; it is **estimating a value**. Deleting actions there cannot prune
anything (there is no ranking to prune — the greedy pass takes one line), so the only effect
is that the line it takes is worse. Worse lines take longer to win, which is exactly the
measured `turn_steps` rise. The leaf's job is to say how good a position is; a leaf forbidden
to play its best cards reports a pessimistic number, and every value in the search inherits
the bias.

This is the same fact the existing root-turn-authority comment already states — *"every
deeper m1 is budget-starved, and its passes are exactly the unreliable kind the m2 re-offer
exists to rescue"* — but as a **cost** result rather than a quality one.

## The fix

`MTG_CONDEMN_SEARCHED_ONLY=1` gates the *drop* (not the stamp) on being in the decision space:

```cpp
const bool decision_space = g_search_candidate_enum || g_condemn_root_turn < 0;
```

* `g_search_candidate_enum` — false exactly during the greedy collect, so this admits every
  searched enumeration and excludes the leaf.
* `g_condemn_root_turn < 0` — "outside any solve" = the executor's committed play, including a
  first-main breakpoint continuation it resolves greedily. That one *is* a real decision, so
  the greedy exclusion must not swallow it.

The stamp still runs everywhere (`MTG_CONDEMN_ALL_TURNS=1`), which is the line-local model:
every projected turn records its own m1 pass, and `m1_hand_turn == turn_number` keeps it from
leaking across turns.

Result — 10.3x more condemnation than ship, and work goes **down**:

| arm | drops (greedy/searched) | calls | turn_steps | cands |
|---|---|---|---|---|
| root-turn only (ship) | 688 (0/688) | 247,812 | 404,993 | 341,932 |
| **ALL_TURNS + SEARCHED_ONLY** | **7,114 (0/7,114)** | **247,251** | **404,281** | **341,075** |

The sign is now correct. The magnitude is small (~0.2%) because FiveColour's searched
projections are a small share of its total work; the value of the change is that
condemnation now scales the right way when it is extended.

## Quality: smoke on the fix arm

`MTG_CONDEMN_ALL_TURNS=1 MTG_CONDEMN_SEARCHED_ONLY=1 bash test/regression.sh --smoke`
(not accepted — the run must not be promoted into GT while the flags are experimental):

```
Result: 34 passed, 2 failed, 0 new
  FAIL  fivecolour_smoke_d3_s1001  exp=5.0133/41f9429d... got=5.0133/84f20d2f...
  FAIL  fivecolour_smoke_d5_s1001  exp=5.1733/3a054210... got=5.1733/7b0e54e6...
  [searched] slower=0  faster=0  play-changed=8
  [d0      ] slower=0  faster=0  play-changed=0
```

Both failures are **digest-only** — the avg win turn is unchanged on both. Every other deck is
byte-identical PASS, which is the expected blast radius: `CondemnsPassedMainPhase()` is
FiveColour-only. So the change is quality-neutral on smoke and costs ~0.2% less rollout work.

### Shipped configuration

All three flags default ON; each `=0` hatch reproduces a reference arm exactly (20 games, d2,
`--budget-ms 0`):

| config | drops (greedy) | calls | turn_steps |
|---|---|---|---|
| **default** | 7,114 (0) | 247,251 | 404,281 |
| `MTG_CONDEMN_ALL_TURNS=0 MTG_CONDEMN_M1_BP=0` (old ship) | 688 (0) | 247,812 | 404,993 |
| `MTG_CONDEMN_SEARCHED_ONLY=0 MTG_CONDEMN_M1_BP=0` (the broken arm) | 212,367 (205,110) | 258,615 | 421,355 |
| `MTG_5C_CONDEMN=0` | 0 | 247,814 | 404,997 |

`MTG_CONDEMN_SEARCHED_ONLY=0` is **not a supported configuration** — with stamp-everywhere on it
is the inversion itself. The hatch exists to reproduce the finding.

Re-verified after rebasing onto 42 upstream commits: the default, the old-ship hatch and the
condemnation-off hatch all reproduce the table exactly. The broken arm's *drop count* moved
(212,367 → 161,008) while its `calls`/`turn_steps`/`avg` stayed bit-identical — upstream reduced
how often the greedy collector is invoked, so fewer filter invocations produce the same game. The
inversion it demonstrates is unchanged: +4.4% calls / +4.0% turn_steps against the default.

## Rejected variant: keying on the producer

The gate could instead ask *which pass wrote the snapshot* (tag the stamp, carry it on
`GameState`, require it at the filter). Built and measured: **exactly inert** — it admits the
same 7,114 drops, because the rollout's per-turn m1 comes from `SolveWithLookahead` (searched)
and only its *leaf* is greedy, so the producer is almost never the greedy pass. It also costs a
bool in `GameState`, whose deep copy is a measured hotspot. Not carried. The reader is the
right axis: it is the reader that determines whether there is a plan space to prune.

## `MTG_CONDEMN_M1_BP`: shipped ON although currently free

Under this rule the first-main-breakpoint site is **byte-identical on FiveColour**: all 1,457 of
its breakpoint continuations occur inside the search's rollouts and **zero** in committed play,
so every one of them is an evaluator, not a decision, and the gate correctly excludes them.

It ships **ON** regardless. It is the user's model stated in full — *"at every phase AND
BREAKPOINT we are evaluating"* — it costs nothing measurable, and the alternative is leaving a
site unconditionally unfiltered for no better reason than that today's pilot deck cannot reach
it as a decision. It binds the moment a deck reaches a breakpoint in committed play, or when the
searched-breakpoint fan-out fires on one.

Note there is no other deck to validate it on today: `CondemnsPassedMainPhase()` is overridden
only by `FiveColourProvider`, so **no other deck condemns at all**. Any deck that opts in later
inherits this site already wired.

## Instrumentation added

`MTG_ROLLOUT_STATS=1` now also reports:

```
[rollout-stats] cand_scored=<n> condemn_drops=<n> (greedy=<n> searched=<n> greedy_frac=<f>)
```

`interior_nodes` alone could not have found this: it counts only `FullSearchLine`'s
applications and is blind to the rollout's own candidate loop, so it read **bit-identical**
across every arm while the work underneath moved 4%.
