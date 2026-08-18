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

## 6. Seed the union table from base's shipped R=40 cells — the BIGGEST win, and it is BUILDABLE

An earlier draft of this doc listed this under "what not to try", on the argument that a cell's
value is estimated on the whole library and the union deck is 71 cards rather than 60, so the
cells share a key and not a value. **That was wrong** (user pushed back, 2026-08-18) and the
claim is withdrawn. The premise is true; the conclusion does not follow.

The prize, computed against the real bucket set:

```
union cells reachable by either arm   286,714
  ...of which base already has R=40   202,878   (70.8%)
```

~71% of the cells the union table needs already carry a HIGH-R estimate in the shipped base
sidecar — versus the cap-R=10 / floor-R=2 the union run is paying for them now.

**Why the library objection does not block it.** `MTG_KEEP_PRIOR_RAW`'s change-detection carry
already solves exactly this shape: re-sample every cell only THINLY at the floor, compare to the
prior, and spend the refine budget ONLY on cells that actually MOVED; unmoved cells keep the
prior's higher-R value. So the differing library is handled EMPIRICALLY — cells where the 11
extra cards never matter keep base's R=40 number, and cells the swap genuinely disturbs are
re-rolled. The objection assumed every cell moves; the mechanism measures which ones do.

**Matched rollout seeds are what make it work** (user, 2026-08-18: "as long as we match seeds
anyway"). With shared draws, the thin floor estimate and the prior form a PAIRED difference, so
a real move is detectable at floor R=2 — a comparison of two independent R=2 samples never
could be. This is not an optimisation, it is the enabling condition.

**Bucket translation is the missing piece, and the code already names it.** The gate refuses on
`deck_fp`/`K` mismatch with the message "a changed decklist needs bucket translation, not yet
built". Here the translation is a clean injection: base's 17 buckets map 1:1 into the union's 19,
base's Expedite lands in the merged Expedite+Impolite Entrance bucket, and Fortifying Draught /
Luxurious Libation enter at count 0.

**Residual risk:** a false negative — a cell that moved but whose thin re-sample misses it. The
carry is deliberately biased toward MOVED for this reason, and matched seeds tighten it further.

**Bias to keep honest:** values carried from base are fit to BASE's library, so the shared table
becomes asymmetrically fitted (better for base than for trick) where cells are carried rather
than refined, unlike a union-generated table which is symmetrically foreign to both. That is a
bias/variance trade, not a blocker, and it is measurable: high-R a sample of union cells and
compare to base's R=40 for the same cells. Given the floor here is variance-dominated (0.0104,
against an own/foreign fit difference measured at ~0.004t elsewhere), the trade plausibly favours
carrying — but measure it rather than assume it.

## 7. Reuse at the ROLLOUT level, not the cell level — exact, and it removes §6's bias

User, 2026-08-18: *"If we wanted to reuse only the games that don't have access to specific
changed cards even that seems possible. Though we would need to include extra metadata into the
generation."* Both halves are right, and this supersedes §6 where it applies.

§6 carries a cell's *value*, which is an approximation: base's number is fit to base's library, so
carried cells tilt the shared table toward base. §7 instead reuses base's individual ROLLOUTS as
an exact estimator of one COMPONENT of the union's value:

```
V_union(cell) = P(touch)·E[V | touch]   +   P(no touch)·E[V | no touch]
                └── sample fresh ──┘        └── reuse base's R=40 rollouts ──┘
```

Fresh rollouts are then spent only on games that actually draw a changed card — which is exactly
where the new information is — and the rest is already paid for.

### The trap: a touched/untouched FLAG is not enough

"This rollout never drew a changed card" is conditioning on an OUTCOME, and it is not neutral.
The longer a game runs the more cards it draws, and the LESS likely it is to have avoided all of
the new cards. So the no-touch subset over-represents SHORT games — i.e. fast wins — and reusing
it as-is makes every carried cell look optimistically fast.

The correction is a per-rollout weight that depends on how many cards the rollout drew. Base's
rollouts are all no-touch by construction (base holds none of the new cards), so they are an
unbiased sample of base's value; turning them into the union's no-touch component needs each one
weighted by the probability that all of the new cards would have fallen AFTER its k-th draw in a
union shuffle — a closed-form combinatorial factor in (k, |new|, |library|).

### The metadata generation must record

Per rollout, therefore:
- **k — the number of cards drawn** (the load-bearing one; without it the reuse is biased);
- **which changed cards were touched** (drawn or in the opening hand), so the touch/no-touch
  split can be recomputed for a DIFFERENT changed-set later without re-rolling;
- the cell key and pd, which are already recorded.

Recording the touched SET rather than a boolean is what makes the artifact reusable across future
comparisons: a later screen that changes a different card can re-partition the same stored
rollouts instead of regenerating them. That is the standing "new comparison work must exploit
existing comparison work" requirement, satisfied at the rollout level.

### Caveats to settle before building

- Play decisions inside a rollout consult the DECK (e.g. mana/curve heuristics, the provider's
  card-name lists). Verify a base rollout's decisions do not depend on the union's extra cards
  being *present but undrawn*; if any do, the no-touch equivalence is weaker than stated.
- The value-leaf sidecar and the play profile must match across the two, or the reused rollouts
  were played by a different policy.

## What NOT to try

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
