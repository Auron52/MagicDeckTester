# Cantrip branching in NivMizzetCombo: Brainstorm, Ponder, Preordain

Status: OPEN, deferred to the heuristic work planned for the week of 2026-10-19. USER (2026-10-18):
*"We may need to work more on heuristics for this deck next week. Ponder, Preordain and Brainstorm are
especially troublesome in terms of potential branches."*

This page records what each of the three cantrips costs the search today, what has already been
collapsed, and the remaining options with their classification. Every number comes from the
2026-10-18 perf pass (branch `niv-perf`, integrated into `niv-aura`; scratch data in
`build/scratch/niv-perf/`) unless stated otherwise.

## The deck and the rules that bind the work

`decks/NivMizzetCombo/NivMizzetCombo.cod` runs 4 Ponder, 4 Preordain, 2 Brainstorm, 2 Gitaxian Probe
and 4 Remand around a Niv-Mizzet draw/ping loop. It is NOT in the regression suite yet: it fails the
3x cost rule (see `docs/design/analysis-NivMizzetCombo.md`, Stage 5j).

The rules any change here must follow (CLAUDE.md):
- **No greedy in the search window.** A provider may ORDER or RESTRICT options; the engine makes no
  heuristic pick with depth remaining.
- **Completeness at large budget.** Anything that makes a line unfindable at `--budget-ms 0` is lossy and
  out, whatever it measures at the shipped budget. Re-pricing or ordering a finite budget's spend is
  the preferred shape.
- **Collapse wasted search unconditionally.** A SOUND fold (identity or dominance) ships default ON
  even when wall does not move. A LOSSY narrowing stays default OFF until a held-out A/B and the user's
  approval.

## What each cantrip offers the search

Measured with `MTG_CANTRIP_ATTRIB=1` (diagnostic, default off, commit "diag(cantrip): MTG_CANTRIP_ATTRIB")
on the four games that blew up the suite cost.

| Card | Params | Menu per resolution | Searched? |
|---|---|---|---|
| Brainstorm | `draw:3, put_back_from_hand:2` | ordered pairs of DISTINCT hand classes (`PutBackClassKey`): median 23-76, p90 93-111, max 163 | yes, every pair (`Plan::putback_choice`) |
| Ponder | `draw:1, cast_reorder:3` | 7 every time: 3! orders + shuffle (the `ponder_keep` variant + the order axis) | yes, every option |
| Preordain | `draw:1, cast_scry:2` | 5: keep both (2 orders), keep one of the two (2), keep none (1) | **no**: provider's `ScryKeepOnTop` (see below) |

Share of the whole game's units / wall spent under each axis's variants (the axes nest, so the two
columns overlap and do not add):

| Game | Brainstorm put-back | Ponder |
|---|---|---|
| d3 b10 s2018 gi16 | 18% / 25% | 44% / 32% |
| d3 b10 s3007 gi4 | 29% / 33% | 47% / 53% |
| d3 b10 s2029 gi27 | 33% / 30% | 31% / 50% |
| d5 b20 s3007 gi4 | 36% / 44% | 46% / 48% |

**Preordain is not searched, and that is a recorded classification, not a slip.** A cast scry resolves
through `ScryTop` -> `ChooseTopDisposition` -> the provider's `ScryKeepOnTop`. The searched scry axis
(`MTG_SCRY_SEARCH`, `Plan::scry_choice`, `docs/design/searched-scry-disposition.md`) covers land-ETB
scries and Marit Lage's Slumber's land-drop trigger only. The no-greedy audit
(`docs/design/no-greedy-in-search-window.md`, phase 1) lists "cast-scry via ScryKeepOnTop" under
**"Fine as provider prunes"**, with the note "extend the scry axis to cast sources". So Preordain adds
cast-order and breakpoint branches (it draws a card), but not scry branches. Making it a searched axis
is the USER's call (ledger Q14): up to 5x branching per Preordain cast. The only other shipping
Preordain deck is Hinata2 (the suite's `hinata` cases, keep table and value leaf), whose play would
move with it.

**The rule Preordain gets today looks wrong for this deck.** Niv rides `NivMizzetComboProvider`, an
empty derivation, so its `ScryKeepOnTop` is `GenericProvider::ScryKeepOnTop`
(`src/ai/DecisionProviders.cpp`): *keep every nonland; keep a land only while fewer than two lands are in
play.* Both Nivs cost 6. So from turn 3 on, every Preordain bottoms every land it sees, in a deck that
needs six mana (21 lands + Signets, Sol Ring, Servants). The same predicate also supplies the "wanted
first" order inside Ponder's keep branch, but there the order axis searches every order, so only
Preordain is bound by it. A Niv-specific `ScryKeepOnTop` (e.g. keep a land while mana sources < 6 and no
Niv is castable; keep a Curiosity / Lookout when a Niv is in hand or in play) is a provider heuristic.
Per `heuristic-optimization.md` it would be authored as variants behind a temporary selector, measured
on held-out seeds and adopted only with the user's approval. This is the cheapest lead on the page and
the first thing to try, whatever is decided about searching the scry.

## Already collapsed (SOUND, default ON)

| Fold | Where | What it removes |
|---|---|---|
| Closed-form put-back pick | `PutBackPickAt` / `PutBackFoldMemo` (niv-bs, `862224397`) | re-deriving the put-back ranking per variant: lossless on 400 random hands, ~2x core-ms |
| Copies are one class | `PutBackClassKey` | pairs that differ only by which copy of a card goes back |
| Brainstorm returns the LAST copy of a class | `ResolvePutBackFromHand` remap (`41950c0f0`) | a put-back taking the copy a later cast in the same plan needed (casts take the first copy) |
| Front-only canon list | `BpUnbranchedCanon` (`MTG_BP_CANON_FRONT`) | building and caching every put-back and Ponder clone of a breakpoint continuation list when only `front()` is read: 46M clones on one game, 88% of a 151 s decision. Used only when the hand holds a Brainstorm or a Ponder |
| Node put-back clamp repeats | breakpoint node (`MTG_BP_NODE_PUTBACK_FOLD`) | children whose put-back rank clamps onto one a sibling already applied. The child is still charged its unit, so budgeted play is unchanged |
| Post-apply state dedup | `PlanIsAxisVariant` (no-greedy step 9d) | Ponder orders that swap identical cards, and any axis variant whose state an earlier sibling reached |
| Nested overrun bail | `MTG_LA_NESTED_BAIL` (niv-bs, ON) | a nested solve's candidate loop after the enclosing pass's overrun ceiling tripped. Inert at unlimited budget |

Result on the explosion games (same units, same play digest): s2018 gi16 d3 99.4 s -> 8.6 s; s3007 gi4
d3 980.9 s -> 38.4 s; s3007 gi4 d5 1201.2 s -> 53.2 s. s2029 gi27 d3 (180.8 s -> 149.4 s) is bound by
mulligan bottoming, not the cantrips.

## Candidates left

**SOUND, not shipped (low value):**
- The node clamp skip WITHOUT charging the skipped child's unit. It changes budgeted play (more budget
  left for real children) and buys nothing on passes that already hit the overrun ceiling. It is a
  re-price, so it is legitimate; it needs to be tested as a play change.
- A bigger canon memo cap: measured, only -3% builds.

**ORDER-only (inert at unlimited budget, changes budgeted play, needs a held-out A/B):**
- A provider order over the Brainstorm pairs and the Ponder options (the likely-best first), so a finite
  budget spends on them first. This is the "searched-with-heuristics" shape the user asked for, and the
  most natural home for next week's heuristic work. Nothing is refused, so raising the budget restores
  every line.
- A budget-staged admission (re-price the non-default variants). `MTG_PUTBACK_PRICE` is the built
  instance for Brainstorm: it measured WORSE (d3 +0.0267, d5 +0.0133 avg turns for 0.91x units) and is
  OFF. The likely reason is that the provider's first candidate is not good enough to deserve the head of
  the queue, so a better order (above) is a precondition for any price.

**LOSSY (out under completeness unless re-shaped as an order or price):**
- Capping or ranking-and-truncating the Brainstorm pairs or the Ponder options.
- Merging put-back orders when a later Ponder in the same plan re-orders the top. Ruled out because
  the variants are searched one axis at a time, so a merge would drop combinations.

## Neighbouring costs that are NOT cantrip branching

These dominate the remaining cost and should not be mistaken for it:
- **Mulligan bottoming.** With no keep table yet, each legal bottom subset plays a full game at play
  settings: 51-56% of d3 units/wall, ~64% of d5 and 2HG. The three heaviest d5 s3003 games are 95-98% of
  this. The keep table removes it. Until then the per-deck bottom-eval setting is the lever, and it is
  the USER's (ledger Q15).
- **Aborted passes.** 45-99% of units sit in passes that ran to the global 25x overrun ceiling and were
  thrown away (ledger Q16).

## Instruments

- `MTG_CANTRIP_ATTRIB=1`: per-resolution menu widths and units / wall under each axis's variants.
- `MTG_BP_CANON_FRONT_VERIFY=1`: checks every front-only build against the full list (0 mismatches over
  167k builds).
- `MTG_NO_BP_ENUM_CACHE=1`: removes breakpoint-cache residency from unit comparisons (the 2 of 160 games
  whose units moved by -1 / -36 match exactly with it).
