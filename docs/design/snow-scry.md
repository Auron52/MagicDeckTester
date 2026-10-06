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

Held-out run: see below.
