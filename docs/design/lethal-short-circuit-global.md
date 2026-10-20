# Follow-up: evaluate the board-lethal search short-circuit GLOBALLY (all decks)

## What it is
`TurnSolver::Solve` and `EnumeratePlans` have a **board-lethal short-circuit**: when the current board's
attack-all damage already kills the opponent this turn (`pending_atk >= opp.life`), evaluate only the
empty (attack-only) subset and return it, skipping the 2^m cast-subset odometer. A turn-winning plan
dominates every other plan this turn, so nothing is lost.

It is gated by the provider hook `DecisionProvider::UseLethalShortCircuit()` (default **false**),
overridden to **true** only in `GoblinsProvider`. *(2026-09-03: no longer Goblins-only — four
providers opt in: Goblins, Mirrorwing, Equipment, Stompy. The ROOT default is still false, so
the global A/B below remains the open item.)* Off-switch `MTG_NO_LETHAL_CUT`. Shares the
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

## 2026-10-20: the cut was NOT sound -- now verified (MTG_LETHAL_CUT_VERIFY, default ON)

"Nothing is lost" above held only while `pending_atk` (PendingAttackDamage) was real damage. It is a
projection, and it can read lethal on a board whose attack does not kill. The case that proved it:
**Bruna overnight s4004 gi334** (seed 4338, d3 b10; also d8 b0), owned by c5b72e56 (Bruna opting in).

* The T6 search verified a T8 kill: Greaves equip + Arcanum Wings on a Somberwald Sage, then at the
  post-entry breakpoint (site 9) continuation #1 = Avacyn's Pilgrim + Greaves onto it. In combat the
  Wings swaps for Colossification ({2}{U}, paid by Birds + the hasty Pilgrim's {W} through Skycloud
  Expanse): 20 damage.
* At the executor's real T8 breakpoint, Remote Farm had paid the Wings and FLOATED a {W}. The swap term
  of the projection (CountAttackGatherPump -> ApplyCombatAuraSwap on a copy of the main-phase board)
  paid the swap with that float and read 20 against 20 life (0 with the pool drained). Mana empties
  before combat (CR 500.4). The cut returned attack-only as the ONLY continuation, the committed
  `bp_choice=1` overran the one-entry list, nothing was applied, and combat found the swap unpaid.
  T8 win -> unwon.

The fix: both hosts apply the plan they would return on a copy and simulate combat (SimulateCombat,
the executor's lockstep combat) before skipping anything -- the go-off / equip-route siblings' rule. A
refused trial restores the frame, so the walk is exactly the no-cut walk. The cut is now an identity: it
fires only where the attack-only plan really kills. `MTG_LETHAL_CUT_VERIFY=0` restores the unverified
cut; `MTG_TRACE=lethalcut` prints every refusal. Unit: test_bruna_sweep.cpp, "Board-lethal cut: a lethal
projected with main-phase floating mana does not collapse the frame".

### Deferred: the projection's combat-swap term itself (not changed)

The verification makes the CUT sound; it does not make `pending_atk` right, and other readers still
trust it -- consider()'s `wins` (Solve's greedy ranking, EnumeratePlans' `wins_this_turn`, the
wins-first sorts) and SolveWithLookahead's "first winning candidate returns" loop. The swap term is
wrong in BOTH directions:

* **over-credit**: it pays the {2}{U} with main-phase floating mana (above);
* **under-credit**: it is computed once on the PRE-plan board, so a plan that ADDS the swap's mana
  (the Pilgrim + Greaves continuation above) is not credited with the swap it makes payable.

Draining the float on the projection's copy (mirroring SimulateCombat's CR 500.4 drain) fixes the first
and leaves the second, so on this very board it would flip the winning continuation's flag from
"wins" (right for the wrong reason) to "does not win". That is a play change with mixed signs on Bruna
(the only deck with an aura-swap card), so it needs its own held-out A/B and was not bundled with the
soundness fix. Measure with a default-OFF lever on Bruna's suite rows plus a large held-out sample.

Observed, not resolved: every SEARCH-side derivation of that T8 site-9 list ran on a board with no
combat-swap pin (`scripted_combat_aura_swap = -1`, projection 0), while the executor's ran with the
committed pin (= 1). Inferred, not proven: base plans write no pin and inherit the state's, so the two
lists' base-plan indices should agree -- unless the DERIVATION depends on the pin, which the unverified
cut did (with the cut verified, this game realises the scored continuation). The swap-pin VARIANTS are
built differently on a pinned vs an unpinned state (EnumeratePlansWithLand: NO-SWAP becomes the
variant), so a committed choice that indexes a variant is worth checking the same way.
