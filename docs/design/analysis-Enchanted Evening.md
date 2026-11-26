# Analysis ledger: Enchanted Evening

Worktree `.claude/worktrees/enchanted-evening`, branch `ee-analysis` (from 05e2e0030). Nothing pushed yet.
Decklist: `decks/Enchanted Evening/Enchanted Evening.cod` (60 cards, GW enchantress / lock).

## CHECKPOINT (orchestrator, updated 2026-11-26, pre-compaction #2) -- read this first after a compaction

* **USER RULINGS 18-21 BUILT + PRUNE RE-MEASURED (2026-11-26): code 2df2cebb1 + the ledger commit** (section "USER
  rulings 18-21 + prune re-measure"). Units 4251/4251; smoke 247/0, play-changed=0 (no deck moves); GT not accepted.
  Three-arm A/B (224 games, d3 b10, fairness ON): OFF 5.848, ON 5.799 (net -11, units -59.7%), ON + amendment
  5.799 (net -11, units -57.8%). Every slower game recovers at d3 b0 except ho gi=84 and hb gi=24 (both arms),
  both clairvoyance. No prune gap remains. Rulings 19-21 vs step 8b: one game moved (ho gi=31 6 -> 5, the ruling-20
  O-Ring on a virtual Kumena's Speaker). Clause 4 and FoV-vs-evasion-Aura never fired in a real game. Open USER
  decisions: `MTG_EE_USER_PRUNE` OFF / ON / ON + `MTG_EE_GROVE_PRELOCK` (numbers in the section); ruling 21's
  power >= 1 default (no data); ruling 20's "board lock already stands" precondition.
  NEXT = **step 8c**, in this order:
  * hb1 gi=90 (d3 b0 T6 vs step 6 T5: the T5 main-1 Force of Vigor on opponent lands instead of Grove -> Opalescence);
  * the d3 b0 intractability of ho1 gi=3 (not the free-turn rule) and ho1 gi=70 (the free-turn chain);
  * the hb1 gi=31 Calming Verse that "does nothing" -- FIRST check whether it was clearing a VIRTUAL board (ruling 8
    credits removal we actually cast; a Verse under EE clears virtual permanents too), which would make it a real
    play, not a dominated cast;
  * root-cause gi=12 / gi=13 (steps 3+5 notes);
  * (low priority, prune-OFF arm only) ho gi=84 at d3 b0 with the prune OFF: 2.64M units in 2 h, unfinished at T1 --
    wall outside the unit counter.
  Then the discard buckets: build `docs/design/enchanted-evening-discard-buckets.md` (c08f9b1d8, the AI-drafted
  design with the USER amendments) behind a default-OFF flag and A/B it, with the withdrawn dead-land fold returning
  under it. Then step 9 (viewer `eot_cast` + ruling 14 + DECISIONS.md; also: virtual O-Ring targets in human play, the
  upkeep Grove activation missing from the trace).
* (superseded by the rulings 18-21 entry) **LOCK STEP 8b DONE (2026-11-26): fd439c7af (WIP) + e51f75196 + the ledger commit** (section "Lock-cluster step
  8b"). Shipped: Utopia Sprawl's colour axis held to {G, W} and the Parallax Wave Exploration fold (both proven, default
  ON, no flag). The dead-land fold was WITHDRAWN (deleted): the held land can cost a nonland at a later cleanup,
  because the deck's discard is the ranked pick. The census diagnostic was removed. Units 4248/4248; smoke 247/0,
  play-changed=0 (no deck moves); A/B m8b vs 8a net +1 turn / 224, units -17..-31%. The 3 slower games recover at
  d3 b0. hb1 gi=16 now finishes at b0 (T6, 74 min); ho1 gi=3 and gi=70 stay
  b0-unfinished at 2 h.
  NEXT = **step 8c**, in this order:
  * hb1 gi=90 (d3 b0 T6 vs step 6 T5: the T5 main-1 Force of Vigor on opponent lands instead of Grove -> Opalescence);
  * the null Calming Verse cast (hb1 gi=31 at b10: no opposing enchantment and no untapped land after payment, so it
    resolves to nothing; the step-3+5 fold withdraws only KEEP, and SWEEP collapses to nothing here);
  * the d3 b0 intractability of what stays unfinished: ho1 gi=3 (not the free-turn rule) and ho1 gi=70 (the free-turn
    chain; counting free turns as plies at b0 would be the USER's call). hb1 gi=16 now finishes (T6);
  * root-cause gi=12 / gi=13 (steps 3+5 notes).
  Then step 9 (viewer `eot_cast` + ruling 14 + DECISIONS.md).
* (superseded by step 8b) **LOCK STEP 8a DONE (2026-11-26): e0e74e894 + 44303575e + the ledger commit** (section "Lock-cluster step 8a"). It holds the
  free-turn standing pass (`MTG_FREE_TURN_STAND`, default ON, a re-price, inert at b0), the FoV own-target fold
  (Presence withdrawn from it in review), and `MTG_EE_FOV_EOT_OPP_ONLY` + the nesting diagnostic DELETED. Units
  4245/4245; smoke 247/0, play-changed=0 (no deck moves); A/B m8 vs step 7 net -1 turn / 224, units -12..-19%.
  NEXT = **step 8b**, in this order:
  * the land play under EE + Opalescence (a fold);
  * the Parallax Wave own-target fold (design §4.4; `FadeExileCreatureTargets`, a different code path from FoV);
  * the +27% units from step 4;
  * root-cause gi=12 / gi=13 (steps 3+5 notes);
  * the d3 b0 intractability of ho1 gi=3 and hb1 gi=16 (> 2 h, and still > 30 min with `MTG_FREE_TURNS=0`, so not the
    free-turn rule); ho1 gi=70 at b0 IS the free-turn chain (0.5 s with free turns off; a possible USER question);
  * also: hb1 gi=90 (d3 b0 T6 vs step 6 T5, the T5 main-1 FoV over Grove -> Opalescence; unchanged by 8a); the
    tie-indifferent own-target FoV casts (ho1 gi=16 / 73 destroy our own Presence at round 3); the slower games' b0
    verdicts in the step-8a section.
  Then step 9 (viewer `eot_cast` + ruling 14 + DECISIONS.md).
* (superseded by step 8a) **LOCK STEP 7 DONE (2026-11-26): a26942ea0 + c69cb9da9 + 1ad7ca2da (the pending fix) + the ledger commit.** NEXT =
  **step 8**: folds (a) the land play under EE + Opalescence, (b) Wave own-target dominance. Also, in this order:
  * the end-step FoV own-target dominance fold (the full menu's slower games ho1 gi 3 / 40 / 44 / 84 / 9 are
    own-target casts);
  * the free-turn x end-step-fan cost blowup (step-7 section, "Pending fix + re-measure"): price the fan in the
    budget, or a sound fold under a locked opponent;
  * root-cause hb1 gi=90 (d3 b0: step 7 T6 vs step 6 T5) and gi=12 / gi=13 (steps 3+5 notes).
  Then step 9 (viewer `eot_cast` + ruling 14 + DECISIONS.md). (The temporary `MTG_EE_FOV_EOT_OPP_ONLY` was DELETED
  in step 8a, on the orchestrator's brief: "delete, don't lever". That USER question is withdrawn.)
* Done: Stage 1 coverage; lock-cluster DESIGN 84a576461 (with the orchestrator's Grove correction below); four simple
  cards DONE (f986f6e19, 61e5bc848, b5b8dd3cb, d7885c7fe: units, smoke 247/0 play-changed=0, coverage full); lock
  steps 1, 2, 3+5 DONE (entries below).
* RUNNING (orchestrator-launched Opus agent): "Finish EE lock step 4 Wave" -- continues from
  `logs/ee/lock4-handoff.md` (WIP f855925a7: Parallax Wave + ruling-7 end-step sweep, builds, NOT yet tested). Its
  brief carries the orchestrator default for G >= 2 Groves (sweep still spends the Wave's counters on opponent
  creatures, ruling 7 literally). Its own handoff (if any) = `logs/ee/lock4-handoff2.md`. Smoke may move a Saproling
  Burst deck (fading now placed on every entry route): root-cause, do NOT accept GT without the orchestrator.
* **RULINGS 15/16/17 (Grove copies, tutor targets, no finite sweep tail) DONE (2026-11-25):** 0e80072ed + fe22e545f +
  6840da137 + de901bc9e. `MTG_EE_USER_PRUNE` DEFAULT ON (USER-requested; ADOPTION IS THE USER'S: held-out 200 games
  ON 188 won / avg 6.825 vs OFF 189 / 6.775, units -54%; the amendment arm `MTG_EE_GROVE_PRELOCK`, DEFAULT OFF, 188 /
  6.795). Units 4225/4225; smoke 247/0 play-changed=0 on both 0e80072ed and de901bc9e. Two slower games do not
  recover at d3 b0 (Grove as a 2/2 Opalescence attacker). Details + firing counts: "Ruling 15/16/17 prune" section.
  NEXT = lock step 6 (below, item 2).
* (superseded list, kept for the order) NEXT, strictly serial (same files), one narrow Opus agent each, 250K handoff rule in every brief:
  1. DONE -- USER ruling 15 (Grove policy): provider prune "no second Sterling Grove while one is out, except after
     sacrificing the first in the same window" + tutor-target ORDER combo pieces first (EE, Opalescence, Wave; mana
     enchantments + Presence after; orchestrator also applies it to Idyllic Tutor, surfaced to the USER). Measure the
     24-game sanity + b0 checks; REPORT games with mana-efficiency loss pre-combo and lost Presence draws (ruling 15
     follow-ups) for the USER's pre-combo-exception decision.
  2. Step 6: free-turn integration (trait in `DeckMayLockOpponent`, `DeckLockNeedsLandStrip` false, reach
     certificate OFF for this deck, fairness check per rulings 5, 6, 8-13: virtual board, Verse wipe point, hexproof
     trample >= 5 test incl. castable Auras, FoV vs virtual Auras as a searched cast). **DONE 2026-11-25** (entry below).
  3. Step 7: FoV end-step pitch. 4. Step 8: folds (land play under the lock, fetch variants) + root-cause gi=12/gi=13
     (steps 3+5 notes). 5. Step 9: viewer wiring incl. ruling 14 (auto float + manual tap) and DECISIONS.md.
  Then: Stage 3 coverage loop, Stage 4 profile + provider audit (provider must NOT opt into `ClassifiesMainPhases`),
  suite addition (all three tiers, 3x Hinata gate: watch step-1 gi=14 328 s), Stage 5 verification incl. 5d sweep,
  fetch-hold review (Heath, Foothills), Stage 6 report (disclose ruling 12's attack-everything optimism). Push later
  via the push queue as one batch.
* Idyllic Tutor question ANSWERED (ruling 16: yes, and prune). The G >= 2 sweep default is REPLACED by ruling 17
  (no loop -> the sweep does nothing). Open USER questions: (a) adopt `MTG_EE_USER_PRUNE` ON (as shipped now) / OFF /
  with the amendment `MTG_EE_GROVE_PRELOCK`; (b) the 4-vs-5 mana breakpoint (the 4-band fired 32 times in 200 games;
  the search took a lock piece there 28 times).
* Housekeeping: `build/wt/ee-base35` (scratch baseline build output) can be deleted.

* **LOCK STEP 6 (free-turn integration + the USER fairness check, §3.2-§3.4) DONE (2026-11-25):** code 4a676a477 +
  ff0c0014a (umbra-armor rules fix, test setup fix) + 9571ebde7 (log tags). Units 4233/4233; smoke 247/0
  play-changed=0 (searched and d0), scenarios 677/677; card-field audit rc=0. Fairness check ON (shipped) vs the
  prune-ON baseline: sanity 24/24 avg 5.7083 (was 22/24 6.7083), held-out 5.86 / 5.85 (was 6.81 / 6.84), units
  -30..-43%; vs the check OFF it costs 0.67-1.00 turn. Real vetoes: 11 turns in 9 of 224 games, all clause 1.
  Reach audit: never consulted. Slower games (sanity gi=1, gi=3) recover at d3 b0. Three USER questions (clause 3,
  O-Ring vs virtual, evasion) in the "Lock-cluster step 6" section. NEXT: step 7 (Force of Vigor end-step pitch;
  the virtual-Aura FoV pins must be offered at the pitch too).
* **LOCK STEP 7 (Force of Vigor end-step cast, §4.5) BUILT, NOT CLOSED (2026-11-25):** code a26942ea0 + the WIP after it. Units 4239/4239, smoke 247/0 play-changed=0, coverage all `full`. m7 vs step 6 (fairness ON): sanity 24/5.6250 (was 5.7083), 8000 95/5.89 (5.86), 10000 98/5.76 (5.85), units +9..+23%; 39 real casts, all mana-paid, 17 on our own permanents (optimizer's curse, step 8). OPEN DEFECT: an over-wide `FaceUpUncoveredPending` (FoV in hand, menu width 1) nests boundary rollouts without bound (m7o ho1 gi=9). Resume from `logs/ee/lock7-handoff.md`; then step 8 (folds + root-cause gi=12/gi=13).
* **LOCK STEP 1 (type statics: Enchanted Evening + Opalescence) DONE (2026-11-26):** code a9d866afe, Scryfall snapshot
  aea0e800e (update rc=0, offline diff rc=0). Units 4191/4191 (13 new, `test/unit/test_enchanted_evening_lock1.cpp`).
  Smoke (Release, `MTG_PGO=0`): 247 passed / 0 failed / 0 new, play-changed=0 (searched and d0), scenarios
  677/677 (EE is not in the suite yet). Sanity run (24 games, seed 7000, d3 b10, no profile): 23 won, avg 6.625; the
  lock formed in 18 of the 23 inspected (both sides 0 lands, opponent 0 permanents, later drops die, no fetch); the unwon game is a
  no-threat flood (Opalescence + Exploration only). SLOW-GAME 34-45 s on 3 games (no keep table yet). Coverage: EE + Opalescence `full`; missing = Calming Verse,
  Force of Vigor, Parallax Wave. Notes for steps 2-8: "Lock-cluster step 1" section below the Cards table.
  (Correction: step 1's gi=14 also took 328 s / 7.52M units; the "34-45 s" above omitted it.)
* **LOCK STEP 2 (pre-float at the lock, §4.2) DONE (2026-11-26):** code 67ab77dfe. Units 4196/4196 (5 new,
  `test/unit/test_enchanted_evening_lock2.cpp`). Smoke (Release, `MTG_PGO=0`): 247 passed / 0 failed / 0 new, play-changed=0 (searched and d0), scenarios 677/677. Sanity run (same 24 games,
  seed 7000, d3 b10): 23 won, avg 6.625 (unchanged); exactly ONE game's play moved (gi=7, T-index 21: EE completes the
  lock, Presence draws Exploration, which is now cast off the floated {G}; same win turn); units per game within
  +0.4% of step 1 (wall times inflated by a concurrent smoke run). Notes: "Lock-cluster step 2" section. NEXT: step 3
  (Calming Verse, §4.3).
* **LOCK STEPS 3+5 (Calming Verse §4.3; Force of Vigor HARD CAST §4.5) DONE (2026-11-25):** code 471a97cab + 21a2cb821
  + 38c56beb8 + 89c1f70f0, Scryfall snapshot 3cd4e4c8f. Units 4206/4206 (10 new, `test/unit/test_enchanted_evening_lock35.cpp`).
  Smoke (Release, `MTG_PGO=0`): 247 passed / 0 failed / 0 new, play-changed=0 (searched and d0), scenarios 677/677 (on the final code; also on the WIP code before the folds). Coverage: Calming Verse + Force of Vigor `full` (FoV's pitch clause is NOT
  modelled -> partial, step 7); missing = Parallax Wave only. Sanity (24 games, seed 7000, d3 b10): 23/24, avg 6.7083
  vs the step-2 baseline's 6.625 (4 games moved a turn: gi=3 faster, gi=5/12/13 slower), units +8.7%; the slower
  games do not recover at d3 b0 (gi=12, gi=13: T6 -> T7) -- NOT root-caused, handed to step 8. Held-out playout A/B
  neutral. Notes: "Lock-cluster steps 3+5" section. NEXT: step 4 (Parallax Wave, §4.4).
* **LOCK STEP 4 (Parallax Wave §4.4 + the USER ruling-7 end-step sweep) DONE (2026-11-26):** code f855925a7 (WIP) +
  eb746055a (finite tail, retype-before-targeting, test fixes, Scryfall snapshot: update rc=0, offline diff rc=0).
  Units 4216/4216 (10 new, `test/unit/test_enchanted_evening_lock4.cpp`; one expectation fix: the idle pass is appended
  by every search host, not by `EnumerateMainPlans`). Smoke (Release, `MTG_PGO=0`): 247 passed / 0 failed / 0 new,
  play-changed=0 (searched and d0), scenarios 677/677 -- the enter-cascade fading change moved nothing. Coverage:
  missing = none, every card `full`. Sanity (24 games, seed 7000, d3 b10): 22/24, avg 6.7083 (= step 3+5's 6.7083,
  which was 23/24); units 18.13M vs 14.22M (+27.4%). Moved a turn: gi=5 8->7, gi=9 8->6 faster; gi=2 6->7, gi=11 5->6,
  gi=16 8->unwon slower. d3 b0 repros: gi=11 T5 (recovers), gi=16 T8 (recovers to the base), gi=2 still running at commit (d3 b0 over 45 min, a single game; `build/scratch/ee/l4/b0-2.out`, marker `b0.DONE`). Root cause
  of the three: the Wave is now a real card (it was a blank before), so the d3 b10 search spends on its cast and its
  one-action-per-target activation menu (no own-target folds, step 8) and picks worse roots (gi=11: Wave T3 instead of
  Opalescence T3; gi=16: no T1 land drop; gi=2: Tutor -> Grove T5 instead of Opalescence T5). Notes + the open USER
  question (the sweep's finite tail): "Lock-cluster step 4" section. NEXT: step 6 (fairness check, §3.4).

## USER rulings (verbatim, 2026-11-25)

1. *"Note that the deck locks down the opponent so, like Worldfire, we will need to take that properly into account. In
   particular Calming Verse with Enchanted Evening out removes everything and temporarily locks their lands. Enchanted
   Evening and Opalescence permanently locks the lands, though we should consider cards they can play prior to being
   locked to see if it is a true lock. One Parallax Wave is also this is considered a full lock because any creature can
   be exiled using the Parallax Wave + Opalescence combo and all permanents except Opalescence are creatures."*
2. *"(the lock idea is all about getting free turns in particular)"*
3. *"But with the full combo out here, we can attack with Enchanted Evening and Parallax Wave, ending the game pretty
   quickly."* / *"(while the opponent can do nothing)"*
4. *"It should be one 5/5 and one 4/4."* / *"(only enchanted evening costs 5, parallax wave is 4)"*

5. *"Yes, Calming Verse + Enchanted Evening is not a full lock, but if they play no land then it gives free turns. Also,
   when Opalescence drops it becomes a full lock as long as there is not a major gap between them."* -- so Verse+EE
   needs no rule change (an opponent turn ending with no land and no creature is already free), and Verse+EE then
   Opalescence is a full lock; the fairness check counts only what the opponent could cast in the gap between them.
6. *"I think "could they actually have cast a permanent with the lands they had in the gap" probably works, but we'll
   discuss if anything else comes up."* -- that is the gap test; no further threshold.
7. *"The default for Parallax Wave and Opalescence in this deck should be to exile all opponent creatures. Then when
   Enchanted Evening drops all of the opponent permanents are creatures. This means that they should all be exiled.
   Fortunately new lands die immediately so that doesn't need to enter the equation."* -- Wave + Opalescence exile
   every opponent creature (the self-exile refresh loop makes it unlimited); with EE that is every opponent permanent.
   A deck rule: it lives in this deck's provider.
8. On whether Force of Vigor / Oblivion Ring in our hand answer a permanent the opponent could have cast (fairness
   check): *"I guess it could count for that in theory, though it is a bit tricky to do so given that it requires us to
   either pay the mana or the extra card. It doesn't count for removing a follow-up land for sure."* -- default stays
   NO credit for held removal in the fairness check (only removal we actually cast counts); never assume a follow-up
   opponent land is removed.
9. On a hexproof creature the opponent could have cast vs the Wave + Opalescence lock: *"It does technically, but would
   need to be able to grow to at least 5 power with trample to be an actual threat to the full lock. The reason is that
   Enchanted Eveining can block a creature with less power creatures without trample can be infinitely chump blocked
   with their blocker exiled and replaced."* -- so in the fairness check an untargetable (hexproof) castable creature
   breaks the full lock ONLY if it has trample and can reach power >= 5; otherwise it is contained (chump-blocked
   forever, blockers exiled and replaced by the Wave) and the lock holds.
10. *"But it is true that Auras is a threat to this setup if they get the right stuff on board."* -- the "can reach
   power >= 5 with trample" test must count what the opponent could have put ON the hexproof creature in the gap, not
   just the creature's printed stats: castable Auras / equipment from its hand (Rancor-style +power and trample) within
   the same land budget. Rules detail: under EE + Opalescence the creature is also a non-Aura enchantment, so its BASE
   P/T becomes its mana value (layer 7b), and Aura bonuses apply on top (7c); Auras themselves are never animated and
   the Wave cannot target them.
11. *"But, if they can prevent attacks from the Auras deck in the way I mentioned, Enchanted Evening is guaranteed to
   win with the lock because, in the worst case, they can stall until Calming Verse."* -- Calming Verse does not
   target, so hexproof is no defence: under EE it destroys the creature AND its Auras (all enchantments we don't
   control). Orchestrator's reading for the fairness check, consistent with rulings 5, 6 and 8: (a) a hexproof
   castable creature that cannot reach power >= 5 with trample never breaks the lock (contained by chump blocks until
   a Verse); (b) one that CAN vetoes free turns only until we actually resolve a Calming Verse with EE out after it;
   that Verse is a new wipe point, so the "what could they cast in the gap" window restarts from it (ruling 6), and
   with Opalescence also out the opponent cannot rebuild lands after it.
12. *"(and to be clear, if it only hits 3 power or less you can actually attack freely with Enchanted Evening, holding
   back Parallax Wave to block."* -- context for (a): against a contained threat of power <= 3 the real clock is EE
   attacking (5/turn) with the Wave (4/4) held back as a blocker. The engine's passive opponent never attacks and a
   virtual (could-have-cast) creature is never on the board, so the engine lets every attacker swing; in those rare
   games our clock is slightly optimistic. Disclose in Stage 6a; no modelling change unless the USER asks.
   USER follow-up: *"Yes, we won't play that out, I'm just clearing up why and why not something might be a full
   lock."*
13. *"Note that not having a full lock because of a hexproof creature with enchantments is a real use case for Force of
   Vigor. In that situation the Force will often be able to reduce the size or remove trample, leading to an actual
   lock."* -- Force of Vigor targets the AURAS (not the hexproof creature), so it can take the threat below 5 power or
   strip trample and restore the full lock. Consistent with ruling 8 (only removal we actually cast counts), the
   orchestrator's DEFAULT design: the fairness check keeps the could-have-cast threat as a structured object (the
   creature + the Auras/equipment it could carry), and while such a threat vetoes the lock the deck may actually cast
   (hard-cast or end-step pitch) a Force of Vigor against up to two of those virtual Auras -- paying its real cost --
   after which the reach test is recomputed. A searched action, offered only while a virtual threat exists.
   USER: *"That sounds good."* (approved, 2026-11-25)

14. On the step-2 pre-float (lands about to die to EE + Opalescence are tapped for mana before the lock completes)
   in human play: *"Automatic tap is good, but manual tap using the manual tap interface should be allowed."*
   (2026-11-26) -- the viewer keeps the automatic float as the default, AND the human may instead tap lands
   through the viewer's existing manual-tap interface before the lock completes; those lands are then
   already tapped, so the automatic float skips them, and the mana they made stays in the pool for the phase.
   Owed in step 9 (viewer wiring): verify both routes in the viewer and in a reference replay.

15. On two Sterling Groves out at once (which makes the Wave self-exile loop impossible): *"My recommendation would be
   to not put out 2 sterling groves simultaneously or to only play the second one when sacrificing the first.
   Typically you want to use them to get combo pieces anyway. We should also prune that if possible. Rarely you want a
   mana enchantments like Wild Growth or Utopia Sprawl or Enchantresses' presence for cards, but generally you want to
   advance the lock by gathering pieces."* (2026-11-26) -- so (a) a USER-requested PROVIDER prune
   (`EnchantedEveningProvider`): never offer casting/putting a second Sterling Grove onto the battlefield while one is
   there, except in the same window where the first one is sacrificed (its tutor activation) before the second
   arrives; (b) Grove / Idyllic Tutor targets are ORDERED combo pieces first (Enchanted Evening, Opalescence, Parallax
   Wave), mana enchantments (Wild Growth, Utopia Sprawl) and Enchantress's Presence after -- an ordering, not a
   restriction ("Rarely you want ..."). The orchestrator's step-4 default for the end-step sweep with G >= 2 (spend
   the Wave's counters on opponent creatures, ruling 7 literally) stays as the fallback for the rare board that still
   reaches two Groves.
   USER follow-up: *"We'll have to think about whether this causes any mana efficiency issues and maybe we'll need an
   exception prior to the full combo being assembled, but let's play with it."* -- build it, then measure for
   mana-efficiency losses (games where a second Grove before the combo is assembled would have used mana better,
   e.g. a spare 2 mana with nothing else to cast); report those games so the USER can decide on a pre-combo exception.
   USER follow-up 2: *"Another reason to play a second early could be the card from Enchantresses' Presence, but
   usually we can afford to sacrifice one of them in that case."* -- a second Grove cast is an enchantment spell, so it
   draws a card off Enchantress's Presence; the prune's "sacrifice the first in the same window" exception already
   covers the usual line (sacrifice one Grove to tutor, cast the second for the Presence draw). Count, in the
   measurement, games where the Presence draw from a second Grove was lost because the first could not be sacrificed
   (e.g. no {1} for the activation).

16. **Ruling 15 follow-up 3 / ruling 16 (2026-11-25), answering "should the tutor order also apply to Idyllic Tutor?"**

   > Yes, the ordering should go that way. However, we should go beyond that and prune certain entries that are not going
   > to be the choice. At most there should only be 5 choices for search (Parallax Wave, Enchanted Evening, Opalescence,
   > Enchantresses' Presence and one of Wild Growth or Utopia Sprawl (depending on our mana needs) and usually much less
   > than that, since we don't need duplicates of any of the pieces (including Enchantresses' Presence I would say) and we
   > don't need the mana option if we are doing fine on that front.

   Orchestrator reading (for the ruling-15 agent):
   - Applies to BOTH Sterling Grove's sacrifice tutor and Idyllic Tutor. Provider prune (USER-requested), plus ordering
     combo pieces first.
   - Never offered: Sterling Grove, Exploration, Oblivion Ring (any other enchantment in the list).
   - Duplicates pruned: a piece we already control or hold in hand (EE, Opalescence, Parallax Wave, Enchantress's
     Presence) is not offered.
   - Mana option: at most ONE of Wild Growth / Utopia Sprawl, chosen by mana need (Utopia Sprawl needs a Forest to
     enchant), and none at all when mana is "doing fine". The agent defines that test (e.g. our mana can cast every
     missing lock piece) and reports its definition and the measured effect to the USER.
   - If everything is pruned the tutor still resolves (find nothing / don't activate); "do nothing more" stays an option.

   **Ruling 16 follow-up (2026-11-25): which mana enchantment**

   > Utopia Sprawl should only be fetched over Wild Growth if we need the extra white or perhaps if we have 2 forests. If
   > we have enough white and are lacking in forests Wild Growth is a better option.

   Orchestrator reading: the single mana option is Wild Growth by default. Offer Utopia Sprawl instead only when (a) we
   are short of white (Sprawl names white as it enters) or (b) we control 2+ Forests (Sprawl needs a Forest; with one
   Forest, Sprawl would compete with a Wild Growth for the same land's value). With enough white and few Forests, offer Wild
   Growth. If neither card is left in the library, offer whichever is there.

   **Ruling 16 follow-up 2 (2026-11-25): when mana is "doing fine"**

   > Mana is doing fine I would assume would be 2 white and 4 or 5 mana. I'm not sure whether the breakpoint should be 4
   > or 5 as it kind of depends on the situation. As-in, if we are early in the game we have a good chance to draw mana
   > over a couple of turns but, if hitting 5 mana is the holdup, it would be much more important.

   Orchestrator default (surfaced to the USER, not settled): "fine" = we can produce >= 2 white AND >= 5 total mana ->
   the mana option is pruned. At exactly 4 total mana (with 2 white) the mana option is NOT pruned but ordered LAST, so the
   search decides between "4 is enough for now" and "5 is the holdup" (Enchanted Evening is {3}{W}{W}). Below 4, or below 2
   white, it is offered (ordered after the missing combo pieces). Count mana the way the engine counts available mana
   (lands + Wild Growth/Utopia Sprawl already in play). Report how often the 4-mana band is hit and what the search picks there.

   **Ruling 16 follow-up 3 (2026-11-25): the 4 vs 5 breakpoint**

   > Essentially the 5 mana breakpoint is not as urgent as 4 because we can cast the other two enchantments while we are
   > waiting. That said, if mana is the only thing missing and we have the opportunity to get it early, that probably
   > makes sense to do.

   Orchestrator reading (SUPERSEDES the "ordered last at 4" default in follow-up 2):
   - < 4 total mana or < 2 white: the mana option is offered (after the missing combo pieces in the order).
   - exactly 4 total mana with >= 2 white: the mana option is offered ONLY when mana is the only thing missing, i.e. every
     combo piece (Enchanted Evening, Opalescence, Parallax Wave) is already in hand or on the battlefield. Otherwise it is
     pruned and the tutor fetches a piece (Opalescence / Wave are castable at 4 while we wait for the 5th mana).
   - >= 5 total mana with >= 2 white: pruned.

   **Ruling 16 follow-up 4 (2026-11-25): Enchantress's Presence is not a lock piece**

   > Right, I wasn't counting Enchantress's Presence as a lock piece or something missing here. It's purpose is pretty much
   > to draw us out of tougher situations. If we have the chance to get a lock piece and are missing nothing else, that is
   > an obvious choice over more draw.

   Orchestrator reading:
   - "Lock pieces" = Enchanted Evening, Opalescence, Parallax Wave. Presence never counts as missing.
   - When a lock piece is fetchable (missing from hand + battlefield, present in library) and mana is fine (per follow-up 3,
     so no mana fetch is offered), Presence is PRUNED: the tutor offers lock pieces only.
   - Presence stays on offer (ordered after lock pieces and the mana option) when no lock piece is fetchable, or when mana
     is also missing. Never a second copy.

17. On the step-4 end-step sweep's "finite tail" (when the Wave self-exile loop is impossible -- 2+ Sterling Groves
   shroud the Wave, or 1 Grove with too few counters for Grove + self -- the step-4 orchestrator default spent the
   Wave's fading counters on opponent creatures, one linked exile each): *"No, we don't want to spend them until the
   combo is active again."* (2026-11-25) -- so with the loop impossible the sweep does NOTHING: no counter is spent,
   the Wave keeps its counters until the loop is possible again. The tail is DELETED (not levered) in
   `PerformWaveOpalSweep` (ruling-15/16 change set, see "Ruling 15/16/17 prune" below).

**Rulings 18-21 (2026-11-26)**, answering the step-6 / prune questions: (2) the 4-mana breakpoint of the ruling-16 tutor
prune; (3) should a clause-3 hexproof non-threat block the free turn when there is no able Wave loop; (4) should
Oblivion Ring be able to target the opponent's virtual board; (5) does an evasive hexproof creature (Gryff's Boon
flying, Spirit Mantle protection from creatures) break the lock without trample + power >= 5.

18. **(Q2, 4-mana breakpoint):** *"That sounds good to me, unless you are finding issues with the rule."*
   -> the ruling-16 breakpoint stands as built.
19. **(Q3, clause-3 hexproof non-threat):** *"No blocking should be done to be consistent for every deck."*
   -> a hexproof creature that is not a threat does NOT veto the free turn, with or without an able Wave loop. Delete
   clause 3 of `eefair::Veto`.
20. **(Q4, Oblivion Ring vs the virtual board):** *"I suppose if this is the different between the lock and
   not it should be an option. Otherwise I wouldn't cast it."* -> an Oblivion Ring we cast may target a permanent on
   the opponent's virtual board, offered ONLY when exiling it is what makes the lock hold (it removes the last veto);
   otherwise that target is not offered.
21. **(Q5, evasive hexproof):** *"I suppose that is true. If they cannot be blocked by the creature then we
   need to remove the flying or the creature itself to fully lock."* -> a hexproof creature our creatures cannot block
   (flying with no flyer/reach of ours, protection from creatures, unblockable, ...) vetoes the lock (in addition to
   the ruling-9 trample + power >= 5 case) until the creature or its evasion source (e.g. the Aura granting it, via
   Force of Vigor) is removed. Orchestrator default, to confirm: power >= 1 (a 0-power creature deals no damage).

   Also from the same message, on the tutor prune (Q1): *"For the tutor prune what information do you have regarding
   the games that are lost between them? I think your exception is potentially fine, but I'd like to hear more about
   the unrecoverable cases."*

Reading: the lock is credited through the EXISTING free-turn rule (`MTG_FREE_TURNS`, default ON,
`src/core/FreeTurns.h`, `docs/design/opponent-lockdown-free-turns.md`): a locked opponent turn does not advance the
round. The deck's lock cards must join `GoldFishRunner::DeckMayLockOpponent`. Under EE + Opalescence the attackers are
Enchanted Evening 5/5 (MV 5) and Parallax Wave 4/4 (MV 4), plus the other non-Aura enchantments at P/T = MV.

## Stage 1 coverage (2026-11-25)

Missing (9): Enchanted Evening, Calming Verse, Sterling Grove, Enchantress's Presence, Opalescence, Force of Vigor,
Parallax Wave, Sunpetal Grove, Oblivion Ring. Implemented already: Utopia Sprawl, Wild Growth, Forest, Temple Garden,
Exploration, Windswept Heath, Plains, Idyllic Tutor, Wooded Foothills. Fetch-hold review owed (Heath, Foothills).

## Stage 2 work split

| group | cards | owner | status |
|---|---|---|---|
| simple | Sunpetal Grove, Oblivion Ring, Enchantress's Presence, Sterling Grove | implementer agent A | DONE (f986f6e19, b5b8dd3cb): coverage `full` x4; units 4178/4178; smoke (Release) 247 PASS, play-changed=0; Scryfall snapshot 61e5bc848 (offline diff rc=0) |
| lock cluster | Enchanted Evening, Opalescence, Calming Verse, Parallax Wave, Force of Vigor | design agent B -> `docs/design/enchanted-evening-lock-design.md`, then an implementer | design DONE (84a576461); step 1 (EE + Opalescence type statics) DONE a9d866afe / aea0e800e; steps 2-9 to do |

## Lock-cluster design (84a576461, `docs/design/enchanted-evening-lock-design.md`)

Tiers: EE + Opalescence Tier 3 (type refresh + explicit 0-toughness SBA sweep in both worlds; pre-float our lands
when the lock completes), Calming Verse Tier 3 (two variants: tap-all / not), Parallax Wave Tier 3 (exile ability,
linked exile list, self-exile loop, USER ruling-7 sweep at the opponent end step), Force of Vigor Tier 2 hard cast +
Tier 3 end-step pitch. Lock trait: EE plus one of Opalescence / Verse / FoV. Reach certificate OFF for this deck.

**Orchestrator correction to §4.4/§8 (Sterling Grove vs the Wave loop):** the doc's "`1 + G` counters, nets `4 - G`"
holds only for G = 1. With G >= 2 Groves each Grove shrouds the OTHER Groves (and the Wave), so the Wave can target
none of them and the self-exile loop is impossible (a Grove's own {1}-sacrifice needs mana, which the EE + Opalescence
lock has removed unless floated). The implementer must model the targeting legality, not the formula.

## Cards (tier, viewer bucket)

| card | tier | params | lockstep sites | viewer bucket | open |
|---|---|---|---|---|---|
| Sunpetal Grove | 1 | `produces` G/W, `checkland_subtypes` Forest/Plains | shared `LandWouldEnterTapped` | none | -- |
| Enchantress's Presence | 2 | `draw_on_cast_types` ["Enchantment"] | `FireOnCastTriggers` (both cast paths) draws via `TrickDraw`; site-10 put-in-hand fan reads the same predicate (`CastTypeDrawWatcherMatches`) | none (mandatory draw) | -- |
| Oblivion Ring | 2/3 | `etb_exile_nonland_until_leaves` | shared enter cascade (`ResolveEtbExileNonlandUntilLeaves`, every entry path); LTB in `FireLeavesBattlefieldTriggers` (`ReturnExiledUntilLeaves`); link = `kExileUntilLeavesBase` marker + `linked_exile_number` (already folded) | A: `target` (board click, full legal set) | -- (target SEARCHED, D1 below) |
| Sterling Grove | 3 | `grants_other_enchantments_shroud`, `sac_tutor_top_cost` {1}, `sac_tutor_top_types` [Enchantment]; derived `CardDefinition::activated_tutor`; deck stamp `deck_has_enchantment_shroud` | shroud: `EnchantmentShroudFromGrant` inside `LandHasShroud` / `CreatureHasShroud` / new `PermanentHasShroud` (so Aura host selection, equip/aura creature targeting and O-Ring all honour it); hook for EE = `PermanentIsEnchantmentNow`. Activation: new `PermAbilityMode::SacTutorTop` (CollectActions emission one variant per `RankedTutorCandidates` name; pin = `SacNamedPin.target` in BOTH trailing passes; `ApplyPermAbility` -> `ResolveSacTutorTop`; site-9 key + `CardHasPostEntryActivation`); upkeep window: `GameEngine::UpkeepTutorOptions` battlefield half + `AIEngine::CastUpkeepTutor` battlefield dispatch (shared by both worlds) + the BoundaryFan `tutor_` liveness + the human-play upkeep prompt | A: `main_phase` plan variants (tutor target); upkeep = the tutor chooser prompt | -- (lead activation, D2 below); rulings 15/16: Grove-copy plan prune + tutor-target prune (`MTG_EE_USER_PRUNE`, DEFAULT ON, USER adoption pending; amendment `MTG_EE_GROVE_PRELOCK` OFF) |
| Idyllic Tutor | (existing) | `tutor_to_hand`, `tutor_types` [Enchantment] | `EnchantedEveningProvider::TutorCandidates` (ruling 16, shared with the Grove's tutor) + `TutorNeedsCandidate` (nothing kept -> not cast) | tutor chooser (full list; the prune's ranking first) | USER adoption of `MTG_EE_USER_PRUNE` |
| Enchanted Evening | 3 (lock step 1) | `static_all_permanents_enchantments`; deck stamp `deck_has_type_statics` | `RefreshTypeStatics` (layer 4, every battlefield copy, both sides) at every `RefreshDevotionCreatures` chokepoint (enter-cascade top, `OnCreatureDies`, the leave helpers, bounce, Replenish, scenario loader); deaths at `TypeStaticsSbaCheckpoint` (inside `LegendRuleSbaCheckpoint`: post-resolution both worlds, land drop, end step; + `ResolvePassiveOpponentLandTurn`, both spawn sites, both turn starts) | none (static) | -- |
| Calming Verse | 3 (lock step 3) | `destroy_opp_enchantments_then_own_if_untapped_land` (its OWN param, never `destroy_all_enchantments`) | shared `ResolveCalmingVerse` (SpellEffects.h) at both worlds' sorcery resolution (`EffectHandler` beside Worldfire / `apply_one`); destroys through `DestroyRemoveTogether` + `DestroyFireTogether` (one simultaneous event, triggers after both clauses, every leave reaches the chokepoints); KEEP floats via `g_lock_prefloat_hook`; searched axis KEEP "VK" / SWEEP-SELF "VS" in the CollectActions post-pass, `#DV` in both plan signatures | A: `main_phase` plan variants ("tap all lands first" / "leave lands") | -- (EE provider withdraws a no-op KEEP, notes below) |
| Force of Vigor | 2 + 3 (lock step 5: hard cast; lock step 7: clause 1 pitch at the opponent's end step, `alt_cost_exile_green_off_turn`) | `destroy_up_to_artifacts_enchantments` 2 | shared `ResolveDestroyUpToArtEnch` (re-checks each target, CR 608.2b), same destroy halves; target SET searched: `DestroyUpToArtEnchOptions` (identity classes, every 0..2 multiset), pinned "FV<n>[,<m>]", `#DV` | A: `main_phase` plan variants (step 9 may move it to the `target` multi-target row); B: the end-step pitch = `eot_cast` (step 9; NOT yet offered in human play, the audit manifest expects it) | none: the pitch is modelled (step 7: the morph window's FoV half, searched {no cast} + {pin} x {pitched green class}, mana-paid when payable); coverage `full` |
| Opalescence | 3 (lock step 1) | `static_animate_other_nonaura_enchantments_mv` | same refresh (layer 4 + 7b: other non-Aura enchantments = MV/MV creatures; two animate each other); 0/0s die at the checkpoint through `OnCreatureDies` (LTB fires: O-Ring returns its card); orphaned Auras (Sprawl / Wild Growth) swept; fetch on play suppressed both sides (`LandDiesToTypeStatics` in `PlayLandCore` and `PlayLeftmostLandFrom`), opponent Karoo finds no land | A: existing `attackers` | -- |
| Parallax Wave | 3 (lock step 4) | `fading_counters` 5, `fade_exile_target_creature`; deck stamp `deck_has_wave_opal_sweep` (EE provider `WaveOpalSweepRule` + Wave + Opalescence in the list) | fading: put by the cast sites pre-push AND by the shared enter cascade on every other route (a self-exiled Wave returns with five), `PerformUpkeepFading` at our upkeep; activation `PermAbilityMode::FadeExileCreature` (cost = one fade counter; one searched Action per admitted target, `FadeExileAdmittedTargets` beside CounterDestroy's, identity-class fold; `ApplyPermAbility` -> `ResolveFadeExileCreature`; `PermAbilitySourceLive` re-checks counter + target); exile via `FadeExilePermanent` (linked = `kExileUntilLeavesBase` marker + digest in `linked_exile_number`; token ceases to exist; leave chokepoints); return via `ReturnExiledUntilLeaves` from `FireLeavesBattlefieldTriggers`; ruling-7 sweep `PerformWaveOpalSweep` in BOTH opponent_turn lambdas (loop on targeting legality, no loop -> nothing, USER ruling 17; top-up) | A: `main_phase` plan variants (exile target; step 9 may move it to `target`); the sweep has no viewer decline yet (step 9) | -- (finite tail deleted, ruling 17) |

Provider: `EnchantedEveningProvider` (empty `DeckProvider` derivation, `Certificate()` = NotAssessed), routed FIRST on
`grants_other_enchantments_shroud || draw_on_cast_types || static_all_permanents_enchantments ||
static_animate_other_nonaura_enchantments_mv || destroy_opp_enchantments_then_own_if_untapped_land ||
destroy_up_to_artifacts_enchantments || fade_exile_target_creature` (Parallax Wave's param OR-ed in at lock step 4, also in `revpkg::CardCanEndOwnEnchantment`). Since lock steps 3+5 the
provider is no longer empty: it carries the two no-op cast folds (`CalmingVerseVariants`,
`DestroyUpToArtEnchTargetOptions`; "Lock-cluster steps 3+5" below).

## Lock-cluster step 1 (type statics) -- notes for the later steps

* **Where the state lives.** The refresh writes the permanent's own `Card` copy; what it added is recorded ON THE CARD
  (`Card::m_ts_mask` + `m_ts_saved_power/_tough`, in Card's tail padding: `sizeof(Card)` 136, `sizeof(Permanent)` 336,
  `sizeof(GameState)` 1744 -- all unchanged; `deck_has_type_statics` sits in a pad byte). A card that leaves keeps its
  bits, so an O-Ring / Parallax Wave return (step 4) is undone and re-derived by the first refresh after it re-enters;
  build the Wave's return through the enter cascade (or call `RefreshTypeStatics`) and nothing else is needed.
  Off-battlefield cards are NOT scrubbed (readers there use the definition, the Card.h convention); flagged only.
* **Deaths happen only at `TypeStaticsSbaCheckpoint`** (never inside the refresh: the enter cascade's callers hold slot
  indices). A new effect that can make a 0/0 outside a stack resolution / land drop / end step (Parallax Wave's
  opponent-end-step sweep, step 4; Calming Verse is a resolution, so it is covered) must call
  `TypeStaticsSbaCheckpoint(state)` itself at a point that holds no battlefield index, in both worlds.
* **Step 2 (pre-float) hook:** the lock completes inside `FireEtbWatchers` (the second of EE / Opalescence entering),
  and the lands die at the next checkpoint. `TypeStaticsOnBattlefield(state)` / `LandDiesToTypeStatics` are the
  presence tests to reuse; the pre-float must run before the refresh that makes the lands creatures (or at least before
  the checkpoint) in the shared resolution path. The whole-turn PREPAY (`BatchPrepayMainCasts`) may already have
  tapped lands before the lock-completing cast: check it when building step 2.
* **Wasted search to collapse later (step 8):** under EE + Opalescence our fetch-on-play variants (one per fetch target)
  all collapse to "the fetch enters and dies"; and any land play is dominated by holding it. Not folded in step 1.
* **Audit of the definition-reading battlefield sites (§2.1):** all 23 listed sites plus the new ones a fresh scan found
  (SE.h `PipFreeOutletOnBoard`, TS `AuraPlanHostKeyOn`, `HasteUnlockedManaOf`, the `poolaudit` diagnostic, Shadowborn /
  AntiLifegain / LandfallElementals / Stompy / Merfolk provider overrides) are param-gated, provider-gated (this deck
  routes to `EnchantedEveningProvider`, an empty `DeckProvider`), or read a hand / library / cast card. **No site needs a
  fix.** Note for step 3: keep Calming Verse on its own param, NOT `destroy_all_enchantments` (ManaPayment's
  `ApplyEnablerWipeRecheck` keys on that one; unaudited for this deck).
* **2HG:** a second head's land that dies goes to `players[1].graveyard` (the second head has no tracked graveyard; nothing
  reads it).

Notes for the lock-cluster implementer: (a) Enchanted Evening must extend `PermanentIsEnchantmentNow`
(SpellEffects.h) so the Grove grant covers every permanent; (b) every own-target site it adds (Parallax Wave, Force of
Vigor) should consult `PermanentHasShroud`; (c) any leave path it adds (Wave's exile, FoV's destroy, an Opalescence
creature dying) must reach `FireLeavesBattlefieldTriggers` so O-Ring's linked card returns; (d) under Opalescence a
Grove is a creature: `SacrificeSourceAsCost` already routes it through the creature death chokepoint.

## Lock-cluster step 2 (pre-float at the lock) -- notes for the later steps

* **What it does.** `PrefloatLandsAtTypeStaticsLock` (SpellEffects.h, beside `LandDiesToTypeStatics`) runs at the top of
  the universal enter cascade (`FireEtbWatchers`), right BEFORE `RefreshTypeStatics`, so one site serves the executor,
  the rollout and every put path. When the entrant carries a type static, its controller is the active player, and a
  land of ours dies under the statics WITH the entrant but not WITHOUT it (`LandDiesUnderTypeStatics`, the
  with/without test; EEs are counted so a second EE / Opalescence floats nothing), each such UNTAPPED land is tapped
  into `floating_mana` through `g_lock_prefloat_hook` (ManaPayment.cpp `LockPrefloatInstaller` -> `ApplyEnginePreTap`,
  `HumanPreTapOfferFor` the legality). A land drop under an active lock is never floated (it dies before priority).
  Already-tapped lands (the cast's own payment, `BatchPrepayMainCasts`' whole-turn prepay, a human pre-tap) are
  skipped: their mana is already spent or already floating, so nothing is lost or counted twice (unit test 2).
* **Colour (no greedy pick).** One free face: float it. A dual floats ONE unit as `wild` (tapped as its first face, that
  unit turned to wild) only under the new deck stamp `GameState::deck_float_wild_exact` (StampDeckTraits: every coloured
  pip of the list is a colour every multi-colour land of the list makes; a hybrid pip by either half; phyrexian /
  twobrid count as required) and for a one-mana tap; otherwise the dual dies untapped as before. TRUE for this list.
  Default FALSE (unstamped states). The searched colour-split axis for a deck where the stamp fails is NOT built.
  Only `Plain` / `Conversion` routes float (a sacrifice route would erase a permanent while the cascade's caller holds
  slot indices). Fetchlands are not cracked (design §4.2).
* **Phase end.** The float lives in `floating_mana`, which both worlds empty at every step/phase boundary
  (`CrossManaBoundary`: `GameEngine::CombatPhase` / the rollout's combat, the end step), CR 500.4; unit test 3.
* **Planner phase-boundary gate: nothing to build.** Each main phase is its own search node, enumerated from the real
  post-lock state (lands dead, float emptied at combat), so a main-2 cast after a main-1 lock is never offered off
  dead lands; within one phase the float equals the land mana it replaced (a dual as wild is exact under the stamp).
  The dominance comparator stands down on a nonzero float (Dominance.h's mid-turn boundary assertion), so the float
  cannot fold two distinct states. **Watch for the provider audit:** the Main2 classification
  (`ClassifiesMainPhases`) assumes "main 1's apply leaves the moved cast its mana", which a main-1 lock breaks; this
  deck's provider must NOT opt into it without accounting for the lock (default off today).
* **Human play:** the pre-float fires in the viewer too (it is a dominance with no choice under the stamp: viewer
  bucket "none", design §5); a human who pre-tapped some lands keeps that, the rest are floated.
* **For step 3 (Calming Verse KEEP):** reuse `g_lock_prefloat_hook(state, controller, num)` per untapped land before
  the Verse resolves (it is the §4.2 machinery and colour rule); the "dies with/without" filter is specific to the
  type statics, so call the hook directly with Verse's own land list.
* **Not covered (no such card in the list):** a non-land mana source that dies to the lock (an MV-0 mana rock or token:
  Treasure, a 0-cost mana creature) is not floated; extend the doomed scan if a deck adds one.

## Lock-cluster steps 3+5 (Calming Verse; Force of Vigor hard cast) -- notes for the later steps

* **Code.** WIP 471a97cab (resolvers, post-pass, hooks, viewer labels, cards.json, 9 unit cases), 21a2cb821 (FoV pin
  parser rejected nothing on a trailing comma -- a real bug the unit test caught; the WIP build had never compiled
  the new test file because its CMake registration landed after the build's configure), 38c56beb8 (no-op cast folds,
  below; its playout half was A/B'd behind a temporary `MTG_PLAYOUT_NOOP_CAST_DROP` lever, deleted in the final
  commit with the behaviour kept ON). Scryfall snapshot 3cd4e4c8f. Units 4206/4206.
* **Design decisions taken by the implementer (none is a USER-owned default):**
  1. Verse KEEP is offered whenever we control ANY untapped land at emission -- NOT withheld for an untapped fetchland
     (a departure from design §4.3). Completeness: emission is at the phase root, so "crack the fetch, then Verse
     KEEP" would be unfindable if KEEP were withheld. Resolution is literal: KEEP floats what it can; an untapped
     fetch makes clause 2 fire.
  2. In the decision space and in human play there is NO unpinned Verse / FoV cast (the base is converted to a pin);
     the provider picks (`CalmingVerseKeepDefault`, `DestroyUpToArtEnchPick`) run only beyond the horizon / d0. A
     provider that empties the FoV menu WITHDRAWS the cast (never leaves the unpinned base in the window).
  3. FoV targets come off the EMISSION board; a permanent entering earlier in the same main is targetable from the
     next main's root (each main is its own search root) -- not unfindable.
  4. FoV branching: identity classes (`CastTriggerTargetFoldKey`), every 0/1/2-target multiset; under EE with ~10-14
     legal classes that is ~50-100 variants per castable FoV. No lossy prune was added (a prune of own targets is a
     USER-owned narrowing; a budget-staged wave is the sanctioned shape if one is wanted).
  5. Human play: both axes are `main_phase` plan variants (Charm shape); step 9 may switch FoV to the `target`
     multi-target row (`EnumerateTargetSets`).
  6. Destroyed tokens never reach the graveyard (an opponent spawn body counts as a token).
  7. Known gaps, noted not fixed (unreachable here): Acidic Slime / Foundation Breaker / Charm destroy paths do not
     fire a NONCREATURE's LTB (an O-Ring they destroy would not return its card); umbra / totem armor is not modelled
     on any destroy path.
* **Rules review (mtg-rules skill):** Verse -- no target (hexproof/shroud no defence), clause 2 checked after clause 1
  is applied (Scryfall 2004-10-04), "untapped land" read at that moment, lands are enchantments under EE (ours go to
  clause 2, theirs to clause 1), one simultaneous destroy with triggers after the whole resolution (CR 603.3),
  indestructible stays, unearth exiled, cards to their OWNER's graveyard, orphaned Auras swept (CR 704.5m), KEEP's
  mana abilities with the Verse on the stack (CR 605.3a), the float empties at the phase end (CR 500.4). FoV -- 0..2
  DISTINCT targets (CR 115.3), artifact-or-enchantment on the copy, shroud binds our own targeting (Sterling Grove;
  CR 702.18a), opponent hexproof, per-target re-check on resolution (CR 608.2b; an all-illegal resolution does
  nothing, which matches the fizzle's effect). No violation found.
* **The no-op cast folds (38c56beb8; SOUND dominance, default ON, no flag).** The first sanity batch put Verses that
  destroyed nothing on the board (gi=12 T3 / gi=13 T3, before any EE) and target-less Forces. Premise, true of this
  list: nothing rewards casting an instant/sorcery as such (Presence draws on ENCHANTMENT spells), so a cast that
  changes no permanent is dominated by not casting it (always offered). The EE provider withdraws Verse KEEP when no
  enchantment we don't control exists (KEEP is then a no-op, or -- a land it cannot float -- equals SWEEP-SELF, which
  stays), and the FoV empty set (no legal target -> FoV not offered). Unit case "EnchantedEvening provider folds". A
  list that adds a cast-count payoff must drop both. A SWEEP-SELF paid with every land (clause 2 then finds no
  untapped land, so it is a no-op too) is NOT folded: emission cannot see the payment.
* **The playout no-op drop (same commit; ON, no lever):** beyond the horizon an unpinned FoV whose pick is the empty
  set, and a Verse whose KEEP the provider withdrew, are not cast. A playout-policy change, so it was measured on a
  HELD-OUT block (200 games, seed 9000, d3 b10, search folds ON in both arms): drop ON 6.92 vs OFF 6.93 (paired
  -0.01 +- 0.03 turns; 17 faster / 16 slower; 14 unwon in each). Neutral, so it stays as wasted-work removal and the
  temporary lever was deleted. It is generic code (any provider) but reachable only by these two params.
* **Sanity (24 games, seed 7000, d3 b10, no profile; step-2 baseline rebuilt at 31a923ce7 and re-run with every game
  reporting units, `MTG_SLOW_GAME_MS=1`):** baseline 23/24, avg 6.625, 13.08M units. Before the folds: 23/24, avg
  6.6667, 15.31M units (+17.0%); 16 games moved, faster gi=3 (7->6) and gi=22 (7->6), slower gi=12 (6->8) and gi=13
  (6->7). After the folds: 23/24, avg 6.7083, 14.22M units (+8.7%); faster gi=3 (7->6), slower gi=5 (7->8), gi=12
  (6->7), gi=13 (6->7). The unwon game is gi=20 in every arm. Work: the big cost is not the FoV fan itself -- a game
  that holds a Verse/FoV all game and never casts it (gi=16) went 8.7K -> 96K units, and games that never HOLD one
  at a turn end (gi=6 +113%, gi=14 +0.9..8%) moved too: the search's deeper plies and the playout now draw and cast
  live copies of 8 cards that used to be dead. Per-game units: `build/scratch/ee/cmp35.py` (pre-fold) /
  `cmp35f.py` (post-fold).
* **Slower games at d3 b0 (unlimited), step-2 base vs new:** gi=12 base T6 (139K units), pre-fold T6 (246K), post-fold
  T7 (377K), post-fold with the playout drop OFF T7 (362K); gi=13 base T6 (155K), pre-fold T7 (4.84M), post-fold T7 (3.12M), post-fold drop OFF T7 (3.15M). gi=13's divergence:
  the base line sacrifices Sterling Grove at the T2->T3 boundary to put Enchantress's Presence on top and casts it
  T3; the new line keeps the Grove, draws a Forest and casts a no-op Verse T3 (post-fold still a Verse on T4), then
  runs the same sequence a turn late. NOT ROOT-CAUSED (handed on, below). gi=12's b0 T6 -> T7 is the search-window
  folds' doing (it stays T7 with the playout drop off). Those folds are sound at the board level, so they can move a
  d3 result only where the d3 leaf scores the dominated no-op cast ABOVE not casting it: a leaf / horizon defect to
  find (step 8), not a reason to unfold. The regression bar is d8 b0, which these
  games were not run at (cost); EE has no play setting yet.
* **For step 4 (Parallax Wave):** reuse `DestroyRemoveTogether` / `DestroyFireTogether`'s two-half shape for the exile
  (EXILE, not destroy: no indestructible check, no graveyard, a linked exile list instead), and OR its param into
  the routing signature and `revpkg::CardCanEndOwnEnchantment`.
* **For step 6 (fairness check):** clear the virtual board inside `ResolveCalmingVerse` after clause 1 (design §3.4.3,
  comment placeholder there), and add both params to `DeckMayLockOpponent` / `DeckLockNeedsLandStrip` (design
  §3.2/§3.3).
* **For step 7 (FoV pitch):** reuse `ResolveDestroyUpToArtEnch` + `DestroyUpToArtEnchOptions` in the end-step window,
  pinned on the BoundaryFan digit; nothing here assumes our turn except Verse's KEEP hook (active player, fine for a
  sorcery). The EE provider's empty-set fold applies to the pitch too (a pitched FoV with no target only costs a card).
* **For step 8 (folds):** FoV own-target dominance (e.g. our tapped land vs untapped) and Verse KEEP / SWEEP
  coincidence when every land pays the Verse; and root-cause gi=13 (seed 7013, game-index 13, d3 b0): why "no Grove
  tutor + no-op Verse" outscores "tutor Presence + Presence" at the T2->T3 boundary.

## Lock-cluster step 4 (Parallax Wave, §4.4 + the USER ruling-7 end-step sweep) -- notes for the later steps

What was built (code f855925a7 WIP + the final commit): Parallax Wave in `cards.json` (`fading_counters` 5,
`fade_exile_target_creature`); FADING is now put by the shared enter cascade on every NON-cast enter route (the cast
sites already put it pre-push), so a Wave returned by its own leave trigger re-enters with five; the activation is
`PermAbilityMode::FadeExileCreature` (no {T}, cost = one fade counter, CR 602.2b), one searched Action per admitted
target (identity-class fold via `CastTriggerTargetFoldKey`; the source never folded; human play = full legal set), the
exile LINKED to the Wave (`kExileUntilLeavesBase` marker; an order-free digest of the linked set in
`Permanent::linked_exile_number`, so every existing key and fold separates linked sets) and returned by the shared
`ReturnExiledUntilLeaves` on ANY leave route; a token exiled ceases to exist; target legality = creature (printed or
animated), no shroud (`PermanentHasShroud`, so Sterling Grove's grant counts), no opponent hexproof. The USER ruling-7
deck default (`EnchantedEveningProvider::WaveOpalSweepRule` -> `GameState::deck_has_wave_opal_sweep`, needs Wave +
Opalescence in the list) runs `PerformWaveOpalSweep` in BOTH opponent_turn lambdas right after the off-main alt-win,
before the rest of the end-step window and before `freeturn::NoteOpponentTurnEnd`.

The sweep, on the targeting legality (never a counter formula):
* **Loop possible** (0 Groves, or 1 Grove which is exiled first): cycles of (enablers, linked) -> up to
  `counters - enablers - 1` opponent creatures exiled FOR GOOD (their activations resolve after the old Wave left,
  CR 607.2a / 400.7) -> the Wave exiles itself and returns with five; repeats while a target remains. Unlimited.
* **Loop impossible, targets left (2+ Groves; or too few counters for enablers + self)** -- ORCHESTRATOR DEFAULT
  2026-11-26, a USER question below: ruling 7 LITERALLY. The Waves spend the counters they have, one LINKED exile per
  targetable opponent creature, until targets or counters run out; the rest stay. A Wave spent to zero is sacrificed
  at our next upkeep (CR 702.32a) and its linked cards return then -- the rules' consequence of the default.
  (The previous step-4 agent had made this case do nothing.)
* **No target: TOP-UP** (information, beyond design §4.4.1's "at least one opponent creature" gate): a Wave below five,
  nothing linked, nothing attached, with clean enablers, is looped once (enablers + self) and returns with five. It
  returns exactly what it exiled at the opponent's end step, so it only adds counters (weak dominance); this is how
  "fading never kills a Wave beside an Opalescence" (design §4.4) actually holds.
* The sweep retypes (RefreshTypeStatics + TypeStaticsSbaCheckpoint) BEFORE choosing targets, so a permanent the
  opponent's turn added (a card a fading sacrifice returned, a spawn) is current and a 0/0 dies rather than costing a
  counter; it ends with the same pair.

**OPEN USER QUESTION (step 4; default taken, work continues):** with 2+ Sterling Groves out the self-exile loop is
illegal (each Grove shrouds the others and the Wave). Should the end-step sweep still spend the Wave's counters on the
opponent's creatures (finite, LINKED exiles that return when the Wave leaves; a Wave spent to zero is sacrificed at our
next upkeep and they come back then), or leave the counters for our own turn's searched activations? ORCHESTRATOR
DEFAULT (built, unit-tested): spend them -- ruling 7 literally, because the free-turn lock check right after the
opponent's end step needs it to have no creatures, and an our-turn activation cannot reach a creature that appeared on
the opponent's turn. The same tail covers a single-Grove Wave with too few counters for Grove + self.

Not built, recorded (each has a counterexample, so a fold would be LOSSY -> USER / step 8):
* **F1** (an own-turn self-reset is dominated by the end-step sweep): counterexample -- reset now, then cast a second
  Grove this turn, which makes the end-step loop illegal.
* **F2** (an own-target exile without payoff is dominated): counterexample -- exile our enchantment-creatures, then a
  Calming Verse SWEEP-SELF destroys the Wave and they return (a protection line; KEEP is not strictly better since it
  spends main-2 land mana).
* **No our-turn "loop batch" action** (permanent exile on our turn): the end-step sweep reaches the same state before
  the only reader (the lock check), and explicit single activations compose the self-reset correctly.

Notes for the later steps:
* **Step 6 (fairness check):** clear the VIRTUAL opponent creatures inside `PerformWaveOpalSweep` (comment hook
  there); treat the sweep as able to clear them only when `WaveSelfTargetEnablers` succeeds (the loop) -- not a
  `1 + G` counter formula (the finite tail is deleted, ruling 17: no loop -> no exile). A hexproof virtual creature
  is never exiled (ruling 9).
* **Step 7 (FoV pitch):** shares the opponent's end-step window; the sweep runs BEFORE it.
* **Step 8 (folds):** F1 / F2 above, identity folds of OWN targets, and the branching cost the activation adds (one
  action per admitted target at every main).
* **Step 9 (viewer):** a decline entry for the sweep (`eot_cast`), and human-play wiring of the activation (today
  `main_phase` plan variants; `exile_target` in the JSON).
* A fading sacrifice of an animated Wave goes through `PerformUpkeepFading`'s plain erase (no `OnCreatureDies`): no
  dies-reader in this list, noted.

## Ruling 15/16/17 prune (Grove copies, tutor targets, no finite sweep tail) -- 2026-11-25

Code: 0e80072ed (rulings 15/16: `MTG_EE_USER_PRUNE`, heurarm `EE_USER_PRUNE`, DEFAULT ON), fe22e545f (ruling 17: the
sweep's finite tail DELETED), 6840da137 (comment), de901bc9e (amendment arm `MTG_EE_GROVE_PRELOCK`, heurarm
`EE_GROVE_PRELOCK`, DEFAULT OFF). All in `EnchantedEveningProvider` (DecisionProviders.cpp "the USER's Grove / tutor
prune") except ruling 17 (`PerformWaveOpalSweep`, SpellEffects.h). Units 4225/4225 (9 new,
`test/unit/test_enchanted_evening_grove_prune.cpp`: A, B1-B7, lever off, amendment; lock4 sweep cases updated for
ruling 17 + a 1-Grove/2-counter loop case). Smoke on 0e80072ed: 247 passed / 0 failed / 0 new, play-changed=0 (searched
and d0). Smoke on de901bc9e: 247 passed / 0 failed / 0 new, play-changed=0 (searched and d0).

**What is built (the action model).**
* (A) Grove copies -- `PlanListFilterLive` / `PlanListAdmit` (every plan enumeration, root + breakpoint continuation,
  main 1 + main 2): a plan that casts Sterling Grove is kept only when `ours + Grove casts - Grove sacrifice
  activations in the plan <= 1`. A Grove sacrifice (`SacTutorTop`) is in a plan as a LEAD activation (before every
  cast: the Presence draw off the second Grove takes the tutored card) or a TRAIL one (after the casts): both satisfy
  "only play the second one when sacrificing the first". The activation is emitted only while its {1} is payable and
  the tutor keeps a target, so an unsacrificeable Grove blocks the second copy (the lost-Presence-draw case). Not
  restricted: the beyond-horizon playout (permitted greedy); a breakpoint continuation entered after the second
  Grove's cast may still hold two (the sweep then does nothing, ruling 17).
* (B) Tutor targets -- `TutorCandidates` for an enchantment tutor (Grove's sacrifice tutor, Idyllic Tutor; upkeep
  window included): (1) missing lock pieces, EE > Opalescence > Parallax Wave (EE is in both lock routes and is the
  5-drop; Opalescence is the land lock's other half and the Wave loop's animator; the Wave locks only beside
  Opalescence); (2) ONE mana Aura while mana is needed; (3) Presence while no lock piece is fetchable or mana is
  needed; nothing else; no copy of anything in hand or on our battlefield. Mana is counted as the engine credits it:
  each of our lands one unit (an uncracked fetch one unit of what it finds), each land Aura on our land its extra
  unit (Utopia Sprawl its LOCKED colour), tapped or not. "Needed" = total < 4 or white < 2; at exactly 4 (with 2
  white) only when no lock piece is fetchable; at >= 5 with 2 white never. Mana Aura = Wild Growth, or Utopia Sprawl
  when short of white with a Forest host (or a fetch that finds one) or with 2+ Forests; whichever is left if only
  one is. Empty list -> Idyllic Tutor is not cast (`TutorNeedsCandidate`), the Grove is not activated; the idle pass
  is untouched (unit-tested).
* Ruling 17 -- with the self-exile loop impossible (2+ Groves, or 1 Grove and fewer counters than Grove + self) the
  end-step sweep spends NO counter. With the prune ON, 2+ Groves do not arise at turn ends in any measured game (0 of
  224 ON games); they still arise with the lever OFF (57 turn ends in 200 games) and under the amendment arm (52 in
  200), and via a continuation after a second Grove's Presence draw (not seen).

**Measurements (d3 b10, no profile; one pooled batch per sample, arms paired per game).**

| sample | arm | won | avg turn | Munits |
|---|---|---|---|---|
| sanity seed 7000 (24) | step-4 base (CLI) | 22 | 6.7083 | 18.54 |
| | OFF (ruling 17 only) | 22 | 6.7083 | 18.55 |
| | ON | 22 | 6.7083 | 6.83 (-63%) |
| held-out seed 8000 (100) | OFF | 95 | 6.7500 | 57.38 |
| | ON | 93 | 6.8100 | 28.74 (-50%) |
| | ON + amendment | 93 | 6.7700 | 30.18 |
| held-out seed 10000 (100) | OFF | 94 | 6.8000 | 59.54 |
| | ON | 95 | 6.8400 | 25.27 (-58%) |
| | ON + amendment | 95 | 6.8200 | 27.22 |
| both held-out (200) | OFF / ON / ON+amend | 189 / 188 / 188 | 6.775 / 6.825 / 6.795 | 116.9 / 54.0 / 57.4 |

Ruling 17 alone moved no game of the sanity set (OFF == step-4 base per game; units +0.05%). ON vs OFF net: sanity 0
(gi=2 faster, gi=23 slower), seed 8000 +6 turns (10 slower, 4 faster), seed 10000 +4 (11 slower, 7 faster). ON +
amendment vs OFF: +2 and +2. Neither arm meets the "net <= 0 per deck" adoption bar on this sample; the unit cost
halves.

**Slower games at seed 8000 (ON vs OFF) and their d3 b0 repros (ON arm):**

| gi | OFF b10 | ON b10 | ON b0 | first divergence | verdict |
|---|---|---|---|---|---|
| 4 | 8 | unwon | running at commit (`build/scratch/ee/gp/b0.err`, job `b0ho1g4`) | T6: ON casts Wild Growth + Grove, tutors EE (locks T7, 3 life short at T8); OFF casts Verse + Grove | search choice, no denied option at the root |
| 8 | 6 | 7 | 6 | T1 land: ON Forest, OFF Foothills | recovers (budget) |
| 14 | 7 | 8 | 7 | T6: ON Verse + Grove; OFF Wave + Grove | recovers |
| 19 | 5 | 6 | 6 | T1 land order; at b0 T3: OFF Idyllic -> Sterling Grove (B1 refuses) | NOT recovered -- Grove as a 2/2 Opalescence attacker |
| 32 | 7 | 8 | 7 | T2 Idyllic: OFF Sterling Grove, ON Opalescence | recovers |
| 45 | 5 | 6 | 5 | T2 land: ON Foothills, OFF Forest | recovers |
| 48 | 7 | 8 | 7 | T4: ON sacrifices the Grove for Opalescence | recovers |
| 55 | 7 | 8 | 7 | T1 land | recovers |
| 91 | 6 | 7 | 7 | T3: OFF casts the SECOND Grove (rule A refuses); both Groves attack under Opalescence | NOT recovered -- same mechanism |
| 92 | 8 | unwon | 8 | T1 land | recovers |
| sanity 23 | 6 | 7 | 6 | T5: ON sacrifices the Grove for Wave | recovers |

Root cause of the two that do not recover: the deck also wins by an Opalescence BEATDOWN, where a Sterling Grove is a
cheap 2/2 attacker (MV 2). The prune's reason (two Groves make the Wave loop impossible) only binds once the lock is
assembling. The amendment arm `MTG_EE_GROVE_PRELOCK` (DEFAULT OFF) restores both lines while Enchanted Evening is not
ours and an Opalescence is ours or in hand (the second Grove may be cast; Sterling Grove is the LAST tutor target).
It recovers gi=19 (6 -> 5), gi=91 (7 -> 6), and also gi=8, 45, 92 at b10 (5 faster, 1 slower vs ON at seed 8000;
-2 net vs ON at seed 10000). The seed-10000 slower games were not b0-checked (an extra sample).

**Prune firing (real tutor resolutions, rebuilt from the traces by `build/scratch/ee/gp/prune_stats.py`; the replica
agrees with the engine on 120 of 124 ON resolutions -- the other 4 are fetch-crack timing / Sprawl colour, which the
log does not carry):**
* OFF arms (200 games, 116 resolutions): 65 picks the rule would refuse -- 17 never-offered (B1: 15 Sterling Grove, 1
  Exploration, 1 Oblivion Ring), 29 duplicates (B2: 18 Opalescence, 4 Wave, 3 EE, 4 Presence), 9 mana Auras with mana
  fine (B4), 5 the other mana Aura (B3), 5 Presence while a lock piece was fetchable and mana fine (B5).
* ON arms (200 games, 115 resolutions): picks Opalescence 55, EE 32, Wave 9, Sprawl 12, Wild Growth 3, Presence 4.
* The 4-mana band (exactly 4, >= 2 white): 32 ON resolutions -- Opalescence 24, EE 3, Wave 1, Presence 1, Sprawl 3
  (the Sprawl / Presence ones are all-pieces-held boards). OFF fetched mana in that band 1 of 20 times.
* Grove-copy rule: OFF cast a Grove beside one already ours 35 times (14 phases also held a Grove sacrifice); ON 17 times, every one
  beside a sacrifice in the same phase. Turn ends with 2+ Groves: OFF 57, ON 0.
* Mana-efficiency / Presence (USER follow-ups 1-2): ON turn ends before the lock with a Grove ours, a Grove in hand
  and >= 2 untapped lands (a second Grove would have used the mana): 8 in 200 games (2 with Presence out = 2 lost
  Presence draws); OFF 0. Turn ends with EE in hand, uncast, at exactly 4 mana (the 5th mana as the holdup): ON 134,
  OFF 119 (+13%), amendment 132. Presence fetched: OFF 11, ON 4.

**Slower-game classification (2026-11-26, d3 b0 at de901bc9e; full write-up `logs/ee/prune-classify.md`,
scratch `build/scratch/ee/pc/`).** Seed-8000 gi=4 recovers at b0 on both arms (ON 8, amendment 8 = OFF b10 8).
Seed 10000: ON slower in 11 games, amendment in 8; at b0 all but two recover. The two that do not (both arms 7 vs
OFF 6; OFF b0 also 6):
* gi=50 -- PRUNE GAP B2, not clairvoyance. OFF: Opalescence from hand, a Grove tutors a SECOND Opalescence to the
  top, the two animate each other as 4/4s beside the 2/2 Grove(s); every card used is in hand or the Grove's own
  tutor. ON (rule A + B2) and the amendment (B2) cannot take the duplicate. The pre-lock Opalescence beatdown again;
  proposed: inside `MTG_EE_GROVE_PRELOCK`, admit a second Opalescence as a tutor target while EE is not ours and an
  Opalescence is ours.
* gi=44 -- CLAIRVOYANCE. OFF Idyllic -> Presence (B5) instead of the missing Opalescence, and wins only because the
  post-shuffle library delivers Opalescence and an extra Idyllic / Exploration on time (the b10 kill is exactly 20).
Totals, ON (22 slower incl. sanity gi=23): 18 budget noise, 3 prune gap (8000 gi=19, 91 -- fixed by the amendment --
and 10000 gi=50), 1 clairvoyance. Amendment (14 slower): 12 budget noise, 1 prune gap (gi=50), 1 clairvoyance.

## Lock-cluster step 6 (free-turn integration + the USER fairness check, §3.2-§3.4) -- 2026-11-25

Code: 4a676a477 (WIP: trait, no reach certificate, `MTG_FREE_TURN_EE_FAIR` virtual board, key folds), ff0c0014a (test
setup fix + umbra armor), 9571ebde7 (measurement-only log tags). Units 4233/4233 (9 new cases in
`test/unit/test_enchanted_evening_lock6.cpp`; the 8 `EE lock step 6*` / `EE fairness*` cases RAN, `-tc` count 8/8).
`audit_card_fields.py` offline rc=0 (hard fields match). Smoke (Release, `MTG_PGO=0`, ff0c0014a): 247 passed / 0
failed / 0 new, play-changed=0 (searched and d0), scenarios 677/677 -- every other deck unmoved (masks zero for them).
9571ebde7 only adds tags to an env-gated stderr line; the step-6 batch rerun on it is digest-identical per job.

**What is built.** `GoldFishRunner::DeckHasEeLock` (EE + one of Opalescence / Calming Verse / Force of Vigor, main or
wishable side) joins `DeckMayLockOpponent`; `DeckLockNeedsLandStrip` is false for it (no reach certificate);
`deck_has_ee_lock` is stamped from it. `freeturn::LockHolds = LockHoldsOnBoard && eefair::Veto == kNone`. The virtual
board (`opp_virtual_board` / `opp_virtual_spent`, one 64-bit mask per head, folded nonzero-gated into the sim key, the
dominance signature and the route signature) records, after every opponent land turn (both worlds, and the executor's
on-the-draw turn 0), what the head could cast with its lands that turn: one mana budget per turn, hand left to right,
non-Aura nonland permanents first, then creature Auras that have a host. Cleared only by removal we actually cast
(ruling 8): Calming Verse (every enchantment-now, i.e. all under EE; indestructible stays), the Wave + Opalescence sweep
(targetable virtual creatures cost a counter each; ruling 17 unchanged: no loop, nothing), and Force of Vigor against
virtual Auras (ruling 13, a SEARCHED pin appended after the board options, only while a hexproof threat exists).
Veto clauses: 1 = a virtual creature now; 2 = a hexproof creature reaching power >= 5 with trample, counting every
virtual Aura of its head (rulings 9 / 10, layers 7b + 7c); 3 = a hexproof creature with no able Wave loop. A hexproof
non-threat under an able loop is contained.

**Rules review (mtg-rules skill).** Checked: hexproof only stops opponents' targeting (CR 702.11b; the opponent's own
Auras may target it; Verse does not target); Opalescence sets base P/T = MV in layer 7b, Aura bonuses on top in 7c, an
MV-0 one dies as a 0/0 (704.5f); Auras are never animated; an Aura needs a host (303.4a); under EE + Opalescence an
Equipment is a creature and cannot equip (301.5c), so Equipment never adds reach; indestructible survives the Verse.
**One rules fix (ff0c0014a): umbra armor (CR 702.89a).** A "destroy all" does not destroy the creature an umbra
enchants; the umbra is destroyed instead. The pool's Auras list holds Lion / Hyena / Spider Umbra next to Slippery
Bogle and Gladecover Scout, so the old `VerseClears` wrongly erased a hexproof creature that survives. Now, with a live
virtual umbra, ONE virtual creature (the hexproof one if any) survives the Verse bare; its Auras are cleared. New param
`aura_umbra_armor` (Hyena, Spider, Lion, Crab, Indrik Umbra), read only here. Unit case added.
**Test fix (not engine):** the lock6 fixture's all-Forest deck stamped `deck_has_enchantment_shroud` false, so two Groves
never shrouded the Wave in the two ruling-17 cases; the EE list holds Grove, so the real stamp is true.

**Disclosed gaps (all on the opponent's side unless stated):**
* Rancor's return to hand is not modelled: a destroyed virtual Rancor is spent (generous to us; under the full lock the
  opponent has no land to recast it).
* Granted hexproof / shroud on a virtual creature (Alpha Authority, in the Auras list; Lightning Greaves) is not
  modelled: such a creature is treated as targetable and swept (generous to us).
* Evasion is not part of the hexproof test: a Bogle with Gryff's Boon (flying) or Spirit Mantle (protection from
  creatures) cannot be chump-blocked, which ruling 9's containment argument assumes (see the USER question below).
* Daybreak Coronet / Lion Umbra host restrictions ignored, and every virtual Aura of a head assumed on its one
  creature (generous to the opponent).
* Virtual mana (a recorded mana creature, a recorded Wild Growth) does not raise later budgets (generous to us).
* Oblivion Ring cast by us is not credited against a virtual permanent (agent default; USER question below).
* Design §6's scenario fixtures for the three lock configurations are not built (the unit cases cover each clause).

**Measurement** (ONE pooled batch, `build/scratch/ee/l6/m6b.*`; d3 b10, no profile, `MTG_EE_USER_PRUNE` ON,
`MTG_EE_GROVE_PRELOCK` OFF; arm 1 = fairness check ON (shipped), arm 0 = OFF (credit on the board alone); baseline = the
rulings-15/16/17 prune-ON runs, no free-turn credit for this deck). avg = credited turn, unwon = 9.

| family (seed) | baseline won / avg / units | ON won / avg / units | ON free turns (games) | OFF won / avg / units | OFF free turns (games) |
|---|---|---|---|---|---|
| sanity (7000 x24) | 22 / 6.7083 / 6.83M | 24 / 5.7083 / 4.77M | 49 (20) | 24 / 4.7083 / 3.18M | 72 (23) |
| held-out (8000 x100) | 93 / 6.81 / 28.7M | 95 / 5.86 / 16.5M | 200 (74) | 97 / 5.19 / 10.9M | 258 (90) |
| held-out (10000 x100) | 95 / 6.84 / 25.3M | 97 / 5.85 / 16.0M | 231 (83) | 99 / 5.05 / 13.1M | 302 (98) |

* ON vs baseline: about one turn faster in every family. ON vs OFF: the check costs 1.00 / 0.67 / 0.80 turns (ON slower
  in 16 / 57 / 60 games, faster in 0 / 1 / 2). The cost is mostly NOT executor vetoes: the board lock forms later in ON
  in 17 / 55 / 61 games, because the search (whose rollouts apply the same check) no longer completes an EE +
  Opalescence lock that a castable creature would break. It waits for the Wave or a Verse, or just attacks (hb gi=3: the
  opponent could cast Kor Spiritdancer from round 1; ON never locks and wins T6, OFF locks round 4 and wins T4).
* Real-game vetoes (a board lock the virtual board refused; bottom-trial playouts excluded by the new tags): sanity 0;
  8000: 5 turns in 5 games (seeds 8016 Galadriel of Lothlorien, 8021 Neverwinter Hydra, 8031 Kumena's Speaker -- that
  game still got 4 free turns later, 8053 Light-Paws, 8060 Ruby Medallion); 10000: 6 turns in 4 games (10006 Sigarda's
  Aid, 10009 Dragon Tempest, 10027 Akoum Hellhound x3, 10047 Street Wraith). EVERY real veto is clause 1 (a castable
  creature, or a castable artifact / enchantment that Opalescence animates); clauses 2 and 3 never fired in a real
  game (clause 2 once in a sanity bottom trial: Gladecover Scout). The virtual board was non-empty in 21 / 92 / 93 of
  the ON games.
* Reach audit (`MTG_FREE_TURN_REACH_AUDIT=1`): no `[free-turn-reach]` line at all (checks = 0). The certificate is
  never consulted for this deck, so it certifies nothing.
* Slower than baseline: sanity gi=1 (6 -> 7) and gi=3 (6 -> 7); none in either held-out family. Both diverge at the T1
  land choice (Temple Garden vs a fetch), budget noise at b10. d3 b0 repros (pinned ff0c0014a binary,
  `build/scratch/ee/l6/b0-*`): gi=3 ON = T6 with 2 free turns (789 s, 6.51M units) -- RECOVERS. gi=1 ON = T5 with 2 free turns (2352 s, 19.4M units) -- RECOVERS (better than both b10 runs). Same-binary baseline at d3 b0 (`MTG_FREE_TURNS=0`): gi=1 T6 (32.1M units), gi=3 T6 (3.77M units), so ON is one turn faster on gi=1 and equal on gi=3 at unlimited budget. Neither slower game needs a fix.

**USER questions (defaults taken, work not blocked).**
1. Clause 3: with no able Wave loop (ruling 17), a hexproof creature the opponent could have cast vetoes the lock even
   if it is not a threat (ruling 9's containment needs the loop's "blocker exiled and replaced"). Default: vetoes.
   It never fired in this measurement.
2. Should an Oblivion Ring that we actually cast be able to exile a virtual permanent (ruling 8: "only removal we
   actually cast")? Default: not built (only Verse, the sweep and FoV count).
3. Evasion: ruling 9 contains a hexproof creature below 5 power with trample because it can be chump-blocked forever.
   The Auras list also has Gryff's Boon (flying) and Spirit Mantle (protection from creatures); our animated
   enchantments have neither flying nor reach, so such a creature cannot be blocked and attacks freely. Should an
   evasive hexproof creature also break the full lock (for example at any power, or only above some clock)? Default:
   ruling 9 literally (trample and >= 5 only).

## Lock-cluster step 7 (Force of Vigor clause 1: the off-turn cast at the opponent's end step, §4.5) -- 2026-11-25

Code: a26942ea0 (param `alt_cost_exile_green_off_turn`, stamp `deck_has_eot_fov_pitch`, the window, the shared pin
builder, `MTG_EE_FOV_PITCH_LOG`, 6 unit cases) + the step-7 WIP commit after it (this section, the Cards-table row, a
TEMPORARY A/B lever `MTG_EE_FOV_EOT_OPP_ONLY`, default OFF, to be deleted after its A/B). Units 4239/4239 on
a26942ea0 (the 6 `EE lock step 7*` cases RAN, `-tc` count 6/6). Coverage (`analyze_deck.py --coverage-only`): 18
cards, every one `full`, missing none. Smoke (Release, `MTG_PGO=0`, a26942ea0): 247 passed / 0 failed / 0 new,
play-changed=0 (searched and d0), scenarios 677/677. Every other deck is unmoved (the stamp is false for them).

**What is built.** The morph window (`PerformOpponentEndStepFaceUp`, both `opponent_turn` lambdas, AFTER the ruling-7
`PerformWaveOpalSweep` and before `freeturn::NoteOpponentTurnEnd`) gets a FoV half: `EotFaceUpMenu` follows each flip /
put entry with its FoV options (`EotFovOptions`, built on the state that entry leaves), "no cast" FIRST, so rank 0 is
the front (the playout never casts there) and the existing BoundaryFan face-up digit, `Plan::eot_face_up_choice`,
`scripted_eot_face_up` and `ChooseRealCrossingFaceUp` carry it unchanged (`FaceUpUncoveredPending` ORs in the stamp and
a FoV in hand). Options = {target pin} x {pitched green card class}: the pins are the main-phase cast's EXACT menu
(`DestroyUpToArtEnchSearchPins`, extracted verbatim from TurnSolver's FoV post-pass: identity-folded real targets the
provider admits -- the EE provider drops the empty set -- the provider's pick first, then the fairness check's
virtual-Aura pins, ruling 13); the pitch classes = distinct NAMES of the green cards in hand other than this FoV (a
second FoV is legal). SOUND FOLD: when {2}{G}{G} is payable from what we left untapped, the cast is offered mana-paid
only (the mana has no other use at the opponent's end step; premise true of this pool: no source costs life,
sacrifices itself or stays tapped). Only inside `OppEndStepWindowScope` ("if it's not your turn"). HUMAN PLAY: not
offered yet (the viewer's `eot_cast` decision is step 9; `audit_viewer_decisions.py` now EXPECTS `eot_cast` for the
param -- an owed MISS until step 9).

**Rules review (mtg-rules skill + CR).** Alternative cost (CR 118.9: replaces the mana cost, one alternative cost per
spell, mana value stays 4), chosen at 601.2b, targets at 601.2c before costs are paid at 601.2h; the card is on the
stack from 601.2a, so it cannot exile itself, while another FoV copy is a legal green card; "green" = the hand card's
colour (lands are colourless); "if it's not your turn" holds only inside the opponent's turn; an instant may be cast in
its end step; mana empties between steps (CR 500.4: the window pays from untapped sources only, pools saved and
restored); each target re-checked on resolution (608.2b) through the shared `ResolveDestroyUpToArtEnch`; the card goes
to our graveyard last (608.2n); the pitched card to exile; Grove's shroud and an opponent's hexproof honoured by the
shared legality test. Not modelled (no reader in this list, disclosed): the off-turn cast does not bump our
spells-cast counters (Presence counts enchantment spells only). No violation found.

**Measurement** (ONE pooled batch `build/scratch/ee/l7/m7.*`, d3 b10, no profile, `MTG_EE_USER_PRUNE` ON,
`MTG_EE_GROVE_PRELOCK` OFF, fairness ON; baseline = step 6's fairness-ON arm, `l6/m6b` san1 / ho1 / hb1; analyser
`l7/an7.py`). avg = credited turn, unwon = 9.

| family (seed) | step 6 won / avg / units | step 7 won / avg / units | faster | slower | real FoV end-step casts (games) |
|---|---|---|---|---|---|
| sanity (7000 x24) | 24 / 5.7083 / 4.77M | 24 / 5.6250 / 5.19M (+8.8%) | 2 | 0 | 3 (3) |
| held-out (8000 x100) | 95 / 5.86 / 16.5M | 95 / 5.89 / 19.4M (+17.6%) | 2 | 5 | 17 (15) |
| held-out (10000 x100) | 97 / 5.85 / 16.0M | 98 / 5.76 / 19.8M (+23.4%) | 9 | 2 | 19 (17) |

* **Every real cast was MANA-PAID (39 of 39); the pitch was never chosen** (it is offered: unit cases; the search
  just never preferred exiling a card). Targets: opponent permanents 23 (Mountain 9, Plains 3, other lands), our own 17
  (Wild Growth 4, Sterling Grove 3, Exploration 3, Utopia Sprawl 2, Presence 1, O-Ring 1), virtual Auras 2.
* **Own-target casts look like an optimizer's-curse defect, not a line**: e.g. ho1 gi=4 / 9 / 27 / 44 destroy our own
  Wild Growth / Sprawl at rounds 2-4. The window adds ~10-60 dominated-looking own-target options per boundary while a
  FoV sits in hand, and a noisy d3 leaf can rank one above "no cast". The step-8 own-target fold (design §7 / steps 3+5
  notes) is the place; an opponent-only narrowing is LOSSY and therefore a USER decision (below).
* Slower games, d3 b0 repros (pinned binary `build/Release/mtg-b7`, `l7/b0-*`): ho1 gi=9 T4 (step 6 b10 T4: RECOVERS),
  ho1 gi=44 T5 (=5: RECOVERS), hb1 gi=32 T6 (step 6 T7: RECOVERS, 2542 s). ho1 gi=3 still RUNNING at handoff (> 3 h 50 m);
  ho1 gi=40, gi=84, hb1 gi=90 queued behind it in the same script.
* **A/B arm `MTG_EE_FOV_EOT_OPP_ONLY=1` (m7o, temporary lever): KILLED, a DEGENERATE game.** san1 finished: 24 /
  5.6250 / 4.89M (= the full menu's avg, -5.7% units). ho1 / hb1 were at 96 / 95 of 100 when ho1 gi=9 (seed 8009; 761 ms
  in m7) had run 3.3 h at b10 with 9 of 24 workers busy; killed. Single-game repro under gdb (`l7/g9.*`) reproduces
  it (> 2 min). Stack: an unbounded nesting BoundaryChooseByRollout -> SimulateToEnd -> SolveWithLookahead ->
  BoundaryChooseByRollout ... (40+ levels). **Verdict: a STEP-7 DEFECT.** `FaceUpUncoveredPending` is true whenever a FoV
  is in hand, while the end-step menu may hold NO FoV option (width 1: no target the provider admits, or no green card
  and no mana). For morph / Soulstoke "pending" always meant a real choice; for FoV it does not, and it puts every
  boundary of every nested rollout on the crossing-copy path (`fan.Copies()`). The full-menu arm hides it because own
  targets nearly always make the menu wide and a cast removes the FoV from hand; the narrower arm exposes it. The same
  over-wide pending probably accounts for part of m7's +9..+23% units. Fix owed (handoff): make the pending test
  require a FoV option that can exist at the end step, and/or skip the crossing-copy path when the learned width is 1;
  then re-run m7 and the m7o A/B.

**USER question (default taken, nothing blocked).** Should the end-step Force of Vigor be restricted to opponent
targets? It is a lossy narrowing (an own target can matter: a second Sterling Grove's shroud blocks the Wave loop), so
it is the USER's. Default: the full menu (as committed); the opponent-only arm is measured after the pending fix.

**Pending fix + re-measure (1ad7ca2da, 2026-11-25 late).** `FaceUpUncoveredPending` now ORs `EotFovMayOffer`
(SpellEffects.h) instead of "a FoV in hand". It is a sound over-approximation of "the end-step FoV menu is non-empty",
tested on the state BEFORE the crossing, in the same shape as morph and Soulstoke. Its two conditions:
* PAYMENT: another green card in hand, or {2}{G}{G} payable from untapped sources now.
* TARGET: one of
  * an artifact or enchantment on the battlefield now;
  * a land the opponent may still play that will be one (Enchanted Evening out, or an artifact / enchantment land in
    its dealt list);
  * a virtual Aura the fairness check may deploy (armed, and the list holds a creature Aura and a hexproof card);
  * a Burgeoning put.

Unknown list cards count as everything. Unit case `EE lock step 7: the crossing is pending only when the end step can
offer a cast` (7 sub-cases; each "not pending" also asserts that the window has no option). The old code fails it on
the first sub-case. New seam `TurnSolver::FaceUpPendingForTest`. The temporary lever became a per-job heurarm slot
(`EE_FOV_EOT_OPP_ONLY`), so both arms ran in ONE pooled batch. Units 4240/4240. Smoke (Release, `MTG_PGO=0`): 247/0/0,
play-changed=0 (searched and d0), scenarios 677/677. GT not accepted (nothing moved).

ONE pooled batch `l7/m7b` (full menu: san1 / ho1 / hb1; opponent-only: san1o / ho1o / hb1o), d3 b10, fairness ON;
analysers `l7/an7c.py` and `l7/an7b.py`. The baseline is step 6's fairness-ON arm (`l6/m6b`) on the same games. Columns
are won / avg / units vs step 6; faster and slower are paired per game against step 6.

| family | full menu (post-fix) | opponent-only | faster / slower vs s6 (full; opp) |
|---|---|---|---|
| sanity 7000 x24 | 24 / 5.6250 / 5.19M (+8.8%) | 24 / 5.6250 / 4.87M (+2.1%) | 2 / 0 ; 2 / 0 |
| held-out 8000 x100 | 95 / 5.89 / 19.42M (+17.6%) | 96 finished: 91 / 5.8646 / +8.4%; **4 unbounded** | 2 / 5 ; 2 / 1 |
| held-out 10000 x100 | 98 / 5.76 / 19.73M (+23.2%) | 95 finished: 93 / 5.7474 / **+197%**; **5 unbounded** | 9 / 2 ; 6 / 0 |

* **The fix does not move the full menu's play.** Its digests equal the pre-fix m7 exactly (48cde385 / 7e72cc14 /
  2592585f). Units fell by only 0.00-0.19%, so the over-wide pending was NOT what made m7 cost +9..+23% over step 6.
  That cost is the menu's real width while a FoV is held (own targets). All 39 real casts were mana-paid again: 23
  opponent targets, 17 own, 2 virtual.
* **The opponent-only arm is STILL degenerate after the fix.** 9 of 224 games ran past 15 min at b10 (ho1o gi 9, 62,
  70, 95; hb1o gi 17, 30, 67, 71, 99). In the full menu those same games take a few seconds to minutes. The batch sat
  at 9/24 workers, and I killed it after ~25 min (own experiment). **Root cause** (two single-game probes, `l7/dg-*`):
  * The same games with `MTG_FREE_TURNS=0` finish: gi=9 in 0.7 s, gi=62 in 93 s.
  * A free turn gets the depth give-back (DepthAcrossTurn, and SimulateToEndImpl's `roll_entry_extra`; USER-accepted
    2026-11-21: a free turn is not a new round). So along a projected lock's chain of free turns the rollout depth
    never decreases.
  * Under the opponent-only menu a FoV with a live target stays in hand: under Enchanted Evening the opponent's land is
    a fresh target every turn, and the search rarely casts at it. So every free-turn boundary of every nested rollout
    fans the FoV menu (width W), and the cost grows like W^(free turns). The gdb stack from m7o (13+ nested
    BoundaryChooseByRollout -> SimulateToEnd -> SolveWithLookahead levels) is that chain.
  * The full menu escapes only because a FoV is usually cast soon (on something), which ends the fan.
  * So it is NOT a pending defect any more: the menu really is wide. It is a cost interaction between the step-7 fan
    and the free-turn give-back, and the b10 budget does not bound it. It is latent for the full menu too: any game
    where a FoV with options is held through a projected lock would show it.
  * Owed to step 8, budget-shaped (re-price, never refuse): charge the fan's per-branch rollouts to the decision
    budget (the `ChargeDecisionUnits` shape, inert at unlimited), or find a sound fold for the end-step FoV under a
    locked opponent.
* **Quality, paired on the same finished games.**
  * ho1: opp 5.8646 vs full 5.8958 (3 faster, 0 slower: gi 40, 44, 84 are full-menu own-target casts).
  * hb1: opp 5.7474 vs full 5.7263 (2 faster: gi 32, 90; 3 slower: gi 31, 52, 79).
  * Over the 191 games together the net is about -1 turn in total, i.e. neutral. Opp-arm casts: 20, all mana-paid
    (20 opponent targets, 2 virtual).
  * Verdict: no better in aggregate, and pathological in cost. (**DELETED in step 8a.**) **Recommendation: delete `MTG_EE_FOV_EOT_OPP_ONLY`**
    ("delete, don't lever") and treat the own-target curse in step 8 (own-target dominance fold). The default (full
    menu) stays. The lever is still in the tree, default OFF, until the USER answers.
* d3 b0 repros on the fixed binary `build/Release/mtg-b7f` (= 1ad7ca2da), written to `l7/b0f-*`. The b0 step-6
  baseline is `build/Release/mtg-b6`, built from a6c65504e in the scratch worktree `.claude/worktrees/ee-s6`, written to
  `l7/b06-*`.
  * ho1 gi=40: **T6 = step 6's b10 T6, RECOVERS** (42 s).
  * hb1 gi=90: **T6 vs step 6 at d3 b0 T5, does NOT recover.** Mechanism: the board after turn 4's main 1 is identical
    (EE down, Sterling Grove out, hand = FoV, every land tapped).
    * Step 6 sacrifices Grove at the T5 upkeep for Opalescence, casts it, locks and wins T5.
    * Step 7 keeps Grove, draws Enchantress's Presence, hard-casts FoV in main 1 on two opponent lands (Grand
      Coliseum, Ancient Tomb), and wins T6.
    * So the FoV lines (destroying opponent lands, which also cuts the fairness check's virtual mana) out-rank the
      certain Grove -> Opalescence kill in the d3 b0 search. Likely the optimizer's curse over the wide FoV menu.
      Root-cause in step 8.
  * Earlier verdicts (pre-fix binary; the fix does not move the full menu's play): gi=9 T4, gi=44 T5, hb1 gi=32 T6 --
    all recover. Pre-fix gi=3: **b0 unfinished** (4 h 57 m, killed).
  * (CLOSED in step 8a: ho1 gi=3 b0-unfinished at the 2-h cap; ho1 gi=84 re-run on the 8a tree, T6, recovers. See the step-8a section.)
  * **b0 PENDING at close (left running, owned by this step; the step-8 agent reads them).** Each has a 2-h cap
    (`l7/cap2h.sh`); a cap writes "b0-unfinished" to `l7/b0cap.progress`.
    * ho1 gi=3 on `mtg-b7f`: mtg PID 1994870, out `l7/b0f-ho-3.{out,err}` + `l7/b0f-ho-3/`. Marker: a
      `s7-ho-3 exited|b0-unfinished` line in `l7/b0cap.progress`. Started 23:29 UTC, capped at 01:29 UTC.
    * ho1 gi=84 on `mtg-b7f`: lane PID 2007071 (its mtg child is started on admission), out `l7/b0f-ho-84.*`. Markers:
      `ho-84 done` in `l7/b0f.progress` and `s7-ho-84 ...` in `l7/b0cap.progress`.
    * ho1 gi=84 step-6 baseline on `mtg-b6`: mtg PID 2003192, out `l7/b06-ho-84.*`. Markers: `ho-84 done` in
      `l7/b06.progress`, then `l7/b06.DONE`; `s6-ho-84 ...` in `l7/b0cap.progress`. It had run > 15 min at close.
    * Verdict rule: gi=84 recovers if step 7 at b0 <= step 6's b10 T6, or <= step 6's own b0. gi=3: step 6's b10 T7.
    * Remove `.claude/worktrees/ee-s6` with `scripts/safe_worktree_remove.sh` once the step-6 repro has finished.

## Lock-cluster step 8a (the free-turn standing pass; the Force of Vigor own-target fold; the opp-only lever deleted) -- 2026-11-26

Code: e0e74e894 (WIP: the standing pass, the fold, the tests) + 44303575e (the temporary nesting
diagnostic `MTG_DIAG_NEST` removed, the lever `MTG_EE_FOV_EOT_OPP_ONLY` DELETED, the fold review below, this section).
Scratch: `build/scratch/ee/l8/`.

**Root cause of the step-7 opp-only hang (ho1o gi=9, > 3.3 h at d3 b10).** Every iterative-deepening host assumes its
PASS 0 is cheap: it is the ranking that always stands and that no ceiling may cut. `SolveWithLookahead` arms
`PassCeiling` only for `sub_depth > 0`, and the ten executor crossing nodes (`ChooseRealCrossing*`,
`CrossingPassCeiling`) never armed pass 0. A FREE turn gets the depth give-back at two sites (`SimulateToEndImpl`'s
entry give-back `roll_entry_extra`, and `DepthAcrossTurn`). Along a projected lock the round never advances, so pass
0's depth-0 rollouts become a depth-1 SEARCH at every free turn, and that search's own pass-0 rollouts enter the next
free turn at depth 1 again. Only the game's end or `TurnSeqCapped` bounds the nesting, and each level multiplies by
its candidates and the end-step FoV fan. The budget IS charged, but nothing polls it (nested solves run
`enforce_budget=false`; `Overrun()` is armed only for passes >= 1). Evidence: a temporary diagnostic on ho1o gi=9
(seed 8009) put the escaping host at the executor's real crossing node, pass 0, no overrun limit; rollout nest 6 -> 15
at T4 (seq 10 -> 19, every entry a free turn at depth 0); 709k units against a 9k limit (78x) and climbing.
`MTG_FREE_TURNS=0` finished the same game in 0.7 s.

**The fix: the free-turn standing pass (`MTG_FREE_TURN_STAND`, DEFAULT ON, a re-price).** Under a FINITE budget, on a
deck with free turns, pass 0 of `SolveWithLookahead` (gate root) and of every crossing node is armed at the
proportional ceiling (`kOverrunBudgetMult` = 25 x the budget, FullSearchLine's constant). A pass 0 that trips it (with
no proven win to rescue) is re-run once as the STANDING pass: inside `FreeTurnTrueD0Scope` a free turn takes no
give-back (it counts as a ply, like any other turn), and that ranking stands. The scope is folded into every rollout
memo key, so the standing pass never reads a give-back answer. The gi=9 check (fix + fold, opp-only lever still in):
**T4 (= step 6 b10 T4), 13.8 s, 233k units**, was > 3.3 h.

**Review against the CLAUDE.md rules (step 8a, done by reading the code).**
* **Inert at budget 0: YES, by construction.** `FreeTurnStandArms` requires `budget != nullptr && !Unlimited()`. The
  scope is set only by a re-run, and a re-run happens only after an armed pass 0 trips. With the scope unset,
  `GivesRoundBack(s)` is exactly the old `InSameRoundTurn()` (`in_extra_turn || in_free_turn`), and the memo-key fold is
  skipped. So b0 play is byte-identical, and the b0 hours below are untouched by it.
* **Only re-prices work under a finite budget: YES.** A pass 0 that stays under its ceiling is unchanged unit for unit
  (`Overrun()` never fires below the limit, and `StandTripped()` is false). The ceiling grows with the budget, so a
  larger budget completes the round-complete pass 0 that a smaller one cut. Measured: on all 224 games of the A/B below
  the arm `MTG_FREE_TURN_STAND=0` is **unit-for-unit identical** to the default (no game's pass 0 ever reached 25x). The
  only things the rule moved are the degenerate shapes (ho1o gi=9; the new unit case, where `=0` did not finish in
  116 s and was killed, against 1 s with it on).
* **The fallback re-run only ranks; it never removes an option: YES.** The standing pass walks the SAME candidate list
  (SWL: the full `candidates` vector; crossings: the same `BoundaryFan`). Only the rollout's depth accounting differs.
  Nothing it computes filters a candidate out of a later pass: `best_plan` / `committed_*` are overwritten by any
  completed deeper pass, and passes >= 1 run outside the scope (`ft_stand == 2`, `stand == 2`) with the normal
  give-back, over all candidates. In practice a later pass rarely runs after a trip, because the 25x first try has
  spent the budget. That is the price of the ceiling, and it shrinks as the budget grows. NO-GREEDY: the standing pass
  is still a search over every candidate, with greedy only beyond the horizon (remaining depth <= 0), as before. It is
  not a pick from a provider's top candidate.
* **No unbudgeted-only change: YES.** It exists only under a finite budget, and that is the point: at b0 the same
  nesting IS the USER-accepted depth-in-rounds rule (free turn = no ply, 2026-11-21) and is exhaustive by design (see
  the b0 note below).
* Residual (not a defect today, recorded for 8b): the condemnation escalation's honest re-score runs outside the pass
  loop, unarmed. It runs only when `CondemnsPassedMainPhase()`, which the EE provider does not set.

**The Force of Vigor own-target fold (`EnchantedEveningProvider::FovOwnTargetInert`, a SOUND fold, no flag).** Every
option holding a folded own permanent is dominated by the same option without it, which the "up to two" menu also
lists. The fold applies to both windows (`DestroyUpToArtEnchSearchPins`). `EotFovMayOffer` skips folded own permanents,
so they do not make the crossing pending. It is not applied in human play. The premise holds for this list: nothing in
it reads our graveyard, our permanents dying or leaving, our land count, or our permanent count. **Review per class
(mtg-rules skill + `cards.json`, step 8a):**
* FOLDED, a mana land (`produces`: Forest, Plains, Temple Garden, Sunpetal Grove). It only adds mana. The one reader of
  "an untapped land you control" is Calming Verse's clause 2 ("Then if you control an untapped land, destroy all
  enchantments you control"). Its KEEP variant taps every mana land into the float first, so a destroyed mana land
  never reaches a Verse result that the kept land cannot. Under EE + Opalescence our lands are 0/0 and gone anyway.
  Proven.
* FOLDED, a fetchland (Windswept Heath, Wooded Foothills), only while no Calming Verse is in our hand or library. With
  no Verse, nothing reads it as an untapped land. Its activation is optional, so keeping it is never forced. The deck
  has no recursion, so a Verse in the graveyard cannot return. With a Verse live it is KEPT. Proven.
* FOLDED, a mana Aura (Wild Growth, Utopia Sprawl), **now only while it enchants a land WE control** (tightened in this
  review). Both enchant ANY land ("its controller adds"), so our Aura on an opponent's land feeds THEIR mana, a
  downside that destroying it removes. The AI's host narrowing keeps them on our lands today, but `DecisionUnpruned`
  can lift that. On our own land it only adds mana. Proven.
* FOLDED, Exploration ("You may play an additional land", a "may"). It has no downside. Under Opalescence it is a 1/1
  attacker. Proven.
* **WITHDRAWN from the fold in this review: Enchantress's Presence.** Its draw is MANDATORY ("Whenever you cast an
  enchantment spell, draw a card"). On a short library every enchantment spell we cast draws toward decking (CR 704.5b;
  the engine models it, `player_lost_on_draw`). Destroying the Presence first is then the one line that lets the lock
  piece be cast safely. That is rare, but it is not dominated, so Presence stays a target. (The m8 arm shows the
  OTHER side of this: ho1 gi=16 and gi=73 cast a mana-paid FoV on our own Presence at round 3 and win on the same turn
  as step 7. Step 7's gi=73 hit our Wild Growth instead, which is now folded. This is a tie-indifferent own-target
  cast, the curse step 8b looks at.)
* KEPT, as the WIP had them: Sterling Grove (its shroud blocks the Wave self-exile loop, ruling 17; the second-Grove
  prune counts it), Parallax Wave and Oblivion Ring (leave triggers return the exiled cards), Enchanted Evening (its
  static makes our lands enchantments, for Verse clause 2 and Opalescence), Opalescence (its static kills our lands
  under EE), and every card the list does not name.
* Test updated: `EE lock step 8a: Force of Vigor own-target fold` now asserts that Presence is KEPT and that our Wild
  Growth on an opponent land is KEPT.

**Units: 4245/4245** (3,679,528 assertions). The 5 `EE lock step 8a*` cases RAN (`-tc`: 5 passed, 604 assertions). The
7 `EE lock step 7*` cases RAN, including the edited pending case (own Exploration NOT pending, own Grove pending). The
two bound constants of the endless-lock cases (crossing `>= 25x` and `< 40x` the limit; root `< 60x`) passed as
written. No tuning was needed.

**Smoke (Release, `MTG_PGO=0`, the step-8a tree): 247 passed / 0 failed / 0 new; play-changed=0, searched AND d0;
configs changed 0 / 247. NO deck moves.** That includes the three free-turn decks in the suite (Worldfire, Cyclone,
Shadowborn): no smoke game's pass 0 reached the 25x ceiling, and the fold is EE-provider-only (EE is not a suite deck
yet). GT not accepted (nothing to accept).

**A/B: ONE pooled batch `l8/m8`** (san1 7000 x 24, ho1 8000 x 100, hb1 10000 x 100; d3 b10, user prune ON, Grove
prelock OFF, fairness ON; binary `mtg-f8a2`; 224 games, batch wall 4.5 min), paired per game against step 7's full
menu `l7/m7b` and step 6's `l6/m6b`. Plus a decomposition arm `l8/m8s0` (`MTG_FREE_TURN_STAND=0`, the same batch):
**identical to m8 per game AND per unit**, so every move below is the fold's.

| family | n | won 8a / 7 / 6 | avg 8a | avg 7 | avg 6 | sum(8a-7) | units 8a vs 7 | vs 6 | game-s 8a / 7 / 6 |
|---|---|---|---|---|---|---|---|---|---|
| san1 | 24 | 24 / 24 / 24 | 5.5833 | 5.6250 | 5.7083 | -1 | -12.6% | -4.9% | 532 / 687 / 546 |
| ho1 | 100 | 95 / 95 / 95 | 5.8900 | 5.8900 | 5.8600 | 0 | -12.4% | +2.9% | 2501 / 2971 / 2076 |
| hb1 | 100 | 98 / 98 / 97 | 5.7600 | 5.7600 | 5.8500 | 0 | -18.7% | +0.1% | 1818 / 2637 / 2166 |

(avg counts a non-win as 9; the losses are the same games in all three steps: ho1 23 / 30 / 60 / 72 / 89, hb1 69 / 95.)
* vs step 7 (the full menu): 5 faster (san1 gi=1 7->6; ho1 gi=9 5->4, gi=44 6->5; hb1 gi=13 7->6, gi=32 8->7) and
  4 slower (ho1 gi=70 7->8, gi=76 6->7; hb1 gi=16 6->7, gi=79 6->7). Net -1 turn over 224 games. None of the 9 moved
  games casts a FoV in either arm, so the moves are the narrower FoV menu changing how the b10 budget is spent
  (budget churn), not a different FoV line.
* hb1 gi=90 at b10: still T6 (step 6: T5), the same as step 7.
* Slowest game per family: 168 s (ho1 gi=62), 158 s (san1 gi=14), 143 s (hb1 gi=25), against step 7's 183 / 183 / 273 s.
  No hung game. The heartbeat never showed a starved pool (the batch was done before the first 10-min heartbeat).

**d3 b0 repros (ONE pooled batch `l8/b0p`, binary `mtg-f8a2`, 7 games, per-game wall cap 2 h via
`MTG_MAX_GAME_WALL_SEC=7200`).** The lane was full (the other agent held all 4 slots), so the repros ran as the one
big process under the box flock instead of seven 1-slot lane jobs. The bar for each slower game is the faster arm's
b10.
| game | 8a b10 | bar | 8a d3 b0 | wall | units | verdict |
|---|---|---|---|---|---|---|
| ho1 gi=76 | T7 | step 7 b10 T6 | **T6** | 222 s | 4.40M | RECOVERS |
| hb1 gi=79 | T7 | step 7 b10 T6 | **T6** | 178 s | 2.94M | RECOVERS |
| ho1 gi=70 | T8 | step 7 b10 T7 | unfinished (still deciding T1) | > 2 h | 84.8M | **b0-UNFINISHED** |
| hb1 gi=16 | T7 | step 7 b10 T6 | unfinished (reached T3) | > 2 h | 37.4M | **b0-UNFINISHED** |
| hb1 gi=90 | T6 | step 6 b10 T5 / b0 T5 | **T6** | 10 s | 0.21M | does NOT recover (same as step 7) |
| ho1 gi=84 | T6 | step 6 b10 T6 / b0 T6 | **T6** | 547 s | 6.15M | RECOVERS (the step-7 pending repro, on the 8a tree) |
| ho1 gi=3 | T8 | step 6 b10 T7 | unfinished (still deciding T1) | > 2 h | 157.3M | **b0-UNFINISHED** |
* hb1 gi=90 mechanism, unchanged by 8a. T1-T4 are identical to step 6 (Wild Growth, Grove, Wave, Sprawl + EE). At T5
  step 6 casts Opalescence (via the Grove) and wins T5. Step 8a hard-casts Force of Vigor in main 1 and casts
  Opalescence at T6. The FoV targets are opponent lands, which the fold does not touch. This is at b0 (exhaustive),
  so it is not budget noise: the projection rates the FoV line at least as fast as the Opalescence kill. Root-cause it
  in step 8b.
* Step-7 pending repros, closed:
  * ho1 gi=3 on `mtg-b7f`: **b0-unfinished**, killed by its 2-h cap at 01:29:32. The pre-fix run was also unfinished
    (4 h 57 m).
  * ho1 gi=84 on `mtg-b7f`: never admitted (the lane stayed full). Its lane waiter was killed (own experiment) and the
    game re-run on the 8a tree in `l8/b0p`: T6, recovers.
  * ho1 gi=84 on step 6 (`mtg-b6`): T6 (00:18, in step 7).
* **The b0 tractability point (a possible USER question; the rule is NOT changed).** A locked round is exhaustive at
  b0, because a free turn costs no ply. Own-experiment probe `l8/b0nf`: the three unfinished games at d3 b0 with
  `MTG_FREE_TURNS=0`, 30-min cap.
  * ho1 gi=70 **finishes in 0.5 s** (18k units), against > 2 h / 84.8M with free turns. Its b0 cost IS the free-turn
    lock chain. (The T8 is not comparable: with free turns off, locked opponent turns count as rounds.)
  * ho1 gi=3 and hb1 gi=16 are **still unfinished at 30 min** (19.3M / 16.0M units, both still on T1 or T2). Their b0
    cost is something else on this deck at d3 b0, not the free-turn rule. Root-cause it in step 8b, beside the +27%
    units from step 4.
  * So the free-turn rule explains one of the three unfinished b0 games. The USER-accepted 2026-11-21 default (free
    turn = no ply) is unchanged. Counting free turns as plies at b0 would be the USER's call, with these numbers.

## Lock-cluster step 8b (Utopia Sprawl colour fold; Parallax Wave Exploration fold; the dead-land fold WITHDRAWN) -- 2026-11-26

Code: fd439c7af (WIP: three folds, the tests, a temporary census diagnostic) + the close commit (the dead-land fold
DELETED in review, the census diagnostic `MTG_EE_CENSUS` removed, this section). Scratch: `build/scratch/ee/l8b/`.
Handoff of the first agent: `logs/ee/lock8b-handoff.md`.

**The folds as shipped (EE provider only, searched play only, never in human play; sound, default ON, no flag).**
* **Utopia Sprawl's colour axis held to {G, W}** (`EnchantedEveningProvider::EtbChoiceCandidates`,
  `EeSprawlProofBreaks`; the JumboCactuar / Beasts / Hydras precedent). Re-checked against `cards.json` for all 18
  names of the list (one zone, no sideboard, no wish card): every coloured pip is {G}, {W} or Enchanted Evening's
  hybrid {W/U}; every activation cost is generic ({1} for Sterling Grove) or life / a fade counter; nothing reads a
  chosen colour or the colour of mana spent; Force of Vigor's pitch reads a GREEN CARD, not mana. A U mana pays
  generic and {W/U}, both of which W pays; B and R pay only generic. So W dominates U and G (or W) dominates B and R,
  exactly. A land Aura can enchant only our own lands here (`land_aura_can_target_opponent` false), so the colour
  never feeds the opponent. The runtime proof check (any of our cards in any zone with a plain pip outside {G, W}, a
  hybrid with no G/W half, phyrexian / twobrid, or a black or red colour -> full fan) and `MTG_UNPRUNE=etbcolor`
  keep it list-specific. Proven.
* **Parallax Wave: our Exploration withdrawn as a target while no Calming Verse is in our hand or library**
  (`EnchantedEveningProvider::FadeExileCreatureTargets`). Exiling it costs a fade counter and a 1/1 attacker, and its
  absence pays only in design F2 (exile it, then a Verse's clause 2 destroys the Wave and Exploration returns). With
  no Verse left (no recursion in the list; neither tutor finds a sorcery) that line does not exist. Re-checked this
  step: the one other thing the activation buys is a counter spent, i.e. the Wave dying a turn earlier at our
  upkeep. Whenever Exploration is a legal target the Wave itself is too (both are Opalescence creatures, and a
  Sterling Grove shrouds both alike), and the self-exile (kept) returns every linked card at once and costs the same
  counter; nothing in the list rewards the Wave being gone rather than reset. Proven. Every other class stays a
  target, each with its counterexample (code comment): the Wave itself, Grove, another Wave, Oblivion Ring, Enchanted
  Evening / a second Opalescence (ends the lock), Enchantress's Presence (mandatory draw, CR 704.5b), and every
  opponent creature (the free-turn check reads them; the end-step sweep is not always there).
* **The dead-land fold is WITHDRAWN (deleted, not levered).** Under Enchanted Evening + Opalescence a land we play is
  a 0/0 and dies before priority, so the WIP dropped "play the land" for its no-land twin when no mana was left and
  the hand held <= 7. Holding the land does keep every later option in the RULES, but not in this ENGINE: this deck's
  cleanup discard is the provider's ranked pick (highest mana value first, `CleanupDiscardSearchWidth` 1, so a land
  is shed last), and the in-search cleanup does not branch. Counterexample inside a d3 horizon: hand 7 with a Forest,
  two Presences out, a Wave with counters. Played line: the Forest dies (hand 6). Next turn both draw (7 / 8); the
  Wave exiles our own Opalescence (the lock ends), both play a Forest and cast Wild Growth (two Presence draws):
  played ends at 7, held at 8 and sheds its highest-MV card, a card the played line keeps. The WIP's induction
  covered locked turns only; after the lock ends, a turn that uses every land drop and nets a card (Idyllic Tutor or
  any enchantment with Presence out) leaves the held line one card over. The provable narrowings ("no later net card
  gain possible", "the lock can never end") never hold with four Presences, two Idyllic Tutors, four Force of Vigor
  and four Waves in the list, so the fold is deleted. Its census share was small (below). The unit case now pins that
  the land play stays offered under the lock.
  * Possible USER question (only if wanted; the rule is unchanged): a deck discard rule that sheds a land first when
    the hand is over the limit would make the fold exact. That is a discard-policy change and the USER's to make.

**Census (temporary `MTG_EE_CENSUS`, now removed; san1, 24 games, d3 b10; per-enumeration plan counts).**
| count | c0 (step 8a + census) | c1 (all three folds) | change |
|---|---|---|---|
| plans enumerated | 3,395,013 | 2,572,138 | -24.2% |
| enumerations | 284,501 | 225,462 | -20.8% |
| Sprawl plans | 981,322 | 466,358 | -52.5% |
| Sprawl plans with a colour outside {G, W} | 606,332 | **0** | the fold |
| Wave-activation plans | 730,428 | 631,745 | -13.5% |
| ... on our own permanents (not the Wave) | 320,273 | 296,658 | -7.4% |
| ... self / opponent | 175,513 / 339,450 | 163,474 / 274,221 | |
| dead-land plans | 8,927 | 3,286 | (fold withdrawn) |
| lock enumerations / their plans | 3,609 / 24,394 | 2,964 / 16,295 | |
| san1 result, units | 24/24, 5.5833, 4,538,357 | 24/24, 5.5833, 3,063,619 (-32.5%) | |
At a fixed b10 budget the per-class counts mix the fold with budget re-spent elsewhere; the clean figures are the
Sprawl colour copies (606k -> 0: the main waste the census found, 17.9% of all plans) and the total. The final tree
(without the dead-land fold) measures san1 3,142,693 units (-30.8%), so the withdrawn fold was worth about 2.5% of
san1 units. **Residual (the step-4 +27%):** the Wave's remaining branching is genuine. It spends a resource (fade
counters) whose value depends on the rest of the turn, over targets that each have a counterexample above; only the
Exploration sliver was provably wasted.

**Units: 4248/4248** (3,679,880 assertions), on the final binary. `-tc='EE lock step 8b*'`: 3 passed (the dead-land
case replaced by "the land play stays offered"; Sprawl {G, W} + full fan; Wave Exploration fold). `-tc='EE lock step
8a*'`: 5 passed. (On the WIP binary with the census: 4250/4250, 8b 5/5, 8a 5/5.)

**Smoke (Release, `MTG_PGO=0`, the final tree): 247 passed / 0 failed / 0 new; play-changed=0, searched AND d0;
configs changed 0 / 247; scenarios 677/677. NO deck moves** (every change is in the EE provider; EE is not a suite
deck yet). GT not accepted (nothing to accept).

**A/B: ONE pooled batch `l8b/m8b`** (binary `mtg-f8b` = the final tree; san1 7000 x 24, ho1 8000 x 100, hb1 10000 x
100; d3 b10, user prune ON, Grove prelock OFF, fairness ON; 224 games, batch wall ~5 min, so no 10-min heartbeat
fell inside it), paired per game against step 8a's `l8/m8`.
| family | n | won 8b / 8a | avg 8b | avg 8a | sum(8b-8a) | units 8b vs 8a | game-s 8b / 8a |
|---|---|---|---|---|---|---|---|
| san1 | 24 | 24 / 24 | 5.5833 | 5.5833 | 0 | 3.14M vs 4.54M (-30.8%) | 342 / 532 |
| ho1 | 100 | 95 / 95 | 5.9000 | 5.8900 | +1 | 14.10M vs 17.00M (-17.1%) | 1671 / 2501 |
| hb1 | 100 | 98 / 98 | 5.7600 | 5.7600 | 0 | 11.45M vs 16.03M (-28.6%) | 1133 / 1818 |
(avg counts a non-win as 9; the losses are the same games as in 8a.) Net **+1 turn over 224 games**, units -17..-31%.
* Faster (3): hb1 gi=49 7->6, gi=60 7->6, gi=79 7->6 (gi=79 was a step-8a slower game; it is back at step 7's T6).
* Slower (3). Each diverges at an early root decision (T1 land, T2 cast), where the cheaper Sprawl and Wave subtrees
  change how the b10 budget is spent (budget churn; the b0 repros below are the test):
  * ho1 gi=15 5->6: diverges at the T1 land (Forest instead of Wooded Foothills).
  * hb1 gi=31 4->6: at T2 the 8b line casts a **Calming Verse that does nothing** (no opposing enchantment, and the
    payment taps every land, so clause 2 cannot fire) instead of Sterling Grove; EE lands a turn later. That is a
    dominated cast which the step-3+5 fold does not catch: it withdraws Verse KEEP with no opposing enchantment, but
    the SWEEP variant collapses to nothing when no untapped land survives the payment. Owed to step 8c.
  * hb1 gi=32 7->8: T2 Temple Garden + Utopia Sprawl (8a: no T2 play) and a different T3.
* Slowest game per family: 83 s (san1 gi=5), 176 s (ho1 gi=31), 98 s (hb1 gi=17); 8a: 158 / 168 / 143 s. No hung game.

**d3 b0 repros (ONE pooled batch `l8b/b0p`, binary `mtg-f8b`, 6 games, per-game wall cap 2 h via
`MTG_MAX_GAME_WALL_SEC=7200`, under the box flock).** The bar for a slower game is 8a's b10.
| game | 8b b10 | bar | 8b d3 b0 | wall | units | verdict |
|---|---|---|---|---|---|---|
| ho1 gi=15 | T6 | 8a b10 T5 | **T5** | 1072 s | 23.27M | RECOVERS |
| hb1 gi=31 | T6 | 8a b10 T4 | **T4** | 20 s | 0.40M | RECOVERS |
| hb1 gi=32 | T8 | 8a b10 T7 | **T6** | 281 s | 5.24M | RECOVERS (beats the bar) |
| hb1 gi=16 | T7 | step 7 b10 T6 | **T6** | 4420 s | 18.77M | RECOVERS, and now FINISHES at b0 (8a: unfinished at 2 h, 37.4M units, reached T3) |
| ho1 gi=3 | T8 | step 6 b10 T7 | unfinished | > 2 h | 137.3M | **b0-UNFINISHED** (8a: 157.3M at 2 h) |
| ho1 gi=70 | T8 | step 7 b10 T7 | unfinished | > 2 h | 72.2M | **b0-UNFINISHED**, the free-turn lock chain (8a: 84.8M at 2 h; 0.5 s with `MTG_FREE_TURNS=0`) |
* Every slower game of the A/B recovers at d3 b0, so the moves are budget churn.
* hb1 gi=16 is no longer b0-intractable: the step-8b folds removed enough of its tree for the game to finish
  (T6, = step 7's b10) in 74 min.
* ho1 gi=3 and gi=70 stay unfinished at 2 h (abandoned by the wall backstop, `ABANDONED-WALL`); both are step 8c.
  hb1 gi=90 was not re-run (its b10 is T6 in both 8a and 8b, the same as 8a's b0 T6; the non-recovery vs step 6 is
  step 8c).
* The batch ran 6 workers on a 24-core box for 2 h: the tail was the two capped games, and nothing else of this step
  was waiting to run.

## USER rulings 18-21 + prune re-measure -- 2026-11-26

Code: **2df2cebb1** (everything below; rulings verbatim in the rulings list above, moved there in d51f51508). Scratch:
`build/scratch/ee1821/` (`m21.*` the three-arm batch, `b0*.{json,sh,err,out}` + `*-traces` the d3 b0 repros, `an21.py`,
`an_b0.py`, `fir21.py`, `dg.py`, `brief.py`). Handoff of the build agent: `logs/ee/rulings1821-handoff.md`.

**What is built.**
* Ruling 18 (the 4-mana breakpoint): no change; ruling 16's breakpoint stands as built.
* Ruling 19: `eefair::Veto` clause 3 (a hexproof non-threat with no able Wave loop) is DELETED. A hexproof
  non-threat never vetoes, loop or no loop. Clause 2 (trample + power >= 5) applies without the loop too.
  `WaveLoopAble` is no longer read by the veto; Oblivion Ring's `AtCheck` reads it.
* Ruling 21: new clause **4** (`kHexproofEvasive`, the log prints `veto=4`): a hexproof virtual creature our creatures
  cannot block, power >= 1 (ReachPower, virtual Auras counted as in clause 2). `eefair::Unblockable` covers protection
  from creatures (new param `aura_grants_protection_from_creatures`: Spirit Mantle), a colour-conditional unblockable
  `aura_color_bonuses` grant, flying (own keyword or new param `aura_grants_flying`: Gryff's Boon, Arcanum Wings) with
  no flyer / reach of ours, fear, and menace with fewer than 2 creatures of ours. A creature with no evasion is NOT
  unblockable even when we control no creature (that would bring clause 3 back). `ThreatOnHead` (ruling 13's FoV
  window over virtual Auras) opens for clause 4 too, so Force of Vigor can destroy the evasion Aura.
* Ruling 20: an Oblivion Ring we cast may target the opponent's virtual board (`eefair::OringVirtualTargets`), offered
  only when `AtCheck(s)` vetoes and the same board without that card does not, i.e. only when exiling it is what
  makes the lock hold. Never a hexproof creature (O-Ring targets); its Aura can be offered. TurnSolver's O-Ring
  post-pass adds the precondition `freeturn::LockHoldsOnBoard(state)` and emits one pinned `OR<n>` variant per such
  card after the real targets; the unpinned base keeps the real pick, so a virtual target is never forced. The
  resolver marks the card spent and records `GameState::opp_virtual_oring_link` (source, head, bit; in the 4-byte
  hole after `noncombat_trigger_depth`, sizeof(GameState) unchanged at 1776), folded into BuildSimKey,
  dominance::Build and the route signature; `ReturnExiledUntilLeaves` puts the card back on the virtual board when
  that O-Ring leaves. One link at a time.
* Amendment `MTG_EE_GROVE_PRELOCK` (default OFF; adoption is the USER's) extended: `eeprune::SecondOpalExtension`.
  With EE not on our battlefield, an Opalescence ON our battlefield and none in hand, the enchantment tutors (Grove
  sacrifice, Idyllic Tutor) offer a second Opalescence, right after the missing lock pieces (before the mana Aura /
  Presence; the amendment's Sterling Grove stays last). This targets the seed-10000 gi=50 B2 gap of the
  de901bc9e classification.
* Units **4251/4251**; `-tc='EE fairness*,EE prune amendment*'` 10/10 (new: evasion, O-Ring virtual target, second
  Opalescence; the lock6 clause-3 expectations changed to kNone / clause 2).

**Smoke (Release, `MTG_PGO=0`, 2df2cebb1): 247 passed / 0 failed / 0 new; configs changed 0 / 247; play-changed=0,
searched AND d0; scenarios 677/677. NO deck moves** (the new params and the link are read only by the fairness
check). GT not accepted (nothing to accept).

**Three-arm A/B: ONE pooled batch `m21`** (binary `mtg-2df` = 2df2cebb1; 672 games: san 7000 x24, ho 8000 x100, hb
10000 x100, each x {OFF = `MTG_EE_USER_PRUNE` off, ON = prune on, AMD = prune on + `MTG_EE_GROVE_PRELOCK` with the
extension}; d3 b10, fairness ON, no profile). Wall 11 min; the 10-min heartbeat read 9/24 (the tail: the slowest game
was 0.12 h, ho_off gi=31); no hung game. Paired per game; avg counts a non-win as 9.

| family | arm | won | avg | units | game-s | vs OFF: net / faster / slower |
|---|---|---|---|---|---|---|
| san 7000 (24) | OFF | 24 | 5.6250 | 10.23M | 844 | |
| | ON | 24 | 5.5833 | 3.14M | 250 | -1 / 2 / 1 |
| | AMD | 24 | 5.5417 | 3.85M | 326 | -2 / 2 / 0 |
| ho 8000 (100) | OFF | 95 | 5.9100 | 29.55M | 3086 | |
| | ON | 95 | 5.8900 | 13.77M | 1515 | -2 / 10 / 7 |
| | AMD | 95 | 5.8600 | 14.14M | 1560 | -5 / 10 / 5 |
| hb 10000 (100) | OFF | 98 | 5.8400 | 30.53M | 3923 | |
| | ON | 98 | 5.7600 | 11.45M | 1432 | -8 / 12 / 5 |
| | AMD | 98 | 5.8000 | 11.68M | 1244 | -4 / 10 / 7 |
| **all 224** | OFF / ON / AMD | 217 / 217 / 217 | 5.848 / 5.799 / 5.799 | 70.31M / 28.36M (-59.7%) / 29.66M (-57.8%) | 7853 / 3197 / 3130 | ON -11 (24 / 13), AMD -11 (22 / 12) |
| held-out 200 | OFF / ON / AMD | 193 / 193 / 193 | 5.875 / 5.825 / 5.830 | | | ON -10, AMD -9 |

* **Both prune arms now meet "net <= 0" against OFF on every family** (the de901bc9e measurement, before the
  fairness check and steps 7-8b, had ON +10 and AMD +4 on the held-out 200). Same games lost in every arm.
* AMD vs ON, paired: san -1 (gi=1), ho -3 (gi=8, 76, 92 faster), hb +4 (gi=83 faster; gi=6, 12, 60, 79, 85 slower):
  **net 0 over 224**, at +4.6% units. Every AMD-slower-than-ON game recovers at d3 b0 to ON's b10 (gi=6 / 60 / 79 in
  the b0 table below; gi=12 b0 T6 and gi=85 b0 T4 in the lane run `b0amd2`).
* Second-Opalescence extension, real tutor picks of an Opalescence while one is ours and EE is not (from the traces):
  AMD 6 (hb gi=12, 25, 26, 51; ho gi=54; san gi=2), ON 0 (refused), OFF 3. The extension's target game (seed-10000
  gi=50) and the other prior verdict games (10000 gi=44; 8000 gi=19, 91) now TIE in every arm at b10 (6 / 6 / 5 / 6),
  so none of them is a slower game here.

**Rulings 19-21 on the shipped default (ON arm vs step 8b's `l8b/m8b`, the same jobs / seeds / flags):** san1
identical (play digest 6eb2aaf31332bca5 in both; units 3,142,816 vs 3,142,693), hb1 identical (digest
6cf8ea0af4623774, units equal), ho1 **net -1**: one game moved, ho gi=31 6 -> 5, the ruling-20 O-Ring (below);
units 13.77M vs 14.10M (-2.4%, that game). So the rulings change one game of 224, faster.

**Firing counts, REAL games only** (`[ee-fair]` / `[ee-fov]` lines with `real=1`; mulligan bottom-trial playouts
excluded; `fir21.py`). "Under a board lock" = the line's `board_lock=1`, i.e. the veto actually withheld credit.
| count | OFF | ON | AMD |
|---|---|---|---|
| clause 1 vetoes under a board lock (turns / games) | 9 / 7 | 13 / 11 | 13 / 11 |
| clause 2 (trample >= 5) under a board lock | 0 | 0 | 0 |
| clause 2, any line (no board lock) | 2 turns, san gi=3 Gladecover Scout | 1, same | 1, same |
| **clause 4 (ruling 21)** under a board lock | 0 | 0 | 0 |
| clause 4, any line (no board lock) | 1 (ho gi=3 round 7, Slippery Bogle) | 0 | 0 |
| **O-Ring holding a virtual card (ruling 20)**, games (rounds) | 0 | **1** (ho gi=31, 5) | **2** (ho gi=11, 31; 7) |
| FoV end-step casts / on a virtual target | 34 / 1 | 28 / 1 | 29 / 1 (each: san gi=3 round 6, `virtual|virtual`, Gladecover Scout's Auras, clause 2) |
| **FoV vs a virtual EVASION Aura** | 0 | 0 | 0 |
* Ruling 20 in ho gi=31 (ON): the lock formed at round 4, then the opponent's could-cast Kumena's Speaker vetoed it
  (round 4, raw 7); at round 5 an O-Ring exiled the virtual Speaker and the lock was credited from there on
  (`oring=1`, spent 3 -> 4). The game is T5 in ON / AMD vs T6 in OFF and in step 8b.
* FoV vs a virtual evasion Aura cannot fire while clause 4 never vetoes a board lock (the FoV window opens only while a
  threat is on a head). A main-phase FoV's targets are not in the trace; the inference ("a veto=4 round followed by a
  non-4 round with a FoV cast between") found 0. The ruling-21 power >= 1 default therefore has no measured game.

**d3 b0 repros: ONE pooled batch `b0`** (binary `mtg-2df`, 25 games: every game slower than OFF in AMD (12) and in ON
(13), each in the arm that was slower; per-game wall cap 2 h, `MTG_MAX_GAME_WALL_SEC=7200`; under the box flock; wall
5 min, no game near the cap). The bar is OFF's b10.
| game | OFF b10 (bar) | ON b10 / b0 | AMD b10 / b0 | verdict |
|---|---|---|---|---|
| san gi=1 | 5 | 6 / **5** | (5 at b10) | recovers |
| ho gi=4 | 6 | 7 / **6** | 7 / **6** | recovers |
| ho gi=8 | 6 | 7 / **6** | (6) | recovers |
| ho gi=32 | 4 | 5 / **4** | 5 / **4** | recovers |
| ho gi=40 | 6 | 7 / **6** | 7 / **6** | recovers |
| ho gi=76 | 6 | 7 / **6** | (6) | recovers |
| **ho gi=84** | 5 | 7 / **6** | 7 / **6** | **NOT recovered** -- clairvoyance (below); OFF b0: unfinished at 2 h |
| ho gi=92 | 6 | 8 / **5** | 7 / **5** | recovers (beats the bar) |
| hb gi=6 | 6 | (6) | 7 / **6** | recovers |
| **hb gi=24** | 6 | 7 / **7** | 7 / **7** | **NOT recovered** -- clairvoyance (below); OFF b0: T6 (316 s, 9.84M units) |
| hb gi=32 | 7 | 8 / **6** | 8 / **6** | recovers (beats the bar) |
| hb gi=47 | 4 | 6 / **4** | 6 / **4** | recovers |
| hb gi=60 | 6 | (6) | 7 / **5** | recovers (beats the bar) |
| hb gi=67 | 5 | 6 / **5** | 6 / **5** | recovers |
| hb gi=79 | 6 | (6) | 7 / **6** | recovers |
| hb gi=83 | 6 | 7 / **6** | (6) | recovers |
(A value in parentheses = that arm was not slower; not re-run. In san gi=1, ho gi=76 and hb gi=67 the b0 game kept a
different card at the London-mulligan bottom: same deal, but bottoming plays out the candidates at the job's own
settings, so b0 can bottom differently.)
* ON: 11 of 13 slower games recover; AMD: 10 of 12. The same two do not recover in either arm.
* OFF-arm d3 b0 of the two non-recoverers, in the small-job lane beside the batch (`b0off`, 2 slots; hb gi=32 also
  finished there, T6 = the ON / AMD b0; a fourth game, san gi=1, was not needed and was stopped).
* **ho gi=84 (OFF b10 T5; ON/AMD b0 T6).** First divergence of ON b10 from OFF b10: T3 main 1. OFF (lands Temple
  Garden, Plains, Forest + a Wild Growth = exactly 4 mana, 2 white; EE in hand, Opalescence and Wave missing) casts
  Presence and sacrifices the Grove for a SECOND Wild Growth. Ruling 16 follow-up 3 prunes that (exactly 4 with 2
  white, a lock piece missing -> no mana option; it is also a copy of a Wild Growth ours). OFF's T4 then needs a
  post-shuffle draw: casting the tutored Wild Growth, Presence draws Temple Garden, which is the land that makes
  Enchanted Evening castable that turn (without it, 3 lands + 2 Wild Growth leave 4 after the Wild Growth cast). The
  Grove's search shuffled the library, so that land is a specific future draw not in hand or on board. **Clairvoyance**
  (USER 2026-10-13 test; the tutor-to-top is the Wild Growth, which does not by itself make EE castable). Relevant to
  ruling 18 ("unless you are finding issues with the rule"): this is the 4-band prune refusing a line, but the line's
  gain is a lucky Presence draw, so it is not an issue with the rule.
  OFF at d3 b0 did NOT finish: abandoned by the 2-h wall cap at T1 with only 2.64M units, far below the usual
  units per second (ON b0 of the same game: 1.83M units in 163 s), so the wall went somewhere the unit counter does
  not price. Prune OFF only (not the shipped arm); not root-caused, listed for step 8c.
* **hb gi=24 (OFF b10 T6; ON/AMD b0 T7).** First divergence: our T2 upkeep. OFF sacrifices its Sterling Grove for
  Utopia Sprawl (shuffling the library); ON keeps it and draws Wild Growth. That decision is IN ON's menu (2 mana, 1
  white: the mana option is offered, Sprawl as the short-of-white pick). OFF later casts two Groves at once at T3
  (rule A refuses; the amendment does not admit it either: no Opalescence ours or in hand), but the T6 kill does not
  need them (Enchanted Evening + Wave swing 9, then 13 with Opalescence on the credited turn). What OFF's T6 does need
  is its post-shuffle draws: Wooded Foothills at T3 and T4 (EE castable at T4), Wave at T5, Opalescence at T6, none of
  them tutored. ON's library (unshuffled, then shuffled by its own T3 / T4 Grove tutors) gives no land from T2 until
  T5. **Clairvoyance.** (The ON b0 line also wastes a tutor: T3 Grove -> Opalescence to the top, then the T4 upkeep
  Grove -> Utopia Sprawl reshuffles that Opalescence away. Both picks are in the menu; a search choice, not a gap.)
  OFF at d3 b0 also wins T6, by ANOTHER line: T2 Grove -> Enchantress's Presence (offered under ON too: mana is
  missing), then at T4 it casts a second Sterling Grove while the first is still out, with no {1} left to sacrifice
  either (rule A refuses this: the "lost Presence draw" case of ruling 15 follow-up 2). That cast's Presence draw
  is Opalescence, cast at T6; the lands it needs are post-shuffle draws too (Foothills x3, Windswept Heath). Both OFF
  lines win on specific post-shuffle draws, so the verdict stays clairvoyance; the b0 line is one more data point
  for the USER's pre-combo Grove exception (ruling 15 follow-ups 1-2).
* Totals: ON 13 slower = 11 budget noise + 2 clairvoyance; AMD 12 slower = 10 budget noise + 2 clairvoyance. **No
  prune gap remains** in this sample (de901bc9e: ON 3 gaps, AMD 1).

**Disclosed gaps (unchanged by the measurement):**
* Ruling 21 evasion not modelled: landwalk, shadow, a printed "can't be blocked", Geist of Saint Traft's attack-made
  4/4 flying Angel token (Spirits list); Alpha Authority's granted hexproof (an old gap).
* Ruling 20: an O-Ring cast BEFORE the lock pieces in the same turn is not offered the virtual target (the
  precondition is checked at emission); one O-Ring link at a time; viewer / human play does not offer virtual targets
  (step 9).
* Trace gap (found here): an upkeep-window Sterling Grove activation writes no `REVEAL` / `ABILITY` to the game trace
  (hb gi=24: OFF's T2 Grove vanishes and the tutored Sprawl is drawn; ON b0's T4 likewise). The game is right; the
  trace omits the window. Step 9 (viewer) should check that a human sees it.
* Main-phase Force of Vigor targets are not in the trace (only the end-step pitch logs them), so the "FoV vs virtual
  Aura" count above is exact for the pitch and inferred for the hard cast.

**USER decisions (data in; the current settings stand until the USER answers):**
1. `MTG_EE_USER_PRUNE` adoption: OFF / ON (as shipped) / ON + `MTG_EE_GROVE_PRELOCK` (with the extension). 224 games:
   OFF 5.848, ON 5.799 (net -11, units -59.7%), AMD 5.799 (net -11, units -57.8%); every slower game recovers at d3 b0
   or is clairvoyance (2 per arm, the same two). AMD vs ON net 0 at +4.6% units. Agent recommendation: ON as shipped
   (the amendment buys nothing net on this sample).
2. Ruling 21's power >= 1 default: no real game reached clause 4 under a board lock, so there is no data either way.
3. Ruling 20's "the board lock already stands" precondition (no virtual O-Ring target for an O-Ring cast before the
   lock pieces): fired in 1 real ON game (ho gi=31, one turn faster); the excluded ordering was never measured.

## Done items (formerly provisional Q1 / Q2 -- settled by standing CLAUDE.md rules, not USER questions)

* **D1 (was Q1) -- Oblivion Ring's target is a SEARCHED axis** (settled by the NO-GREEDY rule: a provider may only
  restrict / order, never pick one of several without branching). TurnSolver's Oblivion Ring post-pass (beside
  Archdruid's Charm's) emits, in the decision space, one cast variant per legal target at emission
  (`EtbExileNonlandLegal`: both sides, shroud / hexproof honoured), pinned on `tutor_target` "OR<n>"
  (`ExileUntilLeavesPin`; `#OR` in BOTH plan signatures keeps the dedup lossless). The provider may only
  restrict / order them (`DecisionProvider::EtbExileNonlandTargets`, default every legal target). The unpinned base
  keeps the live-board pick (`EtbExileNonlandTargetPick`, the old ladder) -- the route to a permanent that enters
  earlier in the same plan, every put path, and the rollout leaves beyond the horizon; one legal target = no
  variant. A pin whose permanent is gone falls back to the base pick. Human play board-clicks the full legal set at
  resolution. Unit test: "Oblivion Ring target is a SEARCHED axis".
* **D2 (was Q2) -- Sterling Grove's same-turn "tutor to top, then cast an enchantment so Presence draws it"** (settled
  by the COMPLETENESS rule: it was unfindable at any budget, because the trailing pass runs after every cast). Each
  main-phase SacTutorTop variant now has a LEAD twin (`chosen_x == 1`, `TurnSolver::IsSacTutorTopLead`) in the
  decision space, applied before every cast by ONE body in both worlds (`TurnSolver::ApplySacTutorTopLeadActs`,
  called beside the morph lead in ApplyPlanDirect's and AIEngine::TakeTurn's pre-cast passes). Both twins sit in
  the source's one activation family (a plan holds at most one); chosen_x and the tutor target (`#F`, now tagged
  for SacTutorTop -- before this every candidate folded onto the first) keep them apart in every signature. Unit
  test: "Sterling Grove LEAD activation" (the plan is in the root enumeration, so it is findable at
  `--budget-ms 0`; both worlds draw the tutored card). **Note for the lock-cluster implementer:** the
  opponent-end-step activation (tutor at their end step, draw it in our draw step) is NOT built -- the upkeep window
  (before the draw) is today's nearest timing, but it spends THIS turn's mana where the end-step activation spends
  last turn's untapped mana; build it on the end-step window Force of Vigor's pitch needs.

## Approved deferrals

_none_
