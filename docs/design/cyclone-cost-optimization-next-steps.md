# Cyclone cost optimization after integration

The implementation and reviewed validation landed on `phase-1-2-deck-analyzer`
at `08d816495ab054db887fa16b007e0f18f171de8d`. The measurements under
"Admission measurement" are the historical starting point. The October 31
follow-up below was validated on an isolated branch before landing on the
shared `phase-1-2-deck-analyzer` branch.

The baseline source is `89b146d10406063213d9abe41e1cfdf1a616c021`; canonical PGO
SHA256 is `20de8d519c6c7c8494925d5170ff91ed06f5b4fcde4fd1bc04d1325113bb5e80`.
The integration evidence is committed at `logs/cyclone-completion/verified-89b/`.
All original branches/worktrees and reference games remain preserved.

## Admission measurement

The sole uncontended pool completed all855 games and86 chunks at32 workers.
At that time the gate used the worst searched-cell mean per-game wall duration
divided by same-run Hinata, not batch makespan or measured thread CPU time. Hinata's
worst cell measured2,040.213ms/game.

| Configuration | Worst ms/game | Ratio | Required reduction to3x |
|---|---:|---:|---:|
| Current defaults |27,923.650|13.687x|78.1%|
| PB plans |7,176.500|3.518x|14.7%|
| PB + KEEP_AMEND + KEEP_MYRIAD |7,243.300|3.550x|15.5%|

All three FAIL. Coarse descriptive intervals do not change the point-estimate
bar. No override is authorized. Cyclone stays outside all three regression tiers
until it qualifies; neither generator should be launched. Policy choices remain
user-owned, including adopting or rejecting the candidates. The small paired
cost cohort is not an adoption-quality experiment; retain the separately labelled
V5 quality history and pending-policy document.

## Actual profile

A single32-worker PGO queue replayed the first16 measured live/PB identities per
arm, seeds2002–2017 and GI0–15, with unchanged original job settings. All32 wins,
digests and deterministic unit counts reproduced the quiet pool exactly.
`perf record -e cycles:u -F49 --call-graph dwarf,8192` recorded18,086 samples with
zero lost samples. This is pooled attribution, not separate arm shares or a new
timing gate. The binary has symbols/CFI but no source debug information.

| Measured function/family | Self cycles | Observed local inclusive cycles |
|---|---:|---:|
| BuildSimKeyBase |6.94%| — |
| ApplyPlanDirect body |4.11%|27.22%|
| ApplyPlanDirect body and lambdas |10.41%|63.52%|
| Putback resolution |2.42%|13.55%|
| Payment helper | — |14.34%|
| ComputePlanTraits |2.43%| — |
| Human top-disposition builder |0.607%|0.893%|
| CacheHitCharge |0.04%|0.04%|

Inclusive values overlap and must not be added. About98.6% of weighted cycles (99.53% of raw stacks)
eventually reach an unknown ancestor; local chains and self instruction samples
are useful, but they are not a complete root/leaf cost partition. Scope::Done has
one self sample; named solve/leaf telemetry atomic instruction neighborhoods
account for about0.09% of sampled cycles, with skid/attribution limits. These
measurements do not support bookkeeping or human labels as the main solution.

Compact reports, guarded replay recipe and results are committed in
`logs/cyclone-completion/verified-89b/profile-evidence.tar.gz` with a separate
`profile-index.json`. Raw profile and working analysis are retained under
`build/scratch/cyclone-finish/landing905-review/profile89-first16-live-pb/`.
The raw perf file is152,729,556 bytes and stays outside Git. Source reviews and
small reports are preserved separately; do not call incomplete ancestors a
measured complete pipeline or infer a speedup from operation counts alone.

## October 31 measured policy candidate

The cost metric now pools seeds within each searched depth and budget, then
takes the largest per-game average (USER 2026-10-31). A same-run 32-worker PGO
batch measured 615 games on the baseline-source binary with explicit policy
flags. Both Cyclone arms used the two-plan put-back prune, the two-plan Ponder
prune and no-second-main kill-only parity; only the candidate also pruned the
Curator scry pin. The 3x yardstick is Hinata's d5/b30 grouped mean.

| Group | Baseline ms/game | With seed prune ms/game | Hinata ms/game |
|---|---:|---:|---:|
| d3/b10 | 3,684.150 | 3,543.175 | 1,171.960 |
| d5/b20 Cyclone; d5/b30 Hinata | 5,645.625 | 5,294.625 | 1,801.623 |

The gated ratio is 3.134x without the seed prune and **2.939x with it**.
The Release check was a separate screen; the PGO batch above is the relevant
compiler setting. The source-bound build and tier gate still have to confirm
the ratio after changing defaults.

Paired play quality on the Phase C seed block and a fresh d3 extension was
d3/b10 **-4 total turn steps over 800 games** and d5/b20 **-6 over 200**,
seed prune minus the other Cyclone flags. The first 400 d3 games were +3, the
next 400 were -7. Eight known-library
blind salts on every slower game found no repeated bad scry choice; seven of
eight residual cases tied at 4x or 16x budget. The one remaining salt was one
turn slower at 16x, but its game over 40 paired hidden-library salts favored
the prune by 0.275 turns/game (SE 0.183; 11 better, 6 worse, 23 tied). This is
evidence against a consistent known-information loss, not a proof that no
individual future can go worse. Working results are in
`build/scratch/cyclone-seed-{pgo-gate,quality,quality-extension,blind-classify,gi317-blind40}-20261031/`.

The official source-bound PGO cost gate, after making these policies default,
measured Cyclone at 4,111.030 ms/game (worst d5/b20) against same-run Hinata at
1,822.547 ms/game: **2.26x, PASS**. The full smoke tier passed 157 existing
cases and admitted four Cyclone keys; the full regression tier passed 253
existing cases and admitted five Cyclone keys. Its strict reference check found
zero play drift, board divergence, or contract failures. The regression tier
had no searched-depth slower games or play changes against committed ground
truth. These runs used a fresh PGO binary from this branch's source tree.

The Cyclone-only overnight tier then passed all 14 new cases (four depth-0,
four d3/b10, four d5/b20, and two screened budget-0 backstops). The 300-game
d5 blocks averaged 5.0767, 5.1100, 5.1567, and 5.0967 turns; the 400-game
d3 blocks averaged 5.0550, 5.1175, 5.1575, and 5.1125. All new fingerprints
were accepted; `check_gt_logs.py` reported 937 consistent, zero stale or
missing. The full 516-job overnight suite was stopped after its early existing
deck groups to start Cyclone's long artifact build sooner. Full smoke, full
regression, the strict 709-reference replay, and the Cyclone overnight tier
completed on this source.

The one depth-8/budget-0 blind replay that lost the depth-3/budget-160 win was
seed 111317, game 317, blind salt 2, with the same forced opening hand and
bottoms (Toxic Deluge and Ponder). Both depths cast Brainstorm on turn 1 and
put back Curator and Emrakul. Depth 3's budget commits its first-pass forecast
of a sampled turn-5 win, holding Street Wraith; that line wins on real turn 7.
Depth 8 finds a turn-5 win in the *sampled unknown library* by cycling Street
Wraith during Brainstorm's continuation. It commits that equal-turn sampled
line, but real draws then leave the game unwon through turn 8. At the decisive
Brainstorm breakpoint, the depth-3 first pass rates the cycle branch at turn 8
and the hold branch at turn 5. The deeper pass rates the cycle branch at turn 5;
its earlier-win cutoff then seeks only a turn-4-or-earlier alternative and does
not retain the hold line on a tie. Both arms of the Curator seed-prune test are
unwon at depth 8/budget 0 on this salt, so that prune did not introduce the
depth-8 miss. The eight-salt depth comparison below holds that prune OFF in
both arms.
Showing the real
library to either depth yields a turn-4 win; disabling the blind machinery's
library shuffle while retaining its executor also yields a turn-4 win. Thus the
miss is caused by a single sampled unknown order, not a missing legal play or
an executor divergence. On eight blind salts of that same fixed hand, depth 8
scores 61 total turn steps versus depth 3's 62: salt 2 is +2 for depth 8,
salt 3 is +1, salt 7 is -3, salt 6 is -1, and the other four tie. This does not prove a
general quality gain; it does show the reported game is not a consistent
depth-8 failure. Replays and the paired batch are in
`build/scratch/cyclone-seed-gi317-diagnostic-20261031/` and
`build/scratch/cyclone-gi317-depth-blind-salts-20261031/`.

## Next work

Prioritize exact reuse of repeated plan application/prefix and metadata work,
and avoid redundant key construction where the state is demonstrably unchanged.
A prefix snapshot must retain all physical/RNG/known-top state, provider context,
TLS side effects, meter/budget charges, cut/partial state, latent dependencies and
plan ordering. A snapshot-only shortcut is insufficient. Preserve the no-greedy
search rule and every existing plan/order.

Two narrower mechanisms are worth bounded prototypes, not assumed gate fixes:

- PutBackPickAt performs class/provider ordering and builds its full candidate
  sequence before the existing scoring-cache lookup. Earlier reuse requires the
  entire class/condemnation/hand-order/physical-copy key, with negative-key tests.
- Library front insertion shifts the live suffix even after draws leave dead
  prefix slots. An explicit put-on-top operation could reuse such a slot, while
  preserving known-position, bottom/index carry, card alias and iterator contracts.
  The observed vector-reallocation path accounts for about1% of pooled cycles.

Retain billing-context bit64. General free/billed cache reuse is not proven safe:
free derivations lack counterfactual charge traces and budgeted derivations may
cut or perform nested work. General charged-cost mode2 is not a safe substitute.
A certified complete enumeration-only memo might replay exact rounded charges
within the same context, but this profile does not make that the first priority.

Use an isolated branch from the landed baseline. Build only through `./build.sh`,
use all32 CPUs normally, and run a new source-bound experiment for each actual
code change. First prove exact plans/order/units and physical behavior against
this frozen baseline; cover cold/hot/pressure and interrupted/cut cases where
relevant. Use normal regression review for changed games, never promote GT from
an aggregate alone. Re-time on a quiet host with same-run Hinata after correctness
checks close. Do not relabel older measurements as fresh-source evidence.

Do not restart the stopped5,986-game optional batch or the13k historical pool as
an automatic next step. Do not add per-thread cache quotas or weaken the3x bar.
After a measured configuration qualifies and any user-owned policy decision is
settled: add all three suite tiers, run/accept the new rows, push everything,
measure feasibility at real generation settings, then use the standard solitary
mulligan-fast and value-leaf drivers in that order.
