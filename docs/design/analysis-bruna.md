# Analysis ledger — Bruna

Deck: `decks/Bruna/Bruna.cod` (Bant Auras/Equipment voltron around Bruna, Light of Alabaster;
Glittering Wish into an 8-card sideboard). Branch/worktree: `bruna-analysis` @ `/home/vscode/wt/bruna`
(cut from origin c34312e8). Worktrees live OUTSIDE /tmp (a /tmp wipe lost work on 2026-10-05).

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells or blocks; opponent-facing triggers are inert, but
  SYMMETRIC / self-binding clauses bind us. Model the full card; never drop a self-affecting clause.
  Goldfish-inert deferrals are PROVISIONAL until the user signs off.
- No greedy / heuristic substitute inside the search window (user hard rule). Choices a card
  introduces (targets, auras to attach, modes, X, wish picks) are SEARCHED or surfaced to the viewer.
- A legal line the engine cannot express at any budget is a DEFECT, never a disclosed gap.
- Cast order / range / main-split are USER-OWNED: author and present, never adopt unilaterally.
- Memory safety (OOM on 2026-10-05): one heavy job (build or batch) at a time, --threads 20.

## Stage 1 — coverage (2026-10-05)
Missing (20): Eldrazi Conscription, Somberwald Sage, Bruna Light of Alabaster, Prodigious Growth,
Skycloud Expanse, Seaside Citadel, Glittering Wish, Mythic Proportions, Colossification, Arcanum
Wings, Botanical Sanctum, Avacyn's Pilgrim, Boseiju Who Endures, Mother of Runes, Open the Armory,
Indrik Umbra, Almost Perfect, Unflinching Courage, Elgaud Shieldmate, Worldfire.
Present: Lightning Greaves, Birds of Paradise, Forest, Azorius Chancery, Remote Farm, Sol Ring,
Razorverge Thicket, Wild Growth.

SCAN BUG: the coverage tool marks the whole sideboard reachable via Glittering Wish, but Glittering
Wish fetches only MULTICOLORED cards. Reachable: Bruna, Indrik Umbra, Almost Perfect, Unflinching
Courage. NOT reachable: Mythic Proportions, Avacyn's Pilgrim (both also mainboard), Elgaud Shieldmate,
Worldfire (sideboard-only, never castable in a game here -> not implemented; analyze_deck.py's
wish detection to be fixed to honour the wish's restriction).

## Stage 2 — cards
(drafts in logs/bruna_drafts/, integrator fills this table)
