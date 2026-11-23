# Worldfire mulligan generation setting

Measured 2026-11-22 before the first exhaustive profile. The deck has 21
equivalence buckets: Scalding Tarn and Flooded Strand merge; the other card
names remain distinct. `value_play.expected_buckets` records that discovery
result. With the deck's copy limits, the size-seven table has 562,383 bucket
compositions per play/draw side.

The default 48-hand, R24 setting screen chose depth 1 / 1 ms, but its reference
noise ceiling was 0.9202. All candidates saturated the normalized fidelity
score, so that screen could not distinguish them. I repeated the screen with
96 hands and R64, then measured intermediate budgets against the same play
reference (depth 5 / 20 ms). Cost is search units per rollout; fidelity is
Spearman rank correlation divided by the reference's noise ceiling.

| Seed | Setting | Fidelity | Units | Relative cost |
| --- | --- | ---: | ---: | ---: |
| 20261123 | d5 / 20 ms reference | 1.0000 | 26,772 | 1.000 |
| 20261123 | d2 / 7 ms | 0.9876 | 15,546 | 0.581 |
| 20261123 | **d2 / 8 ms** | **0.9911** | **16,219** | **0.606** |
| 20261123 | d2 / 9 ms | 0.9951 | 17,322 | 0.647 |
| 20261123 | d2 / 10 ms | 0.9988 | 18,191 | 0.679 |
| 20261124 | d5 / 20 ms reference | 1.0000 | 29,678 | 1.000 |
| 20261124 | **d2 / 8 ms** | **1.0000** | **17,824** | **0.601** |

Depth 2 / 8 ms is the cheapest tested setting that met the 0.99 fidelity gate
on both independent samples. Its measured search work is about 40% lower than
play settings. This is a generation label setting; the deck's play settings
remain unchanged.

The score caches and full logs are in the isolated worktree's ignored
`build/scratch/worldfire-mullperf/` and `logs/worldfire-mullperf-*.log`.

The suite PGO training manifest initially read generation settings only from
`.value.json`. Worldfire has no value sidecar, so it trained at the default
depth 5 / 20 ms despite the new contract. `pgo_train_manifest.py` now reads
the profile first, then lets a value sidecar override it. The regenerated
manifest reports `gen_worldfire` at depth 2 / 8 ms.

On the same 1,000 uniformly sampled size-seven rollouts, Release used 497 ms
of thread CPU per rollout and PGO+LTO used 346 ms (1.44x throughput). Both
reported 18,680 search units per rollout and digest `5b57e1a7c0074163`.
The PGO sample's slowest 1% accounted for 16.1% of time; its maximum was
15.4 s. At 32 fully occupied cores, one rollout per side across all 562,383
size-seven compositions alone projects to about 3.4 hours. A full profile
adds the floor's second rollout, refinement, and smaller-hand tables, so this
sample does not estimate its total time.

A 15-second low-frequency `perf` sample during the PGO scout found no single
dominant engine function. The largest exclusive samples were `CollectActions`
(4.3%), `BuildSimKeyBase` (3.7%), plan enumeration (3.0%), and `memmove`
(2.6%); `WorldfireKeepRank::Of` was 1.9%. The cost is spread across search
work, so a small local rewrite is unlikely to change the generation bill.

## Full-run checkpoint, 2026-11-23

The `fast` R30 run resumed 38,097 scout cell-sides under fresh PGO+LTO and ran
for 5 h 54 min before it was stopped to investigate the multi-day estimate.
Its size-seven floor had completed 1,465,811 rollouts, still with 0% worker
idle time and no sub-table batches started. The R2 floor covers 1,124,766
play/draw cell-sides, hence about 2.25 million rollouts; the observed rate
ranged from 50 to 104 rollouts/s as hand composition changed. The journal is
durable at `Worldfire.keepmodel.exhaustive.raw.json.journal` (801,871 records,
40 MB) and the driver can resume it with the same `fast` recipe.

The long tail is not the main cost. Of the 1.47 million rollouts, 519 exceeded
30 s, 39 exceeded 60 s, and three exceeded 120 s (maximum 200 s). Their
combined time was 5.96 core-hours, about 3% of the run's CPU time. The slowest
hands vary: two Rift Bolts plus two Memory Lapses and Mystical Tutor; a
Worldfire/Seething Song/Personal Tutor/Lotus Bloom hand; or four Rift Bolts
with Mystical Tutor. None points to one recurring runaway. The PGO perf samples
likewise spread cost across action collection, state-key construction, plan
application, and search.

Comparison runs give an order-of-magnitude total estimate, not a reliable ETA:
EDF's `fast` R30 run took 61.5 h for 773,890 size-seven cell-sides at d1/b1;
Worldfire has 1,124,766 at d2/b8. The earlier 3–4 day extrapolation multiplied
the observed floor rate by a large uniform refinement allowance. That is too
pessimistic as a decision estimate: a comparable adaptive run averaged 9.06
rollouts per size-seven cell-side and 17.98 per smaller-hand cell-side. Applying
those averages to Worldfire's 1,124,766 and 435,038 cell-sides gives about 18.0
million rollouts, or roughly 63 hours at 80 rollouts/s. This is a scenario,
not an ETA: Worldfire's actual adaptive freeze distribution and the cost of
smaller-hand rollouts are still unmeasured. The R30 cap does not mean every
cell receives 30 rollouts. The low-R alternative still owes the same R2 floor.
The discovery cache's closest non-fetchland pair is Personal Tutor/Mystical
Tutor at 0.1475 mean win turns per probe, far beyond the 0.01 automatic merge
threshold; the next closest is Mistrise Village/Island at 0.1825. The two
manual merges below are a mulligan decision judgment, not measured card
equivalence.

### Candidate bucket merges

The 400 fixed discovery probes compare the turn reached after substituting one
card into the same six-card hand. The next possible merges are well outside the
0.01-turn threshold. Counts below account for the deck's copy limits, rather
than using the uncapped combinations formula.

| Pair | Probe distance | Probes with different outcome | Probes crossing turn-4 boundary | Size-seven compositions after merge | Reduction |
| --- | ---: | ---: | ---: | ---: | ---: |
| Personal Tutor / Mystical Tutor | 0.1475 turns | 55 / 400 | 29 / 400 | 438,710 | 22.0% |
| Mistrise Village / Island | 0.1825 turns | 68 / 400 | 45 / 400 | 460,213 | 18.2% |
| Steam Vents / Island | 0.1875 turns | 68 / 400 | 44 / 400 | 433,607 | 22.9% |
| Rift Bolt / Keldon Halberdier | 0.2175 turns | 78 / 400 | 40 / 400 | 398,443 | 29.2% |
| Saprazzan Skerry / Sandstone Needle | 0.2375 turns | 81 / 400 | 55 / 400 | 398,443 | 29.2% |
| Ponder / Preordain | 0.2850 turns | 106 / 400 | 63 / 400 | 432,560 | 23.1% |

Ponder and Preordain have a much smaller *signed* gap than their probe distance:
Ponder wins earlier in 50 probes, Preordain in 56, and they tie in 294; the
signed mean (Ponder minus Preordain) is +0.025 turns. Discovery uses a
clairvoyant rollout, including searched Ponder reorder/shuffle choices, so the
0.285 absolute gap may overstate their distinction at mulligan time. Its exact
contribution has not been isolated. The user accepted merging them for mulligan
decisions and rebuilding the table. Each rollout draws actual member copies from a shuffled
deck, so a merged Ponder/Preordain bucket samples their real copy mix. The
representative card is only used for the static-policy comparison report.

The closest pair is only superficially similar: Mystical Tutor can find Seething
Song and can be cast in upkeep on the go-off turn; Personal Tutor can do neither.
Personal Tutor is a sorcery that finds sorceries, while Mystical Tutor is an
instant that finds instants or sorceries. Mistrise Village enters tapped without
a Mountain or Forest, while Island does not; Steam Vents also provides red.
Rift Bolt and Keldon Halberdier are different post-Worldfire win conditions,
and the depletion lands produce different colours. Ponder looks at three cards
and can shuffle; Preordain scries two before drawing. Those differences can affect
which hand to keep and which card to bottom. These merges are a human ruling
about mulligan decisions, not an equivalence claim from the discovery metric.
They make the current 21-bucket journal unusable for the new table, so their
saved future work must outweigh the already banked rollouts.
The turn-4 boundary is a diagnostic, not the generator's mulligan threshold;
crossings show that even the closest pair can straddle a plausible keep line.
Using the comparable run's adaptive averages, a Tutor-only merge would have
reduced the whole run from about 18.0 million to 14.25 million rollouts. The
combined Tutor and cantrip merge is more consequential, as measured below.

### Worldfire-only PGO trial, 2026-11-23

The R1 scout was stopped after it completed the size-seven first pass and
started size-six cells. Its journal had 1,151,706 sampled cell-sides on resume.
The suite-trained PGO binary was saved, then `./build.sh pgo decks/Worldfire`
trained a replacement. On identical 5,000 uniformly sampled size-seven
rollouts, both builds gave digest `d4f07f231c3458e3` and 19,769.52 search
units per rollout. The suite build used 383.51 ms of thread CPU per rollout;
the deck build used 381.86 ms, a 0.4% reduction. A 1,000-rollout repeat showed
the same direction but only 0.8%. This is not a material gain, so the suite
build was restored. The `fast` R30 driver resumed the 21-bucket journal at
10:05 UTC; all banked floor cells were admitted, including the R40-to-R30
carry. No source or rollout setting changed.

### Two-bucket reduction, 2026-11-23

The user considered grouping Personal Tutor/Mystical Tutor and
Ponder/Preordain acceptable for mulligan decisions. The two merges together reduce K from
21 to 19, size-seven compositions per side from 562,383 to 335,641 (40.3%),
and smaller-hand cell-sides from 435,038 to 278,474 (36.0%). Applying the
historical adaptive averages above projects about 11.1 million rollouts for
K19, versus 18.0 million for K21. The stopped K21 journal holds 2.51 million
rollouts. A restart at K19 therefore projects roughly 4.4 million fewer
remaining rollouts than finishing K21; this is a planning estimate, not an ETA.

The runtime table decides bucket counts. Its bottomer previously removed the
first physical card when a merged bucket had excess copies, which could bottom
Mystical Tutor while keeping Personal Tutor. A within-bucket preference now
uses the deck's explicit Mystical-over-Personal rule, then marginal card scores
for pairs without a strict rule. When marginal scores tie, a duplicate is
bottomed before a singleton; thus a lone Preordain can remain alongside Ponder.
Ponder/Preordain remain separate physical
cards during each rollout, sampled from their actual 4:1 deck mix.
The profile's explicit order opts Worldfire into physical-member selection;
existing decks without an order retain their previous bottoming path.
