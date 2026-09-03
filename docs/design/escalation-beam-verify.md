# Escalation value-guided beam ("reuse the leaf-value pass, do only the incremental rollout")

> **UPDATE 2026-09-03 — READ FIRST: the beam SURVIVES, `fresh0.5` is DEAD, and the escalation budget is no
> longer a config at all.** This doc's headline config is written throughout as `beam2_ld2/beam3_ld2 + fresh0.5`;
> the fresh0.5 half of that pairing is refuted and gone. Authoritative record:
> `escalation-budget-investigation.md`.
> - **The beam + single-pass `escalation_cap` design — the actual subject of this doc — is CONFIRMED VALUABLE
>   and stays per-deck.** Engaging cap5 + beam W3/ld2 on Creature Giving moved the escalation's mean committed
>   depth **1.75 → 2.53 at the same budget** and cut **22% of wall** vs the no-cap config. cap+beam at
>   fresh-full is control-PARITY quality (t=−0.33, **0 win↔loss flips either way**) at **0.43×** the control's
>   wall on CG held-out.
> - **The escalation now ALWAYS runs on a FRESH budget equal to the full decision budget ("fresh-full"), as
>   ENGINE BEHAVIOR.** `value_play.escalation_fresh_frac` was **DELETED** (field, parse, pass-through, hybrid
>   param) and the `0.5` keys were stripped from the antilife/dragonstorm/hinata2 sidecars. An ABSENT frac key
>   silently meant "legacy starved leftovers" — 8 of 11 enabled value decks had no key and were running starved
>   in production, and every historical value-leaf "play rejection" was that artifact (measured escalation mean
>   committed depth ~1.4 starved vs the pure-heuristic control's ~2.1–2.35 on the matched unverified-decision
>   population).
> - **Fresh 0.5 is REFUTED, not merely superseded:** worse than legacy on the train fleet (net **+0.0018** —
>   legacy leftovers often EXCEED half the budget, so 0.5 caps the common generous case while fixing the rare
>   starved one) AND worse than control held-out **even with cap+beam engaged** (**+0.0031, t=+2.71, 12/16
>   seeds**; its depth 1.90 still sits below the control's 2.11). Only 1.0 dominated.
> - `MTG_ESCALATION_FRESH_FRAC` remains **as a research hatch only** (default **1.0**; **−1 = legacy
>   shared-leftover budget**). Every "fresh0.5", "fresh -1", "budget-restore" and per-deck-frac mention below is
>   a HISTORICAL measurement record — the numbers stand, the config does not.

> **ADOPTED + SHIPPED (2026-07-19): single-depth escalation = R-hint + live climb, per-deck `value_play.escalation_cap`.**
> The escalation now runs ONE heuristic pass at a depth chosen by a cheap FROZEN cost-per-leaf hint (R=120 default),
> then a per-decision LIVE climb/back-off corrects the depth from THIS decision's measured cost — deterministic AND
> adaptive (no cross-game `thread_local`; that was the determinism blocker with the old adaptive R). Config is one
> auto-derived integer per deck: `escalation_cap = target_depth` (emitted by `valueleaf_table_to_metadata.py`);
> R and the climb are engine defaults. Set in all 6 decks' `.value.json`. **Validation:** smoke/regression clean
> (no win→loss); overnight NET-NEUTRAL — 8 searched d5 win→loss are ALL budget-edge CHURN (each recovers to a win
> at 4× case budget, `scripts/slowdown_recover.sh`), offset by 3 loss→win + 7 turn-earlier; d0/d3 + light decks
> BYTE-IDENTICAL. GT rebaselined (`--accept-with-regressions`). **Escalation work −20..−38%** on the on-policy d5
> decisions (heaviest saving on antilife). Key mechanism findings: (a) R must be HIGH to protect light decks (a
> low hint over-shoots → fallback-thrash explosion 173–945%); the climb recovers depth UPWARD where affordable.
> (b) Tuning R DOWN to climb less is NOT worth it — climbing is cheap (TT reuse; work ~flat across R) AND a
> conservative-R incremental climb is slightly HIGHER quality than a cold deep jump (ladder-like warmup).
> **Determinism VERIFIED** (T4==T4==T1, thread-invariant). Env knobs: `MTG_ESC_SINGLE*` (research), `MTG_ESC_SINGLE_R`
> (frozen-R override), `MTG_ESC_SINGLE_CLIMB`, `MTG_ESC_CLIMB_GROWTH` (climb growth-estimate mode; see below).
>
> **BEAM-AWARE climb estimate (`MTG_ESC_CLIMB_GROWTH`) — MEASURED ON THE MERGED BASE + REJECTED (2026-07-20).**
> The toggle stays a DORMANT, default-0 (byte-identical) research knob; mode 1 is NOT adopted. mode 0 estimates the
> next climb depth's cost from the probe's UN-BEAMED leaf ratio, which over-estimates hinata's BEAMED deep-combo
> cost → the climb stops one depth short there = hinata's tiny residual. mode 1 = MEASURED beamed growth for climb
> steps ≥2 (light decks climb ≤1 step → byte-identical). **Decisive 3-way A/B (ladder=origin binary vs mode0 vs
> mode1; hinata + burn; 6 seeds 2002/3003/4004/5005/6006/7007 × 200g; d5/budget20; dLP vs the true ladder = the
> "neutrality" metric; work = deterministic turn_steps; `scripts/beam_growth_merged_ab.sh`):**
> - **hinata mean dLP-vs-ladder: mode0 = +0.0133, mode1 = +0.0125** — a 0.0008 "recovery" = pure NOISE, and
>   INCONSISTENT: mode1 recovers on 2 seeds (3003/5005), no change on 3 (2002/6006/7007), and **REGRESSES s4004**
>   (mode0 was PERFECTLY neutral +0.0000 there, mode1 made it +0.0050 WORSE). So it does not reliably make results
>   more neutral, and it can hurt.
> - **cost: +5–7% hinata turn_steps on EVERY seed** — a consistent tax on the slowest, most expensive deck.
> - **burn (light deck): byte-identical** (mode0==mode1==ladder, 100.0% work) — confirms mode1 is light-deck-safe.
> **VERDICT: reject.** No quality benefit (recovery within noise + one regression) at a real +6% work cost on the
> deck where work matters most. The shipped mode0 is already within +0.0133 of the ladder — all budget-edge churn,
> 0 win→loss (see the merged-base smoke/regression validation) — so there is essentially nothing to recover. The
> pre-merge "marginal → deferred" read is CONFIRMED and strengthened (the s4004 regression is new evidence
> against). mode 2 (cost-ratio bootstrap) not separately re-run — same mechanism, subsumed by the reject.

> **CORRECTION (2026-07-18, user directive): SINGLE-DEPTH ESCALATION IS *NOT* REJECTED.** An earlier note here
> called the single-depth pass (`MTG_ESC_SINGLE` + `MTG_ESC_SINGLE_OFFSET`) "REFUTED" and it was "dropped" in
> favor of the beam. That framing was wrong and was never the user's decision to bypass. The actual record
> (memory `escalation-levers-ab-2026-07-18`): **single-depth offset-2 was a MILD WIN on antilife** (−0.0033 LP,
> 5/6 seeds better, 0 worse, faster on 5/6; earlier "~35% faster" hint). The ONLY negative was hinata, on a tiny
> 2-seed×60g stub-table sample. It was de-prioritized when the beam+fresh0.5 combo became the focus — a lever
> choice, not a rigorous universal refutation. **The single-depth code is intact and functional; it remains an
> OPEN, per-deck candidate to be measured properly (current binary, real metric, held-out seeds) and decided
> WITH the user — it can compose with or stand apart from the beam.** See "SINGLE-DEPTH — OPEN" below.

## SINGLE-DEPTH ESCALATION — VINDICATED: 2.5–13.6× FASTER at neutral quality (2026-07-18)
**The old "single-depth is slower" refutation was measuring the WRONG offset.** `MTG_ESC_SINGLE_OFFSET=K` targets
`clamp(committed - K, 1, depth)`, and `committed` is the VALUE-LEAF's (deep, cheap) commit depth (~5). So small
offsets (1–2) aim the *heuristic* pass far DEEPER than the budget-limited ladder ever reaches -> 2–4× the work
(the "slower" everyone saw). At **offset 3–4** the pass targets the SHALLOW depth that actually decides the
escalation (off3≈d2, off4≈d1), and it is dramatically faster at neutral quality.

Measured d5 / budget 20, 4 held-out seeds (4004/5005/6006/7007); LP = mean loss-penalized avg win turn; work =
deterministic `MTG_ROLLOUT_STATS` turn_steps (seed 4004):
| deck | ladder LP | off3 LP | off4 LP | off4 speedup |
|---|---|---|---|---|
| slivers | 4.2567 | 4.2567 | 4.2567 (exact) | **7.2×** |
| knights | 4.3683 | 4.3683 | 4.3683 (exact) | **2.5×** |
| burn    | 4.3167 | 4.3167 | 4.3167 (exact) | **13.6×** |
| hinata  | 6.3937 | 6.3438 (BETTER) | 6.3937 (exact) | **3.1×** |
| antilife| 4.7083 | 4.7125 | 4.7125 (+0.004) | **3.5×** |
| th      | 4.2550 | 4.2650 | 4.2650 (+0.010) | **7.6×** |

**off4 dominates off3** on 5/6 decks (same quality, faster; slivers/knights clamp both to the same floor). hinata
is the exception: off3 is quality-BETTER (−0.05, all 4 seeds) at ~ladder cost, off4 is exact at 3× faster — a real
knee (hinata's ladder slightly over-searches). This is a FAR bigger lever than the escalation beam (1–6%), and
likely SUPERSEDES it (single-depth replaces the ladder with one shallow pass -> little ladder left for the beam to
prune). Env-gated (`MTG_ESC_SINGLE`), GT untouched. **NEXT: absolute-depth per-deck `value_play` knob (cleaner
than the relative offset since `committed` varies) + full regression/overnight held-out validation + per-deck
off3/off4 (or absolute d1/d2) decision — bring to user; no adoption without sign-off. Bounds A/B (sparse-seed,
rollout) were a red herring — the lever is the TARGET DEPTH, not the incumbent.**

## SINGLE-DEPTH — RESOLVED via a PREDICTED-AFFORDABLE single pass (2026-07-19). Clean fleet-wide win.
User directive: "go single-depth; per-deck is fine; I need an extremely strong case to give up on it." Built and
validated the predicted-affordability single pass. **This is the adoption candidate.**

**The mechanism (`MTG_ESC_SINGLE_PREDICT`, and per-deck `value_play.escalation_cap`).** Instead of the full
1..depth heuristic LADDER, the escalation runs ONE pass at the depth the ladder WOULD commit to — its budget-
affordable depth — CAPPED at the deck's convergence depth. That target is predicted from the value-leaf probe's
FREE per-depth leaf structure (`g_probe_leaves`, recorded during the probe) × an amortized heuristic cost-per-leaf
`g_esc_R`, walked through the SAME start-gate the ladder uses (`chat[d] = probe_cost[d] + R*probe_leaves[d]`, take
the deepest d whose incremental cost still fits gate_alpha*remaining). One cold pass at that target; budget-
fallback to d-1..d1 if it overruns; `g_esc_R` self-calibrates by EMA after each pass.

**THE BUG that made an earlier version under-search (and the fix).** `R` must be anchored to the probe's
CUMULATIVE UN-BEAMED leaves through the pass depth — NOT the pass's own beamed leaf count. The escalation runs
BEAMED but the walk uses the probe's un-beamed leaf counts; calibrating R on beamed leaves inflates it by the
beam-pruning factor, so the walk stops too shallow. Symptom: hinata under-searched to d1/d2 (LP +0.025). Fixed by
`R = (pass_cost − cumulative_probe_cost) / cumulative_probe_leaves`, making `chat[]`'s cumulative sum reproduce the
measured cost. After the fix the predictor tracks the ladder depth faithfully on every deck.

**MEASURED (fixed R, bare shipped configs, `MTG_ROLLOUT_STATS` turn_steps + `MTG_HYBRID_STATS` h-histogram):**
- **antilife** (4 seeds ×200g, cap 3): dLP avg = **+0.0000** (per-seed ±0.005, cancels), escalation work **35–48%**
  of the ladder (**−55% avg**). The heaviest deck; the clear win.
- **light decks** (burn/knights/slivers/th, 2 seeds ×300g, cap 3): dLP = **+0.0000 on EVERY seed** (exactly
  neutral), work **45–94%** of the ladder. The forced-fixed-depth confound (7–11× on these) is GONE — the
  predictor targets the same d1/d2 the ladder does, and is even a bit faster.
- **hinata**: `pred_fb` cap4/cap5 ≈ neutral-to-+0.01 at ~77% work (borderline; see the run — hinata's quality
  needs depth, so it is the marginal deck). Decide its cap (or leave it on the ladder, escalation_cap=0) from the
  full seed set.

**Wiring (built, default-off byte-identical).** `value_play.escalation_cap` (int, 0=off): parsed in
MulliganProfileIO, passed by AIEngine on-policy (`vp_here`), consumed in FullSearchLineHybrid. Precedence mirrors
the beam: an explicit `MTG_ESC_SINGLE` env is the research override (its `_PREDICT`/`_ABS`/`_FALLBACK` knobs); else
`escalation_cap>0` drives the per-deck predicted path (predict + fallback ALWAYS on). `escalation_cap=0` + no env =>
legacy ladder => byte-identical. **REMAINING: finalize hinata cap, set per-deck caps, full regression/overnight A/B
+ GT rebaseline + user sign-off.**

## SINGLE-DEPTH DEEP-DIVE — OPEN + PROMISING (2026-07-18/19). DO NOT call single-depth "slower"/"refuted".
Long collaborative investigation with the user. Two SEPARATE, both-valid optimizations emerged; neither is
adopted. **The user's clear direction: single-depth (one budget-limited pass) is the goal — do NOT drop it or
conclude it "does more work"; a prior naive measurement that said so was CONFOUNDED (see below).**

1. **Depth matters, converges ~d3.** Clean abort-free `value_leaf_table` heuristic_lp per depth: d1→d2 is a big
   gain (−0.05..−0.07 on combo/value decks), d2→d3 small, **d3→d4→d5 ≈ 0 on every deck**. So the escalation
   should reach ~d3 and no deeper. (th/antilife tail to ~d4; slivers/knights/burn/hinata converge exactly d3.)

2. **SINGLE-DEPTH (skip the shallow passes) is CHEAPER at MATCHED committed depth — user's intuition CONFIRMED.**
   Fair test (budget 150 so both actually reach d3; achieved-depth histogram verified MATCHED): a single d3 pass
   vs the ladder-to-d3, both committing d3: **burn 32629 vs 40510 (−20%), th 250866 vs 279624 (−10%)**. Matches
   the codebase's JUMP-ladder note ("a single cold pass is ~18% cheaper than d1..K at matched depth — the memo
   does NOT pay for the shallow passes"). CONFOUND WARNING: an earlier run comparing `abs_d3` (FORCES uniform d3)
   vs the budget-limited ladder (which only reaches d1/d2 at budget 20) showed single "1.6-11x MORE work" — that
   was NOT real; it was single searching DEEPER than the ladder did, not single being inefficient. Only compare
   at MATCHED committed depth (verify with the `h`-histogram). OPEN piece for a clean adoption: pick the budget-
   AFFORDABLE depth (<= the ~d3 convergence cap) and run ONE pass there, falling back to d2/d1 when d3 doesn't
   fit — the user's spec. Incumbent/bound experiments tried so far (warm value-leaf bound; sparse-ladder seed;
   one deep rollout at value-leaf depth `MTG_ESC_SINGLE_BOUND=3`) did NOT help the LONE pass and are a side road;
   the real result above is the matched-depth speedup. **Note: the value-leaf PROBE still searches to full depth
   (d5), so deep wins in the search window are still found even if the heuristic escalation is shallow.**

3. **Separately, capping the DEEP passes is a clean win too.** `MTG_ESC_DEPTH_CAP=3` (cap the heuristic ladder at
   d3, drop the converged d4/d5) = exact quality on all 6 decks, −16..−48% work; multi-seed neutral. This is a
   DIFFERENT axis from #2 (skip-deep vs skip-shallow) and could COMPOSE with it. It is NOT a replacement for
   single-depth.

**STATUS: OPEN (resume after compaction).** Both #2 (single budget-limited pass, cheaper at matched depth) and #3
(cap the deep end) are promising and uncommitted. All knobs (`MTG_ESC_SINGLE*` incl. `_ABS`/`_FALLBACK`/`_BOUND`,
`MTG_ESC_DEPTH_CAP`) are env-gated, default-off, GT-safe. Adoption of either needs full regression/overnight A/B +
GT rebaseline + user sign-off; per-deck depth must come from the profile's convergence depth, not a global env.

## SINGLE-DEPTH ESCALATION — OPEN (not rejected)
The escalation default is the full 1..depth iterative-deepening ladder (`FullSearchLine`). `MTG_ESC_SINGLE` runs
ONE heuristic pass at a single target depth `clamp(committed - MTG_ESC_SINGLE_OFFSET, 1, depth)` instead — the
idea being to skip the ladder's shallow-pass rework and concentrate budget on the crossover-deciding depth. It is
env-only, default off, no deck's `value_play` enables it. Status: **OPEN candidate, mild win on antilife, hinata
result inconclusive (tiny sample). To do: a proper A/B — offset sweep, all decks, held-out seeds, current
binary/metric, wall + `MTG_ROLLOUT_STATS` work — and bring the numbers to the user for a per-deck decision. Do
NOT mark it rejected.**


**Status (2026-07-18): ADOPTED on the two heavy decks (antilife + hinata) as `beam2_ld2 + fresh0.5`, per USER
("wire + full A/B now"). Smoke + regression A/B GREEN (0 searched win->loss; all d5 cases neutral-to-better;
d0/d3/light byte-identical). Overnight A/B + GT rebaseline in flight. Not yet committed.**
*(UPDATE 2026-09-03: the BEAM half of this adoption stands; the `fresh0.5` half does not. `escalation_fresh_frac`
was deleted from the engine and stripped from the sidecars — the escalation now runs fresh-FULL unconditionally,
and 0.5 measured worse than both legacy and control. See `escalation-budget-investigation.md`.)*

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
  *(UPDATE 2026-09-03: only the two beam keys survive. `escalation_fresh_frac` was deleted from `ValuePlay` and
  the `0.5` values stripped from antilife/dragonstorm/hinata2; "light decks ... fresh -1 => byte-identical" is
  no longer true either — every deck now escalates on the full fresh budget, which is exactly the silent-starve
  footgun the deletion removes.)*
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
## D3 AT PRODUCTION BUDGET 10 (2026-07-18) — the adoption-relevant measurement
The earlier d3 study was UNBOUNDED (budget 0); production d3 uses **budget 10**. Re-ran the 6-deck sweep at
`--depth 3 --budget-ms 10` (6 seeds 4004/5005/6006/7007/10010/11011 x 300 games) via `esc_fallback_ab.py`
(`ESC_AB_DEPTH=3 ESC_AB_BUDGET=10`). dLP vs the no-beam d3 baseline (neg = better; loss=9):

| deck | baseline LP (ms) | sbeam5_ld1 | sbeam7_ld1 | vbeam3_ld1 |
|---|---|---|---|---|
| antilife | 4.6494 (83) | +0.0000 faster | −0.0006 (0w) slower | **−0.0050 (4b/1w) slower** |
| hinata   | 6.1994 (1403)| +0.0105 worse | +0.0061 worse | +0.0100 worse, −443ms |
| th       | 4.2506 (29)  | +0.0011 faster| +0.0006 ~neut | +0.0039 worse |
| slivers  | 4.2350 (36)  | −0.0017 (3b/0w)| −0.0011 (0w) | −0.0011 (0w) faster |
| knights  | 4.3389 (35)  | −0.0006 (0w)  | +0.0000 (0w)  | −0.0011 (0w) faster |
| burn     | 4.3439 (32)  | +0.0000 (0w)  | +0.0000 (0w)  | +0.0011 (2w) |

**Findings (log: `logs/eval/beam_d3_b10_full.out`):**
- On **5 of 6 decks the beam is quality-safe at d3 AND faster** — deltas tiny (±0.001–0.005 on a ~4.4 base).
  At a real budget the beam is essentially a **speed lever with quality preserved**.
- **Static W7 ld1 is the safest single shallow config** (quality-neutral-or-better on 5/6, 0 worse-seeds on
  antilife/slivers/knights/burn). Static W5 ld1 nearly as safe with more speedup.
- **Value-ranked (W3) wins quality on antilife** (−0.0050) but costs speed; its antilife *speed sign flipped*
  vs the 4-seed partial → seed-noisy. Value also best on slivers/knights, worse on th/burn.
- **Hinata is the exception**: d3 beam is +0.006–0.010 WORSE but hugely faster. d3 isn't a clean operating
  point for hinata — but hinata ships at d5, so this only affects off-policy d3 cases, not production.
- **Conclusion**: d3 is "generally supported" enough for a depth-adaptive beam. Shallow config = **static
  W5–W7 ld1** (safe), deep (d5) stays the adopted **value W3 ld2** (byte-identical). Hinata d3 = documented
  speed/quality trade, not a blocker (off-policy).

## D3 OPEN ISSUES + WIDTH-LADDER INVESTIGATION (2026-07-18, RESUME HERE after compaction)

**User goal: a d3 config that is quality NEUTRAL-OR-BETTER on EVERY deck while staying faster (the adoption
bar). Address ALL d3 shortfalls, not just hinata.** At budget 10 the small quality shortfalls are:
- **hinata**: +0.006–0.010 WORSE on all beam configs (W7 least-bad); big speedup. The real issue.
- **th**: slightly worse on every beam config (sbeam7 +0.0006 best; vbeam3 +0.0039).
- **burn**: NEUTRAL on static W5/W7 (+0.0000, 0 worse); only the VALUE variant is worse (+0.0011). User still
  wants it addressed / confirmed.
- antilife/slivers/knights: already neutral-or-better on static W5–W7.

**Hypothesis:** the leaf beam (ld1 = prune only the leaf ply, already the least-aggressive setting) sometimes
ranks the winning line outside the top-W at the leaf; **WIDER width = less pruning = closer to baseline**
(W7 already less-bad than W3/W5 on hinata). The lever for ALL the shortfalls is likely WIDTH. Question: is
there a width that makes every deck neutral-or-better AND still faster? If the neutral-making width kills the
speedup, the beam isn't a clean d3 win for that deck.

**WIDTH-LADDER RESULT — RESOLVED. The wide leaf beam fixes every d3 shortfall.** Logs:
`beam_d3_light_width.out` (th/slivers/knights/burn, 6x300), `beam_d3_hinata_width.out` (hinata, 4x200),
`beam_d3_antilife_width.out` (antilife, 6x300). At **static W20 ld1, ALL 6 decks are quality-NEUTRAL
(+0.0000, 0 worse-seeds) and faster on 5/6:**

| deck | dLP @ W20 ld1 | speed | note |
|---|---|---|---|
| antilife | +0.0000 (0w) | ~neutral | value W12 = −0.0006 & −17ms if per-deck tuning wanted |
| hinata | +0.0000 (0w) | **−275ms (~23%)** | W7 +0.0112 → W12 +0.0050 → W20 +0.0000 (monotone) |
| th | +0.0000 (0w) | faster | was +0.0006 at W7 |
| slivers | +0.0000 (0w) | faster | |
| knights | +0.0000 (0w) | faster | |
| burn | +0.0000 (0w) | faster | |

**Mechanism:** W20 keeps the top-20 plans at the leaf ply, so it prunes ONLY the very widest leaf nodes — never
drops a winning line (neutral quality everywhere) yet still trims the escalation frontier. The speedup lands
where it's needed (hinata, the slow combo deck) and is negligible on the already-fast light decks. **The narrow
beam (W3–W7) was the whole problem; wide (W20) at ld1 is the d3 answer.**

**CONCLUSION: shallow (d<=4) config = static W20 ld1.** Deep (d>=5) stays the adopted value W3 ld2
(byte-identical). Next = build the DEPTH-ADAPTIVE beam (below) with these two regimes.

## DEPTH-ADAPTIVE BEAM — BUILT + VALIDATED (2026-07-18)
Implemented the two-regime auto-select in `FullSearchLineHybrid` (`src/ai/TurnSolver.cpp`) + broadened the
AIEngine beam gate (`vp_beam = value_play.drives()`, so the beam fires at ANY depth, not only on-policy; fresh
renewal stays on-policy-only via `vp_here`) *(UPDATE 2026-09-03: the fresh-renewal clause is stale — fresh-full
is unconditional engine behavior at every depth now, with no `vp_here` gate and no per-deck field; only the beam
gate remains as described)*:
- **deep (depth >= 5)**: value-ranked, the deck's own `beam_width`/`beam_leafdepth` (== the ADOPTED d5 config).
- **shallow (depth 3..4)**: STATIC, width 20, leafdepth 1 (the width-ladder answer above).
- **d0/d1/d2**: too shallow to protect >= 2 top plies (`depth < eff_leafdepth+2`) => beam OFF => byte-identical.
- The env path (`MTG_ESC_BEAM` set) stays literal (research tool, no depth adaptation).

**Validation (smoke s1001 + regression s2002/s3003, read-only vs committed GT):**
- **ALL 24 d0 + d5/d6 (production) cases BYTE-IDENTICAL** — adoption does not touch shipped play at all.
- **16 of 18 d3 cases BYTE-IDENTICAL** (the light decks' d3 frontier is < 20 wide, so W20 keeps everything).
- **Only 2 hinata d3 cases moved** (smoke s1001 gi134, regression s2002 gi15) and both are **QUALITY-NEUTRAL**:
  avg unchanged (6.0267=6.0267, 6.2150=6.2150), **win turn unchanged** (T6/T5 both ways), and across the whole
  audit **0 win->loss, 0 loss->win, 0 later, 0 earlier**. The beam pruned a different leaf line -> a later
  fetch/shuffle resolved to a different draw -> physically different but same-win-turn game.
- **Beam fires + faster at d3** (hinata d3 s4004 120g: beam-on 6.1000 / 95.9s vs beam-off `MTG_ESC_BEAM=0`
  6.1000 / 101.5s = quality-identical, ~5.5% faster this seed; the 4-seed width study measured up to ~23%).

**GT impact:** rebaseline the 2 moved hinata d3 digests only (avg unchanged) via `regression.sh --accept` for
smoke + regression. Overnight has hinata d3 cases too -> its GT goes stale (quality-neutral), defer its
rebaseline (contained change, production byte-identical). **PENDING USER APPROVAL of the GT accept.**

### SCOPE: shallow beam is d3-ONLY (2026-07-18, user-directed conservative rollout)
The shallow regime fires at **depth == 3 ONLY** -- the sole validated + suite-covered off-policy depth. d1/d2/d4
keep the beam **OFF = byte-identical to pre-adoption** (empirically confirmed: a bare adopted run vs
`MTG_ESC_BEAM=0` is digest-IDENTICAL at d1/d2/d4 on hinata/antilife/burn). **d4 is a deliberate hole**: same
W20/ld1 mechanism as d3 so plausibly neutral, but UNMEASURED -> left off until a d4 sweep confirms it (user:
"leave it off on those depths to start and see whether it makes sense to turn on after"). Widening to d4 later
is a one-line change (`else if (depth == 3)` -> `(depth == 3 || depth == 4)`) + a d4 A/B.

### ADOPTION A/B NUMBERS (d3, 4 held-out seeds 4004/5005/6006/7007 x 200g, budget 10; adopted bare vs beam-off)
Log `logs/eval/esc_fallback_ab.log`. dLP = quality (loss=9, lower=better); dms/game = speed (lower=faster):
| deck | baseline LP (ms/g) | dLP (worse-seeds) | dms/game (faster-seeds) |
|---|---|---|---|
| antilife | 4.7363 (83.6) | **+0.0000 (0)** | +1.71 (0/4) -- beam bookkeeping overhead, W20 rarely prunes a fast deck |
| hinata   | 6.2363 (954.7)| **+0.0000 (0)** | **-38.38 (4/4)** -- the real win (the slow combo deck) |
| th       | 4.2637 (23.8) | **+0.0000 (0)** | +0.27 (2/4) noise |
| slivers  | 4.2525 (28.0) | **+0.0000 (0)** | -0.59 (2/4) noise |
| knights  | 4.3513 (26.5) | **+0.0000 (0)** | +0.29 (1/4) noise |
| burn     | 4.3150 (24.4) | **+0.0000 (0)** | -0.82 (4/4) |
**Read:** quality is PERFECTLY NEUTRAL (+0.0000, 0 worse-seeds) on all 6. The wall-clock ms/game was measured
under 12 parallel workers, so the sub-ms deltas (antilife +1.71, th +0.27, knights +0.29) are CONTENTION noise,
not real work -- see the CPU-operation measurement below.

### CONTENTION-FREE CPU-WORK MEASUREMENT (`MTG_ROLLOUT_STATS`, d3, seed 4004; deterministic, thread-invariant)
Wall-clock under parallel load is contention-noisy; deterministic rollout counts are the true cost. Adopted
(bare) vs beam-off (`MTG_ESC_BEAM=0`), rollout `calls` (SimulateToEnd, ~94% of escalation cost) + `turn_steps`:
| deck | Δcalls | Δturn_steps | note |
|---|---|---|---|
| hinata  | **-105085 (-3.9%)** | **-190178 (-3.8%)** | the real win (slow combo deck) |
| slivers | -6512 (-2.4%) | -8172 (-1.8%) | less work |
| knights | -1621 (-0.6%) | -2763 (-0.6%) | less work |
| th      | -1122 (-0.4%) | -1203 (-0.3%) | less work |
| antilife| -29 (-0.01%)  | -31 (-0.007%) | ~identical (narrow d3 frontier -> W20 prunes ~nothing) |
| burn    | 0             | 0             | EXACTLY identical (frontier always < 20) |
**The beam does LESS-OR-EQUAL CPU work on EVERY deck -- never more.** The earlier wall-clock "antilife +1.71ms /
th +0.27ms slower" was pure scheduler contention (antilife's real delta = 29 fewer rollout calls out of 229k).
So by CPU operations the adoption is neutral-to-better ACROSS THE BOARD (strictly less on hinata/slivers/knights/
th, ~0 antilife, 0 burn), quality perfectly neutral, production d5/d6 + d1/d2/d4 byte-identical. Log:
`MTG_ROLLOUT_STATS=1 ... --depth 3` prints `[rollout-stats] calls=.. turn_steps=.. interior_esc=..`.

## DEPTH-ADAPTIVE BEAM (build after the width question is settled)

**Task: build a DEPTH-ADAPTIVE beam so d3 (and any depth) is generally supported.**

### Why: the beam's optimal ordering/width is depth-dependent
Escalation is *verification* at deep search (value leaf reliable) but *primary* at shallow search (leaf weak).
Measured d3 (all decks escalate 30-58% at d3 vs 2-8% at d5 -- escalation fires on UNVERIFIED decisions, and a
shallow search verifies far fewer):
| deck | best d3 config | quality Δ (loss=9) | speedup |
|---|---|---|---|
| hinata | static W5 ld1 | 0.000 (EQUAL; value-ranked = WORSE 6.53 vs 6.40) | ~2x (5.2->2.6 s/game) |
| slivers | value W3 ld1 | 0.000 (equal) | 83% |
| knights | value W3 ld1 | 0.000 (equal) | 78% |
| antilife | static W5 ld1 | +0.0017 | 51% |
| burn | static W5 ld1 | +0.0013 | 59% |
| th | static W5 ld1 | +0.0006 (value W3 was +0.0069) | 37% |
Two lessons: (1) **STATIC MoveOrder pruning beats value-ranked at d3** -- the shallow leaf is a bad ranking
proxy so the value reorder picks worse lines (hinata d3 value 6.53 vs static/baseline 6.40; TH static +0.0037 vs
value +0.0069). Even at no-prune width the value reorder alone drifts quality. (2) **ld1 is essential at d3**
(only 3 plies; ld2 protects just the root -> +0.0175 on TH); wider width (W5) recovers most residual. hinata d3
legacy is impractically slow (5.2 s/game) -> the beam is ~required there.

### The design to build
Beam settings should auto-select by SEARCH DEPTH (the FullSearchLineHybrid `depth` param), removing per-depth
tuning:
- **deep (depth >= 5)**: value-ranked, width 3, leafdepth 2  (== the ADOPTED d5 config, unchanged/byte-identical)
- **shallow (depth <= 4)**: STATIC, width 5, leafdepth 1
Implementation sketch: in FullSearchLineHybrid, when the per-deck beam drives (vp_here / eff_beam>0), pick
(width, leafdepth, static) from `depth` instead of fixed values -- OR add value_play fields for a shallow
profile. Keep the existing d5 path byte-identical (regression/overnight GT must not move). Then A/B at d3 across
all decks (static W5 ld1) confirming equal-or-near-equal quality + big speedup, and re-confirm d5 byte-identical.
Optional follow-up: shrink the TH/burn/antilife d3 residual (+0.0006..+0.0017) via wider width or per-game look.

### Build state for resume
- **COMMITTED + PUSHED**: `f517bb3` = beam3_ld2_fresh adoption (antilife+hinata) + GT rebaseline. On
  `phase-1-2-deck-analyzer`.
- **UNCOMMITTED (byte-identical off, safe)**:
  - `src/ai/TurnSolver.cpp`: `MTG_ESC_BEAM_STATIC` toggle (`g_esc_beam_static`, gates the value reorder off ->
    static MoveOrder pruning). Built + in the current binary. This is the shallow-depth ordering lever.
  - `scripts/esc_fallback_ab.py`: `ESC_AB_DEPTH=D` (runs `--depth D --ignore-play-profile`); `_v(...,static=)`;
    `_OFF` baseline forces true legacy (`MTG_ESC_BEAM=0 FRESH=-1`).
- **Env knobs**: `MTG_ESC_BEAM=W`, `MTG_ESC_BEAM_LEAFDEPTH=D` (INT_MAX=uniform), `MTG_ESC_BEAM_STATIC=1`,
  `MTG_ESCALATION_FRESH_FRAC=f`. Env-path beam is literal (no depth guard); the per-deck path has the
  `depth >= beam_leafdepth+2` guard + the AIEngine `vp_here` (depth==target) gate.
  *(UPDATE 2026-09-03: `MTG_ESCALATION_FRESH_FRAC` is a RESEARCH HATCH only now — default 1.0 (fresh-full,
  the shipped behavior), −1 = legacy shared-leftover. It has no per-deck counterpart to override.)*
- **d3 study logs**: `logs/eval/beam_d3_light.out` (th/slivers/knights/burn), `beam_d3_width.out` (width sweep),
  `beam_d3_static.out` (static vs value), `beam_d3_antilife.out`, `beam_d3_hinata.out` (partial; baseline slow).
- **Pending (separate, user wants fixed independently)**: the batch per-thread-AIEngine knife-edge -- games are
  not independent because GoldFishRunner.cpp:262 reuses one AIEngine per thread across games; a game deep in a
  batch differs from its `--game-index` isolated replay. This is what makes the residual overnight win->loss
  (both win in isolation). Pre-existing, not beam-caused.

---
## (original investigation notes below)

## The goal (user-directed)
Make the value-leaf hybrid's heuristic **escalation** match the original (baseline) quality but run faster, by
**reusing everything practical from the value (first/probe) pass and paying only for the incremental heuristic
rollouts**. A cold "single-depth" pass measured slower on hinata here (it throws away the ladder's move-ordering
+ leaf memo) — but this was NOT a universal refutation (it was a mild WIN on antilife) and single-depth is an
OPEN lever, not rejected; see the CORRECTION at the top of this file. The BEAM keeps the ladder and prunes its
WIDTH: expand only the top-W plans per node, so the
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
  *(UPDATE 2026-09-03: this bullet's mechanism was right and its CONFIG was wrong. The lever is real — starvation
  was the whole story — but 0.5 is the wrong number and a per-deck field is the wrong home: it is REFUTED against
  both legacy (+0.0018 train fleet) and control (+0.0031, t=+2.71 held-out with cap+beam), and the field is
  deleted. Fresh-FULL is now unconditional engine behavior. The measurements above stand as a record of the
  starved baseline they were taken against; do not treat "fresh0.5" as a live or wired option. See
  `escalation-budget-investigation.md`.)*
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
