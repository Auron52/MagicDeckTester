# Human play: apply the queued action order AS-IS (casts and board activations interleaved)

2026-09-10, EldraziDisplacerFlicker. USER report, verbatim:

> "the order is off for Emiel activations and Kitchen activations. This means that I cannot draw
> and then untap the kitchen without going to an extra breakpoint..."
> "Ideally the order I provide would be followed as-is."
> "It's up to me to make sure the order is correct."

Shipped in this repo as `MTG_HUMAN_LINE_ORDER` (default **ON**, `=0` restores the previous
cast-only pin). This doc records the confirmed mechanism, the shape of the fix, and the one piece
deliberately **left undone**.

---

## The mechanism, as measured (not inferred)

Three separate layers each discarded the human's order; only the third one mattered.

1. **`tools/play/linebuild.js`'s `encodeLine` groups tokens by verb** — every `cast=` first, then
   `vial=`/`retrace=`, then the other verbs. So `[crack the Clue, blink Emiel]` and
   `[blink Emiel, crack the Clue]` encode to the *same string*. This turns out not to matter,
   because:
2. **`TurnSolver::CheckLine` matches a line to a plan by MULTISET**, not by sequence. Confirmed at
   seed 1 / game 0 / turn 6: `cast=Clue Token;blink=Emiel the Blessed@10*1` and
   `blink=Emiel the Blessed@10*1;cast=Clue Token` both return `accept` on **plan 164**.
3. **...and plan 164's action vector is `[Emiel the Blessed, Clue Token]`, which is the only order
   the enumerator ever emits.** A census of all 184 plans at that decision found
   `(Mariposa, Emiel)`, `(Conservatory, Emiel)`, `(Emiel, Clue Token)` and never a reverse — the
   activations come out in **battlefield-index order**, i.e. the order those permanents happened to
   enter play. A fact about the past, with no bearing on the turn.

The apply then honours that vector: `ApplyPlanDirect` casts the hand spells (in `CastOrderLess`
order, or in vector order for a `searched_order` plan) and afterwards runs
`apply_trailing_activations(plan.actions)`, a single pass in vector order. `--cast-order` — the
side channel the viewer already used to pin the human's order across the two processes — only ever
carried **hand cast** names (`Plan::searched_order`), so activations were never in the pin at all.

**Where the extra breakpoint came from.** With `(Emiel, Clue)` the only expressible order, "crack
the Clue to DRAW, then blink to UNTAP the land" required committing `cast=Clue Token` alone, taking
the commit-line re-prompt, and committing `blink=Emiel...` as a second line. That re-prompt is the
"extra breakpoint". Nothing was stopping mid-line — the deck has no draw-engine card
(`is_draw_engine` / `stage_draw_break` are false for all 26 of its cards), and the perm-ability
draws (Mariposa's `{5},{T}` and the Clue's sac-draw) truncate nothing. The order alone forced the
split.

*(Kitchen and Conservatory are mechanically the same card here — `{4},{T}: Investigate`, param
`tap_investigate_cost` — so the reproduction on Conservatory/Clue is the reported case exactly.)*

## The fix

* **`LineSpec` is untouched.** The order does not travel in the line string, because the viewer
  validates in one process and commits by plan *index* in another. It travels in the existing
  `--cast-order` side channel, which already survives that hop **and** is already recorded into
  saved references (`cast_order` on the main-phase trace entry) and reconstructed by
  `viewer_protocol_check.py`'s `side_channel_args`.
* **A leading `"*"` marker** widens that channel from "the cast order" to "the whole line's order".
  No MTG card name is `*`, so it cannot collide; every reference saved before 2026-09-10 carries a
  cast-only list with no marker and takes the old branch verbatim. A **marker**, not an inferred
  "did this list happen to cover every slot?" test — inference is exactly how an old cast-only list
  would get silently promoted and shuffle a saved game's activations.
* **`AIEngine::ReorderPlanCasts`** widens its reorderable slot set from "non-sac hand casts" to
  "non-sac hand casts + every board activation" (`TurnSolver::IsTrailingActivation`) and sets
  `Plan::human_action_order` when an activation is actually in the sequence.
* **`ApplyPlanDirect`** then walks `actions` ONCE in vector order, dispatching each board
  activation inline where it stands through the *same* trailing dispatcher (a one-action slice —
  never a second implementation of any activation), and skips the trailing pass. The top-level
  `apply_plan_actions(plan.actions, ...)` call moved below the dispatcher's definition so the
  ordered cast loop can reach it; that is a pure code move (a lambda definition executes nothing).
* **`tools/play/index.html`** pins the full queued sequence whenever the queued entries and the
  matched plan's `actions` are the same multiset; otherwise it falls back to the historical
  cast-only pin. A plan carrying an action the line does not name (an implicit `SacForMana`, a Vial
  deploy) therefore keeps exactly the old behaviour — the queued order says nothing about those.

Kinds that keep their canonical pass and are **not** sequenceable: the land drop, `ActivateVial`,
`SacForMana`, `Suspend`, `DigDraw` (a cycle commits its own line), sac-land casts,
`CastFromGraveyard` (retrace), `DiscardToLandsEdge`. Each runs in a loop of its own, before or
after the interleaved walk, and the viewer has no affordance to place them against the rest.

`MTG_LINE_ORDER_TRACE=1` prints the realised sequence (real applies only, never a rollout) — the
order is otherwise unobservable from outside the process, which is how a first cut of this work
read the search's last scored candidate as though it were the played line.

## DEFERRED: a dropped board ACTIVATION is silent in the viewer

Server-truth resolution says a declared action that cannot be executed is **dropped and reported**.
The drop half holds for activations and always has: every branch of the trailing dispatcher checks
its precondition and its payment *first* and no-ops without mutating anything ("stranded-outlet
safe"). The **report** half does not: `g_play_dropped_cast_sink` is fed from `apply_one` only, so
the viewer's `dropped_casts` channel covers hand casts and nothing else.

This gap predates the order work — the trailing pass could already strand an activation behind a
cast that spent the mana — but honouring the human's order makes it easier to reach on purpose,
since the human can now place an activation before the mana that would have paid for it.

**Why it is not fixed here, and what a fix needs.** The cheap proxies do not work:

* *"count `TapForCostDirect` failures around the action"* — `ApplyBlinkLoop` and
  `SpendRepeatActivations` **terminate on a failed payment by design** (the blink loop is
  self-funding and runs until it cannot pay), so a perfectly successful multi-activation would
  report as a drop.
* *"diff a cheap state fingerprint"* — misses the kinds whose only effect is a flag or a counter
  (`AnimateLand`'s `is_animated`, `JitteModeAbility`'s counters).

An exact fix needs a success flag returned from each of the twenty branches in
`apply_trailing_activations` (and its `AIEngine` twin), pushed into `g_play_dropped_cast_sink` on
failure. That is mechanical but touches every activation kind and wants its own reference sweep,
so it is its own change. Shipping a heuristic version would put false red "dropped" banners in
front of the player, which is worse than the current silence.
