# SelesnyaLifegain v2 becomes the shipped list (2026-10-22)

`v2-bogbeast-heroes` was promoted to `decks/SelesnyaLifegain/` and the original list was archived as
`decks/SelesnyaLifegain/v1-avatar-alchemist/`. The mechanics follow the KittyEquipment v3 promotion
(`kitty-v3-promotion-2026-10-14.md`, commit `34cfa9050`); its three traps were each checked here.
Unlike Kitty, **no suite rows were dropped**: dropping the original list's rows is the USER's call.

## The measurement that authorized it

From `analysis-SelesnyaLifegain-v2-bogbeast-heroes.md` (HEAD:src `ec2de5145`, PGO+LTO). Each arm ran its
OWN profile, keep table and value sidecar out of its own directory at its own play setting (shipped
d5 b20, v2 d6 b20), `provider=SelesnyaLifegain` on all 40 jobs; one pooled batch, 10 paired blocks x
4,000 games per arm per format, `max_turns` 8.

| format | shipped | v2 | delta | t | blocks better | unwon | ms/game |
|---|---|---|---|---|---|---|---|
| std | 5.1341 | 4.5908 | **-0.5433** | -127 | 10/10 | 0.48% -> 0.20% | 123.4 -> 40.6 |
| 2HG | 5.4255 | 4.8892 | **-0.5363** | -73 | 10/10 | 0.99% -> 0.48% | 216.3 -> 75.4 |

v2 is also ~0.33x the cost per game. The numbers cover the list AND its refitted artifacts (including
the leafless nlfit search shape), not the cards alone.

## What moved

| from | to |
|---|---|
| `decks/SelesnyaLifegain/SelesnyaLifegain.{cod,profile.json,value.json,keepmodel.exhaustive.profile.json.gz,keepmodel.exhaustive.raw.json.gz}` (original list) | `decks/SelesnyaLifegain/v1-avatar-alchemist/` |
| its untracked generation leftovers (`.raw.json.journal`, `.raw.json.probe`, `.raw.json.slow.log`, `.keepmodel.gencache.json`) | same folder (moved, not deleted) |
| `decks/SelesnyaLifegain/v2-bogbeast-heroes/SelesnyaLifegain.*` (5 tracked + `.journal.bak1/.bak2` + gencache) | `decks/SelesnyaLifegain/` (folder removed; moved, not duplicated) |
| `references/SelesnyaLifegain/claude_s*_gi*.json` (10) | `references/SelesnyaLifegain/v1-avatar-alchemist/` |
| `references/suboptimal/SelesnyaLifegain/claude_s4_gi3.json` (1) | `references/suboptimal/SelesnyaLifegain/v1-avatar-alchemist/` (server.js `relSub` puts a versioned list's suboptimal games one level deeper) |

The slug names what distinguishes the original: Accomplished Alchemist and 4 Verdant Sun's Avatar,
both cut by v2. Every reference was a pure rename (same blob; the JSON carries no deck path), 657
reference files before and after, all 11 Selesnya hashes identical. No reference had been saved under
`v2-bogbeast-heroes/`, so **the shipped list has zero hand-played references** until some are played --
the same honest state Kitty, Mirrorwing and CritterLifegain had at their promotions.

Generation scratch, so a future run on `decks/SelesnyaLifegain` resolves to v2 and never resumes v1:
`logs/vlq_selesnyalifegain` -> `logs/vlq_selesnyalifegain_v1_avatar_alchemist` (the name valueleaf.sh
derives for the archive), `logs/SelesnyaLifegain_mullgen` -> `logs/SelesnyaLifegain_v1-avatar-alchemist_mullgen`
(mullgen.sh's key for the archive), `logs/eval/SelesnyaLifegain.v1-shipped-19elf.value.STAGED.json` ->
`logs/eval/SelesnyaLifegain.v1-avatar-alchemist.value.STAGED.json`. `logs/eval/SelesnyaLifegain.value.STAGED.json`
is v2's (written by the v2 queue) and stays, as do `logs/vlq_selesnyalifegain_v2_bogbeast_heroes` and
`logs/SelesnyaLifegain_v2-bogbeast-heroes_mullgen` (history; their stamps name the v2 key, so they are
not resumed by a top-level run). The keep journal backups now beside the top-level list are v2's.

## Suite keys: one key per decklist

| key | before | after |
|---|---|---|
| `selesnya` | top level (the original list) | `v1-avatar-alchemist/` -- same list, same file content |
| `selesnyav2`, `selesnyav22hg` | `v2-bogbeast-heroes/` | top level -- same list, same file content |

No key changed lists, so no GT moved. There is no `selesnya2hg` row. The shipped key keeps its `v2`
suffix for the reason `kittyv3` kept its suffix: renaming would reuse names the original's GT occupies.
`test/suite_cost.json` is keyed by suite key, so it needs no edit.

## The three Kitty traps

1. **`buckets.json` (resolves in the deck's OWN dir, never walks up).** Neither list has one; nothing to
   carry.
2. **The 2HG row is invisible to a grep for the deck key.** `selesnyav22hg` resolves through
   `regression.sh`'s suffix strip to `selesnyav2`, so it follows the promoted list automatically; there
   is no `selesnya2hg`.
3. **Tracked files naming the top-level path meant the original list.** Decisions:

   | file | decision |
   |---|---|
   | `test/scenarios/selesnya_human_karoo_default_rest_control.json`, `..._landat0_rest_lethal.json`, `..._landat0_validate.json`, `selesnya_karoo_first_rest_bogbeast_lethal.json`, `..._control.json` | -> archive. Selesnya Sanctuary (the Karoo) lines on v1's numbering; v2 plays no Sanctuary |
   | `scripts/bp_open/make_ab.py` (`selesnya` entry) | -> archive; mirrors the suite key |
   | `test/tools/intent_replay.py` (usage example) | -> archive; the example plays Alchemist + Avatar |
   | `test/unit/test_dork_growth_x.cpp` (comment naming the suboptimal reference) | -> the reference's new path |
   | `docs/design/analysis-SelesnyaLifegain.md`, `trailing-activation-payment-hole.md`, `selesnya-keepgen-tail.md` (repro commands) | -> archive, so each still reproduces the v1 game it describes; the analysis ledger gained an ARCHIVED banner |
   | narrative mentions of past runs (`mullgen.sh run decks/SelesnyaLifegain fast`, `suite_gate.py --cost decks/SelesnyaLifegain`, `./build.sh pgo decks/SelesnyaLifegain`) in the v1 ledger and `selesnya-keepgen-bulk-cost.md` | left: dated records of what was run that day |
   | `test/unit/test_pirates_provider.cpp` (`{"SelesnyaLifegain","SelesnyaLifegain"}` routing pin) | FOLLOWS the shipped list, deliberately: v2 must keep routing to `SelesnyaLifegainProvider`, which the unit suite now asserts on v2 |
   | `tools/mulligan/decks.js` | regenerated (`build_deck_list.js`); adds the archive entry (and picked up six decks added since it was last generated) |
   | `test/tools/stompy_order_ab/results/manifest.new.json` | left: a results artifact with absolute paths into a long-gone worktree |

## Verification

Neither decklist changed, so every key had to reproduce its GT byte-identically (avg AND play digest):

| gate | result |
|---|---|
| smoke | 144 passed, configs changed 0 / unchanged 144; re-run after rebasing onto 1aa6cc32a (engine changes upstream): 148 passed, configs changed 0 / unchanged 148 |
| regression | 276 passed, configs changed 0 / unchanged 276; after the rebase: 282 passed, configs changed 0 / unchanged 282 |
| references (`viewer_protocol_check.py --strict`, in the regression tier) | 651 refs (656 after the rebase: +5 NivMizzetCombo): 228 ok (233), 423 repaired, 0 play-drift / shuffle-dead / board-diverged / enum-gap / mull-drift / contract-fail; all 10 v1 references resolve under `SelesnyaLifegain/v1-avatar-alchemist/` |
| scenarios | 302 passed (306 after the rebase), 0 failed (the five repointed `selesnya_*` scenarios reproduce their recorded outcomes) |
| unit (`mtg-test`) | 1884 test cases, 3,516,576 assertions, SUCCESS (incl. the provider-routing pin, now on v2) |
| `test/check_gt_logs.py` | consistent 991 (1017 after the rebase), STALE 0, missing 0 |
| `scripts/check_references_safe.py` | OK, before and after the push |
| CI (run 39825637387) | ubuntu success, windows success, Linux/Windows determinism parity success |
| `ref_bench.py` on the archive | 10 refs, human 5.600, search 5.400, 0/10 short |
| viewer `listDecks()` | `SelesnyaLifegain` (refs 0, keep table + value sidecar present) and `SelesnyaLifegain  —  v1-avatar-alchemist` (refs 10) |

`test/deck_case_resolution_check.py` fails 2 synthetic checks (`references/Foo_Bar`), identically on the
pre-promotion tree; unrelated to this move.

`test/ref_bench.json`: the `selesnyalifegain` entry was v1's bench (n=10) under the key that now names
v2, which would lend v1's evidence to v2 (the borrowed-evidence case `server.js` guards against). It
was re-keyed by re-benching the archive (`ref_bench.py --deck selesnyalifegain_v1_avatar_alchemist`)
and removing the stale shipped-key row. KittyEquipment's `kittyequipment` row has the same defect
(n=16 = v1's count, the shipped v3 has no references); it was left for the USER to rule on.

## Open

* Whether to drop the original list's `selesnya` rows (smoke 3, regression 5, overnight 12, plus the
  gen-gate cells) is the USER's call.
* `require('tools/play/server.js')` + `listDecks()` is not side-effect free: a stale bench stamp spawns
  `ref_bench.py --stale-only --json test/ref_bench.json` per deck in the background. An agent checking
  the deck list that way rewrites the bench cache and runs engine games.

## v1 suite rows retired (2026-10-22)

USER, once the v1-vs-current summary (`v1-vs-current-selesnyalifegain.md`: -0.5433t standard / -0.5363t
2HG, 10/10 blocks) was approved: *"Once we're happy with it we'll remove the old list from the regression
test."* The `selesnya` key is gone from all three tiers (3 smoke, 7 regression, 14 overnight rows incl. the
unbudgeted backstop), with its 24 GT keys, gt_logs and `suite_cost.json` entry. Tiers 145 / 275 / 573,
`check_gt_logs` consistent at 993. The archived list's ten references are still replayed and benched on
`v1-avatar-alchemist/` (every reference is played, USER 2026-10-22); the archive now carries no suite rows,
so `suite_gate --require` blocks generation on it, like every other archived list.
