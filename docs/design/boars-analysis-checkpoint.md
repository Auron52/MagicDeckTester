# Boars analysis — resume checkpoint

Short resume note for the in-flight Boars `analyze-deck` run (Claude orchestrator session,
2026-11-13). The full state is the ledger `docs/design/analysis-Boars.md`; this file says where the
run stands and what to do next.

## Where things live

* Worktree `/workspaces/MagicDeckTester/.claude/worktrees/boars`, branch `boars-analysis` (local
  commits only; NOT pushed; base `d81e83fae`). The primary checkout's `decks/Boars/` is still
  untracked there; the worktree copy is committed in I1.
* Scryfall JSON for the 8 new cards: `/workspaces/MagicDeckTester/build/scratch/boars/scry/`.
* Research drafts (gitignored): `logs/boars-research/{ilharg,decimator,raggadragga,herald-zhurtaa,endraze-flinthoof-horn}.md`.
* Handoff files, if any agent hits 250K: `logs/boars-research/I*-handoff.md`.

## Stage status (as of the compaction request)

* Stage 1 coverage: DONE (8 missing + Windswept Heath old-model gap).
* Stage 2 research: DONE (5 drafts).
* Integration, serial, one agent per change set:
  * I1 DONE `3641c75fd` (+ledger `9ab087cb5`): Windswept Heath battlefield fetch, Flinthoof Boar,
    Herald's Horn, `creature_type` decision, `BoarsProvider`.
  * I2a DONE `5739ea267` (+ledger `febae2a30`): Zhur-Taa Druid; cache fix verified 0/466,440
    mismatches; Anti-Lifegain + Angels byte-identical vs I1. Owed: ping in the this-turn win
    projection (draft item E).
  * I2b DONE `103bb7592`: Herald of Ilharg (+ MTG_ATTACK_HERALD_TRIGGER projection), REQUIRED
    `FireOnCastTriggers(mana_spent)` at every site, `self_haste_cost` main_phase manifest row. Angels / Burn /
    Five Colour byte-identical vs I2a. I4 emerge must pass its paid cost and sacrifice before the call.
  * I3a DONE (End-Raze Forerunners, `temp_vigilance`, combat-kept-mana clause in `UntapSecondMainLive`; Stompy /
    Angels / Snow byte-identical vs I2b).
  * I3b DONE `db1c83d1b`: Raggadragga (anthem via CreatureHasManaAbility, declared-attacker untap + main-2 clause,
    7-mana cast trigger on the searched cast-trigger rank, refund -> site 9). 2966/2966 unit tests; Unpredictable
    Cyclone / Snow / Stompy Surprise byte-identical vs I3a; Boars 40/40 at d3 b10. OPEN USER Q: per-cast target rank
    (risk 3; recommend building it in I4). I4 must not ride emerge on `sac_pins` through the cast-trigger axis skip.
  * I4 DONE `2e2356e2e` (code `ccf358f45`): Decimator emerge (per-victim cast variant, victim on per-cast
    `soulfire_own_targets` not `sac_pins`) + cast-trigger team pump. 2975/2975 unit tests; Shadowborn Apostle /
    Landfall Elementals / Melira Pod byte-identical vs I3b; Boars 40/40 avg 5.325 at d3 b10 (was 5.525). Owed:
    this-turn win estimate ignores the team pump; risk 3 per-cast Ragga rank left for the next integrator.
  * I4c DONE `07e71e938`: Raggadragga target PER TRIGGER INSTANCE (risk 3 RESOLVED): packed per-instance rank +
    `GameState::cast_untap_pump_ordinal`; also fans breakpoint continuations. 2977/2977; Extra Turns / Unpredictable
    Cyclone / Stompy Surprise / Angels byte-identical vs I4; Boars 40/40 avg 5.325 (identical to I4).
  * I5a DONE `5be6f80bf`: Ilharg attack put from hand (searched pin, every name explicit incl. the default's) +
    end-step return, projection, Horn fold term, D6 clause. 2987/2987; Knights / Goblins / Anti-Lifegain
    byte-identical vs I4c; Boars 40/40 avg 5.225 at d3 b10 (was 5.325). USER Q: AttackPutDefault playout formula.
  * I5b item 1 DONE `b8f9c7ba5`: exalted counts the DECLARED attackers (ExaltedAtDeclare / ExaltedBonusForCombat,
    both combats + the put projection). 2990/2990; 9 decks identical vs I5a; Boars 40/40 avg 5.225 byte-identical; no
    movers. Items 2-4 handed off: `logs/boars-research/I5b-handoff.md`.
  * I5b item 2 DONE `b268c794c`: per-attacker "attacks" trigger helpers (both combats + ResolveCombatDamage's
    Leeching/Utvara hooks) take SOURCES from the DECLARED prefix only (CR 508.4); "whenever you attack" unchanged.
    2994/2994; 24 decks identical vs I5b item 1; Boars 40/40 avg 5.225 byte-identical; no movers. Items 3-4 remained.
  * I5b item 3 DONE `1c3871699` (+ I4 emerge-record lockstep fix `8521da210`): Ilharg's dies clause
    (`dies_to_library_position` 3, searched tuck/leave twins in both worlds, dig viewer; searched play now offers a
    second Ilharg). 3002/3002; Kithkin / Critter Lifegain / Mirrorwing Dragon / Knights identical vs item 2; Boars
    40/40 avg 5.225 wins byte-identical (tuck fires in 3/40). No movers. Item 4 (legend-rule keep) remains;
    owed: class-(c) duplicate-plan fold; USER Q: DiesTuckDefault playout policy.
  * I5b item 4 DONE `8600244c7`: the legend-rule keep (CR 704.5j) is SEARCHED on the cast routes (keep-old/keep-new
    twins in CollectActions + a plan-level fan + the attack-put bit) and asked of a human for every legend whose copies
    differ. 3006/3006; 28 duplicate-legend decks + 2 controls byte-identical by full game log; Boars 40/40 avg 5.225
    byte-identical. No movers. Remaining in-window provider picks (land play, puts, nested free casts, token copies):
    `docs/design/legend-rule-keep-remaining-greedy.md`. USER Q: the beyond-horizon keep default.
  * **I5b INTEGRATION COMPLETE (2026-11-13): items 1-4 done, all local commits, nothing pushed.**
  * **LK DONE**: `204cedbe6`, LK2 `b48089aa9` `c58f8b75a` `aa1450512`, LK3 `c8b303062` (upkeep routes; rollout legend-checks
    nested free casts), docs `b570e1ac9`. 3018/3018. 31-deck identity vs mtg.i5b4: no result change, none slower; logs
    moved Galadriel 15/20, Unpredictable Cyclone 2/20, Melira Pod 1/20, Prevent Damage 1/20; Boars 5.225 identical.
  * **Stage 3 DONE** (19/19 full; cost 616/616; fields clean). **Stage 4 DONE** `d16666093` (profile; BoarsProvider;
    NO_COST_INTERACTIONS; discard NO_RULE_CONSIDER_SEARCH on only 9 decisions in 400 games).
  * **4-bis DONE** `bca43941d` (rows in all 3 tiers; cost gate 1.05x Hinata -- but the gate's Boars figure was
    ~2.4x inflated vs the full regression run (boars2hg 4.4 s/game vs 10.8 measured; Hinata matched) -> re-measure
    on an idle box; USER Q: gate on units?). Then found + fixed:
    * Fetch-crack fix `26c08b53e` (casts priced against the pre-crack board; crack-first roots).
    * USER 2026-11-14: "We shouldn't fetch hold for the decks. Only very specific decks care about the hold." ->
      `124950596` ConsiderFetchHold default false. Smoke GT accepted on that binary `35a421903`.
    * USER: "Human play doesn't need to keep the hold, at least by default" + "allow deferring the fetch in the choice
      dialog" + "okay with it being on all of the time" -> viewer fix `b96880022`+`74d8e579d`: human fetch dialog =
      every legal target + "(hold: crack later)"; act=<fetch>;cast=... one-shot line accepted; hold-era refs replay
      via --legacy-human-fetch-hold; 836 refs 0 drift; autonomous byte-identical.
    * Regression+overnight run on 74d8e579d (NOT accepted): only Boars/CG move, all better in aggregate, but same-draw
      slower games (overnight s5005 gi153 etc.) traced to the on-play fetch target = top-1 of LIBRARY ORDER (fetched
      Mountain with Birds in hand). USER: "why do we only try the first candidate by default? That sounds bad. It's a
      different story if we have an intentional prune." then "We should obviously use the same heuristic that just
      grabs Stomping Ground." -> `43afd29fd`: on-play battlefield fetch uses the activation's LandSearchPicksMemo
      (needed-and-lacking colours first), base width ALL; AL/5C/Morph/Cyclone keep explicit top-1 (USER Q: keep?).
      3029/3029.
    * Measurement `build/scratch/boars/rank/r3b/cmp_*.txt` (300 g held-out seed 71001, + shuffle-decoupled salts):
      ORDER (shared heuristic, top-1) better/neutral everywhere (Boars d3 -0.018 t-2.7; CG play -0.012 t-2.3); all 5
      repros recover; controls identical. WIDTH all vs top-1 mixed: Boars play -0.033, Boars2hg +0.020, CG play +0.027
      (t3.4), CG2hg d3 +0.042. USER: "Searching every target is something that enables clairvoyance. I'm suspicious of
      your claims as a result." Answer given: decoupled salts close the post-fetch-shuffle channel; coupled vs
      decoupled width deltas are similar here. RECOMMENDED (awaiting USER): shared order + explicit TOP-1 fetch width
      (intentional prune, reason: extra targets mostly buy clairvoyance; costs CG at budget); alternative = budget-staged
      waves. (superseded: see the 2026-11-14 ~08:30 entry below.) Then: re-run smoke/regression/overnight-boars,
      accept, check_gt_logs.
  * **2026-11-14 ~08:30 UTC: fetch width TAKEN BY DEFAULT (NEVER-BLOCK rule, USER may overrule) + cast order draft**
    -> `4a59c17f6`: `DecisionProvider::OnPlayFetchWidth` default 1 (the land drop searches the top of the shared
    order; human play / unpruned audit keep every target; a held fetch's activation keeps LandSearchFetchPickWidth).
    USER 2026-11-14: "If you want a ruling on the cast order I recommend you come up with a recommended order and I
    review it." -> `docs/design/boars-cast-order-2026-11-14.md` + `BoarsProvider::CastOrderRank` behind
    MTG_BOARS_CAST_ORDER, DEFAULT OFF (awaiting the USER). 3029/3029.
    **RUNNING chain1** (pid in `build/scratch/boars/chain1/run.pid`, status `chain1/status.txt`, `chain1/DONE`):
    smoke -> regression -> overnight --deck=boars,boars2hg (MTG_PGO=0) on 4a59c17f6, then the cast-order A/B
    (`chain1/order_ab.json`, 400 g x 5 shapes x off/on, seeds 91001-95001; result `chain1/ab_cmp.txt`).
    NEXT: audit/classify each tier's movers, accept smoke + regression + the Boars overnight rows, check_gt_logs,
    commit GT; report the A/B to the USER (adoption is theirs).
  * **2026-11-14 ~09:10 UTC:** chain1's tiers ABORTED at the scenario gate (RW Burn hold-then-crack fixture broke at
    43afd29fd: the crack now takes Burn's ranked Sacred Foundry; fixture pinned to Mountain, `99932ef9a`). Its cast-order
    A/B finished (better/neutral in all 5 cells, results in docs/design/boars-cast-order-2026-11-14.md). USER approved the
    full cast order ("That order seems reasonable to me.") -> ADOPTED default ON `963b0858f`.
    **RUNNING chain2** (`build/scratch/boars/chain2/{run.pid,status.txt,DONE}`): smoke -> regression -> overnight
    --deck=boars,boars2hg on 963b0858f. NEXT: audit/classify movers, accept, check_gt_logs, commit GT.
    Stage 5d sweep DONE (16 games, ledger "## Claude-play sweep", 6 unresolved). Investigators (Opus, local only):
    `.claude/worktrees/boars-haste` (97009 Flinthoof self-haste unfindable at any budget, 97002 hidden by a gate,
    97005 no T4 land, + d0 gi92 cast-order repro) and `.claude/worktrees/boars-pay` (97013 emerge overpay, 97003
    creature-only leftover, human-menu dupes/unpayable emerge victims/dead equip, display fields). Briefs + handoffs in
    each worktree's `logs/boars-research/`. Their fixes land on boars-analysis by rebase, then another smoke/regression.
  * **2026-11-14 ~11:40 UTC (compaction point) -- CURRENT STATE, read this first.**
    * Branch `boars-analysis` HEAD `151d9b9b6` (+ this doc commit): cast order ON `963b0858f`; Flinthoof site-9 fix
      `ba5d932b0` (from boars-haste); Cyclone OnPlayFetchWidth 0 `94f435fee` (my 4a59c17f6 cap had cut Cyclone's
      USER-requested Xander's Lounge -- smoke attribution: Cyclone moved only at 4a59c17f6, Five Colour moved at
      43afd29fd, better); payment `c8004bd60` + human menu/display `080665c27` (from boars-pay). 3033/3033, 516/516.
    * **RUNNING chain3** pid in `build/scratch/boars/chain3/run.pid` (status.txt, DONE): smoke DONE (rc=1 = movers),
      regression running, then overnight --deck=boars,boars2hg. Waiter armed; cron faac6d71 (2-hourly, session-only).
      Smoke movers (16 cases): Boars d3 4.86->4.78, d5 4.92->4.87, 2HG 5.20->5.12, d0 5.495->5.501 (d0 lighter bar;
      greedy cast-with-haste follow-up pending); CG d0 better, d5 4.6467->4.6533 (top-1 width; USER width question),
      d3/2hg play-only; Five Colour better; Minotaur better everywhere; Bruna d0 5.938->5.935 (payment fix). Searched
      slower = 2, both CG d5 (gi105 = draws diverge after a fetch shuffle = clairvoyance, not a regression; gi113 = a REAL same-draws slowdown from the top-1 land-drop width + the ranking tie: T2 board Tomb+Birds makes every colour, library order picks Breeding Pool, no W left for Enlightened Tutor (Temple Garden / Stomping Ground cast both); persists at 16x; handed to the boars-rank agent as a second 97005-class case -- Birds is one source that cannot pay W and R at once);
      `classify_turn_later.sh smoke` running -> `chain3/classify_smoke.txt`.
    * NEXT when chain3 DONE: read regression + overnight logs, classify slower games, then
      `bash test/regression.sh --smoke --accept` (and --regression, --overnight --deck=boars,boars2hg), check_gt_logs,
      commit GT. Then integrate the two follow-up agents and re-run smoke/regression.
    * **Agents running** (worktrees with ABSOLUTE paths this time; both wait for chain3/DONE before their A/Bs):
      `.claude/worktrees/boars-rank` (97005 fetch-ranking tie -> pips needed vs board sources, heurarm
      MTG_FETCH_RANK_PIPS, A/B armed), `.claude/worktrees/boars-d0haste` (greedy d0/rollout "cast with haste" option,
      heurarm MTG_GREEDY_SELF_HASTE). Briefs in each `logs/boars-research/*-brief.md`. NOTE the earlier boars-haste /
      boars-pay worktrees are NESTED at `.claude/worktrees/boars/.claude/worktrees/` (relative-path mistake); their
      briefs sit in stray plain dirs `/workspaces/MagicDeckTester/.claude/worktrees/boars-{haste,pay}/logs/` (brief
      files only). Both branches are integrated; remove those worktrees via scripts/safe_worktree_remove.sh later.
    * Sweep record in the ledger: 5 of 6 flags fixed; 97005 open (boars-rank). Follow-ups: Vampires' Stromkirk
      Condemned likely has the same site-9 gap (USER Q). Both earlier agents overran 250K (360K/470K) -- watch it.
    * USER rulings this session (verbatim): "If you want a ruling on the cast order I recommend you come up with a
      recommended order and I review it." -> full list shown -> "That order seems reasonable to me." Fetch width:
      top-1 taken as the agent default (NOT yet answered by the USER).
  * **Hinata perf question (USER surprised by ~10 s/game):** TIMINGS.md says d3 b10 0.59 s/game, d5 b20 1.25 s/game
    (1 thread, post 9229b25ca); now d3 6.4 / d5 9-11.5 s/game (32-thread full load, d5 b30), 8 tail games = 41% of the
    d5 s3003 row; Hinata ~145 ms/kunit vs Boars ~50 and the 2e6-units~45s calibration (~23). NEXT (not started):
    idle-box single-thread timing at HEAD vs 9229b25ca on the TIMINGS shapes, then profile a typical 7-10 s game.
  * Also open: Boars d5 unbudgeted tail (~16% abandon); d5 b0 backstop rows identical to d3 b0 on 30/46 decks
    (they do run at depth 5); Raggadragga `target` viewer HARD_MISS (pre-existing); claude-play "hold then crack in
    the same plan" duplicate lines (USER Q: drop?).
  * AFTER the fetch decision + tiers: Stage 5 (verify_deck, 5a-5h, claude-play sweep fan-out, discard policy), Stage 6.
  * I5b TODO (original list; ALL DONE): (1) Ilharg death clause `dies_to_library_position` 3 + its searched "may" + dig viewer; (2) exalted
    declared-count fix in both combats + the projection; (3) put creatures firing pre-existing attack triggers --
    ApplyAttackSelfPumps / ApplyBattleCry / ApplyAnnihilator / ApplyAttackExileDefenderTop / ApplyAttackDrawTriggers
    / ApplyGoblinGuideAttackTriggers / AttackUntapSnowFires / ApplyAttackLifegainTeamPump must iterate the declared
    prefix for SOURCES; (4) legend-rule keep choice in the viewer (EnforceLegendRule asks a human only for Chandra).
  * Every integrator: add its Boars-only params to the BoarsProvider signature; whitelist its card
    for the Herald's Horn take-fold (Ilharg's end-step return needs a bounded term instead); unit
    tests; build; audits; commit locally; HARD 250K context limit (I1 ran to 466K).
* After integration: Stage 3 coverage re-run; `audit_card_costs.py` + `audit_card_fields.py`;
  `provider_audit.py --check`; Stage 4 `analyze_deck.py --no-rebuild`; ONE pooled smoke run (expect
  moves on Anti-Lifegain / Creature Giving / Five Colour from Heath, plus any exalted-fix movers);
  then 4-bis suite membership (3x Hinata cost gate), Stage 5 (verify_deck, 5a-5h, claude-play sweep
  fan-out, discard bucket policy), Stage 6 report.

## Open USER questions (surfaced, not blocking; repeat in the closing report)

D1-D8 in the ledger ("Agent defaults taken"), I3b risk 3 RESOLVED in I4c, plus from I1: the viewer now asks a creature type for
Urza's Incubator / Mimic / Automaton in other decks; Boars no longer rides AntiLifegainProvider.
Also: agent-chosen beyond-horizon playout defaults (AttackPutDefault formula; DiesTuckDefault; legend keep
LegendKeepIndex); Herald of Ilharg cast-order slot (D8); Zhur-Taa 2HG hold-back-to-ping and post-combat lethal ping in
human play; Herald's two "no decision" manifest rows; Ulamog + 7-mana spell in one plan still shares one target rank
(no current deck); owed: Zhur-Taa ping + Decimator team pump in the this-turn win projection; Raggadragga per-instance
target branching cost unmeasured.

## Orchestration notes (for the resumed orchestrator)

* Every integrator agent got a HARD ~250K brief; most still hit it — when one reports >~240K, SendMessage it to commit
  WIP + write `logs/boars-research/<name>-handoff.md` and stop, then launch a FRESH agent from that file.
* Snapshots of each stage's binary for identity checks live in `build/scratch/boars/mtg.*` (latest `mtg.i5b4`); identity
  scripts `build/scratch/boars/i5b4_run.sh` etc. (SORTED win lines or full game logs).
* Nothing pushed. The shared-branch push (4 h rhythm) happens from the parent after smoke + check_gt_logs.

* 2026-11-14 USER RULING on the land-drop fetch width (verbatim): "Okay, I get it. For a colour complex deck like
  creature giving you might have a point that we need to allow 2-3 options in some cases. Though it might still be
  preferable to figure out a rule that indeed narrows it sufficiently. I would try that first and only fall back on the
  other if all else fails and even then try to make it only 2 options."
  -> keep OnPlayFetchWidth = 1; fix the ORDER first (boars-rank: castable-pip bipartite match, MTG_FETCH_RANK_PIPS).
  A width-2 fallback only for top-1 misses no deliberate rule can resolve, never 3+. Relayed in rank-handoff.md.
  USER follow-up (verbatim): "Boars and such do not fall into the same category. The search choices are obvious."
  -> a width-2 fallback, if ever needed, is scoped to colour-complex decks (Creature Giving class); Boars stays top-1.
  USER rule for two-colour decks (verbatim): "If there are 2 colours you literally can just always get the shock and then
  basics after that. This makes fixing extremely easy." -> two-colour deck: the dual first while any is left, then the
  basic chosen by the castable-pip rule; relayed in rank-handoff.md for the fresh rank agent.
* boars-rank: 1st agent handed off at 220K -- `34483bf24` (castable-pip bipartite tie-break, MTG_FETCH_RANK_PIPS default
  OFF; units 3038/3038, scenarios 516/516; 97005 T6->T5, CG gi113 T5->T4 with it on). A FRESH agent now implements the
  USER shock-over-basic / two-colour rules on top, then the pooled A/B (rank2/run_ab.sh, after chain3/DONE).
