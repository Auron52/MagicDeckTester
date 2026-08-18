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

## Instrument note worth keeping regardless of outcome

`GameWorkMeter` `work_units` is the right currency for load-immune comparisons ONLY when both
arms share a leaf configuration. Across leaf families it is biased in a known direction
(understates horizon-rollout cost ~2.7x at d3/b3). If this bites again, the fix is a meter that
charges leaves by actual simulation steps — but that changes SearchBudget accounting (= play
behaviour at any binding budget), so it is a measured change, not a cleanup.
