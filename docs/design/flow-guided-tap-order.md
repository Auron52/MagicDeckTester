# Flow-guided tap order — PARKED (2026-08-16)

**Status: built, measured, REFUSED by the held-out seeds, parked behind a default-OFF flag.**
Nothing is broken and nothing is half-finished — the code is complete and the suite is green with it
off. What is missing is a *good* source ranking, not a mechanism. Self-contained; read this and you
can resume without any other context.

## The idea

`TapFlowInfeasible` (`src/core/SpellEffects.cpp`) proves a mana cost unpayable with a max-flow. But a
max-flow does not only answer yes/no — **the saturating flow IS an assignment of sources to colour
demands**. Today, on the ~88% of top-level payments that turn out to be feasible, that assignment is
computed and thrown away, and the backtracker then rediscovers the same answer by search.

So: publish the assignment, and use it to order the backtracker's loops.

## What is implemented (all committed, all behind flags)

| flag | default | meaning |
|---|---|---|
| `MTG_FLOW_ORDER` | **OFF** | publish the flow assignment and order the backtracker by it |
| `MTG_FLOW_SCARCITY` | ON *(only when the above is on)* | additionally bias which assignment the flow picks |

* The oracle records, per source, the battlefield index and the colours its S→src / src→colour edges
  actually carried flow on (`g_flow_src_mask`, `g_flow_col_mask`).
* At the top-level call the candidate list is stably partitioned so flow-used sources come first,
  **before** the identical-sibling dup-collapse chain is built (that chain's correctness argument
  depends on the iteration order, so the permutation must not happen after it).
* In the per-source branch loop, the flow's assigned colour for that source is tried first.
* All the bookkeeping is gated on the flag, so with it off the oracle is byte-identical to before.

## What was measured

**It works, mechanically.** Backtracker nodes, 12 games at profile depth:

| deck | nodes OFF | nodes ON | payable nodes/entry |
|---|---|---|---|
| fivecolour | 2,697,230 | 198,218 (**13.6x**) | 71.3 → 4.3 |
| hinata | 37,083 | 23,630 (1.57x) | 7.6 → 4.5 |
| mirrorwing | 93,606 | 80,959 (1.16x) | 4.4 → 3.6 |

4.3 nodes for a payment that taps ~4.5 sources means it walks straight to the answer.

**THE SOURCE SET ALONE IS NOT THE USEFUL HALF.** Ordering only the candidate list was a WASH
(+0.4% nodes) — the fan-out is sources × colours, and a five-colour source still branches over every
colour. The collapse above only appears once the per-source COLOUR assignment is used too. Anyone
retrying this should not repeat that half-measure.

**And it buys ~1% of runtime**, because the tap backtracker is only ~1% of engine cost. Paired CPU,
minima, arm order alternated: 16.64 vs 16.72 s at play depth; 80.47 vs 79.77 s over 300 games at
mulligan-gen settings while nodes fell 13.9M → 1.46M. See `engine-cost-profile-2026-08-16.md`.

**Adoption was refused on PLAY quality.** Net avg win turn vs baseline (negative = better), summed
over every case in the tier:

|  | smoke (s1001) | regression (s2002/3003) | TOTAL |
|---|---|---|---|
| flow order only | +0.0082 | +0.0330 | **+0.0412** |
| flow order + scarcity bias | −0.0550 | +0.0690 | **+0.0140** |

Both worse. The scarcity arm's −0.0550 on smoke looked like a clean win (9 configs faster, 1 slower,
mirrorwing faster at all three depths) and **reversed on the held-out seeds**. Only the sum decides.
Ground truth was deliberately NOT rebaselined; `test/regression_gt.txt` is untouched.

## The one real bug it surfaced, and its fix (kept)

The scarcity ranking scored a source by how few colours it makes. That demoted **Grove of the
Burnwillows** to least-scarce ({R}, {G} and {C}) — but Grove's coloured tap gives the opponent life,
which a Tainted Remedy / Plague Drone converts into DAMAGE. Anti-Lifegain *wants* that tap. Spending
basics instead threw the damage away and cost a hand-played reference its turn-4 kill
(`references/Anti-Lifegain/claude_s5_gi4.json` replayed T4 → T5).

Fixed by ranking a live drip source (`tap_opponent_lifegain > 0` and `OpponentLifegainUseful`) as
maximally scarce so the flow spends it FIRST. Reference replays T4 again. Two things to know:

* Grove is the **only** drip card in `cards.json`, and only Anti-Lifegain runs it.
* The fix lives inside the scarcity sort, so it is **inert while `MTG_FLOW_ORDER` is off**.

This is also the clearest evidence that the reference gate earns its keep: it caught a substantive
play bug, not a stale recording. The human's plan was still enumerated and still chosen (content
anchoring correctly remapped index 6 → 9, same three casts in the same order) — only the payment
differed.

## Where to resume

The hypothesis, **untested**: the scarcity rank scores a source purely by colour count, so it is
blind to every other tap side effect — exactly the Grove mistake, in other clothes. The held-out
losses were on hinata and mirrorwing, and hinata runs filter lands that consume floating mana
(Cascade Bluffs, Ferrous Lake, Izzet Signet); Karoo-style lands have the same shape. Neither deck
runs a drip land, so the drip fix cannot explain those losses.

Suggested route, in order:

1. Instrument which payments change assignment under the bias, and check whether the changed ones
   concentrate on filter / Karoo / other side-effect sources. **Measure before writing a rank rule** —
   three separate things looked like wins in this project and none were.
2. If they do, extend the rank the way the drip was extended: encode what the engine already values,
   rather than inventing a preference.
3. Acceptance: net avg win turn must improve on **both** seed sets summed (smoke AND regression), and
   `viewer_protocol_check.py --strict` must stay at 0 play-drift. Only then touch ground truth.

Reproduce any of the above with:

```bash
bash build.sh
# node counts
MTG_TAP_STATS=1 MTG_FLOW_ORDER=1 ./build/Release/mtg decks/FiveColour/FiveColour.cod \
  --profile decks/FiveColour/FiveColour.profile.json --games 12 --seed 1001 --threads 1
# three-arm play A/B (MTG_FLOW_ORDER=0 | 1 | 1 with MTG_FLOW_SCARCITY=0) over a tier's manifest
./build/Release/mtg --batch test/logs/<mode>/manifest.json --threads 0 \
  | grep -oP '^\S+: played=\d+ avg=[\d.]+'      # NB: never compare ms= -- wall time is not a result
```

## Before spending more time here, read this

Even a perfect tap order buys ~1% of runtime. `engine-cost-profile-2026-08-16.md` measures the real
distribution and names byte-identical work worth 18.4% on the FiveColour gen workload. This is parked
because it is *small*, not because it is *hard*.
