# Snow: where did the every-turn second main's extra gain come from?

Status: **deferred, partly answered** (2026-10-04). A real main-1 defect is localised to one repro; see
"Measured 2026-10-04" at the end.

## Background

Snow plays the MDFC Jorn, God of Winter // Kaldring, the Rimestaff as **Jorn** (USER 2026-10-03).
Jorn's attack trigger untaps every snow permanent, so the deck needs a post-combat main on the
turns Jorn attacks. Two shapes were measured on all Snow tier cases (4,330 games), against the
3de66a1f baseline:

| arm | searched units | avg win turn, searched cells |
|---|---:|---|
| Skred never cast (`NeverCast`), no Jorn | 0.85x | ≈ baseline (±0.02) |
| Jorn, main 2 on **every** turn (`DeckUsesSecondMain`) | 1.66x | 0.05–0.23 better |
| Jorn, main 2 **only on Jorn turns** (`UntapSecondMainLive`) — shipped | 1.30x | 0.03–0.20 better |

The shipped arm keeps most of the gain. The remainder — the every-turn arm is a few hundredths of
a turn better in most cells — comes from playing a main 2 on turns Jorn did **not** attack.

Caveat on the every-turn row: it was measured on a binary with the untap-timing bug (attackers were
tapped by `ResolveCombatDamage` after Jorn's untap), so its Jorn turns were slightly *under*-served.
The gap is therefore, if anything, understated.

## The open question

What does a non-Jorn main 2 buy a deck with no post-combat effects? Every Snow card is at least as
good cast in main 1 (snow permanents feed the Treefolk before combat), so a main-2 cast should be
weakly dominated — unless main 1 **could not** make the play. Candidates:

1. A card main 1 could not see: something found or drawn mid-turn after the main-1 search committed.
2. A tie broken the wrong way. The first suspect, s2002 gi56 T4, turned out to be this and harmless:
   the root searched "cast the Scrying Sheets find (Arcum's Astrolabe) now" and the base line tied
   on win turn (both verified T5), so the base line won the tie. Not a gap.
3. Budget: at d3/d5 b10 the main-1 search may truncate before reaching a line that a cheap main 2
   then picks up.

Only (1) or (3) would be a real defect; (2) is a tie-break preference.

## Method (about 30 minutes)

1. Run Snow's three tiers (`--deck=snow`) on both binaries, keeping each run's
   `test/logs/<mode>/wins` (they are overwritten by the next run — copy them out).
   Every-turn arm: rebuild d08c2c6f (Jorn + `DeckUsesSecondMain`), ideally with the untap-timing fix
   from 6fb10c80 cherry-picked so the arms differ only in the main-2 gate.
2. Diff per game; take the games faster **only** in the every-turn arm.
3. For a few of them: `MTG_FD_TRACE=1 MTG_BP_TRACE=1 MTG_BP_CONT_TRACE=<turn>` on both binaries,
   find the non-Jorn turn whose main 2 acted, and classify it as (1), (2) or (3).
4. Re-run those games at `--depth 8 --budget-ms 0` on the shipped binary: if they reach the
   every-turn arm's turn there, it is budget (3), not expressiveness.

## Measured 2026-10-04

Both arms in ONE pooled batch on f5ba78cc (every-turn = the per-job flag
`"flags": {"MTG_FORCE_USES_M2": true}`, which restores the deck-wide second main on the same
binary -- so the arms differ ONLY in the main-2 gate, both with the untap-timing fix):

* Control: the shipped (per-turn) arm reproduced Snow's accepted GT exactly, 0 / 4,330 per-game
  mismatches, inside a 24-worker pooled batch.
* Every-turn vs per-turn, 3,600 searched games: net **-15** (21 faster, 6 slower), **1.33x** units.
* The 15 distinct faster games re-run at `--depth 8 --budget-ms 0` on the shipped binary:
  * **10 reach the every-turn result** -> budget churn, not a gap (smoke s1001 gi12/gi78, regression
    s2002 gi8/gi15, s3003 gi10, overnight s5005 gi25/gi149, s6006 gi10/gi143, s7007 gi38).
  * **4 do NOT** -> a real gap: overnight s5005 gi147 (d8b0 8 vs 7), s4004 gi110 (7 vs 6),
    s4004 gi28 (7 vs 6), s6006 gi33 (7 vs 6).
  * **smoke s1001 gi2 is an anomaly of its own**: d3/b10 per-turn wins T7, but d8b0 on the same
    binary gives **T8** -- an unlimited search doing worse than a budgeted one. Investigate separately.

### The gi147 repro (cheapest: 0.4 s at d8b0)

`logs/m2q/g147.json`-style manifest: deck Snow, seed 5152, game_index 147, depth 8, budget_ms 0,
ignore_play_profile; second job identical plus `"flags": {"MTG_FORCE_USES_M2": true}`.

* Every-turn arm commits at T1 a verified **T7** line whose T6 is:
  m1 `Ice-Fang Coatl | bp[k4:Scrying Sheets]` (Coatl's ETB draws Scrying Sheets; the continuation
  plays it), m2 `Scrying Sheets(x1)` (the look; it finds Frost Augur, cast T7).
* Per-turn arm commits at T1 a verified **T8** line; its T6 is an empty phase (`land=(defer)`), it
  attacks for 5 instead of 7, and holds Coatl.
* The main-1-only equivalent IS offered: inside the T1 search, the T6 site-10 continuation list for
  Coatl's draw contains `#0 land=Scrying Sheets: Scrying Sheets(x1)` (play the drawn Sheets and look
  in the same continuation), and follow-on lists show the look's find (Frost Augur). Mana suffices:
  Coatl on Forest+Island, the look's {1}{S} on Island+Mountain, Sheets' own {T}.

So the T7 line is EXPRESSIBLE in main 1 and an exhaustive search still settles on T8. Something on the
main-1 path drops or misprices that variant -- candidates, in the order to check: (a) the site-8 look
NESTED inside a site-10 continuation resolving narrow / EMPTY at scoring time (the apply's
`window_base` branch), so the find never reaches hand in the scored world; (b) a dominance or
dedup collapse merging the look variant with the no-look one; (c) order-condemnation.

Next step: `MTG_BP_TRACE=1 MTG_BP_CONT_TRACE=6` on the per-turn job, find the T6 node that applies
continuation #0 and dump its post-apply hand (does Frost Augur arrive?) and its scored win turn.
