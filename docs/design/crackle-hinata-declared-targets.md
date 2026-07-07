# Crackle with Power — declared targets, derived Hinata discount, faithful damage

Status: **in progress**. Soulfire dialog fix landed (see bottom); Crackle engine work staged below.

## Problem

**Crackle with Power** (`{X}{X}{X}{R}{R}`, deals 5X to each of *up to X targets*) is a Hinata
combo finisher: with **Hinata, Dawn-Crowned** in play each declared target shaves {1} off the
cost, so declaring more targets buys a bigger X. Today the model is wrong in three ways:

1. **The discount is auto-max at cast time, decoupled from declared targets.**
   `HinataGenericDiscount = min(X, HinataAvailableTargets)` where
   `HinataAvailableTargets = 1 (opp face) + every creature on the battlefield`
   ([SpellEffects.h:1704](../../src/core/SpellEffects.h)). The search gets the reduction
   *for free*: in the rollout `g_play_target_chooser` is nulled (clairvoyance guard) so
   `ApplyPlanDirect` resolves Crackle **face-only — no creatures die**
   ([TurnSolver.cpp:2555-2616](../../src/ai/TurnSolver.cpp)), yet the discount still applies.
   Worse, the auto-count includes **Hinata herself** — a discount for a target you could never
   legally realize (targeting her kills her and removes the discount for the rest).

2. **The viewer can't declare the targets.** `g_play_target_chooser` *does* fire for Crackle
   and `CollectDamageTargets` lists creatures + both faces ([main.cpp:615](../../src/main.cpp)),
   but the choice is at resolution and doesn't feed back into the (already-paid) discount, so it
   can't *derive* the discount from the declaration.

3. **Self is never a discount target.** `discount_self_safe=false`, so "You (face)" is offered
   as a damage target but never counts toward the discount — even when it would be safe.

## Model (the fix)

**Declare targets → derive discount → faithful damage**, mirroring Soulfire's searched
own-target count (`soulfire_own_targets`; enumerate [TurnSolver.cpp:934-968](../../src/ai/TurnSolver.cpp),
resolve `SoulfireDig` [SpellEffects.h:1460](../../src/core/SpellEffects.h)).

- **Declared count** `crackle_targets` = # of *additional* beneficial targets beyond the opponent
  face (creatures + self), a cast-time searched/declared parameter (0..cap). Total targets =
  `1 + crackle_targets`; **discount = min(X, 1 + crackle_targets)**. Replaces the auto-max.
- **Available additional targets**, expendable-first, **Hinata last** (reuse the
  `SoulfireOwnCreatureOrder` ordering / `IsHinataPermanent`): opponent creatures, then own
  non-Hinata creatures, then **self if `5*X < your life`** (dynamic — the user's insight: a
  self-hit for the discount is fine when non-lethal), then Hinata last.
- **Faithful damage**: on resolution each declared creature/self takes `5*X`; lethal creatures
  die via SBA (`damage >= toughness`, reuse the SoulfireDig kill loop
  [SpellEffects.h:1534-1556](../../src/core/SpellEffects.h)) — so they leave the target pool for
  *subsequent* spells (the real GT-moving interaction). Applied **lockstep** in both
  `ApplyPlanDirect` (rollout) and `EffectHandler::ResolveDirectDamage` (executor).

### GT surface is tiny (bounded by the user)

Only **X=2 and X=3** can move GT: X=1 hits a single target (no extra discount target, nothing
dies); **X≥4 is lethal** (5·4 = 20 to face) so the win lands regardless of discount details.
"Larger X = won game," so the discount only matters where it lets a *non-lethal* Crackle reach a
turn earlier. Expect a small, correctness-improving shift on a few Hinata games. **Measure, don't
predict** (regression A/B on train seeds, then rebaseline).

## Sequencing: attack-then-Crackle (the Hinata-dies subtlety)

Because faithful damage now *kills* Hinata when she's a declared discount target, the optimal line
is often **attack with Hinata first, then Crackle post-combat** targeting her (she's already dealt
combat damage, then dies for the discount). Crackle pre-combat in the wrong order loses her attack
— and maybe the game. Options:

- **(C1) Second-main archetype** *(preferred — clean, no bespoke heuristic)*: make Hinata a
  "play main 2, skip main 1" deck. Slivers/Knights/Burn must cast in main 1 to grow combat damage,
  but **Hinata doesn't care about its board's combat** — so running everything post-combat lets
  Hinata attack in combat, then Crackle post-combat targeting her, for free. Reuses the existing
  `second_main`/`SetSearchPostCombat` machinery; the twist is *suppressing* main 1 for this
  archetype (today second-main decks play BOTH mains). Verify a pure-post-combat line doesn't lose
  a tempo case where casting-before-attack matters (Hinata's own combat is the only attacker that
  matters, and she attacks either way).
- **(C2) Bespoke heuristic**: sequence Hinata's attack before a Crackle that targets her. More
  targeted but adds special-case logic; C1 subsumes it.
- **(C3) Full both-mains post-combat search**: expensive; unnecessary if C1 works.

Lean **C1**. Can start without C (search casts pre-combat / avoids targeting Hinata) and add it
once A+B validate.

## ROOT CAUSE of the Stage-A regression (measured 2026-07-07) — Stage C is REQUIRED

Stage A (faithful damage in the autonomous search) regressed Hinata badly: d0 wins 518→~348,
**12 searched (d3/d5) win→loss** (gate fail). `explain_game.py smoke hinata_smoke_d3_s1001 26`
(identical kept hand + draws) shows the exact mechanism:

- **OLD T5**: `Reality Spasm ×3; Crackle; ATTACK` → opp −4 → **win**. Crackle alone did NOT kill;
  the finishing points came from **Hinata + Ornithopter attacking**.
- **NEW**: faithful Crackle is cast **pre-combat**, so it **kills the attackers (Hinata,
  Ornithopter) for the discount before combat** → the attack damage that closed the game is gone
  → the combo no longer wins → the search **abandons the whole line** (never even plays Hinata).

This is NOT a bug — the faithful model correctly shows that **Crackle-before-combat throws away the
attack**. The fix is the sequencing we already planned: **attack first, Crackle post-combat**
(Stage C). Without Stage C, faithful damage and the combat finish are mutually exclusive. WITH it,
creatures attack, then Crackle kills them for the discount (they've already dealt combat damage).

Therefore the correct order is **implement Stage C BEFORE re-measuring Stage A** — the searched
win→loss games should recover once the combo can run post-combat. The autonomous heuristic already
in place (lethal→faithful max, non-lethal→legacy `-1` no-kill) stays.

## Staging

- **A — engine core**: `crackle_targets` on Action/StackEntry; enumerate (X, count) variants for
  Hinata Crackle (bound to the small X=2/3 surface); derived discount; expendable-first/Hinata-last
  order; dynamic self; faithful damage + SBA lockstep (rollout + executor). Keep the AI's target
  *selection* heuristic (auto-pick the count/creatures); the search picks among count variants.
- **B — viewer**: the count becomes a structured `crackle` sub (nice picker, like the fixed
  Soulfire one); a which-targets step reuses `g_play_target_chooser`; surface self-when-safe.
- **C — sequencing**: the attack-then-Crackle heuristic (C1).
- **D — validate**: regression A/B (X=2/3), rebaseline smoke+regression GT (overnight deferred),
  run both viewer checks.

## STAGE B DONE (2026-07-07, commit 21555b0) — viewer count picker

The play viewer casts Crackle with a chosen target COUNT (the "can't target the opponent's
creatures" gap) AND lets the human board-click which specific permanents the extra targets
hit. Mechanism: the count range and the board-click resolution are both gated on
`HumanPlayActive()` (initially a dedicated `g_viewer_enum` thread_local, since removed) —
true across the plan MENU, `CheckLine`, and the apply re-run, so the count variants have
consistent plan indices between validate and apply, and false in the search rollout / batch
(single autonomous count, byte-identical). CheckLine emits a `crackle` sub (mirrors Soulfire);
the GUI's generic picker renders it (index.html KIND: crackle after x). At resolution,
`ApplyPlanDirect` routes the extra targets through `g_play_target_chooser` when it is set
(human play), defaulting to `CrackleExtraTargetOrder`'s opp-first ordering; the chooser is
null in the rollout so the batch falls to `CrackleHitExtraTargets` unchanged.

**DEFERRED refinements #1 + #2 are now DONE:**
1. **Full unpruned count range — DONE (167b670).** `plan_signature` now includes
   `crackle_targets`, so distinct counts survive dedup as distinct variants instead of
   collapsing to the first-enumerated.
2. **Per-target board-click — DONE, proven exactly correct.** The human clicks WHICH specific
   permanent each extra target hits (via `g_play_target_chooser` at resolution), not just a
   count. Verified by identity routing on seed 2 / game 1 (X=2, 5X=10): clicking own 4/4
   Hinata kills Hinata and spares all 8 opponent tokens (opp 20→10, no combat); clicking a
   token kills exactly that token and spares Hinata (opp 20→6). Face always takes exactly 5X;
   each extra takes exactly 5X; SBA moves the dead creature to its owner's graveyard; the
   option set is exactly `CrackleExtraTargetOrder` (opp creatures, own non-Hinata, self-when-
   safe, Hinata last). Autonomous byte-identical (608/6.78289 d0, digests match GT).

**Still deferred:** the combined main-1 `Reality Spasm + Crackle` plan over-generates at
enumeration (RS ritual mana credited but not realizable at apply → Crackle silently dropped).
A speculative artifact the human path never reaches; parked in
`docs/design/crackle-reality-spasm-overgeneration.md` alongside the autonomous RS→Crackle
combo, since both stem from Reality Spasm's floated-mana abstraction.
3. Label which targets a given count hits (incl. self-when-safe) in the picker.

## BUNDLE + FULL VALIDATION (2026-07-07) — 3 stacked Hinata changes, ready to accept

The shipped change is THREE stacked, individually-validated Hinata improvements:
1. **Faithful Crackle** (Stage A) — correctness: the auto-max free-discount cheat (incl. targeting
   Hinata herself) is gone; discount derives from declared targets, which take 5X + die (SBA).
   Made outcome-neutral autonomously by the sentinel fix below.
2. **Second main** (Stage C) — `DeckUsesSecondMain` true for `hinata_cost_reducer`, so the search
   sequences attack-then-Crackle post-combat.
3. **Hold mana dorks** (`HinataProvider::ShouldAttackWith`) — a 0-power no-trigger dork (Ornithopter
   of Paradise) never swings (0 damage, and swinging taps it out of funding the second-main Crackle).
   Off-switch `MTG_NO_HINATA_HOLD_DORK`. A/B: **d0 +11 (597→608), d3 +1 (145→146), d5 =72**, faster.

**Full validation (final bundle):**
- Smoke: searched win→loss **0**; every other deck (burn/slivers/th/knights/antilife) BYTE-IDENTICAL
  (15 pass, only hinata's 3 cases moved); d0 518→608, d3 145→146, d5 71→72, all faster.
- Regression seeds 2002/3003 (pre-dork-hold binary): searched win→loss **1 = DRAWS-DIVERGE variance**
  (d5_s2002 gi54, different physical game — harness-flagged), net searched **+8**, d0 net **+68**.
- Viewer protocol check: **0 contract-fail** (47 play-drift EXPECTED — Hinata plays differently now;
  play-drift is informational, not a failure), 0 mull-drift.

**Accept requires a FRESH smoke+regression run on the FINAL (dork-hold) binary** (the regression
above predates the dork-hold add), THEN `--accept` each mode. Overnight deferred.

## RESOLVED (2026-07-07) — the "regression" was a SENTINEL BUG, now fixed

The whole autonomous regression was **not** a cost of correctness — it was a `crackle_targets`
**sentinel bug**. The legacy/"no declared count" value must be **-1** (→ auto-max discount), but
`Action::crackle_targets` **defaulted to 0**, and several call sites/defaults passed `0`. A `0`
means "declared ZERO extra targets" → `HinataGenericDiscount` returns `min(X, 1+0) = 1` instead of
the auto-max, so Crackle was priced with only a {1} discount — drastically overpriced — and the
search never found the `Reality Spasm×N → big Crackle` combo (gi26: identical hand+draws, OLD wins
T5, buggy-NEW just attacks and loses). Instrumenting `HinataGenericDiscount` on gi26 showed tens of
thousands of `x=5 ct=0 -> disc=1` calls — the smoking gun.

**Fix (sentinel = -1 everywhere):**
- `Action::crackle_targets` default `0 → -1` ([TurnSolver.h:84](../../src/ai/TurnSolver.h)).
- `AIEngine::CastSpellFromHand` decl + `cast_by_name` lambda defaults `0 → -1`
  ([AIEngine.h:331](../../src/ai/AIEngine.h), [AIEngine.cpp:1381](../../src/ai/AIEngine.cpp)).
- Two `apply_one(...)` call sites (cascade, alt-lifegain payload) passed literal `0 → -1`
  ([TurnSolver.cpp:2976,3411](../../src/ai/TurnSolver.cpp)).
- `EffectHandler` `entry.crackle_targets.value_or(0) → value_or(-1)`
  ([EffectHandler.cpp:246](../../src/core/EffectHandler.cpp)).

**Result (Hinata smoke, faithful Crackle + second main, POST-fix):**

| depth | GT wins/avg | POST-fix wins/avg | vs GT |
|-------|-------------|-------------------|-------|
| d0    | 518 / 6.87  | **597 / 6.79**    | +79 wins, faster |
| d3    | 145 / 5.74  | **145 / 5.58**    | same wins, faster |
| d5    | 71 / 5.79   | **72 / 5.61**     | +1 win, faster |

Gate: **searched win→loss = 0** (was 15 with the bug); searched `earlier=38`, `loss→win=1`, one
`turn-later` (gi50, a kept-hand divergence = different physical game), 116 same-turn play-changed
(faithful damage now visible on lethal turns — benign). d0 net +79 (`loss→win=93` vs `win→loss=14`).
The only "REGRESSION DETECTED" is the digest changing → needs `--accept` rebaseline. **Faithful
Crackle is now outcome-neutral-or-better autonomously AND correct.** Adopt A + C together.

**Remaining:** classify is clean; still TODO = regression-mode run, `--accept` smoke+regression GT
(overnight deferred), then Stage B (viewer picker) + original viewer Crackle/Soulfire request.

## ISOLATION MEASUREMENT (2026-07-07) — Stage C is pure upside; Stage A is the whole regression

Ran Hinata smoke three ways (GT = pre-Crackle baseline):

| depth | GT wins/avg | **A+C** (faithful + 2nd main) | **C-only** (2nd main, NO faithful) |
|-------|-------------|-------------------------------|------------------------------------|
| d0    | 518 / 6.87  | 389 / 7.23                    | **597 / 6.79** (+79, faster)       |
| d3    | 145 / 5.74  | 136 / 6.21                    | **145 / 5.58** (same, faster)      |
| d5    | 71 / 5.79   | 65 / 6.08                     | **72 / 5.61** (+1, faster)         |

**Stage C (enable second main for Hinata) is a STRICT IMPROVEMENT** — more wins, faster,
every depth, zero win→loss. The deck genuinely wants attack-then-Crackle post-combat; enabling
the second main lets it. It only "fails" the gate because the digest changed (all movement is
positive). **Adopt it** (rebaseline).

**Stage A (faithful autonomous Crackle) is the ENTIRE regression.** Adding it on top of C drops
d0 597→389 (−208), d3 145→136, d5 72→65, and slows every depth. Classification of 20 d0 win→loss
games: **13/20 like-for-like line changes** (identical hand+draws → genuinely worse play), 7/20
draw-divergence (mostly downstream of a Crackle/Ponder resolving differently). So the autonomous
faithful model is a real, broad capability loss — **not** the "small, X=2/3-only" change we
predicted. Mechanism (gi9/gi44): the engine stops assembling the `Reality Spasm×2 → big Crackle`
burst once Crackle can cost it creatures; greedy d0 has no lookahead to justify the self-damage.

**Conclusion / recommendation:** the auto-max face-only discount is a load-bearing simplification
for autonomous goldfish strength. Making the AUTONOMOUS engine faithful nerfs the deck ~25% at d0.
The user framed this as "primarily a viewer change" — vindicated. Plan:
- **Adopt Stage C** (second main) as an independent measured improvement — rebaseline.
- **Scope faithful Crackle to the VIEWER only** (Stage A → viewer-only, gated on
  `g_play_target_chooser` active): autonomous keeps auto-max (byte-identical to C-only baseline);
  the viewer gets declare-targets → derive-discount → faithful-damage → opp-creature-targeting.
- Awaiting user decision before reshaping the WIP.

## CONTINUATION STATE (2026-07-07, pre-compaction) — read this first

**All Crackle work is UNCOMMITTED WIP** (last commit is `cee32c2`, the viewer queue fix + test
infra). `git status` shows the WIP across: `src/ai/TurnSolver.{h,cpp}`, `src/core/GameState.h`,
`src/core/SpellEffects.h`, `src/ai/AIEngine.{h,cpp}`, `src/core/EffectHandler.cpp`, plus the
Soulfire dialog fix (also in TurnSolver.cpp) and this doc. `git checkout` reverts everything.

**Done + builds (Release clean):**
- Soulfire dialog fix (GT-neutral, human-play CheckLine only).
- Crackle Stage A engine: `crackle_targets` field; `HinataGenericDiscount(...,crackle_targets=-1)`
  overload (–1 = legacy auto-max, no kill); `IsCrackleCountSpell` predicate (excludes Reality
  Spasm); `CrackleExtraTargetOrder`/`CrackleHitExtraTargets` (expendable-first, Hinata last, dynamic
  self `5X<life`); enumeration emits one heuristic count per X (**lethal→faithful max, non-lethal→
  `-1` legacy**); faithful damage lockstep in `ApplyPlanDirect` + `EffectHandler`, threaded through
  `apply_one`/`CastSpellFromHand`/all call sites.

**Measured:** Stage A alone regresses Hinata (d3/d5 **12 searched win→loss**, d0 518→~348) —
root-caused above: faithful Crackle **pre-combat** kills the attackers before combat. **Stage C is
the fix, do it next.**

**NEXT STEP — Stage C (in progress):** make Hinata a second-main deck so the search runs the combo
AFTER combat. The gate is `GoldFishRunner::DeckUsesSecondMain` ([src/runner/GoldFishRunner.cpp:45](../../src/runner/GoldFishRunner.cpp)),
which today returns true only for `spectacle_cost` / `lifegain_to_loss`. Add a Hinata-archetype
condition — e.g. `def->params.hinata_cost_reducer` (Hinata herself) OR `IsCrackleCountSpell(def->params)`
(Crackle) → return true. That flips `SetSearchPostCombat(true)` for Hinata everywhere
(AnalyzerEngine/BatchRunner/ExhaustiveKeep all read `DeckUsesSecondMain`). Also mirror the archetype
detection in `DecisionProviders.cpp:1199` (the comment says it mirrors `DeckUsesSecondMain`).
Then re-measure `bash test/regression.sh --smoke --deck=hinata`; expect the **12 searched win→loss
to recover** (search defers Crackle to main 2, attacks first). d0 greedy may stay shifted (no
lookahead to defer) — that's the lighter bar, rebaseline it. If searched still regresses, the
search may need main-1 SUPPRESSED for Hinata (the "skip main 1" variant), not just main-2 enabled.

**Then:** Stage B (viewer count picker), GT rebaseline (smoke+regression; overnight deferred),
viewer checks. Archetype note for later per-deck main-1/main-2 policy: Anti-Lifegain/Slivers/Knights/
Burn need main 1 (combat pump); Hinata/TH don't.

## Done already

- **Soulfire dialog fix** ([TurnSolver.cpp](../../src/ai/TurnSolver.cpp) `CheckLine` sub-builder):
  the `count=0` Soulfire variant now emits a structured `soulfire` sub (detected via the card's
  `damage_equals_top_mv`), so the viewer shows the nice per-count picker instead of the flat
  "choose how to resolve" fallback. Human-play `CheckLine` only → GT-neutral.
