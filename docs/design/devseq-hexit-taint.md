# MTG_DEV_SEQ_HEXIT_TAINT: a first-pass horizon exit is a read of the shallower passes (2026-11-20)

Soundness fix for `MTG_BP_DEV_SEQ`'s two passes against the depth-free in-window cache (`MTG_INWIN_CACHE`), the
game-line cache (`MTG_GAME_LINE_CACHE`) and the first-pass memo carry. Lever `MTG_DEV_SEQ_HEXIT_TAINT`, heurarm slot
`DEV_SEQ_HEXIT_TAINT`, **default ON**; `=0` restores the hole. Code: `FsHexit` / `DvsHexitTaintOn` /
`g_devseq_pass_lmt` in `src/ai/TurnSolver.cpp`; unit test "deviation sequences: a first-pass horizon exit past a
latent turn is dirty" in `test/unit/test_bp_dev_seq.cpp`.

## The symptom

Accomplished Alchemist, seed 12345681 game 0, keep 7, `--depth 8 --budget-ms 0`, heuristic off
(`MTG_AA_LOOP_CAST_DRAW=0`): T4 at every budget, T3 with `MTG_INWIN_CACHE=0`. With `MTG_INWIN_CACHE_VERIFY=1`:
`[inwin-cache] DISAGREE turn=2 depth=2 cutoff=3 cached=11 fresh=3` -- the cache answered "no win by T3" for a T2 state
whose full tree wins T3 (a two-deviation line: two different site-9 continuations at two Crab Umbra re-opens).

## The mechanism

`MTG_BP_DEV_SEQ` searches a decision twice: pass 1 with compound (multi-deviation) waves suppressed, then, if pass 1
met a LATENT occurrence (a site-9 re-open a variant reaches only by targeting it), pass 2 on the full tree for a
strictly earlier win. Pass 1's answers are one-deviation answers. A pass-1 node is "dirty" when its answer depends on
a latent occurrence (`g_devseq_latent_n` moved under it): its own walk met one, or it read a dirty node's memo entry
(`MTG_DEV_SEQ_DIRTY_READ`, `LocalFslMemoHit`, gamelc `SupTag`, the searched-M2 memo). Dirty nodes are dropped from the
carry into pass 2, filed only under `gamelc::SupTag`, and never filed in the in-window cache.

One dependency was not tracked: the FIRST-VERIFIED-WIN HORIZON EXIT (`MTG_FS_HORIZON_EXIT`). A node stops at the
first win at its horizon edge because, under the iterative-deepening ladder, every SHALLOWER pass already refuted
every earlier win. In pass 1 those refutations are one-deviation refutations. The trace (`MTG_DVS_TRACE=1`):

```
[dvs] latent9m T3 ... sup=1 pend=1          (x144, ladder pass 3: the T3 Umbra re-opens)
[search] T1 pass=3 done win=11 cost=2988
[dvs] iw-widen T2 depth=3 cutoff=11 b=3 (was 2) ... sup=1 dlat=0     <- ladder pass 4, horizon exit on a T4 win
[search] T1 pass=4 done win=4 cost=4
[dvs] iw-hit T2 depth=2 cutoff=3 bound=3 ... sup=0                   <- pass 2 reads the one-deviation proof
```

Pass 4's T2 node exited on its first T4 win in 4 units, met no latent occurrence itself, so it was clean, and the
in-window store widened its "no win before T4" into a depth-free full-tree proof. Pass 2 read it at T2 depth 2 and
never walked the two-deviation T3 kill. The same clean node was also carried into pass 2 and filed in the game-line
cache under the full-tree tag: all three consumers shared the one missing dependency, so one fix covers all three.

## The fix

`FsHexit(state, win_turn)` replaces every `++g_fs_hexits` (nine exit sites in FSLineWin / FSLineTail). In a suppressed
pass, when the pass has met a latent occurrence at a turn BEFORE `win_turn` and the win lies past the node's own turn,
the exit counts as a latent dependency: `++g_devseq_latent_n` (the node and its ancestors are dirty) and the node's
earliest latent turn becomes that turn (so `MTG_DEV_SEQ_TAIL_SKIP` still erases it when pass 2's cut covers it).
`g_devseq_pass_lmt` is the earliest latent turn over the whole suppressed pass (every latent record folds into it;
`DvsPassLmtScope` resets it at each suppressed-pass entry); the per-node `g_devseq_latent_min_turn` cannot serve,
because `DvsMltFrame` resets it at every node and the premise is about OTHER nodes (the shallower passes).

Soundness argument: a pass-1 node's answer is a full-tree answer when nothing it rests on differs between the one- and
the multi-deviation trees. It rests on (1) its own walk, (2) memo answers it read, (3) through a horizon exit, the
shallower passes' refutations of every win before `win_turn`. (1) and (2) were tracked; (3) is now tracked
conservatively: any latent occurrence the pass met before `win_turn` might have hidden an earlier win in those
refutations. A latent occurrence at turn t only opens compound lines that win at t or later, so one at or after
`win_turn` cannot, and a win in the node's own turn needs no premise. Nothing is refused: a tainted node is re-walked by
pass 2 (or filed first-pass-only), so the lever only moves work from a reuse to a walk.

Alternative considered: give pass-1 entries their own namespace in the in-window cache (readable by first passes
only). Rejected as insufficient: the stored proof itself is mis-labelled, so a LATER decision's first pass reads it as
clean, never marks itself dirty, may skip its pass 2 entirely, and commits the one-deviation answer. The dependency has
to be recorded where it is taken.

## Measured

(filled in below as the runs land)
