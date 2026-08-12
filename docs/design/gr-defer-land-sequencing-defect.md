# Deferred re-solve strands attackers by casting ahead of the land drop (RESOLVED)

> **RESOLVED 2026-08-12 — root cause was narrower than the title: the trick-class deferred
> continuation was UNSEARCHABLE (site-masked off), so the greedy re-solve's beater-tapping cast
> could never be declined.** See "Actual root cause" and "Fix" below; the original analysis is
> kept for the record. The land-first plan shape was in fact always enumerated.

## Symptom (found 2026-08-12, overnight rebaseline analysis)

13 mirrorwing overnight games run 1 turn slower since the Gold Rush same-turn-Treasure change
(e958539) and do NOT recover at 4x/16x budget or at depth 7 — so they are not budget churn and
not horizon-limited judgment. 6 of the 8 distinct games recover their old (faster) win turn
under `MTG_NO_DEFER_CANTRIP=1`, pinning the mechanism to the deferred site-3 re-solve path.

## Root cause (traced: mirrorwing s4004 gi118, d3 b10, old T4 vs new T5)

Turn 2, one Forest on board, Hierarch + Gold Rush + Forest in hand:

- OLD line: PLAY_LAND(Forest) -> cast Gold Rush paying {1}{G} from TWO lands -> Hierarch stays
  untapped -> attacks T2 (and T3). The chip damage makes the (identical) T4 go-off lethal. T4 win.
- NEW line: cast Gold Rush FIRST (breakpoint plan shape; the land drop lands in the deferred
  continuation AFTER the cast), so the payment solve sees only one land and taps Hierarch for
  the {1}. The dork is tapped -> no T2/T3 attacks -> the same T4 go-off is 1 damage short. T5 win.

Both engines cast the same cards on every turn; only the cast-vs-land ORDER and the resulting
attacker availability differ. Depth cannot fix it: the land-first-then-GR ordering appears not
to be reachable/scored in the breakpoint plan shape (d7 b160 still picks the stranded line), so
this is an ENUMERATION/SEQUENCING gap, not a mis-judgment. The attacker-reservation logic
(FindBestOwnAttacker) cannot save it either: at cast time the dork genuinely IS needed to pay --
because the land has not been played yet.

## Fix directions (next session)

1. Ensure EnumeratePlansWithLand still produces the LAND-FIRST variant of breakpoint-opening
   plans (cast enumerated on the post-land board, breakpoint deferral preserved after it), or
2. Make the deferred plan shape commit the land drop BEFORE the breakpoint-opening cast when
   the land is unconditionally playable (a land drop is never worse played earlier in a
   goldfish main phase).
Validate: the 8 games in logs/mwprof/persist_list.txt (6 should recover; gi331/gi227 are a
different residual -- defer-off does not recover them; classify separately), full suite +
held-out, then GT rebaseline picks up the recovered turns.

## Actual root cause (traced 2026-08-12, MTG_DUMP_EWINS_TURN=2 + a temp apply trace on gi118)

Both fix directions above rested on a wrong premise. The land-first plan IS enumerated --
`add_for_land("Forest")` emits `land=Forest, casts=[Gold Rush]`, and `ApplyPlanDirect` plays the
land before the casts. The EWINS dump shows it present at the T2 root and rolling out to **T6**,
i.e. MIS-SCORED (its true value, realised under `MTG_NO_DEFER_CANTRIP=1`, is T4). The chain:

1. In that plan's apply, Gold Rush is paid off the two Forests, leaving Hierarch (the would-be
   attacker) untapped -- so far exactly the old line.
2. The DEFERRED site-3 re-solve then runs a greedy `TurnSolver::Solve`, which casts Expedite.
   The only untapped tap-source is the Hierarch (a Treasure pays only via an explicit
   SacForMana action the greedy plan did not include), so the payment taps the beater. Attack
   stranded, chip damage lost, rollout scores T6.
3. The continuation that DECLINES the Expedite cast was unreachable at ANY depth/budget:
   the trick class (solo_target_trick with cast_draw / creates_treasures) was wired into
   breakpoint site 3 -- the plain-cantrip site, which `MTG_BP_SITES`' default masked OFF (an
   admitted quality prune, measured on Hinata's Ponder/Preordain). With the class unsearchable,
   `bp_searched_plan` never resolved and the wave walker opened no slots: the Gold Rush
   continuation was PERMANENTLY GREEDY. That -- not missing land-first enumeration -- is why
   d7 b160 could not recover the line.

So "cast-vs-land order" was a symptom: the defer-shaped plan (GR paid Forest+Hierarch, land in
the continuation) scored T5 and beat the mis-scored T6 land-first plan.

## Fix (2026-08-12): give the trick class its own searchable site

The trick class moved from site 3 to its OWN site **5**, default ON (`MTG_BP_SITES` default
0x17 -> 0x37), leaving the plain-cantrip prune (bit 3, the measured Hinata regressor) intact.
`ApplyPlanDirect` tracks which class armed the deferred re-solve (`deferred_trick_armed`; trick
class owns the breakpoint if both arm it). The searched-continuation + deferred-wave machinery
then covers the class: wave 0 fans W variants, the walker reaches every rank on budget.

Validated (d3 b10 single-game, seed base+gi):

| game        | old | broken | fixed |
|-------------|-----|--------|-------|
| s4004 gi118 | 4   | 5      | 4     |
| s4004 gi182 | 4   | 5      | 4     |
| s4004 gi211 | 6   | 7      | 6     |
| s4004 gi331 | 4   | 5      | 4  (defer-off did NOT recover this) |
| s6006 gi123 | 4   | 5      | 5 -> 4 at 4x (budget churn now; was structural) |
| s6006 gi145 | 5   | 6      | 5     |
| s6006 gi287 | 4   | 5      | 5 -> 4 at 16x (budget churn now; was structural) |
| s7007 gi227 | 5   | 6      | 5  (defer-off did NOT recover this) |

Smoke: all non-trick decks byte-identical (Hinata included); mirrorwing d3 5.2600 -> 5.2467,
d5 5.1467 -> 5.0933; 4 searched-slower games all classify `churn` (recover at 4x/16x).

## Status

RESOLVED pending GT rebaseline -- fix beats both the broken state and the defer-off hatch on
the validation list. The overnight accept 73be0a6 stood in the interim (net -0.2125, tail
bounded ~13 turns/5200 games).
