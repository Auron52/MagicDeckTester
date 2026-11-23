# bruna-ro handoff 2 (2026-11-23) -- 7a amendments + 7b ritual credit, pooled A/B running

Worktree /workspaces/MagicDeckTester/.claude/worktrees/bruna-ro, branch wip/bruna-rule-override (push ONLY this branch;
never phase-1-2-deck-analyzer -- the parent lands shared-branch work). Scratch: /workspaces/MagicDeckTester/build/scratch/claude_bruna_ro/.
Measurement record: docs/design/bruna-line-critical-levers.md (first A/B tables, slower-game replays).

## USER rulings (verbatim)
- "7a. Okay, that makes sense along with the amendment. How does this do in searched?"
- "7b. That seems fine, except I'm more interested in the impact on searched than d0."
- Both levers break standing rules, so the USER decides adoption. Report numbers; do NOT adopt or flip a default.
- "Claude usage is at 5%, so keep running until it runs out, but keep a Codex handoff current." (USER 12:35 2026-11-22)
- Memory (USER 11-23): rank play setting > other searched shapes >> d0; judge the average; d0 matters only via searched.

## Done (commits on wip/bruna-rule-override)
- ed186487e: doc + unit boards fixed (7a: Overgrowth host reserved during its own payment) + crit supply computed once
  per pip (byte-identical: 1,704 A/B jobs / 42,600 games, all digests equal). 5f70e7741: doc (Worldfire gi295 recovers).
- 0f84c9f7d: DELETED MTG_PAY_LINE_CRIT_HASTE_SAC_LAST (dead code: Generator Servant, sac_for_mana_amount 2, is never a
  PaySacSpendableNow candidate) and MTG_D0_HASTE_SAC_CHARGE (Niv d0 +60). Units 3898/3898; identity 1,704 jobs equal.
- (this commit) two NEW default-OFF measurement levers + unit subcase "7a second amendment" (bin8 units 50/50 Bruna):
  * MTG_PAY_LINE_CRIT_RESERVE_LAST (R): a noncreature source of the held tiers (rank 60..498: manland 60, storage 62,
    burst 63) does not win a pip over a crit land 7a moved to the 499 band (crit_excluded, reselect via crit_demote).
  * MTG_PAY_LINE_CRIT_RITUAL_CREDIT (T): in the crit test (7a AND 7b), when the spell being paid (PayingSpellCard) is a
    ritual, its ritual_floating_mana of ritual_float_color (any colour if empty) is credited against the line's LATER
    pips: need = max(0, later - credit) + nd_left.

## Root causes found
- Dragonstorm gi679 under 7a (5 -> 6 at every depth d1..d8 b0, also with MTG_NC_SEARCH=1): inside the T1 search, at
  T3-T5 nodes, Desperate Ritual's {1} generic pip: 7a flagged Sandstone Needle critical for the line's later {R}s and
  moved it to the 499 band -- behind Mercadian Bazaar's storage tier (62), so the Bazaar spent storage counters. That
  devalues the Bazaar-first opening; the search plays Needle T1, never suspends Lotus Bloom, kills T6 not T5. Two flaws:
  (1) the 499 band sits behind every held land, not just the dorks; (2) the crit test ignores that the ritual being
  paid makes RRR for those later pips. bin8 real games: a+R = 5, a+T = 5 (both shapes d3b10/d5b20); Jumbo gi1447 d0
  stays 4 with U+R and U+T (7a alone 7).
- Cyclone 7b searched +6 (gis 186, 359, 442, 529 at d3 and d5; all recover at d8 b0 = budget losses): the crit rule
  fires thousands of times per T1 search, mostly on Seething Song {2}{R} (Steam Vents held for the line's later {R},
  which the Song's RRRRR covers), also Xander's Lounge on a {2}{U}{U} cost. The changed payments alter which lines pay
  first, and the T1 land choice diverges. bin8 d3b10: b+T fixes gi186 (6->5) and gi359 (4->3); gi442 (6) and gi529 (4)
  NOT fixed -- remaining cause not yet identified (Xander's Lounge {2}{U}{U} firings are the next suspect; the crit
  diag used was a temp MTG_CRIT_DBG print, removed; re-add it to src/ai/ManaPayment.cpp near crit_hold to trace).

## RUNNING: chain4 (pooled A/B, PID 1540319, setsid)
- build/scratch/claude_bruna_ro/chain4.sh: waits on claude_box.lock (queued 16:23:50 UTC; the lock queue is long), then
  MTG_PGO=0 MTG_RSS_CAP_GB=40 bin8/mtg --batch ab2/m.json --threads nproc. 302,000 games, est ~50 cpu-h (~1.6 h wall @32).
- Manifest mk_ab2.py 1830000 2000 0 2000: searched 2000/shape, d0 2000. Arms: off; 7a decks (cyclone dragonstorm spirits
  jumbo bruna elementals selesnyav2 stompy) aUR, aUT, aURT (a = BEHIND_DORKS + UNTAPPER_LAST); 7b decks (cyclone
  mirrorwing niv[+2hg] spirits worldfire elementals) b, bT.
- Log build/scratch/claude_bruna_ro/chain4.log (last line DONE); output ab2/ab.out, ab2/wins/.
- Wait: `while kill -0 1540319 2>/dev/null; do sleep 60; done` (run_in_background) + 2-hourly CronCreate (durable:false)
  if it spans 12:30-20:00 UTC.

## Next, in order
1. After DONE: `python3 build/scratch/bad/cmp_arms.py ab2/wins ab2/ab.out <arm> off` for aUR aUT aURT b bT, plus bT vs b;
   aggregate with build/scratch/claude_bruna_ro/tools/summ.py <cmp.txt> ab2/ab.out. Report SEARCHED per deck first
   (play setting first), then d0, then CPU (MTG_PGO=0 both arms; this is also the re-measure after the supply hoist).
2. Every slower game: d8 b0 replay via build/scratch/claude_bruna_ro/d8/one.sh pattern (lane, scripts/box_lane.sh --slots 1);
   unrecovered -> MTG_NC_SEARCH=1. Only clear clairvoyance may stay.
3. Cyclone gi442/gi529 under bT: trace the remaining crit firings (Lounge {2}{U}{U}), propose a fix.
4. Update docs/design/bruna-line-critical-levers.md with the tables; commit; push wip/bruna-rule-override.
5. Open USER decisions: 7a (+ which amendment), 7b (+ ritual credit). Defaults stay OFF.
