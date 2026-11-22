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
| ba (7b-amended) | b + `MTG_PAY_LINE_CRIT_HASTE_SAC_LAST` | a `sac_for_mana_grants_haste` source (Generator Servant) paying a noncreature cost does not win over such a land (built for Niv d0 gi532) |
| h | `MTG_D0_HASTE_SAC_CHARGE` | greedy (d0 / horizon) plan score charges a plan that casts no creature for each haste-rider sac source it must sacrifice |
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
  sacrificed for Ponder with no land left at all. Recommendation to the USER: delete
  `MTG_PAY_LINE_CRIT_HASTE_SAC_LAST`.
* **The d0 haste-sac charge is clearly worse** (Niv d0 +60, 36 faster / 69 slower; searched +6). It does not even fix
  gi532 (7b + charge = T7 either way). Recommendation: delete `MTG_D0_HASTE_SAC_CHARGE`.

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
* 7b amended: gi532 does not exercise the amendment, and no board can: the subcase now pins the structural reason
  (`PaySacSpendableNow` is false for Generator Servant) and that the Servant is kept with and without the amendment,
  under 7b and behind the creature band alike.

## Open USER decisions

1. 7a / 7a-amended: neither clears the bar (a searched +3; aa d0 Selesnya +2 / Stompy +1). Default stays OFF unless the
   USER rules otherwise.
2. 7b: best overall (searched -2, d0 -7) but Cyclone searched +6 and Niv / Mirrorwing / Worldfire d0 +2 / +1 / +2.
3. Delete the inert 7b amendment and the losing d0 haste-sac charge (recommended).
