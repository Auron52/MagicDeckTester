# Faithful restricted-mana for Unclaimed Territory / Cavern of Souls / Secluded Courtyard

**Status: scoped, deferred.** Flagged by the user during Dragonstorm play-testing 2026-07-22
("we should update unclaimed territory ... but in this case the Lotus Bloom produces the red" —
i.e. a real modeling gap, but not the cause of any active bug). Substantial + GT-affecting, and the
current model is a *deliberate* simplification (see the card comments), so it is written up here
rather than changed inline.

## The gap

Three "tribal any-color" lands are modeled as **unrestricted 5-color** (`produces:[W,U,B,R,G]`,
no restriction), per their card comments in `cards.json` ("Simplified: modelled as producing all
five colors; ETB choice and color restriction not modelled"):

| Card | Real Oracle |
|------|-------------|
| Unclaimed Territory | choose a type; `{T}`: Add `{C}`. `{T}`: Add one mana of any color — spend only on a creature spell of the chosen type. |
| Cavern of Souls | as above + "that spell can't be countered". |
| Secluded Courtyard | as above (+ activate an ability of a creature source of the chosen type). |

Each is really a **mixed** source:
- `{C}` — unrestricted (pays any generic cost), AND
- one **colored** mana — usable ONLY to cast a **creature spell of the chosen type**.

The engine currently lets the colored mana pay **anything** — so e.g. in the Dragonstorm deck
Unclaimed Territory wrongly pays the `{R}` pip of a ritual / Dragonstorm / Apex. Reality: it can
pay their **generic** with `{C}`, but the **red pips** must come from real red sources
(Mountains, rituals, Lotus Bloom). Net effect of the gap: the deck's mana reads **easier** than it
is, so the engine's Dragonstorm evaluation/play is optimistic.

## Existing machinery

`creature_mana_only` (bool) already exists — **Ancient Ziggurat** uses it. Semantics: the source
may be tapped ONLY when paying for a creature spell (`for_creature`). Checked at ~6 mana-solver
sites: `if (def.params.creature_mana_only && !for_creature) { reject/continue; }`
(`AIEngine.cpp:2673`, `TurnSolver.cpp:285/2521`, `SpellEffects.h:3936/3973`, sim-key at
`TurnSolver.cpp:6167`).

But `creature_mana_only` makes **ALL** of the source's mana creature-only — Ancient Ziggurat has no
`{C}` ability, so that's correct for *it*. Unclaimed/Cavern/Courtyard need the `{C}` escape too, so
the flag can't be reused as-is (setting it would wrongly forbid paying generic with `{C}`).

## Recommended approach

Add a new flag `colored_creature_only` (bool). Semantics: **`{C}` is unrestricted; the COLORED
mana is creature-only.** Model these lands as `produces:[C,W,U,B,R,G]` + `colored_creature_only:true`.

At each of the ~6 solver color-match sites, when the spell being paid for is **not** a creature (of
the chosen type): allow the source to satisfy only a `{C}`/generic need, never a colored pip. When it
**is** a qualifying creature: all colors are usable (same as today).

**Chosen creature type — simplify to "any creature".** These lands are played in single-tribe decks
(Dragon for Dragonstorm, Sliver for Slivers), so "creature spell of the chosen type" ≈ "any creature
spell" for every deck we test. Model the colored restriction as plain `for_creature` (reuse the
existing predicate) and skip per-source type tracking. Note the approximation in the card comment;
revisit only if a multi-tribe deck is ever tested. (Sliver Hive already carries a `tap_token_*`
tribe param but its mana is likewise modeled unrestricted — same treatment applies.)

## Scope / cost

- New param `colored_creature_only` in `CardDatabase.{h,cpp}`; set it on Unclaimed Territory, Cavern
  of Souls, Secluded Courtyard (and consider Sliver Hive) in `cards.json`, add `C` to their
  `produces`. Update the "[Simplified: ...]" card comments to record the new (still-approximate) model.
- Extend the ~6 `creature_mana_only` solver sites to also honor `colored_creature_only` (colored-only
  restriction, `{C}` always allowed). Fold the new bit into the sim-key at `TurnSolver.cpp:6167`.
- **GT-affecting**: nerfs Dragonstorm + Slivers mana → smoke + regression (+ eventually overnight)
  rebaseline. Expect Dragonstorm to get measurably slower (the combo can no longer lean on Unclaimed
  for red pips) — a *correctness* improvement, not a regression.
- Consult `.claude/skills/mtg-rules.md` before implementing (mana abilities / restricted mana).

## Verification

- Unit-ish: at a Dragonstorm state with Unclaimed Territory untapped and no other red, a ritual
  ({R} pip) must NOT be payable off Unclaimed alone; a hard-cast Dragon ({...}{R}{R}) must be.
- Reproduce seed-23 style combos: Unclaimed contributes to generic but red pips still require real
  red — the combo timing should shift later where it previously leaned on Unclaimed for red.
