# Soldiers + BreachingDragonstorm: the USER-approved cast orders, implemented and measured (2026-10-15)

Both orders were settled in the 2026-10-14 walk-through (`cast-order-review-2026-10-14.md`, "Soldiers --
SETTLED" and "BreachingDragonstorm -- SETTLED"); the drafts are `cast-order-drafts-2026-10-14/Soldiers.md`
and `.../BreachingDragonstorm.md`. This doc records what was built, how it was measured, and what the
numbers say. Per-deck tables also live in `cast-order-rankings.md` ("Soldiers", "BreachingDragonstorm").

## 1. The rulings

- **Soldiers.** USER: *"Soldiers' order also looks good."* Main 1: land > Champion of the Parish > Recruiter
  of the Guard > Ranger-Captain of Eos > Thalia's Lieutenant (at 4, the amendment: after Champion and the
  tutors) > Aether Vial > Recruitment Officer > Esper Sentinel > Thalia, Guardian of Thraben > Coppercoat
  Vanguard > Cathar Commando > Jirina > Harbin > Brutal Cathar > Field Marshal > General Kudro > King Darien
  XLVIII > Rick (positions 6-17 order-free; the measured 10-04 sequence keeps it total).
- **BreachingDragonstorm.** USER: *"I'm not too worried about Breaching Dragonstorm's order, since it
  literally dumps most of the creatures in the deck on the field. I don't think we can fail to kill with
  essentially any order so your order is fine. Plus, there is almost no chance of casting two of these
  spells in one turn. The cheapest is 5 mana, so that would need 10 mana."* Main 1: land > Breaching
  Dragonstorm > Boarding Party > Annoyed Altisaur > Maelstrom Wanderer > Creative Technique > Call Forth the
  Tempest > Sakashima's Protege, with the provider opting into `OrderOpaqueCastsByRank` so the rank applies.
- **Cross-deck (both):** *"Let's fully order the cards in each list inasmuch as possible. I don't like the
  randomness of choosing the order."* -- no two different cards may compare equal; a tie falls to the card
  NAME, never to plan / hand order.

## 2. What was built

| commit | what |
|---|---|
| `c6719bc8` | `DecisionProvider::CastOrderTotalByName` + `CastOrderNameTieLess` -- the name tie-break, read by the shared comparator (`CastOrderLessRanked`) and the enumeration's sequential payability walk. An EXACT copy of the EldraziDisplacerFlicker agent's hook (worktree `agent-a2c348fc`, `f6b9263c`+) so the integrator keeps one copy. Default false: byte-identical. |
| `67ed4a88` | Soldiers: `MTG_SOLDIERS_ORDER` amended to the approved order, default ON, total by name; the two wrong "user-proposed" labels fixed (the order was AGENT-AUTHORED 2026-10-04 and is USER-APPROVED 2026-10-14). |
| `38b42d8c` | BreachingDragonstorm: `MTG_BREACH_ORDER` (new heurarm slot `BREACH_ORDER`, appended), default ON -- `CastOrderRank`, `CastOrderTierName`, `OrderOpaqueCastsByRank`, `CastOrderTotalByName`; new scenario `breaching_protege_after_dragonstorm`. |
| `047c506e` | `test/unit/test_soldiers_breach_cast_order.cpp` (7 cases). |

### Soldiers ranks (by params; names only within a mana value)

| slot | card | key |
|---|---|---|
| 1 | Champion of the Parish | `own_creature_enters_self_counters` (without the Lieutenant's ETB) |
| 2 | Recruiter of the Guard | `tutor_to_hand` + `tutor_max_toughness` |
| 3 | Ranger-Captain of Eos | `tutor_to_hand` |
| 4 | Thalia's Lieutenant | `etb_each_other_own_creature_counters` (tested first: it also carries the watcher param) |
| 5 | Aether Vial | `upkeep_adds_charge` |
| 6 | Recruitment Officer | `activated_dig_count` |
| 7-17 | the order-free block | `100 + 10*mv + k`: cheapest first by mana value (a param); within a mana value the 10-04 sequence (`k`, by name). Thalia (`noncreature_spell_tax`) holds k = 0 of mana value 2. Esper Sentinel 111; Thalia 120, Coppercoat 121, Cathar Commando 122, Jirina 123, Harbin 124; Brutal Cathar 131, Field Marshal 132, Kudro 133, King Darien 134; Rick 141 |
| - | any card the deck does not hold | `200 +` the generic rank, then NAME |

The 10-04 sequence never puts a dearer card ahead of a cheaper one, so "cheapest first, the measured
sequence within a mana value" reproduces the approved list exactly (pinned by the unit test).

### BreachingDragonstorm ranks (all by params)

| rank | card | key |
|---|---|---|
| 10 | Breaching Dragonstorm | `etb_exile_until_nonland` -- enters before its own walk, so a later Protege can copy it |
| 26 / 27 / 28 | Boarding Party / Annoyed Altisaur / Maelstrom Wanderer | creature with `cascade_max_mv` m: `20 + m`, narrowest walk first (a cascade resolves before its creature enters, CR 702.85a) |
| 40 | Creative Technique | `shuffle_reveal_freecast` -- a sorcery, after the entrants |
| 50 | Call Forth the Tempest | `damage_opp_creatures_mv_cast` -- after the casts it counts |
| 60 | Sakashima's Protege | `enter_as_copy_of_entrant` (tested first; it also has cascade) -- LAST, every earlier cast is a copy target |
| 100+ | any card the deck does not hold | generic rank + 100, then NAME |

**The opt-in is the essential part.** Every nonland card in the 60 is `OrderingOpaque` (cascade, Creative
Technique, the Dragonstorm walk), so before this the rank was never read: the executor, the breakpoint
continuation and the rollout all cast in plan (= hand) order. `=0` restores exactly that.

## 3. Verification

- **Unit tests:** 1060/1060 pass (`mtg-test`), including the 7 new cases: each deck's rank lays out the approved
  list exactly; the comparator is TOTAL over the deck's distinct names plus two cards the rank does not know
  (they fall to the NAME); 42 permutations of the plan vector (Soldiers also with Vial puts in place of
  casts) all realise the approved order; lever off = the old order.
- **Scenarios:** 161/161 pass on the new binary. The new `breaching_protege_after_dragonstorm` is T6 with
  the order, T7 on the base binary and with `MTG_BREACH_ORDER=0`. `soldiers_vial_after_tutor_continuation`
  (pinned `MTG_SOLDIERS_ORDER=1`) passes on both.
- **The drafts' probes, old -> new** (single games, threads 1): Soldiers s8800010 gi10, gi302, gi308 go
  **T5 -> T4** at shipped play settings (d5/b20 default + keep table + value leaf) and at d5 b20, exactly the
  draft's §4.1 claim; gi10 at d0 stays T5. Breaching s1001 gi0 is unchanged at d0 (T5) and d5 b20 (T3).
- **`--cast-order-report`** prints both decks in the approved order (the m2 labels on Esper Sentinel, Thalia,
  Cathar Commando and Annoyed Altisaur are inert: neither deck uses a second main).

## 4. Measurement

ONE pooled `mtg --batch` per binary (a batch runs one binary), back to back in one hold of the box lock,
each holding all four decks and every cell. Old = `mtg.base` (integration tip `16dfb7e4`); new = both orders
ON (manifest flags). Held-out seeds from 11,700,001; loss = turn 9; paired per game. Cells: shipped (no
depth / budget: the profile's play settings, keep table + value leaf in place), d3 b10, d0.

| deck | cell | games | faster | slower | net (turns) | per game | t | units new/old | old avg -> new avg |
|---|---|---|---|---|---|---|---|---|---|
| soldiers | shipped | 4000 | 19 | **0** | -19 | -0.00475 | -4.37 | 0.9865 | 4.2505 -> 4.2458 |
| soldiers | d3 b10 | 6000 | 32 | **0** | -32 | -0.00533 | -5.67 | 0.9966 | 4.2555 -> 4.2502 |
| soldiers | d0 | 20000 | 295 | **0** | -295 | -0.01475 | -17.30 | - | 4.4511 -> 4.4363 |
| soldiers2hg | shipped | 2000 | 28 | **0** | -28 | -0.01400 | -5.33 | 0.9588 | 4.8940 -> 4.8800 |
| soldiers2hg | d3 b10 | 3000 | 57 | **0** | -57 | -0.01900 | -7.62 | 0.9872 | 4.9097 -> 4.8907 |
| soldiers2hg | d0 | 10000 | 186 | **0** | -186 | -0.01860 | -13.77 | - | 5.1050 -> 5.0864 |
| breaching | shipped | 4000 | 0 | 0 | 0 | 0 | - | 1.0000 | 3.0903 = |
| breaching | d3 b10 | 6000 | 0 | 0 | 0 | 0 | - | 1.0000 | 3.0938 = |
| breaching | d0 | 20000 | 0 | 0 | 0 | 0 | - | - | 4.8169 = |
| breaching2hg | shipped | 2000 | 0 | 0 | 0 | 0 | - | 1.0000 | 3.0930 = |
| breaching2hg | d3 b10 | 3000 | 0 | 0 | 0 | 0 | - | 1.0000 | 3.0910 = |
| breaching2hg | d0 | 10000 | 0 | 0 | 0 | 0 | - | - | 4.8196 = |

Units over the four decks' searched cells: 97,565,809 -> 96,242,345 (**0.9864**).

- **Soldiers: no slower game in any cell** (59,000 games), so there is nothing to recover. Play changed in
  ~half the games at every cell (e.g. 1,910 of 4,000 shipped games have a different decision digest, 1,891
  of them at the same win turn): the order decides the sequence of almost every multi-cast main.
- **BreachingDragonstorm: no win turn moved in 45,000 games.** The searched cells are byte-identical even in
  their play digests: those games win by ~T3, and a main with two hand casts needs 10 mana (turn 6+). At d0,
  6 games changed play at the same win turn -- each a T6+ two-cast main now cast in the order (spot-checked:
  breaching d0 gi 8803, `Creative Technique|Breaching Dragonstorm` -> `Breaching Dragonstorm|Creative
  Technique`, T6 both). The lever is live at depth > 0 too (the scenario's depth-3 trace: base casts
  `Sakashima's Protege|Breaching Dragonstorm`, new `Breaching Dragonstorm|Sakashima's Protege`). This is the
  USER's own expectation ("I don't think we can fail to kill with essentially any order") borne out.
- **=0 hatch:** the new binary with both flags 0 equals the base binary game for game (turn AND digest) on
  300 games of every cell of all four decks (3,600 games).
- **Identity:** every other suite deck (31) at shipped / d3 b10 / d0, 25 games each (93 jobs, 2,325 games per
  binary): every per-game turn and digest, and every job digest, identical.
- **References:** `scripts/ref_bench.py` (Soldiers 30, BreachingDragonstorm 10) is identical old and new:
  Soldiers search 4.133 vs human 4.200 (0/30 short, 2 faster than the human), BreachingDragonstorm 3.200 =
  human (0/10 short). `test/viewer_protocol_check.py --strict`: 30 ok, 10 repaired, 0 play-drift / enum-gap /
  board-diverged, identical on both binaries (the 10 "repaired" are BreachingDragonstorm references that
  predate a decision point -- informational, unchanged by this work).

**Verdict: both orders meet the bar** (every deck and cell net <= 0, no slower game) and ship **default ON**
(`=0` restores the old order byte-for-byte). Suite ground truth is not re-accepted here: soldiers and
soldiers2hg cells will FAIL on digest (and on avg where the cell holds a moved game) in smoke / regression /
overnight until accepted; breaching's searched cells should stay PASS and only a d0 cell can move by digest.

## 5. Findings outside the order

- **Sakashima's Protege's copy pick at depth > 0** (scenario `breaching_protege_after_dragonstorm`). Eleven
  lands, hand Protege + Breaching Dragonstorm, the top two cards Boarding Party, opponent at 12. With the
  order, Dragonstorm resolves first, its walk free-casts the top Boarding Party, and the Protege enters as a
  copy of it: 12 hasty power, T6 (depth 0; hand order gave T7). At depth 3 b200 and depth 5 b0 the search
  casts in the same new order but its OWN line (MTG_FD_TRACE: `LINE win=7`, T6 opponent at 6) has the Protege
  entering as itself, so the executor replays that. Not caused by the order (the order is realised), not
  root-caused here: the Protege's copy source is a per-plan variant (decline / heuristic / one per battlefield
  entrant at enumeration time), and the entrant that matters only exists after the Dragonstorm's walk. Logged
  for whoever owns the Protege's copy choice; the fixture is pinned at depth 0 meanwhile.
