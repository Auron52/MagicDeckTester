# Kithkin mana-clean identity follow-up (2026-11-10 UTC)

The `clean-extension-20261109` gate played Kithkin games 125000–224999 on
source tree `3bda5c647e7c`. It found **zero issue flags**, but its oracle-on
and oracle-off play differed in one 250-game job, `kithkin__g173500`. The
record is correctly **invalid**. A zero-issue count cannot make this a clean
stamp while play identity fails.

| arm | 250-game average | digest | work units |
|---|---:|---|---:|
| original oracle on | 4.4920 | `4817e9fe3ba0b4fb` | 485695 |
| original oracle off | 4.4880 | `fe32fade83d537d0` | 485812 |
| focused oracle-on replays | 4.4880 | `fe32fade83d537d0` | 485812 |

The original oracle trace and the focused replay first disagree at seed
`30173535`, game index `173535`: original T3 post-combat main had one plan,
the replay had none. The original game finished on T7; the replay and the
oracle-off game finished on T6. Three existing oracle-on replay forms all
returned the oracle-off digest: the 250 games split into one-game jobs; the
250-game job alone; and the 195-job Kithkin prefix through that job at 29
threads. The saved files are under
`build/scratch/mana_clean/clean-extension-20261109/kithkin-split/`.

The search budget counts deterministic work units, so elapsed-time drift is
not an explanation. The original batch reported no `[determinism] WARNING`
for a global plan or line-cache pool refusal. No Kithkin deck or profile
change landed during the original run, and their current deck checksum still
matches its recorded checksum.

## Mechanism found on the fresh block

The current-source fresh block completed with **byte-identical play**, but
found eight `UNMODELED` flags in one Figure of Destiny activation signature.
Its T6 `{R/W}` × 6 payment on a board of Plains reached an engine result that
the oracle's scripted payment refused. A one-game replay at seed `30225663`,
index `225663`, reproduced one flag before the fix.

The cost is six hybrid pips, so `TapForCostShared` enumerates 64 assignments.
`src/ai/ManaPayment.cpp` allocated `order[16]` and, when floating mana and a
later line bill were present, `score[16]`, then filled both through
`n_assign = 1 << hybrid_count`. This writes beyond each stack buffer for a
six-pip Figure activation. Both buffers now derive their capacity from the
six-entry `ManaCost::hybrid_pair` field. The overflow provides a concrete
mechanism for the eight scripted-payment failures and a plausible explanation
for the earlier one-game identity anomaly. The original anomaly itself was
not reproduced, so that causal link remains an inference until post-fix gates
show identity.

Focused validation on the fixed Release binary at seed `30225663`, index
`225663`, returned zero oracle flags (`UNMODELED=0`, `MISS=0`,
`ORACLE-GAP=0`, `CAPPED=0`). The same one-game batch under oracle on and off
returned digest `26932479312cd709` and 3,754 work units on both arms. The
pre-fix PGO binary had returned one `UNMODELED` on this exact game; the
Release/PGO codegen difference is not expected to change play under the
repository's binary parity contract.

A fixed-source paired Release batch also tested two 250-game jobs at four
workers: historical `225500–225749` (contains the repro seed) and independent
probe `500000–500249` (keeps the next official gate block untouched). Oracle
on/off results matched on each job, including work units: historical average
4.5840, digest `749ccd05d0d578c0`, 553,120 units; probe average 4.4960,
digest `c87365989bf90f2d`, 606,502 units. The oracle summary reported
`MISS=0`, `ORACLE-GAP=0`, `UNMODELED=0`, `CAPPED=0`. This validates the focused
repair without promoting a new mana-clean stamp; that still requires the
full fresh 5,000-game PGO gate after the fix lands.

The focused unit run on the fixed Release build,
`build/Release/mtg-test --test-case='Figure six hybrid pips*'`, passed both
cases and all 17 assertions. The new case exercises both the normal shared
payer and the oracle's float-only scripted path with all six hybrid pips.

## Fresh-block gate

The run described below tested games `225000–229999` (seeds
`30225000–30229999`) on source `c3b6ce358884` with 16 workers to share CPU
with the simultaneous Extra Turns gate. It recorded 8 `UNMODELED`, zero
other issue flags and byte-identical oracle-on/off play; verdict **issues**.
Its state is isolated at `/tmp/codex-kithkin-mana-state-20261110.json` so it
cannot overwrite the Extra Turns gate's `test/mana_clean.json` update. Merge
its Kithkin run into the primary registry only after both gates complete,
preserving every run from both.

After the buffer fix lands, run a **new** 5,000-game block, expected
`230000–234999`, on a clean PGO build from the fixed source. A clean block
with byte-identical play can restore the current-source Kithkin stamp. Keep
the original invalid record and its artifacts for the unresolved identity
history.

Command launched from the primary checkout, with its source left clean:

```sh
cp test/mana_clean.json /tmp/codex-kithkin-mana-state-20261110.json
python3 scripts/mana_clean.py run kithkin --games 5000 --threads 16 \
  --state /tmp/codex-kithkin-mana-state-20261110.json \
  --name kithkin-identity-refresh-20261110
```

The process writes `/tmp/codex-kithkin-identity-refresh-20261110.log` and
`.status`, and its report under
`logs/mana_clean/kithkin-identity-refresh-20261110/`.
