# Deleting the greedy breakpoint continuation (MTG_BP_NO_GREEDY_CONT)

**Status: BUILT, measured on 9 decks, default OFF. Adoption is a USER call; one deck (hinata) is
still underpowered.** Self-contained. Everything needed to resume is here or in git.

## 0. The USER's bar, in their words (2026-08-26)

> *"I would rather delete all greedy because we know it can be wrong. However, I want to work to
> avoid compromises."*
> *"Greedy removal is a correctness thing from my perspective. So, while it would be nice to get a
> bunch of extra wins when removing it, I'm willing to accept it not being there and not paying a
> notable performance or quality cost."*
> *"So if it is performance + quality neutral that works. If it is much slower then we'll have to
> see what additional options we have."*

So the acceptance test is **quality-neutral AND performance-neutral vs baseline, in exchange for
deleting greedy**. A play-identical result is NOT a reason to keep greedy -- it means there is
nothing to trade away. An agent draft that concluded *"a greedy step is worth removing only where it
is measurably wrong"* was RETRACTED (`e35a270c`): "the digests matched on the sample we ran" is an
absence of evidence, the same shape the standing no-lossy-truncation bar already rejects.

## 1. What the lever changes

`bp_searched_plan` (in `TurnSolver.cpp`, the shared breakpoint-apply lambda) resolves a SEARCHED
continuation only when

```cpp
eligible = plan.bp_choice >= 0 && class_on && seen_before == plan.bp_at;
```

i.e. only for the ONE variant that is targeting THIS breakpoint occurrence. Every other plan that
reaches the same breakpoint -- above all the BASE PLAN itself -- fell through to a greedy
`TurnSolver::Solve`. Two consequences:

* it is the dominant greedy site in the engine on some decks, and
* a base plan was **SCORED on a greedy continuation while its own variants were scored on searched
  ones**, so the two were never compared on the same footing.

`MTG_BP_NO_GREEDY_CONT` (heurarm slot `BP_NO_GREEDY_CONT`, default OFF) replaces that fallback with
the CANONICAL continuation, `cands[0]` of the enumerated list. It is near-free by construction: the
variants of the same base plan already enumerated this exact breakpoint state, so it is an enum-memo
hit and a vector index instead of a greedy `Solve`. Gated on `class_on`, so it cannot reach a deck
whose breakpoint class is masked off.

### The greedy split that made this findable

`MTG_BP_PROBE` now prints `[greedy split: overrun=N untarget=N]`:

* **overrun** -- eligible but `bp_choice >= cands.size()`. Wave 0 emits a fixed `depth*W` variants
  blind, so these are provably WASTED duplicates of the base plan. They scale with W.
* **untarget** -- the plan is not the variant exploring this breakpoint. **FLAT across every width,
  order and condemnation configuration tried**, because it is decided by plan/variant structure, not
  by the continuation list. This is the real population, 82.5% of Kitty's.

## 2. Measurement -- KittyEquipment (pre-rebase binary, but re-confirmed post-rebase)

| check | result |
|---|---|
| GREEDY DELETED | s8 337,769 -> 79,159 per 200 games (**-76.6%**); untarget -78.6% |
| quality, play settings, 6,000 paired games | **0 faster, 0 slower**; hold BYTE-IDENTICAL, train differs in 5 of 3,000 |
| quality, mode 3 (d7/b10000), 1,500 paired games | **BYTE-IDENTICAL, 0 games differ** |
| performance, play settings | **-0.04% / -0.11%** (250 cheaper : 57 dearer, 234 : 65) |
| performance, mode 3 | -0.01% / -0.00% |

It needs neither the full cast order nor condemnation -- it stands alone.

## 3. Cross-deck, ON THE MERGED BINARY (post mana-overhaul rebase, 2026-08-26)

31,200 games, one pooled batch, paired, both blocks. **21 better : 22 worse overall.**

| deck | better | worse | t | units | note |
|---|---|---|---|---|---|
| mirrorwing | 2 | 1 | -0.58 | -0.33% / -0.73% | see §4 -- the earlier regression is VOID |
| kitty | 0 | 0 | — | -0.21% / -0.04% | cheaper |
| burn | 0 | 0 | — | -0.03% | cheaper |
| th | 13 | 13 | 0.3 | -0.43% / +1.13% | neutral |
| dragonstorm | 6 | 4 | 1.1 | +2.03% / -2.11% | neutral |
| **hinata** | **0** | **4** | 1.42 | +1.06% / +0.53% | **UNDERPOWERED (n=400) -- the open item** |
| creature_giving, fivecolour, antilife | — | — | — | 0 games differ | masked class, see §5 |

## 4. A pre-rebase result that did NOT survive the update -- read this before trusting any old number

On the PRE-REBASE binary mirrorwing measured **6 better : 26 worse** (t = 2.14 / 2.84), and all 26
were escalated on both arms: 20 recovered at 100x budget, and **6 survived 100x budget AND +1 depth**
-- which is the standing test for a genuinely deleted line. That looked like a hard blocker.

**On the merged binary it is gone**: 4 better : 2 worse, faster on both blocks, cheaper. The games
the lever touches also fell from ~225 to ~90 per block, i.e. the overhaul changed mirrorwing's play
enough that the fallback rarely fires there now.

**LESSON: a regression root-caused against a superseded baseline can evaporate.** Re-measure before
root-causing anything across a rebase that replayed engine changes -- CLAUDE.md already says to
rebuild and re-check after such a rebase, and this is what that rule is protecting.

## 5. Three decks the lever CANNOT reach, and why

creature_giving, fivecolour and antilife show 0 games differing. Their breakpoint greedy is the
`deferred_cantrip` class = **site 3, which `MTG_BP_SITES` masks off by default** (default `0x77`).
A disabled class is never `eligible`, so `class_on` is false and the lever declines.

Measured on FiveColour, 200 games:

| arm | s8 greedy | searched | avg |
|---|---|---|---|
| default (mask 119) | 40,311 | 0 | 4.8850 |
| enable the class (mask 127) | 40,311 | 0 | 4.8850 |
| **127 + MTG_BP_NO_GREEDY_CONT** | **9,218 (-77%)** | **30,972** | 4.8850 |

Enabling the class ALONE does nothing -- no variants are emitted for a class outside the wave-0 mask,
so `bp_choice` stays negative and nothing is ever eligible. It takes BOTH. That pair is **not yet
A/B'd** and is the other open item.

> **TRAP, and it invalidated a recorded "dead end".** `MTG_BP_SITES` is parsed with `std::atoi`, so
> **`MTG_BP_SITES=0x7F` parses as 0** -- every class OFF, which reads as "no change" and is
> indistinguishable from "the mask is not the cause". A note in this repo concluded exactly that from
> exactly that command. Use DECIMAL (119 default, 127 adds bit 3).

## 6. What "the window" is NOT (a corrected agent misreading)

`MTG_BP_SEARCH` (W, default 2) is **not** a cap on which continuations the search can reach. It is
how many ranks **wave 0** emits variants for; the **deferred wave phase** (`BpWaveWalker`,
`MTG_BP_WAVES`, default ON) walks ranks >= W afterwards, reading the real list length back from
`g_bp_cands_last` because the length is only knowable at APPLY time. `TurnSolver.cpp` says it
outright: *"no rank is unreachable at an unbounded budget."*

So the `[bp-cands] unreachable=NN%` figure is a **wave-0-only statistic** and must NOT be read as a
truncation defect. Widening W was measured and is actively harmful: on Kitty, W=2 -> W=32 drops
nominal `unreachable` 63.4% -> 2.2% but **`searched` stays flat (274,359 vs 272,403)** while greedy
rises 5x (all overrun) and cost rises 42%. The budget binds first; widening only manufactures waste.

## 7. Related work landed alongside (all default OFF, all measured)

* `MTG_KE_ORDER_FULL` -- KittyEquipment's FULL (total) cast order, 17 distinct positions,
  param-derived. The shipped order is a CLASS ranking with ties, and those ties are why condemnation
  is inert there: a TIE cannot be a decline, which forces the tie-exemption, which exempts the whole
  Equipment class -- the very class the equipment-ETB breakpoint is about. Measured: condemnation
  live on 82,506 of 82,506 continuation enumerations, dropping **84 cards, every one Puresteel
  Paladin**. With the full order that becomes **thousands**, spread across the Equipment class, and
  its reach goes from ~90 to ~278 games (3.5x). Quality vs baseline over 6,000 paired games: 1
  faster, 0 slower. Mode 3: 0 faster, 0 slower. **The 17 positions still await USER review.**
* `MTG_M2_D0_SEARCHED` -- the `depth<=0` branch-site interior second main. Play-IDENTICAL on
  antilife and kitty over 2,000 unbudgeted games; costs up to +74%. See
  `searched-design-deck-rollout.md` §3c.

## 8. Open items

1. **hinata needs power** -- 0 better : 4 worse at n=400 per block, t=1.42. It is the only deck
   leaning negative. Re-run at >= 2,000 games/block before any default flip.
2. **The masked-class pair** (`MTG_BP_SITES=127` + the lever) is measured on FiveColour for GREEDY
   COUNT only; its quality/perf A/B has not been run, on any of the three decks.
3. **KittyEquipment's 17-position full order awaits USER review**, and a full order for MIRRORWING
   was requested (2026-08-26) and not yet authored -- its reviewed order is still class-based, with
   ties at rank 5 (Mirrorwing Dragon / Zada), among the mana dorks, and at 14 (Impolite Entrance /
   Oracle's Restoration).
4. **~23% of Kitty's site-8 greedy remains** (79,159 calls) -- `cands` empty or `class_on` false.
   Not yet split.
5. **The horizon (s90) is untouched** and is 62% of all greedy engine-wide. Different problem: at
   depth 0 there is no search left, so the replacement is a leaf evaluator, not more search.
6. **The OVERNIGHT GT tier is STALE** (20 kitty cells) -- smoke and regression were regenerated and
   accepted after the rebase, overnight was not.

## 9. SOUND-NGC: `MTG_BP_CANON_CONT` (2026-09-02, BUILT -- gate pending)

Idea 1 of the post-attribution menu (`bp-node-partition.md`, "IDEAS MENU"). Both prior deletion
forms failed for reasons that were finally ROOT-CAUSED this session, and each root cause dictated
one piece of the new lever's shape:

### Root cause A -- NGC's "lossiness" was two-thirds an INSTRUMENT BUG, one-third real

`MTG_CONT_DIFF` compared greedy at the PRE-LAND state, while the real greedy path plays the
breakpoint land BEFORE its Solve. Every continuation the land makes affordable therefore
mis-reported as "greedy declines": th's top case (n=11,568, `ORDER=[Treasure Hunt] greedy=[(nothing)]`)
sat at `unt=1/2 pool=0` -- Treasure Hunt ({1}{U}) is UNAFFORDABLE there without the land. The
instrument now rebuilds greedy's post-land state on a copy (the land lambdas took a `GameState&`
target param for this). Corrected th picture: agreement 93.4% -> **98.0%**, and the TRUE empty
class (greedy genuinely casts nothing) is 3,226 of 5,261 diffs (61%) -- real, but a third of what
the buggy instrument showed.

### Root cause B -- the Dragonstorm wall is UNCHARGED RECURSIVE enumeration

gi=2686 (`--seed 6602687 --game-index 0`), base vs NGC: 36k -> **1.20M** bp-enum lookups, 582 ->
**46,992** full derivations, **5 wholesale cache clears** (cap 8192), 8.85s -> ~150s wall at LOWER
units. The nested split (new `[bp-enum] nested_*` counters) pins it: **96% of the lookups AND the
misses arrive with `g_bp_enum_depth > 0`** -- an apply inside `EnumeratePlansWithLand` hits a
breakpoint, NGC enumerates again, at every level, none of it billed to units. A 1M cache cap
(clears=0) only halves the wall, so thrash is half the story; the recursion is the rest.

### The lever

`MTG_BP_CANON_CONT` (heurarm slot, default OFF; mutually exclusive with NGC / BASE_EMPTY):

* **ACT-vs-PASS is judged by the greedy path's own Solve at its own post-land state** (a state copy
  is taken only when a drop is actually still open). On PASS it falls through to the greedy path
  verbatim -- the land is replayed on the real state and the greedy Solve memo-hits the probe's
  state, so a genuine "cast nothing" reproduces byte-for-byte. This deletes the empty-class
  lossiness BY CONSTRUCTION (measured: every `greedy=[(nothing)]` case vanished from th's list).
* **On ACT the continuation is the canonical `cands[0]`** -- greedy's free pick of WHICH casts is
  what stays deleted; only act-vs-pass remains with Solve, the same judgement class as the accepted
  greedy land drop.
* **Stands down inside a derivation** (`g_bp_enum_depth > 0`): kills the recursion. gi=2686 under
  canon: 46,592 hits / 2,431 misses / 0 clears / nested=0, no SLOW-GAME (was 145-151s under NGC),
  same win turn.

th residual under canon: 99.0% agreement, 7 cases, ALL pure pick-order differences
(Land's Edge-vs-Treasure Hunt-vs-Throes) -- the intended payload, each a cast-order review item.

### Gate (pending)

`scripts/gen_canon_gate.py` -> 64 jobs / 186,000 games, one pooled batch, every deck at its own
play settings, two disjoint blocks, paired via MTG_DUMP_WINS. ACCEPTANCE: overall
quality-neutral-or-better; **hinata keeps a real gain** (NGC's only quality, -0.006 -- lose that
and the lever has no upside); **th's +0.0016 regression gone**; **no one-sided dragonstorm
SLOW-GAME tail**. Flag-off byte-identity smoke ran first (the land-lambda refactor touches the
play path).
