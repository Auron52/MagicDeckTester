# Dragonstorm: the search doesn't recognize its own go-off as lethal (ROOT-CAUSED)

**Status: root-caused 2026-07-22, fix not yet implemented.** Found while investigating the Unclaimed
Territory `colored_creature_only` regression (see `unclaimed-territory-restricted-mana.md`). The user
reframed that regression: it is **mostly the search wasting rituals / mis-sequencing**, not a faithful
mana correction — and the Unclaimed change merely *exposed/amplified* a pre-existing Dragonstorm
search-quality bug. This doc is that bug.

## The observable bug (gi112, the clean repro)

d5 s3003 game 112. Opening 7 (no draws needed): Ruby Medallion, Unclaimed, Rite of Flame, Desperate
Ritual, 2× Sandstone Needle, Dragonstorm.

- **Baseline (fake-red Unclaimed) won T3**; under the fix the search wins **T5** and is UNRECOVERABLE by
  budget/depth (T5 at depth 8 / budget 4000, and still T5 at depth 5–6 / budget 30000).
- **A real-red T3 kill DEFINITELY EXISTS under the fix** (verified by forcing the line via claude-play,
  `--force-mulligan "0:" --choices "18,-1,146,-1,0,..."`): lead both tapped Sandstones T1/T2, Ruby T2,
  then T3 cast Rite + Desperate + Desperate + Dragonstorm off the two Sandstones' 4 real red (Unclaimed
  pays only `{C}`). Storm = 3 prior spells → **4 dragons** (`max_puts=4`). Result: **opponent to −48 (3
  Scourge) / −34 (AI's Lathliss+2 Scourge+Karrthus)** — super-lethal T3, no fake red anywhere.
- So gi112 is a **genuine search failure**, NOT a faithful loss. The search never assembles the
  trivially-lethal T3 go-off (kill bar is only ≥3 dragons = 2 rituals + Dragonstorm).

Three symptoms the user identified, all the same root:
1. casts a ritual (Desperate Ritual) on a setup turn with **no same-turn payoff** → floats & wastes the
   mana + a Sandstone depletion counter + storm count;
2. mis-sequences lands — leads **untapped** Unclaimed T1 (nothing to cast) instead of the **tapped**
   Sandstones, so both aren't online by T3 (violates mtg-ai skill Land-Drop rule #2);
3. prefers hard-casting a Dragon over the finishers (Apex/Dragonstorm).

## ROOT CAUSE (confirmed in code)

Dragonstorm is **rollout-based** — its profile (`decks/Dragonstorm/Dragonstorm.profile.json`) has NO
`value_play`/`value_leaf` model (just `card_scores` + `mulligan`). So the d3/d5 search evaluates every
leaf with the **greedy d0 policy** (`TurnSolver::Solve` / `RolloutWinTurn` → `PlayOut`). Greedy quality
drives everything.

The greedy scorer `Solve::consider` (`src/ai/TurnSolver.cpp:2064`) ranks non-winning plans by
`total_eval` (= Σ `c.eval`, board-development) and lets a **lethal plan dominate** via:
```
wins = (projected_atk + direct_dmg + extra_lethal) >= opponent.life
extra_lethal = provider.ExtraLethalDamage(state, casting)   // TurnSolver.cpp:2219
```
- Dragonstorm the spell has **`direct_damage == 0`** (its damage is the fetched dragons' Scourge ETB,
  which resolve later, not part of the plan's `casting` list).
- **`DragonstormProvider` does NOT override `ExtraLethalDamage`/`HasExtraLethalModel`** — only
  `GenericProvider` (returns **0**, `HasExtraLethalModel=false`) and `TreasureHuntProvider` implement
  them (`src/ai/DecisionProviders.cpp:270-278`; the only two overrides). `DragonstormProvider`
  (`DecisionProviders.h`) overrides `TutorToBattlefieldPutOrder`, `CastOrderRank`,
  `UseAccelPrefixCollapse`, `WantsCastOrderingSearch` — but NOT the lethal model.

**Therefore the greedy/rollout `wins` check NEVER recognizes the Dragonstorm go-off as a win**
(`extra_lethal = 0`, `direct_dmg = 0`). The opponent only dies in *execution* (Scourge ETB fires for
real). Consequences:
- greedy never fires the go-off as a **win-now** line → treats Dragonstorm as a board-dev spell (by
  `total_eval`, which a hard-cast Dragon's power+toughness beats → symptom 3);
- rituals get cast for `total_eval` "spend the most mana" with no recognized payoff to consume them →
  waste (symptom 1) (cf. mtg-ai skill §Selection: "do not include a mana producer if it displaces a
  higher-cost spell and leaves total mana spent the same or lower");
- every rollout leaf therefore casts Dragonstorm *late* (whenever board-dev happens to line up), so
  every candidate line rolls out to ~T5 → the search can't tell "lead Sandstones (→ T3 kill)" from
  "Unclaimed T1 (→ T5)" and no budget/depth rescues it (the win-turn signal is flat).

This is exactly the mtg-ai skill's mandate unimplemented: **the Win-Now check must find the
ritual→ritual→payoff lethal sequence.** For Dragonstorm that sequence's lethality lives entirely in
`ExtraLethalDamage`, which is a no-op.

## The fix (proposed, not yet built)

Primary — **implement `DragonstormProvider::ExtraLethalDamage` + `HasExtraLethalModel`** (the go-off
win-now model, provider-scoped per the skill). Given the plan's `casting` set at a state, project the
go-off damage: storm count (prior spells this turn) → dragons fetched (= storm copies + 1, capped by
library Dragons) → the ETB burst the picker would deal. Damage model must capture what execution does:
Scourge-of-Valkas ping scales with Dragons in play as each enters (Σ over entries), Lathliss adds a 5/5
per nontoken Dragon (more ping fuel), Karrthus/haste dragons can attack the entering turn. The picker
(`TutorToBattlefieldPutOrder`) already encodes the ideal put; reuse its role counts. Kill bar is low
(≥3 dragons), so a conservative-but-nonzero projection already flips `wins` for real go-offs → greedy
assembles them → rollouts return the true early win turn → the search finds "lead Sandstones → T3".

Secondary — **d0/rollout "rituals-for-payoff-only" guard** (user's slam-dunk for d0): on a NON-lethal
turn, don't cast a mana ritual whose float isn't consumed by a same-turn cast. Fixes ritual waste on
setup turns (where win-now can't fire). User is UNSURE about applying this as a *search enumeration*
prune (you keep the ritual in hand for a future turn, so the tree may not shrink) — so scope it to the
greedy/rollout policy, not the enumerator. (Related dormant precedent: `MTG_HINATA_SPASM_GATE` in
`f2ee9d7`/`708ae7b`, root-caused in `hinata-spasm-gate-rootcause.md` — a Crackle-specific search prune;
"similar idea, not exactly this change." The other agent is re-measuring it; user believes it's ~neutral
+ ~0.5% perf, not harmful.)

Tertiary (parked, overkill-only) — picker tweak in `TutorToBattlefieldPutOrder` **Case A**
(`DecisionProviders.cpp:1839`): drop the up-front haste reservation when Lathliss is present (go max
Scourge, the −48 pick); reserve the haste dragon only when Lathliss is absent. User: "irrelevant once
they're that deep in the negatives."

Also worth checking: land-drop should prefer the **tapped** land when the untapped land's mana isn't
used this turn (lead Sandstones) — mtg-ai skill Land-Drop #2. May fall out of the win-now fix, or need a
provider land hook.

## Method / A-B harness (reuse)

`logs/unclaimed_ab/` has the full autonomous A/B: `run_case.sh` (one binary, `--cards-json` toggles
baseline vs fix), `realred.py` (faithful-vs-ignitable classifier), `validate_case.py`,
`gen_viewer.py` (viewer-grade per-game logs via goldfish `--force-mulligan "0:" --log-dir`; card
NUMBERING must come from the goldfish/claude-play logger — the batch `--game-trace-dir` writes empty
`cardNumbering` so hands show blank in the viewer; do NOT "fix" the batch logger to number — it perturbs
London bottoming and shifts the GT digest, verified). Measure any fix as: Dragonstorm regression train
seeds (s2002/s3003) win-turn + perf, per heuristic-optimization skill; report before adopting.
Reproduce a batch game gi in a single run: `--seed <base+gi> --game-index <gi> --games 1` (goldfish
seeds each game `base_seed+gi`, spawns `base_game_index+gi`).

## Status of the Unclaimed fix itself (still uncommitted)

The `colored_creature_only` fix is correct + faithful (per-case proof: 45 regressed games across all 5
cases = 26 faithful-block / 18 ignitable-deferral / 0 stumbles; all 5 win→loss faithful; d0 100%
faithful — but note that classification is now REFRAMED: many "ignitable/faithful" cases are the search
wasting rituals, which this go-off bug would improve). GT NOT rebaselined; decision (rebaseline vs hold
vs revert) deferred pending the ritual/go-off work, since fixing the search will move the Dragonstorm
numbers again.
