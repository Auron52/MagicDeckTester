# Sea Creatures analysis — resume checkpoint

Short resume note for the in-flight Sea Creatures `analyze-deck` run (Claude orchestrator session,
2026-11-16). The full state is the ledger `docs/design/analysis-Sea Creatures.md`.

## Where things live

* Worktree `/workspaces/MagicDeckTester/.claude/worktrees/sea-creatures`, branch
  `sea-creatures-analysis` (local commits only; NOT pushed; base `8cd0bbf23` (rebased 2026-11-16 from f92bb3b7c)). The primary checkout's
  `decks/Sea Creatures/` is untracked there (the user's drop); the worktree copy is committed.
* Scryfall JSON: `/workspaces/MagicDeckTester/build/scratch/sea/scry/`.
* Research drafts (gitignored): `logs/sea-research/*.md` in the worktree.

## Stage status (2026-11-17, before compaction)

* Stage 1 coverage DONE. Stage 2 research DONE (6 drafts in `logs/sea-research/`).
* Serial integration (brief `logs/sea-research/INTEGRATOR-BRIEF.md`; each step's row is in the ledger):
  * I1 DONE `4eb5a3e10` + `4cf605b4f` (Misty viewer-only hold) + `8682648b6`.
  * I2 DONE `fac368913` + `403b7f58c` (Koma, Spawning Kraken, Serpent, Lorthos, Whelming Wave, shared
    `ReturnPermanentToOwnersHand`).
  * I3 DONE `407838f7b` + `ffad8ce2d` (Kiora, Summon: Leviathan on the World War Hulk Saga engine;
    conditional main 2 on Leviathan-draw turns, `MTG_FORCE_USES_M2=1` = every-turn arm).
  * CG investigation doc cherry-picked `7374f09b1`: the CG churn was an A/B artifact (base binary on the
    new cards.json). RULE: every byte-identity arm uses its OWN commit's cards.json.
  * I4 DONE `8e1be70c1` + `314dbfc9c` (Hullbreaker, Brinelin; search declines by USER ruling).
  * I5 Sin IN FLIGHT (agent launched 2026-11-17 on base `314dbfc9c`), incl. the USER ruling: engine searches
    the subset, provider prune `MTG_SEA_SIN_STRIP_SAGAS` strips every Saga and nothing else.
  * I6 Trench Gorger NEXT: engine full menu; provider prune exile all lands if >= 6 in library, else decline.
* Smoke (d3 b10, 40 g): I2 37/40 avg 6.20; I3 40/40 avg 5.925; I4 40/40 avg 6.225 (I5 checks whether the
  I4 move is just composition -- missing cards dropped -- or a play defect).

## Next steps

1. Collect I5; launch I6 (Gorger) from `logs/sea-research/gorger.md` + the ledger's #10 rulings.
2. Stage 3 coverage re-run (must be clean); Stage 4 profile (`scripts/analyze_deck.py --no-rebuild`),
   provider audit, 4-bis suite rows in ALL THREE tiers (3x-Hinata cost gate: `suite_gate.py --cost`).
3. Stage 5 via `scripts/verify_deck.py`, plus the A/Bs owed to the USER: fetch Island-first vs Forest-first;
   Sprawl always-blue vs searched {U,G}; bounce declines vs =0; Wave never vs =0; Sin strip-Sagas vs =0;
   Gorger exile-all vs searched menu (report the entries the searched arm picks); main 2 conditional vs
   every-turn. Claude-play sweep (5d), discard bucket policy (Whelming Wave first to shed, ledger #15),
   leaf tie-break check (5c2).
4. Stage 6 report, with the full open-question list from the ledger.
5. Push the shared branch once integrated (4-hour rhythm; rebase onto origin, rebuild + smoke after).
