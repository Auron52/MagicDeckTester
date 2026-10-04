# Remove a mass death's bodies in ONE pass instead of one erase per death

**Status: IMPLEMENTED.** The guard this doc specified was audited and built; see
`MassDeathBodiesAreUnobservable` / `ApplyMassDeathBulk` in `src/core/SpellEffects.h`.

This was the last item on the Fungus candidate-b straggler's critical path. With it, that rollout
is **82,678 s -> 227 s = 363x** and the profile is FLAT (top symbol 7.39%).

## 1. The defect

`SweepDeadFadeTokens` and `DestroyTokensCreatedBy` erased one body at a time:

```cpp
state.battlefield.erase(state.battlefield.begin() + i);   // memmoves every element above i
OnCreatureDies(state, ctrl, dead, tok, minus, dwatch);
```

`sizeof(Permanent) == 296`, so a sweep cost O(deaths x board x 296 B), and the search re-runs that
end step on every line it explores. A Saproling Burst under three or four Doubling Seasons has made
dozens of tokens on a board of hundreds; when its last fade counter goes they all die at once.

## 2. Why the obvious fix is not automatically identical, and the guard that makes it so

Removing the bodies first and only then firing triggers is one stable compaction — but today each
`OnCreatureDies` still sees the bodies that have not been processed yet. So the fast path is taken
only behind a proof. The condition, derived by auditing every board read reachable from
`OnCreatureDies`:

| | condition | what it closes |
|---|---|---|
| (a) | every dying body is `def_absent` | every watcher loop's `LookupCached` returns null and skips it — `FireCreatureDiesWatchers`, the `reactions` gather, `DoublerShift`, `MinusCounterReplacement` |
| (b) | every dying body is a token | the persist and Worldspine blocks (`!dead_was_token`) stay shut, so nothing with a definition can be PUT onto the battlefield mid-sweep |
| (c) | every dying body has no coloured pip | `DevotionTo` is the one reachable walk that does NOT look a definition up — it sums `card.m_mana_cost` directly |
| (d) | no watcher on that side reaches `GainLife` | **the subtle one** — see below |
| (e) | one controller | makes (d) a question about one side |

**(d) is why this needed a guard rather than a comment.** `GainLife -> FireLifegainWatchers` carries
a `lifegain_each_own_creature_counters` loop that puts a +1/+1 counter on EACH own creature. With
the bodies still present that loop would both count them and *actually save a 0/0 token that was
about to die*. That is a real play difference, not a reordering. Checking `BoardSources::dwatch`
suffices: a `dies_trigger_self_gain` watcher with an empty `dies_watch_subtype` can never enter
`reactions`, and a dying body cannot contribute its own self-watcher because (a) makes its
definition lookup fail.

Under (a)–(e) a doomed body is invisible to every observer, so folding the removals is identical by
construction. Trigger order is preserved exactly (descending index, as the one-at-a-time loop used),
and the toughness verdict is hoisted into one `is_dead` lambda shared by both paths so they cannot
drift. Pre-collecting the dying set is itself valid under the proof: an admitted trigger can deal
damage, create vanilla tokens or exile from the library, none of which changes a survivor's
toughness.

## 3. TWO MEASUREMENT CORRECTIONS — read these before trusting any number in a profile

**A ~70 s window of a ~260 s run is not a share of the run, and I over-read one by 7x.** The first
version of this doc led with `79.71% __memmove_avx_unaligned_erms` and predicted ~5x. The
implemented fix delivered **1.12x** (258,932 ms -> 230,845 ms). Profiling the WHOLE rollout at
`-F 97` instead showed the truth: no symbol above 7.39%, and memmove not in the top 20 at all. Its
real whole-run share was ~11%.

The cause is that this game's turns differ by three orders of magnitude in cost (turn 6 was 2,204 s
before any fix; turns 1–5 together were 2.7 s), so where `perf` happens to attach decides the
answer. **Sample the whole unit of work, or state explicitly that you did not.** This is the same
family of error as [[profile-is-scoped-to-one-cell]] and
[[average-understates-a-width-scaled-defect]], one level down: not the wrong cell, the wrong
*window within* the right cell.

**Shrinking `Permanent` is NOT the risk-free big win this doc first claimed.** Measured:

```
Permanent      296
  Card         128      <-- 43%
  CounterList   52      <-- 18%
  InternedName    8
  (47 scalars) ~108     <-- 20 ints + 22 bools + padding
```

`Card` + `CounterList` are **61%** of the struct, so packing the 20 small counter `int`s into
`int16_t` and bitfielding the 22 `bool`s recovers perhaps 55 bytes — about **1.2x** on the stride,
for a change that touches the whole engine. The earlier framing ("pays off on all of them at once
and cannot change behaviour") was true but mispriced; the win is in `Card`, which is a far deeper
change. Do not reach for this before measuring where the bytes are.

## 4. Reproduction

```
MTG_KEEP_REPLAY="Vitaspore Thallid (+1) x2; Utopia Mycon x1; Secluded Courtyard x1; Peat Bog (+1) x1; Doubling Season x1"
MTG_KEEP_REPLAY_R=20  MTG_KEEP_REPLAY_PD=0  MTG_DECISION_WORK_X=1000
```

Run in a directory holding the deck's `buckets.json` **and** `gencache.json` — without both,
`policy_fp` misses and the probe burns cores re-running depth-5 discovery multithreaded. It is
journal-safe: the replay block returns before `journal_path` is ever assigned. Current: **227 s**,
`win_turn=7`.

## 5. Where the wall went

| | elapsed | vs previous | cumulative |
|---|---|---|---|
| generation's own slow log | 82,678 s | — | — |
| `ae97edbf` two board scans per creature | 342 s | 242x | 242x |
| `39686e79` death-watcher prefilter | 259 s | 1.32x | 319x |
| this change | 227 s | 1.12x | **363x** |

`win_turn=7` at every step.

## 6. What is left, and the warning that goes with it

Nothing above 7.39%, spread across mana/action enumeration (`EffectiveSpellCost`,
`CollectActions`, `PermanentManaYield`, `LiveManaGrant`, `AddSourceToPool`). Per the repo's
collapse doctrine that is the expected terminal state: *"Once all of those are gone the expectation
would be that whatever remains is not branching related."* Further work here is a broad per-node
campaign with no single target, not another defect hunt.

`GatherBoardSources` is now 4.57% — the prefilters' own cost. `SweepDeadFadeTokens` gathers
eagerly on every call because its `is_dead` test needs the lists; making that lazy is a small, safe
follow-up.

**Do NOT price any of this on the straggler alone.** The precedent is explicit: 1.53x on the worst
cell was 1.05x on the job. The aggregate needs a fixed-window, same-odometer-region journal-rate
comparison, the method used for the haste/bulk-token rounds. What the straggler legitimately
establishes is a makespan fact — the generation's worst single rollout fell from ~23 h to ~4 min,
which is what made it a barrier.

Related: `mulligan-rollout-performance-floor.md` (the case set and this straggler),
`keepgen-straggler-task-splitting.md` (what to do when a rollout is still slow),
`keepgen-producer-barrier-and-durability.md` (Defect 3, the barrier it held).
