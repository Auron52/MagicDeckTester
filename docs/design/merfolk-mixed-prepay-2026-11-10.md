# Merfolk: reserve creature-only lands during mixed prepayment

The PGO mana-clean run `20261108-140909` found 349 Merfolk APPLY-DROP lines in 5,000 games. Reproductions pair a creature with Spreading Seas. Secluded Courtyard and Unclaimed Territory can pay a coloured creature pip, but only provide colourless mana toward Seas's generic pip. The batch prepayment DFS spent those lands on Seas before paying the creature, even when an Island or Soporific Springs could supply Seas's blue pip.

`BatchPrepayMainCasts` now retries a failed mixed creature/noncreature solve with one or two of those creature-only lands reserved for the creature stage. The existing stage order and tap choice remain first, so successful old payments are unchanged. The two-land retry covers a Merfolk Mistbinder's green/blue bill and a kicked Tide Shaper's generic/blue bill; it runs only after the ordinary attempts fail.

## Verification

- All seven distinct top Merfolk reproduction commands from `20261108-140909` now produce zero APPLY-DROP lines on the modified Release binary. The initial patch fixed five; the two-land reserve fixed gi105516, and counting generic creature cost fixed gi107109.
- A paired Release probe over 200 fresh games, indices `500100` through `500299`, has identical per-game averages, digests, and work units with the mana oracle on and off. The oracle-on arm reports `MISS=0`, `ORACLE-GAP=0`, `UNMODELED=0`, `CAPPED=0` and no counted issue flags. This is diagnostic, not a 5,000-game PGO clean stamp.
- `./build.sh release -- --target mtg` and `git diff --check` passed.

Next: integrate, build the current-source PGO binary, and run the next fresh Merfolk block `110000` through `114999` before declaring the deck clean.
