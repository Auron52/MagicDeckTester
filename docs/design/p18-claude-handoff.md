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

## P18j checkpoint (Claude, 2026-11-17, USER: "We shouldn't leave any of these open. If there are cases where the mode loses to the heuristic ladder we should keep investigating.")
- Treasure Hunt candidate sidecar `{"value_play":{"sampled_gate":3,"target_depth":5,"budget_ms":40,"enabled":true,"mull_gen_depth":3,"mull_gen_budget_ms":20}}`
  is digest-identical to the screened m3 arm (TH1, 100 games). Its shipped sidecar's `leaf: none` attaches a constant
  value model, which disables any sampled gate, so the switch is a play-shape change (USER decision, surfaced).
- Mode 4 (`sampled_gate=4`, WIP f2b4df687 on wip/p18j-claude, scratch `build/scratch/p18g/P1..P14`): WIN-TRIGGERED lookahead.
  Root rollout (FSLineWin depth 0, cut at the window edge, capped at 1% of the decision budget `_pzc`) -> if it wins in
  the window, leafless passes 1..W-T+1, each admitted only if the ladder's start gate would admit the rollout pass it
  replaces (priced l_k + n_k * q * max(0, W-T-k), n_k counts window-cut nodes). A miss REFUNDS its units
  (SearchBudget::Refund) and the whole lookahead runs ISOLATED: own memo epoch, scratch TT for the root rollout, no
  stores into the game-level caches (rgc / gamelc / inwin) or workctr-muted caches. Without isolation the root rollout
  warmed pass 1 (Hinata 13220014 T1: 9125 -> 246 units) and the start gate left heur's K (gate drift, both directions).
- Findings: mid-ladder walk (after each rollout pass) saves little (the in-window incumbent already prunes heur's later
  passes) and adds drift -> `_pnm` (no mid walk) is the better arm. `_pfp` (full-path reach) and `_pwc` (per-pass cap)
  kill the fast-deck savings. P13 (16 decks x 20, seeds 13,000,000+): m4_pnm 1.0097x heur CPU, 0 earlier / 0 later;
  fast decks 0.43-0.89x, budget-bound decks 0.99-1.05x except Melira 1.115x (one 65k leafless pass-3 bet lost).
- Running: P14 `_paw` (walk to depth when the root rollout shows no in-window win, mode 3's reach, still priced + refunded).
- P15 (16 decks x 20): `_pnm_pkt_plx` 1.0016x CPU / 0.992x units, 0/0 -- fast-deck savings back near mode 3 (Angels 0.44,
  Critter 0.43, TH 0.57, WK 0.41). A cached root rollout (0 units) walked pass 1 only: 7 hits in 333 walks -> no walk.
  Mode 4 now = llw+lrf+llr+nmw+pkt+plx (committed WIP).
- S2 (56 decks x 100, seeds 14,000,000+, shipped settings, binary mtg_j19; `build/scratch/p18g/S2/summary.txt`):
  m4 vs heur 2 earlier / 0 later, units 0.960x, CPU 1.036x. Rollout-heavy decks 0.35-0.83x; budget-bound 1.00-1.04x.
  Two outliers: Gods 1.64x CPU at 1.00x units = ONE game (gi=35, seed 14210035, digest differs from heur, 316 s CPU
  for 299k units -- repro manifest /tmp/g35.json; profile it); Five Colour 1.22x units = 17 walks past the reach that
  ran to the whole remaining budget (`_plx` admitted an over-priced pass; W4 33k).
- UNCOMMITTED at compaction: past-reach passes (`_pkt` target / `_plx`) get an allowance of 0.1 x Limit (cap) and
  `_plx` also needs lest <= that allowance (fixes Five Colour). Diagnostic env MTG_P18_ISO (bits 1 no epoch iso,
  2 no store block) and MTG_P18_ZX (1 root rollout only, 2 none) must be REMOVED before shipping. Build in flight.
- NEXT: rebuild; repro Gods gi35 (MTG_P18_ISO variants, then perf on build/Profile); re-run S2-style 56-deck screen;
  if clean (no later game, CPU <= heur on every deck), held-out confirm, then unit tests + smoke/regression GT and
  decide default vs opt-in. Strip dead knobs (_pfp, _pwc, _paw, _pllt-with-llw, mid-walk if unused).
- GODS gi35 ROOT-CAUSED (679c0f1f7): the walk was bounded and refunded in UNITS only. T4's leafless pass 4 cost 9404
  units but 14.5 s (window-edge cut nodes do applies/enumeration charged nothing), tripping the per-decision PRICED
  ceiling (MTG_DECISION_PRICED_X=100, never refunded) so the ladder's pass 1 aborted at 0 units. NoStore's
  `++workctr::t_mute` made it worse: a mute stops priced counting AND disables the plan/mana caches -> 174 s (it also
  skipped ManaCacheMuteEnd). The same unrefunded-real-cost effect is why CPU >> units on Kitty/Gods/Extra Turns, and
  the walk's work is where S2's RSS growth came from (1.17 GB vs 227 MB on gi35). FIX: every walk pass and the root
  rollout carry a priced cap (unit allowance x MTG_DECISION_PRICED_REF_NS; root rollout max(1% unit cap, 0.1 x Limit));
  a miss refunds priced work from the decision ceiling (t_pr_start shift) and the decision unit meter; NoStore blocks
  only rgc/gamelc/inwin (plan/mana caches charge a hit as its recompute). gi35: 8.4 s / 250 MB vs heur 8.2 s / 227 MB.
- RUNNING: S3 = S2's manifest (same seeds, both arms re-run) on mtg_j23, trace on, `build/scratch/p18g/S3/` (DONE marker).
  Diagnostic adds: walk records carry wall ms in the dvs field (trace only).
- S3 (mtg_j23, S2's seeds, both arms re-run): m4 0.983x CPU / 0.982x units, 0/0 games; Gods 1.00x, Five Colour 1.00x.
  Fast-deck savings fell (Kithkin 0.35 -> 0.74): the past-reach allowance edit (0.1 x Limit, unit AND priced) cut
  intermediate passes that S2 walked to hits (Kithkin 14250017 T1: W4 8.3k units no win, W5 5 units hit).
  Isolation check (drift.py: heur's own passes on non-hit decisions, m4 vs heur): ~0 on every deck -> mute not needed.
- S4 (mtg_j24): `_pzb` (no root rollout for the rest of a game after 2 consecutive cuts) Extra Turns 1.036 -> 1.010x,
  neutral elsewhere; `_pxa` mixed. CPU noise per deck is ~+-1% at 100 games (arms with identical units differ that much).
- S5 (mtg_j25): `_pal` 30/50/100 -> 0.971 / 0.969 / 0.959x CPU; pal100: Kithkin 0.37, Knights 0.38, Soldiers 0.40,
  Minotaur 0.49, Angels 0.48, 2 earlier / 0 later. Remaining >= 1.00x on both CPU and units: EDF, Extra Turns, Hinata,
  Landfall, LFE, Martyr, Melira, Merfolk Turns, Niv (+0.2..2%): Zcut 0.4-2% + walk misses 0.2-3%, few hits.
  Melira mechanism: leafless pass k costs ~= heur's rollout pass k (heur's later-pass leaves are TT hits from the pass
  below), so a hit saves little and a miss doubles the passes walked.
- RUNNING: H1 = held-out 300 games x 14 budget-bound decks, heur vs m4_pzb_pal100 (seeds 15,000,000+), mtg_j25,
  `build/scratch/p18g/H1/` -- which of the residual losses are real.
- H1 (held-out, 14 budget-bound decks x 300, seeds 15M, m4_pzb_pal100): 1.000x CPU, 5 earlier / 0 later; real losers
  Melira 1.034x, Martyr 1.017x, Shadowborn 1.009x; EDF/Hinata/Landfall/LFE/Niv 0.99-1.00x (S5 readings were noise).
- Mode 4 DEFAULTS now include _pzb + _pal100 (ce91ddeba); j27 plain m4 digest-identical to S5's m4_pzb_pal100 arm.
- H2 (held-out, 56 decks x 200, seeds 16M, mtg_j27; `build/scratch/p18g/H2/summary.txt`): m4 0.954x CPU / 0.954x units,
  2 earlier / 1 later. >= 1.00x on CPU and units: Extra Turns 1.006/1.022, Melira 1.025/1.012, Landfall 1.011/1.011,
  Shadowborn 1.009/1.015, Martyr 1.008/1.010, Mill, Hinata, Hydras, LFE, Niv (<= +0.5%). `_pip` (intermediate path
  check) 0.957x: Melira 1.018x but Pirates/BDS +3%; not adopted.
- The LATER game: Accomplished Alchemist seed 16000131 gi131 (T4 -> T5). T1: walk hit at W5 (proven T5; W4 complete,
  no T4 found) past heur's reach (heur commits H3, rollout w5, then finds T4 at T2). At b0 (d5 and d8) BOTH arms win
  T4 (T1's unbudgeted search commits depth 4, win T4): a T4 win existed inside W4's tree but the budgeted enumeration
  did not reach it. Recovers at d8 b0 (adoption bar); a budget-time refutation trusted past heur's reach.
  Repro: `build/scratch/p18g/AA131/manifest.json`.
- Tally (pzb+pal100): S5 2/0, H1 5/0, H2 2/1 -> 9 earlier / 1 later.
- USER ruling 2026-11-17 (ship shape): mode 4 will be turned on PER DECK for the clear winners only (not default-on).
  The user may hold the flip until mode 4 is in its final state ("I want to see it in its final state before flipping
  it on"), so finish the residual-loser work and the ship steps first, then present the per-deck winner list for the flip.
  Still open (USER): the AA gi131 later game; Treasure Hunt -> mode 3.
- USER ruling 2026-11-17 (where mode 4 sits in the pipeline): the bare heuristic ladder STAYS the default for new
  decks. Mode 4 is the option turned on AFTER the value-leaf stage, for a deck where the ladder would otherwise win
  (the leaf is not adopted and the deck ships the heur ladder). Ship-step consequence: the per-deck adoption is a
  post-value-leaf step (heur vs m4 held-out A/B on that deck; adopt on a clear win). Whether valueleaf.sh runs that
  A/B itself or it stays a separate step is to be proposed to the USER with the final state.
- USER ruling 2026-11-17 (3): valueleaf.sh GAINS A STAGE that measures mode 4 against the heur ladder (when the ladder
  is the shape that would ship) and turns it on for the deck when it wins. The design goal is unchanged: mode 4 should
  beat the ladder on every deck, so that stage normally just confirms it. A deck where it does not is a residual
  loser to investigate, not an accepted outcome.
