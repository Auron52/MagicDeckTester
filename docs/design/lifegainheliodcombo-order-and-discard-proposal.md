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
