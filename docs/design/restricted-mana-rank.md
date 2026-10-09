# A narrow creature-only source pays first (MTG_RESTRICTED_MANA_RANK)

Status: **ADOPTED 2026-10-09 -- `MTG_RESTRICTED_MANA_RANK` default ON** (`=0` restores the historical
ladder; heurarm slot `RESTRICTED_MANA_RANK`). The engine-side half left open by the Karoo bounce work
(`docs/design/karoo-tap-in-response.md`, "Open for the USER").

## The defect

The mana payer spent a basic land on a creature spell's generic cost while a creature-only mana source
sat untapped. Haven of the Spirit Dragon: "{T}: Add {C}. {T}: Add one mana of any color. Spend this
mana only to cast a Dragon creature spell." (modelled as `produces` W/U/B/R/G/C + `colored_creature_only`:
the {C} pays anything, the coloured half only creature spells).

The scarcity ladder (`ManaSourceRankBase`, `src/ai/DecisionProviders.cpp`) ranks a source by how many
colours it makes -- LOWER taps first: {C}-only 5, mono 10, dual 20, tri 30, rainbow 50, and a
colour-producing land is clamped at 59. It read the Haven's six-entry produces list as a rainbow and
held it LAST of the lands (59). But in mono-red Dragons the Haven is the least flexible land on the
board: its W/U/B/G can pay nothing the deck casts, and its {R} pays only a creature spell. A Mountain
pays every generic pip and every coloured pip the Haven pays, and a noncreature {R} (Lightning Bolt,
Scourge of Valkas' firebreathing) besides.

USER 2026-10-08 on payment: *"if it is just a poor use of existing mana sources then we should fix
that part (i.e. if there was something else we could have tapped for mana then we should do so)"*.

**The case (Dragons, held-out s7007 gi959).** At the T3 Gruul Turf bounce, returning the tapped Mountain
leaves a board where the T5 kill needs Atsushi paid with the Haven, so both Mountains stay free for two
firebreaths. The payer spent a Mountain instead (Atsushi = Fire Diamond + Gruul Turf + Mountain, Haven
up): T6. Reproduced from the bounce itself (claude-play replay `0,1,0,0,-1,-1,0,-1,-1,0,1`, seed 7966,
then `--choices-then-auto`): **tip T6 at d3/20 ms and at d8 unbounded; fixed T5 at both** (Atsushi =
Haven + Fire Diamond + Gruul Turf, both Mountains firebreathe). The Haven-returned board is T5 for both
binaries. The autonomous game under the unamended bounce rule (`MTG_BOUNCE_RULE_AMEND=0`, which returns
the Mountain) goes T6 -> T5 the same way.

## The rule

Which of a restricted source's colours could ever be spent is a property of the DECK: the colours its
creature spells carry. `GameState::deck_creature_pip_colors` (stamped by `StampDeckTraits` from every
creature card's coloured pips in mainboard + sideboard, hybrid halves included; Garth holds every bit;
an unstamped state reads all five, i.e. the historical ladder).

`NarrowRestrictedMask`: a `colored_creature_only` source whose restricted colours intersect that mask in
**at most one** colour is NARROW. A narrow source is a {C} source plus at most one creature-only colour:

* it is **dominated** by any unrestricted source of its one colour (which pays every generic and
  coloured pip it pays, and noncreature pips besides), and it **dominates** a {C}-only source;
* so it ranks **9** (one rung below mono) with one usable colour, **5** (the {C}-only rung, including the
  human-play {C}-sink hold) with none;
* in the sole-colour-provider tier (`MTG_SCARCE_COLOR_HOLD`) its unusable colours are neither colours to
  protect nor colours that make another land a non-sole provider.

A WIDE source (two or more usable creature colours: Sliver Hive / Courtyard / Cavern / Unclaimed in
five-colour Slivers, Unclaimed Territory beside Dragonstorm's Karrthus {B}{G} and Kolaghan {B}, the
Minotaur / Knights / Pirates / Soldiers lands) carries real creature flexibility -- the count prior
that holds a rainbow back applies to it -- and keeps its historical rank. Only two suite decks hold a
narrow source: **Dragons** (Haven, creatures all red) and **Goblins** (Cavern of Souls, creatures all
red).

Two companions ride the same lever, because the Haven now taps first and would otherwise eat its own
activation ("{2}, {T}, Sacrifice: return target Dragon creature card from your graveyard"):

* `ComputePlanTraits` adds a plan's `GraveyardReturnAbility` source to the act-line hold
  (`MTG_ACT_LINE_HOLD`), so a cast earlier in the same plan routes around it when it can;
* the activation's own `{2}` is paid with its source held (`SelfTapSourceHoldBit` + `dmgev::PayHoldScope`
  -- a source cannot pay mana toward an ability whose {T} it owes), executor and rollout in lockstep.
  `test/scenarios/dragons_haven_rebuy_*.json` pass (the {2} taps two Mountains, not the Haven).

This is a payment ranking (the standing payment exemption), deterministic, shared by executor and
rollout; it adds no choice inside the search window.

## Measurement

`test/restricted_mana_rank_ab.sh` -- every smoke + regression + overnight row of Dragons and Goblins (and
their 2HG rows), both arms in ONE pooled batch, paired by construction: 94 jobs, 76,500 games. The OFF
arm reproduces committed GT on all 38,250 games (win turn AND digest).

| deck | set | games | faster | slower | net (turns) | units |
|---|---|---|---|---|---|---|
| dragons | held-out | 14,000 | 155 | 3 | **-152** | 0.9910 |
| dragons | train | 3,500 | 24 | 2 | **-22** | 0.9968 |
| dragons2hg | train | 50 | 1 | 0 | -1 | 1.0135 |
| goblins | held-out | 16,000 | 9 | 0 | **-11** | 0.9991 |
| goblins | train | 3,325 | 2 | 0 | -3 | 0.9998 |
| goblins2hg | held-out | 1,200 | 0 | 0 | 0 | 0.9976 |
| goblins2hg | train | 175 | 0 | 0 | 0 | 0.9994 |

By depth: Dragons searched games held-out 32 faster / 0 slower, train 2 / 0; Goblins searched 0 moved;
every slower game is a d0 game.

**Every slower game recovers** at `--depth <base win turn> --budget-ms 100` AND at `--depth 8
--budget-ms 0` (both arms escalated in one pooled batch; the lever arm equals the base arm at both
stages): dragons d0 s10010 gi1688 (6 -> 7), s4004 gi1376 (5 -> 7), s6006 gi1996 (6 -> 7), smoke s1001
gi655 (6 -> 7) and gi860 (6 -> 7).

**Their mechanism (all five, MTG_TAPDBG, both arms):** every one involves Gruul Turf, whose one tap
makes {R}{G} -- and in mono-red Dragons that {G} can pay nothing but a generic pip, so it is the least
flexible unit on the board. The per-pip greedy cannot see it, in two ways:

* **The generic drain spends the {R}** (gi655, gi1376, gi1996). A generic pip paid by the Haven (now 9)
  leaves the Turf (10) to pay the next one; its tap floats {R}{G} and the autonomous drain
  (`ConsumeFloatingAny`, C-W-U-B-R-G order) spends the {R}, keeping the {G}. The next red creature is one
  {R} short: gi655 / gi1376 Mind Stone {2} = Haven + Turf, then Atsushi / Scourge {R}{R}({R}) unpayable;
  gi1996 Fire Diamond {2} = Mind Stone + Turf, then Lathliss. Before, the Turf paid both generic pips
  whole and the Haven paid the creature's {R}.
* **The Haven's {C} pays a generic the Turf's {G} should have paid** (gi1688, gi860). A noncreature's
  generic (Dragon Tempest {1}{R}, Urza's Incubator {3}) takes the Haven(s) first; the creature that follows
  needs {R}{R}({R}) and the Turf gives one. The joint assignment (Turf's {G} to the generic, the Haven's
  {R} to the creature) exists, but the greedy settles one pip at a time and the backtracker runs only when
  the greedy fails the SAME payment.

The search sees and avoids both (0 searched slower). Neither is new: both are the per-pip greedy's
blindness to a two-mana land whose off-colour unit is generic-only, previously masked because the Haven
never paid a generic. The general fix for the first is the generic-pays-from-SURPLUS drain
(`MTG_PAY_LINE_GENERIC_ORDER`, human play only today: spend the colour the rest of the line does not
need -- here the Turf's {G} -- first). Its autonomous adoption touches every deck with a multi-colour
float and is a separate, measured decision, not taken here.

## Gates (2026-10-09)

Before the rebase onto 196d938e: mtg-test 543/543; scenarios 158/158. Smoke: 112 pass, the 6 FAILs are
exactly dragons d0/d3/d5, dragons2hg d3, goblins d0/d3 -- searched 0 slower / 3 faster / 66
play-changed at the same score, d0 2 slower (gi655, gi860 above) / 13 faster. Regression: 156 pass, the
10 FAILs are exactly dragons and goblins d0/d3/d5 -- searched 0 slower / 0 faster / 161 play-changed,
d0 0 slower / 11 faster. Viewer protocol --strict: 556 refs, 0 play-drift / shuffle-dead /
board-diverged / enum-gap / mull-drift / contract-fail.

Rebased onto 196d938e (the CounterList fix): mtg-test 549/549, scenarios 158/158, smoke and regression
byte-identical to the pre-rebase runs (same FAIL set, same digests, every other deck PASS), viewer
protocol again 556 refs / 0 drift; overnight `--deck=dragons,goblins,goblins2hg`: 25 of 28 cells move,
searched 0 slower / 32 faster / 866 play-changed, d0 3 slower / 132 faster. All 38,250 games of the
three tiers equal the A/B's lever arm (win turn and digest). GT accepted deck-scoped in all three tiers
(`--deck=dragons,dragons2hg,goblins,goblins2hg`; 41 keys + the header, all 15 provenance notes kept),
`check_gt_logs.py` consistent 685/685.

The play-changed searched games are the payment itself, spot-checked: dragons smoke d3 gi9, Scourge
{R}{R}{R} = Haven + 2 Mountains instead of 3 Mountains (two firebreaths left up, opponent -3 instead of
-2); goblins regression d3 s2002 gi247, Goblin Lackey {R} from the Cavern of Souls instead of the
Mountain, which then casts the Lightning Bolt the old payment stranded (same T4 kill, opponent -7
instead of -1).
