# Worldfire: bucketed keep order (discard + Brainstorm / Ponder / Preordain) -- DESIGN, for USER review

Status: **proposed 2026-11-21, amended by the USER the same day (other-type finisher kept above dead cards; dig
prioritised while short a combo piece; Bloom dropped near the combo turn; multiple dig while short; tutors fill the two pieces first; every wait counted in the projection; dig kept to draw tutored cards, below the go-off mana; Ponder shuffle searched; Preordain aggressive while a piece is missing; MANA fill order: colours, next land drop, castable acceleration; 1 blue always, 2 often; Bazaar only early unless far short on mana; USER answers to Q1/Q2/Q4); not adopted.** Discard buckets are USER-owned (analyze-deck Stage 5i); this is the
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
| Brainstorm put-back | the ranking says what goes on top: the two lowest-ranked cards of the post-draw hand (USER: *"for Brainstorm this tells us what we want to put on top, (the lowest priority cards using this rule)"*); the higher-ranked of the two on top (it is drawn first) |
| Ponder | the ranking fixes the ORDER of the top three (best first, ranked together with the hand); **keep-in-that-order vs shuffle stays SEARCHED** (USER 2026-11-21: *"for Ponder it tells us how to order the top 3 ... (though we may leave shuffle searched)"*) -- the `ReorderKeepShuffleOnly` shape: two options, not six orders plus shuffle |
| Preordain | scry 2: the ranking decides what goes to the bottom and the order of what stays. Normally keep a card on top iff it fills a still-unfilled quota (counting cards kept above it), best first; bottom the rest. **When a piece is genuinely missing** (no card and no tutor for it -- above all Worldfire), **bottom AGGRESSIVELY**: keep only the missing piece or a tutor that finds it, and bottom everything else, mana and dig included (USER: *"if you are really missing a piece like worldfire you need to be aggressive about bottoming other cards with Preordain"*) |

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
  Worldfire must come BEFORE it arrives (it would exile it otherwise): T\* <= arrival-1, and the win lands on the
  arrival turn. Waiting after Worldfire for the Halberdier is allowed: the opponent is at 1 with nothing, and under
  the free-turn rule those turns are free (USER 2026-11-21, below).
- **Mana ready:** as below, with each wait counted -- tapped lands from the turn after they are played, a Bloom 3
  turns after it is suspended, Bazaar one storage counter per turn it is left to charge, one land drop per turn.
- T\* = the earliest turn on which all three hold. A wait on any one piece pushes T\*; the quotas are then
  computed against that T\*, so e.g. a slow finisher also slackens the mana quota.

Mana detail:

- **Need:** 9 with RRR for Worldfire, **+1 R** when the finisher is a Rift Bolt that must be suspended that turn
  (no Rift Bolt and no Halberdier pending). **Never more than 10** (USER 2026-11-21: *"In this situation we never
  need more than 10 mana to go off. We don't need extras except perhaps to play a cantrip or Mystical Tutor prior
  to comboing off that turn."*): the only addition is {U} for a cantrip / Personal Tutor we must cast THAT turn to
  find a piece. **Mystical Tutor** (USER 2026-11-22, correcting the earlier wording: *"casting it in the upkeep in order to go off
  on the current turn is a reasonable play, though it has no upside to spending spare blue (not from a depletion
  land) from the last turn so we have more mana available to go off."*): the best timing is the PREVIOUS turn,
  paid with spare blue from a non-depletion source (a depletion counter spent then is mana lost from a later turn),
  which leaves the whole go-off turn's mana for the combo -- the projection charges it nothing on T\* in that case.
  With no such spare blue, casting it in our upkeep of T\* (the tutored card is drawn that draw step) is still
  reasonable, and costs {U} of T\*'s mana: the need on T\* rises by 1.
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
| **FINISHER** | Rift Bolt in hand; Keldon Halberdier in hand | 1, met by: a Rift Bolt pending on suspend; or a Halberdier pending that arrives on or after T\*+1; or one Rift Bolt in hand; or one Halberdier in hand (suspended now it arrives on now+4, so it suits a go-off on now+3 or later) | a second Rift Bolt is DEAD (USER: no duplicate Rift Bolt). With both in hand and nothing pending: Halberdier first when T\* >= now+3 (it keeps the go-off need at 9), Rift Bolt first when T\* is sooner (the Halberdier would arrive too late) -- USER: *"I think your rule is okay, though we'll have to test it out and see if it needs refinements."* **Plus the 9-before-10 case:** project T9 (the first turn with 9 mana) and T10 (with 10); when T9 < T10 and the Halberdier can still be suspended, Halberdier first even if we then wait a turn or more after Worldfire for it to land (USER: *"the one other case where the Halberdier might end up better is when we can hit 9 mana before 10 and have some time to get the Halberdier started ... we are better off using Worldfire with 9 mana even if we need to wait an extra turn after for the Halberdier to land, the result is still better on average, since some of those turns after worldfire will count as free with the new changes."*). With a Halberdier pending, the Rift Bolt drops to BACKUP below (USER: *"rift bolt is not a high-priority target if you have a Halberdier"*) |
| **MANA: Bloom** | Lotus Bloom in hand | **only a Bloom that makes the go-off EARLIER**: project T0 without the Blooms in hand; keep one only when T0 is later than now+3 (or "far"), i.e. when its arrival on now+3 pulls the go-off forward; a 2nd only if T\* is still "far" with the first | **a Bloom near the combo turn is dropped** (USER 2026-11-21: *"Bloom should be dropped if we are anywhere near the combo turn."*): with T0 <= now+3 it is the first overflow card shed after DEAD |
| **MANA: lands** | every land | the land drops through T\* (one per turn, net of a drop already made this turn) | keep order below |
| **MANA: rituals** | Seething Song, Irencrag Feat | the rituals the projection uses | a second Irencrag Feat is DEAD (USER: no duplicate Irencrag); Song is kept over Feat (cheaper, and does not end the turn) |
| **DIG** | Brainstorm, Ponder, Preordain, Overmaster (tutors are not dig: they fill COMBO / FINISHER first; a tutor beyond the missing pieces whose only target is an Irencrag Feat ranks with the rituals) | **While short: 1 + the number of missing pieces (COMBO and FINISHER count one each), at least 2 -- so 2 with one piece missing, 3 with both -- and dig beyond that quota is still kept above surplus mana (USER 2026-11-21: *"If we are missing combo pieces holding multiple dig is potentially worthwhile."*); 1 once both are filled; 0 once COMBO + FINISHER + MANA are all filled. A piece filled only by a TUTOR counts as missing here until it is in hand** (USER 2026-11-21: *"The other advantage of keeping draw is that it can help get the tutored cards."*): a cantrip cast after the tutor draws the tutored card the same turn, which is what the projection credits when it shortens the tutor's wait. **But that slot ranks BELOW every MANA quota card** (USER: *"But I wouldn't say that it is worth keeping over ensuring we have the mana to go off."*): the dig kept to draw a tutored card sits after the mana the projection needs and above surplus mana; the "slot 1 above every mana card" priority applies only to a piece that is genuinely missing (no card, no tutor). While a combo piece is missing the dig is PRIORITISED (USER 2026-11-21: *"We should also prioritize keeping some dig if we are short combo pieces."*): dig slot 1 ranks directly below COMBO / FINISHER and ABOVE every MANA card; slot 2 ranks above every mana card except the next land drop | fill order: **Brainstorm > Ponder when we hold useless cards (DEAD or overflow) AND a shuffle effect (an uncracked fetch in hand or on the battlefield, or a Ponder); otherwise Ponder > Brainstorm**; then Preordain > Overmaster (USER 2026-11-21: *"I would keep it as-is, though we might want to prioritize Brainstorm if we have useless cards and shuffle effects and Ponder otherwise."*) |
| **BACKUP** | **the second finisher of the OTHER type** (a Rift Bolt beside a held or pending Halberdier, or a Halberdier beside a held or pending Rift Bolt); a 2nd Halberdier. **No extra mana:** lands and rituals beyond the projection are overflow, never BACKUP (USER: never more than 10 needed) | 0 (kept only after every quota) | reverse of that order: the other-type finisher is the LAST overflow card shed (USER 2026-11-21: *"we should keep the second Finisher (of the second type) before any dead cards, just in case the arithmetic changes"*) |
| **DEAD** | Memory Lapse (goldfish-inert); a 2nd Worldfire / Rift Bolt / Irencrag Feat; a tutor with nothing missing (`TutorNeedsCandidate`: it would not be cast) | 0 | always first |

**Inside MANA** (USER 2026-11-21: *"We probably should have similar rules about what we keep in the mana camp.
Keeping the next land drop is important, though not much really beats the fast acceleration unless we are unable to
cast it. We need to ensure we have the colours we need, also."*). Fill order of the MANA quota, best first:

1. **Colours first.** The cards that, with the board, give the colours the plan needs by T\*: **R** -- one red
   source to cast Seething Song or suspend Rift Bolt, three red for Irencrag Feat ({1}{R}{R}{R}); Worldfire's RRR
   can come from a red ritual's output (Song adds RRRRR, Feat seven R), so a castable ritual covers it -- and **U**:
   the combo is all red, but the dig and tutors are blue (Brainstorm, Ponder, Preordain, Mystical / Personal Tutor),
   so **at least one blue source always, and a second is good** (USER 2026-11-21: *"The combo itself is all red,
   but we usually need to blue for dig or tutors, so we need at least one blue and often 2 are good."*). The first
   U source is in this colour tier; a second U source (wanted while we hold two or more blue spells, or expect to
   cast two in one turn) ranks first among the further land drops (tier 4). Saprazzan Skerry's UU covers both from
   one land for a turn; Steam Vents and the fetches count as either colour, not both at once. Sandstone
   Needle counts RR, a fetch counts as the colour it can fetch, Mercadian Bazaar as R once charged.
2. **The next land drop** (one land, the best by colour need, then the land keep order below), unless one has
   already been played this turn.
3. **Fast acceleration that we CAN cast by T\*:** Seething Song (needs 3 mana incl. R before it), Irencrag Feat
   (needs 4 incl. RRR, and must be the last spell before Worldfire), a Lotus Bloom that makes the go-off earlier
   (the Bloom rule above). Kept above every further land drop: *"not much really beats the fast acceleration"*.
   **Uncastable acceleration** (the board plus the kept land drops cannot pay for it by T\*) ranks BELOW the
   further land drops, and above surplus mana only if the further drops would make it castable.
4. **Further land drops** through T\*, in the land keep order.

**Land keep order** (best first): Sandstone Needle (RR, the red the deck needs) > Steam Vents > Scalding Tarn >
Flooded Strand (the fetches also shuffle away a Brainstorm put-back) > Mountain > Mercadian Bazaar (ONLY EARLY: on our turns 1-2, when it can charge for several turns; from turn 3 on it is
**the last land of all, below Mistrise Village, and the first land shed** -- USER 2026-11-21: *"Mercadian Bazaar
should be greatly deprioritized if we are not early in the game. It is pretty slow if you don't drop it early."* **Exception: really short on mana** -- T\* is "far" (no go-off within the horizon) -- the Bazaar keeps its
normal place as a land drop even late, *"just because it eventually builds up"* (USER, same day).) > Saprazzan Skerry (UU: generic only) > Island >
Mistrise Village. **Tapped lands** (Needle, Skerry, Bazaar) are judged by WHEN we can play them (USER 2026-11-21: *"If we can play
them this turn that is incorrect, but if we only have the option to play them next turn and have the ability to go
off then then we don't need them."*): a tapped land we can still play THIS turn keeps its place (it is untapped by
the go-off turn); a tapped land whose earliest possible play is the go-off turn T\* itself (this turn's drop
already made or taken by a better land, and we can go off next turn) contributes nothing to T\* and is overflow.

### Global shed order of the overflow (most expendable first)

1. **DEAD:** Memory Lapse, then a 2nd Worldfire / Irencrag Feat / same-type Rift Bolt, then a tutor with nothing
   missing. (A finisher of the other type is never dead: it is BACKUP.)
2. A Lotus Bloom near the combo turn (T0 <= now+3).
3. Lands beyond the drops.
4. Rituals beyond the projection.
5. Dig beyond its quota (reverse fill order: Overmaster first).
6. BACKUP: a 2nd Halberdier, and last the second finisher of the other type.

When only quota cards remain, shed in reverse importance. **Short a combo piece** (COMBO or FINISHER unfilled):
the last-needed land, then rituals, then Bloom, then the next land drop, then dig slot 2, then dig slot 1, then
FINISHER. **Nothing missing:** DIG first, then the last-needed land, then rituals, then Bloom, then FINISHER.
**Worldfire (or the tutor standing in for it) is never shed while any other card is in hand.**

## Open questions (defaults taken above; all the USER's)

1. **Halberdier vs Rift Bolt:** ANSWERED -- the go-off-distance rule stands (to be tested and refined), plus the
   9-before-10 case.
2. **Dig quota:** ANSWERED -- as is; Brainstorm first with useless cards + a shuffle effect, Ponder otherwise.
3. **Tutor stand-in.** ANSWERED (USER: tutors fill in first for both pieces, at a draw's cost; and *"The other
   advantage of keeping draw is that it can help get the tutored cards."*). So a tutor-filled piece still counts as
   SHORT for the DIG quota until the piece is in hand: the cantrip that draws the tutored card this turn is what
   removes the tutor's wait.
4. **Extra mana:** ANSWERED -- none; never more than 10, plus {U} for a same-turn cantrip / Personal Tutor, and
   {U} for a Mystical Tutor cast in T\*'s upkeep (nothing when it was cast the turn before with spare
   non-depletion blue).
5. **Tapped lands late:** ANSWERED -- judged by when they can be played (above).
6. **Projection horizon now+4:** ANSWERED -- *"I suppose that makes sense."*
7. **Ponder shuffle:** ANSWERED -- keep-vs-shuffle stays searched; only the order is fixed by the ranking.
8. **Preordain aggression:** ANSWERED -- dig spells go to the bottom too (USER: *"I think dig spells going to the
   bottom is correct from what we have found with other decks. this is something you can feel to experiment with,
   though. Arguably it could be incorrect when we have multiple blue mana remaining, nothing else to use it for and
   no ability to combo if we find the right piece, but our tests in Hinata generally showed that it was better to
   bottom. I think either one is correct."*). Default: bottom. EXPERIMENT ARM: keep a dig spell on top when we have
   2+ blue mana left this turn, nothing else to spend it on, and no way to combo this turn even if the piece is
   found. Either missing piece (Worldfire or finisher) triggers the mode (agent's reading of "either one is correct").
9. **Bazaar "early" = turns 1-2:** ANSWERED -- *"That seems fine to me. In the best case scenario the bazaar taps
   for 2 on turn 4 if played on turn 1. Turn 3 would be 1 mana on turn 5 and 2 on turn 6. Neither is particularly
   inspiring, though it should be fine to experiment if you are unsure."* Optional arm: early = turn 1 only.

## Build and measurement plan

Behind `EnvOn` flags in `WorldfireProvider` (built default OFF; **turned ON 2026-11-22** with the deadline order,
see `analysis-Worldfire.md` "Adoption"): one for the
cleanup discard, one each for the Brainstorm put-back, the Ponder disposition and the Preordain scry (all reading
the one ranking). A/B on the prune blocks (seed 9100001 x 64, held-out 9200001 x 96, d5 b20, profile attached,
paired), plus units/wall; every worse game attributed by a one-lever flip at b200.
