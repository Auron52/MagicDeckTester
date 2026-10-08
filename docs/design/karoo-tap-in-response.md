# Karoo tap-in-response + the USER's bounce order (MTG_BOUNCE_UNTAPPED_FIRST)

Status: **ADOPTED 2026-10-08 -- `MTG_BOUNCE_UNTAPPED_FIRST` and `MTG_BOUNCE_SEARCH` default ON.** The
first held-out run found one game the ORDER half lost and no budget recovered (the bounce was not
searched); the USER asked for the bounce to be searched, and the second held-out run (lever + searched
bounce) clears the bar on every deck. Human play floats the returned land regardless (2026-10-08).

## What the user asked for

* 2026-10-06, Hinata viewer: a tapped Mystic Monastery was recommended as the Karoo's bounce over an
  untapped Forbidden Orchard. USER: *"Generally the Orchard is better to bounce because the monastery
  will come into play tapped again."*
* The ordering alone (the first `MTG_BOUNCE_UNTAPPED_FIRST`) moved 2 of 19,880 games, one a real
  regression: Hinata d0 s2002 gi522 7 -> 8 — a mid-turn Karoo returned an untapped Mountain whose mana
  Hinata still needed. USER: *"I recommend modelling tapping the land in response."*

## Rules

A Karoo's "return a land you control to its owner's hand" is a triggered ability (CR 603.2). Before it
resolves the controller holds priority and may activate mana abilities (CR 605.3a) — including the
mana ability of the land about to be returned. The mana stays in the pool until the step/phase ends
(CR 106.4). The land is chosen on resolution (no target), but tapping any *other* land in response
changes nothing, so "tap the land you will return" is the whole manoeuvre.

## What was built (one lever, both halves)

`KarooTapInResponseOn()` (`src/ai/EngineFlags.h`, heurarm slot `BOUNCE_UNTAPPED_FIRST`, env
`MTG_BOUNCE_UNTAPPED_FIRST`, default OFF). The old "order without the float" arm is gone; it was
measured as a regression.

1. **Tap in response** — `BounceKarooLand` (`src/core/SpellEffects.cpp`), the one land-drop ETB that
   the executor, the rollout and the enumeration probe share, calls `FloatKarooBouncedLand` on its
   FINAL pick (the heuristic's or the human's). An UNTAPPED land that `KarooBounceFloatable` accepts
   is tapped and its output booked into `state.floating_mana`. Not floatable: pain / life / energy /
   opponent-lifegain taps, filters and other conversion sources, pay-sac, storage, domain and scaled
   yields, spend-restricted mana (creature-only, big-spell-only, coloured-for-creatures), depletion
   lands (their sacrifice trigger would resolve first), animated lands that cannot tap, and any land on
   a Manabarbs-armed board. Colour = the shared real-float rule, now one helper
   (`FloatSourceTapCommitted`) also used by the Reality Spasm tap-ahead: one colour -> that colour, a
   Karoo bundle -> one of each, a choice of 2-4 -> need-aware commit from the hand's pips, full rainbow
   (Forbidden Orchard) -> wild (the established tap-ahead convention). No second Orchard Spirit: the
   per-turn model already assumes the Orchard is tapped every turn. A land Aura's extra mana rides the tap.
   **No planner credit is added anywhere**: the float is real `GameState`, and every later pool in the
   phase already reads `floating_mana`, so the planner sees exactly what the apply realised.
2. **The USER's order** — `DecisionProvider::BounceLandCandidates`: an untapped land that is
   floatable (the apply's own predicate) joins the tapped lands in the no-loss tier; then "re-enters
   untapped" decides; then a tapped land beats an untapped floatable one that replays equally well (its
   float dies at the end of the phase, the untapped land would still be there for main 2). With no
   untapped floatable land on the board this is the base order exactly.
3. **Viewer** — the bounce decision's note says an untapped land is tapped in response (lever on only);
   the human's pick gets the float too (verified on Hinata `--claude-play --seed 7`, choices
   `1,4,-1,-1,8,-1,-1,4,2,1`: returning the untapped Mountain shows `floating_mana {R:1}` next frame).

## Human play: the float is ON regardless of the lever (2026-10-08)

USER, Bruna seed 18 gi17 T3 (keep 7; T1 Remote Farm; T2 Forest, Avacyn's Pilgrim, Open the Armory ->
Colossification): the line `land=Azorius Chancery; cast: Wild Growth -> Azorius Chancery` was
REJECTED. *"The engine is rejecting this even though this is a perfectly valid way to play the line.
You just need to float the Green."*

**Root cause.** The line was never illegal to the validator: `--validate-line
"land=Azorius Chancery;cast=Wild Growth"` returned `choose` with three host variants, the Chancery one
included (plan 109 on the tip). The rejection came at EXECUTION. Both apply worlds defer a Karoo past
the cast loop and hold a land Aura that names it (`karoo_host_auras`), so the order was: play the
Chancery -> its ETB bounce prompt (default = the Forest, an untapped land that re-enters untapped;
the human picked it) -> `BounceKarooLand` -> `FloatKarooBouncedLand` was a no-op because
`MTG_BOUNCE_UNTAPPED_FIRST` is OFF -> the Forest left untapped -> the held Wild Growth had no {G}
source -> `dropped_casts: ["Wild Growth"]` -> the viewer rolled the line back ("not enough mana").

**The bounce choice.** CheckLine never sees it: the bounce is a resolution-time prompt answered after
the line is committed. Its menu match is bounce-agnostic (the enumerator offers the host variant), and
its trial-apply (`plan_pays`, advisory, used only to order payable variants first) runs with every
chooser nulled, so `BounceKarooLand` takes the provider's default pick (the Forest). The human's own
pick is applied only by the executor. On this board, returning Remote Farm instead would have left the
Forest to pay {G} even on the tip (verified: Wild Growth attaches to the Chancery); returning the Forest
-- the user's line, keeping Remote Farm's {W}{W} -- needs the float. With the float modelled, both picks
pay, so no `bounce=` line verb was added: the remaining case where the pick still matters is returning
an untapped land the model does not float (pain / depletion / filter / restricted mana), where the
prompt's answer is applied for real and a shortfall is reported as a dropped cast, which is accurate.

**Fix.** `HumanKarooFloatOn()` (`src/core/GameLogger.h`): in human play the float is modelled whatever
the lever says (`KarooFloatModelled()` = lever || human play, `SpellEffects.h`), for the executor, the
enumeration and CheckLine alike; the bounce note says so. The provider's ORDER (the lever's other
half) stays on the lever -- in human play the person picks the land. `HumanPlayActive()` is false in
the engine's clairvoyant rollouts and after a `--choices-then-auto` hand-back, so autonomous play is
byte-identical.

**Old recordings.** The reference writer stamps `"karoo_float": 1`; `viewer_protocol_check.py`'s
`recording_rule_args` passes `--legacy-karoo-float` for a reference without it (the
`combat_swap_timing` / `--legacy-main-swap` pattern); `MTG_HUMAN_KAROO_FLOAT=0` is the env twin.
`--legacy-karoo-float` reproduces the tip's seed-18 frame byte for byte (dropped Wild Growth).

Tests: `test/scenarios/bruna_human_karoo_float_wild_growth.json` (the user's T3 board through the
human path: the Forest is tapped for {G} in response, Wild Growth ends on the Chancery) and its control
`bruna_human_karoo_float_legacy.json` (the legacy arm: no float, Wild Growth not attached); the
scenario harness gained `depletion_counters`, `human_variants` and `expect_attached` for them.
`test/viewer_client_check.js` `testKarooFloatWildGrowth` plays the user's game through the real client
(keep, T1, T2 + Open the Armory, T3 Chancery + Wild Growth dragged onto it, return the Forest) and
requires no rollback and Wild Growth attached to the Chancery.

## Verification (lever OFF)

`mtg-test` 469/469 (6 new cases in `test/unit/test_karoo_tap_in_response.cpp`); scenarios 147/147;
smoke 118/118 byte-identical, 0 plays changed; the A/B's base arm is byte-identical to the committed
regression GT on all 59 Karoo-deck cells (outcome + play hash per game).

## A/B (regression tier, the 11 Karoo decks, both arms in one pooled batch, 19,880 paired games)

`test/karoo_tap_in_response_ab.sh`. gi522 with the lever on: T6 Hinata cast off the floated {R}, win T7
(= base).

| deck | games | faster | slower | sum Δturns | mean/game | searched: faster / slower / Δ |
|---|---|---|---|---|---|---|
| angels | 2250 | 0 | 0 | +0 | +0.0000 | 0 / 0 / +0 |
| bruna | 1450 | 65 | 17 | -54 | -0.0372 | 3 / 1 / -2 |
| creature_giving | 2100 | 15 | 1 | -14 | -0.0067 | 0 / 0 / +0 |
| dragons | 2100 | 102 | 3 | -104 | -0.0495 | 0 / 0 / +0 |
| fungus | 1600 | 58 | 5 | -57 | -0.0356 | 0 / 1 / +1 |
| hinata (+2hg) | 1750 | 51 | 21 | -46 | -0.0263 | 5 / 5 / +1 |
| kitty | 2100 | 13 | 9 | -4 | -0.0019 | 0 / 0 / +0 |
| melira | 1230 | 21 | 12 | -18 | -0.0146 | 0 / 0 / +0 |
| minotaur (+2hg) | 2250 | 29 | 3 | -30 | -0.0133 | 0 / 1 / +1 |
| mirrorwing | 1600 | 54 | 28 | -40 | -0.0250 | 0 / 1 / +2 |
| selesnya | 1450 | 51 | 19 | -35 | -0.0241 | 0 / 1 / +1 |
| **all** | 19880 | 459 | 118 | -402 | **-0.0202 ± 0.0016** | 8 / 10 / +4 |

Every deck's aggregate is <= 0. Work units are flat (0.9975 overall).

### Every slower game, root-caused (118)

All 118 were re-run with both arms at rising search (`logs/karoo/escalate.py`, the
`gt_line_playable.py --escalate-ab` ladder). **All 118 converge (lever ON <= OFF)**: 116 at the first
searched rung (d3/200ms), 2 at d5/500ms (hinata d3 s2002 gi73, hinata d3 s3003 gi157).

* **108 d0 games.** The depth-0 runner plays a Karoo LAND-FIRST (the karoo defer is a searched-drop
  feature), so its bounce takes an untapped land — which is exactly where the base arm threw that
  land's mana away and where most of the -402 comes from. With the float, the d0 greedy spends the
  mana on an extra cast/activation on the Karoo turn (77 games), the new order returns a different
  land (29), or the extra mana is spent later in the game (2), and in these games that turned out worse: e.g. Bruna gi212 (5 -> unwon): Glittering Wish
  for Lightning Greaves spent Remote Farm's last depletion counter, so Bruna was not castable on T4;
  Minotaur gi874 (5 -> 6): the float plus a Blood Crypt paid Slaughter-Priest of Mogis's {2} sac
  outlet and threw away Gnarled Scarhide for first strike (that activation does not appear in the
  game log's action list at all -- a pre-existing d0 logging gap, noted, not touched here). The mana is legal; the choice is the d0
  greedy's (a permitted greedy, outside the search window), and searched play recovers every one.
* **10 searched games** (d3/10ms, d5/20ms). The first play difference is on T1-T3, before either arm
  has played a Karoo: the lever changes how rollouts value future Karoo turns, which perturbs root
  choices at the tier's tiny budgets (a different cantrip / land / mana creature). Budget churn: all
  converge at d3/200ms or d5/500ms.

No slower game was traced to a float defect (wrong amount, wrong colour, phantom mana or a missed one).

## Held-out A/B (2026-10-08): overnight tier, the 9 Karoo decks, one pooled batch, 112,800 paired games

`DECKS=fungus,hinata,creature_giving,mirrorwing,minotaur,kitty,dragons,melira,selesnya CASES=overnight
bash test/karoo_tap_in_response_ab.sh` on a copied binary of `384d7dab` (232 jobs, both arms in one
`--batch`, 24/24 workers busy at every heartbeat). Angels is out (its Azorius Chancery was cut), Bruna is
not in the suite, fungusb / kittyv2 hold no Karoo.

| deck | games | faster | slower | sum Δturns | mean/game | searched: faster / slower / Δ | units |
|---|---|---|---|---|---|---|---|
| creature_giving | 14000 | 128 | 22 | -107 | -0.0076 ± 0.0010 | 4 / 2 / -2 | 0.9987 |
| dragons | 14000 | 638 | 34 | -665 | -0.0475 ± 0.0021 | 4 / 4 / +0 | 0.9944 |
| fungus | 10800 | 411 | 53 | -376 | -0.0348 ± 0.0021 | 3 / 2 / -1 | 1.0030 |
| hinata | 10800 | 416 | 140 | -428 | -0.0396 ± 0.0037 | 12 / 8 / -8 | 1.0022 |
| hinata2hg | 400 | 1 | 4 | +5 | +0.0125 ± 0.0109 | 1 / 4 / +5 | 0.9865 |
| kitty | 14000 | 151 | 47 | -110 | -0.0079 ± 0.0011 | 2 / 0 / -2 | 1.0037 |
| melira | 9400 | 204 | 75 | -177 | -0.0188 ± 0.0026 | 0 / 1 / +1 | 1.0005 |
| minotaur | 14000 | 252 | 23 | -235 | -0.0168 ± 0.0012 | 0 / 1 / +1 | 1.0028 |
| minotaur2hg | 600 | 0 | 0 | +0 | +0.0000 | 0 / 0 / +0 | 1.0036 |
| mirrorwing | 10800 | 444 | 159 | -396 | -0.0367 ± 0.0034 | 7 / 0 / -7 | 0.9954 |
| selesnya | 14000 | 362 | 138 | -259 | -0.0185 ± 0.0021 | 4 / 4 / +0 | 0.9994 |
| **all** | 112800 | 3007 | 695 | -2748 | **-0.0244 ± 0.0007** | 37 / 26 / -13 | 0.9998 |

Every deck's aggregate is <= 0 (Hinata with its 2HG rows: -423 over 11,200). The 2HG configuration
alone is +5 over 400 games -- its 4 slower games are all budget churn (below). Work units flat.

### Every searched slower game (26), escalated in one pooled batch per stage

Stage 0 re-ran each game as a chunked single-game job at the A/B's own settings (reproduced all 26:
the chunk mechanics are exact). Stage 1 = `--depth <base win turn> --budget-ms 100`, stage 2 = `--depth 8
--budget-ms 0`, both arms.

* **23 converge at stage 1** (lever <= base): creature_giving d3 s5005 gi171, d3 s7007 gi681; dragons d3
  s4004 gi577, d3 s5005 gi382; fungus d5 s4004 gi106, d3 s4004 gi106; hinata2hg d5 s4004 gi5 (5 -> 8 in
  the A/B), gi37, d5 s7007 gi40, gi54; hinata d3 s4004 gi222, gi335, d3 s5005 gi104, d3 s6006 gi30, gi42,
  gi314, d5 s7007 gi253; melira d3 s6006 gi74; minotaur d3 s7007 gi155; selesnya d3 s4004 gi560, gi609,
  d3 s5005 gi340, d3 s6006 gi752. Budget churn at the tier's 10-40 ms.
* **1 converges at stage 2**: hinata d3 s5005 gi68 (6 = 6 at d8/unlimited). Its first play difference is
  on T1 (Sol Ring vs Ponder), before any Karoo is played -- the lever changes how the rollouts value
  future Karoo turns, and at d3/10 ms that perturbs the root choice.
* **1 game PERSISTS at d8/unlimited -- a real loss from the ORDER half**: Dragons seed 4004 gi47 (both
  its d3 and d5 cells, 5 -> 6; at d8/0 still 5 -> 6). T4: Lightning Greaves, then Gruul Turf. Every land
  is untapped when the bounce resolves (Greaves was paid by Sol Ring; the Mountains are kept for Scourge
  of Valkas' firebreathing). Base order: a tie, index order returns Haven of the Spirit Dragon. Lever
  order: the Mountain is FLOATABLE, so it takes the no-loss credit (+100) over Haven (restricted mana,
  not floatable) and is returned -- but its floated {R} dies at the end of main 1 (CR 106.4) with
  nothing left to cast, and in combat Scourge firebreathes once instead of twice: 5 damage, not 6. T5
  leaves the opponent at 1; the win slips to T6. No search setting recovers it because the bounce pick
  is NOT SEARCHED: `BounceKarooLand` takes `BounceLandCandidates(...).front()`.

The d0 slower games (669) were spot-checked, not each root-caused (regression skill: d0 is light-touch);
e.g. creature_giving d0 s6006 gi1449 (5 -> unwon): T2 land-first Chancery returns the only land, a
Forbidden Orchard; with the float the d0 greedy spends it on Crop Rotation (sacrificing the Chancery for
an Orchard) and loses the T3 double-Crop-Rotation line -- legal mana, the permitted d0 greedy's choice.

### Decision after run #1: NOT flipped (superseded below)

Every deck nets <= 0 and the overall gain is -0.0244 turns/game, but CLAUDE.md's adoption bar also asks
that every slower game recover at `--depth 8 --budget-ms 0`, and Dragons s4004 gi47 does not. The
loss is the ORDER half, not the float: the no-loss credit treats a floatable untapped land as free to
return, but the float only lives until the end of the PHASE -- and in searched play the Karoo is played
AFTER the main-phase casts (`karoo_deferred`), so the float usually has no consumer while the land's
mana for combat / main 2 is gone.

**Proposed amendment (not built):** make the Karoo bounce a SEARCHED dimension -- a plan field (like
`fetch_target`) that the enumerator fans over the distinct `BounceLandCandidates` and both apply worlds
honour, the provider's order becoming the ordering and the d0 / beyond-horizon default. That is also
what the no-greedy rule asks for: `ranked.front()` inside the search window is a provider top pick
taken without branching. With it the search can return the Haven in gi47. A cheaper alternative is an
order-only tweak (no-loss credit for a floatable untapped land only when the float has a consumer this
phase -- a land-first d0 play, or a land Aura held for the Karoo), but it re-opens a heuristic the
search should own.

## The amendment: the Karoo bounce is SEARCHED (MTG_BOUNCE_SEARCH, 2026-10-08)

USER, on the proposal above: *"That makes sense. The Karoo decision should also be searched with
heuristics. However, we may want to heuristic it more often than not, since the decision is usually an
easy one."*

**What was built.** `Plan::bounce_choice` (-1 = the provider's front pick; k >= 1 = candidate k of
`DecisionProvider::BounceSearchCandidates` evaluated on the state AT THE BOUNCE, clamped to the last).
It is the existing resolution-time, index-bound axis machinery (the scry / tutor-resolve / sac-land
shape): `AppendSubdecisionAxes` emits one variant per further candidate of every base plan whose drop
is a Karoo (last of the axes, base plans only, so the cost is additive); `ScriptedBounceChoice` pins it
around the Karoo play in BOTH worlds (ApplyPlanDirect's deferred and non-deferred plays,
`CombatSwapStaysPayable`'s probe, AIEngine::TakeTurn's deferred and non-deferred plays) and for a
breakpoint continuation's own Karoo drop (`ContPins::bounce`, replayed by the executor like the scry
pin); `BounceKarooLand` consumes it. The axis is registered in the plan fingerprint, `SamePlan`,
`IsApplyEmptyPlan`, `PlanIsAxisVariant` (so an inert variant is dropped by the post-apply state dedup
for one apply), both PlanDom keys and the plan summary (`[bounce k]`); `kPlanDomAssertedSize` 400 ->
408. The d0 runner and the playout beyond the horizon carry no pin and keep the front pick -- the only
permitted greedy. Human play never fans it: the person answers the bounce prompt, whose default
(`heuristic_default`) is the provider's top pick. `MTG_UNPRUNE=karoobounce` opens the whole list.

**The narrowing rules** (`DecisionProvider::BounceSearchCandidates`, on the state at the bounce, in the
provider's order so element 0 is always its front):

1. *Exclusions* -- another Karoo (its replay bounces again) or a land carrying an Aura (the Aura dies)
   is never offered while any other land is returnable. These are the order's own -1000 / -500 tiers.
2. *Tapped clean land* -- a TAPPED land that re-enters UNTAPPED costs nothing (no mana this turn, a
   clean replay), so it dominates every untapped land and every land that re-enters tapped: the
   candidates are the tapped clean lands alone, plus a tapped DEPLETION land whose replay would refresh
   its counters (a real alternative, not dominated).
3. *Dominated* -- otherwise, an UNTAPPED land that also re-enters tapped is dominated by a tapped one
   (the same lost tempo next turn plus its mana this turn).
4. *Identical fold* -- same name, same tapped state, same counters.
5. *Replay fold* -- with no other land in hand, TAPPED lands of one re-entry class fold: the returned
   land is next turn's drop, so which colour sits in hand for the rest of a spent turn is moot.
   UNTAPPED lands never fold by colour -- their mana is still live (combat, main 2), which is exactly
   the contested case (Dragons s4004 gi47: the floatable Mountain vs Haven of the Spirit Dragon).
6. At most 3 candidates (`kBounceSearchWidth`).

**Sizing.** Which lands are spent is known only to the apply, so the enumeration's width is an UPPER
BOUND: exact for a plan that spends no mana (the bounce sees the node's board); for a plan that MUST
spend a clean land (one is already spent, or its mana exceeds everything else the board makes and
nothing in it makes mana) rule 2 bounds it at one (plus a depletion land) when no land can be in hand
by then, else one per clean name; otherwise the distinct returnable names. Never below the truth, so
no candidate is lost. The first cut (distinct names only) clamped 78% of its pins; this one ~43%, and an
inert pin costs one apply, not a rollout.

Tests: `test/unit/test_karoo_bounce_search.cpp` (each rule, the Dragons gi47 board -- two candidates,
the pin returns Haven, a pin past the set clamps, the axis off ignores the pin).

### The candidate-count distribution (MTG_BOUNCE_STATS, the held-out lever arm, searched sites only)

Counted where the width is a decision -- a SEARCHED plan's Karoo play, in the real game and inside the
search; the d0 runner's and the beyond-horizon playout's greedy land plays are not counted.

| where | width 1 | width 2 | width 3 |
|---|---|---|---|
| real game (13,648 bounces) | **12,601 (92.3%)** | 802 (5.9%) | 245 (1.8%) |
| inside the search (41.1M) | 29.26M (71.2%) | 6.21M (15.1%) | 5.60M (13.6%) |

Why the real game's one-candidate bounces were one (12,601): only one legal land 5,156; replay fold
3,681; a spent clean land 2,317; identical copies / the Karoo-Aura exclusions 1,443; dominated 4. The
search sees more contested boards than the real game plays (it scores the lines that keep lands
untapped, which the real game mostly does not choose). The enumeration emitted 10.9M one-wide,
5.4M two-wide and 3.6M three-wide fans; 7.18M of its 13.57M pinned bounces (53%) clamped onto a
sibling's state (one apply each). Work units +1.5% overall (+0.3% to +3.1% per deck).

### Dragons s4004 gi47 recovers

Both cells, at their own settings (d3/10 ms and the d5 cell's value_play at 40 ms): 5, as the base
arm. The fan offers {Mountain, Haven of the Spirit Dragon}; the search returns Haven, Scourge of Valkas
firebreathes twice, the T5 kill stands. The searched bounce changed 23 of the 112,800 lever-arm games
against the lever-only run.

### Ground truth: all three tiers re-run and ACCEPTED (rebased onto the SelesnyaLifegain keep table)

On the adopted binary (both defaults ON, PGO+LTO), rebased onto `60db0d8c` (the SelesnyaLifegain keep
table, the Prevent Damage pain deferral, the Soldiers value leaf, the CheckLine real-payment fix), full
tiers vs the committed GT -- only the 9 Karoo decks move, every other cell is byte-identical:

| tier | PASS / FAIL | Karoo decks, sum Δturns | searched faster / slower | d0 to-unwon / from-unwon |
|---|---|---|---|---|
| smoke | 84 / 30 | -334 (every deck <= 0) | 4 / 1 | 17 / 30 |
| regression | 122 / 38 | -348 (every deck <= 0) | 5 / 11 | 13 / 30 |
| overnight (full, PGO) | 276 / 109 | -2692 (every deck <= 0; Hinata 2HG rows +6) | 32 / 31 | 99 / 204 |

All 43 searched slower games, re-run on the rebased binary in one pooled batch: 42 converge at
stage 1, hinata overnight d3 s5005 gi68 at stage 2; none persists. Every to-unwon game is a d0 cell
(the permitted greedy, light touch per the regression skill; spot-checked: creature_giving d0 s6006
gi1449 spends the floated Orchard mana on a Crop Rotation that sacrifices the Chancery). Accepted with
`--accept-with-regressions` notes per tier (the SelesnyaLifegain notes carried forward);
`test/check_gt_logs.py` 659 consistent. Viewer protocol `--strict`: 556 refs, 0 play-drift / 0
board-diverged / 0 enum-gap. `test/viewer_client_check.js` all green.

### Held-out A/B #2: lever + searched bounce vs the tip, 112,800 paired games

Same apparatus as #1 (overnight tier, the 9 Karoo decks, one pooled batch, a copied binary,
24/24 workers busy), on the tree before the SelesnyaLifegain keep table landed. The base arm is
byte-identical to run #1's tip on all 116 cells. The rebased overnight tier (above) reproduces the lever
arm game for game on every deck but SelesnyaLifegain, whose keep table changed its hands (there: -216,
searched 2 / 2).

| deck | games | faster | slower | sum Δturns | mean/game | searched: faster / slower / Δ | units |
|---|---|---|---|---|---|---|---|
| creature_giving | 14000 | 128 | 23 | -106 | -0.0076 ± 0.0010 | 4 / 3 / -1 | 1.0076 |
| dragons | 14000 | 636 | 31 | -666 | -0.0476 ± 0.0021 | 2 / 1 / -1 | 1.0042 |
| fungus | 10800 | 411 | 53 | -376 | -0.0348 ± 0.0021 | 3 / 2 / -1 | 1.0030 |
| hinata | 10800 | 415 | 146 | -418 | -0.0387 ± 0.0038 | 11 / 14 / +2 | 1.0203 |
| hinata2hg | 400 | 2 | 6 | +6 | +0.0150 ± 0.0117 | 2 / 6 / +6 | 1.0137 |
| kitty | 14000 | 151 | 47 | -110 | -0.0079 ± 0.0011 | 2 / 0 / -2 | 1.0037 |
| melira | 9400 | 204 | 76 | -176 | -0.0187 ± 0.0026 | 0 / 2 / +2 | 1.0107 |
| minotaur | 14000 | 252 | 23 | -235 | -0.0168 ± 0.0012 | 0 / 1 / +1 | 1.0168 |
| minotaur2hg | 600 | 0 | 0 | +0 | +0.0000 | 0 / 0 / +0 | 1.0314 |
| mirrorwing | 10800 | 443 | 159 | -395 | -0.0367 ± 0.0034 | 6 / 0 / -6 | 1.0100 |
| selesnya | 14000 | 364 | 139 | -260 | -0.0186 ± 0.0021 | 6 / 5 / -1 | 1.0187 |
| **all** | 112800 | 3006 | 703 | -2736 | **-0.0243 ± 0.0007** | 36 / 34 / -1 | 1.0153 |

Every deck nets <= 0 (Hinata with its 2HG rows: -412 over 11,200; the 2HG rows alone are +6 over 400,
all six slower games budget churn, below).

**All 34 searched slower games recover in the two stages** (re-run as chunked single-game jobs, both
arms; stage 0 = the A/B's own settings): 32 at stage 1 (depth = win turn, 100 ms) and 2 at stage 2
(depth 8, unlimited): hinata d3 s5005 gi68 and selesnya d3 s4004 gi609. One of them -- dragons d3
s4004 gi577, 4 -> 5 in the batch -- does not even reproduce as slower in isolation (4 = 4 at every
stage, including its own settings): a known batch-position susceptibility
(docs/design/batch-run-to-run-nondeterminism.md), not the change. Every one reads `on <= off` at its
converging stage; the 2HG six are hinata2hg d5 s4004 gi5 (5 -> 8 in the batch, 5 = 5 at stage 1), gi37,
d5 s5005 gi16, d5 s6006 gi99, d5 s7007 gi40, gi54.

### Decision: ADOPTED (2026-10-08)

The bar is cleared -- every deck net <= 0 on held-out, every searched slower game recovers in two
stages -- so `MTG_BOUNCE_UNTAPPED_FIRST` and `MTG_BOUNCE_SEARCH` both default ON (the USER asked for this
model on 2026-10-06 and for the searched bounce today). `=0` on either restores the old behaviour.
In HUMAN play the replay gate for old recordings (`--legacy-karoo-float`, no `karoo_float` stamp) now
also turns the lever's order off, so a reference recorded before today replays with the default it was
played under.

## Open for the USER

* ~~Adopt?~~ Adopted with the searched bounce (above).
* The Hinata 2HG rows alone net +6 over 400 held-out games (all budget churn); Hinata as a deck nets
  -412. Counted as one deck here, as run #1 did -- say if 2HG should be judged as its own deck.
* The sizing still over-emits: 53% of pinned bounces clamp onto a sibling (one apply each, +1.5% work
  units). A tighter bound needs the payer's tapped set before the apply; left as is.
* Tie-break choice made here: among lands that both re-enter untapped and cost nothing this phase, a
  TAPPED one is returned before an untapped floatable one (keeps the untapped land for main 2). Your
  rule did not cover that case; say if you want it the other way.
* Forbidden Orchard (full rainbow) floats as `wild`, following the Reality Spasm tap-ahead; the
  "pools hold typed mana" doctrine (2026-09-09) would commit it to a colour instead. Kept consistent
  with the existing real-float rule rather than changing that rule here.
