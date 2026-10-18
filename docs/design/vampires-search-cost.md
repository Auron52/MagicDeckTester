# Vampires search cost (Stage 5f, 2026-10-18)

Deck `decks/Vampires/Vampire.cod`, tip `728c51afd` (the merged analysis). Branch `vampires-perf`.
Goal (CLAUDE.md "COLLAPSE WASTED SEARCH UNCONDITIONALLY"): find the wasted branching in the slowest
games and collapse it -- sound folds default ON, anything lossy default OFF and put to the USER.

## 1. The slow games, and what they are made of

`build/Release/mtg decks/Vampires/Vampire.cod --profile decks/Vampires/Vampire.profile.json --games 1
--seed S --game-index G` + the cell's depth / budget (2HG: `--start-life 30` via `MTG_START_LIFE=30
MTG_OPPONENT_HEADS=2`, or the manifest's `starting_life` / `opponent_heads`).

| game | cell | units before | units after | wall before -> after (paired, contended) |
|---|---|---|---|---|
| 13180132 gi131 | d5 b20 | 4,855,143 | **1,439,773** (3.4x) | 76.3 s -> 21.3 s |
| 13180132 gi131 | d3 b10 | 1,724,802 | **470,256** (3.7x) | 30.3 s -> 7.0 s |
| 13180134 gi133 | d5 b20 | 2,166,970 | **747,059** (2.9x) | 54.7 s -> 12.6 s |
| 13180033 gi32 | d5 b20 | 805,260 | 805,260 | 36.6 s -> 35.1 s |
| 2HG 13180009 gi8 | d3 b10 | 1,179,944 | 1,109,325 | 40.4 s -> 48.9 s (box noise; units -6%) |
| 2HG 13480035 gi34 | d3 b10 | 1,471,327 | 1,471,327 | 61.1 s -> 58.7 s |
| 2HG 13480067 gi66 | d3 b10 | 738,092 | 738,092 | 43.6 s -> 43.0 s |

Every win turn is unchanged. "Wall" is one pooled batch with both arms side by side on a box other
agents were compiling on (load ~25 on 24 cores): read the ratio, not the seconds.

### 1.1 Where the units are -- the census (`MTG_TURN_CENSUS`, single-threaded)

**99.7% of every slow game's units are mulligan-bottoming trial games** (`probe=1`): gi131 mulligans
twice and the bottom is chosen by playing each candidate out as a full game at play settings (35 trial
games; `[bottom-cost] rollouts=35`). The real game's own decisions cost ~5k units. So a slow game is
"(number of bottom candidates) x (one expensive game)" -- the keep table (the next stage,
`mulligan-profile.md`) removes the multiplier; this stage removes waste inside each game.

Inside the trial games (gi131 d3): 438 decision roots, the slowest 5% hold 75% of the units, and the
expensive roots are the **second main** of turns 5-6 (`FullSearchLineHybrid`, `pre=0`: 77% of units),
each a committed d3 pass running to 10-20x its 9,000-unit budget. 88% of units are `la_cand` (one unit
per candidate the lookahead scores), the post-apply duplicate rate is only 9.4%, and units/candidate
is ~1.0 -- a **branching** problem, not a per-node one.

### 1.2 Which axis multiplies -- `MTG_VAMP_CAND_CENSUS` (new, inert by default)

Every candidate charged at the `la_cand` site, classified by the Vampires axes it carries:

| gi131 d3 (before) | share | 2HG gi34 d3 | share | gi32 d5 | share |
|---|---|---|---|---|---|
| main 2, no Vampires axis | 32.4% | main 2, Pit Fighter **breakpoint variant** | 48.7% | main 2, Voldaren Estate Blood | 34.9% |
| **main 2, Condemned discard-only pump** | **24.8%** | main 2, Pit Fighter base plan | 18.7% | main 2, plain | 12.3% |
| main 1, Condemned pump | 24.5% | main 2, plain | 12.9% | main 1, Blood + Baron convoke | 9.6% |
| main 1, plain | 14.9% | main 1, plain | 6.5% | main 1, plain | 7.7% |
| main 2, Pit Fighter breakpoint variant | 1.7% | main 2, Blood | 4.2% | main 2, Pit Fighter bp variant | 7.2% |

`MTG_BRANCH_STATS` names the odometer drivers: Falkenrath Pit Fighter's activation family is one
mutually-exclusive group of up to **104** members (victim class x discard pin x madness answer) and owns
292M of gi131's 322M odometer positions -- but only 20% of its plans; Stromkirk Condemned (one variant
per distinct hand name) drives 59% of the plans. `MTG_VAMP_PLAN_DUMP` (new) prints the big lists.

## 2. The folds (all committed)

### 2.1 `MTG_VAMP_M2_PUMP_DISCARD_FOLD` -- subset rule 9, a DOMINANCE fold (commit `46f82d9e5`)

In the second main, Stromkirk Condemned's "Discard a card: Vampires you control get +1/+1 until end of
turn" pumps nothing (the combat damage step is over and the goldfish never blocks). An activation whose
discard is not CAST for madness does one thing: it moves card X from hand to graveyard.

*Proof (weak dominance).* P = the selection without the pump, P' = with it. They reach the same state but
for X's zone, the until-end-of-turn bonus, and Condemned's once-each-turn flag (reset at our untap).
From P every continuation of P' is still playable -- hold X, never use it -- so P is worth at least P'
unless one of those differences is READ:
- the bonus and the flag: nothing after combat reads power or the flag -- the deck vouches
  (`VampiresProvider::PostCombatPumpDiscardFoldOptIn`, checked card by card against the list: the only
  power readers are combat ones; Pit Fighter / Blood / steal / transform / Voldaren Estate read counts and
  types);
- the graveyard / the discard: only Bloodghast's graveyard landfall (exempt) and madness (a CAST answer
  is never folded; a decline is a plain discard) -- the deck vouches;
- the hand size, which the ENGINE checks exactly: a hellbent upkeep reader (Asylum Visitor) on our
  battlefield or in hand when the plan empties the hand (exempt); more than seven cards at cleanup
  (exempt: the pump then picks the discard instead of the cleanup's ranking); a draw outlet in the
  selection (exempt: its hand-entry breakpoint re-solves the phase, so the end hand is not countable).

Implemented as a subset rule in both walkers (`SubsetPostCombatPumpDiscardDominated`, beside rules 7/8),
so the twin without the pump is always enumerated. Off in human play and in the un-pruned oracle list.
A provider opt-in, so no other deck can reach it.

### 2.2 `MTG_VAMP_MADNESS_COLOR_FOLD` -- an IDENTITY fold (commit `6161e387c`)

A madness cast variant pins (cost index, convoke arm); `ResolvePendingMadness` resolves a pin whose option
the trigger cannot pay as the **decline**, so a cast variant no line can fund is the decline twice. The
emission gate was mana VALUE only (`madness_mv` rides `a.cost` as generic, so the subset walk's colour
check never sees the madness pips): with a lone Swamp it emitted "Condemned discards Falkenrath Gorger,
cast it for {R}" next to the decline at every node (gi131 T8: 16 of 83 plans). Now an option is dropped
when the turn's OPTIMISTIC pool -- `AvailableManaPool` plus whatever `OptimisticTurnMana` credits beyond it
(the land drop, Treasures, Lotus) as wild -- cannot pay its reduced cost together with the outlet's own.
Skipped when there is no ceiling. The provider's default is still chosen over the unfiltered options and
an unpayable default becomes the decline in the front slot, so the rollout leaf resolves as before.

### 2.3 `MTG_VAMP_OUTLET_NOT_GROWTH` -- sound (commit `6161e387c`)

`OptimisticTurnMana` declined to bound mana whenever ANY `sac_creature_outlet` was on the battlefield or
in hand (the Skirk Prospector rule). Falkenrath Pit Fighter is a DRAW outlet: it adds no mana to the
enumeration it is in (its activation runs after the plan's casts; a card it draws is cast only through the
hand-entry breakpoint, a new enumeration with its own ceiling). One Pit Fighter disarmed the ceiling for
the emission prune, Markov Baron's convoke arms and the madness gate. A discard-cost outlet that is not a
mana outlet is no longer growth.

### 2.4 Byte-identity / per-game evidence

Pooled A/B, ONE `mtg --batch` (1,200 games: three arms x d3 b10 x200 seed 7700001, d5 b20 x100 seed
7800001, 2HG d3 b10 x100 seed 7900001), `MTG_DUMP_WINS=1`:

| cell | off: units / core-ms | 2.2+2.3 only | all three | per-game win turns |
|---|---|---|---|---|
| d3 b10 | 13,096,839 / 864,784 | 13,023,192 (digest **identical**) | 10,324,818 (-21.2%) / 689,910 | 200/200 identical |
| d5 b20 | 12,156,294 / 832,714 | 12,140,014 (digest **identical**) | 8,628,821 (-29.0%) / 698,683 | 100/100 identical |
| 2HG d3 b10 | 15,728,890 / 1,119,511 | 15,740,883 (digest moved) | 13,233,236 (-15.9%) / 885,859 | 100/100 identical |

`mtg-test` 1558/1558 (new `test/unit/test_vampires_perf.cpp`: the colour fold on / off / with a red
source, the ceiling with Pit Fighter on the battlefield, rule 9 and each exemption, main 1 keeping every
pin while main 2 keeps only Bloodghast's).

**Large budget (the dominance fold's per-game claim).** d5 **b100** x60 (seed 8100001), all three folds
off vs on, in one batch: **byte-identical** (digest `18160d825e12e05e` both arms), 60/60 win turns
identical, units 13,521,573 -> 11,480,212 (-15.1%). Rule 9 removes no line the search ever chose.

## 3. What remains (after the folds)

gi131 d5 after: main 2 plain 62.8%, main 1 16.7%, main-1 Condemned 13.3%, Pit Fighter breakpoint
variants 4.9%. The remaining main-2 Condemned discard-only candidates are the exemptions (gi133:
Bloodghast 6,408, the hellbent Asylum Visitor case the rest).

**The 2HG games are untouched by these folds** and are now the deck's most expensive cell (2HG d3 b10
8.9 core-s/game in the pooled batch vs 3.4 / 7.0 for d3 / d5). Their cost is the breakpoint fan of
Falkenrath Pit Fighter's draw: every Pit Fighter base plan (victim x discard x madness) gets six wave-0
arms (rank 0, rank 1, EMPTY, uniform 0/1, CHAIN), and `MTG_DEDUP_CENSUS` on 2HG gi34 shows:

| arm | applied | reaches a state a sibling already reached |
|---|---|---|
| EMPTY | 130,741 | **130,741 (100%)** |
| CHAIN | 130,741 | 127,886 (97.8%) |
| rank | 261,482 | 171,231 (65%) |
| base plans | 452,609 | 13,814 (3%) |

45.5% of 2HG gi34's charged candidates are post-apply duplicates (443,776 of 975,677), almost all of
them breakpoint arms of their own base plan. Every Vampires measurement shows EMPTY at 100% and CHAIN at
98-100%. See questions Q2 and Q3.

The odometer WALK (CPU, not units) is still dominated by Pit Fighter's group: 199M positions for 145k
plans on gi131 d5 after the folds -- the units fell 3.4x and the wall 3.6x, so it has not become the
bottleneck yet, but it is the next per-node cost (a factorised victim x pin axis, or pruning pins the
subset can never realise, would remove it).

## 4. Open questions for the USER (work continued on the stated default)

- **Q1 -- rule 9 is a DOMINANCE fold, shipped DEFAULT ON.** It is sound by the proof in §2.1, and the
  deck half of that proof is an agent's reading of the list (no post-combat power reader; no graveyard
  or discard reader but Bloodghast and madness). It changes plans (digests move) with every measured win
  turn unchanged. [default: ON; =0 restores every pin]
- **Q2 -- the breakpoint CHAIN arm for Vampires** (`BpChainSlotOptIn`, the per-deck lever Burn /
  TreasureHunt / Equipment / Auras / Dragons already set to 0 on held-out A/Bs): 98-100% duplicate here.
  It is a lossy-in-principle settings lever (the 2.2% of chain applies past W are reachable only through
  the deferred waves), so it is measured and NOT adopted: see §5. [default: unchanged, 1 slot]
- **Q3 -- a generic identity collapse of the EMPTY arm** (100% duplicate of its own base plan in every
  Vampires measurement): a base plan resolves an un-carried breakpoint as EMPTY unless the base canon
  fires, so EMPTY@0 IS the base whenever the base's apply used no canon. A collapse keyed on that (and on
  the learned continuation-list length, which makes rank k >= len an EMPTY twin) would remove as many
  candidates as the CHAIN opt-out does (one EMPTY arm per breakpoint-opening base plan): by that
  measurement ~8% of 2HG gi34's units and ~2% of the 2HG cell. It is engine-wide breakpoint machinery
  (the w0collapse family, where the repo already records that the `ci < 0 -> EMPTY` identity is "not
  established" since MTG_BP_BASE_CANON=1) and it moves every deck's budgeted GT, so it is not built here.
  [default: not built; proposed with the numbers]
- **Q4 -- identical Voldaren Estates through the land drop** ("Land: Estate, Estate #58 makes Blood" vs
  "Estate #60 makes Blood", seen at gi32 T4): the canonical-prefix fold covers it only under
  `FoldSearchOdometerOptIn` (ledger question 33, which Vampires does not take). Measured in §5.

## 5. The candidate levers measured (none adopted)

One pooled batch (seeds as §2.4, all folds ON in every arm), per-game win turns via `MTG_DUMP_WINS`:

| arm | d3 b10 x200 | d5 b20 x100 | 2HG d3 b10 x100 | slow games (units) |
|---|---|---|---|---|
| shipped (folds on) | 10,324,818 | 8,628,821 | 13,233,236 | as §1 |
| `bp_chain_slot: 0` (Q2) | 10,160,716 (-1.6%, digest identical) | 8,536,248 (-1.1%, digest identical) | 12,941,434 (-2.2%, digest moved, 100/100 win turns identical) | 2HG gi34 1,471,327 -> 1,352,261 (-8.1%); others <= -1.5% |
| `MTG_FOLD_SEARCH_ODO=1` (Q4) | 10,306,599 (-0.18%, digest identical) | 8,616,526 (-0.14%, digest identical) | 13,182,781 (-0.38%, digest identical) | gi32 805,260 -> 773,273 (-4.0%); others ~0 |

Both are small. The CHAIN opt-out matters only on 2HG gi34, whose EMPTY and CHAIN arms are 13.4% of its
charged candidates EACH (130,741 of 975,677); most of them are applied and then skipped by the post-apply
dedup, so each costs one unit and an apply, not a rollout. The canonical-prefix fold is byte-identical on
all 400 games and nearly inert. Neither is adopted: the CHAIN opt-out is a per-deck quality-gated lever
(the five decks that took it did so on a two-cell held-out A/B at 800 games/arm/cell), and the fold is
ledger question 33's, gated on MTG_FOLD_VERIFY.

## 6. The suite's own Vampires rows (smoke + regression manifests), folds OFF vs ON

One pooled batch, both arms per row (`=0` on all three flags vs shipped), same binary
(`ee3dbd8f6`); per-game win turns via `MTG_DUMP_WINS`:

| row | units off -> on | per-game |
|---|---|---|
| smoke d3 s1001 x150 | 9,555,747 -> 7,573,679 (-20.7%) | 150/150 identical |
| smoke d5 s1001 x75 | 7,684,189 -> 6,499,949 (-15.4%) | 75/75 identical |
| smoke 2HG d3 s1001 x50 | 6,043,722 -> 5,120,596 (-15.3%) | 49 same, **1 faster** (gi12 5->4) |
| regression d3 s2002 x150 | 10,361,665 -> 8,523,343 (-17.7%) | 150/150 identical |
| regression d3 s3003 x150 | 13,517,272 -> 10,480,610 (-22.5%) | 150/150 identical |
| regression d5 s2002 x75 | 7,277,104 -> 6,128,367 (-15.8%) | 75/75 identical |
| regression d5 s3003 x75 | 12,260,904 -> 9,105,689 (-25.7%) | 75/75 identical |
| regression 2HG d3 s2002 x100 | 16,068,270 -> 13,808,654 (-14.1%) | 98 same, **2 faster** (gi25, gi79 5->4) |
| regression 2HG d5 s2002 x50 | 13,795,651 -> 12,145,683 (-12.0%) | 49 same, **1 faster** (gi25 5->4) |
| smoke d0 s1001 x1000 | (greedy) | 999 same, 1 faster (gi199 5->4) |
| regression d0 s2002 x1000 | (greedy) | 998 same, 1 faster (gi225), **1 slower (gi265 4->5)** |

All searched rows together: 96,564,524 -> 79,386,570 units (**-17.8%**), 0 slower games, 4 faster (all
2HG, where the freed budget is re-spent). Other decks are untouched by construction (the colour fold is
behind `deck_has_madness`, the growth fix behind a discard-cost sac outlet, rule 9 behind the provider
opt-in, the diagnostics inert); the regression tier's reference check shows the one pre-existing drift
(CritterLifegain/claude_s6_gi5, ledger question 27) and nothing else.

**The d0 slower game, root-caused** (regression d0 gi265, seed 2267): bisected to
`MTG_VAMP_OUTLET_NOT_GROWTH` alone. With Pit Fighter on the battlefield the ceiling is now armed, so the
emission prune drops the uncastable Bloodline Keeper from the T3 candidate list; the depth-0 greedy walk
then also finds "cast Stromkirk Condemned + Markov Baron, pump with the first Condemned discarding the
Keeper" (+2 damage now, a higher greedy value) and takes it -- which leaves the second Condemned no
fodder on T4 (19 damage -> 15, win T4 -> T5). The prune is sound (the Keeper was unpayable on every line);
what moved is the d0 runner's own myopic pick on a walk that now sees more of its subsets. Net over the
two d0 rows: 2 faster, 1 slower of 2,000.

**GT is NOT accepted here.** The committed Vampires smoke / regression GT predates the sweep fixes
(the ledger's "GT re-accept pending"), so a harness run against it reports every row FAIL for reasons
that are mostly not this change; the per-row off-vs-on table above is the attribution. The one
searched game slower than the committed GT (regression d3 s2002 gi76, 4->5) is identical with the folds
off -- it is HEAD's, not this change's.
