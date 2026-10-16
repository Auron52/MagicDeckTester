# Kicker (CR 702.33) — an optional additional cost, as a synthesized cast face

**Status: IMPLEMENTED AND VALIDATED 2026-10-16. NOT YET MEASURED** (the Saproling
Migration count re-measurement is still owed). User-directed: *"We should fix
Saproling Migration for sure to add kicker."*

## Why this was needed, and what it was costing

`grep -rn "kicker\|kicked" src/` was **empty**: kicker existed nowhere in the engine. Saproling
Migration entered the Fungus census on the JSON-only route with its unkicked half modelled and its
kicker omitted, and `cards.json` disclosed the consequence — the omission is one-way *against* the
card, so its measured delta was a lower bound.

It screened near the line. Fungus round 36 measured cutting Saproling Migration 4 → 3 at
**+0.0022 ±0.0020 std / +0.0056 ±0.0022 2hg** — under the apparatus floor, i.e. indistinguishable
from free, and biased *toward* cutting. So the count could not be ruled on. **This work unblocks a
measurement; it is not a tidy-up.**

## The card

Saproling Migration — `{1}{G}` Sorcery. *"Kicker {4}. Create two 1/1 green Saproling creature
tokens. If this spell was kicked, create four of those tokens instead."* Kicked total: `{5}{G}`.

## Rules facts that shape the design

* **CR 702.33a** — kicker is an *optional additional cost* paid as the spell is cast.
* **CR 601.2b** — additional costs are **added into the total cost**, so kicked = `base + kicker`
  (`{1}{G}` + `{4}` = `{5}{G}`). **This is the one place the design differs from `evoke_cost` and
  `bestow_cost`, which are ALTERNATIVE costs that REPLACE.** Get it backwards and the kicked mode
  becomes the cheap one, the search kicks unconditionally, and the re-measurement is nonsense in the
  direction that flatters the card.
* **CR 601.2f** — cost increases are applied before reductions, so a reduction applies to the
  *summed* cost. Hence the implementation reduces the face's cost, never base-then-add.
* **CR 601.2b** — the choice is locked in at cast time; "was kicked" is a property of the spell *on
  the stack*.
* **CR 608.2m** — on resolution the spell **card** goes to the graveyard. It is ONE card named
  "Saproling Migration"; "kicked" is not a different card. This is what the graveyard tail below
  exists for.
* The kicked clause **replaces** the token count ("create four of those tokens *instead*").

## THE DESIGN AS BUILT — and why it is not what this document first proposed

The original plan was to add `Action::kicked` plus a `kicked_creates_tokens` param and **thread the
flag ~20 sites down into `SpellEffects`**, with this document flagging one unknown: *"verify how
evoke/adventure reach SpellEffects before writing this."*

**That verification changed the design, and it is the main finding of the work.** `evoke` and
`adventure` do **not** reach `SpellEffects` at all. Neither does `bestow`. All three work by
**swapping in a different `CardDefinition` at cast time**, in one place, after which every
downstream reader — cost, resolution, the token cascade — just reads an ordinary card:

| mode | face | built how |
|---|---|---|
| adventure | `adventure_face_name` | a second hand-authored `cards.json` entry |
| bestow | `"<name> (Bestowed)"` | **synthesized by `CardDatabase` at load** |
| MDFC back | `mdfc_back_name` | synthesized, as a **full copy** of the front |

So kicker is built as a DB-synthesized **`"<name> (Kicked)"` face**, following bestow. The payoff is
that `SpellEffects.h`, `EffectHandler`'s resolution and the whole token cascade are **untouched** —
Doubling Season doubling comes for free because nothing about the token path changed.

### 1. The params (`CardDatabase.h`)

```cpp
std::optional<ManaCost> kicker_cost;             // the kicker ALONE, as printed -- never pre-added
int                     kicked_creates_tokens;   // REPLACES cast_creates_tokens when kicked
std::string             kicked_base_name;        // set only ON a face; names the printed card
```

### 2. The face synthesis (`CardDatabase.cpp`, beside the bestow face)

A **full copy** of the def — not a selective field list like the aura face — because a kicked spell
*is* the same spell with one clause changed, so anything not explicitly overridden must behave
identically by construction. Overrides: the name, `m_def = nullptr` (the name-derived cache must
reset — MDFC precedent), the cost via `ManaCost::PlusAdditionalCost` (**additive**, CR 601.2b),
`cast_creates_tokens = kicked_creates_tokens`, `kicker_cost` cleared (a kicked spell cannot
re-kick), `kicked_base_name` set, and `behaviour_identity = 0` (the documented "no identity, fall
back to the name" value — a face is *not* interchangeable with its own printed entry, yet both
digest from one `cards.json` entry and would collide).

A non-flat kicker (hybrid / phyrexian / twobrid / snow / `{X}`) **throws at load**. Thrown, not
asserted: `assert` is compiled out under `NDEBUG`, which is every build that measures anything, and
the failure it would admit is a cost that silently pays wrong.

### 3. Enumeration (`TurnSolver.cpp`, beside the bestow/evoke variants)

An extra `Action::Kind::CastFromHand` sharing `hand_index`, with **no `continue`**, so the unkicked
cast still emits and the two are mutually exclusive within a plan. Cost and eval are read **off the
face** (`EffectiveCost(*kdef, state)`), which is what gets CR 601.2f's reduction order right.

**No emission gate, deliberately.** Evoke is gated on a live LTB target because without one the
dearer cast is *strictly dominated*; nothing dominates here — unkicked is two mana for two bodies,
the best unconditional rate in the pool, while kicked is six for four and competes for exactly the
turn-4/5 mana that casts Saproling Burst or a third Doubling Season (under one Season, 8 bodies
against 4). A "kick when mana is spare" test would be a heuristic pick inside the search window.

### 4. `Action::kicked`, threaded exactly as `adventure` is

`TurnSolver.h` (the field), the fold hash (`FoldMix`), the action differ, `apply_one`'s signature and
all 13 call sites, `AIEngine::CastSpellFromHand` + `cast_by_name` and their call sites, and the two
one-line def swaps (`apply_one`, `CastSpellFromHand`) that are the whole resolution half.

**`plan_signature` is the site where forgetting the marker silently undoes the entire change.** The
signature keys on the card NAME, and the two modes share one name, so without a marker the dedup
keeps whichever plan sorts higher and the search sees only ONE mode per subset — enumeration would
emit both variants and the kicker would still never be weighed. Marker is `#KI1`/`#KI0`; **not
`#K`**, which is already Terastodon's destroy-K count. Param-gated, so no deck without a kicker card
moves a signature.

### 5. The cost RECOMPUTE sites — the non-obvious half

Three sites re-derive a cast's cost from `a.def` rather than reading `a.cost`, and each already
carries an `if (a.evoke) ec = *evoke_cost;` line. A kicked cast must be re-derived from its **face**,
or the prepay taps four mana short and the search believes an unaffordable line is affordable.
Shared helper `KickedFaceOr(d, kicked)` swaps the def *before* `EffectiveCost`, since the summed cost
lives on the face. (The two actual cast sites do **not** call it — they swap once, up front.)

Found by grepping `evoke_cost`, which is the canonical "lockstep list" for this class of bug.
Incidentally, **`bestow` is missing from all three** — a pre-existing gap, not touched here.

### 6. The graveyard tail — the one place the face must NOT leak

`SpellEffects.h`'s `UnkickedSpellCard(def, resolved)` maps a face back to its printed card at the two
graveyard sites (`TurnSolver`'s `apply_one` and `EffectHandler::MoveToGraveyard`), preserving the
copy's `m_number` (a kicked card is the same physical copy — the adventure path keeps the number for
the identical reason).

Without it the graveyard fills with "Saproling Migration (Kicked)". Besides being wrong under
CR 608.2m, **that splits EOT dominance folding**: a graveyard holding the kicked name is a different
state from one holding the printed name, so two lines reaching an identical board stop folding and
the search pays for both — a cost that looks like nothing in the output.

### 7. The viewer

`LineSpec::kickeds` + `kicked=<card>` (parsed in `main.cpp`, encoded in `tools/play/linebuild.js`),
and `"kicked": true, "verb": "kick", "kicked_cost_mv"` on the action JSON so the choose dialog can
tell two otherwise identical rows apart. Compared **unconditionally**, like `adventure=` and unlike
every optional verb: by card name the modes are identical, so an absent key has to *mean* "unkicked"
and be enforced. Safe to make unconditional on the same evidence adventure used — Saproling Migration
is the only card in `cards.json` with `kicker_cost`, and **no saved reference plays it** (verified by
grep over `references/`), so no recorded line's meaning can move.

## Explicitly NOT bundled

**`EvalCard` has no `cast_creates_tokens` branch at all**, so it falls through to `return DMG` (100)
— a cantrip's value — for every token spell in the pool (Saproling Migration, Spore Swarm, Fungus
Frolic). It moves GT for both Fungus configs, so it is adopt-and-measure and kept separate, so each
change's GT movement stays attributable. Both errors push the same way, so **Saproling Migration's
re-measured delta is still a lower bound** until `EvalCard` is fixed too — smaller than before, not
unbiased.

**Multikicker (CR 702.33b)** is not needed by any card in the pool; not generalised speculatively.

## Validation

| # | item | state |
|---|---|---|
| 1 | `ManaCost::PlusAdditionalCost` / `IsFlat` correct | **PASSED** standalone (`logs/kicker_check/`) |
| 2 | `test/unit/test_kicker_additional_cost.cpp` — face synthesis, additive cost, payload replacement, no re-kick, graveyard mapping, inertness | **PASSED** 6 cases / 76 assertions; full suite 602/602 |
| 3 | scenario: the search *chooses* each mode when better | **PASSED** — 134 unkicked vs 12 kicked (8.2%) over 115 of 200 games (`logs/fungus_screen/kicker_scenario.sh`) |
| 4 | smoke | **PASSED** — 119/119, 0 configs changed, byte-identical (see the correction below) |
| 5 | regression (adds the reference-reproducibility gate smoke skips) | running |
| 6 | re-measure Saproling Migration 4→3 and retire the kicker caveat | owed |

On (4) — **CORRECTED 2026-10-16 after the run.** This document previously predicted *"GT will move
for the Fungus configs -- the kicked mode is genuinely new play, so that is an accept."* **That was
wrong, and the error was not reading the suite's decklists.** `grep -rl "Saproling Migration" decks/`
is EMPTY: neither suite Fungus list (the incumbent `decks/Fungus/Fungus.cod` nor
`candidate-b-2026-09`) plays the card. The card lives only in the screening lists under `logs/`.

So **there is no GT key covering a kicker card anywhere in the suite, and no GT acceptance to make.**
Smoke came back **119 passed / 0 failed / 0 configs changed / 0 play-changed**, digests included —
which is not a weak result but the *complete* proof that the param gate holds, since every deck in
the suite is a deck without `kicker_cost`.

The consequence to keep straight: **the suite cannot regress the kicker's own play, now or later.**
Its only guards are the unit tests and the scenario test below, and they will stay its only guards
until a list holding Saproling Migration is adopted into `decks/` and enters the suite. Any future
change near the cast-variant machinery should be checked against the scenario test deliberately,
because nothing will fail on its own.

## Operational note

**Do not build while a screen batch is running** — one heavy process at a time (a parallel `-O3`
build beside a batch OOM-killed the container on 2026-09-09). Editing `src/` on disk is safe: the
running binary is already loaded and `src/` is not read at runtime.

**A SINGLE `-fsyntax-only` TU IS NOT A BUILD, and it is worth doing beside a batch.** Measured here,
one core each, with `ulimit -v` set so an overrun kills the compiler rather than the container:

| TU | peak RSS | time |
|---|---|---|
| `src/cards/CardDatabase.cpp` | 305 MB | 3.5 s |
| `src/core/EffectHandler.cpp` | 468 MB | 6.7 s |
| `src/ai/AIEngine.cpp` | 568 MB | 8.0 s |
| `src/ai/TurnSolver.cpp` (~70k lines) | **851 MB** | 14.5 s |
| `src/main.cpp` | 610 MB | 10.2 s |
| `test/unit/test_kicker_additional_cost.cpp` | 407 MB | 6.5 s |

Under 1 GB even for the largest file in the repo, against ~9 GB free — nothing like the parallel
`-O3` link that caused the OOM. Flags:
`g++ -std=c++20 -fsyntax-only -Isrc -Ibuild/generated -Ibuild/_deps/nlohmann_json-src/single_include
-Ibuild/_deps/doctest-src`. **C++20, not 17** (`NameRegistry.h` uses `constinit`). Doing this caught
one real error — `Card::IsNoncreature()` does not exist; the repo idiom is `!card.IsCreature()` —
which would otherwise have cost the whole build slot the batch was blocking.

`cards.json` is different and was checked before editing, not assumed: a running batch loads it once
at startup via the `CardDatabase` singleton, so an edit cannot reach it — **provided the driver does
not relaunch**. Round 37 runs `index_carry: false`, hence a single `run_batch` and a single `mtg`
process (verified by PID and start time), so the edit was safe. Under `index_carry: true` there are
**two** `run_batch` waves and a `cards.json` edit between them would silently mix two engines inside
one round's numbers.
