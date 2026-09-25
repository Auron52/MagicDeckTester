# The per-game audit is not deck-scoped, and `run_regression` branches on it

**Status:** open, not started. Small, self-contained, no measurement required to fix.

## The defect

`test/audit_changed_games.py` selects the configs it audits with

```python
keys = sorted(os.path.basename(f)[:-5] for f in glob.glob(f"test/gt_logs/*_{MODE}_*.wins"))
```

— **every key of the mode**, regardless of which decks the run actually executed. It then diffs
committed ground truth against whatever per-game logs happen to be sitting in
`test/logs/<mode>/wins/`.

Those logs are not cleared between runs and are gitignored, so on any long-lived working copy they
accumulate from earlier runs at earlier commits. A `--deck=<name>`-scoped run therefore reports a
per-game audit dominated by **other decks' stale output**, with no indication that this has
happened.

## Why it is more than a reporting nit

`scripts/mullgen.sh`'s `run_regression` greps those totals and **branches on them** to choose which
causal story to print:

```bash
faster=$(grep -oE 'faster=[0-9]+' "$OUT/regression.log" | head -1 | cut -d= -f2)
slower=$(grep -oE 'slower=[0-9]+' "$OUT/regression.log" | head -1 | cut -d= -f2)
...
if [ "$faster" -ge "$slower" ]; then
  log "  Direction: net FASTER ..."
else
  log "  Direction: net slower ... Expected when BOTTOMING dominates ..."
fi
```

That branch exists for a good reason — the comment above it records that asserting a direction the
run then contradicts "is how a report stops being trusted", after StompySurprise moved the opposite
way to the text's assumption. But it is fed a number computed over the wrong population, so it can
pick the wrong branch **and attach a confident, plausible mechanism to it**.

### Worked example (Giants, 2026-09-24)

A `--deck=giants` regression run executed 5 cells. The audit reported:

```
configs changed: 85   unchanged: 28   no-run-dir: 16
[searched] slower=1258  faster=385  play-changed=2761
```

85 changed configs across ~15 decks — critter 1023 entries, stompy 994, th 377, mirrorwing 304 —
of which **Giants contributed 57 of the 1258**. `run_regression` took the `slower` branch and
printed the bottoming-dominates explanation.

Recomputed over Giants alone, the deck was the *opposite* case:

| | slower | faster | wins lost | wins gained |
|---|---|---|---|---|
| Giants only | **188** | **454** | 36 | 102 |

Every cell faster at every depth (−0.167 t to −0.483 t). The printed mechanism described a
phenomenon that was not occurring.

The same pollution appears in the `--smoke --deck=<name>` path, where it merely misleads a reader
(`slower=194 play-changed=412` on a run whose three cells were byte-identical). The regression path
is worse only because a script consumes it.

## Fix

Either of these closes it; the first is preferable.

1. **Scope the audit to what ran.** Pass the executed deck/config set to `audit_changed_games.py`
   (the harness already knows it — it built the batch manifest) and glob only those keys. This also
   makes the audit faster, since it currently re-explains hundreds of stale games on every
   deck-scoped run.
2. **Refuse to state a direction when the audit is not scoped.** If `--deck=` was used and the
   audit is mode-wide, have `run_regression` print the totals with an explicit "covers all decks'
   logs, not just `<deck>`" caveat and skip the FASTER/slower branch entirely.

A third, complementary measure: clear or timestamp `test/logs/<mode>/wins/` at the start of a run so
`no-run-dir` / stale entries cannot masquerade as this run's output.

## Guard to add with the fix

A regression test that runs one deck's cells with another deck's stale `wins` present and asserts
the audit's reported counts cover only the deck that ran. Without it the defect is invisible on a
fresh clone — `test/logs/` is empty there, so the audit is accidentally correct and stays correct
until someone's working copy has been used for a while.
