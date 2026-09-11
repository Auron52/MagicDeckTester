# COMBO OFF replay hunt — the phase-1 question at TRUE fidelity

Companion to `docs/design/combo-off-sweep-catalogue.md`. That sweep asked 1,037 states through
`--scenario` fixtures and closed with §6, "Fidelity limits of this sweep": **no floating mana, a
synthetic library order, an invisible exile zone, no energy.** Every one of those is a property of
the *reconstruction*, not of the engine. This hunt re-asks the same question through the stateless
`--claude-play` replay protocol, which has none of them — it replays the real deterministic game,
so the pool, the shuffle, the exile zone and the energy counters are whatever they really were.

Harness: `bash test/combo_off_replay_hunt.sh` (`--quick` = 4 references + 12 seeds + horizon 6,
**9 seconds**). Results: `logs/combo_off_hunt/results.json`, one record per state under a stable
content-addressed id. Analysis without re-running: `--report <results.json>`; second passes:
`--retrace`, `--float-offer`, `--finalise`.

**Base commit: `d8f700bb`** (the phase-1 sweep commit), `build/Release/mtg`.

> **READ THIS BEFORE FIXING ANYTHING.** The Combo Off executor has moved since `d8f700bb` —
> `d671f568` (draw-guard self-tap + rollback-unless-win), `93ad0585` (draw sink real trial),
> `d92c5fc2` (outlet switch, Gorge slot, sink trial, land-aura UB), `18c404d6` (bank counts the
> floating pool). Several of those land squarely on C1a/C1b/C2, so **some class-b states in this
> catalogue may already be fixed.** This run was NOT repeated on the newer tip; the honest way to
> use it is to re-run `bash test/combo_off_replay_hunt.sh` on the current tip and diff
> `results.json` on the stable state ids, which is exactly what the ids exist for.

---

## 1. What was probed

| population | games | main-phase frames | how the states were reached |
|---|---:|---:|---|
| `reference` | 11 / 11 | **541** | every main-phase frame of the user's own saved games, replayed BY INTENT through `viewer_protocol_check.check_reference`'s own content-anchored walk |
| `autonomous` (driven) | 59 / 60 | **5,632** | seeds 13..72 at `--game-index seed-1`, driven forward one decision at a time to a horizon of 8 |
| **total** | **70 / 71 jobs** | **6,173** | |

**Cut short, stated plainly:** seed 39 (`--game-index 38`) is missing. Three replays in the full
run each burned **>80 minutes of CPU inside a single `--claude-play` step** (§7 HANG-1..3); they
were killed, and the run was finalised from its own 70-of-71 partial snapshot rather than restarted.
Everything below is 70 completed jobs. Nothing is extrapolated to the missing one.

**The fidelity that was actually bought.** Of the 6,173 frames, **3,525 (57.1%) carry a non-empty
floating mana pool**, 903 of those were offered a Combo Off, and **218 of the 295 executor failures
happened on a board with mana floating**. A further 349 frames carry non-zero energy (a live Aether
Hub). Both are states `--scenario` cannot stage at all, so more than half of this population is
outside the phase-1 sweep by construction — which is the point of the exercise.

### What the driven population is, and is NOT

`WriteDecisionJson`'s **main_phase frame carries no `heuristic_default` and no `ai_choice`** (those
keys exist only on the auxiliary frames — mulligan, bottom, vial_charge, free_cast, target), `-1`
in `--choices` means PASS, and there is no `--auto` flag. Verified in `src/main.cpp`, not assumed.
So **"answer every decision with the engine's own default pick" is not expressible** for a
main-phase decision — see §7 PROTO-1. Auxiliary frames *are* answered from their own default; main
phases are answered by a documented, deterministic **develop-greedy** in `drive_pick` (commit the
plan with the most casts, then a land, then the most board activations; never a combo-off plan,
because clicking one ends the game and the branch is taken separately). It reaches the mid-loop
re-prompt boards this hunt is about. It is **not** the deck's shipped policy, so do not read §1's
driven numbers as a search-shortcut base rate the way the phase-1 catalogue's `engine` population
could be read.

---

## 2. The class table

Classification is `combo_off_sweep.classify()` **verbatim**, over a record built in the same shape;
the python arithmetic oracle (`arith`) and the cluster tagger (`mechanism`) are likewise imported,
not re-derived, so the counts are directly comparable with the phase-1 catalogue.

| class | meaning | n | % | reference | driven |
|---|---|---:|---:|---:|---:|
| **a** | offered, and the click **wins this turn** | **755** | 12.2% | 222 | 533 |
| **b** | offered, and the click does **not** win this turn — **EXECUTOR FAILURE** | **295** | 4.8% | 55 | 240 |
| **c** | not offered, and the line won that turn — **MISSED OFFER** | 77 | 1.2% | 77 | 0 |
| ↳ `c_never_offered_that_turn` | *the real signal* | **2** | 0.03% | 2 | 0 |
| ↳ `c_before_first_offer` | an earlier frame of a turn the button *did* later fire on | 75 | 1.2% | 75 | 0 |
| **c′** | not offered, won by plain **combat damage** | 97 | 1.6% | 12 | 85 |
| **d** | offered and provably unwinnable — **RULE TOO LOOSE** | **0** | 0.0% | 0 | 0 |
| **e** | not offered, not winnable — correct | 4,940 | 80.0% | 175 | 4,765 |
| x | probe error (all nine are the killed hangs, §7) | 9 | 0.1% | 0 | 9 |

**Of the 1,059 states where the button appeared, 755 won and 295 did not — a 27.9% failure rate on
the click** (phase 1 measured 31.3% on 268 offers). The user's complaint reproduces at true
fidelity, at nearly the same rate, on four times as many offers.

**Two sub-splits this catalogue adds, because the raw class is misleading without them:**

* **Class c is almost entirely an artefact of when in the turn you ask.** A frame on a turn the line
  went on to win, sitting *before* the turn's first offer, is the turn still ASSEMBLING the combo —
  the button correctly does not fire until the loop exists. 75 of the 77 are that. Only **2** are
  frames where the button never fired on a turn that was nevertheless won (§6).
* **Class b is mostly "too early", not "broken".** See §4.

---

## 3. THE RESULT THAT MATTERS MOST: `combo_off_verified` is still a perfect *sufficient* oracle — and it is now demonstrably not a *necessary* one

Across all 1,059 offered states, on real boards with real pools, real shuffles and real exile:

```
verified  won-this-turn      n
  True        True          721      <-- 100.0%, zero exceptions
  False       False         304
  False       True           34      <-- NEW: phase 1 saw 0 of these in 78
```

* **`combo_off_verified` ⇒ the click wins this turn: 721 / 721.** Phase 1's headline (268/268)
  holds, and now on a population four times larger that includes everything a fixture cannot stage.
  **A search shortcut keyed on `verified` remains the only safe trigger on the evidence.**
* **But `verified` is NOT necessary.** 34 of 338 unverified offers (10.1%) *do* win this turn —
  29 `WISH-DRAW`, 4 `GORGE`, 1 `WISH-NODRAW`. Phase 1 recorded "UNVERIFIED but DID win: 0" and
  concluded the flag was a perfect oracle in both directions; on replayed boards that second half
  is false. A shortcut that *skips* work whenever `verified` is false would therefore discard ~10%
  of real kills. Gate the shortcut on `verified == true`; never on `verified == false`.
* **`combo_off_offered` on its own would be wrong 27.9% of the time.** Unchanged conclusion:
  do not key the shortcut on the display rule.

### Per rule

| rule | offered | wins this turn | rate | phase-1 rate |
|---|---:|---:|---:|---:|
| `DEPLOYED` | 44 | 44 | **100%** | 86.3% |
| `IN-HAND` | 4 | 4 | **100%** | 83.9% |
| *(rule name EMPTY)* | 45 | 45 | **100%** | 100% (n=1) |
| `WISH-DRAW` | 948 | 657 | 69.3% | 54.5% |
| `GORGE` | 16 | 4 | 25.0% | **0.0%** |
| `WISH-NODRAW` | 2 | 1 | 50% | 100% (n=1) |

Two things move against phase 1, and both are good news for the fixer:

1. **`DEPLOYED` and `IN-HAND` are clean on real boards.** Phase 1 attributed 16 + 5 failures to
   them; here they are 48 for 48. Those failures were synthetic-population artefacts.
2. **`GORGE` is no longer 0%.** It wins 4 of 16. It is still the worst rule by a distance, and
   C2 is still real (§5), but "GORGE has never once executed" is no longer true.

**Every one of the 295 failures is `WISH-DRAW` (282), `GORGE` (12) or `WISH-NODRAW` (1), and 286 of
them have the Living Wish still in the LIBRARY.** The wish-from-library family is ~97% of the
failure mass — phase 1 put it at 55%. That is the single sharpest targeting statement in this
document.

**The phase-1 `net_c <= 0` predicate does not transfer.** It separated 42/42 offers perfectly in the
synthetic matrix; here only 49 of 295 failures have `loop.net_c <= 0` (the mode is `net_c == 1`,
141 states). The synthetic matrix over-represented net-zero boards. Do not ship `net_c > 0` as a
display narrowing on the strength of phase 1's separation — on replayed boards it would suppress
real offers without catching the failures.

---

## 4. NEW SHAPE: most "failures" are the button firing EARLY, not the combo being broken

This does not appear in the phase-1 catalogue at all, because a `--scenario` fixture is graded on a
single turn and cannot see what happens next.

| | n |
|---|---:|
| class-b states whose line **does** win, just on a later turn | **173** |
| class-b states that never win inside the horizon | 122 |

Delay distribution (win turn − offered turn): **+1: 85, +2: 22, +3: 13, +4: 25, +5: 28.**

So for 59% of the failures the go-off machinery is *working*; the display rule fired a turn or more
before the kill was actually available. That reframes the worklist: the aggressive-display doctrine
(fire wherever the rule holds) is producing an offer whose own label — "COMBO OFF (NOT PROVEN — may
not finish)" — is honest, and the user-visible defect is a promise of *this turn*. Whether that is
fixed by narrowing the rule or by relabelling the unproven button with the turn it can actually
reach is a ruling, not an agent's call (§8, question 1).

---

## 5. The failure clusters

Tagged by `combo_off_sweep.mechanism()` verbatim, then named with the phase-1 cluster whose
signature the tags match. **No new cluster shape was found: every one of the 295 failures falls
into one of the five catalogued shapes.** Phase 1's sixth, **C3** (the go-off sized in generic mana
while the finisher is paid in {C} pips), **does not occur at all** in this population — it was 19/20
synthetic.

| cluster | n | reference | driven | rules | wins later | never wins |
|---|---:|---:|---:|---|---:|---:|
| **C6** loop runs, finisher never activates | **103** | 9 | 94 | WISH-DRAW 102, WISH-NODRAW 1 | 31 | 72 |
| **C1a** loop runs, the draw engine under-digs, wish never found | **64** | 29 | 35 | WISH-DRAW 64 | 46 | 18 |
| **C5** the finish fires and runs out | **61** | 2 | 59 | WISH-DRAW 56, GORGE 5 | 58 | 3 |
| **C1b** the draw-to-find route stops the loop before it starts | **58** | 15 | 43 | WISH-DRAW 58 | 36 | 22 |
| **C2** a Shivan Gorge on the battlefield starves the loop | **9** | 0 | 9 | GORGE 7, WISH-DRAW 2 | 2 | 7 |

Every repro below was **re-run and verified to reproduce** before being listed.

### C6 — the loop runs to completion and the finisher never activates once (103)

Largest cluster, and it is the one phase 1 saw only synthetically (11 states, all synthetic).

**Exemplar** `RR-0a59241227`, `claude_s10_gi9` T4 pre-main, ordinal 4, rule `WISH-DRAW`,
`verified=false`, `loop.net_c = 2`.
Board: Brushland, Brushland + Fertile Ground + Wild Growth, **Eldrazi Displacer**, Kitchen,
Mariposa Military Base, **Peregrine Drake**, **Training Grounds**; hand `[Eldrazi Displacer]`;
opponent 20, library 50.

Mechanism: the plan promises 18 blinks and **all 18 run**. The draw sink runs hard —
`draw_seen 200, draw_paid 99` — and `ComboFinishFromHand` is called **100 times with
`none_found = 100`, `wish = 5`, `pay_fail = 0`, `no_mana = 0`**. So the loop is healthy, the mana is
there, the wish was seen five times, and the finisher is *never a candidate*. Zero drains, zero
exiles; the opponent ends the turn on 20. The line does go on to win on turn 8.

```
MTG_PLAY_PLANS_CAP=0 build/Release/mtg decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod \
  --claude-play --seed 10 --game-index 9 --max-turns 8 --depth 0 \
  --profile decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json \
  --choices '0,-1,-1,2,-1,-1,2,-1,-1,22,9,-1,-1,-1,-1,-1,-1,-1,-1,-1' --force-mulligan 0: \
  --cast-order '3:Training Grounds|Peregrine Drake' \
  --tap-pref '2:pre:1:1;3:pre:2:1;4:pre:4:11;4:pre:31:9;4:pre:32:7'
```

`none_found = calls` with `wish > 0` is the signature: the wish *was* reachable and the finisher
selection still produced no candidate. That is a selection bug, not a mana bug — `pay_fail` and
`no_mana` are both zero.

### C1a — the loop runs and the draw engine stops short, so the wish is never cast (64)

**This is the user's seed-6 report** — *"the loop drew 13 cards and stopped although Living Wish was
still in the library"* — reproduced on the user's own saved boards (29 of the 64 are reference
frames).

**Exemplar** `RR-8667e49f22`, `claude_s8_gi7` T3 pre-main, ordinal 4, rule `WISH-DRAW`,
`verified=false`, `loop.net_c = 0`.
Board: Cloud of Faeries, Conservatory + Fertile Ground (tapped), **Eldrazi Displacer**,
Mariposa Military Base + Fertile Ground + Wild Growth, Yavimaya Coast; hand empty; library 52.

Mechanism: 60 blinks promised, **2 run**. `draw_seen 35, draw_paid 11, draw_guard 24`;
`calls 24, none_found 24, wish 0`. The draw engine fires eleven times, the guard refuses
twenty-four, the dig never reaches a Living Wish, and the loop dies after two iterations.

```
MTG_PLAY_PLANS_CAP=0 build/Release/mtg decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod \
  --claude-play --seed 8 --game-index 7 --max-turns 8 --depth 0 \
  --profile decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json \
  --choices '0,-1,-1,2,-1,-1,49,0,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1' --force-mulligan 1:20 \
  --cast-order '1:Wild Growth|Fertile Ground;2:Fertile Ground|Cloud of Faeries' \
  --tap-pref '2:pre:1:0;3:pre:3:0;3:pre:4:8;3:pre:27:5;3:pre:29:5;3:pre:32:8,12;3:pre:40:0,1,7,10;3:pre:41:9;3:pre:58:0,1,9;3:pre:60:10;3:pre:62:0,1'
```

### C1b — the draw-to-find route eats the loop's entry price (58)

Same board one decision later: the loop runs **zero** times.

**Exemplar** `RR-b7c88345d0`, `claude_s8_gi7` T3 pre-main, **ordinal 6** (the immediate re-prompt
after the C1a frame above), rule `WISH-DRAW`, `verified=false`, `loop.net_c = 0`, **float `{G:2}`**.

Mechanism: 60 promised, **0 blinks**. `draw_seen 54, draw_paid 13, draw_guard 41`,
`calls 32, none_found 32, wish 0`. `SpendSurplusOnDrawSinks` runs before `pay(c)` and the blink is
then unpayable — the phase-1 C1b shape exactly, now on one of the user's own boards, with a real
{G}{G} float in the pool at the moment of the click.

```
MTG_PLAY_PLANS_CAP=0 build/Release/mtg decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod \
  --claude-play --seed 8 --game-index 7 --max-turns 8 --depth 0 \
  --profile decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json \
  --choices '0,-1,-1,2,-1,-1,49,0,1,2,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1' --force-mulligan 1:20 \
  --cast-order '1:Wild Growth|Fertile Ground;2:Fertile Ground|Cloud of Faeries' \
  --tap-pref '2:pre:1:0;3:pre:3:0;3:pre:4:8;3:pre:27:5;3:pre:29:5;3:pre:32:8,12;3:pre:40:0,1,7,10;3:pre:41:9;3:pre:58:0,1,9;3:pre:60:10;3:pre:62:0,1'
```

The C1a/C1b pair on **ordinals 4 and 6 of one turn of one saved game** is the tightest possible
A/B for the draw-sink repair: the boards differ only by the one committed blink between them.

### C5 — the finish fires and runs out (61)

**Exemplar** `RR-9f29089519`, `claude_s3_gi2` T4 pre-main, ordinal 5, rule `WISH-DRAW`,
`verified=false`, `loop.net_c = 2`, **Living Wish in HAND** (`wish_in_lib_only = false`).

Mechanism: 15 blinks promised, **1** runs; the wish resolves, `⚡ combo finish: Living Wish →
Essence Depleter`, the Depleter is cast and drains **twice** — opponent 20 → 18 — and stops.
`calls 4, hand 2, wish 2, none_found 4`. The whole chain works and the loop simply did not bank
enough before the finisher was deployed. Wins on turn 7.

```
MTG_PLAY_PLANS_CAP=0 build/Release/mtg decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod \
  --claude-play --seed 3 --game-index 2 --max-turns 8 --depth 0 \
  --profile decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json \
  --choices '1,-1,-1,7,3,-1,-1,46,-1,-1,42,24,-1,-1,-1,-1,-1,-1,-1' --force-mulligan 1:5 \
  --cast-order '1:Trace of Abundance|Living Wish' \
  --tap-pref '2:pre:1:2;3:pre:2:2,6;4:pre:5:13;4:pre:27:2,6'
```

C5 is the cluster that most often wins later (58 of 61) — it is "the go-off is short", not "the
go-off is broken".

### C2 — a Shivan Gorge on the battlefield starves the loop (9)

Smallest cluster, and **zero reference instances** — phase 1's 17 were 16 synthetic + 1 engine.
It survives, at a much lower rate, in driven play.

**Exemplar** `AA-60e2a97f7f`, seed 16 / gi 15, T5 pre-main, ordinal 4, rule `GORGE`,
`verified=false`, `loop.net_c = 1`.
Board: Adarkar Wastes + Trace of Abundance, Brushland, Cloud of Faeries, **Emiel the Blessed**,
Kitchen + Fertile Ground, **Shivan Gorge**.

Mechanism: 60 blinks promised, the apply produces exactly **one** event —
`🔥 Shivan Gorge deals 1 to the opponent` — and **zero blinks**. `draw_seen 60, draw_guard 60,
draw_paid 0`. Identical to phase 1's C2: the damage sink is paid before the activation and the
blink is then unpayable. Tags also carry `draw-to-find-starves-loop`, so on this board **both**
sinks are in front of `pay(c)`.

```
MTG_PLAY_PLANS_CAP=0 build/Release/mtg decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod \
  --claude-play --seed 16 --game-index 15 --max-turns 8 --depth 0 \
  --profile decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json \
  --choices '1,0,0,0,0,0,0,2,0,0,0,0,0,69,-1,-1,-1,-1,-1,-1,-1'
```

### Worst single game in the corpus

`claude_s8_gi7` — one of the user's own T3 wins — offers the button on **60 of its 68 frames and
only 13 of those click through**, 47 failures in one game (C1a 22 / C1b 13 / C6 9 / C5 3). It is
the highest-yield single repro in this catalogue.

### Instrument caveat (states it before you trust a tag)

`[edf-goff]` is printed by the go-off count sizer at **enumeration** time and is capped at 40 lines
per process (`MTG_EDF_GOFF_DEBUG_N`); `[finish]` is dumped at **exit** and is cumulative. On a
replay both therefore cover the whole game, not the probed frame. The cluster tags above rest on
`[finish]` (whose work in human play is dominated by the clicked go-off, since
`EdfAutoGoOffAfterCasts` returns early under `HumanPlayActive()`) and on the frame-local play-event
stream, which is exact. The `goff` block in `results.json` is the blunt one — treat its numbers as
indicative. `--retrace` implements the exact fix (two-pass differencing: pass A = prefix only, so
its LAST `[edf-goff]` line is the probed frame's own sizing and its finish counters are everything
before the click; pass B = prefix + click; the click's own work is B − A). It was **not run** on
this result set, because the executor has already moved past the base commit and the right time to
spend that is on the re-run against the current tip.

---

## 6. Missed offers — the rule is too tight in almost nowhere

**2 states** (both reference frames) were never offered on a turn the line nevertheless won. Both
frame repros were re-run and the button is still absent.

**M-R1 — `RR-26ad4c4217`, `claude_s2_gi1` T4 pre-main** (a frame with no `main_ordinal`, i.e. a
look-at-the-board re-prompt). Board: Conservatory (T), Conservatory + Wild Growth (T),
**Dimensional Infiltrator**, **Eldrazi Displacer**, Peregrine Drake (T), Shivan Gorge (T),
Yavimaya Coast (T); hand `[Emiel the Blessed, Kitchen]`; float `{G:1}`. The line won that turn.
Five of the seven permanents are tapped, so the rule that *should* have fired is `DEPLOYED`
(Infiltrator on the battlefield) and what blocks it is the board's spent mana, not the rule text.

```
MTG_PLAY_PLANS_CAP=0 build/Release/mtg decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod \
  --claude-play --seed 2 --game-index 1 --max-turns 8 --depth 0 \
  --profile decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json \
  --choices '1,-1,-1,37,2,-1,-1,0,-1,-1,21,18,33,7,2,3' --force-mulligan 0: \
  --cast-order "1:Wild Growth|Eladamri's Call" \
  --tap-pref '2:pre:1:0;3:pre:2:0,1;4:pre:4:6;4:pre:6:1;4:pre:7:0,3,5,7'
```

**M-R2 — `RR-bd677d04f3`, `claude_s11_gi10` T6 pre-main, ordinal 15.** Board: Brushland,
Brushland + Trace of Abundance, Cloud of Faeries, Conservatory, Conservatory + Fertile Ground ×2 +
Overgrowth, **Emiel the Blessed**, Mariposa Military Base; hand `[Aether Hub, Brushland, Mariposa
Military Base]`; **float `{W:1, G:27, C:9}`**. The line won on turn 6. Twenty-seven green and nine
colourless already in the pool and the button is absent — the rule that should have fired is
`WISH-DRAW` (`wish_in_lib_only = true`), and the candidate cause is that **`ComboOffPossible` reads
board inventory and does not read `state.floating_mana`** (which is what `18c404d6`, "bank counts
the floating pool", is about — re-check this state on the current tip first).

```
MTG_PLAY_PLANS_CAP=0 build/Release/mtg decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod \
  --claude-play --seed 11 --game-index 10 --max-turns 8 --depth 0 \
  --profile decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json \
  --choices '0,-1,-1,1,-1,-1,6,-1,-1,12,-1,-1,8,0,-1,-1,36,49,64,72,73,74,74,74,74' \
  --force-mulligan 0: --cast-order '2:Overgrowth|Fertile Ground;3:Emiel the Blessed|Fertile Ground' \
  --force-attackers '5:Emiel the Blessed' \
  --tap-pref '2:pre:1:0;3:pre:2:0,1;4:pre:3:0,1;5:pre:4:0,6;6:pre:32:0,6,9;6:pre:45:0,6,9;6:pre:54:0,6,9'
```

Phase 1's **M1** (`HasBlueOrBlackSource` ignores a land Aura's wild mana) is **not** re-confirmed
here and does not need to be: `d92c5fc2` lists "land-aura UB" among its changes, i.e. it has
already been fixed on the current tip.

The 97 `c′` combat kills are split out exactly as the sweep does, and the 75 `c_before_first_offer`
frames are documented in §2 as an artefact of asking mid-assembly rather than a rule defect.

---

## 7. FLOATING MANA — what it actually changes

Two separate experiments, because "does the pool change the MENU" and "does the pool change the
EXECUTION" are different questions with different answers.

### 7a. In-apply arms: a hand-staged pool makes the go-off WORSE, 3.4 to 1

For every offered state the click was re-run with `tap=<Card>#<num>:<COLOUR>` tokens riding
`--cast-order` (`docs/design/viewer-manual-tap-pay.md`), position 0, so the go-off's payments run on
a board that already has a real floating pool. Two arms: every hand-tappable source on its first
legal face, and only the {C}-capable sources tapped for {C}.

| arm | staged | taps really applied | **fixed a failing click** | **broke a winning click** |
|---|---:|---:|---:|---:|
| `float_all` | 1,040 | 1,024 | **20** | **68** |
| `float_c` | 1,035 | 1,020 | **8** | **31** |

**Pre-tapping is net negative: it is ~3.4× more likely to break a go-off that worked than to rescue
one that did not.** The mechanism is visible in the exemplars: the breakages cluster on tapping a
DUAL for one face (`tap=Azorius Chancery#200000:W`, `tap=Brushland#6:G`, `tap=Brushland#6:C` — six
of the `claude_s12_gi11` T4 frames break on exactly those), which commits a colour the allocator
would otherwise have chosen later. This is documented behaviour, not a bug — the manual-tap doc says
plainly that a pre-tap controls *supply* and that "sources the human does not pre-tap stay the
allocator's to choose" — but it is worth the user knowing that **hand-tapping before clicking Combo
Off will more often cost the kill than save it**, and worth a fixer knowing that 20 real go-offs are
rescued purely by having more mana in the pool at `pay(c)` time (which is the C1b/C2 sink-ordering
story from the supply side).

An earlier reading of this data — that a pre-tap's unspent mana is silently destroyed — was **my own
measurement error** (I read `floating_mana` at the top level of the decision JSON instead of under
`me`). Re-checked: the taps fire, the source is tapped, the surplus persists into the next committed
line exactly as the doc says. Recorded here so nobody re-derives the false version:
with `--choices 1,0,0,1,0,0,107` on seed 1/gi 0, adding
`--cast-order '2:tap=Conservatory#13:G'` yields `me.floating_mana = {G:2}` at the next frame and
`manual tap: Conservatory for {G}` in the events. (It *does* cut that frame's plan list from 64 to 9,
because committing Conservatory to {G} removes the deck's {U}/{W} access — correct, and the reason
7a comes out negative.)

### 7b. Cross-frame: a floating pool DOES create offers the empty-pool sweep cannot see

The in-apply arms cannot move the menu — `combo_off` is decided at ENUMERATION, before any pick. To
move it the pool has to exist *before* the frame, and the only way to arrange that (also the only
way a human can) is to over-tap on the PREVIOUS committed line and let the surplus ride into the
re-prompt. `--float-offer` does exactly that for every class-c state.

```
selected 77   stageable 67   NEW offers 5
[10 not stageable: the preceding decision is not a main_phase in the same turn+phase,
 so no pool can survive into it (CR 500.4) -- correct, and the question does not arise]
```

The clean, verified one (the other four ride a reference that already carries its own
`--cast-order`, so the probe's appended one displaces it and those rows are confounded):

**`claude_s1_gi0` T3 pre-main, ordinal 23 — NOT offered with an empty pool; offered AND
`combo_off_verified` with the pool staged.**

```
MTG_PLAY_PLANS_CAP=0 build/Release/mtg decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod \
  --claude-play --seed 1 --game-index 0 --max-turns 8 --depth 0 \
  --profile decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json \
  --choices '1,0,0,1,0,0,84,2,2,0,2,1,3,7,8,10,10,10,10,8,4,10,10,8,10,22,22,20' --force-mulligan 0: \
  --tap-pref '2:pre:1:0;3:pre:2:0;3:pre:3:1;3:pre:4:4;3:pre:5:0,1;3:pre:14:4;3:pre:18:4;3:pre:22:4;3:pre:26:4;3:pre:31:4;3:pre:33:0;3:pre:34:1' \
  --cast-order '22:tap=Aether Hub#3:C|tap=Conservatory#13:G|tap=Mariposa Military Base#35:C'
```
→ `float {U:1, G:30, C:2}`, plan 6 `combo_off_verified: true`. Drop the `--cast-order` line and the
button is gone.

**So the phase-1 catalogue's "no floating mana ⇒ a LOWER bound on offers" caveat is real and
measurable**, and the fix for the rule table is the one M-R2 points at: read the pool.

---

## 8. Protocol and quality anomalies

The brief asked for these explicitly. All are at `d8f700bb`.

**HANG-1/2/3 — a single `--claude-play` step that does not terminate.** Three replays each ran
**>80 minutes at ~95% CPU inside one decision** before being killed; two were walk sessions
(seed 51 / gi 50 and seed 71 / gi 70) and one was a float arm (seed 39 / gi 38, `--cast-order
'155:tap=Adarkar Wastes#2:C|tap=Aether Hub#3:C|tap=Brushland#7:C'`). All three are turn-7/8 frames
on a heavily developed board. `--claude-play` sets `MTG_UNPRUNED=1`, and this deck's late boards
emit **7,509 plans on turn 5 and 11,453 on turn 6** with the enumeration itself unbounded — the same
class as `docs/design/claude-play-unprune-blowup.md`, now on EDF. **A human reaching such a board in
the play viewer gets a frozen UI, not a slow one.** Deterministic repro (the driven walk is
deterministic):
`python3 test/combo_off_replay_hunt.py --skip reference --seeds 51:51 --workers 1`.
This is also why `--quick` pins the horizon at 6.

> **ROOT-CAUSED AND FIXED 2026-09-11** — `analysis-EldraziDisplacerFlicker.md` §Session 18. The
> hunch above is right in class and wrong in one detail worth recording: the frames are **turn 6**
> (`main_ordinal` 196–260), the odometer carries **one digit per Clue token** on top of four
> Eldrazi Displacer digits so the product *doubles every few clicks* (1.15e5 → 4.61e5 within one
> turn, 448,195 plans and 15.75 s CPU on the worst frame), and the guard that should have stopped it
> — `CapGroupsBySituationalRank`'s `MTG_PLAN_SPACE_CAP` — is returned out of by
> `DecisionUnpruned(UnprunedGate::GroupCap)`, which `--claude-play`'s blanket `MTG_UNPRUNED` opens.
> `MTG_PLAY_STEP_TIMING` also clears the Combo Off trial apply completely: 47 ms of trials against
> 548.9 s of base enumeration. Fix: `MTG_VIEWER_PLAN_CAP` (human play only, default on), which
> bounds the product and *reports* the truncation (`plans_truncated`) instead of hanging. Worst
> click on this exact line 15.75 s → 1.76 s. Gate: `test/viewer_plan_space_check.py`.

**ANOM-1 — 45 offered plans carry an EMPTY `combo_off_rule`.** All 45 are reference frames, all are
`combo_off_verified`, all win — so it is a provenance/display defect, not a correctness one, but the
viewer's rule badge renders blank and a saved decision JSON cannot say which rule fired. Phase 1 saw
this once (`R_claude_s11_gi10_58`) and called it minor; at 45/1,059 offers (4.2%) it is not minor.
Example: `claude_s1_gi0` T3, `land=none; cast: Cloud of Faeries, Emiel the Blessed: blink Peregrine
Drake x19 -- COMBO OFF: wins this turn`, `--choices
'1,0,0,1,0,0,84,2,2,0,2,1,3,7,8,10,10,10,10,8,4,10,10,8,10,22,22,20,46,72'` (+ the s1_gi0
`--tap-pref` above).

**ANOM-2 — 173 clicks win on a LATER turn than the one they were offered on** (§4). The verified
button says "wins this turn"; the unproven one says "may not finish". Neither says "wins on turn
+5", which 28 of them do.

**ANOM-3 — `claude_s9_gi8` is a PLAY-DRIFT at this commit.** `viewer_protocol_check` reports
`replay won=True win_turn=8 vs ref won=True win_turn=4`. This is the user's reported seed-9 T4
failure and it is **four turns of a saved, user-owned game**; the `--strict` gate is red on it
independent of anything in this hunt. In this hunt that reference produced **0 combo-off offers
across all 62 frames**. Not caused by this work; it was already there at `d8f700bb` and needs
root-causing before anyone re-saves that reference. (Every other EDF reference replays `ok` or
`repaired` with its recorded win turn intact.)

**PROTO-1 — the protocol exposes no engine pick for a main-phase decision.** `heuristic_default` /
`ai_choice` are emitted for every auxiliary decision type and for none of the main-phase ones, `-1`
means pass, and there is no `--auto`. So there is no way to ask the protocol "play this game the way
the engine would", which is what a faithful autonomous population needs. A one-line addition
(`d.HeuristicDefault(ai_pick)` on the main-phase frame) would make claude-play-driven sweeps
comparable with `--log-dir` autonomous play; without it, §1's driven population is a proxy.

**PROTO-2 — no protocol defect found in 6,173 frames otherwise.** Every frame was well-formed JSON
with the required keys; no malformed frame, no clamped pick, no engine error. The nine `x_error`
states are the three hangs' killed children, not engine faults.

**HARNESS-1 (recorded because it will bite the next person).** Retaining every walked frame's full
plan list OOM-killed the first full run at **35 GB RSS**, 50 jobs in — an EDF mid-go-off board emits
thousands of plans and the walks hold hundreds of frames × 14 workers. `compact_frame()` now drops
every non-combo plan before a frame is retained, and the driven walks run at the viewer's own
`MTG_PLAY_PLANS_CAP=200` (safe: the verified/offered combo-off plan has a reserved slot the cap
cannot evict, and `index` stays the true engine index). Walk time fell from 13 s to 3 s on a
397-frame game. A partial snapshot is now written every ten jobs, which is the only reason this
document exists after the hangs.

---

## 9. Ranked worklist for the fixer

| # | item | states | kind | first repro |
|---|---|---:|---|---|
| **1** | **C6 — the loop runs, the wish is seen, and the finisher is never a candidate** (`none_found == calls`, `pay_fail = 0`, `no_mana = 0`) | **103** | executor: finisher SELECTION, not mana | `RR-0a59241227` (§5) |
| **2** | **C1a + C1b — the draw-to-find route**: under-digs (64) or eats the loop's entry price (58). 286 of 295 failures have the wish in the library | **122** | executor: sink ordering vs `pay(c)` | `RR-8667e49f22` / `RR-b7c88345d0` — ordinals 4 and 6 of ONE turn of `claude_s8_gi7` (§5) |
| **3** | **C5 — the finish fires and runs out** (58 of 61 win later, so this is "short", not "broken") | **61** | sizing: bank before deploying the finisher | `RR-9f29089519` (§5) |
| **4** | **The rule table does not read `state.floating_mana`** — M-R2 has `{W:1,G:27,C:9}` in the pool and no button, and §7b turns a not-offered frame into a *verified* one purely by staging a pool | 2 confirmed + 5 float-created | rule too tight — widening, doctrine-aligned | §6 M-R2, §7b |
| **5** | **`claude_s9_gi8` play-drift T4 → T8** (ANOM-3), and the 45 empty `combo_off_rule` badges (ANOM-1) | 1 game + 45 offers | correctness regression / display provenance | §8 |

Below the line but cheap: **C2** (9 states, GORGE, the damage sink still runs before `pay(c)`), and
the **HANG** class (§8), which is a play-viewer usability bug rather than a Combo Off one.

**Do not start from `net_c > 0`** (§3) and **do not spend time on C3** (§5): both were
synthetic-population effects and neither reproduces on replayed boards.

---

## 10. Open questions for the user (surfaced, not blocking — nothing waited on these)

1. **59% of the "failures" are the button firing EARLY, not the combo being broken** (§4: 173 of
   295 win, a median of +2 turns later). Under the aggressive-display doctrine that is arguably
   working as intended and the defect is the *wording*. Do you want the unproven button to carry the
   turn it can actually reach (e.g. "Combo Off — wins turn 6"), or the rule narrowed so it only
   fires on the turn the kill lands? Nothing was changed either way.
2. **Hand-tapping before Combo Off costs more kills than it saves** (§7a: 68 broken vs 20 fixed).
   Should the viewer warn when a pre-tap commits a dual's only flexible face while a combo-off plan
   is on the menu? This is a UI question, not an engine one.
3. **Phase 1's `net_c <= 0` separation does not survive contact with real boards** (§3: 49 of 295).
   If a display narrowing is still wanted, it needs a predicate fitted to *this* population — the
   one that actually separates here is "the Living Wish is still in the library" (286 of 295).
4. **`claude_s9_gi8` loses four turns at `d8f700bb`** (§8 ANOM-3). That is a saved, user-owned game.
   Do you want it root-caused before or after the C1/C6 executor work?
