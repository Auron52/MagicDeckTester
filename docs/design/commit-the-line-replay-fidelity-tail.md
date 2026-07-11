# Commit-the-line replay-fidelity tail (fd-diverge optimism)

Status: **FIXED (2026-07-11) — burn `d3 s6006` fd-diverge 8 → 0.** Two commits, same
root cause (executor lapsing a Light Up the Stage-staged copy, desyncing from the
committed line): the **staged LAND** class first, then the **staged SPELL** class
(6225). Not a card bug. A key gotcha that made 6225 look murky is recorded below:
`--seed X --games 1` faces opponent **pattern 0** (pure goldfish), but a batch game
at index `gi` faces `PATTERNS[gi % 10]` — so the real batch game must be reproduced
with `--game-index gi` or its opponent board (and thus its whole line) differs.

## What fd-diverge is

`MTG_FD_ORACLE` (fd = "future-decision") flags a game where the **realized** win
turn is later than the win turn the search **predicted/committed to** at the
breakpoint. Off-by-one (delta 1) is minor; delta ≥ 2 is "severe". On the burn
overnight rebaseline run, the tail was:

- 50 off-by-one (delta 1) — expected search/replay noise.
- 4 "severe" (delta ≥ 2) rows that collapsed to only **2 unique games**
  (seeds 6225 and 6303, each counted once at d3 and once at d5). Both predicted
  **T4** and realized **T6** (delta 2).

## Root cause (2026-07-11, FIXED) — the executor lapsed a Light Up the Stage-staged land

Root-caused on **burn seed 6303** (predicted T4, realized T6) via `MTG_FD_TRACE`:

1. **Light Up the Stage** (cast turn 2) exiles 2 cards playable only **through the
   end of turn 3** (`expiry_turn = cast_turn + 1`; `Player.h:6,23`,
   `EffectHandler.cpp:349`). On 6303 it stages **a Mountain + a Lightning Bolt**.
2. The committed T4 line reaches lethal by casting **two Shard Volleys on turn 4**
   (each extra cost = *sacrifice a land*) + a Goblin Guide attack. That needs a land
   drop on T4, which only works if the **staged** Mountain is spent on **T3** and
   the **drawn** Mountain is held for T4 — a legal sequence.
3. The gap was **which copy each side plays**. The search's rollout and the
   executor use **separate** land-play functions: `TurnSolver::PlayLandByName`
   (search/rollout) vs `AIEngine::TryPlaySpecificLand` / `TryPlayLand` (real
   executor). Both took the **first hand match** by hand order, so the executor
   played the *drawn* Mountain on T3, lapsed the staged Mountain at end of T3, and
   had no land on T4 → one Shard Volley → realized T6. The staged-T3/drawn-T4 line
   the search intended was never realized.

**Why the first attempt failed** (recorded so the trap is remembered): changing only
`PlayLandByName` to prefer the staged copy made the *search* more optimistic (more
committed T4 lines) while the *executor* still lapsed the staged land — so the
fd-diverge set *grew*. The two paths must be fixed **together**.

**THE FIX (2026-07-11).** Prefer the expiring staged copy of a land in **both**
paths — `PlayLandByName` (removed the `s_human_play` gate) **and**
`TryPlaySpecificLand` (added the same pick). Now the search's committed line and the
executor pick the same land, so the line realises exactly. Result: seed 6303 → **T4**;
burn `d3 s6006` fd-diverge **8 → 2**; **byte-identical on all non-staging decks**
(slivers/th/knights/antilife/hinata smoke unchanged); burn smoke = play-changed only,
**0 searched win→loss**, no win-turn change. It is strictly-correct MTG play (spend
the deadline resource first) and generalizes to any staged-land situation.

**SECOND FIX — 6225 = the staged-SPELL analog (2026-07-11).** The initial "flood /
leaf-optimism" read was WRONG: it came from analyzing `--seed 6225 --games 1`, which
faces **opponent pattern 0** (goldfish). The real batch game 6225 is index 219 →
`PATTERNS[9]` (creatures), reproduced with `--seed 6225 --game-index 219 --games 1`
(predict T4, realize T6, `fd_best_win=4` at turn 2, `searched_depth=3` — genuinely
verified-in-horizon, not a leaf estimate). With opponent creatures present, **Light
Up the Stage stages a Skullcrack**, and the committed T4 line casts `Searing Blood +
Skullcrack` (opp 8 → −1). The executor cast the **drawn** Skullcrack on T3, lapsed
the staged one, and had no Skullcrack for T4 → one Skullcrack total, realize T6 —
exactly the land bug but for a **cast**. Fix: apply the same staged-copy preference
to `AIEngine::cast_by_name` (the executor's spell cast). Result: 6225 → T4, 6420 →
resolved, burn `d3 s6006` fd-diverge **2 → 0**; byte-identical on non-staging decks;
burn + hinata (Expressive Iteration / Soulfire Eruption stage spells) change to
faster/neutral lines only (audit: 0 searched win→loss). Smoke+regression rebaselined.

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

## Follow-ups (both fd-diverge classes now fixed)

- **Remaining staged-copy paths not yet touched.** The fix covers the two paths that
  the burn games exercised — `PlayLandByName` / `TryPlaySpecificLand` (lands) and
  `AIEngine::cast_by_name` (spells). For completeness, audit the sibling selectors
  for the same "prefer the expiring staged copy" rule: `cast_alt`
  (`AIEngine.cpp:~1658`, alt-cost casts) and any TurnSolver-side spell cast the
  *search* uses to build the committed line (the executor-only spell fix sufficed for
  6225 because the search's sim already played staged-first by hand order, but that is
  luck, not invariant — a shared `PickStagedFirst(hand, name)` helper would remove the
  latent desync).
- **Broader reproduction gotcha (write it into any fd-diverge investigation):** a
  batch game at index `gi` faces `PATTERNS[gi % 10]` opponent creatures
  (`GoldFishRunner::PopulateOpponentSpawns`); `--seed X --games 1` is always pattern
  0. Reproduce the exact batch game with `--game-index gi`, else you analyze a
  different game (this cost real time on 6225).

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
