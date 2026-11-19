# Accomplished Alchemist: the Staff-in-hand go-off (seed 12345681 T3) -- MTG_AA_LOOP_CAST_DRAW

Status (2026-11-19): code committed on branch `alch-t3-1119` (worktree `.claude/worktrees/alch-t3`),
**NOT YET BUILT** -- BOX.lock was held all night by the Sea Creatures mulligan generation. Nothing below
the "Verify" list has been run on the new code.

## The defect (USER 2026-11-19)

*"A T3 win is possible and we fail to reach it for reasons I cannot explain."* *"In the viewer it even
recognizes that there is a chance to combo off once I make certain moves, but can never realize it."*
*"For some reason the viewer is also really difficult to get to save that line. Combo Off keeps failing and
it doesn't even put the button up right away. I had to do a number of the pieces manually to finally get the
win."* Reference: `references/Accomplished_Alchemist/claude_s12345681_gi12345680.json` (win_turn 3, on
`origin/references-autosave`).

Seed 12345681, keep 7. T3 board after the land drop: 3 Forest (one with a blue Utopia Sprawl), Arbor Elf,
Accomplished Alchemist (not sick); hand Crab Umbra, Forest, Living Wish, Staff of Domination; Stroke of
Genius 17 cards down; opponent library 51 (lethal X = 52). Six mana from lands + Arbor re-tap:
Living Wish -> Brindle Boar (2), Boar (3), Umbra on the Alchemist (U), sacrifice the Boar (X = 4). Then
Umbra {2}{U} nets +1 per untap. The Staff ({3}) must be cast **out of the loop's float with the Alchemist
still untapped**: cast it off the Alchemist's own tap and 4 - 3 = 1 floats, the Umbra is unpayable, the
turn is dead. Two single Umbra untaps first (float 2) are the minimum. Then the Staff's {5},{T} draws (with
its {2},{T} gain raising X) dig to the Stroke, and Stroke X = 52 at the opponent.

Expressible through `--claude-play` today (old binary): keep, T1 Forest + Arbor, T2 Sprawl(U) + Alchemist,
T3 `land=Forest; Living Wish -> Boar`, Boar + Umbra on the Alchemist, sac, 5 x `Crab Umbra: untap`, then the
offered "Combo Off route -- COMBO OFF: wins this turn" -> win_turn 3.

## Root cause

ONE missing need, three symptoms.

1. `ComputeUntapLoopNeed` (src/core/SpellEffects.h) had no payoff for a draw engine in HAND. (d) "Staff draws
   to find" requires the Staff on the battlefield; (e) chains only drain / pump creatures. On the post-sac
   board (Staff in hand, no Stroke in hand) the need is `kLoopPayNone`, so
   `UntapLoopWorthOffering` is false and `AccomplishedAlchemistProvider::UntapActivationCounts` offers only
   the generic K = 1..min(3, affordable) = 1. A K-activation plan opens no continuation (only a CAST
   untapper keys kActUntapper), and activations trail casts within a plan, so "untap, untap, cast Staff"
   is inexpressible to the search. Hand-off from that board: win T4 at d5 b20, b200, b2000 and **b0**.
2. The rule table (`AccomplishedAlchemistProvider::ComboOffPossible`) skipped FIN-STAFF-DIG unless the Staff
   was on the battlefield or was the LOOP's own untapper (`!staff_bf && !L.staff -> continue`).
   `[aa-co-why]` on the post-sac board: `LOOP-UMBRA refused: x0=4 P0=0 c_steady=3`. So
   `AlchemistComboRouteOffer` (DecisionProviders.cpp, `if (!prov.ComboOffPossible(...)) return 0`) never ran
   the route walk there -- for the viewer's button OR the search's route candidate. The button appears
   only once the float reaches 5 (row LOOP-STAFF-HAND: Staff as untapper, its gain bootstrapping X to 5),
   i.e. after the human has done the first untaps by hand.
3. Even with the rule firing, the route walk (aaroute::Dfs) has no LOOP link without a need, so it takes the
   SETUP link "cast Staff" first and strands itself -- the same failure as the search.

A separate, smaller defect behind "Combo Off keeps failing": the plain "cast Staff of Domination" plan at
float U2 pays the {3} with U2 + an Alchemist tap that `BurstPendingActColour` (src/ai/ManaPayment.cpp) floats
as **WHITE** (prod[0]); the board's own Umbra is not a pending act pip of that plan, so W3 floats and the
Umbra's {2}{U} cannot be paid. The rule then offers an UNPROVEN "Combo Off route" (LIFE-GAINED+LOOP-UMBRA+
FIN-STAFF-DIG) and the click rolls back ("did not finish the game"). Not fixed here: it is a budgeted
payment-colour change for the whole deck and needs its own A/B.

## The fix (sound: adds one need, one variant, one kill-fold target, one rule row; removes nothing)

* `kLoopPayCastDraw` (SpellEffects.h): (f) in `ComputeUntapLoopNeed`, after (e) -- a hand card with
  `tap_draw_cost` + `self_untap_cost` and a payoff above the library's last card. `cost` = its cast only (what
  the loop funds; the loop stops on an untapped Alchemist), `chain` = cast + draws (what
  `UntapLoopWorthOffering`'s "board already funds it" test prices). A chained pass after (d)'s second pass.
* `UntapLoopKillAction` / `UntapLoopKillPlansFor` (TurnSolver.cpp): the kill fold casts the engine by name;
  the cast untapper's kActUntapper key re-opens site 9, where the need is (d) and the loop digs.
* Rule row `FIN-STAFF-HAND-DIG` (DecisionProviders.cpp): FIN-STAFF-DIG on a non-Staff loop with the Staff in
  hand, need + the Staff's cast, bank unbounded when the Staff can gain.
* `MTG_AA_LOOP_CAST_DRAW` (default ON, =0 restores the old behaviour) -- A/B lever only.
* Fixtures: `test/combo_off/aa_co_17_staff_in_hand_umbra_loop_seed12345681_t3.json` (button offered and
  verified), `test/scenarios/aa_staff_in_hand_umbra_loop_seed12345681_t3.json` (autonomous d5 b20 wins T3).
  Both FAIL on the pre-fix binary (verified: offered=0 rule says=0; win_turn 4).

## Verify (owed, in order)

1. `flock build/scratch/BOX.lock env -C .claude/worktrees/alch-t3 ./build.sh`; `build/Release/mtg-test`.
2. The two fixtures above; `bash test/combo_off_check.sh` (all 52 -- negatives must stay absent);
   `bash test/scenarios.sh` filtered to `aa_*`.
3. Repro: `mtg "decks/Accomplished Alchemist/Accomplished Alchemist.cod" --profile ... --seed 12345681
   --game-index 0 --games 1 --force-mulligan 0:` -> expect win T3 (was T4). Also the claude-play hand-off
   from the post-sac frame (`--choices 1,0,0,-1,1,1,0,-1,136,0,2,0 --choices-then-auto`).
4. It is a BUDGETED play change for the AccomplishedAlchemist deck (more loop variants and kActUntapLoopFunded
   re-plans on any board with a Staff in hand): held-out A/B on the deck, then smoke/regression GT accept.

## Second half (USER 2026-11-19): the SEARCH must find this line without the heuristic

USER: *"I would like to know why we can't find it at any budget. That seems like something we will need to
fix."* and *"As-in, we should improve the heuristic and the search itself."*

The fix above is the heuristic half: the provider now recognises the in-hand draw engine as a loop target.
It is not enough by itself. The line is unfindable at `--budget-ms 0` today because the provider's
restriction is the only route to a multi-untap count, so a loop target the provider does not anticipate is a
completeness hole at every budget (CLAUDE.md: "everything should be findable at unlimited or large
budgets"; providers may restrict/order, but a restriction that removes the only winning line is lossy).

Where the search side caps it (to confirm with a census before changing anything):
* The generic `DecisionProvider::UntapActivationCounts` offers K = 1..min(3, max_affordable), and
  `max_affordable` ignores the mana a self-funding loop refunds (an untapped Alchemist at X >= the untap
  cost). So on a self-funding loop the count axis is bounded by the PRE-loop pool, not by what the loop can
  pay; the provider's `kUntapLoopToNeed` is the only way past that bound.
* Whether the search can instead reach "untap, untap, cast the Staff from the float with the Alchemist still
  untapped, untap again" as a SEQUENCE of K=1 activations interleaved with the cast (site-9 continuation,
  `kActUntapper` re-plan, one-family-per-plan, dedup/fold rules) -- find which rule cuts it.

Target shape (budget-staged, never a gate): the provider's counts stay FIRST (cheap, high-yield wave); the
rest of the count axis / the interleaved sequence is admitted in a later wave, priced, so a small budget
spends on the provider's guess and a large budget reaches everything. Acceptance test: with
`MTG_AA_LOOP_CAST_DRAW=0` (heuristic half OFF), seed 12345681 gi 0 keeping 7 must win T3 at
`--depth 8 --budget-ms 0`, and the generic route must be covered by a unit test on a non-Staff-hand loop as
well. Then the usual: held-out A/B on the deck, smoke/regression accept. Scheduled after the Boars keep-table
adoption (2026-11-19).

### Census (2026-11-19, heuristic OFF: `MTG_AA_LOOP_CAST_DRAW=0`, post-sac hand-off, `--depth 8 --budget-ms 0`)

Instrumented with `MTG_FS_ROOT_DUMP=3 MTG_SITE9_TRACE=3` on the hand-off
(`--choices 1,0,0,-1,1,1,0,-1,136,0,2,0 --choices-then-auto`). The root scans exactly three plans:
`{Staff of Domination}` (+ its site-9 variants, whose continuation list is EMPTY: the Alchemist is tapped
and 1 floats), `{Crab Umbra(x1)}` with **no site-9 variant at all**, and the T4 line. Heuristic ON, the
same hand-off wins T3.

* **Count axis:** the generic `UntapActivationCounts` gives K = 1 only (`max_affordable` = 4 div 3 = 1;
  the refund is not counted). Not the decisive cut: any K ends the main phase with the refund floating.
* **The decisive cut is the site-9 DELTA RULE.** A plan applies its casts first and its activations in
  the trailing pass, so the mana an untap refunds is spendable by nothing in that plan. After
  `Umbra x1` the Alchemist is untapped again (kActLifeYield unchanged), kActUntapper is unchanged, and
  kActUntapLoopFunded exists only for a provider-recognised loop NEED -- so
  `PostEntryActivationPending` is false, no continuation opens, and the float dies with the phase. The
  only route to "untap, untap, cast the Staff off the float" was the provider's `kUntapLoopToNeed`.

### The search half (MTG_BP_UNTAP_FLOAT, default ON; heurarm BP_UNTAP_FLOAT)

* `kActUntapFloat` (TurnSolver.cpp `CollectActivationKeys`): mana FLOATING beside a live mana creature
  that an own activated untapper can aim at, valued by the float total. An untap that refunds mana
  presents a new value, so site 9 re-plans the rest of the main against the real float; each further
  refund re-opens it (MTG_SITE9_REOPEN). Generic: any untapper (Umbra / Staff / Galvanic / Witch), any
  mana creature, no provider need.
* `PlanOpensBreakpoint` marks site 9 for any plan holding an `ActivateUntap` aimed at an own mana creature
  (the old mark covered only the Alchemist's life-scaled X).
* Budget-staged, not a gate: the provider's counts and the one-deviation continuations come first; a
  line needing different continuations at several re-opens is a compound slot of the wave walker
  (MTG_BP_DEV_SEQ: wave = deviation count, priced), so a finite budget spends on the provider's guess
  first and an unlimited one reaches every sequence. The count axis is unchanged: K = n untaps is
  reachable as n K=1 re-opens.
* `MTG_AA_LOOP_CAST_DRAW` now reads through heurarm (slot AA_LOOP_CAST_DRAW) so both halves A/B in one
  pooled batch.
* Unit tests (test_accomplished_alchemist.cpp, "Untap refund float"): the key on a GenericProvider board
  (Alchemist X = 4 + Umbra, Kalonian Hydra in hand, no loop need), and the both-worlds line "untap once,
  then the continuation casts the 5-drop off the float" -- absent with the lever at 0.
