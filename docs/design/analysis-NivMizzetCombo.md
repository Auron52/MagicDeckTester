# Analysis ledger — NivMizzetCombo

Deck: `decks/NivMizzetCombo/NivMizzetCombo.cod` (U/R draw-trigger combo: 4 Niv-Mizzet, the Firemind,
2 Niv-Mizzet, Parun, 4 Curiosity, 4 Tandem Lookout; cantrips 4 Ponder, 4 Preordain, 2 Brainstorm,
2 Gitaxian Probe, 4 Remand; ramp 4 Izzet Signet, 1 Sol Ring, 4 Generator Servant; 21 lands incl.
3 Saprazzan Skerry, 2 Sandstone Needle, 4 Cascade Bluffs, 4 Spirebluff Canal, 1 Ferrous Lake,
1 Sulfur Falls, 6 Island). The user dropped the list into the primary checkout and asked for an
analysis on 2026-10-17.

Branch/worktree: `niv-analysis` @ `/workspaces/MagicDeckTester2/.claude/worktrees/niv` (cut from
origin `7d11c561e`). The decklist was COPIED here; the user's original stays in the primary checkout
untouched.

**The combo.** Either Niv + (Curiosity on that Niv, or Tandem Lookout paired with it) + any card draw
is a draw/ping loop: every draw deals 1 damage, every 1 damage to the opponent draws a card, until the
opponent is dead or the library is empty. Curiosity's draw is "may"; Lookout's is mandatory. Both
Nivs cost 6 (Firemind `{2}{U}{U}{R}{R}`, Parun `{U}{U}{U}{R}{R}{R}`), so mana colour fidelity is
load-bearing (Parun needs three of each colour; Generator Servant's `{C}{C}` pays only Firemind's
generic).

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells or blocks; opponent-facing clauses are inert, but
  self-binding clauses bind us. Model the full card; goldfish-inert deferrals are PROVISIONAL until
  the user signs off.
- No greedy / heuristic substitute inside the search window (user hard rule). Choices a card
  introduces are SEARCHED, provider-narrowed (restrict/order only), or collapsed by a stated sound
  dominance fold — and surfaced to the viewer.
- Cards are generic (cards.json is deck-agnostic).
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present,
  never adopt unilaterally.
- One heavy job (build or batch) at a time; total threads <= 24. Other sessions share this box.
- Agents: no `cd` in compound commands, no `rm` / `rm -rf`, no `timeout`.

## Stage 1 — coverage (2026-10-17)
Missing (8): Niv-Mizzet, the Firemind; Niv-Mizzet, Parun; Curiosity; Tandem Lookout; Generator
Servant; Brainstorm; Gitaxian Probe; Sulfur Falls.
Present (11): Remand, Island, Cascade Bluffs, Ponder, Izzet Signet, Sol Ring, Saprazzan Skerry,
Sandstone Needle, Preordain, Spirebluff Canal, Ferrous Lake. No sideboard.

Bracket notes reclassified for this deck:
- **Remand** (`goldfish_inert`): the note says "no legal target, never cast". Wrong in general —
  Remand can target your OWN spell (CR 115.1: any spell on the stack). In this deck a self-Remand is
  a 2-mana draw (one Niv ping), a cast trigger for Parun, and against an uncounterable Parun it is a
  pure cantrip (the counter fails, "Draw a card" still happens). Tier 3 gap, NOT an accepted
  deferral. Remand is also in Hinata2 and Unpredictable Cyclone, so the fix is a play change there.
- **Izzet Signet / Ferrous Lake** (`ramp_filter`, U+R "treated as wild"): the note was written for
  Hinata ("the deck's dedicated W sources cover Hinata's {W}"). Parun's `{U}{U}{U}{R}{R}{R}` makes
  the colour split load-bearing here — fidelity to be checked (Stage 2).
- **Ponder**: all-or-nothing reorder/shuffle with the `ponder_keep` plan variant; previously
  accepted, unchanged.
- **Spirebluff Canal**: fully modelled.

## Stage 2 — cards (in progress)

Engine survey (2026-10-17, three read-only agents):
- **No per-draw trigger exists**; draws happen at ~26 sites (`cards_drawn_this_turn +=`), several
  helpers. Non-combat damage carries no source. No Aura grants a triggered ability. Soulbond exists
  (Silverblade Paladin) with a searched pair-or-wait and a viewer prompt. Creature `{T}: draw` exists
  (`tap_draw_cost`). Every triggered "any target" damage in the engine goes to the opponent's face
  (Pashalik Mons / Omnath precedent).
- **No stack window**: every cast resolves before the next; nothing can target a spell. Self-Remand
  is modellable as one combined action (cast X + Remand X: both costs, both cast-time blocks, X
  returns to hand, Remand draws), the splice / same-plan trick-target shape.
- **Mana**: the shared payer is exact for Signet / Ferrous Lake (U+R). The FLAT affordability pool
  over-credits single-colour feeders (Island+Island+Signet "affords" three `{U}` spells; the payer
  then refuses) and under-credits Sol Ring feeding a Signet (C+U+R). Phyrexian mana and checklands
  already exist. Sac-for-mana exists but has no summoning-sickness gate and no spend rider.

Work split (2026-10-17 ~22:40 UTC), one worktree + branch each, integrated serially here:
- `niv-core`: shared draw-trigger hook + damage-to-opponent watcher; Niv-Mizzet, the Firemind;
  Niv-Mizzet, Parun; Curiosity; Tandem Lookout; same-turn reachability of trigger-drawn cards.
- `niv-remand`: self-targeted Remand (combined action, lossless target folds, viewer), incl. the
  can't-be-countered Parun cantrip case; revisit Hinata/Fluctuator "never cast" hooks.
- `niv-mana`: Generator Servant (tap-gated sac mana + haste-rider pool), Gitaxian Probe, Sulfur
  Falls, Brainstorm (searched put-back, viewer `put_back`), Signet/Sol Ring feed fidelity.

### Card table
| Card | Tier | Summary | Deferrals (status) |
|---|---|---|---|

## Open questions for the user (collected, never blocking)
- **Q1 (stack).** The AI skill asks that a stack-order-dependent card raise "does the stack need
  implementing first?". Self-targeted Remand is one. Default taken: no full stack — a combined
  "cast X, Remand X" action is faithful against a passive opponent (it never casts, so no other
  stack interaction exists). PROVISIONAL.
- **Q2 (Niv's "any target").** Always the opponent's face, the human is not asked (Pashalik /
  Omnath precedent; in a goldfish nothing else is worth hitting, and a 20-ping loop would be 20
  prompts). PROVISIONAL.
- **Q3 (Curiosity "you may draw").** Always drawn while the library is non-empty. PROVISIONAL.
- **Q4 (Remand is a play change for Hinata2).** Remand was never cast; self-Remand is legal, so
  Hinata2's play (and GT) will move. Tested as a play change (held-out A/B), not gated.

## Approved deferrals
