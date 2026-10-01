# Snow's phase-A degeneracy: it is BREAKPOINTS, and three plausible causes it is NOT

Self-contained. Written 2026-09-21 after the user cancelled Snow's phase A at ~24 h
(12,731 rows banked, `A_rows` deliberately unmarked, queue resumable).

The user's framing that drove this, and which turned out to be the right one:
*"If we are doing something the other decks are not, we should fix it"*, and
*"we plan out the full rest of the turn unnecessarily with breakpoints... evaluating mana on a
turn segment basis doesn't make sense. We need to evaluate it preferably on a turn basis."*

## What was happening

Phase A ran 32/32 workers for 24 h. 18 of 32 in-flight games were over 10 h; nine sat at 23 h;
five of six sampled zero-row games emitted **nothing**. It was not starved (`3171%` CPU) and not
a scheduling fault.

**The proximate cause is a unit-scaling accident.** `SearchBudget::NODES_PER_VIRTUAL_MS = 900`
(`src/ai/SearchBudget.h`) and the labeller's budget is
`EnvInt("MTG_VALUE_LABEL_BUDGET_MS", 1000000)` (`TurnSolver.cpp`, ~43850), so the default is
**900,000,000 units per POSITION**. Snow converts units to real time at 11-28 units/ms, so one
position is permitted **9 to 23 hours**. `scripts/valueleaf.sh` (~753) never sets the knob, so
phase A runs at that default.

The fingerprint is unmistakable -- six of the worst games stop within 0.2% of exactly 900,000,000:

| gi | units | wall | u/ms |
|---|---|---|---|
| 33 | 900,108,836 | 22.38 h | 11.2 |
| 13 | 900,599,879 | 19.49 h | 12.8 |
| 195 | 901,963,405 | 18.86 h | 13.3 |
| 176 | 901,415,552 | 17.03 h | 14.7 |
| 224 | 900,423,590 | 14.87 h | 16.8 |
| 84 | 900,588,436 | 14.69 h | 17.0 |

They did identical WORK; the 8-hour spread between them is purely throughput. And a position that
trips the ceiling bumps the truncation watermark, so `EmitEvalRows` drops it -- *"their true win
turn is unknown, so no row was written"* (`AIEngine.cpp` ~453). Rows are emitted per position in
turn order, so one hung position takes the rest of that game's rows with it. **23 hours, zero rows.**
The same comment names the precedent: *"slivers/treasure_hunt/Knights produced zero rows in 34
hours: not slow decks, one hung position per game."*

**Direct proof the ceiling is what held the game:** seed 901283 (the 22.38 h game) completes in
**80 seconds** at `MTG_VALUE_LABEL_BUDGET_MS=3000` -- 2.8M units instead of 900M, same win turn
(7), 6 of 7 positions kept.

## Where the cost actually is

One game, seed 901283, budget-bounded (`MTG_BP_PROBE=1`):

| breakpoint site | consultations | share | searched | overrun | untarget |
|---|---|---|---|---|---|
| `snow_look_top` (Scrying Sheets / Frost Augur) | 1,605,062 | 54.4% | 75.7% | 94,076 | 295,605 |
| `put_in_hand` (any cast whose resolution drew) | 908,930 | 30.8% | **19.3%** | 149,633 | 583,681 |
| `deferred_cantrip` | 417,963 | 14.2% | 100% | 0 | 0 |
| `post_entry_act` | 16,333 | 0.6% | 81.5% | 1,631 | 1,390 |
| **total** | **2,948,288** | | 61.8% | **245,340 (8.3%)** | **880,676 (29.9%)** |

Against `interior_nodes = 2,753,762` -- **the breakpoints ARE the search**. Unit sites agree:
`fs_bp_wave` 58.9%, `fs_pre` 39.2%, `rollout_step` **0.48%**. Almost no unit reaches a rollout.

**What Snow does that no other deck does:** it runs 16 permanents that put cards into hand, 8 of
them REPEATABLE -- 4 Scrying Sheets and 4 Frost Augur (`{1}{S}`/`{T}`: look at top, if snow put it
in hand), 4 Arcum's Astrolabe (ETB draw), Ice-Fang Coatl. Every card arriving in hand mid-turn
opens a re-plan, and they chain. `snow_look_top` is bp site 8; the code notes *"Snow reaches site 8
on every consultation."* No other deck in the suite has one.

## Why condemnation cannot help

Condemnation's premise, stated at the mana-site exemption: *"the pre-breakpoint section CONSIDERED
a card and declined it."* **A card Scrying Sheets just put in hand was never considered and never
declined -- it was not there.** So for 85% of Snow's breakpoint work the mechanism is structurally
inapplicable. Measured: `bp_condemn_seen=1,249,500 drops=5,447` = **0.44%**, all of them `searched`
(no waste, just no reach). Two explicit exemptions compound it:

* `MTG_BP_CONDEMN_ACTIVATION` = **0, off entirely** -- no activated ability is ever condemned, and
  Snow's cost is entirely activations.
* `MTG_BP_CONDEMN_MANA_SITE_EXEMPT` = **on** -- if the card that opened the breakpoint ADDED mana,
  condemn nothing there. (Corrected 2026-09-21: an earlier version of this line said the exemption
  fires because "Scrying Sheets is a mana land / Astrolabe is a mana rock". It does not --
  `BpSiteAddedMana` keys on a ritual float, an untap-X effect or a Treasure mint only, and the
  whynot census reads `manasite=0` on Snow. The exemption is inert here; the two live blockers are
  the ones the census names below: `noplancast` and `managrew`.)

There is no condemnation tuning that fixes this.

## Three causes this is NOT (each proposed, each killed by its own control)

1. **Global plan-cache ratchet.** Both memo pools sat pinned (`fsl=1605M/1605M`,
   `plan=3199M/3200M`) and the code warns the symptom of a pool leak is *"pool full forever =>
   recompute everywhere => a uniform silent slowdown with BYTE-IDENTICAL play"*. But throughput
   over the run dips and RECOVERS (median u/ms by sixth: 24.0, 26.0, 27.8, 19.9, 18.8, 23.7) --
   not a ratchet -- and the bp cache self-clears on full. Not a leak; at most thrash.
2. **Mana-side enumeration / Arcum's Astrolabe.** Snow is genuinely the ONLY deck that reaches the
   mana side (116,049 enumeration calls vs **0** for Melira Pod and slivers_vial, 108 for
   FiveColour), and its affordability filters prune nothing (`SubsetPayable` and `ColorFeasibility`
   each drop **0** of 72,461). Looked decisive. **`MTG_NO_ROCK_RAMP=1` drives the mana side to
   exactly 0 and the cost does not move** -- units 1.002x / 1.018x / 1.003x on three games,
   identical win turns. Slightly WORSE without it. The unique thing is real and is not the cause.
3. **Prefix-scoped mana prepay.** `MTG_BP_PREFIX_PREPAY` was already BUILT, MEASURED and
   **REJECTED on quality** (+0.0200 hold, t 6.20; +0.0214 train, t 6.41; paired 5000/cell) -- see
   `bp-node-partition.md`. Its lesson is the important part and it CONFIRMS the user's principle:
   *"the 'wasted' float is NOT waste. The mana a base plan taps for its tail is chosen jointly
   across the whole turn, and that joint allocation is better for the node's CONTINUATIONS than a
   prefix-only payment is."* Scoping mana to a segment loses. (It is also inert unless
   `MTG_BP_NODE` arms `bp_capture` -- an A/B on Snow came back byte-identical, 1.000x.)

A methodological note worth keeping: the control table in (2) was initially presented with Snow's
number **censored** by the very ceiling under investigation while the controls were uncensored.
Capped and uncapped numbers must not share a column.

## The open design (the user's, and the doc that asks for it)

`bp-node-partition.md` root-caused the cross-dupe bucket and found it is **not** a transposition
problem: *"the cross bucket is the base-plan enumerator enumerating PAST the breakpoint and the
node then covering the same ground again. That is exactly what the USER's direction at the top of
this doc forbids (**'Enumerating ahead without the drawn or staged cards doesn't make sense either
way'**): the doctrine was implemented for the node but **never enforced on the enumerator feeding
it**."* Sized: cross 1,192,267 of 1,541,982 dupe children, ~10% of node units.

So the user's 2026-09-21 framing is a restatement of their own earlier directive, still only
half-implemented. The named, unbuilt fix is **truncate-at-emission** (truncate AFTER
`eval_and_push`'s filters; a naive pre-filter drop is lossy and already confirmed so).

And the payment model the rejection explicitly asks for is the user's other proposal --
*"only figure out which state we want later when we need different mana"*:

> *"Do not re-propose this without a payment model that KEEPS THE JOINT ALLOCATION while dropping
> only the truly-dead tail."*

Deferring commitment is the one move that gets both: no float in the pend state (so the existing
prefix dedup collapses the duplicates) **and** a whole-turn joint allocation when it is finally
resolved (so nothing strands). Clairvoyance makes deferral safe -- the engine already runs a
*"clairvoyant search over a known, deterministically-shuffled library"*, and there is a precedent
for the shape: Call of the Wild's `activated_reveal_top_cost`, *"repeatable; the search chooses how
many activations (clairvoyant top)"* -- one segment, N as a decision variable, no breakpoint.
Scrying Sheets and Frost Augur instead ride `tap_draw_cost` + `tap_draw_requires_top_supertype`,
which puts a card in hand and therefore opens a breakpoint.

## Two facts that do not depend on any of the above

* Snow does not need a value leaf at all. The 2026-09-20 shape probe ran 6,000 games and died
  before printing its verdict; recovered from the surviving `wins/`+`units/` artifacts:
  heur 375,921,463 units / 5.8725 avg; `esc_nl` 1.443x / z=+1.80 (tie); **`fit_nl` 1.160x /
  z=+2.35 (BETTER play)**. Leafless is fine on Snow; it is not cheaper, but choosing it means
  phase A never runs. StompySurprise and Goblins already ship `leaf: none` with a model-less
  `.value.json` carrying only the shape and `mull_gen_depth`.
* The `g_bp_enum_depth` counter does two jobs. Its stated purpose in
  `BpDeriveContinuationList` is *"suppress the fan-out"*, but the enum memo requires
  `g_bp_enum_depth == 0`, so every breakpoint continuation is also disqualified from the
  enumeration memo. Measured hit rates on Snow: 0.07% (`hits=8 misses=10,839`), 5%, 7.5%.
  Separating the two concerns is a small change worth measuring on its own.

---

## 2026-09-30: four concrete degenerate shapes, measured — and none of them is a wrong ANSWER

Asked directly: *"Can I see some examples of degenerate breakpoints for snow? I'd like to see if we are
doing anything wrong."* Reached from the `MTG_ACT_LINE_HOLD` cost work
(`trailing-activation-payment-hole.md`), which established that snow's +21–30% from that lever is this
degeneracy being fed ~18% more work rather than any overhead of the lever itself.

**Headline, stated before the details so it is not oversold: I found nothing that computes a wrong
answer.** Every shape below is the engine being *right at avoidable cost* — a budget mismatched to its
workload, or an arming predicate looser than the deck deserves. Reproduce with:

    mkdir -p logs/snowbp && python3 - <<'PY'
    import json
    json.dump({"jobs":[{"name":"snow_cands","deck":"decks/Snow/Snow.cod",
                        "profile":"decks/Snow/Snow.profile.json","games":20,"seed":400000,
                        "depth":3,"budget_ms":10,"ignore_play_profile":True}]},
              open("logs/snowbp/cands.json","w"), indent=1)
    PY
    MTG_BP_CANDS_PROBE=1 MTG_BP_PROBE=1 MTG_BP_EMPTY_CENSUS=1 \
        ./build/Release/mtg --batch logs/snowbp/cands.json --threads 32

20 games, snow d3/b10. `[bp-cands]` reports per site: `n` = opens with a non-empty list, `total` =
Σ list length, `reach` = Σ min(len, W), `unreachable` = **Σ max(len − W, 0), i.e. rank-gated OUT by the
variant budget — NOT "unplayable"**, `capped` = opens where len > W, `EMPTY` = opens whose list came
back with nothing.

Denominators differ per column, so they are named explicitly — mixing them is how the earlier
site-8/9 smear went unnoticed. `opens` = non-empty lists + empty ones; `capped` is a share of the
**non-empty** lists; `rank-gated OUT` is a share of **all continuations enumerated**; `EMPTY` is a share
of **all opens**.

| site | opens (non-empty + empty) | mean len | max | capped (of non-empty) | rank-gated OUT (of continuations) | EMPTY (of opens) |
|---|---|---|---|---|---|---|
| 8 `snow_look_top` (Sheets/Augur) | 99,195 = 84,599 + 14,596 | 4.06 | **90** | 43,756 / 84,599 = **51.7%** | **194,111 / 343,067 = 56.6%** | 14.7% |
| 10 `put_in_hand` (any cast) | 90,383 = 19,277 + 71,106 | 2.71 | 113 | 32.9% | 22,785 / 52,312 = 43.6% | **78.7%** |
| 3 `deferred_cantrip` | 683 = 190 + 493 | 2.60 | 19 | 37.9% | 41.1% | 72.2% |
| 9 `post_entry_act` | 1,016 = 911 + 105 | 4.50 | 26 | 56.5% | 59.6% | 10.3% |

The `searched` shares quoted below (site 8 **33.3%**, site 10 **7.8%**, `deferred_cantrip` **96.1%**,
`post_entry_act` **78.8%**) come from a SEPARATE `MTG_BP_PROBE` run — 100 games, same d3/b10, so they are
shares of total consultations there, not of the 20-game opens above. Do not divide one table by the other.

### A. The variant budget is a CONSTANT W=2 fighting a list whose length runs 1 → 90

**W is 2, and the probe proves it rather than asserting it.** `reach` = Σ min(len, W), so with the
histogram below W=2 predicts Σ min(len,2) = 2·(84,599−20,242) + 20,242 = **148,956** — exactly the
`reach` printed — and `capped` (len > W) = 84,599 − 20,242 − 20,601 = **43,756**, also exact. Any other W
misses both.

Site 8's length histogram: `1=20,242  2=20,601  3=8,626  4=13,724  5-8=13,353  9-16=6,531  17-32=1,395
33-64=122  65+=5`. Wave 0 emits a fixed `depth*W` variants regardless. Both failure modes are therefore
live *at the same time*, which is the tell:

* **W too small on 51.7% of opens** → **56.6% of every continuation enumerated at snow's hottest site is
  built and then discarded unexplored** (194,111 of 343,067). That is a ~2.3x enumeration amplification.
* **W too large on 8.6% of opens** (`overrun`, from `MTG_BP_PROBE`) → the surplus ranks re-score the
  EMPTY line; the first such rank is a real line ("I am done acting"), and only the **second and later**
  duplicate it (`ovr_dup` = 0.87% of site-8 opens).

Not a defect — the explored set is correct either way — but a fixed budget is the wrong shape for this
distribution, and site 8 is where snow spends its turn.

**How this relates to what this doc already establishes, because most of it is NOT new.** The §"Where
the cost actually is" finding that 85% of consultations are `snow_look_top` + `put_in_hand` is
reproduced exactly here. The session-4 work already identified the `rank` arm's 82.3% duplicate rate
"at W=2" and named the fix — **truncate-at-emission on the enumerator** — recording it as
**design-blocked on the deferred joint-allocation payment model**, which is the user's own "evaluate
mana per TURN, not per segment". So the enumerate-then-discard shape is a known, already-diagnosed
consequence, and the genuinely new part here is only its SIZE at site 8 (56.6% of continuations, 51.7%
of lists capped) and the fact that the *same* constant simultaneously overshoots on 8.6% of opens.
Likewise §"Why condemnation cannot help" already rules out the obvious filter: a card just drawn was
never considered-and-declined, so condemnation's premise does not apply. And the two lossless wave-0
skips (`MTG_BP_W0_NOBP`, `MTG_BP_W0_UNIF_COLLAPSE`) were built, measured and adopted default ON at
`57d57cfb` — so the cheap structural duplicates in wave 0 are already gone; what remains is the
enumeration itself.

### B. Site 10 opens 90,383 times per 20 games and 78.7% of those offer NOTHING

`put_in_hand` ("ANY cast whose resolution put a card in hand", the class adopted at `6b9f0cb2`,
2026-09-27) is opened 90,383 times and **71,106 of those produce an empty continuation list**. Its
searched share is **7.8%**, against `deferred_cantrip`'s 96.1% and site 8's 33.3%. The probe localises
the empties to two call sites, both plan applies rather than anything site-10-specific:

    [bp-cands]   EMPTY in-rollout total=83,125 by apply@line: 53394=61,885  53870=21,210  53882=30
    [bp-cands]   EMPTY in-search   total=3,175  by apply@line: 48316=2,425  48421=493 ...

`TurnSolver.cpp:53394` is wave 0's **base-plan** apply; `:53870` is the **variant** apply inside the wave
loop. So on snow this class is mostly arming, enumerating, and finding nothing — the shape the
*arming-rule* work was about. Worth re-asking whether its predicate should be tightened for a deck whose
drawn card usually cannot be cast the turn it arrives. (The occurrence still has to be COUNTED even when
narrowed — skipping a count shifts every later `bp_at` index, the PodBreakpointClassOn lesson — so this
is a narrowing of the enumeration, never of the count.)

### C. The SECOND look in a turn is never searched — and that one is deliberate

Committed-line trace (`MTG_BP_TRACE`, one game, single-threaded) shows the shape plainly:

    [bp-exec]  turn=7 idx=0 bp_at=0 bp_choice=1 searched=1
    [bp-exec]  turn=7 idx=1 bp_at=0 bp_choice=1 searched=0

Two look activations in one turn; the plan's `bp_at` addresses occurrence 0, so occurrence **1** falls to
a greedy continuation. Snow runs 4 Scrying Sheets + 4 Frost Augur, so this is routine — it is the
`nested-unsearchable` bucket, 169,549 consultations at site 8 (rising to 228,599 with
`MTG_ACT_LINE_HOLD` on).

**This is a documented, deliberate trade, not a bug.** The CHAIN CAP comment in `ApplyPlanDirect`'s
site-8 arm gives the reason: *"4 Sheets + 4 Augur = up to 8 nested Solves per apply -- a measured 9x
playout multiplier"*, so depth is capped at 1 and a nested find waits for the playout's next simulated
turn. Recorded here because it is the one place where snow's cost and snow's *quality* point the same
way: the cap is why the second look is scored greedily, and `untarget` (58.1% of site-8 opens) is the
same gap seen from the base plan's side — the field's own comment says those *"need a searched
REPLACEMENT, not deletion."*

### D. Site 8's playability gate is sound but MANA-VALUE ONLY, and snow is the deck that punishes that

The gate exists and it earns its keep — without it *"the greedy Solve fired 521k times in ONE d3 game (a
43s game)"*. But for a nonland find it tests quantity, never colour:

```cpp
ManaPool have = AvailableManaPool(state);
have.AddPool(state.floating_mana);
snow_look_worth = static_cast<int>(have.Total()) >= fd->card.m_mana_cost.ManaValue();
```

Snow is three colours over colourless-heavy snow sources plus filters — the deck whose greedy payment
already strands 15.4% of the time. So a found Rimescale Dragon `{4}{R}{R}`, Ice-Fang Coatl `{G}{U}` or
Rimefeather Owl `{6}{U}` passes this gate on total mana alone, with no untapped producer of the colour it
needs, and buys a full re-solve that cannot cast it.

A colour-aware companion test stays a **necessary** condition, hence lossless: for each coloured pip the
cost requires, demand at least one untapped source whose `EffectiveProducesFor` can make it (hybrids
satisfied by either colour; `{S}` by any snow source). Note this is NEW code, not a reuse —
`PaymentManaCovers` looks like the helper for it and is **not**: it bounds untapped mana *quantity*
(`UntappedManaUpperBound`), so it is the same axis the gate already tests.

**Ceiling, so it is not oversold:** site 8's own `EMPTY` rate — 14.7% of opens already re-solve and find
nothing to offer — bounds what a tighter gate can remove. That is the honest size: single-digit-to-15%
of site-8 opens, not a multiple.

### E. Condemnation is NOT inert — it is HAND-CAST-ONLY, and snow's declined options are ACTIVATIONS

USER correction, and it is right: *"Condemnation is not inert because new cards can be used in
conjunction with old ones in the plan"* — a continuation is `{new card} ∪ {old cards}` and
`MTG_BP_NEW_ONLY` only requires AT LEAST ONE new card, so the old cards riding along WERE
offered-and-declined before the site. The "a card just drawn was never there to decline" line explains
only the new card, not its companions. Measured (`MTG_BP_CONDEMN_WHYNOT=1 MTG_ROLLOUT_STATS=1`, snow
d3/b10, 20 games — snow opts in via `MTG_SNOW_CONDEMN`, default true):

    bp_condemn_seen=542551  drops=7525                                    -> 1.39% drop rate
    bp_whynot notdecision=6624 notturn=908 noplancast=186756 managrew=107647
              plancasts=20254 notail=0 manasite=0 PEER=83914 reached=136448
              notdominated=22579 unpayable=80561 newoption=25783
    bp_whynot CEILING: today=7525 peer-blocked=83914 yield=0.0551
              => realistic max ~12152 (1.61x today), hard upper bound 91439 (12.15x)

**Three claims made earlier in this session are refuted by this histogram, and are retracted here:**

1. *"Condemnation is near-inert at site 8, 0.44% drop rate."* It drops **7,525** at **1.39%**. Not inert.
2. *"`MTG_BP_CONDEMN_MANA_SITE_EXEMPT` (default ON) exempts Scrying Sheets as a mana land."*
   **`manasite=0`** — that gate never fires once.
3. *"`PEER` should be 0 after the 2026-09-15 activated-site fix."* **`PEER=83,914`**, the third-largest
   blocker.

The user's other instinct — *"it probably does less than it used to"* — is also supported: this file's
own §"Why condemnation cannot help" era recorded `drop_rate=0.0534` (5.34%) on 2026-08-28, against
**1.39%** here. Different configuration, so suggestive rather than a clean before/after.

**Where the consultations actually go, and it is upstream of the conjunction case:**

* **`noplancast` = 186,756 (43% of non-drops) — "the plan cast NOTHING → it declined nothing."** The
  biggest blocker by far. On snow's site-8 turns the arriving plan frequently contains **no casts at
  all**: a land drop plus tap-draw *activations*. §A's dump is exactly this — alphabet of ONE,
  `Frost Augur|k26`, an activation. The conjunction case needs the plan to have cast something; 43% of
  the time here it has not.
* **`managrew` = 107,647 (25%)** — the USER's own "uncondemn on land drop or rock played" rule
  (`ManaSourceCount(state) <= g_bp_mana_sources_before`). On a deck whose dug card is usually a snow
  land that the continuation then plays, source count grows and every old card is re-admitted. Correct
  by specification, and it costs a quarter of all consultations.
* **`PEER` = 83,914 (19%)** — the cast order's ceiling.
* Of the 136,448 that pass every gate, **`unpayable` = 80,561 (59%)** is the largest rejection. That is
  "quick pruning of unpayable lines" working, but at VERDICT time — after the candidate was enumerated,
  emitted and applied.

**The missing half is already specified by the user and is DEFAULT OFF.**
`BpCondemnActivationMode()` = `EnvInt("MTG_BP_CONDEMN_ACTIVATION", 0)`, whose own comment is the
2026-09-18 ruling *"Activation should be able to be condemned"* and states the defect exactly:

> *"Condemnation has always been a HAND-CAST rule — its candidate loop is indexed over `ap.hand[i]` —
> so an activation could never be condemned whatever its rank. That is why Scrying Sheets shows ZERO
> drops in every arm while Frost Augur shows drops only as a CAST… 67.6% of wave slots are stillborn
> and each one can do this."*

Its soundness argument needs no new snapshot (so it cannot break search/executor lockstep the way a new
`CantripOrderScope` field would): the trailing pass is ordered by `ActivationOrderRank` and applies only
the plan's own actions, so an activation still available when a rank-R site fires **was offered at its
slot and passed over** — condemnation's premise, directly. **Mode 1** is the user's "same ability,
identical card" restriction, a NAME test — which is precisely the two interchangeable Frost Augurs of §A.

### Summary: three levers are OFF, and all three point at the SAME dump

| lever | state | what it would have collapsed in §A |
|---|---|---|
| `MTG_BP_CONDEMN_ACTIVATION` | **OFF** (`EnvInt(...,0)`) | the declined second Frost Augur — snow's decision space is activations, and condemnation only sees casts |
| `MTG_CAND_DEDUP` | **OFF** (`EnvOn("MTG_CAND_DEDUP")`, no default) | the `bp=0,bp=1,bp=0,bp=1` rank/uniform duplicate pairs. Measured units −5.16%, CPU −5.2%, **play byte-identical**; unadopted only because it moves 17 GT keys |
| `MTG_FOLD_SEARCH_ODO` | **OFF** (`EnvOn("MTG_FOLD_SEARCH_ODO")`) | the `s17` vs `s20` split. `MTG_FOLD_ACT_SOURCES` is default ON on the user's own interchangeability ruling but only **1.4%** of the search's selections carry the odometer flag (100% on the greedy side), so the fold "has never once been applied where those widths are counted" |

Condemnation itself is ON for snow and working at 1.39%, with the engine's own ceiling read putting its
headroom at **1.61x** — because it can only see casts. The activation half is where snow's headroom is.
`MTG_FOLD_SEARCH_ODO` must be measured with `MTG_FOLD_VERIFY=1`: the fold deletes a line outright when
its canonical twin is not enumerable (knights gi497 lost a turn-4 kill that way).

### F. `MTG_CAND_DEDUP` SETTLED 2026-09-30: it fires, play is byte-identical, and it is NOT faster

USER BAR: *"if the option works with neutral or net positive quality, is faster and doesn't introduce
unrecoverable lines it should be on by default."* Its speed leg had been recorded `NOT RESOLVED` since
2026-09-09 because a shared box gave a 16.2% within-arm spread against a 0.8% effect. Re-measured on an
idle box with the instrument the lever's own comment prescribes — **single-threaded, fixed seed, arms
interleaved, CPU (user+sys)**, which counts `BuildDedupKey`'s hashing that `units_total` structurally
cannot see:

    snow d3/b10, 25 games, seed 400000, threads=1, 5 reps interleaved ctrl/dedup
      ctrl   29.28  29.22  29.38  29.59   (user+sys, s)
      dedup  29.23  29.28  29.41
    within-arm spread 1.3% (vs 16.2% in 2026-09-09) -- the instrument works; the arms OVERLAP
    every run: avg=5.8400 digest=9cd64103a4113d51  -- byte-identical play, both arms, all reps

    same manifest, 5 games, units:  units_total 474162 -> 462691  (-2.4%),  ms 5281 -> 5385 (+2.0%)

**Verdict: it fires, it is play-neutral here, it saves 2.4% of SEARCH units, and it spends that saving
back on hashing.** CPU is indistinguishable from zero at 25 games and mildly negative at 5. So it
**fails the "is faster" leg** — not by regressing, but by being neutral. The 2026-09-09 quality evidence
still stands (regression `slower=0 faster=5`, smoke `slower=0 faster=7`), so this is a quality-positive,
cost-neutral change that also costs a 17-key GT rebaseline. That is a judgement call, not an automatic
adoption, and it is the USER's.

**What it does and does not cover — the user's "I'm sure we do some deduplication" is correct.** The two
state-keyed skips sit adjacent in the wave loop:

```cpp
if (CandDedupActive() && !reframe_seen.insert(BuildDedupKey(copy)).second)   // DEFAULT OFF, all candidates
{ ++candidates_done; continue; }
if (bp_variants_here && !bp_seen_states.insert(BuildDedupKey(copy)).second
    && plan.bp_choice >= 0)                                                 // ALWAYS ON, variants only
{ ++candidates_done; continue; }
```

`bp_seen_states` is unflagged and always on. Census (`MTG_DEDUP_CENSUS`, snow d3/b10, 5 games):

    dedup_census seen=170979 dup=104992 dup_rate=0.614  copy_perm=50621  copy_FALSE=32214
    dedup_why    dup_bp=80245  dup_nobp=24747
    dedup_why_arm base=80116/dup=24747(0.309) rank=50640/dup=41213(0.814)
                  unif=14906/dup=13884(0.931) chain=25317/dup=25148(0.993)

So of 104,992 duplicates, **80,245 are breakpoint variants already caught today**; `MTG_CAND_DEDUP` adds
only the **24,747 base-plan** ones. The `unif` 93.1% and `chain` 99.3% rows are what the already-adopted
`MTG_BP_W0_UNIF_COLLAPSE` / `MTG_BP_W0_NOBP` skips target.

### G. THE BIGGEST UNCLAIMED ITEM: ~5,600 stranded tap-draw activations PER GAME

From the same census, and it is not a dedup problem at all:

    dedup_why_act strand_dup=17024  strand_but_UNIQUE=8164
                  stranded_activations=28893  of_which_tapdraw=28009   (97%)

**28,009 tap-draw activations in FIVE games — ~5,600 per game — are enumerated, put in a plan, pre-tapped
by `ApplyPlanDirect`, found unpayable by `TapForCostDirect`, and silently untapped.** 97% of all
stranded activations are tap-draws, i.e. Scrying Sheets and Frost Augur. And **8,164 of them land on a
UNIQUE post-apply state**, so no state-keyed dedup can ever reach them — they are distinct lines that
simply cannot be paid.

This is the user's third mechanism ("quick pruning of unpayable lines") and it currently fires at
**payment** time rather than **enumeration** time: the candidate is built, copied, applied, and only then
abandoned. An affordability pre-check at emission — does an untapped source remain that can pay this
activation's cost after the plan's casts — would remove the apply entirely. It is the same shape as
§D's colour-aware gate and the same shape as the `PAY BOUND` fail-fast, just moved one stage earlier,
and it is a NECESSARY-condition test so it cannot delete a payable line.

Ranked against §E–§F this is now the largest item with no lever built for it yet.

### F-bis. …and on HELD-OUT seeds the quality gain does not replicate — so it should stay OFF

USER: *"If its neutral and better quality then it's perhaps worth flipping on. Though that depends a bit
on how much the quality improved. If it is very small providing it is better is more difficult."* Exactly
right, and the recorded evidence was never an effect size — `slower=0 faster=5` / `faster=7` are CELL
COUNTS. Converted to net loss-penalized turns on held-out seeds (26 suite decks, 3 seeds each, 40 games,
d3/b10, seeds 700000/701000/702000 — stride 1000 > games 40; snow excluded as proven byte-identical):

    paired cells 78   games/arm 3120
    NET loss-penalized turns (dedup - ctrl) = +1.00 over 3120 games (+0.000321/game)
    cells whose PLAY changed: 5 of 78
      dragonstorm s702000  4.4250 -> 4.4250   +0.00 / 40
      fluctuator  s702000  3.8500 -> 3.8750   +1.00 / 40   <-- exactly ONE game = the metric's quantum
      giants      s700000  5.7750 -> 5.7750   +0.00 / 40
      hinata      s700000  5.3000 -> 5.3000   +0.00 / 40
      hinata      s702000  5.5000 -> 5.5000   +0.00 / 40
    of moved cells: BETTER 0   WORSE 1   digest-only 4

**Verdict against the user's three-leg bar: quality NULL (not positive), speed NEUTRAL, soundness fine.
So `MTG_CAND_DEDUP` stays DEFAULT OFF** — flipping it would move 17 GT keys to buy nothing. The
2026-09-09 `faster=5`/`faster=7` reading was the cell-count artefact it looked like: on held-out seeds
only 5 of 78 cells move at all, four of them at *identical* avg, and the single non-tying cell is one
game the wrong way. Same lesson as every other trade this repo has re-measured — a cell count is not an
effect size, and held-out replication is what separates them.

This closes the lever. The remaining work is §G (the stranded-activation pre-check), which the user
correctly identified as the much bigger one.

---

## 2026-09-30 (later): §D and the activation-condemnation ruling both MEASURED — and both are small

Two of the three levers §353's summary table listed as OFF are now settled with numbers rather than
estimates. Both fire. Neither is worth flipping. The reasons are different and both are worth keeping.

### D-bis. The colour-blind gate is real and its ceiling was 13.2% — but the opens it removes are already CHEAP

§D predicted the size as *"single-digit-to-15% of site-8 opens"*. Measured, on a 20-game snow d3/b10
cell (seed 500000), with the test **counted but not applied** so the reading costs zero behaviour change:

    nonland finds                       1,158,427
    ...pass the shipped MANA-VALUE gate    871,579  (75.2%)
    ...that a COLOUR test would reject     115,254  (13.22% of mv_pass)   <-- the ceiling

So the prediction was right and the mechanism is exactly as described: the activation that just fired
tapped a Scrying Sheets and paid its own `{1}{S}`, and 4 Sheets + Boreal Druid all produce `{C}`, so the
pool left behind is both small and colourless-skewed. A found Frost Augur `{U}`, Boreal Druid `{G}`,
Ice-Fang Coatl `{G}{U}` or Marit Lage's Slumber `{1}{U}` passes `Total() >= ManaValue()` off mana that
provably cannot cast it.

**One §D claim was wrong and is retracted.** §D said a colour test *"is NEW code, not a reuse"*. It is a
reuse: `ManaPool::CanPay` is exactly the right helper — it is the enumerator's own payability test, and
it is deliberately OPTIMISTIC (every multi-colour source counts as one `wild` that satisfies any single
pip), so a `CanPay` failure means no assignment of the untapped sources pays that cost at all. That is
what makes the gate a **necessary condition** and therefore incapable of deleting a reachable line. The
whole lever is four lines.

**But arming it buys almost nothing on the shipped cell** (`MTG_SNOW_LOOK_COLOR=1`, 3 reps interleaved,
single-threaded, idle box):

    units_total   2,025,490 -> 2,020,315   -0.26%
    mv_pass         871,579 ->   865,501   -0.70%
    CPU median        28.38s ->    28.29s   -0.3%  (within-arm spread 2.0% / 0.85% -> NOISE)
    avg_win_turn      6.3000 ->    6.3000   IDENTICAL
    digest      3d7f67aa9427a33d -> 6a39035bb1de674b   play MOVED

Two reasons, and they compound:
* **The chain cap already made the opens cheap.** §C: only a *searched* continuation (`bp_choice`
  targeting this occurrence, and only at `s_snow_look_depth == 0`) runs a real Solve; every other open
  takes the narrow land-only fallback. So most of the 115,254 rejected opens were never the expensive
  thing — §D implicitly priced them all as re-solves, and that was the error in the estimate.
* **Under a budget no work reduction can show up as time.** [[matrix-cost-is-abandonment-rate]] — ~90%
  of a snow cell's wall is games pinned at `abandon_units`, so the budget is a work CAP and freeing work
  inside it just lets the search spend it elsewhere. This is why the reading had to be repeated
  unbudgeted, and why any future snow lever must be sized there first.

### H. `MTG_BP_CONDEMN_ACTIVATION` — the USER's 2026-09-18 ruling FIRES, and changes NOTHING

USER 2026-09-18: *"Activation should be able to be condemned."* Mode 1 ("same ability, identical card",
a NAME test) was built to that spec and left default OFF, unmeasured, and §E identified it as the half
that covers snow's real decision shape — condemnation's top blocker is `noplancast` at 186,756 (43%:
*"the arriving plan cast NOTHING, so it declined nothing"*), and snow's site-8 turns are a land drop plus
tap-draw ACTIVATIONS, which the hand-indexed candidate loop can never see.

Measured (same 20-game cell, 2 reps each, interleaved):

    drops                     0 -> 3,342      it FIRES -- not inert, so this is not a [[digest-equality-can-mean-broken]] null
    cand_scored         718,267 -> 720,808    +0.35%   (work goes UP)
    units_total       2,025,490 -> 2,032,522  +0.35%
    CPU median            28.32s -> 28.61s    +1.3%
    avg_win_turn         6.3000 -> 6.3000
    digest     3d7f67aa9427a33d -> 3d7f67aa9427a33d   BYTE-IDENTICAL

**So the rule is sound, implementable, and inert in outcome: it drops 3,342 candidates and the play does
not change at all, while work rises 0.35%.** The mechanism is the same one that killed §A's width
argument: a breakpoint's wave slots are a FIXED budget (W=2), so condemning a candidate does not remove
a slot — it frees one, and the next-ranked variant immediately fills it. Condemnation can only pay off
where it shortens a list *below* the width, and snow's lists (1.7–2.3 entries after `MTG_BP_NEW_ONLY`)
are already at or under it.

That is a genuinely useful negative result for the user's ruling: the objection to activation
condemnation was never soundness (the availability-is-evidence-of-decline argument holds, and needs no
new snapshot), it is that **there is no work downstream for it to remove.** Keep it default OFF; keep
the lever, because the finding is only reproducible with it.

### What this leaves

Of §353's three OFF levers: `MTG_CAND_DEDUP` closed (§F/F-bis, quality null + speed neutral),
`MTG_BP_CONDEMN_ACTIVATION` closed (§H, play-identical), `MTG_FOLD_SEARCH_ODO` still unmeasured. The
live items are §G (the stranded activations — see
`trailing-activation-payment-hole.md`, where the mechanism is now split into two separate defects) and
the unbudgeted sizing of `MTG_SNOW_LOOK_COLOR` + the user's `MTG_DIG_MANA_LAST`.

**The methodological rule this session earned, and it is the reusable part:** *size a snow lever
UNBUDGETED before believing a budgeted null.* Three levers in a row (`MTG_CAND_DEDUP`,
`MTG_SNOW_LOOK_COLOR`, `MTG_BP_CONDEMN_ACTIVATION`) measured ~0 on a b10 cell, and a work CAP guarantees
that outcome for any lever that removes work rather than removing *games*.

---

## 2026-09-30 (overnight): the two cost levers read on HELD-OUT seeds — one adoptable, one REFUTED

One pooled batch, 128 jobs, **1,440 games per arm**, four arms carried as per-job `heurarm` flags (no
per-arm process wave). Decks: **snow AND EldraziDisplacerFlicker** — edf is in scope because
`MTG_DIG_MANA_LAST` gates any `TapDraw` mode and Mariposa Military Base (`tap_draw_cost {5}`) ships
there; edf is not in `regression_cases.sh`, so it has no GT and was measured off its own decklist.
Cells: d3/b10 × 60 games and d5/b20 × 30 games, 8 seeds each, seeds **810000 + 1000·i** — held out from
every GT tier (smoke 1001, regression 2002/3003, overnight 4004–7007) and strided 1000 > games/job so
no job replays another's games. Paired on (deck, depth, seed); metric is NET loss-penalized turns ×
games, because a per-cell delta equal to `1/games` is ONE game.

    lever                    NET turns (+=worse)   CPU      cells moved   BETTER  WORSE  digest-only
    MTG_SNOW_LOOK_COLOR           -1.00            -0.58%      12 / 32       1      0        11
    MTG_DIG_MANA_LAST             +7.98            -4.77%      22 / 32       2      8        12
    both                          +6.98            -5.00%      25 / 32       2      7        16

### `MTG_SNOW_LOOK_COLOR` — RECOMMENDED for adoption (not flipped; see why below)

**Zero cells worse in 1,440 games.** Net −1.00 turns is exactly the metric's quantum (one game), so
call the quality **NULL-to-slightly-better**, and the load-bearing fact is the 0/12 worse. Cheaper in
both regimes: −0.58% CPU budgeted, and **−1.78% units / −1.84% CPU unbudgeted with a BYTE-IDENTICAL
digest**. It is a strictly tighter necessary condition (`ManaPool::CanPay`, the enumerator's own
deliberately-optimistic payability test).

**The scoping argument was confirmed empirically, not just asserted:** all 12 moved cells are snow, and
**every edf cell is byte-identical** — as predicted from `tap_draw_requires_top_supertype` existing only
on Scrying Sheets and Frost Augur, held only by `decks/Snow`.

Against the user's standing bar — *"if the option works with neutral or net positive quality, is faster
and doesn't introduce unrecoverable lines it should be on by default"* — this clears all three legs.
**One honest caveat on the third leg:** the gate's own question ("can the FOUND card be played now") is
answered losslessly, but a skipped continuation is a full re-solve that could in principle have cast
something *else*. Site 8's doctrine makes that out of scope (USER 2026-09-06, *"we need to be able to
play it"*), and the evidence is consistent with nothing being lost — byte-identical unbudgeted, 0 worse
cells budgeted — but that is evidence, not proof.

**Why it was NOT flipped tonight, which is a tree-state reason and not an evidential one.**
`test/regression_gt.txt` and 22 `gt_logs/*.wins` files are **already modified and uncommitted** by the
`MTG_ACT_LINE_HOLD` adoption. Both levers move snow's keys, so a second rebaseline on top would
entangle the two adoptions in one uncommitted tree and leave neither separately reviewable. The right
order is: commit the ACT_LINE_HOLD rebaseline first, then flip this lever and rebaseline snow as its own
step. That sequencing is the user's call because the commit is.

### `MTG_DIG_MANA_LAST` — REFUTED as a default. The user's own caution was right

USER 2026-09-25 asked for this lever and flagged it up front: *"it's a real question as to whether this
will be 100% lossless."* It is not. **Net +7.98 turns worse over 1,440 games, 8 cells worse against 2
better**, and the worst single cell is edf d3 s815000 at **+3.00 turns** — three games. That buys
−4.77% CPU (−11.0% units unbudgeted). A 0.0055 turns/game quality cost for ~5% CPU is not a trade this
deck wants, and the losses are exactly the mechanism the lever's own comment predicts: it forbids the
**dig-then-deploy** line whenever the mana is contested, which is the line breakpoint site 8 exists for.

Worth noting what the 2-game unbudgeted probe said: **byte-identical digest**. That is how a lossy
lever looks on a 2-game sample, and it is a clean example of why a held-out read is not optional — the
cheap probe sized the *cost* correctly (−11%) and was completely uninformative about the *quality*.

**Verdict: stays default OFF.** Keep the lever; it is the measured answer to a standing user request,
and the number is the deliverable. If ~5% CPU on snow ever becomes worth 0.0055 t/game, this is the
price tag.
