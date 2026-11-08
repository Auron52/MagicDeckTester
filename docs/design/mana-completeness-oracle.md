# Mana completeness oracle — "the search never misses an option due to mana"

**Status (2026-10-27): Stages 1 and 2 BUILT** (L1 + L1b below: APPLY-DROP, DOMINATED, PHANTOM,
ENUM-GAP, and the I1 self-check). Default OFF, play byte-identical with it on or off (verified on 16
decks). Stage 3 (LOST-FOLLOW-UP) and L4 not built. See "As built" and "First results" at the end.
Origin: USER, 2026-10-26:

> *"Okay, let's put serious focus into mana problems. What I would like to do is create an analyzer
> that plays games with mana tapping done in the current manner, but also in a relatively complete
> manner (how complete will be a discussion point, but ideally fully complete) and ensures they end
> up with the same list of plans. If they don't, we should flag it as a bug."*
>
> *"One thing we could consider is the table of possible useful mana combinations we can generate."*
>
> *"But, we also need to consider special mana such as that from Generator Servant."*
>
> *"Overall, though, what I want to make sure is that the search never misses an option due to
> mana, even if we largely use heuristics."*

## 1. The invariant

Tapping is a standing **heuristic exemption** in the search (CLAUDE.md, NO GREEDY: "the standing
attack / mana-payment exemptions"): the search branches over *plans* (unordered action sets) and the
payer decides, by ranking and holds, which sources pay for them. That is allowed. What is **not**
allowed is for that heuristic to make an option unreachable. Heuristics may pick *which* payment
realises an option; they may never remove the option.

Mana can remove an option in four places, all of which the oracle checks:

| class | where | what happened |
|---|---|---|
| **ENUM-GAP** | enumeration | a plan some legal order + payment realises is not in the menu (a mana gate rejected it) |
| **APPLY-DROP** | apply | the plan is in the menu, but the engine's payment drops one of its actions, although a legal order + payment realises all of them; the search scores the smaller plan believing it scored this one |
| **DOMINATED** | apply | the engine's realisation leaves strictly fewer resources than another legal realisation of the same plan (a dork tapped instead of a land and the attack lost, a Treasure cracked instead of a land, Generator Servant's haste-tagged {C} spent on a noncreature spell) |
| **LOST-FOLLOW-UP** | apply → next decision | the engine's realisation and another one are incomparable, and the other one enables a later option this turn (a main-2 cast from the known hand, an attacker, a breakpoint continuation) that the engine's does not |

**PHANTOM** (a menu plan no legal payment realises) is reported too, as a secondary class: it is not
a miss, but it is noise the search spends nodes on.

The oracle checks it as **two set inclusions** at every real decision, which together are
"the same list of plans":

* **I1 — current ⊆ complete (oracle sanity).** Every outcome the engine realises must appear in the
  complete set. A violation is an **ORACLE-GAP**: the oracle failed to reproduce the engine's own
  play, so its "complete" set is not complete and nothing it says at that node can be trusted. These
  are fixed in the oracle, never in the engine.
* **I2 — complete ⊆ current (the USER's invariant).** Every *useful* outcome in the complete set must
  be covered (equal or dominated) by an outcome the engine reaches. A violation is a **MISS**
  (one of the four classes above), fixed in the engine.

## 2. The complete side

### 2.1 The mana table ("useful mana combinations")

Per board, the oracle enumerates the **minimal payments** of a cost: every set of (source, face)
taps — plus units already floating — that pays the cost exactly, with no tap that could be removed.
Identical sources (same definition, state and counters) fold into counts, the same soundness
argument as the backtracker's sibling collapse. Over-taps are only generated where a source is
indivisible (Sol Ring's {C}{C} for a {1}).

**Mana is typed, not just coloured.** A unit is (colour ∈ WUBRGC) × (tag), where the tag carries the
mana's special properties:

| tag | source | why it is not interchangeable |
|---|---|---|
| haste | Generator Servant (`floating_haste_c`) | a creature spell it pays gains haste |
| creature-only | `creature_mana_only` (Somberwald Sage, Ancient Ziggurat), `colored_creature_only` faces (Cavern, Unclaimed Territory) | spendable only on creature spells |
| big-spell-only | Troyan (`mana_only_spell_min_mv`) | spendable only on a big spell |
| subtype-only | Giada, Smokebraider (`mana_only_subtype`) | spendable only on that subtype |
| snow | snow sources (`snow_units`) | the only units that pay {S} |

The sources and their faces come from **the engine's own legality function**,
`HumanPreTapOfferFor` (`src/ai/ManaPayment.cpp`), and every tap is performed through the engine's own
route for it (`ApplyHumanPreTap`: plain taps, fed conversions, pay-sac cracks, sac actions with the
haste tag, any-colour Aura choices). The oracle therefore has no second model of what a source does
that could drift from the payer's. Restricted sources, which the viewer deliberately does not offer
(they would launder restricted mana into the general pool), get an oracle-only route into their own
restricted pools.

### 2.2 The realiser (ordering × payment, layered)

For a plan, the realiser walks its actions in **every order**, and before each action chooses
**every minimal payment** from the mana table of the *current* board. A cast rock, a ritual's float,
a minted Treasure, a hasted dork or a cost reducer is therefore seen by the next step exactly as the
engine resolves it, because each step is applied by the engine itself:

1. **Capture the cost.** A probe apply of the step under a *float-only* payment scope with an empty
   float records the exact `ManaCost` the payer is asked for, including reducers, taxes, X, kicker
   and strive. There is no second cost model.
2. **Enumerate the minimal payments** of that cost on the current board (2.1).
3. **Apply.** For each payment: taps on a copy through the engine routes, then the step is applied
   by `TurnSolver::ApplyPlan` under the float-only scope, so the payer can spend only what the oracle
   tapped and cannot substitute a heuristic tap of its own.

States are deduplicated per layer (actions done, state) and **Pareto-pruned** on resources (below),
so the walk is 2^k layers wide rather than k! × payments^k.

**Resources** (what dominance compares; everything else must be identical): each permanent's
tapped flag, the typed floating pool, life, `temp_haste` on creatures, counters on mana sources
(depletion, storage, charge), and the presence of one-shot sources (Treasures, Lotus Bloom,
Generator Servant). Outcome A dominates B iff A has every resource B has.

### 2.3 Follow-ups (Stage 3)

For LOST-FOLLOW-UP the oracle takes, per menu plan, the frontier of its complete realisations and
asks of each one what is still possible **this turn from the cards already known**: attackers
(untapped, not sick or hasted), and which subsets of the remaining hand can still be cast (the
same mana table, on the residual board). A follow-up reachable from some realisation but not from
the engine's is a MISS naming the follow-up. Each flag is confirmed by running the realiser on it.

A follow-up that needs a card drawn **after** the payment is made (a cantrip's draw, a dig) is
**CLAIRVOYANT**: reported, but not a bug, because no player could have tapped for it.

## 3. Where it runs

* `MTG_MANA_ORACLE=1` (default OFF; `=2` prints every flag rather than the first few per class).
  Real play only (`g_real_resolution`, not inside a rollout), at every main-phase decision
  (`AIEngine::TakeTurn`), on **copies** of the state. Play is byte-identical with it on: the
  digests are checked.
* Output follows the house pattern (`MTG_MANA_CACHE_VERIFY`, `MTG_POOL_AUDIT`): one
  `[mana-oracle] <CLASS> seed=.. job=.. gi=.. T<turn> m<1|2> plan=<...> | <detail> | board=..`
  line per flag (capped per class at level 1), and an exit summary
  `=== MANA ORACLE: decisions=.. plans=.. realised=.. MISS{...} ORACLE-GAP=.. UNMODELED=.. ===`.
* `scripts/mana_oracle.py`: ONE pooled `--batch` across the suite decks (or a named deck), collects
  the flag lines, folds them by (deck, class, signature), and writes `logs/mana_oracle/<run>/report.md`
  with a ready-made single-game repro command per signature.

## 4. Levels of completeness

| level | covers | status |
|---|---|---|
| **L0** order-free affordability on one board | ENUM-GAP / PHANTOM for plans whose casts do not feed one another | subsumed by L1 |
| **L1** ordering × payment within the phase | APPLY-DROP, PHANTOM, DOMINATED, and I1 | Stage 1 |
| **L1b** ENUM-GAP | observe-mode mana gates: every per-subset mana rejection (`flat-pool`, `colour-exists`, `colour-exact`, `ritual-colour`, `ef-*`) records its label and lets the subset continue, so the "mana gates off" list keeps every NON-mana rule identical | Stage 2 |
| **L2** follow-ups this turn | LOST-FOLLOW-UP (main 2, attackers, continuations; known cards only) | Stage 3 |
| **L3** persistent resources | a Treasure / depletion or storage counter / life spent when a realisation with the same follow-ups spends none | Stage 3 |
| **L4** inside the search | the same checks on sampled rollout nodes, not only real decisions | later |

**What the oracle does not model, and says so.** At a node where a plan needs something the
realiser cannot drive, it reports **UNMODELED** with the reason rather than a verdict, so a clean
run is never read as more than it is. Initially that covers: activations with mana costs other
than SacForMana, alternative payments (convoke, improvise, delve), sac-outlet creature fodder
(Skirk Prospector), the engine's own untap choices (`EtbUntapLands`, ritual tap-ahead), storage-land
partial bursts, and Wirewood Lodge's untap burst.

## 5. Open questions for the USER (surfaced, not blocking)

1. **Incomparable trade-offs.** When main 1 can be paid from an Island or a Forest and only the
   Forest keeps a known main-2 card castable, the engine picking the Island is a LOST-FOLLOW-UP:
   the search never evaluates "P, then that card". The default is to flag it as a bug (the fix would
   be a searched payment variant for exactly those cases, not a better ranking).
2. **Harmless over-taps.** Tapping more than needed when nothing this turn could have used the
   difference is counted, not flagged. One-shot and counter resources (L3) are flagged.
3. **Scope.** Real decisions first (every decision of every game the batch plays). Rollout-node
   sampling (L4) after the real-decision classes are clean.

## 6. As built (2026-10-27)

**Run it:** `python3 scripts/mana_oracle.py [deck ...] [--games N] [--seed S] [--name RUN]` -- deck
names are folder slugs or the suite's short keys (`niv`, `edf`, `fungusv3`, ...); no deck = every
shipping deck. One pooled `--batch` with the oracle on, one with it off; the per-job play digests must
match (else exit 2: the oracle moved play). Report: `logs/mana_oracle/<RUN>/report.md` (per-deck table,
then per class the top signatures, each with a single-game repro command) + `flags.jsonl`. Exit 1 when
any MISS or ORACLE-GAP is found, 0 when clean.

**Engine flags** (all default off / inert): `MTG_MANA_ORACLE=1|2|3|4` (1 = summary + first 25 per class,
2 = every flag, 3 = + per-plan frontier trace, 4 = + the mana table and failed verifications);
`MTG_MANA_ORACLE_TURN=<t>` limits the level-3/4 trace to one turn; `MTG_MANA_ORACLE_NODES` (per-plan
apply cap, default 2000; a plan that hits it is realized once more under `MTG_MANA_ORACLE_NODES_RETRY`, default
20000, always on -- the env changes the number only, see 13.14); `MTG_MANA_ORACLE_GATE_MAX` (gate-rejected plans checked per decision, default
400); `MTG_MANA_ORACLE_MAX_PLANS` (menu cap, default all).

**Where the code is.** `src/ai/ManaOracle.{h,cpp}` (the oracle), plus four guarded hooks, each a
thread-local that only the oracle sets on its own state copy:
* `TapForCostShared` -> `ScriptedPay` (ManaPayment.cpp): the scripted payment, then float-only.
* `TapForCostSharedOnceImpl`: the float-only early exit (`t_float_only`).
* `BatchPrepayMainCasts` and `apply_one` (TurnSolver.cpp): the joint prepay stands down; the land-Aura
  host reservation stands down; the ETB-untap / untap-ritual TAP-AHEAD becomes a scripted, searched
  step (`ScriptedTapAhead`) instead of the engine's heuristic.
* `EnumeratePlans` (TurnSolver.cpp): observe mode (`t_gate_observe`) -- the four labelled mana
  rejections, the odometer's mana bound and the selection gate index let everything through.
* `g_tap_ahead_calls` (SpellEffects.h): a diagnostic counter, so an engine tap-ahead the oracle did not
  drive (the blink loop's) is reported UNMODELED instead of misread.
* `g_untap_choice_calls` (SpellEffects.h, `NoteUntapChoice`): the same for an untap effect that CHOSE
  among different tapped lands (`EtbUntapLands` -- Cloud of Faeries; `UntapManaSources` -- Reality
  Spasm; `RitualUntapSources`). Which ones come back is a yield-order heuristic the oracle does not
  branch on, so such an outcome is UNMODELED (`untap effect chose among different lands`).

**Things the build taught, each now pinned by `test/unit/test_mana_oracle.cpp` or a classification rule:**
1. *Minimality is unsound for converters.* Paying {U} with "Island feeds Cascade Bluffs -> {U}{U}" is not
   minimal, but once the Island is tapped Bluffs has no feeder, so it is not "the same as tapping Bluffs
   later". Every paying set is also extended with FED conversions.
2. *Optimistic arithmetic must not stop the search.* A set the arithmetic said covers (a Utopia Sprawl
   Forest's yield credited to its bare twins) is paid for real; if that fails the search keeps extending.
   A source's class now includes what is attached to it.
3. *The engine reserves by marking sources tapped* (the land-Aura host during its own payment), so a
   frontier snapshot can look poorer than the board is. Those holds stand down on the oracle's copy.
4. *Options are outcomes, not action lists.* Ghalta, Stampede Tyrant cast alone puts the Hulk and the
   Craterhoof a gate-rejected "cast all three" plan names; ENUM-GAP therefore requires the payable
   outcome to be a board NO menu entry reaches WITH AT LEAST ITS RESOURCES. The same board with less of
   something and more of nothing is a dominance, and takes the DOMINATED-* class of what differs (an
   extra untapped land stays DOMINATED-LAND); every other order of the plan is still tried.
5. *A drop is a miss only if a menu sibling with the same actions reaches the same boards.* An order
   variant exists because order can matter, so "a sibling pays in full" is not enough: the dropping
   order is paid for real, and DROP-COVERED needs every board it reaches to be a sibling's board with at
   least the resources (or the order to be unpayable outright). Otherwise it is APPLY-DROP. (The first
   version stopped at "a sibling pays in full" -- 193 DROP-COVERED on 16 decks, 14 of them real misses.)
6. *A plan cut at a draw is continued by a solver from the board the payments left*, so two payments can
   lead to two different lines; only realisations of the SAME line (cards used, spells cast, library
   depth) are compared (else CONTINUATION-DIFFERS).
7. *Faces fold to the useful colours* -- the colours any card the player owns can spend, plus one
   representative and {C} (painless on a painland, energy-free on Aether Hub). This is the USER's
   "table of possible useful mana combinations", and it is what keeps the Aura-heavy EDF boards tractable.
8. *Boards are compared by identity, not ids.* The engine's state key mixes every permanent's
   `m_number` and the zone order (it replays index-encoded plans), so Shard Volley sacrificing Mountain
   #21 instead of the identical #22, or two creatures cast in the other order, read as two boards. End
   boards are compared on a canonical copy (zones sorted, permanents renumbered, attachment pointers
   remapped); the mid-plan Pareto prune keeps the exact key, because a later action can name a copy.
9. *A NO verdict needs the whole space.* PHANTOM ("no order and no payment realises it") is given only
   when every order was tried -- casts AND activations, up to five movable actions; past that it is
   UNMODELED (`too many actions to try every order`) -- and when no order's realisation was capped,
   unmodelled or touched by an engine choice the oracle does not drive.

10. *A game's cards carry no cost.* A `Card` in a game's hand / library / graveyard holds its name and number;
   the cost lives on the definition. The useful-colour table (item 7) read `Card::m_mana_cost`, so in a real
   game it held only the bill's own colours and folded away every colour a LATER card needs -- e.g. the {U}
   a Fertile Ground bonus floats while paying Living Wish, which the continuation's Training Grounds then
   spends painlessly (EDF seed 3002 T3, ORACLE-GAP "life engine 20 oracle 19"). Fixed 2026-10-27 (costs
   read from the definitions; pinned by "a colour only a later card in hand can spend is still offered").
   EDF census games: ORACLE-GAP 31 -> 15, PHANTOM 2291 -> 2200, CONTINUATION-DIFFERS 540 -> 463, ENUM-GAP
   107 -> 111; the 38-deck sample barely moves (DOMINATED-LAND +2) at +70% oracle time and CAPPED 248 -> 314.
   The 15 left: Living Wish continuations one life apart and Conservatory's {4} investigate -- items 11-15.

11. *A copy the plan itself uses is not its twins' equal.* Identical untapped permanents fold into one source
   class and the lowest-numbered copy taps first. When the plan's own activation needs one of them ({T} or a
   sacrifice), tapping that copy for mana strands the activation. EDF seed 3508 T4: Wild Growth, then
   Conservatory #13's {4},{T}: investigate; the fold tapped #13 for the Wild Growth and missed the painless line
   the engine pays with #14. BuildSources now keys a plan source apart (`t_plan_srcs`, set by Realize).
12. *Which leftover floats is a choice.* A tap set that makes more than the bill leaves units in the pool, and the
   float-only payer spends a generic pip in a fixed colour order, so the oracle always kept the same unit --
   never the {U} a Trace of Abundance bonus could float for a Training Grounds two payments later (EDF seed 3353
   T8; the engine floats its bonus as wild). When a payment has a multi-unit face, Verify records the same taps
   once per OTHER leftover of the same size (a set-aside may hold units the taps just made: ScriptedPay
   subtracts it after the taps). Plain units only: a wild or haste-tagged {C} in the pool keeps the payer's own.
13. *One tap more can be the line.* A minimal payment beside a multi-unit source (an Aura-bonus land) leaves that
   source's surplus floating; tapping ONE more one-unit source beside it can float a unit a later payment needs
   (EDF seed 3101). ExtendSurplus tries each one-unit face once more past every minimal multi-unit payment. Cost:
   EDF seed 3353's oracle time 3.9 s -> 90 s (the leftover enumeration and the extra tap multiply), which is why
   both run only beside a multi-unit face.
14. *An Aura's host is part of the line.* A continuation that put a land Aura (Wild Growth) on a different land
   reached a board the engine never had, and compared as a payment difference. LineKey now names every Aura new
   to the battlefield with its host (`Aura>Host`, `*` = a host that entered this turn), so such a realisation is
   CONTINUATION-DIFFERS. DiffText names an Aura/Equipment host difference, a hand of the same size with
   different cards, and a sideboard-size difference (Living Wish).
15. *A same-line realisation is not the oracle's best payment.* With items 12-13 the oracle reaches more boards,
   and a wider payment (a land more, a life more) can happen to reach the engine's line while the painless ones
   the engine matches float a unit that sends the continuation solver elsewhere (EDF seed 3946 T4: Conservatory,
   Eldrazi Displacer, then the engine's continuation casts Training Grounds). Judged against that one outcome,
   the plan read as an ORACLE-GAP. An uncovered plan with a different-line realisation at least as healthy as
   the engine's is CONTINUATION-DIFFERS: the open question is the continuation, not the mana.

   EDF census (the 144 games, same play): ORACLE-GAP 15 -> 4 (all seed 3946, item 15 -> 0), APPLY-DROP 689 ->
   757 (68 lost lines the old oracle could not pay), APPLY-DROP-ORDER 108 -> 71, PHANTOM 2274 -> 1929,
   CONTINUATION-DIFFERS 463 -> 338, DOMINATED-LAND 2077 -> 1921, DOMINATED-LIFE 5678 -> 5549. The price: CAPPED
   4344 -> 6513 and oracle time 8.6 M -> 14.6 M ms (+69%). The 8 gap games: 0 ORACLE-GAP each; seed 3779 (the
   Displacer blink loop) is CAPPED 544 times, so that game is unresolved rather than clean. With item 15,
   seed 3946's four are CONTINUATION-DIFFERS.

   38 decks x 6 games (`logs/mana_oracle/census3`, on d369f89d8; play identity with the oracle off:
   byte-identical): ORACLE-GAP 195 -> 38 (Melira Pod 28 = Birthing Pod's Phyrexian mana, Anti-Lifegain 8 =
   Grove, Auras 2 = Horizon Canopy -- the known unmodelled sources), CONTINUATION-DIFFERS 191 -> 433, CAPPED
   314 -> 4,378, oracle time 590 -> 870 s. Most of the CAPPED rise is Melira Pod (39 -> 3,954) and Hinata
   (3 -> 112), whose games changed between the runs (new play budgets: 13,147 menu plans against 5,147); on
   the decks that played the same games it is EDF 122 -> 136, Fungus 0 -> 12, SelesnyaLifegain 14 -> 23,
   AccomplishedAlchemist 21 -> 24.

16. *An enchanted land is not its bare twin's equal in the frontier prune* (2026-10-28). The Pareto prune between
   frontier boards counts untapped permanents by `ClassKey`, which reads the permanent alone -- so "Aether Hub #3
   untapped" (Wild Growth and the plan's new Fertile Ground on it: three mana) and "Aether Hub #4 untapped" (bare,
   no energy: one) were the same resource. EDF seed 3003 T5, the plan Fertile Ground onto Hub #3, Trace of
   Abundance onto Adarkar Wastes, then Eldrazi Displacer's {2}{C} blink: the engine pays Fertile Ground off
   Brushland {G} + Hub #4, Trace off Hub #3 (its {C}, Wild Growth's {G}, Fertile Ground's {W}), and the blink off
   the floating {C} plus the Wastes with Trace's bonus -- seven mana from four lands. The board after paying
   Fertile Ground off Hub #3 itself (a Brushland more untapped, a life up) "covered" the engine's, so the prune
   discarded the one frontier the engine's line continues from, and every line the oracle kept was a mana short
   of the blink: nine ORACLE-GAPs on one board, the last unexplained gaps of the EDF census. `Resources` now adds
   what is attached to each permanent (`AttachedKey`: Auras and Equipment, sorted), as `BuildSources` already did
   for the payment table; seed 3003: ORACLE-GAP 9 -> 0. The first-payment options were complete all along -- the
   "never offers Brushland {W} + Hub #4's energy {G}" reading was wrong (no energy was left; both Hubs make {C}
   only). Pinned by `manaoracle::CoversEngineOutcome`, a test hook that runs AuditPlan's I1 check for one plan.
   The oracle's plan and board text now print each Aura's host (`Fertile Ground@3`).

## 7. First results (16 decks x 6 games, seed 424200, 798 real decisions, 10,469 menu plans)

**I1 holds: ORACLE-GAP = 0** -- wherever the oracle gives a verdict it reproduces the engine's own
outcome. Play digests identical with the oracle on and off.

| class | count | what it is |
|---|---|---|
| DOMINATED-HASTE | 4 (NivMizzetCombo, 1 position) | {Niv-Mizzet the Firemind, crack Generator Servant, Preordain}: the engine's realisation leaves Niv without haste; spending one tagged {C} on Niv and feeding the other into an Izzet Signet ends on the same board WITH haste (so `{T}: draw` the same turn). The card's own data note calls this residue "not a dominance"; here it is one. |
| APPLY-DROP | 2 (NivMizzetCombo T3) | {Izzet Signet, Generator Servant} off two Islands + a Signet: the engine drops the Servant; Island->old Signet pays the new Signet, Island->new Signet pays the Servant. The `MTG_RAMP_FEED_ROUTE` shape with a Signet instead of Ferrous Lake. |
| ENUM-GAP | ~70 (AccomplishedAlchemist, EDF) | Alchemist: lifegain first (Feed the Clan, Nourish), then Accomplished Alchemist taps for the life gained, funding a bigger Stroke of Genius X -- the flat gate credits Alchemist as one mana, so e.g. Nourish + Stroke X=3 is never on the menu. EDF: Cloud of Faeries first (untap two lands), then Trace of Abundance off them. |
| DOMINATED-CREATURE | 4 (FiveColour, Fungus) | FiveColour T5: Maelstrom Archangel paid with Birds + three lands + Faeburrow Elder (ending with {W}{U}{B}{R} floating) where Faeburrow alone pays. Fungus: a 1/1 Saproling tapped where 0/0s pay. |
| DOMINATED-LIFE | 191 (EDF, Fungus) | EDF: painland coloured faces where a {C} face or a painless source pays (real, low impact in goldfish). Fungus's 56 are NOT payment misses: they are the Ancient Tomb pain bug (section 7.1). |
| DOMINATED-OTHER | 22 (EDF) | Aether Hub energy spent where a {C} tap pays. |
| PHANTOM | 135 | menu entries no order or payment realises (T1 Forest + Sol Ring + Priest of Titania: the joint bill is order-blind). Wasted nodes, not misses -- see section 7.1 for what that claim rests on. |
| DOMINATED-LAND | 595 | needless land taps -- a miss only if something this turn could have used the land (Stage 3). |

**Not yet driven (reported UNMODELED / CAPPED, never as clean):** EDF's blink-loop tap-ahead (518 plans,
the bulk), combo-route macros and loop-count variants (AccomplishedAlchemist's Galvanic untap loops),
floating pools too large to enumerate set-asides (rituals, ~940 frontiers), Smokebraider's
subtype-restricted mana, the engine's choice of WHICH lands an untap effect untaps, and per-plan caps on
EDF / Snow / AccomplishedAlchemist.

### 7.1 Are the quiet classes really quiet? (2026-10-27, USER: "How can you tell that it is noise?")

"Noise" means the entry costs the search a node but hides no option. Each quiet class has to EARN that,
and the first version did not:

* **DROP-COVERED** only checked that another menu entry with the same actions paid in full, then
  returned before paying the dropping order at all. It never compared boards. Fixed (section 6, item 5):
  14 of the 193 became APPLY-DROP. Fungus 8: the continuation after a Saproling-token payment casts
  Beastmaster Ascension and Saproling Migration the engine's own line dropped. AccomplishedAlchemist 3,
  Hinata 2: Gamble / Sol Ring. EDF 1: a different Living Wish target, an incomparable board.
* **PHANTOM** rested on "every order and payment was tried", which was not true: activations were
  never reordered, a plan with more than five casts got no other orders at all, the other orders' own
  caps were ignored, and an untap effect's choice of lands was not branched (section 6, item 9 and the
  `g_untap_choice_calls` hook). Fixed: EDF 30 -> 21 (the rest are now UNMODELED), Dragonstorm 4 -> 3.
  A hand check of the samples holds up: Hinata T2 Sol Ring + Ponder off one Mystic Monastery (Sol
  Ring's {C}{C} cannot pay {U}); Snow's Scrying Sheets {1}{S} with one untapped snow source; EDF Aether
  Hub land + two Cloud of Faeries (no energy left for the second {U} -- and the same plan with Yavimaya
  Coast as the land IS payable, which the oracle reports as ENUM-GAP). What a PHANTOM still rests on:
  the oracle's mana table being complete for that board. ORACLE-GAP = 0 tests that table only where the
  engine succeeded, so it is evidence, not proof, for the boards where it failed.
* **ENUM-GAP** dismissed a gate-rejected plan whenever a menu entry reached the same board, without
  comparing resources. Fixed (section 6, item 4).
* **Board identity** compared ids (section 6, item 8): Burn's two "APPLY-DROP"s were Shard Volley
  sacrificing a different identical Mountain.

**An engine rules bug the oracle surfaced (Ancient Tomb).** On an UNARMED board (every deck but Prevent
Damage) `TapSourceIntoFloat` treats a {C} tap of a land that has a {C} mode as a painland's painless
ability and never reads `tap_self_damage_any_mode`, so the payment (`TapForCostShared`) and the pre-tap
tap Ancient Tomb for free. A real Fungus game (`--seed 424200`, game 2 of 6) taps the Tomb for Shroofus Sproutsire and
stays at 20 life; the search's plan application does charge it. That split is the whole of Fungus's
DOMINATED-LIFE (56, every one "+2 life by paying with Ancient Tomb:C"). FIXED 2026-10-27 (section 12): the
unarmed branch uses `dmgev::PainForTap` too, and the pin in `test/unit/test_mana_oracle.cpp` is a plain test.

**38 decks (2026-10-27, `logs/mana_oracle/dev8`).** The 22 decks never run before add ORACLE-GAP 195 --
the oracle is not yet complete there: Melira Pod 185 (Birthing Pod's Phyrexian {G/P}, mana vs life),
Auras 2 (Horizon Canopy's pay-1-life tap; the engine line ends 1 life higher -- possibly another
tap-path pain split), Anti-Lifegain 8 (Grove of the Burnwillows' opponent lifegain). No quiet verdict on
those decks is trustworthy until their gaps are closed.

## 8. Next steps

0. ~~Fix the Ancient Tomb pain split~~ (done, section 12). Close the new decks' ORACLE-GAPs (Phyrexian
   mana, Horizon Canopy, Grove), then rerun all 38.
1. **Stage 3, LOST-FOLLOW-UP** -- needs the USER's call on question 1 (section 5) before it is worth
   building, because it decides whether incomparable trade-offs (DOMINATED-LAND's real cases) are bugs.
2. Drive the blink-loop tap-ahead and untap-target choices (the EDF UNMODELED bulk; untap choices alone
   are 481 UNMODELED frontiers on dev8).
3. Engine fixes for the confirmed misses, each through the normal A/B -- the oracle locates them; it does
   not decide them.
4. L4: sample rollout nodes, not only real decisions.

## 9. The rollback census (2026-10-27)

USER: *"we would like to know how many lines are fully inaccessible and which are accessible only with
rollbacks to the previous line (i.e. keeping up blue instead of white so that we can cast a blue spell only
after we discover that blue is needed)."* Every APPLY-DROP (a menu plan the engine's own payment drops, that
some payment of the same order realises in full) gets a DEPTH:

1. **Replay the engine's own line through the hook** with every mana decision an ENGINE STEP
   (`PayOption::engine`, `Hook::all_engine`): the hook stands down and the engine's own payment, joint
   prepay, land-Aura host hold or tap-ahead runs, counted as one step. The replay must end exactly where
   the unhooked apply ends (board, resources with float, drops); otherwise the verdict is `unfaithful`.
2. `f` = the first engine PAYMENT step that failed. For `k = f, f-1, ..., 0`: keep the engine's first `k`
   decisions and let the oracle re-make the rest. The largest `k` that pays every action (and uses the cards
   the line meant to use) gives **depth = f - k**: 0 = the failing payment itself had a way (a payer miss);
   1 = the decision before it has to change too (keep blue up instead of white); and so on.
3. Also recorded: whether the lost action was DECLARED in the plan (else it is a continuation's, chosen after
   a draw -- `need-later`, which no earlier payment could plan for), and whether `MTG_PAY_ROLLBACK` forced on
   recovers it.

Buckets: `depth0/1/2/3+`, `capped`, `unfaithful`, `no-failed-payment`, `unmodeled` (every paying line
rests on an engine mana choice the oracle does not drive -- the blink loop's tap-ahead, an untap effect's
choice of lands -- so no depth can be vouched for), `inconsistent`; plus `reorder`
(APPLY-DROP-ORDER: only another order pays) and `off-menu` (ENUM-GAP: a mana gate kept it off the menu),
counted alongside so the table answers "how many lost lines, of which kind". Exit line `=== MANA ORACLE
rollback census: ... ===`; `scripts/mana_oracle.py` adds a "Rollback census" table per deck.

**census1 (38 decks x 6 games, `logs/mana_oracle/census1/`):** 16 APPLY-DROP -- depth1 7
(AccomplishedAlchemist 3, NivMizzetCombo 2, EDF 1, Hinata 1), depth2 7 (Fungus 6, Hinata 1), depth3+ 2
(Fungus), depth0 0; none unfaithful, capped or inconsistent. `MTG_PAY_ROLLBACK` forced on rescues **0 of
16** (it declines with mana floating and on multi-mana sources). 13 of the 16 lost a continuation's cast
(need-later); 3 lost a declared one (Niv 2, Hinata 1). Off the menu: 82 ENUM-GAP (Alchemist 44, EDF 15,
Slivers 11, Niv 6, Mirrorwing 3, Selesnya 2, Landfall 1).

**EDF, viewer seeds 3000-4000 (the deck owner's manual-tap report).** 134 of 1,001 games hold an
APPLY-DROP (689 flags; T3 311, T4 326): Eladamri's Call 172, Living Wish 136, Eldrazi Displacer 129,
Training Grounds 73, Emiel 68, Trace of Abundance 63. The census over those 144 games (`edf_s<seed>` jobs):
depth0 46, **depth1 512**, depth2 45, depth3+ 27, 59 resting on an engine choice the oracle does not drive
(reported `inconsistent` by the first build, `unmodeled` since); 110 more pay only in another order
(reorder), 107 are off the menu (ENUM-GAP). `MTG_PAY_ROLLBACK` forced on rescues **0 of 689**. 559 lost a
cast the plan declared, 130 a continuation's. So on EDF the typical lost line is exactly the USER's example:
one earlier payment spent the colour a later cast needed. ORACLE-GAP 31 in the same games, NOT yet diagnosed: e.g.
seed 3002 T3 "cast Living Wish", whose continuation plays the fetched Adarkar Wastes and casts Training
Grounds -- the engine's line ends at 20 life with the new Wastes untapped (its Wish payment tapped
Conservatory + the Fertile Ground host, floating the bonus for the {U}), while every same-line oracle
outcome pays the {U} with the new Wastes' painful face (19 life). An oracle defect to fix (I1), owed.

**What causes EDF's (viewer seed 3000 T4):** Training Grounds {U} then
Eldrazi Displacer {2}{W} on Conservatory, Mariposa Military Base + Fertile Ground, Shivan Gorge. Depth 1:
the Grounds' payment has to change. (a) The greedy cannot pay {U} (its only blue is Fertile Ground's bonus),
and the backtracker's DFS walks battlefield order and accepts the FIRST covering set, so it taps
Conservatory (useless for {U}) and Mariposa, spending the only {W}. Pinned by the `should_fail` test
"payment: {U} off a Fertile Ground host taps the host alone". (b) `BatchPrepayMainCasts` declines ("wild ->
pip pinning ambiguous") whenever an any-colour land Aura is live, so EDF's multi-cast plans are paid one
cast at a time and never jointly. Both fixes change play: USER decision.

**Not covered:** a rollback reaching an EARLIER decision's payments (this turn's previous main-phase
commit). The census keeps or re-makes decisions within one plan's application only.

## 10. The viewer's line check -- "can the search find this line" (2026-10-27)

USER: *"a user using the viewer should be able to tell: 'Can the search actually find this line' while
using it"*, and *"show when it is possible to roll back the previous taps"*. The human's commit pays in
the human's order with the human's taps; the search scores a plan as the ENGINE pays it. So a line can play
in the viewer and still be one the search never reaches.

* `manaoracle::AuditLine(state, pre, plan, menu)` (ManaOracle.h): the engine's own apply of the plan;
  if it drops something, a menu entry with the same actions that does not; if none, the oracle in this
  order (stop at the first full payment) with the census (keep / failed / depth / `MTG_PAY_ROLLBACK`), then
  the other orders. Runs whether or not `MTG_MANA_ORACLE` is on, quietly (no logger, no chooser), with a
  400-apply cap per payment search so a validation stays interactive. A line the engine pays costs one
  apply.
* `WriteValidation` (src/main.cpp) emits `search_lines` (one entry per plan the verdict can commit; the
  oracle proper is asked about the first three dropping ones) and `order_audit` (a line rejected because
  its queued ORDER cannot pay: can any taps pay that order, the human's own taps left out?).
* The viewer: an accepted line the search cannot pay gets a "⚠ search can't pay" history badge and a note
  (what the engine's tapping drops, the taps that pay it payment by payment, how far a rollback has to
  reach); the reject panel for an unpayable order shows the taps that would pay it, or that the queued
  taps are the problem, or that no tapping pays it; the post-commit "not paid" message no longer claims
  the board lacks the mana.
* MANA ONLY: whether the search's own enumeration offers the line (the viewer's menu is the unpruned human
  menu) is not asked.
* **Queue these taps** (the reject panel, when the queued order is payable in that order): each payment
  carries the human-sequence step it was made in (`pays[].step`: the v2 walk writes `manaoracle::t_seq_step`
  for the step it dispatches, the hook records it per payment), so the viewer puts each payment's
  `tap=<name>#<num>:<face>[+aura]` tokens right before that step, replacing the segment's own manual taps
  (the check chose every tap). Nothing is committed: the human clicks Commit and the line validates as usual.
  Not an auto-pick, deliberately -- the manual-tap dialog exists because the engine choosing for the human was
  the bug. Worked case (EDF `claude_s1_gi0` T3): "Mariposa, Living Wish, Trace of Abundance" is rejected
  (auto-tap drops Trace); the check pays it with Aether Hub {C} (+ its Wild Growths) before Living Wish and
  Conservatory {W} before Trace, and the line with those taps queued validates.
* **On the USER's own games** (`node test/viewer_validate_check.js`, every reference, 2026-10-27): every
  accepted line the check could audit is one the search's own payment casts -- 2,795 yes, 0 no, 307 not
  audited (the sample looked at: passes, which commit no plan and are no longer counted).

## 11. What the lost lines cost -- the upper bound (2026-10-27)

The census counts lost OPTIONS. Whether they cost win turns was measured two ways, both measurement-only levers
built in a scratch worktree (`.claude/worktrees/smarttap`, not on the branch):

* **`MTG_PAY_SHRINK`** -- the per-cast payer gives back what its backtracker tapped but the bill did not need
  (the whole-turn prepay's `MTG_PREPAY_SHRINK` pass for one cast; subset-only, never a swap). Fixes the EDF
  seed 3000 T4 over-tap (`should_fail` pin in test_mana_oracle.cpp passes with it). On the 144 EDF census
  games it recovers **56 of 689** lost lines (depth1 512 -> 455; games with a lost line 134 -> 125). Smoke and
  regression: no searched-depth win turn moves (16 searched configs change play at equal score); d0 15
  slower / 4 faster. Held-out (seed 900001; 3,000 d0 + 120 d3 games per deck per arm, the 9 decks that
  moved): d3 identical everywhere; d0 EDF -0.0007, niv -0.0006, PD -0.0010, Snow -0.0010, TH +0.0017, rest
  identical. Sound, neutral, NOT strictly better -> USER decision. ADOPTED 2026-10-27 (section 12).
* **`MTG_ORACLE_PAY`** -- the UPPER BOUND: every top-level full plan apply whose own payment drops a cast takes
  the oracle's full payment of the same plan in the same order (executor and search share the apply). Seed
  910001, 200 d3 b10 + 1,000 d0 games per deck per arm: **EDF 3.980 -> 3.980**, Fungus 4.590 -> 4.590,
  AccomplishedAlchemist 5.000 -> 5.000, Hinata 5.525 -> 5.520, NivMizzetCombo 4.665 -> 4.655; d0 identical on
  all five (EDF's play digest moves, its win turns do not). Not covered by the bound: breakpoint-captured
  rollout applies (they keep the engine's payment) and ENUM-GAP lines (off the menu).

So a perfect payer for menu plans buys at most ~0.01 turn on these decks: when a line is lost to mana the
search nearly always has an equally fast alternative. The mana misses matter where the HUMAN's line is the
point -- the viewer, which is what section 10 serves ("Queue these taps") -- not in the search's win turns.
The remaining levers to look at for search are the ENUM-GAP gates (AccomplishedAlchemist's 44 off-menu
lifegain-then-Alchemist lines are the largest class) and the CONTINUATION classes.

## 12. Payer fixes from the census (2026-10-27)

* **Painless {C} under a land Aura (`MTG_PAINLESS_AURA_PIP`, default ON).** The USER's EDF viewer sweep (seeds
  3000-4000) flagged DOMINATED-LIFE in 724 of 1,001 games: a painland under its own land Aura paid a COLOURED
  pip with its painful face while the Aura's bonus already covered that pip and the painless {C} face would
  have paid the generic (Brushland + Wild Growth for Living Wish {1}{G}: {G} + 1 damage, where {C} + the
  Aura's {G} pays it for nothing). `PainlessAuraPipColor` (ManaPayment.cpp, the greedy's coloured tap) takes
  the {C} face when the Aura's fixed-colour units cover every pip of that colour still owed and a generic pip
  is left to spend the {C} on. Only EldraziDisplacerFlicker runs a painland under a land Aura. Held-out A/B
  (seed 920001; d0 2,000, d3 600, d5 150, 2HG d3 200 games per arm): 0 games slower, 0 faster, 0 play
  digests changed; units d3 4,137,488 -> 4,116,395, d5 1,211,858 -> 1,210,148, 2HG 1,413,848 -> 1,411,993.
  Strictly better, adopted.
* **Ancient Tomb's pain on an unarmed board** (section 7.1): `TapSourceIntoFloat`'s unarmed branch now reads
  `dmgev::PainForTap`, the armed branch's predicate, so the payment and the pre-tap charge the Tomb's 2 like
  the search's plan application always did. Rules fix (life only; Fungus is the only unarmed deck with the
  Tomb). The viewer's painland tap-ahead (SpellEffects.h, `pain_c`) no longer treats the Tomb's {C}{C} as a
  painless {C} either.
* **The per-cast give-back (`MTG_PAY_SHRINK`, default ON; section 11).** The USER, told it fixes the backtracker
  over-tap (a one-mana spell tapping two lands, the second spell of the line left unpayable): "that sounds like an
  actual issue". Held-out at every deck's SHIPPED play setting (seed 930001; 38 suite decks, 600 games per deck
  per arm, AccomplishedAlchemist 427): 22,627 paired games, 47 play digests change in five decks (EDF 19, Snow
  10, NivMizzetCombo 11, PD 6, TH 1) and only NivMizzetCombo's win turns move: 3 slower / 2 faster (+1 turn over
  600 games). All five reach the same turn in BOTH arms at 4x and 16x the budget (b80, b320): budget churn.
  Work: 303,711,599 -> 303,582,423 units (-0.04%; NivMizzetCombo -0.8%, Snow +0.1%). The pin "payment: {U} off a
  Fertile Ground host taps the host alone" is a plain test; the viewer line check's EDF seed 3000 T4 case now
  runs with the give-back off, and a new case shows the search's own payment casts that line.

**What the payer fixes bought on the EDF census** (the 144 games, `build/scratch/manaoracle/edfcensus4`, on
55151e2e4 -- the painless {C}, the Tomb, the give-back and the item 11-15 oracle -- against the same oracle on
the old payer): APPLY-DROP 757 -> 697 (rollback depth 1: 569 -> 507), games with a lost line 141 -> 133 (seed
3779 aside), DOMINATED-LIFE 5,549 -> 4,611. The rest of the lost lines are mostly the whole-turn prepay
declining whenever its tap set makes any-colour mana (`BatchPrepayMainCasts`, "wild -> pip pinning
ambiguous": every land-Aura bonus does), which hands the line to the per-cast payer.

* **The whole-turn prepay settles its colours (`MTG_PREPAY_COLOURS`, default ON) and holds a land Aura's host
  (`MTG_PREPAY_AURA_HOST`, default ON).** The USER: "We want the mana payment to be as close to fully reliable
  as possible." `MTG_PREPAY_PROBE` on EDF (40 games, seed 940001): of ~252k multi-cast prepay calls only 10,117
  (4%) prepaid; 13,915 declined on any-colour mana (PP_WILD) and 11,592 on a non-empty float, and those lines
  went to the per-cast payer, which drops casts the board pays (seed 3040 T3, Fertile Ground + Living Wish).
  Two causes, one lever: a land Aura's "any colour" bonus is booked as `wild`, and the joint bill was summed
  from the flat colour ints, so a hybrid pip entered as its FIRST colour (Trace of Abundance's {R/W} read
  {R}). `PrepayColoursFit` (TurnSolver.cpp) carries the hybrid pairs into the bill and settles every hybrid
  pip and every plain any-colour unit to a colour the pool covers; a `wild` with a non-"any of five" subset
  (wild_c / wild_phantom / wild_conv) still declines. Same probe after: 29,286 prepaid (x2.9), PP_WILD and
  the float decline 0. The first held-out A/B (seed 950001, 21,001 paired games at play settings) found one
  slower game, EDF gi188 T3 -> T4 at EVERY budget (b20..unlimited): the joint solve now paid {Fertile Ground
  onto Conservatory, Eladamri's Call} and tapped Conservatory, the new Aura's own host, before the Aura
  resolved -- its bonus gone, so the tutored Cloud of Faeries had no {U}. The per-cast payer and the rollout
  already hold that host (AIEngine's RESERVE THE AURA'S OWN HOST); `PrepayAuraHostHold` is the prepay's twin:
  every untapped host the batch's land Auras name sits out the joint solve, and a batch the board cannot pay
  without it declines to the per-cast payer (which pays the Aura first and then has host + bonus). A host is
  held only when the rest of the board can pay its Aura's own cost: otherwise every realisation taps it
  before the Aura resolves, and holding it only trades the joint solve for the per-cast walk (EDF gi 1011 T2,
  `test_edf_aura_payer_as_executed`).
  EDF census (edfcensus5, the colours lever alone, vs census4): APPLY-DROP 697 -> 203, rollback depth 1 507 ->
  68, games with a lost line 128 -> 70, DOMINATED-LIFE 4,611 -> 3,768.
  ADOPTED (both default ON) on a fresh held-out A/B at every deck's SHIPPED play setting (seed 960001, all 38
  suite decks, both levers off vs on): 21,001 paired games, **0 slower, 2 faster** (both EDF), 56 play digests
  changed in seven decks (EDF 28, FungusV3 17, Bruna 3, Hinata 3, FiveColour 2, Melira 2, AccomplishedAlchemist
  1); units 229,297,747 -> 229,121,049 (-0.08%; EDF -3.6% -- the colours lever alone had cost EDF +5.9%, which
  the host hold took back). The viewer fixture `edf_wish_first_keeps_trace` (the USER's "it skips to T4 after
  Living Wish") now pins the historical payer: on the new one the USER's own Wish-then-Trace order pays both
  casts, which `edf_wish_first_pays_trace_jointly` pins.
  The census on the adopted binary (edfcensus6; the 142 games all three runs finished) shows what the host hold
  TRADES: APPLY-DROP 695 (census4) -> 203 (colours alone) -> 309 (colours + hold), CONTINUATION-DIFFERS 365 ->
  442 -> 284; the two together 1,060 -> 645 -> 593. A batch the hold makes unpayable declines to the per-cast
  payer, and that walk drops named casts the unheld joint solve paid -- e.g. {Emiel the Blessed, Wild Growth}
  (29 of the 122 hold-only drops), whose batch needs the host's mana but not the bonus.

* **The staged Aura prepay (`MTG_PREPAY_AURA_STAGE`, default ON) and the unheld retry (`MTG_PREPAY_AURA_UNHELD`,
  default OFF -- USER decision owed).** Instead of declining a batch the held hosts leave unpayable,
  `BatchPrepayMainCasts` now solves it in two stages: (1) the held land Auras' own bills, off every source but
  their hosts; (2) the rest of the batch from what stage 1 leaves, with each host untapped and carrying a
  stand-in of its Aura, so it pays its own mana AND the bonus. Two facts shape what it may do with the answer.
  A land Aura's bonus is a triggered mana ability (CR 605.1b): the mana exists only once the Aura has resolved
  and its host is tapped. And the order the casts are REALISED in is fixed after the prepay (the enabler
  hoist, `CastOrderLess`, the deck's cast arrangement, the range ladder, `ApplyPayableCastOrder`), so it is
  not the order of `acts`. Seven versions, each failing on something the one before could not see:
  - v1 pre-loaded both stages into the pool. Illegal: a cast realised BEFORE the Aura spent bonus mana that
    did not exist yet. The AccomplishedAlchemist PROPOSED-order test caught it ({Wild Growth, Living Wish}
    on two Forests, the Wish cast first).
  - v2 split by `acts` order (stage 1 = every cast up to the last held Aura) and retried the unheld joint
    solve before declining. Held-out A/B: EDF gi1958 T3 -> T4 and gi1867 T4 -> T5, at every budget and at
    d8 b0. Both bisected to the unheld retry plus the acts-order split: the joint solve paid the batch with
    Brushland as an ordinary source before Wild Growth went onto it, and lost the bonus the per-cast walk
    would have kept.
  - v3: stage 1 is ONLY the held Auras' bills, and only stage 1 is pre-loaded, once stage 2 is shown
    payable; the rest is paid cast by cast. Only an APPLY (the executor, the rollout's two calls) takes the
    partial pre-load; it returns false (`PP_AURA_PARTIAL`) and the per-cast payer finishes. A PROBE
    (`SubsetPayableAsExecuted`, `OrderPrepaysWholeTurn`, the creature-tap key) still gets "false, state
    untouched", and the oracle's step hook counts a partial pre-load as a step made. Searched play was clean
    (seed 980001, 10,299 paired games: 0 slower, 1 faster), but the tiers' searched same-score changes held a
    dropped cast (regression s2002 gi85 T4: Emiel the Blessed left in hand) and d0 was worse (EDF seed
    990001, 4,000 games: net +21 turns).
  - v4 fixed gi85's two causes. Stage 1's backtracker paid Trace of Abundance's {R/W}{G} off THREE lands
    ({W}{G} and a spare {C}), and the leftover stage 2 read came from the backtracker's own `out_leftover`,
    which spends hybrid-blind -- it kept the {W} Trace's pip took, so Emiel looked payable. Now the leftover
    is the settled pool less the settled bill, and each source stage 1 tapped is offered back, kept up when
    stage 2 still pays (at no more pain or energy) or newly pays -- judged by stage 2, since giving back the
    Coast (no {W}) still pays Trace and strands Emiel. d0 improved to net +11 but was still worse.
  - v5 closes the realised-order hole d0 found (EDF seed 991265 gi1264 T3: {Overgrowth onto Yavimaya Coast,
    Fertile Ground onto a Mariposa}; the Coast held, the Mariposa not, because the rest of the board cannot
    pay Fertile Ground without it; stage 1 paid Overgrowth off that Mariposa and the apply cast Fertile Ground
    first, which drained Overgrowth's pre-load). Two guards: the split runs only when EVERY land Aura of the
    batch with an untapped named host is held, and it is refused when the rest-first order (rest of the batch,
    then the Auras, no bonus) pays on the untouched board but not under the pre-load -- the order the
    decline's walk would have paid. Replayed, all eight v4 d0 slower games tie. Its A/B (seed 1030001
    searched, 990001 d0) was clean on EDF, FungusV3 and Bruna d0, but AccomplishedAlchemist searched gi682 was
    T5 -> T6 even at d8 b0.
  - v6: stage 1 never spends an untap-target-land source (Arbor Elf) that can re-tap a held host. The
    Arbor's tap is one more tap of a Forest, Aura bonuses included (`UntapLandSourceDef`), so the held host's
    new Aura raises what it makes, and stage 1 runs before that Aura resolves. gi682 T5 (Forest + Utopia
    Sprawl, a second Forest, Arbor Elf; Crab Umbra, two more Sprawls onto the enchanted Forest, Accomplished
    Alchemist -- seven mana): stage 1 paid the Sprawls off the bare Forest and the Arbor at the old yield, the
    turn made seven where the walk makes eight, and with no slack the per-cast payer dropped Crab Umbra.
  - v7 (shipped): the stage-1 give-back runs on EVERY stage 1, not only an over-tap, and once stage 2 pays it
    keeps a release only when the re-solve taps fewer sources or as many with fewer colours between them.
    Bruna T6 (Remote Farm, Razorverge Thicket, a fresh Forest; two Open the Armory and Wild Growth onto the
    Farm, five for five): stage 1 paid Wild Growth's {G} off the Thicket, and the apply's first Armory spent
    the Farm's second {W} on its generic, so the second Armory had no {W}. Now stage 1 takes the Forest.
  A diagnostic found both of these: `MTG_PREPAY_STAGE_SHADOW=1` replays every search-internal plan apply whose
  prepay took the split with the split off and prints the plans whose realised casts differ (`[stage-shadow]
  WORSE|BETTER`, with the board, hand, job and game index). On v6 (seed 1060001, play settings, 1,200 games)
  Bruna showed 1,127 such applies, almost all WORSE; on v7 Bruna is 333 BETTER / 25 WORSE, EDF 491 / 51 and
  AccomplishedAlchemist 19 / 138. AccomplishedAlchemist's are one T3 board of one game (gi25), whose result
  is the same in both arms. Every remaining WORSE apply examined comes down to the per-cast payer's
  generic-pip order (WUBRG in autonomous play: gi682's Alchemist spent both new {U} on its {3} and kept a
  {B}; Bruna's Armory spent its second {W}). The line-aware order human play already has is the fix for
  that, measured separately as `MTG_AUTO_LINE_GENERIC`.
  The unheld retry stays OFF: on the v4 run it was 2 slower / 1 faster in searched EDF (seed 1010001) and
  net +9 at d0, and gi2852 is T3 -> T4 even at d8 b0 -- the retry taps a host as an ordinary source, the
  search's rollouts pay through it too, and the Conservatory line that wins reads worse on turn 1. What it
  buys is real (the EDF census's APPLY-DROP 304 -> 225 over 142 games), so it is the USER's call.
  `MTG_PREPAY_PROBE` on EDF (40 games, seed 940001, v3): of 251,879 multi-cast calls, 20,826 prepaid whole and
  28,979 pre-loaded the Auras' share. v7 held-out A/B, staged split off vs on (83,104 games): searched at play
  settings on seed 1030001 (AccomplishedAlchemist 854, EDF 3,844, FungusV3 4,000, Bruna 1,000) and 1070001
  (AccomplishedAlchemist 854, Bruna 1,000, EDF 2,000): 0 slower / 0 faster on every deck, the same mean win
  turns, work +0.03% (EDF +0.18% and +0.09%, the rest under 0.02%). d0 on seed 990001 (4,000 each) 0 slower / 0
  faster on all four decks; EDF d0 seed 1020001 (12,000): 5 slower / 7 faster, net -6 turns, and all five
  slower games tie between the arms at d8 b0.

* **The per-cast payer's line-aware generic order in autonomous play (`MTG_AUTO_LINE_GENERIC`, default ON) and
  the scarce-colour hold (`MTG_SCARCE_COLOR_MASK`, default OFF)** (2026-10-28). The staged prepay's stage-shadow
  sweep showed every remaining WORSE realisation coming down to the per-cast payer, and the mana oracle found the
  same payer dropping casts on a smoke board. Three defects:
  - *A generic pip spends WUBRG-first.* The three line-aware generic orders human play has had since 2026-09-10
    (the float drain's surplus-first order, the payment pool's order, a choice source's colour) were gated on
    `HumanPlayActive()`; autonomous play and the search's rollouts spent a {U} the line still owed before a spare
    {B} (AccomplishedAlchemist gi682 T5: the triple-Sprawl Forest's {G}{B}{U}{U} paid Accomplished Alchemist's
    {3}{G}, Crab Umbra was dropped; Bruna: Open the Armory's {1} took the Remote Farm's second {W}).
    `LineGenericOrderLive()` now admits autonomous play under the lever -- but NOT for a line that casts a
    breakpoint opener (`g_line_opens_unseen`: a cantrip, a tutor, Treasure Hunt), whose named casts understate
    what the turn will owe. Without that guard the regression tier found TreasureHunt s3003 gi96 T4 -> T6 at every
    budget and at d8 b0: the line {Treasure Hunt, Treasure Hunt} kept the second Hunt's {U} and spent {R} on the
    first's generic, the first Hunt revealed Land's Edge {1}{R}{R} (the kill), and the continuation could not pay
    it; WUBRG had spent the {U}. Held-out A/B without the guard (seed 1080001, 39 decks at play settings, 20,910
    paired games): 0 slower / 2 faster (Dragons gi214 6 -> 5, Niv gi211 7 -> 6), 29 digests, work +0.001%. With the
    guard (the same baseline; the lever-off path is unchanged): 0 slower / 1 faster (Dragons gi214), 19 digests,
    work +0.024%. ADOPTED.
  - *A coloured pip taps the line's only source of another colour it owes.* EDF smoke s1009 T3 (Mariposa, Yavimaya
    Coast, a fresh Brushland; Wild Growth onto Mariposa, then Cloud of Faeries {1}{U}): Wild Growth's {G} took the
    Coast -- the board's only {U} -- over Brushland's {G}, battlefield order breaking the tie. The existing
    default-off `ScarceColorHoldMask` is the per-cast hold for exactly this; it now reads the line's remaining
    demand (`g_line_unpaid_cost` less this payment's pips) instead of the plan's totals, and sits behind heurarm
    `SCARCE_COLOR_MASK`.
  - *A land Aura's host hold forces the scarce source.* With Wild Growth onto Brushland the per-cast host hold
    keeps Brushland up and the Coast is the only {G} left; the payment's ladder ends in the unrestricted greedy, so
    the hold's own fallback took it. `ScarceHeldOnlyScope` (both host holds: the rollout's apply_one, AIEngine's
    cast) makes a failed scarce-held attempt return false so the caller releases the host first: Wild Growth is
    paid off its own host, the bonus lost, and the Coast pays Cloud of Faeries.
  The scarce hold's A/B (same batch): 3 slower / 0 faster (EDF gi103 4 -> 5, Hinata gi223 5 -> 6, Snow gi239
  7 -> 8), work +0.18% (EDF +1.29%) -- NOT adopted, owed a root cause; with the line-aware order on as well, the
  same three. EDF gi103 (seed 1080104) is T4 -> T5 at d8 b0 too: the held arm's search picks Shivan Gorge over
  Kitchen on T2 and casts Cloud of Faeries a turn early, so the hold changes what the rollouts read, not one cast. The v7 staged prepay already pays the Mariposa batch before the per-cast payer is asked, so the pins
  run with the split off.
  Not yet: the line hold (`g_line_unpaid_cost`) is hybrid-blind -- `AddManaCost` drops the hybrid pairs, so Trace of
  Abundance's {R/W} reads as a hard {R} to every reader of the hold, the line-aware orders included. In EDF there
  is little {R} to protect and WUBRG spends {W} first anyway, so it is no worse than before; the big-mana and
  mixed prepay splits still pay flat bills too.

**EDF seed 3003 T5** (9 ORACLE-GAPs on the new payer) was the oracle's own frontier prune, not the payer: see
section 6 item 16.


## 13. The payment refine and the executor's payment script (2026-10-28)

The USER: *"Because the mana not being searched was a compromise just for performance we are really looking for a
way to address all of the issues we can find"* -- and every fix lossless, by mechanism and dominance, never a lever
that trades one game for another.

**The structural fact.** The census's DOMINATED classes compare RESOURCES, and a tapped source is a resource. So a
payment that dominates the engine's can tap nothing the engine's left untapped: the better payment, whenever there
is one, is made of the engine's own taps -- fewer of them (Remote Farm alone for Giada's {1}{W}, where the greedy
tapped the Plains beside it and floated a {W} away), or the same ones on cheaper faces (Llanowar Wastes' {C} for a
generic pip instead of its painful {G}). That makes "is this payment dominated?" a small, exact question.

* **The per-payment refine (`MTG_PAY_REFINE`, DEFAULT ON since 2026-10-29 -- the USER's adoption, see 13.3).** After every OUTERMOST payment (not the oracle's own
  copy, not a float-only probe, not an armed damage board) that spent life, energy or a haste-tagged {C} on a
  noncreature bill, or left mana floating off two or more taps, `RefinePayment` (ManaOracle.cpp) enumerates the
  minimal payments of the same bill on a board where ONLY the payment's own taps are untapped, replays each on the
  real board, and keeps one only if (a) its taps are a subset of the engine's, (b) its end resources cover the
  engine's -- strictly, or with a source given back -- and (c) the sources it gives back can re-make every unit
  the engine's payment left floating (`ReachesByTapping`), so nothing the rest of the turn could have used is
  lost. Best = most sources given back, then life, then energy. The payment is then remade in place on the real
  board (`PayRefineAfter`, ManaPayment.cpp); a remake the payer refuses keeps the engine's own. Dominating by
  construction: never a swap, never a guess about the rest of the turn. Skipped where a field-wise restore is not
  exact (a sacrificed source, a graveyard or the opponent's life the payment moved). It runs at every budget and
  in every playout, so the search sees the refined board everywhere it pays.
* **Not where a same-phase untapper lives (`GameState::deck_has_mana_untapper`).** The trade in (c) -- a source
  given back for the unit the engine floated off it -- rests on "an untapped source is worth what it would float".
  That fails when something can untap the source again in the same phase: Cloud of Faeries' enter untaps two lands,
  so the engine's {1}{U} off Brushland AND Adarkar Wastes (one unit floating) comes back with both lands up and the
  unit still floating, where Adarkar alone (its {C} + Fertile Ground's {U}) leaves Brushland up and nothing
  floating -- one mana down. The fifth seed found it twice in EDF (1170001 gi38, regression s2002 gi108: the T3
  Cloud + Displacer go-off a mana short, T3 -> T4 even at d8 b0). So a deck holding a card that untaps a mana
  source mid-phase (Cloud of Faeries, Peregrine Drake, Reality Spasm, Arbor Elf, Wirewood Lodge, Staff of
  Domination, Galvanic Alchemist, Seedcradle Witch; stamped in `StampDeckTraits`, default true) gets no trade: a
  candidate must cover the engine's float outright. Jorn's and Moraug's untaps come at a phase boundary, where the
  float is gone anyway, and keep the trade.
* **What per-payment dominance cannot promise: the greedy payer after it -- and the payer rule that closes it
  (`MTG_LINE_FLOAT_COVERED`, default ON).** A refined board is resource-for-resource no worse, but the per-cast payer
  is greedy, and one more untapped source can change its next choice. NivMizzetCombo seed 1180001 gi521 T2: the
  engine paid Ponder off Saprazzan Skerry AND Cascade Bluffs (a {U} and a {C} floating); the refine gave Bluffs back
  and kept the {U}. Sol Ring's {1} then spent the {U} -- the payer drains float before it taps -- and Tandem Lookout's
  {U} had nothing: the turn went Sol Ring + Generator Servant, and the T3 Niv + Lookout kill became T4, at d8 b0
  too. The line rung `MTG_LINE_FLOAT_TAP_FIRST` exists for exactly this (pay the generic off a spare source, keep
  the float the line owes) but refused Bluffs because Bluffs "makes {U}" -- by filtering a {U}/{R}, and the only one
  on the board was that same float. When the reserve left after a payment's own pips already covers every coloured
  pip the line still owes, the rung's colour test refuses for nothing: no later pip needs any source for its colour,
  so a source paying the generic leaves the named line exactly as payable with the owed float kept. The rule
  skips the test then, under the unseen guard too (that guard protects creature-only-colour lands, whose clause
  is untouched; a first try that also exempted unseen lines is why it once looked like it did not work). Held out
  (seed 1210001, 48,020 paired games): 0 slower / 0 faster alone, decision streams moving only in Dragonstorm and
  EDF; with the refine its one turn change is gi521, T4 -> T3.
* **Quality, seven held-out seeds at every deck's play settings (base = `MTG_PAY_REFINE=0`):**

  | seed | paired games | slower | faster | note |
  |---|---|---|---|---|
  | 1110001 | 20,910 | 1 | 7 | Hinata gi63 T6 -> T7 is budget churn (both arms T6 at d8 b0) |
  | 1130001 | 20,910 | 0 | 2 | SelesnyaLifegain gi17, gi209 T5 -> T4 |
  | 1150001 | 19,658 | 1 | 1 | Niv gi1 T5 -> T6: the arms split at T1 on Ponder vs Brainstorm (no payment differs there); both T5 at d8 b0 |
  | 1160001 | 17,747 | 0 | 5 | |
  | 1170001 | 49,330 | 3 | 13 | with the untapper rule; Bruna excluded (its keep table landed between the arms). Niv 1180001 gi390 / gi462 T4 -> T5 recover (T4 both at d8 b0); **gi521 T3 -> T4 does not** (below) |
  | 1190001 + Niv 1200001 | 49,570 | 1 | 6 | held out, the final build: Niv gi262 T6 -> T7 recovers (T5 both at d8 b0) |
  | 1210001 | 48,020 | 2 | 3 | with `MTG_LINE_FLOAT_COVERED` (identical turns without it): FiveColour gi58 T6 -> T7 recovers (T5 both at d8 b0); gi774 T4 -> T5 is CLEAR CLAIRVOYANCE -- the non-clairvoyant search (`MTG_NC_SEARCH=1`) plays T5 in both arms at d8 b0 (the refine's one effect: a search T4 payment of {W}{U}{B}{R}{G} off Bloom Tender alone instead of Bloom Tender + three lands floating {W}{U}{B}, a genuinely dominating payment, which moved the T2 choice). Niv 1180001: gi521 T4 -> T3 |

  The first four rows are the refine before the untapper rule (below); the last three are the build that lands. Before
  the rule seed 1170001 read 4 slower / 15 faster, two of the slower EDF games failing at d8 b0 -- the rule fixed both
  (and the regression tier's EDF s2002 gi108, the same shape). Per deck the Niv net is negative on every seed of the
  final build (-1 / -7 / 0 / -1).
* **Census (10 decks x 30 games, seed 1100001, `logs/mana_oracle/rf1`):** DOMINATED-LIFE 3,444 -> 2,204,
  DOMINATED-LAND 1,649 -> 1,377, DOMINATED-CREATURE 44 -> 31. Every remaining DOMINATED flag is LINE-level -- a
  unit left floating at the end of a line, or a face choice that only pays off two payments later -- which the
  per-payment rule correctly refuses, because the float it would give up might be the next payment's.

### 13.1 Its cost, and the exact cuts that pay for it

As first built the refine made the A/B's wall +17.7% (FiveColour +746%). Every cut below is EXACT -- the same
payment chosen in every case -- and each was shown so on a pooled identity run over every suite deck.

* **The lazy surplus (`MTG_PAY_REFINE_LAZY`, default ON).** Profiled (FiveColour, 2 games, one thread), 90% of all
  time was the refine and 68% of that the superset step: a paying set holding a multi-unit source (Faeburrow
  Elder, Bloom Tender) is also tried with each one-more one-unit tap. 432 of the 903 refines were ONE payment,
  Nicol Bolas {4}{U}{B}{B}{R} over six taps, re-paid by the search from the same board (830 sets + 3,267 supersets
  each, hitting the enumeration's 4,096-probe cap). With every face REGULAR (its tap turns its own source and
  nothing else; `Face::regular`, measured in `MeasureFace`) a superset of a paying set S releases exactly
  |taps| - |S| - 1 sources, and the choice takes the most released first; so supersets are parked and a set's are
  verified only when they can still reach the best release found, each in its eager place in the order so ties
  break as before. The Bolas refine: 370 verifies instead of 2,250.
* **The memo (`MTG_PAY_REFINE_MEMO`, default ON).** The refine is a function of its boards: keyed on the engine's
  mid-turn identity of the board before (`MidTurnStateKeyOf`), every payment field read on BOTH boards
  (`PayFieldsDigest`: tapped flags, counters, all three floating pools -- the restricted ones the identity does not
  fold -- life, energy), the bill (`ManaCostDigest`, every field of the cost), its context, the useful-colour mask
  (the identity reads a library by size and front only), and the payer's own context (`PayContextDigest`,
  ManaPayment.cpp: the line being paid, the holds and budgets around it, the heuristic arm -- the float-only
  payment's leftover depends on them). A plan-source mask (the oracle's own Realize) is not memoised.
  `MTG_PAY_REFINE_MEMO_VERIFY=1` recomputes every hit.
* **The wild cap (`MTG_PAY_REFINE_WILD_OUT`, default ON).** After those two, EDF was still 3.2x: single games ran
  57 s against 7 s (seed 1130464 gi463: 486,000 refines, 15.8 million verified candidates, almost none accepted).
  Over half were Brushland under Fertile Ground or Trace of Abundance. The payer credits an any-colour land Aura's
  bonus as `wild` -- a unit whose colour is decided later -- and when that unit is still floating after the
  payment, no candidate can cover it: the table's faces always take a concrete colour, and every acceptance ends in
  covering the engine's floating pools key by key, `wild` a key of its own. With every face regular, a candidate
  and its re-taps can float no more wild than was already floating plus each source's wildest face (`Face::wild`,
  measured), so a payment that leaves more is answered before any search. 136,104 of gi463's 248,589 refines.
* **The Aura-pick fold (`MTG_ORACLE_FACE_FOLD`, default ON).** Two any-colour Auras on one land that credit the
  same amount (`AuraPicksSymmetric`) spell the same tap twice when their colours are swapped, so `BuildSources`
  keeps only non-decreasing pick pairs. It folds the oracle's own table too.
* **The positional cover (`CoversBoards`).** The acceptance compared full resource tables built for both boards on
  every candidate. Two boards that differ only in tapped flags, pools, life and energy -- a candidate replayed on
  the engine's board always does -- are now compared field by field in place; anything else falls back to the
  tables. `MTG_ORACLE_COVERS_VERIFY=1` checks every positional answer against the tables. The candidate is also
  ranked BEFORE it is proven: one the choice would not take changes nothing, so it is never covered or re-tapped.
* **Cheaper keys.** The bill is digested, not printed; the useful-colour mask is computed once per refine; the
  payer-context digest folds the ~590-slot heuristic arm eight slots to a word instead of one.

  | run | what it adds | identity | wall rf/base |
  |---|---|---|---|
  | first build | | | 1.177 (FiveColour 8.5x) |
  | seed 1120001 | lazy surplus + memo | eager vs lazy+memo 12,714 paired: 0 digests changed; 6,050,533 memo hits verified, 0 mismatches | |
  | seed 1130001 | | | 1.185 (EDF 3.21x) |
  | seed 1140001 | Aura-pick fold | 12,714 paired: 0 digests changed | |
  | seed 1160001 | wild cap, positional cover, rank-first | wild cap off vs on, 17,747 paired: 0 digests changed; positional cover 0 mismatches | 1.096 (EDF 1.68x) |
  | seeds 1170001 / 1190001 | the untapper rule, packed digests | lazy tables + positional cover + digests vs the seed-1160001 build: 15,788 paired, 0 digests changed; refine-off arm vs the previous binary's: 4,954 paired, 0 changed | 1.105 (EDF 1.64x) / 1.060 held out |

  EDF seed 1130464 gi463: 56.9 s -> 13.6 s (7.4 s with the refine off). The held-out 1.060 is low: FiveColour ran
  with every trade blocked there (the Garth bug, fixed after it), and FiveColour alone measured about 1.25x before.
  Heaviest decks on the final build: EDF 1.63x, FiveColour ~1.25-1.6x, AccomplishedAlchemist and SelesnyaLifegain
  ~1.4x -- the refine still enumerates where the untapper rule then refuses the trade; an exact pre-check (no trade
  possible and no face cheaper) would cut that, and is the next cost item.

### 13.2 The executor's payment script (`MTG_REAL_PAY_SCRIPT`, default OFF -- a USER decision)

Real play only. Before the executor applies the chosen plan, `PlanPaymentScript` (ManaOracle.cpp) runs the
search's own apply on a copy; when that apply pays every action but on a DOMINATED set of taps, the oracle's
dominating realisation (masked to the engine's own end-untapped sources, replayed on the real board, same line
and same core key) becomes the payment script. The executor pays through a SAFE hook (`RealPayScope`): an entry
is used only while its bill matches and it pays; otherwise the board is restored and the engine pays this payment
and every later one. Not for a line that draws or casts a card the plan did not name, a combo route, or an armed
damage board; node cap `MTG_REAL_PAY_NODES` (600).

* **It no longer pays a DROP.** As first built it also paid lines whose search apply DROPPED an action the board
  could pay for. Held-out A/B (seed 1130001, 20,910 paired): 17 slower / 0 faster, 15 of them drop-paid -- the
  executor cast a spell the search's apply had dropped (Niv gi214 Preordain, Dragonstorm gi234 Kolaghan, Slivers
  gi27 replicate vs Aether Vial, Melira gi138 Chord, Hinata gi219 Preordain), i.e. it played a line the search
  never evaluated. That breaks the search-executor contract, so the branch is gone (`PlanPaymentScript` returns
  false on a partial apply). A dropped action is the SEARCH's problem to fix (section 13.4, item C).
* **Dominating-only:** seed 1150001, 19,658 paired: 0 slower / 0 faster, wall +1.1%. Its one slower game anywhere
  is FiveColour seed 1130288 gi287, T6 -> T7, NOT recovered at d8 b0. Root cause: the script leaves Breeding Pool
  untapped on T5 (a dominating payment), and the attack heuristic then keeps Deathrite Shaman home, so T6's
  attack is one damage short. The defect is in combat, not mana; the script stays OFF until the USER decides
  (fix the attack rule first, then re-measure).

#### 13.2.1 gi287 root-caused: the script broke COMBAT LOCKSTEP (2026-10-31, branch `wip/dork-hold-script`)

The USER asked: *"we should fix the issue first. Don't we search dork attackers?"* We do, but that was not what
failed here. The mechanism, from `MTG_REAL_PAY_ATK_TRACE=1` on gi287 (`--seed 1130288 --game-index 287`, play
settings d7 b40; OFF T6, ON T7):

* T5's plan is *Two-Headed Hellkite + Deathrite #12's drain* (exiling Unite the Coalition, 16 -> 14). The
  engine's own apply of it taps Breeding Pool for a {G} that FLOATS and then empties at combat. So on the board
  the search scored -- `[rp-atk] engine-apply board: 9T ... 13 ...` -- the second Deathrite (#13) is the only
  untapped source when attackers are declared: 1 mana casts nothing in hand (Bloom Tender is {1}{G}, Faeburrow
  Elder {1}{G}{W}), and `FiveColourProvider::ShouldAttackWith` sends it in. The search's line: 14 -> 8 after combat
  (`[fsw] T5 ... Two-Headed Hellkite,Deathrite Shaman, after=8 tail=6`), T6's swing exactly lethal.
* The script pays the same line without Breeding Pool (`script board: 9 ...`). At the REAL declaration Deathrite +
  Pool make 2 mana, Bloom Tender is castable, and the same provider rule holds Deathrite home. 5 damage, 9 life
  left, main 2 casts nothing, and T6's total of 8 is under 9, so the lethal exemption does not fire and the hold
  repeats (only the Hellkite swings). T7.
* So the combat the executor realised was not the combat the search scored. The search never saw the script's
  board, since it scores a plan by its own apply, so no searched branch could have decided this.

**Does the searched dork attack cover this hold anyway? No.** `DorkAtkContestedKind` contests (1) a dork the
collapsed-main TOWER holds (`HoldManaSourceForCollapsedMain`, which holds 0-power dorks only) and (2) an
attacking dork whose mana the deferred main would spend. A DAMAGE dork held by an archetype's `ShouldAttackWith`
was explicitly left greedy (*"a ShouldAttackWith decline stays greedy"*). Only FiveColour has such a rule (every
other provider holds only 0-power bodies), and it fires whenever any hand card is castable.

**Fix 1 -- `MTG_REAL_PAY_ATTACK_PIN` (heurarm `REAL_PAY_ATTACK_PIN`, DEFAULT ON, inert unless the script is on).**
`PlanPaymentScript` now also returns the scored combat: our creatures on the engine's apply board, each marked
attacked / not, declared through `RolloutCombatDeclaration`. That is the rollout combat's own pre-declaration body
(`CombatPreDeclare`, factored out of `SimulateOneCombat`: the pool empties first, which is exactly what the float
hinges on), read under the plan's own `atk_dork_release` variant. `AIEngine::DeclareAttackers` then declares that
set for a scripted pre-combat main. A body the scored board did not list keeps the heuristic's verdict. The pin is
dropped after a breakpoint re-solve or an abandoned script, and it does not apply when an extra combat runs first.
The saved sources are pure surplus for main 2. This is a lockstep fix (the executor replays the search's
decision), not a heuristic.
* gi287 with the script: **T7 -> T6** (`atk-pin-changed=1`).
* Held-out A/B, seed 1620001, one pooled batch, script ON both arms, pin 0 vs 1: FiveColour 3000, FiveColour 2HG
  (d5 b40) 400, Anti-Lifegain 1500, Fungus 1500, Prevent Damage 500, Hinata 200, Bruna 200 (the decks whose
  attack rules read untapped mana) -- **7,300 paired, 0 slower / 0 faster, units 1.000x**, 9 digests moved (44 real
  declarations changed). Script-vs-engine on FiveColour (seed 1620001): without the pin 0 / 0 with 6 games' digests
  moved; with it 0 / 0 with 1 moved, so the pin removes most of the script's play divergence.
* gi287's own seed block (seed 1130001, FiveColour 600, play settings): engine -> script without the pin 1 slower
  (gi287 6 -> 7) / 0 faster; engine -> script WITH the pin 0 / 0 and the job's aggregate digest identical to the
  engine arm's (`61402fbdaa9ead00`): on these 600 games the pinned script plays exactly the engine's games.
* Scenario `test/scenarios/fivecolour_script_attack_pin.json` (gi287's T5 board, `MTG_REAL_PAY_SCRIPT=1`): T6;
  T7 with `MTG_REAL_PAY_ATTACK_PIN=0`. Unit test `test/unit/test_dork_atk_provider_hold.cpp` pins the float detail
  (declared on the post-apply board as it stands, a floating {G} lets Bloom Tender be cast and Deathrite is held;
  `RolloutCombatDeclaration` empties the pool and Deathrite swings) and kind 3's contest/release.

**Fix 2, measured but NOT adopted -- `MTG_DORK_ATK_PROVIDER_HOLD` (heurarm `DORK_ATK_PROVIDER_HOLD`, DEFAULT
OFF).** This answers the USER's question directly: it makes the searched dork attack contest an ARCHETYPE hold too.
`DorkAtkContestedKind` returns kind 3 when a non-vigilant, untapped mana dork that would deal >= 1 damage is held
by the provider's `ShouldAttackWith` (not by the tower or the aura-swap hold), on any board, collapsed main or not.
The FSLineWin branch then evaluates the release (override 3, which releases only those dorks) beside the natural
combat, and the executor pins the choice (`Plan::atk_dork_release = 3`). Kind 3 is checked last, so every verdict
kinds 1 and 2 reach is unchanged. Ties go to the hold, because there the hold is the deck's stated policy (USER
2026-08-09, *"Deathrite only very rarely wants to attack"*) and a provider may order options. It is inert on every
deck but FiveColour.
* FiveColour, seed 1620001, 3000 paired at play settings: **8 slower / 31 faster** (one of the faster is a no-win
  -> T8), **units 1.35x**. All 8 slower games RECOVER at d8 b0 (`--depth 8 --budget-ms 0 --ignore-play-profile`,
  both arms equal to the OFF arm's play-settings turn): gi284 5, gi298 4, gi406 4, gi713 5, gi1531 5, gi1539 6,
  gi1697 5, gi2291 4. Net -22 turns over the games both arms win, plus gi947 (no win -> T8).
* Not the gi287 fix: with it on, gi287 also wins on T6 with the script, but only because the line changes from T3
  on. The scored-board hold at T5 is not contested, because Deathrite attacks on the search's board.
* A trade-off (+35% FiveColour compute for 31:8), so it is the USER's call; it stays OFF until then.

### 13.3 The adoption call (USER)

`MTG_PAY_REFINE` lands DEFAULT OFF, so default play is byte-identical (smoke 153/153, regression 246/246, 0
reference drift). Whether to turn it on is the USER's call. With `MTG_LINE_FLOAT_COVERED` (2026-10-28) it meets the
bar:

* For: a lossless rule per payment (the untapper rule included); held out, every slower game either recovers at
  d8 b0 or is clear clairvoyance (FiveColour 1210001 gi774); every deck's net <= 0 pooled over the final build's
  seeds 1170001, 1190001 and 1210001 (FiveColour 2 slower / 3 faster); it removes the census's single-payment DOMINATED flags (Remote Farm + Plains,
  painland faces) in real play, the viewer's auto-tap and the search alike. Niv gi521, the one game that failed the
  bar, now recovers (T3).
* Against: the wall cost is +6-10% (EDF 1.6x; 1.07x over the suite on seed 1210001).
* Recommendation: turn it on.

**Adopted 2026-10-29 (USER):** *"I think that 7% might make sense, particularly if we are improving quality and
reducing unrecoverable lines."* Re-measured first on the tree that lands it (Garth's crack, the sac-colour credit and
the sequenced rock credit already in): seed 1300001, every suite deck at its play settings, 48,020 paired games, 1
slower / 3 faster (FiveColour 0 / 1, Stompy 0 / 2, Niv 1 / 0), units 0.996x. The one slower game, Niv gi62 T4 -> T5,
recovers (T4 in both arms at d8 b0, the identical game). Niv's pooled net over the final build's seeds stays
negative (-1 / -7 / 0 / -1, then +1 on 300 games).

### 13.4 What is left

Every DOMINATED flag that survives the refine is line-level: a unit left floating at the end of a line, or a face
choice that only pays two payments later. The design for the search side is an end-of-line masked re-apply: when
a plan's apply ends with mana floating, re-apply with a tapped source whose units are no more than the leftover
reserved, and keep it only if the line and its core key are unchanged and the end board strictly covers. The
census's other classes stay on the program: C (a dropped action the board could pay -- the search-level
fallback), D (exact menu gates for ENUM-GAP and PHANTOM), E (oracle coverage: UNMODELED, CAPPED, ORACLE-GAP).

**The big census, by mechanism** (`big1`: 39 suite decks x 30 games, seed 1100001, the pre-refine engine; 37 decks
finished -- KittyEquipment and LandfallElementals were stopped after five hours in oracle CAPPED work). ENUM-GAP 442:
* AccomplishedAlchemist 163 -- "gain life, then Stroke of Genius for X off Accomplished Alchemist's life-gained
  mana" is not ONE menu plan, but the search reaches it: a lifegain cast is a breakpoint (the LIFEGAIN route,
  TurnSolver site 9) and the rest of the turn is re-planned with the grown yield. An oracle accounting gap (E: a
  breakpoint continuation should count as reaching the line), not a lost line.
* Mirrorwing 41 -- Gold Rush into Gold Rush off the first one's Treasure: the FRESH-HOLD doctrine (a Treasure minted
  this turn is banked; `FreshHoldActive`, released only for a same-turn kill). A search doctrine measured worse when
  released (73 slower / 31 faster), not a mana gap; the oracle should label it as such.
* NivMizzetCombo 106, Slivers 34, KittyEquipment 22 -- Generator Servant's sacrifice mana and filter-land chains;
  Sliver Hive's tap-for-token and Mutavault; Remote Farm's {W}{W} split across two casts. Untriaged.
APPLY-DROP 339 (Niv 145, Melira 68, Alchemist 41, LandfallElementals 39): long lines (Melira's 32-step Felidar loop
into Chord of Calling) where the first payment decides a cast forty actions later -- item C's territory.

### 13.5 The ENUM-GAP triage (2026-10-29), and the first two fixes

Census `eg1` (11 decks x 30 games, seed 1100001, the refine-era engine): 210 ENUM-GAP. Four triage passes sorted
every flag by mechanism, each one reproduced on a single-game repro (`MTG_DBG_MULTI=<turn>` names the rejecting
gate). One trap when reading it: the oracle's observe pass also switches the odometer's mana bound off, so a subset
the bound cut from the real menu prints no line before `DECISION` but an `early-gate` line in the observe pass.

**Real gaps** (a payable plan the engine's gates keep off the menu):

| deck | flags | mechanism | where | fix | status |
|---|---|---|---|---|---|
| FiveColour | 6 | Garth's conjured Black Lotus cannot be cracked in the plan that conjures it | Garth emission (CollectActions) | conjure-and-crack variant | **fixed below** |
| FiveColour | 8 | `SubsetWidensDomainBy` is all-or-nothing: a subset holding several domain wideners gets no widen when not all of them are payable | the domain widen credit, AND (the real first cut) the odometer's total-mana bounds, which had no widen term | **fixed** (`MTG_DOMAIN_WIDEN_PARTIAL` + `MTG_DOMAIN_WIDEN_ORDER`; below) |
| FiveColour | 1 | `HasteUnlockedManaOf` misses a hasted domain dork's own colours | the haste-unlock credit | add them | **fixed** (`MTG_DOMAIN_SELF_COLORS`: `AddSourceToPool` with no permanent counts the source's own colours) |
| Fungus | 3 | a same-plan mana GRANT (Brightcap Badger) is not credited | the rock/grant credit | credit the granted bodies | **fixed** (`MTG_GRANT_CAST_CREDIT`; `badger-grant-defined-fungi-backtracker-gap.md`) |
| Fungus, FiveColour | 8 | Concordant Crossroads' blanket haste does not unlock the plan's own dorks | the rock-credit stamp | credit them | **fixed** (`MTG_HASTE_DORK_CAST_CREDIT`: a hasted creature mana source cast this turn carries `rock_mana`) |
| Giants | 2 | `SubsetPayableSequentialImpl` refuses any non-cast action (an Equip {0}) | the sequential walk's kind filter, `SubsetHasUnmodelledSeqKind`, `OrderPaysSequentiallyImpl` | skip zero-cost non-mana activations | **fixed below** |
| Goblins | 1 | the colour-presence gate misses a planned sac outlet's colour | `WidenHaveWithSubsetRocks` | the outlet half of `MTG_SUBSET_SAC_COLOR` | **blocked** (below) |
| NivMizzetCombo | 5 | a mana rock that pays for another rock: the rock credit applies only when the board alone pays every rock | the `pool.CanPay(rock_costs)` guard (both walkers) | an exact sequential rock check from the credited pool | **fixed below** |
| NivMizzetCombo | 2 | Generator Servant's {C} through Izzet Signet: a {1}-fed ramp filter turns a {C} into coloured mana, under-counted by one per fed filter | the per-subset credits | relabel up to one {C} per fed filter as wild | open |
| EDF | 6 | the rescue's scalar ceiling omits the ETB-untap refund (Cloud of Faeries, Peregrine Drake) | `ef-scalar` pre-filter | add the refund | **built, DEFAULT OFF** (`MTG_EF_SCALAR_REFUND`, 13.8: one held-out slower game does not recover; a USER decision) |
| EDF | 4 | the odometer gate prices the untap refund off the pre-plan board, missing a same-selection land Aura on the refunded land | `BuildManaGateIndex`, `ManaPruneBound` | scale the Aura bonus by the untappers | **fixed** (`MTG_ETB_BOUND_AURA`, 13.8) |
| EDF | 12 | the rescue walk is ONE greedy payment: Aether Hub's energy colour counted as cover, a painland's painless {C} never banked ahead of an untap, a coloured unit that must be banked before the untap, the tap-ahead's host and colour choice | the EDF sequential rescue | "Proposal X" (the oracle's bounded payment search as the last rescue, its script on the plan) -- NOT built: the shapes on the re-census are three payer rules, each exact, in the shared payer (13.8) | **fixed** for the energy-cover and painland-bank shapes (`MTG_TAPAHEAD_ENERGY_COVER`, `MTG_PAINLAND_TAPAHEAD_CAST_AUTO`, 13.8); the other shapes did not recur on the 20-game census (14 -> 6 ENUM-GAP shipped, 0 with the refund) |

**Oracle false positives** (the plan is not lost; the oracle must learn the rule): Slivers 34 (an apply that silently
skips a TapForTokenPay / AnimateLand whose source is tapped -- `Clean()` checks casts only); NivMizzetCombo 97
(Generator Servant offered as a payment-time source while the engine cracks it only as a plan action -- the `+sac`
twin is on the menu -- plus dominated plans and continuation artefacts); Selesnya 5 + Landfall 6 (Genesis Wave's
follow-up paid by lands it reveals from the library: hidden information); Hammer 4 (an equip skipped after Sigarda's
Aid; `ClassKey` keys on the entered-this-turn marker); Angels 2 (Giada's Angel-only mana restriction ignored); EDF 2
(Wild Growth on Brushland, payable only Cloud-first, where the menu already holds the dominating sibling with Wild
Growth on Adarkar Wastes -- the USER's catch: *"With Adarkar Wastes and Brushland you would play Wild Growth first
... I see no need to change the order for that case"*. The oracle should compare against siblings that differ only
in an Aura's host).

**The oracle's false positives, fixed (2026-10-29; oracle-only, play byte-identical).** The ENUM-GAP pass's per-plan
check is now `EnumGapPlan` (ManaOracle.cpp; `manaoracle::EnumGapVerdict` is its unit-test hook). The first full
outcome still ends an order's search, and when the menu reaches its board that is still the order's answer. Only an
outcome that would be REPORTED is asked the costlier questions. If it fails one, the search goes on to the next payment
(`Realize`'s `accept` test). The rules:
* *An action the realisation did not perform* (`PerformedPlan`). Remove each of the plan's actions in turn and replay
  the same payment script. If the end board is identical (exact key, every resource, the floating pools, the whole
  script consumed), that action did nothing in this realisation. When the plan without it is on the menu, or is a plan
  of the observe-mode enumeration (checked on its own), the outcome is not this plan's hit. This covers Slivers 34 (a
  TapForTokenPay / AnimateLand skipped on a tapped source), Hammer 4 (an equip the Equipment's entry trigger already
  made), and the Niv continuation artefacts (a cast that a breakpoint's continuation replaced). It is general: any
  silent skip in ApplyPlanDirect's trailing pass is caught the same way.
* *The hand cards a line USED* (`UsedFromHand`). A card put back on the library (Brainstorm) is no longer counted as
  cast.
* *A land drop the plan did not make* (`TapsContinuationLand`). A breakpoint's continuation may play a land from hand
  in a `no land` plan, and that land can then pay for the plan's own casts. If the plan with that land drop is on the
  menu or enumerated, the outcome belongs to that plan. NivMizzetCombo seed 1100005 gi4 T3: once the Servant could no
  longer be cracked mid-payment, Island #19 paid for Curiosity.
* *No SacAction route in the ENUM-GAP pass* (`t_no_sac_action`, read in `BuildSources`). Generator Servant and an
  on-board Lotus make their mana only through a plan's own SacForMana action. The plan WITH the crack is in the
  observe-mode enumeration and is checked on its own. Otherwise it is structurally folded, or it cannot be emitted at
  all (a Servant cast this turn); both are dominated lines. AuditPlan keeps the route, because that is how it finds a
  better crack timing. Fixes Niv 95.
* *Hidden information* (`TapsHiddenCard`, a new quiet class **HIDDEN-INFO**, not a MISS). The top block of the library
  is the root library's leading cards that are gone by the end of the line, i.e. a reveal, a dig or a draw; a search
  takes its card from anywhere. A payment that taps one of those cards spent mana the player could not have planned on.
  A top card that Courser of Kruphix reveals is exempt. Applies to Selesnya 5 and Landfall 6 (Genesis Wave). They are
  reported as HIDDEN-INFO only when no other payment pays.
* *A subtype restriction* (`OracleCreatureTap`, `BuildSources`). The frontier now records the spell being paid for
  (`Hook::f_spell`, from the cast site's `PayingSpellCard()` scope, ManaPayment.cpp `ScriptedPay`). `MinimalPayments`
  sets that scope, so Giada's {W} is offered only to an Angel spell. Fixes Angels 2. The "subtype-restricted mana"
  UNMODELED note is now kept only for the ability half (Smokebraider).
* *A land Aura's host is a choice of the plan, not a resource* (`HostBlindCore` / `HostBlindRes`). The menu covers a hit
  when it reaches the same board with every mana land Aura's host set aside and at least the same resources, where each
  Aura whose host is still untapped counts as its own resource (`AU:`). Fixes EDF 2. It also covers three that the
  triage had filed as real walk misses (classes F and G), because the menu holds the same casts with the Aura on another
  land and reaches the same board or a better one. Seed 1100030 gi29 T3: Overgrowth on Conservatory #13 instead of #15,
  an identical board. Seed 1100005 gi4 T3: Overgrowth on Kitchen instead of Mariposa, identical. Seed 1100023 gi22 T4:
  Trace of Abundance on Mariposa instead of Conservatory, which ends with two lands more untapped and 2 life more. The
  greedy walk's miss on the rejected host is still there; it just costs no option on these boards.
* *An untapped land's age is no resource* (`ResClassKey`). In the resource tables, a land that can never be a creature
  no longer records whether it entered this turn: an untapped new Mountain equals an untapped old Mountain. A manland
  keeps the marker. Precision for all classes. On the census it also removed one false Fungus ORACLE-GAP (the engine
  left the new Forest up, the oracle left the old one).

The census, re-run on the same 11 decks x 30 games at seed 1100001, compares the tree before the fix (56a28d38d, which
already has Garth's crack) with the tree after it:

| deck | ENUM-GAP before | after | what is left |
|---|---|---|---|
| NivMizzetCombo | 106 | 6 | rock funds rock (4) and {C} through Izzet Signet (2): real |
| Slivers | 34 | 0 | |
| EDF | 24 | 19 | the rescue-walk classes A-G (proposal X): real -- 13.8 takes its own 20-game census 14 -> 6 shipped (0 with `MTG_EF_SCALAR_REFUND`) |
| Fungus | 11 | 11 | F1 / F2: real |
| FiveColour | 9 | 9 | A / B: real |
| Selesnya | 5 | 0 | (5 HIDDEN-INFO) |
| Landfall | 6 | 0 | (6 HIDDEN-INFO) |
| Hammer | 4 | 0 | |
| Giants | 2 | 2 | the walk refuses Equip {0}: real |
| Angels | 2 | 0 | |
| Goblins | 1 | 1 | the outlet half of `MTG_SUBSET_SAC_COLOR` (blocked): real |
| **total** | **204** | **48** | |

Every flag that went away belongs to one of the classes above, and no new ENUM-GAP appeared. Niv flag 25 (`no land +
Izzet Signet x2 + Sol Ring + Preordain`) went with them: its only paying lines tap the Island that the post-Preordain
continuation played (`TapsContinuationLand`). The `land=Island` version of the same plan is still reported, and so is
its no-Preordain twin.

The other classes moved through the same rules, most of it through `ResClassKey`:
* Slivers DOMINATED-LAND 5 -> 3 (the ENUM-GAP pass's DOMINATED-* for a fizzled TapForTokenPay).
* Fungus ORACLE-GAP 4 -> 3, from 13 -> 12 overall (a false gap: new against old Forest).
* The frontier prune now folds a new land against an old one. Plans that hit the 2,000-apply cap before now finish:
  CAPPED 2445 -> 2185 (niv gi7 388 -> 147, gi29 51 -> 39, Landfall 847 -> 843), and oracle applies fell from 17.3 M to
  16.6 M. Those plans now get verdicts they never had. APPLY-DROP rose 171 -> 196, DROP-COVERED 470 -> 487 and
  DOMINATED-LAND 1474 -> 1522, all of it in NivMizzetCombo (gi7, gi29, and gi27 +8). At gi27, outcomes that differed
  only in a land's age used to be incomparable, and are now compared.

Play with the oracle off is byte-identical (39 decks x 12 games, digests and units). With the oracle on, play is
identical to the oracle-off run (all 11 census decks).

**Fix 1: Garth's conjure-and-crack (`MTG_GARTH_LOTUS_CRACK`, DEFAULT ON).** USER: *"We should be allowing garth to
produce black lotus for mana for sure."* The conjured Lotus is a noncreature artifact, so it can pay its own {T} the
moment it lands, but a SacForMana is only ever emitted for a Lotus already on the battlefield, so {Garth -> Lotus,
crack, the casts it pays for} was inexpressible (seed 1100028 gi27 T6: two Two-Headed Hellkites off eleven mana plus
the Lotus's three). Now a conjure-and-crack GarthActivate (`ritual_float` 3) is emitted beside conjure-and-keep, ONE
VARIANT PER COLOUR and credited in that colour (`CreditFixedColorSac`); `ApplyGarthActivate` cracks the copy as it
lands, from every apply site (`TurnSolver::GarthLotusCrackColor`), so the search, the executor and the oracle agree.
Autonomous play only: a human already cracks a conjured Lotus at payment time, and a second "conjure Black Lotus"
row would make a typed or saved line ambiguous. The colour is searched, not folded: the sac-colour fold's scarcity
guess (`SacFloatColorFor`) picked R, W, U or B for the same {Cannons, Progenitus} demand depending on the board, and
the apply dropped Progenitus. gi27 T6: 0 ENUM-GAP after (6 before). Its remaining APPLY-DROPs there are ONE plan,
{Cannons, Progenitus, the crack} (20 flags: each colour variant, both land choices) -- Cannons paid out of Bloom
Tender's five without keeping the {U}{B} Progenitus needs -- the line-level payer class (13.4, C).
Held out (seed 1240001, 5,000 FiveColour games): 2 faster / 2 slower, both slower recovering at d8 b0 (gi1385,
gi2083: T5 both arms); wall 1.017x interleaved (units 1.033x).

**Fix 2: same-subset sac colours (`MTG_SUBSET_SAC_COLOR`, DEFAULT ON).** The colour-presence gate
(`WidenHaveWithSubsetRocks`) saw only board colours, while the flat gate credits a same-subset sac float: an on-board
Lotus Bloom's {B} for a Dragon was "no source makes black" whenever no land made it. The `MTG_SUBSET_ROCK_COLOR` shape,
now for `sac_for_mana_amount` sources and Garth's crack: a pinned colour, or every colour when it is resolved at
apply. Held out: Dragonstorm 53 faster / 0 slower (seed 1240001, 5,000 games; no-wins 61 -> 53, avg 4.3912 -> 4.3732)
and 56 / 0 (seed 1220001; 59 -> 52, 4.3996 -> 4.3838); wall 0.95x. Every other suite deck byte-identical.

**Fix 3: rocks that fund each other (`MTG_ROCK_SEQ_CREDIT`, DEFAULT ON).** Both subset walkers credit a mana rock cast
in the plan only "once the board can already pay for the rocks themselves" -- tested all-or-nothing against the sum
of every selected rock's cost, so Sol Ring paying for an Izzet Signet, or one Signet for a second, earned nothing.
`SequencedRockProduction` is the `SequencedRitualCredit` walk for rocks: a rock is credited once the board plus the
rocks credited before it pay the accumulated cost, to a fixed point; a rock still never funds its own cost, and when
the board pays every rock the result is the old one. NivMizzetCombo seed 1100014 gi13: the rock ENUM-GAPs 4 -> 0
(and the game T6 -> T5); seed 1100022 gi21 T3 {Signet, Signet} closes. Some newly admitted plans are APPLY-DROP-ORDER
-- the deck's own cast order puts a Signet before the Sol Ring that pays for it (a USER-owned order); the search
scores those by what their apply really does. Held out (seed 1260001, every suite deck, 48,020 paired games): 37
decision streams move (Dragons 10, Landfall 2, Niv 25), no win turn; Niv seed 1270001 (1,500 games): 2 faster / 2
slower, both slower recovering at d8 b0 (gi1373, gi1465: T5 both arms); units flat.

**Fix 4: a free attach no longer blocks the sequential walk (`MTG_SEQ_FREE_ATTACH`, DEFAULT ON).** The rescue that
admits a subset payable only in sequence (a cost reducer before the spell it discounts) refused any subset holding an
action other than a hand cast or a Vial put. Lightning Greaves' Equip {0} taps nothing and spends nothing, so the
three walk sites (`SubsetHasUnmodelledSeqKind`, `SubsetPayableSequentialImpl`, `OrderPaysSequentiallyImpl`, in
lockstep) now skip it as they skip a Vial put, and the human's attach-every-free-Equipment bundle with it. Skipping
can only hide mana from the walk (Greaves' haste on a fresh dork), never invent it -- except by moving Greaves off a
summoning-sick host whose {T} it enables, which stays refused. Giants seed 1100023 T6: ENUM-GAP 2 -> 0
({Stinkdrinker Daredevil, Tectonic Giant / Sunrise Sovereign, equip Greaves}), the game's win turn unchanged. Held
out (seed 1310001 every suite deck, plus Giants seed 1320001 4,000 games; `MTG_PAY_REFINE` on in both arms):
52,020 paired games, 0 slower / 0 faster; Giants 87 and Dragons decision streams move; units +0.3%.

**The outlet half is blocked.** The same credit for a creature sac OUTLET (Skirk Prospector's {R}, Utopia Mycon's any
colour) is just as valid, and measured Goblins 32 faster / 3 slower and Fungus 2 / 1 (seed 1230001, 4,000 games
each). Fungus gi1040 and Goblins gi1914 recover at d8 b0; Goblins gi3401 (T3 -> T4) and gi691 (T4 -> T5) do not,
and both win on the base turn under the non-clairvoyant search, so neither is clairvoyance:
* gi3401: the committed T3 plan {Muxus, a Skirk cast, a Skirk sacrifice burst} -- the second Skirk is the card
  Rundvelt Hordemaster exiles when a sacrificed Goblin dies -- casts Muxus in the search's rollout and not in the
  executor (three sacrifices, four mana, Muxus costs five with Goblin Warchief): a rollout/executor lockstep break in
  the burst + impulse path. **Root-caused and fixed (`MTG_ECHO_STAGED_GAS`, DEFAULT ON):** the break is Mogg War
  Marshal's ECHO at the next upkeep, not the burst. Between the executor's TakeTurn passes the impulse card sits OUT
  of hand in `Player::staged_cards`, while the rollout keeps it IN hand flagged `m_is_staged`; the provider's
  `PayEchoToKeep` clause (b) ("nothing else to cast -> pay") scanned the hand only. So the search saw gas and declined
  the echo, the real game saw none and paid it, two of three Mountains went, and the committed Muxus was no longer
  castable. Both representations are now read (an unexpired staged card is gas in both worlds, an expired one in
  neither); `test/unit/test_echo_staged_gas.cpp` pins the agreement and the lever-off split.
* gi691: no divergence -- the unlimited search over the larger menu simply commits a T5 line where the base finds
  T4 (Piledriver held for turn 4). The mechanism is a breakpoint base plan that took the unbranched canon and is
  never re-opened as a wave walker, so the larger menu's cascade cannot challenge it. **Fixed (`MTG_BP_CANON_WAVE`,
  DEFAULT ON):** a base plan that took the canon default becomes a walker base too. Its slots are handed out LAST
  (after every other slot), and under a finite budget the enumerations its variants run are billed to the host's
  budget (`MTG_BP_CANON_WAVE_W` visits per unit, default 1). The first, unbilled version hung
  EldraziDisplacerFlicker seed 1330259 gi258 at T1: three turn-4 bases with ~50-card hands each opened ~13,000
  rank variants, and a continuation derivation is an enumeration, not a node, so the walker's budget check never
  fired (234 of 18,000 units used after 45 s). Billed, gi258 wins T4 with the off arm's moves (21 s vs 15 s wall:
  the billed T1 line is honestly marked truncated, so T2/T3 search again). At `--budget-ms 0` nothing is billed and
  every slot is reached. gi691 at d8 b0: T5 -> T4. Held-out (seed 1390001, every suite deck, 34,154 games,
  `MTG_PAY_REFINE` on): 15 faster / 0 slower, units +0.41% (EDF -9.2%, Goblins +29.8%, Minotaur +10.9% with an
  identical digest).
* A third, found on the re-measure (seed 1330145 gi144, T3 -> T4 at d8 b0, T3 on both arms under the non-clairvoyant
  search): with the outlets credited the T1 search only finds a turn-4 win. Open.
`SubsetSacColorSource` leaves the outlets out until gi144 is fixed.

**Also found: the sac-colour fold is a guess.** `MTG_SAC_COLOR_FOLD` (on-board Lotus / Lotus Bloom) resolves the
colour at apply from a scarcity score over the accounting pool, which counts a dual land toward both its colours; the
gi27 trace shows one demand folding to four different colours. Where it guesses wrong the plan drops a cast the
fanned colour pays -- an APPLY-DROP the oracle can see. **USER ruling 2026-10-31: NOT a branch per colour** -- *"Lotus
colour guess should not be a new branch. This is the kind of thing I want to avoid. We spend it when we need it
targeting the most relevant colour."* So the lossless shape is a DEFERRED colour: the sac mana is credited as
uncommitted any-colour and its colour is committed at the moment it is spent, to the pip the payment and the rest of
the line need (the sequence-aware float commit, `CommitFloatChoicesBySeq`, wip/floatseq-land). Owed, after floatseq
lands: the on-board Lotus / Lotus Bloom fold AND Garth's conjure-and-crack (whose per-colour variants, above, are the
shape this ruling rejects) both move to it.

### 13.6 Three more gaps closed (2026-10-30): Crossroads haste, a domain dork's own colours, the partial widen

**Concordant Crossroads** (`MTG_HASTE_DORK_CAST_CREDIT`, DEFAULT ON). A creature mana source cast this turn that
will have haste the moment it enters -- its own keyword, or a haste lord we control that covers it
(`HasHasteFromLords`, CR 302.6) -- now carries `Action::rock_mana` like a cast mana rock, so the plan's later casts
may be funded by its tap. Fungus s1100027 gi26 T3: Forest + Undercellar Myconid + Sporecrown Thallid, the Myconid's
{W} paying the Thallid under Crossroads, was off the menu (25 plans -> 28; MISS 1 -> 0).

**A domain dork's own colours** (`MTG_DOMAIN_SELF_COLORS`, DEFAULT ON). `AddSourceToPool` with no permanent (a
hasted cast from hand, a same-plan rock credit) priced a Faeburrow Elder / Bloom Tender at the board's colours
only; it will be one of "the permanents you control" when it taps, so its own colours now count.

**The partial widen** (`MTG_DOMAIN_WIDEN_PARTIAL` + `MTG_DOMAIN_WIDEN_ORDER`, DEFAULT ON). The triage blamed the
all-or-nothing `SubsetWidensDomainBy` credit, and that was half of it. The FIRST cut was earlier: neither total-mana
bound at the odometer (`ManaPruneBound`'s extra credit, `BuildManaGateIndex`'s per-action gain) had a widen term,
so FiveColour s1100001 gi0 T3 {Cosmic Spider-Man, Deathrite} = 6 died against a bound of 5 before `consider()` saw
it. Four pieces, each needed:
1. **Bounds:** `widen_bound` = live x |union of the cands' new colours| on the scalar bound; `WidenBoundGain`
   (live x the action's new colours) per gate term. Both are upper bounds.
2. **Partial credit:** when the all-widener credit is refused (the payoff is itself a widener, so the whole subset
   would have to be prepaid), try every proper widener subset W. The t domain sources that tap AFTER the widener
   leave the pool before W is priced -- they cannot pay for it and then tap again widened -- and only they earn the
   widen (a first version without this hold-back took the census's PHANTOM count from 9 to 111). A necessary
   condition on top: the rest of the plan's pips in colours the widened domain cannot make come out of the held
   pool too (gi11 T4: lands pay Oko, the domain sources then make G/W/U and Hellkite's {B}{R} is stranded).
3. **Order:** the executor's payable-order walk (apply site, only for an order no other producer admits) treats
   a widener as a producer: the domain sources are held back until a cast needs them. Without it the admitted
   plans dropped the widener (APPLY-DROP-ORDER 9 -> 0 on the 19-game census).
4. The all-widener credit is unchanged (byte-identical wherever it fires).

19-game FiveColour census (seed 1100001): ENUM-GAP 6 -> 0, PHANTOM 9 -> 17. The PHANTOMs that remain are single
casts or one-widener subsets, which the partial path never touches -- the pre-existing over-admission shapes
(gi8's lone Jared, 7 flags, is identical in base). One new APPLY-DROP (gi6 T4, Mana Cannons + Jared, 7 flags) is
payer-level: the plan is payable but the per-cast payer taps Faeburrow for Cannons before the widen; the line-level
payment script (`MTG_REAL_PAY_SCRIPT`) is the mechanism for that class.

**Held-out A/B** (one pooled batch per measurement, per-job heurarm flags):
- seed 1520001, Fungus 6,000 + FiveColour 2,000 per arm. Crossroads credit: Fungus 0 slower / 7 faster (-7),
  FiveColour byte-identical. Own colours: FiveColour 1 slower (gi1178 T4 -> T5: a turn-1 fetch reshuffles the
  library; T4 on both arms at d8 b0, T5 on both under the non-clairvoyant search), Fungus byte-identical.
- seed 1530001, FiveColour 5,000 per arm. Own colours: 0 slower / 1 faster. Partial widen + order: 3 slower /
  7 faster (-4), units +3.5%. All of them: 3 slower / 8 faster (-5), units +4.0%. The three slower games all
  recover at d8 b0 (gi402 T4, gi174 T5, gi3068 T5 on both arms; gi174 is T6 on both non-clairvoyant).
Only FiveColour holds a domain source (Faeburrow Elder, Bloom Tender), so the two domain levers move no other deck.

Tiers: smoke and regression accepted for Fungus / FiveColour / FiveColour 2HG only (every other deck byte-identical
on smoke; Snow d0/d3/d5 and PD d0 were already red on origin with the same digests). Regression searched slower
games: FiveColour d3 s2002 gi8 and 2HG d5 gi58 recover at d8 b0. **Open, not recovered:** Fungus d5 s3003 gi148
(T4 -> T5, the Crossroads credit) is T4 -> T5 at d8 b0 too, and T5 on both arms under the non-clairvoyant search,
so it passes the bar as clairvoyance (base's T4 needs the T4 Ancient Tomb draw). It is still a signal worth
chasing: the credited T3 menu is a strict superset (30 -> 36 plans, base's Wild Growth + Myconid line included), yet
the unlimited clairvoyant search picks Wild Growth + Thallid, so something between the menu and the search's
evaluation of the new plans is not sound. **Resolved 2026-10-31** (13.7, `MTG_PO_LAND_AURA_HOLD`): the exact order walk ignored Wild Growth's bonus and permuted the hasted Myconid first; gi148 is T4 again. The same T3 also holds one remaining Crossroads gap: {Wild Growth,
Undercellar Myconid, Utopia Mycon}, where the hasted Myconid's own tap pays the Mycon, is still kept off by a gate.

**Deferred: the land-Aura + producer order** (that remaining gap, mechanism traced 2026-10-31). Board Forest +
Hickory Woodlot (pool 3) under Crossroads; {Wild Growth -> Forest, Undercellar Myconid, Utopia Mycon} = 5. The flat
gate credits the hasted Myconid (rock rule, +1) but never the land Aura (its bonus is a sequencing question, priced
only by `SubsetPayableSequential`), so it reads 4 < 5 and falls to the walk. The walk pays the CastOrderRank order
Wild Growth, Mycon, Myconid and is one {G} short; the only payable order is Wild Growth, Myconid, Mycon (the Aura
before its host taps, the producer before the cast it funds). None of the walk's retries produces it: the payable
order at site 1 admits makers / reveal casts only, the untapper hoist is human-play only. A retry ordered "land
Auras, then rock_mana producers, then the rest" would admit it -- but the executor must realise the same order,
and its exact order walk (`ExactOrderWalkImpl`) does not model a land Aura's bonus either, so every permutation
projects unpayable and the given (failing) order is kept: admitting the subset alone would trade this ENUM-GAP for
an APPLY-DROP. The lockstep fix is the Aura bonus in BOTH walks (the next tap of an untapped host after the Aura
resolves adds `land_aura_extra_mana`), then the producer domain at site 1 for the enumerator's retry. **Half done 2026-10-31:** both walks now hold the Aura's host and credit its bonus (13.7, `MTG_PO_LAND_AURA_HOLD`); the producer domain at site 1 for the enumerator's retry is still owed.

### 13.7 The ramp-filter {C} feed (2026-10-31)

**`MTG_RAMP_FILTER_C_FEED`** (DEFAULT ON). A fed ramp filter (Izzet Signet, Ferrous Lake, Skycloud Expanse, Mossfire
Valley: "{1}, {T}: add two") is credited "+1 wild", which assumes the unit that feeds its {1} was coloured. A {C}
feeds it just as well and comes back as two colours, so up to one {C} per filter -- the untapped ones on the board
plus the ones the subset casts -- is relabelled as wild that can still pay a {C} pip (`wild_c`). The total is
unchanged; only colour widens. NivMizzetCombo s1100028 gi27 T6: Generator Servant's {C}{C}, one cracked into Izzet
Signet for {U}{R}, pays Niv-Mizzet, Parun's six pips + Brainstorm (7 coloured off a pool the model read as 6 + {C}{C});
ENUM-GAP 2 -> 0, no new drops. Unit test `test_ramp_filter_c_feed.cpp`.

Held-out A/B, seed 1540001, every deck holding a ramp filter: TreasureHunt 4,000, Breaching 4,000, NivMizzetCombo
1,000, Bruna 600, Hinata2 400 per arm. Niv 1 slower / 3 faster (-2), units +0.7%; every other deck 0 / 0. The
slower game (Niv gi665, T3 -> T4) is T3 on both arms at d8 b0 (identical digest).

**`MTG_PO_LAND_AURA_HOLD`** (DEFAULT ON) -- the 13.6 open item, Fungus regression d5 seed 3003 gi148 (T4 -> T5 at
d5 and at d8 b0 once the Crossroads haste credit landed). A hasted creature carrying the rock stamp makes a line
"producer only", so the apply site runs the exact payable-order walk. That walk drew from the flat pool and never
saw a land Aura's bonus, so it judged the working order {Wild Growth, Undercellar Myconid} unpayable and permuted
the hasted Myconid first; the Myconid then tapped itself for Wild Growth and missed the attack the T4 kill needs.
Now the walk (exact and plain, and the bound it reports) holds back the ONE own untapped land the order's land
Auras name as host (several distinct hosts -> no hold), releases that land's yield plus the bonuses of the Auras
already resolved onto it the first time a cast cannot pay without it, and adds each Aura's bonus once it resolves
while the host is still held. gi148 is T4 again at d5 and at d8 b0 (the pre-credit play). Held-out A/B, seed
1550001, every land-Aura deck: Fungus 6,000, EDF 1,500, Bruna 800, Alchemist 500 per arm -- 0 slower / 0 faster,
units 0.9998x. Unit test `test_land_aura_order_hold.cpp` (fails with the lever off); scenarios
`fungus_wild_growth_host_hold_win.json`, `fungus_wild_growth_host_hold_order.json`.

**`MTG_DOMAIN_OPEN_EXACT`** (DEFAULT ON) -- the FiveColour PHANTOM rise in the eg3 census (37 -> 58). The partial
widen (13.6) let more plans past the odometer, and two old generous reads then admitted plans no payment
realises (s1100021 gi20 T3: lone Jared Carthalion, and {Deathrite Shaman, Jared}, on Overgrown Tomb, a Deathrite
with no graveyard land, Island and Faeburrow Elder -- domain {W,B,G}, nothing makes {R}):
- the colour-exact gate (`ColorFeasibility`) opened a domain source to all five colours. Now each domain source
  counts as one unit of each colour it makes from the board (`dom_src`, `dom_mask`), and per subset
  (`DomainOpenFor`) a colour outside that mask opens only for a cast OTHER than the permanent that brings it
  (a permanent cannot widen the domain for its own cost) -- for pips when another selected cast demands that
  colour, for generic from any selected permanent's colours. A board permanent that makes coloured tokens keeps
  the old all-five opening (a token's colour is not modelled per subset).
- the two widen credits in the subset walk (all-widener and partial) prepaid the wideners off the flat pool, which
  books a domain source's yield as wild, so Jared "paid" his own {R}. Both now also require the wideners to pass
  the colour-exact gate from the board alone (`wideners_colour_ok`; the partial widen's chosen set).
gi20 oracle: PHANTOM 0, MISS 0. Held-out A/B, seed 1560001, FiveColour 5,000 per arm (the only suite deck with a
domain source): 1 slower / 2 faster (net -1), units 0.970x. The slower game, gi3031 T4 -> T5, is T5 on BOTH arms
under MTG_NC_SEARCH=1 at d8 b0 (clear clairvoyance); its oracle run on the new arm has 240 gate rejections checked,
0 payable (no ENUM-GAP, no MISS). Unit test `test_domain_open_exact.cpp` (fails with the lever off).
Still open: gi3031 T4 {Maelstrom Archangel, Bloom Tender, equip Lightning Greaves} PHANTOM (2 flags, with and
without the Windswept Heath drop). Without the Greaves equip the subset is rejected; with it the Greaves haste makes
the freshly cast Bloom Tender a credited producer (the haste-dork credit), and that credit books a DOMAIN dork's
yield without the colour-exact read, so Archangel's {U} -- which only Tender's domain makes, and only once Archangel
itself is on the battlefield -- is counted for Archangel's own cost. Fix shape: the haste-dork credit of a domain
source goes through the same per-subset domain opening (`DomainOpenFor`).
resolves adds `land_aura_extra_mana`), then the producer domain at site 1 for the enumerator's retry.

### 13.8 The EDF payment shapes and Aether Hub's energy (2026-10-31)

The USER: *"Aether hub's energy is one of the troublesome cases we need fixed. (reported by the deck owner) As with
everything else we need to implement it correctly, but with a little cost as possible."* The deck owner's report is
EDF viewer seed 3432 T3: Kitchen untapped, Aether Hub carrying Wild Growth, {E} 1, the line "land=Kitchen; cast Wild
Growth -> Kitchen" -- *"T3 play costs the energy counter when it absolutely should not. We need it T4 to go off."*
The Aura's declared host is held while the Aura pays (the RESERVE THE AURA'S OWN HOST rule), so the Hub pays the
{G}, and the payer took its ANY-COLOUR mode (`[paydbg] cost={G} ok=1 energy 1->0 taps: Aether Hub`) where the Hub's
free {C} plus Wild Growth's own {G} pays the same pip. Pinned by `test/scenarios/edf_s3432_t3_hub_energy_kept.json`
(new scenario key `expect_active_energy`).

**Proposal X was not built.** The re-census (EDF, 20 games, seed 1100001, `logs/mana_oracle/edfx0`) put the 14
ENUM-GAP flags in five shapes, every one a greedy rule in the SHARED payer or a missing term in a bound -- each with
an exact local fix, in code the executor, the rollout, both walks and the oracle already share, so lockstep holds by
construction and nothing is stored on a plan. A bounded every-order payment search per failing untapper subset
would have cost an oracle realisation (hundreds of full applies) on every `ef-walk-fail` of every enumeration -- on
gi2 T3 alone that is ~40 per enumeration -- against the few-percent bar.

| shape | flags | witness | mechanism | fix (lever) |
|---|---|---|---|---|
| an energy-paid colour taken where a free pip exists | (the report) | viewer seed 3432 T3 | `PainlessAuraPipColor` covered painlands only | `MTG_ENERGY_AURA_PIP` |
| the Hub's energy colour counted as cover | 3 | s1100006 gi5 T2, s1100020 gi19 T3: Kitchen + Aether Hub, {Trace of Abundance, Cloud of Faeries} | `ReservedColorsStillCovered` counted the Hub's {U}; Kitchen banked {G}, Cloud's {U} spent the only {E}, Trace's {R/W} had nothing after the untap | `MTG_TAPAHEAD_ENERGY_COVER` |
| a painland's painless {C} never banked ahead of an untap | 1 | s1100003 gi2 T3: Brushland + Wild Growth, Mariposa + Trace, {Cloud, Peregrine Drake} | the cast-site painland bank was human-only; Cloud's untaps refunded one land, the Drake a mana short | `MTG_PAINLAND_TAPAHEAD_CAST_AUTO` |
| the rescue's scalar ceiling omits the untap refund | 4 | s1100006 gi5 T2: Kitchen + Adarkar Wastes, {Trace, Cloud x2} | `ef-scalar` compared four coloured pips with pool 2 + Trace 1 | `MTG_EF_SCALAR_REFUND` (**DEFAULT OFF**, a USER decision -- below) |
| the odometer's refund misses a same-selection land Aura | 6 | s1100009 gi8 T2 {Wild Growth -> Adarkar, Cloud, Displacer}; s1100005 gi4 T3 (five-cast plans) | `EtbUntapBoundCredit` priced the refund off the pre-plan board | `MTG_ETB_BOUND_AURA` |

* **`MTG_ENERGY_AURA_PIP`** (ManaPayment.cpp `PainlessAuraPipColor`). The painland rule, for an energy-paid colour:
  the Aura's fixed-colour units cover every pip of that colour this payment owes, and the {C} the free tap leaves
  over is spent on this payment's generic. For energy a second exact case: the leftover FLOATS while the line that
  binds this payment (`g_line_unpaid_cost` holds it) owes no further pip of that colour. The two realisations then
  differ in one floating unit -- `needed` against {C} -- and the {E}; for the rest of the line the {C} pays every pip
  the coloured unit could (a generic: no pip of that colour is owed) and a {C} pip it could not, and the phase ends
  with the float either way. Not for a line that casts a breakpoint opener (its continuation re-plans on the float),
  not outside a line. The witness's T3 line: energy 1 -> 1, the board otherwise the same.
* **`MTG_TAPAHEAD_ENERGY_COVER`** (SpellEffects.h `ReservedColorsStillCovered`). The ETB-untap tap-ahead commits a
  dual land's banked unit to the pending cast's own pip unless another untapped source "still covers" it. A Hub's
  coloured mode is not cover: committing the dual elsewhere does not just move which land pays the pip, it spends
  the {E}, while the dual's unit pays the same pip for nothing and the Hub's free {C} still pays a generic. Its Aura
  bonus still counts (it rides the free tap).
* **`MTG_PAINLAND_TAPAHEAD_CAST_AUTO`** (SpellEffects.h `EtbUntapTapAheadIntoFloat`). The cast-site half of the
  painland bank in autonomous play too, under its own two guards: `count >= tapped_n + 1` (every tapped land, this
  one included, comes back with the ETB) and the blanket reserve (a painland that makes a pip of the pending cost
  stays out; the coverage widening stays human-only). The bank is then free mana: the land ends the step untapped
  and its painless {C} plus its Aura's bonus float.
* **`MTG_EF_SCALAR_REFUND`** (TurnSolver.cpp `exec_feas_rescues`; **DEFAULT OFF**). The ceiling already added the same-subset land
  Aura's bonus as "the one term that genuinely ADDS total mana"; the untap refund is the other -- each same-subset
  untapper's `EtbUntapLandsCredit` plus the subset's N largest land-Aura bonuses. Computed only when the ceiling
  would reject; an upper bound, so only the walk decides.
* **`MTG_ETB_BOUND_AURA`** (TurnSolver.cpp `EtbUntapBoundCredit`, read by `ManaPruneBound` and the selection-exact
  gate). The refund is the board's top-N yields; a land Aura the same selection casts makes a refunded land yield
  more. An Aura sits on one land and an untap re-taps each land once, so the N largest land-Aura bonuses among the
  HAND's cards (a host variant counted once per card) bound the gain; each untapper adds it. An upper bound.

`test/unit/test_edf_pay_rescue.cpp` pins each shape on its witness board and fails with its lever off.

**Census** (EDF, 20 games, seed 1100001; `logs/mana_oracle/edfx{0,1,2}`; play identity oracle on/off byte-identical in
all three):

| build | ENUM-GAP | APPLY-DROP | DOMINATED-OTHER | DOMINATED-LIFE | menu plans |
|---|---|---|---|---|---|
| before (`edfx0`) | 14 | 0 | 89 | 385 | 3,840 |
| all five levers (`edfx1`) | **0** | 0 | 27 | 329 | 4,158 |
| shipped: four, refund OFF (`edfx2`) | **6** | 0 | 27 | 325 | 4,148 |

The six left under the shipped defaults are the refund's: gi5 T2 {Trace of Abundance, Cloud of Faeries x2} on
Adarkar Wastes and on Aether Hub (two hosts each), and gi8 T2 {Wild Growth -> Adarkar, Trace of Abundance, Cloud of
Faeries, Training Grounds} (two Trace hosts). DOMINATED-OTHER is mostly the energy class: 89 -> 27.

**Measurement.** One pooled `mtg --batch` per round, per-job heurarm flags, every deck at its play settings,
`MTG_RSS_CAP_GB=12 MTG_PGO=0`, two threads (`build/scratch/edfx/ab1`, `ab2`). Units are the cost measure; the wall
column of round 2 is contended (another session's batch shared the box) and is quoted only for completeness.

| round | arms | deck | paired | slower | faster | net turns | avg | units | wall |
|---|---|---|---|---|---|---|---|---|---|
| seed 1640001 | all five off -> all five on | EDF | 2,000 | 2 | 15 | -13 | 4.0650 -> 4.0585 | 1.012 | 1.029 |
| seed 1640001 | same | Auras 300, Prevent Damage 300, SelesnyaLifegain 300, Melira Pod 200, Fungus 300, Bruna 100, AccomplishedAlchemist 100 | 1,600 | 0 | 0 | 0 | identical digests | 1.000 | |
| seed 1650001 | all off -> the four DEFAULT-ON levers (shipped) | EDF | 2,000 | 5 | 14 | -9 | 4.0800 -> 4.0755 | 1.052 | (1.066) |
| seed 1650001 | all off -> all five | EDF | 2,000 | 5 | 15 | -10 | 4.0800 -> 4.0750 | 1.052 | |
| seed 1650001 | four -> five (the refund alone) | EDF | 2,000 | 0 | 1 | -1 | | 0.999 | |

Every lever is inert without an Aether Hub (energy), an ETB untapper (the tap-ahead, both bounds) or an untapper and
a land Aura in one subset, so only EDF moves; the seven other decks holding a painland or a land Aura are
byte-identical by digest and units.

The slower games, each at `--depth 8 --budget-ms 0 --ignore-play-profile` on both arms:
* seed 1650001, the shipped set: gi185 4 -> 5, gi340 4 -> 5, gi555 3 -> 4, gi1145 5 -> 6, gi1499 6 -> 7 -- **all recover**
  (T4, T4, T3, T5, T6 on both arms).
* seed 1640001 gi1052 T4 -> T5 (all five, and the shipped four alone): recovers (T4 on both arms).
* seed 1640001 gi552 T3 -> T4 (all five): **does not recover** -- T3 off, T4 on at d8 b0. Bisected by turning one lever
  off at a time: `MTG_EF_SCALAR_REFUND` alone (with it off the game is T3 at d8 b0 and at play settings). The
  mechanism, as far as traced: the refund lets far larger untapper subsets reach the walk, every one of them paid by
  it (18,008 distinct T3 multi-cast plans pushed against 139), and with them the T2 search's per-candidate earliest
  wins (`MTG_DUMP_EWINS=1 MTG_DUMP_EWINS_TURN=2`) read {Shivan Gorge, Wild Growth -> Gorge, Fertile Ground -> Kitchen}
  as a T4 win -- the off arm reads it T3 -- although a T3 root search from that exact board (a scenario rebuilt from
  the game log, every library card in order) finds the kill at every depth 3-8 with the lever on. The search
  memos were ruled out (`MTG_GAME_LINE_CACHE`, `MTG_FS_NOWIN_CACHE`, `MTG_LABEL_NOWIN_CACHE`, `MTG_SOLVE_MEMO`,
  `MTG_EXH_LEAF_MEMO`, the enumeration and payment memos all off: still T4). So it is an in-tree defect that a
  larger exact menu exposes, not a payment the lever gets wrong; the non-clairvoyant check (`MTG_NC_SEARCH=1`, d8
  b0) was started and stopped unfinished after 25 CPU-minutes per arm. **`MTG_EF_SCALAR_REFUND` therefore ships
  DEFAULT OFF -- the USER's call**; it closes the four `ef-scalar` flags of the census and measured 0 slower / 1
  faster on its own on seed 1650001.

**Cost.** EDF units +1.2% (seed 1640001, all five) and +5.2% (seed 1650001, the shipped four); the larger menus are
where it goes (the T2 refund and Aura bounds admit more subsets, and the walk pays them). The oracle's own cost rose
far more (applies 5.1 M -> 19.8 M on the 20-game census, one game's decision alone 0.56 h): more plans on the menu
are more plans to audit -- an oracle-only cost, play identity on/off unchanged.

**The witness, after.** `edf_s3432_t3_hub_energy_kept.json` passes ({E} 1 at the end of T3, `[paydbg] cost={G} ok=1
energy 1->1 taps: Aether Hub` -- the {C} face). On the T4 board that line leaves (Kitchen + Wild Growth, Aether Hub +
Wild Growth, Kitchen; Cloud of Faeries, Eldrazi Displacer, Peregrine Drake, a land in hand; rebuilt as a scenario,
`build/scratch/edfx/t4_e{0,1}.json`) the turn goes off with the {E} kept -- Conservatory, Cloud, Drake, Displacer and
the blink loop draws the library -- and stalls after the Displacer with it spent (no finisher is in that rebuilt hand,
so neither wins by T4; the go-off is the difference). Unit suite 2,244 / 2,244; scenarios 350 / 350.

**Open.**
* `MTG_EF_SCALAR_REFUND` (DEFAULT OFF): the USER's call, and the gi552 in-tree defect it exposes (above) is worth
  chasing on its own -- a larger exact menu should never lose a line at an unlimited budget.
* Proposal X as the general backstop: not needed for any flag on this census; the next census that shows a walk
  failure no payer rule explains is where it would start.

### 13.9 Burn: the sac-land pin indexed twins, and a suicide counted as a paying order (2026-11-03, branch `wip/mc-burn`)

The mana-clean gate's pass1 on burn (100,000 games, `logs/mana_clean/pass1-burn/report.md`) had 2,770 ISSUE flags:
APPLY-DROP 2,270 (24 signatures), ORACLE-GAP 498 (31), APPLY-DROP-ORDER 2. Every APPLY-DROP and ORACLE-GAP vanished
with `MTG_SAC_AXIS=0` -- they were one defect, in the searched sacrifice-a-land pin.

**The pin indexed the raw ranking, i.e. twins (engine defect).** `Plan::sac_pins` entry k takes `ranked[k]` of
`SacrificeLandCandidates` at resolution (`PerformSacrificeLandCost`, `src/core/SpellEffects.h`), and that ranking is
tapped-first in battlefield order. On burn's all-Mountain board the axis (width = distinct names + 1 = 2) emits a
pin-1 variant per Shard Volley, and pin 1 after a one-Mountain payment names the first UNTAPPED Mountain: Shard Volley
eats the land the next spell needed while the tapped one survives. Seed 30000241 T3 `{land, Volley, Volley, Light Up
the Stage}` on three Mountains: the second Volley tapped #40 and sacrificed untapped #36, so Light Up the Stage
({R} spectacle) had no mana (APPLY-DROP x371, and every Skullcrack / Searing Blaze / Searing Blood / Eidolon drop
signature is the same shape). The ORACLE-GAPs (seed 30000945 T6 x153, and the `{Volley, Light Up}` ones whose
continuations differ) are the other face of it: the outcome depended on WHICH identical Mountain a payment tapped, so
the oracle's source classes (identical untapped copies fold, lowest number taps first) could not reproduce the
engine's board -- there it was the new Mountain kept vs an old one.

**Fix (lossless).** A pin past the front now indexes the ranking's distinct land CLASSES (`SacLandClassRanking`;
twins = `SacLandTwins`: same card, controller, token-ness, no counters / damage / riders / attachments, not a creature,
and the same age unless the land can never be one), each class represented by its first TAPPED copy, else its first
copy. Sacrificing the tapped copy of two otherwise identical lands is never worse (sound dominance), and a different
land class is now one pin step away instead of behind its twins -- on `[Mountain(T), Mountain, Mountain, Forest]` the
Forest sat at raw rank 3, past the axis width, and was unreachable. pin <= 0 is untouched (the provider's front), and
the human chooser is untouched. On burn every pin variant now resolves to the base plan's state and is skipped by the
axis-variant post-apply dedup (`PlanDupSkippable`). Shared by every world (executor cast path, `apply_one`, the
oracle's realisations), so lockstep holds by construction. Other sac-land decks: Creature Giving (Crop Rotation).

**APPLY-DROP-ORDER x2 was a suicide (oracle gap).** Seed 30017261 T5, two Eidolons, life 8, four Mountains: Searing
Blaze (targeting us -- the goldfish has no creature) then Light Up the Stage is a Mountain short. The claimed order,
Light Up first and Blaze off the staged Mountain, takes us 8 -> 4 -> 0 through the Eidolon triggers, a plan the search
skips (TurnSolver "A plan that kills the active player"). `Outcome::Clean()` now also requires the active player
alive, so no claim rests on a line that dies; the repro reads PHANTOM.

**Validation.** Unit `test_sac_land_class_pin.cpp` (4 cases, incl. applying every menu variant of the 241 board --
each casts Light Up the Stage); scenario `burn_volley_volley_lightup_sac_tapped.json` (pins the line and the T3 kill;
it is not discriminating -- the base variant always won -- the unit test is). Unit 2,433 / 2,433, scenarios 392 / 392.
Every report repro is clean. Fresh slice gi 100000-109999 (`MTG_MANA_ORACLE=2`): **0 ISSUE flags** (origin's binary
on the same games: APPLY-DROP 220, ORACLE-GAP 45, DOMINATED-LAND 2,253, CONTINUATION-DIFFERS 192, DROP-COVERED 7,444
-> 0 / 0 / 0 / 0 / 894); play byte-identical with the oracle off. Held-out A/B (seeds 41000000+gi, gi 0-4999, play
settings): 13 digests moved, **0 games slower, 0 faster, net 0 turns** (mean 4.2910 both), CPU 319 s -> 305 s.

### 13.10 The Vampires mana-clean findings (2026-11-03, branch `wip/mc-vampires`)

The first mana-clean gate pass on Vampires (`logs/mana_clean/pass1-vampires`, 100,000 games, src `7ec55bb`) failed on
ORACLE-GAP 7,208 and APPLY-DROP-ORDER 4 (DOMINATED-LIFE 41,682 is disclosed, USER 2026-11-03). Two mechanisms, one on
each side of the invariant.

**ORACLE-GAP: the oracle folded the plan's own Blood Estate into its twin (oracle incompleteness, not an engine
miss).** The dominant shape is `{... cast:Markov Baron (or Falkenrath Pit Fighter) + act:Voldaren Estate(x1)}` with
two Voldaren Estates: the engine pays the cast with Estate #58 (its {C}, or a coloured face for 1 life) and makes the
Blood with #57. Item 11 already keys the plan's own source (#57) apart in the payment TABLE, but two places fold a
payment's RESULT by keys that do not say which copy of identical permanents is tapped: the table's one-option-per-
result dedupe (`PayEnum::PayAndRecord`, the engine's mid-turn key folds a permanent by name) and the frontier's Pareto
prune (`Resources`, class counts). The table reaches #57 first (it is its own class), so "#57 taps for {C}" stood for
"#58 taps for {C}"; the Blood then found #57 tapped and dropped, and the engine's line was never reached ("Voldaren
Estate#58 tapped by engine only", and with the coloured face "life engine 16 oracle 17"). Fix (`ManaOracle.cpp`):
a plan source's tapped flag is part of both identities (`Resources` keys it `|plan#N`, `PayAndRecord` folds it into
the result key). The engine's payment is not worse here: Estate #58 untapped (a second Blood source) and Secluded
Courtyard untapped (painless coloured creature mana) are incomparable, so the oracle now reproduces the engine and
flags nothing. Oracle-only: play is byte-identical with the oracle on and off.

**APPLY-DROP-ORDER: a Markov Baron short of the bodies its books counted (engine miss, fixed in the order).**
Seed 30001869 T3, `{Swamp, Markov Baron, Markov Baron, Stromkirk Captain}` on four creatures and three lands. Subset
rule 7 (`SubsetConvokeBodiesInvalid`) books the selection's convoke casts against every body it can count, the
Captain included, but the dependency order only placed Baron B after the arrival its own arm names (Baron A). Baron A
took three board bodies, Baron B found Baron A and Pit Fighter but not its third body, paid {1} off a land, and the
Captain's {1}{B}{R} dropped; Baron A > Captain > Baron B convokes Baron B entirely. Fix
(`ManaPayment.cpp ApplyConvokeShortfallOrder`, `MTG_CONVOKE_SHORTFALL_ORDER`, DEFAULT ON, a heurarm slot): walk the
order counting the bodies there are at each cast-time convoke cast per colour class (as `ConvokeCastTaps` takes them);
while one is short, move the first short one to the earliest later place that minimises the order's total shortfall,
if that strictly lowers it, and never ahead of an arrival a plan-body arm names. Structural, the attach / arrival
dependency precedent: it realises the line rule 7 booked and picks between no lines. A convoke cast placed later only
finds more bodies. Called at the end of both forms of `ApplyPayableCastOrder` (both apply worlds, the enumerator's
site-1 walk). Only Markov Baron taps at the cast, so only Vampires can move.

Pinned by `test/unit/test_vampires_sweep.cpp` (the order on the witness board, the plan casting all three, and
`CoversEngineOutcome` on the seed 30000337 T4 board for every plan where the engine taps the twin Estate).

**Oracle census, Vampires, 2,000 games** (gate games 0-999 and the fresh block 100,000-100,999; before = PGO `7ec55bb`,
after = this branch; `build/scratch` run, play identity oracle on/off byte-identical on the after binary):

| class | before | after (oracle + convoke) | after (+ twin tap binding) |
|---|---|---|---|
| ORACLE-GAP | 134 | 6 | **0** |
| APPLY-DROP-ORDER | 0 | 0 | 0 |
| PHANTOM | 56 | 39 | 39 |
| CONTINUATION-DIFFERS | 707 | 193 | 195 |
| DROP-COVERED | 557 | 484 | 458 |
| UNMODELED (context) | 82 | 82 | 86 |
| DOMINATED-LIFE (disclosed) | 862 | 862 | 861 |

The six repro commands of the pass-1 report (the four APPLY-DROP-ORDER seeds and the two ORACLE-GAP seeds) are clean
of both classes after; the oracle fix alone (`MTG_CONVOKE_SHORTFALL_ORDER=0`) already clears the two ORACLE-GAP seeds.

**The continuation's own Blood: a misindexed memo replay (engine defect, fixed in the key).** The 6 flags left after
the two fixes above (seed 30000611 T3 `{Swamp, Baron, Pit Fighter}`, seed 30100014 T4; about 54 of pass 1's 7,208)
are breakpoint VARIANTS (`bp_choice = 0`): the plan names no Blood, its continuation (candidate 0 of the list
derived at the breakpoint) makes one. That list is memoised (the bp-enum cache), keyed by the canonical sim key plus
`FsOrderSig`. The canonical key folds each permanent's state as a multiset and `FsOrderSig` its (name, number) per
position, so nothing binds a twin's TAPPED flag to its number: "#57 tapped, #58 untapped" (the engine paid
Pit Fighter's {R} with #57) and "#58 tapped, #57 untapped" (the oracle's realisation paying it with #58) were one
key. The list derived on the first holds "Blood with #58"; served on the second, the source is tapped and the
activation drops. Confirmed three ways: `MTG_NO_BP_ENUM_CACHE=1` clears both seeds' ORACLE-GAPs; a unit test on the
seed 30000611 T3 board (`test_vampires_sweep.cpp`, "a memoised continuation's Blood source is not served to its
tapped twin") fails with the fix off; and with it on, every such plan is covered by an oracle realisation. The same
hole is in every canonical-key consumer `FsOrderSig` guards (FSL win lines, dedup drops, the rollout TT): any answer
naming a {T} source by number could be replayed on its tapped twin, in play as much as in the oracle.

Fix (`TurnSolver.cpp FsOrderSig`, `MTG_ORDER_SIG_TAP_BIND`, DEFAULT ON, heurarm slot): a permanent with a {T}-cost
activation a plan names by its number (PermAbilityTaps' modes, Blood's crack, the token / put-from-hand {T}
abilities, a tapping land search) that has a same-name twin on the battlefield folds its tapped flag at its
position. Without such a twin the signature is exactly the old one, so only boards holding two copies of such a
permanent can key differently (in the shipping decks: Vampires' Voldaren Estate and Bloodline Keeper, EDF's
Conservatory / Mariposa / Kitchen, Snow's Scrying Sheets / Frost Augur, the Myriad / Evolving Wilds / Terramorphic
decks, Blighted Steppe, Sliver Hive, Niv-Mizzet, Staff of Domination, Speaker of the Heavens). It only ever splits a
key, so a memo answers a strict subset of what it answered, each on a board where its numbers mean the same thing.

After all three fixes the 2,000-game census reads **ORACLE-GAP 0** and no issue class (the MISS count is
DOMINATED-LIFE, disclosed). The six flags became UNMODELED, joining 80 pre-existing flags of one shape: "a mana
choice the oracle does not drive: Voldaren Estate" -- the continuation's own Blood source taps as its activation's
{T}, and the oracle's unscripted-tap check (which exempts only the PLAN's sources) reads it as an undriven mana tap.
UNMODELED is context, not a gate class; making the check exempt a continuation's activation sources would turn
those 86 into verdicts and is the natural next oracle step.

**The continuation's activation sources are exempt; twinned numbered {T} sources are keyed apart (oracle only).**
That step is taken. `ApplyPermAbility` records the source of every {T}-cost activation it resolves into a
thread-local sink (`g_perm_ability_tap_target` / `_sink`, `SpellEffects.h`; null outside the oracle and
pointer-matched to the oracle's own copy of the board, so play and nested searches never record), and Realize's
unscripted-tap check skips those sources: the Estate a continuation taps for its Blood is that activation's cost,
not a mana choice. The exemption exposed one ORACLE-GAP shape (seed 30000159 T5, 2 flags on the 2,000): the plan
`{land=Voldaren Estate + Pit Fighter + Aether Vial + act:Bloodline Keeper + tapfortokenpay}` -- the engine pays
Vial with Estate #57 and its continuation makes a Blood with #59, but the frontier prune (`Resources`, which drops
a land's age) folded "#57 tapped" into "#59 tapped", and the board where #59 paid the Vial continued with no Blood
(a positional `bp_choice` indexes a list derived on the board at hand). Fix (`ManaOracle.cpp KeyedApart`): while a
realisation runs, every permanent that `TurnSolver::IsTwinnedNumberedTapSource` names -- a {T}-cost activation a
plan or continuation names by number, with a same-name twin of the same controller -- is keyed apart in the payment
table, the result dedupe and the prune, judged on the board at hand (a land drop makes the twin mid-plan). It is
the same predicate `FsOrderSig`'s binding uses, kept general (any twinned numbered {T} source, not Vampires') so
other decks' twins can share it. A side effect pinned in the seed 30000611 unit test: with the binding OFF the
oracle now reaches the engine's line through the payment whose memo agrees, so the key hole is pinned directly on
`MidTurnStateKeyOf` instead. The test's covering realisation must also be scripted (`CoversEngineOutcome(...,
scripted_only)`).

Census, Vampires, `MTG_MANA_ORACLE=2`, the same 2,000 games plus a fresh 5,000 (gi 110,000+, seed 30110000+):

| class | 2k before (`1d8ceae3c`) | 2k exemption only | 2k final | 5k before | 5k exemption only | 5k final |
|---|---|---|---|---|---|---|
| ORACLE-GAP | 0 | 2 | **0** | 0 | 0 | **0** |
| UNMODELED (context) | 86 | 0 | **0** | 133 | 14 | **14** |
| PHANTOM | 39 | 39 | 39 | 278 | 284 | 284 |
| CONTINUATION-DIFFERS | 195 | 195 | 195 | 510 | 510 | 509 |
| DROP-COVERED | 458 | 458 | 458 | 638 | 638 | 638 |
| DOMINATED-LAND | 40 | 40 | 40 | 218 | 218 | 218 |
| DOMINATED-LIFE (disclosed) | 861 | 861 | 861 | 1762 | 1762 | 1762 |

No issue class. The one CONTINUATION-DIFFERS that went (seed 30114200 T5, a plan dropping a second Voldaren
Estate) is the mid-plan-twin case `KeyedApart` covers. The 14 UNMODELED left are one older shape, all seed
30111212 T5: "engine dropped [cast Bloodline Keeper (activation)]; ... too many actions to try every order" (the
more-than-5-movable-actions limit of `OtherOrders`), not the exemption's. Play: 0 of 400 job digests changed against
`1d8ceae3c` on all 7,000 games (oracle on, units ratio 1.0000); oracle on vs off on the final binary: 0 of 400 digests changed, units identical.

Play under the binding: held-out A/B (`MTG_ORDER_SIG_TAP_BIND` per-job off/on, one pooled batch, seed 1720001, every
deck at its play settings) over every deck holding two copies of such a permanent -- Vampires 4,000 + 2HG 2,000, EDF
2,000, Colossus Hammer 2,000, SelesnyaLifegain 2,000, slivers_vial 2,000, Landfall 1,000, LandfallElementals 1,000,
Snow 600, NivMizzetCombo 600, AccomplishedAlchemist 600, Unpredictable Cyclone 200 (18,000 paired games): **every job
byte-identical** (0 of 360 digests changed), units at most +0.005% (vampires2hg; the split keys re-derive a few
lists). Smoke identical to GT apart from the four Merfolk keys that drift on the unchanged base too.

**Play.** Held-out A/B (`MTG_CONVOKE_SHORTFALL_ORDER` per-job off/on, one pooled batch, seed 1710001, Vampires at its
play settings): Standard 6,000 paired games and 2HG 2,000: **0 slower, 0 faster**, average win turn identical (4.2550,
2HG 4.9370); 2 of 160 jobs changed play (digests), units +0.0004%.

### 13.11 Auras: a plain land's age on the end board, and a continuation that owed nothing (2026-11-03, branch `wip/mc-auras`)

The mana-clean gate's pass1 on Auras (98,000 games, `logs/mana_clean/pass1-auras-r2/report.md`, src `7ec55bb`) had
11,713 ISSUE flags: ORACLE-GAP 11,688 and APPLY-DROP 25. Two mechanisms, one on each side.

**ORACLE-GAP: the end board told a new Horizon Canopy from an old one (oracle defect).** Every kept flag reads
"Horizon Canopy#31 only in engine; Horizon Canopy#32 only in oracle", often with "life engine 18 oracle 17". Seed
30000073 T3: Brushland and Canopy #31 in play, Canopy #32 the land drop, Slippery Bogle, and the searched Canopy dig
(`Plan::dig_choice` 1). The engine pays the Bogle off #31 and the dig sacrifices #32 (the first untapped copy), its
{1} paid with Brushland's painless {C}. The oracle's frontier prune keeps only one of "#31 tapped, #32 up" and "#32
tapped, #31 up" -- the resources forget a plain land's age (`ResClassKey`) and the mid-plan core clears the tapped
flags -- so its dig sacrificed #31, and its end board (one tapped Canopy, Brushland tapped for {C}, 18 life) differed
from the engine's only in whether the surviving Canopy entered this turn. The oracle did hold that board; the
"nearest" it printed was a Brushland {G} variant a life lower. The end-board core (`CoreKey`, canonical) now applies
`ResClassKey`'s rule too: a land that can never be a creature (`AgeIsInert`) forgets `entered_this_turn`. Sound for
the same reason the resources already forget it: the marker matters to summoning sickness only (CR 302.6), and the
landfall condition reads the player's history bit and land-drop count, which stay in the key. The engine was right;
nothing in play changes. Pinned by "the surviving copy's age does not tell two end boards apart".

**APPLY-DROP: a breakpoint continuation's cast section bound no line (engine defect).** All 25 flags (11
signatures) are one shape: Kor Spiritdancer in play, an Aura cast, Kor's draw re-solves the rest of the main
(`apply_continuation_plan`), and the continuation's first cast pays its generic pip with the colour its second
cast needs. Seed 30013411 T4: Forest, Plains, Branchloft Pathway; the land drop a Pathway as Boulderloft {W}; the
plan casts Rancor on Kor and the draw re-solves to {Light-Paws, Rancor}. Light-Paws' {1}{W} took Plains + the
Branchloft's {G}, the board's last green, so the continuation's Rancor was dropped -- Plains + Boulderloft pay it
and leave the {G}. The Pathway is incidental: seeds 30022178 / 30054013 strand a {W} the same way with a drawn
Forest. The payer HAS the rule for this (`MTG_LINE_SPARE_GENERIC_FIRST` with `MTG_LINE_SPARE_TIGHT`: a colour-short
line's generic pip taps a spare source first), but every line-aware payer rule reads the line hold
(`g_line_unpaid_cost`), and only a TOP-LEVEL apply binds it: a continuation applied inside the plan saw the outer
line's remainder (zero once the outer Rancor was paid) and so owed nothing. `ManaPayment.cpp` even said so ("a hold
left over from an outer line, e.g. during a breakpoint continuation whose casts are not in it"). PlanTraits already
treat a continuation as its own mini-plan; the line hold now does too.

* **Fix.** `apply_continuation_plan` arms `cont_line_armed`; the `apply_plan_actions` call it wraps binds, right
  after its `CastSectionUnlock` opens, `g_line_unpaid_cost` = the outer line's remainder + `LineCastCostTotal` of
  the continuation's actions and `g_line_opens_unseen` = either line's opener, for the rest of that cast section.
  Every one of the twelve continuation sites goes through it.
* **Lockstep.** The executor replays a committed continuation from its records, which can hold the planned tail one
  nesting level down, so the records alone cannot rebuild the line. Each recorded cast now carries the hold its
  payment began under (`Action::rec_line_hold`, captured at `apply_one`'s entry), and `replay_recorded` installs it at
  that cast and keeps it through the rest of the section, closing it where the rollout's section closes (a dig, the
  end of the records); the executor's own cast decrements it exactly as the rollout's did. The three appliers that
  re-derive a continuation instead (`resolve_draw_breakpoint`, the pod chain, site 9) bind it with
  `ExecBindContinuationLine` right before their cast loops.
* Pinned by "a breakpoint continuation's first cast keeps the colour its later cast needs (Auras Kor draw)".

**Validation.** Every report repro is clean: the eleven ORACLE-GAP games (seeds 30000004, 73, 121, 458, 652, 1545,
2507, 2631, 3743, 3829, 9279) and the seven APPLY-DROP games (13411, 22178, 54013, 59069, 71422, 74169, 87009) read
no ISSUE class (DOMINATED-LIFE stays, disclosed). Fresh slice gi 100000-103999 (seed 30000000+gi,
`MTG_MANA_ORACLE=2`): the gate's binary (src `7ec55bb`) ORACLE-GAP 418, APPLY-DROP 6, CONTINUATION-DIFFERS 20,
DOMINATED-LIFE 6,724; this branch **0 / 0 / 0**, DOMINATED-LIFE 6,724.

Play identity on that slice with the oracle off: byte-identical (40/40 job digests).

**Play movement.** The ORACLE-GAP fix is oracle-only. The line binding moves play wherever a continuation's first
cast used to strand its second. Held-out A/B (seed 52000000+gi, every suite deck at its shipped play settings,
300 games each and Auras 3,000; 15,300 paired games; base = the same binary with the binding off): **0 slower, 1
faster** (Unpredictable Cyclone gi296 T7 -> T6), 6 jobs' play digests moved (Colossus Hammer 1, NivMizzetCombo 3,
Cyclone 2), Auras 0 of 3,000; work +0.027% (Cyclone +0.07%, Niv +0.09%, every other deck within 0.05%). No deck
has a positive net. The Colossus Hammer digest (gi274, T3 both arms) is the mechanism itself: after Steelshaper's
Gift tutors the Hammer, the continuation {Colossus Hammer, Magnetic Theft} now casts both (the Hammer's {1} no
longer takes the Mountain's {R}). Smoke (THREADS=12): 12 rows FAIL. Seven are this change -- cyclone d3/d5,
colossus2hg d5, edf d5, niv d5, niv2hg d5 with the same average and moved play, and colossus d3/d5 with one game
FASTER, gi15 T4 -> T3 (the same Gift -> {Hammer, Magnetic Theft} continuation, attaching the Hammer to Lizard Blades
for the T3 kill) -- each confirmed by re-running the row with the binding off, which reproduces its ground truth.
The other five (merfolk d0/d3/d5, merfolk2hg d3; 294 d0 games moved) reproduce identically with the binding off:
Merfolk's committed smoke GT does not match origin's own src, independent of this change. No searched game is
slower, so there is nothing to recover at d8 b0. Unit 2,433 / 2,433, scenarios 391 / 391.
### 13.7 Rocks that fund each other, realised (2026-10-31): the feed-blind order walk, and the Servant's haste

USER 2026-10-31: *"There is nothing wrong with Sol Ring going first or paying for the Signet."* -- the
APPLY-DROP-ORDER plans 13.5's Fix 3 admitted (a Signet ordered ahead of the Sol Ring that pays for it) are an
engine defect, not a cast-order question.

**The mechanism (which step puts the Signet first, and why nothing reorders it).** Niv has no USER cast order
(no `CastOrderRank` override; the `MTG_NIV_ORDER_*` levers order Ponder/Preordain LOOKS, not casts), so the
generic tiers apply: Sol Ring and Izzet Signet are both tier 5 (`mana_rock`, non-creature), the comparator
keeps plan (hand) order, and a Signet listed first is cast first. The step that should then rescue an order
that cannot pay is the payable-order decider (`PayableCastOrderCore`, apply site: a rock's `rock_mana` puts the
order in its producer domain). It never fired, because its order walks (`ExactOrderWalkImpl`,
`FirstUnpayablePosImpl`, both through `CreditCastOutput`) credited the cast Signet's `rock_mana` -- +1 wild, its
NET gain once fed -- right after its own payment, whether or not anything was left to pay its {1}. On two lands
"Signet, Sol Ring" spent both lands on the Signet and then paid Sol Ring out of that phantom, so the walk read
the order as payable and kept it; both apply worlds then cast the Signet and dropped Sol Ring. Witnesses:
census s1100014 gi13 T3 {Signet, Signet, Sol Ring} on Island + Sandstone Needle (the oracle's paying order:
Signet > Sol Ring > Signet), and the cast-order draft's probe p2 (Island + an Island drop, Signet ahead of Sol
Ring in hand: `paid=0` for Sol Ring at d1/d3/d5; the swapped hand casts both).

**Fix 1 (`MTG_RAMP_FILTER_FEED_ORDER`, DEFAULT ON).** The walks HOLD a ramp-filter rock's credit until the pool
holds a unit to feed it (a later cast's output -- Sol Ring's {C}{C} -- included; a fed filter feeds the next).
The Signet-first order then projects unpayable and the decider's nearest payable permutation (Sol Ring first)
is what the executor and the rollout realise, since both order through `ApplyPayableCastOrder`. It only removes
the phantom: whenever a unit is left after the filter's own payment the credit lands at once, as before, and the
aggregate bound keeps the unconditional credit (it must stay a necessary condition). Probe p2: Sol Ring then
the Signet, both paid. General, not Niv's: it moves any deck that casts a ramp-filter rock in a line (in the
suite only Niv -- Hinata2's Signet is in its sideboard).

**Fix 2 (`MTG_SERVANT_HASTE_KEEP`, DEFAULT ON).** The same lines exposed a payer hole the cast-order draft traced
(probe p5; the USER hit it saving references): Generator Servant's {C}{C} are haste-tagged, and the payer drains
floating mana before it taps, so a NONCREATURE cast ahead of the creature spent the haste -- "crack the Servant,
Signet off two Islands, hasty Firemind attacks for 4" never won, even at `--budget-ms 0`. Both apply worlds now
seed `g_line_haste_takers` beside the line hold (the creature casts that would take a tagged unit) and both cast
sites count a creature down as it is paid; a noncreature payment that would spend a tagged unit those creatures
could take is tried on scratch boards with the units set aside (first as many as the creatures take, then all of
them) and is taken only if it pays AND the rest of the line stays payable on the board it leaves -- the
`RampFeedRouteRepay` shape, in the shared payer, so the viewer's auto-tap gets it too. Probe p5 wins T5 (d3 b50
and b0; T-none before).

**The USER's Niv cast order (`MTG_NIV_CAST_ORDER`, DEFAULT ON)** landed beside them (Sol Ring > Signet > ...; the
order and its rulings are in `analysis-NivMizzetCombo.md`). With it Sol Ring ranks ahead of the Signet outright,
so for Niv the walk fix is a backstop; it stays the general fix for every deck on the generic tiers.

**Held out** (seed 1640001, one pooled batch, per-job heurarm arms; branch `wip/niv-rock-order`):

| comparison | n | slower | faster | net | units |
|---|---|---|---|---|---|
| Niv off -> feed (Fix 1) | 2000 | 1 | 0 | +2 | 0.999x |
| Niv feed -> feed + haste (Fix 2) | 2000 | 0 | 4 | -4 | 0.996x |
| Niv feed + haste -> + the USER's order | 2000 | 31 | 34 | -4 | 0.986x |
| Niv off -> all three | 2000 | 29 | 35 | -6 | 0.982x |
| Hinata2 / Dragons / Giants off -> all three | 300 / 500 / 300 | 0 | 0 | 0 | byte-identical |

Every slower game against the bar (`build/scratch/nivorder/d8/run1.log`, each arm at `--depth 8 --budget-ms 0
--ignore-play-profile`): Fix 1's one (gi1045, T5 -> T7) is T4 on both arms at d8 b0. Fix 2 has none. Of the
order's 31, 30 recover at d8 b0 (the same turn on both arms; e.g. gi29/gi45/gi247 T4, gi273/gi306 T5,
gi391/gi1379 T6); gi1950 (T4 -> T5, T4 vs T5 at d8 b0 too) is T5 on BOTH arms under `MTG_NC_SEARCH=1` at d8 b0,
so it is clear clairvoyance (the off arm's T4 needs the draws it cannot know). Every deck nets <= 0 for the
package (Niv -6); Fix 1 alone nets +2 on this seed (its one slower game, which recovers).

**Second held-out seed** (1650001, 2,000 Niv games per arm, single-thread batches `ab2` / `ab3`): Fix 1 alone 3
slower / 2 faster (+1; gi670 T3, gi831 T5, gi1390 T6 -- all three recover at d8 b0, the same turn on both arms);
Fix 1 on top of Fix 2 + the order (the SHIPPED configuration) is **byte-identical** (0 moved of 2,000: with Sol
Ring ranked first the phantom order never arises on Niv); the package against off 43 slower / 46 faster (-2).
Pooled over both seeds: package -8 on 4,000 games; Fix 1 alone +3 on 4,000, every one of its slower games
recovering at d8 b0. Fix 1 is a lossless projection fix (it only stops a walk from promising mana that does not
exist), so its residue on the generic tiers is the budgeted search re-spending, not a lost line.
The package's 43 second-seed slower games against the bar (`build/scratch/nivorder/d8/run2a.log`, `run2b.log`,
`run3a.log`, `run3b.log`, `nc2.log`, `nc887.log`): 40 recover at d8 b0 (gi1659 is T6 on both arms there -- the
off arm's budgeted T5 does not survive unlimited search either). gi774 (T3 -> T4) is T4 on both arms under
`MTG_NC_SEARCH=1` at d8 b0: clear clairvoyance. **gi887 (T4 -> T5) fails the bar**: T5 vs T6 under the
non-clairvoyant search at d8 b0 as well. It is the ORDER's (budgeted: off / feed / feed+haste T4, feed+order and
the package T5; NC d8 b0: feed+haste T5, feed+order T6). Mechanism (game logs `build/scratch/nivorder/g887/`):
on T1, one Island, the USER's draw sub-order casts Preordain before Gitaxian Probe, so the {Preordain, Probe}
plan pays Preordain with the Island and Probe with 2 life, while the generic order (a hand-order tie) cast Probe
for {U} and Preordain on T2. The draws then come in a different order (the order arm sees Niv-Mizzet, the
Firemind on T1), and at T4 the search -- non-clairvoyant, unlimited -- takes Izzet Signet + Tandem Lookout in
main 2 over Signet + Generator Servant, so no Servant crack funds a T5 Firemind. No rules or payment fault: a
search-valuation choice downstream of a different, legal T1. Surfaced to the USER; the order stays ON (net
-4 / package -2 and -6 on the two seeds; 1 of 74 slower games fails the bar).

**Census** (Niv, seed 1100001, the same 19 games on both binaries -- gi7, which runs the oracle for over half an
hour, left out): APPLY-DROP-ORDER **4 -> 0**, APPLY-DROP 37 -> 21, DROP-COVERED 123 -> 87, DOMINATED-HASTE 28 -> 25.
PHANTOM 21 -> 28, all on boards the changed play reaches (s1100014 and s1100006 take a different T2): the s1100014
T3 ones are the pre-existing ENUMERATION twin of the walk's phantom -- a menu plan casting a Signet and a cheap
spell off exactly the Signet's unfed +1 ({Signet, Preordain} on two lands) -- and s1100006 T3's six are
{Preordain, Generator Servant} (+/- Sol Ring) sets of the class base already shows on that seed's T4. Open: `AddSourceToPool`'s ramp_filter branch and
`SequencedRockProduction` credit the +1 whenever the BOARD has a feeder, not whether one survives the plan's own
payments, so those plans reach the menu and their apply drops the spell. The walk's hold is the apply half; the
gate half is the same hold in the subset walkers (13.4 D).

### 13.12 The slivers_vial mana-clean findings (2026-11-03, branch `wip/mc-slivers`)

The first mana-clean gate pass on slivers_vial (`logs/mana_clean/pass1-slivers_vial`, 100,000 games, seed 30000000 +
gi, src `7ec55bb`, `MTG_MANA_ORACLE=2`) failed on ORACLE-GAP 6,055, APPLY-DROP 3,763, ENUM-GAP 59 and
APPLY-DROP-ORDER 26. Four mechanisms; two are oracle incompleteness, one an engine RULES bug, one an enumeration gate.

**ORACLE-GAP (~95%): the frontier prune folded Mutavault into its twin -- the same fold as 13.9.** `{animate
Mutavault #32 + animate #33}`: the engine pays each animation with the land itself (a mana ability during cost
payment), so both end animated and tapped. `AnimateLand` names its land by number (`sac_source_id`), so it is a plan
source, and the prune compared "#33 pays for #32" with "#32 pays for itself" by class: one untapped Mutavault each, so
one was dropped, and the survivor had #33 tapped with its own animation unpayable. Same for `{cast Sinew Sliver +
animate #33}`. No new mechanism: the Vampires fix (13.9: `Resources` / `PayAndRecord` key a plan source apart;
`KeyedApart`) covers it. Two unit tests pin the Mutavault shapes (`test_mana_oracle.cpp`, "SLIVERS").

**APPLY-DROP (>99% with Hatchery Sliver): the unpinned replicate cast's greedy copies (oracle cover rule).** Hatchery
grants replicate, and the enumeration offers each Sliver at pinned counts 0..kmax AND unpinned (`replicate_count`
-1, copies made greedily at resolution, TurnSolver.cpp ~38800). Autonomous play has no line hold for that greedy
(`SinkCostWithLineHold` returns `own` unless `HumanPlayActive()`), so in `{Predatory Sliver(-1) + Plated Sliver}` the
copy eats Plated's mana and Plated drops. The oracle realises both by overpaying Predatory so no copy is affordable,
leaving Mutavault up; the pinned-zero sibling pays Predatory off Mutavault + Cavern and leaves Secluded Courtyard up.
Those boards are incomparable, so the sibling test (engine boards only) failed. Fix (`AuditPlan`, oracle only): an
uncovered full board also counts as covered when a same-actions sibling reaches it (same core, at least the
resources) under one of the SIBLING's OWN payments (`Realize(sibling, ...)`), because which of several incomparable
payments realises a plan is the payer's standing exemption and the sibling's own audit vouches for the payment the
engine picks there. A capped or unmodelled sibling search reaches nothing (fail closed). Unit test: "a replicate drop
whose board a same-actions sibling reaches under its own payment is covered" (`manaoracle::PlanVerdict`, a test
entry that captures `Emit`'s classes).

**ENUM-GAP (a): the Sliver Hive token was paid as a creature SPELL (engine rules bug).** "{5},{T}: create a 1/1
Sliver" is an activated ability of a land; both apply worlds (`ApplyPlanDirect`, `AIEngine::TakeTurn`) paid it with
`for_creature=true`, so Ancient Ziggurat's creature-spell-only mana bought it (CR 106.6). Fix: pay with
`for_creature=false`, under `CreatureAbilityPayScope` when the source is a creature (Basri: Secluded Courtyard's
"...or activate an ability of a creature source of the chosen type"). Unit test "a Sliver Hive token is never paid
with creature-spell-only mana"; scenario `slivers_hive_token_no_creature_mana.json` (a legality pin: the enumeration
already priced the activation as noncreature, so the plan was off the menu either way).

**ENUM-GAP (b): Thrumming Hivepool's affinity ignored pinned replicate copies (enumeration gate).**
`SameTurnAffinityDiscountFor` credited each same-subset Sliver cast but not its pinned copies, so `{Galerider + one
copy + Hivepool}` off three lands priced Hivepool at {3} instead of {1} and kept the plan off the menu. Fix: credit
`1 + replicate_count` for a pinned count (exact; it can only add plans). The unpinned cast keeps the cast-alone credit
(its greedy count is unknown at enumeration).

**Census (10,000 fresh games, gi 100000-109999, seed 30000000 + gi, oracle level 2).** Base = origin + the Angels fix
(f627ae4c6); new = + Vampires 13.9 + this section; `sib0` = new with the cover rule off (same games, oracle-only arm).

| class | base | new, cover rule off | new |
|---|---|---|---|
| ORACLE-GAP | 574 | 83 | **83** |
| APPLY-DROP | 346 | 342 | **87** |
| ENUM-GAP | 6 (5 Hive token, 1 Hivepool) | 0 | **0** |
| APPLY-DROP-ORDER | 2 | 2 | 2 |

Attribution: ORACLE-GAP 574 -> 83 is the 13.9 keying; APPLY-DROP 342 -> 87 is the sibling-payment cover (the 255 become
DROP-COVERED); the six ENUM-GAPs are the Hive-token rule fix (5) and the affinity credit (1). Play identity: oracle
level 0 vs 2 on the new binary over the same 10,000 games: 0 of 40 job digests changed, units identical.

**What is left -- all the unpinned-replicate family (USER question).** The 87 APPLY-DROPs (85 with Hatchery on board;
e.g. `{Aether Vial + Galerider Sliver(-1)}`: Galerider is ranked first, paid with Cavern of Souls' coloured face, the
greedy copy takes Unclaimed Territory, and Vial has only Ziggurat left) and the 83 ORACLE-GAPs (e.g. s30100003 T4
`{Cloudshredder(-1) + Galerider(-1)}`: the engine's greedy loop skips a Cloudshredder copy and copies Galerider; the
oracle's realisations make the Cloudshredder copy instead and never reach the engine's board) are both the greedy
count of the -1 variant. The -1 variant duplicates the pinned plans (wasted search under the COLLAPSE rule) and exists
for Hivepool-affinity lines. Keep it, give it an affinity-aware line hold, or drop it where no affinity spell is
co-planned: a USER decision, not settled here. **APPLY-DROP-ORDER (2 in 10k, 26 in 100k):** `{Sinew + Hivepool +
Cloudshredder}` casts Hivepool before Cloudshredder, so Hivepool costs {1} and Cloudshredder drops; Cloudshredder >
Sinew > Hivepool pays all. The generic rank puts creatures (10) before noncreature spells (20), so something
downstream reorders (not traced). Cast order is the USER's: a candidate rule is "an affinity spell after every
same-subtype cast of the plan".

**Play.** Held-out A/B (one binary, the Hive-token pay and the affinity credit off/on per arm, seed 53000000 + gi,
slivers_vial at its shipped profile): Standard 5,000 paired games: **0 slower, 0 faster**, net 0, mean 4.2082 both, 2
games changed play, units +0.004%; 2HG (30 life, two heads) 5,000: **0 slower, 0 faster**, net 0, mean 4.8720 both,
22 changed, units +0.005%. The A/B levers were temporary and are removed: both fixes are unconditional.

**USER rulings and what followed (2026-11-03).** (1) The unpinned (-1) replicate variant is DELETED ("if it is waste it
is waste"); the resolution-time greedy copy loops stay because the beyond-horizon greedy and the pending-grant plain
cast use them. (2) Hivepool already comes last. (3) **B5, the USER's provider prune:** `SliversProvider` keeps ONE
replicate count per cast set, the most copies payable from the mana left after the plan's other casts
(`MTG_SLIVERS_REPLICATE_TOPK`, default ON). (4) **B6, the USER's full Slivers cast order**, one row per card
(`MTG_SLIVERS_ORDER`, default ON). Slice (5,000 games, gi 100000-104999, ORACLE-GAP / APPLY-DROP / ORDER / ENUM-GAP):
base 57/34/0/0, -1 deleted 17/21/0/0, +B5 13/19/0/0, +B5+B6 13/23/0/0; DROP-COVERED 2950 -> 0.

**Held-out A/B** (`build/scratch/mcslivers/ab3.py`, seed 61000000 + gi, 5,000 Standard + 5,000 2HG per arm, one pooled
batch per binary; a0 = base on the pre-deletion binary; the deletion is an exact identity for every arm: a1/a2 re-run on
the post-deletion binary reproduce all 200 job digests and units):

| arms | Std changed / slower / faster / net / units | 2HG changed / slower / faster / net / units |
|---|---|---|
| a0 -> a2 (B5+B6, shipped) | 2052 / 3 / 4 / **-1** / -14.03% | 2346 / 3 / 5 / **-2** / -16.90% |
| a0 -> a4 (B5+MANLAND) | 465 / 0 / 1 / **-1** / -14.55% | 715 / 2 / 0 / **+2** / -15.90% |
| a0 -> a5 (B5+B6+MANLAND) | 2046 / 0 / 4 / **-4** / -14.89% | 2338 / 2 / 5 / **-3** / -16.40% |
| a1 -> a4 (MANLAND alone) | 16 / 0 / 3 / -3 / -1.00% | 16 / 0 / 1 / -1 / +0.73% |
| a2 -> a5 (MANLAND alone) | 16 / 0 / 3 / -3 / -1.00% | 15 / 0 / 1 / -1 / +0.60% |

a2's slower games: Std 1891 / 4064 / 4771, 2HG 191 / 1145 / 1570; at d8 b0 only Std 1891 recovers. In Std 4064/4771 the
base casts a one-pip Sliver with NO copy on T3 and animates Mutavault with the spare mana while the top count taps out.
**THE MANLAND RESERVE** (ADOPTED 2026-11-04, always on with the top-count fold; the flag `MTG_SLIVERS_REPLICATE_MANLAND`
and its heurarm slot are deleted. USER: *"Sure, that makes sense. In that case we might need to search the two
options."* Both counts survive the fold as separate plans and the search branches on them) also keeps the top count whose bill leaves an untapped start-of-turn manland its own mana + animate cost. It
removes four of the six at the shipped setting (Std 1891/4064/4771, 2HG 1145) and adds none; of the two left, 2HG 1570
recovers at d8 b0 (6 -> 5) and **2HG 191 does not** (5 at d8 b0, base 4). 2HG 191 is not a B5 or MANLAND loss: the
-1 deletion alone (a3) plays the same game as a1 (identical digest). The base's T4 kill rides a T3 Plated Sliver cast
with replicate x2 by the resolution greedy loop: {W}{W}{W} from Mutavault ({C} only), Ancient Ziggurat and Sliver Hive
-- two coloured sources. That reads as an over-payment in the greedy copy path (unconfirmed; to trace).

**Follow-up: a continuation takes over the casts it re-names (2026-11-03, branch `wip/cyclone-bind`).** The binding
above counted some casts TWICE. `g_line_unpaid_cost` still owes every cast of the invoking list that has not paid,
including the ones the continuation, re-solved with the drawn card, re-names and casts itself. Each such card is cast
once (by the continuation), and the invoking list's copy then finds no card. Unpredictable Cyclone held-out
(seed 1810001+gi, base = before the binding): gi74 / gi514 / gi962 / gi1491 went 6/5/5/5 -> 7/6/6/6. In gi962 T5,
{Seething Song, Ponder, Cyclone} re-solves at Ponder's draw to {Cyclone}, so the hold read {6}{R}{R}{R}{R}.
`LineFloatTapFirstPlan` then tapped the last Steam Vents for Cyclone's generic, and the drawn Curator's cycling {U}
was gone. That cost the Cyclone -> Emrakul kill.

* **Fix.** `ContinuationLineOwed` (TurnSolver.cpp, used by the rollout's `cont_line_armed` bind and the
  executor's `ExecBindContinuationLine`) takes the take-over off the outer remainder before adding the
  continuation's casts. Per name N: live = min(invoking list's counted hand casts of N, hand(N)), and
  taken = clamp(live + cont(N) - hand(N), 0, live). The costs of the `taken` latest such actions come off (clamped).
  The invoking list is the section carrying the hold: the rollout's `line_section_acts`, and the executor's
  `exec_line_invoker` (a re-derived continuation's own list while its cast loop runs). The hold only orders
  payments, so this changes no payable set.
* **Known approximation.** The count does not know which of the invoking list's casts already paid. If a same-name
  copy was already cast and the continuation casts a spare copy from hand, one paid copy counts as live. Its cost
  then comes off the hold, an under-count that is clamped.
* **Repros.** gi514/962/1491 -> 5/5/5; gi74 stays 7 at play settings and recovers to 6 at `--depth 8
  --budget-ms 0`. On gi74 the takeover fires in its rollouts, but the game is byte-identical with or without it.
  What remains there is the binding's intended addition of the continuation's own casts. Restoring the outer
  hold minus the taken casts at close (tried, dropped) does not move it either. The seven Auras APPLY-DROP repros
  stay 0.
* **Held-out A/B** (seed 2610001+gi, 2HG 2710001+gi; on vs the binding as landed; one pooled batch, 528 jobs /
  24,000 games): Cyclone 3,000 net -1 (0 slower, 1 faster: gi1122 T6 -> T5; 22/120 jobs' digests moved; units
  -0.032%); Cyclone 2HG 600 net 0 (1/24 jobs moved; -0.013%); Auras 3,000 and 20 scanned decks
  byte-identical (units within +/-0.01%). No slower game.

### 13.13 KittyEquipment: the equip route audited link by link (2026-11-04, branch `wip/mc-kitty`)

Pass 1 r2 (`logs/mana_clean/pass1-kittyequipment-r2`, 42,000 games, src `644270cdc4ce`) failed on UNMODELED 56,506
(every one "combo-route macro") and ENUM-GAP 183. All three fixes are ORACLE-ONLY: play is byte-identical.

**UNMODELED: the equip route's macro is now audited.** `equiproute::Run` realises its macro as a chain of plain menu
plans, each applied with `ApplyPlan`. Each link apply now goes through `manaoracle::RouteLinkApply`, which is
`ApplyPlan` plus a tape armed only by the oracle's `EngineApply` (`RouteTape`, `ManaOracle.h`). The trial inside
`EquipRouteApply` runs under `RouteTapeSuspend`, and a losing DFS branch is rewound (`RouteTapeMark/Rewind`).
`AuditRoute` audits each recorded link with `AuditPlan(before, plan, after/drops)` inside `EquipRouteLinkFrame`, so the
oracle's applies replay the route's. Flags carry the prefix `equip-route link k/n of {...}:`; the summary line is
`=== MANA ORACLE combo routes: N equip-route macros audited, M links, X ms ===`. The EDF and Alchemist routes (which
hand-roll their payments) stay UNMODELED.

**ENUM-GAP, 147 of 183 ("fewer resources: ...plan#N 1>0"): oracle keying asymmetry.** `JudgeHit` runs inside
`Realize`, which keys the plan's own sources apart (`t_plan_srcs`, `t_key_twin_taps`), while the menu's boards
(`MakeMenuView`) are keyed outside it. The same board then compares as "fewer resources". `JudgeHit` now computes
unkeyed. Repro seed 30009252 T3.

**ENUM-GAP, the other 36: the noop reduction order.** `PerformedPlan` removed actions greedily in plan order. A cast
that the Sram-draw continuation re-makes anyway also "does nothing", so it dropped a Bone Saw first and ended on a plan
no enumeration offers. It now returns its whole reduction chain, and the noop test runs it activations-first and in
the old order, accepting any chain plan that is on the menu (each is realised by the same script on the same board, so
the test is sound; the old final is still checked, so flags can only go). Repro seed 30001917 T2.

No ENUM-GAP was an engine enumeration miss; every one examined was oracle-side.

| check | result |
|---|---|
| the 108 games holding r2's 183 ENUM-GAPs | ENUM-GAP 0, UNMODELED 0; 137 macros audited, 454 links, no ISSUE flag |
| play identity, oracle off, gi 0-1999 (20 jobs) | 20/20 digests and units identical |
| **gate `mc-kitty-pass2`, fresh block gi 42000-46999 (PGO, src `2d767a1caac7`)** | **clean: 5,000 games, 0 issues**; DOMINATED-LAND 5,796 (disclosed); 6,774 macros audited, 23,425 links; play identical with the oracle off |

Cost: the route audit is 8.4 s of 2,153 s oracle CPU over the gate (0.39%).

### 13.14 Fluctuator: a breakpoint continuation paid blind to its own line (2026-11-03, branch `wip/mc-fluctuator`)

The mana-clean gate's first Fluctuator run (`logs/mana_clean/pass1-fluctuator/`, 100,000 games, src 7ec55bb) failed on
APPLY-DROP alone: 591 flags, 121 signatures, every one `need-later` (rollback depth1 568, depth2 15, depth3+ 8).

**Real engine misses, not a misclassification.** The obvious suspicion -- Enlightened Tutor and the free cycles draw,
so the boards might differ only through hidden information -- does not hold. Seed 30003033 gi3033 T3 (the top
signature): plan `{no land}`, a free cycle (Drannith Stinger, cycling {1} under Fluctuator = {0}) draws Capital City
#9, the breakpoint's continuation plays it and casts {Unearth, Enlightened Tutor}. Both payments come AFTER the draw,
with no draw between them: the continuation was chosen on a board that already showed the City. The oracle's line
feeds Capital City #9 with #11's free {C} for Unearth's {B} and pays the Tutor with Glittering Massif's {W}; the engine
fed #9 with Massif's {R} and the Tutor met #11 alone. The HIDDEN-INFO rule (`TapsHiddenCard`) is about a GATE deciding
before the draw; here the payer decides after it, so APPLY-DROP is the right class.

**Root cause: a continuation's casts were paid under the OUTER line hold.** A continuation is applied inline
(`apply_continuation_plan` in ApplyPlanDirect; AIEngine's `resolve_draw_breakpoint`, pod chain and site-9 loops, and
the committed-line replay), so `g_line_unpaid_cost` held only what the outer plan still owed -- zero for `{no land}`.
Every line-aware payer rule (the line-spare generic, the line-float hold, the ramp-filter route) was blind to the
continuation's own line. The greedy declines a coloured pip off an any-colour filter, so Unearth's {B} went to the
complete backtracker, whose first covering set feeds the filter with Massif.

**That half is 13.10's binding.** This branch first fixed it with its own scope (`ContinuationLineHold`, flag
`MTG_CONT_LINE_HOLD`). The Auras fix (13.10, c4b7ea18d) landed the same mechanism first: `cont_line_armed` in
`apply_plan_actions`, `ExecBindContinuationLine` at the executor's three continuation cast sections, and the
`Action::rec_line_hold` stamp installed by `replay_recorded`. This branch is rebased onto it and its own copy is
deleted. 13.10's version covers every site the deleted one did and two more: the replay keeps the stamped hold for
the rest of the cast section, not only for the stamped cast, and it stamps `CastFromGraveyard` too. The two differ in
one respect: 13.10 binds after the section's `CastSectionUnlock` up-front fire and restores the outer hold unchanged
on exit, where the deleted scope wrapped the whole section and restored the outer hold minus what the section paid.
Every number below is measured on the rebased build, i.e. with 13.10's binding.

**The fix on top: the any-colour filter feed route** (`AnyFilterFeedRouteHold`, ManaPayment.cpp, called from
`TapForCostShared`; no flag -- it was `MTG_ANY_FILTER_FEED_ROUTE` for the A/B only). RampFeedRouteRepay's shape for the
any-colour filter: when the line still owes mana and an any-colour filter is untapped, the payment is tried on a
scratch board; if the trial tapped an any-colour filter and the rest of the line is no longer payable (one
backtracker solve), while the board pays the bill and the rest together, the payment is re-tried with ONE of the
trial's sources held out (battlefield order) and the first that leaves the line payable is made. Otherwise the
payment is the greedy's, byte for byte. It can only fire where an any-colour filter is on the battlefield: in the
suite, Fluctuator (Capital City), Galadriel (Crystal Grotto, Rumble Arena) and Snow (Arcum's Astrolabe).

Attribution on the slice below: 13.10's binding alone leaves the same 51 APPLY-DROPs as the base, flag for flag (it
gives the route a line to read, but the backtracker still takes the first covering set); the route on top of it
clears 49. On the named repros the binding alone does clear the generic-pip shape (seed 30081285 T4, Drannith
Stinger's {1} off Irrigated Farmland, the line's only {W}: the line-spare generic now sees the Tutor). The route without a
binding cannot fire at all (its `later` bill is zero). The named repros (oracle level 2, one game each) go from
APPLY-DROP 3033:2, 9815:4, 4938:1, 81285:4 to 0 each; 4350:1 and 11214:1 stay (mechanism 4, below).

**Census, a fresh slice** (Fluctuator games gi 100000-104999, seed 30000000 + gi, `MTG_MANA_ORACLE=2`; base =
980616e55's binary, `build/scratch/mcf/slice_base.*`; c4b7ea18d alone = the rebased binary with the route switched
off, and this branch, one pooled batch, `build/scratch/mcf/slice_rb1.*`):

| build | APPLY-DROP | depth1 | depth3+ | play digests changed vs c4b7ea18d | units |
|---|---|---|---|---|---|
| base (980616e55) | 51 | 49 | 2 | -- | 17,347,203 |
| c4b7ea18d alone | 51 (the same 51) | 49 | 2 | -- | 17,347,124 |
| this branch | **2** | 0 | 2 | 1 of 20 jobs (gi 104750-104999) | 17,347,192 |

**Held-out A/B, the route alone** (c4b7ea18d alone vs this branch: one pooled batch, the rebased build with the
route switched off by its temporary A/B flag; seed 1810001 + gi, shipped play settings, `build/scratch/mcf/ab2/r.*`).
The route can only fire where an any-colour filter is on the battlefield, so the three decks that hold one are the
whole scope:

| deck | paired | net turns | slower | faster | play digests changed | work units |
|---|---|---|---|---|---|---|
| Fluctuator | 4,000 | 0 | 0 | 0 | 0 of 16 jobs | +0.000% |
| Galadriel | 1,000 | 0 | 0 | 0 | 0 of 10 | +0.001% |
| Snow | 300 | 0 | 0 | 0 | 0 of 12 | -0.186% |

No searched branching is added: the route is a re-payment of a bill the search already chose, not a new option, and
it runs only where the greedy's payment strands the declared line. The fresh slice's one moved job (above) is the
whole play effect seen so far. The final code (no flag) is identical to the flagged build with the route on (job
digests and units, Fluctuator 250 + Galadriel 100 games).

**The one slower game, root-caused: Unpredictable Cyclone seed 1710206 gi205, T5 -> T6** (the first, pre-rebase A/B;
it is 13.10's binding, not the route: the rebased build is T6 with the route on or off, and Cyclone holds no
any-colour filter). It recovers at `--depth 8 --budget-ms 0 --ignore-play-profile` (T5 on base, pre-rebase and
rebased builds). The two games diverge before turn 1, at the MULLIGAN BOTTOM: Cyclone has no keep table, so the bottom
of the 7-card mull-to-6 hand is chosen by playing each candidate out (`MTG_BOTTOM_TRIAL_TRACE`). Base: bottom Emrakul
wins T7, bottom Ulamog the Infinite Gyre wins T5, so it bottoms the Gyre and the game wins T5. With the binding, the
bottom-Emrakul trial plays BETTER (T7 -> T6), so the incumbent best is 6 and the remaining trials run under the
bottom turn cut's strict stop, 5. The bottom-Gyre trial plays the same game as base, unit for unit, and wins in
the EXTRA turn Emrakul's cast trigger grants in round 5. `GameEngine::PlayOutFrom` does not play an extra turn owed
after round `stop`, so the trial reads "no win", and Emrakul (T6) is bottomed instead. With `MTG_BOTTOM_TURN_CUT=0`
both builds bottom the Gyre and win T5. **So it is a pre-existing defect in the bottom turn cut**, which the binding
only exposed. The stop check's comment ("a win there would only TIE a round-`stop` win") holds for a non-strict stop
(= best), but under the strict stop (= best - 1) an extra-turn win in round `stop` is a strict improvement and is
cut. The repair is below (its own commit).

**The bottom turn cut plays an extra turn owed in the cut round** (commit "engine: the bottom-trial turn cut plays an
extra turn owed in the cut round"). `GameEngine::PlayOutFrom` now stops only when `turn_number > stop`, or
`turn_number == stop` with no extra turn pending. Extra turns do not count toward the turn count, so a win in one is a
round-`stop` win: a tie under the non-strict stop and a strict improvement under the strict one. Unit test in
`test_cyclone_phase2.cpp`. Held-out A/B (seed 1810001 + gi for Cyclone and 2010001 + gi for Cyclone 2HG, shipped play
settings; fix off = the c4b7ea18d + route arm of the A/B above, fix on = the same jobs on the hold-up build with the
fix, which is byte-identical to it elsewhere, checked on 50 games; the 2HG pair ran in one pooled batch,
`build/scratch/mcf/ab4/xt.*`):

| deck | paired | mean on / off | net turns | slower | faster | job digests moved | work units | CPU ms on / off |
|---|---|---|---|---|---|---|---|---|
| Cyclone | 3,000 | 4.9417 / 5.0663 | **-374** | 5 | 369 | 118 of 120 | **-3.858%** | 52.7M / 72.3M (different runs, unreliable) |
| Cyclone 2HG | 600 | 5.1000 / 5.2117 | **-67** | 2 | 66 | 23 of 24 | **-3.190%** | 18.81M / 19.15M (-1.8%, one batch) |

gi2278 (5 -> 6 under the binding) is restored, T5. Every slower game recovers at `--depth 8 --budget-ms 0
--ignore-play-profile` (fix on, d8 b0 vs the fix-off arm's shipped turn): Cyclone gi332 5 vs 5, gi1217 5 vs 6, gi2241 5
vs 6, gi2516 4 vs 4, gi2879 4 vs 4; 2HG gi394 6 vs 6, gi549 5 vs 6 (`build/scratch/mcf/c3/slowx2.txt`). The size of
the gain says how often it fired: Cyclone has no keep table, so every mulligan's bottom is chosen by trial playouts,
and Emrakul's cast trigger puts an extra turn in many of them. Units fall because games end sooner. The fix adds no
search and no branching. Smoke: cyclone d3 5.16 -> 5.00, d5 5.06 -> 4.91, cyclone2hg d3 5.22 -> 5.06, no slower game.

**Mechanism 4: a payment made BEFORE a draw for a cast chosen AFTER it** (both remaining flags, and the repros
seed 30004350 gi4350 / 30011214 gi11214). Plan `{no land, Fluctuator}`: Fluctuator's {2} is the only cast the line
names, and the greedy pays it with the land whose colour a card still in hand needs (gi58: Festering Thicket's {B},
Unearth in hand; gi166: Scattered Groves' {W}, Enlightened Tutor in hand). The cycles then draw what makes that card
castable (a Drannith Stinger to Unearth, a reason to Tutor), and the continuation's cast is dropped. Still
APPLY-DROP, correctly: this is exactly the USER's rollback case ("keeping up blue instead of white so that we can
cast a blue spell only after we discover that blue is needed", section 9), need-later at depth 3+, and the mana the
oracle spends is on the board at the plan's start -- nothing hidden is tapped. No line hold can see it (the card is
not in the line); the candidate remedies are a hand-demand tie-break for a generic pip in autonomous play (today
`MTG_HUMAN_TAP_DEMAND` is human-only, by measurement) or the item-C search-level fallback. Both are trade-offs and
the USER's call. The USER chose a third, the deck hold-up below.

**The USER's deck hold-up, for mechanism 4** (commit "mana(fluctuator): any-colour filter feed route; the
deck's payment hold-up"). `DecisionProvider::PaymentHoldUp` returns an ordered list of costs the deck wants left
payable after a payment; `ProviderHoldUpHold` (ManaPayment.cpp, after the route in `TapForCostShared`) makes the
greedy's payment on a scratch board and, only if that leaves the line's rest plus the hold-up unpayable while the
board pays the bill and both together, holds ONE of the trial's sources out (battlefield order), taking the first
that keeps them payable. A preference, never a refusal, and no new searched option: the payment is the greedy's
whenever the hold-up does not bind. Fluctuator's list, each entry only while the card is in hand: {1}{B}{R} (Unearth
and Drannith Stinger), else {B} (Unearth), else {1}{R} (Stinger), else {W} for Enlightened Tutor only while NO
Fluctuator is in hand or on the battlefield (USER: "its purpose is to get Fluctuator and if we have one there is no
need to hold W"). A pip the bill itself carries is not held for.

Repros, oracle level 2, one game each: 30004350 gi4350 0, 30011214 gi11214 0, 30104058 gi104058 0 APPLY-DROP (it
was 1 each without the hold-up). 30100666 gi100666 stays APPLY-DROP 1 with the Tutor entry on, off, and with every
hold off: the drop is the payment of Fluctuator's own {2} at T4, and a Fluctuator is in hand, so the USER's condition
keeps the {W} hold off. That is the rule working as stated, not a miss.

Census, the fresh slice (gi 100000-104999, `MTG_MANA_ORACLE=2`, hold-up on and off in one pooled batch,
`build/scratch/mcf/c3/slice_h3.*`): APPLY-DROP on 1 (gi100666, above) vs off 2 (gi100666, gi104058); CAPPED 0 (the
escalation below retried 10 plans, all 10 finished); 8 UNMODELED per arm, all one game (seed 30101162 gi1162 T4,
plans {Stinger, Unearth, Unearth}), "scripted payment did not pay {1}{R}": the oracle's script taps Canyon Slough
for {B}, Glittering Massif for {R} and Capital City for its any-colour mode, and applied, the City is fed the
Massif's {R} (float {W}{B} after the taps). An oracle-side defect in how a scripted any-colour filter tap picks its
feed; it is the same in both arms, so it is not this branch's, and it is owed to the checker (UNMODELED now fails the
gate).

Held-out A/B, hold-up on vs off (seed 1910001 + gi; Fluctuator 2HG at 30 life, two heads; one pooled batch,
`build/scratch/mcf/c3/hold.*`):

| deck | paired | mean on / off | net turns | slower | faster | job digests moved | work units | CPU ms on / off |
|---|---|---|---|---|---|---|---|---|
| Fluctuator | 4,000 | 3.5455 / 3.5463 | -3 | 0 | 3 | 16 of 16 | +0.238% | 564,675 / 484,539 (+16.5%) |
| Fluctuator 2HG | 2,000 | 3.4950 / 3.4960 | -2 | 0 | 2 | 16 of 16 | -0.242% | 239,691 / 201,389 (+19.0%) |

The CPU rise is real (on was dearer in every one of the 32 interleaved job pairs) and sits outside the unit meter:
on 250 games the hold-up ran 782,769 trials (~3,100 per game, 3.9% fired), each a scratch GameState copy, a greedy
payment and a backtracker solve, because a hold-up card is in hand on most Fluctuator payments, in the search's
rollouts as well as the executor (the payer is shared, as it must be). Two exact cuts are in the commit: an entry the board cannot pay together with the bill
is dropped on the payer's AMOUNT bound before any scratch board, and one greedy trial serves every entry (trials
21.4M -> 7.0M on the 6,000 on-arm games; digests, means and units identical on all 32 jobs). Measured as three
binaries side by side on the 6,000 on-arm games (`build/scratch/mcf/c3/cost/`; loaded box, a few points of noise):
CPU vs hold-up off was +13.3% before the cuts and +9% to +14% after them, over two runs. A third exact cut (solving
the last entry on the trial board, one copy fewer) measured nothing and was dropped. **What is left is the rule's
own price: about one scratch payment and one backtracker solve per Fluctuator payment while Unearth, Drannith
Stinger or (without a Fluctuator) Enlightened Tutor is in hand.** It is confined to Fluctuator (every other
provider's list is empty, so the hook returns before any work), adds no searched branching, and buys the gate's
APPLY-DROPs for mechanism 4 plus net -3 / -2 turns. A later exact cut (commit "perf(fluctuator hold-up): pay first on
the real board where the trial's quiet scope is a no-op"; byte-identical on all 32 jobs) makes the payment first on
the real board wherever the trial's quiet scope is a no-op (search, rollouts, autonomous play) and decides the hold on
the board it left: instructions over hold-up off **+6.8% / +6.9%** (was +10.9% / +11.0%).
**ADOPTED by the USER 2026-11-04 at that +6.8%** (asked whether the hold-up is worth its cost: *"I suppose so."*).
It is unconditional: there is no `MTG_PROVIDER_HOLD_UP` lever.

Smoke (c4b7ea18d's 12 failing rows are unchanged): fluctuator d0 4.989 -> 4.982, d3 and d5 same score with the
digest moved; no slower game.

**The oracle's CAPPED escalation** (commit "mana-oracle: a CAPPED plan is realized again under a 20000-apply
ceiling"). CAPPED now fails the gate (USER 2026-11-03), so a plan that hits `MTG_MANA_ORACLE_NODES` (2000) is
realized once more at 20000. It is always on; `MTG_MANA_ORACLE_NODES_RETRY` overrides the number and is not an off
switch. Checker-only. The 7 CAPPED Fluctuator games of 3,000 (seed 30000000 + gi: 651 2139 2391 2655 2670 2772 2802)
all finish at 20000 with wall about unchanged, and the slice above has 10 retried, 10 finished, CAPPED 0.

### 13.15 Niv known cards in non-clairvoyant reshuffles (2026-11-07 port)

Niv gi887 (seed 1650001) has a known Curiosity on top after Brainstorm. The old non-clairvoyant
search shuffled it away in its sampled futures, so it preferred Signet + Tandem Lookout over
Signet + Generator Servant even though Servant enables a guaranteed T5 hasty Firemind + Curiosity
kill. The fix from `wip/niv-nc-known-top` is ported onto the current library's known-position mask:
`Library::ShuffleUnseen` shuffles only unknown positions, preserving placed top cards and known
bottom cards; a real shuffle still clears knowledge. The honest-teacher and `MTG_NC_SEARCH`
reshuffles call it. With no known cards it uses the original whole-library shuffle exactly.
The older branch's 2,000-game shipped Niv arm was byte-identical. On the current tree, all 2,631
unit cases pass and Niv gi887 wins T5 under `MTG_NC_SEARCH=1` at d8/b0. The T4 menu has one known
top and two known bottom cards: Generator Servant + Izzet Signet scores 5.000, versus Izzet
Signet + Tandem Lookout at 6.000. `EmitEvalRows` now uses the same known-preserving shuffle for
training labels; the blind leaf and mulligan trainer remain on their existing paths.

### 13.16 Modern RW Burn and Giants mana gates (2026-11-08)

Modern RW Burn's first 5,000-game gate found 10 ENUM-GAPs, all two-copy
`Skewer the Critics` lines. The first Skewer pays its full `{2}{R}`, deals face
damage, and the second pays spectacle `{R}`. `SpectacleCreditCtx` now prices
each selected cast against face-damage casts before it in the executor's stable
order, including an earlier Skewer. The fresh 5,000-game block (5000-9999,
source `90af1f5c88b8`) was clean with oracle-off play identity.

Giants' next 5,000-game block (64000-68999, same source) found two ENUM-GAPs:
`Stinkdrinker Daredevil` cast this turn discounts `Giant Harbinger`, allowing
Harbinger plus `Lightning Greaves` and its equip on six mana. The existing
`SameTurnReducerGenericCredit` supported subtype reducers, but the enumerator's
frame-level `any_reducer` scan only enabled it for color reducers. It now
recognizes subtype and chosen-type reducers. The free-attach guard preserves
the old refusal when an equip moves haste away from a fresh host, while a
newly cast Greaves has no old host to lose. Both original repro seeds have
`MISS=0`, 2,636 unit cases passed, and strict replay of 777 references found
zero drift. The fresh 5,000-game Giants block (69000-73999, source
`95d103c9dfa9`) was clean with oracle-off play identity.

Kithkin's hybrid payable-order bound and oracle frontier payment-history fixes
remained clean on this source: games 15000-19999, zero issues, with oracle-off
play identity. The earlier clean block was games 10000-14999 on source
`a93beb64c687`.

The pooled current-source refresh (source `95d103c9dfa9`) also passed Modern RW
Burn games 10000-14999 and Soldiers games 81000-85999: 5,000 each, zero issues,
byte-identical oracle-off play. Together with the Giants and Kithkin blocks,
these four decks have clean 5,000-game stamps on the same source.

### 13.17 Lifegain refresh and Minotaur Karoo identity (2026-11-08)

The pooled fresh-source gate for CritterLifegain and LifegainHeliodCombo ran
100,000 games each (100000-199999, source `95d103c9dfa9`), found zero mana
issues, and reproduced byte-identical play with the oracle off. Both clean
records are in `test/mana_clean.json`.

Minotaur's 5,000-game block (162000-166999, same source) found five ORACLE-GAP
flags on two seeds and no engine MISS. Seed 30163325 has two Unclaimed Territory
copies: the payment table folded tapping #58 into tapping #57 even though a
later deferred Rakdos Carnarium bounce returns a specific land. While a Karoo
is in hand, the oracle now keys battlefield lands by card number through its
payment table and frontier resources. That seed replays with zero gaps; all
2,636 unit cases, all 179 smoke and 286 regression configurations, and strict
replay of 777 references passed without play changes or reference drift.

Seed 30164916 still has four ORACLE-GAP flags. Its no-land plan casts
Slaughter-Priest of Mogis, then a continuation plays Rakdos Carnarium and
tries a `{2}` sacrifice outlet payment. The oracle's nearest outcome taps
Secluded Courtyard, while the engine's board leaves it untapped. This is an
open checker gap; Minotaur remains in beta and needs a fresh clean block after
it is fixed.

### 13.18 Fresh ramp-filter land gate (2026-11-08)

BreachingDragonstorm's current-source 5,000-game block (100000-104999,
`bc60d82d28ef`) found five ENUM-GAPs in one T8 game (gi101733): a newly
played Mossfire Valley and two casts, Breaching Dragonstorm followed by
Maelstrom Wanderer or Annoyed Altisaur. The oracle pays the sequence, but the
flat pool cannot express the Valley's `{1},{T}` color conversion and the
filter fallback tries one payment order, which rejects the later cast. The
`MTG_DBG_MULTI=8` trace showed pool total 13, combined cost 13 for Wanderer,
with one active ramp filter and a flat-gate rejection. Disabling the scalar
mana prune alone left all five gaps.

When an untapped ramp filter entered this turn, the enumerator now admits a
multi-cast subset rejected by that single-order fallback if its total and
noncreature total costs fit the optimistic pool. The apply validates the actual
sequence. The gi101733 repro fell from five ENUM-GAPs to zero. An attempt to
sort the fallback's payment walk by cast rank did not clear any of the five,
so the single-order limitation remains; the bounded admission is the adopted
fix. All 2,636 unit cases passed, smoke had 179 unchanged configurations, and
regression had 283 unchanged plus three equal-win-turn play changes (Niv 2HG
T4, Niv T5, Treasure Hunt T4), no slower or faster games. Strict replay of 777
references found zero drift or enum gaps. The accepted regression baseline
records those three line choices. BreachingDragonstorm's next 5,000-game block
(105000-109999, source `8b9a23e598f8`) passed with zero issues and oracle-off
byte identity.

The same source's pooled queue also gave clean 5,000-game blocks for Knights,
KittyEquipment, CritterLifegain, LifegainHeliodCombo, Fluctuator,
Colossus Hammer, Burn, Auras, and Modern RW Burn after its new profile was
adopted. The other refreshed decks remain beta with their issue counts in
`test/mana_clean.json`.

### 13.19 Current-source refresh and Jumbo Cactuar baseline (2026-11-08)

The source `8b9a23e598f8` pooled 5,000 fresh games each for Auras, Burn,
Colossus Hammer, Fluctuator, Modern RW Burn, Knights, CritterLifegain,
LifegainHeliodCombo, Giants, Kithkin, Soldiers, and WhiteKnights. All twelve
were clean, with oracle-off byte identity. KittyEquipment's next 5,000 games
had no MISS or ORACLE-GAP, but one large T4 decision exhausted its 300-priced-
second audit budget, leaving 888 CAPPED-DECISION flags. This is an audit
coverage limit on a new game range, not a fresh-ramp-filter path (the deck has
no ramp filter).

Jumbo Cactuar's first 5,000-game block (0-4999) found 17,966 issue flags:
1,450 ENUM-GAP, 589 APPLY-DROP, 870 APPLY-DROP-ORDER, 653 DOMINATED-CREATURE,
333 ORACLE-GAP, 340 UNMODELED, and 13,731 CAPPED-DECISION. The capped flags
come from one decision. Oracle-off play remained byte-identical. The first
repair target is the repeated T3 Wild Growth / Overgrowth / Tooth and Nail
enumeration gap; the per-class signatures and reproductions are in
`logs/mana_clean/20261108-090123/report.md`.
