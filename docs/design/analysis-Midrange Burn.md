# Midrange Burn analysis

Deck: `decks/Midrange Burn/Midrange Burn.cod` (60 cards, mono-red with Fiery Islet).
Started 2026-10-29. Implementation and the original-branch full verifier pass
are complete. The original-branch b20 cost gate passed at 2.95x Hinata. The required
horizon leaf check completed 18 blocks and both its aggregate and unlimited-
budget preservation gates pass; keep default ON. The authored cleanup-discard
policy was approved and adopted on 2026-10-30 behind a default-on, reversible
Midrange-only flag. Its paired seeded tiers are non-inferior overall; the one
persistent slower game is documented below.
Exhaustive mulligan-profile and value-leaf generation was paused on 2026-10-30
while another container used the box. A new shared
passive-opponent land pattern passed a full smoke A/B. A targeted fold of burn
spells against inert passive creatures restored the 20 ms two-headed play
setting under the integrated-branch cost gate: 1.21x Hinata (limit 3x),
with a 6.0133 two-headed regression average. The current branch has accepted
Midrange Burn smoke and regression baselines (11 keys); overnight ground truth
has not yet been recorded here.
The final discard-rule full unit suite passed 2,230 cases and 3,556,777
assertions; all 349 scenarios passed. The final integrated verifier passed every
blocking gate, including 15/15 card coverage, field and cost audits, viewer
wiring, mismatch checks, play invariants, and suite membership.
Chronological checkpoints below include superseded intermediate states; this
opening status is current.

On 2026-10-31 the first ten hand-played references were preserved on the shared
branch. The shipped search, with each recorded mulligan and bottom forced, tied
all nine human wins and won the one human loss on turn 7: **zero shortfalls**.
All ten replay exactly after correcting the reference checker's one-based
graveyard-candidate index mapping; the full strict sweep passed 699 references
with zero play drifts or enumeration gaps. The viewer now queues two Rift Bolt
suspends in one plan, while a directed search test confirmed that the autonomous
enumerator already retains and executes that line. Equivalence discovery found
**15 card names and 14 buckets**, merging only Chain Lightning with Lightning
Bolt. The measured mulligan-setting screen selected depth 1 and a 1 ms budget
(0.15x the built-in play cost while clearing its fidelity floor) and recorded
the 14-bucket contract. The fast exhaustive profile completed with 56,656
entries at effective R=30 and passed its acceptance gate: keep -0.3565 turns,
blind bottoming -0.0488 turns versus the prior policies. Smoke and regression
ground-truth changes were attributed to mulligan count, bottom choice, or
bottomed-card order with zero unexplained play changes; both tiers were
accepted. With the profile active, the ten references still show zero search
shortfalls. The resized overnight set (depth 3: 4x1,000; depth 5: 4x500;
2HG depth 5: 4x170) was measured first without the table, then with it. Over
14,006 single-player games the loss-penalized average moved from 5.2012 to
4.8440 turns (-0.3572); over 680 2HG games it moved from 5.9794 to 5.8236
(-0.1558). The overnight attribution reproduced both baselines. Four
same-hand play digests differed under table bottoming, but all four exactly
matched the static baseline with lookahead bottoming restored, confirming
bottoming-method search residue rather than an in-game table effect. Only
Midrange Burn's overnight ground truth was accepted; the full-suite run also
found unrelated stale keys that need their own large-change audit. The value
leaf is next.

## Search cost and cost-choice audit (2026-10-30)

At depth 5 and 20 ms, a ten-game two-headed sample (seed 2002, one worker)
used 271,903 search units for Midrange Burn, 171,740 for classic Burn, and
508,276 for Hinata. The respective CPU task-clock totals were 5.90, 4.88,
and 23.13 seconds. Midrange Burn's measured cost per search unit was lower
than either comparison deck's; its total work came from more units than Burn
and from the still-missing mulligan artifact. This is a small diagnostic sample,
not a general speed ranking.

The Midrange branch census (three two-headed games, seed 2002) attributed
344,491 odometer positions to Searing Blaze (43%), 202,891 to Fireblast
(25%), 150,549 to Chandra (19%), and 3,610 to Grim Lavamancer (<1%). A
heavy game at seed 2024, game index 22, spent 612,015 of 632,395 units
(96.8%) on 327 mulligan-bottoming trial decisions. The separate mulligan
profile run should remove that multiplier; no profile or value leaf was built
in this container. Sampling the unmodified game found no single dominant
per-unit routine: plan/search was 24.4% of self samples, allocation/copy
9.5%, and mana 5.8%.

Magma Jet is not a spell-cast scry search axis. Its two-card scry resolves
through the provider's one-choice heuristic, so keep/bottom and top order
do not multiply its cast plans. The generic rule keeps spells, keeps lands
only below two lands in play, and preserves look order among kept cards.
Changing those rules would be a card-quality decision rather than a cost
prune; the human viewer still offers all legal ordered dispositions.

The new Midrange-only, default-on cost-choice hooks choose a tapped Mountain
pair for Fireblast when every eligible land is a basic Mountain, choose one
stable Grim Lavamancer graveyard pair while leaving damage targets searched,
and omit Fireblast's six-mana mode only when even an optimistic same-turn
pool cannot pay it. The printed-cost mode remains available with enough mana;
human and unpruned play retain every legal choice. Each hook has an independent
`MTG_MIDRANGE_*` off switch. In the same ten-game sample, these hooks reduced
units from 271,903 to 270,837 (0.39%) with the same 5.1 average win turn.
The measured CPU task clock fell from 4.14 to 3.94 seconds in paired runs,
but that small timing difference is sensitive to box load.
On a separate 100-game two-headed held-out range (seed 32000), the narrowed
and full-choice arms both averaged 6.02 turns and matched all 92 reported
winning game turns; the other eight games were unwon in both arms. Search
units fell from 18,801,128 to 17,396,963 (7.5%). This result is for the
three hooks together. The paid-cost bound was then tightened to count
floating mana once; that correction can only omit an unaffordable paid cast.
The corrected build passed all 2,234 unit cases (3,556,833 assertions), all
349 scenarios, the scoped smoke and regression batches, and strict replay
of 689 saved viewer references with zero play drift or enumeration gaps.

## Initial coverage

Initial card-database inspection found these nine missing entries, all now
implemented and covered:

- Chain Lightning
- Rift Bolt
- Skewer the Critics
- Price of Progress
- Fireblast
- Chandra's Incinerator
- Chandra, Torch of Defiance
- Magma Jet
- Grim Lavamancer

Existing entries: Lightning Bolt, Searing Blaze, Monastery Swiftspear,
Mountain, Fiery Islet, Light Up the Stage. Their clause/field/viewer audit is
complete; final coverage is 15/15 full.

## Provider routing

`MidrangeBurnProvider : DeckProvider` gives this list its own identity with the common baseline. Initial routing inspection found it accidentally inherited Burn through Searing Blaze's landfall signature. The audit found that Burn's three-land banking and main-2 cast restrictions are invalid for Chandra and Incinerator, so those deck-specific policies are not inherited. No original-Burn judgement policy has been adopted for Midrange Burn. Existing Burn's routing is unchanged.

Signature: Searing Blaze's landfall marker AND any of Chandra's Incinerator, Chandra, Torch of Defiance, or Grim Lavamancer (names checked before definition lookup). This spans three signature cards, excluding generic staples.

Certificate: **NotAssessed**. A safe winless-turn proof must account for direct damage, suspend, planeswalker abilities, graveyard-fuelled activations, and combat. Hook/ordering measurement and any future Midrange policy remain to be validated below.

Provider routing, final build, directed tests, and all three final regression
tiers passed their required gates. The final full unit suite passed 2,136
cases / 3,553,305 assertions; 348/348 scenarios passed.

## Pending convergence work

1. Keep the four Claude-faster seeds as documented search-quality candidates;
   they are not unresolved legality or viewer flags.

## Passive-opponent land update, 2026-10-30

All passive opponents now start with three inert lands. The ten-game index
cycle has nonbasic counts `{0,2,1,3,2,0,3,1,2,3}`: two games each at zero and
one, three each at two and three. Among 40 unique regression-suite decklists,
the modal nonbasic count in three lands was 0:10, 1:6, 2:12, 3:12, so the two
extra slots went to buckets two and three. Both play/draw parity halves
include all four buckets. Direct unit coverage checks the pattern, basic
classification, and Price of Progress damage. The full unit suite passed
2,137 cases / 3,553,387 assertions and all 348 scenarios passed.

An old/new Release smoke A/B used the same 157 jobs and 51,675 game seeds.
Only seven rows changed. Midrange Burn's mean turn improved at d0
5.869→5.713, d3 4.92→4.80, d5 4.78→4.62; its 2HG d5 row improved
6.48→6.16. The Snow d0/d3/d5 rows changed play digests without changing any
game's win turn; Rimefeather Owl can target the new opponent lands, changing
the amount of final overkill. The other 150 rows kept identical averages and
digests. Aggregate deterministic work units fell 0.77%. Batch wall time
was 443→425 seconds (single-run observation), while peak RSS rose from
4.75 to 5.11 GB (about 7.5%). One Midrange 2HG game became a turn slower
(T6→T7) and remained so at 4x and 16x search budget; six in that row became
faster. It reflects a different earlier play/draw line, and is recorded as a
tradeoff rather than hidden by the better average. Logs and comparison script:
`/tmp/passive-land-full-old`, `test/logs/smoke`, and
`/tmp/compare_passive_land_full.py` in this container.

The original-branch scoped regression ran 13 Midrange Burn/Snow rows and 2,787
games. Midrange Burn improved d0 5.9260→5.7670, d3 4.7733→4.4800,
d5/s2002 4.8200→4.5733, d5/s3003 4.5800→4.4533, and 2HG d5
6.1867→6.0400. The two searched slower games in the 2HG row were both
classified as budget churn: gi13 changed T6→T7 at b20 but won T5 at 4× and
16×; gi67 changed T7→T8 at b20 and returned to T7 at both higher budgets.
The other searched Midrange regression changes were faster. Snow had no prior Linux
ground-truth rows and will receive them in the integrated-branch run.

After rebasing the deck work onto the shared branch, the Release build passed
2,225/2,225 unit cases (3,556,643 assertions), 349/349 scenarios, and the
provider routing audit. Four existing Burn saved references initially showed
`shuffle-dead` in the strict replay, but the same four replayed correctly on
the pre-deck binary, even before the passive-land change. The cause was the
new two-stage Searing Blaze target protocol: a saved one-stage creature choice
was being compared with its newly inserted player-choice stage. The reference
checker now derives that player from the saved target creature and then
content-matches the creature choice. All 17 Burn references now replay: 16
reproduce their recorded win turn; the marked suboptimal one is beaten.

The lightweight analyzer profile was refreshed at seed 871000 after the
opponent land update (1,000 d5 scoring games). Price of Progress's score
moved from -0.6126 to -0.0441. The analyzer updated the card scores only;
the generic mulligan settings and absence of a hand-score gate are unchanged.
This is the deck's baseline `.profile.json`, not an exhaustive mulligan table
or a value leaf; generation of those larger artifacts remains paused by the
user's 2026-10-30 instruction.

The integrated-branch b20 cost gate initially failed at 3.91x Hinata: the
2HG regression row took 15,876.773 ms/game against Hinata's 4,055.947.
Reducing only the 2HG budget to b10 or b15 lost two smoke wins and warned
that those rows no longer matched the shipped b20 play setting, so the budget
was restored. A CPU sample of the two worst 2HG seeds (gi12 and gi22, both
six-creature passive-opponent buckets) found repeated `sscanf` burn-pin parsing
and a large burn-target enumeration. `DecodeFixedBurnPin` now uses integer
parsing without `sscanf`; Midrange Burn's autonomous target menu folds inert
passive-creature targets. Single-target burn keeps the face target; Searing
Blaze keeps the toughest passive creature to preserve a later target. When an
enemy target exists, Blaze also drops own-creature targets that can kill an
attacker. Human and unpruned menus retain all legal targets. The two outlier
regression games kept their T8/T7 wins and identical play digests while their
single-game runtime fell from hundreds of seconds to about 42/35 seconds.

The final same-run cost gate passed at **1.21x Hinata**: Midrange Burn's worst
searched row was 2HG d5/b20 at 4,674.227 ms/game; Hinata's was 3,877.193.
The final integrated smoke is accepted: Midrange d0 improved 5.713→5.670;
d3 and d5 kept their 4.800/4.620 averages; 2HG kept 6.160. Its two searched
turn changes offset (gi12 T7→T8, gi17 T7→T6), and gi12 wins T6 at b80.
The final integrated regression is accepted: Midrange d0 5.926→5.725,
d3 4.7733→4.4800, d5/s2002 4.8200→4.5800, d5/s3003
4.5800→4.4533, and 2HG d5 6.1867→6.0133. Its only two searched
slowdowns were budget churn: 2HG gi13 T6→T7 wins T5 at b80, and
single-player gi45 T5→T6 returns T5 at b80. The strict replay on the
first passive-target fold passed all 689 saved references with zero protocol
or board drift; the final edit narrows only autonomous Blaze self-targets
while leaving human and unpruned menus complete, and is covered by a directed
unit test. The final full unit suite passed 2,226/2,226 cases and
3,556,657/3,556,657 assertions. The final overnight tier accepted all 26
Midrange Burn/Snow rows: every Midrange aggregate was better or equal than
its pre-land baseline, with 2HG d5 rows improving by 0.17–0.24 turns and
single-player searched rows by 0.18–0.23 turns. All 13 searched slower
individual games recovered their old win turn at 4x budget. The three Linux
tiers have 46 consistent baseline/log pairs (7 smoke, 13 regression, 26
overnight); Snow's previously missing Linux rows are now recorded.

## Cleanup-discard adoption, 2026-10-30

The user approved `docs/design/midrange-burn-discard-policy.md` and noted that
real cleanup discards should be rare. `MidrangeBurnProvider` now returns one
discard candidate from a full keep-set order; `MTG_MIDRANGE_BUCKET_DISCARD=0`
restores the generic ranking. The first two lands and the one-drop creature
then spell tier take priority. Visible damage is kept until it can bring the
opponent to three or less over two turns; a needed third land or two-Mountain
Fireblast fuel joins that package. Third and fourth lands otherwise occupy
optional slots, and Chandra never makes four lands mandatory. Dead Price of
Progress and Searing Blaze shed first. The full-order hook re-ranks multi-card
cleanup after each shed because losing a land or damage spell can change the
next quota. Human card choices and other deck providers are untouched.

The Release build, targeted flag-on/flag-off tests, full unit suite (2,230
cases, 3,556,777 assertions), and 349 scenarios passed. The corrected final
smoke had one same-score play change and no slower games. The regression tier
improved the 2HG average 6.0133→5.9867; gi13 went T7→T5 because the generic
arm shed Fireblast at T2 while the new arm retained it. One single-player
game went T6→T7 at 20ms but recovered to T6 at 4× and 16× budget. Other
searched single-player averages were unchanged. Overnight covered 18 Midrange rows / 11,206 games:
10 searched games faster, 2 slower, and about 12 fewer total game-turns.
The d3 s4004 gi40 T5→T6 loss recovered at 4× budget. The d3 s4004 gi310
T8→unwon loss persisted at 16×: the new rollout induced an early T2
Fireblast, leaving only three lands for a Chandra drawn on T7. This is a real
search-quality tradeoff, carried in the Linux GT provenance, while the pooled
outcome is non-inferior. All 46 Linux baseline/log pairs remain consistent.
The prior full strict replay covered 689 saved references; there are no
Midrange Burn saved references, and this adoption changes only its provider.
The regression tier was therefore completed with the unrelated full-corpus
replay skipped after a targeted inventory confirmed zero matching references.
The final-policy standalone discard-analysis repeat at seed 930000 found zero
real cleanup discards in 400 d3 games, 1,359 rollout sheds (all below four
battlefield lands), and therefore no searchable real-site discard labels.
The post-policy `verify_deck.py --write-ledger` pass completed every blocking
gate: 15/15 card coverage, card costs and fields, all three suite tiers,
viewer decisions and wiring, 120 mismatch games per arm, eight play-invariant
games (215 decisions), and suite membership. Its current ledger appears below.

## Horizon leaf tie-break checkpoint

The first eight-block PLAY-settings A/B completed 16,000 games (8,000 paired)
in `logs/leaf_tiebreak/midrange-burn-final-8blocks.out`; the command output is
`build/midrange-burn-analysis/leaf-final-8blocks.log`. Default ON changed only
seven games: six improved, one worsened, net **-6 win turns** relative to OFF.
Each 4,000-pair half saved three turns. The script correctly reported **NO SIGN
AT THIS SAMPLE** because seven changed games are below its 20-game direction
threshold, and suggested roughly 16 blocks. Extend with eight fresh blocks
starting at seed 1,800,000 using the same shipped play settings and 24 threads;
then combine the 16 independent blocks without replaying the original seeds.
The eight-block extension completed with
`python3 scripts/leaf_tiebreak_check.py --tag midrange-burn-leaf-extension8 --seed-base 1800000 --blocks 8 --threads 24 'decks/Midrange Burn/Midrange Burn.cod'`;
its records are `logs/leaf_tiebreak/midrange-burn-leaf-extension8.out`, with the
script report in `build/midrange-burn-analysis/leaf-extension8.log`.
The second eight-block run completed: 8,000 pairs, six better and six worse,
net **-2 turns** with default ON. The combined 16-block report in
`build/midrange-burn-analysis/leaf-combined16.log` shows 16,000 pairs, 12
better, seven worse, net **-8 turns**, with both 8,000-pair halves negative
(-6 and -2). Because 19 changed games were one shy of the 20-game threshold,
we extended two more independent blocks at seed 2,600,000 instead of rerunning
either existing sample. The final extension command is
`python3 scripts/leaf_tiebreak_check.py --tag midrange-burn-leaf-final2 --seed-base 2600000 --blocks 2 --threads 24 'decks/Midrange Burn/Midrange Burn.cod'`;
its records are `logs/leaf_tiebreak/midrange-burn-leaf-final2.out`, report
`build/midrange-burn-analysis/leaf-final2.log`.
The final two blocks completed: 2,000 pairs, two better, three worse, net
**+3 turns** with default ON. All 18 blocks combined in
`logs/leaf_tiebreak/midrange-burn-combined18.out` and reported in
`build/midrange-burn-analysis/leaf-combined18.log`: **18,000 pairs**, 14
better, 10 worse, **-5 turns** overall, 24 changed games (above the tool's
20-game threshold). Half A saved seven turns; half B lost two. The tool's
verdict is **KEEP THE DEFAULT (ON)**. Three ON regressions were wins in OFF
and unwon by T8 in ON: 1900000/gi222 (T8 to unwon), 2600000/gi161 (T8 to
unwon), 2600000/gi884 (T6 to unwon). All three recovered at d5 with
unlimited budget (`--budget-ms 0`) and the same pre-regression winning depth:
T7, T8 and T6 respectively. This passes the separate preservation gate.
Each replay used single-game seed `base+gi` and `--game-index gi%10`. Logs are
`build/midrange-burn-analysis/leaf-recovery-<base>-<gi>.log`.

The final refreshed verifier output is
`build/midrange-burn-analysis/verify-complete.log`: every blocking gate passes,
including coverage 15/15, live costs, viewer surface and wiring, the runtime
mismatch and play-invariant harnesses, Claude sweep with zero unresolved flags,
and all three suite tiers. The clause ledger gate is the documented SKIP
covered by coverage, bracket notes and oracle comparison; overall GATE PASS.

## Suite admission and sizing

Rows for Midrange and Midrange 2HG are in all three tiers; the Linux GT has
29 accepted keys and no stale/missing per-game logs. The final shipped b20
cost gate measured maximum searched 19,317.627 ms/game (2HG) versus Hinata
6,539.813 ms/game in the same isolated run, ratio **2.95x**, below the fixed
3x cap. A b10 2HG diagnostic passed but was rejected because its row did not
match the shipped b20 setting; suite rows remain b20.

For overnight pre-keep-table admission, the documented ENTRY shape is four seed blocks each of d0/2000 games, d3/400 games/b10, d5/300 games/b20, subject to measured mode budget. Do not shrink another deck's rows to make room. Accept each newly added tier with the same `--deck=midrangeburn,midrangeburn2hg` filter as its run, inspect the GT diff, and include rows with their accepted GT. Value-leaf and mulligan generation remain blocked until admission. Full overnight resizing happens immediately before keep-table adoption using measured cost and a profile-absent rebaseline.

## Verification commands

```sh
python3 scripts/analyze_deck.py 'decks/Midrange Burn/Midrange Burn.cod' --coverage-only
python3 scripts/provider_audit.py 'decks/Midrange Burn' --check
python3 scripts/suite_gate.py --cost 'decks/Midrange Burn'
python3 scripts/verify_deck.py 'decks/Midrange Burn/Midrange Burn.cod' --write-ledger
```

The verifier includes field/cost, coverage, viewer/wiring, runtime mismatch
and invariant gates. The real Claude-play sweep below used the Claude oracle.

## Claude-play sweep

- commit: `4a04d8c14` (working tree sweep; later menu fixes passed directed tests and a 40-game viewer replay)
- seeds: 871001 games: 15
- flags: 0 unresolved

The rebuilt rescue binary completed 15/15 Claude-driven games, output in
`logs/midrange-burn/claude-sweep-rescue/`. Eleven matched the shipped AI win
turn; Claude was one turn earlier on seeds 871001, 871003, 871005 and 871012.
The sweep found two viewer/menu defects (unaffordable Chandra paid-cast modes,
and Searing Blaze pinned to a creature still in hand before casting it). Both
were fixed with red-before/green-after directed tests; the final 40-game viewer
gate passed every expected choice type. Remaining differences are search
quality; 871005's faster Fiery Islet dig is clairvoyant relative to visible
information. Seed 871001 stayed T6 at much larger depth/budget, supporting a
deck-specific ordering/value candidate rather than a simple budget increase.

## Approved deferrals

None.

## Implementation checkpoint (in progress)

Primary tree now includes Chain Lightning, Rift Bolt, Skewer the Critics, Price of Progress,
Fireblast, Magma Jet, and Grim Lavamancer definitions plus implementation code. Chandra and
Incinerator remain in isolated implementation worktrees and are not yet integrated. No claim
of completed analysis, coverage convergence, or simulation results is made at this checkpoint.

Additional fixes in primary: Fiery Islet distinguishes life-payment costs from damage;
Swiftspear's keyword implementation is recognized by coverage without fabricating a deferral;
Searing Blaze's paired player/planeswalker and creature targets, loyalty damage, and independent
surviving targets are modeled. Legacy autonomous decks keep their provider-owned historical target
policy; Midrange Burn, human play, and unpruned runs have the full fixed-burn target axis.

Four interaction fixtures under `test/scenarios/midrange_burn_*.json` cover tapped-Mountain
Fireblast, refusing non-Mountain sacrifice, suspend arrival, and Incinerator after two Bolts.
Focused C++ tests are authored but runtime validation is pending.

Integration artifacts and initial build/baseline logs: `build/midrange-burn-analysis/`.
A pre-change smoke run is in flight against a copied pre-change binary; this distinguishes existing
baseline differences from this implementation. The first incremental compilation is also in flight.
Independent Oracle research: `midrange-burn-spells-research.md`,
`midrange-burn-existing-card-audit.md`, and the permanent implementation's research document.

The mandatory Claude sweep will use the actual authenticated Claude CLI. Prepared launcher:
`build/midrange-burn-analysis/run_claude_sweep.py` (15 disjoint seeds 871001–871015, three concurrent
Claude Opus processes; read-only prompts, raw outputs under `logs/midrange-burn/claude-sweep/`).
Do not run until the complete binary and profile exist. No sweep games have run yet.

### Pre-change baseline result

Copied pre-change Release binary: `build/midrange-burn-analysis/mtg-before`.
Baseline smoke completed: **338/338 scenarios pass; 153/153 configurations pass; zero searched or d0
changed games**. Batch makespan 838 s (13m58s), after cold keep-table cache construction.
Log: `build/midrange-burn-analysis/baseline-smoke.log`. This is a baseline only, not validation
of any new Midrange Burn implementation.

### Source-data audit checkpoint

Full cost audit completed: **501 costed cards checked against live Scryfall, all 501 compared,
all costs match**. Log: `build/midrange-burn-analysis/card-cost-audit.log`.
All 15 Midrange unique cards now pass coverage (no missing or partial entries), and the static viewer
manifest/oracle cross-check is clean. Runtime verification and analysis/profile remain pending.

### Integrated build checkpoint

All nine missing cards, Chandra's four loyalty modes and emblem, and Incinerator's
noncombat resource/mandatory triggers are integrated in primary. Five additional permanent
fixtures cover Chandra mana/exile casts and Lavamancer's graveyard fuel. Focused tests cover
actual prevented damage, last-life costs stopping resolution, nested trigger priority,
Fireblast cost modes under Chandra, and separate emblem trigger resolutions.
Incinerator's baseline provider returns every legal enemy creature/planeswalker target;
no unmeasured largest-target narrowing is retained. The emblem's autonomous face-target
policy remains an explicit provider decision awaiting verification.
A clean optimized rebuild of mtg, mtg-analyze, and mtg-test is running. No new-card runtime
or deck performance result is claimed yet.

### Runtime scenario checkpoint

All **348/348** hand-built scenarios pass on the new Release binary, including all ten
new Midrange fixtures. Log: `build/midrange-burn-analysis/scenarios-all.log`.
Database startup caught unsupported `Spectacle`/`Scry` tags on Skewer/Magma Jet;
these structural abilities are represented by their cost/effect parameters, as existing cards
already do. Removed the unsupported tags; no ability was removed. The baseline analyzer is
running 1,000 scoring games at d5/b20 with seed 871000. Full unit build still in progress.
Price of Progress assumes the inherited passive opponent with no lands (zero opposing damage);
Fiery Islets still incur our own Price damage. No opponent-land model was enabled.

### Compaction checkpoint, 2026-10-29 Vancouver

Verified: full source integration and 15/15 coverage; live cost audit 501/501; all 348
scenarios pass on first integrated Release binary; baseline profile generated with analyzer
seed 871000 (1,000 d5/b20 scoring games); baseline smoke before code 338/338 scenarios,
153/153 configurations, zero changed games. Probe against Hinata completed in one pooled
batch (see `build/midrange-burn-analysis/probe.log`), provisional d5 b20 Midrange
5,496–5,960 core-ms/game vs Hinata d5 b30 3,730 core-ms/game; a stricter same-run
suite-gate cost check is still needed after final binary. The analyzer's discard evidence saw
only ONE cleanup decision in 400 d3 games, with zero regret. Its initial cost diagnostic
incorrectly skipped Incinerator; `scripts/analyze_deck.py` now recognizes its cost param,
so rerun diagnostic on the final binary. No all-tier GT or Claude sweep yet.

Current running jobs: `./build.sh release -- --target mtg mtg-analyze mtg-test`
(session 32391, `build/midrange-burn-analysis/build-timing-fix.log`) after source fix;
`audit_card_fields.py --update` session 25979, log
`build/midrange-burn-analysis/card-fields-update.log`, encountering HTTP 429s late.
Latest source fix: after EACH Chandra emblem damage, explicitly flush queued
Incinerator triggers through an outer loyalty-effect scope, and stop if own life lost.
The focused two-emblem test now carries an outer scope. Earlier test-only compile errors
were corrected (`Library` lacks initializer/clear; EffectiveSpellCost needs copies,
AvailableManaPool needs skip pointer). Final build/tests after these edits remain pending.

Next: finish build, run focused unit tests and all scenarios again; resolve any failures.
Refresh only missing source-field snapshot cards if the full fetch remains throttled,
then offline field audit. Rerun full analyzer/cost diagnostic if source fix changed play.
Use full profile for provider audit, viewer runtime, verifier, Claude 15-game sweep, d0/d3/d5
sanity, oracle A/B, leaf tie-break. Apply the prepared all-tier suite rows patch
`build/midrange-suite-rows.patch` ONLY after final same-run cost gate and screened b0 blocks;
run/accept each of three tiers and inspect GT. Finish Stage 6a disclosure and safe-reference
check. No exhaustive mulligan/value generation.

### Correctness loop: cast order

Focused test found a real dropped line: with three Mountains and hand Bolt, Bolt,
Incinerator, the same plan dealt six and reduced Incinerator to {R}, but the generic
creature-first execution order attempted the six-mana creature first, dropped it, then
cast both Bolts. The post-state had one red source untapped and Incinerator stranded.
MidrangeBurnProvider now ranks Incinerator after damage sources under default-on
`MTG_MIDRANGE_INC_ORDER` (`=0` restores common rank). This is a provider-owned ordering
heuristic, requiring full-order A/B and final regression/GT checks. Clean build after
this change: `build/midrange-burn-analysis/build-inc-order.log` running.

The viewer sweep initially crashed parsing an unescaped newline in a Lavamancer fuel
pair. `src/main.cpp` JsonStr now escapes control characters. A 40-game rerun reported
all expected choice types (`free_cast`, `gy_return_one`, `sacrifice`, `scry`, `target`)
surfaced and the static oracle-text cross-check clean. Log: `viewer-runtime-final.log`.
Field snapshot update inserted nine new cards; the full live fetch hit Scryfall 429 on
some older cards, but the offline 646-card hard-field audit passes (snapshot retained
older entries for failed refreshes). Log: `card-fields-offline.log`.
A 200+200 game same-seed cost-reframe diagnostic sees Incinerator and reports
COST_NEUTRAL (4.765 vs 4.765 at d5/b20), log `cost-diagnostic-final.json`.

Suite rows were provisionally added across ALL three tiers with 2HG cases and screened
unlimited backstops at regression seeds 30021..30023 and overnight 30041..30043;
both fit the two-million-unit cap and each game's wall time was below 11 s in
`backstop-screen.log`. The formal suite cost check was started on the pre-order-fix
binary; its result is diagnostic only and must be repeated after the final build.
No tier ground truth accepted yet. Claude sweep began on the pre-order-fix binary,
so seed-level play/version consistency must be checked before accepting its record.

### Compaction handoff: Claude verifier findings

The first three Claude-play games finished on the pre-cast-order binary. Seed 871001
confirmed a combined Bolt + Incinerator plan prepaid Incinerator's printed cost
before Bolt damage lowered it, tapping extra Fiery Islets and losing two life.
`BatchPrepayMainCasts` at `src/ai/TurnSolver.cpp` ~52974 explicitly declines X,
Soulfire, Hinata dynamic bills but not `cost_less_per_opponent_noncombat_damage`.
Extend that decline and verify a focused same-plan payment fixture. Check the
unexpected WHITE floating mana in the same game separately.

Seed 871003 confirmed two spectacle ordering defects: a multi-cast Bolt + Light Up
line dropped the Light Up despite sufficient mana after Bolt; a Lavamancer + Light Up
line hard-cast Light Up first, then could not afford the Lavamancer activation. The
canonical cast order and trailing-activation pass both reorder the plan. Fix in
rollout and executor lockstep; run the exact Claude repro. Cosmetic viewer issues:
Lavamancer's `GraveyardExileAbility` mode 3 gets Deathrite's mode-2 label in
`src/main.cpp` ~757; `gy_return_one` exile-cost note says “return to battlefield”
at ~4368; Chandra paid-exile prompt and fallback damage event need checking.

Seed 871002 reported redundant Chandra exile-cast variants that cannot pay after
Chandra's cost. These are display/search quality only; don't generically prune the
candidate before payment because a combined line might still fund it. All completed
Claude sweep outputs are in `logs/midrange-burn/claude-sweep/seed-*.stdout.json`.

The 199-step `build-inc-order.log` build was running when this handoff was written.
Source files must not be edited until it completes to preserve binary/source
consistency. The earlier suite-cost gate is diagnostic because it uses an older
binary; rerun on the final binary. After correctness fixes, rerun 348 scenarios,
focused unit tests, 40-game viewer audit, 15-game Claude sweep, analyzer/profile,
same-run Hinata cost gate and all three regression tiers with ground truth.

### Correctness patch awaiting a rebuild

The intermediate `build-inc-order.log` build was intentionally interrupted before
linking because the next confirmed fixes change a shared header and would force a
second full rebuild. Source now includes: provider-owned spectacle cast ordering
including opaque draw plans; batch prepay decline for Incinerator/spectacle dynamic
costs; a shared helper to hoist an already-fueled face Lavamancer activation before
its sole same-plan spectacle cast in both rollout and executor; mode-3 viewer labels
and exile-cost prompt text; Chandra paid-cast prompt and fallback damage event.
Focused tests were added for all three play defects. None of these new changes has
been built or validated yet; the current Release binary predates them. Next: run
`MTG_BUILD_JOBS=12 ./build.sh release -- --target mtg mtg-analyze mtg-test`, fix any
compile/test failure, replay Claude exact seeds, then perform full validation.

Claude seeds 871005-871007 finished while that build was starting. Seed 871005
confirmed Searing Blaze-only menu plans that name a Swiftspear still in hand as
their required creature target; the cast silently no-ops. `FixedBurnCandidates`
intentionally includes pending hand targets, but no subset-validity filter required
the target's cast. A `SubsetHasMissingFixedBurnTarget` filter now rejects these
in both plan enumerators while keeping the valid creature-then-Blaze line. Seed
871006 found Searing Blaze's target prompt displaying base 1 after landfall;
`PrepareFixedBurnPin` now passes the current 3 to the chooser. Seed 871007
repeated the Light Up/Bolt ordering defect. These additions also await a rebuild.
The new build was interrupted at 4/204 to include the Searing Blaze fixes.
Seeds 871008-871009 repeated already-fixed labels/prompts, and found missing
Lavamancer damage events plus a CAST_SPELL log that marked a Grim Lavamancer
creature spell as targeting the opponent (its any-target parameter belongs to its
activated ability). A narrow event and log fix is in source. Midrange plan labels
now sort normal hand casts by the same rank used at execution; Chandra +1 labels
distinguish the decline-damage and preferred paid-cast branches. The build was
stopped again before linking so these source edits can be included coherently.

Seed 871010 reported two low-severity log/menu issues: Chandra's paid-cast
variants remain offered when the later same-plan mana is spent, then correctly
fall back to 2 damage; and the autonomous game log omits suspend/arrival entries
and shows Fireblast's printed cost even when its alternative cost paid. No rules
error was found. The Chandra menu label is now explicit about preference and
cost, but the redundant variants are left for later evaluation because pruning
before the plan's other mana-producing actions resolve could delete a legal line.

`CLAUDE.md` now instructs Claude and Codex to maintain the same repo checkpoint
for active long work, updating after milestones and before long runs. This file
is the Midrange Burn checkpoint; current source has not been rebuilt since the
latest fixes. The earlier Release binary remains in use by the in-flight Claude
sweep and suite-cost diagnostic.

### 2026-10-30 current run checkpoint

At 03:31 UTC, 11/15 first-pass Claude games had finished. Seed 871011 found only
the already-known redundant Chandra paid-cast menu branch, now distinguished by
the patched summary text. The first-pass sweep (`run_claude_sweep.py`, PID 57371)
and old-binary suite-cost diagnostic (`suite_gate.py --cost`, PID 67068) remain
running. The latter is in the mandatory full viewer reference check; its cost
verdict is diagnostic because it uses the old binary. The new source is compiling
as `./build.sh release -- --target mtg-test` on six jobs, output at
`build/midrange-burn-analysis/build-test-final.log`; this target leaves the old
`build/Release/mtg` executable available for those in-flight games. `git diff
--check` passed. When the first-pass Claude sweep finishes, build `mtg` and
`mtg-analyze`, run the focused tests, and proceed with the full validation list
above. If the old cost gate is still running, its snapshot binary is
`test/logs/regression/mtg.run`, so relinking Release/mtg is safe after that copy.

Seed 871012 confirmed a log/viewer gap: autonomous Light Up leaves unplayed exile
cards in `Player::staged_cards` between decision passes, while
`CollectBoardState` only serializes staged cards found in `hand`. The game rules
and draw order are correct, but `boardAfter.hand` and `.staged` omit those cards.
After the current build completes, teach `CollectBoardState` to include unexpired
`staged_cards` in both output arrays, consistent with human-play staging. Its
Searing Blaze landfall prompt finding is already fixed in source.

The old-binary same-run suite cost gate finished PASS: Midrange Burn's worst
searched case (2HG d5 seed 2002) was 34,414.787 core-ms/game against Hinata's
13,552.713, or 2.54x under the 3x limit. Log: `suite-cost.log`. This verdict must
be remeasured on the final binary after correctness fixes. Claude seed 871013
found a possible T5-vs-T6 AI misplay (saving Chain+Skewer to reduce Incinerator)
and a legal but apparently wasteful self-targeted Blaze, plus already-known
suspend logging gaps. Reassess the misplay on the final binary before tuning.

`scripts/check_references_safe.py` passed for all 21 worktrees: no unprotected
reference games. Container Codex config setup was checked for idempotence,
preservation of unrelated TOML settings and first-run backup; all passed.
`scripts/sync_codex_skills.py` regenerated 11 entrypoints successfully.

All 15 first-pass Claude games finished. Seed 871015 repeated the Blaze-in-hand
target bug already addressed by `SubsetHasMissingFixedBurnTarget`, and exposed
that a second Chandra always loses the generic keep-oldest legend rule. Keeping
the fresh copy after activating the first can give a second loyalty activation
that turn (CR 704.5j); the human viewer currently gets no choice. Add a
Midrange-specific keep heuristic for search and a human legend-keep choice in
the viewer protocol. Seed 871014 found only the known alternate-Fireblast cost
log and duplicate plan summaries. The first `mtg-test` build succeeded, and
focused Midrange tests passed 33/35: the two failures were overstrict test
assertions expecting an empty hand after Light Up staged two cards, while the
spell did cast and resolve. Those assertions were corrected in source. The
autonomous staged-card snapshot fix is also in source, awaiting rebuild.

The Chandra legend-rule fix is now in source: Midrange's provider scores an
unspent loyalty activation alongside loyalty total, with a default-on A/B flag
`MTG_MIDRANGE_LEGEND_KEEP`; real human resolution offers both copies through a
`legend_keep` board-click decision, including loyalty/activation state. A focused
unit test checks search's recommended copy and a human override. Viewer JS
syntax passed `node --check`. The full final build (`mtg`, `mtg-analyze`,
`mtg-test`) is running at `build-final-all.log`; once it succeeds, rerun all 36
focused Midrange cases, the 348 scenarios, and the viewer/Claude repros before
accepting the new behavior.

The final build passed. Focused Midrange unit tests passed 36/36 (515 assertions),
and coverage reports all 15 cards full. A direct seed-871015 protocol replay
reached `legend_keep` at decision 17, with both Chandra copies offered and their
loyalty/activation states serialized. Choosing option 0 kept physical card 7;
choosing option 1 kept physical card 8. Viewer type registry check passed all
40 emitted types. The full 348-scenario rerun is still in progress.

The 348/348 scenario rerun finished clean. The final Release binary also passed
a live `--claude-play` seed-871015 replay: decision 17 was `legend_keep`, and
choosing old/new kept card number 7/8 respectively. The final analyzer/profile
generation is now running (`analyzer-final.log`, 1,000 scoring games d5); full
unit-suite run is also in progress (`units-all-final.log`). After the profile
finishes, run `verify_deck.py`, the viewer 40-game gate, the leaf tie-break gate,
same-run Hinata suite cost gate, and a final Claude sweep before regression tiers.

The first full 2,130-case unit run found one onboarding-table failure:
`test_pirates_provider.cpp` requires every new deck folder to pin its expected
provider; Midrange Burn was missing from that table. The table now names
`MidrangeBurn`, and an incremental `mtg-test` build is running at
`build-test-routing.log`. The other 2,129 unit cases passed. Analyzer completed
its 1,000-game scoring stage and wrote a 14-card-score baseline profile; its
cost-aggregate diagnostic is still running.

The provider-routing table was rebuilt; a second full unit run is in progress
at `units-all-final2.log`. The regenerated profile is on disk (14 scored cards;
Mountain/basic excluded), with cost diagnostic still running. The 40-game viewer
auditor is running at `viewer-runtime-rebuilt.log`. A second 15-seed Claude
oracle sweep on the final binary/profile has started via
`run_claude_sweep_final.py`, with outputs under
`logs/midrange-burn/claude-sweep-final/` and launcher log
`claude-sweep-final-launch.log`. The binary is now stable; do not relink it
during this sweep. After these finish, run leaf tie-break and final suite gate.

The second full unit run passed all 2,130 test cases and 3,553,241 assertions.
Smoke tier passed its four new Midrange keys (no GT yet), and the 40-game viewer
auditor surfaced all five expected card-choice types PLUS the new `legend_keep`;
no hard miss. Analyzer cost reframe diagnostic is COST_NEUTRAL. The final same-run
Hinata suite cost gate is now running at `suite-cost-rebuilt.log`; it sets
`VPC_SKIP=1` because the formal regression tier will run the full reference
replay once on this binary.

Container ground truth is platform-specific: `.devcontainer/README.md` notes the
committed `test/regression_gt.txt` is Windows/MSVC and must not be overwritten by
Linux/GCC fingerprints. The harness, verifier and digest checker now honor
`REGRESSION_GT=test/regression_gt.linux.txt` and
`REGRESSION_GT_LOGS=test/gt_logs.linux` for a separate container baseline. The four
new smoke keys were accepted there after inspection; `check_gt_logs.py` reports
4 consistent, 0 stale, 0 missing. Regression and overnight Linux rows await their
final runs, and Windows baselines still require a Windows run.

Quick horizon tie-break A/B at production play settings completed 400 paired games
with zero changed plays and no sign. As the script recommends, a larger eight-block
sample (8,000 pairs, eight workers) is running at `leaf-final-b8.log`. The final
Claude sweep has completed seed 871002: AI and Claude both won on turn five; its
only confirmed flag is the known redundant unaffordable Chandra paid-cast branches,
whose fallback resolves correctly. Remaining final Claude seeds are in progress.

Final analyzer/profile generation completed on the rebuilt binary: 14 nonbasic
card scores, no hand-score gate, default one-to-five-land keep window. Cost
reframe A/B was neutral (4.72 vs 4.72 over 200 games). Discard evidence found no
real cleanup discards in 400 games, though 1,659 occurred inside rollouts; the
analyzer correctly returned `DISCARD_UNLABELLED` and requested a bound on that
unobservable ranking. A paired 200-game-per-arm `MTG_SHED_WORST` A/B is running
from `shed-ab.json`, with output `shed-ab.log` and per-game logs in `shed-wins/`.
Claude seed 871001 matched the autonomous turn-six win and raised an uncertain
missing bundled Bolt→Incinerator plan on a two-Mountain board with three damage
already dealt; the post-Bolt menu offered Incinerator immediately. Investigate
whether the flat subset cost gate omits a same-plan Incinerator discount and
whether that affects autonomous one-plan-per-phase search before final sign-off.

Seed 871003's T5-Claude/T6-AI gap is budget churn: a single-game autonomous
replay at the same d5 and seed with budget 100 (versus shipped 20) wins on T5
(`seed871003-b100.log`). The b20 line self-targeted Searing Blaze and killed its
own Lavamancer; it is legal but poor. No source change to suppress legal self
targets is justified without an A/B at shipped settings. Seed 871005's T4-Claude/
T5-AI gap came from Claude cracking Fiery Islet to draw a known Bolt; the generic
policy intentionally does not search clairvoyant digs (user ruling 2026-09-29),
so this is a known search-policy limitation, not an engine transition bug.

The final same-run cost attempt (`suite-cost-rebuilt.log`) ended without a verdict:
`test/logs/regression/batch.log` was emptied at completion, so `suite_gate.py`
reported no Midrange timings. The live stream had shown several Midrange and
Hinata result lines, but the file is zero bytes and `test/results/regression.env`
is empty; do not claim a pass. Re-run the pooled candidate/Hinata gate after
final source convergence and preserve its log. Claude seeds 871003 and 871004
finished: seed 871003 is a real T5-Claude/T6-AI quality candidate, with the AI
self-targeting Searing Blaze to kill its own Grim Lavamancer on T4. Investigate
the search/rollout valuation or candidate restrictions. Other flags are redundant
unpayable plan menu entries and a Lavamancer activation menu duplicate; rules
transitions were correct. Seed 871004 matched AI on T4.

The focused two-Mountain enumeration test was added to
`test/unit/test_midrange_burn_spells.cpp` and run on the stable engine: it fails
with no Bolt+Incinerator plan (`inc-enum-before.log`). This confirms a planner
coverage defect, not just a viewer display issue. Existing ApplyPlan tests prove
the legal sequence resolves; the missing piece is the flat subset's same-plan
discount rescue. `MTG_DBG_MULTI=5` reports `ef-no-interact`; the existing
`SubsetPayableSequential` walk already prices the live Incinerator discount after
Bolt, but the executor-feasibility rescue admits only enumerated interaction
classes. Add this exact interaction to the rescue gate. Keep `mtg` unchanged
until the current Claude sweep finishes, then fix and rerun the affected gates.

The eight-block leaf tie-break run was stopped after the confirmed planner gap;
its first two 1,000-game baseline jobs completed, but the sample would be stale
after the required engine fix. Rerun the large paired check on the final binary.

The old integration-review note about Chandra emblem/Incinerator trigger order is
already addressed in the integrated `FireOnCastTriggers`: every emblem has its own
`NoncombatTriggerScope`, and `FlushNoncombatTriggersAtPriorityBoundary` runs before
the next emblem target is chosen. Existing permanent tests cover stacked emblems.

Claude's two-land Skewer-before-Bolt menu finding is an intentional partial-line
feature of the human ordering search: `Plan::would_drop` records the skipped
Skewer, `WritePlansJson` emits `drops`, and the viewer warns. Deleting such plans
regressed saved references before (documented near `g_order_drop_label` in
TurnSolver). A directed test was revised to assert that any partial bundle
carries the label; run it with `MTG_HUMAN_PLAY=1 MTG_UNPRUNED=1` after rebuild.
Do not prune these ordering variants.

Final Claude seed 871012 won on T5 against the shipped b20 search's T6. A d5
budget-100 replay of the same seed wins on T5 (`seed871012-b100.log`), so this
too is budget pressure, not proof that greater depth is required. The proposed
line holds Chain+Skewer until T4 to discount Incinerator; the confirmed missing
same-plan discount may also contribute at b20, so reassess after the fix.

That directed human-order test now passes 1/1 with 11 assertions
(`order-label-test.log`), confirming the Skewer partial line is flagged in the
plan. It does not excuse the extra target prompt before the dropped spell; that
is a lower-severity viewer workflow limitation to disclose or fix separately.

The paired cleanup-discard bound finished (`shed-ab.log`): 200 games per arm at
d3/b10, base average 4.6800 vs worst 4.6900, one play and one win turn differed;
the inverted ranking was 0.0100 turn worse. This is a small but nonzero rollout
effect, with no real cleanup-discard labels. Keep the current ranking pending a
deck-authored bucket policy and A/B; do not call the axis inert.

All 15 final-binary Claude games completed on seeds 871001..871015. Claude
matched the autonomous win turn in 12 and won one turn earlier in 3: 871003
(budget-100 d5 recovers T5), 871005 (clairvoyant Fiery Islet dig, deliberately
outside the shipped search policy), and 871012 (budget-100 d5 recovers T5).
The sweep confirmed the two-Mountain Bolt+Incinerator bundled-line omission,
which the focused test also reproduces. Seed 871015 found that human Fireblast
through Chandra's paid-exile cast gave no chance to float mana from the basic
Mountains it was about to sacrifice; that legal mana can fund a second Chandra
the same turn. Other flags are display/log/redundancy issues, with no illegal
state transition found. Raw JSON is under `logs/midrange-burn/claude-sweep-final/`.

Source now has two targeted fixes awaiting build: the flat subset fallback admits
an Incinerator plus same-line face-damage selection to the existing sequential
payment walk in both greedy and searched enumerators; human Fireblast automatically
floats {R} from each untapped basic Mountain chosen for its sacrifice, which is
strictly beneficial before that land leaves play and does not alter autonomous
payment. Focused tests cover the missing bundled plan and the human mana float.
The Release rebuild is running at `build-rescue-final.log`; the previous verified
binary was preserved as `mtg-before-rescue` for A/B and reproducibility. Once
built, rerun focused/full unit, scenarios, viewer seed15, full verifier, cost
gate, regression tiers/GT, and leaf tie-break on the new binary.

`test/regression.sh` now streams each batch into its normal `batch.log` and a
unique temporary backup, restoring the normal log if the two differ at job end.
This protects the pooled cost measurement against the zero-byte log seen at
04:20 and preserves the live per-job stream; bash syntax check passed.

### 2026-10-30 post-rebuild checkpoint

The Release rebuild finished successfully. The directed two-Mountain
Bolt→Incinerator enumeration test passes (9 assertions), the human Fireblast
pre-sacrifice mana-float test passes (15 assertions), and the human partial-plan
warning test passes (11 assertions). Full unit tests, all scenarios, and the
final analyzer/profile regeneration are running in parallel; outputs are
`build/midrange-burn-analysis/{full-unit-final,scenarios-final,analyzer-final}.log`.
Next: inspect those results, replay Claude's affected seeds on the new profile,
then run the final viewer/mismatch/regression/leaf checks and record their
verdicts. The prior 15-game sweep used the pre-rescue binary, despite the
earlier note calling it the "final-binary" sweep.
Static coverage remains 15/15 full. The static viewer audit reports five
expected decision types (`free_cast`, `gy_return_one`, `sacrifice`, `scry`,
`target`) and no unmodeled oracle choice phrase. `git diff --check` passes.
The full unit suite now passes 2,133 cases and 3,553,274 assertions. All 348
scenarios pass. `check_references_safe.py` finds every changed reference
protected across 21 worktrees. Analyzer card scoring is still active.
The analyzer has now written the final 14-card-score profile with default land
window 1–5 and no hand-score gate; its cost reframe diagnostic remains active.
Runtime viewer sweep: `viewer-runtime-final.log` (seed 871100, 40 games).
Linux full-suite smoke: `smoke-final.log` with platform-specific GT env.
Rebuilt-binary Claude replay: `run_claude_sweep_rescue.py`, output
`logs/midrange-burn/claude-sweep-rescue/`, launcher log
`claude-sweep-rescue.log`, seeds 871001–871015 (three concurrent agents).
The analyzer's 200+200 game cost-reframe diagnostic finished COST_NEUTRAL on
the rebuilt binary. It is now running the 400-game single-thread discard
evidence pass. Linux smoke has completed its 348 scenario preflight and entered
the pooled game batch.
The rebuilt-binary 40-game viewer sweep passes: all five expected card
decision types surfaced, as did `legend_keep`; no runtime hard miss.
In the still-running smoke batch, Midrange Burn d3 is 4.9000 versus the
pre-rescue Linux GT's 4.9200. The sole changed win turn is game index 38,
T6→T5. The d5 and 2HG d5 averages are unchanged at 4.7700 and 6.4800;
their play digests change in 19/100 and 6/25 games, respectively, as the
newly legal bundle enters the plan pool. Inspect the full audit before accept.
The completed smoke batch also sees d0 6.0230 versus old 6.0590. A control
run with the preserved pre-rescue binary but the new profile scores 6.0580
(`old-binary-new-profile-d0.log`), so almost all of this d0 improvement is
the engine change, not the profile rescore. The full per-game audit is active;
do not accept GT until the Chandra menu fix and final rerun.
The smoke audit finished with no searched-depth slower game, one d3 T6→T5,
and 36 searched play changes at unchanged score. Its only d0 slower game is
index 758 (old GT T5, new T6). Replaying that exact seed+game-index on the
preserved pre-rescue binary with the regenerated profile also produces T6,
so that isolated slowdown comes from profile rescore, while the 1,000-game
d0 aggregate still improves by 0.036 turns.
Claude replay seed 871002 completed (AI T5, Claude T5). It reconfirmed that
`CollectActions` emits Chandra +1 nested Searing Blaze paid-cast labels even
with only one Fiery Islet available; execution correctly falls back to damage.
The payable-option helper already exists, but the loyalty action generator at
`TurnSolver.cpp` around line 38343 previously serialized every nested cast
action without calling it. A directed test and filter are now in source,
awaiting the test-only rebuild. Its second flag was
an uncertain cosmetic ordering mismatch on a partial Incinerator line.
Claude replay seed 871001 completed (AI T6, Claude T5), matching the same
mulligan and bottom. Claude held Chain Lightning from T1, cracked Fiery Islet
T3 into Swiftspear, then dealt six noncombat damage with Chain+Bolt T4 to
cast Incinerator for {R} on three lands. This uses only reveal-six cards,
so is a real search-quality candidate, not clairvoyance. Claude reported
that b200 still wins T6. Its Chandra menu also showed an unaffordable paid
cast of a second Chandra with all four lands tapped, covered by the same
candidate filter above. The printed cast order on some Incinerator plans
differs from execution order; no illegal state was observed.
The directed Chandra enumerator test passed in autonomous mode but failed as
expected under `MTG_HUMAN_PLAY=1 MTG_UNPRUNED=1` (two unaffordable branches).
`CollectActions` now probes each serialized Chandra nested cast with the
existing `ChandraPaidCastOptionPayable` before publishing it. Only `mtg-test`
is being rebuilt for green verification; `mtg` remains the pre-filter binary
until the Claude sweep and suite cost gate finish.
Claude seed 871003 repeated the AI T6 vs Claude T5 split previously recovered
at higher search budget; it saw the intentional labeled Skewer-first partial
line and another unaffordable Chandra nested cast. Seed 871004 matched T4;
its only flag was duplicate Grim Lavamancer plan summaries, with the real
target chooser still offered at resolution. No illegal state was reported.
Claude seed 871005 matched the earlier AI T5 vs Claude T4 finding, driven by
Fiery Islet drawing a known Bolt and held burn into Swiftspear; this is the
documented clairvoyant-dig case outside the shipped search policy. It also
exposed a REAL viewer-plan legality gap: a pre-main plan listed Searing Blaze
targeting a Swiftspear still in hand, then Swiftspear. It had no `drops`
warning, and execution skipped Blaze. The legal Swiftspear-first order was
offered. A directed ordered-enumeration test is now in source (not built yet);
reject the impossible target-before-creature order without pruning the legal
one, then rerun the viewer gate.
The directed Searing Blaze ordered-plan test failed as expected under human
flags. It counted exactly one illegal searched-order variant and zero illegal
canonical variants. `EnumerateMainPlans` now removes only searched-order
plans whose pinned burn target is still a card in the initial hand at that
point in cast order; the Swiftspear-first plan remains. Test-only rebuild is
running; the current `mtg` game binary is still pre-filter for the active
Claude/cost jobs.
The final analyzer/discard evidence run completed successfully on the
pre-Chandra-filter binary: 14 card scores, cost reframe COST_NEUTRAL,
400 d3 games, 0 real cleanup discards, 1,627 rollout discards (all with
fewer than four lands), verdict DISCARD_UNLABELLED. The earlier paired
worst-vs-default bound was 4.6800 vs 4.6900 over 200 games per arm;
the axis is small but nonzero. Seed 871006 matched AI/Claude T5 with no flags.
The pre-filter same-run suite cost gate failed: Midrange Burn's 2HG d5 row
measured 32,085.827 ms/game versus Hinata's pinned 9,358.840 ms/game,
3.43× above the 3× limit. This run overlapped compiler/Claude work;
an earlier otherwise similar run passed at 2.54× (Hinata 13,552.713
ms/game), so remeasure the final binary with no competing game/compiler jobs.
If it still fails, reduce the 2HG candidate cost with a provider-owned,
measured heuristic before claiming suite admission. Do not raise the limit.
Claude replay seeds 871008–871010 completed. 871008 matched T6 and saw the
known labeled Skewer-first partial line with an unnecessary target prompt.
871009 matched T5 and reproduced the target-before-cast Blaze bug with Grim
Lavamancer instead of Swiftspear; the new generic human searched-order guard
covers both. 871010 matched T4 and saw unaffordable Chandra paid-cast labels
with redundant Fireblast Mountain-pair variants; the affordability filter
covers the former, while the latter are equivalent legal cost choices.
The test-only rebuild completed. Both human-menu reproductions pass under
`MTG_HUMAN_PLAY=1 MTG_UNPRUNED=1` (Chandra 14 assertions, Blaze order 12).
The normal focused Midrange unit set passes 40/40, 548 assertions. The Blaze
order assertion is human-only; autonomous canonical plan vectors need not
reflect execution order. `git diff --check` passes. The `mtg` game binary
has still not been relinked during the Claude sweep.
Claude seeds 871011 (AI/Claude T4) and 871012 (AI T6, Claude T5) finished.
Their affordability-drop flags are the already documented partial-plan
policy: the executor reports `dropped_casts`, and legal alternate orders
remain offered. Seed 871011 also saw unaffordable Chandra nested labels,
covered by the pending filter. The sweep subsequently completed.
The rebuilt-pre-menu-fix Claude sweep finished all 15 seeds, all complete
(11 same win turn, Claude one turn earlier on 871001, 871003, 871005,
871012). Seed 871014 matched T4 and saw only duplicate Fireblast cost-pair
plan summaries. Seed 871015 matched T6 with no flag; it did not exercise
the earlier Fireblast pre-sacrifice float branch, which remains covered by
the directed unit and needs a targeted final viewer replay. The sweep's
newly confirmed menu defects are Chandra's unaffordable paid-cast label
and Searing Blaze target-before-creature searched order; both are fixed
in source and have red-before/green-after tests. `mtg`/`mtg-analyze` final
link is now in progress at `build-menu-final.log`.
The menu-fix game and analyzer binaries linked successfully. Direct CLI
replay of seed 871002's Chandra decision now offers only decline/+RR/-3
legal modes and no unaffordable paid Searing Blaze cast. The old seed 871005
menu choice prefix diverges earlier after filtering, so its Blaze fix is
covered by the red-before/green-after human ordered-plan unit, pending
the final runtime viewer sweep. Pinned-seed analyzer/profile regeneration
(`--analyzer-seed 871900`) is running at `analyzer-pinned-final.log`, and
all-scenario final rerun at `scenarios-after-menu.log`.
The final linked binary passed 348/348 scenarios. The pinned analyzer has
written a reproducible 14-card-score profile (seed 871900), default land
window 1–5, no hand-score threshold; its cost diagnostic is running before
discard evidence. The 40-game final viewer gate is running at
`viewer-runtime-menu-final.log`. A 2HG paired budget manifest with b10/b15/b20
on the same 75 seeds is prepared under `build/midrange-burn-analysis/` and
will run after the analyzer's cost diagnostic to avoid contention.
The pinned analyzer's cost reframe is COST_NEUTRAL and it has entered its
400-game discard evidence phase. The 2HG b10/b15/b20 paired 75-game batch
is now running at `2hg-budget-ab.log` (wins in `2hg-budget-ab-wins/`), using
the pinned final profile. The final viewer driver has reached seed 871134
of 871100–871139 without stopping.
The final 40-game viewer audit has completed PASS on the linked binary and
pinned profile: all five expected card decision types plus `legend_keep`
surfaced, with no oracle-text choice phrase left unmodeled.
The final-profile paired 2HG budget batch completed (75 identical seeds per
arm): b20 avg 6.2667, 26,885.7 ms/game; b15 avg 6.2800, 20,486.5 ms/game;
b10 avg 6.3067, 15,425.4 ms/game. Versus b20, b15 wins one game one turn
earlier and loses two one turn later (net +1 turn/75); b10 wins two one turn
earlier and loses five one turn later (net +3 turns/75). Keep b20 if the
isolated formal 3× Hinata gate passes; lowering budget has measured quality
cost. The analyzer's evidence has moved to the 40-game shed census; wait for
it to finish before rerunning the formal gate.
An additional seed-871001 diagnostic stayed T6 at d5/b1000 and d6/b200;
the held-Chain T5 line is a deck-specific heuristic candidate, not simply
a budget bump. A full unit rerun on the two menu fixes is active at
`full-unit-after-menu.log`; the game binary still remains fixed.

The final linked binary passed the full unit suite (2,135/2,135 cases,
3,553,288 assertions) and all 348 scenarios. The pinned analyzer completed
with 14 card scores and a neutral cost reframe. Its discard evidence found
zero real cleanup sheds in 400 d3 games and 1,627 rollout sheds, all below
four battlefield lands. The authored proposed bucket policy is in
`docs/design/midrange-burn-discard-policy.md`; it is not implemented or adopted
pending the skill's user review and a non-inferiority A/B. The isolated formal
same-run Hinata suite cost check is running at `suite-cost-isolated-final.log`;
do not launch another regression tier until it finishes, because both use the
same batch output files. Final viewer surface sweep passed 40/40 games.

The isolated b20 gate completed FAIL at 4.26x Hinata: Midrange Burn's worst
searched row was 2HG d5/b20 at 20,697.813 ms/game; same-run Hinata max was
4,852.993 ms/game. The paired 75-seed 2HG budget A/B supports b10 as a
measured tradeoff: b10 was +3 net win turns /75 games versus b20, with two
faster and five slower, and roughly 0.57x cost. All three 2HG suite tier rows
now use b10; single-player remains d5/b20. A second isolated same-run gate is
running at `suite-cost-b10.log`. This is the proposed 2HG play budget if it
passes. Do not accept any tier until it does.

The isolated b10 gate PASSED: Midrange worst searched per-game cost was
11,606.240 ms (2HG d5/b10), Hinata's same-run max 4,148.267 ms, ratio
2.80x against the fixed 3x cap. `suite-cost-b10.log` has the exact verdict.
The final filtered Linux smoke tier is running at `smoke-b10-final.log`.
Once its changed games are reviewed, accept with the same two-key filter,
then run the filtered regression and overnight tiers sequentially.

The b10 smoke run emitted `PLAY-SETTING SHAPE: row b10, ships b20` for 2HG.
That makes b10 an off-settings diagnostic, not an acceptable suite admission.
Its four keys were therefore NOT accepted, and all three 2HG rows were
restored to shipped b20. To fit the gate at shipped settings, the provider now
has a default-on, =0-restorable autonomous single-target own-burn prune. Direct
single-target burn at ourselves cannot improve the goldfish clock; Searing
Blaze's self-creature axis stays because a no-landfall Blaze can trigger
Swiftspear prowess without killing it. Enemy face/permanent targets remain.
Human and unpruned menus still enumerate every legal target. A directed test
covers those paths. Build in progress at
`build-self-prune.log`; measure quality and cost before considering adoption.

The own-burn prune build and full unit suite passed (3,553,305 assertions);
focused default, human/unpruned, and flag-off directed tests passed. On the
same 75 2HG seed-2002 games, b20 improved from 6.2667 to 6.1867 average
turns: three faster games (gi28 T8->T5, gi30 T6->T5, gi38 T8->T6), zero slower,
nine same-turn changed plays. The first formal cost run read 19,670.560
ms/game against Hinata 6,913.520, ratio 2.85x, but it overlapped the full
unit run during its early reference jobs. It is not an isolated admission
result. A clean repeat with `MTG_SLOW_GAME_MS=1000` is active at
`suite-cost-self-prune-isolated.log`; its per-game tail lines will identify
the expensive seeds for any further optimization.

The clean b20 cost repeat PASSED at 2.95x Hinata (Midrange worst 2HG
19,317.627 ms/game, Hinata 6,539.813 ms/game, fixed limit 3x). The long
2HG outliers are seed 2014/gi12 (341,642 wall-ms, 1,307,542 units) and
seed 2024/gi22 (437,607 wall-ms, 619,187 units; see
`test/logs/regression/batch.err`); the cost gate's
`MTG_SLOW_GAME_MS=1000` is diagnostic only and does not alter decisions.
The final b20 filtered smoke tier is running at `smoke-self-prune-b20.log`.

The final smoke tier finished 348/348 scenarios and four changed Midrange
digests. The per-game audit found three searched slower outcomes against the
old Linux smoke GT: single-player gi31 T6->T7 at d3 and d5, and 2HG gi10
T7->unwon by T8, with two faster 2HG seeds offsetting the latter in that
25-game row. All have identical kept hand and draws, and the slower turns
persist at 4x/16x budget (`classify-smoke-self-prune.log`): these are real
search-quality tradeoffs, not shuffle variance. The same-binary flag-off A/B
(`smoke-arm-off.log`) attributes them to the own-burn prune: d0 gained 154
net turns/1000; d3 lost 1/50, d5 lost 1/100, and 2HG netted 0/25. On the
larger paired 2HG 75-game sample the prune gained 6 net turns with no slower
game. Target-pruned play is legal; gi31's first changed T3 picks Rift Bolt
instead of a Searing Blaze line, while 2HG gi10 spends burn early and ends
one damage short on T8. These known quality losses should be called out in
smoke acceptance; do not call them budget churn.

Smoke was accepted with an `accepted-with-regressions` provenance note into
the Linux-specific ground truth (`test/regression_gt.linux.txt` plus four
`test/gt_logs.linux/*.wins`), checked 4 consistent/0 stale/0 missing.
Filtered regression is now running at `regression-self-prune-b20.log`.

The filtered regression tier completed 7 new keys (1,456 games) with
348/348 scenario preflight. All seven were accepted with the exact two-key
filter into Linux GT; `check_gt_logs` reported 7 consistent/0 stale/0 missing.
The required filtered overnight ENTRY tier is now running at
`overnight-self-prune-b20.log`. After reviewing/accepting its new keys, run
the final verifier and horizon tie-break check.

Overnight finished 18 new keys / 11,206 games with all 348 scenarios passing.
All 18 were accepted with the two-key filter; Linux GT now has 29 consistent
Midrange rows across all three tiers, no stale/missing wins logs. Four d5
single-player blocks: 4.8433, 4.7700, 4.8167, 4.7733; four d3 blocks:
4.8400, 4.8400, 4.8200, 4.8375; d0 blocks 5.9055, 5.8780, 5.8420,
5.7950; four 2HG d5 blocks 6.1600, 6.1000, 6.1700, 6.0400. Search-depth
sanity is plausible and d5 improves over d0 by about one turn.

The full `verify_deck.py --write-ledger` run completed: coverage 15/15,
live Scryfall costs PASS, 646 hard card fields PASS with 16 allowlisted global
divergences, all tiers PASS, viewer surface and wiring PASS, mismatch nonconv
and fd-oracle PASS over seeds 7001/8001 x60 per arm, play invariants PASS over
8 games/236 decisions, suite membership PASS. It initially disclosed that the
Claude sweep section lacked its machine-readable flag count; the user-owned
heading now records `flags: 0 unresolved`, and a direct gate call returns PASS.
The generated verifier block remains in this file with the detailed advisory
list, largely global oracle-text wording divergences rather than Midrange hard
field mismatches.

The quick horizon leaf tie-break check was 0 changed in 400 paired games,
which its own script calls NO SIGN, not a pass. The required eight-block
follow-up is active at `leaf-final-8blocks.log` (16,000 games / 8,000 pairs,
play settings, 24 threads). Do not infer a leaf verdict until it completes.

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-10-30)

`verify_deck.py decks/Midrange Burn/Midrange Burn.cod --write-ledger` -> **PASS**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 15 cards full (missing=0, partial=0) |
| card_costs | PASS | yes | all mana costs match Scryfall (cost/cmc only) |
| card_fields | PASS | yes | 647 cards match snapshot (cost/PT/types/keywords); 16 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| regression_tiers | PASS | yes | in all 3 suite tiers with GT (midrangeburn) |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | 5 type(s) wired (emitter + GUI): free_cast, gy_return_one, sacrifice, scry, target |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 8001] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/215 decisions: determinism+integrity+progress hold |
| claude_sweep | PASS | yes | Claude-play sweep recorded, 0 unresolved flags |
| suite | PASS | yes | suite gate: Midrange Burn is regression key `midrangeburn` |

### Pending user sign-off (block the gate until fixed OR approved below)
_none_ -- every blocking gate is green or already signed off.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is
- allowlisted divergence -- Auriok Champion [keywords]: 'Protection from black and from red' is a real keyword but inert in goldfishing on all four DEBT axes (the passive opponent never damages, targets, bl
- allowlisted divergence -- Brightcap Badger [mana_cost]: ADVENTURE CARD (CR 715), authored as TWO entries -- 'Brightcap Badger' (the {3}{G} 3/4 creature face) and 'Fungus Frolic' (the {2}{G} instant half). S
- allowlisted divergence -- Fungus Frolic [mana_cost]: The ADVENTURE HALF of Brightcap Badger // Fungus Frolic -- see the Brightcap Badger entry. Scryfall's combined mana_cost is '{3}{G} // {2}{G}'; the in
- allowlisted divergence -- Fungus Frolic [P/T]: An Instant has no power or toughness. Scryfall reports 3/4 because it describes the COMBINED card, whose creature face is the 3/4 Brightcap Badger. Gi
- allowlisted divergence -- Mycoloth [keywords]: 'Devour 2' is a real keyword and is FULLY MODELLED -- but via the `devour` CardParam, not via Keyword::Devour. The engine's Keyword enum deliberately
- allowlisted divergence -- Brutal Cathar [keywords]: Transforming DFC (Brutal Cathar // Moonrage Brute): Scryfall reports CARD-LEVEL keywords for both faces. The front face's only keyword is Daybound, mo
- allowlisted divergence -- Moonrage Brute [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the front's Daybound and the layout's Transform; Nightbound is modelled structurall
- allowlisted divergence -- Bloodline Keeper [keywords]: Transforming DFC (Bloodline Keeper // Lord of Lineage): Scryfall reports CARD-LEVEL keywords for both faces (Flying, Transform). Transform is the layo
- allowlisted divergence -- Lord of Lineage [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the layout's Transform; the back face carries no transform of its own (transform_fr
- oracle_text advisory -- Gideon, Ally of Zendikar: oracle_text diverges (similarity 0.10); scryfall='+1: Until end of turn, Gideon becomes a 5/5 Human Soldier Ally creature with indestructible that\'s still a pl
- oracle_text advisory -- Basri, Tomorrow's Champion: oracle_text diverges (similarity 0.15); scryfall="{W}, {T}, Exert Basri: Create a 1/1 white Cat creature token with lifelink. (An exerted creature won't untap d
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.31); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.40); scryfall="As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.15); scryfall='As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.21); scryfall='As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.
{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.35); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.
{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.
{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.
You may choose not to untap this land during your untap step.
At the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.
{T}: Put a storage counter on this land.
{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)
This land enters tapped.
When this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.
{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.
Sacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.45); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.27); scryfall='First strike (This creature deals combat damage before creatures without first strike.)
Other Knight creature
- oracle_text advisory -- Valiant Knight: oracle_text diverges (similarity 0.18); scryfall='Other Knights you control get +1/+1.
{3}{W}{W}: Knights you control gain double strike until end of turn.'
- oracle_text advisory -- Kinsbaile Cavalier: oracle_text diverges (similarity 0.05); scryfall='Knight creatures you control have double strike.'
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.
{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins
Other Knights you control get +2/+2 and have protection from Assassins.
When Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance
Adeline's power is equal to the number of creatures you control.
Whenever you attack, for each opp
- oracle_text advisory -- Accorder Paladin: oracle_text diverges (similarity 0.33); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)'
- oracle_text advisory -- Silverblade Paladin: oracle_text diverges (similarity 0.25); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Niv-Mizzet, the Firemind: oracle_text diverges (similarity 0.16); scryfall='Flying
Whenever you draw a card, Niv-Mizzet deals 1 damage to any target.
{T}: Draw a card.'
- oracle_text advisory -- Niv-Mizzet, Parun: oracle_text diverges (similarity 0.23); scryfall="This spell can't be countered.
Flying
Whenever you draw a card, Niv-Mizzet deals 1 damage to any target.
Wh
- oracle_text advisory -- Curiosity: oracle_text diverges (similarity 0.16); scryfall='Enchant creature
Whenever enchanted creature deals damage to an opponent, you may draw a card.'
- oracle_text advisory -- Tandem Lookout: oracle_text diverges (similarity 0.38); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Hero of Bladehold: oracle_text diverges (similarity 0.50); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)
When
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.
{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)
{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying
Rot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.
Whenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.17); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.
Target
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.

- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.30); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.32); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.
Whenever you tap this land for mana, target opponent creates a 1/1 colorless
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.05); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.32); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.
Draw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.16); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.
Ferocious — If you control a creature with power 4 or greater, those creatures don't
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample
Spells you cast cost {1} less to cast for each target.
Spells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {U}{
- oracle_text advisory -- Orzhov Basilica: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {W}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.28); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —
• Tap X target permanents.
• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying
{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.10); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.05); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.
Splice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste
Other creatures you control have haste.
Whenever an opponent casts a creature or planeswalker
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste
When Karrthus enters, gain control of all Dragons, then untap all Dragons.
Other Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.25); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying
Whenever this creature or another Dragon you control enters, it deals X damage to any target, where X
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying
Whenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying
Whenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.
Storm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.
If this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.
Whenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.33); scryfall='Enchant creature
Enchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature
Enchanted creature gets +2/+0 and has trample.
When this Aura is put into a graveyard from
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.35); scryfall='Enchant creature with another Aura attached to it
Enchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature
Enchanted creature gets +2/+2 and has trample.
Whenever enchanted creature deals damage, yo
- oracle_text advisory -- Hyena Umbra: oracle_text diverges (similarity 0.52); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has first strike.
Umbra armor (If enchanted creature woul
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)
Umbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature
Enchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature
Enchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature
Enchanted creature gets +1/+0 and has flying.
{3}{W}: Return this card from your graveyard
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature
Enchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature
Enchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)
Whenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.77); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)
Enchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Darkbore Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {B}.'
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)
Other Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.
Goblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.
{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.
{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.
Whenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.46); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.24); scryfall='First strike
When this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach
When this creature enters, it deals 2 damage to any target.
Channel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Three Tree City: oracle_text diverges (similarity 0.47); scryfall='As Three Tree City enters, choose a creature type.
{T}: Add {C}.
{2}, {T}: Choose a color. Add an amount of
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.
When this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.
Whenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.
Whenever a creature
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.15); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.24); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.19); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.
{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment,
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.
Search your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.28); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.29); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.33); scryfall='Flying, first strike, trample, lifelink, haste
At the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste
Whenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything
If Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance
This creature gets +1/+1 for each color among permanents you control.
{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)
{B},
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.18); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)
Equip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying
Whenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.
−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.
−2: Gain control of target creature.
−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")
+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.39); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.08); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying
{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.35); scryfall="Choose five. You may choose the same mode more than once.
• Target permanent phases out.
• Target player dra
- oracle_text advisory -- Disenchant: oracle_text diverges (similarity 0.21); scryfall='Destroy target artifact or enchantment.'
- oracle_text advisory -- Mirrorwing Dragon: oracle_text diverges (similarity 0.42); scryfall='Flying
Whenever a player casts an instant or sorcery spell that targets only this creature, that player copie
- oracle_text advisory -- Zada, Hedron Grinder: oracle_text diverges (similarity 0.57); scryfall='Whenever you cast an instant or sorcery spell that targets only Zada, copy that spell for each other creature
- oracle_text advisory -- Goblin Instigator: oracle_text diverges (similarity 0.32); scryfall='When this creature enters, create a 1/1 red Goblin creature token.'
- oracle_text advisory -- Fists of Flame: oracle_text diverges (similarity 0.36); scryfall="Draw a card. Until end of turn, target creature gains trample and gets +1/+0 for each card you've drawn this t
- oracle_text advisory -- Luxurious Libation: oracle_text diverges (similarity 0.24); scryfall='Target creature gets +X/+X until end of turn. Create a 1/1 green and white Citizen creature token.'
- oracle_text advisory -- Fortifying Draught: oracle_text diverges (similarity 0.33); scryfall='You gain 2 life. Target creature gets +X/+X until end of turn, where X is the amount of life you gained this t
- oracle_text advisory -- Gold Rush: oracle_text diverges (similarity 0.36); scryfall='Create a Treasure token. Until end of turn, up to one target creature gets +2/+2 for each Treasure you control
- oracle_text advisory -- Ancestral Anger: oracle_text diverges (similarity 0.52); scryfall='Target creature gains trample and gets +X/+0 until end of turn, where X is 1 plus the number of cards named An
- oracle_text advisory -- Oracle's Restoration: oracle_text diverges (similarity 0.10); scryfall='Target creature you control gets +1/+1 until end of turn. You draw a card and gain 1 life.'
- oracle_text advisory -- Expedite: oracle_text diverges (similarity 0.29); scryfall='Target creature gains haste until end of turn.
Draw a card.'
- oracle_text advisory -- Impolite Entrance: oracle_text diverges (similarity 0.18); scryfall='Target creature gains trample and haste until end of turn.
Draw a card.'
- oracle_text advisory -- Scale the Heights: oracle_text diverges (similarity 0.49); scryfall='Put a +1/+1 counter on up to one target creature. You gain 2 life. You may play an additional land this turn.\
- oracle_text advisory -- Twinflame: oracle_text diverges (similarity 0.42); scryfall="Strive — This spell costs {2}{R} more to cast for each target beyond the first.
Choose any number of target c
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.39); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.
When this land enters, you gain 1 life.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.
Equip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.
Equip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.
{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.
Whenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.
Equipped creature gets +3/+0 and h
- oracle_text advisory -- Ancient Den: oracle_text diverges (similarity 0.02); scryfall='{T}: Add {W}.'
- oracle_text advisory -- Bone Saw: oracle_text diverges (similarity 0.29); scryfall='Equipped creature gets +1/+0.
Equip {1} ({1}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Paradise Mantle: oracle_text diverges (similarity 0.10); scryfall='Equipped creature has "{T}: Add one mana of any color."
Equip {1}'
- oracle_text advisory -- Cathar's Shield: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +0/+3 and has vigilance.
Equip {3} ({3}: Attach to target creature you control. Equip
- oracle_text advisory -- Accorder's Shield: oracle_text diverges (similarity 0.23); scryfall="Equipped creature gets +0/+3 and has vigilance. (Attacking doesn't cause it to tap.)
Equip {3} ({3}: Attach t
- oracle_text advisory -- Kite Shield: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +0/+3.
Equip {3} ({3}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Spidersilk Net: oracle_text diverges (similarity 0.22); scryfall='Equipped creature gets +0/+2 and has reach. (It can block creatures with flying.)
Equip {2} ({2}: Attach to t
- oracle_text advisory -- Golem-Skin Gauntlets: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+0 for each Equipment attached to it.
Equip {2} ({2}: Attach to target creature you
- oracle_text advisory -- Skateboard: oracle_text diverges (similarity 0.19); scryfall='When this Equipment enters, tap target permanent.
Equipped creature gets +1/+0 and has haste.
Equip {1} ({1}
- oracle_text advisory -- Dragonfire Blade: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +2/+2 and has hexproof from monocolored.
Equip {4}. This ability costs {1} less to act
- oracle_text advisory -- Deconstruction Hammer: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+1 and has "{3}, {T}, Sacrifice Deconstruction Hammer: Destroy target artifact or en
- oracle_text advisory -- Sram, Senior Edificer: oracle_text diverges (similarity 0.08); scryfall='Whenever you cast an Aura, Equipment, or Vehicle spell, draw a card.'
- oracle_text advisory -- Cid, Freeflier Pilot: oracle_text diverges (similarity 0.13); scryfall='Equipment and Vehicle spells you cast cost {1} less to cast.
Jump — During your turn, Cid has flying.
{2}, {
- oracle_text advisory -- Sigarda's Aid: oracle_text diverges (similarity 0.14); scryfall='You may cast Aura and Equipment spells as though they had flash.
Whenever an Equipment you control enters, yo
- oracle_text advisory -- Dwalin, Weaponmaster: oracle_text diverges (similarity 0.12); scryfall='First strike
Whenever Dwalin enters or attacks, put a hone counter on each Equipment you control. (Each hone
- oracle_text advisory -- Ardenn, Intrepid Archaeologist: oracle_text diverges (similarity 0.17); scryfall='At the beginning of combat on your turn, you may attach any number of Auras and Equipment you control to targe
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.
Remove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.43); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.
Metalcraft — Equipment you control have equip
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.30); scryfall='First strike
Balan has double strike as long as two or more Equipment are attached to it.
{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying
Whenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.
{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.03); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.39); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
- oracle_text advisory -- Mirri's Guile: oracle_text diverges (similarity 0.44); scryfall='At the beginning of your upkeep, you may look at the top three cards of your library, then put them back in an
- oracle_text advisory -- Call of the Wild: oracle_text diverges (similarity 0.52); scryfall="{2}{G}{G}: Reveal the top card of your library. If it's a creature card, put it onto the battlefield. Otherwis
- oracle_text advisory -- Hornet Queen: oracle_text diverges (similarity 0.41); scryfall='Flying, deathtouch
When this creature enters, create four 1/1 green Insect creature tokens with flying and de
- oracle_text advisory -- Terastodon: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may destroy up to three target noncreature permanents. For each permanent put i
- oracle_text advisory -- Elderscale Wurm: oracle_text diverges (similarity 0.53); scryfall='Trample
When this creature enters, if your life total is less than 7, your life total becomes 7.
As long as
- oracle_text advisory -- Craterhoof Behemoth: oracle_text diverges (similarity 0.44); scryfall='Haste
When this creature enters, creatures you control gain trample and get +X/+X until end of turn, where X
- oracle_text advisory -- Worldspine Wurm: oracle_text diverges (similarity 0.39); scryfall="Trample
When this creature dies, create three 5/5 green Wurm creature tokens with trample.
When Worldspine W
- oracle_text advisory -- Vaultborn Tyrant: oracle_text diverges (similarity 0.47); scryfall="Trample
Whenever this creature or another creature you control with power 4 or greater enters, you gain 3 lif
- oracle_text advisory -- Natural Order: oracle_text diverges (similarity 0.38); scryfall='As an additional cost to cast this spell, sacrifice a green creature.
Search your library for a green creatur
- oracle_text advisory -- Turntimber Symbiosis: oracle_text diverges (similarity 0.45); scryfall='Look at the top seven cards of your library. You may put a creature card from among them onto the battlefield.
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.
{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.12); scryfall='First strike
{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.
{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.
Whenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.25); scryfall='First strike
Other Minotaurs you control have first strike.
As long as you have one or fewer cards in hand,
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample
Other Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.
{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.21); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.
{2}, Sacrifice another creatu
- oracle_text advisory -- Atsushi, the Blazing Sky: oracle_text diverges (similarity 0.43); scryfall='Flying, trample
When Atsushi dies, choose one —
• Exile the top two cards of your library. Until the end of
- oracle_text advisory -- Inferno of the Star Mounts: oracle_text diverges (similarity 0.35); scryfall="This spell can't be countered.
Flying, haste
{R}: Inferno of the Star Mounts gets +1/+0 until end of turn. W
- oracle_text advisory -- Dragon Tempest: oracle_text diverges (similarity 0.34); scryfall='Whenever a creature you control with flying enters, it gains haste until end of turn.
Whenever a Dragon you c
- oracle_text advisory -- Urza's Incubator: oracle_text diverges (similarity 0.15); scryfall='As this artifact enters, choose a creature type.
Creature spells of the chosen type cost {2} less to cast.'
- oracle_text advisory -- Mind Stone: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.
{1}, {T}, Sacrifice this artifact: Draw a card.'
- oracle_text advisory -- Fire Diamond: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.
{T}: Add {R}.'
- oracle_text advisory -- Dragonspeaker Shaman: oracle_text diverges (similarity 0.15); scryfall='Dragon spells you cast cost {2} less to cast.'
- oracle_text advisory -- Glorybringer: oracle_text diverges (similarity 0.46); scryfall="Flying, haste
You may exert this creature as it attacks. When you do, it deals 4 damage to target non-Dragon
- oracle_text advisory -- Haven of the Spirit Dragon: oracle_text diverges (similarity 0.32); scryfall='{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana only to cast a Dragon creature spell.
{2}, {T}
- oracle_text advisory -- Nest Invader: oracle_text diverges (similarity 0.27); scryfall='When this creature enters, create a 0/1 colorless Eldrazi Spawn creature token. It has "Sacrifice this token:
- oracle_text advisory -- Young Pyromancer: oracle_text diverges (similarity 0.28); scryfall='Whenever you cast an instant or sorcery spell, create a 1/1 red Elemental creature token.'
- oracle_text advisory -- Undercellar Myconid: oracle_text diverges (similarity 0.39); scryfall='Whenever this creature enters or dies, create a 1/1 green Saproling creature token.
{T}: Add one mana of any
- oracle_text advisory -- Frontline Heroism: oracle_text diverges (similarity 0.39); scryfall='When this enchantment enters, create a 1/1 red Soldier creature token with haste.
Whenever you cast a spell t
- oracle_text advisory -- Adarkar Wastes: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.
{T}: Add {W} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Caves of Koilos: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {W} or {B}. This land deals 1 damage to you.'
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Llanowar Wastes: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {B} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.
{T}: Add {G} or {W}.
{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
- oracle_text advisory -- Mariposa Military Base: oracle_text diverges (similarity 0.26); scryfall='You may have this land enter tapped. If you do, you get two rad counters.
{T}: Add {C}.
{5}, {T}: Draw a car
- oracle_text advisory -- Eldrazi Displacer: oracle_text diverges (similarity 0.47); scryfall="Devoid (This card has no color.)
{2}{C}: Exile another target creature, then return it to the battlefield tap
- oracle_text advisory -- Emiel the Blessed: oracle_text diverges (similarity 0.48); scryfall="{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.

- oracle_text advisory -- Cloud of Faeries: oracle_text diverges (similarity 0.25); scryfall='Flying
When this creature enters, untap up to two lands.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Peregrine Drake: oracle_text diverges (similarity 0.22); scryfall='Flying
When this creature enters, untap up to five lands.'
- oracle_text advisory -- Wild Growth: oracle_text diverges (similarity 0.50); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Cryptolith Rite: oracle_text diverges (similarity 0.05); scryfall='Creatures you control have "{T}: Add one mana of any color."'
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land
Enchanted land has shroud. (It can't be the target of spells or abilities.)
Whenever enchanted
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.04); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.05); scryfall="This spell can't be countered.
{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)
{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)
Flash
Flying
{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.08); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).
{T}: Add {C}.
{T}, Pay {E}: Add one mana of any colo
- oracle_text advisory -- Maelstrom Wanderer: oracle_text diverges (similarity 0.34); scryfall='Creatures you control have haste.
Cascade, cascade (When you cast this spell, exile cards from the top of you
- oracle_text advisory -- Annoyed Altisaur: oracle_text diverges (similarity 0.50); scryfall='Reach, trample
Cascade (When you cast this spell, exile cards from the top of your library until you exile a
- oracle_text advisory -- Sakashima's Protege: oracle_text diverges (similarity 0.28); scryfall='Flash
Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Boarding Party: oracle_text diverges (similarity 0.62); scryfall='Haste
Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Breaching Dragonstorm: oracle_text diverges (similarity 0.26); scryfall="When this enchantment enters, exile cards from the top of your library until you exile a nonland card. You may
- oracle_text advisory -- Call Forth the Tempest: oracle_text diverges (similarity 0.40); scryfall="Cascade, cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland
- oracle_text advisory -- Creative Technique: oracle_text diverges (similarity 0.33); scryfall='Demonstrate (When you cast this spell, you may copy it. If you do, choose an opponent to also copy it.)
Shuff
- oracle_text advisory -- Dwarven Ruins: oracle_text diverges (similarity 0.09); scryfall='This land enters tapped.
{T}: Add {R}.
{T}, Sacrifice this land: Add {R}{R}.'
- oracle_text advisory -- Svyelunite Temple: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {U}.
{T}, Sacrifice this land: Add {U}{U}.'
- oracle_text advisory -- Hickory Woodlot: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped with two depletion counters on it.
{T}, Remove a depletion counter from this land: Ad
- oracle_text advisory -- Melira, Sylvok Outcast: oracle_text diverges (similarity 0.34); scryfall="You can't get poison counters.
Creatures you control can't have -1/-1 counters put on them.
Creatures your o
- oracle_text advisory -- Vizier of Remedies: oracle_text diverges (similarity 0.55); scryfall='If one or more -1/-1 counters would be put on a creature you control, that many -1/-1 counters minus one are p
- oracle_text advisory -- Kitchen Finks: oracle_text diverges (similarity 0.44); scryfall="When this creature enters, you gain 2 life.
Persist (When this creature dies, if it had no -1/-1 counters on
- oracle_text advisory -- Murderous Redcap: oracle_text diverges (similarity 0.51); scryfall="When this creature enters, it deals damage equal to its power to any target.
Persist (When this creature dies
- oracle_text advisory -- Carrion Feeder: oracle_text diverges (similarity 0.28); scryfall="This creature can't block.
Sacrifice a creature: Put a +1/+1 counter on this creature."
- oracle_text advisory -- Bloodthrone Vampire: oracle_text diverges (similarity 0.26); scryfall='Sacrifice a creature: This creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Recruiter of the Guard: oracle_text diverges (similarity 0.35); scryfall='When this creature enters, you may search your library for a creature card with toughness 2 or less, reveal it
- oracle_text advisory -- Ranger of Eos: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, you may search your library for up to two creature cards with mana value 1 or less,
- oracle_text advisory -- Severance Priest: oracle_text diverges (similarity 0.40); scryfall="Deathtouch
When this creature enters, target opponent reveals their hand. You may choose a nonland card from
- oracle_text advisory -- Birthing Pod: oracle_text diverges (similarity 0.25); scryfall="({G/P} can be paid with either {G} or 2 life.)
{1}{G/P}, {T}, Sacrifice a creature: Search your library for a
- oracle_text advisory -- Chord of Calling: oracle_text diverges (similarity 0.25); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1}
- oracle_text advisory -- Reveillark: oracle_text diverges (similarity 0.22); scryfall="Flying
When this creature leaves the battlefield, return up to two target creature cards with power 2 or less
- oracle_text advisory -- Felidar Guardian: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may exile another target permanent you control, then return that card to the ba
- oracle_text advisory -- Voice of Resurgence: oracle_text diverges (similarity 0.32); scryfall='Whenever an opponent casts a spell during your turn and when this creature dies, create a green and white Elem
- oracle_text advisory -- Scavenging Ooze: oracle_text diverges (similarity 0.23); scryfall='{G}: Exile target card from a graveyard. If it was a creature card, put a +1/+1 counter on this creature and y
- oracle_text advisory -- Ravenous Chupacabra: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, destroy target creature an opponent controls.'
- oracle_text advisory -- Reclamation Sage: oracle_text diverges (similarity 0.16); scryfall='When this creature enters, you may destroy target artifact or enchantment.'
- oracle_text advisory -- Celes, Rune Knight: oracle_text diverges (similarity 0.25); scryfall='When Celes enters, discard any number of cards, then draw that many cards plus one.
Whenever one or more othe
- oracle_text advisory -- Drifting Meadow: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {W}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Polluted Mire: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {B}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Smoldering Crater: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {R}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Blasted Landscape: oracle_text diverges (similarity 0.30); scryfall='{T}: Add {C}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fetid Pools: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {U} or {B}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Irrigated Farmland: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {W} or {U}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Canyon Slough: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {R}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Sheltered Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {G}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Scattered Groves: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {G} or {W}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Glittering Massif: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {W}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Festering Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {G}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Capital City: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{1}, {T}: Add one mana of any color.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Forsake the Worldly: oracle_text diverges (similarity 0.22); scryfall='Exile target artifact or enchantment.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fluctuator: oracle_text diverges (similarity 0.17); scryfall='Cycling abilities you activate cost {2} less to activate.'
- oracle_text advisory -- Drannith Stinger: oracle_text diverges (similarity 0.26); scryfall='Whenever you cycle another card, this creature deals 1 damage to each opponent.
Cycling {1} ({1}, Discard thi
- oracle_text advisory -- Hollow One: oracle_text diverges (similarity 0.20); scryfall="This spell costs {2} less to cast for each card you've cycled or discarded this turn.
Cycling {2} ({2}, Disca
- oracle_text advisory -- Unearth: oracle_text diverges (similarity 0.24); scryfall='Return target creature card with mana value 3 or less from your graveyard to the battlefield.
Cycling {2} ({2
- oracle_text advisory -- Scrying Sheets: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{1}{S}, {T}: Look at the top card of your library. If that card is snow, you may reveal it and
- oracle_text advisory -- Skred: oracle_text diverges (similarity 0.23); scryfall='Skred deals damage to target creature equal to the number of snow permanents you control.'
- oracle_text advisory -- Coldsteel Heart: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.
As this artifact enters, choose a color.
{T}: Add one mana of the chosen color.
- oracle_text advisory -- Abominable Treefolk: oracle_text diverges (similarity 0.35); scryfall="Trample
Abominable Treefolk's power and toughness are each equal to the number of snow permanents you control
- oracle_text advisory -- Arcum's Astrolabe: oracle_text diverges (similarity 0.25); scryfall='({S} can be paid with one mana from a snow source.)
When this artifact enters, draw a card.
{1}, {T}: Add on
- oracle_text advisory -- Frost Augur: oracle_text diverges (similarity 0.39); scryfall="{S}, {T}: Look at the top card of your library. If it's a snow card, you may reveal it and put it into your ha
- oracle_text advisory -- Ice-Fang Coatl: oracle_text diverges (similarity 0.21); scryfall='Flash
Flying
When this creature enters, draw a card.
This creature has deathtouch as long as you control at
- oracle_text advisory -- Jorn, God of Winter: oracle_text diverges (similarity 0.10); scryfall='Whenever Jorn attacks, untap each snow permanent you control.'
- oracle_text advisory -- Kaldring, the Rimestaff: oracle_text diverges (similarity 0.17); scryfall='{T}: You may play target snow permanent card from your graveyard this turn. If you do, it enters tapped.'
- oracle_text advisory -- Marit Lage's Slumber: oracle_text diverges (similarity 0.35); scryfall="Whenever Marit Lage's Slumber or another snow permanent you control enters, scry 1.
At the beginning of your
- oracle_text advisory -- Rimefeather Owl: oracle_text diverges (similarity 0.26); scryfall="Flying
Rimefeather Owl's power and toughness are each equal to the number of snow permanents on the battlefie
- oracle_text advisory -- Rimescale Dragon: oracle_text diverges (similarity 0.29); scryfall="Flying
{2}{S}: Tap target creature and put an ice counter on it. ({S} can be paid with one mana from a snow s
- oracle_text advisory -- Soul's Attendant: oracle_text diverges (similarity 0.12); scryfall='Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Auriok Champion: oracle_text diverges (similarity 0.11); scryfall='Protection from black and from red
Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Ocelot Pride: oracle_text diverges (similarity 0.12); scryfall="First strike, lifelink
Ascend (If you control ten or more permanents, you get the city's blessing for the res
- oracle_text advisory -- Serra Ascendant: oracle_text diverges (similarity 0.21); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)
As long as you have 30 or mo
- oracle_text advisory -- Ajani's Pridemate: oracle_text diverges (similarity 0.09); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Voice of the Blessed: oracle_text diverges (similarity 0.26); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.
As long as this creature has four or more +1/+1
- oracle_text advisory -- Daxos, Blessed by the Sun: oracle_text diverges (similarity 0.18); scryfall="Daxos's toughness is equal to your devotion to white. (Each {W} in the mana costs of permanents you control co
- oracle_text advisory -- Archangel of Thune: oracle_text diverges (similarity 0.24); scryfall='Flying
Lifelink (Damage dealt by this creature also causes you to gain that much life.)
Whenever you gain li
- oracle_text advisory -- Heliod, Sun-Crowned: oracle_text diverges (similarity 0.13); scryfall="Indestructible
As long as your devotion to white is less than five, Heliod isn't a creature.
Whenever you ga
- oracle_text advisory -- Ranger-Captain of Eos: oracle_text diverges (similarity 0.12); scryfall="When this creature enters, you may search your library for a creature card with mana value 1 or less, reveal i
- oracle_text advisory -- Ajani, Strength of the Pride: oracle_text diverges (similarity 0.27); scryfall='+1: You gain life equal to the number of creatures you control plus the number of planeswalkers you control.

- oracle_text advisory -- Apex Altisaur: oracle_text diverges (similarity 0.21); scryfall="When this creature enters, it fights up to one target creature you don't control.
Enrage — Whenever this crea
- oracle_text advisory -- World War Hulk: oracle_text diverges (similarity 0.17); scryfall='(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)
I — The next red or
- oracle_text advisory -- Ghalta, Stampede Tyrant: oracle_text diverges (similarity 0.17); scryfall='Trample
When Ghalta enters, put any number of creature cards from your hand onto the battlefield.'
- oracle_text advisory -- Lyra Dawnbringer: oracle_text diverges (similarity 0.11); scryfall='Flying
First strike (This creature deals combat damage before creatures without first strike.)
Lifelink (Dam
- oracle_text advisory -- Youthful Valkyrie: oracle_text diverges (similarity 0.08); scryfall='Flying
Whenever another Angel you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Bishop of Wings: oracle_text diverges (similarity 0.10); scryfall='Whenever an Angel you control enters, you gain 4 life.
Whenever an Angel you control dies, create a 1/1 white
- oracle_text advisory -- Righteous Valkyrie: oracle_text diverges (similarity 0.15); scryfall="Flying
Whenever another Angel or Cleric you control enters, you gain life equal to that creature's toughness.
- oracle_text advisory -- Seraph Sanctuary: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 1 life.
Whenever an Angel you control enters, you gain 1 life.
{T}: Add {C}.
- oracle_text advisory -- Resplendent Angel: oracle_text diverges (similarity 0.11); scryfall='Flying
At the beginning of each end step, if you gained 5 or more life this turn, create a 4/4 white Angel cr
- oracle_text advisory -- Legion Angel: oracle_text diverges (similarity 0.07); scryfall='Flying
When this creature enters, you may reveal a card you own named Legion Angel from outside the game and
- oracle_text advisory -- Serra the Benevolent: oracle_text diverges (similarity 0.16); scryfall='+2: Creatures you control with flying get +1/+1 until end of turn.
−3: Create a 4/4 white Angel creature toke
- oracle_text advisory -- Giada, Font of Hope: oracle_text diverges (similarity 0.11); scryfall='Flying, vigilance
Each other Angel you control enters with an additional +1/+1 counter on it for each Angel y
- oracle_text advisory -- Saproling Burst: oracle_text diverges (similarity 0.13); scryfall='Fading 7 (This enchantment enters with seven fade counters on it. At the beginning of your upkeep, remove a fa
- oracle_text advisory -- Shroofus Sproutsire: oracle_text diverges (similarity 0.06); scryfall='Trample
Whenever a Saproling you control deals combat damage to a player, create that many 1/1 green Saprolin
- oracle_text advisory -- Slimefoot, the Stowaway: oracle_text diverges (similarity 0.05); scryfall='Whenever a Saproling you control dies, Slimefoot deals 1 damage to each opponent and you gain 1 life.
{4}: Cr
- oracle_text advisory -- Vitaspore Thallid: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Deathspore Thallid: oracle_text diverges (similarity 0.10); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Sporecrown Thallid: oracle_text diverges (similarity 0.22); scryfall="Each other creature you control that's a Fungus or Saproling gets +1/+1."
- oracle_text advisory -- Tukatongue Thallid: oracle_text diverges (similarity 0.13); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Yavimaya Sapherd: oracle_text diverges (similarity 0.06); scryfall='When this creature enters, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Deathbloom Thallid: oracle_text diverges (similarity 0.05); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- The Mycotyrant: oracle_text diverges (similarity 0.18); scryfall='Trample
The Mycotyrant\'s power and toughness are each equal to the number of creatures you control that are
- oracle_text advisory -- Simic Growth Chamber: oracle_text diverges (similarity 0.21); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {G}{
- oracle_text advisory -- Psychotrope Thallid: oracle_text diverges (similarity 0.22); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Concordant Crossroads: oracle_text diverges (similarity 0.01); scryfall='All creatures have haste.'
- oracle_text advisory -- Brightcap Badger: oracle_text diverges (similarity 0.12); scryfall='Each Fungus and Saproling you control has "{T}: Add {G}."
At the beginning of your end step, create a 1/1 gre
- oracle_text advisory -- Fungus Frolic: oracle_text diverges (similarity 0.10); scryfall='Create two 1/1 green Saproling creature tokens. (Then exile this card. You may cast the creature later from ex
- oracle_text advisory -- Spore Swarm: oracle_text diverges (similarity 0.06); scryfall='Create three 1/1 green Saproling creature tokens.'
- oracle_text advisory -- Saproling Migration: oracle_text diverges (similarity 0.13); scryfall='Kicker {4} (You may pay an additional {4} as you cast this spell.)
Create two 1/1 green Saproling creature to
- oracle_text advisory -- Doubling Season: oracle_text diverges (similarity 0.28); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Parallel Lives: oracle_text diverges (similarity 0.13); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Primal Vigor: oracle_text diverges (similarity 0.15); scryfall='If one or more tokens would be created, twice that many of those tokens are created instead.
If one or more +
- oracle_text advisory -- Beastmaster Ascension: oracle_text diverges (similarity 0.17); scryfall='Whenever a creature you control attacks, you may put a quest counter on this enchantment.
As long as this enc
- oracle_text advisory -- Thallid: oracle_text diverges (similarity 0.17); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Thallid Shell-Dweller: oracle_text diverges (similarity 0.13); scryfall='Defender
At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters
- oracle_text advisory -- Sporesower Thallid: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on each Fungus you control.
Remove three spore counters
- oracle_text advisory -- Utopia Mycon: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Mycoloth: oracle_text diverges (similarity 0.14); scryfall='Devour 2 (As this creature enters, you may sacrifice any number of creatures. It enters with twice that many +
- oracle_text advisory -- Lightstall Inquisitor: oracle_text diverges (similarity 0.17); scryfall='Vigilance
When this creature enters, each opponent exiles a card from their hand and may play that card for a
- oracle_text advisory -- Lyra, Archangel of Dawn: oracle_text diverges (similarity 0.06); scryfall='Flying
Whenever you gain life, put a +1/+1 counter on each Angel you control.'
- oracle_text advisory -- Sunrise Sovereign: oracle_text diverges (similarity 0.10); scryfall='Other Giant creatures you control get +2/+2 and have trample.'
- oracle_text advisory -- Stinkdrinker Daredevil: oracle_text diverges (similarity 0.11); scryfall='Giant spells you cast cost {2} less to cast.'
- oracle_text advisory -- Giant Harbinger: oracle_text diverges (similarity 0.14); scryfall='When this creature enters, you may search your library for a Giant card, reveal it, then shuffle and put that
- oracle_text advisory -- Risen Reef: oracle_text diverges (similarity 0.15); scryfall="Whenever this creature or another Elemental you control enters, look at the top card of your library. If it's
- oracle_text advisory -- Flamekin Harbinger: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, you may search your library for an Elemental card, reveal it, then shuffle and put
- oracle_text advisory -- Foundation Breaker: oracle_text diverges (similarity 0.17); scryfall="When this creature enters, you may destroy target artifact or enchantment.
Evoke {1}{G} (You may cast this sp
- oracle_text advisory -- Fulminator Mage: oracle_text diverges (similarity 0.07); scryfall='Sacrifice this creature: Destroy target nonbasic land.'
- oracle_text advisory -- Pyroclasm: oracle_text diverges (similarity 0.05); scryfall='Pyroclasm deals 2 damage to each creature.'
- oracle_text advisory -- Hamletback Goliath: oracle_text diverges (similarity 0.09); scryfall="Whenever another creature enters, you may put X +1/+1 counters on this creature, where X is that creature's po
- oracle_text advisory -- Borderland Behemoth: oracle_text diverges (similarity 0.06); scryfall='Trample
This creature gets +4/+4 for each other Giant you control.'
- oracle_text advisory -- Inferno Titan: oracle_text diverges (similarity 0.13); scryfall='{R}: This creature gets +1/+0 until end of turn.
Whenever this creature enters or attacks, it deals 3 damage
- oracle_text advisory -- Surtland Flinger: oracle_text diverges (similarity 0.18); scryfall="Whenever this creature attacks, you may sacrifice another creature. When you do, this creature deals damage eq
- oracle_text advisory -- Tectonic Giant: oracle_text diverges (similarity 0.16); scryfall='Whenever this creature attacks or becomes the target of a spell an opponent controls, choose one —
• This cre
- oracle_text advisory -- Metallic Mimic: oracle_text diverges (similarity 0.18); scryfall='As this creature enters, choose a creature type.
This creature is the chosen type in addition to its other ty
- oracle_text advisory -- Smokebraider: oracle_text diverges (similarity 0.12); scryfall='{T}: Add two mana in any combination of colors. Spend this mana only to cast Elemental spells or activate abil
- oracle_text advisory -- Incandescent Soulstoke: oracle_text diverges (similarity 0.17); scryfall='Other Elemental creatures you control get +1/+1.
{1}{R}, {T}: You may put an Elemental creature card from you
- oracle_text advisory -- Fury: oracle_text diverges (similarity 0.13); scryfall='Double strike
When this creature enters, it deals 4 damage divided as you choose among any number of target c
- oracle_text advisory -- Adaptive Automaton: oracle_text diverges (similarity 0.23); scryfall='As this creature enters, choose a creature type.
This creature is the chosen type in addition to its other ty
- oracle_text advisory -- Corsair Captain: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, create a Treasure token. (It\'s an artifact with "{T}, Sacrifice this token: Add on
- oracle_text advisory -- Dire Fleet Captain: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature attacks, it gets +1/+1 until end of turn for each other attacking Pirate.'
- oracle_text advisory -- Goblin Tomb Raider: oracle_text diverges (similarity 0.09); scryfall='As long as you control an artifact, this creature gets +1/+0 and has haste.'
- oracle_text advisory -- Forerunner of the Coalition: oracle_text diverges (similarity 0.22); scryfall='When this creature enters, you may search your library for a Pirate card, reveal it, then shuffle and put that
- oracle_text advisory -- Staunch Crewmate: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, look at the top four cards of your library. You may reveal an artifact or Pirate ca
- oracle_text advisory -- Malcolm, the Eyes: oracle_text diverges (similarity 0.26); scryfall='Flying, haste
Whenever you cast your second spell each turn, investigate. (Create a Clue token. It\'s an arti
- oracle_text advisory -- Siren Stormtamer: oracle_text diverges (similarity 0.16); scryfall='Flying
{U}, Sacrifice this creature: Counter target spell or ability that targets you or a creature you contr
- oracle_text advisory -- Daring Buccaneer: oracle_text diverges (similarity 0.09); scryfall='As an additional cost to cast this spell, reveal a Pirate card from your hand or pay {2}.'
- oracle_text advisory -- Kitesail Larcenist: oracle_text diverges (similarity 0.17); scryfall='Flying, ward {1}
When this creature enters, for each player, choose up to one other target artifact or creatu
- oracle_text advisory -- Spirebluff Canal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {U} or {R}.'
- oracle_text advisory -- Blackcleave Cliffs: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {B} or {R}.'
- oracle_text advisory -- Battlefield Forge: oracle_text diverges (similarity 0.18); scryfall='{T}: Add {C}.
{T}: Add {R} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Karplusan Forest: oracle_text diverges (similarity 0.43); scryfall='{T}: Add {C}.
{T}: Add {R} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Tarnished Citadel: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{T}: Add one mana of any color. This land deals 3 damage to you.'
- oracle_text advisory -- Grand Coliseum: oracle_text diverges (similarity 0.53); scryfall='This land enters tapped.
{T}: Add {C}.
{T}: Add one mana of any color. This land deals 1 damage to you.'
- oracle_text advisory -- Ancient Tomb: oracle_text diverges (similarity 0.17); scryfall='{T}: Add {C}{C}. This land deals 2 damage to you.'
- oracle_text advisory -- Manabarbs: oracle_text diverges (similarity 0.12); scryfall='Whenever a player taps a land for mana, this enchantment deals 1 damage to that player.'
- oracle_text advisory -- Tamanoa: oracle_text diverges (similarity 0.13); scryfall='Whenever a noncreature source you control deals damage, you gain that much life.'
- oracle_text advisory -- Rhox Faithmender: oracle_text diverges (similarity 0.37); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)
If you would gain life, you
- oracle_text advisory -- Vito, Thorn of the Dusk Rose: oracle_text diverges (similarity 0.16); scryfall='Whenever you gain life, target opponent loses that much life.
{3}{B}{B}: Creatures you control gain lifelink
- oracle_text advisory -- Dina, Soul Steeper: oracle_text diverges (similarity 0.21); scryfall="Whenever you gain life, each opponent loses 1 life.
{1}, Sacrifice another creature: Dina gets +X/+0 until en
- oracle_text advisory -- Bilbo, Birthday Celebrant: oracle_text diverges (similarity 0.24); scryfall='If you would gain life, you gain that much life plus 1 instead.
{2}{W}{B}{G}, {T}, Exile Bilbo: Search your l
- oracle_text advisory -- Purity: oracle_text diverges (similarity 0.24); scryfall="Flying
If noncombat damage would be dealt to you, prevent that damage. You gain life equal to the damage prev
- oracle_text advisory -- Spellshock: oracle_text diverges (similarity 0.09); scryfall='Whenever a player casts a spell, this enchantment deals 2 damage to that player.'
- oracle_text advisory -- Pyrohemia: oracle_text diverges (similarity 0.11); scryfall='At the beginning of the end step, if no creatures are on the battlefield, sacrifice this enchantment.
{R}: Th
- oracle_text advisory -- Rolling Earthquake: oracle_text diverges (similarity 0.08); scryfall='Rolling Earthquake deals X damage to each creature without horsemanship and each player.'
- oracle_text advisory -- Beseech the Queen: oracle_text diverges (similarity 0.20); scryfall="({2/B} can be paid with any two mana or with {B}. This card's mana value is 6.)
Search your library for a car
- oracle_text advisory -- Green Sun's Zenith: oracle_text diverges (similarity 0.17); scryfall="Search your library for a green creature card with mana value X or less, put it onto the battlefield, then shu
- oracle_text advisory -- Dimir House Guard: oracle_text diverges (similarity 0.25); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)
Sacrifice a creatu
- oracle_text advisory -- Shriekmaw: oracle_text diverges (similarity 0.26); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)
When this creature
- oracle_text advisory -- Timeless Witness: oracle_text diverges (similarity 0.29); scryfall="When this creature enters, return target card from your graveyard to your hand.
Eternalize {5}{G}{G} ({5}{G}{
- oracle_text advisory -- Acidic Slime: oracle_text diverges (similarity 0.25); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)
When this creature enters
- oracle_text advisory -- Ageless Entity: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, put that many +1/+1 counters on this creature.'
- oracle_text advisory -- Selesnya Sanctuary: oracle_text diverges (similarity 0.06); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {G}{
- oracle_text advisory -- Blossoming Sands: oracle_text diverges (similarity 0.06); scryfall='This land enters tapped.
When this land enters, you gain 1 life.
{T}: Add {G} or {W}.'
- oracle_text advisory -- Verdant Sun's Avatar: oracle_text diverges (similarity 0.05); scryfall="Whenever this creature or another creature you control enters, you gain life equal to that creature's toughnes
- oracle_text advisory -- Feed the Clan: oracle_text diverges (similarity 0.04); scryfall='You gain 5 life.
Ferocious — You gain 10 life instead if you control a creature with power 4 or greater.'
- oracle_text advisory -- Nourish: oracle_text diverges (similarity 0.03); scryfall='You gain 6 life.'
- oracle_text advisory -- Eternal Witness: oracle_text diverges (similarity 0.13); scryfall='When this creature enters, you may return target card from your graveyard to your hand.'
- oracle_text advisory -- Elvish Harbinger: oracle_text diverges (similarity 0.20); scryfall='When this creature enters, you may search your library for an Elf card, reveal it, then shuffle and put that c
- oracle_text advisory -- Ezuri, Renegade Leader: oracle_text diverges (similarity 0.16); scryfall='{G}: Regenerate another target Elf.
{2}{G}{G}{G}: Elf creatures you control get +3/+3 and gain trample until
- oracle_text advisory -- Scholar of Athreos: oracle_text diverges (similarity 0.13); scryfall='{2}{B}: Each opponent loses 1 life. You gain life equal to the life lost this way.'
- oracle_text advisory -- Brindle Boar: oracle_text diverges (similarity 0.05); scryfall='Sacrifice this creature: You gain 4 life.'
- oracle_text advisory -- Weather the Storm: oracle_text diverges (similarity 0.14); scryfall='You gain 3 life.
Storm (When you cast this spell, copy it for each spell cast before it this turn.)'
- oracle_text advisory -- Utopia Sprawl: oracle_text diverges (similarity 0.21); scryfall='Enchant Forest
As this Aura enters, choose a color.
Whenever enchanted Forest is tapped for mana, its contro
- oracle_text advisory -- Crab Umbra: oracle_text diverges (similarity 0.16); scryfall='Enchant creature
{2}{U}: Untap enchanted creature.
Umbra armor (If enchanted creature would be destroyed, in
- oracle_text advisory -- Staff of Domination: oracle_text diverges (similarity 0.09); scryfall='{1}: Untap this artifact.
{2}, {T}: You gain 1 life.
{3}, {T}: Untap target creature.
{4}, {T}: Tap target
- oracle_text advisory -- Galvanic Alchemist: oracle_text diverges (similarity 0.18); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Seedcradle Witch: oracle_text diverges (similarity 0.08); scryfall='{2}{G}{W}: Target creature gets +3/+3 until end of turn. Untap that creature.'
- oracle_text advisory -- Trufflesnout: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, choose one —
• Put a +1/+1 counter on this creature.
• You gain 4 life.'
- oracle_text advisory -- Heroes' Reunion: oracle_text diverges (similarity 0.01); scryfall='Target player gains 7 life.'
- oracle_text advisory -- Rest for the Weary: oracle_text diverges (similarity 0.05); scryfall='Target player gains 4 life.
Landfall — If you had a land enter the battlefield under your control this turn,
- oracle_text advisory -- Accomplished Alchemist: oracle_text diverges (similarity 0.03); scryfall='{T}: Add one mana of any color.
{T}: Add X mana of any one color, where X is the amount of life you gained th
- oracle_text advisory -- Blighted Steppe: oracle_text diverges (similarity 0.04); scryfall='{T}: Add {C}.
{3}{W}, {T}, Sacrifice this land: You gain 2 life for each creature you control.'
- oracle_text advisory -- Wellwisher: oracle_text diverges (similarity 0.02); scryfall='{T}: You gain 1 life for each Elf on the battlefield.'
- oracle_text advisory -- Nykthos Paragon: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, you may put that many +1/+1 counters on each creature you control. Do this only once e
- oracle_text advisory -- Blossoming Bogbeast: oracle_text diverges (similarity 0.05); scryfall='Whenever this creature attacks, you gain 2 life. Then creatures you control gain trample and get +X/+X until e
- oracle_text advisory -- Genesis Wave: oracle_text diverges (similarity 0.07); scryfall="Reveal the top X cards of your library. You may put any number of permanent cards with mana value X or less fr
- oracle_text advisory -- Champion of the Parish: oracle_text diverges (similarity 0.11); scryfall='Whenever another Human you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Field Marshal: oracle_text diverges (similarity 0.12); scryfall='Other Soldier creatures get +1/+1 and have first strike. (They deal combat damage before creatures without fir
- oracle_text advisory -- Thalia, Guardian of Thraben: oracle_text diverges (similarity 0.06); scryfall='First strike
Noncreature spells cost {1} more to cast.'
- oracle_text advisory -- Thalia's Lieutenant: oracle_text diverges (similarity 0.12); scryfall='When this creature enters, put a +1/+1 counter on each other Human you control.
Whenever another Human you co
- oracle_text advisory -- Rick, Steadfast Leader: oracle_text diverges (similarity 0.16); scryfall="As Greymond, Avacyn's Stalwart enters, choose two abilities from among first strike, vigilance, and lifelink.\
- oracle_text advisory -- Esper Sentinel: oracle_text diverges (similarity 0.14); scryfall="Whenever an opponent casts their first noncreature spell each turn, draw a card unless that player pays {X}, w
- oracle_text advisory -- Coppercoat Vanguard: oracle_text diverges (similarity 0.17); scryfall='Each other Human you control gets +1/+0 and has ward {1}. (Whenever it becomes the target of a spell or abilit
- oracle_text advisory -- Recruitment Officer: oracle_text diverges (similarity 0.17); scryfall='{3}{W}: Look at the top four cards of your library. You may reveal a creature card with mana value 3 or less f
- oracle_text advisory -- Jirina, Dauntless General: oracle_text diverges (similarity 0.09); scryfall="When Jirina enters, exile target player's graveyard.
Sacrifice Jirina: Humans you control gain hexproof and i
- oracle_text advisory -- Harbin, Vanguard Aviator: oracle_text diverges (similarity 0.08); scryfall='Flying
Whenever you attack with five or more Soldiers, creatures you control get +1/+1 and gain flying until
- oracle_text advisory -- General Kudro of Drannith: oracle_text diverges (similarity 0.19); scryfall="Other Humans you control get +1/+1.
Whenever General Kudro or another Human you control enters, exile target
- oracle_text advisory -- Cathar Commando: oracle_text diverges (similarity 0.06); scryfall='Flash
{1}, Sacrifice this creature: Destroy target artifact or enchantment.'
- oracle_text advisory -- Brutal Cathar: oracle_text diverges (similarity 0.10); scryfall='Whenever this creature enters or transforms into Brutal Cathar, exile target creature an opponent controls unt
- oracle_text advisory -- Moonrage Brute: oracle_text diverges (similarity 0.22); scryfall='First strike
Ward—Pay 3 life.
Nightbound (If a player casts at least two spells during their own turn, it be
- oracle_text advisory -- King Darien XLVIII: oracle_text diverges (similarity 0.17); scryfall='Other creatures you control get +1/+1.
{3}{G}{W}: Put a +1/+1 counter on King Darien and create a 1/1 white S
- oracle_text advisory -- Fortified Beachhead: oracle_text diverges (similarity 0.26); scryfall='As this land enters, you may reveal a Soldier card from your hand. This land enters tapped unless you revealed
- oracle_text advisory -- Silent Clearing: oracle_text diverges (similarity 0.10); scryfall='{T}, Pay 1 life: Add {W} or {B}.
{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Waterlogged Grove: oracle_text diverges (similarity 0.11); scryfall='{T}, Pay 1 life: Add {G} or {U}.
{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Adelbert Steiner: oracle_text diverges (similarity 0.02); scryfall='Lifelink
Adelbert Steiner gets +1/+1 for each Equipment you control.'
- oracle_text advisory -- Adaptive Omnitool: oracle_text diverges (similarity 0.07); scryfall='Equipped creature gets +1/+1 for each artifact you control.
Whenever equipped creature attacks, look at the t
- oracle_text advisory -- Kíli the Resourceful: oracle_text diverges (similarity 0.07); scryfall='Storied (If you control three or more artifacts, legendaries, and/or Sagas, you have an enduring story for the
- oracle_text advisory -- Almost Perfect: oracle_text diverges (similarity 0.20); scryfall='Enchant creature
Enchanted creature has base power and toughness 9/10 and has indestructible.'
- oracle_text advisory -- Arcanum Wings: oracle_text diverges (similarity 0.15); scryfall='Enchant creature
Enchanted creature has flying.
Aura swap {2}{U} ({2}{U}: Exchange this Aura with an Aura ca
- oracle_text advisory -- Boseiju, Who Endures: oracle_text diverges (similarity 0.38); scryfall='{T}: Add {G}.
Channel — {1}{G}, Discard this card: Destroy target artifact, enchantment, or nonbasic land an
- oracle_text advisory -- Botanical Sanctum: oracle_text diverges (similarity 0.29); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {G} or {U}.'
- oracle_text advisory -- Bruna, Light of Alabaster: oracle_text diverges (similarity 0.24); scryfall='Flying, vigilance
Whenever Bruna attacks or blocks, you may attach to it any number of Auras on the battlefie
- oracle_text advisory -- Colossification: oracle_text diverges (similarity 0.14); scryfall='Enchant creature
When this Aura enters, tap enchanted creature.
Enchanted creature gets +20/+20.'
- oracle_text advisory -- Eldrazi Conscription: oracle_text diverges (similarity 0.39); scryfall='Enchant creature
Enchanted creature gets +10/+10 and has trample and annihilator 2. (Whenever it attacks, def
- oracle_text advisory -- Glittering Wish: oracle_text diverges (similarity 0.06); scryfall='You may reveal a multicolored card you own from outside the game and put it into your hand. Exile Glittering W
- oracle_text advisory -- Indrik Umbra: oracle_text diverges (similarity 0.38); scryfall='Enchant creature
Enchanted creature gets +4/+4 and has first strike, and all creatures able to block it do so
- oracle_text advisory -- Mother of Runes: oracle_text diverges (similarity 0.13); scryfall='{T}: Target creature you control gains protection from the color of your choice until end of turn.'
- oracle_text advisory -- Mythic Proportions: oracle_text diverges (similarity 0.40); scryfall='Enchant creature
Enchanted creature gets +8/+8 and has trample.'
- oracle_text advisory -- Open the Armory: oracle_text diverges (similarity 0.21); scryfall='Search your library for an Aura or Equipment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Prodigious Growth: oracle_text diverges (similarity 0.26); scryfall='Enchant creature
Enchanted creature gets +7/+7 and has trample.'
- oracle_text advisory -- Seaside Citadel: oracle_text diverges (similarity 0.15); scryfall='This land enters tapped.
{T}: Add {G}, {W}, or {U}.'
- oracle_text advisory -- Skycloud Expanse: oracle_text diverges (similarity 0.06); scryfall='{1}, {T}: Add {W}{U}.'
- oracle_text advisory -- Somberwald Sage: oracle_text diverges (similarity 0.12); scryfall='{T}: Add three mana of any one color. Spend this mana only to cast creature spells.'
- oracle_text advisory -- Unflinching Courage: oracle_text diverges (similarity 0.43); scryfall='Enchant creature
Enchanted creature gets +2/+2 and has trample and lifelink. (Damage dealt by the creature al
- oracle_text advisory -- Linvala, Shield of Sea Gate: oracle_text diverges (similarity 0.31); scryfall="Flying
At the beginning of combat on your turn, if you have a full party, choose target nonland permanent an
- oracle_text advisory -- Troyan, Gutsy Explorer: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}{U}. Spend this mana only to cast spells with mana value 5 or greater or spells with {X} in their
- oracle_text advisory -- Detention Sphere: oracle_text diverges (similarity 0.25); scryfall="When this enchantment enters, you may exile target nonland permanent not named Detention Sphere and all other
- oracle_text advisory -- Steel of the Godhead: oracle_text diverges (similarity 0.31); scryfall="Enchant creature
As long as enchanted creature is white, it gets +1/+1 and has lifelink. (Damage dealt by the
- oracle_text advisory -- Auroral Procession: oracle_text diverges (similarity 0.07); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Reborn Hope: oracle_text diverges (similarity 0.10); scryfall='Return target multicolored card from your graveyard to your hand.'
- oracle_text advisory -- Rampaging Baloths: oracle_text diverges (similarity 0.14); scryfall='Trample
Landfall — Whenever a land you control enters, create a 4/4 green Beast creature token.'
- oracle_text advisory -- Mossborn Hydra: oracle_text diverges (similarity 0.13); scryfall="Trample (This creature can deal excess combat damage to the player or planeswalker it's attacking.)
This crea
- oracle_text advisory -- Scythecat Cub: oracle_text diverges (similarity 0.16); scryfall='Trample
Landfall — Whenever a land you control enters, put a +1/+1 counter on target creature you control. If
- oracle_text advisory -- Scute Swarm: oracle_text diverges (similarity 0.15); scryfall="Landfall — Whenever a land you control enters, create a 1/1 green Insect creature token. If you control six or
- oracle_text advisory -- Moraug, Fury of Akoum: oracle_text diverges (similarity 0.17); scryfall="Each creature you control gets +1/+0 for each time it has attacked this turn.
Landfall — Whenever a land you
- oracle_text advisory -- Avenger of Zendikar: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, create a 0/1 green Plant creature token for each land you control.
Landfall — When
- oracle_text advisory -- Omnath, Locus of Rage: oracle_text diverges (similarity 0.20); scryfall='Landfall — Whenever a land you control enters, create a 5/5 red and green Elemental creature token.
Whenever
- oracle_text advisory -- Phylath, World Sculptor: oracle_text diverges (similarity 0.19); scryfall='When Phylath enters, create a 0/1 green Plant creature token for each basic land you control.
Landfall — When
- oracle_text advisory -- Courser of Kruphix: oracle_text diverges (similarity 0.17); scryfall='Play with the top card of your library revealed.
You may play lands from the top of your library.
Landfall —
- oracle_text advisory -- Khalni Heart Expedition: oracle_text diverges (similarity 0.19); scryfall='Landfall — Whenever a land you control enters, you may put a quest counter on this enchantment.
Remove three
- oracle_text advisory -- Akoum Hellhound: oracle_text diverges (similarity 0.15); scryfall='Landfall — Whenever a land you control enters, this creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Brushfire Elemental: oracle_text diverges (similarity 0.26); scryfall="Haste
This creature can't be blocked by creatures with power 2 or less.
Landfall — Whenever a land you contr
- oracle_text advisory -- Rampant Growth: oracle_text diverges (similarity 0.16); scryfall='Search your library for a basic land card, put that card onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Search for Tomorrow: oracle_text diverges (similarity 0.26); scryfall='Search your library for a basic land card, put it onto the battlefield, then shuffle.
Suspend 2—{G} (Rather t
- oracle_text advisory -- Cultivate: oracle_text diverges (similarity 0.24); scryfall='Search your library for up to two basic land cards, reveal those cards, put one onto the battlefield tapped an
- oracle_text advisory -- Evolving Wilds: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then
- oracle_text advisory -- Terramorphic Expanse: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then
- oracle_text advisory -- Myriad Landscape: oracle_text diverges (similarity 0.25); scryfall='This land enters tapped.
{T}: Add {C}.
{2}, {T}, Sacrifice this land: Search your library for up to two basi
- oracle_text advisory -- Primeval Titan: oracle_text diverges (similarity 0.16); scryfall='Trample
Whenever this creature enters or attacks, you may search your library for up to two land cards, put t
- oracle_text advisory -- Radiant Fountain: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 2 life.
{T}: Add {C}.'
- oracle_text advisory -- Haunted Fengraf: oracle_text diverges (similarity 0.12); scryfall='{T}: Add {C}.
{3}, {T}, Sacrifice this land: Return a creature card at random from your graveyard to your han
- oracle_text advisory -- Temple of Abandon: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Deceit: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Enlightenment: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malady: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malice: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Mystery: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Plenty: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Triumph: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Zhalfirin Void: oracle_text diverges (similarity 0.74); scryfall='When this land enters, scry 1. (Look at the top card of your library. You may put that card on the bottom.)
{
- oracle_text advisory -- Crystal Grotto: oracle_text diverges (similarity 0.39); scryfall='When this land enters, scry 1.
{T}: Add {C}.
{1}, {T}: Add one mana of any color.'
- oracle_text advisory -- Rumble Arena: oracle_text diverges (similarity 0.44); scryfall='Vigilance
When this land enters, scry 1. (Look at the top card of your library. You may put it on the bottom.
- oracle_text advisory -- The Grey Havens: oracle_text diverges (similarity 0.24); scryfall='When The Grey Havens enters, scry 1.
{T}: Add {C}.
{T}: Add one mana of any color among legendary creature c
- oracle_text advisory -- Galadriel of Lothlórien: oracle_text diverges (similarity 0.24); scryfall='Whenever the Ring tempts you, if you chose a creature other than Galadriel as your Ring-bearer, scry 3.
Whene
- oracle_text advisory -- Clifftop Retreat: oracle_text diverges (similarity 0.37); scryfall='This land enters tapped unless you control a Mountain or a Plains.
{T}: Add {R} or {W}.'
- oracle_text advisory -- Furycalm Snarl: oracle_text diverges (similarity 0.31); scryfall="As this land enters, you may reveal a Mountain or Plains card from your hand. If you don't, this land enters t
- oracle_text advisory -- Steelshaper's Gift: oracle_text diverges (similarity 0.18); scryfall='Search your library for an Equipment card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Swiftblade Vindicator: oracle_text diverges (similarity 0.27); scryfall="Double strike (This creature deals both first-strike and regular combat damage.)
Vigilance (Attacking doesn't
- oracle_text advisory -- Lizard Blades: oracle_text diverges (similarity 0.11); scryfall="Double strike
Equipped creature has double strike.
Reconfigure {2} ({2}: Attach to target creature you contr
- oracle_text advisory -- Magnetic Theft: oracle_text diverges (similarity 0.04); scryfall="Attach target Equipment to target creature. (Control of the Equipment doesn't change.)"
- oracle_text advisory -- Forge Anew: oracle_text diverges (similarity 0.09); scryfall='When this enchantment enters, return target Equipment card from your graveyard to the battlefield.
During you
- oracle_text advisory -- Raubahn, Bull of Ala Mhigo: oracle_text diverges (similarity 0.07); scryfall="Ward—Pay life equal to Raubahn's power.
Whenever Raubahn attacks, attach up to one target Equipment you contr
- oracle_text advisory -- Speaker of the Heavens: oracle_text diverges (similarity 0.16); scryfall="Vigilance, lifelink (Attacking doesn't cause this creature to tap. Damage dealt by this creature also causes y
- oracle_text advisory -- Path to Exile: oracle_text diverges (similarity 0.12); scryfall='Exile target creature. Its controller may search their library for a basic land card, put that card onto the b
- oracle_text advisory -- Sulfur Falls: oracle_text diverges (similarity 0.27); scryfall='This land enters tapped unless you control an Island or a Mountain.
{T}: Add {U} or {R}.'
- oracle_text advisory -- Gitaxian Probe: oracle_text diverges (similarity 0.16); scryfall="({U/P} can be paid with either {U} or 2 life.)
Look at target player's hand.
Draw a card."
- oracle_text advisory -- Generator Servant: oracle_text diverges (similarity 0.15); scryfall='{T}, Sacrifice this creature: Add {C}{C}. If any of that mana is spent on a creature spell, it gains haste unt
- oracle_text advisory -- Brainstorm: oracle_text diverges (similarity 0.07); scryfall='Draw three cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Legion Lieutenant: oracle_text diverges (similarity 0.11); scryfall='Other Vampires you control get +1/+1.'
- oracle_text advisory -- Vampire of the Dire Moon: oracle_text diverges (similarity 0.37); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)
Lifelink (Damage dealt by
- oracle_text advisory -- Falkenrath Pit Fighter: oracle_text diverges (similarity 0.11); scryfall='{1}{R}, Discard a card, Sacrifice a Vampire: Draw two cards. Activate only if an opponent lost life this turn.
- oracle_text advisory -- Stromkirk Condemned: oracle_text diverges (similarity 0.15); scryfall='Discard a card: Vampires you control get +1/+1 until end of turn. Activate only once each turn.'
- oracle_text advisory -- Voldaren Estate: oracle_text diverges (similarity 0.35); scryfall='{T}: Add {C}.
{T}, Pay 1 life: Add one mana of any color. Spend this mana only to cast a Vampire spell.
{5},
- oracle_text advisory -- Bloodghast: oracle_text diverges (similarity 0.19); scryfall="This creature can't block.
This creature has haste as long as an opponent has 10 or less life.
Landfall — Wh
- oracle_text advisory -- Stromkirk Captain: oracle_text diverges (similarity 0.11); scryfall='First strike
Other Vampire creatures you control get +1/+1 and have first strike.'
- oracle_text advisory -- Drana, Liberator of Malakir: oracle_text diverges (similarity 0.14); scryfall='Flying, first strike
Whenever Drana deals combat damage to a player, put a +1/+1 counter on each attacking cr
- oracle_text advisory -- Mavren Fein, Dusk Apostle: oracle_text diverges (similarity 0.14); scryfall='Whenever one or more nontoken Vampires you control attack, create a 1/1 white Vampire creature token with life
- oracle_text advisory -- Vampire Socialite: oracle_text diverges (similarity 0.26); scryfall="Menace (This creature can't be blocked except by two or more creatures.)
When this creature enters, if an opp
- oracle_text advisory -- Falkenrath Gorger: oracle_text diverges (similarity 0.27); scryfall="Each Vampire creature card you own that isn't on the battlefield has madness. The madness cost is equal to its
- oracle_text advisory -- Markov Baron: oracle_text diverges (similarity 0.26); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1}
- oracle_text advisory -- Asylum Visitor: oracle_text diverges (similarity 0.33); scryfall="At the beginning of each player's upkeep, if that player has no cards in hand, you draw a card and you lose 1
- oracle_text advisory -- Captivating Vampire: oracle_text diverges (similarity 0.12); scryfall='Other Vampire creatures you control get +1/+1.
Tap five untapped Vampires you control: Gain control of target
- oracle_text advisory -- Bloodline Keeper: oracle_text diverges (similarity 0.12); scryfall='Flying
{T}: Create a 2/2 black Vampire creature token with flying.
{B}: Transform this creature. Activate on
- oracle_text advisory -- Lord of Lineage: oracle_text diverges (similarity 0.17); scryfall='Flying
Other Vampire creatures you control get +2/+2.
{T}: Create a 2/2 black Vampire creature token with fl
- oracle_text advisory -- Kessig Wolf Run: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {C}.
{X}{R}{G}, {T}: Target creature gets +X/+0 and gains trample until end of turn.'
- oracle_text advisory -- Harrow: oracle_text diverges (similarity 0.19); scryfall='As an additional cost to cast this spell, sacrifice a land.
Search your library for up to two basic land card
- oracle_text advisory -- Explosive Vegetation: oracle_text diverges (similarity 0.29); scryfall='Search your library for up to two basic land cards, put them onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Nature's Lore: oracle_text diverges (similarity 0.19); scryfall='Search your library for a Forest card, put that card onto the battlefield, then shuffle.'
- oracle_text advisory -- Skyshroud Claim: oracle_text diverges (similarity 0.26); scryfall='Search your library for up to two Forest cards, put them onto the battlefield, then shuffle.'
- oracle_text advisory -- Martyr of Sands: oracle_text diverges (similarity 0.06); scryfall='{1}, Reveal X white cards from your hand, Sacrifice this creature: You gain three times X life.'
- oracle_text advisory -- Walking Ballista: oracle_text diverges (similarity 0.06); scryfall='This creature enters with X +1/+1 counters on it.
{4}: Put a +1/+1 counter on this creature.
Remove a +1/+1
- oracle_text advisory -- Abiding Grace: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your end step, choose one —
• You gain 1 life.
• Return target creature card with mana v
- oracle_text advisory -- Emeria, the Sky Ruin: oracle_text diverges (similarity 0.19); scryfall='This land enters tapped.
At the beginning of your upkeep, if you control seven or more Plains, you may return
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- claude_sweep recorded at commit 4a04d8c14 (HEAD 1e0b6438214e); re-run if play changed since (play_invariants + smoke digests track play live).

<!-- verify_deck:end -->
