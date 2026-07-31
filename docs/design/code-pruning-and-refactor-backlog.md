# Code pruning / refactor / adjustment backlog

A survey of MagicDeckTester's source for things worth **pruning, refactoring, or adjusting**,
written for another agent to pick up cold. Every claim below is backed by a measurement or a
`file:line` citation taken on branch `phase-1-2-deck-analyzer` at commit `e97c85a`
(2026-07-30).

## Status (updated 2026-07-30, after verification of the claims)

The Tier A claims below were independently re-verified before acting (multi-config default
really was Debug; the orphans really are referenced by nothing but this document). Done:

- **A1 DONE** (`e3bfb01`) — Debug dropped from `CMAKE_CONFIGURATION_TYPES`,
  `CMAKE_DEFAULT_BUILD_TYPE=Release` backstop, stale `build/Debug` deleted,
  `compile_commands.json` now Release/-O3.
- **A2 DONE** (`a3d5fdd`) — `heuristic_defaults.env` resolved by walking up from
  `/proc/self/exe` (CWD fallback); applied set logged to stderr. Verified from `/tmp`.
- **A4 DONE** (`b57749f` + follow-up) — 12 orphans deleted, the 8 untracked `test/*.sh`
  committed. Step 2 done with user approval: the retired scripts moved to `scripts/attic/`
  (with a README noting docs cite the old `scripts/<name>` path), leaving 13 live tools in
  `scripts/`. The live set was re-derived transitively (skills/src/harness roots plus
  script-to-script references), not taken from this doc's approximate ~11.
- **A7 DONE** (`15b151e`) — `src/ai/EngineFlags.h`, one reader per lockstep flag.
  Smoke 24/24, all digests byte-identical, per-game audit 0 changed configs.
- **A8 DONE** — `build-prof/` deleted (superseded by `./build.sh profile`); the stale
  CMakeLists build-prof comment fixed in `e3bfb01`. `build-asan`/`build-tsan` deleted with
  user approval (binaries were a month stale; the one-line recreate recipe:
  `cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g"`,
  tsan analogously with `-fsanitize=thread -O1`). No `build.sh` sanitizer modes added —
  sanitizer use is too rare to earn a permanent mode.

- **A3 DONE except step 4** (user-approved) — `src/core/EnvFlags.h` (`EnvOn(key, dflt)` /
  `EnvSet` / `EnvInt`); all 126 nullptr-compared truthiness sites and the value-aware
  lambdas migrated. The 5 variables using presence to mean "user pinned a value"
  (`MTG_ESC_BEAM`, `MTG_ESCALATION_FRESH_FRAC`, `MTG_NC_TEMPO`, `MTG_HUMAN_PLAY`,
  `MTG_SHUFFLE_SALT_SEARCH`) use `EnvSet`; `MTG_LOG_HAND` keeps its raw value-carrying
  read. No script in the tree set any `MTG_*=0`, so nothing needed updating. Verified:
  clean-env smoke 24/24 byte-identical; `MTG_LEGACY_SEARCH=0` smoke ALSO 24/24 (the flip
  working — under presence-only semantics that run would have failed); `=1` diverges
  (lever intact). Convention documented in `.claude/skills/coding-conventions.md` +
  CLAUDE.md pointer.
- **A3 step 4 DONE** — flag registry generated at build time from the source literals
  (`cmake/FlagRegistry.cmake` → `generated/flag_registry.h`, engine-fingerprint pattern, so it
  cannot drift); `mtg --list-flags` dumps it; both binaries warn at startup on any `MTG_*` env
  var not in the registry (typo / deleted flag = no longer a silent no-op; deliberately
  non-fatal so old scripts keep running). `MTG_BIN` + `heuristic_defaults.env` keys whitelisted.

- **C2 DONE** (`ca0cb96`) — `mtg-test` doctest target; twin-equivalence + golden mana-payment
  tests (6 cases / 156 assertions). Test seams: `TapForCostDirect` external linkage,
  `MtgTestSeam` friend.
- **C1 step 2 DONE** (`c33986c`) — the mana-payment twins unified into `TapForCostSharedOnce`
  (`src/ai/ManaPayment.cpp`), byte-identical (unit + smoke + full regression). Finding: under
  `MTG_TAP_LEGACY` the executor was the unfixed twin of the `6bb2791` coloured-pip fix
  (resolved toward `ProducesForPayment`).
- **C1 unit 2 DONE** (`227af4a`) — `EffectiveCost` twins → `EffectiveSpellCost`
  (`src/ai/ManaPayment.cpp`), byte-identical.
- **C1 unit 3 DONE** — `CastOrderLess`/`OrderingOpaque` twins (TurnSolver statics +
  AIEngine's `CastOrderLessAI`/`OrderingOpaqueAI`) → shared definitions in
  `src/ai/ManaPayment.cpp`, byte-identical (unit + smoke + full regression).
- **C1 unit 4 DONE** — `BuildPool`/`AIEngine::BuildAvailableMana` twins → the shared
  `AvailableManaPool` (`src/ai/ManaPayment.cpp`), byte-identical. All call sites renamed to
  the one name; the test seam's pool helper dropped (the shared function is public).
- **C1 unit 5 DONE** — the payment WRAPPER twins (`AIEngine::TapForCost` /
  `TapForCostDirect`, reserved-first retry) → `TapForCostShared`, and the mana-sink twins
  (`AnimateLands`/`ActivateTapTokens` vs `SimulateAnimateLands`/`SimulateTapTokens`) →
  `AnimateLandsShared`/`ActivateTapTokensShared` (all in `src/ai/ManaPayment.cpp`); the
  now-dead `TapForCostOnce`/`TapForCostDirectOnce` delegator layer deleted. Byte-identical.
  **Finding:** the tap-token twins were NOT byte-equal — the executor gates affordability on
  its accounting pool BEFORE tapping the {T} source (the gate can see the source's own mana),
  the rollout taps first and gates on a fresh board pool that excludes it (and ignores the
  payment result). Preserved as an explicit executor/rollout branch in
  `ActivateTapTokensShared` rather than silently picking one.
- **C1 unit 6 DONE** — the land drop existed **three** times (`TryPlaySpecificLand`, the
  `play_land_iter` lambda in `TryPlayLand`, and the rollout's `PlayLandByName`); the placement
  core is now `PlayLandFromHand` in the new `src/ai/LandPlay.{h,cpp}`, with every difference an
  explicit `LandPlayOptions` field. Byte-identical. **Finding (now FIXED, see below): the
  executor's GREEDY land drop never fired Forbidden Orchard's on-play Spirit**, while its searched
  drop and the rollout both did. Three more divergences documented in
  `docs/design/rollout-executor-lockstep.md`: `greedy_land_name`'s pre-pass has drifted from
  `TryPlayLand`'s, the claude-play land-entry chooser never reaches the real drop, and the
  scry/surveil source labels differ. Land SELECTION heuristics deliberately not unified (the
  executor's four-pass ranker and the rollout's two-pass fallback are different policies, not
  twins).
- **C1 unit 7 DONE** (`5a29c00`) — the combat twins (`GameEngine::CombatPhase` /
  `TurnSolver::SimulateCombat`) → `DeclareAttackerIndices` + `ResolveCombatDamage` in the new
  `src/ai/Combat.{h,cpp}`. Byte-identical. NOT shared, deliberately: the combat steps, stack
  resolution, state-based actions, the game log and play-viewer events (executor-only), and each
  caller's own attack-token / self-pump / firebreathing / exalted sequence, because the two order
  those differently (values identical today; passing the exalted bonus in preserves both orders
  rather than picking one). That leaves `TakeTurn`/`ApplyPlanDirect` as the only unconverted C1
  pair, which the safety order says to attempt last, if ever.
- **Orchard bug FIXED + GT rebaselined** — the greedy drop now fires the on-play Spirit like the
  other three sites. Smoke `hinata_smoke_d0_s1001` 7.1050→7.0790 (−0.026, 31 faster / 6 slower),
  regression `hinata_regression_d0_s2002` 7.1830→7.1500 (−0.033, 31 faster / 7 slower); every
  searched-depth case byte-identical, all other decks byte-identical, references clean. Smoke +
  regression ground truth accepted; overnight (held-out) rebaselined separately.
- **D1/D2 PARTIAL (user-approved)** — deleted with sign-off: 9 spent diagnostics
  (`MTG_ESC_PREDICT_{STATS,COSTCURVE,RALPHA}`, `MTG_HYBRID_LEAFDIAG`, `MTG_KEEP_{DETECT_Z,DUMP,SLOW_LOG}`,
  `MTG_TRACE_PLAYOUT_{SEED,TURN}`), 8 rejected-experiment knobs (`MTG_ESC_SINGLE_{ABS,FALLBACK,NOCLIMB,ROLLDEPTH,SEED}`,
  `MTG_ESCALATION_GATE_{T_LOW,BUDGET_CUT}`, `MTG_ESC_RESTORE`), and the `MTG_HINATA_SPASM_GATE`
  three-mode branch (redesign preserved on `recovered/hinata-spasm-gate-redesign`). All
  verified byte-identical (unit + smoke + regression). `MTG_SCORE_HIST` + `MTG_SCORE_DETAIL`
  deleted in a later focused pass (hist/detail plumbing in the MULL_EV and SCORE_COMPS
  diagnostics). NOT deleted: the `MTG_LEGACY_*` hatches (user chose keep), all `MTG_NO_*`
  hatches, capacity knobs, and keep-model gen knobs.

- **A5 DONE** — [docs/design/README.md](README.md) indexes all 108 docs (verified: every file
  linked exactly once, no broken links). Tags are the five this doc asked for plus `OPEN`, which
  earns its place because "a known live defect awaiting a fix" and "a parked idea nobody is
  working on" are materially different reads for an agent picking a task. The **dead-ends table
  is the point of the file**: 21 measured losses, each with its one-line reason, several of which
  are recorded *only* inside a long doc (the leaf-apply gate's "nothing slow may live in the
  rollout"; the nested-breakpoint result that smoke+regression alone would have adopted; the d0
  divergence-digest lesson that the rollout benefits from anti-WASTE rules, not from
  good-judgment rules the search already makes). Two stale headers found and flagged in place
  rather than edited: `per-deck-folder-layout.md` still says "deferred / not started" although
  the move shipped, and `land-signature-completeness.md` says GT-rebaseline pending.

- **A6 DONE** — hook numbering retired. All 43 `Hook N` mentions across the four provider/solver
  files (plus one in `SpellEffects.h`) now name the METHOD, and `DecisionProvider.h` carries a
  legacy `Hook N` decoder table so old commits and design docs stay readable. The design docs were
  deliberately left alone — decoding them is what the table is for. **The collision had already
  produced a wrong cross-reference:** `ScaledCastVariants` was labelled "Hook 28" in both
  `DecisionProviders.h` and `DecisionProviders.cpp`, but Hook 28 is `WantsCastOrderingSearch`;
  both now name the right method. Comment blocks whose lines grew past ~110 columns were reflowed.
  Verified comments-only mechanically (every changed line is a `//` line), then byte-identical:
  `mtg-test` 156/156, smoke 27/27, regression 45/45, 0 configs changed, 138 references clean.

- **B3 DONE** — `test/lib/harness.sh` (binary + deck-path resolution, manifest/batch, metric
  parsing, per-game diff, repro-command printer) + `test/lib/README.md` + a standing
  `test/lib/check_paths.sh` audit. `regression.sh` sources it for binary resolution and keeps its
  own manifest emitter (the `value_play` depth rules and the Hinata LPT weight are suite policy,
  and those manifest bytes are load-bearing for the GT). Suite re-verified 27/27 + 45/45.
  **Found while extracting:** `fd_quick_ab.sh` and `fd_overnight_ab.sh` hard-coded
  `build/Release/mtg.exe` with no fallback *and* pre-folder-move deck paths — neither could run on
  Linux at all; both fixed. 11 more scripts still name moved decklists (52 dead paths); they are
  unverified one-off A/Bs, so `check_paths.sh` **reports** them rather than blind-editing.
  Three latent bugs in the primitives themselves were caught by testing them before shipping —
  a dropped final manifest job, a `paste`-based diff that read the play digest as a win turn and
  mis-paired out-of-order files, and a numeric win-turn compare that ranked a loss (`-1`) as the
  fastest possible win. Every one of those yields a plausible number instead of an error, which is
  the whole argument for the library.

- **B2 DONE** — all **20** decision-JSON emitters (the doc said 18; the real count is 20) now build
  their frame through one `DecisionJson` writer in `src/main.cpp`. `grep 'os << "{\n"'` over the
  file returns exactly **1** hit, inside the helper. Kept hand-emitted rather than switched to
  `nlohmann::json`, as this doc specified — the bytes are a wire protocol. The helper is an ordered
  writer, not a schema: each emitter still picks its own key order, because several legitimately
  interleave their own keys into the prologue (`main_phase` puts `main_ordinal` before `type`;
  `vial`/`echo`/`target` deliberately pack keys onto one line). Three emitters keep a raw `os <<`
  line for exactly those historic layouts, commented as such.
  **Verification** is `test/lib/capture_decisions.py`, written for this: it replays every reference
  at every prefix (forced *and* unforced, the latter to reach the mulligan/bottom emitters) plus
  self-driving sweeps that take the engine's own default — including `--firebreathe-prompt` /
  `--storage-hold-prompt` sweeps, without which those two side-channel emitters are never reached
  at all. That is **11,511 frames covering all 23 wire types**, and the capture is itself verified
  deterministic (two runs byte-identical) before being trusted as a baseline. Converted in four
  batches, each built and byte-diffed against the baseline: **all four byte-identical.** Then
  `mtg-test` 156/156, smoke 27/27, regression 45/45, viewer protocol 138 ok / 0 drift.

- **B4 item 2 DONE, and it RESIZES the rest of B4.** The 7 largest **cold** helpers (819 LOC:
  `TapForCostBacktrack`, `PerformTutor`, `PerformLightPawsAttach`, `PerformMuxusReveal`,
  `PerformEtbDig`, `ResolveExpressiveIteration`, `BounceKarooLand`) moved to a new
  `src/core/SpellEffects.cpp`; the header keeps the declarations plus a note stating the rule.
  Measured on a quiet machine at `-O3`: TurnSolver 17.67→16.93 s (−4.2 %), AIEngine 7.05→6.68 s
  (−5.2 %), GameEngine 4.11→3.56 s (−13 %), new TU +3.25 s (parallel, off the critical path).
  Runtime **neutral**: deterministic callgrind Ir on burn / slivers / TH / Auras moved
  +0.0003 %…+0.0010 % (~1,200 instructions total). Suite 27/27 + 45/45, digests unchanged.
  **The honest correction to this doc's own framing:** a probe that stripped 1429 LOC (27 % of the
  header) promised −2.2 s on TurnSolver, but that probe included the per-cast / per-death /
  per-combat helpers, which must NOT move — there is no LTO anywhere in `CMakeLists.txt`, so an
  out-of-line body is un-inlinable across TUs and those fire on every rollout node. Restricted to
  provably cold bodies the win is −0.74 s, not −2.2 s. So **the remaining compile-time win is
  locked behind either enabling LTO or item 1 (splitting `TurnSolver.cpp`)** — moving more header
  bodies is now a small and increasingly risky lever. Note also that these helpers are fully
  inlined today and therefore **do not appear in a profile by name at all**; only total Ir moves,
  which is why the adopt/reject test has to be a deterministic Ir A/B rather than intuition.

- **B1 IN PROGRESS (analyzer half).** Five commits (`d83dac4`, `c0929ab`, `8d94d7a`, `2817eb6`,
  `7e55c28`), one unit each, every one verified byte-identical before the next was started:
  `RunExhaustiveKeep` **2423 → 1959 LOC** (equivalence-class construction; the policy-value,
  disagreement and label-noise reports; the notable-hands report; the runtime-policy and raw-sidecar
  writers) and `BuildKeepModel` **860 → 716 LOC** (the ridge-fit machinery).
  **The unlock was deduplication, not extraction.** `KeepVal` / `ArgminSub` / the hypergeometric hand
  weights / the Dopt backward induction existed as `[&]`-capturing lambdas in *three* functions —
  the policy builder, the generator and the merge tool's synth path — which is why the reporting
  sections could not move: they reached the machinery through a capture. One file-scope definition
  each removed both the duplication hazard (the copies had to agree exactly or a merged profile
  would stop matching the in-run one — the third copy is literally introduced by the comment
  "mirrors BuildPolicyFromTables' internals") and the obstacle.
  **Verification harness, reusable — build it before touching this file.** A deterministic ~20 s
  `MTG_KEEP_EXHAUSTIVE=1 MTG_KEEP_ROLLOUTS=1 MTG_KEEP_MAX_MULL=1 MTG_EQUIV_PROBES=4` burn
  regeneration whose raw sidecar and report are byte-diffable, extended to four paths: that gen, two
  disjoint-seed R=10 chunks (which cross the R floor and so actually write a runtime profile and
  exercise `BuildPolicyFromTables`), their merge, and a `MTG_KEEP_SYNTH_ADAPTIVE_BOTTOM`
  reconstruction of that merge — twelve artifacts, ~2 min 40 s. For `BuildKeepModel`,
  `MTG_KEEP_MODEL_ONLY=1 MTG_KEEP_SPLIT=both MTG_KEEP_GAMES=200 MTG_ANALYZE_DEPTH=2` against a
  *copy* of the deck folder fits all four models (gini / regret / additive score / hybrid) from one
  rollout table in ~46 s. Two gotchas, each of which silently invalidates a comparison:
  **`--seed` is mandatory** (an unseeded analyzer randomizes `meta.seed_base` per run, so every
  diff shows churn), and **`meta.engine_fp` must be normalized away** — it is a build-time hash over
  the engine source and so moves on a pure code move.
  Both files are in `mtg-analyze` only, never `mtg`/`mtg-core`, so no analyzer extraction can reach
  a play digest; the suite was still run (156/156, 27/27) before pushing.

- **B1 continued: `RunKeepMerge` 958 → 615** (`3af9fa4`) and **`RunClaudePlay` 1151 → 1018**
  (`d6c98a9`). A third of `RunKeepMerge` was one opt-in diagnostic (`MTG_KEEP_SIM_ADAPTIVE_BOTTOM`)
  that writes nothing and always returned, so it lifts out whole and the caller returns after the
  call. From `RunClaudePlay`: the four side-channel spec parsers, the chooser teardown, and the two
  output writers.
  **Two verification lessons worth keeping.** First, an opt-in path must be *entered and shown to do
  work* — the sim path was added as a fifth harness arm and the check only means something because it
  printed real numbers (+0.0157t blend regret, 50.9–31.8 % sub-table savings), not because it exited 0.
  Second, **`test/lib/capture_decisions.py` alone is NOT sufficient for `main.cpp`**: it stops at
  decision frames and never reaches the terminal `<<<CLAUDE_RESULT>>>` block or the `--log-dir` trace
  file, which is exactly what `WriteClaudePlayResult` / `WriteClaudePlayTrace` emit. The second A/B
  (replay all 138 references to completion with `--log-dir`, diff the result frame plus the written
  trace bytes; 106 result frames) is the other half and belongs with it.

Still open: the rest of B1.
- `RunExhaustiveKeep` (1959) — the `MTG_KEEP_PRIOR_RAW` change-detection carry is the largest single
  block left at ~256 LOC, but ~9 of its locals escape into the refine loop (27 later references), so it
  needs a carry struct and a new verification path rather than a verbatim move.
- `RunClaudePlay` (1018) — the remaining ~760 LOC is eighteen chooser lambdas. **Do not move them
  piecemeal:** the engine stores their addresses in `g_play_*` globals, so a lambda that moves out of
  the function's stack frame dangles. The extraction that works is an owning `struct PlayChoosers`
  built in the caller plus a context struct for what they capture (`decisions_made`, `choices`,
  `trace`, `main_ordinal`, `state`, `reveal_count`, `log_dir`) — a design change, not a move, and it
  needs both halves of the verification above.
- B4 item 1 (the TurnSolver split). Remaining C1 units.

Items are grouped by **risk tier**, not by subsystem, because in this repo the cost of a change
is dominated by how hard it is to prove it did not alter play. Work top-down: Tier A items are
safe and immediately useful; Tier C items need a test seam that does not exist yet.

---

## 0. Read this before you change anything

This codebase is *not* messy through neglect. It is a search engine that has been tuned against
a measured metric for months, and most of what looks like clutter is a deliberate A/B lever.
Three hard constraints follow from that:

1. **The metric is average turn-to-win** (unwon = `max_turns + 1`, i.e. 9 for a 8-turn cap);
   negative delta = better. Win% is noise on a goldfish. A refactor is "free" only if it is
   byte-identical or if a held-out sweep says the delta is ≥ 0 in the good direction.
2. **`MTG_UNPRUNED` / `MTG_UNPRUNE=<gates>` must keep working.** The architecture bar is
   *search primary, heuristics only prune*, and that gate ([DecisionProviders.h:38-64](src/ai/DecisionProviders.h#L38-L64))
   is the audit tool that proves it. Do not "simplify" a pruning gate out of existence — extract
   it, keep the gate.
3. **Never wrap a run in a timeout, and never kill a run that has been going > ~10 minutes.**
   See [CLAUDE.md](CLAUDE.md). A truncated sweep reads as a result and corrupts an A/B.

The verification recipe used throughout this document:

```
./build.sh                                    # never raw cmake
test/regression.sh smoke                      # < 15 min, fingerprint = <avg>/<digest>
test/regression.sh regression                 # < 45 min, train seeds
test/regression.sh overnight                  # < 8 h, HELD-OUT seeds — the real test
test/regression.sh <mode> --accept            # promote an INSPECTED run to ground truth
```

"Byte-identical" means the per-mode digest in `test/regression_gt.txt` is unchanged, on **all
8 decks**, at **every mode**. A refactor that changes a digest is a behaviour change and needs
the held-out sweep and the user's sign-off, no matter how mechanical it looked.

---

## Tier A — zero behaviour risk, do these first

### A1. `cmake --build build` still builds `-O0`, and a stale `-O0` analyzer is on disk

**This is the exact footgun the project has a CMake guard, a `build.sh`, and a CLAUDE.md
section to prevent — and the guard does not cover the path that is actually reachable today.**

Evidence:

- The guard at [CMakeLists.txt:8-13](CMakeLists.txt#L8-L13) only fires when
  `CMAKE_BUILD_TYPE` *and* `CMAKE_CONFIGURATION_TYPES` are both empty. The live tree is
  Ninja **Multi-Config**, so `CMAKE_CONFIGURATION_TYPES` is set and the guard never runs.
- `build/CMakeCache.txt` holds `CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release;RelWithDebInfo;Profile`.
  Ninja Multi-Config defaults to the **first** entry when no `--config` is given, and
  `CMAKE_DEFAULT_BUILD_TYPE` is not set anywhere. Dry run confirms it:
  `ninja -C build -n` schedules **24 Debug objects**.
- The `-O0` artefacts already exist: `build/Debug/mtg-analyze` (25 MB) and
  `build/Debug/libmtg-core.a` (51 MB), both dated 2026-07-07. So this has happened.
- `build/compile_commands.json` is generated for the Debug config
  (`-DCMAKE_INTDIR=\"Debug\" ... -g -std=gnu++20`, **no `-O` flag**), so clangd/IDE
  diagnostics and anyone who reads that file sees the unoptimized flags.

Action (three lines in [CMakeLists.txt](CMakeLists.txt), no source change):

```cmake
# Multi-config: a bare `cmake --build build` must NOT pick Debug (-O0). Also fixes
# compile_commands.json, which is emitted for the default config.
set(CMAKE_DEFAULT_BUILD_TYPE Release)
# And drop Debug from the offered configs (CLAUDE.md: there is deliberately no -O0 mode).
```

Then delete the stale `build/Debug/` artefacts. Verify with `ninja -C build -n` (should
schedule Release objects) and by re-checking `CMAKE_INTDIR` in `compile_commands.json`.

**Risk: none** — build configuration only, no source touched. Highest value-per-line item in
this document.

### A2. `ApplyHeuristicDefaults()` silently no-ops unless CWD is the repo root

[HeuristicDefaults.h:20-24](src/core/HeuristicDefaults.h#L20-L24) defaults its path to the
**relative** `"src/ai/data/heuristic_defaults.env"` and returns silently when the file is
absent. Called from [main.cpp:2903](src/main.cpp#L2903) and
[analyzer/main.cpp:42](src/analyzer/main.cpp#L42).

So the set of live heuristic defaults depends on the **current working directory** of the
process. Run `mtg` from `logs/`, from a batch worker with a different CWD, or from a second
machine's checkout with a different layout, and every adopted heuristic silently reverts to
baseline — with no warning and no fingerprint difference to catch it. That is a
cross-machine-reproducibility hazard of exactly the kind the profile-pooling protocol exists to
prevent.

The file is currently empty of adoptions (`# (no adopted heuristics yet)`), so **there is no
live bug** — which makes this the cheapest possible moment to fix it.

Action: resolve the path relative to the executable (`/proc/self/exe` on Linux, or
`std::filesystem::canonical(argv[0]).parent_path()`) with the CWD-relative path as a fallback,
and **log to stderr when the file is found and non-empty** so a run's adopted-defaults set is
visible in its own log. This matches the repo's existing convention of resolving sidecar models
directory-relative off the profile path.

**Risk: none while the file is empty.** Do it before the first adoption lands, not after.

### A3. `MTG_X=0` enables 88 of the ~290 flags — and the docs tell you to write exactly that

Three different truthiness conventions coexist:

| Convention | Sites | `MTG_X=0` means |
|---|---:|---|
| `getenv(...) != nullptr` | 88 | **ON** (presence-only) |
| `getenv(...) == nullptr` | 41 | **ON** when unset; `=0` is still ON |
| `e && *e && std::string(e) != "0"` | 17 | OFF (value-aware) |

This has already burned the project once — recorded verbatim in agent memory as
*"Gotcha: `MTG_MAGMA_FAITHFUL=0` still ENABLES it (getenv != null → UNSET for default)"*.

Worse, [HeuristicDefaults.h:10-11](src/core/HeuristicDefaults.h#L10-L11) documents the
disable lever as *"set its env var to the baseline"* and `auto_heuristics.py` writes
`KEY=VALUE` lines that are `setenv`'d at startup. For any presence-only flag, writing
`MTG_FOO=0` into that file **turns the heuristic on**. Latent today only because the file has
no entries.

Action (mechanical, byte-identical if done right):

1. Add `EnvOn(const char* key)` / `EnvInt(const char* key, int dflt)` helpers in one header,
   with the value-aware convention (`unset` or `0`/`off`/`false` → off) documented once.
2. Migrate the 41 `== nullptr` and 17 value-aware sites first — for those, the value-aware
   convention is already the intent, so migration is provably byte-identical.
3. The 88 presence-only sites are a **behaviour change** (`MTG_X=0` flips meaning). Migrate
   them, but note in the commit that any script setting `=0` to mean "on" must be updated —
   grep `test/` and `scripts/` (55 scripts invoke the binary directly) before flipping.
4. Add a startup validator: walk `environ`, warn on any `MTG_*` name not in the known-flag
   list. A typo'd flag currently means "default" with zero feedback, which silently invalidates
   an A/B arm.

### A4. Prune the orphaned one-off experiment scripts

`scripts/` holds **85 files**. Classified by who references them:

- **12 orphans** — referenced by nothing at all, not even a design doc:
  `dragonstorm_reconstruct_sweep.sh`, `esc_freshfrac_ab.sh`, `esc_mix_sweep.sh`,
  `esc_predict_ab.sh`, `ritual_guard_ab.sh`, `ritual_guard_ab_deck.sh`,
  `valueleaf_batch.py`, `valueleaf_finalize.py`, `xover_ab5.sh`, `xover_ab_heldout.py`,
  `xover_attribute.py`, `requirements.txt`.
- **~40 docs-only** — cited by a `docs/design/*.md` write-up of a finished experiment
  (the whole `valueleaf_*`, `nc_*`, `esc_*`, `xover_*`, `train_eval_*` family).
- **~11 live** — reachable from a skill, `CLAUDE.md`, the harness, or `src/`:
  `analyze_deck.py`, `verify_deck.py`, `audit_card_fields.py`, `audit_card_costs.py`,
  `audit_viewer_decisions.py`, `analyze_earliest_wins.py`, `auto_heuristics.py`,
  `esc_train_gate.py`, `play_invariants.py`, `rollout_divergence_digest.py`,
  `mine_heuristics.sh`.

`test/` is the same shape: **38 `.sh`**, of which **8 are untracked** one-offs
(`burn_mm6_overnight.sh`, `keepmodel_ab_widseeds.sh`, `keepmodel_overnight_chain.sh`,
`mm6_ab_chain.sh`, `mm6_full_supervisor.sh`, `mm6_overnight_chain.sh`, `th_keepflip_ab.sh`,
`th_keepflip_nc_ab.sh`).

Action, in order of confidence:

1. Delete the 12 orphans. Nothing can call them; the experiments they ran are recorded in
   `docs/design/`.
2. Move the docs-only family to `scripts/attic/` (or delete — git remembers). The point is that
   `ls scripts/` should show the ~11 tools that are part of a workflow, not 85 files where the
   live ones are outnumbered 7:1. Whichever you choose, add the moved/deleted path to the
   design doc that cites it so the write-up stays self-contained.
3. For the 8 untracked `test/*.sh`: either commit them (they are real work and
   `git status` noise hides genuine changes) or delete them. Do not leave them untracked —
   per [CLAUDE.md](CLAUDE.md) the repo's rule is that deferred work lives in git.

**Risk: none** for orphans. For the docs-only set, `grep -rl <name> docs/` first and update the
citing doc in the same commit.

### A5. Give `docs/design/` an index

105 files, 1.5 MB, no `README.md` or `INDEX.md`. They mix live specifications
(`exhaustive-keep-policy.md`, `per-deck-folder-layout.md`), adopted-feature write-ups
(`accelerant-ordering-and-self-funding.md`), and *measured dead ends* that must not be
re-attempted (`enumeration-feasibility-via-executor.md`'s leaf-probe result, the
land-colour-capping result, the agreed-post-hybrid placement result).

A new agent cannot tell those apart without reading all 105. That is how a measured dead end
gets re-implemented.

Action: add `docs/design/README.md` — one line per doc with a status tag:
`SPEC` / `ADOPTED` / `DEAD-END` / `DEFERRED` / `HISTORICAL`. Sort dead ends to their own
section with a one-line "what was measured and why it lost". Cheap to write, and it is the
single highest-leverage document for onboarding.

### A6. Fix the colliding hook numbers

The `DecisionProvider` hook numbering has **duplicates**, counted over
[DecisionProvider.h](src/ai/DecisionProvider.h):

| Hook # | times declared |
|---|---:|
| 30 | 3 |
| 22 | 3 |
| 1 | 3 (`1`, `1b` variants — legitimate) |
| 21, 20, 18, 16, 13 | 2 each |

Concretely: "Hook 16" is both *combat: should this creature attack* ([:193](src/ai/DecisionProvider.h#L193))
and *does this deck's goldfish opponent play lands* ([:284](src/ai/DecisionProvider.h#L284)).
"Hook 22" is three unrelated things ([:256](src/ai/DecisionProvider.h#L256),
[:270](src/ai/DecisionProvider.h#L270), [:310](src/ai/DecisionProvider.h#L310)).

Hook numbers are used as identifiers in commit messages, design docs, and code comments
(`Hook 26 force-keep`, `Hook 30 splice collapse`), so a collision makes the historical record
ambiguous — and there are 79 `Hook` mentions across the four provider/solver files.

Action: stop numbering. The hooks are already virtual methods with names; refer to them by
method name (`ShouldAttackWith`, `OpponentPlaysLands`). Renumber-or-rename in one pass, add a
table at the top of `DecisionProvider.h` mapping every legacy `Hook N` mention to its method
name so old docs and commits stay decodable. Comment-only change.

### A7. De-duplicate the flag statics that are declared twice

Identical `static const` flag readers exist in two translation units:

- `s_legacy_static_tapped` — [AIEngine.cpp:2644](src/ai/AIEngine.cpp#L2644) and
  [TurnSolver.cpp:9391](src/ai/TurnSolver.cpp#L9391)
- `s_land_closing_window` — [AIEngine.cpp:2645](src/ai/AIEngine.cpp#L2645) and
  [TurnSolver.cpp:9392](src/ai/TurnSolver.cpp#L9392) (same lambda, copy-pasted)
- `s_bp_trace` — [AIEngine.cpp:85](src/ai/AIEngine.cpp#L85) and
  [TurnSolver.cpp:10957](src/ai/TurnSolver.cpp#L10957)

These are *by design* meant to agree (executor and rollout must read the same flag), which is
precisely why they should be read once in one place. Two copies is two chances to update one
and not the other — the same failure mode as Tier C below, in miniature.

Action: one `inline bool LandClosingWindowEnabled()` etc. per flag, in the header nearest the
behaviour. Byte-identical by construction; fold into the A3 sweep.

### A8. Disk hygiene: 1.1 GB of superseded build trees

`build-asan/` (452 MB), `build-prof/` (282 MB), `build-tsan/` (428 MB) are root-level trees
predating the multi-config `build/` layout. `build-prof` is fully superseded by
`./build.sh profile` → `build/Profile`. All three are gitignored (`build-*/`), so this is
disk-and-clutter only, not repo hygiene. `logs/` is 20 GB.

Action: delete `build-prof/`. For asan/tsan, note that `build.sh` offers **no** sanitizer mode —
if sanitizer builds are still wanted, add `asan`/`tsan` modes to `build.sh` (as
`build/Asan`, `build/Tsan`) rather than leaving hand-rolled trees around that nothing
regenerates. Also worth adding: a note in `CMakeLists.txt` line 39 which still documents the
obsolete `cmake -S . -B build-prof -DMTG_PROFILE=ON` recipe.

---

## Tier B — mechanical refactors; prove byte-identical

These do not change behaviour if done carefully, but they touch code the search runs millions
of times, so each one needs a digest check. Land them **one at a time** — a batch of five
"obviously safe" extractions that together move a digest is very expensive to bisect.

### B1. Eleven functions over 500 lines; two over 1700

| LOC | Location | Function |
|---:|---|---|
| 2423 | [ExhaustiveKeep.cpp:243](src/analyzer/ExhaustiveKeep.cpp#L243) | `RunExhaustiveKeep` |
| 1796 | [TurnSolver.cpp:5189](src/ai/TurnSolver.cpp#L5189) | `ApplyPlanDirect` |
| 1357 | [AIEngine.cpp:1199](src/ai/AIEngine.cpp#L1199) | `AIEngine::TakeTurn` |
| 1083 | [main.cpp:1378](src/main.cpp#L1378) | `RunClaudePlay` |
| 961 | [TurnSolver.cpp:7939](src/ai/TurnSolver.cpp#L7939) | `EnumeratePlans` |
| 958 | [ExhaustiveKeep.cpp:2674](src/analyzer/ExhaustiveKeep.cpp#L2674) | `RunKeepMerge` |
| 860 | [KeepModelTrainer.cpp:464](src/analyzer/KeepModelTrainer.cpp#L464) | `BuildKeepModel` |
| 830 | [TurnSolver.cpp:3536](src/ai/TurnSolver.cpp#L3536) | `TurnSolver::Solve` |
| 793 | [TurnSolver.cpp:11224](src/ai/TurnSolver.cpp#L11224) | `FullSearchLineHybrid` |
| 774 | [TurnSolver.cpp:1716](src/ai/TurnSolver.cpp#L1716) | `CollectActions` |
| 665 | [TurnSolver.cpp:12810](src/ai/TurnSolver.cpp#L12810) | `CheckLine` |

(18 functions exceed 300 LOC.)

**Start with the analyzer ones, not the solver ones.** `RunExhaustiveKeep` (2423 LOC) and
`RunKeepMerge` (958 LOC) are offline generation code: they do not run inside the search, so an
extraction there cannot perturb a play digest, and correctness is checkable by regenerating one
small profile and diffing the output JSON. That makes them the safe place to build confidence
in the extraction workflow before touching `ApplyPlanDirect`.

For the solver functions, extract only along seams the code already marks with comment
banners — do **not** reorganize control flow. The mechanical, safe pattern is
`static` helper + call, with the helper taking the same references, so the compiler produces the
same sequence of operations.

`RunClaudePlay` (1083 LOC) is a special case: it is opt-in verification tooling
(`--claude-play`), so a bug there costs a debugging session, not a corrupted ground truth. Good
second target.

### B2. `main.cpp`: 18 hand-rolled decision-JSON writers with an identical prologue

`grep -c 'static void Write.*DecisionJson' src/main.cpp` → **18**, spanning
[main.cpp:406](src/main.cpp#L406)–[main.cpp:1352](src/main.cpp#L1352). Each repeats the same
7-line preamble:

```cpp
os << "{\n";
os << "  \"decision_index\": " << decision_index << ",\n";
os << "  \"type\": \"" << ... << "\",\n";
os << "  \"source\": "; JsonStr(os, source); os << ",\n";
os << "  \"turn\": " << s.turn_number << ",\n";
WriteBoardContext(os, s, 0);
os << "  \"heuristic_default\": " << heuristic_default << ",\n";
```

then an options array, then a `note`. Compare
[WriteBounceDecisionJson:987](src/main.cpp#L987) with
[WriteDigDecisionJson:1018](src/main.cpp#L1018) — the difference is the option-item shape and
the note text.

This is also the protocol surface that `test/viewer_protocol_check.py` (16 KB) and
`test/viewer_client_check.js` (19 KB) validate, i.e. 35 KB of test code guarding 18 copies of
one emitter. Adding a 19th decision type today means copy-pasting the prologue and hoping.

Action: one `DecisionJson` helper — `Begin(os, index, type, source, state, heuristic_default)`,
`Options(os, range, item_fn)`, `Note(os, text)` — and 18 short call sites. Verify by
byte-diffing the emitted JSON for every decision type against the current binary before/after
(`test/viewer_validate_baseline.txt` already exists for this purpose).

Note the project already links `nlohmann::json` ([CMakeLists.txt:51-56](CMakeLists.txt#L51-L56)).
Hand-rolling the output is defensible for exact-byte stability, so **keep the manual emitter** —
just stop having 18 of them.

### B3. `test/` and `scripts/`: no shared harness library, 55 direct binary invocations

- 55 scripts invoke `build/Release/mtg` directly.
- 16 re-implement the avg-turn-to-win metric math inline.
- 26 build their own `MTG_*` A/B skeleton.
- There is no `test/lib.sh`, `test/common.sh`, or equivalent.

`test/regression.sh` (19 KB) already implements pooled-batch execution, per-mode seed
disjointness, fingerprint parsing, and the accept flow correctly. Every one-off A/B script
reimplements a subset of it, which is how an A/B ends up measuring the wrong thing (the repo
has already hit "the correct d5 repro is `value_play`, omit `--depth`, `--game-index gi`,
seed `base+gi`" — a config mismatch that produced phantom nondeterminism).

Action: extract `test/lib/harness.sh` with the primitives the harness already has —
`build_manifest`, `run_pooled_batch`, `parse_avg`, `delta_vs`, `heldout_seeds` — and have
`regression.sh` source it. New A/B scripts then get the pooled-single-queue behaviour that
[CLAUDE.md](CLAUDE.md) mandates for free, instead of each one re-learning it.

### B4. Compile-time / dev-loop cost

Measured single-TU compiles at `-O3` on this machine:

| TU | wall | peak RSS |
|---|---:|---:|
| `src/ai/TurnSolver.cpp` | **19.2 s** | 730 MB |
| `src/ai/AIEngine.cpp` | 9.1 s | 465 MB |
| `src/core/GameEngine.cpp` | 6.4 s | 375 MB |
| `SpellEffects.h` alone (empty TU) | 1.6 s | 306 MB |
| `nlohmann/json.hpp` alone | 0.8 s | 221 MB |

So the header is *not* the dominant cost — `TurnSolver.cpp`'s own 13 501 lines are. But
[SpellEffects.h](src/core/SpellEffects.h) is 4722 lines of `inline` implementation (163 `inline`
functions, including a 268-LOC `TapForCostBacktrack` at
[:4355](src/core/SpellEffects.h#L4355)) included by 13 TUs, so **any** edit to it recompiles
everything, at ~1.6 s × 13 plus each TU's own work.

Action, in value order:

1. Splitting `TurnSolver.cpp` into a few TUs along the existing seams (candidate collection,
   plan enumeration, plan execution, search drivers) turns a 19 s serial step into parallel
   ones. This is the same work as B1 and should be done as its outcome, not separately.
2. Move the large non-trivial bodies out of `SpellEffects.h` into a `SpellEffects.cpp`
   (declarations stay in the header). Start with `TapForCostBacktrack` and the four ~190-LOC
   functions around [:2577-2581](src/core/SpellEffects.h#L2577-L2581).
3. `MulliganProfileIO.h` (1099 LOC, 35 `inline`, 6 includers) is the same pattern at smaller
   scale.

Do **not** reach for a precompiled header first — that hides the structural problem and makes
the build tree more fragile.

---

## Tier C — the structural item: the engine is implemented twice

This is the largest real debt in the repository, and the one that has produced actual bugs.

### C1. Executor and rollout are twin implementations of the same rules

The real game runs through `GameEngine`/`AIEngine`. The search's rollout runs through
`TurnSolver`'s `Simulate*`/`ApplyPlanDirect` family. They must agree exactly — and they are
separate code.

Measured overlap:

| Executor | Rollout | Overlap |
|---|---|---|
| [`AIEngine::TapForCostOnce`:2992](src/ai/AIEngine.cpp#L2992) (375 LOC) | [`TapForCostDirectOnce`:4392](src/ai/TurnSolver.cpp#L4392) (377 LOC) | **303 of 381 lines identical** (whitespace-insensitive) |
| [`AIEngine::TakeTurn`:1199](src/ai/AIEngine.cpp#L1199) (1357 LOC) | [`ApplyPlanDirect`:5189](src/ai/TurnSolver.cpp#L5189) (1796 LOC) | 307 identical non-comment lines |
| [`GameEngine::CombatPhase`:334](src/core/GameEngine.cpp#L334) (147 LOC) | [`SimulateCombat`:6987](src/ai/TurnSolver.cpp#L6987) (95 LOC) | same rules, separate code |
| [`AIEngine::TryPlayLand`:2648](src/ai/AIEngine.cpp#L2648) | [`SimulateLandPlay`:7472](src/ai/TurnSolver.cpp#L7472) / [`PlayLandByName`:7347](src/ai/TurnSolver.cpp#L7347) | " |
| [`AIEngine::AnimateLands`:2869](src/ai/AIEngine.cpp#L2869) | [`SimulateAnimateLands`:7123](src/ai/TurnSolver.cpp#L7123) | " |
| [`AIEngine::ActivateTapTokens`:2915](src/ai/AIEngine.cpp#L2915) | [`SimulateTapTokens`:7081](src/ai/TurnSolver.cpp#L7081) | " |
| [`AIEngine::EffectiveCost`:3400](src/ai/AIEngine.cpp#L3400) | [`EffectiveCost`:671](src/ai/TurnSolver.cpp#L671) | " |
| [`CastOrderLessAI`:247](src/ai/AIEngine.cpp#L247) | [`CastOrderLess`:4824](src/ai/TurnSolver.cpp#L4824) | " |
| [`OrderingOpaqueAI`:268](src/ai/AIEngine.cpp#L268) | [`OrderingOpaque`:5004](src/ai/TurnSolver.cpp#L5004) | " |
| [`AIEngine::BuildAvailableMana`:2960](src/ai/AIEngine.cpp#L2960) | [`BuildPool`:258](src/ai/TurnSolver.cpp#L258) + [`BuildNonCreaturePool`:280](src/ai/TurnSolver.cpp#L280) | " |

Mana payment specifically exists **three** times: the two above plus
[`TapForCostBacktrack`](src/core/SpellEffects.h#L4355) as the exponential fallback.

The coupling is maintained entirely by comment discipline: **376 occurrences** of
"lockstep" / "mirror" / "keep the two in sync" across `src/`. Sample, verbatim from
[AIEngine.cpp:2991](src/ai/AIEngine.cpp#L2991):

> `// Mirrors TurnSolver::TapForCostDirectOnce byte-for-byte (lockstep).`

and from [TurnSolver.cpp](src/ai/TurnSolver.cpp) inside the twin:

> `// Mirrored in AIEngine::tap_source -- keep the two in lockstep.`

This has cost real bugs, all of the same shape — one twin fixed, the other not:

- the coloured-pip payment path reading `EffectiveProduces` instead of `ProducesForPayment`
  (fixed `6bb2791`);
- the legend-rule state-based action (fixed `b71e5e3`);
- staged-cards-vs-max-hand-size and the land copy-ID (fixed `87b8823`);
- the recorder dropping `chosen_float_color` → `WILD` ("lockstep #6");
- the Karoo bounce land producing one colour instead of two — the comment in the fix explicitly
  says the spell was "enumerated but unpayable, a silent no-op".

`docs/design/rollout-executor-lockstep.md` exists and `fd-diverge` is currently 0 on all 8
decks, so the *current* state is clean. The debt is that it takes a dedicated divergence
detector plus periodic audits to keep it clean, forever.

**Do not attempt a big-bang unification.** The right sequence:

1. **Build the test seam first (C2).** Without it, a unification is unverifiable.
2. Unify the **leaf-most, most-duplicated** unit only: `TapForCostOnce` /
   `TapForCostDirectOnce` → one function parameterized by what actually differs. From the diff,
   the real differences are: (a) the executor threads a `ManaPool& available` and decrements it,
   the rollout does not; (b) `ap.life` vs `state.players[active].life` (identical semantics);
   (c) logging. That is a `struct TapContext { ManaPool* available; bool log; }` away from being
   one function. **303 of 381 lines are already identical.**
3. Prove byte-identical on all 3 modes, accept the (unchanged) GT, commit, stop.
4. Only then consider the next unit, in this order of decreasing safety:
   `EffectiveCost` → `CastOrderLess`/`OrderingOpaque` → `BuildPool`/`BuildAvailableMana` →
   `AnimateLands`/`ActivateTapTokens` → land play → combat. `TakeTurn`/`ApplyPlanDirect` is
   **last**, and probably never as a single step.

Each step must be its own commit with its own digest check. If a step cannot be made
byte-identical, stop and report the discrepancy to the user — a discrepancy found this way is a
**bug in one of the twins**, which is a finding worth more than the refactor.

### C2. There are no unit tests, at all

`grep -rn 'enable_testing\|add_test\|catch2\|gtest\|doctest' CMakeLists.txt cmake/` → nothing.
There is no test target; `test/` is entirely end-to-end shell harness over the built binaries.

Consequence: the cheapest way to check *any* change is a 15-minute smoke run, and the honest way
is a 45-minute regression or an 8-hour overnight. That cost is why the giant functions stay
giant — the feedback loop makes small safe refactors uneconomic, so nobody does them.

Action — deliberately small, since a large test suite would itself become a maintenance burden:

1. Add a `mtg-test` target (the repo already fetches dependencies via `FetchContent`, so
   Catch2 or doctest is a 6-line addition) plus `enable_testing()`/`add_test`.
2. Seed it with tests **only** for the units Tier C wants to unify, written against the
   *current* behaviour as golden values: pay a cost from a fixed board with each twin and assert
   they produce the same end-state; `EffectiveCost` for the discount/splice/X cases;
   `CastOrderLess` ordering on a fixed action list; the land-signature dedupe key.
3. That gives a **seconds-long** check that a unification is faithful, before spending 45
   minutes on the digest.

This is the enabler for everything in Tier B and C. It is worth doing first even though it
appears late in this document.

---

## Tier D — needs the user's decision, do not act unilaterally

### D1. ~290 environment flags, and no lifecycle for retiring them

`grep -rhoE 'MTG_[A-Z0-9_]+' src/ | sort -u` → **290 distinct names** across **310 `getenv`
call sites**. By file: `TurnSolver.cpp` 115, `ExhaustiveKeep.cpp` 48, `analyzer/main.cpp` 44,
`AIEngine.cpp` 31, `SpellEffects.h` 18, `DecisionProviders.cpp` 17.

Most of these are load-bearing: A/B levers, escape hatches for adopted changes
(`MTG_LEGACY_*`), instrumentation, and the `MTG_UNPRUNED` audit gate. **That is the
architecture working as intended** and must not be swept away.

But some are provably spent. **64 flags are referenced nowhere outside `src/`** — not in
`test/`, `scripts/`, `.claude/skills/`, `docs/`, or `tools/`:

```
MTG_DISCARD_PROTECT             MTG_ESCALATION_GATE_BUDGET_CUT  MTG_ESCALATION_GATE_T_LOW
MTG_ESC_PREDICT_COSTCURVE       MTG_ESC_PREDICT_RALPHA          MTG_ESC_PREDICT_STATS
MTG_ESC_RESTORE                 MTG_ESC_SINGLE_NOCLIMB          MTG_ESC_SINGLE_ROLLDEPTH
MTG_ESC_SINGLE_SEED             MTG_EVAL_MAX_TREES              MTG_FD_LEAF_DEPTH
MTG_HYBRID_LEAFDIAG             MTG_KEEP_DETECT_Z               MTG_KEEP_DUMP
MTG_KEEP_FORM                   MTG_KEEP_MODEL                  MTG_KEEP_NO_PROBE_CARRY
MTG_KEEP_PROBE_CARRY            MTG_KEEP_REPLAY_PD              MTG_KEEP_REPLAY_R
MTG_KEEP_SIM_SEED               MTG_KEEP_SUB_CUTOFF_R           MTG_KEEP_SYNTH_ABOT_CAPNOISE
MTG_KEEP_SYNTH_ABOT_FLIP_EPS    MTG_LEGACY_2ND_MAIN_LAND        MTG_LEGACY_LIGHTPAWS_STATIC
MTG_LEGACY_NO_AURA_NEW_CREATURE MTG_LEGACY_SOLVE                MTG_LOG_DIR
MTG_LOG_HAND                    MTG_LOG_MAXTOP2LANDS            MTG_LOG_N
MTG_LOG_PLAY                    MTG_LOG_TURN                    MTG_MANA_PRUNE
MTG_MULL_EV                     MTG_MULL_EV_MAXM                MTG_NO_BP_ENUM_CACHE
MTG_NO_COMBO_LINE               MTG_NO_DEFER_CANTRIP            MTG_NO_FLOAT_LEFTOVER
MTG_NO_GOFF_SHORTCIRCUIT        MTG_NO_HINATA_HOLD_CRACKLE      MTG_NO_KAROO_DEFER
MTG_NO_LOTUS_PREFIX             MTG_NO_RESERVE                  MTG_NO_RITUAL_PAYOFF_GUARD
MTG_NO_ROCK_RAMP                MTG_NO_SEARCH_SHUFFLE           MTG_POOL_ALLOC
MTG_SCORE_DETAIL                MTG_SCORE_HIST                  MTG_SEARCH_SHUFFLE
MTG_SOLVE_GROUP_CAP             MTG_TAP_SCARCITY                MTG_TRACE_PLAYOUT_SEED
MTG_TRACE_PLAYOUT_TURN          MTG_TT_CAP                      MTG_X
```

(`MTG_HAVE_ZLIB` and the `MTG_*_` prefix-match artefacts in that scan are not flags.)

Being src-only is **evidence, not proof** — a `MTG_NO_*` hatch for an adopted default is
supposed to be unused until someone needs it. So:

**Action — propose, do not delete.** Split the 64 into three buckets and put the list in front
of the user:

- **Instrumentation with no consumer** (`MTG_*_STATS`, `MTG_*_TRACE`, `MTG_*_DUMP`,
  `MTG_LOG_*`, `MTG_KEEP_DETECT_Z`, `MTG_HYBRID_LEAFDIAG`) — these answered a question that has
  been answered. Deleting them removes dead branches from hot functions.
- **Tuning knobs for rejected experiments** — the `MTG_ESC_SINGLE_*` / `MTG_ESC_PREDICT_*`
  families are the parameter surface of escalation variants whose outcomes are already recorded
  in `docs/design/escalation-*.md`. If the variant lost, its knobs are dead weight.
- **`MTG_LEGACY_*` hatches** — genuinely valuable, and per user feedback *"needs a GT rebaseline"
  is never a reason to default something off*. Keep unless the user says the hatch has aged out.
  Note `MTG_LEGACY_SOLVE`, `MTG_LEGACY_2ND_MAIN_LAND`, `MTG_LEGACY_LIGHTPAWS_STATIC`,
  `MTG_LEGACY_NO_AURA_NEW_CREATURE` are the oldest and the most likely to be retirable.

Also worth proposing, independent of deletion: a `--flags` / `mtg --list-flags` dump —
every known flag, its default, its owning subsystem, and whether it is
`instrument | hatch | ab-lever | audit`. That is the registry the validator in A3 needs, and it
makes the flag surface reviewable instead of grep-only.

### D2. Flags whose experiment is documented as not-adopted

`MTG_HINATA_SPASM_GATE` (7 sites, [DecisionProviders.cpp:1847](src/ai/DecisionProviders.cpp#L1847)
plus a mode reader in the solver at [TurnSolver.cpp:998](src/ai/TurnSolver.cpp#L998)) gates a
three-mode variant whose recorded outcome is "perf/quality tradeoff, NOT adopted", and whose
redesign lives on an unmerged branch. It is the clearest example: a live three-way branch in a
hot provider hook for a variant nobody selected.

Contrast with `MTG_TH_STRICT_FLOOD`
([DecisionProviders.cpp:959-989](src/ai/DecisionProviders.cpp#L959-L989)), which is **not** a
candidate — adopted default-on, correct value-aware `=0` hatch, and 30 lines of comment
explaining the irreducible fd-diverge floor it creates and that seed 4661 must **not** be
"fixed". That is what a healthy retained lever looks like; use it as the shape to compare
against, and as the reference for the value-aware convention in A3.

Action: for each genuine candidate, either (a) delete the losing branch and keep the winner
inline, or (b) confirm with the user that it stays as a lever. Ask once, in a batch, with the
list — do not open a question per flag.

---

## Do NOT do these

Recorded so a future agent does not "clean up" something load-bearing:

- **Do not remove or narrow `MTG_UNPRUNED` / `MTG_UNPRUNE` / `UnprunedGate`.** It is the user's
  core architectural bar, and the granular gate list at
  [DecisionProviders.h:38-64](src/ai/DecisionProviders.h#L38-L64) exists because the global
  version explodes the tree. `SetGateProbe`/`QueriedGatesMask` likewise.
- **Do not "simplify" the `DecisionProvider` archetype split back into the solver.** Archetype
  behaviour belongs in a provider override, never in the root. The reverse move — hoisting a
  provider override into `GenericProvider` when it is provably generic — is correct and has
  already been done for the ritual/reducer/restrictor tiers.
- **Do not touch anything under `references/` except to commit it.** Those are hand-played
  ground-truth games. No `checkout`/`restore`/`reset`/`clean`, no overwrite, no delete. Reverting
  a re-saved reference has already destroyed unrecoverable user work once.
- **Do not re-run the suite to regenerate ground truth.** Inspect the run, then
  `test/regression.sh <mode> --accept`.
- **Do not re-litigate these measured dead ends:** the enumeration-feasibility leaf probe
  (nothing in the rollout was slow), demand-capping land colours (0/8 decks), splitting every
  land signature rather than promoting the dominant land (+61 % on slivers for zero quality),
  agreed-post-hybrid placement for breakpoint waves. Each is written up in `docs/design/`.
- **Do not add a precompiled header** as the answer to B4. Fix the structure.
- **Do not batch Tier B/C commits.** One unit, one digest check, one commit.

---

## Appendix: measured inventory (branch `phase-1-2-deck-analyzer`, `e97c85a`, 2026-07-30)

```
C++ source (src/, excl. deps)      53,892 LOC across 60 files
  src/ai/TurnSolver.cpp            13,501   115 getenv sites
  src/core/SpellEffects.h           4,722   163 inline fns, included by 13 TUs
  src/ai/AIEngine.cpp               4,002
  src/analyzer/ExhaustiveKeep.cpp   3,631
  src/main.cpp                      3,397   18 Write*DecisionJson
  src/ai/DecisionProviders.cpp      2,648
Env flags                          290 distinct / 310 getenv sites / 64 src-only
  truthiness: 88 presence-only, 41 absence-only, 17 value-aware
"lockstep|mirror|keep in sync"     376 comment occurrences in src/
Functions > 300 LOC                18      (> 500 LOC: 11)
scripts/                           85 files: ~11 live, ~40 docs-only, 12 orphans
test/                              38 .sh (8 untracked), 0 unit tests, 0 add_test
docs/design/                       105 files, 1.5 MB, no index
Build trees on disk                build/ 1.2 G + build-{asan,prof,tsan}/ 1.1 G; logs/ 20 G
Single-TU compile (-O3)            TurnSolver 19.2 s / 730 MB; AIEngine 9.1 s; GameEngine 6.4 s
Default build config               Debug (-O0) — see A1
```

Regenerate any of these with the greps quoted inline above; they are all one-liners.
