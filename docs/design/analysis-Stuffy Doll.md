# Analysis ledger — Stuffy Doll

Deck: `decks/Stuffy Doll/Stuffy Doll.cod` (Boros damage-reflection combo), 60 cards:
2 Stuffy Doll, 4 Nomads en-Kor, 1 Shaman en-Kor, 4 Mogg Maniac, 2 Boros Reckoner, 4 Screaming
Nemesis, 4 Spiteful Sliver, 4 Blasphemous Act, 4 Chain Reaction, 4 Guilty Conscience, 3 Loran's
Escape, 3 Mind Stone; lands 4 Forbidden Orchard, 4 Inspiring Vantage, 3 Sacred Foundry,
3 Boros Garrison, 3 Arid Mesa, 2 Plains, 2 Mountain. No sideboard.

The USER asked for the analysis on 2026-11-17 ("begin analysis of Stuffy Doll").

Branch/worktree: `stuffy-analysis` @ `.claude/worktrees/stuffy` (cut from origin
`phase-1-2-deck-analyzer` 18ab35ca8). The deck folder was COPIED from the primary checkout (untracked
there; original left in place). The primary's other uncommitted files belong to another session.

**The plan of the deck.** Every reflector turns damage dealt TO it into damage to the opponent:
Stuffy Doll (indestructible; to the chosen player), Mogg Maniac (target opponent), Boros Reckoner /
Screaming Nemesis (any (other) target), Spiteful Sliver (grants slivers the trigger; target player).
The sweepers hit them all at once: Blasphemous Act (13 to each creature, {1} cheaper per creature on
the battlefield, opponent's Forbidden Orchard Spirits included) and Chain Reaction (X = number of
creatures). Two reflectors under an Act = 26 >= 20. Guilty Conscience on Stuffy Doll is an
unbounded loop (Doll pings itself -> deals 1 to the player -> GC deals 1 to the Doll -> ...) that
ends only when the opponent dies; on an indestructible (Loran's Escape) Reckoner/Nemesis likewise.
Nomads / Shaman en-Kor redirect damage ({0}, any number of times) onto a reflector — e.g. an Act's 13
to Nomads moved onto the Doll — and Shaman's {1}{W} pulls a source's damage off another creature.

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells, attacks or blocks; opponent-facing clauses are inert
  (Nemesis's "can't gain life", hexproof vs an opponent), but self-binding clauses bind us. Model the
  full card; goldfish-inert deferrals are PROVISIONAL until the user signs off.
- **Our creatures DO get dealt damage in this deck** (our sweepers, Doll's self-ping, GC, redirects,
  reflector triggers aimed at our own creatures). Earlier "inert: nothing ever damages our
  creatures" deferrals (Boros Reckoner D1 in Minotaur's ledger) do not hold here.
- No greedy / heuristic substitute inside the search window (user hard rule). Choices a card
  introduces (reflection-trigger targets, en-Kor shield count/target, GC host, Loran target, Doll's
  chosen player, Act/Chain timing) are SEARCHED, provider-narrowed (restrict/order only), or collapsed
  by a stated SOUND dominance fold, and surfaced to the viewer.
- Cards are generic (cards.json is deck-agnostic); fix shared cards faithfully for every deck.
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present,
  never adopt unilaterally.
- One heavy job (build or batch) at a time; total threads <= `nproc`; no two concurrent worktree builds
  beyond two.
- Agents: no `cd` in compound commands, no `rm` / `rm -rf`, no `timeout`; build with `./build.sh`
  only; subagents do NOT push the shared branch; hand off at ~250K context (CLAUDE.md rule).

## Stage 1 — coverage (2026-11-17)
Missing (10): Stuffy Doll, Nomads en-Kor, Mogg Maniac, Blasphemous Act, Chain Reaction, Guilty
Conscience, Shaman en-Kor, Loran's Escape, Screaming Nemesis, Spiteful Sliver.
Present but with a gap for THIS deck: Boros Reckoner (vanilla 3/3; damage-reflection trigger deferred
as inert in Minotaur — not inert here; `{R/W}` first-strike activation still goldfish-inert).
Present, re-review: Forbidden Orchard (modelled as "tapped every turn -> one Spirit per Orchard per
turn at turn start"; here the Spirits feed Blasphemous Act's discount and Chain Reaction's X, so the
tap timing is load-bearing), Mind Stone, Arid Mesa (fetch hold modelled), shocks/fast lands/Karoo.
Scryfall JSONs: `build/scratch/stuffy/scry/` (primary build volume).

## Stage 2 — cards (in progress)
Design DONE 2026-11-17: `docs/design/damage-reflection-pipeline.md` (Opus architect). One shared
`src/core/DamageReflection.h` pipeline (shields -> aggregate per creature -> snapshot triggers ->
SBA -> LIFO trigger resolution; executor AND rollout call it), armed per deck via
`GameState::reflect_armed` so other decks stay byte-identical. Loops iterate for real under a 65,536
resolution cap + 64-step no-progress rule (proved unable to cut a lethal loop, life <= 30 / 2HG).
Implementation = §10 steps 1-11 (12 optional speed cert; 13/14 only on USER request), one fresh
implementer agent per step, serial.

| step | content | status |
|---|---|---|
| 1 | pipeline core, no cards | DONE 773bfef4b (unit 3403/3403, scen 561/561) |
| 2 | Maniac, Reckoner, Nemesis, Act (per-creature cost reducer), Chain Reaction; target fold + in-window ABORT tripwire | DONE 2a3d5db13 (scen 563/563; smoke PLAY-NEUTRAL: 219/219 job digests identical to a base binary (src b6d0cd16e, 18ab35ca8) on the same manifest; the 44 smoke FAILs vs GT are stale origin GT from 7 src commits since the last accept (6112caed4..b25400e31), not ours; cost audit re-run + full field --update owed, Scryfall 429). Deviations: design §14. Handoff: logs/stuffy/impl-step1-2-handoff.md |
| 3 | searched reflection-target axis (Incinerator precedent) | DONE ebfea35d5 (unit 3406/3406, scen 564/564, smoke 0/219 digests moved vs step 2; Plan +24 B for every deck, play-neutral; viewer `target` prompt wired; deviations §14) |
| 4 | Stuffy Doll (choose player, {T} self-ping) | DONE f8dabbaea (unit 3413, scen 566/566, smoke 0/219 moved vs step 3; chosen-player fold = deck-level `!dmg_events_armed`, else branched on the ETB choice axis; viewer `choose_player` prompt + `selfping=` verb wired, 5h forced run owed; OPEN: a dropped unit check found no 'don't ping' plan on a test board — root-cause in step 5) |
| 5 | Guilty Conscience (+ combat damage hook, loops) | DONE fffd6abd3 (unit 3419, scen 568/568, smoke 0/219 moved vs step 4; GC host searched (excluded from aura host collapses); Doll+GC loop kills from 20/30 in <=2L+1 resolutions; no-progress rule tested; FIXED in-window greedy: main-1 offered no 'hold the Doll' plan -> SelfPingHoldPending joins the face-up hold pass) |
| 5b | combat-time reflection target pins (GC'd attacker reflects inside combat: currently front pick = no-greedy gap) + classify the 3 unclassified step-2 viewer params | DONE 06907847f (unit 3422, scen 568/568, smoke 0/219 moved vs step 5; combat pins after kCombatPinMark in Plan::reflect_targets -> GameState::scripted_combat_reflect (GameState 1608->1632 B, play-neutral); static viewer audit rc=0; residual: bp_pre clones inherit combat pins (Prophets-precedent gap), extra combats not probed (no such card)) |
| 6 | Spiteful Sliver | DONE de3ab61d7 (unit 3428, scen 571/571, smoke 0/219 moved vs 5b; one granted instance per Spiteful Sliver, snapshot pre-apply (LKI); 2 Slivers + Act vs 52 life kills exactly; viewer prompt now keys on t_viewer_reflection (granted triggers were never prompted); Sliver TOKENS not prompted (no CardDefinition; none in pool)) |
| 6a | fix: 5b scenario's 'sick' Doll was unsickened by untap -> add resume_at main1 so it really needs the combat fan | DONE (parent) |
| 7 | Loran's Escape | DONE 2275bff54 + 85ed9b0a6 (two agents; unit 3434, scen 572/572, smoke 0/219 moved vs 6; oracle targets EITHER side (no 'you control'); target searched w/ identical + no-effect folds; NEW searched relative order of sweeper vs setup spell (Loran's/GC), loam_order precedent — cast-order RANKS untouched; Loran-before-Act scenario wins T4; scry = engine-wide provider pick pending Q-U3; R7 unreachable here) |
| 8 | Nomads en-Kor shields (sweeper rider) | IN PROGRESS (~18:50 UTC) |
| 9 | Shaman en-Kor {1}{W} pull | pending |
| 10 | StuffyDollProvider + ExtraLethalDamage | pending |
| 11 | second main (Q-U4) | pending |

Defaults taken while USER questions are open (see below): Orchard model kept; Loran's scry =
provider pick (existing engine-wide cast_scry practice); second main ON subject to measurement;
deferrals provisional.

## Open questions for the USER (surfaced, not blocking)
- Q-U1 PROVISIONAL deferrals: Boros Reckoner {R/W} first strike (no blockers -> outcome-inert);
  Loran's hexproof (inert); en-Kor shields offered only as a rider on our own sweeper and Shaman's
  source = our sweeper (other windows dominated, design §4.2/4.3); canonical simultaneous-trigger
  order (design §4.7 proof).
- Q-U2 Forbidden Orchard: keep turn-start Spirit model (default; equals the legal "tap every Orchard
  at main-1 start" line, never worse here) or build tap-time Spirits (moves Hinata/Creature Giving).
- Q-U3 Loran's Escape scry 1: provider pick like every other spell scry today (default), or searched?
  Note the provider pick is an in-window single pick — the NO-GREEDY rule may want it purged engine-wide.
- Q-U4 second main for this deck: recommend ON, measured before adoption.
- Q-U5 cast-order rules: USER-owned; none proposed, order is searched.
- Rules check before step 8: is an en-Kor shield consumed when its redirect target has left the
  battlefield? (default: not consumed — it simply doesn't apply).
- Step-2 PROVISIONAL deferrals: Boros Reckoner {R/W} first strike; planeswalkers not offered as reflection
  targets (Maniac/Reckoner/Nemesis); Nemesis lifegain lock modelled but goldfish-inert.
- Side finding (step 2): executor SBA and CounterPingKillAt put dead cards in the CONTROLLER's graveyard, not the
  owner's (CR 400.3); pipeline uses the owner. Inert while we never control opponent's creatures.
- Pre-existing, not ours: test/check_plan_sig_lockstep.py exits 1 on the base commit too (#BB, crackle_targets/splice_count).
- Q-U6 (step 5 finding, ENGINE-WIDE): once the land drop is spent, the main-1 subset walk skips the empty
  selection and the hosts add a 'pass' plan only for face-up holds (now also Doll holds) — main 1 never offers
  'do nothing' while main 2 always does. Fixed only for the Doll. Should 'pass main 1' be offered for every deck?
  (Would move play broadly; needs the no-greedy adoption bar.) Default: leave other decks unchanged pending USER.
- Viewer audit classifications needing USER OK (all filed as no-choice): `aura_host_damage_back` (GC),
  `cost_less_per_creature_on_battlefield` (Act), `damage_all_creatures_count_creatures` (Chain X counted on
  resolution), `reflect_player_cant_gain_life` (Nemesis rider; the recipient is already the `target` choice).
- Side finding R7: two rollout-only kill paths (TurnSolver.cpp ~73041/~73148) ignore indestructible
  and skip death triggers — not on this deck's paths; separate fix.
