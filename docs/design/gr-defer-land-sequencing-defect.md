# Deferred re-solve strands attackers by casting ahead of the land drop (open defect)

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

## Status

OPEN -- documented at discovery; the overnight accept 73be0a6 stands (net -0.2125; this class
is a small fixable tail inside a clearly net-positive change). The 13 games' cost is bounded
(~+13 turns over 5200 overnight games).
