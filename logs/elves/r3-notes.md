# Elves round 3 (wip/elves-r2) -- notes

Base: wip/elves-r2 f9d1ad107 (round 2: `logs/elves/r2-notes.md`, `logs/elves/r2-handoff.md`).

## USER rulings this round (verbatim, 2026-11-27, relayed by the coordinator)

- On "band vs three/two": "Okay, that's fine. We'll leave it." -> band stays the rule; the three / two selector and
  code paths are DELETED (task 4).
- ACCEPTED, NOT IMPLEMENTED HERE (it comes after this round, on the same function, ElvesScalingAdmit): admit a second
  scaled sink in one plan when a scaled source grown by the first sink's bodies pays for it (Brigade 814: the first
  Brigade's Elves grow Marwyn, whose tap pays the second Brigade).
- Carried (round 2, the tap order): "we should take control of the tap order a bit in the heuristic provider to make
  sure that we maintain whatever thing we want to maintain, whether that is the attackers or the scaling mana";
  "Usually on an earlier turn (like turns 2-3) you would focus on building your mana and keep the scaling back. After
  that you usually want to attack. But ideally how we handle this would depend more on our board than the turn.";
  "when using a scaling spell, (Joraga, Galadhrim, Genesis Wave) you usually would want to prioritize attackers or
  just pump all of the mana into the spell."

## 1. The fix: attackers order from the SINK's own payment onward

- Round-2 defect (std 2176): `MTG_ELVES_TAP_ORDER` applied PRESERVE ATTACKERS to every payment of a plan that casts a
  scaled sink, so the cheap Elf casts BEFORE the sink tapped the Archdruid / Priest first (scaled dork 65 ahead of the
  attack-capable Llanowar 66). A scaled dork tapped before the plan's Elves land yields less; the kill line became
  unaffordable and the T2 choice moved.
- Round 3 (`ElvesSinkReached`, DecisionProviders.cpp): the attackers order applies only once the payment has reached the
  sink. That is (a) the sink's OWN bill, recognised through the paying-spell scope (`PayingSpellCard()`, set at both
  cast sites and in SeqWalkFixedOrder with the kicked face: `params.kick_times > 0`, or an X-put spell), or (b) any
  payment after it: a copy of a sink recorded in `PlanTraits::scaled_sink_name / scaled_sink_hand0` (the sink's
  printed name and its copies in hand when the traits were built; filled only when MTG_ELVES_TAP_ORDER is on, so the
  OFF arm's traits hash is unchanged) has left the hand. Every payment before it keeps the inherited
  scaling-preserving order (scaled dorks last). Keyed on what the plan does, never the turn number.
- Disclosed gap: a breakpoint continuation's traits are rebuilt over the REMAINING actions; once the sink is already
  cast it is not in them, so payments after it in such a continuation fall back to the scaling order (round 2's
  per-plan rank had the same property).
- Unit tests (test/unit/test_elves.cpp):
  - "a set-up Elf cast BEFORE the sink keeps the scaling mana (the 2176 shape)": Archdruid + 2 Llanowars, no lands;
    hand Mystic + Brigade. Mystic then Brigade squad x1 (6) is payable only if a Llanowar pays the Mystic and the
    Archdruid taps for 4 after it. It resolves in both worlds under the fix. Under the round-2 whole-plan order
    (checked once with the temporary MTG_ELVES_TAP_WHOLEPLAN=1) the plan is enumerated but fails on apply: no squad
    copy, both Llanowars still untapped. The 2176 mechanism, reproduced on a built board.
  - "the sink's own payment still PRESERVES ATTACKERS after a set-up cast": + 1 Forest; the Forest pays the Mystic, the
    Brigade's 5 is the Archdruid (4) and ONE Llanowar, the other untapped (the inherited order taps both).

## 2. 2HG single-game repros

- The single-game CLI DOES do 2HG: `MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2` (gamesetup::StartingLife / OpponentHeads
  env fallbacks). `logs/elves/r3_one.sh <fmt> <gi> <arm>` replays one A/B game (seed 9610001+gi, --game-index gi) on
  the snapshot binary; it reproduced every batch win turn below. `logs/elves/r3_diverge.py` prints the first
  divergence of two game logs.

| game | band | bandtap (round 2) | bandtapfix | first divergence band vs bandtap |
|---|---|---|---|---|
| 2HG 75   | T4 | T5 | T4 | T2: band Archdruid, bandtap Ezuri |
| 2HG 202  | T4 | T5 | T4 | T3: band Archdruid + Brigade K1 (5); bandtap Archdruid only, Mystic untapped |
| 2HG 428  | T4 | T5 | T4 | T2: band Archdruid, bandtap Brigade K0 |
| 2HG 2162 | T3 | T4 | T3 | T2: band Priest + Crossroads, bandtap Archdruid |
| std 2176 | T3 | T4 | T3 | T2: band Archdruid, bandtap Brigade K0 |

- **Same mechanism, all five.** bandtapfix replays band's game ACTION FOR ACTION in every one (full log equality), and
  bandtapfix differs from bandtap only in the payments before a sink. So the lever flip attributes each loss to the
  pre-sink tap order. Where it bites:
  - 202: the executed T3 line itself. The new Archdruid's bill tapped the board Archdruid before the Mystic (scaled 65
    before fixed attacker 66), so the Brigade K1 that follows is unaffordable.
  - 2162, 2176: band's kill turn is Elf set-up casts and then Genesis Wave with an Archdruid / Priest on board
    (2162 T3: Archdruid, Priest, Wave X3, Clancaller, Brigade; 2176 T3: Mystic, Warcaller, Taunting Elf, Wave X6,
    Brigade). Under the whole-plan order that line is unaffordable in the search, so the T2 choice moves.
  - 75, 428: band's kill turn has no set-up-before-sink payment (75 T4 Ezuri + Clancaller; 428 T4 Brigade K3 +
    Shepherd), so the mispriced line is inside the T2 lookahead rather than the executed game.

## 3. A/B (ONE pooled batch, 18,000 jobs, 32/32 workers, rc=0, 12:20-12:42 UTC)

Round-2 held-out seeds 9610001+gi, game_index gi, 3,000 std + 3,000 2HG per arm, d5 b20 with the profile; 2HG = life
30 + 2 heads. Snapshot sha1 fc83c4c8 (wip/elves-r2 f9d1ad107 + the round-3 working tree, before the tidy).
`logs/elves/r3_ab_manifest.py`, `r3_run.sh`, `r3_abstat.py`; output `build/scratch/elves-r3/ab/`.

**Apparatus check:** band and bandtap reproduce round 2's band and bandtap arms exactly, (turn, digest) on all
12,000 games. So the fix's traits fields are inert with the selector off, and the MTG_ELVES_R2 deletion is play-inert
(that binary already had it deleted; round 2's band ran it ON).

| fmt | arm | avg | net vs band (se) | faster / slower vs band | units/game | x band |
|---|---|---|---|---|---|---|
| std | band       | 4.3237 | --               | --      | 17,600 | 1.0000 |
| std | bandtap    | 4.3173 | -0.0063 (0.0015) | 20 / 1  | 17,029 | 0.9676 |
| std | bandtapfix | 4.3170 | -0.0067 (0.0015) | 20 / 0  | 17,053 | 0.9689 |
| 2hg | band       | 4.6213 | --               | --      | 37,988 | 1.0000 |
| 2hg | bandtap    | 4.6090 | -0.0123 (0.0023) | 42 / 5  | 36,779 | 0.9682 |
| 2hg | bandtapfix | 4.6083 | -0.0130 (0.0021) | 39 / 0  | 36,950 | 0.9727 |

- Won: std 2993 / 3000 and 2HG 2991 / 3000 in every arm.
- bandtapfix vs bandtap: std -0.0003 (se 0.0006), 2 faster (1342, 2176) / 1 slower (449); 2HG -0.0007 (se 0.0009),
  5 faster (75, 202, 428, 1816, 2162) / 3 slower (786, 1226, 2371). The fix keeps essentially all of bandtap's gain
  (std 18 of its 20 faster games, 2HG 37 of 42) and removes every one of its losses. It costs +0.14% / +0.47% units
  over bandtap; it is still 3.1% / 2.7% cheaper than band.

## 4. d8 b0 bar (CLAUDE.md: every slower game recovers at --depth 8 --budget-ms 0; aggregate net <= 0)

- **bandtapfix vs band: 0 slower games in either format, so there is nothing to recover. The aggregate is net < 0 in
  both (std -0.0067, 2HG -0.0130). The bar is MET.**
- Informational (NOT part of the bar): d8 b0 on the 4 games where bandtapfix is slower than the old bandtap
  (std 449; 2HG 786, 1226, 2371; in each bandtapfix plays band's turn). Manifest `build/scratch/elves-r3/ab/d8x.json`
  (8 jobs), output `d8x.out` / `d8x.err`; verdict: `python3 logs/elves/r3_d8_verdict.py $S/ab.out $S/d8x.out
  bandtapfix bandtap`. Queued in box_lane at handoff (PID 2825391).

## 5. Tidy (task 4) -- BUILT 13:17 UTC (EXIT=0), units green, digests identical
- MTG_ELVES_R2 deleted: kick cap = starting life, Wave priced X = 3, land floor at every land count, all
  unconditional. The R2=0 test checks are removed.
- USER ruling (band stays): MTG_ELVES_HOLDBACK_MODE, the heurarm twins MTG_ELVES_HOLDBACK_THREE / _TWO, the three /
  two branches, the in-between counting (`inbetween=`, `between` trace tag, nearest_below) are deleted. The three/two
  unit test is now a band test on the same board. The band rule itself is unchanged (`drop = count < top - h`).
  ElvesHoldBackNormalMana stays: the chosen census (diagnostic) reads it.
- The temporary MTG_ELVES_TAP_WHOLEPLAN A/B slot (round-2 order) is deleted again. MTG_ELVES_TAP_ORDER stays (see 6).
- Informational d8 b0 RESULT (finished after the handoff): none of the 4 recovers. bandtapfix at d8 b0 wins on band's
  turn and the OLD order stays a turn faster: std 449 (T5 vs T4); 2HG 786 (T5 vs T4), 1226 (T4 vs T3), 2371 (T5 vs T4).
  So in these games, tapping a scaled dork early for a set-up cast (keeping a fixed attacker untapped) is a genuinely
  faster line that neither the fix nor band finds at any budget: the set-up order is a fixed heuristic, not searched.
  It is not a bar failure (the bar is vs band). Owed if pursued: root-cause one of the four (r3_diverge.py on
  bandtap vs bandtapfix). The candidate amendment is a board-keyed exception: a set-up cast taps a scaled dork first
  when its yield cannot grow before the sink (no Elf cast between it and the sink), or when the attacker it saves
  is worth more.

- Tidy binary (build 13:11/13:17 UTC, EXIT=0): full units 4336/4336 (3,687,541 assertions); Elves filter
  (`-tc="*Elves*,*elves*,Joraga*"`) 38/38; full units under MTG_ELVES_TAP_ORDER=1 4336/4336. MTG_UNPRUNED=1: the full
  suite single-threaded ran 1h45m and was killed at the coordinator's request; the Elves filter was re-run under it
  (tidy binary): 38/38 (5,496 assertions).
- Inertness replay: 200 jobs of `build/scratch/elves-r3/ab/ab.json` (100 games x band/bandtapfix: 70 games whose arms
  differ, 30 identical; no MTG_ELVES_TAP_WHOLEPLAN job), manifest `build/scratch/elves-r4/replay.json`: **200/200
  digests identical** to ab.out (provider=Elves).

## 6. ADOPTED: MTG_ELVES_TAP_ORDER default ON (2026-11-27)
- The coordinator adopted it under the USER's standing rule "almost anything that measures better and is cheaper is a
  good option": bandtapfix vs band std -0.0067 (se 0.0015) 20 faster / 0 slower; 2HG -0.0130 (se 0.0021) 39 / 0;
  ~0.97x units; d8 b0 bar met (no slower game).
- `ElvesTapOrderOn()` reads `EnvOn("MTG_ELVES_TAP_ORDER", true)`; `=0` restores the scaling-preserving order. The
  tap-order unit test now also pins the default (arm unset, no env: same taps as the forced-on arm).
- Elves play moves at the default: smoke/regression/overnight Elves GT is owed through the normal accept flow (the
  coordinator integrates with wip/elves-mem2 and validates the combination).
- Flipped build (15:33/15:38 UTC, EXIT=0): full units 4336/4336 (3,687,546 assertions: +5 from the default-pin
  checks); Elves filter under MTG_ELVES_TAP_ORDER=0 (the hatch) 38/38.
