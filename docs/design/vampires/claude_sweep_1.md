# Vampires — claude-play sweep 1 (analyze-deck Stage 5d), 2026-10-18

- commit: `1c4484e7e` (branch `vampires-analysis`), binary `build/Release/mtg` of that tree
- seeds: 91000-91019 (disjoint from every suite seed), 20 games, one Opus player per seed
- protocol: `--claude-play --seed S --max-turns 10 --reveal 6`; benchmark `--games 1 --seed S --depth 5 --budget-ms 200`
- player brief: build/scratch/vampires/sweep/BRIEF.md (scratch; player probe dirs build/scratch/vampires/sweep/s<seed>/)

## Outcome
Claude tied the search's win turn in all 20 games (T4: 91004, 91005, 91007, 91008, 91009, 91011, 91013,
91016, 91019, 91003; T5: 91000, 91001, 91002, 91006, 91010, 91012, 91014, 91015, 91017, 91018), each
player checking that its turn was the floor for its draws. Every hand-computed combat total, life change,
lord bonus, counter and cost matched the engine. Mechanics verified in play: madness (own, Gorger-granted,
from every outlet, at cleanup, surviving a sacrificed Gorger, unpayable auto-decline), Stromkirk Condemned
(once per copy, pump reaches the madness-cast creature), Falkenrath Pit Fighter (post-combat only;
discard -> sacrifice -> madness -> draw; creature-ability colour rules), Voldaren Estate (life payment,
Blood cost reduction, crack), Bloodghast (landfall return, haste at <= 10), Drana's two-step damage with
and without Stromkirk Captain, Mavren Fein, Vampire Socialite (both clauses), Bloodline Keeper (token,
transform, Lord of Lineage), Asylum Visitor (empty-hand draw), Aether Vial, convoke, lifelink, lords.
NOT exercised: Captivating Vampire's steal (claude-play runs game-index 0, whose opponent-spawn slot has no
creatures) — covered by test/unit/test_vampires_lords.cpp and the B integrator's seed 128 / gi 8 repro.

## Flags (12 distinct defects; full repros in build/scratch/vampires/fix/DEFECTS.md, copied below by id)
- A SERIOUS phantom convoke mana: two convoked Markov Barons in one plan share bodies; the executor reduces
  the cost by committed counts (91011, 91002; the shipped search uses it).
- K SERIOUS batch pre-payment ignores convoke: Baron paid twice, Blood activation dropped (91016).
- B convoke ignores creatures cast / Vial-put earlier in the same plan (91014, 91019, 91013, 91005, 91002).
- C convoke arm order ignores lord bonuses and pip colour (91000, 91003).
- D convoke arms print identically in main_phase plans (8 players).
- E Condemned pump (and two-outlet plans) offered when the plan's land/casts leave no card (91009, 91011,
  91013, 91008, 91006, 91007).
- F payer spends Voldaren Estate's pay-1-life mode when a painless assignment exists (10 players; both worlds).
- G duplicate Blood plans for identical Estates (91017, 91001, 91010).
- H human-play plans casting the 5th Vampire omit Bloodline Keeper's transform (91012).
- I transform payable only via creature-SPELL-only colour offered, refused; partial plan executes (91012, 91004).
- J plan lists 'pump, then Blood' but executes Blood first (cost +1) (91015, 91007).
- L payer spends Secluded Courtyard (pays creature abilities) on a creature spell while Unclaimed Territory
  could pay; an offered activation then no-ops (91001).
Status: all handed to the fix integrator (worktree `vampires-fix`); resolution recorded in the ledger's
"Sweep fixes" section and the `## Claude-play sweep` gate section after the fixes land.
