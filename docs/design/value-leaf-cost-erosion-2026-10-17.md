# Why value leaves started measuring slow (2026-10-17)

Status: root cause found; one fix ADOPTED and pushed (a83d2d77d); four follow-ups OPEN as USER decisions
(bottom of this doc).

The USER's question: *"how the vast majority of decks can be faster with the leaf and suddenly everything is
slower"*, then *"why only recent decks seem to be getting bad leaf performance numbers"*. Landfall's phase E
(2026-10-16) read the staged leaf at 2.29x live's units for -0.027 turns; Snow (10-14) 1.33x; Prevent Damage
(10-11) 1.21x; while July-September leaves had measured 0.07-0.6x.

Evidence lives in `.claude/worktrees/leafcost2/logs/census/` (scripts and summaries; per-game logs were on the
build volume). All numbers are deterministic work units unless marked wall.

## 1. The baseline moved, not the leaves

**The 10-15 strict child cut made no-leaf play 2-3.5x cheaper.** 8bdc96467 ran `MTG_FS_STRICT_CUT` at every
budget (it had been unbudgeted-only; the USER ruled that shape out). It is exact and saves the tie turn a
ROLLOUT leaf plays out in full. Engine time sweep on the seven oldest learned leaves, whose deck inputs (list,
profile, sidecar, keep table) are byte-identical since 09-15, same games (seed 800000 x 200), leaf cost as a
fraction of no-leaf cost:

| deck | 09-16 | 09-23 | 09-30 | 10-07 | 10-10 | 10-15 pre-cut | today, cut off | today |
|---|---|---|---|---|---|---|---|---|
| Knights | 0.08w | 0.09w | 0.10w | 0.07 | 0.07 | 0.07 | 0.09 | 0.30 |
| slivers | 0.07w | 0.07w | 0.07w | 0.07 | 0.07 | 0.07 | 0.10 | 0.40 |
| Auras | 0.09w | 0.15w | 0.15w | 0.11 | 0.11 | 0.11 | 0.14 | 0.43 |
| burn | 0.18w | 0.21w | 0.19w | 0.29 | 0.29 | 0.29 | 0.16 | 0.62 |
| FiveColour | 0.57w | 0.45w | 0.53w | 0.68 | 0.68 | 0.67 | 0.70 | 2.14 |
| Hinata2 | 0.60w | 0.66w | 0.67w | 0.68 | 0.67 | 0.67 | 0.81 | 1.37 |

(w = wall ratio, engines before 10-07 print no units.) Flat for a month, then the cut: Knights no-leaf
13.7k -> 3.9k units/game while its leaf stayed ~1k. A leaf's main saving is skipping rollouts, and the cut
removed much of the rollout cost it used to save. That half is a real improvement to no-leaf play.

**No-leaf QUALITY also improved, and a stale leaf falls behind.** Hinata2, 1,000 paired games per engine:
09-30 leaf vs no leaf -0.048 (se 0.011); today +0.041 (se 0.011). No-leaf play got 0.11 turns better
(5.650 -> 5.537), the leaf (fitted to older play) only 0.02. Not the cut: Hinata's value beam keeps the cut
off.

So every leaf measured after 10-15 -- the recent decks first, because they had no earlier measurement --
faces a much cheaper and somewhat stronger baseline.

## 2. The defect: the cut ADDED work to value-leaf searches (FIXED)

An O(1) leaf costs the same at any cutoff, and under the strict cut the value-leaf ladder's deep passes cost
MORE at an unchanged admission rate: Landfall d5 7.5k -> 11.9k units/pass, Prevent Damage d5 3.6k -> 8.3k,
Pirates d4 2.8k -> 3.9k (the mechanism inside the pass is not pinned down). Bisected: Prevent Damage's
leaf/no-leaf ratio was 1.08x at 1d94003d2 and 1.95x at 8bdc96467; of the commit's four cuts only
`MTG_FS_STRICT_CUT` moved it. The commit's own note ("units rise on nine decks ... cuts admitting deeper
passes") misread it: every one of the nine carries a value sidecar, and the passes are not deeper, they
are dearer.

**Fix (a83d2d77d, `MTG_FS_STRICT_CUT_O1` default OFF):** keep the inclusive cut where the horizon leaf is
O(1). Same answer at every node; re-prices work only. Held-out A/B, 19 learned-leaf decks, 7,559 paired
games: units 0.596x, 2 faster / 0 slower (Giants 0.40x, Bruna 0.45x, slivers 0.48x, Melira 0.52x, FiveColour
0.59x, Prevent Damage 0.62x; WhiteKnights +4.8%, Critter +2.2%, Breaching +1.9%). Tiers: smoke 117/120,
regression 160/166 byte-identical, one slower game = churn.

## 3. Recent decks also start weaker: the 8.8x start gate

The value ladder may start a pass estimated at up to 1.10 x `MTG_VALUE_STARTGATE_ALPHA` (8.0) x the remaining
budget. It exists so a leaf with a TRUST depth can finish the pass that skips the heuristic redo
(`escalation-spend-to-trust.md`; the USER called the flat 8x blunt on 08-14, and the targeted path-to-trust
gate was adopted without retiring it). Without trust there is no redo to skip and the 8x is extra budget.
Recent leaves are mostly untrusted, slower (wins T5-5.7), wider decks: today 57-72% of their leaf's units are
past the per-decision limit (Melira, Giants, Bruna, FiveColour, Prevent Damage) vs <10% on the fast trusted
decks. On Landfall the leaf's whole gain is that budget: tightening the gate removes it step by step
(x8 2.43x -0.018; x4 2.00x -0.007; x2 1.59x -0.002; x1 1.31x +0.012), and live given the same spend does at
least as well (b40 1.38x -0.020; b60 1.72x -0.023; b100 2.06x -0.035). Trusted decks need the gate: Knights
at x1 costs 0.40x vs 0.24x.

## 4. The fleet today (fix arm, 400 games, seeds 800000/801000; leaf cost / no-leaf cost, paired turns)

Still cheaper than no leaf: Knights 0.29, Breaching 0.22, WhiteKnights 0.25, Critter 0.38, Soldiers 0.42,
slivers 0.36, Auras 0.46, burn 0.53, Anti-Lifegain 0.61 (beam), Creature Giving 0.77 (beam), Kitty 0.90,
Pirates 0.64 (-0.010), Selesnya 0.64, Giants 0.79. Dearer even with the fix: FiveColour 1.40 (-0.018),
Melira 1.43 (+0.020, worse), Bruna 1.13 (-0.015), Prevent Damage 1.26 (-0.025), Hinata2 1.32 (+0.041 at
1,000 games, worse). Landfall's staged leaf: 1.78x live, -0.018.

## Open -- USER decisions

1. Scope the 8.8x start gate to leaves with a trust depth (play change for the untrusted shipped leaves:
   Anti-Lifegain, Bruna, Creature Giving, Hinata2, KittyEquipment, Melira Pod, Prevent Damage, Selesnya).
2. Phase E: add a live arm at the leaf's realized spend, so a leaf is judged against live at equal cost
   (tooling only).
3. Re-check every adoption against today's baseline; refit the leaves that now lose on quality (Hinata2,
   Melira) on current play.
4. Landfall: no leaf, or a budget raise for live (b40: -0.020 at 1.38x). Its mulligan generation waits.
