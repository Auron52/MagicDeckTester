# Accomplished Alchemist untap loop — design (AccomplishedAlchemist deck)

Status: IMPLEMENTATION IN PROGRESS (2026-10-19) -- chunks A, B, C1, C2a, C2b, P, E1 landed; Stage 5 verification run (E1 = its engine fixes). 2026-10-20: COMBO OFF (rule table + the mechanical untap-loop route + the search query) -- see the ledger. Per-deck ledger: `analysis-AccomplishedAlchemist.md`.
Card drafts (research fan-out, gitignored): `logs/aa_drafts/*.md|json` in the worktree.

## What the engine could not do (verified 2026-10-18, file:line at fork 2176032d)

1. **Lifegain → Alchemist in one main.** The plan is priced from the pre-plan pool, where
   `ScaledDorkCount` credits the Alchemist `max(1, life_gained_this_turn)` (SpellEffects.h:18800);
   subset feasibility and `chosen_x` are fixed at enumeration. Lifegain is NOT a breakpoint
   (`BpSiteMask` TurnSolver.cpp:21214, `PlanOpensBreakpoint` :56255; site 9 = "new activatable ability").
   Sac-outlet / tap-lifegain activations are TRAILING (after casts). So the line happens only by accident.
2. **Repeatable untaps.** Wirewood Lodge's `UntapCreature` (TurnSolver.h:186; SpellEffects.h:13303/13342)
   taps its source and auto-targets; `UntapBurstBestYield` (SpellEffects.h:18881) needs a 1-colour dork.
3. **Second main is OFF** for every Alchemist deck (`MTG_SL_SECOND_MAIN` default OFF since 2026-09-30,
   GoldFishRunner.cpp:234, measured on SelesnyaLifegain). The Alchemist / Feed the Clan notes and
   HeuristicArm.h:280 still say "default ON" — stale, to be corrected.
4. **Stroke of Genius cannot be cast.** Hand-cast `{X}` DrawX is skipped in enumeration
   (TurnSolver.cpp:26239; the X block :26561 is DirectDamage only); the rollout has no DrawX branch;
   `ResolveDrawX` (EffectHandler.cpp:921) always draws for the caster and never flags a self deck-out.
   A forced opponent draw from an empty library is not a loss anywhere (`opponent_decked` is set only by
   `EndOfTurnDraw` and `TakeFromTop`, OpponentDeck.h). `DeckTouchesOpponentZones` (GoldFishRunner.cpp:524)
   keys only on `exile_opponent_top_cost`.

## Real costs (Scryfall) and loop thresholds

Net mana per iteration = X − untap cost (X = life gained this turn; all X mana is ONE colour).

| untapper | cost per untap | unbounded when | notes |
|---|---|---|---|
| Crab Umbra (aura, {U}) | {2}{U} | X ≥ 4 | no {T}; target = enchanted creature |
| Galvanic Alchemist (soulbond, {2}{U}) | {2}{U} (granted to both halves) | X ≥ 4 | |
| Seedcradle Witch ({G/W}) | {2}{G}{W} | X ≥ 5 | + target gets +3/+3; needs TWO colours |
| Staff of Domination ({3}) | {3},{T} untap creature + {1} untap Staff | X ≥ 5 | {2},{T} gain 1 life grows X |

Staff bootstrap (other mana P, g = 5 − X): P ≥ 3g − 1 and P + X − 2g ≥ 3 → minimum P = 2,5,8,11,14 for
X = 4,3,2,1,0. Use an exact small computation over (pool, X, Staff tapped, Alchemist tapped), not a formula.

Kill costs once unbounded: Stroke at opponent with library L: X = L + 1 → L + 4 mana incl. {U}.
Scholar of Athreos (Living Wish {1}{G} + {2}{W}) then {2}{B} per opponent life. Ezuri {2}{G}{G}{G} per
+3/+3 on attacking non-sick Elves. Staff draw {5}+{1} per card.

## Why loop SIZING is a closed form, not a greedy pick

Each iteration returns the board to the same state plus mana (and, for Staff, life); unspent mana
empties at end of phase (no mana burn); the opponent is passive. So every K ≥ the need is outcome-
equivalent and K = exact need is a sound dominance fold. WHICH payoff, and whether to loop at all, stay
SEARCHED (one Action variant per payoff K). Ceiling = safety cap only, never a lever (completeness rule).

## Implementation plan (serial chunks)

* **Chunk A — straightforward cards.** Nourish, Eternal Witness, Elvish Harbinger (Tier 1); Ezuri fair
  pump (team_pump_*), Scholar (`drain_each_opponent`), Brindle Boar (`sac_outlet_lifegain`), Weather the
  Storm (`storm` + ApplyCastLifegain copies + ProjectPlanWalk counter); stale "m2 default ON" notes fixed.
* **Chunk B — Stroke of Genius.** `draw_target_player`; DrawX enumeration with X candidates and a searched
  self/opponent target; rollout DrawX branch; `opponentdeck::ForcedDraw` (deck-out when n > library, and
  when the library empties — TakeFromTop reasoning); self over-draw sets `player_lost_on_draw`;
  `DeckTouchesOpponentZones`; self-draw breakpoint class.
  **LANDED 2026-10-18** (see analysis-AccomplishedAlchemist.md "Chunk B"). For chunk C: the enumerator
  sizes Stroke's X from the PRE-PLAN flat pool, so X funded by mana the same plan creates (the loop's
  untaps, an Alchemist yield raised by a same-main lifegain) is not offered -- the loop's resolution must
  either size the finisher itself (the SpendSurplusOnExile shape) or re-solve after the mana exists.
* **Chunk C — untap primitives + loop + provider.** Crab Umbra (`untap_enchanted_cost`), Galvanic
  (`soulbond_grants_untap_self_cost`; partner = searched), Staff (`self_untap_cost`, `tap_lifegain_flat`,
  untap-any-creature, `tap_draw_cost`; tap-target ability = inert deferral), Seedcradle Witch
  (targeted untap+pump, searched target). Shared `CreatureUntapAbilityCost` oracle. `RecogniseUntapLoop` /
  `ApplyUntapLoop` (beside `ApplyBlinkLoop`, one helper for executor + rollout), `Action::Kind::UntapLoop`
  with `chosen_x` = iterations, float credited like `ritual_float`. Lifegain breakpoint (new site) when
  `life_gained_this_turn` rises with a tappable Alchemist out. Deck provider `AccomplishedAlchemistProvider`
  (derived from what the deck routes to today) with `UntapLoopActivationCounts` (exact K per reachable
  payoff), TutorSearchWidth ≥ 5, certificate answered. Trufflesnout searched ETB mode; Utopia Sprawl
  Enchant-Forest + locked colour as a SEARCHED axis.
  **C1 LANDED 2026-10-18** (provider + Utopia Sprawl + Trufflesnout; see the ledger's "Chunk C1"). The
  provider exists (`AccomplishedAlchemistProvider : SelesnyaLifegainProvider`, signature
  draw_target_player | storm) -- C2 adds the untappers' params to its signature and the loop hooks. The
  ETB CHOICE AXIS (CollectActions post-pass, chosen_x pin down the etb_kx channel) is the shape for any
  further searched as-enters choice. Trufflesnout's life is a NEW param (etb_modal_lifegain) that the
  lifegain projections (ProjectPlanWalk, PlanTraits, the loop sizer) do not read yet -- C2 must.
  **C2a LANDED 2026-10-18** (the four untappers as ActivateUntap + one shared resolver ApplyActivatedUntap,
  Galvanic's searched partner, the Staff's {1} interleave, the LIFEGAIN breakpoint as a site-9 route
  (kActLifeYield), viewer verbs untapact= / tapdraw=). The closed-form loop + payoff sizing is C2b; its
  hand-off list is in the ledger's "Chunk C2a" section.
  **C2b LANDED 2026-10-18** (the closed-form loop + its payoffs; details in the ledger's "Chunk C2b"):
  `ActivateUntap` with `chosen_x = kUntapLoopToNeed` (no new Action kind -- every ActivateUntap site already
  handles it) resolved by ONE shared `ApplyUntapLoop` behind `ApplyUntapAction` (rollout + executor), driving
  only the C2a primitives. It stops when the FLOAT pays the cheapest this-turn kill exactly
  (`ComputeUntapLoopNeed` + `UntapLoopFloatPays`); the Staff bootstrap is an exact BFS (`UntapLoopBootOp`);
  tap colour per iteration = what is still owed (`UntapPreTapColour`). The payoff is cast by the site-9
  continuation the loop opens (`kActUntapLoopFunded`), folded to the proven kill (`MTG_UNTAP_LOOP_KILL_FOLD`).
  Viewer: `untaploop=`.
* **Chunk D — viewer.** Verbs for untap activations / loop, etb_mode, colour; DECISIONS.md rows;
  audit_viewer_decisions manifest.

## Open questions for the USER (defaults taken, see ledger)

1. Second main for THIS deck (default taken: global default stays OFF; measure an m2 arm and report).
2. Loop end state: leave Alchemist untapped to attack (+3 mana) — default: searched as a variant only
   when it changes the kill.
3. Staff draw: draw-to-find (EDF precedent) by default.
4. Proven loop stamps infinite life (Melira-style tiebreak)? default: no until measured.
5. Weather the Storm cast order (storm wants late, Alchemist wants life early) — USER-owned; generic
   order kept, storm-last proposed.

## What C2b chose, and why (2026-10-18)

* **Funding: resolve the loop, then a BREAKPOINT -- not a ritual_float credit.** A credit would have the
  enumerator price casts against mana the loop has not made yet (the loop's yield depends on the X the plan's
  own casts reach, and on colours). Instead the loop is a trailing action; when it stops, the float pays the
  need, which presents a NEW site-9 key (`kActUntapLoopFunded`, the need's mana value in bits 40+), and the
  continuation enumerates against the REAL float. Enumerate-optimistic only where it is safe: the loop's
  recognition projects the hand's lifegain onto X (autonomous only), and a loop on an untapped mana creature
  is priced at zero plan mana (its first activation is paid by its own first tap); the resolver validates.
* **Need = the cheapest this-turn kill** (every win this turn scores the same), so ONE loop variant per
  (untapper, target) instead of one per payoff -- a sound fold. Payoffs the ONE continuation can finish:
  Stroke of Genius from hand at the opponent (minimal lethal X), a drain source on the battlefield (Scholar,
  lethal count -- provider `ManaSinkActivationCounts`), Ezuri's main-phase pump to lethal (new
  `MainPhaseTeamPumpCounts` hook, float-gated; mode 2 now adds toughness), the Witch's own +3/+3 on an
  attacking target, and the Staff's draws TO FIND a Stroke (done INSIDE the loop -- a drawn card opens no
  re-plan of its own -- then the loop funds the Stroke).
* **NOT reachable this turn, by the engine's structure (recorded, not a deferral of a card):** Scholar of
  Athreos from hand or via Living Wish, Ezuri from hand. Each needs a SECOND post-entry re-plan after its own
  cast, and site 9 opens once per apply (the first-occurrence rule). Wishing / casting it this turn and
  looping next turn is still searched.
  **SUPERSEDED by chunk E1 (2026-10-19):** site 9 RE-OPENS after an applied continuation (MTG_SITE9_REOPEN),
  and ComputeUntapLoopNeed funds the CHAINED payoffs (kLoopPayCastDrain / kLoopPayWishDrain /
  kLoopPayCastPump) behind the one-step ones -- see the ledger's "Chunk E1".
* **The kill fold** (`MTG_UNTAP_LOOP_KILL_FOLD`, default ON): when the float pays the need, the continuation
  list is the proven kill alone. Without it a post-loop continuation (~50 floating mana, every hand subset
  affordable) enumerated 9k-360k plans; one d5 game went 9 s / 148 MB -> 175 s / 4.7 GB and the pooled
  probe tripped the RSS cap. Sound: a win this turn dominates every alternative in a goldfish.
* **Proven-loop memo marker: NOT built.** With the kill fold the post-loop states are no longer a
  fragmentation problem, and folding `life_gained_this_turn` would be unsound for a state whose payoff is
  still being funded off a specific X.
* **Remaining search cost (diagnosed, not changed -- engine-wide folds):** the slow games are per-node cost
  in the greedy subset walk (`SolveUncached`; d3 gi55: 11.8 M subset visits, 7.9 M payment calls, ~250-800
  payment solves per unit) and the EnumeratePlans odometer. `MTG_BRANCH_SHAPE` prices an
  INTERCHANGEABLE-COPY fold at 7.26x odo (4 Nourish / Feed the Clan / Stroke / Living Wish copies are each a
  x2 group) and a one-per-class LAND-AURA host fold at 3.7x (Utopia Sprawl x 18 Forests x its colour axis;
  `FoldInterchangeableAuraHosts` keeps one member per land-Aura in hand). Both are engine-wide and move other
  decks' ground truth -- surfaced to the USER, not adopted here.
