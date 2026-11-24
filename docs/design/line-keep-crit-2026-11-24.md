# MTG_LINE_KEEP_CRIT -- the line-critical land defect, fixed by the exact line-keep judge instead of a re-rank

Status 2026-11-24: implemented on `wip/bruna-crit2`, default ON (a payment that strands the declared line when a
hold-out of its own sources keeps it is a defect). Held-out A/B NOT yet run: manifest generator
`logs/bruna-crit2/mk_ab.py`. Adoption follows the A/B (USER bar below).

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

(filled in below)

## USER rulings in force

* "I think I would be willing to accept something that helps a lot of decks, but doesn't help a few deck and then look
  for ways to mitigate the cost on the other decks without blocking the adoption. Naturally, if the only reason it is
  worse on those other decks are the specific seeds we can just move on. d0 is not very important at all as I said
  previously. The primary thing that matters is play settings, though we should also check whether any worse case is a
  result of clairvoyant play. We can ignore those."
* "Play settings are the most important, so if the change was specific to Maze's end we shouldn't adopt." "(until we
  can show it is clairvoyance, variance or fix it)"
* Cut CPU; 3x is not a CPU licence.
