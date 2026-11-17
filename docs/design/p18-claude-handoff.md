# P18 sampled gate: Claude handoff (2026-11-17)

## Objective and status

The user wants an opt-in replacement that **consistently beats the bare heuristic rollout ladder** on the repository's decks. It does not need to become the default. Do not stop at a few favorable decks or a speed result that sacrifices win turns. Work through the remaining failures and measure a new candidate on fresh paired seeds across all decks.

The current implementation is `value_play.sampled_gate=2` in `src/ai/TurnSolver.cpp` (around line 108217 for the sampler and 117521 for ladder integration). Mode 2 is shipped as an option at shared commit `6a21bedd2`; Boars opts in via `decks/Boars/Boars.value.json`. The shared Windows CI for that commit passed. The full design history and 56-deck measurements are in `docs/design/sampled-gate-ladder.md`. **No source changes were made during this handoff turn.**

This file is in the persistent `.claude/worktrees/p18-ship` checkout, branch `p18-ship`. The primary checkout is a separate local history at `e4f2b7c71` (six commits ahead of its old base, 147 behind `origin/phase-1-2-deck-analyzer`, with an untracked `build.lock`). Do not reset or pull it casually. The `p18-ship` checkout was clean before this document was added. Its `build` symlink points to `/workspaces/MagicDeckTester/build/wt/p18-ship`; `build/Release/mtg` exists. Verify branch tips and process state on resume.

## Measured result against the bare ladder

F2: 56 decks x 400 paired games, depth 5, 20 ms, max turn 8, with identical deck/profile inputs and **no value leaf on either arm**. Mode 2 charged less thread CPU in 48/56 decks, but the savings are often 1–3%. Changed win turns:

| Deck | Seed (`--game-index`) | Bare -> mode 2 |
|---|---:|---:|
| Dragonstorm | 5120149 (149) | T6 -> T7 |
| Gods | 5210038 (38) | T6 -> T7 |
| Gods | 5210051 (51) | T6 -> T8 |
| Gods | 5210058 (58) | T8 -> unwon |
| Gods | 5210124 (124) | T7 -> T8 |
| Gods | 5210250 (250) | T6 -> T7 |
| Gods | 5210312 (312) | T8 -> T5 |
| Gods | 5210336 (336) | T5 -> T7 |
| Kithkin | 5250100 (100) | T4 -> T5 |
| Merfolk Turns | 5340317 (317) | T6 -> T7 |

Thus Gods is 1 earlier / 6 later; the other three decks have one later each. F5: another 1,000 paired Mill games found seed **8010197** (`--game-index 197`) going T5 -> T6. Its first decision pre-refused pass 5 on a cold price, while the bare ladder reached pass 5 after a T5 pass-4 result and spent only 11 units there. Mill stays on the bare ladder. Boars had 1,800 paired games over F2/F4/F5 with identical win turns and 1.3–1.7% deterministic work savings, so it was adopted.

F2 inputs and old wins are in `/workspaces/MagicDeckTester/build/scratch/p18f/F2/{manifest.json,v/,wins/}`. F5 inputs are in the adjacent `F5/` directory. Those ignored scratch outputs may be absent on another machine; reconstruct diagnostic directories from committed `decks/<Deck>/` by symlinking the `.cod`, `.profile.json`, `.buckets.json`, and keepmodel sidecars into two arm directories. The sampled arm alone gets `<Deck>.value.json` with `{"value_play":{"sampled_gate":2}}`. Use the same seed **and game_index** in paired jobs. Batch syntax is `build/Release/mtg --batch <manifest.json> --threads "$(nproc)"`; use `./build.sh` for all rebuilds, never raw CMake. Run both arms in one pooled batch for comparable CPU, and use deterministic charged units to confirm the result.

## Likely next experiment

The current mode 2 sets `b1`, `b2`, `b3`, `b3r=3`, `lcap`, `rr`, `rx2`, `fr=2`, `a1=.95`, `fd`, `zn`, `hx`, `hw=3`, `wn=3`, and `z=0` in `p18::Knobs()`. At the decision loop around line 117730, `kn.hx` can pre-refuse a pass **before** the `kn.fd && k == depth` branch. Thus the documented “terminal pass is run once, with no priced verdict” still allows an extrapolated *pre-refusal* at depth 5. This is a concrete candidate for the Mill loss. First trace the 11 later games with `MTG_P16_TRACE=1`; compare the ordinary ladder's pass costs and the sampled gate's `phx`/priced decisions. Then test moving the terminal `fd` branch before `hx`, or a narrower rule motivated by those traces. That change can increase runtime when the ordinary ladder correctly refuses the last pass, so measure it rather than assuming a win.

Gods and the other losses may have different causes (including earlier pass refusal and cache warming). The P18 report records many failed variations, including conservative R36, which matched win turns on one 17-deck held-out set but saved essentially no CPU and did not establish the fleet bar. Read that history before rerunning an old arm. The user ruled on 2026-11-13 that uncertainty should drive *more or fewer sampled rollouts*, with a hard fractional ceiling; an uncertain decision should not fall back to running the entire heuristic ladder. Preserve that design intent.

## Acceptance route

1. Reproduce and classify all F2 and F5 later games. A fixed-seed repair screen is for diagnosis only.
2. Measure candidate speed and win turns in one paired batch across all 56 decks, at the bare ladder's depth 5 / 20 ms / turn 8. Require no deck's mean win turn to worsen; inspect every later game, including any earlier/later trade. A material CPU saving should hold across independent fresh sets, not only the training seeds. The full table is in `docs/design/sampled-gate-ladder.md` lines 724–798.
3. Confirm with a separate held-out seed range, including larger samples for near-neutral or high-variance decks. Compare to configured play shapes separately before adopting a deck; a win over the bare ladder does not imply a win over a value leaf or single-pass setting. Keep Boars' existing adoption unless a new candidate is demonstrably better there.
4. Run unit tests and smoke; audit any ground-truth mismatches against the exact parent binary, as P18 did. The prior smoke run had 39 mismatches, all 39 reproduced on parent `8cd0bbf23`, and no P18-induced drift. Do not rebaseline unrelated ground truth.
5. Update `docs/design/sampled-gate-ladder.md` with the new candidate and all-deck results. Commit locally. Follow the repository's four-hour shared-push rhythm; personal `wip/*` branches may be pushed sooner. Run `python3 scripts/check_references_safe.py` before pushing or ending. Watch Windows CI after a platform-sensitive push.

Relevant instructions: repository `CLAUDE.md`; `.claude/skills/mtg-ai.md`, `.claude/skills/regression-testing.md`, and `.claude/skills/coding-conventions.md` if adding a diagnostic lever. The user's stated bar is stronger than the current mode 2 result: **do not report the project complete while quality losses or merely neutral runtime remain.**

## Claude session checkpoint (P18i, 2026-11-17)

Scratch: `/workspaces/MagicDeckTester/build/scratch/p18g` (`diff.py` side-by-side p16/p18 traces, `mkcases.py`,
`mkfleet.py`, `summ.py`, `run.sh` = pooled batch under `build/scratch/box.lock`). All runs launch from this worktree.

Classification of the 11 F2/F5 later games (first divergence; T1 actions are identical in all class C/D games):
- A (cold over-priced pass 4 refused after heur's own real-cost gate admitted it): Gods 38, 124, 336.
- B (`phx` pre-refused pass 5 on a cold pass-4 price): Mill 197.
- C (T1 depth identical; at T2 heur's low passes are cache hits from T1's superseded rollout passes, mode 2's are not,
  and `c_k^2/c_{k-1}` over a zero-leaf cached denominator refuses): Dragonstorm 149, Gods 51/58/250, Merfolk Turns 317.
- D (cold committing pass returns a worse win than an exact pass already found): Kithkin 100.

New screen-only arm-key knobs (default off; mode 2 unchanged): `_pqg` zero-leaf growth -> bootstrap growth;
`_pqh` re-check a `phx` refusal on the commit's real cost; `_pqr` the same for every refusal taken while pass K was
priced; `_pqf` heur's real-cost admission after an exact pass binds; `_pqk` keep a better exact line (UNSOUND as a
rationale: Gods 312's "proven T4" exact line actually won T8; it reverses mode 2's one gain).
Repro T2 (12 games): `_pqg` repairs 6 (Dragonstorm 149, Gods 51/58/250, Merfolk Turns 317, Mill 197); `_pqh` repairs
Mill 197; `_pz20` repairs Gods 38; Gods 124/336 need `_pqf`. Next: T3 repro of `_pqg_pqf_pqr`, then a 56-deck screen.

### P18i results so far (2026-11-17)
- R1 (56 decks x 400, F2 seeds, `mtg_i1`): m2 reproduces F2 exactly (1/9). `_pqg_pqf_pqr(_pqz)` repairs the 11 targeted
  games but is 1.011-1.015x CPU and shows new Gods / Alchemist losses: repair-by-guard on the priced gate costs more
  than mode 2 saves. Abandoned as the main line.
- O1 ORACLE CEILING, all 56 decks (60 games, seeds 9,000,000+; `MTG_P16_ORACLE` from C1's heur trace): keep-1 0.925x CPU
  overall with 0 later games; on rollout-heavy decks 0.36-0.60x (Breaching Dragonstorm, Angels, White Knights, Critter,
  Burn, Knights, Slivers, Red Elementals). Mode 2 captures under a fifth of it: on Angels T1 it runs passes 1-3 exactly.
- Heur's K on those decks is set by an IN-WINDOW WIN in 80-100% of decisions (C1), which a leafless pass proves exactly.
  New shape `_pll` LEAFLESS LOOKAHEAD: leafless passes 1..depth, commit the first in-window win, else heur's own ladder
  from pass 1 with the lookahead spend credited. L1 (60 games): Critter 0.31x, Breaching 0.34x, Angels 0.34x, Slivers
  0.37x, Kithkin 0.46x, Soldiers 0.47x, Knights 0.50x CPU, but interior-heavy decks blow up (Vampires 2.2x) and 11
  later games. L1T traces: (a) leafless walks shared the main TT and changed the fallback ladder's result (Alchemist
  9000035 T1: H1 T4 -> T5); (b) the lookahead walked passes heur never runs (W3 17.5k of an 18k budget).
  Fixes `_pllt` (scratch TT) and `_pllg<pct>` (predictive stop on the leafless growth extrapolation). L2 screens them.
- L2/L3 (100 games/deck, seeds 9,500,000+): the predictive stop `_pllg` and the scratch TT `_pllt` are REJECTED (the stop
  forfeits the cheap deep commits; the scratch TT costs CPU and did not fix quality). Root cause of the lookahead's later
  games: a lookahead commit is a VERIFIED line, which AIEngine replays across later turns with no re-search; heur, whose
  budget never reached that depth, re-searches each turn (Alchemist 9500073: T1 W5 proved T5 and was replayed; heur
  found T4 at T2). `_pllr` (TurnSolver::TakeNoReplay) keeps a lookahead commit to the current turn. With it, ~30 decks
  are 0.31-0.80x heur CPU with zero later games in 100; every remaining later game is on an interior-heavy or gate-bound
  deck (Mill, Cyclone, Gods, Landfall, Hinata, Melira, Shadowborn) where the lookahead also costs more than heur.
- No per-decision stop rule found (simllx.py): pass-1 rollout/leafless ratio does not separate the decks, and leafless
  growth extrapolation stops before cheap deep commits. Shipping model: per-deck opt-in on measurement.
- H1 (running): 56 decks x 400 games, seeds 10,000,000+, arms heur / `m2_pll_pllr` / live shipped shape (all at d5/b20).
- H1 (56 decks, seeds 10,000,000+, ALL arms at each deck's SHIPPED depth/budget; `LIVE_SETTINGS=1 mkfleet.py`): ABORTED
  by the 20 GB RSS cap after 2,737/6,720 jobs (~170 paired games/deck, all decks). Lookahead vs heur: 0.31-0.9x CPU with
  no later game on the rollout-heavy decks; later games only on Mill/Gods/Hinata/Cyclone/Melira/Shadowborn/NivMizzet/
  Kitty/LFE/Eldrazi and 2 Alchemist. Vs the LIVE shipped shape (paired): cheaper with no later game on Red Elementals
  0.63x, Morph 0.64x (3 earlier), Spirits 0.72x, Angels 0.79x, Boars 0.83x (1 earlier), Treasure Hunt 0.90x, Slivers
  0.93x; the shipped no-leaf shapes are cheaper still on Critter/White Knights/Kithkin/Breaching/Dragons/etc.
- TOOLING BUG FIXED: a `live` arm symlinked decks/*.value.json and mkfleet wrote mode 2 THROUGH it (48 deck sidecars in
  this worktree overwritten, never committed; restored with git checkout). mkfleet now unlinks before writing.
- Production mode `value_play.sampled_gate = 3` = `_pll` + `_pllr` (TurnSolver.cpp Knobs()).
- H2 (RUNNING at compaction, `build/scratch/p18g/H2`, DONE marker H2/DONE, binary mtg_i6): 9 candidate decks x 1000
  games, seeds 11,000,000+, shipped settings, arms heur / live / m3 / m2_pll_pllr. Read: `H2/summary.txt` (vs heur),
  `H2/summary_vs_live.txt` (m3 vs live); also check m3 == m2_pll_pllr digests (identity of the production mode).
- Next: if H2 is clean, adopt mode 3 on the bare-ladder decks (Red Elementals, Morph, Spirits: new
  `{"value_play":{"sampled_gate":3}}`; Boars 2 -> 3), surface Angels/Treasure Hunt/Slivers to the USER (they ship other
  shapes whose sidecars carry more than play settings); then unit tests, smoke + regression GT for the adopted decks'
  rows, update docs/design/sampled-gate-ladder.md, push wip branch, watch CI. Open: CPU >> units on Kitty/Gods/Extra
  Turns under the lookahead (unprofiled); memory growth at deep shipped settings (H1 abort).
- H2 DONE (clean): vs heur m3 0.698x CPU 38/0; vs shipped: Boars 0.786x 12/0, Morph 0.652x 7/0, Spirits 0.758x 1/0,
  Treasure Hunt 0.870x 2/1 (game 432 = heur's line, recovers at 4x/16x/d8b0), Angels/Slivers ~1.0x, Mirrorwing/Selesnya
  worse. m3 != m2_pll_pllr by design (m3's fallback is the bare ladder; 0.698x vs 0.711x).
- Rebased onto 1e36f852b (upstream adopted Red Elementals no-leaf d6/b20; it is digest-identical to m3 at d6, so
  Red keeps it). Sidecars committed (9f5a38ed7 pre-rebase): Boars 2->3, Morph and Spirits new. H3 (5 decks x 1000,
  seeds 12,000,000+, mtg_i7 = rebased): clean, committed sidecars digest-identical to m3.
- RUNNING: `test/regression.sh --both` (log build/scratch/p18g/reg_both.log). Next: accept GT with
  `--deck=boars,boars2hg,morph,morph2hg,spirits,spirits2hg` after auditing; any other mismatch -> parent-binary audit
  (build 1e36f852b in a separate worktree); push wip branch; shared-branch push at the 4h window; CI watch.
- USER decision surfaced: Treasure Hunt -> mode 3 (needs a sidecar that drops the leaf-table shape; not done).
- DONE 2026-11-17 ~06:30 UTC: rebased onto a5c156a20; smoke+regression GT accepted for the 15 Boars/Morph/Spirits keys
  (parent-attributed; all other failing keys fail identically on the exact parent). Pushed to the shared branch.
  Remaining: USER decision on Treasure Hunt; open profiling items above. P18i is otherwise closed.
