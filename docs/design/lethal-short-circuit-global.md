# Follow-up: evaluate the board-lethal search short-circuit GLOBALLY (all decks)

## What it is
`TurnSolver::Solve` and `EnumeratePlans` have a **board-lethal short-circuit**: when the current board's
attack-all damage already kills the opponent this turn (`pending_atk >= opp.life`), evaluate only the
empty (attack-only) subset and return it, skipping the 2^m cast-subset odometer. A turn-winning plan
dominates every other plan this turn, so nothing is lost.

It is gated by the provider hook `DecisionProvider::UseLethalShortCircuit()` (default **false**),
overridden to **true** only in `GoblinsProvider`. Off-switch `MTG_NO_LETHAL_CUT`. Shares the
`MTG_UNPRUNED(ComboLine)` audit gate with the sibling combo-line / go-off cuts.

## Why it is currently Goblins-only (not correctness — bookkeeping)
It is **win-turn-invariant on every deck** (verified in a smoke run: all non-Goblins avg win-turns were
byte-identical with the cut on). It only changes **which** winning plan is chosen at a lethal node — it
skips pointless pre-lethal casts, arguably *cleaner* play. But the regression fingerprint includes the
`play_digest`, so turning it on globally flipped 22 configs (all same-avg, digest-only). Re-accepting
every deck's GT for a Goblins-motivated change was more churn than warranted mid-stream, so it was scoped
to Goblins (which was re-accepting its GT anyway).

## The follow-up (what the user asked to look at)
Measure how the cut behaves on the OTHER decks and decide whether to adopt it as a root default:
1. Flip the gate to a root default (either `DecisionProvider::UseLethalShortCircuit() -> true`, or a
   root `s_lethal_cut`-style default-on flag) — one line.
2. For each suite deck, A/B the **rollout speedup** (value.json moved aside, `--ignore-play-profile
   --depth D`, cut on vs `MTG_NO_LETHAL_CUT=1`) and **confirm win-turn invariance** (avg identical; only
   the digest moves). Aggro/lethal-heavy decks (burn, slivers, knights, auras) should benefit most; combo
   decks (dragonstorm/hinata) that win via the go-off model already have their own lethal short-circuits,
   so the marginal gain is smaller.
3. If the aggregate speedup is worth it, adopt globally and **re-accept all three GT tiers** (the digest
   change is expected and benign — winning this turn either way). Otherwise leave it Goblins-only.

Expected payoff is MODEST (on Goblins it was ~7% of the deep rollout, because the cost is the turn 3-4
BUILD-UP nodes, not the lethal leaves the cut skips). The value leaf already makes shipped play ~90x
faster, so this only speeds the pure-rollout / depth-matrix arm.
