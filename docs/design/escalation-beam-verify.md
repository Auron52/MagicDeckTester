# Escalation value-guided beam ("reuse the leaf-value pass, do only the incremental rollout")

**Status (2026-07-18): ADOPTED on the two heavy decks (antilife + hinata) as `beam2_ld2 + fresh0.5`, per USER
("wire + full A/B now"). Smoke + regression A/B GREEN (0 searched win->loss; all d5 cases neutral-to-better;
d0/d3/light byte-identical). Overnight A/B + GT rebaseline in flight. Not yet committed.**

## ADOPTION (2026-07-18)
- **Wired**: `ValuePlay.beam_width` + `beam_leafdepth` (MulliganProfile.h), parsed in MulliganProfileIO.h,
  threaded through `FullSearchLineHybrid` (env-precedence, mirrors `escalation_fresh_frac`), passed from
  AIEngine gated on **`vp_here = value_play.drives() && m_lookahead_depth == target_depth`**. The depth==target
  gate is LOAD-BEARING: the harness runs d0/d3 sanity cases at off-policy depths with the block still loaded, so
  without it the levers fired at d3 and over-pruned (measured d3 win->loss). TurnSolver also has a belt-and-
  suspenders `depth >= beam_leafdepth+2` guard on the per-deck path. Both preserve byte-identical off.
- **Set** on antilife + hinata value_play: `beam_width=2, beam_leafdepth=2, escalation_fresh_frac=0.5`. Light
  decks untouched (beam off, fresh -1) => byte-identical. burn's target_depth=6 => vp_here at d6, but beam_width
  default 0 => off.
- **A/B results (train seeds)**: smoke s1001 — antilife d5 neutral (4.6333=4.6333), hinata d5 better
  (6.0800->6.0667), 0 win->loss. regression s2002/s3003 — antilife d5 −0.004/−0.008 (2 loss->win!), hinata d5
  −0.01/−0.02, 0 win->loss, all d0/d3/light byte-identical. Every d5 change is an improvement or same-win-turn
  line change; NO regressions.

## REFINEMENT (2026-07-18, user: "avg win turn (loss=9) is the metric; look into the worse cases, are they fixable")
- Overnight's 4 searched win->loss were on seed 7007. **In ISOLATION (`--game-index`) all 4 replay as a T5 WIN
  under BOTH beam-off and beam-on** -- the beam does not misplay them alone. The flip only appears IN-BATCH.
  Root cause: the goldfish/batch runner creates ONE AIEngine per THREAD and reuses it across all games (GoldFish
  Runner.cpp:262, outside the per-game loop), so a game deep in a batch starts from carried-over per-thread state
  that an isolated replay lacks. The beam perturbs that state on knife-edge games. (Diagnostic: MTG_DUMP_WINS.)
- **Clean per-game beam-off vs beam-on diff (goldfish 1000g, s7007)**: shipped `beam2_ld2_fresh` flips gi564+gi589
  win->loss and gi801 loss->win (net +1 unwon). **`beam3_ld2_fresh` (widen W=2->3): ZERO win->loss, keeps gi801
  (net -1 unwon).** So the worse cases ARE fixable by widening the beam.
- **Clean 4x1000g antilife config sweep** (baseline = true legacy via MTG_ESC_BEAM=0 + FRESH=-1; LP = avg win
  turn, loss=9 -- the user's metric):
  | config | dLP | seeds worse | speed vs legacy |
  |---|---|---|---|
  | beam2_ld2 (beam only) | +0.0005 | 2 | faster |
  | fresh0.5 (fresh only) | −0.0013 | **0** | ~neutral |
  | beam2_ld2_fresh (SHIPPED) | −0.0027 | 1 (s7007) | faster |
  | beam2_ld3_fresh | −0.0020 | 1 | faster |
  | **beam3_ld2_fresh** | **−0.0047** | **0** | +2.3ms SLOWER (~6%) |
  - **beam3_ld2_fresh = best avg + 0 worse seeds**, but SLOWER than legacy on antilife (wider beam explores more;
    antilife is already fast at ~40ms so absolute cost tiny). fresh0.5-alone = the speed-neutral 0-regression
    option (smaller avg gain). beam2_ld2_fresh (shipped) = fastest + good avg but 1 knife-edge seed worse.
  - So there's a quality<->speed knee: W=2 faster/1-worse, W=3 best-avg/slower.
- **hinata 4x150g sweep** (baseline legacy LP=6.2550): beam2_ld2_fresh −0.0233(0 worse)/−52ms; beam2_ld3_fresh
  −0.0267(0)/−14ms; **beam3_ld2_fresh −0.0333(0 worse)/−12ms** = best avg AND still FASTER than legacy (hinata's
  frontier is wide, so W=3 still prunes a lot). beam2_ld2 alone +0.0000/−113ms; fresh0.5 alone −0.0100.
- **DECISION: adopt `beam_width=3` (beam3_ld2_fresh) on BOTH heavy decks** -- best avg + 0 worse seeds on both,
  faster on hinata (the deck where speed matters), marginally slower on antilife (~6% of a ~40ms deck, buys the
  best avg + zero regressions). Matches the user's priority: avg win turn is the metric, fix the worse cases,
  speed secondary. Per-game s7007 recheck: beam3_ld2_fresh = ZERO win->loss (vs beam2's 2). Smoke re-run:
  antilife d5 s1001 byte-identical to legacy, hinata d5 −0.04, 0 win->loss. beam3 overnight A/B in flight.

---
## (original investigation notes below)

## The goal (user-directed)
Make the value-leaf hybrid's heuristic **escalation** match the original (baseline) quality but run faster, by
**reusing everything practical from the value (first/probe) pass and paying only for the incremental heuristic
rollouts**. A cold "single-depth" pass was REFUTED (slower on hinata — it throws away the ladder's move-ordering
+ leaf memo). The BEAM keeps the ladder and prunes its WIDTH: expand only the top-W plans per node, so the
escalation rolls out ~`W^depth` frontier states instead of the full B&B frontier. Rollouts are ~94% of
escalation cost, so cutting frontier width cuts the dominant cost.

## What is BUILT (all in `src/ai/TurnSolver.cpp`, gated by env; default off = byte-identical)
Three layers, each a strict refinement of the last:

1. **Uniform static beam** (`MTG_ESC_BEAM=W`): cap the plan loop at the top-W `MoveOrderPlans`-ordered plans in
   `FSLineWin`/`FSLineTail`. `g_esc_beam_width` + `EscBeamGuard`. — *Measured: faster, but on antilife the narrow
   beam is 4/4 WORSE (the static order misses the heuristic-best line at knife-edge nodes).*

2. **Value-ranked beam** (same `MTG_ESC_BEAM=W`, now the default beam behavior): the probe records, per interior
   node (keyed by the same `BuildSimKey` the FSLineCache uses), the value-win-turn each MoveOrder'd plan produced
   (`g_probe_plan_vals`, armed by `g_probe_val_recording` around the probe). The escalation reorders its identical
   `pre` by those recorded value ranks *before* the beam-cap, so the kept W lines are the ones the VALUE pass
   rated best — the faithful "reuse the first pass's ranking." — *Measured: helped only MARGINALLY (antilife
   beam2 4/4-worse → 3/4-worse; beam3 3-worse → 2-worse). The value ranking is a WEAK proxy for the heuristic
   ranking exactly at escalation nodes — that's WHY those nodes escalate — so it can't rescue a narrow beam alone.*

3. **Depth-aware beam** (`MTG_ESC_BEAM_LEAFDEPTH=D`): the beam applies ONLY at nodes with remaining depth <= D
   (near the leaf, the widest layers = most rollouts). Nodes ABOVE it — the top plies, crucially the ROOT (the
   play we actually commit) — keep full static exploration, so a narrow beam can never drop the heuristic-best
   PLAY; it only prunes the deep win-turn-ESTIMATE frontier. `g_esc_beam_leafdepth` (INT_MAX default = uniform).
   — *THIS is the fix.*

## Measured — antilife (4 seeds x 300g, the deck where the uniform beam regressed)
| variant | dLP (lower=better) | speed |
|---|---|---|
| baseline | — | 60.2 ms/game |
| beam2 (uniform value-ranked) | +0.0034 (0 better, **3 worse**/4) | −5.10 ms, 4/4 faster |
| **beam2_ld2** (protect top plies) | **−0.0017 (2 better, 1 worse)** | −2.38 ms, 4/4 faster |
| beam2_ld1 | −0.0017 (1 better, **0 worse**) | −1.03 ms, 4/4 faster |
| beam2_ld3 | +0.0009 (2 better, 2 worse) | −4.01 ms, 4/4 faster |
| fresh0.5 (budget-restore) | −0.0008 (1 better, 0 worse) | −0.15 ms |
- **beam2_ld2 turned the uniform beam2's +0.0034/3-worse into −0.0017/quality-safe AND stayed faster** — and it's
  a BIGGER speedup than fresh0.5 on antilife. `ld1` is safest (0 worse) but tiny speedup; `ld3` starts slipping.
- Byte-identical when off (smoke 18/18, 0 play-changes), re-verified after each layer.

## Measured — hinata (4 seeds x 80g, STUB table = noisy/directional only)
- Uniform value-ranked beam2: +0.0094 (noisy), −232 ms. beam2_ld1: +0.0000 (net), −190 ms. **beam2_ld2:
  −0.0094 (2 better, 1 worse), −265 ms.** beam2_ld3: +0.0000, −293 ms.
- **fresh0.5: −0.0312 (3/4 better, 0 worse), faster** — the standout quality lever on hinata.

## STACKING — combo `beam2_ld2 + fresh0.5` (the depth-aware gate fixed the combo the uniform beam broke)
| deck | beam2_ld2 | fresh0.5 | **beam2_ld2_fresh (combo)** |
|---|---|---|---|
| antilife (4x300) | −0.0017 / −5.4ms | −0.0008 / −2.9ms | **−0.0025 (2 better,1 worse) / −2.4ms, 4/4 faster** |
| hinata (4x80) | −0.0094 / −77ms | −0.0312 / −76ms | **−0.0312 (4/4 better,0 worse) / −120ms, 3/4 faster** |
- The combo is **quality-BETTER than baseline AND faster on both** — and faster than either lever alone. They
  STACK: fresh0.5 = quality (budget to commit the good line), depth-aware beam = speed (cut the deep rollout
  frontier). NOTE the *uniform* beam2+fresh0.5 was +0.0050/4-worse; the ld2 play-protection is what flipped it.

## Consolidated read — TWO complementary levers, both meet "match quality, faster"
- **fresh0.5** (`escalation_fresh_frac=0.5`, ALREADY wired as `value_play.escalation_fresh_frac`): the QUALITY
  lever — more escalation budget → commits the good line. hinata −0.0312, antilife −0.0008; faster/neutral; inert
  on light decks. The cleanest standalone win.
- **depth-aware value-ranked beam** (`MTG_ESC_BEAM=2 MTG_ESC_BEAM_LEAFDEPTH=2`): the SPEED lever — reuses the
  probe's value ranking + protects the committed play, cutting the deep rollout frontier at equal quality. The
  faithful realization of the user's "reuse the leaf-value pass, do only the incremental rollout."
- They MAY stack (the uniform beam2+fresh0.5 HURT because the beam dropped good plays; with the play protected,
  fresh0.5's budget should now help). **Test `beam2_ld2 + fresh0.5` next.**

## NEXT STEPS (resume here)
1. Finish hinata depth-aware A/B (in flight); confirm ld2 quality-safe + faster there too, pick per-deck D.
2. Test **beam2_ld2 + fresh0.5** stacking on both heavy decks.
3. Report the consolidated finding + recommendation to the USER (the two levers; magnitudes are modest and on
   PROVISIONAL tables — treat as directional until the real antilife gap-fill + hinata mulligan tables land).
4. If adopting the beam per-deck: add `beam_width` + `beam_leafdepth` to `ValuePlay` (`MulliganProfile.h`), parse
   in `MulliganProfileIO.h`, thread into `FullSearchLineHybrid` gated on `value_play.drives()` (like
   `escalation_fresh_frac`). Then full regression + overnight A/B (train + held-out) + GT rebaseline + approval.

## Surrounding state a fresh agent needs
- **Branch `phase-1-2-deck-analyzer`**, pushed through `d5cbba9`. See `remote-integration-2026-07-18` memory.
- **Uncommitted**: `src/ai/TurnSolver.cpp` (the 3-layer beam), `scripts/esc_fallback_ab.py` (avg-regex fix for
  the a4f2be7 metric change + `ESC_AB_WORKERS` + th/slivers/knights/burn deck map + beam/ld variants). Byte-
  identical-off, safe to commit the scaffold.
- **Env knobs**: `MTG_ESC_BEAM=W` (0=off), `MTG_ESC_BEAM_LEAFDEPTH=D` (INT_MAX=uniform). Both read once at the
  top of `FullSearchLineHybrid`; recording armed only when W>0.
- antilife d5 ~6GB/process → run its A/B at `ESC_AB_WORKERS<=4`. Overnight OOMs at THREADS=24 → use 12.
- ALL magnitudes are on PROVISIONAL tables (antilife gap-fill + hinata mulligan pending) — directional only.
