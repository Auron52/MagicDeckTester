# Rollout/executor lockstep: the bug class, the method, and the open case (2026-07-29)

**The class.** The rollout scores and COMMITS a line that the real executor does not reproduce. This
is not a heuristic disagreement and not search weakness: the search was right, the game just played
something else. It reads as "the search is bad", and **no depth and no budget can fix it**, which is
exactly what makes it expensive to diagnose and easy to misattribute. Three instances were found and
two fixed on 2026-07-28/29; this doc records the method so the next one is cheap.

`MTG_FD_ORACLE` is the detector: it flags any game whose realised win turn is worse than a win the
search had already PROVEN, and prints `[fd-diverge] seed=N realized_win=.. predicted_win=..`.

## Method

1. **Map it.** `MTG_FD_ORACLE=1 mtg <deck> --games 500 --seed 4004` per deck. The per-deck rate is the
   map; a deck at 0 needs no attention.
2. **Reproduce one game.** `--seed <the printed seed> --game-index <seed - base_seed> --games 1`
   (the runner seeds each game at `base_seed + gi`).
3. **Diff the two sides.** `MTG_BP_TRACE=1 MTG_FD_TRACE=1 MTG_NONCONV_TRACE_SEED=<seed>` gives three
   aligned streams:
   - `[fd-pred]` — the rollout replaying its own COMMITTED line (what the search believed)
   - `[traj]` — what the real game actually did
   - `[bp-pay]` — one line per cast from BOTH sides: turn, cost, floating mana, untapped sources
4. **Read the first difference.** The signature tells you the subsystem:

   | first thing that differs | subsystem |
   |---|---|
   | `[bp-pay]` cost/float/untapped | mana payment |
   | `[bp-pay]` identical but `libtop` differs | a tutor / shuffle / random choice |
   | both identical but life or creature counts differ | combat, a continuous effect, or a missing SBA |
   | the two sides cast a DIFFERENT NUMBER of spells | something upstream changed the hand |

Targeted traces exist for the subsystems that have bitten, all inert unless set:
`MTG_LP_TRACE` (Light-Paws fetch + shuffle, tagged `APPLY` vs `EXEC`), `MTG_DISCARD_TRACE` (Gamble's
random discard: seed inputs, hand size, victim, and every hand card's `m_number`), `MTG_SOULFIRE_TRACE`
(the Soulfire dig from both sides: life, opponent creatures, target count, chosen targets, exiled
cards), `MTG_BP_TRACE` (per-cast mana from both sides) and `MTG_CCO_AUDIT` (illegal creature-only
taps).

**Print the per-copy `m_number`, not just card names.** Two hands that print identically by name can
hold different physical copies (#4 below was invisible until the trace showed `Island#0` vs
`Island#23`), and a name-only trace will send you looking for a decision bug that isn't there.

## Found so far

| # | cause | status |
|---|---|---|
| 1 | rollout paid coloured pips off a `colored_creature_only` land for a non-creature spell | **FIXED** (`6bb2791`) |
| 2 | legend rule not enforced as a state-based action in the executor | **FIXED** (`b71e5e3`) |
| 3 | rollout counted STAGED cards toward the 7-card cleanup limit; the executor never does | **FIXED** (below) |
| 4 | rollout did not stamp a played LAND's per-copy `m_number`; the executor always has | **FIXED** (below) |
| 5 | Gamble's "discard a card at random" indexes the hand AS STORED | **built, opt-in, NOT default** (below) |
| 6 | a cast recorded INSIDE a breakpoint continuation lost `chosen_float_color` / `enchant_target` | **FIXED** (below) |

Rate per 500 games (seed 4004, 8 decks, 4000 games) after 1-4: **0 everywhere, Hinata included.**
(Dragonstorm was 4-5 at the suite gate budgets before #1; Auras was 3 before #2; Hinata was 4 before
#3.)

## #3 — staged cards counted against maximum hand size (the Hinata case, root-caused)

CR 514.1 discards down to maximum hand size from the **hand**. A card exiled with "you may play it
until ..." — Soulfire Eruption's dig, Light Up the Stage, Expressive Iteration, Apex of Power, an
Apex land — is in **exile**, not hand, and does not count. The engine models those as hand cards
flagged `m_is_staged` purely so the castable-set code can see them, and the real executor moves them
back out to `Player::staged_cards` at the end of `AIEngine::TakeTurn` — so by the time
`GameEngine::CleanupStep` runs they are simply not in hand. The rollout
(`SimulateEndAndStartNextTurn`) kept them in `ap.hand` for its whole lookahead and counted them.

Hinata seed 4010: the T5 Soulfire dig staged 8 cards into an otherwise-empty hand, so the rollout
shed one at cleanup and planned turn 6 one card short. The realised turn kept that card, cast it,
and reached Crackle with Power one mana short of the lethal X the search had proved (predicted T6,
realised T7). Fix: count and shed only non-staged cards. Hatch `MTG_LEGACY_STAGED_HANDLIMIT`.

**This is the bug the previous write-up mis-attributed to `SoulfireDig`.** A both-sides dig trace
(`MTG_SOULFIRE_TRACE`) showed the two sides exiling the *identical* 8 cards — the target count was
never wrong. Reading the first difference, not the most plausible suspect, is what found it.

## #4 — a played land lost its per-copy number in the rollout

`AIEngine::TryPlaySpecificLand` has always done `perm.card.m_number = it->m_number`; the rollout's
`PlayLandByName` did not, so every land the rollout played became permanent `#0`. Invisible until
something reads a LAND permanent's number — `BounceKarooLand` returns `battlefield[pick].card` to
hand, so an Izzet Boilerworks bounce handed the rollout an unnumbered Island while the real game got
the numbered one (Hinata seed 4153 T3). The Vial-deploy path had the same gap on the rollout side.
Every real-board permanent-creation site now stamps the number; the only unstamped ones left are
opponent tokens (no card) and the speculative mana-rock copies inside the scratch feasibility state.

## #5 — Gamble's random discard: built, opt-in, deliberately NOT the default

`PerformTutor` picks `mix % hand.size()` as an index into the hand **as stored**, and storage order
is not part of the lockstep contract (the rollout pushes staged cards straight into hand; the
executor merges `Player::staged_cards` at its own breakpoints). After #3 the two hands hold the same
cards — in a different order — and still shed different ones:

```
[discard] T6 handsize=8 victim=1(Island)  hand=[Distorting Wake,Island,Mountain,...]      <- rollout
[discard] T6 handsize=8 victim=2(Island)  hand=[Reality Spasm,Distorting Wake,Island,...] <- executor
```

`MTG_CANON_TUTOR_DISCARD=1` draws over ascending `m_number` instead — still a uniform draw over the
same set, and it does put the two sides in lockstep (both shed `Island#21` above).

**Why it is off by default.** Measured over 4000 held-out games on 8 decks: avg-neutral everywhere,
but it takes the fd-diverge count **0 -> 1** (Hinata seed 4259, where the discards themselves
AGREE — the changed line simply walks into a different, still-unidentified divergence). Raising the
metric this workstream exists to drive down is not defensible until 4259 is root-caused.

**Order of fixes mattered, and the earlier write-up's conclusion was budget-limited, not wrong.**
Enabling #5 alone (hands still differing in CONTENT) took Hinata 2 -> 4 per 500. With #3 it went
3 -> 1 only after #4 landed too. Any index-based pick lands on a different card when the sets differ,
however it is canonicalised.

## #6 — a breakpoint continuation's cast lost its searched float colour (2026-07-29)

`ApplyPlanDirect` records the casts made inside a breakpoint continuation into
`Action::breakpoint_casts` so `AIEngine::replay_recorded` can reproduce them verbatim. The recorder
copied 11 fields; the replay feeds NINE of them back into `cast_by_name`, and **two of those nine
were never recorded**: `chosen_float_color` and `enchant_target`.

`GameState` documents the consequence for the first one: "empty -> wild / no colour choice". So for
Apex of Power ("If this spell was cast from your hand, add ten mana of any one color") the rollout
floated the SEARCHED colour and the executor floated **wild** — which the card's own contract
explicitly forbids ("NOT wild; wild could illegally pay a multicolor mix"). The two sides then paid
different pips from the same float and the realised turn fell behind the committed one.

Repro (`Dragonstorm` seed 6006 gi362, `--depth 9 --budget-ms 0 --ignore-play-profile`):
`[fd-diverge] realized_win=7 predicted_win=6 proven_at_turn=1`. `MTG_BP_TRACE` shows both sides
agreeing on the breakpoint indices and the whole cast script, then splitting inside one cast:

```
[bp-apply] turn=6 site=2 idx=1 bp_at=0 bp_choice=16 searched=0
apply  cast=Utvara Hellkite  cost=6/R2  float=R14 wild0
exec   cast=Utvara Hellkite  cost=6/R2  float=R4  wild10
```

**Latency is the interesting part.** This was unreachable until continuation rank 16 became
searchable: at the shipped `MTG_BP_SEARCH=2` no continuation that casts a SECOND Apex was ever
selected, so the recorder's gap never mattered. It reproduces at `MTG_BP_SEARCH=17` with the
deferred waves off, so the waves EXPOSED it rather than caused it — the same shape as #1, which
nested breakpoint search exposed by reaching deeper into a turn where mana is tightest.

Fixed by recording both fields. Inert at every suite budget (smoke 21/21, regression 35/35,
overnight 84/84 ALL PASS, zero configs changed) — W=2 does not reach such a continuation on a
committed line. `enchant_target` had the same gap and is fixed alongside; no case exercises it today
(Auras casts its auras from the main plan, not from a breakpoint continuation), so it is a
latent-bug fix, not a measured one.

## Both remaining leads: ROOT-CAUSED, and NEITHER is a lockstep bug

This matters for the metric: **`fd-diverge` is not a pure bug count.** It flags "the search proved a
better line than the game realised", and there are two legitimate ways that happens without any
engine defect. Both of the leads left after #1-#4 turned out to be one of them.

### Overnight seed 4661 (TH, `realized=9 predicted=5`) — the non-clairvoyance gate, WORKING AS DESIGNED

Not a bug; do not "fix" it. `MTG_TH_STRICT_FLOOD=0` and `MTG_UNPRUNE=drawengine` both recover the
T5 win, so the cause is `TreasureHuntProvider::ShouldCastDrawEngine` — the adopted (2026-07-16)
strict-flood gate that refuses to cast Treasure Hunt once the land drop is spent with no Land's Edge
outlet in play. The proven T5 line casts Treasure Hunt into a library whose next ~9 cards are lands
followed by Land's Edge, then casts it and throws 17 lands for 34. **That proof is clairvoyant** —
it is only correct because the search knows where Land's Edge is. The gate deliberately declines the
gamble, which is the whole point of the adoption.

Re-measuring the gate confirms the original adoption note rather than contradicting it (TH, held-out
seeds 4004-7007, `MTG_UNPRUNE=drawengine` vs default):

| depth | removing the gate |
|---|---|
| d0 (greedy, no lookahead) | **worse** on 4/4 cases (+0.062 … +0.115) |
| d3 / d5 (searched) | "better" on 8/8 cases (-0.066 … -0.077) |

The searched-depth "gain" is exactly the `+0.11..+0.125 clairvoyantly WORSE` figure the adoption note
already recorded and audited across 1012 games, finding 0 real regressions. So TH has an
**irreducible fd-diverge floor** at searched depth: a clairvoyant oracle will always be able to prove
wins that a non-clairvoyant gate correctly refuses. Judge TH by avg turn-to-win, not by fd-diverge.

### Hinata seed 4259 — breakpoint continuation WIDTH, and #5 is not at fault

`MTG_BP_SEARCH=4` recovers the T5 win; the default width is W=2, so the winning post-breakpoint
continuation is simply not among the two variants searched. The decisive control:

| discard | W=2 | W=4 |
|---|---|---|
| default (raw order) | T6, no proof | T6, no proof |
| `MTG_CANON_TUTOR_DISCARD=1` | T6, **proof of T5** | **T5** |

With the default discard the T5 win **does not exist at all**. The canonical discard *creates* it;
only the default width fails to cash it. Realised turns are T6 either way, so #5 costs nothing in
the metric — the `0 -> 1` fd-diverge was the oracle correctly reporting a newly-available line, not
a regression. Widening the breakpoint search is its own (measured, node-costly) question — see
`post-breakpoint-search.md`.

**Where that leaves #5.** Suite A/B with the flag on, all three modes: searched-depth net **-0.105**,
but that splits into train (smoke+regression) **-0.113** and held-out (overnight) **+0.008** — i.e.
train-positive, **held-out neutral**. The d0 cases move +0.044, which is noise: at depth 0 there is
no lookahead, so changing which card a random discard takes just reshuffles. Conclusion: it is a
genuine lockstep correctness fix with no measurable quality cost or gain, and its stated blocker is
cleared. Flipping the default is now purely a correctness-vs-rebaseline-churn call (12+ Hinata GT
cases across 3 modes), not a quality one.

## Why this is worth doing

Every one of these silently caps the search: the engine proves a better line than it plays. #1 was
worth -0.010..-0.033 avg across 8 of 8 held-out Dragonstorm cases with zero regressions; #2 took a
tracked Auras game from T5 to the T4 the search had already found, and stopped the executor
double-counting a legendary's cost discount. #3+#4 measured, on all three suite modes:

| mode | changed cases | net avg | per-game |
|---|---|---|---|
| smoke | 3 (hinata x2, dragonstorm) | -0.020 | 0 slower, 3 faster, 28 play-changed |
| regression | 8 (hinata x4, burn x2, dragonstorm x2) | -0.068 | 2 slower (both draw-divergent variance), 10 faster incl. one unwon -> T6 |
| overnight | 15 | **-0.192**, every case better or neutral | 15 slower (10 budget churn, 2 variance, 1 recovers to T6 = better than its old T8, 1 same-draws one-turn), 8/8 hinata cases improved |

`nonconv` stayed 0 in every arm. Suite-wide fd-diverge fell 23 -> 17 on overnight and 3 -> 1 on
regression, with the single surviving SEVERE case (seed 4661) present identically in both A/B arms —
i.e. pre-existing, not introduced. None of this was reachable by tuning a heuristic.

## A note on #2: how long the duplicate actually lived

The legend rule applies IMMEDIATELY -- a second copy never gets to do anything. Pre-fix the executor
had exactly two sweep sites: Vial deploys (`AIEngine`) and begin-combat (`GameEngine::CombatPhase`,
before `DeclareAttackers`). So the window ran **from the duplicate entering to the next begin-combat
step**:

| duplicate enters in | window |
|---|---|
| main 1 (measured) | rest of main 1, incl. every breakpoint re-solve in it |
| main 2 | through cleanup, the opponent's whole turn, AND all of the next main 1 |

Measured per phase on the pre-fix binary (Auras seed 4227 gi223): T3 MAIN_1 ends holding both
Light-Paws (#38, #39); T3 COMBAT is back to one. So it is not a permanent extra body -- and it could
never ATTACK, since the sweep precedes `DeclareAttackers`. That is the one outcome the old placement
happened to prevent, and it prevented it by accident of placement, not by design.

Everything else was live for the whole window, which is where the damage is: static abilities,
on-enter and on-cast triggers, and anything counting permanents all saw two. Spirit Link resolved
with two Light-Paws out and fetched twice; two Hinata, Dawn-Crowned double-counted a static cost
discount and let the executor cast a Reality Spasm it could not afford.

Two lessons for this bug class:

1. **State the window you measured, not the impression it gave.** An SBA deferred to a later phase
   looks like a correct engine whenever you inspect the board between turns, which is where most
   inspection happens. "Rest of the main phase" was itself an understatement here -- nothing in the
   code bounded it to one phase.
2. **An SBA that "runs a bit late" is not cosmetic.** Late enough to cover the window in which the
   game does things is the same as not running.

If a legend-rule-off effect is ever added (Mirror Gallery), or a name-changing legend (Sakashima):
no such card exists in `cards.json` today, so enforcement is unconditional. Adding one means gating
EVERY `EnforceLegendRule` site, not just the new one.

---

## The land drop: three copies, four divergences (2026-07-30)

The land drop existed **three** times: `AIEngine::TryPlaySpecificLand` (the executor's SEARCHED
drop), the `play_land_iter` lambda inside `AIEngine::TryPlayLand` (the executor's GREEDY drop), and
`TurnSolver::PlayLandByName` (the rollout). Unifying the placement core (`src/ai/LandPlay.cpp`,
`PlayLandFromHand`) turned each difference into a named `LandPlayOptions` field instead of a silent
one. Each caller keeps exactly its old behaviour, so the unification is byte-identical; what follows
is what the unification *found*.

### 1. The executor's GREEDY drop did not fire Forbidden Orchard's on-play Spirit — FIXED

`SpawnOpponentSpirit` was called from `TryPlaySpecificLand` (ungated) and from `PlayLandByName`
(gated on `!MTG_LEGACY_SEARCH`), but **not** from `TryPlayLand`. So when a Forbidden Orchard comes
down on the greedy path, the executor gives the opponent no Spirit that turn while the rollout that
scored the line does. The turn-start spawn covers copies already in play, so the effect is a
one-turn delay of the first Spirit — but it lands squarely on the turn the land is played.

Reachability, measured rather than argued: setting `spawn_orchard_spirit = true` on the greedy path
and re-running the smoke suite changes play in `hinata_smoke_d0_s1001 gi10`, and nowhere else.
Hinata2 is the only suite deck running Forbidden Orchard. Two of the three greedy call sites are
depth-0-only (`fold_land = m_lookahead_depth > 0 && is_pre_combat_main`, and the second main); the
third — the post-draw **flood-keep fallback** — is *not* depth-gated, so the path is reachable at
searched depths on a flood turn even though no d3/d5 case exercised it here. For Hinata the Spirits
are not incidental: they are opponent creatures, hence first-class Soulfire-dig / Crackle-discount /
removal targets, which is exactly the quantity a rollout/executor mismatch distorts.

**Fixed** by firing the on-play Spirit on the greedy path too, matching the executor's searched
drop, the rollout's drop, and the ungated turn-start spawn. Measured on both train seed sets, with
the searched depths byte-identical (which is itself the proof the gap was greedy-only):

| mode | case | before | after | delta | per-game |
|---|---|---|---|---|---|
| smoke | `hinata_smoke_d0_s1001` | 7.1050 | 7.0790 | **−0.026** | 31 faster / 6 slower |
| regression | `hinata_regression_d0_s2002` | 7.1830 | 7.1500 | **−0.033** | 31 faster / 7 slower |
| overnight (held-out) | `hinata_overnight_d0_s4004` | 7.1650 | 7.1410 | **−0.024** | 264 faster / 74 slower |
| overnight (held-out) | `hinata_overnight_d0_s5005` | 7.1050 | 7.0820 | **−0.023** | " (all 4 seeds) |
| overnight (held-out) | `hinata_overnight_d0_s6006` | 7.1220 | 7.0915 | **−0.031** | " |
| overnight (held-out) | `hinata_overnight_d0_s7007` | 7.1835 | 7.1570 | **−0.027** | " |

Held-out is **4 seeds improved / 0 worse**, at the same magnitude as the train seeds.

The direction follows from the mechanism: more opponent Spirits means more legal targets, so
Hinata's per-target discount and Crackle with Power's target count both improve (`gi257` 7→5 casts a
lethal Crackle a full two turns earlier). The slower games are d0 variance — the draws diverge from
the first target-count change, so they are physically different games, not like-for-like slowdowns.
Note this is a **correctness** fix, so the metric is informational; the justification is that four
sites modelled the same trigger and one of them silently didn't.

### 2. `greedy_land_name` had drifted from the ranker it documents itself as mirroring — FIXED

`TurnSolver`'s `greedy_land_name` lambda said it "Mirrors TryPlayLand's TH pre-pass + four-pass".
It had drifted in **three** ways, all in the direction of naming a land the executor would not play:

1. the Reliquary pre-pass fired only on `has_draw_until_nonland`, missing `TryPlayLand`'s
   `|| hand_flooding` (hand size > 7) clause;
2. that pre-pass did not skip `m_impulse_no_land` (Apex-exiled) lands;
3. the four-pass did not skip a Karoo bounce land with no other land in play, which the executor
   skips because its mandatory bounce would return itself for a net-zero land drop.

`greedy_land_name` is the **last** tiebreak in the plan comparator, reached only after win-turn,
value and the develop tiebreak have all tied — so a wrong answer silently defaults the search to the
wrong land exactly when it is indifferent, which is the one job this lambda has.

**Fixed** by deleting the hand-rolled mirror and sharing `TryPlayLand`'s ranker outright
(`GreedyLandChoiceIndex`, `src/ai/LandPlay.cpp`). Done in two commits so the risk is separable: the
extraction alone is byte-identical (40/40), which proves it reproduces the executor; pointing the
tiebreak at it is the behaviour change.

The change is **score-neutral by construction and in measurement** — it only moves a tie. Every
changed case kept its exact average (7 configs in regression, 4 in smoke, all `exp == got` on the
avg), with `slower=0 faster=0` and depth 0 untouched (d0 uses the ranker directly, so it never saw
the drift). The line changes are uniform and legible: on a flooded Treasure Hunt turn the drop
becomes Reliquary Tower (`th_regression_d3_s2002` gi83/gi96/gi102, identical hands and draws, same
win turn), and Hinata stops opening on a self-bouncing Izzet Boilerworks. Ground truth was
rebaselined for the digest change only.

### 3. `--claude-play` land-entry choice — NOT a divergence (corrected)

This was reported as a divergence and was **wrong**; recording the correction because the reasoning
error is the reusable part.

The claim was that `g_play_land_entry_chooser` (pay the shock life / reveal to enter untapped?) and
the conservative human `allow_shock_pay` policy are consulted only in `PlayLandByName` — the
rollout — while the realised land comes from the executor's `TryPlaySpecificLand` / `TryPlayLand`,
which never ask. The first half is true. The second half is not: under `--claude-play`
`AIEngine::TakeTurn` takes an **external-chooser branch** (`use_external = m_external_chooser &&
!m_in_rollout`) that applies the human's chosen plan through `TurnSolver::ApplyPlan` →
`ApplyPlanDirect` → `PlayLandByName`, and returns without ever reaching the executor's own land
drop. So in the only mode where a chooser exists, the code that plays the real land *is* the code
that consults it. Eleven saved Anti-Lifegain references contain recorded `land_entry` decisions,
which is the direct evidence.

The lesson: "which function plays the real land" is mode-dependent here, and reading the autonomous
path alone will mislead you. Verify by following the actual call chain for the mode in question
(`RunClaudePlay` → `GameEngine::RunGame` → `AIEngine::TakeTurn` → the external branch), not by
assuming the executor is always the executor.

### 4. The look SOURCE label differs

The executor passes the land's own name to `ScryTop` / `SurveilTop`; the rollout leaves the default
`"Scry"` / `"Surveil"`. The autonomous heuristic ignores the source, so this only affects the reveal
log and the claude-play prompt label.

### Not unified, deliberately

The land **selection** heuristics are not twins and were left alone: the executor's four-pass ranker
(untapped/tapped x multi/any, with a Reliquary pre-pass and the closing-window sub-order) and the
rollout's `SimulateLandPlay` two-pass fallback (multi-colour, then any) are genuinely different
policies. Unifying those is a behaviour change to be measured, not a refactor.
