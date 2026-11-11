# Analysis ledger — Morph

Deck: `decks/Morph/Morph.cod` (R/G morph-pump aggro, 60 cards, no sideboard):
4 Blistering Firecat, 2 Hystrodon, 4 Den Protector, 1 Spellskite; 4 Birds of Paradise,
4 Ignoble Hierarch; 4 Berserk, 3 Temur Battle Rage, 4 Might of Old Krosa, 2 Vines of Vastwood,
4 Legion Leadership // Legion Stronghold (MDFC); 4 Wild Defiance; lands 7 Forest, 1 Mountain,
4 Stomping Ground, 4 Gruul Turf, 1 Rootbound Crag, 1 Fire-Lit Thicket, 2 Wooded Foothills.

The USER asked for the analysis on 2026-11-11 ("Please analyze the morph deck.").

Branch/worktree: `morph-analysis` @ `.claude/worktrees/morph` (cut from origin
`phase-1-2-deck-analyzer` 6b3dea976). The deck folder was COPIED from the primary checkout
(untracked there; original left in place). Scryfall JSONs: `build/scratch/morph/scry/` (primary
build volume).

**The plan (what the deck is trying to do).** Get a creature onto the battlefield early (a dork-
accelerated face-down morph for {3}, or a cheap Den Protector), then turn Blistering Firecat face up
for {R}{R} (a 7/1 trample haste that is sacrificed at end of turn — but a face-down Firecat that
entered last turn is not summoning sick, so the unmorph swings at once) and stack pump spells:
Berserk (+X/+0, X = power), Legion Leadership (double power, first strike), Temur Battle Rage
(double strike), Might of Old Krosa (+4/+4 in a main phase), Vines (+4/+4 kicked). Every targeting
instant on our creature also triggers Wild Defiance (+3/+3 each). Ignoble Hierarch's exalted adds
+1/+1 to a lone attacker.

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells, never blocks, never targets. Opponent-facing clauses
  (trample, Den Protector's "can't be blocked by smaller", Vines' "can't be the target of spells your
  opponents control", Spellskite's redirect, Legion Leadership's first strike) are goldfish-inert —
  model what is cheap to model, and every deferral is PROVISIONAL until the USER signs off.
  Self-binding clauses bind us: Firecat's end-step sacrifice, Berserk's end-step destroy, megamorph's
  +1/+1 counter, Den Protector's regrowth (OUR graveyard).
- No greedy / heuristic substitute inside the search window (user hard rule). Choices a card
  introduces are SEARCHED, provider-narrowed (restrict/order only), or collapsed by a stated sound
  dominance fold, and surfaced to the viewer.
- Cards are generic (cards.json is deck-agnostic); morph is an ENGINE mechanic usable by any card.
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present,
  never adopt unilaterally.
- One heavy job (build or batch) at a time; total threads <= 24.
- Agents: no `cd` in compound commands, no `rm` / `rm -rf`, no `timeout`. Direct `mtg` runs must
  pass `--cards-json <worktree>/src/cards/data/cards.json` (the primary's cards.json is otherwise
  loaded).
- Subagents never push the shared branch; over ~250K context they write a handoff and stop.

## Stage 1 — coverage (2026-11-11)
Missing (11): Blistering Firecat, Hystrodon, Den Protector, Spellskite, Berserk, Temur Battle Rage,
Might of Old Krosa, Vines of Vastwood, Legion Leadership // Legion Stronghold, Wild Defiance,
Fire-Lit Thicket. Present (8): Birds of Paradise, Ignoble Hierarch, Gruul Turf, Forest, Mountain,
Stomping Ground, Rootbound Crag, Wooded Foothills (re-verify against Scryfall in Stage 2).
Fetch-hold review required (Wooded Foothills; hold is modelled).

**No morph support exists in the engine** (no face-down casting, no turn-face-up action). That is the
Tier-3 centre of this analysis.

## Stage 2 — cards (in progress)
Research fan-out launched 2026-11-11: (R1) morph/megamorph engine design — Firecat, Hystrodon, Den
Protector; (R2) pumps + targeting triggers + remaining cards — Wild Defiance, Berserk, Temur Battle
Rage, Might of Old Krosa, Vines, Legion Leadership MDFC, Spellskite, Fire-Lit Thicket, and a
re-verify of the 8 present cards. Drafts: `logs/morph/research/*.md` (gitignored; decisions below are
the durable record).

## Approved deferrals
(none yet)
