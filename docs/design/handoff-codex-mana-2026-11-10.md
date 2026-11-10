# Mana analyzer handoff from Codex (2026-11-10)

The user will hand this work to Claude manually. Codex started a Claude Code
background session (`58f579e3`) during handoff preparation, then **stopped it**
when the user clarified that they would do the handoff. It made no repository
changes. There is no Codex-owned batch still running. The primary checkout is
clean at `20082c69e` on `phase-1-2-deck-analyzer`, tracking origin. The six
commits after `a9872cfdf` are independent Martyr Lifegain work from another
actor; check for concurrent work before editing and fetch before pushing.

Read root `CLAUDE.md` and the applicable `.claude/skills/*.md`. The user has
already authorized running, recording, committing, and pushing completed mana
work after `python3 scripts/check_references_safe.py`. Avoid duplicate runs,
keep `/tmp/codex-mana-heartbeat` current while working, and arrange a verified
continuation for long jobs. Use `./build.sh` for builds. The current `build/PGO`
stamp predates the Martyr source changes, so rebuild PGO for any new long run.

## Live measurements: leave running

Three EDF gi2535 tails from the Dragons mana handoff remain CPU active:

| PID | Measurement | Output |
|---:|---|---|
| 347828 | hostfix A/B, final `edf_on_2400` job | `build/scratch/mcdragons/lrprice/hostfix-ab.{out,err}` |
| 356311 | price control, base | `build/scratch/mcdragons/lrprice/hostfix-2535-base.{out,err}` |
| 356792 | price control, price4 | `build/scratch/mcdragons/lrprice/hostfix-2535-price4.{out,err}` |

They have run roughly 70 hours and still consume about one CPU each. Do not
cancel or duplicate them merely because surrounding jobs finished. The full
brief is `.claude/worktrees/agent-a9dab9fbcae918660/docs/design/handoff-dragons-mana.md`.
Check their PIDs and output before acting. Their existing binary inodes remain
valid after primary checkout updates.

## Completed and pushed

* `188e61691` fixed Jumbo Zenith/Finale exact same-tap float rescue; focused
  seed's `APPLY-DROP` fell from 40 to zero.
* `c783ff4c9` preserved Dragonstorm's rollout Storm options; the focused gi31
  searched win moved from turn 5 to turn 4.
* `2c316e618` accepted audited full smoke/regression ground truth. The patched
  Dragonstorm rows had zero slower searched games, six faster in smoke and ten
  faster in regression. See `docs/design/last-week-decks-regression-audit-2026-11-10.md`.
* `05f24ab62` changed the mana oracle to prove up to 180 **distinct** action
  orders. Shadowborn paired seed 30015115 moved 48 `UNMODELED` verdicts to
  `PHANTOM`, with oracle-off output byte identical. The 360-order case remains
  unmodeled. This is oracle-only; it does not fix the engine's payment misses.
* `a9872cfdf` recorded all four last-week mana-clean reports under
  `logs/mana_clean/lastweek-20261110-*/report.md`, the updated registry, and
  Shadowborn's analysis. The fresh Shadowborn block was 20000-24999.

At `05f24ab62` the Release unit suite passed 2,683/2,683 cases and 3,579,291
assertions. Strict viewer replay passed all 787 references with zero play drift,
board divergence, enum gap, mulligan drift, or contract failure. The reference
safety scan passed 195 checkouts/worktrees before the report push. Later Martyr
source changes require their own validation; CI run `42809513650` for current
HEAD `20082c69e` completed successfully.

## Six last-week decks

| Deck | Last mana-clean result | Next work |
|---|---|---|
| Kithkin | 5,000 clean; registry says `stale-clean` after later source edits | Retain the tested clean evidence; rerun when a relevant source change merits it. |
| Merfolk | 5,000 clean; `stale-clean` | Same. |
| Modern RW Burn | 100,000 clean; `stale-clean` | Same. |
| Jumbo Cactuar | 37,918 issues in fresh 5,000, indices 20000-24999 | Two decisions caused 34,384 `CAPPED-DECISION`; seeds 30024581 and 30022536. Also 560 `ENUM-GAP`, 879 `APPLY-DROP`, 572 `APPLY-DROP-ORDER`, 666 `DOMINATED-CREATURE`, 175 `ORACLE-GAP`, 682 `UNMODELED`. Inspect the report before choosing a bounded fix. Next block 25000-29999. |
| Shadowborn Apostle | 5,429 issues in fresh 5,000, indices 20000-24999 | 4,759 `UNMODELED`, 554 `APPLY-DROP-ORDER`, 95 `APPLY-DROP`, 16 `ORACLE-GAP`, 5 `ENUM-GAP`. Top payment misses involve Lake of the Dead paying Sire of Insanity. Next block 25000-29999 after a fix. |
| Extra Turns | No complete 5,000 gate | Oracle-on seed 30000003 p3 took 628.597 s versus 0.13 s off. A cap-1 replay left 7,404 `CAPPED-DECISION`; lowering the cap cannot yield a clean gate. See `docs/design/extra-turns-mana-oracle-sizing-2026-11-10.md`. |

Every completed mana gate above had byte-identical oracle-on and oracle-off
play. Do not compare issue totals from different fresh blocks as a causal
effect. The live registry is `python3 scripts/mana_clean.py status`; source
changes may mark more rows due or stale. The reports contain exact reproductions
and class signatures. `docs/design/analysis-Shadowborn Apostle.md` records
the order-proof boundary and remaining engine misses. The Extra Turns
cast-order review was independently closed at `f5b5dff75`.

## Suggested first checks

1. Verify branch/working-tree state, live PIDs and outputs, current CI, and any
   other active Claude work before launching a job.
2. Continue the three gi2535 tails via a real background waiter or scheduled
   check. Inspect the paired results when all three finish.
3. Pick one bounded mana-correctness investigation from the Jumbo or
   Shadowborn reports. Avoid rerunning a known-failing 5,000-game gate on
   unchanged code. Use the next unused block after an actual fix, and check
   oracle-off play identity.
4. Preserve a durable diagnosis for Extra Turns' oracle cost before attempting
   its full gate. Do not hide incomplete audits by raising or lowering caps.
5. Before any push, run `python3 scripts/check_references_safe.py`; record the
   result, commit only reviewed files, fetch/rebase if origin moved, and watch
   Windows/Ubuntu/parity CI.
