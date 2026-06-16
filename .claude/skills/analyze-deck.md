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
  - `gaps`: oracle text features missing from the current implementation → must fix before analysis
  - `deferred`: bracket-noted items → **read every bracket note and classify it** (see below)

**Classifying bracket notes — this is mandatory, not optional:**

A bracket note is only an accepted deferral if the mechanic is **genuinely Tier 4** (out of scope for the engine). Every other bracket note is a gap that must be implemented.

The following are NOT acceptable deferrals and must be treated as gaps requiring implementation:
- "Simplified: modelled as a basic land" when the card has additional effects (scry, ETB conditions, sacrifice abilities, depletion counters, cycling, shock costs, draw effects)
- "Simplified: modelled as a dual land" when the real card has ETB conditions (Frostboil Snarl), life costs (Steam Vents), or sacrifice abilities (Fiery Islet)
- "Cycling not modelled" — cycling is a Tier 2/3 activated ability
- "Scry not modelled" — scry is a Tier 2 ETB effect
- "Depletion counters not modelled" — counter management is Tier 3
- Any effect that could affect game outcomes and is implementable at Tier 1–3

**Silently leaving out card effects without a bracket note is unacceptable. Bracket-noting a simplification as "deferred" when it is Tier 1–3 is nearly as bad.** The analyze process exists to produce accurate simulations. A card that is 50% implemented is a bug, not a feature.

If `missing` is empty and all bracket notes are confirmed Tier 4 deferrals, skip to Stage 4. Otherwise, all gaps (including reclassified bracket notes) go to Stage 2.

---

## Stage 2 — Implement All Gaps

For every card in `missing` or with `gaps`, work through the escalation ladder below. **Complete all cards before moving to Stage 3.** Only involve the user when escalation tier 4 is reached.

**Guiding principle — implement FAITHFULLY for accurate results.** Model every card exactly as its real Oracle text and mana cost specify; the simulator's value comes from accuracy, so an approximation that diverges from the real card silently corrupts the analysis. The ONLY case where a simplification is acceptable is when it **provably changes nothing for goldfishing** (a single passive opponent that never blocks, casts, or gains/prevents life — so e.g. flying, first strike, and "target opponent" vs "each opponent" collapse to the same outcome). Even then, prefer faithful and bracket-note the simplification with WHY it is inert. When unsure whether a detail matters, implement it faithfully rather than guess. Real bugs this caught: a land modelled as a free dual that actually costs `{1}` to tap (Ferrous Lake), a land missing its enters-tapped + surveil (Thundering Falls), an animated land wrongly granted haste (Mutavault), and life loss modelled as damage (Leeching Sliver).

### 2a. Fetch oracle text

Use WebFetch to get the authoritative oracle text from Scryfall:
```
GET https://api.scryfall.com/cards/named?exact=<url-encoded card name>
```
Key fields: `oracle_text`, `type_line`, `mana_cost`, `power`, `toughness`, `keywords`.

**Always fetch from Scryfall — never rely on a bracket note's description of the "real" mechanic.** Bracket notes are written by an LLM and may mischaracterise or misremember the oracle text. The Scryfall response is the only authoritative source. This applies equally when implementing a previously deferred card: fetch first, implement from the live oracle text.

**Copy `mana_cost` VERBATIM from the Scryfall JSON — do NOT type a cost from memory.** This warning is separate from the oracle-text one above and is just as important: the mana cost is a small, "obvious-feeling" field, which is exactly why recalled-but-wrong costs slip past a general "fetch from Scryfall" instruction (real bugs found this way: Land's Edge entered `{1}{R}` for `{1}{R}{R}`, Skullcrack `{R}{R}` for `{1}{R}`, Throes of Chaos `{2}{R}` for `{3}{R}`). The model's confidence in the cost is what defeats the warning, so do not trust it — paste the field. For any MV-derived parameter (e.g. `cascade_max_mv` must equal the card's `cmc`), derive it from the fetched `cmc`, never from memory. After writing or editing any card, **run the mechanical guard in Stage 2d** — a prose reminder alone has repeatedly failed to catch this.

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

**Tier 4 — genuinely out of scope**: The mechanic requires infrastructure that does not exist and would take more than a session to build correctly (e.g., full stack with multiple priorities, replacement effects modifying themselves, complex multi-zone loops, casting from graveyard before that infrastructure exists). **Stop here and check with the user** before proceeding. Present exactly what is unimplemented and why, and propose the bracket-note text. If the user agrees to defer, add the bracket-noted `custom` entry and continue with remaining cards.

Do not pre-emptively escalate to Tier 4. Attempt Tier 3 first — most mechanics that appear complex can be modelled well enough at Tier 3.

**Tier 4 does NOT include**: scry, cycling, ETB conditions, life payments, depletion counters, sacrifice abilities, filter mana, or any other land or spell mechanic with a clearly defined effect on a known zone. These are all Tier 2 or Tier 3.

### 2c-bis. Check second-main (post-combat) relevance

By default the engine plays **no post-combat (second) main phase** — in a clairvoyant goldfish, combat creates no new resources, so everything castable is castable in the first main (see the second-main-handling design and `AIEngine::SetSearchPostCombat`). The second main is enabled per-deck only when a card makes it a real decision, i.e. when **combat itself enables a play that was not available before it**. For every card you implement, decide which bucket it is in:

- **Not second-main-relevant** (the vast majority): nothing to do.
- **Spectacle** — the alternate cost unlocks once the opponent has lost life this turn, so the finisher is cast cheaply *after* the attack. Already handled: `params.spectacle_cost` is detected by `GoldFishRunner::DeckUsesSecondMain`, which flips `SetSearchPostCombat` on. Reasonably implemented; no extra work, but confirm the spell's `spectacle_cost` is set so detection fires.
- **Resources generated during combat** — e.g. lands untapped in combat (Bear Umbra, Hidden Strings), or mana/triggers from combat damage. These are **not yet modelled**. Treat as a Tier 2/3 gap: (1) add a `CardParams` flag for the mechanic, (2) implement the resource it generates (in `EffectHandler` / combat utilities), and (3) extend `GoldFishRunner::DeckUsesSecondMain` to detect the new flag so `SetSearchPostCombat` turns on for decks that run it. Without step 3 the engine skips the card's post-combat line and under-rates the deck.

If a second-main-relevant card is present but its detection/wiring is missing, this is a real gap — build it, do not defer silently.

### 2d. Review each implementation

After each card, re-read `.claude/skills/mtg-rules.md` Step 4 (Card Code Review) and verify:
- Every oracle text clause is either implemented in the C++ pipeline or bracket-noted as an accepted deferral
- Damage values, targeting types, and all parameters match oracle text exactly
- No clauses are silently omitted without a bracket note
- If the card's value depends on the post-combat main (spectacle, combat untap, combat-damage triggers), confirm `GoldFishRunner::DeckUsesSecondMain` detects it so the second main is enabled (see 2c-bis)

### 2d-bis. Audit costs against Scryfall (mandatory mechanical gate)

A prose "fetch from Scryfall" reminder has repeatedly failed to stop a recalled-but-wrong mana cost from slipping in (see 2a). So after writing/editing cards — and before trusting any analysis — run the mechanical check, which does not depend on the model choosing to be careful:

```
python scripts/audit_card_costs.py
```

It fetches every costed `cards.json` entry's `mana_cost`/`cmc` from Scryfall and reports any divergence (and cross-checks `cascade_max_mv == cmc`), exiting non-zero on a mismatch. **Fix every mismatch by pasting the Scryfall value — do not rationalise a difference.** Only proceed when it reports "All mana costs match Scryfall" (cards that 429 are rate-limit transients, not failures; re-run or verify them by hand). Treat a non-zero exit as a hard stop, exactly like a build error.

### 2e. Rebuild after all cards

Once all cards in `missing` and all `gaps` are resolved:
```
cmake --build build --config Release
```

Fix any build errors before proceeding to Stage 4.

---

## Stage 3 — Re-run Coverage Check (loop until clean)

After the build succeeds, re-run the coverage check:

```
python scripts/analyze_deck.py <deck_path> --coverage-only
```

Apply the same bracket-note classification from Stage 1. If any gaps remain — including newly reclassified bracket notes — return to Stage 2 and fix them. Repeat until every bracket note is a confirmed Tier 4 deferral and the build is clean.

**Do not proceed to Stage 4 with any outstanding Tier 1–3 gaps.** A simulation run on a partially-implemented deck produces misleading results and defeats the purpose of the tool.

---

## Stage 4 — Generate the Profile

```
python scripts/analyze_deck.py <deck_path> --no-rebuild
```

The analyzer is a fixed-recipe **profile generator** — it produces the deck's
`<deck>.profile.json` (optimised mulligan + per-card scores) and takes no
game-count/depth/budget knobs. It does NOT report win-rate; **evaluation is the
regression suite's job** (see the regression-testing skill / `test/regression.sh`).

Parse the JSON output:
- `analysis.mulligan_profile`: the optimised mulligan settings (also written to disk)
- `analysis.card_scores` / `analysis.hand_score_threshold`: per-card keep values
- `analysis.mulligan_flags`: required-piece flags worth reviewing with the user

For win% / average win turn, run the deck through the regression suite after the
profile is written.

---

## Stage 5 — Report to User

Present a concise summary:
1. **Cards implemented this run**: list any new/updated implementations, noting the tier used for each
2. **Mulligan profile**: the optimised settings (and notable card scores / required-piece flags)
3. **Win rate / average win turn**: from a regression-suite run on the new profile (the analyzer no longer reports these)
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
