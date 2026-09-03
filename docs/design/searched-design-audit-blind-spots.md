# Auditing "searched with heuristics": what the chooser-list method cannot see

Status: **method note + open work queue.** Written 2026-08-01 after two decisions were found that
the existing audit method structurally could not have found, one of which was worth ~200x every
ranking adopted the same day.

## The design being audited

No surprise greedy implementations or built-in heuristics anywhere in search. Every assumption is a
provider hook; a heuristic is a branch's DEFAULT / PRUNE / TIE-BREAK, never a substitute for
branching. Per-deck answers live in the archetype provider, never in the root `GenericProvider`.

## The method that was being used, and why it is not sufficient

The inventory trick (from the 2026-08-01 audit) was:

> the `g_play_*` human-play chooser declarations in `src/core/GameLogger.h` are a near-complete list
> of every decision point the engine has, because a chooser exists exactly where a human must
> override an engine default.

That is a good method and it did close the ownership gap (`7a3d5cd`). It has **two structural blind
spots**, both since demonstrated:

### Blind spot 1 -- offer/prune decisions

A chooser exists where a human PICKS AMONG candidates. It never exists where the engine silently
OFFERS or OMITS a candidate. So "should this action be in the candidate list at all" is invisible.

Found instance: `OfferDuplicateLegendCast` (`d8ef7f0`, adopted `ebcb0cd`). The legend rule is
modelled correctly and immediately, so casting a duplicate legend with no enter effect leaves board
value unchanged -- which makes it a TIE, not a loss, and a tie-break took it 109 times per 600 d0
games. **A correct rule plus an indifferent search produced a persistent misplay.** Worth
`hinata_d0 -0.0417` (t=-158, 6/6 seeds) AND 4.4% fewer rollout calls.

Note what makes this class hard: nothing is *wrong*. No rule is violated, no hook is missing from
the inventory, and the decision has no "candidates" to rank. It is only visible by asking what the
engine does when the answer does not matter.

### Blind spot 2 -- chooser-gated decisions with a hardcoded autonomous fallback

The decision HAS a chooser, so it reads as covered -- but the whole block is gated on the chooser
being non-null, so the AUTONOMOUS path (and every rollout) takes a hardcoded answer with no hook.

The method asks "does a chooser exist?" when it must ask **"what happens when the chooser is
ABSENT?"**, because that path is the engine's own rule.

Found instance: the storage-land tap-vs-charge decision (`d4f58cb`, `6292eae`). Both Mercadian
Bazaar (main-phase `{T}`) and Dwarven Hold (decline to untap; charges at upkeep) were gated this
way, in two different files, so the clairvoyant search could not represent CHARGING EITHER LAND.

Sub-lesson, learned the hard way: fixing one site of a decision is not fixing the decision. Bazaar
was fixed first and Dwarven Hold missed, because the two cards' mechanisms look different. The grep
that found the first would have found the second had it been run across ALL sites of the flag.

## The corrected method

For each candidate decision point, ask all four:

1. Does a provider hook exist? (ownership)
2. Is it consulted on the AUTONOMOUS path, not only when a human chooser is attached?
3. Is it consulted at EVERY site of that decision? (grep the flag/hook name repo-wide, not the first hit)
4. Is the result branched, or consumed `front()`-only? (branching)

And separately, for the offer/prune class, which no hook enumeration will surface:

5. Are there actions the search takes because it is INDIFFERENT rather than because they help?

## How to triage before building (this is the expensive lesson)

Contention rate alone is a **bad** predictor. Measure with a trace, then ask whether the difference
PERSISTS. Three decisions measured the same day:

| decision | contested when it fires | verdict |
|---|---|---|
| retrace discard | 98%, ranking moves the pick 60% | adopted, -0.0002, 24 faster / 8 slower |
| Land's Edge pitch | 1.7% (burns whole hand 97.6% of the time) | adopted on mechanism only, near coin-flip |
| legend-rule keep | 100% "differ" | **NOT BUILT** -- all differences erased by next untap |

The legend keep scored HIGHEST on naive contention and is worth the least: exactly one copy
survives, the ETB is banked before the state-based action, and every observed difference (tapped,
sickness, temp pump) is gone by the next untap step. "Do the candidates differ" is the wrong
question; **"does the difference survive long enough to change anything"** is the right one.

The user's shorter version, which got there without the trace: *a legend-rule keep only matters if
the legend has a use other than attacking, since only one copy can attack either way.* Krenko
(`{T}`: create X Goblins) is exactly that card, and it duplicated 0 times in 600 games.

## The offer/prune sweep: two probes, and why only one of them works

Blind spot 1 needed an instrument, since no hook enumeration can surface it. Two were built. The
first is the obvious one and it is nearly useless; the second is the one to reach for.

### `MTG_TRACE=tie` -- free within the projected win turn (WEAK, kept for context)

`FSLineWin` keeps a plan only when it is STRICTLY better, and `MoveOrderPlans` sorts candidates by
static value, so among plans the search cannot tell apart **the highest static value wins**. The
probe records every scored plan at the committed node and asks whether the chosen plan strictly
CONTAINS an equally-scoring one; the difference is a set of actions that bought nothing measurable.

It fires far too often to act on -- **67.5% of Goblins decisions** contain such an action, and every
card sits at a 70-93% free-rate. The reason is structural: at a bounded horizon most plays do not
move the projected win turn. Casting a lord on turn 2 does not make a turn-4 win arrive sooner, yet
no player would skip it. Aggregating into a per-card free-rate (`scripts/tie_probe_report.py`)
does not separate the classes either, because the noise is not per-card.

### `MTG_TRACE=nil` -- board-nullity (SHARP; the one worth reaching for)

What actually characterised the duplicate legend is stronger and **horizon-independent**: applying
the action left the BOARD exactly as it was. The copy resolved, the legend rule killed it, and the
only trace was a card gone from hand and mana spent -- neither of which the leaf evaluator prices,
which is exactly why the search could not see the loss.

So: apply the committed plan WITH and WITHOUT each action and compare a board signature (permanents
with tapped/sickness/counters/damage/auras, plus both life totals). Nine decks, 40 games each:

| deck | board-null actions | verdict |
|---|---|---|
| burn, th, knights, slivers, antilife, auras | 0 | clean |
| goblins | **25** (all Lightning Bolt) | enumeration hygiene, WITHDRAWN -- see below |
| hinata | 151 | all explained: rituals/cantrips + 2 probe artifacts |
| dragonstorm | 30 | all the known ritual afford optimism (deliberate) |

Read the output as a shortlist, never a verdict. Three benign classes dominate and must be
recognised before acting:

1. **Board-null by construction** -- rituals (float mana), cantrips and tutors (hand/library),
   Suspend (moves to the suspended zone). All good plays; their payoff is simply not on the board.
2. **Deliberate optimism** -- Dragonstorm's turn-1 `Seething Song`/`Scourge of Valkas` strand
   because the ritual afford credit is order-free and over-optimistic. That is LOAD-BEARING and was
   already measured: tightening it only hurts, because optimism PROPOSES go-off lines the re-sim
   VERIFIES. Do not "fix" it.
3. **Probe artifact: the draw-breakpoint re-solve.** When the plan contains a cantrip, dropping a
   later action does not remove it from the outcome -- the breakpoint re-solve casts it anyway, so
   the counterfactual is not clean. This is what Hinata's `Ornithopter of Paradise` /
   `Hinata, Dawn-Crowned` lines are. The `strand=` field separates it: `strand=1` means the card is
   still in hand (enumerated but unpayable), `strand=0` with an equal board means the continuation
   took the action regardless.

### What it found: the enumerator could not pay for what it offered

`BuildNonCreaturePool` -- the pool that verifies a NONCREATURE subset is payable -- skipped only
`creature_mana_only` (Ancient Ziggurat) and handed `colored_creature_only` lands (Cavern of Souls,
Unclaimed Territory, Sliver Hive, Secluded Courtyard) to `AddSourceToPool`, which books a 6-colour
source as one **WILD** that satisfies any coloured pip. So the enumerator believed a Cavern could
pay {R} for a Lightning Bolt. The payment path already models this correctly
(`ProducesForPayment`), so the cast **stranded as a silent no-op**.

The land-fold then ranked "play Cavern + Bolt" as good as "play Mountain + Bolt", so the engine
played the Cavern -- the land off which the Bolt could NOT be cast. Goblins gi28: T1 Cavern +
nothing (opp still 20 through T2) vs T1 Mountain + Bolt (opp 17).

**WITHDRAWN (user-directed), and the reasoning is the durable part of this entry.** The fix is
committed but `MTG_CCO_NONCREATURE_POOL` defaults OFF. Two things sank it:

1. **It is not a correctness fix, and I wrongly argued it was.** The bar for keeping a change whose
   cases regress is: *we allowed invalid behaviour, we now disallow it, and that is what made those
   cases worse.* Not met. The evaluator ALREADY refused the Bolt off a Cavern -- `MTG_CCO_AUDIT`
   reports **0 illegal taps** on the unfixed arm, a fact this audit had measured and then failed to
   draw the conclusion from. Nothing illegal was ever played; the cast merely stranded. So the
   change is enumeration hygiene, and must earn its place on measurement like any heuristic.
2. **On measurement it is exactly neutral.** 0.0000 delta on all 12 jobs, 6600 fresh games/arm
   (seeds 9001-9006, d3/10ms + d5/20ms). Every digest differs -- play IS perturbed -- but no
   outcome moves. The `+0.0024t` first reported was sampling noise from ONE train seed pair over
   five cases; it was adopted on train seeds with no held-out validation, which is the process error
   underneath the judgement error.

**Why it cancels -- the live finding.** The fixed arm GAINS the Bolt's damage and PAYS more land
branching: playing the Mountain first leaves the singleton Cavern in hand, so every later turn must
keep branching over two distinct land choices, where dumping the odd land early collapses the hand
to all-Mountains and one land option. Measured on same-outcome games: **+18% interior nodes (gi28),
+97% (gi166), -3% (gi90)**. Caveat: `interior_nodes` is confounded by early-win cutoffs, so only
compare games whose win turn is unchanged. This search-economy effect -- not the pool -- is the
question worth pursuing (land-order heuristic, GoblinsProvider).

### What DID ship out of this: the land-order prune (WITHDRAWN — see the correction below)

The branching side-effect turned out to be the real finding, and it belongs to a class the audit had
not been looking at: **breadth the search pays for on turns where the answer is not interesting.**
`EnumeratePlansWithLand` emits one plan group per distinct land NAME in hand, so a hand holding two
different lands doubles the candidate set every turn until one is played -- and under a fixed ms
budget that breadth comes out of the spell decisions.

`GoblinsProvider::ForcedEarlyLandName` (hook `DecisionProvider::ForcedEarlyLandName`,
`MTG_FORCED_EARLY_LAND`) collapses the turn 1-2 land fan-out to Mountain when one is in hand. Goblins
runs 21 Mountain + two singleton utility lands (Cavern of Souls, Three Tree City), neither of which
makes red the turn it lands.

| | result |
|---|---|
| quality | **0.0000** -- win turn IDENTICAL on all 12 jobs, 6600 fresh games/arm (seeds 9001-9006) |
| cost | **-3.72% rollout calls** (4 seeds, -2.88% to -4.75%, same direction every time) |
| train seeds | -0.0038, 5 faster / 1 slower -- NOT claimed; that magnitude is noise on this deck |

Adopted on COST at flat quality -- the same basis as the duplicate-legend prune's -4.4%. Notes:

- The prune fires only when a Mountain is actually in hand; with a single Mountain it forces turn 1
  and then the hook returns empty, so turn 2 fans out normally. Turns 3+ are always searched.
- **It gives up nothing on those turns**, which is the point. Both utility lands are strictly {C}
  sources until turn 3: a Cavern's colours never pay for a noncreature spell, and Three Tree City's
  scaled mode needs {2} from OTHER sources (>= 3 lands) AND 3+ Goblins before it beats a plain {C}
  tap. Delaying Three Tree City costs no access either -- playing it turn 2 vs turn 3 leaves the
  same three lands down on turn 3.
- Method traps, one caught and one nearly missed:
  * The mana base was first read by grepping the decklist for `"mountain|cavern|land"`, which
    silently MISSED Three Tree City and produced a confident, wrong "21 Mountain + 1 Cavern" premise.
    **Resolve card types through `cards.json`, never by name-pattern on a decklist.**
  * Having found Three Tree City, the correction over-shot the other way and recorded it as a
    "real choice the prune suppresses" -- reasoning from the card's NAME and flavour instead of from
    `ScaledManaNetYield`, which shows the mode is unreachable in the pruned window. **Read the
    model's activation conditions before pricing a card's option value.**

Two lessons, both about the audit rather than the engine:

- **Fixing one site of a decision is not fixing the decision** (third instance). The restricted-mana
  model was adopted at the payment sites; the enumerator's affordability pool was a separate site
  nobody grepped. Same shape as Dwarven Hold vs Mercadian Bazaar. Question 3 exists for this.
- **Do not let a category justify a result.** This was filed under offer/prune and then defended as
  a "faithfulness fix" -- a category claim doing work that only a measurement can do. A sweep aimed
  at one class will surface others; classify what you find, and when the finding turns out to be a
  heuristic rather than a bug, hold it to the heuristic's bar.

## Open queue (agreed order, 2026-08-01)

1. ~~**Sweep for offer/prune gaps (blind spot 1).**~~ **DONE 2026-08-02** -- see above. Nine decks
   swept with the board-nullity probe; one real bug (the non-creature pool over-credit), everything
   else benign or already-deliberate. The `nil` probe is the standing instrument for this class.
2. **Aura tutor branching -- `LightPawsAuraCandidates`.** The one tutor in the engine still picked by
   a static rank and consumed `front()`-only (`SpellEffects.cpp:227`), on the deck whose entire plan
   is which aura lands on which body. Every other tutor is searched, and the tutor axis is the
   highest-value thing this audit has produced (-0.7015 when first searched; -0.0620 more from
   tuning goblins' width alone). Unlike the legend keep, a fetched aura PERSISTS.
3. **`ManaSourceRank`.** Fires constantly; a stranded colour persists. The heuristic-optimization
   skill's worked example lives here. *(2026-09-03: DONE — worked through the whole mana
   order/reserve arc, incl. recorded non-adoptions; the worked example now sits in
   `.claude/skills/heuristic-optimization.md`.)*

Lower priority, with reasons: the three board-targeting hooks
(`BurnCreatureTargetCandidates` / `LifegainRemovalCandidates` / `OwnPumpTargetCandidates`) face a
passive goldfish opponent, so contested targets are rare -- likely another legend-rule case;
`ShouldAttackWith` (attacking is almost always right against a non-blocker); `BounceLandCandidates`
(the rule is well-reasoned, not arbitrary); the rollout cleanup-shed axis (built, measured worse,
ships inert at width 1).

## Standing instruments

`MTG_TRACE=nil` (**board-nullity** -- the offer/prune instrument; one line per board-null action of a
committed plan, with `strand=` separating "enumerated but unpayable" from "genuinely effect-less"),
`MTG_TRACE=tie` / `tiescan` (free-within-horizon + its denominator; weak, see above --
`scripts/tie_probe_report.py` aggregates them into a per-card free-rate),
`MTG_TRACE=legend` (duplicate-legend groups + what differs), `MTG_TRACE=retrace` (contention +
whether the ranking moved the pick), `MTG_TRACE=lepitch` (strict-subset rate -- i.e. whether the
pitch ORDER is observable at all), `MTG_TRACE=discard`, `MTG_CCO_AUDIT` (any tap of a
`colored_creature_only` source for a coloured pip on a noncreature spell), `MTG_ROLLOUT_STATS`
(deterministic call counts -- use these, not wall clock, which is load-sensitive).

All trace streams are compile-time-present / runtime-off: a disabled stream costs one cached bool
test, so they are safe to leave in hot paths. Verify inertness the way the pool fix was verified --
run the suite with the change's flag off and confirm every committed digest reproduces.

A/B harnesses: `test/le_pitch_power_ab.sh`, `test/th_rung0_baseline_ab.sh` (ladder vs the arbitrary
rule, plus a rebuild-neutrality control), `test/th_retrace_ab.sh`, `test/dup_legend_prune_ab.sh`
(includes the goblins/Muxus safety control), `test/th_mechanism_probe.sh` (WHICH cards leave the
hand -- use when avg win turn is too blunt).

## Method trap: overlapping seed bases (2026-08-02, cost two withdrawn adoptions)

The per-game seed is `base + gi`. So a sweep over bases 9001..9006 with N games each covers
`9001..9000+N`, `9002..9001+N`, ... -- **999 of every 1000 games SHARED**. An A/B reported as
"18 jobs better / 0 worse" over "6600 games/arm" was really ~3 independent samples (one per depth)
and ~1100 unique games, dressed up as eighteen.

It manufactured two false results in a single session, both adopted and then withdrawn:

* a `-3.72%` rollout-call win that was **`+1.87%` worse** on disjoint bases (the Goblins forced
  opening Mountain, `1fd821a` -> `c33ac9f`);
* a "bit-identical at d3/d5, differs only at d0 by 0.0010" claim about the aura ranking that was
  simply false -- properly sampled the two variants differ at every depth.

**Rule: seed bases must be spaced STRICTLY WIDER than the per-job game count**, and jobs of
different depths need their own ranges too. `logs/tie_probe/aura_big.json` is a worked example
(bases 1000000+i*10000 for 5000-game jobs).

**This trap was hit TWICE, independently, on the same day.** The other machine walked into it on a
Goblins value-leaf sweep (bases `100001..100100` at 1000 games/job: 100,000 games claimed, 1,099
distinct, a 1.3-sigma result reported as -14.4 sigma) and wrote it up as **rule 7 of
`.claude/skills/regression-testing.md`**, which is now the enforcement point -- it carries the
spacing formula (`base = S0 + i*N`), the assertion (`distinct(base+gi) == sum(games)`) and the best
tell for spotting it after the fact: **a zero-variance paired result.** An average over exactly `N`
games is an integer turn-sum over `N`, so identical per-seed deltas across supposedly independent
seeds mean one game counted `k` times. Two agents inventing the same bug from scratch within hours
is the argument for the assertion rather than the discipline. This section is the case study; the
skill is the rule.

**And a THIRD instance, this one committed.** After writing the two above, an audit of
`test/regression_cases.sh` found the trap sitting in the suite's own held-out tier: every deck's
`d0` overnight case runs **2000 games** on bases spaced **1001** (`4004/5005/6006/7007`), so each
case overlapped its neighbour by 999 games -- 27 overlapping pairs across 9 decks, 8000 games
reported per deck against 5003 distinct. Smoke and regression were clean. Fixed 2026-08-02 by
re-spacing `d0` to `4004/6006/8008/10010`; `d3`/`d5` run 1000 games on the 1001-spaced bases and
were already disjoint **by exactly one seed**, which is a trap primed to spring the moment anyone
raises those game counts. The audit is ~20 lines of Python (group cases by `(deck, depth)`, assert
`base_i + games_i - 1 < base_{i+1}`) and is worth re-running whenever a case is added.

The pattern across all three: **the trap does not announce itself.** Each instance produced a
plausible, confidently-reported number. What catches it is checking the seed arithmetic directly,
never the result's appearance.

Two corollaries learned the same day:

* **Per-job "N better / 0 worse" is only as strong as the number of INDEPENDENT samples.** Count
  unique games, not jobs.
* **A tiny delta over a small effective sample is not a tie-break, it is noise.** A `+0.0024`
  read (5 cases, one seed pair) and a `0.0010` d0 read (3 divergent games in 1000) were both
  reported as consistent findings; the first reversed and the second was a 2-1 coin flip.
