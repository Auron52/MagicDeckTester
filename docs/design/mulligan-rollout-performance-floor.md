# A keep rollout must not take hours — the mulligan-settings performance floor

**Status: OPEN. User-raised 2026-10-03, during the Fungus candidate-b K=17 generation.**

## The ruling, quoted rather than paraphrased

> *"Either way, let's note down that game as this level of performance is not acceptable, especially
> under mulligan settings. I'll want to fix it anyway."*

This is a **standing quality bar**, not a request scoped to one run. It was raised while a single
in-flight keep rollout had blocked the generation's floor-completion barrier for **4.7 hours**.

## Why "especially under mulligan settings" is the whole point

A keep rollout for this deck runs at the settings the value sidecar dictates
(`decks/Fungus/candidate-b-2026-09/Fungus.value.json`, `value_play`):

| setting | value |
|---|---|
| `mull_gen_depth` | **1** |
| `mull_gen_budget_ms` | **3** |
| `max_turns` (horizon) | **8** |

**Depth 1. Three virtual milliseconds. Eight turns.** A rollout under that configuration taking
hours is not an expensive search — it is a search whose stated budget has no relationship to what it
spends. A plausible upper bound for one such rollout is single-digit seconds; the observed worst is
four orders of magnitude past that.

This is the sharpest available framing of the defect and it should survive into whatever fixes it:
**the cost is not bounded by anything the configuration says.**

## Reproducible case set (all on the CURRENT binary unless marked FIXED)

The instrument is `MTG_KEEP_REPLAY`, which reruns exactly one keep-rollout byte-identically to the
generator's own `run_one` and then exits. `pd`: `draw` = 0, `play` = 1. Run from a directory holding
the deck plus its `*.keepmodel.gencache.json` so discovery is a cache hit (~90 s of startup).

```bash
MTG_KEEP_REPLAY="<hand exactly as the slow.log prints it, (+N) included>" \
MTG_KEEP_REPLAY_R=<r> MTG_KEEP_REPLAY_PD=<0|1> MTG_DECISION_WORK_X=1000 \
build/Profile/mtg-analyze Fungus.cod --cards-json src/cards/data/cards.json --gen-mulligan fast
```

| # | cell | r / pd | in generation | current binary |
|---|---|---|---|---|
| 1 | `Wild Growth x1; Utopia Mycon x1; Peat Bog (+1) x1; Doubling Season x4` | 0 / draw | 43,647,925 ms (12.12 h) | **28.7 s — FIXED** by `632fde7a` |
| 2 | `Vitaspore Thallid (+1) x1; Sol Ring x1; Doubling Season x3; Brightcap Badger (+2) x2` | 1 / play | 42,249,670 ms (11.74 h) | **>13 min, stopped unfinished — OPEN** |
| 3 | `Wild Growth x2; Utopia Mycon x1; Sol Ring x1; Doubling Season x3` | 5 / draw | — | **1,444,573 ms (24 min) — OPEN** |
| 4 | `Wild Growth x1; Utopia Mycon x2; Forest (+1) x2; Doubling Season x1` (size6) | 23 / draw | — | **1,389,127 ms (23 min) — OPEN** |
| 5 | the in-flight straggler, identity TBD | — | — | **>4.7 h — OPEN** |

**Case 2 is the primary repro**: it is the cheapest reliable way to sit inside the defect, and it was
measured at `>=54x` faster than before the soulbond gate yet still unfinished at 13 minutes. Case 4
matters because it is a **size-6** hand and a **high r**, proving this is not confined to size-7
floor cells.

**Capture case 5 when it lands.** It will append to
`decks/Fungus/candidate-b-2026-09/Fungus.keepmodel.exhaustive.raw.json.slow.log` with its hand, size,
`r`, `pd` and seed. That file is append-only and gitignored, so copy the line into this document
before the directory is cleaned.

## What has already been ruled out — do not re-derive these

The 12.12 h cell was **one defect**, now fixed: an ungated full-board soulbond scan in
`FireEtbWatchers`, cost `O(entrants x board_width)`, deck-gated in `632fde7a`
(15.07% -> 0.40% of profile; 2.4x of generation wall). It did **not** close the axis — cases 2-5 are
all post-fix. See `slow-rollout-tail-and-the-uncharged-greedy-walk.md` AMENDMENT 3.

Six censuses run against case 1 post-fix and against the live run, all clean
(AMENDMENT 3b in the same doc):

* `perf`: **flat** — top symbol 3.48%, biggest category 17.3% (mana payment), ~600 symbols.
* odometer: **74 positions per enumeration call** average, worst turn 174. **Not wide.**
* plan dedup: removes **3.3%**. No duplicate-plan explosion.
* re-enumeration: **1.27x**, perfect memo removes `<=21.2%` (explicit upper bound). Consistent with
  `enum-memo`'s 2.8% real hit rate.
* board width: avg **15.1** permanents by turn 8. Not a token swarm.
* memory: RSS 8.5-9.1 GB against a ~35 GB cap. No refusal-driven recompute.

**So the cost is NODE COUNT and per-node cost, not branching width.** That is the same conclusion
FiveColour reached ("the 23x is MANY nodes, not wide ones"). Anyone starting here should NOT begin by
hunting a wide odometer or a hot symbol — both have been measured and are not it.

## Why none of the existing mitigations bound this

1. **The keep-rollout path has NO wall backstop.** `MTG_MAX_GAME_WALL_SEC` /
   `MTG_MAX_GAME_PREDICT_SEC` are read only in `BatchRunner.cpp`. `capture_slow` *reports* a slow
   rollout; nothing stops one.
2. **`MTG_DECISION_WORK_X` cannot bind it.** The ceiling counts work units, and a unit is one
   simulated turn-step — so the greedy subset walk inside a step, and the whole ETB enter cascade,
   charge **nothing**. X=1000 is already adopted generation-side and removes 24.6% of billed units;
   it is measuring a quantity that is not where the time goes.
3. **The precompute filler protects UTILISATION, not LATENCY.** When a straggler holds
   `floor_incomplete`, kind -1 tasks keep all 24 workers busy banking future rollouts (observed:
   22.7 cores, `pre` climbing past 1.9 M). That is working exactly as designed and it is why the 4.7 h
   was not *wasted* — but the phase still cannot advance, and nothing bounds the wait.

## The structural ask

`fungus-doubling-season-rollout-tail.md` section 6 already specifies it and it remains the right
shape: **bound a rollout by REACHABLE STATES rather than by a wall clock** (the labeller precedent —
a wall-clock bound would make the artifact machine-dependent and non-reproducible), and **record a
truncation when it bites** so a truncated cell is not silently scored as converged.

Two scheduling facts worth knowing before designing it:

* **The floor-completion barrier is ONCE-ONLY.** Refs must be fixed from a complete floor snapshot,
  so one outstanding rollout gates that single transition. Afterwards each cell freezes
  independently off the fixed refs and "cores stay full to the last live cell".
* **But the FINISH barrier recurs.** The run cannot complete until its last live cell lands, so a
  degenerate draw late in the refine phase stalls completion the same way.

## Acceptance criteria (proposed, for the user to adjust)

1. **No keep rollout exceeds a fixed reachable-states budget** at `d1/b3`, chosen so the current
   median rollout (~0.5 s) is unaffected. Cases 2-5 must all complete in seconds.
2. **A truncation is recorded per cell** and surfaced in the raw sidecar + the monitor, so a
   truncated cell is distinguishable from a converged one and cannot silently enter a keep table.
3. **The bound is deterministic** — a pure function of the state, never of wall clock or thread
   count, so two machines produce byte-identical raws (the pooling precondition).
4. **Gate as a play change, not a generation rule.** Per the user's standing directive —
   *"I don't want generation-side rules. You should work to make my heuristic workable for play"* —
   this must earn its place in play and be inherited by generation, not switched on only for gen.

## The cost of NOT fixing it

It invalidates every ETA. On this run the floor barrier cost **4.7 h of a projected ~20 h**, and the
projection could not be stated with confidence because the blocking quantity is unbounded. A
generation whose completion time is set by one unbounded rollout is not a schedulable job.

Related: `fungus-doubling-season-rollout-tail.md` (section 6, 6a),
`slow-rollout-tail-and-the-uncharged-greedy-walk.md` (AMENDMENT 3, 3b),
`fungus-token-search-cost.md` (rounds 7-10, the collapses already harvested),
`per-decision-work-census.md`.

---

## Tested: do the 84 incoming commits fix this? NO — they are ~5% MORE work

2026-10-03. A concurrent agent pushed 84 commits touching 28 `src/` files, including several
explicit perf commits (five frozen-board/compacted-walk enum collapses, a `BuildSimKey` buffer
realloc fix, `EffectiveSpellCost` walking the battlefield three times, the no-Aura plan sort,
`MTG_FREE_CAST_HOIST` default ON) and one that looked directly relevant to this defect --
`2e85e9fa feat(search): finish a game the search has proven unwinnable`.

**Tested in a separate worktree** (`git worktree add /tmp/mdt-origin origin/<branch>`, built with
`taskset -c 0-7 ./build.sh profile` so the live generation kept the rest of the box). Each arm used
its OWN binary **and its own `cards.json`** -- that file is in the change set, though the diff is
`301 insertions, 0 deletions`, purely the new Kitty cards, so Fungus's card data is untouched and
this is a clean engine-only comparison.

### Method, and why the obvious version of it fails

The first attempt -- 100 goldfish games at `d1/b3`, interleaved, 3 reps -- was **inconclusive and had
to be thrown away**: OLD 39.9/28.5/47.7 s against NEW 35.4/37.2/62.4 s. Within-arm spread (28.5 ->
47.7) exceeded anything between arms, because the box was shared with a live generation. It also
contained **zero slow games**, so it never touched the tail. Naturally-drawn hands do not reproduce
the degenerate cells; keep rollouts start from a FORCED hand.

What worked: hunt degenerate games at mulligan settings first
(`--games 1500 --depth 1 --budget-ms 3 --ignore-play-profile MTG_SLOW_GAME_MS=8000`), then replay the
worst individually, single-threaded, interleaved, two reps -- and judge on **`units`, which is
deterministic and thread-invariant and therefore immune to load**, with wall only as corroboration.

### Result (seed base 424200, `--game-index gi`, d1/b3)

| game | OLD units | NEW units | delta | OLD wall | NEW wall | wall |
|---|---|---|---|---|---|---|
| gi=25 | 154,313 | 185,496 | **+20.2%** | 11.2 s | 12.7 s | 1.13x |
| gi=8  | 255,619 | 262,113 | +2.5% | 9.0 s | 10.4 s | 1.15x |
| gi=19 | 156,922 | 155,736 | -0.8% | 8.1 s | 9.0 s | 1.11x |
| gi=60 | 228,858 | 233,943 | +2.2% | 6.9 s | 7.3 s | 1.07x |
| | | | **mean +5.2%, median +2.5%** | 8.8 s | 9.8 s | **1.12x, NEW slower 4/4** |

**`wt` (win turn) is IDENTICAL on every game and every rep** -- 6,6,6,6,6,6,5,5. No quality change
here; the search explores differently and arrives at the same answer.

### Reading it

* **Origin's engine does not fix this defect. On this deck's degenerate games it is ~5% more work
  units and ~1.12x more wall.** The units figure is the trustworthy one; the wall deltas are
  individually inside the noise but agree in SIGN on 4 of 4 game-means, which is the only reason to
  quote them.
* The +5.2% mean is carried by gi=25 alone; the other three are within +-2.5%. Call it
  "a wash to slightly worse", not "a 5% regression".
* **A coherent mechanism:** `d1832b52 refactor(search): no greedy or heuristic substitute inside the
  search window` REMOVES heuristic shortcuts, so the search does more real work -- more units is the
  expected sign. The perf collapses offset part of it. Their measured wins were on kittyv2, hinata
  and antilife; **a perf gain is scoped to the deck it was measured on**
  (`perf-ratios-are-scoped-to-deck-artifacts`), and Fungus is not in that set.
* **The play digest moved**, measured directly: keepgen's 64-game battery gives
  `86004bde6b4b45ae` on origin against `5672dd070faaa160` on this branch. So a rebase would
  invalidate the banked journal, and the discovery gencache misses too -- full discovery is 8,800
  rollouts at ~2/s, about **73 minutes**, which is why the exact keep-cell replays were not run on
  the origin arm.

### Consequence for the decision

Rebasing mid-generation would have cost the 503,409 banked cell-sides AND produced a **slightly
slower** run. Letting the in-flight generation finish on this branch is both the cheaper and the
faster choice. **This defect has to be fixed on its own; no incoming work does it.**

**Caveat on scope:** these four games are the 7-31 s class at `d1/b3`, not the multi-hour class. They
are the right proxy available without paying 73 minutes of discovery per measurement, but they do not
prove anything about cases 2-5 above. If origin's engine is ever the baseline, re-run cases 2-5
against it before concluding.
