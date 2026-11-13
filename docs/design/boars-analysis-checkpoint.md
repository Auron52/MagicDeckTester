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
    Angels / Snow byte-identical vs I2b). I3b TODO: Raggadragga (add its attack-untap clause to the same predicate).
  * I4 TODO: Decimator emerge.
  * I5 TODO: Ilharg (+ exalted declared-count fix, put creatures not firing attack triggers,
    legend-rule keep choice in the viewer).
  * Every integrator: add its Boars-only params to the BoarsProvider signature; whitelist its card
    for the Herald's Horn take-fold (Ilharg's end-step return needs a bounded term instead); unit
    tests; build; audits; commit locally; HARD 250K context limit (I1 ran to 466K).
* After integration: Stage 3 coverage re-run; `audit_card_costs.py` + `audit_card_fields.py`;
  `provider_audit.py --check`; Stage 4 `analyze_deck.py --no-rebuild`; ONE pooled smoke run (expect
  moves on Anti-Lifegain / Creature Giving / Five Colour from Heath, plus any exalted-fix movers);
  then 4-bis suite membership (3x Hinata cost gate), Stage 5 (verify_deck, 5a-5h, claude-play sweep
  fan-out, discard bucket policy), Stage 6 report.

## Open USER questions (surfaced, not blocking; repeat in the closing report)

D1-D8 in the ledger ("Agent defaults taken"), plus from I1: the viewer now asks a creature type for
Urza's Incubator / Mimic / Automaton in other decks; Boars no longer rides AntiLifegainProvider.
