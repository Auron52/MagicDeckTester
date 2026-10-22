# SelesnyaLifegain v2-bogbeast-heroes -- analysis record

The USER-approved list from the 2026-10-22 screen (rounds 1-15, `logs/selesnya_screen/`), staged at
`decks/SelesnyaLifegain/v2-bogbeast-heroes/` while its artifacts are generated. The shipped list stays
at `decks/SelesnyaLifegain/` and is archived only after every v2 resource exists (USER 2026-10-22).

## The list (60)

18 Elves: 4 Llanowar Elves, 4 Elvish Mystic, 2 Fyndhorn Elves, 4 Priest of Titania, 4 Elvish Archdruid.
4 Blossoming Bogbeast, 4 Nykthos Paragon, 4 Ageless Entity, 1 Craterhoof Behemoth, 4 Feed the Clan,
2 Heroes' Reunion, 2 Genesis Wave. 21 lands: 4 Brushland, 4 Branchloft Pathway, 4 Razorverge Thicket,
2 Horizon Canopy, 2 Forest, 4 Blighted Steppe, 1 Wirewood Lodge.

Screen estimate (round 15, 40,000 games per format, shared shipped apparatus): -0.509 std / -0.533 2HG
turns vs the shipped list. Goldfish caveats: life payments (Canopy, Brushland) are priced at ~0, and the
opponent never blocks.

## Stages

- **Coverage:** 0 missing, 19/19 cards implemented, no gaps.
- **Profile:** `scripts/analyze_deck.py <v2 .cod> --no-rebuild --analyzer-seed 3000`; card_scores for
  all 19 cards. No value sidecar until the value-leaf stage.
- **Provider:** resolves to SelesnyaLifegain (6 of 9 signature cards), same as shipped; every new suite
  row's `[play]` line prints `provider=SelesnyaLifegain`.
- **Buckets:** no `SelesnyaLifegain.buckets.json` exists for either list; the keep table uses
  discovered buckets.
- **Suite rows:** keys `selesnyav2` / `selesnyav22hg` in smoke, regression and overnight (overnight =
  the ENTRY set; expansion is owed at keep-table adoption). 31 GT keys accepted
  (35f132e1e); `suite_gate --cost` 0.48x Alchemist.
- **Mismatch harnesses:** 0 non-convergence, 0 fidelity flags (seeds 7001/8001 x 60).
- **Viewer audits:** decision surfacing and wiring clean.
- **Play invariants:** pass (after teaching `play_invariants.py` Nykthos Paragon's counter decision,
  22c55d98f).
- **Depth ladder (300 games, seed 55055):** d0 5.53, d3 4.8533, d5 4.8533; d0->d3 151 better / 0 worse.
  2 unwon games, both kept hands with only colourless lands (Steppe/Lodge); left to the keep table.
- **Tie-break check:** 50,000 games, 17 changed, net -3 turns: stays at the default.
- **Discard analysis:** status quo. Cost diagnostic: no cost interactions.

## Claude-play sweep
- commit: `974deab83`
- seeds: 79001 79104 79207 79310 79413 79516 79619 79722 79825 79928 80031 80134 80237 80340 80443 80546
- games: 16
- flags: 3 unresolved

Claude tied the search in 15 games and was slower in one (80134); never faster.

Fixed during the sweep: Genesis Wave putting a fastland (Razorverge Thicket) onto the battlefield
counted the other lands entering in the same Wave (1db4d3afc, unit-tested); the claude-play text for
Heroes' Reunion's player target described it as damage.

Unresolved, all in the existing mana-payment family:
1. The menu certifies cast orders the executor then drops, with no warning label (79413, 80237, 80134).
2. An unpayable Blighted Steppe activation is offered and silently dropped (80237).
3. Paying one spell at a time spends the only white source or a growing Elf's mana, so legal lines
   become unreachable (79516, 79619, 79825, 80443). On 80443, Genesis Wave with X>0 skips whole-turn
   payment and blocks a T4 kill that the search also misses; it won T5.

## Next

`scripts/mullgen.sh run decks/SelesnyaLifegain/v2-bogbeast-heroes`, then
`scripts/valueleaf.sh run decks/SelesnyaLifegain/v2-bogbeast-heroes`, serially (USER 2026-10-22). Then
the v2-vs-shipped measurement with each list's own artifacts, then promotion + archive.
