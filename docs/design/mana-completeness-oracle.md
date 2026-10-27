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
DOMINATED-LIFE (56, every one "+2 life by paying with Ancient Tomb:C"). Fix: use `dmgev::PainForTap` in
the unarmed branch too. It changes Fungus play (life only), so it goes through the normal GT flow --
pinned meanwhile by a `should_fail` test in `test/unit/test_mana_oracle.cpp`.

**38 decks (2026-10-27, `logs/mana_oracle/dev8`).** The 22 decks never run before add ORACLE-GAP 195 --
the oracle is not yet complete there: Melira Pod 185 (Birthing Pod's Phyrexian {G/P}, mana vs life),
Auras 2 (Horizon Canopy's pay-1-life tap; the engine line ends 1 life higher -- possibly another
tap-path pain split), Anti-Lifegain 8 (Grove of the Burnwillows' opponent lifegain). No quiet verdict on
those decks is trustworthy until their gaps are closed.

## 8. Next steps

0. **Fix the Ancient Tomb pain split** (section 7.1) and close the new decks' ORACLE-GAPs (Phyrexian
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

Buckets: `depth0/1/2/3+`, `capped`, `unfaithful`, `no-failed-payment`, `inconsistent`; plus `reorder`
(APPLY-DROP-ORDER: only another order pays) and `off-menu` (ENUM-GAP: a mana gate kept it off the menu),
counted alongside so the table answers "how many lost lines, of which kind". Exit line `=== MANA ORACLE
rollback census: ... ===`; `scripts/mana_oracle.py` adds a "Rollback census" table per deck.

**census1 (38 decks x 6 games, `logs/mana_oracle/census1/`):** 16 APPLY-DROP -- depth1 7
(AccomplishedAlchemist 3, NivMizzetCombo 2, EDF 1, Hinata 1), depth2 7 (Fungus 6, Hinata 1), depth3+ 2
(Fungus), depth0 0; none unfaithful, capped or inconsistent. `MTG_PAY_ROLLBACK` forced on rescues **0 of
16** (it declines with mana floating and on multi-mana sources). 13 of the 16 lost a continuation's cast
(need-later); 3 lost a declared one (Niv 2, Hinata 1). Off the menu: 82 ENUM-GAP (Alchemist 44, EDF 15,
Slivers 11, Niv 6, Mirrorwing 3, Selesnya 2, Landfall 1).

**What causes EDF's (viewer seed 3000 T4, the deck owner's manual-tap report):** Training Grounds {U} then
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
* Next: a "queue these taps" action. The tokens are already in `pays[].taps` (`tap=<name>#<num>:<face>[+aura]`);
  placing each before the step it pays for needs the payment -> action index recorded per payment.
