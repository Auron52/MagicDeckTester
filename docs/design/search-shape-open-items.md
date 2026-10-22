# Search shape: what shipped, what is open, and how to pick each one up

**Written 2026-09-12, at the end of the one-depth adoption session.** This is the standalone
pick-up-later document: everything needed to resume without reconstructing context. It is deliberately
self-contained — the detailed evidence tables live in `shape-adoption-decisions.md` (the decision
sheet) and the lever inventory in `per-deck-levers.md`, and this file says which section to read for
what, but it does not depend on anything outside git.

---

## 1. Where things stand

**Shipped and pushed** (branch `phase-1-2-deck-analyzer`, commits `0068592e`, `a2eb2e0c`, `35192f22`):

* `ladder: "single"` (the FIT one-depth pass) **on Dragons and Minotaur only**.
* `MTG_ESC_FIT_LAZY_R` default **ON**; `MTG_ESC_FIT_ALPHA` / per-job `esc_fit_alpha` and per-deck
  `value_play.fit_alpha` / `fit_lazy_r` exist but **no deck sets them**.
* `MTG_ESC_FIT_REACH` **deleted** (measured inert).
* All three GT tiers (smoke / regression / overnight) accepted on this binary.
* CI green on **ubuntu-latest, windows-latest, and the Linux/Windows determinism-parity job**.

**Not shipped, and the reason matters:** three of the five adoptions that were staged did not survive a
rebase onto ~250 files of upstream work. See §2 — it is the single most important thing in this file.

---

## 2. THE GOVERNING LESSON: cost-axis results do not survive an engine rebase

The rebase brought in `perf(search)` winless-turn certificates, `perf(mana)`, `perf(rollout)`
unbudgeted-leaf memo, and `MTG_OF_WAVE_SHARE` 0.005 → 0.0005. Re-measuring the same five adoptions on a
fresh third seed base (14500000), 16×250 per cell:

| deck | before rebase | after rebase | outcome |
|---|---|---|---|
| dragons | 0/0 @ 0.613x | **0/0 @ 0.759x** | still a clean win — **adopted** |
| minotaur | 0/0 @ 0.964x | **0/0 @ 0.893x** | still a clean win — **adopted** |
| hinata | −0.0080 @ 0.991x | −0.0127 @ **1.245x** | flipped to a TRADE — reverted |
| fivecolour | −0.0050 @ 0.995x | −0.0085 @ **1.021x** | flipped to a TRADE — reverted |
| melira `single`+`a4` | −0.0125 ± 0.0040 | **−0.0025 ± 0.0026** | null — reverted |

**One mechanism explains all five rows: upstream made the LADDER cheaper too**, and FIT's advantage was
partly that it does less work. Note what did *not* happen — **no deck's quality got worse**; hinata's
and fivecolour's gains actually grew. Every reversal was the cost axis moving underneath a decision.

Three consequences to carry forward:

1. **After a rebase that replays engine changes, re-run the A/B — not just the byte-identity smoke.**
   Smoke proves *your* diff is inert. It says nothing about whether a *delta* you measured still holds
   once the other arm's cost moved. This check cost ~45 minutes and stopped three unsupported
   adoptions from shipping.
2. **"Identical play, cheaper" is the only robust win shape.** Dragons and minotaur survived because
   the only thing that *can* move is the size of the cost win. Every marginally-cheaper win flipped.
3. **A replicated diagnosis does not protect the adoption built on it** (see §4 on Melira).

**⚠ This invalidates the cost figures in every pre-rebase table**, including the §4 trades below. Treat
every `units` number measured before 2026-09-12 as provisional until re-measured.

---

## 3. OPEN — awaiting a user ruling (nothing here should be adopted unilaterally)

### 3a. hinata and fivecolour `ladder: single` — the two live trades

Measured post-rebase, so these numbers are current:

| deck | quality | units | shape |
|---|---|---|---|
| hinata | **−0.0127 ± 0.0033** (3.8σ better) | **1.245x** | real quality gain, 24.5% more work |
| fivecolour | **−0.0085 ± 0.0017** (5.0σ better) | **1.021x** | real quality gain, 2.1% more work |

They are reverted because a clean win must not regress on **any** axis, so a trade is the user's call
and stays reserved until made. Note fivecolour's is a *much* cheaper trade than hinata's — they do not
have to be ruled on together.

**To act on a ruling, no re-measurement is needed.** Insert `"ladder": "single", ` immediately after the
`"value_play": {` token in `decks/Hinata2/Hinata2.value.json` and/or
`decks/FiveColour/FiveColour.value.json` — a *textual* insert, so the model floats elsewhere in the file
are left untouched. Then rebuild, re-run all three tiers, classify any searched-slower games, and accept.
Expect d3 **and** d5 cases to move (`ladder` is tier-1, applied at load) and d0 to be untouched.

One thing to be aware of on hinata specifically: `single` loses under d3/b10 (+0.0125 ± 0.0023) while
winning at play settings. There is a standing user ruling that play settings outrank the d3 mode — but
that ruling was given when `single` was also *cheaper*, which is no longer true, so it should not be
assumed to carry over.

### 3b. The pre-rebase trades — all need re-measuring before they can be ruled on

From the decision sheet §4, measured on base 9910000 **before** the rebase. Every one of these is a
cost-axis judgement, which is exactly what §2 says is now unreliable:

| deck | change | quality (pooled) | units (STALE) | status |
|---|---|---|---|---|
| cgiving | `nocap` (*more* ladder, not less) | −0.0045 ± 0.0012 (3.7σ) | 1.13x | the one trade that replicated exactly |
| fluct | `exhaust_mult: 4` | −0.0013 ± 0.0003 (4.1σ) | 1.28x | halved on held-out but real |
| stompy | `exhaust_mult: 4` | −0.0010 ± 0.0003 (3.2σ) | 1.19x | halved on held-out but real |
| kitty | `ladder: single` | −0.0017 ± 0.0007 (2.3σ) | 1.18x | **shrank to a null on held-out** (−0.0005 ± 0.0012) |

Recommendations as they stood, still reasonable but now needing fresh cost numbers:

* **cgiving `nocap`** is the strongest of the four and points *against* one depth — Creature Giving
  measurably prefers the **full ladder**. Worth re-measuring first.
* **kitty `single`: recommend NOT adopting.** The pooled 2.3σ is driven entirely by the training half;
  held-out is 0.4σ (8 better / 6 worse). This reverses a lean the user had expressed, so it is flagged
  rather than assumed.
* **`exhaust_mult`** is a real monotonic dial (2 → 4 buys more for more) that **no deck uses**. A real
  option, just a dearer one than the first screen suggested.

### 3c. Key deletions available (decision sheet §7)

These reduce the knob count without losing measured quality. None is urgent; all are the user's call:

* `ladder: "emulated"` — **dead** (worse on all 20 decks, up to 108.6x wall for byte-identical play).
* `commit: "model"` — **unearned**, and its earlier "earns its key" claim was *retracted* (the arms
  moved three keys at once, so the attribution was invalid). No deck sets it.
* `regime` — parsed, stored, **never read** by any decision. Fold into the `note` fields.
* `alpha` — set by exactly the 8 decks that set `leaf: none`, always `"relaxed"`. Fold into `leaf: none`.
* `beam_width` / `beam_leafdepth` — single-valued (3 and 2) on the only 4 decks that set them.
* `escalation_cap` — **inert under FIT** (byte-identical digest *and* units, 16/16, on dragons and
  minotaur). Retain only as a fast-deck depth ceiling; see §5.
* `escalation_r` — superseded under FIT (calibrated per game), but still read by `TrustPathGuard`, so
  it cannot simply be deleted.

~~**Do not turn `escalation_fresh_frac` into a constant.**~~ **SUPERSEDED 2026-10-22 (USER: *"The escalated shape was meant to always use 0.5"*; the 2026-09-11 ruling was "make the fresh escalation budget unconditional").** The key is DELETED and the escalation always runs on a fresh 0.5 x budget_ms **as a floor** (the probe's leftover when larger), every deck, every depth. The 2026-09-12 revert below settled a cost trade (treasure_hunt 1.083x units, z +0.71) that was the USER's to make and was never surfaced; meanwhile every deck without the key escalated on leftovers (Kitty, Melira: the escalation committed depth 1 or was skipped). Held-out 2026-10-22 vs the leftover (`logs/tgt/fresh05` in the lf-target work tree): play settings, 26 decks x 500, floor -0.0012 (14 faster / 1 slower), flat 0.5 -0.0011 (15 / 4); d3 b10, 33 decks x 300, floor -0.0004 (5 / 1), flat 0.5 +0.0027 (10 / 32) -- a flat allowance under-funds the escalation after a cheap probe, hence the floor. USER 2026-10-22: *"0.5 floor probably and 0.5 default certainly."* -- the 0.5 is settled; the floor (vs a flat 0.5) is the USER's provisional choice, open to a later re-measure.

### 3d. Under the 0.5 floor, the escalation's next pass is over-priced (2026-10-22)

**Status: DECIDED 2026-10-22 -- keep today's shape (see "Outcome" at the end of this section).**

**What the floor cost, and where it went.** At play settings the floor costs, against the old leftover
budget: Bruna 1.12x units, treasure_hunt 1.11x, Auras 1.11x, slivers_vial 1.08x, Prevent Damage 1.06x,
everything else 1.04x or less. On Auras, slivers_vial, Giants, Fluctuator, Knights, CritterLifegain and
Soldiers, play is byte-identical over 500 games (same digests). Two kinds of work make up the extra:

* **Taken escalations that went deeper.** These are the rule working as intended. They changed play only
  on Bruna, treasure_hunt and Prevent Damage, each 1 faster and 0 slower in 500 games.
* **Discarded escalations.** These are the take rule throwing the line away (24-85% of the extra; see
  `escalation-skip-calibration-stage.md`, floor re-calibration). Under the leftover budget they were
  starved and cheap. Now each one spends real budget and is still discarded.

**Why they are discarded: the next pass is over-priced, not unaffordable.** On the ladder decks most
discards stop with more than half of the fresh budget unspent:

| deck | escalations discarded with >50% of the budget left |
|---|---|
| Prevent Damage | 538 of 642 |
| Bruna | 1048 of 1431 |
| Vampires | 63 of 72 |
| Giants | 42 of 50 |

The start gate extrapolates the next pass from the last one (kDefaultGrowth 6x at pass 2, then the
measured ratio). Real rollout cost per probe leaf FALLS with depth:

| deck | units per leaf, d1 / d2 / d3 / d4 |
|---|---|
| Prevent Damage | 282 / 145 / 75 / 58 |
| Bruna | 137 / 64 / 36 / 21 |
| Selesnya | 146 / 68 / 35 / 23 |

Deeper leaves are closer to the end of the game, and the shared tail rollouts hit more. So the gate
over-estimates the refused pass by a median 1.7x (PD), 2.5x (Bruna), 2.1x (Selesnya) and 5.2x (Giants).
Price that pass at the deck's measured per-depth cost per leaf instead, and it would have fit in about
half the cases: PD 46%, Bruna 52%, Selesnya 52%, Giants 78%. That estimate has a known bias: the price
comes from escalations the gate admitted, which skew cheap. The single path's climb shows the same thing.
A discarded single-path escalation leaves a median 55-92% of its budget unspent (slivers_vial: the d1
pass costs ~1.2k of ~13k units, and the climb refuses d2). The same over-estimate on Kitty, 8.8x at pass
2, is what made Kitty's starved escalations commit depth 1.

**The two levers compete for the same escalations.**
* **Skip** (escalation-skip allowance, E.6): cheaper, play unchanged, and it gives up the escalation.
* **Re-price** (a per-depth cost-per-leaf model in the escalation's start gate and climb, fitted per deck
  like `escalation_skip_r`): costs more, lets more escalations reach T and be taken, and so spends the
  0.5 the USER asked the escalation to have.

Re-pricing is a price, so it is inert at unlimited budget (the budget rule's good shape). It changes
budgeted play on every hybrid deck, so it needs a held-out A/B per deck. It is likely a trade (better
play for more work), which makes it the user's call.

**Outcome (2026-10-22).** USER: *"We definitely do not want to do any work on discarded escalations.
Anything we escalate to should be a depth a which we expect better quality."* Then, on cost: *"if
anything it should provide a cost savings or do nothing if we already do this."* Three shapes were
built as research arms and A/B'd at play settings on the 8 ladder-path hybrid decks (Bruna, Prevent
Damage, Selesnya, Vampires, Fluctuator, Giants, Mirrorwing, Soldiers), 1000 paired games each, seed
8.3M. The code is NOT in the tree; it is preserved as `logs/tgt/fresh05/esc_to_take_research.patch` in
the lf-target work tree, with the drivers `totake.py`, `arms.py`, `predict.py` and `rfit` beside it.

| shape | turns vs today | faster / slower | units |
|---|---|---|---|
| force to T (the gate may not stop the ladder below T) | -0.0034 | 24 / 0 | 1.199x |
| predict, then force (cutoff 1.1x budget) | +0.0006 | 13 / 16 | 1.056x |
| predict, then force (cutoff 2x) | -0.0024 | 20 / 3 | 1.150x |
| predict and skip, no forcing (cutoff 1.1x) | +0.0025 | 1 / 17 | 0.967x |
| predict and skip, no forcing (cutoff 1.5x) | +0.0010 | 1 / 7 | 0.979x |
| predict and skip, no forcing (cutoff 2x) | +0.0005 | 1 / 4 | 0.988x |

The predictor is the sum over passes 1..T of probe_cost[d] + R_d x probe_leaves[d], with R_d the
deck's median per-depth rollout cost fitted on the E.6 probe (seed 7.1M).

* **Forcing removes discards, but by spending past the budget.** Forced escalations spend 2-21x the 0.5
  allowance, and 29 of the first run's 31 faster games (22 decks, seed 8.1M: -0.0014 pooled, 31 / 4,
  1.113x) contain one of them. So its quality gain means "more escalation budget helps Bruna and
  Prevent Damage". It does not mean the gate mispriced the pass: the gate's refusals were mostly right.
  On the single-depth path forcing turned discards into overruns at T, which is still waste.
* **Skipping saves work but is lossy.** The per-decision cost of reaching T cannot be predicted well
  enough: the model is right ~80-93% of the time, so any skip rule also drops escalations that would have
  reached T and changed the game.
* **Today's up-front skip already is this, tuned to the lossless edge.** That is the fleet R = 0 plus the
  E.6 per-deck allowances, re-fitted under the floor the same day with a no-slower-game bar. USER: *"I
  think it's okay not to force it if we are already doing something like this."* Nothing adopted. The
  remaining discarded work (3.5-7% on these decks) is the price of prediction error. Shrinking it
  losslessly needs a better predictor (features beyond the probe's tree size), not a different shape.
* **Open, not ruled on:** Bruna and Prevent Damage measurably gain from a larger escalation budget
  (forced: -0.011 and -0.012 turns, 10 / 0 and 11 / 0). That is a per-deck budget-setting question. It
  is not about the escalation's shape.

---

## 4. CLOSED — decided, with the reasoning, so they are not re-litigated

**Melira is a null, not a trade.** `fit_alpha` is *not* what failed: plain `single` on the current
engine is **+0.0200 ± 0.0036 worse** (82 better / 163 worse) and `fit_alpha: 4.0` still repairs almost
all of that — a **+0.0225 swing**. So the diagnosis reproduces exactly: FIT's affordability gate admits
only 1.10x remaining where the ladder admits ~5x (telemetry `completed=7103 overruns=0` — FIT never once
attempted a pass it might not finish). What changed is the **destination**: the repair now lands *level*
with the pre-adoption ladder at identical cost, instead of ahead of it. Shipping three per-deck keys to
arrive where the simpler config already sits buys nothing. Declining a null is not a trade for the user.

(SUPERSEDED 2026-10-22, see §3: the key is deleted.) **`escalation_fresh_frac = 0.5` must stay a per-deck key, default `-1`.** Hardcoding it was recommended,
implemented, and reverted within a day: the fleet screen said "free or better on all 13 decks that lack
it", but `treasure_hunt`'s own **later, on-policy** re-screen records 0.5 as **REJECTED at 1.083x units**.
A default reaches that deck *precisely because* it omits the key, so the constant was silently overruling
the deck's own better-targeted measurement. Isolation was exact — forcing `-1` reproduced the committed GT
digests byte-for-byte for `auras_regression_d5_s3003` and `th_regression_d5_s2002`.

The generalisable trap: **a gated key tested outside its gate produces a wall of IDENTICAL that reads as
proof and is worthless.** `escalation_fresh_frac` is tier-3 (`vp_here` = `drives() && lookahead_depth ==
target_depth`); the inertness probe behind the "behaviourally inert" claim ran at depths that never reach
that gate. Before believing any null on a gated key, assert the gate actually fired — and note that even
a correctly-gated check can miss a rare effect: auras' smoke case *is* at its target depth, but 250 games
missed what the regression tier's 500 caught.

**`MTG_ESC_FIT_REACH` is deleted** — byte-identical at alpha 1 and numerically identical to the plain
alpha arms at 2/4/8. FIT's structural cap never binds; the budget gate binds first.

**`ladder: "emulated"` is dead** (§3c).

---

## 5. Still not measured

* **A fast-deck depth ceiling for FIT.** FIT is cheap where it *finds* something and expensive where it
  changes nothing — breaching is **4.655x units for not one changed game of 4,000**, because it spends a
  deep rollout on a deck that wins on turn 3.1. `escalation_cap` is exactly the knob for this, and it is
  the one role the "inert under FIT" result does **not** cover (that was measured on dragons/minotaur,
  where FIT already stops shallow). This is the most promising unexplored item here.
* **Unconditional frac** (applied at every depth, not only `target_depth`). Every frac measurement to
  date applied it at `target_depth` only, so a constant applied everywhere is broader than anything
  measured.
* **`escalation_r` necessity as a standalone per-deck value.** A 120k-game A/B moved only Goblins
  (74 → 240); FIT may retire it anyway.
* **The 14 stale crossover matrices** (`value_fallback_crossover`) — self-declared stale on 14 of 20
  decks and never regenerated.

---

## 6. How to run the measurement, concretely

The apparatus from this session is reusable and lives in `logs/modes/` (gitignored, so treat these as a
recipe rather than files you can count on finding):

* **Generator** — build one manifest with every arm of every deck pooled into **one** `mtg --batch`.
  Never a loop of per-arm invocations: env statics are process-global, which is why per-job `ValueArm`
  overrides exist (`esc_fit_alpha`, `esc_fit_lazy_r`, `esc_depth_cap`, `of_wave_share`, …), precedence
  **arm > deck > env**. For a sidecar-key arm, synthesize a variant sidecar and point the job at it with
  `value_profile`.
* **Assert every arm moves only its declared keys.** A prior screen invalidated its own conclusion by
  running arms that moved three keys at once. The generator should diff the synthesized sidecar against
  the shipped one and assert the moved-key set exactly matches what the arm claims, and that no other
  sidecar block changed.
* **Always include an above-range control arm** — it is what proves the wiring rather than assuming it.
* **Sizing** used throughout: 16 blocks × 250 games = 4,000 games per cell; `se` computed over the 16
  block means. Run with `MTG_DUMP_UNITS=1 MTG_DUMP_WINS=1` and `--game-log-dir` so per-game win turns
  and unit counts are available for the better/worse counts.
* **Judge cost on UNITS, never wall** — every screen shares a box and the plan-cache pool is global.
  `B == W == 0` proves neutrality outright and needs no significance test.
* **Check utilisation in the first ten minutes** (`[batch] heartbeat: N/M workers busy`, or just CPU% —
  ~3000% of 3200% is right on this box). If it is not near M/M, fix the scheduling before continuing.
* **Diagnostic that cracked Melira: compare arms at MATCHED depth.** Capping the ladder and capping FIT
  to the same final depth (`esc_depth_cap` / `esc_fit_depth_cap`) gave depth 1 → −0.0040 ± 0.0048 (a coin
  flip), depth 2 → +0.0065, depth 3 → +0.0080. Depth 1 is the one depth where iterative deepening has no
  climb to make, so the gap being *born* at depth ≥2 proved machinery, not depth. Cheap and decisive
  where whole-config A/Bs were not.

**Rebaseline scope for any adoption here:** all the decks under discussion are in **all three** tiers at
depths 0/3/5. `ladder` is tier-1 (applied at load), so it moves **d3 as well as d5**; `escalation_fresh_frac`
is tier-3, so it moves **d5 only**. d0 has no search and should be byte-untouched — that is a useful
self-check on any diff. If a rebase has intervened, **reset GT wholly to origin first** (`git checkout
origin/<branch> -- test/gt_logs test/regression_gt.txt`) so the per-game audit is attributable: a rebase
splits the two GT halves (conflicted files take one side, non-conflicting ones replay the other), and
auditing against that chimera produces phantom "slower" games. Verify with `python3 test/check_gt_logs.py`.

---

## 7. Two claims from this session that were wrong, recorded so they are not repeated

* **"The `escalation_fresh_frac` default change is behaviourally inert."** It was not — see §4. The
  probe ran outside the key's activation gate.
* **"The FIT gate relaxation will generalise to the cost-failing decks (breaching, critter)."** It does
  not. Their play is unchanged across all arms and alpha is pure added cost.

And one from the sheet's own history, worth keeping visible because it was load-bearing: *"the ladder
commits mean 1.39"* came from a **33-decision** sample; over 30,701 decisions it is **3.06**. That stale
figure was the only reason "the ladder wins by being shallow" ever looked plausible.

A warm leaf table can never explain a **quality** gap: the leaf table is asserted byte-identical to
`nullptr`, so a sound memo acts only through **cost** — and cost is exactly what FIT measures. Use that
argument to kill cache explanations for quality differences.
