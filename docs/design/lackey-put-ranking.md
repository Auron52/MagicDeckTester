# Goblin Lackey: which permanent to cheat into play

2026-08-01. Part of the audit of engine-embedded heuristics
(`engine-heuristics-to-providers.md`). Goblin Lackey's "whenever this deals combat damage to a
player, you MAY put a Goblin permanent card from your hand onto the battlefield" had **no provider
hook at all** — the engine picked highest-MV inline. It is now
`DecisionProvider::CombatCheatCandidates`, which returns the ranked candidate list.

## The decision is NOT an Aether Vial deploy in miniature — it is bigger

The put is **free** and resolves in the combat-damage step, so:

- the permanent is summoning-sick **and** attackers are already declared → it cannot attack this
  turn, so raw power on the body is worth less than it looks;
- the whole value is **the mana you never paid**. That is why "highest MV" is not the crude proxy it
  appears to be: it is a direct estimate of the thing being stolen.

## How often is there a choice?

`MTG_LACKEY_TRACE=1`, Goblins, 200 games, seed 4004 — 1,697,859 trigger evaluations:

| candidates | share |
|---|---|
| 1 (forced) | 30.9% |
| 2 | 30.6% |
| 3 | 23.2% |
| 4 | 11.7% |
| 5+ | 3.7% |

**69% of triggers have a real choice**, mean 2.3 candidates. The picks are dominated by
Siege-Gang Commander, Muxus, and Krenko — the expensive payoffs, which is the rule working.

## Variant sweep (MTG_LACKEY_RANK), 300 games x 7 seeds, all three seed sets

Sum of avg win turn across seeds (lower is better):

| variant | total | vs `mv` | description |
|---|---|---|---|
| **`mv`** | **30.5500** | — | highest MV, ties by power then card number (**shipped**) |
| `uncast` | 30.5568 | +0.0068 | highest MV among cards you could NOT cast right now, then the rest |
| `pow` | 30.6101 | +0.0601 | highest power first |
| `low` | 32.0200 | +1.4700 | LOWEST MV first — a deliberate anti-heuristic |

Per-seed, `mv` is best-or-tied on 7/7. `uncast` ties it on 5 seeds and loses on 2 (3003, 5005).

**Reading it.** The `low` arm is the point of the sweep: it bounds the headroom. A *bad* ranking
costs **+1.47** total (+0.21/seed), so this decision matters a great deal — far more than the ETB
dig, whose entire searched-axis gain was 0.06 total. But every *sensible* ranking lands within 0.06
of the best, and the one that encodes the "you're stealing mana" intuition explicitly (`uncast`) does
not beat the cruder proxy that already correlates with it.

So: the value here is in **not getting it wrong**, and highest-MV already gets it right. That is the
argument for keeping MV as the default — not an argument against searching it, only against
expecting a search to find much.

## Status

- **Ported** to `CombatCheatCandidates` (byte-identical) — commit `0a67d09`.
- **Reviewed + measured** — this document.
- **Branched** — the candidates are returned ranked so a searched axis inherits MV as its tie-break
  winner (defect class 3: strict improvement means the first-enumerated option owns every tie).

`MTG_LACKEY_RANK` is a temporary experiment lever. Per the coding conventions its losing branches
(`low`, `pow`, `uncast`) should be deleted now that the outcome is recorded here — pending user
sign-off, since deletion of a measured lever is a user call.
