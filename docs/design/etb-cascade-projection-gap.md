# The ETB-cascade projection gap

**Status: CLOSED 2026-08-30.** The call is in (`ApplyPlanDirect`'s noncreature-permanent branch,
beside `FireEtbWatchers`). Byte-identical across all three tiers — smoke 48/48, regression 80/80,
**overnight 192/192** — plus scenarios 42/42 and units 702/702. Kept because the *shape* of how this
stayed open for so long is worth more than the one-line diff.

## The gap

> **Renamed 2026-08-30, and this document is why.** The two enter cascades were named after the
> first deck that needed each, and both names were wrong by the time a dozen archetypes hooked them:
>
> | was | is | fires |
> |---|---|---|
> | `OnGoblinEnters` | **`FireOwnEtbTriggers`** | the ENTERING permanent's own "When this enters, ..." |
> | `OnDragonEnters` | **`FireEtbWatchers`** | OTHER permanents watching the entry |
>
> The tribal name is a direct cause of the gap below: a missing `OnGoblinEnters` in the search's
> noncreature-permanent projection reads as a Goblins-only concern, so it sat unfixed while it
> silently cost an Enchantment its ETB token. Older text and commit messages use the old names.

Two code paths bring a permanent onto the battlefield:

| path | fires the param-gated ETB cascade (`FireOwnEtbTriggers`)? |
|---|---|
| the executor, `EffectHandler::EnterBattlefield` | **yes**, on every entry, creature or not |
| the search's projection, `ApplyPlanDirect` — creature branch | yes (always did) |
| the search's projection, `ApplyPlanDirect` — noncreature-permanent branch | **no** ← the gap |

`FireOwnEtbTriggers` is the shared ETB cascade, and nothing about it is tribal — it reads
`etb_damage_any`, `etb_damage_each_opponent`, `etb_damage_devotion_color`, `etb_reveal_count`,
`etb_self_creates_tokens`, `etb_opp_creates_tokens`, `etb_team_pump_per_creature`, `etb_life_floor`,
`etb_destroy_own_noncreature_max`, `etb_opp_creatures_debuff`, `tutor_to_hand`, `tutor_to_top`,
`tutor_types`.

So the search **under-projected** any such permanent: it planned as though the ETB does nothing,
then the executor fired it for real. An fd-diverge in the same family as the Puresteel note in
`TurnSolver.cpp`.

## The surface is exactly ONE card — and it is the card under active development

Counted mechanically against `cards.json` on 2026-08-30 (cascade params come only from the JSON
`parameters` object; no template injects one, so the count is complete):

| kind | count | reaches the gap? |
|---|---|---|
| creatures | 17 | **no** — taken by the `else if (is_creature)` branch, which has fired `FireOwnEtbTriggers` (with `tutor_target`/`chosen_x`) all along |
| instants / sorceries | 8 | **no** — excluded by the branch's own `!IsInstant() && !IsSorcery()` |
| noncreature permanents | **1** | **yes** |

That one card is **Frontline Heroism** — `{2}{R}` Enchantment, `etb_self_creates_tokens: 1`
(a 1/1 red Soldier with haste). `is_creature` is `def.card.IsCreature()`, so even artifact creatures
take the creature branch; nothing else can arrive here.

Which is why the tier is byte-identical and yet this mattered: **Frontline Heroism is the card the
Instigator-slot screen chose** (`mirrorwing-instigator-slot-screen.md`, 4-of, −0.4431 avg win turn),
and it ships in `decks/Mirrorwing Dragon/v3-heroism-draught/`. Under the gap the search valued a
3-mana enchantment at *nothing* while the real game got a hasty attacker off it.

### Measured on the deck it affects — −0.268 avg win turn

Same binary either side of the one-line change (the pre-fix control is the harness's own
`logs/snapshots/overnight-baseline`, saved at the accept immediately before the edit), v3 list on the
shipped Mirrorwing profile, d5/b20, 4 seeds x 1000 games:

| seed | gap open | fixed | delta |
|---|---|---|---|
| 930001 | 4.6100 | 4.3350 | −0.2750 |
| 930002 | 4.6030 | 4.3360 | −0.2670 |
| 930003 | 4.6010 | 4.3340 | −0.2670 |
| 930004 | 4.6010 | 4.3370 | −0.2640 |
| **mean** | **4.6038** | **4.3355** | **−0.2683** |

4/4 seeds, and the effect is ~25x the between-seed spread (range 0.011) — this is not a
close call. It is also ~24% cheaper per game (~950 s -> ~725 s per 1000 games): the search reaches
lethal in fewer turns, so there are fewer turns to search.

Note the direction for the screen: its verdict was measured with the card **under-projected**, so the
fix is pure upside for the arm that already won — the −0.4431 conclusion is safe, but its magnitude
is understated and the ladder is worth re-running.

## The lesson, which this document got wrong itself

Three comments have stood at this line. All three asserted the reachable set from a hand-written
list rather than counting it, and the first two were wrong in opposite directions:

1. *"all 8 cards carrying a cascade param are instants or sorceries, which never reach this branch"*
   — **false**; 17 are creatures. This is why the change looked inert and was dropped.
2. *"18 PERMANENTS carry a cascade param against 5 instants/sorceries"* (written here, 2026-08-30,
   while correcting #1) — the count is right but the **conclusion is wrong**: those permanents are
   creatures, and creatures take the other branch. It overstated the surface by 18x.
3. The current comment, which states the split and how it was derived, so the next reader can
   re-derive it in one command instead of trusting it.

**A byte-identity claim in a comment is a measurement with an expiry date.** #1 may well have been
true when written and was trusted long after `cards.json` moved underneath it. #2 shows that
*re-counting* is not enough either — you have to count against the branch that actually executes.

## Why it was blocked, and what unblocked it

`minotaur_regression_d5_s2002` / `_s3003` were nondeterministic in the full tier, independently of
this line — the control commit flaked at the same rate — so no GT movement here could be told apart
from the flake. Root-caused and fixed 2026-08-30 (`1e6a9c23`: a `thread_local` probe-structure array
read by decks that never write it; see `minotaur-d5-regression-flake.md`, CLOSED). With the tier
deterministic again the change validated in one pass.

Historical note, since it caused confusion across machines: another agent's commit message describes
`c4e9930b` as "removing the OnGoblinEnters projection call" (quoted verbatim; that was the function's
name at the time — see the rename note above). That is a misreading — `FireOwnEtbTriggers`
itself was never removed (12+ call sites throughout), and the *projection* call was never committed
at all (it lived in a stash). `c4e9930b` is where it was deliberately left out, with the KNOWN GAP
comment that this document was written to explain.

## Retracted claims — recorded so they are not re-derived

Both were published during the investigation and are **wrong**:

1. *"The `OnGoblinEnters` line causes the minotaur failures."* It does not. The cells flaked with the
   line absent, and on the unmodified control commit. The real cause is in `FullSearchLineHybrid`.
2. *"`ProfileCache` eviction is the root cause."* It is not. Asserted from ONE clean run at a raised
   cache cap; repeated five times, 3/5 still failed.

Both came from concluding on a **single run of a flaky thing**. On this suite, treat one run as one
sample of a distribution until proven otherwise, and repeat every arm of a comparison before
believing the contrast.
