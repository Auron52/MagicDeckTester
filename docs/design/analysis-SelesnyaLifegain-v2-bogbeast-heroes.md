# SelesnyaLifegain v2-bogbeast-heroes -- analysis record

The USER-approved list from the 2026-10-22 screen (rounds 1-15, `logs/selesnya_screen/`), staged at
`decks/SelesnyaLifegain/v2-bogbeast-heroes/` while its artifacts were generated, and **PROMOTED to the
shipped path `decks/SelesnyaLifegain/` on 2026-10-22** once every v2 resource existed (USER 2026-10-22).
The original list is archived at `decks/SelesnyaLifegain/v1-avatar-alchemist/` (suite key `selesnya`,
ledger `analysis-SelesnyaLifegain.md`); this list keeps its own suite keys `selesnyav2` / `selesnyav22hg`.
Paths below that name `v2-bogbeast-heroes/` describe where the work was done on the day; the files now
live at the top level. Promotion record: `selesnya-v2-promotion-2026-10-22.md`.

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

## Artifacts (2026-10-22, generated at src 35b5cef3d; committed 9537bb50f)

- **Keep table:** `mullgen.sh` fast R=30, K=16 discovered buckets, gen setting d1 / 3 ms. VALIDATION
  PASSED: keep -0.2978t vs static (4/4 seeds), confounded bottoming -0.0773t (4/4).
- **Value sidecar:** `valueleaf.sh` E.5 picked shape #3 nlfit (final-depth single pass, LEAFLESS: 0.51x
  units, delta -0.00025 on 8 x 1000 held-out); E.7 picked play setting **d6 b20** (0.95x, delta 0);
  E.6 no escalation allowance. The shipped list plays a learned value-leaf table at the built-in d5 b20.
- **Overnight rows** expanded to the FULL set with both artifacts ABSENT (f6227009b), then the adoption
  accepted in all three tiers (bc8766e76): 31 keys, all faster, nothing else moved. Keep table
  attribution 0 unexplained in every tier; the sidecar moved 8 games slower / 4 faster, 7 of the 8 are
  d3 budget churn, the 8th (2hg overnight d5 s5005 gi6, T8 -> unwon) is unwon without the sidecar too at
  `--depth 8 --budget-ms 0`.

## v2 vs the shipped list (2026-10-22, HEAD:src ec2de5145, PGO+LTO)

Each arm with its OWN profile, keep table and value sidecar out of its own directory, at its own play
setting (shipped d5 b20, v2 d6 b20; `provider=SelesnyaLifegain` on all 40 jobs). One pooled batch,
10 paired blocks x 4,000 games per arm per format (`seed = BASE + off`, `game_index = off`; std BASE
51,000,000, 2HG 52,000,000), `max_turns` 8 (probe win rates 99.3-100%, so the horizon does not bind).
Apparatus confound: the numbers cover the list AND its refitted artifacts, including the leafless
search shape, not the cards alone. Generator + analysis: `logs/selv2_vs_shipped/` (gitignored).

| format | shipped avg(pen=9) | v2 | delta | se | t | blocks better | unwon ship -> v2 | <=T4 | >=T6 | ms/game |
|---|---|---|---|---|---|---|---|---|---|---|
| std (20, 1 head) | 5.1341 | 4.5908 | **-0.5433** | 0.0043 | -127 | 10/10 | 0.48% -> 0.20% | 18.59% -> 53.74% | 24.57% -> 9.46% | 123.4 -> 40.6 |
| 2HG (30, 2 heads) | 5.4255 | 4.8892 | **-0.5363** | 0.0074 | -73 | 10/10 | 0.99% -> 0.48% | 9.02% -> 35.00% | 37.78% -> 17.50% | 216.3 -> 75.4 |

## Promotion (2026-10-22)

USER-approved and done: v2 is the shipped list at `decks/SelesnyaLifegain/`, the original is archived as
`v1-avatar-alchemist/` with its references. The move is inert for both lists (every suite key reproduces
its GT byte-identically). Details and the traps checked: `selesnya-v2-promotion-2026-10-22.md`.

**The shipped list has ZERO hand-played references** until some are saved: all ten of the original's
(plus its one suboptimal target) were played on v1 and moved with it.
