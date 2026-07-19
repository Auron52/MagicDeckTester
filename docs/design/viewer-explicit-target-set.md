# Viewer: explicit wide target sets + Soulfire own-target selection (issue #8)

Status: **DONE — per-target reply shipped; Soulfire wide selection + dig verified.**

**Shipped:** the Soulfire target chooser (`g_play_soulfire_chooser`, human-play only) now reads a
PER-TARGET reply — one int per legal target (1 = targeted), like `divide` — instead of a single index
into the ≤256 enumerated subsets. The viewer's `commitBoardSel` submits that 0/1 list for
`random_damage` decisions. Uncapped, so the human can target ALL opponent creatures. Verified on
Seed 4 Game 3: submitted 11/13 targets (face + 3 creatures + 7 spirits) → dig staged 11 playable
cards (previously the 256-cap's largest option was 8, and a wider pick silently fell back to 4 →
"targets ignored, did not give cards"). Reference-safe (no reference casts Soulfire) and GT-neutral
(chooser nulled for the search) — smoke byte-identical.

Remaining nicety (optional): the heuristic DEFAULT still seeds the min floor, so "Follow AI" starts
narrow; a better default (seed all beneficial opponent creatures) would let one click take the wide
line. Low priority now that the human can select freely.

---

### Original analysis (retained)

## Problem (as reported)

Casting **Soulfire Eruption** in Hinata: the default targets did not include the user's 8 Spirit
tokens + 2 creatures; the user selected them manually, but "my targets were ignored and did not give
cards," forcing them to give up the game.

## What is already fixed (front-end, shipped)

`commitBoardSel` in [tools/play/index.html](../../tools/play/index.html) previously matched the user's
board selection to a pre-enumerated `target` option **by exact set match, else silently submitted
`heuristic_default`** — discarding the user's picks with no notice. That silent fallback is removed: a
non-enumerated set now surfaces a `flashHint` and aborts the commit instead of eating the selection.
This stops the "targets ignored, no cards" surprise but does **not** yet let the user execute an
arbitrary wide selection.

## Root cause of the remaining gap

Two engine-side facts, both confirmed:

1. **The `target` protocol is option-INDEX based, capped at 256 subsets.** The engine pre-enumerates
   target subsets in `EnumerateTargetSets` ([src/main.cpp:764](../../src/main.cpp#L764):
   `mask < (1<<n) && opts.size() < 256`). With ~12 legal targets (8 tokens + 2 creatures + 2 faces),
   the user's large subset is a high-mask combination beyond the first 256, so it is never offered.
2. **Soulfire's own-target COUNT is a *plan* attribute, not a target decision.** The engine models
   Soulfire's own-creature targeting as `soulfire_own_targets` (0..#own creatures, "expendable first,
   Hinata last") baked into the main-phase plan and emitted as a plan-action field
   ([src/main.cpp:444](../../src/main.cpp#L444)), not as a `target`/`soulfire_targets` decision. The
   viewer's `soulfire_targets` decision branch is **dead** (never emitted). So the way to get more
   own-creature digs is to pick a *plan* with a higher `soulfire_own_targets` count — which the viewer
   does not surface as a first-class choice.

Each own target exiles+**stages** the top library card as playable (`SoulfireDig` in
`src/core/SpellEffects.h`, lockstep across EffectHandler/ApplyPlanDirect/Solve). With the manual
selection discarded (old bug) the dig count collapsed to the default → "did not give cards."

## Fix options (deferred)

- **A. Surface `soulfire_own_targets` as a plan choice.** In the main-phase plan menu, when plans
  differ only by `soulfire_own_targets`, present them clearly ("target N of your creatures: deeper dig,
  −N generic via Hinata") so the user can pick the count. Lowest-risk: no protocol change; the
  enumeration already produces these plans. This directly matches how the engine models the effect.
- **B. Explicit-target-set reply for wide `target` decisions.** Extend the `target` protocol so a
  `random_damage`/dig spell can accept an explicit set of target keys (not just a pre-enumerated
  index), removing the 256-cap for these spells. More general but a protocol + chooser change; also
  requires the engine to accept and validate an arbitrary own-creature set for `SoulfireDig`.
- **C. Remove the dead `soulfire_targets` viewer code** ([index.html](../../tools/play/index.html)
  `wireDecisionBoard`/`promptPanelHtml`/`commitBoardSel` branches) once A or B lands, to avoid
  confusion. (Left in place for now — harmless, but not wired to any emitted decision.)

**Recommendation:** A first (matches the model, no protocol churn, likely resolves the user's need),
then evaluate B if arbitrary damage-target subsets (e.g. for the face/creature split) are still needed.
Read `.claude/skills/mtg-rules.md` (targeting) + `.claude/skills/claude-play.md` and the Soulfire
`cards.json` entry before implementing.

## Verification

Reproduce Hinata Seed 4 Game 3 (`--seed 4 --game-index 3`), reach the Soulfire cast, and confirm the
user can choose a plan/target set that targets their tokens/creatures and that each such target stages
a playable card (`is_staged` in the next decision's hand).
