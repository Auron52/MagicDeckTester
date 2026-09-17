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
| Thallid Shell-Dweller | research launched |
| Sporesower Thallid | research launched |
| Utopia Mycon | research launched |
| Doubling Season | research launched |
| Mycoloth | research launched |
| Beastmaster Ascension | research launched |
| Sporecrown Thallid | research launched |
| Tukatongue Thallid | research launched |
| Psychotrope Thallid | research launched |
| Simic Growth Chamber | research launched |

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
