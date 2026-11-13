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
    2994/2994; 24 decks identical vs I5b item 1; Boars 40/40 avg 5.225 byte-identical; no movers. Items 3-4 remain.
  * I5b TODO (original list; (2) is now done): (1) Ilharg death clause `dies_to_library_position` 3 + its searched "may" + dig viewer; (2) exalted
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
