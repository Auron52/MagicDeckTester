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
blind-play loss (turn six versus seven), but its trace does not show a visible
condition that predicts drawing is better across games. Adding a draw choice
to the live search would expand the decision surface and allow coupled search
to exploit the true unseen top card. The provider keeps the user's low-land
draw exception and otherwise stays on the measured dredge policy.
