# Analysis ledger — Fungus

Per-deck durable state for the `analyze-deck` run on `decks/Fungus/Fungus.cod`.
Written per the skill's "per-deck ledger" rule so the run survives compaction and hand-offs.

**Run started:** 2026-09-17, autonomous overnight session (user: "analyze the Fungus deck
autonomously overnight … when all stages are done you can feel free to launch the value-leaf
or performance tests to find any slow areas of the deck").

**Branch:** `phase-1-2-deck-analyzer`

---

## Decklist

```
4  Thallid Shell-Dweller       4  Sporecrown Thallid
4  Sporesower Thallid          4  Tukatongue Thallid
4  Thallid                     1  Psychotrope Thallid
4  Utopia Mycon                2  Mycoloth
4  Doubling Season             2  Wild Growth
19 Forest                      1  Essence Warden
3  Simic Growth Chamber
4  Beastmaster Ascension
--- side (unreachable: no wish effect) ---
1  Spore Flower                3  Essence Warden
```

Archetype: mono-green Thallid/Saproling go-wide tokens. Spore counters convert to Saproling
tokens; Mycoloth devours them into +1/+1 counters and pays them back multiplied each upkeep;
Doubling Season doubles both halves; Beastmaster Ascension turns the wide board into the kill.

---

## Stage 1 — Coverage check

`python3 scripts/analyze_deck.py decks/Fungus/Fungus.cod --coverage-only`

* **Sideboard: unreachable** (no mainboard wish effect) — correctly not scanned. The `Spore
  Flower` + 3 `Essence Warden` in the side are import residue, not live cards.
* **Already `full`:** Forest, Wild Growth (land-aura model), Essence Warden
  (`any_creature_enters_lifegain`).
* **`missing` (11):** Thallid Shell-Dweller, Sporesower Thallid, Thallid, Utopia Mycon,
  Doubling Season, Simic Growth Chamber, Beastmaster Ascension, Sporecrown Thallid,
  Tukatongue Thallid, Psychotrope Thallid, Mycoloth.

### Engine capability gaps found at Stage 1 (pre-research)

Grepped `src/cards/CardDatabase.h` / `src/core/Permanent.h`:

| mechanic | engine support today |
|---|---|
| spore counters | **none** |
| devour | **none** |
| quest counters | **none** |
| Doubling Season (token/counter doubling replacement) | **none** |
| `sac_creature_outlet` | exists |
| `dies_trigger_creates_tokens` + `dies_token_*` | exists |
| `upkeep_creates_tokens` + `upkeep_token_*` | exists |
| `etb_bounce_land` (Karoo) | exists |
| `lord_effect` + `lord_excludes_self` | exists |

Counter convention in this repo: a new counter KIND gets its own dedicated `int <x>_counters`
field on `Permanent`, param-gated so it is never inspected for other decks (byte-identical).
Precedents: `charge_counters`, `verse_counters`, `storage_counters`, `ice_counters`,
`age_counters`.

---

## Stage 2 — Implementation

### Research fan-out (11 agents, Opus)

Per CLAUDE.md "AGENT FAN-OUT IS EXPECTED" and the model-on-difficulty rule (card-data reasoning
→ Opus). One agent per missing card, each doing 2a (curl Scryfall) + 2b (rules skill) + tier
classification + 2c-ter viewer bucket, returning a compact draft. Integration is serial (the
skill forbids parallel writes to `cards.json` / shared C++).

| card | cost (Scryfall, verbatim) | tier | what it needs |
|---|---|---|---|
| Sporecrown Thallid | `{1}{G}` | **1** | cards.json only — `lord_effect`, `subtypes_affected: [Fungus, Saproling]` |
| Tukatongue Thallid | `{G}` | **1** | cards.json only — `dies_watch_includes_self` + `dies_trigger_creates_tokens` |
| Simic Growth Chamber | *(land)* | **1** | cards.json only — copy Azorius Chancery (7th Karoo) |
| Thallid | `{G}` | 3 | spore family |
| Thallid Shell-Dweller | `{1}{G}` | 3 | spore family + **first `Defender` in cards.json** |
| Sporesower Thallid | `{2}{G}{G}` | 3 | spore family + each-Fungus variant |
| Utopia Mycon | `{G}` | 3 | spore family + any-colour sac-for-mana |
| Psychotrope Thallid | `{2}{G}` | 3 | spore family + **new `sac_outlet_draw` payload** |
| Beastmaster Ascension | `{2}{G}` | 3 | quest counters + conditional anthem + lethal projection |
| Doubling Season | `{4}{G}` | 3 | the two doubling chokepoints |
| Mycoloth | *(pending)* | — | devour (research still running) |

**⚠ Card-data correction — I got this wrong, Scryfall settled it.** My research brief described
Beastmaster Ascension from memory as a *mandatory* trigger granting **+2/+2**. The real card is
"**you may** put a quest counter" and **+5/+5**. Verified by direct curl. Every downstream number
changes: seven attackers deal **42**, not 21, and with one Sporecrown out **three** 2/2 bodies are
already lethal from 20. This is exactly the claude-play Rule 0 failure mode (card recall is
unreliable) — recorded here so no later stage re-derives the wrong figure.

### New engine work, consolidated

**New `CardParams`** — spore family (`spore_upkeep_self`, `spore_upkeep_each_fungus`,
`spore_saproling_cost`, `spore_token_*`), quest family (`quest_counter_per_attacker`,
`quest_anthem_threshold/_power/_tough`), `doubles_tokens` + `doubles_counters` (two independent
flags: Parallel Lives is tokens-only, Corpsejack Menace counters-only), `sac_outlet_draw`,
`sac_outlet_add_mana_any_color`, and Mycoloth's devour params.

**Three defects the drafts found that would each silently weaken the deck** (all verified by me
in-tree, not taken on trust):

1. **The Goblins misroute** (flagged independently by three agents) — see the section above.
2. **A draw payload scores zero.** `TurnSolver.cpp:15144` sets
   `a.eval = (sac_outlet_damage + sac_outlet_creates_tokens) * DMG`. There is no `sac_outlet_draw`
   term, so Psychotrope's draw activation evaluates as *pure loss* (it gives up a body for
   nothing) and the search would never take it. The repo's convention is 1 card = 1 `DMG`.
3. **`PendingAttackDamage` cannot see Beastmaster Ascension's kill.** It is a `const` projection
   over the *current* state, so it calls `ComputeLordBonus` on the quest counters as they are
   *before* this combat's own attack triggers. A board at 6 counters with 7 attackers projects
   **7** damage where the real combat deals **42** — so the generic win-check
   (TurnSolver.cpp:19150) is blind to the deck's win condition on precisely the turn it matters.
   The `d>=1` search is correct for free (it mutates state then reads power), but the plan-level
   lethal recognizer is not. Fix belongs beside the existing `CountAttackTriggerLifeLoss` /
   `CountExalted` / Adeline-token terms, which solve the identical "attack triggers change *this*
   combat's damage" problem — plus its mirror in `CollectAttackingManaSources`.

**A missing draw breakpoint (quality, measured precedent).** `PlanOpensBreakpoint` has no site for
a draw off a sac-outlet activation, so the drawn card is dead until the next turn. The measured
cost of the identical omission for equipment draws: over 150 logged games, ~109 main-1 draws, and
a card not already in hand at turn start was cast in main 1 **exactly zero times**.

### Shared architecture (INTEGRATOR-BINDING — decided from the drafts, do not re-derive)

Four mechanics are new. They share two chokepoints; every card routes through them or Doubling
Season silently misses that card.

**1. Spore counters.** New dedicated `int spore_counters` on `Permanent` (repo convention for a
new counter kind: `charge_/verse_/storage_/ice_/age_counters` are the precedents), param-gated so
it is never inspected for any other deck → byte-identical.

Param family, adopted across all five spore cards so they compile as ONE mechanic:

| param | cards |
|---|---|
| `spore_upkeep_self` (int) | Thallid, Thallid Shell-Dweller, Psychotrope Thallid, Utopia Mycon |
| `spore_upkeep_each_fungus` (bool) | Sporesower Thallid only |
| `spore_saproling_cost` (int, =3) + `spore_token_*` spec | all five |

**2. The spore activation is a new `PermAbilityMode::SporeSaproling`, NOT a new `Action::Kind`.**
Two drafts disagreed here; `PermAbilityMode` wins because it reuses machinery that is already
wired end-to-end (CollectActions, ApplyPlan, the viewer's `activate: true` board thumb, and
CheckLine's `activations` SubChoice for the K axis). Structurally it is the
Drain/ExileTop/IceCounter shape: **no `{T}`, no sacrifice** → repeatable within a turn and legal
on a summoning-sick body (CR 302.6 restricts only `{T}` abilities). Add it to
`PermAbilityTaps()`'s false-list.

*It must NOT route through `SpendRepeatActivations`* — that helper's `if (per <= 0) break;` guard
exists to stop a **free** repeatable sink from non-terminating. The cost here is counters, not
mana, so K is bounded by `spore_counters / 3` instead.

**3. Doubling Season hooks at exactly two places:**
- **Tokens** → `CreateToken` (src/core/SpellEffects.h:3777) is *already* the single token
  constructor every site calls. Double there, **gated on the token's controller matching the
  Doubling Season controller** — `CreateToken` is also used to gift tokens to the opponent
  (Forbidden Orchard, Varchild's), which DS must not double. Per-token doubling is numerically
  identical to per-effect doubling, and N copies correctly give ×2^N.
- **Counters** → there is **no** shared counter helper today (every site open-codes
  `++p.ice_counters` / `p.counters.push_back(...)`). One must be **added**
  (`AddCountersToPermanent`) and every Fungus counter placement routed through it.

**CR correctness, load-bearing:** removing three spore counters is a **cost paid on activation**
(CR 601.2h / 602.2b), so it is **not** doubled — DS doubles only counters being *put on*. Under
one DS a Thallid gains 2 spores/upkeep and makes 2 Saprolings per 3 spores = **4× throughput**.
That is precisely why the counters must be modelled for real and the card cannot be collapsed to
`upkeep_creates_tokens`.

**3b. Counter STORAGE: scalar ints, not `Counter::Type` enum values.** The drafts split on this
and it is the pivotal call, so here is the evidence and the reasoning.

*Both* routes have a key hole; they are just in different places:
- `BuildSimKey` folds the counters vector generically (`for (const Counter& ctr : perm.counters)`
  — type *and* count), so `Counter::Type` would be correct there for free. Scalars must be folded
  by hand (and `verse_counters` / `ice_counters` are in fact **not** folded today — latent holes).
- But `BoardSignature` (TurnSolver.cpp:1387) folds only `counters.size()`, **not** the per-type
  counts. So as a `Counter::Type`, a Thallid with 2 spores and one with 5 both read `/c1` and
  **collide**. Fixing that generically would change the signature for every existing counter deck
  and churn GT for no reason.

**Decision: scalar `int spore_counters` / `int quest_counters`.** Three reasons:
1. **The hot path.** `EffectivePower()`/`EffectiveToughness()` iterate `counters` and are called
   constantly. Putting a Spore entry on every Thallid adds an iteration to the hottest function in
   the engine — on *the* deck with 40-token boards. Scalars leave that path untouched.
2. **Byte-identity is trivial and well-understood.** Every read is nonzero-gated, so no other deck
   can observe the field. The `Counter::Type` route needs a `BoardSignature` change that has to be
   gated anyway to avoid perturbing existing decks — so it buys no simplicity.
3. It matches five existing precedents (`charge_/verse_/storage_/ice_/age_counters`).

The counter-doubling chokepoint therefore takes a counter **kind** covering both vector-backed and
scalar-backed counters and dispatches internally — one helper, all kinds, doubler applied once.

The cost of this choice is that the key sites must be handled by hand. That is the documented
failure mode, so it is a **checklist**, not a memory test — see 4 below.

**4. Engine hazards every new counter field must satisfy** (verified in-tree, not assumed):
- `BuildSimKey` (TurnSolver.cpp ~32951) — fold `spore_counters` under a `> 0` gate. Spore counters
  are *future-determining*, so two states differing only there must not share a TT entry. The file
  documents this exact defect class for `storage_counters` at line 32784.
- The two "plain/vanilla permanent" equivalence gates (TurnSolver.cpp ~6126, ~7632) enumerate every
  counter field explicitly — an unlisted field lets the canonicaliser merge a 2-spore Thallid with
  a 0-spore one.
- `ManaPayment.cpp` (~179) pay-path parity check.
- `main.cpp` viewer counter badge (~159) **and** `--scenario` staging (~5771). Both are
  hand-maintained field lists; the scenario builder has silently dropped a field three times.
- `Dominance.h` / `DecisionProvider.h` — new `DomAxis::SporeCounters`. Unenumerated fails *closed*
  (EqualRequired), so this is safe-by-default but costs prune reach.

**5. Upkeep ordering** must be identical in the lockstep pair `GameEngine::UpkeepTail` and
`TurnSolver::SimulateEndAndStartNextTurn`, or the `[fd-diverge]` harness fires. Collect against a
**snapshot** and apply after: two Sporesowers must each spore every Fungus including each other.

**Verified in-tree while integrating (agent claims I re-checked myself):**
- `Keyword::Defender` exists and is read by `CanAttack`/`CanAttackFull`, but **0 cards in
  cards.json use it** — Thallid Shell-Dweller is the first. Not inert: under Sporecrown's lord it
  would otherwise be a 1/6 attacking for 1 every turn, a fake clock.
- `subtypes_affected` is a `std::vector<std::string>` with **ANY-OF** matching, so Sporecrown's
  "Fungus or Saproling" needs no new param (first multi-entry use in the file).
- `lord_excludes_self` is **per-source** (address identity), so N Sporecrowns stack correctly.
- `Undercellar Myconid` is already in cards.json and is the **precedent to copy** for Saproling
  tokens (`"Saproling"` spelled exactly) and for "add one mana of any color" (`produces: WUBRG`).
- `DeclareAttackerIndices` is a heuristic, **not** subset enumeration — a 40-token board does not
  explode attacker selection.

### ⚠ ARCHETYPE MISROUTE — must be fixed before anything is measured (Stage 4a)

`DecisionProviders.cpp:9780` sets `goblin = true` on **`p.sac_creature_outlet` alone**. Utopia
Mycon and Psychotrope Thallid both carry it, so **Fungus would silently route to
`GoblinsProvider`** — the sixth occurrence of the misroute class the surrounding comment already
records for Mirrorwing, StompySurprise, Minotaur, Dragons and Melira Pod.

**I verified the concrete harm in-tree, and it is severe.** `GoblinsProvider::
DeferSacOutletPreCombat` (DecisionProviders.cpp:6646, **default ON**) takes a *mana* outlet
straight to the haste gate: `HasHasteFromLords` → false (no haste lords in Fungus), then a hand
scan for `grants_haste` → false (the deck has none). It returns **`true` = defer to the second
main**. But Fungus has **no second main** (`DeckUsesSecondMain` does not fire for it), so the
deferral does not move the ability — it **deletes** it. Ramping into Mycoloth or Doubling Season
on curve becomes unreachable, with no error anywhere and the deck simply measuring weak.

**Fix:** add a Fungus signature routed **above** the goblin check, OR-ing gated params from
*several different cards* so no single deckbuilding swap loses it — `spore_upkeep_self` (Thallid,
Shell-Dweller, Psychotrope, Utopia Mycon) ∨ `spore_upkeep_each_fungus` (Sporesower) ∨
`spore_saproling_cost` ∨ `devour` (Mycoloth) ∨ `doubles_tokens_and_counters` (Doubling Season) ∨
the quest-counter params (Beastmaster Ascension). Every one is new and gated, so no existing deck
can set them. Then run `scripts/provider_audit.py` as the standing check.

Routing to `GenericProvider` is the intent — a new deck earns its own provider only once it has a
**measured** hook to hold.

### The `IsSacManaOutlet` one-line trap

Four sites infer "this is a mana outlet" from `!sac_outlet_add_mana_color.empty()`
(TurnSolver.cpp:15081, SpellEffects.h:8656, DecisionProviders.cpp:6843, and main.cpp:1464's viewer
chip). Utopia Mycon adds mana of **any** colour, so it deliberately leaves that string empty and
would fall into the **value**-outlet branch — producing an action that sacrifices a Saproling for
*nothing*. Add one shared `IsSacManaOutlet()` reader in CardDatabase.h (the `IsFilterLike`
pattern) and convert all four, per the coding-conventions "one shared reader" rule.

Good news on the mana half: `ApplySacForMana` already takes the colour as an **argument** from the
Action, and the Lotus/Apex colour fan (`ChosenFloatColorCandidates`) already exists and is shared.
For this mono-green list that fan collapses to the singleton `{G}`, so the correct any-colour
implementation costs one bool param, one shared reader and two enumerator lines — **no changes to
`ManaPayment.cpp` or `ManaPool.h` at all**. A sac-for-mana source is modelled as an *action*, not
a pool source, so it never touches the payer.

### Known performance risk (for the user's requested perf pass, not a blocker)

`ComputeLordBonus` is O(battlefield) **per creature** at every call site *except*
`src/ai/Combat.cpp:66-79`, which pre-builds a lord-index list. On a Mycoloth + Doubling Season
board of 30-40 Saprolings that is ~1200-1600 inner iterations per `PendingAttackDamage` / eval
call. **This cost exists with or without Sporecrown** (the scan is unconditional, with no "any
lords on board?" early-out), so it is not caused by any card here. If the deck measures slow, the
remedy is to port Combat.cpp's pre-filter to TurnSolver.cpp:4533/4601 and
DecisionProviders.cpp:6795/13687 as a **separate, byte-identical** optimisation commit.

---

## Approved deferrals

*(none yet — every proposed deferral lands here marked PROVISIONAL until the user signs it off;
an unanswered deferral is PROVISIONAL, never approved)*

---

## Open questions surfaced to the user (non-blocking)

*(per CLAUDE.md: questions are surfaced and the documented default is taken; work never halts)*

### Q1 — Should Fungus get a searched SECOND MAIN? (Stage 2c-bis)

`GoldFishRunner::DeckUsesSecondMain` keys on `spectacle_cost`, `pod_mv_delta`, `convoke`,
`lifegain_to_loss`, `hinata_cost_reducer` and the Goblin-Lackey combat cheat. **None fire for
Fungus**, so it is first-main-only by default.

But the deck has a genuine post-combat line, and it is *exactly* the pattern the repo already
whitelisted Birthing Pod for: **attack with Saprolings, then sacrifice the attackers post-combat**
to Utopia Mycon (mana) or Psychotrope Thallid (a card). In a goldfish the opponent never blocks,
so the attackers always survive — the body banks its combat damage *and* its sacrifice value off
one card. With only a first main the search is forced to trade one for the other.

**Decision taken (default, non-blocking): ship first-main-only, then MEASURE it in Stage 5.**
The repo ships `MTG_FORCE_USES_M2=1` as a default-off measurement lever built for precisely this
question ("A/B whether a non-whitelisted deck's skipped m2 ever has value"). So this is settled by
measurement, not by my judgement: if the m2 arm measures better, add a Fungus signature to
`DeckUsesSecondMain`; if it is neutral, the whitelist is confirmed for this deck and the absence
is disclosed in Stage 6a. Recorded here so the result lands either way.

---

## Stage 3 / 4 — results

**Stage 3 coverage: CLEAN.** All 14 cards report `full`; `missing` is empty; no partial gaps.
Every card's `mana_cost`, `cmc`, P/T, type line, keywords and oracle text was verified by a direct
Scryfall fetch (the `/cards/collection` endpoint, after waiting out a rate limit) — not taken from
the research drafts. All five late-fetched cards matched their drafts exactly.

**Stage 4a provider routing: FIXED, and it took TWO fixes.** The first (found by reading) was the
Goblins misroute via `sac_creature_outlet`. The second was found only by **running the deck**, and
is broader: **Wild Growth is a land aura, and `is_land_aura` ALONE sets the EldraziFlicker
signature** — which is detected *above* even the goblin check, so Fungus was inheriting
`EldraziFlickerProvider` wholesale. That is the **seventh** instance of the archetype-neutral-param
misroute class, and the widest-reaching yet: any deck playing Wild Growth, Fertile Ground or Utopia
Sprawl hits it.

Fungus is now routed above both. Verified empirically across all 25 decks that **no other deck's
routing moved** — the signature is OR'd across six cards' params, every one new and gated, so no
existing deck can set it.

*Lesson worth keeping: reading `SelectDecisionProvider` found one misroute; running one game found
the other. The audit is cheap — run it, don't reason about it.*

**Stage 4 profile: written** (`decks/Fungus/Fungus.profile.json`), 13 card-score entries (all
non-basics — so no card is scored as an empty slot). Baseline only, per policy: no exhaustive
keep/bottom table, which is the separate user-initiated mulligan stage.

Notable card scores (first-copy marginal value): Sporecrown Thallid **+0.51**, Beastmaster
Ascension **+0.35**, Thallid **+0.30**. Thallid Shell-Dweller is **−0.31** — consistent with a 0/5
Defender whose only contributions are the spore engine and being a Fungus body. Beastmaster
Ascension's second copy is **−0.35**, i.e. strongly diminishing: one is a wincon, two is a dead
card. That is a deckbuilding observation for the user, not a modelling problem.

First real game: wins **turn 5** at depth 0.

## Claude-play sweep

- commit: `bf3306a1`
- seeds: 9101 games: 16 (game-indices 0-15, disjoint from the suite's 1001-7007)
- flags: 0 unresolved

**Result: all 16 games matched the search exactly at turn 5.** Zero misplay candidates, which
is the expected healthy outcome for a guided Claude against a clairvoyant search (the sweep's value
is bug-finding, not beating the AI).

**One real bug, found independently by FOUR agents (gi0, gi1, gi11, gi15) — fixed in `bf3306a1`.**
Two Saproling-gated sac outlets (Utopia Mycon's mana, Psychotrope Thallid's draw) were offered in a
single plan while only ONE Saproling was on board; at apply the first consumed it and the second
silently no-opped — no draw, the `{1}` never paid, no error. Fixed at the enumerator with a
fodder-supply reject; verified over 204 decision frames / 96 sac-outlet plans, 0 remaining.

*Severity note worth keeping:* gi15 read the apply path and found the degradation is deliberate and
documented in `ApplySacCreatureOutlet`, and that path is **shared executor+rollout** — so both
worlds degrade identically and the search was probably not misvalued. The defect is that the
ENUMERATOR advertised a plan it could not perform, which misleads a human or claude-play driver.

**Dismissed (not a defect): plan-list duplication.** Many agents flagged 38-76 plans collapsing to
~16-24 distinct summaries. gi9 found the cause — `main.cpp` sets `MTG_UNPRUNED=1` for the whole
claude-play session, so this is the viewer's deliberately-widened enumeration (the human-play
must-not-narrow invariant), **not** the shipped search's plan width. Autonomous play is unaffected.

**Doc gap (not a defect):** the `bounce` decision type is not in `claude-play.md`'s documented
decision-type list. It self-documents via its `note` field. Five agents noticed it.

### Mechanic verification — all four now confirmed IN REAL PLAY

The sweep games all end on turn 5, so **Doubling Season and Mycoloth/devour were never drawn** in
any of the 16. That coverage gap was closed separately by mining 40 logged games at `--max-turns 14`:

| mechanic | evidence |
|---|---|
| **Spore counters** | Sweep: accrue exactly 1/upkeep, activation offered at exactly 3 (not at 1 or 2), removes exactly 3, no mana and no `{T}`, legal while summoning-sick. gi14 probed to 6 counters and confirmed two activations in one turn (the enumerator offers `x:1` per plan and re-enumerates within the same main). |
| **Doubling Season** | Logged games: spore gain per upkeep is **1** with no Doubling Season (82 obs), **2** with one (8 obs), **4** with two (2 obs) — 2^N stacking confirmed empirically. Token half confirmed below. |
| **Devour** | Game 11: Mycoloth enters T5 with **2 ×+1/+1** (devour 2 × 1 creature); Saprolings 0→1 the same turn = **Tukatongue's death trigger refunding a Saproling**, i.e. the deferred-death-trigger ordering works. T6 upkeep: 2 counters → **2** Saprolings (1→3). T7 with a Doubling Season out: the same 2 become **4** (3→7) — token doubling confirmed. |
| **Beastmaster Ascension** | Sweep, many independent confirmations: exactly one counter per **declared** attacker (summoning-sick creatures correctly contribute none), and the **+5/+5 applies to the same combat** that crosses 7. Agents recomputed the 40 damage from card text exactly (1/1 +2 from two Sporecrowns +5 = 8, ×5). |

Also confirmed incidentally: Sporecrown's two-subtype lord stacks across copies with
`lord_excludes_self` per-source and pumps Saproling **tokens** (so the subtype strings line up), and
the Karoo correctly taps for mana *before* its ETB bounce resolves.

## Stage 5a/5b results

* **nonconv: 0 flags** (60 games, d3, b200).
* **fd-diverge: 0 flags** (40 games, d5, b200, `MTG_FULL_DEPTH=1`).
* **Multi-depth:** d0 **5.99** → d3 **5.61** — monotone. (These ran on the pre-fix binary; re-run
  on the shipped binary below.)

## Stage 6a — Encoded heuristics & assumptions disclosure

### 1. Global engine assumptions in force

| assumption | effect on this deck |
|---|---|
| Single **passive** opponent — never blocks, casts, gains or prevents life | Large. Every attacker connects every turn, so Beastmaster Ascension's threshold is reached just by attacking wide, and the +5/+5 is pure clock. It also means Thallid Shell-Dweller's 5 toughness is inert and **Tukatongue Thallid's death trigger can only ever fire off Mycoloth's devour** — nothing else in the deck kills a Fungus. |
| Clairvoyant search over a known library (deterministic shuffle) | Standard. |
| **First main only** — `DeckUsesSecondMain` does not fire for Fungus | See Q1: the attack-then-sacrifice double-dip is unrepresentable. Measured below. |
| Depth / budget the results were produced at | d0/d3/d5, budget 200 ms, `--lookahead-bottoming`. |

### 2. Card-modeling simplifications (every bracket note in this deck)

| card | deferral | why inert | status |
|---|---|---|---|
| All five spore cards | activation enumerated main-phase only, though printed instant-speed | The enabling counter arrives at the controller's own upkeep; the passive opponent offers nothing to respond to; a token made anywhere between turn N and N+1 first attacks on N+1 either way. Main-phase is in fact weakly *dominant* (the Saproling is then available to devour, the lord, and the sac outlets that same turn). | **PROVISIONAL** |
| Beastmaster Ascension | "you **may** put a quest counter" modelled as always-take | Strictly dominated: no counter cap, no sacrifice-at-N clause, no cost, and no state where fewer counters is better. | **PROVISIONAL** |
| Mycoloth | attack-then-devour post-combat sequencing unrepresentable | NOT inert — a real under-rating (see §4). Bounded to one decision per copy per game. | **PROVISIONAL** |
| Mycoloth | `devour` absent from the `keywords` array | Fully modelled via the `devour` param; the engine's `Keyword` enum carries only keywords some code path reads, and the loader rejects unknown strings. Allowlisted in `scryfall_divergences.json`. | disclosed |
| Simic Growth Chamber | — | No deferral. The `{U}` is unusable for coloured pips (no blue cards) but **pays generic**, so it is a true 2-mana land for the deck's seven generic-pip spells. | — |

### 3. Deck / archetype DecisionProvider heuristics

**Fungus routes to `GenericProvider` and therefore overrides NOTHING** — no deck-specific
narrowing at all, pure search within the global assumptions above. This is deliberate: a new deck
earns its own provider only once it has a *measured* hook to hold.

Getting there took **two** routing fixes, both recorded above — the `sac_creature_outlet` → Goblins
misroute and the `is_land_aura` → EldraziFlicker misroute. Verified across all 25 decks.

### 4. Play-viewer auto-resolved decisions

| card / choice | status |
|---|---|
| Karoo bounce target | **Surfaced** (`bounce` decision, confirmed live by 5 sweep agents). |
| Spore activation (whether, and how many times) | **Surfaced** as `main_phase` board activations; K folds to 1 under human play and the main re-prompts, so K=2 is reached by choosing twice (gi14 confirmed). |
| Which Saproling a sac outlet eats | **Surfaced** — reuses the existing `sacrifice` decision. |
| Devour **count** | **Surfaced** — a real searched plan variant, keyed into `plan_signature`. |
| Devour **which creatures** | **NOT surfaced** — auto-resolved by the shared expendability ranking. Wiring it needs a multi-select sacrifice the registry has no shape for. Bounded: devour fires ≤2× per game, the fodder is overwhelmingly fungible 1/1 tokens, and the ranking already prefers the one correct special case (Tukatongue, whose death refunds a Saproling). **PROVISIONAL — needs sign-off.** |

Everything else: no card choice is silently heuristic-resolved. The auditor's oracle-text
cross-check reports *"No oracle-text choice phrase is left unmodeled by params. Clean."*


## Stage 5c2 — horizon-honest tie-break (`GradesNoWinLeaf`)

`python3 scripts/leaf_tiebreak_check.py decks/Fungus/Fungus.cod` — 24,000 games (12,000 paired),
both arms in ONE pooled batch, 1 h 48 m, `[batch] heartbeat` at 24/24 throughout.

```
split         games  net turns  worse  better   (negative = the tie-break HELPS)
half A         6000         +0      4       4
half B         6000         -4      3       7
ALL           12000         -4      7      11
binding: 18 changed games of 12000 paired (0.150%)
VERDICT: NO SIGN AT THIS SAMPLE
```

**Decision: keep the default (ON).** Per the skill, "NO SIGN" is *not* a pass — but there is a
mechanistic reason the binding rate is this low rather than it being pure under-sampling: the
tie-break only fires when a rollout reaches the horizon **without** a win, and this deck wins on
turn 5-7, comfortably inside it. The measured direction is mildly negative (−4 net turns, i.e.
helping). The skill's own fallback applies: *"If it stays unbindable at a large sample, the default
(ON) is fine by default: a lever that never fires costs the deck nothing."*

**Open (non-blocking):** the script suggests `--blocks 24` for a decisive sample. That is ~3.5 h on
this deck. Not run — the performance finding below is the higher-value use of the box, and the
default is already the safe side. Re-run if you want the direction pinned down.

## ⚠ PERFORMANCE — the deck has pathological games

Surfaced by the batch runner's own `SLOW-GAME` reporting during the run above (>30 s per game),
which is exactly the signal CLAUDE.md says to check:

| metric | value |
|---|---|
| games over 30 s | **526** of 24,000 (~2.2%) |
| median slow game | 53.8 s |
| p90 | 195.8 s |
| **worst single game** | **1,446 s — 24 minutes** |
| total wall time inside slow games | **14.6 hours** |

Shape of it:
* Slow games concentrate at **win turns 6-7** (444 of 526) — the longer the game runs, the wider
  the token board, the slower each node.
* The `base` and `leaf` arms are hit about **equally**, so this is **the deck, not the tie-break
  lever**.
* Worst repros (both arms agree on the same games, which is itself confirmation it is board-driven):
  * `--seed 1100235 --game-index 235` (wt 8) — 1446 s / 1239 s
  * `--seed 1600607 --game-index 607` (wt 6) — 924 s / 870 s
  * `--seed 2100009 --game-index 9` (wt 7) — 843 s

This is the board-size scaling the Doubling Season research predicted, and it matches the repo's
own clue-fusion precedent (67-69 permanent boards → 807 s/game; *"token floods, not units, ate the
wall"*). It is a **performance** problem, not a correctness one — every correctness gate is green.

**Consequence for sequencing:** a value-leaf generation is described as tens of hours on a normal
deck. On a deck with 24-minute games it could be far worse, so profiling this came first.

**Profiled — full write-up in [fungus-token-search-cost.md](fungus-token-search-cost.md).** Summary:
it is **not** a plan explosion (avg 15.7 plans/enumeration), **not** memory (128 MB peak) and
**not** the real board (15 permanents at d0). It is **58x the cost PER NODE** on top of 78x the
nodes — board-size scaling inside ROLLOUTS, which explore token-engine lines real play never
reaches. `MTG_BP_SEARCH=0` looked like a 1.88x win on the worst single game and was **rejected**
on a 100-game sample (1.2% faster, slightly worse average).

## Stage log

* **2026-09-17** — Stage 1 coverage run; 11 missing cards; four engine mechanics absent
  (spore counters, devour, quest counters, Doubling Season). Ledger opened. Research fan-out
  launched (11 Opus agents).
* **2026-09-17** — Stages 2-4 complete. All 14 cards implemented and verified against Scryfall;
  four new mechanics built; TWO provider misroutes found and fixed (Goblins via
  `sac_creature_outlet`, EldraziFlicker via `is_land_aura`); profile generated.
* **2026-09-17** — Stage 5 complete. nonconv 0, fd-diverge 0, multi-depth monotone
  (5.99/5.61/5.60), 16-game claude-play sweep all matching the search with one real bug found and
  fixed, viewer audit clean, 5c2 NO-SIGN (default kept). **smoke 80/0, regression 108/0,
  `verify_deck.py` GATE PASS.**
* **2026-09-17 18:08Z** — **Value-leaf generation LAUNCHED**, frozen at `64464c25`
  (src tree `8d6d07f7b81e`), play fingerprint `8deac4f6131c`. Phase A: 10 jobs in one pooled
  queue; utilisation checked at ~24/24 cores, 4.6 GB RSS. Expected to run for hours —
  `bash scripts/valueleaf.sh status decks/Fungus` for progress.
  *Rationale beyond the user's request: the perf finding is a ROLLOUT-cost problem, and the value
  leaf replaces the horizon rollout with an O(1) evaluator, so it is also the natural remedy.*

## Open questions for the user (all non-blocking; defaults taken, work never halted)

1. **Q1 — searched second main?** Fungus can attack with Saprolings then sacrifice the spent
   attackers post-combat (the Birthing Pod double-dip). Shipped first-main-only (the default);
   settle by measurement with `MTG_FORCE_USES_M2=1` rather than judgement. Not yet run.
2. **Q2 — devour victim selection** is auto-resolved (count IS searched). PROVISIONAL.
3. **Q3 — the five instant-speed / "you may" deferrals** listed in §2 of the 6a disclosure.
   PROVISIONAL — an unanswered deferral is never "approved".
4. **Q4 — `--blocks 24` re-run of 5c2** to pin the tie-break direction (~3.5 h). Not run; the
   default is already the safe side.
5. **Q5 — should Fungus join the regression suite?** `verify_deck.py` warns that nothing tracks
   its play digest otherwise. Blocked today by the perf finding: a 24-minute game would blow the
   smoke/regression time budgets.
6. **Q6 — the `is_land_aura` misroute is GENERAL.** Any deck with Wild Growth / Fertile Ground /
   Utopia Sprawl inherits EldraziFlickerProvider. Fixed for Fungus only.

**Not pushed.** Engine changes are substantial and CI (Linux + Windows) has not run; MSVC is
unverified from this container. `git push` when you want the Windows/determinism-parity signal.
