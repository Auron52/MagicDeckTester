# Analysis ledger — Pirates

Per-deck ledger for the `analyze-deck` workflow (see `.claude/skills/analyze-deck.md`, "The per-deck
ledger"). This file is the durable state a resumed session or a second machine reads to continue;
context is disposable, this is not.

**Deck:** `decks/Pirates/Pirates.cod` — U/R/b Aether Vial Pirates tribal, 60 cards, no sideboard.
**Started:** 2026-09-26. **Branch:** `phase-1-2-deck-analyzer`.

## The list

| n | card | status at Stage 1 |
|---|---|---|
| 4 | Daring Buccaneer | missing |
| 4 | Corsair Captain | missing |
| 2 | Kitesail Larcenist | missing |
| 1 | Malcolm, the Eyes | missing |
| 4 | Staunch Crewmate | missing |
| 4 | Adaptive Automaton | missing |
| 4 | Goblin Tomb Raider | missing |
| 4 | Metallic Mimic | missing |
| 4 | Dire Fleet Captain | missing |
| 2 | Forerunner of the Coalition | missing |
| 2 | Siren Stormtamer | missing |
| 4 | Spirebluff Canal | missing |
| 1 | Blackcleave Cliffs | missing |
| 4 | Aether Vial | implemented |
| 5 | Mountain | implemented |
| 4 | Secluded Courtyard | implemented |
| 4 | Unclaimed Territory | implemented |
| 2 | Fiery Islet | implemented |
| 1 | Lightning Bolt | implemented |

Every "choose a creature type" in the deck (Metallic Mimic, Adaptive Automaton, Secluded Courtyard,
Unclaimed Territory) is **Pirate**.

## Stage 1 — coverage (2026-09-26)

13 missing, 6 `full`. Bracket notes on the implemented cards:

| card | bracket note | classification |
|---|---|---|
| Aether Vial | AI heuristic for charge/activation | not a simplification — the shared root Vial policy (`GenericProvider::WantVialCharge`); disclosed in 6a |
| Secluded Courtyard | type choice simplified to "any creature" | exact here: every coloured-cost creature is a Pirate; the colourless Mimic/Automaton need only generic |
| Unclaimed Territory | "ETB choice and color restriction not modelled" | **note is stale/wrong**: `colored_creature_only` IS set. Exact for this deck (see Stage 2) — note reworded, behaviour unchanged |

## Stage 2 — implementation

Per-card research fanned out to 11 Opus agents (one per card / pair) on 2026-09-26; drafts kept under
`logs/pirates/drafts/` (gitignored scratch). Integration is serial in a scratch worktree off the origin
tip (the main tree held another session's uncommitted discard-gate WIP).

| card | tier | mechanism (new params in **bold**) |
|---|---|---|
| Spirebluff Canal, Blackcleave Cliffs | 1 | existing fastland (`fastland_max_other_lands`) |
| Siren Stormtamer | 1 | vanilla 1/1 flier; activation unmodelled (PROVISIONAL, see below) |
| Staunch Crewmate | 2 | existing `etb_dig_*`; dig filter now matched by `CardMatchesTypeName` so `"Artifact"` works |
| Dire Fleet Captain | 2 | Piledriver attack pump + **`attack_pump_tough_per_other_matching`** |
| Corsair Captain | 2 | `lord_effect` + **`etb_creates_treasures`** |
| Goblin Tomb Raider | 2 | **`static_artifact_threshold/power/tough/haste`** conditional static + conditional haste |
| Forerunner of the Coalition | 2 | Giant Harbinger tutor-to-top + **`own_creature_enters_opp_life_loss`** |
| Malcolm, the Eyes | 2 | **`nth_spell_trigger_n` / `nth_spell_investigate`** (Clue) |
| Metallic Mimic, Adaptive Automaton | 3 | **`chosen_type_added_to_self`**, **`other_chosen_subtype_enters_counters`**, **`lord_affects_chosen_subtype`**: the chosen type (DominantCreatureSubtypeId = Pirate) is appended to the BATTLEFIELD permanent's subtypes only |
| Daring Buccaneer | 2 | **`reveal_or_pay_subtype/cost`** in EffectiveSpellCost + an order-aware per-subset surcharge in the enumerator |
| Kitesail Larcenist | 3 | **`etb_treasurify_each_player`**: in-place rewrite to a Treasure, searched own-side target, new `treasurify` viewer decision |

### Pre-existing bugs found in OTHER decks during research (verified 2026-09-26)

1. **Keyword mask overflow** — `Keyword` has 35 values, `Card::m_keyword_mask` was `uint32_t` with
   `1u << k`: Persist/Evoke/Convoke were UB and on x86 aliased Haste/Flying/Trample (probe: adding
   Persist stores mask `0x1`). **Kitchen Finks and Murderous Redcap (Melira Pod) had phantom haste.**
   Pirates needs `Ward`, so the fix is on the critical path. Own commit; Melira Pod GT re-accepted.
2. **Felidar Guardian decline divergence** — `AIEngine` only stores `chosen_x` on the stack entry when
   `> 0`, so a searched DECLINE (0) resolves in the executor as `-1` -> provider `FlickerTarget`, while
   the rollout declines. Executor/rollout divergence (Melira Pod).
3. **Oko's Elk keeps its old definition** — `elk_transform` renames to "Elk" but never clears the cached
   `Card::m_def`, so `LookupCached` still returns e.g. Birds of Paradise's definition (FiveColour).

## Approved deferrals

(none yet — every deferral below is PROVISIONAL until the user signs it off)

PROVISIONAL:
- **Siren Stormtamer** — "{U}, Sacrifice: counter target spell or ability that targets you or a creature
  you control" unmodelled: never legally activatable (the passive opponent never targets), and nothing
  in the 60 has a death payoff.
- **Kitesail Larcenist** — the "for as long as it remains" revert (it cannot leave the battlefield in
  this list); opponent-side target auto-declined in autonomous play (spawns never block, nothing reads
  them; human can still pick); rename to "Treasure Token" (rules keep the name; inert here); flying/ward inert.
- **Corsair Captain** — the global `MTG_PAYSAC_FRESH_HOLD` doctrine banks a Treasure made this turn;
  real Magic can spend it at once (e.g. T3 Captain -> crack for a 1-drop). A NARROWING — under review.
- **Chosen creature type** (Mimic / Automaton) not surfaced in the viewer: deck-constant Pirate weakly
  dominates every alternative (every other creature is a Pirate).
- **Daring Buccaneer** reveal-vs-pay auto-resolves to reveal (paying {2} when a reveal is possible is
  strictly dominated).

## Open questions for the user

(collected here; never blocking — defaults taken are stated)

1. Sign off the PROVISIONAL deferrals above.
2. Cast order (user-reviewed per deck): Malcolm vs Metallic Mimic first (Clue vs +1/+1 counter);
   Daring Buccaneer ahead of other Pirate casts (keeps a reveal available); Forerunner ahead of other
   Pirates (catch their drains). Default: search-visible provider ranks as listed, pending review.
3. OK to classify the new params in `audit_viewer_decisions.py` INERT_PARAMS (script self-guard)?
4. Corsair's same-turn Treasure: lift the fresh-hold for non-trick Treasures (A/B-measured)? Default: measure.

## RESUME STATE (2026-09-26, pre-compaction)

* **Worktree:** `/tmp/pirates-wt`, branch `pirates-analysis` (based on origin tip `c29b9a61`). The main
  tree `/workspaces/MagicDeckTester` holds ANOTHER session's uncommitted discard-gate WIP (cards.json,
  DecisionProviders.*, verify_deck.py, CLAUDE.md, discard_policy_audit.py, 15 proposal docs) — never
  commit or revert it from here. This ledger and `decks/Pirates/Pirates.cod` are still UNCOMMITTED in
  the worktree. Drafts: `/workspaces/MagicDeckTester/logs/pirates/drafts/*.md`.
* **Committed locally (not pushed):** `52bb7af9` keyword mask -> 64 bit (+Ward); `23369f3b` chunk 1
  (11 cards + chosen-type infra; 179 unit tests pass; coverage: only Buccaneer + Larcenist missing).
* **In flight:** integrator chunk 2 (background agent) — commits, in order: Felidar decline fix,
  Oko Elk m_def fix (+ Card rename helper), Daring Buccaneer (EffectiveSpellCost + per-subset
  surcharge + SubsetPayableSequential bookkeeping), Kitesail Larcenist (+ `treasurify` viewer decision).
* **Next — chunk 3:** `PiratesProvider : DeckProvider` (NOT VialProvider — Vial policy is root default;
  VialProvider only adds Knights-gated cast order); route it ABOVE `anti` in DetectDecisionProvider
  (Forerunner's tutor_to_top trips `anti`) with a signature OR-ed from several Pirates-only params;
  `Certificate()` answer; `TutorSearchWidth` >= 9 (9 distinct Pirate names; default 6);
  bucketed discard policy per `/workspaces/MagicDeckTester/docs/design/discard-bucket-authoring-brief.md`
  behind `MTG_PIRATES_BUCKET_DISCARD` + `docs/design/pirates-discard-policy-proposal.md` (USER review);
  cast-order proposals (Malcolm/Mimic, Buccaneer first, Forerunner first) are USER-reviewed — propose only.
  Then `python3 scripts/provider_audit.py --check`.
* **BOX HOLD:** load avg ~24/24 from a sibling container (likely the Fungus generation; nothing
  visible in this container). One-batch-at-a-time rule => NO smoke/regression/sweeps until the box is
  quiet (a background watcher loop was checking `/proc/loadavg` < 8 for 10 min). Builds + unit tests OK.
* **Then:** smoke tier (byte-identity except Melira Pod [keyword, Felidar] / FiveColour [Elk] — each
  changed game needs a verdict; accept per regression-testing skill), Stage 4 profile
  (`analyze_deck.py --no-rebuild`), Stage 5 (`verify_deck.py`, nonconv/fd-diverge, d0/3/5 sweep,
  leaf_tiebreak_check, claude-play sweep ~15-20 Opus/Sonnet agents, viewer audit), fresh-hold A/B
  (`MTG_PAYSAC_FRESH_HOLD=0`) to bound Corsair's same-turn Treasure narrowing, add Pirates to the
  regression suite, push via this worktree (`git pull --rebase` first; rebuild + smoke after rebase),
  watch CI (Windows).
