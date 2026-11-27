# Control mode, stage 3: what the search must change to optimise E

Status: DESIGN (2026-11-27). Read-only audit of the search on branch `feature/control-mode` (worktree
`.claude/worktrees/control-mode`, HEAD `73694c1d0`, the prototype commit on top of `1c5ab04be`). Nothing here is built.
This is the stage-3 companion to `docs/design/control-scoring-mode.md` (the "parent doc"). Section numbers like
"parent 4.5" point into that document.

Line numbers are for `src/ai/TurnSolver.cpp` (TS), `src/ai/AIEngine.cpp` (AE), `src/core/GameEngine.cpp` (GE),
`src/core/FreeTurns.h` (FT) and `src/ai/TranspositionTable.h` (TT) at that HEAD. Seven parallel read-only audits
produced them, and spot checks were within two lines. They drift with every commit, so re-grep the quoted name before
editing.

**Summary.**
* **Scale.** Section 2 cites about 600 distinct lines, grouped into about 100 mechanisms (85 table rows plus about
  15 grouped families). Most are small re-statements of one integer comparison. The cost of stage 3 is in roughly ten mechanisms, listed in section 2.13.
* **Five facts settle almost every site** (section 1.2):
  1. A win in the current round is still unbeatable, so every "won this turn, stop" shortcut stays sound. That is
     about 60 sites.
  2. "No win by round X" proofs stay TRUE, but they no longer justify a cut by themselves.
  3. A subtree's best line depends on the effect HISTORY of the path into it, so every win-line memo needs the
     history in its key.
  4. `max_turns + 1` currently means two things, "refused" and "never wins". Under E they must be split, because an
     unwon line that cast control effects has a real, finite score that can beat the incumbent.
  5. Every restated test collapses to the integer test when the history is empty and no control effect is reachable.
     That gives stage 3 an exact IDENTITY TEST: a goldfish deck run in control mode with an empty mapping must
     reproduce its goldfish digests byte for byte.
* **The hardest sites** (section 2.13):
  * the integer `cutoff` threaded through every node, which becomes (E*, history);
  * the first-verified-win exits and every iterative-deepening stop, which need a new quantity: the minimum LB over
    the pass's horizon frontier;
  * the bound-qualified no-win memos and their stores;
  * the in-window cache and LEAFLESS_READS_LEAFED, which must be off;
  * the kill-only family, which becomes a greedy pick under E if left on;
  * the provider-proof gates (last-ply fold, winless certificate, edge no-reach, second-main-unproductive), which
    need a new provider fact, "no control effect castable here";
  * a prerequisite outside the search: **control cards are not castable at all in goldfish.**
    `CastableOnItsOwn` (TS 44775), the target gates (TS 45188-45290) and several provider `NeverCast` doctrines
    remove them before the search sees them.
* **The node payload is small** (section 3). A node carries a 4-byte handle into a hash-consed history trie, placed
  in existing padding of `GameState`. Each trie node holds the aggregate score curve `C_H[W]` (at most 16 int64
  values). LB is `C_H[t]` and E is `C_H[W]`, both O(1). The per-game work (about 1,260 games) happens only when a
  NEW history is created.
* **One finding contradicts the parent doc** (Q-A). Parent 4.5 says "only a verified incumbent cuts". Goldfish B&B
  today cuts against LEAF-ESTIMATED incumbents (`FsChildCut`, TS 120854, takes `best.win_turn` including value-leaf
  and rollout values). That is exact for the search's own leaf-valued objective and keeps the completeness
  invariant. Verified-only would make control mode cut far less than goldfish, and it would bias the USER's Q4 cost
  decision. Default: leaf-valued, as goldfish does. Both arms are measured.

## 1. The framework every row is classified against

### 1.1 Notation

* `H` = the effect history of a path: control events `e_k = (key_k, slot_k, T_k)`, `k = 1..m`, in resolution order.
  `T_k` = `state.turn_number` at resolution (section 3.6 shows this matches parent 4.6 on the play and on the draw).
* Mapping (parent 7.5): for each gauntlet game `(g, j)` (G decks x N games each, N equal for all decks, at least 20),
  the joint delay `D_k(g,j)` of the prefix `1..k`.
* Per game: `f_{H,gj}(W) = min(W, min_k max(T_k, W - D_k(g,j)))`.
* Curve: `C_H[W] = sum over (g,j) of f_{H,gj}(W)`, in milli-rounds, int64. Then `E = C_H[W] / (G * N * 1000)`.
  Comparisons use `C` directly, so they are exact integers (section 3.4).
* `LB_H(t) = C_H[t]`. `E*` = the incumbent's C.
* `Z(state)` = "a control effect is REACHABLE from here": some card in hand, library, graveyard or on the battlefield
  (an activation) maps to a control key (section 4.3). In a control deck `Z` is true for most of the game. It is
  false once every control card is spent.

### 1.2 Five facts (proofs in parent 4.3 and 4.5, restated for the code)

* **F1 (monotone, never better than winning).** `f` is non-decreasing in W and `f(W) <= W`. Appending an event never
  raises `f` for a fixed W. So `C_H` is non-decreasing in W, and `C_{H+e}[W] <= C_H[W]`.
* **F2 (the bound).** Every future event has `T >= t`, and every future kill has `W >= t`. Appending `e` with
  `T >= t` leaves `C[t]` unchanged, because `max(T, t - D) >= t >= f(t)`. So every continuation of a node at round t
  scores at least `LB_H(t)`. This holds for value-leaf and rollout estimates too, because the leaf clamps W to at
  least t (TS 125062).
* **F3 (win now is dominant).** A kill at the current round t scores exactly `LB_H(t)`, and so does every
  same-round alternative's best case. So every "a plan won this turn, stop scanning siblings" site is sound as it
  is, provided it tests a win at the CURRENT round. The code's tests are `OpponentHasLost` after the apply, or
  `best.win_turn <= state.turn_number`, and both are that.
* **F4 (history dependence).** Two equal game states reached with different H have different objectives, and their
  argmin continuation can differ. A memo storing a best LINE or a best value from a state is sound only with H in
  its key. A memo storing a history-free FACT about our game ("no line kills by round b"; "this deterministic
  playout wins on W and casts schedule S") is sound with a state-only key, but its READER must apply the restated
  test.
* **F5 (reduction).** With `H` empty and `Z` false, `C_H[W] = 1000 * G * N * W`, `LB = t`, and every restated test
  in section 2 reduces to its integer form. With `H` non-empty and `Z` false, E is a fixed non-decreasing function of
  W. The integer machinery is then valid with `cutoff = w_cut(H, E*) = max{W : C_H[W] < E*}`. This is the lever that
  keeps most integer fast paths alive late in a game. Section 5 builds the identity test on it.

### 1.3 The sentinel split

Today `{max_turns + 1, {}}` is returned for six different things: refused at the cutoff, deduplicated or folded away,
budget overrun or abandon, a lost game (own death, deck-out, turn-sequence cap), a no-leaf constant, and a genuine
line that never wins. In goldfish they all score `max_turns + 1` and none can displace an incumbent. Under E:

* **refused, folded, overrun** must be "no candidate" (C = +inf), never adopted;
* **lost** must be scored worst-case, with its events NOT credited (a suicide line that cast a counter must not
  outrank a live line). Sites: TS 116050, 116054, 116325, 116412, 116455;
* **never wins in the window, or no-leaf constant** is a real, adoptable line scored `C_H[Hmax + 1]`, keeping its
  phases. Today TS 123238 throws such a line away (`sub.win_turn >= max_turns + 1 -> {max_turns + 1, {}}`), so it
  could never be committed. TS 126376 initialises `best` to an unadoptable sentinel.

`SearchLine` therefore needs a result kind (section 3.3). This touches every return in FSLineWin, FSLineTail and
SimulateToEnd. The rows below mark it **SENT**.

### 1.4 The incumbent: leaf-valued, as goldfish does (Q-A)

`FsChildCut` (TS 120854) returns `min(cutoff, best_win - 1)`. `best_win` is `best.win_turn`, which the plan loop
updates from `tail.win_turn` (TS 127227, 127667, 127947, 128093, 128193), and that includes value-leaf and rollout
estimates (TS 125060-125122). So goldfish's B&B cuts against estimated incumbents.

That is exact for the search's own objective, the minimum over lines of their leaf values. A refused line's every
continuation has a leaf value at least its LB (F2), so the argmin cannot change. At unlimited budget the leaves are
the same, so nothing findable is lost. That satisfies CLAUDE.md's completeness rule.

The parent doc's "only a verified incumbent cuts" is stricter than goldfish. Two problems with it:
* It contradicts the parent's own claim that "goldfish's rule is the special case, exactly as now".
* A control deck kills late (Enchanted Evening's goldfish W was 5.80 with every lock credit, and control horizons
  run to 12-15), so many passes verify nothing. A verified-only search would then run nearly cut-free.

Default: **leaf-valued incumbent**, with a verified-only arm kept only as a measurement for Q4 and deleted after the
USER rules. Per the "delete, don't lever" rule it must not survive as a default-off knob.

## 2. The soundness audit

Classes:
* **S**: sound as is.
* **R**: restate against LB/E* (the restatement is given).
* **K**: sound only with H in the key.
* **O**: off in control mode whenever `Z || !H.empty()` (cost given).
* **I**: a goldfish-inertness gate that control mode must lift.
* **D**: diagnostic only.

"Off" always means gated by section 5's dispatch so goldfish is untouched. "W-fact" means a history-free statement
about our game.

### 2.1 The integer cutoff and the refusals

| site | mechanism (flag, default) | class | restatement |
|---|---|---|---|
| TS 120513, 124960, 123076 | `int cutoff` in FSLineWin / FSLineTail signatures | R | Pass `int64 estar` (+ a strict/inclusive bit). H travels in the state (section 3.2). |
| TS 124966 | entry refusal `turn_number > cutoff` (`g_fs_cut_prunes`) | R | Refuse iff `C_H[turn] >= estar` (strict). Integer form when `!Z`: `turn > w_cut(H, estar)`. |
| TS 120821, 120848, 120864, 120854 | `FsStrictCutOn` (MTG_FS_STRICT_CUT ON), `_O1` (ON), `ProbeInclusiveCutOn` (ON), `FsChildCut` | R | Child bound = `min(estar, C(best))`. Strict refuses at `LB >= bound`, inclusive at `LB > bound`. The `best - 1` trick has no real-valued analogue. The probe's R calibration is goldfish-fitted (a cost note). |
| TS 120896 + call sites 123878, 123996, 124170, 124408 | `DeadTailFanSkip` (MTG_DEAD_TAIL_FAN_SKIP ON) | R | Skip iff `C_H[turn] >= estar`. |
| TS 120906 + 123893, 124034, 124185, 124431 | `BillDeadCrossing` (MTG_DEAD_CROSS_PRICE) | R | dead iff `C_{H'}[crossed round] >= estar`. A price, but the fan fold reads `dead`. |
| TS 121929; 123944, 124066, 124203, 124456 | MTG_DEAD_FAN_FOLD (ON) | S | Sound under the restated `dead`, because crossings share the pre-advance H. It holds only while every opponent-turn control window is listed in `FanPreAdvanceLive` (TS 121926). A new end-step control cast must join that list. |
| TS 36976-36978; 129355; producers 129310, 131665, 131846 | `g_fsl_root_cut` / `FslRootCut` | R | An int64 E bound. |
| TS 116046 | rollout abort `turn > cutoff_turn` | R | Stop at the loop head iff `C_{H + S_sofar}[turn] >= estar` (S_sofar = events the rollout has cast). A fixed integer computed at the call is not a bound, because the rollout's own events lower the curve. First build: run control-mode rollouts uncut, then add the LB stop as an optimisation (section 7, step 6). |

### 2.2 Incumbents, improvement tests and sentinels (SENT)

| site | mechanism | class | restatement |
|---|---|---|---|
| TS 126376, 129431, 134629, 134856 | `best.win_turn = max_turns + 1`, `committed_win`, `pass_best_win` initialisers | R + SENT | `best = none` (C = +inf). |
| TS 127227, 127667, 127947, 128093, 128193; 123922, 124047, 124191, 124356, 124435 | `tail.win_turn < best.win_turn` / `sub.win_turn < best.win_turn` | R | `C(tail) < C(best)`, where `C(tail) = C_{H_tail}[W_tail]` and H_tail is the history at the tail's end. Rank 0 keeps ties (S). |
| TS 127211, 127465, 127537, 127925, 128077, 128182 | default `tail{max_turns + 1, {}}` when a tail is skipped (DvsTailGate, edge-elided, refused) | SENT | Must be "none", never `C_H[Hmax + 1]`. |
| TS 123083, 124994, 116113, 116126, 116452 | overrun / Abandoned / decisionwork exits | SENT | none (truncated). |
| TS 116050, 116054, 116325, 116412, 116455 | losses reported as `max_turns + 1` | SENT | Worst case; events are not credited. |
| TS 123238 | FSLineTail: an unwon `sub` is discarded | R + SENT | Keep its phases and score `C_H[Hmax + 1]` (or its leaf value). Without this, a lock line with no in-window kill can never be committed. |
| TS 127539, 127573-127575 | dork-release alt cut and `alt_wins` `<=` / `<` (MTG_DORK_ATK_SEARCH) | R | The same comparisons on C. Release keeps winning ties. |
| TS 136641-136680 | SolveWithLookahead `better` chain: W, then InfWinTie (ON), then leaf tb, then `plan.value` | R | The primary key becomes C. Every tie-break stays as is, below exact C equality. A no-win line can now beat a win. |
| TS 130759-130786, 136690-136725, 130138, 130182, 130231, 131915, 132128, 132176, 132983 | ANYTIME rescues (MTG_ID_ANYTIME ON) | R | `C(attempt) < C(prev)`. A truncated subtree contributes "none", never its LB, so a partial pass stays pessimistic. |
| TS 127591, 128092, 128192 | freshmode admissibility drops a line with `win > max_turns` (MTG_FRESH_SPEND_AXIS) | R (Q-G) | Admit iff `C(line) < estar`, so a no-win lock line is admissible. |

### 2.3 Wins in the current round (F3): sound

Every site below tests a kill at `state.turn_number` (the round; extra turns share it), so the line scores
`LB_H(t)` and nothing beats it.

* **FSLineWin:** 127196-127209, 127401-127443, 127521-127535, 127916-127923, 128071, 128112-128118, 128138-128144,
  128181, 128206-128213, 128238, 128251, 128265.
* **Revelation generator stop:** 128219-128264. `best.win_turn <= state.turn_number` reads as `C(best) <= LB_H(t)`.
* **FSLineTail:** 123859, 123868, 123970, 123983, 124147, 124160, 124324, 124332, 123509-123522.
* **SolveWithLookahead:** 134433-134508 (MTG_ET_ROLLOUT_CHAIN_PROOF), 134570, 134586, 134954 / 135604
  (`NestedRoundWinStop`), 136296, 136337, 136375, 136826-137097, 137050.
* **Rollout:** 116068, 116336, 116345, 116419, 116425, 116431, 116229-116279.
* **Greedy and enumeration short-circuits:** `Solve` 66395-66410 (MTG_SOLVE_VERIFIED_WIN), 68010-68553
  (MTG_GREEDY_LETHAL_STOP, MTG_GREEDY_PUMP_LETHAL, MTG_SOLVE_ATTACK_ONLY_WIN); `LethalCutKillIsReal` /
  `BoardLethalAsIs` 65119-65150; go-off / sac-named kill / equip-chain 93405-93506; MTG_UNTAP_LOOP_KILL_FOLD 139907;
  MTG_KO_CROSS_ROUND_STOP 87010; EOT-hold no-twin-for-winners 992-1018, 108040.
* **Executor and labels:** `TrySecondMainStrandedKill` AE 3143-3170; the winless certificate's go-off seed TS
  125716-125825 and EDF combo-off 126071-126082 (win-now).

The one caveat is a store: a win-now line stored in a memo is optimal under EVERY history. It may keep a state-only
key, but the entry must store W and the line, never a C. Each reader recomputes C from its own H.

### 2.4 First-verified-win exits and iterative-deepening stops (the ID premise)

The goldfish premise: shallower passes refuted every earlier kill, so the first in-window win sits at the horizon edge
and is the minimum. Under E it fails twice. A later-W line, or one beyond the window, can have a lower C. And two edge
wins with different H differ in C.

Each pass must therefore return `frontier_lb`: the minimum of `C_{H_n}[turn_n]` over the horizon-frontier nodes n
the pass left unexpanded (+inf if exhaustive). The restated stop is `C(best) <= frontier_lb`. For an effect-free
deck, `frontier_lb >= 1000GN * (turn + d)` while `C(best) <= 1000GN * (turn + d - 1)`, so the test reduces to
today's (F5).

| site | mechanism (flag) | class | restatement |
|---|---|---|---|
| TS 127693-127713, 127250-127268, 123942, 124060, 124201, 124368, 124454, 123558; 120668 | MTG_FS_HORIZON_EXIT (ON) | R | Inside a node: exit iff `C(best) <= LB_H(this round)`, i.e. only at the F3 floor. Otherwise keep scanning. FsHexit taint (120693) and `inwin::ExitGuardOn` (124749/124757) become bookkeeping. |
| TS 127704, 127258, 127719-127729, 127961-127966 | MTG_BP_WAVE_COMPLETE deferral, wave-phase exit | R | Follows the exit. |
| TS 130887 | **main ladder verified stop** | R | `C(line) <= line.frontier_lb`. |
| TS 129868, 129879-129899, 129913, 129920 | emulated ladder (MTG_LADDER_EMULATED, OFF; per-deck) | R | Same stop. |
| TS 130109, 130198, 130248, 130386-130389, 130149 | p18 sampled gate (default OFF) | O | Turn the whole block off in control mode first. Its aspiration bounds (130165, 130213, 130256, 130320, 130590, 130713, 130744-130753, 130804) would all need E windows. |
| TS 129990-130009, 130647-130657 | lazy leaf (MTG_LAZY_LEAF ON at unlimited budget) and skip-leaves passes: "the first pass with a win holds the minimum" | O | Its frontier is scored by a constant leaf, which is pessimistic under E (`C_H[Hmax + 1]` is a real value), not a bound. Cost: unlimited-budget decisions pay the leafed ladder. Restate later as `C(probe) <= frontier_lb` if it is wanted back. |
| TS 129279-129330 | dev-seq two-pass (MTG_BP_DEV_SEQ ON): `cut2 = win1 - 1`, skip pass 2 when `cut2 < turn`, `p1_settled`, `take2 = win2 < win1` | R + K | Pass 2's bound is `C1`. "Skip when pass 1 won this round" is S (F3). `take2` compares C. `DevSeqDeferredP2::cut2 / win1` (129179-129181) become int64. `FslEraseDirty` (128981-129014) erases by W bounds, which is R. |
| TS 131879, 131654-131681, 131955, 132153, 133068 | hybrid `verified`, `held_verified`, `cd_finish`, `line_constant` | R | `verified` uses the restated stop. `line_constant` ("a leafless line is worth nothing") is false under E. The take rule (133069-133080) never compares lines. Under E, require `C(hline) <= C(line)` when both are verified. Its depth-crossover tables (131325-131330, 132291 `kValueTrustOffset`) are goldfish-calibrated, a cost issue only. |
| TS 131533-131541 | `ExtraTurnChainTrial` (MTG_ET_CHAIN_WALK ON, provider-gated) | R | It returns without searching. Commit only if its C <= the search's bound; otherwise off. Extra-turn decks only. |
| TS 131809-131854 | MTG_ET_D0_BOUND (ON, unlimited, no model) | R | The incumbent needs the rollout's events. Its root cut `W - 1` (131846) becomes C. |
| TS 133196-133673 | EnumerateEarliestWins / label ladder (MTG_LABEL_LADDER ON, MTG_LABEL_GOFF, earliest-only) | S as a W-labeller; O for E labels | It produces value-leaf labels. These stay W labels (Q-C). |
| TS 133842-133939 | `ReshuffleAvgChoosePlan` (MTG_NC_SEARCH OFF) | R | Sum C per sample; the tempo bonus is in the same units. Off by default. |

### 2.5 Memos and caches

The general rule:
* **Win entries (a stored best line):** **K**. Fold `H.hash` (section 3.2) into the WIN key only. This mirrors the
  existing split-key design (TS 125178-125200): win keys order-exact, no-win keys canonical.
* **No-win entries:** keep state-only keys and store a **W-fact** `b`: no continuation kills at `W <= b`. The
  reader applies the restated test.

**Derivation of b for a no-win store (the subtle part).** Today's bound is "the cutoff the node ran under", which is
true only because every child ran at the node's cutoff (FSLineEntry comment, TS 117417). Under E, children are
refused by `LB >= estar`, which depends on H and the incumbent. So a refused child at round `t' <= cutoff` can hold a
kill at t'. The sound W-fact from a node searched under `(H, estar)` that found nothing better is
`b = max{W : C_H[W] < estar}`. A kill at `W <= b` would score at most `C_H[W] < estar` (F1, because added events only
lower C), so it would have been adopted.

**The restated no-win read** (agent X3): with the reader's `(H', estar')`, refuse iff

    LBnw = sum over (g,j) of min( f_{H',gj}(b+1), max(t, b+1 - Dmax_{gj}) )  >=  estar'

`Dmax` is the largest delay any still-reachable event could add, and `Dmax = inf` collapses LBnw to `C_{H'}[t]`. In
practice:
* `!Z`: LBnw = `C_{H'}[b+1]`, the full integer power;
* `Z`: LBnw = `C_{H'}[t]`, which equals the entry refusal and is no stronger.

So no-win memos keep their value exactly where `Z` is false, and no finer `Dmax` is planned for the first build.

| site | mechanism (flag) | class | restatement / cost |
|---|---|---|---|
| TS 125218-125221; key builders 114083 `BuildSimKeyBase`, 115065 `SimKeyAtDepth`, 114037 `FsOrderSig` | FSLineCache key / key_win | K (win) | Fold `H.hash` into `key_win` only when `!H.empty()`. |
| TS 125366-125384, 128593-128596, exit stores 127259, 127705, 127724, 127964; def `FSLineStoreWin` 119153 | per-decision FSLineCache win read/store (MTG_FSL_SPLIT_KEYS ON) | K | As above. |
| TS 125385-125394, 128604-128616; def `FSLineStoreNoWin` 119197 | no-win read `cutoff <= nowin_bound` / store `min(cutoff, max + 1)` (MTG_FS_NOWIN_CACHE ON) | R | Store b as derived above. Read with LBnw. |
| TS 125398-125406, 128617-128622 | MTG_PROVISIONAL_NOWIN (ON, only once the budget is spent) | R | Same as no-win. It is budget re-pricing, inert at unlimited budget, so it is allowed. |
| TS 124582-124631 `gamelc::Tag`; reads 125243-125286; stores 128387-128499 | game-line cache (MTG_GAME_LINE_CACHE ON, per game) | K (win) + R (no-win) | The tag folds `H.hash`. H must be ABSOLUTE (including the real game's events), because the cache outlives decisions. P18f leaf-free (125243, 124662, 128478; OFF) is R. |
| TS 125292-125323, 124646 | MTG_LEAFLESS_READS_LEAFED (ON) | O | Its "w > we means no in-window win" inference (125309) is false under E: the leafed entry holds the C-best line, not the minimum-W line. Cost: cold re-searches on leafless decks (Hydras: 576k vs 15k nodes on one pass). The no-win half (125315) is R. |
| TS 124735-124797, 125331-125361, 128502-128592 | in-window cache (MTG_INWIN_CACHE ON, depth-free) | O (win), R (no-win) | "A deeper window cannot beat an in-window win" is false under E. Serve a win only if it is win-now (F3). About 0.5% of units. |
| TS 125413-125456; 119011 `FSOfHintKey`, 119117 `FSLineStoreOfHint` | order-free wave (MTG_FSL_OF_WAVE ON, share 0.0005) | O | It returns a bare W with no events and no H. Cost: about 0.916x work for no measured quality. |
| TS 125458-125569 | non-split path (only with SPLIT_KEYS=0) | O | Off by default anyway. |
| TS 125579-125627, 126764-126787, 122982-123040, DvsTailGate 127214, 127486, 127540, 127928, 128080, 128185 | dev-seq dirty records and tail skip (MTG_DEV_SEQ_TAIL_SKIP ON): "loosest cutoff answers tighter", `Skip(cut)` | R + K | Record the W-fact b and fold the per-turn sig into `ck`. The latent-turn test `mlt > cut` becomes rounds vs LB. |
| TS 116854-117387; TT 14-224 | rollout TT: win `return *cached` (116861), no-win `cutoff <= e->bound` (116943), xmmemo (MTG_EXH_LEAF_MEMO ON; 117054, 117359), rollout game cache rgc (MTG_ROLLOUT_GAME_CACHE ON, game-persistent; 117100-117160) | S for W + store S; R for no-win | With a history-blind playout policy (section 6.5), `(W, schedule S)` is a function of the state. Keys stay; entries store S out of line (section 5.2). No-win entries store S up to `bound`, and the reader checks `C_{H + S_b}[bound + 1] >= estar`. `full_run` (117028) must mean "reached the natural horizon exit". rgc stores each boundary's SUFFIX of S under that boundary's key (117151). |
| TS 138494-138607, 138709-138800, 64735 | m2 search memo (ON), rollout m1 memo (MTG_ROLLOUT_M1_MEMO ON), solvememo | S under a history-blind policy; K otherwise | They memoise policy choices. If the policy ever reads H (Q-B), fold the post-root history. |
| TS 139129, 141078 | bp-enum cache | S | Plan lists do not depend on the objective. |
| TS 126160-126210, IdHintRecord 127260, 127706, 127725, 127733 | MTG_ID_ORDER_HINT (ON), MTG_ID_HINT_ROOT=3 | S (ordering) | Mode 2's `hit->win` comparison (126493) is R. Record the C-best. |

### 2.6 Folds and dedups by post-state equality: K on the per-turn signature

Siblings of one node share H, and every new event has `T = t`. So the only thing missing from these keys is the
**per-turn signature** `sig_t`: the ordered events cast since the node's root.

Under goldfish such siblings really do reach equal states with different effects. A modal spell, a counter with no
target in our game, a removal aimed at the gauntlet, and an attack-trigger bounce all leave no footprint on our board.
**One change fixes the whole family:** fold `sig_t` into `BuildDedupKey` when it is non-empty. Goldfish keys are
untouched.

* **FSLineWin:** put-bounce 126638-126676 (MTG_PUT_BOUNCE_FOLD ON); combat-pin 127448-127462 (MTG_COMBAT_PIN_FOLD
  ON); putback-xbase 126744-126751, 127047-127078 (MTG_PB_XBASE_FOLD ON); node_prefix_seen 127045; node-dupe
  127170-127190; post-apply-dupe 127279-127344; wave/tranche/revwave dedups 127826-127883, 128068, 128178;
  LandTwinDup / `bp_twins.Close` 127222, 127579, 127936, 128088; EOT closure 126406-126417 (unbudgeted / label
  scope).
* **FSLineTail:** `LandTwinDup` / `m2_seen_states` 123957, 124068; node prefix/child sets 123706, 123833, 123952,
  124133; `EotStateSet` 120926 (MTG_FSW_EOT_DEDUP, unbudgeted).
* **SolveWithLookahead:** `CandDedupActive` 136251; `LandTwinDup` / `bp_seen_states` / `LaWaveDup` 136276, 136820,
  136849, 136916, 136935; `pin_fold_seen` 136309; cp_cache 134700-134850 (S: an exact state copy carries the
  handle).

**Same-round order (agent X5).** Two events in one round have equal T but different `max(t, W - D)` terms, so prefix
order inside a round changes f. Make the order canonical by construction: events carry a `slot` (our turn, or the
opponent's turn of that round), and within a (round, slot) the mapping treats them as a SET. Sort by key in the trie
(section 3.2). Then `sig_t` is order-free and these folds are sound. The cost is that the mapping cannot tell
"Wave then Opalescence" from "Opalescence then Wave" in the same main phase. Parent 4.3a's lock needs them on
different rounds anyway. The gauntlet replay must apply same-slot events in the same canonical order.

### 2.7 Folds by action identity: sound

These skip a candidate because it REPLAYS THE SAME RESOLVED ACTIONS as one already scored, so the events are
identical. The proviso is that events are recorded from the resolved apply, never from `Plan::actions`. Without it,
a stranded or unpayable control spell would be credited.

* stillborn 126552-126583 (MTG_BP_NEW_ONLY ON)
* w0-collapse 126591-126611 (OFF)
* bounce-fold 126619-126629
* etb-bounce 126684-126719
* dig-fold 126725-126731
* putback-fold 126734-126739. Its F4 "strand" arm is S only under the proviso above.
* etbc-fold 126811-126841
* EMPTY pre-skip 127110
* node put-back fold 127124
* wave retire / prefix cache 127794-127817
* NSKIP length records 127008-127026
* main-2 stillborn 123597-123621
* the SolveWithLookahead twins 135398-135930: W0 NOBP / UNIF / chain / PO_W0, bounce / dig / put-back / ETB /
  Summit / etbc / cp-share
* enumeration identity folds 963-48125 (copy/host classes, put-back, ETBC, cycle seeds), as listed by the enumeration
  audit

### 2.8 Provider-proof gates: need a new provider fact

Each of these pairs a provider claim ("no kill here", "this main 2 does nothing") with the integer edge. Under E each
silently drops control lines in the window, so each becomes a greedy pick of "do nothing" there.

The fix: add `DecisionProvider::NoControlCastable(state, scope)` (scope = this turn / post-combat / this upkeep axis),
computed from section 4.3's control-key table (no hand list). Each gate fires only if its old proof holds AND
(`NoControlCastable` OR the LB test). Without the new fact, the gate is O.

| site | gate (flag ON unless noted) | restated condition |
|---|---|---|
| TS 125127-125171, 124878 | MTG_LASTPLY_NOWIN_FOLD + `ProvenWinlessThisTurn` (DecisionProvider.h:1721/1747; Beasts, Gods, Snow, Fungus, Maze's End, Eldrazi Flicker, STUCK cert TS 53528) | proven winless AND (`NoControlCastable(turn)` OR `C_H[Hmax + 1] >= estar`). Its answer is the REAL value `C_H[Hmax + 1]`, not a refusal (SENT). It is an identity fold, so turning it off costs work only (Maze's End: 96.5% of breakpoint-wave applies sit at the last ply). |
| TS 125644 / 118704, 123169 | `WinlessCertificateActive` M1 / M2 (MTG_WINLESS_CERT; label scope, turn >= cutoff) | S while label search stays a W question. If E ever reaches it: winless AND no control castable AND `C_H[t + 1] >= estar`. |
| TS 123156 -> 115318-115345 | MTG_FSL_EDGE_NO_REACH | provider proves no post-combat kill AND `NoControlCastable(post-combat)` AND `C_H[t + 1] >= estar`. |
| TS 8425, 115305-115316 | MTG_KO_M2_NO_REACH + `PostCombatCannotKillOptIn` (Beasts, Vampires) | Same extension of the vouch. |
| TS 123195-123247, 123272; 7660-7680 | MTG_M2_EMPTY_FAST / `SecondMainUnproductive`, deferred-cast gate, MTG_M2_SKIP_EMPTY_APPLY | "Unproductive" must also mean "no castable or activatable control effect". A post-combat removal activation is exactly what is dropped today. |
| TS 86633 (uses 121011-121427) | `EchoEdgeFoldHere` (constant leaf cannot tell pins apart) | Fold only for upkeep/draw axes that cannot create an event before the leaf. A Defense of the Heart put of an ETB-removal creature, an upkeep tutor that casts, or a Soulstoke put can. |
| AE 4986-4996, 5066; GE 217 | PROVEN NO-WIN -> finish the game with `Plan{}` (unlimited budget) | O. Under E the remaining turns can still lower E. The empty plan is an unsearched pick for the rest of the game. |
| TS 125831-125848, 126437-126448, 127995 | MTG_LABEL_GOFF_DOM (label-only) | O if label search ever scores E. Inactive by default. |

### 2.9 The kill-only family: off when an event is reachable

MTG_LEAF_KILL_ONLY (ON), `_EXTRA` (ON), `_NOM2` (provider opt-in, Cyclone): TS 115198-115299, 116148-116159,
134507, 134649, 134833-134849, 136542, 136857, 136937-136939.

The premise is "on the cutoff turn only a kill this turn can change the return". Under E an event cast that turn
changes E for every later W.

Worse, with no kill the nested solve returns `candidates.front()` unscored (TS 134657, break at 134849). Today the
enclosing rollout reads only "did it kill", so that is harmless. Under E the enclosing rollout reads the plan's
events, so this becomes a value-ranked pick: a greedy choice inside a search scope.

Restatement: arm only if `!Z` (or the candidate casts no event this turn), or
`sum min(f_{H + S}(X + 1), X) >= estar`, which is rarely true. **O when `Z`.** Cost: Vampires measured 72% of the
1-ply leaf's units in slack-0 rollouts. The verify arm (`MTG_LEAF_KILL_ONLY_VERIFY`, 115232, 115419-115640) is D.

### 2.10 Enumeration-level drops (both lockstep twins together)

| site | mechanism | class | restatement |
|---|---|---|---|
| TS 92632-92930 and twin 67544-67800 | lethal-exempt waste/hold drops: `iren_waste`, `XCastBelowMax` (S), `HoldsSacLandBurn`, `HoldCastUnlessLethal` (MTG_HINATA_HOLD_LETHAL_BURN), `SubsetSpendsUnneededSacForMana` | R | `keep_lethal` becomes "lethal OR casts an event its kept twin does not". Change both sites in one commit, or the leaf scores plans the enumerator never offers. |
| TS 18763-18860, 63581-63622, 66231, 66800-66815, 90046, 91133-91143 | payoff prunes: `PrunesAcceleratorWithoutPayoff` (Dragonstorm, by payoff ROLE), `PrunesRitualWithoutSpend`, `MTG_UNPRUNED(payoffprune)` | R / S | The role form treats a ritual that funds a counter as "no payoff" (R: a control key counts as a payoff). The spend form is S. |
| TS 95473-95482; HeuristicArm.h 274, 301, 352 | MTG_PD_QUAKE_NO_OVERKILL / MTG_PD_PYRO_NO_OVERKILL / `_TWO_TURN` | R | A larger sweeper X that costs our creature can hit a bigger gauntlet creature (D grows with X). Keep X variants that change the event's key. |
| TS 18700-18760 | Reverent Silence "bricked unless lethal" | R | Keep the subset when it casts the wipe as an event. |
| TS 109900-109960, 111236, 25205, 29857, 65176, 10515-10612, 41999-42092 | cast dominance (superset adds casts), `NoLandPlanDominated`, equip/aura Pareto folds, sac-fodder / equip inert folds | S | The dropped side holds no control card, or the superset only adds events. |
| Dominance.h (consumers 117897+; TS 126401, 136562, 123230, 124025, 124341, 124426, 124485) | MTG_DOM_PRUNE / MTG_DOM_CENSUS (both OFF) | O | Resource dominance reads "cast a counter" as "fewer resources". If ever enabled, H must be an exact-match (or per-game dominance) field of `DomSnap`. |

### 2.11 Goldfish-inertness gates: control mode must lift these (a prerequisite, not a search change)

The search cannot optimise E over casts it is never offered. These gates remove control casts BEFORE enumeration,
because the passive opponent gives them no target (CR 601.2c):

| site | gate | cards |
|---|---|---|
| TS 44775 (`CastableOnItsOwn`, SpellEffects.h:62831), probes 17702, 17776, 69435, 49317 | skips `goldfish_inert`, `counters_target_spell_to_hand` (Remand), `counters_target_spell_to_graveyard` | every counter and every `goldfish_inert` card. The probes must agree or the dig re-solve never fires for a drawn counter. |
| TS 58069-58300, 80260; 58187-58200, DecisionProvider.h:1575, DecisionProviders.cpp:40916, ValueArm.h:186 | Remand only as a rider on our own cast; Remand-only inert bases (MTG_CYCLONE_REMAND_INERT OFF) | Remand, Memory Lapse; Toxic Deluge offered only as Remand fodder |
| TS 45188-45262, 45199-45204, 45273-45290, 45665-45700, 48779-48820, 51167 | targeting gates: no opponent creature or permanent means skip; Swords needs a Remedy enabler; tuck removal (USER doctrine, X=0); Path; Garth's Terror stub | removal, bounce, Swords, Unexpectedly Absent, Path to Exile |
| TS 51904-51930 | MTG_SKIP_INERT_LIFEGAIN (ON) | life gain is a control effect in this mode (parent 5.6, USER 2026-11-27), so this is I |
| TS 6315-6350; 80120-80133, 104170-104215 | opponent-bounce trigger axis counts only our permanents; Ulamog's exile axis ranks only our board | tempo bounce, "exile target permanent" as removal or land destruction |
| DecisionProviders.h `NeverCast` and discard/cycle tiers: Equipment 2675, Snow 3370, Colossus 1254 (MTG_COLOSSUS_NO_PATH), Sea Creatures 808, Mill 511, Blood Sun 4542, Maze's End 5487; .cpp 18418, 35754, 39719, 41162, 41404, 25122 | USER goldfish doctrines ("not relevant for goldfishing"); an inert card is dead, so discard or cycle it first | USER-owned per deck (Q-F) |
| ManaPayment.cpp:9683, EquivalenceDiscovery.cpp:161-171 | inert cards skipped in mana demand; folded as interchangeable for mulligan buckets | pointers for the stage that builds the virtual cast |

**What control mode needs instead: a VIRTUAL-TARGET control cast.** In control mode a card with a control key is
castable against the gauntlet abstractly:
* our game pays its cost, the card goes to its destination zone, and Remand draws;
* the event is recorded;
* the passive opponent's state is NOT touched.

That last point avoids double credit: the opponent's spawns are goldfish scaffolding, and the gauntlet effect lives in
the mapping. Counters and flash removal also need a searched window on the opponent's turn: a BoundaryFan axis "cast
<card> in the opponent's turn of round t", in the shape of the end-step window (TS 86941-86966,
`PerformOpponentEndStepFaceUp`), with "cast nothing" always one option. Parent 5.7's default gates when the cast is
OFFERED: our game casts the counter when most of the gauntlet's games cast a spell that turn, which the mapping
reports per round. That is a provider restriction, which is allowed.

**Cross-stage dependency:** stage 2's schedule recorder is useless for counter and removal decks until these casts
exist, because the goldfish search never casts the cards.

### 2.12 Free turns, executor sites and the remainder

| site | mechanism | class | note |
|---|---|---|---|
| FT 185 `NoteOpponentTurnEnd` -> `pending_free_turn`; GameState.h:866-877 `AdvanceTurnCounter`; FT 64 `RuleArmed` | the free-turn lock credit | O | Parent 4.5: the mapping replaces it. A lock must not be credited twice. Make `RuleArmed` false in control mode, and stamp `deck_has_free_turn` false too, or `NextTurnMayShareRound` (FT 265) stands edge prunes down for nothing. |
| FT 70/92 spawn-land rule, FT 457 hurdles, FT 164 Sire fairness | the passive opponent's model in OUR game | S | Not the objective. |
| TS 123071 `DepthAcrossTurn` | depth counts rounds | S | |
| TS 122266-122904 (`ChooseRealCrossingFetch`, Reanimate, FaceUp, Lands, Land, Horn, UpkeepLegend, ResolutionFetch/Land, AttackLand); AE 3327-3460 | executor choose-by-search nodes: `cut = best_wt - 1`, `wt < best_wt` | R | Compare C with the REAL game's H. |
| TS 134157-134251 (`BoundaryChooseByRollout`, `...CrossingLandsByRollout`); call sites 136546, 136858, 137200 | in-search pin choice (echo, Defense of the Heart, tutor, miracle, Grace, Emeria, dredge, Klothys, Keranos, face-up, horn, upkeep legend, crossing lands) | R | Pick by C, with E* as the cutoff. |
| TS 134937, 136580-136592, 135174, 136571-136603, 136867-136881, 136945-136958 | MTG_LA_TIE_PRUNE (ON), MTG_LA_TIE_TIGHTEN (ON), MTG_FB_HOLD_STRICT (ON), wave `score_gw` | R | Skip iff `C_{H_cand}[t + 1] >= C(best)`, where `H_cand` includes the candidate's own events this turn (it differs between candidates of one node). Tighten becomes the strict E bound. |
| TS 137146-137242 | MTG_CONDEMN_ESCALATE (ON): sort and `esc_eval` by W | R / O | It repairs `CondemnsPassedMainPhase`, which refutes "cannot win". Under E a condemned plan can score well, so that filter is O in control mode, and the escalation is then inert. |
| TS 4804-7279, 44834-44913, 98790-98870, 56803-56935, 76955-76990, 40226-40302 | breakpoint and main-phase CONDEMNATION family | S | Its premise is a permutation duplicate plus the declared cast order, not "no win". Same turn means same round, so T is identical, and main 1 vs main 2 does not matter to E (parent 4.1). The caveat is that providers' cast orders (USER-owned) must rank control cards. |
| AE 2073-2170 `BlindChooseDrawSpell`; TS 8221 `LastCommittedTieBreak` | blind chooser by (win, tb) | R | By C. |
| AE 2174-2260 `RolloutWinTurn(From)` / `PlayOutFrom`; AE 2495-2672, 2718-2880; AE 2452, 2640-2650, 2809-2815, GE 198-204 | bottoming (subset table, per step, top-K refine, blind-K), MTG_BOTTOM_TURN_CUT (ON) | R | Score masks by C from the trial's own events. The turn cut becomes an LB stop inside `PlayOutFrom` (a fixed stop round is unsound). `HeuristicBottomPick` stays legal as a tie-break among exact C ties (AE 2878). |
| AE 1336-1345, 2262-2411 | exhaustive keep/bottom tables, `RolloutKeepWinTurn` | O | They are W-fitted. Use the scored `.control.` table (parent Q6) or run tableless. |
| AE 12152-12262 | legacy rollout cleanup discard (MTG_SEARCHED_DISCARD, OFF behind MTG_DISCARD_NODE) | R if re-enabled | The default discard node pin inherits the search's E. |
| AE 4905, 5010-5040 | executor reads `line.win_turn`; verified commit-and-replay | S + R plumbing | The commit test stays on W ("verified" = a win in the window). A verified line's C is exact, because events after W do not count. A no-win line can now be the argmin: commit this turn and re-search. |
| AE 926-1060, 3940; AE 4820-4832 | eval/value-row label dumps; MTG_NC_SEARCH | O | W labels; relabel only for a scored leaf. |
| TS 3594-3624 `MoveOrderPlans`, 3534, 93833, 94395, 111721, 111670-111690; 125986, 123326 | move ordering (wins_this_turn first) | S | Ordering only. For budgeted quality, an order that brings control-casting plans forward is worth an A/B (section 6). |
| TS 8236-8260, 8330-8345, 116461-116470 | graded no-win leaf (MTG_LEAF_GRADE_NOWIN ON), MTG_INFLIFE_WINTIE (ON), MTG_NOWIN_STORE_UNGRADED (ON) | R | Tie-breaks below exact C equality. |
| TS 126020 | idle-pass insertion (`IdlePassMissing`, unconditional) | S | Must stay; section 6.2. |
| TS 126090-126118, 126506, 131620-131644, 132345 | escalation beam (`g_esc_beam_width`, MTG_ESC_BEAM; per-deck) | R | Already lossy. Rank by C. |
| TS 132296-132315, 132394-132433, 132737-132761 | MTG_ESCALATION_GATE (opt-in, goldfish-trained model) O; MTG_ESC_SKIP_PREDICT (ON) cost; MTG_ESC_SINGLE_BOUND modes 1-3 (default 0) R | | |
| TS 126228-126372 | post-apply dedup family armed per node | S given 2.6 | |
| GE 112-143, 178, 214 | `RunGame` returns `win_turn`; `OnGameEnd` | R | Also return the real game's history handle. The batch reporter prints W and the modelled E beside the stage-2 measured E. |
| AEh 181-191; GE 255-421, 1889-1900 | `CtrlInject*` hooks (the stage-1/2 measurement apparatus) | S | Must never be armed on a control deck's own game. |

**Diagnostic (D), no play effect:** the treereuse probe, p17/p18 ledgers, m2yield, MTG_PROFILE counters,
`g_fs_leaf_evals` / `g_fs_leaf_wt_sum`, MTG_FD_ORACLE (`g_vleaf_min_est`, AE `m_fd_best_win`), MTG_FLAG_NONCONV
(AE 1918-1950; under E a later win is not non-convergence, so update it to compare C), gamelc / inwin / leaf /
no-win VERIFY modes (`r_same` / `r_loose`), pastcut, the ofw and exh probes, lastply census, winless stats, dev-seq
stats and `[dvs]` traces, FswDeclinedTrace / FSW traces, tie / nil / residual counters, OrderOracleProbe, candcensus,
HybridStats, EscalationDump, the TS 143205-147520 CheckLine / PlanDump paths, the `*ForTest` hooks at TS
147843-147915, and the unit fixtures TS 129019-129156. The verify modes are worth KEEPING: each becomes a stage-3
soundness check once it compares (W, events) instead of W.

### 2.13 The hardest sites, ranked

1. **The cutoff threaded through every node and every rollout** (2.1). It is a signature change across FSLineWin,
   FSLineTail, SimulateToEnd and SolveWithLookahead, plus the int globals (`g_fsl_root_cut`, `cut2`, `p18_wb`,
   `single_cut`, `pass_cut`, `best_wt`). It is mechanical but touches hundreds of lines.
2. **The ID premise and `frontier_lb`** (2.4). This is a new quantity FSLineWin must compute and return. Every ladder
   stop, `verified`, the horizon exits and the lazy leaf all hang on it. Get it wrong and the search either stops
   early (loses lines) or never stops (budget).
3. **No-win memos** (2.5). The stores must write the W-fact b, not the E-mode cutoff, or they cache false facts that
   poison later nodes and decisions. Their power survives only where `!Z`.
4. **The sentinel split** (1.3). Every return in three functions, plus the adoption of no-win lines, which goldfish
   never had to commit.
5. **Kill-only and the provider-proof gates** (2.8, 2.9). Both default ON. Under E both become greedy picks inside
   the window unless gated on the new `NoControlCastable` fact.
6. **History in win keys and `sig_t` in dedup keys** (2.5, 2.6). Correct by construction, but they cut reuse, and
   the gamelc and inwin savings are the main cross-decision savings.
7. **The prerequisite: virtual-target control casts and the opponent-turn cast window** (2.11). Without them there is
   nothing to optimise.

## 3. Carrying partial scores at a node

### 3.1 What a node needs

Only two numbers are ever read: `LB_H(t) = C_H[t]` (every cut) and `C_H[W]` (every scored line). Both come from the
**aggregate curve** `C_H[1 .. Hmax + 1]`, at most 16 int64 values, because W is an integer round no larger than
`Hmax + 1 = 16`.

The per-gauntlet-deck partialness the USER asked for (parent Q4) is honoured INSIDE the curve: `min` and `max` are
applied per game before the sum, so deck 1 delayed three rounds and deck 2 not at all stay distinct. The curve is
exact for E and LB. The per-deck curves `P_g` are needed only for reports and the modelled-vs-measured gap flag, so
they are computed at game end, not carried.

The parent's per-deck histogram over (base kill round, delay) is unnecessary: `f` does not depend on the gauntlet
game's base kill round K (parent 4.2 holds for every K), only on the per-game delay vector.

### 3.2 Representation: a hash-consed history trie, a 4-byte handle per state

* **`CtlHistory` trie**, one arena per game (deterministic, reset at game end). A node holds:
  * `parent`, the `event` (key, slot, round), and `hash`: a content hash over the canonical prefix, used in keys.
    Never the node id;
  * the per-game **delay state** the mapping needs in order to extend (section 4.2). For the stage-1 table that is
    the per-game cumulative delay `D_m(g,j)`, int16 in milli-rounds with a LOCK sentinel: G x N x 2 bytes, about
    2.5 KB at 63 x 20;
  * the aggregate curve `C[16]` (128 B).
* **Per-game f is not stored.** Extending recomputes it by walking at most m ancestors:
  `f' = min(f, max(T, W - D'))`, at 16 points per game. Per-game Pareto pruning keeps only events whose D exceeds
  every earlier D, which keeps m effectively at 1-3.
* **Canonical same-slot order** (2.6): `Extend(node, e)` inserts `e` in key order among the events of its own
  (round, slot). The prefix hash is therefore order-free within a slot.
* **`GameState` carries `uint32_t ctl_hist`** (0 = empty) **in existing padding**, as the codebase already does for
  Plan fields (`gdb ptype /o`; TurnSolver.h 1499-1506). Copying a state copies the handle. The rollout, the executor
  and every trial copy inherit H by value, so H "travels down" with no new plumbing.
* **An intern map `(parent hash, event) -> node`** makes repeated extensions O(1). A new history costs one
  extension: `G * N * m * 16` (about 1,260 x 2 x 16, roughly 40k simple ops, ~20-40 us).
* **Memory:** about 2.7 KB per distinct history. Distinct histories per decision are bounded by the applies that cast
  an event. Default: free the per-decision part of the arena at each decision end and keep only the real game's
  spine. That bounds the arena by one decision's distinct histories. It must be measured in step 9 against
  `MTG_RSS_CAP_GB`: 10k histories is about 27 MB per worker.

### 3.3 `SearchLine` and the result

`SearchLine` (TurnSolver.h 4616) keeps `win_turn` (goldfish stays identical, and the verified tests keep their
meaning) and gains:
* `kind`: none / scored / lost;
* `int64 c`: the line's curve value;
* `int64 frontier_lb` (section 2.4);
* `uint32 hist_end`: the H at the line's end, so the caller can recompute C under its own view and the executor can
  log it.

**Byte-identity trap.** `sizeof(FSLineEntry)` (TS 117414, which embeds SearchLine) feeds the byte budgets on the
non-tier-1 path (TS 118102, 118119, 128458, 128487). `tier1::kFslEntryB = 56` (Tier1Ledger.h:115) is a fixed
constant. Growing SearchLine changes when stores are refused, which changes units and therefore play. So either:
* (a) pin every `sizeof`-derived budget to fixed constants first, as tier 1 already did for portability; or
* (b) keep the new fields out of line (a `uint32` index into a per-decision side table, in padding).

Default (b). Verify with `ptype /o` and smoke play-changed = 0 (section 5).

### 3.4 Exact arithmetic

* If the mapping's per-game delays are integers in milli-rounds (the stage-1 table is integer rounds times 1000; a
  learned model is quantised to 1/1000), every `f` is an integer and C is an int64 sum.
* N must be equal for every gauntlet deck. That is the USER's "equally is fine" (parent 7.5), enforced by the mapping
  builder. Then `E = C / (1000 G N)` exactly. Otherwise weight each deck's sum by `lcm(n_g) / n_g`.
* With integer C, the strict cut, ties and tie-breaks are byte-stable across threads and compilers (MSVC vs gcc). No
  float ever enters a comparison.

**Censoring** (parent Q5: the mapping is built at horizon 15). A delayed gauntlet game not killed by round 15 has a
censored `D >= 16 - K`. For every W up to 15, treating it as a LOCK is exact: the race is won at every W the control
deck can score. So the mapping stores LOCK, never a censored number.

### 3.5 Leaf combination

* **Value leaf** (TS 125060-125079): returns a W estimate in milli-rounds. Score `C_H` interpolated at it. `f` is
  piecewise linear in W with breakpoints at integer `T + D`, so linear interpolation between integer points is exact
  for integer delays.
  * The leaf credits NO events beyond the horizon, so it is pessimistic for lines whose control payoff is later. It
    is in-distribution for our own game: the mapping, not our state, carries the gauntlet's disruption. Parent 5.5's
    out-of-distribution concern applies to GAUNTLET leaves, not ours.
* **No-leaf constant:** `C_H[Hmax + 1]`, a real value (SENT).
* **Rollout:** returns W and its own events, which are appended to the state's handle as the playout resolves them.
  The leaf scores `C_{H_end}[W]` from the rollout's final handle, returned through a frame-scoped thread-local like
  `leafeval::t_tb` (TS 8270-8280), because `SimulateToEnd` returns int.
  * Memo hits must replay S onto the reader's H: the TT entry stores the event delta S relative to its own root, out
    of line, in control mode only. `Extend(H_reader, S)` costs one trie walk.
  * A history-blind playout's (W, S) is a function of the state, so the rollout's keys stay state-only (F4).

### 3.6 Threading through the executor and the rollout's lockstep twin

* **Recording point:** the SHARED effect layer, at RESOLUTION. That is `EffectHandler::Resolve` at GE 1902 and the
  ETB / activated / trigger handlers that both `GameEngine::ResolveStack` and the search's `ApplyPlanDirect` call.
  This follows the lockstep-twin convention.
  * A countered or fizzled spell records nothing.
  * A permanent-based effect (Parallax Wave's activation, Opalescence entering) records when the activation resolves
    or the permanent enters.
* **`T = state.turn_number` at resolution.** Only `AdvanceTurnCounter` (GameState.h:866) moves it, and it runs after
  the opponent-turn lambda (TS 86941-86966; executor twin GE 482-527).
  * On the play, an instant in the opponent's turn after our turn t records t.
  * On the draw, the opponent's turn filed after our turn t-1 is their turn t, and it records t-1.
  * That is exactly parent 4.6's convention. Upkeep and draw-step events (BoundaryFan axes after the advance) record
    t+1.
* **The slot** is "ours" during our turn and "theirs" inside the opponent-turn window.
* **The executor** keeps no history member: the real game's history is the state's handle. Every search root
  (`blindsearch::View`, AE 4905/5077/6934/7702/3212) and every bottoming trial (AE 2210) copies it. Trials cannot
  pollute the real game, and nothing needs stashing in `RolloutWinTurnFrom` (AE 2229-2257).
* **Stage-2 schedule log:** at game end, walk the real game's spine. The stage-2 recorder and the search's history
  are one structure.

### 3.7 Cost per node

Per node, the extra work is: copy a 4-byte handle, one array read for the entry refusal, and one array read per
scored line. A new history additionally costs one `Extend`, from a hash lookup up to roughly 20-40 us, once per
distinct (parent, event) per decision. Dedup and win keys fold an 8-byte hash only when H or `sig_t` is non-empty.

The real cost of stage 3 is not per-node work but LOST PRUNING:
* the integer stops that become rare (2.4);
* the no-win memos that weaken while `Z` (2.5);
* kill-only and the identity folds that switch off (2.8, 2.9);
* the longer horizon (12-15).

Section 7 measures exactly those.

## 4. The mapping-consult interface

### 4.1 Interface (`src/ai/ControlScore.h`, new; immutable, loaded once per batch, shared across threads)

```cpp
struct CtlKey   { std::uint16_t kind; std::uint16_t arg; };          // section 4.3
struct CtlEvent { CtlKey key; std::uint8_t slot; std::uint8_t round; };
class ControlMapping {
public:
    int  Games() const;                                  // G * N (N equal per deck)
    int  Horizon() const;                                // 15
    // Per-game joint delay of (parent prefix + e), in milli-rounds; kLock for a lock. Fills child's
    // per-game delay state. Pure, deterministic, O(G*N) for levels 3-4; O(G*N * model) for level 2.
    void Extend(const DelayState& parent, const CtlEvent& e, bool on_the_play, DelayState& child) const;
    double OfferShare(CtlKey k, int round, int slot, bool on_the_play) const;  // share of gauntlet games
                                                         // with a legal target (parent 5.7's offer rule)
    std::uint64_t Fingerprint() const;                   // gauntlet decklist hashes + play digests + seed set
};
```

`DelayState` is opaque per level:
* **level 4** (pure sum of per-game singles; the stage-1 table at fixed seeds): `int16 D[G*N]`;
* **level 3:** adds the per-game interaction state (counts by family: lands destroyed, counters this round,
  creatures removed);
* **level 2** (learned model): a per-game feature accumulator. It includes the gauntlet game's own state at round T,
  READ FROM THE STORED FIXED-SEED BASELINE (parent 7.5 already stores the baseline per seed). That is the "A-state"
  input of parent 5.1 with no co-simulation;
* **level 1** (full-joint measurements): consulted FIRST as a cache keyed by the canonical prefix hash. A hit
  overrides the lower level per game.

O(1) at a node, because the trie memoises `Extend`. "Stage-1 table first, learned model later" is a swap of the
`ControlMapping` implementation behind one interface. The trie, keys and search do not change.

### 4.2 Lifetime and invalidation

* The mapping file records its fingerprint: the gauntlet deck list, each deck's decklist hash and play digest, the
  seed set and N.
* A control deck's game attaches the mapping by mode (`control_score: true`), like the value model pointer
  (`GameState::m_value_model`, GameState.h:2316). It is a `const ControlMapping* m_ctl`, null in goldfish.
* `on_the_play` is per game.
* A stale fingerprint refuses to run. It never silently runs on an old gauntlet.

### 4.3 Effect keying from engine actions

* **The key table:** `CtlKey ControlKeyFor(const CardDef&, int mode_or_ability)`, derived from `cards.json`
  `parameters` once per deck at load into a name-to-key table. Cards with no key record nothing. The same
  derivation drives `deck_compare.py`'s "control-based" classification (parent Q11 answer), so the two never
  disagree.
* **The stage-1 vocabulary:**
  * `land` (destroy land);
  * `bigcrt` / `crt` (destroy or exile creature; arg = how many, for Wave counters spent);
  * `sweep`;
  * `counter_hard` / `counter_to_hand` / `counter_to_top` (arg = the Q8 trouble threshold band);
  * `bigdisc` / `disc`;
  * `lifegain` (arg = life);
  * `fog`;
  * `exile_until` (arg = return round, from fade counters);
  * `tax`;
  * `skip`;
  * `lock_piece` (Opalescence entering with a Wave out is the key `opalescence`; the mapping, not the engine, knows it
    locks).
* Parent 4.3a's Wave + Opalescence lock is an interaction the MAPPING carries (levels 1-3). The engine only records
  "Wave exiled j on round 3" and "Opalescence on round 4".
* **Target, for Q3/Q8:** the event does not name a gauntlet card. The mapping's replay chooses the target by the
  USER's rule (clairvoyant highest-value), so the key is (kind, arg, round, slot) only.
* **`Z` (reachability)** is "any card in hand / library / castable graveyard, or an activatable permanent, has a
  `CtlKey`". It is a count maintained by zone moves or computed per node (a 40-card scan). It feeds F5 and
  `NoControlCastable`.

## 5. Goldfish byte-identity

### 5.1 One objective path, runtime dispatch

Use a branch on `state.m_ctl != nullptr` (call it `ctl`) at each restated site. The integer code stays exactly as
written. The E code is the `if (ctl)` arm, reducing to the integer arm whenever `H.empty() && !Z` (F5).

Rejected: compile-time dispatch (templating FSLineWin / FSLineTail / SimulateToEnd on an Objective type).
TurnSolver.cpp is a 148k-line translation unit. Templating its core would double the hot code, compile time and PCH
churn on the slow machine (a full Release build there is 20+ minutes of this file alone) for a saving of one
predictable branch per site. The runtime branch costs goldfish nothing measurable: `m_ctl` is null and the branch is
perfectly predicted.

### 5.2 The traps that would move goldfish ground truth (each needs a test)

* **`sizeof` growth** feeds byte budgets:
  * `ApproxStateKb` (TS 74455, the prefix-resume cache cap, "result-neutral" but unit-bearing);
  * `BoardBytes` (ManaOracle.cpp:3672, MTG_MANA_ORACLE_BOARD_MB);
  * `sizeof(FSLineEntry)` (TS 118102-128487);
  * `kWinEntryBytes` / `kNoWinEntryBytes` (TT 223-224) and the m1memo `ByteCap`.

  So `GameState` gains only the padding-resident `ctl_hist` handle and the `m_ctl` pointer, `SearchLine` gains only
  padding-resident fields, TT entries gain nothing inline (the event delta is out of line, allocated only when
  `ctl`), and each is checked with `ptype /o`.
* **Key folds** happen only when non-empty (`H.hash`, `sig_t`) or only when `ctl`.
* **Lifted inertness gates** (2.11) are gated on `ctl`. Memory Lapse stays inert and Remand self-target-only in
  goldfish (parent 5.7).
* **The free-turn rule** is untouched when `!ctl`.

### 5.3 Two identity tests (the backbone of validation)

* **Goldfish untouched:** smoke and regression play-changed = 0 at every step, plus unit tests.
* **Control mode with an empty mapping equals goldfish.** Attach a `ControlMapping` with zero keys to a goldfish deck
  (`Z` false, H empty). Every restated site must then take its integer arm, so the digests must equal the deck's
  goldfish GT. This one test catches a restatement that does not reduce (F5), a key fold that fires on empty, or a
  sentinel mishandled in the no-effect case.
* **A second variant:** keys present but every delay 0. Z is true, so the O-class gates are off and digests WILL
  move. E must still equal W per game, and the per-decision C must equal `1000 G N W`. This exercises the E arms on
  real trees.

## 6. Rule interactions

### 6.1 No greedy in the search window

Three sites become greedy picks under E unless changed. Each is handled above:
* kill-only's unscored `candidates.front()` (2.9);
* the provider-proof gates that drop control lines (2.8);
* PROVEN NO-WIN's `Plan{}` (2.8).

Nothing in the E path substitutes a heuristic for a branch:
* The virtual-target cast and the opponent-turn cast window are BRANCHED options. A provider may only restrict them
  (the parent 5.7 offer-share rule) or order them.
* `HeuristicBottomPick` and `plan.value` stay tie-breaks below exact C equality.
* `TurnSolver::Solve`'s `GreedyPermit` gate is untouched.

### 6.2 "Do nothing more" is always an option

The idle pass is inserted unconditionally (TS 126020 `IdlePassMissing`) and stays. Control mode adds two roots that
must each offer "nothing":
* the opponent-turn cast window: "cast nothing" (hold the counter, or let the spell through);
* a mid-combat block window, if parent 5.6's blocking is ever searched on our side.

Under E "pass" matters more than in goldfish: holding {1}{U} through our main is exactly the line that casts the
counter on their turn.

### 6.3 Completeness at large budget

* Every R restatement is exact for the leaf-valued E objective (F2 and section 1.4).
* Every O site is a W-identity fold or a heuristic that is switched off. Off means more work, never a lost line.
* No site is turned into a budget-dependent gate.
* At `--budget-ms 0`, control mode finds the leaf-valued argmin of E over the window.
* The lossy levers already in the tree (the escalation beam, the p18 sampled gate) keep their current lossy status,
  ranked by C. None is new.

### 6.4 No unbudgeted-only changes; turn-complete spending

* Every restatement applies at every budget. The tiers will run control-mode rows budgeted (stage 3 validation), and
  the unbudgeted backstop rows smoke the `Unlimited()` paths (lazy leaf, ET_D0_BOUND, label scope).
* The LB cut is per round. A line is refused only as a whole round, never mid-frame, and nothing here truncates
  within a turn. The turn-complete preference (finish turn k's frames before turn k+1's) is unaffected. If a
  budget-staged wave is ever added for control casts, its boundary is the turn.

### 6.5 Valuing control beyond the horizon (Q-B)

**Default for the first build: the playout stays a history-blind, W-minimising policy, but it RECORDS what it casts.**
* Every rollout memo stays state-keyed (F4).
* Goldfish stays identical by construction.
* The leaf credits the events the playout happens to cast.
* The weakness: the playout casts control cards only when its W-policy happens to. Under kill-only (off in control
  mode) or a tie on `plan.value` it never seeks them. So the leaf is pessimistic about holding counters.

**The measured upgrade:** an `E_self` policy, which ranks each per-turn candidate by the E of its own turn plus
playout events, scored against an EMPTY prior history. Because it reads no history, memos stay state-keyed.
* Cheapest form: `E_self` as a tie-break above the graded `Quantity` (2.12).
* Full form: `E_self` as the primary key. Its tie-prune and tighten need the LB form.
* Beyond the horizon, a provider rule is also legal (the sanctioned playout greedy): for example "cast removal when
  the offer share is at least 0.5". That needs no search change.

**Not recommended:** the true E in the playout. It needs H in every rollout key, and the transposition loss would
likely make the rollout leaf unaffordable.

The value leaf credits no future events at all. A scored leaf fitted to E (the Q6 pattern) would fix that, but it is
an artifact for after stage 3 (Q-C).

## 7. Build plan (small, testable steps)

Each step lands default-inert for goldfish, under the push queue's batching rule. "Smoke = 0" means smoke play-changed
= 0 against GT with the step's binary. Steps 1-2 belong to stages 1/2 or are their prerequisites, and they are listed
because stage 3 cannot start without them.

0. **Prerequisite: virtual-target control casts + the opponent-turn cast window** (2.11). Gated on `ctl`.
   * Validate: smoke = 0.
   * Unit tests per card family: a counter, a removal spell, a Wave activation and a life gain each resolve with the
     cost paid, the card moved and an event logged. The passive opponent's state is unchanged.
   * The window offers "cast nothing".
   * The executor/rollout twin test: the same line logs the same events in `GameEngine::ResolveStack` and
     `ApplyPlanDirect`.
1. **`ControlScore.h`: CtlKey table, ControlMapping (level 4), the trie, curve, Extend.** No search change.
   * Unit tests:
     * empty H gives `C[W] = 1000GNW`;
     * parent 4.3a's worked table reproduces exactly (rows 3 / 4 / 6 / 9);
     * F1 and F2 on random schedules (append never raises C; `C[t]` unchanged by an event at `T >= t`);
     * the censoring-as-lock rule;
     * same-slot order invariance;
     * integer exactness and thread invariance (the same trie built on two threads gives the same hashes).
2. **Recording at resolution** into the state's handle, executor and search alike.
   * Validate: smoke = 0.
   * The stage-2 schedule log equals the spine.
   * The twin test from step 0 now compares handles.
3. **`SearchLine` result kind + `c` + `frontier_lb` + `hist_end` (out of line); the sentinel split; `ctl` dispatch
   wired but every E arm still returning the integer answer.**
   * Validate: smoke = 0.
   * Identity test 5.3, empty mapping: digests equal goldfish GT.
   * `ptype /o` shows no `sizeof` change.
4. **Restate the refusals, child cuts and improvement tests** (2.1, 2.2) in FSLineWin, FSLineTail and
   SolveWithLookahead.
   * Validate: identity test 5.3 (empty mapping) is still byte-identical.
   * **The soundness oracle:** a test-only arm with EVERY cut, memo and fold off (`MTG_CTL_REFERENCE`) computes
     plain minimax E at d2-d3, unlimited budget, on 50-100 recorded Enchanted Evening positions. The restated search
     must return the same root C and an equal-C line on every position.
5. **Memos:** H in win keys, W-fact no-win stores with LBnw reads, `sig_t` in `BuildDedupKey`. Off: inwin wins,
   LEAFLESS_READS_LEAFED, the OF wave, the non-split path.
   * Validate: identity test 5.3; the step-4 oracle with memos ON (must still match).
   * The existing VERIFY modes (gamelc, inwin, nowin, leaf) extended to compare (W, events): zero disagreements on the
     oracle set.
6. **Rollout:** events returned through the frame side channel, the TT/xmmemo/rgc event deltas, the loss sentinel.
   Kill-only off when `Z`. The LB stop at the loop head replaces `turn > cutoff_turn`.
   * Validate: identity test 5.3; rollout determinism (the same state twice gives equal (W, S)).
   * The rollout TT verify (`r_same` / `r_loose`) on (W, S).
   * The step-4 oracle with the rollout leaf.
7. **Iterative deepening, ladders and the hybrid:** `frontier_lb`, restated stops, the `verified` test, escalation
   comparisons. Lazy leaf and p18 off in control mode.
   * Validate: identity test 5.3; the step-4 oracle at d3 with the ladder on.
   * A recovery check: every control game whose E got WORSE vs step 6 recovers at `--depth 8 --budget-ms 0`.
8. **Provider-proof gates + `NoControlCastable`; executor sites** (proven-no-win off, ChooseReal*, BoundaryChoose*,
   bottoming by C with the LB stop, the blind chooser). The free-turn rule off in control mode.
   * Validate: identity test 5.3 and the oracle.
   * Unit tests for each gate's new fact: a counterexample position where the gate would have dropped a control line
     now keeps it.
9. **Cost measurement for the USER's Q4 decision**, one pooled `--batch`, Enchanted Evening plus at least one
   two-mode deck (goldfish + control, for example Hinata with Remand/Memory Lapse, parent 5.7), held-out seeds, the
   shipped play setting, `MTG_PGO=0` on every arm. Arms:
   * **(a) partial scores, leaf-valued incumbent** (the default);
   * **(b) partial scores, verified-only incumbent** (Q-A);
   * **(c) the same deck's goldfish play at the same `control_max_turns`**: the stand-in for the banked-free-turn
     fallback's cost (Q-K), whose search is integer;
   * **(d) the same deck's goldfish play at its goldfish `max_turns`**: the deck's normal cost.

   Report per arm:
   * units and wall per game and per decision;
   * the depth committed at the shipped budget, and the share of decisions reaching the user depth;
   * the stage-2 MEASURED E, with its standard error, and the modelled-vs-measured gap;
   * cut rate (refusals per node), memo hit rates (FSLineCache win / no-win, gamelc, rollout TT) vs goldfish;
   * the share of nodes with `Z` true;
   * distinct histories per decision and the trie's peak bytes;
   * the 3x-Hinata suite gate check.

   The USER's question is "is (a) prohibitively expensive?". So the headline is `(a)/(c)` in units and wall, beside
   the quality difference in measured E. If (a) looks prohibitive, report which lost prunes dominate (from the cut
   and memo counters), because a fix there may be cheaper than the fallback. Building the fallback itself (banked
   integer free turns) waits for the USER's ruling on these numbers.
10. **Control-mode suite rows and GT** (the deck-in-suite-before-artifacts rule): smoke / regression / overnight rows
    for the control decks, then the scored keep table (parent Q6). The adoption bar: net <= 0 per deck on held-out
    seeds, and slower games recover at d8 b0.

## 8. Open questions (each with the default taken; none blocks the steps above)

* **Q-A. Which incumbent cuts?** Parent 4.5 says verified-only. Goldfish today cuts against leaf-valued incumbents
  (section 1.4). That is exact for the search's leaf-valued objective and keeps completeness at large budget.
  Verified-only cuts far less on late-killing control decks.
  *Default:* leaf-valued, as goldfish. Step 9 measures both, and the verified-only arm is deleted after your ruling.
* **Q-B. How should the playout value control beyond the horizon?**
  *Default:* a history-blind W-policy that records the events it casts (sound, cheap, pessimistic). The `E_self`
  tie-break, then primary key, is built only if step 9 shows a gap. Per-deck provider playout rules ("cast removal
  when a target is likely") are a legal alternative.
* **Q-C. The value leaf credits no events beyond the horizon.**
  *Default:* use it as a W estimate scored through `C_H`. A scored leaf fitted to E comes after stage 3, and only if
  you want one (the Q6 pattern).
* **Q-D. When is a virtual-target control cast OFFERED?** Parent 5.7's default for counters is to cast when most of
  the gauntlet's games have a spell that turn.
  *Default:* the same rule for every family, with the mapping's per-round target share at least 0.5. Below that the
  cast is not offered. (A share of 0 is always pruned.)
* **Q-F. The provider `NeverCast` doctrines and the inert-first discard/cycle tiers** (Equipment's Removal, Snow's
  Skred, Colossus's Path to Exile, Sea Creatures' wave; Hinata, Fluctuator, Worldfire, Cyclone and FiveColour treat
  inert cards as dead). Their stated reason is goldfish. They are yours per deck.
  *Default:* they stay exactly as they are in goldfish play. In control-mode play of a two-mode deck they do not apply
  (control cards are cast and are not discarded first), pending your per-deck review. I will list each deck where
  this changes a doctrine when its control rows are built.
* **Q-G. The fresh-spend axis drops a no-win line (TS 127591).**
  *Default:* in control mode admit a line iff its C beats the incumbent, so a no-win lock line is admissible.
* **Q-K. Is the same deck's goldfish play at the control horizon an acceptable stand-in for the banked-free-turn
  fallback's cost?** The fallback keeps the integer machinery plus a credit.
  *Default:* yes. Build the fallback only if you ask for it after step 9.
* **Q-L. Same-slot events are an unordered set** (2.6). It makes every state-keyed fold sound, at the price of not
  distinguishing two control spells' order within one main phase.
  *Default:* a set, ordered canonically by key in the mapping's replay.

Not questions (settled by rules or recorded rulings, recorded here so they are not re-asked):
* life gain is a control effect (parent 5.6);
* the free-turn credit is off in control mode (parent 4.5);
* kill-only and the W-identity folds go off when an event is reachable (a W-only identity is not an E identity);
* bottoming stays on and scores by E;
* 2HG is out of scope (Q7).
