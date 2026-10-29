# KittyEquipment screen: a 2nd Sigarda's Aid, and Paradise Mantle as a 21st/22nd {0}-drop (2026-10-28)

Status: SCREENED. Held-out confirmation of the only improving arm (`m1_spear`) pending at the time
of writing -- see the bottom section.

## The questions (USER 2026-10-28)

1. Do we need more than one Sigarda's Aid?
2. More importantly: is Paradise Mantle worth adding as a 21st {0}-drop? The USER would not play it
   over any of the other twenty {0}-drops, only as an addition.

**Cut rules (USER, same day):** keep all 10 creatures (resilience) and every 1-of ("the 1-ofs are
there for a reason"). Legal cuts are lands and other multi-copy cards. Shadowspear 2 -> 1 is allowed
but riskier: it is the deck's only evasion, and the deck should not lose to one removal spell plus
chump blockers. A first run that cut 1-ofs (Skateboard, Deconstruction Hammer, Dragonfire Blade) was
discarded for breaking this rule.

## Apparatus

`scripts/deck_compare.py logs/kitty_screen_1028/spec.json`, shipped list as base, 17 arms x
{std, 2hg} x 4,000 paired games, one pooled batch, d5 / budget 14 (the deck's `value_play`), PGO
binary at `4df5e5201`, shared keep table (K=15, R=30), value leaf deciding.

* **Paradise Mantle was implemented for this screen** (`4df5e5201`): the host gets a real any-colour
  mana ability at every payment site, and the equip ranking now OFFERS a Mantle equip (+1 rider) --
  without that the search equipped it once in 46 probe games.
* **Mantle rides Kite Shield's keep-table bucket** (the approved alias route, nothing generated), so
  the mulligan treats it as a shield. This fixes the keep policy across arms; it cannot see whether
  Mantle wants a different keep policy.
* **Extra Sigarda's Aids are untabled compositions**: up to 3.35% of hands (aid3) fall back to the
  heuristic keep, worth about 0.002t of one-sided bias -- an order of magnitude below the effects.
* Cast order: Mantle sorts among the {0} Equipment by the USER's "cheapest first, then by name" rule
  (between Kite Shield and Spidersilk Net). Discard: Mantle is in the Equipment bucket; extra Aids
  beyond one are shed first (the existing rule keeps at most one).

## Results (negative = FASTER; avg win turn)

| arm | cut | std delta | t | 2hg delta | t |
|---|---|---|---|---|---|
| aid2_plains | Plains 8->7 | +0.0220 | +2.30 | +0.0300 | +3.13 |
| aid2_farm | Remote Farm 4->3 | +0.1110 | +9.57 | +0.1260 | +10.54 |
| aid2_den | Ancient Den 4->3 | +0.0097 | +1.14 | +0.0220 | +2.57 |
| aid2_hammer | Colossus Hammer 4->3 | +0.0490 | +6.36 | +0.0622 | +8.13 |
| aid2_gaunt | Golem-Skin Gauntlets 4->3 | +0.0315 | +4.15 | +0.0585 | +7.40 |
| aid2_kite | Kite Shield 4->3 | +0.0243 | +3.64 | +0.0483 | +7.00 |
| aid2_spear | Shadowspear 2->1 | -0.0073 | -1.01 | -0.0032 | -0.46 |
| aid3_plfarm | Plains 8->7, Farm 4->3 | +0.1690 | +10.68 | +0.1865 | +11.67 |
| m1_plains | Plains 8->7 | +0.0037 | +0.46 | +0.0092 | +1.07 |
| m1_farm | Remote Farm 4->3 | +0.0820 | +7.87 | +0.1027 | +9.34 |
| m1_den | Ancient Den 4->3 | -0.0015 | -0.21 | +0.0073 | +0.94 |
| m1_hammer | Colossus Hammer 4->3 | +0.0512 | +8.52 | +0.0555 | +8.90 |
| m1_gaunt | Golem-Skin Gauntlets 4->3 | +0.0227 | +5.05 | +0.0508 | +9.44 |
| **m1_spear** | **Shadowspear 2->1** | **-0.0147** | **-3.69** | **-0.0155** | **-3.46** |
| m2_plains | 2 Mantle, Plains 8->6 | +0.0428 | +3.54 | +0.0387 | +3.12 |
| m2_plfarm | 2 Mantle, Plains + Farm | +0.1095 | +7.87 | +0.1280 | +8.84 |
| aid2_m1 | Aid + Mantle, Plains + Farm | +0.1407 | +9.44 | +0.1643 | +10.72 |

Base: std 3.8165, 2hg 4.1623. ~80-96% of games identical per arm.

## Reading

**Sigarda's Aid: one is right.** Every legal cut for a second copy is slower, at both formats, except
Shadowspear, which is a null (-0.007 / -0.003, |t| <= 1). A third copy is the worst arm in the
screen. The card's unique value is concentrated before metalcraft (after it every equip is {0}), and
a second copy is redundant on the battlefield, so this is the expected shape.

**Paradise Mantle as a 21st {0}-drop: no land can pay for it.** For a Plains or an Ancient Den it is
a null (+0.004 / +0.009 and -0.002 / +0.007); for a Remote Farm it is clearly worse (+0.08 / +0.10)
-- the double-mana land is load-bearing. Cutting a Colossus Hammer or a Gauntlets for it is worse at
both formats. A 22nd {0}-drop (two Mantles) is worse everywhere.

**The one improvement is Mantle for the 2nd Shadowspear: -0.015 at BOTH formats, t ~ -3.5**, and it is
the same at std and 2hg (difference-of-differences -0.0008, t -0.17). It is selection-biased (the best
of 17) until the held-out confirmation below. **It is also flattered by the goldfish, in exactly the
direction the USER flagged:** Shadowspear's trample is an approved deferral (the engine has no
blocking), and the passive opponent never removes a creature, so the goldfish structurally cannot see
"lose to one removal spell and chump blockers". What it measures is the 2nd Shadowspear's +1/+1 and
lifelink against Mantle's metalcraft/draw/mana value. Read -0.015 as an upper bound on what the swap
is worth in a real game, and the decision as the USER's tradeoff of ~0.015 turns of goldfish speed
against evasion/resilience.

**Apparatus direction.** The driver's rollout-half scatter `d*` is large next to most effects, but it
is one-way: misfit lands on the arm and can only make an edit look SLOWER. So the improving arm is
conservative, while the small positive deltas (aid2_den, m1_plains, m1_den) could be partly apparatus
-- which only strengthens "no gain" rather than "a loss".

## Open

* Held-out confirmation of `m1_spear` (`--confirm m1_spear`, disjoint seeds).
* Remote Farm's measured weight (cutting one costs ~0.08-0.11t) says the mana base has no spare land
  at all; worth remembering before any future land cut on this list.
