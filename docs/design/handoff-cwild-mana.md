# READ FIRST (4th agent, 2026-11-04 ~05:00Z; older blocks below are history)
M3 (`floating_prepay_c`, EDF only) BUILT and MEASURED (`build/scratch/cwild/bin/mtg.gcf3`). EDF passes the bar (A/B 0 changed); one
Hinata regression game (d5 s3003 gi27) has no d8 b0 verdict yet -- see the regression bullet.
- Units 2459/2459 (`build/scratch/cwild/unit_gcf3.log`).
- Repros on gcf3: EDF smoke d0 gi316 T8 digest a65219d356826269 and gi486 T5 d3ef027cfc0efc9d (= old, recovered);
  Slivers d0 gi190 T4 a6d8339d92f9878e (= old); Melira d0 gi738 T6 fc22d16b822e2c38 (= GT); Niv d0 301/436 unchanged.
- Held-out A/B (seeds 87300000+gi; new arm = gcf3 one batch `run6/new.json`; old arms = mtg.c03b MTG_PREPAY_C_AS_C=false,
  EDF std from run3, EDF 2HG `run6/old.json`; report `run6cmp/report.txt`): EDF std 1500 games 0 changed, digest
  17e8af0f3d3fc471 BYTE-IDENTICAL to old, units -0.00%; EDF 2HG 1500 games 0 changed (avg 4.0753 both), units +0.23%.
  Every other deck's code path is unchanged by M3 (cpip_deck gate), so run5 (de5e21099) stands for them.
- Smoke on gcf3 (`build/scratch/cwild/smoke_gcf3.log`, wins `smoke_gcf3_wins/`): 152 pass / 13 fail (same 13 rows as
  de5e21099). Per-game vs GT: slivers d0 610,706 6->5 (faster); hinata d0 gi46 8->loss and d5 gi59 4->5 (both known
  illegal-payment losses); edf d0 gi534 6->5 (faster); EDF d0 gi316/gi486 no longer move. Every other failing row is
  play-changed with identical per-game outcomes. Auras / Prevent Damage / SelesnyaLifegain do not move in smoke.
- instructions:u (200 games each, same seeds as old perf, `perf/new.*.perf`): EDF -0.12%, Slivers 2HG +0.10%,
  Hinata -1.50%, Niv +0.06%, Slivers +1.60% -- of which 97% is gi151 alone (old 1.92e9 -> new 2.40e9: the illegal
  old line won T4, the legal line plays to T5), i.e. +0.05% excluding that game.
- Scoped regression (`--deck=slivers,slivers2hg,hinata,hinata2hg,melira,melira2hg,edf,edf2hg,niv,niv2hg`,
  `build/scratch/cwild/reg_gcf3.log`, wins `reg_gcf3_wins/`): 16 pass / 14 fail. Moved rows: slivers d0/d3/d5 s2002,
  d5 s3003; hinata d0/d3/d5 s2002, d5 s3003; hinata2hg d5 s2002; edf d0 s2002 (same per-game outcomes); niv d0/d5
  s2002, d5 s3003; niv2hg d5 s2002. All melira rows and every edf searched row PASS. References: 0 drift.
  Per-game slower (11 searched + 1 d0), repros `build/scratch/cwild/regslow/` (old = mtg.c03b MTG_PREPAY_C_AS_C=false
  MTG_CWILD_TRACE=1 serial `old1.err`; new + d8 b0 = gcf3 `new.err`; divergence via `classify.py regslow/cls`):
  CONCLUSIVE ILLEGAL (diverge exactly at the turn where the old line paid {U}{U} with both Sol Ring {C}, laundered =
  whole pre-load wild): hinata d5 s2002 gi54, gi66; hinata2hg d5 s2002 gi54; hinata d5 s3003 gi51, gi68, gi82.
  RECOVER at d8 b0 (old turn): hinata d5 s3003 gi6 (4), gi64 (6), gi100 (4); niv d5 s3003 gi65 (5); hinata d0 s2002
  gi751 (5; also illegal at T5). OPEN: hinata d5 s3003 gi27 (old T8 win, new loss, no ILLEGAL trace -- the trace misses
  out-of-TapForCost payments); d8 b0 still RUNNING (pid in `regslow/new.pid`); at d5 b0 (`--ignore-play-profile`) BOTH arms lose it (new `g27.err`, old `g27old.err`), so the unbudgeted d5 search does not separate the arms. The new arm at the play depth with 10x budget (d5 b300) WINS T8 = old turn, and a hand count says the old T8 line is payable legally (Mountain's {R} and the Orchard's unit through the two Cascade Bluffs give the {U}s the old line took from Sol Ring), so this is budget churn, not a capability loss. Faster: hinata d5 s3003 gi95 6->5;
  slivers d0 207/404/656/754.
NOT pushed, GT NOT accepted. Base is 33ddea2fd (origin has moved on: rebase + rebuild + smoke before landing).
Commits squashed (backup of the pre-squash tip: branch `backup/c-wild-pre-squash-4` = 703e7e7f4).

## (history) 3rd agent handoff READ FIRST (2026-11-04 ~04:10Z)
Branch `fix/c-wild` (local only; NOT pushed; GT NOT accepted). Commits on 64e6d8844:
- `de5e21099` MEASURED: (M1) the shared payer lets generic take floating {C} before `wild`
  (`GenericCFirstScope`/`t_generic_c_first`, `src/core/SpellEffects.h` after HoldColorlessScope ~1506; drain block
  before `drain(cost.generic, reserve.wild)` in SpendFloatingTowardCost ~38540; set in `src/ai/ManaPayment.cpp`
  TapForCostSharedOnceImpl ~1613 creature reserve / ~1645 general float). Off in decks with a {C}-pip cost
  (new deck constant `GameState::deck_spends_c_pip`, `src/core/GameState.h` offset 701, stamped in
  `src/runner/GoldFishRunner.cpp` before the Jorn stamp; only EDF has it). Never the Generator Servant haste-tagged
  {C}. Skipped under the drain's `hold_c` test. Fixes Slivers smoke d0 gi190 (T4, digest a6d8339d92f9878e = old).
  (M2) `pure_c_tapped` (`src/ai/TurnSolver.cpp` ~56623) keeps a {C} `colorless` unless its source makes ALL FIVE
  colours: a Caves of Koilos {C} booked `wild` paid Birds' {G} once M1 reordered the drain (Melira smoke d0 gi738).
- WIP commit (this one) UNBUILT/UNTESTED when handed off: (M3) EDF regression from M2. Smoke on de5e21099 had EDF d0
  gi316 8->loss and gi486 5->6: Brushland / Yavimaya Coast {C} now booked `colorless`, EDF keeps wild-first, so
  generic ate the Fertile Ground any-colour units and Emiel's {W}{W} was refused (old line was legal by
  attribution). Fix: `GameState::floating_prepay_c` + `_stamp` (uint8 x2, offsets 702/703, size 1240 unchanged):
  the prepay's kept {C} (set at the end of BatchPrepayMainCasts, `prepay_c_kept`), honoured only in the stamped
  turn+phase (`PrepayKeptC`/`SetPrepayKeptC`, SpellEffects.h); in EDF the payer drains ONLY those units first
  (base order exactly: they used to be `wild`); restored on payment failure; mixed value-gated into the main-phase
  key (~87010). Unit test added. Build was RUNNING at handoff (background, log
  `build/scratch/cwild_build7.log`, via buildq; the binary lands in `build/wt/c-wild/Release/`).
MEASURED on de5e21099 (`build/scratch/cwild/bin/mtg.gcf2`; run5 new arms vs run3 OLD arms, report
`build/scratch/cwild/run5cmp/report.txt`, 18,000 games):
| deck | n | changed | slower | faster | net | units% |
|---|---|---|---|---|---|---|
| Slivers | 3000 | 2 | 2 | 0 | +2 | +0.46 |
| Slivers 2HG | 3000 | 0 | 0 | 0 | 0 | -0.01 |
| EDF | 1500 | 0 | 0 | 0 | 0 | -0.06 |
| Hinata2 | 1500 | 34 | 34 | 0 | +34 | +0.54 |
| Niv | 1500 | 4 | 2 | 2 | 0 | -0.03 |
| Melira | 1500 | 0 | 0 | 0 | 0 | -0.04 |
| Dragons/Fungus/Goblins/Mirrorwing | 1500 each | 0 | 0 | 0 | 0 | ~0 (gob/drg/mwd/fun byte-identical) |
Slower dispositions: Slivers 151, 499 = old line paid colour pips with Mutavault's {C} (illegal). Niv 426, 585 =
conclusive illegal (run3 set). Hinata 34 = the 33 disposed in run3 (31 conclusive illegal, 627 illegal by hand,
38 recovers d8b0) + 403 (old flagged ILLEGAL at T7, new recovers at d8b0 T7: `slow403/`). Hinata's digest is
identical to run4 (64e6d8844), i.e. M1 does not move Hinata. Hinata +34 / Slivers +2 are entirely illegal-line losses.
Smoke on de5e21099 (`build/scratch/cwild/smoke_gcf2.log`): 152 pass / 13 fail (adds melira_d5 play-changed);
searched slower only hinata d5 gi59 (illegal, known); d0 slower hinata gi46 (illegal) + EDF gi316, gi486 (M3 above).
Units on de5e21099 2458/2458 (`unit_gcf2.log`).
instructions:u: OLD arms done (`build/scratch/cwild/perf/old.{slv,slv2,edf,hin,niv}.perf`, 200 games each,
mtg.c03b with MTG_PREPAY_C_AS_C=false); NEW arms NOT run (`perf/new.*.json`; run with `perf stat -e
instructions:u -x, -o ... <bin> --batch perf/new.X.json --threads 8`).
NEXT: (1) build done -> snapshot to `bin/mtg.gcf3`, units, then EDF d0 316/486 repro (`edf_d0/m.json`, expect
final2 digests d3ef027cfc0efc9d / a65219d356826269) and gi190/d0c repros. (2) EDF A/B new arm 1500 (other decks
should be byte-identical to gcf2: check digests in smoke). (3) Smoke on HEAD. (4) perf new arms. (5) Smoke did not
cover Auras / Prevent Damage / SelesnyaLifegain beyond smoke sizes: they hold partial-colour {C} lands (M2) --
if smoke moves them, A/B them. Task 3 (surplus COLOURED booked `wild`) is NOT implemented: see the OPEN USER
QUESTION section below.

## (history) 3rd agent mid-run READ FIRST (3rd agent, 2026-11-04 ~02:40Z; the 2nd agent block follows as history)
HEAD = `de5e21099` on `fix/c-wild` (local only; NOT pushed; GT NOT accepted). On top of the 2nd agent's
`64e6d8844` (24f626be4 physical-{C} booking, lever deleted, c03be714c reverted):
`de5e21099` = the gi190 fix + the partial-colour {C} fix:
- MECHANISM 1 (gi190, Slivers smoke d0): with Mutavault's {C} booked `colorless`, the wild-first generic drain spent
  the Cavern unit on Predatory's generic and stranded the {C}, so the replicate {G} was refused. FIX: the shared
  payer (`src/ai/ManaPayment.cpp` TapForCostSharedOnceImpl, the creature-reserve and general-float spends ~1613 /
  ~1641) opens `GenericCFirstScope` (`src/core/SpellEffects.h`, after HoldColorlessScope ~1506); the drain
  (`SpendFloatingTowardCost`, block before `drain(cost.generic, reserve.wild)` ~38520) spends up to
  `t_generic_c_first` {C} on generic first. Cap = 0 when `GameState::deck_spends_c_pip` (new deck constant,
  `src/core/GameState.h` after deck_has_discard_scry_watcher, offset 701, size unchanged; stamped in
  `src/runner/GoldFishRunner.cpp` before the Jorn stamp; EDF is the only deck with it: USER "EXCEPT for EDF"),
  else colorless minus Generator Servant's haste-tagged units (c03be714c's Niv gi436/301 loss). Skipped while a
  {C} pip holds colourless (same test as the drain's `hold_c`).
- MECHANISM 2 (exposed by 1; Melira smoke d0 gi738 T5): the prepay booked a Caves of Koilos {C} ({W}/{B}/{C})
  as `wild` ("source has a colour mode"), and that wild paid Birds of Paradise's {G}. FIX: `pure_c_tapped`
  (`src/ai/TurnSolver.cpp` ~56621) keeps a {C} `colorless` unless its source produces ALL FIVE colours (Cavern /
  Hive / Territory / Courtyard still ride `wild`). Partial-colour decks: Auras, EDF, Melira, Prevent Damage,
  SelesnyaLifegain (Brushland, painlands, Caves, Llanowar Wastes, Battlefield Forge, Karplusan Forest).
- Repros on `build/scratch/cwild/bin/mtg.gcf2` (= HEAD): gi190 T4 digest a6d8339d92f9878e (= old); niv d0 436/301
  and melira d0 738 = the 24f6 digests (`d0c/`). Units 2458/2458 (`unit_gcf2.log`).
- run4 (64e6d8844 = mtg.final2, vs run3 OLD arms): slv 3000 +2 (151, 499 illegal), edf 0, hin +34 (the 33
  disposed + 403: old flagged ILLEGAL at T7, recovers at d8b0 T7, `slow403/`), niv 0 (426, 585 illegal; 2 faster).
  Report `run4cmp/report.txt`.
RUNNING: run5 = all 9 decks + Melira new arms on mtg.gcf2 (`build/scratch/cwild/run5/`, PID 2882277 in `run5/pid`,
7 threads). Old arms = run3 (Melira: `run5/m_old_mel.json` on mtg.c03b, not yet run).
NEXT: run5 table (`run5cmp/`), classify new slower games (d8b0 on mtg.gcf2; MTG_NC_SEARCH=1 for clairvoyance),
smoke on HEAD, perf instructions:u new arms (`perf/new.*.json`; old arms done in `perf/old.*.perf`).

## (history) 2nd agent READ FIRST (3rd agent starts here; 2nd agent handed off 2026-11-04 ~00:55Z at the context cap)
HEAD = `a4628a933` on `fix/c-wild` (local only; not pushed; GT not accepted). Commits on 33ddea2fd:
`24f626be4` fix (physical {C} stays `colorless` in the prepay pre-load; TurnSolver.cpp `pure_c_tapped` ~56621,
`c_must_stay` ~56643, pre-load ~57900-57935, stage_pool ~58035, Aura probe ~57588) -> `c03be714c` WIP drain change
-> `eb600d44b` doc -> `6df95b46d` lever `MTG_PREPAY_C_AS_C` + `MTG_CWILD_TRACE` DELETED -> `a4628a933` c03b REVERTED
(SpellEffects.h now identical to base; reason in "Results on c03be714c" below: haste-tag theft).
Binaries (build volume): `build/scratch/cwild/bin/mtg.final2` = HEAD (lever-free, no c03b; byte-equal to the 24f6
flag-on arm on every repro checked); `mtg.c03b` (has the flag, =false is base-equivalent: use it for OLD arms and
MTG_CWILD_TRACE); `g46/mtg.24f6`. Units on HEAD: 2454/2454.
RUNNING: final A/B new arms `run4/` (PID in `run4/pid`, 2781176; manifest `run4/m.json` = new.slv 3000, new.edf 1500,
new.hin 1500, new.niv 1500 with mtg.final2). Compare against run3's OLD arms (`run3/ab.err`, same seeds) with
`ab_report.py`-style per-game diff (old from run3, new from run4); run2's new arms are the 24f6 result for the other
5 decks (all 0 changed except slv +2 illegal). Waiter: `while kill -0 2781176`.
SMOKE on HEAD done (`build/scratch/cwild/smoke_final2.log`): 153 pass / 12 fail. Searched slower = hinata d5 gi59
(illegal, see below). d0 slower: hinata gi46 (illegal), slivers gi190 4->5 (OPEN legal-line loss: wild-first drain
spends the Cavern unit on a generic and strands Mutavault's {C}; refusing the replicate {G}). Faster: slivers d0
610, 706. Play-changed only: slivers d5, hinata2hg d5, edf2hg d5, edf d0/d3/d5, niv d3/d5, niv2hg d5.
NEXT: (1) when run4 lands, per-deck table + classify every slower game: Hinata/Niv slower sets should be SUBSETS of
the run3 sets already disposed (`slowhin/`, `slowniv/`, classify.py; d8b0 manifests in g38/, slowniv/d8.json);
any NEW slower game -> classify.py with mtg.c03b old arm + d8b0 with mtg.final2. (2) instructions:u per moving
deck: `perf stat -e instructions:u` works; old = mtg.c03b with flags MTG_PREPAY_C_AS_C=false, new = mtg.final2, ~200
games per deck (slv, slv2, edf, hin, niv). (3) Report to the coordinator: commits, table, dispositions, smoke moves,
open items (gi190; colour-funded generic below). Do not push or accept GT.

# Handoff: a physical {C} paying a coloured pip (c-wild), 2026-11-03

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/c-wild`, branch `fix/c-wild` (off origin
`phase-1-2-deck-analyzer` 33ddea2fd). NOT pushed, GT NOT accepted (the user lands it).

## Mechanism (confirmed live on origin)
- `TurnSolver::BatchPrepayMainCasts` true-colours pre-load (`src/ai/TurnSolver.cpp` ~57850-57930, see the
  "A PHYSICAL {C} NEVER BECOMES `wild`" comment at ~57902) funded the bill's generic colourless-first and
  booked it as `pool.wild = combined.generic`. Twin: mixed-batch `stage_pool` (~58044); staged-Aura probe
  (`pre.wild += sc.generic`, ~57580).
- `wild` pays any colour pip: `SpendFloatingTowardCost` step 2 (`src/core/SpellEffects.h` ~38460).
- Out-of-bill spends hit it: executor replicate loop `src/ai/AIEngine.cpp:10270` (`rep_gate` ->
  `TapForCost`), rollout twin `src/ai/TurnSolver.cpp:62803`. Both drain the same pre-load, so the fix is in
  the prepay (one place).
- Repro Slivers 2HG gi191 seed 61000191: Mutavault {C} pays a replicate copy's {W}. Old digest
  8da1c3ff0aa2a1a6 (T4) -> fixed T5.

## Commits
1. `24f626be4` fix: generic-funding {C} from a PHYSICALLY colourless source (no colour mode: Mutavault,
   Sol Ring, Wastes -- `pure_c_tapped` lambda ~56621, `c_must_stay`) stays `colorless`; a {C} from a
   flexible source (Sliver Hive, Cavern, Secluded Courtyard, Unclaimed Territory) keeps riding `wild`
   (any-colour mana, colour committed at spend; a first cut that kept those as {C} lost legal lines:
   Slivers 87301106, 2HG 87300294/87301008). Big-spell move pins the kept {C}. Lever: heurarm
   `MTG_PREPAY_C_AS_C` (default ON, =0 old) + reader `PrepayCAsCOn()` in `src/ai/EngineFlags.h`.
   Diagnostic `MTG_CWILD_TRACE=1` (AIEngine::TapForCost + prepay): prints `[cwild] ... ILLEGAL` when a
   wild unit pays a colour pip after a prepay that laundered physical {C} (counts only physical {C}).
   Only instruments AIEngine::TapForCost (misses other payment paths, e.g. d0 runner).
   Unit test `test/unit/test_prepay_c_not_wild.cpp` (5 cases incl. =0 reproducing the illegal 3rd {W}
   and a Sliver Hive {C} still paying {W}). Full unit suite 2455/2455 on this commit.
2. `c03be714c` WIP, BUILT BUT UNTESTED: in `SpendFloatingTowardCost` (`SpellEffects.h` ~38488) generic
   pips drain `colorless` BEFORE `wild` when no {C} pip holds colourless (`hold_c_pre`), gated on
   `PrepayCAsCOn()`. Reason: with {C} booked as colorless, wild-first spent the flexible unit on a generic
   and stranded the {C} (Slivers smoke d0 gi190 T4: Predatory replicate {G} from a Cavern unit lost).
   This changes drains for ANY float holding both C and wild (Hinata rituals + Sol Ring etc.) -> must be
   measured.

## Measurements so far (on 24f626be4, i.e. WITHOUT c03be714c)
Pooled held-out A/B, seeds 87300000+gi, game_index gi, shipped play settings, one batch, both arms via the
per-job flag. Scratch: `/workspaces/MagicDeckTester/build/scratch/cwild/` (`mk_manifest.py`,
`ab_report.py <err> <out>`, results of that run in `run2/ab.{out,err}`; first-cut run in `run1/`).
| deck | n | changed | slower | faster | net | units% |
|---|---|---|---|---|---|---|
| Slivers std | 3000 | 2 | 2 | 0 | +2 | +0.46 |
| Slivers 2HG | 3000 | 0 | 0 | 0 | 0 | -0.00 |
| Dragons / EDF / Fungus / Goblins / Mirrorwing | 1500 each | 0 | 0 | 0 | 0 | ~0 |
Slower 151, 499 (std): both old lines paid a coloured pip with Mutavault's physical {C} (MTG_CWILD_TRACE;
151 T3 wild was 100% physical {C} on a {W}; 499 T3 both wild units paid {R} pips, one was the {C}) ->
legitimate losses. Old-arm digests are byte-identical across runs (determinism OK).

Smoke (`THREADS=8 bash test/regression.sh --smoke` on 24f626be4): 153 pass / 12 fail (slivers d0/d5,
hinata d0/d5/2hg, edf d0/d3/d5/2hg, niv d3/d5/2hg). CONTROL: the 12 failing jobs re-run from the smoke
manifest with `MTG_PREPAY_C_AS_C=false` reproduce GT exactly (`smoke_ctl.out`), flag-on reproduces the
smoke digests (`smoke_on.out`) -> every move is caused by this change. Searched slower: hinata_smoke_d5
gi59 4->5 = old line paid {U}{U} with 2 physical {C} (Sol Ring) -> illegal-payment, legit. d0 slower:
slivers_d0 gi190 4->5 (legal line lost via wild-first drain -> motivated c03be714c), hinata_d0 gi46 8->loss
= ILLEGAL-PAYMENT loss (2nd agent, 22:10Z; traces `build/scratch/cwild/g46/tr/*.json`, `smk_d0.err`): T8 bill
Expressive Iteration {U}{R} + Ponder {U} + Irencrag Feat {1}{R}{R}{R} off 6 lands + Sol Ring ({C}{C}); Remand {U}
(cast on its own Feat, OUT of the bill) found float {C:1, wild:1} -- both Sol Ring units -- and the old line paid
its {U} with one of them, leaving Forbidden Orchard + Ornithopter for Reality Spasm's {U}{U} (X=9 untap) -> win.
Count: U5 R4 colour pips before the first Spasm untaps, 8 colour-capable sources + Sol Ring: unpayable legally.
New line taps the Orchard for Remand's {U}, Spasm is unfunded -> no win. (MTG_CWILD_TRACE did not flag it: the
response cast pays outside AIEngine::TapForCost.) Repro manifests: `smk_slow.json`, `smk_d0.json` (traces in `*.err`).

## Second agent (22:05Z ->)
- `c03be714c` built; prepay units 17/17, full unit suite 2455/2455 (binary snapshot
  `build/scratch/cwild/bin/mtg.c03b`). Smoke slower games re-run on it (`smk_slow3.{out,err}`): slivers d0 gi190
  now byte-identical to the old arm (digest a6d8339d92f9878e, T4) -> recovered; hinata d0 gi46 and d5 gi59 unchanged
  (both illegal-payment losses).
- `6df95b46d`: the lever `MTG_PREPAY_C_AS_C` and the `MTG_CWILD_TRACE` diagnostic are DELETED (both fixes always on;
  the trace only measured the =0 arm). Unit test drops the =0 case, adds the drain-order case. Rebuild:
  `build/scratch/cwild_build4.log`.
- Pooled A/B on mtg.c03b (flag arms; flag-off == GT per the smoke control): `build/scratch/cwild/run3/ab.{out,err}`.
  Old run2 (24f626be4 only) is the attribution baseline for c03be714c on the 7 non-Hinata/Niv decks.

### Results on c03be714c (run3, 33,000 games) and why it was REVERTED (`a4628a933`)
| deck | n | changed | slower | faster | net | units% |
|---|---|---|---|---|---|---|
| Slivers std | 3000 | 2 | 2 | 0 | +2 | +0.46 |
| Slivers 2HG | 3000 | 0 | 0 | 0 | 0 | -0.01 |
| EDF | 1500 | 2 | 0 | 2 | -4 | -0.46 |
| Hinata2 | 1500 | 33 | 33 | 0 | +33 | +0.71 |
| NivMizzetCombo | 1500 | 9 | 4 | 5 | -1 | -0.69 |
| Dragons / Fungus / Goblins / Mirrorwing | 1500 each | 0 | 0 | 0 | 0 | ~0 |
c03b attribution (run3 new vs run2 new, 7 decks): only EDF moved (541, 1441 faster). Hinata slower (`slowhin/`,
`classify.py`): 31 CONCLUSIVE illegal (old arm [cwild] colour-from-wild > flexible wild), gi627 illegal by hand
(T4 main 2: U4 R1 before the first Spasm untap vs 4 colour units: Mountain, Monastery, Island, Cascade Bluffs +1),
gi38 recovers at d8 b0 (T7). Niv slower (`slowniv/`): 426, 585 conclusive illegal; 507 recovers at d8 b0; 1385 does
NOT (new d8b0 T5, old d8b0 T4; NC both T6 = clairvoyant) and is CAUSED by c03b (24f6 binary: T4 at play and d8b0).
Smoke on the c03b final: d0 slower niv 301 (8->loss), 436 (6->8), melira 738 (6->7) -- all c03b (24f6 keeps GT).
MECHANISM (exec-tap, `d0c/`): Niv gi436 T3 float u1 r1 c2(Servant, haste-tagged) wild1; Tandem Lookout {2}{U} took
one tagged {C} (TakeHasteFloatForCreatureSpell) and then c03b paid its generic with the SECOND tagged {C} instead of
the wild, so the second Generator Servant was cast without haste. A refined c03b would have to drain {C}-first only
for the units the prepay used to launder (and never haste-tagged units) -- not attempted.

## OPEN USER QUESTION (NOT implemented, by instruction): surplus COLOURED mana booked as `wild`
**What it is.** The same class as the {C} bug, one level up. The prepay (`BatchPrepayMainCasts`, the true-colours
pre-load, `src/ai/TurnSolver.cpp` ~57880-57935, and its twins `stage_pool` ~58035 and the Aura probe ~57588) funds
the bill's GENERIC pips from the largest surplus colour and books that funding as `wild`. `wild` pays ANY colour pip
(`SpendFloatingTowardCost` step 2, `src/core/SpellEffects.h` ~38490). Inside the bill this is harmless (the bill's
own casts drain generic wild-first). But a spend OUTSIDE the bill -- a replicate copy (executor
`src/ai/AIEngine.cpp` ~10270, rollout `TurnSolver.cpp` ~62803) or a response cast (Remand, Memory Lapse) -- can
then pay e.g. a {W} pip with a FOREST's {G}. Example shape (the gi191 shape with a Forest for the Mutavault):
bill = Plated Sliver {W} + Sinew Sliver {1}{W} off Plains, Plains, Forest -> pool W:2 wild:1 (the Forest's {G}
funding Sinew's {1}). Plated pays a W; Hatchery's replicate copies ({W} each, NOT in the bill) take the other W
and then the `wild` -- the Forest's {G} paying {W}. CR 106.1b: illegal.
**What it is NOT.** A unit from a source that could have made the needed colour (a dual, Cavern of Souls, Sliver
Hive, Unclaimed Territory, a Mystic Monastery) is legitimately any-of-its-colours mana committed at spend (USER: no
per-colour branching), so `wild` is right for it. Only the funding from a source that could NOT have made the
colour the out-of-bill spend took is illegal -- the same physical-source subtlety as Mutavault vs Sliver Hive.
**Why it is a USER question and not a quiet fix.** (1) Frequency is unmeasured: no audit exists yet. (2) Fixing it
changes play in every deck that prepays a multi-cast bill (far more than the {C} fix moved). (3) The fix is not
local:
  a. Booking the funding in its TRUE colour breaks the bill: generic drains colours in WUBRG order (step 3), so an
     earlier cast's generic would eat a later cast's PINNED colour. Wild-first is what protects the pins; a true-
     colour booking needs a bill-aware generic order (the remaining bill exported as a hold the drain reads, like
     `g_line_unpaid_cost`), maintained per cast by BOTH the executor and the rollout.
  b. A `wild_gen` subset count ("wild that may pay generic only", the `floating_haste_c` pattern) is lossy -- the
     unit really is green, so an out-of-bill {G} may legally take it -- and ManaPool equality/hash/AddPool/Clear
     plus ~140 `.wild` sites would have to maintain or clamp it.
  c. Legality is per funding SOURCE (could this land have made {W}?), so it needs a per-colour `pure_c_tapped`
     analogue.
**Suggested route if the USER wants it:** first an audit (count out-of-bill colour pips paid from `wild` when the
pre-load's colour-funded generic exceeds the units whose sources could make that colour), then, if it fires, (a)
true colours + exported bill hold, A/B'd exactly like this fix (held-out, every slower game illegal or recovering
at d8 b0).

## Running / pending
- Build of c03be714c: background task, log `/workspaces/MagicDeckTester/build/scratch/cwild_build3.log`
  (via buildq). Check `tail` for `>>> done`.
- No batch running. `ab.json` is regenerated with Hinata2 + NivMizzetCombo added (18 jobs, 33,000 games).

## Next steps
1. When the build is done: `./build/Release/mtg-test -tc="prepay:*"` then full `./build/Release/mtg-test`.
2. Re-run `smk_slow.json` with `MTG_CWILD_TRACE=1 MTG_DUMP_WINS=1` and check slivers d0 gi190 / hinata d0
   gi46 / hinata d5 gi59 under the new build.
3. Pooled A/B: `cd <worktree> && MTG_DUMP_WINS=1 MTG_RSS_CAP_GB=43 ./build/Release/mtg --batch
   build/scratch/cwild/ab.json --threads 8 > ab.out 2> ab.err` (background, wait on PID), then
   `python3 ab_report.py ab.err ab.out` (add `hin`/`niv`; the script reads tags from the err file).
   If c03be714c moves decks beyond Slivers, report per deck changed/slower/faster/net/units.
4. Each slower game: re-run the `old` arm single-game with `MTG_CWILD_TRACE=1` (+`MTG_EXEC_TAP_TRACE=1`);
   an ILLEGAL line where the laundered physical {C} >= colour pips paid from wild (or wild was 100% physical)
   = legitimate loss. Otherwise run the new arm at `"depth": 8, "budget_ms": 0, "ignore_play_profile": true`
   (the profile's value_play refuses a bare depth) and require recovery to the old turn.
5. If c03be714c is net worse or leaves unrecoverable slower games, drop it (git revert) and report the
   d0 gi190 legal-line loss as the open item instead.
6. `THREADS=8 bash test/regression.sh --smoke`; classify searched slower games the same way. Do not accept GT.
7. Squash/clean commits as wanted (local only), report: bug live on origin (yes), mechanism, fix, test, A/B,
   smoke, hash. Open item to report: colour-FUNDED generic is still booked `wild` (a Forest's surplus {G}
   funding the generic could pay an out-of-bill {W}); same class, not fixed here.
