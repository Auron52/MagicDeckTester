# Prevent Damage: reserve two visible Earthquake reds

The fresh PGO gate `codex-pd-nonexact-20261116` found four APPLY-DROP flags,
all on seed 30,064,758, turn five. The outer plan casts Dina, Soul Steeper;
its continuation casts two Rolling Earthquakes. The first nonred payment's
existing `PaymentHoldUp` sees an Earthquake in hand but reserves only one red
pip. The cheaper payment can leave one red source, pay the first Earthquake,
and strand the second. Both Earthquakes are visible in hand before Dina.

`MTG_PD_MULTI_QUAKE_HOLD` (default on; `=0` restores the one-red hold) asks the existing armed payer
to preserve up to two red pips, one per Earthquake in hand. When the current
payment is the first Earthquake, it subtracts that spell's own red pip and
protects the other. It changes no plan or cast order. The payer can take a
more painful assignment only when the payment remains legal, the player
survives, and the remaining mana sources can pay the visible pips. The
focused oracle replay has four drops with the switch off and zero with it on.

A first provider-only trial reserved two reds for Dina, but did not protect
the second red when paying the first Earthquake. The oracle flags disappeared
only because the changed payment altered the path through the search; a
recorded-board test still failed on the second Earthquake. That test caught
the omission before adoption. Its 1,000-pair Release pilot (seeds
10,000,001–10,001,000) changed one play digest and no win turns. A separate
5,000-pair held-out run (seeds
10,100,001–10,105,000) also changed one of 200 job digests, with no change
to the total 27,338 turn score. Work units were 152,968,091 off and
153,090,323 on (+0.08%); recorded game milliseconds were 3,852,761 off
and 3,946,025 on (+2.4%). Raw manifests and results are under
`build/scratch/pd_multiquake_*`.

The expanded trial now passes the recorded-board unit test: it pays Dina and
both Earthquakes, and the player survives. On 5,000 fresh paired Release
games (seeds 10,200,001–10,205,000), nine games had different play digests
but all 5,000 had the same win turn, confirmed by per-game replay of every
changed job. Both arms scored 27,281 total turns. Work units were
156,071,302 off and 155,974,698 on (-0.06%); recorded game milliseconds
were 3,893,850 off and 4,056,120 on (+4.2%). The extra time included empty
checks on red payments when only one Earthquake was in hand. Those checks
are now skipped before the line probe. The optimized on-arm rerun was
byte-identical to the initial on arm across all 200 job digests and used
3,972,419 recorded game milliseconds (+2.0% versus off) with the same
155,974,698 work units.
The full unit suite with the optimized trial on passed 3,355 cases and
3,614,078 assertions. The default-on focused unit passed on the rebuilt
Release binary. The deck-scoped smoke/regression run had zero viewer play
drift, board divergence, or enum gaps across 864 references; both required
beat references passed. Its one searched row with a changed digest,
`pd_regression_d5_s2002`, was compared on the same binary with the switch
off: one of 150 games changed play, but no win turn changed. The combined
harness exit remains a committed ground-truth mismatch seen in both arms;
no ground truth was accepted.

The fresh PGO mana-clean gate `codex-pd-multiquake-20261116` on game indices
67,000–71,999 was oracle-on/off play-identical. It found **zero APPLY-DROP**
flags, 12 APPLY-DROP-ORDER flags in three signatures, and 59 ORACLE-GAP flags;
no UNMODELED or CAPPED flags. The next fresh Prevent Damage block is
72,000–76,999. The full report is
`logs/mana_clean/codex-pd-multiquake-20261116/report.md`.
