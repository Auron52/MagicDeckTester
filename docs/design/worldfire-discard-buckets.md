# Worldfire: bucketed keep order (discard + Brainstorm / Ponder / Preordain) -- DESIGN, for USER review

Status: **proposed 2026-11-21, amended by the USER the same day (other-type finisher kept above dead cards; dig
prioritised while short a combo piece; Bloom dropped near the combo turn; multiple dig while short; tutors fill the two pieces first; every wait counted in the projection; dig kept to draw tutored cards); not adopted.** Discard buckets are USER-owned (analyze-deck Stage 5i); this is the
AI's best shot, built to be amended. USER request (verbatim): *"It seems pretty clear we are going to need prunes
for Brainstorm, Ponder and Preordain. Probably similar to those in Cyclone, but a bit simpler because we don't need
to worry about our topdeck."* -- *"But essentially what we'll need to design is the discard heuristic to do this
well. ... It should be bucketed based on the combo and mana needs."*

## One model, four consumers

A single **keep order** over the cards in hand (plus, for the dig spells, the cards looked at), built quota-first
from buckets netted against the board and the suspended cards. Everything below reads it:

| consumer | rule |
|---|---|
| cleanup discard | shed the lowest-ranked card (one index; re-ranked after each shed) |
| Brainstorm put-back | put back the two lowest-ranked cards of the post-draw hand; the higher-ranked of the two on top (it is drawn first) |
| Ponder | rank the three looked-at cards together with the hand. **Keep** (best first) if at least one fills a quota the hand has not filled; **shuffle** if none does (all three would be overflow or dead) |
| Preordain | scry 2: keep a card on top iff it fills a still-unfilled quota (counting cards kept above it), best first; bottom the rest |

The search still branches over whatever the provider admits; these are provider restrictions (one-option prunes
for the put-back and the scry/Ponder disposition), the same shape as Cyclone's `PutBackSingleCandidate` /
`ReorderKeepShuffleOnly` and Niv's cantrip rules. Simpler than Cyclone because nothing reveals from the top: the
library top only matters as "what we draw next".

## The go-off projection (the mana need)

The quotas depend on WHEN we expect to cast Worldfire, as the tutor ruling already said (*"It really depends on
when we project to go off."*). Project T\* = the earliest turn from now (now .. now+4) on which EVERYTHING is ready
-- the mana reaches the need AND both pieces are castable that turn. **Every wait counts** (USER 2026-11-21: *"(and
we need to count any waiting in our projection)"*):

- **Worldfire ready:** in hand now; or tutored -- Mystical Tutor (instant: cast in the opponent's end step or our
  upkeep) delivers it on our next draw step; Personal Tutor (sorcery) puts it on top this turn, so it arrives on
  the NEXT turn's draw unless a cantrip we can still cast this turn draws it. Each tutor also spends the mana to
  cast it on its turn ({U}) and the draw it replaces.
- **Finisher ready:** a Rift Bolt in hand (or tutored, same delays) is suspended on T\* itself; a Halberdier
  pending arrives on its own turn (`arrive_turn`), and one in hand arrives on now+4 if suspended now -- either way
  T\* must be the turn BEFORE it arrives (Worldfire would exile it if it arrived first), so a Halberdier finisher
  pins T\* to arrival-1.
- **Mana ready:** as below, with each wait counted -- tapped lands from the turn after they are played, a Bloom 3
  turns after it is suspended, Bazaar one storage counter per turn it is left to charge, one land drop per turn.
- T\* = the earliest turn on which all three hold. A wait on any one piece pushes T\*; the quotas are then
  computed against that T\*, so e.g. a slow finisher also slackens the mana quota.

Mana detail:

- **Need:** 9 with RRR for Worldfire, **+1 R** when the finisher is a Rift Bolt that must be suspended that turn
  (no Rift Bolt pending on suspend and no Halberdier arriving on T\*+1).
- **Supply on turn t:**
  - lands on the battlefield (1 each; Sandstone Needle / Saprazzan Skerry 2 while counters remain; Mercadian
    Bazaar = its storage counters plus the turns it can still charge before t);
  - one land drop per turn from hand through t, taken in the land keep order below (a tapped land counts from the
    turn after it is played);
  - Lotus Blooms on suspend arriving by t (3 each), and a Bloom in hand if suspending it now arrives by t;
  - rituals in hand, net: Seething Song +2 each, Irencrag Feat +3 (at most one Feat: after it only Worldfire is
    castable).
- No T\* within now+4: "far" -- every mana card fills a quota (we are short, keep all of it).

This is the same estimate the tutor prune makes (`TutorCandidates`: `LooseManaCeiling` + Bloom + net rituals
vs `kWorldfireMana`), extended over turns so it can see land drops and arriving Blooms. The cards it USES to reach
T\* are the MANA quota; the rest of the mana is overflow.

## Buckets (quota-first; only the overflow is sheddable)

| bucket | members | quota (net of board / suspend) | inside an over-full bucket, shed first |
|---|---|---|---|
| **COMBO** | Worldfire. **Tutors fill in FIRST** for a missing piece (USER 2026-11-21: *"tutors fill in first for the 2 combo pieces, though they take up draws to do so"*): while no Worldfire is held, the best tutor (Mystical Tutor > Personal Tutor) fills this quota; a second tutor fills FINISHER the same way when it is missing too (both tutors find Rift Bolt, a sorcery; neither finds Halberdier). A tutor ranks just below the real card. **It costs a draw:** the tutored card goes on top, so the projection treats the next draw as that card (no other new card from it), and the piece is in hand only after that draw or a cantrip | 1 | a second Worldfire is DEAD (USER: *"we don't need duplicate Worldfire"*) |
| **FINISHER** | Rift Bolt in hand; Keldon Halberdier in hand | 1, met by: a Rift Bolt pending on suspend; or a Halberdier pending that arrives on or after T\*+1; or one Rift Bolt in hand; or one Halberdier in hand (suspended now it arrives on now+4, so it suits a go-off on now+3 or later) | a second Rift Bolt is DEAD (USER: no duplicate Rift Bolt). With both in hand and nothing pending: Halberdier first when T\* >= now+3 (it keeps the go-off need at 9), Rift Bolt first when T\* is sooner (the Halberdier would arrive too late). With a Halberdier pending, the Rift Bolt drops to BACKUP below (USER: *"rift bolt is not a high-priority target if you have a Halberdier"*) |
| **MANA: Bloom** | Lotus Bloom in hand | **only a Bloom that makes the go-off EARLIER**: project T0 without the Blooms in hand; keep one only when T0 is later than now+3 (or "far"), i.e. when its arrival on now+3 pulls the go-off forward; a 2nd only if T\* is still "far" with the first | **a Bloom near the combo turn is dropped** (USER 2026-11-21: *"Bloom should be dropped if we are anywhere near the combo turn."*): with T0 <= now+3 it is the first overflow card shed after DEAD |
| **MANA: lands** | every land | the land drops through T\* (one per turn, net of a drop already made this turn) | keep order below |
| **MANA: rituals** | Seething Song, Irencrag Feat | the rituals the projection uses | a second Irencrag Feat is DEAD (USER: no duplicate Irencrag); Song is kept over Feat (cheaper, and does not end the turn) |
| **DIG** | Brainstorm, Ponder, Preordain, Overmaster (tutors are not dig: they fill COMBO / FINISHER first; a tutor beyond the missing pieces whose only target is an Irencrag Feat ranks with the rituals) | **While short: 1 + the number of missing pieces (COMBO and FINISHER count one each), at least 2 -- so 2 with one piece missing, 3 with both -- and dig beyond that quota is still kept above surplus mana (USER 2026-11-21: *"If we are missing combo pieces holding multiple dig is potentially worthwhile."*); 1 once both are filled; 0 once COMBO + FINISHER + MANA are all filled. A piece filled only by a TUTOR counts as missing here until it is in hand** (USER 2026-11-21: *"The other advantage of keeping draw is that it can help get the tutored cards."*): a cantrip cast after the tutor draws the tutored card the same turn, which is what the projection credits when it shortens the tutor's wait. While a combo piece is missing the dig is PRIORITISED (USER 2026-11-21: *"We should also prioritize keeping some dig if we are short combo pieces."*): dig slot 1 ranks directly below COMBO / FINISHER and ABOVE every MANA card; slot 2 ranks above every mana card except the next land drop | fill order: Brainstorm > Ponder > Preordain > Overmaster (Brainstorm also gets two cards out of the hand) |
| **BACKUP** | **the second finisher of the OTHER type** (a Rift Bolt beside a held or pending Halberdier, or a Halberdier beside a held or pending Rift Bolt); 1 extra land beyond the drops; 1 extra ritual; a 2nd Halberdier | 0 (kept only after every quota) | reverse of that order: the other-type finisher is the LAST overflow card shed (USER 2026-11-21: *"we should keep the second Finisher (of the second type) before any dead cards, just in case the arithmetic changes"*) |
| **DEAD** | Memory Lapse (goldfish-inert); a 2nd Worldfire / Rift Bolt / Irencrag Feat; a tutor with nothing missing (`TutorNeedsCandidate`: it would not be cast) | 0 | always first |

**Land keep order** (best first): Sandstone Needle (RR, the red the deck needs) > Steam Vents > Scalding Tarn >
Flooded Strand (the fetches also shuffle away a Brainstorm put-back) > Mountain > Mercadian Bazaar (only while
T\* is 2+ turns away, so it can charge; otherwise after Mistrise) > Saprazzan Skerry (UU: generic only) > Island >
Mistrise Village. A tapped land (Needle, Skerry, Bazaar) is only worth its yield before T\*; on T\*-1 or later an
untapped land ranks above it.

### Global shed order of the overflow (most expendable first)

1. **DEAD:** Memory Lapse, then a 2nd Worldfire / Irencrag Feat / same-type Rift Bolt, then a tutor with nothing
   missing. (A finisher of the other type is never dead: it is BACKUP.)
2. A Lotus Bloom near the combo turn (T0 <= now+3).
3. Lands beyond the drops + 1.
4. Rituals beyond the projection + 1.
5. Dig beyond its quota (reverse fill order: Overmaster first).
6. BACKUP: a 2nd Halberdier, the extra ritual, the extra land, and last the second finisher of the other type.

When only quota cards remain, shed in reverse importance. **Short a combo piece** (COMBO or FINISHER unfilled):
the last-needed land, then rituals, then Bloom, then the next land drop, then dig slot 2, then dig slot 1, then
FINISHER. **Nothing missing:** DIG first, then the last-needed land, then rituals, then Bloom, then FINISHER.
**Worldfire (or the tutor standing in for it) is never shed while any other card is in hand.**

## Open questions (defaults taken above; all the USER's)

1. **Halberdier vs Rift Bolt as the finisher in hand.** Default: Halberdier first when the projected go-off is
   3+ turns away (it costs nothing on the go-off turn, so the need stays 9), Rift Bolt first when it is sooner. Or
   always Rift Bolt first?
2. **Dig quota 2-3 / 1 / 0.** Is 1 + missing pieces (2 or 3) right while short, and should Brainstorm count double (it also
   clears two junk cards)?
3. **Tutor stand-in.** ANSWERED (USER: tutors fill in first for both pieces, at a draw's cost; and *"The other
   advantage of keeping draw is that it can help get the tutored cards."*). So a tutor-filled piece still counts as
   SHORT for the DIG quota until the piece is in hand: the cantrip that draws the tutored card this turn is what
   removes the tutor's wait.
4. **Extra land and extra ritual in BACKUP (1 each).** Cyclone keeps 2 extra lands; this deck's mana is mostly
   one-shot (depletion lands, Bloom, rituals), so 1 seemed enough.
5. **Tapped lands late.** Needle / Skerry / Bazaar ranked below untapped lands from T\*-1 on: right?
6. **Projection horizon now+4** before calling T\* "far".
7. **Ponder shuffle threshold.** Shuffle only when NONE of the three fills a quota (default), or when the top card
   does not?

## Build and measurement plan

Behind `EnvOn` flags in `WorldfireProvider`, **default OFF until the USER approves the buckets**: one for the
cleanup discard, one each for the Brainstorm put-back, the Ponder disposition and the Preordain scry (all reading
the one ranking). A/B on the prune blocks (seed 9100001 x 64, held-out 9200001 x 96, d5 b20, profile attached,
paired), plus units/wall; every worse game attributed by a one-lever flip at b200.
