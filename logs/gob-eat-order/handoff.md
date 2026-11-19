# Handoff: Goblins oracle eat-ORDER fold (wip/gob-eat-order)

Worktree: /workspaces/MagicDeckTester/.claude/worktrees/agent-ae8f56a1352ef506f, branch wip/gob-eat-order (base 86ec7c2f4).

## Done
- Commit 76ddb5592 (WIP, NOT YET BUILT OR COMPILED): the fold, check mode, unit test, design doc.
  - src/ai/ManaOracle.cpp: block "THE EAT-ORDER FOLD" above Realize (OracleEatOrderFoldOn, EatPlanOk, EatGapOk,
    EatNewForbid); EatClass / EatBodySwappable / t_eat_forbid / t_eat_table_out / Fnv64 before BuildSources;
    BuildSources fodder loop refactored through FodderBodyOk and records classes / applies the restriction;
    Realize: eat_fold domain, goal_cands guard shared, Pareto key hashes (F, res) when F non-empty, restriction around
    MinimalPayments (skip frontier if restriction emptied the table), child window state on each option;
    RealizationDominates; AuditPlan: realize_main(goal, eat), redo without fold if not covering or DOMINATED, check mode
    (MTG_ORACLE_EAT_ORDER_FOLD_CHECK=1 abort / =2 count), goal-fold check skipped for eat-folded r; summary line;
    test hooks EatOrderFoldCuts/Redos/Checked/Mismatches, SetEatOrderFoldCheckForTest, SetEatOrderFoldForTest.
  - src/ai/ManaOracle.h: PayOption::eat_live / eat_cont_sig / eat_forbid; the public test API.
  - src/cards/CardDatabase.{h,cpp}: CardDefinition::eat_body_audited / eat_item_audited / eat_perm_audited from
    raw-JSON whitelists (per-key reasons in the comments).
  - test/unit/test_mana_oracle.cpp: "the eat-order fold builds one order of the same eaten bodies (three Siege-Gang shots)".
  - docs/design/goblins-oracle-eat-order-fold-2026-11-19.md: design + per-card argument (section 3 measurements TODO).

## Design / exactness argument (full text in the design doc and the code block comment)
Window = consecutive payments with nothing between them that can tell which audited bodies are alive (only how many).
Exchange D-at-j / C-at-j1 (C had an uneaten copy at j) gives the same line: same mana per body, gap events blind to it,
and the table eats a class's copies in battlefield (entry) order, so the eaten copies per class are identical whatever
the timing. Canonical order: no payment eats class C after an earlier window payment ate a higher-ranked class while C
had an uneaten copy (rank = FNV of FodderKey). F (forbidden classes) is carried in PayOption and removed from the table.
Per card: exchanged bodies = Lackey, Piledriver, Muxus, Matron, Siege-Gang (not when named by the plan: KeyedApart),
Chainwhirler, Twinshot, Stingscourger, vanilla tokens. Never exchanged: King, Chieftain, Warchief, Rundvelt (statics),
Skirk (outlet), MWM / Pashalik (death triggers), Krenko ({T}). Gap items allowed: King/Chieftain/Warchief/SGC/
Chainwhirler/Skirk/Piledriver/Lackey casts, Bolt only with no opposing creature/planeswalker, SGC activation only with
its baked victim alive. Matron (tutor ranks the board), Muxus (reveal), Twinshot close the window. Board perms must be
eat_perm_audited (Pashalik/Rundvelt close it; MWM ok: self-only death). Continuation (cont_sig), batch, tap-ahead,
cast-sacrifice, combat hold close it. EatPlanOk: every action has a coloured payment (no free actions) or is an
audited land drop. Goal-fold deck premises required. Prune: frontiers with non-empty F merge only when identical
(F and resources hashed into the key). Restriction only under goal_cands_scripted. Fallback: not covering or
DOMINATED-* -> realise again without the eat fold.

## Remaining, in order
1. Chain 1 results (below): fix compile errors if any (build1.log); units must all pass incl. the new test; check
   mode pool must show 0 mismatches (grep "EAT-ORDER-FOLD-CHECK MISMATCH" check.err; summary "=== MANA ORACLE eat-order
   fold" line); 131075 probe: applies / ms for T2 m2 with fold on (u131075.err PLAN-COST, DECISION-COST lines).
2. If the probe does not reach ~6.6M applies (600-priced-s budget), consider widening the exchange set (lords as
   bodies needs a "no P/T or haste reader, no damage marked on our creatures" argument). Do NOT raise the budget.
3. Off/on arms: one pooled batch each over build/scratch/claude_gob_order/pool.json (42 games),
   MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_COST_TRACE=1, MTG_ORACLE_EAT_ORDER_FOLD=0 vs 1 (can run concurrently, threads and
   RSS caps split). Compare with flags.sh (flag lines identical except where off was CAPPED/CAPPED-DECISION), play
   digests identical, applies per seed with perseed.py.
4. Smoke from the worktree: MTG_RSS_CAP_GB=40 bash test/regression.sh --smoke -> ALL PASS, 0 configs changed.
5. Fill section 3 of the design doc, commit, push wip/gob-eat-order only.

## Running job
Chain 1: logs/gob-eat-order/chain1.sh (worktree), launched as `(sleep 180; flock .../claude_box.lock chain1.sh)`;
PIDs seen 3156691 / 3156693 / 3157120. Outputs in /workspaces/MagicDeckTester/build/scratch/claude_gob_order/:
chain1.status, build1.log, units1.log, u131075.{out,err} (probe: fold on, ONLY_MENU=446, DECISION_S=1200),
check.{out,err} (pool, CHECK=2). DONE marker: build/scratch/claude_gob_order/chain1.DONE.

## Measurements so far
None with the fold yet. Baseline (previous agent): 131075 T2 m2 with goal fold = 446 plans, 14.1M applies, 965 s
unbounded; budget covers ~6.6M.

## Original brief (verbatim)
You are continuing mana-oracle performance work in the MagicDeckTester repo (C++ MTG simulator). GOAL: make Goblins' mana-clean gate tractable by an EXACT fold of the remaining sac-for-mana "eat ORDER" work in the mana oracle, so decision s30131075 (game_index 131075) T2 main 2 finishes inside the existing 600-priced-second decision budget instead of needing ~965 s unbounded.

READ FIRST: CLAUDE.md (repo rules, binding), then docs/design/goblins-oracle-eat-fold-2026-11-19.md (the previous agent's full write-up: what MTG_ORACLE_GOAL_FOLD does, why it is exact, measurements, and why the remainder is eat ORDER + token-count variation), and docs/design/goblins-sac-mana-oracle-2026-11-18.md. Code: src/ai/ManaOracle.cpp (Realize, frontier/Pareto prune, CoreKey, the goal fold from commit 689c96872), src/ai/ManaPayment.cpp. Base your worktree branch on the local branch phase-1-2-deck-analyzer at 86ec7c2f4 (it contains the goal fold); name your branch wip/gob-eat-order.

STATE: with the goal fold, 131075 T2 m2 has 446 plans; unbounded it finishes all of them (6 APPLY-DROP, none capped) in 14.1M applies / 965 s; the budget covers ~6.6M. What remains: the order in which payments eat the same bodies, and token-count variation. Goblin King, Goblin Chieftain and Goblin Matron read the board between payments, and tokens are created mid-plan, so a naive commutation is NOT sound. You need a per-card argument: when do two eat orders reach boards that are equal on everything the verdict reads (CoreKey, plus whatever the unscripted/UNMODELED checks read)? Typical exact shapes: canonical ordering of interchangeable fodder (identical cards in identical state), or proving that a static lord's presence cannot change any payment's legality/output in a given window. Read the card data in src/cards/data/cards.json for every card involved; never reason from card memory.

THE BAR (USER rules): the fold must be a SOUND identity fold: nothing findable at unlimited budget may become unfindable. Provide a check mode (like MTG_ORACLE_GOAL_FOLD_CHECK) that realises folded plans in full and aborts or counts on any mismatch, and show 0 mismatches. Measure one pooled batch per arm, fold off vs on, over the same 42 Goblins games the previous agent used (the 12 heavy seeds + gi 130000-130029; the seed list is in its doc or in build/scratch/claude_gob_fold/). Flag lines must be identical between the arms except where the off arm was CAPPED/CAPPED-DECISION, and play digests must be identical (the oracle is analyzer-side; play must not change). Then: units (build/Release/mtg-test) all pass, plus a new unit test for the fold; smoke from your worktree ALL PASS with 0 configs changed. Default ON once exact. If you find no exact fold that reaches the budget, report that with numbers. Do NOT raise the decision budget; that is a USER decision.

MACHINE RULES (strict):
- Build only with ./build.sh (Release). Wait ~200 s after edits before building (9p clock lag).
- ONE engine/build process at a time on this box. Run every engine run and build as `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock <cmd>` with MTG_RSS_CAP_GB=40. The lock is shared with other agents and is not FIFO; expect to wait.
- Inside one lock hold, use the whole machine: pooled `mtg --batch` manifests with --threads $(nproc), or parallel processes whose per-process MTG_RSS_CAP_GB sum to <= 36. NEVER hold the lock with a single-threaded or open-ended job: give any unbounded probe a ceiling (e.g. MTG_MANA_ORACLE_DECISION_S=1200), and pair it with other work in the same hold. (The last agent idled the box this way.)
- Scratch under /workspaces/MagicDeckTester/build/scratch/claude_gob_order/; summaries under logs/gob-eat-order/ in your worktree.
- Never use `timeout`. Wait on runs with a run_in_background waiter keyed on a PID (`kill -0`) or a DONE marker, never `pgrep -f`, never repeated polling turns.
- Rebase, never merge. Commit to your wip branch and push ONLY wip/gob-eat-order (allowed anytime). NEVER push phase-1-2-deck-analyzer; the parent does that. Never touch references/.
- New env flags: read .claude/skills/coding-conventions.md (EnvOn("MTG_X", true) for default-ON).

CONTEXT BUDGET: when your context passes ~250K tokens, finish the current step, commit, write logs/gob-eat-order/handoff.md (done with hashes; remaining in order; these instructions verbatim; running jobs with PIDs/markers; measurements), and stop with a short report naming it. Normally stop between 250K and 350K; NEVER exceed 400K (the previous agent reached 424K; do not repeat that).

FINAL REPORT: commits, the exactness argument per card, the check-mode result, the off/on table (applies, oracle ms, CAPPED, CAPPED-DECISION, flag-line diff), 131075's new cost vs the 600-s budget, units/smoke results.
