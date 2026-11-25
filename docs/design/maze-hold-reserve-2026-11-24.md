# Maze's End hold-up: the RESERVE mode and the cheap gate (2026-11-24, wip/maze-reserve; 2026-11-25, wip/maze-holdup)

Status (2026-11-25): the hold-up LIST is DEFAULT ON (`MTG_MAZE_HOLD_UP`, `=0` off) and the RESERVE mode is DELETED
(USER 2026-11-25, verbatim: "Okay, those ideas are all fine and it would be nice to cut the CPU if possible, but we'll
see whether that is practical."). Sections below "2026-11-25" are the history of the reserve work; the reserve rule,
`MTG_MAZE_HOLD_RESERVE`, `PaymentReserve`, `ReserveHoldOut` and their tests no longer exist. See "2026-11-25: list
default ON, reserve deleted, exact CPU cuts" at the end for the current state.

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
- ONLY WHILE A CONTINUATION CAN FOLLOW (commit 090ca0337): the spell being paid for opens a breakpoint (Sylvan
  Scrying / Archdruid's Charm tutor, Guild Summit, Exploration), an opener in hand fits in the rest of the line, or a
  Guild Summit of ours is out. Otherwise nothing is reserved (the rest of the line is all the turn casts).
- Gates Ablaze and lands spend nothing. Only coloured pips are reserved (the amount left does not depend on which
  sources paid; the colours do).
- Chain: drop the blue first, then one green at a time, then the blue alone: {G}{G}{U} -> {G}{G} -> {G} -> {U}.
- Payer (ManaPayment.cpp, ProviderHoldUpTargets / HoldOutTargets): each entry becomes the target rest + entry. An
  entry the board cannot pay with the bill on the payer's AMOUNT bound is passed over. The first entry the greedy's
  payment keeps ends the loop (the greedy's payment stands). Otherwise, for an entry the board can pay jointly, one
  of the greedy's sources is held out, then (reserve only) two together; the first that keeps the entry is the
  payment made. Never a refusal: the greedy pays when nothing keeps an entry.
- Held payments (ReserveHoldOut): only the greedy's sources that make a colour an open entry needs are candidates;
  singles, then pairs, each made ONCE and read against every open entry; the most preferred entry kept wins, ties to
  the earliest candidate (the same answer as trying candidates per entry).

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
| + reserve only while a continuation can follow (policy) | 379.70 | 39,646 | moves play |

### Final (v7 = 090ca0337), the 120-game slice

| arm | G instr | vs off |
|---|---|---|
| off | 1266.22 | - (identical games to 56f23f14c off) |
| list | 1336.42 | x1.0554 (identical games to 56f23f14c list, which was x1.0878) |
| reserve | 1369.99 | x1.0820 (v1 reserve was x1.1728) |

| reserve probe (decided, discarded; plays the off games) | 1385.80 | x1.0944 (0 differing lines vs off) |

v7 exactness: off and list identical to v1 / 56f23f14c (0 lines); VERIFY on v7: reserve kept-claims 1,108,686,
trial-cuts 513,021, infeasible-claims 579,274; list 1,652,718 / 1,531,323 / 383,632; all 0 MISMATCH, verify games
identical to the plain arms. Fluctuator / Prevent Damage / Gods (other.json, 30 games, VERIFY on): identical to
56f23f14c, claims 8,057 / 261 / 927, 0 MISMATCH.

Win turns on this slice (too small for a verdict): play 5.45 / 5.45 / 5.45, 2HG 5.125 all arms, d0 6.5 / 6.5 / 6.475.

## What remains

- The three-arm A/B: `logs/maze-reserve/reserve_ab_gen.py <out.json>` (14,400 jobs: 2,500 play d5 b20, 2,000 2HG,
  300 d0, each x off / list / reserve; seeds 29400000 / 29600000 / 29800000). Not run.
- Further reserve cost cuts, if the A/B says the reserve is worth keeping: the remaining work is the held payments
  (2HG: ~33k single + ~18k pair tries per game against ~4k fires) and the trial's pre copy + kept solves. Pairs could
  be limited to entries of two or more reserve pips; a candidate whose colours the paid board still has could be
  skipped (both policy, need their own check).

## 2026-11-25: list default ON, reserve deleted, exact CPU cuts (wip/maze-holdup)

USER ruling (verbatim, answering "Maze hold-up list default ON + delete the reserve mode?"): "Okay, those ideas are all
fine and it would be nice to cut the CPU if possible, but we'll see whether that is practical." Standing rule: a fix's
CPU cost is never justified by suite headroom.

- `5412ef79c`: `MTG_MAZE_HOLD_UP` DEFAULT ON (`=0` off, the A/B lever). Deleted: `MTG_MAZE_HOLD_RESERVE` and its heurarm
  slot, `DecisionProvider::PaymentReserve`, `MazesEndProvider::PaymentReserve`, `ReserveHoldOut`, `HoldPick`, the
  reserve branch of `ProviderHoldUpTargets`, and every reserve unit test. Kept: the exact gate (HoldSupply COVERS), its
  exact infeasibility half (HoldSupplyMax), and the VERIFY / PROBE / STATS diagnostics.
- `a68b2f2c3` (exact): the gate runs before the amount bound; a per-payment amount memo (PaymentManaCovers is monotone
  in need); PaymentTapped made lazily; COVERS(target, 0) on each held board before its solve; a payability-dominance
  skip (a target within a kept one is kept, one containing a dead one is dead); an insertion sort in
  `MazesEndProvider::PaymentHoldUp`.
- `5f2383600` (exact):
  - THE BOARD COPY. PayProviderHoldUp copied the whole GameState before every pay-first payment (2.0M on the slice)
    and read it on about 1 in 5. It now takes a `PayRefineSnap` (the field snapshot MTG_PAY_REFINE already undoes a
    successful payment with) when no payment can change a zone (`HoldUndoSafe`: no sac-for-mana, Deathrite or
    sac-outlet source of ours, no granted mana, every permanent defined; else the full copy as before). The board
    before the payment is made lazily (`HoldPre`), as a copy of the paid board with the snapshot restored, only when
    a target reaches the joint test or a held trial; a held payment restores the real board in place. Zone sizes are
    checked after the payment and the run aborts on a miss (a field restore could not undo it).
  - In-place solves (`HoldSolveInPlace`): the kept test on the paid board and the joint test on the made board solve
    on the board itself and are undone, instead of RampLaterPayable's copy each.
  - The held trial's solve consumes its own scratch board (no one reads it again).
  - The joint test's amount bound is skipped: ProviderHoldUpTargets' amount_covers already proved the same
    PaymentManaCovers query on the same board for every listed target.
  - `MTG_PROVIDER_HOLD_UNDO_VERIFY=1` (diagnostic, default off): a full copy beside every undo, compared with
    BoardsEqual (complete memberwise GameState equality) after it; prints `[provider-hold-undo] ... MISMATCH=`.
- `a2226f751` (exact): the undo snapshot's buffers are reused per thread (two allocations a payment).

### Measurements (the 120-game slice, perf stat instructions:u, --threads 2; off = 1261.15G)

| binary | on, G instr | vs off | overhead |
|---|---|---|---|
| 5412ef79c (list ON, reserve deleted) | 1331.62 | x1.0559 | 70.5G |
| a68b2f2c3 (cut2) | 1326.78 | x1.0520 | 65.6G |
| + undo instead of the pre copy (cut3) | 1326.49 | x1.0518 | 65.3G |
| + in-place solves, held consume (cut4) | 1325.48 | x1.0510 | 64.3G |
| + joint amount known (cut5 = 5f2383600) | 1324.71 | x1.0504 | 63.6G |
| + thread-local undo buffers (cut6) | 1324.18 | x1.0500 | 63.0G |

The off arm on cut5 is 1260.96G with identical games to the base off (0 digest / units diffs).

Exactness: every cut binary's on arm against a68b2f2c3's: 0 digest and 0 units diffs on 120/120 jobs; the
UNDO_VERIFY arms too. cut5 UNDO_VERIFY: 1,872,740 undo checks, 70,681 fired restores, 1,020,770 in-place solves, all 0
MISMATCH. Fluctuator / Prevent Damage / Gods (other.json, 30 games): identical to a68b2f2c3 with and without VERIFY;
12,048 undo checks, 429 fired restores, 6,395 in-place solves, 0 MISMATCH. Units 4184/4184.

THE BOARD COPY WAS NOT 24% OF THE OVERHEAD. The earlier flat profile put about 24% of the hold-up's samples in GameState
copies, but replacing the pre copy on 1.92M of 2.07M pay-first payments (148k stayed on the copy path) saved only 0.3G:
a copy-assign into the warm RampScratch pool slot costs little more than the field snapshot that replaces it. The
in-place solves saved another 1.0G for the same reason. The copies were spread over many small sites, and none was
large.

Where the remaining ~63G goes (cut5, perf record probe minus off, flat samples, ~11.2k overhead samples):
- the gate scans (ScanHoldSupply, HoldSupplyCovers, EffectiveProducesFor with Gond Gate's ControlledSubtypeColors
  walk, subtype checks): ~16%. Most of it is the pre-payment scan, which runs on every outermost payment with a target
  (7.9M entries; 5.9M end with no target);
- the backtracker solves (worker, ManaCacheKey, TapFlowInfeasible, colour feasibility): ~17%;
- the amount bound (UntappedManaUpperBound, SourceMaxNetLive, grants, scaled feeders): ~9%;
- ProviderHoldUpTargets / PaymentHoldUp themselves and their vectors: ~10%;
- the undo snapshot (SnapPayFields, Take / Restore): ~4%; the held payments (the payer): ~5%.

Ideas not done (each worth a few % of the overhead at most): derive the paid board's HoldSupply from the pre-payment
scan (a source stays counted unless the payment tapped it, cut its life below the tap's cost or drained its storage),
with a VERIFY against the real scan; a per-thread buffer for the target list and the provider's hold vector.

### Final A/B (2026-11-25; `logs/maze-holdup/ab_gen.py`, one pooled batch, 14,000 jobs, binary 5f2383600)

Hold-up ON vs OFF (`MTG_MAZE_HOLD_UP=0`), paired per seed. Play: 31100000+gi, d5 b20, 5,000 games. 2HG:
31300000+gi, life 30, two heads, 2,000 games. Net = slower - faster (negative = ON better).

| format | differ | slower | faster | net | mean win turn off -> on | units on/off |
|---|---|---|---|---|---|---|
| play | 220 | 13 | 17 | -4 | 5.5150 -> 5.5142 | 1.0023 |
| 2HG | 88 | 4 | 7 | -3 | 5.3445 -> 5.3425 | 1.0016 |

The cpu-ms ratios (play 1.066, 2HG 1.060 without its two tail games) were measured on a contended box. The slice's
instruction ratio (x1.050) is the clean CPU figure.

Slower games, each re-run at d8 b0 (both arms) and at play settings with `MTG_LINE_KEEP_CRIT=0` (both arms):
- 16 of 17 recover at d8 b0: play gi 525, 662, 1105, 1317, 1392, 1813, 2615, 3251, 3965, 4370, 4590, 4931; 2HG gi 660,
  1126, 1598, 1942.
- play gi 4785 (seed 31104785) does not: d8 b0 off 5 / on 6. The arms diverge at T2, with one green source (Black
  Dragon Gate): OFF casts Exploration, then two Manabonds on T3; ON casts Manabond. The divergence is the same at play
  and at d8 b0. MTG_NC_SEARCH=1 d8 b0: off 6 / on 6, identical games (same digest). Without clairvoyance OFF also wins T6, so its T5 was clairvoyant luck. This is a clairvoyant loss, not a hold-up defect.
- The line-keep interaction: hold-up ON skips the line-keep rung for any payment with a hold-up target
  (`!phu_pay_first`), so the A/B measures both effects together. None of the 17 traces to it. With the rung's
  `MTG_LINE_KEEP_CRIT=0` in both arms, every one keeps the same off/on gap.

Not hold-up specific: 2HG gi 1380 and 1647 take 20-33 min of wall in BOTH arms for only ~172k / ~60k units, while
ordinary games take ~10-30 s at 0.6-1.4M units. Something in them is expensive and not charged in units. Repro:
`--seed 31301647 --game-index 1647 --games 1`, life 30, two heads.
