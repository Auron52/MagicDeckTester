# Cycling / sac-draw as line-chain activations (`cycle=` / `sacdraw=`) — deferred

USER direction (2026-08-27, treasure_hunt s11 session): *"Cycling should just be handled like other
abilities, part of the chain"*, *"(and I can commit them in the same way)"*, *"It's weird that it
(and sacrifice draw) act differently than all of the other abilities."*

## Today's state (after the s11 fixes)

Digs are **standalone plans only** (`AppendHumanPlayDigPlans`): one plan per affordable source,
never carrying a land drop (a land-variant experiment was built and REVERTED the same day — a
re-prompted menu renumbers, the user picks by remembered index, and a variant silently spends the
drop; see the s11 rejection artifact). Interactively this works: each dig applies, the segment loop
re-prompts, and the human sequences land/digs/casts across picks. What is missing is the **line**
form: a human-assembled `land=X; cycle=Y; cast=Z` cannot be validated or committed as one chain the
way `equip=` / `jittemode=` / `taptoken=` lines can.

## Design (mirrors the existing verb architecture)

* **`cycle=<card name>`** — from-HAND alternative play, the `channel=`/`suspend=` class: a cycling
  land can also be *played* as the drop, so `land=` vs `cycle=` on the same name is exactly the
  ambiguity those verbs exist to remove.
* **`sacdraw=<land name>`** — battlefield activation naming the source (Fiery Islet), the
  `gyreturn=` class (a land, never castable).

Pieces (all have per-verb precedents in the same files):

1. `TurnSolver::LineSpec`: `std::vector<std::string> cycles, sac_draws;`
2. `ParseLineSpec` (main.cpp): two table rows.
3. `BoardActivationIllegalReason`: presence checks (hand + `cycling_cost`; untapped battlefield +
   `sacrifice_draw_cost`).
4. `CheckLine` matching: declared-vs-legacy split like every verb (sorted multisets; a `DigDraw`
   action matches `cycle=`/`sacdraw=` when declared). **Prerequisite**: `CollectActions` must
   enumerate `DigDraw` candidates into plans (human-play gated, next to the Jitte-modes block) so a
   combined plan exists to match — today digs never join the odometer.
5. Plan JSON encoding (main.cpp ~966): `DigDraw` gets `"activate": true, "verb": "cycle"/"sacdraw"`.
6. Viewer (index.html): hand-card action for cycling (the `suspend` badge pattern), board-click for
   the sac-draw land; chain labels.
7. Apply order: `ApplyPlanDirect` already executes `DigDraw` inside any plan's action list
   (s_human_play pass) — but verify the PASS ORDER honours the chain position relative to casts
   (payment and what the draw enables both depend on it). This is the risky part; the interactive
   re-prompt loop sidesteps it today.

## Also open from the same session

* **Anti-Lifegain s5/gi4 play-drift (T4→T5)**: the reference protocol gate's one red entry. Proven
  to be the `MTG_PREPAY_SHRINK` adoption (rebased in from the other machine): the sweep is clean
  under `MTG_PREPAY_SHRINK=0`. The shrink measured neutral-or-better in aggregate but breaks this
  one recorded human line's replay. Options: scope the shrink off under human play (the
  human-line-vs-AI-average precedent), deeper repair of the replay walk, or accept-and-re-save.
  **USER decision pending.**
* **Deck-version selector** (USER 2026-08-27): a separate viewer selector to play OLD or modified
  deck versions (e.g. `decks/Mirrorwing Dragon/v1-twinflame-anger/` — the archived list whose 24
  references exist), defaulting to the latest stable version; maturity/references/bench resolve
  per VERSION (the archived list's references must not count for the new list — today's
  `REFERENCE_DECK` ownership rule already enforces the counting half).
