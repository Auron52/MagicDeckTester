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

The mulligan decision is deck-dependent. No single function works across all archetypes — a 40-land deck wants 5–6 lands in hand, while a combo deck should mulligan aggressively until it sees its key piece. The heuristic is therefore parameterized by a **mulligan profile** attached to each deck.

#### Mulligan Profile

| Field | Default | Purpose |
|---|---|---|
| `minLands` | 1 | Mulligan if fewer lands than this |
| `maxLands` | 5 | Mulligan if more lands than this |
| `requiredPieces` | `[]` | Card names/IDs — mulligan unless at least one is in hand |
| `skipCurveCheck` | `false` | Set true for decks that don't need early plays |
| `stopAt` | 4 | Keep unconditionally once hand reaches this size |

The defaults represent a generic fair deck. Non-generic archetypes must override:

- **Land-heavy decks** (e.g. 40+ lands, one key non-land): raise `maxLands` to 6, add the key card(s) to `requiredPieces`, set `skipCurveCheck` to true.
- **Combo decks** (must see a specific piece to function): add the required combo piece(s) to `requiredPieces`. The AI mulligans until a required piece appears or hand size reaches `stopAt`.
- **Decks with no early plays by design**: set `skipCurveCheck` to true.

`requiredPieces` uses OR logic — any one listed card satisfies the requirement. For AND requirements (must have card A AND card B), encode them as two separate `requiredPieces` checks evaluated together, or flag the deck for manual review if the combo is too narrow to simulate reliably.

#### Heuristic

```
keepHand(hand: Card[], onMulligan: int, profile: MulliganProfile) -> bool:
  handSize = 7 - onMulligan
  landCount = count cards in hand that are lands
  nonLandCount = handSize - landCount

  // Land count bounds
  if landCount < profile.minLands: return false
  if landCount > profile.maxLands: return false
  if nonLandCount == 0: return false

  // Required pieces
  if profile.requiredPieces is not empty:
    if none of profile.requiredPieces are in hand: return false

  // Curve check: can we cast something in the first 2 turns?
  if not profile.skipCurveCheck:
    castableByTurn2 = any card in hand with manaValue <= 2 and castable given landCount >= 2
    if not castableByTurn2 and onMulligan < 2: return false

  return true
```

On each mulligan, bottom one card before redrawing (Vancouver rule). Stop at `profile.stopAt` cards and keep unconditionally.

**[Phase 2] Mulligan complexity increases significantly in 1v1.** The profile system above is Phase 1 only — it evaluates hands purely against your own game plan. In 1v1, the keep decision gains a second axis: does this hand answer the opponent's strategy?

Concretely, a hand that is a fine goldfish may be an immediate mulligan against a specific opponent. Examples:
- A fast aggro hand may be wrong to keep against a combo deck that wins on turn 3 if you have no disruption.
- A hand full of removal is weak against a control deck with few creatures but correct against an aggro deck.

Extending the profile naively (e.g. adding `requiredInteraction` fields) does not solve this cleanly, because the value of a given piece of interaction depends on what the opponent is doing — information the profile can't encode statically. **Raise for discussion before implementing Phase 2 mulligan logic.** The practical options are: (a) ignore the opponent dimension and accept the quality ceiling, (b) matchup-specific profile overrides authored per deck pairing, or (c) a limited hand evaluation that scores interaction against the known opponent decklist.

### Land Drop Decision

The land drop decision is not just "do I have a land" — it's **which land and when within the turn**. Evaluate the land drop after resolving any draw effects so the full hand is visible before committing.

**Timing**: resolve cantrips, draw spells, and fetch crack decisions before playing a land. The land you'd have played at the start of main phase may no longer be the best choice after drawing.

**Which land to play** (once you've decided to play one):
1. Prefer the land that best matches the colors needed to cast cards in the current hand.
2. Prefer lands that enter untapped over tapped — but only when you can use the additional mana this turn. If your planned spells don't consume the extra mana the untapped land would provide, prefer the tapped land instead and preserve the untapped land for a future turn where tempo matters.
3. Among equal options, prefer the land that introduces a color not yet covered by lands already in play — a land that duplicates an existing color source is less valuable than one that opens a new color.

**When not to play a land immediately**:
- Hold the land drop until after draw effects resolve — a drawn land may be a better fit for the colors needed.
- If you hold a fetch land and expect to draw this turn, crack the fetch after drawing to find the most color-appropriate land given the full hand.
- If the only available land enters tapped and you have an untapped land you could play next turn, consider whether the tempo cost is worth it this turn.

These are heuristics, not hard rules. Deck-specific logic may override them — a deck with heavy color requirements may always prioritize color fixing over tempo.

### Spell Selection and Sequencing

Turn planning has two distinct phases that must run in order. The development heuristic only applies when the win-now check fails.

#### Phase 1 — Win-Now Check

Before selecting any spells, ask: **can I deal lethal damage this turn?**

```
canWinThisTurn(state) -> (bool, Sequence):
  attackDamage = sum of power of creatures that can legally attack this turn
  // For each affordable subset of direct-damage spells in hand:
  //   check if attackDamage + spellDamage >= opponent.life
  //   accounting for mana sequencing (rituals cast before payoffs)
  // Return the winning sequence if one exists, else (false, [])
```

If a winning sequence exists, execute it — skip the development heuristic entirely. The winning sequence is not always obvious: you may need to cast a ritual or tap a mana dork before you can afford the burn spell that closes it out, meaning the sequencing itself must be solved, not just the damage total.

**Complexity boundary**: For most decks this check is tractable (enumerate affordable direct-damage spells, sum with attacker power). It becomes a search problem when the win requires a specific multi-spell chain (e.g. ritual → ritual → payoff) or when pump spells interact with creature power in non-obvious ways. If the winning sequence search is too broad for a given card set, flag those cards for manual sequence annotation and fall back to the development heuristic.

#### Phase 2 — Development Heuristic

If no win this turn, the goal is to maximize board development toward winning a future turn. This has two sub-steps: **selection** (what to cast) then **sequencing** (in what order).

**Selection — what to cast**

Selection and sequencing are separate decisions. The priority order below governs sequencing only; do not use it to drive selection.

Select the combination of spells that:
1. Spends the most total mana this turn, then
2. Among equally efficient combinations, maximises impact — generally: higher mana cost = higher impact; use the board evaluation function to break close ties.

Mana producers (dorks, rituals) participate in this trade-off like any other spell. Include them when:
- You need their mana this turn to afford a higher-impact spell (cast the producer first, then the payoff), or
- There is spare mana remaining after selecting your main plays.

Do not include a mana producer if doing so displaces a higher-cost spell and leaves total mana spent the same or lower. A 3-mana threat beats a 1-mana dork on a turn where you only have 3 mana — the dork can wait for a future turn when you have mana to spare.

**Sequencing — in what order to cast**

Once the set of spells to cast is decided, order them:

1. **Mana producers** — cast first so they fund the spells that follow
2. **Enablers** (cards whose value depends on other permanents being in play) — cast before the permanents they enable
3. **Threats** (creatures, planeswalkers that advance the clock)
4. **Card draw / filtering** — cast after threats to find more threats
5. **Pump / combat tricks** — hold for the attack step if possible

**Sequencing with ETB triggers**: If spell A's ETB trigger benefits from a permanent already being in play (e.g., "when this enters, put a +1/+1 counter on target creature you control"), cast the permanent first, then A.

### Target Selection

For beneficial effects (pump, protection, draw):
- Prefer the target with the highest combined power + toughness (most likely to matter in combat)
- For "draw a card" attached to a sacrifice cost, sacrifice the least useful permanent

For damage/removal effects (in goldfishing these mostly target the opponent directly):
- Target the opponent when possible
- If forced to target a permanent (e.g., fight), pick the one that advances the win fastest

For "up to N targets" — choose as many targets as advance your position:
- Beneficial effects (pump, protection, draw): always fill N from your own permanents/players if legal targets are available.
- Negative effects (damage, destruction, debuffs): fill N from opponent permanents and the opponent only; never pad to N by targeting your own permanents or yourself.

### Attack Declaration

In goldfishing, the opponent has no blockers. Attack with every creature that can legally attack, subject to the tap-ability exception below:
- Must be untapped
- Must not have summoning sickness (unless haste — see `mtg-rules.md`)
- Must not have Defender

**Exception — tap-requiring activated abilities**: Before committing a creature to the attack, check whether its tap ability provides more value than its combat damage this turn. If so, hold it back and use the ability during the main phase instead.

| Ability type | Default action |
|---|---|
| Mana production | Use during main phase if you have spells left to cast; attack only if all castable spells are already on the stack and the mana would go unused |
| Card advantage (draw, loot, filter) | Almost always prefer the tap ability — card selection exceeds 1–2 combat damage in virtually every scenario |
| Tap-for-damage | Use the ability during main phase; creature ends up tapped either way, but this lets you pick targets and preserves Phase 2 flexibility |
| Other (pump, bounce, etc.) | Evaluate case by case; prefer the ability if its immediate effect advances the win more than the combat damage |

Creatures with none of the above always attack.

Track `enteredThisTurn` on each permanent to enforce summoning sickness correctly.

### X Value Selection

X spells are not a simple mana dump. Whether to cast at all, and for how much, depends on the spell type.

**Damage X spells** (`{X}: deals X damage to any target`, e.g. Fireball, Crackle with Power):
- Evaluate as part of the win-now check first. If casting for lethal this turn, include in the winning sequence at the required X.
- If not part of a lethal line, default to holding rather than casting for a small X. Spending mana on a damage spell that does not close the game is usually worse than developing the board. Decks that use X damage spells as their primary finisher should almost never cast them for sub-lethal amounts.
- Exception: cast for partial damage if the deck has no other use for the mana and the game is in a late, mana-flush state.

**Creature pump X spells** (`put X +1/+1 counters on target creature`): pay all remaining mana into X — growing a permanent threat is always worth maximising.

**Draw X spells** (`draw X cards`):
- Participate in spell selection like any other spell — higher-impact plays take priority and are cast first. The draw spell only gets cast with whatever mana remains after those.
- Do not cast if the remaining mana after other plays would produce a low X. Holding for a future turn at full mana is usually better than drawing 1–2 cards now. A reasonable default minimum: X ≥ 3, or a deck-specific threshold.
- Do not use low hand size alone as a trigger to cast early — play out other spells first, then reassess with the mana that remains.
- When mana is high, do not dump all of it into X. Reserve enough mana to cast at least one spell from what you draw — how much to hold back is deck-dependent on the curve (e.g. a deck full of three-drops should hold at least 3). Drawing a large number of cards and then having no mana to act on them wastes a full turn.
- Clear exception: if this is your only remaining nonland card in hand and you have full or near-full mana, cast it. There is no benefit to holding when there is nothing else to do.

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

## AI Implementation Approach

The core constraint from the specification is: **no inference calls during game execution**. The AI must be a self-contained program that makes all decisions locally at runtime without calling external services (LLMs, APIs, remote models).

Within that constraint, any implementation technique is valid:

| Approach | Notes |
|---|---|
| Rule-based heuristics | What most of this skill describes; inspectable and correctable |
| Search (minimax, MCTS, lookahead) | Fine for any decision point where the branching factor is tractable |
| Offline-trained local models | Fine if the model is trained before the program runs and executes locally; not fine if it requires inference calls at runtime |

The heuristics in this skill are the starting point because they are simple to implement and easy to inspect when the game log shows a bad decision. Search or learned approaches can replace or augment them if a heuristic proves too weak for a given decision point — raise for discussion before implementing a non-heuristic approach so the complexity trade-off can be evaluated.

**When heuristics become impractical** (raise for discussion before proceeding):
- Cards with open-ended modal choices where the best choice depends on information the AI can't see (opponent's hand in Phase 2)
- Cards that require multi-turn planning (e.g., Suspend cards, Saga sequencing across 3+ turns)
- Highly synergistic combo decks where the win line requires a specific sequence across multiple turns

For these cases, consider: (a) hardcoding a known combo line as a special case, (b) a limited search lookahead, or (c) flagging the card as "requires human review" in the log.

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
