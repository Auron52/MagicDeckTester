# Beam CPU on Creature Giving (agent abb1d924, 2026-11-25; saved by the parent)

Branch wip/beam-cpu 5cd426699. Scratch build/scratch/beam-cpu/. CG d5 b30, seeds 34,600,000+,
200 paired games per arm, 2-thread lane jobs; units are the reliable metric (box shared).

## 1. Where the no-beam cost goes (A = beam W3, N = MTG_ESC_BEAM_NONE)
- Totals: A 2,049,994 units, N 3,248,160 (x1.58). All of the excess is in the escalation
  (177K vs 1,378K); outside it the arms are equal (1.873M vs 1.870M).
- It is leaf rollouts: rollout_step +444K, greedy_fallback +425K, la_cand +309K. Interior nodes
  equal; the probe-tree re-walk is ~1% of units.
- 43% of N's escalation units (594K) are aborted passes: 21/67 escalations aborted their first
  pass, 10 were then discarded with nothing gained (297K).
- 53/67 escalations commit d2 (T=2) and beam_leafdepth is 2, so every node of a d2 pass is beamed,
  the root included (the root plays only its top-3 value-ranked plans).
- Flat CPU profile (max 4.3%, BuildSimKeyBase).

## 2. Why a lower budget barely helps
- The escalation is fully charged (budget units == gamework units). It runs on a fresh
  0.5 x budget_ms (13.5K units at b30) or the probe's leftover if larger.
- A heuristic pass is complete-or-abort: FSLineWin stops only at the 2 x Limit overrun ceiling,
  never at Exhausted(). A smaller budget turns completions into aborts, not smaller trees.
- The frozen escalation_r (21, beam-era) underpredicts without the beam: units per probe leaf
  45.9 with the beam, 114.4 without.

Completeness: the escalation runs only when the probe is short of budget. CG d5 b0 (4 games)
and b300 (10 games): 0 escalations, A == N identical. b100: 1 escalation. So on CG the beam
makes nothing unfindable at large/unlimited budget.

## 3. Cuts
Exact (byte-identical): none of consequence (removing charged units re-spends budget).

| Arm | What | Units/200 | vs A | taken/discarded |
|---|---|---|---|---|
| A | beam W3 (today) | 2.050M | 1.00 | 69/0 |
| N | no beam | 3.248M | 1.58 | 57/10 |
| NK | N + MTG_LEAF_KILL_ONLY_NOM2 | 2.914M | 1.42 | 63/4 |
| NR | N + esc_deck_r=114 | 2.591M | 1.26 | 46/21 |
| NKR | NK + R=114 | 2.466M | 1.20 | 47/20 |
| S | MTG_ESC_BEAM_STAGED (new, default OFF) | 2.673M | 1.30 | 67/0 |
| SK | S + KILL_ONLY_NOM2 | 2.576M | 1.26 | 67/0 |
| AK | A + KILL_ONLY_NOM2 | 2.004M | 0.98 | 68/0 |

- NK: existing sound identity (0 verify mismatches), opted in only by Cyclone's provider; CG never
  searches a main 2. Exact per leaf at b0 but re-spends budget (digests move 3/8 jobs).
- NR: per-job R re-price; inert at b0; 24 targets drop to d1 and are then not taken.
- S: staged admission: at a beamed node the top W plans always expand, the rest only while the
  escalation budget is not Exhausted(). Complete at unlimited budget, lossy under a budget.
- Proof: with staged off the new binary reproduces wip/beam-gate's digests/units on all 8 jobs;
  units 4163/4163.

## 4. A/B generator (logs/beam-cpu/mk_ab.py)
Writes build/scratch/beam-cpu/ab/{ab.json,rows.json} for logs/beam-gate/ab_report.py.
Default AL/CG/DS play settings, 2000 std + 1000 2HG per arm; arms A,N,NK,NR(CG R=114),NKR,S,SK;
seeds 37.1M+. Needs this branch's binary (S arms). esk_pair.py pairs per-game/per-escalation lines.

## Open
Opt CG into the no-main-2 kill-only identity (USER question, same as the Cyclone fleet default).
Which arm, if any, replaces the beam is the USER's call after the A/B.
