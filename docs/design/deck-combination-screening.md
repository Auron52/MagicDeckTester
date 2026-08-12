# Fast comparison across deck changes (combination screening)

> ## CORRECTION 2026-08-11 — the probes below ran PROFILE-LESS, in two layers
>
> Every hand-rolled probe in this document passed `value_profile` but **not** `profile`, and every
> throwaway R=10 keep table was generated in a scratch directory that held neither. Both the
> measurement *and* the apparatus therefore described a deck we do not ship.
>
> **Layer 1 — the measurement.** A default profile zeroes slivers' `vial_target_mv` (2 in the shipped
> profile): the engine did not know how to *use* Aether Vial. Same committed table, ladder mode,
> 20,000 paired games:
>
> | 4 Vial -> 2, Muscle 4 -> 6 | delta |
> |---|---|
> | WITHOUT the deck profile (`vial_target_mv=0`) | −0.0953 |
> | WITH the deck profile (`vial_target_mv=2`) | **−0.0246** |
>
> **Layer 2 — the apparatus.** `RunExhaustiveKeepMode` resolves the play profile and the value
> sidecar *directory-relative off the decklist* and falls back to `DefaultProfile()` in silence, so a
> gen run from a scratch dir is fit to that same wrong deck. It shows in the artifact: the play digest
> moves (`04cd8c9d…` → `16d24356…`), and **bucket discovery merges Ancient Ziggurat into the land
> bucket** — 9 buckets / 4,411 size-7 cells instead of 10 / 7,758. Re-run with the profile present,
> R=10 discovery reproduces the committed R=60 bucketing *exactly*, all ten buckets. See
> "Bucketing was not unstable — it was profile-less" below.
>
> This is the trap `.claude/skills/value-leaf.md` already names: *"profile always attached —
> measuring profile-less describes a deck we do not ship."*
>
> **Invalidated:** all absolute EFFECT sizes and every effect/floor ratio below; the bucket
> comparisons; and the cell-count/cost figures (a profile-less gen is ~1.8x *cheaper* precisely
> because it merges buckets it should not).
>
> **Survives:** the methodological findings, because both arms shared one consistent apparatus in
> every case — alignment being worth ~21x, slot/identity keying being worth 3.2x more on a reshuffling
> deck, inheritance beating slot-form 1.4x, sharing a table halving the se, the coverage cliff, and
> no-table being symmetric. Those are all relative comparisons under a fixed apparatus.
>
> `scripts/deck_compare.py` attaches `profile` correctly, and its `--floor` mode copies the profile
> and value sidecar into the arm directory *before* generating a table, so neither layer can recur.

**Status: BUILT + MEASURED 2026-08-11.** The engine route (`MTG_DECK_NUMBERING` / the manifest's
`deck_numbering`) and the driver (`scripts/deck_compare.py`) are in-tree, default OFF and
byte-identical unset. The headline finding is that the expensive stages this doc was written to make
cheap **do not need to run at all** for screening.

The problem (user, 2026-08-11): card implementation and heuristic tuning are acceptable **one-time**
costs per card. What must be minimised is the **per-combination** cost — trying combination after
combination of *ratios/counts* of an already-implemented card pool. Target was "a few hours"; the
measured answer is **minutes**.

## Rule 0 — do not regenerate per-deck artifacts per combination

Measured on burn (1 Skullcrack → 1 Lightning Bolt, 5000 games, d5, identity-keyed shuffle), with each
arm's own R=10 exhaustive keep table vs both arms sharing one table:

| apparatus | measured delta | se |
|---|---|---|
| each arm its OWN keep table | −0.0264 | ±0.0089 |
| both arms on the BASE table | −0.0294 | ±0.0042 |
| both arms on the VARIANT table | −0.0326 | ±0.0040 |

Direct cost of using the base table on the variant instead of its own: **−0.0030 ± 0.0087
(t = −0.35)** — indistinguishable from zero.

All three agree on the answer, but **sharing one table halves the standard error** (2.2×, i.e. ~4.5×
fewer games). This is the non-obvious part and it is the whole design: two *different* keep tables
mulligan differently on hands that have nothing to do with the swap, injecting divergence into the
comparison. One shared table makes every divergence attributable to the card change. Sharing is not
a cheap approximation — it is the *better measurement*.

The symmetry check (sharing the variant's table instead) rules out "the table favours the arm it was
fit to" at this edit size.

So the per-combination cost is **only the measurement games**. Bucket discovery, the keep table, the
value leaf and the play profile are all one-time per pool.

## What actually dominated the cost: shuffle alignment

A decklist edit re-permutes every game, so the two arms share nothing but the seed and the
comparison is effectively unpaired. Measured on burn (5000 games), the *same* 1-card swap written
two ways:

| decklist layout | identical win turn | sd(diff) | games for 3σ on 0.03t |
|---|---|---|---|
| naive count edit (`4 Bolt` → `5 Bolt`) | 75.6% | 0.678 | 4,594 |
| index-aligned (`3 Skullcrack` + `1 Bolt`) | 97.8% | 0.147 | **215** |

21×, from file layout alone, with the point estimate unchanged (−0.0156 vs −0.0220; the naive arm's
se is 4.6× larger). `GoldFishRunner::SetupGame` shuffles with a positional Fisher–Yates, which
applies the *same index permutation* to a nearly-identical vector — so keeping the expanded card
vector positionally aligned preserves nearly the whole game.

**Canonical slot form** (60 lines of `1 <card>`, slot order fixed by the pool) makes this automatic
for 1:1 substitutions and is byte-identical to the equivalent grouped decklist (verified 4839/4839).

## `MTG_DECK_NUMBERING` / `deck_numbering` — the shipped route

Default OFF → byte-identical (verified: unset and `=0` both reproduce the pre-change baseline
exactly). When a numbering map is supplied, `SetupGame` assigns `m_number` from it **pre-shuffle** and
orders the opening library with `Library::ShuffleByKey` instead of the positional Fisher–Yates. The
permutation is then a pure function of `(seed, m_number)`, identical across every combination that
numbers its shared copies the same way — and, because `m_number` is also the mid-game CRN key, the
*reshuffles* align across combinations too.

Two forms, one mechanism:
- `MTG_DECK_NUMBERING=<map.json>` — process-level, for a single run.
- a job's `"deck_numbering": <map.json>` in a batch manifest — per job (`decknumbering::t_map`
  thread_local, same pattern as `valuearm::Arm`), which is what lets every combination pool into ONE
  batch. Verified: pooled == two separate processes, 0 games differ.

The map is `{"Card Name": [n1, n2, ...]}` and must cover every copy; a card left at `m_number == 0`
aborts the run loudly rather than silently disabling the reshuffle.

Two earlier prototypes are **deleted**, and both should stay dead:
- `MTG_SLOT_SHUFFLE` (number = decklist slot index) — this general route reproduces it exactly
  (5000/5000 identical) and, unlike it, survives deck-size changes and does not require every
  combination to be hand-authored in canonical slot form.
- name-keyed pool numbering — see below.

The measurement that motivated identity keying, on the deck where it matters (one that reshuffles):

| Anti-Lifegain (12 fetchlands + 3 tutors), 1 Reverent Silence → 1 Aria of Flame | identical | games for 3σ |
|---|---|---|
| positional alignment only (slot-form decklist) | 80.5% | 3,115 |
| identity-keyed shuffle | **94.0%** | **981** |

3.2×, and the two agree on the delta (+0.0123 vs +0.0137). On a deck that never reshuffles the two are
equivalent (slivers: 756 vs 733 games).

### Rejected: name-keyed pool numbering

The first prototype implemented `.claude/skills/mtg-ai.md`'s union-numbering scheme literally
(number over the union of card *names*, `MTG_POOL_NUMBERING=<pool file>`). It is **worse** and should
not be revived: it aligns identities but not positions, so swapping 2 Hatchery for 2 Leeching deletes
two numbers and inserts two others at their own keyed ranks, shifting everything in between. Slivers,
5000 games: 80.3% identical vs 93.3% for plain slot form. Slot-keying is the fix.

## Worked example (user's ladder), slivers, 5000 games/arm, d5, shared base apparatus

| change | delta | t | identical |
|---|---|---|---|
| 2 Hatchery Sliver → 2 Leeching Sliver | −0.0440 ± 0.0038 | −11.5 | 93.3% |
| remove 1 Hibernation Sliver → +1 Leeching | −0.0288 ± 0.0027 | −10.6 | 96.5% |

Both resolve decisively in ~3 minutes of compute. Only ~700 / ~400 games are needed for 3σ on a
0.03t effect, so the measurement stage is essentially free.

## The value leaf does not need regenerating either — but the reason matters

Its features (`MidGameFeature`, `src/ai/KeepModel.h`) are entirely generic board/plan/library counts
with **no card-name indexing**, so a model transfers structurally to any combination. That alone only
buys validity, not calibration, and five of ten decks ship `value_trust_depth` set at or below their
play depth — there the leaf genuinely decides, and a stale residual is a real risk.

The escape is to configure the screening apparatus so the leaf **cannot** decide:
`MTG_VALUE_MODEL=0` + `MTG_VALUE_PROFILE=<pool model>` + `MTG_LADDER_VALUE_LEAF=1` (the H-cell shape
from `test/ladder_value_leaf_check.sh`). Model attached to make warm-up passes cheap; committed pass
stays pure heuristic. `TurnSolver.cpp` argues byte-identity of the committed line by construction and
names the one residual coupling: **budget** (cheaper warm-ups leave more for the deep pass), so it is
exact only at an unbounded budget. At `budget_ms=20` treat it as a favourable change needing its own
check, not a free one.

**MEASURED 2026-08-11** (slivers, 20,000 paired games, d5, deliberately attaching *burn's* model —
a different archetype — as the "wrong model" arm; all arms pooled in one batch via the `ValueArm`
manifest fields):

| config | delta vs the correct model | identical |
|---|---|---|
| **ladder mode, UNBOUNDED budget** | — | **0 of 2000 differ — byte-identical** |
| ladder mode, budget 20 | −0.00075 ± 0.00022 | 99.92% |
| normal mode (leaf decides), budget 20 | −0.00010 ± 0.00007 | 99.99% |
| ladder mode, model → NO model | +0.00220 ± 0.00050 | 99.52% |

The structural claim holds exactly: at unbounded budget a completely wrong model produces literally
identical play. At the realistic bounded budget the residual budget coupling is **0.0008t** — 6–10×
below the mulligan bias floor and ~100× below the effects being measured. **Sharing one value model
across combinations is safe.**

Qualifications: this is ONE deck, and slivers turns out nearly model-insensitive even in normal mode
(0.01% of games move); a leaf-sensitive deck (FiveColour's model was worth −0.0099t) could differ.
And normal mode measured *less* sensitive than ladder mode (0.0001 vs 0.0008) — unexplained, and too
small to matter, but not a result to build on. Use ladder mode as the screening default because it is
guaranteed by construction, not because it measured smaller here.

## What this makes unnecessary (for now)

`docs/design/carry-under-card-edits.md` plans a game-level execution-trace carry — per-rollout
"cards seen" records so a re-generation only re-runs the games that drew the changed card. That work
is **not needed for screening**, because screening does not regenerate the table at all. It remains
the right mechanism for the *end* of the process — producing a definitive per-deck table for a
combination you have decided to adopt — and its precondition is now identified and built: without an
identity/slot-keyed shuffle, a game that never drew the changed card is still not the same game, so
nothing is reusable. That precondition was why `execution-trace-carry.md` concluded "not for small
deck changes"; with an identity-keyed opening shuffle the conclusion is worth revisiting.

## Channel (B): measured, and it does not separate

The user's simpler proposal was to reuse keep cells whose opening hand contains only unchanged cards.
Comparing the two burn R=10 raw tables cell-by-cell on their identical 10-bucket space:

| hand size 7 | cells | rms z |
|---|---|---|
| hand contains NEITHER changed card | 6,360 | 1.284 |
| hand contains a changed card | 15,200 | 1.245 |

The two groups are **indistinguishable** (and the "neither" group moves marginally *more*), so cells
cannot be triaged by whether the hand holds the changed card — channel (B), the card sitting in the
library and being drawn, is not smaller than channel (A). `rms z ≈ 1.25` against 1.0 for pure
sampling noise; an R=10 same-deck control run is needed to say how much of the excess is real
movement versus variance-estimation artifact on n=10 skewed samples. **That control has not been
run.** It is moot for screening (no regeneration) but matters if the carry is ever built.

## Recipe

`scripts/deck_compare.py <spec.json>` does all of this; the steps are here so the tool is auditable,
not so anyone re-does them by hand.

1. Express every combination as **base deck + card → new count**, and derive numbering by
   **inheritance** (unchanged copies keep their numbers, a replacement inherits the number it
   replaced). Never renumber.
2. Give **every arm the same apparatus** — one `profile` (and, through it, one exhaustive keep
   sidecar) and one `value_profile`, pinned explicitly in the manifest, from the pool's base deck.
   Attaching the play profile is not optional: without it the arms play a deck nobody ships (top).
3. Configure the value leaf so it cannot decide: `value_model: false` + `ladder_value_leaf: true`.
4. Pre-flight the keep table's coverage; if any combination names a card it never bucketed, drop the
   table from **every** arm rather than from some.
5. Pool every arm's every game into ONE `mtg --batch` manifest (per CLAUDE.md) — `deck_numbering` is
   per job, so there is no reason to split.
6. Score paired by game index, unwon = `max_turns + 1`.
7. For any result whose margin over the apparatus bias is not several-fold, `--floor <tag>` measures
   that bias directly instead of predicting it.

Only when a combination is chosen for adoption does it earn its own keep table / value leaf, via the
normal `.claude/skills/mulligan-profile.md` and `value-leaf.md` routes.

## The bias floor — the honest framing (user, 2026-08-11)

A shared apparatus always carries *some* bias. The question is scale: **does the effect clear the
bias floor?** So the screen reports effect, floor, and their ratio (`--floor <tag>`), and escalates
when the ratio is low. Bias = the delta measured under the variant's own table minus the delta
measured under the shared one, paired over the same game indices.

### Re-measured with the profile attached (slivers, 10 cells x 60,000 paired games)

The one case that has been measured correctly end to end — 2 Aether Vial → 2 Muscle Sliver, every
table generated with the deck's profile and value sidecar present:

| apparatus | effect | se | identical |
|---|---|---|---|
| **shipped R=60 table — what the screen runs** | **−0.0303** | ±0.0014 | 90.9% |
| base's own R=10 table | −0.0296 | ±0.0015 | 90.0% |
| variant's own R=10 table | −0.0345 | ±0.0015 | 89.9% |
| base's own R=10 table, **profile-less** (this morning's config) | −0.0778 | ±0.0014 | 91.3% |
| variant's own R=10 table, **profile-less** | −0.0796 | ±0.0015 | 91.1% |

| bracket | bias | floor | effect/floor |
|---|---|---|---|
| matched R=10, profiled | −0.00488 ± 0.00150 (t = −3.26) | 0.0079 | **3.8×** |
| operational: shipped R=60 vs the variant's R=10 | −0.00422 ± 0.00128 (t = −3.28) | 0.0068 | **4.5×** |
| matched R=10, **profile-less** (reproduces the old number) | −0.00175 ± 0.00146 (t = −1.20) | 0.0047 | 16.7× |

**The profile-less configuration flattered the method in both directions at once** — it inflated the
effect 2.6× (−0.078 vs −0.030) *and* shrank the floor (0.0047 vs 0.0079). The advertised 9× margin is
really **3.8–4.5×**. The result survives; the comfort does not.

Two readings that were wrong before and are now coherent:

- **The bias is real and has the expected sign.** Each table flatters the deck it was fit to
  (t ≈ −3.3), where the profile-less bracket had it lost in noise. Its size, ~0.005t, is what the
  screen is actually risking.
- **A deck's own R=10 table no longer beats the shipped one — it loses to it by ~0.032t**, and by
  almost exactly the same amount for both decks (+0.0319 base, +0.0316 variant). Among R=10 tables the
  own/foreign gap is +0.0039 in the *correct* direction (own better). The old "a deck's own table made
  it significantly worse, which is impossible" reading was itself the profile-less artifact.

The practical consequence is unchanged but better founded: an R=10 bracket arm plays ~0.032t worse
than the shipped apparatus — the size of the whole effect — so **the bracket overstates the floor**
and can never be read as the accurate arm. A level difference cancels out of the
difference-of-differences; the interaction does not.

### Burn, re-measured WITH the profile (2026-08-12) — BOTH rows

The doc's top open item was that both burn floors were profile-less. Both re-run through
`deck_compare.py --floor` (which always attaches the profile), 20,000 paired games, R=10 bracket:

| burn | effect, profile-less | **effect, WITH profile** | bias, profile-less | **bias, WITH profile** | floor | margin |
|---|---|---|---|---|---|---|
| 1 Skullcrack → 1 Lightning Bolt | −0.0324 | **−0.0315 ± 0.0015** | +0.00220 ± 0.00129 | **−0.0015 ± 0.0018** (t = −0.87) | 0.0051 | **6.14x** |
| 1 Skullcrack → 1 Mountain | +0.0248 | **+0.0239 ± 0.0023** | +0.00163 ± 0.00153 | **+0.0011 ± 0.0023** (t = +0.48) | 0.0057 | **4.22x** |

**Burn's effects were NOT distorted by the profile-less bug** — −0.0315 vs −0.0324 and +0.0239 vs
+0.0248, both within noise. That is the opposite of slivers, where profile-less inflated the effect
**2.6x**, and the reason is deck-specific: `DefaultProfile` zeroes `vial_target_mv`, which is
deck-*defining* for slivers (the engine stops casting Aether Vial altogether), while burn has no
equivalent profile-carried lever to lose. The top-of-document correction's "all absolute effect sizes
are invalidated" is right to be conservative but wrong as a prediction — the damage scales with how
much of a deck's identity lives in its profile, and for burn it was ~nil.

**On the bias sign, do not over-read this.** Collecting the three correctly-measured brackets:

| bracket | bias | t |
|---|---|---|
| slivers, 2 Vial → 2 Muscle | −0.00488 ± 0.00150 | **−3.26** |
| burn, Skullcrack → Bolt | −0.0015 ± 0.0018 | −0.87 |
| burn, Skullcrack → Mountain | +0.0011 ± 0.0023 | +0.48 |

Only **slivers** resolves the bias at all. Both burn brackets are consistent with zero, in opposite
directions. So "each table flatters the deck it was fit to" is established on **one** deck, and what
burn adds is only an upper bound — its apparatus bias is at most ~0.002–0.005t, sign unresolved.

Also measured: burn's R=10 bracket table plays **+0.0495 ± 0.0041** (bolt) and **+0.0615 ± 0.0042**
(mountain) weaker than its shipped R=60 one — *larger* than slivers' +0.032t, and consistent across
the two burn runs. "Regeneration only pays at high R" now holds on a second deck, and the penalty is
deck-dependent: do not carry slivers' 0.032 around as a constant.

On "floor tracks CRN distance", there are now three correctly-measured points, and they are in the
conjectured order — burn/bolt (spell→spell, CRN ~0.07) → 0.0051, burn/mountain (spell→land, CRN
≥0.22) → 0.0057, slivers (CRN 0.32) → 0.0068–0.0079. Three points across two decks, every bracket
low-R and noise-dominated: suggestive, still not a predictor.

### The earlier burn numbers (PROFILE-LESS — kept only for shape)

| change | deck | CRN dist | effect | apparatus bias | floor | effect/floor |
|---|---|---|---|---|---|---|
| Skullcrack → Lightning Bolt (spell→spell) | burn | ~0.07 | −0.0324 | +0.00220 ± 0.00129 | 0.0048 | 6.8× |
| Skullcrack → Mountain (spell→**land**) | burn | ≥0.22 | +0.0248 | +0.00163 ± 0.00153 | 0.0047 | 5.3× |
| 2 Aether Vial → 2 Muscle Sliver | slivers | 0.32 | −0.0770 | −0.00332 ± 0.00264 | 0.0086 | 9.0× |

Every row ran profile-less in both layers, and the slivers row is now known to be wrong by 2.6× on
the effect and ~1.7× on the floor. "Floor tracks CRN distance" was read off these three points and
**has not been re-tested**; treat it as an untested conjecture, not a predictor.

### At R=10, table REGRET dominates table FIT — regeneration can make it worse

Measured against the shipped R=60 table (positive = the R=10 table plays worse):

```
variant deck on its OWN R=10 table    +0.03157 +-0.00146  (t = +21.6)
base deck on its OWN R=10 table       +0.03193 +-0.00153  (t = +20.9)
base deck on the VARIANT's R=10 table +0.03578 +-0.00154  (t = +23.2)
```

An R=10 table costs **~0.032t** against the shipped one — as large as the entire effect being
screened — while the own-vs-foreign *fit* difference among R=10 tables is only **+0.0039**, an order
of magnitude smaller and in the correct direction. Consequences:

- **Do not "regenerate for accuracy" at R=10.** It swaps a ~0.005t systematic bias for a ~0.032t
  regret. Regeneration only pays at high R (R=40 regret ~0.0006t).
- The cheap low-R **bracket** stays the right guard precisely because it *overstates* the floor —
  conservative, the correct direction for a safety check. It must never be the "accurate" arm.
- Every bias number therefore bounds *the difference between two low-R tables*, not the gap to a
  well-fit one.

Deckbuilding aside: cutting 2 of the 4 Vials for Muscle Slivers is worth **−0.030t** (not the
−0.077t the profile-less run reported) — still the largest effect measured here, and consistent with
Vial being weak in a 1v1 goldfish racing to T4 (user's own reading, and a reason to expect it to look
better under the planned 2HG format).

**The category change did NOT show a larger floor.** Three limits on that result before it is trusted:
1. burn is a poor test — 24→25 lands is a 4% move in a land-rich aggro deck with no land-count cliff
   near its keep boundary.
2. Both tables are R=10, carrying ~0.01t of their own regret (the gen's own R curve) — ~5× the bias
   being detected. This bounds *the difference between two mediocre tables*, not the gap to a well-fit
   one.
3. burn contains no Vial-like card (see below), which is the mechanism actually under dispute.

An earlier 5,000-game read of the same quantity gave −0.0032 and was pure noise (the sign flipped at
60k). **Do not read direction from an underpowered bias estimate.**

## Bucketing was not unstable — it was profile-less

The working assumption behind "pin the bucketing over the pool" was that `EquivalenceDiscovery` drifts
between runs and between decks, so cross-table cell addressing needs a frozen bucket map. The evidence
for the drift was that a scratch R=10 gen merged Ancient Ziggurat into the land bucket where the
committed R=60 table keeps it separate. That evidence was the profile-less bug:

| slivers base deck, R=10 discovery, 400 probes | buckets | size-7 cells | Ziggurat |
|---|---|---|---|
| scratch dir (no `profile.json`, no `value.json`) | 9 | 4,411 | merged into the 18-card land bucket |
| profile + value sidecar present | **10** | 7,758 | **separate — identical to the committed R=60 map** |

With the deck's profile attached, R=10 discovery reproduces the committed bucketing *exactly*, all ten
buckets. The mechanism is the user's own argument: `vial_target_mv` is 0 under a default profile, so
the engine never plays Aether Vial off anything, so Ancient Ziggurat's `creature_mana_only` restriction
(`ManaPayment.cpp:50` — it is the one land in the deck that cannot cast Vial) never costs a probe
anything and it merges with the lands it is otherwise identical to. Discovery had the interaction
right; it was shown a deck in which the interaction did not exist.

Consequences:

- **Discovery is far more reproducible than recorded**, so pinning the bucket map is a *guard*, not a
  correction — and cross-table cell addressing (the multi-source library's precondition) is much more
  attainable than the earlier reading implied.
- A **payment-capability signature** is still worth having, but as a cheap assertion rather than a fix:
  two mana sources that differ in what they can pay for must not merge, and here that would have caught
  the profile-less run's map without needing to know *why* it was wrong. That is the useful shape —
  an invariant that fires on a mis-configured gen.
- **Profile-less gen is ~1.8x cheaper** (4,411 vs 7,758 size-7 cells) because merging buckets collapses
  the cell space. A gen that finishes suspiciously fast is a symptom, not a win.

## Change-kind classification: use the measured CRN distance, not the type line

`EquivalenceDiscovery` already emits nearest-neighbour distances in **mean |Δ win-turn| per probe** —
the same units as the bias to be predicted — and the buckets are cached per deck. Slivers, from the
committed table:

```
[0] Leeching, Muscle, Predatory, Sinew      [4] Aether Vial          [7] Hatchery Sliver
[1] Cavern, Courtyard, Sliver Hive, Unclaimed  [5] Ancient Ziggurat  [8] Mutavault
[2] Galerider, Plated, Striking             [6] Cloudshredder        [9] Thrumming Hivepool
[3] Crystalline, Hibernation
```

| slivers nearest-neighbour distance | turns |
|---|---|
| Ancient Ziggurat ~ Cavern of Souls | 0.05 |
| Hatchery ~ Crystalline | 0.06 |
| Leeching ~ Hatchery | 0.16 |
| **Aether Vial ~ Ancient Ziggurat** | **0.3225** |

Two independent confirmations of the user's role argument, from behaviour alone: **Aether Vial merges
with nothing** (Artifact, but not grouped with lands *or* creatures — it is the most isolated card in
the deck), and **Ancient Ziggurat separates from the other four creature-lands** (it is
`creature_mana_only`, verified live at `ManaPayment.cpp:50`, so it genuinely cannot cast Vial).

**Limit of the metric:** this is a *play-quality* distance (mean |Δ| under substitution), not a
*mulligan-structure* distance. Vial's actual hazard — it makes low-land hands look keepable while not
being a land — is a threshold effect on which hands are kept, which an averaged win-turn delta
understates. Same for lands (burn's `Mountain ~ Searing Blood = 0.22` understates a spell→land keep
shift). So the classifier wants distance **plus** an explicit mana-source-count delta, and where it is
unsure it should *measure* the floor rather than predict it — bracketing with a throwaway ultra-low-R
(R=2–5) variant table is far cheaper than a shippable regeneration and answers the question directly.

Per user: the AI classifies by default, and the user can override when requesting a specific test.

## Format axis (2HG, planned)

A 2-Headed Giant mode (two opponents sharing 30 life) is planned. Two things to build in rather than
retrofit:
- **Format must be part of artifact identity** — filename *and* sidecar fingerprints. Today the keep
  table resolves as `<stem>.keepmodel.exhaustive.profile.json`, directory-relative and
  presence-activated, so a 1v1 table would silently load for a 2HG run (same trap class as
  `enabled: false` not disabling a value sidecar), and a merge could pool across formats.
- **The value-leaf transfer argument does NOT extend across formats.** It holds across deck
  combinations because the features are generic counts with no card-name indexing; but `OppLife` /
  `OppCreatures` / `OppTotalPower` are single-opponent scalars, so 2HG is a feature-space change and
  genuinely needs its own model.

## The coverage cliff — and why NO table is the right apparatus for a large edit

A hand containing a card the table has never bucketed does not get a *biased* answer, it gets **no
answer**: `ExhaustiveKeepPolicy::Keep()` sets `present=false` and the caller silently falls back to
the generic heuristic (same for `BottomCards`). So the failure mode for an edit that introduces a new
card is a cliff, not a gradient, and it is invisible in the delta.

Measured on the user's 8-card slivers change (4 Vial → 2 Ancient Ziggurat + 2 City of Brass;
3 Crystalline + 1 Hibernation → 2 Leeching + 2 Striking), 60,000 paired games, matched R=10 tables.
The variant collapses 9 buckets → 7 and 4,411 hands → 969 (its table is 4.6× cheaper to generate);
City of Brass merges straight into the land bucket, so it is covered by W1's own table but absent
from W0's. **Both tables here were generated profile-less** (see the correction at the top), so the
bucket counts and the 4.6× are not the shipped deck's — the qualitative cliff is the finding, the
numbers are not. The structural claim (`present=false` → silent heuristic fallback) is code, not
measurement, and stands.

```
W0: none -> W0's OWN table   -0.06310   full coverage
W1: none -> W1's OWN table   -0.06277   full coverage
W1: none -> W0's table       -0.04688   retains 74.7%   (~22% of hands fall back; 78% predicted)
W0: none -> W1's table       +0.00465   retains -7.4%   (~61% fall back)
```

A table is worth ~0.063t. Retention under a foreign table tracks the *coverage* prediction closely
when coverage is high, but goes **negative** when it is low — worse than no table at all — because the
hands that do resolve are resolved by a table fit to a different deck. The cliff is coverage loss plus
misfit.

| apparatus | measured effect | error vs honest |
|---|---|---|
| each deck its OWN table (honest) | −0.2005 | — |
| both on W0's table | −0.1846 | +0.0159 |
| both on W1's table | −0.2683 | −0.0678 |
| **both on NO table** | **−0.2008** | **−0.0003** |

**No table is essentially unbiased**, and structurally so: it is deck-*independent*, so both arms get
the identical generic heuristic and neither is favoured. A shared *table* is by construction fit to one
arm and covers the two asymmetrically. Cost: both arms play under a mulligan policy ~0.063t weaker
than they could. Untested risk: a deck whose value depends heavily on mulligan quality (a combo deck
needing specific pieces) could be mis-ranked under a weak-but-symmetric policy.

### Apparatus selection rule — superseded 2026-08-11 by the POOL TABLE

The rule below was the original two-way choice. It is kept because its *measurements* stand, but
"no table on either arm" is no longer what the driver does — see the pool table immediately after.

| condition | apparatus |
|---|---|
| card sets **mutually covered** (count changes only, incl. to zero) | shared table — better play, bias 0.002–0.003, tracks CRN distance |
| **not** mutually covered (any card the other table never bucketed) | ~~no table on either arm~~ — symmetric and unbiased, but see below |

### Generate the table for the POOL, not the base deck (user, 2026-08-11)

Dropping the table is symmetric, and symmetric is not the same as cheap. Three costs, all measured
here, none of which the original rule priced:

- **~0.063t of play quality on BOTH arms** (the doc's own figure for what a table is worth);
- **~22x per-game wall** (slivers 9.8 → 254.8 ms/game);
- **bottoming regresses to the lookahead** — and `cfg.bottoming_enabled = true` is now baked ON at
  generation with no off switch ("a footgun no agent should be able to reach"), so **all 9 committed
  sidecars have it enabled**. Dropping the table therefore doesn't restore some neutral default; it
  takes a facility every shipped deck actually uses.

So the driver now generates the apparatus for the **pool** — the union of every arm's cards at the
highest count any arm plays them — whenever the shipped table cannot answer every arm. Coverage is
total **by construction**, closing both of `Decide`'s `present=false` paths at once:

- every arm's cards are in the union → nothing is unbucketed;
- every bucket's union count ≥ that bucket's count in any arm → `EnumComps`' cap is never binding for
  an arm, so every reachable composition is enumerated.

`verify_coverage()` asserts exactly that before any game runs and refuses otherwise — an unverified
coverage claim would put us back where we started. `verify_bottoming()` asserts the other half on the
artifact itself, because `DecideBottom` fails *independently* of `Decide`: no bottoming block, the
flag off, or an entry with no target row each silently restores the rollout-per-candidate path. On
the generated pool table, **0 of 10,231 entries** lack a usable bottoming row (shipped R=60: 0 of
7,758).

Measured end to end on slivers + {Ziggurat 2→4, Hivepool 1→4}: generation took **881 s** on 24
threads, and the screen then ran at **21–38 ms/game** against the **254.8 ms/game** the dropped-table
route costs — the table pays for itself within one 20k-game screen.

The pool table is a **screening apparatus, not a shippable one**: `pool_R` defaults to 10, and an
R=10 table plays ~0.032t weaker than a shipped R=60 one. That is the right trade because it is
*symmetric* (own/foreign fit among R=10 tables is only 0.004t) and because the thing it replaces is
strictly worse on both axes — 0.063t weaker instead of 0.032t, and 22x slower instead of 1x.
Adoption still goes through `mulligan-profile.md`; a pool table never becomes a deck's sidecar.

#### Measured: the union deck does NOT bias the comparison

The symmetry argument above was structural, so it was tested. slivers, `cut_vial` (4 Aether Vial → 4
extra Muscle Sliver) — an edit the **shipped** table already covers fully, so all three apparatuses
are valid and the deltas are directly comparable. 2 decks x 3 tables = 6 cells, one pooled batch,
20,000 paired games:

| apparatus | delta | se | identical |
|---|---|---|---|
| shipped R=60 base table | −0.0332 | ±0.0032 | 84.3% |
| base deck's own R=10 | −0.0328 | ±0.0035 | 83.4% |
| **POOL R=10 over the 64-card union** | **−0.0353** | ±0.0035 | 83.6% |

| difference-of-differences (paired) | value | t |
|---|---|---|
| **union bias** — pool R=10 vs base R=10, *R controlled* | **−0.0026 ± 0.0033** | −0.77 |
| total vs shipped — pool R=10 vs R=60 | −0.0021 ± 0.0029 | −0.71 |
| R cost alone — base R=10 vs R=60 | +0.0004 ± 0.0030 | +0.15 |

**No detectable union effect.** And the *level* cost is exactly the known R=10 cost, not more — the
pool table plays +0.0330/+0.0309 weaker than shipped on the two arms, against the base R=10 table's
+0.0308/+0.0313. Generating over the union costs what generating at R=10 costs, and nothing extra.

Three limits worth keeping attached to that number:

- **One deck, one edit, one R** — not a trend, exactly as with the floor measurements above.
- At R=10 the table's own sampling noise (~0.01t) is the size of what is being bounded, so this reads
  "no effect above R=10 noise", not "zero". The 2se bound is roughly ±0.007.
- **It is the favourable case by construction.** Because the changed bucket already held ≥ 7 copies,
  the union's composition grid was *identical* to the base's (7,758 cells both), so only the `count`
  vector and the rollout deck differed. A pool table that adds a genuinely NEW bucket — the case the
  driver actually reaches for — is **not** covered by this measurement. Closed below.

#### Closed (2026-08-12): a NEW bucket does not bias it either — and three things about bucket shape

Two more cases, on burn, 4 cells x 20,000 paired games each in one pooled batch. The three-apparatus
design above does not transfer: an introduced card is *unbucketed* by the shipped table, so there is
no reference apparatus common to both arms. Each arm instead runs under **its own R=10 table** (which
covers that arm fully) and under the **pool table** — every cell fully answered, no fall-through
anywhere — and the difference-of-differences decomposes exactly into two WITHIN-deck nulls.

| case | pool K / cells (base = 10 / 10,945) | union bias | t |
|---|---|---|---|
| slivers, Vial 4→0 / Muscle 4→8 — grid unchanged | 10 / 7,758 = base | −0.0026 ± 0.0033 | −0.77 |
| burn, 4 Skullcrack → 4 Lava Spike — card MERGES | 10 / 11,000 (**+0.5%**) | −0.0075 ± 0.0055 | −1.36 |
| burn, 2 Mountain → 2 Mutavault — **NEW bucket** | 11 / 17,853 (**+63%**) | +0.0010 ± 0.0049 | +0.21 |

Three cases, both signs, a grid that grew 0% / 0.5% / 63% — all consistent with zero. The open item is
closed: **the pool table is a sound shared apparatus, including when it adds a dimension.** The same
R-noise caveat applies (each bound is ~±0.01 at 2se), and the effects being screened were −0.1169
(Lava Spike) and −0.0007 (Mutavault), so the first clears its bias by ~15x and the second is simply a
wash. Three structural findings came out of it, and two contradict what this doc said before:

1. **An introduced card usually MERGES into an existing bucket — a new bucket is not the default.**
   Lava Spike ({R} sorcery, 3 to the face) landed in *Lightning Bolt's* bucket in every table that
   held both, the arm's own and the pool's. The union grid grew by exactly **55 cells**, which is the
   arithmetic of one 4-of bucket's cap rising 4 → 7 at hand size 7 (45 + 9 + 1), not a new dimension.
   Forcing a genuinely new bucket took a card unlike anything in the deck — Mutavault, a colourless
   land in a mono-red deck. "Introduced card" and "new bucket" are different questions.
2. **A dropped card REMOVES a dimension from that arm's own table.** The Lava Spike arm's own table is
   K=9 / 6,120 cells against the base's K=10 / 10,945 — strictly *coarser*, because Skullcrack's
   bucket is gone and the new card merged into Bolt's. An arm's own table is not automatically the
   better-fitting one, and two arms' own tables are not the same kind of object.
3. **The pool table REFINES every arm's own partition, so it plays better, not worse.** All four burn
   nulls came out negative (pool faster): base −0.0018 / −0.0044, variant −0.0092 / −0.0034. The union
   holds every card any arm plays, so its partition is at least as fine as any arm's — "each table
   flatters the deck it was fit to" is a **shipped-table** reading and does not carry over. `--floor`
   no longer prints an expected sign on the pool route; it prints the two nulls instead.

Measured on slivers + {Ziggurat 2→4, Hivepool 1→4}: the union is 65 cards and 17,831 cells against
the base table's 14,117 (**1.26x**), and discovery reproduced the same 10 buckets with the raised
counts. Cell counts for the other shapes, which is what sets the price:

| pool vs base | K | cells | vs base |
|---|---|---|---|
| base slivers | 10 | 14,117 | 1.00x |
| a count change inside a bucket already ≥ 7 (Vial 4→0, Muscle 4→8) | 10 | 14,117 | **1.00x — the same grid** |
| a new card that MERGES into an existing bucket | 9 | 8,406 | 0.60x |
| a new 2-of in its own bucket (replacing a 2-of bucket) | 11 | 14,117 | 1.00x |
| a raised small bucket (Hivepool 1→4) | 10 | 14,975 | 1.06x |
| a new 4-of in its own bucket | 11 | 24,235 | 1.72x |

The first row is worth internalising: because `cap[b] = min(count[b], H)`, a bucket already holding ≥ 7
copies is capped by the *hand size*, so raising it further adds **no cells at all** — the pool table
has exactly the same composition grid as the base table, and differs only in the `count` vector fed
to `HandWeights`/`ComputeDopt` and in the deck the rollouts are played on. Cell count is driven by
small buckets, not by how big the edit looks.

Cost is not proportional to cells, though: the base-deck R=10 table (7,758 cells) took **~28 min**
while the 65-card union table (10,231 cells) took **14.7 min**. The generator is adaptive, so the
bill is set by how many cells sit near a keep/mull flip and need refining to the cap, not by the grid
size. Do not size a gen from the cell count alone.

### The bracket itself had to change for an introduced card (2026-08-12)

`--floor` used to run **both** decks under the *variant's* own table. That is right whenever the arms
hold the same cards and only counts differ — one table per delta keeps the delta internally
consistent. It is wrong the moment an edit **drops** a card: the variant's table then has no bucket at
all for a card the base deck still plays, so the `base__own` cell silently loses the table on every
hand holding one. For a 4-of swap that is **40.0% of hands** (1 − C(56,7)/C(60,7)), on one arm only,
each of them falling to the generic heuristic *and* to lookahead bottoming. At ~0.063t for a dropped
table that is ~0.025t of one-sided damage — an order of magnitude more than the fit bias the bracket
exists to measure, and not something more games can average away.

So when the arms' card **sets** differ, each deck is now bracketed on its own R=10 table. Two
properties follow, and the second is what makes it sound rather than merely covered:

- no cell in the bracket has any fall-through (the driver prints per-cell coverage, all `full`);
- the difference-of-differences decomposes into two **within-deck** nulls,
  `bias = [var@own − var@shared] − [base@own − base@shared]`, so the level difference between two
  *distinct* tables cancels instead of leaking into a delta. That is the objection to mixing
  apparatuses inside one delta, and it is answered by construction rather than by hoping the two
  tables are equally strong. The driver prints both nulls; a bracket is only worrying when they
  **differ**, and two large equal nulls are a level difference that cancels.

Validated by rebuilding the Mutavault measurement through `--floor` with every table cached: it
reproduces the standalone probe to the digit (pool +0.0003 ± 0.0032, own-vs-own −0.0007 ± 0.0048,
nulls +0.00435 / +0.00335, bias −0.0010 ± 0.0049).

#### The bug that first run caught: a stale sidecar out-ranking the one we linked

The first attempt at that validation disagreed with the probe by 10x, and the cause was neither
statistics nor the new code. `AttachExhaustiveSidecar` (`src/ai/MulliganProfileIO.h:858`) resolves
`.keepmodel.exhaustive.profile.json.gz` **first** and only then the plain `.json`. `apparatus_dir()`
removed just the one name it was about to write, and `app_ship/` is reused by every `--floor` on a
deck — so a run whose shared apparatus was a **pool table** (plain `.json`) actually ran under the
**shipped R=60 `.gz`** left behind by an earlier run, and reported that table's numbers under the pool
table's label. The per-cell coverage print said `full` throughout, because it is computed from the
table the driver *intended*. The tell was the nulls: +0.0388 / +0.0419, which is exactly burn's known
R=10-vs-R=60 gap (+0.0495 / +0.0615) rather than a pool-vs-own difference.

Same family as the profile-less bug: a scratch directory silently supplying a *different apparatus*
than the one named in the output. `apparatus_dir()` now clears **both** resolvable names before
linking. The `.bincache` beside each is deliberately left — it is keyed by the source's (size, mtime),
which follows the symlink, so it self-invalidates, and deleting it would charge every run a full
re-parse (14–68 s on the big sidecars).

Two smaller fixes went with it: `--floor` now uses the same **pooled card_scores profile** the screen
uses (it did not, so a bracket on an introduced card folded the card_scores asymmetry into what it
reported as apparatus bias), and `gen_table()`'s reuse fingerprint now includes **R** (it was keyed on
the arm's counts alone, so changing `floor_R` would silently reuse a table generated at another R —
worth 0.03–0.06t, the size of a screened effect).

### Why regenerate the union instead of topping up the base table

The generator already has whole-pool warm-start (`MTG_KEEP_PRIOR_RAW` + change-detection carry +
execution-trace reuse + prune-set carry), but it is built for **the same decklist on a new commit**
and says so at the point of refusal:

```
[keepgen] PRIOR-RAW: fingerprint mismatch (bucket/deck/equiv_seed/K/max_mull)
  -- REFUSING (a changed decklist needs bucket translation, not yet built)
```

**Bucket translation is the missing piece**, and it is worth building: the cell counts above say a
pool table shares most of its cells with the base one (a raised bucket needs only ~6% new cells; a
new 4-of bucket ~40%), so a translated warm-start would turn a per-pool full generation into a small
top-up. Until then the union regeneration is the correct-by-construction route, and it is the same
one-time-per-pool cost the skill has always quoted for a mulligan table.

The pre-flight test is set membership against `ExhaustiveKeepPolicy::name_to_bucket` for every card of
every combination — free, and it selects the mode rather than merely refusing.

### The SECOND cliff: composition coverage (2026-08-11)

Card membership is not the whole test, and the half that was missing sits on the *mainstream* case —
a plain count change. `ExhaustiveKeepPolicy::Decide` sets `present=false` in **two** places:

```cpp
auto it = name_to_bucket.find(n);
if (it == name_to_bucket.end()) { present = false; return false; }   // (a) unbucketed card
...
auto it = keep.find(comp);
if (it == keep.end())           { present = false; return false; }   // (b) untabled COMPOSITION
```

Only (a) was checked. (b) exists because the analyzer enumerates compositions bounded by the deck it
was generated for — `cap[b] = std::min(count[b], H)` in `EnumComps` — so a combination that **raises**
a bucket above its base size makes hands reachable that were never tabled. They fall silently to the
generic heuristic, **on the arm that raised it and nowhere else**: the exact lop-sidedness the
card-level pre-flight exists to prevent.

Cuts are always safe (they only make compositions unreachable), and a bucket already holding ≥ 7
copies cannot overflow — every composition 0..7 was enumerated. So the exposure is precisely *raising
a bucket that has fewer than a handful of copies*. Exact hypergeometric rates on slivers:

| combination | overflowing bucket | P(hand falls through) |
|---|---|---|
| Thrumming Hivepool 1 → 4 | `[9] Thrumming Hivepool 1->4` | **6.32%** |
| Ancient Ziggurat 2 → 4 | `[5] Ancient Ziggurat 2->4` | 0.39% |
| Cloudshredder 3 → 6 | `[6] 3->6` | 0.099% |
| Hatchery 4 → 8 | `[7] 4->8` | 0.020% |
| every combination in the worked example above | — | **0.000%** (they only move 14- and 16-card buckets) |

"Try 4 of the card I run 1 of" is an ordinary deckbuilding question, and it costs 6.3% one-sided
fall-through — at a table worth ~0.063t, roughly **0.004t of bias, the size of the whole apparatus
floor**.

`deck_compare.py` now computes the rate exactly (hypergeometric over the enumerated compositions,
milliseconds) and treats it as a **bias budget rather than an all-or-nothing rule**: below
`max_fallback` (default 1% ≈ 0.0006t, a tenth of the measured floor) it keeps the table and prints
the rate; above it, the table is dropped from every arm. A binary rule would be wrong in both
directions here — dropping the table is not free (~0.063t on *both* arms and ~22x per-game wall), so
trading it away for a 0.008% fall-through is a worse deal than the bias it avoids.

The same check now runs inside `--floor`, where each table is fit to a *different* deck and the
generating counts are what set the caps. It immediately showed an asymmetry nobody had quantified:
in the `half_vial` bracket, **the base arm on the variant's own table falls through on 0.39% of
hands**, because that table capped Aether Vial at 2 while the base deck runs 4.

## The introduced-card route (2026-08-11)

Every measurement above is a **count** change over a fixed pool. Introducing a card the deck has
never held is the case a user reaches for first — *"what if I ran 4 Lava Spike instead of 4
Skullcrack?"* — and it was the least-defended path in the driver. It fails in four places; three are
mechanical and now guarded, the fourth is a reading task that no guard can do.

### 1. Dropping the table dropped the PLAY PROFILE too (bug, fixed)

The table-drop above was implemented as "pass no profile for this job". But `BatchRunner::ParseJob`
treats an absent `profile` key as *auto-detect* — `<deck>.profile.json` beside the **deck**, and the
arm decklists are written to a scratch directory under `logs/deckcmp/`. `ProfileCache::load` then
falls back to `DefaultProfile()` for a path that does not exist, in silence.

So every arm of a screen that introduced a new card ran profile-less: the *third* layer of the
two-layer bug corrected at the top of this document, and it sat on exactly the route that triggers it
(an introduced card is never bucketed, so it always drops the table). Dropping the table is now
`MTG_EXHAUSTIVE_PROFILE=none` on the batch — the documented override in `AttachExhaustiveSidecar`,
which suppresses the sidecar and nothing else — and the profile stays pinned in every job.

### 2. An unscored card is scored as an EMPTY SLOT, in the one arm that plays it

`ComputeHandScore` (`AIEngine.cpp`) iterates the hand and **skips** any name absent from
`profile.card_scores`; the keep gate then compares that short sum against `hand_score_threshold`.
`CardScore()` likewise returns 0.0 for an unknown name, so the bottoming pick discards the new card
first. A profile's `card_scores` covers the cards of the deck it was analysed from, so **every**
introduced card lands in this hole.

This is not a symmetric approximation like the dropped table. It is a one-sided penalty on whichever
arm plays the new card, applied at mulligan time, and invisible in the output.

The fix is one `mtg-analyze` run over the **union** of every arm's cards (the same fixed recipe that
produced the shipped scores: default keep, no gate, `vial_target_mv` derived from the deck), with
only the missing entries merged into a copy of the shipped profile. Merging rather than replacing is
what keeps the base arm identical: an added name cannot appear in a base-arm hand, and the shipped
threshold keeps the meaning it was fitted with. Measured cost on the 62-card slivers union: **21 s
wall** on 24 cores.

#### Measured — real, one-sided, and much smaller than the code suggests

Two 4-cell paired runs, each `{base, variant} x {shipped profile, pooled profile}` in one batch off
the same game indices, no keep table on any arm (which is what an introduced card forces anyway):

| deck | introduced card (its pooled score) | effect under shipped | under pooled | **bias** | variant arm identical |
|---|---|---|---|---|---|
| burn, 20,000 paired | Lava Spike (+0.213) | −0.1188 ±0.0036 | −0.1188 ±0.0036 | **−0.0001 ±0.0000** | 100.0% |
| slivers, 6,000 paired | City of Brass (+0.024) | −0.0028 ±0.0008 | −0.0007 ±0.0013 | **+0.0022 ±0.0010** (t=+2.1) | 99.6% |

**The base arm was byte-identical in both** (0 of 11,967 and 0 of 6,000 games differ) — the merge
provably touches only hands that can hold the new card, which is what makes it safe to apply to every
arm rather than only to the arm that needs it.

Burn's zero is not noise, it is structural: `burn.profile.json` carries a **`keep_model`**, and
`KeepHand` hands the decision to the model at every hand size *above* the size-1 floor, so the
`card_scores` gate below it is unreachable. The model's features (`ComputeKeepFeatures`) are
structural — mana demand, castability by MV, key-piece counts by name list — and never read
`card_scores`. The residual 1-game-in-20,000 is the *bottoming* channel, where `CardScore()` only
breaks ties among the rollout-optimal removals.

So the channel is live only on a deck with `card_scores`, a real `hand_score_threshold`, and **no**
keep model. Surveying the 12 committed profiles:

| profile shape | decks | gate |
|---|---|---|
| has `keep_model` | burn, Anti-Lifegain, Hinata2 | dead — the model decides |
| `hand_score_threshold = -1e18` (`NO_GATE`, what the analyzer emits today) | Auras, Creature Giving, Dragonstorm, FiveColour, Goblins | dead — every hand clears it |
| no `card_scores` at all | treasure_hunt | dead |
| **live gate** | **slivers_vial, Knights** | **exposed** |

And slivers' +0.0022 is for a *land* scoring +0.024. A card scoring like Lava Spike (+0.21) in an
exposed deck would move far more hands, so treat the number as a lower bound on a narrow case, not as
the size of the effect in general. The honest summary: the pool profile is **21 s of insurance
against a real but small and deck-shape-dependent asymmetry** — worth doing automatically because it
is cheap and provably inert where it does not apply, not because it rescues a big number.

The derivation is deterministic — fixed `pool_seed` — so the pooled profile is byte-reproducible from
the spec, and reuse is keyed on the union *plus the source profile's bytes* rather than on the file
existing (regenerating the deck's profile must not silently reuse the old merge).

### 3. An unimplemented card is a pre-flight, not a runtime error

`CardDatabase::Get` throws `Unknown card template` — inside a pooled batch, i.e. after the manifest is
built, taking every arm's games with it. `deck_compare.py --preflight` now checks membership in
`cards.json` before anything runs, and runs `analyze_deck.py`'s own `CheckExistingCoverage` oracle-text
scan over the introduced cards (imported, not re-implemented — a screen must hold a new card to the
same standard deck onboarding does). Both refuse, and the refusal prints the analyze-deck route.

Only *introduced* cards are gap-scanned. The base deck's cards came through analyze-deck already and
are present in every arm, so re-litigating them would block screens over a decision taken elsewhere.

### The price of the dropped table is ~22x PER GAME, and it is all bottoming

Introducing a card always drops the table (it is never bucketed), and the apparatus-selection rule
above accepts that as a play-quality cost of ~0.063t. It is also a **throughput** cost, which nothing
had measured. slivers, base arm, 300 games, 8 threads, idle box:

| apparatus | ms/game | avg |
|---|---|---|
| shipped table, bottoming on (the normal screen) | **9.8** | 4.1867 |
| shipped table, `MTG_EXHAUSTIVE_BOTTOM=0` (keep still tabled) | 233.1 | 4.1533 |
| no table at all (`MTG_EXHAUSTIVE_PROFILE=none`) | **254.8** | 4.1933 |

**The whole 22x is the bottoming path, not the keep path.** Without a bottoming table, `BottomCards`
falls back to the lookahead: a `RolloutWinTurn` per bottom candidate per bottom step. Turning off
*only* the table's bottoming already costs 24x, while its keep decisions stay free.

The planning consequence is concrete: a screen that introduces a new card costs ~20x more per game
than the same screen over count changes, on any deck whose sidecar has `bottoming_enabled: true`
(slivers does; most ship with it off, and those already pay the rollout price). Size the games
accordingly — or read the `ms=` on a 300-game probe first.

### 4. What no guard can catch: the engine's model of the card

`Skullcrack`'s `oracle_text` ends `[Life-gain lock and damage-prevention lock not modelled]`. Its
riders are irrelevant to goldfishing, so nothing is *wrong* — but a Skullcrack → Lava Spike screen is
comparing a card the engine models fully against a card it models partly, and the number will not say
so. The bracket notes are where that is recorded; connecting one to the question being asked is a
reading task, which is why the skill lists it as the AI's job rather than the driver's.

## Reweight, and the multi-source cell library (user, 2026-08-11)

**The policy separates cleanly into an expensive part and a free part**, and the code already reflects
it — `BuildPolicyFromTables(tables, count, bucket_members, deck_size, ...)` takes the deck composition
as a parameter *separate* from the rollout values:

```cpp
const std::vector<double> P = HandWeights(tables, count, deck_size);  // pure arithmetic
Dopt_pd[pd] = ComputeDopt(tables, K, P, max_mull, pd);                // pure arithmetic
```

So feeding a prior table's `V` with a **new deck's `count`** yields a correctly reweighted policy —
new hand probabilities, new `Dopt`, new keep flags — with **zero rollouts**. That is strictly better
than sharing a table raw, which is the same `V` with the *wrong* `count`.

`Comb(n,k)` returns 0 for `k > n`, so cells the new deck can no longer reach get `P = 0` and drop out
of `Dopt` automatically. **Removals are free and correct by construction.** Only one case needs
rollouts:

> a cell needs generating iff, for some bucket `b`, `comp[b] > old_count[b]` **and** `comp[b] <= new_count[b]`
> — i.e. only cells made *newly reachable* by a bucket's capacity rising.

Worked example (Hatchery 4→2, Leeching 2→4): Leeching is **bucketed** with Muscle/Predatory/Sinew, so
that bucket goes 14→16 — but a 7-card hand can never hold more than 7, so nothing above the old
capacity was ever reachable. Hatchery 4→2 only *removes* cells. Net: **zero newly-reachable cells**, so
the swap is an exact reweight with no rollouts at all. (There is no per-card "3 Leeching" cell; a hand
of 3 Leeching + 2 Muscle and one of 1 Leeching + 4 Muscle are the same cell.)

### Multi-source cell library

Once several combinations have been fully regenerated, each cell may have `V` from several tables.
Pooling them is attractive **because of the R=10 noise result above**: sampling noise (~0.01t) exceeds
fit bias (~0.002–0.003t), so averaging K sources trades a small bias increase for a ~sqrt(K) variance
reduction — likely a net win, and the opposite of the usual "never mix decks" instinct. It also makes
cost amortize the right way: every full regeneration permanently improves all future screens.

Prerequisites and guards:
- **Bucketing must be pinned over the pool.** Cells are compositions over buckets, so cross-table
  addressing needs identical bucketing. Discovery does vary with the deck (the 8-card case produced 9
  buckets for W0 and 7 for W1 — a genuine composition change), but it is *reproducible across runs* of
  the same deck: the one case that looked like run-to-run drift was the profile-less bug, and a correctly
  configured R=10 run reproduces the committed R=60 map exactly. So pinning is a cheap guard on a stable
  quantity, not a repair of an unstable one.
- **Separate entry point from `MTG_KEEP_MERGE`.** `ExhaustiveKeep.cpp:3504` rejects inputs whose
  `deck_fp` differs, and `:1168` names a modified-list carry as a separate feature. Pooling across
  seeds of one deck is EXACT; pooling across decks is APPROXIMATE. They must not share a door.
- **Free bias estimate:** with several sources per cell, inter-source spread is a per-cell error bar,
  draw-probability weighted — so the effect/floor report becomes a by-product instead of needing a
  second 60k-game arm under a second table.
- **Unverified:** source weighting by "closeness". Uniform pooling over covering sources is the safe
  start (noise > fit), but that rests on one deck at one R.

## Numbering: inherit, never renumber (user, 2026-08-11) — SPEC for the rework

The current implementation numbers by decklist POSITION, which only yields correspondence if each
combination is hand-authored in canonical slot form. The better formulation is to express a
combination as **base deck + an edit list**, and inherit numbers:

| operation | numbering | alignment |
|---|---|---|
| `replace(old, new)` | new **inherits old's number** | **exact** — identical key order in both decks; only the card identity at that one position differs, so any game that never draws it is byte-identical |
| `remove(card)` | its number vanishes; nothing is renumbered | survivors keep relative order; positions after it shift by one |
| `add(card)` | takes `max+1` | survivors keep relative order; positions after it shift by one |

**`replace` must be a PRIMITIVE, not sugar for `remove` + `add`.** They yield the same 60-card deck but
measurably different alignment: remove-then-add leaves a hole at the old number and puts the new card
at its own keyed rank, so the two decks diverge at *two* positions with a shift between them — the same
defect that made name-keyed pool numbering lose (80.3% vs 93.3%).

Invariants: **unique within a game** (`ShuffleByKey`'s stated contract — it is a total order on
`(key, m_number)`) and **non-zero** (0 is the "unnumbered" sentinel; all-zero is what made the batch's
reshuffle a silent no-op). **Contiguity is NOT required** and never was: `m_number` is never used as an
array index or bit position anywhere in the engine — all ~40 use sites are equality comparisons on a
per-copy identity token — and `main.cpp`'s scenario path already hands out out-of-range numbers.

This drops the pool file entirely and makes deck-SIZE changes work, which neither slot-form nor the
position-based implementation can do.

## Open / not measured

- **A high-R (R≥40) confirmation arm.** Every floor uses R=10 tables that cost ~0.032t against the
  shipped one — larger than the effect they bound — so "regenerate to confirm" is still unvalidated as
  a tier, and the *true* fit bias against a well-fit table is unknown in both size and sign.
- ~~Re-measuring the two burn floors with the profile attached.~~ **DONE, both rows** (see above).
  Both effects survived the correction unchanged (burn carries little of its identity in its
  profile, unlike slivers); both biases came out consistent with zero, so the expected-sign reading
  still rests on slivers alone.
- Whether the corrected floor is *deck*-sized or *edit*-sized — one deck, one edit is not a trend.
- ~~A pool table that adds a genuinely NEW bucket.~~ **DONE (2026-08-12), and it does not bias the
  comparison either** — burn + 2 Mutavault, K 10→11, grid +63%, union bias +0.0010 ± 0.0049. Three
  cases now (grid +0% / +0.5% / +63%), all consistent with zero. See the section above; it also
  showed that an introduced card usually *merges* rather than adding a bucket.
- Larger edits (3+ cards), whose earlier measurements were all profile-less. (The Lava Spike case is a
  4-of swap, but it is an introduced card measured against the pool table, not a shipped-table floor.)
- ~~A floor for an introduced card.~~ **Closed by the pool table.** `--floor` used to refuse an
  introduced card ("the screen DROPS the table … nothing to bracket"), which left the edit kind with
  the largest apparatus question un-bracketable. Now that the screen falls back to a pool table
  rather than to no table, the shared apparatus always exists, so `--floor` brackets *the pool table*
  against the combination's own — and reports that both arms are then low-R, so the usual "the
  bracket plays ~0.032t weaker" asymmetry does **not** apply and what remains is the union-vs-
  combination fit difference.
- The pool profile's own accuracy: the new card's marginals come from a union deck (60 + the
  introduced copies) at `MTG_ANALYZE_SCALE=2`, while the shipped scores may have been generated at
  scale 1 on the exact 60. Same recipe and same scale of quantity, higher variance; not calibrated.
- The R=10 same-deck control for the channel-(B) rms-z reading.
- Deck-size changes (add/remove without a matching replace) under inherited numbering — the route
  supports them by construction, but the alignment cost has not been measured.
- The reweight path (zero-rollout retarget of an existing table to a new combination) and the
  multi-source cell library: specified above, not built.

## The reporting / batching / accuracy pass (2026-08-12)

An end-to-end audit of the driver after the bracket rework. Everything here is either a guard that
refuses a silent failure, a number that was already computed and never printed, or a batch that was
being split when it did not need to be.

**Accuracy — three assumptions that nothing checked.**

- **The table's entry count is a fingerprint of the decklist it was generated for.** Every coverage
  answer rests on `spec.counts` being the *generating* counts (`fallback_rate` derives its caps from
  them), and nothing verified that. The sidecar records `commit`, `effective_R`, `buckets` and
  `max_mull` but not the counts — so the check is structural: the number of size-7 cells implied by
  the decklist must equal `len(entries)`. Exact on all 10 committed tables (K=10..21, 7,758..431,144
  entries), and it fires on a ±1-card edit (10,945 vs 10,990). This is the same mistake as the
  `--floor` coverage bug one level up: there, the *arm's* counts were passed as the generating counts.
- **The driver resolved keep tables in the OPPOSITE order to the engine.** `AttachExhaustiveSidecar`
  tries `.gz` first; `table_buckets()` tried the plain `.json` first. `decks/slivers_vial/` holds
  exactly that pair today — a gitignored **R=1** table beside the committed **R=60** `.gz`. Their
  buckets are identical, so no past measurement was affected, but their keep decisions are not:
  coverage was being computed from one file while the games ran on another. Now engine-order, and two
  candidates that disagree about the bucketing are a refusal.
- **A job that did not finish every game just made `n` smaller.** Everything pairs on the
  intersection of game indices, so a truncated batch printed an ordinary-looking number — the failure
  CLAUDE.md's no-timeout rule exists to prevent, arriving by another route. Now a refusal, with the
  log left intact.

**Accuracy — selection bias, which no guard can fix.** A screen reports the max of N deltas and calls
it the winner; that is optimistic even when every estimate is honest. `--confirm TAG` re-runs base vs
that combination on a disjoint block of games (`confirm_seed`, default `seed + 500000`) under the
**same** apparatus — `only` restricts which arms run, not which arms the apparatus is built from,
because rebuilding it would make a shrunken effect ambiguous between selection bias and an apparatus
change. Each run stamps a fingerprint (arms, profile content, table, play settings) and the comparison
refuses across a mismatch. Same train/held-out shape as `heuristic-optimization.md`.

**Batching.** `--floor` took one tag and ran one batch per tag, which is the loop CLAUDE.md forbids —
T tails, and the table generations between them serial. It now takes a comma-separated list and pools
every cell into one batch, deduped: `base` under the shared apparatus is one job however many
combinations are bracketed, and so is the base deck's own table whenever the edits drop a card. T tags
cost **2T+2 cells rather than 4T**. What is *not* done: a screen and a floor still re-run the shared
cells (byte-identical games). Reusing the screen's log across invocations was rejected — too easy to
mix two runs' games silently — but a `--with-floor` mode putting both in one batch would be honest.

**Batching, correctness half.** Two runs on one deck at once interleave: arm decklists and
`numbering.json` are rewritten per run and read by the engine when the *job* runs, minutes later.
Per-deck namespacing fixed the cross-deck case; a per-deck lock (PID-checked, so a killed run never
wedges it) fixes two specs over the same deck, which would have mis-numbered in silence.

**Reporting.** The batch's `[batch] heartbeat` and `SLOW-GAME` lines are teed to stdout as they
arrive — CLAUDE.md mandates the first-ten-minutes utilisation check, and those lines were landing in
a log that also carries one `[win]` line per game (the last floor run's only heartbeat was line
80,006 of 80,006). The apparatus block now prints the table's path, K, cells, `effective_R` and
`commit`, with a note when the engine has moved on; `--floor` says explicitly that it mixes engine
states. The screen prints a pairwise matrix of every combination against every other — the top two
are usually the interesting pair, and it was the one comparison the tool never printed. And `--floor`
reports both **per-arm nulls** rather than only the variant's, since the bias is exactly their
difference and a bracket only worries when they differ.

**One more that is not a driver bug:** `--dry-run` used to *generate keep tables* (10–40 min) before
deciding to run nothing, and its stale-table cleanup meant an interrupted dry run left the arm with no
table at all. It now reports what it would generate.

## Guards now in the tree (so the two-layer bug cannot recur)

- `mtg-analyze` **refuses** a keep-gen when `<stem>.profile.json` is not beside the decklist
  (`MTG_KEEP_ALLOW_NO_PROFILE=1` is the deliberate hatch). It refuses rather than warns because the
  route runs for tens of minutes and a warning at minute 0 is not read at minute 90.
- `scripts/deck_compare.py --floor` copies the profile *and* the value sidecar into the arm directory
  before generating, and keys table reuse on the arm's exact card counts **and R** (`.counts.json`),
  so editing a combination — or changing `floor_R` — cannot silently bracket against the previous
  run's table.
- `apparatus_dir()` clears **both** sidecar names the engine can resolve (`.json.gz` first, then
  `.json`) before linking the one it wants. Removing only the name it writes let a `.gz` left in a
  reused `app_*/` directory out-rank the fresh link, so a run reported one apparatus's numbers under
  another's label — with the coverage print, computed from the *intended* table, saying `full`.
- `--floor` runs on the same **pooled card_scores profile** the screen does, so a bracket on an
  introduced card cannot fold the card_scores asymmetry into what it reports as apparatus bias.
- `scripts/deck_compare.py` resolves `profile` / `value_profile` from the deck's **siblings** when the
  spec omits them, and **refuses** a screen with no profile at all (`"allow_no_profile": true` is the
  hatch, and it stamps an inert `_no_profile_deliberate` in the manifest so a later reader can tell
  the hatch from the bug). Making the load-bearing field optional was how the third layer hid.
- The **table-drop path** keeps the profile: `MTG_EXHAUSTIVE_PROFILE=none` on the batch, never
  `profile: null`, which BatchRunner reads as *auto-detect* and resolves to nothing.
- `--preflight` refuses an unimplemented or partially-implemented introduced card, and pools a
  `card_scores` entry for one the profile has never seen.
- Both decklist formats go through `analyze_deck.py`'s parser. The driver's own split-on-space text
  parser silently produced nonsense card names for a Cockatrice `.cod` file — which is **14 of the 17
  decks in `decks/`**, so the tool effectively only worked on burn, slivers and treasure_hunt.
- A **payment-capability signature** over mana sources is still worth adding as an assertion: two
  sources that differ in what they can pay for must not merge into one bucket. It would have caught
  the profile-less bucketing without anyone needing to know why it was wrong.
