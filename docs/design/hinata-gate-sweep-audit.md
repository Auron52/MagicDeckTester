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
