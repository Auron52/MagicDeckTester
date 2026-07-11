# Commit-the-line replay-fidelity tail (fd-diverge optimism)

Status: **staged-land class FIXED (2026-07-11); one residual class (6225) deferred.**
A rare, pre-existing accuracy tail in the commit-the-line engine. Not a card bug.
The dominant class — the executor lapsing a Light Up the Stage-staged land — is now
fixed (burn `d3 s6006` fd-diverge 8 → 2). The remaining 6225-class (flood +
intermediate leaf-estimate optimism) is a separate, murkier residual, deferred.

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

**REMAINING RESIDUAL — 6225 (deferred, separate class).** After the fix, burn
`d3 s6006` still shows 2 (6225 severe T4→T6, 6420 T4→T5). 6225 is **not** a
staged-land case: it is a **flood** hand (4 Mountains kept, draws more Mountains + a
dead Searing Blood) whose committed *play* line is actually **T5** (Skullcrack ×2 +
Eidolon + Swiftspear prowess). The `fd_best_win=4` is an **intermediate verified
estimate** the oracle recorded that is neither committed (T5) nor realized (T6) —
i.e. a search-consistency / leaf-optimism issue on a flooded draw, with no clean
single mechanism. Deferred as its own investigation (next after this ships).

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

## Next: the 6225 residual (flood + intermediate leaf-optimism)

The staged-land class is fixed and shipped. The remaining 6225-class is a separate
investigation:

1. **Find where `fd_best_win=4` is recorded** for 6225 when the committed line is
   T5. The oracle updates `m_fd_best_win` at `AIEngine.cpp:~1200` from a
   `FullSearchLine` result whose `line.win_turn <= turn + searched_depth - 1`
   (verified-in-horizon). Trace which turn/pass returns win_turn=4 — it is NOT the
   final committed line (T5), so either an intermediate pass or a sub-branch verified
   a T4 the top-level selection then discarded. A verified win the engine does not
   commit is itself suspicious (it should commit the earliest verified win).
2. **Check for a flood realizability gap.** The committed T5 line assumes casting a
   **second Skullcrack** (+ prowess) that the flooded real draws (Mountains + a dead
   Searing Blood) don't deliver by that turn. Determine whether the search's
   simulated draws diverge from the executor's (Light Up exile shifting the draw
   order) or whether a staged **spell** (Skullcrack) is being counted past its
   expiry — the staged-expiry analog of the land fix, but for casts (a different
   code path than `TryPlaySpecificLand`).
3. Whatever the mechanism: confirm non-flood decks stay byte-identical, A/B burn for
   the aggregate, rebaseline via the accept flow.

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
