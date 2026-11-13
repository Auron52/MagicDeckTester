# Rename the Hinata2 deck to Hinata (queued 2026-10-30; DONE 2026-11-13, with slivers_vial -> Slivers)

## Status: DONE 2026-11-13 -- and the follow-up it opens

USER 2026-11-13: *"Can you start by renaming Hinata2 to Hinata and slivers_vial to Slivers?"* -- *"A number
being in it or vial being in the name are especially bad, which is why I singled those out at first. But I
think it would be good to go for fully consistent naming, with Capitals and spaces once we believe we can do
so."* -- *"But to start let's just do these two. Then we can see what kind of problems this causes."* The USER
gave special dispensation to push this one at once (outside the 4-hour shared-branch window), since a rename
that sits causes the most trouble.

**What moved** (`git mv`, history kept): `decks/Hinata2/` -> `decks/Hinata/` and `decks/slivers_vial/` ->
`decks/Slivers/` with every sibling stem; `references/Hinata2/` -> `references/Hinata/` and
`references/slivers_vial/` -> `references/Slivers/` (10 USER games each, moved in the same commit as their
decks); `test/artifact_ledger/{hinata2,slivers_vial}.json` -> `{hinata,slivers}.json`; the four
`test/scenarios/hinata2_*.json` -> `hinata_*`; `test/unit/test_slivers_vial_discard.cpp` ->
`test_slivers_discard.cpp`. Live tools, tests, scripts, skills and `tools/mulligan/decks.js` (regenerated)
were rewritten; slug-keyed files (`mana_clean.json`, the artifact ledgers) took the new slugs `hinata` /
`slivers`.

**Deliberately left as history:** `src/` (comments only -- leaving it keeps `HEAD:src`, the PGO stamp and
every paused generation freeze unchanged), `docs/design/` prose, `scripts/attic/`, GT and `job_cost.tsv`,
recorded `logs/...` paths, run ids, `artifact-refresh/...` branch names, the value sidecar's changelog note
(artifact bytes untouched, so the artifact queue's blob hashes do not move), and the finished claims
`claims/hinata2.json` / `claims/slivers_vial.json` on origin's `artifact-queue` branch (both landed; the
ledger governs a landed claim, and a missing claim does not queue a regeneration).

**Things the rename exposed (the checklist for the broad rename below):**
1. `hinata2hg` is NOT the old name: it is the `hinata` case key + the `2hg` format suffix (GT keys, gt_logs,
   job_cost). Any mechanical rewrite must protect `<key>2hg`.
2. Two key conventions coexist: the FOLDER name (`references/stable_shuffle_recoverability.json`,
   `test_pirates_provider.cpp`, `h_deck`, `tools/mulligan/decks.js`) and the deck_registry SLUG
   (`mana_clean.json`, `ref_bench.json`, `test/artifact_ledger/`, artifact-queue claims). With spaces the
   slug becomes `creature_giving`-style; a rename changes both.
3. `deck_registry` seed bases hash the slug (crc32 % 900): a rename gives the deck a new row-dump
   seed_base (Hinata 335 -> 716, Slivers 784 -> 333). Checked: no OTHER deck's base moved. Re-check this
   for every batch, since collision probing runs in sorted key order.
4. `test/ref_bench.json` is rewritten by the running viewer's automatic `--stale-only` bench; do not
   hand-edit it. `ref_bench.py` drops orphan rows and re-benches under the new key by itself.
5. A path-keyed cache (`~/.cache/mtg/keeptable`) just misses once per renamed deck.
6. The artifact-queue tool baselines staleness on the commit that last touched the artifact PATH, so a
   rename resets the drift clock to the rename commit (under-reports staleness; never triggers a regen).

**QUEUED (USER, not yet scheduled): fully consistent deck names, Capitals and spaces.** Once this rename has
run a while without trouble, rename the rest to the `Mirrorwing Dragon` / `Creature Giving` shape. Candidates
on 2026-11-13: `burn` -> `Burn`, `treasure_hunt` -> `Treasure Hunt`, and the CamelCase names
(`AccomplishedAlchemist`, `BreachingDragonstorm`, `CritterLifegain`, `EldraziDisplacerFlicker`,
`FiveColour`, `KittyEquipment`, `LandfallElementals`, `LifegainHeliodCombo`, `NivMizzetCombo`,
`SelesnyaLifegain`, `StompySurprise`, `WhiteKnights`) into spaced words -- the exact spellings are the
USER's call. `Anti-Lifegain` has a hyphen to decide on. Archived `v<N>-<slug>/` folders and their
reference folders move with their decks.

---

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
