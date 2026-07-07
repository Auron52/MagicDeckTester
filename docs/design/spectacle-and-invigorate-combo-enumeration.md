# Enumerating the Spectacle (Light Up the Stage) and Invigorate-combo lines

> **STATUS: SHIPPED (2026-07-07).** Both lines are now committable. The fixes differ from the
> original plan below (kept for the reasoning/history):
>
> - **#8 spectacle (commit `984cc99`):** the fix was NOT the "opaque apply reorder + re-add the
>   reverted general credit" plan. The search already had a dedicated **spectacle trigger+draw
>   path** in `EnumeratePlans` (a `{trigger, draw}` plan builder, trigger pushed FIRST so the
>   opaque apply casts the burn before Light Up). Three changes made it enumerate the fixtures:
>   (1) emit a plan per **distinct** affordable trigger (not just the cheapest) → Searing Blood as
>   well as Lightning Bolt; (2) **fix a latent naming bug** — the trigger name was `ap.hand[cand_idx]`
>   (a candidate index used as a hand index), which mislabeled/cast the wrong card (a Mountain)
>   when the hand held lands, and was the reason "Searing Blood → Light Up" wasn't enumerated AND
>   why some old plans cast a phantom land as the "trigger" (an illegal free Light Up); (3) allow a
>   **sac-land** burn (Shard Volley) as a trigger and **hoist** it ahead of the opaque Spectacle
>   spell in `ApplyPlanDirect` + the `AIEngine::TakeTurn` mirror. Burn-only; win counts identical,
>   avg win turn net slightly better (earlier=12 vs later=8, the later mostly removing the
>   phantom-trigger bug); burn GT rebaselined. All three fixtures → `accept`.
> - **#2 Invigorate (commit `2df0cbe`):** solved **match-only** in `CheckLine`, NOT by enumeration
>   (enumerating Invigorate as an action shifted plan indices at every antilife decision and broke
>   saved replays). Invigorate is an own-creature-pump safe alt payload skipped before the alt
>   emission when we have no own attacker, so it is in no plan's cast multiset. The matcher now
>   folds such an L704-blocked payload out of the human's cast set, so "Fiery Justice; Invigorate;
>   Swords" matches the enumerated "Fiery Justice; Swords" (same win turn; payload moot / auto-fires
>   as a closer). Byte-identical everywhere (CheckLine is only the --validate-line path). Fixture →
>   `choose`. **Known limitation:** if Invigorate were ever the *necessary* closer AND its only
>   target is one a same-line Swords removes, the folded line would under-realise — not the case for
>   any real fixture (the combo lines are lethal without the pump).

Self-contained record (2026-07-06). Two burn/anti-lifegain lines a human can legally play in the
viewer but the **search never enumerates**, so the play viewer shows them as a `legal_not_enumerated`
rejection (not committable). The `--validate-line` check was already fixed to report them *accurately*
(commit `30743aa`); this doc is about making the **search enumerate them** so they become `accept`
(committable) — **without** the regression a naive attempt caused.

These correspond to the user's issues **#8** (Light Up the Stage "artifacts") and **#2** ("saved
artifact for Anti-Lifegain") — both were "I tried to commit a line and it was disallowed", logged under
`logs/play/rejections/*.json`.

## The reject artifacts = the test fixtures

Each is reproducible with `--claude-play … --validate-line "<line>"`. Success = verdict becomes
`accept` (or the run proceeds to the next decision) instead of `legal_not_enumerated`.

| artifact | deck | seed/gi/turn | line | old reason |
|---|---|---|---|---|
| `burn_txt_s19_gi18_t3` | burn | 19/18/3, choices `1,0,0` | `land=Mountain;cast=Searing Blood;cast=Light Up the Stage` | can't pay {2}{R} (spectacle) |
| `burn_txt_s4_gi3_t4` | burn | 4/3/4, choices `1,0,0,1,0,-1,-1` | `land=Mountain;cast=Shard Volley;cast=Light Up the Stage` | can't pay {2}{R} (spectacle) |
| `burn_txt_s6_gi5_t3` | burn | 6/5/3, choices `1,0,0,0,0` | `cast=Searing Blood;cast=Light Up the Stage` | can't pay {2}{R} (spectacle) |
| `Anti-Lifegain_cod_s17_gi16_t5` | antilife | 17/16/5, choices `0,0,1,1,4,7,4,6,7,-1` | `land=Windswept Heath;cast=Fiery Justice;cast=Invigorate;cast=Swords to Plowshares` | Invigorate combo not enumerated |

Repro (example): `build/Release/mtg decks/burn.txt --profile decks/burn.profile.json --cards-json
src/cards/data/cards.json --claude-play --seed 19 --game-index 18 --max-turns 8 --depth 0 --choices
1,0,0 --validate-line "land=Mountain;cast=Searing Blood;cast=Light Up the Stage"`

---

## #8 — Spectacle: "burn, then Light Up the Stage {R}"

**Card:** Light Up the Stage = `{2}{R}` with **Spectacle {R}** (`spectacle_cost` param): if an opponent lost
life this turn, it costs `{R}`. `EffectiveCost` (TurnSolver.cpp ~L451) already returns the spectacle cost
when `state.opponent_lost_life_this_turn`.

**Why the search misses it:** at *enumeration* (turn start) the opponent has not lost life yet this turn,
so `EffectiveCost` returns the full `{2}{R}`. The `{burn, Light Up}` subset is then priced at
`burn + {2}{R}` and dropped by the affordability gate as unaffordable — even though, *when applied*, the
burn resolves first, the opponent loses life, and Light Up genuinely costs `{R}`.

**Naive attempt (REVERTED — do not just redo this):** credit the spectacle generic savings in the
affordability gate (both `Solve::consider` and `EnumeratePlans::eval_and_push`, right after the same-turn
**affinity** credit at ~L1338, mirroring it) whenever a subset holds a spectacle spell **and** deals
opponent damage (`direct_dmg > 0`). This DID promote all three Light Up lines to `accept`, but **regressed
burn** (smoke: searched `later=4`, d0 `later=49`, `earlier=0`). Root cause: **cast order**. Light Up
`stages_cards`, so it is an `OrderingOpaque` / re-solve-breakpoint card; ApplyPlanDirect's **opaque
branch** (TurnSolver.cpp ~L3115–3124) casts enablers first and then everything else **in plan order**, NOT
by `CastOrderRank`. So the plan frequently casts **Light Up BEFORE the burn** → spectacle off at apply →
Light Up pays `{2}{R}` → the plan under-realises (can't also afford the burn) → weaker line → win later.

**Proper fix (do this):**
1. **Guarantee the apply order: damage-source BEFORE the spectacle spell.** In the opaque apply branch
   (TurnSolver.cpp ~L3115–3124) *and its lockstep mirror in `AIEngine::TakeTurn`* (search for the
   matching enabler-first opaque path; the code comment at L3130 says "Mirrored in AIEngine::TakeTurn"),
   when the cast set contains a spectacle spell, cast the spectacle-enabling **damage sources** (a
   `DirectDamage` that makes the opponent lose life — face/any/multi burn, or Searing Blood's death
   rider) *before* the spectacle spell. Casting a burn earlier is always safe in a goldfish (it goes to
   face regardless, combat is later, prowess fires on cast independent of order, and Light Up's staged
   cards are playable through next turn either way). Suggested order in the opaque branch: enablers →
   spectacle-enabling damage sources → spectacle spell(s) → the rest (plan order). Keep it inert unless a
   spectacle spell is present, so non-spectacle decks stay byte-identical.
2. **Re-add the enumeration spectacle credit** (the reverted block), now SAFE because the apply realises
   it. Gate it on `direct_dmg > 0` (opponent will lose life) — note `direct_dmg` for Searing Blood is
   only credited when it kills (death rider), which is exactly when it enables spectacle, so the gate is
   correct: Blood-on-own-creature (no opponent damage) correctly does NOT credit spectacle.
3. The credit + the order fix are **lockstep**: the enumeration may credit spectacle **only** for the
   ordering the apply guarantees. Both must ship together.

**Expected result:** the three Light Up artifacts become `accept`; smoke/regression show burn `later=0`
and now `earlier>0` (the spectacle line is a real improvement the search previously missed). **Rebaseline
burn GT** (smoke + regression accept), defer overnight. Non-burn byte-identical.

**Lockstep sites (all must agree):**
- Enumeration affordability: `Solve::consider` + `EnumeratePlans::eval_and_push` (~L1338, the two
  identical affordability blocks — the affinity credit shows the pattern; `Action` carries
  `has_spectacle` (main.cpp/CollectActions L977) and `def`).
- Apply order: `ApplyPlanDirect` opaque branch (~L3115) + `AIEngine::TakeTurn` opaque mirror.
- `EffectiveCost` already handles the discount at apply time — no change there.

---

## #2 — Invigorate combo: "Fiery Justice, Invigorate, Swords"

**Line:** `land=Windswept Heath; cast=Fiery Justice; cast=Invigorate; cast=Swords to Plowshares`
(anti-lifegain s17 gi16 t5). Invigorate is an **alt-cost** spell (free: opponent gains 3 life, reversed to
−3 by a Tainted Remedy). The `--validate-line` check now reports `legal_not_enumerated` (was
`unsupported`), so it is recognised as legal but the search does not enumerate this 3-spell subset.

**Not yet root-caused — investigate first.** Candidate reasons the subset is dropped at enumeration:
- **Swords enabler gate**: `SubsetHasUnbackedLifegainRemoval` (TurnSolver.cpp ~L624) rejects a Swords cast
  unless a `lifegain_to_loss` enabler is live or in the same subset. Check the s17 gi16 t5 board: is a
  Tainted Remedy / Plague Drone in play? If not, Swords is "unbacked" and the subset is (correctly?)
  rejected — in which case the human's line hands the opponent life and the rejection may be *right*
  (verify whether the user's line is actually good, or whether an enabler is in play so it should pass).
- **Invigorate targeting / affordability**: the alt-cost cast needs a legal target and the free alt cost;
  confirm the enumeration offers Invigorate here.
- The specific 3-card **joint** subset may exceed a group cap or not be produced by the combo enumeration.

Start by dumping the enumerated plans at that decision (`--validate-line` emits the model's plans in the
verdict JSON, and the reject artifact stored `modelPlans`) and compare to the desired line. Then extend
enumeration minimally (like the shipped same-turn Remedy→Swords combo in
`docs/design/antilifegain-swords-targeting.md`) so this subset is produced — **only if** it is genuinely a
good line (an enabler makes the Swords life-loss real). If the line is only good with an enabler the
user had in play, ensure the enabler-in-play case is enumerated.

**Verification:** the s17 gi16 artifact becomes `accept`; smoke/regression on antilife show no searched
`win->loss` / `later`; rebaseline antilife GT only if plays change for the better.

---

## Guardrails (from this session)

- **Byte-identical discipline:** every credit/order change must be inert for decks without the relevant
  card (spectacle spell / Invigorate) — verify with smoke (non-burn/non-antilife unchanged).
- **Measure `later`, not just `win->loss`:** the naive spectacle credit passed the `win->loss=0` gate but
  had `later=4/49` — the real signal. Any enumeration change must show `later=0` before accept.
- **References are commit-only.** All saved refs were committed at the batch start.
- The reject artifacts under `logs/play/rejections/` are the regression fixtures — re-validate all four
  after the fix (script pattern used this session: replay `priorChoices` + `--validate-line`, assert the
  verdict flips to `accept`).
