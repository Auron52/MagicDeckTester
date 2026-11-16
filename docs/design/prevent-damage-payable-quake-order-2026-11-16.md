# Prevent Damage: payable Earthquake order at low life

The fresh PGO mana-clean gate `codex-pd-multiquake-20261116` found 12
`APPLY-DROP-ORDER` flags in three signatures. In each, the preferred cast
ranking puts Vito or Spellshock before one or two Rolling Earthquakes. At two
life, that order commits a prefix and drops an Earthquake, while moving a
Quake earlier pays the complete declared line. The recorded seeds are
30,067,652 (Spellshock + Quake), 30,068,425 (Spellshock + two Quakes), and
30,068,718 (Vito + two Quakes).

`MTG_PD_PAYABLE_QUAKE_ORDER` is default on; `=0` restores the ranked order.
For a two or three cast set at four life or less, the provider first probes
the preferred order. If it cannot pay the whole set, it tries placements of
the visible Earthquakes that preserve the relative order of all other casts
and of the Earthquakes themselves, and chooses the payable placement with
the fewest adjacent moves. This is an execution fallback for a chosen cast
set; it adds no searched action or draw choice. The life bound is empirical:
all 12 recorded failures started at two life, and the broad form added cost
without finding extra play changes in the paired screen.

Focused Release oracle replays with the switch on removed the
`APPLY-DROP-ORDER` flags from all three recorded seeds. The broad first trial
used 5,000 paired games at seeds 10,300,001–10,305,000. It changed three
per-game play digests and zero win turns, with 26,610 total turn score in
both arms. Recorded game time rose from 5,684,045 to 5,946,789 ms (+4.6%);
work units moved from 150,545,331 to 150,545,171. Restricting the exact
probe to life four or less preserved all 5,000 win turns and exactly the
same three play changes. It used 5,647,797 recorded game ms (-0.6% versus
off, within run noise) and 150,546,690 work units (+0.001%). The paired
data are in `build/scratch/pd_quakeorder_ab/`.

The same gate also reported 59 `ORACLE-GAP` flags. The repeated
`Manabarbs + Manabarbs` signature at seed 30,068,427 is a separate audit
coverage problem: the engine's whole-line payment taps sources before the
first Manabarbs resolves, while the audit enumerates the two spell payments
sequentially. All 182 listed oracle outcomes on turn six have at most 13
life on the engine's completed line; the engine has 14. Disabling the oracle
tap memo and disabling the Manabarbs batch optimization do not remove the
gap. This diagnosis covers that signature, not the other gap signatures.

The default-on Release binary passed all 3,355 unit cases and 3,614,078
assertions. Focused replays of the three recorded seeds had zero
`APPLY-DROP-ORDER` flags. The deck-scoped combined smoke/regression run had
zero viewer play drift, board divergence, or enum gaps across 864 references,
and both required beat references passed. The harness exit remains a known
committed ground-truth mismatch in `pd_regression_d5_s2002` (game 14 turns
6→7). A same-binary `=0` control on the exact nine-job manifest had
**zero win-turn differences** from the default-on arm, including that game;
only three depth-zero play digests changed. No ground truth was accepted.

A fresh 5,000-game mana-clean gate is still owed after this source commit.
