# Keep-gen: continuous, always-reporting, always-incremental — with no off switches

**Status:** **IMPLEMENTED + VALIDATED 2026-08-10** (branch `phase-1-2-deck-analyzer`, uncommitted).
Follow-on to
[continuous-only-keepgen.md](continuous-only-keepgen.md), which removed the *execution-path*
toggles but left three properties still defeatable.

**Goal (user-directed):** mulligan table generation must be **fully continuous/batched (no tail
until the very end)**, must **report slow games in every situation**, and must be **incremental**
(any interruption resumes, nothing is repaid) — and there must be **no option that turns any of
those three off**.

## Review findings (what was actually defeatable)

### Continuous / batched

| # | Gap | Where |
|---|-----|-------|
| C1 | **Uniform (non-continuous) path still reachable and is the env route's DEFAULT.** `r0 = (r_floor>0 && r_floor<rollouts) ? r_floor : r_max`, `adaptive = r0 < r_max`, `continuous = adaptive`. `MTG_KEEP_R_FLOOR` defaults to **0** ⇒ `MTG_KEEP_EXHAUSTIVE=1` without it runs uniform: no pool, no journal, no floor report, no adaptive freeze. | `ExhaustiveKeep.cpp:1811-1812,1828`; `main.cpp:160` |
| C2 | **Pass A is a joined barrier for 2 of 3 recipes.** Only `complete` (`fuse_sub`) hands sub-table work to the pool; `fast`, `recommend` and change-detect runs `process_tasks(...)` + join before any size-7 rollout. | `ExhaustiveKeep.cpp:2082-2096, 2098-2157` |
| C3 | **Two silent serial heads.** Equivalence discovery parallelises over *probes only* (one item = N candidate rollouts ⇒ tail of a whole probe); `RolloutConfigDigest` runs **64 games single-threaded** on one core, on every invocation *including every resume*. | `EquivalenceDiscovery.cpp:69-93`; `ExhaustiveKeep.cpp:354-380` |

### Slow-game reporting

| # | Gap | Where |
|---|-----|-------|
| S1 | **The top-12 table is printed at exactly ONE site** — `floor_report_maybe_stop`, continuous-only, at floor-complete. No end-of-run dump, no periodic dump, nothing on the uniform path, nothing persisted. Documented casualty: the Creature Giving scout stopped at 24% and *"no slow-cell list was captured"* (`analysis-Creature Giving.md`). | `ExhaustiveKeep.cpp:2365-2377` |
| S2 | **`MTG_KEEP_SLOW_MS=0` disables** the live stream; `MTG_SLOW_GAME_MS=0` does the same for the goldfish runner. | `ExhaustiveKeep.cpp:1422`; `GoldFishRunner.cpp:313-320` |
| S3 | **Discovery rollouts and the 64-game digest battery are never timed** — a degenerate card in either phase is invisible. | `EquivalenceDiscovery.cpp`; `ExhaustiveKeep.cpp:354-380` |

### Incremental

| # | Gap | Where |
|---|-----|-------|
| I1 | `journal_on = continuous && !out_raw.empty()` ⇒ uniform runs and `MTG_KEEP_NO_WRITE=1` runs have **zero persistence**. | `ExhaustiveKeep.cpp:1843`; `main.cpp:195-205` |
| I2 | **Discovery is not cached by default** (`MTG_EQUIV_CACHE` opt-in) and the play digest is never cached ⇒ every resume repays both. | `ExhaustiveKeep.cpp:395-467, 1402-1407` |
| I3 | Probe carry is **off by default on the env route** and has an explicit opt-out (`MTG_KEEP_NO_PROBE_CARRY`). | `main.cpp:190`; `ExhaustiveKeep.cpp:2035-2036` |
| I4 | An interrupted run is resumable but **not poolable**: the raw is only written at the end, so a killed chunk's work cannot be merged or shipped. | `ExhaustiveKeep.cpp:2851` |

## Bugs this review surfaced (found by running the check in the surviving configuration)

The generator's own regression check (`test/lib/keepgen_check.sh`) ran its `gen` cases **uniform** — the
path being deleted. Re-pointing it at the adaptive/continuous path (which is what every real run uses)
immediately failed, on the *committed* binary:

1. **Probe carry double-counted `r=0` in every sub-table cell.** `feed_sub` enqueued fused sub-table
   batches as `{1, w, pd, 0, st.r1}` — hard-coding the start index to 0 and discarding `st.r0`. For a
   fresh run `st.r0 == 0`, so it was invisible; after a `recommend` probe (or any partial carry) `st.r0`
   is 1, so the pool re-rolled `r=0` on top of the carried one. Measured on burn: every sub-cell had
   `count 5` where a fresh run had `4`, with one sample duplicated into `sum`. This corrupts the
   sub-tables — i.e. bottoming and the Dopt thresholds — on exactly the flagship path
   (`recommend` scout → `--gen-mulligan complete`), and silently broke the "byte-identical to a
   from-scratch run" guarantee that is the entire justification for probe carry. **Fixed** (pass `st.r0`).
2. **The generator is not run-to-run deterministic at a fixed seed.** Rolling-vg
   (`MTG_KEEP_REFS_OFFSET`, default 2) re-derives the freeze shrink target on a *timing*-triggered
   schedule, so a cell sitting on the freeze threshold stops at a different R between two runs of the
   same binary and seed. Measured on burn R=10: one size-7 cell of 330 flipped between `count 7` and
   `count 10`, ~1 run in 3. This is **accepted behaviour** (`continuous-only-keepgen.md` records the
   user accepting "a little variance" for a more accurate freeze), and is NOT changed here — but it was
   silently defeating two things that assume determinism, both now pinned to `MTG_KEEP_REFS_OFFSET=0`:
   the byte-exact generator check, and the **cross-machine determinism handshake** the mulligan skill
   mandates before trusting a pool.

## Pre-existing dead carry features (found, NOT changed here)

Both were already inert once continuous became the only path, because Pass A stopped enumerating size-7
cells and the pool never picked them up. Neither is a regression from this work; each is a behaviour
change needing its own measurement, so they are recorded rather than quietly switched on:

- `prune.carry_lowfloor7` (the "verify mode" reduced floor for carried confident-mull size-7 cells).
- `pc.reuse_all_cells` for size-7 (the whole-pool reuse when `play_digest` matches exactly — the case
  that is supposed to make a re-gen on identical play nearly free).

## Decisions (user, 2026-08-10)

1. **Delete the uniform path; reject cap R < 2.** The floor is derived internally; a config that
   cannot be adaptive is an error, not a silent downgrade. This retires the legacy R=1 chunk
   protocol (`scripts/hinata_gen_chunks.sh`, `test/exhaustive_chunked_gen.sh` round mode) —
   superseded by distinct-`--seed` recipe runs pooled with `MTG_KEEP_MERGE`.
2. **Both serial heads are in scope** — re-grain discovery, parallelise the digest battery (folding
   in game order ⇒ byte-identical), instrument and cache both.
3. **The goldfish runner's `MTG_SLOW_GAME_MS=0` gets the same treatment** — no rollout path anywhere
   may run with slow reporting off.

## The changes

### 1. One execution path (continuous), no way to ask for another

- `ExhaustiveKeepConfig::r_floor` becomes **derived, not configured**: `r0 = clamp(r_floor?:2, 1,
  rollouts-1)`. `adaptive`/`continuous` become invariants, not booleans — delete both and the
  `if (continuous)` guard, inline the pool as the sole path.
- **Reject `rollouts < 2`** loudly in `main.cpp` (before any work): a cap of 1 cannot be adaptive,
  and R<10 cannot ship a profile anyway (`kMinProfileR`).
- `MTG_KEEP_R_FLOOR` is **deleted** (it exists only to defeat the pool). `MTG_KEEP_ROLLOUTS` stays:
  it is the cap-R / precision lever, not an execution-path lever.
- Delete the dead uniform special-cases: the `init = r_max` uniform branch and the
  `adaptive ? "floor-pass" : "full-pass"` label.

### 2. No barrier before the pool (Pass A always fuses)

- Pass A **never joins**. Its tasks always become `fused_sub_tasks` (kind-1), for every recipe.
- `sub_refine_step()` additionally gates on `sub_remaining.load() == 0`, so the first adaptive
  sub-wave still sees a *complete* sub-floor (today that is guaranteed by Pass A's join).
- `compute_refs()`'s sub-table recompute becomes unconditional (Pass A no longer recomputes).
- Byte-identity argument is the one already validated for `fuse_sub`/`fuse_subfloor`: `run_batch` is
  a whole-batch, single-thread, self-committing unit whose value is a pure function of
  `(seed_base, r, w, pd)`, so fusing is scheduling-independent.

### 3. No serial heads

- **Discovery** re-grained to per-`(probe, candidate)` work items (`sig[k][i]` writes are already
  independent) ⇒ tail is one rollout, not one probe. Adds a heartbeat + slow capture.
- **`RolloutConfigDigest`** runs its 64 games on the pool and folds the per-game digests **in game
  order** afterwards ⇒ byte-identical fingerprint, 1 core → N cores. Adds slow capture.
- The digest is **deliberately not cached**, only parallelised. It is the load-bearing pooling identity,
  and a correct cache key would have to cover the deck, cards.json, the engine *and* the value sidecar —
  a stale hit would silently mis-attribute a pool. Parallelising already takes it from "a silent
  single-threaded head on every resume" to seconds. It is also computed FIRST now, so it can serve as
  the equivalence cache's key (below).

### 4. Slow reporting everywhere, un-disableable

- `kSlowStreamMs = 30000` baked. `MTG_KEEP_SLOW_MS` survives only as a **lower-only** override
  (clamped to `[1, 30000]`); `0`/absurd values can no longer disable it. Same clamp for
  `MTG_SLOW_GAME_MS` in `GoldFishRunner`.
- The top-N tracker is hoisted into a small shared `SlowTracker` used by **discovery, the digest
  battery, Pass A/fused batches and the pool**.
- Report sites become: floor-complete (existing) **+ every producer heartbeat + unconditionally at
  end-of-run + at the end of discovery and of the digest battery**.
- Every streamed slow rollout is **appended to `<out_raw>.slow.log`** so an interrupted run leaves
  its evidence on disk (this is what the Creature Giving scout needed and did not have).

### 5. Always incremental

- `journal_on = !out_raw.empty()`, and **`out_raw` is never empty on a rollout gen**:
  `MTG_KEEP_NO_WRITE` may suppress the *profile* only.
- **Default bucket cache next to the deck**, no flag: `<stem>.keepmodel.gencache.json`, fingerprint-gated
  exactly as `MTG_EQUIV_CACHE` already was (deck_fp / probes / depth / budget / threshold / equiv_seed /
  max_turns / commit) **plus the play digest**. The digest addition closes a hole the always-on default
  would otherwise have widened: discovery rolls games through the profile, so a changed value sidecar or
  card definition (same HEAD, clean tree) would have produced a stale hit under the commit-only key.
  `MTG_EQUIV_CACHE` still overrides the path.
- **Probe carry always on** — delete `MTG_KEEP_PROBE_CARRY` / `MTG_KEEP_NO_PROBE_CARRY`; it is
  already fingerprint + `play_digest` gated, so a mismatch is ignored rather than misapplied.
- `MTG_KEEP_MERGE` accepts a **`.journal`** input (replays it with the existing loader and emits a
  raw), so an interrupted chunk is poolable/shippable without finishing it.

### 6. Callers and docs that re-open the switches

- `test/exhaustive_chunked_gen.sh` — round mode is a barriered, uniform, non-journalled driver and
  sets four retired flags. **Deleted** (the recipe + journal resume *is* the resumable path).
- `test/lib/keepgen_check.sh` — `gen` runs are uniform and `cont()` sets the retired
  `MTG_KEEP_CONTINUOUS`; updated to adaptive, and the phase-name regex (`full-pass`,
  `refine-wave-<n>`) refreshed. Re-baselined.
- `scripts/hinata_gen_chunks.sh`, `scripts/attic/dragonstorm_keepgen.sh` — R=1 / retired flags.
- `.claude/skills/mulligan-profile.md` — drop the manual uniform recipe, drop the false
  *"`--gen-mulligan` is single-shot — it does not auto-resume"*, drop the chunked-driver section,
  document "run it = start, run it again = resume".
- `docs/design/adaptive-batched-keepgen.md`, `continuous-only-keepgen.md` — mark the follow-on done.

## Verification (all run 2026-08-10)

1. **`test/lib/keepgen_check.sh`, baseline from a scratch worktree of HEAD, both tags pinned to
   `MTG_KEEP_REFS_OFFSET=0`.** Result: **both generator invariants pass** (probe carry byte-identical
   to a fresh r=0 roll — it *failed* on the baseline, see bug 1; killed+resumed raw == uninterrupted
   raw), and **every `.raw.n` and every `.profile` is byte-identical to the committed binary's**, with
   the single exception of `d6carry.raw.n`, which is the probe-carry fix (it now equals `d6plain`).
   So the Pass-A fusion, discovery re-graining, digest parallelisation, derived floor, unconditional
   probe carry and always-on journalling are all byte-neutral to the produced tables, as designed.
   `.report`/`.err` diffs are the new reporting only (reviewed line by line); the play digest itself is
   unchanged (`36d760f77f3cff49` before and after parallelisation).
2. **`test/regression.sh --smoke`: 30 passed, 0 failed**, 0 play-changed, 0 slower/faster, viewer
   protocol 0 drift — confirming the `GoldFishRunner` slow-game clamp is play-neutral.
3. **`--gen-mulligan recommend` on burn:** one continuous phase (`10945 cells ... + 15816 fused
   sub-table batches`), no `floor-pass` join line; projection, slow table and probe chunk all written.
4. **Slow reporting:** `MTG_KEEP_SLOW_MS=0` is clamped (still streams at 30 s); `=1` streams from all
   three rollout phases (`discovery` 40, `keep-rollout` 6298, `play-digest` 64) with the stderr stream
   and `<raw>.slow.log` agreeing exactly (6402/6402 — they disagreed until the stderr write was moved
   back inside the lock; concurrent writers were interleaving mid-line).
5. **Rejection + resume paths:** `MTG_KEEP_ROLLOUTS=1` aborts with the "no uniform path exists" error;
   a re-run hits the bucket cache and resumes from the prior raw/journal; a killed run's `.journal`
   merges as a poolable chunk (and correctly refuses to emit a profile below `kMinProfileR`).
