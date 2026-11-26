# Full overnight checkpoint, 2026-11-26

The 793-job, 766,896-game batch measured source commit
`e430cc58529ef75cf507a46b25967436903d1246` with a PGO binary. The source
tree hash in the binary metadata is `401750ac746c7a1ae12ce57557c5d90e3098b1b4`.
The batch took 39,151 seconds (10 h 52 m 31 s) on this 12-thread host and used
about 128 worker hours. PGO rebuilding before the batch added roughly 72 minutes;
the end-to-end run including comparison and audit was about 12 hours. The earlier
`<8 h` description was not a reliable ceiling for this host and matrix.

All 793 case results and per-game logs were complete. Compared with the previous
overnight baseline, searched games had 3,136 faster and 1,151 slower win turns,
with a pooled loss-penalized delta of -0.0087 turns per game. Depth-0 games had
13,940 faster and 7,768 slower win turns, with a pooled delta of -0.0219. There
were 24 searched old wins that became unwon: 14 Creature Burn mulligan changes;
two Antilife rows for one physical seed with a changed land/fetch line and
divergent draws; two Treasure Hunt rows for one physical seed affected by
opponent land life gain; two Fungus rows for one physical seed recovering at a
higher budget; PD and Giants opponent land life gain; and one Goblins 2HG and
one Shadowborn budget-churn case. The earlier accepted debt and attribution
remain in `handoff-mana-overnight-2026-11-14.md` and
`overnight-accept-2026-11-20.md`.

The largest per-deck costs in the completed batch were EDF 7.09 worker hours,
Beasts 7.08, Cyclone 5.91, Merfolk Turns 5.48, Maze's End 5.27, Hinata 4.78,
and Snow 4.78. The full deck-level CPU table is
`overnight-deck-runtime-2026-11-26.csv` (CPU totals are slightly lower than
the per-job elapsed worker-hour sums above). EDF's four depth-0 rows alone used
about 2.3 worker hours;
depth-0 `units=0` currently records weight 1 in `test/job_cost.tsv`, so the
manifest's weighted progress estimate understated that tail. The four main
seed blocks each accounted for about a quarter of the manifest's estimated
searched work. By measured CPU, seed 4004 used 36.0 worker hours, 5005 used
27.4, 6006 used 30.3, and 7007 used 29.2; the additional depth-0 seeds and
unbudgeted backstops used about 2.4 combined. A single block therefore costs
roughly 2.3–3.0 game hours at 12-way occupancy, before build and audit overhead.
Roughly 6% of listed games share physical seeds with another same-setting row.
These timings describe this host; compare the full tier on another machine
before changing the periodic policy. The user expects to run future full
checkpoints elsewhere if this host remains this slow.

The batch logged 66 memory heartbeats, with RSS from 0.9 to 4.1 GB (median
2.2 GB) against about 10.9 GB of RAM, and its workers were usually fully
occupied. The jobs accumulated about 125.2 CPU hours against 128.2 elapsed
worker hours (97.7% CPU occupancy). A sampled engine process used about 11.7
CPU cores and 2.5 GB RSS.
This makes sustained swapping an unlikely explanation for most of the batch
time, but swap-in/out counters were not recorded throughout the run, so it
cannot be ruled out. A later 12-way Release compilation did encounter memory
pressure and swap; that was a separate validation build, not this PGO game
batch.

The shared branch advanced after the measured source, including Worldfire's
mulligan and value-leaf artifacts and the later land-1126 play changes. This
checkpoint deliberately records the measured source hash and does not claim to
cover those later changes. Their smoke and regression baselines were accepted
separately on the shared branch; the next periodic overnight checkpoint will
measure them.
