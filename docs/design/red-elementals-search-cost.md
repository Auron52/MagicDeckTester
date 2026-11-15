# Red Elementals -- search cost (Stage 5f, agents H / H2, 2026-11-14)

Self-contained record of the Stage 5f (runtime) findings for `decks/Red Elementals/Red Elementals.cod`
(provider `RedElementalsProvider`, play = d5 b20, no keep table, no value leaf). Ledger:
`docs/design/analysis-Red Elementals.md`, section "Stage 5 runtime verification (agents H/H2)".

Two cost questions were open after Stage 4a / 4-bis:

* **(a)** Leaving `MorphProvider` costs about **+50% search units** at equal turns (s300 / s100 / s5000:
  +48% / +53% / +52%). Morph's work-cutting hooks were not inherited.
* **(b)** At budget 0 the deck spent about 130 us of wall per search unit, about 3x martyr, and one budget-0
  backstop game ran 60 s. Was that work the unit meter misses?

## (b) The "130 us/unit unbudgeted" anomaly -- EXPLAINED, not a defect

* s30123 gi2 at d5 budget 0 (the 60 s backstop game), single thread, `MTG_ROLLOUT_STATS=1`: 141.0 s wall,
  of which **[bottom-cost] 140.99 s** is the lookahead BOTTOMING of a mulligan (bottom 2: seven clairvoyant
  full-game rollouts at budget 0, 14 name-dedupe reuses). The game itself is about 0 s.
* s7400 gi197 at d5 b20 (the slowest 5a game, 78 s inside the batch): 45.6 s single thread, bottom-cost 42.0 s
  (a mulligan to 5).
* Every one of the 29 SLOW-GAME lines in the 5a batch (30-79 s at b20) is a mulligan game.
* Budgeted cost per unit is NOT anomalous. One pooled census batch with `MTG_TIER1_STATS`: d5 b20 s2002 x150 is
  254 us/unit for Red Elementals vs 344 for martyr; d3 b10 is 236 vs 260. The priced-work shares are similar
  (Red Elementals: apply 25%, simkey 19%, enum 18%, plan_emit 14%).
* **The confound in the earlier comparison:** martyr HAS a keep table, so its bottoming is a blind table read
  with no rollouts. Red Elementals has none, so each mulligan plays C(7,k)-ish clairvoyant full games, and at
  budget 0 each of those is unbudgeted. The units ARE counted (967k units for that game), so this is not work
  the meter misses.
* **Resolution:** the keep table (the next pipeline stage, the mulligan profile). Nothing to change in search.

## (a) The +50% units -- one sound fold built, one Morph hook refused, one lossy proposal

### Refused: Morph's `EndStepSacFaceUpDeadAfterCombat`

Its premise (a post-combat cast of an end-step-sacrifice creature is dead) is FALSE here. A post-combat Spark
Elemental / Hellspark / Ball Lightning cast puts the card in the graveyard at the end step, which feeds
Thunderkin Awakener's attack-trigger return next turn, Hellspark's unearth, and a post-combat Nova Chaser's
champion (it can exile the doomed body). It is a real line, so it is not taken.

### Proposal for the USER (lossy, NOT adopted): Morph's largest-attacker trick target

Morph's USER rule gives a solo-target trick ONE cast-time target variant (the largest attacker,
`kTrickBestOwnTarget`). That restriction is lossy in general (doubling a smaller creature can matter, e.g. when
a lord or a counter changes later in the line), so it is the USER's call. It is likely a large unit saver. Not
adopted; default stays every attack-eligible target.

### Built: the dead-trick fold `MTG_REDELEM_TRICK_FOLD` (SOUND dominance fold)

`RedElementalsProvider::TrickTargetsAttackersOnly` and `TrickTargetCandidates`
(`src/ai/DecisionProviders.cpp`; heurarm slot `REDELEM_TRICK_FOLD`; `MTG_UNPRUNE=tricktarget` and human play
restore every target).

* Pre-combat, Legion Leadership is not aimed at a creature that cannot attack this turn (summoning sick
  without haste, tapped -- e.g. a Soulstoke that paid for a put) or at a same-plan hand creature without haste
  access.
* Once this turn's combat is over (and the list has no extra combat), Legion Leadership is not cast at all.

**The proof, checked card by card against `src/cards/data/cards.json` for all 14 names in the 60** (agent H2):

| Premise | Why it holds in this list |
|---|---|
| Leadership's payload is until-end-of-turn combat power, no rider | "double target creature's power and it gains first strike"; first strike is unmodelled and inert vs the never-blocking goldfish |
| Nothing reads a non-attacker's power | Nova Chaser's champion reads subtype; Thunderkin reads TOUGHNESS (Leadership doubles power only) and the PRINTED toughness of the graveyard card; Soulstoke's lord/put, Mimic's counter, Bladewhirl's reveal, Lynx's ETB (nonbasic-land count), Lightning Bolt, Fiery Islet read no power |
| No cast trigger, no spells-cast count | No prowess / storm / "whenever you cast" in the 60; Soulstoke's put is not a cast and reads nothing about casts |
| No graveyard / instant count | Thunderkin returns an Elemental CREATURE card only; nothing counts instants or graveyard size (no delirium / threshold / flashback) |
| Unearth, champion, Soulstoke | Unearth acts on Hellspark only; champion exiles by subtype; a Soulstoke-PUT body is targetable once it is on the battlefield with its temp haste (attack-eligible) -- same-plan hand targets need a CAST (`SubsetHasMissingTrickTarget`) |
| No mid-combat cast window | The engine resolves Leadership in the main phase, except the doubler-after-exalted window (`MTG_DOUBLER_AFTER_EXALTED`, Combat.cpp gated on `exalted > 0`); the list has no exalted. A Thunderkin-returned attacker (tapped and attacking, from the graveyard) can therefore not be a Leadership target in EITHER arm. Real MTG allows that line (cast Leadership in the declare-attackers step after the trigger resolves); it is a pre-existing MODELLING gap, disclosed below, and the fold neither causes nor widens it |
| Not casting keeps more | The card stays in hand, which keeps its land face (Legion Stronghold), and the mana stays untapped (it can still pay a Soulstoke opponent-end-step put) |
| Hand size | With 8 cards at cleanup, casting Leadership post-combat equals discarding it minus the mana; the cleanup discard is searched over the provider's FULL ranking (`DecisionProvider::CleanupDiscardRanking`), which includes Leadership |
| Face-up TRAIL regrow fold | `MTG_FACEUP_TRAIL_STALE_PIN_FOLD` also reads `TrickTargetCandidates`' `{0}` sentinel, but only for a face-up regrow trigger; Blistering Firecat (the list's only morph) has no face-up trigger, so it is inert here |

A list revision that adds a power reader (ferocious, a power threshold, a fling), a spell-count or graveyard-
instant payoff, an exalted / attack-step cast window, or an extra combat must drop the fold.

**Disclosed modelling gap (pre-existing, both arms):** Legion Leadership cannot be cast during combat on a
creature Thunderkin Awakener returned tapped and attacking (e.g. a returned Ball Lightning, 6 -> 12). That
line is real in MTG. A USER question: model a declare-attackers-step doubler window after attack triggers
(the `MTG_DOUBLER_AFTER_EXALTED` machinery generalised)?

**Evidence the fold matters:** the 5b outliers s7400 gi45 (T4 main 2) and s1001 gi46 (T4 main 2) both CAST
Legion Leadership after combat (dead; same win turn).

### Fold A/B (agent H2) -- ADOPTED, default ON

One pooled batch (`build/scratch/redelem/H2/man_fold.json`, both arms on the same binary via the per-job flag
`MTG_REDELEM_TRICK_FOLD`, `MTG_DUMP_WINS=1`, 2,480 games). Per-game pairing by `H/wins.py`.

| cell | games | avg ON | avg OFF | faster | slower | units ON | units OFF | units |
|---|---|---|---|---|---|---|---|---|
| d3 b100 s300 | 120 | 4.3500 | 4.3500 | 0 | 0 | 2,422,724 | 2,547,313 | -4.9% |
| d3 b100 s100 | 120 | 4.2917 | 4.2917 | 0 | 0 | 1,544,137 | 1,619,915 | -4.7% |
| d3 b100 s5000 | 600 | 4.3817 | 4.3817 | 0 | 0 | 10,618,979 | 11,104,158 | -4.4% |
| d5 b20 s7100 (play) | 200 | 4.3750 | 4.3750 | 0 | 0 | 3,334,288 | 3,415,880 | -2.4% |
| d5 b20 s7400 (play) | 200 | 4.4050 | 4.4050 | 0 | 0 | 4,389,764 | 4,552,257 | -3.6% |

* **0 faster, 0 slower, net 0 on all 1,240 paired games; units -4.0% in total.** The bar (0 slower, units down)
  is met. The fold is a sound identity fold, so it ships ON (CLAUDE.md "collapse wasted search
  unconditionally").
* **Identity:** the OFF arm reproduces the HEAD binary exactly: s7100 d5 b20 4.3750 digest `ec8b883b0636ad67`,
  s7400 d5 b20 4.4050 digest `c3c1c02f1ca1ce42` (agent H's 5a batch).
* Some ON digests differ at identical win turns (s7100, s7400, s100). The plays changed (the dead
  post-combat Leadership is no longer cast) but no win turn did.
* Unit suite 3,096 / 3,096 with the new case "Legion Leadership: no target that cannot attack this turn; no
  cast after combat (sound fold)".

## What is still open on cost (for the USER)

* The remaining ~+45% units vs the Morph-routed base. The only large candidate left is Morph's
  largest-attacker trick target, which is LOSSY (one cast-time target) and so is the USER's call.
* The budget-0 slow tail is mulligan bottoming; it goes away with the keep table (next pipeline stage).
