# Goblins mana oracle: the goal fold of sac-for-mana "eat" work (2026-11-19)

Branch `wip/gob-oracle-fold`. Follows `goblins-sac-mana-oracle-2026-11-18.md` (MTG_ORACLE_SAC_FODDER), whose §6 owed
"a sound fold of dominated eat options". Analysis-only: play is untouched (the oracle never feeds play).

## 1. The problem

Gate `claude-queue-20261119b` (Goblins gi 130000-134999): 479 CAPPED-DECISION + 9 CAPPED, all from two games:
s30131075 gi131075 (T2 m2, 431 flags; the 600-s oracle wall backstop; plans also past the 400000-apply retry cap)
and s30132663 gi132663 (T3 m2, 48 + 8 CAPPED). Shape: every land tapped, ~16 Goblin bodies (two Piledrivers,
Muxus, Grandee, two Siege-Gang Commanders and their six tokens, Chieftain, Skirk Prospector), a hand of Lightning
Bolt / Goblin King / Goblin Matron / Goblin Chieftain, and a 446-plan menu. Every payment is made by eating bodies
through Skirk, and the fodder table offers every minimal option (§2b of the 11-18 doc), so each payment layer
branches over the body multisets.

## 2. What is NOT foldable exactly

- **Copy symmetry** (which of two identical tokens was eaten): already folded. The fodder table puts identical
  bodies in one class (FodderKey) and Build eats a class's lowest-numbered copy first, so two paths that eat the
  same multiset reach the same numbered board and the frontier prune merges them. Measured `sym_dup` below.
- **Dominance between bodies** ("eating a token is never worse than eating a Piledriver"): a value judgement. The
  end boards differ in their core (CoreKey), so they are incomparable in the oracle's model; ranking them would
  change verdicts (a USER-level model change, not a fold).

## 3. The goal fold (MTG_ORACLE_GOAL_FOLD, default ON)

**What it skips.** When the engine realised the whole plan, AuditPlan's verdict (I1 cover, I2 domination) reads only
oracle outcomes on the engine's line whose canonical `CoreKey` equals the engine end board's. The fold does not
expand a kept frontier from which no such outcome is reachable.

**The test** (`KeyedCards`, `KeyedShort`). Per card name: the active player's NONTOKEN cards in a zone `CoreKey`
keys by name -- permanents they control, hand, library, sideboard -- plus their spells on the stack and the spell
the frontier is paying for; key 0 the total; pay-sac sources' names left out (CoreKey drops those permanents). A
frontier is skipped when some name has FEWER such cards than on the engine's end board.

**Why it is exact.**
1. Equal CoreKeys imply equal counts: the key folds every battlefield permanent's name and controller, the hand,
   the library (ordered) and the sideboard by name.
2. The counts never rise along a realisation: a nontoken card leaves those zones only for one the key does not
   hold (a graveyard, exile), and nothing brings it back. That is the same assumption the frontier prune already
   rests on: two frontiers that differ only in graveyard / exile contents share a key, and ParetoKeep drops one --
   sound only if those contents cannot change what is reachable. New nontoken cards are never created.
3. So a frontier short of the engine's count on some name has no outcome on the engine's key, and every outcome
   the verdict reads is still produced, in the same order (the layer's other scripts are applied as before).

**Preconditions** (`GoalFoldAdmissible`; otherwise the plan is realised unfolded):
- `deck_gy_readers == 0` (no graveyard projection in the key; a reader would make "nothing comes back" false);
- `!deck_has_mana_untapper` (the only cards under which the engine's tap-ahead / untap-choice counters move -- an
  `unscripted` outcome below a skipped frontier would otherwise go unseen);
- per frontier (`FrontierRec::goal_cands_scripted`): every root permanent the unscripted check reads (untapped, a
  mana source, not the plan's own) is already tapped by the frontier's own script (`Hook::tapped_by_script` as it
  stood when the frontier was taken). Every outcome below replays that prefix, so those permanents are "tapped by
  script" in each of them and the check's root half cannot fire there;
- `!deck_has_tap_steal`, `!deck_has_transform`, and no card of the active player's (any zone, tokens aside) that
  renames a card or makes / enters as a copy (faces, daybound, adventure, face-down, token copies, Garth, steal):
  the counts are by NAME, so a rename or a same-name token copy could fake or raise one;
- the engine's end board has an empty stack.

**Side channels and the fallback.** Everything outside the cover/domination test that reads all outcomes:
- the unscripted-tap UNMODELED line: cannot fire, by the preconditions;
- a not-covered verdict (ORACLE-GAP's outcome count and "nearest" script, CONTINUATION-DIFFERS, the UNMODELED
  "cannot reach" line): if the folded realisation does not cover the engine's board, the plan is realised again in
  full and judged on that (`g_goal_fold_redo`), so those texts are the unfolded ones;
- the apply notes (`r.unmodeled`) of skipped frontiers: on a covered plan they feed only the UNMODELED census
  counter, not a flag line;
- CAPPED: a plan that was capped may now conclude (fewer applies) -- "no worse".

`MTG_ORACLE_GOAL_FOLD_CHECK=1` realises every folded plan in full as well and aborts unless the full realisation
also covers, holds the same on-key outcomes in the same order, and has no unscripted outcome.
`MTG_ORACLE_FOLD_STATS=1` (diagnostic) prints per plan the sizing: kept frontiers failing the test, the scripts
below them, rises of the counts parent -> child (`mono`, must be 0) and on-key outcomes with different counts
(`keyviol`, must be 0).

## 4. Measurements

Apparatus: one pooled `mtg --batch` per arm over 42 Goblins games (the 12 heavy seeds below + gi 130000-130029),
Release build of this branch, 32 threads, `MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_COST_TRACE=1 MTG_RSS_CAP_GB=40`, arms
`MTG_ORACLE_GOAL_FOLD=0/1`. Applies are the PLAN-COST + GATE-COST sums per game; the decision cap that fired is the
PRICED one (`BUDGET-SPENT priced_ms=600003`), so the CAPPED-DECISION counts are deterministic here. Raw output:
`build/scratch/claude_gob_fold/`; summaries: `logs/gob-oracle-fold/`.

### 4.1 First version (a root-level unscripted guard: no untapped root mana source the plan does not name)

| | fold off | fold v1 |
|---|---:|---:|
| oracle applies (all 42 games) | 35,241,987 | 20,141,759 |
| oracle ms (sum) | 1,856,899 | 1,085,915 |
| CAPPED | 14 | 0 |
| CAPPED-DECISION | 412 | 288 |
| other flag lines | 339 | 342 (the 339 identical + 3 APPLY-DROP on plans that were CAPPED-DECISION off) |
| play digests | | identical |

Per heavy seed (applies off -> v1): 132663 9.71M -> 1.05M (9.3x; its 14 CAPPED plans now conclude; game wall 510 s
-> 52 s), 130085 432K -> 46K (9.5x), 133281 2.89M -> 325K (8.9x), 133407 5.13M -> 2.05M (2.5x), 134078 742K -> 482K;
131075 6.75M -> 6.60M (its T2 m2 decision spends the 600-priced-s budget either way, but audits 215 of its 446
plans instead of 91); 131364 / 131479 / 132765 / 132895 / 133150 / 133979 unchanged -- the root guard refused them
(an untapped Mountain / Cavern at the root), which is what the shipped guard (4.2) fixes. The 30 ordinary seeds:
57,282 -> 57,276 applies, every flag line identical.

`MTG_ORACLE_GOAL_FOLD_CHECK=1` on the same pool: 503 folded plans realised in full as well, 0 mismatches. The
copy-symmetry ceiling (`sym_dup`, FOLD-STATS on the heavy seeds): 147,696 of 2,377,147 kept frontiers (6%), and not
an exact fold anyway (printed card numbers) -- not pursued.

### 4.2 Shipped version (the per-frontier unscripted guard)

| | fold off | shipped fold |
|---|---:|---:|
| oracle applies (all 42 games) | 35,241,987 | 17,587,961 (2.0x fewer) |
| oracle ms (sum) | 1,856,899 | 940,255 |
| CAPPED | 14 | 0 |
| CAPPED-DECISION | 412 | 288 |
| other flag lines | 339 | 342 (the 339 identical + the same 3 APPLY-DROP on plans CAPPED-DECISION off) |
| play digests | | identical |

Applies per heavy seed, off -> on (SLOW-GAME wall where printed):

| seed (gi) | off | on | x |
|---|---:|---:|---:|
| 131075 | 6,748,671 | 6,600,939 | 1.02 (decision budget-bound both ways; 91 -> 215 of 446 plans audited) |
| 132663 | 9,708,540 | 1,048,750 | 9.26 (510 s -> 56 s; 14 CAPPED -> 0) |
| 130085 | 432,465 | 45,637 | 9.48 |
| 131364 | 445,398 | 275,886 | 1.61 |
| 131479 | 257,338 | 256,975 | 1.00 |
| 132765 | 700,421 | 553,754 | 1.26 |
| 132895 | 610,019 | 484,791 | 1.26 |
| 133150 | 1,239,318 | 450,776 | 2.75 |
| 133281 | 2,886,466 | 324,814 | 8.89 |
| 133407 | 5,132,946 | 2,048,138 | 2.51 (218 s -> 77 s) |
| 133979 | 6,281,322 | 4,957,876 | 1.27 (261 s -> 196 s) |
| 134078 | 741,801 | 482,389 | 1.54 |

The 30 ordinary seeds: 57,282 -> 57,236 applies, every flag line identical.

Exactness checks:
- `MTG_ORACLE_GOAL_FOLD_CHECK=1` on the pool: 861 folded plans realised in full as well, 0 mismatches (same cover,
  same on-key outcomes in the same order, no unscripted outcome); its flag lines (it judges the full realisation)
  equal the off arm's exactly, CAPPED-DECISION aside (it pays for both realisations).
- FOLD-STATS on the 12 heavy seeds, fold off: over 2,224 engine-full plans, 2.37M kept frontiers and 17.7M
  outcomes, the true keyed counts never rose from a frontier to a child (`mono` = 0), no outcome on the engine's
  key had different counts or descended from a failing frontier (`keyviol` = 0), and below the failing frontiers
  (411,045 outcomes) there was no unscripted outcome and no apply note (`bad_uns` = `bad_unm` = 0).
- Name-blind (total only) test for comparison: 136,504 scripts below failing frontiers vs 858,589 by name -- the
  per-name test is what buys the fold, and it is what needs the rename/copy guard.
- Full unit suite 3713/3713 (a new case: two Siege-Gang shots paid by eating; the fold engages, no redo, the verdict
  is clean).

## 5. Is Goblins gate-tractable?

TBD.
