# K fleet-wide + SG50 / SG50+K: report

Written by the parent session from the agent's final message (its harness blocks report files).
Tables, verdicts and scripts are in this directory (412d435d0). The rebased beam is `wip/beam-staged2`
at 0c2465c8a. No default was changed.

## Setup

- **Binaries:** "today" is origin e430cc585. "SG50" is `wip/beam-staged` rebased onto e430cc585; its
  net `src/` patch matches the original line for line. Both are Release builds with `MTG_PGO=0`.
- **Arms:** K is set per job (`MTG_LEAF_KILL_ONLY_NOM2=1`), at each deck's play settings, standard and
  2HG, paired on (seed, game_index).
- **Seeds:** 57.1M for CG/AL/DS (5000 std + 2000 2HG, four arms); 57.5M for the other 29 armed decks
  (1000 + 500); 58.0M for a follow-up on Maze's End, Gods and Mill (3000 + 1500 more each, pooled to
  4000 + 2000).
- **Runs:** one pooled batch per binary, under one hold of the box lock, 32 threads, 32/32 busy.
- The CPU column for SG50 vs today compares two processes, so treat it as rough. Units are deterministic.

net = slower games minus faster games, so negative is better.

## 1. K fleet: K=1 vs today

| deck | n std/2HG | net std (faster/slower) | net 2HG | t std / 2HG | mean turn std (today -> K) | units std / 2HG | cpu std / 2HG |
|---|---|---|---|---|---|---|---|
| Maze's End | 4000/2000 | -77 (83/6) | -36 (38/2) | -8.23 / -5.74 | 5.5140 -> 5.4947 | x0.766 / x0.744 | x0.911 / x0.889 |
| Gods | 4000/2000 | -8 (15/7) | -1 (7/6) | -1.78 / -0.37 | 5.5637 -> 5.5602 | x0.990 / x0.981 | x0.999 / x0.989 |
| Mill | 4000/2000 | +12 (6/18) | +2 (0/2) | +2.45 / +1.41 | 4.9890 -> 4.9920 | x0.927 / x0.929 | x0.886 / x0.890 |
| Creature Giving | 5000/2000 | -1 | -1 | -1.0 / -1.0 | 4.6206 -> 4.6204 | x0.985 / x0.961 | x0.996 / x0.965 |
| Dragonstorm | 5000/2000 | +2 (0/2) | 0 | +1.41 / 0 | 4.3730 -> 4.3734 | x0.958 / x0.950 | x0.979 / x0.975 |
| Elementals | 1000/500 | 0 | -1 (2/1) | 0 / -0.58 | 4.337 | x0.992 / x0.995 | x0.988 / x0.967 |
| KittyV3 | 1000/500 | +1 (0/1) | 0 | +1.0 / 0 | 3.814 -> 3.815 | x0.969 / x0.977 | x0.901 / x0.924 |
| Pirates | 1000/500 | 0 | -1 (1/0) | 0 / -1.0 | 4.377 | x0.986 / x0.945 | x0.974 / x0.922 |

The other 23 armed decks are 0 / 0 (every paired game has the same turn), with units x0.908-1.000 and
CPU x0.87-1.03. Anti-Lifegain (K does not arm) is byte-identical.

- Totals: -71 standard, -38 2HG, -109 overall. Without Maze's End it is +6 / -2, and Mill alone is +12 / +2.
- Mill's 22 slower games all recover at d8 b0, so they come from how the freed budget is spent, not from
  anything structural. Which decisions it moves on Mill was not investigated.

**K slower games (45): none is real.** 40 recover at `--depth 8 --budget-ms 0` (Mill 22, Maze's End 8,
Gods 6, Dragonstorm 2, Elementals 1, KittyV3 1). The 5 that do not are all Gods, and `MTG_NC_SEARCH=1`
at play settings gives both arms the same turn in each, so they are clairvoyant:
57612000 gi21, 57609000 gi70, 57613000 gi35, 58050000 gi18 (d8 b0 abandoned at 40M units), 58063000 gi113 (2HG).

## 2. SG50 and SG50+K vs today on CG / AL / DS (5000 std + 2000 2HG, paired)

| row | arm | net (faster/slower) | t | mean turn (today -> arm) | units | cpu |
|---|---|---|---|---|---|---|
| CG std | SG50 | -11 (21/10) | -1.98 | 4.6206 -> 4.6184 | x1.029 | x1.012 |
| CG std | SG50+K | -11 (21/10) | -1.98 | 4.6206 -> 4.6184 | x1.004 | x0.987 |
| CG 2HG | SG50 | -13 (19/6) | -2.65 | 4.9055 -> 4.8985 | x1.035 | x1.052 |
| CG 2HG | SG50+K | -14 (19/5) | -2.89 | 4.9055 -> 4.8980 | x0.993 | x1.000 |
| AL std | SG50 | -6 (6/0) | -2.14 | 4.2088 -> 4.2072 | x1.010 | x1.017 |
| AL std | SG50+K | -6 (6/0) | -2.14 | 4.2088 -> 4.2072 | x1.010 | x1.011 |
| AL 2HG | SG50 | +2 (0/2) | +1.41 | 4.1335 -> 4.1345 | x1.030 | x0.972 |
| AL 2HG | SG50+K | +2 (0/2) | +1.41 | 4.1335 -> 4.1345 | x1.030 | x0.970 |
| DS std | SG50 | -1 (3/2) | -0.45 | 4.3730 -> 4.3728 | x1.050 | x1.032 |
| DS std | SG50+K | 0 (3/3) | 0.00 | 4.3730 -> 4.3730 | x0.997 | x1.006 |
| DS 2HG | SG50 | -3 (3/0) | -1.73 | 4.4845 -> 4.4830 | x1.056 | x1.062 |
| DS 2HG | SG50+K | -5 (5/0) | -2.24 | 4.4845 -> 4.4820 | x0.998 | x1.025 |

- SG50 alone: -18 standard, -14 2HG, at +1% to +6% units.
- SG50+K: -17 standard, -17 2HG, at units x0.993-1.004 on CG/DS. AL keeps +1% / +3% because K does not arm there.
- K on top of SG50: CG 0 / -1, AL 0 / 0, DS +1 / -2, units x0.95-0.98 on CG/DS.

**SG50 slower games: none is real.** 19 of 20 recover at d8 b0 in each arm. The one that does not is the
same in both: CG 57107000 gi197 (7 -> 8); its d8 b0 replay was abandoned at 40M units, and
`MTG_NC_SEARCH=1` gives 9 = 9 on both arms, so it is clairvoyant. The single slower game of SG50+K vs
SG50 (DS 57175000 gi175) recovers.

## Questions for the USER (data complete)

1. Turn K on by default for every eligible deck? -109 net, lower units on nearly every deck, soundness
   check clean. Cost: Mill +14 over 6000 games, all recoverable at d8 b0. Agent's recommendation: on
   fleet-wide, then deal with Mill.
2. Adopt SG50 together with K on CG/AL/DS? With K: -17 / -17 at roughly flat units on CG/DS (AL +1% / +3%).
   Without K: -18 / -14 at +1% to +6% units. Agent's recommendation: only together with K.
