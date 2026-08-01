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

## Open queue (agreed order, 2026-08-01)

1. **Sweep for offer/prune gaps (blind spot 1).** Entirely unexplored and the highest-value class
   found so far. Look for actions whose application leaves board value unchanged. A generalisable
   probe: instrument candidate application and flag any that produce no eval delta, then ask whether
   a human would ever consider them.
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

`MTG_TRACE=legend` (duplicate-legend groups + what differs), `MTG_TRACE=retrace` (contention +
whether the ranking moved the pick), `MTG_TRACE=lepitch` (strict-subset rate -- i.e. whether the
pitch ORDER is observable at all), `MTG_TRACE=discard`, `MTG_ROLLOUT_STATS` (deterministic call
counts -- use these, not wall clock, which is load-sensitive).

A/B harnesses: `test/le_pitch_power_ab.sh`, `test/th_rung0_baseline_ab.sh` (ladder vs the arbitrary
rule, plus a rebuild-neutrality control), `test/th_retrace_ab.sh`, `test/dup_legend_prune_ab.sh`
(includes the goblins/Muxus safety control), `test/th_mechanism_probe.sh` (WHICH cards leave the
hand -- use when avg win turn is too blunt).
