# Viewer: bounce-land after casts + X-spell line validation (issue #7)

Status: **DEFERRED — engine (`CheckLine`) + front-end (`linebuild.js`); reproduction needed. User has a
working two-phase workaround, so not blocking.**

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

## Fix (deferred, staged)

1. **Reproduce** `--seed 3 --game-index 2` (Boilerworks + Crackle) to capture the offending plan and
   confirm the enumerator over-credits the tapped Boilerworks (item 3).
2. **Fix the mana accounting** so a land that `enters_tapped` contributes **0** available mana the turn
   it enters (its `{U}{R}` only counts next turn). This is the core correctness fix — verify the
   enumerator and executor agree (lockstep) that the combined plan is only affordable when the 5 other
   mana cover Crackle independent of Boilerworks.
3. **Let the viewer express cast-then-bounce-land in one commit:** support X-spells in `CheckLine`
   (with an X value) so hand-built Crackle lines validate, and allow `encodeLine` to sequence a
   bounce/Karoo land after casts (reuse `s_karoo_defer` in the line-check path).
4. Engine mana-accounting changes may move GT for Karoo/enters-tapped decks → rebaseline per
   `.claude/skills/regression-testing.md`.

## Verification

In the repro, hand-build `cast Crackle (X=1); land=Izzet Boilerworks` in one commit → Crackle resolves
(to graveyard), Boilerworks enters and bounces a land, no misfire. Confirm two-phase play still works.
Read `.claude/skills/mtg-rules.md` (mana abilities, lands entering tapped) before implementing.
