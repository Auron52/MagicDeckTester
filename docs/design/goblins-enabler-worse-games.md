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

## ROUND 6 (2026-08-04): the last two named misses — Skirk ADOPTED, Piledriver REJECTED

Round 5 left two cards named as past-window commits. Both looked like the *same* defect — a card
priced on its body because a term was missing or miscounted — and both were tested the same way.
They came apart completely, and the pair is a good record of why the measurement is not optional.

### Skirk Prospector: the fodder count was never corrected here (ADOPTED)

Skirk's entire ramp credit lives in `enabler_of` as `min(0.40, 0.13 * max(0, G - 1))`. In all three
of its past-window commits — `s3003 gi194`, `s4004 gi727`, `s5005 gi920` — **G is 0 or 1, so that
fraction is exactly ZERO** and Skirk scores 100: a vanilla 1/1, rank 12 of 14-16. That the *gate*
(`stuck_hand_value > 0`) is not the problem is provable from the same dumps: Goblin Lackey, the other
enabler, collects **+340** from `stuck_hand_value` in those very states.

`G - 1` is wrong in exactly the three ways `skirk_ramp` was already corrected in `236bb13` +
`c13cbac` — and this line got **none** of them, because `skirk_ramp` is gated on `skirk_on` (a
Prospector *already* on the battlefield) and is therefore identically zero in precisely the states
where we are deciding whether to fetch one. The fodder a fetched Prospector will actually eat is the
board's expendable Goblins (`goblin_fodder`, not `G` — lords de-buff the team) **+ the entering tutor
source** (a Goblin creature, the reason this function is running) **+ the Prospector itself** (the
ability is repeatable, needs no tap, and Skirk is a Goblin, so N bodies make N mana, not N-1 — which
is how `CollectActions`' multi-sac burst already counts victims). The old `-1` was doubly wrong: it
subtracted a legal self-sac *and* omitted the two arriving bodies.

Typical corrected count is 1 + 1 + 1 = 3 → fraction 0.39, just under the cap. Skirk scores **321
instead of 100** and moves from rank 12 to rank 5-8.

```
                     train (regression)   HELD-OUT (overnight, 8000g)   d0 (12000g)
Skirk fetch-fodder         -2.0                    -3.0                     0.0
                                            4 games better / 1 worse
```

Same sign on both tiers — it replicates, which is the bar.

### Goblin Piledriver: the crowd count is NOT the axis (REJECTED)

The argument was the better-looking of the two, and it was wrong. `G` is read at the instant of the
FETCH, when the board is smallest, but a fetched Piledriver enters **summoning-sick** and cannot
attack until the following turn — by which point the entering tutor source has landed, another deploy
has landed, and every sick body is ready. In both of its past-window commits (`s3003 gi290`,
`s7007 gi371`) G is 1 against a `buff_targets` of 4, so it scores 190 and sits at rank 9-10.

Widening the count does exactly what it was designed to do — Piledriver lifts to rank 3-5 — and it
does not pay:

```
crowd                 per    HELD-OUT    games
G (shipped)            45      0.0       --
buff_targets           45     -1.0       5 better / 4 WORSE
G + entering + 1       45     +4.0       0 better / 4 WORSE
buff_targets           25     +2.0       0 better / 2 WORSE
```

Three things kill it:

1. **The one non-positive arm is noise.** Split by seed, `buff_targets`/45 is **+1 on s4004+s5005 and
   −2 on s6006+s7007** — opposite signs on the two halves. Its train result (−4.0 on the regression
   tier) did not replicate; this is the ~1,325-searched-game resolution limit biting again, exactly as
   it did once before in this document.
2. **It is not additive with the Skirk fix.** Skirk alone measures −3.0 held-out; Skirk + Piledriver
   measures **−3.0**. It contributes nothing on top, while adding 3 more games worse.
3. **Magnitude, not count, is what the games like.** The two arms that hold the product near shipped —
   to separate "wrong count" from "wrong size" — are *strictly worse with zero games improved*. So the
   `45.0` is calibrated against the undercounted crowd, and no redistribution of that product helps.

`d0` was **exactly 0.0 in every Piledriver arm**: across 12,000 greedy games it never once changed the
top pick, so all of this only ever moved the card around inside the tail.

Same shape of negative result as `MTG_GOBLIN_CUT_WIDTH` — a well-motivated recount that turns out not
to be the axis the states differ on. Lever kept (`MTG_GOBLIN_PILEDRIVER_CROWD`, `_PER`), default 0.

### The transferable lesson

Two cards, identical symptom (*scored as a bare body, committed to past the window*), identical fix
shape (*count the crowd/fodder that exists when the card ACTS*), opposite verdicts. The symptom does
not predict the outcome; only the held-out measurement does. Note also which evidence was decisive in
each case — for Skirk, a term that was **provably, arithmetically zero** where a sibling enabler
collected +340; for Piledriver, nothing was zero, the term was merely *small*. A missing term is worth
fixing; a small term is a tuning question, and tuning questions on this deck mostly come back noise.

### Follow-on: the ramp RATE, trained (`test/goblins_skirk_rate_train.sh`)

The fodder fix moves Skirk from 100 to 321, but the chosen-rank instrument still shows it committed at
rank 7-9 — better, not inside the window. The reason is *not* the 0.40 cap. At `s3003 gi194` the credit
is 0.26 = `0.13 x 2`: the single board Goblin there is a **lord**, so `goblin_fodder` is 0 and the only
fodder is the entering Matron plus the Prospector itself. Raising the cap would change nothing; the
**per-body rate** is the lever. (The cap is raised to 0.60 alongside purely so a higher rate cannot
silently clip on high-fodder boards — it is inert at the old rate, 13/60 reproducing 13/40 exactly.)

```
rate   TRAIN(4000g)   VALIDATE(4000g)   d0(12000g)
 13       -4.0            +1.0             0.0      the fodder fix alone
 18       -6.0            +1.0             0.0      <- ADOPTED
 22       -6.0            +1.0             0.0
 26       -6.0            +1.0             0.0
 32       -6.0            +1.0             0.0
```

Flat from 18 up — the ordering **saturates**. At gi194, `0.18 x 2 = 0.36` scores Skirk 406, just over
Goblin Chainwhirler's 400, and that single crossing is the entire gain; beyond it Skirk passes only
cards whose position does not change an outcome. So take the smallest rate that captures the effect,
the same rule the face-damage weight was chosen by. `d0` is 0.0 at every rate: the greedy top pick
never changes, so this is purely window membership. The `+1.0` on validate is one game, `s7007 gi588`
(T4→T5), and it is **churn** — recovers at 16x budget and at unlimited.

### A caution about re-reading the miss set after a ranking change

After adopting the fodder fix the instrument reported *more* past-window commits (11, up from 10),
which looks like a regression and is not one. `MTG_TUTOR_CHOSEN_RANK` forces `MTG_TUTOR_WIDTH=12`, so
the arm being measured plays a different game from the shipped W=6 engine; change the ranking and both
the ordering **and the W=12 trajectory** move, so the before/after decisions are not the same decisions
re-scored. The instrument is sound for "in the games width decides, what rank does the search commit
to" — it is not a before/after quality metric across an engine change. Turn-units are the ground truth.

## ROUND 7 (2026-08-04): Piledriver re-derived from the user's model — ADOPTED

Round 6 rejected the Piledriver crowd-count fix on a clean held-out measurement. The rejection was
correct; the *diagnosis* was not. The user supplied the model that was missing:

> "similar to a lord except it does 2 per other goblin and only realized when it attacks ... 2 per
> other attacking goblin plus the 1 base power, whereas the lord does 1 per other attacking goblin +
> 1 base power ... pretty close to Rundvelt Hordemaster unless the lord effect can give lethal this
> turn. Piledriver usually wins if there is haste or there are multiple turns it can attack." And,
> decisively: **"it's important that the Piledriver is not strictly better."**

### The two halves of the comparison were never on one scale

As swing damage added, with N = other attacking Goblins: **lord = N·1 + 1, Piledriver = N·2 + 1.**
The lord side was already priced exactly that way (`power_bonus * buff_targets * BODY` + body).
Piledriver was `G * 2 * 45` — a different crowd (board-only, read at the fetch, when the board is
smallest) *and* under half the per-point rate. At `buff_targets` 4 that is **190 against the lord's
500, where the true ratio is 9/5**.

This also explains Round 6's negative result rather than contradicting it: every variant there was
swept at the old 45/point, and `d0` was **exactly 0.0 in all of them** — across 12,000 greedy games
Piledriver never once changed the top pick, because 190→460 still lost to every lord. The count was
never the axis. The **scale** was, and nothing tested moved it far enough to matter.

### The conditionality is derived, then capped — and the cap is the user's call, not a tuned knob

Over T remaining attack steps a lord realizes `T(N+1)` while Piledriver, which cannot attack the turn
it lands, realizes `(T-1)(2N+1)`. So the pump scales by `(T-1)/T`, and T is estimated from board
damage against opponent life — "close to lethal" and "plenty of turns left" being one quantity read
two ways. But the derived factor alone reaches 0.83 by T=6, making Piledriver ~1.67x a lord, and the
held-out cost is **monotone in how far past parity it goes**:

```
realization factor          HELD-OUT searched   d0        crowd
0.50  (== lord parity)            -3.0         -3.0      board + source + 1   <- ADOPTED
0.65                               0.0        +21.0      board + source + 1
(T-1)/T, uncapped ~0.83           +2.0        -52.0 *    board + source + 1
(T-1)/T, uncapped                 +6.0        +55.0      buff_targets
* not a win: one seed carries it (s4004 -81) while the other three d0 seeds are +13/+8/+8.
```

So `min(0.50, (T-1)/T)`, which keeps **both** halves: T=1 gives exactly 0.0 (*"if close to lethal and
no haste the lord is better"*), everything from T=2 up sits at parity, and haste removes the lost
swing entirely for the full 2x. The user's intuition beat the derivation — the 0.50 anchor is
*measured*, and "not strictly better" was empirically right.

**Crowd matters too, and `buff_targets` is wrong here.** A lord's buff waits around for hand Goblins
to land; a pump that fires only on the swing is bounded by what is deployed by then. `buff_targets`
ranked Piledriver **first on a T1 empty board** off three undeployed hand cards — the same over-reach
as the rejected variant 3 — and measured +6.0 held-out. Board + entering source + one deploy is the
honest count.

Shipped: smoke -1.0, regression -3.0, **held-out overnight -3.0 searched and -3.0 d0**.

### Rundvelt Hordemaster x Skirk Prospector: real, and inert here (lever kept, default off)

> "Rundvelt Hordemaster is better if we are sacrificing creatures to Skirk Prospector — the extra
> effect comes into play ... and the skirk effect can be pretty huge."

True, and `value_of` had no `dies_trigger_impulse_exile` term at all, so it was worth zero. Both
directions were implemented (fetch the Hordemaster into a Skirk board; fetch the Skirk into a
Hordemaster board — the commoner state, since Hordemaster ranks 2-4 and is usually the one already
down) and priced off the sacs actually expected, each self-gating on its partner.

It measures **exactly nothing**: searched 0.0 alone, and with the Piledriver rescale in place the
arms with and without it are byte-identical. Not a broken gate — a Skirk is live at ~16% of tutor
decisions (`skirk_ramp` up to 26). The pairing simply never decides a fetch: Hordemaster is already
ranked high enough that more value does not move it, and where it did move something (d0 +1.0 alone)
it moved it wrong. `MTG_GOBLIN_IMPULSE_PER`, default 0.

## ROUND 8 (2026-08-04): Twinshot Sniper — the face weight re-trained on the CURRENT ranking

Round 7 left Twinshot Sniper as 3 of the 4 remaining past-window commits. Two things about how this
was diagnosed are worth keeping.

**The obvious hypothesis was wrong.** All three misses are T5 decisions, so "late, near lethal, and a
flat face weight cannot see it" was the natural guess — and a near-lethal ramp would have been built
on it. The states refute it outright: `opp_life` 15 / 19 / 14 against swings of 5 / 3 / 3, i.e. 3-6
swings still to come. Nothing near lethal. Checking the states before building the fix cost one dump
and saved a whole mechanism.

**What it actually was: a calibration that had gone stale.** Twinshot scores exactly 400 in all three
(200 body + 2 x 100 face) and the margins are tiny — in `s4004 gi828` it is *tied on 400* with Goblin
Chainwhirler and loses only the stable-sort tie-break; in `s6006 gi496` it needs +61 to clear Krenko;
in `s4004 gi14` it is already inside the window. The weight had been trained in round 5, **before**
the Skirk and Piledriver fixes reshuffled everything around it, so it was tuned against a ranking that
no longer existed.

Re-swept with the same train/validate split against the current ranking:

```
per     TRAIN(4000g)   VALIDATE(4000g)   d0(12000g)
100        0.0             0.0              0.0     the shipped weight (baseline)
120       -2.0             0.0            +10.0
140       -2.0            -2.0            +10.0
160       -4.0            -2.0            +17.0     <- ADOPTED, interior optimum
180        0.0            -2.0            +19.0
200       +2.0            -2.0           +123.0
```

Train has a genuine interior minimum at 160 with 140 and 180 both worse on either side, so it is an
optimum rather than a sweep boundary. 160 was **also** the train minimum in round 5's sweep against
the old ranking — a stable optimum across two different rankings, which is the best evidence available
that it is real and not selection noise. (Round 5 nonetheless shipped 100, correctly at the time: the
searched curve was flat from 100 up *then*, so the smallest weight won on the tie-break. It is no
longer flat.)

Held-out: **searched -6.0 with 6 games better and ZERO worse** — the cleanest searched result of this
sequence. It is not free: **d0 +17.0, 1 game better and 17 worse**, by far the largest d0 cost adopted
here (Skirk 0.0, Piledriver -3.0). Taken on the standing priority that searched is what ships and d0 is
a diagnostic, but it is a genuine trade and is recorded as one.

### Where the width gap stands after rounds 6-8

```
                       W=12 better   W=12 worse   past-window commits
start of the sequence       16            3          10 of 21  (48%)
after Skirk + Piledriver     9            3           4 of 13  (31%)
after the face weight        6            4           3 of 11  (27%)
```

Remaining misses are one each of Twinshot Sniper, Goblin Lackey and Rundvelt Hordemaster — no card
appears twice any more, which is the signal that the systematic mispricings are done and what is left
is per-state noise. Of the 6 games still better at W=12, only 3 contain a ranking miss at all; the rest
commit *inside* W=6 and are plan diversity, which no reordering recovers.

Note the "W=12 worse" column grew to 4. That is not a regression in shipped play — it counts games
where widening the axis HURTS, i.e. where the shipped W=6 ranking now beats the wide search
(`s1001 gi113` T4 vs T5). It is a fact about search instability, not about the ranking.

## ROUND 9 (2026-08-05): Goblin Chainwhirler — the evaluation is right, the engine already agrees

> "Is there any case we really want Chainwhirler? It seems worse than Twinshot in almost every case
> and worse than a lord in the others ... if you need a good 3-drop threat you want a lord. If you
> want the immediate damage Twinshot is better ... what makes it playable in a real game is the
> ability to ping 1 toughness creatures (i.e. like dorks or aggressive 1-drops). So Chainwhirler is
> pretty bad in this goldfish environment."

The card evaluation is correct, and the card data already concedes the key point — Chainwhirler's ETB
hits "each creature and each planeswalker your opponents control and each opponent", and against a
goldfish there are no opponent creatures, so the half that justifies the card is inert (`cards.json`
notes it "only matters vs opponent spawn tokens"). What is left is **1 damage to the face against
Twinshot Sniper's 2**, on `{R}{R}{R}` where Twinshot can be **channelled from hand for `{1}{R}`**.
Both are 1-ofs.

Implemented as a general **dominated-burn** rule rather than by card name: a tutor takes exactly ONE
card, so a burn payoff only earns its credit when nothing strictly better is still fetchable. It
self-restores — once Twinshot leaves the library Chainwhirler is the best burn again and gets full
credit, which is precisely "only taken if Twinshot is gone and we need the 1 damage".

It works exactly as designed and is **not worth shipping**:

```
mode                              regression searched   HELD-OUT searched   d0(12000g)
0  off                                   0.0                  0.0              0.0
1  drop the redundant face credit        0.0                  0.0            +88.0
2  drop from consideration entirely      0.0                  0.0            +88.0
```

Chainwhirler drops 460 -> 300, rank 5 -> rank 7, out of the window — and **zero searched games change,
across 8,000 held-out plus 1,100 regression**. Modes 1 and 2 are indistinguishable, which is itself
the finding: once the card leaves the window, ranking it dead last buys nothing more. The search never
wanted it, *and* freeing its window slot helps nobody else either.

The only measurable effect is a cost, and it is a **greedy artifact**: d0 +88.0 is one game
(`s8008 gi1882`, T8 -> unwon) against two d0 games improved, and at depth 3 and depth 5 both arms win
on **T5**. d0 takes `cands[0]` with no search, so it is the only policy that can be hurt by demoting a
card the search would have rejected anyway.

**The transferable point:** a mis-ranked card inside the window is not automatically a bug. The search
evaluates what the ranking offers it and discards the bad ones; the ranking only has to get a card
*into* the window, and only matters when a bad card crowds out a good one. Round 8 fixed a real case
of that (Twinshot losing a tie-break). This is the opposite case — a genuinely weak card that costs
nothing by being present. Worth knowing before "obviously bad card ranked too high" is treated as a
defect again. Lever kept at `MTG_GOBLIN_DOMINATED_BURN`, default 0.

## ROUND 10 (2026-08-05): crowding — "the only task of the heuristic is to figure out 5 to cut"

The framing shift that drove this round (user):

> "With W=6 we should almost always be able to include the best goblins for each role ... at 11
> goblins [that] is a complete list of cards you might want. So then, the only task of the heuristic
> is to figure out 5 to cut ... maybe than a ranking what we need is to group these by role and drop
> the worse one on the current board ... keep cards that are significantly different in utility and
> drop ones that are similar, but worse in a situation."

### First: the slot IS worth something — an earlier conclusion here was wrong

Round 9 concluded that a weak card inside the window costs nothing, on the evidence that demoting
Goblin Chainwhirler changed zero searched games. That inference was wrong, and the user pushed back on
it. Measuring the width directly:

```
W=4  +26.0      W=5  +11.0      W=6  0.0 (shipped)     held-out searched
```

**The marginal slot is worth ~11 turn-units.** Chainwhirler measured 0 only because the rank-7 card
that replaced it was equally unwanted in those particular states — an absence of evidence, not a free
slot. (The W=4/W=5 arms in that round were also uninformative by construction: Chainwhirler sits at
index 5, so at W<=5 it is already excluded and both arms are trivially identical.)

Search *cost* is not the mechanism, though: rollout calls are within 0.3% with and without the rule
(2.984M vs 2.993M), because the window is a fixed six slots and demoting one card just lets another
in. The cost is purely opportunity cost on a scarce slot.

### Strict dominance: Goblin Chieftain vs Goblin King (ADOPTED, the big win)

Applying "similar but worse" to the roster finds a **provable** case, and it is not a tail card:

| | cost | body | effect |
|---|---|---|---|
| Goblin Chieftain | `{1}{R}{R}` | 2/2 | +1/+1 to Goblins, **grants haste**, has haste |
| Goblin King | `{1}{R}{R}` | 2/2 | +1/+1 to Goblins, mountainwalk |

Identical cost, identical body, identical buff. King's sole differentiator is mountainwalk, which
`cards.json` itself flags "INERT in goldfishing". King is a **2-of that ranks 1st or 2nd in nearly
every dump**, so the pair was burning two of six slots to do one card's job — crowding at the TOP of
the window, not the bottom.

Implemented by rule, not name: B is dropped when some other fetchable A costs no more, has a body no
smaller, and is >= on every goldfish-relevant capability with a strict edge somewhere. Capabilities are
compared as a vector, so it survives a decklist change, and it is computed over the LIBRARY — once the
last Chieftain is drawn, King is no longer dominated and comes back ("if we have removed all of the
Goblin Chieftains it might make sense in some cases").

### Role cut: Hordemaster vs Piledriver (ADOPTED) — and the tie-break axis matters

> "Rundvelt Hordemaster and Piledriver. We should be able to work out which is better on the current
> board and drop the other ... an early or hasty piledriver will inevitably be better. The lord wins
> when the damage add from the turn it is played will be significant."

That rule is already in the round-7 scores, which is why this is a cut and not new judgement:
Piledriver is `2*crowd*BODY*realized` with realized = 1.0 under haste, 0.0 when the game ends this
turn, and 0.5 otherwise — algebraically identical to a +1/+1 lord's `1*crowd*BODY`. They tie at parity
and every condition the user named breaks the tie the way they described.

**The first implementation failed, and the failure was diagnostic** (user: "if our role-cut fails that
is an indicator we did it wrong. We should evaluate the worse cases"). It cut by TOTAL score, which
sent `s3003 gi290` from T5 to T6. At that T4 a haste lord is out, so Piledriver should win and does on
board value (700 vs 500) — but Hordemaster's total is 800, because it collects +300 of enabler/lord-
amplification credit, which measures how much sooner a stuck bomb in HAND arrives. Letting that settle
a board duel cut the right card. Comparing on `value_of` alone fixes it.

### The enabler cut fails, in every form tested (NOT adopted)

Cutting Lackey vs Skirk measured **-3.0** held-out against **-9.0** for not cutting them. Fixing the
tie-break to use the enabler credit (the axis that defines the role) did not help, and neither did
"sometimes we can drop both" (drop the whole role when its best member contributes nothing). The
reason is measurable: **in 61% of sampled states BOTH enablers score enable=0**, because nothing in
hand is stuck — so the duel has no signal and the cut discards a card by coin flip.

### Result

```
arm                                        regression   HELD-OUT searched   d0
off                                            0.0            0.0            0.0
dominance (King out)                           0.0           -7.0          -24.0
dominance + role cut, total-score tie-break    +1.0           -6.0          -24.0
dominance + role cut, board-value tie-break    0.0            -9.0          -24.0   <- SHIPPED
  ... + enabler cut (role_cut=2)               +2.0           -3.0          -24.0
  ... + drop-both refinement (role_cut=3)      +2.0           -3.0          -24.0
  ... + Chainwhirler out                       0.0            -9.0          +64.0
```

**-9.0 held-out searched with 9 games better and ZERO worse**, plus d0 -24.0 — improving on both
tiers at once, which nothing else this session managed.

Chainwhirler was retested on top of all the other prunes, since a single prune might not be enough to
let a different card reach the window ("it might need multiple prunes"). It still adds exactly nothing
(-9.0 either way) and still costs the one d0 game. Three prunes deep, its slot buys nothing.

### Postscript: the three "unresolvable" games were never ranking failures

`s2002 gi299`, `s4004 gi483` and `s7007 gi624` were tracked from round 6 onward as games where W=12
beats the shipped width and which resisted every ranking change. They are **wall-clock budget churn**,
not width or ranking effects. Re-run directly across widths and budgets:

```
             W=6                              W=12
gi483        bud0 T4  bud20 T5  bud80 T4      bud0 T4  bud20 T5  bud80 T4   <- identical
gi299        T4 at every budget               T4 at every budget            <- width-independent
gi624        T5 at every budget               T5, except a single T6 at bud20
```

The harness's searched cases run under a `budget_ms`, which is wall-clock and therefore load-sensitive
(the same reason `MTG_ROLLOUT_STATS` exists as the deterministic cost instrument). So a game can be
recorded T4 in ground truth and T5 in a later run without anything in the engine changing. Treat a
"W=12 worse" entry as churn until it reproduces at unlimited budget.

With those retired, the width gap is fully closed: W=12 wins 2 games and loses **no real ones**.

### Mogg War Marshal: not the reason gi483 moved (checked, left alone)

The chosen-rank instrument listed a Mogg War Marshal commit at rank 11 in `gi483`, which looked like a
ranking miss. It is incidental: the shipped W=6 fetches **Goblin Piledriver at rank 1** on T4 and
reaches T4, the W=12 arm takes a different line that happens to fetch Mogg on T3, and both land on the
same turn at every budget. The instrument reports whatever the wide arm committed to, and that game is
decided by the clock.

There is a plausible undervaluation worth recording without acting on it. Mogg scores 190 = 1 power x
BODY + 1 token x 90, which prices the ETB token *below* a real body and **ignores the death token
entirely** — even though letting echo lapse sacrifices it and still nets that token (as `cards.json`
notes). For `{1}{R}` that is two Goblin bodies immediately and three over its life, in a deck where
bodies are the currency for every lord, for Piledriver's +2-per-other-attacker and for Skirk's fodder.

Not changed, deliberately: the search never reaches for it in any surviving miss, it is absent from
the user's list of cards worth fetching, and inflating a mediocre card to crowd the window is exactly
the bug round 10 removed. Revisit only if a measured miss points at it.

### The two survivors are real, width-only, and do NOT recover with depth or budget

Applying the churn test to the two games W=12 still wins, they behave in exactly the opposite way to
the three retired ones -- perfectly stable, and responsive only to width:

```
                 W=6                       W=12
s3003 gi101      d3/d5/d6 = T6             d3/d5/d6 = T5      (budget 0/20/80/320 identical)
s4004 gi124      d3/d5/d6 = T6             d3/d5/d6 = T5      (budget 0/20/80/320 identical)
```

Only ONE is a ranking miss. `s4004 gi124` commits to Goblin Lackey at rank 8, outside the window --
a genuine miss, and the last one left.

`s3003 gi101` is NOT a ranking miss, and the earlier claim here that it was therefore "unrecoverable"
was wrong (user: "if it isn't a search miss it should be recoverable?"). What actually differs is
*when the Matron is cast*:

```
W=6   casts Matron T3 -> Goblin Chieftain      rank 2/13   -> T6
W=12  casts Matron T4 -> Siege-Gang Commander  rank 3/13   -> T5
```

Both fetches are INSIDE the shipped W=6, so the ranking never excludes the wanted card. The width is
changing the search's PLAN -- hold the Matron a turn for the bomb rather than cast it now for the lord
-- and depth 5 and 6 do not find that line either. So it is recoverable in principle, but through the
search's plan evaluation, not by reordering tutor candidates.

The plausible mechanism, UNCONFIRMED: the T4 line's value depends on the candidate ranking at that T4
board state, which differs from T3's, so a 6-wide window can leave the good T4 branch unevaluated even
though the eventual pick ranks 3rd there. Hard to confirm cheaply -- the W=6 arm never reaches a T4
Matron state to inspect.

## ROUND 11 (2026-08-05): s3003 gi101 explained — the discount VETOES a bomb from the search

`s3003 gi101` was the last unexplained width gap: W=6 wins T6, W=12 wins T5, stable at every depth
(3/5/6) and budget. The full causal chain, and it is not what it first looked like.

### Why the Siege-Gang line wins

```
T4  cast Goblin Matron, fetch Siege-Gang Commander TO HAND, attack
T5  Goblin Lackey connects -> PUTS Siege-Gang onto the battlefield FREE, attack, win
```

Siege-Gang was never going to be cast. It is a **Lackey target** — the combat-damage trigger bypasses
`{3}{R}{R}` entirely, and it arrives with three 1/1 tokens plus a `{1}{R}`, sac-a-Goblin: 2 damage
outlet to finish. That is the whole reason holding the Matron a turn beats casting it on T3 for a lord.

### The model is NOT mispricing it — the SEARCH cannot see it

`turns_to_deploy` already handles the Lackey path, and prices the card correctly when a Lackey is out:

```
lackey_persist=0   Siege-Gang  score= 65.8   value=510.0  x disc=0.129 (t=3)   -> rank 10
lackey_persist=1   Siege-Gang  score=433.5   value=510.0  x disc=0.850 (t=1)   -> rank  3
```

The failure is at T3. When the search asks "should I hold the Matron and cast it on T4 instead", it
evaluates projected T4 states in which the Lackey is not yet on the battlefield — and there Siege-Gang
is rank 10, outside a 6-wide window. The branch is unreachable, the "hold" line is never costed, and
the search casts on T3. The width threshold is exactly **W=9**, the rank Siege-Gang occupies in those
projections.

**DISCOUNT-AS-VETO** is the general defect: the deploy discount is a static pre-scorer estimate, but
the window turns a low rank into an EXCLUSION, so the estimate silently overrules the forward
simulation that would have judged the line properly. As a ranking signal the discount is sound; as a
veto it inverts the intended relationship between heuristic and search.

The blind spot is the normal case, not a fluke. Over 35,066 sampled tutor states, **61% have a
higher-RAW-value card sitting outside the window**, overwhelmingly the deck's two bombs:

```
12,568x  Muxus, Goblin Grandee      933x  Twinshot Sniper
 5,302x  Siege-Gang Commander       750x  Goblin Chainwhirler
```

### NOT a clairvoyance artifact (checked, because Matron shuffles)

Goblin Matron has `tutor_shuffle_after`, so a tutor line's value depends on a reshuffle the search
simulates — exactly the situation `MTG_SHUFFLE_SALT_SEARCH` exists to police. Decoupled, so the search
plans against a reshuffle the real game will not deal, the edge **survives at every salt tried**:

```
search salt   W=6    W=6+reserve2   W=12
coupled       T6         T5          T5
salts 1..5    T6 (all)   T5 (all)    T5 (all)
```

A decision that only won by foreseeing a specific reshuffle collapses there. This one does not.

### The fix: reserve window slots for the best RAW-value candidates (ADOPTED, narrow)

Two slots, not one — the highest raw value is Muxus (850) but the card that wins is Siege-Gang (510),
so rescuing only the top one grabs the wrong bomb and gi101 stays T6.

```
                regression      HELD-OUT (8000 searched)    d0 (12000)
reserve=1          0.0                   0.0                  0.0     inert
reserve=2         -2.0                   0.0                  0.0     <- adopted
```

Held-out is **exactly** zero — not one file changed. The -2.0 on regression IS gi101 (counted at d3 and
d5), the game this was built for, so it is not independent confirmation. Adopted as a fix for a
diagnosed mechanism at zero measured cost, **not** as a measured win: it currently moves one game in
20,000. The honest reading of held-out zero is that the discount is usually RIGHT — forcing the bomb
onto the axis is harmless because the search rejects it — but it was wrong here and the search never
got the chance to say so.

Open question worth keeping: the sharper fix may be to the projection rather than the window, since
the model already ranks the bomb correctly once the Lackey is visible. Reserving slots compensates for
the projection instead of correcting it.

## ROUND 12 (2026-08-05): gi101 traced end to end — the deciding move is the T2 LAND DROP

Round 11 left two things wrong. The deciding state was recorded as "unpinned", and the reserve's
mechanism was described as "keep the top-N raw-value cards", which is **not what the code does**. Both
are now traced.

### The line, in full

Three Tree City taps for `{C}` in base mode (`{2},{T}` is the Goblin-scaled red mode). So *when* it is
played decides which spells are castable:

```
W<=8   T2 Three Tree City -> T3 lands are {R}{R}{C}: Chainwhirler ({R}{R}{R}) is UNCASTABLE
                          -> T3 cast Matron, fetch Goblin Chieftain -> T5 Chieftain -> T6
W>=9   T2 Mountain        -> T3 lands are {R}{R}{R}: cast Goblin Chainwhirler
                          -> T4 play Three Tree City, cast Matron (fetch Siege-Gang) + Goblin Lackey
                          -> T5 Lackey connects, PUTS Siege-Gang in free with three tokens;
                             Three Tree City now taps {2},{T} for 6 Goblins' worth of {R}, paying
                             FOUR {1}{R} sac activations = exactly 8 to close                 -> T5
```

The fetch is not the decision. The decision is the **T2 land drop**, and to value it the search has to
see the T4 Siege-Gang fetch two turns ahead.

### Why W=9 exactly — and WHICH state binds (corrected)

The first draft of this section blamed a T3 state where Siege-Gang happens to sit at rank 8. That was
a coincidence. The lookahead **does** re-rank at every projected turn, using the heuristic for that
particular state, so the deciding state had to be pinned rather than guessed. Two diagnostic gates on
the reserve do it — `MTG_GOBLIN_RESERVE_TURN` (apply only at turn N) and `MTG_GOBLIN_RESERVE_NEXT`
(apply only when `mana_next` is N):

```
reserve at turn=  1   2   3   4   5   6        reserve at T4, mana_next=  2   3   4   5
gi101 (d3/d5)    T6  T6  T6  T5  T6  T6        gi101 (d3/d5)             T6  T6  T5  T6
```

It binds at **turn 4, mana_next 4** and nowhere else. T4 is ranked *twice*, because the plan
enumerator evaluates the Matron both before and after the turn's land drop:

```
T4 G=1 opp=16 untapped=3 next=4   (3 lands, land STILL IN HAND)   Siege-Gang rank 8   OUTSIDE W=6
T4 G=1 opp=16 untapped=4 next=5   (4 lands, land played)          Siege-Gang rank 4   inside
```

`mana_next = CountLandsInPlay + 1` assumes this turn's land drop is already spent. On the pre-land
copy it is one short, so a `{3}{R}{R}` bomb prices `t=2` (disc 0.287) instead of `t=1` (disc 0.637) —
and the enumerator binds on that copy. Rank 8 there is also exactly why the width threshold is W=9;
`MTG_TUTOR_WIDTH` 7 and 8 add only Krenko and Mogg War Marshal and change nothing:

```
W=6 T6   W=7 T6   W=8 T6   W=9 T5   W=10..13 T5      (d3 and d5, budgets 0/20/80/320/2000)
```

Stable at every depth and budget, so it is causal rather than tie-break churn.

### The sharper fix is right about the diagnosis and measurably WRONG in practice

Round 11 left an open question: "the sharper fix may be to the projection rather than the window."
It is now implemented (`MTG_GOBLIN_PENDING_LAND`: credit the pending land drop in `mana_next` when the
drop is unused and a land is in hand). It **does** recover gi101 on its own with the reserve off — the
diagnosis is correct. It also loses games everywhere else:

```
                                       gi101   HELD-OUT (8000 searched)   d0 (12000)
reserve=2, PENDING_LAND=0 (SHIPPED)      T5           0.0  (baseline)        0.0
reserve=0, PENDING_LAND=0                T6           0.0                    0.0
reserve=0, PENDING_LAND=1                T5          +7.0  (0 better/7 worse) 0.0
reserve=2, PENDING_LAND=1                T5          +9.0  (0 better/9 worse) 0.0
```

Never better, 7-9 games worse. The likely reason is calibration: the discount curve (0.85 at `t=1`,
then 0.45/step) was fitted **against** this pessimistic `mana_next`, so the bias is already priced in.
Crediting the pending drop moves every expensive bomb from `t=2` to `t=1` at every pre-land-drop state
simultaneously, re-tuning all of the thresholds the curve was fitted to. Making it pay would mean
refitting the curve with it, not dropping it in. Rejected, kept default-off with its number.

The general lesson, twice over in one round: **a locally-correct fix to one input of a calibrated
heuristic can be globally worse, because the calibration absorbed the error.**

### The reserve does not do what its name says — and the bug is load-bearing

`cands.insert(cands.begin() + (W - 1 - k), rescued)` shifts the previous rescue from `W-1` to `W`, i.e.
straight back out of the window. So `reserve=N` rescues raw-value ranks **2..N and drops rank 1**. On
gi101's T3 state, `reserve=2` holds Siege-Gang and has evicted the Muxus it rescued first.

Implementing the intended semantics (evict the weakest *survivor*, accumulate rescues) behind
`MTG_GOBLIN_VALUE_RESERVE_FIX=1` and measuring on the held-out overnight tier:

```
                                             gi101   HELD-OUT (8000 searched)   d0 (12000)
reserve=1   rescue Muxus only                 T6            0.0                    0.0
reserve=2   rescue rank 2 only  (SHIPPED)     T5            0.0                    0.0
reserve=2 + FIX=1  keep ranks 1 AND 2         T5          +20.0  (0 better/20 worse)  0.0
```

Dropping rank 1 is the *better* rule, and consistently so — 20 games worse, none better. Rank 1 is
Muxus (raw 850, MV 6), the single card whose "genuinely stuck" discount is most often RIGHT, so
forcing it onto the axis costs a real window slot and buys nothing. The shipped default is kept
byte-identical; `FIX` stays default-off with its number, per the rejected-lever convention.

Naming is now the only thing wrong with it, and the comment says so at the definition.

## ROUND 13 (2026-08-05): the root defect, located and fixed — and the fix is measurably worse

Round 12 rejected `MTG_GOBLIN_PENDING_LAND` (a provider-side patch to `mana_next`) and guessed the
loss was calibration. Both halves of that were worth testing properly, because the underlying issue is
a genuine engine defect, not a heuristic taste question.

### The defect (TurnSolver, affects every deck with a tutor)

`EnumeratePlansWithLand` builds a post-land state per land candidate and enumerates the base plans on
it, so **the base plan's tutor target is ranked correctly**. The post-dedup tutor axis fan-out — which
supplies the *alternatives the search actually chooses among* — then calls
`TutorCandidates(state, ...)` on the **pre-land turn-start `state`**, and caches the result keyed by
card name alone, so one pre-land ranking is reused for every plan regardless of which land it plays.

Two consequences, both real:

1. The provider feeds `mana_now` / `mana_next` into a deploy discount, so a turn whose land is still in
   hand prices every expensive card one turn further away than the plan actually leaves it — and a
   card pushed past the axis width is **excluded**, not merely ranked low. This is exactly gi101:
   Siege-Gang is rank 8 pre-land (`mana_next=4`, `{3}{R}{R}` reads `t=2`) and rank 4 post-land
   (`mana_next=5`, `t=1`).
2. The two halves of one plan set disagree, and the pre-land list's own rank-0 card is silently
   dropped, because the fan-out loop starts at `c = 1` to skip what it assumes is the base target.

Fixed behind `MTG_TUTOR_AXIS_POSTLAND`: rank each plan's axis against that plan's post-land state,
cache keyed by the land played. It recovers gi101 **with the value reserve off** — the diagnosis is
right, and this is the root cause rather than the window symptom.

### It loses, and refitting the curve does not save it

Held-out overnight, the two decks that can move (verified: no other deck changed a single game across
21,000 smoke + regression games, and the code path requires a tutor in the plan):

```
goblins   postland=1     +18.0     0 better / 18 worse
hinata    postland=1      ~-3      (see below)
```

Hinata's raw number is **-275, and it is a GT artifact worth recording**: three games GT stored as
UNWON actually win. `gi90` is a genuine 9 -> 8 (stable at budgets 20/80/320/1280); `gi158` is pure
churn that converges to 6/6 by budget 320. Under batch load at 20 ms the baseline simply failed to
finish them, and the 99-point loss penalty turns a real -2 into -275. **Any single unwon/won flip
dominates this metric, so always re-run the flipped games standalone across budgets before believing
an aggregate.**

Then the calibration hypothesis, tested directly — sweep the `t=1` discount constant with the fix ON
(`MTG_GOBLIN_DISC_T1`, train `s4004+s5005`, validate `s6006+s7007`):

```
DISC_T1     85    75    65    55    45    38
train      +10    +6    +6    +6    +6    +8
validate    +8    +6    +4    +4    +4    +4
better       0     0     0     0     0     0      <-- every arm, every seed
```

It saturates at +10 and never approaches baseline, and **not one arm produces a single better game
anywhere**. So this is not a mis-tuned constant absorbing a bias.

### What that actually means

The pessimistic pre-land view is functioning as a **tempo prior**, and in goldfishing that prior is
right: "the card I can deploy *now*" beats "the card I could deploy next turn", and pricing next turn
accurately promotes expensive cards a race deck does not want. This is the third independent result
this session pointing the same way — the reserve eviction (+20 when corrected), `PENDING_LAND` (+7/+9),
and now the root fix (+18, unrescuable by refit). All three make the mana projection more accurate;
all three lose.

Making the projection honest would mean re-deriving the discount from **tempo** rather than from
turns-to-cast, and re-fitting it as a whole. That is a real project, not a constant sweep, and it is
the open item this round leaves. The flag stays in the tree default-off so the defect is one env var
away from being reproduced.

## ROUND 14 (2026-08-05): the tempo rewrite is UNNECESSARY — the ranking is already 99.1% optimal

Round 13 left "re-derive the discount from tempo" as the open project. It was attempted, and the
first thing built to justify it killed it instead. **Recording this so nobody starts it again.**

### A name-keyed truth table (the reusable part)

`MTG_TUTOR_FORCE_CARD="<name>"` collapses the tutor to one card BY NAME, and
`test/goblins_tutor_truth_table.py` builds a table of the real win turn of fetching each candidate.
The existing `goblins_tutor_truth.py` probes by RANK, so its table dies the moment the ranking
changes — rank 4 is a different card before and after. Keyed by name it is model-independent: build
once, then score any model with ONE run per game instead of one per card.

Built at `--budget-ms 0` (unbounded), so every number is deterministic and load-independent. That
matters: this session twice had a wall-clock-churn artifact masquerade as signal. Scan of 1,600 games
(seeds 4004/5005/6006/7007) found **429 with a searched tutor fetch**; 6,864 (game, card) probes.

Regret is scored FORCED-vs-FORCED. Forcing collapses the axis, so a forced run explores fewer plans
and the same card can win a turn later under force; comparing a model's free run against a forced
oracle produces negative "regret" that is pure instrument artifact.

### The result

```
model                     total regret   optimal picks
SHIPPED                        +4         425/429 = 99.1%
SHIPPED + postland             +6         423/429 = 98.6%
TEMPO                          +7         414/421 = 98.3%
TEMPO + postland               +8         413/421 = 98.1%
```

**The shipped ranking is 99.1% optimal, and the entire headroom for a perfect oracle is 4 turns
across 429 tutor games — 0.009 turns/game.** There is nothing to win. Every lever shipped this
session moved single-digit turn-units over 8,000 games; a total theoretical ceiling of 4 turns is
below the noise floor of the suite that would have to validate it.

The tempo model (damage before the projected kill: `once + per_team*(H-t) + per_body*(H-t-1)`, which
DERIVES the hand-tuned Piledriver-vs-lord rule rather than encoding it) also **failed its own
acceptance test**: it was supposed to get better as the projection got more honest, and instead
degraded under `postland` in the same direction as the shipped model (+7 -> +8 vs +4 -> +6). Removed
rather than kept default-off — the headroom number closes the whole area, not just this variant, so
leaving a parallel scorer in a hot path would be clutter that invites someone to retry a dead lead.

### What this retroactively explains

The value reserve measured exactly 0.0 held-out. That was read as "the discount is usually right".
It is stronger than that: the discount is right **99.1% of the time on the decision that matters**,
so forcing bombs onto the axis was always going to be inert. Likewise the three rejected projection
fixes were not narrowly unlucky — they were perturbing a component that is already very nearly
optimal, where essentially every change is a downgrade.

**The tutor ranking is closed.** Future Goblins work should look at decisions with actual headroom,
not this one.
