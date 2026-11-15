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

Seed 7,900,011 illustrates the cause. With the old rule, the turn-three
Careful Study draws Replenish and the deck wins turn four. With strict
dredging, Study mills Replenish; Mystic Retrieval is not milled until turn
five, is flashed back on turn six to return Replenish, and the deck wins turn
seven. This is the policy tradeoff itself, not a failure to select Loam
targets or a lost plan at the Study breakpoint. The user expressly rejected
the old natural-draw-for-Replenish rule. This conflicts with the handoff's
net-nonworsening adoption bar, so the measured tradeoff has been sent to the
user for a choice. Do not promote this branch or its ground truth before the
answer. A fresh mana-clean gate is still needed if the rule is adopted.

The Gods-only combined smoke/regression batch finished all eight cells. Both
depth-zero cells are byte-identical to ground truth. All six searched cells
have a worse total win-turn score: smoke d3 +4/50, smoke d5 +11/50, smoke
2HG +6/25; regression d3 +15/50, regression d5 +29/50, regression 2HG
+29/50. The per-game audit was stopped because this branch is awaiting the
policy choice; the completed `.wins` files remain in `test/logs/` in this
worktree. No ground truth was accepted.
