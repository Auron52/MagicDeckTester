# Rolling the searched main-phase design out to more decks

**Status: OPEN, not started. USER-requested next project (2026-08-22).**
Goal, in the USER's words: *"expand the number of decks using the new searched design with
condemnation and no greedy."*

This doc is self-contained. Everything it needs is in git; it does not depend on any agent's notes.

## 1. What "the new searched design" is

Three PER-DECK provider hooks, adopted independently, in this order. Each is a virtual on
`DecisionProvider` (`src/ai/DecisionProvider.h`) overridden in `src/ai/DecisionProviders.{h,cpp}`.

| # | hook | what it turns on | default |
|---|---|---|---|
| 1 | `ClassifiesMainPhases()` | the REAL main-1/main-2 split -- the pre-combat filter erases `MP::Main2` cards from the m1 candidate set (`MP::Both` survives = "let the search decide") | false |
| 2 | `CondemnsPassedMainPhase()` | ORDER CONDEMNATION -- main 2 continues with main 1's condemnation list instead of re-litigating the hand; membership decided once, at m1 | false |
| 3 | `SearchedSecondMainInSearch()` | **"no greedy"** -- the search's INTERIOR second mains run a real budgeted search instead of the greedy `Solve`, at every full-search ply | false |

Hook 3 is the one that satisfies the standing USER directive *"search should be truly search at every
level. Greedy is simply too unreliable to be part of it."* Hooks 2 and 3 are both scoped to hook 1
being live (`return on && <PhaseEnabled>()`), because without a real split the interior m2 carries no
deferred decision and searching it is pure budget dilution.

### Engine support already shipped (all DEFAULT ON, all with `=0` hatches)
These are generic and already in place, so a new deck inherits them:

| flag | what it fixes |
|---|---|
| `MTG_CONDEMN_PASS_EXEMPT` | a PASS is not a decline -- an m1 plan that casts nothing must not condemn the whole hand, or "pass, see combat, cast at m2" is unrepresentable |
| `MTG_CONDEMN_ENABLER_EXEMPT` | a gift payload declined with no enabler live is a statement about the BOARD, not the card; the enabler can arrive later the same turn (tutored in) |
| `MTG_PHASE_DAMAGE_BOTH` | `ClassifyMainPhase`'s DirectDamage arm priced combat BENEFIT but never combat COST -- deferring past combat can make the spell unaffordable because the attack taps its sources |
| `MTG_DORK_ATK_SEARCH` (+`_HOLD_DIR`) | searched dork attack/hold, both directions, vigilance-exempt |
| `MTG_UNBACKED_ETB_GIFT` | the unbacked-gift prune extended to ETB payloads (see §4) |

## 2. Where each deck stands today (2026-08-22)

| deck | split | condemn | searched m2 | note |
|---|---|---|---|---|
| **FiveColour** | YES | YES | **YES** | the reference adoption -- fully on the design |
| **KittyEquipment** | no | no | **YES** | `SearchedSecondMainInSearch() { return true; }` only. **Carries NO `gt_logs` rows in the suite, so its behaviour is currently unmeasured by the harness** -- worth fixing before using it as evidence for anything |
| **Anti-Lifegain** | YES | YES | **YES (branch site)** | ADOPTED 2026-08-22, byte-identical + perf-neutral; the ROLLOUT site is declined (`SearchesRolloutSecondMain()=false`), see §3 |
| Hinata | no | no | no | measured red on the global searched-m2 lever (2026-08-09, re-measured 2026-08-19) -- but see §4, that verdict predates the fixes |
| burn, auras, goblins, knights, slivers, treasure_hunt, dragonstorm, creature_giving, mirrorwing, StompySurprise | no | no | no | untouched |

## 3. Anti-Lifegain: RESOLVED 2026-08-22 -- the blocker was the ROLLOUT, not the decision

With every fix above in place, turning on hook 3 for Anti-Lifegain measures (6000 train games,
d3+d5, per game, loss-penalized):

| arm | net turns | worse | better | wins lost |
|---|---|---|---|---|
| searched m2, before `MTG_UNBACKED_ETB_GIFT` | **+31** | 29 | 4 | **7** |
| searched m2, after it | **-1** | 14 | 14 | 0 |
| **searched m2 vs KEEPING GREEDY** | **+13** | 13 | 1 | 0 |

That +13 was measuring TWO call sites at once. `SolveSecondMainInSearch` is reached from the
candidate loop of `SolveWithLookahead` (the **BRANCH** site -- a real decision) and from
`SimulateToEndImpl` (the **ROLLOUT** site -- the playout policy of the leaf estimator). Split with
`MTG_SSM_SITE`, on the same 6000 train games:

| site the searched m2 applies to | d3 / 3000 games | d5 / 3000 games |
|---|---|---|
| both (what the +13 measured) | +12 | +1 |
| **BRANCH only -- the decision** | **+0, BYTE-IDENTICAL** | **+0, BYTE-IDENTICAL** |
| ROLLOUT only | +12 | +1 |

The branch-site result holds over **26,000 games** (6000 train, 8000 held-out on all four overnight
seeds, 12,000 across four shuffle salts) with the searched path demonstrably firing. The rollout-site
cost is real and robust: worse in all 5 shuffle realisations (+52 / 30,000 games) and all 4 DECOUPLED
search salts (+43 / 12,000), so it is neither variance nor clairvoyance; ~2/3 of it is budget
dilution (the rollout charges the shared budget per simulated turn-step) and the rest survives 20x
budget. And it is NON-MONOTONE: removing the rollout's m2 entirely costs +7, so **greedy is an
interior optimum** -- more playout fidelity is not more ranking accuracy.

`DecisionProvider::SearchesRolloutSecondMain()` now splits the two (defaulting to the branch-site
answer, so fivecolour/KittyEquipment are byte-identical); `AntiLifegainProvider` declines the rollout
site. **`MTG_AL_SSM` is ADOPTED DEFAULT ON (USER, 2026-08-22)**: play is byte-identical to the
committed tree on 8 cells / 3200 games, perf is -0.0% (single-threaded, min of 8 reps), and smoke +
regression + the 208-reference gate are clean with 0 configs changed -- so there is no GT to
rebaseline. Full record: `antilife-main-phase-split.md` 2026-08-22y.

> **The general lesson for this rollout, and it REPLACES the "greedy's ordering knows something"
> reading:** hook 3 is two levers wearing one name. Adopt it at the BRANCH site -- that is where the
> USER directive applies, and on AL it was free. Be far more sceptical at the ROLLOUT site: a leaf
> estimator is scored, not played, and upgrading its playout policy changes the estimate's BIAS
> unevenly across candidates. Measure the two separately on every deck (`MTG_SSM_SITE=1` vs `=2`),
> because a single number over both can read "red" while the decision half is free.
>
> The older warning still applies to a deck's CARDS: before enabling hook 3, ask what that deck's
> ordering knows that a win-turn leaf cannot see -- any conditionally self-harming card can blow up
> the way Aria of Flame did (§4).

## 3a. What the ROLLOUT site costs the two decks that ship it (measured 2026-08-22)

The obvious follow-up to §3: fivecolour and KittyEquipment both have hook 3 on, which today means
BOTH sites, so both inherit whatever the rollout site costs. Measured the same way AL was --
branch-only (`MTG_SSM_BRANCH_ONLY`) vs shipped, per game, paired, one pooled batch of 51,200 games.
Positive = removing the rollout site is WORSE.

| deck | train d3 | held-out d3 | train d5 | held-out d5 | salts 1/2/3 (d3) | verdict |
|---|---|---|---|---|---|---|
| **KittyEquipment** | +0 (0w/0b) | +0 | +0 | +0 | +0 / +0 / +0 | **exactly INERT -- 0 of 9,600 games differ** |
| **FiveColour** | -1 (4w/5b) | +8 (15w/6b) | -1 | -1 | +8 / +3 / -2 | no signal: +14 turns / 11,500 games, **44 worse : 32 better** (p ~ 0.19), and rows flip sign |

**Neither deck pays AL's cost, and FiveColour's weak direction is the OPPOSITE one** (its rollout
site, if anything, mildly earns its keep). AL is the special case; leave both decks alone.

Kitty's result is the striking one: the searched m2 fires ~400 times per game there and **never
changes an outcome** -- consistent with the evidence its adoption rested on (four arms, one digest).
Perf, single-threaded min of 4 alternating reps: removing it saves Kitty **1.6% at d3 / 0.5% at d5**
at byte-identical play, i.e. free but not worth a behaviour-affecting hook change on its own.
FiveColour's perf (-4.5% d3 / +0.8% d5) is NOT a like-for-like read -- its play differs between the
arms, so the arms are timing different games.

Method note: the FiveColour cells are 500 games each (its games cost 12-26x an AL game), so this has
less power than AL's 1000-game cells. It is enough to exclude an AL-sized effect (+12 per 3000), not
enough to call a sub-turn-per-thousand one.

## 3b. The ROLLOUT site re-opened and re-measured at PLAY settings (2026-08-23)

USER: *"We shouldn't have any greedy within the searched window."* `MTG_AL_SSM_ROLLOUT` (heurarm,
DEFAULT OFF) was added to re-open the declined rollout site, and `MTG_M2_CAP1` to try the
strict-win route first (cap the interior m2 solve to depth 1, so it is still SEARCHED -- no greedy
pick -- without compounding against the iterative-deepening pass).

**GATE 1 -- aggregate at PLAY settings (d5/20 value_play), 30 seed blocks x 1,000 games:**

| arm | paired | net turns | worse | better |
|---|---|---|---|---|
| rollout searched | 30,000 | **+10** | 15 | 5 |
| rollout searched + cap1 | 30,000 | **+10** | 15 | 5 |

Both halves positive (+2, +8); 15:5 of 20 changed games is ~2.2 sigma WORSE. **Fails gate 1.**

The cap1 arm is byte-identical to uncapped -- and so is the pre-existing `MTG_M2_SEARCH_DEPTH=1`
env knob, so this is the knob being inert on this deck, not a wiring bug. AL's interior m2 candidate
sets are small enough that the solve's DEPTH does not change the plan it returns. **The strict-win
route via the depth cap is therefore dead**, and with it the "~2/3 of the cost is budget dilution,
so cap the interior" hypothesis.

**GATE 2 -- game by game, unlimited budget at the pre-regression winning depth:** 14 of the 15
regressed games recover at 10x budget (b200), so the bulk of the cost really is the rollout's m2
charging the shared budget and starving the outer candidate loop. ONE game does not:

| s1200000 gi264 | b20 | b200 | b2,000 | b20,000 | b200,000 |
|---|---|---|---|---|---|
| control | 7 | 7 | 7 | 7 | 7 |
| rollout searched | 8 | 8 | 8 | 8 | 8 |

Never converges on budget -- but it DOES recover on depth: d6 -> 7, d7 -> 7. So the T7 line is not
structurally deleted, it is pushed past what d5 can find. Borderline rather than a clean gate-2
failure; gate 1 is the one that settles it.

**Conclusion: the rollout site stays greedy on AL, now on a play-settings measurement rather than a
gate-cell one.** Note what the architecture says about scope: `depth` passes through
`SimulateToEndImpl` UNCHANGED (it does not decrement per simulated turn), so the rollout is the leaf
ESTIMATOR -- a playout that is scored, never played -- not part of the branching. Under the repo's
own law (OPTIMISTIC where you BRANCH, HONEST where you SCORE) it is the SCORE side. By that reading
AL already has no greedy in the searched window: its branch site is 79 searched / 0 greedy at play
settings.

**What IS inside the window, on every deck, is the d<=0 branch-site greedy** -- see the M2 SITE
counter. FiveColour, the reference adoption, takes 2,025,249 greedy branch-site second mains at
d<=0 against 42,502 searched ones: 98% greedy. That was the open item; §3c closes it.

## 3c. The d<=0 branch-site greedy: MEASURED, and it is NOT unreliable -- do NOT adopt (2026-08-26)

`SolveSecondMainInSearch` falls to greedy `Solve()` whenever `depth <= 0`, **regardless of every
per-deck hook**. That is not a rare corner: the branch-site depth IS the iterative-deepening PASS
INDEX (`for (sub_depth = 0; sub_depth <= depth-1; ...)`), so pass 0 -- the pass that finally commits
most decisions -- prices every candidate with a greedy interior second main. Census at HEAD, shipped
play settings, `MTG_M2_YIELD_STATS` (200 games; 5C 50):

| deck | BRANCH searched | BRANCH d<=0 (greedy) | share greedy |
|---|---|---|---|
| Anti-Lifegain | 0 | 29,298 | **100%** |
| KittyEquipment | 9,450 | 175,954 | 94.9% |
| FiveColour | 21,824 | 109,633 | 83.4% |

`MTG_M2_D0_SEARCHED` (heurarm, **default OFF**) runs those calls at depth 1 instead: one ply, so it
cannot compound with the outer pass, but the plan is CHOSEN by enumerating the m2 candidate set and
scoring each with a playout rather than by `Solve()`'s ordering. It is gated on the deck having
already adopted the searched interior m2, so it widens an adopted design rather than starting a new
one, and it is **BRANCH-SITE ONLY for a structural reason**: the rescued call re-enters
`SolveWithLookahead(is_pre_combat=false, depth=1)`, whose own rollout re-enters this function at
depth 0 with `in_rollout=true`, and `depth` passes through `SimulateToEndImpl` UNCHANGED -- rescuing
there too recurses with no decrementing bound. Declining at the rollout site terminates it, and it is
also where the standing law puts it (OPTIMISTIC where you BRANCH, HONEST where you SCORE).

### Gate 1 -- quality at PLAY settings. All six cells WORSE.

One pooled batch, 24,000 paired games, arms paired on seed, depth/budget from each deck's own
`value_play` (AL d5/b20, Kitty d5/b20, 5C d6/b20). Positive delta = worse.

| deck / block | n | delta | se | t | faster | slower | plays differ |
|---|---|---|---|---|---|---|---|
| al train | 5000 | **+0.0032** | 0.0010 | 3.27 | 1 | 14 | 43 |
| al hold | 5000 | +0.0008 | 0.0006 | 1.41 | 2 | 6 | 31 |
| kitty train | 5000 | +0.0008 | 0.0005 | 1.63 | 1 | 5 | 20 |
| kitty hold | 5000 | +0.0006 | 0.0003 | 1.73 | 0 | 3 | 19 |
| 5c train | 2000 | +0.0015 | 0.0009 | 1.73 | 0 | 3 | 13 |
| 5c hold | 2000 | +0.0005 | 0.0005 | 1.00 | 0 | 1 | 18 |

**6 of 6 cells worse, 32 games slower : 4 faster.** No single cell is decisive; the agreement across
six independent cells and the 8:1 game count are.

### Gate 2 -- WHY. It is budget dilution, and the greedy plan is the SAME plan.

All 36 changed games re-played at escalated budget AND depth, **both arms escalated together**
(escalating only the arm compares two configurations and answers nothing):

| cell | games | identical PLAY DIGEST | turn differs |
|---|---|---|---|
| 10x budget (b200) | 36 | **35** | 1 |
| 100x budget (b2000, al+kitty) | 32 | 31 | 1 |
| 100x budget + 1 depth (b2000/d6) | 32 | **32** | 0 |
| 10x budget + 1 depth (b200/d7, 5c) | 4 | **4** | 0 |

Not merely the same win turn -- the same DECISION STREAM. The single holdout (al train gi=889, 6->7)
survives 100x budget and closes on one extra depth ply.

Confirmed on the whole population rather than just the changed games, at `budget_ms=0` (UNLIMITED),
1000 paired games each, native depth:

| deck | base avg | arm avg | play digest | search work |
|---|---|---|---|---|
| Anti-Lifegain | 4.1880 | 4.1880 | **IDENTICAL** `f01244e4c19986ce` | +0.04% (1 cheaper : 50 dearer : 949 same) |
| KittyEquipment | 4.3560 | 4.3560 | **IDENTICAL** `b0f6812ee188f954` | **+73.72%** (0 : 80 : 920) |

**The lever fired, and that was checked rather than assumed** -- the failure mode this counter exists
for is that "no effect" and "never ran" look identical. AL at b0/d5, 30 games: 1,441 of 1,441
branch-site d<=0 calls rescued in the arm, 0 in the baseline, with the branch-site searched count
(819) and every rollout count unchanged. FiveColour was not run unbudgeted (its games already reach
100 s at the shipped b20); its four changed games are covered by the escalation table above.

Where the cost lands at the shipped budget is visible directly: the number of pass-0 candidate
evaluations that reach the interior m2 DROPS, because the interior spend comes out of the same
allowance -- AL 29,298 -> 20,837 (**-28.9%**), Kitty 175,954 -> 118,999 (**-32.4%**).

### Verdict: NOT YET adoptable -- and the blocker is COST ALONE

**USER BAR (2026-08-26), which supersedes an earlier reading in this file:**

> *"I would rather delete all greedy because we know it can be wrong. However, I want to work to
> avoid compromises."*

An agent draft of this section concluded "a greedy step is worth removing only where it is
measurably wrong". **That is the wrong bar and it has been retracted.** "The digests match on the
sample we ran" is an absence of evidence, not a safety argument -- it is the same shape as the
standing NO-LOSSY-TRUNCATION bar, where *rare-but-possible is reason enough* and the frequency of a
reachability defect is the wrong axis to judge it on. Greedy goes, everywhere; the job is to make
removing it cost nothing, not to find decks where keeping it is defensible.

So read the measurement above the other way round. It does not say "keep the greedy". It says:

* **There is NO quality risk to trade against.** The conversion is play-IDENTICAL -- byte-identical
  digests over 2,000 unbudgeted games and on 35 of the 36 games where the shipped budget made the
  arms diverge. Nothing has to be given up to delete this greedy.
* **The entire blocker is cost**, and the 6-of-6 gate-1 loss is a PURE consequence of it: the
  interior spend comes out of the same allowance, so the outer candidate loop gets ~30% fewer
  candidates. Remove the cost and the quality loss removes itself.

And the cost is not a broad tax -- it is a **TAIL**, which is the tractable shape:

| deck | total | games differing | where the extra work is |
|---|---|---|---|
| Anti-Lifegain | **+0.04%** | 51 / 1000 | worst single game +473 units on a 66,086-unit game |
| KittyEquipment | +73.72% | 80 / 1000 | **gi=231 (x5.3) + gi=470 (x1.7) = 77.7% of ALL of it**; top 5 = 84.9% |

**On Anti-Lifegain the greedy can be deleted today at no measurable cost.** On KittyEquipment the
work is to root-cause a handful of blow-up games, not to pay a 74% tax. `MTG_M2_D0_SEARCHED` stays
in the tree default OFF as the instrument WHILE that work happens -- not as a settled rejection.

## 4. The two known instances of that class (both fixed, both prunes)

`TurnSolver.cpp` refuses to EMIT a candidate subset that hands the opponent life with no
`lifegain_to_loss` enabler live. Two functions, same shape, same `MTG_UNPRUNED` hatch, same
self-backing exemption (an enabler cast in the SAME subset converts the gift, since the canonical
order resolves the enabler at rank 0 first):

* `SubsetHasUnbackedAltPayload` -- alt-cost payloads (Invigorate, Reverent Silence, Skyshroud Cutter).
* `SubsetHasUnbackedEtbGift` -- ETB payloads (`etb_opponent_lifegain`). **Added 2026-08-22 after the
  first one was found to key on `alt_cost` and miss hard-cast enchantments entirely.** Aria of Flame
  is the only card in `cards.json` with that parameter today.

The ETB fix measured -82 turns over 229100 games with **zero worse games**, 16 losses converted to
wins, and ~18% faster on antilife d3 -- and 48 of those turns were at **d0**, i.e. the shipped greedy
runner was making the same misplay. A new deck may need its own instance of this class.

## 5. Method -- the traps that cost real time in the arc that produced this

1. **"Unrecoverable" requires escalating BOTH budget AND depth.** A budget-only escalation proved
   nothing twice: 10000x budget moved nothing while ONE extra depth ply fixed two of three games.
   A gap that closes with more work is churn; only a gap that survives unlimited budget *and* depth
   is a deleted line.
2. **A lever measured while it is buggy has not been measured.** `MTG_AL_CONDEMN` was recorded
   "inert at cost, not adopted"; re-measured after two rule fixes it was worth -5 turns and a win.
   Any "measured red" verdict predating the §1 fixes -- including Hinata's searched-m2 rejection --
   should be re-measured before it is trusted.
3. **Reproduce a harness row at its RESOLVED depth.** `test/logs/<mode>/manifest.json` omits `depth`
   and pins only `budget_ms`, so BatchRunner takes depth from the deck's `value_play` block:
   `fivecolour_regression_d5_*` actually runs at **depth 6**. Read the run's `[play]` line first.
4. **A "KEPT HANDS DIFFER" diff row is usually not a bug.** A deck with no exhaustive keep/bottom
   table falls through to `AIEngine::BottomCards`, which runs a full clairvoyant ROLLOUT per candidate
   card -- so it executes the play logic, and any play change re-picks which card is bottomed. Isolate
   with `MTG_BOTTOM_ROLLOUTS=0`: if both arms then keep the identical hand, that is the cause.
   Table-less decks today: **FiveColour, KittyEquipment, StompySurprise** (all others have one).
5. **Perf A/B on this box: use `--threads 1` on a single job.** `/proc/loadavg` reads ~25 under WSL2
   with no `mtg` process running, so any "wait for a quiet box" guard hangs forever and multi-threaded
   timings swing 2.4x within one arm.

## 6. Suggested route per deck

1. **Re-measure, do not trust the old verdict** (trap 2). Train seeds 1001/2002/3003, d3+d5,
   1000 games each, `--ignore-play-profile`, via one pooled `mtg --batch`.
2. **Audit the deck's cards for the §3 class first** -- grep `cards.json` for the deck's list and look
   for conditionally-harmful parameters (`etb_opponent_lifegain`, `opponent_lifegain`,
   `alt_lifegain_cost`, anything whose sign flips on board state). Cheaper than debugging it later.
3. Adopt hooks in order 1 -> 2 -> 3, measuring each. Hooks 2 and 3 are inert without hook 1.
4. **Validate on held-out** (overnight seeds 4004/5005/6006/7007) before proposing adoption.
5. Escalate every worse row on BOTH arms at equal config (trap 1) and classify: budget churn /
   depth-limited / fetch or mulligan variance / real defect.
6. Report to the USER with a better-vs-worse tally; the standing bar is **accept only on an overall
   improvement**. Then GT rebaseline across all three tiers.

## 7. Open items inherited by this project

* **FiveColour has no mulligan profile** -- it is the only fully-adopted deck without one, so every
  play change re-litigates its bottoming through live rollouts (trap 4). Generating one would remove
  that diff noise and most of the deck's runtime (bottoming was measured at 90.4% of it). Per
  `.claude/skills/mulligan-profile.md` Rule 0, generate late on a frozen commit -- its play is stable
  as of 337816d2.
* **Anti-Lifegain's value leaf and keep model** were fit under the pre-2026-08-22 play behaviour and
  are stale on the merits.
* **KittyEquipment has no ground-truth rows** in the regression suite despite having hook 3 on.
* **The 13-turn residual** in §3 is RESOLVED (2026-08-22): it was entirely the rollout site, and the
  decision site was free. **The cross-deck version is now MEASURED too, and neither other deck pays
  AL's cost** -- so AL was the special case and no global change is warranted (see §3a).
* **Hinata's searched-m2 rejection should be re-measured PER SITE** (`MTG_SSM_SITE=1`), not just
  re-measured: its red verdict, like AL's, is a single number over both sites.
* **The d<=0 branch-site greedy is MEASURED and its removal is a COST problem only** (§3c,
  2026-08-26): play-identical to the search on every deck tested, so nothing is traded away by
  deleting it. Free on Anti-Lifegain today; on KittyEquipment two games carry 77.7% of the cost.
  **Open work: root-cause that tail, then delete the greedy.** Not a rejection.
* **The remaining greedy inside the search is the BREAKPOINT CONTINUATION fallback** --
  `greedysite` sites 0-8, taken when `bp_searched_plan(site, ...)` returns false because the site is
  not searchable for that deck (site 3, the plain cantrip, is pruned outright). That is now the only
  unmeasured greedy class, and §3c gives the cheap way to test it: does it return a different plan?

## 6. HINATA MEASURED END TO END (2026-08-30) -- greedy wins today, and WHY is now known

The full greedy-free ladder on Hinata, paired 1200x2 (hold s6600001 / train s5500001, d5/20ms),
units from 300-game hold probes, greedy = `acted` breakpoint-continuation decisions per 300 games:

| arm | site-3 continuation | hold | train | units | acted bp greedy |
|---|---|---|---|---|---|
| **shipped (all defaults)** | greedy Solve | **5.6433** | **5.6917** | **14.59M** | 2.41M |
| ORDER_FULL + NO_GREEDY_CONT ("ng") | **still greedy** (class-masked -- NGC does NOT reach a masked site; `why_class` said so) | 5.6658 | 5.7033 | 16.08M | 1.91M |
| + SITE3 + SITE3_DEFER (recipe, generic order) | canonical deferred | 5.6600 | 5.7258 | 14.63M | **0** |
| recipe + ORDER_FULL | canonical deferred | 5.6892 | 5.7325 | 15.42M | **0** |
| ORDER_FULL + NGC + MTG_BP_NODE | searched node | 5.6517 | 5.7033 | 21.70M | **0** |

Paired vs shipped: recipe+ORDER_FULL +0.046/+0.041 (t 3.7/3.3); recipe-generic +0.017/+0.034
(t 1.6/3.0); node +0.008/+0.012 (t 0.6/1.0, indistinguishable) at 1.49x units. ORDER_FULL is
measurably WORSE than the generic tiering in both contexts it was isolated in today
(shipped-vs-ng, recipe-gen-vs-recipe-of +0.029 t 3.8) -- the revised order does not clear its
own review bar on the numbers. The interior second main is 100% greedy in every arm (M2 PATH
SEARCHED 0%; ~150k by-hook + ~1.08M by-depth<=0 per 300 games) -- hook 3 untouched.

**THE ROOT CAUSE (USER 2026-08-30: "it makes 0 sense unless we are doing something wrong" --
confirmed, and the something is named):** greedy `Solve` re-prices SEQUENTIALLY -- each cast
resolves before the next decision, so Hinata's per-target discount and Reality Spasm's float
are always priced against the true state. Every searched form prices plans STATICALLY at
enumeration, and `CountSameTurnReducers` covers only colour/subtype reducers and metalcraft --
**`hinata_cost_reducer` (per-target discount) has NO same-subset credit**, so any one-enumeration
plan containing {Hinata, Spasm.., payoff} prices the chain at full cost, fails affordability,
and is never emitted. This one gap explains, at once: why greedy beats every searched form on
this deck; why "all-main-2" fails (the m1/m2 boundary is the search's only free re-pricing
point -- gi=22's T4 chain is inexpressible in one enumeration at 100x budget); why the node
loses at equal compute; and why Hinata "leans negative" under NO_GREEDY_CONT. On budget
fairness (USER asked): two mains get NO extra budget -- one shared budget object, and measured
units at 20ms are node (two-main) 21.70M vs nodem2 (one-main) 22.06M.

**THE WORK ITEM THAT FALLS OUT: build the Hinata same-subset discount credit in enumeration**
(precedent: `SameTurnMetalcraftEquipCredit` -- optimism is sound where the apply validates;
EnumeratePlans only, never the d0 leaf, per the LeafReducerCreditEnabled law; the per-target
count is available where X is chosen). Then RE-MEASURE this ladder: the expectation is the
searched forms close the gap to greedy, and both "no greedy" and "drop main 1" become askable
on a sound enumerator instead of being answered by its defect.

### 6a. The credit BUILT and MEASURED (2026-08-30, `22c0efc9`) -- necessary but NOT sufficient

`MTG_HINATA_SUBSET_CREDIT` (default OFF, heurarm slot; rationale on the reader in
EngineFlags.h): the ritual emits when she is castable from hand (honest undiscounted cost),
the X-payoff's range is sized as if she resolves first, and the enumerator's subset gate
credits her would-be discount for subsets that cast her (`SameTurnHinataCredit`, the
metalcraft pattern -- realised, never stranded: she casts first by rank and the per-cast
payment recomputes; the batch prepay declines on X-spells so it cannot fix costs early).

Measured, paired 1200x2 on the ladder arms: **quality-neutral everywhere** -- shipped
+credit hold -0.0050 (t -1.1) / train +0.0042; recipe +0.0042/+0.0017; node -0.0050/+0.0067.
~25 games move per cell, the gap to greedy does not.

**Why: the static-pricing gap has (at least) TWO LAYERS, and the credit fixes only the
first.** gi=22 under all-main-2 + credit still cannot express the T4 chain at 100x budget --
the whole chain in ONE subset must now pass the COLOUR-EXACT feasibility gate against the
pre-untap board (~8 coloured pips vs 5 sources), and the refloat's COLOURS are not modelled
there (the flat credits are generic/wild). The two-main + breakpoint structure never faces
this: each boundary chunks the chain into small subsets priced against REAL state (M2 sees
her resolved; the continuation sees the float). So the boundaries are compensating for
sequential-realization as a whole -- generic discounts (now credited) AND colours AND float
timing. **The principled full fix is the parked executor-validated feasibility design
(`enumeration-feasibility-via-executor.md`, "kill the per-deck patching treadmill"): when the
flat gate rejects an INTERACTING subset, decide with a real sequential per-cast probe instead
of another credit.** That is a hot-path architecture change its own doc says to review first
-- USER sign-off before building.
