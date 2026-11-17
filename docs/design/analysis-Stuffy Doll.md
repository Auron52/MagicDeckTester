# Analysis ledger — Stuffy Doll

Deck: `decks/Stuffy Doll/Stuffy Doll.cod` (Boros damage-reflection combo), 60 cards:
2 Stuffy Doll, 4 Nomads en-Kor, 1 Shaman en-Kor, 4 Mogg Maniac, 2 Boros Reckoner, 4 Screaming
Nemesis, 4 Spiteful Sliver, 4 Blasphemous Act, 4 Chain Reaction, 4 Guilty Conscience, 3 Loran's
Escape, 3 Mind Stone; lands 4 Forbidden Orchard, 4 Inspiring Vantage, 3 Sacred Foundry,
3 Boros Garrison, 3 Arid Mesa, 2 Plains, 2 Mountain. No sideboard.

The USER asked for the analysis on 2026-11-17 ("begin analysis of Stuffy Doll").

Branch/worktree: `stuffy-analysis` @ `.claude/worktrees/stuffy` (cut from origin
`phase-1-2-deck-analyzer` 18ab35ca8). The deck folder was COPIED from the primary checkout (untracked
there; original left in place). The primary's other uncommitted files belong to another session.

**The plan of the deck.** Every reflector turns damage dealt TO it into damage to the opponent:
Stuffy Doll (indestructible; to the chosen player), Mogg Maniac (target opponent), Boros Reckoner /
Screaming Nemesis (any (other) target), Spiteful Sliver (grants slivers the trigger; target player).
The sweepers hit them all at once: Blasphemous Act (13 to each creature, {1} cheaper per creature on
the battlefield, opponent's Forbidden Orchard Spirits included) and Chain Reaction (X = number of
creatures). Two reflectors under an Act = 26 >= 20. Guilty Conscience on Stuffy Doll is an
unbounded loop (Doll pings itself -> deals 1 to the player -> GC deals 1 to the Doll -> ...) that
ends only when the opponent dies; on an indestructible (Loran's Escape) Reckoner/Nemesis likewise.
Nomads / Shaman en-Kor redirect damage ({0}, any number of times) onto a reflector — e.g. an Act's 13
to Nomads moved onto the Doll — and Shaman's {1}{W} pulls a source's damage off another creature.

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells, attacks or blocks; opponent-facing clauses are inert
  (Nemesis's "can't gain life", hexproof vs an opponent), but self-binding clauses bind us. Model the
  full card; goldfish-inert deferrals are PROVISIONAL until the user signs off.
- **Our creatures DO get dealt damage in this deck** (our sweepers, Doll's self-ping, GC, redirects,
  reflector triggers aimed at our own creatures). Earlier "inert: nothing ever damages our
  creatures" deferrals (Boros Reckoner D1 in Minotaur's ledger) do not hold here.
- No greedy / heuristic substitute inside the search window (user hard rule). Choices a card
  introduces (reflection-trigger targets, en-Kor shield count/target, GC host, Loran target, Doll's
  chosen player, Act/Chain timing) are SEARCHED, provider-narrowed (restrict/order only), or collapsed
  by a stated SOUND dominance fold, and surfaced to the viewer.
- Cards are generic (cards.json is deck-agnostic); fix shared cards faithfully for every deck.
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present,
  never adopt unilaterally.
- One heavy job (build or batch) at a time; total threads <= `nproc`; no two concurrent worktree builds
  beyond two.
- Agents: no `cd` in compound commands, no `rm` / `rm -rf`, no `timeout`; build with `./build.sh`
  only; subagents do NOT push the shared branch; hand off at ~250K context (CLAUDE.md rule).

## Stage 1 — coverage (2026-11-17)
Missing (10): Stuffy Doll, Nomads en-Kor, Mogg Maniac, Blasphemous Act, Chain Reaction, Guilty
Conscience, Shaman en-Kor, Loran's Escape, Screaming Nemesis, Spiteful Sliver.
Present but with a gap for THIS deck: Boros Reckoner (vanilla 3/3; damage-reflection trigger deferred
as inert in Minotaur — not inert here; `{R/W}` first-strike activation still goldfish-inert).
Present, re-review: Forbidden Orchard (modelled as "tapped every turn -> one Spirit per Orchard per
turn at turn start"; here the Spirits feed Blasphemous Act's discount and Chain Reaction's X, so the
tap timing is load-bearing), Mind Stone, Arid Mesa (fetch hold modelled), shocks/fast lands/Karoo.
Scryfall JSONs: `build/scratch/stuffy/scry/` (primary build volume).

## Stage 2 — cards (in progress)
Design DONE 2026-11-17: `docs/design/damage-reflection-pipeline.md` (Opus architect). One shared
`src/core/DamageReflection.h` pipeline (shields -> aggregate per creature -> snapshot triggers ->
SBA -> LIFO trigger resolution; executor AND rollout call it), armed per deck via
`GameState::reflect_armed` so other decks stay byte-identical. Loops iterate for real under a 65,536
resolution cap + 64-step no-progress rule (proved unable to cut a lethal loop, life <= 30 / 2HG).
Implementation = §10 steps 1-11 (12 optional speed cert; 13/14 only on USER request), one fresh
implementer agent per step, serial.

| step | content | status |
|---|---|---|
| 1 | pipeline core, no cards | pending |
| 2 | Maniac, Reckoner, Nemesis, Act (per-creature cost reducer), Chain Reaction; target fold + in-window ABORT tripwire | pending |
| 3 | searched reflection-target axis (Incinerator precedent) | pending |
| 4 | Stuffy Doll (choose player, {T} self-ping) | pending |
| 5 | Guilty Conscience (+ combat damage hook, loops) | pending |
| 6 | Spiteful Sliver | pending |
| 7 | Loran's Escape | pending |
| 8 | Nomads en-Kor shields (sweeper rider) | pending |
| 9 | Shaman en-Kor {1}{W} pull | pending |
| 10 | StuffyDollProvider + ExtraLethalDamage | pending |
| 11 | second main (Q-U4) | pending |

Defaults taken while USER questions are open (see below): Orchard model kept; Loran's scry =
provider pick (existing engine-wide cast_scry practice); second main ON subject to measurement;
deferrals provisional.

## Open questions for the USER (surfaced, not blocking)
- Q-U1 PROVISIONAL deferrals: Boros Reckoner {R/W} first strike (no blockers -> outcome-inert);
  Loran's hexproof (inert); en-Kor shields offered only as a rider on our own sweeper and Shaman's
  source = our sweeper (other windows dominated, design §4.2/4.3); canonical simultaneous-trigger
  order (design §4.7 proof).
- Q-U2 Forbidden Orchard: keep turn-start Spirit model (default; equals the legal "tap every Orchard
  at main-1 start" line, never worse here) or build tap-time Spirits (moves Hinata/Creature Giving).
- Q-U3 Loran's Escape scry 1: provider pick like every other spell scry today (default), or searched?
  Note the provider pick is an in-window single pick — the NO-GREEDY rule may want it purged engine-wide.
- Q-U4 second main for this deck: recommend ON, measured before adoption.
- Q-U5 cast-order rules: USER-owned; none proposed, order is searched.
- Rules check before step 8: is an en-Kor shield consumed when its redirect target has left the
  battlefield? (default: not consumed — it simply doesn't apply).
- Side finding R7: two rollout-only kill paths (TurnSolver.cpp ~73041/~73148) ignore indestructible
  and skip death triggers — not on this deck's paths; separate fix.
