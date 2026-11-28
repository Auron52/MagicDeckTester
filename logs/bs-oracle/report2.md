# bs-oracle report 2: the leftover fold (mechanism C)

Branch `wip/bs-oracle`, commit **633af33b2** (on top of the pay-table memo 30efe9772). Scratch:
`/workspaces/MagicDeckTester/build/scratch/bs-oracle/` (S). Binaries: `S/mtg.memo` = 30efe9772 (before),
`S/mtg.lf` = 633af33b2 (after).

## 1. Where g531 T4 m1's cost went

The study531 outputs show the same thing on both arms (origin and memo):
`priced_ms=1331581 BUDGET-SPENT skipped=406`. Pay took 528 s (206 M payments) and simkey took 784 s (209 M keys).
The memo got no hits (0 of 312 lookups). The menu is Emrakul / Ulamog casts on a Fastbond board with two Lotus
Fields, two Cities of Brass and four Scorched Ruins. It has no tutors, so mechanism B is not involved.

A per-table census (a temporary diagnostic, removed again) at a 40-priced-s budget:

| game | tables | base verify payments | leftover-split payments | leftover payments that were NEW results | distinct (E, final pools) | fold-key conflicts |
|---|---:|---:|---:|---:|---:|---:|
| g531 T4 | 46 | 1.33 M | 23.0 M | 0.36 M (1.6%) | 0.33 M | 0 |
| g46 T7 | 11,008 | 1.58 M | 12.95 M | 0.57 M (4.4%) | 0.80 M | 0 |

So the cost is **mechanism C**. A tap set is verified once per colour assignment of its multi-unit any-colour faces
(Lotus Field / Vale, City of Brass). Each assignment's leftover split then re-pays the bill once per leftover, and
most of those leftovers repeat across assignments. Each repeat copied the board, re-made the taps, paid the bill and
keyed the result, and only then did `seen` drop it.

## 2. The cut: `MTG_ORACLE_LEFTOVER_FOLD` (default ON, `=0` off)

Code: `src/ai/ManaOracle.cpp`, `LeftoverFold` and `PayEnum::Verify`'s leftover split.

Within one MinimalPayments table, each leftover result is keyed by **E** and the pools it ends with:

- E is the post-tap board's TapFoldKey with the general pool emptied. It is taken from the first real leftover probe
  of a tap set.
- Every real leftover probe records (E, final pools) and maps it to its result key.
- A later leftover probe whose (E, stash) is already recorded is not made. Had it paid, it would only have recorded
  the same result again. Had it failed, it would have recorded nothing.

Why this leaves the table unchanged:

- The table returns the same options, in the same order, from the same representatives.
- A folded probe still counts toward `PayEnum::cap`, so a table that was cut is cut at the same place.

Self-checks:

- (a) A real probe whose key is already recorded under a different result is a conflict.
- (b) A real probe that does not end with exactly its predicted pools is a spill.
- Either one turns the fold off for the rest of that table.
- Only plain pool states take part: no restricted, haste, prepay, wild or snow units, and an empty
  `prepay_wild_true`.

Diagnostic mode: `MTG_ORACLE_LEFTOVER_FOLD_CHECK=1` makes every folded probe anyway and reports a
`[leftover-fold] MISMATCH`.

Scope: the fold is audit-only. It is gated by `LeftoverFoldScope` in Realize's table lambda, and the payer's
line-keep repair (ManaPayment.cpp `MinimalPayments`) never sees it. Play and its priced work are therefore untouched.

Unit test: "the leftover fold pays each colour face's shared leftovers once, with the same verdicts". It covers menu
verdicts on and off, plus the raw table compared option by option (stash, taps, colours).

## 3. Results

**Check mode** (g531 T4 and g46 T7 at 40 priced s): 29.2 M folded probes checked, with 0 mismatches, 0 conflicts and
0 spills.

**Units:** 4349 of 4349 pass (`S/unit_par2.log`).

**Blood Sun, the 20-game set** (`cmp_bs20_lf.txt`, memo_bs against lf_bs):

| | memo | fold |
|---|---:|---:|
| on-arm CPU | 2,920 s | 1,807 s (0.62x) |
| oracle ms | 2,567 s | 1,520 s |
| leftover probes folded | - | 70.3 M |

- Flags are equal on every class: DROP-COVERED 174, DOMINATED-LAND 297, CONTINUATION-DIFFERS 12, CAPPED 12,
  DOMINATED-LIFE 8, ORACLE-GAP 18, UNMODELED 119.
- Every oracle counter is equal (applies 11,165,596; options 13,530,553; frontiers 2,680,442).
- Play digests: 0 differ out of 20.
- This set had 0 CAPPED-DECISION games on both arms.
- A rerun later in the day under heavier box load (lf2_bs) measured 2,082 s. CPU on this box moves about ±15% with
  load. The folded probe count and the priced work are the deterministic numbers.

**Dragonstorm and Prevent Damage, 20 games each** (`cmp_other_lf.txt`, `priced_other_lf.txt`):

- Flags are equal (DOMINATED-LIFE 133, DOMINATED-LAND 120, PHANTOM 39), and digests are equal on all 40 games.
- The unpaired CPU read was +6.7%, which is box load. A paired run interleaved the arms (memo 27.9 / 33.7 s
  against fold 27.2 / 36.1 s) and did not separate them.
- Deterministic priced work, summed over every DECISION-COST:

  | deck | memo | fold |
  |---|---:|---:|
  | Prevent Damage | 20,231 ms | 20,142 ms (-0.4%) |
  | Dragonstorm | 4,425 ms | 4,442 ms (+0.4%; the E keys, only 82 K folds there) |

- I tried gating E to tap sets that have a multi-face class, and reverted it. It lost 7,241 legitimate folds, because
  a single-face tap set shares E with itself under the table's other set-asides. It saved only 2 priced ms.

**A 7-game CAPPED-DECISION sample** (g5, 51, 91, 170, 233, 300, 531; `lf_dc_decision_cost.txt`):

- **All 7 games still have CAPPED-DECISION on the fold arm.**
- The memo arm (memo_dc) had not finished at handoff, so the per-decision flag comparison is owed (handoff2.md). The
  gate's per-game classes (S/gate_cls.txt) come from 80cb93166cfb, a different engine commit, so they are not a valid
  before arm. Non-capped classes differ there by up to 5x.

**The shift.** The fold moves the binding limit:

- 9 of the 13 heavy decisions in the sample now stop on **WALL-BP-CAP**, the oracle audit's 1,300 thread-CPU s
  backstop, at only 274-883 priced s.
- Example, g531 T4 m1: on the fold arm it made 6.6 M applies and audited 46 plans, with 726 priced s against
  1,462 s of CPU. Before, it made 0.95 M applies and audited 7 plans in 1,331 priced s.
- The backstop's own comment names the unpriced work: "physical work in a muted cache recompute", i.e. breakpoint
  derivations repeated inside pledger-MUTED recomputes.
- One decision, g51 T4 m1, still spends the priced budget (1,300 s) without hitting the backstop. 574 s of that is
  simkey and 305 s is pay (153 M keys, 119 M payments), so payment tables are still heavy there.

## 4. Perf of the post-fold g531 decision

Pending at the time of writing. See handoff2.md (`S/p531lf.self.txt`, `S/p531lf.children.txt`).

## 5. What is next

1. The unpriced muted-recompute work behind WALL-BP-CAP. Price it, or cut it. Pricing it would only move the cap,
   so cutting it is what buys verdicts.
2. g51 T4 m1's payment tables: 119 M payments in one decision even with the fold. Check which part of the table
   that is (base probes, or leftovers in tables that are not plain).
3. Base probes of a second colour assignment of the same taps still each re-pay the bill. Folding them needs a
   success prediction, which is a stronger claim than this fold makes.
4. Mechanism B (chained tutor continuations) does not appear in this sample's heavy decisions.
