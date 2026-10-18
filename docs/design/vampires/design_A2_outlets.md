# Vampires — Stage 2 research, cluster A2: discard / sacrifice outlets + graveyard recursion

Cards: **Falkenrath Pit Fighter**, **Stromkirk Condemned**, **Voldaren Estate** (+ its **Blood** token),
**Bloodghast**. Read-only research against worktree `.claude/worktrees/vampires` @ 2176032dd. Every
file:line below is a hint into that tree; anchor on the symbol name.

Standing decisions honoured (ledger `docs/design/analysis-Vampire.md`): faithful model of every clause;
every new decision SEARCHED (no greedy pick at remaining depth > 0; a provider may only order/restrict);
discard buckets / cast order are USER-owned (proposed here, never adopted); goldfish-inert deferrals are
PROVISIONAL until the user signs off.

---------------------------------------------------------------------------------------------------

## 0. Cross-cutting findings (read these first — they shape every card below)

### 0.1 The discard choice is NOT searched anywhere today, and for this deck it must be

Every non-cleanup discard in the engine picks the provider ranking's FRONT:
`ChooseNonCleanupDiscardIndex` (`src/core/SpellEffects.h:13391`) → `rank.front()`; used by Burning-Fist's
`ApplyActivatePump` mode 1 (`SpellEffects.h:13466-13476`), Neheb (`src/ai/Combat.cpp:267`), Troyan's loot
(`SpellEffects.h:21796`). The no-greedy audit (`docs/design/no-greedy-in-search-window.md` row **new-4**,
re-examined at line ~595) accepted it as COMPLIANT only because its reach was *bounded* on Minotaur
(best-vs-worst bracket +0.00025 t/game). That argument does not transfer: here WHICH card is discarded
decides whether a madness creature is cast (A1), whether Bloodghast goes to the graveyard to return on
the next land, and which land stays in hand. The audit's own compliant fix for new-4 is "fan over the
provider's list" — that is what this design does, for the NEW activations only (Burning-Fist / Neheb /
Troyan stay byte-identical; extending the fan to them is the open audit item, the USER's call).

**Mechanism (shared by Pit Fighter, Condemned, Blood):** a new action-carried pin
`InternedName Action::discard_name` (place it in existing padding; `TurnSolver.h` Action ~l.300-420).
* CollectActions emits one variant per **distinct discardable class** in the PRE-PLAN hand. Class key =
  `(m_name, m_impulse_no_land)`; staged cards (`m_is_staged`, in exile) are never candidates. Two Swamps
  are one variant — a sound identity fold (same name ⇒ same definition ⇒ interchangeable; madness is
  granted per Vampire creature card by name, so it does not split a class).
* Order = `DecisionProvider::ActivationDiscardCandidates(state, src_def)` (NEW hook; default = the
  provider's existing `CleanupDiscardCandidates` order mapped to classes, no restriction). The hook may
  ORDER and RESTRICT (provider prune, measured) — never the engine.
* `plan_signature` MUST fold `discard_name` (the Gamble/`tutor_target` lesson in analyze-deck "Core
  invariant": a sub-decision missing from the signature silently collapses to the first variant).
* Apply: `ChooseNonCleanupDiscardIndex` gains an optional pinned name. Pin present and a copy in hand →
  that copy (first non-staged copy by hand order — identical by the fold). Pin present and NO copy →
  the activation is a full NO-OP decided BEFORE any cost is paid (the "real-card victim premise failed"
  rule in `ApplySacCreatureOutlet`, `SpellEffects.h:14148-14158`), never a substitution. No pin → the
  provider front (reachable only in human play, where `g_play_discard_chooser` overrides it, and in the
  depth-0 greedy, which is permitted).
* Subset guard `SubsetOversubscribesDiscards` (NEW, beside `SubsetOversubscribesSacFodder`): reject a
  subset whose pinned discards + hand casts of a name exceed that name's hand copies — those variants are
  degenerate duplicates of a smaller plan (sound, lossless).
* Human play: NO discard fan in the menu (one activation entry); the card is asked at resolution through
  the existing `discard` decision (`NonCleanupDiscardContext()` = source name) — the Natural Order
  "asked at resolution off the live board" doctrine (DECISIONS.md, World War Hulk section).
* Invariant to enforce (cheap counter + abort-in-debug, the `GreedyPermit` spirit): an autonomous apply of
  a pinned-kind activation (`sac_outlet_discard_cost`, `discard_team_pump_*`, `BloodDraw`) with NO pin
  while `greedywindow::InWindow` reports depth > 0 is a bug.

### 0.2 The shared discard hook (contract with cluster A1 / madness)

There is no single discard function today; discards are open-coded at ≥10 sites
(`AIEngine.cpp:4532` retrace, `:8820` searched cleanup trials, `TurnSolver.cpp:46053` retrace,
`SpellEffects.h:701` cleanup shed, `:13474` Burning-Fist, `:14774` Channel, `:14808` Transmute,
`SpellEffects.cpp:469` Gamble, `Combat.cpp:270` Neheb, `DecisionProviders.cpp:1027` shed reference).
A2 ASSUMES A1 provides ONE function and routes every A2 discard through it:

```
// Moves hand[hand_idx] out of the hand as a DISCARD (CR 701.8). Applies, in order:
//   madness (CR 702.35): the card is exiled instead and its madness trigger RESOLVES INLINE (the
//     searched cast-or-decline is A1's pin; decline -> graveyard), so a creature cast this way is on
//     the battlefield when this returns;
//   Progenitus replacement (MaybeReplaceGraveyardWithLibraryShuffle);
//   else -> owner's graveyard.
// Logs (LogDiscard / EmitPlayEvent / MTG_TRACE=discard). Returns where the card ended up.
DiscardDest DiscardFromHand(GameState&, int controller, int hand_idx, DiscardCause cause,
                            const std::string& source_name);
```

What A2 relies on: (a) **inline madness resolution inside the cost step**, which reproduces the real
order exactly — the madness trigger goes on the stack above the activated ability and resolves first, so
the madness creature is on the battlefield before the ability resolves (Scryfall ruling on Stromkirk
Condemned, 2016-07-13: *"If you discard a Vampire card with madness and cast it, it'll enter the
battlefield before Stromkirk Condemned's ability resolves"* — so it DOES get the +1/+1); (b) A2 calls the
hook only AFTER every other cost of the same activation is paid (Pit Fighter: the sacrifice first, so a
madness-cast creature can never be picked as that activation's victim; Blood: tap + mana + token
sacrifice first); (c) a declined madness card lands in the graveyard (that is the route by which a
discarded Bloodghast with Gorger out becomes a graveyard landfall watcher); (d) A1's madness pin rides
the SAME action as `discard_name` (e.g. `int8 Action::discard_madness`: -1 n/a / 0 decline / 1 cast),
fanned only for discard classes that have madness at that moment, and folded into `plan_signature`.

### 0.3 Sacrifice-victim choice is an engine greedy today

Value sac outlets bake ONE victim from `CanonicalSacVictim` (`SpellEffects.h:12050`, ranked by
`DefaultSacExpendabilityRank`, `:11944`) at enumeration (`TurnSolver.cpp` ~32550, comment "BOUNDED victim
selection (heuristic narrowing...)"). That is audit row **new-2/3** (engine violation; fix = provider
victim-candidates hook + host fan). The searched precedent is **Dina, Soul Steeper**
(`TurnSolver.cpp:32882`): one variant per distinct victim. Pit Fighter takes that shape, param-gated so
Goblins/Fungus/Melira stay byte-identical (extending it to them = the open audit item, USER's call).

### 0.4 Sac-outlet mana is paid WITHOUT the creature-ability pay scope — a real mis-model here

The trailing passes pay a `SacCreatureOutlet` with no `CreatureAbilityPayScope`
(`TurnSolver.cpp:48288-48292`, `AIEngine.cpp:5654-5657`), unlike `ActivatePump`
(`TurnSolver.cpp:48714`, `AIEngine.cpp:6179`). Secluded Courtyard's coloured mana is legal for "an ability
of a creature source of the chosen type" (`colored_creature_ability_ok`, `ProducesForPayment`
`SpellEffects.h:27352-27364`), so today Courtyard could not pay Pit Fighter's `{R}`. In this list `{R}`
sources for the ability are Blackcleave Cliffs (2) and Secluded Courtyard (4) only (Unclaimed Territory
and Voldaren are cast-only). **Fix:** open `CreatureAbilityPayScope` around both sac-outlet payments
(sources are always creatures). Byte-identical except for decks holding BOTH a `sac_creature_outlet` and a
`colored_creature_ability_ok` source — integrator must grep the suite (Minotaur/Pirates/Soldiers run
Courtyard) before claiming identity.

### 0.5 The main-2 held-land bar breaks Bloodghast's core line (USER rule — needs the user)

`HeldLandBarredFromMain2` (`TurnSolver.cpp:2951`; consumers `:5650`, `:62093`, `:62118`, `:62391`)
implements USER 2026-09-30 *"Only newly arrived lands in second main would be okay"*. Its stated premise is
that a held land played post-combat "re-derives a main-1 line (the same end-of-turn state) apart from
ordering effects -- landfall after a main-1 cast ...". For a GRAVEYARD landfall card that premise is false:
"main 2: Pit Fighter sacrifices Bloodghast (legal only after combat damage), THEN play the held land →
Bloodghast returns" ends the turn with Bloodghast on the battlefield (unsick next turn, Pit Fighter
fodder again, Vampire count for Voldaren, convoke fuel); the main-1-land line ends with it in the
graveyard. Main 1 cannot express it (Pit Fighter is illegal pre-combat). So the line is unfindable at any
budget. **Proposal (surfaced to the user, default = implement):** exempt a held land from the bar iff the
deck has a graveyard-landfall card (`deck_has_gy_landfall`) AND, at the main-2 state, one is in our
graveyard or an enumerated main-2 activation can put one there (Bloodghast on the battlefield/in hand
with a live sac/discard outlet). Same exemption inside main-2 breakpoint continuations. Everything else
keeps the USER rule.

### 0.6 Second main: required

Pit Fighter is a spectacle-class card (combat damage unlocks it). `GoldFishRunner::DeckUsesSecondMain`
(`src/runner/GoldFishRunner.cpp:56`) must return true for `activate_only_if_opp_lost_life` (beside
`spectacle_cost`, `:72`). Main 2 then also hosts: Bloodghast-return lines (0.5), Blood creation/cracks
post-combat, Condemned-as-pure-discard-outlet (madness / Bloodghast-to-graveyard), madness casts. If a
future VampiresProvider opts into `SecondMainNeedsDeferredCast` (`DecisionProvider.h:2202`) for perf,
`SecondMainUnproductive` must count these activations and a live held-land Bloodghast return as
"productive", or `MTG_M2_EMPTY_FAST` (`TurnSolver.cpp:67459`) skips them.

---------------------------------------------------------------------------------------------------

## 1. Falkenrath Pit Fighter — Tier 3

**Scryfall (verbatim):** name `Falkenrath Pit Fighter`; mana_cost `{R}`; cmc 1.0; type_line
`Creature — Vampire Warrior`; power 2 / toughness 1; keywords `[]`; oracle_text
`{1}{R}, Discard a card, Sacrifice a Vampire: Draw two cards. Activate only if an opponent lost life this turn.`
Ruling 2021-09-24: activatable if any opponent lost any life this turn, even if they also gained life.

| Clause | Model | Tier |
|---|---|---|
| `{R}` 2/1 Vampire Warrior | creature body (P/T, subtypes Vampire+Warrior) | 1 |
| Cost `{1}{R}` | `sac_creature_cost "{1}{R}"`, paid under `CreatureAbilityPayScope` (0.4) | 1 (+fix) |
| Cost "Discard a card" | NEW `sac_outlet_discard_cost: 1`; searched `discard_name` pin (0.1); via A1's `DiscardFromHand` (0.2), called after the sacrifice | 3 |
| Cost "Sacrifice a Vampire" (self legal) | `sac_creature_outlet` + `sac_creature_requires_subtype "Vampire"`; searched victim class (§1.2) | 2-3 |
| "Draw two cards" | `sac_outlet_draw: 2` → `TrickDraw` (`SpellEffects.h:18103`, `DrawIntoHand(...HandEntryReason::Draw)` → hand-entry breakpoint) | 1 |
| "Activate only if an opponent lost life this turn" | NEW `activate_only_if_opp_lost_life: true` reading `GameState::opponent_lost_life_this_turn` (`GameState.h:288`; reset `GameEngine.cpp:255`, `TurnSolver.cpp:50325`; the spectacle flag) at enumeration, site-9 key, and the pre-pay live check in both worlds | 2 |
| No `{T}` ⇒ repeatable; legal while summoning-sick | no tap; repeats reached through the draw breakpoint (§1.2) | — |

### 1.1 cards.json
```json
{
 "name": "Falkenrath Pit Fighter",
 "mana_cost": "{R}",
 "types": ["Creature"],
 "subtypes": ["Vampire", "Warrior"],
 "keywords": [],
 "power": 2,
 "toughness": 1,
 "oracle_text": "{1}{R}, Discard a card, Sacrifice a Vampire: Draw two cards. Activate only if an opponent lost life this turn. [Repeatable creature-sac VALUE outlet (sac_creature_outlet, sac_creature_requires_subtype Vampire -- itself legal, sac_outlet_draw 2 through TrickDraw, so the two cards open a hand-entry breakpoint and a further activation is a fresh searched decision). The discard is an ADDITIONAL COST (sac_outlet_discard_cost 1), routed through the shared discard hook AFTER the sacrifice so a madness card cast from it can never be this activation's victim. WHICH card is discarded and WHICH Vampire dies are both SEARCHED: one variant per distinct hand-card name x per identity-folded victim class (human play asks both at resolution: the `discard` and `sacrifice` decisions). activate_only_if_opp_lost_life reads opponent_lost_life_this_turn (any loss counts, per the 2021 ruling), so in a goldfish it is live after combat damage -- the deck uses the searched second main (DeckUsesSecondMain). The {1}{R} is paid as a creature-source ability, so Secluded Courtyard's coloured mana may pay {R}; Unclaimed Territory's and Voldaren Estate's may not. PROVISIONAL deferral: instant-speed timing collapsed to the main phases' trailing passes (inert vs the passive opponent -- see the A2 design).]",
 "template": "custom",
 "parameters": {
  "sac_creature_outlet": true,
  "sac_creature_cost": "{1}{R}",
  "sac_creature_requires_subtype": "Vampire",
  "sac_outlet_draw": 2,
  "sac_outlet_discard_cost": 1,
  "activate_only_if_opp_lost_life": true
 }
}
```

### 1.2 Search design

**Emission** (CollectActions value-outlet block, `TurnSolver.cpp` ~32476-32900; Dina's fan at `:32882`
is the template). Gate the whole new shape on `sac_outlet_discard_cost > 0` (byte-identical elsewhere):
1. **Legality first** (shared predicate `SacOutletLive(state, src, def)`, NEW, read by enumeration, by
   the `kActSacOutlet` key in `CollectActivationKeys` (`TurnSolver.cpp:21414-21421`) and by BOTH apply
   sites before paying): `opponent_lost_life_this_turn` ∧ hand has ≥1 non-staged card ∧ ≥1 legal
   Vampire victim. (Today an outlet whose cost cannot be completed would pay mana then no-op.)
2. **Source pooling (sound fold):** the ability has no `{T}` and no source-referencing payload, so which
   Pit Fighter activates is unobservable. Emit the family once, from the canonical (oldest, by
   `LandfallTargetFoldKey`-equal class) Pit Fighter; one activation per plan (mutually exclusive variants
   share `sac_source_id` → one `ActivationFamilyKey` group, the SacOutletPool shape `TurnSolver.cpp`
   ~32700). Further activations: the draw is a hand entry → breakpoint → the continuation enumerates a
   fresh activation with fresh victim/discard choices (the Recruitment Officer "one at a time through the
   put-in-hand re-solve" shape, `Permanent.h` `ActivatedDig` note). No K-count block (each activation's
   picks are their own decisions — the no-greedy rule).
3. **Victim axis:** one variant per identity class of legal Vampire victims on the pre-plan board
   (key = `LandfallTargetFoldKey`, `SpellEffects.h:7044` — full-state, conservative: tapped,
   entered_this_turn, counters, temp P/T, attachments, token creator; an unkeyable permanent is its own
   class), baking `sac_victim_id`. The canonical source itself is a legal class. Order =
   `DecisionProvider::SacOutletVictimCandidates(state, src, need_sub)` (NEW; default = all classes by
   `SacExpendabilityRank`; the provider may order/restrict).
4. **Same-plan victims (completeness):** a creature the SAME main-2 plan casts (e.g. "cast Vampire of the
   Dire Moon, then sacrifice it, keeping the attackers") is invisible to pre-plan enumeration and site 9
   does not fire (the ability is not newly available). Add Pit Fighter to the post-dedup re-point axis
   that already does this for Natural Order (`AppendSubdecisionAxes`, `TurnSolver.cpp:60190-60290`,
   `MTG_SAC_CREATURE_AXIS`, default ON): re-point `sac_victim_id` at a Vampire the plan casts strictly
   earlier, one per distinct name, base plans only.
5. **Discard axis:** cross each victim variant with the discard classes (0.1), and with A1's madness pin.
   Width per activation ≈ V_classes × H_names × (1..2). Typical main 2: V≤4, H≤6.
6. `a.eval` (ordering + depth-0 only; the rollout scores the truth): `(sac_outlet_draw −
   sac_outlet_discard_cost)·DMG − victim_loss`, where `victim_loss = 0` for a graveyard-landfall victim
   with a land in hand and a drop left (this turn or next), else the victim's effective power·DMG. The
   bare `draw·DMG` credit (Psychotrope's convention, which eats 1/1 tokens) would make the greedy leaf
   sacrifice attackers for cards.

**Apply** (shared `ApplySacCreatureOutlet`, `SpellEffects.h:14060`, called from both trailing passes):
`SacOutletLive` → pay mana (under `CreatureAbilityPayScope`) → sacrifice the baked victim (existing
code; a missing REAL-card victim no-ops as today) → `DiscardFromHand(pinned)` → `TrickDraw(2)`.
Sacrificed Bloodghast lands in the graveyard via the existing `graveyard.push_back(victim)` →
`OnCreatureDies`; it is then a graveyard landfall watcher (§4).

**Timing.** Main 1: illegal (no pre-combat life loss in this list — verified across all 16 missing
cards' oracle; Asylum Visitor loses OUR life). During combat after first-strike/regular damage ≡ start of
main 2 for every outcome in a goldfish (cards drawn in combat are only castable in main 2; sacrificing an
attacker before regular damage only loses damage) → collapsed to main 2 (PROVISIONAL, §6).

**Beyond the horizon** (depth ≤ 0, `GreedyPermit` HorizonLeaf/D0Runner): Solve picks among the same
variants by `eval` — i.e. provider-front victim/discard, and the netted eval above keeps the greedy from
eating attackers.

### 1.3 C++ changes
* `CardDatabase.h/.cpp`: `int sac_outlet_discard_cost = 0;` `bool activate_only_if_opp_lost_life = false;`
  (+ `BuildParamsFromJson`, `CardDatabase.cpp` ~1632 region).
* `TurnSolver.h` Action: `InternedName discard_name;` (+ A1's `discard_madness`). `plan_signature` fold.
* `SpellEffects.h`: `SacOutletLive`; `ApplySacCreatureOutlet` discard step + pinned-name
  `ChooseNonCleanupDiscardIndex` overload; `DefaultSacExpendabilityRank` untouched.
* `TurnSolver.cpp`: emission (§1.2 1-6); `AppendSubdecisionAxes` re-point extension;
  `SubsetOversubscribesDiscards`; `CreatureAbilityPayScope` at `:48288`; `kActSacOutlet` key uses
  `SacOutletLive`.
* `AIEngine.cpp:5654`: `CreatureAbilityPayScope` (executor twin).
* `DecisionProvider.h/DecisionProviders.cpp`: `SacOutletVictimCandidates`, `ActivationDiscardCandidates`
  (defaults = no narrowing).
* `GoldFishRunner.cpp:72`: `if (def->params.activate_only_if_opp_lost_life) return true;`.

---------------------------------------------------------------------------------------------------

## 2. Stromkirk Condemned — Tier 3

**Scryfall (verbatim):** name `Stromkirk Condemned`; mana_cost `{B}{B}`; cmc 2.0; type_line
`Creature — Vampire Horror`; 2/2; keywords `[]`; oracle_text
`Discard a card: Vampires you control get +1/+1 until end of turn. Activate only once each turn.`
Rulings 2016-07-13: a madness Vampire discarded to it and cast enters BEFORE the ability resolves; the
affected set is fixed as the ability resolves — Vampires you begin to control later don't get +1/+1.

| Clause | Model | Tier |
|---|---|---|
| `{B}{B}` 2/2 Vampire Horror | body | 1 |
| Cost "Discard a card" (no mana, no `{T}`; legal while sick) | NEW ActivatePump **mode 4**; searched `discard_name`; `DiscardFromHand` | 3 |
| "Vampires you control get +1/+1 until EOT" | NEW `discard_team_pump_power 1` / `_toughness 1` / `_subtypes ["Vampire"]`: `temp_power_bonus/temp_tough_bonus += 1` on every Vampire we control AT RESOLUTION (after the inline madness cast) — `CardHasSubtype(.., "Vampire") || AnimatedAllTypes()`; tokens included | 2 |
| "Activate only once each turn" (per object) | NEW `activate_once_each_turn: true` + `bool Permanent::once_per_turn_used` | 3 |

### 2.1 cards.json
```json
{
 "name": "Stromkirk Condemned",
 "mana_cost": "{B}{B}",
 "types": ["Creature"],
 "subtypes": ["Vampire", "Horror"],
 "keywords": [],
 "power": 2,
 "toughness": 2,
 "oracle_text": "Discard a card: Vampires you control get +1/+1 until end of turn. Activate only once each turn. [Searched main-phase Action::Kind::ActivatePump mode 4 (discard-cost team pump): no mana and no {T}, so it is legal the turn Condemned is cast (reached in the same main through breakpoint site 9). WHICH card is discarded is SEARCHED (one variant per distinct hand-card name; human play: the `discard` decision at resolution), through the shared discard hook, so a madness Vampire discarded to it is cast and is on the battlefield before the pump resolves (2016 ruling) and gets +1/+1. The +1/+1 lands on every Vampire we control AS THE ABILITY RESOLVES (temp P/T, cleared at cleanup); later arrivals get nothing. Once-per-turn is per copy (Permanent::once_per_turn_used, reset at our untap in both worlds). Deliberately NOT team_pump_cost, so the combat firebreathing converter can never spend it. PROVISIONAL deferral: instant-speed timing collapsed to the main phases' trailing passes (a combat activation buys nothing a main-1 one does not vs the passive opponent).]",
 "template": "custom",
 "parameters": {
  "discard_team_pump_power": 1,
  "discard_team_pump_toughness": 1,
  "discard_team_pump_subtypes": ["Vampire"],
  "activate_once_each_turn": true
 }
}
```

### 2.2 Search design
* **Emission** (ActivatePump block, `TurnSolver.cpp:32331-32472`): mode 4 when
  `discard_team_pump_power|toughness > 0`, source unused this turn, hand non-empty. No mana ⇒ the
  `kmax` mana bound is moot. **Pooled interchangeable copies** (sound symmetry fold): unused Condemned
  copies in one identity class are one source; emit count k = 1..n_unused, each with a discard MULTISET of
  size k (names in non-decreasing canonical order — the symmetry breaker), so two Condemned give
  C(H+1,2)+H variants instead of (H+1)² ordered duplicates. Human play: k = 1 only (re-prompt is more
  expressive — the blink-count precedent), discard asked at resolution.
* **When:** whether to activate = subset membership; WHERE = the plan's trailing pass (after the plan's
  casts, so every Vampire cast this main gets the bonus — the maximal set). A Condemned cast this main is
  reached via site 9 (`PostEntryActivationPending`, `TurnSolver.cpp:21587`, delta rule) — needs its key:
  `kActTeamPump` in `CollectActivationKeys` must also key mode 4 (unused ∧ hand non-empty). Main 2
  activation (pure discard outlet: madness, Bloodghast-to-graveyard before a main-2 land) is enumerated
  the same way. Interleavings that matter ("Blood discards Bloodghast → land → Bloodghast back →
  Condemned pump includes it") are reached through the Blood draw's hand-entry breakpoint (the
  continuation plays the land first, then pumps).
* **eval** (ordering/depth-0): main 1 `(#Vampires that can still attack this turn)·DMG − DMG`; main 2
  `−DMG` (+ provider credit when the pinned discard is a castable madness card or a returnable Bloodghast).
* **Apply** (`ApplyActivatePump` mode 4, `SpellEffects.h:13452`, both trailing passes): check unused ∧
  pinned card present → set `once_per_turn_used` → `DiscardFromHand` (madness inline) → pump the Vampires
  on the battlefield NOW. Executor/rollout share the body (`AIEngine.cpp:6172`, `TurnSolver.cpp:48709`).
  Keep the `CreatureAbilityPayScope` (harmless, no mana).

### 2.3 C++ changes — new Permanent field checklist (`once_per_turn_used`)
`Permanent.h` (beside `loyalty_activated_this_turn`, l.473); reset at OUR untap in BOTH worlds
(`GameEngine.cpp:330-338`, `TurnSolver.cpp:~50401`); BuildSimKey per-permanent bits
(`TurnSolver.cpp:615-632`, next free bit 1<<19); fold refusals `TurnSolver.cpp:13035-13043`
(foldcensus), `Dominance.h:1166/1218`, `SpellEffects.h:7057` (`LandfallTargetFoldKey`), `:32448`
(`RemovalSelfTargetFoldKey`). Params: `CardParams` + JSON reader. Converter isolation: the combat
firebreathing converter keys on `team_pump_cost` (`SpellEffects.h:16189`) — the new params never set it.

---------------------------------------------------------------------------------------------------

## 3. Voldaren Estate (+ Blood token) — Tier 1 (mana) + Tier 3 (Blood)

**Scryfall (verbatim):** name `Voldaren Estate`; mana_cost `""`; cmc 0.0; type_line `Land`; keywords
`[]`; oracle_text:
`{T}: Add {C}.` / `{T}, Pay 1 life: Add one mana of any color. Spend this mana only to cast a Vampire spell.` /
`{5}, {T}: Create a Blood token. This ability costs {1} less to activate for each Vampire you control. (It's an artifact with "{1}, {T}, Discard a card, Sacrifice this token: Draw a card.")`
Rulings: a Blood token can't be sacrificed to pay multiple costs; "a Vampire spell is a spell with the
creature type Vampire on its type line".

| Clause | Model | Tier |
|---|---|---|
| `{T}: Add {C}` | `produces` includes C; painless (painland reading, `ManaPayment.cpp:465-496`) | 1 |
| `{T}, Pay 1 life: any colour` | `produces` W,U,B,R,G + `tap_self_damage: 1` — unarmed decks take it as raw life LOSS (not damage), exactly "pay life"; the Fiery Islet precedent | 1 |
| "Spend only to cast a Vampire spell" | `colored_creature_only: true` (NOT `colored_creature_ability_ok`): coloured half pays creature SPELLS only (incl. madness casts and convoke, which are casts), never an ability — not Pit Fighter's `{R}`, not Blood's `{1}`/`{5}` (its `{C}` pays those). **Exact for this list:** every creature in the deck is a Vampire (all 13 creature type lines verified from the Scryfall JSON) and the only noncreature spell, Aether Vial `{1}`, is generic. Same disclosed collapse as Unclaimed Territory/Haven | 1 |
| `{5},{T}: create a Blood token` | NEW `PermAbilityMode::TapBlood` (TapInvestigate's shape) + `tap_blood_cost "{5}"`; creates a "Blood Token" named token def (`CreateBloodTokens`, the `CreateClueTokens` shape `SpellEffects.h:21486`) | 3 |
| "costs {1} less per Vampire you control" | NEW `tap_blood_cost_less_per_subtype "Vampire"`: generic −= count of permanents we control with subtype Vampire (changeling/animated-all-types included), floor 0, counted AT ACTIVATION; the `{T}` remains. Mirrors `tap_draw_cost_less_per_rad` at all its sites (`TurnSolver.cpp:5103, 31554, 81243`, `DecisionProviders.cpp:37460`) + recomputed at apply | 3 |
| Blood: `{1},{T}, Discard a card, Sacrifice this token: Draw a card` | NEW `PermAbilityMode::BloodDraw` on the "Blood Token" def, `blood_draw_cost "{1}"`: a `{T}` mode (ABSENT from `PermAbilityTaps`' exclusion list, `Permanent.h:287`); artifacts are never summoning-sick (`Permanent::CanTap`, `Permanent.h:704`) so a fresh Blood is crackable now; searched `discard_name`; sacrifice is a cost (`FireSacrificeWatchers`); `TrickDraw(1)` | 3 |

### 3.1 cards.json
```json
{
 "name": "Voldaren Estate",
 "mana_cost": "",
 "types": ["Land"],
 "subtypes": [],
 "keywords": [],
 "oracle_text": "{T}: Add {C}.\n{T}, Pay 1 life: Add one mana of any color. Spend this mana only to cast a Vampire spell.\n{5}, {T}: Create a Blood token. This ability costs {1} less to activate for each Vampire you control. (It's an artifact with \"{1}, {T}, Discard a card, Sacrifice this token: Draw a card.\") [MANA: produces all five colours plus {C}; tap_self_damage 1 with a {C} mode = the painland reading, so the {C} tap is free and each coloured tap costs 1 life (raw life loss -- 'pay', not damage). colored_creature_only restricts the coloured half to creature SPELLS (madness and convoke casts included), never an activated ability. 'Vampire spell' is simplified to 'creature spell': EXACT for the Vampires list (every creature is a Vampire -- type lines verified; the only noncreature spell, Aether Vial, costs generic {1} which the {C} pays identically). BLOOD: PermAbilityMode::TapBlood, cost {5} minus one generic per Vampire we control at activation (floor {0}; the {T} stays), creating the 'Blood Token' named token def; the Estate taps itself for the {T}, so it can never pay its own {5}. A searched activation (when/whether); a Blood made this turn is crackable this turn and its crack is offered through breakpoint site 9.]",
 "template": "basic_land",
 "parameters": {
  "produces": ["W", "U", "B", "R", "G", "C"],
  "colored_creature_only": true,
  "tap_self_damage": 1,
  "tap_blood_cost": "{5}",
  "tap_blood_cost_less_per_subtype": "Vampire"
 }
}
```
```json
{
 "name": "Blood Token",
 "mana_cost": "",
 "types": ["Artifact"],
 "subtypes": ["Blood"],
 "keywords": [],
 "oracle_text": "{1}, {T}, Discard a card, Sacrifice this token: Draw a card. [Token definition (created by Voldaren Estate; never in a decklist) -- the Clue Token / Treasure Token named-def precedent, skipped by audit_card_fields as a ' Token'. PermAbilityMode::BloodDraw: a {T} mode (an artifact is never summoning-sick, so a Blood made this turn is crackable now), cost {1} plus the discard and the self-sacrifice (all costs, CR 602.2b). WHICH card is discarded is SEARCHED (one variant per distinct hand-card name; human play: the `discard` decision at resolution), through the shared discard hook (madness fires). NOT fused into its creation the way a Clue is: the fusion would have to pick the discard, which is a decision.]",
 "template": "custom",
 "parameters": {
  "blood_draw_cost": "{1}"
 }
}
```

### 3.2 Search design
* **Mana:** standing mana-payment exemption — which land pays is the payer's business (greedy allowed).
  The Blood activation's `{T}` must be in the cast section's tap reserve so the plan's casts do not spend
  the Estate as mana (`ActivationTapReserveUnion` / `PlanSourceReserveScope`, `TurnSolver.cpp` trailing
  pass ~48230) — verify the reserve covers ActivatePermAbility `{T}` modes on LANDS (Conservatory is the
  existing case).
* **Blood creation (TapBlood):** whether/when = subset membership in any main. Pool interchangeable
  untapped Estates (existing interchangeable-source fold for PermAbility modes, see
  `BpAvailablePermAbilityModes` header). **Cost depends on the board at activation**, and the trailing
  pass runs AFTER the plan's casts, so the enumeration-time cost over-states it whenever the plan casts
  Vampires — a line "cast two Vampires, then Blood for {0}" would be priced unaffordable and never
  emitted, and site 9 would not rescue it (the ability was available at plan start). **Fix (sound,
  complete):** emit cost variants `c_k = max(0, 5 − (V0 + k))` for k = 0..min(#Vampire creature cards
  castable from hand, 5 − V0), each requiring the plan to cast ≥ k Vampires (subset guard
  `SubsetBloodCostUnderfunded`, NEW); at apply, recompute the true cost and pay THAT (a lower real cost
  just pays less; a higher one — a Vampire was sacrificed earlier in the trailing pass — fails to pay and
  the activation no-ops in both worlds). A provider `ActivationOrderRank` putting Blood creation before
  Pit Fighter sacrifices makes the common case exact (order only).
* **Blood crack (BloodDraw):** each Blood token is a source; pool identical Bloods (fold key WITHOUT
  `created_by_number`, which is behaviour-irrelevant for Blood — LandfallTargetFoldKey mixes it); one
  activation per plan with the discard fan; further cracks through the draw's hand-entry breakpoint. A
  Blood created mid-plan becomes crackable → NEW key (`kActModeBase` + table index; append LAST in all
  three ModeSpec tables, `TurnSolver.cpp:5071, 21466, 31442`, and the fourth at `:81225`) → site 9
  continuation offers the crack. **Do not** add a Clue-style fusion (`SpellEffects.h:21698-21745`): it
  would pick the discard.
* `PermAbilitySourceLive` (`SpellEffects.h:21508`) for BloodDraw: untapped ∧ hand non-empty ∧ pinned
  card present — checked before paying in both worlds.
* **eval:** TapBlood +small (a banked card, only with spare mana); BloodDraw `0·DMG` + provider credit
  for a "free" discard (excess land / returnable Bloodghast / castable madness).

### 3.3 C++ changes
* `Permanent.h`: `PermAbilityMode::TapBlood`, `BloodDraw` (append LAST — plan signatures and ActKey slots
  are keyed on the numeric value); `PermAbilityLabel` strings ("create a Blood token", "discard,
  sacrifice: draw a card"); `TurnSolver.cpp:36396` mode-name table; `:82799` names list.
* `CardDatabase.h/.cpp`: `std::optional<ManaCost> tap_blood_cost; std::string
  tap_blood_cost_less_per_subtype; std::optional<ManaCost> blood_draw_cost;`.
* `SpellEffects.h`: `CreateBloodTokens`; `ApplyPermAbility` cases (`:21669`); BloodDraw discard via the
  hook; `PermAbilitySourceLive` clauses.
* All ModeSpec tables + the cost-variant guard; `main.cpp` plan JSON: `act_label` (existing),
  optional `makes_blood` affordance (the `makes_clue` shape, `main.cpp:2091`).

---------------------------------------------------------------------------------------------------

## 4. Bloodghast — Tier 3

**Scryfall (verbatim):** name `Bloodghast`; mana_cost `{B}{B}`; cmc 2.0; type_line
`Creature — Vampire Spirit`; 2/1; keywords `["Landfall"]`; oracle_text
`This creature can't block.` / `This creature has haste as long as an opponent has 10 or less life.` /
`Landfall — Whenever a land you control enters, you may return this card from your graveyard to the battlefield.`
Ruling 2009-10-01: the landfall ability triggers only if Bloodghast is already in the graveyard when the
land enters. (CR 113.6k: an ability that moves "this card from your graveyard" functions only there.)

| Clause | Model | Tier |
|---|---|---|
| `{B}{B}` 2/1 Vampire Spirit | body | 1 |
| "can't block" | inert — the passive opponent never attacks, so we never block (Carrion Feeder's bracket note) — PROVISIONAL deferral | (4-inert) |
| Haste while an opponent has ≤ 10 life | NEW `haste_if_opp_life_at_most: 10` (§4.2) | 3 |
| Landfall from the GRAVEYARD, "you may return ... to the battlefield" | NEW `landfall_return_self_from_gy: true`; a graveyard watcher in the landfall chokepoint; "may" = YES by dominance (PROVISIONAL), human asked via `revive` | 3 |

### 4.1 cards.json
```json
{
 "name": "Bloodghast",
 "mana_cost": "{B}{B}",
 "types": ["Creature"],
 "subtypes": ["Vampire", "Spirit"],
 "keywords": ["Landfall"],
 "power": 2,
 "toughness": 1,
 "oracle_text": "This creature can't block.\nThis creature has haste as long as an opponent has 10 or less life.\nLandfall — Whenever a land you control enters, you may return this card from your graveyard to the battlefield. [LANDFALL FROM THE GRAVEYARD (landfall_return_self_from_gy): a GRAVEYARD watcher dispatched by the one landfall chokepoint (OnLandEntered) beside the battlefield watchers; it functions only in the graveyard (CR 113.6k) and only for a copy already there when the land enters (2009 ruling); the return goes through PutCardOntoBattlefield (untapped, a new object, enter cascade). The 'may' is resolved YES by a dominance argument (PROVISIONAL, see the A2 design: an extra untapped 2/1 Vampire against a passive opponent is never worse -- nothing in the list reads our graveyard except this card); human play is asked (the `revive` decision). The deck stamps deck_has_landfall, so the searched land position is live: a plan whose sacrifice/discard puts Bloodghast in the graveyard offers 'land after it'. CONDITIONAL HASTE (haste_if_opp_life_at_most 10): read against the opponent's life (team life in 2HG) at its entry and at the beginning of combat in both worlds. [PARTIAL: can't block is inert -- the passive opponent never attacks, so we never block. PROVISIONAL deferral pending user sign-off.]]",
 "template": "custom",
 "parameters": {
  "haste_if_opp_life_at_most": 10,
  "landfall_return_self_from_gy": true
 }
}
```

### 4.2 Design
**Graveyard landfall watcher.**
* `CardDatabase`: NEW per-definition byte `gy_landfall_watcher` (beside `landfall_watcher`,
  `CardDatabase.h:3930`, derived in `RebuildInternedIndex` ~`CardDatabase.cpp:416`). It must NOT be part
  of `DefHasLandfallTrigger` — otherwise a Bloodghast ON the battlefield reads as a battlefield watcher and
  every Bloodghast CAST would count as `PutsLandfallWatcher`, spawning useless land-position clones.
* `GoldFishRunner.cpp:1376`: `deck_has_landfall = garth || landfall || gy_landfall` (so `OnLandEntered`'s
  gate at `SpellEffects.h:7364` opens and `LandOrderMatters`, `GameState.h:1157`, is true).
* `FireLandfallTriggers` (`SpellEffects.h:7223`): after snapshotting the battlefield watchers, snapshot
  `players[controller].graveyard` cards whose def has `landfall_return_self_from_gy` (by m_number, at the
  moment of entry — the ruling). For each: autonomous → return (dominance YES); human →
  `g_play_revive_chooser` once per land entry over the snapshot (one 0/1 flag per copy, all preselected,
  `source` "Bloodghast (landfall)"). Return = erase from graveyard + `PutCardOntoBattlefield`
  (`SpellEffects.h:9499`) inside a `GraveyardEnterScope`-style frame as `PerformReturnFromGraveyard...`
  does (`:9536`). Battlefield watchers first, graveyard returns after (CR 603.3b: we order our own
  simultaneous triggers; no payload in this list interacts).
* **Dominance for the "may" (PROVISIONAL):** returning adds an untapped 2/1 Vampire. Against a passive
  opponent no permanent is ever worse off for it: lords/Captivating/convoke/Voldaren cost all want more
  Vampires; Pit Fighter wants it ON the battlefield as fodder; nothing in the list counts or reads our
  graveyard (A1 madness exiles from hand-discards, not the graveyard); declining only preserves an option
  (return later) whose every use is weakly worse than returning now (later = possibly summoning-sick next
  turn; now = unsick next turn). Present to the user as a provisional dominance collapse (the
  Avenger/Khalni "may → yes" precedent, `SpellEffects.h` landfall block).
* **State keys:** NEW `GyR_GyLandfallNames` bit (`GameState.h:85-115`), stamped in
  `DeckGraveyardReaders` (`GoldFishRunner.cpp:642-690`), folding the count of gy-landfall cards in OUR
  graveyard into BuildSimKey (`TurnSolver.cpp` ~63912, the `GyR_CreatureNames` shape) and the dominance
  build (`Dominance.h:1074`). Without it two states differing only in "Bloodghast in graveyard vs in
  exile/absent" share a TT/memo entry.

**Land position (the line Bloodghast exists for).**
* Main 1 / any main with the drop unused: `land_slot = kLandSlotAfterCasts (-1)` already plays the land
  AFTER the trailing activations (`ApplyPlanDirect`: trailing pass `TurnSolver.cpp:49167-49209`, deferred
  land `:49309`; executor `exec_play_deferred_land` `AIEngine.cpp:6688` — integrator: verify it also runs
  after `exec_trailing_activations`). But the -1 clone is emitted only for plans holding a
  `PutsLandfallWatcher` action (`TurnSolver.cpp:62479-62510`). Add a SEPARATE predicate
  `PutsGyLandfallFeeder(a)`: true for an activation whose `discard_name` names a gy-landfall card (with
  A1's madness pin ≠ cast), or whose `sac_victim_id` class is one; use it ONLY in the -1 emission test
  (`watchers_in`). Do NOT fold it into `PutsLandfallWatcher`: that predicate also counts the k-slot
  positions over CAST dispatch order (`TurnSolver.h:1102`), which activations never enter.
* Main 2: needs the held-land exemption (0.5).
* Cleanup-discarded Bloodghast returns next turn through the default land-first plan — no new work.
* Lines with an interleaving (discard Bloodghast to Blood → land → Bloodghast back → Condemned pump)
  are reached through the Blood draw's hand-entry breakpoint (the continuation plays the land at slot 0).

**Conditional haste.** `CanAttackFull`/`CanTapNow` (`SpellEffects.h:5045, 5099, 5118`; 92 call sites)
see only the battlefield vector, not opponent life. Proposal: a DERIVED `bool Permanent::cond_haste`
(beside `temp_haste`), read by both `CanAttackFull` overloads and `CanTapNow`, recomputed (set AND
cleared — never touching `temp_haste`, which other grants own) by NEW `RefreshOppLifeHaste(GameState&)`:
`cond_haste = haste_if_opp_life_at_most > 0 && players[1-ctl].life <= N`. Called at (a) the
begin-combat checkpoints in BOTH worlds beside `RefreshReconfigureCreatures` (`GameEngine.cpp:674`,
`TurnSolver.cpp:49996`) — exact for attack legality; (b) the entry of a permanent carrying the param
(`FireOwnEtbTriggers`, shared) so the main-phase projection (`PendingAttackDamage`,
`TurnSolver.cpp:9311`; CollectActions' haste stamp `:28053`) sees it. Residual (disclose): an opponent
life drop in main 1 AFTER Bloodghast entered is invisible to the projection until combat (the real
combat is exact; this list has no pre-combat burn). Derived ⇒ not folded into keys (opponent life is).
Bloodghast has no `{T}` ability, and "tap N untapped Vampires" costs (Captivating) / convoke are not
`{T}` abilities (CR 302.6), so haste matters only for attacking.

---------------------------------------------------------------------------------------------------

## 5. Viewer decisions (2c-ter)

| Card / choice | Bucket | Wiring |
|---|---|---|
| Pit Fighter: activate | A | existing `sacout=<outlet>` verb (LineSpec::sac_outlets; DECISIONS.md "Creature-sac outlets"); one menu entry, no fan in human play |
| Pit Fighter: which Vampire dies | A | `sacrifice` at resolution (`ChooseSacOutletVictimIndex`, need_sub "Vampire") |
| Pit Fighter: which card is discarded | A | `discard` at resolution (`ChooseNonCleanupDiscardIndex`, `NonCleanupDiscardContext()` = "Falkenrath Pit Fighter") — the Burning-Fist/Troyan row (DECISIONS.md:84) |
| Condemned: activate | A | ActivatePump, `activate:true`; a copy in hand makes `cast=` ambiguous → the generic `act=<name>` board-activation token (`LineSpec::board_acts`, CheckLine ~`TurnSolver.cpp:80163`) covers it; verify in 5h |
| Condemned: discard | A | `discard` at resolution |
| Voldaren: make Blood | A | ActivatePermAbility on a land, verbless `cast=Voldaren Estate` + `act_label` (Conservatory's Investigate precedent; a land in hand is never a cast) |
| Blood: crack | A | ActivatePermAbility on the token (`cast=Blood Token`, unambiguous) + `discard` at resolution |
| Bloodghast: return ("may") | A (new call site) | `revive` (`g_play_revive_chooser`, GameLogger.h:753; DECISIONS.md:92) called from the landfall chokepoint; one 0/1 flag per graveyard copy, all preselected |
| Land position for Bloodghast | A | existing `landafter=` (-1) |

Auditor manifest (`scripts/audit_viewer_decisions.py`): `sac_outlet_discard_cost → discard` (truthy, the
`firebreathing_discard` row :287); `discard_team_pump_power → discard` + a `BOARD_ACTIVATIONS` row
(`("activate", truthy, <zero-cost field>)` — the manifest's third field reads a mana cost; Condemned has
none, so confirm how a free activation is classified, else it reads UNVERIFIED);
`landfall_return_self_from_gy → revive` (the `ltb_return_creatures` row :205); `tap_blood_cost →
main_phase` and `blood_draw_cost → main_phase` + `discard` (the `tap_investigate_cost`/`sac_draw_cost` rows
:215/:264); `activate_only_if_opp_lost_life`, `sac_outlet_draw`, `haste_if_opp_life_at_most`,
`activate_once_each_turn` → inert-detail notes. Add DECISIONS.md rows for every new call site.

## 6. Proposed deferrals / collapses (ALL PROVISIONAL — user sign-off required; none is a Tier 1-3 drop)

1. **Bloodghast "can't block"** — inert: the goldfish opponent never attacks, so we never block
   (`GoldFishRunner.cpp:777`: goldfish creatures never block/attack).
2. **Instant-speed timing** of Pit Fighter / Condemned / Blood (and the Estate's Blood creation)
   collapsed to the main phases' trailing passes. Inert vs the passive opponent: a pre-attack combat
   activation ≡ the main-1 trailing pass (same Vampires pumped; a madness creature cast in combat could
   not attack anyway, in main 1 a hasty Bloodghast can); a post-damage combat activation ≡ main 2 (drawn
   cards are castable only in main 2). The Haunted Fengraf precedent.
3. **Bloodghast "may" resolved YES** by dominance (§4.2); human play is asked.
4. Disclosed EXACT collapses (not deferrals): Voldaren "Vampire spell" ≡ "creature spell" for this list;
   "pay 1 life" ≡ unarmed `tap_self_damage` (raw life loss).

## 7. Second-main relevance (2c-bis) — summary
`activate_only_if_opp_lost_life` ⇒ `DeckUsesSecondMain` true (spectacle bucket). Main 2 carries Pit
Fighter, Bloodghast returns (with the 0.5 held-land exemption), Blood creation/cracks, Condemned as a
discard outlet, madness casts. Searched main 2 is not free (Fungus measured 4.25x at d1/b3 under
`MTG_FORCE_USES_M2`); it is reachability here, not a preference.

## 8. Beyond-horizon playout (greedy permitted at depth ≤ 0 only)
The depth-0 Solve picks among the SAME enumerated variants by `eval`; no separate policy. Proposed
provider defaults (USER-owned; order only inside the window), for a `VampiresProvider`:
* discard order (activations and cleanup): Bloodghast when a land drop is available after the activation
  (this turn or next) → a madness creature whose madness cost is payable now (A1) → lands beyond the
  curve's need → highest-MV uncastable; never the last Gorger while madness payoffs remain (proposal);
* victim order: Bloodghast with a land available → summoning-sick lowest-power Vampire → Pit Fighter
  itself → tokens → lords/Gorger last (override `SacExpendabilityRank`; the base rank sacs TOKENS first,
  which here means 2/2 flying Bloodline Keeper tokens before a returnable Bloodghast);
* `ActivationOrderRank`: Blood creation → Condemned pump → Blood cracks → Pit Fighter.

## 9. Unit tests (`test/unit/`)
1. Voldaren: coloured tap pays a Vampire creature's `{R}` and costs 1 life; `{C}` tap painless; coloured
   refused for Pit Fighter's `{R}` and for a synthetic noncreature coloured pip; Courtyard's coloured DOES
   pay Pit Fighter's `{R}` (0.4) at both apply sites.
2. Blood creation: cost with 3 / 6 Vampires = `{2}` / `{0}` + `{T}`; the Estate never pays itself; cost
   counted at activation (plan casts two Vampires first → cheaper; cost-variant guard rejects a variant
   whose k Vampires were not cast); Blood token created, crackable same turn; site-9 key appears.
3. Blood crack: needs `{1}`+untapped+hand; discards the pinned name (two Swamps → one variant);
   missing pinned card → no cost paid; token gone; one card drawn; discard hook called once.
4. Pit Fighter: no emission and apply refusal before opponent life loss; legal after combat; victim
   classes identity-folded (two identical untapped tokens → one variant; tapped vs untapped → two);
   self-sacrifice legal; madness card discarded can never be the victim; draws 2; hand-entry breakpoint
   arms; `plan_signature` differs per (victim, discard).
5. Condemned: +1/+1 to Vampires present at resolution only; a Vampire cast after gets nothing; a madness
   Vampire discarded to it gets +1/+1 (A1 stub); once per turn per copy; resets at untap in both worlds;
   used vs unused copies not folded (BuildSimKey bit, fold refusals); never spent by the combat converter.
6. Bloodghast: returns on a played land only from the graveyard (not when on the battlefield, not when it
   entered the graveyard AFTER the land); hasty at opp 10, not at 11 (begin-combat refresh both worlds);
   `deck_has_landfall` stamped; `GyR_GyLandfallNames` makes the keys differ; human `revive` decline leaves
   it in the graveyard.
7. Search: land -1 variant emitted for a plan whose activation pins Bloodghast; main-2 held land offered
   only under the 0.5 exemption; no unpinned autonomous apply inside the window (counter = 0 on a sweep).
8. Lockstep: a recorded main-2 line (Pit Fighter sac Bloodghast + land) realises the same state in the
   executor and the rollout (the 5a mismatch harnesses).

## 10. Risks (top 3 first)
1. **Width / suite cost.** Pit Fighter ≈ V×H×(madness) variants per activation, plus Condemned/Blood
   families, in an always-searched main 2 — the 3x suite-cost gate is the real exposure. Mitigations here
   are all SOUND folds (source pooling, name fold, full-state victim fold, condemned multiset, one
   activation per plan + breakpoints instead of K-blocks); any narrowing beyond that is a provider prune
   needing an A/B and the user (budget doctrine: order/wave, never make a line unfindable).
2. **Lockstep surfaces:** name-pinned discards vs the plan's own casts (guard), the Vampire-count-
   dependent Blood cost (recompute at apply), the -1 land after the EXECUTOR's trailing pass, and A1's
   inline madness inside a cost step — each must be identical in `ApplyPlanDirect` and `AIEngine`.
3. **User-owned rules touched:** the main-2 held-land bar (0.5), the provisional collapses (§6), and
   searching non-cleanup discards for these activations (consistent with audit new-4's fix, but it means
   these discards stop following the provider front).
4. Haste via a derived bit refreshed at entry + begin-combat: projection-only staleness if a future list
   adds pre-combat burn.
5. Sac-outlet pay-scope fix (0.4) may move another deck that runs both Courtyard and a sac outlet.

## 11. Open questions for the user (work proceeds on the stated default)
* Q1 (0.5) Exempt held lands from the main-2 bar when a graveyard-landfall card can be in our graveyard?
  Default: YES (narrow exemption).
* Q2 (§6.3) Accept "Bloodghast may return" as YES by dominance in autonomous play? Default: yes,
  provisional; human is asked.
* Q3 (§6.1/6.2) Sign off "can't block" inert and the instant-speed timing collapse. Default: provisional.
* Q4 (0.1/0.3) Extend the searched discard/victim fans to Burning-Fist/Neheb/Troyan and Siege-Gang/
  Pashalik/Psychotrope (audit rows new-4, new-2/3)? Default: no — Vampires-only, others byte-identical.
* Q5 (§8) The proposed VampiresProvider discard / victim / activation orders are drafts for review.

## 12. Cross-cluster notes
* A1 (madness): the `DiscardFromHand` contract in 0.2; the madness pin rides the discarding action.
  Declining madness for Bloodghast is a REAL option (it returns free on the next land) — the madness axis
  must keep "decline", not collapse to "cast".
* C (Vampire Socialite): "each other Vampire you control enters with an additional +1/+1 counter as long
  as an opponent lost life this turn" must apply on Bloodghast's graveyard return (it goes through
  `PutCardOntoBattlefield`, so the replacement belongs on that shared entry path, not only on casts).
  Verified: no card in the list makes the opponent lose life before combat (Asylum Visitor costs OUR
  life; Mavren's tokens are made on attack), so Pit Fighter is main-2-only in practice.
