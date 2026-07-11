# Commit-the-line replay-fidelity tail (fd-diverge optimism)

Status: **open, deferred** — a rare, pre-existing accuracy tail in the
commit-the-line engine. Not a card bug, not result-degrading on aggregate, and it
does **not** bias any keep-profile A/B (both arms use the same engine). Recorded
here so the ~6/1000 tail isn't lost after the burn overnight investigation
(2026-07-10).

## What fd-diverge is

`MTG_FD_ORACLE` (fd = "future-decision") flags a game where the **realized** win
turn is later than the win turn the search **predicted/committed to** at the
breakpoint. Off-by-one (delta 1) is minor; delta ≥ 2 is "severe". On the burn
overnight rebaseline run, the tail was:

- 50 off-by-one (delta 1) — expected search/replay noise.
- 4 "severe" (delta ≥ 2) rows that collapsed to only **2 unique games**
  (seeds 6225 and 6303, each counted once at d3 and once at d5). Both predicted
  **T4** and realized **T6** (delta 2).

## Root cause (2026-07-10) — Light Up the Stage staged land, but the exact mechanism is NOT yet pinned

Investigated the cleanest severe game, **burn seed 6303** (predicts T4, realizes
T6), via `MTG_FD_TRACE` (committed line + predicted opp-life / `hand_lands` per
phase). What is established:

1. **Light Up the Stage** (cast turn 2) exiles 2 cards playable only **through the
   end of turn 3** (`expiry_turn = cast_turn + 1`; `Player.h:6,23`,
   `EffectHandler.cpp:349`). On seed 6303 it stages **a Mountain + a Lightning
   Bolt** (`hand_lands` jumps 0→1 right after it resolves).
2. The search's committed line reaches lethal at T4 by casting **two Shard Volleys
   on turn 4** (each extra cost = *sacrifice a land*) + the Goblin Guide attack
   (opp 8→0). That needs two sacrificeable lands on T4.
3. Reading `hand_lands` across the turns, the search plays the **staged** Mountain
   on **T3** (within its window) and keeps the **drawn** Mountain (card 23) for T4
   — a sequence that is, on its face, **legal**. The executor instead plays the
   drawn Mountain on T3, lapses the staged Mountain at end of T3, and so has only
   one land on T4 (the T4 draw was Eidolon, a non-land) → one Shard Volley →
   realized T6.

**UNRESOLVED CONTRADICTION (do not "fix" until this is understood).** If the
staged-T3 / drawn-T4 line is genuinely legal, an optimal per-turn re-decider should
find it — but **no engine at any budget realizes T4**: default(commit)=T6,
`MTG_LEGACY_SEARCH`=T5, `MTG_FD_ALWAYS_RESEARCH`=T5, and default at depth 5 / 25×
budget still =T6 (predicting T4 *earlier*, `proven_at_turn=1`). Best realized is T5.
So either (a) the "legal T4 line" has a blocker not visible in the phase trace
(e.g. a T3 mana/sacrifice conflict between casting the staged Bolt **and** a Shard
Volley **and** playing the staged land, so holding the drawn Mountain isn't
actually affordable), or (b) the search over-credits something at T4 that even the
staged-land sequence can't deliver. This gap is the crux; it must be closed before
any code change.

**FAILED FIX ATTEMPT (2026-07-10, reverted).** Hypothesis: the executor and search
disagree on *which* Mountain to play on T3 because `PlayLandByName`
(`TurnSolver.cpp` ~4031) picks the first hand match (hand-order dependent) in
autonomous mode and only prefers the expiring staged copy under `s_human_play`.
Change tried: make **all** modes prefer the staged copy. Result: **did NOT help** —
seed 6303 stayed T6 and the burn `d3 s6006` fd-diverge set *grew* (6→8+; the search
committed *more* phantom T4 lines while the executor still realized T6). Reverted;
`src` is byte-clean vs HEAD `5aa7573`. Lesson: the divergence is not a simple
land-copy pick, and the search-side change makes the search *more* optimistic
without improving realization — consistent with (a)/(b) above being the real issue,
not the executor's land choice. Needs the contradiction resolved first.

Ruled out as causes:

- **Not Searing Blood** (the first card suspected). It is modeled correctly at
  `TurnSolver.cpp` ~1063-1072: its `death_trigger_damage=3` is credited to the
  face **only** when `FindBurnKillTarget` finds a killable **opposing** creature
  (`SpellEffects.h:596` skips `controller_index == active`); in a goldfish there is
  none, so it contributes 0.
- **Not Eidolon of the Great Revel.** Its `on_cast_trigger_damage` is applied to
  the **caster** in both the executor (`SpellEffects.h:718`) and the search (the
  search accumulates it as `self_damage` and uses it only as a suicide guard,
  `TurnSolver.cpp:1527`) — never credited to the opponent.

Discriminator: seed 6303 default(commit)=T6 vs legacy(`MTG_LEGACY_SEARCH=1`)=T5 vs
`MTG_FD_ALWAYS_RESEARCH`=T5. It is **not** a search-budget problem: at depth 5 and
25× budget the game still realizes T6 and the oracle still predicts T4 (now
`proven_at_turn=1`) — more search finds the phantom *earlier*, confirming a
modeling over-credit rather than a budget/depth shortfall.

## Why it does NOT block adoption or bias A/Bs

Magnitude refutes systemic harm. On burn seed 6006 depth 5, 1000 games:

| engine | wins | avg win turn |
|--------|-----:|-------------:|
| default (commit-the-line) | 996 | 4.357 |
| legacy (re-decide per turn) | 993 | 4.407 |

Commit-the-line is **net better** — it is faster on ~60 games versus legacy's ~6,
for +3 wins and −0.05 turns overall. The severe fd-diverge are a rare optimism
**tail** (~6/1000), the price of a line-commit engine that wins more games sooner
on average. Commit-the-line is the right default.

Because the tail is pre-existing and engine-level (identical on both A/B arms), it
does not bias the burn NEW-vs-OLD keep-profile comparison or any other keep A/B.

## Next step (when picked up) — CLOSE THE CONTRADICTION FIRST, then decide the fix

Do NOT jump to a code change (the first attempt did, and made it worse). The
investigation order:

1. **Determine whether the T4 line is actually realizable at all.** On seed 6303,
   hand-force the "legal" sequence and see if it wins T4: play the **staged**
   Mountain on T3, cast the staged **Lightning Bolt** + one **Shard Volley** on T3,
   hold the drawn Mountain, then two Shard Volleys on T4. Check the T3 mana budget
   carefully — casting Bolt **and** a Shard Volley (which also *sacrifices* a land)
   the same turn you played only your second land may leave you a mana/land short,
   which would mean the drawn Mountain can't be held and the T4 double-Volley is
   impossible. Use `--force-mulligan` / a scripted line or `MTG_FD_TRACE`'s
   per-phase mana to verify. If no legal sequence wins T4 → the search is
   **over-crediting** (a real fidelity bug: the committed line's T4 is a phantom the
   `FSLineWin` simulation accepts but `ApplyPlanDirect` can't reproduce). Find the
   over-credit in `FSLineWin`/`ApplyPlanDirect` for the Shard-Volley-sacrifice +
   staged-land combination.
2. **If a legal T4 sequence *does* exist**, the bug is that neither the executor's
   committed-line replay nor legacy re-decide finds it. Then instrument WHERE the
   executor's replay of the committed line diverges from the search's simulated
   line (which turn's applied plan first differs, and why) — the land-copy pick was
   only one candidate and was ruled out.
3. Only after (1)/(2) pin the mechanism: make the change, then re-run the fd-oracle
   over the burn overnight seeds (target: severe delta≥2 → ~0), confirm non-Light-Up
   decks stay **byte-identical**, and A/B burn overnight for the aggregate before
   `--accept`.

General watch item: cards that grant a **time-boxed play window** for cards outside
hand (Light Up the Stage, Expressive Iteration, Soulfire Eruption's dig — all share
the `expiry_turn` primitive at `SpellEffects.h:1319-1321`) are where the
search-vs-executor fidelity is most fragile; whatever the fix, add a regression that
exercises the same-turn "spend the expiring resource" line.

## Watch items

- `MTG_FD_TRACE` shows the breakpoint casts; `MTG_FD_ORACLE` flags the divergence
  count at `OnGameEnd` (`AIEngine.cpp:109-121`).
- The rollout uses `shuffle_salt_search` under `ShuffleEvalGuard`
  (`AIEngine.cpp:701-728`); a lookahead-shuffle mismatch is a **different**
  divergence class (draw-breakpoint, recs > 0) and would need separate treatment.
