# Maze's End: making the mana-clean gate's verdict meaningful (2026-11-20)

Branch `wip/maze-gate`, on the shared branch at `c86a86333` (src tree `31070f52`). The input is gate
`claude-queue-20261119i` (5,000 games, src `8deb2c93`, before the oracle decision budget went 600 -> 1300 priced s
and before the backstop moved to thread CPU): maze_s_end 287,482 issues, of which UNMODELED 139,380,
CAPPED-DECISION 131,360, CAPPED 13,729; the real classes APPLY-DROP 1,909, ENUM-GAP 617, ORACLE-GAP 410,
DOMINATED-ONESHOT 48, APPLY-DROP-ORDER 29. Report: `logs/mana_clean/claude-queue-20261119i/report.md` (primary
checkout). Everything here is ANALYZER-side (the mana oracle, `src/ai/ManaOracle.cpp`): play is not changed.

(Status: IN PROGRESS -- sections are filled as the measurements land.)

## 1. UNMODELED: the engine's non-mana Gate taps

### Mechanism

139,000 of the 139,380 were the note "engine tapped a mana source outside any payment", and every top signature named
the Gate the provider's rank picked ("a mana choice the oracle does not drive: Gond Gate" / "Talon Gates of Madara").
Two effects in the list tap Gates for something other than mana (texts read from `src/cards/data/cards.json`):

* **Guild Summit**: "When this enchantment enters, you may tap any number of untapped Gates you control. Draw a card
  for each Gate tapped this way." The COUNT is searched (the ETB choice axis, `chosen_x = N + 1`; the deck
  provider's menu is {rest-of-line, 0, all}); WHICH Gates is `MazesEndProvider::EtbTapSubtypeOrder` (pure-mana
  Gates first by ManaSourceRank, Baldur's Gate next, a Gate with a live non-mana {T} last) -- the mana-payment
  exemption, ledger decisions 2/10/30 (USER sign-off pending there).
* **Heap Gate**: "{1}, {T}, Tap an untapped Gate you control: Create a Treasure token." The other Gate is picked by the
  same order (`PayActivationOtherTap`).

The oracle's unscripted-tap check (Realize) flags any root mana source that ends tapped without the oracle's script
having tapped it. Those Gates are exactly that, so every outcome with a Summit resolution or a Heap activation in it
(including a breakpoint continuation's Summit cast, which is why plans such as `{land=Cliffgate}` were flagged) gave
no verdict. 4,845 of the 5,000 whole UNMODELED lines in the gate's stderr hold a Summit cast, the other 155 a Heap
Gate activation.

The engine's action is not wrong: tapping by the provider's order is the standing mana-payment exemption, and the
oracle's job is to find out whether that exemption ever loses a line. That needs the oracle to drive the pick.

### Fix: the NON-MANA TAP CHOICE (`MTG_ORACLE_TAP_CHOICE`, default ON, oracle-only)

* `SpellEffects.h`: one function pointer, `g_oracle_tap_choice`, consulted at the two pick sites after the provider
  orders the eligible Gates. The oracle registers it at static init; off the oracle's own state copy it returns
  false without touching anything, so play is byte-identical by construction.
* `ManaOracle.cpp OracleTapChoice`: on the oracle's copy, when the eligible Gates fall in two or more RESOURCE classes
  (Resources' own key) and 0 < n < |eligible|, the pick is a scripted step served like a payment: the next script
  entry (`what == "tap-choice"`) names the n Gates; past the script's end the frontier is captured; an engine step
  (the rollback census) keeps the provider's order. A forced pick (all of them, or interchangeable copies) is
  recorded as the line's own tap.
* `TapChoiceOptions`: the frontier's options are every distinct multiset of n classes, the provider's own pick first
  (cap `MTG_ORACLE_TAP_CHOICE_CAP`, default 512; past it the frontier is cut and noted UNMODELED).
* Guards: a payment or a tap-ahead that meets a tap-choice entry (the script out of step with the apply) is a
  scripted-payment refusal, never a silent mis-tap; the viewer's line check names a tap choice in text only, never
  as a pre-tap token.
* Unit tests (`test/unit/test_mana_oracle.cpp`): a Summit tapping two of three Gates of different classes is covered
  with no unscripted outcome and no UNMODELED / ORACLE-GAP; a forced Summit tap is the line's own.

### Measured

(pending)

## 2. CAPPED-DECISION and the cost of a Maze's End decision

### Where the capped decisions' work goes

The three top CAPPED-DECISION repros, each auditing one turn (`MTG_MANA_ORACLE_ONLY_TURN`, a new diagnostic: play is
the same with the oracle on or off, so the earlier turns need not be audited), one thread, binary with the tap
choice:

| repro | applies | frontiers / pruned | priced s | oracle s | process wall | RSS | verdict |
|---|---|---|---|---|---|---|---|
| g30 T6 | 1,878,180 | 1,494,399 / 783,069 | 189 | 1,728 | 1,571 s | 2.6 GB | BUDGET-SPENT, WALL-BP-CAP, 92 of 106 plans unaudited |
| g4 T7 | 1,069,544 | 1,041,551 / 495,454 | 142 | 1,453 | 1,324 s | 2.2 GB | BUDGET-SPENT, WALL-BP-CAP, 128 of 132 unaudited |
| g14 T5 | 1,599,771 | 1,337,365 / 1,147,657 | 165 | 577 | 526 s | 1.5 GB | completes; 2 plans CAPPED by the board memory budget (768 MB) |

The priced work is 10-30% of the decision's real cost: the decisions are cut by the wall backstop, not the priced
budget. A `perf record` of g30 T6 (flat, 49 Hz) put **~75% of all samples in the frontier Pareto prune's comparison**:
`__memcmp` 34.5%, `std::_Rb_tree<std::string,...>` lookups 19.1%, `_Rb_tree_increment` 12.7%, `ParetoCovers` itself
8.4%; `Realize` 3.0%, `BuildSimKeyBase` 1.8%, everything else under 1% each. The online prune (Realize, `admit`)
compares each new frontier with every live frontier of the same key, and each comparison looked every resource name
of both tables up in the other (`Res` is a `std::map<std::string,int>` whose keys are long class strings with a
common prefix). It is unpriced, which is why the priced budget never saw it.

### Fix: an exact speedup of the prune (no change to what it keeps)

* `ParetoCovers` is one merge walk over the two sorted tables instead of a lookup per name (same answer: a name a table
  lacks reads 0, strictness counted only on names y holds).
* THE PARETO SCREEN (`ParetoSig`, per frontier, filled on first use): two necessary conditions of a cover, tested
  before the walk. If y covers x then sum(y) >= sum(x) (strictly for a strict cover), and every name x holds a
  positive amount of is positive in y, so x's positive-name bit mask (one hashed bit per name) is a subset of y's. A
  pair failing either cannot be a cover, so the prune keeps and drops exactly the frontiers it did before.
* Identity check: g21 gives the same applies / frontiers / pruned / options (10,395 / 2,002 / 504 / 10,058) with
  oracle ms 1,624 -> 516 (3.1x).

(Measurements on g167 and on the three capped repros: pending.)

## 3. The real classes

(pending)

## 4. Questions for the USER

(pending)
