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
