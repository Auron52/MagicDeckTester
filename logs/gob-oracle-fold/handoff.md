# Handoff: Goblins mana-oracle eat fold (wip/gob-oracle-fold)

Written 2026-11-19 ~07:00 UTC by the first agent, which handed off at ~280K context.
Worktree: `/workspaces/MagicDeckTester/.claude/worktrees/gob-oracle-fold`, branch `wip/gob-oracle-fold`
(created from origin/phase-1-2-deck-analyzer at 3e8a209bc).

## Done
- The WIP commit on `wip/gob-oracle-fold` (see `git log -1`; pushed) adds the diagnostic `MTG_ORACLE_FOLD_STATS=1`
  (default off, prints only) in `src/ai/ManaOracle.cpp`. **It has NOT been compiled yet** (no build allowed while the gate
  ran). It prints, for each AuditPlan's own realisation:
  `[mana-oracle] FOLD-STATS <ctx> applies= engine=full|drop capped= layers= fronts= kept= gy_out= sym_dup= outcomes= full= ek= unscripted= unmodeled= plan={..}`
  - `kept`: frontiers kept by ParetoKeep (summed over layers); `sym_dup`: kept frontiers whose CANONICAL core + Resources +
    frontier context equal an earlier kept one in the same layer (numbering-only twins = the ceiling of an exact
    symmetry fold); `gy_out`: kept frontiers whose graveyard (card numbers) is not inside the engine's end graveyard
    (only meaningful as a sizing number -- see the analysis: the graveyard is not in Goblins' key); `ek`: full outcomes
    with CoreKey == the engine end board's (the only outcomes covered/best read when the engine is full).
- No measurement has been taken yet. Nothing is running.

## Heavy seeds (gate claude-queue-20261119b, raw flags in build/scratch/mana_clean/claude-queue-20261119b/on.0.err)
- CAPPED-DECISION comes from only TWO games: seed 30131075 gi 131075 (431 flags; T2 m2, decision wall backstop 600 s;
  plans also hit the 400000-apply retry cap) and seed 30132663 gi 132663 (48 flags, T3 m2; also 8 of the 9 CAPPED).
- Other slow Goblins games (SLOW-GAME lines, oracle on): 130085, 131364, 131479, 132765, 132895, 133150, 133281, 133407,
  133979, 134078 (seed = 30000000 + gi, game-index = gi).
- Ordinary-seed set for the identity check: e.g. gi 130000-130029 (skip any heavy one).
- Repro form: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile
  'decks/Goblins/Goblins.profile.json' --games 1 --seed 30131075 --game-index 131075`.
  Manifest job shape: `{"name":..,"deck":"decks/Goblins/Goblins.cod","profile":"decks/Goblins/Goblins.profile.json","games":1,"seed":S,"game_index":GI}`.
- NOTE: the cap that fires is the oracle's WALL backstop (elapsed s), so CAPPED-DECISION counts are timing-dependent
  under contention; compare priced seconds / applies, not just the flag count.

## Design analysis so far
- For an engine-FULL plan, AuditPlan's covered/best read only outcomes with canonical CoreKey == the engine end board
  (and the same LineKey). Other outcomes feed side channels: `unscripted` (UNMODELED), `r.unmodeled`, same_line /
  CONT_DIFFERS, the ORACLE-GAP text (outcome count, "nearest" script), and everything on the engine-DROP path (APPLY-DROP,
  DROP-COVERED, PHANTOM, sibling payments) plus ENUM-GAP hit-only checks.
- The fodder table already folds identical bodies into one class (FodderKey; Build eats lowest-numbered copies first),
  and paths eating the same multiset converge and are deduped per layer by the exact key + ParetoKeep. So the explosion
  is expected to be DISTINCT body multisets (Muxus vs Piledriver vs token...), ~prod(n_class+1) frontiers per layer x
  dozens of eat options each -- genuinely different end boards in the oracle's model (different cores, incomparable).
- A goal-directed prune ("this frontier can no longer reach the engine's end core") is NOT provably exact:
  * The graveyard is NOT in Goblins' key (BuildSimKeyBase folds graveyard contents only under deck_gy_readers; hand and
    library are keyed by name hash), so no number-based graveyard invariant exists.
  * The battlefield is renumbered canonically, and bodies of an eaten class can come back mid-plan: Krenko (X tokens =
    Goblin count), Siege-Gang ETB tokens, Mogg War Marshal death token, Pashalik tokens, Rundvelt's exile-and-play,
    Muxus reveal. A proof would need per-card semantics (a classifier), and it would still leave `unscripted` /
    `r.unmodeled` side channels unverifiable in pruned subtrees.
  * Treating bodies as Pareto resources would change verdicts (a USER-level model change, not an exact fold).
- The remaining EXACT candidate: fold eats/frontiers that differ only by WHICH identical copy was eaten (numbering-only
  twins), keyed on full state (canonical core + Resources + every frontier-context component). Caveat to resolve: flag
  detail text prints card numbers (TapsText), and `best` keeps the LAST of an equal-resource chain while APPLY-DROP /
  "nearest" print the FIRST -- dropping a twin can change printed numbers even when the verdict is unchanged. The bar is
  literal flag-line identity (except CAPPED / CAPPED-DECISION), so this must be measured, not assumed.
- Other exact (per-node) levers if the fold is small: CoreKey copies the whole GameState per frontier; Resources builds
  string maps; these cut WALL (the backstop that actually fires) without changing any verdict -- but plans that hit the
  400000-apply cap stay CAPPED, so per-node speed alone cannot pass the gate.

## Next steps, in order
1. Wait until `/workspaces/MagicDeckTester/build/scratch/claude_gate0630/DONE` exists AND
   `/workspaces/MagicDeckTester/build/scratch/claude_push_lock` does not (gate PID was 2974752; wait with a
   run_in_background until-loop on the DONE file, never pgrep -f). Then wait ~200 s after any edit and `./build.sh` in the
   worktree (fix any compile error in the FOLD-STATS block first).
2. One pooled `mtg --batch` (all nproc threads, MTG_RSS_CAP_GB=40, scratch in build/scratch/claude_gob_fold/) over the 12
   heavy seeds with `MTG_MANA_ORACLE=2 MTG_ORACLE_FOLD_STATS=1 MTG_MANA_ORACLE_COST_TRACE=1`; read PLAN-COST /
   DECISION-COST (menu vs gate split) and FOLD-STATS (sym_dup vs kept, ek vs outcomes).
3. Decide: if sym_dup is a large share of kept on the heavy plans, implement the twin fold (EnvOn flag, default ON once
   proven), with a CHECK mode that runs both and aborts on any difference; verify flag-line identity on ~30 ordinary
   seeds + no-worse on the heavy ones; measure applies / priced s / CAPPED-DECISION before/after. If sym_dup is small,
   the brief says: stop and report why it is not provably exact, with these measurements -- do not ship a lossy version.
4. If shipping: full unit suite; `MTG_RSS_CAP_GB=40 bash test/regression.sh --smoke` from the worktree (ALL PASS, 0 configs
   changed); doc in docs/design/; commit and push wip/gob-oracle-fold; report commits, exactness argument, before/after,
   and whether Goblins looks gate-tractable.

## The brief's rules (verbatim)
GOAL: make the mana completeness ORACLE tractable on Goblins by removing wasted audit work: a SOUND fold of dominated or equivalent sac-for-mana "eat" options (Skirk Prospector-style outlets eating interchangeable 1/1 Goblin tokens or other bodies). Today the oracle spends its per-decision budget on wide token boards. In the latest gate (logs/mana_clean/claude-queue-20261119b/report.md, section "CAPPED-DECISION"), Goblins has 479 CAPPED-DECISION flags. Every top signature is "oracle audit wall backstop (600 elapsed s) spent before this plan was audited". Repro for the top one: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg 'decks/Goblins/Goblins.cod' --profile 'decks/Goblins/Goblins.profile.json' --games 1 --seed 30131075 --game-index 131075` (T2 m2, a board of ~12 goblins and tokens, Siege-Gang Commander sac outlet). CAPPED-DECISION fails the mana-clean gate, so this cost blocks Goblins from passing. The oracle is analysis-only, so play must stay byte-identical: no GT accept is needed, and smoke must show 0 configs changed.

CONTEXT TO READ:
- docs/design/mana-completeness-oracle.md (the oracle).
- docs/design/goblins-sac-mana-oracle-2026-11-18.md: MTG_ORACLE_SAC_FODDER, the oracle's sac-mana model. It notes Goblins oracle cost ~8.8x on wide boards and that "a sound fold of dominated eat options is owed".
- docs/design/extra-turns-mana-oracle-sizing-2026-11-10.md: earlier oracle cost collapses (hit-only Realize, SibPayMemo) and the soundness bar the oracle has to keep.
- docs/design/gods-oracle-decision-cap-2026-11-15.md.
- src/ai/ManaOracle.cpp/.h.
- The sac-mana parts of src/ai/ManaPayment.cpp.

THE BAR:
- The fold must be EXACT: the oracle's verdicts (every flag line except CAPPED / CAPPED-DECISION) must be identical before and after on a set of Goblins seeds that finish uncapped, and must be no worse on the capped ones.
- Collapsing wasted work is wanted unconditionally (CLAUDE.md "COLLAPSE WASTED SEARCH"). A LOSSY narrowing is not acceptable in the oracle: it would hide engine misses.
- Interchangeable tokens: identical 1/1 Goblin tokens with no distinguishing state are a valid symmetry ONLY if nothing reads their card numbers asymmetrically. Prove that or key on full state. Lords and Muxus etc. make non-token bodies non-interchangeable.
- Measure: applies, priced seconds and CAPPED-DECISION counts on ~10 heavy Goblins seeds (take them from the report's CAPPED-DECISION repros), plus flag-line identity on ~30 ordinary seeds.

MACHINE RULES (hard):
- ONE engine process at a time on this box, builds included. A pooled mana-clean gate is running now: PID in /workspaces/MagicDeckTester/build/scratch/claude_gate0630/pid, finished when build/scratch/claude_gate0630/DONE exists.
- Do NOT build or run any engine process until that DONE file exists, AND while /workspaces/MagicDeckTester/build/scratch/claude_push_lock exists. Until then, read code and design the fold. Wait with a run_in_background loop on the DONE file, never pgrep -f.
- Work in a NEW git worktree: `git worktree add .claude/worktrees/gob-oracle-fold -b wip/gob-oracle-fold origin/phase-1-2-deck-analyzer`. Never edit the primary checkout's src.
- Build only with ./build.sh. Wait ~200 s after edits before building (9p clock lag). Never wrap commands in timeouts.
- Batches use `mtg --batch` with all cores (nproc) and MTG_RSS_CAP_GB=40. Scratch goes in build/scratch/claude_gob_fold/. Copy summaries into logs/gob-oracle-fold/ in the worktree.
- Env flags via EnvOn (read .claude/skills/coding-conventions.md). A new fold should be default ON once it is proven exact.
- Never push the shared branch phase-1-2-deck-analyzer. Pushing wip/gob-oracle-fold is fine and expected when done.
- Rebase, never merge.
- Never touch references/.
- Windows CI uses MSVC: no GCC-only builtins.

WHEN DONE:
1. Full unit suite.
2. `MTG_RSS_CAP_GB=40 bash test/regression.sh --smoke` from the worktree: must be ALL PASS, 0 configs changed.
3. Write the doc in docs/design/.
4. Commit and push wip/gob-oracle-fold.
5. Report back: commits, the exactness argument, before/after measurements, and whether Goblins now looks gate-tractable.
If the fold turns out not to be provably exact, stop and report why, with the measurements. Do not ship a lossy version.

CONTEXT RULE: if your context passes ~250K tokens, finish the current step, commit WIP, write logs/gob-oracle-fold/handoff.md (done with hashes; remaining in order; these rules verbatim; running jobs with PIDs and markers; measurements), and stop with a short report naming that file. Never exceed 400K.

## Running jobs
None of mine. (The coordinator's gate: PID file build/scratch/claude_gate0630/pid, marker build/scratch/claude_gate0630/DONE.)
