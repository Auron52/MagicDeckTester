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
| 8 | Nomads en-Kor shields (sweeper rider) | DONE a4f820e2a (unit 3441, scen 573/573, smoke 0/219 moved vs 7; shield allocation = pins chosen at sweep resolution (dmgrx::AddShieldRider/PinnedChoice), shields live on the damage Batch (no GameState list); tie fold narrower than §4.2 (face-only reflectors folded; Reckoner/Nemesis/GC'd/Doll-chose-us kept apart); autonomous play never redirects onto a live non-reflector (viewer `divide` still offers all); Nomads+Act+Doll = 26 kills T4, control fixture leaves opp at 8; Q-R1 default: shield to a departed target not spent (CR 614.9)) |
| 9 | Shaman en-Kor {1}{W} pull | DONE 268e96f4a (unit 3447, scen 576/576, smoke 0/219 moved vs 8; pulls on the damage Batch applied before shields (CR 616.1); count/targets searched with folds P0-P3; {1}{W} paid at sweep resolution via TapForCostDirect, offered only while affordable; Shaman pull Spirit kill from 39 T4, Nomads control no win by T6) |
| 10 | StuffyDollProvider + ExtraLethalDamage | IN PROGRESS (~20:15 UTC) |
| 9b | Shaman pull AND en-Kor {0} shields from ANY damage source in the engine; provider prunes to our sweeper (+ P0-P3 for the pull) (USER 20:30/20:35); provider GC-host restriction to loop-sustaining creatures (USER 20:40) | pending (after 10) |
| 9c | searched attack-vs-hold for GC-enchanted non-indestructible creatures; GC host order (tappable Doll first, else Doll + one other reflector); Loran's target GC'd creature first; cast order GC before Loran's (USER 20:45/20:50) | pending (after 9b) |
| 11 | second main (Q-U4) | pending |

Defaults taken while USER questions are open (see below): Orchard model kept; Loran's scry =
provider pick (existing engine-wide cast_scry practice); second main ON subject to measurement;
deferrals provisional.

## Open questions for the USER (surfaced, not blocking)
- USER RULING 2026-11-17 ~20:25 UTC (verbatim): "The Shaman heuristic prune makes sense." Read as sign-off on the
  step-9 Shaman en-Kor pull restrictions: the pull's source is only our own sweeper (no other activation window),
  and folds P0-P3 (P1 argued against the full shield allocation). These leave PROVISIONAL status. The rest of Q-U1
  (Reckoner first strike, Loran's hexproof, planeswalker targets, Nemesis lock, Nomads shields only as a sweeper
  rider, canonical trigger order) is still open.
- USER RULING 2026-11-17 ~20:30 UTC (verbatim, refines the one above): "My take on the pull is that it should be
  possible to pull from any source that is damaged, but we should heuristically prune it to what you said."
  => ENGINE models the pull for ANY damage source (sweepers, reflection triggers, GC, Doll self-ping, combat);
  the StuffyDollProvider RESTRICTS it to our-sweeper source + P0-P3 (restrict-only heuristic; lifted by
  MTG_UNPRUNE / human play; the viewer offers every source). Scheduled as step 9b, right after step 10.
- USER RULING 2026-11-17 ~20:35 UTC (verbatim, on the {0} shields): "Yes, they probably should get the same
  treatment, but realistically there are no other sources of damage that hit our creatures in goldfishing."
  => step 9b also makes the Nomads/Shaman {0} shields apply to ANY damage event in the engine, with the provider
  restricting them to our sweeper (same shape as the pull). Low priority within 9b: the other in-deck routes onto an
  en-Kor are corner cases (a reflection aimed at an en-Kor; GC on an attacking en-Kor).
- USER RULING 2026-11-17 ~20:40 UTC (verbatim, GC host): "Fair enough, the Guilty Conscience is something that can
  actually happen in practice. That said, it's not a good play to put it on the en-Kor. You should always choose
  something that can keep the loop going."
  => StuffyDollProvider RESTRICTS Guilty Conscience hosts to creatures that keep the loop going: our creatures whose
  being dealt damage makes them deal damage (Doll, Reckoner, Nemesis, Mogg Maniac, Slivers under a Spiteful grant).
  Restrict-only heuristic (engine still models every host; lifted by MTG_UNPRUNE / human play). If no such host
  exists the provider must not empty the list (keep the engine's list). Built in step 9b with the pull/shield hooks.
  USER clarification: "(heuristically)" — i.e. a provider restriction, not an engine rule.
- USER RULING 2026-11-17 ~20:45 UTC (verbatim): "Stuffy Doll is even better if out, but given that it is slow often
  the others would be the target. One slightly messy case is if we need to Guilty Conscience and Loren's Escape next
  turn. We need to not always attack in the meantime."
  => (a) provider ORDERS GC hosts Doll first (when on the battlefield), then the other loop-sustaining creatures.
  (b) ATTACK HOLD: today attacks are the provider's per-creature `ShouldAttackWith` (the standing attack exemption),
  so a GC'd non-indestructible reflector always attacks and kills itself via GC (e.g. Reckoner: 3 combat + GC 3 back
  -> dies), destroying the "GC now, Loran's Escape + loop next turn" line. Fix (step 9c): make attack-vs-hold a
  SEARCHED binary for each GC-enchanted creature that is not indestructible (bounded: <= 4 GCs), executor and
  rollout in lockstep, other decks byte-identical; the provider may order (e.g. hold first when a Loran's Escape
  is in hand) but not pick. Fallback only if the searched axis proves infeasible: a provider `ShouldAttackWith`
  hold heuristic, surfaced to the USER.
- USER RULING 2026-11-17 ~20:50 UTC (verbatim, refines (a)): "Actually, Doll first only works consistently if it can
  tap this turn. If not, we might need to consider one other critter that reflects damage. Similarily Loren's Escape
  should target a critter with Guilty Conscience on it if such exists. The order should make this easy. (Guilty
  Conscience first?)"
  => GC host (provider, heuristic): if a Stuffy Doll can tap THIS turn (untapped, not summoning sick) -> Doll first;
  otherwise keep the Doll AND one other loop-sustaining reflector as candidates (Doll + the best other reflector),
  ordered so the reflector that can loop this turn comes first.
  => Loran's Escape target (provider, heuristic): a creature carrying Guilty Conscience first when one exists.
  => CAST ORDER (USER-proposed, marked "?"): Guilty Conscience before Loran's Escape, so the Escape can see the GC'd
  creature. USER rationale: "The idea here being that summoning sickness is an issue since we need damage to start
  the chain." => "can loop this turn" = has a STARTER this turn: the Doll's {T} ping or an attack (both need it
  unsick), OR any other damage reaching it this turn (our sweeper in hand and castable, a reflection aimed at it),
  which works on a summoning-sick creature too. The provider's host order uses that starter test.
  USER (verbatim, ~20:58): "Yes, and if there is a reflector without summoning sickness and a doll we should
  potentially try both. The doll is better if we need to wait a turn anyway, but the other creature may be able to
  go off." => with an unsick reflector AND a Doll on board, BOTH stay host candidates (never prune either); the
  one with a starter this turn is tried first; the Doll is the preferred host for a wait-a-turn line.
  USER (verbatim, ~21:00): "But we only need to try a maximum of 2 targets." => provider RESTRICTS GC hosts to at
  most 2: the Doll (if on board) + the best other loop-sustaining reflector (one with a starter this turn
  preferred); with no Doll, the best 2 reflectors. Restrict-only heuristic; lifted by MTG_UNPRUNE / human play.
  USER (~21:01): "(and often just 1)" => ONE candidate when a host clearly dominates: a Stuffy Doll with a starter
  this turn (it can tap, or our sweeper reaches it) — indestructible and loops now, nothing beats it; also when only
  one loop-sustaining creature exists. TWO only in the split case: the Doll must wait a turn but another reflector
  can go off now.
  USER (~21:05): "Hmm... Though I guess in rare cases we would win by combat damage and the conscience could be
  better on a higher-power creature. That makes a clear order." => host ORDER (and which 1-2 survive the cap):
  (1) a Stuffy Doll with a starter this turn; (2) a reflector that can go off this turn; (3) ties broken by higher
  power (an attacking GC'd reflector deals power + reflects power, so the combat-damage win favours the bigger one).
  USER (~21:08): "In rare cases we could have a lower-power attacker able to attack and a higher-power one with
  summoning sickness. I would choose the lower-power one unless we have a good chance of lethal next turn with the
  other." => the starter test outranks power (lower-power unsick attacker first); the sick higher-power one stays as
  the 2nd candidate, so the search (not the provider) decides whether next turn's lethal with it is worth waiting for.
  USER (~21:10): "(in which case you can search)" and "I don't want an order for the most part. We should prune it."
  => GC HOST IS A PRUNE, NOT AN ORDER. The provider RESTRICTS the candidate set; order inside it is immaterial:
     * ONE host when one dominates: a Doll with a starter this turn; else the single reflector with a starter this
       turn when no Doll/bigger reflector is worth waiting for; or the only loop-sustaining creature.
     * TWO hosts only in the split case: the best creature that can go off THIS turn (starter; among those the
       higher power) + the best wait-a-turn host (the Doll if on board, else the higher-power sick reflector).
       The search decides between them.
     * Everything else pruned (non-reflectors, en-Kors, lower-power duplicates of the same role).
  USER (~21:12): "I'm saying this is an exception to the prune." => the DEFAULT is ONE host; the two-host split case
  (go-off-now creature vs a wait-a-turn Doll / bigger sick reflector) is the EXCEPTION where both are searched.
  Adopted as the USER's order for this deck's provider; measured once the deck runs and reported back
  (if it measures worse, root-cause it and propose an amendment — never silently drop it).
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
