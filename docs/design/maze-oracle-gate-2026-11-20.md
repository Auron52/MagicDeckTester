# Maze's End: making the mana-clean gate's verdict meaningful (2026-11-20)

Branch `wip/maze-gate`, on the shared branch at `c86a86333` (src tree `31070f52`). The input is gate
`claude-queue-20261119i` (5,000 games, src `8deb2c93`, before the oracle decision budget went 600 -> 1300 priced s
and before the backstop moved to thread CPU): maze_s_end 287,482 issues, of which UNMODELED 139,380,
CAPPED-DECISION 131,360, CAPPED 13,729; the real classes APPLY-DROP 1,909, ENUM-GAP 617, ORACLE-GAP 410,
DOMINATED-ONESHOT 48, APPLY-DROP-ORDER 29. Report: `logs/mana_clean/claude-queue-20261119i/report.md` (primary
checkout). Everything here is ANALYZER-side (the mana oracle, `src/ai/ManaOracle.cpp`): play is not changed.

(Status: UNMODELED fixed and measured; the CAPPED-DECISION hot spot fixed (exact); the small classes and the residual capped cost are open, section 3 / 2b.)

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

Repro games (`MTG_MANA_ORACLE=2`, one thread, seed 30000000 + gi, the deck's own profile), baseline binary = the same
src without the change:

| gi | applies base -> new | UNMODELED base -> new | ORACLE-GAP / MISS | oracle ms base -> new | wall, RSS (new) |
|---|---|---|---|---|---|
| 9 | 370 -> 370 | 0 -> 0 | 0 / 0 | 58 | 0.7 s, 42 MB |
| 21 | 10,390 -> 10,395 | 10 -> **0** | 0 / 0 | 1,210 -> 1,624 | 3.7 s, 229 MB |
| 27 | 2,926 -> 2,926 | 0 -> 0 | 0 / 0 | 1,091 | 3.6 s, 166 MB |
| 167 | 578,290 -> 621,924 (+7.5%) | 170 -> **0** | 0 / 0 | 87,911 -> 101,800 (+16%) | 96.8 s, 1.37 GB |

Every outcome that used to carry the unscripted-tap note now gets a verdict, and none of them is a gap: on these games
the provider's Gate order never lost a line. The cost is the tap-choice frontiers' own expansions (+7.5% applies on
g167). Game output is byte-identical to the baseline on all four.

* Unit tests: the two new cases pass, and both FAIL with `MTG_ORACLE_TAP_CHOICE=0` (the negative control); the whole
  suite passes, 3,780 / 3,780.
* Smoke (4 threads, lane): 235 passed, 0 failed, per-game audit 0 configs changed, 0 play changes -- oracle-off play
  is byte-identical, as the construction says. No GT to accept.

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

Measured (one thread, lane; "before" = the tap-choice binary without the screen):

| repro | applies before -> after | plans audited before -> after | oracle s before -> after | priced s after | verdict after |
|---|---|---|---|---|---|
| g21 (whole game) | 10,395 -> 10,395 (identical) | all | 1.6 -> 0.5 | -- | same |
| g167 (whole game) | 621,924 -> 621,924 (identical; frontiers 299,749, pruned 188,633) | all | 101.8 -> 25.4 (4.0x) | -- | same; wall 96.8 -> 26.2 s |
| g14 T5 | 1,599,771 -> 1,599,771 (identical) | 32 -> 32 | 577 -> 230 (2.5x) | 165 | same (MISS 3, CAPPED 2 by board memory) |
| g30 T6 | 1,878,180 -> 7,937,265 | 14 -> 61 of 106 | wall cap both (~1,500) | 793 | still WALL-BP-CAP, 45 unaudited (was 92) |
| g4 T7 | 1,069,544 -> 5,964,226 | 4 -> 27 of 132 | wall cap both (~1,500) | 851 | still WALL-BP-CAP, 105 unaudited (was 128) |

Where nothing is capped the counters are identical, which is the identity check. Where the backstop binds, the same
wall now does 4-6x the applies, and priced work went from 10-15% of the wall to ~55%: the budget now sees most of
what the decision costs. Unit suite with the screen: 3,780 / 3,780. Smoke is not re-run for this commit: the prune
runs only inside the oracle (smoke's games run it off), and the tap-choice smoke above already showed oracle-off play
byte-identical.

The two capped decisions remain capped. In priced terms their work is now `simkey` (117-125M calls, 440-467 priced s)
and `pay` (95-106M calls, 243-271 priced s): about 15 BuildSimKey and 13 payment calls per apply. A second perf profile
of g30 T6 on the screened binary is in section 2b.

## 3. The real classes

Not yet re-classified across the gate: that needs a re-run of the gate (5,000 games under the box lock), which this
branch has not done. One repro checked on the screened binary:

* **ORACLE-GAP gi115 T4** (`MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 MTG_MANA_ORACLE_ONLY_TURN=4`, seed 30000115): still
  3 ORACLE-GAP (plans `{land=Maze's End}` and `{land=Baldur's Gate}` x2), no UNMODELED; 82 / 54 oracle outcomes, none
  covers the engine's. It is in the gate report, so it predates the tap choice. The engine's end board does NOT hold
  the plan's land: for `{land=Maze's End}` the engine ends with Gond Gate#31 and Baldur's Gate#7 played (new, tapped),
  Exploration#20 cast, Sylvan Scrying cast (Citadel Gate fetched), Maze's End still in hand, and Gond Gate#28 +
  Maze's End#43 + Cliffgate tapped. The oracle's nearest outcome leaves Gond Gate#28 and Maze's End#43 up and taps
  Izzet Guildgate + Baldur's Gate. So the engine's continuation (Exploration's extra land drop, after the Sylvan Scrying
  draw) re-decided the land drops on a board the oracle's realisations do not reach. Next: trace that plan's engine
  apply (the continuation actions and their payments) against the oracle's realisation of the same continuation.

## 2b. What the capped decisions cost after the screen

(pending: perf of g30 T6 on the screened binary)

## 4. Questions for the USER

No new decision. Everything here is analyzer-side and exact (the tap choice adds verdicts, the screen changes nothing
the prune keeps). One existing item now has an instrument: the provider's Gate order for Summit / Heap Gate taps
(`MazesEndProvider::EtbTapSubtypeOrder`, ledger decisions 2/10/30, USER sign-off pending there). The oracle can now
tell whether that order ever loses a line. On the repro games (g9, g21, g27, g167) it never did (0 ORACLE-GAP,
0 MISS). A gate re-run would answer it deck-wide, and any APPLY-DROP it surfaces on a Summit / Heap outcome is
evidence for that ledger decision.

## 5. Remaining, in order

1. Re-run the Maze's End mana-clean gate on this branch (box lock) to re-count every class, with UNMODELED expected
   ~0 and CAPPED-DECISION lower.
2. The residual capped cost (2b): profile, then a sound collapse or another exact speedup.
3. ORACLE-GAP gi115 T4 (section 3): trace the continuation's land re-decision.
