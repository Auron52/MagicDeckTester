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
