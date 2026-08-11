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

### Apparatus selection rule (measured)

| condition | apparatus |
|---|---|
| card sets **mutually covered** (count changes only, incl. to zero) | shared table — better play, bias 0.002–0.003, tracks CRN distance |
| **not** mutually covered (any card the other table never bucketed) | **no table on either arm** — symmetric, unbiased, zero generation cost |

The pre-flight test is set membership against `ExhaustiveKeepPolicy::name_to_bucket` for every card of
every combination — free, and it selects the mode rather than merely refusing.

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
- **Re-measuring the two burn floors with the profile attached.** Only the slivers case has been done
  correctly end to end. Until burn is redone, "floor tracks CRN distance" rests on two profile-less
  points and one corrected one, and should not be used to predict a floor.
- Whether the corrected floor is *deck*-sized or *edit*-sized — one deck, one edit is not a trend.
- Larger edits (3+ cards, a genuinely new card), all of whose earlier measurements were profile-less.
- The R=10 same-deck control for the channel-(B) rms-z reading.
- Deck-size changes (add/remove without a matching replace) under inherited numbering — the route
  supports them by construction, but the alignment cost has not been measured.
- The reweight path (zero-rollout retarget of an existing table to a new combination) and the
  multi-source cell library: specified above, not built.

## Guards now in the tree (so the two-layer bug cannot recur)

- `mtg-analyze` **refuses** a keep-gen when `<stem>.profile.json` is not beside the decklist
  (`MTG_KEEP_ALLOW_NO_PROFILE=1` is the deliberate hatch). It refuses rather than warns because the
  route runs for tens of minutes and a warning at minute 0 is not read at minute 90.
- `scripts/deck_compare.py --floor` copies the profile *and* the value sidecar into the arm directory
  before generating, and keys table reuse on the arm's exact card counts (`.counts.json`), so editing
  a combination cannot silently bracket against the previous edit's table.
- A **payment-capability signature** over mana sources is still worth adding as an assertion: two
  sources that differ in what they can pay for must not merge into one bucket. It would have caught
  the profile-less bucketing without anyone needing to know why it was wrong.
