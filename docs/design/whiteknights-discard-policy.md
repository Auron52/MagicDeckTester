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
- Sol Ring (`mana_rock`) and Remote Farm (`produces_amount` 2) count as mana: the land quota counts
  mana UNITS (3, or 4 with a held 4-drop), a rock is a mana source, and among kept sources the bigger
  one is preferred (fewer cards for the same mana). In the fill a kept Sol Ring is cast on the first
  turn it is affordable and nets its output less its cost that turn; Remote Farm enters tapped.
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
4. Remote Farm's depletion is not modelled (a fresh Farm lasts exactly the 3-turn window).

Note for the routing comment in `DecisionProviders.h`: WhiteKnights now holds a judgement hook of its
own, so a misroute onto KnightsProvider would lose these extras (it is no longer play-neutral).
