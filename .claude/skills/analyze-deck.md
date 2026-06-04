# Deck Analysis Skill

Use this skill when the user asks to analyze a deck, add a new deck, or run a simulation on a deck file. It orchestrates the full workflow: coverage check → implement all gaps → run the C++ analyzer. **Do not stop between stages to ask for user approval unless a genuine design decision is required.**

---

## When to Use

- User says "analyze \<deck path\>"
- User says "run the analyzer on \<deck\>"
- User says "add this deck: \<path\>"
- User wants to know the average win turn for a deck

---

## Stage 1 — Coverage Check

Run the coverage tool to find out what needs implementing:

```
python scripts/analyze_deck.py <deck_path> --coverage-only
```

Parse the JSON output:
- `missing`: cards not in `src/cards/data/cards.json` at all → must implement before analysis
- `coverage[*].status == "partial"`:
  - `deferred`: bracket-noted deferrals (previously accepted) — no action needed
  - `gaps`: oracle text features missing from the current implementation → must fix before analysis

If `missing` is empty and no `gaps` exist (only accepted deferrals), skip to Stage 4.

---

## Stage 2 — Implement All Gaps

For every card in `missing` or with `gaps`, work through the escalation ladder below. **Complete all cards before moving to Stage 3.** Only involve the user when escalation tier 4 is reached.

### 2a. Fetch oracle text

Use WebFetch to get the authoritative oracle text from Scryfall:
```
GET https://api.scryfall.com/cards/named?exact=<url-encoded card name>
```
Key fields: `oracle_text`, `type_line`, `mana_cost`, `power`, `toughness`, `keywords`.

**Always fetch from Scryfall — never rely on a bracket note's description of the "real" mechanic.** Bracket notes are written by an LLM and may mischaracterise or misremember the oracle text. The Scryfall response is the only authoritative source. This applies equally when implementing a previously deferred card: fetch first, implement from the live oracle text.

### 2b. Consult the rules skill

Read `.claude/skills/mtg-rules.md` and work through the Card Implementation steps for the card. Use this to determine: what ability type is this (triggered, activated, static, replacement)? What data model does it need?

### 2c. Escalation ladder

Apply the **first tier that fits**:

**Tier 1 — cards.json only**: The card fits an existing template and all its oracle text maps to existing `CardParams` fields. Write the entry and move on.

**Tier 2 — new parameter(s) on an existing template**: The card's mechanic is a small extension of an existing template (e.g., a new flag on `direct_damage`, a new field on `draw_spell`). Do the following:
1. Add the field to `CardParams` in [src/cards/CardDatabase.h](src/cards/CardDatabase.h)
2. Read it in `BuildParamsFromJson()` in [src/cards/CardDatabase.cpp](src/cards/CardDatabase.cpp)
3. Wire the effect in [src/core/EffectHandler.cpp](src/core/EffectHandler.cpp) (spell effects) and/or [src/ai/TurnSolver.cpp](src/ai/TurnSolver.cpp) (lookahead evaluation) and/or [src/core/SpellEffects.h](src/core/SpellEffects.h) (on-cast/combat utilities)
4. Write the cards.json entry using the extended parameters

**Tier 2 examples**: `landfall_damage` (Searing Blaze), `death_trigger_damage` (Searing Blood), `stages_cards` (Light Up the Stage), `on_cast_trigger_max_mv` + `on_cast_trigger_damage` (Eidolon).

**Tier 3 — new template or significant engine change**: The card needs a new `CardTemplate` enum value, a new trigger pattern, or a new zone (e.g., full stack implementation, activated abilities with costs, upkeep counters). Do the following:
1. Read `.claude/skills/mtg-rules.md` for the correct model
2. Read `.claude/skills/mtg-ai.md` if the AI needs to evaluate or use the new mechanic
3. Implement the full C++ pipeline: CardDatabase → EffectHandler → TurnSolver / AIEngine
4. Write the cards.json entry using the new template
5. Build and confirm success

**Tier 4 — genuinely out of scope**: The mechanic requires infrastructure that is explicitly deferred (e.g., full stack with multiple priorities, replacement effects modifying themselves, complex multi-zone loops). **Stop here and check with the user** before proceeding. Present exactly what is unimplemented and why, and propose the bracket-note text. If the user agrees to defer, add the bracket-noted `custom` entry and continue with remaining cards.

Do not pre-emptively escalate to Tier 4. Attempt Tier 3 first — most mechanics that appear complex can be modelled well enough at Tier 3.

### 2d. Review each implementation

After each card, re-read `.claude/skills/mtg-rules.md` Step 4 (Card Code Review) and verify:
- Every oracle text clause is either implemented in the C++ pipeline or bracket-noted as an accepted deferral
- Damage values, targeting types, and all parameters match oracle text exactly
- No clauses are silently omitted without a bracket note

### 2e. Rebuild after all cards

Once all cards in `missing` and all `gaps` are resolved:
```
cmake --build build --config Release
```

Fix any build errors before proceeding to Stage 4.

---

## Stage 3 — Re-run Coverage Check

After the build succeeds, re-run the coverage check to confirm there are no remaining `gaps`:

```
python scripts/analyze_deck.py <deck_path> --coverage-only
```

If new gaps appear (e.g., due to a cards.json mistake), fix them and rebuild. Do not proceed to Stage 4 with outstanding gaps.

---

## Stage 4 — Run the C++ Analyzer

```
python scripts/analyze_deck.py <deck_path> --games 500 --no-rebuild
```

Parse the JSON output:
- `analysis.average_win_turn`: key metric
- `analysis.win_rate`: percentage of games won
- `analysis.mulligan_profile`: the optimised mulligan settings
- `analysis.mulligan_flags`: required-piece flags worth reviewing with the user

---

## Stage 5 — Report to User

Present a concise summary:
1. **Cards implemented this run**: list any new/updated implementations, noting the tier used for each
2. **Average win turn**: from the analysis
3. **Mulligan profile**: the optimised settings
4. **Accepted deferrals**: bracket-noted items the user agreed to skip (Tier 4), with the bracket text shown
5. **Suggested next steps**: any Tier 4 deferrals worth revisiting, or interesting profile observations

Ask the user if they want to explore any aspect further (e.g. comparing card choices, investigating a specific mechanic, or revisiting a deferred implementation).

---

## Decision Rule: When to Stop for User Input

**Proceed without asking** when:
- The implementation choice is unambiguous given the oracle text
- A known simplification is already established in this codebase (e.g., Predatory Sliver modelled as flat lord)
- The gap is clearly Tier 1, 2, or 3

**Stop and ask the user** when:
- Two structurally different implementations are plausible and the choice affects simulation accuracy in a non-trivial way
- The mechanic is Tier 4 (out of scope)
- A new engine feature would require redesigning an existing interface in a way that affects multiple cards

---

## Template Reference

| Template           | Use when |
|--------------------|----------|
| `basic_land`       | Basic land or tribal land (simplified) |
| `vanilla_creature` | Creature with only keyword abilities (no triggered effects, or triggers bracketed as deferred) |
| `mana_dork`        | Creature with "{T}: Add {color}" |
| `direct_damage`    | Damage spell; check all parameters below |
| `draw_spell`       | Fixed draw count; may have spectacle/staged |
| `draw_x`           | "Draw X cards" |
| `counter_spell`    | Counterspell |
| `removal`          | Destroy / exile a permanent |
| `pump_spell`       | "+N/+M until end of turn" |
| `lord_effect`      | "+N/+M to creatures of a subtype" |
| `custom`           | Tier 4 only — must have bracket note |

**direct_damage parameters** (include all that apply):
- `damage`: base damage — REQUIRED
- `targeting`: `"any"` / `"player"` / `"creature"` / `"multi"` — REQUIRED
- `sacrifice_land`: `true` if oracle has "sacrifice a land" as additional cost
- `spectacle_cost`: e.g. `"{R}"` for Spectacle alternate cost
- `landfall_damage`: boosted damage when a land entered the battlefield under your control this turn
- `death_trigger_damage`: damage to creature's controller when targeted creature dies this turn
- `on_cast_trigger_max_mv` + `on_cast_trigger_damage`: Eidolon pattern

**draw_spell parameters**:
- `draw`: card count — REQUIRED
- `spectacle_cost`: Spectacle alternate cost
- `stages_cards`: `true` for "exile the top N cards… until end of your next turn"

---

## Notes

- Always run Stage 1 first — never skip the coverage check.
- Do not modify `src/cards/data/cards.json` until after the review step (2d).
- For multi-colour decks, verify that `min_playable` in the profile is set to at least 1 after the C++ analyzer runs.
- When adding Tier 3 C++ features, read both skills (mtg-rules.md and mtg-ai.md) before implementing — the AI may need to evaluate or play around the new mechanic.
