# Colossus Hammer: the cast order (USER-ADOPTED 2026-10-18)

**Status: ADOPTED, default ON** (`MTG_COLOSSUS_ORDER`, heurarm `COLOSSUS_ORDER`; =0 restores the generic
order). AI-drafted 2026-10-17 under the USER's standing ask (*"The generic order is never a good option ...
It should be edited by you and then presented to me for additional feedback"*); the USER on 2026-10-18:
*"That seems reasonable, but we should make a full order, also, we should have equip abilities listed in
the order. Path to Exile can be excluded entirely via a heuristic for goldfishing."* So the draft's nine
classes became one position per card, the board activations are positions of the order too, and Path to
Exile is never cast (`MTG_COLOSSUS_NO_PATH`, default ON). Code: `ColossusHammerProvider::CastOrderRank`,
`ActivationOrderRankFor`, `NeverCast` (src/ai/DecisionProviders.cpp, "Cast order").

## The deck's ordering problem in one paragraph

Every line that matters is "get Colossus Hammer (+10/+10) onto a double striker without paying equip
{8}". Two of the five cheats are ORDER-SENSITIVE inside one main phase: **Sigarda's Aid** attaches an
Equipment only as it ENTERS, so the Aid must already be on the battlefield and a HOST must already be
there; **Forge Anew**'s graveyard return enters through the same trigger, so it also wants the Aid and the
hosts first. **Magnetic Theft** needs both its targets on the battlefield (enforced structurally by
`ApplyAttachSpellDependencyOrder` anyway). The tutors put a card into hand mid-phase, and site 10 (the
put-in-hand breakpoint) re-solves the rest of the phase, so a tutor first leaves the most of the phase to
spend on what it found.

## The full order (all main 1)

| # | step | why here |
|---|---|---|
| 0 | the land drop | precedes every cast: holding it never pays in this list (no landfall, Karoo or storage land) |
| 1 | cast Steelshaper's Gift | the found card enters hand and the rest of the phase re-solves; first = the most phase left |
| 2 | cast Open the Armory | the same; second only because it costs more |
| 3 | cast Sigarda's Aid | must be on the battlefield BEFORE any Equipment enters |
| 4 | cast Kor Duelist | a double-strike host, out when the Aid's trigger picks one |
| 5 | cast Swiftblade Vindicator | the same |
| 6 | cast Speaker of the Heavens | a further (lifelink) body |
| 7 | cast Raubahn, Bull of Ala Mhigo | attaches at its own attack, so its timing is free |
| 8 | cast Ardenn, Intrepid Archaeologist | attaches at begin combat, so its timing is free |
| 9 | cast Lizard Blades | after the hosts (the Aid can put it ON one), before the Hammer (a Lizard left a creature -- a 1/1 double striker -- can carry it) |
| 10 | cast Forge Anew | after the Aid and the hosts: the Equipment it returns attaches as it enters |
| 11 | cast Colossus Hammer | after every host and the Aid; first of the Equipment, the kill piece |
| 12 | cast Loxodon Warhammer | |
| 13 | cast Dragonfire Blade | |
| 14 | cast Magnetic Theft | after every Equipment and creature |
| -- | Path to Exile | **never cast** (below) |
| 15 | Lizard Blades: reconfigure, unattach | a creature again before anything is equipped |
| 16 | Colossus Hammer: equip | the FIRST equip ability, so the one Forge Anew's {0} pays for |
| 17 | Loxodon Warhammer: equip | |
| 18 | Dragonfire Blade: equip | |
| 19 | Lizard Blades: reconfigure, attach | not an equip ability, so never waived |
| 20 | Speaker of the Heavens: create the Angel | last: the token is summoning-sick, nothing above wants it first (also offered in the narrow post-combat main, `MTG_SPEAKER_M2`) |

Positions 15-20 run after every cast -- the shape the USER set for KittyEquipment (*"the equip usages
should all go at the end of main 1"*): an equip is pure generic mana, so paying it after the casts lets
every coloured source go to a spell first, the hosts and Forge Anew are on the battlefield by then, and
nothing in the list reads an equipped creature while a spell is being cast. Every position is keyed on
the card's parameters, never its name; an unlisted card falls back on its class, then mana value, then
name, so plan order never decides.

**Where order does not change the result** (said so, as the USER asked to know): within 4-5, 6-8 and
11-13 nothing reads another's arrival (the Aid's host pick stacks every piece onto the best host
whichever enters first), and 17-18 commute (generic mana, different pieces). **Forge Anew's waiver**: the
engine gives it to the plan's MOST EXPENSIVE equip (`StoriedWaivedEquipId`), which position 16 makes the
first equip activated whenever the Hammer is in the plan; when it is not and Warhammer and Dragonfire
Blade both cost {3}, which one is "first" changes no mana and no board.

## Path to Exile: never cast

`ColossusHammerProvider::NeverCast` drops it at the enumeration choke point (the KittyEquipment Swords
shape), so the search, the breakpoint continuations, the rollouts and the d0 runner never see it, and
the discard reads it as dead. Its only goldfish use was the self-target ramp line (exile our own
creature, fetch a TAPPED basic), which trades a body -- usually a host -- for a land that taps next turn;
the opponent target never had any effect. Human play and `MTG_UNPRUNE=uacast` still offer it.

## Measurement of the DRAFT (2026-10-17, PGO binary of `8c1de4465`, ONE pooled batch, heurarm arms per job)

Held-out seeds (13100001+, disjoint from every suite seed), every arm the same games, loss = 9, paired per
game, diff = draft - generic (negative = faster). Script: `build/scratch/colossus/order_ab/make_ab.py`.

| cell | n | generic | draft | diff (se) | faster / slower | search units |
|---|---|---|---|---|---|---|
| play settings (d5 b20) | 3000 | 3.7917 | 3.7387 | **-0.0530** (.0043) | 160 / 2 | 0.944x |
| d3 b10 | 4000 | 3.8185 | 3.7630 | **-0.0555** (.0038) | 219 / 3 | 0.948x |
| d0 | 20000 | 5.0282 | 4.9804 | **-0.0478** (.0025) | 512 / 16 | -- |
| 2HG play settings | 1000 | 4.4500 | 4.4040 | **-0.0460** (.0071) | 49 / 3 | 0.965x |

**Every searched slower game recovers** (the adoption bar): all at stage 1 (same depth, budget 100) except
s13102419 (play cell, T5 -> T7), which recovers at stage 2 (d8, unlimited budget: T5).

**WHY it wins (the mechanism, from the logs and claude-play seed 2100014):** under the generic order Sigarda's
Aid and every Equipment tie at rank 20 and fall to PLAN order, which casts the Equipment first -- so the Aid
is not on the battlefield when the Hammer enters and its attach trigger never fires. "Aid, then Hammer
onto a host" is the deck's cheapest kill (T2 off Kor Duelist + {W} + {1}) and under the generic order it is
**unfindable at any depth or budget** (seed 2100014: generic T3 at d1..d5 unlimited; draft T2). Slot 2 (Aid
before every Equipment) is the slot that matters; the rest of the draft is tie-break.

The USER adopted it on 2026-10-18 with the amendments above; the adopted order is measured in the next section.

## Measurement of the adopted order and the Path exclusion (2026-10-18, Release binary of `df147efd3`)

ONE pooled batch, four heurarm arms per job, fresh held-out seeds (13200001+), loss = 9, paired per game
against `off` = the committed tree's play (generic order, self-target Path searched). Script:
`build/scratch/colossus/order_ab/make_ab2.py` (run2).

| cell | n | full order, Path kept | Path excluded, generic order | both |
|---|---|---|---|---|
| play settings (d5 b20) | 3000 | **-0.0557** (170 faster / 4 slower) | +0.0340 (5 / 72) | -0.0210 (167 / 69) |
| d3 b10 | 4000 | **-0.0525** (209 / 1) | +0.0340 (12 / 101) | -0.0180 (208 / 91) |
| d0 | 20000 | -0.0433 (487 / 26) | -0.0343 (528 / 181) | -0.0769 (997 / 194) |
| 2HG play settings | 1000 | **-0.0540** (56 / 2) | +0.0380 (6 / 29) | -0.0180 (61 / 28) |

Search work: the order alone 0.95x units, with the exclusion 0.86x.

**The order is a clean gain everywhere.** **The Path exclusion splits:** the d0 runner misplays Path
(it is better off without it, -0.034), but the SEARCH loses +0.034 without it. Root cause of the
search's loss, traced on s13200232 gi231 (T3 with Path, T7 without): the T2 self-target Path's land search
SHUFFLES the library, and the next draw is Colossus Hammer; without the Path the next draw is a Plains and
the Hammer does not come for four turns. The search's rollouts read the real library, and the engine's
shuffle is deterministic, so the search can SEE the post-shuffle draws: a self-Path is a "re-roll the
library when the known draws are bad" button. That is clairvoyance (USER 2026-10-13: a line that needs
known draws is not a regression), not ramp. The decoupled-shuffle arms of run3 (`shuffle_salt_search`:
the search plans against a reshuffle the game will not deal) measure how much of the +0.034 is that.

## Equipment tutors (USER 2026-10-18) and the clairvoyance check (run3, run4)

The USER, after a seed 1 reference where the AI's tutor default (library order) was not the Hammer:
*"Steelshaper and Open the Armory should always get Colossus Hammer when you have the ability to
auto-equip and a creature ... Honestly, Colossus Hammer is the pick the vast majority of the time."* /
*"We should always include it in the search, but the others we could often drop."* / *"in a real game I
would only search Hammer and Lizard Blades when there is no creatures ... In Goldfish these are not very
good."* / *"I don't know 100% if it will be lossless to never get Warhammer or Dragonfire Blade, though.
That is something to test."* / *"Certainly it will be unnecessary if we free equip."*

`ColossusHammerProvider::TutorCandidates` (`MTG_COLOSSUS_TUTOR_HAMMER`, default ON): Colossus Hammer
always and FIRST (so it is also the d0 pick and the human menu's default); Lizard Blades with no creature
to carry it, or no double striker and no cheap attach; Loxodon Warhammer / Dragonfire Blade only with no
cheap attach (`MTG_COLOSSUS_TUTOR_OTHERS`, default ON after the test below; =0 never). A cheap attach =
Sigarda's Aid, Forge Anew, Magnetic Theft, Raubahn or Ardenn, in hand or on our battlefield.

run3 (shipped search, 156,000 games, seeds 13300001+) and run4 (`MTG_HONEST_PLAY=1`, 32,000 games, seeds
13400001+: each rollout turn plans against a reshuffled unseen library), paired, negative = faster:

| comparison | play (d5 b20) | d3 b10 | d0 | 2HG |
|---|---|---|---|---|
| tutor rule vs every target, shipped search | +0.0093 (28 / 51) | +0.0018 | **-0.1610** | 0.0000 |
| ... honest search | **-0.0143** (92 / 51) | **-0.0182** | -- | **-0.0270** |
| + Warhammer / Dragonfire with no cheap attach, shipped | **-0.0070** (27 / 9) | **-0.0090** | 0 | -0.0130 |
| ... honest | **-0.0067** (33 / 17) | **-0.0123** | -- | -0.0090 |
| Path excluded vs kept (tutor rule on), shipped | +0.0367 (1 / 66) | +0.0280 | -0.0238 | +0.0240 |
| ... decoupled search shuffle (`shuffle_salt_search`) | +0.0287 | +0.0170 | -- | +0.0190 |
| ... honest | +0.0333 (7 / 75) | +0.0293 | -- | +0.0280 |

The tutor rule saves ~25% of search work. Under the shipped search it ties the every-target search; under
the honest search it WINS, because each tutor target removes a different card before the library is
shuffled, so N targets give the clairvoyant search N looks at the post-shuffle draws. Never fetching
Warhammer / Dragonfire is not lossless (both searches agree), so they stay as the no-cheap-attach
fallback. Search work vs the committed tree: 0.72x (tutor rule), 0.77x (with the fallback).

**Path, the mechanism (six traced games, run3 seeds):** in every one the draws diverge right after the
self-target Path's land search shuffles, and the faster win comes from the post-shuffle draw (247 and 325:
a tutor on T3 for a T3 kill, against Forge Anew / a second Path and no kill without it; both kills cost
three mana, so the fetched land is not what won). Only 84 plausibly used the land. Neither instrument above
removes ROOT clairvoyance -- both still SCORE each line against the true library -- so the Path question is
settled by the non-clairvoyant search (`MTG_NC_SEARCH`: each real decision ranked by the 8-reshuffle
average), run5.

**run5, the non-clairvoyant search (`MTG_NC_SEARCH=1`, play settings, 1,000 paired games, seeds 13500001+,
tutor rule on in both arms):** Path excluded vs kept **-0.0010 +- 0.0134** (22 faster / 23 slower), at **0.59x
wall**. With the search unable to read the library, the Path's value is gone: the +0.03 of run2-run4 was the
shuffle re-roll, i.e. clairvoyance (not a regression, USER 2026-10-13). The exclusion stays ON as the USER
ruled, and it is now measured lossless under honest play as well as cheaper.

## The USER's references (2026-10-18)

The USER hand-played 15 Colossus games in the viewer (`references/Colossus_Hammer/`, seeds 1-15, average
T3.27, all won). Every Steelshaper's Gift / Open the Armory pick is the Hammer except two T1 fetches of Lizard
Blades with no creature anywhere (seeds 5 and 15) -- the tutor rule exactly -- and in four games (seeds 1, 4, 7,
12) the pre-rule default named Loxodon Warhammer / Dragonfire Blade and the USER overrode it. Path to Exile
was never cast. On the rebased tree with the whole package: `viewer_protocol_check` 15/15 replay to the same win
turn (9 ok, 6 index-repaired by the reordered menus, 0 drift / enum-gap / board-diverged); `ref_bench` (the
search from the USER's exact kept hand) 0/15 short, search 3.200 vs human 3.267 -- seed 7 one turn faster
(T2 Aid + Kor Duelist instead of Open the Armory, so the Duelist is ready when the Hammer lands on T3; every card
it used was in hand at the time).

## The final package vs the pushed tree (run6, 2026-10-18, Release binary of `a84047060`)

ONE pooled batch, 84,000 games, fresh held-out seeds (13600001+), paired per game, loss = 9. `off` = every
10-18 flag off (the play pushed at `7d11c561e`); `full` = the shipped defaults (the USER's full order with the
equips in it, Path never cast, the tutor rule with the Warhammer / Dragonfire fallback, Sigarda's Aid
lethal-first); `noaid` = full with `MTG_COLOSSUS_AID_LETHAL=0`. Script `build/scratch/colossus/order_ab/make_ab6.py`.

| cell | n | full - off | faster / slower | search work | wall |
|---|---|---|---|---|---|
| play settings | 3000 | **-0.0307 +- 0.0068** | 192 / 70 | 0.71x | 0.69x |
| d3 b10 | 4000 | **-0.0250 +- 0.0058** | 250 / 113 | 0.71x | 0.69x |
| d0 | 20000 | **-0.2348 +- 0.0066** | 3710 / 1005 | -- | 0.61x |
| 2HG play settings | 1000 | **-0.0390 +- 0.0115** | 75 / 29 | 0.69x | 0.67x |

`noaid` has the same win turn as `full` in every searched game and differs by two games at d0 (-0.0001); its
play differs in 7 of 280 chunks (1 play-settings, 4 2HG, 2 d0) -- the Aid's lethal-first case (a summoning-sick
double striker vs a ready host the attach makes lethal) is real but rare, and in the searched cells it never
moved a win turn. Kept ON (the USER's ruling; it costs nothing). The package's slower games go through the two-stage recovery check (below).

**Recovery of every slower game (two-stage, USER 2026-10-05; scripts `recov6.py`, `attr6.py`):** 1,217 run6 games
are slower under `full`. Stage 1 (`--depth W --budget-ms 100`, W = off's win turn) recovers 961 (budget
truncation). The other 256 were replayed with ONE restriction lifted at a time, then at `--depth 8 --budget-ms 0`:

| what recovers it | games | verdict |
|---|---|---|
| Path allowed | 207 | clairvoyance: the Path shuffle re-rolls a library the search reads (run5: worth 0 honestly) |
| a tutor target the rule drops, fetched and NEVER cast / the win is the post-shuffle draw | 6 | clairvoyance: the tutor shuffle as a re-roll (run3/run4) |
| the full package at d8 b0 | 4 | budget truncation |
| **Lizard Blades with no double striker (the amendment below)** | **38** | **a real loss of the rule as stated** |
| nothing (the full arm's own tutor shuffle made a physically different game, s13600918) | 1 | variance |

## The Lizard amendment (2026-10-18, proposed to the USER; default ON pending the ruling)

The rule as stated admits Lizard Blades only with no creature, or with no double striker AND no cheap attach. The
38 games above are the case it misses: the only creatures are Ardenn / Speaker / Raubahn (no double strike), a cheap
attach is in hand, and the winning line fetches Lizard Blades as the double-strike BODY the Theft / Raubahn / Forge
Anew puts the Hammer on (s1001 gi118: Gift -> Lizard Blades on T1, Gift -> Hammer and Theft it onto the Lizard on
T3, 22 damage; the rule's line is T4). With no double striker the Hammer is half the damage -- the USER's "only
consider the others if it falls short". `MTG_COLOSSUS_TUTOR_LIZARD_DS` (default ON; =0 -> the rule as stated):
Lizard Blades joins (still after the Hammer, so d0 and the human default are unchanged) whenever no creature on our
battlefield or in hand can double strike. run7 (56,000 games, seeds 13700001+, paired against the package):

| cell | n | amendment - package | faster / slower | work |
|---|---|---|---|---|
| play settings | 3000 | **-0.0057 +- 0.0014** | 17 / 0 | 1.02x |
| d3 b10 | 4000 | **-0.0043 +- 0.0011** | 18 / 1 | 1.02x |
| d0 | 20000 | 0 | 0 / 0 | -- |
| 2HG play settings | 1000 | -0.0040 +- 0.0020 | 4 / 0 | 1.02x |

It recovers all 38 games (37 at stage 1, s13600710 at d8 b0), and the suite's four tutor counterexamples (smoke s1001 gi118 and 2HG gi22, regression
s2002 gi25 and gi112), at stage 1.

### Narrowed (2026-10-18, after the USER: *"Wait, if we have a creature and free attach hammer should always win in actual play?"* / *"+10/+10 will beat double strike."*)

Right for the trade it names: on the same creature the Hammer (+10/+10) beats Lizard Blades' double strike every
time. The amendment's wins are not that trade -- they get BOTH pieces and the Lizard is the double-strike BODY the
Hammer goes on (gi118: two Gifts in hand). But the broad amendment also offered the Lizard with no other route to a
Hammer, which only pays if a Hammer shows up later -- a bet the clairvoyant search can see the answer to.
`MTG_COLOSSUS_TUTOR_LIZARD_NEEDS_HAMMER` (default ON): beside a non-double-striker and a cheap attach, Lizard Blades
only when a Hammer is already reachable (in hand, on our battlefield, or a second Equipment tutor in hand);
otherwise the Hammer alone. run8 (24,000 games, seeds 13800001+) and run8nc (`MTG_NC_SEARCH=1`, 3,000), paired
against the rule as stated:

| cell | broad amendment | narrowed | work / wall (broad, narrowed) |
|---|---|---|---|
| play settings, shipped search | -0.0027 (9 / 1) | -0.0017 (6 / 1) | 1.018x, 1.012x |
| d3 b10 | -0.0060 (25 / 1) | -0.0045 (19 / 1) | 1.015x, 1.009x |
| 2HG play settings | -0.0010 (1 / 0) | -0.0010 (1 / 0) | 1.019x, 1.013x |
| **play settings, NON-clairvoyant search** | **-0.0040 (4 / 0)** | **-0.0040 (4 / 0), the same 4 games** | wall 1.024x, 1.006x |

Under honest play the broad form's extra is worth nothing (its shipped-search edge was the search knowing a
Hammer would come); the narrowed rule keeps all of the real gain. Its two slower games (s13800066, s13820383)
recover at depth W b100.
