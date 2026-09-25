# The activation picker is multiplicative, and the sac victim is asked one at a time

**Status: DESIGNED, NOT BUILT (2026-09-25).** Two user reports from the same seed-11 session, one
root shape. Written up rather than built immediately because the fix spans the engine's chooser
protocol and the viewer's picker, and a batch of verified, gated work was waiting to land first.

## The two reports

**USER 2026-09-25, on the activation picker:**

> "Though the chooser is pretty poor. We should have a cleaner way to choose the mode that is not
> multiplicative."

**USER 2026-09-25, on the sacrifice, candidate-B Fungus seed 11 gi 10 T4:**

> "The sacrifice for Utopia Mycon should not choose only one target if you need more than one."

then, after playing it:

> "I chose the saproling, but the Shroofus was also drawn in."

and the ruling:

> "I don't actually want it to fire twice. The right way to handle it is a multi-choice. (i.e. pick 2)"

## Report 1 — the picker is a CROSS PRODUCT

Utopia Mycon has two activated abilities, and one of them has two free axes. At the reported frame
the engine enumerated these five distinct actions on that one permanent:

| act_label | sacout | sac_count | float_color |
|---|---|---|---|
| remove three spore counters: create a Saproling | – | – | – |
| – | yes | 1 | G |
| – | yes | 1 | B |
| – | yes | 2 | G |
| – | yes | 2 | B |

`index.html`'s option builder keys each on
`(verb, mode, payload, act_label + '|sac' + sac_count + '|' + float_color)`, so the picker renders
**five flat entries** — one real mode choice plus a 2x2 cross product of colour and count. Mycon adds
"one mana of **any** colour", so the ceiling is 5 colours x N counts for a single ability.

The axes are INDEPENDENT. Which ability, which colour and how many bodies are three separate
questions, and multiplying them into one flat menu is what the user is objecting to. The fix is to
ask them separately: pick the ability, then the colour, then the victims — and the last of those is
report 2.

Note the `sameCard` / `listShape` rule in `renderDimPick` already solves the analogous problem one
layer down (a dimension whose every choice is the same card art renders as a list, not a grid of N
identical thumbnails). The activation picker has no equivalent and needs one.

## Report 2 — the victim prompt is sequential, so it never shows the whole cost

`ChooseSacOutletVictimIndex` (`core/SpellEffects.h`) short-circuits:

```cpp
if (cands.empty())     { return -1; }
if (cands.size() == 1) { return cands[0]; }   // forced -- do not prompt for a non-choice
```

and the burst loops it once per body (`ApplySacForMana`'s skirk branch, and
`ApplySacCreatureOutletBurst` for the value outlets). At the reported frame the legal Saprolings were
the `1/1 Saproling Token` **and Shroofus Sproutsire — which is itself a Saproling**. So:

* sac 1 of 2: two candidates -> prompt -> the human picked the token;
* sac 2 of 2: one candidate left -> **short-circuit, no prompt** -> Shroofus died silently.

Nothing lied: each prompt was honest and the forced pick was genuinely forced. The defect is that a
one-at-a-time shape **cannot disclose the total cost before the first irrevocable answer**. The human
committed to "sac 2" believing they were choosing the victims, and only the first choice was theirs.

This is not the `HUMAN PLAY NARROWS NOTHING` violation it looks like — the set offered at each step
was complete. It is a **disclosure** defect, which is why the fix is a different widget rather than a
wider candidate list.

## Report 1b — the same cross product is in the PLAN MENU, and it is starving the cap

MEASURED at candidate-B Fungus seed 12 gi 0 T4 (two Utopia Mycons out, `MTG_PLAY_PLANS_CAP` at its
default 200):

```
plans emitted: 200
distinct plays ignoring float colour: 49
slots spent on pure colour permutations: 151  (76% of the menu)
```

One play appears **eighteen** times, differing only in which colour each Mycon floats. The axes are
(Mycon A colour) x (Mycon B colour) x (cast ordering), and because Mycon adds "one mana of **any**
colour" the ceiling is 5^n, not 2^n — the frame only shows {G}/{B} because those are the colours
some cast could consume.

**This is worse than an ugly menu: it is the seed-8 cap defect on a bigger axis.** The display cap
keeps 200 plans, so 151 slots spent on the same play wearing different mana are 151 real, distinct
plays pushed out of the human's menu entirely. `docs/design/searched-cast-order-not-reported.md`
records the same shape for cast ORDER.

**The root cause is that the colour is not expressible any other way.** `CheckLine`'s sub-choice
dimensions (`addSub`) cover tutor, enchant, equip, modal, X, splice, replicate, devour, activations,
loyalty, free, fetch, face — but there is **no `float_color` sub**. So the only way a human can say
"float {B} instead of {G}" is to pick a different PLAN, which is exactly why the enumeration has to
fan them. Give the colour a dimension and the fan collapses to one representative plus a question.

## The design

**Three independent axes, asked independently: which ability, which colour, which bodies.**

### Viewer — ability selection is an AFFORDANCE ON THE CARD, not a modal list

**USER 2026-09-25:** *"Maybe a better option would be to separate the two abilities using a tag? So
you click on a different part of the card or the tag at the bottom?"* and *"I don't actually need the
sacrifice option very often."*

So the modal picker goes away for the common case, and the two abilities become two click targets on
the permanent itself:

* **the card body** = the primary ability (Mycon's "remove three spore counters: create a Saproling");
* **a small tag at the bottom of the card** = the secondary one (the sac outlet).

The affordance hierarchy is set by the second quote: the sacrifice is the RARE choice, so it must not
be what an ordinary click does. Today it is worse than default — it is the only thing the source flag
could produce (see the `queueActivation` fix already landed).

This also removes the cross product at the point where the user meets it: two tags, not five rows.

### Viewer — the victim is a MULTI-SELECT, and the count is its cardinality

Choosing the sac tag opens a **multi-select over the legal victims** — "pick 2" — with the count
coming from how many bodies the human selects rather than from a menu axis. This is the user's ruling
applied directly: `sac_count` stops being a cross-product dimension.

### Engine — the float colour becomes a sub-choice dimension

Add a `float_color` sub to `CheckLine` for any `SacForMana` carrying a `chosen_float_color`, kind
`"color"`, alongside the `devour` sub added the same day. Then:

* the human picks the colour in the existing dimension walk (which already renders same-art choices
  as a compact LIST rather than N identical thumbnails — the `sameCard` rule);
* the emitted-plan cap can collapse colour permutations to one representative, freeing ~76% of the
  menu at this frame for plays the human currently cannot see at all.

`CheckLine` is viewer-only (sole caller `--validate-line`), so the sub itself is GT-neutral. The cap
collapse is display-only and therefore GT-neutral too. **Narrowing the ENUMERATION's colour fan is a
separate, measured change and is NOT part of this** — it would be a real play change.

### Engine

The chooser protocol has to carry "N picks from one candidate set". Two routes were considered:

* **A keyed side channel** (the `--firebreathe` / `--jitte` / `--storage-hold` precedent, for answers
  that are not one option index). Rejected: it takes the answer off the positional `--choices` stream,
  and every saved reference would need the new channel to replay.
* **ONE frame that consumes N entries of the positional stream** (CHOSEN). The frame declares
  `pick_count: N`; the driver in `main.cpp` consumes N ints (or a single `-1` meaning "take the
  heuristic's N"). `--choices` stays a flat int stream, so references, the stateless replay and the
  protocol checker are all unchanged in shape.

Concretely: a new `MultiPickChooser` returning `std::vector<int>`, a `sacrifice_multi` decision type
carrying the full candidate list plus `pick_count`, and `ChooseSacOutletVictimIndex`'s callers
switching to a `ChooseSacOutletVictims(..., k)` that asks once for k rather than k times for one.
`g_play_*_chooser == nullptr` keeps the heuristic path byte-identical, exactly as today, so
autonomous play, the search and every rollout are untouched.

### What must NOT change

* **Defaults reproduce the autonomous line.** The heuristic's k picks are the preselected default, so
  holding enter through the dialog plays exactly what the search would have played — the property
  that lets an old reference replay against a new frame.
* **The forced case still must not nag.** When there are exactly k legal candidates for k picks, the
  dialog opens READ-ONLY (it discloses, it does not ask) rather than being skipped. Disclosure is the
  whole point of the change; skipping it is the bug being fixed.

## Related, and NOT the same bug

`docs/design/sac-fodder-created-in-the-same-line.md` — the "Widening" step. At this very frame the
human wanted to spore-activate Mycon for a FRESH Saproling and then sac two tokens, which would have
spared Shroofus entirely:

```
cast=Utopia Mycon; sacout=Utopia Mycon; sacout=Utopia Mycon                  -> legal_not_enumerated
cast=Utopia Mycon; sacout=Utopia Mycon; sacout=Utopia Mycon; cast=Doubling Season -> ILLEGAL "can't pay {4}{G}"
sacout=Utopia Mycon; sacout=Utopia Mycon; cast=Doubling Season                -> accept
```

The adopted same-line-fodder fusion only manufactures fodder when the board has **no** victim at all,
so a BURST that runs out of fodder halfway does not reach it. That is a separate change with its own
measurement, and it is the one that would have made the user's preferred line available.
