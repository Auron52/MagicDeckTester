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

(pending)

## 3. The real classes

(pending)

## 4. Questions for the USER

(pending)
