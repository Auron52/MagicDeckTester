# Dragonstorm: splice reveals at cast time

The fresh Dragonstorm mana-clean gate on games 52,000–56,999 reported 26
`APPLY-DROP-ORDER` flags. Seed `30054701`, turn four, exposed an invalid
oracle rescue: its apparent paying order cast one Desperate Ritual plainly,
then cast the second with one copy spliced. The first copy was already in the
graveyard, so the second had no other card in hand to reveal. The payment
script produced enough mana only by counting that impossible splice.

The executor and the rollout now use the same live-hand check before a
spliced cast pays mana. It counts other physical, non-staged copies of the
same card. Apex of Power's exiled cards are stored in the hand vector for
casting, but are not in hand for a splice reveal. The focused test covers
spliced-first then plain (legal), plain-first then spliced (illegal), and a
real/staged pair. The exact `30054701` replay changed from two order-drop
flags to zero mana-clean issues, with zero oracle gaps or caps.

On 5,000 fresh paired games, the cast-time guard changed 13 play digests and
two win turns, both one turn later: indices 162 (turn three to four) and 2441
(turn six to seven). The old traces show the faster lines splicing Desperate
Rituals that Apex had exiled. The first used multiple staged Rituals; the
second spliced after its real hand copy had already been cast. Neither old
win line was legal. The paired work-unit ratio was 1.0010. Inputs, per-game
wins, and traces are under `build/scratch/dragonstorm_splice_ab/`.

An attempted enumeration cut excluded staged copies from the splice option
count. It was dropped: a candidate's `hand_index` is not the physical copy
the executor necessarily casts. The executor prefers an expiring staged copy
of the same name, which can legally splice a different real copy remaining in
hand. The cut hid such a legal line in paired game 1497 (turn four to five),
while the cast-time guard alone left that game unchanged. The final source
retains the broad candidate menu and checks the chosen physical copy at cast
time.

The final Release build passes all 3,360 unit cases and 3,614,324
assertions; the focused seed still has zero issues on that build. Combined
Dragonstorm smoke and regression on the fresh PGO build passed 561 scenarios.
The per-game audit found zero slower searched games and one faster depth-zero
game. Ten searched games changed their play line at the same win turn; each
was reviewed before accepting the six changed Dragonstorm ground-truth rows.
The strict viewer replay recorded 329 ok, 534 repaired, one beaten, and zero
play drift, dead shuffles, board divergence, enum gaps, mulligan drift, or
contract failures across 864 references. Both suboptimal games were still
strictly beaten. `test/check_gt_logs.py` reports all 1,252 rows consistent.

The fresh PGO mana-clean block on indices 57,000–61,999 completed with
byte-identical oracle-on/off play. It found 63 issues: 61 `APPLY-DROP` and
two `APPLY-DROP-ORDER`, with zero enum gaps, oracle gaps, unmodeled, or caps.
The largest remaining group loses a Karrthus cast after Seething Song and
Apex of Power while black or green mana must be reserved through an earlier
generic payment. The report is
`logs/mana_clean/codex-dragonstorm-splice-20261116/report.md`; the next fresh
block is 62,000–66,999.
