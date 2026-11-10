# Extra Turns ladder cost diagnosis (2026-11-10)

The paired 8,000-game Phase E comparison found leafless escalation 0.2214 scored
turns faster than the old heuristic ladder at 1.373 times its work units. The
learned leaf was 0.2100 turns faster at 1.960 times the units. The emulated
ladder was 0.0656 turns slower at 1.139 times the units. These are work-unit
ratios, not wall-clock ratios; the result files are in `logs/vlq_extra_turns/`.

## Where the work moved

The table below uses one further paired 400-game block (seed 7,605,000) at
depth 5, budget 20 ms, on the same Release binary. Its direction agrees with
the eight-seed result; it is a diagnostic sample, not the adoption test. Logs
and telemetry are in `logs/et_ladder_diag/`.

| Mode | Mean scored turn | Work units | Core time | Interior search nodes | Rollout calls | Full-search preparation + enumeration units |
|---|---:|---:|---:|---:|---:|---:|
| Old heuristic | 5.0400 | 7.231m | 787s | 1.180m | 0.998m | 2.065m |
| Leafless escalation | 4.8125 | 10.154m | 1,230s | 5.214m | 0.203m | 8.989m |
| Leafless single FIT | 4.8150 | 10.392m | 1,241s | 5.231m | 0.212m | 9.192m |
| Learned value leaf | 4.8250 | 13.563m | 1,511s | 6.801m | 0.200m | 12.432m |
| Emulated ladder | 5.1200 | 7.904m | 814s | 3.397m | 0.479m | 5.428m |

The old heuristic pays for many rollouts and commits its few completed
heuristic-ladder searches shallow: 177 of 248 at depth 1, only 11 at depth 4
or 5. Leafless escalation removes most rollout cost, so its cheap probe and
subsequent heuristic escalation traverse much deeper trees: 407 of 932
completed heuristic-ladder decisions reached depth 4 or 5. It saves about
1.52m rollout-step units and 1.48m greedy-fallback units, but adds about 3.98m
full-search preparation and 2.94m enumeration units. The extra tree work is
why it costs more overall and finds earlier wins. These meter buckets overlap
in places; they are explanatory counters, not an additive partition.
Core time on this paired block rose by 56% for leafless escalation and 92%
for the learned leaf. These are sums of per-game elapsed time, not the batch's
wall time under parallel execution. Work-unit ratios were 40% and 88%; the
difference reflects the different mix of operations being charged.

The single FIT mode traverses almost the same tree as leafless escalation and
adds 0.238m units here without an appreciable quality gain. The learned model
visits 30% more interior nodes than leafless escalation and triggers 1,229
hybrid decisions versus 1,099. Its preparation and enumeration charges grow
by 3.44m units. Different model scores and resulting play lines change the
tree and future states; this sample identifies where the extra cost occurs but
does not isolate one model prediction as the cause.

## Why the emulated ladder loses

The emulated ladder tries cheap value-leaf warm-ups and predicts the final
depth using a frozen rollout cost per leaf. It then has to replay that depth on
the heuristic leaf to commit a heuristic line. The committed cost table has
only two depths, `[457.6, 293.97]` units per leaf; the second number is reused
for depths 3 through 5. In this diagnostic block, measured paired costs were
roughly 175 at depth 1, 78 at depth 2, and 1,250 at depth 3, with only 23, 2,
and 3 paired samples respectively. The frozen table is a poor predictor for
this deck's changing search trees.

The cost reconstruction also assumes that value and heuristic leaves visit
the same tree at a fixed depth. The paired-pass telemetry contradicts that
assumption here: leaf counts differed in 3 of 24 depth-1 pairs, all 3 depth-2
pairs, and 2 of 4 depth-3 pairs. Heuristic/value leaf-count ratios were 1.05,
0.20, and 0.10 respectively. Branch-and-bound pruning changes with leaf
scores, so a single rollout-cost-per-value-leaf multiplier cannot accurately
predict every heuristic pass. The measured actual/reconstructed cost ratio was
0.38 at depth 1, 0.27 at depth 2, and 3.74 at depth 3 (small paired samples).

On the 400-game block, 1,981 emulated-ladder calls made 2,137 value passes and
3,102 heuristic passes. They recorded 1,650 fallback attempts, 3,156 overrun
attempts, and 1,779 calls with no completed heuristic line. Those are call and
attempt counters, not numbers of distinct games. The code keeps a completed
value line when every heuristic replay overruns. That defeats the mode's
intended guarantee that its final line is the heuristic's. In one exact replay
(seed 7,605,098, game index 98), the heuristic won on turn 5 using 17,436
units; emulation won on turn 8 using 115,767 units. Its 14 emulated calls made
21 overrun attempts and completed no heuristic line. The first play already
diverged: Island for the heuristic, Myriad Landscape for emulation.

Three paired diagnostic variants (same 400 games; `logs/et_emul_cause/`) probe
which part matters. These are mechanism tests, not tuned candidates:

| Emulation variant | Mean scored turn | Units | Overrun attempts | Calls with no heuristic line |
|---|---:|---:|---:|---:|
| Original frozen costs | 5.1200 | 7.904m | 3,156 | 1,779 |
| Replace costs with this block's measured values | 5.0350 | 8.544m | 3,499 | 1,634 |
| Force an exact heuristic depth-1 pass | 4.9975 | 7.572m | 1,955 | 1,794 |
| Halve the warm-up prediction margin | 5.1200 | 7.917m | 3,158 | 1,780 |

Better cost estimates recover some quality but increase work; the small margin
change barely moves decisions. An exact first pass helps more, yet still
does not reach leafless escalation's 4.8125 turn result. The large number of
calls without a completed heuristic line persists. The failure is therefore
both prediction error and an expensive replay structure under a tight budget,
not simply an unfortunate constant. The shipped leafless escalation remains
the better measured choice; these variants were not adopted.

## Full regression rebaseline

After the diagnosis, all three full modes were run and accepted on the shipped
policy. Smoke ran 183 jobs and 445 passing scenarios; regression ran 291 jobs,
445 passing scenarios, 787 passing strict viewer references, and two strictly
beaten suboptimal references. Overnight ran 588 jobs and 445 passing scenarios.
The overnight audit found 64 faster and 6 slower searched games on overlapping
samples, for a net gain of 60 scored turns. Five slowdowns recovered at four
times the tier budget; the remaining Modern RW Burn 2HG game recovered at
unlimited budget. No searched win became unwon.

The four Extra Turns overnight depth-5 rows now cover 425 games each. Their
1,700-game mean is about 4.74 scored turns. The 300 games previously covered
by each row match their old win turns and play digests exactly, as do all 50
previously covered games in the expanded 100-game 2HG row. A separate oracle
rerun matched all 588 overnight job fingerprints and found zero nonconvergence.
Its 57 prediction flags comprise 54 one-turn misses and three severe flags
from two distinct Treasure Hunt seeds; both severe games are unchanged from
their prior ground-truth outcomes and show a preexisting committed-line fidelity
issue. The overnight acceptance header records this follow-up and retains the
older Angels and Bruna follow-ups.

## Follow-up emulation screen (2026-11-10)

We tested existing emulated-ladder controls on paired seeds using the current
Release binary. Manifests and results are in `logs/emul_fleet_2026_11_10/`.
The 31-deck screen forced emulation on each regression deck that has a learned
model, at each deck's play depth and budget, with 40 games per arm. Making
depth one an exact heuristic pass changed no scored outcomes on that small
screen. It changed play digests on Hinata, Modern RW Burn and Prevent Damage;
the total work-unit ratio was 0.999. This is a coverage screen, not evidence
of per-deck neutrality at scale.

We then paired 150 games per arm on Landfall, Melira Pod, Modern RW Burn and
Extra Turns. Exact depth one improved Extra Turns from 5.0400 to 4.9333 scored
turns, but cost 1.21 times the units. It was quality-neutral on the other three;
Modern RW Burn used 0.88 times the units, Landfall 1.004, and Melira 1.000.
Forcing the first two depths onto the heuristic had no general benefit. A
0.25 prediction margin made Landfall 0.0133 turns slower. On a second 200-game
block, exact depth one moved Extra Turns 4.8950 to 4.8800 and reduced units
to 0.86 times baseline; the direction of its cost effect therefore varies by
seed. The other four 200-game arms, including KittyEquipment, showed no
scored-turn improvement. Direct commitment of verified value warm-up wins
changed no scored outcome on these five decks; across the pooled 20-job run,
only 189 of 11,081 emulated decisions used that path.

Finally, 100-game paired blocks tried leafless warm-ups. That helped Extra
Turns from 5.1800 to 5.0600 (5.0800 when depth one was exact), but the
shipped leafless escalation scored 4.8800 on the same games. Leafless warm-ups
made Landfall 0.0200 turns slower and cost 1.17 times its baseline units.
Melira, Modern RW Burn and KittyEquipment had unchanged scored averages.

No tested control improved quality and cost across decks. The old emulation
prediction assumes a common value/heuristic search tree, contradicted by the
same-depth measurements above. Exact depth one protects the baseline line in
some states but cannot make later reconstructed gates exact. Keep the shipped
per-deck policies. A future redesign should measure or bound the *heuristic
tree* cost directly and retain a completed heuristic line before spending on
warm-ups; these experiments do not justify a global switch.
