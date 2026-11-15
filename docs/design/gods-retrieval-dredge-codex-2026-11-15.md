# Gods: dredge toward Mystic Retrieval

## Decision

The old Gods rule 4 took a natural draw after three Gods reached the graveyard
if Replenish was absent from hand. The user clarified that the deck should
dredge toward Mystic Retrieval, use it to recover Replenish, and cast Life from
the Loam to recover lands when it can. The natural draw for zero or one land
with no land in hand remains; it addresses an immediate mana shortage.

`MTG_GODS_RETRIEVAL_DREDGE` (default on, `=0` control) removes only the
three-Gods/no-Replenish natural draw in rule 4. The Gods provider still returns
one choice at each dredge decision. The exact physical Loam targets are chosen
when Loam is cast from the live graveyard, as before.

## Focused evidence

The Release unit suite passes 3,304 cases and 3,612,555 assertions. The
focused decision test passes with the lever both on and off. On seed 30005620,
turn eight, the off arm reports one ENUM-GAP. The on arm offers the original
`Tendo Ice Bridge -> Careful Study -> Life from the Loam` plan. Study dredges
Grave-Troll, Loam returns City of Brass, Gemstone Mine, and Reflecting Pool,
and the oracle reports zero issues. This confirms that the land-first order is
sufficient for this repro; the post-Study land-position axis is unnecessary
here. Full logs are `gods-retrieval-off-repro.log` and
`gods-retrieval-on-repro.log` in this worktree.

## Paired measurement

The fresh-seed Release batch was stopped early after 650 completed paired
games, because the effect was large and consistent across all 13 completed
chunks. With unwon scored as turn nine, the old rule averaged 5.3862 turns;
the user's strict dredge rule averaged 5.5692 turns (+119 turn-games), with
112 slower and 26 faster games. All 13 chunks had a positive delta. The
remaining 4,350 pairs would have measured the magnitude more precisely but
were unnecessary to identify the tradeoff. Data is under
`build/scratch/gods_retrieval_ab/` in this worktree.

Seed 7,900,011 illustrates the coupled result. With the old rule, the turn-three
Careful Study draws Replenish and the deck wins turn four. With strict
dredging, Study mills Replenish; Mystic Retrieval is not milled until turn
five, is flashed back on turn six to return Replenish, and the deck wins turn
seven. This is not a failure to select Loam targets or a lost plan at the
Study breakpoint. The coupled search knows the unseen library, so its apparent
advantage from drawing Replenish is not decisive for a blind-play policy. The
user asked for the non-clairvoyant result before choosing a rule. A fresh
mana-clean gate is still needed if the rule is adopted.

The Gods-only combined smoke/regression batch finished all eight cells. Both
depth-zero cells are byte-identical to ground truth. All six searched cells
have a worse total win-turn score: smoke d3 +4/50, smoke d5 +11/50, smoke
2HG +6/25; regression d3 +15/50, regression d5 +29/50, regression 2HG
+29/50. The per-game audit was stopped because this branch is awaiting the
policy choice; the completed `.wins` files remain in `test/logs/` in this
worktree. No ground truth was accepted.

## Non-clairvoyant check requested by the user

The coupled measurement above lets the search inspect the real unseen library.
The fair comparison pairs `MTG_NC_SEARCH=1` (reshuffle-averaged plan choice)
with `MTG_NC_BLIND_BOTTOM=1` (reshuffle-averaged mulligan bottoming). Gods has
no exhaustive blind keep/bottom sidecar, so omitting the latter would leak the
real draw order before play. Both policy arms used the same K=4, depth=1,
blind-bottom K=4 setting, 32 workers, Release, the same seeds and game indices,
and an eight-turn horizon. The resumable batch and per-game logs are under
`build/scratch/gods_retrieval_nc_full1000/`.

On 1,000 paired seeds (7,900,001 onward), the old natural-draw rule averaged
7.017 and strict dredging 6.912 loss-penalized turns: **strict is 0.105 turns
faster** (199 faster, 138 slower, 663 equal), with 785 vs 734 wins by turn eight.
The paired standard error is 0.0317 turns and the approximate 95% interval for
the strict-minus-old mean is [-0.167, -0.043]. Strict also used 2,683,279 vs
2,927,627 work units (-8.3%) and 4,001,941 vs 4,635,475 summed thread-ms
(-13.7%). On the exact 650 completed seeds from the coupled comparison,
strict-minus-old changes from +119 coupled turn-games to -91 blind turn-games.
Of the 112 coupled slower games, 91 are equal or faster under blind play;
21 remain slower and need state inspection. The 25-game blind pilot also favored
strict by five turn-games, with all five coupled slower games recovering, but
was too small to decide alone.

This is evidence that much of the coupled loss is clairvoyant planning, and
that the strict rule has a real blind-play advantage at K=4/depth=1. It does
not establish that every state should dredge. Eight residual blind losses
(indices 59, 189, 251, 283, 321, 388, 402, 632) were traced. In every one,
the policy arms first diverge at a main-phase plan choice before reaching the
three-Gods draw rule. These losses demonstrate that different blind search
plans can have different outcomes; they do not identify a visible draw state
for a conditional exception. Traces and summary are under
`build/scratch/gods_retrieval_nc_residual_traces/`. A K=8/depth=2 comparison
was stopped after partial completion due to extreme game duration; its partial
files remain resumable under `build/scratch/gods_retrieval_nc_k8d2_200/`.

The first 1,000-game comparison used the original rule-4 Loam ordering. A
source review found a separate contradiction of the user's low-land
instruction: with zero or one land, no land in hand, and Loam plus a land in
the graveyard, the provider chose Loam before reaching the natural-draw check.
The correction checks the zero/one-land draw first and leaves Loam at two
lands; the Gods unit test covers all three land counts. The K=8/depth=2
partial run predates this correction and uses its frozen binary.

The corrected Release build passes the focused 46-assertion Gods policy test.
On the same 1,000 fully blind paired seeds, the corrected old-rule arm averages
7.017 and wins 733 games by turn eight. The corrected strict arm averages 6.910
and wins 785: **107 turn-games faster** (200 faster, 138 slower, 662 equal).
The paired standard error is 0.0318 and the approximate 95% interval is
[-0.169, -0.045] turns per game. The low-land correction changes only three
strict-arm outcomes and two old-arm outcomes relative to the pre-correction
binary. The blind advantage survives essentially unchanged. Data is under
`build/scratch/gods_retrieval_nc_lowland1000/`.

Of the 112 coupled slower games, 90 are equal or faster with the corrected
blind policy. The remaining 22 were traced with both corrected arms under
the same blind settings (`build/scratch/gods_retrieval_nc_lowland_residual_traces/`).
All first differ during a main phase. In 21 cases the arms choose different
actions or plan order; in game 607 they execute the same Careful Study and
Magus actions but end with different graveyards and hands, consistent with a
different draw replacement inside the action. That one outcome is a real
blind-play loss (turn six versus seven): the strict arm mills Replenish during
the turn-three Study, while the old arm draws it on turn four. Both arms had
the same visible board and hand before Study, so this trace does not show a
visible condition that predicts drawing is better across games. Other blind
losses could still contain a genuine conditional draw case; the paired average
does not rule one out. Adding a draw choice
to the live search would expand the decision surface and allow coupled search
to exploit the true unseen top card. The provider keeps the user's low-land
draw exception and otherwise stays on the measured dredge policy.

## Fresh blind held-out check after integration

The integrated shared-branch binary passed all 3,305 unit cases and 3,612,583
assertions. On 2,000 new paired seeds starting at 8,100,001, with the same
fully blind K=4/depth=1 search and blind bottoming, the old rule averaged
7.056 turns and won 1,481 games by turn eight. Strict dredging averaged 6.9255
and won 1,590. The paired difference was **-0.1305 turns per game** (427 faster,
275 slower, 1,298 equal), standard error 0.0237, approximate 95% interval
[-0.177, -0.084]. The independent result confirms the blind advantage. Data is
under `build/scratch/gods_retrieval_nc_heldout2000/` in the primary checkout.

## Coupled-search audit and narrow Loam trial

Gods-only smoke/regression on the integrated binary leaves both depth-zero
cells identical and moves the searched cells slower: smoke d3 +4 turn-games,
smoke d5 +11, regression d3 +15, regression d5 +29. The viewer protocol still
has zero play drift, board divergence, or enumeration gaps across 854 saved
references. The per-game audit reports 18 slower and six faster smoke games,
32 slower and five faster regression games. All old/new trace replays succeeded
using the baseline's historical card data. The smoke 4×/16× budget classifier
found four of 18 slower games recover and 14 persist. These coupled results
must be reported alongside the blind benefit; not every slower game is a small
search-budget artifact. Logs are `build/scratch/gods_integrated_regression.log`
and `build/scratch/gods_classify_smoke.log` in the primary checkout.

A default-off worktree trial, `MTG_GODS_LOAM_FOURTH`, chose Loam at three lands
only when Replenish was already in hand, no land was in hand, and Loam could
recover a graveyard land. This is a narrower subset of the old rule-7 idea:
it does not introduce a natural draw at three lands. The off arm reproduced
the integrated binary exactly in the first 50 blind games. Across 1,000
paired blind seeds, the trial was four turn-games slower (80 faster, 80
slower, 840 equal; interval for turns/game [-0.037, 0.045]), although it won
two additional games by turn eight. The directly traced game 607 got worse,
turn seven to eight. The trial is dropped; it is not integrated on the shared
branch. Data is under `build/scratch/gods_loam_fourth_blind1000/` in the Gods
worktree.

The regression 4×/16× classifier covered all 32 slower games: eight recover
the old turn and 24 persist. Its final 16× game was stopped after the same
game had already recovered at 4×; the 63 completed reruns and per-game verdicts
are preserved in `test/logs/regression/classify/`, with a complete summary in
`build/scratch/gods_classify_regression_complete.txt`. The persistent coupled
slowdowns are part of the policy cost; the independent blind measurements
favor strict dredging despite them.

## Two-headed blind check

The same fully blind K=4/depth=1 policy was paired on 1,000 fresh two-headed
games starting at seed 8,400,001 (starting life 30, two opposing heads). Old
natural draws averaged 7.077 turns and won 742 games by turn eight; strict
dredging averaged 7.008 and won 769. Strict was 69 turn-games faster (185
faster, 139 slower, 676 equal), with paired standard error 0.0325 and
approximate 95% interval [-0.133, -0.005] turns per game. This smaller but
positive result supports the same policy in the two-headed variant. Data is
under `build/scratch/gods_retrieval_nc_2hg1000/` in the primary checkout.

The saved two-headed smoke cell moved from 5.32 to 5.56 turns (+6 turn-games
over 25), and regression from 5.22 to 5.80 (+29 over 50). Their per-game
audits found four slower/two faster smoke games and 17 slower/zero faster
regression games. The strict viewer protocol again reports no correctness
gaps. These are the coupled-search costs that accompany the blind gain.

All four two-headed smoke slowdowns persist at 4×/16× budget. Of the 17
two-headed regression slowdowns, five recover and 12 persist. As in the
one-headed classification, the final 16× rerun was stopped after that game's
4× result had already recovered the old turn; the per-game verdicts are in
`build/scratch/gods2hg_classify_regression_complete.txt`. The blind two-headed
gain is modest but survives its paired confidence interval.

## Visible-state natural-draw follow-up

The user asked whether a residual blind loss can reflect a genuinely better
natural draw. A default-off trial in an isolated worktree draws at
four or more lands with three or more Gods in the graveyard, no Replenish in
hand, at most one Replenish in the graveyard, and Mystic Retrieval still absent
from the graveyard. These are observable facts; the rule never reads library
order. With fully blind K=4/depth=1 search and blind mulligan bottoming, 1,000
paired training games at seeds 8,700,001–8,701,000 had 784 baseline wins and
780 trial wins. The loss-penalized average moved from 6.944 to 6.964 turns:
71 games faster, 84 slower, 845 tied; paired 95% interval for the turn delta
[-0.020, +0.060]. The broad exception has no measured benefit and stays off.
Representative faster and slower game replays often diverge at an earlier
main-phase plan because blind lookahead evaluates the future rule. A tighter
visible-state trial requires at least four graveyard Gods, zero graveyard
Replenishes, and at most 20 cards left in the library. On the same training
block it changed 12 play digests but only two win turns: one unwon game became
a turn-six win and one turn-seven win slipped to turn eight. The mean gain was
0.002 turns per game, paired interval [-0.008, +0.004]. On a fresh 2,000-pair
held-out block (seeds 8,800,001–8,802,000), the narrow rule changed only 16
play digests: five win turns faster, five slower, 1,990 equal. Baseline won
1,526 games and averaged 7.0105; the trial won 1,525 and averaged 7.0110.
The paired delta was +0.0005 turns/game, interval [-0.0044, +0.0054]. Neither
tested visible condition has a reproducible blind advantage, so the shipped
strict-dredge rule and its zero-or-one-land natural-draw exception remain.
Data are under `build/scratch/gods_visible_draw_trial/`.
