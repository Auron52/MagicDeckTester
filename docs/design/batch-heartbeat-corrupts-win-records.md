# The batch heartbeat can corrupt a `[win]` record, and it voids the whole run's report

**Status:** open, unfixed. Found 2026-09-16 during the StompySurprise list convergence.
**Severity:** a 1.6M-game, 65-minute screen produced no report. The data was fully recoverable, but
only because the failure was noticed and hand-repaired.

## What happens

`mtg --batch` writes two kinds of line to the same stream from different threads:

* one `[win] job=<name> gi=<n> wt=<turn>` per finished game (under `MTG_DUMP_WINS=1`, which
  `scripts/deck_compare.py` sets on every screen), and
* one `[batch] heartbeat: N/M workers busy ...` line every 10 minutes.

These are not synchronised against each other. A heartbeat emitted while a win record is being
written interleaves into it. The observed corruption, from
`logs/deckcmp/StompySurprise/confirm.screen.err` line 1056274:

```
[batch] heartbeat: 32/32 workers busy (100%)[win] job=1v1::f15_wurm1 gi=56277 wt=4
```

Both payloads are intact; they are simply on one line instead of two.

## Why it voids the run

`deck_compare.py:374` parses win records with

```python
m = re.match(r"\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)", line)
```

`re.match` anchors at the start of the line, so a record preceded by heartbeat text does not match
and is silently dropped. That one missing game then trips the (correct, and load-bearing) short-job
guard at `deck_compare.py:378`:

```
the batch did not finish every game -- refusing to report a number.
  1v1::f15_wurm1           99,999 of 100,000
```

The guard is doing exactly its job — CLAUDE.md's "a truncated run reads as a result" rule is why it
exists, and it should NOT be relaxed. The bug is upstream of it.

**Note the asymmetry that makes this worth fixing rather than living with:** the guard only fires
because `expect` is known. A corrupted record in any context where the expected count is *not*
pinned would silently bias a mean instead of refusing — the same failure mode the `wt=-1` regex
comment at `deck_compare.py:365` warns about.

## Recovery without re-running (proven)

The raw log retains both payloads, so the run is salvageable. Split the mangled line into a repaired
**copy** (never edit the original — it is the raw record for diagnosis), then score the copy with
deck_compare's own functions so the `wt=-1 -> max_turns+1` rule and the paired statistics are the
shipped ones, not a reimplementation:

```python
for line in open(src):
    if not line.startswith("[win]") and "[win] job=" in line:
        head, tail = line.split("[win] job=", 1)
        out.write(head.rstrip("\n") + "\n"); out.write("[win] job=" + tail)
    else:
        out.write(line)

import deck_compare as dc
spec = dc.Spec("<spec>.json")
got = dc.score(repaired, [spec.jname(f, t) for f in spec.formats for t in spec.arms],
               spec.maxturn, expect=spec.games)
d, se, n, ident = dc.paired(got, spec.jname(fmt, "base"), spec.jname(fmt, tag))
```

This recovered all 16 jobs x 100,000 games on the run that hit it.

## Fix options, in preference order

1. **Serialise the writers.** One mutex around whichever sink emits `[win]` and `[batch]`, so a line
   is written atomically. Correct at the source and fixes every consumer at once.
2. **Write the win dump to its own stream/file** rather than sharing with progress output. The
   heartbeat already mirrors to `logs/batch/heartbeat.txt`, so the precedent exists.
3. **Make the parser tolerant** — `re.search` instead of `re.match`, or scan for every `[win] job=`
   occurrence in a line. Cheapest, and worth doing *regardless* as defence in depth, but it treats
   the symptom: any other interleaved writer would still corrupt output nothing is parsing yet.

Do **not** "fix" this by setting `MTG_BATCH_HEARTBEAT=0` on screens. The heartbeat is the
first-ten-minutes utilisation check CLAUDE.md mandates, and it is how two separate multi-hour
core-starvation incidents were eventually diagnosed. Losing it costs more than an occasional
repairable line.

## Related

* `src/ai/AIEngine.cpp` ~4808 — the `UntapCreature` branch pays and untaps but never calls
  `m_logger`, unlike every sibling branch, so Wirewood Lodge activations are invisible in every game
  log this repo has produced. Same class of defect: the measurement apparatus quietly not recording
  something. Both are deferred until the StompySurprise list is frozen, because rebuilding the
  engine mid-measurement would break comparability across the seed blocks.
