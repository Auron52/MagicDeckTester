# The value-leaf quality FLOOR: a bad leaf must cost time, never quality

User, 2026-08-15, on finding that a value-leaf model had been rejected on a play A/B:

> *"It doesn't really make sense that value-leaf would damage things that badly. If it did it would
> mean we aren't using it correctly."*
> *"If the value-leaf is terrible we should be escalating if we can't find the win. It still is
> somewhat useful even then."*
> *"Even always falling back is usually still worth it, because you find wins more quickly with
> search and only escalate to one heuristic depth. So overall, there tends to be a work savings."*

This is a design invariant, and it is not currently asserted anywhere.

## The invariant

The leaf's job is to PROPOSE a line cheaply. The win still has to be verified, and an unverified line
escalates to one heuristic search. Under that contract a bad leaf buys a worse prior and costs
verification time -- it cannot cost quality, because every line it proposes is either verified or
replaced by what the heuristic would have said anyway.

At a FIXED BUDGET the invariant is stronger than "no worse". Shipped play is budgeted, so a cheaper
evaluator buys strictly more nodes for the same milliseconds; the saving converts into breadth rather
than wall clock. So:

> **A correctly-configured hybrid should measure >= the no-sidecar arm at equal budget, even when it
> never keeps a single leaf line.**

A measured, one-sided quality LOSS is therefore evidence about the CONFIGURATION, not about the
model. Treat it as a config fault and audit the three levers below before believing it.

## The three levers, with their real semantics

Taken from `src/ai/MulliganProfile.h` (verify there before relying on this summary -- the naming is
easy to invert):

| field | meaning | SAFE direction |
|---|---|---|
| `value_trust_depth` | keep the leaf line WITHOUT escalation once its committed depth >= this. **0 = unset = always eligible to escalate** | LOW / unset |
| `value_no_fallback` | after escalating, TAKE the heuristic result only if it clears the take-crossover; otherwise **FALL BACK to the leaf line**. `true` = always take the escalation | **true** |
| `value_fallback_crossover.take_heuristic_at_hdepth[c]` | after escalating a leaf line committed at depth `c`, take the heuristic iff `hcommitted >= this[c]`, else keep the leaf | LOW (take sooner) |

**Note the terminology inversion.** In this codebase "fall back" means *keep the value-leaf line*, not
*retreat to the heuristic*. `value_no_fallback: true` is the always-take-the-heuristic setting. An
agent reasoning from the colloquial meaning will pick exactly the wrong lever.

**So `trust UNSET` is the SAFE setting, not a misconfiguration.** It means every unverified line is
escalated. Its cost is real -- verification consumes the budget that would otherwise widen the
search -- but its quality direction is conservative. The quality leak is on the OTHER two levers: a
high trust depth (commit to leaf lines unverified) or a fall-back that keeps the leaf after the
escalation disagreed.

## Why the safest configuration is still worth shipping

Escalate everything, always take the heuristic: quality lands at ~the heuristic arm, and the cost is
the leaf search plus ONE heuristic search per decision, instead of a heuristic rollout at every leaf
of the search. That is where the work saving comes from, and it survives the maximally-conservative
take rule intact.

Measured supporting numbers already in the repo:

* the leaf is 1.35x-84.8x cheaper than the heuristic path per cell (the H-cell ladder guard exists
  precisely because a MISSING sidecar costs that much);
* Creature Giving's own matrix: V5 1.8 s/game against H4 29 s/game unbounded;
* falling back is measurably the risky direction -- Hinata scored **LP 6.0250 with fall-back vs
  5.9917 always-take** (s1001), i.e. keeping the leaf line cost quality on roughly a third of
  escalations. That is what `value_no_fallback` was added for.

**Conclusion: defaulting to the safe side costs the UPSIDE of trusting the leaf, not the work
saving.** There is no tradeoff to weigh, so "keep the leaf" should require positive evidence.

## Where the derivation currently breaks that

The levers are DERIVED from the depth matrix, so a thin or starved matrix can talk the deriver into
the unsafe side. Two instances, both real:

* **FiveColour**: the H6 row -- 111 unusable games from cells that tripped the one-hour guard --
  flipped the derived fallback rule from *never* to *fall back at H6*. Junk cells moved a quality
  lever. (H6 has since been dropped from the ladder, `cbb7876`.)
* **Creature Giving** (matrix 2026-08-06, `logs/eval/valueleaf_depth_cg.txt`): H5 and V8 are
  reference-capped at **50 games against 400** everywhere else, so every crossover entry for c >= 6
  rests on an eighth of the sample. The stored cells are **mean-only** (`lp_sum`/`games`/`ms`, no
  per-game rows), so the trust call -- `V5=4.7800` vs `h_conv=4.7762`, `+0.0038` against a hard
  `0.0020` tolerance -- is a point estimate compared to a constant, with no SE anywhere in it. This
  is the same boundary flip-flop the Knights work flagged: *"the trust derivation's hard 0.002
  threshold on a noisy cell flip-flops near the boundary -- a noise-aware margin (clear tol by > cell
  SE) is a deferred improvement."*

## The proposed rule (2 of 4 SHIPPED)

Driver-side, in the deriver, applying to every deck:

1. **Conservative default on thin evidence.** If a cell supporting a lever is reference-capped
   (intractable), quality-capped, or holds materially fewer games than its comparators, the lever
   takes its SAFE value. Never let a starved cell buy "keep the leaf". *(For `value_trust_depth` this
   now falls out of 2 -- fewer games means a wider bound means no trust. Still open for the
   crossover.)*
2a. **SHIPPED: trust is NOMINATED by the matrix and DECIDED by games.** User, 2026-08-15:
   *"Realistically how we should be handling trust is by playing with it A/B on vs off in additional
   games and verifying that the results are good. So the tolerance here would just gate an acceptance
   test."* The matrix cannot settle it: it measures the two arms SEPARATELY and UNBOUNDED, while
   trust is a claim about what happens when leaf lines are kept inside real BUDGETED play, where the
   skipped escalation is spent widening the search instead. Different experiment.

   So phase D writes `value_trust_depth_candidate` and phase E runs **trustON vs trustOFF** on 8
   held-out seeds x 1000 games -- pooled into the SAME batch as the existing A/B and play sweeps, on
   seeds disjoint from both and spaced exactly games-apart so the arms tile once (rule 7).
   `scripts/vlq_trust_accept.py` applies the verdict and, on acceptance, promotes the candidate into
   `value_trust_depth` **in the staged file** -- still not adoption.

   **NON-INFERIORITY, not an improvement test.** Trust is a cost lever whose upside is the escalation
   it skips, so the claim under test is that skipping does not cost QUALITY: accept iff the
   one-sided 95% bound on (ON - OFF) is at or below tol. Requiring ON to measure BETTER would reject
   a lever that is exactly neutral and cheaper, which is the outcome we most expect and most want.
   Not accepted => no trust => everything stays eligible to escalate, the side that cannot cost
   quality. The script also refuses on a seed-tiling violation, and flags an arm pair that came back
   byte-identical on every seed (the lever never engaged -- accepting would be accepting nothing).

   First run, burn: candidate d4, ON-OFF **+0.00000 turns** (95% upper +0.00000) over 8 x 1000, at
   **0.96x the cost**, 7/8 seeds byte-identical -- ACCEPTED and promoted. Exactly the expected shape:
   quality-neutral, and the saving is the escalations it stopped paying for.

2. **SHIPPED: noise-aware margin instead of a hard tolerance.** `value_trust_depth` is now an
   EQUIVALENCE claim: the PAIRED gap `V_d - h_conv`, over the games both cells hold, must have its
   one-sided 95% upper bound at or below `tol` -- not merely its point estimate. Thin or noisy
   evidence therefore fails SAFE, which is the direction that cannot cost quality. A table with no
   per-game record (every pre-2026-08 matrix, including Creature Giving's) falls back to the old
   point test, because an error bar cannot be added retroactively and turning those decks' trust off
   on absent evidence would be the same mistake in the other direction. The gap, SE, bound, paired-N
   and sample resolution are recorded per depth in `value_leaf_table.trust_gap_bounds`, so an UNSET
   that measured a real gap can be told apart from one that ran out of evidence -- those call for
   opposite responses (accept it, vs. measure more games).

   **What it does NOT do: fold in the rule-of-three resolution floor**, and that was measured rather
   than assumed. The first cut did fold it in, by analogy with the matrix driver's rung test, and it
   flipped burn from `trust=4` to UNSET -- on V6, which is identical to the heuristic on every one of
   its 1,445 paired games. At `tol=0.0020`, `3/n < tol` needs n >= 1500 paired games, and a cell tops
   out at 4 seeds x 400 before any skip-list attrition. Folding it in makes the rule unreachable at
   every sample we run, i.e. a test that can only return one answer. **So the floor is reported, not
   applied:** the deriver warns once per deck when `tol` sits below the sample's resolution, and
   emits an explicit INCONCLUSIVE note when the interval straddles `tol` (burn V3: gap +0.0021,
   interval [+0.00011, +0.00405]). Whether to answer that with a bigger sample or a bigger `tol` is a
   policy call with play consequences, and it is deliberately left to a human.
3. **Separate the two questions when reporting.** *When do we verify* (`value_trust_depth`) is a
   BUDGET-ALLOCATION question; *what do we take after verifying* (`value_no_fallback` +
   `take_heuristic_at_hdepth`) is the only QUALITY question. An A/B that moves both at once cannot
   attribute its result to either.
4. **Assert the floor.** A staged arm measuring significantly WORSE than no-sidecar at equal budget
   is a config fault, not a model verdict -- the pipeline should say so rather than report it as a
   rejection.

Related: `.claude/skills/value-leaf.md` (sidecar PRESENCE activates the hybrid; a rejected model
ships as `<stem>.value.DISABLED.json`), `docs/design/value-leaf-regeneration-queue.md` (the Knights
decline and the deferred noise-aware margin), `docs/design/depth-matrix-degenerate-games.md` (why a
capped cell must not enter a paired comparison).

**Status 2026-08-15:** unimplemented and deliberately not started -- another agent was mid-measurement
on Creature Giving's rejection, and changing the deriver would have moved the numbers under them.
*(2026-09-03: still unimplemented, but the blocking reason expired weeks ago — that measurement
is long finished; free to pick up.)*
