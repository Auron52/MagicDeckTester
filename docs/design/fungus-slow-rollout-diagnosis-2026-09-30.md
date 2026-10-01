# Fungus candidate-B: a measured diagnosis of the slow keep-rollouts

**Status: DIAGNOSIS ONLY — no fix proposed here, deliberately.** 2026-09-30, after the candidate-B
mulligan generation was cancelled at 90.8 h (63.7% frozen) because its tail made the run unbounded.

USER: *"we don't have a good diagnosis. We haven't dug enough into why this is a problem"* and
*"essentially, we can't come up with a solution because we don't have a diagnosis."* This file is the
diagnosis. It supersedes the untested hypothesis in
`fungus-doubling-season-rollout-tail.md` §4 — see §5 below, which **refutes** it.

## 1. What was measured, and how to redo it

One captured slow rollout, replayed byte-identically via the generator's own replay hook:

```
MTG_KEEP_REPLAY="Utopia Mycon x2; Hickory Woodlot x1; Doubling Season x2; Deathspore Thallid x1; Brightcap Badger x1" \
MTG_KEEP_REPLAY_R=2 MTG_KEEP_REPLAY_PD=0 \
MTG_EQUIV_CACHE=logs/fungus_slowturn_analysis/gencache.HEAD.json \
MTG_KEEP_OUT_RAW=logs/fungus_slowturn_analysis/scratch.raw.json \
MTG_BRANCH_STATS=1 \
build/Profile/mtg-analyze decks/Fungus/candidate-b-2026-09/Fungus.cod \
  --cards-json src/cards/data/cards.json --gen-mulligan fast
```

It reproduces: **20,700 ms** on current HEAD (30,038 ms in the generation, on the older engine).
`MTG_EQUIV_CACHE` must point at a cache built by the CURRENT engine or discovery re-runs and costs
9 minutes; `MTG_KEEP_OUT_RAW` is essential — without it the replay appends to the real run's
`.slow.log` and rewrites its `gencache.json`, which would break resuming the cancelled run.

perf, with the recipe from `fungus-token-search-cost.md` (write to `/tmp`, software event):

```
perf record -e cpu-clock -F 499 --no-buildid -o /tmp/slowroll.perf.data -- <the command above>
perf report -i /tmp/slowroll.perf.data --stdio --no-children
```

**Caveats that matter.** perf inflated the rollout 20.7 s → 35.9 s (1.7x), so *relative* attribution
is usable and absolute figures are not. The 64-game play-digest battery (~9 s) runs before the replay
and is included; its games are narrow-board, so it contributes low-odometer calls and dilutes rather
than creates the signal below.

## 2a. CORRECTION (same day): "no hot spot" was the wrong framing

USER: *"No hot spot is stupid. I'm talking about branching here. There are obviously hot spots."*
Correct, and §2/§3 below are written around my error, so read this first.

I answered *"where do CPU cycles go"* with a flat profile, found it diffuse, and let that framing
overwrite the question actually asked — **where does the branching concentrate.** The branching hot
spot is in the data in §4 and it is severe:

| | share of calls | share of odometer space |
|---|---|---|
| `groups=5-8 board=16+` | **7.2%** | **64.5%** |
| top four situation buckets | 27% | **93%** |

The claim *"there is no equivalent win here"* in §2 is therefore **withdrawn as unsupported**. The win
is not in a function, it is in **not generating those plans**. A diffuse flat profile is *consistent*
with a concentrated branching hot spot rather than evidence against one: if one situation class builds
most of the plans and each plan pays a spread of small per-plan costs, the cycles smear across a dozen
functions while the cause stays in one place. That is exactly the shape here.

**And the `Mycoloth` driver attribution in §4 is weak — do not act on it.** `odo` is the product over
*all* groups (`Π(1 + |group_i|) × 2^independent`), while `driver` merely names the card owning the
*largest* group. So "73.8% of odometer under `Mycoloth [+1 tied]`" does **not** mean Mycoloth causes
it. Two further facts kill that reading:

- **`MTG_FUNGUS_DEVOUR_CANDS` is already ADOPTED default ON**, so devour `k` is already a short
  candidate list (fodder floor + contested bodies), not `0..own`. The obvious narrowing is done.
- Working backwards from avg_odo 4,042 across `groups=5-8`: that is roughly **6 groups averaging size
  ~3**, multiplying. No single group is large.

**So the real statement of the hot spot is structural, not per-card:** on wide boards the solver
jointly enumerates the **cross product of 5–8 simultaneously-open option groups**, and the
subset-reject predicates in §2 (`SubsetOversubscribesSacFodder`, `SubsetWastesCreatureSacMana`,
`SubsetHasDuplicateSacSource`, 6.76% combined) are the evidence of *why* it is joint rather than
independent: those groups **compete for shared sac fodder and shared mana**, so they cannot simply be
chosen separately.

That reframes the whole problem from "find the slow function" to: **the enumeration is
generate-then-reject against a shared-resource constraint, when the constraint could bound the walk
instead.** Whether that is achievable is the open question — but it is a far more promising direction
than anything in §2, and it is the direction §6.1 should have led with.

## 2b. THE PER-TURN VIEW, and what the shared-resource constraint actually rejects

Measured with a new instrument, `MTG_BRANCH_SHAPE` (`TurnSolver.cpp`, `namespace shapestats`),
default off and byte-identical when off (smoke 101/101, configs-changed 0). It answers the two things
§6 listed as unknown, on the same replayed rollout.

```
MTG_DECISION_WORK_X=1000 MTG_BRANCH_SHAPE=1 MTG_BRANCH_STATS=1 \
MTG_KEEP_REPLAY="Utopia Mycon x2; Hickory Woodlot x1; Doubling Season x2; Deathspore Thallid x1; Brightcap Badger x1" \
MTG_KEEP_REPLAY_R=2 MTG_KEEP_REPLAY_PD=0 \
MTG_EQUIV_CACHE=logs/fungus_retention/gencache.HEAD.x1000.json \
MTG_KEEP_OUT_RAW=logs/fungus_retention/shape.raw.json \
build/Release/mtg-analyze decks/Fungus/candidate-b-2026-09/Fungus.cod \
  --cards-json src/cards/data/cards.json --gen-mulligan fast
```

### The real turns (§6.4, asked for twice, now answered)

| turn | calls | sum_odo | plans | dedup | avg_odo | avg groups | avg board |
|---|---|---|---|---|---|---|---|
| 1 | 682 | 11,260 | 277 | 274 | 16.5 | 3.44 | 1.4 |
| 2 | 2,779 | 69,251 | 5,073 | 4,419 | 24.9 | 3.88 | 2.8 |
| 3 | 6,288 | 143,684 | 21,944 | 18,673 | 22.9 | 3.46 | 5.3 |
| 4 | 13,619 | 380,732 | 76,425 | 67,916 | 28.0 | 2.72 | 7.6 |
| 5 | 26,123 | 2,792,237 | 301,272 | 277,981 | 106.9 | 2.97 | 10.1 |
| **6** | 24,219 | **41,283,327** | 1,245,159 | 956,302 | **1,704.6** | 3.74 | 12.8 |
| 7 | 19,690 | 3,161,935 | 727,626 | 691,036 | 160.6 | 3.46 | 12.2 |
| **8** | 22,459 | 8,175,380 | **2,757,165** | 2,745,272 | 364.0 | 4.32 | 15.5 |
| | 115,859 | 56,017,806 | 5,134,941 | 4,761,873 | | | |

**The cost is NOT spread across the turn range, and the two blowups are different blowups.**

* **Turn 6 owns the odometer SPACE: 41.3 M of 56.0 M = 73.7%**, at avg_odo 1,704 — 5–16x every other
  turn — from only 20.9% of the calls.
* **Turn 8 owns the PLANS: 2.76 M of 5.13 M = 53.7%**, from a space of only 8.2 M (avg_odo 364).
* So turn 6 is a wide space that prunes well (dedup removes 23.2%, the only turn where dedup earns
  its keep), and turn 8 is a narrow space that prunes barely at all (dedup removes **0.4%**) and
  simply builds everything it enumerates.

Turn 8 is the horizon edge (`max_turns=8`), which fits `fungus-value-leaf-status.md`'s finding that
99.7% of this deck's evaluations sit at the horizon edge. Half the constructed plans are built on the
last turn the search will ever look at.

### 2d. CORRECTION + the sharpest result so far: TWO walks, and one predicate (2026-10-01)

**The correction first.** §2b reported "the walk visits 43.7 M subsets to produce 5.1 M plans — 8.5
subsets per plan". **That is not a ratio of anything.** The reject counters were instrumented in
`TurnSolver::SolveUncached` (the search's greedy subset walk) while the odometer, plan and dedup
columns come from `EnumeratePlans` (the enumeration walk). They are two different enumerations in two
different functions, and dividing one by the other is meaningless. Both are now measured apart:

| walk | subset visits | rejected | dupSacSrc | wasteSacMana | overFodder |
|---|---|---|---|---|---|
| `SolveUncached` | 43,664,421 | **25.0%** | 8.15% | 4.56% | 12.32% |
| `EnumeratePlans` | 11,927,667 | **39.3%** | 1.5% | 4.5% | **33.3%** |

**And on the decisions that actually matter it is far more concentrated than either figure.** Ranking
heavy decisions by subset VISITS rather than odometer (the odometer having been shown in §2c not to
predict cost), the top three are all turn 6 and all look like this:

```
HEAVY rank=1 odo=88200 turn=6 board=22 hand=4 plans=368
  FUNNEL visits=13076 dupSacSrc=0 wasteSacMana=0 overFodder=9612 other=0
         PASSED=3464 rejectPct=73.5 visitsPerPlan=35.5
```

**73.5% of the walk is discarded, every bit of it by ONE predicate —
`SubsetOversubscribesSacFodder` — and the other two reject nothing at all.** 35.5 visits per plan
produced.

The mechanism is exactly the shared-resource story §2a proposed, now pinned to a single gate: the
board holds 8 Saprolings and several outlets that each want them (two Utopia Mycon, each offering a
`sacN=5` activation — 10 demanded against 8 available — plus Psychotrope Thallid and Undercellar
Myconid). The enumerator builds those combinations and then throws three-quarters of them away.

**This replaces 25% as the size of the prize on the hot decisions.** A fodder bound applied *before*
generating, rather than as a leaf test after, is now the one candidate with real mass behind it, and
it is worth noting the bound only has to be good on turn-6-shaped boards to collect most of it.

### 2e. The 73.5% COLLECTED — but not by the bound §2d proposed (2026-10-01)

**Profile first, as the rule says — and it redirected the work.** §2d ended pointing at a *prefix
bound* inside the odometer. Before building one, `perf record` on the same replay was asked how much
time the predicate actually costs. Inclusive, over its three clones:

| symbol | inclusive | self |
|---|---|---|
| `SubsetOversubscribesSacFodder` | **6.61%** | 2.66% |
| `SubsetHasDuplicateSacSource` | 1.43% | 1.38% |
| `SubsetWastesCreatureSacMana` | 0.89% | 0.85% |

So the leaf test is ~3/4 of the whole sac-filter bill, and the two filters a prefix prune would
*additionally* skip on rejected positions are together ~2.3% — of which only the fodder-rejected
share is collectable. **The prefix bound was aimed at the smaller half.**

**And the leaf test did not need a bound at all — it needed to be read as arithmetic.** Every term of
the predicate is a sum or an OR over the selected actions, against board quantities that are fixed
for the whole enumeration:

```
sac_actions / outlets / devour / want[f]   sums over sel
pooled_multi / unbounded[f]                ORs  over sel
credit[f]                                  a sum over sel
supply[f]                                  a BOARD count -- identical for every subset
```

So one precomputed term per candidate turns the per-subset cost into `|sel|` integer adds. What it
replaces, *per enumerated subset*, is: a heap-allocated `vector<pair<string,int>>`, a `std::string`
copy per outlet, string compares to group the demand, and 3–4 full battlefield walks inside
`board_supply` — on a turn-6 board that is ~22 permanents × `CardHasSubtype`, tens of thousands of
times per decision. `FodderIndex` in `TurnSolver.cpp` is that re-association.

**This is EXACT, which the prefix bound could not have been.** The predicate is not monotone — a cast
creature adds +1 body, a spore pop adds `k × spore_creates_tokens` — so a violating prefix can be
made legal by extension, and the sound prefix test is the strictly weaker
`demand > supply + max-remaining-credit`. Re-associating the leaf needs no such weakening: it is the
same test, same branches, same order.

**Measured on the heaviest cell,** `MTG_SAC_FODDER_AGG=0` vs default, arms **interleaved** and
reported **per pair** (an aggregate-only mean hides sign flips — see
`wall-ab-aggregate-right-signs-wrong.md`):

| pair | off | on | ratio |
|---|---|---|---|
| 1 | 12,503 ms | 10,718 ms | 1.167x |
| 2 | 12,004 | 10,624 | 1.130x |
| 3 | 11,951 | 10,607 | 1.127x |
| 4 | 12,049 | 10,807 | 1.115x |
| 5 | 12,187 | 10,764 | 1.132x |
| 6 | 12,060 | 10,813 | 1.115x |
| 7 † | 16,200 | 13,679 | 1.184x |
| 8 † | 15,238 | 13,152 | 1.159x |

**mean 1.141x, median 1.131x, range 1.115–1.184x, and all eight pairs favour the aggregate.**
Rollout-config play digest `4b55aac85d0e0b77` identical on all sixteen runs.

† Pairs 7–8 were run at load ~13.7 against ~5.3 for 1–6. Both arms move up together and the *ratio*
moves slightly in the aggregate's favour — which is the whole argument for pairing: absolute wall on
this box is worth nothing right now, paired interleaved wall is worth plenty. (Hardware counters
would have been better still, but this VM reports `instructions:u` as `<not supported>`, so there is
no PMU to fall back on.)

**And the symbol is gone from the profile, which is the independent check on the wall number.**
Re-profiling the same replay after the change:

| symbol | before | after |
|---|---|---|
| `SubsetOversubscribesSacFodder` (self, 3 clones) | 2.66% | **absent** |
| `CardHasSubtype` | 1.49% | **0.41%** |
| `BuildFodderIndex` (the new per-candidate pass) | — | 0.26% |

So two thirds of every `CardHasSubtype` call in the engine was this one predicate's `board_supply`,
and the index that replaces the whole thing costs 0.26%.

**Equivalence is checked, not asserted.** `MTG_SAC_FODDER_AGG_VERIFY=1` runs both forms on every
subset and prints the first disagreement. Zero mismatches on the Fungus replay and across the smoke
suite.

**The lesson, which is the same one as §2c one level down.** §2c found that the odometer bounds the
prize but does not predict it. §2e finds that **visits do not predict it either**: 73.5% of visits
was a real number and a real prize, but the way to collect it was to make each visit cheap, not to
stop making visits. The profile is what distinguished the two, and it took ten minutes.

#### What is on top now (same replay, after the change)

| self | symbol | note |
|---|---|---|
| 5.93% | `SolveUncached`'s `consider` lambda | the subset body itself: cost folding, credits, scoring |
| 3.30% | `EnumeratePlanPositions<SolveUncached>` | the odometer + the two-stage split's pair sort |
| 2.30% | `BuildSimKey` | open question 2 — the 42.6% solve-memo hit rate lives here |
| 2.29% | `operator new` | per-subset allocation; not yet attributed to a caller |
| 1.98% | `CollectActions` | |
| 1.66% | `ManaPool::CanPayFlat` | |
| 1.48% | `SubsetHasDuplicateSacSource` | now the biggest subset filter |
| 1.37% | `~vector<Action>` | Action vectors copied/destroyed per plan |
| 1.04% | `SubsetWastesCreatureSacMana` | |

**Two of these are the same shape as the one just fixed, and both are worth taking.**
`SubsetWastesCreatureSacMana` is additive in exactly the same way (`spend` is a sum, the three flags
are ORs) **and its death-payoff loop is a whole-battlefield walk with a `LookupCached` per permanent
that depends on nothing but the board** — a per-call constant evaluated per subset, which is the
`SubsetFilterPre` defect one more time. `SubsetHasDuplicateSacSource` is pairwise O(|sel|²) rather
than a sum, so it needs a different idea.

**`operator new` at 2.29% is the one to attribute next**, because an allocation per enumerated subset
is a bigger structural problem than any single filter, and `~vector<Action>` at 1.37% beside it
suggests the Action vectors are the source. Attributing it needs a call-graph profile with a working
unwinder (this box has no frame pointers, so `--call-graph dwarf` is the only route and it did not
resolve the allocator's callers cleanly).

### 2f. The second filter of the same shape — `SubsetWastesCreatureSacMana` (2026-10-01)

**§2e named this filter and `SubsetHasDuplicateSacSource` as "the same shape as the one just fixed,
and both worth taking". This is the one that really is the same shape** — the duplicate-source filter
is pairwise `O(|sel|²)` rather than a sum and still needs a different idea (see below). Read as
arithmetic, every term of this one is a SUM or an OR over the selected actions:

```
spend              a SUM of cost.ManaValue() over sel
any sac-for-mana   an OR  over sel   (the necessary-condition prepass)
has_creature_sac   an OR  over sel   (reads sac_src_def[j], already a SubsetFilterPre hoist)
direct_damage      an OR  over sel   (the early-out, which also answers false)
death payoff       a BOARD fact -- identical for every subset
```

`WasteIndex` in `TurnSolver.cpp` is that re-association: one precomputed term per candidate, so the
per-subset body becomes `|sel|` integer adds. What it replaces, per enumerated subset, is a
`ManaCost::ManaValue()` call per selected action, two separate walks of `sel`, and — on every subset
that reaches the end — a walk of the whole battlefield with a `LookupCached` per permanent.

**The board walk was pure waste on this deck, and that is checkable rather than arguable.** No card in
the Fungus list carries `dies_trigger_damage`, `dies_trigger_creates_tokens` or
`dies_trigger_impulse_exile` (checked against `cards.json`, 2026-10-01), so the walk ran to completion
and answered "no payoff" **every single time**, on the deck whose boards are the widest in the suite.

**Two fixes, and the second is the bigger structural one.** The board fact is hoisted into
`SubsetFilterPre` and computed in the SAME battlefield walk that already produces `board_persist` —
two extra field reads, not a second pass. When it is TRUE the filter's answer is a foregone `false`
for the whole enumeration, so `BuildSubsetFilterPre` now **clears `creature_sac_mana`** and the filter
is skipped outright, including its walks of `sel`. That direction does nothing for Fungus (no payoff
anywhere in the list) and everything for a deck that has one. It is also the first `SubsetFilterPre`
bit derived from the BOARD rather than from `cands`, which the struct comment now calls out: it is
exact because the test it stands for reads only the board, and the board is frozen for the
enumeration, so no selection could make the answer true.

**EXACT, not a bound — and checked rather than asserted.** `MTG_SAC_WASTE_AGG_VERIFY=1` runs both
forms on every subset and prints the first disagreement. On Fungus d3/s2002 (200 games): the
`[waste-agg] verify ARMED` line present, **zero mismatches**, avg turns 5.4050 — identical to the
pre-change baseline. The ARMED line is load-bearing: without it, "zero mismatches" could equally mean
the fast path never ran, which is the `unchanged-average-three-causes` trap one level down.

The one re-ordering worth naming: the original returns false on the FIRST selected action with
`direct_damage > 0`, before finishing the spend sum, whereas the aggregate sums everything and then
tests. Exact, because the answer is false either way — `direct_damage` rejects the subset outright, it
is not a term in the sum.

**`units` is the WRONG meter for this change, by construction.** It makes each subset visit cheaper
without altering how many subsets are visited, so the deterministic work meter reads identical on
both arms and proves nothing. That is the opposite of `MTG_FUNGUS_DEVOUR_LETHAL`, where the collapse
removed branches and `units` was exactly the right instrument. Pick the meter from what the change
moves: wall (paired, interleaved, idle box) for cost-per-visit, `units` for visit counts.

Hatch: `MTG_SAC_WASTE_AGG=0` reverts to the original per-subset function. It also stands down under
`MTG_NO_SAC_WASTE_PRUNE` (the filter is off outright, so building an index to reproduce a constant
would be the waste this exists to remove) and whenever no `sac_src_def` table was built — which is
what the `SubsetFilterPre` instruments disarm, so they disarm this too, for the same reason.

#### What it measured: 1.00x, and the profile says the cell was the wrong place to look

**Honest result first: no measurable effect on the `fungus d3 s2002` regression cell.** Paired,
interleaved, order-flipped, single-threaded, 200 games per run:

| pair | off | on | ratio | |
|---|---|---|---|---|
| 1 | 21.06s | 21.16s | 0.9955x | clean |
| 2 | 21.11 | 21.10 | 1.0007x | clean |
| 3 | 21.04 | 20.86 | 1.0085x | clean |
| 4 | 20.98 | 21.36 | 0.9823x | clean |
| 5 | 20.98 | 20.94 | 1.0019x | clean |
| 6 | 38.67 | 20.98 | 1.8432x | **CONTAMINATED** |
| 7 | 44.50 | 25.44 | 1.7496x | **CONTAMINATED** |
| 8 | 46.70 | 51.22 | 0.9116x | **CONTAMINATED** |

Clean pairs: **median 1.0007x — no effect.** Pairs 6–8 are host load, not signal: the same 200 games
that take 21s in pairs 1–5 take 38–51s there, and host loadavg went 3.36 → 23.41 across the run (and
loadavg in this container is the HOST's, see `loadavg-is-the-hosts-not-ours.md`). Pair 8 caught both
arms and reads 0.91x.

**The aggregate of all eight pairs is 1.1576x, and it is a FALSE WIN.** Reporting it would have
claimed a 15.8% improvement manufactured entirely out of noise that landed asymmetrically on the arms.
This is `wall-ab-aggregate-right-signs-wrong.md` happening live, and it is the reason the per-pair
rule exists: the per-pair column makes the contamination obvious at a glance, the aggregate hides it.

**And the profile explains the 1.00x rather than leaving it a mystery** — which matters, because an
unchanged number has three causes (no effect / never ran / backwards) and only a profile separates
them. `perf record -e cpu-clock -F 499` on `build/Profile/mtg`, 200 games per arm:

| symbol | arm OFF (original) | arm ON (aggregate) |
|---|---|---|
| `SubsetWastesCreatureSacMana` | **0.10%** | **absent** |
| `SubsetHasDuplicateSacSource` | 0.54% | 0.44% (same code; the delta is noise) |

**The symbol DID leave the profile — the change does what it was built to do — and it was only 0.10%
of this cell to begin with.** That is the whole explanation of the 1.0007x: a tenth of a percent is
one or two orders of magnitude below what a 21-second wall A/B on a contended box can resolve. So the
reading is "the fix works, and this cell is the wrong place to price it", not "no effect".

*(CORRECTION, same session: I first reported this symbol as "absent from EITHER arm". That was wrong
— my `grep` was truncated by a `head -8` that filled with `LookupCached` clones, so the line was cut
off rather than missing. `false-absence-from-truncated-reads.md`, applied to my own evidence. The
table above is the full read. Commit 6b591882's message carries the uncorrected claim; it was already
pushed, so the correction lives here rather than in a rewritten history.)*

The 1.04% figure §2e recorded for this filter came from the **keep-generation replay** (wide boards,
long rollouts), not from a regression cell. **That measurement was not redone**, for two reasons worth
recording: the `gencache.HEAD.json` in `logs/fungus_slowturn_analysis/` is now STALE against today's
engine, so the replay re-runs discovery (8,800 rollouts at ~8/s ≈ 18 min **per arm**); and the box is
contended, which makes wall from it worthless anyway. So the regime where this filter costs 1% is an
**open measurement**, and the change is justified on exactness plus work-removed, not on a wall number.
`CLAUDE.md`'s "collapse wasted search unconditionally" is the doctrine that makes that the right call.

#### 2g. The branching question has a terminus, and on the regression cell it has arrived

The user's method: *"keep pruning anything that seems unreasonable and then the any remaining
unreasonable items should be more obvious. Once all of those are gone the expectation would be that
whatever remains is not branching related."* Measured against the current engine, that state is here
for `fungus d3/d5 s2002`:

* **The odometer is tame.** `MTG_ENUM_STATS=1 MTG_ENUM_STATS_MIN=1000` over 200 games at d3 prints
  **exactly one** shape above 1e3: `bound=1.02e+03 groups=8 ind=2` (= 2^8 x 2^2), eight single-member
  groups plus one Mycon's two variants. d5/100 games is the same ceiling, `groups=9 ind=1`. Against
  the historical `bound=4.92e+04 groups=6 ind=8` that motivated this whole ledger, the explosive
  shapes are gone.
* **No subset filter is in the top 12 of the profile any more.** What is on top is per-NODE cost:
  `CollectActions` 3.80%, `BuildSimKey` 3.35%, `operator new` 3.35%, `SimulateEndAndStartNextTurn`
  2.64%, `PrePlanAvailabilityKeys` 2.38%, the `consider` lambda 2.13%, `SolveUncached` 2.10%.
* **The cell is 21x cheaper than its own suite comment claims.** `test/regression_cases.sh` still said
  "1,331.1s CPU ... d3_s2002 gi83 alone is 208s of that case's 507s"; that cell now runs its 200 games
  in **23.4s CPU** single-threaded, and gi83 replayed alone (`--seed 2085 --game-index 83`) wins on
  turn 5 with 2,657 enumeration calls. It is not a heavy game. The comment has been corrected in place.

**Two named branching items survive, both quantified, neither yet built:**

1. **55.0% of payoff-side lines are unaffordable under EVERY mana line** (2,536,895 of 4,610,578 at
   d3; 846,036 of 1,577,094 = 53.6% at d5). An unaffordable line is not a line, so dropping it cannot
   cost quality — this is lossless, and the largest remaining item by a wide margin over the sub-1%
   filters. **But read the prize carefully: it is 1.19x, not 55%.** The diagnostic models two-stage
   cost as `m_kept + p_lines + pairs_live` (576,151 + 4,610,578 + 4,481,318 = 9,668,047) against the
   flat 11,536,877 pairs, i.e. **1.19x fewer VISITS** — because you still enumerate every payoff line
   once even when no mana line can pay it. And §2c/§2e already established that neither the odometer
   nor the visit count predicts cost, only bounds it: the fodder prize was collected by making each
   visit cheap, not by making fewer visits. So 1.19x fewer visits is an upper bound on a restructure
   whose per-visit costs differ from today's, and it needs a profile before it is worth building.
2. **`SubsetHasDuplicateSacSource`**, the key-based rewrite described below.

**And one item that is NOT branching and should stop being treated as such:** the leaf-tie rate is
**52.2% at d3 and 67.1% at d5** (`published=60883 ties=31787`, `published=56261 ties=37754`). Two
thirds of leaf evaluations at d5 cannot separate their branches. That is an EVALUATOR resolution
question, not a width question, and no prune addresses it. Likewise `enum-memo` at a 1.4% hit rate
(1,234 / 88,145) and `solve-memo` at 21–29% are memo-key questions (`BuildSimKey`, 3.35% and the
3rd-biggest symbol), not enumeration questions.

### 2i. The heaviest turns and every branch on them — and a CORRECTION to §2g (2026-10-01)

USER: *"I want to get back to finding the most serious chunks of branching. It would also be a big
help if you can find the heaviest turns and get a list of all of the branches on it. If there is
nothing obvious to cut there, then please show it to me."*

The instrument already existed — `MTG_BRANCH_SHAPE=1 MTG_BRANCH_HEAVY=<N>` keeps the N heaviest
individual `EnumeratePlans` calls ranked by subset visits, with every option group rendered. Two
gaps in it were closed first, because without them the dump cannot be reviewed at all:

* **`AbilityModeTag` had no case for `SporeSaproling`**, so Fungus's single most common activated
  ability printed as `mode=mode9` on every line of the dump.
* **The aura variants printed as `[cast | cast | cast]`** — no distinguishing field whatsoever. Which
  land an Aura enchants is the *entire content* of a land-aura variant, so a reviewer could not tell
  a real choice from a pure symmetry. Now rendered as `:host=Forest` / `:host=Simic Growth
  Chamber(T)` (name + tap state, the two fields that decide interchangeability).

#### Which turns are heaviest

`fungus d3 s2002`, 200 games, `--threads 1`. 201,894 enumeration calls, 4,263,313 total odometer.

| turn | calls | sum_odo | share of odo | plans | avg_odo | avgGrp |
|-----:|------:|--------:|-------------:|------:|--------:|-------:|
| 3 | 33,465 | 677,586 | 15.9% | 113,976 | 20.2 | 3.65 |
| **4** | **63,841** | **1,596,771** | **37.5%** | 335,525 | 25.0 | 3.43 |
| **5** | **55,994** | **1,038,769** | **24.4%** | 259,285 | 18.6 | 2.76 |
| 6 | 25,659 | 476,882 | 11.2% | 129,584 | 18.6 | 2.63 |

**Turns 4–5 are 62% of the odometer.** d5 agrees (turn 4 36%, turn 5 29%).

#### What the heaviest decisions are actually made of

The top decision is `odo=1152, turn=7, visits=527`. There is **no catastrophic decision anywhere** —
mean width is ~21 and the single widest is 1,152. The cost is 200k calls of modest width, not a few
explosions. Three shapes recur across the top 14:

1. **The `Utopia Mycon` sac-for-mana group of 8** (`sacForMana:sacN=2..8`) — ranks 1, 3, 4. This is
   the **adopted** count pool (`MTG_SAC_OUTLET_POOL`, 2.29x, see `fungus-token-search-cost.md`
   Round 8), not an oversight. Its `plans == dedup` exactly (279/279, 204/204, 195/195): the pool
   emits **zero duplicate plans**. It is already clean.
2. **`Wild Growth` enumerated once per legal land host** — in 10 of the top 14. Rank 2 is the clean
   case: `size=4 [cast:host=Forest | cast:host=Forest | cast:host=Forest | cast:host=Forest]`,
   appearing **twice** (the deck runs 2 copies), i.e. 25 odometer slots on an axis with at most 4
   distinct outcomes. `plans=341 dedup=41` — 88% of the plans at that decision are duplicates.
   The card's own note explains it: *"WHICH land to enchant is a searched plan variant per legal
   host."* `LandAuraHostCandidates` exists to narrow this, but the **base returns empty = no
   narrowing** and only `EldraziFlickerProvider` overrides it; `FungusProvider` does not. On 19
   Forests those hosts are overwhelmingly the same object.
   **Rank 5 is why a naive collapse would be WRONG:** `[cast:host=Simic Growth Chamber |
   cast:host=Forest | cast:host=Forest]` — the Chamber is a genuinely different land. Any fold here
   must be by equivalence CLASS, not "keep one host".
3. **The same card getting two separate size-1 groups** — `Beastmaster Ascension` twice in ranks 6
   and 9, `Sporecrown Thallid` twice in 6, `Thallid` twice in 7, `Sporesower Thallid` twice in 3.
   Two copies in hand, each multiplying the odometer by 2, when the real menu is "cast 0, 1 or 2".

#### All three priced BEFORE building anything — and the obvious one is the worthless one

Added as measurement-only counters (`MTG_BRANCH_SHAPE`, default off, nothing applied):

| item | d3 | d5 | meter |
|---|---:|---:|---|
| **Re-enumeration** — the identical decision walked again | **1.92x** | 1.68x | **calls** |
| Interchangeable-copy fold (hand copies + aura host classes) | 1.32x | 1.29x | odometer |
| Land-aura host classes alone | 1.06x | 1.06x | odometer |

**The symmetry that dominates the heavy list is worth 1.06x.** It touches only 7.38% of calls
(14,896 of 201,894). This is the §2c lesson again in a new costume: ranking decisions by individual
weight points at the most *conspicuous* structure, not the most *expensive* one. Had the dump been
acted on without pricing, the work would have gone into the smallest of the three.

The copy fold is priced by the formula `C(s+k,k)` instead of `(1+s)^k` for `k` interchangeable
groups over `s` variant classes. `sac_victim_id` is deliberately KEPT in the signature, so it never
claims the Saproling-victim symmetry it has not checked. **It is an ODOMETER number and must be
discounted accordingly** — §2c measured −88% odometer buying −0.8% visits.

#### The re-enumeration number, and why it is the serious one

201,894 enumeration calls resolve to **105,235 distinct decisions**. The fingerprint is deliberately
strict: turn, phase, battlefield (name / tapped / spore counters / controller), the **exact hand**,
floating mana, both life totals, and the full option menu. A first draft keyed only on board + hand
*size* and read 2.06x; adding hand contents, floating mana and life moved it only to **1.92x**, so
the repeat is not an artefact of a loose key. Still unhashed — and so able only to shrink the prize,
never grow it — are remaining search depth, graveyard/exile, library order, and non-spore counters.

A second, independent instrument agrees. `MTG_CONSIDER_STATS` (built for the user's 2026-08-14
question *"figure out where we are considering spells multiple times"*) reports **777,861 harvests
over 542,498 distinct states = 30.3% duplicate calls**, concentrated in the FSLine nest:

| site | calls | distinct | duplicate |
|---|---:|---:|---:|
| `solve.m1.fs2` | 274,766 | 176,963 | 97,803 (35.6%) |
| `solve.m1.fs3` | 178,319 | 104,307 | 74,012 (41.5%) |
| `solve.m1.fs1` | 106,880 | 85,497 | 21,383 (20.0%) |
| `enum.m1.fs3` | 71,545 | 57,192 | 14,353 (20.1%) |

`CollectActions` is the **top symbol in the profile at 3.80%**, and ~30% of its calls re-harvest a
state it has already harvested.

#### CORRECTION to §2g

§2g called the two-stage gating item *"the largest remaining item by a wide margin"* at 1.19x fewer
visits, and filed the memo question under *"NOT branching and should stop being treated as such"*.
The first clause is **wrong**: re-enumeration is 1.92x of all enumeration calls, measured on the
call meter rather than the odometer, and that is a wider margin than 1.19x. The second clause was
right that it is a memo-key question rather than a width question — but §2g priced it only by the
enum-memo's 1.4% hit rate, which understated it badly. The enum-memo is the **breakpoint**
continuation cache (`BpEnumEntryFor`); it is consulted on 89,379 of 201,894 calls and does not cover
the main `EnumeratePlans` path at all. **There is no memo on that path.**

§2g's other findings stand: the odometer is tame, no subset filter is in the top 12, and the cell is
21x cheaper than its comment claimed.

#### Repro

```
MTG_BRANCH_SHAPE=1 MTG_BRANCH_HEAVY=14 build/Release/mtg decks/Fungus/Fungus.cod \
  --profile decks/Fungus/Fungus.profile.json --games 200 --seed 2002 --depth 3 \
  --budget-ms 10 --threads 1 2> dump.err
MTG_CONSIDER_STATS=1 ... same cell ...      # the per-site duplicate-harvest table
```

Gates on the instrument change: unit 343/343, scenarios 118/118, smoke 101/101 `play-changed=0`.
Everything added is inside `if (shapestats::Enabled())`, so the default path is untouched.

### 2h. `SubsetHasDuplicateSacSource`, half done — the per-clause preconditions (2026-10-01)

`SubsetHasDuplicateSacSource` is **1.48% on the keep replay and 0.54% on the d3 regression cell** — the
biggest of the trio and the first of them visible in BOTH regimes. It is **seven** independent
"do two selected actions collide on one source" clauses, and each opens by testing
`cands[sel[a]].kind` for every selected action. Fungus plays cards for two of them: it has no
planeswalker, no Garth, no blink outlet and no free cast, and still paid four kind comparisons per
selected action **per enumerated subset** to re-establish that.

**The built half: per-clause preconditions.** Each clause needs TWO selected candidates that match it,
so fewer than two matches in `cands` makes its answer a foregone false. `SubsetFilterPre` now carries a
`dup_clause` bitmask (the `kDup*` enum) beside its existing single `dup_source` bit — and the gap is
exactly the slack, because `dup_source` is set true by a **single** participating candidate, which is
the weaker condition.

Unlike the two sac filters next door this needs **no instrument disarm**: it only skips clauses that
would have returned false, so nothing is reordered, no clause changes which one reports a rejection,
and the `equiv_tag` canonical-prefix clause and every `bfcensus` / `FoldVerify` counter are reached
exactly as before. The fold VERIFIER at the recoverability check deliberately keeps the default
`kDupAll` — it has no `SubsetFilterPre` and must ask the full filter. Play is byte-identical on every
counter of the d3 cell.

**NOT separately measurable, and the attempt to measure it went wrong in a way worth recording.** On
this cell the symbol is ~0.4–0.5% and the mask removes a handful of integer comparisons inside it, so
the effect is below what a single perf run resolves. Worse, the before/after I tried to take is
**invalid**: `perf record --no-buildid` makes `perf report` resolve addresses against the binary *on
disk now*, so rebuilding `build/Profile/mtg` for the "after" silently destroyed the "before" sample.
The tell was the old profile printing the NEW signature (`..., int, bool, unsigned int`) for a
parameter that did not exist when it was recorded, and reporting 0.04% for a symbol that had measured
0.44% against its own binary an hour earlier. **Both numbers in that comparison were junk.** A perf A/B
has to record both arms before any rebuild — trivial with one binary plus a runtime flag, impossible
across two builds unless you keep the binaries or drop `--no-buildid`.

So this is kept on the same basis as §2f: exact, byte-identical, provably-dead work removed, per
`CLAUDE.md`'s collapse-waste-unconditionally doctrine — **not** on a measured win.

**The unbuilt half, still the different idea §2e asked for.** Every clause asks the same question in a
different key: *do two selected actions share `(clause, sac_source_id)`* — or, for the hand-slot clause,
*share `hand_index` with differing `free_cast`*. "Does this multiset of keys contain a repeat" is
`O(|sel|)` with one precomputed key per candidate, not seven pairwise passes, and that is the
algorithmic win rather than the constant-factor one taken here. It interacts with the census
instruments (splitting the pairwise clauses from the `equiv_tag` pass changes which counters a rejected
subset reaches), so it needs the disarm treatment and a `*_VERIFY` twin — build it that way or not at
all.

### The subset funnel — the real work unit (NOTE: see §2d, this section's cross-walk ratio is wrong)

| turn | subsets entered | dupSacSrc | wasteSacMana | overFodder | other | PASSED |
|---|---|---|---|---|---|---|
| 3 | 20,600 | 1,449 | 176 | 0 | 0 | 18,975 |
| 4 | 219,740 | 16,602 | 14,746 | 16,186 | 0 | 172,206 |
| 5 | 1,689,371 | 71,798 | 180,474 | 204,312 | 0 | 1,232,787 |
| 6 | 7,143,659 | 347,537 | 452,700 | 884,971 | 0 | 5,458,451 |
| 7 | 15,220,528 | 1,516,631 | 407,720 | 2,195,334 | 0 | 11,100,843 |
| 8 | 19,370,442 | 1,603,940 | 936,120 | 2,080,275 | 0 | 14,750,107 |
| **total** | **43,664,421** | 3,557,957 (8.15%) | 1,991,936 (4.56%) | 5,381,078 (12.32%) | **0** | 32,733,450 (74.97%) |

Three things fall out, and the first is the most important:

1. **The walk visits 43.7 M subsets to produce 5.1 M plans — 8.5 subsets per plan.** Every previous
   number in this document (including my own "5.4 million constructed plans") understated the work
   by nearly an order of magnitude. **43.7 M is the number an optimisation has to move.**
2. **The shared-resource constraint rejects 25.03% of the walk** — and `other` is **exactly zero**.
   No other predicate in that funnel rejects a single subset on this deck. §2a's reframing was right
   about the mechanism: the only thing throwing work away here is fodder/mana contention.
3. **The rejection is back-loaded**: turns 7 and 8 are 79% of all subset visits and 76% of all
   rejects. It tracks board width, as the mechanism predicts.

### 2c. DEVOUR WIDTH IS NOT THE COST (measured 2026-10-01, a NEGATIVE result worth keeping)

`MTG_BRANCH_HEAVY=3` on the same rollout showed the heaviest decision (turn 6, board 22) decomposing
as `14 x 14 (Mycoloth devour, two copies) x 3 x 3 (Burst X) x 5 x 5 (Mycon sac) x 2 = 88,200`.

**Why the devour ladder was 13 of 14 wide.** `DevourCountCandidates` emits one rung per *contested*
body and `big` is `EffectivePower() >= 2`. The eight Saprolings on that board are not 1/1s: Saproling
Burst mints them at a printed 0/0 and `RefreshFadeTokens` writes their live P/T as the Burst's
remaining fade counters, so every one read as big and bought its own rung. The fodder floor the
per-body ladder is built around **did not exist on the board where it was meant to pay**. (The `0/0`
in the token name is deliberate — a stable name for `m_name_hash` and the TT key as counters fall.)

**A landmark menu fixes the width and buys nothing.** `MTG_FUNGUS_DEVOUR_LANDMARKS` (default OFF)
replaces the ladder with a computed menu — decline / free-fodder prefix / last-irreplaceable-outlet /
Beastmaster quest rungs / eat-all, at most five and independent of board width, the same shape
`FadeKLandmarks` already uses for the Burst's own k.

| | off | on | delta |
|---|---|---|---|
| win turn | 7 | 7 | unchanged |
| wall | 11,714 ms | 11,831 ms | **+1.0% (no gain)** |
| odometer | 56,017,966 | 19,742,411 | −64.8% |
| plans | 5,134,973 | 4,488,263 | −12.6% |
| **subset visits** | 43,664,421 | 43,313,150 | **−0.8%** |
| heaviest call | 88,200 | 10,368 | −88% |

The conclusion rests on the deterministic counter, not the timing: **subset visits moved 0.8%.** The
walk was already pruning those devour branches, so the odometer was counting a space nobody walked.

Two things follow, and both constrain what to try next:

* **The odometer bounds the prize and does not predict it** — now measured twice on this deck. An
  88% cut to the heaviest decision's space was worth zero wall.
* **Plan construction is not the bottleneck either.** Plans fell 12.6% with no wall change. What is
  left with real mass is the **43.7 M subset visits** and the per-subset predicate work inside them.

This also confirms, at a far larger cut, what `DevourCountCandidates`' own comment already said and an
earlier draft of this document cited against it: *"worth ~6.6% at d1/b3 … It is NOT a fix for the slow
rollouts … Do not cite it as one."*

### How big is the prize, honestly

25.03% of visits is what a leaf-level constraint currently discards. That is **not** a 4x, and the
temptation to read it as one should be resisted — it caps a leaf-level fix at about **1.33x**.

**And the obvious escalation does not work as stated.** I expected `SubsetOversubscribesSacFodder` to
be monotone in the subset — if a prefix already oversubscribes, so does every extension — which would
let the walk prune whole subtrees rather than leaves and blow well past 25%. Reading it, **it is not
monotone**: adding a candidate can add *supply* as well as demand (`plan_fodder_credit` counts a cast
creature as +1 body, and a spore pop as `k x spore_creates_tokens`), so a violating prefix can become
legal by extending it with a creature cast.

That does not kill prefix pruning, it just makes the sound version weaker than the leaf test: prune a
prefix only when `demand > supply + (maximum fodder any remaining candidate could still add)`. That
bound is computable and it cuts subtrees, so its saving in *visits* can exceed 25% even though it
rejects fewer *leaves*. **Whether it does is the next measurement, and it is now a well-posed one.**

## 2. Where the time goes — the flat profile (read §2a first)

Flat profile, grouped by family (98.9% of samples accounted):

| family | self % |
|---|---|
| odometer / plan enumeration | **14.82** |
| mana payability (`CanPay*`, `ManaPool`, `ManaGate`, `LiveManaGrant`) | **10.84** |
| allocation / vector churn (`operator new`, `~vector`, `memmove`, `push_back`) | **9.60** |
| subset-REJECT predicates (`SubsetOversubscribesSacFodder`, `…HasDuplicateSacSource`, `…WastesCreatureSacMana`) | **6.76** |
| `SolveUncached` per-subset callbacks | **6.39** |
| memo keying + compare (`BuildSimKey`, `memcmp`) | **6.14** |
| card lookup / subtype | 4.55 |
| **turn simulation / ETB / token creation** | **2.65** |
| other (953 symbols, none above 0.95%) | 37.19 |

The hottest single symbol is **5.66%**. Contrast the 2026-09-17 investigation, where one function
(`CountControlledDragons`) was 45% and a one-signature fix bought 1.43x. **There is no equivalent
single win here.** The 37% "other" was checked symbol by symbol: its largest entry is
`PrePlanAvailabilityKeys` at 0.95%, and the remaining 953 symbols average under 0.03% each.

## 3. The volume that explains the diffuseness

`MTG_BRANCH_STATS` for the isolated rollout (warm cache, so digest + this one rollout):

```
EnumeratePlans calls = 117,937     odometer space = 59,984,021
plans constructed    =  5,448,014  after dedup    =  5,068,608
leaf evals published =    508,766  of which TIES  =    345,188  (67.8%)
solve-memo           = 415,333 hits / 558,908 misses  (42.6% hit rate)
```

**Read `odometer space` carefully — it is not work done.** `odo = Π(1 + |group_i|) × 2^independent`
(`TurnSolver.cpp:38146`) is the *size of the space*, and `EnumeratePlanPositions` prunes during the
walk, so only 5.45 M plans are actually built out of a 60 M space. An earlier draft of this note read
the 60 M → 5.07 M gap as "91% discarded by dedup"; that was wrong. **Dedup removes only 7%**
(5.45 M → 5.07 M). The pruning is already happening inside the walk.

So the cost is **~5.4 million constructed plans for one rollout**, at ~46 plans per
`EnumeratePlans` call, each one paying plan construction, a payability check, the subset-reject
predicates and a memo probe. That is why the profile is flat: the per-plan overhead is spread across
a dozen functions and the volume is the multiplier.

## 4. Where the volume concentrates

By driver (the card owning the largest option group of size ≥ 2):

| driver | calls | odo space | share of odo | avg odo | max odo |
|---|---|---|---|---|---|
| `Mycoloth  [+1 tied]` | 10,944 | 44,239,585 | **73.8%** | **4,042** | 88,200 |
| `Utopia Mycon` | 29,468 | 5,898,273 | 9.8% | 200 | 28,800 |
| `Mycoloth` | 20,388 | 4,303,915 | 7.2% | 211 | 9,000 |
| `Utopia Mycon  [+2 tied]` | 946 | 1,032,710 | 1.7% | 1,092 | 16,000 |

**9.3% of calls carry 73.8% of the odometer space.** By situation the concentration is sharper still:
`groups=5-8 board=16+` is 8,520 calls (7.2%) and 38.7 M odo (64.5%), avg 4,538.

**What `[+1 tied]` does and does NOT mean.** It means one other option group had the *same size* as
the largest, so the instrument declines to attribute the call to a single card
(`TurnSolver.cpp:38176`) — the label exists because a previous version of this table blamed Mana
Cannons for 15% of branching purely for being cheap and enumerated first. It is an **attribution
ambiguity marker, not evidence that the groups are interchangeable.** I initially read it as
fungible-duplicate groups; the code does not support that, and proving or refuting fungibility is the
next piece of work, not a conclusion of this one.

## 5. This REFUTES the standing hypothesis

`fungus-doubling-season-rollout-tail.md` §4 proposed that the cost is the second main's per-turn
`GameState` copy plus enumeration, scaling with **board width**, with Doubling Season compounding
width turn over turn. Two measurements contradict it:

- **Turn simulation, ETB watchers and token creation together are 2.65%.** Playing the turns —
  including every token Doubling Season doubles — is nearly free. A width-driven copy cost would
  appear here and does not.
- **Option-group count dominates board width in the odometer.** `groups=9-12 board=0-6` averages
  2,928 odo on a *narrow* board, while `groups=0-4 board=16+` averages 297 on a wide one — an order
  of magnitude the other way. Width matters only through how many groups it creates.

That is now the second structural hypothesis about this deck to be refuted by a profile (the first
was `GameState` deep-copy scaling, 2026-09-17, also plausible, also wrong). The pattern is worth
naming: **on this deck, "the board is wide so copying is expensive" keeps being the wrong answer, and
"the decision space is wide" keeps being the right one.**

## 6. What is NOT yet known — the honest boundary

1. **Are the tied groups actually fungible?** If two equal-size option groups are permutations of an
   unordered choice, the odometer is enumerating the same decision twice and that is pure waste. If
   they are genuinely distinct, the width is earned. §4 cannot tell these apart. This is the single
   highest-value next measurement, and `sac-outlet-count-pool-refuted.md` records a previous
   count-pooling attempt at this that was **refuted at 2.29x**, so it needs care.
2. **Why is the solve-memo hit rate only 42.6%?** 558,908 misses for one rollout. Either the key is
   too specific (`BuildSimKey` is 1.97% of profile) or the states genuinely differ. If plans that tie
   at the leaf are producing distinct sim keys, the memo is missing on states it should share.
3. **Why do 67.8% of leaf evaluations tie?** A tie means the evaluator cannot separate two plans. If
   the search is constructing millions of plans that are indistinguishable at the leaf, the question
   is whether they are distinguishable at all — and if not, where the earliest point is at which they
   could be collapsed.
4. ~~**Per-turn shape.**~~ **ANSWERED in §2b** (`MTG_BRANCH_SHAPE`): turn 6 holds 73.7% of the
   odometer space, turn 8 holds 53.7% of the constructed plans, and the walk visits 43.7 M subsets —
   8.5x the plan count everything above is written around.

5. ~~**Does a sound prefix bound beat the leaf test?**~~ **ANSWERED in §2e, and the answer is that
   the question was aimed at the wrong half.** The leaf test is 6.61% of the run and the two filters
   a prefix prune would additionally skip are together 2.3%, so the bound was chasing the smaller
   number — and it would have had to be the *weakened*, non-monotone-safe form. Re-associating the
   leaf test as a per-candidate aggregate (`FodderIndex`) is exact and measured **1.131x** on the
   heaviest cell. A prefix prune is still available on top, now worth at most ~1–2% and needing
   odometer surgery in a path that has OOM'd before; it is not the next thing to do.

6. **Why does turn 8 dedup only 0.4%?** Every other turn removes 4-23%. Turn 8 builds 2.76 M plans
   and discards almost nothing, on the horizon edge where `fungus-value-leaf-status.md` says 99.7% of
   evaluations already sit. Either those plans are genuinely distinct, or the dedup key is too
   specific exactly where it matters most — which is the same suspicion as the 42.6% memo hit rate in
   item 2, one layer up.

## 7. Artifacts

* `logs/fungus_slowturn_analysis/replay1.err` — cold-cache run, process-wide branch stats
* `logs/fungus_slowturn_analysis/replay2.err` — warm-cache run, the isolated numbers in §3/§4
* `logs/fungus_slowturn_analysis/gencache.HEAD.json` — equivalence cache at current HEAD (reuse this)
* `logs/fungus_slowturn_analysis/slow_keep.txt` — the 3,288 slow keep-rollouts of the cancelled run
* `logs/fungus_journal_backup/` — the cancelled run's 350 MB journal, gencache and slow.log, byte-identical to what it left behind
* `/tmp/slowroll.perf.data` — the perf sample (not durable)

**The cancelled run's own tail, for scale:** 3,288 slow keep-rollouts ≥ 30 s totalling **199 core-hours**,
median 38 s, p90 232 s, worst **25,712,961 ms (7 h 08 m) in a single rollout**. Doubling Season lift is
5.73x with a per-copy dose response (0 copies 71 s → 2 copies 744 s → 4 copies 960 s); `Psychotrope
Thallid`, despite appearing in the worst hands, has lift **0.90x** and is a passenger, not a driver.
