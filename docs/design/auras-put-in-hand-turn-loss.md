# Auras loses a turn to the put-in-hand class, and no budget buys it back

**Status:** gi20 FIXED (`e927240a`); **gi428 still open, and it is a DIFFERENT bug IN A DIFFERENT
LAYER** -- an executor fix cannot touch it. Post-fix tiers: smoke **4 better / 0 worse**,
regression **23 better / 3 worse** (3 = `hinata gi103` churn + gi428 counted at d3 and d5).

The put-in-hand class was not the cause OF gi20 -- for that game it is strictly additive at the
search level (`MTG_LEGACY_SEARCH=1` finds T4 with the class ON); it merely selected a line that
exposed an executor defect. **That sentence does NOT generalise to gi428**, where the class costs
the legacy search a turn too; see the bottom section. That defect: `bp_play_searched_land` played a continuation's land with the plan's
full triple (`fetch_target`, `land_face`, `rad_mode`) but recorded only `card_name`, and
`replay_recorded` replayed it as `TryPlaySpecificLand(state, a.card_name)` -- front face, no fetch
target. The searched land and the replayed land were different lands. gi20: Boulderloft `{W}`
searched, Branchloft `{G}` replayed, the recorded `Hyena Umbra {W}` stranded in hand, committed T4
kill lost. Fixed by carrying the triple through the record/replay boundary; gi20 is now T4 in all
20 ladder cells.

**gi428 is unchanged by that fix and never emitted `[fd-diverge]`** -- and it is not an executor
bug at all. It is a SEARCH-layer loss; see "gi428 IS A DIFFERENT LAYER" at the bottom, which also
retracts two claims made about it above. The sections below were written before the fix and
describe the shared investigation; read them for method, not for gi428's verdict.

**Found:** 2026-09-19, while running the pre-push gate for the breakpoint-condemnation branch.

## The observation

Two Auras games win a turn later once `MTG_BP_PUT_IN_HAND` is open:

| case | game | origin tip | with the class open |
|---|---|---|---|
| `auras_smoke_d3_s1001` / `d5` | gi20 (`--seed 1021 --game-index 20`) | T4 | T5 |
| `auras_regression_d3_s2002` / `d5` | gi428 (`--seed 2430 --game-index 428`) | T4 | T5 |

Each is ONE physical game seen at two depths, so the suite's "3 searched slower" is really two
games plus `hinata gi103` (which classifies as benign churn — it recovers to T5 at 4x and 16x).

## It is not churn, and not a width problem

`classify_turn_later.sh` says PERSISTS at 4x and 16x the case budget. The full two-binary ladder
(`logs/skipA/auras_ladder.sh`) says the same across **depth 3/5/7/9 × budget 10/40/160/640ms and
UNLIMITED (`--budget-ms 0`)**: origin wins T4 in all 20 cells, the class-open build wins T5 in all
20 cells, for both games. It never converges.

Widening every plausible window (`MTG_TUTOR_WIDTH`, `MTG_TT_PUT_WIDTH`, `MTG_SCRY_WIDTH` to 999,
`MTG_BP_WAVES=0`) does not recover it either, so it is not the fixed-width-window crowding that
budget cannot buy back.

**Hatch sweep names the lever unambiguously.** At d3/b10 on both games: defaults → T5;
`MTG_BP_PUT_IN_HAND=0` → T4; `MTG_BP_CONDEMN_NOWIN_TRUNC` at either value → T5. The no-win-memo
work is not involved.

## Where it diverges: a legal, affordable earlier win is not taken

Diffing the two logs by `(turn, phase)` — same binary, one flag — the games are identical through
T2 and diverge at T3 MAIN_1, with three lands on the battlefield in both:

```
class OPEN : PLAY_LAND Branchloft Pathway; CAST Kor Spiritdancer {1}{W}; CAST Slippery Bogle {G}
             hand after: 42, 7, 26          <- card 7 is Armadillo Cloak, still in hand
class OFF  : PLAY_LAND Branchloft Pathway; CAST Armadillo Cloak {1}{W}{G}
             hand after: 42, 37, 54, 26
```

Both spend all three mana; Armadillo Cloak is demonstrably still castable in the open-class line
(it is sitting in that line's hand afterwards). The Cloak line triggers Light-Paws, Emperor's
Voice (`aura_cast_tutor_attach`) and kills on T4 at opponent −10. The open-class line instead
builds to a T5 kill at opponent **−35**.

So the search, **clairvoyant and at unlimited budget**, did not take an earlier win that was
legal and affordable at that node.

There are two ways that can happen, and they are opposite verdicts: the Cloak cast was **absent
from the candidate list** (a lossy prune — the reachability dealbreaker, the hinata gi232 class),
or it was **present and out-ranked** (a scoring defect — the open hinata gi202 non-monotone
class, which would mean the class merely EXPOSES a pre-existing bug rather than causing one).
**It turned out to be NEITHER** — see ROOT CAUSE below; the search is fine and the executor is
not. The measurement rules out the first outright (the candidate is enumerated identically in
both arms). Three traps on the way there, recorded because each produced a wrong or worthless
answer:

* **"Still in hand afterwards" proves the cast was *playable*, not *enumerated*.** Do not treat
  it as evidence of either verdict. It was written into an earlier draft of this doc as though it
  proved "out-scored, not pruned"; it proves no such thing.
* **`MTG_BF_DUMP` is silent on this deck and that is not informative.** It fires 6 times on a Snow
  game and **zero** times on Auras at any width threshold including `=1`, because Auras' main
  phase never reaches `SolveWithLookahead` (`bf_census decisions=0` for Auras vs 266 for Snow).
  Reaching for it and finding nothing says nothing about the candidate list.
* **A fall in total CONSIDERATIONS is not a fall in CANDIDATES.** Opening the class lowers total
  harvests (1196 -> 961 actions) while leaving the winning card's own per-site counts unchanged.
  Harvest counts describe how much tree got walked; they do not say what was on offer at a node.
  Reading one as the other is what produced the retracted "it is a PRUNE" verdict.

### The position admits T4 with the class OPEN

`MTG_DUMP_EWINS` / `MTG_DUMP_EWINS_TURN` certify the earliest win from a given turn. At **turn 1,
turn 2 and turn 3 — the turn the two lines diverge — both arms report `"earliest":4`**, byte
identical. So the T4 win is not something the class destroyed by reshuffling the game into a
different physical shape; it is still there in the class-open world at the decision point.

Be careful with that instrument: `EnumerateEarliestWins` is the clairvoyant LABEL path, and this
repo has repeatedly caught agents reading label-path breadth as play-path capability. The
certificate alone would only prove the win exists, not that the search can express it. What
closes that gap is the other arm: **`PUT_IN_HAND=0` play actually realizes T4 from the identical
state**, so the play path demonstrably *can* produce this win here.

Put together: the position admits T4, the play path can reach T4, and opening a strictly-additive
class makes the play path stop reaching it at every depth and budget tested. **"Physically
different game" is definitively ruled out** — which is what the harness label claimed, twice.

## The trade, measured

The class is a clear deck-level win for Auras. Per-game, against committed ground truth:

| key | better | worse | net turns |
|---|---|---|---|
| `auras_regression_d3_s2002` | 6 | 1 | −5 |
| `auras_regression_d3_s3003` | 4 | 0 | −4 |
| `auras_regression_d5_s2002` | 6 | 1 | −5 |
| `auras_regression_d5_s3003` | 4 | 0 | −4 |

All four Auras keys improve their aggregate (−0.0080 to −0.0100). Across the whole regression
tier the branch is **better=22 / worse=3 / net −19 turns**, with every one of the 5 score-moving
keys moving the right way and none moving the wrong way.

**Re-measured after the gi20 fix (`e927240a`), 2026-09-19.** The fix's blast radius is surgical:
it moved exactly **two smoke keys** (`auras_smoke_d3/d5_s1001`, each by the single game gi20,
T5 -> T4) and **one regression key** (`auras_regression_d5_s3003`), all in the better direction;
every other changed key is byte-identical to the pre-fix run. Post-fix totals:

| tier | better | worse | distinct physical games worse |
|---|---|---|---|
| smoke | 4 | **0** | — |
| regression | 23 | 3 | `hinata gi103` (churn, recovers at 4x/16x) + `auras gi428` (d3 and d5) |

So gi428 is the ONLY unexplained regression left on the branch.

## Why this is written down rather than decided

The repo's rebaseline bar wants a verdict per difference, and the two precedents point opposite
ways:

* `hinata gi232` was accepted as *"a named, isolated cost"* inside a deck-level win — the shape
  here exactly.
* But the criterion that settled gi232 was **reachability at unlimited budget**, and the user's
  ruling there was that failing it means *"we introduced a bug"*, with unsoundness a dealbreaker
  that cost is no argument against. These two games fail that criterion.

With the earliest-win certificates in hand, the reachability bar is the one that applies: the
position admits T4 with the class open, the play path reaches T4 with the class closed, and no
depth or budget recovers it with the class open. That is a real loss of a reachable win, not a
physically different game and not budget churn.

## ROOT CAUSE: a commit-the-line REPLAY divergence. The class is additive; the executor is not

**The put-in-hand class is strictly additive at the SEARCH level, and the one-line proof is the
legacy engine.** `MTG_LEGACY_SEARCH=1` routes decisions through `SolveWithLookahead` instead of
the default commit-the-line executor:

| arm | commit-the-line (default) | `MTG_LEGACY_SEARCH=1` |
|---|---|---|
| class ON | **T5** | **T4** |
| class OFF | T4 | T4 |

With the class OPEN the legacy search still finds T4. Opening the class loses nothing that the
search can see. Everything below is about the executor.

**What the default engine actually does** (`MTG_FD_TRACE=1`). At T1 it computes and COMMITS a
four-phase line, then pops one phase per turn instead of re-searching:

```
class ON : [fd] T1 LINE win=4 ... | pre:spells[Kor Spiritdancer,Slippery Bogle]{land=Branchloft Pathway}
                                  | pre:spells[Spirit Link,Lion Umbra]{land=<none>}
           [fd] T1 line win=4 searched_depth=4 verified=1 refuted_full=0 phases=4
           [fd] T1..T4 pre=1 POP committed ...          <- all four phases replayed, no re-search
           [fd] T5 LINE win=5                           <- the T4 kill never happened; re-search wins T5
class OFF: [fd] T1 LINE win=4 ... | pre:spells[Armadillo Cloak]{land=Branchloft Pathway}
                                  | pre:spells[Lion Umbra,Slippery Bogle,Hyena Umbra]{land=Forest}
           [win] wt=4                                   <- prediction realised
```

The class-open line predicts `turn=4 pre opp_life=-3` and execution delivers opponent at 6 — a
~9 damage shortfall against a line the search marked `verified=1`.

**The engine's own fidelity oracle names it.** `MTG_FD_ORACLE=1` on gi20:

```
[fd-diverge] seed=1021 realized_win=5 predicted_win=4 proven_at_turn=1 leaf_est=none
```

`leaf_est=none` is the load-bearing field. Per the oracle's own comment, `leaf_est == predicted_win`
would mean the "verified" win was the value leaf's guess landing inside the horizon (which
`fd_verified` cannot distinguish from a real simulated win), whereas **`none` means the leaf
claimed no win, so the divergence is a genuine commit-the-line REPLAY failure**. The T4 win was
SIMULATED, committed, and then did not reproduce on replay.

**It is not the value leaf.** Auras ships `Auras.value.json`, and sidecar PRESENCE activates the
hybrid, so that was the obvious suspect. Re-running against a copy of the deck directory with the
sidecar absent: gi20 still goes T5 with the class on, still emits `[fd-diverge]`, and the
`leaf_est` field flips `4 -> none` — i.e. removing the leaf removes the ambiguity and leaves the
replay failure standing. With the leaf present the same game reports
`leaf_est=4 [WIN WAS A LEAF ESTIMATE, NOT SIMULATED]`, so BOTH failure modes are live on this
deck; the leaf one is a second, independent problem.

**Ruled out, each by measurement:**

| suspect | evidence it is not the cause |
|---|---|
| budget / breakpoint cost | `id_pass aborted=0` in BOTH arms at unlimited budget; the class-open arm uses FEWER units (1225 vs 1660) for the worse answer |
| a candidate prune | Armadillo Cloak considered identically: `enum.m1.fs3` 42 vs 42, `fs3.bpcont` 90 vs 90; at the root the class considers MORE (50 vs 32 actions) |
| condemnation | off for this deck; and `NEW_OPTION=0`, `NOWIN_TRUNC` either value, `TRUNC_COMPLETE=0` all still T5 |
| the value leaf | divergence persists with the sidecar absent |
| search depth / bp depth | `--depth` 3/5/7/9 and `MTG_BP_DEPTH` 2/3/5 all still T5 |
| width windows | `TUTOR`/`TT_PUT`/`SCRY_WIDTH`=999, `BP_WAVES=0` all still T5 |
| cast ordering | `MTG_AURA_CAST_ORDER` 0 and 1, `MTG_BP_CANDS_ORDER=0` all still T5 |

**So the class's only role is SELECTION: it changes which line looks best, and the line it selects
happens to be one that does not replay faithfully.** The defect is the replay divergence, which is
pre-existing and independent of the class — it is simply not exercised on this game until the
class points the search at that line. This is the same family as
`docs/design/`-adjacent prior art on a DEVIATION leaving the committed line stale.

**gi428 was checked and it is NOT this bug.** See the next section.

## gi428 IS A DIFFERENT LAYER: the search never finds T4, and the executor is faithful

Measured 2026-09-19 on the fixed binary (`e927240a`), `--seed 2430 --game-index 428 --depth 5
--budget-ms 0 --ignore-play-profile`.

**The oracle is silent because there is nothing to diverge.** With the class open the search's own
committed line says:

```
[fd] T1 LINE win=5 | pre:<pass>{land=Horizon Canopy} | pre:spells[Slippery Bogle,Gryff's Boon]{land=Plains}
                   | pre:spells[Kor Spiritdancer]{land=Brushland} | pre:spells[All That Glitters]{land=Branchloft Pathway}
                   | pre:spells[Rancor,Light-Paws, Emperor's Voice]{land=Brushland}
[fd] T1 line win=5 searched_depth=5 verified=1 refuted_full=0 phases=5
```

It **predicts T5 and delivers T5**. gi20 predicted T4 and failed to replay it; gi428 never predicts
T4 at all. `[fd-diverge]` not firing is the oracle being CORRECT, not the oracle missing a shape.

With the class closed, same game, same depth, same unlimited budget:

```
[fd] T1 LINE win=4 | pre:<pass>{land=Horizon Canopy} | pre:spells[Kor Spiritdancer]{land=Plains}
                   | pre:spells[Gryff's Boon,All That Glitters]{land=Branchloft Pathway} | pre:spells[Rancor]{land=Branchloft Pathway}
[fd] T1 line win=4 searched_depth=4 verified=1 refuted_full=0 phases=4
```

**`searched_depth` is the tell.** Iterative deepening stops the OFF arm at depth 4 because it has a
win there. The ON arm ran on to depth 5 -- so at depth 4 it found NO win, at unlimited budget. The
T4 line left the reachable set.

### RETRACTION: the legacy engine does NOT recover gi428

This doc previously said gi428 showed "the same legacy-engine recovery". It does not. The 2x2:

| | commit-the-line (default) | `MTG_LEGACY_SEARCH=1` |
|---|---|---|
| class OFF | **T4** | T5 |
| class ON  | T5 | **T6** |

The class costs BOTH engines exactly one turn. That is the strongest single argument that gi428's
defect lives in the shared search/apply layer and not in the executor -- and it is why the gi20 fix,
which is purely an executor record/replay repair, does nothing for it.

### Ruled out, each with its power checked

| suspect | evidence | power check |
|---|---|---|
| a candidate prune | `All That Glitters` considered **16,903** times with the class ON vs **1,772** OFF | n/a -- the count moved |
| search effort | `--budget-ms 0`; the ON arm does **56,357** considerations vs 5,110, for a WORSE answer | n/a |
| the solve memo | `MTG_SOLVE_MEMO=0` still T5 | trace differs from default -> arm had power |
| the enum memo | `MTG_ENUM_MEMO=0` still T5 | trace differs from default -> arm had power |
| the no-win memo | `MTG_FS_NOWIN_CACHE=0` byte-identical | `MTG_NOWIN_VERIFY_POISON=1` DIFFERS -> the cache is live here, so the inert arm is a REAL negative |

More search, at unlimited budget, considering the winning card ten times more often, returning a
worse answer. That is not effort and not a prune: opening the class changes which lines are
**constructible**.

### THE CERTIFICATE MOVES: the class costs a win in the CLAIRVOYANT ENUMERATOR, not the search

This is the sharpest result on gi428 and it reframes the bug. `MTG_DUMP_EWINS_TURN` runs
`EnumerateEarliestWins` -- the clairvoyant LABEL path, not the play search. At **turn 1**, before
the two arms have diverged at all (both pass, both play Horizon Canopy, identical state):

| arm | earliest win from T1 | from T2 | from T3 |
|---|---|---|---|
| `MTG_BP_PUT_IN_HAND=0` | **4** | **4** | **4** |
| class open | **5** | **5** | **5** |

A position's earliest win cannot depend on a play-search flag. It does. **Opening the class makes a
clairvoyant enumerator fail to find a win it otherwise finds, from an identical position.** That is
the reachability bar failing at the ENUMERATION level, and it is why no budget, depth, width, cap or
memo touches it -- there is no amount of effort that recovers a line the enumerator cannot express.

**Contrast with gi20, which is what proves these are two different bugs.** On gi20 both arms certify
`"earliest":4` byte-identically; the position admitted T4 with the class open and the executor threw
it away on replay. On gi428 the certificate itself moves. Same symptom, opposite layer.

**This is also the cheap repro.** The turn-1 certificate reproduces the defect without playing the
game out, so the next investigator does not need the full five-minute unlimited-budget run:

```
MTG_DUMP_EWINS=1 MTG_DUMP_EWINS_TURN=1 build/Release/mtg decks/Auras/Auras.cod \
  --profile decks/Auras/Auras.profile.json --games 1 --seed 2430 --game-index 428 \
  --depth 5 --budget-ms 0 --ignore-play-profile
# class open -> "earliest":5 ; add MTG_BP_PUT_IN_HAND=0 -> "earliest":4
```

Since search, label enumerator and executor all apply plans through the same `apply_one`, and the
site-10 arming lives there (`TurnSolver.cpp` ~26174), the shared apply path is where to look.

### Ruled out on the CERTIFICATE harness, with power checks

| arm | certificate | power |
|---|---|---|
| `MTG_LABEL_NOWIN_CACHE=0`, `MTG_LABEL_GOFF_WIDTH=999`, `MTG_LABEL_HORIZON_WIDTH=999`, all three | 5 | **byte-identical -- NO POWER.** Not negatives. The label path's own knobs do not reach this. |
| `MTG_CONDEMN_M1_BP=0` (default is ON -- `EnvOn(..., true)`), `MTG_CONDEMN_SEARCH=0`, `MTG_CONDEMN_ALL_TURNS=0`, all three | 5 | **byte-identical -- NO POWER.** Condemnation is genuinely inert on this game. The earlier claim that "condemnation is off for this deck" is CORRECT, but nothing above had established it -- those tests moved sub-rules (`NEW_OPTION`, `NOWIN_TRUNC`, `TRUNC_COMPLETE`), never the master gate. |
| `MTG_BP_SEARCH` 4 / 8 / 16 (continuation width; default 2) | T5 | differs -> had power. A REAL negative: width is not the cause. |
| `MTG_PLAN_SPACE_CAP=0`, `MTG_SOLVE_SPACE_CAP=0`, both, `MTG_NO_GROUP_CAP=1` | T5 | differs -> had power. REAL negatives: the position caps are not the cause. |
| `MTG_EXEC_DROP_REPLAN=1` | T5 | re-planning every turn does not recover it either. |

### Two code facts to start from next time (observed, NOT yet proven causal)

* **Kor Spiritdancer's implementation assumes the opposite of what the class asserts.** Its
  `cards.json` note reads *"the draw fires in FireOnCastTriggers -- always drawn (card advantage),
  to hand for later turns (no same-turn re-solve)"*. `draw_on_aura_cast: true` plus
  `MTG_BP_PUT_IN_HAND`'s `HandGainedACard` test means every Aura cast in this deck now arms a
  deferred continuation -- precisely the same-turn re-solve the card says it does not do. The T4
  line needs `Gryff's Boon` AND `All That Glitters` in ONE turn with Spiritdancer already out, so
  it is exactly the shape that arming splits.
* **Site 10 has no `node_owns_site(10)` consumer.** Sites 3/5/6 each have one (`bp_truncate`);
  site 10 arms (`TurnSolver.cpp` ~26174) but nothing partitions on it, and `BpNodeSites()` cannot
  return bit 10 under any flag. **A trap recorded so it is not repeated:** adding bit 10 to that
  mask behind a new flag is a NO-OP -- byte-identical output -- because there is no consumer to
  read it. That is a no-power arm, not a negative result.

## Reproducing

```
bash logs/skipA/auras_ladder.sh            # both games, both binaries, depth x budget incl. unlimited
MTG_BP_PUT_IN_HAND=0 MTG_DUMP_WINS=1 build/Release/mtg decks/Auras/Auras.cod \
  --profile decks/Auras/Auras.profile.json --games 1 --seed 2430 --game-index 428 \
  --depth 5 --budget-ms 0 --ignore-play-profile
```

Related: `docs/design/breakpoint-condemnation-status.md`.
