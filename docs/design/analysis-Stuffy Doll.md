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
| 9d | Loran's target prune admits a creature cast earlier in the same plan, and a same-plan GC host (line creature -> GC -> Escape -> Act) (USER 23:25); Loran's scry 1 searched + provider prune (USER 23:25, first consumer of the engine-wide searched-scry axis) | DONE 76ac763be + 4fedba055 (unit 3468, scen 580/580, smoke 219/219 digests = 9c parent-verified, provider_audit clean, viewer audit lists scry). Part A: Escape target list admits same-plan hand creatures + the hosts a GC in hand would take (enumeration-time admission, option (b); late binding would be an in-window single pick). Deviation: no per-plan GC/Escape same-host consistency. Part B: Escape scry 1 searched; variant count = cards seen (honest -- the WIP sized it off the unseen top card, CLAIRVOYANT, fixed); ScryVerdict (keep missing reflector/sweeper; keep land if lands<5; bottom land at >=7) applies at RESOLUTION only, so it settles the choice but saves no enumeration work. New scenario stuffy_hand_reckoner_gc_escape_act wins T4 (9c T5). d0 50: 44 won avg 5.386 = 9c per game; 9 games changed play. Found: land-ETB scry search (EnumeratePlansWithLand Plan::scry_choice ~TurnSolver 93195) sizes off the unseen top -> engine-wide scry inventory item (searched-scry-disposition.md) |
| 9e | viewer/human hold prompt as an EXCEPTION to the always-attack rule: a provider hook (deck heuristic) names the creatures that may decline to attack (Stuffy: GC'd non-indestructible); the viewer asks the human only for those, default = attack (old references replay unchanged; reconcile --force-attackers); the step-9c searched hold fan reads the SAME hook so engine and viewer offer one set (USER ~00:37) | DONE 7e6f64116 + 1b05425b4 (unit 3471, scen 580/580 outcome lines = 9d, smoke 219/219 digests = 9d parent-verified; Stuffy 50-game A/B vs a 9d build byte-identical at d0 and d3/b10; viewer_protocol_check --strict over 875 refs: 0 drift/diverged/enum-gap). DecisionProvider::MayDeclineAttack (default false; Stuffy: GC'd, not indestructible printed or Escaped); the 9c fan asks the hook. Human play: AskHumanAttackHolds in the real DeclareAttackers asks a `dig` yes/no '(may attack)' per named would-attack creature, default attack; 'no' writes the search's hold code. --force-attackers turns: a named creature absent from the recorded set = held. claude-play seeds 1-60: prompt on 10 seeds, one creature each; seed 14 hold kept opp at 20 / Reckoner alive, attack -> 17. Deviations (design §14): the fan's cheap 'GC on battlefield or cast by plan' pre-check stays engine-side; the prompt is gated on reflect_armed; pre-declaration human-play projections assume a named creature attacks (no Stuffy effect). NOT RUN: dynamic viewer-audit sweep (needs the Stuffy profile; static audit rc 0) |
| 11 | second main ON; sweepers (Act, Chain Reaction) cast in main 2 only; everything else unchanged (USER 21:45/21:48); USER total cast order + Mind Stone dig at end of main 1 (USER 00:45-01:20) | DONE 32a2154d0 + e1b5db874 (unit 3476, scen 583/583, smoke 219/219 digests = 9e parent-verified). MTG_STUFFY_MAIN_SPLIT default ON (=0 reproduces 9e digests d0 54b7c0312d42fd02 / d3 e5df6a1d0cf45c02); cast order is a sub-flag MTG_STUFFY_CAST_ORDER (inert when the split is off). Sweepers keyed by param, order by name; --cast-order-report: no tied rank, only Chain/Act m2. A/B 50 paired, MTG_PGO=0: d0 9e 44 won avg 5.82 -> split 47 won 6.02 (6 faster/20 slower); d3/b10 4.44 = 4.44 (0/0, all 50 win turns identical), CPU 0.96x. d0 ROOT CAUSE: the d0 greedy main 1 spends the mana the main-2 sweeper needs on a creature (gi12 T3->T5, gi2 T4->T5, gi30 T4->T6); d3 scores main 1 with its main 2, so it does not move. PROPOSED AMENDMENT (USER decision, default OFF): MTG_STUFFY_SPLIT_LETHAL_M1 = main 1 may also cast a sweeper whose own reflection is projected lethal now: d0 49 won avg 5.46 (8 faster/1 slower vs 9e: gi12 T3->T4), d3 unchanged, CPU 0.92x. Wider bound (all hand, ignoring mana) measured and rejected (d0 5.62; allows non-lethal main-1 sweeps). Main 2 verified in executor/rollout/d0 (sweepers cast MAIN_2 in real d0 games). Finding: stuffy_split_attack_then_act_off (9e search) fails at budget 50/500, finds the T5 line only unbudgeted. Report DIG line still prints m1/m2 (generic scan; report-only) |
| E | engine fixes from the USER 07:30 answers (items 4b/5/6) + the d0 duplicate-legend gate (USER ~08:50) | DONE 0f28237f9 (battlefield deaths -> OWNER's graveyard, CR 400.3; OnCreatureDies dead_owner) + 243f5ac83 (R7: rollout burn kills use the shared executor kill helpers: indestructible + death triggers) + 61b04ee7d (MTG_NC_SEARCH land-ETB scry fan no longer sized off the unseen top card: a NON-clairvoyant leak, fixed; blind mode + honest teacher clean) + 633ee0dce (DuplicateCastHasUpside moved below the d0 gate in OfferDuplicateLegendCast, restoring the 2026-08-29 ruling). Unit 3562/3562 (+11), scen 583/583, smoke 221/223 vs current GT: only whiteknights_smoke_d0_s1001 (back to its pre-451a4c571 digest 1f86383d17efda44, avg unchanged) and merfolkturns_smoke_d0_s1001 (6.1020 -> 6.0930, new digest 4631fb56c0df453f; gi8 seed 1009 T6 -> T5: gate-off cast a second Emperor Mihail that the legend rule removed). Every fix fails its tests with the mechanism reverted (10/10 targeted cases). NOT fixed (executor and rollout agree, so no divergence): Searing Blood's death rider ignores indestructible; Crackle/Soulfire kills skip indestructible + death triggers. Non-clairvoyant scry PERFORMANCE prune (item 4) not built (low priority). GT ACCEPTED 9974993a2 (regression tier on 633ee0dce: 351/353, the same two d0 keys -- WK digest back to pre-451a4c571, Merfolk 6.0140 -> 6.0100; searched 0/0/0 both tiers, d0 slower 0; VPC 878 refs 0 drift). |
| 9f | best order for simultaneous triggers wherever order matters (USER 07:30 item 2: "we need to make sure that it is the best order when the order matters"); viewer Shaman pull asked once per loop (item 8) | DONE 3510d7afe (unit 3568/3568, scen 583/583, smoke 223/223 = fixes digests, 0 changed games; Stuffy 50-game A/B vs the fixes binary IDENTICAL per game at d0 (47 won, 5.830) and d3/b10 (50, 4.440), CPU 0.96x; VPC 878 refs 0 drift/diverged/enum-gap; static viewer audit rc 0). The canonical order is NOT always best (contrived board: canonical 7 vs searched 8), so order is SEARCHED where it can matter and fixed where design 4.7 proves it cannot: sweeper/reflection with only face targets or a reachable loop host = no choice, else searched; GC resolution = same test; Doll ping chose-opponent = no choice (chose-us = human only); combat GC'd Reckoner + Nemesis = searched (unit test shows both orders branched), two GC'd Maniacs = no choice; en-Kor shields/pulls are replacement effects (order-free); no death triggers in the pool. Pull prompt: it was asked at EVERY damage event to a creature (human play skips the window prune), so a Doll+GC loop asked once per GC hit; now asked once per damage CHAIN, a decline stands for the chain, and a 'no pull now, ask again' option keeps later-step pulls reachable; also fixes a viewer panel that could not decline (forced 1 pick). Deviations (design 14): a pointless order prompt in the human-only Doll-chose-us case; a disclosed human-play PARTIAL with an en-Kor out. |
| S3-4 | Stage 3 re-coverage, 4a provider audit, Stage 4 baseline profile, 5j cost gate, 4-bis suite entry | DONE. Coverage 19/19 full, no new gaps (deferrals = Q-U1 / step-2). provider_audit --check rc 0 (StuffyDoll routes, Certificate NotAssessed, no other deck on it). Profile 1dc62fd6a (defaults / static keep, mulligan_flags [], play d5 b20, cost diagnostic COST_NEUTRAL 4.595, discard STATUS_QUO_OK). COST GATE PASSED: one pooled probe with Hinata, thread-wall ms/game Stuffy d3b10 2,005 / d5b20 4,981 / 2HG d5b20 10,590 (worst) vs Hinata 6,970 / 15,296 (worst) / 9,533 -> 0.69x (CPU 0.87x). Suite rows 69d24ef80 (stuffy + stuffy2hg, all three tiers, ENTRY shape; 2HG because Maniac/Sliver/Doll hit one head of shared team life). Rebased onto origin 6462493e9 (Maze's End; GameState 1648 fix 7deda94a9); smoke+regression on the rebased binary: unit 3643, scen 583/583, only our engine-fix keys moved (byte-identical to the pre-rebase accept), searched 0/0/0, d0 slower 0; GT ACCEPTED 88c1e78a6 incl. 11 new Stuffy keys (smoke d0 6.4520 / d3 4.6200 / d5 4.6400 / 2hg 4.7200). Overnight Stuffy rows + verify_deck full run still owed. |
| OPT | heavy-tail diagnosis (budgeted d5 b20 games to 161 s / 1.26M units; budget-0 backstops hitting the 2M cap) | DONE, NO FIX NEEDED. The budgeted tail is the MULLIGAN BOTTOMING trial games (no keep table: each candidate bottom is a full d5 b20 game): worst game 1,247,913 of 1,255,945 units are trials; 15 of 17 games >30 s mulliganed; per-decision budget honoured (worst 5.4x of its units, inside the 25x overrun ceiling); wall/unit 128 us < Hinata 211 us; Hinata's own tail is worse in the same batch (242 s). Budget-0 caps: 3 mulligan trials, 3 kept-7 genuinely wide exhaustive searches (repeated end states 1-5%; the searched Loran's scry is ~35% of one; other decks also cap at b0). Cure = the keep table (generation, not tonight). Scratch build/scratch/stuffy/opt/. |
| AB | held-out prune A/Bs (base 500000; d0 3000/arm, d3 b10 + d5 b20 1000/arm; MTG_PGO=0; per-job flags f53235400) | DONE (report logs/stuffy/prune-ab-report.md). D split-lethal: d0 won 2548->2621, avg 6.009->5.561, 959 faster/29 slower; d3/d5 neutral-better, wall 0.92x/1.04x. The 29 slower: main-1 Chain 1-4 short because ExtraLethalDamage counts +1 per untapped Forbidden Orchard but the engine spawns Spirits at turn start (27/29); fix in flight. C opp-creature prune: play-identical (changed games diverge at early land/cast choices; d3 gi7 1 turn faster), units 0.96x -> USER adopt? B unprune-9b: d0 better (pen -0.036+-0.011) via the GC-host prune (single-lift pen sum -139; Escape prune lift +38, redirect window inert): the 'can attack now' starter keeps the highest-power host, which dies in the d0 runner's all-out combat; proposal = attack counts as starter only for an indestructible host, else keep the cheapest-to-lose host (en-Kor, then lowest power); same kept-set size; amends a USER rule -> USER. d3 +0.016+-0.0095, d5 neutral, units 1.33-1.49x. 108 runaways = DEFECT: shield-redirect cascade inside AppendSubdecisionAxes->SimulateCombat is uncharged, ignores the stop flag, escapes the resolution/no-progress caps (redirectwindow+gchost lifted: 2 GB in 4.6 min); human play/viewer exposed; fix in flight (runaway-fix-brief.md) |
| RUN | enumeration runaway + Orchard over-count (runaway-fix-brief.md; handoffs runaway-fix-handoff.md) | DONE. What grew: the reflection fan's WIDTH -- an indestructible Nemesis aimed at a Nomads en-Kor gets the point back via the shield and reflects again; each line stops at the 64-resolution cut but every step is a choice, so all two-Nomads sequences were listed; the old fold never fired because its key carried the pins' cursor. 2ba33823f zero-progress cycle fold (sound identity fold, default ON, MTG_RFX_CYCLE_FOLD) + backstop poll: repro gi8 d3 unpruned b10/b0 0.33-0.67 s, 38-40 MB (was 2 GB/4 min, 8 GB/21.7 min); default play identical. e40f17e70 budget-accounting fix: each fan probe charged 1 unit (MTG_RFX_FAN_CHARGE, default ON): held-out base 500000 Stuffy d3 b10 4.5383->4.5403, d5 b20 4.5244->4.5264 (0 faster/10 slower, all 10 recover at 2x budget = budget, not a lost line), units 1.09-1.13x, CPU 0.94-0.95x; Minotaur identical. Fails 'net <= 0' by +0.002 turns -> surfaced to USER with an alternative (bill fan probes only to the Overrun ceiling). 1e37f0b1b Orchard over-count dropped from ExtraLethalDamage (MTG_STUFFY_LETHAL_ORCHARD_LEGACY=1 restores): default A d0 4 faster/0 slower; D vs A d0 958 faster/2 slower, -0.474+-0.016 turns (old count had 30 slower; 28 gone); D d3 2 faster/1 slower, all 222 main-1 sweeper turns lethal that main. d0 gi134/2145 = Shaman pull credited by the projection but never made by the d0 runner (open). Tiers: unit 3666, scen 584; smoke+regression 592/599 digests unchanged, the 7 Stuffy keys accepted ae755a9da. |
| ESC | Loran's Escape as a bare scry 1 (escape-scry-brief.md; handoff escape-scry-handoff.md; USER 11-19: "the purpose of that card in the deck is primarily for indestructible and the combo, not for incidental scry 1" / "The scry option is sort of okay if we are in a bad spot") | BUILT, both DEFAULT OFF, 30c7aae7b (shared predicate StuffyEscapeCastIsScryOnly: grant counts only on our non-indestructible creature that damage can reach this turn). Entry: CollectActions Loran's branch (TurnSolver ~44471); step-7 folds no-effect targets to ONE scry-only target, opp creatures emitted; the 9b provider prune runs only with 2+ targets, so seed-5's lone Orchard Spirit is never pruned; 9d twins per scry pin. P MTG_STUFFY_ESCAPE_SCRY_PRUNE (lossy, provider refuse; human play exempt). W MTG_STUFFY_ESCAPE_SCRY_LAST (ordering only via new PlanMoveOrderBack hook; Plan::move_front->move_tier same byte; inert at d0). Held-out base 500000 paired, MTG_PGO=0 (avg, loss=9): d0 A 5.991 / P -0.024+-0.008 (149f/112s) / W identical; d3 b10 A 4.548 / P +0.026+-0.007 (8f/30s, units 0.914x) / W -0.001+-0.002 (2f/1s, 1.006x); d5 b20 A 4.534 / P +0.023+-0.006 (5f/25s, 0.879x) / W -0.003+-0.002 (3f/0s, 1.004x). P slower replays: gi115 scry-dig to Blasphemous Act (real line), gi374 T2 scry-away-Nemesis (the 'escape early' line, a turn faster), gi954 Escape then Mind Stone draws GC -> combo: P's test cannot see a same-turn draw = UNSOUND refusal. W gi563 budget-ordering noise. Seed 5 gi4 (reference hand) d5+d3: A T4, W T4, P T5 (= USER). Tiers: unit 3716, scen 584, smoke+regression 599 pass 0 moved. SURFACED TO USER: adopt P / W / neither (agent recommends neither now, W the only candidate); whether to move Escape's slot (10) in the USER cast order, since the d0 runner gains from P. |
| VSH | viewer mana (USER 11-19: "I had to manual tap in reference 9, which is a bit of a pain. That is something to address."; "The engine makes smart decisions about tapping lands that can't possibly be needed, but doesn't go beyond that."; "the user should not need to make any extra decisions in the viewer unless they want to enable it in the settings") | DONE c0a8eef3f + e5af3b289 (cherry-picked from viewer-shock-default b3b84de9a/5acbe1315; handoff logs/viewer-shock/handoff2.md in that worktree). A: the human shock-land default pays the life unless nothing this turn can use the mana (plays the land on a copy with the life paid and asks the payer for any payable use: hand spell/ability, graveyard cast, mana ability incl. a boast before combat; restricted mana only where it pays; never at life <= payment). The tapped default was NOT new: since aa8582ca3 (2026-06-30) human play paid only when the plan segment spent mana; all 23 tapped answers in the refs are on turns with nothing castable. Spirits s9 T2 still defaults tapped (the boast's {W} has no legal source). No new dialog; viewer options unchanged. B: human payment counts the turn's flashback/retrace costs as demand (MTG_HUMAN_GY_DEMAND default ON; ordering only): s9 T4 Souls taps Territory+Courtyard+Plains, Grave stays up for the flashback {B}. Autonomous play unchanged: smoke 231/231 + regression 368/368 0 moved, scen 587, 8 new unit cases, node viewer checks pass. OWED (pre-existing, NOT this change): VPC Mill/claude_s2_gi1 + claude_s6_gi5 repaired -> shuffle-dead on the tree pushed 6bbfe4df1/eab488fcc (reproduced on the stuffy binary without VSH; the primary's older viewer binary replays them) -> FIXED (local a9066ed26, dropped at the 11-20 rebase: origin 7aa15bc1e landed the same fix, same flag + stamp, plus batch/ref_bench support): cause 2ad8c3d41 (the passive opponent shuffles on its own ordinal; both refs were recorded before it, so their opponent shuffles from Surgical Extraction and Archive Trap had bumped OUR ordinal). --legacy-shared-shuffle-ordinal for any ref without the new `opponent_shuffle_ordinal` stamp; VPC --strict 909 refs, 0 shuffle-dead. USER question: MTG_CCO_NONCREATURE_POOL ON in human menus (stop offering plays the viewer cannot pay, e.g. the s9 flashback once the Grave is spent; today offered and labelled known_drops)? |
| SLA | split-lethal gate: d0 gi134/2145 pull credit (open in RUN) + gi930/gi1717 (exposed by it) | DONE d591a36f6, gate still DEFAULT OFF (USER decision). ROOT CAUSE: the gate's projection credited Shaman en-Kor pulls it cannot see executed (gi134: the d0 runner never pulls), and did not credit the attack AFTER the sweep: the goldfish never blocks, so every surviving attacker connects. gi930/gi1717 projected 14 (7 reflected by Nemesis/Spiteful Sliver + 7 en-Kor shield forward) vs 15 life; the kill was the shielded Shaman en-Kor attacking for 1, which the pull credit had stood in for by accident. FIX: StuffyLethalProjection(credit_pulls, credit_combat); the gate uses (false, true) = no pull credit + credit each attack-capable survivor's power (indestructible, under lethal damage, or an en-Kor shielded by a reflector); MTG_STUFFY_LETHAL_PULL_LEGACY=1 restores (true, false); ExtraLethalDamage keeps (true, false), so default play is unchanged. Held-out base 500000 paired, MTG_PGO=0, A = split off / D = amended / Dleg = legacy (avg, loss=9): d0 3000 A 6.4373 / D 5.9613 (D-A -0.4760+-0.0158, 956 faster/0 slower; D-Dleg -0.0077+-0.0023, 16/0) ; d3 b10 500 A 4.5020 / D 4.4940 (D-A -0.0080+-0.0049, 5/1; D-Dleg 1/0); d5 b20 500 identical. A arm byte-identical to the RUN A/B (avg + units). The one slower game d3 gi105 (A T5, D T6) diverges on a T1 land tie (Arid Mesa vs Inspiring Vantage, so the shuffles differ; the gate never fires in a real turn); --depth 8 --budget-ms 0 both T5, same digest 695c091d8c87d027 = adoption bar met. New scenario stuffy_d0_split_lethal_attack (gi1717 T5 board, kill T5; fails under the legacy projection). Unit + scenarios pass on the amended binary. USER decision 3 (adopt MTG_STUFFY_SPLIT_LETHAL_M1 ON) now rests on these numbers. |

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
- USER (~07:30 11-18, verbatim, answers the batched open questions):
  1. "If the redirect target has already left the battlefield before it can happen then yes, there is no opportunity
     for it to happen. I'm not sure how this can occur goldfishing with this list, though." => en-Kor shield to a
     departed target: not consumed, doesn't apply (default CONFIRMED).
  2. "The simultaneous triggers using one order is fine, except we need to make sure that it is the best order when
     the order matters. Overall, these seem fine." => Q-U1 deferrals ACCEPTED; OWED: show the canonical trigger order
     is the best order wherever order matters (else search the order there).
  3. "We should probably work through this, perhaps after other work on Stuffy dries up." => Q-U6 (engine-wide
     main-1 pass) scheduled AFTER the Stuffy work.
  4. "I don't quite get this, but clairvoyance is expected of a clairvoyant engine to a degree. That isn't to say we
     don't want a non-clairvoyant heuristic prune for the performance of the scry, though. I think could make sense."
     => land-ETB scry sizing is not a defect per se; OWED (low priority): a non-clairvoyant provider prune for scry
     performance.
  5. "If there is a bug built-in we should fix it." => R7 rollout kill paths (indestructible / death triggers): FIX.
  6. "Makes sense to fix. It isn't a rush, but I would rather not be bitten in the future." => SBA / CounterPingKillAt
     controller-vs-owner graveyard: FIX (not urgent).
  7. "Right, there are no choices there." => the 3 no-choice viewer-audit classifications CONFIRMED (Act cost
     reduction, Chain X, Nemesis rider); aura_host_damage_back is now a dig prompt (9e).
  8. "I'm not sure what prompts for pull, but once per loop might make sense." => viewer Shaman-pull prompt: ask
     once per loop (investigate what prompts it first).
  4b. (~07:35, verbatim, refines 4) "With the clairvoyance case, it would be a different story if it slipped through
     to non-clairvoyant search. In that case it is a real bug to fix." => OWED: check whether the land-ETB scry
     sizing (EnumeratePlansWithLand Plan::scry_choice) reads the unseen top in NON-clairvoyant search; if it does,
     FIX it as a bug.
  + "Yes, let's check that White Knights' result change." => investigate 451a4c571's WK digest move
     (logs/stuffy/gt-attribution.md: LookupCached side-effect hypothesis; caches must not change units).
- USER (~08:50 11-18, verbatim, on the WK digest move traced to 451a4c571's DuplicateCastHasUpside sitting above the
  d0 gate of OfferDuplicateLegendCast): "Yes, we should restore the gate most likely. It's too tricky to get depth-0 to
  be smart about this for now." => OWED (queued after the E-fixes): move the DuplicateCastHasUpside check BELOW the
  `!HumanPlayActive() && !g_searched_play` d0 gate (restores the 2026-08-29 ruling "turn it on all of the time, but
  have d0 ignore that option"); expect WK d0 and likely Merfolk Turns d0 GT to move back -> accept with this provenance.
- USER (~07:00 11-20, verbatim, answers the 7 batched decisions):
  1. Loran's Escape scry-only (P / W / slot): "The real question is how much of that usage is clairvoyance and how
     much a real play. I suppose if you have the cards to win otherwise it might make sense, but if it is relying on
     clairvoyant draws that isn't so great. All that said, it might not be worth our effort fiddling with this unless
     there is a significant benefit somewhere." => NEITHER P nor W; Escape keeps slot 10; no further work unless a
     significant benefit turns up. Both levers stay built, default OFF.
  2. MTG_RFX_FAN_CHARGE: "Let's try (b)" => OWED: bill reflection-fan probes only against the Overrun ceiling (not
     the normal budget); measure vs (a) and vs the pre-charge tree; adoption bar as usual.
  3. MTG_STUFFY_SPLIT_LETHAL_M1: "I see, so this is a d0 fix. I guess that is okay, though it shouldn't have a big
     impact on searched." => ADOPT, default ON (accept the moved Stuffy GT keys with this provenance).
  4. MTG_STUFFY_OPP_CREATURE_PRUNE: "Why not just always target the opponent in the prune? But yeah, obviously we
     should not waste anything on their creatures." => ADOPT, default ON. Answered: the only own-creature targets
     the provider keeps are a Guilty Conscience host (damage into it starts/continues the loop -- the USER's own
     "a reflection aimed at it" starter) and a Sliver with 2+ Spiteful grants (reflects once per grant), plus F2c
     candidates while a GC is out; every other own target is already folded to the face.
  5. GC-host amendment: "I don't really care about this. I doubt this is worth doing. Just leave it as-is. But yes,
     nothing should be en-Kor first. I think the higher-power reflector that can attack is slightly preferred
     because the loop is shoter. (otherwise it doesn't matter) If we really wanted to fix d0 we would just not cast
     guilty conscience except when it provides lethal either by finishing the opponent after combat damage or
     through the combo which requires indestructible." => NOT BUILT; GC-host prune stays as is.
  6. MTG_CCO_NONCREATURE_POOL in human menus: "This is fine to turn on as long as we make sure to fix any issues we
     might find. I am not okay with needing to replay references I already did, so any that are broken need to be
     fixed." => OWED: ON for human menus; every reference must still replay (VPC --strict 0 drift / enum-gap; fix
     the engine or checker for any that break -- never a re-save).
  7. Karoo bounce float colour (sweep issue E): "(a) is the way to go and should be sufficient when combined with
     manual tap." => OWED: pip-aware human default float colour (the colour something in hand can spend this
     phase); manual tap stays the override; new `karoo_float` stamp + legacy replay flag so older references replay
     as recorded. Closes sweep flag E once built.
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

## Claude-play sweep

Stage 5d, 2026-11-20. Driver: Claude (Opus agents, one per seed, via the Workflow tool), `--claude-play --max-turns 8
--reveal 6`, shipped profile; bench = the shipped search (d5 b20) on the same seeds.

commit: `8c18a5163` (local, pre-rebase; rebased as 13b0f8ab7, src = d591a36f6)
seeds: 930000-930015
games: 16
flags: 1 unresolved (E: a USER decision, below)

Result: Claude's win turn = the search's in 15 of 16 games (T3-T6). 930012: Claude T5, search T6. Verified as a
BUDGET effect, not a lost line: the search holds the T1 Arid Mesa and finds the T5 kill at b200+ and at
`--depth 8 --budget-ms 0`. No engine or card defect, no fix.

12 flags, every one reproduced by an independent Opus verifier (results + verdicts:
build/scratch/stuffy/sweep/results.json). They reduce to 7 issues:

| id | seeds | classification | fix |
|----|-------|----------------|-----|
| A | 930006, 930009, 930014, 930015 | engine bug | Full-order human menus (DecisionUnpruned SearchOrder) offered `Loran's Escape -> X, X` with X still in HAND. Realised order cast the Escape first, with no legal target (CR 601.2c), so it fizzled and spent its {W} with no prompt. TurnSolver's ordering expansion filtered that shape (OrderingPlacesTrickBeforeHandTarget) only for rfx plans; it now filters every plan. New check test/claude_play_trick_order_check.py walks the 4 recorded games: 50 main-phase menus, 0 violations (it fails on the unfixed tree). FIXED |
| B | 930007, 930008 | protocol defect | The multi-ordering drop probe ran ApplyPlanDirect on a plan with the land undecided, so it played the GREEDY land (e.g. the second Sacred Foundry) and labelled an unpayable line `not_verified` with no drops (Mind Stone after Maniac with 1 mana left). The single-ordering probe already pinned `land_decided`. The ordering probe now pins it too, human play only, so search is untouched. human_plan_realised_check: 712 committed plans, 0 realised differently from their label. FIXED |
| C | 930004 | protocol defect | Stuffy Doll's as-enters choose_player listed creatures. Picking one was silently dropped, and "opponent" stood. The legal set is now players only (main.cpp choose_player legal set). FIXED |
| D | 930005 | protocol defect | A `divide` reply (en-Kor shield, Fury, fixed-damage divide) that did not sum to the total was silently replaced by the engine's allocation, and the trace recorded the rejected amounts. Now a stderr `[claude-play] WARNING`, and the trace records the APPLIED allocation, with the reply under `rejected`. FIXED |
| E | 930004 | viewer defect, USER decision | When Boros Garrison bounces an untapped dual, the engine taps it "in response" and picks the float colour itself (SpellEffects FloatLandBeforeSacrifice); human play is never asked. In 930004 {W} would have cast Guilty Conscience. Options: (a) pip-aware human default, (b) a viewer setting that asks, (c) both (recommended). Any option needs a new `karoo_float` rule stamp plus a legacy replay flag so older references replay as recorded. OPEN, USER |
| F | 930011 | viewer defect | The simultaneous-trigger order prompt listed two Guilty Conscience triggers as just "Guilty Conscience". Entries now read `<name> on <host>: deals N to it` / `<name>: reflects N` (dmgrx::t_viewer_dig_labels; claude-play `label`, viewer panel). FIXED |
| G | 930011 | cosmetic | The combat event's life arrow was read after damage triggers had resolved: "7 to opponent (9->-1)" with 3 of it from Reckoner's reflection. The event now appends "+ N from damage triggers" (viewer text only; LogAttack unchanged). FIXED |

Fixes: 8fc873144. Validation: build, unit, scenarios, trick-order check, human_plan_realised_check,
viewer_protocol_check --strict over 909 refs (0 play-drift / board-diverged / enum-gap; 1 beaten suboptimal ref),
test/viewer_checks.sh (re-run on the rebased binary; its two pre-existing failures are listed in the Stage 6 report). Search play is unchanged: B is human-only; A removes only orders that cannot execute; C, D, F
and G are protocol/viewer text.

GT accepted 2026-11-20 at f6695ff3e (smoke + regression, one pooled batch, 100 min makespan): the USER-adopted
flips (344b79788: MTG_STUFFY_SPLIT_LETHAL_M1 and MTG_STUFFY_OPP_CREATURE_PRUNE default ON) plus the F2c fold
(f6695ff3e: F2c holds while a GC is out unless the SOURCE carries a GC). Only Stuffy / Stuffy-2HG keys moved (11
rows). Searched: 0 slower, 3 faster, 297 play-changed at the same score. d0: 651 faster, 1 slower (regression d0
s2002 gi145 4->5); d0 averages 6.426->5.935 (smoke) and 6.470->6.018 (regression). viewer_protocol_check --strict:
930 refs, 0 drift / contract-fail. The ~2 h Galadriel s10 gi9 replay in that run is origin's (Maze's End F5R
pay-refine Dfs, reproduced on origin 8fbbc5023 without these commits); fix queued as wip/refine-node-cap.
