# Goblin Lackey decline prune (MTG_LACKEY_DECLINE_PRUNE)

Status: implemented, default ON, unit-tested. The paired A/B of the FINAL version is OWED (see "Owed").

## Problem

`MTG_LACKEY_DECLINE` (87bf2ed4c) made Goblin Lackey's "you MAY put" a searched choice: every damaging-Lackey
ordinal also carries a decline. It cost Goblins ~1.36x search units (2HG 1.37x) for 39 real declines in 11,001
games. The USER's rules: collapse wasted search, but only by SOUND dominance; nothing may become unfindable at
unlimited budget; a restriction lives in a provider.

## The proof

**Lemma (DecisionProvider.h, `CombatCheatPutDominatesHold`).** Let A = card X on the battlefield from this
combat-damage step, B = X in hand, all else equal. For any line of B, A plays the same line, skipping what B does
with X in hand (cast, Vial put, channel, a later Lackey put, a cleanup discard) and keeping the mana. A's line is
at least as good iff nothing about X is worse for entering earlier or for leaving the hand. That holds for a
"plain" card: a body (it can attack exactly when a main-2 cast could; a later cast is sick); +N/+N lords, haste
grants, Goblin cost reduction (only add); ETB tokens and ETB damage (any target is the face in the goldfish;
earlier damage is never worse); a channel no stronger than the ETB; optional sac / tap abilities; attack pumps; a
death watcher that pings; the Lackey's own "may". It fails for anything that reads the library or the hand or starts
a clock: Muxus's reveal and Goblin Matron's tutor + shuffle (a later ETB reads a different library), Rundvelt
Hordemaster's forced impulse exile on Goblin deaths, echo (Mogg War Marshal, Stingscourger), X costs, and a
LEGENDARY card while another copy of its name exists in hand / library / battlefield / graveyard (CR 704.5j).

`cheat_put_plain` is computed at load from a WHITELIST of raw cards.json keys (CardDatabase.cpp
`ClassifyCombatPut`), so an unknown key, template, keyword or supertype fails toward "not plain".

**Deck gate (`CombatCheatDeclinePruneSound`).** No OTHER card may make X in hand worth more than X on the
battlefield (a cast trigger such as prowess, a hand-size or discard reader, an enter watcher). True iff every card
the controller owns (hand, library, graveyard, non-token permanents) is `cheat_hold_inert` (the plain whitelist plus
the listed non-plain-but-harmless keys: echo, tutor, reveal-put, impulse exile, Vial charge, burn, mana lands). The
goldfish opponent never interacts.

**Rule A (TurnSolver.cpp "THE DECLINE PRUNE").** If no NON-plain card can be a candidate at any Lackey trigger this
combat (none left in hand at combat damage after the plan's own casts, and nothing can join: no tutor or reveal in
the plan, no feeder a breakpoint continuation could cast, no feeder put by an earlier trigger, unless the library
holds no non-plain candidate), every decline is dominated: put the front instead. Whatever the declining line later
puts, the putting line can still put (a card's rank only falls when another card leaves the hand, so it stays inside
the searched width); every extra card it puts, including at a trigger the ordinals did not count (which takes the
front), is plain, so the lemma covers it. Induct on the first dominated decline (it moves strictly later each step).

**Rule B.** If the ordinals are EXACT (no breakpoint, no feeder action, no ordinal past the product cap), a plain
card is guaranteed in hand at trigger k (plain in hand, minus plain cards the plan takes out, minus earlier put
digits, counting only plain cards no non-plain card could drop through the provider's dominance prune), and fewer
non-plain cards than searched ranks can be there, then a plain card is inside the provider's top `width`: put it at
k and decline wherever the declining line later put it. Exactness matters because an unpinned trigger takes the
front, which could otherwise be an extra non-plain put.

Everywhere else the decline stays.

## Measured

First version (Rule A/B without the refinements below), held-out paired A/B, ONE pooled batch, shipping
settings (budget 60, the profile's play depth), lever ON vs OFF in the same binary:

| deck | games | seed | faster | slower | net | units ON/OFF | ms ON/OFF |
|---|---|---|---|---|---|---|---|
| Goblins | 8000 | 3310001 | 0 | 2 (gi3241, gi3590: T3 -> T4) | +2 | 0.890 | 0.878 |
| Goblins 2HG | 3000 | 3320001 | 0 | 0 | 0 | 0.875 | 0.861 |

Soundness probe, d5 b0 (unbudgeted), same batch: Goblins 200 games (s3330001), 2HG 100 games (s3340001):
**0 games changed** (units 0.889 / 0.885).

The two slower games have NOT yet been checked at d8 b0.

Census of that version (`MTG_LACKEY_PRUNE_STATS=1`, 1,000 Goblins games s3310001, decline digits judged):
pruned A 30,323, B 29,656; kept: deck gate 8,641 (a dead Goblin TOKEN in the graveyard vector -- fixed),
candidate set can grow 224,309 (breakpoint 99,209, a Matron / Muxus cast 119,918, a feeder put earlier 5,182),
no plain card 15,972, non-plain cards fill the width 23,309.

Refinements since (the committed version): tokens in the graveyard pass the gate; a non-plain card the plan itself
casts is not counted at combat; a breakpoint makes the set grow only when a continuation could cast a feeder (Matron
/ Muxus in hand, Rundvelt in hand / play / cast by the plan); impulse exile alone (staged cards are never
candidates) no longer blocks a non-breakpoint plan. Census of the committed version (same 1,000 games): pruned A
61,429, B 28,380; kept: gate 0, grows 192,277 (breakpoint continuation 65,389, Matron / Muxus cast 121,651, feeder
put 5,237), inexact 4,701, no plain 15,034, width 16,033.

Most of what remains is a plan that casts Goblin Matron or Muxus before combat (the tutor / reveal can bring a
non-plain candidate the plan did not count) and breakpoint plans beside a feeder. Those keep the decline on purpose.

## Owed

1. The paired A/B of the COMMITTED version (same manifest: Goblins 8000 s3310001, 2HG 3000 s3320001, ON vs OFF,
   one pooled batch; plus the d5 b0 soundness rows). Scripts: `build/scratch/lackeydecl/mkab.py`, `report.py`,
   `mkslow.py` (the slow-game d8 b0 manifest).
2. Every slower game at `--depth 8 --budget-ms 0` (seed = base + gi, game_index = gi, ignore_play_profile).
   First version: Goblins gi3241 and gi3590.
3. Rebase onto 53f00da73 if not done in this commit's branch.

## Possible follow-ups (each must stay sound)

* Rank-aware Rule B: use the provider's ranking of the hand left at combat instead of "fewer non-plain cards than
  ranks" (kept_width ~16k judgments per 1,000 games). Needs the ranking to be independent of state the plan changes
  (the `MTG_LACKEY_RANK=uncast` arm reads mana).
* A Matron-cast plan: the tutor adds exactly one card, but its breakpoint continuation can still cast the guaranteed
  plain card; bounding that needs the continuation's mana, so it is not attempted.
