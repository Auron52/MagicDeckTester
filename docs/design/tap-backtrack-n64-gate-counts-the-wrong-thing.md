# The n>64 gate counts the WRONG THING — root cause of the mixed-class blow-up (2026-08-16)

**Status: root-caused and measured. Fix 1 written but NOT YET VALIDATED. Fix 2 designed, not written.**
Answers the open question in [[tap-backtrack-mixed-class-sighting]] (the other agent's Mirrorwing
handoff). Self-contained.

## The finding in one line

`n` is `state.battlefield.size()` — **every permanent on the table** — but the failure memo it gates
only ever indexes **the active player's tappable mana sources**. On the pathological boards those two
numbers differ by 3x, so the memo is switched off by permanents it would never index.

Measured on Mirrorwing (`--max-turns 12`, gen settings), at the moment the gate trips:

```
[bf>64] total=105  MINE=105 (tokens 87)  OPPONENT=0  tappable sources=33
          63 x Treasure Token        16 x Ignoble Hierarch     4 x Mirrorwing Dragon
          10 x Elvish Mystic          2 x Goblin Instigator    1 x Zada + 2 Goblin tokens
           8 lands                    turn=4   opp life=20
max battlefield n=105   max ACTIVE-PLAYER SOURCES=33
top-level entries with bf>64: 1      with sources>64: 0
```

**60% of the board is Treasure tokens** minted by Zada-copied Gold Rushes. A Treasure is a
sac-for-mana ACTION, not a source (`AvailableManaPool` deliberately excludes it), so not one of those
63 appears in the 33-entry `cands` list — yet each one pushes `n` past 64. The source count never came
close to the limit: **0 entries** in that whole run had more than 64 sources.

## Why it costs so much

The `n > 64` guard disables the memo. And because the flow-prune oracle was gated on
`top_level && fail_memo && FlowPruneEnabled()`, and `fail_memo` is null exactly when `n > 64`, **the
guard silently disabled the infeasibility ORACLE too** — on precisely the boards that need it.

That is the whole cost, and the numbers are stark. Proving a cost UNPAYABLE:

| | nodes / entry |
|---|---|
| Mirrorwing, oracle live (n <= 52) — measured here | **1.0** |
| Mirrorwing, memo-off cells — the handoff's two repros | **3,692** and **12,644** |

Same deck, same half of the work, three to four orders of magnitude apart, and the only difference is
whether the oracle ran. The handoff's finding that "99.7–99.9% of all nodes go to proving costs
UNPAYABLE" is the signature of the oracle being absent: where it is live, that half is ~1 node.

Note this also means the handoff's proposed fix #2 ("cheap unpayable precheck… rejects in O(n)
instead of thousands of nodes") ALREADY EXISTS, ships default-on, and was merely being switched off.

## Fix 1 — decouple the oracle from the memo (WRITTEN, NOT VALIDATED)

Three edits in `TapForCostBacktrackWorker` / `TapFlowInfeasible`:

1. gate the oracle on `top_level && FlowPruneEnabled()` — it examines all untapped sources fresh and
   needs nothing from the memo;
2. null-guard its single `fail_memo->insert(key)` (its own comment already calls it "harmless at top
   level");
3. **fix a real UB the change exposes**: `reserved_mask & (1ull << i)` is undefined for `i >= 64`,
   which is exactly the range being enabled. Treat `i >= 64` as unreserved — sound in the safe
   direction, since crediting a reserved source only ADDS supply and the oracle only claims
   INFEASIBLE.

`MTG_FLOW_PRUNE_MEMO_GATED=1` restores the old behaviour from one binary for A/B.

## Fix 2 — gate on the SOURCE count, and key the mask by source index (DESIGNED)

The real fix, and the one the measurement above argues for. Replace battlefield indexing with
`cands` (source-list) indexing:

* gate the memo on `cands.size() <= 64` instead of `n <= 64`;
* build the top-level `tapped_mask` by scanning `cands` and setting bit `ci` (the source's POSITION in
  `cands`) when `battlefield[cands[ci].first].tapped`;
* in the recursion, OR in `1ull << ci` using the loop position rather than the battlefield index.

**Why it is byte-identical.** Within one top-level call the mask is only ever compared for equality,
and position-in-`cands` is a bijection onto the set of tapped sources, so exactly the same states
collide and exactly the same proven failures are pruned. The old mask additionally set bits for the
active player's tapped NON-source permanents; those cannot change during a payment (nothing enters or
leaves, and the recursion only taps sources), so they were constant within a call and dropping them
changes no comparison.

With this, a 33-source board keeps its memo no matter how many Treasures are lying around.

## Validation still owed

1. `bash test/regression.sh --smoke` then the regression tier — both must stay **byte-identical**
   (the oracle only ever refuses provably-unpayable costs, so play must not move). Ground truth must
   NOT be rebaselined; if a digest moves, the change is wrong.
2. The handoff's two deterministic repros for the real before/after. **They need
   `decks/Mirrorwing Dragon/Mirrorwing Dragon.keepmodel.gencache.json`, which is gitignored and absent
   from a fresh tree** — without it the command runs full equivalence discovery instead of the fast
   replay (this cost a wasted probe). Generate the cache once, then the 11.3 s and 45 s repros work.
3. Ordinary Mirrorwing games reach `n > 64` only once in ~75k top-level entries, so the effect is NOT
   measurable from normal goldfish runs — it needs the keep-rollout context.

## Separate finding, NOT a mana problem — and it is a big one

That same board is **turn 4, opponent at 20 life, ~35 creatures, 63 Treasures**, and the game is
still going. It should be over several times over, because of what Gold Rush actually says:

```
Gold Rush {1}{G}: Create a Treasure token. Until end of turn, up to one target creature
                  gets +2/+2 FOR EACH TREASURE YOU CONTROL.
                  params: creates_treasures 1, pump_per_treasure_power/tough 2
Zada, Hedron Grinder: copy an instant/sorcery targeting only Zada, for EACH other creature.
```

With 63 Treasures on board the pump is **+126/+126**, and Zada copies it onto every creature. The
Treasures are themselves minted by the copies, so the pump escalates as the chain resolves — the last
copy in that state is pumping +126/+126. Against 20 life, with ~35 creatures, any single attack step
ends it by an enormous margin.

So the engine assembled a board that is lethal many times over and did not convert. Whatever the
cause — not attacking, or valuing "make another Treasure" above "win now" — it is a play/evaluation
defect, not a mana defect, and fixing the memo gate will make the search reach these states FASTER
rather than avoid them. Worth its own investigation, and probably worth more than the mana fix:
degenerate cells are expensive precisely because the game does not end.

A caveat before anyone chases it: this state was sampled inside a SEARCH ROLLOUT, not necessarily
from the line actually played (that run's avg win turn was 5.05 over 60 games). Confirm it appears in
committed play before treating it as a shipped misplay.

## FiveColour

Not affected by this gate: max board n=20, `memo-off(n>64) top-level=0`, and its unpayable half
already costs 1.6 nodes/entry. FiveColour's degenerate cells are slow for a different reason — the
search simply does ~30x more work (rollout calls 27x, interior nodes 32x, CPU 36x from a typical to a
degenerate game), with every component scaling together.
