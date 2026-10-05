# Site-9 continuation: executor/rollout index mismatch (deferred, 2026-10-05)

Status: **OPEN, deferred.** Found while fixing the Bruna Stage-5d claude-play sweep findings
(`docs/design/analysis-Bruna.md`, "Claude-play sweep", finding E). Not reachable on the shipped
default flags over the Stage-5a seed set (2,400 games, 0 `[fd-diverge]`); reproduced only with the
experiment lever `MTG_ROLLOUT_AURA_SWAP=1`, whose changed trajectories reach it.

## Repro

```
./build.sh
MTG_ROLLOUT_AURA_SWAP=1 MTG_FD_ORACLE=1 ./build/Release/mtg decks/Bruna/Bruna.cod \
    --profile decks/Bruna/Bruna.profile.json --games 1 --seed 4205 --game-index 201 \
    --depth 5 --budget-ms 20
# [fd-diverge] seed=4205 realized_win=6 predicted_win=5 proven_at_turn=5 leaf_est=none
```

(Batch form: job `seed 4004, games 300, depth 5, budget_ms 20`, game index 201.)

## What happens

Turn 5, committed plan `[Razorverge Thicket] Glittering Wish (tutor_choice 1 = Almost Perfect),
Arcanum Wings -> Avacyn's Pilgrim #3`, `bp_choice = 1 @ 0`. The FD line's continuation is
`[Aura swap: Almost Perfect]`; with it the two Pilgrims stay untapped and attack for exactly lethal.

The executor reaches breakpoint SITE 9 (post-entry activation: the freshly cast Wings' {2}{U} swap)
and, by design, takes `EnumerateBreakpointPlans(state, ...)[plan.bp_choice]` -- an INDEX into a list
it rebuilds from its own board. Its list was

```
#0 Lightning Greaves + swap(Almost Perfect)
#1 Lightning Greaves
#2 swap(Almost Perfect)
```

so index 1 cast Lightning Greaves, which (with the swap) tapped both Pilgrims for mana: no attack,
realised T6. The scoring rollout's list at the same index named the swap alone -- its board at that
point differed (dumped rollout boards for this plan show both Pilgrims already TAPPED by the batch
prepay, float 3, so "Greaves + swap" was unaffordable there and dropped out of the list). The
executor's prepay held the Pilgrims (dork reserve, `reserved_crea` = both) and tapped the lands.

So the defect class is: **a site-9 continuation is index-addressed, and the index is only
meaningful if both worlds build the list on the same board -- which the whole-turn prepay's
reservation does not guarantee across the scoring rollout and the executor.** Site 9 records no
breakpoint script (`bp_sink_push` is not called there), so the executor cannot fall back to
replaying the scored actions the way the deferred sites (3/5/6) do.

## Not yet established

* Why the scoring rollout's prepay tapped the Pilgrims for the same (state, plan.actions) the
  executor prepaid without them. The rollout boards printed for this plan were not byte-equal to
  the executor's (e.g. the Thicket entered TAPPED in them -- three other lands), so the scoring
  board itself may be a different node (FSLine verification at an earlier root), not a prepay
  divergence on one board.

## Candidate fixes (none built)

1. Record the site-9 continuation into the plan's breakpoint script in the rollout (as the deferred
   sites do) and have the executor REPLAY it when the plan is committed, instead of re-enumerating.
2. Or key the continuation by CONTENT (a fingerprint of the scored continuation) rather than index,
   standing the site down when the executor's list does not contain it.

Either needs the mismatch harness (`MTG_FD_ORACLE=1 MTG_FLAG_NONCONV=1`, Stage-5a seed set) with
`MTG_ROLLOUT_AURA_SWAP=1` to read 0 before that lever can be considered for adoption.
