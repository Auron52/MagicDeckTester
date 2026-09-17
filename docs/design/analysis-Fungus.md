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

| card | status |
|---|---|
| Thallid | research launched |
| Thallid Shell-Dweller | **draft in** — Tier 3 |
| Sporesower Thallid | research launched |
| Utopia Mycon | research launched |
| Doubling Season | research launched |
| Mycoloth | research launched |
| Beastmaster Ascension | research launched |
| Sporecrown Thallid | research launched |
| Tukatongue Thallid | research launched |
| Psychotrope Thallid | research launched |
| Simic Growth Chamber | research launched |

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

---

## Stage log

* **2026-09-17** — Stage 1 coverage run; 11 missing cards; four engine mechanics absent
  (spore counters, devour, quest counters, Doubling Season). Ledger opened. Research fan-out
  launched.
