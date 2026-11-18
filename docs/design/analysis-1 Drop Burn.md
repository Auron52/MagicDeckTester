# Analysis ledger — 1 Drop Burn

Deck: `decks/1 Drop Burn/1 Drop Burn.cod` (60 cards, mono-R: 14 Mountain, 2 Sunbaked Canyon, 3 Fireblast,
4 Lightning Bolt, 2 Needle Drop, 4 Goblin Guide, 3 Grim Lavamancer, 4 Monastery Swiftspear, 4 Vexing Devil,
4 Boltwave, 4 Chain Lightning, 4 Lava Spike, 4 Rift Bolt, 4 Skewer the Critics).

Started 2026-11-17 on `phase-1-2-deck-analyzer` @ 17262a61f.

## Stage 1 — coverage

* Missing: **Needle Drop**, **Vexing Devil**.
* All other 12 cards `full`. Goblin Guide carries a bracket note (reveal implemented against the passive
  opponent's library; the actual-deck opponent model is a separate architecture project) — existing,
  shared with `decks/burn`, already accepted.
* No sideboard; no fetchlands (`fetch_hold_review.required = false`).

## Stage 2 — cards

| card | Scryfall cost | oracle | tier | status |
|---|---|---|---|---|
| Needle Drop | `{R}` Instant | deals 1 damage to any target that was dealt damage this turn. Draw a card. | 3 | **implemented** -- `direct_damage` + `cast_draw: 1` + new `target_dealt_damage_this_turn` (see below) |
| Vexing Devil | `{R}` Creature — Devil 4/3 | When this creature enters, any opponent may have it deal 4 damage to them. If a player does, sacrifice this creature. | 2 | **implemented** -- `vanilla_creature` + new `etb_opp_may_take_damage_to_sac: 4` (PROVISIONAL opponent policy) |

### Stage 2 implementation notes

* **Damage tracker.** New `GameState::player_dealt_damage_this_turn[2]` (DAMAGE only -- combat or noncombat,
  never bare life loss / life payment; index 1 = the opposing team in 2HG). Written through
  `GameState::MarkPlayerDealtDamage` at every damage-to-a-player site (combat damage in both combat paths,
  `RecordNoncombatDamageToPlayer`, `DealPermanentAbilityDamage`, the armed damage-event model, and each
  open-coded opponent-damage site that sets `opponent_lost_life_this_turn`; the pure life-loss sites -- drains,
  attack-trigger life loss, Tainted Remedy flips -- deliberately do not mark). Reset at both untap sites;
  rolled back by every mana-payment probe that rolls back `opponent_lost_life_this_turn`; folded into
  `dominance::Build` / `BuildSimKey` only when true AND the deck constant `deck_reads_damage_dealt` is
  stamped (only decks holding Needle Drop), so other decks are byte-identical. Self-damage from painlands is
  not marked (no Needle Drop deck has one; it only affects targeting yourself).
* **Needle Drop legality.** Shared helpers in `SpellEffects.h`: `DamagedThisTurnPinLegal` (players by the
  tracker, creatures by marked damage), `DamagedTargetCastLegal` (the pre-payment cast gate in BOTH apply
  worlds, beside the Path / Aura gates), `FixedBurnCandidates` filters the cast-time list (the viewer's
  `target` decision reads it -- Bucket A) while the enumeration list (`pending`) keeps every target,
  `ResolveFixedBurnPin` / the face-only resolution re-check on resolution (CR 608.2b: no draw on a fizzle),
  `FreeCastHasLegalTargets` for the free-cast walks. A set holding Needle Drop pays per cast
  (`PP_DYNAMIC_COST`) so a refused cast never leaves pre-paid mana tapped. `DeckUsesSecondMain` detects the
  param (2c-bis). Planeswalker targets are not modelled (no per-turn damage record; no deck faces one).
* **Cast order (USER-owned -- surfaced, not decided).** `GenericProvider::CastOrderRank` gives a
  `target_dealt_damage_this_turn` card tier **29** (after creatures 10 and other noncreature spells 20, before
  the on-cast self-damage tier 30), so a same-main Bolt / Vexing Devil makes the opponent a legal target
  before Needle Drop is cast. Byte-identical for every other deck. The Burn / MidrangeBurn providers' user
  orders were NOT touched; whichever provider this deck routes to owns the final order.
* **Vexing Devil.** `ResolveVexingDevilEtb` runs from the universal own-ETB cascade (`FireOwnEtbTriggers`,
  after the entrant's other ETBs, the champion placement) -- every enter route, executor and rollout alike.
  Policy in ONE function, `VexingDevilOpponentTakesDamage`: take it iff `life > 4`. Taken: 4 noncombat
  damage via `DealPermanentAbilityDamage` (marks dealt-damage + lost-life, feeds spectacle / Needle Drop),
  then `SacrificePermanentAt` (graveyard, death/sacrifice watchers, morbid). The play viewer's history gets
  a `trigger`/`damage` event naming the opponent's choice. Not a search branch, not a viewer decision.
* **Tests.** `test/unit/test_needle_drop_vexing_devil.cpp` (both apply worlds per case).

### Modelling decisions

* **Vexing Devil opponent policy — USER RULING 2026-11-17** (replaces the provisional "take unless
  lethal"). Verbatim: *"For devil it should probably always take the damage except if it is played late.
  Given that your opponent doesn't know your hand let's make a rule that they will take the damage unless
  they are at 4 or less when you have no cards in hand or 7 or less if you are holding cards. Essentially,
  an attempt at determining whether you are likely to kill this turn without clarirvoyantly looking at your
  hand. Generally this will mean we can just stack devils early to avoid needing to wait which is exactly
  what a good player does anyway."* Implemented in `VexingDevilOpponentTakesDamage` (decline at life <= N
  with our hand empty, <= N + 3 while we hold any card; N = 4; hand SIZE only, counted at resolution).
* **Needle Drop "dealt damage this turn"** is DAMAGE, not life loss: needs a per-turn damage tracker for
  the opponent player (and marked damage for creature targets). It is an instant, so it is castable after
  combat damage; the deck already enables the second main via Skewer the Critics' spectacle, but Needle
  Drop's own param must also be detected by `DeckUsesSecondMain` (2c-bis).

## Cast order — DRAFT for USER review (2026-11-17)

USER: *"Can you make a full cast order for me to review? Note that Needle Drop should have the option to be
cast in the second main."* Drafted as `MTG_ONEDROPBURN_ORDER` (default OFF until the USER approves;
`OneDropBurnProvider::CastOrderRank`). Within ONE main phase; which main each cast goes in stays SEARCHED
(the provider applies no main-1 filter, the deck plays a second main), so Needle Drop can go post-combat.

**Amended 2026-11-17 (USER):** *"Needle drop should only be split into main 1 and main 2 casts. You can cast
it at the end of main 1 or the beginning of main 2 if we are reliant on combat damage. The tricky part is that
we need to allow casting any cards drawn in main 2 if that happens."* / *"Skewer the critics has similar
behaviour except it doesn't draw."* / *"if we cast it in main 1 that opens a breakpoint in which the new spells
can be cast."*

**Amended 2026-11-18 (USER), SUPERSEDES the 5/6 main-2 tier below:** *"The cast order is kind of weird, in
particular needle drop and skewer the critics should always default to later than other spells if possible. In
particular, they need to be cast before the attack phase if this is possible for prowess. We want to only cast
them second main if there is no other damage source than combat. Beyond the two spells, only the card drawn by
Needle Drop should be considered to cast Main 2 and only when they couldn't be cast earlier."*

Implemented under the same `MTG_ONEDROPBURN_ORDER` (still OFF pending approval):
* Ranks: Needle Drop 29, Skewer 30 in BOTH mains (the 5/6 main-2 tier is gone); Fireblast 40 stays last.
* MAIN-2 RULE (`OneDropBurnProvider::RestrictsMain2Casts` / `Main2CastAllowed`, burn's MTG_BURN_M2_RULE shape):
  main 2 admits Needle Drop / Skewer only while combat is the opponent's ONLY damage source this turn (a new
  per-turn NONCOMBAT bit, `GameState::kDealtNoncombatDamage`, written only for this rule's deck); once one of
  them is cast in main 2 the other stays admitted (Needle Drop's own 1 is noncombat). Every other hand cast is
  main-1 only, EXCEPT the card(s) drawn after the first main-2 Needle Drop / Skewer (hand snapshot at that cast,
  `Main2StagerSnapshotsHand`). Main 1 stays fully searched; human play is not narrowed.
* Agent readings inside the ruling (surfaced to the USER): (a) a Lavamancer ping or Canyon draw are
  ACTIVATIONS, not casts -- unrestricted in main 2, but a Lavamancer ping before a main-2 Needle Drop counts as
  a noncombat source and bars it; (b) the land drop is not restricted; (c) with no damage at all, main 2 does
  not offer a hardcast Skewer (it belongs before combat for prowess); (d) a card drawn by Sunbaked Canyon in
  main 2 is castable only after a main-2 Needle Drop / Skewer was cast (the snapshot), not on its own.

Previous draft (2026-11-17), kept for the A/B record:

| rank | card | why |
|---|---|---|
| land | land drop | searched at depth > 0 (unchanged) |
| 5 | Needle Drop -- *main 2, opponent already damaged (combat)* | the main's first cast; its draw is a breakpoint, main 2 re-decides with the card |
| 6 | Skewer the Critics -- *main 2, opponent already damaged* | spectacle {R}, right after Needle Drop |
| 10 | Goblin Guide, Monastery Swiftspear | haste; prowess sees every later spell |
| 12 | Vexing Devil | before the burn: the opponent decides at its highest life of the main; its 4 turns on spectacle / Needle Drop |
| 13 | Grim Lavamancer | no haste; a body this turn |
| 22 | Boltwave, Chain Lightning, Lava Spike, Lightning Bolt | name order |
| 26 | Rift Bolt (hardcast) | MV 3 |
| 29 | Needle Drop -- *end of main 1 (or main 2 before any damage)* | after the burn that makes it legal; draws before Skewer |
| 30 | Skewer the Critics -- *end of the main* | after the burn and Needle Drop |
| 40 | Fireblast | last: its alt cost sacrifices two Mountains after they have tapped |

Agent choices inside the ruling (surfaced): Needle Drop before Skewer in both windows (draw first); main 2 keeps
the damage test for Skewer (first in an undamaged main 2 it would be hardcast for 3 when a same-main Devil /
burn could make it {R}); an undamaged main 2 leaves both at the end rather than pruning that window.

### Cast-order A/B (2026-11-18, held-out overnight cells, paired, both arms one pooled batch)

`ARMSET=onedropburn CASES=overnight DECKS=onedropburn,onedropburn2hg bash test/mana_tap_order_ab.sh` at
41be66453 (Release; `build/scratch/1dropburn/order_ab/paired.txt`). Draft = `MTG_ONEDROPBURN_ORDER=1`.

| cells | games | draft - base | better / worse |
|---|---|---|---|
| d3 b10 (4 x 400) | 1600 | -0.0075 .. -0.0225 per cell | 27 / 0 |
| d5 b20 (4 x 300) | 1200 | -0.0100 .. -0.0167 per cell | 17 / 0 |
| 2HG d5 b20 (2 x 100) | 200 | -0.020, -0.030 | 5 / 0 |
| d0 (4 x 2000) | 8000 | -0.0905 .. -0.1090 per cell | 760 / 62 |
| backstops d3/d5 b0 | 10 | 0 | 0 / 0 |
| **ALL** | **11010** | **-0.0763 +/- 0.0032** (95% CI -0.083..-0.070) | 809 / 62 |

Every searched cell is <= 0 with **zero slower games**; units ~-1.5%. Mechanisms (sweep, below): Fireblast last
(rank 40) stops it sacrificing untapped Mountains a same-plan burn needed; Needle Drop ordered after the damage
(rank 29/5) inside its order-opaque set; the binding order activates the SPECTACLE PLAN CREDIT so "burn ->
Skewer {R}" is one enumerable plan. **Recommendation to the USER: adopt the draft. Still OFF pending approval.**

## Claude-play sweep
- commit: `41be66453`
- seeds: 50001..50016 (one game each; benchmark d5 b20) games: 16
- driver: 8 Opus agents x 2 seeds
- flags: 1 unresolved
  - **50008 (AI T4, Claude T3): the order-OFF canonical order casts Fireblast BEFORE Boltwave** (generic tier 20
    tie), Fireblast's alternative cost sacrifices both untapped Mountains, Boltwave is dropped: the T3 kill is
    unfindable at d5 b20, d8 b200 AND d8 b0. With `MTG_ONEDROPBURN_ORDER=1` (Fireblast rank 40) the seed wins T3.
    Resolves when the USER adopts the order (or rules on a Fireblast-last tier alone); surfaced to the USER.

Results: Claude = AI in 13 games; faster in 3 (50008 above; 50010 T4 v T5 and 50011 T3 v T5 are 1-land 7-card
keeps the default policy mulligans -- chosen with the draws visible, so mulligan-profile input, not a search
misplay). Per-game JSON: `logs/1dropburn/sweep/<seed>.json`.

Verified correct with arithmetic across the games: Vexing Devil's USER rule (takes at 20/18/17/16/14/13/12/10/8,
takes at 5 and 6 with an empty hand, declines at 7/6/5/4/3 holding cards); Needle Drop (only a damaged target,
draws, the drawn card castable in the same main incl. main 2); Skewer spectacle only after life loss; Fireblast's
sacrifice (tapped Mountains allowed); Rift Bolt suspend + upkeep free cast; Lavamancer; Goblin Guide reveal;
Swiftspear prowess; Sunbaked Canyon pain and sac-draw; Lava Spike players-only.

Resolved / non-play findings:
- PARTLY FIXED (viewer): a Needle Drop plan with no earlier damage source was listed without `drops` and silently
  did nothing. `apply_one`'s DamagedTargetCastLegal gate now reports the drop with its reason to the committed
  line's drop sink (the viewer's drop banner / the claude-play result; search silent). The MENU still lists it
  as `not_verified` (the menu trials only some plans), and **the search enumerates it** (`search_lines`:
  `engine_full: true, payable: false`): a sound collapse candidate, DEFERRED -- "certainly dropped" must hold
  for the realised order AND for self-damage (Sunbaked Canyon's pain makes US a legal Needle Drop target, a
  legal cantrip line), so the fold needs the payment's damage model, not just "no burn in the subset".
- FIXED (search waste, sound folds): Lavamancer's exile fanned over every graveyard pair and Fireblast over every
  Mountain pair; 1 Drop Burn now shares Midrange Burn's identity/dominance folds, plus a sound paid-Fireblast
  six-mana bound (`MTG_ONEDROPBURN_{FIREBLAST_PAIR,LAVAMANCER_FUEL,FIREBLAST_PAID}`, default ON).
- MENU ONLY (deferred): Fireblast plans annotate phantom drops -- the trial apply has no human chooser, so it
  does not float the sacrificed Mountain's {R} as the human commit does (`PayFireblastAlternative`).
- VIEWER ONLY (deferred): `--validate-line` rejects a legal line ending in Fireblast ("can't pay {4}{R}{R}");
  its free-alternative test (TurnSolver ~132198) knows the life-gain kind only.
- CLAUDE-PLAY ONLY (deferred): a lethal upkeep Rift Bolt does not end the game before the main-1 decision
  (GameEngine checks CheckWinCondition after main 1 / combat, not after upkeep / draw), and its damage line is
  missing from the events.
- Midrange Burn likely shares the Fireblast-before-burn drop (its order has no Fireblast tier): for that deck.

## Open questions for the USER

1. ~~Vexing Devil opponent policy~~ -- RULED 2026-11-17 (above).
2. Cast order draft (above): approve / amend.
3. Deferrals: Needle Drop cannot target planeswalkers; painland self-damage does not set the "dealt damage this
   turn" flag (inert here). USER 2026-11-17: *"the viewer does display your life which is good enough for this case."*

## CHECKPOINT 2026-11-17 (orchestrator; resume here)

* **Committed (local, NOT pushed):** 15a6a5c86 -- Needle Drop + Vexing Devil (impl agent; handoff
  `logs/1dropburn/impl-handoff.md`). Cost audit rc 0 (658/658). Field audit: snapshot refreshed
  (`src/cards/data/scryfall_reference.json`, UNCOMMITTED) and offline audit rc 0; the Vexing Devil oracle
  advisory is only its bracket note.
* **Committed c1ed42e00 (local):** `OneDropBurnProvider` (empty DeckProvider derivation; Needle Drop tier 29
  moved into it), tests, Scryfall snapshot, decklist. Unit tests pass; smoke `logs/1dropburn/smoke2.log`
  identical to smoke1 (same 39 drift keys, same got digests, 180 pass). Stage 3 coverage: 14/14 full.
* **Stage 4 done:** baseline profile written (`decks/1 Drop Burn/1 Drop Burn.profile.json`, UNCOMMITTED);
  cost diag NO_COST_INTERACTIONS; discard stage DISCARD_UNLABELLED (0 real sheds in play; rollout sheds 2221x
  -- bound with MTG_SHED_WORST A/B per the stage's advice, 5i). 4a provider_audit --check rc 0 (OneDropBurn,
  cert NotAssessed). Cost gate PASS 0.84x hinata (`logs/1dropburn/costgate2.log`).
* **4-bis rows ADDED (UNCOMMITTED, test/regression_cases.sh):** key `onedropburn`/`onedropburn2hg`; smoke
  d0 1000/d3 50 b10/d5 100 b20/2hg d5 25; regression d0 1000/d3 75/d5 150 x2/2hg 75; overnight ENTRY set
  (d0 4x2000, d3 4x400 b10, d5 4x300 b20, 2hg 2x100); backstops 30261 (regression) / 30161 (overnight), screen
  `logs/1dropburn/backstop_screen.*` (s30281 abandoned a d5 game; heavy unbudgeted tail).
  **Regression GT ACCEPTED** (7 keys, check_gt_logs clean; uncommitted).
* **RUNNING (started ~19:40 UTC):** `bash test/regression.sh --smoke --overnight --deck=onedropburn,onedropburn2hg`
  -> `logs/1dropburn/smoke_overnight.log` (DONE marker at end). It first builds PGO (harness). Then accept
  `--smoke --accept --deck=onedropburn,onedropburn2hg` and `--overnight --accept` with the same filter,
  `python3 test/check_gt_logs.py`, commit rows+GT+profile+suite_cost/job_cost.
* **5b done:** monotone; 75 shared seeds d0 4.44 / d3 3.653 / d5 3.653 (d3==d5 per game, none slower than d0);
  d5 ~49% T3, ~40% T4; d0 11/1000 no-win.
* **Next:** verify_deck.py (5a nonconv/fd-diverge etc.), 5c2 leaf_tiebreak_check.py, 5d claude-play sweep
  (~16 games, Sonnet player agents via Agent tool, seeds disjoint e.g. base 50001), 5h viewer, 5i discard
  (shed-worst A/B), Stage 6 report. Push once (4h rule; no push yet this session) + background CI watch.
  2-hourly status cron 01e1d807 is session-only.
* **RESOLVED -- the 39 smoke mismatches are PRE-EXISTING upstream drift, not 15a6a5c86.** An exact
  parent build (17262a61f, scratch worktree, since removed) replayed all 39 failing smoke jobs plus two
  passing canaries (midrangeburn d0/d3) from the primary checkout: all 41 per-game `.wins` files are
  byte-identical to the 15a6a5c86 smoke run (`build/scratch/1dropburn/parent_replay.json`). Same 39 keys
  P18i recorded as unaccepted upstream drift (sampled-gate-ladder.md). Left alone; not accepted here.
* **Next after the fix:** build, unit tests, smoke byte-identity -> commit provider + snapshot -> Stage 3
  coverage re-run -> Stage 4 profile (`analyze_deck.py --no-rebuild`) -> 4a provider_audit -> 4-bis suite
  rows in all 3 tiers (template: midrangeburn rows) + accept -> Stage 5 (verify_deck.py, 5a-5i, claude-play
  sweep on Opus/Sonnet) -> Stage 6 report.
* **2026-11-17 ~20:00 UTC: USER ruled the Devil policy (hand-size rule) and asked for a full cast order.**
  The smoke+overnight run was stopped while still building PGO (its GT would have been the old policy).
  Devil rule implemented + unit test; full order drafted behind `MTG_ONEDROPBURN_ORDER` (default OFF).
  Next: build, unit tests, smoke (only onedropburn may move), re-run + re-accept the deck's regression GT,
  A/B the draft order (held-out seeds), then smoke+overnight accept, then Stage 5.
* **~22:20 UTC: BUILD RACE caught.** The order edit landed mid-compile, so DecisionProviders.cpp.o was
  stale-but-newer (old class layout vs new header). Touched all edited files; clean rebuild RUNNING ->
  `logs/1dropburn/build_order2.log` (">>> done" at end). After it: `./build/Release/mtg-test` (all),
  `MTG_ONEDROPBURN_ORDER=1 ./build/Release/mtg "decks/1 Drop Burn/1 Drop Burn.cod" --cast-order-report`
  must show ranks 10/12/13/22/26/30/35/40, smoke (only onedropburn keys + the 39 known drift may differ),
  re-run regression `--deck=onedropburn,onedropburn2hg` and re-accept, A/B the order (held-out seeds,
  `MTG_ONEDROPBURN_ORDER=1` vs off), commit, then smoke+overnight for the deck, then Stage 5.
* **Open USER questions:** see "Open questions for the USER" above.
* **~22:50 UTC: clean rebuild OK; the draft report shows ranks 10/12/13/22/26/30/35/40.** Registered
  `MTG_ONEDROPBURN_ORDER` as a heurarm lever (`ONEDROPBURN_ORDER`, per-job manifest flag) and added ARMSET
  `onedropburn` (base/draft) + `OUT_DIR` override to `test/mana_tap_order_ab.sh`. Rebuild RUNNING ->
  `logs/1dropburn/build_lever.log`. Then: mtg-test, smoke, regression re-accept, and the A/B:
  `ARMSET=onedropburn CASES=overnight DECKS=onedropburn,onedropburn2hg OUT_DIR=build/scratch/1dropburn/order_ab bash test/mana_tap_order_ab.sh`
  (~3000 core-s, paired; result in `$OUT_DIR/paired.txt`).
* **2026-11-18 ~00:30 UTC.** USER amended the order: Needle Drop / Skewer split by MAIN (end of main 1, or
  first in main 2 after combat damage; see the amended table). Built; mtg-test 3393/3393 pass. Traced probe
  (`build/scratch/1dropburn/m2trace`, 300 games, draft ON): main-2 Needle Drop first in 12/12 first-copy
  casts; the drawn card IS cast in the same main 2 (gi28 Skewer, gi35 Bolt). Full smoke `logs/1dropburn/smoke3.log`:
  180 pass, the SAME 39 drift keys with the SAME got digests as smoke2, 4 NEW onedropburn keys -> ACCEPTED
  (filtered). Modern RW Burn's 2 Vexing Devil are SIDEBOARD only, so the Devil ruling moves no other deck.
  RUNNING: regression `--deck=onedropburn,onedropburn2hg` -> `logs/1dropburn/regression2.log` (DONE marker).
  Next: accept it (same filter) + check_gt_logs, then the order A/B (command above), commit, smoke+overnight
  rows... (overnight: run `--overnight --deck=onedropburn,onedropburn2hg` once, accept), Stage 5.
* **~00:50 UTC: regression GT re-ACCEPTED under the Devil ruling** (7 keys, only onedropburn; d0 4.436->4.441,
  d3 3.653->3.640, the rest same avg / new digest); check_gt_logs 1263 consistent, 0 stale.
  RUNNING: the order A/B -> `logs/1dropburn/order_ab.log` (DONE marker), results
  `build/scratch/1dropburn/order_ab/paired.txt`. Next: report it to the USER, commit, overnight rows, Stage 5.
* **~01:40 UTC: COMMITTED 41be66453** (Devil rule, rows+GT, profile, draft order OFF, A/B armset). Claude-play
  sweep LAUNCHED at 41be66453: 8 Opus agents x 2 seeds, 50001..50016, d5 b20 benchmark; per-game results
  `logs/1dropburn/sweep/<seed>.json`. Order A/B still running. Next: A/B report, overnight rows, verify_deck,
  tie-break, viewer, discard A/B, report, push.
* **~02:45 UTC:** A/B done (draft -0.0763 +/- 0.0032, 0 slower searched games; REPORTED to the USER, OFF pending
  approval). Sweep done (16 games, 1 unresolved: 50008 Fireblast-first, fixed by the draft). UNCOMMITTED: the
  three sound folds + the Needle Drop drop report (DecisionProviders.{h,cpp}, TurnSolver.cpp); built, unit
  tests pass. RUNNING: `--smoke --regression --deck=onedropburn,onedropburn2hg,midrangeburn,midrangeburn2hg` ->
  `logs/1dropburn/both5.log` (DONE marker). Expect: midrangeburn byte-identical (refactor only), onedropburn
  moves (folds). Next: accept onedropburn keys (deck filter), commit, overnight tier once, verify_deck,
  tie-break, viewer audit, discard A/B, report, push.
* **~04:20 UTC: COMMITTED 42c6e9007** (folds + Needle drop report + onedropburn regression GT re-accept, 4 keys
  digest-only; smoke unchanged; check_gt_logs 1263 consistent). midrangeburn smoke + 8/9 regression PASS;
  **midrangeburn2hg_regression_d5_s2002 moved 5.9600/473dd2a6 -> 5.9467/bf75dda8, NOT accepted** -- the folds
  cannot touch it (no Needle in that list; refactor identical), 95 upstream src commits since its 11-15 accept.
  RUNNING: attribution build of upstream tip 17262a61f in `.claude/worktrees/odb-attrib` (log
  `logs/1dropburn/attrib_build.log`, `rc=` line at end). Then, FROM THAT WORKTREE:
  `bash test/regression.sh --regression --deck=midrangeburn2hg` -> FAIL there too = upstream drift (report to the
  USER, leave unaccepted); PASS = our commits moved it (root-cause). Remove the worktree with
  `scripts/safe_worktree_remove.sh` after check_references_safe. Next: overnight tier once
  (`--overnight --deck=onedropburn,onedropburn2hg`, accept same filter), verify_deck, tie-break, viewer audit,
  discard A/B, report, push (none yet this session), CronDelete 01e1d807.
* **~05:45 UTC: ATTRIBUTED -- midrangeburn2hg_regression_d5_s2002 drift is UPSTREAM.** The pre-session tip
  17262a61f (worktree build, since removed via safe_worktree_remove) gives the SAME got=5.9467/bf75dda8b8606139
  (audit: 1 faster, 0 slower). Not ours; left UNACCEPTED for the deck's owner (surfaced to the USER).
  RUNNING: `bash test/regression.sh --overnight --deck=onedropburn,onedropburn2hg` -> logs/1dropburn/overnight.log
  (`rc=` line on completion). Then accept with the same filter + check_gt_logs, then verify_deck, tie-break,
  viewer audit, discard A/B, report, push (none yet this session), CronDelete 01e1d807.
* **~07:00 UTC: overnight tier ACCEPTED** (df9705a3b, 16 new keys, check_gt_logs consistent). **USER amended the
  cast order** (Needle Drop / Skewer last in either main; main 2 only when combat is the only damage; only
  Needle Drop's draw castable in main 2) -- implemented behind MTG_ONEDROPBURN_ORDER (UNCOMMITTED: GameState
  noncombat bit, DecisionProvider main-2 virtuals, NoteMain2StagerCast hand snapshot, OneDropBurnProvider rule,
  unit tests). RUNNING: ./build.sh -> logs/1dropburn/m2rule_build.log (`rc=`). Next: mtg-test, smoke+regression
  byte-identity (order OFF -> every key incl. onedropburn must PASS), then the paired A/B
  (`ARMSET=onedropburn CASES=overnight DECKS=onedropburn,onedropburn2hg bash test/mana_tap_order_ab.sh`),
  then verify_deck / tie-break / viewer audit / discard A/B / report / push.
* **~09:10 UTC CHECKPOINT (pre-compaction).** UNCOMMITTED src (built, mtg-test 3396/3396 PASS): the USER's
  amended order + main-2 rule (GameState `player_dealt_damage_this_turn` widened to uint8 bits, kDealtNoncombatDamage
  + `deck_m2_tracks_noncombat`; Combat.cpp / DamageEvents pass combat; both key folds; GoldFishRunner stamp in
  StampDeckTraits AND after m_provider attach; DecisionProvider `Main2StagerSnapshotsHand` /
  `Main2RuleReadsNoncombatDamage`; NoteMain2StagerCast hand snapshot; OneDropBurnProvider RestrictsMain2Casts /
  Main2CastAllowed; ONE CARD PER RANK by name: 10 Goblin Guide, 11 Swiftspear, 12 Vexing Devil, 13 Lavamancer,
  20 Boltwave, 21 Chain Lightning, 22 Lava Spike, 23 Lightning Bolt, 26 Rift Bolt, 29 Needle Drop, 30 Skewer,
  40 Fireblast; unit tests in test_needle_drop_vexing_devil.cpp).
  RUNNING: `bash test/regression.sh --smoke --regression` (all decks, order OFF byte-identity) ->
  logs/1dropburn/m2rule_tiers.log (`rc=` line). EXPECT: only the 39 known smoke drift keys (same got digests as
  logs/1dropburn/smoke3.log) + midrangeburn2hg_regression drift (5.9467/bf75dda8b8606139); everything else PASS.
  If so COMMIT the src + tests (do NOT accept anything; the drift is reconciled upstream in f71a7e0ce).
  **USER rulings 2026-11-18 (verbatim):**
  - *"For the cast order separation I think I meant that we need one item per row. We want every item to have a
    distinct position rather than relying on hand position."* -> done (ranks above).
  - *"That order looks fine to me, but the activation for Grim Lavamancer should be in there and before Neetle
    Drop and Skewer the Critics. It should go in main 1 only. Sunbaked Canyon sac to draw should also be in there
    and be main 1."* -> ORDER APPROVED: switch MTG_ONEDROPBURN_ORDER default ON after the amendments + A/B
    (root-cause if any searched game is slower).
  - Item 4 (upstream drift): *"Is this not fixed now? I had an agent correct this I believe."* -> yes, origin
    f71a7e0ce reconciled GT (midrangeburn2hg = 5.9467/bf75...); arrives at the rebase.
  - *"5. This is fine for now. 6. Yeah, the default mulligan is not great on this deck. It can easily work with 1
    land, especially if there are ways to draw like Rift Bolt -> Needle Drop or just heavy creature hitting power.
    The proper profile will fix this. 7. Let's addres this when we can."* -> planeswalker deferral OK; mulligan
    profile stage will fix 1-land keeps; Midrange Burn Fireblast-first fix is a TODO for this session after 1DB.
  **NEXT (activations, designed, not yet coded):**
  1. Lavamancer before Needle/Skewer: extend `TurnSolver::SpectacleGraveyardDamageFirst` (TurnSolver.cpp ~64793,
     both worlds already call it: ApplyPlanDirect ~77444, AIEngine ~8537) with a provider hook (default false;
     1DB = order ON): when the plan holds a Needle Drop / Skewer cast and a face Lavamancer action
     (GraveyardExileAbility gy_exile_mode==3, enchant_target==0), hoist it before the casts. Goldfish-equivalent
     to "after the burn, before Needle/Skewer" (a ping fuelled only by this plan's burn falls back to the trailing
     pass, and then that burn already enabled them). Order report: Lavamancer activation = rank 27.
  2. Main 1 only for Lavamancer: OneDropBurnProvider RestrictsCollectedActions (order ON) + CollectedActionAllowed
     -> false for a tap_graveyard_exile_damage GraveyardExileAbility when !is_pre_combat.
  3. Canyon sac-draw (DigDraw via the dig loop, runs at the END of a main's apply = after Fireblast, "last"):
     main 1 only -> override OneDropBurnProvider::ShouldConsiderDig: order ON && s.phase == PostCombatMain ->
     false, else DeckProvider/Generic. Surface: Canyon draw sits after Fireblast.
  4. Unit tests, build, scoped smoke+regression (--deck=onedropburn,onedropburn2hg; order OFF must PASS), then
     the paired A/B `ARMSET=onedropburn CASES=overnight DECKS=onedropburn,onedropburn2hg bash
     test/mana_tap_order_ab.sh` (ON vs OFF), then flip default ON, accept onedropburn GT (smoke+regression only).
  5. Then verify_deck, tie-break, viewer audit, discard A/B, Midrange Burn Fireblast-first fix, report, rebase onto
     origin (rebuild + smoke + check_gt_logs), push (none yet this session), gh run watch --interval 60,
     CronDelete 01e1d807.

### CHECKPOINT ~09:25 UTC 2026-11-18 (post-compaction)
- b93e3e803: amended order + main-2 rule committed (pending the order-OFF tier check, logs/1dropburn/m2rule_tiers.log).
- UNCOMMITTED, not yet built: the activation rules (HoistsFaceGraveyardDamage hook in
  TurnSolver::SpectacleGraveyardDamageFirst; OneDropBurnProvider CollectedActionAllowed = Lavamancer main 1 only;
  ShouldConsiderDig / DigOfferAt closed in PostCombatMain = Canyon main 1 only), all under MTG_ONEDROPBURN_ORDER;
  plus Midrange Burn's Fireblast rank 25 (MTG_MIDRANGE_FIREBLAST_ORDER, heurarm slot, default OFF until A/B);
  unit tests; `ARMSET=burnorders` in test/mana_tap_order_ab.sh.
- NEXT: build -> unit -> scoped smoke+regression (onedropburn*, midrangeburn*: must PASS, both levers OFF) ->
  `ARMSET=burnorders CASES=overnight DECKS=onedropburn,midrangeburn bash test/mana_tap_order_ab.sh` -> root-cause
  any slower game -> flip both defaults ON, accept smoke+regression for the four decks.
- ~10:25 UTC: the order-OFF tier run (~97% by cost) is still going; a background waiter (b9ssejbok) prints its FAILs + rc=.
  User asked how long until viewer/references and artifacts: answered ~5-7 h to push (then mullgen), viewer best
  after the order flip (~4 h) or now if the user wants (human play is never narrowed). USER has not answered yet.

### CHECKPOINT ~12:30 UTC 2026-11-18 -- order-OFF tier verdict + USER perf request
- USER (verbatim): "Okay, in that case I will generate the references tomorrow after the overnight. In the meantime I
  would like you to do everything you can about performance including testing bits of mulligan profile generation
  and value-leaf generation to see if there are any major inefficiencies."
- Order-OFF tier run (logs/1dropburn/m2rule_tiers.log, b93e3e803, makespan 3h18m): smoke 39 FAIL = exactly the
  known smoke3 drift (same got digests). Regression 56 FAIL: 49 match origin f71a7e0ce's reconciled GT exactly
  (upstream drift), midrangeburn2hg (known upstream), and 7 (mill x4, mill2hg, creature_giving d0, jumbo d0) that
  match neither -- the same decks drift in smoke on the pre-b93 binary; jumbo d0 being re-run on build/PGO/mtg
  (= df9705a3b src f0c9c2ee, pre-b93) via build/scratch/1db/attr7.json to prove it.
- PERF FINDINGS (single-thread diagnostics, logs/1dropburn/perf/):
  * ~89% of 1 Drop Burn's per-game search work is mulligan LOOKAHEAD BOTTOMING playouts (no keep table yet):
    20 std games d5 b20, MTG_DECISION_PROGRESS: bot 877,380 vs real 104,232 work. The slowest 2HG suite game
    (seed 2008 gi 6, 2.03M units, 355 s contended / 92 s solo) is 99.97% bottoming (9 full-game playouts).
    Real play ~5.2k units/game, below Midrange Burn (which has a keep table). => the keep table is the big lever.
  * MTG_BRANCH_STATS 10 games vs Midrange: 9x EnumeratePlans calls, 10x plans; Rift Bolt suspend/cast groups +
    Lavamancer dominate the raw odometer; raw enumerated ~= final, nothing obviously unreasonable. perf: flat
    profile (CollectActions 4.3% top), no hotspot.
- ~13:10 UTC: attribution PROVEN -- the 7 keys replayed on build/PGO/mtg (pre-b93 src) give the identical got digests
  (logs/1dropburn/perf/attr7_pre.out), so b93e3e803 is byte-identical with the order OFF on the whole suite.
  Activation rules + Midrange Fireblast lever built; unit suite 3399/3399 PASS (logs/1dropburn/unit_all2.log); committed.
