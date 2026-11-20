# Shadowborn: Culling the Weak is a sacrifice claim for Apostle-before-Ritual (`MTG_SB_APOSTLE_FODDER_CLAIMS`), 2026-11-19

> **RETIRED 2026-11-20 (USER ruling).** The USER: *"in the stone already out situation it may be simpler just to let the ritual go first if it is not a consumer"*, clarified *"'It may be simpler' (it referring to the simplicity of the implementation and correction logic) ... 'if it is not a consumer' referring to the ritual"*, then *"Let's just go with my rule."* Both Apostle-before-Ritual moves (`MTG_SB_APOSTLE_BEFORE_RITUAL_INTENT`, `MTG_SB_APOSTLE_FODDER_CLAIMS`) were deleted: with Thrumming Stone out a plain Ritual keeps its rank-1 slot and the Apostle goes just before the sacrifice spell that eats it. Both were play-neutral in their A/Bs; the rare shape they fixed (s30097662, s30114160) is expected to show as APPLY-DROP-ORDER in the mana-clean gate again.

Status: landed on `wip/shadowborn-order` (63924ec7f), DEFAULT ON. Play moves in 1 game in 5,000 per cell, no win turn
moves. GT not accepted (see "Tiers").

## Findings

Mana-clean gate `claude-queue-20261119g2` (src 7a6a381a32fc), Shadowborn Apostle gi 110000-114999: 44
APPLY-DROP-ORDER and 8 APPLY-DROP.

* **All 44 APPLY-DROP-ORDER are one decision**: s30114160 gi114160 T5 m2. The board is Lake of the Dead,
  Thrumming Stone, Swamp#42 and Swamp#43 (new), with no creature. The hand is Diabolic Intent, Culling the Weak,
  Dark Ritual, Shadowborn Apostle and Jetmir. Plans `{Intent + Culling + Ritual + Apostle (+ act:Apostle)}` drop
  `cast Diabolic Intent`.
* The 8 APPLY-DROP are four games (s30113869 T4, s30113981 T4, s30114695 T3, s30114703 T3) and a different
  mechanism (see "Not fixed").

## Mechanism (verified with the level-3 trace)

Both Intent and Culling have the SAC-ANY-CREATURE hand victim `trick_hand_target = Shadowborn Apostle`. The
realisation order is the USER's rank order (Dark Ritual 1, Culling 2, Apostle 4, Intent 10), after
`ShadowbornProvider::ArrangeCastOrder`. That function moves the Apostle just ahead of Culling, so the line is
**Ritual > Apostle > Culling > Intent**. The aggregate payability walk passes this order, so the payable-order
decider keeps it. (Its unlock-order domain needs a same-line haste, so it is not involved.)

With Thrumming Stone out, every cast ripples 4. Dark Ritual goes first, and its ripple reveals the top four and puts
every non-Ritual on the bottom. That buries the Apostles that the hand Apostle's ripple would otherwise have
chained into. The hand Apostle then reveals the next four and finds no Apostle. Culling eats the only body, and
Intent has no victim at cast (CR 601.2h). The engine ends with 8 {B} floating and Intent still in hand.

The oracle's paying orders all cast the **Apostle before the Ritual**. In that order the Apostle's ripple chain
supplies the bodies for both sacrifices and, in the `act` variant, the six for the activation.

**Why the existing rule missed it.** `MTG_SB_APOSTLE_BEFORE_RITUAL_INTENT`
(`docs/design/shadowborn-two-lake-oracle-pin-2026-11-16.md`) exists for exactly this shape. With Stone visible, it
moves one Apostle ahead of the first plain Ritual when the line pays in that order (`OrderPaysSequentially`, which
credits no ripple). But it triggered only on **two or more Diabolic Intents** (`tutor_to_hand &&
sac_additional_creature`). Culling the Weak makes the same claim on an Apostle body, so `{Intent, Culling}` counted
as one claim and the rule stayed off.

This is a hidden-information bet in both directions. Ritual first buries Apostles, and Apostle first buries
Rituals. The rule's stated reason is that two claims on Apostle bodies need ripple fodder before the claimants
consume it, and that reason does not depend on which card makes the claim.

## Change

`MTG_SB_APOSTLE_FODDER_CLAIMS` (heurarm `SB_APOSTLE_FODDER_CLAIMS`, `EnvOn(..., true)`, DEFAULT ON; `=0` counts
Intents only, as before). In `ShadowbornProvider::ArrangeCastOrder` (DecisionProviders.cpp), the claim count now
includes every `sac_additional_creature` cast. Everything else is unchanged:

* the rule fires only with a visible Stone and at least two claims;
* it makes one move, under the same exact-walk payability guard;
* it adds no searched order;
* it is ordering only, inside the deck's provider (the NO-GREEDY rule's permitted place).

Unit test: `SB order: Culling counts as a sacrifice claim for Apostle-before-Ritual`
(test/unit/test_shadowborn_mainphase.cpp) covers four cases. With the lever on, the order is
Apostle > Ritual > Culling > Intent. With it off, the order is Ritual > Apostle > Culling > Intent. One claim alone
does not move anything. With no Stone, nothing moves.

## Results

**Repros** (oracle level 2, new binary): s30114160 T5 APPLY-DROP-ORDER 44 -> 0, HIDDEN-INFO 4 -> 0. With the lever
`=0`, the same binary gives 44 / 4.

**Gate slice** (gi 110000-114999, the gate's own games, seed 30000000+gi, both arms pooled in one batch,
`MTG_MANA_ORACLE=2`):

| class | off | on |
|---|---:|---:|
| APPLY-DROP-ORDER | 44 | **0** |
| APPLY-DROP | 8 | 8 |
| HIDDEN-INFO | 7 | 3 |
| CONTINUATION-DIFFERS | 23 | 23 |
| DROP-COVERED | 713 | 713 |
| PHANTOM / DOMINATED-LAND | 19156 / 10883 | 19156 / 10883 |

The off arm reproduces the gate's counts exactly.

**Held-out A/B** (one pooled batch, seed 89710000, game_index 0, 5000 games per arm per cell, lever 0 vs 1):

| cell | changed digests | slower | faster | net | units | CPU (new/old) |
|---|---:|---:|---:|---:|---:|---:|
| shadowborn (play: d5 b20) | 1 | 0 | 0 | 0 | x1.000 | x1.039 / x1.010 |
| shadowborn_d3 (d3 b10) | 1 | 0 | 0 | 0 | x1.000 | x1.004 / x0.984 |
| shadowborn2hg_d3 (2HG, d3 b10) | 1 | 0 | 0 | 0 | x1.000 | x1.062 / x1.016 |

In every cell, the one changed game is gi 3049, which wins on turn 6 in both arms. No game is slower, so no d8 b0
recovery run is owed. Every cell nets 0. The lever reaches only `ShadowbornProvider`, so no other deck can move.

**CPU.** The two CPU figures per cell come from two batches:

* the first number is the batch above, with old jobs placed before new;
* the second is a control batch with the same jobs and new placed before old (identical digests and units).

Both arms play byte-identical games in 14,997 of 15,000 cases, and units are x1.000 in every cell, so the whole
spread is batch-position and contention noise. Pooled over both batches, new/old CPU is x1.021. The change's real
cost is at most one extra `OrderPaysSequentially` walk per arrangement. That walk runs only when a line holds a
visible Stone, a plain Ritual ahead of an Apostle, and two sacrifice claims, which is rare enough that it is not
measurable here.

## Tiers (Release, from the worktree, `MTG_PGO=0 bash test/regression.sh --both`, one pooled batch)

* Smoke: 231/231 PASS, configs changed 0.
* Scenarios: 584/584.
* Regression: 367/368. The one FAIL is `shadowborn_regression_d5_s2002` (exp 4.7600/0bf2c69e88c477dd, got
  4.7600/0bf71f1a62309132). One play-changed game at the same score: gi98, which wins on T6 in both arms. Slower 0,
  faster 0.

  Traced with the lever at 1 and at 0 (seed 2100, game index 98): T1-T5 main 1 are identical. In T5 main 2, both
  arms cast Dark Ritual, then Apostle (an 18-reveal ripple chain). The arms then differ:

  | lever | T5 main 2 continues | T6 draw |
  |---|---|---|
  | `=0` | Culling, Intent (tutors Lake), Lake | Abhorrent Overlord |
  | `=1` | Intent (tutors Lake), Lake, Culling | Swamp |

  Both arms activate the Apostle on T6 and win on T6. The rule did not move this game's Ritual: the realised line
  is still Ritual-first. What changed is the arrangement of some candidate plans, so the search picked a different
  but equally scoring line. The cast after the tutor's shuffle then changed the T6 draw.

GT is NOT accepted here (left to the parent).

## Not fixed: the 8 APPLY-DROP (post-tutor continuation payment)

Example: s30113869 T4 m1 `{Diabolic Intent}` and `{Intent + act:Apostle}`. The board has three Apostles, Swamp#41,
Lake of the Dead, and Lotus Bloom (new). The hand is Thrumming Stone, Intent and two Apostles.

* **Engine.** Intent tutors, and the continuation casts Thrumming Stone. The payer funds the Stone from the Lotus
  float, the Swamp and Lake's **plain** {B}. It ends with Swamp#41 and Lake tapped, so the continuation's two
  Apostle casts (`a lost action was not declared in the plan`) have no source left.
* **Oracle.** It **cracks** Lake (Swamp#41 tapped first, then sacrificed) for {B}{B}{B}{B}, and one Apostle
  starts the ripple chain.

The Lake crack is "on demand" (taken only when the cost cannot otherwise be paid). The Stone's cost can be paid
without it, and the Apostle casts that needed it come after the Intent's tutor breakpoint, where no payment rule
can see them. `MTG_PAY_ROLLBACK` (forced on) does not recover it. This is the parked class "need-later across the
Diabolic Intent tutor breakpoint" (`docs/design/mana-clean-pass-checkpoint.md`, Queue item D round 3). It would
need a SEARCHED early-crack decision at the continuation's first payment, and is not attempted here.

The oracle's paying realisation also feeds on the Apostle chain off the top of the library. s30114695 T3
`{Lake + Demonic Tutor}` and s30114703 T3 `{Lake + Intent}` are the same post-tutor shape: the engine cracks where
the oracle does not, leaving {B}{B}{B} floating and one Apostle dropped.

Repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Shadowborn Apostle/Shadowborn Apostle.cod'
--profile 'decks/Shadowborn Apostle/Shadowborn Apostle.profile.json' --games 1 --seed 30113869 --game-index 113869`.

## USER decision

Cast order is the USER's per deck. This change extends an existing default-ON ordering rule
(`MTG_SB_APOSTLE_BEFORE_RITUAL_INTENT`) to count Culling the Weak as a sacrifice claim. In the lines it touches,
that moves the Apostle ahead of the USER's rank-1 Dark Ritual. It ships DEFAULT ON. Recommendation: keep it.

* It removes the gate's whole APPLY-DROP-ORDER class for this deck.
* It changes 1 game in 15,000 A/B games, with no change in win turn.
* `=0` restores the Intents-only rule.
