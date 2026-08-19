# Mirrorwing card tournament: eliminate cards one at a time, with evidence

**Status:** IN PROGRESS, started 2026-08-19. The shared apparatus is generating; no test has run yet.
See [[mirrorwing-trick-suite-result]] for the screening work this supersedes, and
`.claude/skills/deck-screening.md` Rule 0a for the apparatus rules it obeys.

## What the user asked for

> "start eliminating cards from contention, because they are superceded by another, each with one
> test per, with analysis and evidence provided (along with logs I can look at for each trial)"

Sequential elimination, each test resolving one slot, on the **original list**, with these cards
FIXED in every arm: **4 Gold Rush, 4 Fists of Flame, 2 Impolite Entrance**. Creature-creation is
held at **3** for now ("maybe we can fiddle with 4x Libation ... at the end").

| test | question |
|---|---|
| 1 | Twinflame vs Luxurious Libation, **including mixes** (3/0, 2/1, 1/2, 0/3) |
| 2 | Ancestral Anger vs Oracle's Restoration ("they clearly fill the same slot") |
| 3 | the Scale the Heights slot, **four-way**: keep Scale / Draught / Entrance / test-2's winner |
| 4 | is Draught worth some number of copies, or all of the draw+pump slot |

Every test runs at **20 life AND 30 life** (2HG). Both life totals go in ONE pooled batch — the
per-job `starting_life` field (`src/core/GameSetup.h`) exists so they share one queue and one tail.

## The three numbers every test reports

User's spec, verbatim: *"percentage change across all games, percentage change across games where
the card was available and better/worse totals."*

1. **% change over ALL games** — how much the swap matters at all. This is the number that licenses
   "I don't care about this tiny change, because <unmodelled effect> outweighs it" — e.g. Impolite
   Entrance's trample, which this engine cannot see.
2. **% change over LIVE games** (both arms drew that card number) — *"The main games that matter are
   the ones where which cards actually matters."* Undiluted.
3. **better / worse GAME COUNTS + margin** — the recommendation, even when the size is negligible.

Implemented in `scripts/tourney_report.py`. Each margin is printed with **σ = margin / √divergent**,
because a fixed count bar means different things at different N: the margin grows with N but its
noise only as √N, so at 10,000 divergent games a margin of 20 is 0.2σ — noise. (The user initially
asked for "20+", then "more like 100"; σ is what makes either interpretable.)

Plus, per test: **≤500 case logs where the card was actually cast or played**, split better vs worse,
with play-viewer repro commands, and a write-up of where each side wins and **what made the losing
combination fall short**.

### Decision rules the user has already given

- **Ties are an answer.** *"If they are really really close in effectiveness, at some point we will
  need to just accept one option even if it is 'within noise'. And this test would tell us it doesn't
  matter all that much, anyway."*
- **Oracle beats Anger on a tie.** *"even if Oracle is just slightly better (within noise) than Anger
  without Draught I would probably take it given that it has upside not encoded in that test, whether
  with Draught or even just for survivability."*
- **Entrance's trample is unmodelled**, so a small measured loss for Entrance may still be a win.

## The apparatus

**One union TABLE over 60 real 60-card decklists** — never a union deck (banned term; see Rule 0a).
`logs/tourney/arms/*.txt` holds the 60 arms: 4 creature-creation ratios x 3 Anger/Oracle splits
(4/0, 2/2, 0/4) x 5 options for the Scale slot (Scale, Draught, Entrance->4, Oracle+2, Anger+2).
Every test in the table above is a subset, so nothing regenerates between tests.

Generated with the feature built for this (`MTG_KEEP_ARM_DECKS`, commit 6a90c8b1): envelope caps are
the per-bucket MAX across arms, every composition no arm can hold is DROPPED, and each surviving cell
carries a 64-bit arm mask.

```
size-7: 529,868 reachable cells of 574,405 envelope (7.8% dropped by 60 arms)
total 1,059,736 cells, K=20, floor R=2 cap R=10, at 20 life
```

Why one table rather than four per-test ones: 529,868 cells vs ~1,240,000 — **2.3x cheaper**, and no
append machinery needed. (Against the *killed* superset-deck table it is only 9% smaller; the win
there was legitimacy, not cost.)

Trimmed to fit the 64-arm mask: the 3/1 and 1/3 Anger/Oracle splits. Widening the mask to 128 bits is
cheap if wanted. Dropping the 2/2 mix would save only 2.5% (the flex slot keeps anger<=6/oracle<=6
either way), so it was kept as a free mixture data point.

## The open question: does 2HG need its own table?

A 30-life table costs ~3.3x a 20-life one (measured: a 30-life GAME costs 3.4x the thread-time, and
the reason is per-turn search cost, not longer games -- see the cost note below). So ~30-40 h. The
user: *"If we can avoid generating the full thing, but still be relatively confident in the results
that would be better ... we need to know this info for the future."*

**Prior evidence says the shortcut is plausible.** `deck-screening.md`: an R=10 table plays 0.032t
worse than a shipped R=60 one, while the **own-vs-foreign fit difference among R=10 tables is only
0.004t** -- an order of magnitude under the 0.03-0.08t effects here. Caveat: that bias has a SIGN
(each table flatters the deck it was fit to, ~0.005t), which is why a SHARED table matters more than
a FRESH one; a pool table refines every arm's partition and has no expected sign.

**Rejected:** comparing against a no-table (heuristic) arm. User: *"I don't think I want to use no
table as that is unrealistic."* Agreement under an unrealistic policy does not license the realistic
one.

**The plan:** a targeted 30-life re-roll rather than a full generation, using the user's structural
prediction:

> *"The biggest difference in the 2HG lists I would expect is for hands that are spell-light to be
> dropped more frequently. One pump will only very rarely be sufficient with that much damage
> needed."*

That makes the flip candidates identifiable A PRIORI (low pump-spell count), not just statistically.
So: re-roll at 30 life only the cells that are spell-light OR borderline at 20 life, measure the
**keep-decision flip rate by spell count**, and include a **random control sample of confident,
spell-rich cells**. If flips cluster where predicted and the control is ~0%, the 20-life table is
sound for 2HG and every future 2HG run gets 3.3x cheaper. If not, generate, knowing it was needed.

`MTG_KEEP_PRIOR_RAW` already implements "thin-sample at the floor, refine only what MOVED", and all
its gates (deck_fp, bucket_fp, K, equiv_seed) match across life totals. The missing piece is
restricting its thin pass to the targeted cells; unrestricted it covers all 1.06M cells at 3.3x
rollout cost (~20 h), which is the thing to avoid.

**Safety fix this exposed** (commit 29df38c5): starting life was NOT in the rollout-config
fingerprint, so a 20-life and a 30-life table were indistinguishable to the carry/resume gates. Now
stamped (`d2/b3/t8/L30`), compared only when both sides recorded it.

## Why a 30-life game costs 3.4x

Measured, 600 games per life total: turns grow only 7.3% (5.10 -> 5.47) while cost grows 3.33x, so
~all of it is **per-turn search cost (3.10x)**. The search looks for the earliest win; at 20 life it
finds one at shallow depth and cuts off, at 30 it must expand far more of the plan tree to conclude
anything. It **saturates** -- marginal ratios 1.97x / 1.69x / 1.40x at 25 / 30 / 40 life.

Related, and counter-intuitive: per game, **going off is CHEAP and failing to go off is expensive**
(magnet-resolved games cost 0.40x no-magnet ones at 20 life). The worst state is never going off and
then losing. A fan-out that does not end the game costs ~1.6x one that does.

## The measurement is a FACTORIAL, not four sequential A/Bs

The 60 arms are the full cross product of the three contested slots, so each of the four tests is a
**marginal** of one run rather than a run of its own:

```
tf{3,2,1,0}lib{0,1,2,3}   x   a{4,2,0}o{0,2,4}   x   slot{scale,draught,entrance,oracle,anger}
```

| test | factor varied | contexts | paired games per comparison (N=10,000) |
|---|---|---:|---:|
| 1 Twinflame vs Libation | `tf` | 15 (ao x slot) | 150,000 |
| 2 Anger vs Oracle | `ao` | 12 (tf x {scale,draught,entrance}) | 120,000 |
| 3 the Scale slot | `slot` | 12 (tf x ao) | 120,000 |

Three reasons this beats running the tests one after another:

- **12-15x the paired games per comparison** for the same wall clock. That is what buys a
  better/worse margin large enough to act on at the user's "more like 100" bar.
- **No test conditions on an earlier test's winner.** Sequential elimination cannot re-examine a
  card that was cut early in the light of what came later; the factorial measures every slot in
  every context.
- **One pooled queue, one tail** (CLAUDE.md's pooling rule). Four runs would be four tails.

Test 2's contexts deliberately exclude the `oracle` and `anger` flex slots: those change the SAME
two cards' counts, so including them would compare a level against itself.

The per-context spread is printed for every comparison and the reporter prints a **MIXTURE
WARNING** when contexts disagree in sign. That is not decoration -- reporting this deck's Draught
slot as a pooled null once hid two large opposite effects.

## Test 4 needs an APPEND pass -- the current table cannot answer it

Test 4 asks whether Draught is worth "some number of copies, or all, of the draw+pump slot". Every
arm here caps Fortifying Draught at **2** (it only ever occupies the Scale slot), so the envelope
itself caps at 2 and every cell holding 3+ Draught was never enumerated. An arm playing 4 Draught
would hit `present=false` and silently fall through to the generic heuristic keep.

So Test 4 runs as the **append** the user asked for: widen the arm set with the Draught-heavy
lists, regenerate with `MTG_KEEP_PRIOR_RAW` carrying the finished table, and only the genuinely new
cells cost rollouts. This is also the natural place to re-admit the 3/1 and 1/3 Anger/Oracle splits
that were trimmed to fit the 64-arm mask. Sequencing it after Tests 1-3 is right anyway: their
winners decide which Draught-heavy lists are worth building.

## The apparatus fixes found while building the run

- **Two arm cards had no `card_scores` entry** in the pool profile: Luxurious Libation and Impolite
  Entrance. `ComputeHandScore` SKIPS an unknown name, so an unscored card is scored as an *empty
  slot*, and that penalty falls only on the arms that play it -- i.e. on one side of Test 1 and one
  arm of Test 3. `tourney_run.py pool_scores` derives them with the same fixed recipe the shipped
  scores came from and merges them in. The keep TABLE is unaffected: its keep/bottom decisions come
  from measured cell values (`best_sub`), never from `card_scores`.
- **Coverage is verified exactly, per arm, before any game runs** (`verify_coverage`): every size-7
  composition each of the 60 decklists can draw must be present in the table. A missing cell does
  not error -- it answers `present=false` and falls through to the generic heuristic on that arm
  only, which is an apparatus that differs *between* arms, the one thing a paired comparison cannot
  survive.
- **The 30-life arms do NOT need their own value leaf.** The jobs run the leaf in ladder mode
  (`value_model: false, ladder_value_leaf: true`), where the model only makes warm-up passes cheap
  and the COMMITTED pass stays pure heuristic -- verified byte-identical under a deliberately wrong
  model at unbounded budget, with residual budget coupling measured at 0.0008t. A 20-life-fitted
  leaf therefore cannot decide a 30-life line. (This does not settle the 30-life KEEP TABLE
  question, which is about mulligans and is still open above.)

## MTG_DUMP_CARDS: why the run does not write traces

The second reported number ("games where the card was available") and the case-log rule ("the card
was cast, or played if it is a land") are both per-game facts about a handful of card numbers.
Answering them from traces costs ~20 KB of JSON per game -- **24 GB and 1.2M files** for this run.

`MTG_DUMP_CARDS=1` instead emits one ~120-byte line per game:

```
[cards] job=tf3lib0_a4o0_scale@L20 gi=1 seen=54,43,39,15,16,40,34,5,24,49,57,44 cast=15,34,43,5,24,16,39,49,54,44
```

Card NUMBERS, not names: every arm inherits one numbering from `tf3lib0_a4o0_scale`, so a number
names the same *slot* in all 60 arms (50-52 = the Twinflame/Libation slot, 53-56 = Anger/Oracle,
57-58 = the Scale slot) -- which is exactly what "did both arms see the swapped slot" has to ask.
The whole 1.2M-game run reads back in ~20 MB of masks. Traces are then re-generated for only the
few hundred games a report shows (`tourney_cases.py`), and each replay is **verified to reproduce
the measured win turn** -- a case log of a different game than the one that was counted would be
worse than none.

Accumulation is gated on the flag and nothing is folded into the play digest; verified inert
(identical digest with the flag on and off).

## How to run it

```
python3 scripts/tourney_run.py plan  --games 10000        # cost arithmetic
python3 scripts/tourney_run.py prep                       # numbering + unscored-card check
python3 scripts/tourney_run.py run   --smoke --games 24    # plumbing only, table optional
nohup bash scripts/tourney_overnight.sh <gen-pid> 10000 > logs/tourney/overnight.log 2>&1 &
```

`tourney_overnight.sh` waits for the generator PID to exit (not for the file to appear -- the
profile is written last), rebuilds, gates on the regression smoke, runs the one pooled batch,
writes `logs/tourney/run/REPORT.md`, then emits case logs for the headline comparison of each test
at both life totals.

Cost, from measured per-game thread-time (0.387 s at 20 life from
`logs/overnight/batch/batch.out`; 3.33x that at 30 life from `logs/tourney/lifecost.*`):
**~279 thread-hours = ~8.7 h wall on 32 cores at N=10,000.**

## State as of 2026-08-19 (post-compact)

- 20-life union table GENERATING: pid 1260187, `logs/tourney/pool/gen.log`, phase=floor,
  ~113 rollouts/s, 512k of ~2.12M floor rollouts at 4,500 s -- ~4 h more for the floor, then
  adaptive refinement to cap R=10.
- Built and validated while waiting: `MTG_DUMP_CARDS` (engine), `scripts/tourney_run.py`,
  `scripts/tourney_report.py`, `scripts/tourney_cases.py`, `scripts/tourney_overnight.sh`.
- Numbering written for all 60 arms; slot alignment verified (50-52 / 53-56 / 57-58).
- NOT yet done: any tournament measurement.
