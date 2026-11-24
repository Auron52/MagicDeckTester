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

(filled in below as measured)
