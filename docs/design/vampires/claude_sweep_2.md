# Vampires — claude-play sweep 2 (CONFIRMATION, analyze-deck Stage 5d), 2026-10-18

- commit: `2ed53cf74` (branch `vampires-analysis`: sweep-1 fixes A-L, payer re-fix, Stage 5f search-cost
  folds), binary `build/Release/mtg` of that tree
- seeds: 92000-92007 with `--game-index` 2..9 (one opponent-spawn slot each, so the passive opponent has
  creatures and Captivating Vampire's steal is reachable; sweep 1 ran slot 0 only), one Opus player per game
- protocol: `--claude-play --seed S --game-index G --max-turns 10 --reveal 6`; benchmark
  `--games 1 --seed S --game-index G --depth 5 --budget-ms 200`
- brief: sweep-1's player brief plus an addendum re-checking every sweep-1 defect class (scratch,
  `build/scratch/vampires/sweep/BRIEF2.md`)

## Outcome
Claude tied the search's win turn in all 8 games (T4: 92001, 92002, 92005, 92006, 92007; T5: 92000, 92003,
92004), each player confirming its turn was the floor for its draws (92000: at most 18 damage by T4).
Every hand-computed combat total, lord bonus, counter, life change and cost matched the engine.

Mechanics verified in play or in side probes:
- **Captivating Vampire's steal** (new this sweep; 92000, 92001, 92006): offered exactly when five untapped
  Vampires are controlled, summoning-sick bodies and Captivating itself included; target prompt lists the
  opponent's creatures; with exactly five Vampires the bodies are forced; the stolen creature becomes a
  Vampire, gets every lord bonus, is summoning sick the turn it is taken and attacks the next (92006: the 6/6
  wall attacked as an 8/8); it counts as a body for a later steal; re-offered on later turns.
- Convoke (sweep-1 A/B/C/K classes): pip colours paid by matching bodies, bodies cast earlier in the same plan
  (sick Socialite, Mavren, Pit Fighter, Keeper) convoke, no creature taps twice across two Barons, no convoke
  from combat-tapped creatures (92001, 92002, 92004).
- Payer (F/L): Voldaren Estate's pay-1-life colour spent only when no painless source can pay; Courtyard kept
  for {R} activations (every game).
- Discard outlets (E): no Blood crack / pump offered with an empty hand (92000, 92007).
- Execution order (J): plans execute in listed order (92007).
- Bloodline Keeper token, transform ({B} from Swamp / Courtyard), Lord of Lineage +2/+2 and its token
  (92003, 92004, 92007) (H/I).
- Madness: Gorger-granted at cleanup and from Condemned / Pit Fighter / Blood crack; the pump reaches the
  madness-cast creature; an unpayable cast auto-declines to the graveyard; Gorger sacrificed after the
  discard still lets the card go to exile first (92001, 92005, 92006, 92007).
- Falkenrath Pit Fighter: post-combat only, {R} via Courtyard, discard -> sacrifice -> draw 2, no second
  activation without mana (92000, 92006).
- Blood: cost {5} minus Vampires at activation ({3}/{2}/{1}/{0} observed), two Estates make two Bloods,
  same-turn crack (92002, 92004).
- Mavren Fein's token (not attacking, lord-boosted), Aether Vial, Blackcleave Cliffs, lifelink.

## Flags
- **M (CONFIRMED, two independent repros; minor, conservative): Voldaren Estate's Blood is priced without the
  plan's non-cast Vampire arrivals.** The TapBlood cost variants (TurnSolver.cpp, `tap_blood_cost` block) cap
  k at the Vampire cards in hand and SubsetVampOutletInvalid rule 3 counts only CastFromHand / ActivateVial
  arrivals, so a Bloodline Keeper token made earlier in the same plan (and, per the code, a Bloodghast
  landfall return, a madness cast or a stolen creature) does not discount the Blood. The executor pays the
  true cost, so offered plans are legal; plans affordable only at the true cost are missing.
  92003 decision 13 (`1,0,0,0,4,0,0,0,0,0,1,0,0`, seed 92003 gi 5): no 'Captain, token, Blood' without a land;
  92004 decision 15 (`1,1,-1,-1,0,-1,-1,4,-1,-1,-1,-1,6,-1,-1`, seed 92004 gi 6, MTG_PLAY_PLANS_CAP=0): no
  'token, Blood, Blood, transform' / 'Vial, token, Blood, Blood[, transform]'. Fix: branch `vampires-fixm`
  (ledger "Sweep 2 fixes").
- Dismissed observation (92006): in human play the steal is not offered in a frame that needs same-plan casts
  to reach five Vampires; it appears in the re-prompt after those casts. This is the documented
  HumanPlayActive design in EmitStealVariants (the autonomous enumerator does emit arrival variants), so no
  line is lost.
