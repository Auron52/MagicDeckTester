# Spirits: spend the Chancery's spare color before Phantom

The fresh Spirits mana gate on games 5,000–9,999 found 35 `APPLY-DROP`
flags. All four signatures had an Aether Vial `{1}` and White Orchid
Phantom `{W}{W}` on a board where Azorius Chancery plus a white
creature-only land could pay both. The source-level scarce-color mask held
both white providers during Vial's payment. Its held attempt failed, and
the unrestricted retry spent the creature-only land on Vial's generic pip.
Chancery stayed untapped, leaving only its one white unit for Phantom.

`MTG_SCARCE_BUNDLE_SPARE` (default on, `=0` control) changes that held
attempt only when the current bill is one generic mana, a fixed two-color
Karoo is held, and exactly one of its colors is needed by the remaining
named line. The mask releases the Karoo so the existing line-aware generic
order spends its other color and floats the needed one. It leaves the
creature-only source held for the later cast. No card choices or plan
branches change.

The focused test in `test/unit/test_spirits_geist_wanderer.cpp` fails before
the change and passes after it in both rollout and executor. The three exact
gate repros (seeds 30,008,932, 30,008,702, and 30,008,778) have zero
`APPLY-DROP` flags after the fix. With the switch off, seed 30,008,932 again
reports 14 drops, confirming the A/B isolation. The full Release unit suite
passed 3,367 cases and 3,614,613 assertions.

On 2,000 paired Spirits games (seeds 30,012,000–30,013,999) using the same
Release binary with the switch off/on, neither win turn nor play digest
changed. Both arms averaged 4.6250 loss-penalized turns; search work rose
about 0.01%. Data and per-game logs are under
`build/scratch/spirits_bundle_spare_ab/`.

Spirits-only combined smoke/regression passed all three smoke cells and all
searched regression cells unchanged. One depth-zero regression game (index
492) improved from turn six to turn five; it was the only changed game in
that tier and the ground truth was accepted after the per-game audit. The
strict viewer replay reported zero drift, board divergence, or enumeration
gaps across 864 references, and all 561 scenarios passed. Run details are in
`build/scratch/spirits_bundle_spare_regression.log`.
