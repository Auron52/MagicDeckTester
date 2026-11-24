# Bruna line-critical land levers (7a / 7b), their amendments, and the d0 haste-sac charge -- measurement record

Status 2026-11-22: **all levers default OFF, measurement-only. Adoption is the USER's decision** (each lever breaks a
standing rule; USER: "7a. Okay, that makes sense along with the amendment. How does this do in searched?" and "7b.
That seems fine, except I'm more interested in the impact on searched than d0."). Branch `wip/bruna-rule-override`.

## The levers (src/ai/ManaPayment.cpp `crit_hold`, `crit_excluded`, `crit_demote`; TurnSolver.cpp `haste_sac_value`)

| arm | flags | what it does |
|---|---|---|
| a (7a) | `MTG_PAY_LINE_CRIT_BEHIND_DORKS` | a land that is the last source of a colour the rest of the line owes ranks behind the creature band for a GENERIC pip (Bruna s30051257 T2: Pilgrim, not the only {U} land, pays Sol Ring) |
| aa (7a-amended) | a + `MTG_PAY_LINE_CRIT_UNTAPPER_LAST` | a land-untapping creature (`untap_target_land`, Voyaging Satyr) does not win a pip over such a land (Jumbo d0 gi1447) |
| b (7b) | `MTG_PAY_LINE_CRIT_LAND` + `MTG_PAY_LINE_CRIT_OPENER` | the critical land taps after every other land (still ahead of creatures), opener lines included (Bruna s30051968 T2) |
| ba (7b-amended, **DELETED**) | b + `MTG_PAY_LINE_CRIT_HASTE_SAC_LAST` | a `sac_for_mana_grants_haste` source (Generator Servant) paying a noncreature cost does not win over such a land (built for Niv d0 gi532) |
| h (**DELETED**) | `MTG_D0_HASTE_SAC_CHARGE` | greedy (d0 / horizon) plan score charges a plan that casts no creature for each haste-rider sac source it must sacrifice |
| bh | b + h | |

The crit supply (the other sources' units per colour) is computed once per pip selection and the candidate's own share
subtracted (it was an O(board) loop per candidate land); byte-identical, cuts the lever's per-pip cost.

## A/B (2026-11-22, bin3 = bf5ba3b33 tree, 907,400 games, 214 regression shapes, seed base 1810000, held out)

Paired: game gi -> seed 1810000+gi, game_index gi. Searched shapes: 1000 games for the decks the levers move (and
every deck with a dork), 400 for the rest; d0: 2000 per deck. aa runs searched only on Jumbo (the only deck with an
untapper), ba / h / bh searched only on Niv + Niv 2HG (the only Servant decks); every d0 shape runs off/a/aa/b/ba.
net = sum(turn_A - turn_off), unwon = 9; lower is better; 0 is neutral.

### SEARCHED (d3/d5 at shipped budgets)

| arm vs | faster | slower | net | decks net > 0 |
|---|---|---|---|---|
| a vs off | 1 | 4 | **+3** | cyclone +2 (0/2), dragonstorm +2 (0/2); spirits -1 |
| aa vs off (Jumbo only) | 0 | 0 | 0 | none (Jumbo searched never moves under a or aa) |
| b vs off | 10 | 8 | **-2** | cyclone +6 (2/8); mirrorwing -2, niv -2, niv2hg -1, spirits -1, worldfire -2 |
| ba vs b | 0 | 0 | 0 | byte-identical (digests too) on every shape |
| h vs off (Niv, Niv 2HG) | 5 | 11 | **+6** | niv +3, niv2hg +3 |
| bh vs off | 8 | 11 | +3 | niv +1, niv2hg +2 |

### d0

| arm vs | faster | slower | net | decks net > 0 |
|---|---|---|---|---|
| a vs off | 5 | 7 | **+2** | jumbo +3 (1/4), selesnyav2 +2 (0/2), stompy +1 (0/1); bruna -1, cyclone -1, elementals -2 |
| aa vs off | 4 | 4 | 0 | jumbo +1 (0/1), selesnyav2 +2, stompy +1; bruna -1, cyclone -1, elementals -2 |
| aa vs a | 3 | 1 | -2 | (Jumbo only moves) |
| b vs off | 11 | 8 | **-7** | mirrorwing +1 (0/1), niv +2 (2/4), worldfire +2 (0/1); cyclone -10, elementals -2 |
| ba vs b | 0 | 0 | 0 | byte-identical |
| h vs off | 36 | 69 | **+60** | niv +60 |
| bh vs off | 36 | 69 | +60 | niv +60 |

### CPU (searched; units are budget-bound so equal by construction)

| arm vs | units | cpu |
|---|---|---|
| a vs off | 1.0000 | 1.0037 |
| b vs off | 1.0003 | 1.0045 |
| aa vs a | 0.9997 | 0.9970 |
| ba vs b | 1.0000 | 0.9991 |
| h vs off | 0.9938 | 0.9909 |

The cpu deltas (+0.4%) are of the order of the run's interleaving noise (extraturns reads +3.7% on BOTH a and b at
units 1.0000), measured BEFORE the supply hoist above.

## Readings

* **No arm clears the bar** (every deck net <= 0 on searched AND d0). b is net-better overall (searched -2, d0 -7) but
  Cyclone searched is +6 (2 faster / 8 slower, the same four gis at d3 and d5) and Niv / Mirrorwing / Worldfire d0 are
  slightly worse. a is net-worse on both. aa = a on searched (Jumbo never moves); on d0 the amendment recovers Jumbo
  (-2 vs a) as designed for gi1447, but Selesnya / Stompy (no untapper, aa = a there) stay +2 / +1.
* **7b-amended is dead code.** ba is byte-identical to b in all 127,000 paired games, and structurally so: its
  predicate (`crit_excluded`) requires `PaySacSpendableNow`, i.e. `IsPaySacSource` -- a one-unit, produces-nothing
  pay-sac source (Treasure). Generator Servant (`sac_for_mana_amount` 2) never qualifies: its {C}{C} reaches the payer
  as FLOATING mana from a plan SacForMana action, never as a selection candidate. And in gi532 the Servant is
  sacrificed for Ponder with no land left at all. **Deleted 2026-11-22** (dead code; default OFF, so play is unchanged).
* **The d0 haste-sac charge is clearly worse** (Niv d0 +60, 36 faster / 69 slower; searched +6). It does not even fix
  gi532 (7b + charge = T7 either way). **Deleted 2026-11-22** (our own failed experiment; default OFF).

## Slower games at --depth 8 --budget-ms 0 (treatment arm)

Paired game gi = seed 1810000+gi; "off / arm" = the A/B turns (unwon = -1); d8 = the treatment arm replayed at
`--depth 8 --budget-ms 0` (bin3). Recovered = d8 <= off.

| arm | deck | gi | off / arm (shape) | d8 b0 | |
|---|---|---|---|---|---|
| a | cyclone | 529 | 3 / 4 (d3, d5) | 3 | recovered |
| a | dragonstorm | 679 | 5 / 6 (d3, d5) | **6** | **NOT recovered** (also 6 with MTG_NC_SEARCH=1; d1-d4 b0 all 6 vs off 5) |
| a | jumbo | 173, 208, 1056, 1361 | 4/5, 5/6, 3/4, 5/6 (d0) | 4, 4, 3, 4 | recovered |
| aa | jumbo | 1056 | 3 / 4 (d0) | 3 | recovered |
| a (= aa) | selesnyav2 | 1193, 1332 | 4/5, 5/6 (d0) | 4, 4 | recovered |
| a (= aa) | stompy | 1784 | 6 / 7 (d0) | 5 | recovered |
| b (= ba) | cyclone | 186, 359, 442, 529 | 5/6, 3/4, 5/6, 3/4 (d3, d5) | 5, 3, 5, 3 | recovered |
| b (= ba) | cyclone | 1188, 1209 | 8/unwon, 7/8 (d0) | 5, 3 | recovered |
| b (= ba) | mirrorwing | 1358 | 5 / 6 (d0) | 3 | recovered |
| b (= ba) | niv | 662, 753, 1150, 1363 | 5/6, 5/7, 6/7, 5/7 (d0) | 5, 4, 4, 3 | recovered |
| b (= ba) | worldfire | 295 | 7 / unwon (d0) | 5 (4,694 s) | recovered |

h / bh (the d0 haste-sac charge, 80 slower games) were not replayed: the lever is net +60 on Niv d0 and +6 searched,
far from adoptable, and does not fix the game it was built for.

**Dragonstorm gi679 under 7a is a real loss, not clairvoyance.** The arms split at T1's land drop (off: Mercadian
Bazaar, a: Sandstone Needle), and from there off suspends Lotus Bloom on T2 (it arrives T5 and pays Scourge of Valkas's
firebreathing for the 14-damage T5 kill), while a never suspends it and wins T6. Every searched depth from d1 to d8
(b0) gives the same split, so it is not budget or horizon: 7a's payer changes what the search can pay inside its own
nodes (payment is the standing greedy exemption, not searched), so no amount of search recovers it. Root cause inside
the payer is still OWED (which payment of T2-T5 7a orders differently on this board).

## Unit tests (test/unit/test_bruna.cpp, "Bruna: line-critical land variants (measurement levers)")

* 7a amended (Jumbo gi1447 T3): the first board left Overgrowth's host Forest untapped, so its {G} counted as supply and
  no Forest was critical -- the OFF half could not reproduce. The executor RESERVES the declared host across a land
  Aura's own payment (AIEngine `reserved_host` + `ScarceHeldOnlyScope`); the board now does the same.
* 7b amended: deleted with the lever (it was pinned as structurally inert before the deletion).

## Open USER decisions

1. 7a / 7a-amended: neither clears the bar (a searched +3; aa d0 Selesnya +2 / Stompy +1). Default stays OFF unless the
   USER rules otherwise.
2. 7b: best overall (searched -2, d0 -7) but Cyclone searched +6 and Niv / Mirrorwing / Worldfire d0 +2 / +1 / +2.

## A/B 2 (2026-11-23/24, bin8 = a3feba9d1 tree): 7a amendments (R, T) and 7b ritual credit (T)

Arms: **aUR** = 7a + `MTG_PAY_LINE_CRIT_UNTAPPER_LAST` + `MTG_PAY_LINE_CRIT_RESERVE_LAST`; **aUT** = 7a + untapper +
`MTG_PAY_LINE_CRIT_RITUAL_CREDIT`; **aURT** = all three; **b** = 7b; **bT** = 7b + ritual credit. 7a arms run on the
decks that hold a dork / untapper (bruna cyclone dragonstorm elementals jumbo selesnyav2 spirits stompy), 7b arms on the
decks the first A/B moved (cyclone elementals mirrorwing niv spirits worldfire). MTG_PGO=0 on every arm (same binary).

* **Shapes A/B** (`ab2`, 302,000 games, seed 1830000+gi, 2,000 games per arm per shape): the regression tier's d0 b0,
  d3 b10 and d5 b20 rows (and 2HG d5 b20), every one PINNED with `ignore_play_profile`. So d5 b20 is the deck's play
  setting only for Niv, Spirits and Worldfire (no enabled value_play block); for the rest it is one depth below it.
* **Play-setting A/B** (`ab3`, 78,000 games, seed **1850000**+gi, 2,000 per arm): depth OMITTED so each deck's enabled
  value_play block picks it (Bruna d7, Cyclone / Elementals / Jumbo / Mirrorwing / Selesnya (+2HG) / Stompy d6,
  Dragonstorm d5 with its beam), b20 as shipped; checked in the `[play]` lines.

Cell = avg-turn delta per game in MILLI-turns (unwon = 9; lower is better) (faster / slower), then searched CPU ratio
vs off. "play" = the play-setting row (ab3; for Niv, Spirits, Worldfire it is their d5 b20 row). Rows that never move
under any arm are omitted (Bruna, Elementals, Spirits: 0 at every shape for every arm; Jumbo / Selesnya / Stompy
searched except as listed).

| deck | shape | aUR | aUT | aURT | b | bT |
|---|---|---|---|---|---|---|
| cyclone | **play d6** | -1.0 (1/0) | 0 | 0 | **+2.5 (2/8)** | **+1.5 (4/8)** |
| cyclone | d5b20 | +0.5 (0/1) | 0 | 0 | -2.0 (6/2) | -3.5 (7/1) |
| cyclone | d3b10 | +0.5 (0/1) | 0 | 0 | -1.0 (7/5) | -3.5 (9/3) |
| cyclone | d0 | 0 | 0 | 0 | +3.0 (4/8) | +1.0 (8/7) |
| dragonstorm | **play d5** | +0.5 (0/1) | 0 | 0 | | |
| dragonstorm | d5b20 / d3b10 | +0.5 / +0.5 | 0 / +0.5 | 0 / 0 | | |
| dragonstorm | d0 | +2.0 (0/2) | +1.0 (0/2) | 0 | | |
| jumbo | d0 | +2.0 (0/3) | +2.0 (0/3) | +2.0 (0/3) | | |
| selesnyav2 | **2HG play d6** | +0.5 (0/1) | +0.5 (0/1) | +0.5 (0/1) | | |
| selesnyav2 | d0 | -0.5 (1/0) | -0.5 (1/0) | -0.5 (1/0) | | |
| stompy | d5b20 (play d6: 0) | +0.5 (0/1) | +0.5 (0/1) | +0.5 (0/1) | | |
| stompy | d0 | +1.0 (0/2) | +1.0 (0/2) | +1.0 (0/2) | | |
| mirrorwing | **play d6** | | | | 0 | 0 |
| mirrorwing | d5b20 / d3b10 | | | | +0.5 / +0.5 | +0.5 / +0.5 |
| mirrorwing | d0 | | | | -1.0 (2/0) | -1.0 (2/0) |
| niv | **play d5** | | | | +0.5 (0/1) | +0.5 (0/1) |
| niv | **2HG play d5** | | | | +1.0 (0/2) | +1.0 (0/2) |
| niv | d0 | | | | +3.5 (2/7) | +3.5 (2/7) |
| worldfire | play d5 / 2HG play / d3 | | | | 0 (1/1) each | 0 (1/1) each |
| worldfire | d0 | | | | +1.0 (0/1) | 0 |

Totals vs off (games; milli-turns per game):

| arm | play rows | other searched | d0 |
|---|---|---|---|
| aUR | 0 (1/2), +0.00 | +5 (0/5), +0.16 | +9 (1/7), +0.56 |
| aUT | +1 (0/1), +0.06 | +2 (0/2), +0.06 | +7 (1/7), +0.44 |
| aURT | +1 (0/1), +0.06 | +1 (0/1), +0.03 | +5 (1/5), +0.31 |
| b | **+8 (4/13), +0.50** | -4 (14/10), -0.22 | +13 (8/16), +1.08 |
| bT | **+6 (6/13), +0.38** | -12 (17/7), -0.67 | +7 (12/14), +0.58 |
| bT vs b | -2 (5/3), -0.12 | -8 (8/0), -0.44 | -6 (8/2), -0.50 |

CPU: every searched cell is within 0.988-1.017 of off, in both directions on arms that are byte-identical there (e.g.
aURT vs aUR on Elementals 0.998-1.000 with no game moving), so the levers cost nothing measurable. The d0 cells read
0.94-0.99 on every arm, the job-order interleave (off runs first in each chunk), not a lever effect.

### Third 7a amendment: MTG_PAY_LINE_CRIT_GROWER_LAST (G), built from the play-setting A/B

Selesnya 2HG gi837 (s1850837, d6 b20 play) lost a turn under every 7a arm (4 -> 5), and stayed 5 at d8 b0 and with
MTG_NC_SEARCH=1, while off is 4 at d8 b0 too. The logs split at T3: off pays Elvish Archdruid {1}{G}{G} from the three
lands, then Priest of Titania taps for GG (two Elves) for Fyndhorn Elves + Elvish Mystic; under 7a the land is held
for the line's later {W} and Priest pays Archdruid's {1} while it counts ONE Elf, so Mystic slips to T4 and the kill to
T5. A scaled dork's tap grows with the casts the line still makes, so it is the one creature a critical land must not
rank behind. G (crit_excluded: `IsScaledManaDork`) restores off's game exactly (same digest). It reaches only decks
with Priest / Archdruid / Accomplished Alchemist / Three Tree City-shaped sources (of the 7a decks: Selesnya, Stompy).

`ab4` (bin9 = this commit, the ab2 / ab3 seeds, aURT and aURTG on every Selesnya / Stompy shape, 40,000 games):
aURTG vs aURT = 1 faster (gi837) / 0 slower over 20,000 pairs; every other game byte-identical. bin9 aURT = bin8 aURT on
all 800 job files (the lever is inert when off).

**7a with all three amendments (aURTG) vs off: play rows 0 (0 / 0) over 18,000 games; other searched +1 (0 / 1, Stompy
d5 b20 gi1027, recovers at d8 b0); d0 +5 (1 / 5: Jumbo +4, Stompy +2, Selesnya -1; every one recovers at d8 b0).**
Bruna -- the deck 7a was written for -- does not move in a single game at any shape.

## d8 b0 replays of every slower game (ab2 + ab3), bin8

Treatment arm at `--depth 8 --budget-ms 0 --ignore-play-profile`, seed base+gi (1830000 ab2, 1850000 ab3). Recovered =
d8 <= the A/B's off turn. 96 replays: 80 recover. Unrecovered ones were replayed with off at d8 b0 (all = the A/B off
turn) and with the arm under MTG_NC_SEARCH=1:

| arm | deck | gi | shapes (off -> arm) | arm d8 b0 | off d8 b0 | arm NC d8 b0 | verdict |
|---|---|---|---|---|---|---|---|
| aUR, aUT, aURT | selesnyav2 2HG | 837 | play 4 -> 5 | 5 | 4 | 5 | real loss; FIXED by G (above) |
| aUR only | dragonstorm | 787 | d0 6 -> unwon, d3 / d5 6 -> 7 | 7 | 6 | 7 | real loss; R's own (aUT, aURT do not lose it) |
| aUR only | dragonstorm | 1431 | play 6 -> 7 | 7 | 6 | 7 | real loss; R's own (aURT does not lose it) |
| b, bT | mirrorwing | 329 | d3, d5 4 -> 5 (play: 0) | 5 | 4 | **4** | clairvoyance (NC recovers) |
| b, bT | niv (+2HG) | 537 | play d5 3 -> 4 (both formats) | 4 | 3 | 4 | **real loss** (below) |

Niv gi537 under 7b: the game splits at T1's land. Off plays Sandstone Needle (enters tapped), so T2's Needle {R}{R}
pays Izzet Signet, Island + Signet chain Brainstorm, Sol Ring and a second Signet, and T3 has 8 mana for Niv-Mizzet +
Curiosity + Brainstorm (T3 kill). Under 7b the search plays Island + Brainstorm T1 and Needle T2 (tapped), is one mana
short all game and kills T4, at every depth through d8 b0 and non-clairvoyantly. As with Dragonstorm gi679, 7b changes
a payment inside the search's own Needle-first line (payment is the greedy exemption, not searched), which devalues
that line; which pip is still OWED (re-add the temp crit trace near `crit_hold`).

Still running when this was written (b / bT, d8 b0): Worldfire gi1373 (d3 / d5 and 2HG) and gi1380 (d0), Cyclone
gi1477 (d0). None is a play-setting row except Worldfire's d5 / 2HG (Worldfire play = d5 b20), where b / bT are 0 net
(1 faster / 1 slower) anyway.

## Readings (2026-11-24)

* **The play setting reverses 7b's Cyclone result.** d3 / d5 Cyclone is better under b / bT (-6 / -14 over 4,000
  pairs), but at Cyclone's shipped d6 b20 it is worse: b +5 (2 / 8), bT +3 (4 / 8). Every one of those play-row losses
  recovers at d8 b0, so they are budget losses, but they are the shipped play and they are what the average is.
* **bT dominates b**: vs b it is -2 on play rows (5 / 3), -8 other searched (8 / 0), -6 d0 (8 / 2), never net worse on a
  deck. If 7b ships, it ships with the ritual credit.
* **7b still fails the bar**: play rows bT +6 (Cyclone +3, Niv +1, Niv 2HG +2, gi537 a real loss), d0 +7 (Niv +7).
* **7a, fully amended (aURTG), is inert where it matters**: 0 play-row games move in 18,000; the residue is +1 other
  searched and +5 d0, all recovering at d8 b0. It fails the every-deck net <= 0 bar only on d0 / one d5 game, but it
  buys nothing: not one faster game at any play setting.
