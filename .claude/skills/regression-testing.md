# Regression Testing Skill

Authoritative guide for running the simulator's regression suite, reading its
results, updating ground truth, and A/B-testing changes. Read this before
running any regression, changing the test matrix, adding a deck to the suite, or
rebaselining ground truth.

> **The one rule that keeps getting skipped:** never `--accept` (or commit new
> ground truth) on the basis of an aggregate fingerprint or a plausible
> *explanation* of why it moved. Every ordinary `regression.sh <mode>` run
> auto-prints the per-game audit at the end so the evidence is in front of you (see
> **"Before `--accept`: analyze every changed game"** below). The audit **reports,
> it does not block** — the accept decision is a **human judgement on the NET
> loss-penalized delta** (avg win turn with an unwon game scored `max_turns+1`).
> Review ALL searched-depth (d>0) SLOWER games; a couple of d0 examples suffice. An
> explanation is not a measurement — and for slower games the measurement is
> generated for you by `test/classify_turn_later.sh <mode>`.

The harness lives in `test/`:

| File | Role | Committed? |
|------|------|-----------|
| `regression.sh` | runs a mode, compares to ground truth, and (with `--accept`) promotes a run into ground truth. `--deck=<name>` restricts the run to one deck's cases (safe with `--accept` — updates only that deck's GT). | yes |
| `regression_deck.sh` | runs ONE deck across modes (`regression_deck.sh <deck> [smoke regression overnight]`) — the per-deck counterpart; each mode still prints its audit and is accepted separately | yes |
| `audit_changed_games.py` | **the pre-`--accept` report you must read**: per-game SLOWER / faster / play-changed breakdown split by depth. Metric = loss-penalized avg, so a game going from a win to unwon is not special — it is just the maximal slowdown, folded into SLOWER. REPORT-ONLY (always exits 0); auto-run by `regression.sh` on every run. The accept decision is yours, on the net delta. | yes |
| `classify_turn_later.sh` | auto-classifies each searched-depth SLOWER game (`churn` = recovers at 4x/16x budget vs `PERSISTS` = variance/real), re-running that one game — the generated form of the slowdown classification | yes |
| `explain_game.py` | old-vs-new **per-turn diff** of ONE changed game (kept-hand/draw divergence + aligned lines + classification hint). Called inline by `audit_changed_games.py` for every searched SLOWER game; also runnable standalone. Baseline = `logs/snapshots/<mode>-baseline` (saved by `--accept`) or `--old-bin`. | yes |
| `regression_cases.sh` | the test matrix (deck × depth × seed × games × budget) + deck metadata — **single source of truth** | yes |
| `regression_gt.txt` | ground truth, **aggregate**: `<deck>_<mode>_d<depth>_s<seed>=<avg_win_turn>[/<play_digest>]` (avg = mean turn-to-win, an unwon game scored `max_turns+1`, so a game going from a win to unwon shows up directly as a worse avg). The **play digest** (a per-case fold of per-game decision-stream hashes) makes a play change that keeps the same avg still FAIL — the coarse avg cannot see it. Legacy entries without a digest match on avg alone until re-accepted. | yes |
| `gt_logs/<key>.wins` | ground truth, **per-game**: one `<game_index> <win_turn> <play_digest>` line per game (`-1` = loss; digest = 16 hex chars). Committed old-side baseline for the per-game audit — `--accept` promotes it with `regression_gt.txt`. The digest column is optional to every reader, so pre-digest logs still parse. | yes |
| `TIMINGS.md` | measured per-case wall times; sizing reference for the matrix | yes |
| `viewer_protocol_check.py` | **reference reproducibility gate**, run `--strict` (threaded, seconds) in **regression mode only** (`VPC_ALWAYS=1` forces it in smoke/overnight, `VPC_SKIP=1` skips it): replays each saved `references/<deck>/claude_*.json` by INTENT (content-anchored picks; engine defaults for decision points the ref predates — see `docs/design/reference-intent-replay.md`). `play-drift` (a recorded human line now ends differently) or `ENUM-GAP` (a plan vanished for an identical state) FAILS the suite; `repaired` is informational (re-save via the viewer when convenient); `shuffle-dead`/`mull-drift` are accepted classes and never gate. | yes |
| `results/<mode>.env` | last run's fingerprints (what `--accept` promotes) | no (gitignored) |
| `logs/<mode>/<key>.log` (+ `.err`) | full binary output per case | no (gitignored) |
| `logs/<mode>/wins/<key>.wins` | **this run's** per-game outcomes (same `<index> <win_turn>` format); diff against `gt_logs/<key>.wins` for the per-game audit | no (gitignored) |
| `logs/<mode>/mtg.run` | snapshot of the exact binary that produced this run (`+ .meta` records its git hash/state) | no (gitignored) |
| `regression_result_<mode>.txt` | last run's summary table | no (gitignored) |

All runs are **deterministic and thread-invariant** — same seed + budget ⇒ same
result on any core count. That is what makes ground-truth comparison and A/B
testing valid. Every search run (depth > 0) uses `--lookahead-bottoming`. Build
Release first: `./build.sh` (never raw `cmake` — see CLAUDE.md; a bare `cmake` leaves
`CMAKE_BUILD_TYPE` empty and silently builds at `-O0`).

---

## The three modes

| Mode | Flag | Budget | Cadence | Seeds |
|------|------|--------|---------|-------|
| smoke | `--smoke` | < 15 min | frequently / before every push | 1001 |
| regression | *(default)* | < 45 min | before committing | 2002, 3003 |
| overnight | `--overnight` | < 8 h | while sleeping | 4004, 5005, 6006, 7007 |

Seeds are **disjoint across modes on purpose**, so running more modes covers more
seeds. Budgets are shared across all decks — see "Adding a deck" below.

```bash
bash test/regression.sh --smoke      # fast gate
bash test/regression.sh              # pre-commit
bash test/regression.sh --overnight  # deep sweep (run later, not inline)
```

---

## Reading results

Each line is `STATUS  <key>  exp=<avg>[/<digest>]  got=<avg>[/<digest>]  <wall>s`
(avg = mean turn-to-win, an unwon game scored `max_turns+1`):

- **PASS** — fingerprint matches ground truth.
- **FAIL** — mismatch (a real behaviour change) *or* no output (crash/hang). Open
  `test/logs/<mode>/<key>.log` and `.err` to see what moved: the avg win turn (with
  unwon games folded in at `max_turns+1`) or the per-game outcomes.
- **NEW** — no ground-truth key yet (e.g. first overnight run before it is
  accepted). Does **not** fail the suite.

**Per-game audit needs NO re-run** — the old-side baseline is already committed.
To see *which* games changed (the per-game moves the aggregate hides), diff the
committed per-game GT against this run directly:

```bash
diff <(sort -n test/gt_logs/<key>.wins) \
     <(sort -n test/logs/<mode>/wins/<key>.wins)   # < = committed GT, > = this run
```

That reveals the exact per-game moves a fingerprint conceals — e.g. a net `−0.2`
avg can be **26 games slower vs 15 faster** (some all the way to/from unwon), which
a single number never shows.

**Re-run policy (minimize it):**
- **Never re-run the whole case or suite to learn "what changed vs GT"** — the
  committed `.wins` already hold every per-game outcome; it's a file diff.
- When you genuinely need *decision detail* for a flip (which keep/play differed),
  re-run **only that one changed game** — `--log-dir` (or `MTG_DUMP_WINS`) on its
  exact seed (`base + gi`), one at a time — never the surrounding thousands. The
  committed `.wins` is **outcome-only** (`<index> <win_turn>`, ~6 B/game); the full
  per-game trace (`--log-dir`, ~38 KB/game — turns/casts/reveals/board) is too bulky
  to commit across the matrix. Determinism makes that one-game regen an **exact**
  reproduction, so storing traces would buy nothing over regenerating the single
  game you care about.
- **If the committed logs lack what an audit needs, EXPAND what the harness
  captures** (add the datum to the `.wins`/snapshot it records on every run) rather
  than defaulting to a re-run. Re-running is the last resort, only when we truly
  can't reconstruct what happened from committed logs.
- The one legitimate full A/B re-run is *isolating a code change* from a stale
  baseline (the `MTG_DUMP_WINS` A/B below, rules 5–6) — a different question from
  "what did this run change vs its GT."

The fingerprint is the **loss-penalized `avg_win_turn`** (an unwon game scored
`max_turns+1`), so a game going from a win to unwon moves the avg directly — no
separate win-count is needed (this matters for Treasure Hunt, which does not always
win: ~95% at depth 0, 100% under lookahead). Always glance at the `wall` column — a
sudden slowdown is the cheapest early warning of a perf regression even when the
metric is unchanged.

Exit code: 0 = all pass (NEW allowed), 1 = any FAIL.

---

## Updating ground truth (the accept flow)

Ground truth is **never hand-edited and never regenerated by re-running**. A
normal run already records every fingerprint to `test/results/<mode>.env`. After
you have inspected a run and decided its numbers are correct, promote them:

```bash
bash test/regression.sh --smoke --accept       # promote the last smoke run
bash test/regression.sh --accept               # promote the last regression run
bash test/regression.sh --overnight --accept   # promote the last overnight run
```

`--accept` does **not** run the binary. It merges `results/<mode>.env` into
`regression_gt.txt` in canonical order, touching only the accepted mode and
leaving the others intact (so accepting smoke never disturbs overnight values).
Commit `regression_gt.txt` together with the code/profile change that justified
the new numbers.

Typical rebaseline cycle after a deliberate change:
`run → inspect FAILs/deltas → analyze EVERY changed game per-game → --accept → (optional) re-run to confirm all PASS`.
The per-game analysis comes **before** `--accept`, always.

### Before `--accept`: analyze every changed game (the discipline)

This is the step that most often gets skipped — so **the harness surfaces it for
you** on every run. `--accept` does not block (the audit is report-only), so the
discipline is on you: do not `--accept` (or commit new ground truth) until you have
diffed the individual games that changed and explained each one. A plausible-sounding
*narrative* for an aggregate shift is NOT a substitute for measuring it per game.
The accept decision is a human judgement on the **net loss-penalized delta** — a
net-negative avg with every SLOWER game understood, not a top-line number that "looks fine."

**The harness runs the audit automatically — you read and act on it.** Four pieces:

1. **Every `regression.sh <mode>` run** prints the per-game audit at the end (the
   split-by-depth SLOWER / faster / play-changed breakdown + each searched-depth
   SLOWER game), so you see it without a separate step.
2. **The audit prints an inline old-vs-new per-turn DIFF for every searched SLOWER
   game** — the exact thing you must inspect, already at your fingertips (no separate
   `--log-dir` step). Each block shows: the win-turn change (or a win becoming
   unwon, the maximal slowdown); whether the **kept hand** or the **draws diverged**
   (→ a physically different game, not like-for-like); the aligned per-turn line
   (lands / spells + targets / attacks / opponent life); and the classification hint.
   It needs the baseline binary saved by the last `--accept`
   (`logs/snapshots/<mode>-baseline`); without it the block shows the NEW line only +
   how to get a baseline.
3. **`regression.sh <mode> --accept` promotes the last run** and snapshots the
   accepted binary to `logs/snapshots/<mode>-baseline` so the NEXT run's audit can
   diff against it. It does **not** re-run the games or re-run the audit — inspect
   the audit in the RUN output first, decide there, then `--accept`. The optional
   `--accept-with-regressions="gi<N>:<reason>; ..."` records WHY an intended
   slowdown was accepted into the ground-truth provenance header — documentation,
   not a gate.
4. **`test/classify_turn_later.sh <mode>` classifies every searched SLOWER game
   for you**, re-running each at 4x and 16x its case budget: `churn` (recovers →
   benign search-truncation) vs `PERSISTS` (draw-divergence variance if the deck
   shuffles/fetches, else a real same-draws slowdown).

   > **`PERSISTS` does NOT mean "real regression".** It re-runs only the NEW arm, so it
   > cannot see the case where the OLD side's faster win was itself budget luck. Measured
   > 2026-08-01: `goblins_smoke_d5_s1001` gi43 went T5 → T6 and classified `PERSISTS` at 4x
   > and 16x — but the *old* arm also becomes T6 at 4x and stays T6 at **unlimited** budget,
   > so the baseline was the artifact, not the change. **Before calling a searched slowdown
   > real, re-run BOTH arms at `--budget-ms 0`.** Note `0` means unlimited — a large number
   > does not; the budget is a deterministic virtual work-unit count
   > (`SearchBudget::NODES_PER_VIRTUAL_MS`), and `<= 0` disables the limit.

You can also run the raw audit / a single-game diff directly (what the harness invokes):

```bash
python3 test/audit_changed_games.py <mode>          # smoke | regression | overnight
python3 test/audit_changed_games.py <mode> --old-git   # to re-check an accept that already happened
bash   test/classify_turn_later.sh <mode>           # auto-classify searched turn-later games
python3 test/explain_game.py <mode> <key> <gi>      # old-vs-new per-turn diff of ONE game
        # e.g.  python3 test/explain_game.py regression antilife_regression_d3_s2002 206
        # baseline = logs/snapshots/<mode>-baseline (from the last --accept); or pass
        # --old-bin <snapshot> (see test/snapshot_bin.sh) to diff against any build.
```

> **Two ways `explain_game.py` diagnoses the wrong thing** (both measured 2026-08-01, see
> `docs/design/goblins-value-model-ab.md`):
> - It pins `--depth <case depth> --ignore-play-profile`. That is right for d0/d3, whose manifest
>   jobs pin depth too — but the **d5 job OMITS the depth key on purpose so the deck's `value_play`
>   block owns the depth**. Pinning it bypasses the block and diagnoses a configuration the case
>   never ran; where the arms' blocks differ they may not even run at the same depth. Reproduce a d5
>   case by omitting `--depth` and passing only `--budget-ms`.
> - `--old-bin` resolves the DECK from the current tree. Fine for a code change; useless when the
>   change is a deck-sibling data file (`<deck>.value.json`), because the old binary still picks up
>   the new file and reports "no change". A/B a **copy** of the deck folder instead.

It diffs the committed per-game GT against this run and prints, **split by depth**,
the per-game `slower` / `faster` / `play-changed` counts, listing each searched-depth SLOWER
game individually. A SLOWER game is any worse loss-penalized score — a bigger win turn OR a game
that went from a win to unwon (the maximal slowdown); the two are one bucket. `play-changed` = the
per-game **play digest** moved while the score did NOT — a line change (deliberate or a bug) that
the score diff alone cannot see; the audit prints an inline `explain_game` diff for each so you can
tell which. The audit is **report-only (always exits 0)** — it surfaces what to analyze; the accept
decision is yours on the net delta. (Play-changed games likewise must be analyzed before `--accept`.)

**Review bar differs by depth (this is the part that makes the audit tractable):**

- **Searched depth (d>0) — review ALL of them.** Every SLOWER game must be
  classified as one of: *fetch-shuffle variance* (confirm: the two lines' DRAWS
  diverge — an eval change flipped an early land/fetch tie-break, reshuffling the
  game), or *budget churn* (confirm: the game recovers when you re-run that ONE game
  at a higher budget — a marginal deep line the new search just needs a hair more
  budget to find), or a *real slowdown* (draws identical AND doesn't recover at
  higher budget → a bug). This is where engine quality lives; do not sample it. A
  game that went from a win to unwon is the same analysis — just the maximal slowdown.
- **Depth 0 (greedy, no search) — light touch.** d0 has no lookahead, so its swaps
  are the greedy casting-more / attack-order heuristic churning; they are expected
  and are **not** a quality bar. Spot-check a couple of examples for sanity (no
  crash, the swap is a plausible greedy line) and move on — do **not** root-cause
  every d0 change. The script reports d0 separately.

When a run moves **hundreds or thousands** of games (a deliberate engine/horizon/
baseline shift this mode never exercised — e.g. the overnight first seeing
commit-the-line), per-game reading of all of them is infeasible: use the
**"Auditing a LARGE changed-game set"** workflow below (classify → legality sweep →
out-of-horizon reproduction → in-horizon deep-dive), which satisfies this discipline
at scale. The rules below still bind every game it surfaces.

The rules — all of them, every time:

1. **Analyze BEFORE accepting, never after.** The order is
   `run → per-game diff → explain every delta → --accept → commit`. If you have
   already accepted/committed and *then* go analyze, you did it wrong. `--accept`
   *certifies* the analysis is already done.
2. **Aggregates certify nothing.** A flat `avg_win_turn` routinely hides equal
   improvements and regressions cancelling; a *better* aggregate can still hide a
   game that went from a win to unwon (the maximal slowdown); a *reduced*
   `[nonconv]`/`[fd-diverge]` count can be predictions becoming uniformly more
   pessimistic (**masking**), not a fidelity gain. Never reason "the totals look
   fine/better, so it's safe."
3. **Never explain by inference from another mode or deck.** "Smoke and
   regression improved, so the overnight change is the same kind of thing" is NOT
   allowed — measure the mode you are accepting, on **its own seeds**. Different
   seeds expose different games (in practice a game going from a win to unwon
   appeared at exactly one overnight seed and nowhere in smoke/regression).
4. **Surface the maximal moves explicitly — but they are not a separate category.**
   List every `X → -1` (a win became unwon) and `-1 → X` (unwon became a win). Under the
   metric a win becoming unwon is **just the maximal slowdown** (win turn replaced by the
   `max_turns+1` loss score) and is already inside the average — `audit_changed_games.py`
   says exactly this at the top of the file. So surface them as the loudest per-game
   signal an avg can bury, and use them to hunt bugs, but **decide on the NET
   loss-penalized delta**; do not gate an accept on root-causing each one, and do not
   report them as a distinct harm alongside the average (they would be double-counted).
   A cluster of them at `max_turns`/`max_turns-1` is usually knife-edge games at the
   horizon, each costing only +1 or +2, not a new failure mode.
5. **Diff against the right baseline: the prior committed code (the change you
   are certifying), not the stale ground truth.** When the GT is itself stale
   (e.g. it predates an earlier switch), diffing the new binary against the stale
   GT conflates two changes. To isolate the change under test, A/B the new binary
   vs the immediately-prior committed code.
6. **Verify the two A/B arms actually differ before trusting the diff.** The
   classic trap: if your change is already committed, `git stash` stashes nothing,
   both builds are identical, and a clean "0 changes" diff is *meaningless*. Build
   the baseline by checking out the **parent revision of the changed files**
   (`git checkout <C>~1 -- <files>`), and sanity-check that a config you *expect*
   to move actually shows nonzero diffs before believing any clean result.
7. **Space base seeds by AT LEAST the games-per-job, or your sample silently
   collapses.** A game's identity is `base_seed + game_index`, so a job with base
   `B` and `N` games covers effective seeds `[B, B+N-1]`. Base seeds spaced closer
   than `N` make consecutive jobs **replay the same games**. A 2026-08-01 goblins run
   used bases `100001..100100` at 1000 games/job and believed it had 100,000 games per
   arm; it had **1,099 distinct games, each counted up to 100 times**, which inflated a
   1.3σ result into a reported −14.4σ. Use `base = S0 + i*N`, key the analysis on
   `base+gi` rather than on the job, and assert
   `distinct(base+gi) == sum(games)`. **A zero-variance paired result is the tell** —
   an average over exactly `N` games is an integer turn-sum / `N`, so identical
   per-seed deltas across supposedly independent seeds mean one game seen `k` times.

Recipe (isolates THIS change; works whether or not it is already committed).
**Prefer a `git worktree` over checking files out in place** — `git worktree add /tmp/wt_<sha> <sha>`
then `bash build.sh` inside it gives you a second binary without ever touching the working tree,
which sidesteps the hard rule against `git checkout` over uncommitted work:

```bash
# NEW arm = current HEAD (your change). Dump + normalize per-game win turns:
MTG_DUMP_WINS=1 ./build/Release/mtg <deck> --seed S --games N --depth D \
  --budget-ms B --lookahead-bottoming --threads 1 2>&1 \
  | sed -E 's/.*gi=([0-9]+) wt=(-?[0-9]+).*/\1 \2/' | sort -n > new.txt

# BASELINE arm = committed code WITHOUT your change. If the change is already
# committed as <C>, check out its PARENT for the touched files (NOT git stash —
# that is a no-op on committed files and silently gives you two identical arms):
git checkout <C>~1 -- <changed files>
./build.sh
MTG_DUMP_WINS=1 ./build/Release/mtg <deck> ... --threads 1 2>&1 \
  | sed -E 's/.*gi=([0-9]+) wt=(-?[0-9]+).*/\1 \2/' | sort -n > old.txt
git checkout HEAD -- <changed files>; ./build.sh

# SANITY (rule 6): the arms MUST differ for a config you expect to change.
diff old.txt new.txt | grep -c '^[<>]'        # 0 here on an expected-change config => invalid A/B

# Classify every delta; the games going to/from unwon are the maximal moves (rule 4):
awk 'FNR==NR{o[$1]=$2;next} ($1 in o)&&o[$1]!=$2{
       t=(o[$1]<=0&&$2>0)?"faster(from-unwon)":($2<=0&&o[$1]>0)?"SLOWER(to-unwon)":($2<o[$1])?"faster":"SLOWER";
       print t" gi="$1": "o[$1]" -> "$2}' old.txt new.txt | sort | uniq -c
```

Then reproduce each moved game single-game in BOTH builds
(`--seed <base+gi> --games 1 --game-index <gi> --log-dir <dir>`, or
`MTG_NONCONV_TRACE_SEED=<seed>`) and read the log. Confirm **every** moved game
makes sense: faster games win via a legal, genuinely-better line (not a phantom —
verify mana/targets/zones); slower games — including **every** game that went to
unwon (the maximal slowdown) — are an understood, acceptable consequence (not a
real misplay, rules violation, or
**budget starvation** — confirm starvation by re-running that game at a much
larger `--budget-ms`; if a bigger budget recovers the good line, the change
starved the search). Decks with no relevant cards must stay **byte-identical
PASS** — a FAIL there is collateral damage, not an accept-able delta. If any moved
game fails this, fix the root cause before accepting. Acceptance certifies you
have explained the deltas — not that the top-line number looks unchanged.

---

### Auditing a LARGE changed-game set (a baseline/engine/horizon shift)

When a deliberate engine change that this mode never exercised lands between
baselines — a commit-the-line/full-depth default switch, a horizon change
(`max_turns`), a NODES rebase, dig modeling — a single run can move **hundreds or
thousands** of games. Reading every log is infeasible, but the discipline's *intent*
(understand every change, no hidden regression) still holds. Do it in two passes:
mechanical classification, then a legality sweep over ALL games plus a targeted
deep-dive of the concerning buckets. **All of this happens before `--accept`.**

> `max_turns` does double duty: it is both the rollout/search **horizon** and the
> win-acceptance **cutoff** (`won = 0 < win_turn ≤ max_turns`). A stale GT taken at
> a *larger* horizon records wins past the new cutoff; those become `→ -1` and are
> EXPECTED. `MTG_DUMP_WINS=1` emits `[win] gi=… wt=…` to **stderr** (use `2>&1`).

**Pass 1 — classify every changed game** from the `.wins` logs (this run's
`test/logs/<mode>/wins/<key>.wins` vs `test/gt_logs/<key>.wins`), given horizon H:

```python
# buckets: horizon-cut (gt>H -> -1, expected if H was lowered), improvement
# (new<gt, or gt=-1 & new<=H), slowdown (gt,new<=H, new>gt; still a win),
# IN-HORIZON TO-UNWON (gt in 1..H & new=-1; a win within the horizon now unwon =
# the maximal in-horizon slowdown; the loudest), new-win.
for gi in gt:
    g,n = gt[gi], new.get(gi,-1)
    if g==n: continue
    if g>H and n==-1: bucket='horizon_cut'
    elif 1<=g<=H and n==-1: bucket='IN_HORIZON_TO_UNWON'   # root-cause each
    elif g==-1 and 1<=n<=H: bucket='new_win'
    elif 1<=g<=H and 1<=n<=H and n<g: bucket='faster'
    elif 1<=g<=H and 1<=n<=H and n>g: bucket='slowdown'
    elif g>H and 1<=n<=H: bucket='pulled_into_horizon'
```
Report the bucket counts; they must sum to the total changed and each class must
have a single explained cause. Horizon-cut + faster/pulled-in are expected for a
horizon-lowering + search-improvement change; **the in-horizon-to-unwon bucket is
the loudest — root-cause each before accepting.**

**Pass 2a — legality sweep over ALL games (mandatory).** This is how "improvements
and out-of-horizon results" get checked for an illegal/phantom line without reading
every log — a play line that is illegal or that the executor can't realize shows up
as a harness flag. Re-run the SAME manifest with the oracles on:
```bash
MTG_FLAG_NONCONV=1 MTG_FD_ORACLE=1 ./build/Release/mtg --batch <manifest> \
  --threads N 2>flags.err          # full-depth/commit-the-line is the default
```
- `[nonconv]` MUST be **0**. Any non-convergence is a search inconsistency — root-cause; never accept with `nonconv>0`.
- `[fd-diverge]` = commit-the-line predicted a win earlier than the real game realized it. Categorize by `realized-predicted`: **off-by-one** (realized=predicted+1) is the known minor rollout-optimism — note the count (it must not grow run-over-run); **delta≥2, or predicted-but-never-realized** (realized beyond horizon) is SEVERE → root-cause each. Caveat: the oracle only catches *predicted-but-missed*; a game commit-the-line should have won but **never predicted** is NOT flagged here — Pass 1's in-horizon-to-unwon bucket + Pass 2c catch those.

**Pass 2b — out-of-horizon "worse": reproduce at a lifted horizon.** A
`gt>H → -1` game is benign only if its old win still EXISTS. Re-run those cases at
the GT's original (higher) horizon via a per-job `max_turns` override and diff vs
GT:
```python
m=json.load(open('<mode>/manifest.json'));     # per-job override is honored
for j in m['jobs']: j['max_turns']=20          # the GT's old horizon
json.dump(m, open('<mode>_hi/manifest.json','w'))
```
Games that reproduce their GT win (same turn or earlier) at the lifted horizon are
pure horizon-cut (benign). Games that **do not win even at the lifted horizon** are
real regressions the cut was hiding → root-cause.

**Pass 2c — in-horizon-to-unwon games & severe divergences: per-game + engine A/B.**
For each in-horizon-to-unwon game (and each severe fd-diverge): reproduce single-game,
read the log (the per-game recipe above), and decide whether it is a
**commit-the-line (full-depth) regression** by comparing the default engine to
legacy per-turn search on that exact game:
```bash
# default (commit-the-line):
MTG_DUMP_WINS=1                ./build/Release/mtg <deck> --seed $((base+gi)) --games 1 \
  --depth D --budget-ms B --max-turns H 2>&1 | grep '^\[win\]'
# legacy per-turn re-deciding:
MTG_LEGACY_SEARCH=1 MTG_DUMP_WINS=1 ./build/Release/mtg <deck> --seed $((base+gi)) --games 1 \
  --depth D --budget-ms B --max-turns H 2>&1 | grep '^\[win\]'
```
- **legacy wins, default loses ⇒ commit-the-line regression.** The committed line
  misfires on the realized board, OR — because lookahead-bottoming rolls out with
  the same engine — the bottoming kept a worse hand: **compare the two opening
  hands, they can differ** (a real case: default bottomed a flood-prone double-
  Treasure-Hunt hand and lost; legacy kept a leaner hand and won T4).
- **both lose ⇒ not commit-the-line** (horizon-edge or genuinely unwinnable; the
  horizon-edge cost is ~0.1% at suite budget and shrinks with budget).
- Rule out **budget starvation** either way by re-running at a much larger `--budget-ms`.

**Checklist (clear all before `--accept`):** Pass-1 buckets sum and each has one
explained cause; `nonconv=0`; every severe fd-diverge root-caused (off-by-one
count noted); every out-of-horizon "worse" reproduces at the lifted horizon or is
root-caused; every in-horizon-to-unwon game categorized (commit-the-line regression /
horizon-edge / budget / genuinely unwinnable). A net-positive aggregate NEVER
waives a single unexplained in-horizon-to-unwon game or a `nonconv`.

---

## A/B testing a change (the suite IS the A/B — no separate script)

To measure a change (adding `card_scores` to a profile, a search tweak, …):

1. Ensure the **committed** state is the baseline arm and its ground truth is
   current.
2. Apply the change to the working tree (profile or code); rebuild if code.
3. Run `bash test/regression.sh` (and/or `--smoke`). **FAIL on the affected deck
   is the expected signal**, not an error — read `exp` vs `got`: the avg-win-turn
   delta (lower = better) is the A/B result. Inert/unaffected decks must stay
   **PASS** (byte-identical), confirming no collateral change.
4. For more seeds / tighter numbers, run `--overnight`.
5. If positive and accepted: `--accept` the moved mode(s) and commit code +
   profile + ground truth together. If not, revert the change.

Paired seeds mean the only difference between arms is the change itself, so even
~0.01-turn deltas are signal, not noise — the suite compares the same seed on both
arms by construction.

---

## Adding a deck to the suite

1. Add the deck + profile and get it implemented/analyzed (see `analyze-deck.md`).
2. Add `[<name>]=...` to `DECK_FILE` and `DECK_PROF` in `regression_cases.sh`.
3. Add cases to `SMOKE_CASES` / `REGRESSION_CASES` / `OVERNIGHT_CASES`. Size game
   counts from a quick timing probe (mirror `TIMINGS.md`); keep each **mode**
   within its total budget across **all** decks.
4. If a mode now exceeds budget, **trim the cheaper decks first** (drop a high-
   depth seed) rather than letting the mode overrun.
5. Run each mode, inspect, `--accept`, and commit the new ground truth.

The smoke matrix is also where to pin a few **known-troublesome specific games**
(by seed) as decks reveal them, so the fast gate catches the bugs that bite.
