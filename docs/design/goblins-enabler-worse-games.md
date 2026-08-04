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

---

## ROUND 2 (2026-08-04): user-directed rules, and the real defect behind the enabler credit

Driven by four domain rules from the user. Measured on held-out goblins overnight
(`regression.sh --overnight --deck=goblins`, ~90 s, 8,000 searched + 8,000 d0), vs shipped `606a381`.

| user's rule | implemented as | held-out searched | verdict |
|---|---|--:|---|
| "Lackey is probably not a great search option for Matron" | drop the Lackey clause / drop Lackey as a candidate | **+12.0 (worse)** | KEEP Lackey — see below |
| "Skirk needs bodies on board; Lackey is the opposite (heavy hand, empty board); both only help you USE GAS IN HAND" | Skirk credit ∝ sacrificeable bodies; Lackey credit ∝ turns-out-of-reach | +1.0 (neutral) alone | keep — recovers gi849 |
| "There should be a play-this-turn benefit" | flat ×1.35 for castable-now | **+10.0 (worse)** | REJECTED as implemented |
| "Test duplicates in HAND, not board — you might want a Muxus in hand for a Lackey drop even with one on board" | discount 0.55 per copy **in hand only** | hand+board +11.0 → hand-only +8.0 → −3.0 once play-now removed | **ADOPT** — the distinction flipped the sign |

### Is Lackey a big help? No — a rare, narrow one (measured)

Excluding Lackey entirely changes **16 games out of 16,000 (0.1%)**: 14 worse, 2 better, net +12.0
turn-units, and **exactly 0.0 at d0**. The zero at d0 is the tell: the line depends on the *searched*
Aether Vial charge decision, which is inert at depth 0.

The mechanism, from `logs` of overnight `s6006 gi842`: hand holds **two Muxus** (MV 6, dead until
turn 6) plus an Aether Vial. The search **overrides the charge heuristic at T2 to add a counter, then
declines at T3 to HOLD the Vial at 1** (`MTG_VIAL_TRACE`: `turn=2 counters=0 win(heur)=6 win(alt)=5`
→ add; `turn=3 counters=1 win(heur)=5 win(alt)=6` → hold). Matron then fetches Lackey (MV 1), the
Vial deploys it free that same turn, and T4 Lackey cheats Muxus in → 6-card reveal → T5 kill. Holding
a Vial at 1 is unusual but correct here, and it is exactly what defeats the "you cannot even deploy
the Lackey that turn" objection. Confirmed legal: every put path gates `ManaValue() == charge_counters`.

### THE defect: "stuck" was measured against untapped mana only

The enabler credit is a fraction of `stuck_hand_value` — the best hand Goblin we cannot deploy. That
test compared the card's cost to **`untapped_mana`**, ignoring the Skirk ramp and next turn's land.
The gi573 dump shows the damage:

```
[tutor-rank] T2 G=4 buff_targets=7 | mana: untapped=1 now=4 next=6
   0. * Goblin Warchief     1638   value= 630  +enable=1008
   1. * Goblin Chieftain     901   value=1260  +enable= 378
   3. * Rundvelt Hordemaster 800   value= 800  +enable=   0
```

`stuck_hand_value = 1260` is a **Goblin Chieftain in hand costing 3, with 4 mana available now and 6
next turn** — not stranded at all. The enablers collected ~1000 points for "accelerating" it, and
Chieftain collected 378 for accelerating **itself**. Testing against `mana_next` instead (the user's
"when we can cast what we have anyway neither is worth considering") **recovers gi573 and gi849**.

Candidate = Skirk/Lackey asymmetry + hand-only duplicates + fixed stuck test:
**held-out searched −5.0 turn-units, d0 +18.0**, 2 of the 3 tracked regressions recovered.

### gi44 is blocked by a PLAN-ENUMERATION BUG, not by the ranking

`smoke d3_s1001 gi44` resists every ranking variant, because its cause is not ranking. Per-phase
board trace (seed 1045):

```
pre-ranking  T1 MAIN_2  bf=[Cavern, Skirk Prospector]  gy=[]      -> T2: attack, sac Skirk for {R}, cast Matron
candidate    T1 MAIN_2  bf=[Cavern]                    gy=[Skirk] -> Skirk sacrificed on T1 for mana NEVER SPENT
```

The engine sacrifices Skirk Prospector in T1 main 2, casts nothing, and the `{R}` is lost. That plan
is **strictly dominated** by the same plan without the sacrifice, so it should never survive
enumeration — a lost body plus lost tempo, which is exactly the turn gi44 gives up (no Skirk → no T2
Matron → the whole line slides a turn). It appears only with the enabler term on (`enabler-only` and
`candidate` reproduce it; `pre-ranking` and `swing-only` do not), so the ranking *exposes* it rather
than causes it: the changed valuation makes the search prefer a plan that was always unsound.

**This is a bug to fix against the rules/solver, not a heuristic to tune** — a ritual/sac-for-mana
whose mana goes unspent should be pruned as dominated. `TurnSolver.cpp` has related pruning around
"drop every group all of whose options are rituals" (~line 4071) but it evidently does not cover
this case.

### The solver bug: found, fixed, adopted — but it did NOT recover gi44 on its own

`SubsetWastesCreatureSacMana` (TurnSolver.cpp) now rejects a subset that activates a **creature**
sac-for-mana outlet while spending no mana at all. Two corrections to the diagnosis above are worth
recording, because both were wrong in an instructive way:

1. **`Solve` already had the rule; only the SEARCH lacked it.** `MTG_NO_RITUAL_PAYOFF_GUARD=1` at d0
   reproduces the sacrifice exactly, so the rituals-for-payoff guard is what kept d0/d1 honest all
   along. `EnumeratePlans` skips that guard *deliberately* — for a HAND ritual, "cast it now or keep
   the card for a later turn" is a real branch the search should arbitrate. That rationale simply does
   not transfer to an **in-play** outlet: declining leaves both the outlet and the body on the
   battlefield, so the branch has no upside to weigh. That asymmetry is the whole fix.
2. **A blanket "sac-for-unused-mana is dominated" prune would have been WRONG for this very deck.**
   Goblins runs three death payoffs — Pashalik Mons (1 damage per Goblin death), Rundvelt Hordemaster
   (impulse exile) and Mogg War Marshal (a token on its own death) — any of which turns a "wasted"
   sacrifice into real value with the mana incidental. The prune bails whenever any death-watcher is
   on the controller's battlefield, and on any subset carrying direct damage.

Measured (prune off reproduces committed GT exactly on every tier — that is the validity check):

```
smoke        d3 4.2467 -> 4.2467 (digest only)   d5 4.2133 -> 4.2000
regression   d3 -0.0134 / -0.0100               d5 -0.0040 / 0.0000
overnight    8 of 8 searched cases BETTER, net -36.0 turn-units / 8,000 games (-0.0045/game)
             36 games better, 1 worse (d3_s4004 gi173, CHURN: recovers at 4x budget and at 0)
             d0 net exactly 0.0 -- Solve already had the rule, so only searched moves
             every non-Goblins deck byte-identical (no other deck has a creature mana outlet)
```

**But gi44 stayed at T5.** Removing the dominated play made both arms play identical T1–T2 lines
(Skirk survives, T2 Matron off the sac), which did not fix the game — it *isolated* it. gi44 is now
a pure ranking question, and it is the SAME Chieftain → Warchief substitution as the 31 d0 games:

```
pre-ranking  T2 Matron fetches Goblin Chieftain -> T3 Chieftain, T4 Piledriver + Lackey,
             Lackey has HASTE from Chieftain -> connects -> cheats Muxus in -> T4 kill
shipped/v2   T2 Matron fetches Goblin Warchief  -> T3 Warchief, T4 Krenko + Piledriver -> T5 kill
```

Chieftain scores 945 (200 body + 400 lord over `buff_targets=4` + 255 haste-enabler); Warchief 1040
(360 body + 0.80 × 850 of "the Muxus in hand arrives sooner"). Warchief's cost cut *does* genuinely
pull a hard-cast Muxus from T6 to T5, which is why the turns-saved gate of variants 1/2 could never
recover this game — the enabler model is right about the mana and still picks the wrong card, exactly
as it was for smoke gi19. What decides gi44 is on the other side: the Muxus that lands on T4 brings
three more Goblins, so Chieftain's real contribution to that swing is +7, not the +4 it is priced at.

---

## ROUND 3 (2026-08-04): all three tracked regressions recovered — ADOPTED

The three tracked searched regressions are **gi44, gi573, gi849** and they are now all recovered, at
their pre-ranking turns. Re-deriving the set against the post-prune ground truth also retired the two
churn entries (gi289, gi112) — the whole searched regression set had shrunk to those three.

The adopted change is a **bundle of three levers that is not decomposable**; each is measured, and two
of them are actively harmful alone:

| lever | what it does | alone, held-out searched |
|---|---|--:|
| `MTG_GOBLIN_RANK_V2` | the round-2 corrections (stuck vs `mana_next`, Skirk/Lackey scaling, hand-only duplicates) | −4.0 |
| `MTG_GOBLIN_LORD_AMP` | a lord also multiplies the bodies the stuck bomb ARRIVES with | **+11.0** |
| `TutorSearchWidth` 4 → 6 | the tutor axis window | −6.0 |

```
                       held-out searched    held-out d0
  v2                        -4.0              +18.0
  amp                      +11.0             -114.0     <- rejected alone
  v2 + amp                  +7.0              -95.0     <- rejected
  W6                        -6.0                0.0
  amp + W6                  -4.0             -114.0
  v2 + W6                   -7.0              +18.0
  v2 + amp + W6             -5.0              -95.0     <- ADOPTED
```

### The finding worth keeping: a ranking loss that was a WINDOW loss

Amplification alone reads as a contradiction — the biggest d0 gain of anything tried here (−114.0
turn-units) *and* a searched loss (+11.0). d0 takes `cands[0]`, so it reads the ordering directly:
−114.0 says the ranking genuinely got better. The searched loss was therefore not a bad order but a
bad **window** — promoting a lord into a four-slot axis pushes out a card the search still wanted.

The measurement separates the two cleanly. Re-run at W=6, the same term is −4.0 on searched while its
d0 gain is **identical** (−114.0, as it must be: width is irrelevant when you take the top card).
Width is cheap here because the tutor axis is additive, not multiplicative — the full-suite smoke
makespan moved 17s → 21s.

**Generalisable:** when a ranking change improves d0 and regresses searched, suspect the width before
rejecting the ranking. `docs/design/searched-action-subdecisions.md` set these widths per provider
against the *old* ranking; a ranking that reorders candidates can invalidate its own window.

### Measured result of the adopted bundle

```
smoke       searched -3.0    d0 -1.0     (0 searched games worse)
regression  searched -2.0    d0 +1.0     (0 searched games worse)
overnight   searched -5.0    d0 -95.0    (held out)
            -> -10.0 turn-units over 9,325 searched games, train and held-out agreeing on sign
every non-Goblins deck byte-identical (the ranking is GoblinsProvider-owned; the width is the
provider's override, NOT the global MTG_TUTOR_WIDTH, which would also move antilife's W=2)
```

Four searched games got slower; three (overnight gi327, gi483, gi830 on s4004) are **churn** — they
recover at 4× budget and hold at unlimited.

### NEW tracked regression: overnight s6006 gi131 (T4 → T5), PERSISTS

The one real cost, and it is the exact mirror of gi44 — same two cards, opposite verdict:

```
base   T2 Matron -> Goblin Warchief; T3 Warchief; T4 King + Stingscourger + Piledriver -> T4 kill
amp    T2 Stingscourger; T3 King; T4 Matron -> Chieftain; T5 Chieftain -> T5 kill
```

Warchief's cut is worth far more here than the ranking can see: with King(3) + Stingscourger(2) +
Piledriver(2) in hand, −{1} each is the difference between two spells and **three in one turn**. The
enabler credit is anchored on the single best stuck card, so it prices the cut as accelerating one
card — right for gi44, badly short for gi131.

**Rejected fix — scale the cut by deployable gas** (`MTG_GOBLIN_CUT_WIDTH`, kept default-off in
`DecisionProviders.cpp` with the numbers in place). Credit `0.25 + 0.25 × min(3, hand Goblins within
one mana of next turn's reach)` instead of a flat 0.50. It does exactly what it was designed to do on
gi131 (T5 → T4) and simply **moves the error**: gi44 goes T4 → T5 and gi573 T3 → T4 straight back.
Held-out: searched −5.0 → 0.0, d0 −95.0 → +37.0. Worse on both tiers.

The useful negative result: **"how much gas the cut unlocks" is not the axis that separates these
states.** One scalar trades them against each other. gi131 is the standing item — a better account of
the cost cut needs to price *multi-spell turns* without inflating the single-bomb case, and neither a
flat fraction nor a linear count of deployable cards does that.

---

## ROUND 4 (2026-08-04): scoring the ranking against real win turns

Every measurement above is a PROXY — aggregate turn-units, or "what width does the search need".
`test/goblins_tutor_truth.py` (via `MTG_TUTOR_FORCE_RANK`) measures the thing itself: force the tutor
to each ranked candidate and record the real win turn. 400 held-out games, 100 with a searched tutor:

```
all 100 decisions       best at #1: 96   top4: 97   top6: 98
the 16 where the fetch
CHANGES the win turn    best at #1: 12   top4: 13   top6: 14
total regret @W=4: 3 turns / 100 decisions        @W=6: 2 turns
```

**The ranking is not broadly wrong.** The fetch is a tie in 84% of decisions, and among those that
matter it picks the best card outright three times in four. The entire W=4 gap is three decisions in
a hundred. Score the ranking only on the decisions where the fetch changes the win turn — averaging
in the ties flatters it badly, and a flat row counts as a "hit" purely by argmin convention.

Two earlier claims in this doc were corrected by that measurement:

* *"All 11 persisting regressions recover at W=12, so the ranking put the right card outside the
  window"* — the recovery numbers were right, the mechanism was not. gi14 is T6 at **every** forced
  rank 1..16 yet reaches T5 at W=12, so part of what wider W buys is extra search branching, not a
  better fetch.
* *"Two of the three W=4 misses are Muxus buried by the deploy discount"* — read off T1
  search-simulated states rather than the decision. Both winning lines deploy Muxus perfectly well
  (gi206 hard-casts it off Skirk sacs; gi33 puts it in via a Lackey connect).

**Instrument limitation:** it forces the same rank at every tutor in a game and collapses the axis to
width 1, so a game needing different picks at two Matrons cannot be represented — gi14 is that shape.

### The real gi206 defect: skirk_ramp counted the wrong bodies (ADOPTED)

At the T3 Matron, `mana_next` reads 4 against a Muxus at MV 6, so the deploy discount prices the
deck's bomb three turns out and buries it — while the line that wins a turn earlier hard-casts Muxus
*next turn* off exactly these sacs. Two counting errors, both user-directed:

1. **The entering tutor source was not counted.** Goblin Matron is itself a Goblin creature and is
   entering right now. The board scan runs while it is still in hand, so it was missed. `606a381`
   made this same correction on the ATTACK side (the swing projection counts the source as entering);
   the MANA side never got it.
2. **Lords were counted as fodder.** Feeding a Chieftain to Skirk de-buffs the whole board, so it is
   fodder only in extremis — now excluded, matching `CanonicalSacVictim`'s expendability ordering.
   They stay in `G` for every other purpose; this is only about what Skirk can eat.

Measured separately, because bundling is what hid the harmful component in round 3:

```
entering source only    held-out searched -2.0   d0 +10.0
lords-not-fodder only   held-out searched  0.0   d0  -2.0
both (ADOPTED)          held-out searched -2.0   d0  +1.0     train: smoke +1, regression -2
```

The entering-source correction carries the searched gain; the lord exclusion is searched-neutral and
cancels its d0 cost. The aggregate is **inside the noise band** — these are adopted for MODEL
CORRECTNESS on a measured non-regression, not on turn-units. The sharper evidence is the diagnostic:
gi206's Muxus climbs **rank 13 → 7**, and the worst miss across the 100 sampled decisions improves
from 13 to 11.

`regret@W=4` is unchanged at 3 turns — Muxus is closer but still outside a four-slot window. That is
the standing target: **get regret@W=4 to zero and the W=6 widening can be reverted.**

### Why the ideal card was good, per game (2026-08-04)

Analysing the three `regret@W=4` misses individually, rather than as an aggregate.

**gi206 — Muxus. GENUINE, now fixed.** At the T3 Matron the board is just Skirk, 2 lands, hand holds
Siege-Gang / Hordemaster / Chieftain. The winning line fetches Muxus, plays a fourth land, sacs two
Goblins to Skirk for 6 mana, and Muxus converts the top of the library onto the battlefield — lethal
from 19 in one swing.

The ranking could not see the mana, and the reason is an inconsistency with the engine:
`skirk_ramp = G - 1`. "Sacrifice a Goblin: Add {R}" is repeatable and needs no tap, and Skirk is
itself a Goblin, so the last activation eats Skirk and **N bodies convert to N mana, not N-1**.
`CollectActions`' own multi-sac burst already models it that way (victim count `V` includes the
source; `k` is capped at `V`), so the ranking was predicting one less mana than the solver would
find. With `-1`, `mana_next` reads 5 against a MV-6 Muxus and the discount prices the bomb two turns
out; with the correct count it reads exactly 6 — castable next turn. **Muxus rank 7 → 2**, so the
full chain on this decision is 13 → 7 → 2.

**gi33 — INSTRUMENT ARTIFACT, not a ranking miss.** The fetched Muxus is never cast; it sits in hand.
The T3 kill comes from Siege-Gang sacrificing Goblins for damage in main 2 (life 6 → 0). What changed
is the PLAN: the shipped line spends both mana on Skirk plus a second Lackey, the forced line casts
only Skirk and keeps mana for the sac kill. Collapsing the tutor axis to width 1 changed the plan's
value — the documented limitation of `MTG_TUTOR_FORCE_RANK`, showing up in the data. Do not chase it
as a ranking defect.

**gi124 — Lackey. GENUINE, STILL OPEN, and the clearest remaining modelling gap.** At the T3 Matron:
Aether Vial out, 2 lands, hand holds Krenko / Chainwhirler. Fetching Lackey deploys it that turn, and
it then connects on **T4 and again on T5**, putting Siege-Gang (MV 5) and Chainwhirler (MV 3) onto
the battlefield free — 8 mana of creatures across two combats.

What the ranking pays it: a 1/1 body worth 100, plus `min(0.60, 0.20 x (stuck_turns - 1)) x
stuck_hand_value`. Only Krenko is out of reach and only by one turn, so `stuck_turns = 2` and the
fraction is **0.20**, applied to Krenko's 300 — Lackey scores ~160 for two free deploys.

The defect is structural: **the Lackey credit is a fraction of ONE stuck card, but Lackey is a
REPEATING engine.** It does not accelerate a single bomb by a turn; it deploys a card every combat
for the rest of the game. The natural representation is the decayed sum of the top-2 hand Goblins it
can realistically drop, not a fraction of the best one. Muxus has a milder version of the same
problem: `etb_reveal_count x 75` prices six revealed cards at 450, when 54% of the deck is Goblin
creatures (33 of 61), so it expects **~3.2 free bodies** whose real average value is several hundred
each. Both are the lord-amplification family — value that arrives as OTHER CARDS.

Self-sac measured: `regret@W=4` **3 → 2**, `@W=6` 2 → 1, top-4 among live decisions 13/16 → 14/16.
Aggregate is searched-NEUTRAL (held-out 0.0, regression -1.0, smoke 0.0) with d0 +6.0 / 12,000. It is
adopted on the engine-inconsistency and the regret metric, NOT on turn-units. The Lackey
repeating-deploy model is the one that should actually move turns.

### Lackey as a repeating engine: MEASURED, REJECTED, and it settles the width question

Built as `MTG_GOBLIN_LACKEY_REPEAT` (kept default-OFF in `DecisionProviders.cpp` with the numbers in
place). Every other enabler channel is a fraction of the ONE best stuck card. Lackey is not that
shape — it puts a Goblin from hand onto the battlefield every combat, and 33 of this deck's 61 cards
are Goblin creatures, so its worth is not bounded by what is stuck right now. gi124's second drop
(Siege-Gang) was not even in hand at the fetch. So the later drops are priced off the mean value of
the Goblin creatures still in the LIBRARY (composition, not order — the same deck knowledge
`TutorCandidates` already uses).

Refined by the user mid-build, and the refinement is a model change rather than a constant: *"that
Chainwhirler can only attack on turn 6 ... unless the 1 damage from it does the trick, this isn't so
amazing. But yes, it is very much not nothing. And really does ensure we don't slip much beyond T6."*
Lackey puts the creature in on COMBAT DAMAGE, so it arrives summoning-sick and its body does nothing
until the following turn. Only its ETB half pays immediately — Chainwhirler's ping, Siege-Gang's
tokens. So the later drop is credited on `value_of` **minus the body**, a floor against slipping a
turn rather than a tempo gain.

**Result: far too weak to matter.** Weights of 25 / 35 / 60 % give *identical* held-out numbers
(searched −2.0, d0 0.0) and gi124's Lackey stays at rank 11. The deck's mean ETB-only Goblin value is
only ~100–150, so even at 60 % the term adds ~80 to a Lackey scoring ~160 — nowhere near the ~3×
needed to climb into a four-slot window.

**Do not simply scale it up.** Excluding Lackey from the candidate list *entirely* costs only **+12.0
turn-units over 16,000 games** (measured in round 2) — that is the total value of Lackey-as-a-fetch
across the whole suite. Over-crediting it to recover one game risks more than the entire upside
available. gi124 is left standing deliberately.

### THE RESOLUTION: W=6 is not a crutch for a bad ranking

With every ranking fix above in place, reverting to W=4 was re-measured:

```
W=4 vs shipped W=6      smoke searched 0.0    regression searched +3.0    overnight searched +14.0
                        d0 unchanged at every tier
```

So the width is still worth ~17 turn-units, *more* than the −6.0 it measured before the ranking was
fixed. Set that against the ranking-side metric, which says W=4 loses only **2 turns per 100 tutor
decisions** (1 more than W=6). The two disagree because they measure different things:

* `regret@W` measures **fetch quality** — is the best card inside the window.
* The aggregate also includes **search branching** — a wider axis gives the search more plans to
  arbitrate, which helps even when no single forced candidate would. gi14 is the proof: T6 at every
  forced rank 1..16, yet T5 at W=12.

**Conclusion: "move the ranking close enough that W=4 captures everything" is not achievable by
ranking work alone**, because a material part of the width's value was never about ranking. The
ranking is now right at #1 in 12 of the 16 decisions that matter and inside the top 4 in 14 of 16;
the residual belongs to plan diversity, not to candidate ordering. W=6 stays, and this line of
investigation is closed.

---

## ROUND 5 (2026-08-04): reading the rank the SEARCH COMMITS TO, and the face-damage term

### The forced-rank table was unsound — do not rebuild it

`MTG_TUTOR_FORCE_RANK` collapses the tutor axis to width 1, which changes the PLAN, not just the
fetch. `s7007 gi371` is the proof: forced rank 6 casts Matron on T4 and forced rank 10 casts it on
T3, **both fetching a Goblin Piledriver**. Same card, two "ranks", two board states whose candidate
lists never coexist. Any per-rank win-turn table built that way mislabels plan changes as ranking
misses, and the first version of this analysis did exactly that.

### The sound instrument: MTG_TUTOR_CHOSEN_RANK

Log the ranking position the search **commits to** at real tutor resolution (gated on
`g_real_resolution`, so rollouts are silent). Run wide and it reads unambiguously: a committed rank
past the shipped width is a real ranking miss with a trustworthy card name; inside it means the extra
width bought plan diversity and no reordering will recover that game. Ranks are over NAMES deduped in
list order, since fetching by name always takes the first matching library card.

```
the 19 games the width decides, at W=12   21 real tutor decisions, 10 committed PAST W=6:
   4x Skirk Prospector   ranks 8, 9, 12, 12      2x Goblin Piledriver  ranks 7, 8
   3x Twinshot Sniper    ranks 11, 12, 13        1x Goblin Lackey      rank 10

BASE RATE, 300 arbitrary games                   80 real tutor decisions, 5 committed PAST W=6:
   4x Twinshot Sniper        1x Goblin Lackey                     -> 6% of decisions
```

Past-window commits are a **6% tail** in general, so the shipped W=6 is right for the overwhelming
majority of decisions. But **Twinshot Sniper is 4 of the 5 unbiased misses** — the deck's single
largest ranking error. It also explains `s4004 gi14`, which no single forced rank could reach: it has
TWO tutor decisions, rank 1 at T3 and rank 11 at T5, and the forced instrument pins one rank for the
whole game by construction.

### Face damage was worth exactly ZERO (ADOPTED, trained)

`value_of` had no `etb_damage_any` / `channel_damage` term at all, and `face_burst` — which does
compute the damage — is consumed only by the exact-lethal override. So Twinshot Sniper's "deals 2
damage to any target" paid nothing unless it happened to be the last 2 points; it scored as a 2-power
body, 200. A plain modelling gap, not a tuning question.

TRAINED rather than guessed (`test/goblins_face_value_train.sh`). Selection used the overnight
searched cases of seeds **s4004+s5005 only**, reading s6006+s7007 afterwards — sweeping the whole
overnight tier and taking its minimum is selection on the holdout, and the regression tier's ~1,325
searched games cannot resolve a delta this size:

```
per     TRAIN(4000g)   VALIDATE(4000g)   d0(12000g)
 80        0.0             0.0             +3        inert -- below a lord's score, nothing reorders
100       -4.0            -2.0             +4        <- ADOPTED (== BODY)
120       -4.0            -4.0            +15
160       -6.0            -3.0            +25        <- train minimum
200       -4.0            -3.0           +133
```

Train's minimum is 160, but 160 vs 100 is 2 turn-units over 4,000 games — noise. Searched is flat
from 100 up; what actually separates the weights is **d0 cost, which climbs steeply**. So take the
smallest weight that captures the effect: it is statistically indistinguishable from the train
optimum, by far the cheapest, and the least extreme claim — 100 is exactly `BODY`, i.e. *a point of
unconditional face damage is worth a point of power on a body*, which deliberately does NOT assert
that burn beats creatures.

`max()`, not sum, over the ETB ping and the Channel mode: they are alternatives (cast the creature,
or discard it from hand for the same damage), so adding them double-counts one card.

Held-out: **zero searched slowdowns or play changes**, every searched case better or equal. One
regression-tier game slower (`s3003 gi156` T4→T5) — CHURN, recovers at 16x budget and at unlimited.
