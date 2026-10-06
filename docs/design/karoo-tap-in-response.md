# Karoo tap-in-response + the USER's bounce order (MTG_BOUNCE_UNTAPPED_FIRST)

Status: **BUILT, default OFF, measured on the regression tier — adoption awaits the USER's sign-off**
(and, if wanted, a held-out `CASES=overnight` run). Nothing in ground truth moves with the lever off.

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

## Open for the USER

* Adopt (flip the default ON)? Train-tier evidence above; a held-out `CASES=overnight` run is the
  standing next step if you want one before flipping.
* Tie-break choice made here: among lands that both re-enter untapped and cost nothing this phase, a
  TAPPED one is returned before an untapped floatable one (keeps the untapped land for main 2). Your
  rule did not cover that case; say if you want it the other way.
* Forbidden Orchard (full rainbow) floats as `wild`, following the Reality Spasm tap-ahead; the
  "pools hold typed mana" doctrine (2026-09-09) would commit it to a colour instead. Kept consistent
  with the existing real-float rule rather than changing that rule here.
