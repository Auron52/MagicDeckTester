# The minotaur d5 regression cells are NONDETERMINISTIC in the full tier

**Status: FIXED 2026-08-30** — root cause below, one-line fix in `TurnSolver.cpp`
(`FullSearchLineHybrid`). Found and closed the same day. Kept in full because the *method* is the
reusable part: three wrong root causes were published before the right one, all from the same
mistake.

**No ground truth moved.** The committed GT for both cells was the CORRECT (clean) value all
along; the flake produced a wrong answer some fraction of runs. Post-fix the reproducer returns
`4.9600/52937a4626b089b6` and `4.9040/8ec3f5ad5d923ca2` — byte-identical to `regression_gt.txt` —
8/8 runs. Regression tier 80/80, smoke 48/48, units 679/679, scenarios 42/42.

> An earlier revision of this doc said the committed GT was "a snapshot of one coin flip" and later
> that it was the *contaminated* face. Both were wrong: the mapping runs the other way.

## ROOT CAUSE — a thread_local array read by decks that never write it

`g_probe_leaves[]` / `g_probe_cost[]` (`TurnSolver.cpp`, ~line 22997) hold the probe's per-depth
leaf structure. They are `thread_local`, and were **cleared and written only inside the predictor
branch** of `FullSearchLineHybrid` — which a deck enters only when it ships
`value_play.escalation_cap > 0` (or under `MTG_ESC_PREDICT`).

But the **path-to-trust start-gate override** (`MTG_ESC_TO_TRUST`, ADOPTED default-ON) *reads*
`g_probe_leaves[pass_depth - 1]` on every value-leaf deck that ships a `value_trust_depth`,
**including decks that never record it**:

```cpp
const double leaves  = g_probe_leaves[pass_depth - 1];   // never cleared for a non-recording deck
const double avoided = g_trust_R * leaves;               // g_trust_R = 120
if (path <= slack * (remaining + avoided)) { fits = true; }
```

So a pooled batch worker handed one deck's leaf counts to the next deck's game. Measured at the
divergent decision (Minotaur seed 2042 gi 40, turn 1):

| run | `g_probe_leaves` | `avoided` | pass 5 | result |
|---|---|---|---|---|
| isolated | `0/0/0/0/0` | 0 | rejected | committed=4, escalates → `fefcd063588d3038` |
| pooled, clean | `5/41/221/0/0` | 0 | rejected | committed=4, escalates → `fefcd063588d3038` |
| pooled, POISONED | `15/114/1668/`**`68259`**`/0` | 8.19M | **starts** | committed=5, verified, NO escalation → `de6eeed37c0e6af9` |

Passes 1–4 cost *identically* in both (`4/115/2707/113593`); only pass 5 differs (`0` vs `1642`).
The budget limit is 18,000 units and 116,419 were already spent, so `Remaining()` is 0 — pass 5 can
only start through the `avoided` credit, and that credit was **a Knights game's leaf counts**.
Committing depth 5 makes the line *verified*, which cancels the escalation, so the weak value-leaf
line ships instead of the escalated heuristic one — a different play at the same win turn.

**Why exactly two cells.** Across all 16 decks with a value sidecar, **Minotaur is the only
reader-without-writer**: `value_trust_depth = 5` (arms the trust push) and no `escalation_cap`
(never clears). Every other deck with a trust depth also records, so it clears the arrays itself;
decks with `trust = 0` never arm the push at all.

**The fix.** Clear both arrays unconditionally before the probe. They describe *this* decision's
structure; reading another decision's — let alone another deck's — is never correct. A
non-recording deck now reliably reads 0, which is what it already read whenever its worker happened
to be clean: the isolated answer.

**Verification:** `pair_knights` reproducer 7/20 flipped → **0/20**; committed 8-job reproducer
~60% → **8/8 identical**.

## The observation

Two cells flip between exactly two play digests across identical runs of an identical binary. The
**score never moves** — 4.9600 and 4.9040 to four decimal places every time. Only the chosen line
changes.

```
minotaur_regression_d5_s2002   52937a4626b089b6 (GT)  <->  cf3f51e7546fa082
minotaur_regression_d5_s3003   8ec3f5ad5d923ca2 (GT)  <->  bddf6e1b6a550719
```

Measured, 5 full-tier runs per arm, same machine, sequential (never concurrent):

| build | full-tier runs failing ≥1 minotaur d5 cell |
|---|---|
| HEAD + the Mirrorwing candidate-card work | **4 / 5** |
| control: `9b54274f` (upstream's minotaur value-leaf adoption, unmodified) | **2 / 5** |

`regression.sh --deck=minotaur` is **stable**: 3/3 pass. The flake needs the full 80-cell pool.

**It is pre-existing.** The control has no local engine changes at all and still flakes. 4/5 vs 2/5
is not a real difference at n=5 (Fisher p ≈ 0.5), so there is no evidence the Mirrorwing work
changes the rate.

## Why it mattered more than the failing cells

Any A/B touching minotaur d5 read this as signal — and one did (see the cross-machine note below).
The score being stable was the small mercy: the *metric* GT compares (`avg`) was unaffected, so
measurements that only read `avg` were safe. It was the **play digest** that was unreliable.

## THE REPRODUCER — 8 jobs, ~60% hit rate, ~90 s per run

```bash
./build/Release/mtg --batch logs/soloflake/p_C.json --threads 32 2>/dev/null \
  | grep minotaur_regression_d5_s2002 | grep -o "digest=[0-9a-f]*"
#   52937a4626b089b6 == GT (correct)     cf3f51e7546fa082 == the poisoned value
```

Eight jobs, all **d5**: the target plus `minotaur_d5_s3003`, `knights_d5_s2002/s3003`,
`hinata_d5_s2002/s3003`, `kitty_d5_s2002/s3003`. Run it 5x; expect ~3/5 wrong. (Regenerate the
manifest from `test/logs/regression/manifest.json` if it is missing — see `logs/soloflake/*.sh`.)

## Isolation ladder — it is NOT pool size, it is d5 NEIGHBOURS

The cell is perfectly deterministic alone. Contamination depends on *which* jobs share the process,
not how many. Every row is 5 runs unless noted:

| pool | jobs | result |
|---|---|---|
| this cell alone, 32 threads | 1 | **stable, = GT** |
| + `slivers_regression_d0_s2002` | 2 | stable |
| target + every **d0** cell | 17 | stable |
| target + every **d3** cell | 33 | **stable** — bigger than pools that DO flake |
| halfA: antilife, auras, burn, creature, dragons, dragonstorm, fivecolour, goblins | 17 | stable |
| quadD: mirrorwing, slivers, stompy, th | 9 | stable |
| every **d5** cell | 32 | **FLAKY 2/5** |
| halfB: hinata, kitty, knights, minotaur, mirrorwing, slivers, stompy, th | 15 | **FLAKY 4/5** |
| **quadC: hinata, kitty, knights, minotaur** | **8** | **FLAKY 3/5** |
| the full tier | 80 | **FLAKY 40–80%** (15 runs) |

Two conclusions:

* **Not pool size.** The 33-job d3 pool and 17-job halfA stay clean while 8 jobs of quadC flake.
* **d5-specific.** d5 cells are the ones that omit an explicit depth, so `value_play` owns the depth
  and **value-leaf sidecars are live**. Upstream adopted minotaur's sidecar (trust d5) in `9b54274f`
  immediately before this appeared. hinata / kitty / knights / minotaur is the poisoning set;
  mirrorwing / slivers / stompy / th is not.

It is cross-**job** contamination inside one `mtg --batch` process, not intra-job threading: 32
threads on a single job is stable, which rules out a race among a job's own games.

**Next bisection step:** split quadC's non-target jobs `{knights×2, hinata×2, minotaur_s3003,
kitty×2}` in half and repeat. Note `--deck=minotaur` (which contains all three minotaur d5 cells)
is stable 3/3, which argues against same-deck cross-seed contamination — so expect the culprit to be
a foreign deck.

### REFUTED: `ProfileCache` eviction

`ProfileCacheCap()` defaults to 3 and the batch runner shares one cache across every job, so
eviction was the first suspect. It is **not** the cause — with eviction switched off entirely the
flake persists at the same rate:

| arm (5 full-tier runs each) | failing |
|---|---|
| default cap 3 | 4 / 5 |
| **cap 64 — no eviction** | **3 / 5** |
| control `9b54274f`, default cap | 2 / 5 |

Note that raising the cap stops eviction but leaves the cache **shared**, and if anything makes
sharing *more* persistent. Shared-mutable-state-in-the-cache is therefore not excluded; only the
eviction mechanism is.

## ELIMINATED by inspection — do not re-derive these

Every known nondeterminism source in the engine is accounted for. Each looked plausible; each is out:

| candidate | why it is out |
|---|---|
| memo key collision across decks (`solvememo` / `enummemo`) | both are `thread_local` and keyed on `g_decision_epoch`, which is **only ever incremented** (3 `++` sites, no assignment or reset). An earlier game's entries on a reused thread carry a strictly lower epoch and can never be hit |
| wall-clock search deadline | `SearchBudget` is **virtual** — it counts rollout turn-steps via `NODES_PER_VIRTUAL_MS`, not time. It exists precisely to kill this class |
| deck-numbering leak (`decknumbering::t_map`) | would change the shuffle and therefore the SCORE; scores are stable to 4 dp in every observation |
| `GameWorkMeter` / `CellCeiling` calibration | only armed when a job sets `abandon_k`/`abandon_calib`. The regression manifest sets neither, and the "[batch] relative per-game ceiling armed" banner is absent from the logs |
| `ProfileCache` eviction | refuted empirically: `MTG_BATCH_PROFILE_CACHE=64` (no eviction) still fails 3/5. Note this leaves the cache SHARED, so shared-mutable-state in it is *not* excluded — only eviction is |

## What the narrowing actually took (the useful part)

Bisecting the POOL stalled at 8 jobs. What broke it open was bisecting the GAME instead:

1. **`--game-log-dir` gives per-game win turn + digest.** Six runs of the 8-job pool, diffed
   per game index: exactly ONE unstable game per cell (`s2002` gi 40, `s3003` gi 19), same win
   turn, two digests. No other deck in the pool had a single unstable game.
2. **`--game-trace-dir` gives the full decision stream.** Diffing clean vs poisoned traces put the
   divergence at turn 2 main 1: cast Slaughter-Priest `{B}{R}` vs Deathbellow Raider `{1}{R}`,
   same seed, same opening hand, same mulligan, same win turn.
3. **`MTG_DUMP_UNITS` gives per-game search work.** 147,177 (clean) vs 118,061 (poisoned), *always
   exactly those two* — a binary flip of one decision, not accumulated noise. That killed every
   "cache hit rate / budget drift" theory.
4. **Conditions, each measured at n=20** (n=5 is worthless here — see the method warning):
   value model ON required (`MTG_VALUE_MODEL=0` → 0/20); a FOREIGN deck required (500 extra
   *Minotaur* games → 0/20, 500 *Knights* games → 7/20); genuine concurrency required
   (`--threads 1` clean in every order, including knights-immediately-before and
   mino→knights→target); sibling games required (ONE minotaur game among 500 knights → 0/10).
5. **A temporary env-gated dump** (`MTG_HYB_TRACE`) of each hybrid decision's per-pass ladder costs
   named it in one run. That is the step to reach for sooner: no amount of pool bisection was going
   to print `g_probe_leaves`.

## ELIMINATED by measurement — do not re-derive

| candidate | how it was ruled out |
|---|---|
| bp-enum cache (`EnumerateBreakpointPlans`, thread_local + no epoch check) | `MTG_NO_BP_ENUM_CACHE=1` still flakes **8/20**. Looks exactly like the bug (never cleared, no epoch) and is not it |
| the `g_mana_cache` payment memo (thread_local, cleared only at 500k) | `MTG_MANA_CACHE=0` still flakes **6/20** — despite the divergence BEING a mana payment |
| solve-memo / enum-memo | each disabled separately, still flakes |
| pool allocator | `MTG_POOL_ALLOC=0` still flakes |
| profile `value_trust_depth` | pinned with `MTG_VALUE_MIN_DEPTH=5`, still flakes 6/20 |
| budget knife-edge | units identical for `budget_ms` 18→30 |
| `ProfileCache` eviction | the 3-job pair holds 2 distinct profiles at cap 3 — cannot evict — and flakes 35% |
| a DATA RACE | ThreadSanitizer on the poisoning configuration reports exactly ONE race: `CardDatabase::LookupCached` lazily filling `m_def` on the shared `def.card` prototype. Real UB, but it writes the same value every time, so it cannot change a result |
| uninitialised / out-of-bounds memory | valgrind memcheck (`--track-origins=yes`) on the isolated game: **zero errors** |

## Leading hypothesis, not confirmed (SUPERSEDED — kept for the method note)

**`thread_local` state surviving a job boundary.** The batch runner reuses a worker thread across
consecutive jobs, and those jobs are *different decks*. `TurnSolver.cpp` carries a large amount of
`thread_local` state (`g_decision_epoch`, `g_cantrip_order_site`, `g_bp_hand_before`,
`g_bp_plan_casts`, memo tables, scratch vectors, …). Anything semantic that is not reset when a
thread moves from deck A to deck B becomes a channel between them, and *which* thread picks up which
game varies run to run — giving exactly this signature: stable alone, stable in a small pool, flaky
in a big heterogeneous one, at a rate that depends on how much foreign work shares the process.

The `enum-memo` / `solve-memo` tables are the specific things to check first (`MTG_ENUM_MEMO`,
`MTG_SOLVE_MEMO`, plus their `_VERIFY` variants, which look purpose-built for this). If a memo key
does not fully capture deck identity, an entry created while running deck A can be hit while running
deck B. That is the same class of defect as the canonical-memo-key order-invariance work.

One note for whoever picks this up: `test/regression.sh`'s manifest emitter is deterministic — job
order is fixed by the case list, so manifest ordering is *not* the source. Its comment "Ordering is
lossless — results are unchanged" is the claim this defect contradicts, but the ordering it refers
to is not what varies.

## Method warning — THREE wrong root causes were published before the right one

All three came from **concluding on a single run of a flaky thing**, or from a plausible-looking
code reading that was never measured:

1. An `OnGoblinEnters` projection line was blamed, on a probe showing filtered-pass / full-fail. The
   filtered tier passes with *or* without that line; the probe never isolated the code.
2. `ProfileCache` eviction was then declared the root cause on the strength of **one** clean run at a
   raised cap — which, at a ~20–60% pass rate, is barely evidence at all.
3. On a second machine, the same flake was attributed to commit `6b3ae5c2` after reproducing the
   cell in an ISOLATED 2-job batch (retracted in `a22cf7d0`). The isolation ladder above shows why
   that can never work: **the cell is perfectly deterministic on its own**, so an isolated repro
   agreeing with itself carries no information about a pooled disagreement.

Two rules that would have saved all of it:

* **Treat any single run as one sample of a distribution.** Every arm in this doc that decides
  something is n=20; the n=3–5 arms are labelled and were not trusted. Two candidates (bp-enum
  cache, mana cache) survived n=5 and died at n=20.
* **A code reading is a hypothesis, not a finding.** The bp-enum cache is `thread_local`, never
  cleared, and has no epoch check — it *looks* exactly like this bug. It is not this bug. Only the
  `MTG_HYB_TRACE` dump, which printed the actual offending value, settled it.

## Cross-machine note

The overnight GT accepted in `a22cf7d0` was taken **while this bug was live**, and its own commit
message says the minotaur cell it moved was "a coin flip". Post-fix that cell is deterministic; if
it disagrees with the accepted face, the accepted face was the contaminated one and the key should
be re-derived — that is a legitimate rebaseline, justified by this fix, not a regression.

## Related

* `docs/design/etb-cascade-projection-gap.md` — a separate, latent projection gap found alongside
  this. Its fix is blocked on this defect, because a genuine GT movement cannot currently be
  distinguished from the flake.
* The earlier, still-unexplained batch-pool contamination episode (closed as irreproducible). Same
  smell — a pooled-batch result that would not reproduce. This one reproduces in ~2 minutes and is
  the better handle.
