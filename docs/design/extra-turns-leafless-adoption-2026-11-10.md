# Extra Turns leafless escalation adoption (2026-11-10)

The user chose the leafless escalation play policy after the eight-seed phase-E
comparison. The live sidecar now selects `ladder=escalation`, `leaf=none`, and
`alpha=relaxed`, at the existing built-in play setting (depth 5, budget 20 ms).
The learned model remains archived as `Extra Turns.value.DISABLED.json`.

| Arm | Paired games | Mean scored win turn | Work relative to old live |
|---|---:|---:|---:|
| Old live | 8,000 | 5.00537 | 1.000x |
| Leafless escalation | 8,000 | 4.78400 | 1.373x |
| Leafless single ladder | 8,000 | 4.78400 | 1.420x |
| Learned leaf | 8,000 | 4.79537 | 1.960x |
| Emulated ladder | 8,000 | 5.07100 | 1.139x |

The planned E.7 play-setting grid was stopped by user authorization after 69 of
200 jobs. It measured 200,000 more games across depth, budget, and other shape
arms. That sweep was unnecessary to enable the chosen policy: the existing
depth-5/budget-20 setting was already the setting in the phase-E comparison.
Moreover, 48 grid jobs repeated the chosen leafless escalation sidecar under a
second arm name. E.7, E.7b, and E.6 remain unmarked; rerunning the full
`valueleaf.sh run` driver will restart E.7. The discarded grid logs remain under
`logs/vlq_extra_turns/playset/` for diagnosis, not as accepted measurements.

## Paired ladder diagnostic

One further held block of 400 paired games at the same play setting gave old
live 5.0400, leafless escalation 4.8125, and emulated ladder 5.1200 scored turns.
Work units were 7.231 million, 10.154 million, and 7.904 million respectively.
The one-seed sample corroborates the eight-seed direction but does not replace
it. The diagnostic artifacts are `logs/et_ladder_diag/`.

The old live heuristic ladder committed 177 of its 248 recorded decisions at
depth 1 and only 11 at depths 4 or 5. Leafless escalation recorded 932 ladder
decisions, with 407 at depths 4 or 5. Its ladder passes cost 0.107 million units
versus 0.302 million for old live, while its full-search preparation and
enumeration rose to 5.151 and 3.839 million units (old live: 1.168 and 0.897
million). Thus the old policy's lower total work is explained in this sample by
shallow commitments; the leafless policy spends its cheaper leaf evaluations to
reach many more deeper search decisions. These counters overlap by design and
must not be summed as a partition of `units_total`.

The emulated ladder recorded 1,981 decisions, 1,650 fallback attempts, 3,156
overruns, and 1,779 decisions with no completed heuristic line after its
fallback attempts. This is a concrete mechanism for its quality loss: the
emulation often cannot produce the heuristic ladder's committing line under the
budget, so it falls back to an earlier or value-leaf line. Its frozen `emul_r`
has only two calibrated depth entries, reused beyond those depths. The counters
identify a failure mode, but they do not isolate whether that sparse calibration
is the primary cause; the shape comparisons already reject it for play.

## Acceptance

Deck-filtered smoke: zero slower searched games, 31 faster, 69 same-turn play
changes. Deck-filtered regression: zero slower searched games, 65 faster, 135
same-turn play changes. The depth-zero rows were byte-identical. Strict viewer
replay: 787 references, zero play drift, board divergence, enumeration gaps, or
contract failures. Smoke and regression ground truth were accepted for
`extraturns` and `extraturns2hg`; no engine source changed.

After rebasing onto the remote mana changes, Release smoke and regression
reproduced those accepted Extra Turns digests exactly. The full strict viewer
replay passed all 787 references with zero drift or gaps. The regenerated
overnight slice improved 241 searched games and slowed 2 against the previous
per-game logs; depth-zero outcomes were identical. Both slower games retained
the same hand and draw sequence but chose a different play line. Across the
four 300-game depth-5 Extra Turns seeds, the mean fell from 4.9458 to 4.7450
scored turns; the 50-game 2HG seed fell from 5.1000 to 4.8800. The smoke,
regression, and overnight aggregates were accepted on the rebased binary.
