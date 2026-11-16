# Spirits: finish Souls flashback and the following creature

The fresh Spirits gate on games 10,000–14,999 found 61 `APPLY-DROP`
flags in three games. Fifty-eight involved Lingering Souls flashback
`{1}{B}` and White Orchid Phantom `{W}{W}` with Azorius Chancery and a
white creature-only land. The other three involved Souls and Millicent
`{W}{U}`. The oracle found paying continuations in all cases; the engine's
shared payer spent a source or floated color the later cast needed.

`MTG_SPIRITS_SOULS_LINE_HOLD` is default on and `=0` restores the old payer.
For the named Spirits `{1}{B}` payment, one held attempt uses the line's
remaining pips and the visible sources:

* With two later white pips, hold the creature-only white land and use
  Chancery's spare blue unit for Souls' generic pip.
* With a later white and blue pip, hold Chancery and use an ordinary land
  for Souls' generic pip.
* If Chancery is already tapped and both white and blue are floating,
  keep those units for Millicent and let an untapped black source pay
  Souls. The scarce-source mask is released for this attempt because the
  already-floating blue covers its later pip.

Every held attempt restores the board on failure and then follows the
ordinary payer. The change uses only the named line and visible mana
sources; it does not add or search plans.

Exact replays of gate seeds 30,014,133, 30,010,478, and 30,013,710 now
have zero `APPLY-DROP` or `ORACLE-GAP` flags. With the switch off they
restore 45, 13, and 3 drops. The full Release unit suite passes 3,368
cases. On 2,000 fresh paired Spirits games (seeds 30,015,000–30,016,999),
both arms averaged 4.6230 loss-penalized turns; every win turn and play
digest matched, and total work units were equal. The paired logs are under
`build/scratch/spirits_souls_hold_ab/`. Spirits and two-headed Spirits
combined smoke/regression passed all ten cells with no changed game or
digest. All 561 scenarios passed. Strict viewer replay of 864 references
reported zero play drift, board divergence, or enumeration gaps; both
suboptimal references remained beaten.

The fresh PGO gate on games 15,000–19,999 found ten `APPLY-DROP` flags,
all in seed 30,015,009 on a different cost: Souls cast from hand
`{2}{W}` before Phantom `{W}{W}`. Oracle-on/off play was byte-identical;
there were no enum gaps, oracle gaps, or caps. The gate record is
`logs/mana_clean/codex-spirits-souls-line-20261116/report.md` and
`test/mana_clean.json`. The hand-cast route has a follow-up held payment:
with exactly three white providers (Chancery, one ordinary white land,
and one white creature-only land), plus a nonwhite generic land, it
reserves the latter two white providers for Phantom. Chancery's W/U bundle
and the nonwhite land pay Souls. The new focused rollout/executor fixture
fails with `MTG_SPIRITS_SOULS_LINE_HOLD=0` and passes with it on. Seed
30,015,009 falls from ten drops to zero. The full Release unit suite
passes 3,369 cases. On 2,000 new paired Spirits games (seeds
30,020,000–30,021,999), both arms averaged 4.6220 loss-penalized turns;
all win turns and play digests matched, with +0.01% work units. Logs are
under `build/scratch/spirits_souls_hand_ab/`. The three earlier flashback
gate repros still have zero misses. Spirits and two-headed Spirits combined
smoke/regression pass all ten cells with no changed game or digest; all
561 scenarios pass. Strict viewer replay has zero drift, divergence, or
enumeration gaps across 864 references, and both suboptimal references
remain beaten.

The next fresh PGO gate, games 20,000–24,999, reports twelve
`APPLY-DROP-ORDER` flags, all in seed 30,021,365. Its single plan drops
Flooded Strand and Vials White Orchid Phantom before casting Millicent.
The Phantom's pinned own-land destruction removes Azorius Chancery before
Millicent pays; casting Millicent before the Vial put pays the entire line.
The two report signatures are alternative fetchland outcomes. There are no
enum gaps, oracle gaps, or caps, and oracle-on/off play is byte-identical.
See `logs/mana_clean/codex-spirits-souls-hand-20261116/report.md` and
`test/mana_clean.json`.

`MTG_SPIRITS_OWN_LAND_ORDER` is default on (`=0` restores the ranked
order). When the planned Vial put pins White Orchid Phantom to an untapped
own land and Millicent follows it, the Spirits provider moves Millicent
immediately before the put only if the sequential mana probe confirms the
new order pays. This keeps Phantom first when its Spirit affinity is needed.
The exact seed 30,021,365 now has zero issues; the off control restores all
twelve `APPLY-DROP-ORDER` flags. A focused test covers the order. In 2,000
fresh paired Spirits games (seeds 30,030,000–30,031,999), both arms average
4.6490 loss-penalized turns, with identical win turns and play digests in
every game; the on arm uses 12 more work units out of 77.9 million. Logs are
under `build/scratch/spirits_own_land_order_ab/`. The combined Spirits/2HG
smoke and regression batch passes all ten cells with no per-game change.
All 561 scenarios pass. Strict viewer replay reports zero play drift,
board divergence, or enumeration gaps across 864 references; both
suboptimal references remain beaten. The full Release unit suite passes
3,370 cases and 3,614,735 assertions.

The fresh PGO gate on games 25,000–29,999 has zero missed plans, order
drops, enum gaps, or caps. It reports three `ORACLE-GAP` flags, all in
seed 30,025,459, and oracle-on/off play is byte-identical. The engine
pays a Millicent → White Orchid Phantom line by tapping Hallowed Fountain
and leaving Secluded Courtyard untapped. The nearest oracle outcome taps
Courtyard and leaves Fountain untapped; neither board covers the other
under the oracle's resource key. The issue is now confined to that
payment branch. See
`logs/mana_clean/codex-spirits-own-land-order-20261116/report.md`.

The oracle's first-payment table omitted a legal advance tap: Millicent
costs {W}{W}, but the engine taps Watery Grave for {U} alongside its two
white sources and carries that blue into White Orchid Phantom. The table's
existing `MTG_ORACLE_LINE_SURPLUS` route opened such extra one-unit taps
only for a one-mana first bill. `MTG_ORACLE_LINE_SURPLUS2` (default on,
`=0` control) opens one extra tap after a two-mana first bill only when
the named line later owes a color absent from that first bill. It changes
oracle coverage, not the searched candidate menu. The exact seed has zero
gaps with the flag on; `=0` restores all three. On 500 fresh Spirits
games at seed 30,050,000, both arms average 4.6220 turns, with zero
mana-clean issues. The on arm explores 2,123,000 oracle payment options
versus 2,038,107 off (+4.2%); recorded oracle work time is 107,866 ms
versus 106,461 ms (+1.3%). The full Release suite passes 3,371 unit
cases and 3,614,741 assertions. Combined Spirits/2HG smoke and regression pass all ten
cells, with zero per-game play changes. All 561 scenarios pass. Strict
viewer replay has zero drift, divergence, or enumeration gaps across
864 references, and both suboptimal references remain beaten. The fresh
PGO mana-clean gate on games 30,000–34,999 is clean: zero issues, zero
misses, zero oracle gaps, and byte-identical oracle-on/off play. Spirits
is recorded clean in `test/mana_clean.json`; the report is
`logs/mana_clean/codex-spirits-line-surplus2-20261116/report.md`.
