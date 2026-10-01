# The trailing-activation payment hole

An **engine-wide** reachability defect, found while onboarding SelesnyaLifegain (2026-09-30) and
measured on four decks. Self-contained: everything needed is here or in the referenced code.

This is the sibling of [`mana-source-reservation.md`](mana-source-reservation.md), and it is the
follow-up that doc scoped and never took: whole-turn batch payment jointly solves the turn's
**casts**, and *trailing activations sit outside that solve*.

---

## The defect

A committed plan is applied casts-first, then its **trailing activations** (the
`apply_trailing_activations` pass in both worlds — `AIEngine::TakeTurn` and
`TurnSolver::ApplyPlanDirect`). The cast payment cannot see those activations, so it spends whatever
`ManaSourceRank` puts first. Two things then happen, and **neither is disclosed**:

**A — the source is tapped for MANA, nullifying the `{T}` half of its own cost.**
`PermAbilitySourceLive` (`src/core/SpellEffects.h`) fails and the activation branch no-ops with no
log line, no error and no else-branch. The source ends **TAPPED**.

**B — a pip the activation needs is spent by the casts.** The `{T}` half is paid first
(`SetPermTapped`), `TapForCost` then fails, and the tap is rolled back. The source ends **UNTAPPED**.

**The final tap state is how you tell them apart.**

### Why it was invisible

A dropped **cast** gets a `drops` disclosure; a dropped **activation** gets nothing.
`Plan::would_drop` is populated only in the cast-ORDERING expansion (`TurnSolver.cpp`, gated on >= 2
reorderable `CastFromHand` actions), so an unaffordable activation is *structurally* unlabelled. The
rollout twin is identical to the executor, so the mismatch harness correctly reports **zero
`fd-diverge`** — executor and rollout ARE in lockstep. Nothing in the test apparatus could see it.

The only pre-existing counter, `g_stranded_activation_count` (`TurnSolver.cpp`), covers case B in the
**rollout only** and surfaces only under `DedupCensusOn()`. Case A was uncounted anywhere.

### It is a reachability defect, not a mis-scored plan

The search is not lying to itself: it scores the plan as "casts, no activation" because both worlds
drop it identically. What is wrong is that **the strictly better line — casts AND the activation — is
absent from the choice set entirely.** On SelesnyaLifegain this made a 2-of land's whole payoff
clause unreachable on any turn that also cast a spell, i.e. almost every turn, on a deck whose
engine is life-gain events.

---

## The instrument: `MTG_ACT_DROP_AUDIT`

Added with the fix (`src/core/GameLogger.{h,cpp}`, four call sites in `AIEngine.cpp` /
`TurnSolver.cpp`). Purely additive — every digest is byte-identical whether or not it is on, which is
what lets it run over a whole tier.

```
MTG_ACT_DROP_AUDIT=1 ./build/Release/mtg <deck> ... 2>&1 | grep ACT_DROP
MTG_ACT_DROP_AUDIT=2   # ...and one line per drop (turn, card, cost, reason)
```

It reports four reasons and **only two are defects**. Splitting them is what makes the number
honest: the first cut reported 28% on Snow and a large share of that was legitimate.

| reason | meaning | defect? |
|---|---|---|
| `tapped` | source already tapped for MANA when the trailing pass ran | **yes (A)** |
| `unpaid` | `{T}` paid, mana half failed and rolled back | **yes (B)** |
| `gone` | source had left the battlefield (sacrificed to an earlier cost, bounced, blinked) | no |
| `noTap` | summoning-sick (CR 302.6) or activation-restricted (Bilbo's life gate) | no |

`PermAbilityDeadReason` (`src/core/SpellEffects.h`, next to `PermAbilitySourceLive`) does the split.
`Fungus` is the worked example of why it matters: its 2,087 drops are **all** `gone`, so it reads
CLEAN once classified and alarming before.

The same flag also prints a second line, **`ACT_HOLD_COST`**, which prices the fix rather than the
defect (`g_act_pay_calls` and friends in `GameLogger.h`; the counters live at the reserve-mask rung of
`TapForCostSharedImpl`). It exists because the cost question turned out to have a counter-intuitive
answer — see "THE CPU COST" below.

| counter | meaning |
|---|---|
| `pay_calls` | payments reaching the reserve-mask rung — the denominator |
| `hold_mask` | `ActLineHoldMask` returned non-zero (the lever held something) |
| `hold_solo` | …and it was `rmask`'s **only** contributor, so the held attempt exists *because of the lever* |
| `hold_retry` | a held attempt failed while the lever was holding ⇒ one extra solve |
| `solo_retry` | …and the lever was solely answerable for it. `solo_retry / pay_calls` is the lever's added-solve rate |

`act_hold` is split out of the `rmask` fold in `ManaPayment.cpp` purely so this attribution is
possible; the folded value is identical and nothing under `ActDropAuditOn()` executes when the audit is
off. Verified inert: with the audit on, snow d3's digest is `1f9935c72324a70c` (OFF arm) and
`385c5169741d3dcc` (ON arm) — the same two digests those arms produce with the audit off.

### Measured at HEAD, before the fix (40 games, d3, budget 20 ms, seed 9001)

| deck | `tapped` | `unpaid` | `fired` | defect rate |
|---|---|---|---|---|
| Snow | 1,539,268 | 62,479 | 3,851,912 | **29.4%** |
| EldraziDisplacerFlicker | 214,255 | 103,753 | 414,267 | **43.5%** |
| SelesnyaLifegain | 62,942 | 3,270 | 1,280,827 | 4.9% |
| Prevent Damage | 0 | 12,903 | 307,307 | 4.0% |
| Fungus | 0 | 0 | 257,761 | CLEAN |
| StompySurprise | 0 | 0 | 0 | CLEAN (no such activation) |

Note **Snow and EldraziDisplacerFlicker are far worse affected than the deck that found it.** Snow
carries a value leaf and a mulligan profile fitted to this play; EDF is not a suite deck at all and
has a reference corpus instead.

### THOSE RATES ARE UNDERSTATED — the audit could not see `UntapCreature` (fixed 2026-09-30)

The table above hooks only the `ActivatePermAbility` arm. `Action::Kind::UntapCreature` (Wirewood
Lodge's `"{G}, {T}: untap target Elf"`) is the *other* kind that drops the same two ways, and it was
counted nowhere. With it hooked, SelesnyaLifegain at the same 40-game d3 harness reads

| | `tapped` | `unpaid` | `fired` | defect rate |
|---|---|---|---|---|
| as first measured (`ActivatePermAbility` only) | 62,942 | 3,270 | 1,280,827 | 4.9% |
| **with `UntapCreature` hooked** | **167,560** | **27,865** | 1,414,907 | **13.8%** |

So the deck that found the defect is hit ~2.8x harder than reported. **An instrument that cannot see
a whole action kind reads as evidence of absence**, and this one did real damage: a game which
demonstrably dropped a Lodge untap reported `fired=0 ... (CLEAN)`, which is what sent the first
diagnosis of the `- cur[c]` defect below down the wrong path.

`UntapCreatureDeadReason` (`src/core/SpellEffects.h`, beside `CanApplyUntapCreature`) does the split,
using `PermAbilityDeadReason`'s encoding so one set of reasons covers both kinds.

**Still open, same family:** the `UntapCreature` apply arms make **no `LogAbility` call at all** in
either world, so a Lodge untap is invisible in the JSON game log as well — the play viewer never
shows it, and any log-based analysis silently under-counts it (it is why a first pass at classifying
the d0 regressions below put games in an "activation set unchanged" bucket that the audit then showed
had fired three activations). Adding the call is disclosure-only, but check the play digest before
assuming it is inert.

---

## The fix: `MTG_ACT_LINE_HOLD` (heurarm slot `ACT_LINE_HOLD`, ADOPTED 2026-09-30, DEFAULT ON; `=0` disables)

`ActLineHoldMask` (`src/ai/ManaPayment.cpp`), driven by two new `PlanTraits` fields
(`act_pips[5]`, `act_src_nums[]` — `src/ai/PlanContext.h`, filled by `ComputePlanTraits`). While a
plan apply is paying, hold back both halves of what its own trailing activations still owe:

* **their `{T}` sources**, so a cast payment cannot tap one for mana (case A);
* **enough providers of each COLOUR their costs need**, net of this payment's own pips and the
  float, narrowest provider first (case B).

Held by **count**, `LineColorlessHoldMask`'s shape rather than `ScarceColorHoldMask`'s
all-or-nothing: over-holding only makes the held attempt fail and costs a second solve.

Wired in **two** places, which is what makes it cover the whole turn:

1. `TapForCostSharedImpl`'s `rmask` — the per-cast greedy path (prepay declined).
2. A **rung of the `BatchPrepayMainCasts` ladder** (`reserved_a`, pushed above the line-`{C}` rungs,
   because losing a whole action costs more than a suboptimal tap order).

Same lossless contract as every other mask there — **held-first attempt, unrestricted retry** — so a
cast that genuinely needs a held source still gets it and no cast is ever lost.

### The `- cur[c]` defect: the first version of this mask HELD HALF AN ACTIVATION (fixed 2026-09-30)

The coloured half originally computed its hold count as

```cpp
need[c] = pt->act_pips[c] - cur[c] - fl[c];      // cur[] = THIS payment's own pips.  WRONG
```

An activation's pips are demand **alongside** the cast's, never demand the cast satisfies, so
subtracting `cur[c]` treats the cast's competing demand as *supply*. Whenever a cast wanted at least
as many pips of a colour as the activation did — the common case — `need[c]` went negative, clamped to
0, and the coloured half of the hold did **nothing** while part (a) still held the `{T}` source.

That is the worst of the three available states. Enough hold to spoil the casts' tap assignment, not
enough to make the activation payable: the `{T}` gets paid, the mana half fails, and the whole thing
rolls back — **case B, manufactured by the fix meant to close case A.** It is visible directly in the
audit's `unpaid` column, which *inflated* on every deck whose activations want a coloured pip and fell
only on Prevent Damage, whose activations want none:

| deck | `unpaid` OFF → ON (buggy mask) | activations need a coloured pip? |
|---|---|---|
| snow | 62,479 → **440,066** (7.0x) | yes |
| selesnya | 3,270 → **11,352** (3.5x) | yes |
| EldraziDisplacerFlicker | 103,753 → 162,268 (1.6x) | `{1}{C}` — colourless |
| Prevent Damage | 12,903 → **4,248** (0.3x) | **no** |

That inflation was originally written off as "reclassification" (a `tapped` becoming an `unpaid` once
the source is held). Some of it is. The 7x is not.

The fix is `need[c] = act_pips[c] - max(0, fl[c] - cur[c])`: only float that *survives* this payment is
supply the activation can spend without tapping anything. The prepay rung calls the mask with an
**empty** cost, so that call site is byte-identical; only the per-cast path changes — and a
single-cast turn always takes it, because the prepay declines on one cast.

**Worked repro** (SelesnyaLifegain, batch seed 4004 game 7 ⇒ `--seed 4011`, d0). T3 holds a Wirewood
Lodge for its `{G}, {T}` untap, pays Elvish Archdruid's `{1}{G}{G}` by tapping the **attacking Priest
of Titania** instead of the Lodge, and then cannot raise the `{G}` — no untap *and* no attack, and the
win slips T6 → T7. With the arithmetic fixed the held attempt is correctly unaffordable, the
unrestricted retry fires, and both arms win on T6:

```bash
for arm in "" MTG_ACT_LINE_HOLD=1; do echo -n "[$arm] "; env $arm ./build/Release/mtg \
  decks/SelesnyaLifegain/SelesnyaLifegain.cod \
  --profile decks/SelesnyaLifegain/SelesnyaLifegain.profile.json \
  --games 1 --seed 4011 --depth 0 --budget-ms 0 --ignore-play-profile --threads 1 \
  2>/dev/null | grep -i "avg (turns)"; done
```

**Inert with the lever off — checked per GAME, at both depth classes, not by aggregate.**

* **d0:** the four free d0 cells (`selesnya`/`snow` × `smoke s1001` / `regression s2002`, 1000 games
  each) reproduce `test/gt_logs/*.wins` **byte-identically** on the fixed binary.
* **searched (d5, b20):** 150 `edf` games run lever-off on the pre-fix and post-fix binaries agree on
  every per-game win turn *and* play digest. (Free check: a job's game `i` uses shuffle seed
  `seed + i` independently of the job's size, so a 25-game job's games 0–24 are directly comparable to
  a 100-game job's games 0–24 at the same seed.)

### Why the prepay rung is not optional

`MTG_ACT_TAP_RESERVE` (the pre-existing, narrower, also-default-OFF lever) reserves the activation's
source by card number through `g_plan_reserved_sources`. It is installed in
`AIEngine::TakeTurn` **at line ~4775 — AFTER the `BatchPrepayMainCasts` call at ~4763.** So it cannot
stop the prepay from stealing the source, and measurement confirms it: on SelesnyaLifegain seed 77617
T4 the prepay taps Blighted Steppe for Feed the Clan's `{1}` (four sources tapped for a three-mana
turn), and **`MTG_ACT_TAP_RESERVE=1` does not fix that repro** — nor does `MTG_NO_BATCH_PAY=1`, nor
`MTG_LINE_C_HOLD=1`, nor all three together. **Do not re-derive this** — the whole matrix is one loop
over the repro above, and every arm gives `life: 25` with Blighted Steppe still on the battlefield
(only `MTG_ACT_LINE_HOLD=1` gives 31 with it in the graveyard):

```bash
for e in "" MTG_ACT_TAP_RESERVE=1 MTG_NO_BATCH_PAY=1 MTG_LINE_C_HOLD=1 \
         "MTG_NO_BATCH_PAY=1 MTG_ACT_TAP_RESERVE=1 MTG_LINE_C_HOLD=1" MTG_ACT_LINE_HOLD=1; do
  echo -n "$e -> "
  env $e ./build/Release/mtg decks/SelesnyaLifegain/SelesnyaLifegain.cod \
    --profile decks/SelesnyaLifegain/SelesnyaLifegain.profile.json \
    --claude-play --seed 77617 --max-turns 8 --reveal 6 \
    --choices "1,1,0,0,0,0,0,2,0,0,0,60" 2>/dev/null \
  | grep -oE '"life": [0-9]+|"graveyard": \[[^]]*\]' | head -2 | tr '\n' ' '; echo
done
```

### Self-limiting across a multi-activation trailing pass, by construction

The `{T}` half is paid **before** the mana half, so an activation that has already fired owns a
**tapped** source; the mask only ever holds untapped ones, so it automatically covers exactly the
activations still to come. That is the two-Scrying-Sheets case `MTG_ACT_TAP_RESERVE` was written for,
covered here for coloured pips as well as for the tap.

### NOT gated on `HumanPlayActive()`

`LineColorlessHoldMask` stands down under human play, because its hold is a tap-ORDER preference and
a replayed reference would drift off its recorded picks. This mask is different in kind: the human
explicitly **chose** a plan containing the activation, so paying it in a way that silently deletes
the activation is not a preference being overridden — it is the plan not being executed. The
reference corpus is **verified** instead of standing down.

---

## Measured effect of the fix

### On the instrument (same 40-game d3 harness, OFF → ON)

| deck | `tapped` | `unpaid` | total defects | `fired` |
|---|---|---|---|---|
| SelesnyaLifegain | 62,942 → **330** | 3,270 → 11,352 | 66,212 → **11,682** (−82%) | 1,280,827 → 1,334,177 (+4%) |
| Snow | 1,539,268 → 338,014 | 62,479 → 440,066 | 1,601,747 → **778,080** (−51%) | 3,851,912 → 6,237,594 (**+62%**) |
| EldraziDisplacerFlicker | 214,255 → 45,391 | 103,753 → 162,268 | 318,008 → **207,659** (−35%) | 414,267 → 502,787 (+21%) |
| Prevent Damage | 0 → 0 | 12,903 → **4,248** | 12,903 → **4,248** (−67%) | 307,307 → 311,393 (+1%) |
| Fungus / StompySurprise | unchanged | unchanged | CLEAN either way | unchanged |

**Read the `fired` column, not just the drops.** Part of the `unpaid` rise is *reclassification*: the
source is now held, so `PermAbilitySourceLive` passes, the `{T}` is paid, and a case that used to
count as `tapped` now counts as `unpaid` if the mana half still fails. The unambiguous number is
`fired` — the engine now actually makes 62% more of the activations it planned on Snow.

### Residual `unpaid`, and why it is left

Three causes, none of which this lever should fix:

1. **Generic capacity is not held.** Blighted Steppe's `{3}{W}`: the `{W}` provider is held, the 3
   generic are not. Holding generic capacity means *casting fewer spells*, which is a real trade and
   the search's call, not the payer's.
2. **The held attempt failed and the unrestricted retry released the hold.** Correct by design —
   never lose a cast.
3. **Genuinely unpayable** (SelesnyaLifegain seed 77101: Nykthos Paragon `{4}{W}{W}` plus the
   Steppe's `{3}{W}` need three white pips against two white sources). Rules-correct as a no-op.

Case 3 is a **disclosure** bug, not a payment one, and is still open — see "Open" below.

### The lever is INERT when off — proved, not assumed

Smoke arm with `MTG_ACT_LINE_HOLD` unset: **104/104 PASS, `configs changed: 0 / unchanged: 104`,
`play-changed=0`** at both depth classes. Unit tests and scenario fixtures pass on **both** arms
(359 unit / 118 scenarios). The reference corpus passes `viewer_protocol_check.py --strict` with the
lever **ON** (`0 play-drift, 0 enum-gap, 0 contract-fail` over 355 refs), which is what justifies not
standing down under human play.

### The tier read, and why its searched-depth verdict was NOT evidence

Only `selesnya`, `snow`, `melira` and `stompy` ever moved; every other deck is byte-identical.

| tier | games/config | selesnya net | snow net |
|---|---|---|---|
| smoke (1 seed) | 500–1000 | **−0.0420** (better) | +0.0360 (worse) |
| regression (2 seeds) | 30–1000 | **−0.0080** (better) | **−0.0627** (better) |
| **overnight (4 seeds)** | 100–1000 | **+0.0275** (worse) | **+0.0807** (worse) |

Smoke and regression are noise at this effect size and point in **opposite** directions for Snow, so
the overnight tier is the one to read. Per-config means, split by depth class:

| deck | depth class | configs | mean delta |
|---|---|---|---|
| selesnya | d3/d5 (searched — what ships) | 8 | **+0.00025 — NEUTRAL** |
| selesnya | d0 (greedy) | 4 | +0.00637 |
| snow | d3/d5 (searched) | 8 | **+0.00709** |
| snow | d0 (greedy) | 1 | +0.02400 |

> **CORRECTION (2026-09-30). `snow +0.00709 at searched depth` was read as a real, consistent cost and
> it is not a measurement at all — it is the metric's QUANTUM.** Decode each cell against its own game
> count: every `snow` d3 cell has 150 games and reads exactly **+0.0067 = 1/150**, and every `snow` d5
> cell has 100 games and reads exactly **+0.0100 = 1/100** (or 0.0000). The "striking consistency
> across 8 configs" is **one game, one turn worse, in seven of eight cells** — 7 discordant games out
> of 1,000. A per-cell delta that equals exactly `1/n` in every cell is the signature of a single-game
> flip, and averaging eight of them does not add power; it just reproduces the quantum with more
> decimal places. `slower=36 faster=29` over 65 configs says the same thing: p ≈ 0.45.

**The data supports a far sharper read than per-config means, and it was sitting unused.** Same seed +
same `game_index` ⇒ identical library and opening hand, so every game is a **matched pair**, and the
`.wins` files carry per-game win turns *and* play digests. Only games whose digest changed carry any
information; the rest are exact ties. Pairing the overnight arm against committed GT
(method: read both arms' `.wins`, score an unwon game's `-1` as `max_turns+1 = 9` per
`ComputeAvgTurns`, join on `game_index`, and count better/worse **only over rows whose digest
differs**):

| deck | depth class | games | diverged | better | worse | tie | sign p |
|---|---|---|---|---|---|---|---|
| selesnya | d3/d5 (searched) | 6,000 | 916 | 24 | 24 | 868 | **1.000** |
| selesnya | d0 (greedy) | 8,000 | 1,398 | 51 | 97 | 1,250 | **0.0002** |
| snow | d3/d5 (searched) | 1,000 | 371 | 5 | 12 | 354 | 0.143 |
| snow | d0 (greedy) | 1,000 | 431 | 8 | 27 | 396 | **0.002** |
| melira | all | 9,400 | 2 | 0 | 0 | 2 | 1.000 |

So the effect is **real and highly significant at d0, and absent or unestablished at searched depth**
— selesnya is 24-vs-24, an exact null on 916 diverged games. The `- cur[c]` defect above was found by
chasing one of those d0 games.

*(Trap worth keeping: a first pass at this analysis scored `.wins`' unwon marker `-1` literally
instead of as 9. That inverted `snow` d0 from +0.024 to −0.046 and manufactured a disagreement with
the doc. Check the loss encoding before trusting any re-analysis of a `.wins` file.)*

### The budget sweep did not show what it was read as showing

`slower` at a tight budget can mean the extra held-first solve simply stole search work. The sweep run
to rule that out — Snow d5 seed 7007, **75 games** per cell — reads:

| per-decision budget | lever OFF | lever ON | delta | decoded |
|---|---|---|---|---|
| 20 ms (the suite's) | 6.1467 | 6.1600 | +0.0133 | **1 game** (1/75) |
| 80 ms (4x) | 6.1333 | 6.1600 | +0.0267 | **2 games** (2/75) |
| 200 ms (10x) | 6.1467 | 6.1733 | +0.0267 | **2 games** |

"The gap widens and then persists" is one game becoming two games out of 75. It neither establishes
nor refutes churn. **Before quoting a delta, divide it by `1/games` and say how many games it is.**

### THE DEPTH LADDER: the cost lives ONLY where there is no search, and dies at ONE ply

Fixed lever vs lever-off, paired per game, on non-overlapping seeds held out from every tier. d0 costs
~0.14 ms/game, so it gets 20,000 distinct games; the searched cells get 2,000–4,000 each.
**`better`/`worse` count only the games whose play digest changed.** One pooled `mtg --batch` over both
arms; each job is

```json
{"name":"<deck>_d<depth>_s<seed>_<arm>", "deck":"decks/…/X.cod", "profile":"decks/…/X.profile.json",
 "games":<per>, "seed":200000+10000*k, "depth":<d>, "budget_ms":<b>, "ignore_play_profile":true,
 "flags":{"MTG_ACT_LINE_HOLD":true}}          // "flags" omitted entirely for the off arm
```

with the two arms given **identical** `(seed, games)` so `seed + game_index` pairs every game, and the
seed stride (10000) chosen to exceed every cell's `games` per job — see the seed trap below.

| deck | depth | distinct games | diverged | better | worse | mean/game | sign p |
|---|---|---|---|---|---|---|---|
| snow | **d0 (no search)** | 20,000 | 9,051 | 198 | 447 | **+0.01350** | **~0 worse** |
| snow | d1 | 4,000 | 1,544 | 35 | 30 | −0.00125 | 0.620 |
| snow | d2 | 2,000 | 773 | 15 | 14 | −0.00050 | 1.000 |
| snow | d3 | 3,000 | 1,147 | 22 | 28 | +0.00200 | 0.480 |
| snow | d5 | 2,000 | 775 | 18 | 19 | +0.00050 | 1.000 |
| selesnya | **d0 (no search)** | 20,000 | 3,645 | 130 | 256 | **+0.00645** | **~0 worse** |
| selesnya | d1 | 2,000 | 387 | 24 | 17 | −0.00350 | 0.349 |
| selesnya | d2 | 2,000 | 377 | 25 | 12 | −0.00650 | **0.047 better** |
| selesnya | d3 | 4,000 | 751 | 38 | 30 | −0.00200 | 0.396 |
| selesnya | d5 | 3,000 | 555 | 24 | 19 | −0.00167 | 0.542 |
| EldraziDisplacerFlicker | d0 | 20,000 | 1,465 | 161 | 181 | +0.00115 | 0.304 |
| EldraziDisplacerFlicker | d3 / d5 | 1,000 / 400 | 47 / 17 | 3 / 2 | 2 / 0 | ~0 | 1.000 / 0.500 |
| melira | d3, d5 | 1,000 each | **0** | 0 | 0 | 0 | — |
| stompy | d3, d5 | 1,000 each | **0** | 0 | 0 | 0 | — |

**Not one searched cell is significantly worse, on any deck.** Pooling each deck over its searched
depths only (d1/d2/d3/d5, d0 excluded):

| deck | games | diverged | better | worse | net turns | mean/game | sign p |
|---|---|---|---|---|---|---|---|
| selesnya | 11,000 | 2,070 | 111 | 78 | **−33** | −0.00300 | **0.020 — better** |
| snow | 11,000 | 4,239 | 90 | 91 | +1 | +0.00009 | 1.000 (null) |
| EldraziDisplacerFlicker | 1,400 | 64 | 5 | 2 | −2 | −0.00143 | 0.453 |
| melira | 2,000 | **0** | 0 | 0 | 0 | 0 | — |
| stompy | 2,000 | **0** | 0 | 0 | 0 | 0 | — |

`melira` and `stompy` no longer diverge on a single game (the pre-fix lever moved their digests). The
entire cost is at **d0**, and it is gone by **d1** — not a gentle decline with depth but a cliff
between "no search" and "one ply".

> **Report this BY DEPTH, never pooled.** d0 carries 20,000 of selesnya's 31,000 games, so the pooled
> row reads `+0.0031, p ≈ 0` — "significantly worse" — purely because the greedy cell outweighs the
> four searched ones. The pooled number is real arithmetic and a meaningless summary.

> **Seed trap, kept because the tiers hide it.** A batch job's game `i` shuffles with `seed + i`
> (`BatchRunner.cpp:58`), so **consecutive** per-job seeds OVERLAP. A first version of these runs used
> seeds 31001…31010 with 4,000 games per job and reported "40,000 games per arm"; it was **4,009
> distinct games each counted exactly 10.0x**. Direction and ratios survive deduplication, but `n` and
> every p-value were inflated 10x — and at d5, where 20 jobs × 150 games covered only **169** distinct
> games, the unrepresentative sample read `0 better / 60 worse` where the truth is `24 / 19`. The
> regression tiers are immune *by design* (seeds spaced 1001+ apart, wider than any case's game count),
> which is exactly why the habit is invisible until you write your own manifest. Rule:
> `seed = BASE + STRIDE * job_index` with `STRIDE > games_per_job`, asserted in the generator. Tell
> that you have it wrong: replaying the "worse" games by `base + gi` hands you the same seed twice.

### Why d0 is the whole story

The d0 cost is ~2:1 worse-than-better and the arithmetic fix did not shift it — which is the first clue
that **it is not the same defect, and not a payment defect at all.** Replaying the losing games and
asking the one question that separates the two possible causes — *did the held activation actually
fire?*:

* **Snow: 12 of 12** losing games fired an extra activation, every one a **Scrying Sheets** look.
* **SelesnyaLifegain: 5 of 10** fired an extra one at the first diverging turn (dominantly **Blighted
  Steppe**), and the remaining 5 turned out to be firing too — they were misclassified because
  `UntapCreature` writes no log line (seed 31077: the audit shows lever-OFF **dropping** the T3
  Wirewood Lodge untap with `tapped(mana stole the {T})` and `fired=0`, while lever-ON reports
  `fired=3 (CLEAN)`).

**The hold does its job; the activation happens; the turn is worse.** Seed 31077 is the clean picture:
both arms cast Elvish Mystic + Priest of Titania for 3 mana, but lever-OFF pays from three *lands* and
attacks for 4, while lever-ON pays by tapping an **Elvish Mystic** and attacks for 2 — so T4's
16-damage alpha strike leaves the opponent on 2 instead of 0 and the win slips T4 → T5.

### So the answer to "why doesn't fixing a bug help every deck?" is: TWO BUGS WERE CANCELLING

At d0 there is no search (`SolveWithLookahead`: `depth <= 0` ⇒ `Solve()`, the greedy plan builder,
which ranks candidate plans by **projection**). So the activation *is* compared against not doing it,
and the projection **prefers it** — then loses. The greedy evaluator over-values these activations
relative to what they cost:

* **Scrying Sheets**, `{1}, {T}: look at the top card` — the `{1}` is mana not spent on the board, and
  the look only sometimes hits.
* **Blighted Steppe**, `{3}{W}`, `{T}`, **sacrifice a land**, gain 2 life per creature — against a
  passive opponent life buys **nothing directly** (only Ageless Entity counters and Nykthos Paragon
  waves), and it permanently costs a land.
* **Wellwisher**, `{T}`: gain 1 life per Elf — likewise worth only what it enables.

The payment hole had been **silently deleting those plans**, which is why the greedy evaluator's
miscalibration never showed up in any number. Closing the payment hole does not create the cost; it
**stops hiding a second, independent, pre-existing defect** — the greedy projection's valuation of a
trailing activation. And the ladder is the signature of exactly that: the cost is significant at d0,
**gone at d1**, and stays gone through d5, because from one ply of lookahead onward the search prices
the line and declines it when it is wrong. A miscalibrated *leaf heuristic* is precisely the kind of
defect that vanishes the moment anything searches past it; a payment defect would not care about depth.

**And this is already a known weakness with a standing user ruling on it.** The bar is *"no greedy
steps except attack decisions and mana allocation"* (2026-08-09), and the principle verbatim:
*"search should be truly search at every level. Greedy is simply too unreliable to be part of it."*
(`main-phase-classification.md:103`; the greedy-Solve memo's own header calls itself
**transitional by design** for the same reason). That ruling is aimed at greedy *inside the search
window*, and d0 is a configuration that is greedy by construction rather than by accident — so it does
not say "d0 must be fixed". What it does say is that a number produced by the greedy planner is not a
number to gate a correctness fix on.

This is why the original recommendation below (gate the hold on the archetype provider) was the wrong
call. A provider gate would suppress a correct reachability fix on every other deck in order to keep a
*different* component's miscalibration out of one tier's ground truth — and would leave that
miscalibration unrecorded, still mispricing every activation the engine ever plans at d0.

That said, note what the fix is and is not. It does not make the decks faster; it makes the simulator
able to express a legal line it could not express before. For a tool whose purpose is comparing card
and deck performance, that matters independently of average win turn — a user asking "is Blighted
Steppe worth a slot?" was being answered on a board where it never activated.

---

## THE CPU COST: +21–30% on Snow (depth-dependent), and it is VOLUME, not overhead

The win-turn ladder above says nothing about cost, so the cost was priced separately. Per-job
**core**-ms is already printed by every batch (`main.cpp`: `ms=` on each job line, from
`BatchJobResult::elapsed_ms`), and the v2 A/B ran both arms as matched `(seed, games)` jobs **inside
one pool**, so both arms saw the same box and the same contention. Paired by cell:

| cell | OFF ms/game | ON ms/game | ratio |
|---|---|---|---|
| snow d1 / d2 / d3 / d5 | 1502.9 / 2820.3 / 3397.8 / 4820.2 | 2028.6 / 3717.7 / 4471.8 / 6153.5 | **1.35 / 1.32 / 1.32 / 1.28** |
| selesnya d1 / d2 / d3 / d5 | 246.9 / 850.7 / 1022.5 / 1909.1 | 253.4 / 876.7 / 1049.4 / 1949.3 | 1.03 / 1.03 / 1.03 / 1.02 |
| edf d3 / d5 | 8193.3 / 14697.7 | 8350.1 / 14990.5 | 1.02 / 1.02 |
| melira d3 / d5 | 424.7 / 723.7 | 415.2 / 710.2 | 0.98 / 0.98 |
| stompy d3 / d5 | 91.1 / 54.8 | 91.9 / 50.4 | 1.01 / 0.92 |
| snow d0 / selesnya d0 / edf d0 | 0.026 / 0.229 / 23.2 | 0.059 / 0.238 / 23.9 | 2.21 / 1.04 / 1.03 |

Read the d0 row for what it is: snow d0 doubles in *ratio* and costs **0.033 ms/game** in absolute
terms. It is free and it is not the story.

**melira and stompy set the noise floor.** Those two diverge on zero games, so every one of their job
pairs is digest-identical — byte-identical play, same games — and they still scatter 0.92–1.01. So a
per-job read is worth about ±8%; snow's +28–35% across four independent depths is far outside it, and
selesnya's and edf's +2–3% are barely outside it.

### It is not the retry, and it is not longer games

The obvious suspect was the reserve-mask contract: a non-zero `rmask` buys a **held** payment attempt,
and a failed one re-solves unrestricted — two solves plus two snapshot/restores where there was one.
`MTG_ACT_DROP_AUDIT` now prices exactly that (`ACT_HOLD_COST`, the `g_act_pay_calls` family in
`GameLogger.h`): `hold_solo` counts the payments where this mask was `rmask`'s **sole** contributor, so
the held attempt exists *because of the lever*; `solo_retry` counts those that then failed.

    # snow d3, 100 games, seed 400000, budget 10 ms (both arms, identical games).
    # As FIRST WRITTEN this built the OFF arm by POPPING `flags`, which was correct only while the
    # default was OFF. Post-adoption that arm is ON, so both arms now pin the flag explicitly --
    # reproduced in full in the re-examination below.
    python3 - <<'PY'
    import json, copy
    base = {"deck":"decks/Snow/Snow.cod","profile":"decks/Snow/Snow.profile.json","games":100,
            "seed":400000,"depth":3,"budget_ms":10,"ignore_play_profile":True}
    for tag, on in (("on", True), ("off", False)):
        j = copy.deepcopy(base); j["name"] = "snow_d3_" + tag
        j["flags"] = {"MTG_ACT_LINE_HOLD": on}
        json.dump({"jobs":[j]}, open(f"logs/snowcost/cost_snow_{tag}.json","w"), indent=1)
    PY
    MTG_ACT_DROP_AUDIT=1 ./build/Release/mtg --batch logs/snowcost/cost_snow_off.json --threads 24
    MTG_ACT_DROP_AUDIT=1 ./build/Release/mtg --batch logs/snowcost/cost_snow_on.json  --threads 24

`solo_retry` is **337,978 of 53,282,690 payments = 0.63%**. At the OFF arm's average cost per payment
that is ~22 ms/game of the +1074 — **2% of the gap**. The retry is not the cost. Nor is the game
longer: `avg` moves 5.9000 → 5.9100, one game by one turn, the metric's quantum at 100 games.

Two further candidates were measured and killed. **Mask construction** — `ActLineHoldMask` runs on
every payment, and building the provider list calls `LookupCached` / `EffectiveProducesFor` per
permanent — was isolated with a temporary probe that computed the mask and then discarded it before the
fold; it accounts for at most a third of the gap, and that reading is itself contaminated (the probe
left the prepay rung live, so its digest is neither arm's). **The graveyard snapshot** —
`gy_snap = state.players[a].graveyard;`, a full `vector<Card>` copy taken on *every* held attempt and
provably dead weight on Snow, which has no graveyard-tapping source — was skipped by a second probe:
the digest came back identical, confirming it is unnecessary, and the time went *up* 3% (noise). The
warm `PaySnapScratch` buffer already makes that copy cheap.

### What it actually is: 33% more payments, each 13% cheaper

The same two audit runs, on identical games, say it plainly:

| | OFF | ON |
|---|---|---|
| activations that **fired** | 5,379,702 | **6,975,543** (+29.7%) |
| dropped: `tapped` + `unpaid` | 1,645,315 + 79,467 | 353,513 + 447,306 |
| **drop rate** | **24.3%** | **10.3%** |
| payment calls (`pay_calls`) | 40,013,565 | **53,282,690** (+33.2%) |
| core-ms | 148,637 | 172,518 (+16%) |
| **core-µs per payment call** | **3.71** | **3.24** |

**Per payment call the lever is 13% CHEAPER.** The entire +28% is volume: a third more payments,
because **30% more activations actually resolve** instead of being silently discarded, and each
resolving activation is itself a payment plus the extra plan-space the search can now reach with it.
So this is not overhead to be optimised away — it is the cost of executing a line the engine had been
dropping. The OFF arm was not cheaper so much as it was **quitting**: it threw away 1.65M activations
per 100 games *after* paying to enumerate and plan them.

> **SUPERSEDED — this next paragraph was the wrong answer.** It read "snow pays most because it has by
> far the largest activation volume in the suite (Scrying Sheets resolves ~70k times per game inside the
> search); selesnya and edf, whose activation volume is far lower, pay 2–3%." Volume is real but it is
> not the mechanism, and the claim fails its own arithmetic: snow has ~3x selesnya's activation volume
> and ~11x its cost ratio. The measured answer is a 3x bigger hole multiplied by **breakpoint site 8** —
> see "WHY SNOW, mechanically" below.

> **Corollary — do not expect the residual `unpaid` fix to be free.** ON's `unpaid` is *up*, 79,467 →
> 447,306. That is the open `act_generic` item below, and Snow shows why: `{S}` is not one of the five
> colours, so snow's `act_pips` are all zero, part (b) never runs, and the hold protects Scrying
> Sheets' `{T}` while the casts spend the `{1}{S}` its own cost needs — the `{T}` is paid and the mana
> half rolls back. Closing that means holding *more*, which will cost more CPU, not less. The
> opposite framing (stand down when the activation is unaffordable) would hold less, but is not the
> prize either: **447,275 of 7,934,160 holds = 5.6%** are followed by an `unpaid` drop (exact counts
> from the re-examination below), so that is the whole ceiling on standing down.

### RE-EXAMINED 2026-09-30 at the user's ask — the solver MIX is unchanged, and the ratio is WORST where snow's generation lives

> *"Added cost there is a concern, since that deck is already so darn slow."*

The section above priced the cost from the activation audit, which counts activations and payments but
says nothing about what a payment COSTS inside the solver. That left a real hole in the argument: fired
activations rose by 1.6M while payment calls rose by 13.3M — an **8.3x gap**. If those extra payments
were backtracker entries, the lever would be making the solver work harder and there would be
something to reclaim. `MTG_TAP_STATS` answers it directly (it perturbs timing, so it is used for
RATIOS ONLY; the timing below is a separate uninstrumented pool).

The counters are process-global and dumped at exit, so the two arms must be two PROCESSES (a genuine
data dependency -- one pool would merge them). **Both arms set the flag EXPLICITLY:** now that the
default is ON, an arm with no `flags` is the ON arm, so the pre-adoption `cost_snow_off.json` -- which
relied on "no flags == off" -- would silently measure ON against ON.

    mkdir -p logs/snowcost && python3 - <<'PY'
    import json, copy
    base = {"deck":"decks/Snow/Snow.cod","profile":"decks/Snow/Snow.profile.json","games":100,
            "seed":400000,"depth":3,"budget_ms":10,"ignore_play_profile":True}
    for tag, on in (("on", True), ("off", False)):
        j = copy.deepcopy(base); j["name"] = "snow_d3_" + tag
        j["flags"] = {"MTG_ACT_LINE_HOLD": on}
        json.dump({"jobs":[j]}, open(f"logs/snowcost/mech_{tag}.json","w"), indent=1)
    PY
    for a in off on; do MTG_ACT_DROP_AUDIT=1 MTG_TAP_STATS=1 ./build/Release/mtg \
        --batch logs/snowcost/mech_$a.json --threads 32 2>&1 | grep -E "ACT_|PAYMENT ENTRIES|PAY BOUND"; done

| snow d3/b10, 100 games | OFF | ON | Δ |
|---|---|---|---|
| payment solver entries (`impl`) | 46,894,866 | 62,658,255 | **+33.6%** |
| top-level solves (`once`) | 40,021,911 | 53,624,714 | +34.0% |
| solved by the cheap greedy | 33,869,033 (**84.6%**) | 45,521,833 (**84.9%**) | +34.4% |
| fell to the exponential backtracker | 6,153,185 (**15.4%**) | 8,103,196 (**15.1%**) | +31.7% |
| fail-fast bound prunes | 6,872,955 (14.7% of impl) | 9,373,390 (15.0%) | +36.4% |

**Every share is flat, and the backtracker's share falls slightly (15.4% → 15.1%).** The lever is not
making payments harder to solve; it produces a third more payments of *identical character*. That is
the mechanism behind "13% cheaper per payment", and it is the clean version of the claim the earlier
mask-construction probe could not settle (that probe left the prepay rung live, so its digest was
neither arm's — it is superseded here, not merely caveated).

**The same run bounds what could ever be reclaimed.** `ACT_HOLD_COST` on the ON arm:
`hold_mask=7,934,160` (14.89% of payments) with `hold_solo` **equal to it** — on snow this lever is
always `rmask`'s sole contributor, so every held attempt is wholly its own. Of those holds,
`solo_retry=339,849` = **4.28% of holds** (0.64% of payments) cost a second solve, and 447,275 =
**5.6% of holds** were futile (the `{T}` was protected and the activation still went `unpaid`, the
`{S}`-capacity case below). Those two buckets are the *entire* addressable waste and together they are
under 1.5% of payments. There is no overhead here to optimise away.

#### WHY SNOW, mechanically: a 3x bigger hole, multiplied by 8 through breakpoint site 8

*"Why is it so expensive on snow?"* The answer this doc gave first — **"snow has by far the largest
activation volume in the suite" — is wrong**, and it fails on its own arithmetic: snow enumerates ~4.1x
SelesnyaLifegain's activations (7,104,517 vs 1,726,729 per 100 games) but pays ~11x the cost *excess*
(+29.6% vs +2.7%, both uninstrumented at d2/b1). Running the identical audit on selesnya
(same d3/b10, 100 games, seed 400000) separates the three factors that actually multiply.

| snow vs selesnya, OFF → ON | snow | SelesnyaLifegain |
|---|---|---|
| activations ENUMERATED | 7,104,517 → 7,776,137 (**+9.45%**) | 1,726,729 → 1,725,495 (**−0.07%**) |
| ...of which `tapped` drops, OFF | 1,645,316 = **23.2%** | 137,488 = **8.0%** |
| activations that FIRE | 5,379,701 → 6,975,324 (+29.7%) | 1,340,240 → 1,417,415 (+5.8%) |
| payments (`pay_calls`) | 40,021,911 → 53,284,865 (**+33.1%**) | 4,579,244 → 4,657,409 (**+1.7%**) |
| **extra payments per extra FIRING activation** | **8.3** | **1.01** |
| holds taken, as % of payments | 14.89% | **29.07%** |
| held attempts that re-solved, as % of holds | 4.28% | **12.83%** |
| payments reaching the exponential solver | 15.4% → 15.1% | 1.7% → **5.2%** |
| backtracker invocations | 801,608 → 1,275,285 (+59.1%) | 285,837 → 346,643 (+21.3%) |
| nodes per invocation | 26.2 → 20.8 (**−20%**) | 25.5 → 31.6 (**+24%**) |
| core-ms **UNDER TAP_STATS — not a cost figure** | +21.2% | +7.2% |

(That last row is instrumented and must NOT be compared against the clean +29.6% / +2.7%: TAP_STATS
charges an atomic per backtracker node, and selesnya's nodes grew +50% while snow's grew +27%, so the
instrument inflates selesnya's arm the most. It is here only to show both arms moved in the direction
their counters predict. The real ratios are the uninstrumented paired pools below.)

**First: the lever is not more active on snow — it is LESS.** Snow holds on 14.89% of payments against
selesnya's 29.07%, and snow's held attempts re-solve on 4.28% of holds against selesnya's 12.83%. So
"the hold itself is expensive on snow" is dead: whatever snow is paying for happens *downstream* of a
firing activation, not in the hold.

**Second: snow had a 3x bigger hole to close.** 23.2% of snow's enumerated activations were being eaten
by the cast payment, against 8.0% on selesnya. That is a property of the CARD: Scrying Sheets' `{T}`
source *is a land that taps for mana* — its oracle is `{T}: Add {C}.` **plus** `{1}{S}, {T}: Look at
the top card…` — so the payer reaches for it constantly, in a manabase where every source is contested.
Selesnya's Blighted Steppe is immune by construction (its `{T}` is paid before the mana half via
`SetPermTapped` — the Botanical Conservatory / CR 602.2a ordering, so it "can never tap for mana toward
its own activation"), which left only Wirewood Lodge exposed.

**Third, and this is the 8x: a Scrying Sheets activation ARMS BREAKPOINT SITE 8, and snow reaches that
site on every consultation.** `TurnSolver.cpp` arms site 8 on exactly
`Action::Kind::ActivatePermAbility` + `AbilityMode::TapDraw` with a non-empty
`tap_draw_requires_top_supertype` — i.e. Scrying Sheets and Frost Augur, and nothing else in the suite.
The site is **unconditionally on**, outside `MTG_BP_SITES`' reach, because same-turn playability of the
found card is a correctness requirement (USER 2026-09-06, *"we need to be able to play it"*). So every
activation the lever un-drops forces a mid-apply **re-solve of the rest of the turn**, each with its own
payments — and 56 of this deck's 60 cards are snow, so the look actually finds a card ~93% of the time.

That is the whole difference, and the enumeration row proves it: **on snow the SEARCH ITSELF grows
(+9.45% activations enumerated); on selesnya it does not move at all (−0.07%)** — there, the lever only
converts drops into fires. Hence 8.3 extra payments per extra activation on snow against 1.01 on
selesnya.

Note also that the two decks pay in *opposite currencies*, which is why neither deck's reading
generalises: snow takes many more payments each 20% CHEAPER, selesnya takes barely more payments each
24% HARDER (its backtracker share triples, 1.7% → 5.2%). Selesnya's total stays small only because its
payments begin 98.3% greedy-solved; snow's begin 84.6%.

**The consequence for the cost decision.** Snow's +21–30% is not this lever's overhead — it is
**snow's own breakpoint degeneracy, fed 30% more work.** `snow-breakpoint-degeneracy.md` already
records that degeneracy (2.95M breakpoint consultations per game) as the 5.9x-units half of snow's
cost. This lever does not create the amplifier; it supplies it. So the fix that would make snow
tractable — cutting site-8 re-solve volume — would shrink this lever's snow cost by the same factor,
and no work aimed at `ActLineHoldMask` can.

#### MITIGATION OPTIONS ON SNOW, SIZED — and six of them are measured DEAD

Asked directly ("what else can we do to mitigate on snow?"), so each candidate is priced rather than
listed. `MTG_BP_PROBE` sizes the breakpoint half (per 100 games, snow d3/b10; the probe's site-8/9
smear bug is fixed — see the `kBpSites` guard in `TurnSolver.cpp` — so pre-fix numbers are void and
these were taken fresh):

| site, OFF → ON | total | searched | `untarget` | `overrun` (`ovr_dup`) | nested-unsearchable | committed-line |
|---|---|---|---|---|---|---|
| 8 `snow_look_top` (Sheets/Augur) | 3,170,743 → 3,841,469 (**+21.2%**) | 33.3% → 30.4% | 1,843,564 → 2,372,418 | 271,479 (**27,468**) → 301,869 (31,684) | 169,549 → 228,599 | 38,868 = **1.2% of total** |
| 10 `put_in_hand` (any cast) | 3,018,642 → 3,485,310 (+15.5%) | 7.8% → 6.5% | 1,874,594 → 2,266,040 | 907,375 (**177,289**) → 991,047 (188,843) | 789,257 → 921,795 | 55,510 |
| all four sites | 6,406,053 → 7,523,161 (**+17.4%**) | | | | | |

Snow re-solves the remainder of its turn **~7,700 times per turn** across sites 8 and 10, and the lever
adds 17.4% to that. Payments grow about twice as fast (+33.1%) because each searched consultation
carries its own payments.

**DEAD — measured, do not re-try:**

1. **Optimising the hold itself.** Bounded above at 1.5% of payments (4.28% of holds re-solve, 5.6% are
   futile), and snow holds *less* often than selesnya.
2. **Normalising `reserved_mask` out of the mana-cache key.** Already done, and not as a hash term:
   `ManaCacheKey` uses the mask as a per-source *eligibility* test
   (`elig = !p.tapped && !((reserved_mask >> i) & 1) && dork_elig != 0`), so the key is the EFFECTIVE
   source multiset. "Board minus Scrying Sheets" is a genuinely different payment problem, not a
   spurious key.
3. **Disabling a snow value leaf.** There isn't one — see the correction under the 3x gate below.
4. **An affordability stand-down** (hold only when the activation is payable). Ceiling 5.6% of holds.
5. **Skipping the graveyard snapshot.** Already measured free (digest unchanged, time up 3% = noise).
6. **Reclaiming the breakpoint machinery's big bucket.** `untarget` is 58% of site 8 and 62% of site 10,
   and it is **not waste**: the field's own comment says these are "real scoring positions whose
   continuation is decided greedily… **These need a searched REPLACEMENT, not deletion.**" Acting on them
   costs MORE, not less. The only *provable* waste is `ovr_dup` (second-and-later overruns of a slot,
   which re-score byte-identical EMPTY): **0.87% of site 8** and 5.9% of site 10.

**LIVE, ranked by payoff per unit of effort:**

1. **Stop opening site 8 so deep in the ROLLOUT.** Only **1.2%** of site-8 consultations are on a
   committed line — 98.8% are inside rollout evaluation. A lever already exists for exactly this shape
   (`MTG_BP_NEW_ONLY` + the mint credit, landed for snow at `d665aa34`, with its "gap 12" still open).
   This is the only candidate whose ceiling is a multiple rather than a percentage.
2. **Make wave 0's variant count at site 8 length-adaptive.** A fixed `depth*W` is being applied to a
   list whose length runs **1 → 90**, and it is wrong in BOTH directions at once: too small on **51.7%**
   of opens (so **56.6% of every continuation enumerated there is built and discarded unexplored** —
   194,111 of 343,067), and too large on 8.6% (`overrun`, whose 2nd+ ranks re-score byte-identical EMPTY).
   Bigger than the ~9% I first quoted from `overrun` alone. Search-shape lever, so it belongs under the
   heuristic-optimization skill; `MTG_BP_AXIS_W` / `MTG_BP_W` are the handles. Evidence and the length
   histogram are in `snow-breakpoint-degeneracy.md` §2026-09-30.
3. **Kill site 10's `ovr_dup`** — 177,289 consultations per 100 games that re-score byte-identical
   EMPTY. Lossless by construction and the largest single provable-waste item found. ~3% of breakpoint
   consultations.
4. **Re-size snow's suite cases.** Its d0 cases are free (0.026 ms/game), so the cost is d3/d5 seed
   count — halving snow's four overnight d3 seeds would halve its overnight share. This is a GT-coverage
   trade and therefore the USER's call, not an agent's.
5. **The documented 5.9x-units campaign** (`snow-breakpoint-degeneracy.md`). Largest payoff, largest
   effort, and it subsumes 1–3.

**What is NOT on the list: anything that takes this lever's cost off snow without touching snow's
baseline.** There is no such option — the amplifier is snow's, not the lever's.

#### But the ratio was read at the CHEAPEST cells, and snow's expensive work is not the tier

Two fresh paired pools, both arms in ONE batch on identical seeds, uninstrumented. Stride 100 exceeds
games-per-job, because `BatchRunner` shuffles with `seed + i` and consecutive per-job seeds would
otherwise replay each other's games:

    mkdir -p logs/snowcost && python3 - <<'PY'
    import json
    jobs = []
    for name, cod, prof, d, b, g in (
            ("snow_d5",     "decks/Snow/Snow.cod", "decks/Snow/Snow.profile.json", 5, 20, 40),
            ("snow_d2b1",   "decks/Snow/Snow.cod", "decks/Snow/Snow.profile.json", 2,  1, 60),
            ("seles_d2b1",  "decks/SelesnyaLifegain/SelesnyaLifegain.cod",
                            "decks/SelesnyaLifegain/SelesnyaLifegain.profile.json", 2, 1, 60)):
        for i in range(8):
            s = 500000 + 100*i
            for tag, on in (("on", True), ("off", False)):
                jobs.append({"name":f"{name}_{tag}_s{s}","deck":cod,"profile":prof,"games":g,
                             "seed":s,"depth":d,"budget_ms":b,"ignore_play_profile":True,
                             "flags":{"MTG_ACT_LINE_HOLD": on}})
    json.dump({"jobs":jobs}, open("logs/snowcost/paired.json","w"), indent=1)
    PY
    ./build/Release/mtg --batch logs/snowcost/paired.json --threads 32   # pair the `ms=` by (name, seed)


| cell | games/arm | OFF ms/game | ON ms/game | ratio | net loss-penalized turns |
|---|---|---|---|---|---|
| snow d5/b20 (`suite_cost`'s gated worst case) | 320 | 3963.0 | 4807.5 | **1.213** | **+0.00** |
| snow d2/b1 (the keep-table gen's own config) | 480 | 1446.3 | 1873.8 | **1.296** | +1.00 (one game) |
| selesnya d2/b1 (control) | 480 | 215.5 | 221.4 | 1.027 | **−2.00** |

So the honest headline is a **range, and it runs the wrong way**: the ratio *rises as depth and budget
fall* (d5 1.21, d3 ~1.32, d2/b1 1.30, d1 1.35), which means the regression tier's cells are the
CHEAPEST place the cost could have been read. The "+28%" above is a fair summary of the tier; it
understates a low-budget generation and overstates the gated d5 cell.

That matters because **snow's expensive work is not the tier — it is a keep-table generation, and that
rolls at d2/b1**, the worst cell. `decks/Snow/Snow.keepmodel.exhaustive.raw.json.journal`'s meta stamps
`depth=2, budget_ms=1, max_turns=8`.

#### What the lever did NOT cost: no banked snow work was stranded

Worth stating because it was the obvious risk. That snow journal (591,692 lines / 30 MB) is **already
un-resumable, and was before this lever existed**: resume gates on `play_digest`
(`PlayIdentityAllows`, `src/analyzer/ExhaustiveKeep.cpp`), the journal carries
`play_digest=2ba6aeecbdcdbbdb` at commit `91e2aecc` (2026-09-25), and **`6b9f0cb2` (2026-09-27) moved
32 snow GT lines** — snow's play changed two days after that journal was written. It is also not a
paused run to protect: `logs/Snow_mullgen/VALIDATION.txt` reads `GENERATION FAILED`, and the last
monitor line shows it at `frozen=0/351944 (0.0%)` after **23,102 s (6.4 h)** at 70–100 sub-rollouts/s.
So the adoption stranded nothing; the exposure is entirely to a FUTURE snow generation.

#### The trade, stated plainly: snow pays nearly all of the cost and measures none of the benefit

| deck | CPU | searched-depth quality |
|---|---|---|
| SelesnyaLifegain | +2.7% (d2/b1), +2–3% (searched) | significantly **BETTER** (111/78, p=0.020; −2.00 turns / 480 games here) |
| EldraziDisplacerFlicker | +2–3% | not significant |
| melira / stompy | neutral (noise floor) | zero divergence |
| **Snow** | **+21% (d5) … +30% (d2/b1)** | **null** (A/B 90/91; +0.00 turns / 320 games at d5; +1.00 / 480 = the quantum at d2/b1) |

That is the decision, and it is not a measurement question any more. The fix is a reachability
correction — on snow it cuts silent activation drops 24.3% → 10.3% and resolves 1.29M more Scrying
Sheets activations per 100 games — and snow's win turns simply do not care. Nothing about the +30% is
reclaimable, so the only ways to remove it from snow are to not fix snow (a per-deck gate, WITHDRAWN
above for the same reason it was withdrawn for selesnya) or to make snow cheaper outright.

**And "make snow cheaper outright" is where the leverage actually is.** At the identical d2/b1 config
snow is 1446 ms/game against selesnya's 215 — **6.7x** — and snow alone is 10.6% of the whole
regression tier (415.5 of 3,931 core-s) at 4.0x the next-most-expensive deck (fivecolour, 1033.5
ms/game). A 30% lever is a rounding error beside a 4–7x structural gap, and that gap, not this lever,
is why the keep-table generation failed at 0% frozen after 6.4 h.

The thread to pull is already documented and it is not this lever:
`snow-payment-solver-cost.md` factors snow's wall clock into **5.9x the units** (the breakpoint
degeneracy of `snow-breakpoint-degeneracy.md`) and **1.8x the cost per unit**, of which **41.5% of a
heavy Snow game is the mana payment solver** (Melira: 9.6%) — consistent with the 627k payment solves
per game measured here. That is a multiple, not a percentage.

**Sized against the run that actually matters.** `snow-generation-cost-2026-09-25.md` projects the
snow mulligan profile at **~24 h (range 20–40 h)** against an ~8 h window — already 3–5x over. At the
d2/b1 ratio measured above the lever takes that to **~31 h (range 26–52 h)**. So it does not change
the verdict on that run (it was infeasible before and is infeasible now, for reasons an order of
magnitude larger), but it is the one budget in the repo the adoption moves by more than a rounding
error, and the earlier "no budget is threatened" line was written about the regression tier only.

### No TIER budget is threatened, and the 3x gate does not move

Relative cost is the wrong unit for the decision; the budgets are absolute. **Read this as scoped to
the tiers and the gate** — it was originally written as "no budget is threatened", which the
re-examination above narrows: snow's *generation* budget does move (~24 h → ~31 h), and that is the
one place the adoption costs more than a rounding error.

* **Snow is the most expensive deck in the suite** — `suite_cost.json` 4178.8 ms/game, 415.5 core-s
  for its whole regression tier. +28% takes it to ~5,350 ms/game and ~532 core-s.
* **The whole regression tier is 3,931 core-s** across 27 decks = **2.7 min wall on 24 cores**, against
  a **< 45 min** budget. The lever adds ~116 core-s, i.e. ~2 core-minutes. The tier goes 2.7 → 2.8 min.
* **The 3x new-deck gate does not move.** `suite_gate.py --report` names the reference as the most
  expensive deck holding **both** a value leaf and a mulligan profile — that is **fivecolour** at
  1033.46 ms/game (budget 3100.38), *not* snow. (**Correction 2026-09-30:** this said snow "ships a leaf
  but no mulligan profile". Snow ships **neither** — `decks/Snow/Snow.value.json` carries only
  `value_play.mull_gen_depth`/`mull_gen_budget_ms` and **no `eval_model`**, with `target_depth`
  deliberately absent so `value_play.present()/drives()` stay false; its own note records it verified
  play-inert at "smoke 93 passed / 0 configs changed". So there is no snow value leaf to disable as a
  mitigation, and the gate conclusion is unchanged either way.) So snow's
  rise cannot tighten the gate on any future deck. selesnya, the deck the gate was last run for, goes
  1843.6 → ~1892 ms/game: 1.78x → 1.83x of fivecolour, still a comfortable PASS.

So performance changed notably in ratio on exactly one deck, and immaterially in every unit that any
budget is written in.

---

## ADOPTED 2026-09-30 — default flipped ON, all three tiers rebaselined

`ActLineHoldEnabled()` is now `EnvOn("MTG_ACT_LINE_HOLD", true)` (`src/core/SpellEffects.h`), with the
heurarm slot's table entry updated to match. Gates on the adopted binary: **359 unit tests /
2,641,189 assertions, 118/118 scenarios.**

**The tiers reproduce the held-out A/B independently.** Net loss-penalized turns, from each tier's
`FAIL` lines weighted by that case's game count (`avg` already scores an unwon game as `max_turns+1`,
so `(got − exp) × games` *is* net turns):

| tier | configs moved | searched | d0 |
|---|---|---|---|
| smoke | 6 | **−1.00** turns / 375 games | +8.00 / 2,000 |
| regression | 11 | **−4.00** turns / 930 games | +21.00 / 2,000 |
| overnight | 24 | +7.00 turns / 8,350 games | +94.00 / 9,000 |
| **pooled** | | **+2.00 turns / 9,655 games (+0.0002/game)** | **+123 turns / 13,000 (+0.0095/game)** |

So searched depth is **2 games in 9,655** — the quantum again, from the other direction. And it
decomposes exactly as predicted: in the overnight tier `selesnya` is net **0.00 over 6,000 searched
games**, `critter` 0.00, `melira` 0.00, and the whole +7.00 is `snow` over 1,000 — whose searched sign
**flips across the three tiers (+2 / −3 / +7)**. d0, by contrast, is the same sign and the same
magnitude in all three tiers and on both decks, matching the A/B's snow +0.0135 / selesnya +0.0065.

**Two claims made before the tiers ran were wrong and are corrected here.** "melira and stompy keys do
not move at all" was true of the A/B's seeds and false of the suite's: `stompy_regression_d3_s3003`
changes **one game of 300** (gi145, score unchanged at T4), and `melira_overnight_d3_s6006`,
`melira_overnight_d5_s6006` and `critter_overnight_d3_s4004` all move their digest at an **identical
avg**. And "snow is worse at searched depth" was a smoke-tier reading; the regression tier has snow
*better* on both its searched seeds (d3_s3003 6.0167 → 5.9833, d5_s3003 6.1333 → 6.1000).

**The reference corpus survives, which was the real risk of not standing down under
`HumanPlayActive()`.** `viewer_protocol_check.py --strict`, 355 refs: **0 play-drift, 0 enum-gap,
0 contract-fail** — the three classes that gate. The 3 `board-diverged` and 10 `mull-drift` do not gate
(`viewer_protocol_check.py`'s exit is `STRICT and (counts["play"] or counts["unresolvable"])`), and the
`board-diverged` three are **pre-existing**: re-running the identical strict check with
`MTG_ACT_LINE_HOLD=0` returns byte-identical counts (35 ok, 307 repaired, 3 board-diverged, 10
mull-drift).

**`=0` reverts EXACTLY.** On the same 100 games used for the cost work (snow d3 b10, seed 400000),
recorded before the flip and re-run after it:

    MTG_ACT_LINE_HOLD=0  ->  avg 5.9000  digest 1f9935c72324a70c   (the pre-flip OFF arm, byte-identical)
    (unset, now default) ->  avg 5.9100  digest 385c5169741d3dcc   (the ON arm)

So backing the adoption out is one env var plus a GT revert; no code path is stranded.

**GT integrity after three accepts.** The one-line-note rule matters here — a multi-line `ACCEPT_ACK`
injects raw text into `regression_gt.txt` and each later mode's accept then drops the earlier mode's
keys. Verified: **610 keys before and after**, 3 notes (one per mode), **0** non-key non-comment lines,
and `python3 test/check_gt_logs.py` → `610 consistent, STALE: 0, missing: 0`. Each note carries the
still-binding 2026-09-29 WhiteKnights list provenance, which a full accept would otherwise filter out.

---

## Open — and the route, with the earlier recommendation WITHDRAWN

> **WITHDRAWN (2026-09-30): "gate the hold on the archetype provider."** It was recommended on the
> strength of `snow +0.00709 at searched depth`, which decodes to **7 discordant games in 1,000** at
> the metric's quantum, against 5 that went the other way (sign p = 0.14). There is no measured
> searched-depth cost to gate away. A provider gate would have suppressed a correct, engine-wide
> reachability fix on every other deck in order to keep a **different component's** miscalibration —
> the greedy projection's valuation of a trailing activation — out of one tier's ground truth, and
> would have left that miscalibration undiagnosed and unrecorded. It is also the wrong shape on its
> own terms: the `heuristic-optimization` skill's "adopt in the archetype provider" rule is about
> *judgement heuristics*, and its own Rule 0 excludes exactly this case — "if the engine models a card
> or rule wrong, that is a bug to fix against the MTG Rules skill, not a heuristic to tune."

**The route instead, in dependency order:**

1. **DONE — the two defects in the fix itself are fixed and verified**: the `- cur[c]` arithmetic and
   the `UntapCreature` audit blind spot (both above). Inert with the flag off, checked per game against
   committed GT at d0 and cross-binary at d5. Gates green on the fixed binary: **359 unit tests /
   2,641,189 assertions, 118/118 scenarios.**
2. **DONE — the fixed lever is priced at every searched depth with real power** (the ladder above).
   Not one searched cell on any deck is significantly worse; `selesnya` pooled over its four searched
   depths is significantly **better** (p = 0.020); `melira` and `stompy` do not diverge on a single
   game. **There is nothing at searched depth to trade away, so the adoption question is no longer a
   trade-off** — it is a GT rebaseline decision. Adopting globally moves `selesnya` and `snow` d0 keys
   (and `selesnya`'s searched keys, which this deck's onboarding owes anyway); `melira` and `stompy`
   keys do not move at all.
3. **OPEN, and now the actual defect: the greedy (d0) projection over-values a trailing activation**
   — Scrying Sheets' look, Blighted Steppe's life-for-a-land, Wellwisher. It is deck-independent,
   pre-existing, and was only ever invisible because the payment hole deleted the plans that exposed
   it. The ladder localises it precisely: it is a **leaf-valuation** error, not a payment one, because
   it disappears at d1. Fixing it helps every deck at every depth; suppressing the reachability fix
   helps no deck at any depth. Note the standing user ruling that greedy is "too unreliable" already
   points the same way.
4. **DONE — the CPU cost is priced and attributed** ("THE CPU COST" above). +28% on snow, +2–3% on
   selesnya and edf, neutral on melira and stompy. It is **volume, not overhead**: 33% more payment
   calls each 13% cheaper, because 30% more activations resolve. No tier budget is threatened (the
   regression tier goes 2.7 → 2.8 min wall against a 45-min ceiling) and the 3x new-deck gate does not
   move (its reference is fivecolour, not snow).
5. **Remaining judgement call for the user, and it is now a small one:** whether a **d0-only** win-turn
   regression on two decks, plus **+28% CPU on snow**, is worth accepting to make a legal line
   reachable at every depth. The honest framing on each half: d0 is the configuration with no search at
   all and its number comes from the component the user has already ruled unreliable; and the snow CPU
   is not waste but the price of executing ~1.6M activations per 100 games that were previously
   enumerated, planned, and then thrown away.

Remaining items whichever way that goes:

1. **Disclosure for the genuinely-unpayable case.** Extend `drops` to activations, or colour-check
   the activation at enumeration. Today the menu advertises a plan whose activation will not happen,
   and `Plan::would_drop` structurally cannot cover it. GT-neutral (`would_drop` is carried, not
   acted on) so this is adoptable on its own.
2. **Partial `reserved_a` rungs.** The rung is **all-or-nothing** over every activation source: on a
   Snow board with four Sheets the turn cannot spare all four, the rung fails, and the ladder falls
   straight back to holding nothing — the same "partial save thrown away" defect the
   depletion/creature ladder already learned. Per-source rungs would keep one activation live instead
   of none, and might turn Snow's win-turn result around. Note it would *raise* Snow's CPU further, for
   the reason "THE CPU COST" gives: on this deck cost tracks the number of activations that resolve.
3. **EldraziDisplacerFlicker** — no longer wholly unmeasured. It has no GT (not a suite deck), but an
   A/B needs no GT, only two arms, so it was pooled into the runs above. At **d0, 40,000 games per
   arm**, the fixed lever is a **null, marginally better**: 340 better / 310 worse, −0.00075/game,
   p = 0.255. That is worth noting on its own — EDF has the **highest measured drop rate (43.5%)** and
   is the one deck of the three whose activation cost is *colourless* (`{1}{C}`), i.e. the one the
   `- cur[c]` defect could not bite. Its searched depths are in the same pooled run as item 2.
   Beware its cost profile when sizing: EDF d5 games have a heavy tail (individual games up to 101 s,
   `SLOW-GAME` in the batch log), and a 24-game probe under-priced the cell **3.6x**.
4. **Generic-capacity holding** — deliberately not attempted (residual cause 1 above): it means
   casting fewer spells, which is the search's call and not the payer's.
5. **An affordability STAND-DOWN, which is not the same as holding generic.** Every residual `unpaid`
   is a hold that was taken and then bought nothing: the `{T}` source (and now its coloured pips) were
   kept back from the casts, and the activation still did not fire. That is a pure loss — a worse tap
   assignment for no line. With the arithmetic fixed, `selesnya`'s residual is 32,945 such cases per
   40-game d3 harness. The lossless retry catches the subset where the *held attempt itself* fails; it
   cannot catch the case where the held attempt succeeds and the activation is nevertheless unpayable
   (Blighted Steppe's 3 generic). The fix is to **return 0 from the mask** when the activation's full
   cost cannot be met from what the hold reserves plus surviving float — stand down entirely rather
   than hold a partial. Needs `act_generic` added to `PlanTraits` (a plain struct, not a persisted
   index, so appending is free) and is careful around scaled dorks, whose held source may yield more
   than one. Not attempted yet: with the d0 regression now attributed to the greedy projection rather
   than to the hold, this is a smaller effect than it looked, and it should be measured on its own arm.

---

## A SIBLING defect found in the same sweep, also engine-wide, also open

Recorded here so it is findable outside a per-deck ledger; the full repro and the ruled-out levers are
in [`analysis-SelesnyaLifegain.md`](analysis-SelesnyaLifegain.md) under "a FALSE `drops` label".

**The rollout's payer under-realises a line whose payment depends on a scaled dork GROWING mid-turn.**
Priest of Titania's `mana_per_creature_count_all` counts Elves on the battlefield, so on a board of
{Blossoming Sands, Brushland, Priest of Titania} the ordering `[cast Priest #2, cast Feed the Clan]`
is payable — pay Priest #2 off the two LANDS, and once the second Elf has entered Priest #1 taps for
**2** instead of 1. The executor finds that allocation (verified: four mana realised from three
sources, both casts made). `ApplyPlanDirect`'s `TapForCostDirect` does not, and drops the second cast.

Same family as the hole above — **a payer that cannot see what the rest of the plan needs** — but a
different axis: there it is a trailing activation's cost, here it is the plan's own effect on supply.
Two consequences:

* the visible symptom is a **false `drops` label** on the plan menu (GT-neutral: `would_drop` is
  carried, not acted on);
* the real cost is that **the search never scores the line at all**, because the rollout is the
  search. `fd-diverge` cannot catch it: it compares realised vs predicted win turn, and this error is
  conservative. It is not a systematic rate divergence either — `MTG_AFFORD_AUDIT` gives rollout 2.33%
  vs real 2.30% on SelesnyaLifegain and 2.02% vs 2.11% on Snow — it is this specific allocation.

Ruled out: `MTG_PAY_BOUND=0`, `MTG_NO_BATCH_PAY=1`, `MTG_SCALER_PLAN_BIAS` both ways,
`MTG_DORK_TAP_LAST=1`. The remaining difference between the two payers is the accounting pool
(`available != nullptr` in the executor, `nullptr` in the rollout, which also flips
`honor_legacy_cco`). Fixing it is a rollout payment change and therefore GT-moving on every deck with
a scaled dork — SelesnyaLifegain, StompySurprise, Goblins.

---

## 2026-09-30 (later): the residual is NOT one thing — it splits into TWO defects, and the bigger one is NEW

USER: *"Any case where we are silently dropping things is an indicator of problems that should be
flagged going forward."* This section is that flag. On snow d3/b10 (2 games, seed 400000), with the
adopted lever ON, **15.7% of all enumerated activations are still silently dropped**:

    MTG_ACT_DROP_AUDIT, snow d3/b10, 2 games, single-threaded
                        hold OFF      hold ON       what moved
    tapped             133,602        44,439        -66.7%   (case A: the {T} was spent for MANA)
    unpaid               2,698        25,458        +844%    (case B: {T} paid, mana half failed)
    total drops        136,300        69,897        -48.7%
    fired              344,967       374,492        +8.6%
    drop rate            28.3%         15.7%

So the adoption is confirmed as a net win on its own terms — **half the drops, 8.6% more activations
resolve** — but it plainly CONVERTS case A into case B, which is what the "residual `unpaid`" section
above recorded without sizing. Sized and split, it is two independent defects with different fixes.

### DEFECT 1 (NEW, and it is the whole `tapped` class): a breakpoint continuation DOUBLE-BOOKS the base plan's activation source

**One control settles it.** `MTG_BP_SEARCH=0` (wave width W=0, so no breakpoint variant ever applies a
searched continuation), everything else identical:

    base d3/b10           tapped = 44,439   unpaid = 25,458   fired = 374,492
    MTG_BP_SEARCH=0       tapped =      1   unpaid = 20,786   fired = 353,959

`tapped` goes to **one**. So essentially 100% of the remaining case-A drops are caused by breakpoint
**searched continuations**, not by the base plan's own casts — which is the part the adopted lever was
built for and does close.

**MECHANISM — and my first diagnosis of it was WRONG, so both readings are recorded.**

*The refuted one.* All three of the sites that install a continuation's own `PlanTraitsScope`
(`ComputePlanTraits(state, extra.actions)` in the rollout's deferred-continuation apply and the
executor's `resolve_draw_breakpoint` + record replay) do shadow the base plan's traits, which would
hide the pending activations from `ActLineHoldMask`. That reading is plausible and it is not what is
happening here: **breakpoint site 8's continuation installs NO trait scope at all**, so the outer
plan's traits are still live while it runs, and the hold already covers its pending activation
sources. Built the carry-over anyway as `MTG_ACT_HOLD_OUTER` (all three sites, `act_src_nums` only)
and measured it: `tapped` 44,439 -> **44,435**. Four drops. The lever is kept because it closes a real
hole at the *other* two sites for decks that reach them, but it is not this one.

*The measured one.* An exact join settles it. Under the audit, each plan apply records which sources
its own breakpoint continuation **activated**, and the drop site tests membership:

    tapped_why   by_cont_ACTIVATION = 23,974    other = 20,461      (54.0% double-booking)

So more than half the class is a **DOUBLE-BOOKING of one physical permanent**. The continuation is a
fresh Solve over the board as it stands *mid-trailing-pass*, where the base plan's un-fired activation
sources are still UNTAPPED — so `CollectActions`' only gate on them
(`if (taps && (src.tapped || !src.CanTap())) continue;`) passes, and the continuation cheerfully
enumerates and applies an activation of a Scrying Sheets the base plan is still going to activate.
One permanent cannot tap twice (CR 602.2a), so when the outer trailing pass reaches its own
activation of that source it finds it tapped and no-ops silently.

The remaining 46% is the continuation's **cast payments** taking the source. There the hold IS live and
either succeeds or legitimately falls back to the unrestricted solve — *"never lose a cast"* — so that
half is correct-by-design residual, not a defect.

**WHY THE DOUBLE-BOOKING HALF IS THE GOOD KIND OF FIX: it is lossless AND it removes work.** The
candidate it deletes is one whose activation provably cannot execute, so the line it scores is
identical to the same line without that activation — a duplicate the enumerator manufactured. That is
exactly the shape the user pointed at (*"Your other unpayable idea seems to be a much bigger lever"*),
now located precisely: the test is not affordability, it is a physical `{T}` already owed. Excluding it
at the continuation's ENUMERATION removes the copy + apply + rollout, not just the rollback.

The exclusion is a necessary condition and needs no new snapshot: `CurrentPlanTraits()->act_src_nums`
already *is* the list of sources the enclosing plan owes a `{T}` to, and it is self-limiting the same
way the mask is (a fired activation owns a tapped source, so filtering on untapped leaves exactly the
pending ones). It is also naturally scoped — the traits scope is installed by the APPLY, so a
continuation's Solve sees it while the next simulated turn's Solve (outside the apply) does not.
**Caveat that must be checked before building it:** whether site 8's continuation candidates are
enumerated during the apply (where the scope is live) or earlier, at parent plan-enumeration time by
`EnumerateBreakpointPlans` (where it is not). If the latter, the filter belongs at that call site
instead. NOT YET BUILT.

Both worlds reach this identically, which is why it never produced an `fd-diverge`: the executor and
the rollout drop the same activation, so lockstep is intact and the better line is simply absent from
both. Same signature as the original hole.

### DEFECT 2 (sized, was known as "cause 1"): a `{S}` or generic activation cost holds NO MANA AT ALL

Split by card, the `unpaid` class is **89% one card**:

    Scrying Sheets  22,655   (mv=2)
    Frost Augur      2,800   (mv=1)
    Rimefeather Owl      3

That is not a coincidence, it is the representation. `CardDatabase` parses `{S}` as
`++cost.generic; ++cost.snow_pips` (Card.h:228) — **`{S}` is not a colour**. So Scrying Sheets'
`tap_draw_cost = {1}{S}` is `generic=2, snow_pips=1` with all five coloured pips ZERO, and Frost Augur's
`{S}` is `generic=1, snow_pips=1`, likewise. In `ActLineHoldMask`:

```cpp
int need[5]; bool any_pip = false;
for (int c = 0; c < 5; ++c) { need[c] = pt->act_pips[c] - float_left; if (need[c] > 0) any_pip = true; }
std::uint64_t mask = 0;
/* (a) the {T} sources ... */
if (!any_pip) { return mask; }      // <-- EVERY snow tap-draw returns HERE
/* (b) the coloured pips */
```

`act_pips` is indexed `[W,U,B,R,G]` only, so for every snow tap-draw `any_pip` is false and part (b)
never runs: the lever holds the **`{T}` source** and holds **zero mana**. The casts then legitimately
spend the pool, the `{T}` half is paid, the mana half fails, and the tap rolls back. Which is precisely
the doc's own description of the `- cur[c]` bug's worst state — *"enough hold to spoil the casts' tap
assignment, not enough to make the activation payable"* — reached by a different route, and one the
`- cur[c]` fix could not have touched because the colour loop is skipped entirely.

The fix is an `act_generic` term (plus an `act_snow_pips` term, since `{S}` needs a *snow* source), held
the way `act_c_pips` → `LineColorlessHoldMask` already holds a blink loop's `{C}`. The precedent exists.

**But it is a REAL TRADE, exactly as "cause 1" said, and the new measurement sharpens why:** holding
generic capacity means casting fewer spells. And on snow specifically it points the wrong way for cost —
every activation that newly resolves opens breakpoint site 8, so closing this hole makes snow **slower**
while making it play better. That is the opposite of what the user asked for on this deck. So it is
staged and flagged, not adopted, and it must be measured on a deck where site 8 does not multiply it.

### What the ENUMERATION side is NOT

Worth recording because it was the first hypothesis and it is wrong: the subset bill does **not**
double-count the activation's own `{T}` mana source. `MTG_TAP_ABILITY_DEBIT` is already **default ON**
(`TurnSolver.cpp:2625`, *"this is a correctness fix, not a heuristic"*), and `PermAbilityTapDebitOf`
already subtracts the tapped source's own yield from the subset pool in **both** walkers
(`Solve::consider` and `EnumeratePlans::eval_and_push`). `SubsetPayable`'s colour gate also includes
activation pips (it skips only `ActivateVial`). So the enumerator's necessary condition is sound and the
gap is entirely on the sequential-payment side, which is where both defects above live.

### Also flagged, and NOT acted on (it would move GT on a deck that ships)

* **`PlanTraits::kMaxActSrcs = 8` is exactly snow's activation-source count** (4 Scrying Sheets + 4
  Frost Augur). A plan carrying a 9th `{T}` activation silently drops out of the hold with no
  disclosure — the overflow is a bare `t.act_src_count < PlanTraits::kMaxActSrcs` test. Not currently
  reachable on any shipped deck, so it is a latent cliff rather than a live bug; it wants an assert or a
  counter rather than a bigger array.
* **Skred is not modelled as a snow card.** `cards.json` gives it no `supertypes`, and the local
  `scryfall_reference.json` says `"type_line": "Instant"`. The real card is a **Snow Instant**. Because
  `tap_draw_requires_top_supertype = Snow`, Scrying Sheets and Frost Augur can never find a Skred — so
  the deck's own removal spell is invisible to its two dig engines. This is a card-DATA fix, it changes
  play and therefore GT on a shipped deck, and it is the user's call.

### THE DOUBLE-BOOKING FIX: design, and the ONE hazard that stopped it being built tonight

Where the filter goes is settled: `bp_searched_plan` calls
`TurnSolver::EnumerateBreakpointPlansRef(state, is_pre_combat)` **from inside the apply**, and site 8
installs no trait scope, so `CurrentPlanTraits()` there is the ENCLOSING plan's traits. That means
`act_src_nums` — already exactly "sources this plan owes a `{T}` to" — is available at the
continuation's enumeration with no new plumbing, and it is self-limiting in the same way
`ActLineHoldMask` part (a) is: filter on *untapped*, and a fired activation drops out automatically.
It is also naturally scoped, because the traits scope belongs to the APPLY: a continuation's Solve
sees it, the next simulated turn's Solve (outside the apply) does not.

**THE HAZARD, and it is the reason this is deferred rather than shipped:
`EnumerateBreakpointPlansRef` is MEMOIZED ON STATE.** The filter depends on the *enclosing plan*, not
on the state, so two different enclosing plans that reach the same board with different pending
activations would share one memo entry and get each other's filtered list. That is a silent play
corruption of exactly the kind this repo has been bitten by before, and it cannot be papered over:
the owed-source set has to become part of the memo key (cheap — it is at most 8 small ints, and
`ManaCacheKey` already has the precedent of folding a reserve set into a key as an *eligibility*
term), or the filtered path must bypass the memo. Either is a deliberate change to a hot memo and
wants its own measurement, not a late-night patch.

Two further requirements before it can be adopted:
* **The executor twin.** `AIEngine`'s `resolve_draw_breakpoint` re-derives the same continuation. If
  the filter applies in one world and not the other, the candidate lists differ and `bp_at` indices
  shift — the failure mode the shared-body refactor of the site-8 gate was done to prevent.
* **Prove it removes candidates rather than shifting them.** `MTG_BP_CONDEMN_ACTIVATION` is the
  cautionary precedent measured the same day: it dropped 3,342 candidates and work went **UP** 0.35%,
  because the freed wave slot (W=2) is immediately refilled by the next-ranked variant. A
  double-booking exclusion is a better bet — it removes a candidate whose line is a *duplicate*, so
  the refill is a genuinely different line rather than the same one — but that is a prediction, and
  the counter to check is `cand_scored`, not the drop count.

**Size to expect:** 23,974 of 44,439 `tapped` drops on 2 snow d3/b10 games (54%), i.e. ~12k per game
of candidates whose activation cannot execute. Against the ~10% of candidate mass §G measured as
stranded, this is the recoverable slice with a *lossless* argument behind it (one permanent, one tap,
CR 602.2a), which the affordability framing never had.

#### RESOLVED 2026-09-30 (overnight): where the double-booking filter goes, and why the memo hazard dissolves

The blocker recorded above ("`EnumerateBreakpointPlansRef` is memoized on state") is answerable, and
reading the executor settles all three open questions at once.

**1. BOTH worlds take the list from the SAME memo, so lockstep is automatic if the filter lives there.**
The rollout calls `TurnSolver::EnumerateBreakpointPlansRef(state, is_pre_combat)`; the executor's
plan-carried branch calls `TurnSolver::EnumerateBreakpointPlans(state, is_pre_combat_main)` — and both
resolve through `BpEnumEntryFor`. The executor's own comment says so: *"EnumerateBreakpointPlans is the
SHARED list (fan-out suppressed) ApplyPlanDirect indexed."* So filtering **inside the derivation**
cannot desynchronise `bp_choice`, because there is only one list. Filtering at either call site
individually would.

**2. The memo key has an established pattern for exactly this, and the new fold is four lines.**
`BpEnumBuildKey` already folds conditional thread-locals the answer depends on — `g_cantrip_order_site`
(*"the continuation list depends on the bound site, so the same state under different sites must not
share an entry"*) and `g_bp_hand_before`. The owed-source set is the same kind of term and joins them
the same way. It also already folds the WHOLE `heurarm::t_arm` array under the stated rule *"a lever
that changes a memoised answer must be hashed into the key"* — so routing the lever through a heurarm
slot covers the arm dimension for free, and only the per-plan owed-set needs adding.

**3. The thread-local leak that would have made `CurrentPlanTraits()` alone WRONG is avoided by
binding inside the derivation.** This is the part worth keeping, because the naive version is a real
bug: reading `CurrentPlanTraits()` directly in `CollectActions` would filter *every* enumeration
occurring inside any apply — including a FUTURE simulated turn's Solve inside a rollout launched from
`resolve_draw_breakpoint` (the executor's unsearched branch runs a full `SolveWithLookahead` there).
On that future turn the Scrying Sheets is untapped again and owes nothing, so the filter would delete
reachable lines on a turn it has no business touching. Binding the owed-set for the duration of
`BpEnumEntryFor`'s derivation only is safe because that derivation is **pure enumeration** —
`EnumeratePlansWithLand` runs no rollouts — so nothing nested can inherit it.

**Verification route, already provided by the code:** `MTG_NO_BP_ENUM_CACHE=1` disables storage with a
shipped identity contract (*"results must be identical either way"*, verified at 200 games seed 710000,
digest `7c54eb23778c9c31` both ways, 0/200 games moved, no-cache side 2.05x slower so the comparison
has power). Running the filtered lever with and without the cache is therefore a direct test of whether
the new key fold is COMPLETE — a digest difference means the owed-set is not fully keyed.

So the remaining work is mechanical and bounded: bind + fold + filter, prove inertness with the lever
off (smoke digests), prove key completeness with `MTG_NO_BP_ENUM_CACHE`, then read `cand_scored` (NOT
the drop count — the `MTG_BP_CONDEMN_ACTIVATION` precedent) and the held-out quality on snow + edf.

---

## 2026-09-30 (census): DEFECT 2 RE-DIAGNOSED AND GENERALISED — it is not the `{S}` blind spot, it is "generic is never held"

The per-decision work census (`docs/design/per-decision-work-census.md`) was run on
EldraziDisplacerFlicker as a control for snow, and the control turned out to have the defect
**worse**. Chasing why corrected the diagnosis, which had been too narrow.

### What I had said, and why it was wrong

I had recorded Defect 2 as the `{S}`-is-not-a-colour blind spot: `CardDatabase.cpp:470` parses `{S}`
as `++cost.generic; ++cost.snow_pips`, so Scrying Sheets' `{1}{S}` has all five coloured pips zero,
`ActLineHoldMask`'s `int need[5]; bool any_pip` loop finds nothing, and `if (!any_pip) return mask;`
returns after part (a) holding the `{T}` source and no mana. That is all true, and it is not the
defect — it is one **symptom** of a more general one.

EDF's drops are 32.2% of enumerated activations (vs snow's 12.6%), and its single largest drop
class is **Shivan Gorge at 3,072 drops**. Shivan Gorge's cost, read from `cards.json` rather than
recalled, is **`{2}{R}`** — which *does* carry a coloured pip, so `any_pip` is true, part (b) runs,
and the early return never fires. The `{S}` story cannot explain the biggest class on the deck.

### The actual defect

`ActLineHoldMask` has exactly two parts: **(a)** hold the `{T}` sources, **(b)** hold providers for
the **coloured** pips (`act_pips[W,U,B,R,G]`). There is no part for generic. Colorless is handled,
but by a *different* mask OR'd in at the payment site — `LineColorlessHoldMask`, gated on
`pt->act_c_pips`. So the coverage table is:

| pip kind in an activation cost | reserved? | by what |
|---|---|---|
| coloured W/U/B/R/G | **yes** | `ActLineHoldMask` part (b), via `act_pips[]` |
| colorless `{C}` | **yes** | `LineColorlessHoldMask`, via `act_c_pips` |
| **generic** (`{2}`, `{4}`, `{5}`, and the generic half of `{1}{S}`) | **NO** | **nothing** |
| the `{S}` snow *constraint* | **NO** | nothing (`{S}` parses to generic + `snow_pips`) |

> **`ActLineHoldMask` never reserves generic mana. Every activation cost with a generic component
> can have that component spent out from under it by the cast half of the same turn.**

That one sentence explains **every** sampled drop class on both decks, which the `{S}` story did
not:

| card | cost (from `cards.json`) | what is held | what is exposed | drops (1 EDF game) |
|---|---|---|---|---|
| Shivan Gorge | `{2}{R}` | `{T}` + a red source | the **2 generic** | 3,072 |
| Kitchen | `{4}` | `{T}` only | all 4 | 915 |
| Clue Token | `{2}` | `{T}` only | both | 1,098 |
| Mariposa Military Base | `{5}` | `{T}` only | all 5 | 65 |
| Essence Depleter / Dimensional Infiltrator | `{1}{C}` | `{T}` + `{C}` via the colorless mask | the **1 generic** | 155 |
| Scrying Sheets | `{1}{S}` | `{T}` only (early return) | everything | snow's 89% class |
| Frost Augur | `{S}` | `{T}` only (early return) | everything | — |

### Why this is the state the code itself warns about

`ActLineHoldMask`'s own comment, written when the `- cur[c]` bug was found, states the hazard
exactly:

> *"That is the worst of the three states: enough hold to spoil the casts' tap assignment, not
> enough to make the activation payable. The `{T}` then gets paid and the mana half rolls back --
> case B, MANUFACTURED by the fix meant to close case A."*

For **`{2}{R}`** that is precisely the live, shipped, default-ON configuration: the red source is
held (spoiling the casts' assignment) while the two generic are not (so the activation is still
unpayable). Shivan Gorge's 3,072 drops are the documented worst state, reached by design rather
than by a bug.

**This is not a claim that `MTG_ACT_LINE_HOLD` should be reverted.** Its adoption measurement stands
(total drops −48.7%, activations fired +8.6%) — on balance it wins. The finding is narrower and
more useful: the **residual `unpaid` class is now fully explained**, it is not snow-specific, and the
completion of the lever has a deck-general motivation it did not have before.

### What this changes about the planned fix

The fix I had specified — add `act_generic` (+ `act_snow_pips`) to `PlanTraits` and a part (c) that
reserves generic breadth — is unchanged in shape but **much better motivated**:

* It is **not a snow special case.** Two of the three exposed classes on EDF have no snow card in
  them, and EDF is a shipped suite deck.
* **EDF is the measurement deck I said this needed.** My earlier note was that the generic hold
  "points the WRONG way for snow cost, since every newly-resolving activation opens site 8, and it
  must be measured on a deck where site 8 does not multiply it." EDF is exactly that deck: its
  `bp_condemn_seen` is **0** across 3,758 decisions, so the breakpoint machinery is not amplifying
  anything there. Measure on EDF first, then read snow for the site-8 interaction separately.
* The `- cur[c]` lesson applies directly and is the main design risk: a generic hold that reserves
  *some* of the generic is the same worst-of-three state one rung along. It must reserve the whole
  generic requirement or decline to hold at all.

**Still not built.** Status unchanged: specified, now with a control deck and a complete diagnosis.
