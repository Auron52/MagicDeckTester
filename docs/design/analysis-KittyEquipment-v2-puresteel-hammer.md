# Analysis ledger — KittyEquipment **v2-puresteel-hammer** (opened 2026-10-01)

Parent ledger: [analysis-KittyEquipment.md](analysis-KittyEquipment.md) (the v1 Stoneforge/Balan list —
1350 lines of heuristic doctrine, approved deferrals and cost work that still applies to the engine,
but **not** to this list's card pool).

## ⇥ RESUME HERE

**What this is.** The user added a NEW VERSION of KittyEquipment (`KittiesEquipmentV2.cod`, dropped in
the deck folder 2026-10-01) and asked to place it correctly, analyze it, optimize as needed, and
**move toward adoption**. The user is asleep for 7–8 h and asked for autonomous work; they will
**create references in the morning**, so *viewer-readiness is the hard overnight target*.

**Where it lives — and why not at top level.** `decks/KittyEquipment/v2-puresteel-hammer/KittyEquipment.cod`.
This follows the **Fungus `candidate-b-2026-09` shape**: an in-progress new version lives in a
subfolder while the shipped list stays at top level. The user named Fungus explicitly, then noted
*"Fungus is not a fully complete variant yet, since the value-leaf and profile are still in progress"*
— which is exactly this deck's state, so this is the Fungus position, not the Angels one.
* Files inside a variant are **named after the PARENT stem** (`KittyEquipment.cod`, not `...V2.cod`) —
  `tools/play/server.js` `listDecks()` looks up the parent stem inside each subfolder, and the engine
  resolves sidecars directory-relative off the profile path. The viewer will list it as
  **KittyEquipment — v2-puresteel-hammer**; its references belong under
  `references/KittyEquipment/v2-puresteel-hammer/`.
* **Promotion (the Angels/CritterLifegain shape) is DEFERRED until this list is actually adopted.**
  At that point v1 is archived as `decks/KittyEquipment/v1-stoneforge-balan/` *with its artifacts*,
  its 8 references move to `references/KittyEquipment/v1-stoneforge-balan/`, and v2 takes top level.
  Doing it now would have been actively wrong twice over: it voids `kitty` ground truth in all three
  regression tiers, and it strands `KittyEquipment.value.json` + the keep table — both fitted to v1's
  card pool — as **live** sidecars on a deck they do not describe (sidecar PRESENCE activates the
  value-leaf hybrid in play; it is not inert).

## The deck, and how it wins (USER's own strategy brief, 2026-10-01)

Verbatim intent — subagents do NOT inherit this, so pass it in every prompt:

> *"It 'goes off' by playing a lot of 0-mana equipment to draw cards with Sram or Puresteel and fill
> up the board with equipment. Then it uses Puresteel Paladin or Sigarda's Aid to equip them. Cid is
> in there to make a lot of the 1-mana equipment also cost 0."*
>
> *"So this ends up being a deck that now needs either draw card to be in hand pretty much. The
> mulligans will likely be quite different because of that."*

**Consequences that must survive into the artifacts:**
1. The engine is the *draw* engine — Sram (on CAST) and Puresteel (on ENTER) are the payoff, and both
   can be live at once (a cast Equipment with both out draws **2**).
2. **Equipping is free, not cheap**: Puresteel metalcraft (equip {0} at 3+ artifacts) or Sigarda's Aid
   (attach on enter, bypassing equip entirely).
3. **Cid turns the {1} equipment into {0}** — so the "free" artifact count is much larger than the
   printed {0} cards alone.
4. **The mulligan requires a draw engine in hand.** This is a *keep-condition* statement, and it is
   the single most important input to the eventual mulligan profile + `mulligan_flags`. Expect it to
   differ sharply from v1's table. Do not reuse v1's keep table.

### List (main 60)
4 Bone Saw · 4 Cathar's Shield · 4 Accorder's Shield · 4 Kite Shield · 4 Spidersilk Net ·
4 Colossus Hammer · 3 Golem-Skin Gauntlets · 2 Skateboard · 2 Shadowspear · 1 Dragonfire Blade ·
1 Deconstruction Hammer · 4 Sram, Senior Edificer · 4 Puresteel Paladin · 1 Cid, Freeflier Pilot ·
1 Dwalin, Weaponmaster · 1 Sigarda's Aid · 4 Ancient Den · 4 Remote Farm · 8 Plains

Sideboard (23) is **unreachable** — no mainboard card fetches from outside the game, so coverage
correctly does not scan it. Several v1 staples (Kor Duelist, Jitte, Bonesplitter, Armored Skyhunter,
Sol Ring, O-Naginata) sit there and are therefore **out of scope for this analysis**.

## Stage 1 — coverage (run 2026-10-01)

**14 missing, 5 already full.** Already implemented and carrying v1's approved deferrals:
Colossus Hammer, Puresteel Paladin, Plains, Shadowspear, Remote Farm.

Missing: Cid Freeflier Pilot · Sigarda's Aid · Ancient Den · Cathar's Shield · Accorder's Shield ·
Dragonfire Blade · Bone Saw · Kite Shield · Deconstruction Hammer · Golem-Skin Gauntlets ·
Skateboard · Spidersilk Net · Dwalin Weaponmaster · Sram Senior Edificer.

Stage 2 research fanned out 2026-10-01 as 11 Opus agents (the four cheap shields pooled into one,
since they are one shape; Bone Saw separate). Research fans out; **integration is serial** —
`cards.json` and the shared C++ cannot take concurrent edits.

## ⚠ Stage 4a hazard found BEFORE anything was measured — provider misroute via Cid

`DetectDecisionProvider`'s return precedence (src/ai/DecisionProviders.cpp ~11815–11845) is:

```
... if (goblin) { return g_goblins; }        <-- EARLIER
    if (equipment) { return g_equipment; }   <-- LATER
```

and the `goblin` signature is set by **`reduces_spell_subtype` ALONE** (src/ai/DecisionProviders.cpp
~11690). That param is the existing, correct machinery for "Equipment spells cost {1} less"
(implemented in `ManaPayment.cpp:1588` as a subtype-matched generic reduction) — i.e. **exactly what
Cid needs** per the user's brief.

**So implementing Cid the obvious way silently routes this whole deck to `GoblinsProvider`**, which
narrows `TutorSearchWidth` and applies goblin-tuned rankings. This is the **8th** instance of the
archetype-neutral misroute class already documented for Mirrorwing, StompySurprise, Minotaur,
Dragons, Melira Pod, Fungus and Giants (`DecisionProviders.h` ~270–287 names
`reduces_spell_subtype` as the param that misrouted both Dragons *and* Giants).

**Resolution (per the skill: route the deck ABOVE the offending branch):** move the `equipment`
return above `goblin`. Safe only if no deck sets BOTH flags — v1 kitty does not set `goblin`
(`equip_grants_haste`/`shroud` are deliberately excluded from these signatures as colourless
staples), and Goblins carries no equipment param. **This must be proven by digest, not argued:**
smoke `play-changed=0` on every other deck, Goblins especially.

Gate: `python3 scripts/provider_audit.py --check` must land this list on `Equipment`.
Note `provider_audit.py` only audits decklists that have a `.profile.json`, so the variant is
"NOT AUDITED" until Stage 4 writes one — re-run it after.

## Stage progress

| Stage | State |
|---|---|
| 1 coverage | **DONE** — 14 missing, sideboard unreachable |
| 2 research fan-out | **DONE** — 11 Opus agents, all returned |
| 2 integration (serial) | **DONE** — all 14 cards in `cards.json`, build green |
| 3 re-coverage to clean | **DONE — 0 missing, all 19 cards `full`** |
| 4a provider routing | **DONE — two misroutes found and fixed; `provider=Equipment` verified** |
| 2d-bis field audit | **DONE** — all hard fields match; 12/14 via snapshot, 2 HAND-VERIFIED (named) |
| 2d-bis cost audit (`audit_card_costs.py`) | **UN-RUN** — Scryfall rate-limited all night by the fan-out. The snapshot's hard-field check covers cost/cmc for the 12, and the 2 were hand-checked, so nothing is unverified — but the dedicated cost audit itself did not run |
| 4 baseline profile | **DONE** — written, cast-order verdict `STATUS_QUO_OK` |
| 4a provider routing | **DONE — two misroutes fixed; `provider=Equipment` verified; all 29 decks unchanged** |
| 5a mismatch harnesses | **DONE — nonconv 0/80, fd-diverge 0/60** |
| 5d claude-play sweep | **DONE — 8 games, Opus. Zero engine bugs, zero data divergences** |
| 5h viewer-readiness | **DONE with ONE KNOWN DEFECT** — deck is selectable and fully playable; the plan-space valve drops attach-equipment actions on artifact-heavy boards (see below) |
| 5b multi-depth sweep | **UN-RUN** — the deck does not finish at d5 in a usable time (see the performance finding) |
| 5c2 `leaf_tiebreak_check` | **UN-RUN** — same reason |
| 4-bis suite tiers | **NOT started** — user's call, and blocked on cost anyway (see open question 2) |
| value leaf / mulligan | blocked by the suite gate (confirmed: `suite_gate.py` refuses the variant path) |

Commits: `03c68e63` (onboarding + the three engine defects) and `208cb158` (the P/T label fix +
four scenarios + the snapshot refresh). **NOT PUSHED** — see open question 5.

## What was implemented (Stage 2, integrated serially 2026-10-01)

Tier 1 (cards.json only): **Ancient Den** (artifact land — the metalcraft count is a plain
type-mask walk, so `types:["Artifact","Land"]` counts with no code change), **Bone Saw**,
**Kite Shield**, **Spidersilk Net**, **Deconstruction Hammer**.

Tier 2 (new param + wiring):
* **Sram, Senior Edificer** — `draw_on_cast_subtypes`, matched on the cast card's printed SUBTYPE
  at the shared on-cast trigger site. On CAST, not on enter (a *put* Equipment must not draw).
* **Sigarda's Aid** — `attach_equipment_on_etb`, fired inside the existing `is_equipment` entrant
  gate in the enter cascade. **Reuses the existing `attach_host` decision**, so no new viewer
  wiring (all four sites verified present in the tree before relying on it). It TARGETS, so the
  legal host set excludes shroud — the one line a copy-paste from the attack-dig attach gets wrong.
* **Golem-Skin Gauntlets** — `equip_scale_power_per_equipment`, summed in `EquipBonusFor` in ONE
  pass (`sum(scalers) x count`, exact because every scaler on a host sees the same count).
* **Cathar's / Accorder's Shield** — `equip_grants_vigilance` + a new `CreatureHasVigilance`
  consumed at the single site that reads vigilance at all (the combat attack-tap). **Implemented,
  not deferred** — see the agent-conflict note below.
* **Skateboard** — `etb_tap_target_permanent` + `ResolveEtbTapMandatory` (mandatory, targeted,
  both sides; reuses the `target` board-click).
* **Dragonfire Blade** — `equip_cost_less_per_target_color`, applied in `EquipCostGenericNow` at
  **enumeration as well as payment** (the viewer renders only enumerated plans, so a payment-only
  discount leaves a legal line un-expressible).
* **Cid, Freeflier Pilot** — the cost reduction reuses the existing `reduces_spell_subtype`
  machinery; `gy_return_sacrifices_source=false` added so Cid TAPS without being sacrificed
  (repeatable, and it forgoes the attack); `Keyword::Jump` added as an inert tag because
  `KeywordFromString` **throws** on an unmapped keyword — `"Jump"` would otherwise crash card
  loading rather than degrade.

Tier 3: **Dwalin, Weaponmaster** — a new counter type. `Permanent::hone_counters`, a
`PutHoneCounters` chokepoint (so Doubling Season doubles them, CR 614), both fire sites (own-ETB
cascade + declare-attackers in **both** worlds), `DomAxis::HoneCounters` with `MoreDominates`,
the `sizeof(Permanent)` 296 → 304 ledger entry, nonzero-gated sim-key folding, and the
lockstep field compare.

## Three defects this analysis found in the EXISTING engine

1. **A dynamic equip bonus was unplayable in the pruned search.** `rider_delta` priced an attach
   off the flat `equip_power_bonus`, which is 0 for a per-equipment scaler; `if (rd > 0)` then the
   `kept` gate `continue`s, so the Equip action was **never emitted**. All three Golem-Skin
   Gauntlets would have been castable and never equippable — while human play, which opens every
   host, equipped them fine, so it was invisible in hand-played reference games. Fixed with a
   shared `EquipAttachDeltaFor` that both attach rankers call; it reduces to `equip_power_bonus`
   when no scaler is involved, so it is byte-identical for every other Equipment. **Confirmed by
   reading the three sites, not inferred.**
2. **Provider misroute, twice over, from ONE card.** See the Stage 4a section below.
3. **The attack projections would under-rate a hone attack.** They read `EquipBonusFor` without
   firing the attack trigger, so a `CountAttackHonePump` addend was needed in both — otherwise the
   search values the attack at its pre-trigger power, a textbook projection-vs-executor divergence.

## Stage 4a — TWO provider misroutes, both from Cid, both fixed

`DetectDecisionProvider`'s return order put `goblin` **and** `dragons` above `equipment`, and Cid
carries one archetype-neutral param for each:

| param | printed clause | signature it sets |
|---|---|---|
| `reduces_spell_subtype` | "Equipment and Vehicle spells you cast cost {1} less" | `goblin` |
| `gy_return_requires_subtype` | "{2},{T}: return target Equipment from your graveyard" | `dragons` |

Measured, not read: `mtg --batch` reported **`provider=Goblins`**, then after the first hoist
**`provider=Dragons`**, and only after hoisting `equipment` above *all* of
minotaur/dragons/giants/melira_pod does it report **`provider=Equipment`**. That is exactly the
Giants precedent ("removing either card would not fix it; the deck must be routed ABOVE both
branches") — moving past one branch at a time just finds the next one. This is the **8th and 9th**
instance of the misroute class the file already records for seven other decks.

Also fixed, a second-order trap the Cid research caught: Cid tied with `is_equipment` at cast-order
rank 8, and the within-tier tie-break is cheapest-first, so the {0}/{1} Equipment would have been
cast **before** the {1}{W} cost reducer and **the discount would never have been realised** — the
Minotaur Ragemonger bug verbatim. Cid now ranks 7, ahead of what it discounts; Sram joins Puresteel
at 6 on the same information-first logic.

**Verified no collateral:** `provider_audit.py` reports all 29 decks on exactly the providers they
had before the reorder.

## An agent conflict worth recording — vigilance is NOT inert

Two research agents disagreed, and resolving it is why integration is serial:
* the shields agent judged `vigilance` *effectively* inert, because the only creature with a `{T}`
  ability (Cid) looked to have no graveyard fuel;
* the Cid agent independently found the fuel **is** real — Sram + Puresteel overdraw, and the
  surplus is **discarded at cleanup**, which fills the graveyard.

So the line "attack with a vigilant Cid, then still pay its `{T}` rebuy" is reachable, and
vigilance is **implemented** rather than bracket-noted. Both agents were right that vigilance is
NOT covered by the usual "nothing blocks in the goldfish" argument — it is not a blocking keyword;
its live read is the attack-tap. (Contrast the same cards' `reach`, which stays a disclosed note
because `Keyword::Reach` has **zero** readers anywhere in the engine.)

Note this also means an existing over-broad claim in `CardDatabase.h` — that aura-granted
vigilance/reach are "provably INERT vs the passive opponent" — was only ever true because no aura
deck happened to have a post-combat `{T}`. Flagged, not changed (it would move other decks' GT).

## ⚠ THE HEADLINE PERFORMANCE FINDING — this deck is slow at d3, and that is the blocker

Measured 2026-10-01, ONE pooled batch, 60 games x 2 arms, seed 95001, **d3 / budget 250 ms** (the
shape the regression gate uses), `MTG_SLOW_GAME_MS` default so every game over 30 s reports itself:

| | slow games (>30 s) | worst completed game | worst overall |
|---|---|---|---|
| route ON | 17 / 60 | 250.5 s (gi=31) | **gi=35 never finished — killed after >30 min** |
| route OFF | 17 / 60 | 224.1 s (gi=31) | same gi=35 pathology |

**Read the two arms against each other, not in isolation.** They have the SAME 17 slow games in
the SAME order, and `gi=31` reports **identical `units=8033567`** on both — so the breakpoint route
below is NOT the cause. The cost is **intrinsic to the deck**: 28% of games exceed 30 s at the gate
cell, and at least one game is effectively non-terminating at >30 min.

**What this blocks, in order:** the 3x cost rule (`suite_gate.py --cost`) → suite membership →
**both generators**. Per the standing directive an intractable deck is *"a performance problem to
fix, not a deck to quietly exempt"*, so **getting this deck into range is the first goal, ahead of
the value leaf and the mulligan profile.** I did NOT run `suite_gate.py --cost` (it spawns a real
measurement run and the box was not free of my own probes); the numbers above already settle the
question qualitatively.

Repro for the pathological game:
`--seed 95036 --game-index 35 --games 1 --depth 3 --budget-ms 250` (profile attached).
Where to start looking: the deck casts ~20 free Equipment, so a turn's plan space is a large
product of (which Equipment) x (which host) x (cast order), and the draws re-enter the solve.
`MTG_TURN_CENSUS` is the per-decision work census built for exactly this question.

## The breakpoint route — a real quality hole, closed, and priced

The canon audit (`MTG_BP_CANON_AUDIT=1`, the repo's own enforcement for this doctrine) reported on
the pre-fix binary:

```
=== CANON AUDIT: 25282 canon defaults, 16306 UNCHALLENGEABLE (64.50%), 0 site-unmarked ===
  site 6   total=8976   UNCHALLENGEABLE=0       ok
  site 10  total=16306  UNCHALLENGEABLE=16306   <-- NO ROUTE INTO THE VARIANT MACHINERY
```
armed by every Equipment in the list (Bone Saw 2409, Shadowspear 2501, Spidersilk Net 2236,
Golem-Skin Gauntlets 2143, Cathar's Shield 1729, Skateboard 1594, …).

That **is** the deck's engine — cast a free Equipment, draw off Sram, cast another — so 64.5% of
canon defaults were taken as an unchallengeable `cands.front()` at any depth or budget. Sram added
a new site-10 arming ROUTE without the matching fan-out CLAUSE, which is the exact failure the
code's own comment warns about (*"If a route is ever added, ADD IT HERE TOO — nothing enforces
it"*). Site 6 (Puresteel's equipment-ETB draw) already had its clause and audited clean at 0.

Fixed by adding the cast-subtype watcher pre-scan + clause in `PlanOpensBreakpoint`, collecting the
**union of watched subtypes** over the battlefield *and* the plan's own casts — a battlefield-only
test would miss "cast Sram, then cast an Equipment", which is the common line here precisely because
the cast order ranks the watcher at 6 and Equipment at 8.

Behind **`MTG_BP_CAST_SUBTYPE`, default ON** (`=0` reverts). Default ON because the audit's own
instruction is to fix a nonzero `unreachable` *"by giving that site a clause … never by suppressing
the audit"*; a flag rather than a bare clause because it is a genuine widening whose price deserves
to stay measurable. **Measured price: ~10–15% wall on the slow games** (250.5 s vs 224.1 s on the
worst), with the slow-game COUNT unchanged at 17/60 — i.e. it does not create the tractability
problem above, it is a modest surcharge on top of it.

## Stage 5d — the claude-play sweep (8 games, Opus, 2026-10-01)

**Zero engine bugs and zero data divergences across 8 games.** Every agent read `cards.json` first
(Rule 0) and several then checked all 19 cards against Scryfall independently — two reported
"cards.json is exact for all 19 cards", including Dwalin's Scryfall reminder text confirming the
counter-intrinsic +1/+0 modelling. Win turns: Claude 4,3,none,5,4,none,7,4 vs engine 4,5,none,7,5,
none,8,6.

**What the sweep CONFIRMED working** (each independently arithmetic-checked, which is why it is
worth as much as the flag list):
* **Sram draws on CAST, exactly** — 7 separate batches of 3,1,4,4,2,2,1 draws for the same counts of
  Equipment casts, and correctly silent for creatures and for Sigarda's Aid (an Enchantment with no
  Aura subtype).
* **Sram ⊕ Puresteel stack to W+1** — one cast Equipment with 1 Paladin + 1 Sram drew exactly 2,
  confirmed in three separate games; 2 Paladins + 3 Equipment drew 6.
* **Puresteel metalcraft equip {0}** — one plan attached 7 Equipment whose printed equip costs total
  **20 mana** while leaving 2 lands untapped; another resolved 13 equips with zero mana available,
  Colossus Hammer's printed equip {8} included. Recognised at ENUMERATION inside the same plan that
  casts the Paladin, so the documented conservative bound did not bite.
* **Golem-Skin Gauntlets' dynamic scaler, at five different counts** — 1 → +1, 3 attached → +3,
  8 → +8, 9 → +9, 13 → +13, always counting itself, and the toughness-only shields correctly added
  **0 power while still counting +1 each toward the Gauntlets**. One agent put it best: a flat-bonus
  bug "would have read 16" instead of 29.
* **Dwalin's hone counters, both halves** — the enter half, and the attack half applying to THAT
  combat; one counter on **all 18** Equipment, attached and unattached. Exact combat arithmetic:
  33 = 2 base + 15 flat + 8 Gauntlets + 8 hone.
* **Sigarda's Aid** — fired for all 4 Equipment entering after it, each as an `attach_host`
  decision, and **fully bypassed Colossus Hammer's equip {8}**; this was one game's winning play.
* **Cid** — the reduction is GENERIC-ONLY and floored at 0: `{1}` Equipment really became free while
  the `{W}` Deconstruction Hammer was correctly NOT reduced. Cid was never sacrificed.
* **Dragonfire Blade's per-colour discount at BOTH sites** — a plan that is illegal at the
  undiscounted {4} was enumerated and then paid, on a mono-white host.
* **Skateboard** — the mandatory ETB surfaced as a both-sides `target` with the opponent's permanent
  as `heuristic_default` and the Skateboard itself among the options (resolving as a clean no-op);
  its haste grant let a just-cast creature attack.
* **Vigilance grant** — a shielded attacker was still `tapped: false` after combat. (Worth noting
  because this is the clause the research wanted to defer.)
* **Metalcraft correctly INACTIVE as a negative control** — with 11–13 artifacts but no Puresteel, a
  Colossus Hammer equip was never offered at 3, 4 or 5 mana.
* Ancient Den counts as an artifact; Remote Farm's depletion + self-sacrifice; Shadowspear lifelink
  per-creature; the duplicate-legend cast prune on Sram; mana legality exact in both directions.

**Not exercised**, so claimed neither way: the `+0/+N` toughness grants (nothing in a goldfish
damages our creatures) and Cid's `{2},{T}` rebuy in live play (the graveyard fills only by overdraw
discard, and games end turns 3–7) — the latter is now covered by three constructed scenarios instead.

### The one real defect the sweep found — and it is a VIEWER defect, not a rules one

**The human-play plan-space valve deletes every "attach an unattached Equipment" action.** Censused
over the FULL enumeration (not the display slice): across five consecutive frames the only equips
offered were **moves of already-attached Equipment off the live attacker onto a summoning-sick
body**, while an unattached Colossus Hammer and two Golem-Skin Gauntlets got zero actions. The
valve's own stated guarantee — *"every action is still reachable one click at a time"* — did **not**
hold; `dropped_groups` stayed 13–18 throughout. Measured cost in that game: a turn-3 kill (26 power
available vs 20 needed) became a turn-4 one. Same class as the Prevent-Damage Genesis-Wave valve
drop that was confirmed and fixed previously.

**Why: the bound is an odometer PRODUCT and it is wildly wrong on this deck shape.** Measured at one
frame: `positions_full = 4.2e13` against ~10^5 plans that actually materialise — a 7-order-of-
magnitude over-estimate, because ~20 interchangeable {0} Equipment multiply the odometer without
multiplying real outcomes. So the valve fires on frames it did not need to, and the groups it drops
(lowest `SituationalCardRank`) are the equips, i.e. the deck's entire point.

**I tried the obvious fix and it is REFUTED — do not retry it.** I added a deck-scoped override for
the valve's positions bound, measured it, and reverted it:

| bound | dropped_groups | wall for ONE frame |
|---|---|---|
| 65536 (default) | 11–18 | fast |
| 2e7 (the override) | **still 11** | fast |
| 1e10 | — | **>7 min, killed** |
| valve off | — | **>7 min, killed** |

So raising the bound either changes nothing or reinstates exactly the frozen-viewer hang the valve
was built to prevent. **The fix must be RANKING, not budget**: the valve should not be able to drop
the equip groups on a deck whose plan IS equipping. The natural lever is
`EquipmentProvider::SituationalCardRank`, but that hook **also feeds the autonomous
`MTG_PLAN_SPACE_CAP`**, so changing it can move ground truth and needs its own measured A/B — which
is why I did not ship it blind at 3am. **This is the top open engineering item.**

Practical note for playing references in the morning: this bites only once the board is
artifact-heavy (~20 artifacts, i.e. mid-go-off). Before that the menu is complete. There is no
usable env-var workaround — `MTG_VIEWER_PLAN_CAP=0` is correct in principle but takes minutes per
click on this deck.

### Fixed during the sweep: the protocol printed every creature's P/T with no Equipment

Found by the same sweep and **fixed + verified**: all four P/T label sites in the decision protocol
summed only the state-free reads, so a Sram wearing eight Equipment printed as **"(2/2)"** while
attacking for 19 — and this is the only P/T the protocol prints anywhere, so a player had no way to
read their own clock. Dwalin's hone counters were invisible by the same omission. The four sites now
share one `ProtocolLivePT`/`ProtocolPTLabel` helper (three of them were also missing the older
characteristic-defining-base and lord-bonus fixes, so the drift WAS the bug). Verified on the exact
reported frame: that label now reads **"Sram, Senior Edificer (16/15)"**. Chooser-guarded, so
ground-truth inert — smoke re-run after the fix: 104 passed, `play-changed=0`.

### A convergent heuristic finding (NOT a bug) — three agents found it independently

At `--depth 0` the cast-order heuristic **dumps free Equipment before deploying the draw engine**,
forfeiting the Sram/Puresteel draws that are the deck's whole plan; one game left Puresteel uncast
for two turns with spare white mana and 10+ artifacts out. One agent verified the enumerator's plan
**index 0** on the engine's own turn-7 board equips all 9 and wins that turn, so this is a *ranking
preference, not an enumeration gap*; another reported it disappears at depth >= 1. Two concrete
rules were proposed: *hold {0} Equipment while an uncast draw payoff is in hand*, and *play the
land that enters tapped on a turn whose mana you will not spend*. This belongs to
`heuristic-optimization` (measure, then adopt), not to rules correctness — and note the no-search
ranker also serves as rollout/leaf policy, so it is not purely cosmetic.

## Open questions for the user (surfaced, NOT blocking — work continues)

1. **Variant name.** I chose `v2-puresteel-hammer`. Fungus used a `candidate-<x>-<date>` slug for an
   in-progress list; the `v<N>-<slug>` form is what archived predecessors use. If you want the
   Fungus-style naming instead, it is a `git mv` of one folder.
2. **Regression tiers (4-bis) are the real fork.** The mandatory rule is *all three tiers or none*,
   and `kitty` currently holds accepted GT in all three **for the v1 list**. A variant is not the
   shipping list, so adding `kitty_v2` rows means new keys and new GT in all three tiers; swapping
   `kitty` to point at v2 voids v1's GT everywhere. **I am not touching the suite or GT tonight** —
   that is a hard-to-reverse, measurement-voiding action and it is your call. Note this *does* mean
   the suite-membership gate blocks value-leaf and mulligan generation for the variant until it is
   resolved (`MTG_ALLOW_UNTESTED_DECK=1` is a USER decision, never an agent's).
3. **`expected_buckets` / a `buckets.json` for v2.** Bucket ruling is user-only. v1's
   `KittyEquipment.buckets.json` is a ruling about v1's card pool and I will not copy or adapt it.
4. **Deferrals** raised by the card research are PROVISIONAL until you sign them off. The full list
   is in "Provisional deferrals" below.
5. **Nothing is pushed.** Two commits sit on `phase-1-2-deck-analyzer`. I did not push because you
   did not ask me to — but note this change set is exactly the platform-sensitive class CI exists
   for: a new `Permanent` field (296 -> 304), new sim-key fields, a new counter type and a new
   dominance axis. **The `determinism parity` job is the only Windows signal** and it cannot be
   checked from this container. Say the word and I will push and watch it.
6. **The next engineering goal, if you agree:** make the deck tractable at d3 (the gate cell). The
   sweep handed us a concrete lead — see "the branching lead" below — and it is the thing standing
   between this list and suite membership, the value leaf and the mulligan profile.

## The branching lead (why the deck is slow, with evidence from the sweep)

The sweep's agents independently reported the plan-space shape, and it lines up exactly with the
performance finding:
* turn 1 with five castable Equipment: **717 plans**, of which 120 are the orderings of one
  5-Equipment cast set — and on that board **every ordering is outcome-identical** (no Sram,
  Puresteel or Cid out, so no on-cast trigger and no cost reduction, and mana payment is forced).
* a mid-go-off frame: `positions_full` **4.2e13**, and another reported **94,478,400,000,000**
  combinations with 31,250 kept and 200 printed.
* one frame was `4095 = 2^12 - 1` plans that all `cast: (nothing)` and differ only in WHICH subset
  of 12 Equipment moves onto a summoning-sick creature.

That is the signature of a product over interchangeable items, which is precisely the shape the
standing "collapse wasted search" directive targets, and the precedent it cites — pooling the
interchangeable sac outlets, worst odometer 4.61e+03 -> 2.02e+03 — is the same move. Two candidate
collapses, both needing the usual soundness argument before adoption:
1. **Fold cast-order permutations of free, trigger-neutral Equipment** when no on-cast watcher and
   no cost reducer is on the battlefield and the casts are all {0}. Under those conditions the
   orderings are provably identical, so this is an identity fold, not a heuristic narrowing.
   CAREFUL: it stops being sound the moment a Sram/Puresteel/Cid is on the board or in the plan.
2. **Fold same-host / no-op equip moves** and subsets that differ only by which interchangeable
   +0/+N shield lands on the same host.
Measure with `MTG_TURN_CENSUS` (built for this) and gate on `units` moving, not on wall clock.

## Provisional deferrals — these need your sign-off

None were decided unilaterally as "fine"; each is listed with why it is inert.
1. **Deconstruction Hammer's granted activated ability** ("{3},{T}, Sacrifice: destroy target
   artifact or enchantment"). Two legs: the passive opponent never controls an artifact or
   enchantment, so no opponent target ever exists; and every own-side activation is strictly
   dominated (spends {3}, taps the attacker, eats the Hammer AND a second own artifact — which can
   flip metalcraft off — for zero upside, since this 60 has no sacrifice outlet, no artifact death
   trigger and no graveyard-artifact payoff). It is OPTIONAL, so unlike a mandatory ETB there is no
   forced negative to model.
2. **Cid's "and Vehicle spells"** half, and the "or Vehicle" half of its rebuy filter — zero
   Vehicles in this deck and zero in all of `cards.json`.
3. **Cid's Jump** ("during your turn, Cid has flying") — nothing blocks anywhere in the engine, and
   the engine has no turn-conditional keyword layer, so an unconditional grant would be unfaithful
   while buying nothing.
4. **Dragonfire Blade's "hexproof from monocolored"** — hexproof restricts only opponent-controlled
   spells and abilities, and the passive opponent casts and activates nothing. Deliberately NOT
   mapped to `equip_grants_shroud`, which in this engine is enforced against our OWN targeting and
   would have locked ~30 Equipment off the host.
5. **Spidersilk Net's reach** — `Keyword::Reach` has zero readers anywhere in the engine, and
   nothing blocks.
6. **Dwalin's first strike** — `Keyword::FirstStrike` has no readers at all and combat damage is one
   event. (Note the contrast: DOUBLE strike is not inert here, so this is specifically the
   first-strike-alone collapse.)
7. **Sigarda's Aid's flash clause** — the only two readers relax a "stack is empty" gate the
   goldfish never fails, and the deck holds zero Auras.
8. **The reachable empty-library case, disclosed rather than deferred:** with 20 free Equipment and
   W+1 draws per cast, the chain's branching factor goes supercritical at 3-4 Puresteels, so the
   library really can empty mid-turn — and both draw sites SKIP on an empty library rather than
   modelling CR 104.3c, so the engine would hand us a win where the rules give a loss. Previously
   disclosed as unreachable on other decks; on THIS list it is reachable.

## Approved deferrals (v2)

*(none yet — provisional deferrals from the card research land here pending USER sign-off)*
