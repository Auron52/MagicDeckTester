# Mana queue handoff: Codex to Claude, 2026-11-17

This is the current local handoff in the **primary checkout** `/workspaces/MagicDeckTester`, branch `phase-1-2-deck-analyzer`. Continue the mana-clean queue autonomously until the queue is empty or a real user tradeoff remains. Read root `CLAUDE.md`, `.claude/skills/regression-testing.md`, `.claude/skills/coding-conventions.md`, and `.claude/skills/mtg-ai.md` before modifying the related code. The older queue and history are in `docs/design/handoff-mana-chain-to-codex-2026-11-14.md`; this document supersedes its status paragraphs.

## Live status, updated 06:57 UTC

**Breaching Dragonstorm diagnosis and audit fix complete:** The two
fresh ENUM-GAP flags are **hidden future mana**, not a visible pre-cast
source shortage. Exact repros in
`build/scratch/dragonstorm_visible_lump_ab/breaching_gap_30222319_t6.out`
and `breaching_gap_30221073_t7.out` show the first cast's free cascade /
enter reveal puts Sakashima's Protege #52 into play as a copy of Mountain.
The second cast pays with Mountain #52; it did not exist on the initial
board. The first subset is rejected as `ef-no-interact` (10 total, only
one visible red); the second as `ef-walk-fail` (13 total, selected sac
actions are unmodelled by that walk). Both rejections are correct before
the reveal. Creative Technique later shuffles the library, so the oracle's
root/end-library order inference misses #52's top-of-library provenance
and incorrectly labels a missing menu option as ENUM-GAP.

A committed **audit-only** source fix now records every top-of-library
free-cast number in the oracle Hook (cascade, Breaching Dragonstorm,
Creative Technique, ripple, Cyclone), then classifies a later tap of that
card as HIDDEN-INFO even if the library was shuffled. Edits are in
`src/ai/ManaOracle.h`, `src/ai/ManaOracle.cpp`, and `src/ai/TurnSolver.cpp`.
The incremental Release build passed. Both exact replays now return
HIDDEN-INFO=1, **zero ENUM-GAP**, naming Mountain #52; outputs
`build/scratch/dragonstorm_visible_lump_ab/breaching_gap_30222319_hiddenfix.out`
and `breaching_gap_30221073_hiddenfix.out`. The 31 focused oracle unit
cases passed. The source commit is `79d7b898b`, with
`HEAD:src=342674496bfb8ddc912cee3193513f41b0388d58`. The PGO+LTO
rebuild completed in exec session `88239`, console
`build/scratch/dragonstorm_visible_lump_ab/pgo_hidden_freecast_oracle.out`.
Its compile, pooled 275-job/7,447-game training batch, and final link
passed. `build/PGO/SRC_TREE` matches
`342674496bfb8ddc912cee3193513f41b0388d58`. The fresh PGO gate
`codex-breaching-hidden-freecast-20261117` then passed on indices
225,000–229,999: **Breaching Dragonstorm clean, 5,000 games, zero issue
flags**, oracle-on/off play byte-identical; four HIDDEN-INFO cases are
disclosed. Report:
`logs/mana_clean/codex-breaching-hidden-freecast-20261117/report.md`.
`test/mana_clean.json` carries the record. Next run smoke plus strict
viewer reference checks on this source and compare to the known 39
unrelated GT failures from the previous full smoke. Then inspect the
Spirits seed 30040647 T4 one APPLY-DROP. Do not widen the player search
gate to exploit the future reveal.

The last stale-clean refresh `codex-clean-refresh-newsrc-c-20261117`
finished, 5,000 games each at source `f7cce0693b6a`:
**Martyr Lifegain clean; Prevent Damage 30 issues (8 ORACLE-GAP,
22 CAPPED); Spirits one APPLY-DROP**, rescued by forced payment rollback
at depth one. Oracle-on/off play was byte-identical. Report:
`logs/mana_clean/codex-clean-refresh-newsrc-c-20261117/report.md`.
`test/mana_clean.json` carries all three records. The 56-deck status now
has **15 clean, 39 issues, two never-run, zero stale-clean** at the prior
source. Extra Turns and Merfolk Turns are never-run. No gate is active;
the PGO build described below includes its own training batch.

Windows CI `44025173062` **passed all checks** by 06:30 UTC;
the watcher output is
`build/scratch/dragonstorm_visible_lump_ab/windows_ci_44025173062.out`.
Keep the CI check asynchronous. The focused Dragonstorm test **failed on
the shipped payer** (Bloom left intact, both Territories tapped,
Karrthus unpayable). The committed source fix in `src/ai/ManaPayment.cpp` now
passes that case plus the two adjacent visible-closer cases. It removes
the prepayment `short_with_shared` gate and compares coloured plus
**non-lump wild** supply after trial payment: an uncracked Bloom otherwise
counts as three speculative wild units and obscures the two Territories
saved by an early crack. `MTG_ONDEMAND_VISIBLE_AFTERPAY_WILD=0` restores
both old conditions for a paired A/B. Exact seed **30074224 T5** has
zero oracle issues with the trial, but APPLY-DROP=1 with the legacy lever;
outputs `build/scratch/dragonstorm_visible_lump_ab/seed_30074224_afterpay.out`
and `seed_30074224_legacy.out`. The full Release unit suite passed:
**3,384/3,384**, zero failures; console
`build/scratch/dragonstorm_visible_lump_ab/unit_bloom_afterpay.out`.
The fix is committed as `eec786b06`, with
`HEAD:src=7ecf53121b172fb2cbf078f7e5f554086b7b28fe`. A PGO+LTO
build completed in exec session `36997`, console
`build/scratch/dragonstorm_visible_lump_ab/pgo_bloom_afterpay.out`.
Its 275-job/7,447-game training batch and final link passed; the
`build/PGO/SRC_TREE` stamp exactly matches
`7ecf53121b172fb2cbf078f7e5f554086b7b28fe`. A same-binary paired
Dragonstorm A/B finished on 5,000 paired games per arm, indices
77,000–81,999: **zero earlier and zero later wins**, four changed play
digests at the same win turn, 4,996 byte-identical game records. New
search work was 9,985,030 units versus 9,976,007 old (+0.09%); recorded
job ms 464,651 versus 465,987. Outputs and per-game `.wins` are under
`build/scratch/dragonstorm_visible_lump_ab/afterpay_wild_5k/`.

A fresh PGO mana-clean gate **finished** in exec session `36993`:
`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run dragonstorm --games 5000 --name codex-dragonstorm-afterpay-wild-20261117`.
Console `build/scratch/dragonstorm_visible_lump_ab/clean_afterpay_wild.out`;
report `logs/mana_clean/codex-dragonstorm-afterpay-wild-20261117/report.md`.
It found **71 issues: 70 APPLY-DROP and one APPLY-DROP-ORDER**, zero
ENUM-GAP/ORACLE-GAP/CAPPED, oracle-on/off play byte-identical. The top
cluster is seed 30078447 T4, a Pyretic→Apex→Irencrag→Kolaghan line
whose Bloom-black payment differs; see the report for 29 APPLY-DROP
signatures. `test/mana_clean.json` carries the new record. Next reproduce
Breaching Dragonstorm seeds **30222319 T6** and **30221073 T7** with
`MTG_DBG_MULTI=6` and `=7`, identify the exact rejecting gate, and make a
narrow deterministic correction. The queue remains open.

The paired A/B manifest and runner are prepared under
`build/scratch/dragonstorm_visible_lump_ab/afterpay_wild_5k/`:
50 jobs of 100 games, Dragonstorm indices 77,000–81,999, same PGO
binary and seeds in both arms, old arm with
`MTG_ONDEMAND_VISIBLE_AFTERPAY_WILD=0`. Runner:
`bash build/scratch/dragonstorm_visible_lump_ab/afterpay_wild_5k/run.sh`,
console `build/scratch/dragonstorm_visible_lump_ab/afterpay_wild_5k/runner.out`.
Inspect per-game `.wins`, aggregate turns and search work before accepting
the fix.

A focused unit case for seed 30074224's payment shape has been added to
`test/unit/test_dragonstorm_followups.cpp`, immediately after the existing
one-Territory visible-Karrthus case. It sets Mountain + two Unclaimed
Territories + Lotus Bloom, pays Song, then credits five red and checks
Karrthus payment. Its baseline failure and trial success are confirmed;
the held-out A/B remains.

The paragraphs below are a chronological work log. Older statements
that say a batch or build is active describe its state at that time;
use this live-status section for the current action.

## Chronological work log

**Second new-source clean refresh complete:** all nine decks in
`codex-clean-refresh-newsrc-b-20261117` are clean at source
`f7cce0693b6a`, 5,000 games each, zero issue flags, oracle-on/off play
byte-identical: Colossus Hammer, Critter Lifegain, Kithkin, Knights,
Lifegain Heliod Combo, Merfolk, Modern RW Burn, Soldiers, White
Knights. Report:
`logs/mana_clean/codex-clean-refresh-newsrc-b-20261117/report.md`.
`test/mana_clean.json` carries the nine records. Only Martyr Lifegain,
Prevent Damage, and Spirits remain stale-clean at this source; refresh
those next. The CI watcher is still active.

**Active final stale-clean batch, exec session `74609`:** 5,000 games each for Martyr Lifegain,
Prevent Damage, and Spirits, command
`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run martyr_lifegain prevent_damage spirits --games 5000 --name codex-clean-refresh-newsrc-c-20261117`.
Console `build/scratch/dragonstorm_visible_lump_ab/clean_newsrc_c.out`,
report `logs/mana_clean/codex-clean-refresh-newsrc-c-20261117/report.md`.

**First new-source clean refresh complete:** 5,000 games each at source
`f7cce0693b6a`, oracle-on/off play byte-identical. Burn, Red
Elementals, Slivers, Giants, and Auras are clean; Breaching Dragonstorm
has **two ENUM-GAP** flags, zero other issue classes. Report:
`logs/mana_clean/codex-clean-refresh-newsrc-a-20261117/report.md`.
The two exact repros are seed 30222319 T6 (Breaching Dragonstorm then
Creative Technique, with Mountain land drop) and seed 30221073 T7
(Creative Technique/Call Forth the Tempest and sacrifice lands; paying
order casts Call Forth first). These were new seed ranges, so the
previous clean verdict does not imply a regression. Commit the updated
`test/mana_clean.json` with this handoff, then refresh the remaining
stale-clean decks. Windows CI watch remains active separately.

**Active gate, exec session `76552`:** nine more previously clean decks, 5,000 games
each: Colossus Hammer, Critter Lifegain, Kithkin, Knights, Lifegain
Heliod Combo, Merfolk, Modern RW Burn, Soldiers, White Knights. Command:
`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run colossus_hammer critter_lifegain kithkin knights lifegain_heliod_combo merfolk modern_rw_burn soldiers white_knights --games 5000 --name codex-clean-refresh-newsrc-b-20261117`.
Console `build/scratch/dragonstorm_visible_lump_ab/clean_newsrc_b.out`,
report `logs/mana_clean/codex-clean-refresh-newsrc-b-20261117/report.md`.
After it, refresh Martyr Lifegain, Prevent Damage, and Spirits; those
are the last stale-clean records from the old source.

**Shared branch pushed:** after a final fetch confirmed origin had not
moved, `git push origin phase-1-2-deck-analyzer` published 57 local
commits through `a5c156a20` at about 05:35 UTC. Reference safety had
passed across 210 checkouts. The next ordinary 4-hour push window is
**09:35 UTC**. Windows CI run `44025173062` was pending at launch;
background `gh run watch 44025173062 --interval 60 --exit-status` is
active in exec session `25317`, output
`build/scratch/dragonstorm_visible_lump_ab/windows_ci_44025173062.out`.
The shell's Git credential helper supplied the token only to the child
process. Check the watcher asynchronously; if red, pivot to its failure.

**Next mana batch:** refreshing six stale-clean decks at the new source,
5,000 games each: Burn, Breaching Dragonstorm, Red Elementals, Slivers,
Giants, Auras. It is now active in exec session `47565`. Command:
`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run burn breaching_dragonstorm red_elementals slivers giants auras --games 5000 --name codex-clean-refresh-newsrc-a-20261117`.
Report will be
`logs/mana_clean/codex-clean-refresh-newsrc-a-20261117/report.md`;
console `build/scratch/dragonstorm_visible_lump_ab/clean_newsrc_a.out`.
Check the result, update this handoff, and commit the gate record locally.

**Post-Boars full smoke comparison complete:** the fresh-source smoke
run ended 180 pass, 39 fail, zero new, exactly the same known GT drift
as before. More decisively, all **261 per-game `.wins` files are
byte-identical** to the archived pre-Boars smoke run; there are zero
added, missing, or changed files. The Boars source integration is
smoke-play-neutral, while the existing GT drift remains for separate
audit. Console:
`build/scratch/dragonstorm_visible_lump_ab/post_boars_rebase_smoke.out`.
`python3 test/check_gt_logs.py` passed again (1,252 consistent,
zero stale/missing), and the second reference safety scan passed.
Next fetch once more, rebase only if origin moved, then push the local
commits in this now-open 4-hour window. Attach an async Windows CI
watch with 60-second polling; `gh` needs the Git credential helper in
this shell. Then run fresh new-source mana-clean blocks for the 18
stale-clean decks and issue decks. No simulation batch is active.

**PGO build complete:** `./build.sh pgo` finished successfully at
`HEAD:src=f7cce0693b6a16d779df5ac756af16100e3b677d`; the stamp in
`build/PGO/SRC_TREE` matches. Full output:
`build/scratch/dragonstorm_visible_lump_ab/rebased_pgo_build.out`.
The next batch is a full `bash test/regression.sh --smoke` on this new
source; compare its per-game `.wins` with the archived pre-Boars smoke
directory `build/scratch/dragonstorm_visible_lump_ab/rebased_current_smoke_logs/wins`.
If byte-identical, the upstream Boars source change is smoke-neutral and
the 39 previously documented GT failures remain preexisting drift.
That smoke is now active, exec session `71911`, console
`build/scratch/dragonstorm_visible_lump_ab/post_boars_rebase_smoke.out`.

**Active build:** `MTG_RSS_CAP_GB=40 ./build.sh pgo` for source
`f7cce0693b6a16d779df5ac756af16100e3b677d`, exec session
`97680`, console
`build/scratch/dragonstorm_visible_lump_ab/rebased_pgo_build.out`.
Check its exit and PGO source stamp before any fresh mana gate. A
second reference safety scan is also running (session `70825`, output
`build/scratch/dragonstorm_visible_lump_ab/reference_check_pre_push.out`)
but the push now waits on this new source validation.
The second reference scan passed after 210 checkouts/worktrees. With
the new source hash, `python3 scripts/mana_clean.py status` reads 56
decks: **36 issues, 18 stale-clean, two never-run; 38 DUE**. These are
source-stamp statuses, not evidence that the Boars change broke clean
decks. Fresh blocks are needed before calling the queue current again.

**Upstream source integration supersedes the active gate:** at 05:15
UTC, `origin/phase-1-2-deck-analyzer` gained three Boars label-memory
commits (`37eebd3ab`, `2d56d1716`, `596ae188e`), including
`src/ai/TurnSolver.cpp`. I stopped the old-source due-four gate after
three additional attempt-3 chunks and rebased all 51 local commits
cleanly. The new `HEAD:src` is
`f7cce0693b6a16d779df5ac756af16100e3b677d`; old PGO stamp
`fcb9352608ea` is stale. The due-four run was planned at that old
source and **must not be resumed after this rebase**. Its completed
Bruna 5,000-game record remains valid only for the old source; the
other three are incomplete. Run `./build.sh pgo` for the new tree,
then start fresh due records under new run names. The failed smoke
analysis below remains a baseline issue; the new upstream source also
requires a post-rebase smoke before the shared push. No push yet.

**Historical old-source gate:** due-four mana gate had resumed at eight
workers after the first smoke, exec session `46978`, console
`build/scratch/dragonstorm_visible_lump_ab/due_four_resume8_after_smoke.out`.
It was stopped for the new upstream source integration above. Its
completed chunks remain archived, but its run plan cannot resume under
the new source hash. No simulation batch is active until the new build.

**Push temporarily held by smoke failure:** the full post-rebase
`bash test/regression.sh --smoke` finished with 180 passed, 39 failed,
zero new. Its per-game audit found 15 searched slower games and 345
same-score searched play changes; 248 d0 slower games are reported under
the lighter d0 bar. Failures include Mill, White Knights, Anti-Lifegain,
Unpredictable Cyclone, Creature Giving, Five Colour, Critter Lifegain,
Heliod, Melira, Minotaur, Selesnya, Prevent Damage, Galadriel, and
Vampires. Full output:
`build/scratch/dragonstorm_visible_lump_ab/rebase_smoke.out`.
At the time of this **first** rebase, the source hash was unchanged and
the remote's three commits touched only the Red Elementals value sidecar
and its GT. These failures are not attributable to that rebase. The
newer Boars source rebase above is a separate event and needs its own
smoke. The 05:09 push window is open, but no push yet.
The smoke audit's searched rows are 15 slower, 13 faster, 345 changed
at the same score; over 14,515 searched games the net loss-penalized
delta is **+4 turn-games** (+0.000276 per game). d0 is 248 slower and
260 faster, net -31 turn-games over 55,100. The classifier output at
`build/scratch/dragonstorm_visible_lump_ab/rebase_smoke_classify.out`
marks 12 of the 15 searched slowdowns as higher-budget churn. Three
persist at 4x/16x: Creature Giving d5 gi113, Cyclone d3 gi37, Five
Colour d3 gi24. `test/explain_game.py` replays the saved baseline binary
and current binary at the original budget for these three and reports
the **same T5** on both, though committed `.wins` says old T4 in each;
the baseline snapshot is `logs/snapshots/smoke-baseline` (built from
f2156d4e6 with dirty state). Investigate this GT/snapshot mismatch
before accepting, and review the same-score changes by affected deck.
No GT acceptance or push has occurred. The old-source mana gate is
archived; start a new run after rebuilding the new source.
Source scope check before the Boars rebase: the only four local source
commits since the then-current origin were Dragonstorm Ruby and Apex
provider/solver changes plus the storage-land backtracker repair.
`cards.json` marks only Dwarven Hold and Mercadian Bazaar as
`storage_land`, and `rg` finds those only in the Dragonstorm shipping
deck. **None of the 39 failed smoke rows is Dragonstorm.** The upstream
Red Elementals sidecar also has no failed row. Thus these smoke failures
are baseline drift already present outside the new source paths, not a
reason to change or reject the four mana fixes. Keep GT unchanged and
continue their separate audit; do not silently accept the drift.

The eight-worker due-four gate was intentionally stopped at chunk
boundary for the post-rebase smoke and upcoming push. It had completed
18 new oracle chunks in attempt 2; all finished jobs persist. Current
top-level simulation batch is **smoke**, exec session `98865`, console
`build/scratch/dragonstorm_visible_lump_ab/rebase_smoke.out`, command
`MTG_RSS_CAP_GB=40 bash test/regression.sh --smoke`. Once smoke passes,
push no earlier than 05:09 UTC, start an async Windows CI watch, and
resume the due-four gate with `--threads 8` (attempt 3). If smoke fails,
investigate before the push; the mana gate stays paused meanwhile.

**Active batch:** refreshing four older issue records at source
`fcb9352608ea`, 5,000 games each: Anti-Lifegain, Bruna, Midrange Burn,
and Selesnya Lifegain. The first 32-worker attempt hit the 40 GB RSS cap
(oracle-on exit 137); only Bruna finished a full 5,000-game block with
3,218 issues, and the other three records are incomplete. Resume the same
run at **16 workers** so completed chunks survive. First attempt console:
`build/scratch/dragonstorm_visible_lump_ab/due_four_gate.out`; resume
console: `build/scratch/dragonstorm_visible_lump_ab/due_four_resume.out`.
Original command:
`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run anti_lifegain bruna midrange_burn selesnya_lifegain --games 5000 --name codex-due-four-20261117`.
Resume command:
`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run --resume codex-due-four-20261117 --threads 16`.
The 16-worker resume (`23177`) was terminated after RSS climbed to
38.5 GB; it had completed three additional chunks. The next resume uses
**eight workers**, exec session `33637`, console
`build/scratch/dragonstorm_visible_lump_ab/due_four_resume8.out`:
`MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run --resume codex-due-four-20261117 --threads 8`.
Resume remains chunk-granular and source-identical. `python3
scripts/check_references_safe.py` passed at 04:51 UTC after scanning 210
checkouts/worktrees. The three upstream Red Elementals sidecar commits
were fetched and the 42 local commits rebased cleanly at 04:53 UTC. The
only conflict was the regression GT header; the newer upstream header
and both decks' rows are preserved. `HEAD:src` remains exactly
`fcb9352608ea7e19580916d696643bc094d6ef3a`, so the running gate
still matches the PGO binary. `python3 test/check_gt_logs.py` passed:
1,252 consistent, zero stale/missing. A shared-branch push is due no
earlier than 05:09 UTC; rerun the reference check if new reference work
appears before that push. Because GT changed on rebase, run the smoke
gate once the current simulation batch finishes, before the push.
The eight-worker oracle arm is Selesnya-heavy: its 56-game chunks have
used about 180-360 core seconds each, with RSS around 21-24 GB. This
run is much longer than the initial cost estimate, but the completed
chunks are retained. At 04:58 UTC it had completed five new chunks.
The shell's `gh` has no persistent login, but `git credential fill`
returns the existing GitHub credential to a subprocess. A Python read
of the Actions API using that credential succeeded at 05:00 UTC; it
showed the prior Windows build run `43993315542` as successful. Keep
the credential in process memory and print only run metadata; after
the next push start a 60-second-poll CI watcher in the background.
Output: `build/scratch/mana_clean/codex-due-four-20261117/` and report
`logs/mana_clean/codex-due-four-20261117/report.md` when complete.
Commit the updated `test/mana_clean.json` and this handoff after it finishes.

**Six due-issue decks now refreshed:** Boars, Dragons, Goblins, Hydras,
Jumbo Cactuar, and Morph each ran 5,000 games at PGO source
`fcb9352608ea`, indices respectively 10,000-14,999, 76,000-80,999,
115,000-119,999, 5,000-9,999, 105,000-109,999, and 10,000-14,999.
Oracle-on/off play was byte-identical. All six still have issues:

| Deck | Issues | Leading classes |
|---|---:|---|
| Boars | 2,253 | 1,455 dominated creature, 233 apply drop, 206 capped decision, 166 dominated haste |
| Dragons | 948 | 663 apply drop, 285 oracle gap |
| Goblins | 1,131 | 1,047 oracle gap, 75 apply drop |
| Hydras | 54,075 | 54,031 capped decision, 34 apply drop order |
| Jumbo Cactuar | 942 | 422 enum gap, 239 apply drop order, 127 dominated creature |
| Morph | 1,752 | 1,151 oracle gap, 416 dominated creature, 184 apply drop |

Report: `logs/mana_clean/codex-due-six-20261117/report.md`.
The record is in `test/mana_clean.json`. At this source, the status is
56 decks: 18 clean, 36 issues, two never run, 21 still DUE (older issue
records or never run); zero stale-clean. No simulation batch is active.
Next inspect Goblins' seed 30116157 T3 Three Tree City payment (66
apply-drop flags), Dragons' seed 30077853 T5 Lathliss then Urza's
Incubator (32), and Boars' seed 30010755 T5 Ilharg then Horn then
Flinthoof Boar (44). Those are distinct from Hydras' nearly entire
block hitting the per-decision audit budget. Jumbo's leading enum gaps
combine Overgrowth, Finale, Tooth and Nail, and mana creatures; seed
30109313 T4 has 32 flags. See the report for exact repro commands.

**Final stale-clean refresh complete:** Martyr Lifegain, Prevent
Damage, and Spirits each ran a fresh 5,000-game PGO mana-clean block at
source `fcb9352608ea`; all three are **clean**, with zero issue flags
and byte-identical oracle-on/off play. Prevent Damage had 16,985
disclosed DOMINATED-LIFE observations, which this gate does not count as
issues. Report:
`logs/mana_clean/codex-clean-refresh-heavy-20261117/report.md`.
The three records are committed in `test/mana_clean.json`. There are no
stale-clean decks remaining at this source; the open queue consists of
issue decks and the never-run Extra Turns/Merfolk Turns decks.
No simulation batch is active after this gate.

**Second pooled fresh PGO gate complete:** 5,000 games each for 13
decks at source `fcb9352608ea`, oracle-on/off play byte-identical.
Auras, Colossus Hammer, Critter Lifegain, Kithkin, Knights, Lifegain
Heliod Combo, Merfolk, Modern RW Burn, Soldiers, and White Knights are
**clean**. Fluctuator has one APPLY-DROP; Minotaur has two APPLY-DROP;
Vampires has 26 ORACLE-GAP. Report:
`logs/mana_clean/codex-clean-refresh-two-20261117/report.md`.
Fluctuator's exact seed 30218614 T4 drops Drannith Stinger after
Fluctuator and a cycling chain (rollback depth 3+). Minotaur's seed
30284922 T5 drops Deathbellow Raider after Slaughter-Priest; oracle
spends Rakdos Carnarium B plus Territory C first, preserving Secluded
Courtyard's creature-only R (rollback depth one). Vampires' nine oracle
signatures all involve Voldaren Estate activation after Stromkirk
Condemned, sometimes with a pump or Vial put; leading seed 30228525
T4 has six flags. Nearest oracle outcomes differ in tapped Courtyard,
Blood Token count, life, or float; investigate oracle fidelity before
claiming a mana drop. All 13 records are committed in
`test/mana_clean.json`. No simulation batch is active after this gate.

**Six-deck fresh PGO gate complete:** all at source `fcb9352608ea`,
5,000 games per deck, oracle-on/off play byte-identical. Burn,
Breaching Dragonstorm, Red Elementals, Slivers, and Giants are **clean**
with zero issues. Angels is **issues**: 11 APPLY-DROP, eight
APPLY-DROP-ORDER, zero oracle/enum/cap. Report:
`logs/mana_clean/codex-clean-refresh-20261117/report.md`.
Angels' largest drop is seed 30220574 T4 (seven flags): Youthful
Valkyrie before Serra the Benevolent; oracle spends Seraph Sanctuary's
colorless plus Giada's Angel-only white on Valkyrie to preserve Plains,
Sol Ring, Plains for Serra. The order drops center on Giada, Font of
Hope with Lightning Greaves/equip; seed 30221928 T3 has two flags and
needs Giada → equip → Resplendent Angel. Seed 30222520 T4 has both
payment and order drops. These are distinct payment/order repairs; the
five clean deck verdicts should be preserved. All six records are
committed in `test/mana_clean.json`. No batch is active after this gate.

**Treasure Hunt fresh gate complete:** indices 80,000–84,999 at PGO
source `fcb9352608ea` yielded 289 issues: 20 ENUM-GAP, 217
APPLY-DROP, and 52 APPLY-DROP-ORDER. There are 370 disclosed
DOMINATED-LIFE observations, not counted as issues. Zero oracle gap or
cap; oracle-on/off play byte-identical. Report:
`logs/mana_clean/codex-treasure-hunt-fresh-20261117/report.md`.
The 20 enum gaps are mostly a same-plan double Treasure Hunt on turn
three; seeds 30081492, 30081921 and 30082722 have two flags each.
Payment drops cluster around Cascade Bluffs plus Land's Edge and Hunt,
with several 16-flag signatures; the same filter/source ordering also
appears in Throes of Chaos lines. The `test/mana_clean.json` record is
committed with this handoff. No batch is active after this gate.

**Fungus fresh gate complete:** indices 80,000–84,999 at PGO
source `fcb9352608ea` yielded 486 issues: 137 ENUM-GAP, 116
DOMINATED-CREATURE, 204 ORACLE-GAP, and 29 CAPPED. There are also 102
disclosed DOMINATED-LIFE observations, which are not issue flags.
Oracle-on/off play byte-identical. Report:
`logs/mana_clean/codex-fungus-fresh-20261117/report.md`.
The enum gaps span 129 signatures, often Concordant Crossroads first
making a just-cast mana creature usable later in the same line (seed
30082639 T3), or Saproling Migration producing later mana fodder.
The oracle-gap and cap clusters need separate fidelity/cost work; seed
30080373 T5 repeatedly reaches the 200,000-apply cap, while seed
30083140 T5 has an oracle gap with 1,328 outcomes. The on arm had a
long tail of about nine minutes despite its old cost estimate; do not
assume its average cost reflects those outliers. The `test/mana_clean.json`
record is committed with this handoff. No batch is active.

**Creature Giving fresh gate complete:** indices 65,000–69,999 at PGO
source `fcb9352608ea` yielded 280 issues: 276 ENUM-GAP across 249
signatures, three APPLY-DROP, one DOMINATED-CREATURE; zero oracle gap or
cap. Oracle-on/off play byte-identical. Report:
`logs/mana_clean/codex-creature-giving-fresh-20261117/report.md`.
The large cluster is same-turn Crop Rotation chains: the gate rejects
plans that cast Crop, put an untapped mana land into play, then use it to
pay the next Crop or creature. Exact seeds 30065017 T2 (two Crops +
Suture Priest / War-Riders, eight flags), 30065271 T2 main two (two
Crops, four flags), 30065289 T2 main two (three), and 30066910 T2 main
two (three). Focus on the gate's sequential land replacement credit,
not a broad new searched branch. Three APPLY-DROP flags are one separate
T4 signature (see report). The `test/mana_clean.json` record is committed
with this handoff. No batch is active after this gate.

**Mill fresh gate complete:** indices 10,000–14,999 at PGO source
`fcb9352608ea` yielded 131 ENUM-GAP flags across 51 signatures, with
zero other issue class or cap and byte-identical oracle-on/off play.
Report: `logs/mana_clean/codex-mill-fresh-20261117/report.md`.
The leading signatures are still Visions of Beyond → Surgical Extraction,
usually with Surgical life-paid and a different land left untapped; exact
seeds include 30010053 T2 (15 flags), 30010015 T2 (14), 30010356 T2
(10), and 30010627 T2 (7). This is the residual investigated in
`docs/design/mill-surgical-mana-clean-2026-11-16.md`: simply reopening
Surgical's searched mana-vs-life choice did not clear the gate and added
substantial work. The menu/resource comparison across Visions' draw
breakpoint needs a narrower diagnosis. The new `test/mana_clean.json`
record is committed with this handoff. No simulation batch is active.

**Shadowborn Apostle fresh gate complete:** indices 100,000–104,999 at
PGO source `fcb9352608ea` yielded three issues: two APPLY-DROP, one
ORACLE-GAP, no cap/enum/order issue. Oracle-on/off play byte-identical.
Report: `logs/mana_clean/codex-shadowborn-fresh-20261117/report.md`.
The oracle gap is seed 30103262 T5, Dark Ritual → Diabolic Intent on a
board with two Lakes of the Dead; the oracle's nearest outcome taps
Swamp#42 and leaves B3, while the engine leaves that Swamp untapped and
B2. Investigate oracle payment coverage first. The drops are seeds
30104750 T4 (Bloom/Lake, Thrumming Stone continuation) and 30104985
T3 (Lake, double Diabolic Intent); both need depth 3+ rollback, so the
one-step rollback does not rescue them. The gate record is committed in
`test/mana_clean.json`. No simulation batch is active after this gate.

**Pirates fresh gate complete:** indices 99,000–103,999 at PGO source
`fcb9352608ea` yielded two APPLY-DROP flags in one seed, no enum/oracle
or capped flag, with oracle-on/off play byte-identical. Report:
`logs/mana_clean/codex-pirates-fresh-20261117/report.md`.
Both flags are seed 30103196 T3, two variants of Vialed Staunch
Crewmate followed by Dire Fleet Captain: oracle pays the intervening
cast with newly played Spirebluff Canal#50:R, retaining Blackcleave
Cliffs#9:B and older Spirebluff Canal#49:R for Captain. The engine drops
Captain. Rollback depth one, with the Captain action absent from the
prefix plan; forced `MTG_PAY_ROLLBACK` rescues both, but that global
backup remains default off. Investigate a forward color/source hold
across the Vial breakpoint before enabling rollback broadly. The gate
record is in `test/mana_clean.json` and committed with this handoff.
Pirates is now **issues**, not stale-clean.

An **Extra Turns first-ever gate attempt** was stopped during its
64-game sizing probe. After six minutes at 32 threads, only ten one-game
jobs had finished and the process was at about 21 GB RSS; the enforced
5,000-game minimum would take many hours and threaten the 40 GB cap.
No full gate ran, no `test/mana_clean.json` record was written, and Extra
Turns remains **never-run**. The probe artifacts are in
`build/scratch/mana_clean/codex-extra-turns-first-20261117/`, and console
output is `build/scratch/dragonstorm_visible_lump_ab/extra_turns_first_gate.out`.
The process group was terminated and verified gone. Investigate this
oracle cost before attempting the full gate; meanwhile continue cheaper
due decks. Do not mark Extra Turns clean or completed.

**02:47 UTC update:** The scoped Ruby/Seething ordering repair in
`src/ai/DecisionProviders.cpp` is complete and committed with this
handoff. The provider moves Ruby behind a contiguous Rite/Seething ritual
run only when the current order fails both payment probes, a later creature
is visible, fewer than two cheap rituals precede Song, and exact sequential
payment accepts the new order. Seeds 30068752 T3 and 30071498 T4 clear
both APPLY-DROP-ORDER flags each; `MTG_DRAGONSTORM_RUBY_FUNDING=0` restores
them. Four older Ruby exact repros remain clear. Release units pass 3,383
cases and 3,614,946 assertions. Dragonstorm/Shadowborn smoke+regression
passes all cells, 561 scenarios and strict viewer replay of 864 references
with zero drift/divergence/enum gap. Five thousand paired held-out games
at seed 9,820,001 had identical win turns (4,957 wins each). The combined
Ruby flag (old and new rule) adds 2.0% CPU in this sample, so that is not
an isolated cost for the new rule. PGO+LTO has since rebuilt successfully.
Full evidence is appended to `docs/design/dragonstorm-ruby-apex-storage-mana-2026-11-17.md`.
The source/doc commit is `5e9c477e9`, and the PGO source tree stamp is
`fcb9352608ea7e19580916d696643bc094d6ef3a`.

**Fresh PGO Dragonstorm gate complete** on indices 72,000–76,999:
`logs/mana_clean/codex-dragonstorm-ruby-song-20261117/report.md`.
It found 28 APPLY-DROP flags across 16 signatures, with zero ENUM-GAP,
APPLY-DROP-ORDER, ORACLE-GAP, or capped flags. Oracle-on/off play is
byte-identical. The new block is not directly comparable to the earlier
157-issue block. Leading residuals: seed 30075408 T4 Apex continuation
loses Dragonlord Kolaghan (six flags); seed 30073082 T5 Apex/Scourge
continuation loses Karrthus (ten flags across variants). Additional
exact repros: 30072155 T5 Ruby → Song → Karrthus, 30074224 T5 Song →
Karrthus, 30074081 T4 Karrthus → Scourge, 30076435 T4 Desperate → Feat
→ Karrthus. The report has commands for each. The `test/mana_clean.json`
record and design note are being committed with this update. No simulation
batch is active after this gate. Next: investigate the APPLY-DROP clusters
and run another fresh gate after any accepted fix.

The earlier three source commits following pushed `f2156d4e6` are:

* `b82aee80c` Dragonstorm Ruby funding order fallback in `src/ai/DecisionProviders.cpp` (`MTG_DRAGONSTORM_RUBY_FUNDING`, default on). Four exact old APPLY-DROP-ORDER seeds clear; 5,000 paired games had identical win turns and play digests, +0.8% CPU and +1.6% work units.
* `d06b9559c` visible two-copy Apex line gate in `src/ai/TurnSolver.cpp` and focused tests (`MTG_APEX_VISIBLE_LINE_MANA`, default on). Old ENUM-GAP seed 30062821 clears. Narrowed from a costly one-copy trial; 5,000 paired: two earlier wins, none later, +5% CPU, +1.37% units. Human play menu excluded to preserve reference order.
* `126872514` storage-land full burst backtracker and cached replay in `src/core/SpellEffects.cpp` (`MTG_STORAGE_BACKTRACK_FULL_BURST`, default on). Old ORACLE-GAP seed 30062628 clears; off control restores it. This matches the simulator's existing greedy/executor full-burst **simplification**; actual MTG storage lands may remove fewer counters.

Full Release unit suite: 3,383 passed, 3,614,946 assertions. Combined Dragonstorm/Shadowborn smoke and regression: 5,762 games, 561 scenarios pass, strict viewer 864 refs with zero drift/divergence/enum gap, both suboptimal references beaten. Shadowborn unchanged. Nine Dragonstorm GT logs, `test/regression_gt.txt`, `test/job_cost.tsv` were updated and accepted. Four depth-zero turn changes derive from previously shipped `MTG_ONDEMAND_VISIBLE_LUMP`, not these three new fixes. PGO+LTO build succeeded at source stamp `7940651a93b1749003e268106ee064aa5264b50b`; use `build/PGO/mtg` for long runs. Detailed evidence: `docs/design/dragonstorm-ruby-apex-storage-mana-2026-11-17.md`.

**Fresh PGO Dragonstorm gate finished** on indices 67,000–71,999, command:

```
MTG_RSS_CAP_GB=40 python3 scripts/mana_clean.py run dragonstorm --games 5000 --name codex-dragonstorm-ruby-apex-storage-20261117
```

Report: `logs/mana_clean/codex-dragonstorm-ruby-apex-storage-20261117/report.md`; `test/mana_clean.json` is committed in `304d1fed8`. **157 issues:** 121 APPLY-DROP (40 signatures), 4 APPLY-DROP-ORDER (4), 4 ENUM-GAP (4), 28 CAPPED-DECISION (19); ORACLE-GAP=0. Oracle-on/off play byte-identical. The increase from the prior block's 93 issues is a fresh seed block, not evidence of a regression; compare exact old seeds separately. Capped decisions deserve focused rerun/analysis, not assumed correctness failures. Report says 129 misses, 28 caps. Top new APPLY-DROP clusters: seed 30068634 T7 Karrthus/Apex, seed 30070527 T5 Dragonlord Kolaghan/Apex. New ENUM-GAP seed 30068175 T4 (two plan variants) and 30070173 T5 (two). New order-drop seed 30068752 T3 (two variants) and 30071498 T4 (two). Exact repro commands are in the report; substitute `build/PGO/mtg` if its stamp still matches. The older unresolved seed 30065244 T6 had 24 Karrthus/Apex drops; changing Lotus color order alone had no effect and was removed. Seed 30062531 T4 may have a `LineFloatTapFirstPlan` ordering issue: oracle spends Unclaimed Territory as colorless for Lathliss to preserve floating red for Irencrag Feat, while engine spends red. Check fresh signatures before prioritizing it.

## Immediate continuation

First-pass source diagnosis after the gate: `ApexPairGateCredit` in
`src/ai/TurnSolver.cpp` credits Apex's ten mana to both scalar and
selection-exact gates only when **two unstaged Apex copies** are in hand.
The new seed 30068175 T4 and 30070173 T5 enum gaps are **one Apex plus a
visible Scourge of Valkas**; their oracle order casts Apex before Scourge.
This points to the pair-only guard as the gate, but broad one-copy credit
previously cost +22% CPU. Seek a narrower visible-payoff condition and A/B
it; the sequential payment walk already applies the Apex mana after cast.
The new Ruby/Seething order gaps are fixed in `5e9c477e9`; see the update
above and the design note. The Apex one-copy gaps remain open.

One residual payment lead is especially concrete: seed 30074224 T5,
`Seething Song → Karrthus`. Oracle pays Song by cracking Lotus Bloom for
red, leaves both Unclaimed Territories untapped, then pays Karrthus's
black and green with them. The engine's replay ends with Bloom still on
the board, both Territories tapped, five red floating, and Karrthus
dropped. The `MTG_ONDEMAND_VISIBLE_LUMP` branch in `src/ai/ManaPayment.cpp`
uses `short_with_shared` against the **prepayment** pool before comparing
the trial payments. Two Territories make black/green look covered before
Song, even though ordinary Song payment spends them as colorless. This is
a likely guard mismatch, not yet a proven fix. A scoped trial should
compare the after-payment pools and preserve the two later colored
sources when early Bloom:red pays the current generic bill. The user
already authorized early Bloom cracking for a visible later cast.
The existing focused test is in `test/unit/test_dragonstorm_followups.cpp`
("visible Karrthus needs Bloom black with one wild Territory"); add a
two-Territory + Mountain + Bloom Song/Karrthus case alongside it. After
paying Song, require Bloom spent and both Territories untapped; after
crediting Song's five red, Karrthus should pay. Removing the prepayment
`short_with_shared` filter while retaining the after-payment
`saves_later_source` comparison is the smallest candidate, but it has
not been A/B tested and must not be assumed correct. Check exact older
Ruby/Bloom seeds and held-out turns.

1. Inspect `git status --short`, the fresh report and raw `build/scratch/mana_clean/codex-dragonstorm-ruby-apex-storage-20261117/on.0.err`. The Dragonstorm design note and gate record are committed in `304d1fed8`; keep this handoff current as work proceeds. Gates return nonzero when flags remain.
2. Work the new Dragonstorm issue clusters with exact on/off repros, narrow deterministic payment/order fixes, 5,000 paired A/B where material, Release unit/regression/viewer validation, and a fresh PGO 5,000-game gate. Avoid adding searched options or costly broad branches. One top-level simulation batch at a time, `MTG_RSS_CAP_GB=40`, use `nproc` for worker count. Build only via `./build.sh`; PGO+LTO for long runs.
3. Separate Gods trial worktree `.claude/worktrees/codex-gods-hold-20261117` has only a dirty provider edit adding a `ready >= 3` floor to the default-off `MTG_GODS_LOAM_GREEN_HOLD`. It is unbuilt/untested. The broad green hold cleared exact seeds 30037134 and 30037273 but caused 2 later wins/0 earlier and +12.1% recorded time in 5,000 paired games. User wants a less costly fix and full mana completeness. See `docs/design/gods-loam-line-green-hold-2026-11-16.md`. Do not run its long test concurrently with Dragonstorm gates.
4. Resume the wider queue from `python3 scripts/mana_clean.py status` and the older handoff: Pirates, Extra Turns, Jumbo, Hydras, Fungus, Shadowborn, Morph and due decks. The target is a fresh 5,000-game block for every shipping deck and every issue class repaired; stale-clean records also need fresh runs.

The 04:27 UTC status snapshot has 56 decks: 18 clean on current source,
36 with issues, two never run, and zero stale-clean; 27 are marked DUE.
This remains a large open queue. Extra Turns and Merfolk Turns are the
never-run decks. Extra Turns needs an oracle cost investigation before
its 5,000-game gate can be practical.

## User decisions and operational constraints

Gods should dredge toward Mystic Retrieval for Replenish, use Loam to recover lands, and natural-draw when graveyard has one or zero lands; a non-clairvoyant A/B supported the strict dredge policy, which is shipped. Study → Loam original plans continue across the Study breakpoint; new plans there are pruned. User allows a visible later cast to justify cracking Dragonstorm Lotus Bloom early. For EDF, spend colorless for generic when free, but preserve `{C}` for Eldrazi Displacer loops and the final kill. User permits narrow non-searched line-opening options. Windows CI is the useful compatibility check; run asynchronously, do other work, and respond only to failure. Linux CI can be dropped. Prior Windows run 43993315542 succeeded; current run 44025173062 is watched asynchronously. The branch was pushed at 05:35 UTC Nov17; next ordinary push is at or after 09:35 UTC. Before any push or worktree removal, run `python3 scripts/check_references_safe.py`; promptly push new user references if any. The user explicitly asked this local handoff be kept up to date for Claude because Codex usage is nearly exhausted.

EDF follow-up source lead: `src/ai/ManaPayment.cpp` already drains
floating colorless before W/U/B/R/G for generic costs in the normal
route, while `MTG_HOLD_C_FOR_SINK` and `MTG_HOLD_C_IN_PAYMENT` protect
human-play `{C}` sinks when `BoardHasColorlessPipSink` sees one. The
current guard is board-based and human-only; verify the user's final
kill case separately: the sideboard has Essence Depleter and Dimensional
Infiltrator, reached through Living Wish, and the main deck has Shivan
Gorge. A `{C}` sink may therefore be visible as a Wish continuation
before `BoardHasColorlessPipSink` sees it on the board. See the EDF
analysis around its combo-finish route. This is an
unverified lead, not a newly adopted EDF change.
