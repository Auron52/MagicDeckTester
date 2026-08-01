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

## Revealed preference: is the ordering strict, or situational?

`MTG_LACKEY_PREF` logs which candidate the SEARCH actually took, for real resolutions only
(`g_real_resolution`, cleared by `RevealLogPause`), run at `MTG_LACKEY_WIDTH=12` so nothing is
pruned before the search sees it. 823 contested triggers:

| card | picked/available | | card | picked/available |
|---|---|---|---|---|
| Muxus, Goblin Grandee | 204/206 **99.0%** | | Goblin King | 37/157 23.6% |
| Siege-Gang Commander | 180/244 73.8% | | Pashalik Mons | 15/74 20.3% |
| Krenko, Mob Boss | 34/73 46.6% | | Goblin Matron | 20/151 13.2% |
| Twinshot Sniper | 31/73 42.5% | | Goblin Piledriver | 17/132 12.9% |
| Goblin Warchief | 26/62 41.9% | | Rundvelt Hordemaster | 5/136 3.7% |
| Goblin Chieftain | 29/87 33.3% | | Skirk Prospector | 0/32 0.0% |

**30 of 33 pairs are shutouts; only 3 are contested** — Muxus/Siege-Gang 77-2, Warchief/Siege-Gang
1-14, Siege-Gang/Skirk 13-1. All three are at the TOP of the order, i.e. where the decision matters.

## Why a per-deck priority table is NOT worth shipping (measured 0.0000)

That order is **exactly mana-value order across tiers**, which is why the plain MV rule scored so
well. The mechanism (user): the put is free and summoning-sick, so its value is the mana you skipped
— and the cheap card gets hard-cast anyway, so using the Lackey on the expensive one usually deploys
BOTH. Twinshot Sniper and Krenko are MV 4, a tier above the 3s, so MV already ranks them right.

MV is blind only WITHIN a tier: six cards tie at MV 3 with a 3x spread in pick rate, where the tie
falls through to power then card NUMBER (shuffle order). A `GoblinsProvider` was built to supply
that within-tier order from the measured data, keeping MV primary. It measures **+0.0000 on all
seven seed sets**. Verified live rather than assumed — INVERTING the table moves play, so the null
is real:

| perturbation | cost |
|---|---|
| invert the ACROSS-tier order (`MTG_LACKEY_RANK=low`) | **+0.85** |
| invert the WITHIN-tier order (inverted priority table) | **+0.0033** |

Two orders of magnitude apart. Within-tier order is worth ~0.003 at most, and the searched axis
already recovers it: W=2 gives the search two shots, and the third-ranked candidate is rarely the
right answer. So the table was **not shipped** — it is dead weight plus a hardcoded name list that
goes stale the moment the decklist changes, bought for nothing.

**The reusable lesson:** before hand-building a per-deck priority table, measure the *tier* signal
against the *within-tier* signal. Here one is 250x the other and only the cheap generic rule (mana
value) was needed.

### The one genuine CROSS-tier exception — and why it still needs no table

A per-deck table keeping MV primary could not have fixed a cross-tier error anyway. Tested the two
proposed candidates (user: "Twinshot Sniper could definitely be incorrect to prioritize over Goblin
Chieftain. Maybe Krenko as well"):

- **Krenko, Mob Boss is NOT an exception** — 16-0 vs Chieftain, 11-0 vs Warchief, 17-0 vs King,
  12-0 vs Chainwhirler. MV order is right every time.
- **Twinshot Sniper IS**, and BOARD WIDTH is the condition:

| Twinshot Sniper vs | creatures <= 2 | creatures >= 3 |
|---|---|---|
| Goblin Chieftain | **3-3 (50%)** | 7-0 (100%) |
| Goblin Warchief | **2-2 (50%)** | 1-0 |

A lord's value scales with the bodies it buffs; Twinshot's 2 damage does not. So on a narrow board
it is a coin flip and on a wide one the 4-drop wins. Small n — suggestive, not settled.

The point: those numbers ARE the search's own choices. Twinshot (MV 4) and Chieftain (MV 3) are the
top two by mana value, so at W=2 both are scored and the search took Chieftain in 3 of 13. A static
rank — per-deck table or not — cannot express "unless the board is narrow"; the branch can. This is
the same conclusion the axis measurement reached, arrived at from the opposite direction.

## Hypotheses tested and not supported

- **"A second Muxus is worth less than the first"** — 68-2 with none in play, 9-0 with one already
  down. Its value is a one-shot ETB, not a static buff, so copies do not go redundant like a lord's.
  (n=9 rules out a large effect, not a small one.)
- **"Matron beats a lord on a narrow board"** (fetch Muxus so the Lackey can drop it next turn) —
  Matron is 0-4 / 0-4 / 0-4 against King / Chieftain / Warchief at `creatures <= 1`, and loses at
  every board width. But n is small, and the goldfish horizon structurally undervalues the line: it
  needs Lackey to connect AGAIN a turn later, and with a 4.3-turn average clock a turn-4 Matron
  rarely cashes. The engine is not disagreeing with the MTG reasoning; it is correctly pricing a
  two-turn payoff in a format that ends first.
- Matron DOES beat **Piledriver 15-1**, which confirms the summoning-sick mechanism: Piledriver's
  whole value is attacking, and a cheated-in creature cannot attack.

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
