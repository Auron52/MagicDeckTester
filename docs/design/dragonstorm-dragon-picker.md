# Dragonstorm Dragon-put override dialog (deferred)

**Status:** deferred feature. The *selection rule* is fully implemented and correct; what's
missing is a **viewer multi-pick UI** so a human can OVERRIDE which Dragons enter, defaulting
to the AI's rule pick. Requested by the user during the 2026-07-21 Dragonstorm viewer bug-bash.

## What already works (do NOT rebuild)

`DragonstormProvider::TutorToBattlefieldPutOrder` (`src/ai/DecisionProviders.cpp`) picks WHICH
Dragons Dragonstorm puts and in WHAT order, and it now fires in human play too (the
`!HumanPlayActive()` carve-out at the `DecisionUnpruned(Tutor)` guard — MTG_UNPRUNED, which the
viewer sets, used to disable it → raw library order). Verified: the viewer resolves Lathliss +
reserved-haste-Dragon identically to the search.

**Roles** (by params): Lathliss=`etb_other_subtype_creates_tokens` (token engine); Scourge
×3=`dragon_ping_on_enter` (pinger, X=Dragons); Utvara ×2=`attack_per_matching_creates_tokens`;
Karrthus/Kolaghan=`grants_haste` (Karrthus preferred).

**Selection priority (what you GET):**
- `max_puts <= 2` (small N — lethal usually unreachable, ping weak): haste → Lathliss → Utvara →
  Scourge → 2nd haste.
- `>= 3`: Case A (Lathliss+haste) reserve haste, Lathliss, 1 Scourge, then max Scourges, Utvara;
  Case B (haste, no Lathliss); Case C (no haste — max Scourges for the ping).

**Play ORDER (independent of selection):** Lathliss → all Scourges → Utvara → Karrthus →
Kolaghan. Lathliss & Scourge stay FRONT (order-dependent: Lathliss tokens + Scourge pings fire
off every LATER Dragon); haste-Dragon last.

## What to build — the override dialog (bucket-B decision type)

A modal multi-pick, **pre-selected to `TutorToBattlefieldPutOrder`'s pick**, letting the human
toggle which library Dragons enter (up to `max_puts`); the engine applies the rule's ORDER to
whatever subset is chosen (user only picks WHICH). Model on the **`dig`** decision — the 4 sites:

1. **Chooser hook** — `src/core/GameLogger.h`: `using DragonChooser = std::function<…>` +
   `extern thread_local DragonChooser* g_play_dragon_chooser;`. **Must be nulled in
   `RevealLogPause`** (so it's inert in search/rollout, fires only on real resolution).
2. **Call site** — `PerformTutorToBattlefield` in `src/core/SpellEffects.h` (the shared executor,
   NOT AIEngine's autonomous path — that's the recurring dead-chooser bug). When the pointer is
   non-null, offer the library Dragons with the rule pick as default; use the returned subset.
3. **Protocol emitter** — `src/main.cpp`: `WriteDragonDecisionJson` between
   `<<<CLAUDE_DECISION>>>`/`<<<END_DECISION>>>` (exit 70), `"type":"dragon"`, the candidate
   library Dragons, `max_puts`, and a `heuristic_default` (the rule's selection). Reply rides the
   integer `--choices` stream — a subset needs a stable encoding (e.g. a bitmask over the sorted
   candidate list, or one index per put).
4. **GUI** — `tools/play/index.html`: a `dragonPanelHtml` modal (art grid, pre-checked to the
   default, cap at `max_puts`), dispatched on `S.decision.type==='dragon'`; add `'dragon'` to the
   `SUBDECISIONS` list and `isModal`. Use card IMAGES (play-viewer principle).

Register the new row in `tools/play/DECISIONS.md` and the auditor manifest
(`scripts/audit_viewer_decisions.py`).

## Gotchas carried from this session

- `--claude-play --seed S --game-index G` seeds DIFFERENTLY from goldfish `--games` internal
  indexing. To reproduce a viewer game use `--seed S --game-index G` directly; don't map to a
  goldfish game number.
- The viewer runs `MTG_UNPRUNED` + `MTG_HUMAN_PLAY`; any provider heuristic gated on
  `DecisionUnpruned(...)` will turn OFF in the viewer unless carved out with `!HumanPlayActive()`.
- Separate open item: **Dragonstorm d5 (avg ~8.34) plays WORSE than d3 (~6.1)** — the default d5
  play policy underperforms on this combo deck (no calibrated `value_play`). Not a picker issue.
- Optional cheap add (#4 usability): the reject menu could show "short by N mana" so a 1-short
  Apex line explains itself (the engine's affordability is correct; the line was genuinely short).
