# Glittering Wish + Open the Armory candidate rules (Bruna) -- status 2026-10-07

Branch `wish-finish` (local, NOT pushed), on `phase-1-2-deck-analyzer` @ `15bf177a`. **NOT landed**: Bruna
is out of the regression suite (USER 2026-10-06, it failed the 3x suite-cost rule) and is not re-added here.
This work exists to bring Bruna back under 3x (budget: 3 x FiveColour = **3100.38 ms/game**, per-game core-ms
at the deck's worst searched regression case). Everything below can be picked up from this doc alone; the
raw logs are under `logs/wish/` (gitignored) in the `wish-finish` worktree.

**Where it stands**
* Both tutors are narrowed by the USER's own conditional rules; the full-width control is byte-identical to
  the pre-rule engine and the two Wish cuts / three Armory exclusions / the Greaves variants are MEASURED.
* Every round-5 Wish worse game is dispositioned (below); the only unrecovered ones not explained by a
  different kept hand or an in-set allocation are the USER's two Wish cuts.
* The census: the tutor axis fell from 30.0% to 9.3% of scored plans, total units 0.725x; the mulligan-
  bottoming playouts are now 71% of Bruna's units -- the next lever, and a USER decision.
* The final measurement on a NEW held-out seed set (pooled batch D) is RUNNING; its sections are marked pending.

## The rules as they stand (`BrunaProvider::TutorCandidates`, src/ai/DecisionProviders.cpp)

Both tutors read ONE census (`BrunaTakeCensus`: next turn's mana by colour, the cheat paths, the hosts,
what is owned) and ONE mana-short test (`BrunaManaShort`), so Troyan (Wish) and Wild Growth (Armory)
agree by construction. Roles come from PARAMS, never names. Executor, rollout, d0 and search all read
this hook; `MTG_UNPRUNED` and human play keep every legal name, and the viewer marks each rule's set
as "suggested" (`TutorMarksSuggested`: Glittering Wish AND Open the Armory).

### Glittering Wish (round 5 + the Shusher fix)

USER 2026-10-07: *"I would leave out Vexing Shusher. Others should be skippable based on the current
conditions."* / *"Troyan is basically good if you need acceleration and not worth it otherwise. An aura
may not be necessary if we have good ones (and enough for lethal) in hand."* / *"if I have the choice to
cast bruna or Linvala next turn then we should definitely cast Bruna."*

1. **Bruna** (`attack_gather_auras`): unless a copy is in our hand or on our battlefield (legend rule),
   and unless lethal is already deliverable on a creature we have (ENOUGH FOR LETHAL, on-board hosts).
2. **Primary Aura**: the payload Aura (power grant / base setter / colour grant; not Wings, not a land
   Aura) of mana value > 3 with the highest resulting power over our hosts. **Skipped when the hand
   already holds ENOUGH FOR LETHAL** (definition below).
3. **Body = Linvala only** (the POOL's hardest-hitting non-dork creature of mana value <= 3): only when
   nothing can carry the Auras -- no non-dork creature and no Aura-wearer on our battlefield, none of
   mana value <= 3 in hand -- **and Bruna cannot come down next turn** (in hand or fetchable; next turn's
   mana covering {3}{W}{W}{U}, creature-only Sage mana and Troyan's big-spell mana counted, colours
   checked). **Vexing Shusher is out entirely** -- including once Linvala has left the sideboard (round 5
   let the body role pass to him then: 2 of 11,666 committed wishes; fixed 2026-10-07, unit-tested).
4. **Troyan** (`mana_only_spell_min_mv`): only when **mana is short** (`BrunaManaShort`): next turn's
   supply (lands / rocks / unrestricted dorks at their yield, +1 for a land drop) is below the largest 5+
   cost among the hand's creatures (Bruna; creature-only mana counts), the hand's noncreature spells -- a
   payload Aura only when no gatherer is on our battlefield (Bruna puts it on for free) and a host exists
   -- Bruna if this wish can fetch her, and the primary when it is offered.
5. **Cheap Aura** (MV <= 3, at most one): a second Aura beside an offered primary, only with no
   cheat-into-play path (colour-aware: Bruna on board or castable next turn; Wings on board with U, or
   in hand with a creature, mana and U for cast + swap) and a creature of ours to wear it (a mana dork
   included). **LETHAL NOW**: offered even without a primary when this turn's ready attackers (Aura- and
   haste-aware) fall short of the opponent's life by no more than it adds.
6. **Regrowth** (Auroral Procession / Reborn Hope): only when it restores a cheat path -- it can return
   Arcanum Wings from our graveyard, no cheat path exists otherwise, and a payload Aura is in hand.
7. Everything else is excluded (Detention Sphere, Vexing Shusher, a non-top Aura, other regrowths).
   Order (front = base plan / rollout / d0 pick): Bruna, primary, Linvala, Troyan, cheap Aura, regrowth.
   **Nothing missing** (empty set): ONE name, the primary (else the first legal name), never the pool.

**ENOUGH FOR LETHAL**: a host H with power(H + what H can receive) + our other creatures' power >= the
opponent's life (20, or 2HG's shared 30). Hosts: our creatures (CombatPowerOf), creature cards in hand
castable next turn, Bruna / Linvala if this wish could fetch them and they are castable next turn. A
gatherer receives every payload Aura in hand AND graveyard (no mana); any other host the best subset of
the hand's payload Auras that fits next turn's unrestricted mana and colours, **not counting a
host-tapping Aura** (Colossification taps the creature it enters on). Timing (haste) is not modelled.

Control: `MTG_WISH_FULL_WIDTH=1` (heurarm `WISH_FULL_WIDTH`) = GenericProvider's 11 names.

### Open the Armory (NEW, USER spec 2026-10-07)

USER: *"Open the Armory should only have Colossification, Arcanum Wings, Wild Growth and maybe Eldrazi
Conscription as targets. Though, to be honest, I don't think we even need the Conscription when
Goldfishing. Wild Growth is not needed unless we need mana or acceleration and Arcanum Wings is only
useful if we don't already have one. Technically Colossification is not needed if we already have one
either."* / *"Actually, we also need to consider Lightning Greaves."* / *"So, up to 5 options, but most
can be dropped based on the circumstances."* / *"Actually, I guess 4 is enough really."* / *"And then we
could heuristically eliminate more of them since you rarely have need of all of them."* / *"The greaves
condition we need to be a bit careful with, because it can be a setup turn for Bruna to land."*

At most FOUR roles, each conditional (`BrunaArmoryCandidates`):

1. **Primary Aura (Colossification)**: the library's highest-power payload Aura -- only when it is the
   DECK's best payload (no stronger one in our hand, on our battlefield or in our graveyard, so Eldrazi
   Conscription / Mythic Proportions / Prodigious Growth never inherit the role when every
   Colossification is drawn) and **no copy is in our hand or on our battlefield**.
2. **Aura swap (Arcanum Wings)**: only when **no Aura-swap Aura is in our hand or on our battlefield**.
3. **Land-Aura ramp (Wild Growth)**: only when **mana is short** -- the SAME `BrunaManaShort` test as
   Troyan, with this tutor's own fetch (the primary when offered) and Bruna reachable through a
   Glittering Wish in hand as the extra needs.
4. **Haste Equipment (Lightning Greaves)**, FORWARD-LOOKING: no haste Equipment in our hand or on our
   battlefield, no gatherer of ours that can already attack, and a creature that would attack at once
   is COMING -- Bruna in hand or through a Glittering Wish in hand, a non-mana-dork creature card in
   hand, or a non-dork creature of ours that entered this turn (before combat). Nothing needs to be on
   the battlefield yet: "it can be a setup turn for Bruna to land".
5. Excluded: Eldrazi Conscription, Mythic Proportions, Prodigious Growth (USER list; their cost is
   measured below and reported -- the USER decides).
   Order: primary, swap, ramp, haste. **Nothing missing**: ONE name, the library's top payload.

Control: `MTG_ARMORY_FULL_WIDTH=1` (heurarm `ARMORY_FULL_WIDTH`) = GenericProvider's library list
(byte-identical to the pre-rule engine on the Armory axis).

Instruments: `MTG_TRACE=wishproof,armoryproof` (at a REAL resolution the rule's set in any arm, tagged
arm=R/C) and `MTG_TUTOR_CHOSEN_RANK=1`, whose `[tutor-chosen]` line now carries ` ## rule=<arm>{set}`
(the same resolution's rule set) and ` ## job=<batch job>` -- so a control arm's committed fetch is
attributable inside one pooled batch. `MTG_TRACE=wishcands,armorycands` print every evaluation.

Unit tests (test/unit/test_bruna_sideboard.cpp): 14 Wish cases + Shusher-out-entirely + 6 Armory cases
(at most the four names / the control gives all seven; Colossification only when none owned, and no
lesser payload inherits the role; Wings only when none owned; Wild Growth only when short -- agreeing
with Troyan on the same board; Greaves forward-looking, both sides of each clause incl. a summoning-sick
Bruna before combat; nothing missing -> one name).

## Round-5 verification on the final Wish binary (f4881764 = `mtg_final3`; logs/wish/abH*, abG*, st1_final3*, b2 S2_*, recH_d5*, diag3/)

A/B: the rule arm (abH, f4881764) vs the full-width control (abG/abF; the control is byte-identical across
these commits -- re-verified: the refactored binary's Armory-full arm reproduces abH's rule digests on all 7
d5 cells). Cells = Bruna's 21 removed suite rows (smoke/regression = train, overnight = "held-out" of
rounds 1-4; both have since been used for tuning, so a NEW held-out set is in the final measurement).
Loss = 9. "Worse" = the control won sooner.

| cell group | n | better | worse | net turns |
|---|---|---|---|---|
| overnight d0 (4 cells) | 8000 | 262 | 106 | -172 |
| overnight d3 (4 cells) | 4000 | 34 | 36 | +4 |
| overnight d5 (4 cells) | 2000 | 27 | 20 | -8 |
| train d0 | 2000 | 68 | 19 | -58 |
| train d3 | 450 | 4 | 3 | -1 |
| train d5 | 225 | 2 | 1 | -1 |
| train 2HG d3 | 50 | 1 | 1 | 0 |

**Two-stage recovery of every worse game** (the USER's spec: stage 1 `--depth <control win turn>
--budget-ms 100 --ignore-play-profile`; stage 2 `--depth 8 --budget-ms 0`; `--seed base+gi --game-index gi`):

* **d0: 125 of 125 recover at stage 1.** (d0 has no bottoming, so the kept hand is the same in both arms;
  every d0 worse game is a greedy-line difference the searched rule finds.)
* **Searched (d3/d5/2HG): 61 worse games.** Each was replayed single-game in both arms with the proof
  traces (kept hand, every committed fetch vs the rule's set, first divergence):

| disposition | games |
|---|---|
| recovered at stage 1 | 16 |
| recovered at stage 2 (d8 b0) | 17 |
| **USER cut -- Vexing Shusher**: unrecovered; the control's winning line wished for Shusher (outside the set) | **18** |
| **USER cut -- Bruna over Linvala**: unrecovered; the control wished for Linvala, which the rule drops because Bruna can come down next turn | **4** |
| kept hand differs (the bottoming playouts read the rule; physically different games) | 3 |
| control's line inside the set (search/EV allocation) | 3 |

  The three "inside the set" games: s4004 gi58 (d3 and d5, same seed: the control wished for Unflinching
  Courage, which the rule also offered); s6006 gi996 (the rule arm's T1 Remote Farm spends both
  depletion charges by T3 and draws no W/U source -- an expected-value line that the d8 b0 search also
  plays; the control's only out-of-set fetch was Indrik Umbra over Almost Perfect, equal on Bruna).
* **d3 gi117 (round 3's open case, d3 b10 s10.2M): gone** -- both arms win T5 on the final binary.
* **Every unrecovered loss that is not a different hand or an in-set allocation is one of the USER's two
  Wish cuts.** The Shusher states are uniform: a T2-T4 wish, no carrier on board or in hand, Bruna not
  castable next turn -- i.e. exactly the state where the body role is live and Linvala IS offered -- and
  the control takes the 2-mana hybrid {R/G}{R/G} body over Linvala's {1}{W}{U} (Arcanum Wings in hand in
  ~60% of them). The one mana and the green-castable cost let body + Wings (or body + a big Aura) land a
  turn earlier. Their cost is MEASURED directly below (temporary slots `MTG_WISH_ADD_SHUSHER`,
  `MTG_WISH_LINV_ANYWAY`), not inferred from these games.

**Round-5 Wish candidate counts** (abH, 11,666 committed wishes incl. d0): 1: 33.7%, 2: 20.3%, 3: 16.9%,
4: 21.8%, 5: 7.2% (mean 2.48; round 2 was 1: 9.5%, 2: 16.4%, 3: 30.2%, 4: 43.9%). Round 5 still offered
Vexing Shusher in 2 of those wishes (Linvala had left the sideboard and the body role passed to him) --
fixed: the body role is the POOL's hardest hitter only.

## The USER's two Wish cuts -- measured cost and proposed amendments (b3, temporary slots, now deleted)

Same pooled batch, coupled (the Wish does not shuffle, so its fetch is not a reshuffle lottery), every one of
the 21 removed suite rows' searched cells (6,725 games), each arm paired against the final rule:

| arm (one change to the final rule) | better | worse | net turns | per game (se) | units |
|---|---|---|---|---|---|
| **Vexing Shusher back** -- the cheapest body offered beside Linvala whenever the body role is live (round 4's role) | **38** | **7** | **-34** | -0.0051 (0.0011) | 1.010x |
| **Linvala even when Bruna can come down next turn** (drop "Bruna over Linvala") | 11 | 8 | -5 | -0.0007 (0.0008) | 1.015x |

* **"Leave out Vexing Shusher" costs 34 turns in 6,725 games (4.6 se).** In the Shusher arm he is offered in 799
  of 2,491 committed wishes (32% -- exactly the need-a-carrier state, always beside Linvala) and taken in 56 (7% of
  offers, mostly T2-T3). Root cause (the 18 unrecovered round-5 losses, all the same state): a T2-T4 wish with no
  carrier on board or in hand and Bruna not castable next turn; the 2-mana hybrid {R/G}{R/G} body is castable off
  the deck's many green sources and leaves the mana for body + Arcanum Wings (or body + a big Aura) the same
  turn, where Linvala's {1}{W}{U} needs W AND U and one more mana. **Proposed amendment (USER's call, not
  adopted): keep Shusher out everywhere EXCEPT the need-a-carrier state, where he joins Linvala** -- that is
  exactly the measured arm (+1 candidate in a third of wishes, +1.0% units).
* **"Bruna over Linvala" costs nothing measurable (-5, < 1 se).** Its 4 unrecovered round-5 losses are one
  state: Linvala castable THIS turn, right after the wish (in 2HG gi13 with Greaves she even attacked that
  turn), while Bruna is only castable next turn -- outside the comparison the USER described ("cast Bruna or
  Linvala NEXT turn"). An amendment "Linvala stays when she can be cast this turn" fits those games, but the
  previous attempt read it at the wish's resolution, where it depends on which lands the unsearched payer
  tapped, and it did not recover s4004 gi65 at d8 b0; with the clause neutral in aggregate the recommendation
  is to KEEP the USER's clause as is.

## Open the Armory -- development, proof, exclusions

### Armory v1 -> v2: deriving the Lightning Greaves condition (logs/wish/b2*, diagA/)

**v1** (first cut of the USER's forward-looking clause): Greaves only when no haste Equipment is owned,
no gatherer of ours can already attack, AND a creature that would attack at once is COMING -- Bruna in
hand or through a Glittering Wish in hand, a non-dork creature card in hand, or a non-dork creature that
entered this turn. A/B on all 21 removed suite rows, rule (H) vs the Armory-only control
(`MTG_ARMORY_FULL_WIDTH=1`, Wish rule on in both arms), coupled (the default search):

| cells | n | better | worse | net turns | units H/A |
|---|---|---|---|---|---|
| overnight d3 (4) | 4000 | 45 | 49 | +7 | |
| overnight d5 (4) | 2000 | 18 | 19 | +1 | |
| train d3 + d5 + 2HG | 725 | 12 | 14 | +2 | |
| **searched total** | 6725 | 75 | 82 | **+10** (+0.0015/game) | **0.916** |
| d0 (5 cells) | 10000 | 790 | 327 | -828 | (no search) |

d0's large gain is the FRONT pick: the control's front is library (shuffle) order, the rule's is
Colossification / Wings / Wild Growth / Greaves.

The 82 searched worse games, each replayed in both arms with the proof traces:

| disposition | games |
|---|---|
| control's Armory fetches inside the rule's set | 29 |
| control never cast Open the Armory (the rule arm did) | 7 |
| no Armory in either arm | 3 |
| kept hand differs (bottoming playouts) | 5 |
| control fetched **Lightning Greaves** outside the set | 14 |
| control fetched an excluded payload: Eldrazi Conscription 11, Prodigious Growth 7, Mythic Proportions 4 (overlapping) | 19 |
| control fetched Wild Growth (not short) / a second Arcanum Wings | 4 / 2 |

**A confound that does not exist for the Wish: Open the Armory SHUFFLES.** A different fetch reshuffles
the library, so every later draw differs, and the default search pre-sees that reshuffle (search salt ==
executor salt). A full-width Armory therefore holds seven "lottery tickets" for a lucky draw order, and
some control wins are visibly that: s5005 gi335 took a second Eldrazi Conscription (+10) over the
offered Colossification (+20) -- strictly dominated as a payload -- and won; s4004 gi102 won because Bruna
was drawn T5 instead of T6 after the fetch. Repo doctrine (heuristic-optimization skill): a coupled
measurement alone can never justify WIDENING a fetch/tutor-class decision; the
`MTG_SHUFFLE_SALT_SEARCH` decouple ensemble is part of the proof. Both views are reported below.

**The Greaves states** (all 14): no Greaves owned, no Bruna on the battlefield, payload Auras in hand, and
NO non-dork creature in hand -- the attacker that the control's Greaves later hasted came from a later draw
or a later Glittering Wish. v1's "coming" clause only looked at the hand, so it excluded exactly these.
**v2** drops that clause: Greaves whenever none is owned and no gatherer of ours can already attack. Every
one of the 14 states is covered by v2.

**The decouple ensemble decides it** (b3: `shuffle_salt_search` 101 and 202, the four overnight d5 cells,
4,000 paired games per comparison; the search plans against a reshuffle the executor will not deal, so a
fetch can no longer be chosen for the draw order it produces):

| comparison | coupled (b2, 2,000 d5 games) | decoupled (b3, 4,000) |
|---|---|---|
| v1 vs full width | 18 better / 19 worse, +1 | **61 better / 47 worse, -20** |
| v2 vs full width | -- | 58 / 47, -16 |
| v2 vs v1 | 20 / 6, -14 (all 21 train rows' searched cells, 6,725 games; d5 alone 6 / 3, -3) | 4 / 8, +4 |

With clairvoyance removed the v1 rule is BETTER than full width, and widening Greaves to v2 buys nothing
(slightly worse) -- while coupled, v2 looks 14 turns better: the same reshuffle-lottery signature as the 14
coupled Greaves losses. Repo doctrine (a coupled measurement alone can never justify widening a fetch-class
decision) and the decoupled data agree. (The decouple ensemble covered the d5 play cells only; a decoupled d3
check of v2 is the one open item if the USER wants Greaves wider.) **Greaves stays v1** (the condition
above): no haste Equipment owned, no gatherer that can already attack, and Bruna (in hand or through a
Glittering Wish in hand) or another non-dork attacker in hand / entered this turn -- the "setup turn for
Bruna to land" is covered whenever Bruna is reachable; a Greaves fetched for an attacker that is not yet
reachable at all does not pay once the search cannot see which card the reshuffle brings.

### Exclusion costs and the held-out Armory proof

Pending the final pooled batch (D): coupled add-back arms on every held-out searched cell and decoupled ones on the held-out d5 cells.

## Candidate-count distributions (committed resolutions, all cells incl. d0; b2 = the final rules)

| tutor | 1 | 2 | 3 | 4 | 5 | mean | full width (legal) |
|---|---|---|---|---|---|---|---|
| Glittering Wish (23,275 wishes) | 34.0% | 20.4% | 16.9% | 21.7% | 7.1% | **2.47** | 10.74 |
| Open the Armory (6,261 resolutions) | 25.4% | 36.6% | 29.0% | 9.0% | -- | **2.22** | 6.67 |

Round 2's Wish was 1: 9.5%, 2: 16.4%, 3: 30.2%, 4: 43.9% (mean 3.1). The 5-wide Wish sets are the early
five-way state (Bruna | Almost Perfect | Linvala | Troyan | Unflinching Courage). The most common Armory sets:
Wings | Wild Growth, Colossification | Wings, Colossification | Wings | Wild Growth, Wings | Wild Growth |
Greaves, and all four.

## Final measurement (pooled batch D: train cost rows + a NEW held-out seed set)

Pending.

## Branch-source census -- "What other major branch sources do we have?" (USER 2026-10-07)

Run: `scripts/turn_census_run.sh decks/Bruna 5 20 9 <out> 24` (Bruna's play settings d5 b20, the profile
attached, 24 single-threaded processes x 9 games = 216 games, seeds 8,950,000+ -- the 2026-10-05 census's
seeds) with `MTG_BRANCH_STATS=1` exported for the per-axis / per-card plan tables. Units are the budget's
work units (deterministic). Three configurations on the same 216 games: the final rules (`census_r6`, play-identical to the final binary), the Wish rule with the Armory at full width (`census_A`), and both tutors at full width (`census_C`, the pre-rule engine).

**1. Mulligan-bottoming playouts are still the dominant cost: 71.4% of all units** (2026-10-05:
68.7%). Every mulligan triggers clairvoyant bottoming playouts (one full d5/b20 game per legal bottom
subset, up to C(7,2)=21) before the real game; their decision mix is the same as real play (same sites,
same turns), they are simply 2.5x its volume. Real games: 28.6% of units (929 decisions vs the probes' 2,903).

**2. Units by consume site** (real games; probes are within 1 pp of each):
la_cand 37.0% (candidate scoring at the lookahead), rollout_step 27.8% (rollout plies), greedy_fallback 26.3% (the
d0 horizon leaf -- the permitted greedy beyond the horizon), la_bp_wave 6.2% (breakpoint waves), fs_main2 1.7%, fs_pre 0.7%,
the rest < 0.3%. Mana payment is not a budget site: 1.70 payment solves per charged unit (real games).

**3. Scored plans by decision axis** (`MTG_BRANCH_STATS` "post-dedup axis"): 

| configuration | total units | real-game units | scored plans: tutor-target axis |
|---|---|---|---|
| both tutors full width (pre-rule) | 41.25M | 14.93M | 4.99M (30.0%) |
| Wish rule, Armory full width | 32.00M (0.776x) | 10.01M (0.670x) | 1.68M (12.9%) |
| **both rules (final)** | **29.92M (0.725x)** | **8.56M (0.573x)** | **1.13M (9.3%)** |

Every other scored plan is a "base plan" (casts / lands / attacks / activations of the main phase); no other
post-dedup axis fires in this deck (the scry / dig / ponder / cleanup-discard axes are absent).

**4. The enumeration walk** (odometer positions -- CPU, not budget units: plans are deduplicated before
they are scored) is dominated by AURA HOST groups: every Aura in hand is a group with one option per
legal host (Colossification / Eldrazi Conscription / Mythic Proportions / Prodigious Growth / Arcanum
Wings, plus Wild Growth's land host); a hand with three Auras and four hosts walks 5^3 = 125 positions
that the shared host ranking (AuraPlanHostKey) then folds. The walk is 638M positions on the final build (800M at full width); the engine's own pricing of the
INTERCHANGEABLE-COPY FOLD (duplicate hand copies of one card -- two Colossifications -- and Aura hosts of one
class counted as C(s+k,k) instead of (1+s)^k) is **2.27x** of that walk. It is printed by the census as "priced,
not applied".

**Per axis the USER named, with a proposed cut:**

| source | where its cost shows | size | proposed cut | kind |
|---|---|---|---|---|
| mulligan-bottoming playouts | `probe` decisions of the turn census | **71.4% of all units** | (a) the pipeline's own answer: the exhaustive keep/bottom table makes the bottom a table read and the playouts disappear -- but it needs Bruna in the suite first (the 3x rule); (b) until then the per-deck `bottom_eval_depth 0` / `bottom_eval_topk 5` profile policy (the Melira precedent: stage-1 bottoming playouts greedy, the 5 cheapest-best re-rolled at real settings) -- measured below (PBEV arm) | heuristic: **USER's call** |
| Glittering Wish targets | tutor-target axis | full width -> rule: total units -22.4% (41.25M -> 32.00M); the axis 30.0% -> 12.9% of scored plans | done (this rule). Further: the Shusher / Linvala questions below (both WIDEN, measured) | heuristic (USER's spec) |
| Open the Armory targets | tutor-target axis | rule v1: total units a further -6.5% (32.00M -> 29.92M); the axis 12.9% -> 9.3% | done (this rule). Further narrowing ("you rarely need all of them"): see the Armory section | heuristic (USER's spec) |
| Aura host choice | the ENUMERATION WALK (CPU, not budget units): Aura groups with one option per host drive ~94% of the 638M odometer positions; AuraPlanHostKey already folds them before scoring | the dominant CPU cost of plan generation | **SOUND**: apply the engine's INTERCHANGEABLE-COPY FOLD (identical hand copies and same-class hosts as C(s+k,k), not (1+s)^k) -- priced by the census itself at **2.27x** of the walk; an identity fold, so it must be byte-identical (a dedup-equivalent walk) -- default ON once that is verified | sound (engine-wide; free to do, not done here) |
| Lightning Greaves equip targets | equip groups in the walk | never the largest group in any EnumeratePlans call (< 1% of the walk); equip {0} | none needed; duplicate Greaves copies are covered by the copy fold above | -- |
| Reborn Hope return targets | tutor axis (graveyard zone) | **0** committed resolutions in 6,725 searched + 10,000 d0 games (Auroral Procession: 4) | none: the Wish rule offers a regrowth only to restore the Wings cheat path | -- |
| Mother of Runes | her activation is NOT modelled (USER-approved deferral, inert in goldfish) | 0 branching beyond her cast | none | -- |
| Bruna's gather | the searched gather subset | proven dominance collapse (248/248 under the full-powerset control) | none | already sound |
| the Wings swap | the swap-in Aura | collapsed to the damage-max pick, proven against MTG_AURA_SWAP_BRANCH | none | already proven |
| mana payment | not a budget site; 1.70 payment solves per charged unit (real games), 1.30 in probes | CPU per node, not branching | none from the census: per-node cost (the profiling step), not a branch source | -- |
| d0 horizon leaf (greedy_fallback) | 26.3% of real-game units | the permitted greedy beyond the horizon | a SETTINGS question (depth matrix / search_leaf_depth), not a search hack | -- |
| breakpoint waves (la_bp_wave) | 6.2% of real-game units | -- | none proposed | -- |

## Open questions for the USER (none blocks anything; the default taken is stated)

1. **Vexing Shusher.** "Leave out Shusher" costs 34 turns in 6,725 searched games (38 better / 7 worse for
   putting him back; 4.6 se). Proposal: Shusher only in the need-a-carrier state, beside Linvala (+1 candidate
   in ~a third of wishes, +1% units). **Default taken: your rule (Shusher out).**
2. **Bruna over Linvala.** Neutral (11 / 8, -5). Recommendation: keep it. **Default: kept.**
3. **The three Armory exclusions** (Eldrazi Conscription, Mythic Proportions, Prodigious Growth): cost pending the final batch (D).
   **Default taken: excluded (your list).**
4. **Lightning Greaves.** v1 (an attacker must be reachable: Bruna in hand or via a Wish, a non-dork creature
   in hand / just cast) is shipped; the wider v2 (whenever none is owned and Bruna is not ready) is 14 turns
   better COUPLED but 4 turns worse once the search cannot pre-see Open the Armory's reshuffle (d5 only).
   **Default: v1.** A decoupled d3 check is the open item if you want Greaves wider.
5. **Mulligan-bottoming playouts are 71% of Bruna's units.** Options: (a) the keep/bottom table (needs Bruna in
   the suite first); (b) the Melira-precedent profile policy `bottom_eval_depth 0, bottom_eval_topk 5` --
   its measurement is pending (D). **Default: not adopted** (profile unchanged).
6. **Interchangeable-copy fold** (a SOUND identity fold the census prices at 2.27x of the enumeration walk;
   engine-wide, CPU not budget units). Proposal: implement it default-ON after a byte-identity check.
   **Default: not done here** (outside this deck's provider).
7. **Re-adding Bruna to the suite** is gated on the 3x cost rule. pending (D).

## History (rounds 1-5 of the Wish rule; logs under logs/wish/, gitignored)

Rounds 1-3 used the cells d5 b20 s10.1M x400, v5 s10.5M x400, d3 b10 s10.2M x200, 2HG d3 s10.6M x200,
d0 s10.3M x1000 and held-out f5 s11.5M x400 ("worse" = the full-width control won sooner, loss = 9):

| round | rule | d5 | v5 | f5 (held-out) | d3 | 2HG |
|---|---|---|---|---|---|---|
| 1 | spec roles only (no body) | 7 better / 4 worse | 3 / 10 | -- | 0 / 0 | 1 / 0 |
| 2 | + cheapest body (Shusher) | 6 / 1 | 2 / 5 | 2 / 6 | 0 / 0 | 1 / 0 |
| 3 | + hardest-hitting body (Linvala) | 6 / 2 | 2 / 3 | 1 / 3 | 1 / 1 | 1 / 0 |

* Round 1: 9 of the 14 control wins fetched Vexing Shusher (7) or Linvala (2) onto an empty or dork-only
  board -> the BODY role. Round 2: f5 gi264 / gi371 never recover even at d8 b0 -- Linvala's third point
  of damage is the kill (Bruna + Conscription + Linvala = 18 vs 17 life; Shusher's 2 power falls one
  short) -> the hardest-hitting body.
* Cuts measured and REJECTED in round 3 (temporary selectors, deleted): drop Troyan when a body is needed
  (d5 +10, d3 +3, 2HG +10 turns); drop the body when Troyan is offered (v5 +4); drop Almost Perfect when
  no body / no cheat path / Bruna offered (worse on all four cells); drop the cheap Aura when Troyan is
  offered (v5 +1, 2 new worse games).

Rounds 4-5 (2026-10-07) moved to Bruna's 21 removed suite rows as the cells. Every change was forced by a
game the full-width control won sooner and the rule did not recover even at d8 b0, or was ordered by the
USER:

| change | evidence |
|---|---|
| REGROWTH role (Procession returns Wings) | s5005 d5 + d3 gi342: Wings pitched at cleanup; control Wish -> Procession -> Wings -> swap |
| cheat path is COLOUR-aware | s4004 d3 gi904: Wings in hand read as a cheat path on a board with no blue source |
| an Aura-wearing dork is a body | s7007 d3 gi933: Mythic Proportions on Avacyn's Pilgrim read as "no body" |
| LETHAL NOW cheap Aura | gi933: Courage's +2 was the kill this turn |
| Vexing Shusher OUT; Linvala under the original condition | USER 2026-10-07 |
| Linvala loses to a next-turn Bruna | USER 2026-10-07 |
| Troyan only for real acceleration | USER 2026-10-07 |
| primary skipped on ENOUGH FOR LETHAL; Bruna skipped when lethal is on board; nothing missing -> one name | USER 2026-10-07 |
| a host-tapping Aura cast on a host is not next-turn lethal | s7007 d5 gi109 / gi432 / gi472, s4004 d3 gi762 |
| the cheap Aura may be worn by a mana dork | train s2002 d3 gi76: Courage on Avacyn's Pilgrim |
| Shusher never inherits the body role (this session) | round 5 still offered him once Linvala had left the sideboard (2 of 11,666 wishes) |

Measured and REJECTED in round 5: **Linvala only with Arcanum Wings in hand** (USER hypothesis): the rule beat
it 16 better / 8 worse on held-out (-8 turns) and it lost both f5 counterexamples (gi264 / gi371: rule and
control T5, Wings-only T6). Tried and REVERTED: "Linvala stays when she is castable THIS turn" -- read at
the wish's resolution it depends on which lands the (unsearched) payer tapped for the wish, and it did not
recover s4004 gi65 even at d8 b0 (see the Bruna-over-Linvala losses below, which are exactly that state).
