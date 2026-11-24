# MTG_LINE_KEEP_CRIT -- the line-critical land defect, fixed by the exact line-keep judge instead of a re-rank

Status 2026-11-25: **ADOPTED default ON (USER 2026-11-25)**. The USER's ruling: *"If it is slightly better, more
correct in your understanding and doesn't have much CPU cost let's do it."* The held-out A/B (manifest generator
`logs/bruna-crit2/mk_ab.py`, binary on e8a2f7d64): play -1, other searched shapes +2, d0 0, every slower game recovers
at `--depth 8 --budget-ms 0`, CPU ~0 (+0.59% instructions). Landed via `wip/land-crit` on top of
`origin/phase-1-2-deck-analyzer` 05e2e0030.

**Composition with MTG_LKR_PLAYOUT_OFF (adopted 2026-11-24, after the A/B binary's base).** That cut skips the rung in
horizon playouts for every line with no reducer credit, which after the merge would have included the crit-mode
lines -- a shape neither A/B measured. On landing, the playout skip applies only to `kLineKeepFull` lines (the rock
rung it was measured on); a `kLineKeepCrit` line keeps its rung in playouts, as in this lever's A/B. Extending the cut
to crit lines is a one-condition change in `LineReducerKeepHold` (drop `g_line_keep_bound != kLineKeepCrit`) and would
need its own measurement.

## Why (USER 2026-11-24)

> "For Bruna, we shouldn't adopt with those results but we should come up with another way to fix the issue."

The record of the static re-ranks 7a / 7b is `docs/design/bruna-line-critical-levers.md` (branch
`wip/bruna-rule-override`). The defect: the greedy payer spends the LAST source of a colour the rest of the line still
owes on a pip another source could pay (Bruna s30051257 T2: Botanical Sanctum, the only {U}, pays Sol Ring's {1};
s30051968 T2: the Forest, the only {G}, pays it). 7a / 7b re-ranked such a source on EVERY payment that met it, so they
also changed payments where nothing was stranded, inside lines the search had valued: Niv gi537 (7b) and Dragonstorm
gi679 (7a) are real, non-clairvoyant losses at every depth through d8 b0, because payment is the standing greedy
exemption (not searched), so a payment-order change can make a line's win unfindable at any depth.

## The fix

The line-keep rung (`LineReducerKeepHold`, ManaPayment.cpp: trial-pay on a scratch board, judge the rest of the line
with the complete backtracker, and only on a strand re-pay with the fewest of the greedy's own sources held out, then
the oracle table's first keeping option) already is exactly "deviate from the greedy only when it provably strands the
current line". It reached reducer lines (MTG_LINE_REDUCER_KEEP), rock lines (MTG_LINE_KEEP_ROCK, ON since 2026-11-20)
and, parked default OFF by USER ruling, every multi-cast line (MTG_LINE_KEEP_ALL: Alchemist +3 / 36,000 at +3% CPU).

`MTG_LINE_KEEP_CRIT` adds a third binding mode. `LineKeepBound` now returns `kLineKeepFull` (KEEP_ALL / KEEP_ROCK:
every payment tried, unchanged) or `kLineKeepCrit` (any other line of two or more held casts). A crit-mode payment is
tried only past `LineKeepCritGate`: one battlefield scan, no scratch board -- some untapped unrestricted source of ours
makes a colour c the rest owes, the OTHER sources' units of c plus the float fall short of the rest's c pips plus the
bill's own, and the bill has a non-c pip. So the common payment does no copy; a payment past the gate pays one scratch
copy and the KEEP_ALL witness (the greedy pays the rest on the trial board), and only a real strand runs the repair.

What it changes: only a payment whose greedy version leaves the rest of the declared line unpayable on the board it
leaves, while some hold-out keeps it payable. Everything else is the greedy's payment byte for byte.

What it does NOT cover, by design: the two Bruna cases are ROCK lines, so on the current tree they belong to
MTG_LINE_KEEP_ROCK (see "Reproduction"); a ritual line's rest is judged without the ritual's float, so the joint test
refuses there (Dragonstorm gi679's board is unchanged -- unit test); a strand that needs two sources that are only
together the last of a colour is not gated (paid as today).

## Reproduction on origin (e8a2f7d64, src tree 7e8f9795)

| case | origin | with MTG_LINE_KEEP_ROCK=0 | reading |
|---|---|---|---|
| Bruna s30051257 T2 (`MTG_MANA_ORACLE=3`, the 11-21 repro) | APPLY-DROP 0 | 8 | FIXED on origin by MTG_LINE_KEEP_ROCK (a Sol Ring line) |
| Bruna s30051968 T2 | APPLY-DROP 0 | 3 | FIXED on origin by MTG_LINE_KEEP_ROCK |
| Jumbo d0 gi1447 (s1311447) | T4 | | not a defect of the shipped payer: 7a's own loss (4 -> 7) |
| Selesnya 2HG gi837 (s1850837, play) | T4 | | not a defect of the shipped payer: 7a's own loss (4 -> 5) |

So the two cases 7a / 7b were built for are already fixed: MTG_LINE_KEEP_ROCK went default ON 2026-11-20 22:30 and
the 7a / 7b record's boards are from before it. **Why the existing line-keep code did not cover the class in
general:** the rung binds only reducer lines, rock lines, and (parked) every line; a line-critical strand on a line
with NO rock and no reducer was out of reach (`LineKeepBound` returned false), and the payer itself has no line-aware
rule for it (the "no creature ranks ahead of a land" law; 7a / 7b were that rule). The unit fixtures show such strands
are real: three existing tests pinned a historical drop that this lever now repairs on its own -- Angels Remote Farm
`{Sol Ring, Righteous Valkyrie, Lightstall Inquisitor}` (with KEEP_ROCK pinned off), EDF Wild Growth + Cloud of Faeries
(the Coast, the only {U}), and EDF `{Training Grounds, Eldrazi Displacer}` (the viewer line check). Each now pins the
lever off for its historical arm.

## Checks (bin = 864969114 tree, Release, MTG_PGO not used)

* Units: `Line keep*` 16 / 16 (new test_line_keep_crit.cpp: binding modes, both Bruna boards under KEEP_ROCK, two
  no-rock strands repaired, two inert boards byte-identical, Dragonstorm gi679's Desperate Ritual payment unchanged).
  Full suite: 4,167 / 4,170 before the three fixture pins above (all three pass with the lever off).
* The 7a / 7b regression games, base vs crit, digest-identical at every shape: Niv gi537 d5 b20 (T3) and 2HG (T3),
  Dragonstorm gi679 d3 / d5 / play (T5), gi787 d0 / d3 / d5 (T6), gi1431 play (T6), Selesnya 2HG gi837 play (T4), Jumbo
  gi1447 d0 (T4), both Bruna seeds. At `--depth 8 --budget-ms 0` (both arms, one binary): Niv gi537 3 / 3, Dragonstorm
  gi679 5 / 5, gi787 6 / 6, gi1431 6 / 6, Selesnya gi837 4 / 4 -- identical.
* Play-setting slice (13 shapes x 12 games x 2 arms, seed 2690000): every game the same turn. Rung stats (on arm):
  gate 311,639 calls, 151,480 passed, 483 repairs (`[line-keep-crit]`, MTG_LINE_REDUCER_KEEP_STATS=1). CPU:
  `perf stat -e instructions:u` over the same slice, one process per arm: off 340.80 G, on 342.81 G = **+0.59%**
  (budgets are unit-bound, so this is the added work). Wall-clock cpu on this box (load ~36 on 32 cores) varied 2x
  between identical runs and is not usable for this.

## USER rulings in force

* "I think I would be willing to accept something that helps a lot of decks, but doesn't help a few deck and then look
  for ways to mitigate the cost on the other decks without blocking the adoption. Naturally, if the only reason it is
  worse on those other decks are the specific seeds we can just move on. d0 is not very important at all as I said
  previously. The primary thing that matters is play settings, though we should also check whether any worse case is a
  result of clairvoyant play. We can ignore those."
* "Play settings are the most important, so if the change was specific to Maze's end we shouldn't adopt." "(until we
  can show it is clairvoyance, variance or fix it)"
* Cut CPU; 3x is not a CPU licence.
