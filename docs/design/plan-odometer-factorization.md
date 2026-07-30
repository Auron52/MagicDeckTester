# Factoring the mana side out of the main-phase plan odometer

**Status:** BUILT + MEASURED (2026-07-29). Three byte-identical increments shipped default-on; a
fourth (the selection-exact mana gate) is built and, since the 2026-07-30 re-decomposition, is a
**byte-identical pure-performance** option behind `MTG_SEL_MANA_GATE`
because it is not byte-identical — and the reason it cannot be is itself a finding (see
[The legacy bound is unsound](#the-legacy-bound-is-unsound-for-rite-of-flame-chains)).

## The question that started it

> Would it help performance to enumerate the mana that can be produced separately from the spells we
> want to cast, then just do a lookup for each plan? Dragonstorm in particular — there aren't many
> distinct cost options, so we could gate plans that cost too much and avoid enumerating different
> ritual lines for each.

The answer split cleanly in two, and only one half was about mana.

## Measurement (perf -F 999, Release, single-thread, seed 1001)

| deck / depth | games | per-plan mana lookup | all mana code | group predicates |
|---|---|---|---|---|
| treasure_hunt d3 | 150 | 0.44 % | 8.2 % | — |
| Hinata2 d5 | 75 | 1.36 % | 9.1 % | — |
| Dragonstorm d5 | 75 | 0.52 % | 3.0 % | **23.0 %** |

1. **The mana-lookup half was already built and already free.** `BuildPool`,
   `ComputeAvailableColors` and `ManaPruneBound` run once per enumeration call; each plan then pays a
   flat `CanPay` + `SubsetPayable` — ~20 integer ops, 0.4–1.4 %. Nothing to win. The *correctness*
   version of that idea is parked in [exact-mana-enumeration.md](exact-mana-enumeration.md).
2. **The ritual cross-product half was the real cost.** `GroupChoiceOverSplices` (12.2 %) +
   `SpliceCollapseViolated` (10.8 %) = 23 % of Dragonstorm's runtime, spent *rejecting* odometer
   positions one at a time, plus ~4 % of libc `memcmp` from `std::string` card-name compares inside
   them.

## Why the predicates ran on nearly every position

The gate that should have stopped them, `ManaPruneBound`, is a single scalar over the **whole
candidate list**:

- it returns `INT_MAX` — gate fully disabled — if **any candidate** carries `reduces_spell_color`.
  Dragonstorm runs 3× Ruby Medallion, so any turn with one in hand loses the gate entirely;
- otherwise its budget is `pool.Total() + SUM over ALL candidates of (ritual_float + rock_mana)`, so a
  four-ritual go-off hand is budgeted ~12 mana it is not casting.

And both odometers computed the predicates **eagerly** into `const bool`s before testing the gate, so
`&&` never got the chance to short-circuit them.

## What shipped (byte-identical, default-on)

All verified by `test/regression.sh` reproducing ground truth **exactly** in all three modes —
smoke 24/24, regression 40/40, overnight 96/96 (118,000 games), every `<avg>/<play_digest>`
fingerprint unchanged.

1. **`BuildSpliceOdometerIndex`** — sibling of `BuildAccelPrefixOrder`, built once per enumeration.
   Hoists everything the two predicates were re-deriving per position: the splice-base name grouping
   (now a dense int id keyed on the stable `Action::def` pointer instead of `std::string` compares),
   each candidate's copy `pos`, the per-name hand count `N`, and `splice_groups` — the only odometer
   digits the predicates read, so they walk a handful of groups instead of the whole hand.
2. **Cost gate first.** The predicates are pure, so folding them into the `if` after the cost test is
   byte-identical and skips them entirely for positions the gate already rejects.
3. **Predicate result cached across odometer ticks.** All three predicates read `choice[]` only for
   splice-base / accelerant groups, so `MinPredicateDigit` gives the lowest digit that can change
   them; when the mixed-radix carry stops below it the projection is unchanged and the cached verdict
   is reused. Payoff groups below the lowest ritual group stop multiplying the predicate cost.

4. **The two splice predicates merged into one pass** (`SpliceGroupChoiceRejected`, `check_collapse`
   selecting whether the collapse half runs). They read the same projection of `choice` and each did
   its own gather plus a `std::sort`. Since a group is keyed by `hand_index`, every option in it is the
   same hand card — so name and copy position are group *constants*; `splice_groups` is therefore
   pre-sorted by `(name, pos)` at build time and the canonical-form check needs **no sort at all**.
   Only the triangular legality test still orders anything: a descending insertion sort over the run's
   `k` values (at most "copies in hand" ints). Byte-identical — each half tests the same conditions and
   the result is an OR over them, so interleaving them per name cannot change the boolean.

Two smaller things turned out to matter as well. Both index structs are allocated **only when the deck
uses them** (`std::unique_ptr` + a shared read-only empty stand-in), and `BuildManaGateIndex` returns
false — putting the caller back on the free legacy path — when no candidate carries float/ramp or a
per-subset discount, since the two bounds are then the same number. `Solve` is the rollout leaf, called
once per node, and merely default-constructing the empty vectors on every call cost a measurable ~1 %
on decks that use neither. For the same reason the shared empty index and the env flag are
**namespace-scope** statics rather than function-local ones — a magic static would put a thread-safe-init
guard check on the rollout leaf's hot path.

### Result

Instruction counts (callgrind `Ir`) against a worktree build of `HEAD`. Wall clock on this box drifts
~3 % between runs, which is the same order as the non-target deltas, so instruction counts are the
instrument; wall clock is quoted only for the target deck where the effect is far above the noise.

| deck | cfg | base Ir | new Ir | default | + `MTG_SEL_MANA_GATE=1` |
|---|---|---:|---:|---:|---:|
| **Dragonstorm** | d5/25g | 12,360,721,911 | 8,197,027,991 | **−33.68 %** | **−41.92 %** |
| Hinata2 | d5/5g | 16,506,935,109 | 16,522,193,792 | +0.09 % | −0.17 % |
| treasure_hunt | d3/30g | 5,341,721,025 | 5,351,144,684 | +0.18 % | +0.51 % |
| burn | d5/30g | 4,419,502,188 | 4,431,213,452 | +0.26 % | +0.60 % |
| slivers_vial | d5/60g | 3,404,728,173 | 3,410,158,917 | +0.16 % | +0.05 % |
| Knights | d5/40g | 3,131,213,919 | 3,136,936,126 | +0.18 % | +0.26 % |
| Anti-Lifegain | d5/40g | 10,534,998,654 | 10,542,137,092 | +0.07 % | +0.15 % |
| Auras | d5/40g | 5,888,143,143 | 5,896,482,439 | +0.14 % | +0.19 % |

Dragonstorm d5, 400 games, single-thread wall clock, interleaved: **18.05–18.14 s → 14.43–14.58 s
(−19.5 %)**. Wall gains less than instructions because the remaining time is memory-bound. Profile
shares on Dragonstorm: the two splice predicates 23.0 % → 11.7 %, `__memcmp_avx2_movbe` 4.0 % → 1.3 %.

The residual +0.1–0.3 % on the seven non-combo decks was chased and is **not worth chasing further**.
Hoisting the two function-local statics to namespace scope (removing their per-call thread-safe-init
guard checks) recovered only ~0.06 pp; what is left is a null `unique_ptr` pair, one `MinPredicateDigit`
call and a `pred_dirty` branch per position — plus code layout, which is demonstrably part of it: burn
moved +0.20 % → +0.33 % → +0.26 % across changes that cannot touch it (burn has no splice cards). At
~1/500 against −33.7 % on the target deck, the suite makespan is clearly net better.

## The legacy bound is unsound for Rite-of-Flame chains — FIXED DIRECTLY (2026-07-30)

**Superseded framing.** This section previously argued the selection-exact gate "cannot be made
byte-identical, because the legacy bound is wrong". That was true but it drew the wrong conclusion:
the answer is to **fix the legacy bound**, not to ship a tighter bound that quietly compensates for it.
Prompted by the user asking why the gate still cost other decks anything, the work was re-decomposed —
and the result is strictly better on every axis.

### The bug

`ManaPruneBound` credited `SUM over ALL candidates of (ritual_float + rock_mana)` but **not** the
Rite-of-Flame graveyard escalation, which `consider()` *does* credit: the Nth same-turn copy floats
`+(N-1)` beyond its base, a triangular term (`Tri(4) = +6` for four Rite of Flame). Omitting it made
the bound too **tight** — i.e. UNSOUND — so it pruned genuinely payable ritual chains.

The fix is two lines: count the candidates carrying `ritual_float_gy_self_bonus` and add
`ManaGateTriangular(gy)`. Crediting a term can only *loosen* a bound, so it can only add plans back,
never drop one. `MTG_LEGACY_MANA_BOUND=1` restores the old unsound bound (reproduces committed GT
exactly, 24/24 — the isolation gate).

### The consequence: the gate becomes pure performance

With the bound sound, both bounds are sound necessary conditions, so both keep every payable plan and
the tighter one merely reaches the answer faster. Verified, not assumed:

- `MTG_SEL_MANA_GATE=1` vs off is **BYTE-IDENTICAL on all 24 smoke, all 40 regression and all 96
  overnight cases**.

So the two effects that were previously entangled are now separate:

| | what it is | measured |
|---|---|---|
| **Soundness fix** (default on) | quality — recovers dropped payable chains | regression net **−0.1454** (4 better / 0 worse); held-out **−0.2280** (9 better / 2 worse, both d0 at +0.004); Dragonstorm d5 **−0.0667** on smoke. Costs **−0.43 %** instructions, i.e. free |
| **Selection-exact gate** (`MTG_SEL_MANA_GATE=1`) | performance only, byte-identical | Dragonstorm **−13.43 %**, Hinata2 −0.41 %, slivers −0.18 %, and **+0.00..+0.01 %** on the five decks that cannot use it |

The quality win is therefore ~2× what was previously attributed to the gate (held-out −0.2280 vs the
earlier "mean −0.035 turns"), and the gate now carries **no GT churn at all**. Only the soundness fix
needs a rebaseline.

### Known artifact of the soundness fix: Dragonstorm's BLIND d0 greedy sometimes spends its ritual chain a turn early

Found by evaluating every slower game in the rebaseline (2026-07-30). **Searched depths are clean —
`slower=0` in all three modes** (faster = 10 / 30 / 82). The artifact is confined to `d0`, the blind
greedy baseline:

| deck | d0 games | net turns | avg/game |
|---|---|---|---|
| treasure_hunt | 10,000 | −53 | −0.0053 |
| Auras | 10,000 | −25 | −0.0025 |
| **Dragonstorm** | 10,000 | **+6** | **+0.0006** |
| total | 30,000 | −72 | −0.0024 |

Across all `d0`: 176 games faster, 84 slower, 16 newly won, 15 newly unwon. Dragonstorm is the only
deck that nets worse, and it does so while being 59 faster / 37 slower — the loss is concentrated in
11 games that flip from a win to a loss.

**Mechanism** (identical in all three games inspected — `overnight_d0_s7007` gi1578,
`regression_d0_s2002` gi946, `overnight_d0_s5005` gi353; kept hand and draws identical in each):

```
T3  old: land Mountain
T3  new: land Mountain; Rite of Flame; Desperate Ritual; Pyretic Ritual; Rite of Flame
T5  old: land Mountain; Seething Song; ...; Dragonstorm; ATTACK   [opp -520]
T5  new: land Mountain
```

The corrected bound now offers ritual chains the old too-tight bound pruned away. The blind greedy —
no lookahead — takes one a turn *before* the payoff arrives, the float evaporates at end of turn, the
rituals are spent, and the kill never happens. The search sees through this at every searched depth,
which is why d3/d5 only improve.

**Ruled out as causes:**
- *Not* an enumerator/executor divergence — `fd-diverge` is 0 in both bound arms over 300 games, so the
  chosen plan is executed faithfully; the greedy genuinely *prefers* the line.
- *Not* a dead payoff-prune — `SubsetWastesAccelerant` is active and doing heavy lifting in both arms
  (disabling it via `MTG_UNPRUNE=payoffprune` costs 0.85 turns: 5.62 → 6.47).
- *Not* an unsound over-credit — `Tri(N)` is exactly the Rite-of-Flame escalation for N copies cast from
  an empty graveyard, and crediting a term can only loosen a bound; `CanPay` still gates payability.

Isolated to the soundness fix alone: with the gate pinned off, the land changes are inert on Dragonstorm
(5.0000 either way) and only `MTG_LEGACY_MANA_BOUND=1` restores the old line.

**Recoverability — the criterion that clears this (user, 2026-07-30): "an improvement on aggregate is
not enough; the regressions must be easily recoverable."** Checked explicitly, and they are:

- **Every one of the 15 `d0` games that went win → loss recovers at d5** — 11 Dragonstorm + 4
  treasure_hunt, all replayed under both binaries. None is worse at d5 than the old binary, and three
  are strictly *better* (`s7007` gi1339 8→6, gi1578 4→3, `smoke` gi456 4→2). `d0` has no budget knob,
  but depth is the recovery mechanism and it works in 15/15.
- **The wasteful pattern never appears inside the search window.** A detector for "a turn that casts a
  ritual with no payoff and does not win" over a 24-game sample of the 186 changed *searched*
  Dragonstorm games scores **OLD=0, NEW=0** — the search never takes the line the blind greedy takes.
- Searched totals across 24,675 games: **0 slower, 0 newly unwon**, 122 faster, net −0.00604/game.

So the artifact is confined to the blind baseline and is fully recovered by the search the engine
actually plays with.

**Open follow-up:** the payoff-prune drops a subset that casts a ritual with no payoff *in that subset*,
but the losing lines here are reached across a turn's successive re-solves, so no single subset looks
wasteful. Extending the hold rule to "don't start a ritual chain the current hand cannot finish" is a
`d0`-only heuristic worth measuring separately — it cannot affect the searched depths, which are already
clean.

### Making the gate free on decks that cannot use it

Two costs were found and removed, both revealed by the user's question about other-deck regressions:

1. **An unconditional allocation.** The caller did `make_unique<ManaGateIndex>()` *before* asking
   whether the gate applied. `Solve` is the rollout leaf — once per node — so burn/treasure_hunt paid a
   malloc+free per node for a gate that always declined. Split the predicate out
   (`ManaGateWouldHelp`) and gate the allocation on it: +0.65 % → +0.15 % on burn.
2. **A dedicated relevance pass.** That predicate still walked every candidate on every call. Folded
   into the existing group-building loop (which already visits each candidate), set before the
   independent-action `continue` so a Lotus `SacForMana` still counts: +0.15 % → **+0.01 %**.

Exactly 3 of the 8 suite decks contain any ritual / rock / cost-reducer card at all (Dragonstorm,
Hinata2, slivers_vial), so the other five now pay nothing measurable. This is per-*call* self-gating
keyed on the current hand — strictly finer than a per-deck switch, and it cannot drift from the card
data the way a hand-maintained deck list would.

## Two-stage mana-side enumeration (2026-07-29, user-directed build)

The user's original framing — enumerate the mana side separately, then look plans up against it — was
built as `EnumeratePlanPositions`, replacing the flat odometer in BOTH `Solve` and `EnumeratePlans`
with one shared driver (which also removes the duplicated loop those two carried).

**Two mechanisms were measured separately BEFORE building, and they came out very differently.**

**Mechanism 1 — collapsing mana-equivalent ritual lines: DEAD (measured).** Instrumented via
`MTG_ENUM_STATS=1`:

| deck | mana-side combos after existing prunes | distinct outcomes | collapse |
|---|---|---|---|
| Dragonstorm d5 | 40,872 | 40,872 | **1.00×** |
| Hinata2 d5 | 40,694 | 40,662 | **1.00×** |

Dropping cards-spent identity from the signature: still 1.00×. Dropping storm count too: 1.09×. The
existing prefix/canonical prunes already did this job — they take Dragonstorm from 161,547 raw combos
to 40,872 (3.95×) — and what survives is genuinely distinct combinations of *different* ritual cards,
not permutations of equivalent ones. **Do not re-attempt a signature/dedupe collapse here.**

Demand-driven colour bucketing ("don't make a bucket for white in Hinata") measured as exactly zero
effect, for a structural reason worth recording: the mana side of this odometer is ritual float and rock
ramp, which are colourless/wild. Coloured *land* production is not in this enumeration at all — the land
choice is a separate plan dimension (`EnumeratePlansWithLand`). The bucketing idea is sound but aimed at
a different part of the engine.

**Mechanism 2 — per-side gating: real.** A payoff line that no mana line can fund dies on one test
instead of being re-rejected against every mana line. Measured row-skip rates: **38.5 %** of Dragonstorm
payoff lines, **69.0 %** of Hinata's.

**Applicability.** burn, Knights, Anti-Lifegain, Auras, treasure_hunt and slivers have **zero**
mana-side combinations — no rituals, no rocks. The split is a two-deck mechanism; those decks take the
flat path verbatim (and, since no mana side implies every group predicate is inert, that path also drops
the predicate bookkeeping, removing the residual overhead noted above).

**Byte-identity is preserved**, so no ground truth moves. A (mana line, payoff line) pair fully
determines the `choice`/`imask` the flat odometer would have held, so its flat position index is
computable; surviving pairs are sorted by it before being emitted. Order matters here — `Solve`'s
best-plan tie-break and `EnumeratePlans`' returned candidate order are both order-sensitive.

### Two-stage result (final, byte-identical)

Callgrind `Ir` vs a worktree build of `HEAD`. Byte-identical in all three modes: smoke 24/24,
regression 40/40, overnight 96/96 (118,000 games).

| deck | cfg | two-stage | + `MTG_SEL_MANA_GATE=1` |
|---|---|---:|---:|
| **Dragonstorm** | d5/25g | **−38.43 %** | **−46.93 %** |
| Hinata2 | d5/5g | +0.36 % | +0.05 % |
| treasure_hunt | d3/30g | +0.30 % | +0.64 % |
| burn | d5/30g | +0.32 % | +0.65 % |
| slivers_vial | d5/60g | +0.16 % | +0.04 % |
| Knights | d5/40g | +0.15 % | +0.23 % |
| Anti-Lifegain | d5/40g | +0.08 % | +0.16 % |

So the split adds ~4.7 pp on Dragonstorm over the constant-factor work alone (−33.7 % → −38.4 %) while
leaving the other decks where they were.

**Two perf traps hit while building this — both cost more than the split gained, and both are easy to
reintroduce:**

1. **Do not drop the group-level early-out before the `imask` loop.** Testing payability per
   (position, imask) pair instead of skipping the whole inner loop for a doomed group selection cost
   **+2.4 pp on burn and Knights** — doomed positions built `sel` for all 2^num_ind extensions.
2. **Do not route the no-mana-side walk through the driver.** Decks with no ritual or rock keep an
   inline flat walk in each caller. Measured, going through the driver's function boundary was *not*
   the cost (that was trap 1), but the inline form is what the numbers above were taken with, and it
   also lets that path drop the predicate bookkeeping entirely (no mana side ⇒ every group predicate is
   inert).

## Left on the table

- **Physically reordering the odometer digits** so predicate-relevant groups occupy the high positions
  would make the increment-3 caching maximal (currently the splice groups sit wherever the hand put
  them). NOT byte-identical: group order feeds `sel` order → `plan.actions` order → the returned
  candidate order, so it needs its own A/B and rebaseline.
- **Deduping accelerant selections by mana signature** (net mana, storm count, cards consumed) — the
  "not many distinct cost options" collapse. A candidate-count reduction rather than a constant-factor
  one, and explicitly out of scope here because it changes which plans exist.
