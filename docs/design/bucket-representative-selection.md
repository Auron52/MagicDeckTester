# The bucket representative is arbitrary — and changing it silently invalidates a journal

**Status: OPEN (a hazard note plus one unproven hypothesis). Raised 2026-10-03 while diagnosing why
the Fungus candidate-b force-merge from K=22 to K=17 under-delivered.**

## What the code does

`src/analyzer/ExhaustiveKeep.cpp`:

```cpp
std::vector<Card> rep(K);                         // a representative Card per bucket
...
for (const Card& c : deck.mainboard)
{
    auto it = bucket_of.find(c.m_name.str());
    if (it == bucket_of.end()) { continue; }
    count[it->second]++;
    if (rep[it->second].m_name.str().empty()) { rep[it->second] = c; }   // FIRST WINS
}
```

Every rollout's hand is then built from those representatives, one per copy:

```cpp
std::vector<Card> HandCards(const std::vector<Card>& rep, const std::vector<int>& h)
{ ... for each bucket b, push h[b] copies of rep[b] ... }
```

So **`rep[b]` is whichever member of bucket `b` appears first in decklist order** — not the cheapest
to simulate, not the most representative, not even `members.front()` of the equivalence class. It is
an artifact of file ordering.

For a bucket discovered as objective-equivalent that is defensible: any member is as good as any
other, by the definition of the class. **For a FORCE-MERGED bucket it is not** — a force merge
(`MTG_EQUIV_FORCE_MERGE` / `<deck>.buckets.json`) is a deliberate approximation the user authorises,
so its members are explicitly NOT proven interchangeable, and the file order then picks which one
stands in for the group.

Candidate-b's merged groups and their reps (first in `Fungus.cod` order):

| bucket | rep |
|---|---|
| `Vitaspore Thallid`, `Deathspore Thallid` | Vitaspore Thallid |
| `Peat Bog`, `Hickory Woodlot` | Peat Bog |
| `Forest`, `Blooming Marsh` | Forest |
| `Brightcap Badger`, `Shroofus Sproutsire`, `Psychotrope Thallid` | Brightcap Badger |

## THE HAZARD — this is the part that matters

**Changing a representative would silently invalidate a banked journal.** The resume gate is a
short-circuit chain over `bucket_fp && deck_fp && seed_base && K && max_mull && equiv_seed && R &&
PlayIdentityAllows(...)`, and:

* **`bucket_fp` is computed from the CLASSES (their members), not from the chosen reps.** Swap which
  member is the rep and the classes are identical, so the fingerprint does not move.
* **`play_digest` is a 64-game goldfish battery** (`RolloutConfigDigest`), which does not involve
  bucketing at all. It does not move either.

So a journal holding cell-sides computed with `rep = Peat Bog` would be **accepted and resumed** by a
run using `rep = Hickory Woodlot`, and the resulting raw would silently mix rollouts of two different
hands under one cell index. Nothing in the gate, the artifact check or validation would notice.

**If anyone ever changes rep selection — including by REORDERING a decklist, which is the easy way to
do it by accident — the bucket fingerprint must be extended to cover the chosen reps, and every
existing journal for a force-merged deck must be treated as dead.** Note the second-order trap:
`decklist-stamp-line-endings` already records a case where a decklist rewrite moved more than
intended. A tidy-up that reorders `Fungus.cod` is enough to repoint `rep[]`.

## The unproven hypothesis (do not quote this as a finding)

The force merge cut cell count 4.43x (1,522,096 -> 343,538 cell-sides) but the measured rollout rate
roughly HALVED: the K=22 run averaged ~98 rollouts/s through its sub-refine phase against ~48/s for
K=17, so ~4.4x less work bought only ~2x of wall.

One candidate explanation is this file: merging promotes ONE member to stand for the whole group, so
if the rep is the more expensive card to simulate, every cell containing that bucket now pays the
expensive card where previously only the cells holding that specific card did. All four merged
groups above contain a token/spore engine piece.

**It is NOT established.** A competing explanation fits the same observation: merging concentrates
Doubling-Season-bearing hands into fewer, coarser cells, so the surviving cells are individually
wider-boarded and slower for reasons that have nothing to do with rep choice. Both predict "fewer
cells, slower each".

**How to settle it cheaply:** `MTG_KEEP_REPLAY` one merged-bucket cell twice, once per candidate rep,
on the same `r`/`pd`, and compare elapsed. That isolates rep cost from cell coarseness because the
cell index and draw are held fixed. Do it in a scratch directory — see the hazard above, and do not
let a rep experiment anywhere near a real journal.

## If the hypothesis holds, the fix is NOT simply "pick the cheapest rep"

Cheapest-to-simulate is the wrong objective on its own: the rep determines what the keep table's
rollouts actually PLAY, so choosing it for speed biases the table toward the behaviour of whichever
member is cheapest. For a force-merged group that is a modelling decision, not a performance one, and
it belongs with the user alongside the merge itself. The user has already flagged the
Badger/Shroofus/Psychotrope group as *"the LOOSEST group in the file ... the FIRST group revisited if
these cards survive into the final list"* (`Fungus.buckets.json`).

Related: `fungus-second-main-and-devour.md`, `mulligan-rollout-performance-floor.md`,
`.claude/skills/mulligan-profile.md` (the merge protocol and parity fingerprints),
`keepgen-no-off-switches.md`.
