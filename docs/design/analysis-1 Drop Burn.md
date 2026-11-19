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
- flags: 0 unresolved
  - RESOLVED 2026-11-19 (cast orders default ON, 2bed196aa: seed 50008 now wins T3 at d5 b20, = Claude). Was: **50008 (AI T4, Claude T3): the order-OFF canonical order casts Fireblast BEFORE Boltwave** (generic tier 20
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
- ~14:50 UTC MULLGEN PROBES (no artifact written; scratch deck copy build/scratch/1db/deck/):
  * settings screen (mullgen_finalize.py, dry): PICK d2 b3 (fidelity 0.9921 >= 0.99, 2402 units/roll = 0.26x of
    play d5 b20 9258); d1 b1 1747 units at 0.9883 (just under the floor). K = 11 (buckets: Boltwave+Chain+Lava
    Spike+Bolt merged x16, rest singletons). logs/1dropburn/perf/mullgen_screen.log
  * recommend scout at d2 b3 (Release, MTG_PGO=0): 55,982 cells (size7 16,559 x pd + subs), floor 2435 s @ 22
    rollouts/s on 12 threads. PROJECTION fast ~13.5 h (PGO ~10.4 h), complete ~27.1 h -- both over the 8 h window.
    No generation rollout over 30 s. logs/1dropburn/perf/mullgen_recommend.log
  * discovery (d5 b20, 5,600 rollouts, ~11 min) has ONE degenerate probe: #163, every candidate 72-128 s
    contended (25 s solo, MTG_EQUIV_ONLY="163:Goblin Guide"): an unwinnable hand (win turn 9 = none) playing to
    the horizon at d5 b20; 606k EnumeratePlans calls, Rift Bolt / Lavamancer groups dominate. Its post-combat
    mains re-solve on a fresh fallback budget because main 1 found no verified win (`dprog` units=0 there is an
    accounting artefact of the separate fallback budget, not an unbudgeted search). ~23 core-min of ~70 total.
  * gen-like d2 b3 real play: ~436 units/decision, main 2 free (committed line) -- nothing anomalous.
- ~15:05 UTC VALUE-LEAF PHASE A PROBE (36 games seed 900000+, MTG_EVAL_ROWS_K=3, play settings, no keep table):
  17.7 core-s/game, 249k units/game; LABEL 90% / bottoming 8% / real play 1.5% of work; 3.8 rows/game -> 2,500
  games ~12 core-h (~1-2 h on 12 threads), ~9.6k rows (under the ~11k knee: short games). Not a bottleneck.
  logs/1dropburn/perf/rows36.{out,err}
- NEXT: scoped smoke+regression (4 burn decks, both levers OFF) on bb820b141, then ARMSET=burnorders A/B.
- ~14:50 UTC: scoped smoke+regression on bb820b141 (4 burn decks, levers OFF) PASS except the known upstream
  midrangeburn2hg drift (logs/1dropburn/scoped_bb82.log). LAUNCHED `ARMSET=burnorders CASES=overnight
  DECKS=onedropburn,midrangeburn bash test/mana_tap_order_ab.sh` -> logs/1dropburn/burnorders_ab.log (68 jobs,
  51,392 games, Release; DONE marker at the end). Then: root-cause slower games, flip both defaults ON, accept
  smoke+regression for the 4 decks.
- ~16:10 UTC burnorders A/B DONE (logs/1dropburn/burnorders_ab.log; 25,696 pairs, ON vs OFF): ALL -0.0514 t at
  0.83x units; per deck 1DB -0.0754 (883 better / 191 worse), 1DB 2HG -0.040, Midrange -0.0349, Midrange 2HG
  -0.013; no row worse. 23 searched games slower (logs/1dropburn/burnorders_slower.txt). ROOT CAUSE (temporary
  diagnostic MTG_1DB_DIAG_OFF sub-rule mask, uncommitted; slow games seed 4004 gi 259 / 6006 gi 149 both revert
  to T3 with ONLY the main-2 rule off):
  * gi 149 = ENGINE MODELLING GAP: the spectacle plan credit (BuildSpectacleCreditCtx) counted only direct_damage
    casts, so Vexing Devil never turned spectacle on for a same-plan Skewer -- {Devil, Skewer, Fireblast} in
    main 1 was priced at Skewer's {3}{R} and cut ("PROVEN NO-WIN", the engine then did NOTHING in main 1). OFF
    reached the kill only via main-2 Skewer, which the USER's rule bars after noncombat damage. Holds at d8 b0.
    FIX: MTG_SPECTACLE_CREDIT_ETB_OPP (default ON, heurarm slot) counts a Devil cast when
    VexingDevilOpponentTakesDamage takes the damage. Scenario test/scenarios/onedropburn_devil_enables_skewer_main1.json.
  * gi 259 = the USER's "only the card drawn by Needle Drop" clause: T2 OFF line Needle Drop (main 2, combat-only
    damage) draws a Mountain, plays it, casts the HELD Vexing Devil off it; the rule bars the held Devil (it
    "could" have been cast in main 1, but only instead of Needle Drop). The bottoming playouts then keep a
    different 6 and the game is T4. SURFACE TO USER (candidate amendment: also admit a held card that only the
    Needle-drawn land makes payable).
  NEXT: build -> scenario + unit -> `ARMSET=burnorders2` A/B (base / fix / on) -> flip.
- ~20:05 UTC burnorders2 A/B DONE (logs/1dropburn/burnorders2_ab.log): fix-alone vs base moves 0/25,696 games
  (the credit needs OpaqueCastOrderActive, i.e. the order); on vs base ALL -0.0524 t at 0.830x units; 1DB
  -0.0777 (891/177), 1DB 2HG -0.040, Midrange -0.0349 (403/28), Midrange 2HG -0.0132. Slower searched games 20
  (logs/1dropburn/burnorders2_slower.txt); traced: 4004/186 Lavamancer ping (noncombat) forces Needle Drop into
  main 1; 7007/12 Devil rank 12 (opp takes 4) vs OFF's main-2 Devil at opp 5 (declined -> a 4/3 attacker);
  7007/64 main-2 rule + Fireblast-last: OFF's T3 combat -> Skewer x2 + Fireblast in main 2 has no main-1 twin
  (Skewer at 30 precedes its only enabler Fireblast at 40); Midrange 5005/260 different bottom; 6006/120 a
  4-mulligan no-win hand. All USER-rule trade-offs, surfaced. FLIPPED both defaults ON (DecisionProviders.cpp);
  unit 3399/3399. Scoped smoke+regression running -> logs/1dropburn/flip_tiers.log, then accept the 4 decks.

### CHECKPOINT ~20:45 UTC 2026-11-18 (pre-compaction)
- COMMITTED (local, unpushed): bb820b141 activation rules + Midrange lever; b543ae64d MTG_SPECTACLE_CREDIT_ETB_OPP
  (Vexing Devil enables spectacle in the plan credit, default ON, heurarm slot, scenario, burnorders2 armset).
- UNCOMMITTED: (a) DecisionProviders.cpp FLIP -- MTG_ONEDROPBURN_ORDER and MTG_MIDRANGE_FIREBLAST_ORDER default ON
  (built + unit 3399/3399 before the hoist edit); HeuristicArm.h slot comments neutral ("default lives at the
  reader"). (b) MTG_SPECTACLE_CREDIT_HOIST (EngineFlags.h reader + heurarm slot + TurnSolver BuildSpectacleCreditCtx:
  a hoisted face Lavamancer ping precedes every spectacle cast) -- found by the flip's regression gi99 (seed 3102:
  T4 ping + Skewer {R} in main 1 was cut, engine did nothing). Scenario
  test/scenarios/onedropburn_lavamancer_ping_enables_skewer_main1.json; `ARMSET=burnhoist` (base hoist OFF / fix ON)
  in test/mana_tap_order_ab.sh. FULL REBUILD RUNNING -> logs/1dropburn/build_hoist.log ("rc=" at the end).
- Flip tiers (logs/1dropburn/flip_tiers.log, BEFORE the hoist fix): every moved key faster/equal; refs pass;
  1 searched slower = gi99 above. NOT accepted yet.
- NEXT: build rc=0 -> both new scenarios PASS (+ with MTG_SPECTACLE_CREDIT_HOIST=0 the hoist one FAILS) -> unit ->
  `ARMSET=burnhoist CASES=overnight DECKS=onedropburn bash test/mana_tap_order_ab.sh > logs/1dropburn/burnhoist_ab.log`
  -> commit flip + hoist fix -> scoped `bash test/regression.sh --smoke --regression
  --deck=onedropburn,onedropburn2hg,midrangeburn,midrangeburn2hg` -> review slower -> `--accept` with the same
  --deck filter (smoke + regression) -> check_gt_logs -> commit GT -> Stage 5 (verify_deck, leaf_tiebreak_check,
  viewer audit, discard A/B) -> Stage 6 report -> push (check_references_safe, rebase origin, rebuild, smoke,
  check_gt_logs, push, gh run watch --interval 60) -> CronDelete 01e1d807.
- OPEN USER QUESTIONS: (1) Needle Drop clause -- admit a HELD card that only the Needle-drawn land makes payable?
  (2) mulligan gen recipe/where: fast ~10.4 h PGO > 8 h window (weekend / second machine?). (3) rule trade-offs
  surfaced: Lavamancer ping forces Needle Drop into main 1; Devil rank 12 vs a main-2 Devil the opponent declines;
  Fireblast-last vs Fireblast as Skewer's only spectacle enabler. (4) viewer: launch after the flip lands.
- PERF FINDINGS for the user: see the ~12:30 / ~14:50 / ~15:05 entries above (bottoming 89% of play work pre
  keep table; mullgen d2 b3, K=11, fast ~13.5 h Release; discovery probe 163; value-leaf phase A ~1-2 h).

### CHECKPOINT ~22:35 UTC 2026-11-18 -- burnhoist A/B landed; flip + hoist credit committed
- A/B (logs/mana_tap_ab/20261118_220729_overnight; base = flip without MTG_SPECTACLE_CREDIT_HOIST, fix = with it;
  onedropburn overnight cells, one pooled batch, 12/12 workers): ALL GAMES paired delta **-0.0042 +/- 0.0007**
  (95% CI [-0.0056,-0.0028]), 47 better / 4 worse of 11010. Every searched cell (d3/d5/2HG, 3610 games) is
  byte-identical; all movement is in the d0 (playout-policy) cells.
- The 4 slower d0 games (8008 gi1019, 8008 gi1272, 4004 gi1104, 10010 gi475; T4 -> T5, the playout now spends Skewer
  in main 1 off the hoisted-ping credit) all recover to T4 at `--depth 8 --budget-ms 0`. Bar met.
- NEXT: scoped smoke+regression on onedropburn,onedropburn2hg,midrangeburn,midrangeburn2hg -> review -> accept ->
  check_gt_logs -> commit GT; then Stage 5 (verify_deck, leaf_tiebreak_check, viewer audit, shed-worst A/B), Stage 6, push.

<!-- verify_deck:begin (generated -- do not edit inside) -->
### CHECKPOINT ~01:55 UTC 2026-11-19 -- Stage 5
- verify_deck: all gates PASS except `viewer` (auditor crashed: UnboundLocalError `unreached` on a run with no hard
  miss -- a shared-script bug, already fixed on origin; re-run after the rebase). Fixed this round: Vexing Devil's
  `etb_opp_may_take_damage_to_sac` classified INERT (opponent's choice, USER-ruled model); sweep flag 50008 resolved
  (wins T3 with the order ON).
- leaf_tiebreak_check: 0 changed of 12000 paired (unbindable at this sample; re-run at --blocks 48 queued later).
- 5i discard BOUND (ARMSET=shedworst, onedropburn overnight cells, one pooled batch): cleanup worst-vs-base +0.0002
  +/- 0.0002 (1 game moved of 11010), cost/trigger worst-vs-base +0.0000 (0 moved). No ranking can pay: axis CLOSED.
- NEXT: rebase onto origin, rebuild, smoke + check_gt_logs, push, re-run the viewer audit, Stage 6 report.

### CHECKPOINT ~02:25 UTC 2026-11-19 -- rebased onto origin (377 upstream commits), building
- Rebased the 14 local commits onto origin f1342be37 + fb70e908f (now HEAD a635f60e8). Conflicts resolved:
  cards.json / scryfall_reference.json (both sides appended: origin's entries + Needle Drop / Vexing Devil);
  HeuristicArm.h (both sides' slots; the duplicated ONEDROPBURN/MIDRANGE enum lines removed in a635f60e8 -- the
  intermediate commits d4ae8e063..9b8d25e8f do not compile on their own); GoldFishRunner.cpp (both statements kept);
  regression_gt.txt / job_cost.tsv per key (origin's values; keys only we added or only we changed kept);
  suite_cost.json origin's Hinata row; burn .wins ours (re-accepted below).
- RUNNING: ./build.sh (logs/1dropburn/build_rebase.log, DONE marker). NEXT: full smoke (byte-identity after the
  rebase) -> re-accept the four burn decks' smoke+regression -> check_gt_logs -> push -> CI watch (background) ->
  viewer audit -> Stage 6 report -> leaf_tiebreak --blocks 48.

## Last verification (2026-11-19)

`verify_deck.py decks/1 Drop Burn/1 Drop Burn.cod --write-ledger` -> **FAIL**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 14 cards full (missing=0, partial=0) |
| card_costs | PASS | yes | all mana costs match Scryfall (cost/cmc only) |
| card_fields | PASS | yes | 823 cards match snapshot (cost/PT/types/keywords); 18 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| regression_tiers | PASS | yes | in all 3 suite tiers with GT (onedropburn) |
| viewer | FAIL | yes | auditor rc=1:   No oracle-text choice phrase is left unmodeled by params. Clean. | Driving 40 games from seed 9001 to observe surfaced decisions... | Observed decision types: ['discard', 'free_cast', 'gy_return_one', 'sacrifice', 'target'] |
| viewer_wiring | PASS | yes | 4 type(s) wired (emitter + GUI): free_cast, gy_return_one, sacrifice, target |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 8001] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/140 decisions: determinism+integrity+progress hold |
| claude_sweep | PASS | yes | Claude-play sweep recorded, 0 unresolved flags |
| suite | PASS | yes | suite gate: 1 Drop Burn is regression key `onedropburn` |

### Pending user sign-off (block the gate until fixed OR approved below)
Add a key to `## Approved deferrals` to sign one off (only if it is a genuine, understood deferral -- not a bug):
- `viewer:auditor` -- audit_viewer_decisions rc=1:   No oracle-text choice phrase is left unmodeled by params. Clean. | Driving 40 games from seed 9001 to observe surfaced decisions... | Observed decision types: ['discard', 'free_cast', 'gy_return_one', 'sacrifice', 'target']

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Goblin Guide: The mandatory reveal and land-to-hand trigger is implemented against the current passive opponent's library. The planned actual-deck opponent model is a separate architecture project.
- coverage deferral -- Vexing Devil: USER-ruled passive-opponent model of the opponent's choice (2026-11-17; one place: VexingDevilOpponentTakesDamage in SpellEffects.h): the opponent takes the 4 damage unless its life (the shared team life in 2HG) is <= 4 while our hand is empty, or <= 7 while we hold any card (it guesses whether we can kill this turn from our hand SIZE, never its contents); then the Devil stays as a 4/3. The choice is the OPPONENT's, so it is neither a search branch nor a viewer decision; executor and rollout apply the same rule through the shared enter cascade. Taken: 4 damage from the Devil to the opponent (noncombat damage from a red creature source -- marks the opponent dealt damage / lost life this turn) and the Devil is sacrificed to our graveyard.
- allowlisted divergence -- Wanderwine Prophets [keywords]: Champion is modeled by champion_merfolk with linked exile, return, sacrifice if no champion, and searched human choice. The engine has no generic cham
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled 
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is 
- allowlisted divergence -- Auriok Champion [keywords]: 'Protection from black and from red' is a real keyword but inert in goldfishing on all four DEBT axes (the passive opponent never damages, targets, bl
- allowlisted divergence -- Brightcap Badger [mana_cost]: ADVENTURE CARD (CR 715), authored as TWO entries -- 'Brightcap Badger' (the {3}{G} 3/4 creature face) and 'Fungus Frolic' (the {2}{G} instant half). S
- allowlisted divergence -- Fungus Frolic [mana_cost]: The ADVENTURE HALF of Brightcap Badger // Fungus Frolic -- see the Brightcap Badger entry. Scryfall's combined mana_cost is '{3}{G} // {2}{G}'; the in
- allowlisted divergence -- Fungus Frolic [P/T]: An Instant has no power or toughness. Scryfall reports 3/4 because it describes the COMBINED card, whose creature face is the 3/4 Brightcap Badger. Gi
- allowlisted divergence -- Mycoloth [keywords]: 'Devour 2' is a real keyword and is FULLY MODELLED -- but via the `devour` CardParam, not via Keyword::Devour. The engine's Keyword enum deliberately 
- allowlisted divergence -- Brutal Cathar [keywords]: Transforming DFC (Brutal Cathar // Moonrage Brute): Scryfall reports CARD-LEVEL keywords for both faces. The front face's only keyword is Daybound, mo
- allowlisted divergence -- Moonrage Brute [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the front's Daybound and the layout's Transform; Nightbound is modelled structurall
- allowlisted divergence -- Bloodline Keeper [keywords]: Transforming DFC (Bloodline Keeper // Lord of Lineage): Scryfall reports CARD-LEVEL keywords for both faces (Flying, Transform). Transform is the layo
- allowlisted divergence -- Lord of Lineage [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the layout's Transform; the back face carries no transform of its own (transform_fr
- allowlisted divergence -- Emrakul, the Aeons Torn [keywords]: 'Protection from spells that are one or more colors' is a real keyword but INERT here (PROVISIONAL, ledger Q12): nothing in a goldfish game targets, d
- oracle_text advisory -- Deeproot Pilgrimage: oracle_text diverges (similarity 0.55); scryfall='Whenever one or more nontoken Merfolk you control become tapped, create a 1/1 blue Merfolk creature token with
- oracle_text advisory -- Svyelun of Sea and Sky: oracle_text diverges (similarity 0.73); scryfall='Svyelun has indestructible as long as you control at least two other Merfolk.
Whenever Svyelun attacks, draw 
- oracle_text advisory -- Wanderwine Prophets: oracle_text diverges (similarity 0.11); scryfall='Champion a Merfolk (When this enters, sacrifice it unless you exile another Merfolk you control. When this lea
- oracle_text advisory -- Decimator of the Provinces: oracle_text diverges (similarity 0.22); scryfall="Emerge {6}{G}{G}{G} (You may cast this spell by sacrificing a creature and paying the emerge cost reduced by t
- oracle_text advisory -- End-Raze Forerunners: oracle_text diverges (similarity 0.15); scryfall='Vigilance, trample, haste
When this creature enters, other creatures you control get +2/+2 and gain vigilance
- oracle_text advisory -- Flinthoof Boar: oracle_text diverges (similarity 0.17); scryfall='This creature gets +1/+1 as long as you control a Mountain.
{R}: This creature gains haste until end of turn.
- oracle_text advisory -- Herald of Ilharg: oracle_text diverges (similarity 0.24); scryfall='Trample
Whenever you cast a creature spell, put two +1/+1 counters on this creature. If that spell has mana v
- oracle_text advisory -- Herald's Horn: oracle_text diverges (similarity 0.26); scryfall="As this artifact enters, choose a creature type.
Creature spells you cast of the chosen type cost {1} less to
- oracle_text advisory -- Ilharg, the Raze-Boar: oracle_text diverges (similarity 0.21); scryfall="Trample
Whenever Ilharg attacks, you may put a creature card from your hand onto the battlefield tapped and a
- oracle_text advisory -- Raggadragga, Goreguts Boss: oracle_text diverges (similarity 0.23); scryfall='Each creature you control with a mana ability gets +2/+2.
Whenever a creature you control with a mana ability
- oracle_text advisory -- Gideon, Ally of Zendikar: oracle_text diverges (similarity 0.10); scryfall='+1: Until end of turn, Gideon becomes a 5/5 Human Soldier Ally creature with indestructible that\'s still a pl
- oracle_text advisory -- Basri, Tomorrow's Champion: oracle_text diverges (similarity 0.15); scryfall="{W}, {T}, Exert Basri: Create a 1/1 white Cat creature token with lifelink. (An exerted creature won't untap d
- oracle_text advisory -- Goblin Guide: oracle_text diverges (similarity 0.60); scryfall="Haste
Whenever this creature attacks, defending player reveals the top card of their library. If it's a land 
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.31); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.40); scryfall="As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.15); scryfall='As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.21); scryfall='As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.
{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.35); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.
{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.
{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.
You may choose not to untap this land during your untap step.
At the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.
{T}: Put a storage counter on this land.
{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)
This land enters tapped.
When this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.
{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.
Sacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.45); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may 
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.27); scryfall='First strike (This creature deals combat damage before creatures without first strike.)
Other Knight creature
- oracle_text advisory -- Valiant Knight: oracle_text diverges (similarity 0.18); scryfall='Other Knights you control get +1/+1.
{3}{W}{W}: Knights you control gain double strike until end of turn.'
- oracle_text advisory -- Kinsbaile Cavalier: oracle_text diverges (similarity 0.05); scryfall='Knight creatures you control have double strike.'
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.
{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins
Other Knights you control get +2/+2 and have protection from Assassins.
When Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance
Adeline's power is equal to the number of creatures you control.
Whenever you attack, for each opp
- oracle_text advisory -- Accorder Paladin: oracle_text diverges (similarity 0.33); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)'
- oracle_text advisory -- Silverblade Paladin: oracle_text diverges (similarity 0.25); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Niv-Mizzet, the Firemind: oracle_text diverges (similarity 0.16); scryfall='Flying
Whenever you draw a card, Niv-Mizzet deals 1 damage to any target.
{T}: Draw a card.'
- oracle_text advisory -- Niv-Mizzet, Parun: oracle_text diverges (similarity 0.23); scryfall="This spell can't be countered.
Flying
Whenever you draw a card, Niv-Mizzet deals 1 damage to any target.
Wh
- oracle_text advisory -- Curiosity: oracle_text diverges (similarity 0.16); scryfall='Enchant creature
Whenever enchanted creature deals damage to an opponent, you may draw a card.'
- oracle_text advisory -- Tandem Lookout: oracle_text diverges (similarity 0.38); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Hero of Bladehold: oracle_text diverges (similarity 0.50); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)
When
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.30); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.45); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.
{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)
{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying
Rot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.
Whenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.17); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.
Target
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.

- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.30); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.32); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.
Whenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.05); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.14); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.
Draw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.12); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.
Ferocious — If you control a creature with power 4 or greater, those creatures don't 
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample
Spells you cast cost {1} less to cast for each target.
Spells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {U}{
- oracle_text advisory -- Orzhov Basilica: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {W}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.28); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of 
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —
• Tap X target permanents.
• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying
{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.10); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.05); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.
Splice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste
Other creatures you control have haste.
Whenever an opponent casts a creature or planeswalker 
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste
When Karrthus enters, gain control of all Dragons, then untap all Dragons.
Other Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.23); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying
Whenever this creature or another Dragon you control enters, it deals X damage to any target, where X 
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying
Whenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying
Whenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.
Storm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.
If this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.
Whenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.33); scryfall='Enchant creature
Enchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature
Enchanted creature gets +2/+0 and has trample.
When this Aura is put into a graveyard from 
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.35); scryfall='Enchant creature with another Aura attached to it
Enchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature
Enchanted creature gets +2/+2 and has trample.
Whenever enchanted creature deals damage, yo
- oracle_text advisory -- Hyena Umbra: oracle_text diverges (similarity 0.52); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has first strike.
Umbra armor (If enchanted creature woul
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)
Umbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature
Enchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature
Enchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature
Enchanted creature gets +1/+0 and has flying.
{3}{W}: Return this card from your graveyard 
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature
Enchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature
Enchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)
Whenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.77); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)
Enchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Darkbore Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {B}.'
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)
Other Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.
Goblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.
{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.
{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.
Whenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.46); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.24); scryfall='First strike
When this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach
When this creature enters, it deals 2 damage to any target.
Channel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.
When this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.
Whenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.
Whenever a creature 
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.15); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.24); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.19); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.
{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment, 
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.
Search your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.29); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.26); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.16); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.33); scryfall='Flying, first strike, trample, lifelink, haste
At the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste
Whenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything
If Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance
This creature gets +1/+1 for each color among permanents you control.
{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)
{B}, 
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.18); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)
Equip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying
Whenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.
−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.
−2: Gain control of target creature.
−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")
+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.39); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.08); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying
{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.35); scryfall="Choose five. You may choose the same mode more than once.
• Target permanent phases out.
• Target player dra
- oracle_text advisory -- Disenchant: oracle_text diverges (similarity 0.21); scryfall='Destroy target artifact or enchantment.'
- oracle_text advisory -- Mirrorwing Dragon: oracle_text diverges (similarity 0.42); scryfall='Flying
Whenever a player casts an instant or sorcery spell that targets only this creature, that player copie
- oracle_text advisory -- Zada, Hedron Grinder: oracle_text diverges (similarity 0.57); scryfall='Whenever you cast an instant or sorcery spell that targets only Zada, copy that spell for each other creature 
- oracle_text advisory -- Goblin Instigator: oracle_text diverges (similarity 0.32); scryfall='When this creature enters, create a 1/1 red Goblin creature token.'
- oracle_text advisory -- Fists of Flame: oracle_text diverges (similarity 0.36); scryfall="Draw a card. Until end of turn, target creature gains trample and gets +1/+0 for each card you've drawn this t
- oracle_text advisory -- Luxurious Libation: oracle_text diverges (similarity 0.24); scryfall='Target creature gets +X/+X until end of turn. Create a 1/1 green and white Citizen creature token.'
- oracle_text advisory -- Fortifying Draught: oracle_text diverges (similarity 0.33); scryfall='You gain 2 life. Target creature gets +X/+X until end of turn, where X is the amount of life you gained this t
- oracle_text advisory -- Gold Rush: oracle_text diverges (similarity 0.36); scryfall='Create a Treasure token. Until end of turn, up to one target creature gets +2/+2 for each Treasure you control
- oracle_text advisory -- Ancestral Anger: oracle_text diverges (similarity 0.52); scryfall='Target creature gains trample and gets +X/+0 until end of turn, where X is 1 plus the number of cards named An
- oracle_text advisory -- Oracle's Restoration: oracle_text diverges (similarity 0.10); scryfall='Target creature you control gets +1/+1 until end of turn. You draw a card and gain 1 life.'
- oracle_text advisory -- Expedite: oracle_text diverges (similarity 0.29); scryfall='Target creature gains haste until end of turn.
Draw a card.'
- oracle_text advisory -- Impolite Entrance: oracle_text diverges (similarity 0.18); scryfall='Target creature gains trample and haste until end of turn.
Draw a card.'
- oracle_text advisory -- Scale the Heights: oracle_text diverges (similarity 0.49); scryfall='Put a +1/+1 counter on up to one target creature. You gain 2 life. You may play an additional land this turn.\
- oracle_text advisory -- Twinflame: oracle_text diverges (similarity 0.42); scryfall="Strive — This spell costs {2}{R} more to cast for each target beyond the first.
Choose any number of target c
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.39); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.
When this land enters, you gain 1 life.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.
Equip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.
Equip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.
{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.
Whenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.
Equipped creature gets +3/+0 and h
- oracle_text advisory -- Ancient Den: oracle_text diverges (similarity 0.02); scryfall='{T}: Add {W}.'
- oracle_text advisory -- Bone Saw: oracle_text diverges (similarity 0.29); scryfall='Equipped creature gets +1/+0.
Equip {1} ({1}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Paradise Mantle: oracle_text diverges (similarity 0.10); scryfall='Equipped creature has "{T}: Add one mana of any color."
Equip {1}'
- oracle_text advisory -- Cathar's Shield: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +0/+3 and has vigilance.
Equip {3} ({3}: Attach to target creature you control. Equip 
- oracle_text advisory -- Accorder's Shield: oracle_text diverges (similarity 0.23); scryfall="Equipped creature gets +0/+3 and has vigilance. (Attacking doesn't cause it to tap.)
Equip {3} ({3}: Attach t
- oracle_text advisory -- Kite Shield: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +0/+3.
Equip {3} ({3}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Spidersilk Net: oracle_text diverges (similarity 0.22); scryfall='Equipped creature gets +0/+2 and has reach. (It can block creatures with flying.)
Equip {2} ({2}: Attach to t
- oracle_text advisory -- Golem-Skin Gauntlets: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+0 for each Equipment attached to it.
Equip {2} ({2}: Attach to target creature you
- oracle_text advisory -- Skateboard: oracle_text diverges (similarity 0.19); scryfall='When this Equipment enters, tap target permanent.
Equipped creature gets +1/+0 and has haste.
Equip {1} ({1}
- oracle_text advisory -- Dragonfire Blade: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +2/+2 and has hexproof from monocolored.
Equip {4}. This ability costs {1} less to act
- oracle_text advisory -- Deconstruction Hammer: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+1 and has "{3}, {T}, Sacrifice Deconstruction Hammer: Destroy target artifact or en
- oracle_text advisory -- Sram, Senior Edificer: oracle_text diverges (similarity 0.08); scryfall='Whenever you cast an Aura, Equipment, or Vehicle spell, draw a card.'
- oracle_text advisory -- Cid, Freeflier Pilot: oracle_text diverges (similarity 0.13); scryfall='Equipment and Vehicle spells you cast cost {1} less to cast.
Jump — During your turn, Cid has flying.
{2}, {
- oracle_text advisory -- Sigarda's Aid: oracle_text diverges (similarity 0.14); scryfall='You may cast Aura and Equipment spells as though they had flash.
Whenever an Equipment you control enters, yo
- oracle_text advisory -- Dwalin, Weaponmaster: oracle_text diverges (similarity 0.12); scryfall='First strike
Whenever Dwalin enters or attacks, put a hone counter on each Equipment you control. (Each hone 
- oracle_text advisory -- Ardenn, Intrepid Archaeologist: oracle_text diverges (similarity 0.17); scryfall='At the beginning of combat on your turn, you may attach any number of Auras and Equipment you control to targe
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.
Remove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.43); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.
Metalcraft — Equipment you control have equip 
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.30); scryfall='First strike
Balan has double strike as long as two or more Equipment are attached to it.
{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying
Whenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.
{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.03); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.41); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
- oracle_text advisory -- Mirri's Guile: oracle_text diverges (similarity 0.44); scryfall='At the beginning of your upkeep, you may look at the top three cards of your library, then put them back in an
- oracle_text advisory -- Call of the Wild: oracle_text diverges (similarity 0.52); scryfall="{2}{G}{G}: Reveal the top card of your library. If it's a creature card, put it onto the battlefield. Otherwis
- oracle_text advisory -- Hornet Queen: oracle_text diverges (similarity 0.41); scryfall='Flying, deathtouch
When this creature enters, create four 1/1 green Insect creature tokens with flying and de
- oracle_text advisory -- Terastodon: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may destroy up to three target noncreature permanents. For each permanent put i
- oracle_text advisory -- Elderscale Wurm: oracle_text diverges (similarity 0.53); scryfall='Trample
When this creature enters, if your life total is less than 7, your life total becomes 7.
As long as 
- oracle_text advisory -- Craterhoof Behemoth: oracle_text diverges (similarity 0.44); scryfall='Haste
When this creature enters, creatures you control gain trample and get +X/+X until end of turn, where X 
- oracle_text advisory -- Worldspine Wurm: oracle_text diverges (similarity 0.39); scryfall="Trample
When this creature dies, create three 5/5 green Wurm creature tokens with trample.
When Worldspine W
- oracle_text advisory -- Vaultborn Tyrant: oracle_text diverges (similarity 0.47); scryfall="Trample
Whenever this creature or another creature you control with power 4 or greater enters, you gain 3 lif
- oracle_text advisory -- Natural Order: oracle_text diverges (similarity 0.38); scryfall='As an additional cost to cast this spell, sacrifice a green creature.
Search your library for a green creatur
- oracle_text advisory -- Turntimber Symbiosis: oracle_text diverges (similarity 0.45); scryfall='Look at the top seven cards of your library. You may put a creature card from among them onto the battlefield.
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.
{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.12); scryfall='First strike
{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.
{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.
Whenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.25); scryfall='First strike
Other Minotaurs you control have first strike.
As long as you have one or fewer cards in hand, 
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample
Other Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.
{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.21); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.
{2}, Sacrifice another creatu
- oracle_text advisory -- Atsushi, the Blazing Sky: oracle_text diverges (similarity 0.43); scryfall='Flying, trample
When Atsushi dies, choose one —
• Exile the top two cards of your library. Until the end of 
- oracle_text advisory -- Inferno of the Star Mounts: oracle_text diverges (similarity 0.35); scryfall="This spell can't be countered.
Flying, haste
{R}: Inferno of the Star Mounts gets +1/+0 until end of turn. W
- oracle_text advisory -- Dragon Tempest: oracle_text diverges (similarity 0.34); scryfall='Whenever a creature you control with flying enters, it gains haste until end of turn.
Whenever a Dragon you c
- oracle_text advisory -- Urza's Incubator: oracle_text diverges (similarity 0.15); scryfall='As this artifact enters, choose a creature type.
Creature spells of the chosen type cost {2} less to cast.'
- oracle_text advisory -- Mind Stone: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.
{1}, {T}, Sacrifice this artifact: Draw a card.'
- oracle_text advisory -- Fire Diamond: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.
{T}: Add {R}.'
- oracle_text advisory -- Dragonspeaker Shaman: oracle_text diverges (similarity 0.15); scryfall='Dragon spells you cast cost {2} less to cast.'
- oracle_text advisory -- Glorybringer: oracle_text diverges (similarity 0.46); scryfall="Flying, haste
You may exert this creature as it attacks. When you do, it deals 4 damage to target non-Dragon 
- oracle_text advisory -- Haven of the Spirit Dragon: oracle_text diverges (similarity 0.32); scryfall='{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana only to cast a Dragon creature spell.
{2}, {T}
- oracle_text advisory -- Nest Invader: oracle_text diverges (similarity 0.27); scryfall='When this creature enters, create a 0/1 colorless Eldrazi Spawn creature token. It has "Sacrifice this token: 
- oracle_text advisory -- Young Pyromancer: oracle_text diverges (similarity 0.28); scryfall='Whenever you cast an instant or sorcery spell, create a 1/1 red Elemental creature token.'
- oracle_text advisory -- Undercellar Myconid: oracle_text diverges (similarity 0.39); scryfall='Whenever this creature enters or dies, create a 1/1 green Saproling creature token.
{T}: Add one mana of any 
- oracle_text advisory -- Frontline Heroism: oracle_text diverges (similarity 0.39); scryfall='When this enchantment enters, create a 1/1 red Soldier creature token with haste.
Whenever you cast a spell t
- oracle_text advisory -- Adarkar Wastes: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.
{T}: Add {W} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Caves of Koilos: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {W} or {B}. This land deals 1 damage to you.'
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Llanowar Wastes: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {B} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.
{T}: Add {G} or {W}.
{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
- oracle_text advisory -- Mariposa Military Base: oracle_text diverges (similarity 0.26); scryfall='You may have this land enter tapped. If you do, you get two rad counters.
{T}: Add {C}.
{5}, {T}: Draw a car
- oracle_text advisory -- Eldrazi Displacer: oracle_text diverges (similarity 0.47); scryfall="Devoid (This card has no color.)
{2}{C}: Exile another target creature, then return it to the battlefield tap
- oracle_text advisory -- Emiel the Blessed: oracle_text diverges (similarity 0.48); scryfall="{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.

- oracle_text advisory -- Cloud of Faeries: oracle_text diverges (similarity 0.25); scryfall='Flying
When this creature enters, untap up to two lands.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Peregrine Drake: oracle_text diverges (similarity 0.22); scryfall='Flying
When this creature enters, untap up to five lands.'
- oracle_text advisory -- Wild Growth: oracle_text diverges (similarity 0.50); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Cryptolith Rite: oracle_text diverges (similarity 0.05); scryfall='Creatures you control have "{T}: Add one mana of any color."'
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land
Enchanted land has shroud. (It can't be the target of spells or abilities.)
Whenever enchanted 
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.04); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.05); scryfall="This spell can't be countered.
{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)
{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)
Flash
Flying
{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.08); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).
{T}: Add {C}.
{T}, Pay {E}: Add one mana of any colo
- oracle_text advisory -- Maelstrom Wanderer: oracle_text diverges (similarity 0.34); scryfall='Creatures you control have haste.
Cascade, cascade (When you cast this spell, exile cards from the top of you
- oracle_text advisory -- Annoyed Altisaur: oracle_text diverges (similarity 0.50); scryfall='Reach, trample
Cascade (When you cast this spell, exile cards from the top of your library until you exile a 
- oracle_text advisory -- Sakashima's Protege: oracle_text diverges (similarity 0.28); scryfall='Flash
Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Boarding Party: oracle_text diverges (similarity 0.62); scryfall='Haste
Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Breaching Dragonstorm: oracle_text diverges (similarity 0.26); scryfall="When this enchantment enters, exile cards from the top of your library until you exile a nonland card. You may
- oracle_text advisory -- Call Forth the Tempest: oracle_text diverges (similarity 0.40); scryfall="Cascade, cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland
- oracle_text advisory -- Creative Technique: oracle_text diverges (similarity 0.33); scryfall='Demonstrate (When you cast this spell, you may copy it. If you do, choose an opponent to also copy it.)
Shuff
- oracle_text advisory -- Dwarven Ruins: oracle_text diverges (similarity 0.09); scryfall='This land enters tapped.
{T}: Add {R}.
{T}, Sacrifice this land: Add {R}{R}.'
- oracle_text advisory -- Svyelunite Temple: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {U}.
{T}, Sacrifice this land: Add {U}{U}.'
- oracle_text advisory -- Hickory Woodlot: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped with two depletion counters on it.
{T}, Remove a depletion counter from this land: Ad
- oracle_text advisory -- Melira, Sylvok Outcast: oracle_text diverges (similarity 0.34); scryfall="You can't get poison counters.
Creatures you control can't have -1/-1 counters put on them.
Creatures your o
- oracle_text advisory -- Vizier of Remedies: oracle_text diverges (similarity 0.55); scryfall='If one or more -1/-1 counters would be put on a creature you control, that many -1/-1 counters minus one are p
- oracle_text advisory -- Kitchen Finks: oracle_text diverges (similarity 0.44); scryfall="When this creature enters, you gain 2 life.
Persist (When this creature dies, if it had no -1/-1 counters on 
- oracle_text advisory -- Murderous Redcap: oracle_text diverges (similarity 0.51); scryfall="When this creature enters, it deals damage equal to its power to any target.
Persist (When this creature dies
- oracle_text advisory -- Carrion Feeder: oracle_text diverges (similarity 0.28); scryfall="This creature can't block.
Sacrifice a creature: Put a +1/+1 counter on this creature."
- oracle_text advisory -- Bloodthrone Vampire: oracle_text diverges (similarity 0.26); scryfall='Sacrifice a creature: This creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Recruiter of the Guard: oracle_text diverges (similarity 0.35); scryfall='When this creature enters, you may search your library for a creature card with toughness 2 or less, reveal it
- oracle_text advisory -- Ranger of Eos: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, you may search your library for up to two creature cards with mana value 1 or less,
- oracle_text advisory -- Severance Priest: oracle_text diverges (similarity 0.40); scryfall="Deathtouch
When this creature enters, target opponent reveals their hand. You may choose a nonland card from 
- oracle_text advisory -- Birthing Pod: oracle_text diverges (similarity 0.25); scryfall="({G/P} can be paid with either {G} or 2 life.)
{1}{G/P}, {T}, Sacrifice a creature: Search your library for a
- oracle_text advisory -- Chord of Calling: oracle_text diverges (similarity 0.25); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Reveillark: oracle_text diverges (similarity 0.22); scryfall="Flying
When this creature leaves the battlefield, return up to two target creature cards with power 2 or less
- oracle_text advisory -- Felidar Guardian: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may exile another target permanent you control, then return that card to the ba
- oracle_text advisory -- Voice of Resurgence: oracle_text diverges (similarity 0.32); scryfall='Whenever an opponent casts a spell during your turn and when this creature dies, create a green and white Elem
- oracle_text advisory -- Scavenging Ooze: oracle_text diverges (similarity 0.23); scryfall='{G}: Exile target card from a graveyard. If it was a creature card, put a +1/+1 counter on this creature and y
- oracle_text advisory -- Ravenous Chupacabra: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, destroy target creature an opponent controls.'
- oracle_text advisory -- Reclamation Sage: oracle_text diverges (similarity 0.16); scryfall='When this creature enters, you may destroy target artifact or enchantment.'
- oracle_text advisory -- Celes, Rune Knight: oracle_text diverges (similarity 0.25); scryfall='When Celes enters, discard any number of cards, then draw that many cards plus one.
Whenever one or more othe
- oracle_text advisory -- Drifting Meadow: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {W}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Polluted Mire: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {B}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Smoldering Crater: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {R}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Blasted Landscape: oracle_text diverges (similarity 0.30); scryfall='{T}: Add {C}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fetid Pools: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {U} or {B}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Irrigated Farmland: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {W} or {U}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Canyon Slough: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {R}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Sheltered Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {G}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Scattered Groves: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {G} or {W}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Glittering Massif: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {W}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Festering Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {G}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Capital City: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{1}, {T}: Add one mana of any color.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Forsake the Worldly: oracle_text diverges (similarity 0.22); scryfall='Exile target artifact or enchantment.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fluctuator: oracle_text diverges (similarity 0.17); scryfall='Cycling abilities you activate cost {2} less to activate.'
- oracle_text advisory -- Drannith Stinger: oracle_text diverges (similarity 0.26); scryfall='Whenever you cycle another card, this creature deals 1 damage to each opponent.
Cycling {1} ({1}, Discard thi
- oracle_text advisory -- Hollow One: oracle_text diverges (similarity 0.20); scryfall="This spell costs {2} less to cast for each card you've cycled or discarded this turn.
Cycling {2} ({2}, Disca
- oracle_text advisory -- Unearth: oracle_text diverges (similarity 0.24); scryfall='Return target creature card with mana value 3 or less from your graveyard to the battlefield.
Cycling {2} ({2
- oracle_text advisory -- Scrying Sheets: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{1}{S}, {T}: Look at the top card of your library. If that card is snow, you may reveal it and 
- oracle_text advisory -- Skred: oracle_text diverges (similarity 0.23); scryfall='Skred deals damage to target creature equal to the number of snow permanents you control.'
- oracle_text advisory -- Coldsteel Heart: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.
As this artifact enters, choose a color.
{T}: Add one mana of the chosen color.
- oracle_text advisory -- Abominable Treefolk: oracle_text diverges (similarity 0.35); scryfall="Trample
Abominable Treefolk's power and toughness are each equal to the number of snow permanents you control
- oracle_text advisory -- Arcum's Astrolabe: oracle_text diverges (similarity 0.25); scryfall='({S} can be paid with one mana from a snow source.)
When this artifact enters, draw a card.
{1}, {T}: Add on
- oracle_text advisory -- Frost Augur: oracle_text diverges (similarity 0.39); scryfall="{S}, {T}: Look at the top card of your library. If it's a snow card, you may reveal it and put it into your ha
- oracle_text advisory -- Ice-Fang Coatl: oracle_text diverges (similarity 0.21); scryfall='Flash
Flying
When this creature enters, draw a card.
This creature has deathtouch as long as you control at
- oracle_text advisory -- Jorn, God of Winter: oracle_text diverges (similarity 0.10); scryfall='Whenever Jorn attacks, untap each snow permanent you control.'
- oracle_text advisory -- Kaldring, the Rimestaff: oracle_text diverges (similarity 0.17); scryfall='{T}: You may play target snow permanent card from your graveyard this turn. If you do, it enters tapped.'
- oracle_text advisory -- Marit Lage's Slumber: oracle_text diverges (similarity 0.35); scryfall="Whenever Marit Lage's Slumber or another snow permanent you control enters, scry 1.
At the beginning of your 
- oracle_text advisory -- Rimefeather Owl: oracle_text diverges (similarity 0.26); scryfall="Flying
Rimefeather Owl's power and toughness are each equal to the number of snow permanents on the battlefie
- oracle_text advisory -- Rimescale Dragon: oracle_text diverges (similarity 0.29); scryfall="Flying
{2}{S}: Tap target creature and put an ice counter on it. ({S} can be paid with one mana from a snow s
- oracle_text advisory -- Soul's Attendant: oracle_text diverges (similarity 0.12); scryfall='Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Auriok Champion: oracle_text diverges (similarity 0.11); scryfall='Protection from black and from red
Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Ocelot Pride: oracle_text diverges (similarity 0.12); scryfall="First strike, lifelink
Ascend (If you control ten or more permanents, you get the city's blessing for the res
- oracle_text advisory -- Serra Ascendant: oracle_text diverges (similarity 0.21); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)
As long as you have 30 or mo
- oracle_text advisory -- Ajani's Pridemate: oracle_text diverges (similarity 0.09); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Voice of the Blessed: oracle_text diverges (similarity 0.26); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.
As long as this creature has four or more +1/+1
- oracle_text advisory -- Daxos, Blessed by the Sun: oracle_text diverges (similarity 0.18); scryfall="Daxos's toughness is equal to your devotion to white. (Each {W} in the mana costs of permanents you control co
- oracle_text advisory -- Archangel of Thune: oracle_text diverges (similarity 0.24); scryfall='Flying
Lifelink (Damage dealt by this creature also causes you to gain that much life.)
Whenever you gain li
- oracle_text advisory -- Heliod, Sun-Crowned: oracle_text diverges (similarity 0.13); scryfall="Indestructible
As long as your devotion to white is less than five, Heliod isn't a creature.
Whenever you ga
- oracle_text advisory -- Ranger-Captain of Eos: oracle_text diverges (similarity 0.12); scryfall="When this creature enters, you may search your library for a creature card with mana value 1 or less, reveal i
- oracle_text advisory -- Ajani, Strength of the Pride: oracle_text diverges (similarity 0.27); scryfall='+1: You gain life equal to the number of creatures you control plus the number of planeswalkers you control.

- oracle_text advisory -- Apex Altisaur: oracle_text diverges (similarity 0.17); scryfall="When this creature enters, it fights up to one target creature you don't control.
Enrage — Whenever this crea
- oracle_text advisory -- World War Hulk: oracle_text diverges (similarity 0.17); scryfall='(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)
I — The next red or 
- oracle_text advisory -- Ghalta, Stampede Tyrant: oracle_text diverges (similarity 0.17); scryfall='Trample
When Ghalta enters, put any number of creature cards from your hand onto the battlefield.'
- oracle_text advisory -- Lyra Dawnbringer: oracle_text diverges (similarity 0.11); scryfall='Flying
First strike (This creature deals combat damage before creatures without first strike.)
Lifelink (Dam
- oracle_text advisory -- Youthful Valkyrie: oracle_text diverges (similarity 0.08); scryfall='Flying
Whenever another Angel you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Bishop of Wings: oracle_text diverges (similarity 0.10); scryfall='Whenever an Angel you control enters, you gain 4 life.
Whenever an Angel you control dies, create a 1/1 white
- oracle_text advisory -- Righteous Valkyrie: oracle_text diverges (similarity 0.15); scryfall="Flying
Whenever another Angel or Cleric you control enters, you gain life equal to that creature's toughness.
- oracle_text advisory -- Seraph Sanctuary: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 1 life.
Whenever an Angel you control enters, you gain 1 life.
{T}: Add {C}.
- oracle_text advisory -- Resplendent Angel: oracle_text diverges (similarity 0.11); scryfall='Flying
At the beginning of each end step, if you gained 5 or more life this turn, create a 4/4 white Angel cr
- oracle_text advisory -- Legion Angel: oracle_text diverges (similarity 0.07); scryfall='Flying
When this creature enters, you may reveal a card you own named Legion Angel from outside the game and 
- oracle_text advisory -- Serra the Benevolent: oracle_text diverges (similarity 0.16); scryfall='+2: Creatures you control with flying get +1/+1 until end of turn.
−3: Create a 4/4 white Angel creature toke
- oracle_text advisory -- Giada, Font of Hope: oracle_text diverges (similarity 0.11); scryfall='Flying, vigilance
Each other Angel you control enters with an additional +1/+1 counter on it for each Angel y
- oracle_text advisory -- Saproling Burst: oracle_text diverges (similarity 0.13); scryfall='Fading 7 (This enchantment enters with seven fade counters on it. At the beginning of your upkeep, remove a fa
- oracle_text advisory -- Shroofus Sproutsire: oracle_text diverges (similarity 0.06); scryfall='Trample
Whenever a Saproling you control deals combat damage to a player, create that many 1/1 green Saprolin
- oracle_text advisory -- Slimefoot, the Stowaway: oracle_text diverges (similarity 0.05); scryfall='Whenever a Saproling you control dies, Slimefoot deals 1 damage to each opponent and you gain 1 life.
{4}: Cr
- oracle_text advisory -- Vitaspore Thallid: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Deathspore Thallid: oracle_text diverges (similarity 0.10); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Sporecrown Thallid: oracle_text diverges (similarity 0.22); scryfall="Each other creature you control that's a Fungus or Saproling gets +1/+1."
- oracle_text advisory -- Tukatongue Thallid: oracle_text diverges (similarity 0.13); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Yavimaya Sapherd: oracle_text diverges (similarity 0.06); scryfall='When this creature enters, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Deathbloom Thallid: oracle_text diverges (similarity 0.05); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- The Mycotyrant: oracle_text diverges (similarity 0.18); scryfall='Trample
The Mycotyrant\'s power and toughness are each equal to the number of creatures you control that are 
- oracle_text advisory -- Simic Growth Chamber: oracle_text diverges (similarity 0.21); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {G}{
- oracle_text advisory -- Psychotrope Thallid: oracle_text diverges (similarity 0.22); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Concordant Crossroads: oracle_text diverges (similarity 0.01); scryfall='All creatures have haste.'
- oracle_text advisory -- Brightcap Badger: oracle_text diverges (similarity 0.12); scryfall='Each Fungus and Saproling you control has "{T}: Add {G}."
At the beginning of your end step, create a 1/1 gre
- oracle_text advisory -- Fungus Frolic: oracle_text diverges (similarity 0.10); scryfall='Create two 1/1 green Saproling creature tokens. (Then exile this card. You may cast the creature later from ex
- oracle_text advisory -- Spore Swarm: oracle_text diverges (similarity 0.06); scryfall='Create three 1/1 green Saproling creature tokens.'
- oracle_text advisory -- Saproling Migration: oracle_text diverges (similarity 0.13); scryfall='Kicker {4} (You may pay an additional {4} as you cast this spell.)
Create two 1/1 green Saproling creature to
- oracle_text advisory -- Doubling Season: oracle_text diverges (similarity 0.28); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Parallel Lives: oracle_text diverges (similarity 0.13); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Primal Vigor: oracle_text diverges (similarity 0.15); scryfall='If one or more tokens would be created, twice that many of those tokens are created instead.
If one or more +
- oracle_text advisory -- Beastmaster Ascension: oracle_text diverges (similarity 0.17); scryfall='Whenever a creature you control attacks, you may put a quest counter on this enchantment.
As long as this enc
- oracle_text advisory -- Thallid: oracle_text diverges (similarity 0.17); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Thallid Shell-Dweller: oracle_text diverges (similarity 0.13); scryfall='Defender
At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters 
- oracle_text advisory -- Sporesower Thallid: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on each Fungus you control.
Remove three spore counters 
- oracle_text advisory -- Utopia Mycon: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Mycoloth: oracle_text diverges (similarity 0.14); scryfall='Devour 2 (As this creature enters, you may sacrifice any number of creatures. It enters with twice that many +
- oracle_text advisory -- Lightstall Inquisitor: oracle_text diverges (similarity 0.17); scryfall='Vigilance
When this creature enters, each opponent exiles a card from their hand and may play that card for a
- oracle_text advisory -- Lyra, Archangel of Dawn: oracle_text diverges (similarity 0.06); scryfall='Flying
Whenever you gain life, put a +1/+1 counter on each Angel you control.'
- oracle_text advisory -- Sunrise Sovereign: oracle_text diverges (similarity 0.10); scryfall='Other Giant creatures you control get +2/+2 and have trample.'
- oracle_text advisory -- Stinkdrinker Daredevil: oracle_text diverges (similarity 0.11); scryfall='Giant spells you cast cost {2} less to cast.'
- oracle_text advisory -- Giant Harbinger: oracle_text diverges (similarity 0.14); scryfall='When this creature enters, you may search your library for a Giant card, reveal it, then shuffle and put that 
- oracle_text advisory -- Risen Reef: oracle_text diverges (similarity 0.15); scryfall="Whenever this creature or another Elemental you control enters, look at the top card of your library. If it's 
- oracle_text advisory -- Flamekin Harbinger: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, you may search your library for an Elemental card, reveal it, then shuffle and put 
- oracle_text advisory -- Foundation Breaker: oracle_text diverges (similarity 0.17); scryfall="When this creature enters, you may destroy target artifact or enchantment.
Evoke {1}{G} (You may cast this sp
- oracle_text advisory -- Fulminator Mage: oracle_text diverges (similarity 0.07); scryfall='Sacrifice this creature: Destroy target nonbasic land.'
- oracle_text advisory -- Pyroclasm: oracle_text diverges (similarity 0.05); scryfall='Pyroclasm deals 2 damage to each creature.'
- oracle_text advisory -- Hamletback Goliath: oracle_text diverges (similarity 0.09); scryfall="Whenever another creature enters, you may put X +1/+1 counters on this creature, where X is that creature's po
- oracle_text advisory -- Borderland Behemoth: oracle_text diverges (similarity 0.06); scryfall='Trample
This creature gets +4/+4 for each other Giant you control.'
- oracle_text advisory -- Inferno Titan: oracle_text diverges (similarity 0.13); scryfall='{R}: This creature gets +1/+0 until end of turn.
Whenever this creature enters or attacks, it deals 3 damage 
- oracle_text advisory -- Surtland Flinger: oracle_text diverges (similarity 0.18); scryfall="Whenever this creature attacks, you may sacrifice another creature. When you do, this creature deals damage eq
- oracle_text advisory -- Tectonic Giant: oracle_text diverges (similarity 0.16); scryfall='Whenever this creature attacks or becomes the target of a spell an opponent controls, choose one —
• This cre
- oracle_text advisory -- Metallic Mimic: oracle_text diverges (similarity 0.17); scryfall='As this creature enters, choose a creature type.
This creature is the chosen type in addition to its other ty
- oracle_text advisory -- Smokebraider: oracle_text diverges (similarity 0.12); scryfall='{T}: Add two mana in any combination of colors. Spend this mana only to cast Elemental spells or activate abil
- oracle_text advisory -- Incandescent Soulstoke: oracle_text diverges (similarity 0.10); scryfall='Other Elemental creatures you control get +1/+1.
{1}{R}, {T}: You may put an Elemental creature card from you
- oracle_text advisory -- Fury: oracle_text diverges (similarity 0.13); scryfall='Double strike
When this creature enters, it deals 4 damage divided as you choose among any number of target c
- oracle_text advisory -- Adaptive Automaton: oracle_text diverges (similarity 0.23); scryfall='As this creature enters, choose a creature type.
This creature is the chosen type in addition to its other ty
- oracle_text advisory -- Corsair Captain: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, create a Treasure token. (It\'s an artifact with "{T}, Sacrifice this token: Add on
- oracle_text advisory -- Dire Fleet Captain: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature attacks, it gets +1/+1 until end of turn for each other attacking Pirate.'
- oracle_text advisory -- Goblin Tomb Raider: oracle_text diverges (similarity 0.09); scryfall='As long as you control an artifact, this creature gets +1/+0 and has haste.'
- oracle_text advisory -- Forerunner of the Coalition: oracle_text diverges (similarity 0.22); scryfall='When this creature enters, you may search your library for a Pirate card, reveal it, then shuffle and put that
- oracle_text advisory -- Staunch Crewmate: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, look at the top four cards of your library. You may reveal an artifact or Pirate ca
- oracle_text advisory -- Malcolm, the Eyes: oracle_text diverges (similarity 0.26); scryfall='Flying, haste
Whenever you cast your second spell each turn, investigate. (Create a Clue token. It\'s an arti
- oracle_text advisory -- Siren Stormtamer: oracle_text diverges (similarity 0.16); scryfall='Flying
{U}, Sacrifice this creature: Counter target spell or ability that targets you or a creature you contr
- oracle_text advisory -- Daring Buccaneer: oracle_text diverges (similarity 0.09); scryfall='As an additional cost to cast this spell, reveal a Pirate card from your hand or pay {2}.'
- oracle_text advisory -- Kitesail Larcenist: oracle_text diverges (similarity 0.17); scryfall='Flying, ward {1}
When this creature enters, for each player, choose up to one other target artifact or creatu
- oracle_text advisory -- Spirebluff Canal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {U} or {R}.'
- oracle_text advisory -- Blackcleave Cliffs: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {B} or {R}.'
- oracle_text advisory -- Battlefield Forge: oracle_text diverges (similarity 0.18); scryfall='{T}: Add {C}.
{T}: Add {R} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Karplusan Forest: oracle_text diverges (similarity 0.43); scryfall='{T}: Add {C}.
{T}: Add {R} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Tarnished Citadel: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{T}: Add one mana of any color. This land deals 3 damage to you.'
- oracle_text advisory -- Grand Coliseum: oracle_text diverges (similarity 0.53); scryfall='This land enters tapped.
{T}: Add {C}.
{T}: Add one mana of any color. This land deals 1 damage to you.'
- oracle_text advisory -- Ancient Tomb: oracle_text diverges (similarity 0.17); scryfall='{T}: Add {C}{C}. This land deals 2 damage to you.'
- oracle_text advisory -- Manabarbs: oracle_text diverges (similarity 0.12); scryfall='Whenever a player taps a land for mana, this enchantment deals 1 damage to that player.'
- oracle_text advisory -- Tamanoa: oracle_text diverges (similarity 0.13); scryfall='Whenever a noncreature source you control deals damage, you gain that much life.'
- oracle_text advisory -- Rhox Faithmender: oracle_text diverges (similarity 0.37); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)
If you would gain life, you 
- oracle_text advisory -- Vito, Thorn of the Dusk Rose: oracle_text diverges (similarity 0.16); scryfall='Whenever you gain life, target opponent loses that much life.
{3}{B}{B}: Creatures you control gain lifelink 
- oracle_text advisory -- Dina, Soul Steeper: oracle_text diverges (similarity 0.21); scryfall="Whenever you gain life, each opponent loses 1 life.
{1}, Sacrifice another creature: Dina gets +X/+0 until en
- oracle_text advisory -- Bilbo, Birthday Celebrant: oracle_text diverges (similarity 0.24); scryfall='If you would gain life, you gain that much life plus 1 instead.
{2}{W}{B}{G}, {T}, Exile Bilbo: Search your l
- oracle_text advisory -- Purity: oracle_text diverges (similarity 0.24); scryfall="Flying
If noncombat damage would be dealt to you, prevent that damage. You gain life equal to the damage prev
- oracle_text advisory -- Spellshock: oracle_text diverges (similarity 0.09); scryfall='Whenever a player casts a spell, this enchantment deals 2 damage to that player.'
- oracle_text advisory -- Pyrohemia: oracle_text diverges (similarity 0.11); scryfall='At the beginning of the end step, if no creatures are on the battlefield, sacrifice this enchantment.
{R}: Th
- oracle_text advisory -- Rolling Earthquake: oracle_text diverges (similarity 0.08); scryfall='Rolling Earthquake deals X damage to each creature without horsemanship and each player.'
- oracle_text advisory -- Beseech the Queen: oracle_text diverges (similarity 0.20); scryfall="({2/B} can be paid with any two mana or with {B}. This card's mana value is 6.)
Search your library for a car
- oracle_text advisory -- Green Sun's Zenith: oracle_text diverges (similarity 0.17); scryfall="Search your library for a green creature card with mana value X or less, put it onto the battlefield, then shu
- oracle_text advisory -- Dimir House Guard: oracle_text diverges (similarity 0.25); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)
Sacrifice a creatu
- oracle_text advisory -- Shriekmaw: oracle_text diverges (similarity 0.26); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)
When this creature
- oracle_text advisory -- Timeless Witness: oracle_text diverges (similarity 0.29); scryfall="When this creature enters, return target card from your graveyard to your hand.
Eternalize {5}{G}{G} ({5}{G}{
- oracle_text advisory -- Acidic Slime: oracle_text diverges (similarity 0.25); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)
When this creature enters
- oracle_text advisory -- Ageless Entity: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, put that many +1/+1 counters on this creature.'
- oracle_text advisory -- Selesnya Sanctuary: oracle_text diverges (similarity 0.06); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {G}{
- oracle_text advisory -- Blossoming Sands: oracle_text diverges (similarity 0.06); scryfall='This land enters tapped.
When this land enters, you gain 1 life.
{T}: Add {G} or {W}.'
- oracle_text advisory -- Verdant Sun's Avatar: oracle_text diverges (similarity 0.05); scryfall="Whenever this creature or another creature you control enters, you gain life equal to that creature's toughnes
- oracle_text advisory -- Feed the Clan: oracle_text diverges (similarity 0.04); scryfall='You gain 5 life.
Ferocious — You gain 10 life instead if you control a creature with power 4 or greater.'
- oracle_text advisory -- Nourish: oracle_text diverges (similarity 0.03); scryfall='You gain 6 life.'
- oracle_text advisory -- Eternal Witness: oracle_text diverges (similarity 0.13); scryfall='When this creature enters, you may return target card from your graveyard to your hand.'
- oracle_text advisory -- Elvish Harbinger: oracle_text diverges (similarity 0.20); scryfall='When this creature enters, you may search your library for an Elf card, reveal it, then shuffle and put that c
- oracle_text advisory -- Ezuri, Renegade Leader: oracle_text diverges (similarity 0.16); scryfall='{G}: Regenerate another target Elf.
{2}{G}{G}{G}: Elf creatures you control get +3/+3 and gain trample until 
- oracle_text advisory -- Scholar of Athreos: oracle_text diverges (similarity 0.13); scryfall='{2}{B}: Each opponent loses 1 life. You gain life equal to the life lost this way.'
- oracle_text advisory -- Brindle Boar: oracle_text diverges (similarity 0.05); scryfall='Sacrifice this creature: You gain 4 life.'
- oracle_text advisory -- Weather the Storm: oracle_text diverges (similarity 0.14); scryfall='You gain 3 life.
Storm (When you cast this spell, copy it for each spell cast before it this turn.)'
- oracle_text advisory -- Utopia Sprawl: oracle_text diverges (similarity 0.21); scryfall='Enchant Forest
As this Aura enters, choose a color.
Whenever enchanted Forest is tapped for mana, its contro
- oracle_text advisory -- Crab Umbra: oracle_text diverges (similarity 0.16); scryfall='Enchant creature
{2}{U}: Untap enchanted creature.
Umbra armor (If enchanted creature would be destroyed, in
- oracle_text advisory -- Staff of Domination: oracle_text diverges (similarity 0.09); scryfall='{1}: Untap this artifact.
{2}, {T}: You gain 1 life.
{3}, {T}: Untap target creature.
{4}, {T}: Tap target 
- oracle_text advisory -- Galvanic Alchemist: oracle_text diverges (similarity 0.18); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Seedcradle Witch: oracle_text diverges (similarity 0.08); scryfall='{2}{G}{W}: Target creature gets +3/+3 until end of turn. Untap that creature.'
- oracle_text advisory -- Trufflesnout: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, choose one —
• Put a +1/+1 counter on this creature.
• You gain 4 life.'
- oracle_text advisory -- Heroes' Reunion: oracle_text diverges (similarity 0.01); scryfall='Target player gains 7 life.'
- oracle_text advisory -- Rest for the Weary: oracle_text diverges (similarity 0.05); scryfall='Target player gains 4 life.
Landfall — If you had a land enter the battlefield under your control this turn, 
- oracle_text advisory -- Accomplished Alchemist: oracle_text diverges (similarity 0.03); scryfall='{T}: Add one mana of any color.
{T}: Add X mana of any one color, where X is the amount of life you gained th
- oracle_text advisory -- Blighted Steppe: oracle_text diverges (similarity 0.04); scryfall='{T}: Add {C}.
{3}{W}, {T}, Sacrifice this land: You gain 2 life for each creature you control.'
- oracle_text advisory -- Wellwisher: oracle_text diverges (similarity 0.02); scryfall='{T}: You gain 1 life for each Elf on the battlefield.'
- oracle_text advisory -- Nykthos Paragon: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, you may put that many +1/+1 counters on each creature you control. Do this only once e
- oracle_text advisory -- Blossoming Bogbeast: oracle_text diverges (similarity 0.05); scryfall='Whenever this creature attacks, you gain 2 life. Then creatures you control gain trample and get +X/+X until e
- oracle_text advisory -- Genesis Wave: oracle_text diverges (similarity 0.07); scryfall="Reveal the top X cards of your library. You may put any number of permanent cards with mana value X or less fr
- oracle_text advisory -- Champion of the Parish: oracle_text diverges (similarity 0.11); scryfall='Whenever another Human you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Field Marshal: oracle_text diverges (similarity 0.12); scryfall='Other Soldier creatures get +1/+1 and have first strike. (They deal combat damage before creatures without fir
- oracle_text advisory -- Thalia, Guardian of Thraben: oracle_text diverges (similarity 0.06); scryfall='First strike
Noncreature spells cost {1} more to cast.'
- oracle_text advisory -- Thalia's Lieutenant: oracle_text diverges (similarity 0.12); scryfall='When this creature enters, put a +1/+1 counter on each other Human you control.
Whenever another Human you co
- oracle_text advisory -- Rick, Steadfast Leader: oracle_text diverges (similarity 0.16); scryfall="As Greymond, Avacyn's Stalwart enters, choose two abilities from among first strike, vigilance, and lifelink.\
- oracle_text advisory -- Esper Sentinel: oracle_text diverges (similarity 0.14); scryfall="Whenever an opponent casts their first noncreature spell each turn, draw a card unless that player pays {X}, w
- oracle_text advisory -- Coppercoat Vanguard: oracle_text diverges (similarity 0.17); scryfall='Each other Human you control gets +1/+0 and has ward {1}. (Whenever it becomes the target of a spell or abilit
- oracle_text advisory -- Recruitment Officer: oracle_text diverges (similarity 0.17); scryfall='{3}{W}: Look at the top four cards of your library. You may reveal a creature card with mana value 3 or less f
- oracle_text advisory -- Jirina, Dauntless General: oracle_text diverges (similarity 0.09); scryfall="When Jirina enters, exile target player's graveyard.
Sacrifice Jirina: Humans you control gain hexproof and i
- oracle_text advisory -- Harbin, Vanguard Aviator: oracle_text diverges (similarity 0.08); scryfall='Flying
Whenever you attack with five or more Soldiers, creatures you control get +1/+1 and gain flying until 
- oracle_text advisory -- General Kudro of Drannith: oracle_text diverges (similarity 0.19); scryfall="Other Humans you control get +1/+1.
Whenever General Kudro or another Human you control enters, exile target 
- oracle_text advisory -- Cathar Commando: oracle_text diverges (similarity 0.06); scryfall='Flash
{1}, Sacrifice this creature: Destroy target artifact or enchantment.'
- oracle_text advisory -- Brutal Cathar: oracle_text diverges (similarity 0.10); scryfall='Whenever this creature enters or transforms into Brutal Cathar, exile target creature an opponent controls unt
- oracle_text advisory -- Moonrage Brute: oracle_text diverges (similarity 0.22); scryfall='First strike
Ward—Pay 3 life.
Nightbound (If a player casts at least two spells during their own turn, it be
- oracle_text advisory -- King Darien XLVIII: oracle_text diverges (similarity 0.17); scryfall='Other creatures you control get +1/+1.
{3}{G}{W}: Put a +1/+1 counter on King Darien and create a 1/1 white S
- oracle_text advisory -- Fortified Beachhead: oracle_text diverges (similarity 0.26); scryfall='As this land enters, you may reveal a Soldier card from your hand. This land enters tapped unless you revealed
- oracle_text advisory -- Silent Clearing: oracle_text diverges (similarity 0.10); scryfall='{T}, Pay 1 life: Add {W} or {B}.
{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Waterlogged Grove: oracle_text diverges (similarity 0.11); scryfall='{T}, Pay 1 life: Add {G} or {U}.
{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Adelbert Steiner: oracle_text diverges (similarity 0.02); scryfall='Lifelink
Adelbert Steiner gets +1/+1 for each Equipment you control.'
- oracle_text advisory -- Adaptive Omnitool: oracle_text diverges (similarity 0.07); scryfall='Equipped creature gets +1/+1 for each artifact you control.
Whenever equipped creature attacks, look at the t
- oracle_text advisory -- Kíli the Resourceful: oracle_text diverges (similarity 0.07); scryfall='Storied (If you control three or more artifacts, legendaries, and/or Sagas, you have an enduring story for the
- oracle_text advisory -- Almost Perfect: oracle_text diverges (similarity 0.20); scryfall='Enchant creature
Enchanted creature has base power and toughness 9/10 and has indestructible.'
- oracle_text advisory -- Arcanum Wings: oracle_text diverges (similarity 0.15); scryfall='Enchant creature
Enchanted creature has flying.
Aura swap {2}{U} ({2}{U}: Exchange this Aura with an Aura ca
- oracle_text advisory -- Boseiju, Who Endures: oracle_text diverges (similarity 0.38); scryfall='{T}: Add {G}.
Channel — {1}{G}, Discard this card: Destroy target artifact, enchantment, or nonbasic land an 
- oracle_text advisory -- Botanical Sanctum: oracle_text diverges (similarity 0.29); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {G} or {U}.'
- oracle_text advisory -- Bruna, Light of Alabaster: oracle_text diverges (similarity 0.24); scryfall='Flying, vigilance
Whenever Bruna attacks or blocks, you may attach to it any number of Auras on the battlefie
- oracle_text advisory -- Colossification: oracle_text diverges (similarity 0.14); scryfall='Enchant creature
When this Aura enters, tap enchanted creature.
Enchanted creature gets +20/+20.'
- oracle_text advisory -- Eldrazi Conscription: oracle_text diverges (similarity 0.39); scryfall='Enchant creature
Enchanted creature gets +10/+10 and has trample and annihilator 2. (Whenever it attacks, def
- oracle_text advisory -- Glittering Wish: oracle_text diverges (similarity 0.06); scryfall='You may reveal a multicolored card you own from outside the game and put it into your hand. Exile Glittering W
- oracle_text advisory -- Indrik Umbra: oracle_text diverges (similarity 0.38); scryfall='Enchant creature
Enchanted creature gets +4/+4 and has first strike, and all creatures able to block it do so
- oracle_text advisory -- Mother of Runes: oracle_text diverges (similarity 0.13); scryfall='{T}: Target creature you control gains protection from the color of your choice until end of turn.'
- oracle_text advisory -- Mythic Proportions: oracle_text diverges (similarity 0.40); scryfall='Enchant creature
Enchanted creature gets +8/+8 and has trample.'
- oracle_text advisory -- Open the Armory: oracle_text diverges (similarity 0.21); scryfall='Search your library for an Aura or Equipment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Prodigious Growth: oracle_text diverges (similarity 0.26); scryfall='Enchant creature
Enchanted creature gets +7/+7 and has trample.'
- oracle_text advisory -- Seaside Citadel: oracle_text diverges (similarity 0.15); scryfall='This land enters tapped.
{T}: Add {G}, {W}, or {U}.'
- oracle_text advisory -- Skycloud Expanse: oracle_text diverges (similarity 0.06); scryfall='{1}, {T}: Add {W}{U}.'
- oracle_text advisory -- Somberwald Sage: oracle_text diverges (similarity 0.12); scryfall='{T}: Add three mana of any one color. Spend this mana only to cast creature spells.'
- oracle_text advisory -- Unflinching Courage: oracle_text diverges (similarity 0.43); scryfall='Enchant creature
Enchanted creature gets +2/+2 and has trample and lifelink. (Damage dealt by the creature al
- oracle_text advisory -- Linvala, Shield of Sea Gate: oracle_text diverges (similarity 0.31); scryfall="Flying
At the beginning of combat on your turn, if you have a full party, choose target nonland permanent an 
- oracle_text advisory -- Troyan, Gutsy Explorer: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}{U}. Spend this mana only to cast spells with mana value 5 or greater or spells with {X} in their 
- oracle_text advisory -- Detention Sphere: oracle_text diverges (similarity 0.25); scryfall="When this enchantment enters, you may exile target nonland permanent not named Detention Sphere and all other 
- oracle_text advisory -- Steel of the Godhead: oracle_text diverges (similarity 0.31); scryfall="Enchant creature
As long as enchanted creature is white, it gets +1/+1 and has lifelink. (Damage dealt by the
- oracle_text advisory -- Auroral Procession: oracle_text diverges (similarity 0.07); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Reborn Hope: oracle_text diverges (similarity 0.10); scryfall='Return target multicolored card from your graveyard to your hand.'
- oracle_text advisory -- Rampaging Baloths: oracle_text diverges (similarity 0.14); scryfall='Trample
Landfall — Whenever a land you control enters, create a 4/4 green Beast creature token.'
- oracle_text advisory -- Mossborn Hydra: oracle_text diverges (similarity 0.13); scryfall="Trample (This creature can deal excess combat damage to the player or planeswalker it's attacking.)
This crea
- oracle_text advisory -- Scythecat Cub: oracle_text diverges (similarity 0.16); scryfall='Trample
Landfall — Whenever a land you control enters, put a +1/+1 counter on target creature you control. If
- oracle_text advisory -- Scute Swarm: oracle_text diverges (similarity 0.15); scryfall="Landfall — Whenever a land you control enters, create a 1/1 green Insect creature token. If you control six or
- oracle_text advisory -- Moraug, Fury of Akoum: oracle_text diverges (similarity 0.17); scryfall="Each creature you control gets +1/+0 for each time it has attacked this turn.
Landfall — Whenever a land you 
- oracle_text advisory -- Avenger of Zendikar: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, create a 0/1 green Plant creature token for each land you control.
Landfall — When
- oracle_text advisory -- Omnath, Locus of Rage: oracle_text diverges (similarity 0.20); scryfall='Landfall — Whenever a land you control enters, create a 5/5 red and green Elemental creature token.
Whenever 
- oracle_text advisory -- Phylath, World Sculptor: oracle_text diverges (similarity 0.19); scryfall='When Phylath enters, create a 0/1 green Plant creature token for each basic land you control.
Landfall — When
- oracle_text advisory -- Courser of Kruphix: oracle_text diverges (similarity 0.17); scryfall='Play with the top card of your library revealed.
You may play lands from the top of your library.
Landfall —
- oracle_text advisory -- Khalni Heart Expedition: oracle_text diverges (similarity 0.19); scryfall='Landfall — Whenever a land you control enters, you may put a quest counter on this enchantment.
Remove three 
- oracle_text advisory -- Akoum Hellhound: oracle_text diverges (similarity 0.15); scryfall='Landfall — Whenever a land you control enters, this creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Brushfire Elemental: oracle_text diverges (similarity 0.26); scryfall="Haste
This creature can't be blocked by creatures with power 2 or less.
Landfall — Whenever a land you contr
- oracle_text advisory -- Rampant Growth: oracle_text diverges (similarity 0.16); scryfall='Search your library for a basic land card, put that card onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Search for Tomorrow: oracle_text diverges (similarity 0.26); scryfall='Search your library for a basic land card, put it onto the battlefield, then shuffle.
Suspend 2—{G} (Rather t
- oracle_text advisory -- Cultivate: oracle_text diverges (similarity 0.24); scryfall='Search your library for up to two basic land cards, reveal those cards, put one onto the battlefield tapped an
- oracle_text advisory -- Evolving Wilds: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Terramorphic Expanse: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Myriad Landscape: oracle_text diverges (similarity 0.25); scryfall='This land enters tapped.
{T}: Add {C}.
{2}, {T}, Sacrifice this land: Search your library for up to two basi
- oracle_text advisory -- Primeval Titan: oracle_text diverges (similarity 0.16); scryfall='Trample
Whenever this creature enters or attacks, you may search your library for up to two land cards, put t
- oracle_text advisory -- Radiant Fountain: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 2 life.
{T}: Add {C}.'
- oracle_text advisory -- Haunted Fengraf: oracle_text diverges (similarity 0.12); scryfall='{T}: Add {C}.
{3}, {T}, Sacrifice this land: Return a creature card at random from your graveyard to your han
- oracle_text advisory -- Temple of Abandon: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Deceit: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Enlightenment: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malady: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malice: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Mystery: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Plenty: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Triumph: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Zhalfirin Void: oracle_text diverges (similarity 0.74); scryfall='When this land enters, scry 1. (Look at the top card of your library. You may put that card on the bottom.)
{
- oracle_text advisory -- Crystal Grotto: oracle_text diverges (similarity 0.39); scryfall='When this land enters, scry 1.
{T}: Add {C}.
{1}, {T}: Add one mana of any color.'
- oracle_text advisory -- Rumble Arena: oracle_text diverges (similarity 0.44); scryfall='Vigilance
When this land enters, scry 1. (Look at the top card of your library. You may put it on the bottom.
- oracle_text advisory -- The Grey Havens: oracle_text diverges (similarity 0.24); scryfall='When The Grey Havens enters, scry 1.
{T}: Add {C}.
{T}: Add one mana of any color among legendary creature c
- oracle_text advisory -- Galadriel of Lothlórien: oracle_text diverges (similarity 0.24); scryfall='Whenever the Ring tempts you, if you chose a creature other than Galadriel as your Ring-bearer, scry 3.
Whene
- oracle_text advisory -- Clifftop Retreat: oracle_text diverges (similarity 0.37); scryfall='This land enters tapped unless you control a Mountain or a Plains.
{T}: Add {R} or {W}.'
- oracle_text advisory -- Furycalm Snarl: oracle_text diverges (similarity 0.31); scryfall="As this land enters, you may reveal a Mountain or Plains card from your hand. If you don't, this land enters t
- oracle_text advisory -- Steelshaper's Gift: oracle_text diverges (similarity 0.18); scryfall='Search your library for an Equipment card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Swiftblade Vindicator: oracle_text diverges (similarity 0.27); scryfall="Double strike (This creature deals both first-strike and regular combat damage.)
Vigilance (Attacking doesn't
- oracle_text advisory -- Lizard Blades: oracle_text diverges (similarity 0.11); scryfall="Double strike
Equipped creature has double strike.
Reconfigure {2} ({2}: Attach to target creature you contr
- oracle_text advisory -- Magnetic Theft: oracle_text diverges (similarity 0.04); scryfall="Attach target Equipment to target creature. (Control of the Equipment doesn't change.)"
- oracle_text advisory -- Forge Anew: oracle_text diverges (similarity 0.09); scryfall='When this enchantment enters, return target Equipment card from your graveyard to the battlefield.
During you
- oracle_text advisory -- Raubahn, Bull of Ala Mhigo: oracle_text diverges (similarity 0.07); scryfall="Ward—Pay life equal to Raubahn's power.
Whenever Raubahn attacks, attach up to one target Equipment you contr
- oracle_text advisory -- Speaker of the Heavens: oracle_text diverges (similarity 0.16); scryfall="Vigilance, lifelink (Attacking doesn't cause this creature to tap. Damage dealt by this creature also causes y
- oracle_text advisory -- Path to Exile: oracle_text diverges (similarity 0.12); scryfall='Exile target creature. Its controller may search their library for a basic land card, put that card onto the b
- oracle_text advisory -- Sulfur Falls: oracle_text diverges (similarity 0.27); scryfall='This land enters tapped unless you control an Island or a Mountain.
{T}: Add {U} or {R}.'
- oracle_text advisory -- Gitaxian Probe: oracle_text diverges (similarity 0.16); scryfall="({U/P} can be paid with either {U} or 2 life.)
Look at target player's hand.
Draw a card."
- oracle_text advisory -- Generator Servant: oracle_text diverges (similarity 0.15); scryfall='{T}, Sacrifice this creature: Add {C}{C}. If any of that mana is spent on a creature spell, it gains haste unt
- oracle_text advisory -- Brainstorm: oracle_text diverges (similarity 0.07); scryfall='Draw three cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Legion Lieutenant: oracle_text diverges (similarity 0.11); scryfall='Other Vampires you control get +1/+1.'
- oracle_text advisory -- Vampire of the Dire Moon: oracle_text diverges (similarity 0.37); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)
Lifelink (Damage dealt by
- oracle_text advisory -- Falkenrath Pit Fighter: oracle_text diverges (similarity 0.11); scryfall='{1}{R}, Discard a card, Sacrifice a Vampire: Draw two cards. Activate only if an opponent lost life this turn.
- oracle_text advisory -- Stromkirk Condemned: oracle_text diverges (similarity 0.15); scryfall='Discard a card: Vampires you control get +1/+1 until end of turn. Activate only once each turn.'
- oracle_text advisory -- Voldaren Estate: oracle_text diverges (similarity 0.35); scryfall='{T}: Add {C}.
{T}, Pay 1 life: Add one mana of any color. Spend this mana only to cast a Vampire spell.
{5},
- oracle_text advisory -- Bloodghast: oracle_text diverges (similarity 0.19); scryfall="This creature can't block.
This creature has haste as long as an opponent has 10 or less life.
Landfall — Wh
- oracle_text advisory -- Stromkirk Captain: oracle_text diverges (similarity 0.11); scryfall='First strike
Other Vampire creatures you control get +1/+1 and have first strike.'
- oracle_text advisory -- Drana, Liberator of Malakir: oracle_text diverges (similarity 0.14); scryfall='Flying, first strike
Whenever Drana deals combat damage to a player, put a +1/+1 counter on each attacking cr
- oracle_text advisory -- Mavren Fein, Dusk Apostle: oracle_text diverges (similarity 0.14); scryfall='Whenever one or more nontoken Vampires you control attack, create a 1/1 white Vampire creature token with life
- oracle_text advisory -- Vampire Socialite: oracle_text diverges (similarity 0.26); scryfall="Menace (This creature can't be blocked except by two or more creatures.)
When this creature enters, if an opp
- oracle_text advisory -- Falkenrath Gorger: oracle_text diverges (similarity 0.27); scryfall="Each Vampire creature card you own that isn't on the battlefield has madness. The madness cost is equal to its
- oracle_text advisory -- Markov Baron: oracle_text diverges (similarity 0.26); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Asylum Visitor: oracle_text diverges (similarity 0.33); scryfall="At the beginning of each player's upkeep, if that player has no cards in hand, you draw a card and you lose 1 
- oracle_text advisory -- Captivating Vampire: oracle_text diverges (similarity 0.12); scryfall='Other Vampire creatures you control get +1/+1.
Tap five untapped Vampires you control: Gain control of target
- oracle_text advisory -- Bloodline Keeper: oracle_text diverges (similarity 0.12); scryfall='Flying
{T}: Create a 2/2 black Vampire creature token with flying.
{B}: Transform this creature. Activate on
- oracle_text advisory -- Lord of Lineage: oracle_text diverges (similarity 0.17); scryfall='Flying
Other Vampire creatures you control get +2/+2.
{T}: Create a 2/2 black Vampire creature token with fl
- oracle_text advisory -- Kessig Wolf Run: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {C}.
{X}{R}{G}, {T}: Target creature gets +X/+0 and gains trample until end of turn.'
- oracle_text advisory -- Harrow: oracle_text diverges (similarity 0.19); scryfall='As an additional cost to cast this spell, sacrifice a land.
Search your library for up to two basic land card
- oracle_text advisory -- Explosive Vegetation: oracle_text diverges (similarity 0.29); scryfall='Search your library for up to two basic land cards, put them onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Nature's Lore: oracle_text diverges (similarity 0.19); scryfall='Search your library for a Forest card, put that card onto the battlefield, then shuffle.'
- oracle_text advisory -- Three Visits: oracle_text diverges (similarity 0.18); scryfall='Search your library for a Forest card, put it onto the battlefield, then shuffle.'
- oracle_text advisory -- Vernal Bloom: oracle_text diverges (similarity 0.14); scryfall='Whenever a Forest is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Gauntlet of Power: oracle_text diverges (similarity 0.19); scryfall='As this artifact enters, choose a color.
Creatures of the chosen color get +1/+1.
Whenever a basic land is t
- oracle_text advisory -- Nissa, Who Shakes the World: oracle_text diverges (similarity 0.25); scryfall='Whenever you tap a Forest for mana, add an additional {G}.
+1: Put three +1/+1 counters on up to one target n
- oracle_text advisory -- Kalonian Hydra: oracle_text diverges (similarity 0.15); scryfall='Trample
This creature enters with four +1/+1 counters on it.
Whenever this creature attacks, double the numb
- oracle_text advisory -- Garruk, Primal Hunter: oracle_text diverges (similarity 0.20); scryfall='+1: Create a 3/3 green Beast creature token.
−3: Draw cards equal to the greatest power among creatures you c
- oracle_text advisory -- Primordial Hydra: oracle_text diverges (similarity 0.24); scryfall='This creature enters with X +1/+1 counters on it.
At the beginning of your upkeep, double the number of +1/+1
- oracle_text advisory -- Voracious Hydra: oracle_text diverges (similarity 0.21); scryfall="Trample
This creature enters with X +1/+1 counters on it.
When this creature enters, choose one —
• Double 
- oracle_text advisory -- Neverwinter Hydra: oracle_text diverges (similarity 0.29); scryfall='As this creature enters, roll X d6. It enters with a number of +1/+1 counters on it equal to the total of thos
- oracle_text advisory -- Steelbane Hydra: oracle_text diverges (similarity 0.16); scryfall='This creature enters with X +1/+1 counters on it.
{2}{G}, Remove a +1/+1 counter from this creature: Destroy 
- oracle_text advisory -- Gargos, Vicious Watcher: oracle_text diverges (similarity 0.23); scryfall="Vigilance
Hydra spells you cast cost {4} less to cast.
Whenever a creature you control becomes the target of
- oracle_text advisory -- Skyshroud Claim: oracle_text diverges (similarity 0.26); scryfall='Search your library for up to two Forest cards, put them onto the battlefield, then shuffle.'
- oracle_text advisory -- Martyr of Sands: oracle_text diverges (similarity 0.06); scryfall='{1}, Reveal X white cards from your hand, Sacrifice this creature: You gain three times X life.'
- oracle_text advisory -- Walking Ballista: oracle_text diverges (similarity 0.06); scryfall='This creature enters with X +1/+1 counters on it.
{4}: Put a +1/+1 counter on this creature.
Remove a +1/+1 
- oracle_text advisory -- Abiding Grace: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your end step, choose one —
• You gain 1 life.
• Return target creature card with mana v
- oracle_text advisory -- Emeria, the Sky Ruin: oracle_text diverges (similarity 0.19); scryfall='This land enters tapped.
At the beginning of your upkeep, if you control seven or more Plains, you may return
- oracle_text advisory -- Brainsurge: oracle_text diverges (similarity 0.09); scryfall='Draw four cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Street Wraith: oracle_text diverges (similarity 0.26); scryfall="Swampwalk (This creature can't be blocked as long as defending player controls a Swamp.)
Cycling—Pay 2 life. 
- oracle_text advisory -- Unpredictable Cyclone: oracle_text diverges (similarity 0.31); scryfall="If a cycling ability of another nonland card would cause you to draw a card, instead exile cards from the top 
- oracle_text advisory -- Curator of Mysteries: oracle_text diverges (similarity 0.16); scryfall='Flying
Whenever you cycle or discard another card, scry 1.
Cycling {U} ({U}, Discard this card: Draw a card.
- oracle_text advisory -- Emrakul, the Aeons Torn: oracle_text diverges (similarity 0.17); scryfall="This spell can't be countered.
When you cast this spell, take an extra turn after this one.
Flying, protecti
- oracle_text advisory -- Ulamog, the Ceaseless Hunger: oracle_text diverges (similarity 0.15); scryfall='When you cast this spell, exile two target permanents.
Indestructible
Whenever Ulamog attacks, defending pla
- oracle_text advisory -- Ulamog, the Infinite Gyre: oracle_text diverges (similarity 0.31); scryfall='When you cast this spell, destroy target permanent.
Indestructible
Annihilator 4 (Whenever this creature att
- oracle_text advisory -- Long-Term Plans: oracle_text diverges (similarity 0.10); scryfall='Search your library for a card, then shuffle and put that card third from the top.'
- oracle_text advisory -- Toxic Deluge: oracle_text diverges (similarity 0.13); scryfall='As an additional cost to cast this spell, pay X life.
All creatures get -X/-X until end of turn.'
- oracle_text advisory -- Lord of Atlantis: oracle_text diverges (similarity 0.47); scryfall="Other Merfolk get +1/+1 and have islandwalk. (They can't be blocked as long as defending player controls an Is
- oracle_text advisory -- Master of the Pearl Trident: oracle_text diverges (similarity 0.63); scryfall="Other Merfolk creatures you control get +1/+1 and have islandwalk. (They can't be blocked as long as defending
- oracle_text advisory -- Vodalian Hexcatcher: oracle_text diverges (similarity 0.39); scryfall='Flash
Other Merfolk you control get +1/+1.
Sacrifice a Merfolk: Counter target noncreature spell unless its 
- oracle_text advisory -- Phantasmal Image: oracle_text diverges (similarity 0.58); scryfall='You may have this creature enter as a copy of any creature on the battlefield, except it\'s an Illusion in add
- oracle_text advisory -- Cosi's Trickster: oracle_text diverges (similarity 0.38); scryfall='Whenever an opponent shuffles their library, you may put a +1/+1 counter on this creature.'
- oracle_text advisory -- Sink into Stupor: oracle_text diverges (similarity 0.58); scryfall="Return target spell or nonland permanent an opponent controls to its owner's hand."
- oracle_text advisory -- Dark Ritual: oracle_text diverges (similarity 0.04); scryfall='Add {B}{B}{B}.'
- oracle_text advisory -- Demonic Tutor: oracle_text diverges (similarity 0.21); scryfall='Search your library for a card, put that card into your hand, then shuffle.'
- oracle_text advisory -- Diabolic Intent: oracle_text diverges (similarity 0.25); scryfall='As an additional cost to cast this spell, sacrifice a creature.
Search your library for a card, put that card
- oracle_text advisory -- Culling the Weak: oracle_text diverges (similarity 0.13); scryfall='As an additional cost to cast this spell, sacrifice a creature.
Add {B}{B}{B}{B}.'
- oracle_text advisory -- Lake of the Dead: oracle_text diverges (similarity 0.14); scryfall="If this land would enter, sacrifice a Swamp instead. If you do, put this land onto the battlefield. If you don
- oracle_text advisory -- Profane Tutor: oracle_text diverges (similarity 0.27); scryfall='Suspend 2—{1}{B} (Rather than cast this card from your hand, pay {1}{B} and exile it with two time counters on
- oracle_text advisory -- Shadowborn Apostle: oracle_text diverges (similarity 0.17); scryfall='A deck can have any number of cards named Shadowborn Apostle.
{B}, Sacrifice six creatures named Shadowborn A
- oracle_text advisory -- Abhorrent Overlord: oracle_text diverges (similarity 0.24); scryfall='Flying
When this creature enters, create a number of 1/1 black Harpy creature tokens with flying equal to you
- oracle_text advisory -- Reaper from the Abyss: oracle_text diverges (similarity 0.15); scryfall='Flying
Morbid — At the beginning of each end step, if a creature died this turn, destroy target non-Demon cre
- oracle_text advisory -- Sire of Insanity: oracle_text diverges (similarity 0.14); scryfall='At the beginning of each end step, each player discards their hand.'
- oracle_text advisory -- Jetmir, Nexus of Revels: oracle_text diverges (similarity 0.40); scryfall='Creatures you control get +1/+0 and have vigilance as long as you control three or more creatures.
Creatures 
- oracle_text advisory -- Thrumming Stone: oracle_text diverges (similarity 0.17); scryfall='Spells you cast have ripple 4. (Whenever you cast a spell, you may reveal the top four cards of your library. 
- oracle_text advisory -- Chariot of Victory: oracle_text diverges (similarity 0.45); scryfall='Equipped creature has first strike, trample, and haste.
Equip {1}'
- oracle_text advisory -- Voyaging Satyr: oracle_text diverges (similarity 0.13); scryfall='{T}: Untap target land.'
- oracle_text advisory -- Surrak and Goreclaw: oracle_text diverges (similarity 0.63); scryfall='Trample
Other creatures you control have trample.
Whenever another nontoken creature you control enters, put
- oracle_text advisory -- Nylea, God of the Hunt: oracle_text diverges (similarity 0.54); scryfall="Indestructible
As long as your devotion to green is less than five, Nylea isn't a creature. (Each {G} in the 
- oracle_text advisory -- Extraplanar Lens: oracle_text diverges (similarity 0.51); scryfall='Imprint — When this artifact enters, you may exile target land you control.
Whenever a land with the same nam
- oracle_text advisory -- Horn of Greed: oracle_text diverges (similarity 0.27); scryfall='Whenever a player plays a land, that player draws a card.'
- oracle_text advisory -- Howling Mine: oracle_text diverges (similarity 0.37); scryfall="At the beginning of each player's draw step, if this artifact is untapped, that player draws an additional car
- oracle_text advisory -- Thraben Inspector: oracle_text diverges (similarity 0.26); scryfall='When this creature enters, investigate. (Create a Clue token. It\'s an artifact with "{2}, Sacrifice this toke
- oracle_text advisory -- Squadron Hawk: oracle_text diverges (similarity 0.19); scryfall='Flying
When this creature enters, you may search your library for up to three cards named Squadron Hawk, reve
- oracle_text advisory -- Curious Farm Animals: oracle_text diverges (similarity 0.10); scryfall='When this creature dies, you gain 3 life.
{2}, Sacrifice this creature: Destroy up to one target artifact or 
- oracle_text advisory -- Kami of False Hope: oracle_text diverges (similarity 0.09); scryfall='Sacrifice this creature: Prevent all combat damage that would be dealt this turn.'
- oracle_text advisory -- Enduring Innocence: oracle_text diverges (similarity 0.20); scryfall="Lifelink
Whenever one or more other creatures you control with power 2 or less enter, draw a card. This abili
- oracle_text advisory -- Lurrus of the Dream-Den: oracle_text diverges (similarity 0.23); scryfall='Companion — Each permanent card in your starting deck has mana value 2 or less. (If this card is your chosen c
- oracle_text advisory -- Careful Study: oracle_text diverges (similarity 0.27); scryfall='Draw two cards, then discard two cards.'
- oracle_text advisory -- Faithless Looting: oracle_text diverges (similarity 0.59); scryfall='Draw two cards, then discard two cards.
Flashback {2}{R} (You may cast this card from your graveyard for its 
- oracle_text advisory -- Purphoros, God of the Forge: oracle_text diverges (similarity 0.60); scryfall="Indestructible
As long as your devotion to red is less than five, Purphoros isn't a creature.
Whenever anoth
- oracle_text advisory -- Mystic Retrieval: oracle_text diverges (similarity 0.49); scryfall='Return target instant or sorcery card from your graveyard to your hand.
Flashback {2}{R} (You may cast this c
- oracle_text advisory -- Magus of the Bazaar: oracle_text diverges (similarity 0.22); scryfall='{T}: Draw two cards, then discard three cards.'
- oracle_text advisory -- Cephalid Coliseum: oracle_text diverges (similarity 0.62); scryfall='{T}: Add {U}. This land deals 1 damage to you.
Threshold — {U}, {T}, Sacrifice this land: Target player draws
- oracle_text advisory -- Golgari Grave-Troll: oracle_text diverges (similarity 0.53); scryfall='This creature enters with a +1/+1 counter on it for each creature card in your graveyard.
{1}, Remove a +1/+1
- oracle_text advisory -- Stinkweed Imp: oracle_text diverges (similarity 0.64); scryfall='Flying
Whenever this creature deals combat damage to a creature, destroy that creature.
Dredge 5 (If you wou
- oracle_text advisory -- Life from the Loam: oracle_text diverges (similarity 0.68); scryfall='Return up to three target land cards from your graveyard to your hand.
Dredge 3 (If you would draw a card, yo
- oracle_text advisory -- Keranos, God of Storms: oracle_text diverges (similarity 0.58); scryfall="Indestructible
As long as your devotion to blue and red is less than seven, Keranos isn't a creature.
Reveal
- oracle_text advisory -- Mogis, God of Slaughter: oracle_text diverges (similarity 0.58); scryfall="Indestructible
As long as your devotion to black and red is less than seven, Mogis isn't a creature.
At the 
- oracle_text advisory -- Klothys, God of Destiny: oracle_text diverges (similarity 0.65); scryfall="Indestructible
As long as your devotion to red and green is less than seven, Klothys isn't a creature.
At th
- oracle_text advisory -- Ephara, God of the Polis: oracle_text diverges (similarity 0.57); scryfall="Indestructible
As long as your devotion to white and blue is less than seven, Ephara isn't a creature.
At th
- oracle_text advisory -- Phenax, God of Deception: oracle_text diverges (similarity 0.65); scryfall='Indestructible
As long as your devotion to blue and black is less than seven, Phenax isn\'t a creature.
Crea
- oracle_text advisory -- Wild Defiance: oracle_text diverges (similarity 0.21); scryfall='Whenever a creature you control becomes the target of an instant or sorcery spell, that creature gets +3/+3 un
- oracle_text advisory -- Berserk: oracle_text diverges (similarity 0.25); scryfall='Cast this spell only before the combat damage step.
Target creature gains trample and gets +X/+0 until end of
- oracle_text advisory -- Temur Battle Rage: oracle_text diverges (similarity 0.33); scryfall='Target creature gains double strike until end of turn.
Ferocious — That creature also gains trample until end
- oracle_text advisory -- Might of Old Krosa: oracle_text diverges (similarity 0.33); scryfall='Target creature gets +2/+2 until end of turn. If you cast this spell during your main phase, that creature get
- oracle_text advisory -- Vines of Vastwood: oracle_text diverges (similarity 0.44); scryfall="Kicker {G} (You may pay an additional {G} as you cast this spell.)
Target creature can't be the target of spe
- oracle_text advisory -- Legion Leadership: oracle_text diverges (similarity 0.11); scryfall="Until end of turn, double target creature's power and it gains first strike."
- oracle_text advisory -- Spellskite: oracle_text diverges (similarity 0.18); scryfall='{U/P}: Change a target of target spell or ability to this creature. ({U/P} can be paid with either {U} or 2 li
- oracle_text advisory -- Blistering Firecat: oracle_text diverges (similarity 0.21); scryfall='Trample, haste
At the beginning of the end step, sacrifice this creature.
Morph {R}{R} (You may cast this ca
- oracle_text advisory -- Hystrodon: oracle_text diverges (similarity 0.31); scryfall='Trample
Whenever this creature deals combat damage to a player, you may draw a card.
Morph {1}{G}{G} (You ma
- oracle_text advisory -- Den Protector: oracle_text diverges (similarity 0.37); scryfall="Creatures with power less than this creature's power can't block it.
Megamorph {1}{G} (You may cast this card
- oracle_text advisory -- Fire-Lit Thicket: oracle_text diverges (similarity 0.36); scryfall='{T}: Add {C}.
{R/G}, {T}: Add {R}{R}, {R}{G}, or {G}{G}.'
- oracle_text advisory -- Drogskol Captain: oracle_text diverges (similarity 0.25); scryfall="Flying
Other Spirit creatures you control get +1/+1 and have hexproof. (They can't be the targets of spells o
- oracle_text advisory -- Supreme Phantom: oracle_text diverges (similarity 0.17); scryfall='Flying
Other Spirits you control get +1/+1.'
- oracle_text advisory -- Shacklegeist: oracle_text diverges (similarity 0.17); scryfall="Flying
This creature can block only creatures with flying.
Tap two untapped Spirits you control: Tap target 
- oracle_text advisory -- Spirit of the Labyrinth: oracle_text diverges (similarity 0.08); scryfall="Each player can't draw more than one card each turn."
- oracle_text advisory -- Flooded Strand: oracle_text diverges (similarity 0.14); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Island card, put it onto the battlef
- oracle_text advisory -- Geist of Saint Traft: oracle_text diverges (similarity 0.23); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)
Whenever Geist of
- oracle_text advisory -- Lingering Souls: oracle_text diverges (similarity 0.16); scryfall='Create two 1/1 white Spirit creature tokens with flying.
Flashback {1}{B} (You may cast this card from your g
- oracle_text advisory -- Patrician Geist: oracle_text diverges (similarity 0.14); scryfall='Flying
Other Spirits you control get +1/+1.
Spells you cast from your graveyard cost {1} less to cast.'
- oracle_text advisory -- Mausoleum Wanderer: oracle_text diverges (similarity 0.19); scryfall="Flying
Whenever another Spirit you control enters, this creature gets +1/+1 until end of turn.
Sacrifice thi
- oracle_text advisory -- Skyclave Apparition: oracle_text diverges (similarity 0.28); scryfall="When this creature enters, exile up to one target nonland, nontoken permanent you don't control with mana valu
- oracle_text advisory -- White Orchid Phantom: oracle_text diverges (similarity 0.21); scryfall='Flying, first strike
When this creature enters, destroy up to one target nonbasic land. Its controller may se
- oracle_text advisory -- Usher of the Fallen: oracle_text diverges (similarity 0.09); scryfall='Boast — {1}{W}: Create a 1/1 white Human Warrior creature token. (Activate only if this creature attacked this
- oracle_text advisory -- Millicent, Restless Revenant: oracle_text diverges (similarity 0.16); scryfall='Affinity for Spirits (This spell costs {1} less to cast for each Spirit you control.)
Flying
Whenever Millic
- oracle_text advisory -- Zhur-Taa Druid: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {G}.
Whenever you tap this creature for mana, it deals 1 damage to each opponent.'
- oracle_text advisory -- Spark Elemental: oracle_text diverges (similarity 0.46); scryfall="Trample, haste (This creature can deal excess combat damage to the player or planeswalker it's attacking. This
- oracle_text advisory -- Ball Lightning: oracle_text diverges (similarity 0.53); scryfall="Trample (This creature can deal excess combat damage to the player or planeswalker it's attacking.)
Haste (Th
- oracle_text advisory -- Flamekin Bladewhirl: oracle_text diverges (similarity 0.12); scryfall='As an additional cost to cast this spell, reveal an Elemental card from your hand or pay {3}.'
- oracle_text advisory -- Sunspine Lynx: oracle_text diverges (similarity 0.19); scryfall="Players can't gain life.
Damage can't be prevented.
When this creature enters, it deals damage to each playe
- oracle_text advisory -- Hellspark Elemental: oracle_text diverges (similarity 0.16); scryfall='Trample, haste
At the beginning of the end step, sacrifice this creature.
Unearth {1}{R} ({1}{R}: Return thi
- oracle_text advisory -- Nova Chaser: oracle_text diverges (similarity 0.12); scryfall='Trample
Champion an Elemental (When this enters, sacrifice it unless you exile another Elemental you control.
- oracle_text advisory -- Thunderkin Awakener: oracle_text diverges (similarity 0.17); scryfall="Haste
Whenever this creature attacks, choose target Elemental creature card in your graveyard with toughness 
- oracle_text advisory -- Vexing Devil: oracle_text diverges (similarity 0.20); scryfall='When this creature enters, any opponent may have it deal 4 damage to them. If a player does, sacrifice this cr
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- claude_sweep recorded at commit 41be66453 (HEAD 539ca5729320); re-run if play changed since (play_invariants + smoke digests track play live).

<!-- verify_deck:end -->

### CHECKPOINT ~05:45 UTC 2026-11-19 -- rebase verified, burn GT re-accepted, user references benched
- Rebased build (Release, 04:11 UTC) rc=0; unit tests pass. Full smoke: 229/235 byte-identical; the 6 moved rows are
  all burn (origin had already moved burn GT upstream, e.g. d66b5c307 rollout burn kills, 83d5587a7 opponent land
  block). Burn smoke + regression re-accepted (--deck=onedropburn,onedropburn2hg,midrangeburn,midrangeburn2hg):
  every aggregate faster or equal. Searched slower=2, ONE game (Midrange Burn seed 2052 gi50, d3+d5, 6->7, also 7 at
  d8 b0): our draws and line are identical through T3; the opponent's land sequence differs from T4 (83d5587a7) and the
  old opponent lost 1 life of its own between T4 and T5 -- an upstream opponent-model change, not a play regression.
  check_gt_logs: consistent.
- Viewer restarted from the primary checkout (MDT_REFERENCE_BACKUP_DIR set) on the new viewer code.
- USER references: 9 saved (s1-s7, s9, s10; seed 8 was never saved), committed b666fe13d, all on
  origin/references-autosave. ref_bench: search 3.333 = human 3.333, 0/9 short.
- USER viewer asks (2026-11-19): Fireblast's two-Mountain sacrifice and Grim Lavamancer's two-card graveyard exile
  each get ONE multi-select dialog. Fireblast (and Walk the Aeons' three Islands): SacBurstScope disclosure + one
  source label in PayFireblastAlternative / PayWalkBuyback (SpellEffects.h; needs the rebuild). Lavamancer: viewer
  only (gyExilePick2 / gyExileBurstAuto in index.html). Frame counts unchanged -> references keep alignment.
- NEXT: Release build (Fireblast fix) -> unit tests -> commit -> push + CI watch -> USER: "continue on to the mulligan
  profile": bash scripts/mullgen.sh run "decks/1 Drop Burn" fast (builds PGO first).
