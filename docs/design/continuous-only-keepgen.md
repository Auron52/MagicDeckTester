# Continuous-only keep-gen — delete the wave/uniform paths and their knobs

**Status:** **COMPLETE 2026-08-09** on branch `phase-1-2-deck-analyzer`. The 2026-08-08 work landed the
footgun removal + `recommend` port + wave-path delete + auto-commit-stamp + rolling-vg; the final piece —
the `sub_floor` sub-table fusion (**delete `run_refine_waves`**) — landed 2026-08-09 as the producer-driven
`fuse_subfloor` path (see "Implementation status" below). There is now **one execution path, zero
execution knobs**, and `run_refine_waves` is gone.
**Goal:** make the *continuous single-queue* keep-gen (`docs/design/adaptive-batched-keepgen.md`,
SHIPPED 2026-07-24) the **only** execution path, and **delete** the toggle + the now-dead knobs that
select or tune the old paths. An agent reading the analyzer should not be able to run gen the wrong
way, and should not have to know a single execution knob to run it right.

## Implementation status (2026-08-08)

**DONE** (in `src/analyzer/ExhaustiveKeep.cpp` + `main.cpp`):
- **`continuous = adaptive` unconditionally** — the execution-path toggle is gone. Journal is the sole
  persistence (`journal_on = continuous && !out_raw.empty()`); the snapshot-checkpoint machinery
  (`maybe_checkpoint`, the inner `checkpoint()` lambda, `t_last_ck`, the `!journal_on` snapshot block)
  is **deleted**.
- **8 execution flags RETIRED:** `MTG_KEEP_CONTINUOUS`, `MTG_KEEP_JOURNAL`, `MTG_KEEP_CHECKPOINT_SEC`,
  `MTG_KEEP_FLOOR_GROUPS`, `MTG_KEEP_SWEEP_SEC`, `MTG_KEEP_CONTINUOUS_LOOKAHEAD` (baked = 4),
  `MTG_KEEP_NO_FLOOR_SPEC` (floor-spec always on except `recommend`), `MTG_KEEP_NO_SUB_FUSE`
  (fusion mandatory where it applies).
- **`recommend` ported to the continuous floor phase** — `floor_report_maybe_stop` fires at
  floor-complete (projection + degenerate-cell diagnostic + R=1 probe write + early stop); the old
  uniform-R scout path is deleted. `recommend` disables floor speculation so the probe is exactly r0.
- **Wave whole-table path deleted** — `run_refine_waves(false)` call site and the dead
  `adaptive && !continuous` projection/recommend block are gone.
- **Commit auto-stamped** from `git rev-parse --short HEAD` (+`+dirty` on an unclean tree) in the recipe
  block, so the sidecar's pooling identity is recorded without `MTG_COMMIT`.
- **Rolling-vg** (`MTG_KEEP_REFS_OFFSET`, default 2; see the section below).

**DONE (2026-08-09)** — the `sub_floor` sub-table fusion. In the code
`sub_floor = (adaptive && (!bottoming_enabled || adaptive_bottom)) || change_detect`, so `fuse_sub`
(`continuous && !sub_floor`) was already TRUE for **`complete`** (fused). The case still on the
`run_refine_waves(true)` pre-pool barrier was **`fast`/keep-only/change-detect** (`sub_floor == true`).
That adaptive sub-refine now runs producer-driven INSIDE the continuous pool (`fuse_subfloor`),
concurrent with the size-7 floor, fed as **kind-2** tasks `{2, w, pd, have, target}` (the pool task
array widened 4→5). The producer advances one wave per `sub_wave_pending==0`, via a shared
`compute_sub_wave_tasks` (byte-identical to the old barrier's sub_only marking, minus the size-7 vg[0]
+ m=0 gate it never used), a sub-only `recompute_sub`/`apply_prior_override_sub`, and the floor-complete
gate now also waits on `sub_converged`. `run_refine_waves`, the barrier call, and the temp
`MTG_KEEP_FUSE_SUBFLOOR` A/B toggle are **deleted**. Validated on Slivers (`adaptive_bottom`, offset=0,
fixed seed): barrier-OFF == fused-ON == committed-`e164ac0` cell-data byte-identical, and a
kill-and-resume mid-refine reproduces the uninterrupted raw. So: continuous is a total superset; there
is no non-continuous path left.

## Rolling-vg (freeze shrink target re-derived from a completed level)

The size-7 freeze test shrinks each cell's win-turn variance toward a pooled target `vg`. The wave path
recomputed `vg` every wave (from the current accumulators); the continuous path originally **pinned**
`vg` at the noisy r0 floor for the whole refine — the source of the (benign, play-neutral) continuous↔wave
divergence. Rolling-vg re-derives `vg` from the **current** accumulators as the *completed level*
(min committed cnt among still-live cells) advances `MTG_KEEP_REFS_OFFSET` past the last recompute, so
freeze decisions past the first few R use a `vg` from the refined state — exactly the wave's own formula,
lagged a safe offset behind the frontier so the reference level is always a completed wave.
- `MTG_KEEP_REFS_OFFSET` default **2**; **`0` = the old floor-pinned path by construction**
  (`recompute_vg` no-ops), i.e. byte-identical to the pre-rolling continuous behavior.
- `Dopt_ref` does **not** roll — it reads only sub-tables, which are final before the size-7 refine.
- **Not byte-identical run-to-run at offset > 0** (the recompute is timing-triggered on the completed
  level); the user accepted "a little variance" for this, bounded by the conservative offset. The
  **floor `vg` remains the journal/resume anchor** (`vg_roll` re-derives from the reloaded accumulators
  after a resume), so resume determinism is unaffected.

## Why

The one-flag recipe (`--gen-mulligan fast|complete`) never sets `MTG_KEEP_CONTINUOUS`, so
`continuous = adaptive && EnvOn("MTG_KEEP_CONTINUOUS")` (ExhaustiveKeep.cpp:1835) is **false** and the
recipe silently runs the **wave** path. That path:

- **Barriers** — each refine wave is one `process_tasks` over all marked cells, then a serial
  `recompute()` + `maybe_checkpoint()` at the boundary (ExhaustiveKeep.cpp:2327-2331). Wave 1 marks
  *every* borderline size-7 cell for a single `r_batch` (=16) bump (floor 2→18): on Goblins that was
  **364,254 cell-sides × 16 = 5.83M rollouts ≈ 44 h in one uninterruptible chunk**.
- **Checkpoints only at wave boundaries** — `process_tasks` for a wave is called WITHOUT the
  checkpoint callback the floor pass uses (contrast ExhaustiveKeep.cpp:2162 vs 2327), so a ~44 h wave
  writes **zero** checkpoints. A crash mid-wave loses the whole wave (falls back to the floor
  snapshot). This is the exact failure the continuous path was built to prevent.
- **Long tails** — the barrier strands cores at each wave/floor boundary; continuous is barrier-free
  ("cores go idle only at the very END", per the shipped design doc).

The shipped design doc already states the intent: *"the uniform/round/wave code paths are to be
removed"* and *"agents have repeatedly mis-invoked it (uniform R=1 instead of continuous)."* This doc
is the plan to finish that removal so the footgun is gone, not merely defaulted-off.

## End state: one command, zero execution knobs

The default path is **one command and nothing else**:

```
./build/Release/mtg-analyze decks/<name>/<name>.cod --cards-json src/cards/data/cards.json --gen-mulligan fast
```

**The recipe stays a toggle — that is the one intended choice.** `--gen-mulligan fast|complete|recommend`
is preserved exactly as it is today: it selects R + bottoming mode (and `recommend` = the R=1 scout).
That is the *product* toggle, not an execution-path knob, and it is the whole agent-facing interface.

What is removed is the *execution-path* choice — continuous vs wave vs uniform. No `MTG_*` is required or
consulted to decide *how* a recipe runs. Continuous is unconditional; persistence is the per-cell
journal, always. `continuous`/`journal_on` stop being booleans threaded through the code — the
wave/uniform branches are deleted and their bodies inlined as the sole path. So: **three recipes, one
execution path, zero execution knobs.** Env can still
*describe the work* (which buckets, how much precision, where to write) but the **default solo run sets
none of it** — every such flag has a baked default, and multi-machine pooling is the only reason to ever
set one (see "advanced pooling" below). An agent that knows only the command above cannot run gen wrong.

## Restart — the only special case, and it also needs zero settings

Regeneration has exactly one special case: **resume after an interruption** (crash, reboot, manual
stop). It must require **no flag and no remembered value** — you re-run the identical recipe command and
it continues:

- **Flag-free resume is already the mechanism, keep it and lean on it.** The recipe fixes
  `seed = 1000000` by default (main.cpp:226), so a re-run reproduces the same discovery/bucketing/cell
  indices; the per-cell **journal** (`<out_raw>.journal`) is detected on startup and replayed
  (1913-1919), skipping completed cells. So "resume" = "run the same command again." Nothing to pass.
  This must stay true after the continuous-only migration — the journal is the sole persistence, so it
  is *also* the sole resume source; there is no `MTG_KEEP_*` restart lever to get wrong.
- **Auto-stamp the commit — drop `MTG_COMMIT` from the normal path.** Today the recipe leaves the
  sidecar's `commit` empty unless `MTG_COMMIT=<hash>` is set by hand (the Goblins run has `commit: ""`),
  which is exactly the kind of easily-forgotten setting to eliminate. The recipe should stamp
  `commit` from the repo HEAD itself (`git rev-parse HEAD`, and mark it dirty/append `+dirty` if the
  work-tree isn't clean, since a dirty tree makes the fingerprint unreliable). Then the pooling/attribution
  identity is recorded automatically and `MTG_COMMIT` is never needed for a normal run.
- **No resume driver, no wrapper.** Because the command *is* the resume path, there is deliberately no
  separate resumable-driver script and no restart flag — a `run.sh` that just re-execs the recipe is the
  most a caller ever needs, and even that is optional.

### DELETE — execution-path selectors + old-path tuning (the footguns)

| Flag | Where | Why it goes |
|------|-------|-------------|
| `MTG_KEEP_CONTINUOUS` | 1835 | Continuous is the only path — no toggle. |
| `MTG_KEEP_JOURNAL` | 1474,1841,1843 | Journal is the only persistence; its "off" branch (periodic full-raw snapshot) is deleted, so the toggle has nothing to select. |
| `MTG_KEEP_CHECKPOINT_SEC` | 1814-1827 | The periodic full-raw *snapshot* checkpoint is the wave/floor persistence; replaced entirely by the journal. |
| `MTG_KEEP_FLOOR_GROUPS` | 1828 | The 32-group floor *barrier* granularity; continuous floor is barrier-free. |
| `MTG_KEEP_NO_SUB_FUSE` | 2095,2097 | Sub-table fusion into the continuous pool becomes mandatory (see dependency below), so the "don't fuse" escape (a wave fallback) is dead. |
| `MTG_KEEP_NO_FLOOR_SPEC` | 2365-2366 | Floor-speculation opt-out is a wave-path fallback. |
| `MTG_KEEP_SWEEP_SEC` | 1834,1838 | Continuous Dopt/freeze sweep throttle → bake the default as a named constant (internal tuning, not a run-shaping lever). |
| `MTG_KEEP_CONTINUOUS_LOOKAHEAD` | 1833,1836 | Continuous refine-speculation bound → bake as a constant. |

Also delete the code they gate: `run_refine_waves` (2255-2333), the wave call site (2810) and its
`maybe_checkpoint`/snapshot machinery (`write_raw_atomic`-as-checkpoint, `t_last_ck`,
`maybe_checkpoint` at 1888-1896 & 2331), the 32-group floor loop, and the uniform-R (`r_floor>=rollouts`)
special-cases (1383,1795) once nothing selects them. Per the repo flag convention
(`.claude/skills/coding-conventions.md`) each removal deletes an `EnvOn(...)`/`getenv(...)` read — no
presence-only leftovers.

### KEEP — these describe the WORK, not the path — and the default run sets NONE of them

Every flag below has a baked default; a normal solo `--gen-mulligan` run touches zero of them. The only
scenario that sets any is **advanced multi-machine pooling** (disjoint seeds + pinned bucketing across
boxes), which is opt-in and lives outside the default path. Grouped:

- **Bucketing / pooling identity (MUST keep — `bucket_fp` and cross-machine parity depend on them):**
  `MTG_EQUIV_PROBES`, `MTG_EQUIV_THRESHOLD`, `MTG_EQUIV_DEPTH`, `MTG_EQUIV_BUDGET`, `MTG_EQUIV_SEED`,
  `MTG_EQUIV_CACHE`, `MTG_EQUIV_DISCOVER`, `MTG_EQUIV_FORCE_MERGE`.
- **Rollout precision / adaptive schedule (recipe-driven; keep as overrides):** `MTG_KEEP_ROLLOUTS`
  (cap R), `MTG_KEEP_R_FLOOR`, `MTG_KEEP_R_BATCH`, `MTG_KEEP_FLIP_EPS`, `MTG_KEEP_SE_PRIOR`,
  `MTG_KEEP_MAXMULL`, `MTG_KEEP_ADAPTIVE_BOTTOM` (bottoming mode = the fast/complete product knob).
- **IO / pooling plumbing:** `MTG_KEEP_OUT_PROFILE`, `MTG_KEEP_OUT_RAW`, `MTG_KEEP_NO_WRITE`,
  `MTG_KEEP_MERGE`, `MTG_KEEP_PROBE_CARRY`, `MTG_COMMIT`.
- **Change-detection carry — kept as a FEATURE, but its flags are deleted/automated.** Today it is
  driven by env (`MTG_KEEP_PRIOR_RAW`, `MTG_KEEP_CHANGED_CARDS`, `MTG_KEEP_CARRY_MODE`,
  `MTG_KEEP_DETECT_DELTA`, `MTG_KEEP_PRUNE_SET`, `MTG_KEEP_CUTOFF_*`). Under this design the analyzer
  auto-detects the prior sidecar sitting next to the deck and auto-diffs `cards.json` to derive the
  changed-card set — **no flags**. See the phase-1-3 open item below (this feature has to prove it earns
  its keep, and may need a finer instrument).
- **Diagnostics / offline test harness (not the real gen path):** `MTG_KEEP_TRACE`, `MTG_KEEP_SLOW_MS`,
  `MTG_KEEP_REPLAY*`, and the whole `MTG_KEEP_SIM_*` / `MTG_KEEP_SYNTH_*` simulation harness — but audit
  these for references to `run_refine_waves`/wave semantics and repoint them at the continuous path.

The net: the ~8 path/tuning footguns above disappear; what remains is only "which buckets"
(`MTG_EQUIV_*`) and "how much precision / IO" — none of which change *how* the run is scheduled.

## Phasing (agreed order of work)

**Phase 1 makes continuous the ONLY path for EVERY mode — no non-incremental, non-continuous option
survives anywhere.** That means all three recipes route through the single continuous queue:
- `fast` (adaptive bottoming) — continuous already fully covers it (size-7 floor+refine + sub-table
  fusion). No engine work; just delete the wave selection.
- `complete` (bottoming-full) — requires the one real piece of engine work in Phase 1: generalize the
  continuous `fuse_sub` route to the `sub_floor` case so continuous owns bottoming-full sub-table
  refine (see "The `sub_floor` work" below). This is *inside* Phase 1, not a later phase — leaving
  `complete` on the wave path would leave a non-continuous option, which is exactly what we're removing.
- `recommend` / R=1 scout — route its bounded "one rollout per cell, then project and stop" through the
  continuous floor phase (early-stop), and delete the uniform-R scout path. No uniform code remains.

Then delete, in one sweep: `run_refine_waves`, the wave/round/uniform-R (`r_floor>=rollouts`) branches,
the 32-group floor barrier, the snapshot-checkpoint machinery, and all the execution-path flags. After
Phase 1 there is exactly one code path and zero execution knobs.

**Later — carry under card edits.** The change-detection-carry evolution is deferred and lives in its own
doc (`carry-under-card-edits.md`); nothing in Phase 1 depends on it, and carry itself is preserved
(flagless) throughout.

## The `sub_floor` work (the one engineering task inside Phase 1)

Continuous is **not yet** a total superset today: the `sub_floor` case (bottoming-on, full — not
adaptive_bottom, no change-detect) still Pass-A-floors the sub-tables and then calls
`run_refine_waves(true)` (ExhaustiveKeep.cpp:2094, 2344-2351). So `complete`'s sub-table refinement
currently rides the wave path even under continuous. Phase 1 must make the continuous pool own the
sub-table floor+refine for bottoming-full too, i.e. generalize the `fuse_sub` route (2091-2106) to the
`sub_floor` case. Order within Phase 1:
1. Extend the continuous pool to subsume `sub_floor` sub-table refinement (removing the
   `run_refine_waves(true)` usage), landed behind the continuous path so it can be A/B-validated
   byte-identical against the current `complete` output first.
2. Only after that validates, delete `run_refine_waves` and every wave/uniform call site.

## Persistence after removal

Journal-only (`<out_raw>.journal`, per-cell append on freeze/floor/cap + one REFS record), which already
exists and is the continuous default. Resume prefers the journal (1913-1919); the "out_raw floor
snapshot" resume path (1983-2033) can stay as a *read-only* backward-compat loader for pre-migration
sidecars but is no longer *written*. The final authoritative raw is still written once at the end and the
journal deleted (unchanged).

## Determinism & parity guarantees to preserve

- **Byte-identical values.** The shipped doc claims continuous is value-byte-identical to the wave path
  (fixed floor-derived refs; each cell freezes on a pure function of its own rollouts). The migration MUST
  preserve this — it's what lets in-flight and historical profiles stay comparable.
- **`bucket_fp` untouched.** We delete no `MTG_EQUIV_*` and change no bucketing, so cross-machine pooling
  parity holds. Note the **live Hinata2 pooling campaign on the secondary box** (see
  `hinata-gen-perf-campaign`): its chunks must remain poolable across the switch — the byte-identical
  check below is the gate.

## Validation plan (before deleting anything)

1. On a **tiny deck** (small K, e.g. Slivers-scale), run gen twice at identical params: once wave
   (`MTG_KEEP_CONTINUOUS=0`), once continuous (=1). Assert the produced `.raw`/profile are **value
   byte-identical** (the doc's core claim). Do this for both `fast` (adaptive_bottom) and `complete`
   (bottoming-full, i.e. the `sub_floor` path) once step-1 of the dependency lands.
2. Kill-and-resume test on the continuous run mid-refine; assert the resumed profile == the
   uninterrupted one (journal resume correctness — the whole point).
3. Only after 1–2 pass on both recipes: delete the wave code + flags, rebuild, re-run the tiny-deck gen,
   and confirm the profile is unchanged from step 1's continuous output.

## Migration / rollout notes

- **In-flight runs.** The current Goblins gen and any live campaign are on the wave binary; they finish
  on it. The change ships as a new commit for *future* runs — it does not touch a running binary (and a
  crash-resume must use the same binary it started with, so never hot-swap `build/Release` under a live
  gen).
- **Old sidecars.** Keep the snapshot *reader* for one release so pre-migration `.raw` checkpoints still
  resume; stop *writing* snapshots immediately.
- **Docs.** Fold this into `adaptive-batched-keepgen.md` as "removal complete" once done, and update
  `.claude/skills/mulligan-profile.md` (drop every `MTG_KEEP_CONTINUOUS`/wave mention; the recipe is the
  only interface).

## Deferred: carry under card edits (separate topic, separate doc)

Change-detection carry stays and becomes flagless (above), but *how it should evolve* for phase-1-3 card
iteration — the (A) composition / (B) draw-quality / (C) K-remap taxonomy, merge-aggregate vs
split-mint, and the game-level execution-trace instrument — is its own forward design and lives in
**`docs/design/carry-under-card-edits.md`** (with `docs/design/change-detection-carry.md` for the shipped
mechanism). It is explicitly **after** this flag-removal work; nothing here depends on it.

## One-line summary for the recipe after this lands

`--gen-mulligan fast|complete` → continuous single queue, journal persistence, **zero execution knobs**.
Run it = start; run it again = resume (journal-detected, commit auto-stamped). The default path is one
command with nothing to set. The only reason to ever pass an env is opt-in multi-machine pooling — never
to shape a normal run, and never to restart one.
