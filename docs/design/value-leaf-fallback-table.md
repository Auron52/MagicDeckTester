# Value-leaf × heuristic depth table → runtime fallback crossover

**Status:** in progress (2026-07-16). Core shipped for the 5 non-Hinata decks (metadata + engine + A/B).
Hinata measurement and final GT rebaseline pending. Several deferred extensions recorded at the end.
**PARKED (2026-07-17):** all value-leaf NUMBER generation waits on the real per-deck mulligan profiles (see
"DEFERRED" section) — profiles are generating on the secondary machine, slowly. The design worked out this
session (trust rule, light/heavy play-target policy, estimate schema, per-deck generation policy, Hinata
trust≈6) is recorded below so the resume is clean.

## ISOLATION FINDING (2026-07-17): the crossover is play-NEUTRAL on its own

Tested the crossover TABLE in isolation (fixed binary with the escalation **fresh-budget default OFF** —
see `escalation-refactor-drift.md`), paired default(table) vs `MTG_VALUE_TRUST_OFFSET=3` (uniform committed-3):

| deck | crossover vs uniform, fresh-budget OFF |
|------|----------------------------------------|
| burn, knights, TH, hinata | **inert** — `default == uniform == GT`, both regression seeds |
| antilife | **fires (c=4) but WASH** — 20-seed paired mean delta **+0.0016 LP** (SE≈0.0025, <1σ; 8 better / 6 worse / 6 same). The alarming single-seed s3003 (+0.029) is an outlier. |
| slivers  | **inert** — the DERIVED `c3=2` (override removed) `== uniform`, delta `0.00000` on 16 seeds × {d3,d5}. |

**Consequences:**
1. **The crossover tables do nothing measurable in isolation.** Every earlier "effect" — hinata *help*, slivers
   `+0.0004` and antilife `+0.029` *regressions* — was **entangled with the escalation fresh-budget drift**, not
   the table. The table's cells only differ from uniform at committed `c≥4`, and only *fire* when `hcommitted`
   reaches those depths, which needs the fresh (deeper) escalation budget. Fresh-off ⇒ `hcommitted≈1` ⇒ inert.
2. **Shipping the crossover in isolation is byte-identical to the uniform-3 rule (== GT).** It is a principled,
   data-backed, documented replacement of the `kValueTrustOffset=3` magic constant — zero play change today,
   load-bearing only at deeper committed depths / with the fresh budget.
3. **The slivers `c=3` override is unnecessary in isolation** (it overrides a cell that already `== uniform`).
   Per the "don't override neutral cases / don't skew the pure table" principle it should be **dropped** unless
   the fresh-budget is adopted (where it was originally measured to help). The override belongs *with* the
   fresh-budget feature (`escalation-refactor-drift.md` option 2), not the isolated table.

### DECISION (2026-07-17): ship the pure table, drop the override, keep the mechanism
User chose to **ship the crossover as a byte-identical-to-GT principled replacement** of `kValueTrustOffset=3`,
and to **drop the slivers `c=3` override** (only override when we *absolutely* need it — the user is confident
that need will recur, e.g. once fresh-budget is adopted). Done:
- `decks/slivers_vial/slivers_vial.value.json` `value_fallback_crossover` is now the **pure derived table**
  `[1,1,2,3,3,9,9,9]` (dropped `derived_take_heuristic_at_hdepth` + `manual_overrides`; same shape a no-override
  deck gets from the writer). Reproduces GT exactly (d5 s2002 4.22333, d3 s2002 4.2175).
- The override **mechanism stays**: `valueleaf_table_to_metadata.py` still reads/preserves a
  `manual_overrides` block from the profile (conditional `if manual:` at ~L282), and the engine reads the
  applied `take_heuristic_at_hdepth`. To use it later: add a `manual_overrides` block to a deck's `.value.json`;
  re-finalize preserves it and re-emits `derived_take_heuristic_at_hdepth`.
- Fresh-budget stays OFF/opt-in; cross+fresh evaluated together later (option 2). Feature A is now commit-clean
  and byte-identical to GT across the regression suite (no rebaseline).

Scripts: `scripts/xover_wide_ab.py` (paired multi-seed table-vs-uniform), `scripts/slivers_recheck.py`
(temporarily restores the derived cell, backup/restore in `finally`). Logs in `logs/eval/xover_*`,
`logs/eval/slivers_recheck.log`. Binary: `build_xover/Release/mtg` (fresh-budget default off).

## The bug this fixes

The value-leaf hybrid falls back from a cheap-but-weak value-leaf line to a heuristic search when the leaf
line is committed below a "trust depth." Historically that fall-back used a **single uniform assumption**
(`value-leaf-d(c) ≈ heuristic-d(c−3)`, `kValueTrustOffset=3`) and a per-model scalar `value_trust_depth`.

The scalar was *derived* from a measured **value-leaf × heuristic depth table** (`scripts/valueleaf_depth_matrix.py`),
but that table lived only in **gitignored `logs/eval/*.txt`** — and `write_trust()` collapsed it to the one
scalar before writing the model. So: the table was never in the metadata, was per-machine (logs aren't
committed), and **Hinata was never measured at all** (absent from the generator's `DECKS` and every log).
The user had asked for the full table to live in the metadata; its absence was the bug.

## The design: the table *defines* the fallback at runtime

Each `<deck>.value.json` now carries:

- **`value_leaf_table`** — the full by-depth table: `hdepths`+`heuristic_lp`, `vdepths`+`value_leaf_lp`,
  `h_conv`, provenance (`games`/`seeds`/`value_min_depth`), `monotonicity_warnings`, and the derivation params.
  LP = loss-penalised avg win turn (loss = max_turns+1, lower = better).
- **`value_fallback_crossover`** — the "simpler matrix" the engine actually reads: a per-committed-depth vector
  `take_heuristic_at_hdepth[c] = hc*[c] = min{hc : H_hc < V_c}` (the shallowest heuristic depth that BEATS the
  leaf committed at depth `c`). Uses a **monotone (non-increasing) envelope** of `H` to guard LP noise.

**Runtime take-crossover** (`FullSearchLineHybrid`, `src/ai/TurnSolver.cpp`): after escalating a value-leaf line
committed at depth `c` to a heuristic that reached `hcommitted`, **take the heuristic iff
`hcommitted ≥ hc*[clamp(c)]`**, else keep the leaf. This replaces the uniform offset-3 — each committed depth
gets its **own measured fall-back level** (weak-leaf decks fall back sooner; strong-leaf decks keep the leaf
longer). `hc*=1` ⇒ always fall back; `hc* > maxH` ⇒ never fall back (both emergent from the table). The env
`MTG_VALUE_TRUST_OFFSET` forces the legacy uniform path (used as the A/B baseline).

Wiring: `MulliganProfile::value_fallback_take_at` (+`FallbackTakeAt`), parsed in `MulliganProfileIO.h`
(unknown keys are tolerated; missing → empty → legacy path), passed through `AIEngine.cpp`. Empty vector ⇒
byte-identical to the old offset behaviour.

### Per-deck crossovers (from the committed 5-deck pure matrix)

| deck | c1 | c2 | c3 | c4 | c5 | vs offset-3 (`c−2`) |
|---|---|---|---|---|---|---|
| antilife (weak) | 1 | 1 | 1 | **1** | 3 | falls back sooner at c4 (H1 beats V4) |
| slivers (strong) | 1 | 1 | **2** | **3** | 3 | keeps leaf longer at c3/c4 |
| TH | 1 | 1 | 1 | 2 | 3 | ≈ offset-3 |
| burn | 1 | 1 | 1 | 2 | **2** | falls back sooner at c5 |
| knights (strong) | 1 | 1 | 1 | 2 | **6** | **never** falls back at c5 (V5=H5) |

## Games ∝ decision-importance ("pay unless expensive AND doesn't matter")

A cell's game budget is set by whether it can flip a fallback decision, not by cost alone:

- A cell **matters** if it sits near an `H≈V` **crossing** (the keep/take decision there is close). Multiple
  cells can matter for one `c` (e.g. the `V5>H2` boundary), so the writer considers the whole near-crossing
  region, not just the first crossing.
- A deep cell that has **converged** with its shallower neighbour (`|H_hc−H_{hc-1}| ≤ conv_eps`) is redundant →
  **exempt** (the sanctioned cut).
- **Pay full games** for cells that matter, cost regardless (a slow Slivers still fully trains H5, because its
  strong leaf puts the crossing at deep H). **Cut** only expensive-and-irrelevant cells (Hinata H4/H5, past a
  shallow crossing for a weak leaf).
- The writer's `training_adequacy` **warns** iff a MATTERS (non-converged) cell — or the always-decisive value
  arm — is under-trained. That warning means "go pay for it," not "substitute a default."
- Tooling: generator has `--hgames`/`--vgames` and per-depth `--hgames-depth` (cut only the slow, irrelevant
  cells); writer merges an incremental **value-first pass then a targeted heuristic pass** (per-cell games
  tracked); a **monotonicity flag** surfaces any deeper-is-worse cell (noise vs. search pathology).

**Method:** measure the cheap full **value table first** to locate the crossings, then pay for only the H cells
that matter.

### The H budget is derived from the V breakpoints × cost (2026-07-17)

Do **not** codify a fixed H-depth schedule. The one general asymmetry is that **V is the decision-setting side,
H is where we compromise** — but *how* we compromise on H is computed, per deck, from the data:

- **Value arm — fill V generously at every depth, cost regardless.** V is measured *first* and *fully*: it
  gives the **V breakpoints** (the `V_c` levels that each committed depth's fallback decision is compared
  against) and confirms leaf convergence (`V5≈V6≈V7≈V8`). Even when V explodes with depth (Hinata's combo
  search tree: V6 ≈ 9 min/cell vs V5 ≈ 3 min, V7/V8 far worse) we still pay — *(user approved the multi-hour
  Hinata deep-V run)*. The value arm's cost is the **search tree**, not the O(1) leaf, so "value is cheap"
  holds only at shallow depth / small trees, not for combo decks at deep `d`.

- **Heuristic arm — importance FIRST, cost second.** Budget each H cell in two ordered steps:
  1. **Importance (primary gate).** From the full V table, a cell `H_hc` **matters** iff it sits near a
     crossing with some `V_c` (i.e. `H_hc ≈ V_c` for a committed depth `c` in play range) — that's where the
     keep/take decision flips. **Every cell that matters gets full games, cost regardless** (a slow crossing
     cell is still fully trained — you pay because it matters).
  2. **Cost (secondary, applies only to cells that DON'T matter).** Among the far-from-crossing cells, skimp —
     and skimp *harder* the more **expensive** (time to generate) the cell is. A cheap irrelevant cell can
     still get full games (no reason not to); an expensive irrelevant cell gets a token sample just to confirm
     the plateau. Cost never *reduces* a mattering cell's budget; it only decides how little to spend on the
     rest.
  3. **H8 is always inferred** — never measured, anchored to V8 (proven `H8=V8`). The monotone-H envelope
     guards the lightly-sampled deep cells from producing a false crossing.

This makes the schedule **emergent from each deck's V curve**, not fixed:

- **Weak-leaf deck (Hinata):** heuristic dominates → crossings at **low H** (V5≈H2) → the method yields "fill
  H1–3, skimp deep H." That graduated shape is the *output* for Hinata, not a rule to reuse.
- **Strong-leaf deck (Slivers, Knights):** leaf is competitive → crossings at **deep H** → the method yields
  "fully train deep H" (a slow Slivers still fully trains H5). Applying Hinata's shape here would be wrong.

The 5 non-Hinata decks are already measured full at d1–5 (4 seeds × 1000g) and keep that; the method governs
Hinata (measured fresh) and any deep extension.

## The V8 = H8 anchor (at the horizon, H and V coincide)

At `d = max_turns` both arms search to game-end, so their leaves land on decided states and the leaf estimate
becomes exact: **H8 = V8 = the true game value**. Consequences:

- A crossover **always exists by d8** (`H8 = V8 ≤ V_c` for all `c ≤ 8`), so "never fall back" in a d≤5 table is
  really "the crossing is beyond the *measured* range but guaranteed ≤ 8." Safe for shipped play (escalation
  `hcommitted` ≤ search depth ≤ 5, so we never reach the region where the deep crossing would flip it).
- **Proven empirically (burn, 2 seeds/500g):** `H8 = V8 = 4.3260`; heuristic converges by d3, value-leaf by d5
  (so burn's true offset is ~2, not 3). Confirming this on affordable decks makes it a **general rule → we
  never need to measure H8 for any deck** (anchor H8 := V8, which is cheap on the value arm). By convergence,
  once `H_maxh ≈ V8` the whole deep-H tail is pinned, so expensive decks (Hinata) skip deep H entirely.
- **Do not skip the mid-range H (d4–7) entirely** — light-sample them to confirm the plateau holds (catch a
  non-monotonic dip); just don't spend heavy games there.

## depth > table

`hc*[c]` is non-decreasing in `c` (a deeper leaf needs a deeper heuristic to beat it), so for `committed`
beyond the measured max we **clamp to the deepest measured `c`** (`hc*[max]`) — the monotone-safe lower bound
(fall back a hair more readily; never wrongly keep a leaf). Shipped configs are d≤5 = table max, so no
extrapolation occurs in play. Extending the table later is `--hdepths/--vdepths 1..N` + re-run the writer.

## Cost reality (why Hinata is special) — and the BLOCKER (2026-07-16)

Hinata is a combo deck and its **heuristic** is intractable at depth (measured `H3 = 5.71 s/game` at 24 threads
→ ~95 min/1000g/seed; H4/H5 far worse). A naive full-power matrix was launched and **killed**.

**CORRECTED (2026-07-17): NOT a hang — it was contention from orphaned processes.** Earlier this doc claimed the
unbounded pure-leaf search "hangs at depth ≥5." That was a **misdiagnosis**. Measured on a clean box:
- **Single V5 games terminate in ~0.6 s** (games 0–111, seed 8008, all fast; 64 games total = 32 s).
- A **40-game V5 batch (threads=24) = 14.2 s** (0.35 s/game). The value arm is *cheap*.
The apparent "hang" was **CPU contention from my own orphaned `mtg` processes** (repeated `pkill` failures left
zombie runs saturating all cores, so every new run crawled and looked hung). Budgeting the search was proposed
and **rejected — it interferes with correctness** (a budget changes what `V_d` measures). No search fix is
needed.

**The real cost:** the **value arm is fast** (O(1) leaf, no rollout — cheap even on combo games). Only the
**heuristic arm** (H does a rollout to game-end) has a genuine **slow-but-finite tail**: a few combo games have
long rollouts, so e.g. H2 at 200 games ≈ 2 min and when it's down to the stragglers only ~4 cores are busy.
antilife's deep H (d6–8) is the same shape.

**So Hinata IS measurable** — measure value-first (full V1–8, cheap), cost-sample the heuristic (H1/H2/H3), and
**batch the slow heuristic work at the game level (1 thread × many concurrent tasks) so the tail fills all
cores** instead of wasting 20 of them. The H8=V8 anchor still lets deep H be anchored to V.

## Deferred / future

1. **Full V table to d8 for every deck** (cheap on the value arm) — extends the table, captures the anchor,
   confirms leaf convergence (`V5≈V6≈V7≈V8`). In progress for the 5 affordable decks.
2. **d6/d7 (and d8) heuristic** — only if we raise search depth beyond 5. Cost-sample H (monotone envelope
   guards it) but **fully train the value leaf** there (it's the decision-setting side). Never measure H8
   (anchor to V8).
3. **Performance: pure deep value-leaf (V8) vs the escalation hybrid.** V8 reaches optimal quality (V8=H8) at
   the cheap value-arm cost (burn V8 = 1.7 ms/game). If V8 is genuinely faster than the hybrid while matching
   quality, it could *replace* the crossover machinery for decks whose depth-8 search tree stays small (burn
   likely; Hinata's combo tree likely not). Test V8 vs hybrids-of-various-levels on speed at matched quality.
   Relates to the earlier learned-d0 value-leaf-as-policy work.

## Files

- `scripts/valueleaf_depth_matrix.py` — generator (per-deck folders, hinata, `--hgames/--vgames/--hgames-depth`).
- `scripts/valueleaf_table_to_metadata.py` — parse matrix log(s) → derive + write table/crossover/warnings;
  merges value-first + heuristic passes; training-adequacy + monotonicity checks.
- `src/ai/MulliganProfile.h`, `MulliganProfileIO.h`, `TurnSolver.{h,cpp}`, `AIEngine.cpp` — runtime crossover.

## SETTLED DESIGN — deck-specific `value_play` block (2026-07-18, READY TO IMPLEMENT)

Worked out this session with the user; nothing coded yet (deliberately — implement post-compaction). This is the
"fallback logic + budget renewal" pivot: make play settings **per-deck**, driven by a self-contained block in the
deck's file, instead of one global `--depth`/`--budget-ms`.

**Motivation (the two-regime policy).** The value-leaf is 10–25× cheaper than the heuristic at equal depth and, on
light decks, flat-cheap with depth (burn V8=1.7ms vs H8=35ms). So `d_afford(budget)=max{d:V[d]≤budget}`:
- `d_afford ≥ trust` → **light**: lock the trusted depth, play the leaf, never escalate (e.g. burn d6 ≈1.6ms
  beats escalating at d5 ≈29ms — deeper AND ~18× faster, same LP since V6=H6).
- `d_afford < trust` → **heavy**: cap at `d_afford` (d3/d5) + budget-bound + escalate (hinata, antilife).
"Heavy for play" ≠ "heavy to generate" (antilife's per-game cost is bounded; only hinata is heavy to generate).

**Trust rule (final).** `trust = min{d : V[d]−H[d] ≤ 0.001, same depth, both MEASURED cells}`, **uncapped**
(old rule used `V[d]−h_conv` capped at d5 → recorded burn/TH as None). Provisional values under pre-mulligan
numbers: **burn=6, TH=7**, knights/slivers=5. At play depth 5, trust 6/7 ≡ None in play (byte-identical now).

**Storage — a `value_play` block in `<deck>.value.json`** (keeps all value-leaf play config in one file):
- `target_depth` (int, scalar) — THE setting that drives play (light=trust; heavy=`d_afford` for `budget_ms`).
- `budget_ms` (int) — the budget `target_depth` is optimal for. `(target_depth, budget_ms)` = "the optimal setting".
- `leaf_cost_ms[]`, `heur_cost_ms[]` — INFORMATIVE only ("how fast at each level"); for transparency + re-deriving
  `target_depth` if the budget changes. NOT read at runtime.
- `escalation_fresh_frac` (double, default -1=off) — BUDGET RENEWAL, per-deck. Only heavy decks that escalate set it.
- `regime` ("light"|"heavy") — readability tag.

**How it reaches the CLI:** `<deck>.value.json` is AUTO-ATTACHED from the deck folder on every run
(`AttachValueSidecar`, main.cpp:~2148) — not passed separately. `--depth` defaults to 0 (= unset). So the "new
option" is simply the ABSENCE of `--depth`: `mtg decks/burn/burn.cod --seed .. --games ..` (no `--depth`) →
engine uses `value_play.target_depth`/`budget_ms`. `mtg .. --depth 5` with a block present → ERROR (unless
`--ignore-play-profile`). The regression harness keeps `--depth 5` and is unaffected until a deck gets a block.

**Precedence — mutually exclusive (exactly one source of play settings, so there is never doubt what ran):**
| CLI `--depth`/`--budget-ms` | profile `value_play` | result |
|---|---|---|
| given   | absent  | today's behavior (byte-identical) |
| omitted | present | use the file's `target_depth`/`budget_ms` |
| omitted | absent  | default |
| **given** | **present** | **ERROR** — "pass either the profile's play settings or --depth, not both" (even if equal) |

Escape hatch for A/B on a deck that HAS a block: explicit `--ignore-play-profile` (override allowed but never
silent). Rationale: if you leave a stray `--depth` in while expecting the file to drive, you find out instantly.

**Precedence keys on "was depth explicitly provided," across THREE engine-construction paths (not just CLI arg
parse):** (1) single-game `--games` path (main.cpp:~2538 `AIEngine(profile,lookahead_depth,timeout_ms)`);
(2) `--scenario` path (main.cpp:~2199); (3) `--batch` path (BatchRunner.cpp:223 `ai.emplace(job.profile,
job.depth,job.budget_ms)`) — the one the regression harness uses. So the conflict-check/resolution is a small
shared resolver `ResolvePlaySettings(profile, explicit_depth_or_-1, explicit_budget_or_-1, ignore_play)` called
at each site, NOT logic buried in CLI arg parsing. Each site tells the resolver whether depth was explicitly set:
CLI `depth_provided` bool; batch `jspec.contains("depth")`; scenario `j.contains("depth")`. The batch manifest
gains a per-case `"ignore_play_profile"` bool (the batch analogue of `--ignore-play-profile`).

**Regression-harness policy (settled 2026-07-18).** Because every batch case sets an explicit `depth`, once a
deck has a `value_play` block its cases would ERROR. So:
- **d0 and d3 cases KEEP their explicit depth** and pass the ignore flag — batch manifest emits
  `"ignore_play_profile": true` for them (they exist to test *those specific depths*, independent of the play
  policy). regression.sh's manifest generator sets this whenever the deck has a block and the case pins a depth.
- **The d5 regression CASES drop `depth`/`budget_ms` NOW — byte-identical via the default.** Because the
  built-in default value_play is EXACTLY `{d5, budget 20}` = today's d5 gate setting, a d5 case that omits both
  falls to that default and reproduces current GT to the bit. "d5" here = the d5 *regression cases*, not
  depth-5-the-setting. Then adoption needs NO further test edit: when a deck's DERIVED block lands, its
  already-depth-less d5 case automatically switches to the derived policy, and you rebaseline only that one case.
  (This is why Option A — universal default — beats Option B "require explicit depth": B forces a per-deck test
  edit at every adoption; A costs one upfront depth-drop that is byte-identical, then zero per-adoption churn.)

**Mulligan-profile generation settings MAY decouple from regular play (open, 2026-07-18).** Fidelity argues
generation should score keep/bottom decisions under the SAME policy the deck ships with (so the evaluation isn't
measured against a play depth the deck never uses). BUT generation is EXTREMELY expensive (exhaustive bucketed,
many games per bucket), so a deck's regular-play policy (e.g. an enabled `value_play` at target_depth 6, or a
budget-heavy setting) may be intractable to generate under. So we may deliberately use CHEAPER settings for
generation than for regular play. This is a per-deck cost decision, deferred.
- Mechanism (no core-engine change): `value_play` is the REGULAR-play policy only. Generation settings, when
  decoupled, are a SEPARATE optional override the GENERATION SCRIPT reads and passes as explicit
  `--depth/--budget-ms` (+ `--ignore-play-profile` if the deck is enabled, to bypass the lock). The engine
  resolver never sees "generation" — the script constructs the CLI. Record the generation play settings in the
  mulligan raw sidecar (alongside its `commit` fingerprint) so it's never ambiguous what a profile was built
  under.
- Storage option if we want it self-contained in the deck folder: an optional `value_play_mulligan_gen`
  (target_depth/budget) block the generation script consults; absent => fall back to `value_play` (or the d5/20
  default). NOT yet built — add only if we actually adopt decoupled generation.
- Chicken-and-egg caveat (unchanged): a deck's value_play numbers come from value-leaf×heuristic games that
  themselves begin with a mulligan — so the FIRST mulligan profile is generated under the current default play,
  then value_play is derived, then any regeneration uses whatever generation policy we choose (shared or cheaper).

**Universal default vs deck-derived block (settled 2026-07-18).** A new deck has no derived `value_play` yet, so
play needs a sane fallback rather than d0. Ship a BUILT-IN default `value_play = {target_depth: 5, budget_ms: 20,
source: "default"}`. Crucial distinction:
- `source: "default"` (built-in, or a value.json that has no explicit value_play) — `--depth` SILENTLY overrides
  it, NO conflict. This is what keeps the regression harness (every case passes `--depth`) working unchanged.
- `source: "derived"` (written into the deck's value.json by `derive()`) — this is the one that LOCKS against
  `--depth` (ERROR unless `--ignore-play-profile`). Only an adopted deck locks.
The single current-behavior change: a bare `mtg deck --games N` (no `--depth`, no derived block) goes from d0 to
d5/budget-20. All regression d0/d3/d5 cases pass `--depth` explicitly → GT untouched. Rationale: d0 was a lazy
bare default, never the intended play policy; d5/20 matches the gate budgets and is a better "just play it" default.

**Adoption is deliberate + inert-at-start.** No DERIVED `value_play` blocks exist yet → nothing changes. As each deck's
numbers finalize (post-mulligan), add its block; its regression case (still passing `--depth 5`) then ERRORS →
you drop `--depth`, adopt, and rebaseline THAT case. Per-deck, on the user's schedule. GT changes only on that
deliberate switch — the user explicitly expects to move the depth-5 regression items onto this setting.

**Edit points (mostly writer; engine is small):**
- `MulliganProfile.h` — add the `value_play` fields (target_depth, budget_ms, cost vectors, escalation_fresh_frac).
- `MulliganProfileIO.h` — parse the block (absent → today's behavior).
- `AIEngine.cpp:~1413` — where `escalate_below`/`m_lookahead_depth` feed `FullSearchLineHybrid`: add the
  conflict-check (block-present + `--depth` given → error unless `--ignore-profile-play`) and, when the block
  drives, use `target_depth`/`budget_ms`. `--depth` IS the search depth already, so light "lock d6" = target 6.
- `TurnSolver.cpp` `FullSearchLineHybrid` — read `escalation_fresh_frac` from the profile instead of the env-only
  `s_fresh_frac` (budget renewal; default -1/off preserves current).
- writer `derive()` — new trust rule + derive `target_depth`(budget) + emit cost curves + `fresh_frac`.

**Depth and budget are ASYMMETRIC — depth is the POLICY (locked), budget is a RESOURCE KNOB (free override)
(settled 2026-07-18, SUPERSEDES the earlier symmetric rule).** They are different kinds of thing:
- **DEPTH = play policy** (how deep the deck thinks). An ENABLED block OWNS it. `--depth` with an enabled block
  is the guarded/ambiguous case => ERROR unless `--ignore-play-profile`.
- **BUDGET = resource knob** (how long it may think), independent of the policy. `--budget-ms` FREELY overrides
  the block's budget while KEEPING the profile depth — no error. The `[play]` line prints the per-field source
  (`value_play(depth)+cli(budget)`). This is the mechanism for "profile depth + a different budget."
Truth table (enabled block): neither given → profile depth+budget; `--budget-ms N` only → profile depth + N;
`--depth` (±budget) → ERROR unless `--ignore-play-profile`; `--ignore-play-profile` → full manual (CLI + default
0). No enabled block: explicit CLI wins (omitted half → default 0); fully bare → built-in d5/20.
WHY asymmetric (the motivating case): the OVERNIGHT regression is a deep-search bug-net that wants the SHIPPED
depth under a GENEROUS budget (budget-80). With symmetric "both-or-neither" you couldn't express "profile depth +
budget-80" without also re-pinning the depth. Asymmetric lets overnight drop `--depth`, pass `--budget-ms 80` →
burn runs d6/b80 (its shipped depth, deeply searched), every other deck d5/b80 (byte-identical). VERIFIED with a
temp enabled burn block: bare→d6/b20, --budget-ms 80→d6/b80, --depth 3→error, --depth 3 --ignore→d3.
(Note: the H/V table generation passes `--depth D`; once a deck has an ENABLED block that `--depth` errors, so
those runs need `--ignore-play-profile`. Recommendation blocks — enabled:false — never lock, so they're inert.)

**Always PRINT the effective settings (settled 2026-07-18).** The resolved `(depth, budget_ms, source)` must be
printed at each construction site — e.g. `[play] depth=5 budget=20ms source=cli` — so an agent reading the run's
output can always see exactly what ran and WHY (cli / value_play(derived) / value_play(default) /
cli(--ignore-play-profile)). No hidden play state. Print to STDERR (the regression fingerprint parses specific
stdout lines; stderr can't perturb it). The resolver returns the source string for this.

**EMPIRICAL adoption A/B (2026-07-18) — the win is UNBUDGETED-only; at budget-20 it's a WASH.** Measured the
recommended trusted-leaf targets vs the d5 baseline at BOTH operating points (both arms `--ignore-play-profile`,
single-thread, seed 2002/3003):
- **budget-20 (regression operating point):** burn d6 and th d7 give BYTE-IDENTICAL LP to d5 AND same-or-worse
  wall time. The budget caps the search before the depth ceiling (5 vs 7) binds, so the play is literally the
  same. Light decks aren't budget-bound at d5, so the cheap leaf buys nothing here. Adoption = zero benefit.
- **budget-0 (unbudgeted):** IDENTICAL LP (exact parity — trust depth = where V reproduces H's committed line),
  but 4.0× faster for burn (95.8→23.8 ms/game) and 5.8× faster for th (367→63.6 ms/game). This is the real win.
- **CORRECT framing — recommendation (d_trust/UNLIMITED leaf) vs current (d5/budget-20), RIGOROUS (400g/250g x3
  seeds x2 runs, low noise) — the answer is PER-DECK, not a blanket wash:**
  - **burn (d6/unlim) = a REAL WIN:** ~15-20% FASTER (s2002 10.3->8.7, s3003 8.9->7.8, s4004 9.1->7.1 ms/game)
    at EQUAL-or-BETTER quality (LP identical on 2002/3003; s4004 4.29323 < 4.3025). burn wins ~turn 4, so its
    unbudgeted d6-leaf search terminates in ~8 ms — UNDERCUTTING the 20 ms escalation of d5/b20. Genuine free
    speedup.
  - **th (d7/unlim) = NOT a win, a SLOWDOWN:** ~23-73% SLOWER (s2002 19->26, s3003 18.7->23, s4004 15.9->27.6
    ms/game) at neutral quality (LP mixed +-0.01). th is card-advantage control: a bushy tree at d7 unbudgeted
    explodes node count and the cheap leaf can't offset it. (Budgeted d7 = same as d5/b20 = also no win.)
  - **knights/slivers:** already value_trust_depth=5=play depth => adoption is a literal no-op.
  DETERMINANT: does the unbudgeted search at trust-depth terminate cheaply (burn: yes, fast kill) or explore a
  lot (th: no, bushy)? Only decks of the former kind win. (Aside: d5 UNLIMITED heuristic is slower AND worse —
  95.8 ms/game LP 4.4125, the non-monotonic-budget effect; the baseline is d5/b20, not d5-unlimited.)
**Consequence / recommendation:** PER-DECK. burn is a genuine adopt candidate (d6/unlimited: ~15-20% faster,
equal/better quality, LP identical on the regression seeds so likely GT-neutral). th/knights/slivers: do NOT
adopt (th slower; knights/slivers no-op). Blanket "adopt light decks" was WRONG; so was blanket "wash".

**CORRECTION (2026-07-18) — burn wins at ANY budget incl. b20; the win is TRUST not budget.** Re-measured (400g
x2 seeds): burn d6 is INSENSITIVE to budget (b0/b100/b50/b20 all LP 4.32412, ~8.8/8.8/9.0/9.3 ms/game) and
FASTER than d5/b20 (~10.3ms) at EVERY budget. The speedup comes from TRUSTING the leaf at d6 (skip escalation)
on burn's fast-terminating tree, not from unlimited budget. So burn adopts **d6 / budget-20** (keeps the safety
cap, no unlimited-budget risk) + needs `value_trust_depth=6` set alongside target_depth=6 (else it still
escalates at d6). Earlier "d6/b20 = wash" was a misread (10.03 vs 11.52 was already ~13% faster).

**DEPTH-vs-QUALITY sweep (2026-07-18) — trust depth != optimal play depth, but for these decks deeper adds
NOTHING (measured).** Per the principle "the leaf being reliable at trust-d doesn't mean d+1 isn't higher
quality," swept knights/slivers quality at d5/d6/d7 across 8 seeds (4000 games) at BOTH b20 AND high/unlimited
budget (where depth binds): **BYTE-IDENTICAL LP across all depths** (knights 4.3465-4.3475; slivers 4.2235-
4.2263). The committed plan doesn't change with depth — they're converged at d5. The table's V6<V5 hints (slivers
V6=4.203<V5=4.239) are pure-leaf (value_min_depth=0) measurement artifacts on different seeds that do NOT
manifest in actual hybrid play. So knights/slivers/th gain no quality from depth => stay d5/b20. Only burn (fast
aggro, escalation-dominated at d5) benefits, via trust not depth-quality.

**FINAL per-deck verdict (measured):** burn -> d6/b20 (+value_trust_depth=6): ~10-20% faster, identical quality,
likely GT-neutral. th/knights/slivers -> d5/b20 (no quality gain from depth, no safe speed gain). antilife/hinata
-> d5/b20 default until their tables are generated (heavy; possibly the only class where depth buys budget-bound
quality). USER PLAN: set up a value_play block for EVERY deck (self-contained play profile), using the better-of
target where it beats d5/b20 (burn only) and defaulting to d5/b20 otherwise; enable them; harness d5 cases drop
--depth/--budget (block drives), d0/d3 add --ignore-play-profile. Its value is FAST UNBUDGETED play at
heuristic quality (4–6×), which only matters if/when we adopt an unbudgeted (or high-budget) operating point.
knights/slivers are additionally already at trust=5=play depth (adoption is a pure no-op). The place value_play
MIGHT pay at budget-20 is the HEAVY decks (antilife/hinata): there the d5 search IS budget-bound, so a cheaper
leaf could reach deeper within the same 20 ms → a possible quality gain — but their tables aren't ready (antilife
gap-fill + hinata mulligan profile parked). So: keep the scaffold inert+ready; enable nothing now; revisit heavy
decks after generation, or if we move to an unbudgeted operating point. RECOMMENDATION TO USER = do not adopt yet.

**Sequencing:** fallback-target lands before budget-renewal is meaningful (target decides who escalates at all).
All of this is mulligan-INDEPENDENT as *code* (defaults inert); the *values* wait on the parked number generation.
