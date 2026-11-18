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
| 10 | StuffyDollProvider + ExtraLethalDamage | DONE 6d6454d53 (unit 3453, scen 577/577, smoke 0/219 moved vs 9; routes first, signature = §8 params; F2/F3 reflection folds; ExtraLethalDamage; d0 50 games: 44 won avg 5.386 vs 5.591 on the old ModernRwBurn misroute; FIXED step-2 Act discount counting the Doll ping as an ETB, FIXED step-9 pull encoding crash on opp spawn numbers >= 2^17; opp-Spirit prune built OFF (MTG_STUFFY_OPP_CREATURE_PRUNE) pending A/B; cast order = generic (Q-U5)) |
| 9b | Shaman pull AND en-Kor {0} shields from ANY damage source in the engine; provider prunes to our sweeper (+ P0-P3 for the pull) (USER 20:30/20:35); provider GC-host PRUNE (1 host, 2 in the split case) + Loran's target PRUNE to the GC'd creature (USER 20:40-21:14) | DONE a454f7f31 (unit 3460, scen 577/577, smoke 0/219 moved vs 10; riders on every damage event (Batch kind), provider prunes MTG_UNPRUNE=redirectwindow/gchost/escapetarget; d0 50 games 44 won avg 5.386 = step 10 (11 games played differently, net 0 win turns); DECISION to confirm: Loran's prune drops same-plan HAND creatures as targets; viewer pull prompt repeats per loop step; held-out A/B of the prunes owed (measure + report)) |
| 9c | searched attack-vs-hold for GC-enchanted non-indestructible creatures; cast order GC before Loran's (USER 20:50) | DONE a767c1ec0 + 69b81f256 + 517506ff4 (unit 3464, scen 579/579, smoke 219/219 digests = 9b, parent-verified 02:xx). Hold codes ride scripted_combat_reflect; AttackWith refuses held; SOUND kill folds in the combat fan AND the main fan; GC rank 19. gi=26 ROOT CAUSE: exponential reflection fans (combat fan 5b + main fan 3) cloned the plan at every target choice of the Escape+GC Reckoner loop; plan enumeration is never budget-charged, and pinned leaves pile up in `expanded` (no cache grows). The two kill folds fix it (repro T4 win in 1.0 s, 6 MB; 9b >100 s and climbing); scenario stuffy_escape_gc_reckoner_loop_fan_fold pins it. d0 50: 44 won avg 5.386 = 9b. d3/b10 50 vs control (9b+folds; plain 9b explodes on gi26): 50/50 both, avg 4.52 -> 4.46, 1 faster (gi46 T7->T4: GC on Maniac T3, hold, Escape T4, swing into the loop) 0 slower, CPU +11% (hold-subset fan). RESIDUAL RISK: a non-killing reflection fan is still exponential (none known). OPEN for USER: viewer attacker declaration -> step 9e |
| 9d | Loran's target prune admits a creature cast earlier in the same plan, and a same-plan GC host (line creature -> GC -> Escape -> Act) (USER 23:25); Loran's scry 1 searched + provider prune (USER 23:25, first consumer of the engine-wide searched-scry axis) | pending (after 9c) |
| 9e | viewer/human hold prompt as an EXCEPTION to the always-attack rule: a provider hook (deck heuristic) names the creatures that may decline to attack (Stuffy: GC'd non-indestructible); the viewer asks the human only for those, default = attack (old references replay unchanged; reconcile --force-attackers); the step-9c searched hold fan reads the SAME hook so engine and viewer offer one set (USER ~00:37) | pending (after 9d) |
| 11 | second main ON; sweepers (Act, Chain Reaction) cast in main 2 only; everything else unchanged (USER 21:45/21:48) | pending (after 9c) |

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
- USER (~21:14, verbatim): "Yes, that sounds good regarding Loran's Escape." => Loran's Escape target is a PRUNE:
  when a creature carrying Guilty Conscience exists, the provider RESTRICTS the Escape's targets to it (restrict-only;
  lifted by MTG_UNPRUNE / human play; with none, the engine's folded target list stands).
- USER (~21:35, verbatim): "I would prune Loran's Escape even when we have no guilty conscience to one creature or
  perhaps even not castable. It is essentially a Scry 1." => with no GC'd creature, the provider RESTRICTS the
  Escape to ONE target (it is cast as a {W} scry 1: prefer the engine's no-effect/scry-only fold target, else a single
  reflector). Kept castable (default) rather than pruned outright, since the scry digs for combo pieces; "not
  castable" is the USER's alternative if the measurement shows the scry line wastes budget.
  USER (~21:38, verbatim): "I guess you can argue that it could allow a creature to survive a sweeper... Not great,
  but that is a use. In that case highest power (and not stuffy) I suppose." => the ONE no-GC target is our
  highest-power creature other than Stuffy Doll (already indestructible); only if there is none, the scry-only
  no-effect target.
  USER (~21:40, verbatim): "It should be a creature that reflects damage also." => the no-GC target is our
  highest-power damage-REFLECTING creature other than Stuffy Doll (Reckoner, Nemesis, Mogg Maniac, Slivers under a
  Spiteful grant); if none, the scry-only no-effect target.
- USER (~21:45, verbatim, MAIN SPLIT — answers Q-U4): "Probably sweepers should be cast main 2. Arguably everything
  but the combo should be." => step 11: second main ON for this deck; Blasphemous Act / Chain Reaction are cast in
  MAIN 2 (after combat, so attackers deal damage before the sweep). Candidate rule, to be authored and MEASURED
  before adoption: everything except the combo is cast in main 2, where "the combo" = the pieces that must be in
  place before combat this turn (Guilty Conscience on an attacker, Loran's Escape on the GC'd creature, a starter
  for this turn's loop). Report the measurement; never drop the USER's rule silently.
  USER (~21:48, verbatim, SUPERSEDES the candidate rule): "Actually maybe it is easier to allow critters to be cast
  main 1 as well because we might cast the combo on them. So in that case it would just be sweepers in main 2."
  => the rule is ONLY: sweepers (Blasphemous Act, Chain Reaction) are cast in main 2. Creatures and everything
  else keep main-1 casting as today.
  Adopted as the USER's order for this deck's provider; measured once the deck runs and reported back
  (if it measures worse, root-cause it and propose an amendment — never silently drop it).
- USER (~23:25, verbatim, on Q-U6): "Wait, we don't already allow doing nothing in main 1?" => answered: not
  once any plan exists (the land drop being the usual "nothing else" line); only face-up holds and the Doll
  hold append the pass. Proposal surfaced: append the pass engine-wide (adds a line, removes none) as its own
  change with the no-greedy adoption bar. Default until the USER answers: other decks unchanged.
- USER (~23:25, verbatim, answers Q-U3): "It should be searched and typically overridden by a provider and yes,
  this should be engine-wide. That doesn't mean we should just leave it searched in most cases, though. This is
  the kind of thing worth a prune." => scry becomes a SEARCHED axis engine-wide, with provider prunes cutting
  it down in most cases. Engine-wide part recorded in docs/design/searched-scry-disposition.md (2026-11-17 section). Loran's Escape's scry 1
  is searched, with a StuffyDollProvider prune.
- USER (~23:25, verbatim, answers Q-U2): "I guess we can keep this for now, but you are correct that we will
  probably want to move toward supporting it as written at some point." => keep the turn-start Spirit model;
  tap-time Orchard Spirits is future work (noted in docs/design/searched-scry-disposition.md).
- USER (~23:25, verbatim, on the step-9b Loran's prune dropping same-plan HAND targets): "That should be fixed,
  given the potential creature -> Guilty Conscience -> Loran's Excape -> Blasphemous act line." => step 9d:
  the Escape's target prune must admit a creature cast earlier in the SAME plan, and its "the GC'd creature"
  clause must cover a GC cast earlier in the same plan (today the target is fixed at enumeration, before the
  same-plan GC resolves).
- USER (~23:25, verbatim, on the opponent-Spirit prune): "That is this prune?" => yes: MTG_STUFFY_OPP_CREATURE_PRUNE
  drops opponent creatures (Orchard Spirits) as reflection-trigger targets. Plan: held-out A/B, adopt on the
  USER's prune-adoption criteria (better overall + no unrecognizable lost lines + faster), report.
- USER (~23:25, verbatim, on the stale origin smoke GT): "I'm hearing about this from at least one other agent,
  but still have no details on what changes caused this so I would appreciate some." => attribution bisect of
  the 44 FAIL keys on clean binaries (not ours; reported to the USER, not accepted).
- USER (~00:37 11-18, verbatim, answers the step-9c viewer question): "I think we can make it an exception to the always attack-rule. Perhaps have a deck heuristic that can be consulted to reject attacking?" => step 9e: a DecisionProvider hook naming the creatures that may decline to attack (default none; Stuffy: GC'd non-indestructible attackers); the viewer prompts for exactly those (default attack), and the searched hold fan uses the same hook.
- USER (~00:45 11-18, verbatim, CAST ORDER): "Let's see a full cast order. Loran's escape should be before sweepers which are cast in the second main." => Stuffy order (authored, presented): land (searched) > Mind Stone (5) > creatures (10, hand order) > Guilty Conscience (19) > Loran's Escape (20) > [main 2 only] Chain Reaction / Blasphemous Act (21; tie between them = hand order, proposal Chain first). Today Loran's ties the sweepers at 20 (hand order decides) -- step 11 gives the sweepers their own later rank and drops the step-7 'sweeper before Loran's' order branch. Creatures stay castable in main 1 (the generic main classifier would put Maniac/Reckoner/Sliver in m2 once a second main exists -- step 11 must override that).
- USER (~00:55 11-18, verbatim): "Let's split up any buckets there. I want a full order with no ties." => PROPOSED total order (authored, presented; implemented in step 11 as the default until the USER amends it): main 1: land (searched) > 1 Mind Stone > 2 Nomads en-Kor {W} > 3 Shaman en-Kor {1}{W} > 4 Mogg Maniac {1}{R} > 5 Screaming Nemesis {2}{R} > 6 Spiteful Sliver {2}{R} > 7 Boros Reckoner {R/W}x3 > 8 Stuffy Doll {5} > 9 Guilty Conscience > 10 Loran's Escape; main 2 only: 11 Chain Reaction > 12 Blasphemous Act. Creature principle: strict coloured pips first, hybrid next, generic-only last (cheaper first; Nemesis before Sliver as the stronger reflector), so the payer never spends a land a later coloured pip needed.
- USER (~01:00 11-18, verbatim): "Otherwise it seems reasonable." and "Though we should also add mind stone's ability. Perhaps at the end of main 1." => order adopted as above, plus 11 = Mind Stone's {1},{T},sacrifice: draw at the END of main 1 (after Loran's Escape, before combat; whether to activate stays searched); Chain Reaction / Blasphemous Act renumber to 12 / 13 (main 2 only; Chain first accepted).
- USER (~01:20 11-18, verbatim): "That full order looks fine." => the 13-slot total order (Mind Stone draw at 11, Chain 12, Act 13) is the USER's ADOPTED cast order for this deck (Q-U5 closed).
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
