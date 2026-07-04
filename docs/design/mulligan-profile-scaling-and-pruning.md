# Scaling the exhaustive mulligan profile: adaptive sampling, pruning, and force-merge

**Status:** design, mostly not yet built. Companion to `exhaustive-keep-policy.md` (the base
method) and `play-digest-and-pooling-gate.md` (the pooling gate). This doc captures the
efficiency work needed to run the exhaustive keep/bottom profile on *expensive* decks, plus the
bottoming attribution method and its Slivers result. Self-contained.

## The problem: expensive decks

The exhaustive method evaluates `V[size][comp]` for every bucketed hand at R rollouts *per
play/draw mode* (R is per-mode: the loop is `for pd in {0,1}: for r in R`, so a hand costs 2R
rollouts). Cost is dominated by **K** (bucket count after equivalence merging) and the number of
distinct hands it induces. Measured feasibility (equivalence discovery, 400 probes, thr 0.01,
depth 5):

| deck | distinct cards | K | notes |
|------|----------------|---|-------|
| Slivers | 19 | 10 | redundant (lords, any-colour lands, 1-drops collapse); ~13.8k hands |
| Anti-Lifegain | 23 | 23 | **zero merge** at thr 0.01, but *compressible* (manabase cluster at dist 0.03–0.1) |
| Hinata | 23 | 20 | only 4 counters merge; rest 0.11–0.30 apart → **not** compressible |

Per-game cost is no longer the differentiator (Hinata is ~3× after the max-mana gate, not ~40×);
**K is.** At R=20, Anti-Lifegain (K=23) and Hinata (K=20) are multi-day jobs; the levers below
make them tractable. Slivers-class redundant decks are already cheap.

## Lever 1 — per-deck force-merge (shrink K)

Equivalence discovery merges cards whose CRN win-turn signature is within a threshold. For
**differentiated** decks (fetch/dual manabases) the threshold leaves many near-duplicates
separate. Two facts justify a per-deck **manual force-merge list** layered on the CRN clustering:

- **Keep-equivalence is looser than win-turn-equivalence.** The bucketer measures full-game
  win-turn, which captures *play-level* differences (which exact shock a fetch commits to, one
  life here or there). The keep *decision* only needs "is this hand keepable," a coarser bar. So
  a flexible-fixing family (e.g. 4 fetchlands, distances 0.03–0.1) can be one keep-bucket even
  though win-turn separates them slightly.
- **Functional role ≠ card type.** You cannot classify cards for merging (or for the pruning
  bounds below) by `types`. Worked examples from Slivers: Thrumming Hivepool is `types:[Artifact]`
  but functionally a *threat* (affinity payoff); Aether Vial is `Artifact` but an *enabler*
  (land-like fixing); a mana dork is `Creature` but *mana*. Roles must come from behaviour
  (CRN/objective measurement or explicit human label), never the type line.

Force-merge is human-authored and then validated by the in-game A/B (never trust the merge
blindly). It must be pinned per-deck and shared across machines for pooling parity (it changes
`bucket_fp`).

## Lever 2 — confidence-tiered adaptive sampling (reduce R where it's safe)

Uniform R over all hands is wasteful: only the hands near a decision boundary need high R. The
R=20→R=100 Slivers A/B showed only ~15–33% of hand-types sit near the keep/mulligan threshold,
and R=100 differs from R=20 on just 4.4% of decisions (all near-ties, near-zero EV). So spend
rollouts by *importance*.

Scheme — a per-bucket budget vector, e.g. `[1, 5, 25, 100]`:

1. **User rules, high confidence** → tiny R (≈1). Not to *decide* — to **audit** the rule: a
   sample that contradicts the rule flags "your intuition missed something." Decay toward 0 as a
   rule proves out across runs (mature rules become free).
2. **User rules, medium confidence** → low R (≈5). Same audit role.
3. **No rule, but a small sample is confident** → medium R (≈25), then **sequential early-stop**:
   the generator already stores `se[idx][pd]` (stderr of the mean), so the flip probability is
   ≈ Φ(|mean − threshold| / se). If many SEs clear of the boundary, stop; skip the rest.
4. **Borderline** → full R (≈100).

The fixed vector is a coarse staircase; the real form is **"sample until P(flip against the
threshold) < ε."** That self-governs the floor: a genuinely confident cell converges fast, and if
noise keeps a cell ambiguous the rule refuses to stop — so you can never under-sample into
distortion (the "not too low" risk is prevented by construction).

Subtlety: the keep **threshold is not constant** — it's `mull_value(m+1)`, an aggregate over the
whole next table via backward induction. So "is hand X confident?" is measured against a
threshold that itself has uncertainty. Use a provisional threshold from the low-R pass and one
cheap refinement once the table settles; hands near a *still-moving* threshold get more samples.

Everything is indexed per **(mull-level × play/draw)** cell — the two axes are real and large.
Mull: the keep bar drops as m rises (a 1-land hand is mostly-mulligan at size 7, mostly-keep by
size 5). Mode: play/draw carry ~0.5 turn of signal and R is sampled per-mode (R=100 = 100
games/mode), so e.g. "mulligan 1-land/no-Vial" prunes safely down to size 5 on the *play* but only
size 7 on the *draw*. A pruning rule is a region in `(composition × mull × mode)`, human-reviewed
per cell, and every prune ships behind an unpruned A/B proving losslessness.

## Lever 3 — rare + clearly-dominated tail (the safe probability floor)

The *intersection* of "combinatorically rare" (low hypergeometric weight) and "clearly-dominated
value" is the cheapest class, even on the lower tables:

- tiny weight in the threshold aggregate (and noise across the many independent tail cells
  averages out), so under-sampling the rare tail is safe for `mull_value`;
- never selected as a bottoming argmin (a clearly-bad hand is nobody's best subhand);
- far from any keep boundary, so its noise flips nothing.

This is the **safe** version of a blind probability floor: instead of *dropping* rare hands
(betting they don't matter), give them a couple of runs, **confirm** they're dominated, and stop —
if those runs land near a boundary the confidence-stop escalates instead. You still touch every
cell (no zero-cell skipping), but at minimal R; since the rare tail is *most* of a big deck's
cells, this is where the sub-table savings come from.

## Keep vs bottoming: where the savings do and don't apply

- **Size-7 table** — biggest win. Mull-0 is a pure keep/mulligan *threshold* (no bottoming), and
  it's the largest table. Confidence-tiering pays off most here.
- **Lower tables (size 6/5/4)** — can't be skipped, because each cell both (a) feeds a 7-card
  hand's keep decision via the bottoming min, and (b) contributes to threshold aggregates. Savings
  here are *adaptive-R-per-cell*, not cell-skipping.
- **Bottoming is a ranking problem, not a threshold.** Keep is one binary (above/below the bar);
  bottoming is an argmin over subhands. Per-hand best-arm racing does **not** globally reduce work,
  because the `V[7-m][subcomp]` values are **shared** across every 7-card hand containing that
  subcomp — a subhand that's a clear loser in one hand is a razor-thin winner in another, so you
  need most of the sub-size table broadly accurate. Tiering helps bottoming only for confident
  mulligans (never bottomed) and mull-0 (no bottoming). Fortunately the sub-size tables are smaller
  (Slivers 3,787 / 1,661 / 639 vs 7,758), and bottoming ships behavior-gated, so keep-only profiles
  get full tiering and you pay near-full R on the sub-tables only when bottoming is enabled.

## Bottoming attribution (clairvoyance vs worse decisions)

Runtime `--lookahead-bottoming` is **clairvoyant** — it peeks at the actual library to pick the
bottom. Blind exhaustive bottoming (`bottoming_enabled`) uses the profile's argmin. In-game the
clairvoyant one wins (expected — it cheats), so the in-game A/B loss is *confounded*:
`loss = decision-quality loss + clairvoyance loss`. Only the first term matters.

**Non-circular blind-EV test** (the right attribution): on the games where the two policies bottom
*differently*, re-score **both** chosen subhands at high R on **random** libraries
(`MTG_SCORE_COMPS`, blind-EV — neither sees the library), in the game's own play/draw mode.
`Δ = blindEV(our pick) − blindEV(lookahead's pick)`; Δ>0 means our decision was genuinely worse.
Score *all* divergent games, unconditioned on who won in-game (conditioning on the loss biases
toward clairvoyance). The per-all-games figure `mean Δ × (divergent/total)` is on the same scale as
the in-game loss, decomposing it into decision-quality vs clairvoyance.

**Slivers R=100 result** (987 divergent d3 games, R=400 depth-5 blind-EV): mean Δ = **−0.093t**
(our blind pick is *better* blind), 69% ours-better / 26% worse / 5% tie, per-all-games
Δ = −0.0192t (a decision *gain*), worst tail +0.178t with **zero** games > 0.2t. So the entire
+0.0226t in-game loss is clairvoyance; blind bottoming is blind-optimal while lookahead overfits
its pick to the one visible library. ⇒ **adopt blind bottoming** — better blind decisions, no
serious tail, ~3.5× faster (measured: lookahead 21.9 min vs blind 6.2 min, same games), honest,
and consistent with the already-blind keep decision. Cost: goldfish win-turn ~0.0226t slower
(the removed cheat) → a deliberate regression-GT shift, accepted via per-game audit.

## Ordering

For an expensive deck: (1) equivalence discovery to measure K; (2) force-merge review to shrink K;
(3) generate at a chosen R with adaptive sampling; (4) in-game A/B to validate keep; (5) blind-EV
attribution to decide bottoming. Pooling (`play-digest-and-pooling-gate.md`) lets a low-R first cut
be topped up later. Human review gates every force-merge and every pruning rule; unpruned A/B
proves losslessness before anything ships.
