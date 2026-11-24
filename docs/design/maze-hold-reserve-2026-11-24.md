# Maze's End hold-up: the RESERVE mode and the cheap gate (2026-11-24, wip/maze-reserve)

Status: built, DEFAULT OFF, awaiting the three-arm A/B (off / list / reserve) and the USER's call.

## USER guidance (verbatim, 2026-11-24)

- "Green seems to be the biggest concern here. Summit I imagine we often would play up front avoiding the need for
  breakpoints to open? Holding green seems important."
- "I'm not clear whether the rule need to change. Just a thought from my end, but I guess you have a point that a summit
  could be drawn. If your rule works well then that is fine."
- "It is probably one of those cases where we hold a couple green first and then 1 blue after if we don't know what we
  will have in hand. Obviously with knowledge of the hand we can adjust that a bit."
- "And feel free to tweak it slightly if it needs it. That was just an idea that occurred to me, but the real question
  is what we find works in practice."
- On CPU: "if there is a major problem there we should probably fix the performance first"; "cut CPU; 3x is not a CPU
  licence".

Gate data (1,928 APPLY-DROP need-later flags, dropped cast): Guild Summit 586, Sylvan Scrying 381, Exploration 221,
Burgeoning 183, Archdruid's Charm 171, Manabond 147.

## The three arms (one binary, per-job heurarm)

| arm | flags | what the payer keeps payable after an outermost payment |
|---|---|---|
| off | `MTG_MAZE_HOLD_UP=0` (default) | nothing beyond the greedy |
| list | `MTG_MAZE_HOLD_UP=1` | each hand spell's cost alone, cheapest first (rest of the line raised to it) |
| reserve | `MTG_MAZE_HOLD_UP=1 MTG_MAZE_HOLD_RESERVE=1` | ONE joint reserve beyond the rest of the line, as a fallback chain |

## The reserve rule (MazesEndProvider::PaymentReserve)

- GREEN: the hand spells' green pips the rest of the line does not already pay for, plus two ("a couple green") while
  the hand can still grow this turn and a green spell remains in the library. At most two; three with a {G}{G}{G}
  card (Archdruid's Charm) in hand.
- BLUE: one, for a Guild Summit in hand the line does not cast, or while the hand can grow and a Summit remains in the
  library.
- The hand can grow this turn only through Guild Summit's draws: a Summit ours on the battlefield, in hand, or being
  paid for. Otherwise the hand is known and a colour nothing in it can spend is not held.
- Gates Ablaze and lands spend nothing. Only coloured pips are reserved (the amount left does not depend on which
  sources paid; the colours do).
- Chain: drop the blue first, then one green at a time, then the blue alone: {G}{G}{U} -> {G}{G} -> {G} -> {U}.
- Payer (ManaPayment.cpp, ProviderHoldUpTargets / HoldOutTargets): each entry becomes the target rest + entry. An
  entry the board cannot pay with the bill on the payer's AMOUNT bound is passed over. The first entry the greedy's
  payment keeps ends the loop (the greedy's payment stands). Otherwise, for an entry the board can pay jointly, one
  of the greedy's sources is held out, then (reserve only) two together; the first that keeps the entry is the
  payment made. Never a refusal: the greedy pays when nothing keeps an entry.

## The cheap gate (exact; HoldSupply in ManaPayment.cpp)

One battlefield scan, no copy: Hall's theorem on a deliberately smaller supply (our untapped feed-free lands, rocks and
ready dorks, one unit each of their current colours). COVERS(target, k): for every set S of the target's colours,
counted sources making a colour in S, minus k, cover the target's pips in S, and the total covers the total. A payment
of the bill takes at most k = MV + sum over untapped scaled lands of (feed - 1) counted sources, so COVERS(target, k)
on the board before the payment proves the greedy keeps the target (the trial's first test) and the trial is skipped
(list: the target is dropped; reserve: the chain is cut there). COVERS(target, 0) on the paid board proves the kept
test without its copy and solve. Applies to the list mode too (Fluctuator, Prevent Damage, Maze list arm).
`MTG_PROVIDER_HOLD_GATE=0` removes it; `MTG_PROVIDER_HOLD_VERIFY=1` (diagnostic) re-checks every claim with the real
solve and prints `[provider-hold-gate] ... MISMATCH=` at exit.

## Results

### Step 1 (owed from the previous agent, on 56f23f14c)

- `mtg-test -tc="*Maze*,*payment*,*pay*,*prepay*,*Fluctuator*"`: 645 / 645 passed.
- Fluctuator / Prevent Damage / Gods identity (other.json, 30 games): base 50f807e9c vs cut 56f23f14c, 0 differing
  played= lines (ms= / cpu= stripped).

### Instructions (perf stat instructions:u, --threads 2, the 120-game slice: 40 play d5 b20, 40 2HG, 40 d0)

| binary / arm | G instr | vs off |
|---|---|---|
| 56f23f14c off / list | 1264.80 / 1375.90 | x1.0878 |
| v1 (0b90cf1e2) off | 1265.93 | - (identical games to 56f off) |
| v1 list (gate on) | 1337.63 | x1.0566 (identical games to 56f list) |
| v1 list, MTG_PROVIDER_HOLD_GATE=0 | 1377.13 | x1.0878 |
| v1 list probe (decided, discarded) | 1339.78 | x1.0583 |
| v1 reserve | 1484.74 | x1.1728 |
| v1 reserve, gate off | 1529.47 | x1.2082 |
| v1 reserve probe | 1483.58 | x1.1719 |

The gate removes 35.5% of the list mode's overhead (111.2G -> 71.7G) and 17% of v1 reserve's.

### Exactness of the gate

- Probe arms (hold-up decided then discarded) vs off: 0 differing played= lines (list and reserve).
- Gate ON vs OFF (MTG_PROVIDER_HOLD_GATE=0), same arm: 0 differing lines (list and reserve, 120 games each).
- MTG_PROVIDER_HOLD_VERIFY=1 (every claim re-checked with the real solve; play identical to the gate arm):
  reserve kept-claims 2,011,056 / trial-cuts 1,029,483, list kept-claims 1,652,718 / trial-cuts 1,531,323: 0 MISMATCH.
  The infeasibility half (v3+): 108,551 claims on 10 play games, 0 MISMATCH.

### Cutting the reserve's cost (2HG, 10 games, the dearest format; off = 353.25G)

| step | G instr | fired | games vs previous |
|---|---|---|---|
| v1 | 424.46 | | |
| + infeasibility gate (exact) | 424.74 | | identical |
| + one held payment read against every open chain entry (exact restructure) | 419.22 | 57,868 | identical |
| + candidates = only greedy sources making a colour an open entry needs | 408.76 | 57,868 | identical (0 lines) |
| reserve-first (hold the sources a solve of the entry takes) | 408.76 | 17,420 | REJECTED: fires 3x less, no cheaper |
