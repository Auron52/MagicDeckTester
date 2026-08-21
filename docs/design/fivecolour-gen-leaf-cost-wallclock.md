# Wall-clock test: does the value leaf still pay in the FiveColour gen labeller at d2/b1?

> ## STATUS 2026-08-21: ALL ANCHOR NUMBERS BELOW ARE STALE — the "2x slower on HEAD" report is
> REAL, VERIFIED, and ATTRIBUTED to the MTG_5C_PHASE adoption (b7f308a, 2026-08-19)
>
> The secondary machine reported FiveColour ~2x slower on HEAD vs 9188509 (this doc's commit).
> Verified on the primary box, idle, interleaved worktree-vs-HEAD pairs, the protocol's own
> run command:
>
> | config | 9188509 | HEAD | ratio | HEAD avg vs OLD avg |
> |---|---|---|---|---|
> | d2/b1 s1001 | 21.1 s | 20.8 s | 1.0x | 5.1750 = 5.1750 |
> | d2/b1 s2001 | 24.7 s | 63.9 s | **2.6x** | 4.8250 < 4.9000 |
> | d2/b1 s3001 | 32.0 s | 73.9 s | **2.3x** | 4.9667 < 5.0833 |
> | d2/b1 s4001 | 55.1 s | 72.7 s | 1.3x | 4.8833 < 4.9583 |
> | d3/b3 s1001 | 31.7 s | 51.1 s | 1.6x | 5.0417 < 5.1750 |
> | d5/b20 s1001 (30g) | 32.2 s | 43.3 s | 1.35x | 5.1000 < 5.2000 |
>
> Pooled d2/b1 over the four seed blocks: **1.74x** — "about 2x" confirmed. HEAD plays BETTER
> on every block (the cost buys quality). Seed 1001 (the protocol's primary seed) happens to
> dodge the cost almost entirely — do not calibrate from it alone.
>
> CAUSE, isolated by hatch sweep + commit bisection (midpoint a85ccf6 ≈ OLD speed):
> **MTG_5C_PHASE=0 recovers it** (d3/b3: 51→34.6 s; d2/b1 s2001: 64→29.1 s), and within phase
> the dominant term is the COLLAPSED-MAIN UNCAPPED FETCH FAN (`MTG_FETCH_FAN_CAP=2` recovers
> below even phase-off: d3/b3 51→27.0 s, s2001 d2/b1 64→21.1 s). MTG_5C_ORDER=0 is inert.
> Small contributors on top: colour-exact (~10%), SSM (~4%), each quality-bearing. The 1.35x
> at play depths was KNOWN AND ACCEPTED at the phase adoption (held-out 12/12 green, d5
> −0.113); what was not known is that the fan multiplier hits shallow gen searches 2-3x on
> fetch-heavy seed blocks. The intended claw-back (condemnation −34%) proved UNSOUND; sound
> condemnation is perf-neutral, so the phase cost stands unoffset.
>
> RE-BATTERY RESULT (2026-08-21, same 200-hand/R=30 apparatus, current engine, primary box):
>
> | arm | units/rollout | vs pre-phase H21 (10,804) | rho play | shift | sd | pair-agree |
> |---|---|---|---|---|---|---|
> | REF hybrid d6/b20 | 126,584 | (old REF 92,610 → 1.37x) | 1 | 0 | 0 | — |
> | H21 d2/b1 | 25,181 | **2.33x** — the phase cost in units | 0.9906 | +0.054 | 0.093 | **100.0%** (11,412) |
> | **H21 + MTG_FETCH_FAN_CAP=2** | **9,441** | **0.87x — cheaper than pre-phase H21** | 0.9867 | +0.085 | 0.105 | **100.0%** (11,412) |
>
> Three conclusions:
> 1. The units currency confirms the secondary box exactly: H21 costs 2.33x what it did when
>    adopted. The wall ratios above and this number are the same fact.
> 2. **H21 itself RE-VALIDATES under the current engine** (rho 0.9906 — better than its
>    original 0.9891 — and 100% pairwise agreement): the labeller's fidelity claim is
>    refreshed, no longer stale.
> 3. **H21+fancap2 is fidelity-equal by this sweep's own standards** — 100.0% pairwise
>    agreement on all 11,412 separated pairs (the bar cap=1 FAILED at 99.1%/rho 0.9567), rho
>    within a hair of the H21/CUR band, dispersion 0.105 in the same class — at **2.67x less
>    work than current H21** and 13% below even the pre-phase cost. The uncapped fan's extra
>    lines change which play the search finds (in-play quality, the reason the override
>    exists) but do NOT change how hands RANK at gen depth.
>
> ADOPTION (USER call, they were asleep when this ran): a fan-capped GEN arm recovers the
> whole regression. Scoping caveat stands: cap the fan for LABEL ROLLOUTS only — discovery
> keeps shipped play. If the gen driver runs labelling as its own process/phase, env
> MTG_FETCH_FAN_CAP=2 on that phase suffices; otherwise it wants a sidecar-scoped field
> (mull_gen_fetch_fan_cap beside mull_gen_depth). Side observation for gen cost: the REF arm
> showed 18,643 enum-memo cap clears at the 8192 default — MTG_ENUM_MEMO_CAP=262144 is worth
> setting on shipped-depth gen/discovery work.
>
> **MEMO-CAP RECOMMENDATION REFUTED (2026-08-21, measured on 0a59457's successor 2fd017b).**
> The line above is stale: its evidence (18,643 clears) was collected under shipped play =
> UNCAPPED fetch fan. On the current binary (fetch top-entry-only adopted) the same scorer
> config (fivecolour d6/b20, R=30, 40 hands, alternating pairs ×2, `logs/memo_cap_ab/`):
> default-cap 755 clears / 12.8% hits / 66.8s / 635 MB vs cap=262144 0 clears / 14.2% hits /
> 66.9s / 921 MB — outputs byte-identical (md5-equal all four arms), wall DEAD EVEN, +45% RSS.
> The fan cut shrank the per-decision state space enough that clear-on-full no longer costs
> measurable wall at the budgeted gen tier; do NOT set MTG_ENUM_MEMO_CAP on gen/scorer work.
> Still open (unmeasured, expensive): UNBOUNDED deep cells (value-leaf phase C monsters — the
> census game hit ~130k distinct fs6 states/decision at 1% hit rate under the default cap on
> the OLD play; whether that survives the fan cut needs a monster-cell A/B, not a scorer one).
>
> FOLLOW-UP (2026-08-21, USER: "I meant for our heuristic to be used with just the top entry,
> not split and search there. Can we try that?") — cap=1 TRIED, full measurement, plus cap=2
> through the SUITE for the complete trade table (train seeds, 4 fivecolour searched keys,
> GT = shipped uncapped fan):
>
> | arm | suite quality vs GT | play wall (d3/b3 probe) | gen fidelity | gen units/rollout |
> |---|---|---|---|---|
> | uncapped (shipped) | baseline | 51.1 s | 0.9906 / 100.0% | 25,181 |
> | **cap=2** | **net −0.020: 3 keys BETTER, 1 worse (±0.010 each)** | 27.0 s | 0.9867 / **100.0%** | 9,441 |
> | cap=1 (top entry only) | **+0.017 avg, 4/4 keys WORSE** (+0.010..0.020) | 16.0 s | 0.9753 / 99.9% (15/11,412 flip) | 5,481 |
>
> Reading: the doctrine's TOP PICK alone is close (99.9% gen agreement — pre-doctrine cap=1
> was 99.1%) but the searched #2 target still wins real games (~10 net turns / 600 on train,
> every key red). The 3rd..6th targets win NOTHING — cap=2 is quality-NEUTRAL-TO-BETTER on
> the suite while recovering ~half the play-side phase cost and ALL of the gen cost. The
> in-play "cap worse" number that justified the collapsed-main uncap (5.0150 vs 4.9250,
> d3 s2002) PRE-DATES the fetch doctrine; under the reviewed ranking it no longer holds.
>
> ## RESOLUTION 2026-08-21b: the fan's edge is 100% CLAIRVOYANCE — **TOP-ENTRY-ONLY ADOPTED
> at 0a59457** (USER design intent; GT rebaselined WORSE on purpose)
>
> USER: "I meant for our heuristic to be used with just the top entry, not split and search
> there ... or to discard the 'improvements' as clairvoyance (if that is what they are)."
> Held-out first said the fan is real (+0.034 coupled, 8/8 overnight keys). Then the
> MTG_SHUFFLE_SALT_SEARCH decouple ensemble (the always-shuffle protocol; salts 1–4 × the 4
> suite searched cells = 2,400 games/arm) answered the conditional:
>
> | arm | COUPLED (held-out, vs GT) | DECOUPLED ensemble avg |
> |---|---|---|
> | uncapped fan (was shipped) | best (GT) | **5.0558 — WORST, 4/4 salts** |
> | cap=2 | ~neutral (net +2/2800) | 5.0237 (best) |
> | cap=1 top-entry-only | +0.034 (8/8 keys) | 5.0317 (−58 vs uncap, better 4/4 salts) |
>
> The wide fan only "won" by pre-seeing the deterministic post-fetch shuffle — the exact
> artifact the always-shuffle keep resolution retired. Blind, it actively misleads the
> search. cap=2's residual edge over cap=1 decoupled is +19/2400 (~0.008, 3/4 salts) —
> borderline noise, and the USER's design is top-entry-only. ADOPTED:
> FiveColourProvider::FetchSearchCap()=1; the collapsed-main widening respects a cap-1
> provider; MTG_UNPRUNED audit unchanged; byte-identical to the measured env arm
> (digest-exact on all 4 cells). GT rebaselined deliberately WORSE (karoo precedent):
> regression +0.01..0.02 ×4 keys, smoke d3 +0.047 / d5 +0.093, overnight ×8 keys — this is
> the clairvoyance leaving the measurement, not play getting worse. Gen labeller cost:
> 25,181 → **5,481 units/rollout** (4.6x cheaper than pre-fix, 2x cheaper than the
> pre-phase era) — the secondary box's gen throughput is more than restored, no gen-scoped
> arm needed (the policy is the play policy).

2026-08-18. **Self-contained protocol for an IDLE machine** — read this and you can run it cold.
Context lives in `fivecolour-mullgen-labeller-sweep.md`; none of it is required to execute.

## The question, and why it needs a dedicated idle box

The proposed FiveColour mulligan-gen labeller is **hybrid d2/b1** (`--depth 2 --budget-ms 1`,
value sidecar attached — "H21"). The open question is the **cost** of the same setting with the
value leaf detached ("N21", `MTG_VALUE_PROFILE=none`): if N21 is not materially slower, generation
could optionally run WITHOUT the sidecar, decoupling keep artifacts from value-leaf regeneration.

The **fidelity half is already settled** — do not re-run it. On the 200-hand paired battery
(R=30, reference = shipped hybrid d6/b20), N21 ranks essentially identically to H21
(rho 0.9882 vs 0.9891 play-side, dispersion 0.085 vs 0.081, 100% pairwise agreement on all
11,018 separated pairs). Nothing rides on the leaf for label QUALITY at d2/b1. Only cost is open.

Two measurement routes are closed, which is why this doc exists:

1. **The primary box is contended** (a second agent works on it; the repo's standing caveat —
   same arm measured 18.7–39.7 s under load).
2. **The deterministic `work_units` meter is BLIND across the leaf on/off boundary.** Calibration,
   measured 2026-08-18: at d3/b3 the meter prices leaf-off at **0.62x** the hybrid
   (11,436 vs 18,588 units/rollout) while idle-box wall clock says leaf-off is **1.69x SLOWER**
   (40.3 s vs 23.9 s, 120 games). The meter charges a leaf visit roughly flat whether it is an
   O(1) value-leaf eval or a full horizon simulation, so it systematically understates
   horizon-rollout-heavy arms. `work_units` remains valid WITHIN a leaf family (its H21-vs-CUR
   1.67x matched wall's 1.61x) and is conservative for the V arm — but it cannot answer leaf-on
   vs leaf-off. (Hardware counters are `<not supported>` in the dev container, so `perf stat -e
   instructions` is not an option there either.)

## Known anchor points (idle primary box, single thread, 120 games, seed 1001)

**Provenance note (2026-08-18, post-rebase):** the WALL anchors below were measured at the
pre-rebase engine (`6732199a`-era tree). The branch has since taken colour-exact subset
affordability (adopted default-on, GT rebaselined) and a Karoo mana fix, and the protocol's
deterministic avg constants were RE-DERIVED at `91885099` (rebuilt, smoke 36/36 ALL PASS) —
the avgs listed in the commands ARE current. The wall ratios below are therefore advisory;
Step 1 exists to re-establish the direction on the new engine, which is all the gate needs.
Also re-verified at `91885099`: d2/b1 vs d3/b3 remain result-identical on 120/120 proxy games,
so the H21 labeller conclusion survives the engine change.

| config | hybrid (sidecar attached) | `MTG_VALUE_PROFILE=none` | leaf edge |
|---|---|---|---|
| d3/b3 | 23.9 s | 40.3 s | 1.69x |
| d3/b1 | 17.9 s | 23.8 s | 1.33x |
| **d2/b1** | 14.8–15.5 s | **UNKNOWN — this test** | trend says small-but-positive |

The trend (leaf edge shrinks with depth/budget) plus the meter's 0.98x reading make "roughly
neutral at d2/b1" plausible; the meter's known bias direction makes "still meaningfully positive"
equally plausible. Measure, don't extrapolate — this repo has been burned by exactly that.

## Protocol

### Setup

```sh
git checkout phase-1-2-deck-analyzer   # any commit >= 91885099, clean tree
./build.sh                             # Release; NEVER raw cmake (CLAUDE.md)
```

Keep the machine otherwise idle for the duration (~20 min). No timeouts on any command.

### Step 1 — calibration anchor (mandatory gate)

Reproduce the d3/b3 direction before trusting anything else:

```sh
cd <repo>
run() { /usr/bin/time -f "%e s" env "$@" build/Release/mtg decks/FiveColour/FiveColour.cod \
  --profile decks/FiveColour/FiveColour.profile.json --games 120 --seed 1001 --threads 1 \
  --max-turns 8 --ignore-play-profile --depth "$D" --budget-ms "$B" 2>&1 \
  | grep -E 'avg |s$'; }
D=3 B=3 run                          # expect avg 5.1750; wall ~<X>
D=3 B=3 run MTG_VALUE_PROFILE=none   # expect avg 5.1833; wall direction: SLOWER (ratio to re-establish here)
```

Absolute seconds will differ from the primary box (different CPU — only RATIOS transfer). If the
ratio is not clearly >1 (leaf-off slower), STOP: the box is not idle, or the setup is wrong.
The avg values are deterministic and MUST match exactly; a different avg means a different
binary/commit — stop and rebuild.

### Step 2 — the experiment: alternating pairs at d2/b1

10 runs per arm, strictly alternating (A B A B …) so drift affects both arms equally:

```sh
for i in 1 2 3 4 5 6 7 8 9 10; do
  D=2 B=1 run                          # arm A: hybrid   (expect avg 5.1750 every time)
  D=2 B=1 run MTG_VALUE_PROFILE=none   # arm B: leaf-off (expect avg 5.1833 every time)
done
```

Then a second seed block for robustness (5 pairs is enough): repeat with `--seed 2001`
(hybrid avg there is 4.9000).

### Step 3 — analysis and acceptance

* Compare **minima**, not means (the repo's standing rule: wall under any load inflates
  one-sided; the minimum is the least-contended sample).
* A verdict requires **full distribution separation** — every sample of one arm beats every
  sample of the other (this is how the −4% state-reuse was accepted and how an unresolvable
  A/B was correctly rejected). Overlap ⇒ report "effect smaller than measurement noise on this
  box", which is itself an answer: it bounds the leaf's edge below ~5%.
* Report: min/median per arm per seed block, the ratio of minima, and whether separation held.

### Step 4 — optional extras (same session, cheap, high value)

1. **V-arm true wall ratio** (5 alternating pairs, d2/b1): arm C =
   `MTG_VALUE_MODEL=1 MTG_VALUE_MIN_DEPTH=0 MTG_VALUE_STARTGATE_ALPHA=8`. Idle-primary-box
   reference: 2.91 s vs 14.8 s (5.1x); the units meter said 4.27x (understates, per the
   calibration above). Pins the projection table's aggressive-option number.
2. **d3/b1 anchor reproduction** (3 pairs) — confirms the trend row above on this hardware.

## Decision rules (user's call on adoption either way)

* **Leaf-off ≥ ~10% slower, separated** → keep the hybrid H21 labeller; question closed.
  Record the ratio here.
* **Separated and leaf-off within ~5% or faster** → a no-sidecar gen at d2/b1 is on the table:
  it decouples keep generation from value-leaf regeneration (a real operational benefit — today
  a leaf regen entangles keep-artifact lineage via the play digest). Adoption then needs the
  same mechanism care as any labeller change: the arm must be expressible per-deck (the
  `valuearm` "none" profile is the existing explicit form), scoped to LABEL ROLLOUTS only
  (discovery must keep running the shipped play policy, or the deck re-buckets), and stamped
  into `RolloutCfg` so sidecars from different arms refuse to pool.
* Either way: append the numbers to this doc and to `fivecolour-mullgen-labeller-sweep.md`.

## RESULT (2026-08-18, idle secondary box — 12 cores, load 0.4 at start, single thread)

**Verdict: leaf-off is 1.57–2.20x SLOWER at d2/b1, with FULL distribution separation in both seed
blocks. Decision rule 1 fires — keep the hybrid H21 labeller. Question closed.**

Run at `91885099` (this repo's HEAD at the time; see the determinism check below),
`build.sh` Release, no other work on the box. Harness: `logs/fc_leafcost/run.sh` (`/usr/bin/time` is
absent in the dev container, so wall is `date +%s%N` around the process); raw logs in
`logs/fc_leafcost/step2.log` and `step4.log`.

### Step 1 — calibration gate: PASSED

| d3/b3, seed 1001 | hybrid | leaf-off | ratio |
|---|---|---|---|
| this box | 33.78 s | 58.12 s | **1.72x** |
| primary box (doc anchor) | 23.9 s | 40.3 s | 1.69x |

Direction and magnitude both reproduce. Absolute seconds are ~1.4x the primary box (different CPU) —
only ratios transfer, as the protocol says.

### Step 2 — the experiment, d2/b1

| seed | pairs | hybrid min / med / max | leaf-off min / med / max | ratio of minima | separated |
|---|---|---|---|---|---|
| 1001 | 10 | 21.55 / 21.70 / 23.16 s | 33.74 / 34.03 / 36.23 s | **1.566x** | YES (23.16 < 33.74) |
| 2001 | 5 | 28.87 / 29.09 / 29.13 s | 63.46 / 63.75 / 64.02 s | **2.198x** | YES (29.13 < 63.46) |

Both blocks separate with a wide margin — every leaf-off sample is slower than every hybrid sample,
by 10.6 s and 34.3 s respectively. This is not a noise-limited result; the effect is 15–30x the
within-arm spread.

### Step 4 — the extras

* **V-arm true wall ratio** (d2/b1, seed 1001, 5 alternating pairs): hybrid min 21.41 s vs V-arm min
  3.69 s = **5.80x** (primary-box reference 5.1x; the units meter said 4.27x, again understating —
  consistent with the calibration bias). V-arm avg 5.3667 vs hybrid 5.1750, i.e. the known
  play-quality gap is visible here too.
* **d3/b1 anchor** (3 pairs): hybrid min 24.93 s vs leaf-off min 33.87 s = **1.359x**, against the
  doc's 1.33x on the primary box. Reproduces.

### The trend prediction was WRONG — measure, don't extrapolate (again)

The doc reasoned "the leaf edge shrinks with depth/budget, so d2/b1 should be small-but-positive".
Measured on this box the edge is NOT monotone in depth/budget:

| config | leaf edge (ratio of minima) |
|---|---|
| d3/b3 | 1.72x |
| d3/b1 | 1.36x |
| d2/b1 seed 1001 | 1.57x |
| d2/b1 seed 2001 | 2.20x |

d2/b1 is a LARGER edge than d3/b1, and the seed-to-seed spread (1.57x vs 2.20x) is bigger than the
whole depth/budget effect — the leaf's value tracks how horizon-rollout-heavy the particular hands
are, not the search shape. The `work_units` meter's 0.98x reading at d2/b1 was wrong by 60–120%,
exactly in its documented bias direction. Nothing here changes the instrument note below; it
confirms it.

**Consequence for the open question:** a no-sidecar gen at d2/b1 is OFF the table on cost. Decoupling
keep artifacts from value-leaf regeneration would cost 1.6–2.2x wall on the gen labeller — against a
FAST projection already at ~132 h single-box, that is days, not hours. The fidelity half being
settled (N21 ≈ H21 on label quality) does not rescue it.

### Determinism check: the re-derived `avg` constants CONFIRM independently

The run was executed against the pre-refresh revision of this doc (which still listed the
pre-rebase constants 5.1667 / 5.1750 and `8bd337f8` — a commit that does not exist in this
repository's history at all, unpushed or rebased away on the primary box). Measured on this box at
`91885099`:

| anchor | measured here | primary box's re-derivation (`a6b79000`) |
|---|---|---|
| hybrid d3/b3 & d2/b1, seed 1001 | **5.1750** | 5.1750 |
| leaf-off d3/b3 & d2/b1, seed 1001 | **5.1833** | 5.1833 |
| hybrid d2/b1, seed 2001 | **4.9000** | 4.9000 |

Every constant agrees to 4 dp on different hardware. That is a determinism handshake neither box
set out to run: the primary re-derived the constants at `91885099` (smoke 36/36) while this box was
mid-experiment, and the two agree exactly. The pre-rebase drift (+0.0083 on seed 1001 = one game in
120 moving one turn, in both arms) is fully explained by `e4690c37` (ADOPT colour-exact subset
affordability) and `811d165c` (KAROO adds one of EACH colour), and is now reflected in the protocol
above. Nothing about the verdict rests on it — the ratio gate carries that — but the handshake means
the two boxes are running the same engine, so these wall ratios and the primary's are comparable.

## Instrument note worth keeping regardless of outcome

`GameWorkMeter` `work_units` is the right currency for load-immune comparisons ONLY when both
arms share a leaf configuration. Across leaf families it is biased in a known direction
(understates horizon-rollout cost ~2.7x at d3/b3). If this bites again, the fix is a meter that
charges leaves by actual simulation steps — but that changes SearchBudget accounting (= play
behaviour at any binding budget), so it is a measured change, not a cleanup.

## RE-MEASUREMENT at HEAD `552a76b5` (2026-08-20), before the FAST gen launch

The engine took **60 src commits** since the sweep-era measurement above (`MTG_5C_SSM` default-on,
`MTG_ENUM_MEMO`, condemnation, freecast prune, the whole fetch/order doctrine arc), so the leaf
question was re-run cold before committing a multi-week generation to the H21 setting. Same box
class as the "idle secondary" above (12 cores, load 0.16 at start), same protocol, harness
`logs/fc_leafcost_head/run.sh`, raw in `measure.log`.

The deterministic avg constants have MOVED (the engine plays better): seed 1001 hybrid is now
**5.0333** (was 5.1750), leaf-off **5.0333** (was 5.1833), seed 2001 hybrid **4.8250** (was 4.9000).
Leaf-off is no longer even distinguishable from hybrid on the metric at d2/b1 — it was +0.0083 before.

| arm | seed 1001 min / med / max | seed 2001 min / med / max | avg |
|---|---|---|---|
| CAL hybrid d3/b3 | 55.78 s (n=1) | — | 5.0417 |
| CAL leaf-off d3/b3 | 56.48 s (n=1) | — | 5.0333 |
| **H21 hybrid d2/b1** | **43.65 / 43.81 / 45.53 s** (n=8) | **75.23 / 75.24 / 75.39 s** (n=3) | 5.0333 / 4.8250 |
| N21 leaf-off d2/b1 | 46.22 / 46.32 / 47.42 s (n=5) | 89.67 / 89.83 / 90.05 s (n=3) | 5.0333 / 4.8250 |
| V21 pure-V d2/b1 | 7.12 / 7.16 / 7.22 s (n=3) | — | 5.2500 |
| H21 with `MTG_5C_SSM=0` | 41.99 / 42.03 / 42.16 s (n=3) | — | 5.0417 |

### The leaf edge has COLLAPSED — but the verdict is unchanged

| config | leaf edge at `91885099` | leaf edge at `552a76b5` |
|---|---|---|
| d3/b3 | 1.72x | **1.01x** (56.48 / 55.78 — not separated, n=1 each) |
| d2/b1 seed 1001 | 1.57x | **1.06x** (separated: hybrid max 45.53 < leaf-off min 46.22) |
| d2/b1 seed 2001 | 2.20x | **1.19x** (separated by 14.3 s) |

Decision rule 1 still fires on seed 2001 (≥10%, separated) and the hybrid is faster in every block,
so **keep the hybrid H21 labeller** — but the margin is now 1.06–1.19x, not 1.6–2.2x. The
operational case for a no-sidecar gen (decoupling keep artifacts from value-leaf regeneration) is
much cheaper than it was; it is now a ~6–19% wall premium rather than "days". It still needs the
`valuearm`/`RolloutCfg` mechanism work described in the decision rules above, so nothing changes for
this run, but the door the 2026-08-18 result closed is no longer bolted shut.

The seed-dependence lesson from §"The trend prediction was WRONG" repeats: the edge is 1.06x on one
seed block and 1.19x on the other. The leaf's value still tracks how horizon-rollout-heavy the hands
are — the engine's other prunes (enum memo, condemnation, freecast) have absorbed most of the work
the leaf used to save.

### Cost at HEAD is ~2x the sweep-era cost — and `MTG_5C_SSM` is NOT the cause

Hybrid d2/b1 seed 1001: **43.65 s** at HEAD vs **21.55 s** at `91885099` on this box class = **2.03x**
per rollout. d3/b3: 55.78 s vs 33.78 s = 1.65x. This is the single most important number for the
FiveColour gen: the sweep's "FAST ~132 h on 23 cores" projection was priced at the old cost.

The obvious suspect was `MTG_5C_SSM` (searched interior second main, adopted default-on in `5efeb1d`
— strictly more search). **Measured: it is not.** `MTG_5C_SSM=0` runs 41.99 s vs 42.16 s hybrid's
43.65 s — a **1.04x** saving, and it plays *worse* (avg 5.0417 vs 5.0333). There is no cheap gen-only
switch here; the 2x is spread across the arc, and SSM is paying for itself. Do not reach for
`MTG_5C_SSM=0` as a gen lever.

### V21 re-confirmed, bias re-confirmed

V21's wall ratio is now **6.10x** (43.65 / 7.16 s), up from 5.80x — it benefits from the leaf edge
collapsing, since V21 *is* pure leaf. Its play-quality gap is unchanged and still visible in the
metric: **5.2500 vs 5.0333** (the gap widened from +0.19 to +0.22 turns as the hybrid improved). The
dork-hand bias documented in `fivecolour-mullgen-labeller-sweep.md` §2 remains the argument against.
