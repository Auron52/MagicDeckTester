# Elves round 2 (wip/elves-r2) -- notes

Base: origin/phase-1-2-deck-analyzer f2e307fe3 (the landed Elves play batch).

## USER rulings this round (verbatim, 2026-11-27)

- Q1 (hold-back): "Hold all dorks back is okay, but not sufficient in my mind unless we have heuristically
  determined the right option in our situation. i.e. If just searching we should probably consider all, hold back
  normal dorks and hold back all dorks. If we add some heuristic logic to figure out the win turn we could
  potentially prune that further."
- Q1 follow-up: "We could search the options in-between, but that seems quite costly." then "Okay, fair enough. If we
  don't need a prune then we don't need a prune. Though we should check how much this particular branch costs and
  make a decision on whether we perhaps do want a prune. My thought was a bit rough, so if we actually need to prune
  this then perhaps we should take the "heuristic calculation" approach to set things up for lethal as soon as
  possible instead."
- Tap order: "This is also a tricky problem, because technically on earlier turns when we are setting up, you
  usually want to use the scaling elf mana last, to get more on board before you tap it. Then in late turns you want
  to work the other way around to leave multiple attackers."
- "That said, when using a scaling spell, (Joraga, Galadhrim, Genesis Wave) you usually would want to prioritize
  attackers or just pump all of the mana into the spell."
- "I see. So yeah, we should take control of the tap order a bit in the heuristic provider to make sure that we
  maintain whatever thing we want to maintain, whether that is the attackers or the scaling mana."
- "Usually on an earlier turn (like turns 2-3) you would focus on building your mana and keep the scaling back.
  After that you usually want to attack. But ideally how we handle this would depend more on our board than the
  turn."
- Q4 (discard): "I don't understand the Taunting Elf comment. Genesis Wave is technically MV 3, but realistically
  needs to be cast for 6 mana or preferably even more to be good. It shouldn't be counted as a cheap spell in the
  discard heuristic. Mana Elves should indeed be kept above spare lands. We ideally do want to play a land next turn,
  though, so keeping one is nice if we can manage it."
- Q6 (kick cap): earlier "I think Kick counts should be capped at lethal size of 20"; now "Yeah, we may want to scale
  it up for 2HG arguably."

## What was built (all temporary selectors are heurarm slots, per job in a manifest)

1. KICK CAP = the opponent's STARTING life (`MultikickCountCap()`, TurnSolver.cpp; `gamesetup::StartingLife()`):
   20 standard, 30 2HG. `kMaxMultikick` (the DB's face synthesis bound) 20 -> 40 (the largest starting life any
   format uses: 20 / 30 / 40). Only Elves holds multikicker / squad cards (Joraga Warcaller, Galadhrim Brigade).
2. DISCARD, Genesis Wave priced at X = 3 (mana value 6) in the threat order's distance-to-castable
   (`ElvesDiscardCost`, keyed on `reveal_x_put_permanents`). It is the only cost read in the Elves discard
   heuristic (`ElvesThreatValue` reads params only; `CleanupDiscardRankingWithOrder` keeps the provider's order).
3. DISCARD, the LAND FLOOR at every land count (was < 4 lands on the battlefield): one land in hand is the last mana
   card shed; mana Elves stay above SPARE lands. Comment block reworded (the Taunting-Elf reading in plain words).
   Tasks 1-3 sit behind the TEMPORARY `MTG_ELVES_R2` (default ON; =0 = origin) for the A/B; delete after it.
4. HOLD-BACK MODES: `MTG_ELVES_HOLDBACK_MODE=band|three|two` (heurarm twins `MTG_ELVES_HOLDBACK_THREE`,
   `MTG_ELVES_HOLDBACK_TWO`). three = {top, top - h_normal, top - h_all}; two = {top, top - h_normal} (the USER's
   "prioritize attackers or just pump all of the mana into the spell"). A target <= 0 admits no scaled count (the
   no-sink plan is not a row); a target with no exact member admits the largest member below it. The scaling census
   gains `inbetween` (rows in the band that are none of the three: the price of the in-between branch) in every mode.
5. TAP ORDER (`MTG_ELVES_TAP_ORDER`, TEMPORARY, default OFF): `ElvesProvider::ManaSourceRank`. On a pre-combat
   payment of a plan that casts a scaled sink (new `PlanTraits::casts_scaled_sink`), PRESERVE ATTACKERS: dorks that
   cannot attack tap first (64), then scaled dorks (65), the attack-capable fixed dorks last (66). Every other
   payment keeps the inherited scaling-preserving order. Keyed on what the plan does, never the turn number.
   Finding (Explore agent, ManaPayment / TurnSolver): the line-10091 rank 61 never applies to Priest / Archdruid;
   GenericProvider::ManaSourceRank maps them to 66 (tap last) when the plan "casts scaler food" -- and Joraga
   Warcaller and Galadhrim Brigade ARE Elves, so every Warcaller / Brigade sink plan taps the Llanowars first -- 64
   (first) on an attack turn with no Elf cast, else 65. Disclosed gaps: the whole-turn prepay's hold/release ladder
   (BatchPrepayMainCasts) is not driven by this rank; ExecutorPayerOrder (the lethal projection's payer model) does
   not set casts_scaled_sink; the Ezuri main-phase pump is not a sink for the tap order.
6. `MTG_ELVES_CHOSEN_CENSUS=1` (diagnostic only, AIEngine plan-choice point): per real main phase, the chosen sink
   count's category (top / normal / all / between / below / nosink) and the board facts for the tap-order frequency.

## Status (2026-11-27, round 2 measured; NOTHING ADOPTED -- the open decisions are the USER's)

Engine snapshot for the A/B: sha1 2cf15402 (wip/elves-r2 at ade22ece9). The census rerun used the census-key fix
(see "Census fix"); it is diagnostic-only, and all 6,000 census games reproduce the A/B digests exactly.

### Units
- Full suite: 4,334 / 4,334 test cases pass (3,687,645 assertions).
- Elves tests (`-tc="*Elves*,*elves*,Joraga*"`, 36 cases) pass under MTG_ELVES_HOLDBACK_MODE=band / three / two,
  MTG_UNPRUNED=1 and MTG_ELVES_TAP_ORDER=1.
- Two TEST fixes (ade22ece9):
  - The kick-cap test expected 45 Forests to pay Warcaller K 21. They pay K 22: {G} + 22 x {1}{G} = 45. The engine
    was right.
  - The band tests pinned only the THREE slot, so env MODE=two leaked into them. They now pin both slots, and the
    "default = band" check runs only when the env selector is unset.

### Single-game checks
- Census line with `inbetween=`: present.
- MTG_ELVES_SCALING_TRACE: prints `between keep` rows.
- `[elves-chosen]` lines: present.
- 2HG kick counts > 20 offered and EXECUTED: Brigade K 30 (the cap) in 2hg gi 242 and 608, Warcaller K 27 in gi 336
  (3 of 1,000 2HG games).

### A/B
Held-out seeds 9610001+gi, 3,000 std + 3,000 2HG per arm, d5 b20 with the profile, ONE pooled batch, 32/32 workers.
rc=0, no RSS-cap aborts (peak rss ~4.7 GB). avg = avg win turn. Paired net is vs origin (identical to vs band, since
band == origin on every game's turn).

| fmt | arm | avg | net vs origin (se) | faster / slower vs band | units/game | x band |
|---|---|---|---|---|---|---|
| std | origin  | 4.3237 | --              | 0 / 0   | 17,776 | 1.0100 |
| std | band    | 4.3237 | +0.0000         | --      | 17,600 | 1.0000 |
| std | three   | 4.3267 | +0.0030 (0.0010) | 0 / 9   | 16,466 | 0.9355 |
| std | two     | 4.3270 | +0.0033 (0.0011) | 0 / 10  | 16,456 | 0.9350 |
| std | twotap  | 4.3207 | -0.0030 (0.0017) | 17 / 8  | 15,976 | 0.9077 |
| std | bandtap | 4.3173 | -0.0063 (0.0015) | 20 / 1  | 17,029 | 0.9676 |
| 2hg | origin  | 4.6213 | --              | 0 / 0   | 38,080 | 1.0024 |
| 2hg | band    | 4.6213 | +0.0000         | --      | 37,988 | 1.0000 |
| 2hg | three   | 4.6257 | +0.0043 (0.0015) | 3 / 16  | 35,599 | 0.9371 |
| 2hg | two     | 4.6267 | +0.0053 (0.0016) | 3 / 19  | 35,512 | 0.9348 |
| 2hg | twotap  | 4.6143 | -0.0070 (0.0025) | 38 / 17 | 34,310 | 0.9032 |
| 2hg | bandtap | 4.6090 | -0.0123 (0.0023) | 42 / 5  | 36,779 | 0.9682 |

Total units per arm over both formats:

| arm | total units |
|---|---|
| origin | 167.57M |
| band | 166.77M |
| three | 156.19M |
| two | 155.91M |
| twotap | 150.86M |
| bandtap | 161.42M |

- Tasks 1-3 (band vs origin) are play-inert at d5 b20: the same win turn on every one of 6,000 games. 1 std and 5
  2HG digests differ. Units are 1.0% lower in std and 0.24% lower in 2HG.
- Won counts: 2993 / 3000 std in every arm; 2990-2991 / 3000 in 2HG.

### d8 b0 bar
`r2_mk_d8.py`: 155 jobs, depth 8, budget 0, --ignore-play-profile, each arm's own flags. "Recovers" means the slower
arm at d8 b0 wins no later than the reference arm did at d5 b20. Every reference arm played the same turn at d8 b0 as
at d5 b20.

| arm vs band | std slower / recover | 2HG slower / recover | not recovered |
|---|---|---|---|
| three   | 9 / 0  | 16 / 2 (1620, 2606)       | std 49 336 435 996 1383 1592 1756 2459 2615; 2hg 401 485 1019 1058 1106 1457 1519 1592 1622 1699 1837 2673 2782 2824 |
| two     | 10 / 0 | 19 / 2 (1620, 2606)       | three's list + std 459, 2hg 1761 2273 2852 |
| twotap  | 8 / 0  | 17 / 3 (1620, 1816, 2606) | std 49 336 459 1383 1592 2176 2459 2615; 2hg 75 202 428 1019 1106 1519 1592 1622 1699 1837 2162 2673 2824 2852 |
| bandtap | 1 / 0  | 5 / 1 (1816)              | std 2176; 2hg 75 202 428 2162 |

The vs-origin lists are identical. So three and two drop lines the search needs at ANY budget: the in-between
counts are load-bearing, not budget noise.

### The price of the in-between counts (band vs three)
- **Work:** three removes 6.45% of Elves units in std (1,134 units/game) and 6.29% in 2HG (2,389 units/game).
  two removes only 0.05% more than three.
- **Plans:** in the scaling census (band arm, 1,000 games each), the in-between rows are 2.56M of 30.83M
  enumerated plans in std (8.3%) and 6.77M of 66.58M in 2HG (10.2%). That counts every enumeration, rollouts
  included.
- **How often the band EXECUTES a count that is neither top nor top - h_normal:** std 67 of 701 single-sink
  payments (9.6%), 2HG 70 of 871 (8.0%). Nearly all are `between`; only 3 per format are `all` (top - h_all). By
  sink, Genesis Wave is the largest share, then Galadhrim Brigade, then Joraga Warcaller. All are pre-combat.
- **Census floor:** the twotap arm, whose menu should hold only top and top - h_normal, still reads `between` on
  3.3% (std) and 2.7% (2HG). The census key is not exactly the admit rule's class partition (approximation 1 below
  adds to it), so the genuine in-between share in band is roughly 5-6%.
- **Verdict:** the price is MATERIAL (about 6.4% of Elves units), and removing it is LOSSY at every budget
  (+0.003 / +0.004 turns; 0 of 9 and 2 of 16 slower games recover at d8 b0). By "a budget may re-price, not
  remove", three and two are out as prunes. The design below is the sanctioned shape. NOT BUILT.

### Design (deferred, not built): budget-staged hold-back counts keyed on lethal
- **Wave 1** (admitted first under any budget): top, top - h_normal, top - h_all. That is the `three` set, which
  covers about 91% of executed sink counts.
- **Wave 2** (admitted when budget remains; ALWAYS at budget 0 / unlimited): the in-between counts, ordered by a
  board-feature lethal estimate. For each hold-back count k, compute the damage the dorks it leaves untapped would
  add this turn (attack-capable, not summoning sick) plus the sink's own contribution at count k. Rank first the
  counts whose total reaches the opponent's remaining life (the earliest lethal); then by the scaled dorks' growth
  kept for next turn.
- **Keys:** board features only (remaining life, attackers, scaled-dork yields, sink in hand), never the turn
  number. Nothing is refused at unlimited budget, so it re-prices and never removes. It stays searched-with-heuristics
  (the order only decides which counts a finite budget reaches first).
- **Owed if built:** a held-out A/B against band, and the d8 b0 bar on every slower game.

### Tap order (MTG_ELVES_TAP_ORDER, default OFF; the USER's decision)
- **How often the order can matter** (pre-combat, at least one attack-capable fixed dork AND at least one scaled dork
  untapped, plan cost > untapped lands):
  - std: 11.4% of main phases (492 / 4,322), in 36% of games (360 / 1,000); 320 of those are scaled-sink payments.
  - 2HG: 14.0% (647 / 4,615), in 44% of games (444 / 1,000); 435 are scaled-sink payments.
- **Turn distribution of the mode the rank picks** (band arm). These are census facts keyed on the board; the rank
  itself never reads the turn.

  | mode | std | 2HG |
  |---|---|---|
  | PRESERVE ATTACKERS (pre-combat, scaled sink), all | T2 6, T3 190, T4 380, T5 117, T6+ 8 | T2 4, T3 201, T4 447, T5 196, T6+ 23 |
  | PRESERVE ATTACKERS, order can matter | T3 104, T4 177, T5 35, T6+ 4 | T3 124, T4 237, T5 66, T6+ 8 |
  | PRESERVE SCALING, order can matter | T3 94, T4 68, T5 10 | T3 122, T4 66, T5 23, T8+ 1 |

  This matches the USER's description: scaling is kept back while setting up (T3), attackers kept on sink turns
  (T4-T5).
- **Results:**
  - bandtap is better and cheaper than band in both formats: -0.0063 (se 0.0015) std, -0.0123 (se 0.0023) 2HG;
    0.968x units; 62 faster / 6 slower.
  - 5 of the 6 slower games do NOT recover at d8 b0 (std 2176; 2HG 75, 202, 428, 2162). Root cause below.
  - twotap is worse than bandtap on both formats (it carries two's lossy prune).
- **Root cause of the unrecovered bandtap losses (std gi=2176, band T3 vs bandtap T4)**, reproduced single-game:
  - The two arms diverge on T2. With Forest, Forest and Llanowar, band casts Elvish Archdruid; bandtap casts Galadhrim
    Brigade.
  - Band's T3 kill is one all-in main: Lodge, Mystic, Warcaller, Taunting Elf, Genesis Wave X=6, then the Brigade the
    Wave reveals.
  - Under MTG_ELVES_TAP_ORDER the rank is PER PLAN (PlanTraits::casts_scaled_sink). Once a plan casts a scaled sink,
    EVERY payment in it uses PRESERVE ATTACKERS, including the cheap Elf casts made BEFORE the sink. That ranks the
    scaled dork (Archdruid, 65) ahead of the fixed Llanowar (66).
  - The Archdruid taps for one {G} per Elf, so tapped early (with fewer Elves on board) it yields less. The T3 kill
    line is then unaffordable in the search, and the T2 choice moves.
  - This is the USER's first sentence ("use the scaling elf mana last, to get more on board before you tap it")
    violated INSIDE a sink turn.
- **Proposed amendment (NOT built):** PRESERVE ATTACKERS applies only to the SINK's own payment and the payments after
  it. Payments for the casts that come before the sink keep PRESERVE SCALING (scaled dorks last).
  - Equivalently: never tap a scaled dork while an Elf creature cast of the same plan is still to come.
  - Owed: a held-out A/B of bandtap vs this amendment, and the d8 b0 bar on the slower games.
  - The 2HG losses (75, 202, 428, 2162) were not replayed: the single-game CLI has no --starting-life /
    --opponent-heads, and a 1-game batch writes only .wins. They are unattributed.
- (b) Category turn distribution, band vs twotap: see `build/scratch/elves-r2/cen2/chosen.txt`. Under twotap,
  `between` falls to the census floor and `top` absorbs it.

### Census fix (diagnostic only)
- **The defect:** the first census matched the chosen plan to its count siblings with Plan::operator== on a blanked
  copy. The CHOSEN plan carries fields the solver sets after enumeration, so nothing matched and every row read
  `top`.
- **The fix:** the census key is now the land plus the sorted action identities, the sink's count left out. It also
  prints the member count (mean 26-29 per sink payment, never 0).
- **Play is unaffected:** the admit rule compares only freshly enumerated plans, so its equality is sound.

### Approximations to disclose (carried from the handoff)
1. top - ceil(h_normal / unit) can be one count too low (the admit function does not see total mana).
2. The whole-turn prepay (BatchPrepayMainCasts) and ExecutorPayerOrder ignore the tap-order rank. The Ezuri pump is
   not a sink.
3. The origin tap order taps the Llanowars first on Warcaller / Brigade sink plans (GenericProvider rank 66 for
   Priest / Archdruid when the plan casts Elf "scaler food").
4. New: the chosen census reads a plan with two scaled sinks as `nosink`, so the attackers-mode counts are a slight
   undercount.

## Round 3 follow-up (2026-11-27; full write-up in logs/elves/r3-notes.md)

- **Fix:** PRESERVE ATTACKERS applies only from the scaled sink's OWN payment onward. The set-up casts before it keep
  the scaling mana (`ElvesSinkReached`). Arm `bandtapfix` = band + MTG_ELVES_TAP_ORDER (fixed).
- **A/B** (same seeds, 3,000 std + 3,000 2HG, d5 b20, one pooled batch; band and bandtap reproduced this round's
  digests on all 12,000 games):

  | fmt | arm | avg | net vs band (se) | faster / slower | units x band |
  |---|---|---|---|---|---|
  | std | bandtap    | 4.3173 | -0.0063 (0.0015) | 20 / 1 | 0.9676 |
  | std | bandtapfix | 4.3170 | -0.0067 (0.0015) | 20 / 0 | 0.9689 |
  | 2hg | bandtap    | 4.6090 | -0.0123 (0.0023) | 42 / 5 | 0.9682 |
  | 2hg | bandtapfix | 4.6083 | -0.0130 (0.0021) | 39 / 0 | 0.9727 |

- **d8 b0 bar:** bandtapfix has 0 slower games vs band, so nothing is owed and the bar is MET.
- **The 5 unrecovered bandtap games** (std 2176; 2HG 75, 202, 428, 2162): bandtapfix replays band's game action for
  action in all five. The lever flip attributes all five to the pre-sink tap order, the same mechanism as 2176.
- **USER** kept the band ("Okay, that's fine. We'll leave it."). The three / two modes and MTG_ELVES_R2 are deleted
  in the round-3 tree.
