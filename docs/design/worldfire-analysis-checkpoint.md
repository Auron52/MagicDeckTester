# Worldfire analysis -- checkpoint (2026-11-21 ~22:30 UTC)

Owner: the session analyzing `decks/Worldfire/Worldfire.cod` (USER asked: "Please analyze the
Worldfire deck"). Worktree `/workspaces/MagicDeckTester/.claude/worktrees/worldfire`, branch
`wip/worldfire` (rebased on origin/phase-1-2-deck-analyzer bd5a2c93f). Ledger:
`docs/design/analysis-Worldfire.md`. Free-turn design (all USER rulings verbatim):
`docs/design/opponent-lockdown-free-turns.md`.

## CODEX HANDOFF (2026-11-22 ~11:45 UTC) -- START HERE
The USER is moving this work from Claude to Codex (out of Claude usage). The Claude session's subagents were each
asked to stop at a clean step and write a handoff file. Its 2-hourly cron (cf1c0324) dies with that session: SET YOUR
OWN 2-hourly status timer (CLAUDE.md "Codex IDE equivalent"), and wait on PIDs / DONE markers, never `pgrep -f`.
The push queue was NOT joined (no stale lease).

### Four change sets, one landing batch
| set | branch / worktree | state | handoff |
|---|---|---|---|
| LTB triggers for Worldfire's mass exile | wip/wfltb, .claude/worktrees/wfltb, 0848f329e | DONE: units, scenarios, smoke 243/243 + regression 389/389 byte-identical, 940 refs 0 drift | (report in the section "LTB: DONE" below) |
| Cantrip keep order (bucket discard + Brainstorm/Ponder/Preordain rules + deadline order) + box_lane fix 6c5ae8c8d + Worldfire docs | wip/worldfire, this worktree | DONE, BAR MET: defaults ON (2f5957f0b), smoke+reg GT accepted for worldfire keys only (27648ff25); 7 A/B + 18 tier slower games all recover (b200 or d8 b0) except s3003 gi54, shown clairvoyant (T2 Ponder shuffle; blind ensemble 5.5 vs 5.5). Creature_Burn/claude_s9_gi8 strict-ref fail is pre-existing, fixed on shared by d0d5f8463 (clears on rebase) | logs/worldfire/adopt-handoff.md |
| TutorSearchWidth deleted (+ Hinata Gamble merit rule v2, Melira prune, cards.json notes) | wip/tutorwidth, .claude/worktrees/tutorwidth | pass 2: held-out A/B + d8 b0 of its slower games in progress | logs/tutorwidth/handoff2.md (pass 1: handoff.md) |
| Free turns default ON (+ bounceland opening default ON with stamp/legacy flag, spawn requirement free-turn-SCOPED, MTG_FREE_TURN_REACH stand-down optimization, MTG_OPP_SPAWN_DRAW_T1 default OFF) | wip/freeturn, .claude/worktrees/freeturn | pass 6: validation chain8 (units, reach2 batch, audit must be 0), turn-0 opponent land-drop-on-the-draw investigation | logs/freeturn/handoff7.md (earlier: handoff6.md) |

Each set's bar: LTB byte-identical (met). Cantrip: USER 1a (improvement + no unrecoverable slower game that cannot be
shown to be clairvoyance). Tutor width: the no-greedy bar (every deck net <= 0 on held-out, every slower game recovers
at d8 b0; blind arms are never d8-gated). Free turns: USER design, "adoption is not a question" once trait-0 decks are
byte-identical (bounceland opening excepted, which moves Karoo/Lake-opponent games), refs 0 drift, reach audit == 0.

### Landing procedure (after every set has met its bar)
1. Integration branch off origin/phase-1-2-deck-analyzer; rebase/cherry-pick in order wfltb -> worldfire -> tutorwidth ->
   freeturn (no merges). Resolve test/regression_gt.txt + gt_logs by RE-ACCEPTING, never by hand (CLAUDE.md rebase rules).
2. ./build.sh; units; scenarios; ONE pooled `test/regression.sh --both` under scripts/heavy_run.sh; account for EVERY
   moved key by one of the four sets (Worldfire keys: cantrip + free turns; Cyclone/Shadowborn/Sea Creatures/Prevent
   Damage/Elementals/Extra Turns: free turns; Hinata/Goblins/Melira/...: tutor width; Karoo/Lake opponent games:
   bounceland) -- an unexplained mover is a defect. Accept smoke + regression (overnight waits for its periodic run).
   `python3 test/check_gt_logs.py`; reference sweep 0 drift; `python3 scripts/check_references_safe.py`.
3. `python3 scripts/push_queue.py join`, background `wait`, at the lease: fetch + rebase, re-validate if the rebase
   replayed gated changes (under `push_queue.py hold`), `land`. CI watch `gh run watch <id> --interval 60` in background.
4. VIEWER READY for USER references (step 2b of "Next, in order"): fast-forward the PRIMARY checkout, rebuild, tell the
   USER. Untracked decks/Worldfire/Worldfire.cod + two design docs there collide with tracked files: compare first.

### After landing (from "Next, in order")
Follow-up cantrip A/B (USER 1b/1c list below); re-measure the Worldfire budget curve and re-screen the b0 backstop rows
(USER Q16: optimize first); write the LTB owed items into a deferred doc; Stage 6 report.

### The ONE open USER decision
Sire-fair / annihilator-fair lock checks (free-turn doc "USER decisions open after the third pass"): parent recommended
both ON (consistent with the USER's "underestimation of the effect on the opponent"); they ship OFF until the USER answers.
Do NOT surface anything else as a question unless it is genuinely blocked on a USER decision (USER 2026-11-22).

## Done
- Stage 1 coverage: 6 missing (Worldfire, Keldon Halberdier, Mystical Tutor, Personal Tutor,
  Overmaster, Mistrise Village); rest full.
- Stage 2 by integrator #1 (handed off at ~340K): WIP commit 6634f2bb0 -- six cards.json entries,
  `ResolveExileAllAndSetLife` (executor + rollout), off-suspend creature haste, own_death_live armed by
  Worldfire (fetch at life 1 is death in both worlds), `WorldfireProvider` (TutorSearchWidth 32,
  MTG_WORLDFIRE_TUTOR_WIDTH=0 -> 6), test/unit/test_worldfire.cpp, viewer INERT_PARAMS. Handoff:
  `logs/worldfire/integrator-handoff.md` (gitignored, on disk).
- Parent: clean `./build.sh` at 742c81b11 (9p "Cannot allocate memory" needed retries).
- 2-hourly session-only status cron cf1c0324 (`7 */2 * * *`).

- Stage 2 gates (integrator #2, b4a74b519): all pass; smoke byte-identical (235, play-changed=0);
  coverage 22/22 full; costs + fields clean; rules review clean. Sanity d5 b20 40 games: 39/40 won,
  avg 6.0 over wins; Worldfire cast 28/40, 23 won next turn; 2 casts with nothing suspended (gi28 T3
  blind cast -> Stage 5). Pre-existing: test_shadowborn_int9 Thrumming Stone fails at bd5a2c93f;
  unit_par.sh miscounts shards (exit 3 when green); provider_audit red on Landfall/Selesnya; suspend
  writes no JSON-log action.

- Windows CI 44900654317 (b4a74b519): success.
- USER rulings 2026-11-21 (verbatim in analysis-Worldfire.md): free-turn depth = no ply cost; the four
  deferrals APPROVED; INERT_PARAMS APPROVED; the tutors prune to missing combo pieces; Rift Bolt held for
  the combo.

## Prunes (committed 296d53c8d, built, test_worldfire 28/28)
- Tutor combo-piece prune + Rift Bolt hold + OffersCastNow/OffersSuspend hooks (MTG_WORLDFIRE_TUTOR_PIECES /
  MTG_WORLDFIRE_HOLD_BOLT, =0 off). Fix found by the unit test: a pending Halberdier waives the +1 Bolt-suspend
  mana in mana_short.
- Stage 4 baseline profile (pre-prune) in ledger 38f30b9e3; profile json still UNTRACKED -> re-run after the A/B.

## Prune A/B DONE (ledger 384880ad7, 625a8f818)
- ON beats OFF on both blocks (9100001: 5.734 vs 5.891; held-out 9200001: 5.760 vs 5.969, 19 better / 3 worse;
  slow games 3 vs 11). Proposed Bolt race-release (MTG_WORLDFIRE_BOLT_RACE) measured net worse -> OFF.
- Smoke byte-identical with the prunes (235, play-changed=0).

## Done since (2026-11-21 ~16:00 UTC)
- Stage 4 re-run WITH prunes: profile committed b67de3cf3 (COST_NEUTRAL, discard STATUS_QUO_OK, policy PASS).
  provider_audit: Worldfire clean (pre-existing reds only: Landfall, Selesnya).
- Suite rows (all three tiers + worldfire2hg canary + backstop 30401/30521, screened: b0 is DEGENERATE --
  most games hit the 2M cap) f01bff837; cost gate 2.06x Hinata PASS (suite_cost.json 70fc1c321).
- Land-play fix (agent #2) DONE: wip/opp-landplay 53eb009bc + b8d66c163 (scenarios name their opponent list,
  default White Knights). Cherry-picked here as 4668b69b7, 355e5282f. Its tiers: smoke 176/235, regression
  277/375 keys move; 32 better / 213 worse games, 199 of the worse vs life-gain lists (correct rules).

## Done since (~17:00 UTC)
- GT accepted smoke+regression for the land-play fix + Worldfire NEW keys: c81c9fd51 (1369 keys, consistent).
- Claude-play sweep DONE (16 games, ledger f2577e4a5): 0 legality flags; FOUND + FIXED the Irencrag Feat
  restrictor counting a SUSPEND as a cast (008f1147d, scenario worldfire_feat_suspend_bolt_kill; scratch
  worktree .claude/worktrees/wf-featfix, branch wip/wf-featfix -- remove later via safe_worktree_remove.sh).
  Deferred items: docs/design/worldfire-sweep-deferred.md.

## LANDED 42d819bc0 (2026-11-21 ~18:00 UTC)
- Rebased onto origin, GT re-accepted smoke + regression (1409 keys, check_gt_logs consistent), pushed via
  push_queue. Windows CI 44935787667: success. Feat-fix slower game gi52 recovers at b200 (40bd2d0c5);
  verify_deck --write-ledger GATE PASS (7d2067a9c). Those two docs commits ride along with the next landing.

## Done since (~22:30 UTC)
- 5c2 tie-break KEEP; 5b monotone; 5c budget-bound at d5 b20 (d5 b200 17 better / 0 worse) -- ledger 2ecb24d7b.
- Scratch worktrees wf-featfix, opp-landplay-base removed (references verified safe).
- Free-turn (wip/freeturn 2d373428c + d35211372, pushed): flags-OFF smoke 243/243 byte-identical, scenarios
  605/605 both flag states; flag-ON regression measured (ledger 4a7ff87ea). Worldfire credited ~1 turn, raw
  rounds slower. Pre-adoption holes: Creature Burn ref claude_s9_gi8 drifts under the flag; steal_tap_count lock
  trait (Vampires 3x units); passive opponent's oldest-first cleanup hands Burn / Extra Turns / RW Burn free
  turns.

## Done since (~23:00 UTC)
- Budget curve d5 b20/50/100/200, 200 held-out games (ledger 5a3c9d2be); gi53 wins T6 at d8 b0 (548ab8969).
- Windows CI 44952777971 (wip/freeturn d35211372): success.
- Bucket design for the cantrips + discard (USER request + 12 USER amendments, verbatim):
  docs/design/worldfire-discard-buckets.md (latest 36f17f841).
- Free-turn follow-up agent stopped at 269K: handoff /workspaces/MagicDeckTester/.claude/worktrees/freeturn/logs/freeturn/handoff2.md
  (b861f7160 measured; 3abd94208 cleanup-floor WIP UNBUILT; its 5 USER questions are in its report / handoff).
- FOUND: my landing 42d819bc0 shipped a reference play-drift (Creature_Burn/claude_s9_gi8; tiers_land.log said
  REGRESSION DETECTED). Every session's regression tier inherits it.

- Reference fix LANDED d0d5f8463 (join --urgent), Windows CI 44965126799 success.
- Free-turn pass 2 DONE: wip/freeturn 23588e8a8 (rebased on d0d5f8463; cleanup floor replaces lands-first;
  no trait=0 free turns; Vampires / Sea Creatures units 1.000). Flags default OFF; USER questions in the report.
- Found: scripts/unit_par.sh --first/--last does nothing (every shard runs all 3889 cases) -- deferred tooling fix.

## Running (2026-11-22 ~07:30 UTC) -- 4 agents, all parent-launched
- CANTRIP ADOPTION (worldfire worktree; never `commit -a` here): ON + MTG_WORLDFIRE_ORDER_DEADLINE; no game persisted
  at b200 (so the clairvoyance clause never applied). Units done; tiers ran; now a 72-job attribution batch. Then
  GT accept (worldfire keys only), ledger, push wip/worldfire. Handoff if needed: logs/worldfire/adopt-handoff.md.
  Results so far: ON vs OFF -5 / -11 at 0.28x units; ON+deadline -6 / -12.
- FREE-TURN pass 4 (freeturn worktree, from logs/freeturn/handoff4.md): flag-ON regression (reg_on5) 0 trait-0 keys
  moved of 389, trait-0 units 1.000; flags-OFF smoke 243/243 play-changed=0; units 3896/3896 on 5267c39d0. chain6
  queued: fairness batch, spawn global-vs-scoped + root cause, flag-ON reference sweep (LIST only), bounceland
  measurement; then the doc "Third pass" with the flip-to-default-ON delta. Handoff: logs/freeturn/handoff5.md.
- TUTOR-WIDTH pass 2 (tutorwidth worktree, from logs/tutorwidth/handoff.md): Hinata Gamble merit rule (v2 built),
  Melira tutor prune for its b0 row, Goblins reserve deletion kept, stale cards.json notes; held-out A/B vs OLD widths
  queued on the heavy lock. Bar: every deck net <= 0, every slower game recovers at d8 b0. Handoff: handoff2.md.
- LTB (.claude/worktrees/wfltb, wip/wfltb off f9a59b8a7): Worldfire mass exile through the shared LTB path; built
  after repeated 9p ENOMEM retries; sharded unit run going. Handoff: logs/wfltb/handoff.md.
- box_lane admission bug FIXED 6c5ae8c8d (wip/worldfire): a 4-slot job at cap 6 needed 26 GB on a 23 GB box.

## LTB: DONE 2026-11-22 08:50 UTC -- READY TO LAND (wip/wfltb 0848f329e)
Units (2 pre-existing fails: Thrumming Stone; Prevent Damage useful-mode payment -- order-dependent), scenarios,
smoke 243/243 + regression 389/389 byte-identical (0 play changes, check_gt_logs consistent), 940 refs 0 drift.
Owed (agent-side, record in a deferred doc at landing): DestroyAllEnchantments / SweepOrphanedAuras /
SacrificeDepletedLands bypass LTB (unreachable in current decks); Ilharg exile half, Detention Sphere return, Severance
Priest Spirit LTB still deferred; opponent-controlled Keldon Marauders ping sets opponent_lost_life_this_turn (bug);
Skyclave/Wanderwine exile markers share base -0x20000000.

## FREE TURNS pass 4 DONE 2026-11-22 ~09:20 UTC (wip/freeturn 036445e01, logs/freeturn/handoff5.md); pass 5 RUNNING
Checks PASS (trait-0 byte-identical, 940 refs 0 drift, reference LIST EMPTY -> nothing for the USER to approve).
Parent rulings from the USER's standing rules: spawn requirement ships FREE-TURN-SCOPED ("no change to non-locking
decks"; global = 58 non-trait smoke keys +0.635 via Searing Blaze/Blood, Blasphemous Act, Hinata, DotH, Swords losing
the T1 creature); 2b cost (Sea Creatures +26% units) -> OPTIMIZE (USER Q16), not a question. Pass 5: Maze gi168 repro,
bounceland stamp + legacy flag + default ON, MTG_FREE_TURNS default ON (no GT accept: parent accepts on the integrated
tree), 2b stand-down optimization, optional on-the-draw T1 land lever. GENUINE USER decision surfaced 2026-11-22:
Sire-fair / annihilator-fair (ship OFF until answered).

## Next, in order
1. Relay each agent's report as it lands; surface its USER questions.
2. LANDING BATCH via push_queue when ready: cantrip adoption + box_lane fix + worldfire docs, tutor-width (if bar met),
   LTB (if tiers byte-identical), free turns default ON (USER: adoption is not a question, once checks pass). CI watch
   --interval 60.
   USER-pending (bring with numbers when measured): spawn-land rule global vs free-turn-scoped; the reference LIST.
2b. VIEWER READY for USER references (USER 2026-11-22: *"Let me know when you are at a point where the play viewer
   can be run for this deck. I would like to create references for it at some point."*). The deck + every card are
   already on origin (1d1aa4b3f era); the free-turn rule (default ON) and the LTB triggers must land first, since free
   turns change what the opponent does after Worldfire resolves. After the landing batch: fast-forward the PRIMARY
   checkout (`git -C /workspaces/MagicDeckTester pull --rebase`; it was 100 behind on 11-22 06:45, with untracked
   decks/Worldfire/Worldfire.cod + docs/design/{analysis-Worldfire,opponent-lockdown-free-turns}.md that collide with
   tracked origin files -- compare content first, never delete user work), `./build.sh` there, then TELL THE USER.
3. After the cantrip adoption: FOLLOW-UP A/B agent (USER 1b/1c list in "USER rulings on the cantrip keep order").
4. Then re-measure the Worldfire budget curve and re-screen the b0 backstop rows (USER Q16: optimize first).
5. Only surface GENUINE USER decisions (USER 2026-11-22: nothing waiting on our side or on measurement).
6. Stage 6 report; CronDelete cf1c0324 when everything is reported.

## Pending for the NEXT free-turn agent (from handoff3.md, plus these USER items given after it)
- USER 2026-11-22, Sire fairness check: *"Okay, that sounds worth trying."* -- build the "could have deployed a
  creature" check (lever, default OFF), measure on Shadowborn vs Sire-in / Sire-out.
- USER 2026-11-22: *"If we do that rule for Sire we should also apply the same rule to Annihilator if even just to
  check if it would make enough sacrifice targets to keep a permanent."* -- same lever family as the Sire check:
  before an annihilator sacrifice that would leave the opponent locked, add the nonland permanent cards it could
  have cast from hand (MV <= its lands, colours available) as extra sacrifice targets; if real + virtual permanents
  exceed the annihilator count, it keeps a permanent and the lock does NOT hold. Check only, no state change; default
  OFF; measure on CYCLONE (USER: "Probably Cyclone is the more relevant deck for that test, since Extra Turns tends
  to get its own turns. It doesn't need the free ones normally."; Cyclone has 161 free-turn games), Extra Turns as a
  secondary check, with the Sire arm in the same pooled batch.
- USER 2026-11-22 (references under the rule): *"We could correct existing references as needed. I suppose the only
  change would be that some cases give Free Turns. Perhaps get my approval before changing any, though. I don't want
  to lose access to the references because of this change. That would be silly."* -- run a flag-ON reference sweep
  WITHOUT the --legacy-no-free-turns override (diagnostic only) and LIST every reference whose replay changes (gains
  a free turn / drifts). Change NO reference file; bring the list to the USER for approval first. Keep the legacy
  replay path so every reference stays replayable.
- USER 2026-11-22 (bouncelands): *"That's okay, except that we shouldn't be playing a bounceland first if we don't
  have an existing land unless forced by what the player does. The basic replacing bounceland rule should apply."*
  -- generalise the opening-hand "every land needs another land in play -> replace the rightmost with a basic" rule
  from the free-turn hurdles to EVERY game (own lever; a flag-off play change for games vs Karoo/Lake lists:
  smoke + regression, list the moved keys, GT accept via the normal flow after the USER sees the list). Post-Worldfire
  (forced) a drawn bounceland still returns itself.
- USER 2026-11-22: *"What might be nice is to include the average of the extra turns as a separate number, even if
  we don't optimize for it. This is just informative for agents and users."* -- report the average free turns
  (and extra turns) taken per game as a separate, informational number beside the credited avg (batch per-job
  line, --games summary, tier report); never used by the search or the GT digest.

- USER 2026-11-22 (2a, replaces the keep-2-nonlands floor): *"The opponent should discard whichever is more common
  between lands and nonlands including hand and field I suppose. (not counting the creature tokens) That should avoid
  any weirdness as we will try to keep the thing we might be missing."* -- re-count before each discard; MDFC in hand
  uses the "fewer side" rule; exact tie -> discard a land (parent's default, surfaced).
- USER 2026-11-22 (2b, one-land removals lock): *"Yes, it should. We don't have a deck like that yet in the list, but
  I will be adding one later that turns things into artifacts and destroys them. I believe these rules cover that case
  well enough as an underestimation of the effect on the opponent. Those decks need to lock out all lands and the
  turns where the opponent can play a land would still not be free."* -- one-land removals join DeckMayLockOpponent's
  IN set; list suite decks that flip trait 0->1 and whether their games move.

## Other USER answers 2026-11-22 (Q12-Q16)
- Q13 life-gain lands: the moved games are vs Angels (Seraph Sanctuary) and Landfall (Radiant Fountain); also
  Blossoming Sands / Kazandu Refuge carry etb_lifegain but are in no list. Shockland life for the opponent: USER
  *"I guess we can leave that part for now. Unless we want to come up with a standard rule like shockland damage
  should be taken when it is in the first three lands."* -- DEFERRED (proposal below in the report).
- Q15 White Knights default scenario opponent: *"I guess that is fine."*
- Q16 degenerate b0 backstop rows: *"If these are bad enough the first step is always to optimize."* -- after the
  cantrip buckets land, re-screen the Worldfire b0 rows; if still degenerate, census the b0 games (where the 2M
  units go) and optimize before touching the rows.

## USER rulings on the cantrip keep order 2026-11-22 (~03:30 UTC), VERBATIM
- 1a: *"Obviously if I designed the rules and they work well (i.e. improvements in average quality and no
  unrecoverable that cannot be shown to be clairvoyance) then we should turn them on."* (relayed to the adoption agent)
- 1b: *"If there is no difference I would keep the rule as designed. These are worth considering only if we see them
  perform better in testing. If the difference is small we should try a larger test set to separate them. I have no
  issues with any of these options IF they perform better."* -- arms: _AGGR_DIG_BLUE, _BAZAAR_T1, _AGGR_DIG=0,
  _AGGR_FINISHER=0. FOLLOW-UP agent: larger held-out set for the small-difference arms.
- 1c1 (next land drop vs dig slot 2 when a piece is missing): *"We'll have to play with this a bit. My initial take
  is that the land drop is nice, but I would take more dig over it if our mana is good already."*
- 1c2 (third dig below the quota lands): *"That is potentially okay, though I would keep the third dig if we are
  missing both combo pieces. I'm not against doing some A/B with rules like this, though. Some of them may be close
  enough that they are just my preference. For example, my finding is that you can skimp a bit on mana if you have
  to dig a lot. You tend to draw land as part of the process."*
- 1c3 (spare tutor = acceleration while mana short): *"That makes sense. The tutors are goodif we can fill all of the
  slots and not quite so good otherwise because it gives up a draw. We don't want more than 2 at any time."*
- FOLLOW-UP agent (after adoption lands in wip/worldfire): A/B against the adopted ON baseline, each its own lever:
  (i) dig slot 2 > next land drop when mana is already good; (ii) keep a 3rd dig when both pieces are missing;
  (iii) "skimp on mana when digging a lot"; (iv) cap kept tutors at 2; plus the 1b arms on a larger set.
- Still open: 1c4 (next-drop tier after this turn's land), 1c5 (Mystical Tutor free {U}).

## USER rulings 2026-11-22 (~04:00 UTC), VERBATIM
- 2c (MDFC tie -> NONLAND): *"Sure, that sounds fine."*
- 2a tie (majority-kind discard, exact tie -> discard a land): *"Defaulting to discarding land sounds fine."*
- 2d (free-turn adoption): *"Okay, but this is not a question."* -- i.e. the rule is the USER's design; adopt once the
  agent's checks (trait=0 byte-identical, fairness arms, reference list) pass, and report.
- 3 (no ply cost for free turns): *"Let's try it this way for now. If we find this restrictive or troublesome we can
  re-adjust it."*
- 4 (deferrals): the Overmaster / Mistrise riders were already approved (2026-11-21). Worldfire LTB triggers: *"I
  don't like the Leave-the-battlefield triggers one as that seems like a future risk even if it doesn't impact this
  deck, so we should build something there."* -> LTB agent in .claude/worktrees/wfltb (wip/wfltb, off the shared
  branch f9a59b8a7): route the mass exile through the shared LTB path, both worlds, unit tests, tiers expected
  byte-identical. Handoff: logs/wfltb/handoff.md. Parent lands it.
- The "two Worldfire settings" were the viewer-audit INERT_PARAMS entries (no human-play choice), approved 2026-11-21.

## Open USER questions (defaults taken)
- NONE. 1c4 (next-drop tier after this turn's land) and 1c5 (Mystical Tutor free {U}) are AGENT readings, not USER
  questions (USER 2026-11-22: stop raising items not blocked on the USER): keep them; the follow-up A/B may test them.
