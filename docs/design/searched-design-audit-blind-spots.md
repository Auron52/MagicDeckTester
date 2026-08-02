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

### `MTG_TRACE=nil` -- board-nullity (SHARP; this is the one that found a bug)

What actually characterised the duplicate legend is stronger and **horizon-independent**: applying
the action left the BOARD exactly as it was. The copy resolved, the legend rule killed it, and the
only trace was a card gone from hand and mana spent -- neither of which the leaf evaluator prices,
which is exactly why the search could not see the loss.

So: apply the committed plan WITH and WITHOUT each action and compare a board signature (permanents
with tapped/sickness/counters/damage/auras, plus both life totals). Nine decks, 40 games each:

| deck | board-null actions | verdict |
|---|---|---|
| burn, th, knights, slivers, antilife, auras | 0 | clean |
| goblins | **25** (all Lightning Bolt) | **REAL BUG** -- see below |
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

The cost was not a wasted plan variant. The land-fold ranked "play Cavern + Bolt" as good as "play
Mountain + Bolt", so the engine **played the wrong land and wasted the turn**. Goblins gi28: old T1
Cavern + nothing (opp still 20 through T2) -> new T1 Mountain + Bolt (opp 17); same win turn T4, opp
at -7 instead of -2.

This is the third instance of one lesson: **fixing one site of a decision is not fixing the
decision.** The restricted-mana model was adopted at the payment sites; the enumerator's
affordability pool was a separate site nobody grepped. Same shape as Dwarven Hold vs Mercadian
Bazaar. Question 3 of the corrected method exists for exactly this and was not applied here.

Note also what this says about the audit's own framing: it is filed under offer/prune, but it is a
FEASIBILITY bug, not a heuristic. Per the heuristic-optimization skill's Rule 0 that makes it
something to fix against the rules, not something to tune -- and the probe was still the thing that
found it. A sweep aimed at one class will surface others; classify what you find rather than
forcing it into the category you went looking for.

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
   skill's worked example lives here.

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
