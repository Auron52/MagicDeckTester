# Midrange Burn cleanup discard policy (proposal)

This is a proposed `MidrangeBurnProvider::CleanupDiscardCandidates` keep-set
rule. It is not adopted yet. The source of truth for card roles and costs is
`decks/Midrange Burn/Midrange Burn.cod` plus `src/cards/data/cards.json`.

Evidence: the pinned 400-game d3 pass produced **zero real cleanup discards**
and 1,627 rollout cleanup discards, all with fewer than four lands on the
battlefield. There are no searched labels at the real decision site. The
earlier paired worst-vs-default bound over 200 games per arm was 4.6800 vs
4.6900 average turns: changing the rollout ranking matters slightly, but the
maximum observed prize is small. This policy needs a paired A/B and a
non-inferiority result before adoption; labels cannot validate it.

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
   package. Shed Searing Blaze next while the opponent
   controls no creature; promote it above optional lands only if casting it
   on our own creature gives a same-turn prowess/other payoff that improves
   the visible near-lethal package. This promotion must be based on visible
   board state, not future draws.

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

The provider implementation should return **one** discard index through
`CleanupDiscardRankingWithOrder` so staged-card and required-piece protection
remain in force. After approval of this proposal, gate it with a default-on
`MTG_MIDRANGE_BUCKET_DISCARD` flag (`=0` restores the generic rule), then run
paired outcomes and all three regression tiers before adoption. No
implementation or ground-truth change is authorized by this proposal alone.
