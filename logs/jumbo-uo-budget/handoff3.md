# Handoff 3: Jumbo unlock-order lever CPU work (2026-11-22 12:40 UTC), written for an agent with no context

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/agent-a1cd888d17514cedb`, branch `wip/jumbo-uo-budget`
(push this wip branch only; never push `phase-1-2-deck-analyzer`, which the parent session lands).
Scratch dir `S=/workspaces/MagicDeckTester/build/scratch/claude_jumbo_uo_budget`. Older history: `handoff.md`,
`handoff2.md` (same dir). Main doc: `docs/design/jumbo-apply-drop-order-2026-11-18.md` (a new section for this
handoff's results is still owed; see step 5).

## Background in two lines
`MTG_PO_UNLOCK_ORDER` (the "unlock-order lever", default ON, shipped) finds cast orders where a haste equip or a
haste-granting creature unlocks a mana dork. It is a quality GAIN vs off (Jumbo -0.00018 turns/game, Bruna -0.00080,
both CIs exclude 0, 400k-game re-measure) but costs Jumbo +18% CPU. The task is to cut that CPU with EXACT
(byte-identical) cuts, because the USER does not accept CPU growth justified by headroom.

## USER rulings (verbatim)
- 2026-11-20: "I'm okay with trying (a), but we will reserve judgement until we see the results. 20% extra is kind of
  painful, but the other question is how fast or slow Jumbo Cactuar is compared to Hinata?"
- 2026-11-21: "How is the quality comparison with this on vs off? I'm looking for averages, not absolute numbers. I'm
  talking about vs lever off. Maybe we should also test each option on a larger set of games to get more accurate
  numbers."
- Adoption bar (CLAUDE.md): every slower game must recover at `--depth 8 --budget-ms 0`, and every DECK must be net <= 0
  vs the committed tree ("+1 is not neutral. 0 is neutral.").
- Standing: "3x is not a CPU licence" -- cut a fix's CPU cost, never justify it by headroom.
- USER 11-22 (memory): searched impact matters most; size searched samples properly and report them first.
- Coordinator: adopt nothing lossy; trade-offs are the USER's call. Exact cuts (byte-identical on an identity slice) may
  be adopted. Box: ONE big engine process at a time under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock`
  with `MTG_RSS_CAP_GB=40`; small work via `scripts/box_lane.sh --slots N --cap-gb G -- <cmd>` with N <= 2 now (the
  script was changed to max 2). Build only with `./build.sh`; wait ~200 s after editing src before building (9p lag).
  Never `timeout`, never `pkill -f`/`pgrep -f` waiters (use `kill -0 <pid>` or DONE files).

## Done this session (commits on wip/jumbo-uo-budget, NOT yet pushed until this handoff's commit)
- a2d3fcda8 Revert MTG_SPS_EQUIP_MEMO (8eaecbd00): 0 hits in 132,011 lookups (in-scope memo useless: repeats are ACROSS
  enumerations). Play byte-identical with/without; deleted.
- 3f77fbb85 diag: `MTG_PAY_CENSUS` gains fn `UOT` (the enumeration site's UnlockOrderSearchTree), ActSig now mixes
  `sac_victim_id`, repeat attribution by enumerator, `[uot-gap]` crude mana-gap buckets. Plus `MTG_UOT_XMEMO` prototype.
- ecb108487 `MTG_SPS_XMEMO` prototype (site 1 = SubsetPayableSequential as a whole) beside `MTG_UOT_XMEMO` (site 0);
  hit compares stored Actions whole; no memo under a live CurrentPlanContext()/CurrentPlanTraits().
- eb21f629d walk-equality on hit (`uotx::WalkSame`: equiv_ord, eval, cont_pins normalised; hand_index strict because
  the walk reads the hand card at hand_index) + per-field diff census; `MTG_XMEMO_NORM` (default 1) toggles the norm.
  Code: `src/ai/TurnSolver.cpp`, namespace `uotx` + `UoTreeXMemo(site, ...)` just above `SubsetPayableSequential`.
  All of this is DEFAULT OFF (MTG_UOT_XMEMO, MTG_SPS_XMEMO are EnvOn(..., false)).

## Measurements (Jumbo = gi 719000-719399 manifest `$S/prof/on.json`/`off.json`, 400 games; Bruna = same gi,
`$S/xm1/bruna_on.json`, `$S/ps1/bruna_off.json`; 2 threads in the lane; instruction counts via `perf stat -e
instructions:u`, deterministic, the CPU metric to use -- ms/cpu fields swing 2-3x with box load)
- Replay hypothesis (MTG_UO_BUDGET_REPLAY) REFUTED: px16/pcx16 lose the same 6 Bruna games as pc16 (`$S/prof/m_loss`).
  Diag logs (`$S/diag/`): the price variants diverge at the FIRST decisions (T1/T2 land/Greaves), i.e. the price's
  charges re-spend the early budget. Every budget price variant stays net +4..+8 vs ON (handoff2 numbers); none pass.
- perf (`$S/perf1/`): on vs off +13.7% cycles. UnlockOrderSearchTree 7.5% + apply UnlockOrderSearch 1.4% of on's
  cycles; inside the tree ~half is mana payment (TapForCostDirect), ~1% GameState snapshot copies; the rest of the
  delta is diffuse (more payable plans enumerated).
- Census (`$S/cen1`, `$S/xm1/cen`): the enum-site tree runs 126,312 times/400 Jumbo games, 106,520 false; 73,780 (58%)
  are exact repeats WITHIN a decision, all EnumeratePlans->EnumeratePlans on the same board content, 0 verdict
  mismatches once sac_victim_id is keyed. Repeat cycles = 35% of tree cycles. SPS overall: 180k of 360k repeat.
- MTG_UOT_XMEMO, hash-only action key (3f77fbb85, `$S/xm1`, `$S/ps1`): VERIFY 0 mismatches (Jumbo 73,780 hits, Bruna
  10,335), play byte-identical on Jumbo and Bruna 400. Instructions: Jumbo off 303.4G, on 348.9G (+15.0%), on+uotx
  338.0G (+11.4%) => cuts 24% of the lever's added work. Bruna off 825.3, on 837.4, on+uotx 836.6.
- Whole-Action compare (ecb108487, `$S/xm2`): verify 0, identical, but hits halve (Jumbo UOT 36,812 hits, act_diff
  36,968) so the saving falls to 3.1G (on 348.98 -> 345.86). SPS site: 66,556 hits, saves 3.0G, costs +0.8G on off;
  both sites together 347.5 (worse than UOT alone). Some non-walk Action field differs across enumerations.
- crude mana-gap (`[uot-gap]`): false trees are mostly gap >= +1, but true verdicts reach gap +7, so a naive supply
  bound would not prune soundly without crediting unlocks (not pursued).

## Running job
- `xm3`: lane job, box_lane PID 901357 (resubmitted 12:38 UTC; the first submit died with "error reading input file"
  because box_lane.sh was edited under it). Script `$S/xm3/chain.sh`: builds HEAD eb21f629d's src (`./build.sh`),
  copies to `$S/mtg.z`, then runs jver/bver (both sites + VERIFY; read the `[uot-xmemo] ... field diffs` lines in
  `$S/xm3/<tag>/batch.err`), then instruction counts for j/b {on,uot,sps,both}1 and joff/joffsps
  (`$S/xm3/<tag>/stat.csv`). DONE marker `$S/xm3/DONE` ("done" or "fail"). Build log `$S/xm3/build.log`.

## Next steps, in order
1. When `$S/xm3/DONE` exists: check VERIFY_MISMATCH=0 on both sites, the field-diff counts (which fields differed:
   if `other_after_norm` is 0 the normalisation explains all differences), and instruction counts
   (`grep instructions $S/xm3/*/stat.csv`). Identity: compare `played=` lines without ms/cpu between on and each arm
   (`sed 's/ ms=[0-9]*//;s/ cpu=[0-9]*//' | sort`, diff).
2. Pick the winner by instructions (expected: UOT site alone with normalisation, ~338G on Jumbo). If `other_after_norm`
   is large, find the field (add it to the census) and argue whether the walk reads it before normalising it.
3. Make the winner DEFAULT ON (EnvOn("MTG_UOT_XMEMO", true)), rewrite its code comment as a shipped exact cut (the key:
   BuildSimKey depth 0 exact mode + FsOrderSig + floating pools + g_line_unpaid_cost + context contents + whole
   Actions under WalkSame; bypassed under the price, human play and a live plan context), drop the SPS site if it
   does not pay. Commit, wait 200 s, build in the lane.
4. Identity slice under the box lock (one pooled `mtg --batch`, nproc threads, MTG_PGO=0, MTG_RSS_CAP_GB=40): every
   suite deck (`test/regression_cases.sh` DECK_FILE/DECK_PROF) ~300 games at play settings plus Jumbo 2000, arm memo on
   vs `MTG_UOT_XMEMO=0` (env is process-wide, so two processes run sequentially under the lock). Must be
   byte-identical per game (turn, digest, units). Then `bash test/regression.sh --both` under the lock (expect no GT
   change; it is an exact cut, so nothing to accept), and `python3 test/check_gt_logs.py`.
5. Add a doc section to `docs/design/jumbo-apply-drop-order-2026-11-18.md` ("Exact cuts, 2026-11-21/22"): memo revert,
   replay refuted, perf split, census, xmemo numbers. Commit; push wip/jumbo-uo-budget.
6. Report to the coordinator: lever ON vs off stays the quality gain; the price variants all fail the bar (net +4..+8
   vs ON, the loss is the budget being re-spent at early decisions; USER decides whether any trade-off is wanted);
   the exact cut's instruction saving (lever overhead +15.0% -> ~+11.4% on Jumbo). Further exact-cut leads: (a) a
   general cross-enumeration memo of EnumeratePlans results (58-69% of payability probes repeat inside a decision on
   the same board, all EnumeratePlans->EnumeratePlans: the enumeration itself re-runs; the plan cache m1-enum barely
   hits), lever-independent and probably the larger win; (b) a sound supply bound crediting unlocks.
