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

### BATCH DONE 2026-11-13 (all but Niv-Mizzet Combo)

USER: *"Let's pull work through the actual changes and I will give special dispensation to push it without
waiting for the window. Getting the names through quickly is important."* -- *"Except, naturally, we need to
make sure nothing is broken by the change (via tests and such)."*

Renamed (folder + every stem, archived `v<N>-*` subfolders, untracked journals/gencaches, `references/` +
`suboptimal/` + `review/replaced/` folders, artifact ledgers, `docs/design/analysis-<stem>.md` ledgers that
`verify_deck.py` finds by STEM): the twelve CamelCase/lowercase decks in the table below, plus the stems
`Soldiers/soldiers.cod` -> `Soldiers.cod` and `Vampires/Vampire.cod` -> `Vampires.cod`. Tooling:
`logs/rename-decks/rename2.py`, `ledgers.py`, `pass2.py` (scratch, not committed).

* **HELD: `NivMizzetCombo` -> `Niv-Mizzet Combo`.** A USER-paused mulligan generation holds it (claim
  `nivmizzetcombo`, worker `artifact-queue-d7d6a5`, paused 2026-11-13T00:54Z). Rename it once that run has
  landed or been released: same three scripts with Niv added, same checks.
* **Shell quoting:** a rewritten shell token that gained a space was double-quoted (the
  `[creature_giving]="decks/Creature Giving/..."` form), e.g. every `DECK_FILE`/`DECK_PROF` row.
* **NOT updated -- legacy one-off scripts whose deck lists are WHITESPACE-delimited and so cannot hold a
  spaced name at all** (already true of Creature Giving): the `DECKS="a:a b:b"` lists in `test/label_*`,
  `test/nowin_*`, `test/ladder_value_leaf_check.sh`, `test/keepmodel_*`, `test/d5_budget_sweep.sh`,
  `scripts/beam_growth_merged_ab.sh`; the data lines of `scripts/dig_choice_ab/census*.sh` and
  `scripts/hand_entry_census.sh`; `test/patch_reference_mulligans.py` (pre-folder flat paths, already
  dead). Revive one by switching it to a `|`-delimited list if it is ever needed again.
* **Left as history:** `src/` (comments + `cards.json` notes: editing them moves `HEAD:src`, which stales
  PGO and flags card-data drift on every affected artifact), `docs/design/` prose, provider names
  (`DecisionProvider::Name()` -- "WhiteKnights", "SelesnyaLifegain"... are identifiers, not folders),
  regression CASE keys and GT, recorded result files, dated cast-order drafts.
* **Seed bases:** every renamed key gets a new row-dump base, and ONE un-renamed key moves by collision
  probing: `fungus_v1_sporesower_shelldweller` (an archived list; only a future value-leaf row dump on that
  list would see it).
* **Found while testing, NOT caused by the rename** (identical on a HEAD worktree with the same binary):
  `test/pay_line_color_check.py` FAILS (`MTG_PAY_PAIN_LAST=0` no longer restores the reported Adarkar
  payment), and `test/human_line_order_check.py` / `test/viewer_line_macros_check.py` SKIP (their reference
  decision is no longer reachable from its prefix). The reference bench of all 21 renamed lists replays 286 games with ONE shortfall,
  Selesnya Lifegain `claude_s2_gi1` (search 5.4 vs human 5.2 on the deck), identical on the pre-rename tree.

**QUEUED (USER, not yet scheduled): fully consistent deck names, Capitals and spaces.** USER 2026-11-13:
*"words separated by spaces and words starting with capital letters ... words that are one word in the card
should not change, so Dragonstorm remains as-is."* Agent recommendation (2026-11-13, spellings are the USER's
call; "live files" = tracked files outside docs/src/attic/logs naming the old folder, a rough size):

| now | proposed | slug now -> new | live files |
|---|---|---|---|
| AccomplishedAlchemist | Accomplished Alchemist | accomplishedalchemist -> accomplished_alchemist | 64 |
| BreachingDragonstorm | Breaching Dragonstorm | -> breaching_dragonstorm | 17 |
| CritterLifegain (+ v1) | Critter Lifegain | -> critter_lifegain | 36 |
| EldraziDisplacerFlicker | Eldrazi Displacer Flicker | -> eldrazi_displacer_flicker | 126 |
| FiveColour | Five Colour | -> five_colour | 73 |
| KittyEquipment (+ v1, v2) | Kitty Equipment | -> kitty_equipment | 106 |
| LandfallElementals | Landfall Elementals | -> landfall_elementals | 22 |
| LifegainHeliodCombo | Lifegain Heliod Combo | -> lifegain_heliod_combo | 32 |
| NivMizzetCombo | Niv-Mizzet Combo (the card's own hyphen) or Niv Mizzet Combo | -> niv_mizzet_combo | 45 |
| SelesnyaLifegain (+ v1) | Selesnya Lifegain | -> selesnya_lifegain | 36 |
| StompySurprise (+ v1) | Stompy Surprise | -> stompy_surprise | 75 |
| WhiteKnights (+ v1, v2) | White Knights | -> white_knights | 39 |
| burn | Burn | burn (unchanged) | many (substring) |
| treasure_hunt | Treasure Hunt | treasure_hunt (unchanged) | 79 |

Stems that do not match their folder (same class of inconsistency): `decks/Soldiers/soldiers.cod` and
`decks/Vampires/Vampire.cod` (the latter is the USER's own name -- ask). Already consistent and unchanged:
Angels, Auras, Bruna, Colossus Hammer, Creature Giving, Dragons, Dragonstorm, Extra Turns, Fluctuator, Fungus,
Galadriel, Giants, Goblins, Gods, Hinata, Hydras, Jumbo Cactuar, Kithkin, Knights, Landfall, Martyr Lifegain,
Melira Pod, Merfolk, Midrange Burn, Mill, Minotaur, Mirrorwing Dragon, Modern RW Burn, Morph, Pirates, Prevent
Damage, Shadowborn Apostle, Slivers, Snow, Soldiers, Unpredictable Cyclone, Vampires. Open for the USER:
`Anti-Lifegain` (keep the prefix hyphen, recommended), "Lifegain" as one word (recommended; Martyr Lifegain
already does), and the archived `v<N>-<slug>/` folders (recommended: leave -- they are version labels).

**USER RULING 2026-11-13: *"The names you mentioned look good."*** Read as approving the "proposed" column
above -- and explicitly *"I prefer Niv-Mizzet Combo."* *"(because the dash is in the actual name)"* -- the general rule:
a name taken from a card keeps the card's own spelling and punctuation -- keeping `Anti-Lifegain`'s
hyphen, "Lifegain" as one word, leaving the archived `v<N>-<slug>/` folders as they are, and aligning the two
stems to their folders (`Soldiers.cod`, `Vampires.cod`). Not yet scheduled: the USER wants other work first.

Hazards specific to this batch: (a) `burn` -> `Burn` and `treasure_hunt` -> `Treasure Hunt` are the
cheapest (slugs unchanged) but `burn` is a CASE-ONLY rename, and the checkout sits on a case-insensitive
Windows mount: do it as two moves (`burn` -> `burn.tmp` -> `Burn`) and expect every Windows clone to need
care; (b) the seed-base re-hash of the full batch moves ONE unrenamed key,
`fungus_v1_sporesower_shelldweller` (collision probing), so either accept that archived variant's new base or
pin it; (c) archived version folders and their reference folders (`references/<Deck_With_Underscores>/v1-...`)
move with their decks; (d) providers route by card signature, so no play change is expected, but
`test_pirates_provider.cpp` pins every folder name.

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
