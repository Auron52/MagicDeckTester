# Slow generation jobs -- tracker

USER 2026-10-27: *"Let's keep track of the slow jobs. It might be necessary to do some optimizations."*

One section per deck/stage whose generation spends hours in a tail of degenerate rollouts. Each entry records
the evidence (where the numbers came from), the hands, and a mechanism *hypothesis*. A hypothesis is not a
diagnosis until a single-rollout repro has been profiled (census / odometer, see
`keepgen-cost-concentration.md`). Add a new run's numbers to the deck's section rather than a new section.

Machine context: this box has 23.5 GB RAM, and every engine run sets `MTG_RSS_CAP_GB=20`. The planned +16 GB
upgrade would raise that headroom; the cap must be re-derived after it is fitted. A run that reaches the cap
aborts itself, so the journal and its rolling backups bound the loss.

## Melira Pod -- mulligan (keep table), recipe `fast` (R=30), d3 b3, K=23

Run: artifact worker `artifact-queue-d7d6a5`, frozen at `60f525f17`. Generation binary: PGO, src tree
`aba41d81c`. The run started 2026-10-25 19:16 UTC, was paused by the USER on 2026-10-26 22:22 UTC (27 h in), and
resumed 2026-10-27 12:25 UTC.

Evidence: `decks/Melira Pod/Melira Pod.keepmodel.exhaustive.raw.json.slow.log` (rollouts over 30 s) and
`logs/Melira_Pod_mullgen/gen.log` (monitor lines and the top-3 list), both in the worker's frozen worktree.
Snapshot at 2026-10-27 19:11 UTC:

- **874 SLOW-ROLLOUTs (over 30 s), 50.8 core-hours between them.**
  - Duration: median 48 s, p90 143 s, p99 1,411 s.
  - Hand size: 579 at size 7, 246 at size 6, 45 at size 5, 4 at size 4.
- **The worst single rollouts, each one core for hours:**

  | time | size, draw/play, r | seed | hand |
  |---|---|---|---|
  | 36,891 s (10.2 h) | size 6, draw, r=20 | 18054083110755917170 | Kitchen Finks, Forest, Felidar Guardian, Caves of Koilos, Carrion Feeder ×2 |
  | 30,253 s (8.4 h) | size 7, play, r=1 | 4354685594354933543 | Orzhov Basilica, Murderous Redcap, Caves of Koilos ×2, Carrion Feeder ×3 |
  | 22,027 s (6.1 h) | size 7, play, r=1 | 4354685568366855579 | Kitchen Finks ×3, Caves of Koilos ×2, Carrion Feeder, Bloodthrone Vampire |
  | 7,642 s (2.1 h) | size 6, draw, r=1 | 4354686363641241394 | Orzhov Basilica, Forest, Carrion Feeder ×4 |
  | 1,778 s | size 7, play, r=1 | 4354685676544180110 | Reclamation Sage, Birds of Paradise, Ignoble Hierarch ×2, Chord of Calling, Carrion Feeder ×2 |
  | 1,583 s / 1,410 s / 1,166 s / 1,161 s | size 7 | — | Carrion Feeder ×2–4 with Chupacabra / Ranger / Finks |

- **87% of the slow-rollout time is in hands holding Carrion Feeder.**

  | card | slow rollouts | core-hours |
  |---|---|---|
  | Carrion Feeder | 557 | 44.4 |
  | Caves of Koilos | 325 | 36.3 |
  | Kitchen Finks | 635 | 32.4 |
  | Forest | 195 | 18.2 |
  | Orzhov Basilica | 313 | 17.9 |
  | Bloodthrone Vampire | 263 | 14.4 |
  | Felidar Guardian | 107 | 12.5 |

- **Memory and durability.**
  - Generator RSS: 9.6 GB → 10.9 GB → 16.5 GB by 18:47 UTC, against the 20 GB cap. The previous session saw
    16 GB in the same phase.
  - At 19:11 UTC the monitor warned that the journal had not been written for 4,444 s while 28,098 rollouts
    had been fed in the last 300 s. That work is unbanked: a kill or a cap abort discards it.
  - Throughput: roll7 sat at 0/s for about 2 h (16,202–22,503 s into the session, the tail of a sub-wave),
    then recovered to 93–111/s.
  - `keepgen-producer-barrier-and-durability.md` gives the discriminator between a producer barrier and cost
    concentration. Here the journal mtime is frozen (barrier-like) while SLOW-ROLLOUT size<7 entries keep
    appearing (concentration-like), so both may be present.

**Mechanism hypothesis (UNVERIFIED).** The deck runs two free sacrifice outlets: Carrion Feeder costs {B} to
cast and sacrifices a creature for a +1/+1 counter; Bloodthrone Vampire sacrifices for +2/+2. It also runs two
persist creatures, Kitchen Finks and Murderous Redcap, and the blink creature Felidar Guardian. Every
sacrifice is an activation with no mana cost, so it can sit at any point in the turn. Each persist return
re-triggers an ETB, and Redcap's ETB carries a target choice. With 2–4 Feeders the outlets can also eat each
other. The searched turn's branching would grow combinatorially in the number of sac-able bodies, which fits
both the hand list (Feeder ×2–4) and the RSS growth.

**DIAGNOSED 2026-10-27 (replay of the 62.9 s rollout, `Kitchen Finks x2; Caves of Koilos x2; Carrion Feeder x3`,
size 7 draw r=0, via `MTG_KEEP_REPLAY` on the frozen binary -- play digest b2c25a26e00f1eba reproduced).** The
hypothesis above was half right. The fodder sacrifices were already gated (`MeliraPodProvider::FodderSacUseful`,
the USER's 2026-09-05 rule), so the generic sac was not the problem. The cost is the COMBO-ACTIVE case: on the
board `Carrion Feeder x3, Kitchen Finks x2, Celes, Rune Knight` (Celes = a live loop-closer) the persist-loop
block in `CollectActions` emitted K=1 / growth-K variants for EVERY (outlet, persist body) pair, 12 loop actions,
crossed by the breakpoint enumeration into 13,824 plans per node. Two pre-combat decisions (t4, t5 of the
rollout) each ran into the 1M-unit decision work ceiling (`MTG_DECISION_WORK_X=1000` x b3), ~25 s each; an
ordinary rollout costs ~0.25 s (40-rollout sample: 25 ms - 1 s).

**Fix: `MTG_POD_LOOP_FOLD` (TurnSolver.cpp, default ON).** One loop variant per DISTINCT (outlet, body) state:
twin outlets / twin bodies (same card, tap state, counters, everything `PermIsPlainForFold` reads) give states
that differ only by permuting identical permanents, so the twin pair adds no reachable outcome. Oldest copy kept.
Single replay on the new binary: **69.1 s -> 5.2 s (13x), units 2.17M -> 229k, same win turn (6).**

**Replay A/B, same binary, `MTG_POD_LOOP_FOLD=0` vs `1`** (`MTG_KEEP_REPLAY_LIST`: every 8th slow-log line at or under
150 s, plus 1,000 ordinary sampled rollouts; generation suspended so both arms had the box):

| population | n | wall off -> on | avg win turn off -> on | on better / worse |
|---|---|---|---|---|
| slow tail | 98 | 14,212 s -> 2,146 s (**6.6x**) | 6.29 -> **5.98** | **30 / 0** |
| ordinary | 1,000 | 377 s -> 327 s (1.15x) | 5.764 -> 5.764 | 1 / 1 |

The freed budget buys play on the tail (30 rollouts win a turn earlier, none later), and the ordinary rollout is
untouched. Generation was restarted on the fixed commit with `MTG_KEEP_RETAIN_FOREIGN=b2c25a26e00f1eba` (USER
2026-10-27: "it would be nicer if we could retain what we have done") -- the rollouts banked under the old engine
stay in the journal.

**Originally listed checks** (1 is answered above; 2-3 remain):
1. Single-rollout repro of the 7,642 s hand (Basilica, Forest, Feeder ×4: the smallest hand, nearly pure
   Feeder), run under the per-decision census. Does the time go to sacrifice-activation enumeration, and
   are interchangeable outlets/bodies being pooled the way the sac-outlet pooling collapse already does
   elsewhere?
2. Whether the free-sacrifice menu is reachable from positions where no payoff exists. On a goldfish board
   with no death trigger, persist return or counter consumer that matters, sacrificing is a pure loss.
   `SelfSacHasDeathPayoff` is the existing gate shape. If the menu is open without one, that is a sound
   dominance to collapse.
3. The unbanked-journal warning: a 4,400 s gap at 16 GB RSS is the durability risk named in the barrier
   doc. Check whether defect 1 (producer barrier) is still open at `60f525f17`.
