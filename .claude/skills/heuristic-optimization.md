# Heuristic Optimization Skill

Authoritative guide for **AI-driven optimization of the engine's decision
heuristics** — the judgment calls the in-play search can't make — by proposing
variants and measuring them on the regression suite. Read this when the user asks
to **improve / tune / optimize a heuristic**, **try different orderings or
weights**, or **make a decision "searched" empirically** (offline, not at the
table). Builds on `.claude/skills/regression-testing.md` (the harness) and
`.claude/skills/mtg-ai.md` (where the heuristics live).

## Rule 0 — this skill is for HEURISTICS, not correctness bugs

Two different kinds of change look similar but are handled oppositely:

| | **Correctness / modeling** | **Heuristic judgment** |
|---|---|---|
| Question | "Does the engine model this card/rule *correctly*?" | "Given a correct model, which legal choice plays *best*?" |
| Arbiter | the rules (`.claude/skills/mtg-rules.md`) | empirical results (win% / avg win turn) |
| Right answer | exists and is unique | no ground truth; only "measurably better" |
| If wrong | it's a **bug** — fix it, don't "tune" it | it's a **weak heuristic** — optimize it (this skill) |

**Worked distinction (the Grove of the Burnwillows work that motivated this skill):**
- Grove's `{T}: Add {C}` mode was not modelled, so every tap gifted the opponent
  life. That was a **straight-up modeling bug** — fixed against the rules (commit
  `557c965`), not a heuristic to sweep. It should never have been there.
- *Which* red source to tap when Grove and Stomping Ground both make R, and whether
  to prefer/avoid Grove based on whether a Remedy is out — that has **no correct
  answer**, only a better-playing one. That's the heuristic (commit `94f020b`), and
  it's exactly the class this skill targets.

> Ordering sources in a list is the *usual* heuristic lever, but note the Grove case
> needed **more than a reorder**: a correct dual-mode mana model (the bug fix) **plus**
> a *state-conditional* rank (flip the drip land earlier when a Remedy makes its drip
> beneficial). The optimization space includes structure and state, not only list order.

## Why AI owns this

The alternative to AI optimizing these heuristics is a **human inventing every
constant and ordering** (mono=10/dual=20/…, the `+1/-1` Remedy nudge). The goal is to
remove the human from *idea generation*: AI proposes the variants, the harness scores
them, and the human reviews the **reported** result — a veto, not an author. Anything
the search can't decide during play is fair game.

## The heuristics in scope

Fast rules consulted per-decision because searching them at the table would explode
the branching factor. Most live behind `DecisionProvider` hooks (see `mtg-ai.md`):
- **`ManaSourceRank`** — scarcity-first tap order (`DecisionProviders.cpp`); the
  running example below.
- **`CastOrderRank`** — spell sequencing within a turn.
- Attack/block shortcuts, tutor/fetch target choice, mulligan fallbacks, staging.

Per-deck optima diverge, so a tuned value belongs in the **archetype provider**
override (e.g. `AntiLifegainProvider::ManaSourceRank`), never the root
`GenericProvider` — mirror the Grove nudge refactor.

## The loop

1. **Frame the heuristic** and confirm it's judgment, not a bug (Rule 0). Read the
   current implementation and its rationale comments.
2. **Author variants (AI's job).** Enumerate a few *motivated* alternatives — for an
   ordering, permutations of the tiers; for a weight, a small bracket; for structure,
   a state-conditional form. Each needs a one-line hypothesis for *why* it might win.
3. **Expose behind a temporary runtime selector** so a sweep needs no rebuild per
   value: `static const int variant = getenv("MTG_RANK_VARIANT") ...;` — **uncommitted
   scaffolding**, reverted after measuring (only the *winner*, if any, is committed,
   in its proper provider). Only relative order matters for a rank function.
4. **Sweep on the TRAIN seeds.** Run the regression suite once per variant; read the
   `got=<won>/<avg>` per case (both PASS and FAIL lines carry it — don't `--accept`):
   ```
   for V in 0 1 2 3; do MTG_RANK_VARIANT=$V bash test/regression.sh --smoke > /tmp/sweep_v$V.txt; done
   ```
   Prefer the **d0** cases for the first read (most games ⇒ least noise); the searched
   depths (d3/d5) are the play-quality check.
5. **Judge against noise.** A few wins out of ~1000 on a *single* seed is inside
   run-to-run variance — not a result. Score explicitly (win% primary, avg win turn
   tiebreak; they can trade off) and note per-deck divergence.
6. **Validate the winner on HELD-OUT seeds.** The three modes use disjoint seeds:
   tune on smoke/regression, confirm on **overnight** before trusting any delta
   (guards against overfitting the tuning seeds).
7. **Report the decision to the user** — knob, from→to, per-deck win%/avg deltas, any
   `win->loss`, and the noise caveat — *before* adopting. A pass that finds "baseline
   wins" reports that; it does not silently keep the default.
8. **Adopt + rebaseline** only on approval: move the winner into the archetype
   provider, delete the scaffolding, and `--accept` the GT like any change.

## Worked example — a simplification, refuted by measurement

The `AntiLifegainProvider::ManaSourceRank` drip nudge is state-conditional: `+1` (tap
Grove LATER) with no Remedy so a painless source is spent first, `-1` (tap it EARLIER)
under a Remedy since its drip is then 1 damage. A tempting simplification: **drop the
condition and just rank the drip land LAST** (past every real source), relying on the
end-of-main drip sweep (`TapDripLandsForRemedy`) to still deal the damage when an
enabler is out. Purely a list reorder, no state inference — is it as good?

**A/B (`MTG_GROVE_LAST` toggle) — current `±1` nudge vs. Grove-ranked-last:**

| case | `±1` nudge (committed) | Grove-last | |
|------|------------------------|------------|--|
| smoke d0     | 854/5.568 | 851/5.570 | worse |
| smoke d3 / d5| 250/**4.152** · 150/**4.100** | 250/4.168 · 150/4.127 | worse |
| reg d3 (×2)  | **4.147 · 4.144** | 4.150 · 4.157 | worse |
| reg d5 (×2)  | **4.148 · 4.157** | 4.156 · 4.173 | worse |

At **searched depth the signal is decisive and consistent** across disjoint seed sets:
12–14 games win *later* under Grove-last vs. 1–3 earlier; every d3/d5 avg win turn
regresses. (d0 greedy is a noisy wash — which is why the searched depths are the
quality check.) **Why:** ranking Grove last means that under a Remedy it is left for the
sweep while a *flexible* source (a rainbow dork like Birds of Paradise) pays the pip —
burning fixing the combo turn needed. The `-1` taps Grove eagerly instead: it drips AND
pays, sparing the dork. **Decision: keep the committed nudge; reject the simplification.**

The lesson this skill exists to enforce: the "obvious" simpler heuristic was *worse*, and
only measurement showed it. The drip-under-enabler guarantee genuinely "needs the
inference" (the Remedy-conditional rank), exactly as suspected — a static reorder can't
replace it.

## Secondary example — sweeping the generic tiers

Reordering `GenericProvider`'s tiers themselves (V1 dual-before-mono, V2 flat colored, V3
filters-first) across all six decks on smoke produced **no clean winner**: half the decks
(slivers/burn/knights) didn't move at all, and the rest traded ±2 wins in opposite
directions (V1 helped throes/antilife, V3 helped hinata, V2 hurt antilife/hinata) — all
inside single-seed noise. Takeaways: reordering rarely beats an already hand-tuned
baseline, and **per-deck optima diverge**, reinforcing that tuned ordering belongs in the
archetype provider, not the root.

## Pitfalls

- **Single-seed noise** — never adopt a small delta seen on one seed; escalate seeds.
- **win% vs speed** — a variant can win more games but end later; be explicit about
  the objective and surface the trade in the report.
- **Overfitting the tuning seeds** — always validate on the held-out (overnight) set.
- **Root vs provider** — a Remedy/deck-conditional rule is archetype logic; keep the
  root `GenericProvider` neutral.
- **Silent scaffolding** — the `MTG_RANK_VARIANT`-style selector is a throwaway; revert
  it, and commit only the winner in its provider.
