# Hand-entry adoption: deck-by-deck follow-ups (filed 2026-09-27, for Monday 2026-09-28)

`MTG_BP_HAND_ENTRY` + `MTG_BP_NODE_S10` went default ON on 2026-09-27 (the measured pair; see
`breakpoints-should-key-on-hand-entry.md`, 2026-09-27 sections). The three tiers were run once each
against the pushed GT `a21b790f` and accepted. The user's ruling on the result:

> "We should be considering this deck-by-deck in reality. We don't want to lose quality on a deck. That
> said, I don't want to stop for now either. The change seems positive, but if there are decks that are
> overall worse we should pick them back up again on Monday."

This file is that Monday list. **Status: OPEN.** The flag stays ON fleet-wide; each deck below owes a
verdict of its own — keep, or find the mechanism and fix it — and `=0` is available per deck only as a
diagnostic, never as a shipped per-deck exemption (a flag default is not a per-deck setting).

## The fleet picture (overnight tier, 191,800 searched games; counts are per game, net is turns)

| deck | slower | faster | net turns | verdict |
|---|---|---|---|---|
| knights | 0 | 12 | −12 | faster |
| pirates | 1 | 13 | −12 | faster (the 1 = churn) |
| goblins2hg / knights2hg / pirates2hg | 0 | 5 / 5 / 4 | −14 | faster |
| goblins | 1 | 5 | −4 | faster, but gi525 persists (below) |
| whiteknights (+2hg) | 0 | 2 (+1) | −3 | faster |
| **melira** | 6 | 3 | **+3** | all 6 slower = budget churn (recover at 4x/16x) |
| **hinata** | 4 | 1 | **+3** | 4 PERSIST; one with identical draws |
| **dragons** | 2 | 0 | **+2** | both churn |
| **snow** | 2 | 2 | 0 turns, but **2 wins → unwon** | gi52 persists at d3 AND d5 |
| every other deck (24) | 0 | 0 | 0 | unchanged |

Smoke and regression tiers: every searched slower game (3 + 1) was churn; the regression one
(hinata d3 s3003 gi171) reaches T4 at 4x/16x, better than the baseline's T5.

## Deck follow-ups, most serious first

### 1. Snow — `snow_overnight_d{3,5}_s6006 gi52`: T8 win → unwon, persists at 4x and 16x

Explain (d3, 10 ms): T1 Scrying Sheets + Astrolabe; T2 Frost Augur + Boreal Druid; T3 Coldsteel Heart +
Astrolabe; T4 Frost Augur + Coldsteel Heart, attack. **T5 old:** Ice-Fang Coatl + Boreal Druid (no
attack). **T5 new:** Ice-Fang Coatl + Kaldring, the Rimestaff, attack. Draws diverge from T6 (old drew
Snow-Covered Forest, new Snow-Covered Mountain — a Scrying Sheets / shuffle consequence of the T5 line),
old wins T8 (opp −10), new is at opp 9 on T8 and does not win inside the case's turn cap. Same at d5.

Questions for Monday: (a) is the T5 Kaldring line chosen BECAUSE a hand-entry breakpoint (Scrying Sheets
put / Frost Augur reveal is new material) opened a continuation the old search never saw, or is it a
budget-independent valuation change? (b) Does `MTG_BP_HAND_ENTRY=0` on the adoption binary restore T8 at
the case budget and at 16x (isolates the flag from everything else on the binary)? (c) Snow's own
argument for the flag is that its digs/reveals ARE hand-entry material — so the fix, if one is owed, is
a line-quality question inside the breakpoint, not the arming rule. Snow's tier avg moved d3 +0.0067 /
−0.0067, d5 +0.0100 / −0.0100 (one win lost, one gained per seed pair): the deck is net zero on turns
and −2 on wins.

### 2. Hinata — net +3; `s6006 gi286` (d3 AND d5) 5→6 with IDENTICAL draws, persists at 16x

The only like-for-like line regression in the whole audit that survives budget: same kept hand, same
draws, one turn slower at 10 ms, 40 ms and 160 ms, at two depths. The other three (s5005 gi307 5→6,
s5005 gi52 4→5 at d5, both persist; s3003 gi171 in the regression tier = churn-and-better) diverge in
draws (Ponder/Preordain/Gamble shuffles) so they are different physical games downstream of a line
change. Hinata's new-material routes under hand-entry: cantrip resolutions (Ponder/Preordain — already
covered by the deferred draw classes), Gamble's discard-then-tutor, Expressive Iteration's exile-play.
Monday: replay gi286 with `MTG_FD_TRACE=1` both arms, find the first differing committed phase, and
ask whether the hand-entry continuation list at that phase contains a plan the old search ranked
correctly and the new one ranks wrong (a canon-default / `MTG_BP_BASE_CANON` question is the standing
suspect — see `breakpoint-arming-rule-measured` "still open").

### 3. Melira — net +3, all six slower games are churn

Every one recovers to the old turn at 4x and 16x; three of the six have identical draws (s7007 gi102,
d5 gi141) — the flag's extra armings (Pod activations, Viscera Seer / Carrion Feeder sacrifice
triggers = the activated-ability class the flag was built for) cost budget at 10 ms. Monday: this is
a **cost** finding, not a quality one — measure Melira's interior nodes on/off (the `MTG_ROLLOUT_STATS`
census, as done for Knights/TH) and decide whether the 10 ms tier budget is simply too tight for the
deck's new arming count. If the extra armings are >60% empty lists (the Knights pattern), the cheap
part is `MTG_BP_ARM_NEW`, still OFF.

### 4. Dragons — net +2, both churn (s7007 gi142/gi284, identical draws)

Both recover at 4x/16x. Same shape as Melira: budget at 10 ms. Low priority; measure nodes on/off.

### 5. Goblins — net −4 but `d5 s7007 gi525` 3→4 persists (draws diverge from T3)

A T3 kill lost to a different physical game after the T2/T3 line changed (Stingscourger vs Mountain
draw). Goblins is net faster (5 faster, 5 in 2HG); this one game is a variance-class persist. Monday:
b0 both arms; if the flag-off arm also fails to find T3 unbudgeted, it is not a flag regression.

## 2026-09-27 evening — the persisting losses are ONE defect, flag-owned, budget-independent

`logs/hecost/persist_probe.sh` (same binary, the pair ON vs `MTG_BP_HAND_ENTRY=0 MTG_BP_NODE_S10=0`)
at the case budget, 16x and **unbudgeted**: every persisting game is owned by the flag at every budget —
hinata gi286 d3/d5 ON=6/OFF=5, gi307 6/5, d5 gi52 5/4, goblins gi525 4/3, snow d3 gi52 ON=unwon/OFF=T8
(at 10 ms and 160 ms). Isolation on hinata gi286 b0: `MTG_BP_HAND_ENTRY=1 MTG_BP_NODE_S10=0` → T6,
`MTG_BP_HAND_ENTRY=0 MTG_BP_NODE_S10=1` → T5. **Hand-entry alone owns it; the node host is inert here.**

**Mechanism (hinata gi286, `logs/hecost/hin286_{on,off}.fd`, `hin286_on_census.fd`):** the T3 search
commits a 5-phase line whose T5 pre-combat phase is `Forbidden Orchard + Expressive Iteration + Reality
Spasm(x5) | bp[k0:Preordain k0:Ponder] bp_choice=2@2` with `win=5 verified=1`. The commit-time
replay-on-copy of that phase already predicts **opp 9, not a kill** (`[fd-pred] turn=5 pre opp_life=9`):
it opens only two breakpoints (`[bp-apply] site=0 idx=0`, `site=3 idx=1`) and never reaches the
targeted idx 2, because at idx 0 the nested-canon default (`ncands.front()`) resolves to
[Preordain, Reality Spasm] where the search's own apply of the same plan resolved to [Preordain, Ponder].
The executor then replays the RECORDED chain — four nested records, `[Preordain Ponder]`, `[Reality
Spasm Preordain]`, `[Ornithopter, Reality Spasm, Soulfire Eruption]`, `[Irencrag Feat, Crackle with
Power]` — and **Irencrag Feat fails to pay (`untapped=[]`)**, as do Crackle and the last Reality Spasm.
So the rollout that produced the line had mana at record 4 that the executor does not have, and the
same plan re-applied fresh does not even produce the same chain. Two lockstep facts: (1) a plan whose
un-targeted nested slots take a value-ranked default is not reproducible by re-applying it (the rank
is not a pure function of state); (2) the recorded chain itself is unrealisable — a rollout/executor
PAYMENT mismatch inside a nested continuation, still to be located (the fd-pred replay re-derives
rather than replaying the records, so `[bp-pay] apply` never shows the rollout's payment for the chain).

Hatch (ON arm, b0): `MTG_NO_BP_PREFIX_CACHE=1` T6, `MTG_NO_BP_ENUM_CACHE=1` T6, `MTG_NO_M2_SEARCH_MEMO=1`
T6, `MTG_BP_BASE_CANON=0` T6, **`MTG_BP_NESTED_CANON=0` T5**. So the nested-canon default (adopted
2026-09-17 with the greedy deletion) is what turns the extra hand-entry breakpoints into an
unreproducible chain; with EMPTY at un-targeted nested slots the chain is short and realisable.
`logs/hecost/persist_nc.sh` re-runs the six games with `MTG_BP_NESTED_CANON=0`; `logs/hecost/nc_tiers.sh`
runs the three tiers under it (no accept) for the fleet picture. **Not flipped**: reverting a measured
adoption fleet-wide is the user's call (Monday), and the underlying payment mismatch is the real bug —
the nested canon only lengthens the chain that exposes it. Also worth checking Monday: the fd-pred
replay should replay the RECORDS (as the executor does) so the oracle reports the executor's outcome,
not a re-derivation's.

## How to run the per-deck check (the same for each)

```
# the flag pair OFF on the adoption binary isolates the flag from everything else that landed
MTG_DUMP_WINS=1 ./build/Release/mtg <deck.cod> --seed <base+gi> --game-index <gi> --games 1 \
    --depth <d> --budget-ms <b> --ignore-play-profile            # ON (default)
MTG_BP_HAND_ENTRY=0 MTG_BP_NODE_S10=0 MTG_DUMP_WINS=1 ./build/Release/mtg ... same ...   # OFF
```
at b = case budget, 4x, 16x and 0 (unbudgeted; Snow d5 at b0 may take a long time — d3 first).
`logs/bpprobe/diverge.sh` prints the committed lines of both arms side by side.
