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

### THE GATES (2026-09-02, binary = f14312f7's src, both batches same binary, contention-proof)

**Phase 1 -- canon alone vs shipped, 186,000 games, 16 decks, paired, play settings.** Quality-
NEUTRAL everywhere: no |t| >= 2 on any deck/block. hinata +0.0007/-0.0007 at 10k/block; th
+0.0000/-0.0002 (**NGC's +0.0016 th regression: GONE**); dragonstorm -0.0004/+0.0004 with
near-base enum counters; ELEVEN decks byte-identical (0 games changed). SLOW-GAME tail symmetric
(and the box was contended -- USER's warning -- so tail read as indicative only; the counter
evidence carries the cost verdict).

**Phase 2 -- the SOUND RECIPE (SITE3+DEFER+CANON+NODE+ROOTTURN) vs shipped, 140,000 games.**
hinata ladder at 10,000/block, paired (base/canon cells reused from phase 1, same seeds):

| arm | hold | train |
|---|---|---|
| canon alone | +0.0007 (t 1.12) | -0.0007 (t -1.53) |
| **sound recipe** | **-0.0126 (t -4.82)** | **-0.0146 (t -5.89)** |
| NGC recipe (reference) | -0.0168 (t -6.20) | -0.0174 (t -6.77) |

The reference arm REPRODUCES the recorded ROOTTURN result (-0.0162/-0.0174), validating the
setup. The sound recipe keeps ~75-85% of the old recipe's hinata gain while being
lossless-by-construction; the ~0.004 gap is the price of soundness (NGC's forced-cast was
net-positive on hinata but bought with th losses and the Dragonstorm cost pathology).
**All five movers clean**: th +0.0000/-0.0002, burn and kitty BYTE-IDENTICAL, dragonstorm
-0.0004/+0.0004, mirrorwing -0.0002/-0.0004 -- nothing significant anywhere.

**This is the class's adoption candidate.** Remaining before proposing adoption: units cells
(expect ~+3% per the ROOTTURN record), regression+overnight tiers, quiet-box wall confirmation
(USER to be asked for the window), USER sign-off. Greedy `Solve` is gone from every DECISION
apply on every deck: hosted (node, root turn) at site 3, canonical-with-sound-pass (canon)
elsewhere; rollout applies keep greedy per the USER scope ruling of 2026-09-02.

### COST CONFIRMED + post-rebase checks (2026-09-02, same session)

Units cells (300 hinata games, one process/cell, play settings, measurement binary):
**base 11,096,472 -> sound recipe 11,389,594 = +2.64% units** (id_depth 3.475 -> 3.369). Under the
recorded ROOTTURN profile (+3.1%) and far under the USER's "+15% is potentially good enough" bar;
`budget_ms` is virtual so this converts ~1:1 to wall (node wall/units was measured 1.02).
Post-rebase (EDF src work): rebuild + smoke 48/48 byte-identical, gt_logs 320 consistent / 0 stale.
Flag-on REGRESSION preview launched to enumerate the cells adoption would rebaseline.
REMAINING BEFORE ADOPTION: regression preview read; overnight-tier preview; QUIET-BOX wall
confirmation (needs the USER's window); USER sign-off. Nothing adopted yet -- all flags OFF.

### Regression-tier flag-on preview (2026-09-02, new-HEAD binary post-EDF-rebase)

59 pass / 21 move. Per-cell: hinata all four cells BETTER-or-hold (-0.005..-0.015, matching the
gate); dragonstorm 3 better + 1 digest-only; burn/antilife/creature_giving avg-IDENTICAL
digest-only churn; th/mirrorwing +/-0.002-0.005 on 200-300-game cells (noise; the 5k paired gate
answered both as neutral). Net searched delta 20 faster / 16 slower. Healthy rebaseline profile --
improvement concentrated where the gates measured it. OVERNIGHT preview launched. Adoption still
awaits: quiet-box wall + USER sign-off.

### Overnight-tier flag-on preview (2026-09-02 overnight, same new-HEAD binary)

123 pass / 69 move (28m31s makespan, log `logs/ngc_sound/overnight_recipe_preview.log`). Of the 69:
**47 digest-only** (play changed, aggregate identical -- burn/antilife/creature_giving/mirrorwing/
kitty/fivecolour entirely in this class), **11 BETTER, 9 WORSE**. The improvement is exactly where
the gates put it: **hinata owns 7 of the 11 BETTER cells and 7 of the 8 largest moves** (all 4 d3
cells -0.0025..-0.0200 plus 3 of 4 d5 cells -0.0100..-0.0167; the 8th d5 cell digest-only) -- net
-0.088 summed across its cells, no hinata cell worse. Dragonstorm nets BETTER (-0.013: 3 cells
better, 1 worse +0.006). Every remaining WORSE cell is a +-0.0010 single-game move on a 1000-game
cell (th x3, auras x5 -- auras nets +0.004, i.e. 4 games in 6000; its 5k gate cells were neutral).
No new SLOW-GAME tail. Same healthy rebaseline profile as the regression tier: gains concentrated
on hinata, jitter-level churn elsewhere. All three tiers now enumerated for the rebaseline;
adoption still awaits the quiet-box wall number + USER sign-off.

### EF-on-top probe (2026-09-02 overnight, post-EDF-rebase binary, both arms fresh)

`MTG_EXEC_FEAS` added on top of the sound recipe, hinata 10k/block paired x train+hold, one pooled
batch (`logs/ngc_sound/ef_probe.json/.err`), 24/24 workers at the 10-min heartbeat:
**efrecipe -0.0017 hold (t -3.27) / -0.0016 train (t -3.41)** vs screcipe; 39 better : 7 worse
across both blocks. Small but real and consistent -- EF's executor-validated sequential payability
buys a little more of the sequential-re-pricing gap even after the node+canon take most of it. The
SLOW-GAME tail is baseline-owned (5 screcipe.train + 3 screcipe.hold vs 3 efrecipe.train), no
EF-caused tail. EF stays a SEPARATE follow-up candidate -- it was not part of the gated recipe and
does not ride along with the recipe adoption. Units pricing inside the recipe measured next
(units.efrecipe cell); EF standalone was +2.8% wall when measured in isolation.

**EF units (same session): CHEAPER inside the recipe, not dearer** -- screcipe 11,389,594 ->
efrecipe 11,326,878 = **-0.55% units** (300-game hinata cell, one process, play settings). The
executor-feasibility test prunes infeasible subsets the node would otherwise expand, so inside the
recipe EF pays for itself. (The screcipe re-run on the post-EDF-rebase binary reproduced
11,389,594 EXACTLY -- binary equivalence on hinata confirmed at the units level, not just smoke.)
Neutrality gate across the other 15 suite decks launched (`scripts/gen_ef_gate.py` -> 60 jobs /
146k games, base arm = the sound recipe; hinata reused from ef_probe, same binary+seeds).

**EF neutrality gate PASSED (146k games, 15 decks, `logs/ngc_sound/ef_gate.err`): 14 of 15 decks
are GAME-IDENTICAL under EF** (+0.0000, zero paired games changed, both blocks -- at play settings
the executor-feasibility test only ever fires on states those decks never reach); the one mover is
dragons, 3 games BETTER / 0 worse (-0.0007/-0.0013). SLOW-GAME tail balanced (fivecolour 52 EF vs
45 base -- a slow deck, not an EF tail; dragonstorm 4 vs 4). **EF's own dossier is therefore
complete and clean: hinata -0.0017/-0.0016 (t 3.3), dragons a hair better, 14 decks untouched,
-0.55% units inside the recipe.** It meets the adoption bar on its face (quality-improving AND
cheaper) but was NOT part of the gated recipe -- it is staged as a SEPARATE follow-up candidate
awaiting its own USER sign-off, surfaced in the morning report. If the USER wants it folded into
the same rebaseline the cost is zero extra measurement (one more flag flip before the tiers run);
adopting it later instead costs one more 3-tier rebaseline (~35 min of box time).

### "Is greedy deleted" -- the ACTED evidence at recipe settings (2026-09-02 overnight)

`MTG_M2_YIELD_STATS` cells, recipe env on, 300 games each, one process per cell
(`logs/ngc_sound/acted.{hinata,kitty,ds}.err`). The USER's criterion is acted->0 at
DECISION/RECORDED applies; rollout applies keep greedy by ruling.

* **hinata: s0 acted=0 of 976,131; s3 acted=0 of 1,567,976.** Executor: "REAL main-phase
  decisions by depth: NONE". Only s90 (SolveWithLookahead depth<=0 = the rollout LEAF policy,
  allowed) acts.
* **kitty: s6 acted=0 of 154,660** (the engine's once-dominant greedy site, 82.5% of all greedy
  calls at shipped settings -- now decides nothing). Executor NONE; s90 only.
* **dragonstorm: s2 (the impulse-draw re-solve) still acts 31,945 of 48,304** -- but the
  nohost-kind split is the decisive read: **ZERO ROOT-kind fallbacks on all three decks.** Every
  s2 fallback is [rollout] 96.6% / [rollout+rec] 3.4% (wave entries inside rollouts) -- the
  committed decision's own enumeration NEVER falls to unhosted greedy. Canon's derivation
  stand-down (g_bp_enum_depth > 0) plus rollout applies fully account for the residue, and both
  are rollout-side under the scope ruling. Executor NONE here too.
* fell-to-greedy attribution: class-masked 0, empty-cands 0 on all three decks -- no coverage
  hole is hiding behind the mask or an empty enumeration.

**Net: with the recipe on, greedy decides NOTHING at any decision/recorded apply on the three
instrumented decks (incl. the two known worst cases); every remaining greedy act is the rollout
playout policy the USER's ruling allows.**

### Per-deck units + the staged adoption checks (2026-09-02 overnight)

Units at play settings, 300-game cells, base (five flags =0) vs recipe, flipped-defaults binary:
**dragonstorm 1,165,581 -> 1,152,906 = -1.09% (CHEAPER); th 2,026,382 -> 2,016,679 = -0.48%;
kitty 4,225,626 -> 4,225,109 = flat.** Hinata's +2.64% is the suite-wide WORST case -- everywhere
else canon+node either pays for itself (the deleted greedy Solves and NGC-free enumeration) or is
inert. The five defaults are flipped per coding-conventions (`EnvOn(..., true)`, `=0` hatch) and
A3-verified: **hatch smoke (all five =0) 48/48 byte-identical to shipped GT; clean-env smoke moves
exactly 6 cells** (hinata d5 -0.0134 better, 5 digest-only) matching the recipe previews. Staged,
uncommitted -- the quiet-box wall check is the one remaining gate before commit + 3-tier
rebaseline, per the USER's 2026-09-02 criteria message (<10% wall; met => adopt).

### QUIET-BOX WALL: FAILS the USER's bar -- ADOPTION BLOCKED (2026-09-02 ~16:45, load 0.05)

`scripts/wall_probe.sh` protocol (serial, 1 thread, CPU-pinned, 3 reps, startup calibration
subtracted, arms pinned both directions -- `logs/ngc_sound/wall_final/results.tsv`):

| deck | off | screcipe | delta | efrecipe |
|---|---|---|---|---|
| hinata (200g d5/20ms) | 54.05s | 70.60s | **+30.6%** | +32.7% |
| dragonstorm (200g d5/20ms) | 7.82s | 11.71s | **+49.7%** | +53.2% |

Rep spread < 1% -- the numbers are solid. **UNITS LIED BY AN ORDER OF MAGNITUDE** (+2.64% / -1.09%
on these same decks): the recipe's real cost is UNCHARGED enumerator/apply work outside the
ConsumeAt sites, exactly the wall_probe header's warning, and consistent with the recorded "+28%
wall at equal quality" greedy-removal price -- the node-era "wall/units ~1.02" conversion does NOT
hold for the full recipe. The stats pass agrees the charged side is innocent (`units.fs_bp_node`
share 0.2%, `units.greedy_fallback` 4.7%).

Against the USER's criteria (<10%, 15% tolerable with notable quality): **+31%/+50% fails.
NOT ADOPTED; defaults stay staged-off.** Per the USER's instruction the work moves to closing the
gap before any discussion: wall DECOMPOSITION by lever launched (off / s3d / canononly / noderoot /
screcipe, same protocol, `logs/ngc_sound/wall_decomp/`), then the known cost dials (enum-memo
coverage at canon's call site -- the lever's own design premise says it should be a memo HIT;
the 8192-cap wholesale-clear thrash; nested stand-down widening).

### THE GAP-CLOSING ARC (2026-09-02 overnight, commit `a0d3f777`, CI green incl. Windows parity)

**Decomposition named the burner: CANON AT ROLLOUT APPLIES.** ds canononly ALONE = +50.7% (the
whole recipe delta); noderoot alone is FREE on ds (-0.6%) and +4.3% on hinata; hinata's +30% is
the site3-open x canon interaction (canononly is cheap on hinata only because the mask hides s3).
Context counters (`canon fires/copies/enums` by root|rec|resume|cap): hinata 4.03M fires per 300
games, 91.5% [rollout+rec]; each paid a GameState deep-copy (644k) + Solve + a full plan-vector
copy (1.78M enumerations). ds 96.6%+ rollout-side.

Three cuts, each verified (hatch smoke 48/48 byte-identical vs GT; recipe fingerprints IDENTICAL
under MTG_NO_BP_ENUM_CACHE=1; cross-build play identity):

| form | hinata wall | ds wall | play |
|---|---|---|---|
| canon-everywhere (the gated form) | +30.6% | +49.7% | the 140k-gate quality |
| + rollout scope (plain rollout applies stand down) | +20.8% | +26.1% | changed -- re-gate |
| + verdict memo (entry-rider) | +23.5% (WORSE: PASS states derived plans) | +14.6% | same as scoped |
| + verdict memo (SEPARATE map, shipped form) | **+18.7%** | **+14.2%** | same as scoped |
| TIGHT scope (MTG_BP_CANON_REC=0: root/resume/capture only) | **+6.1%** | **+13.3%** | changed -- re-gate |

Residue after the memo = the BuildBreakpointKey walk itself, 4M times (the recorded enummemo
lesson: "a revisit must make the KEY incremental"). The TIGHT scope deletes 91.5% of hinata's
fires instead -- **hinata +6.1% is UNDER the USER's 10% bar** -- and its open question is pure
quality: do the [rollout+rec] fires carry any of the -0.0126? `scripts/gen_tight_gate.py` (140k
games: hinata off/screcipe/tight at 10k/block, 4 movers off/tight at 5k/block) measures exactly
that. ds's tight residue is capture-context canon (the node's own children) -- genuinely
decision-side, likely the floor without an incremental key.

**Traps this arc added to the record:** (1) units UNDERSTATE any lever whose work is uncharged
enumeration/apply -- wall-probe the lever itself, never convert units->wall by a prior lever's
ratio; (2) NEVER edit source while a background build runs -- the object lands NEWER than the
source and the next build silently reuses it (cost: a defaults-revert that "didn't take", found
only because clean-env play failed byte-identity); (3) sed range args print in FILE order, not
argument order.

### TIGHT QUALITY GATE + THE FULL WALL TABLE (2026-09-02 late night)

**Tight keeps a REAL hinata gain at roughly half the full recipe's size** (140k-game gate,
`logs/ngc_sound/tight_gate.err`, all arms fresh on one binary): screcipe REPRODUCES
-0.0126/-0.0137 (t 4.9/5.5); **tightrecipe -0.0057 hold (t -2.67) / -0.0096 train (t -4.52)**;
movers th/kitty GAME-IDENTICAL under tight, mirrorwing 2 games better, ds jitter-neutral. So the
[rollout+rec] canon traffic carries about half the quality -- it is partially load-bearing, and
the choice is now a priced menu, not a blocker.

**Definitive quiet-box wall sweep** (4 arms x 4 decks x 3 reps, `logs/ngc_sound/wall_sweep/`,
rep spread ~1%): hinata off->tight +6.4% / recroot +6.3% / screcipe +18.6%; ds +12.9 / +10.6 /
+16.4; kitty +0.2 / +0.0 / +6.3; mirrorwing +2.9 / +2.6 / +3.1.

**MTG_BP_CANON_RECROOT built (the middle arm): tight + rec applies on the ROOT TURN only** (same
authority test as the node's ROOTTURN gate, `g_condemn_root_turn`). Its wall EQUALS tight's --
root-turn rec traffic is nearly free, the ROOTTURN discovery repeating one level down. The
recroot quality probe (hinata off/tight/recroot, 10k/block x 2, one binary) is the night's last
measurement: if recroot recovers quality over tight, it is FREE quality; if not, tight is the
candidate. NOTE dragons smoke cells moved 2026-09-02 late -- that is the OTHER agent's adopted
Dragons keep table (`c25d45b1`), their rebaseline, not this arc's.

## ADOPTED: the TIGHT sound recipe (2026-09-02, USER conditional sign-off, live conversation)

**recroot measured QUALITY-IDENTICAL to tight** (train literally 217:128 / -0.0096 in both arms;
hold within 2 games) -- the rec traffic's quality lives on LOOKAHEAD turns, not the root turn.
Recorded negative; the lever stays as a probe arm.

**The USER's rulings settled the design:** (1) *"Rollouts being greedy is fine... I can always
increase depth and budget to rely on them less. That is not true for the searched part."* --
TIGHT is the correct scope: canon covers the searched structure (root enum, node resume,
captured applies), every playout apply (recording or not) keeps greedy. (2) *"If quality is
always improved or neutral and dragonstorm is the only one over my limit I think we could adopt
and then look into dragonstorm performance from the new baseline."*

**Both conditions verified before flipping anything:**
* Quality: hinata -0.0057 hold (t -2.67) / -0.0096 train (t -4.52) at 10k/block paired; the 4
  gate movers clean; the other 11 suite decks GAME-IDENTICAL at 1500/block x 2
  (`logs/ngc_sound/tight_screen.err`) -- improved-or-neutral everywhere.
* Wall (quiet box, pinned, cal-subtracted): hinata +6.4%, mirrorwing +2.9%, goblins +2.1%,
  knights +1.3%, dragons +1.4%, everything else <= +-0.6% (th +0.6%, fivecolour -0.2%) --
  **dragonstorm +12.9% is the ONLY deck over 10%** (`logs/ngc_sound/wall_spot.tsv` + wall_sweep).

**Shipped:** five recipe defaults ON + `MTG_BP_CANON_REC` default OFF (each with its =0/=1
hatch). A3-verified: hatch (five =0) 48/48 byte-identical to pre-adoption GT; flipped clean env
byte-identical to the env-pinned form. All three GT tiers run clean, inspected against the tight
previews (cell-for-cell identical), accepted; `check_gt_logs.py` 320 consistent / 0 stale.
Executor evidence at ship settings: main-phase greedy decisions NONE, zero ROOT-kind fallbacks.

**Open from the new baseline:** (1) dragonstorm's +12.9% (capture-context canon at the node's
children; the honest lever is the INCREMENTAL KEY -- the BuildBreakpointKey walk is the repeat
cost everywhere); (2) the ~0.005 hinata quality left in playout-side canon, recoverable by
depth/budget per the USER's own argument, or by the incremental key making REC affordable;
(3) MTG_EXEC_FEAS -- separate, fully-gated follow-up candidate (hinata -0.0017/-0.0016 t~3.3,
-0.55% units, 14/15 decks game-identical), awaiting its own sign-off.

**EF RE-MEASURED ON THE TIGHT BASELINE (2026-09-03): MARGINAL -- PARKED, not adopted.** The old
dossier (-0.0017/-0.0016 AND -0.55% units) was measured on the UNSCOPED canon recipe; on the
shipped tight baseline (130k games, `logs/ngc_sound/ef_newbase.err`): hinata **-0.0005 both
blocks (t -2.24), 5 better / 0 worse each**; dragons 3/0; 14 decks zero games changed; units
FLAT (11,339,543 -> 11,339,860); wall +0.8% (pinned pair). Most of EF's gain rode on the
enumeration volume the tight scope removed. Still strictly one-sided (never worse in 40k paired
games across two baselines) but 10x smaller and no longer self-funding -- below the effort of a
rebaseline unless the USER wants it anyway (a 25-minute adopt) or the incremental key re-widens
canon (EF's value likely returns with REC=1). LESSON, again: a lever's dossier is BASELINE-BOUND
-- re-measure on the baseline it will actually ship onto.

### The hinata BUDGET SWEEP (2026-09-03) -- the USER's depth/budget argument, priced

From the adopted baseline, budget_ms alone (value_play lock keeps d5; b20 self-check reproduces
the shipped means exactly), 10k/block paired x train+hold (`logs/ngc_sound/budget_sweep.err`):

| budget | hold | train | better:worse | wall (PROVISIONAL -- box re-contended mid-session) |
|---|---|---|---|---|
| b20 (shipped) | -- | -- | -- | 62.1s/200g |
| b30 | **-0.0139 (t -7.8)** | **-0.0159 (t -8.6)** | 178:54 / 197:50 | ~+26% |
| b40 | **-0.0231 (t -10.7)** | **-0.0225 (t -10.1)** | 283:77 / 280:79 | ~+36% (sub-linear: games end) |

Budget buys hinata quality FASTER than canon re-widening would (REC=1: -0.007 for +12%): b30
alone exceeds the entire unscoped recipe's -0.0126. This validates the USER's ruling premise
with numbers and weakens the incremental-key urgency further -- the parked ~0.005 is purchasable
with settings. A budget change is a PER-DECK play-settings adoption (USER's call; it also uses
the current value leaf slightly off-distribution, which the sweep's numbers already price in).

**Dragonstorm attribution remains OPEN and now DOUBLY so:** a re-triple on the current binary
read off==noderoot==tight (~10.0/9.9/9.9s -- the +12.9% GONE), but the box re-contended in that
window (load 13-21 from another container; the same window inflated a hinata tight rep to
142s), so neither the disappearance nor the original +12.9% is currently trustworthy on the
shipped binary. Needs one clean quiet-box triple before any decision keys on it.

**Dragonstorm characterized from the new baseline (2026-09-02, first post-adoption probe):
NOT cache thrash.** At ship settings the enum memo runs at a 94% hit rate (176,079 hits /
11,374 misses / 1 clear / 0 nested per 200 games), and MTG_BP_ENUM_CACHE_CAP=262144 is
wall-NEUTRAL vs 8192 (9.73 vs 9.88s, within rep noise) -- the recorded "1M cap halves the wall"
finding belonged to NGC's uncharged recursion, which canon's derivation stand-down deleted. The
+12.9% is the VOLUME itself: ~187k BuildBreakpointKey walks (each "costs as much as a small
enumeration" -- the enummemo record) plus 11.4k real derivations. Conclusion: the INCREMENTAL
KEY project is the one lever that addresses ds's wall, hinata's scoped residue, AND the REC
re-widening at once; no cheaper dial exists in the current structure.
