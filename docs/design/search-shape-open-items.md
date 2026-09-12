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

**Do not turn `escalation_fresh_frac` into a constant.** That was tried and reverted — see §4.

---

## 4. CLOSED — decided, with the reasoning, so they are not re-litigated

**Melira is a null, not a trade.** `fit_alpha` is *not* what failed: plain `single` on the current
engine is **+0.0200 ± 0.0036 worse** (82 better / 163 worse) and `fit_alpha: 4.0` still repairs almost
all of that — a **+0.0225 swing**. So the diagnosis reproduces exactly: FIT's affordability gate admits
only 1.10x remaining where the ladder admits ~5x (telemetry `completed=7103 overruns=0` — FIT never once
attempted a pass it might not finish). What changed is the **destination**: the repair now lands *level*
with the pre-adoption ladder at identical cost, instead of ahead of it. Shipping three per-deck keys to
arrive where the simpler config already sits buys nothing. Declining a null is not a trade for the user.

**`escalation_fresh_frac = 0.5` must stay a per-deck key, default `-1`.** Hardcoding it was recommended,
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
