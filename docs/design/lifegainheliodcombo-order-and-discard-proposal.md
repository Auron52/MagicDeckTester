# LifegainHeliodCombo: cast order and discard rule (PROPOSAL for USER review, 2026-10-28)

USER 2026-10-28: *"We should have an order for this deck most likely even if we choose to share some of it. Same with
the Discard rule. You should keep in mind that the combo does make them a bit different. Please create a potential set
of rules for review."*

## USER review (2026-10-28) -- these rulings SUPERSEDE the proposal below where they differ

USER, verbatim: *"Abiding Grace low is correct in Goldfish. It is good with Martyr and not much else. It is much more
of a menace in real play. A second Ballista is better than a second Heliod, but not particularly strong since its main
usage is combo I would rank it lower for sure. Heliod on its own is pretty solid and I would keep it sometimes, though
it may be discardable if there are better payoffs. Your discard rule is probably fine. Martyr's activation timing
should be as early as possible to get maximum value, not searched. That means Martyr itself needs to go earlier in the
order."*

What that settles:
- **Q2 (Abiding Grace low): CONFIRMED.**
- **Q3 (second Ballista): ranked LOWER.** It stays above a second Heliod (which sheds as a duplicate legend), but drops
  to the very bottom of the fill (the first fill card to go). Agent reading: a FIRST Ballista with no Heliod in hand or
  play sits in the fill just above Abiding Grace ("its main usage is combo").
- **Q4 (Heliod alone): CONFIRMED partner-gated.** Without a Ballista / Ranger-Captain, Heliod is a payoff in the USER's
  payoff order and may be shed for better payoffs.
- **Discard rule: accepted as proposed** with the Ballista change above.
- **Martyr of Sands: activation AS EARLY AS POSSIBLE, NOT SEARCHED, and Martyr EARLIER in the order.** Agent reading of
  "as early as possible": Martyr is the FIRST cast of the turn (after the land drop -- a Plains is not a white card, so
  it costs no reveal), ahead even of Abiding Grace, and its activation runs right after it resolves (or before every
  cast when it is already on the battlefield). Each white card cast before the reveal costs 3 life; a watcher cast
  first would add only 1 (its trigger on Martyr's entry), and payoffs are only one counter per event either way.
- **Q1** (Heliod ahead of Voice / Pridemate with a Ballista in the list) was not ruled on; the recommendation (no)
  stands as the default.
- Grace with Martyr: NOT built. USER: *"Not as crucial unless we are unable to hit life breakpoints, but I think that
  is a rare enough case we could ignore it."*

USER, same day, verbatim: *"Ballista's X should be maximum or less to ensure the combo can go off. Unless we have
reason to believe we need more mana in the turn I would just max it. The activations may as well wait until they would
make lethal or we are comboing."*
- **Walking Ballista X: NOT a full 0..max fan any more.** Candidates = the MAXIMUM affordable X, plus the largest X that
  still leaves the loop's mana (Heliod's {1}{W} grant, or the {4} counter) when that X makes the combo go off this turn.
  A smaller X is otherwise offered only when the turn needs the mana for something else (agent reading: the plan's other
  casts already come out of the pool before X is sized, so "max" is the max of what is left).
- **Ballista activations wait:** the counter pings and the {4} "+1/+1 counter" only when they make lethal or are part of
  the combo (the loop / the bump into a live loop). Today a LIFELINKED non-lethal ping whose gain something reads is
  still offered (MTG_LHC_PING_PRUNE keeps it); under this ruling it goes too.

Implementation plan (agreed shape, not built yet): `LifegainHeliodComboProvider` overrides for the order
(CastOrderRank / CastOrderRankLatest / ArrangeCastOrder / CastOrderingCandidates, on CritterLifegain's helpers: Martyr
slot 1, Grace slot 2, Ranger-Captain first body, Ballista right after Heliod and never before it -- Heliod's FIRST
variant keeps Martyr and Grace ahead of it), Martyr pinned to its earliest reveal position (engine: a reveal-outlet
cast counts as a position boundary in `RevealSlotCounts`; enumeration pins each outlet in place instead of the fan --
position 0 when on the battlefield at turn start, right after its own cast otherwise; continuations keep the trailing
pass), XCandidates for the Ballista, CounterPingCounts / {4} gated on lethal-or-combo, and CleanupDiscardCandidates
(the discard rule above). One lever per rule (MTG_LHC_ORDER, MTG_LHC_MARTYR_EARLY, MTG_LHC_BALLISTA_X,
MTG_LHC_BALLISTA_WAIT, MTG_LHC_BUCKET_DISCARD), unit tests, held-out A/B on lhc + lhc2hg, every slower game classified.

Status: **USER-reviewed; implementation follows these rulings.** (Original proposal below, unchanged.)

Original status line: **PROPOSAL, nothing implemented.** Today the deck inherits CritterLifegain's USER-settled order
(`critterlifegain-cast-order-2026-10-14.md`) and discard rule (`critterlifegain-discard-policy-proposal.md`, "SETTLED")
through `LifegainHeliodComboProvider : CritterLifegainProvider`. The new cards fall into that order's catch-all slots, and
the discard rule has no notion of the combo. Each rule below is marked **SHARED** (kept from CritterLifegain as it is),
**CHANGED** (a CritterLifegain rule amended for this deck) or **NEW** (this deck only). Points that need a USER ruling
are marked **Q**.

## The list

| card | n | cost | role here |
|---|---|---|---|
| Soul Warden, Soul's Attendant | 4 + 4 | {W} | enter watcher (gain 1 per other creature entering) |
| Auriok Champion | 4 | {W}{W} | enter watcher |
| Voice of the Blessed | 4 | {W}{W} | unit payoff (counter per gain event) |
| Ajani's Pridemate | 1 | {1}{W} | unit payoff |
| Heliod, Sun-Crowned | 4 | {2}{W} | payoff (counter on a target per gain event) **and combo piece**; creature at devotion 5 |
| Walking Ballista | 2 | {X}{X} | **combo piece**; also an X body and a mana sink |
| Ranger-Captain of Eos | 2 | {1}{W}{W} | tutors a creature with mana value 1 or less to hand (Ballista, a watcher, Martyr, Serra) |
| Serra Ascendant | 3 | {W} | +5/+5 and flying at 30+ life |
| Martyr of Sands | 2 | {W} | {1}, reveal X white cards, sacrifice: gain 3X (life for Serra) |
| Abiding Grace | 2 | {2}{W} | end step: gain 1, or return a mana-value-1 creature from the graveyard |
| Ajani, Strength of the Pride | 3 | {2}{W}{W} | walker (-2: Pridemate token; +1: gain life) |
| Unexpectedly Absent | 2 | {X}{W}{W} | removal (only ever aimed at spawn tokens) |
| Plains, Emeria, the Sky Ruin | 22 + 1 | - | Emeria enters tapped |

**The combo:** Heliod on the battlefield (creature or not) + a Walking Ballista with 2 or more counters + {1}{W} to give
it lifelink. Each ping gains 1 life, and Heliod puts the counter back on the Ballista: an unbounded kill this turn.
The cheapest assembly with Heliod already out is Ballista for X = 1 ({2}) plus the {1}{W} grant: **4 mana**. One
watcher's gain on the Ballista's entry lets Heliod bump it from 1 counter to 2 (the provider's bump rule,
`MTG_LHC_BUMP_TARGET`).

---

## Part 1: cast order

Everything stays in main 1 and the land drop stays first (SHARED). The flip stays as it is (SHARED): when the board
holds 2 or more watchers than payoffs, the payoff block moves ahead of new watchers.

| slot | card(s) | rule | status |
|---|---|---|---|
| 1 | **Abiding Grace** | first of all casts | **CHANGED** (was last, "everything else") |
| 2 | Soul Warden, Soul's Attendant, then Auriok Champion | enter watchers | SHARED |
| 3 | Voice of the Blessed, then Ajani's Pridemate | unit payoffs, more white pips first | SHARED |
| 4 | Ajani cast for its -2 | right after the Pridemate | SHARED |
| 5 | Heliod | its slot, with CritterLifegain's walk / two-way placement, **never past the Ballista** | **CHANGED** |
| 6 | **Ranger-Captain of Eos** | first of the bodies | **CHANGED** (was among the bodies by mana value) |
| 7 | **Walking Ballista** | right after Heliod (and Ranger-Captain), before the other bodies | **NEW** |
| 8 | Martyr of Sands, Serra Ascendant | other bodies, by mana value then name | SHARED shape (Martyr is new) |
| 9 | Ajani cast for its +1 or with no activation | after the creatures | SHARED |
| 10 | Unexpectedly Absent | last | SHARED |

### Why each change

1. **Abiding Grace first.** Its trigger is at the end step, so its position in main 1 changes nothing except devotion:
   its {W} counts toward Heliod's 5. No watcher fires on a non-creature, so casting it before everything else is never
   worse when the plan casts it this turn, and sometimes makes Heliod a creature sooner. This is a sound dominance, not a
   heuristic.
2. **Heliod never walks past the Ballista.** Heliod is now the combo's anchor. CritterLifegain's placement can move a
   non-creature Heliod to after the creatures of the list (the "walked" and "two-way" cases). If a Ballista is in the
   same list, that would put Heliod after it, and the gains on the Ballista's entry would land no Heliod counter on it.
   Proposal: when a Ballista is in the list, it moves with Heliod and stays right after it. The Ballista adds no
   devotion, so this never changes whether Heliod becomes a creature.
3. **Ballista right after Heliod, before the other bodies.** Every later body's entry is one more gain event per
   watcher. With Heliod out, the bump rule can put the next counter on a 1-counter Ballista and make the loop live.
   Cast after the bodies, the Ballista misses those events. When the loop is not in reach, Heliod's counter goes where it
   did before (an attacker that can swing this turn first, and a summoning-sick Ballista cannot), so the earlier slot
   costs nothing in the usual case.
   X itself stays searched over every legal value.
4. **Ranger-Captain first of the bodies.** Its tutor is information: whatever it finds (a Ballista for the combo, a
   watcher, Martyr) can still be cast this turn after it, and the search re-plans the rest of the turn at that point.
   It stays after the watchers and payoffs so its own entry is seen by them.

### Not ordered (stays searched)

- **Martyr of Sands' activation.** Where the reveal-and-sacrifice happens in the turn is already a searched axis
  (`RevealOutletSlot`): before the casts (more white cards in hand, so more life) or after them (the payoffs cast this
  turn see the gain). That is a real trade-off with no dominant side, so no order rule is proposed.
- **Ballista's X**, the Heliod loop activation, and the counter pings.

**Q1.** Should Heliod go ahead of Voice / Pridemate in this deck when a Ballista is in the same cast list? The flip
already handles the many-watchers case; this would be a combo-specific exception. Recommendation: no. Rule 2 already
guarantees Heliod before the Ballista, and the payoffs before Heliod are what grow the attackers.

---

## Part 2: discard rule

Context first: the 5i bound measured best-vs-worst discard ranking at **+0.0000t (d3) / +0.0005t (d5)** on 2,000
held-out paired games. Cleanup discards are rare here, so this rule is about playing sensibly and consistently, and
about what the human sees, more than about the average.

Shape: CritterLifegain's SETTLED ladder (SHARED), with a **COMBO** bucket added and the new cards placed.

### Buckets

- **MANA (CHANGED):** 3 lands counting the board; **4 while holding a 4+ mana card (Ajani) OR while the combo is in
  reach** (Heliod in hand or on the battlefield AND a Ballista in hand). The 4th land is exactly the X = 1 Ballista plus
  the {1}{W} grant. Plains before Emeria (Emeria enters tapped; Remote Farm's place in CritterLifegain).
- **WATCHERS (SHARED):** Soul Warden / Soul's Attendant > Auriok Champion; quota 2 counting the board. A watcher is
  also what bumps an X = 1 Ballista into the loop, which is one more reason to keep the quota.
- **COMBO (NEW), protected above the payoffs:**
  - the first **Heliod** when a Ballista or a Ranger-Captain is in hand or on the battlefield;
  - the first **Walking Ballista** when a Heliod is in hand or on the battlefield;
  - a **Ranger-Captain** when a Heliod is in hand or on the battlefield and no Ballista is (it is the way to find one).
  Without its partner, each of these falls back to its ordinary bucket below.
- **PAYOFFS (SHARED):** Voice / Pridemate > Heliod > Serra; Serra first at 30+ life. Filled from the hand (creature
  quotas do not count the board, the 2026-10-10 USER ruling).
- **FILL (CHANGED, new cards placed):** the rest of the seven: variety first (one copy per name), then 4+ drops shed
  first (Ajani), then value order: payoffs > **Walking Ballista** (a second copy, or one with no Heliod around: still an
  X body and a mana sink) > **Martyr of Sands** > Ranger-Captain > **Abiding Grace** > Ajani.
  - **Martyr** moves up to just below the payoffs while a Serra Ascendant is in hand or on the battlefield and life is
    under 30 (it is the deck's way to 30).

### Order

- **KEEP:** land -> watcher -> COMBO pieces -> payoff -> land -> payoff -> 2nd watcher -> land -> fill -> 4th land
  (only with Ajani or the combo in reach).
- **SHED:** Unexpectedly Absent -> duplicate legends (a 2nd Heliod / 2nd Ajani, in play or in hand: SHARED) -> surplus
  lands (Emeria first) -> fill overflow (repeat names, then Ajani, then Abiding Grace, then by value) -> spare watchers
  -> the protected cards, the KEEP ladder backwards.

**Q2.** Abiding Grace: I placed it low (fill, just above Ajani). Each end step it is one more gain event, or a returned
watcher / Martyr / Serra, but it never attacks and it is slow. Its baseline card score is -0.29. Higher, lower, or as
proposed?

**Q3.** Should a second Walking Ballista be treated as a duplicate (shed early, like a 2nd Heliod)? Recommendation: no.
Unlike a legend, a second Ballista is a whole card (a body, a mana sink, a backup piece after the first one is used).

**Q4.** The COMBO bucket protects pieces only when their partner is present. The alternative is to always protect one
Heliod (it is a payoff anyway). Recommendation: partner-gated as proposed. Without a Ballista or Ranger-Captain, Heliod
is simply a payoff and the USER's payoff order already places it.

**Not in the rule:** Martyr's X counts the white cards in hand at activation, so every discard lowers a future Martyr
by 3 life. That is too small and too indirect to bucket on.

---

## How it would be adopted (after the USER's review)

- Order behind `MTG_LHC_ORDER`, discard behind `MTG_LHC_BUCKET_DISCARD` (heurarm slots, default ON once the USER
  approves; `=0` restores the inherited CritterLifegain behaviour exactly).
- Unit tests in the style of `test_critterlifegain_cast_order.cpp` / `test_critterlifegain_discard.cpp` (each rule
  pinned, the discard shed by shed).
- Held-out A/B on lhc and lhc2hg at shipped, d3 b10 and d0; every slower game classified at `--depth 8 --budget-ms 0`;
  numbers reported to the USER. If the USER's order measures worse, root-cause it and bring an amended version.
- CritterLifegain is unchanged by all of this (the overrides live in `LifegainHeliodComboProvider`).

## MEASURED (2026-10-28) -- the rules as coded, root causes, and the proposed amendments (USER to decide)

Implemented as above, each rule behind its own lever (`MTG_LHC_ORDER`, `_MARTYR_PIN`, `_MARTYR_SERRA`, `_BALLISTA_X`,
`_BALLISTA_WAIT` + `_BALLISTA_SINK_WAIT`, `_BUCKET_DISCARD`; A/B arms `_MARTYR_LATE`, `_PAYOFFS_FIRST`,
`_HELIOD_BEFORE_PAYOFFS`). Unit cover `test/unit/test_lhc_rules.cpp`; apply-level cover
`test/scenarios/lhc_martyr_after_own_cast.json`. With every lever off the engine reproduces the committed smoke GT
digests exactly (d3, d5, 2HG d5).

**Engine finding (Martyr).** A Martyr of Sands cast this turn has no outlet in the plan that casts it; its entry is a site-9
breakpoint, and the continuation derived there kept the outlet TRAILING (the searched position fan never ran in
continuations). So "cast Martyr, activate it, then cast more" was unfindable at any budget before this work. The pin now
runs in continuations, and both worlds run a continuation's position-0 outlet before its casts. The scenario passes pinned
and fails with `MTG_LHC_MARTYR_PIN=0`.

**A/B 1** (held-out seeds 961001/963001, 2000 games each, d5 b20, std and 2HG, one pooled batch; delta vs the rules as
coded, negative = faster; games better / worse):

| arm | std | 2HG |
|---|---|---|
| all rules off (= committed play) | -0.0055 (37/16) | -0.0057 (32/9) |
| Ballista waits off / {4} counter wait off | -0.0055 (21/0) / identical | -0.0068 (27/0) / identical |
| Martyr always late | -0.0030 (13/1, the 1 = bottoming variance) | 0 |
| Martyr always early | +0.0070 (1/29) | +0.0125 (5/55) |
| payoffs (Voice / Ajani) before the gainers | +0.0063 (0/25) | +0.0088 (1/36) |
| Martyr searched (pin off) | +0.0033 (2/15) | +0.0010 (3/7) |
| Ballista X rule off | +0.0008 (0/3) | +0.0008 (1/4) |
| order off | -0.0002 (15/14) | +0.0010 (3/7) |
| discard rule off; Heliod before payoffs | 0 (never binds) | 0 |

Root causes of the two rules that cost: (1) the {4} "+1/+1 counter" waiting for lethal -- unlike a ping, a counter bought
with idle mana is permanent attack power (smoke lhc d5 gi5 T6 -> T7; every one of the 21/27 games); the pings waiting is
neutral. (2) Martyr hoisted first while a Serra is out casts it ahead of the enter watchers, so the watchers miss its entry
and Heliod / Pridemate get fewer gain EVENTS (smoke lhc d5 gi142: Martyr before Auriok Champion, T4 -> T5). Late wins
everywhere it differs.

A Ballista X-rule defect surfaced on the confirm seeds and is fixed: X = 1 was kept as "the loop's reserve" only with an
enter watcher out, but any later gain event bumps a 1-counter Ballista (seed 967001 gi1347: X = 1, Ajani +1, grant, loop --
dropped, unfindable at any budget). Now any X >= 1 keeps the reserve.

**PROPOSED AMENDMENTS (the candidate): the {4} counter does not wait (pings still do), and Martyr goes late (after the
watchers) even with Serra out.** Final A/B on a third seed set (969001/971001, same shape), after the X fix:

| arm | std | 2HG |
|---|---|---|
| committed play (all off) | 4.7918 | 5.0057 |
| the rules as coded | 4.7992 | 5.0103 |
| **candidate** | **4.7903** (vs committed 8 better / 2 worse) | **5.0047** (7 better / 3 worse) |
| candidate without the X rule | 4.7900 | 5.0053 |

The candidate's 5 slower games at d8 b0: 4 recover (budget churn); 1 persists (2HG s971001 gi231, identical draws): the
Martyr pin changes the T3 evaluation (Heliod vs Voice) through lookahead turns that draw the second Martyr, and the T5
Ballista kill is lost -- pin off recovers it. Not root-caused further.

The same three arms at the tiers' other settings (seeds 969001/971001, 4000 games per cell, one pooled batch):

| setting | committed | rules as coded | candidate |
|---|---|---|---|
| d3 b10 std | 4.8000 | 4.8090 | **4.7995** |
| d3 b10 2HG | 5.0190 | 5.0213 | **5.0165** |
| d0 std | 5.2328 | 5.2455 | **5.2320** |
| d0 2HG | 5.4530 | 5.4600 | **5.4520** |

Open for the USER: adopt the two amendments (or not); keep the pin despite gi231; whether to keep the X rule (net 0 here).

## MEASURED 2 (2026-10-29) -- the two persisting games root-caused; candidate 2 (USER to decide)

**gi231 (2HG s971001) -- the pin cost a Ballista kill, and the cause is a position the rule never names.** A Walking
Ballista cast with X = 1 cannot loop alone: its first lifelinked ping removes its only counter and it dies (SBA) before
Heliod's trigger can target it, so the counter lands elsewhere (verified by forcing Ballista + grant: T8). The kill on
that board is Ballista X = 1, THEN Martyr (the gain puts Heliod's counter on the Ballista -> two counters), grant, loop:
2 + 1 + 2 = all five lands, T5. With the pin, "late" = after the plan's last "whenever you gain life" cast; a Ballista
is not one, so on a plan with no watcher cast "late" degenerates to "before every cast", the Martyr fires before the
Ballista exists, and the kill is unexpressible (the FS root scan held no breakpoint plan; unpinned, the base keeps
Martyr trailing, its gain opens a site-9 breakpoint after the Ballista, and the continuation list offers the loop).
The engine's searched position fan carries the same assumption ("moving it past a non-white non-watcher cast changes
nothing"), which is false for a Ballista beside Heliod.
-> lever **`MTG_LHC_MARTYR_AFTER_PINGER`** (A/B arm, default OFF): `TurnSolver::RevealSlotCounts` counts a counter
pinger's cast, so the pinned late slot (and a fan position) follows it. Read in both worlds through that one function.
Only this deck holds Martyr or Ballista.

**gi1638 (std s975001) -- "Martyr always late" loses the Serra case.** Committed play activates Martyr before casting
Heliod (Heliod still in hand: X = 2, +6 -> 29), Heliod enters as a creature (devotion 6), Auriok Champion +1 -> 30:
Serra Ascendant online, T4 kill. Always-late activates after Heliod (X = 1, 27, T5). The USER's Serra rule activates
early (right) but also HOISTS the Martyr CAST ahead of Auriok Champion on T3, which then misses Martyr's entry (one life
short of 30 -> T5) -- the gi142 mechanism. The two placements want different answers.
-> lever **`MTG_LHC_MARTYR_CAST_LATE`** (A/B arm, default OFF): the cast is never hoisted (watchers see its entry); the
activation keeps the USER's Serra-keyed pin. (`MTG_LHC_MARTYR_LATE` moves both.)

Both games recover under cast-late + after-pinger at d8 b0 (gi231 T6 -> T5, gi1638 T5 -> T4, = committed play).

**Fourth seed set (973001/975001, d5 b20, 4000 games per cell):** candidate (sink-wait off + Martyr late) vs committed
std -0.0028 (12 better / 1 worse), 2HG -0.0020 (9 / 1); + after-pinger: std +0.0003 (0 / 1, recovers at d8), 2HG 0.

**Fifth seed set (977001/979001, d5 b20), CANDIDATE 2 = sink-wait off + Martyr cast late + after-pinger:**

| arm | std | 2HG |
|---|---|---|
| committed play | 4.7598 | 5.0072 |
| old candidate (Martyr always late) | 4.7572 | 5.0053 |
| cast late | 4.7568 | 5.0053 |
| **candidate 2** | **4.7568** (vs committed 15 better / 3 worse; vs old candidate 2 / 0) | **5.0053** (9 / 1) |

All four of candidate 2's slower games recover at d8 b0 (budget churn) -- it passes the adoption bar on this set.

The same seeds at the tiers' other settings (4000 games per cell, one pooled batch):

| setting | committed | old candidate | candidate 2 |
|---|---|---|---|
| d3 b10 std | 4.7665 | 4.7642 | **4.7635** (18 better / 6 worse) |
| d3 b10 2HG | 5.0190 | 5.0155 | **5.0157** (14 / 2) |
| d0 std | 5.1893 | 5.1893 | 5.1893 (1 / 1) |
| d0 2HG | 5.4153 | 5.4147 | **5.4147** (3 / 2) |

Every one of the 11 slower games recovers at d8 b0. **Candidate 2 meets the adoption bar at every tier setting.**

Open for the USER: adopt candidate 2 (= the USER's rules + `MTG_LHC_BALLISTA_SINK_WAIT=0` + `MTG_LHC_MARTYR_CAST_LATE=1` +
`MTG_LHC_MARTYR_AFTER_PINGER=1` as defaults), or any subset; keep the X rule (net 0 throughout).

## USER RULING 2026-10-29 + MEASURED 3 -- Ballista entry / Martyr gain / ping last; Heliod on the Ballista

USER: *"The Ballista entering and ping ability should be separated. The ping ability should go last. The Martyr
lifegain should always happen before."* and *"Heliod naturally should also go first and target the Ballista if it
can."* Implemented (all default ON, the USER's rules):
- `MTG_LHC_MARTYR_AFTER_PINGER` -- the Ballista's ENTRY is a cast in the order (right after Heliod); a Ballista cast is
  a Martyr position boundary, so the late slot follows it (Martyr's gain after the entry, before the ping).
- `MTG_LHC_PING_LAST` -- the Ballista's counter ping and the Heliod loop rank after every other trailing activation
  (`ActivationOrderRankForAction`, both worlds). Measured: changes no game (the engine already ran them last); it
  now guarantees it.
- `MTG_LHC_HELIOD_ON_BALLISTA` -- Heliod's gain counter goes on our Ballista whenever one is out (the loop-live bump
  first, else the fewest counters). Heliod's lifelink grant needed nothing: the loop action pays the grant itself and
  targets the Ballista; a standalone grant still goes to the best attacker (a just-cast Ballista cannot attack).

**Sixth seed set (981001/983001, d5 b20, 4000 games per cell); candidate 3 = the USER's rules incl. the above +
`MTG_LHC_BALLISTA_SINK_WAIT=0` + `MTG_LHC_MARTYR_CAST_LATE=1`:**

| arm | std | 2HG |
|---|---|---|
| committed play | 4.7740 | 5.0202 |
| candidate 2 | 4.7707 | 5.0183 |
| + ping last | 4.7707 | 5.0183 |
| **candidate 3** | **4.7657** (35 better / 2 worse) | **5.0135** (33 / 6) |
| candidate 3, Martyr SEARCHED (pin off) | 4.7687 (23 / 2) | 5.0150 (25 / 4) |
| candidate 3, Martyr always early | 4.7665 (35 / 5) | 5.0135 (33 / 6) |

Six of candidate 3's eight slower games recover at d8 b0; two persist (std s981001 gi1452, 2HG s983001 gi1833), and
both recover with the pin off or Martyr always early: the late slot makes the search spend Martyr on an earlier turn
after that turn's watchers, where holding it and revealing BEFORE casting Heliod / the second Voice (they count in X)
wins a turn sooner. No single pinned slot is right in every game (gi231 needs Martyr after the Ballista, these two
before Heliod / Voice), and SEARCHING the position is worse overall at the shipped budget (+0.0030 std, +0.0015 2HG).
So these two are the pin's measured price. Open for the USER: keep the pin with that price (recommended); adopt
`MTG_LHC_BALLISTA_SINK_WAIT=0` and `MTG_LHC_MARTYR_CAST_LATE=1` as defaults; keep the X rule (net 0).

## USER 2026-10-29 -- Martyr timing is three-way; MEASURED 4

USER: *"it might be right to play and activate it early to get more life for Serra's Ascendant or it might be right to
play and activate it relatively late in a way that forces a lifegain trigger. Even worse, you can empty your hand of
white cards and then it makes sense to consider playing and activating it before the last white card is played."*
Then: *"If there are no things to respond to triggers we don't need to play it late. If we aren't running out of
white cards we don't need to consider that option."* and *"We might even be able to chuck the early option if there
is no Serra Ascendant around, but that is arguably a bit riskier. It could be worth testing."*

`MTG_LHC_MARTYR_THREE` (A/B arm, default OFF): the pinned slot stays and the plan gets TWINS at the other relevant
positions, searched: before every cast (always); after the last counted cast (only when the plan CASTS a responder --
watchers already out see the gain anywhere); right before the plan's last white cast (only when the late slot would
reveal no white card). Outer base plans with one positionable Martyr only. `MTG_LHC_MARTYR_THREE_NO_EARLY` (arm on
top): no early twin unless the Serra rule calls for early.

**Seventh seed set (985001/987001, d5 b20, 4000 games per cell), all arms = candidate 3 + the arm:**

| arm | std | 2HG | work units |
|---|---|---|---|
| committed play | 4.7670 | 4.9895 | 1.086x |
| candidate 3 (pinned slot) | **4.7598** (30 better / 1 worse) | **4.9825** (29 / 1) | 1.000x |
| + three positions | 4.7598 (identical) | 4.9825 (identical) | 1.007x |
| + three, no early without Serra | 4.7598 (identical) | 4.9825 (identical) | 1.001x |

At the shipped budget the three-way choice moves no game on this set; at d8 b0 it recovers the pin's two persisting
games (std s981001 gi1452 5 -> 4, 2HG s983001 gi1833 6 -> 5), so all four hard games (gi231, gi1638, gi1452, gi1833)
now equal committed play. The set's two slower games (std s985001 gi590, 2HG gi756) recover at d8 b0 under every arm.
**Candidate 3 + three positions meets the adoption bar** -- and it is the completeness-safe shape: it adds findable
lines, removes none; no-early removes the early twin at every budget (riskier, as the USER said) for 0.6% of work.

## ADOPTED (USER 2026-10-29: "adopt your recommendations as long as they are measuring well and not removing any relevant lines")

Defaults now: the USER's rules (`MTG_LHC_ORDER`, `_MARTYR_PIN` + `_MARTYR_SERRA`, `_BALLISTA_X`, `_BALLISTA_WAIT`,
`_BUCKET_DISCARD`, `_MARTYR_AFTER_PINGER`, `_PING_LAST`, `_HELIOD_ON_BALLISTA`) + `MTG_LHC_BALLISTA_SINK_WAIT=0` +
`MTG_LHC_MARTYR_CAST_LATE=1` + `MTG_LHC_MARTYR_THREE=1`; `_MARTYR_THREE_NO_EARLY`, `_MARTYR_LATE`, `_PAYOFFS_FIRST`,
`_HELIOD_BEFORE_PAYOFFS` stay A/B arms (OFF). None of the adopted levers removes a line: THREE and AFTER_PINGER add
positions, SINK_WAIT=0 removes a restriction, CAST_LATE / PING_LAST only order, HELIOD_ON_BALLISTA is a resolution pick
(never a searched branch). The X rule is the USER's prune; its one lossy case (X = 1 with a later gain) was fixed.

## USER 2026-10-29 -- Heliod's counter: no summoning sickness, then lifelink (ADOPTED)

USER: *"For Heliod Targets we should prioritize things without summoning sickness followed by ones with Lifelink like
Serra Ascendant."* -- *"That is an important distinction because Serra Ascendant can quickly make itself larger if it
has counters put on it."* -- and, on keeping the Ballista ahead of both: *"Walking Ballista is even more important."*

`MTG_LHC_HELIOD_UNSICK_LIFELINK` (default ON). Heliod's counter target, in order: the Walking Ballista (the loop-live
bump, then `MTG_LHC_HELIOD_ON_BALLISTA`); otherwise a creature WITHOUT summoning sickness (a tapped attacker is not
sick); then a LIFELINKER; then combat power; ties to the lowest battlefield index. It also decides gains off our turn
(the generic pick used to). A devotion-5 Heliod is itself an unsick 5/5 and takes the counter over a sick Serra.

Measured (ninth held-out set 993001/995001, 2,000 games per seed per format, d5 b20, one pooled batch, arms innermost):

| format | rule off | rule on | better / worse | units |
|---|---|---|---|---|
| std | 4.7555 | 4.7555 | 0 / 0 | x1.004 |
| 2HG | 4.9737 | 4.9735 | 1 / 0 | x0.998 |

Play changed in 37 of 160 jobs (28 std, 9 2HG) without moving a win turn except one 2HG game faster: the rule is live,
cost-neutral and never slower, so it meets the adoption bar.
