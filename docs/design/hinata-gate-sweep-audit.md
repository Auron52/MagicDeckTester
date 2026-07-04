# Hinata — granular gate-sweep heuristic audit (2026-07-04)

Deck `decks/Hinata2.cod`. Method: the `.claude/skills/heuristic-optimization.md` granular
`MTG_UNPRUNE=<gate>` protocol — open ONE narrowing gate at a time, UNBOUNDED (`--budget-ms 0`)
d5, and diff per-game win turns vs a same-manifest baseline, tracing every faster win to classify
**real deterministic gap** vs **clairvoyance artifact** (diverged draws the search sees within its
horizon but a blind heuristic cannot). Sample: 100 games, seed 8008, `--game-trace-dir`.

Unbounded Hinata has a pathological-combo tail (≥1 game/100 never finishes — gi=38, like slivers
g4), so each arm was run detached and diffed on the **intersection of games that completed in both
arms** (86–92 games). This is faithful: only per-game like-for-like comparisons are counted.

Gate probe (live gates): **altpayload, tutor, xspell, ponder, groupcap, comboline, searchorder**
(dead: fetch, dig, redirect, drawengine).

## Results (baseline: 99 games, 93 wins, avg-win-turn 5.785)

| Gate | kind | faster | later | win→loss | verdict |
|------|------|-------:|------:|---------:|---------|
| **searchorder** | enabling | 3 | 1 | 0 | **all 3 faster = clairvoyance** (diverged draws); net-neutral |
| **groupcap**    | enabling | 0 | 0 | 0 | inert (breadth cap 12 already sufficient) |
| **xspell**      | enabling | 0 | 0 | 0 | inert (max-affordable X already optimal) |
| **altpayload**  | enabling | 0 | 0 | 0 | inert (no real alt-cost card fires) |
| **tutor**       | shuffle  | 1 | 12 | 0 | net-NEGATIVE; faster = clairvoyance (fetch reshuffles deck) → heuristic earns keep |
| **ponder**      | shuffle  | 0 | 8 | 5 | net-NEGATIVE; all 5 win→loss = diverged draws → heuristic earns keep |
| **comboline**   | disabling| 0 | 1 | 0 | lossless fold, earns keep (matches search-restriction B3) |

## Verdict: NO adoptable heuristic gap on Hinata.

Every faster win under every gate is a **shuffle/reorder clairvoyance artifact** — confirmed by
tracing: same opening hand, but the draws diverge because the opened gate shifts an early land /
tutor / Ponder decision that reorders Hinata's library. Hinata is *saturated* with library
manipulation (Ponder, Gamble, Preordain, Expressive Iteration, Soulfire Eruption), so ANY gate that
perturbs an early decision reshuffles the seeded draws → the only "wins" available are ones a blind
heuristic can't reproduce. The shuffle gates (tutor/ponder) are net-NEGATIVE even unbounded (opening
them loses 5 games / slows 20), which positively confirms the narrowing heuristics (`TutorCandidates`,
`KeepReorderTop`) earn their keep. The enabling gates are inert or clairvoyant-only; the disabling
gate (comboline) is a lossless fold.

Contrast Anti-Lifegain (one real gap = Invigorate lethal-closer, shipped): Hinata has ZERO real gaps
because, as a combo deck, it has no deterministic blind signal to exploit — the win lines all route
through clairvoyant library sculpting the search already exploits within its horizon.

**searchorder pruning is load-bearing for tractability**: the searchorder arm took ~16 min for 89
games vs baseline ~10 min for 99 (cast-order permutation explosion — same pathology as slivers g4).

Traces: `logs/audit/traces/hinata_<gate>/` (gitignored scratch). Nothing authored/adopted.

---

## Aggro / Treasure-Hunt searchorder double-check (300 games, same protocol)

Scaling the prior 50-game aggro sweep (see the `aggro-decks-gate-sweep` memory) to **300 games
unbounded d5, seed 8008**, searchorder (the one live gate that ever showed activity; altpayload/
groupcap were inert at 50g):

| Deck | intersection | faster | later | win↔loss | verdict |
|------|-------------:|-------:|------:|---------:|---------|
| burn    | 297 | 1 | 1 | 0 | net-neutral; both changed games = **lookahead-bottoming divergence** (gi=14 kept a different hand, gi=216 reshuffled draws), NOT a cast-order gap |
| slivers | 297 | 0 | 0 | 0 | clean (3 ordering-explosion stragglers excluded) |
| knights | 300 | 0 | 0 | 0 | clean |
| th      | 297 | 0 | 0 | 0 | clean |

**No real cast-order gap on any deck.** Key finding: even burn — which has NO in-game library
manipulation (tutor/dig/fetch/ponder all dead) — produced its two changed games via the
**lookahead-bottoming rollout** (a clairvoyant full-game rollout that decides which cards to bottom).
Opening searchorder perturbs that rollout → a different kept hand / reshuffle → a physically
different game. So the bottoming rollout is a clairvoyance vector for EVERY deck; a searchorder
faster-win is only a real gap if the kept hand AND the draws are identical — which never occurred.
searchorder remains a net-neutral tuned heuristic and is load-bearing for tractability (slivers/th
had ordering-explosion stragglers that never finish unbounded). Nothing authored/adopted.

---

## Blind-heuristic quality of the ponder/dig decisions (2026-07-04, follow-up)

The gate sweep only rules out CLAIRVOYANT gaps ("does unpruning find a line the heuristic costs the
search?"). It cannot tell whether the *closed* ponder/dig heuristic makes good BLIND decisions — the
unprune compares "heuristic" vs "clairvoyant search," never "heuristic" vs "a better blind rule."
So the ponder heuristic was decomposed into its three sub-decisions and each measured on its own terms:

| Sub-decision | Type | How measured | Result |
|---|---|---|---|
| Reorder ORDER of kept cards | deterministic | reverse the rank order, A/B at d5 budget 20 (fair: no extra branches) | **−0.67pp / +0.03t** (worst case) |
| Keep-SET selection (EI hand/exile/bottom, Preordain keep/bottom) | deterministic | reverse the selection, same A/B | **−1.67pp / +0.10t** (worst case) |
| Keep-vs-SHUFFLE (Ponder shuffle branch) | STOCHASTIC (new library) | needs fork-and-vary Monte-Carlo (unbuilt) | unmeasured |

**Why the deterministic A/B is valid and cheap here:** an ordering/selection POLICY is blind by
construction (a pure function of board state) and swapping one blind policy for another adds NO search
branches — it changes the OUTCOME of one deterministic sub-decision, not the tree breadth. So (a) no
reshuffle => no clairvoyance confound; (b) budget is fair at any level (both arms search identically),
unlike unpruning which dilutes; (c) the winner is directly adoptable. Baseline (both flags unset) is
byte-identical (digest 7d9abd70.../502182002... on s2002/s3003, 570/600 wins, avg 5.837).

**Reading:** reversing is the WORST possible blind policy, so each row is an UPPER BOUND on that
lever. The current rank comfortably beats reverse on both, so it sits in the good part of its band;
the current-vs-optimal gap is strictly smaller than current-vs-reverse. The entire deterministic
ponder/dig selection+ordering surface is worth **≤ ~2pp**, realistically a fraction of that. The
heuristic is NOT silently losing meaningful games on its deterministic decisions.

**Remaining unknown:** only the stochastic keep-vs-shuffle decision, which needs the (expensive)
fork-and-vary Monte-Carlo instrument — fork at the shuffle point, draw N reshuffled libraries,
play each out with clairvoyance preserved WITHIN each sample, and compare E[shuffle] vs keep. Given
both deterministic siblings are <2pp worst-case, the prior is the shuffle lever is similarly bounded,
but that is a prior, not a measurement. Not built. Nothing authored/adopted; all scaffolding reverted.
