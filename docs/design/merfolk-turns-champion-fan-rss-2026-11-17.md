# Merfolk Turns: Wanderwine Prophets champion fan drives one game past 40 GB RSS (deferred, 2026-11-17)

**Status:** FIXED 2026-11-19 on `wip/merfolk-mem` (see "Resolution" below). The original diagnosis named the wrong
materialiser.

## Repro

```
build/Release/mtg "decks/Merfolk Turns/Merfolk Turns.cod" --profile "decks/Merfolk Turns/Merfolk Turns.profile.json" \
    --games 1 --seed 30560698 --game-index 560698
```

- **Peak RSS:** 42.8 GB within about 60 s on the source at `0d1fda100`. The pre-rebase binary of `e720dc68d`
  peaked at 43.0 GB, so this is not a regression from the 2026-11-17 Sea Creatures integration.
- **Turns:** with `--max-turns 5` the game finishes in 2.4 s at 69 MB; `--max-turns 6` passes 7.5 GB within 19 s.
  The runaway is the turn-6 search.
- **Impact:** in a pooled `--batch`, this game takes the process past `MTG_RSS_CAP_GB=40`, and the cap aborts every
  job. That happened three times in `build/scratch/claude_unseen_scarce_ab/` (games 560,000+), even at 12 threads.
  Games 530,000-530,999 run clean.

## Diagnosis

`MTG_AXIS_TRACE=1` on the turn-6 search (`MTG_RSS_CAP_GB=10 ... --max-turns 6`):

```
[axis-trace] pre-looktop             turn=6 plans=540     max_casts=6
[axis-trace] post-prophets-champion  turn=6 plans=154635  max_casts=6
[axis-trace] post-prophets-combat    turn=6 plans=154635  max_casts=6
  hand=Wanderwine Prophets|Three Tree City|Wanderwine Prophets|Rishadan Dockhand|
  bf=Island|Island|Aether Vial|Three Tree City|Rogue's Passage|Seahunter|Wanderwine Prophets|Deeproot Pilgrimage|
     Lightning Greaves|Tidal Warrior|Merfolk Sovereign|
```

- **The fan:** the Wanderwine Prophets champion axis multiplies 540 plans into 154,635, about 286x. Fans this size
  recur across many search nodes (hands with two Prophets dominate the trace).
- **Materialisation:** the 2026-11-14 combat-fan fix (`docs/design/analysis-Merfolk-Turns.md`, "Prophets combat fan
  memory fix") made the COMBAT variants lazy. CHAMPION variants are still materialised eagerly, because the combat
  axis branches from them. This board produces a champion fan large enough to exhaust memory on its own.
- **Candidate direction:**
  - Make the champion axis lazy too (the lazy-candidate layout with mixed-radix pins). Have the combat axis read
    champion variants through the same synthesis, not from a materialised list.
  - And/or fold champion choices that are provably interchangeable (identical Merfolk tokens or same-name
    creatures, whose exile and return are symmetric).
  - Either must keep the candidate order and pin values byte-identical, checked with
    `MTG_PB_LAZY_CANDS_VERIFY=1`, as the combat fix did.

## Not done

No code change. No owner assigned. Merfolk Turns analysis work belongs to the deck's analysis ledger; this note is
the standalone record.

## Resolution (2026-11-19)

The champion fan has been LAZY since `25e86c28a` (2026-11-14); the combat axis that materialises it before branching is
off on Merfolk Turns (`MTG_MERFOLK_PROPHETS_SAC_PRUNE`, default on). What materialised it was the **legend-keep turn
fan**, the last step of `AppendSubdecisionAxes`: whenever any plan was live (two Emperor Mihail II in the deck make it
live) it called `PbLazyMaterialize` on the whole layout and then appended a twin of every live plan. So each live m1
enumeration rebuilt the full champion fan and doubled it.

Evidence (seed 30000172 gi172, d5/b20, plain play):
- `MTG_ENUM_HIWATER_KB=200000`: m1 lists of 136K-705K plans (0.37-2.1 GB each) at turn 8.
- `perf record -e page-faults` on the Profile build: 70% of first-touch faults in `AppendSubdecisionAxes`'
  legend-fan push_back/insert (TurnSolver.cpp, the `lk_extra` loop), 20% in its `PbLazyMaterialize`.

Fix: a live lazy fan is twinned AS A FAN (the soulbond decline twins' shape): its prototype is stamped
`legend_keep_turn = 1` and a twin fan of the same kind, size and pins, carrying `legend_keep_turn = 2` and
`bp_wave0 = false`, goes in a block at the twins' position. `live_of` reads only `land_to_play`, `actions` and
`legend_keep_turn`, none of which a fan varies, so a clone is live exactly when its prototype is.

| | before | after |
|---|---|---|
| s30000172 gi172 peak RSS | 5,532,388 kB | ~459 MB |
| s30000172 gi172 wall (alone) | 59.6 s | 34.9 s |
| s30560698 gi560698 (this doc's repro) | 42.8 GB, aborted | 465 MB, 8.9 s, won T6 |
| Merfolk Turns 300 games (gi 0-299), 16 threads, process peak | 5,629,932 kB, 72.6 s | 1,278,200 kB, 54.8 s |
| six other decks (380 games), 16 threads, process peak | 1,924,032 kB | 1,877,248 kB |

Byte-identity: same digest and units on gi172; 90-job pooled batch (Merfolk Turns 300 games, NivMizzetCombo,
Hinata, Landfall Elementals, Unpredictable Cyclone, Merfolk, Goblins) digest-identical to the HEAD PGO binary;
`MTG_PB_LAZY_CANDS_VERIFY=1` over 28 jobs compared 43.9M list entries, 0 MISMATCH.
