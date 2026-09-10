# Why "DRAWS DIVERGE" happens — and one case where the diagnosis is wrong

Investigated 2026-09-10 against the real old binary (`0ab355c7`, built in a worktree) for the five
overnight games the audit classified as physically-different rather than like-for-like. All five are
downstream consequences of a **real play-policy change**, not shuffle luck: the opening shuffle is
seeded identically and the mid-game CRN reshuffle (`Library::ShuffleByKey`) behaved exactly as
designed in every case. Four distinct mechanisms, and they are worth telling apart.

## The four real mechanisms

**1. A library-manipulating cantrip cast on a DIFFERENT TURN.** `hinata gi202` — opening hands and
mulligan sequences byte-identical, both lines draw the same card at T2's draw step. Old casts
**Ponder** (`draw_spell`, `cast_reorder: 3` + `draw: 1`) on T2; new defers it to T3 and casts
Ornithopter instead. Ponder REORDERS the top three and draws, so from that point the two lines hold
genuinely different libraries. `hinata gi232` is the mirror image (new casts Ponder on T2, old does
not).

**2. Scry / exile effects, same story.** `hinata gi255` — T1 and T2 identical; old then casts
**Preordain** (`cast_scry: 2`, which BOTTOMS cards) and **Expressive Iteration** (top three: one to
hand, one to bottom, one exiled) on T3 while new casts Hinata. Both genuinely rewrite the library.

**3. The bottoming choice.** `melira gi17` — the audit says KEPT HANDS DIFFER and is right, but the
detail matters: **both lines were dealt the identical 7 on attempt 0 AND on attempt 1, and both
mulliganed exactly once.** The only difference is which card went to the bottom — old bottomed
`Caves of Koilos`, new bottomed `Birthing Pod`. That is a London-mulligan bottoming policy change
(bottoming is decided by clairvoyant rollouts, so any search change can move it), not a different
shuffle.

**4. Nothing at all — the SAME library consumed at a different RATE. This one is MISDIAGNOSED.**
`mirrorwing gi173` is reported as *"a fetch/shuffle resolved differently; physically different from
there on"*. **Mirrorwing's line contains no fetch, no tutor and no shuffle effect whatsoever.** What
actually happened, proved by card numbers:

* NEW T3: hand `{35,40,51,9}` -> casts Ignoble Hierarch (40) and **Oracle's Restoration** (51), whose
  `cast_draw: 1` draws **card 13 (Forest)**, and then plays that same Forest as its land drop -> `{35,9}`.
* OLD T4: draws **card 13 (Forest)** at its draw step and plays it; its own Oracle's Restoration
  (cast a turn later) then draws **card 30** — exactly what NEW draws at *its* T4 draw step.

Identical library, identical order, consumed **one card apart** because the same cantrip was cast one
turn earlier. Hand-size arithmetic shows it independently: OLD T4 draws to 4, plays a land and casts
two spells (-3), and ends on **2**, not 1.

## The cause of the misdiagnosis: `cast_draw` draws are NOT logged

`EffectHandler.cpp` resolves `cast_draw` with a bare
`cp.library.DrawN(def.params.cast_draw, cp.hand)` and **never calls `LogDraw`**, while the turn's
draw step (`GameEngine.cpp`) and the `draw_spell` template both do. So Ponder's draw appears in the
game log and Oracle's Restoration's does not.

`test/explain_game.py` then compares the two logged draw sequences with a naive
`zip(old["draws"], new["draws"])` and reports the first index where the card names differ. An
unlogged draw shifts one line's stream by one, so the tool compares two different game moments and
concludes a shuffle resolved differently. It has no way to know a draw happened.

Two further caveats found while confirming this, both about the same tool:
* **`explain_game.py` runs the games itself as SINGLE-GAME probes.** The batch reuses one AI engine
  per job (`cached_job`), so suite games run with a WARM search memo; a single-game probe is cold.
  Its old-vs-new per-turn diffs are therefore indicative, not the suite's actual line.
* **It pins `--depth D --budget-ms B --ignore-play-profile`.** For a deck whose manifest job carries
  no `depth` (hinata, melira, mirrorwing — their depth comes from the profile's `value_play`), that
  overrides the deck's real search shape.

## Fix options for the logging gap

The obvious fix — call `LogDraw` from the `cast_draw` path — is **not free**: `LogDraw` folds into the
play digest (`FoldStr("D"); FoldInt(card_num)`), so it would change the digest of every game on every
deck running a `cast_draw` card and force a ground-truth rebaseline across the suite.

The zero-churn alternative is a `LogDraw`-style call that appends the `Action` for the log/viewer but
skips the digest fold, making the log complete for humans and diagnostics while leaving every
existing digest byte-identical. That is the recommended route; it is not applied here because it is
an engine change beyond the scope of the question that prompted this investigation.

## "Physically different game" is NOT exculpatory — run the DEPTH/BUDGET LADDER on both binaries

USER, 2026-09-10: *"Even the library manipulation spell cases are only so legitimate. They should get
the same results at a higher budget and depth."* Correct, and testing it changed the verdict. If two
engines disagree about WHEN to cast a cantrip, that is itself a search-quality artifact: with enough
search both should reach the same decision. So the ladder — **both binaries, d5/d7/d9/d11 x rising
budget** — is the test that "different physical game" was letting people skip.

| game | memo ON (new) | memo OFF (old lever) | verdict |
|---|---|---|---|
| `hinata gi202` | 6 at d5, **5 at d7+** | 5 at every depth | costs a turn only at shallow depth — RECOVERABLE |
| `hinata gi255` | 6 everywhere | 5 at d5/b20, **6 at d7+** | NOT a regression; old's T5 was shallow-search luck |
| `hinata gi232` | **6 at every depth** | **5 at every depth** | **REAL, STABLE one-turn loss** |

`gi232` holds at T6 even at **d11 / b10240 — 512x the case budget** — so it is definitively not churn.

**Cause named by hatch sweep: `MTG_MEMO_WIN_ORDERFREE` (`d71b4157`, order-free WIN memo reuse,
default ON).** Setting it to 0 restores T5 at every depth; none of the other levers
(`MTG_FOLD_ACT_SOURCES`, `MTG_FOLD_HAND_CASTS`, `MTG_CAND_DEDUP`, `MTG_LADDER_EMULATED`,
`MTG_ENUM_MEMO`, `MTG_ID_ANYTIME`) moves it. Mechanism in the game: new casts **Ponder on T2** where
old holds it, which reorders its draws; at T5 new's line (Reality Spasm, Irencrag Feat, Crackle with
Power) leaves the opponent on exactly **1 life**, while old's T5 fits an extra Soulfire Eruption and a
second Reality Spasm for -1.

**But the lever is an improvement, measured on the whole deck.** A/B over all 16 hinata /
hinata2hg overnight keys: **better on 8 keys, worse on 0, equal on 8, sum -0.1409**. Every d0 key is
byte-identical, confirming it is search-only. So `gi232` is a genuine but isolated cost paid against
many gains — the rebaseline stands, and this is what "analyze the differences" is supposed to
produce: a named cause and a deck-level verdict, not a shrug at "different physical game".

**Bottoming is the one case that cannot converge by construction** (USER: *"the bottoming decision
on the other hand is an actual different game"*). Changing which card goes to the bottom changes the
library from turn 0, so the two lines are not the same game at any depth. (USER also noted it *might*
match given enough budget and search if lookahead bottoming runs at the same settings — untested,
and low priority.)

## THE UNLIMITED-BUDGET TEST: gi232 IS A BUG, and the guard for it already exists (default OFF)

USER, 2026-09-10: *"the question we have to ask would be 'is this line reachable' at unlimited budget
and sufficient depth. If it isn't we introduced a bug."* Run unbudgeted (`--budget-ms 0` = unlimited,
`FromVirtualMs` treats `<= 0` as unlimited):

| `hinata gi232` | d5 | d7 | d9 | d11 | d20 | d40 |
|---|---|---|---|---|---|---|
| `MTG_MEMO_WIN_ORDERFREE=1` (default) | 6 | 6 | 6 | 6 | 6 | 6 |
| `MTG_MEMO_WIN_ORDERFREE=0` | **5** | 5 | 5 | 5 | 5 | 5 |

**The T5 line is unreachable at unlimited budget at every depth to 40.** That is the bug criterion
met — a lossy prune, not a search-effort tradeoff, and it violates the standing no-lossy-truncation
USER bar (the infinite-budget test). It matters that **the search is CLAIRVOYANT**
(`DecisionProvider.h`: *"this search is clairvoyant"*; rollouts draw the real library via
`library.DrawTop()`), so the engine COULD see the earlier win and still does not take it.

**Root cause, and the code already documents it.** `TurnSolver.cpp` order-free WIN reuse carries a
guard, `MTG_MEMO_ORDERFREE_VERIFIED_ONLY`, **defaulted OFF**, whose own comment says: *"a WIN entry is
stored for any win_turn <= max_turns, so it also carries a LEAF ESTIMATE when the win lies beyond the
node's horizon -- and the greedy rollout is not order-invariant, so a permuted state's estimate is not
this state's. Reusing those order-free lost 3 of 16000 Fluctuator games."* Exactly our failure mode.
Setting `MTG_MEMO_ORDERFREE_VERIFIED_ONLY=1` restores T5 at **every** depth and budget with the memo
still ON — and it also fixes `gi202` and `gi255` at the shipped d5/b20.

## USER RULING 2026-09-10: UNSOUNDNESS IS A DEALBREAKER. Verified-only is now the DEFAULT.

*"I don't want it to be unsound and didn't realize something unsound was introduced. That is
unfortunately a dealbreaker. Just because greedy was also unsound doesn't mean we can accept a
different version of unsoundness."* And on what an acceptable cost looks like: *"it only working at
unlimited is also not acceptable, but it may potentially require a high budget. How this scales
should always be done based on using our budget as well as possible."*

`MTG_MEMO_ORDERFREE_VERIFIED_ONLY` now defaults **ON**; `=0` restores the unsound reuse for A/B only.
All three hinata games win T5 at the shipped d5/b20, and gi232 is correct at every depth including
unlimited.

**The cost is CHURN, not lost reachability — which is the distinction that matters.** Smoke under the
sound default: 15 keys changed, searched slower=5 / faster=0, d0 untouched, makespan 80s -> 199s.
**All 5 slower games classify as CHURN** (each recovers to its old win turn at 4x and 16x budget).
So the correct line stays reachable at a higher-but-finite budget — the acceptable failure mode —
whereas the unsound version's wrong answer was unreachable at ANY budget. Those are categorically
different, and only the second is a bug.

## Recovering the throughput SOUNDLY (designed, not yet built)

The prize is precisely known: **`memo ON + verified-only` is byte-identical to `memo OFF` on all 16
hinata keys**, so order-free reuse of VERIFIED entries essentially never fires — 100% of the memo's
benefit sat in the unverified entries. Recovering it therefore means making unverified entries usable
*soundly*, not tuning the guard.

**The route: re-anchor the cached line by card IDENTITY and REPLAY it, instead of importing its
estimate.** A replay that reaches a win at turn N is a *proof for this state*, so it is sound by
construction; the estimate never has to be trusted. Feasibility is good because the line is already
mostly identity-anchored: battlefield sources use `sac_source_id = card.m_number` (stable), casts
carry `card_name`, and only `hand_index` is positional — and the canonical key guarantees the two
states hold the same multiset, so a permutation mapping exists (duplicate copies being
interchangeable is exactly what the hand-cast fold established).

**Shape it as a fallback wave** (USER's suggestion): on an order-mismatched WIN entry, (1) try the
re-anchored replay — cheap, sound, and it verifies the entry for this state; (2) fall back to the
full fresh search only when the replay fails. Today step (2) is the *only* path, which is where the
1.34x goes.

## (superseded) The measurement that prompted the ruling

A/B over all 16 hinata / hinata2hg overnight keys:

| | keys better | keys worse | sum | cost |
|---|---|---|---|---|
| guard ON (sound) vs default | **0** | **8** | **+0.1409** | **1.341x slower** |

And the decisive detail: **`memo ON + verified-only` is byte-identical to `memo OFF` on all 16 keys.**
So the order-free WIN memo's ENTIRE measured quality benefit on hinata comes from reusing precisely
the entries the code calls unsound; the sound half contributes nothing measurable.

The tension is real and is a judgement call, not a fact: the unsound reuse is **wrong at unlimited
budget** but **pays for itself at finite budget**, because being 1.34x cheaper buys more search per
budget than the occasional bad reuse costs. Flipping the default to sound would cost ~+0.14 t across
hinata. Recorded for the user; NOT changed here.

## RESOLVED — the residual is recovered by a BUDGET-GATED WAVE (2026-09-10)

The recovery sketched above ("re-anchor the cached line on card IDENTITY and REPLAY it") was built
and MEASURED, and it does not pay — the node's own plan 0 already yields an equal-or-better
incumbent, so a replayed line saves nothing. What does pay is a gate rather than a rule, because the
twin-agreement probe showed the reuse is **right 99.70% of the time** and its damage is a 0.16% tail
that cannot be told apart WITHOUT SEARCHING. Shipped as `MTG_FSL_OF_WAVE` (default ON): a node may
take a permuted twin's answer only when re-searching would cost a serious share of the budget still
REMAINING, so the reuse rate decays continuously to zero and the gate never opens at an unlimited
budget. gi232 holds win=5 at every share and budget, with `reused` 294 -> 5 -> 0 as budget rises.
Full argument, sizing table and counters: **`order-free-reuse-wave.md`**.

## THE SECOND BUG NO LONGER REPRODUCES (re-measured 2026-09-10, after the sound memo + wave)

Re-run on the recorded cases with the current engine (verified-only + split keys + the reuse wave),
unbudgeted and depth-pinned exactly as the original ladder was (`--depth D --budget-ms 0
--ignore-play-profile`):

* **Depth axis, 18 cells** — gi202 / gi232 / gi255 x seeds 6006, 7007 x d5, d7, d9: **every cell
  agrees within its game.** No "deeper is worse" anywhere.
* **Budget axis, 24 cells** — gi202 / gi255 x seeds 6006, 7007 x b20, b80, b320, b1280, b10240,
  unlimited: **flat throughout.**

The most likely explanation is that the non-monotonicity was a downstream SYMPTOM of the unsound
order-free reuse rather than an independent defect: importing a permuted twin's leaf ESTIMATE fires
at some depth/budget combinations and not others, which is exactly the shape "worse, then better
again" has. (The original note recorded it as memo-independent on the grounds that both
`MTG_MEMO_WIN_ORDERFREE` arms agreed — but that lever is only one of the paths into the reuse, and
the arm comparison predates both the verified-only default and split keys.)

**Not the same as proving the search monotone.** This says the recorded repro is gone, on 42 measured
cells. `MTG_FS_HORIZON_EXIT=0` was added as a standing A/B hatch for the mechanism that would be the
prime suspect if it returns — the first-verified-win exit, whose soundness rests on an ID premise
(*"a pass runs only after every shallower pass found no win"*) that a single pass at a committed depth
does not supply. On the three games that sit at a later win (s6006 gi255, s7007 gi232, s4004 gi232),
turning the exit off changes nothing at d7/unlimited, so the premise holds where it has been tested.

## (historical) the non-monotonicity as first recorded

Independent of the memo (both arms identical), unbudgeted:
* `gi202`: d5 -> **5**, d7/d9/d11/d20 -> **6**. A DEEPER search is worse.
* `gi202` at d5, memo off: b20 -> 5, b320 -> **6**, b10240 -> 6, unlimited -> **5**. Worse, then better.
* `gi202` at d7, memo off: b20/b80/b320 -> 5, then **b1280 onwards -> 6**.
* `gi255`: d5/b20 -> 5, but unlimited at every depth -> 6.

`SearchBudget.h` states the opposite as an exact property: *"The 'a deeper pass is never worse'
property becomes exact rather than statistical."* With a CLAIRVOYANT search and an earliest-win
objective, more search must never lose a found win. No lever moves it (`MTG_ID_ANYTIME`,
`MTG_ESCALATION_GATE`, `MTG_COMMIT`, `MTG_ESC_SINGLE`, `MTG_ENUM_MEMO`, `MTG_BIG_SOLVE_MEMO`,
`MTG_LEAF_CACHE` all leave gi202 d7/b1280 at 6). The d5 vs d7 lines diverge at Ponder's
`cast_reorder` on T2 — d5 reorders to draw Island and kills on T5; d7 reorders to draw Mountain and
wins T6 with huge overkill (opp -34), which hints the deeper pass is preferring a bigger-damage line
over an earlier win. UNDIAGNOSED — this is the next thread.

## What this does NOT change

CRN is working. `ShuffleByKey` orders the live library by `splitmix64(seed, m_number)`, so removing a
card leaves every other card's relative order intact, and the reshuffle seed
(`SearchShuffleSeed(game_seed, state.search_count)`) is keyed on the per-game shuffle ORDINAL — two
lines that reshuffle at the same ordinal stay aligned. No case among the five was a CRN failure.

The practical consequence for A/B judgement is unchanged: cases 1-3 ARE physically different games
and cannot be judged per-game, only in aggregate (hinata: 26 better vs 3 worse, net -25 turns — the
new policy is clearly better there). Case 4 is not physically different at all and should never have
been counted as variance.
