# Hinata exhaustive mulligan profile — secondary-machine generation recipe

Plan for generating the Hinata2 exhaustive keep+bottom profile as a **multi-day, chunked,
cross-machine** run on the secondary box. Read alongside `.claude/skills/mulligan-profile.md`
(authoritative protocol) and `docs/design/exhaustive-keep-policy.md` (design). This doc is the
Hinata-specific recipe: feasibility, chunk config, seed allocation, pool + R-determination.

Hinata is the **hardest** profile we've attempted (combo deck, expensive rollout lines), so the whole
point is to have every efficiency + correctness piece in place *before* the secondary spends days on it.

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
- `MTG_KEEP_CHECKPOINT_SEC=1800` — crash-safe; a killed chunk resumes from its last checkpoint sidecar.
- `MTG_COMMIT=<frozen hash>` stamped into every sidecar.

Per-chunk cap `<cap>`: size so ONE chunk finishes in a few hours (checkpoint-bounded), e.g. cap 20–30.
Prefer several independent chunks (distinct seeds) over one huge chunk — more resumable, and each is an
independent poolable unit. Effective R accrues as chunks pool.

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
R-sweeps give the prior: keep plateaus ~R40; R30 already ~95%). This prevents paying for R the policy
can't use — the whole reason to determine R empirically rather than defaulting to R=100.

## Validate + adopt (after the pooled R target)

- Keep A/B: `KM_DECK=decks/Hinata2.cod KM_MODE=keep bash test/keepmodel_exhaustive_ab.sh` (vs static).
- Bottoming: `KM_MODE=bottom` **with `MTG_CONFOUND_BOTTOM=1`** (confounded gate) — adopt blind bottoming
  only if it ties/beats lookahead once the peek is removed.
- Install `.gz` at `decks/Hinata2.keepmodel.exhaustive.profile.json.gz`, suite re-baseline (per-game
  audit — expect combo-hand win-turn shifts), commit profile + GT together. Same flow as the antilife
  adoption (commit a43f965).

## Open TODO before handoff
1. Run the feasibility probe → fill K / rate / cost → go-no-go.
2. Confirm Hinata play is FROZEN (Rule 0).
3. Commit + freeze the tooling commit (sumsq-through-merge + MTG_KEEP_OUT_* + r_sweep script), smoke-clean.
4. Confirm per-game d3≈d5 on Hinata (cheap keep A/B) or fall back to d5.
5. Determinism handshake with the secondary on the frozen commit.
