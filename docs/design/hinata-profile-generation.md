# Hinata exhaustive mulligan profile — secondary-machine generation recipe

**Status (updated 2026-09-03): SUPERSEDED** — the run happened and the R=22 profile was adopted
2026-07-25 (22947513); artifacts at `decks/Hinata2/`. The "Open TODO before handoff" list is
closed. Kept as the historical recipe.

Plan for generating the Hinata2 exhaustive keep+bottom profile as a **multi-day, chunked,
cross-machine** run on the secondary box. Read alongside `.claude/skills/mulligan-profile.md`
(authoritative protocol) and `docs/design/exhaustive-keep-policy.md` (design). This doc is the
Hinata-specific recipe: feasibility, chunk config, seed allocation, pool + R-determination.

Hinata is the **hardest** profile we've attempted (combo deck, expensive rollout lines), so the whole
point is to have every efficiency + correctness piece in place *before* the secondary spends days on it.

> **Note (2026-08-08):** the `MTG_KEEP_CHECKPOINT_SEC` periodic-snapshot checkpoint referenced below is
> **retired** — persistence is now the per-cell journal (auto-on, no flag), and crash-resume is "re-run
> the same recipe command." This doc is kept as the historical recipe; see
> `docs/design/continuous-only-keepgen.md` for the current (zero-execution-knob) gen path.

## Pieces status (what makes this efficient + correct)

| piece | mechanism | status |
|---|---|---|
| Adaptive sampling (floor + refine only decision-relevant cells, ~42% fewer rollouts) | `MTG_KEEP_R_FLOOR` | DONE |
| Chunked generation → cumulative merge (pool k chunks → effective R) | `test/keepmodel_r_sweep.sh` + `RunKeepMerge` | DONE |
| Direct per-chunk raw output (skip unneeded per-chunk profile) | `MTG_KEEP_OUT_RAW` / `MTG_KEEP_OUT_PROFILE` (analyzer) | STAGED — commit+freeze before handoff |
| `sumsq` survives the merge (enables synthetic R-sweep / resume from a *pool*) | `RunKeepMerge` sumsq accumulate | STAGED — same commit |
| Crash-safe over days (periodic atomic sidecar checkpoint) | `MTG_KEEP_CHECKPOINT_SEC` (default 1800) | DONE (f4aed17) |
| Combo-line perf (branch-and-bound max-mana gate, TapForCostBacktrack failure-memo, scarcity tap order) | engine | DONE (~3× field) |
| Cross-machine pool integrity (commit / play_digest / bucket_fp / deck_fp / equiv_seed + disjoint seed_base) | `RunKeepMerge` fingerprint gate | DONE |
| Goldfish-inert cards merged by construction (provably never cast ⇒ one bucket; robust vs probe-threshold noise) | `DiscoverEquivalence` inert union | STAGED — same commit; takes Hinata K 21→20 |
| Accurate-R method (find the knee cheaply from an initial pool, no over-generation) | synthetic R-sweep (`logs/antilife_run/synth_raw.py` shape) on the pooled sidecar's `sumsq` | METHOD READY |

## Rule 0 gate — is Hinata play FROZEN? (confirm before days of rollouts)

Generation is commit-bound: any later commit that changes Hinata's rollout play **invalidates every
sidecar**. Before the secondary starts:
- Hinata must be regression-clean (it currently PASSes byte-identical in smoke/regression) AND
- no pending Hinata fidelity/combo-line work expected to land (memory flags a possible "combo-line
  detection part (b)"; confirm it is either done or won't change play_digest).
**Do not start the multi-day run until this is confirmed.**

## FEASIBILITY — PROBED 2026-07-07 (K=21, multi-day but feasible)

Probe (bucketing pinned at d3 to match generation):
```bash
MTG_EQUIV_DISCOVER=1 MTG_EQUIV_PROBES=400 MTG_EQUIV_THRESHOLD=0.01 MTG_EQUIV_DEPTH=3 \
  MTG_EQUIV_BUDGET=10 MTG_EQUIV_SEED=20260701 \
  ./build/Release/mtg-analyze decks/Hinata2.cod --cards-json src/cards/data/cards.json --max-turns 8
```
- **K = 20** after the goldfish-inert auto-merge (probe found 21: {Icy Blast, Memory Lapse, Remand}
  merged but left {Distorting Wake} out on threshold noise; all 4 are `goldfish_inert` = never cast =
  provably equivalent, so the discovery now unions them by construction → one "dead draw" bucket). Highly
  **singleton** deck otherwise — the combo pieces (Magma Opus, Crackle with Power, Sol Ring, Hinata, …)
  are all 1-ofs.
- **hands ≈ C(26,7) = 657k upper bound** (K=20; the inert merge cut ~26% off the K=21 C(27,7)=888k);
  actual enumerated ~**350–450k size-7** (1-of copy limits; cf. antilife K=20 → 366k actual). Total cells
  (sizes 7/6/5/4) ≈ **~600–750k** (≈1.5× size-7).
- **Rate:** Hinata d3/b10 ≈ 0.59 s/game (1 thread) → ~40 rollouts/s on 24 threads.
- **Cost:** dominated by the adaptive **floor pass** (R4 over all cells): ~0.8M cells × 2pd × 4 / 40/s
  ≈ **~2–3 days** just for the floor; refining ambiguous cells toward effective R30–40 adds more → a
  **~5–9 day** chunked run on one box (halve with both machines). Verdict: **feasible but genuinely
  multi-day** — adaptive + d3 are mandatory, and chunking is what makes it survivable/poolable.
- Optional K-reduction lever (evaluate vs fidelity): force-merge the two cantrips {Preordain, Ponder}
  (and/or the {Island, Mountain} basics if color-agnostic in rollouts) to drop K by 1–2 → ~25–40% fewer
  hands. Only if a fidelity check shows the merged cards are rollout-equivalent.

## Chunk configuration (each chunk = one adaptive run, distinct seed)

Defaults to pin (MUST match across machines for a shared `bucket_fp`):
- `MTG_EQUIV_PROBES=400 MTG_EQUIV_THRESHOLD=0.01 MTG_EQUIV_DEPTH=5 MTG_EQUIV_SEED=20260701`
- `MTG_KEEP_MAXMULL=3`, `--max-turns 8`, `--cards-json src/cards/data/cards.json`
- **Generation depth/budget: `--depth 3 --budget-ms 10`** (d3 ≈ d5 on Hinata win-turn 5.82 vs 5.84 in
  GT, at ~2× the speed — the antilife combo-speedup pattern). CONFIRM per-game d3≈d5 on Hinata before
  committing (cheap A/B); fall back to d5/b20 if the keep policy diverges.
- **Adaptive** (`MTG_KEEP_R_FLOOR=4`, `MTG_KEEP_ROLLOUTS=<cap>`) for the ~42% savings. Adaptive chunks
  pool fine (element-wise sum; floor cells stay thin, refined cells deepen) — same as antilife run1+run2.
- `MTG_KEEP_BOTTOMING=1` (default) so bottoming sub-tables are sampled (don't handicap bottoming — the
  antilife keep-only mistake). Validate bottoming later via the confounded A/B.
  *(HISTORICAL: this flag was removed in `ea2d530` and is no longer read. Bottoming sub-tables are
  always sampled and `bottoming_enabled` is always baked true — nothing to set.)*
- `MTG_KEEP_CHECKPOINT_SEC=1800` — crash-safe; a killed chunk resumes from its last checkpoint sidecar.
- `MTG_COMMIT=<frozen hash>` stamped into every sidecar.

Per-chunk cap `<cap>`: size so ONE chunk finishes in a few hours (checkpoint-bounded), e.g. cap 20–30.
Prefer several independent chunks (distinct seeds) over one huge chunk — more resumable, and each is an
independent poolable unit. Effective R accrues as chunks pool.

**Prune junk hands after the first pool (big floor-pass saver).** Most size-7 Hinata hands are confident
mulligans; re-sampling them every chunk is the dominant floor cost. After the first 1–2 chunks pool,
emit a prune-set and feed it to every later chunk so they skip those cells and spend the whole budget on
the near-threshold hands. This is exactly policy-preserving for the frozen cells (see
`docs/design/exhaustive-profile-workflow-deferred.md` §2 — a confident-mull size-7 cell contributes
`min(V,Dopt[1])=Dopt[1]` regardless, so its keep flag and Dopt are unchanged):
```bash
# after pooling chunks 1..k into hinata_pooled.raw.json:
MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS="chunk_1.raw.json,...,chunk_k.raw.json" \
  MTG_KEEP_PRUNE_EMIT=logs/hinata_chunks/pruneset.json MTG_KEEP_PRUNE_EPS=0.005 \
  MTG_MERGE_OUT_RAW=logs/hinata_pooled.raw.json \
  ./build/Release/mtg-analyze decks/Hinata2.cod --cards-json src/cards/data/cards.json
# then every later chunk adds these to the generate line:
#   MTG_KEEP_PRUNE_SET=logs/hinata_chunks/pruneset.json MTG_KEEP_CARRY_MODE=skip
```
Within-pool (same commit) use `MTG_KEEP_CARRY_MODE=skip` — exactly policy-preserving (frozen counts come
from the earlier chunks at merge). The prune-set carries the deck/bucket/equiv fingerprints and is refused
on any mismatch. Re-emit it as the pool deepens (a slightly-higher-R pool freezes a few more cells).

**Never re-run Hinata from scratch (the main payoff).** After the first full pool, KEEP the prune-set. When
you regenerate on a NEW COMMIT (e.g. a play-logic fix), pass it with `MTG_KEEP_CARRY_MODE=verify` (default):
```bash
# re-run after a play fix -- carry last run's confident-mull set, re-sample only the live cells:
MTG_KEEP_EXHAUSTIVE=1 ... MTG_KEEP_R_FLOOR=4 MTG_KEEP_ROLLOUTS=<cap> \
  MTG_KEEP_PRUNE_SET=logs/hinata_chunks/pruneset.json MTG_KEEP_CARRY_MODE=verify \
  ./build/Release/mtg-analyze decks/Hinata2.cod ... --seed <new base>
```
`verify` re-samples carried cells at a reduced floor so the adaptive refiner still catches any hand that
became keepable under the new play logic (curse-safe); `skip` (0 rollouts, assert the carried decision) is
the aggressive option for a play change you're confident doesn't touch those hands. Fingerprints match
(same list; play_digest/commit not gated). The prune-set carries confident **mulls AND keeps** (both are
first-hand m=0 decisions), so a re-run re-samples only the **near-threshold minority** of size-7 hands
(`MTG_KEEP_PRUNE_KEEP=0` for mulls-only). NOT yet carried: the size-6/5/4 **bottoming** sub-tables — those
feed the m≥1 thresholds and the bottom argmin, so a re-run with bottoming still re-pays them (the R-hungry
follow-up in `docs/design/exhaustive-profile-workflow-deferred.md` §2).

```bash
HASH=<frozen>; i=1                # chunk index; seed_base = SECONDARY prefix + i
MTG_KEEP_EXHAUSTIVE=1 \
  MTG_EQUIV_PROBES=400 MTG_EQUIV_THRESHOLD=0.01 MTG_EQUIV_DEPTH=5 MTG_EQUIV_SEED=20260701 \
  MTG_KEEP_ROLLOUTS=30 MTG_KEEP_R_FLOOR=4 MTG_KEEP_MAXMULL=3 MTG_KEEP_BOTTOMING=1 \
  MTG_KEEP_CHECKPOINT_SEC=1800 MTG_COMMIT="$HASH" \
  MTG_KEEP_OUT_RAW=logs/hinata_chunks/chunk_${i}.raw.json MTG_KEEP_OUT_PROFILE= \
  ./build/Release/mtg-analyze decks/Hinata2.cod --cards-json src/cards/data/cards.json \
    --max-turns 8 --depth 3 --budget-ms 10 --seed $((SEED_BASE+i))
```

## Seed allocation (ledger)

- **Primary machine:** seed_base prefix `20260706..` (antilife used 20260706/20260707 — Hinata is a
  different deck_fp so no collision, but keep a distinct prefix for clarity).
- **Secondary machine:** seed_base prefix **`21000000 + i`** for Hinata chunk i. Keep this ledger in the
  doc as chunks are produced. The merge overlap-guard is the backstop, not the plan.

## Determinism handshake (ONCE, before trusting any pool)

Both machines, IDENTICAL tiny config (same seed), confirm identical `bucket_fp` + `deck_fp` and
byte-identical size-7 `V` on the shared seed (Linux/Windows shuffle portability must be re-verified per
pair — see the past Linux shuffle divergence fix):
```bash
MTG_KEEP_EXHAUSTIVE=1 MTG_EQUIV_SEED=20260701 MTG_EQUIV_PROBES=400 MTG_EQUIV_THRESHOLD=0.01 \
  MTG_EQUIV_DEPTH=5 MTG_KEEP_ROLLOUTS=2 MTG_KEEP_MAXMULL=1 MTG_COMMIT=<frozen> \
  MTG_KEEP_OUT_RAW=/tmp/hs.raw.json MTG_KEEP_OUT_PROFILE= \
  ./build/Release/mtg-analyze decks/Hinata2.cod --cards-json src/cards/data/cards.json \
    --max-turns 8 --depth 3 --budget-ms 10 --seed 999
# compare bucket_fp/deck_fp + size-7 V across the two machines; only pool if they match.
```

## Pool + R-determination (no over-generation)

As chunks arrive, pool cumulatively and track the R knee via the **synthetic sweep** (the pooled sidecar
now carries `sumsq`, so the sweep works on the pool directly — no single-run sidecar needed):
```bash
MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS="chunk_1.raw.json,...,chunk_k.raw.json" \
  MTG_MERGE_OUT_PROFILE=logs/hinata_pooled.profile.json MTG_MERGE_OUT_RAW=logs/hinata_pooled.raw.json \
  ./build/Release/mtg-analyze decks/Hinata2.cod --cards-json src/cards/data/cards.json
```
Then synthesize lower-R profiles from the pool and A/B them to find where the win-turn gain flattens
(`test/keepmodel_synth_raw.py <R> <seed> hinata_pooled.raw.json /tmp/hinata_R<k>.raw.json` — sumsq now
travels with the pooled raw, so no separate single-run sidecar is needed; then `MTG_KEEP_MERGE` that into
a profile and A/B `KM_MODE=keep` vs static). Stop generating once the knee is reached (antilife/burn
R-sweeps give the prior: **keep plateaus ~R40; R30 ≈ 95%, R20 ≈ ~90%** of the R→∞ gain). This prevents
paying for R the policy can't use — the whole reason to determine R empirically rather than defaulting to
R=100. **With junk-hand pruning on**, the effective R budget concentrates on the near-threshold cells, so
the live cells reach ~R40 while the total cost stays near an R20–30 run — i.e. Hinata can get essentially
full keep quality for the price its performance allows. Practical target: **R30 keep** (bottoming decided
separately on the confounded gate).

## Validate + adopt (after the pooled R target)

- Keep A/B: `KM_DECK=decks/Hinata2.cod KM_MODE=keep bash test/keepmodel_exhaustive_ab.sh` (vs static).
- Bottoming: `KM_MODE=bottom` **with `MTG_CONFOUND_BOTTOM=1`** (confounded gate) — this is the FAIR test.
  `--lookahead-bottoming` picks what to bottom by peeking at the library (clairvoyance you do NOT have in
  a real game); the confounded arm reshuffles after the bottom decision to remove that peek, so it
  measures the table against the *real* blind alternative. **Expect the table to win here, even at
  R20–30** — a noisy low-R argmin still beats a blind heuristic until R nears 1. (The earlier "low-R
  bottoming loses" was measured against the clairvoyant lookahead, an unavailable baseline; it does NOT
  imply the table's bottoming is bad in play. Antilife adopted blind bottoming on exactly this confounded
  gate, −0.1123t.) So the likely outcome is **bottoming ON** even at the lower R Hinata forces; only ship
  it OFF if the confounded A/B fails to beat blind.
- Install `.gz` at `decks/Hinata2.keepmodel.exhaustive.profile.json.gz`, suite re-baseline (per-game
  audit — expect combo-hand win-turn shifts), commit profile + GT together. Same flow as the antilife
  adoption (commit a43f965).

## Open TODO before handoff
1. Run the feasibility probe → fill K / rate / cost → go-no-go.
2. Confirm Hinata play is FROZEN (Rule 0).
3. Commit + freeze the tooling commit (sumsq-through-merge + MTG_KEEP_OUT_* + r_sweep script), smoke-clean.
4. Confirm per-game d3≈d5 on Hinata (cheap keep A/B) or fall back to d5.
5. Determinism handshake with the secondary on the frozen commit.
