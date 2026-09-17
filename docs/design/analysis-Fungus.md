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

## Stage log

* **2026-09-17** — Stage 1 coverage run; 11 missing cards; four engine mechanics absent
  (spore counters, devour, quest counters, Doubling Season). Ledger opened. Research fan-out
  launched.
