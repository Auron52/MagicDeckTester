# The adaptive-bottoming precision dial: deferred, and what to run instead

**Status: ADOPTION deferred by the user, 2026-10-11. The cross-deck TEST (§2) is RUNNING at the
user's request as of 2026-10-11 09:15** — *"I would be curious to see how that works on more decks
for some confidence that it won't hurt here. But, at the same time, I don't want to dig into the
generation time too much."* The test costs **no rollouts** (it reads banked raws), so it does not
spend the generation window. Not a rejection and not a parking of the idea — a re-ordering, plus a
change in what the deliverable should be.

> **USER, 2026-10-11:** *"On the lossy change, I think that could be something to consider, but I
> would rather address it after all of the more lossless/dominance pruning changes are in.
> Realistically, it probably won't be necessary for this run, so the more useful part would be a test
> for the future to see how much this would help on a greater cross-section of decks. If we get lucky
> enough with the other optimizations we may even be able to run kitty both ways in the span of 36
> hours to have another real comparison?"*

Three separable instructions are in that paragraph, and they point at different work:

1. **Ordering.** The lossless / dominance prunes land first. The dial is not considered until they
   are in, because their saving changes whether the dial is needed at all.
2. **Scope.** The valuable artifact is not "should KittyEquipment v3 use it" — it is **how much this
   would help across a greater cross-section of decks**, as a standing result for future generations.
3. **The better experiment.** If the lossless work frees enough of the window, run the KittyEquipment
   v3 generation **both ways inside 36 h** and compare the real artifacts. That replaces a simulated
   regret with a measured one.

This document is the standing record so none of it depends on a session's memory.

---

## 1. What is already known, and the trap inside it

The lever is **adaptive bottoming** — the generator refines the decision-relevant bottoming sub-cells
toward a cap R while leaving clear cells at the floor — together with its two dials,
`MTG_KEEP_FLIP_EPS` (the refine-stop gate) and the cap R itself.

Measured **offline, with no rollouts**, by `MTG_KEEP_SIM_ADAPTIVE_BOTTOM=1`
(`src/analyzer/ExhaustiveKeep.cpp`, `RunAdaptiveBottomRegretSim`) on the **slivers_vial** banked raw:

| configuration | regret (turns) | sub-table rollouts saved | ⇒ off TOTAL generation |
|---|---|---|---|
| at kitty's own cap R=30 | **+0.0033 ± 0.0001t** | 61.1–34.0% | 31.7–17.6% |
| `flip_eps` 0.02 → 0.20 at R=30 | +0.0033 → +0.0031 (inside se) | 72.0–52.4% | 37.3–27.2% |

So at the cap this deck actually uses, adaptive bottoming costs about three thousandths of a turn and
saves between a sixth and a third of the whole generation, and **widening `flip_eps` is very nearly
free** — the regret moves by less than its own standard error while the saving grows substantially.

### The trap: the cap-R sweep must NOT be read as "a low R is cheap"

| raw | R=40 | R=20 | R=16 | R=16 + eps 0.20 |
|---|---|---|---|---|
| +0.0050 | +0.0038 | +0.0023 | +0.0016 | +0.0012 |

Regret **falls** as the cap falls. That is not a finding, it is an artefact of the estimand: the
metric is *adaptive versus full **at that R***, so a lower cap leaves less room for the two policies
to differ. Driving R down drives the measured regret toward zero while making **both** policies
worse in absolute terms, and the comparison cannot see that.

This is the user's own objection, confirmed:

> *"R being too low would be troublesome for the adaptive change, since it would reduce the impact."*

**Any future reading of this sweep must quote absolute policy quality at each R, not the adaptive-vs-full
delta at each R.** Otherwise the cheapest-looking configuration is simply the one with the least to lose.

### Raw R for slivers is 60

Recorded because it was misstated once in session: slivers' full-bottom raw is **R=60**, not R=100.

---

## 2. The cross-deck test (instruction 2) — ready to run, no rollouts

The simulator reads a pooled raw table and writes nothing. **26 decks already have a banked
`decks/<Deck>/<Deck>.keepmodel.exhaustive.raw.json.gz`**, so the cross-section the user asked for
needs no generation at all:

```
Angels, Anti-Lifegain, Auras, BreachingDragonstorm, Creature Giving, CritterLifegain, Dragons,
Dragonstorm, FiveColour, Fluctuator, Fungus, Giants, Goblins, Hinata2, KittyEquipment, Knights,
Melira Pod, Minotaur, Mirrorwing Dragon, Pirates, SelesnyaLifegain, StompySurprise, WhiteKnights,
burn, slivers_vial, treasure_hunt
```

### *** STOP: ONE INVOCATION CORRUPTS EVERY DECK IT TOUCHES — AND IT IS NOT THE REAL ROUTE ***

**Measured the hard way, 2026-10-11.** Running the simulator as

```
<analyze-bin> decks/<Deck>/<Deck>.cod --cards-json ... --merge-keep <raw>     # DO NOT
```

**overwrites `decks/<Deck>/<Deck>.profile.json`.** The log line is explicit — `Profile written to
/workspaces/.../Angels.profile.json` — and the loop had rewritten **16 shipped profiles** with
different `card_scores` (Angels' Archangel of Thune went −0.1680 → −0.0935) before `git status`
caught it. That is not cosmetic: `card_scores` feed the keep decision, so it silently re-fits the
mulligan behaviour of every deck in the sweep — the "a generation can go LIVE mid-run and corrupt
its neighbour" hazard, reached from a direction nothing was guarding.

**The cause is simpler and worse than first recorded, and the first diagnosis sent the next reader
after an analyzer change that is not needed.** It is *not* that the keep-merge branch forgets to
suppress a write. **`--merge-keep` IS NOT A FLAG THIS BINARY HAS** — grep `src/analyzer/main.cpp`:
merge mode is dispatched by the environment (`MTG_KEEP_MERGE`), and there is no `"--merge"` argv
parse anywhere. The analyzer ignores the unrecognised argument, falls through to **default** mode,
runs a full 1000-game card-scores analysis and writes the profile at the tail of `main()`. So the
stage did not merely corrupt profiles as a side effect of measuring — **it never measured anything
at all**; all 16 "sim <deck> done" lines were full deck analyses, and the run produced no regret
number whatsoever.

**The safe route already exists and needs no code change.** `main()` dispatches
`if (EnvOn("MTG_KEEP_MERGE")) { return RunKeepMergeMode(a); }` **before** the profile write, and the
sim returns from inside that before the merge's own write; `MTG_KEEP_NO_WRITE=1` additionally clears
both output paths. Verified 2026-10-11 on Angels: the profile's md5 was unchanged and
`git status --porcelain decks` stayed empty.

```
MTG_KEEP_MERGE=1 MTG_KEEP_NO_WRITE=1 MTG_MERGE_INPUTS=<raw.gz> \
MTG_KEEP_SIM_ADAPTIVE_BOTTOM=1 MTG_KEEP_SIM_TRIALS=128 \
MTG_KEEP_SIM_FLIP_EPS=<eps> MTG_KEEP_SIM_FLOOR=2 \
  <analyze-bin> <deck> --cards-json src/cards/data/cards.json       # CORRECT, writes nothing
```

**The generalisable lesson, which is the part worth keeping:** this binary takes most of its modes
from the environment, and it does **not** reject unknown `--flags`. An invented flag therefore does
not fail loudly — it silently selects the *default* mode, which is the one mode that writes. Check a
flag exists in `main.cpp` before putting it in a loop, and run `git status` after any sweep.
`UN-BLOCKED` as of 2026-10-11; the sweep is `logs/kitty_tail_diag/sim_eps_crossdeck.sh`, which ends
by asserting `git status --porcelain decks` is empty.

### Shape of the run

Per deck, over a grid of (cap R, `flip_eps`), with `MTG_KEEP_SIM_TRIALS` high enough that the
Monte-Carlo floor-noise error is well below the effect (slivers used 128 and reported ±0.0001):

Per deck, over a grid of (cap R, `flip_eps`), using the **correct invocation above** and with
`MTG_KEEP_SIM_TRIALS` high enough that the Monte-Carlo floor-noise error is well below the effect
(slivers used 128 and reported ±0.0001).

Four things to get right, each of which has bitten something in this repo before:

- **`sumsq` is a precondition, not an assumption.** The sim refuses without per-cell sumsq
  (`"pooled sidecars lack per-cell sumsq ... Nothing done."`). Part of the deliverable is the list of
  decks whose banked sidecars predate it — a deck that cannot be simulated must be reported as
  **un-run**, never silently dropped from the average.
- **It must write nothing.** See the warning above; this is the one that actually failed.
- **Do not average regret across decks and quote one number.** Decks differ in how much of their
  generation is bottoming at all (Snow: 59.6% of units), so the quantity the user asked about — how
  much this *helps* — is per deck `saving x (bottoming share)`, and the *risk* is per deck regret.
  Report the joint distribution and name the worst deck.
- **Report it against each deck's own shipped cap**, not a common R, because the §1 trap means a
  common-R table would rank decks by how little they had to lose.

### Why this is worth doing even if v3 never needs the dial

Every future deck's generation faces the same question, and today the only evidence is one deck. A
26-deck table turns "we think this is nearly free" into a result with a known worst case — and it
costs no rollouts, which is the entire reason the simulator was built.

---

## 3. The better experiment (instruction 3): run v3 both ways in one window

If the lossless prunes take KittyEquipment v3's generation far enough below the 36 h window that two
runs fit, then the right move is not to simulate the regret at all:

- **Arm A:** full bottoming, the shipped precision.
- **Arm B:** adaptive bottoming at the deck's cap, `flip_eps` as chosen.

Both produce a real keep table; the two tables are then compared the way keep tables are always
compared in this repo — by the in-game A/B, not by their own internal numbers. That is a *measured*
regret on the artifact that ships, and it retires the simulator's structural assumptions (Gaussian
draws from stored mean+variance, refined cells credited their true mean) rather than trusting them.

**Feasibility is a function of the lossless work and nothing else.** Two full-precision runs do not
fit a 36 h window today; whether they do later is exactly what the prune programme decides, so this
stays conditional and gets re-costed when those numbers land.

Three constraints carry over unchanged:

- **Generations are strictly serial and alone on the box.** Two arms means two sequential runs, never
  two concurrent ones — a contended generation measures nothing, and a keep table dropped beside a
  decklist mid-run changes the other arm's play.
- **Copy each journal aside before anything runs.** A generation journal is days of compute with one
  copy, and the recipe/R must match the journal's own header.
- **Bottoming itself is not the variable.** Bottoming is always on; the variable is only the
  *precision* with which the bottoming decision is estimated.

---

## 3b. THE USER'S RULING, 2026-10-11 — `flip_eps = 0.10` for the v3 run, default still OPEN

> **SUPERSEDED 2026-10-18 — the default IS now 0.10.** USER: *"It should be applied to everything"*,
> after the more-tests step below was done (*"It was simulated for a number of different decks with
> overall very positive results ... surprisingly with little to no quality costs and decent performance
> improvements"*). `cfg.flip_eps` / `ExhaustiveKeepConfig::flip_eps` and both simulators default to
> 0.10; `MTG_KEEP_FLIP_EPS=0.02` restores the old value. flip_eps is NOT in the journal's resume
> identity, so a journal rolled at 0.02 resumes silently under 0.10 (see the note in
> `src/analyzer/ExhaustiveKeep.cpp`). The rest of this section is the 2026-10-11 record.

> **USER, 2026-10-11 ~09:35:** *"Okay, well, I guess we'll try 0.1 then once we have accepted it.
> After this run I may want to do more tests to confirm what the default should be."*

Two separate things, and only the first is settled:

1. **This run:** KittyEquipment v3's mulligan generation runs with **`MTG_KEEP_FLIP_EPS=0.10`**,
   launched **after** the equip-prune adoption is accepted into ground truth. The in-code default
   (`src/analyzer/main.cpp:205`, `cfg.flip_eps`) stays **0.02** — this is a per-run setting, not a
   default change. Do not edit the default to match the run.
2. **The default:** still OPEN, explicitly pending **more tests after this run**. Nothing in the
   cross-deck table below may be used to move the shipped default on its own.

### What the measurement can and cannot say about *this* deck — state it plainly

**The simulator cannot price v3.** It needs a completed full-bottom raw, and v3 has only the
**cancelled** journal (`decks/KittyEquipment/v3-adelbert-gauntlets/*.journal`, R=16, 15 buckets,
play_digest `3a5a92ae6f6e725c`, commit `766424d1`). The only KittyEquipment raw that exists is the
**top-level v2** one (Aug 25, 18 buckets), which the sweep's `decks/*/*.raw.json.gz` glob picks up.
So the row labelled "KittyEquipment" in the cross-deck table is **v2** — the nearest available proxy
for v3, same archetype and largely the same cards, but not the list that will ship. The honest
statement of the risk for v3 is therefore *"inferred from v2 plus the cross-deck distribution"*,
never *"measured on v3"*.

### The two decisions the saving comes from are NOT the same decision

Conflating them overstates how safe `eps` is:

- **Adaptive bottoming at all** (vs full bottoming) is where nearly all the saving lives, and its
  cost is **deck-dependent**: slivers/kitty-reference **+0.0033** at R=30, **Angels +0.0106** at
  R=40 — 3x.
- **Widening `flip_eps` 0.02 → 0.10/0.20**, *given* adaptive bottoming, is very nearly free: Angels
  moves +0.0106 → +0.0103 → +0.0107 (within ~4 se) while the saving grows 24.7–41.4% → 34.8–46.6%.

"Nearly free" is a property of the **eps widening**, not of the dial as a whole.

### What the follow-up tests after this run should do

- **Quote absolute policy quality at each R, never the adaptive-vs-full delta at each R** (§1's
  cap-R trap). A table that reports the delta ranks decks and caps by how little they had to lose.
- **Price it at the R the run actually used.** The cancelled v3 journal is R=16, which is neither
  `fast` (30) nor `complete` (40); whatever R the real run ends up using is the R the follow-up must
  report, because the trap makes a low-R reading flatter itself.
- **Prefer the real comparison (§3) to another simulation** if a window ever allows two runs. The
  simulator's structural assumptions (Gaussian draws from stored mean+variance, refined cells
  credited their true mean) are exactly what a measured artifact retires.
- **Report the worst deck, not the average** — the risk is per deck.

## 4. Who decides

Every dial here — cap R, `flip_eps`, `cutoff_P`, the bucket count, `MTG_DECISION_WORK_X` — is a
**precision/coverage tradeoff, and therefore the user's ruling, not an agent's**. That holds for
leaving one at its default exactly as much as for changing it. The simulator's job, and this
document's, is to put numbers in front of that decision; nothing here may be adopted on the strength
of a measurement alone.
