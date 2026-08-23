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
d<=0 against 42,502 searched ones: 98% greedy. That is the open item.

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
