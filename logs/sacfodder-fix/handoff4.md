# Handoff 4: MTG_SAC_FODDER_MENU fix-up 6 (agent 6, 2026-11-22)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/sacfodder-fix`, branch `wip/sacfodder-fix`.
Scratch: `/workspaces/MagicDeckTester/build/scratch/claude_sacfodder_fix5/` (tools/: mk_ab2.py `deck:gi0:n:tag[:depth]`,
cmp2.py, run1b.sh; binaries mtg.k2 / mtg.k4 / mtg.k6).

## USER rulings (verbatim)
- 2026-11-21: "I don't quite get the issues, but this seems like an issue we need to fix/extend until it is a net benefit."
- 2026-11-22 (reopening): "It still makes no sense to me that our results get slower. We should be searching less. We should
  try any and all options to get it working better."
- 2026-11-22 (via parent): "My understanding of what we are doing with the sac-fodder change is making it so that some of the
  work (the sacrifice ability) is not searched. To me that should tend to reduce the amount of work, but somehow it keeps
  going up, which I don't like or agree with."
- Earlier (handoff.md / handoff2.md): "For 3 I would like us to fix this up. I can't think of a reason why this would reduce
  quality significantly. If anything it should be more quality at a slightly higher CPU cost."; never justify CPU by the 3x
  headroom; adoption bar: every slower game recovers at d8 b0 and every deck net <= 0 vs lever OFF on a large held-out
  sample ("+1 is not neutral. 0 is neutral."); a budget may re-price work, never make a line unfindable at unlimited budget.

## Findings (see docs/design/sac-fodder-menu-2026-11-18.md "Fix-up 6")
1. No second route: the searched Skirk/Mycon SacForMana action exists only where a death pays (SacOutletEatHasValue), and the
   lever is dead on exactly those boards. Elsewhere MTG_SAC_OUTLET_PAY (default ON since 2026-10-31) already replaced it.
2. The memo-key hypothesis is REFUTED: KEY_LIVE / GATE_KEEP / MODE_SHARE (exact) leave units x1.099 -> x1.096 and every game
   unchanged (ab1). 622286 T1 pass 1 is 54,039 units / no win in onP, onPK and onPKG alike (k2 traces).
3. Where the units go (st/, 400 Goblins games, MTG_ROLLOUT_STATS + MTG_DEV_SEQ_STATS, off -> on): total 3.725M -> 4.338M
   (+612K). dev-seq pass 1 +60K; pass 2 +385K (66 -> 117 runs); deferred canon-drop pass 2 +168K (6 -> 17 runs);
   root la_cand +65K. pass2_better = 0 in both arms. So ~90% of the extra work is second passes the wave opens for the
   credited plans, which never took a better line in that slice.
4. ab1 (one pooled batch, mtg.k2, dev gi 620000/630000 x5000 + held-out 640000/650000 x5000 + d0 660000/670000 x5000):
   | arm | gob dev | fun dev | gob HELD | fun HELD | units gob/fun (held) | cpu gob/fun (held) |
   | onP (no new cuts) | 2/3 +1 | 0/0 0 | 4/1 **-3** | 1/0 **-1** | x1.109 / x1.042 | x1.174 / x1.075 |
   | onPKGS (all cuts) | 2/3 +1 | 0/0 0 | 4/1 -3 | 1/0 -1 | x1.107 / x1.041 | x1.173 / x1.073 |
   | onPKGSw0 (wave off) | 2/4 +2 | 0/3 +3 | 4/3 -1 | 0/3 +3 | x1.076 / x1.025 | x1.154 / x1.057 |
   d0: gob/fun 0 changed (the lever is off in the d0 runner under STAGE_PLAYOUT).
   Held-out slower game 640933 (3 -> 4) RECOVERS at d8 b0 (on 3, off 3; d8b0/). Dev 622286/622411/624011 recovered earlier.
5. Quality bar on the held-out sample is MET by onP already (Goblins -3, Fungus -1); the open problem is CPU
   (Goblins x1.17, Fungus x1.07).

6. Where 622286's pass-1 work goes (k4 traces, mtg.k4, MTG_SFM_DIAG_TRACE): of 909 live fodder contexts in the T1 search,
   860 are BREAKPOINT enumerations inside pass 1 (g_bp_enum_depth > 0), 49 top-level pass 1; 2,588 subsets credited,
   only 21 wave drops. Board in those lines: Skirk on the battlefield, Rundvelt in the graveyard (eaten earlier in the
   line), Siege-Gang in hand -- its three ETB tokens are credited as fodder for the same plan's other casts. The wave
   never stages bp enumerations, so those plans are searched in pass 1 and the T1 search runs out (54,039 units, no win)
   where lever-off finds T3 at 51,126. Every variant tried gives the same T1 failure: onP/onPK/onPKG/onPKGS, nolat
   (wave drop without latent, MTG_SFM_DIAG_NOLATENT), w0 (wave off). BPENUM (drop them in pass 1) measured +7 earlier.

## ab2 result (DONE 14:35 UTC; mtg.k6, one pooled batch, same seeds as ab1)
Every arm -- on, p2x2, p2x4 and even p2skip -- is IDENTICAL to on (Goblins dev +1, held -3; Fungus dev 0, held -1; units
x1.096/x1.107 gob, x1.05/x1.04 fun; CPU x1.15-1.17 gob, x1.07-1.09 fun). The arm never fired: it keys on "pass 1 met
ONLY wave latents", and every traced pass 1 also has a hard latent (`hardnew=1` in every [sfm-2p] line, lever off as
well). So the second pass runs in both arms for other reasons, and the lever does not open it -- it ENLARGES it (dirty
nodes 548 -> 1,880 in the st/ census, carried 2,319 -> 5,461). Pricing or skipping "lever-opened" second passes is
therefore not available; the extra work sits inside second passes the engine runs anyway, plus pass-1 breakpoint credits.

## Running at handoff time
Nothing. All scratch under build/scratch/claude_sacfodder_fix5/ (ab1, ab2, st, k2, k4, d8b0).

## Remaining (in order)
1. (ab2 read: no option moved. Per the stuck-iteration rule this line is PARKED again for the USER.) The remaining
   untried cost option is a per-unit PRICE on work done inside a fodder-credited subtree (charge every unit below a
   credited plan k times, pass 1 and pass 2 and bp enumerations alike) -- a re-pricing, inert at unlimited budget.
2. Whatever ships: delete the inert exact cuts or keep them deliberately (KEY_LIVE / GATE_KEEP / MODE_SHARE measured ~0),
   delete TEMP diags (sfmdiag counters, MTG_SFM_DIAG_TRACE dump, MTG_SFM_DIAG_NOLATENT, MTG_SFM_DIAG_NOAPPLY), give the
   kept arm a real heurarm slot (header edit = ~190-file rebuild; reset borrowed slots), then the brief's step 4 (default
   ON, rebase onto origin/phase-1-2-deck-analyzer, units, regression --both, accept GT with attribution,
   check_gt_logs.py, push wip/sacfodder-fix).
3. The CPU decision is the USER's: the lever already meets the held-out quality bar (Goblins -3, Fungus -1) at CPU
   x1.17 / x1.07. Do not adopt a CPU increase without the USER (CLAUDE.md "3x is not a CPU licence").
