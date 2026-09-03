# Divergence analysis: the comparison step that explains WHY, and catches bugs

**Status (updated 2026-09-03):** Tier 1 IMPLEMENTED as `scripts/divergence_report.py`
(a306f919, same day); Tiers 2–3 remain manual.

**Status:** design, 2026-08-18 (user requirement). Applies to any paired deck-version comparison
(`deck_compare.py` screens, and the per-arm store described in [[cell-arm-rollout-store]]).

A screen returns one number: arm A is 0.07 turns better than arm B. That number cannot tell you
whether the new card is *good*, whether it is good *for the reason you think*, or whether the
difference is a **bug**. This step answers those, on the games where the two versions actually
diverged.

## What "diverged" means here

**A game is in scope iff the two arms END ON DIFFERENT WIN TURNS** (user, 2026-08-18). That is the
whole selection rule. A game both arms win on turn 5 is out of scope even if they played it
differently; a game won on turn 4 vs turn 6 is in scope even if the first four turns are identical.

This matters because there is a tempting alternative — "select games whose action sequences differ"
— which is a much larger and much noisier set (nearly every game, since the decklists differ). The
turn at which the two action sequences first diverge is still computed, but as a **derived
diagnostic inside the selected set** (it localises the cause), never as the way in.

Consequence to accept knowingly: a bug that changes play without changing the win turn is invisible
to this step. That is the right trade for a comparison whose unit of value IS the win turn — the
mechanical invariants below can be pointed at same-turn games later if a specific need arises.

Two purposes, in the user's order:
1. **Ensure there are no bugs.** A card that is silently inert, mis-costed, or mis-valued shows up
   as a divergence pattern long before it shows up in a mean.
2. **Understand where the cards are better and worse** — which boards, which turns, which lines.

Output is analysed by AI and presented to the user, who can open any game in the **log viewer**
(`tools/replay/index.html`, drag the game JSON) or re-play it in the **play viewer**
(`tools/play/server.js`) from the printed `--seed S --game-index I` repro.

## Rule 0 — SAME INDEXED CARDS ⇒ SAME WIN TURN; that is the gate

The gate is **conditional**, and the conditional form is the one that means something (user,
2026-08-18): among games where both arms held the **same indexed cards**, the win turn must be
**identical** — not "usually equal", identical. The shuffle is seeded on card NUMBERS and the
`replace` map keeps shared cards at the same numbers, so a game that never draws a swapped number
plays a literally identical card sequence in both arms. A win-turn difference there cannot be the
cards; it is apparatus or a bug.

An earlier version of this rule stated it unconditionally — "most games should not diverge" — and
that is **wrong for a deck that mass-draws**. Measured on Mirrorwing base-vs-trick (2,500 paired
games, 11 of 60 cards swapped): the *unconditional* rate was **40.6%**, while the conditional
invariant passed perfectly at **0 of 151 (0.00%)**. Reading the unconditional number as a pairing
failure would have condemned a correctly-paired run. With cantrip fan-out under a magnet a deck
sees most of its library, so a swapped card turns up in almost every game.

So: compute BOTH, and gate on the conditional one.
- **Conditional (the gate):** divergences among same-indexed-cards games. Must be ~0.
- **Unconditional (context only):** the overall rate. High is normal for a heavy-draw deck and is
  NOT by itself evidence of anything.

### The unconditional rate still needs decomposing, just not as a gate

When it IS high, find out where it starts, because "diverged" hides two very different causes:

| where the divergence begins | meaning |
|---|---|
| at the mulligan (different keep or different bottom) | APPARATUS, unless a swapped card is in the hand |
| in play, from identical keeps | the cards — the thing being measured |

Measured on the same run: **72.4% of divergences began at a different mulligan decision**, and on
hands containing no swapped card the two arms' tables disagreed on keep 13.7% of the time and on
bottoming 56.9% of the time. That is two independently-noisy R=10 tables, not the edit — see
[[shared-apparatus-cost-mitigations]]. Compare opening hands by card **NUMBER**, never by name: the
`replace` map makes number 41 "Ancestral Anger" in one arm and "Impolite Entrance" in the other, so
a name comparison scores an IDENTICAL keep as a different hand (it inflated this figure to 93.9%
before the fix).

### The trap that produces a false high rate

A first attempt measured **57.4% divergence** (1,147 of 2,000) by running the two decklists through
`mtg` directly on the same seed. That number was an artifact, and the cause is worth stating because
nothing about the run looks wrong:

**Card numbers are assigned in alphabetical order of card name, and the shuffle is seeded on card
numbers.** Inserting three new names (Fortifying Draught, Impolite Entrance, Luxurious Libation)
renumbered nearly the whole deck — measured, **12 of the 13 shared card names got different
numbers**. So the two arms drew unrelated libraries from turn 1, and even the 47 *unchanged* cards
sat in different positions. The comparison was two independent samples wearing the same seed.

This is exactly what `deck_compare.py`'s `replace` map is for — it decides which incoming card
inherits which departing card's slots, so shared cards keep their numbers and the shuffles stay
aligned. Without it the freed numbers go out in sorted-name order: deterministic, and arbitrary.

**Therefore: never measure divergence from raw `mtg` runs of two decklists. Always go through the
driver**, which owns numbering and pairing. And treat any confounded apparatus (one arm on a keep
table, the other without) as equally disqualifying — that alone re-randomises the mulligan and thus
the game.

The corollary is a cheap, permanent acceptance gate: **if most games diverge, stop and fix the
pairing.** Only once the rate is low does the triage below matter.

## Triage is structural, not optional

Even at a healthy rate, a 20,000-game screen can produce thousands of divergent games, and AI
reading of all of them is not affordable. So the step splits by cost:

- **Tier 1 — mechanical, EXHAUSTIVE (all divergent games, no AI).** Cheap per-game facts and every
  bug invariant. Bug detection must never be sampled: a bug in 3 games of 11,500 is still a bug.
- **Tier 2 — AI, a RANKED and STRATIFIED sample with a fixed budget** (~40–60 games, not a
  threshold that scales with game count). Reads the two logs side by side and explains the
  divergence.
- **Tier 3 — the user**, with viewer links, on whatever Tier 2 surfaced.

## Tier 1: mechanical facts, every divergent game

Per game, from the two logs: Δwin-turn; the **first turn at which the two action sequences differ**
(the causal locus, not the outcome); which new/old cards were drawn, cast, and at what X; damage per
turn; whether a magnet was out.

### Bug invariants (run on all divergent games; any hit is escalated to Tier 2 regardless of rank)

- **Inert cast** — a new card was cast and the turn's board delta and damage are unchanged versus
  not casting it. This is the per-game form of the neutralise-and-diff gate that caught
  [[x-spell-tricks-dropped-from-enumeration]].
- **Dominated `{X}`** — an `{X}` spell cast at a value that cannot help (X > 0 with no attacker; X
  below the affordable maximum with no other mana sink that turn). This is the shape of
  [[x-variant-invisible-to-plan-ordering]], which survived three fixes precisely because the
  aggregate distribution looked reasonable.
- **Regression despite the card** — the arm holding the new card cast it and finished ≥3 turns
  later. Legitimate sometimes; a cluster is not.
- **Divergence with no new card** — neither drawn nor cast in either arm, yet the outcome differs.
  See the noise floor below.
- **Never-cast card** — a new card drawn in ≥N games and cast in none. Whole-run, not per-game.

### The noise floor (a first-class output, not a footnote)

The fraction of divergences where **no new card was drawn in either arm** is a direct per-run
measurement of contamination — those games diverged for reasons that cannot be the edit. Under a
correct pairing it should be near zero, and every such game is a defect in the apparatus, not a
data point about the cards.

In the broken-pairing run above it was 9.1% (104 games) — and that figure *understates* the damage,
because with the whole library renumbered the other 90.9% were also unrelated games that merely
happened to contain a new card. Report this next to the effect, always, and read it together with
the overall divergence rate: a low rate with a near-zero noise floor is what licenses believing the
headline.

## Tier 2: what the AI sample is chosen to answer

Fixed budget, stratified so the sample is not all tail:
- every game flagged by a bug invariant (capped, with the drop count logged — never silently);
- the largest \|Δ\| in each direction (where mechanisms are clearest);
- a random sample of \|Δ\| = 1 in each direction (where the bulk of the mass is, and where a
  tail-only sample would mislead);
- games where the new card was cast at an unusual X for its turn.

For each, the AI reads both logs from the first divergence turn and reports: what diverged, the
mechanism, which arm was better and why, and a bug/not-bug call with its reason.

## Presentation

A ranked report: one line per game (Δ, first-divergence turn, cards involved, one-sentence
mechanism), grouped into *new card won it* / *new card lost it* / *unrelated* / *suspected bug*,
each with a viewer link and a repro command. Aggregate rollups — per-card win/loss attribution,
per-turn divergence histogram, X-value outcomes — sit above it, because the *pattern* across games
is the deliverable and the individual games are the evidence.

## Pairing caveat

Two versions with different decklists do not draw the same cards from the same seed. `deck_compare`'s
`replace` map (which incoming card inherits which departing card's slots) is what keeps the two
arms' draws aligned; without it, games diverge from turn 1 for reasons that have nothing to do with
the edit and "first divergence turn" stops being meaningful. The map changes no counts and no
estimate — only precision — so it is close to free, and this step is the reason to always set it.
