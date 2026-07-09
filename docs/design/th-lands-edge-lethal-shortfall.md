# Treasure Hunt — Land's Edge lethal held a turn too late (s1: AI T4 vs human T3)

**Status:** OPEN / deferred. Root-caused to the `LandsEdgeHeuristicFireCount` +
land-play interaction; fix not yet written. This doc is self-contained — pick it
up cold.

## The game

Reference `references/treasure_hunt/claude_s1_gi0.json` — the human won **T3**;
the autonomous AI wins **T4** on the *same opening hand*. It is a genuine play
shortfall (a searched result the engine does not find), not a mulligan
difference and not budget churn.

Reproduce (forced to the human's kept hand — the ref has no mulligan, so `0:`):

```bash
MTG_DUMP_WINS=1 ./build/Release/mtg decks/treasure_hunt.txt \
  --profile decks/treasure_hunt.profile.json \
  --games 1 --seed 1 --game-index 0 --depth 5 --budget-ms 4000 \
  --threads 1 --lookahead-bottoming --force-mulligan "0:"
# -> wt=4 at b200/b1000/b4000 (persists; not churn)
```

**Persists under `MTG_UNPRUNED=1` too** (b2000/b8000 still T4) — so this is NOT a
fetch/tutor/alt-payload pruning gate. The lethal line is pruned *upstream of the
search* by the Land's Edge fire-count heuristic (which `MTG_UNPRUNED` does not
gate), so the search never sees "discard 10 lands for 20 this turn".

Opening hand: `Ferrous Lake, Island, Sandstone Needle, Land's Edge, Cascade
Bluffs, Lonely Sandbar, Treasure Hunt`.

## The combo (deck mechanic)

- **Land's Edge** (enchantment, `{1}{R}{R}`): *"Discard a land card: Land's Edge
  deals 2 damage to any target."* — each discarded land = 2 damage. Modelled as
  `Action::Kind::DiscardToLandsEdge` (see `src/main.cpp` `landsedge` count).
- **Treasure Hunt** (`{1}{U}`): reveals from the top until a nonland, puts all
  revealed into hand — refills the hand with lands (= Land's Edge ammunition).
- Lethal from 20 life = discard **10 lands** (10 × 2). `rate` = 2.

## The AI's actual line (T4 win)

```
T2  PLAY_LAND Ferrous Lake; CAST Land's Edge {1}{R}{R}
T3  PLAY_LAND Saprazzan Skerry; CAST Treasure Hunt {1}{U}  (draws 4 lands + a Treasure Hunt)
    fires Land's Edge x3  -> opp 20->14   (only 3 lands discarded)
T4  PLAY_LAND Sandstone Needle; CAST Treasure Hunt; fires x7 -> opp 14->0
```

The human instead reaches ~10 discardable lands on **T3** and fires for the full
20. The AI is left **1 land short of the 10-land lethal on T3** because it
**takes its T3 land drop** (Saprazzan Skerry), spending a land that would have
been Land's Edge ammunition — dropping `lands_in_hand` from 10 to 9.

## Root cause — the heuristic

`LandsEdgeHeuristicFireCount(state, rate)` in `src/core/SpellEffects.h` (~line
43), the sole heuristic behind `TreasureHuntProvider::LandsEdgeFireCount`
(`src/ai/DecisionProviders.cpp:842`), consumed by
`AIEngine::ActivateLandsEdge` / `DoActivateLandsEdge` (`AIEngine.cpp:2963/3000`):

```cpp
int lethal_lands = (opp.life + rate - 1) / rate;      // ceil(20/2) = 10
if (lands_in_hand >= lethal_lands) { return lands_in_hand; }   // go for the kill
int excess = max(0, hand.size() - max_hand);          // else only dump overflow
return min(excess, lands_in_hand);                    // ...and HOLD the rest
```

Policy: **fire everything only if we already hold lethal-many lands; otherwise
fire only the cards we'd discard to hand size anyway and hold the rest.** Sound
in general (don't waste ammo early), but it evaluates `lands_in_hand` *after* the
land drop is taken. The land drop is what pushes us from 10 → 9, so the
`>= lethal_lands` branch never triggers on T3 and we hold — winning T4.

So the real lever is the **land-play decision**, not the fire count: when Land's
Edge is in play and *not* taking the land drop leaves us at `lands_in_hand >=
lethal_lands`, the extra land in play is worth less than the 2 damage it deals as
ammo. The AI greedily plays a land every turn.

## Fix directions (for the next session — measure, don't assume)

This is a **heuristic** interaction, not a rules/modeling bug (confirm against the
mtg-rules skill first, but Land's Edge/Treasure Hunt appear modelled correctly —
the AI *can* execute the T3 kill, it just doesn't choose to). So it belongs in
`TreasureHuntProvider` (deck-specific), behind an A/B env, adopted only after the
regression suite validates it (see `.claude/skills/heuristic-optimization.md` and
`regression-testing.md`).

Candidate levers, cheapest first:

1. **Hold the land drop when it enables Land's Edge lethal this turn.** Before
   taking a land drop, check: is Land's Edge in play, and would keeping this land
   (plus lands already in hand, minus what mana we still need to cast this turn's
   spells) reach `lethal_lands`? If so, don't play it — it is ammo, not a land.
   The tricky part is the **mana constraint**: T3 still needs `{1}{U}` for
   Treasure Hunt, so "hold the land" is only correct when we can still cast the
   turn's spells from lands already in play. Model that explicitly.

2. **Make `LandsEdgeHeuristicFireCount` land-aware of the pending drop** — i.e.
   count the land drop we *could* skip as available ammo, so the `>= lethal`
   branch sees 10 and the executor then also skips the drop. (Same fix, expressed
   in the fire count instead of the land-play; keep them in lockstep — the real
   engine and the rollout must agree, per the existing lockstep comments around
   `DoActivateLandsEdge` / `SelectCleanupDiscardIndex`.)

3. **Search-side:** if instead you want the search to discover it, the fire-count
   heuristic must stop hard-capping the discard count below lethal (it prunes the
   line before the search sees it — that's why `MTG_UNPRUNED` doesn't help). Add a
   Land's-Edge unpruned gate that offers "fire up to hand size" so the search can
   evaluate the lethal. Heavier; the deck's whole point is this combo, so a
   targeted heuristic (1/2) is likely enough and cheaper.

## Validation checklist (before adopting any fix)

- `--force-mulligan "0:"` on seed 1 gi 0 → **T3** at d5 (the target).
- Suite A/B: `bash test/regression.sh --smoke` then `--` (regression); TH is the
  only deck that should move; **inert decks stay byte-identical PASS**. Read the
  auto-audit: every searched turn-later must be variance/churn, zero searched
  `win→loss` (`.claude/skills/regression-testing.md`).
- Confirm no over-firing regression: a hand that is *not* near lethal must still
  hold its lands (don't dump ammo early / into a stall).
- `--accept` smoke + regression, commit code + GT together.

## Pointers

- Heuristic: `src/core/SpellEffects.h::LandsEdgeHeuristicFireCount`
- Provider hook: `src/ai/DecisionProviders.cpp` `TreasureHuntProvider::LandsEdgeFireCount` (842), and its land-play hooks (search `TreasureHunt` in that file)
- Executor: `src/ai/AIEngine.cpp::ActivateLandsEdge` (2963) / `DoActivateLandsEdge` (3000)
- Discard-victim lockstep: `SpellEffects.h::SelectCleanupDiscardIndex`
- Log fields: `src/main.cpp` `landsedge` action count (407–430)
- Related prior TH work: `docs/design` + the cleanup-discard-divergence fix (commit 54f691c) which already fixed a rollout-vs-real discard mismatch that over-counted Land's Edge floods — read it; this fix must not reintroduce that divergence.
