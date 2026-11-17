# Merfolk Turns: Wanderwine Prophets champion fan drives one game past 40 GB RSS (deferred, 2026-11-17)

**Status:** found, diagnosed to the axis, NOT fixed. It is a run hazard: one game aborts a whole pooled batch on the
RSS cap.

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
