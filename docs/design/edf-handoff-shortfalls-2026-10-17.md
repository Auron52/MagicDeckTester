# EldraziDisplacerFlicker: the 7 SHORT hand-off frames, root-caused (2026-10-17)

`python3 scripts/ref_handoff_sweep.py --deck EldraziDisplacerFlicker` on HEAD src `8c08b8e7` reports 7 of
637 frames SHORT (the engine, handed the human's position, wins later than the human did). The same
7 as on 2026-10-16 (`edf-cast-order-2026-10-14.md`). Reference replay is clean (24/24 on their win turn,
`ref_bench` 4.208 vs human 4.542, 0/24 short), so these are the deck's only open reference gaps.

Corrected tally: **2 are a harness bug, 5 are real engine gaps.** None recovers at `--depth 8 --budget-ms 0`.

## 1. Harness bug -- the sweep replays at a different plan-space cap (claude_s6_gi5, 2 frames)

`test/viewer_protocol_check.py` resolves a reference's pick indices under
`MTG_VIEWER_PLAN_CAP_POSITIONS=8192` (`VALVE_POSITIONS`); `scripts/ref_handoff_sweep.py`
(`run_handoff`), `scripts/ref_handoff.py`'s walk and `hunt.Session` replay with the engine default
(65,536). The cap shapes only human-play menus, so the same index names a different plan: at s6_gi5 T4
frame 0, index 1152 is "Living Wish -> Azorius Chancery x2" under the cap (1188 plans) and a
Drake/Call/Training Grounds/Wish line without it (2008 plans). Every frame from T4 on is the wrong board
(the sweep logs "walked 70 picks without reaching a terminal").

With the cap set both frames win T4, the human's turn:
`python3 scripts/ref_handoff.py references/EldraziDisplacerFlicker/claude_s6_gi5.json --turn 4 --frame 1 --env MTG_VIEWER_PLAN_CAP_POSITIONS=8192 --log-dir <dir>`.

**Fix:** pass `vpc.VALVE_POSITIONS` as the cap in all three walkers. `claude_s8_gi7` (also "recovered by
pinning") may be affected the same way although none of its frames are SHORT. Also: `sweep.json`'s
`cmd` strings are not shell-safe (unquoted `|`, `;`, `'`, `*`).

## 2. Enumeration gap -- main 1 after the land drop offers no PASS (claude_s21_gi20 T3 #1; s13_gi12 T4 #1)

After the T3 land drop, the only main-1 plan is "cast Cloud of Faeries" (plus its folded dig twin); the
empty plan is never offered, so the engine casts Cloud, wastes its untap, and cannot afford Emiel's
blink on T4 (5 vs human 4). Handing off at post_main gives 4 (main 2 offers a pass); T3 frame 0 (land
drop still available) gives 4. Still 5 at b200 and at d8/b0.

Where: `TurnSolver.cpp` `EnumeratePlansWithLandUncached`, the no-land-drop branch (~line 58456) never
appends the empty plan; `FSLineWin`'s `MTG_FS_IDLE_NODE` (~65761) adds one only for an EMPTY list. The
gap reaches every main-1 re-solve after the land drop, including the executor's post-draw re-solve.

**Fix:** always append the empty plan on the no-land-drop path (as main 2 does). Changes budgeted play
on every deck -> held-out A/B. Unexplained: Cloud + Emiel together at s21 T3 looks legal (and wins T4)
but is not offered either.

## 3. COMBO OFF route -- the bank step strands the blink's {C} (claude_s13_gi12 T5 #1)

Human T5: Yavimaya Coast, Mariposa draw (Displacer), Cloud, Displacer, loop -> dig -> Living Wish ->
Essence Depleter. The route's trial digs to Living Wish with 4 floating (needs 5), and its bank step pays
the blink by tapping Brushland -- the last untapped {C} land -- for G plus BBB off its Fertile Grounds;
then `pay {2}{C} FAILED ... lands available=0` ("blink unpayable"). Untap choice is always Mariposa +
Brushland, never the Coast. `--turn 5 --frame 1 --env MTG_EDF_COMBO_ROUTE_TRACE=1`; frame 2 (Cloud
already cast by the human) wins T5.

Where: `DecisionProviders.cpp` ~34186-34215, the bank step into `Blink`. Same family as Open 2 in
`edf-pae-followups-2026-10-11.md` (the route's untap choice strands colour / {C}).
**Fix:** reserve the outlet's {C} before paying colour demand, or keep a second {C} land in the untap set.

## 4. The post-draw continuation is not searched (claude_s13_gi12 T5 #0, T4 post_main #0)

The engine passes T4 correctly, then on T5 takes "Coast: Cloud + Mariposa draw" (tail 6). The human's
"Coast: Mariposa draw" is offered (tail 9), and the route trial on its post-draw board WINS, but no
breakpoint continuation is searched after the Mariposa/Clue draw, so that kill never scores; EDF does not
search main 2 (USER ruling, single main phase). Repro: `--turn 5 --frame 0 --env MTG_FS_ROOT_DUMP=5
--env MTG_EDF_COMBO_ROUTE_TRACE=1 --env MTG_FSW_TRACE=1 --env MTG_FSW_TURN=5`.

**Fix direction:** make the Mariposa / Clue draw a searched breakpoint whose continuation includes the
route. Related, unexplained: with `MTG_EDF_HAND_GOFF_REFUND=1` the Displacer+Cloud hand go-off sizes at
80 iterations and passes the subset check, yet never reaches the root list.

Scratch (build volume, regenerable): `build/scratch/edf_short/`, `build/scratch/edf_refs/`.
