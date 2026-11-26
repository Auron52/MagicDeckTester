# bpput-cost handoff (2026-11-26, the first agent stopped at its context limit)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/bpput-cost`, branch `wip/bpput-cost`, off
origin/phase-1-2-deck-analyzer 3f91b7d4a. Nothing is pushed. The design and the proof are in
`docs/design/bpput-inert-land-fold.md`.

## Commits
- `ce189ca01`: the inert-land fold, MTG_BPPUT_CENSUS and the unit tests (committed WIP, before validation).
- The next commit (made alongside this file): the test_elves.cpp call sites, measurements and this handoff.

## What the fold is
`CastPutACardPermanent(..., land_put_credited)` skips a put basic land that nothing can read. Such a land is:
- a non-snow basic, non-creature land,
- whose definition has no reader key,
- and which is TAPPED, or untapped off a cast that carries the land-put mana credit.

That holds when `LandPutReaderLive` finds no reader in any zone:
- hand, graveyard, staged, sideboard and exile;
- every permanent on the battlefield, on both sides.

Readers are derived at load from the JSON param KEYS against a reviewed whitelist
(`CardDefinition::bpput_land_reader` / `bpput_mana_reader`, CardDatabase.cpp `BpPutLandReaderFromJson`). An unknown
key counts as a reader. The mark half is `PlanLandPutFoldsInert`, in PlanOpensBreakpoint's land clause.

Lockstep: the rollout reads the `cast_put_credit` rider, and the executor reads `rdb_bf_credited`, set in
cast_by_name and cast_alt, and false for graveyard casts.

## Measurements so far
- **Census** (seed 9700001; cg 100, cg2hg 50, melira 100, landfall 100, hydras 100 games):
  `logs/bpput/census_base.txt` (base) and `logs/bpput/census_base_vs_new.txt` (armings base vs new).

  | Deck | Source of the put-only armings |
  |---|---|
  | creature_giving | Crop Rotation, 100% untapped (uncredited) lands |
  | melira | Chord of Calling, 100% creatures |
  | hydras | Nature's Lore 369K, Search for Tomorrow 141K, Three Visits 109K; all untapped basics |
  | landfall | Rampant Growth 511K (all tapped basics), Search for Tomorrow 506K untapped, Wave 236K, Titan 138K tapped nonbasics |

  Continuations are non-empty in about 40-60% of the recording applies, so the continuations do real work.
- **Units** (`logs/bpput/units_table.txt`; seed 9800001, the suite d5 row shape, base `build/scratch/bpput/mtg.base`
  vs new `mtg.new`):

  | Deck | Games | Units new/base | Identical digests |
  |---|---|---|---|
  | creature_giving | 400 | 1.000 | 40/40 |
  | creature_giving2hg | 200 | 1.000 | 20/20 |
  | landfall | 400 | 1.000 | 40/40 |
  | melira | 300 | 1.000 | 30/30 |
  | hydras | 400 | 0.997 | 39/40 (same avg) |

  **The fold is sound but almost never fires on these decks.** A reader is nearly always live:
  * Landfall: Evolving Wilds / Khalni / Courser / Avenger, and nonbasic lands in the library.
  * Hydras: Vernal Bloom / Gauntlet / Nissa / Search for Tomorrow's suspend in hand.
  * Creature Giving and Melira: their puts are new mana and new creatures, which the fold must not touch.
- **Units:** the plain suite passes, 4301/4301 (`build/scratch/bpput/unit_plain.log`). Under MTG_UNPRUNED=1 /
  MTG_HUMAN_PLAY=1 the test_land_search + test_elves subset fails exactly the same set as the base (9 / 13). No new
  failures; the new tests pass in both modes (`build/scratch/bpput/sub_*`). The base was the elves-int build,
  approximate.

## Running job
`chain2.sh` (PID 2303453, child of chain3.sh PID 2298366) is running `bash test/regression.sh --smoke` under the
heavy lock. Its output goes to `build/scratch/bpput/smoke.log`. Its markers: `build/scratch/bpput/chain2.status` gets
"smoke rc=..." and then `chain2.DONE` is touched. Leave it running. NO accept.

## Remaining, in order
1. Read smoke.log when chain2.DONE appears. If keys moved (possible only for hydras, by the freed variants), list
   them, and classify every slower searched game at `--depth 8 --budget-ms 0`, new vs `mtg.base`, with
   `bash scripts/box_lane.sh --slots 2 -- ...` and cwd = this worktree.
2. Write `logs/bpput/report.md` covering the census, the fold and its proof, the units table, the smoke result, and
   what was left out. "What was left out" is in the design doc's last section, plus: land-tap bonuses, suspend,
   fetch activations, uncredited untapped lands, creatures and snow.
3. Commit, then push `wip/bpput-cost` only.
4. Possible next lever, NOT implemented and not proven:
   * treat a land-tap bonus as covered for the credited case (LandPutCrossCredit);
   * treat a fetch land's {T}/life/sacrifice activation as non-reading.

   Either would widen the yield on hydras and landfall, but each needs its own proof.

## Rulings (verbatim, from the brief)
- "collapse wasted search unconditionally", but only as a SOUND identity fold (the folded continuation must provably
  reach the same set of states/options as the base plan).
- A budget may re-price work but never make a line unfindable at unlimited budget.
- No lossy restriction without an A/B and the USER's adoption.
- "do nothing" stays an option at every root.
- No greedy pick inside the search window.

Box rules: one heavy process at a time under `flock /workspaces/MagicDeckTester/build/heavy.lock`; MTG_RSS_CAP_GB=20;
MTG_BUILD_JOBS=14; never cmake; never timeout; never accept GT; never push phase-1-2-deck-analyzer; never touch
references/.

## UPDATE: smoke finished
`bash test/regression.sh --smoke` (no accept) on the fold binary: **251 passed, 0 failed, 0 new; scenarios 678/678** --
digest-identical to committed GT, so no slower game to classify. Step 1 of "Remaining" is DONE; left: report.md + push.
