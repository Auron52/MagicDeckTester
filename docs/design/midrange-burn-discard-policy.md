# Midrange Burn cleanup discard policy

This is the approved `MidrangeBurnProvider::CleanupDiscardCandidates` keep-set
rule, adopted on 2026-10-30 behind the default-on
`MTG_MIDRANGE_BUCKET_DISCARD` flag (`=0` restores the previous generic ranking).
The source of truth for card roles and costs is
`decks/Midrange Burn/Midrange Burn.cod` plus `src/cards/data/cards.json`.

Evidence: the pinned 400-game d3 pass produced **zero real cleanup discards**
and 1,627 rollout cleanup discards, all with fewer than four lands on the
battlefield. There are no searched labels at the real decision site. The
earlier paired worst-vs-default bound over 200 games per arm was 4.6800 vs
4.6900 average turns: changing the rollout ranking matters slightly, but the
maximum observed prize is small. There were no searched labels at the real
cleanup site, so adoption is judged by paired seeded outcomes and the
three regression tiers.
The final-policy repeat at seed 930000 likewise found **zero real cleanup
discards in 400 d3 games**. Its 1,359 rollout discards all occurred below four
battlefield lands, confirming that the rule mainly affects search evaluation.

Build the keep set quota-first, net of permanents already on the battlefield.
The damage target is **near lethal**: enough damage from visible hand and board
to bring the opponent to 3 life or less over the next two of our turns. Count
each burn spell once; count attacks and abilities only when the visible state
makes them available. Do not assume future draws, an unearned spectacle or
Incinerator discount, or a Fireblast alternative cost without two Mountains.
If the hand cannot reach that target, retain as much playable damage as the
hand limit allows rather than reserving optional lands.

1. **Critical mana.** Keep enough lands in hand to reach two total land
   sources across battlefield and kept hand. These first two sources have the
   highest land priority. If Fireblast is part of the chosen damage package,
   preserve two *Mountains* across battlefield and kept hand for its
   alternative cost; Fiery Islet cannot satisfy that requirement. Do not keep
   an extra Mountain solely for a Fireblast excluded from the package.
2. **Damage package.** Keep at least two live threats when available, then keep
   adding the most playable damage threats until the visible package reaches
   near lethal. Within this stage, prioritize one-drop creatures (Monastery
   Swiftspear, then a fueled Grim Lavamancer), followed by one-drop spells
   (Lightning Bolt, Chain Lightning, Rift Bolt via suspend, and Skewer the
   Critics when spectacle is live). A single Light Up the Stage joins this
   one-drop tier as an enabler when spectacle is visibly available and the
   hand still needs threats; its unknown cards do not count toward near
   lethal. Keep a less efficient spell ahead of these only when it completes
   an immediate lethal or otherwise necessary
   near-lethal line that the one-drop cannot. This stage outranks a third or
   fourth land. Other threats include live Fireblast, Magma Jet, discounted
   Chandra's Incinerator, Searing Blaze with an opposing creature, and Price
   of Progress with opposing nonbasic lands. Count a battlefield permanent's
   available damage before assigning hand slots. A third land can join this
   stage when it unlocks damage needed for the near-lethal package; a fourth
   can do so only when necessary to reach the target. The third outranks the
   fourth. Chandra, Torch of Defiance is a secondary threat, not a reason by
   herself to reserve four lands. Keep her when already castable or when the
   core damage package is secure and there is room to retain the lands that
   make her castable. An extra Chandra ranks below the first while one is
   already active.
3. **Enablers.** A live one-mana Light Up the Stage is handled with the one-drop
   spells above. If spectacle is not visible, retain it only after the damage
   package when the hand still needs threats. Magma Jet counts once as a damage
   threat above; its scry benefit promotes it within that bucket when the hand
   lacks lands or damage, without reserving a second enabler slot.
4. **Optional mana.** If the damage package and useful enablers leave room,
   keep a third total land source, then a fourth. The third has lower priority
   than needed damage; the fourth has lower priority than the third. Keeping a
   fourth is useful when a retained Chandra can then be cast, but neither she
   nor her printed cost makes it mandatory. Within these optional lands,
   prefer a first Fiery Islet as a redraw source, then Mountains; prefer a
   Mountain over an extra Islet when life is low. Lands above four total are
   overflow unless required for the selected Fireblast line.
5. **Inert cards.** In the passive opponent's zero-nonbasic buckets, shed
   Price of Progress first when it cannot contribute to the visible damage
   package. Shed Searing Blaze next while the opponent controls no creature.
   Cleanup happens after combat, so targeting our own creature cannot create
   the same-turn prowess payoff contemplated in the draft policy.

Among overflow cards, shed the least playable one first. Use current board
mana and discount conditions: Fireblast counts as `{0}` only with two
Mountains available; Incinerator's `{5}{R}` bill falls by noncombat damage
already dealt this turn or payable from visible spells; Skewer and Light Up
use `{R}` only after spectacle is live; Rift Bolt can suspend for `{R}`.
Within equal distance, retain this damage priority: Swiftspear, fueled Grim
Lavamancer, Lightning Bolt, Chain Lightning, Rift Bolt via suspend, live
spectacle Skewer, one live spectacle Light Up when threats are still needed,
live Fireblast, live Searing Blaze, live Price of Progress, discounted
Incinerator, Magma Jet, then Chandra and optional dig. A spell whose
visible damage completes immediate lethal moves ahead of this tie-break.
For a deterministic final tie-break, shed the later hand index.

The provider returns **one** discard index through
`CleanupDiscardRankingWithOrder`, preserving staged-card and required-piece
protection. Its separate full-order hook supplies multi-card cleanup, which
re-ranks after each shed because quotas and the visible damage package can move.

Validation on the approved rule: 2,230 unit tests and 349 scenarios passed.
The corrected final smoke had one same-score play change and no slower games;
the regression tier had one T6→T7 game that returned to T6 at 4× and 16×
search budget. Regression improved the 2HG mean 6.0133→5.9867 turns
(gi13 T7→T5 by avoiding an early Fireblast shed), with the other searched
single-player rows unchanged. The overnight entry set
improved by about 12 game-turns over 11,206 games: ten searched games faster,
two slower. One slower d3 game recovered its old win turn at 4× budget; the
other, gi310 at seed 4314, went T8→unwon by T8 and persisted at 16×.
In that game the new rollout led the search to spend Fireblast on turn 2,
leaving only three lands when Chandra was drawn on turn 7. This is a real
search-quality tradeoff recorded in the Linux ground-truth provenance, not a
discard legality or card-rule failure. The pooled outcome remains non-inferior.
