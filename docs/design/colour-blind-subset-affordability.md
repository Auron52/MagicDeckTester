# The enumeration admits subsets the board cannot pay: multi-colour sources collapse to `wild`

Self-contained record (2026-08-18). Found while reviewing Anti-Lifegain's cast order; it is NOT an
ordering bug and NOT specific to that deck.

**STATUS: BUILT and measured, behind `MTG_COLOR_EXACT` (default off). Adoption is the USER's call —
see "What was built" at the end for the numbers and the soundness evidence.**

## The bug, in two lines

`AddSourceToPool` (src/core/SpellEffects.h, ~6181):

```cpp
if (prod.size() == 1)      { pool.Add(prod[0], amt); }   // Forest -> green
else if (!prod.empty())    { pool.wild += amt; }         // ANY multi-colour source -> wild
```

and `ManaPool::CanPayFlat` (src/core/ManaPool.h) treats one `wild` as satisfying **any** single pip.
So the accounting pool knows *how many* flexible sources there are but not *which colours they can
actually make*. A dual that produces {R}{G} counts as able to pay {W}.

That is the "flat wild-pool approximation" `MTG_AFFORD_AUDIT` already names when it classifies a
dropped cast as `colour-short`.

## Worked repro (exact)

```
mtg "decks/Anti-Lifegain/Anti-Lifegain.cod" --profile "decks/Anti-Lifegain/Anti-Lifegain.profile.json" \
    --depth 0 --ignore-play-profile --budget-ms 0 --seed 1698 --game-index 697 --games 1 --threads 1
```

Turn 5 board: Godless Shrine (W/B), Grove of the Burnwillows (R/G), Ignoble Hierarch (B/R/G),
Forest (G).

* Pool as built: `green 1, wild 3`.
* Committed plan (`MTG_ORDER_TRACE=1`): **Fiery Justice `{R}{G}{W}` + Enlightened Tutor `{W}`**
  -> combined `W2 R1 G1`.
* `CanPayFlat`: deficit = (2-0)+(1-0)+(1-1) = 3, `wild` = 3, so **3 <= 3 -> affordable**.
* Reality: only Godless Shrine makes white. Two white pips are needed. **The set is unpayable.**

The subset is admitted, both casts are declared, the first one to be sequenced takes the single white
and the other is **silently dropped** (`CastSpellFromHand` returns void; the drop is by design, see
the note in `CastOrderLess`).

## Why it matters, and why it looked like an ordering problem

Which cast survives is decided by cast ORDER, so the defect masquerades as an ordering regression:

| arm | T5 order | outcome |
|---|---|---|
| base | `Fiery Justice, Enlightened Tutor` | Fiery Justice resolves, opp 11 -> 1, **wins T5** |
| reviewed order | `Enlightened Tutor, Fiery Justice` | tutor resolves, opp stays 12, **wins T6** |

Base is not playing better here — it wins by luck of plan order. Same shape in antilife gi=777
(Idyllic Tutor vs Swords to Plowshares). USER, correctly: *"The ordering just tells us the order to
cast spells if they are in the plan. It doesn't tell us which spells we should cast"* and *"If we
can only afford Enlightened Tutor or Fiery Justice, then search should find the Fiery Justice and
win."*

**Do not "fix" this by giving tutors a cast-order range.** That was the first proposal and it is
treating the symptom: it makes the order dodge an unsound affordability check rather than making the
check sound.

## Measured frequency (base, no levers)

`MTG_AFFORD_AUDIT=1`, Anti-Lifegain:

* **d0, 200 games:** 12 real drops / 832 attempts (1.4%) — Birds x4, Ignoble Hierarch x5, Swords x2,
  Fiery Justice x1. **Every one `colour-short`, zero `total-short`.**
* **d3, 100 games:** 3 real drops / 381 attempts, plus 3,566 / 342,296 inside rollouts.

The search largely absorbs it: across 400 searched-depth games (d3 250 + d5 150) the reviewed order
changed **zero** outcomes, while d0 moved 3. So the damage is concentrated in greedy play and in
rollout leaves — but the rollout count above shows the leaves are scoring lines they could not
realise, which is the fd-diverge failure mode.

One correction to the audit's own wording: it labels `colour-short` drops "order-independent". The
*count* is order-independent — one cast is lost either way. The *victim* is not, and that is what
costs the turn.

## The fix

Add an **exact colour-feasibility test** and apply it as a SOUND post-filter after the existing
optimistic `CanPay`, so it can only ever prune a subset, never admit one:

1. Build the per-source colour sets for the untapped sources (`EffectiveProduces` already returns
   exactly this, and `ProducesForPayment` is the payment-legal variant that handles
   `colored_creature_only` lands).
2. Feasibility = a bipartite matching between sources and the combined cost's COLOURED pips; generic
   pips then absorb any leftover sources. Hall's condition / Kuhn's algorithm is ample at this size
   (<= ~12 sources, <= ~8 coloured pips).
3. Call it where the subset is admitted: `mana_ok` at TurnSolver.cpp ~8197 and its mirror at ~13094.
   Keep `pool.CanPay(combined)` as the cheap pre-filter and only run the matching when it passes AND
   `pool.wild > 0` AND the cost has >= 2 coloured pips — otherwise the flat check is already exact.

Constraints to respect:

* **Soundness direction.** The new test may only reject subsets the old one accepted. Same discipline
  as the SELECTION-EXACT gate documented above `ManaPruneBound` (TurnSolver.cpp ~6768).
* **Lockstep.** Executor and rollout must agree, so it belongs in the shared unit
  (`ManaPayment.{h,cpp}`) beside `AvailableManaPool`, not in one world's copy.
* **Filters.** `SubsetPayableWithFilters` is the existing rescue path for colour conversion; the new
  test must run with the same filter awareness or it will prune legal filter lines. Karoo /
  domain / Reflecting Pool / storage sources all have bespoke crediting in `AddSourceToPool` —
  each needs a colour set that matches what `tap_source` will really produce.
* **Perf.** This sits in the odometer hot path. Gate it as above, and measure with the standard
  worktree-baseline method before adopting.

Expect a broad play change (it prunes subsets that were previously admitted), so: default-off lever
first (suggested `MTG_COLOUR_EXACT`), measure smoke -> regression -> overnight, and a GT rebaseline
on adoption. Adoption is the USER's call.

## What was built (2026-08-18)

`MTG_COLOR_EXACT`, default off. One new unit in the shared `ManaPayment.{h,cpp}` (so the executor and
the rollout read the same model), called from the two subset enumerators in `TurnSolver.cpp`
immediately after `SubsetPayable`.

* `BuildColorFeasibility(state)` -> `ColorFeasibility{cover[32], usable}`. State-only, so it is built
  ONCE per enumeration exactly like `ComputeAvailableColors`; `cover[S]` is the number of mana units
  that can pay a coloured pip of some colour in the 5-bit set `S`.
* `ColorFeasibility::Payable(cands, sel, credit)` -> Hall's condition over the 31 non-empty colour
  sets. Each coloured pip is a demand carrying the mask of colours that may pay it; a hybrid pip is
  un-baked into its two halves the way `SubsetPayable` already does (without that, `{B/G}` demands
  BLACK and a green board is false-rejected). Generic and `{C}` pips are not modelled at all — any
  unit pays them, and the flat `CanPay` that ran first already checked the total, so Hall on the
  coloured pips is the exact remaining condition.
* `PoolCredit(base, eff)` supplies the subset's same-turn credit (ritual float, rock production,
  haste-dork unlock, domain widening) as extra units with the colours the pool recorded.

Soundness is by over-approximation, never by guessing: a Karoo's two fixed colours become two free
choices, a domain source becomes any colour, credit `wild` pays anything. The one shape no colour set
can express is a CONVERSION source — Cascade Bluffs turns one `{U}` into `{R}{R}` — so a board
holding a filter / ramp-filter / scaled land sets `usable = false` and the whole test stands down,
leaving `SubsetPayableWithFilters` as the only authority there (the same three shapes
`HasUntappedFilterSource` already routes to it).

### The bug is gone

`MTG_AFFORD_AUDIT`, Anti-Lifegain d0, 200 games: **14 drops / 876 attempts -> 0 / 863**. Every
colour-short drop is eliminated and no `total-short` drop appears in its place. On the gi=697 repro
the turn-5 subset now excludes the unpayable second white cast instead of enumerating it and letting
cast order pick the victim.

### Soundness measured, not asserted

`MTG_COLOR_EXACT_PROBE` re-tests every rejection against the real payment path (`TapForCostDirect`
on a copy — the executor's own code) and prints any rejection the board could actually have paid.

| run | rejections | false rejects |
|---|---|---|
| full smoke suite, all decks, d0/d3/d5 (probe=1) | — | **0** |
| Anti-Lifegain d0 x200 (probe=2) | 17 | **0** |
| Anti-Lifegain d3 x40 (probe=2) | 2,608 | **0** |
| FiveColour d0 x200 (probe=2) | 16 | **0** |
| FiveColour d3 x40 (probe=2) | 34,733 | **0** |

The probe has two levels precisely because a gate that never fires is indistinguishable from a gate
that fires cleanly (the lesson the inert cast-order ladder taught). Level 2 prints the agreed
rejections too, which is what makes the zeros above evidence rather than silence.

### Measured effect (suite = the A/B, GT = the lever-off control)

| tier | seeds | net summed delta | play-changed | worse |
|---|---|---|---|---|
| smoke | 1001 | **-0.0800** / 36 keys | 10 | 2 (fivecolour d3 +0.0067, d5 +0.0133) |
| regression | 2002 + 3003 | **-0.1353** / 60 keys | 16 | **0** |
| overnight (HELD OUT) | 4004/5005/6006/7007/8008/10010 | **-0.3699** / 144 keys | 47 | 3, none over +0.0015 |

Negative is better, and all three seed sets are disjoint. Almost all of it is `creature_giving` d0
(-0.34 summed on the held-out tier); per deck on overnight: creature_giving -0.3395, fivecolour
-0.0294, antilife -0.0020, knights -0.0005, hinata +0.0015, every other deck exactly 0. Twenty-six of
36 smoke keys are byte-identical, which is the check that the gate only bites boards that actually
hold the phantom.

**Every searched-depth slowdown recovers with budget** — the test the skill prescribes before calling
one real (re-run BOTH arms at `--budget-ms 0`; a large number is not unlimited, `<= 0` is):

| game | at case budget | 4x | 16x | unlimited |
|---|---|---|---|---|
| fivecolour d3/d5 s1001 gi44 | 5 -> 6 | d3 recovers | d5 recovers | **5 = 5** |
| creature_giving d3 s3003 gi221 (draws also diverge at T4) | 4 -> 5 | recovers | recovers | **4 = 4** |
| antilife d5 s7007 gi194 | 7 -> 8 | — | — | **5 = 5** |
| creature_giving d3 s7007 gi162 | 4 -> 5 | — | — | **4 = 4** |

So no line is lost to the gate; the search simply spends a fixed budget differently. Note the antilife
game is T5 on BOTH arms at unlimited budget, i.e. the recorded T7 baseline was itself budget-limited.

Adopted 2026-08-18: default ON, off-switch `MTG_COLOR_EXACT=0`. The flip was verified play-identical
to the env-driven arm before any ground truth was promoted — smoke (36 keys) and regression (60 keys)
reproduced every fingerprint exactly.

### Coverage is PARTIAL, and the shape of the gap is the point

The gate stands down whenever it cannot model a source exactly, so how much of the phantom it removes
depends entirely on what a deck's mana base is made of. `MTG_AFFORD_AUDIT`, 150 games per cell at
seed 7007, colour-short drops OFF -> ON:

| deck | d0 | d3 | why |
|---|---|---|---|
| creature_giving | 36 -> **0** | 15 -> **0** | plain duals/triomes/fetches — the model is exact |
| antilife | 8 -> **0** | 4 -> **0** | same |
| knights | 0 -> 0 | 0 -> 0 | no phantom in the sample |
| fivecolour | 54 -> 46 | 62 -> 49 | Bloom Tender / Faeburrow Elder are `domain_mana`, opened to all five colours under `MTG_DOMAIN_WIDEN` |
| hinata | 17 -> 17 | 26 -> 26 | **Cascade Bluffs (`is_filter`) + Izzet Signet (`ramp_filter`)** — an untapped conversion source switches the whole test off |

So the entire measured gain comes from the decks whose sources the model covers exactly, and hinata —
which has a real colour-short rate — gets **nothing**. That is by construction, not by accident: a
conversion source is precisely the case where an over-approximation would false-reject, so standing
down is the only sound move available to a per-source colour set.

The follow-up this points at: `SubsetPayableWithFilters` already pays for real, and is already the
authority on filter boards. Running it as the exact test when `usable == false` would close the hinata
gap at the cost of a real payment per surviving subset — a perf question, not a soundness one.

### Follow-up round (2026-08-18): both gaps closed, and a RULES BUG fell out

**1. Conversion sources are modelled instead of standing the test down.** A filter's colours are a
hard bound even though its net yield is not (a Cascade Bluffs can never make white), so it is credited
its GROSS yield in its own colours with no charge for the feed -- more supply than reality, so still
permissive. Only a SCALED land (Three Tree City) still stands the test down, because its yield has no
bindable ceiling; no suite deck plays one.

**2. The non-creature pool is gated.** `eff_nc.CanPay(noncreature_combined)` carried the same phantom;
`BuildColorFeasibility(state, /*noncreature=*/true)` mirrors `BuildNonCreaturePool` (creature-only
sources dropped, `colored_creature_only` following `MTG_CCO_NONCREATURE_POOL`) and scores only the
subset's noncreature casts.

**3. Fixed bundles are modelled exactly.** A Karoo ("{T}: Add {W}{U}"), a domain source and a
two-colour ramp filter add one mana of EACH colour -- they are not n free choices from a colour set.
Crediting them as free choices was handing the test a second blue off an Izzet Boilerworks that can
only make one.

That last one is what exposed the real defect. With bundles modelled honestly the probe reported
**8,616 FALSE REJECTS** -- and every one was the probe's ORACLE being wrong, not the gate:

```
[color-exact] FALSE REJECT turn=4: Hunted Phantasm   UNTAPPED{Forest,Azorius Chancery,Stomping Ground,}
```

`Hunted Phantasm` is `{1}{U}{U}`; the only blue source is an Azorius Chancery, which makes exactly one
blue. The payment path was paying it anyway. `TapForCostBacktrackWorker` (SpellEffects.cpp) branched
over each of a source's colours and added `amt` of THAT colour, so a yield-2 two-colour land offered
`{U}{U}` or `{W}{W}` -- neither of which the card can produce. The `domain_mana` branch immediately
above it documents this exact mispricing and was fixed for it; the Karoo case was the unfixed twin,
and `tap_source` on the greedy path had always got it right, so the greedy refused these payments and
the complete fallback allowed them.

Live instance, `mirrorwing_smoke_d3_s1001` gi10 turn 5: board is four Forests and one Gruul Turf
(`{R}{G}`, i.e. ONE red), and the engine cast **Mirrorwing Dragon `{3}{R}{R}`** off it and won on T6.

**The suite metric gets WORSE, and that is the correct outcome.** Summed delta vs the previous GT:
smoke +0.0950, regression +0.1080, overnight +0.1525. All 41 searched-depth slowdowns are on the
three Karoo decks (mirrorwing 31, creature_giving 6, hinata 4) and ZERO on the nine decks without a
Karoo. mirrorwing carries almost all of it (+0.1536 held out) because its namesake card is exactly the
`{R}{R}` the Gruul Turf was illegally funding. Unlike `MTG_CCO_NONCREATURE_POOL` -- withdrawn because
nothing illegal was ever played, so it had to earn its place on measurement -- this IS "we allowed
invalid behaviour and now disallow it", so it ships unconditionally and the old ground truth is simply
an inflated baseline.

Pinned by unit tests: a lone Azorius Chancery cannot pay `{1}{U}{U}`, and the gi10 board cannot pay
`{3}{R}{R}`. `MTG_AFFORD_AUDIT=2` now prints one line per dropped cast (turn, cost, untapped and
tapped sources), which is what made all of this legible.

### Per-game verification of EVERY slower game (MTG_LEGACY_KAROO)

The first pass justified the regression by deck partition -- "all 41 searched slowdowns are on the
three Karoo decks". That argument does not hold up, for two reasons, and both were mine:

* It covered only the SEARCHED-depth subset. Across all three tiers **355** games got slower, and
  **5 of them are fivecolour, which has no Karoo at all**.
* The same commit also made `ColorFeasibility` model Karoo/domain as fixed bundles, which touches
  exactly the same decks. Deck partition therefore cannot separate the payment fix from the gate
  change -- they are confounded.

`MTG_LEGACY_KAROO=1` restores the pre-fix behaviour (in the payment AND in the gate's Karoo model, so
the arm is coherent), and `NoteIllegalBundleTap` records every ACCEPTED payment that actually
contained a bundle source tapped for N mana of one colour. Replaying all 355 slower games on that arm,
one process per game:

| outcome | n | meaning |
|---|---|---|
| legacy reproduces the old win turn **and** the old line contains an illegal tap | **348** | proven: the original line was illegal |
| legacy reproduces it, no illegal tap, but the old line **silently DROPPED a cast** | 6 | the colour-blind phantom, not an illegal cast |
| legacy arm unaffected / old line clean | **1** | NOT explained -- see below |

The 6 are 5 fivecolour plus 1 mirrorwing, and they are the phantom this document is about rather than
the Karoo bug -- e.g. fivecolour gi416 turn 3 declared `Maelstrom Archangel {W}{U}{B}{R}{G}` off four
sources booked as `wild 5` (`Breeding Pool, Deathrite Shaman, Godless Shrine, Faeburrow Elder`) and
dropped it. The tightened domain bundle model is what now refuses those.

**The one genuine counter-example: `hinata_overnight_d0_s4004` gi1197, win T6 -> LOSS.** Its old line
is clean -- zero illegal taps, zero dropped casts. `MTG_COLOR_EXACT=0` (gate off, Karoo payment still
fixed) wins T6, so the cause is the GATE, not the payment fix. And the gate is right: at turn 4 it
rejects `Hinata, Dawn-Crowned + Preordain + Sol Ring`, which needs two blue and a white off a Mystic
Monastery plus an Izzet Boilerworks -- Hall gives demand 3 vs supply 2 over `{W,U}`, and the Monastery
cannot pay both the white and a blue. What follows is the cost: the d0 greedy, having lost that
subset, commits a different plan that ALSO contains Hinata, and then strands it --
`[afford-drop] t4 Hinata COLOUR-short ... TAPPED{Izzet Boilerworks}` -- because an earlier cast in the
turn took the Boilerworks. That is the whole-turn ALLOCATION defect below, surfacing because the gate
changed which plan was chosen. So this game is collateral from correct pruning meeting an unfixed
allocation bug, not a removed illegal play. One game in 247,775.

**Counting the reverse direction** (the rule that a family is never called one-sided without it):
160 games got FASTER. Net +195 slower out of 247,775 compared.

| deck | slower | faster | net |
|---|---|---|---|
| mirrorwing | 268 | 102 | +166 |
| hinata | 71 | 48 | +23 |
| creature_giving | 11 | 4 | +7 |
| fivecolour | 5 | 6 | -1 |

### Chasing the remaining drops: two mechanisms tried, both measured INERT

Both are built, sound, and default OFF. Neither is a fix, and the reason they are not is the useful
part.

**`MTG_COLOR_RESERVE` -- colour-critical reservation.** A source is critical when removing it makes
the plan's combined coloured demand infeasible by the same Hall test the gate uses; hold those, so an
early cast pays around them. It rides the existing reserve-then-fallback retry
(`PlanSourceReserveScope`), so the cast that genuinely needs a critical source still takes it on the
second attempt -- slack-only, never makes a payable cost unpayable. Both apply paths install it
through one shared `PlanReserveSources`, so executor and rollout cannot drift.
Measured: hinata d0 45 -> 44 drops, mirrorwing 4 -> 3, everything else unchanged.

**`MTG_COLOR_SEQ` -- sequenced producer credit.** A producer's output does not exist until its own
cost is paid, and that cost comes from the board. Hinata turn 1 `{Sol Ring, Ponder}` off a lone
Forbidden Orchard is admitted today because the Orchard covers Ponder's `{U}` and Sol Ring's `{C}{C}`
covers the total -- except the Orchard is also the only thing that can pay Sol Ring's `{1}`, and
`{C}{C}` can never pay `{U}`. Unpayable in either order. The bound: paying the producers takes
`prod_cost` units from the board, units outside a colour set S absorb at most `total - cover[S]` of
that, so the remainder must come out of S. Probed at 102,448 rejections, zero false rejects.
Measured: hinata d0 45 -> 43 drops. Nothing elsewhere.

Kept default-off rather than deleted, on the `MTG_CCO_NONCREATURE_POOL` precedent -- a withdrawn
mechanism stays reachable in one binary so its measurement is reproducible.

### Why they are inert: my "allocation failure" diagnosis was wrong for the bulk

The previous section of this document blamed whole-turn ALLOCATION for the remaining drops. That is
true of the one game it was derived from and false of the population. Two corrections:

**Most drops are deliberate optimism, not defects.** `GameLogger.h` says so directly: enumeration is
intentionally optimistic *because the search discards unpayable lines*, so a suite run drops
thousands. dragonstorm d0 drops 1665 of 2497 attempts. The number that matters is STRANDED
ACCELERANTS, and both levers above leave it untouched (hinata d0 18, d3 162; dragonstorm d0 267,
d3 136 -- identical on every arm).

**The stranded accelerants are a ritual-chain bootstrap problem.** `MTG_AFFORD_AUDIT=2` on
dragonstorm:

```
[afford-drop] t3 Seething Song  cost={2}{R}  total-short  pool[R1]
    UNTAPPED{Mountain,Dwarven Hold,}  TAPPED{}
```

**Nothing is tapped.** The chain's first ritual is unaffordable from the opening board -- one Mountain
plus an uncharged Dwarven Hold (a storage land yields its live counters, i.e. zero) -- and the whole
declared chain then drops. No allocation decision is involved; the plan was never startable.

### THE FIX: `MTG_RITUAL_SEQ_CREDIT=2` -- the plan enumerator was the optimistic one

`SequencedRitualCredit` already exists for exactly this, and the two enumerators gate it at DIFFERENT
thresholds: `SeqRitualCreditMode() >= 1` in `Solve`'s consider (TurnSolver.cpp ~8359) versus `>= 2` in
`EnumeratePlans` (~13307), default 1, comment "DEFAULT: honest at the leaf". So the LEAF is honest and
the PLAN ENUMERATOR is optimistic -- which is why every one of these drops is at searched depth and d0
is untouched (d0 goes through `Solve`, which already runs the honest model).

Raising the enumerator to the honest model is one existing lever, no new code:

| | stranded accelerants | total drops |
|---|---|---|
| hinata d3 | **162 -> 26** (-84%) | 262 -> 45 |
| dragonstorm d3 | **136 -> 65** (-52%) | 206 -> 98 |
| hinata d0 / dragonstorm d0 | unchanged | unchanged |
| goblins (no rituals) | unchanged | unchanged |

And it does not cost play -- it improves it, on both seed sets:

| tier | net | keys moved | worse |
|---|---|---|---|
| smoke (1001) | **-0.0266** / 36 keys | 4 | 0 |
| regression (2002+3003) | **-0.0634** / 60 keys | 8 | 1 (+0.0050) |

hinata carries it (d5 -0.040 / -0.020, d3 -0.005); dragonstorm's play changes at the same score. This
is the actual fix for "a plan declared and then not payable" at searched depth, and unlike the two
mechanisms above it needs no new machinery -- only a default change from 1 to 2.

**NOT adopted.** Flipping the default is a play change on hinata and dragonstorm and needs the
held-out overnight plus a GT rebaseline; adoption is the USER's call. Check the storage-land yield
alongside it (the dragonstorm bootstrap case involves an uncharged Dwarven Hold).

### Still not fixed: whole-turn mana ALLOCATION

hinata / treasure_hunt / slivers colour-short drops did **not** move, and the gate rejects zero subsets
there. `MTG_AFFORD_AUDIT=2` shows why:

```
[afford-drop] t3 Hinata, Dawn-Crowned cost={1}{W}{U}{R} COLOUR-short
    UNTAPPED{Forbidden Orchard,Forbidden Orchard,Sol Ring,}  TAPPED{Cascade Bluffs,}
```

Two any-colour sources for three coloured pips -- because an earlier cast that turn spent the Cascade
Bluffs. At ENUMERATION time the Bluffs was untapped and the subset was genuinely payable, so the gate
admitted it correctly. The turn's mana is then allocated per-cast by a greedy that spends flexible
sources early and strands the payoff. That is a different defect from this one and no aggregate
feasibility test can see it: the subset IS feasible, the allocation is not. It wants a whole-turn joint
payment (`BatchPrepayMainCasts` exists but does not cover these), and it is the same family as the
cast-order work in `cast-order-ideal-with-ranges.md`.

### Not done

* **The non-creature pool.** `eff_nc.CanPay(noncreature_combined)` gets the same flat treatment and
  the same phantom is possible there (it needs a `creature_mana_only` source, e.g. Ancient Ziggurat).
  Only the main pool is gated today.
* **Held-out validation.** `--overnight` (seeds 4004/5005/6006/7007) has not been run.
* Adoption, per standing doctrine, is the USER's call — and adopting means a GT rebaseline across all
  three tiers, since the gate changes play on five decks.

## Related

* `MTG_AFFORD_AUDIT` — the instrument that already reports this (`colour-short` column).
* `docs/design/dragonstorm-plan-execution-fidelity-bug.md` — the storage-land analogue: planner
  promised mana the executor could not deliver, silently dropping a legal cast.
* `ManaPruneBound` / the SELECTION-EXACT gate (TurnSolver.cpp ~6737-6800) — prior art for adding a
  sound tightening next to a loose bound.
* `docs/design/cast-order-ideal-with-ranges.md` — the Anti-Lifegain review this surfaced from.
