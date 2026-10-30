# Rename the Hinata2 deck to Hinata (queued 2026-10-30)

USER 2026-10-30: *"Another small item I would like to queue up is renaming Hinata2 -> Hinata. The names
shouldn't have any numbers in them."* Hinata2 is the only deck under `decks/` with a digit in its name, and
no `decks/Hinata` or `references/Hinata` exists, so nothing collides.

## When

**Only after the Hinata2 artifact-queue pipeline has finished and its artifacts have landed.** The paused
worker (`.claude/worktrees/artifact-worker-MagicDeckTester2-76d2cc`) holds the live keep journal under
`decks/Hinata2/Hinata2.keepmodel.exhaustive.raw.json.journal` (plus its `.spec` side journal and rolling
backups), its `gen.log` under `logs/Hinata2_mullgen/`, and an `artifact-queue` claim keyed on the deck
path. Renaming under a paused or running generation strands all of that. Land the refresh branch
(`artifact-refresh/Hinata2-<date>`) first, then rename in one commit.

## Footprint (git grep on 2026-10-30: 200 tracked files)

| where | what changes |
|---|---|
| `decks/Hinata2/` | `git mv` to `decks/Hinata/`, renaming every `Hinata2.*` sibling to `Hinata.*` (`.cod`, `.profile.json`, `.value.json`, `.escgate.json`, `.keepmodel.exhaustive.{profile,raw}.json.gz`). The engine resolves siblings off the profile path, so the stems must all move together. |
| `references/Hinata2/` | 10 USER hand-played games. The USER asked for this rename, so these are moved by `git mv` (history kept). Never copy-then-delete, and push at once. Run `python3 scripts/check_references_safe.py` before and after, and check the primary viewer tree for uncommitted references first (CLAUDE.md hard rules 2-3b). The viewer pairs references by deck folder (`server.js`), so both folders must move in the same commit. |
| `test/regression_cases.sh` | `DECK_FILE[hinata]` / `DECK_PROF[hinata]` paths (lines ~40, ~90). The case KEY is already `hinata`, so `regression_gt.txt` keys do not change. Check `gt_logs/` file names and `test/suite_cost.json` for the path. |
| `scripts/` (17), `test/` (15 + tools/scenarios/unit) | hard-coded `decks/Hinata2/...` paths in A/B tools, scenarios (`test/scenarios/*.json` deck paths) and unit tests. Update the live ones; `scripts/attic/` can stay as history. |
| `src/` (6 files) | comments only (no behaviour reads the name). Update or leave; no play change either way. |
| `docs/design/` (96) | historical prose. Leave dated write-ups as they are; update living docs (skills, `artifact-staleness-queue.md`, ledgers in active use). |
| artifact queue | `scripts/artifact_queue.json` / origin's `artifact-queue` branch claims, plus the other machine's worker (artifact-queue-d7d6a5): make sure no claim or staleness record still points at `decks/Hinata2`. |
| keep / value artifacts | check whether any artifact header records the deck PATH or stem in a field a gate compares (`deck_fp`, `freeze`, `commit`). If one does, the rename invalidates a gate on an unchanged list. Fix the gate to key on content, not re-generate. |

## Verification

1. `./build.sh` and unit tests (scenario/unit paths).
2. smoke: the `hinata` rows must be byte-identical (same GT). The rename moves no play.
3. `python3 test/check_gt_logs.py`, `python3 scripts/check_references_safe.py` (exit 0), and the reference
   replay for `references/Hinata/` (10/10 replay as before).
4. The play viewer lists `Hinata` (no `Hinata2`) and shows its 10 references.
5. `python3 scripts/artifact_queue.py status` resolves the deck under its new path.
