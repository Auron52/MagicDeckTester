# Shadowborn Culling victim fallback trial

The user's victim order prefers a hand Shadowborn Apostle over a battlefield
Demon when Culling the Weak needs a sacrifice. Seed 30053099 turn five showed
an exception: the preferred Apostle must be cast before Culling and the line
cannot pay its later casts, whereas sacrificing the battlefield Reaper first
provides the four black mana needed for the continuation. The user approved a
narrow trial that does not expose victim selection to search.

`MTG_SB_CULLING_FALLBACK` adds one battlefield variant only for a sacrifice
ritual while the fixed victim is a hand Apostle and the battlefield choice is
a Demon. Before any plan is searched, the Shadowborn provider compares the
same no-land, cast-only line with the hand victim and the battlefield victim
using `OrderPaysSequentially` after its own cast arrangement. The fallback
survives only if the preferred line fails and the fallback pays. If the plan
omits the hand Apostle, the provider tests the same line with that cast added;
omission alone does not override the victim priority.
Plans with a land drop or noncast action retain the established victim choice
because this exact payment walk does not model those actions. `=0` restores
the previous one-victim menu.

The rebuilt `mtg` binary clears the focused seed 30053099 turn-five oracle
misses: with `MTG_SB_CULLING_FALLBACK=0`, it reports 84 APPLY-DROP-ORDER; with
the fallback on, it reports zero misses and 84 DROP-COVERED. Both arms use the
same source and differ only by the fallback switch. The focused unit fixture
passes 26 assertions, and the full suite passes 3,305 cases and 3,612,580
assertions.

A fresh 5,000-game same-binary paired A/B (seeds 8,100,001 onward) is
outcome- and play-digest-identical: both arms average 4.7538 win turns, with
zero faster, slower, or play-changed games. Work units rise from 87,406,179
to 87,414,456 (+0.0095%); summed thread time rises from 4,762,520 to
4,768,044 ms (+0.12%), inside concurrent-run noise. The manifest, game logs,
and comparison are in `build/scratch/culling_fallback_ab/`. The tier and fresh
mana-clean gate remain pending.
