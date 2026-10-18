# Accomplished Alchemist untap loop — design (AccomplishedAlchemist deck)

Status: IMPLEMENTATION IN PROGRESS (2026-10-18). Per-deck ledger: `analysis-AccomplishedAlchemist.md`.
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
