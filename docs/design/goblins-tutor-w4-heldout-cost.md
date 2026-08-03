# Goblins tutor axis W12→4: a small, consistent held-out cost — resolve before trusting it

**Status:** ROOT-CAUSED and FIXED (2026-08-03). Both defects were in the RANKING, not the width —
see "The fix" below. All 5 real regressions recover. The rest of this doc is the investigation
that got there; read "The fix" first, then the mechanism sections for why the diagnosis holds.

---

## The fix (2026-08-03): two ranking defects, both "the model does not see what the card DOES"

`6bc04b8` narrowed the searched tutor axis W12→4 and leaned on a value × deploy-discount ranking to
keep the right targets in the top 4. Held-out, 5 games got a turn slower and stayed slower at
unlimited budget (real quality loss, not churn — see the RESULT section). Both causes are ranking
bugs, and each is a small, local change:

| # | defect | lever | recovers |
|---|--------|-------|----------|
| 1 | **lethal reach measured against the wrong board.** `face_burst` correctly saw Twinshot Sniper's 2 damage, but the lethal test compared it to `ready_atk` — a bare scan of what can attack *right now*, before the lord in hand is cast and before the tutor source itself lands. In gi865 that read **7** where the real swing was **17**, so "does this fetch cross lethal?" answered no at 19 life and Sniper sat at **rank 10 of 15**. | `MTG_GOBLIN_SWING_LETHAL` (default on) — test against a projected swing: board + the best lord hard-castable *alongside the source* + its team buff + the haste it grants + the source's own fresh body. Conservative: real untapped mana only (no Skirk ramp, which eats the very attackers being counted; no Lackey drop, which resolves after combat damage and so cannot attack). | gi865, gi60 |
| 2 | **enablers scored as vanilla bodies.** `value_of` credits a card for its own board impact, so Goblin Warchief = 270, Skirk Prospector = 100, Goblin Lackey = 100 — against a lord's 900–1100. Every accelerant in the deck is therefore unreachable at W=4. At W=12 the *unranked* axis stumbled onto them by shuffle order, which is exactly the quality W12 was buying. | `MTG_GOBLIN_ENABLER_RANK` (default on) — credit what the fetch *unlocks*: a fraction of the best **stuck** hand Goblin's value (cost cut 0.50, blanket haste 0.30, Skirk ramp 0.40, Lackey free-cheat 0.60). Gated on actually holding something uncastable — an enabler with nothing to enable is just its body. | gi602, gi206, gi842, gi924 |

Verified per game at `--budget-ms 0`, depth 3 (all five had PERSISTED at unlimited budget, i.e.
unreachable at any budget under the shipped ranking):

```
          shipped   +swing   +swing+enabler      pre-W (unranked W12)
gi865        T4       T3          T3                     T3
gi602        T6       T6          T5                     T5
gi206        T6       T6          T5                     T5
gi842        T6       T6          T5                     T5
gi924        T6       T6          T5                     T5
gi638        T4       T4          T4    (churn control, unmoved)
gi999        T5       T5          T5    (churn control, unmoved)
```

**Why defect 2 is one idea and not four constants.** Each of the four regressions fetched a cheap
enabler to land the bomb *already in hand* a turn sooner — Warchief's cost cut (gi602, gi206),
Skirk's ramp (gi842), Lackey's free cheat (gi924). So the credit is measured against the hand, not
invented per card: "how much does this pull forward the best thing I am already holding?" The four
clauses are four expressions of that one question, each a fraction of the accelerated card's own
value, so the term introduces no new scale.

### Measured: 5 arms × 19,325 games each (every Goblins case of all three tiers, one pooled batch)

`test/goblins_swing_lethal_ab.sh` runs the whole Goblins matrix through one manifest per arm. The
`base` arm reproduces committed ground truth **exactly — avg AND play digest, all 20 cases** — which
is the A/B validity check (regression-testing skill rule 6); all levers are env flags on one binary,
so there is no risk of two identical arms. Deltas below are vs `base` (negative = faster = better):

| group | games | shipped W4 | + swing | + swing + enabler | pre-W: unranked W12 | both fixes, W12 |
|-------|------:|-----------:|--------:|------------------:|--------------------:|----------------:|
| heldout / d0       | 8000 | — | +0.0000 (0sl/0fa) | **−0.0085** (26/91) | **+0.0304** (265/35) | −0.0085 (26/91) |
| heldout / searched | 8000 | — | −0.0005 (0sl/4fa) | **−0.0050** (4/44) | −0.0035 (21/49) | −0.0070 (7/63) |
| train / d0         | 2000 | — | +0.0000 (0sl/0fa) | **−0.0070** (5/18) | **+0.0295** (63/7) | −0.0070 (5/18) |
| train / searched   | 1325 | — | +0.0000 (0sl/0fa) | **−0.0030** (4/8) | −0.0076 (5/15) | −0.0075 (5/15) |

Three things this settles:

1. **The W12→4 cost is confirmed at exactly +0.0035 on held-out searched** — the `unranked W12` arm
   measures it directly (−0.0035 vs shipped), independently reproducing the per-game estimate the
   earlier section derived from the GT diff. The two methods agreeing is worth more than either.
2. **With both fixes, ranked W4 (−0.0050) now BEATS pre-W unranked W12 (−0.0035)** on held-out
   searched, at one third the axis width. The narrowing is no longer a quality trade — it is a
   straight win, which is what `6bc04b8` claimed and could not yet support.
3. **The ranking was always a large d0 win and that is unchanged.** At d0 the greedy just takes
   `cands[0]`, so unranked = shuffle order: the pre-W arm is +0.03 WORSE with 265 slower vs 35
   faster. Any future "just revert the ranking" instinct should read this row first.

**Ranking vs width, finally separated** (the test this doc has been carrying as open): with the
fixes in place, W12 is still worth a little more than W4 on searched (−0.0070 vs −0.0050 held-out,
−0.0075 vs −0.0030 train). So width does carry residual value beyond ranking. Whether to pay 3× the
tutor axis for ~0.002–0.005 turn is a cost decision, not a correctness one — `6bc04b8` measured the
whole 3-deck regression makespan moving 38s→46s across the entire width range (additive, not
multiplicative), so it is affordable if wanted. **Left for the user to call; not changed here.**

**Honest caveat — the enabler term is a net win, not a free one.** It creates new slowdowns as well
as fixing old ones. Of the 5 searched games that got slower, re-run at `--budget-ms 0` on both arms:
`gi573` (T3→T4), `gi849` (T5→T6) and smoke `gi44` (T4→T5) **PERSIST** (real, budget-unrecoverable);
`gi289` and `gi112` recover (churn). They are outweighed 44:4 on held-out searched and 91:26 at d0,
and every group improves — but this is a heuristic trade, not a bug fix, and the three persisting
games are the honest cost of it.

**The instrument.** `MTG_TUTOR_RANK_DUMP=1` prints the ranked list plus every input it derives from
(board scan, mana, projected swing, per-candidate value/enabler/discount/burst), deduped by
situation so one game emits a readable handful. This is what made the diagnosis a measurement
rather than a story — it showed Sniper at rank 10 with `burst=2` and the clause silently not firing.
Note the top-level resolution never calls `TutorCandidates` (it replays the search's chosen name),
so the dump fires from the search's enumeration; match the real state by the inputs it prints.

---

## What was measured (2026-08-03, held-out overnight seeds)

`6bc04b8` ranked the Goblin Matron tutor axis by value × deploy-discount and narrowed the searched
width **12 → 4**. Its train measurement was `ranked-W4 4.322` vs `unranked-W12 4.316` (d5 b20 s2002,
1000 games) — i.e. **+0.006 slower**, reported as "≈ near-full quality at a narrower, cheaper axis".

Rebaselining the overnight tier (4 held-out seeds, 12 changed keys) gives, at each case's
**restricted** budget, games-weighted on the loss-penalized avg win turn:

| tier            | keys | games | weighted Δ | turn-units |
|-----------------|-----:|------:|-----------:|-----------:|
| d0 (greedy)     |    4 |  8000 | **−0.0304** |     −243   |
| d3 + d5 searched|    8 |  8000 | **+0.0035** |      +28   |
| all changed     |   12 | 16000 |     −0.0134 |     −215   |

(negative = faster = better)

**The searched tier — the one that reflects real play — is marginally WORSE.** d0 (greedy, the
"lighter bar" coverage case) is clearly better and dominates the combined number, which is why a
naive "all changed" read looks like an improvement. Do not quote the combined figure alone.

## Why this is probably not noise

Train (+0.006, 1 seed, 1000 games) and held-out (+0.0035, 4 seeds, 8000 games) agree in **sign and
magnitude**. The commit attributes the residual to post-Matron-shuffle draw variance, but variance
from a differing fetch should be ~zero-mean across 9000 games, not consistently one-signed. So the
working hypothesis is a **small real cost of the narrowing**, deliberately traded for a 3× cheaper
axis.

## RESULT of the settling test (run 2026-08-03)

All 7 same-draws slower games re-run at **`--budget-ms 0` (unlimited) on BOTH arms**
(old arm = `logs/snapshots/regression-baseline`, a pre-W build at 54e1094):

| game | restricted | unlimited | verdict |
|------|-----------|-----------|---------|
| `d3_s6006` gi638 | 4->5 | T4 -> T4 | churn (recovers) |
| `d3_s6006` gi999 | 5->6 | T5 -> T5 | churn (recovers) |
| `d3_s4004` gi865 | 3->4 | T3 -> **T4** | **PERSISTS** |
| `d3_s4004` gi602 | 5->6 | T5 -> **T6** | **PERSISTS** |
| `d3_s5005` gi206 | 5->6 | T5 -> **T6** | **PERSISTS** |
| `d3_s6006` gi842 | 5->6 | T5 -> **T6** | **PERSISTS** |
| `d3_s6006` gi924 | 5->6 | T5 -> **T6** | **PERSISTS** |

**5 of 7 persist at unlimited budget** => real quality regressions, NOT budget churn. `6bc04b8`
claimed "0 real same-draws regressions" from its train measurement; that does not carry to held-out.

### Mechanism: it is NOT a fetch target being ranked out

The obvious hypothesis -- the value x deploy-discount ranking pushes a premium payoff (Goblin
Chieftain) out of the top 4 -- is REFUTED by gi865: **both arms cast Chieftain on T3**, so Chieftain
was in hand, never fetched. The divergence starts at **T1**, before any tutor resolves:

```
T1  old: Goblin Lackey                              T1  new: Skirk Prospector
T2  old: Three Tree City; Skirk Prospector; ATTACK  T2  new: Goblin Lackey
T3  old: Goblin Matron; Goblin Chieftain; ATTACK  [opp 0]   <- lethal
T3  new: Goblin Chieftain; ATTACK                 [opp 16]
```

The old arm leads Lackey so it can connect on T2 and cheat a Goblin in free. The only code
difference is WHICH Matron targets the search enumerates and HOW MANY -- so the narrowed candidate
set changes the search's VALUATION of Matron lines, and that propagates upstream into an unrelated
T1 decision. A cheaper tutor axis is moving decisions that are not the tutor.

### Worked example: gi865 side by side (logs kept at `logs/w_ab/{old,new}/`)

Same opening hand (Mountain, Stingscourger, Three Tree City, Skirk Prospector, Goblin Lackey,
Siege-Gang Commander, Goblin Matron), same draws, both arms at unlimited budget:

```
     OLD (unranked, W=12) -- wins T3        NEW (ranked, W=4) -- wins T4
T1   land Mountain                          land Mountain
   ! cast Goblin Lackey                     cast Skirk Prospector
T2   land Three Tree City                   land Three Tree City
   ! cast Skirk Prospector                  cast Goblin Lackey
   ! ATTACK 1                               (no attack -- Lackey summoning-sick)
T3   land Mountain                          land Mountain
   ! cast Goblin Matron                     cast Goblin Chieftain
   !   searched: TWINSHOT SNIPER            ATTACK 4
   ! cast Goblin Chieftain
   ! ATTACK 17 -> lethal
T4                                          cast Stingscourger; Goblin Matron
                                              searched: Goblin Chieftain
                                            cast Rundvelt Hordemaster; ATTACK 28
```

The game turns on **T1**: leading Lackey lets it attack T2, bringing the free cheat-into-play engine
online a turn early. The W4 arm leads Prospector, so Lackey is summoning-sick on T2 and a full turn
of the engine is lost -- a decision made TWO TURNS before any tutor resolves.

**Leading hypothesis (user, 2026-08-03): the missing piece is TWINSHOT SNIPER's reach.** The old arm
fetches Sniper and swings for lethal on T3; the new arm fetches Chieftain a turn later. If Sniper (a
Channel `{1}{R}`, 2 damage) is being pruned out of the ranked top-4 -- plausibly because a
value x deploy-discount ranking scores a 2/2 body poorly and does not credit reach-as-lethal -- then
the search never sees the T3 kill, and the T1 valuation shifts accordingly. **Check first:** dump
GoblinsProvider::TutorCandidates for this state and see where Twinshot Sniper ranks. Note the
provider DOES have a lethal-reach clause (see 6bc04b8's message) -- so either it is not firing here,
or it fires too late in the ranking.

This also reframes the fix: if the value comes from the search seeing a WIDE set of futures rather
than from picking the single best target, re-ranking will not recover it and the right experiment is
the smallest W that recovers these 5 games.

### ANALYSIS LIMITATION: Lackey puts are missing from the saved game log

The side-by-side above CANNOT show what each Goblin Lackey dropped, which matters a lot for this
deck. The Lackey put report added 2026-08-03 is `viewer_only` (it reaches the play GUI but not the
game log) precisely because a new log entry would fold into the play digest and force a rebaseline.
So post-hoc A/B forensics on a Lackey deck is blind to the cheat-into-play choices. **Fix this at the
next deliberate rebaseline** by dropping `viewer_only` on that call site -- and note the same
argument applies to any future put/report: cheap for the viewer, digest-costly for the log.

### Still open: ranking vs width

Not yet separated. The intended test is the shipped gate -- `MTG_UNPRUNE=tutor` restores
GenericProvider's unranked candidates while `TutorSearchWidth()` stays 4, so recovering under the
gate would implicate the RANKING and not recovering would implicate the WIDTH.

> **A first attempt at this was VOID and must be redone.** It was run without `--old-bin`, and
> `regression.sh --accept` had already overwritten `logs/snapshots/overnight-baseline` with the
> CURRENT binary -- so both arms were the same build and it reported `T4 -> T4` twice, meaning
> nothing. Always pass `--old-bin logs/snapshots/regression-baseline` (or another pre-W snapshot)
> for this comparison, and remember that an `--accept` destroys the mode's old baseline snapshot.

## The original test plan (superseded by the RESULT above)

The audit flagged 49 searched-SLOWER games. Classified from the run output:

* ~12 show `DRAWS DIVERGE` → a different fetch reshuffled the library → physically different game →
  variance, not a like-for-like loss.
* **7 show `kept hand + draws IDENTICAL`** → same physical game, slower line. These are the sample
  that matters:

  ```
  goblins_overnight_d3_s4004 gi602: 5->6      goblins_overnight_d3_s6006 gi638: 4->5
  goblins_overnight_d3_s4004 gi865: 3->4      goblins_overnight_d3_s6006 gi842: 5->6
  goblins_overnight_d3_s5005 gi206: 5->6      goblins_overnight_d3_s6006 gi924: 5->6
                                              goblins_overnight_d3_s6006 gi999: 5->6
  ```

Run `bash test/classify_turn_later.sh overnight`, then for anything that PERSISTS re-run **both
arms** at `--budget-ms 0` (unlimited; a large number is NOT the same — see the regression-testing
skill). Two outcomes:

* **recovers at higher budget → churn.** W4 can still reach the fast line, it just needs more
  budget at the case's restricted setting. Benign; but note it is *odd* for a narrower axis to need
  MORE budget, so understand why before accepting that story.
* **persists at unlimited budget on both arms → real quality loss.** The value × deploy-discount
  ranking is pushing the winning fetch target out of the top 4, so W4 cannot reach it at any budget.
  That is a ranking-quality bug, not a width bug: the fix is to re-rank (or widen to the smallest W
  that recovers those 7), not to revert wholesale.

`6bc04b8` claimed "0 real same-draws regressions" on its train measurement. Held-out shows 7
candidates, so that claim does not carry over — which is the specific thing to re-examine.

## Process note (why this doc exists)

The overnight accept was performed **before** this analysis, which inverts the required order: the
regression-testing skill and `test/classify_turn_later.sh`'s own header mark the slowdown
classification as MANDATORY before `--accept`. The accept is recoverable —
`python3 test/audit_changed_games.py overnight --old-git` re-checks an accept that already happened,
and the GT change was uncommitted. The bar the user set, recorded so it is not lost:

> regressions must be **recoverable with budget**, the net must be an **improvement at restricted
> budget**, and **some games must be inspected** to confirm the implementation change introduced no
> new bugs.

On the second of those, at restricted budget the searched tier is currently **+0.0035, i.e. not an
improvement**.
