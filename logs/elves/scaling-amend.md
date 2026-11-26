# Elves scaling-sink prune: root cause of the 9 held-out losses, and an amended band (2026-11-26)

Branch `wip/elves-scal` (from `wip/elves-int` f12ace07f). The decision is the USER's; this page measures and
recommends. The prune stays default ON.

USER, verbatim (binding): *"There are multiple scaling effects like Genesis Wave's X and Joraga Warcaller, but
generally you want to pick just one of those and scale it up fully, minus perhaps a few 1-mana producing
attackers. So there might be a couple different relevant options, (i.e. do I tap this Llanowar Elves to scale it
further or keep it as an attacker, which can either be searched or heuristically pruned depending on the
situation) but we shouldn't try every possibility."*

## 1. The nine slower games (A/B 9700001+gi, d5 b20, profile)



Every one replayed single-game from the integration binary at the A/B setting (`--seed 9700001+gi --game-index gi
--games 1 --depth 5 --budget-ms 20 --profile decks/Elves/Elves.profile.json`, 2HG `MTG_START_LIFE=30
MTG_OPPONENT_HEADS=2`, `MTG_ELVES_SCALING_PRUNE=1|0`): all nine reproduce the batch result exactly. Lines diffed
with `scripts/diff_game_lines.py`; boards read from the per-game logs' `boardAfter` (tapped flags). Scratch:
`build/scratch/elves-scal/rep/<fmt>_<gi>_{p,f,p_d8}`.

| game | prune / full | split turn | what full did that prune could not | mechanism |
|---|---|---|---|---|
| std 511 | 6 / 5 | T5 main 1 | Marwyn, then Warcaller **K1** (top K2), Marwyn left UNTAPPED to attack (Crossroads haste), Magistrate counters on Warcaller | **A** scaled dork held back |
| std 1586 | 5 / 4 | T4 main 1 | Warcaller **K4** (top K7): the second Priest AND Marwyn (+2) left untapped to attack under Crossroads; 17 dmg vs 8 | **A** (two scaled dorks) |
| 2hg 806 | 5 / 4 | T4 main 1 | Brigade squad **14** (top 16): one Archdruid left untapped to attack; 35 dmg vs 19 | **A** |
| 2hg 209 | 5 / 4 | T4 main 1 | Brigade squad **1** + Priest (class top 3), both Llanowars left untapped; prune cast Brigade squad 3 alone, every dork tapped | **A'** lumpy scaled tap (trace below) |
| std 1899 | 5 / 4 | T3 main 1 (mulligan bottom differs) | T4 Wave **X5** with both Llanowars + Marwyn untapped (top X7) | **A**, reached through **B** |
| std 700 | 5 / 4 | T2 main 1 | T2 Priest (not Clancaller), T4 Brigade squad 2 with Archdruid held back | **B** (look-ahead) onto **A** |
| 2hg 689 | 5 / 4 | T1 main 1 | T1 Llanowar, T4 Warcaller **K1** + Shepherd's 5/5 (class top K8) with BOTH Archdruids + Llanowar untapped | **B** onto **A**, THREE held attackers |
| std 1159 | 5 / 4 | mulligan bottom (Cavern vs Taunting Elf) | T4 Warcaller K4 + Magistrate with Llanowar, Mystic, Marwyn left untapped | **B** onto **A** |
| std 1917 | 5 / 4 | mulligan bottom; T3 | T3 Archdruid, T4 Brigade squad 1 with Archdruid untapped | **B** onto **A** |

* **A -- a scaled dork (Priest of Titania / Elvish Archdruid / Marwyn) held back as an attacker.** The first cut's
  band counts only 1-mana dorks (Llanowar, Mystic), so a hold-back worth 2-8 mana is below the band and the count
  that leaves it untapped is never enumerated. With a Warcaller/Brigade/Ezuri anthem every held Elf swings for the
  pumped size, so leaving a 4-mana Priest untapped is often worth more than two more kicks. This is the miss the
  design doc listed as "holding back a scaled dork (Priest / Archdruid / Marwyn) ... to attack".
* **A' -- a lumpy scaled tap (2HG 209).** Trace (`MTG_ELVES_SCALING_TRACE=4`, first-cut rule): the class
  {Brigade, Priest} has top squad 3 (Archdruid's single tap, after the Brigade and its tokens are on the board, is
  one lump of 6). k = 2 Llanowars gives h = 1 squad step, so squad 2 is kept and squad 1 dropped; but leaving BOTH
  Llanowars untapped is two steps down (the remainder of the lump is stranded: full left a Forest untapped too).
  The first cut prices a held 1-mana dork at exactly one mana, which a lump of scaled mana does not honour.
* **A, three attackers (2HG 689).** Shepherd's {4}{G}{G} makes every Elf a 5/5, so each untapped body is worth 5+
  damage whatever the Warcaller count: full held back Llanowar AND both Archdruids (11 mana, 7 kicks below the
  class top K8). Trace with the amended band (two largest yields, h = 6): K2..K8 kept, **K1 dropped** -- the one
  game the cap of two attackers still misses. Not findable at d8 b0 with the amended (cap-2) band either (T5).
* **B -- look-ahead.** The search values earlier choices (a T1/T2 cast, a mulligan bottom -- the bottoming
  playouts search with the prune on) through the turn-4 lines the prune removes, so with A missing it prefers a
  different early line. B has no separate cause: in every B game the replay with the amended band (which only
  changes turn-4 admission) recovers the full arm's turn, except 689, which needs the third attacker.
* No game lost to the other disclosed misses (two sinks scaled together, Visionary draws, Ezuri's combat pump, a
  smaller Wave kept for the library).

**Findable at d8 b0 with the prune ON?** No, none of the nine: each replayed at `--depth 8 --budget-ms 0`
(prune on) wins on the SAME turn as the d5 b20 prune arm (511 T6, the other eight T5), and at the split turn it
plays the same pruned line. The full arm finds the faster line already at d5 b20. So the first cut is the LOSSY
kind: the winning line is unfindable at any budget with the prune in place.

## 2. The amended rule

Every miss is one shape: **the count that leaves our attack-capable mana creatures untapped is below the band**,
because the first cut sized the band on 1-mana dorks only (capped at two) and priced each at exactly one mana.
The amendment keeps the USER's rule as it stands (one sink, scaled from its max; no partial split across two
sinks; plans with no scaled sink untouched; post-combat only the max) and changes only what the hold-back
counts:

> **Hold-back band:** the attackers a lower count could leave untapped are *every* untapped mana creature of ours
> that can attack this turn -- Llanowar / Mystic at 1, Priest of Titania / Elvish Archdruid at the Elf count,
> Marwyn at her power (scaled yields grown by the plan's other Elf casts). The band runs from the sink's max
> down to the count that leaves all of them untapped: h = ceil(sum of those yields / mana per count step). Counts
> below that -- mana left unspent that could only come from lands or bodies that cannot attack -- are dropped.

Two variants were measured (temporary heurarm selectors, deleted after the A/B):
* **amend (cap 2):** the two largest yields only -- closest to "a couple of options".
* **holdall:** every yield (the rule above).

In the nine games: amend recovers 8/9 at d5 b20 (2HG 689 needs three held attackers; it is T5 at d8 b0 too);
holdall recovers 9/9.

## 3. Held-out A/B (FRESH seeds)

ONE pooled `mtg --batch`, 12,000 jobs, arms innermost, seeds 9750001+gi / game_index gi, d5 b20 with
`decks/Elves/Elves.profile.json`, 2HG = life 30 + 2 heads; binary = this branch at the A/B commit (15ca40efb).
Manifest: `logs/elves/scaling_amend_manifest.py`; output `build/scratch/elves-scal/ab/ab.out`. Net = arm avg win
turn minus full (lower is better; 0 is neutral).

| format | arm | avg turn | net vs full | faster | slower | units/game | x full | digest != full |
|---|---|---|---|---|---|---|---|---|
| std (2000) | cur (first cut) | 4.3220 | +0.0025 | 0 | 5 (672, 785, 1176, 1320, 1957) | 16,590 | 0.951 | 133 |
| std | amend (cap 2) | 4.3200 | +0.0005 | 0 | 1 (1957) | 17,250 | 0.989 | 33 |
| std | **holdall** | **4.3195** | **+0.0000** | 0 | 0 | 17,240 | 0.989 | 30 |
| std | full (prune off) | 4.3195 | -- | -- | -- | 17,438 | 1 | -- |
| 2HG (1000) | cur (first cut) | 4.5910 | +0.0040 | 2 (375, 696) | 6 (209, 218, 483, 496, 587, 943) | 34,751 | 0.938 | 86 |
| 2HG | amend (cap 2) | 4.5870 | +0.0000 | 0 | 0 | 36,480 | 0.985 | 30 |
| 2HG | **holdall** | **4.5870** | **+0.0000** | 0 | 0 | 36,504 | 0.986 | 27 |
| 2HG | full (prune off) | 4.5870 | -- | -- | -- | 37,040 | 1 | -- |

Won: 1999/2000 std and 1000/1000 2HG in every arm. The first cut reproduces its verdict on the fresh seeds
(+0.0025 / +0.0040 vs +0.0030 / +0.0030). holdall costs the same as cap 2 (the extra counts it admits are rare:
a fourth held attacker is uncommon on turn 4) and is the only arm at net 0 in both formats.

**d8 b0 on the amended arms' slower games:** holdall has none. amend's std 1957: d5 b20 T5 vs full T4; at d8 b0
amend T5, full T4 -- unfindable with the cap. Full's line: Brigade squad 1 off four Forests + Mystic with
Llanowar, both Priests and the Archdruid untapped (four held attackers; amend's class top was squad 9).

## 4. Recommendation (the decision is the USER's)

Adopt **holdall** as the scaling-sink rule's hold-back band. It keeps the USER's structure (one sink scaled
fully, no splits, the max-only band post-combat) and turns "minus perhaps a few 1-mana producing attackers"
into "minus the attackers you might keep back, sized by what each one taps for". In the USER's own example the
one Llanowar still gives exactly one extra option. The scaled dorks are the reason it has to be wider: a
Priest or an Archdruid taps for 4-8, and with a Warcaller / Brigade anthem or Shepherd's 5/5 an untapped Elf is
often worth more than the kicks its mana buys. Measured: net 0 in std and 2HG on 3,000 fresh games, 0 slower,
units x0.989 / x0.986 (the first cut was x0.951 / x0.938 at +0.0025 / +0.0040 with 11 slower, all unfindable
at d8 b0). Most of the remaining saving comes from the no-split rule. A cap at two attackers saves no more work
than holdall and loses a game per ~3,000.

The branch now carries holdall as the default. The temporary selectors are deleted and the prune stays default
ON (`MTG_ELVES_SCALING_PRUNE`, unchanged).

Final commit checks (selectors deleted, holdall the only band): units 4232/4232. The final binary with default
flags against the A/B's holdall arm is digest-identical on 107/107 games (all 57 where holdall differs from full,
plus 50 others), so the A/B row above is the shipped shape. The first cut and the cap-2 variant stay in history at
15ca40efb (`MTG_ELVES_SCALING_AMEND=0` / `MTG_ELVES_SCALING_HOLD_ALL=0`). `MTG_ELVES_SCALING_TRACE=<turn>` stays
as a diagnostic: every scaled row on that turn with class, count, top, band and verdict.
Scratch: `build/scratch/elves-scal/{rep,ab,ident}`.
