# Why the PICK shape lost to the model leaf, and the over-budget dead-end memo defect (2026-10-17)

**Status: findings + one prototype fix, all awaiting USER decisions (listed at the end).** Follows
`value-leaf-cost-erosion-2026-10-17.md`. Code is commit `2229ddb27` on the LOCAL branch `leaf-pick` (worktree
`.claude/worktrees/leafpick`), default-off throughout; the batch manifests and analysis scripts are under that
worktree's `logs/pick/` (gitignored; every run regenerates from its `make_*.py`). All measurements: held-out seeds
820000/821000 x 200 games per arm, paired game-for-game across batches (play digests verified identical).

## 1. The PICK shape (USER idea) and its result

USER: *"do no-leaf, but then consider value-leaf ... if we found that we could support depth 4 for value-leaf, but
only 2 for heuristic rollouts, then we consult the matrix to decide which to pick."* Built as `value_play.ladder
"pick"` / per-job `ladder_pick`: a leafless probe (reserve on), then ONE pass on whichever leaf the deck's depth
matrix rates better at the depth each can afford (rollouts: tree + R x leaves fits; value: tree fits).

Cost vs no sidecar, paired win turn vs no sidecar (negative = faster):

| deck | model leaf | pick (strict probe gate) | pick, relaxed gate |
|---|---|---|---|
| FiveColour | 1.60x -0.025 | 0.75x +0.003 | 0.97x -0.005 |
| Bruna | 1.05x -0.018 | 0.80x +0.009 | 0.90x -0.010 |
| Prevent Damage | 1.14x -0.026 | 0.77x +0.008 | 0.91x -0.015 |
| Landfall | 1.61x -0.033 | 0.96x +0.038 | 1.26x +0.023 |
| Melira | 1.34x +0.008 | 0.66x +0.070 | 0.79x +0.075 |
| Hinata2 | 1.28x +0.036 | 1.02x +0.013 | 1.24x +0.015 |
| Snow | 1.69x 0.000 | 0.97x +0.010 | 1.20x +0.010 |

Pick loses to the model leaf on every deck except Hinata2 (where the model leaf itself is bad).

## 2. Root cause

- **The leaf does not cheapen the tree.** At the same completed probe depth, a value-leaf pass costs the same units
  as a leafless one (+-5% on all decks).
- **The value pass is rarely chosen, and choosing it more would not help.** The matrices rate V(d) ~ H(d-2), so pick
  chose the value leaf on 2-4% of passes (Landfall 28-45%, Melira 23-36%); when chosen it runs AT the probe's depth.
  Landfall and Melira chose it often and still lost by 0.04-0.07 turns.
- **The model's probe line is itself a usable decision; a leafless probe's is not.** Pick pays for the tree twice.
  When the probe used up the budget (34-49% of unverified decisions), the model plays its own value line at depth
  4-5; no-leaf escalation plays an unsearched line (its escalation commits nothing); FIT / relaxed pick play a
  ~90-160-unit depth-1 rollout (Landfall, Melira).
- **Most of the model's quality is its budget envelope** (the 8.8x start gate + the 25x run-on; see
  `value-leaf-cost-erosion-2026-10-17.md`). With the strict gate the model leaf plays like no-leaf escalation
  (FiveColour -0.010 both). Given the same run-on, leafless shapes reach the model's quality but at 1.15-2.2x its
  units -- and section 4 shows most of THAT gap was a defect.

## 3. Is the 8.8x gate just budget? No.

No sidecar, and the strict-gate model leaf, each at 1.5x / 2x / 3x the deck budget, interpolated at the model
leaf's own cost (model minus curve; negative = the gated model leaf wins at equal cost; se ~0.006-0.012):

| deck | vs no leaf + budget | vs strict-gate leaf + budget |
|---|---|---|
| Prevent Damage | -0.020 | -0.008 |
| Bruna | -0.016 | -0.017 |
| FiveColour | -0.013 | -0.005 |
| Landfall | +0.020 (no leaf at 60 ms, 1.53x, beats it 13/6) | -0.019 |
| Melira | +0.021 | +0.008 |
| Hinata2 | +0.051 | -0.008 |

Where the leaf is good, the gate spends budget better than a uniform raise (it funds the decisions that need a
deeper pass). Where the model leaf is bad (Hinata2 and Melira, both flagged stale; Landfall's staged leaf), no leaf
with a bigger budget wins. Landfall, no sidecar: 40 ms 1.34x -0.030, 60 ms 1.53x -0.0525 (21 better / 0 worse) vs 20 ms.

## 4. The over-budget dead-end memo defect and its fix (`MTG_PROVISIONAL_NOWIN`)

**Defect.** The search memo stores "wins by turn t" always, but "no win by turn t" (a dead end) only when nothing
beneath it was cut short. Once a pass has spent its budget it keeps searching its main options (to the 25x ceiling)
but skips the deferred wave phases, and every skip books a cut (`g_fs_trunc_events`) -- so from then on almost no
dead end is memoised and every transposition is searched again at full price. A rated leaf barely notices (its
nodes return estimated wins); a leafless pass is nearly all dead ends. This is why leafless passes were given the
stop at the budget and the strict start gate. Inside the over-budget pass the refusal buys nothing: a repeat search
there skips the same phases and returns the same incomplete answer.

**Fix (heurarm `PROVISIONAL_NOWIN`, default OFF, branch `leaf-pick`).** A dead end found once the budget is spent is
stored under a provisional key (private salt + the pass's epoch, `ProvEpochGuard` at the pass root), reusable only
by the same pass while its budget stays spent; each reuse books a cut, so no ancestor is ever memoised as a proof.
Inert at an unlimited budget. Budget reads use a side-effect-free check (`SearchBudget::Exhausted()` books a budget
bind that would itself veto memo stores).

**Measured.** Play byte-identical to the fix-off runs in >= 398/400 games per deck and arm; units:

| arm | units, fix on / off |
|---|---|
| no-leaf escalation run on to 25x | 0.46-0.73x |
| pick run on to 25x | 0.54-0.80x |
| no sidecar | 0.75-1.00x (Melira 0.75x) |
| model leaf | 0.87-1.04x |

With the fix, pick run on to 25x is within ~0.01 turn of the model leaf at lower cost on FiveColour (1.36x vs 1.60x),
Prevent Damage (1.05 vs 1.14), Bruna (1.00 vs 1.05) and Landfall (1.47 vs 1.61, +0.010); Melira +0.038, Hinata2
-0.031. **Smoke (120) and regression (166) ALL PASS with the fix forced on** -- every case byte-identical to
committed ground truth (engagement verified: the env switch stores and reuses provisional entries in batch mode).

## 5. Also observed

Pooled-batch nondeterminism on Bruna: the model arm reran 345/400 (and 690/800) game-identical across batches, every
other deck 400/400. The open fix is `e5ea0e297` on branch `nondet` (not landed). It only adds noise to Bruna's
cross-batch pairings here.

## 6. Open USER decisions

1. Turn `MTG_PROVISIONAL_NOWIN` on by default (play-neutral on both tiers; removes repeated work). Then re-measure
   the leafless run-on shapes with it -- they become cost-competitive with the model leaf.
2. The 8.8x gate: keep it (it beats equal-cost budget where the leaf is good); limiting it to trusted leaves is not
   the right cut. The per-deck question is leaf vs no leaf with a bigger budget at equal cost (Hinata2, Melira,
   Landfall) -- i.e. a cost-matched phase E arm and refits of stale leaves.
3. Landfall: no sidecar at 40 / 60 ms. The Landfall mulligan profile is PAUSED (USER, 2026-10-17); its generation
   setting measured d5/b20 (no cheaper labeller cleared rho 0.99: d3/b20 0.970, d3/b3 0.966) -- settle the play
   budget and the labeller before relaunching.
