# Snow scry: a deck heuristic, and the triggered scry as a searched decision

**USER 2026-10-06:** *"Scry should be optionally searched, but I think we should be able to design a
heuristic for it as well."*

## The decision

Every scry the Snow list makes is Marit Lage's Slumber's *"whenever Marit Lage's Slumber or another
snow permanent you control enters, scry 1"*. It fires from inside a land play (FireSnowEnterWatchers
at LandPlay's tail) or a cast (FireEtbWatchers), once per Slumber copy, and always looks at one card.

Before this change it was decided by `GenericProvider::ScryKeepOnTop`: keep every nonland, keep a
land only while fewer than two lands are in play. That is backwards for this list. Every land in it
is a snow permanent, so a land drop is +1 toward Slumber's ten and +1/+1 on every Treefolk and Owl,
and the deck wants one every turn. The rule also kept Skred, which the deck never casts
(`SnowProvider::NeverCast`).

## The heuristic (`SnowProvider::ScryKeepOnTop`)

Anchored on the user's own Snow bucket policy (`SnowProvider::CleanupDiscardCandidates`: 2 lands, or
3 with none on board; accelerants 1-2; threats 2; draw fills the rest). A discard asks "which card do
I least want in hand" and a scry asks "do I want this card in hand next", so the scry rule reads the
same buckets.

| lever | rule |
|---|---|
| neither | generic (the shipped behaviour) |
| `MTG_SNOW_SCRY=1` (BUCKET) | bottom a never-cast card (Skred), a legendary we already control or hold (Jorn, a second Slumber), and a land past the user's land quota, counting spare lands only (a land in hand that this turn's open drop will spend is not spare) |
| `MTG_SNOW_SCRY_OUTLOOK=1` (OUTLOOK) | BUCKET with a land quota of ONE (keep a land only when it would be next turn's drop), and bottom a nonland whose mana value exceeds next turn's mana + 1 |

Both are per-job heurarm levers, so one pooled batch measures every arm.

## The USER's rule (`MTG_SNOW_SCRY_USER`) -- a heuristic that PRUNES the search

USER 2026-10-06, after the BUCKET / OUTLOOK arms were measured:

> "The way I imagine it is that we create heuristic to prune the search. It should ditch lands when we
> have enough (including one to play next turn, since we want to play a land every turn to increase)
> and always ditch Skred. Overall, it should always keep Abominable Treefolk unless we have no means
> to play it. Cards that draw are usually good to keep, except for Frost Augur when we already have
> multiple draw sources. Extra Marit-Lage's slumber should be pitched because they are legendary.
> Accelerators are good unless we are lacking threats. Dragon and Owl are too slow unless we can play
> them this turn or maybe next turn."

Encoded as `SnowUserScry` (DecisionProviders.cpp). The calls the user stated as ALWAYS are **firm**
and prune the searched fan through the new provider hook `DecisionProvider::ScryVerdict` (+1 always
keep, 0 always away, -1 no verdict; default -1 for every provider, so nothing else is pruned):

* firm bottom: Skred; a legendary already controlled or held (an extra Slumber); a land once a spare
  land for next turn's drop is already in hand;
* firm keep: Abominable Treefolk while every coloured pip has a producer on board or among the lands
  in hand.

The rest are the **default** of a searched branch (the search still tries the other option): a land
when no spare is held -> keep; draw cards -> keep, except Frost Augur with 2+ draw sources
(repeatable ones on board; in hand those plus cantrips -- USER: *"Draw sources here also includes the
cantrip ones. They are great when your goal is to fill the board, which you can do as long as you have
threats."*); accelerants -> keep only with a threat in hand or in play AND a mana sink -- the hand's castable
spells cost more than next turn's mana (USER: *"good early in the game if you have none"*, *"If they let
you cast a Treefolk a turn earlier that is great. But you need to have something to use their mana for.
Acceleration that goes into nothing is a waste."*), and (USER: the snow permanents *"can even just help
Marit-Lage's Slumber themselves and get the 20/20 token"*); a card whose mana
value exceeds next turn's mana (Owl, Dragon early) -> bottom; else keep.

## The search (`MTG_SCRY_SEARCH_TRIGGERED`)

The searched land-ETB scry axis (`MTG_SCRY_SEARCH`, `docs/design/searched-scry-disposition.md`)
emits one plan variant per disposition of a land's own `etb_scry` / `etb_surveil`, pinned through
`Plan::scry_choice` and consumed by the first look inside the land play. A snow land drop with a
Slumber in play triggers its scry inside the same land play, so the same pin reaches it with no new
plumbing: with the lever on, `LandDropTriggeredScry` supplies the scry count when the land has no
look of its own.

**Not reached:** a scry triggered by a CAST (a snow permanent entering from the cast loop), Slumber's
own enter scry, and the second of two Slumbers' triggers on one land. Those stay with the heuristic,
which is also candidate 0 of the searched fan.

## Measurements

Training run (Snow smoke + regression cells, seeds 1001/2002/3003; 2,330 games per arm, of which 330
are searched d3/d5; `logs/snow_scry_ab/train1`). Paired against the shipped rule, loss scored as 9;
the base arm reproduces the committed GT on every cell.

| arm | paired delta (turns) | faster / slower | searched cells faster / slower | units |
|---|---|---|---|---|
| bucket | -0.0459 +/- 0.0068 | 139 / 50 | 10 / 6 | 1.004 |
| outlook | -0.0609 +/- 0.0066 | 153 / 29 | 13 / 4 | 0.995 |
| search (generic heuristic) | -0.0004 +/- 0.0010 | 3 / 2 | 3 / 2 | 1.051 |
| bucket + search | -0.0472 +/- 0.0068 | 142 / 50 | 13 / 6 | 1.094 |
| outlook + search | -0.0618 +/- 0.0066 | 156 / 30 | 16 / 5 | 1.076 |

Held-out run (fresh seeds 8106000/8206000/8306000; d0 2,000 games, d3/b10 600, d5/b20 300 per
arm; `logs/snow_scry_ab/heldout1`):

| arm | paired delta (turns) | faster / slower | searched cells: delta, faster / slower | units |
|---|---|---|---|---|
| outlook | -0.0724 +/- 0.0065 | 187 / 23 | -0.030, 29 / 5 | 1.029 |
| search (generic heuristic) | -0.0038 +/- 0.0014 | 14 / 3 | -0.012, 14 / 3 | 1.075 |
| outlook + search | -0.0745 +/- 0.0065 | 193 / 23 | -0.037, 35 / 5 | 1.109 |

Reading: OUTLOOK replicates on held-out seeds (every cell improves, d0 -0.0915). Searching the
land-drop scry adds about 6 more faster games in 900 searched games on top of OUTLOOK, for ~8% more
search units. BUCKET was dropped after training (OUTLOOK dominated it on every summary).

Held-out run with the USER rule (same seeds, refined rule: cantrips count as draw sources;
accelerants need a threat in hand or play AND a mana sink; `logs/snow_scry_ab/heldout3_user`):

| arm | paired delta | faster / slower | searched cells: delta, faster / slower | units |
|---|---|---|---|---|
| outlook | -0.0724 | 187 / 23 | -0.030, 29 / 5 | 1.029 |
| USER rule | -0.0738 | 216 / 48 | -0.036, 37 / 9 | 1.022 |
| USER rule + land-drop search (pruned) | -0.0738 | 217 / 49 | -0.036, 38 / 10 | 1.072 |
| outlook + land-drop search | -0.0745 | 193 / 23 | -0.037, 35 / 5 | 1.109 |

The USER rule matches OUTLOOK overall and leads on the searched cells, but moves more games both ways
(48 slower vs 23, mostly d0). With its firm calls pruning, the land-drop search has almost nothing
left to branch on and buys nothing (+1/-1 game) for 5% more units.

## Status

Levers default OFF; adoption (which heuristic, and whether the triggered scry is searched by default)
is the USER's decision. Note the tension with the no-greedy-in-the-search-window rule: with the
search lever off, the land-drop scry is a heuristic pick inside the window; with it on, cast-triggered
scries still are.
