# Auras: the USER-approved cast order (`MTG_AURAS_ORDER`), built and measured (2026-10-14)

Deck: `decks/Auras/Auras.cod` (GW Bogles: 4 creatures, 14 Auras, 21 lands). Provider: `AurasProvider`.
The draft this implements, with its full reasoning: `docs/design/cast-order-drafts-2026-10-14/Auras.md`
(sections 3, 4 and 7).

## 1. The directive

* USER, on the draft: *"Otherwise the auras order seems okay. (pending tests)"*.
* The one amendment: *"Maybe Lion Umbra before Daybreak just in case."* / *"So that you could play 2 Daybreak."* /
  *"(if light paws is out)"*.
* The cross-deck ruling the same day: *"Let's fully order the cards in each list inasmuch as possible. I don't like
  the randomness of choosing the order."*
* There was no earlier USER order for this deck. What played before was the generic creature-10 / noncreature-20
  tiers, plus an agent sub-order nobody had reviewed (`MTG_AURA_CAST_ORDER`, in `GenericProvider::CastOrderRank`'s
  Light-Paws block: plain 20, scaler 22, restricted 23, live only while a Light-Paws was already in play).

## 2. The order

TOTAL (one card per position) and UNCONDITIONAL (the same on every turn), all main 1 (single-main deck):

| pos | card | class (the param rule) |
|---|---|---|
| L | land drop | first: no cantrip in the deck; folded into the searched plan at depth > 0 (unchanged) |
| 1 | Light-Paws, Emperor's Voice | creature, `aura_cast_tutor_attach`: every later Aura cast fetches |
| 2 | Kor Spiritdancer | creature, `draw_on_aura_cast` / `aura_self_buff_*`: every later Aura cast draws |
| 3 | Slippery Bogle | other creature with a hybrid pip ({G/U}) |
| 4 | Gladecover Scout | other creature ({G}) |
| 5 | Armadillo Cloak | plain Aura, MV 3 |
| 6 | Alpha Authority | plain Aura, MV 2, power +0 |
| 7 | Spirit Mantle | plain Aura, MV 2, power +1 |
| 8 | Spirit Link | plain Aura, MV 1, power +0 |
| 9 | Gryff's Boon | plain Aura, MV 1, power +1, by name |
| 10 | Hyena Umbra | plain Aura, MV 1, power +1, by name |
| 11 | Spider Umbra | plain Aura, MV 1, power +1, by name |
| 12 | Audacity | plain Aura, MV 1, power +2, by name |
| 13 | Rancor | plain Aura, MV 1, power +2, by name |
| 14 | Ancestral Mask | scaler (`aura_scale_kind` + `aura_scale_power > 0`), MV 3 |
| 15 | All That Glitters | scaler, MV 2 |
| 16 | Ethereal Armor | scaler, MV 1 -- last, so an earlier Light-Paws fetch can take a second Armor |
| 17 | Lion Umbra | restricted, `aura_enchant_requires = modified` |
| 18 | Daybreak Coronet | restricted, `aura_enchant_requires = another_aura` |

* **Plain Auras:** highest mana value first; within one mana value, the WEAKEST first by `aura_power_bonus`. Toughness,
  flying, first strike, reach, trample and lifelink are all valued 0, which is the USER's valuation for this deck.
  Equal power falls back to the name.
* **Restricted Auras:** last, for legality. Among them, `modified` goes before `another_aura`. With a Light-Paws out,
  casting the Lion Umbra first lets its fetch (mana value <= 2, a name not yet controlled) take a Coronet while ours is
  still in hand: *"so that you could play 2 Daybreak"*.
* **The name is the last key only.** It is encoded as a key that is monotone in alphabetical order over the first four
  characters (case-insensitive). It needs no card-database state, so a newly added card cannot move an existing one.
  Only copies of one card can tie, and copies are interchangeable.

**Bogle before Scout (corrected 2026-10-15).** The first build (`7c113028`, measured in ab1) read the hand-over's
rule "then the other creatures by name" and put Gladecover Scout first. The USER's settled list has Slippery Bogle >
Gladecover Scout, and the coordinator corrected it. Both are vanilla 1-mana 1/1 hexproof bodies, and the one param
that separates them is Bogle's hybrid pip ({G/U} against {G}). So the creature key is now hybrid cost first, then the
name. That is an encoding of the USER's list, not a principle of its own: in play the two are interchangeable,
since neither has a cast or enter trigger and the whole-turn prepay settles colour. ab2 (§5b) re-measures every arm
on the flipped binary.

## 3. The build (commit on the worktree branch; see §8 for hashes)

* `AurasProvider::CastOrderRank` / `CastOrderTierName` (`src/ai/DecisionProviders.cpp`, "AurasProvider::CastOrderRank").
  * The rank is `(class << 20) + name key`. Class bands are listed earliest first:
    0 = an off-list accelerant; 1 = Light-Paws; 2 = Kor; 3 = another creature with a hybrid pip (Bogle);
    4 = the other creatures (Scout); 5..68 = plain Auras, keyed by `(7 - MV) * 8 + power`; 69..76 = scalers, keyed
    by `7 - MV`; 77 = `modified`; 78 = `another_aura`; 79 = any other requirement; 80 = off-list.
  * The bands depend on params only. A card off the list keeps the generic order's one structural promise (an
    accelerant goes before the creatures) and otherwise goes last.
* **`MTG_AURAS_ORDER`** is a heurarm slot (appended at the end of the enum and of `kNames`). It is read with
  `EnvOn(..., true)`, **default ON**. `=0` returns `GenericProvider::CastOrderRank` / `CastOrderTierName`, i.e. today's
  behaviour, including the board-conditional `MTG_AURA_CAST_ORDER` block.
* `GenericProvider`'s Light-Paws block is untouched. Other decks hold Auras, and on this deck the block is simply no
  longer reached.
* **Not ORDER-OPAQUE.** None of the 18 cards carries a draw / staging / cascade / retrace / impulse / trick param
  (`OrderingOpaque`). So the clean-set `CastOrderLess` sort is what plays, in the executor (`AIEngine::TakeTurn`) and
  in the rollout (`ApplyPlanDirect`) alike, through the one shared comparator.
* **Realised order, checked in both worlds.**
  * Rollout: the unit test applies the six-cast line from 60 of the 720 hand permutations (`TurnSolver::ApplyPlan`). Each
    one realises the same order and reaches the same end board.
  * Executor: game logs on the new binary covered 500 games (300 d0, 100 d3 b10, 100 shipped), with 641 multi-cast main-1
    turns. They held 20 adjacent out-of-rank casts.
    * 17 cast a card DRAWN mid-turn by Kor or a Canopy dig. Its re-plan is a new cast list, which is the point of the
      breakpoint.
    * 3 were d0 turns where Kor drew a land, the land was played, and a card it newly paid for was cast after.
    * None is an in-hand card cast out of rank within one cast list.
* **The restricted-Aura legality guard still holds.** Restricted Auras rank after every plain Aura and scaler, so the
  SeqAura enabler-first plan sort and the apply's rank sort agree. The unit test enumerates a no-creature board with
  Bogle, Rancor, Armor, Coronet and Lion Umbra in hand. In every line that casts a restricted Aura, that Aura is realised
  after every other Aura and lands on a host that carries another Aura. The cast-ordering search keeps its own guard
  (`OrderingPlacesRestrictedAuraBeforeEnabler`).
* **Unit tests:** `test/unit/test_auras_cast_order.cpp` (7 cases). The full suite passes 1060/1060.

## 4. The two USER questions, built as default-OFF arms (measured, NOT shipped)

### (a) `MTG_AURAS_ORDER_SEARCH`: search the Aura order on Light-Paws turns (Q19)

* **Trigger:** `AurasProvider::WantsCastOrderingSearch()` opens the engine's existing cast-ordering expansion
  (`EnumeratePlans`, `OrderingSearchEnabled`) for this deck.
* **Candidates:** `CastOrderingCandidates` hands it every distinct NAME order of the plan's Auras when both hold:
  * a Light-Paws is in play or cast by the plan;
  * the plan holds 2+ Auras.
* **Fixed parts:** the approved order is the first candidate, so it survives the end-state dedup. Creatures keep their
  slots ahead of the Auras.
* **Engine guards:** the engine applies each order on a copy, dedups by end state, and drops orders that put an Aura
  before its creature, or a restricted Aura before its enabler.
* **Every other plan gets no candidates.** The caller then keeps the plan exactly as it was: no `searched_order`, no extra
  apply.
* **Cap:** more than 120 distinct orders keeps the approved order only (the engine's own cap).

### (b) `MTG_LP_FETCH_USER_RULE`: the Light-Paws fetch follows the 08-25 wording (Q20)

* **The USER's rule:** *"take the SCALER iff this is the last 1-drop Aura fetch in the plan OR the scaler is about to
  be cast from hand before another fetch fires, else take the flat Aura"*.
* **What mode 4 does today:** it calls a fetch "last" only when the hand holds no Aura of ANY mana value castable this
  turn OR NEXT (`MV <= lands + 1`).
* **What the arm does:** a scaler leads iff no LATER fetch of this turn's line can still take it.
  * **The line's remaining Aura casts.** The plan is not visible at a resolution, so they are approximated as the hand's
    Auras in the deck's cast order. Each is kept while `AvailableManaPool` still pays it on top of the ones before it.
    That pool is the untapped sources plus the prepaid float that the plan's later casts drain.
  * **Walking them in order:**
    * A same-name copy first means ours is cast before another fetch can take it, so take the scaler NOW (the second
      clause).
    * A cast of mana value >= the scaler's first means that cast's fetch can still take it, so take the flat Aura.
    * Neither means this is the last fetch that can reach it, so take it.
* **Finding (b), Mask over a duplicate Armor.** A last-chance scaler competes with the others on realized power. So a
  last-chance Mask (+2 per other enchantment) beats a duplicate Armor (+1 each). The pre-arm rule let an Armor copy in
  hand outrank a Mask unconditionally.

## 5. Measurement: the pooled A/B (COMPLETE)

### 5a. ab1 -- the first build `7c113028` (Gladecover Scout before Slippery Bogle)

* **Setup.**
  * old = the integration tip `ff3e205b`, built and kept as `build/scratch/auras-order/bin/mtg.base`.
  * new = commit `7c113028`, kept as `bin/mtg.new`.
  * One OLD batch, then one NEW batch, one at a time, 20 threads each, paired by game index. Seeds are held out
    from 11000001. Loss scores 9 (`max_turns` 8). `MTG_DUMP_WINS=1` and `MTG_DUMP_UNITS=1` were set.
* **Cells.**
  * shipped: no depth key, `budget_ms` 20, with the profile, value leaf and keep table in place (the suite's d5 rows).
  * d3 b10 and d0: `ignore_play_profile`.
  * auras sizes: 4000 / 6000 / 20000 games. auras2hg sizes: 2000 / 3000 / 10000 games.
* **Arms (new binary, per-job heurarm flags, all in one batch):**
  * order = the default;
  * order_a = + `MTG_AURAS_ORDER_SEARCH`;
  * order_b = + `MTG_LP_FETCH_USER_RULE`;
  * order_ab = both;
  * off = `MTG_AURAS_ORDER=0`, on the first quarter of every cell.
* **Identity.**
  * Every other suite deck: 32 decks x {d0, d3 b10, shipped} x 20 games = 1920 games, new default against old.
    **1920 / 1920 byte-identical** (win turn and play digest).
  * `MTG_AURAS_ORDER=0` against old: **11250 / 11250 byte-identical**.

| cell | arm | games | faster | slower | net | d avg | play-changed | units ratio |
|---|---|---|---|---|---|---|---|---|
| auras shipped | **order** | 4000 | 0 | 0 | **0** | 0.0000 | 1121 | 1.0000 |
| auras shipped | order_a | 4000 | 6 | 3 | -3 | -0.0008 | 1125 | 1.0194 |
| auras shipped | order_b | 4000 | 10 | 0 | -10 | -0.0025 | 1473 | 0.9992 |
| auras shipped | order_ab | 4000 | 11 | 3 | -8 | -0.0020 | 1477 | 1.0188 |
| auras d3 b10 | **order** | 6000 | 0 | 0 | **0** | 0.0000 | 1678 | 0.9999 |
| auras d3 b10 | order_a | 6000 | 10 | 3 | -7 | -0.0012 | 1684 | 1.0010 |
| auras d3 b10 | order_b | 6000 | 12 | 2 | -10 | -0.0017 | 2233 | 0.9980 |
| auras d3 b10 | order_ab | 6000 | 18 | 5 | -13 | -0.0022 | 2236 | 0.9999 |
| auras d0 | **order** | 20000 | 3 | 3 | **0** | 0.0000 | 5698 | - |
| auras d0 | order_a | 20000 | 3 | 3 | 0 | 0.0000 | 5698 | - |
| auras d0 | order_b | 20000 | 74 | 14 | -60 | -0.0030 | 7560 | - |
| auras d0 | order_ab | 20000 | 74 | 14 | -60 | -0.0030 | 7560 | - |
| auras2hg shipped | **order** | 2000 | 2 | 0 | **-2** | -0.0010 | 570 | 0.9999 |
| auras2hg shipped | order_a | 2000 | 3 | 5 | +2 | +0.0010 | 579 | 1.0146 |
| auras2hg shipped | order_b | 2000 | 13 | 3 | -10 | -0.0050 | 734 | 0.9975 |
| auras2hg shipped | order_ab | 2000 | 13 | 7 | -6 | -0.0030 | 741 | 1.0125 |
| auras2hg d3 b10 | **order** | 3000 | 1 | 1 | **0** | 0.0000 | 852 | 1.0012 |
| auras2hg d3 b10 | order_a | 3000 | 3 | 3 | 0 | 0.0000 | 874 | 1.0020 |
| auras2hg d3 b10 | order_b | 3000 | 13 | 5 | -8 | -0.0027 | 1125 | 1.0011 |
| auras2hg d3 b10 | order_ab | 3000 | 14 | 8 | -6 | -0.0020 | 1145 | 1.0010 |
| auras2hg d0 | **order** | 10000 | 7 | 7 | **0** | 0.0000 | 2919 | - |
| auras2hg d0 | order_a | 10000 | 7 | 7 | 0 | 0.0000 | 2919 | - |
| auras2hg d0 | order_b | 10000 | 50 | 30 | -20 | -0.0020 | 3859 | - |
| auras2hg d0 | order_ab | 10000 | 50 | 30 | -20 | -0.0020 | 3859 | - |

* **net** is the summed win-turn delta against old over the paired games (negative = faster).
* **units ratio** is arm / old summed search units over the same games. d0 does no search, so it has no units.
* Wall time is not reported: the box ran at load ~60 on 24 cores throughout.
* **Reading.**
  * **The order alone** is aggregate-neutral or better in all 6 cells (0, 0, 0, -2, 0, 0) at unchanged work (units
    0.9999-1.0012). It changes play in about 25-30% of games, almost always at the same win turn.
  * **(b), the fetch rule**, is better in all 6 cells (-10, -10, -60, -10, -8, -20), with work unchanged or slightly
    lower.
  * **(a), the search**, is mixed. It changes nothing at d0, where there is no search window. It costs +1.5-2% units at
    shipped, and auras2hg shipped is +2, i.e. worse.
  * **a+b** is no better than b alone anywhere. It is worse than b in 4 cells and equal at d0.

### 5b. ab2 -- the shipped build `fe342a71` (Slippery Bogle before Gladecover Scout)

* **What was re-run.** Every arm, every cell, the same seeds and sizes as ab1 (180,000 games). It was one pooled batch
  through the box lock, 2026-10-15 04:51-05:15Z, run from an ext4 mirror of the inputs (byte-identical copies, cmp'd).
* **What was not re-run.**
  * The old side is ab1's old batch: same seeds, same inputs.
  * Identity was not re-run. The flip changes only `AurasProvider::CastOrderRank`'s creature branch, a function no
    other deck reaches. Identity was proven on `7c113028`: 1920/1920 other-deck games and 11250/11250 `=0` games.
* **Result: every faster / slower / net count is the same as ab1, in every arm and cell.**
  * Play-changed counts move by single digits (e.g. auras shipped order 1121 -> 1115).
  * Units ratios move by <= 0.0002.
  * The 156 slower games are the same games: 11 order, 24 a, 54 b, 67 a+b.

| cell | order | order_a | order_b | order_ab | units ratio order / a / b / ab |
|---|---|---|---|---|---|
| auras shipped (4000) | 0 (0 / 0) | -3 (6 / 3) | -10 (10 / 0) | -8 (11 / 3) | 1.0001 / 1.0195 / 0.9993 / 1.0188 |
| auras d3 b10 (6000) | 0 (0 / 0) | -7 (10 / 3) | -10 (12 / 2) | -13 (18 / 5) | 0.9999 / 1.0010 / 0.9980 / 0.9999 |
| auras d0 (20000) | 0 (3 / 3) | 0 (3 / 3) | -60 (74 / 14) | -60 (74 / 14) | - |
| auras2hg shipped (2000) | -2 (2 / 0) | +2 (3 / 5) | -10 (13 / 3) | -6 (13 / 7) | 1.0001 / 1.0147 / 0.9976 / 1.0127 |
| auras2hg d3 b10 (3000) | 0 (1 / 1) | 0 (3 / 3) | -8 (13 / 5) | -6 (14 / 8) | 1.0012 / 1.0020 / 1.0011 / 1.0010 |
| auras2hg d0 (10000) | 0 (7 / 7) | 0 (7 / 7) | -20 (50 / 30) | -20 (50 / 30) | - |

Each cell reads "net (faster / slower)", where net is the summed win-turn delta over paired games and loss = 9.

## 6. The order's slower games: 11 of 45,000, one mechanism, 2 do not recover

**Recovery check** (the USER's two stages, on the shipped build `fe342a71`, order alone):
* Stage 1 is `--depth W --budget-ms 100` with `W = max(5, old win turn)`, i.e. 5.
* Stage 2 is `--depth 8 --budget-ms 0`, run on the order arm AND on the old binary.

The 11 games, each run single-game (`--threads 1`) from the ext4 mirror:

| cell | gi | repro `--seed` | old -> new | stage 1 | stage 2 (new / old) |
|---|---|---|---|---|---|
| auras2hg d3 b10 | 1665 | 11401666 | 4 -> 5 | 5: no | **5 / 4: no** |
| auras2hg d0 | 8546 | 11308547 | 4 -> 5 | 5: no | **5 / 4: no** |
| auras2hg d0 | 695 | 11300696 | 4 -> 5 | 4: recovers | - |
| auras2hg d0 | 924 | 11300925 | 4 -> 5 | 4: recovers | - |
| auras2hg d0 | 1638 | 11301639 | 4 -> 5 | 4: recovers | - |
| auras2hg d0 | 2252 | 11302253 | 4 -> 5 | 4: recovers | - |
| auras2hg d0 | 2450 | 11302451 | 4 -> 5 | 4: recovers | - |
| auras2hg d0 | 4689 | 11304690 | 4 -> 5 | 4: recovers | - |
| auras d0 | 4082 | 11004083 | 5 -> 6 | 4: recovers | - |
| auras d0 | 6808 | 11006809 | 4 -> 5 | 4: recovers | - |
| auras d0 | 12561 | 11012562 | 5 -> 6 | 4: recovers | - |

**One mechanism for all 11, read off the per-turn diffs** (`logs/auras-order/firstdiff.py`). Every game has the same
opening hand on both sides, so this is not a draw divergence.

* **The board:** Light-Paws is in play, and the turn casts one MANA-VALUE-2 plain Aura (Alpha Authority or Spirit
  Mantle) together with one or two 1-drop plain Auras.
* **Old line:** the generic tier tied all of them, so plan order put the 1-drop first.
  * The 1-drop's fetch took a 1-drop (Ethereal Armor, or Audacity / Rancor).
  * The 2-drop's fetch was then the LAST fetch, so mode 4 let the scaler lead: All That Glitters.
* **The approved order** puts the 2-drop first (highest MV first: positions 6 / 7 ahead of 8-13).
  * Its fetch is now not the last, because mode 4's "another Aura castable this turn or next" counts the 1-drop.
  * A 1-drop's fetch can never take a 2-drop, so that was the last chance at Glitters, and it is passed over.
  * The fetch takes Daybreak Coronet (+3 flat, legal now that Light-Paws carries the 2-drop), Lion Umbra, or Ethereal
    Armor. The 1-drop's fetch then takes the Armor (or Rancor).
  * Glitters (+1 per artifact or enchantment, about +4 by the end of the turn) is lost, and the board is 1-2 power
    smaller.
* **The slot** is the MV-2 plains (positions 6-7) ahead of the MV-1 plains. The interaction is the fetch rule's
  MV-blind "last fetch" test, which is exactly the draft's finding (a) and the USER's open Q20.
* **Not clairvoyant.** Glitters is fetched from the library by the fetch's own search; no future draw is involved.
* **Why it cannot recover under the order alone:** the order fixes the casting sequence, and the fetch is a provider
  pick, not a searched decision. At `--depth 8 --budget-ms 0` the search plays the identical T3 line, and the old binary
  still wins T4.
* **Why 9 recover at stage 1:** the deeper search finds another winning line within the old win turn.
* **Under arm (b), the USER's 08-25 rule, every one of the 11 is fixed at its own cell setting.**
  * Re-run single-game on `fe342a71` with `MTG_LP_FETCH_USER_RULE=1`, each takes Glitters at the 2-drop's fetch (no
    later fetch of the line can reach an MV-2 scaler).
  * All 11 return to the old win turn: 1665 and the seven 2HG d0 games win T4; 4082 and 12561 win T5; 6808 wins T4.
  * gi1665 under b also ends on a board identical to old's.

**So the order alone does NOT meet the bar:** net <= 0 in every cell, but 2 slower games do not recover. The amendment
that keeps the USER's order is arm (b), the USER's own fetch rule. It is built, measured (§5), and fixes all 11; it is
OFF pending Q20. Arms (b) and (a+b) have slower games of their own: 54 and 67. Their recovery check is in §6b.

### 6b. Arm (b)'s slower games: 54, of which 13 do not recover -- and the narrower reading (b2) that targets them

The same serial two-stage check on arm (b) (`logs/auras-order/recover_serial.py`, on the shipped build):
* **41 of 54 recover at stage 1.**
* **13 do not.** In every one of them stage 2 is still one turn later, and the old binary at stage 2 keeps its
  faster win.
  * auras d3 b10: gi 511, 5471.
  * auras2hg shipped: gi 135, 878, 1357.
  * auras2hg d0: gi 4097, 7032, 8633.
  * auras2hg d3 b10: gi 1643, 1666, 1761, 1863, 2727.
* **None of the 13 is slower under the order alone.** Arm (b) is what slows them.

**Root cause, read off the diffs (identical opening hands):**
* **The board:** T3, Light-Paws out, ONE Aura cast this turn: Spirit Mantle, Ancestral Mask or All That Glitters.
  The hand holds more Auras castable NEXT turn.
* **Old (mode 4):** "another Aura castable this turn or next" says this is not the last fetch, so the flat Aura wins on
  realized power: Daybreak Coronet, +3.
* **Arm (b)** reads "last fetch IN THE PLAN" as this turn only.
  * With this turn's mana spent, it calls the fetch last-chance and takes the scaler: Glitters or Armor, +2 at that
    board.
  * The T3 attack is 1 short (e.g. 25 vs 24 on the 2HG board), and the kill slips a turn.
  * Where the plan differs too (1357, 1643, 1666, 1863, 2727), the search re-planned the turn around the different
    fetch.

So the literal reading over-applies the scaler preference on the last fetch of a turn whose NEXT turn still casts Auras.
The order's 11 games show the opposite defect: an MV-1 Aura in hand counted as a later fetch for an MV-2 scaler.

**(b2) `MTG_LP_FETCH_MV_AWARE`, the narrow reading, built as a third default-OFF report arm.**
* Mode 4 is kept as it is, including its "this turn or next" horizon and its same-name clause.
* The one change: an Aura in hand counts as a later fetch for a candidate only if its mana value reaches the
  candidate's. A 1-drop's fetch can never take All That Glitters.
* By the hands in the logs, it should fix the order's two non-recovering games, because no 2-drop+ is in hand there.
* It should leave arm (b)'s 13 as old. In the 7 hands read (511, 5471, 135, 878, 4097, 8633, 1761), each still holds an
  Aura castable next turn whose mana value reaches the scaler arm (b) took: Gryff's Boon for an Armor, or Alpha
  Authority / Coronet / Lion Umbra / a second Mask / Glitters for a Glitters or Armor.
* Measured in §5c.

## 7. RESUME (wrapped 2026-10-14 ~13:10Z for the night at the coordinator's request: the box is needed)

* **Base:** `ff3e205b` (work/integ-2026-10-14).
* **Built (commit `7c113028`):** the order + arms (a) and (b) + 7 unit tests. Unit tests 1060/1060.
* **Measured (complete):** §5. Identity 1920/1920, =0 identity 11250/11250.
* **Not yet run:**
  1. **The two-stage recovery check** of the 11 order-arm slower games, and of the 145 arm games if wanted.
     * Run `bash logs/auras-order/run_recover.sh ab1 order` (add `order_a order_b order_ab` for the arms).
     * It reads `build/scratch/auras-order/ab1/cmp.slower.json`; a copy is in `logs/auras-order/ab1/`.
     * Stage 1: `--depth W --budget-ms 100`, `W = min(8, max(5, old win turn))`.
     * Stage 2: the stage-1 failures at `--depth 8 --budget-ms 0`, on the arm and on the old binary.
     * Each stage is one pooled batch.
  2. **Root-cause** every order-arm game that does not recover. The rest are expected to be the same fetch-rule
     interaction as gi1665: check each with `bash logs/auras-order/repro.sh <tag> <seed> <gi> <2hg> <depth|shipped>
     <budget> [MTG_LP_FETCH_USER_RULE=1]`.
  3. **The Auras references, old against new.** Run `bash logs/auras-order/run_refs.sh`:
     `viewer_protocol_check.py --strict --only references/Auras/` plus `ref_bench --deck Auras`, on both binaries.
  4. Update `docs/design/cast-order-rankings.md` § Auras with the new `--cast-order-report`. The text is in
     `build/scratch/auras-order/glog/report.txt`.
  5. **The default decision** (§8). It is still default ON in the commit, as the task's default.
* **Resume inputs.**
  * Manifests: `logs/auras-order/ab1/{old,new}.json`, generated by `logs/auras-order/mkmanifest.py`.
  * Per-game `.wins` / `.units`: `build/scratch/auras-order/ab1/{old,new}/` (build volume).
  * Summaries on the host drive: `logs/auras-order/ab1/`.
  * Seeds:
    * auras: d0 11000001+, d3 11100001+, shipped 11200001+.
    * auras2hg: d0 11300001+, d3 11400001+, shipped 11500001+.
    * identity: d0 11600001+, d3 11700001+, shipped 11800001+.

## 8. Defaults and the USER's questions (open)

* `MTG_AURAS_ORDER` is **default ON** in the commit. Whether it meets the bar ("every slower game recovers") is not yet
  established: its aggregate is net <= 0 in every cell, but gi1665 likely does not recover.
* (a) `MTG_AURAS_ORDER_SEARCH` and (b) `MTG_LP_FETCH_USER_RULE`: **default OFF**, as instructed, with their numbers above.
* **Q1 (naming):** the order as handed over listed Slippery Bogle before Gladecover Scout, but its rule says "by name".
  I applied the rule (Scout first). The two are interchangeable in play.
* **Q2 (Q20):** adopt the 08-25 fetch rule (b)? It is better in all 6 cells, and it is the fix for the order's one
  root-caused slower game.
* **Q3 (Q19):** the Aura-order search (a) is mixed and costs ~2% units at shipped. My recommendation is not to adopt it.
