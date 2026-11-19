# viewer-maze2 handoff 4 (V3, 2026-11-19)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/viewer-maze2`, branch `wip/viewer-maze2`. Rules as in handoff 3
(push wip/* only, references read-only, never restart the viewer, heavy lock, MTG_RSS_CAP_GB=20, no timeouts, no pgrep -f).

## Commits since handoff 3
- `05edf7ac2` cherry-pick of R1's e62b38895 (viewer_protocol_check: an effect-tapped source is not a payment tap).
- `251e42c81` the four ETB choosers (colour/type/keywords/creature type) wait for picks under --interactive (was exit 70).
  Fixes `scripts/ref_handoff.py` on Maze's End s6 (turn 3+): verified, hands off at T3 and wins T6; the old binary still fails.
  Side effect: the Maze replay now does 0 stateless fallbacks in 299 steps (was 19 in 220).
- `8963f50c3` the predecessor's handoff-only note.
- Build2 (= 251e42c81 tree) BUILD EXIT 0. `mtg-test -tc="*Maze*"`: 67/67 PASS (incl. the new CheckLine test).

## Validation (build2 binary)
1. s7 Amulet split: halves `cast=Amulet of Vigor` -> accept plan 6; `land=Simic Guildgate;cast=Burgeoning` at `1,0,4,-1,-1,6`
   -> accept plan 2. The combined landat line stays `illegal` (the JS route splits it). JS test passes.
2. s6 (`build/scratch/viewer-maze2/s6val.sh <line>`): the USER's queued `Scrying, act, act` -> illegal "{3} for Maze's End"
   (correct; it read legal_not_enumerated before). The REORDERED `act, act, Scrying` -> ALSO illegal. Root cause, not
   CheckLine's walk: it is payable only if Baldur's Gate's fed mode ({2},{T}: X of one colour, X=4 here) pays the second
   {3} and FLOATS 1 for Scrying. The per-step payer (SpellEffects.cpp scaled-mode branch, "reached only if the basic tap
   did not lead to a payment") takes the plain {C} tap whenever it suffices, so it never floats the surplus. The executor
   has the same limit, so grading it legal would let a line execute and break. Mana payment/tap order = F6's area: OPEN.
3. s5 off-main dialog: replay shows s5 "repaired (the dig the ref predates answered by default)". Expected.
4. seed-8 T5 @65536: `[valve] narrowed repeat Maze's End source 45: 21 -> 13 targets`, dropped_groups 0; 6 two-Maze plans
   (old 0). Cost @65536 on a loaded box: user CPU 10.1 -> 16.7 s, RSS 0.54 -> 0.90 GB (pay est 24,200 -> 63,616).
   @131072 (the USER's viewer setting) the frame FITS: menus byte-identical old vs new, cost unchanged (~20.7 s user).
5. Exploration + two lands: hand JSON carries `grants_land_drops: 1`; on s6 T2 the viewer's segments are
   `land=Sea Gate;cast=Exploration;cast=Exploration` (accept plan 0) then, after the Sea Gate colour pick,
   `land=Maze's End` (accept plan 0). (Clicking Exploration BEFORE the land still queues `landafter=` by click order
   -> illegal; that is the old rule.)

## Replay (strict, 11 Maze refs, primary cwd, `build/scratch/viewer-maze2/vpc_v3.log`)
5 ok, 4 repaired, 2 shuffle-dead (s2_gi1, s7_gi6), 0 enum-gap, VPC EXIT 0. s12345681 now ok (R1's checker fix).
The 2 shuffle-dead are UPSTREAM, not this branch: origin-head binary 2f67da724 (`build/scratch/viewer-maze2/mtg.origin2f67`)
gives the same 2 shuffle-dead; the primary viewer binary (e682d7942 base, recorded them) replays both. Frame-14 diff on s2:
the opponent's battlefield already differs (sea-integ-1118 opponent-land commits, e.g. 2ad8c3d41 opponent shuffle ordinal,
83d5587a7 front block). Once the Maze work lands on origin, these refs need the legacy treatment: R1/parent.

## Units / smoke
- Full units (build2): 3711/3711 PASS (`build/scratch/viewer-maze2/units3.log`).
- Smoke `--smoke` (coordinator's early run, USER-approved beside a d8 tail): 231 passed, 0 failed, audit 0 configs
  changed, 0 play-changed: NO GT key moved (`build/scratch/viewer-maze2/smoke3.log`).
- `node test/viewer_linebuild_check.js`: every section 0 FAIL.

## Open (not this branch's to fix)
- s6 act,act,Scrying: payer does not float Baldur's Gate fed surplus (F6 / mana payment).
- s2_gi1 + s7_gi6 shuffle-dead on any origin-based engine (upstream opponent-land changes vs the recording binary).
