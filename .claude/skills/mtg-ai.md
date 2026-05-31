# MTG AI Engine — Build Guide

This skill covers the design and implementation of the AI engine for MagicDeckTester. Read it when implementing AI decision-making, game logging, deterministic seeding, or opponent behaviour.

It assumes the game engine is rules-correct per `.claude/skills/mtg-rules.md`. Read that skill for data models (`GameState`, `Permanent`, `StackEntry`, etc.) and rules correctness — this skill builds on top of them.

---

## Project Context

The simulator has two phases:

**Phase 1 — Goldfishing**: One AI-controlled deck plays against a do-nothing opponent (20 life, no cards, takes no actions). The goal is to measure the average turn the deck wins. No opponent decisions are needed.

**Phase 2 — 1v1**: Two decks play against each other. Both sides need decision-making AI. This phase is explicitly flagged for discussion before implementation — the complexity increase is significant.

All implementation guidance below targets Phase 1 unless marked **[Phase 2]**.

---

## AI Decision Points

These are the moments during a game where the AI must choose between legal options. In Phase 1, only the active (goldfishing) player has decisions; the opponent passes all priority windows automatically.

| Decision Point | When | Notes |
|---------------|------|-------|
| Mulligan | Before game starts | Keep or send back opening hand |
| Land drop | Main phase | Which land to play (one per turn) |
| Spell selection | Main phase / instant windows | Which spells to cast and in what order |
| Mana payment | On cast | Which mana sources to tap; which color to produce |
| Target selection | On cast / ability activation | Which legal targets to choose |
| Attack declaration | Declare Attackers step | Which creatures to send |
| Damage ordering | Combat Damage step | Order to assign trample damage across multiple blockers |
| Modal choices | On cast | Which mode(s) to select for modal spells |
| X value | On cast | How much to pay for X in `{X}` costs |
| Activated abilities | Any priority window | Whether and when to activate |

**[Phase 2] Additional decision points**: Block declaration, discard choices for opponents, priority passing with reactive spells (instants, flash).

---

## Phase 1: Goldfishing AI

The goldfishing AI's single objective is to **win as fast as possible**. Because the opponent does nothing, the AI never needs to evaluate threats or play defensively. All decisions reduce to: *maximize progress toward winning this turn or as few turns from now as possible.*

### Mulligan Heuristic

Evaluate the opening hand before keeping:

```
keepHand(hand: Card[], onMulligan: int) -> bool:
  landCount = count cards that are lands
  nonLandCount = 7 - onMulligan - landCount

  // Hard mulligans
  if landCount == 0: return false
  if landCount >= 6: return false
  if nonLandCount == 0: return false

  // Curve check: can we cast something in the first 2 turns?
  castableByTurn2 = any card in hand with manaValue <= 2 and castable given landCount >= 2
  if not castableByTurn2 and onMulligan < 2: return false

  return true
```

On each mulligan, bottom one card before redrawing (Vancouver rule). Stop at 4 cards and keep whatever is dealt.

### Land Drop Decision

Always play a land if one is available and the land drop hasn't been used this turn.

When multiple lands are available:
1. Prefer lands that produce colors needed to cast cards in hand this turn or next.
2. Prefer lands that enter untapped over tapped.
3. Among equal options, prefer basic lands (preserves non-basics for later flexibility).

### Spell Selection and Sequencing

Each main phase, collect all castable spells (affordable with available mana, legal targets exist if required). Then order them:

**General priority order** (cast lower-priority items first if they enable higher-priority ones):

1. **Mana producers** (mana dorks, rituals, ramp) — cast early so they fund later spells this turn
2. **Enablers** (cards whose value depends on other permanents being in play) — cast before the permanents they enable
3. **Threats** (creatures, planeswalkers that advance the clock)
4. **Card draw / filtering** — cast after threats to find more threats
5. **Pump / combat tricks** — hold for the attack step if possible

**Mana efficiency rule**: Prefer to spend all available mana each turn. Among castable spells, choose the combination that most fully uses available mana. If two spells cost `{2}` and `{3}` and you have 5 mana, cast both. If you must leave mana unused, spend it on the most impactful single spell.

**Sequencing with ETB triggers**: If spell A's ETB trigger benefits from a permanent already being in play (e.g., "when this enters, put a +1/+1 counter on target creature you control"), cast the permanent first, then A.

### Target Selection

For beneficial effects (pump, protection, draw):
- Prefer the target with the highest combined power + toughness (most likely to matter in combat)
- For "draw a card" attached to a sacrifice cost, sacrifice the least useful permanent

For damage/removal effects (in goldfishing these mostly target the opponent directly):
- Target the opponent when possible
- If forced to target a permanent (e.g., fight), pick the one that advances the win fastest

For "up to N targets" — always choose N if there are legal targets.

### Attack Declaration

In goldfishing, the opponent has no blockers. Always attack with every creature that can legally attack:
- Must be untapped
- Must not have summoning sickness (unless haste — see `mtg-rules.md`)
- Must not have Defender

Track `enteredThisTurn` on each permanent to enforce summoning sickness correctly.

### X Value Selection

For damage spells targeting the opponent (`{X}: deals X damage to any target`): pay all remaining mana into X.
For spells that put X counters on a creature: pay all remaining mana into X.
For draw spells (`draw X cards`): pay all remaining mana into X, unless hand is already full.

### Activated Abilities

Evaluate each activated ability at every priority window:
- If the ability deals damage to the opponent and you have enough mana: activate immediately during main phase before the attack step (so summoning-sick creatures still get to attack)
- If the ability requires tapping a creature, activate before declaring attackers
- Loyalty abilities: activate once per turn during main phase; prefer +loyalty abilities unless the ultimate is reachable this turn

---

## Board State Evaluation

A lightweight evaluation function is useful for comparing candidate actions. Score the board from the active player's perspective:

```
evaluateBoard(state: GameState, player: Player) -> int:
  score = 0
  score += player.life * 2
  score -= opponent(player).life * 2

  for each creature controlled by player on battlefield:
    score += creature.effectivePower + creature.effectiveToughness
    score += 2 if creature has evasion (flying, trample, unblockable)

  for each creature controlled by opponent:
    score -= creature.effectivePower + creature.effectiveToughness

  score += player.handSize        // card advantage
  score -= opponent(player).handSize

  score += player.life            // life as a resource
  return score
```

`effectivePower` and `effectiveToughness` must be computed through the layer system, not read from base values — see `mtg-rules.md` build guidance.

Use this function to break ties when the priority ordering above does not distinguish between two actions. Do not use it as a primary decision driver in Phase 1 (the heuristics above are sufficient and faster).

---

## Game Logging

All games must be persisted for post-hoc review. The log format must be:
- **Human-readable** — Claude should be able to load a log and evaluate the AI's decisions and results
- **Structured** — machine-parseable for aggregate analysis (win turn distribution, etc.)
- **Compact enough** to store 10,000+ games without excessive disk use

### Recommended Format

One JSON file per game. File name encodes the run ID and game number: `run_{runId}_game_{n}.json`.

```json
{
  "runId": "abc123",
  "gameNumber": 42,
  "deckId": "d1",
  "seed": 8675309,
  "result": { "winner": "player", "turn": 5 },
  "turns": [
    {
      "turn": 1,
      "phase": "MAIN_1",
      "actions": [
        { "type": "PLAY_LAND", "card": 3, "cardName": "Forest" },
        { "type": "CAST_SPELL", "card": 12, "cardName": "Llanowar Elves", "manaPaid": "{G}" }
      ],
      "boardAfter": {
        "playerLife": 20,
        "opponentLife": 20,
        "battlefield": [3, 12],
        "hand": [7, 15, 22, 41, 55, 60]
      }
    }
  ]
}
```

Cards are referenced by their number (1–60, see Deterministic Seeding below), not by name, to keep logs compact. Include `cardName` as a human-readable annotation alongside the number.

### Disk Cleanup

Track total log directory size after each run. If it exceeds a configurable threshold (default 500 MB), delete the oldest runs (by `runId` timestamp) until under the limit. Log a summary of what was deleted. Never delete the most recent run.

---

## Deterministic Seeding

Reproducibility is critical: the same seed must produce the same game for both `d1` and `d2`, making it possible to compare apples-to-apples.

### Card Numbering

Assign each card a stable integer ID (1–60 for a 60-card deck):

1. Build a **union** of `d1` and `d2` card names, sorted alphabetically.
2. Number cards that appear in **both** decks first (lowest numbers), sorted alphabetically.
3. Then number cards unique to `d1`, then cards unique to `d2`, all alphabetically.
4. For cards with multiple copies, assign consecutive numbers (e.g., 4x Lightning Bolt → cards 1, 2, 3, 4).

This ensures shared cards always have the same numbers across both lists, so a shuffle producing card 1 on top means the same card regardless of which decklist is being tested.

Store the mapping in the run log for reference.

### Shuffle Implementation

```cpp
std::vector<int> shuffle(std::vector<int> deck, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::shuffle(deck.begin(), deck.end(), rng);
    return deck;
}
```

The seed for game N of a run is: `baseSeed + N`. `baseSeed` is fixed per run and stored in the run manifest. Both `d1` and `d2` use the same seed for the same game number.

### Other Random Events

Any event that requires randomness (coin flips, random discard from opponent's hand in Phase 2) must draw from the same seeded RNG in a deterministic order. The RNG must be advanced the same number of times in both `d1` and `d2` simulations for a given game, even if one deck doesn't use a particular random event — advance the RNG and discard the value to keep the sequences aligned.

---

## Encoding the AI Logic

The spec calls for the AI to be **encoded** — rule-based logic hardcoded into the engine — rather than a learned or search-based system. This is the correct approach for Phase 1 and likely Phase 2 given the project goals.

**Why encoded logic fits here:**
- The objective (goldfish as fast as possible) is well-defined and narrow
- Deck-testing validity depends on the AI making *reasonable* plays, not *optimal* plays — an encoded heuristic is inspectable and correctable when it makes mistakes
- Game logs are intended for Claude to review; encoded logic produces explainable decisions

**When encoded logic becomes impractical** (raise for discussion before proceeding):
- Cards with open-ended modal choices where the best choice depends on information the AI can't see (opponent's hand in Phase 2)
- Cards that require multi-turn planning (e.g., Suspend cards, Saga sequencing across 3+ turns)
- Highly synergistic combo decks where the win line requires a specific sequence across multiple turns

For these cases, consider: (a) hardcoding a known combo line as a special case, (b) a limited lookahead (1–2 turns), or (c) flagging the card as "requires human review" in the log.

---

## [Phase 2] Opponent AI Considerations

Phase 2 adds a real opponent. This dramatically increases AI complexity. Key additions needed:

**New decision points**: Block declaration (which creatures block which), reactive spell casting (instants, flash during opponent's turn), discard choices, sacrifice choices from opponent effects.

**Block heuristic (basic)**:
- Never block if blocking causes a net-negative trade (lose a bigger creature to kill a smaller one)
- Always block if not blocking means lethal damage
- Block to trade equals (remove a threat at the cost of a similar threat)

**Reactive spell casting**:
- Hold instants until the opponent's end step if no immediate trigger requires earlier use
- Counter spells only when the opponent casts a threat that would otherwise win the game

**Opponent model**: In goldfishing-vs-opponent mode, both AIs use the same encoded heuristics. The AI does not model the opponent's hand or future plays — it reacts to visible game state only.

Raise for discussion before implementing Phase 2 AI whether a more sophisticated model (limited search, policy tables per card type) is warranted given the decks being tested.

---

## How to Use This Skill

This file is not an invokable slash command. Ask Claude to read it and apply it to the task at hand.

**AI decision implementation**:
> "Read `.claude/skills/mtg-ai.md` and implement the attack declaration logic for the goldfishing AI."

**Game log design**:
> "Read `.claude/skills/mtg-ai.md` and implement the game logging module."

**Seeding**:
> "Read `.claude/skills/mtg-ai.md` and implement the card numbering and shuffle seeding for a run comparing d1 and d2."

**Phase 2 discussion**:
> "Read `.claude/skills/mtg-ai.md` and outline what would need to change in the AI engine to support a real opponent."

Always cross-reference `.claude/skills/mtg-rules.md` for the data models and rules correctness baseline that this skill builds on.
