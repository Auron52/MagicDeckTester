# Mana affordability arc -- handoff (2026-08-18)

Self-contained state of the subset-affordability / same-turn-mana work, for whoever picks it up.
Everything referenced here is in git; no private notes are needed. The deep detail lives in
`colour-blind-subset-affordability.md` -- this file is the map and the current state.

## Where the tree stands

**Clean and green.** Smoke 36/36 with "no searched-depth slowdowns or play changes"; regression
green; the reference gate at 0 play-drift. Everything below is either ADOPTED (default on, ground
truth rebaselined) or a DEFAULT-OFF lever that is byte-identical off. No work is half-applied.

Latest relevant commits: `e4690c3` (colour-exact adopted), `811d165` (Karoo bundle fix),
`12614a7` (sequenced-walk discount fix + `MTG_SEQ_PROBE`), `0a32efe` (mode 3 + corrected mechanism),
`55fada8` (leaf reducer credit re-tested and rejected).

## THE LAW this arc established -- read this before proposing anything here

The same question ("may this model be optimistic?") has **opposite** correct answers at the two
sites, and both directions are now measured, not assumed:

| site | is there a rollout downstream? | correct posture | measured cost of the wrong posture |
|---|---|---|---|
| `EnumeratePlans` -- **BRANCH** | YES: every plan is rollout-scored, a bad one is discarded | **OPTIMISTIC** | tightening it (`MTG_RITUAL_SEQ_CREDIT=2`) costs dragonstorm **6/8 held-out keys, 0 green** |
| `Solve::consider` -- **SCORE / d0** | NO: the greedy commits to what it picks | **HONEST** | crediting it (`MTG_LEAF_REDUCER_CREDIT=1`) turns **11 d0 wins into losses** (d0 5.3990 -> 5.6140) |

So **"as correct as possible" is not one global setting.** The right model depends on whether
anything downstream can reject a wrong answer. Optimism is free where a rollout validates it and
fatal where nothing does; honesty is right where you commit and harmful where it narrows what the
search may consider. The shipped configuration -- `MTG_RITUAL_SEQ_CREDIT=1`, leaf reducer credit off
-- **is exactly this split**, and is correct rather than a compromise.

A prune's soundness bar follows from the same law: **"unpayable as stated" is NOT "unreachable."** A
plan is a PROPOSAL the executor realises with trimming, so a subset that cannot be paid as written is
still a branch whose realisation may be a good line. A prune keyed on literal payability is therefore
too tight in the only sense that matters, *even when every one of its rejections is individually
correct*.

## Instruments -- use these instead of reasoning about it

* **`MTG_SEQ_PROBE=1`** -- dumps every subset the sequenced tightening turns from accepted to
  rejected (the only place that model can remove a line), with pool, untapped sources, and each
  accelerant's cost/float. `=2` also reports the agreed ones, which is what distinguishes a clean
  sweep from a probe that never fired.
* **`MTG_COLOR_EXACT_PROBE=1|2`** -- the same instrument for the colour-exact gate.
* **`MTG_AFFORD_AUDIT=1|2`** -- executor-side drops; `=2` gives a per-drop trace with the board.
* **`MTG_RITUAL_SEQ_CREDIT=0|1|2|3`** -- 0 legacy, 1 shipped (honest leaf), 2 also honest at the
  enumerator (REJECTED), 3 diagnostic: gate only, no survivor-pool shrink.
* **`MTG_LEGACY_KAROO`**, **`MTG_COLOR_EXACT=0`** -- off-switches for the two adopted fixes, for
  per-game attribution.

**The reference gate is the sharpest change-detector in the suite for Dragonstorm** and it runs in
`regression` mode only (`VPC_ALWAYS=1` forces it elsewhere). Read the CLASSIFICATION line, not the
prose: `enum-gap` means "a recorded plan is gone with an IDENTICAL hand" (an enumeration regression);
`play-drift` only means the line replays to a different outcome. Confusing the two cost me a wrong
diagnosis that survived two commits.

## Settled -- do NOT re-litigate without a stated engine change

1. **`MTG_RITUAL_SEQ_CREDIT=2` -- rejected twice** (2026-08 and 2026-08-18). Its AGGREGATE has been
   better than mode 1 in **all three** measurements while being the wrong configuration each time.
   Aggregate is not the bar here. What would justify it is evidence the sequenced model is exact
   *with respect to what the engine can ultimately play* -- which, per the law above, it cannot be,
   because trimming makes literal payability the wrong predicate.
2. **Leaf reducer credit -- rejected twice** (the note at `Solve::consider`'s `any_affinity` flag,
   and again 2026-08-18 with numbers).
3. **The pattern.** Three levers were proposed in one day; all three already had a recorded
   rejection in the surrounding comments; all three rejections were still valid. **Grep the
   surrounding comments for a recorded rejection before proposing a lever**, and if you re-test one,
   state the specific engine change that invalidates the old measurement. All three re-tests were
   defensible on that ground even though all three failed.

## Genuinely open

* **Whole-turn mana ALLOCATION** (the `hinata gi1197` class). The remaining hinata/th/slivers drops
  are whole-turn allocation failures, not enumeration phantoms: the turn's casts are individually
  payable but the tap assignment across the whole turn fails. This is the biggest real item.
* **Stranded accelerants at the enumerator.** Real, but per the law above the fix is NOT to prune.
  If it is worth fixing, it has to be at a site where a wrong answer is still filtered downstream.
* **Storage-land yield** -- an uncharged Dwarven Hold appears not to be credited/charged correctly.
* **`combined UNPAYABLE` declines on slivers/fivecolour** -- unexplained, never chased.
* **Colour-exact coverage is PARTIAL**: hinata gets nothing from it (Cascade Bluffs / Izzet Signet
  stand the gate down), fivecolour only partially (`domain_mana`). `SubsetPayableWithFilters` as the
  exact test on filter boards is the obvious next step.
* **One unexplained game** from the Karoo per-game audit: hinata gi1197 (win -> loss), believed
  collateral of correct pruning plus the open allocation defect above.

## Method notes worth keeping

* **Earlier tier outputs are the cheapest control you have.** Diffing the `Viewer protocol:` line
  across the session's earlier `/tmp/reg_*.txt` runs established the reference gate's prior state
  for free and settled an adoption question without re-running anything.
* **`explain_game.py` diffs the CURRENT binary**, so flip a default BEFORE auditing an arm, or the
  diff is meaningless.
* **A probe reporting "FALSE REJECT" can mean the ORACLE is wrong.** Read the board dump before
  concluding the new check over-prunes -- that is how the Karoo rules bug was found.
* **Deck partition is not per-game attribution** when one commit bundles two changes that hit the
  same decks.
* Never `--accept` while the suite is red; never run two suite runs of the same MODE concurrently.
