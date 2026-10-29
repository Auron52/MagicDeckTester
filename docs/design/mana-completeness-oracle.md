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
apply cap, default 2000); `MTG_MANA_ORACLE_GATE_MAX` (gate-rejected plans checked per decision, default
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
| FiveColour | 8 | `SubsetWidensDomainBy` is all-or-nothing: a subset holding several domain wideners gets no widen when not all of them are payable | the domain widen credit | try the payable widener subsets when the flat gate fails | open |
| FiveColour | 1 | `HasteUnlockedManaOf` misses a hasted domain dork's own colours | the haste-unlock credit | add them | open |
| Fungus | 3 | a same-plan mana GRANT (Brightcap Badger) is not credited | the rock/grant credit | credit the granted bodies | open |
| Fungus, FiveColour | 8 | Concordant Crossroads' blanket haste does not unlock the plan's own dorks | the rock-credit stamp | credit them | open |
| Giants | 2 | `SubsetPayableSequentialImpl` refuses any non-cast action (an Equip {0}) | the sequential walk's kind filter, `SubsetHasUnmodelledSeqKind`, `OrderPaysSequentiallyImpl` | skip zero-cost non-mana activations | **fixed below** |
| Goblins | 1 | the colour-presence gate misses a planned sac outlet's colour | `WidenHaveWithSubsetRocks` | the outlet half of `MTG_SUBSET_SAC_COLOR` | **fixed below** (`MTG_SUBSET_SAC_OUTLETS`, after three exposed defects) |
| NivMizzetCombo | 5 | a mana rock that pays for another rock: the rock credit applies only when the board alone pays every rock | the `pool.CanPay(rock_costs)` guard (both walkers) | an exact sequential rock check from the credited pool | **fixed below** |
| NivMizzetCombo | 2 | Generator Servant's {C} through Izzet Signet: a {1}-fed ramp filter turns a {C} into coloured mana, under-counted by one per fed filter | the per-subset credits | relabel up to one {C} per fed filter as wild | open |
| EDF | 6 | the rescue's scalar ceiling omits the ETB-untap refund (Cloud of Faeries, Peregrine Drake) | `ef-scalar` pre-filter | add the refund | open |
| EDF | 4 | the odometer gate prices the untap refund off the pre-plan board, missing a same-selection land Aura on the refunded land | `BuildManaGateIndex`, `ManaPruneBound` | scale the Aura bonus by the untappers | open (verified by `MTG_SEL_MANA_GATE=0`) |
| EDF | 12 | the rescue walk is ONE greedy payment: Aether Hub's energy colour counted as cover, a painland's painless {C} never banked ahead of an untap, a coloured unit that must be banked before the untap, the tap-ahead's host and colour choice | the EDF sequential rescue | "Proposal X": when the greedy walk fails on an untapper subset, the oracle's bounded every-order payment search as the last rescue, its script stored on the plan and replayed by all three apply paths (`RealPayScope`) | open (one mechanism for all five classes) |

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
| EDF | 24 | 19 | the rescue-walk classes A-G (proposal X): real |
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

**Fix 5: the outlet half (`MTG_SUBSET_SAC_OUTLETS`, DEFAULT ON, 2026-10-29) -- held out until the three defects it
exposed were fixed.** The same credit for a creature sac OUTLET (Skirk Prospector's {R}, Utopia Mycon's any
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
* gi144 (seed 1330145, T3 -> T4 at d8 b0; T3 on both arms under the non-clairvoyant search), found on the
  re-measure, and gi3447 (seed 1343448, the same mechanism). The T3 kill's main 2 {Skirk Prospector, Siege-Gang}
  needs NON-FRONT continuations at two site-9 occurrences of one apply: Skirk's sacrifice burst at the post-entry
  occurrence, then Siege-Gang activations at its re-open. `Plan::bp_choice` / `bp_at` carry ONE deviation per apply,
  and an untargeted site-9 re-open applies nothing at all (not the canon), so a deviation at the first occurrence
  could reach no continuation at the second. The line was reachable only through the uniform arm (`bp_all`, rank 1
  at both), by luck; the outlets admitted more continuations ahead of the three-sacrifice burst (rank 1 -> rank 8)
  and the uniform arm no longer expressed it. **Fixed (`MTG_BP_DEV_SEQ`, DEFAULT ON):** breakpoint deviation
  SEQUENCES (`Plan::bp_pre`), opened at LATENT occurrences (reachable only by targeting them), one wave per deviation
  count; a full-line search runs them as a second pass bounded by the first pass's answer (bp-empty-arm-deferred-site.md
  6.1). At d8 b0 gi144 and gi3447 both win T3 again.
`SubsetSacColorSource` now credits the outlets (`MTG_SUBSET_SAC_OUTLETS`). Held out together with `MTG_BP_DEV_SEQ` (seed
1440001, every suite deck, EDF / Landfall / Fungus / Goblins 4,000 games each, 60,202 paired games against both levers
off): 36 faster / 3 slower -- Goblins 28 / 0 (3.7680 -> 3.7610), AccomplishedAlchemist 3 / 1, EDF 4 / 2, Landfall 1 / 0,
every other deck's win turns identical -- and every slower game wins on the base turn at d8 b0 (EDF gi1851, gi3772,
Alchemist gi69). On Melira the outlets alone are byte-identical to both off, so its movement is `MTG_BP_DEV_SEQ`'s.
Units 1.049x overall (Goblins 1.34x, Melira 1.20x: the second pass's cost; see bp-empty-arm-deferred-site.md 6.1). Measured on upstream 6b5ec5346; the
re-run on the rebased tree and what it still owes are in that section.

**Also found: the sac-colour fold is a guess.** `MTG_SAC_COLOR_FOLD` (on-board Lotus / Lotus Bloom) resolves the
colour at apply from a scarcity score over the accounting pool, which counts a dual land toward both its colours; the
gi27 trace shows one demand folding to four different colours. Where it guesses wrong the plan drops a cast the
fanned colour pays -- an APPLY-DROP the oracle can see. The lossless shape is the one the Garth crack now uses (one
variant per colour, credited exactly so a useless colour dies at the gate).
