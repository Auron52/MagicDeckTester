# WhiteKnights cleanup-discard policy (USER-settled 2026-10-09)

No AI proposal existed for this deck; the design below was drafted by an agent during the
2026-10-09 review and settled by the USER the same day. USER rulings are quoted and marked USER;
agent choices are marked "agent default" and surfaced for the USER to overturn.

Implementation contract (all decks): a per-deck `XProvider::CleanupDiscardCandidates` override
returning the full shed order (most expendable first, every non-staged hand card named), routed
through `CleanupDiscardRankingWithOrder`, behind a default-ON `EnvOn("MTG_<DECK>_BUCKET_DISCARD",
true)`; `=0` restores `GenericProvider::CleanupDiscardCandidates` (max-MV). Quotas count the board
("net of board"); within a bucket, distance to castable orders the cards (EV = P(play) x value).

### WhiteKnights: no proposal. WhiteKnightsProvider DERIVES FROM KnightsProvider, so it INHERITS the
### settled Knights curve rule unless overridden. Measured DISCARD_INERT: zero cleanup sheds from
### EITHER caller (real + rollout) in 400 d3 games (analysis-WhiteKnights.md). Card data: mono-W
### Knights, 19 Plains + 4 Remote Farm, 1 Vial, 1 Sol Ring, 1 Swords, 1 Unexpectedly Absent, 3 Adeline
### (legendary), Basri (legendary), Silverblade (soulbond double strike), Kinsbaile Cavalier (double
### strike), Hero of Bladehold, Accorder Paladin (battle cry).
Agent draft: inherit the Knights curve rule, plus the Angels precedents -- Swords / UA shed first, Sol
Ring + Remote Farm (2 mana) count as mana, duplicate legends (2nd Adeline / Basri, in play or hand)
shed early; Vial surplus after turn 1.
#### WhiteKnights: SETTLED (USER, 2026-10-09): Knights curve rule + the extras above. Implement as a
WhiteKnightsProvider override that calls the Knights rule and adds the extras (Knights itself has no
Sol Ring / Remote Farm / Swords / UA, so the extras could equally live in the shared rule, param-keyed).
