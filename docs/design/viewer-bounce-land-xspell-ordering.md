# Viewer: bounce-land after casts + X-spell line validation (issue #7)

Status: **REPRODUCED — item 3 (enumerator over-credit) DISPROVEN; the residual issue is a VIEWER
convenience (hand-building an X-spell + Karoo-land line), GT-neutral, with a working two-phase /
plan-menu workaround. Not blocking; no rebaseline needed.**

## Reproduction result (2026-07-19) — item 3 is NOT a bug

Replayed Hinata `--seed 3 --game-index 2` to turn 6 (the Boilerworks + Crackle turn) and **picked the
combined plan** the enumerator offers (`land=Izzet Boilerworks; cast Crackle with Power, Irencrag Feat`,
plan idx 2 at that decision). It **executes correctly**: Boilerworks enters the battlefield, all the
Crackles + Irencrag Feat resolve to the graveyard, and the opponent drops to 5 life. So the engine's
`s_karoo_defer` correctly defers Boilerworks *after* the casts (casts paid from the 5 real untapped
sources + Irencrag's ritual; the tapped Boilerworks contributes 0 that turn), and **the enumerator does
NOT over-credit the tapped Boilerworks** — the combined *plan* path is sound. At turn 5 (only 4 sources,
no ritual) the enumerator correctly offers **no** combined plan (unaffordable). Item 3 is closed.

**So the reported misfire ("Boilerworks stays in hand, Crackle not cast") is the HAND-BUILD path, not
the engine.** When the user assembles the line in the LineBuild UI: (1) `encodeLine`
([tools/play/linebuild.js:55](../../tools/play/linebuild.js#L55)) emits `land=` before every `cast=`, so
the Karoo land is sequenced *first*; and (2) Crackle is an X-spell, which `CheckLine` grades
**`unsupported`** — so the whole hand-built line can't be validated/committed and silently no-ops. Both
are viewer-only (GT-neutral). The user's workaround (pick the combined plan from the menu, or split
across phases) already works because it bypasses the hand-build path.

## Problem (as reported)

Hinata Seed 3 Game 2: casting **Crackle with Power** (`{X}{X}{X}{R}{R}`) with 5 mana on board **and**
playing **Izzet Boilerworks** (Karoo bounce land) to bounce a land in one commit "misfired — Boilerworks
stays in hand, Crackle not cast." Splitting the cast (pre-combat) and the land drop (post-combat) across
phases works — "but I shouldn't have to."

Note: this is a **genuine** cast drop (Crackle → graveyard when cast), so the WS1 battlefield-aware
drop-detector fix does **not** mask it; the viewer correctly reports it.

## Root cause — three converging limits

1. **Fixed land-first line encoding.** `LineBuild.encodeLine`
   ([tools/play/linebuild.js:55-62](../../tools/play/linebuild.js#L55)) always emits `land=` **before**
   every `cast=`, so a hand-built line cannot express "cast Crackle, then play the bounce land."
2. **X-spell lines are `unsupported` by `--validate-line`.** `TurnSolver::CheckLine` reports
   `unsupported` for X / alt-cost / tutor casts (README `tools/play/README.md`), so a hand-built Crackle
   line can't be validated at all — the only route to cast Crackle + play Boilerworks together is a
   pre-enumerated **plan index**.
3. **Enumerator↔executor mana mismatch on the combined plan.** The engine *does* correctly defer a
   Karoo/bounce land after casts (`s_karoo_defer`,
   [src/ai/TurnSolver.cpp:3065](../../src/ai/TurnSolver.cpp#L3065)/`:4368`,
   [src/ai/AIEngine.cpp:1630](../../src/ai/AIEngine.cpp#L1630)). Izzet Boilerworks **enters tapped**
   (`enters_tapped:true`), so its `{U}{R}` is *not* available the turn it's played — but the chosen
   combined plan's enumerated affordability appears to over-credit it, so at execution Crackle can't be
   paid and is dropped.

## Fix (revised — GT-neutral viewer work only; NO engine mana-accounting change)

1. ~~Reproduce + confirm the enumerator over-credits the tapped Boilerworks.~~ **DONE — DISPROVEN.**
   The combined plan executes correctly; the mana accounting is already lockstep-correct (`s_karoo_defer`).
2. ~~Fix the enters_tapped mana accounting.~~ **NOT NEEDED** — no over-credit exists, so no engine change
   and **no GT rebaseline**.
3. **Let the viewer hand-build a cast-then-bounce-land line** (the only remaining, GT-neutral work):
   - `encodeLine` should sequence a Karoo/bounce land AFTER the casts when the land `enters_tapped` /
     is a bounce land (so the encoded order matches how the engine defers it), instead of always
     land-first.
   - `CheckLine` grades X-spells `unsupported`; a hand-built Crackle line therefore can't validate. Two
     options: (a) extend `CheckLine` to validate an X-spell line at a chosen X (larger, viewer-only), or
     (b) cheaper interim — when the assembled line contains an X-spell (or otherwise `unsupported`),
     surface a clear "use the plan menu for this line" hint instead of a silent misfire, since the
     combined *plan* already works.
   Both are viewer-only (`CheckLine`'s sole caller is `--validate-line`; `encodeLine` is the GUI) →
   **GT-neutral, no rebaseline.**

Recommendation: ship 3(b) (the clear-hint) as the low-risk unblock, and treat 3(a) (full X-spell line
validation) as a separate, larger viewer feature. The user's headline pain (a *silent* misfire) is
resolved by 3(b) + the fact that the plan menu already does the combined cast correctly.

## Verification

In the repro, hand-build `cast Crackle (X=1); land=Izzet Boilerworks` in one commit → Crackle resolves
(to graveyard), Boilerworks enters and bounces a land, no misfire. Confirm two-phase play still works.
Read `.claude/skills/mtg-rules.md` (mana abilities, lands entering tapped) before implementing.
