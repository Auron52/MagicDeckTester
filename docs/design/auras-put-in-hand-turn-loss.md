# Auras loses a turn to the put-in-hand class, and no budget buys it back

**Status:** gi20 FIXED (`e927240a`); **gi428 still open, and it is a DIFFERENT bug.**

The put-in-hand class was never the cause -- it is strictly additive at the search level
(`MTG_LEGACY_SEARCH=1` finds T4 with the class ON); it merely selected a line that exposed an
executor defect. That defect: `bp_play_searched_land` played a continuation's land with the plan's
full triple (`fetch_target`, `land_face`, `rad_mode`) but recorded only `card_name`, and
`replay_recorded` replayed it as `TryPlaySpecificLand(state, a.card_name)` -- front face, no fetch
target. The searched land and the replayed land were different lands. gi20: Boulderloft `{W}`
searched, Branchloft `{G}` replayed, the recorded `Hyena Umbra {W}` stranded in hand, committed T4
kill lost. Fixed by carrying the triple through the record/replay boundary; gi20 is now T4 in all
20 ladder cells.

**gi428 is unchanged by that fix and never emitted `[fd-diverge]`** -- same symptom, different
cause, still to be diagnosed. The sections below were written before the fix and describe the
shared investigation; read them for method, not for gi428's verdict.

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

**Not yet explained:** gi428 shows the same T4->T5 loss and the same legacy-engine recovery, but
does NOT emit `[fd-diverge]`. So either the oracle misses that shape, or gi428's loss has a
different proximate cause. Do not assume the two games are one bug until that is checked.

### Where to look next

The committed class-ON phase for T4 is `pre:spells[Spirit Link,Lion Umbra]{land=<none>}` — a
DEFERRED land drop — whereas the class-OFF phase names a concrete land. Compare what the search
simulated for that phase against what the executor actually did when popping it, starting with
the deferred-land path and with Light-Paws' `aura_cast_tutor_attach` trigger (Spirit Link is an
Aura, so the trigger fires during that phase and its choice of fetched Aura moves the damage).

## Reproducing

```
bash logs/skipA/auras_ladder.sh            # both games, both binaries, depth x budget incl. unlimited
MTG_BP_PUT_IN_HAND=0 MTG_DUMP_WINS=1 build/Release/mtg decks/Auras/Auras.cod \
  --profile decks/Auras/Auras.profile.json --games 1 --seed 2430 --game-index 428 \
  --depth 5 --budget-ms 0 --ignore-play-profile
```

Related: `docs/design/breakpoint-condemnation-status.md`.
