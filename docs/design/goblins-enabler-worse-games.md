# Goblins enabler ranking: the games it made WORSE — tracked until recovered

**Status:** OPEN. `606a381` adopted the enabler term on its net (161 games better, 39 worse). This
doc is the standing record of the 39, so they are not quietly forgotten. **Goal: recover them by
improving the ranking, without giving back any of the 161.**

**Terminology (user, 2026-08-03):** these are games with a **worse (later) win turn** that regressed
ground truth. Not "losses" — this is goldfishing, an unwon game is a `max_turns` horizon cutoff, not
a defeat, and win/loss framing makes readers weigh the wrong thing. Report turns.

## The list (authoritative: `git diff 0380173..606a381 -- test/gt_logs/goblins_*.wins`)

Regenerate at any time with that diff — it is the committed per-game ground truth, so this list
never needs a re-run to reconstruct.

### Searched depths — 5 distinct games

| case | game | was → now | classification (4×/16× budget) |
|------|------|-----------|-------------------------------|
| `goblins_overnight_d3_s4004`, `d5_s4004` | gi573 | T3 → T4 | **PERSISTS** (4×=4, 16×=4) |
| `goblins_overnight_d3_s7007`, `d5_s7007` | gi849 | T5 → T6 | **PERSISTS** (4×=6, 16×=6) |
| `goblins_smoke_d3_s1001`, `d5_s1001` | gi44 | T4 → T5 | **PERSISTS** (4×=5, 16×=5) |
| `goblins_regression_d3_s2002` | gi289 | T5 → T6 | churn (recovers to T5) |
| `goblins_regression_d3_s3003` | gi112 | T4 → T5 | churn (recovers to T4) |

Only the three PERSISTS games are real quality regressions; the two churn games recover with budget
and need no fix. Reproduce one with (seed = base + gi):

```
build/Release/mtg decks/Goblins/Goblins.cod --profile decks/Goblins/Goblins.profile.json \
  --games 1 --seed 4577 --game-index 573 --depth 3 --budget-ms 0 --ignore-play-profile --log-dir /tmp/g
# baseline arm: prefix MTG_GOBLIN_SWING_LETHAL=0 MTG_GOBLIN_ENABLER_RANK=0
```

### d0 (greedy) — 31 games

d0 takes `cands[0]` directly, so **every d0 regression here is a pure ranking-ORDER mistake**, and
"budget-recoverable" does not apply — there is no search budget to recover with. That makes d0 the
sharpest diagnostic surface for the ranking, even though the searched tiers are what ship.

```
overnight_d0_s4004  gi112 6→7, gi847 6→7, gi871 5→6, gi1045 5→6, gi1147 6→7,
                    gi1347 5→6, gi1690 6→7, gi1814 5→6, gi1857 6→7, gi1860 6→7
overnight_d0_s10010 gi109 5→6, gi119 4→5, gi187 5→6, gi284 5→6, gi845 5→6, gi947 5→6, gi1382 5→6
overnight_d0_s8008  gi243 5→6, gi554 4→5, gi691 5→6, gi1175 4→5, gi1686 5→6, gi1863 5→6
overnight_d0_s6006  gi126 5→6, gi160 5→6, gi860 5→6
regression_d0_s2002 gi523 5→6, gi555 6→7, gi891 5→6
smoke_d0_s1001      gi19 5→6, gi222 5→6
```

## ROOT CAUSE (found 2026-08-03): the enabler term over-credits Goblin Warchief

Diffing the fetched card between arms across the d0 regressions gives one overwhelming pattern —
**11 of 13 sampled games are the same substitution, Goblin Chieftain → Goblin Warchief**:

```
GAME          OLD fetch                  NEW fetch
s4004 gi112   Goblin Chieftain        →  Goblin Warchief
s4004 gi847   Goblin Chieftain        →  Goblin Warchief
s4004 gi871   Goblin Chieftain        →  Goblin Warchief
s4004 gi1045  Goblin Chieftain        →  Goblin Warchief
s4004 gi1147  Goblin Chieftain        →  Goblin Warchief
s6006 gi126   Goblin Chieftain        →  Goblin Warchief
s6006 gi160   Goblin Chieftain        →  Goblin Warchief
s8008 gi243   Goblin Chieftain        →  Goblin Warchief
s2002 gi523   Goblin Chieftain        →  Goblin Warchief
s1001 gi19    Goblin Chieftain        →  Goblin Warchief
s1001 gi222   Goblin Chieftain        →  Goblin Warchief
s8008 gi554   Goblin King             →  Goblin Chieftain
s2002 gi555   Muxus, Goblin Grandee   →  Goblin Warchief
```

Warchief collects `0.50` (cost cut) + `0.30` (blanket haste) = **0.80 × stuck_hand_value**, which
buries Chieftain's `power_bonus × buff_targets × BODY`. The flat fraction is the defect:

* **The cost cut saves exactly one mana per Goblin spell.** Crediting half the stuck bomb's whole
  value asserts the bomb arrives a turn earlier — but −1 mana only advances it when its cost is
  exactly one above what is payable next turn. Hold Muxus (MV 6) on three lands and the cut still
  leaves it uncastable next turn: it buys **nothing**, and was credited ~425.
* **Blanket haste** is likewise only worth something if the card actually lands and would otherwise
  be summoning-sick.

Chieftain's `+1/+1` across the team, by contrast, is immediate damage every combat — which is why it
was the right fetch in all 11.

## PRIORITY (user, 2026-08-03): the SEARCHED regressions matter, d0 mostly does not

> "I'm less worried about the d0 problem than I am about the searched problems. I wouldn't invest
> too much effort trying to get d0 perfect unless that can also help us a lot with searched."

So weigh the **3 persisting searched games** (gi573, gi849, gi44), not the 31 d0 entries. d0 stays
listed because it is a cheap, high-signal diagnostic — it takes `cands[0]`, so a ranking-order error
shows up there undiluted by the search's ability to recover — but a d0-only improvement is **not**
a reason to ship, and a d0-only regression is **not** by itself a reason to reject.

This reframing forced a re-measurement, and it changed one verdict's *reasoning* (see variant 3
below): the original rejection was driven by a combined net that d0 dominated. Split by tier, the
train-searched signal was +1.0 turn-units over 1,325 games — noise. Only the held-out searched run
settled it.

## THREE FIX ATTEMPTS, ALL MEASURED AND REJECTED (2026-08-03)

Recorded so nobody re-treads them. Each was built, built cleanly, and measured against the committed
`606a381` ground truth (which IS the flat-enabler baseline), so "worse" below means worse than what
ships today — not worse than pre-enabler.

| # | variant | result | verdict |
|---|---------|--------|---------|
| 1 | **Turns-saved gate on ALL channels.** Compute the stuck card's arrival turn with/without the enabler over every channel (cut / ramp / free cheat), credit only `base_t - with_t > 0`. | smoke d0 4.7520 → **4.7610** (exactly the pre-enabler value): gave back **9** d0 gains on s1001 alone and recovered **neither** gi19 nor gi222. | REJECTED — broke what worked, fixed nothing |
| 2 | **Gate only the two Warchief clauses** (cost cut, blanket haste); ramp and free-cheat keep their flat fractions, since neither was implicated. | smoke d0 4.7520 → 4.7550: still gave back 3 gains (gi311, gi468, gi823), still recovered neither gi19 nor gi222. | REJECTED |
| 3 | **Variant 2 + token-aware `buff_targets`** — count a hand Goblin's `etb_self_creates_tokens` as extra buff recipients (Siege-Gang is four bodies, not one), cap raised 3 → 5. | **Recovered gi44 at BOTH searched depths** (smoke d3 → 4.2400, d5 → 4.2000, the pre-ranking values) and gi289. Train: d0 +6.0, searched +1.0 (noise). **Held-out overnight: searched +20.0 turn-units over 8,000 games, worse in 7 of 8 cases** (d0 +39.0). | REJECTED — worse on the tier that matters, on the largest sample |

### Variant 3 in detail: why the held-out searched run was the decisive one

Under the user's priority the first rejection was under-argued — it leaned on a combined net that d0
dominated. Re-split, train looked like this:

```
                d0        searched
smoke         +5.0          -2.0
regression    +1.0          +3.0
              ----          ----
              +6.0          +1.0   over 1,325 searched games -> noise
```

That is genuinely ambiguous on searched, so the variant deserved the held-out run it had never had.
It is unambiguous:

```
overnight  d3_s4004 +3.0   d3_s5005 +1.0   d3_s6006 +4.0   d3_s7007 +2.0
           d5_s4004 +3.0   d5_s5005 +0.0   d5_s6006 +4.0   d5_s7007 +3.0
           searched +20.0 turn-units / 8,000 games (+0.0025/game), 7 of 8 cases worse
```

**Lesson worth keeping: a small train-searched delta over ~1,300 games cannot resolve a change of
this size — only the 8,000-game held-out searched tier can.** Cost is ~90 s with
`--deck=goblins`, so there is no excuse for skipping it. This mirrors the trap `6bc04b8` fell into
(a train-only "+0.006, near-full quality" claim that held-out contradicted).

### What variant 3 proved, and why it still matters

The T3 ranking dump for smoke gi19 (`MTG_TUTOR_RANK_DUMP=1`, seed 1020, d0) is the key evidence:

```
[tutor-rank] T3 | G=0 sick=0 ready_atk=0 opp_life=17 | mana: untapped=3 next=4 | buff_targets=2
   0. * Goblin Warchief    score=525.0  value=270.0 +enable=255.0 x disc=1.000 (t=0)
   1. * Goblin Lackey      score=406.0  value=100.0 +enable=306.0 x disc=1.000 (t=0)
   2. * Goblin Chieftain   score=400.0  value=400.0 +enable=  0.0 x disc=1.000 (t=0)
```

Note **G=0** — no Goblins on board. The cost-cut gate fires *correctly* here: the stuck card is
Siege-Gang (MV 5, value 510) on three lands, so `-{1}` genuinely advances it T5 → T4, and variant 1/2
both credit it. **The enabler model is right about the mana and still picks the wrong card.** The
error is on the OTHER side of the comparison: `buff_targets=2` ignores that the stuck Siege-Gang
arrives with three tokens the lord would buff, so Chieftain is scored 400 when its real
contribution is +1/+1 across four incoming bodies.

Variant 3 fixed exactly that and did recover the games it was aimed at — but `buff_targets` feeds
`value_of` for *every* lord in *every* state, so widening it moved far more than the targeted cases
and lost more than it won. **The insight is sound; the lever is too coarse.**

Also visible above: Goblin Lackey draws `+306` (0.60 × 510) onto a 100-value 1/1 body, for a cheat
that requires it to survive and connect. The enabler credit is a fraction of the STUCK CARD's value
and takes no account of how reliable the enabler itself is — a plausible next axis.

### Suggested next directions (untested)

1. **Make the token-awareness local to the comparison rather than global** — e.g. count incoming
   token bodies only when scoring a lord against an enabler, not inside `value_of` generally.
2. **Discount the enabler credit by the enabler's own reliability** — Lackey must survive and connect;
   Warchief's cut only pays if the mana is otherwise wasted. Currently both take a flat fraction.
3. **De-prioritised by the user, but noted:** d0 and searched may want different rankings — d0 takes
   `cands[0]`, the search only needs the right card inside the top 4. A width-aware credit could
   serve both. Only worth building if it also moves searched materially.

**Measurement protocol for anything tried here**, learned the hard way on variant 3:
`bash test/regression.sh --overnight --deck=goblins` (~90 s, 8,000 searched + 8,000 d0 held-out
games) is the gate. Smoke and regression together are only ~1,325 searched games — enough to kill an
obviously-bad variant fast, never enough to *accept* one.

**Method note:** run smoke FIRST when iterating here — it is ~5 minutes, and all three variants above
were killed by smoke d0 alone before spending a full sweep. Confirm any survivor on regression +
overnight with `test/goblins_swing_lethal_ab.sh` before proposing adoption.

## The original fix direction: credit TURNS ACTUALLY SAVED, not a flat fraction

*(This is what variants 1 and 2 implemented; kept for the reasoning, superseded by the results above.)*

Same lesson as the swing-lethal fix in `606a381` — model the real effect instead of a proxy. Compute
the stuck card's arrival turn with and without the enabler in play, and credit only a real
improvement:

```
base_t = arrival_turns(stuck_mv, extra = 0)
with_t = min over the enabler's channels:
           cost cut  -> arrival_turns(stuck_mv - 1, 0)
           Skirk ramp-> arrival_turns(stuck_mv, G - 1)
           Lackey    -> 1 (connects next combat, drops it free)
credit  = (base_t - with_t > 0) ? f(turns saved) * stuck_hand_value : 0
```

The `saved > 0` gate is the load-bearing part: it is exactly what fails for Warchief in the 11 games
above, and exactly what holds in the four the term was introduced to fix (gi602/gi206 Warchief cut
brings Muxus into range on five lands; gi842 Skirk ramp; gi924 Lackey free drop).

**Acceptance bar for any such change:** recover these games *without* regressing the 161 that
improved. Sweep with `test/goblins_swing_lethal_ab.sh` (5 arms, 19,325 games/arm, all three tiers
pooled into one batch; the `base` arm must reproduce committed GT exactly — that is the validity
check). Compare against `606a381` as the new baseline, not against pre-`6bc04b8`.
