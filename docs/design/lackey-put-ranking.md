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

## The search still beats it (MTG_LACKEY_AXIS, default ON)

That expectation was wrong, and the way it was wrong is the point. Ranked candidates + a searched
axis (width 2), 300 games x 7 seeds, vs the heuristic alone:

| seed | heuristic | searched | delta |
|---|---|---|---|
| 1001 | 4.3367 | 4.3267 | −0.0100 |
| 2002 | 4.3600 | 4.3567 | −0.0033 |
| 3003 | 4.3733 | 4.3700 | −0.0033 |
| 4004 | 4.3267 | 4.3200 | −0.0067 |
| 5005 | 4.3433 | 4.3367 | −0.0066 |
| 6006 | 4.3700 | 4.3633 | −0.0067 |
| 7007 | 4.4400 | 4.4267 | −0.0133 |
| **total** | | | **−0.0499** |

**7/7 improve.** W=3 and W=4 measure identical to W=2 on every seed, so the entire contribution is
"occasionally the provider's #2 beats its #1" — not a deep re-ranking. Cost +12% makespan on
goblins; no other deck has a cheat source, so no other deck pays anything. Suite: 4 faster, 0
slower, 22 same-score line changes (spot-checked: identical hands and draws, the new line simply
deploys more).

**The lesson.** A heuristic being *measurably the best available ranking* does not make the branch
redundant. MV wins every head-to-head against a rival rule and still leaves 0.05 on the table for a
two-wide search. Rankings are static; the search sees the actual board. This is the concrete case
for "a heuristic is a branch's DEFAULT, not a substitute for branching".

## Implementation note — why this pin lives on GameState

`scry_choice` and `etbdig_choice` pin their decision with a scoped thread-local around the plan
apply. That does not work here: the put is chosen in the MAIN phase but consumed in the
COMBAT-DAMAGE step, so a scoped guard is destroyed before the trigger ever fires.
`GameState::scripted_cheat_choice` instead rides every rollout deep-copy for free, so each plan
variant carries its own pick. It is consumed by the first trigger and reset at the start of each
turn (in both `GameEngine::UntapStep` and `SimulateEndAndStartNextTurn`) so it cannot leak into a
later combat.

This keeps the card's real timing — the put still happens in the combat-damage step, as printed —
rather than relocating it to the second main.

## Status

- **Ported** to `CombatCheatCandidates` (byte-identical) — commit `0a67d09`.
- **Reviewed + measured** — this document.
- **Branched** — the candidates are returned ranked so a searched axis inherits MV as its tie-break
  winner (defect class 3: strict improvement means the first-enumerated option owns every tie).

`MTG_LACKEY_RANK` is a temporary experiment lever. Per the coding conventions its losing branches
(`low`, `pow`, `uncast`) should be deleted now that the outcome is recorded here — pending user
sign-off, since deletion of a measured lever is a user call.
