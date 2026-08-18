# Making a SHARED screening apparatus affordable (without dropping R)

**Status:** plan, 2026-08-18. Deferred work — nothing here is implemented yet.
Companion to [[divergence-analysis-step]] and the deck-screening skill.

## Why a shared table, and why it costs so much

Measured on the Mirrorwing base-vs-trick-suite comparison (40,000 paired games, each arm on
its own R=10 table):

| | |
|---|---|
| paired effect (trick − base) | −0.0094 ± 0.0053 |
| apparatus floor (two tables differing only in seed) | 0.0104 |
| **verdict** | **unresolved — the floor exceeds the effect** |

The cause, isolated on hands containing **no swapped card** (both arms looking at literally
identical cards, so any difference is the table alone):

| decision on identical cards | disagreement |
|---|---|
| keep vs mulligan | 13.7% (134 / 975) |
| what to bottom, both having kept | 56.9% (157 / 276) |

72.4% of all win-turn divergences begin at a different mulligan decision, before a card is
played. The bottoming number is the worst of it because adaptive bottoming leaves unrefined
sub-cells at **floor R=2**, and a 2-rollout estimate is near a coin flip.

One shared table removes this by construction — the sidecar stores a baked policy, so the
same cell yields the same decision for both arms. The user's invariant then holds by design:
same indexed cards ⇒ same keep ⇒ same win turn (measured 0 / 151 divergences, 0.00%).

**The price** is a union table: 71 cards, K=19, 419,918 cells, ~9 h at cap R=10 / floor R=2.
Reducing R is the obvious lever and the wrong one — noise is exactly what we are buying our
way out of (user, 2026-08-18: "reducing R below 10 isn't totally ideal").

## 1. Reachability pruning — 31.7%, measured, no quality cost

**The single biggest win.** A union cell is a 7-card multiset over the union's buckets. A cell
needing ≥1 Ancestral Anger *and* ≥1 Fortifying Draught is drawable by **neither** arm, because
no shipped decklist holds both. Computed against the real generated bucket set (reproduces the
generator's cell count exactly):

```
cells over 19 buckets      419,918
  reachable by base        202,878
  reachable by trick       144,630
  reachable by EITHER      286,714  (68.3%)
  UNREACHABLE by both      133,204  (31.7%)
```

So ~1/3 of a 9 h run is spent on hands that cannot occur. Pruning is ~9 h → ~6 h **with no R
reduction and no loss to either arm** — the pruned cells are unreachable, not approximated.

*What it needs:* the generator must take the per-arm bucket ceilings (not just the union's) and
skip any cell exceeding all of them. `keepstore.py`'s `emit_arm` already does exactly this
filtering on the EMIT side; this moves it to the GENERATE side, which is where the hours are.

*Caveat to check first:* confirm nothing downstream (hand weights, `D_opt`, the bottoming
argmin) reads an unreachable cell — a cell with weight 0 should never be consulted, but that
must be asserted, not assumed, before the cells stop existing.

## 2. Make the pool table a durable artifact, not scratch

Today `deck_compare.py` writes the pool table under `logs/deckcmp/.../pooltable/` and it is
effectively thrown away — the fingerprint (`.pooltable.<name>.json`) only guards reuse within
the same spec. A table keyed on **(union card set, play_digest, K, cap R)** and stored per POOL
would be reused by every later screen over that pool. The hours are a property of the pool, not
of the question being asked of it.

## 3. Incremental extension when an option is ADDED

The user's standing requirement: new comparison work must exploit existing comparison work.

Adding card X to an established pool invalidates only cells whose composition includes X's
bucket; the X-free subspace is untouched. The machinery already exists — `ExhaustiveKeep.cpp`'s
AUTO-ATTRIB does exactly this triage:

1. `play_digest` unchanged → reuse the ENTIRE prior pool;
2. digest changed, `engine_fp` matches, some card DEFS changed → card-level attribution;
3. `engine_fp` differs → engine-level invalidation, full regen.

What is missing is a *deck-level* case: same engine, same digest, pool GREW. That should behave
like (2) — reuse every cell not touching the new bucket.

**Sharp edge:** growth is superlinear in distinct options. Cells scale ~C(K+6,7), so one extra
bucket costs roughly **+35%** (C(25,7)=480,700 → C(26,7)=657,800). This argues for screening in
small pools rather than one mega-pool, and it is the honest counter-argument to "just put every
candidate in one union".

## 4. Two-machine split

`mulligan-profile.md` already documents the multi-machine handoff — parity fingerprints,
determinism handshake, seed allocation, merge. It halves wall clock and is fully designed and
unused for screening apparatus. Combined with §1 this is ~9 h → ~3 h.

## 5. Keep K down deliberately

Equivalence discovery merged **Expedite + Impolite Entrance** into one bucket (they are
parameter-identical under this engine: trample unmodelled because the goldfish never blocks,
and sorcery-vs-instant unmodelled because casts resolve in MAIN_1). That merge is free cells.
Where two candidates are behaviourally identical to the engine, the table already knows it —
but it is also a warning that the screen **cannot distinguish them**, which belongs in the
report rather than in a delta.

## What NOT to try

**Seeding the union table from the base deck's shipped R=40 cells.** Tempting — base's 202,878
reachable cells are exactly its shipped table's cell set, already generated at high R. It is
invalid: a cell's value is estimated on the whole library, and the union deck is 71 cards, not
60. The cells share a *key*, not a *value*.

**Matched synthetic seeds across arms.** Considered and rejected: the noise draw is keyed on
the cell's bucket-count VECTOR, and the two arms' bucket orderings are unrelated (measured: 0 of
13 shared cards at the same index — base reverse-alphabetical, trick alphabetical). Even after
canonicalizing the ordering it cannot work, because common random numbers only cancel noise
both sides DRAW: base's table is a reconstruction from R=40 truth while the trick arm's is a
real R=10 sample whose noise is already baked in.

## Contingency, if a shared table still leaves mulligans far apart

Force both arms onto the SAME keep decision, alternating which arm's table supplies it by game
parity (user, 2026-08-18). Cancels apparatus bias to first order.

*What it gives up, and it is not small:* it also erases LEGITIMATE mulligan differences. If a
card genuinely makes more opening hands keepable, that value disappears from the measurement.
It answers "which suite plays better from the same hands", which is narrower than "which suite
is better" — fine as a diagnostic, misleading as a headline.

*What it needs:* `--choices` already carries mulligan keeps and London bottoming picks
(`main.cpp:903`, `main.cpp:965`) but only as a single-game claude-play replay stream. A batch
route needs a donor-decisions file keyed by game index. The donor data is already produced —
`--game-trace-dir` traces record `kept` and `bottomed` card numbers per attempt.
