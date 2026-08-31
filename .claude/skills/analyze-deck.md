# Deck Analysis Skill

Use this skill when the user asks to analyze a deck, add a new deck, or run a simulation on a deck file. It orchestrates the full workflow: coverage check → implement all gaps (**including wiring the card's play-viewer decisions**) → run the C++ analyzer → **verify (mismatch harnesses + multi-depth sanity + play-viewer decision surface) and iterate to convergence**. **Do not stop between stages to ask for user approval — see the next paragraph; there is no longer a "genuine design decision" exception.**

**NEVER BLOCK ON A QUESTION (user directive, 2026-08-31; CLAUDE.md is authoritative).** Every "stop and check with the user" instruction in this skill — the Tier-4 escalation in 2c, the deferral approvals in 2a, the bucket-B "stop and check" in 2c-ter, `audit_viewer_decisions.py`'s self-guard "which needs the user's OK" — still **requires** its sign-off, but that sign-off is **collected without halting the workflow**. Surface the question in your message so the user sees it if they are reading, then in the same turn take the documented default (or the option you would have recommended), record which way you went in the Stage 6 disclosure, and **keep going to the end**. Do not call `AskUserQuestion` and do not end a turn waiting. This exists because stopping mid-workflow to ask which three params were inert left a 32-core box idle through the user's weekend with a ~36 h value-leaf generation unstarted — the paperwork was real, halting the work for it was not.

**"Analyzed" includes "viewer-ready."** A deck is not done when the search plays it correctly — it is done when a human can also play it in the viewer (`tools/play`) without hitting a decision the engine silently made for them. The play viewer drives real human games through the *same* `--claude-play` decision protocol the search uses, so any interactive choice a card creates (targeting, mode, X, tutor/fetch/dig target, scry ordering, Vial charge) must be **surfaced** to the human. When it isn't, the engine falls back to its heuristic, the human can't override it, and the gap only ever surfaced by the user hitting it mid-game and reporting it. This skill folds that wiring into card implementation (Stage 2c-ter) and its verification into Stage 5 (5h), so a new deck is viewer-ready without the user having to find and correct each missing decision by hand.

**Convergence, not a one-way pipeline.** Stages 1–4 are necessary but not sufficient: a clean coverage check and a generated profile do not prove the deck is correctly modelled or correctly *played*. Stage 5 stress-tests the result, and **any issue it finds — a card-implementation bug OR an AI/search issue — sends you back to Stage 2 (fix) → 2d (review) → 2d-bis (cost audit) → Stage 4 (regenerate profile) → Stage 5 (re-verify). Loop until every Stage 5 check passes.** A deck is only "analyzed" once it converges.

---

## Running this at scale — subagent decomposition (so a long analysis never blows context)

A full analysis of a fresh deck is **long**: fetch + implement + review + viewer-wire every
missing card (Stage 2), then run every verification harness reading piles of game logs (Stage
5). Doing it all in one agent's context will exhaust it mid-run. The work is naturally
parallel and its steps conclude in **compact structured data**, so decompose it — subagents do
the heavy reading/fetching in *their* context and return only conclusions, keeping the
orchestrator's context flat regardless of deck size. (This is the `Workflow` engine's shape: a
**pipeline** for Stage 2, **parallel** verdicts for Stage 5. Use it when the user opts into
orchestration; otherwise spawn agents with the `Agent` tool.)

**Stage 2 — per-card research fan-out → single integrator.** One agent per `missing`/`gaps`
card: it does 2a (curl Scryfall — the fat oracle JSON stays in *its* context, never the
orchestrator's), 2b (rules skill), classifies the tier and the 2c-ter viewer bucket, and
returns a compact draft — `{name, tier, mana_cost, cards_json_entry, cpp_changes:[{file,
what}], viewer_decisions:[{choice, bucket, wiring}], deferrals:[…]}`. The orchestrator collects
N small drafts, not N Scryfall dumps. **Do NOT parallelize the writes:** `cards.json` and the
shared C++ (`CardParams`, `EffectHandler`, `TurnSolver`) can't take concurrent edits, so a
**single integrator agent** (or the orchestrator itself) applies the drafts serially, resolves
C++ conflicts (multiple cards extending the same struct/handler), runs 2d/2d-bis, and builds.
Research fans out; integration is serial.

**Stage 5 — verification sub-stages as agents returning verdicts.** 5a (nonconv/fd-diverge),
5b (multi-depth), 5e/5g (heuristic mining) each read many logs but conclude in a sentence —
one agent each, returning `{clean, outliers:[{gi, explanation}]}`. **5d is a small (~15-20
agent) Sonnet fan-out** — copy that model. The orchestrator sees only verdicts and decides
whether to loop back to Stage 2. The heavy log-reading never touches the main thread.

**The per-deck ledger — durable state across compaction AND handoffs.** Write a git-tracked
`docs/design/analysis-<deck>.md` (per the CLAUDE.md "deferred/shared state goes in git, not
private memory" rule, applied to *in-flight* work) that the run continuously updates: cards
done + tier, viewer classifications, verification verdicts, open convergence loops. Context
becomes disposable — the ledger is the memory a resumed session or a second machine reads to
continue.

**The one caveat.** Subagents do NOT inherit the orchestrator's accumulated decisions (e.g.
"we model Reality Spasm as a wild-mana float", an agreed deferral, a naming convention). Pass
those in each subagent's prompt or have it read the ledger + the relevant `docs/design/` doc —
never assume shared context. This reinforces the repo's no-private-project-memory rule rather
than fighting it.

---

## Core invariant — ONLY a deck/archetype heuristic may narrow the search

**This is the most important rule for any AI/search work in this repo. Read it before touching the
enumerator, the solver, or any pruning/dedup/cap.**

> The only thing permitted to limit, prune, or narrow the search's options is a **deck-specific or
> archetype-specific heuristic that lives in its own provider** (`src/ai/DecisionProviders.cpp` —
> `GenericProvider` / `AntiLifegainProvider` / `HinataProvider` / …). **Nothing else may drop a real
> decision branch.**

Generic enumeration machinery — plan-dedup signatures, breadth caps, "keep the first representative"
tie-breaks, ordering collapses — must be **lossless**: it may fold together branches that are
genuinely identical in outcome, but it must **never pick a winner among real alternatives**. The
instant a generic mechanism keeps one of several *distinct* choices (a tutor target, an X value, a
fetch target, a cast the heuristic didn't rank), it has stolen a decision the search was supposed to
make — and because the dropped alternatives never reach a rollout, the search cannot even discover it
played worse. A "the heuristic picks the first one" shortcut is **not** a heuristic unless that
ordering is a deliberate, reviewed, provider-owned ranking; "first in library/enumeration order" is
arbitrary, not a decision.

Why the home matters: a provider heuristic is a **named, single-file, A/B-testable** narrowing,
measured against the full-search oracle (Stage 5e). A generic limiter is an **invisible** narrowing
scattered in the enumerator that no per-deck review will catch. So when a deck needs its options
narrowed for performance, the fix is **a provider heuristic, never a generic cap**; when you find a
generic limiter that drops real branches, **remove it** and let the provider (or the full search)
decide. Perf is bounded by the provider returning a small candidate set — not by the machinery
silently discarding branches.

*Concrete precedent (2026-06-30): `EnumeratePlans`' `plan_signature` keyed on cast-names alone, so
the dedup collapsed every distinct tutor target to the first-enumerated one — a generic limiter that
forced a single fetch on a bare `cast=Gamble` and hid the alternatives from the depth>0 search. The
fix is to fold the sub-decisions (tutor target / X / Ponder keep / Soulfire count / fetch / land)
into the signature so the dedup is lossless again, leaving the provider's `TutorCandidates` as the
ONLY narrower. In the human-play path (`MTG_HUMAN_PLAY`) that shipped immediately and is unambiguously
correct — the human now sees and picks every legal target.*

*But applying it to the AUTONOMOUS search surfaced a second lesson worth its own warning: a generic
limiter does not merely steal a decision — it can MASK a latent search-quality bug, and removing it
exposes that bug as a real regression. Here the now-visible tutor branches revealed that the
lookahead's leaf valuation OVER-values an early `tutor_to_top` (it committed to a tempo-negative
Enlightened-Tutor-on-T1 line that projects well but realizes a turn slower), so several games got
slower at ANY budget — not budget starvation, a valuation flaw the dedup had been hiding. The right
move is therefore NOT to delete the limiter and accept the misplays (the regression gate forbids
baking real misplays into GT), but to remove it AND make the search's valuation robust enough to not
be fooled by the wider plan set. So this autonomous half was deferred to a dedicated search-quality
step; the human-play half shipped. Takeaway: when you remove a generic limiter and the search gets
WORSE on some games, you've found the real bug the limiter was concealing — fix that, don't restore
the limiter.*

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

Fetch the authoritative card from Scryfall with **`curl`** (WebFetch is 403-blocked on the Scryfall API — do not use it):
```
curl -s -A "MagicDeckTester/1.0" "https://api.scryfall.com/cards/named?exact=<url-encoded card name>"
```
Read these fields from the JSON: `oracle_text`, `type_line`, `mana_cost`, `power`, `toughness`, `keywords`.

**Always fetch from Scryfall — never rely on training recall, on a bracket note's description, OR on the existing cards.json entry.** Recall is unreliable; bracket notes are LLM-written and may misremember; and **cards.json itself is only as correct as its last fill-in — it can be flat wrong** (real cases: Irencrag Feat was entered with a *fabricated* oracle "Add six {R}", wrong cost `{3}{R}{R}`, and ritual 6 — the real card is `{1}{R}{R}{R}`, "Add seven {R}. You can cast only one more spell this turn"). The live Scryfall JSON is the ONLY source of truth. This applies equally when *revisiting* an already-implemented card: fetch first, then diff your implementation against the live oracle text.

**No hand-waving, no over-deferral — implement EVERY clause, or bracket-note it as a genuine Tier-4 deferral with WHY.** This is the failure mode that has bitten this deck most: not misremembering a card, but *knowing* roughly what it does and shipping the "gist" — dropping or approximating an implementable (Tier 1–3) clause — which silently corrupts the simulation. It reinforces the Stage 1 rule: a bracket note is an accepted deferral ONLY when the clause is genuinely Tier 4; hand-waving a Tier 1–3 clause as "deferred" is a bug. After fetching, split the oracle text into its individual clauses and account for EACH — cost, every triggered/activated/static ability, tokens created, additional costs, and **restrictions** (e.g. "you can cast only one more spell this turn"). Real failures from hand-waving/over-deferral here: **Forbidden Orchard** shipped as a plain 5-colour land, dropping "whenever you tap this for mana, target opponent creates a 1/1 Spirit" (a core board-state effect — opponent blockers, extra targets); **Expressive Iteration** approximated as `draw 2 + scry 3` when it actually looks at 3 and puts 1 in hand / 1 on bottom / 1 exiled-playable-this-turn; **Magma Opus** shipped as damage+draw only, dropping its 4/4 token, tap-two, and `{U/R}{U/R} discard → Treasure` mode. (And the separate fabrication mode — Irencrag Feat's invented "Add six {R}" — is why you also never trust the existing entry over Scryfall.) If a clause is truly out of goldfish scope, it MUST carry an explicit `[PARTIAL: ... ; WHY inert/deferred]` note — never a silent omission.

**Every proposed deferral requires USER APPROVAL — you may not unilaterally decide a clause is out of scope.** This is the guard against the hand-waving above: the implementer is exactly the wrong party to self-certify "this won't matter for goldfishing," because that self-justification is how Tier 1–3 clauses got dropped. So when a card has any clause you intend to bracket-note rather than implement, present each proposed deferral to the user — the card, the specific clause, and your reasoning for why it's inert/out-of-scope. **Surface it and KEEP WORKING; do not block on the reply** (see the no-blocking rule at the top of this skill): take the deferral provisionally, mark it PROVISIONAL in the ledger and the Stage 6a disclosure, and collect the explicit sign-off in the closing message. An unanswered deferral is PROVISIONAL, never approved. Unapproved deferrals are not allowed; implement the clause instead.

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

**Tier 4 — genuinely out of scope**: The mechanic requires infrastructure that does not exist and would take more than a session to build correctly (e.g., full stack with multiple priorities, replacement effects modifying themselves, complex multi-zone loops, casting from graveyard before that infrastructure exists). **Surface this to the user and continue — do not stop for the answer** (see the no-blocking rule at the top of this skill). Present exactly what is unimplemented and why, propose the bracket-note text, add the bracket-noted `custom` entry PROVISIONALLY, and carry on with the remaining cards. The deferral stays marked PROVISIONAL in the ledger until the user signs it off in reply to the closing message.

Do not pre-emptively escalate to Tier 4. Attempt Tier 3 first — most mechanics that appear complex can be modelled well enough at Tier 3.

**Tier 4 does NOT include**: scry, cycling, ETB conditions, life payments, depletion counters, sacrifice abilities, filter mana, or any other land or spell mechanic with a clearly defined effect on a known zone. These are all Tier 2 or Tier 3.

### 2c-bis. Check second-main (post-combat) relevance

By default the engine plays **no post-combat (second) main phase** — in a clairvoyant goldfish, combat creates no new resources, so everything castable is castable in the first main (see the second-main-handling design and `AIEngine::SetSearchPostCombat`). The second main is enabled per-deck only when a card makes it a real decision, i.e. when **combat itself enables a play that was not available before it**. For every card you implement, decide which bucket it is in:

- **Not second-main-relevant** (the vast majority): nothing to do.
- **Spectacle** — the alternate cost unlocks once the opponent has lost life this turn, so the finisher is cast cheaply *after* the attack. Already handled: `params.spectacle_cost` is detected by `GoldFishRunner::DeckUsesSecondMain`, which flips `SetSearchPostCombat` on. Reasonably implemented; no extra work, but confirm the spell's `spectacle_cost` is set so detection fires.
- **Resources generated during combat** — e.g. lands untapped in combat (Bear Umbra, Hidden Strings), or mana/triggers from combat damage. These are **not yet modelled**. Treat as a Tier 2/3 gap: (1) add a `CardParams` flag for the mechanic, (2) implement the resource it generates (in `EffectHandler` / combat utilities), and (3) extend `GoldFishRunner::DeckUsesSecondMain` to detect the new flag so `SetSearchPostCombat` turns on for decks that run it. Without step 3 the engine skips the card's post-combat line and under-rates the deck.

If a second-main-relevant card is present but its detection/wiring is missing, this is a real gap — build it, do not defer silently.

### 2c-ter. Wire the card's play-viewer decisions (so the human never has to correct a silent auto-choice)

This is the analogue of 2c-bis for the *human-play* path: a per-card "does this card need wiring beyond `cards.json`?" gate, but for interactive decisions instead of the second main. The play viewer (`tools/play`) plays real human games through the same `--claude-play` decision protocol the search uses (see the claude-play skill); every interactive choice a card creates must be **surfaced** to the human as a decision, or the engine silently resolves it with its heuristic and the human can't override it. **Historically each of these was retrofitted one deck at a time** (`git log tools/play`: scry/surveil/reorder `e65e994`, board-click targeting `1dd8a5d`/`9c68f1a`, Karoo bounce `5ace156`, ETB-dig `23cf22a`, cleanup discard `1ccbe95`, divided-damage `6d74897`, Expressive Iteration `e4200d8`, Vial-as-a-choice `772ff62`, cycle/sac-to-draw dig `27f58f5`). The point of this step is to stop the retrofitting: classify each card's decisions **now**, at implementation time, not when the user hits the gap mid-game.

For every card you implement, ask **what interactive choices does its Oracle text create for the player** (not for the search), and check each against the catalog below. **Read the oracle text carefully for the phrases that signal a choice** — "target" (a creature/permanent/any target, not a bare "target player"), "sacrifice a/an/another \<thing\>", "search your library", "choose one/two", "divided as you choose", "scry/surveil", "discard a card", "return … to … hand" — each is a decision the player makes and the GUI must surface. `scripts/audit_viewer_decisions.py <deck> --no-sweep` runs this oracle-text scan statically (no build/profile needed), enumerating every such phrase not modeled by the card's params — use it as an implementation-time checklist of what to wire, so nothing is dropped before you even reach 5h.

| Interactive choice the card creates | Example cards | Surface mechanism | Wired? |
|---|---|---|---|
| Which line to play this main phase | every deck | `main_phase` plans | yes — always |
| Tutor target | Gamble, Enlightened/Idyllic Tutor | `main_phase` plan **variants** (`tutor_target`) | yes — provider `TutorCandidates` |
| Fetch-land target | fetchlands | plan variants (`fetch_target`) | yes — provider `FetchCandidates` |
| X value ({X} spells) | any {X} spell | plan variants (`chosen_x`) | yes — provider `XCandidates` |
| Ponder keep-vs-shuffle | Ponder | plan variants (`ponder_keep`) | yes |
| Soulfire own-target **count** | Soulfire Eruption | plan variants (`soulfire_own_targets`) | yes (count only — see gaps) |
| Scry / Surveil / reorder-top disposition | Preordain, Serum Visions, Ponder | `scry`/`surveil`/`reorder` decision (`g_play_top_chooser`) | yes — `e65e994` |
| Damage-spell targeting (board click) | Bolt, Skullcrack, Searing Blaze | `target` decision (`g_play_target_chooser`) | yes — `1dd8a5d` |
| Divided-damage allocation | Fiery Justice, Magma Opus | `divide` decision | yes — `6d74897` |
| Karoo bounce-land return | Karoo/bounce lands | `bounce` decision (`g_play_bounce_chooser`) | yes — `5ace156` |
| ETB dig / look-and-take | Acclaimed Contender | `dig` decision (`g_play_dig_chooser`) | yes — `23cf22a` |
| Expressive Iteration 3-way split | Expressive Iteration | `expressive_iteration` (`g_play_ei_chooser`) | yes — `e4200d8` |
| Cleanup discard to hand size | any flooded hand | `discard` decision (`g_play_discard_chooser`) | yes — `1ccbe95` |
| Aether Vial upkeep charge | Aether Vial | `vial_charge` (`SetExternalVialChooser`) | yes — `772ff62` |
| Retrace additional-cost land discard | Throes of Chaos | `retrace_discard` (`g_play_retrace_chooser`) | yes |
| WHICH permanents a multi/own-target spell hits (not the count) | Soulfire own-targets, Crackle | reuses the **`target`** decision (`g_play_soulfire_chooser` → `EnumerateTargetSets`, `source`=spell); the `soulfire_targets` type / `WriteSoulfireDecisionJson` are **dead code** | yes (as `target`) |
| **Modal "choose one/two" (non-damage)** | Reality Spasm (tap vs untap; also "which sources to untap") | — | **NO — PHASE 2: needs an engine-model change, not viewer wiring (see note)** |
| **Cascade / Retrace SEARCH target** (which card the cascade/retrace flip casts) | cascade cards | — | **NO — heuristic-picked; build if the deck needs it** |

Then place each of the card's choices into one bucket:

- **Bucket A — reuses an existing decision type (the common case): nothing to build.** The card's choice is already surfaced (a burn spell → `target`, a scry → `scry`, a Karoo → `bounce`). For the plan-variant sub-decisions (tutor/fetch/X/ponder/soulfire-count), the ONLY per-deck work is confirming the provider's `*Candidates` hook returns **every legal option** — human-play runs unpruned (`MTG_UNPRUNED` + `MTG_PONDER_SEARCH`) so every legal sub-decision appears as a distinct `main_phase` plan variant the human picks among. Per the core invariant, the provider is the sole narrower, and **in human-play it must not narrow at all**. No new chooser is needed; just verify surfacing in 5h.

- **Bucket B — introduces a NEW kind of interactive choice with no matching decision type (the bottom three rows, or anything the catalog doesn't cover): build it now.** This is a Tier 2/3 engine change, done under the same escalation ladder and reviewed in 2d. The pattern is **uniform across every viewer commit above** — replicate it, don't invent. **[tools/play/DECISIONS.md](tools/play/DECISIONS.md) is the registry of all existing decision types and their four wiring sites** — read it to copy a sibling type's shape (including which GUI shape, modal art-grid vs board-click, it uses), and after wiring add the new type's row there and its param→type mapping to `audit_viewer_decisions.py`'s manifest:
  1. **Chooser hook** — a `using <Name>Chooser = std::function<…>` typedef + `extern thread_local <Name>Chooser* g_play_<name>_chooser;` in [src/core/GameLogger.h](src/core/GameLogger.h), and null it in the `RevealLogPause` RAII (line ~305) so it is **inert during search/rollout** and fires only on real human resolution.
  2. **Call site** — invoke the chooser from the resolution code ([src/core/SpellEffects.h](src/core/SpellEffects.h) / [src/core/EffectHandler.cpp](src/core/EffectHandler.cpp)), **gated on the pointer being non-null**, falling back to the existing heuristic when null. This keeps the autonomous engine byte-identical (the search never sets the chooser).
  3. **Protocol emitter** — a `Write<Name>DecisionJson` + its wiring in `RunClaudePlay` ([src/main.cpp](src/main.cpp)): print the decision between `<<<CLAUDE_DECISION>>>`/`<<<END_DECISION>>>` (exit 70) or consume the next `--choices` index, with a `"type"` string and a `heuristic_default` field (as `vial_charge` does). Prefer **enumerated option indices** so it reuses the existing integer `--choices` stream — that ships without a new input model and is the recommended default (see `logs/viewer-issues-plan.md`).
  4. **GUI branch** — dispatch on the new decision `type` in [tools/play/index.html](tools/play/index.html) (the GUI switches on `S.decision.type`), routing the choice into the **central dialog or board clicks using card IMAGES, never text** (the play-viewer decision principle — all choices live in the central dialog / board, never the history panel).

  **Wire the chooser at the SHARED resolution site the real game uses, not the autonomous one.** A claude-play main-phase plan is executed via `TurnSolver::ApplyPlan` (see `AIEngine`'s external-chooser branch), NOT `AIEngine`'s autonomous `ExecutePlan` — so a chooser added only to the autonomous path is **dead in the viewer** (real 2026-07-01 bug: the retrace land-discard chooser was first added to `AIEngine::cast_from_graveyard` and never fired; the fix was to move it to the `apply_one` retrace discard inside `ApplyPlan`, which both real claude-play and the rollout share). Resolution effects in `SpellEffects.h`/`EffectHandler.cpp` (scry, bounce, dig, Soulfire) are already on the shared path; plan-execution mechanics (retrace, Land's Edge) live in `TurnSolver`'s `apply_one`. **5h is what catches this** — a chooser that compiles and is byte-identical can still never fire; only driving the deck through the real protocol proves it does.

  A new decision type is the **"surface it to the user, then keep going"** case (never a blocking stop — see the top of this skill) ONLY if it needs a genuinely richer input model than an enumerated option index (e.g. free-form manual ordering), OR if surfacing it faithfully requires an **engine-model change rather than viewer wiring** — that is a Stage-2/phase-2 engine task, not a chooser. *(2026-07-01 precedent — Reality Spasm: its "untap X target permanents" is modeled as a ritual **wild-mana float** (`untap_x_mana_sources` → `RitualRefloatMana` adds the best-X sources' worth as any-color floating mana), so there are no literal permanent targets to pick and "which sources to untap" is currently a no-op — and the produced mana being any-color is itself a latent mis-model. Surfacing the untap-source choice (or the inert tap mode) requires de-abstracting the float into literal colored untap, which is entangled with the deferred Reality-Spasm→Crackle combo and would shift Hinata GT. Deferred to phase 2; disclosed in 6a as an inert-collapse, not silently dropped.)*

Record each card's bucket-A confirmations and any bucket-B wiring; both feed the 5h surface check and the Stage 6a disclosure (a bucket-B choice you deliberately leave auto-resolved because it is provably inert for goldfishing must be disclosed as a known gap, not silently dropped).

### 2d. Review each implementation

After each card, re-read `.claude/skills/mtg-rules.md` Step 4 (Card Code Review) and verify:
- Every oracle text clause is either implemented in the C++ pipeline or bracket-noted as an accepted deferral
- Damage values, targeting types, and all parameters match oracle text exactly
- No clauses are silently omitted without a bracket note
- If the card's value depends on the post-combat main (spectacle, combat untap, combat-damage triggers), confirm `GoldFishRunner::DeckUsesSecondMain` detects it so the second main is enabled (see 2c-bis)
- Every interactive choice the card creates for the player is surfaced in the human-play path (2c-ter): each is either a reused decision type (bucket A) with the provider's `*Candidates` returning all legal options, or a newly-wired chooser+type+GUI branch (bucket B) — with no card choice left silently heuristic-resolved except a disclosed, provably-inert known gap

### 2d-bis. Audit costs against Scryfall (mandatory mechanical gate)

A prose "fetch from Scryfall" reminder has repeatedly failed to stop a recalled-but-wrong mana cost from slipping in (see 2a). So after writing/editing cards — and before trusting any analysis — run the mechanical check, which does not depend on the model choosing to be careful:

```
python scripts/audit_card_costs.py
```

It fetches every costed `cards.json` entry's `mana_cost`/`cmc` from Scryfall and reports any divergence (and cross-checks `cascade_max_mv == cmc`), exiting non-zero on a mismatch. **Fix every mismatch by pasting the Scryfall value — do not rationalise a difference.** Only proceed when it reports "All mana costs match Scryfall" (cards that 429 are rate-limit transients, not failures; re-run or verify them by hand). Treat a non-zero exit as a hard stop, exactly like a build error.

**Also reconcile the non-cost fields (`scripts/audit_card_fields.py`, workstream ②).** The cost audit only checks cost/cmc; `audit_card_fields.py` extends the same reality-diff to **P/T, types/subtypes/supertypes, keywords (all HARD)** and the **verbatim `oracle_text` (advisory — catches the Irencrag "Add six {R}" fabrication class)**. It diffs cards.json against a **committed** Scryfall snapshot (`src/cards/data/scryfall_reference.json`), so it is fast, offline, and deterministic once the snapshot exists:
```
python scripts/audit_card_fields.py --update    # (network) refresh the snapshot, then commit it
python scripts/audit_card_fields.py             # offline diff -- run this in the gate
```
Run `--update` (and commit the snapshot) whenever you add/edit cards; the offline diff (and `verify_deck.py`'s `card_fields` gate) then reality-checks every field. It **fails closed** if a card is missing from the snapshot — an unfetched card is a pending item, never a silent pass.

### 2e. Rebuild after all cards

Once all cards in `missing` and all `gaps` are resolved:
```
./build.sh
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

## Stage 4 — Generate the *baseline* Profile (NO mulligan-profile generation)

```
python scripts/analyze_deck.py <deck_path> --no-rebuild
```

**Policy (user, 2026-07-17): mulligan-profile generation is NOT part of analyze.** Stage 4 only
produces the **baseline** `<deck>.profile.json` needed to *run and play* the deck — per-card scores
+ **defaults / static keep** (`bottoming_enabled` off). The expensive, commit-bound mulligan
work — the exhaustive keep/bottom generation of the separate `mulligan-profile.md` skill, and any
mulligan *optimisation* (grid search / threshold tuning) — is the **LAST stage and the USER kicks
it off**, only after performance is optimised, play correctness is validated, AND the user's own
feedback loop (perf discussion + viewer testing) has settled. See the "Pipeline ordering" section
of [docs/design/deck-onboarding-hardening.md](../../docs/design/deck-onboarding-hardening.md) and
`mulligan-profile.md`. Rationale: mulligan generation has **two independent showstoppers** — it is
(1) **incorrect if play is wrong** (it optimises against whatever the engine does) and (2)
**infeasible if the engine is slow** (generation is expensive; cf. the ~68–82 s claude-play
mulligan keep-eval). Either alone kills it. It is also commit-bound, so any later engine change — a
perf fix, a validation fix, or an issue the user finds in the viewer — invalidates it; paying for
it before the engine is frozen is wasted work.

The analyzer is a fixed-recipe **profile generator** — it produces the deck's baseline
`<deck>.profile.json` and takes no game-count/depth/budget knobs. It does NOT report win-rate;
**evaluation is the regression suite's job** (see the regression-testing skill /
`test/regression.sh`).

Parse the JSON output:
- `analysis.mulligan_profile`: the **baseline** mulligan settings (defaults/static; also written to
  disk). Do NOT drive exhaustive keep/bottom generation or threshold optimisation here — that is
  the deferred, user-initiated final stage.
- `analysis.card_scores` / `analysis.hand_score_threshold`: per-card keep values
- `analysis.mulligan_flags`: required-piece flags worth reviewing with the user

For win% / average win turn, run the deck through the regression suite after the
profile is written.

### 4a. Provider routing check (MANDATORY — run it before anything is measured)

```
python3 scripts/provider_audit.py            # or: --check to exit 1 on a suspect
```

**Confirm the deck landed on the provider you intend**, which for a brand-new deck is almost
always `Generic`. Do this at Stage 4, not later: the provider decides which heuristics NARROW the
search, so every number produced after it — the profile, the depth sweep, the value leaf, the
ground truth — is measured under whatever routing this step would have caught.

`SelectDecisionProvider` detects the archetype from CARD PARAMS, and several of those params are
archetype-NEUTRAL — `etb_self_creates_tokens`, `sac_creature_outlet` and `reduces_spell_subtype`
describe what a card DOES, not which deck it belongs to. A deck merely containing such a card
silently inherits another archetype's narrowing. That has happened four times (Mirrorwing,
StompySurprise, Minotaur, Dragons), every one found by accident well after the deck was measured,
and in Minotaur's case the borrowed hook DELETED a real decision branch rather than reordering it.

Landing on a foreign provider is not automatically wrong — Knights and slivers_vial both ride
`VialProvider` deliberately. What the core invariant forbids is getting there **by accident**. So:

- **Intended?** Say so in the Stage 6 disclosure, naming the provider and why it fits.
- **Not intended?** Add a signature for the deck and route it ABOVE the offending branch in
  `SelectDecisionProvider` (copy the Dragons block). Build the signature by OR-ing gated params
  from **several different cards**, so a deckbuilding swap cannot silently lose it, and never key
  it on a colourless staple any deck might add (Lightning Greaves was excluded for exactly this).
- A new deck earns its OWN provider only once it has a **measured** hook to hold. Until then it
  gets Generic and no narrowing at all.

---

## Stage 5 — Verify: mismatch harnesses + multi-depth sanity (loop to convergence)

**One-command gate: `python3 scripts/verify_deck.py <deck>` (the enforcement spine, workstream ③).**
It runs the whole battery as a single green gate — coverage (**hard-fails on partial gaps**, not
just missing), Scryfall cost audit, the viewer decision auditor, a static sites-3&4
`viewer_wiring` check (every decision type the deck uses has an emitter in `main.cpp` + a GUI branch
in `index.html`), and the `nonconv`/`fd-diverge` mismatch harnesses — and **exits non-zero unless
every blocking check is green or its failure is signed off** in the per-deck ledger
`docs/design/analysis-<deck>.md` (`## Approved deferrals`). Not-yet-built checks (② field/clause
audit, ④ broadened claude-play sweep) are reported as **disclosed skips, never silently omitted**.
Use it as the Stage-5→6 gate; the sub-stages below are what it invokes and how to trace a failure it
reports. (`--no-network` skips the Scryfall audit; `--no-sweep` skips the runtime gates for a fast
static pre-check; `--write-ledger` records the run + Stage-6a disclosure.)

A clean coverage check and a generated profile do NOT mean the deck is correctly modelled or correctly played. Stage 5 runs the deck through the verification harnesses and a multi-depth sanity sweep, and **reads the actual games**. Run everything at depth > 0 (the harnesses and the interesting decisions only exist when the search runs). Results are deterministic and thread-invariant (see the regression-testing skill), so every flagged seed/turn reproduces exactly for tracing — use `--threads 1` for ordered output.

### 5a. Mismatch harnesses (env-gated, inert by default)

These catch the search lying to itself or to the executor. **A single flagged line is a real defect — root-cause it, never average it away.**

- **Non-convergence** (`MTG_FLAG_NONCONV`): a sound search's proven win turn can only get *earlier* as a game progresses. If a later turn "verifies" a WORSE win than one already proved, the search is inconsistent. Emits to stderr:
  `[nonconv] seed=… turn=… verified_win_now=W EXCEEDS earlier verified_win=W' proven_at_turn=… | hand=…`
- **Commit-the-line fidelity oracle** (`MTG_FD_ORACLE`, requires `MTG_FULL_DEPTH`): the committed line predicted a win by turn P but the REAL game realized it later. Emits:
  `[fd-diverge] seed=… realized_win=R predicted_win=P proven_at_turn=T` — a rollout-vs-real-execution divergence, usually a card whose effect the rollout models differently than the executor.

Run the deck across the suite's seeds at the suite depths, capturing stderr:
```
MTG_FLAG_NONCONV=1 ./build/Release/mtg <deck> --games N --seed S --depth 3 \
  --budget-ms B --lookahead-bottoming --threads 1 2>&1 | grep '\[nonconv\]'
MTG_FULL_DEPTH=1 MTG_FD_ORACLE=1 ./build/Release/mtg <deck> --games N --seed S --depth 5 \
  --budget-ms B --lookahead-bottoming --threads 1 2>&1 | grep '\[fd-diverge\]'
```
For each flagged seed, reproduce the single game (`--seed <S+gi> --games 1 --game-index <gi> --log-dir <dir>`) and read the log to classify: if the rollout and the executor disagree about a card's effect → **card-implementation bug** (fix the card); if it is pure search inconsistency with the card modelled correctly → **AI/search issue**. Either way it forces the convergence loop.

### 5b. Multi-depth sanity sweep (you read the results)

Run the deck at depth 0, 3, and 5 and judge whether the numbers make sense — do not just record them:
- **Monotonicity**: deeper search should be ≥ as good — wins non-decreasing, avg win turn non-increasing *given adequate budget*. A depth that plays WORSE than a shallower one is a red flag.
- **Plausibility**: the win turns must match the deck's realistic clock. A deck whose intended line wins ~T4 but that the suite settings win ~T11 is mis-playing — inspect it.
- **Per-game inspection of outliers**: dump per-game win turns (`MTG_DUMP_WINS=1 … 2>&1 | grep '^\[win\]'`), diff across depths (or vs a known-good arm), and **READ the games that moved or look slow**. Template: the Treasure Hunt / Land's Edge case — the deck cast its draw engine *before* its payoff and discarded the drawn lands at cleanup, wasting the combo; the log made it obvious where `boardAfter`/hand told the real story (ignore rolled-back rollout actions that can leak into the log).

### 5c. Budget-starvation check (search decks)

If 5b shows a deck winning much slower than its line should, confirm the cause before blaming the AI: re-run the slow game at a much larger `--budget-ms`. If a bigger budget recovers the good line (monotonically), the suite budget is **starving** this deck — note the threshold for the suite's time-budget sizing; it is not a logic bug. (Seen on Treasure Hunt: the Land's Edge combo needs ~b2000 at d5; b200 starves it.)

### 5c2. Horizon-honest tie-break check (MANDATORY for a new deck)

```
python3 scripts/leaf_tiebreak_check.py decks/<Name>/<Name>.cod
```

`DecisionProvider::GradesNoWinLeaf()` ships **DEFAULT ON**: when a rollout reaches the horizon with
no win, the tie-break is the resulting position's OPPONENT LIFE instead of every hopeless line
scoring the identical `max_turns+1` and falling through to `plan.value`. Measured over 48,300 paired
games it is worth -58 turns across the suite (hinata -45, treasure_hunt -13, fivecolour -4,
mirrorwing -3). The default is ON deliberately, so a new deck inherits the fix rather than waiting
for someone to notice it should opt in.

Its known failure mode -- worth naming when you hit one -- is that **opponent life at the horizon is
a DAMAGE-RACE proxy, so a deck whose value is STORED rather than expressed as damage by the horizon
has its build-up priced at zero.** Combo, storm and ramp decks bank resources for a discontinuous
payoff; rituals and enablers lower nobody's life on the turn they are cast. Dragonstorm is the
measured instance: the tie-break truncated a 13-spell turn-6 chain to six spells and turned a turn-8
win into a LOSS.

Reading the verdict:
* **KEEP THE DEFAULT / NEUTRAL** -> nothing to do.
* **NO SIGN AT THIS SAMPLE** -> not a pass. Binding rates vary by orders of magnitude between decks
  (a deck that wins well inside the horizon can change fewer than 1 game in 1,000), so a small run
  reporting "0 changed" is indistinguishable from "never fired". Re-run at the `--blocks` the script
  suggests before concluding.
* **OPT OUT** -> **THE METRIC IS THE BAR.** This lever exists solely to lower avg turn-to-win, so a
  deck whose average it raises opts out; you do not owe a mechanism story first. Add
  `bool GradesNoWinLeaf() const override { return false; }` to the deck's provider and record the
  measurement in `docs/design/horizon-honest-leaf.md`.

**The two gates are separate, and you run BOTH** (USER, 2026-08-23 -- the general adoption bar):

1. **"Are we improving the play generally?"** -- the aggregate average at PLAY settings on a large
   sample. That is what this script measures. Some seeds being worse is fine if the average improves.
   Escalating depth/budget is NOT evidence here: the tie-break only fires when a rollout reaches the
   horizon *without* a win, so raising depth/budget lets the search find real wins inside the horizon
   and the leaf stops being consulted. "It closed at 20x" says the lever stopped FIRING, not that its
   valuation was sound.
2. **"Are we preventing search from finding a win?"** -- tested GAME BY GAME. Take a regressed game
   and re-run it at **unlimited budget** and the **depth at which it won BEFORE** the change. If the
   win returns, it is recoverable and only clause 1 decides. If it never returns, the change
   structurally deleted the line and that blocks adoption on its own.

Two traps, both hit for real on dragonstorm s5005 gi227: escalate to CONVERGENCE (that game needed
100x budget, not 20x, and had been wrongly filed as unrecoverable), and check whether the game even
regresses at PLAY settings first (it did not -- only at the d3 gate cell).

See `docs/design/horizon-honest-leaf.md`.

### 5d. Claude-play validation sweep (~15-20 games)

As the final verification step, run a **small (~15-20 game) claude-play sweep** — an
independent correctness sweep where a Claude agent (not the encoded AI) drives each game's
main-phase decisions and flags illegal/missing plans, wrong state transitions, or games
it wins earlier than the search. Read `.claude/skills/claude-play.md` first (especially
**Rule 0** — read the deck's cards from `src/cards/data/cards.json` before judging
anything, and **"Running a sweep"** — keep the sweep SMALL and run the agents on Sonnet;
this is the most expensive step in the chain).

**How to run it.** Pick a base seed disjoint from the regression suite's seeds (so the
sample is fresh, not games the suite already covered) and sweep ~15-20 game-indices
starting at 0 (each game replays deterministically from `base_seed + game_index`). Fan the
games out with the **Workflow engine — one `model: 'sonnet'` agent per game** — each agent
reads the deck's cards.json entries once, benchmarks the search for its game, plays the
stateless-replay protocol to completion, and returns `{ai_win, claude_win, choices,
flags[], summary}` (see "Running a sweep" in the claude-play skill). Keep it small: a clean
~15-20 game sample establishes the bug-finding signal without risking the session cap; only
expand if a flag needs more repros. Aggregate the returned objects, do not re-derive them.

**What it gates.** This is a **backstop, not a hard gate on win-turn deltas**: a guided
Claude rarely beats the strong, clairvoyant search, so treat `claude_win < ai_win` as a
*weak* signal (inspect the search, but a slower Claude is usually just Claude playing
worse). The **strong** signal is the legality/invariant flags — an illegal/impossible
plan, a missing legal play, or a wrong state delta. **Every such flag must be
root-caused, never averaged away**, and feeds this same convergence loop: an engine/card
bug goes back to Stage 2; a pure search issue is handled like any 5b outlier. When the
runner flags a suspected **data** error (the engine faithfully matches cards.json but
the result still looks wrong), resolve it against Scryfall exactly as in 2a/2d-bis — that
backstops the non-cost fields the cost audit does not check (P/T, trigger thresholds,
other behavioral params). A flag verified as a false positive (a card-data misread by the
agent — the most common kind, per the prototype sweeps) is not a defect; record it as
dismissed with the reason.

### 5e. Heuristic accuracy vs the full-search oracle (cast ordering, dig, targeting, …)

This sub-stage enforces the **core invariant** above (only a deck/archetype provider heuristic may
narrow the search). It catches two failures: a provider heuristic that narrows *wrongly* (picks
worse than the oracle), and a *generic* limiter that narrows at all (which must be removed, not
tuned). Several decisions are made by a cheap **heuristic that narrows the search** rather than by
the search branching over every option: cast ORDER within a turn (`DecisionProvider::
CastOrderRank`), dig-source choice, removal/pump targeting, tutor/fetch targets. A
narrowing heuristic is only safe if it picks **what the full search would have picked** —
otherwise it silently leaves quality on the table or causes a play error. This sub-stage
verifies that against the search itself as an **oracle**, then locks the heuristic in with
a definitive **with/without** A/B. (User direction 2026-06-23: heuristic proposes, search
picks; proposals must be GROUNDED in full-permutation-search knowledge; the definitive test
is comparing with vs without the heuristic.)

**The oracle levers** (env-gated, open the narrowed space to the full search):
`MTG_SEARCH_ORDER` enumerates cast orderings (deduped by end-of-phase state);
`MTG_UNPRUNED` opens every branch-narrowing gate (tutor/fetch search-all, dig, caps).
Neither is shipped on by default — they are the reference the heuristic is measured against.

**The differential workflow** (reusable for any narrowing heuristic):
1. **Find divergences.** Run the deck twice with `MTG_DUMP_WINS=1` (prints `[win] gi=N wt=M`
   per game to stderr) — once plain (heuristic), once with the oracle lever — and diff the
   per-game win turns. `--threads 1` for ordered output. The games whose win turn differs
   are where the heuristic and the full search disagree.
2. **Reproduce one game.** A 300-game run's `gi=N` is NOT `--game-index N --games 1` (that
   uses seed = base, not base+N). Reproduce 300-run `gi=N` at base seed `S` as a single game
   with: `--seed $((S+N)) --game-index $((N%10)) --games 1` (seed = base+N, opponent-spawn
   pattern = N%10; see `GoldFishRunner.cpp`). The batch-vs-single indexing footgun has bitten
   analysis before — always use this recipe.
3. **Classify each divergence** with `MTG_FD_ORACLE=1` (flags `[fd-diverge] realized>predicted`):
   - **No `[fd-diverge]` → coverage gap.** The search both predicts AND realizes the slower
     turn (rollout and executor agree); it simply never explores the faster line. The
     heuristic should ADD that line. *(Burn: 5/300 prowess-ordering games — Swiftspear before
     Lightning Bolt is +1 prowess damage = a turn earlier.)*
   - **`[fd-diverge]` → true mismatch.** The search predicts a win it can't realize — a
     rollout/executor or win-PROJECTION bug, fix it directly (this is the real search-quality
     defect, not an ordering gap). *(Burn gi=215, CONFIRMED cross-cast mana over-acceptance: the
     search scores a T4 lethal [Skullcrack {1}{R}, Searing Blaze {R}{R}] = 4 mana, execution
     realizes T5. T4 has only **3 lands** — Shard Volley sacrificed one on T3 — so execution pays
     Skullcrack (2), leaving 1, and Searing Blaze ({R}{R}) can't be paid → slips to T5. The
     single-turn Solve check `pool.CanPay(combined)` correctly rejects 4-mana-on-3-lands, so the
     over-acceptance is in the LEAF forward rollout (`SimulateToEndImpl`) scoring this line a T4
     win; its projected T4 pool exceeds execution's 3 lands. Fix = the leaf mana/land projection.)*
     **Counting lesson (this case cost three wrong diagnoses): instrument the actual numbers, do
     not eyeball.** Count LANDS, not the log's `battlefield` array (it includes CREATURES — map
     IDs through `cardNumbering`); and confirm a mana failure with the real available total
     (a temporary `available.Total()` print at the `TapForCost` site) rather than inferring from
     lands-in-play. Player-life drops can be self-inflicted (Eidolon pinging you on your own
     cheap spells), so they do NOT prove an opponent creature is attacking.
   Trace the actual play with `MTG_ORDER_TRACE=1` (prints the executed cast order per main
   phase) and `--log-dir` (per-game JSON: per-turn life/damage timeline) to see WHY.
4. **Derive the rule** from the divergences, grounded in what the oracle actually does — not
   from card-text intuition. To mine rules across many cases, enumerate the earliest-turn-win
   orderings (the oracle's optimal set) and read off what they share. Beware: the full
   permutation search is NOT a clean oracle at a fixed budget — it dilutes the node budget
   across orderings and can commit a *slower* line (burn gi=290: `MTG_SEARCH_ORDER` lost a
   turn). That is itself the argument for a cheap deterministic heuristic over shipping the
   permutation search.
5. **Encode as a provider proposal.** Put the rule in the provider (e.g. `CastOrderRank`:
   prowess creatures before noncreature spells; on-cast self-damage sources like Eidolon
   last). For **situation-dependent** orderings the heuristic cannot resolve statically —
   a card whose resolution triggers a mid-turn re-solve breakpoint (draw / staging / cascade,
   e.g. Light Up the Stage) — do NOT reorder; leave that set's order to the search. The
   heuristic proposes; the search picks the ambiguous cases.
6. **Validate definitively (with/without A/B).** Re-run step 1 comparing the heuristic ON vs
   OFF per game. Accept only if net-positive with **every** regression explained: d0 (no
   search) greedy churn is acceptable (per user); a search-budget line-shift or a
   left-to-search staging game is expected; an unexplained SLOWER game (including a win
   going to unwon) or a true `[fd-diverge]` is not. The aggregate fingerprint can hide
   per-game churn (faster+slower cancel in the avg) — always diff per game against
   `test/gt_logs/*.wins`, never trust the aggregate avg alone.

This generalizes beyond ordering: the same oracle-diff → classify → derive → encode →
with/without-validate loop applies to dig-source selection, targeting, and tutor/fetch
narrowing. Each heuristic added this way is disclosed in Stage 6a.

### 5f. Runtime-reducing heuristics (branching pruning — a performance GATE)

5e keeps heuristics *accurate*; this sub-stage uses the same machinery to make the engine
**cheaper to run**. Under the pipeline-ordering policy (Stage 4) performance is a hard **gate** the
deck must clear *before* the deferred, user-initiated mulligan stage — because slow generation is
one of that stage's two showstoppers. So the freed budget here is not spent on inline mulligan
optimisation (that no longer happens during analyze); it makes the *later* user-kicked-off
mulligan generation feasible (fits an overnight window; more `GRID_GAMES`, threshold candidates,
deeper optimisation *when the user runs it*). The analyzer's
cost is dominated by the per-node search rollout (the land×gate grid alone is hundreds of
thousands of d5 games), and that cost scales with the **branching factor** — how many actions
`CollectActions` emits per node and how wide each candidate set is (subset enumeration is
`O(2^candidates)`). A heuristic that narrows a candidate set to what the full search would
have chosen cuts node count super-linearly at no quality cost. (User direction 2026-06-23:
heuristics may prune to curb branching/perf, **never** as a quality shortcut; ALWAYS keep the
unpruned A/B; the analyzer process should propose and test these so overnight runs can spend
the savings on mulligans.)

**Find the expensive decision points.** Profile where branching explodes: the trace toolkit
(`MTG_TRACE`, see `src/core/Trace.h` / the search-perf memory) and per-node action counts.
The usual offenders are wide candidate sets — X-cost values (full `0..maxX` range), tutor/fetch
targets, dig sources, removal/pump targeting — and flooded hands that emit many redundant
casts. Each is a place where a provider heuristic can narrow `{all legal options}` down to the
few the search actually picks.

**Propose a pruning heuristic, grounded in the full search.** Put it behind the provider
interface and the `MTG_UNPRUNED` gate so the wide set is always recoverable. The proposal must
come from what the unpruned search chooses (5e's oracle), not intuition — e.g. X-cost: the
goldfish-optimal X is almost always "all affordable mana," so `XCandidates` proposes
`{max_affordable}` and the search confirms; tutor: the provider returns the 1–few cards that
matter. One candidate = a decided heuristic (no branch); several = narrowed-but-searched.

**Validate with the unpruned-vs-pruned A/B (the definitive test).** Run pruned (default) vs
`MTG_UNPRUNED` and require: (a) **quality parity** — per-game win turns identical, or every
difference explained and net-neutral/positive (diff per game vs `test/gt_logs/*.wins`, never
the aggregate fingerprint; same discipline as 5e step 6); and (b) **a real cost win** —
measure node count / per-deck analyzer wall-time before vs after (the regen script logs
per-deck timing). Accept only if quality holds AND cost drops. A pruner that changes results
is a quality bug, not a perf win — fall back to `MTG_UNPRUNED` behaviour and re-derive.

**Reinvest the savings.** Once a pruner is locked in, the analyzer's freed budget goes back
into the mulligan optimisation (raise `GRID_GAMES`, add threshold candidates, widen the land
grid) — the whole point is a *better* profile at the same wall-clock. Disclose every pruning
heuristic in Stage 6a alongside the accuracy heuristics, and note its measured cost saving.

### 5g. Heuristic GENERATION — earliest-win rule-miner (`MTG_DUMP_EWINS`)

This is how you DERIVE a new deck's DecisionProvider heuristics (and audit an existing deck's)
from the full search itself instead of card-text intuition — automating step 4 of the 5e
workflow. For each **real pre-combat decision** the runner scores EVERY candidate top-level
play (the same `EnumeratePlansWithLand` candidates the search ranks — cast ORDERINGS included
when `MTG_SEARCH_ORDER=1`) by the EARLIEST full-game win it leads to: apply the play, run its
combat, then full-search the rest of the game with no cross-candidate pruning, so each gets its
TRUE earliest win (`TurnSolver::EnumerateEarliestWins`). One `{"ewins":...}` JSON line per
decision → **stderr**. Inert (zero overhead) unless `MTG_DUMP_EWINS` is set. EXPENSIVE (a full
rollout per candidate) — bound with games/budget; overnight-safe.

**One command** (drives the dump over all decision turns + mines the rules):
```
GAMES=500 SEEDS="2002 3003 4004" scripts/mine_heuristics.sh <deckfile> [profile]
# wraps: MTG_DUMP_EWINS=1 MTG_SEARCH_ORDER=1 MTG_DUMP_EWINS_TURN=0 runner 2>logs/heuristics/<name>.ewins.jsonl
#        + python3 scripts/analyze_earliest_wins.py <that file>   (appends each seed, analyses together)
# knobs (env or positional): GAMES(/seed) SEEDS DEPTH BUDGET TURN  (TURN=0 = every turn; a number = just that turn)
```

**Sizing (overnight):** default **GAMES≈500 per seed × 2–3 disjoint SEEDS** (the ≥2-seed
overfit guard). INCLUSION/LAND signals stabilise in the low hundreds; ORDER rules are the sparse
one and want ~500/seed (and on order-irrelevant lord/anthem decks they stay sparse no matter the
count — that's real, not under-sampling). Measured cost (depth 5, budget 3000, all turns, 24-core
box): ~0.6 s/game narrow (burn), ~1.3 wide-board (slivers), ~2.1 combo (antilife) → 1,500 games
mines in ~15 min / ~30 min / ~55 min respectively. So even a worst-case combo deck is well under
an hour; bump to GAMES=1000 for an order-heavy deck. New deck of unknown cost: start GAMES=300 to
gauge the rate, then scale. Diminishing returns past ~1,000–1,500/seed.

It prints three grounded reports with **support / conflict** counts:
- **ORDER rules** — ordered card pairs whose faster lines agree on direction. *(burn: cast
  Lightning Bolt before Light Up / Searing Blaze, Goblin Guide before Lightning Bolt; th:
  Land's Edge before Treasure Hunt — the real combo line.)*
- **INCLUSION rules** — per-card avg win-turn delta from casting it THIS turn (− helps, + slower),
  shown with a help/hurt/neutral SPLIT. *(burn: Shard Volley +0.33; knights: lords/anthem all
  negative → deploy them; th: Land's Edge −0.44 / Treasure Hunt −0.24 → cast the combo pieces.)*
  Read the split, not the mean: a + card may be a **setup/combo-timing** card (good with a follow-up)
  or an **inert-in-goldfish ability** — the script can't distinguish them and the safe default for
  BOTH is "leave to the search" (see the encoding table). Gate a + card only on a confirmed misplay.
- **LAND / FETCH** — which land / fetch target the earliest-win lines pick. *(antilife: Forest
  first.)*

**Translate candidates → provider** (then validate each, below):
| mined rule | provider encoding |
|---|---|
| 0-conflict ORDER "A before B" | `CastOrderRank`: give A a lower rank than B (archetype override). High-conflict pair → leave to the search (don't encode). |
| INCLUSION strongly − ("cast it") | it already gets cast; only act if it's being *deferred* wrongly — usually no change. |
| INCLUSION + ("slower early") | almost always **leave to the search** — a cast-gate is the rare exception, only for a CONFIRMED greedy misplay (e.g. antilife Reverent self-brick). The miner CANNOT tell *why* a card is slow early; classify it yourself from the oracle text: a **setup/combo-timing** card (helps only with a follow-up, e.g. Hatchery Sliver: T2 → replicate one-drops T3 → duplicate a lord = a real win the **lookahead already finds**) must NOT be gated; an **inert-in-goldfish ability** (e.g. Crystalline Sliver's shroud — no opponent removal to dodge, so it's just a vanilla 2/2) needs no rule either, the search casts it when nothing better exists. Hard-gating either would lose games. |
| LAND / FETCH skew | `FetchCandidates` / land-pick priority (the Forest-first pattern). |

**These are PROPOSALS, never auto-shipped.** Each encoded rule still goes through the 5e step-6
**with/without per-game A/B** (diff vs `test/gt_logs/*.wins`): accept only if net-positive with
every regression explained. The dump can over-fit a few seeds, and the permutation oracle can
itself commit a slower line at fixed budget (step-4 caveat) — so a rule that doesn't survive the
A/B is dropped. Mine across ≥2 seeds before trusting a marginal rule.

**New-deck flow (incl. overnight):** once the deck is playable + profiled (Stages 1–4), run
`mine_heuristics.sh` (a high `GAMES` count is the overnight-friendly knob) → read the candidate
report → encode only the clear, generalizable rules an *archetype* provider needs (most decks
ride `GenericProvider`; add a subclass only when the mined rules diverge from generic) → A/B
each. The mining is unattended-safe; the encode+A/B is the next attended step. Disclose every
encoded heuristic in Stage 6a.

### 5h. Play-viewer decision-surface check (no silent auto-choices)

This is the verification backstop for the 2c-ter wiring: confirm the viewer actually surfaces every interactive choice the deck's cards create, so the user is not the one who discovers a missing decision mid-game. Because the play viewer and the claude-play sweep share **one** decision protocol (the `--claude-play` JSON contract), you verify the viewer by exercising that protocol — no browser needed.

**Run the mechanical gate first (`scripts/audit_viewer_decisions.py`).** This is the viewer analogue of 2d-bis's cost audit — a prose "classify each card's decisions" reminder has the same failure mode as the prose cost reminder (one card's choice ships silently auto-resolved), so a script removes the judgment call:

```
python scripts/audit_viewer_decisions.py <deck> <deck>.profile.json [base_seed] [n_games]
```

It reads each deck card's `cards.json` params, computes the **expected** decision-type set from a param→type manifest (the machine half of [tools/play/DECISIONS.md](tools/play/DECISIONS.md)), drives a bounded `--claude-play` sweep, and diffs expected vs. surfaced. It reports three outcomes: a **HARD MISS** (a card the sweep DID cast whose decision never surfaced → silently heuristic-resolved → back to 2c-ter and wire it, a hard stop like a cost mismatch); **UNVERIFIED** (the card was never cast in the sweep → confirm with the targeted repro below); and a **SELF-GUARD FAILURE** (a card carries a choice-bearing `cards.json` param with no manifest row → a new interactive mechanic was added without viewer wiring/mapping; add the row to the script's `MANIFEST` and wire it per the registry). It also fails loudly on a **DRIVER FAILURE** (games stuck in a decision loop). Treat a HARD MISS or SELF-GUARD/DRIVER FAILURE as a hard stop; UNVERIFIED rows fall through to the targeted repro. **When you wire a new decision type (bucket B), add its `cards.json` param → type mapping to the manifest and a row to the registry so the gate stays exhaustive as cards.json grows.**

**The auditor also runs an ORACLE-TEXT CROSS-CHECK (advisory).** The param manifest can only see choices that were *implemented as params* — so a param-only audit is blind to a choice the card modeling **dropped** (the "hand-waved the gist" failure 2a/2d warn about: a Tier 1–3 clause that never became a param is invisible to it). The cross-check therefore reads each card's real `cards.json` oracle text for choice phrases — anything that says **"target"** (creature/permanent/any target — not bare "target player", which is no choice in goldfish), **"sacrifice a/an/another \<thing\>"** (a real choice of which; not "sacrifice this"), "search your library", "choose one/two", "divided as you choose", "scry/surveil" — and flags any the params don't model. **This is the primary tool for deciding what the GUI must hook up: read the card carefully, and every such phrase is a decision to surface, model, or disclose as inert.** Each advisory is triaged: a genuinely dropped choice → back to Stage 2 (model it) then 2c-ter (wire it); a goldfish-inert clause (usually already carrying a `[…inert…]`/`[…deferred…]` bracket note, which the check annotates) → disclose in 6a; a regex false match → ignore. It never fails the exit code (prose regex is fuzzy), but a well-analyzed deck reports "no oracle-text choice phrase left unmodeled."

**Reuse the 5d sweep as the observational check for anything the auditor left UNVERIFIED.** From each card's 2c-ter classification, build the set of decision `type`s (and plan-variant sub-decisions) the deck *should* emit. Have each 5d agent record the decision `type`s it actually saw and the sub-decision variants it was offered (e.g. did casting the tutor offer every `tutor_target`? did the burn spell emit a `target` decision rather than resolving pre-aimed?). Aggregate across the sweep and **diff against the expected set**. The failure signature is *"the game advanced past a card's choice without a decision firing"* — that means the heuristic silently resolved it, which is exactly the class of gap the user currently finds by hand. Any such card goes back to 2c-ter (wire it), not to the user.

**Targeted repro for anything the sweep left UNVERIFIED — the auditor automates this.** A decision only appears when a game reaches the triggering state, so a card that rarely gets cast may never surface in the fixed sweep (the auditor reports it UNVERIFIED, not a miss). To resolve each, let the auditor seed-search a game that casts it:
```bash
python scripts/audit_viewer_decisions.py <deck> <deck>.profile.json <seed> <budget> --verify-card "<name>"
# -> VERIFIED (decision surfaced), HARD_MISS (cast but never fired -> wire it), or
#    NOT_FORCED (couldn't force the cast in <budget> games)
```
It biases plan selection toward casting the named card across many deterministic games, then confirms the expected `type` surfaces. **A NOT_FORCED result for a decision whose state the forward driver can't manufacture (retrace needs the card already in the graveyard; a combo payoff needs its setup) is expected — fall back to a hand-built `--choices` line** that reaches the state and confirm the `type` fires (exit 70) with the full option list:
```bash
./build/Release/mtg <deck> --profile <deck>.profile.json --claude-play \
  --seed <S> --game-index <GI> --max-turns 8 --choices "<CSV that casts the card>"
# expect a <<<CLAUDE_DECISION>>> block whose "type" matches the card's catalog row
```
A burn spell must emit `target`, a Karoo must emit `bounce`, a scry must emit `scry`, and a tutor's `main_phase` plans must include a variant per legal target. **Targeting must be a real choice, not a pre-baked target** — confirm the `legal_targets`/`plans` list is *complete* (every legal target the human could pick), which is the human-play unpruned guarantee; a truncated list is a surfacing bug even though a game still "works."

**Gate:** no card in the deck has an interactive choice the viewer silently auto-resolves — each is surfaced, or is a Stage 6a-disclosed known gap the user has signed off on as provably inert for goldfishing.

### 5i. Rollout-quality diagnosis — d0-vs-search divergence digest (optional; rollout-only decks)

**When to run:** a **rollout-only** deck (no `value_leaf`/`value_play` model in its profile) whose greedy
**d0 loss-penalized avg-win-turn is far worse than the search** (e.g. Dragonstorm's d0 ~7.1 vs d5 ~4.8).
Because the search finishes every leaf with the greedy d0 policy, that gap poisons the search at every
depth — so closing it is the highest-leverage tuning available. This is the **d0-specific complement to
5g**: 5g mines what the *optimal* play is (earliest-win); this finds where the *rollout policy itself*
misplays, which is exactly where leaves are mis-evaluated. Full method + two worked examples:
[docs/design/dragonstorm-d0-divergence-digest.md](docs/design/dragonstorm-d0-divergence-digest.md).

The loop (each step's tool is committed):
1. **Instrument** — `MTG_DIVERGENCE_LOG=<file>` (inert unless set; [src/ai/AIEngine.cpp](src/ai/AIEngine.cpp)) logs, per real pre-combat decision, the search's plan vs what greedy d0 would do at the SAME state. Run single-threaded, a few hundred games over ≥2 train seeds.
2. **Digest** — `python3 scripts/rollout_divergence_digest.py <file.jsonl> …` clusters the divergences (rate by turn, greedy-idles-vs-over-acts shape, cards the search plays that greedy misses / greedy wastes). These are **HINTS, not payoffs**.
3. **Hypothesize** — map a surviving pattern to an **archetype** `DecisionProvider` hook (`ExtraLethalDamage`/`HasExtraLethalModel` for an unrecognized wincon, `CastOrderRank` for sequencing, `ArchetypeCardValue` for valuation) — never `GenericProvider`. Keep it cheap (d0 runs in every rollout): integer math, no `GameState` copy.
4. **Cost-test** — build behind a temp env gate; measure LP on/off on the train seeds, then validate on the regression suite (per the heuristic-optimization skill) and adopt default-on with an off-switch.

**Two guardrails the digest cannot enforce for you — both cost real retreads to learn:**
- **COUNT ≠ COST.** The digest counts divergences; a tie (two lethal lines, same win turn) counts the same as a blunder. A high count is a hint, not a payoff — cost-weight by win-turn impact and validate on LP, never the count. *(The Lotus-Bloom guard had 500+ counts and was a measured NO-OP — the "wasted" plays were free suspends.)*
- **BLIND d0 LP VALIDATES ONLY INFORMATION-ADDING RULES.** A rule that ADDS lethality/board information (the go-off recognizer: recognize a wincon the leaf would actually execute) helps d0 AND the search — monotone, safe. A rule that PRUNES a greedy option can improve blind d0 while WORSENING the shipped search, because the rollout's job is **faithful simulation, not optimal play**: a blind short-horizon leaf that "correctly" holds a resource just durdles (never reaches the payoff it can't foresee), so the search reads worse leaf win-turns. *(The slow-dragon guard: blind d0 −0.73 but shipped d5 +0.37 — rejected.)* **Judge option-prunes on the SHIPPED-config LP, not d0. Prefer information-adding rules.**

Worked example #1 (shipped): Dragonstorm's `DragonstormProvider::ExtraLethalDamage` — the rollout never saw the storm go-off as lethal (Dragonstorm has `direct_damage 0`; its dragons' Scourge ETB resolves later), so every leaf cast it late as board-dev and the search couldn't tell a T3 kill from a T5 durdle. The hook projects the go-off burst → d0 −0.33, d3/d5 −0.05..−0.10, zero searched-depth slowdowns. **As with 5g, these are PROPOSALS, never auto-shipped** — each survives the regression A/B (with every per-game move explained) or is dropped.

### Convergence criteria

Loop Stage 2 → 2d → 2d-bis → Stage 4 → Stage 5 until ALL hold:
1. Coverage clean (Stage 3) and costs audit clean (2d-bis).
2. **Zero `[nonconv]` and zero `[fd-diverge]` lines** across the tested seeds.
3. Multi-depth results are monotonic and plausible, with **every outlier game explained** (legitimate line, budget starvation, or a fixed bug — not an unexplained slowdown).
4. The (~15-20 game) claude-play sweep (5d) ran, and **every legality/invariant flag is resolved** — fixed (engine/card/data bug) or dismissed with a reason (verified false positive). Win-turn deltas are noted but do not block.
5. Any narrowing heuristic introduced/relied on for this deck (cast ordering, dig, targeting, tutor/fetch) passed the **5e oracle diff + with/without A/B** — its proposals match the full search where it matters, with every per-game regression explained.
6. **Play-viewer decision surface (5h) is clean**: `scripts/audit_viewer_decisions.py` reports no HARD MISS, no SELF-GUARD FAILURE, and no DRIVER FAILURE, and every UNVERIFIED row was confirmed by a targeted repro — every interactive choice the deck's cards create is surfaced in the human-play path, with no card's choice silently heuristic-resolved except a Stage 6a-disclosed, provably-inert known gap the user signed off on.

Only a deck that satisfies all of these is "analyzed." Report (Stage 6) must state which checks were run and their outcomes.

### 5i. Discard analysis — the BUCKET policy (search-informed, per deck)

Every deck gets an AI-authored, measured cleanup-discard policy (user ruling 2026-08-07: no
general discard heuristic). **The AI gives it its BEST SHOT and reports to the user** (user,
2026-08-21): author the complete policy yourself from the deck's cards plus the evidence
below — do not ask the user to design the buckets — then present it with your rationale and
any doubts flagged; the user amends and approves before anything ships. The generic max-MV
tier is only the fallback while no authored rule exists ("too arbitrary" — it embeds "most
expensive = most expendable", which is backwards for payoff decks: it ranked Creature
Giving's engine first to pitch, and shed FiveColour's Progenitus for a measured 1-turn cost).

#### The shape: buckets (USER doctrine, 2026-08-21)

"The best approach is almost always bucketing." Partition the hand into ROLES, give each a
keep QUOTA, and build the keep set quota-first; only overflow beyond quotas is sheddable,
and the shed is the overall-lowest-priority card.

| bucket | notes |
|---|---|
| **Mana** | Sub-divides by role: LANDS (land drops — one beyond colour coverage, two early) vs DORKS/rocks (acceleration, kept while board sources are below the deck's curve top). "You will probably keep some of each, so you have acceleration and land drops" (TH's broken-up mana bucket is the precedent). Colour coverage comes first: the minimal set that, with the board, covers the deck's colours. |
| **Threats** | The CATCH-ALL: "extra spells should always be threats." Ordered by a deck-specific value rank the user reviews, with STATE PROMOTIONS where the deck warrants them (FiveColour: an online free-cast engine lifts its biggest payoffs; a lethal-next finisher jumps to the front). |
| **Enablers** | Kept AFTER a threat floor ("deprioritize Mana Cannons to ensure there are at least 2 threats; if there is space after, it can be included"). |
| **Combo pieces** | A combo-shaped deck gets ONE bucket PER PIECE (often 1-item), plus a dedicated dig/cantrip bucket (the Hinata shape). |

Structural rules, all USER-ruled 2026-08-21:

1. **Quota-first.** Filling every bucket's quota comes before any shed; only overflow is
   sheddable.
2. **Net of board.** Every permanent-based quota (mana sources, permanent combo pieces)
   counts battlefield copies first; the hand owes only the remainder ("we also count the
   number of such cards on board to determine whether we need to keep them").
3. **Sub-quotas are fungible upward.** The parent bucket's total binds — a hand with no
   dorks keeps more lands; sub-roles only express preference within the parent.
4. **Distance-to-playable orders the shed inside an over-full bucket** — colour/mana
   coverage across board+hand vs the card's cost, not raw size. A payoff that is nowhere
   near castable sheds before a castable one ("dropping a Progenitus may still make
   sense... if it is nowhere near playable"), and a promotion that erases the cost (a
   free-cast engine online) erases the distance too.
5. **Return ONE index.** The rule IS the decision (the heuristic-returns-one-option
   standing rule; a single-entry return also skips the executor's trial-rollout fan —
   `AIEngine::ChooseDiscard`). Route your order through `CleanupDiscardRankingWithOrder`
   so the staged-card and required-piece protections stay engine-enforced.

Reference implementations, simplest to fullest: `HinataProvider` (static rank order + dig
bucket), `TreasureHuntProvider` (keep-set rule, single-index, broken-up mana bucket),
`AntiLifegainProvider` / `MirrorwingProvider` (state-dependent buckets, board netted first),
`FiveColourProvider` (quota-first build + distance-to-playable + state promotions + threat
floor — the fullest expression of this doctrine, 2026-08-21).

#### The loop: evidence → author → report → validate → gate

`scripts/analyze_deck.py <deck> --discard-analysis` (also stage 8 of the full flow) runs the
EVIDENCE pass: the retired discard probe re-enabled offline as a label generator
(`MTG_DISCARD_NODE=0 MTG_DISCARD_TRACE=1` — every real cleanup shed emits a searched trial
table), scored against simple visible-info rules, plus a paired-seed A/B of a derived flat
order via the testing-only `MTG_DISCARD_ORDER` lever. Read it for: how often the deck
actually sheds, how often the sheds are ties (the stakes), and which picks the searched
labels refute. A `STATUS_QUO_OK` verdict does NOT end the job — it grades flat orders, not
buckets; author the bucket policy anyway and hold it to non-inferiority.

Then: author the bucket policy from the decklist (read the deck's cards from
`src/cards/data/cards.json` first — never from memory), REPORT it to the user, and on
approval implement it as the provider's `CleanupDiscardCandidates` override behind a
default-on `MTG_<DECK>_BUCKET_DISCARD` flag (=0 restores the generic ranking). Validate:

* **Rule-vs-searched labels:** re-run the evidence config with a testing-only FAN lever
  that returns the full shed order (a single-entry return skips the trace —
  FiveColour's `MTG_5C_DISCARD_FAN=1` is the pattern), and demand zero regret; any
  rule-worse decision must classify as churn/clairvoyance, never a visible-info miss.
* **Suite gates:** smoke + regression through the accept flow (slower searched games
  classified — `test/classify_turn_later.sh`; draws-diverge = variance), overnight tier
  to finish. The bar is NON-INFERIORITY: sheds are rare at shipped play on most decks and
  the searched executor already catches gross misses, so the value is doctrine quality
  plus rollout/gen fidelity (the rollout always sheds heuristically).

A deck whose provider owns its ranking reports `DISCARD_INERT`; `NO_RULE_CONSIDER_SEARCH`
flags a candidate for another authoring attempt, then a small labelled model, then the
searched-width escalation. History and rulings: `docs/design/per-deck-discard-analysis-phase.md`.

---

## Stage 6 — Report to User

Present a concise summary:
1. **Cards implemented this run**: list any new/updated implementations, noting the tier used for each
2. **Mulligan profile**: the optimised settings (and notable card scores / required-piece flags)
3. **Win rate / average win turn**: from a regression-suite run on the new profile (the analyzer no longer reports these)
4. **Verification (Stage 5)**: which checks ran and their outcomes — nonconv/fd-diverge clean (or what was found and fixed), the multi-depth sanity result, any budget-starvation threshold noted, the (~15-20 game) claude-play sweep result (games played, flags raised and their resolution, win-turn comparison vs the search), and the **play-viewer decision surface (5h)**: any new decision types wired this run for the deck's cards, and confirmation that no card choice is left silently auto-resolved (or the disclosed known gaps that are)
5. **Encoded heuristics & assumptions disclosure** (mandatory — see Stage 6a): the full reviewable list of every assumption and heuristic that shapes this deck's results, so the user can catch anything unexpected and decide whether it should be full-searched instead.
6. **Accepted deferrals**: bracket-noted items the user agreed to skip (Tier 4), with the bracket text shown
7. **Suggested next steps**: any Tier 4 deferrals worth revisiting, or interesting profile observations

Ask the user if they want to explore any aspect further (e.g. comparing card choices, investigating a specific mechanic, or revisiting a deferred implementation).

### Stage 6a — Encoded heuristics & assumptions disclosure (mandatory)

**Default stance: full search.** The simulator's value is that it explores the play space; a
heuristic that *decides* something the search could otherwise discover is a deviation from that
ideal. Heuristics are justified **only** when they exist to curb exponential branching (or a
proven perf hot path), never as a shortcut for "what the deck probably wants to do." The user
has explicitly asked to lean toward fully searching the space, so **every heuristic that narrows
or pre-decides a choice must be surfaced for review** — some past assumptions were unexpected, and
the user must be able to veto any that isn't earning its keep.

Compile the disclosure from four sources (do NOT write it from memory — read the code):

1. **Global engine assumptions in force** (restate the ones that materially shape this deck's
   numbers): single passive opponent that never blocks / casts / gains or prevents life;
   clairvoyant search over a known library (deterministic shuffle); no post-combat second main
   unless `DeckUsesSecondMain` fired for this deck (say which flag, or "first-main only"); search
   depth / budget the result was produced at; deterministic dig / lookahead-bottoming if active.

2. **Card-modeling simplifications** — every bracket note in this deck's `cards.json` entries, plus
   any "provably inert for goldfishing" collapse you relied on (flying/first-strike/"target
   opponent" vs "each opponent"). Show the bracket text verbatim and why it's inert (or that it
   isn't, and is a deferral).

3. **Deck / archetype DecisionProvider heuristics** — the authoritative list. Read the deck's
   provider in [src/ai/DecisionProviders.cpp](src/ai/DecisionProviders.cpp) (and which one
   `SelectDecisionProvider` routes this deck to), and list **every hook it overrides** away from
   the generic baseline. For each, give: the hook name, a plain-language description of what it
   decides, and a classification —
   - **Pruning** (narrows the search to curb branching/perf): state *what lines it could miss*.
     These are the candidates for "full-search instead" the user most wants to see.
   - **Correctness shortcut** (bakes a specific decision the search could otherwise make, e.g. a
     combo-safety gate): state the decision and the rule/observation that justifies it.

   A deck routed to `GenericProvider` overrides nothing — say so explicitly (no deck-specific
   heuristics; pure search within the global assumptions above).

4. **Play-viewer auto-resolved decisions (from 2c-ter / 5h)** — any interactive choice a card in
   this deck creates that the human-play path does NOT surface and instead resolves with a
   heuristic (the catalog's known-gap rows — modal "choose one," which-permanents targeting,
   cascade/retrace target — or any bucket-B choice you deliberately left unwired). For each, name
   the card and choice, the heuristic that resolves it, and why it is provably inert for
   goldfishing (or that it isn't, and is a viewer-wiring follow-up the user should schedule). A deck
   whose every card choice is surfaced states that explicitly ("viewer-ready; no auto-resolved
   decisions").

Present this as a short table (assumption/heuristic | source | classification | what it costs us /
why it's safe). Flag anything you'd expect the user to find surprising. If the user decides a
pruning heuristic should be full-searched, that becomes a follow-up engine task — do not silently
keep it.

---

## Decision Rule: When to Stop for User Input

**Proceed without asking** when:
- The implementation choice is unambiguous given the oracle text
- A known simplification is already established in this codebase (e.g., Predatory Sliver modelled as flat lord)
- The gap is clearly Tier 1, 2, or 3

**Surface to the user — and keep working, never blocking** (see the no-blocking rule at the top of this skill) when:
- Two structurally different implementations are plausible and the choice affects simulation accuracy in a non-trivial way
- The mechanic is Tier 4 (out of scope)
- A new engine feature would require redesigning an existing interface in a way that affects multiple cards

In each case: name the question in your message, pick the option you would have recommended, mark it PROVISIONAL in the ledger, finish the workflow, and re-raise the whole set in the closing message. The only genuine stops are a **destructive/irreversible action** or a fork where **either choice would waste a run already under way**.

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
