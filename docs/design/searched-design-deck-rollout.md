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
| **Anti-Lifegain** | YES | YES | no | blocked, see §3 |
| Hinata | no | no | no | measured red on the global searched-m2 lever (2026-08-09, re-measured 2026-08-19) -- but see §4, that verdict predates the fixes |
| burn, auras, goblins, knights, slivers, treasure_hunt, dragonstorm, creature_giving, mirrorwing, StompySurprise | no | no | no | untouched |

## 3. The blocker on Anti-Lifegain, and what it means for every other deck

With every fix above in place, turning on hook 3 for Anti-Lifegain measures (6000 train games,
d3+d5, per game, loss-penalized):

| arm | net turns | worse | better | wins lost |
|---|---|---|---|---|
| searched m2, before `MTG_UNBACKED_ETB_GIFT` | **+31** | 29 | 4 | **7** |
| searched m2, after it | **-1** | 14 | 14 | 0 |
| **searched m2 vs KEEPING GREEDY** | **+13** | 13 | 1 | 0 |

So removing greedy is now **SAFE** (no destroyed wins) but not **BETTER**: it still costs 13 turns
against the greedy interior m2. `MTG_AL_SSM` stays OFF.

**Why, and this is the general lesson for the rollout:** the greedy `Solve` is ordered by `EvalCard`,
which consults the provider's `ArchetypeCardValue` -- so greedy's CANDIDATE ORDERING silently carries
per-deck domain knowledge. The searched path selects by projected WIN TURN, where anything whose cost
lands beyond the horizon is scored as free. **Greedy's ordering was substituting for depth, and the
searched path had nothing in its place.**

> **Before enabling hook 3 on a deck, ask what that deck's ordering knows that a win-turn leaf cannot
> see.** Any card that is conditionally self-harming -- or whose value depends on board state the
> horizon cannot reach -- is a candidate to blow up exactly the way Aria of Flame did.

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
* **The 13-turn residual** in §3 is the honest measure of what greedy's ordering still knows that the
  searched leaf does not. Closing it is the real prize: it is what would let hook 3 ship on decks
  where it is currently a regression rather than merely safe.
