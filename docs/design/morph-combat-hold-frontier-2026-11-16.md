# Morph combat payment frontier identity

## Cause and fix

The Morph mana audit on seed 30,000,074, turn two, could list `Forest:G`
for Might of Old Krosa and `Stomping Ground:G` for Berserk, yet its frontier
prune discarded that route. Berserk's combat-step payer temporarily holds the
attacker tapped while the payment is made, then releases it. The audit captured
the frontier during the hold, when it looked like a board where the creature
had already tapped for mana. That other board could have extra floating mana,
so the resource prune treated it as covering the combat board even though the
attacker stayed tapped afterward.

`DblCombatPayInto` now publishes the held attacker's id only while invoking
the payment callback. The mana oracle records that id on a frontier and keys
frontiers with a temporary combat hold apart from ordinary taps. The marker
does not change a game state or the payer's choice; it only changes audit
pruning. The exact legal line survives to an end state with Ignoble Hierarch
untapped.

## Verification

The focused unit test in `test/unit/test_mana_oracle.cpp` failed before the
change and passes after it. Full Release units passed: 3,366 cases and
3,614,560 assertions. On the exact seed, the audit's 22 `ORACLE-GAP` flags
fell to zero; no issue flags remain there (two `DOMINATED-LAND` disclosures).
On the first 500 Morph games, `ORACLE-GAP` fell 290→99, `APPLY-DROP` stayed
9, and `DOMINATED-CREATURE` fell 50→49. The audit still needs work on the
remaining flags. The 500-game probe used Release, and a fresh PGO gate on
games 5,000–9,999 remains owed.
