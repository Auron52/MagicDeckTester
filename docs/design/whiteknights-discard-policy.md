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

#### Implementation notes (2026-10-09)

Implemented as `WhiteKnightsProvider::CleanupDiscardCandidates` (`src/ai/DecisionProviders.cpp`),
which calls the shared Knights rule (`KnightsCurveShed`, see
`docs/design/knights-discard-policy-proposal.md` "Implementation notes") with the extras on, behind
`MTG_WK_BUCKET_DISCARD` (default ON; `=0` -> `GenericProvider`'s max-MV ranking, independent of
`MTG_KNIGHTS_BUCKET_DISCARD` in both directions). Tests: `test/unit/test_whiteknights_discard.cpp`.

The extras, as implemented (all param-keyed; Knights' mainboard carries none of them):
- Swords to Plowshares / Unexpectedly Absent (`CardTemplate::Removal`) shed FIRST, hand order.
- Sol Ring (`mana_rock`) and Remote Farm (`produces_amount` 2) count as mana -- as corrected in
  review (below): each counts ONE source toward the land quota (3, or 4 with a held 4-drop), and its
  real output goes into the fill's mana.
- 2nd Adeline / Basri (copy in play, or an earlier hand copy) shed right after the removal.
- Vial surplus after turn 1 -- the shared Knights rule.

Agent readings (surfaced for the USER):
1. The Knights rule's "4 while a Haytham is held" is keyed on MV >= 4, so here Hero of Bladehold and
   Kinsbaile Cavalier raise the land target to 4.
2. Value order for the bodies the USER did not rank (only the fill's last tie-break and the order
   creatures outside the fill are shed): Adeline > Kinsbaile (team double strike) > Hero of Bladehold
   > Silverblade Paladin > Acclaimed Contender with a Knight in play > Worthy Knight > Accorder Paladin
   > Contender without > the 2/1s (Basri = Venerable Knight, measured play-identical).
3. "Lords" for the fill's tie-break are the P/T anthems (Benalish Marshal, Knight Exemplar);
   Kinsbaile's double-strike grant is valued, not counted as a lord.
4. ~~Remote Farm's depletion is not modelled~~ -- modelled since review fix 3 below.

Review fixes (2026-10-09, after the integration review):
1. "COUNT AS MANA" = ONE SOURCE each (AGENT CORRECTION, the Angels precedent; was a design mismatch).
   The first implementation counted Sol Ring and Remote Farm as 2 toward the 3-land quota and kept
   them over a Plains on that count, so on board 2 Plains + hand Sol Ring, Plains, Venerable, Worthy,
   Benalish, Exemplar, Accorder, Silverblade the Plains shed first and Benalish Marshal ({W}{W}{W},
   the USER's #2 lord) was stranded. Now each source counts one toward the quota, its real output (2;
   the Ring's colourless) goes into the fill, and when the quota must choose between DIFFERENT
   sources the fill scores every distinct choice -- the one the fill's colours need wins (there: the
   Plains), ties to the keep order (fixer, more mana, untapped, hand order).
2. A REMOTE FARM IN PLAY pays only for the depletion counters it has left (one {W}{W} per turn, read
   off the Permanent), not 2 every turn of the window. Toward the quota it counts one source while it
   has 2+ uses left and none on its last counter (agent reading: it is gone after next turn).
3. THE SOL RING TURN IS HONEST: its {1} is paid out of that turn's pool (colourless first, then the
   most plentiful single colour, then wild) and its {C}{C} is added from that turn on -- previously the
   fill netted +1 colourless while keeping the white source that paid for it.

Note for the routing comment in `DecisionProviders.h`: WhiteKnights now holds a judgement hook of its
own, so a misroute onto KnightsProvider would lose these extras (it is no longer play-neutral).

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

