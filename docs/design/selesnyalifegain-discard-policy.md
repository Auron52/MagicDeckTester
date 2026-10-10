# SelesnyaLifegain cleanup-discard policy (USER-settled 2026-10-09)

No AI proposal existed for this deck; the design below was drafted by an agent during the
2026-10-09 review and settled by the USER the same day. USER rulings are quoted and marked USER;
agent choices are marked "agent default" and surfaced for the USER to overturn.

Implementation contract (all decks): a per-deck `XProvider::CleanupDiscardCandidates` override
returning the full shed order (most expendable first, every non-staged hand card named), routed
through `CleanupDiscardRankingWithOrder`, behind a default-ON `EnvOn("MTG_<DECK>_BUCKET_DISCARD",
true)`; `=0` restores `GenericProvider::CleanupDiscardCandidates` (max-MV). Quotas count the board
("net of board"); within a bucket, distance to castable orders the cards (EV = P(play) x value).

### SelesnyaLifegain: no proposal, no shed measurement. G/W Elf ramp + lifegain payoffs + Genesis Wave
### / Craterhoof. 20 lands (Forest 5, Brushland 4, Branchloft 4, Sanctuary 2 [Karoo], Sands 2, Blighted
### Steppe 2, Wirewood Lodge 1); 17 mana creatures (Llanowar 4, Mystic 3, Priest of Titania 4,
### Archdruid 3, Accomplished Alchemist 3); lifegain Feed the Clan 3, Wellwisher 2; payoffs Genesis
### Wave 4, Ageless Entity 4, Nykthos Paragon 4, Verdant Sun's Avatar 4, Bogbeast 1, Craterhoof 1.
### required_pieces empty. Provider is an empty DeckProvider (it once rode StompyProvider by misroute).
Agent draft (Giants ramp shape): LANDS 3 counting board, >= 1 W source while holding Paragon (WW);
dead Sanctuary first. ELVES = mana: keep every castable Elf up to a mana target counting their
output (Priest / Archdruid scale with Elves); order Priest > Archdruid > Llanowar = Mystic > Alchemist.
LIFEGAIN 1 (Feed the Clan > Wellwisher) only while a lifegain payoff is held or in play. PAYOFFS 2 +
soft 3rd, best first discounted by distance: Genesis Wave > Craterhoof > Paragon > Ageless > Avatar >
Bogbeast.
USER answers 2026-10-09: "We need 2 white for paragon. We should keep it if we can. I agree that elves
are the primary mana, though." Payoffs: "It should ideally be done based on how castable and easily
enabled. Genesis wave you want when you have a lot of mana or a great curve to get there soon. You
want to keep a copy of Paragon preferably if you can get there sooner and enable it." Bogbeast: "also
great, because it pumps your board. It is actually one of the best in goldfish (but a bit weaker to
removal in real games)."
-> LANDS 3 counting board; TWO W sources while holding a Paragon (W-producing lands; Alchemist's any
colour counts); Elves are the primary mana (draft kept). PAYOFFS ranked by castability x
enablement, not a fixed list: a lifegain payoff (Paragon, Ageless) is "enabled" only with a lifegain
source in hand or play (Feed the Clan, Wellwisher, Blossoming Sands, Blighted Steppe, Avatar,
Bogbeast); one Paragon preferred when it is reachable soon (6 mana incl. WW) and enabled; Bogbeast
ranks near the top (self-enabling team pump); Genesis Wave kept only with lots of mana or a curve
that gets there soon (threshold asked).


#### SelesnyaLifegain: SETTLED (USER, 2026-10-09)
- LANDS 3 counting board; TWO W sources while holding a Paragon; dead Selesnya Sanctuary first.
- ELVES are the primary mana: keep castable Elves toward the mana target, counting their output
  (Priest / Archdruid scale with Elves); Priest > Archdruid > Llanowar = Mystic > Alchemist.
- LIFEGAIN 1 (Feed the Clan > Wellwisher) while a lifegain payoff is held or in play.
- PAYOFFS 2 + soft 3rd, ranked by castability x enablement; equal ones Bogbeast > Paragon > Craterhoof
  > Ageless > Avatar (USER). Paragon / Ageless count as enabled only with a lifegain source in hand or
  play; keep one Paragon when reachable soon and enabled. GENESIS WAVE kept only with 8+ mana within 2
  turns (X = 5) (USER).
- SHED: dead Sanctuary -> surplus lands -> lifegain with no payoff -> payoffs not reachable / not
  enabled (Wave below 8 mana first) -> surplus Elves beyond the target -> keep tail.

#### Implementation notes (2026-10-09)
Implemented as `SelesnyaLifegainProvider::CleanupDiscardCandidates` (src/ai/DecisionProviders.cpp,
its first judgement hook) behind `MTG_SELESNYA_BUCKET_DISCARD` / heurarm `SELESNYA_BUCKET_DISCARD`,
default ON (`=0` restores the generic max-MV ranking exactly). Tests:
`test/unit/test_selesnyalifegain_discard.cpp`. Every role is keyed on card PARAMS.

* CASTABILITY = a two-turn projection. M1 = board lands (produces_amount) + board mana dorks + the
  first kept land drop; M2 = board lands + two kept land drops + board dorks + the BEST next-turn
  deployment of the castable hand Elves within M1 (bounded exhaustive over the first 8 offered; REVIEW
  FIX S-3 2026-10-09 -- it was greedy in keep order, so an Archdruid ate M1 = 3 for M2 = 4 where
  Llanowar + Mystic make 5), Priest / Archdruid re-counted over the new Elves. A payoff is distance 1
  off M1, 2 off M2 (Paragon also needs 2 W sources by then), else unreachable, and then its GAP -- the
  mana plus W sources it is still short at M2 -- is its distance to castable.
  Ranking = (reachable and enabled) first, nearer first, then the USER's ties.
* KEEP LADDER (agent default, the Giants ramp shape; most kept first): land > Elf > payoff > land >
  Elf > payoff > land (> W lands kept past 3 for a Paragon) > lifegain > the other kept Elves > the
  soft 3rd payoff. The land slots hold the first min(kept, LANDS-3 need) lands; a W land kept only for
  the Paragon is one of the "past 3" extras (REVIEW FIX S-4: the code used to put it in the top land
  slots, against this ladder).
* SHED: dead Sanctuary (then a dead legend) > unclassified > surplus lands > lifegain with no payoff
  (and surplus lifegain) > payoffs not reachable / not enabled (Waves first) > surplus castable
  payoffs past 2 + soft 3rd > surplus Elves (those not castable next turn first) > the ladder
  backwards. Every non-staged hand card is named.

Agent readings (not USER-ruled; each is a one-line change if overturned):
1. "Held" is LITERAL (in hand, reachable or not) for "a lifegain payoff is held", the Paragon W rule
   and "a lifegain source in hand". That makes the order NOT prefix-stable (shed an unreachable
   Paragon and its Feed the Clan becomes lifegain with no payoff), so the provider declares
   `CleanupDiscardShedStable() == false` while the policy is on: the engine re-asks after each shed,
   i.e. the rule applied card by card (a test pins this). The alternative reading -- only KEPT payoffs
   count -- would be stable and would shed that Feed before the unreachable Paragon.
2. The Paragon W rule may keep W lands BEYOND the 3 ("We should keep it if we can"); W sources are W
   lands (a Pathway's W back face included) and any-colour dorks IN PLAY (Alchemist); a hand Alchemist
   does not count until it resolves.
3. Every land drop adds 1 (a Karoo nets +1 after its bounce; enters-tapped is not modelled);
   Accomplished Alchemist yields 1 (its X is this turn's lifegain); Wirewood Lodge's untap is not
   counted.
4. THE MANA TARGET (the design names none) = the dearest kept payoff's cost (Genesis Wave = 8), floor
   5 (the cheapest payoffs) when none is kept. Castable Elves (castable next turn) are kept in the USER's
   order until M2 reaches it; the rest are surplus.
5. A Blossoming Sands is a lifegain source in hand only (its gain is the ETB); a Blighted Steppe counts
   in hand or play. "Lifegain payoff" for the LIFEGAIN rule = Paragon / Ageless, and (REVIEW FIX S-5,
  verified in cards.json: Blossoming Bogbeast's `attack_team_pump_per_life_gained` -- "+X/+X where X is
  the amount of life you gained this turn") Bogbeast, so a Feed the Clan beside a held / in-play
  Bogbeast has a payoff. Bogbeast is still NOT gated for enablement (its own attack gains 2 first).
  Not extended to Accomplished Alchemist (its mana also scales with life gained this turn) -- open.
6. Quotas net of board: payoffs in play count against 2 + soft 3rd; a Wellwisher in play fills
   LIFEGAIN 1.
7. A reachable Genesis Wave ranks right after Craterhoof among equals (the USER's tie list does not
   name it). The kept Paragon takes the 2nd HARD slot when castability alone would leave it soft or out.
8. Within "payoffs not reachable / not enabled": Waves first (USER), then the largest GAP first (REVIEW
   FIX S-1 2026-10-09, USER "based on how castable" -- the tie order alone shed a 1-short Ageless
   before an 8-drop Craterhoof); a reachable-but-unenabled payoff has gap 0 and goes last; the USER's
   order reversed (Avatar first, Bogbeast last) only on an equal gap.
9. A lifegain card that is the ONLY lifegain source enabling a kept Paragon / Ageless is laddered right
   after that payoff, so the pair gives way together.

#### USER rulings (2026-10-10) on the implementation readings

Cross-deck (verbatim):
- *"Colour beats the land count is correct."* -- a land that gives a KEPT spell a colour nothing on the
  board or already kept provides is kept even past the land quota (Creature Giving, Melira Pod and
  EldraziDisplacerFlicker already do this; every deck follows it).
- *"Creature quotas are less worried about the board than land quotas. This is because you can always add
  more creatures and sometimes this speeds up the clock."* -- creature / threat / payoff quotas are filled
  from the hand WITHOUT subtracting creatures already in play. Land and mana quotas still count the board;
  engine, combo-piece and duplicate-legend rules keep the board rules their design states.
- *"For Distance to castable I think it makes sense to have a smarter model if we can."* -- the shared
  model in docs/design/discard-distance-model.md replaces the per-deck crude proxies.

Deck-specific (verbatim):
- SelesnyaLifegain: *"For SelesnyaLifegain you want to keep the cheapest effective payoffs if mana is
  really poor."* -- when no payoff is reachable, the payoff quota is still kept, cheapest effective
  (enabled) payoffs first; poor mana does not empty the bucket.

