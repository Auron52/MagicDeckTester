# Wall-clock test: does the value leaf still pay in the FiveColour gen labeller at d2/b1?

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
