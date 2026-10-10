# Bruna: the recast Arcanum Wings is paid by the attacker it was meant to arm (executor vs search)

Status: **OPEN, deferred** (found 2026-10-10 while validating the Bruna width collapses; not fixed there).

## Repro

```
build/Release/mtg decks/Bruna/Bruna.cod --profile decks/Bruna/Bruna.profile.json \
  --cards-json src/cards/data/cards.json --seed 5764 --game-index 759 --games 1 \
  --depth 8 --budget-ms 0 --ignore-play-profile           # + MTG_FD_ORACLE=1
```

`[fd-diverge] seed=5764 realized_win=7 predicted_win=6`. Overnight seed block 5005, gi 759 (d3 b20 row:
GT T6, now T7). With `MTG_EQUIP_INERT_FOLD=0` the same game wins T6 at d8 b0 and at d3 b20.

## What happens

T5, board: Birds of Paradise #9 carrying Arcanum Wings, Birds #10, Somberwald Sage carrying Lightning
Greaves; lands Razorverge Thicket, Seaside Citadel, Botanical Sanctum, + the Forest drop (4 mana, two of it
blue); hand: Eldrazi Conscription.

* The search (FSLineWin, T5 d2 node) scores the plan `land=Forest; swap in Eldrazi Conscription` whose
  breakpoint continuation recasts the swapped-out Wings (`cont=[Arcanum Wings]`) as `after=10` -- Birds #9,
  now 10/11 with Conscription, attacks -- and projects the T6 kill. It ties with the same swap WITHOUT the
  recast (`cont=[]`, also `after=10`), and the tie goes to the recast line.
* The executor plays that line and pays the swap's {2}{U} with Razorverge + Sanctum + Forest, then the
  recast's {1}{U} with Seaside Citadel + **Birds #9** (`MTG_TAPDBG`). The 10-power flyer is tapped, nothing
  attacks, the kill slips to T7.

Four lands + two Birds pay both costs with the 0-power Birds #10 instead. The search's simulation leaves
Birds #9 able to attack (`after=10` is the opponent at 10 after combat), so it did not tap #9; the
executor's payer did. That is an executor/rollout lockstep gap in WHICH
creature pays a breakpoint continuation's cast (the attacker-aware preference the search applies is not the
one the executor applies at that point).

## Why it surfaced with the inert-equip fold

Without the fold, the old line had parked Greaves on Birds #9 with an idle main-2 move on T4 (a move the fold
now proves dominated and removes). In that state the search's own simulation of the recast line came out
`after=20` (no attack), so it took the swap without the recast and the executor matched it. The fold changed
the T5 board, the search now simulates the recast line differently from how the executor plays it, and the
pre-existing gap shows. Across the 725 searched Bruna suite games the `[fd-diverge]` count is 1 with and 1
without the width collapses (seed 3018, a different, land-light mechanism), so the fold does not create
divergences in bulk.

## What a fix needs

Find the two payment paths for the continuation cast (executor: AIEngine `cast_by_name` -> `TapForCost` after
the swap's own payment; search: `ApplyPlanDirect`'s continuation apply) and make the executor take the same
sources the simulation took -- or make both spare an attacker whose power exceeds what the cast adds, in one
shared helper. Then re-run this repro (must win T6 at d8 b0) and the Bruna rows (byte-identity of every other
deck is expected: only Bruna recasts an Aura swapper).
