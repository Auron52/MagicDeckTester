# Elves batch memory blow-up (2026-11-26, wip/elves-mem)

## Symptom
`scripts/leaf_tiebreak_check.py decks/Elves/Elves.cod` (24 jobs x 1000 games, d5 b20, 32 workers) died
after ~4,300 games: a global OOM at anon-rss 20.6 GB, then the engine's own `[rss-cap]` abort at 20.18 GB.
The budgeted pools (fsl / plan) were flat, so the growth was outside them.

## It is not per-game growth
* 2 workers x 400 Elves games (seeds s1000000/s1100000, gi 0-199): RSS 0.15-0.47 GB, no trend.
* 32 workers, the same manifest, `MTG_RSS_CAP_GB=14`: RSS flat at ~2.3 GB for 40 s, then +10 GB in ~15 s.
  Every cap report had `Elves~*~s1100000 gi=633` (and gi=398) in flight.
* That game alone, one worker: **0.2 -> 12.3 GB in 15 s** (killed at the 12 GB cap). One game, one
  enumeration, not an accumulation. The "linear" reading came from 32 workers reaching such games one
  after another.

## Where the memory went
A breakpoint-continuation plan enumeration on T8 (in a bottoming playout; hand Elvish Archdruid +
Galadhrim Brigade, a Genesis-Wave-sized board with Leaf-Crowned Visionary, two Immaculate Magistrates,
Allosaurus Shepherd, Wirewood Lodge):

* `EnumeratePlansWithLandUncachedBody` built **240,928** base plans (215,296 with one scaled sink: every
  squad count K x the activation and target choices). That list is 1.25 GB.
* `AppendSubdecisionAxes` then fanned the WHOLE list. The Leaf-Crowned Visionary pay-draw fan
  (`src/ai/TurnSolver.cpp:105160`, the `all.insert` closing the "LEAF-CROWNED VISIONARY" block at :105079)
  appended **1,420,416** variants: 7.7 GB, and the later fans kept going.
* Only after all of that did `ApplyProviderPlanListFilter` (`TurnSolver.cpp:109166` at HEAD) run the
  Elves scaling-sink rule (`ElvesScalingAdmit`, `src/ai/DecisionProviders.cpp:75163`), which keeps the top
  count (+ hold-back band) per class and drops nearly all of them. `ElvesScalingAdmit` itself then made a
  full `Plan` copy per row (`DecisionProviders.cpp:75197` at HEAD), doubling whatever reached it, and
  compared rows every-pair inside a bucket (`:75238`) -- 80% of the game's CPU once the fan was gone.

Found with gdb snapshots of the worker at 3 / 7 / 9 GB (stack: `BpEnumEntryFor -> BpDeriveContinuationList
-> EnumeratePlansWithLand -> ... -> AppendSubdecisionAxes -> Plan::Plan(const Plan&)`, then
`ElvesScalingAdmit -> Plan::Plan(Plan&&)`), then temporary size prints at every fan's append site.

## Does it predate the Elves work?
No. Nothing in it exists at the merge-base 9e566000b: no `decks/Elves`, no multikicker/squad faces
(b74c1754d), no Visionary pay-draw fan (b74c1754d), no `ElvesProvider` / scaling rule / widened Wave X
(7a0d626a4), so Elves cannot be run there. The engine shape it exploits (axes fan the full list, a
provider's plan-list filter runs only at the end) is older and generic, but only Elves pairs a
count-axis list this wide with a provider filter that removes most of it. No other deck is affected:
the new hook defaults off.

## Fix (play-neutral)
1. `DecisionProvider::PlanListFilterBeforeAxes` (new hook, `src/ai/DecisionProvider.h:5506`, default
   false) and `PreAxesProviderPlanListFilter` (`src/ai/TurnSolver.cpp:108215`): when the provider opts in,
   its plan-list restriction ALSO runs on the base list, after `AppendBreakpointVariants` and before the
   axes, at all three hosts (m1 drop path :110962, no-drop path :109343, m2 :108235). The end-of-enumeration
   filter still runs. Placed after the breakpoint variants so `MTG_BP_MAXBASE`'s list-order cap sees the
   list it always saw. `ElvesProvider` opts in (`src/ai/DecisionProviders.h:3984`). Soundness: the rule
   groups plans by everything but the counts, and the axes fan every count variant of one line the same
   way (they read casts, board and hand, never a kick count or Wave X), so the verdict on a variant equals
   the verdict on its base.
2. `ElvesScalingAdmit` keeps no per-row plan copy: the blanked copy is built on demand and only each
   class's first member keeps one; and the class walk is split by a hash of fields the blank leaves alone
   (ordered actions' kind / name / slots / targets, the breakpoint pin), so equal plans always share a
   sub-bucket. Same partition, same admits.
`MTG_ELVES_SCALING_PRUNE=0` (and `MTG_UNPRUNED`, human play) are unchanged: with the rule off there is
nothing to apply early, and that arm still materialises every count and split by design.

## Before / after
| run | before | after |
|---|---|---|
| s1100000 gi=633 alone, 1 worker | 0.2 -> 12.3 GB in 15 s, killed at cap | peak 1.8 GB, 13.1 s, wt=4 |
| same game, ms | -- (never finished) | 13,061 ms (29,551 with fix 1 only) |
| check.json 24 x 1000, 32 workers | rss-cap abort at 20.18 GB after ~4,300 games | all 24,000 games; RSS 2.1-6.8 GB (10 s samples), ~10.5 min |

RSS curve after (GB, every 10 s): 2.1 2.2 2.1 3.3 2.7 3.3 2.6 2.8 3.0 4.5 4.6 2.7 3.8 2.9 2.7 3.1 2.7 4.2
5.1 4.8 3.2 2.8 3.4 2.9 3.1 3.1 3.4 3.2 3.2 4.8 3.6 3.3 4.8 4.5 3.2 3.5 4.7 4.3 3.5 3.7 3.9 5.2 5.3 3.4 4.5
3.5 3.5 3.4 3.7 6.8 5.3 4.4 3.5 3.4 3.4 3.5 3.6 3.9 3.6 3.7 4.9 3.7 2.7 (flat; no trend).

## Play neutrality
* The 12 jobs (12,000 games) the original uncapped run completed: digests **identical** (12/12).
* gi=633 digest identical between fix 1 and fix 1+2 (0101d8e0bf34fe23).
* Unit suite 4231/4231. Smoke (`MTG_PGO=0`, no accept): 251 passed, 0 failed; audit 0 configs changed, 0 play-changed (byte-identical to committed GT).

Full output of the fixed run: `logs/elves/leaf_tiebreak_check_fixed.out` (+ `.rss`), the input for the
leaf tie-break comparison the original run was meant to produce.

## Still open
The 240,928-plan base list (1.25 GB) is built before any filter can run; a worst-case Elves game still
peaks at ~1.8 GB on one worker. Shrinking it needs the count axis restricted inside the subset walk,
which is a search-shape change, not a memory fix.
