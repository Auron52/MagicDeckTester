# Goblins tutor investigation — handoff

Self-contained brief for a fresh agent. Written 2026-08-05 because the current line of work reached
"no fix left" and the user believes **we are likely missing something**. Read this instead of
re-deriving; every dead end below cost real compute.

Branch `phase-1-2-deck-analyzer`, head `960a2a9`. Engine is byte-identical to committed ground truth
with all flags at their defaults (smoke 27/27, regression 45/45). Full narrative:
`docs/design/goblins-enabler-worse-games.md` rounds 12–17.

---

## 1. The original symptom

`s3003 gi101` (Goblins) won on turn 6 where a wider tutor axis won on turn 5. Root-caused fully:

* Three Tree City taps for `{C}` in base mode, so playing it T2 leaves `{R}{R}{C}` on T3 and Goblin
  Chainwhirler (`{R}{R}{R}`) uncastable. **The deciding move is the T2 LAND DROP**, not the fetch.
* The winning line is T2 Mountain → T3 Chainwhirler → T4 Three Tree City + Matron (fetch Siege-Gang)
  + Goblin Lackey → T5 Lackey connects, puts Siege-Gang in free, and Three Tree City's `{2},{T}`
  per-Goblin red pays four sac activations for exactly 8.
* To value the T2 land the search must see the T4 fetch, and Siege-Gang sits at natural rank 8 in the
  binding lookahead state → outside a 6-wide window → the line is never costed.
* Binding state pinned exactly with `MTG_GOBLIN_RESERVE_TURN` / `MTG_GOBLIN_RESERVE_NEXT`: **turn 4,
  `mana_next` = 4** — the T4 node *before its land drop*. T4 is ranked twice (pre- and post-land) and
  the enumerator binds on the pre-land copy, where `mana_next` is one short so `{3}{R}{R}` reads
  `t=2` instead of `t=1`.

gi101 is **already fixed in shipped code** by the value reserve. This is about the latent defect.

## 2. The genuine engine defect (real, located, and NOT worth enabling)

`EnumeratePlansWithLand` builds a post-land `copy` per land candidate and enumerates base plans on
it, so each plan's **base** tutor target is ranked correctly. The post-dedup axis fan-out then calls
`TutorCandidates(state, ...)` on the **pre-land turn-start state**, cached by card name alone. Its
comment states the false premise outright: "the candidate list depends only on `state`".

Consequences: (1) for any plan that plays a land the ranking sees one less mana than the plan has;
(2) base and variants come from two different rankings, and the shared list's own rank-0 is skipped
because the loop starts at `c=1`.

Fixed behind `MTG_TUTOR_AXIS_POSTLAND=1`. It recovers gi101 with the reserve off. **It measures +18.0
held-out (0 better / 18 worse)**, hence default-off.

## 3. Everything tried, with numbers

Held-out = Goblins overnight, 8,000 searched games + 12,000 d0, vs committed GT. **d0 was 0.0 in
every single arm below** — no change ever moved the greedy top pick.

| arm | held-out | notes |
|---|---|---|
| baseline (shipped) | 0.0 | still the best arm |
| value-reserve eviction "fixed" (`..._RESERVE_FIX=1`) | +20.0, 0 better | the bug is load-bearing |
| `MTG_GOBLIN_PENDING_LAND=1` | +7 to +9, 0 better | credit the unused land drop in `mana_next` |
| `MTG_TUTOR_AXIS_POSTLAND=1` | +18.0, 0 better | the root defect actually fixed |
| ↳ + `MTG_GOBLIN_DISC_T1` refit sweep (85→38) | saturates +10, **0 better in all 6 arms** | not a mis-tuned constant |
| tempo model (damage before projected kill) | fetch regret +7 vs shipped +4 | removed; failed its own acceptance test |
| `MTG_GOBLIN_PLAN_AWARE=1` (plan-exact inputs) | +2.0, 3 better / 5 worse | first two-sided result |
| ↳ + postland | +9.0 | plan-awareness halves postland's cost |
| `MTG_GOBLIN_PLAN_AWARE=2` (union w/ near-future) | +12.0, 1 better / 13 worse | over-counts; see round 17 |
| `MTG_TUTOR_AXIS_REBASE=1` | **+0.0, 0/0 — inert** | base and axis never disagree at rank 0 |

Recurring pattern, from four independent directions: **making the deployment projection more accurate
always loses.** The pessimistic reading acts as a tempo prior that suits goldfishing.

## 4. The measurement that closed it — and its assumptions

`test/goblins_tutor_truth_table.py` + `MTG_TUTOR_FORCE_CARD="<name>"` build a **name-keyed,
model-independent** table of the real win turn of fetching each candidate (1,600 games scanned, 429
with a searched tutor, 6,864 probes, `--budget-ms 0` so it is deterministic).

```
model                     total regret   optimal picks
SHIPPED                        +4         425/429 = 99.1%
SHIPPED + postland             +6         98.6%
PLAN-AWARE                     +5         98.8%
PLAN-AWARE + postland          +5         98.8%   <- postland costs ZERO on fetch quality
```

**Headline: the shipped ranking is 99.1% optimal and total headroom is 4 turns across 429 games.**
That is what closed the area. The table is committed at `test/goblins_truth_table.json` (429 games, depth 3) so it need
not be rebuilt — that took ~2 hours. Score a new model in ~4 minutes:

```
MTG_GOBLIN_X=1 python3 test/goblins_tutor_truth_table.py --score test/goblins_truth_table.json --label X
```

Residual dissected (round 16): of the games postland moves, ~⅓ is wall-clock churn (converges by
budget 80) and the rest are **budget-independent** — 5 genuine regressions vs 2 genuine improvements,
stable at budgets 20/80/320/1280. So the residual is plan-set arbitration, not ranking error.

## 5. WHERE WE MAY BE WRONG — start here

These are load-bearing assumptions that were never tested. Ranked by how much they could change the
conclusion.

1. **The framing may be the wrong decision entirely.** Everything above optimises *which card to
   fetch*. But gi101's real fork was the **T2 land drop**, and the fetch only mattered because it
   changed which line got evaluated. Nobody has measured the headroom on *when to cast the Matron*,
   or on the land-drop choice itself. A 99.1%-optimal answer to the wrong question is still a wrong
   framing.
2. **The oracle is measured with a COLLAPSED axis.** `MTG_TUTOR_FORCE_CARD` reduces the plan set to
   one fetch, so a forced run explores fewer plans than a free one. The "oracle" is therefore
   *min over handicapped runs* and may **understate** the true ceiling. If the real oracle is better,
   the 4-turn headroom is wrong and the area is not closed. Testing this needs a per-decision force
   that leaves the rest of the axis intact.
3. **One forced card per GAME, not per decision.** Games casting Matron twice get one card forced for
   both. Unknown how many games that is.
4. **Depth 3 only** for the truth table; d5 unmeasured.
5. **The scan keeps only games where a searched tutor fetch happened**, so games where the right play
   was *not* to cast the tutor are excluded by construction.
6. **Plan-set churn is unexplored.** The surviving regressions are budget-independent: the search
   commits to a worse line given an equally-good candidate set. That is a *search* question — move
   ordering, dedup, beam — and nobody has looked at it. This is where the genuine residual lives.
7. **The value reserve was ON in every measurement above.** Interactions untested.
8. **Only Goblins.** `PLAN_AWARE` touches only `GoblinsProvider`; Hinata also has a tutor (Gamble)
   and `POSTLAND` is engine-wide (Hinata measured ≈ −3, dominated by GT artifacts — see below).

## 6. Instruments available (all in-tree)

| flag / tool | what it does |
|---|---|
| `MTG_TUTOR_FORCE_CARD="<name>"` | collapse the tutor to one card **by name** (model-independent) |
| `MTG_TUTOR_FORCE_RANK=k` | same by rank — **table dies when the ranking changes**, prefer by-name |
| `MTG_TUTOR_CHOSEN_RANK=1` | log the rank the search actually commits to at real resolution |
| `MTG_TUTOR_RANK_DUMP=1` | full ranking + every input, once per distinct situation |
| `MTG_GOBLIN_RESERVE_TURN` / `_NEXT` | apply a lever only at one turn / one `mana_next` — pins binding states |
| `MTG_TUTOR_PREFIX_STATS=1` | distinct pre-tutor prefixes per decision |
| `MTG_TUTOR_WIDTH=N` | override the axis width for any provider |
| `src/ai/PlanContext.h` | hands a provider the plan being enumerated (byte-identical when unused) |
| `test/goblins_tutor_truth_table.py` | build / score against the name-keyed truth table |
| `test/goblins_width_diagnose.py` | separates RANKING MISS from SEARCH BRANCHING per game |

## 7. Methodology warnings — these produced wrong answers here

* **Never trust a loss-penalized aggregate driven by unwon/won flips.** Hinata's raw held-out read
  **−275** and was really about **−2**: three games GT stored as UNWON actually win (batch load at a
  20 ms wall-clock budget). One flip is worth 99 points. Always re-run flipped games standalone
  across several budgets — one was a genuine 9→8, another converged to 6/6 and was pure churn.
* **`budget_ms` is wall-clock and load-sensitive.** Goblins overnight has a *pre-existing* 6/6 digest
  failure with 0 turn-units; HEAD reproduces it. Use `--budget-ms 0` for anything that must be
  deterministic.
* **Score forced-vs-forced.** Comparing a model's free run against a forced oracle yields negative
  "regret" that is pure instrument artifact.
* **Pin a deciding state with a gate, never infer it from a matching number.** A rank-8 card and a
  W=9 threshold looked like proof and were a coincidence; the true binding state was elsewhere.
* **A defect inside an already-A/B'd lever is a new variant needing its own A/B.** Two "obvious bug
  fixes" here (the reserve eviction, the `mana_next` under-count) turned out to be load-bearing.

## 8. Repo rules that bite on this work

* Build with `./build.sh` only — never raw `cmake` (a bare configure gives `-O0`, ~10× slower).
* **Never wrap a command in a timeout.** Truncated runs read as results.
* **Do not kill a run past ~10 minutes** — that is the user's call alone.
* Do not rebuild while a measurement is running: new subprocesses would pick up a different binary.
* Log/output under `logs/` or `test/logs/`, never the repo root.

## 9. RESOLUTION (2026-08-05, Fable) — the missing thing was the BINDING ARCHITECTURE

The user's directive: stop approximating the plan's state from outside and evaluate the heuristic
at the real state the line produces. The architectural asymmetry every prior arm missed:
scry/etbdig/ponder bind an INDEX resolved by the provider at the true mid-line state
(ScriptedTopChoice / ScriptedEtbDig pins); the tutor bound a NAME ranked at a guessed turn-start
state. `MTG_TUTOR_AXIS_RESOLVE=1` (commit 6fd730b + 9918622/3ce3bf6/5ee851a) rebinds the tutor the
same way: `Plan::tutor_choice` is an index into the ranking computed inside PerformTutor at each
plan's own resolution state — land played, prefix casts applied AND PAID FOR, the source on the
battlefield. No prior arm had the spent mana (POSTLAND added the land only; PLAN_AWARE adjusted
counts only), and none touched the d0/collection binding at all.

Held-out (full overnight suite, per-game vs stored GT, loss-penalized):

| deck | searched | d0 (deterministic) |
|---|---|---|
| antilife | −3 (3 better / 0 worse) | **−317 (32 better / 0 worse, 3 real unwon→won)** |
| hinata | ≈0 net of the documented gi90/gi158 GT artifacts; all worse games churn | −11 (12/5) |
| goblins | +3 (0/3, churn: recover at 4× budget) | +14 (17/30) |

**d0 moved for the first time in the whole investigation** — every prior arm was d0 0.0; binding
at collection time (turn-start, pre-land, the cast's own mana still counted) was the most wrong
state of all, and fixing it is worth −314 d0 turn-units held-out.

Two subsequent findings, both by direct game dissection rather than aggregate:

1. **The goblins residual was a WINDOW VETO, not ordering** (answers item 6). In five of six
   regressed games the baseline's winning fetch sat at exactly resolution rank 8–9 — all cheap
   ENABLERS. The legacy pre-land state's double mana pessimism buried the bombs, which kept those
   enablers inside W=6: an accidental diversity mechanism. Width 9 under resolve mode
   (provider-owned) recovers +13 → +3. This is also why the truth table could not see it —
   item 2's collapsed-axis blindness, confirmed: the oracle scores the single fetch, not window
   membership across turns.
2. **`turns_to_deploy` conflated leftover mana with capacity** at mid-turn states (Muxus t=5 off
   leftover 1 where capacity was 5). Capacity-anchored under resolve mode
   (`MTG_GOBLIN_RESOLVE_CAP=0` restores): searched-identical, d0 +3 — adopted on model
   correctness with its number.

Status: all levers default-off, byte-identical off (smoke 27/27, regression 45/45), commits
6fd730b..5ee851a. Adoption (default-on + 3-tier rebaseline) is the user's call; the package read
at adoption config is train searched −93 / d0 −99, held-out searched −273 / d0 −314 (raw), with
the honest hinata-artifact-corrected searched ≈ −5 held-out.
