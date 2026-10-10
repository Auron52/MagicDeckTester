# Bruna mulligan profile: handoff to the secondary machine

Written for **whoever operates the secondary box** (human or agent). Self-contained.

This is a **fresh** keep-table generation. Nothing is banked: a 1 h 36 min `recommend` probe ran on
the primary on 2026-10-10, but it ran on play from before the Bruna width collapses (`fbd63d2f`), so
its journal cannot pool. Do not copy it.

## 1. What to build

Check out `origin/phase-1-2-deck-analyzer` at a commit whose **`src` tree hash is
`5c5cd74c291abaea32ba84eef384947b2dad0662`**. `5b4f963f` (the Wings-recast payment fix) is the first
commit with that tree; later docs/GT-only commits keep it.

**Not `cd0001a3`.** An earlier version of this doc named src `cd0001a3` (`9e56a277`). That tree predates
the payment fix, which changes Bruna's play (the executor now carries out the Greaves moves the search
planned), so a keep table generated there is fitted to play that no longer ships. A run already going
on `cd0001a3` should be reported before deciding whether to restart it.

```bash
git rev-parse HEAD:src     # must print 5c5cd74c291abaea32ba84eef384947b2dad0662
./build.sh                 # NEVER raw cmake (an empty build type is -O0, ~10x slower)
```

**Do not rebase or pull this checkout while the generation runs.** A later engine change can move
Bruna's play digest, and the generator's own validation step (the regression tier,
`regression.sh --deck=bruna`) compares against the ground truth of the tree it is running on; that
ground truth is consistent at `5b4f963f`. Use a dedicated checkout or
worktree for the run.

## 2. Everything it needs is tracked in git

| path | why it matters |
|---|---|
| `decks/Bruna/Bruna.cod` | the decklist |
| `decks/Bruna/Bruna.profile.json` | the play profile |
| `decks/Bruna/Bruna.value.json` | the value leaf AND the generation contract: `value_play.mull_gen_depth 1`, `mull_gen_budget_ms 3`, `expected_buckets 21`. Do not edit it. Without that block the generator falls back to the d5/b20 play default, which costs ~2.3x more per rollout. |
| `decks/Bruna/Bruna.buckets.json` | the USER's bucket rulings: Prodigious Growth + Mythic Proportions MERGED; Birds of Paradise and Avacyn's Pilgrim KEPT APART ("Missing blue is a real problem for this deck") |
| `src/cards/data/cards.json` | card data |

No gitignored artifact is needed. Bucket discovery runs first (~6 min on 24 cores) and caches itself
next to the deck.

## 3. The command

```bash
bash scripts/mullgen.sh run decks/Bruna fast
```

`fast` (adaptive bottoming, cap R=30) is the recipe for a deck this size; `complete` (R=40) would be
roughly twice as long. The command generates AND validates: the keep A/B against static, then the
confounded bottoming A/B (mode 3). A profile that is worse by any amount is quarantined to
`.DISABLED.json`. That validation is part of the run; do not skip it.

Launch it detached (`setsid nohup ... > log 2>&1 < /dev/null & disown`). Check progress with
`bash scripts/mullgen.sh status decks/Bruna`. Re-running the identical command resumes from the
journal. Before re-running anything near the journal, `cp` it aside: CLAUDE.md, "A GENERATION JOURNAL
IS DAYS OF COMPUTE".

## 4. What to expect

- **K = 21.** The generator refuses to start if discovery disagrees with `expected_buckets`. If that
  happens, stop and report; do not edit the number.
- **Hand space:** 671,400 seven-card hands (1,342,800 cells counting play and draw), plus 494,354
  smaller-hand batches.
- **Rollout cost on the primary:** 0.356 core-s at d1 b3 (Release, 24 cores; measured at src `cd0001a3`, before the payment fix, not re-measured), after the width
  collapses (0.437 before).
- **Estimate:** ~2.8 days on 24 cores at similar per-core speed. Scale by your box:
  `wall ≈ 2.8 d × (24 / your cores) × (primary per-core speed / yours)`. Treat it as central with a
  wide spread: the estimate borrows Snow's rollouts-per-cell ratio (~13), and size-7 refine has
  never been observed on this deck.
- **Slow hands:** none over 30 s in the first 214k floor rollouts on the primary. The slowest was
  26.5 s (two lands with a Karoo, two mana creatures, two Greaves, a Glittering Wish). Anything
  >= 30 s streams to `decks/Bruna/Bruna.keepmodel.exhaustive.raw.json.slow.log` with a replay seed.

## 5. When it finishes

Report the validation verdicts (keep and bottoming deltas, with seeds-better counts). On a pass the
profile is live by presence (`decks/Bruna/Bruna.keepmodel.exhaustive.profile.json`). Commit it with
the gzipped raw sidecar (never the uncompressed raw), then take Bruna's ground truth for the keep-table
play change on the PRIMARY's current tree. **Before that, on the primary:** Bruna's overnight rows are
the ENTRY set (since 2026-10-10); expand them to the FULL set and accept them with the profile ABSENT,
then adopt (`.claude/skills/regression-testing.md`, "Sizing a deck's overnight rows").
