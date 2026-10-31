# Cyclone cost optimization after integration

The implementation and reviewed validation landed on `phase-1-2-deck-analyzer`
at `08d816495ab054db887fa16b007e0f18f171de8d`. Later checkpoint commits change
only documentation. This is a performance follow-up; no optimization below has
been implemented, no policy has been adopted, and no artifact generation started.

The baseline source is `89b146d10406063213d9abe41e1cfdf1a616c021`; canonical PGO
SHA256 is `20de8d519c6c7c8494925d5170ff91ed06f5b4fcde4fd1bc04d1325113bb5e80`.
The integration evidence is committed at `logs/cyclone-completion/verified-89b/`.
All original branches/worktrees and reference games remain preserved.

## Admission measurement

The sole uncontended pool completed all855 games and86 chunks at32 workers.
The existing gate uses worst searched-cell mean per-game wall duration divided
by same-run Hinata, not batch makespan or measured thread CPU time. Hinata's
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
