# fea3a2c (rituals-before-payoff CastOrderRank): regression slowdown classification

2026-07-21. `fea3a2c` (DragonstormProvider::CastOrderRank — rituals rank 15, Irencrag 18, before the
payoff at 20) is committed and **net strongly positive** on the loss-penalized avg (regression d3
s2002 6.107→5.557, s3003 6.220→5.600; searched 631 faster / 30 slower). The committed **regression**
GT is still the pre-`fea3a2c` baseline, so a run FAILs dragonstorm and the audit lists 30 searched
slowdowns. **NOT rebaselined** — the real same-draws slowdowns below are the "significantly slower"
games the user wants to review first.

## classify_turn_later (each slower game re-run at 4x + 16x its case budget)

**~17 churn** (recovers at higher budget → benign d3=10ms search truncation): gi2, gi118, gi126,
gi150, gi172, gi180, gi187, gi261, gi266, gi287(s2002), gi9, gi64, gi79, gi115, gi181, gi223, gi257,
gi135(d5). Several are big at suite budget but fully recover (gi79 3→8→3 at 4x; gi266 4→8→4).

**~13 PERSISTS** (no recovery at 16x → shuffle-variance if the deck shuffles, else a same-draws real
slowdown). Cross-referenced with `explain_game` (kept-hand/draws divergence):
- **Same-draws REAL slowdowns** (draws identical → not variance; a genuinely worse line the ordering
  produced): gi20 6→loss, gi22 4→6 (s2002); and the persist set on s3003 (gi20 4→5, gi49 5→6, gi50
  6→7, gi110 6→7, gi132 8→loss, gi287 7→loss, gi288 7→8, gi188 5→6). These are the ones to root-cause.
- Mechanism (from explain_game): on some combo turns the new order casts a **mana rock (Ruby
  Medallion, GenericProvider rank 5) or durdles instead of going for the kill** — e.g. gi2 (pre-churn)
  cast Ruby Medallion on T4 instead of Pyretic+Seething+Dragonstorm; gi22 does nothing on T4 and
  combos on T6 with the same draws. The rock-first / rituals-before-payoff ordering is generally good
  but occasionally spends the combo turn on setup at tight d3 budget.

## Decision needed (user)
1. **Accept the net trade** and rebaseline regression GT (`bash test/regression.sh --accept`) with the
   ~9 same-draws real slowdowns recorded as intended (`--accept-with-regressions="..."`) — justified by
   the strong net avg improvement (the linear loss-penalized metric favors it) — OR
2. **Refine `DragonstormProvider::CastOrderRank`** so it doesn't durdle on a lethal/combo turn (e.g.
   don't front-load a mana rock when the payoff is already affordable this turn), then re-measure. The
   win-now/lethal check should fire the payoff before setup on a go-off turn.

Overnight GT is untouched (also pre-`fea3a2c`); rebaseline it together with whichever path is chosen.
