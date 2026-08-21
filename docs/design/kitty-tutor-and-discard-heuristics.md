# KittyEquipment: tutoring and discard — measured, and there is nothing to build

> **VERDICT 2026-08-21.** USER asked for heuristics for tutoring "and maybe even discard". All four
> candidate decisions were measured before anything was written. Three are already correct or
> already searched; the fourth is a real coverage gap that is **measured immaterial**. Net result:
> one documented rejection and no new heuristic. Recorded so the question is not re-opened blind.

Denominators first, from the 96 logged games in `logs/kitty_census/*` — this repo has burned a
session optimizing a decision that barely fires, and a rate without its denominator has flipped a
verdict here before.

## 1. Cleanup discard — INERT, do not build

**0 DISCARD actions in 96 games.** The deck never reaches a cleanup discard: it empties its hand on
cheap equipment and its games end around turn 5, so hand size never exceeds the limit at cleanup —
even with four Puresteel Paladins drawing.

This matches the base hook's own note (`CleanupDiscardSearchWidth`): "five of nine suite decks never
reach a cleanup discard at all". KittyEquipment is a sixth. There is no decision to make, so a
bucket-discard policy for this deck would be dead code with a maintenance cost and no measurement
able to move.

## 2. Armored Skyhunter's attack dig — ALREADY OPTIMAL

36 decisions across 32 of 96 games (33% of games, 0.38/game) — the deck's most frequent selection
decision, and it is greedy (`AttackDigPutCandidates` -> `ranked.front()`, no search: the trigger
resolves inside combat where no plan-variant branching exists).

Decoded every one of the 36 against what was available: **0 picks below the maximum
`equip_power_bonus` on offer** (Colossus Hammer 24, O-Naginata 4, Bonesplitter 4, Jitte 4 — the last
being the only legal put in those digs).

And max-power is the right OBJECTIVE here, not merely the implemented one: the put bypasses the equip
cost entirely, and in a goldfish with no blockers every competing property is inert — trample and
lifelink (Loxodon Warhammer, Shadowspear) have nothing to trample or race, haste and shroud
(Lightning Greaves) do nothing to a creature already attacking, and Jitte's charge counters are
future value in a race the deck is trying to end. No headroom.

## 3. Stoneforge Mystic's put-from-hand — ALREADY SEARCHED

Not a heuristic at all: `CollectActions` emits one `PutFromHandAbility` variant per (untapped source,
DISTINCT matching hand card name), so every distinct put is a real search branch.

## 4. Stoneforge Mystic's tutor — a REAL gap that is MEASURED IMMATERIAL

The gap is genuine. `GenericProvider::TutorCandidates` deliberately encodes no ranking ("no
deck-agnostic tutor heuristic worth encoding") and returns targets in **library order**; the deck has
**8** distinct Equipment names against a default `TutorSearchWidth()` of **6**. So two legal targets
go unscored at every tutor, and which two is decided by shuffle order rather than merit.

Widened to 8 behind `MTG_KE_TUTOR_ALL` and measured, paired, 150 games per cell (`logs/kitty_tutor`):

| cell | delta | faster | slower | plays differ | cost |
|---|---|---|---|---|---|
| train | **+0.0000** | 0 | 0 | **1 / 150** | +13.5% |
| hold | **+0.0000** | 0 | 0 | **1 / 150** | +12.3% |

The truncation does bite — it changes the decision stream in one game per cell — but it never changes
an outcome, and it costs 12–13% because each extra target is a full rollout and 26 tutor triggers per
96 games is enough to show.

**Why it cannot matter much, which is the part worth keeping:** with a Puresteel out every equip is
{0}, so *which* Equipment was fetched barely changes the turn; and with no Puresteel out, the cheap
equipment already inside the width-6 window is what gets cast anyway. The lever stays in the tree as
the record. Do not re-widen on the coverage argument alone — that argument is measured empty; it
would need a new one.

## What this leaves

Nothing to adopt. The honest summary is that this deck's remaining judgment calls are either forced
(discard), already right (dig), or already searched (put, tutor-within-window). The open items are
elsewhere: site 6's adoption call, and `MTG_EQUIP_LOG_TRUTH` / `MTG_BIG_SOLVE_MEMO`.
