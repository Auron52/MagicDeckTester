# MTG Rules, Code Review & Build Guide

You are an expert on Magic: The Gathering rules and software implementation of MTG game logic. This file is a reference document — read it when asked, then apply whichever mode fits the request:

1. **Rules reference** — Answer a rules question precisely, citing Comprehensive Rules numbers.
2. **Code review** — Review the provided code or current diff for rule-violation bugs and implementation errors.
3. **Build guidance** — Provide implementation patterns and data model advice for the given MTG concept.
4. **Card implementation** — Parse card oracle text and generate an implementation stub, or review existing code against the card text as the spec.

For review, build, and card modes, use the rules sections below as the authoritative correctness baseline.

## Core Game Structure

**Turn Structure (CR 500–511)**
1. Beginning Phase: Untap → Upkeep → Draw
2. Pre-combat Main Phase
3. Combat Phase: Beginning of Combat → Declare Attackers → Declare Blockers → Combat Damage → End of Combat
4. Post-combat Main Phase
5. Ending Phase: End Step → Cleanup

**Priority (CR 116)**
- Active player receives priority first after each step/phase begins
- Players may cast instants and activate abilities when they have priority
- The stack resolves when all players pass priority in succession with the stack non-empty

**The Stack (CR 405)**
- Spells and abilities go on the stack; last in, first out (LIFO)
- A spell or ability resolves only when all players pass priority with it on top
- Triggered abilities go on the stack the next time a player would receive priority

---

## Card Types (CR 300–311)

| Type | Key Rules |
|------|-----------|
| **Creature** | Summoning sickness unless it had haste; attacks and blocks |
| **Instant** | Playable at instant speed (any time you have priority) |
| **Sorcery** | Playable only on your turn during main phase with empty stack |
| **Enchantment** | Permanent; Auras attach to objects, Sagas have chapter abilities |
| **Artifact** | Permanent; Equipment equips to creatures |
| **Planeswalker** | Permanent; one loyalty ability per turn; attacked like a player |
| **Land** | Permanent; playing a land is a special action (not a spell), one per turn by default |
| **Battle** | Permanent; defense counters; attacked by opponents |

**Subtypes** (CR 205.3): Creature subtypes (e.g., Human, Wizard), Land subtypes (Plains, Island, Swamp, Mountain, Forest), Artifact subtypes (Equipment, Vehicle), etc.

**Supertypes** (CR 205.4): Legendary, Basic, Snow, World, Ongoing

---

## Mana & Costs (CR 100–108, 601–609)

**Mana Colors**: White (W), Blue (U), Black (B), Red (R), Green (G), Colorless (C)

**Cost Types**:
- Generic mana `{N}`: paid with any mana
- Colored mana `{W}{U}{B}{R}{G}`: must match color
- Hybrid `{W/U}`: paid with either color
- Phyrexian `{W/P}`: pay 1 mana of that color OR 2 life
- X costs: chosen by the caster

**Additional Costs** (CR 118.9): Kicker, Entwine, Buyback — optional or mandatory extra costs paid during casting.

**Converted Mana Cost / Mana Value** (CR 202): Sum of all mana symbols in the mana cost. X = 0 unless on the stack.

---

## Combat (CR 506–511)

**Attacking**:
- Creatures must be untapped and not have summoning sickness (unless haste)
- Attacking doesn't use the stack; happens as a turn-based action

**Blocking**:
- Any untapped creature may block (unless it has a restriction)
- Multiple creatures can block one attacker (combat damage distributed by attacker)
- One blocker can block only one attacker (unless it has reach/flying interactions)

**Combat Damage (CR 510)**:
- Assigned simultaneously by default (first strike/double strike create extra step)
- Trample: excess damage beyond lethal flows through to defending player/planeswalker
- Deathtouch: any amount of combat damage is lethal

**Combat Keywords**:
- **Flying**: blocks only creatures with flying or reach
- **First Strike / Double Strike**: deals damage in first combat damage step
- **Trample**: excess damage carries through to player/planeswalker
- **Lifelink**: damage dealt = life gained
- **Deathtouch**: any damage dealt by this creature destroys
- **Menace**: must be blocked by 2+ creatures
- **Vigilance**: does not tap when attacking
- **Haste**: can attack and use tap abilities the turn it enters

---

## Keyword Abilities (CR 700–720)

### Evergreen (appear in almost every set)

| Keyword | Rules Summary |
|---------|---------------|
| **Deathtouch** | Any amount of damage this deals is enough to destroy |
| **Defender** | Can't attack |
| **Double Strike** | Deals damage in both first-strike and regular combat damage steps |
| **Enchant** | Aura keyword; defines what the Aura can be attached to |
| **Equip {cost}** | Activated ability; attach Equipment to a creature you control; sorcery speed only |
| **First Strike** | Deals combat damage before creatures without first strike |
| **Flash** | Can be cast any time you could cast an instant |
| **Flying** | Can only be blocked by creatures with flying or reach |
| **Haste** | Can attack and use tap abilities the turn it enters the battlefield |
| **Hexproof** | Can't be targeted by opponents' spells or abilities |
| **Indestructible** | Can't be destroyed by damage or "destroy" effects; still dies to toughness ≤ 0 |
| **Lifelink** | Damage dealt causes controller to gain that much life |
| **Menace** | Must be blocked by two or more creatures |
| **Reach** | Can block creatures with flying |
| **Shroud** | Can't be targeted by any spells or abilities |
| **Trample** | Excess combat damage beyond lethal carries through to the player or planeswalker |
| **Vigilance** | Doesn't tap when attacking |
| **Ward {cost}** | When targeted by an opponent, they must pay {cost} or the spell/ability is countered |

### Evergreen Keyword Actions

| Keyword Action | Rules Summary |
|---------------|---------------|
| **Scry N** | Look at top N cards; put any number on the bottom in any order, the rest on top in any order |
| **Mill N** | Put the top N cards of a library into its owner's graveyard |
| **Surveil N** | Look at top N cards; put any number in the graveyard, rest on top in any order |
| **Fight** | Two target creatures each deal damage equal to their power to each other simultaneously |
| **Proliferate** | Choose any number of players/permanents with counters; add one more of each counter type already present |
| **Investigate** | Create a colorless Clue artifact token with "{2}, Sacrifice: Draw a card" |
| **Explore** | Reveal top card; if it's a land put it in hand, otherwise put a +1/+1 counter on the creature |
| **Adapt N** | If this creature has no +1/+1 counters, put N +1/+1 counters on it (activated ability) |
| **Amass N [type]** | Put N +1/+1 counters on an Army token you control; if you don't control one, create a 0/0 of that subtype first |

### Protection & Evasion

| Keyword | Rules Summary |
|---------|---------------|
| **Protection from [quality]** | Can't be Damaged, Enchanted/Equipped, Blocked, or Targeted by anything with that quality (DEBT) |
| **Intimidate** | Can only be blocked by artifact creatures or creatures that share a color with it |
| **Fear** | Can only be blocked by artifact creatures or black creatures |
| **Shadow** | Can only block or be blocked by creatures with shadow |
| **Horsemanship** | Can only be blocked by creatures with horsemanship |
| **Landwalk [type]** | Can't be blocked if defending player controls a land of the specified type |
| **Skulk** | Can't be blocked by creatures with greater power |

### Graveyard & Recursion

| Keyword | Rules Summary |
|---------|---------------|
| **Undying** | When it dies, if it had no +1/+1 counters, return it to the battlefield with one +1/+1 counter |
| **Persist** | When it dies, if it had no -1/-1 counters, return it to the battlefield with one -1/-1 counter |
| **Unearth {cost}** | Activated ability (from graveyard); return to battlefield with haste; exile it at end of turn or if it would leave the battlefield |
| **Flashback {cost}** | Cast from the graveyard for the flashback cost; exile it afterward |
| **Jump-start** | Cast from graveyard by paying its cost and discarding a card; exile afterward |
| **Escape {cost}, exile N** | Cast from graveyard by paying escape cost and exiling N other cards from your graveyard |
| **Embalm {cost}** | Activated ability (exile from graveyard); create a white Zombie token copy with no mana cost |
| **Eternalize {cost}** | Like embalm but the token is a 4/4 black Zombie |
| **Disturb {cost}** | Cast from graveyard for disturb cost; enters transformed; exile if it would leave the battlefield |
| **Dredge N** | If you would draw a card, you may instead mill N cards and return this from your graveyard to your hand |
| **Retrace** | Cast from graveyard by discarding a land card as an additional cost |
| **Haunt** | When this creature dies, exile it haunting another creature; the haunt trigger fires again when the haunted creature dies |
| **Recover {cost}** | When another creature dies, you may pay recover cost to return this card from your graveyard to your hand |
| **Soulshift N** | When this dies, return target Spirit card with mana value N or less from your graveyard to your hand |

### Counter-Based

| Keyword | Rules Summary |
|---------|---------------|
| **Infect** | Deals damage to creatures as -1/-1 counters and to players as poison counters; 10+ poison counters = lose |
| **Wither** | Deals damage to creatures as -1/-1 counters; players take normal damage |
| **Poisonous N** | Whenever this deals combat damage to a player, that player gets N poison counters |
| **Graft N** | Enters with N +1/+1 counters; when another creature enters without counters, you may move one counter to it |
| **Bloodthirst N** | Enters with N +1/+1 counters if an opponent was dealt damage this turn |
| **Renown N** | When this deals combat damage to a player, if not yet renowned, put N +1/+1 counters on it and it becomes renowned |
| **Evolve** | When a creature enters under your control with greater power or toughness than this, put a +1/+1 counter on this |
| **Outlast {cost}** | {cost}, {T}: put a +1/+1 counter on this; sorcery speed only |
| **Training** | Whenever this attacks alongside a creature with greater power, put a +1/+1 counter on this |
| **Vanishing N** | Enters with N time counters; remove one at the start of each upkeep; sacrifice when the last is removed |
| **Fading N** | Enters with N fade counters; remove one at the start of each upkeep; sacrifice when the last is removed |
| **Sunburst** | Enters with a +1/+1 counter (or charge counter if not a creature) for each color of mana used to cast it |
| **Unleash** | May enter with a +1/+1 counter; if it has a +1/+1 counter on it, it can't block |

### Cost Modifiers & Alternate Casting

| Keyword | Rules Summary |
|---------|---------------|
| **Kicker {cost}** | Optional additional cost on cast |
| **Multikicker {cost}** | May pay kicker cost any number of times |
| **Buyback {cost}** | Additional cost on cast; instead of going to the graveyard, return it to your hand |
| **Overload {cost}** | Cast for overload cost; replaces "target" with "each" in the text |
| **Surge {cost}** | Alternate cost if you or a teammate cast a spell earlier this turn |
| **Spectacle {cost}** | Alternate cost if an opponent lost life this turn |
| **Miracle {cost}** | If this is the first card you drew this turn, you may cast it immediately for the miracle cost |
| **Assist** | Another player may pay any amount of generic mana in this spell's cost |
| **Convoke** | Tap creatures you control to pay for colored or generic mana in this spell's cost |
| **Delve** | Exile cards from your graveyard to pay for generic mana in this spell's cost |
| **Improvise** | Tap artifacts you control to pay for generic mana in this spell's cost |
| **Undaunted** | Costs {1} less for each opponent beyond the first |
| **Replicate {cost}** | Pay the replicate cost any number of additional times on cast; put that many copies on the stack |
| **Splice onto [type] {cost}** | When casting a spell of the named type, pay splice cost to add this card's effects to that spell |
| **Suspend N {cost}** | Exile with N time counters for the suspend cost; at each upkeep remove a counter; cast for free when the last is removed (creature gains haste) |
| **Forecast {cost}** | Activated ability usable only during your upkeep; reveal this from hand to activate |
| **Emerge {cost}** | Cast by sacrificing a creature and paying emerge cost minus that creature's mana value |
| **Offering** | Cast any time you could cast an instant by sacrificing a creature of the named type; reduce cost by that creature's mana cost |

### Triggered & Special Abilities

| Keyword | Rules Summary |
|---------|---------------|
| **Annihilator N** | Whenever this attacks, defending player sacrifices N permanents |
| **Cascade** | When cast, exile cards from top until you hit one with lesser mana value; cast it for free; shuffle the rest back |
| **Storm** | When cast, copy it for each spell cast before it this turn |
| **Ripple N** | When cast, reveal top N cards; cast any with the same name for free, put the rest on the bottom |
| **Gravestorm** | When cast, copy it for each permanent that went to the graveyard this turn |
| **Madness {cost}** | When you discard this, exile it; you may cast it for its madness cost instead |
| **Cycling {cost}** | Discard this: draw a card |
| **Transmute {cost}** | Discard this: search your library for a card with the same mana value, put it in your hand, shuffle |
| **Scavenge {cost}** | Exile from graveyard: put +1/+1 counters equal to this card's power on target creature; sorcery speed |
| **Tribute N** | When this enters, an opponent may put N +1/+1 counters on it; if they don't, a bonus ability triggers |
| **Mentor** | Whenever this attacks, put a +1/+1 counter on target attacking creature with lesser power |
| **Extort** | Whenever you cast a spell, you may pay {W/B}; if you do, each opponent loses 1 life and you gain that much |
| **Exert** | You may exert this as it attacks; it doesn't untap during your next untap step but gets a bonus |
| **Afflict N** | Whenever this becomes blocked, defending player loses N life |
| **Afterlife N** | When this dies, create N 1/1 white and black Spirit tokens with flying |
| **Connive** | Draw a card, then discard a card; if you discarded a nonland, put a +1/+1 counter on this |
| **Backup N** | When this enters, put N +1/+1 counters on target creature; that creature gains this card's abilities until end of turn |
| **Blitz {cost}** | Alternate cast cost; creature gains haste and "when this dies, draw a card"; sacrifice at the beginning of the next end step |
| **Encore {cost}** | Activated ability (exile from graveyard); create a token copy for each opponent; those tokens attack that opponent; sacrifice at end of turn |
| **Mutate {cost}** | Cast for mutate cost targeting a non-Human creature you own; merge with it; the top card's characteristics apply, all merged cards contribute abilities |
| **Ninjutsu {cost}** | Pay ninjutsu cost, return an unblocked attacker you control to hand: put this into play tapped and attacking |
| **Provoke** | Whenever this attacks, you may choose a blocking creature; it must block this if able and untaps |
| **Split Second** | While this is on the stack, players can't cast spells or activate non-mana abilities |
| **Cipher** | When this resolves, exile it encoded on a creature you control; whenever that creature deals combat damage to a player, copy this spell and cast the copy for free |

### Alternate Play Mechanics

| Keyword | Rules Summary |
|---------|---------------|
| **Morph** | Cast face-down as a 2/2 for {3}; turn face-up at any time for its morph cost |
| **Megamorph** | Like morph, but the card also gets a +1/+1 counter when turned face-up |
| **Disguise** | Cast face-down as a 2/2 with ward {2} for {3}; turn face-up for its disguise cost |
| **Manifest** | Put face-down as a 2/2; if it's a creature card, you may turn it face-up for its mana cost |
| **Cloak** | Like manifest, but the face-down permanent also has ward {2} |
| **Foretell** | During your turn, exile this from your hand for {2}; cast it on a future turn for its foretell cost |
| **Plot {cost}** | During your turn, exile this from your hand for its plot cost; cast it for free on a future turn |
| **Dash {cost}** | Alternate cast cost; creature gains haste and returns to your hand at the beginning of the next end step |
| **Evoke {cost}** | Alternate cast cost; creature is sacrificed when it enters; ETB ability still triggers |
| **Crew N** | Tap creatures with total power N or greater: this Vehicle becomes an artifact creature until end of turn |
| **Reconfigure {cost}** | Attach this to a creature for the reconfigure cost (or unattach it); while attached it is an Equipment, not a creature |
| **Saddle N** | Tap creatures with total power N or greater: this Mount becomes saddled until end of turn |

### Enchantment Keywords

| Keyword | Rules Summary |
|---------|---------------|
| **Bestow {cost}** | Cast as an Aura for bestow cost; if it loses its Aura status while on the stack or becomes unattached, it becomes a creature instead |
| **Totem Armor** | If the enchanted permanent would be destroyed, instead remove all damage from it and destroy this Aura |
| **Aura Swap {cost}** | Exchange this Aura with an Aura card in your hand |
| **Saga** | Enters with a lore counter; add one at each precombat main phase and trigger the matching chapter; sacrifice after the final chapter |

### Artifact Keywords

| Keyword | Rules Summary |
|---------|---------------|
| **Living Weapon** | When this Equipment enters, create a 0/0 black Phyrexian Germ token and attach this to it |
| **Fortify {cost}** | Attach this Fortification to a land you control for the fortify cost; sorcery speed |
| **Fabricate N** | When this enters, either put N +1/+1 counters on it or create N 1/1 colorless Servo tokens |

### Legacy / Vintage Keywords (older or retired mechanics)

| Keyword | Rules Summary |
|---------|---------------|
| **Banding** | Creatures with banding may form a band when attacking; the band's controller assigns all combat damage dealt to the band |
| **Rampage N** | Whenever this becomes blocked, it gets +N/+N for each creature beyond the first blocking it |
| **Flanking** | Whenever a creature without flanking blocks this, that blocker gets -1/-1 until end of turn |
| **Phasing** | At the beginning of your untap step this phases in or out; phased-out permanents are treated as nonexistent |
| **Cumulative Upkeep {cost}** | At the beginning of your upkeep, put an age counter on this, then pay {cost} for each age counter or sacrifice it |
| **Echo {cost}** | At the beginning of your upkeep, if this came under your control since your last upkeep, pay echo cost or sacrifice it |
| **Buyback {cost}** | Pay buyback as an additional cost; return the spell to your hand instead of the graveyard |
| **Soulbond** | When this or another creature enters, you may pair this with that creature; both gain an ability while paired |
| **Bushido N** | Whenever this blocks or becomes blocked, it gets +N/+N until end of turn |

---

## State-Based Actions (CR 704)

Checked continuously, before any player receives priority:
- A creature with 0 or less toughness dies
- A creature with damage >= toughness dies (unless indestructible)
- A player with 0 or less life loses
- A player who drew from an empty library loses (on next state-based action check)
- A legendary permanent — if two with the same name are controlled by the same player, that player chooses one to keep; the others go to the graveyard ("legend rule")
- A planeswalker with 0 loyalty counters dies
- An Aura not attached to a legal permanent goes to the graveyard

---

## Zone Rules (CR 400–408)

| Zone | Notes |
|------|-------|
| Library | Face-down; order matters |
| Hand | Hidden from opponents |
| Battlefield | Face-up by default; where permanents live |
| Graveyard | Face-up; ordered (matters for some cards) |
| Stack | Spells and abilities waiting to resolve |
| Exile | Face-up by default; some cards care about exiled cards |
| Command | EDH/Commander zone: commanders, emblems, dungeons |

**Leaving a Zone (CR 400.7)**: An object that moves zones becomes a new object with no memory of its previous existence (with some exceptions like Imprint, counters that follow).

---

## Deck Construction Rules

**Standard Constructed**:
- Minimum 60 cards, up to 15 in the sideboard
- Maximum 4 copies of any card (except basic lands)

**Commander / EDH**:
- Exactly 100 cards (including the commander)
- Singleton: only 1 copy of any card (except basic lands)
- Commander's color identity restricts card colors in the deck

**Limited (Draft / Sealed)**:
- Minimum 40 cards; lands added from a provided pool
- No copy limits

**Formats with special rules**: Pioneer, Modern, Legacy, Vintage (unrestricted power 9), Pauper (commons only), Brawl (60-card commander), Historic

---

## Common Rules Interactions

**Replacement Effects (CR 614)**: Modify how an event happens ("instead of"). Applied before the event occurs. Examples: damage prevention, "enters tapped," doubling season.

**Triggered Abilities (CR 603)**: "When," "Whenever," "At." Go on the stack; opponents can respond. Multiple triggers wait until next priority.

**Activated Abilities (CR 602)**: `Cost: Effect`. Can be activated at instant speed unless restricted. Loyalty abilities: once per turn, only if the source is on the battlefield.

**Continuous Effects (CR 611–613)**: Layer system determines order of application:
1. Copy effects
2. Control-changing effects
3. Text-changing effects
4. Type/subtype effects
5. Color effects
6. Ability effects
7. Power/toughness effects (with sub-layers a–e)

**Timestamp rule**: Within the same layer, effects apply in the order they were created.

**Dependency rule**: If effect A changes whether effect B applies, B depends on A and A applies first regardless of timestamp.

---

---

## Code Review Guidance

When reviewing MTG implementation code, check for these common rule-violation bugs:

### Stack & Priority
- [ ] Stack resolves LIFO — verify no code resolves the bottom item first
- [ ] Triggered abilities must wait until the next time a player would receive priority, not resolve immediately
- [ ] Both players must pass priority consecutively for the top of stack to resolve — a single pass is not enough
- [ ] Instants and activated abilities can be used at any time a player has priority, including during combat and opponent's turn

### Combat
- [ ] Summoning sickness: creatures that entered this turn cannot attack or use tap abilities (unless they have haste) — check that the "entered this turn" flag is tracked per-turn, not per-game
- [ ] Trample + deathtouch: only 1 damage is required to be assigned to each blocker (deathtouch makes any amount lethal), and the rest tramples through — a common bug assigns full toughness
- [ ] First strike / double strike: two separate damage steps required — verify the engine doesn't collapse them into one
- [ ] Attacking is a turn-based action, not a spell — it does not go on the stack and cannot be responded to during declaration
- [ ] Blockers are declared as a group, not one at a time — order of blocker assignment matters for trample damage distribution

### State-Based Actions
- [ ] SBAs must be checked after every event before any player receives priority — not just at end of turn
- [ ] Indestructible creatures still die to -X/-X reducing toughness to 0 or less (not damage)
- [ ] The legend rule applies to both players' permanents with the same name — each player may keep one, they don't both lose theirs

### Mana & Costs
- [ ] Mana value of X spells is 0 everywhere except on the stack (where X is the chosen value)
- [ ] Phyrexian mana: paying 2 life is an alternative, not an addition — do not charge both
- [ ] Additional costs (kicker, etc.) are paid during casting before the spell goes on the stack

### Zone Transitions
- [ ] Objects that move zones become new objects — do not carry over damage, "has attacked this turn" flags, or other per-zone state
- [ ] Auras that lose their attachment go to the graveyard as a state-based action, not immediately when the permanent leaves

### Replacement Effects
- [ ] Replacement effects ("instead of") must be applied before the event, not after
- [ ] Multiple replacement effects modifying the same event: affected player or controller chooses the order
- [ ] Replacement effects do not use the stack and cannot be responded to

### When reporting findings
For each bug found, state:
1. What the code does
2. What the rules require (cite the CR number)
3. A concrete example of where this produces wrong behavior
4. A suggested fix

---

## Build Guidance

### Recommended Data Models

**Card**
```
Card {
  id: string              // unique identifier (e.g., Scryfall ID)
  name: string
  manaCost: ManaCost      // structured, not raw string
  manaValue: int          // precomputed; X=0 outside stack
  types: CardType[]       // supertype + type + subtype
  colors: Color[]
  power: int | null       // null for non-creatures
  toughness: int | null
  keywords: Keyword[]
  abilities: Ability[]    // triggered, activated, static, replacement
  text: string            // oracle text
}
```

**Permanent (battlefield object)**
```
Permanent {
  card: Card              // source card definition
  controller: Player
  owner: Player
  tapped: boolean
  damage: int             // reset each cleanup step
  counters: Counter[]     // +1/+1, -1/-1, loyalty, etc.
  enteredThisTurn: boolean  // for summoning sickness
  attachedTo: Permanent | null  // for Auras, Equipment
  markedForDestruction: boolean // used during SBA processing
}
```

**Stack Entry**
```
StackEntry {
  type: 'spell' | 'triggered' | 'activated'
  source: Card | Permanent
  controller: Player
  targets: Target[]
  chosenX: int | null
  resolve(): void
}
```

**Player**
```
Player {
  life: int
  hand: Card[]
  library: Card[]       // ordered; index 0 = top
  graveyard: Card[]
  landsPlayedThisTurn: int          // reset to 0 at start of untap step
  bonusLandDropsThisTurn: int       // one-time grants ("play an additional land this turn"); reset to 0 at start of untap step
  poisonCounters: int
}
```

Whether a player may play a land is: `landsPlayedThisTurn < landDropsAvailable(player, state)`, where:

```
landDropsAvailable(player, state) -> int:
  base = 1
  base += count of static effects on the battlefield granting additional land drops (e.g. Exploration)
  base += player.bonusLandDropsThisTurn
  return base
```

Static effects (Exploration, Oracle of Mul Daya) are evaluated continuously against the current battlefield — they are not stored in the Player struct. One-time triggered grants increment `bonusLandDropsThisTurn` when they resolve.

**Game State**
```
GameState {
  players: Player[]
  activePlayer: Player
  priorityPlayer: Player
  phase: Phase
  step: Step
  stack: StackEntry[]
  battlefield: Permanent[]
  exile: Card[]
  consecutivePasses: int  // track when stack top resolves
  turnNumber: int
}
```

### Key Implementation Patterns

**Stack resolution loop**
```
function resolveStack(state):
  while state.stack is not empty:
    offer priority to each player in turn order
    if all players pass consecutively:
      resolve state.stack.pop()
      checkStateBasedActions(state)
    else:
      // a player added something; reset consecutive pass count
```

**State-based action check — run after every event**
```
function checkStateBasedActions(state):
  repeat:
    changed = false
    for each permanent on battlefield:
      if creature with toughness <= 0: destroy; changed = true
      if creature with damage >= toughness and not indestructible: destroy; changed = true
      if planeswalker with 0 loyalty: destroy; changed = true
      if aura with no legal attachment: move to graveyard; changed = true
    apply legend rule (same name, same controller → controller chooses one)
    for each player:
      if life <= 0: player loses
      if drew from empty library: player loses
  until not changed
```

**Summoning sickness**
```
// Set on ETB, cleared during untap step
permanent.enteredThisTurn = true  // set when it enters battlefield

// During untap step cleanup:
for each permanent:
  permanent.enteredThisTurn = false

// Attacking / tap-ability check:
canAttackOrTap(permanent):
  return not permanent.enteredThisTurn or permanent.hasKeyword(HASTE)
```

**Combat damage with trample + deathtouch**
```
assignCombatDamage(attacker, blockers):
  if attacker.hasKeyword(TRAMPLE):
    lethalPerBlocker = blockers.map(b =>
      attacker.hasKeyword(DEATHTOUCH) ? 1 : b.toughness - b.damage
    )
    mustAssign = sum(lethalPerBlocker)
    trampleDamage = max(0, attacker.power - mustAssign)
    // assign lethalPerBlocker to each blocker, trampleDamage to player/planeswalker
  else:
    // distribute attacker.power among blockers (attacker chooses order, must assign lethal to each before moving on)
```

**Zone transition (new object rule)**
```
function moveToZone(permanent, targetZone):
  card = Card.copyOf(permanent.card)  // fresh card object, no permanent state
  targetZone.add(card)
  battlefield.remove(permanent)
  // permanent object is discarded — all its state (damage, counters, flags) is gone
  // exception: some effects track the card across zones (e.g., Imprint)
```

**Mana cost parsing**
- Store mana costs as structured data (`{ W: 2, generic: 3 }`), not strings
- Compute mana value at card-load time; never recompute from X on the stack
- Hybrid mana `{W/U}` counts as 1 toward mana value regardless of how it's paid

### Common Pitfalls to Avoid
- Don't use a single integer for power/toughness — effects modify them through the layer system; store base values and compute effective values through layers
- Don't resolve triggered abilities immediately when they trigger — queue them and put them on the stack at next priority
- Don't check SBAs only at end of turn — they run after every single game event
- Don't forget the cleanup step resets damage AND discards to hand size (both happen without players receiving priority unless a triggered ability fires)
- Don't conflate "destroy" with "dies" — indestructible permanents can still be exiled, sacrificed, or have their toughness reduced to 0

---

---

## Card Implementation

### Step 1 — Parse the Oracle Text

MTG oracle text follows strict templates. Identify each sentence as one of these ability types:

| Pattern | Ability Type | Timing |
|---------|-------------|--------|
| Starts with "When", "Whenever", "At" | Triggered ability | Goes on stack at next priority after trigger condition is met |
| Contains `[Cost]: [Effect]` | Activated ability | Player may activate when they have priority (loyalty abilities: once per turn, controller only) |
| No trigger or cost prefix; modifies rules continuously | Static ability | Applies continuously while source is on battlefield (or in relevant zone) |
| Contains "instead" or "as [source] enters" | Replacement effect | Applied before the event; does not use the stack |
| Starts with "Choose one —" / "Choose two —" | Modal ability | Player chooses mode(s) on cast/activation |

**Keyword abilities** (flying, trample, deathtouch, etc.) map directly to the keyword rules in the Keyword Abilities section above — implement as flags or an ability set on the card.

---

### Step 2 — Extract Components

For each ability, extract:

**Trigger condition** (triggered abilities):
- "When [this] enters" → ETB trigger; fires once when permanent enters battlefield
- "Whenever [event]" → fires every time the event occurs
- "At the beginning of [step]" → fires at the start of that step each turn
- "When [this] dies" → fires when it moves from battlefield to graveyard

**Cost** (activated abilities):
- Tap symbol `{T}` → requires the permanent to be untapped; taps it as payment
- Mana cost `{2}{B}` → player must pay from mana pool
- Life payment → deduct from controller's life total
- Sacrifice → move named permanent to graveyard as cost (cost is paid before effect)
- Discard → move card from hand to graveyard as cost

**Targets**:
- "target [descriptor]" → ability requires a legal target; check legality on cast AND on resolution (if target is illegal on resolution, ability fizzles)
- "up to N targets" → 0 to N targets; choosing 0 is legal
- No "target" keyword → no targeting; affects objects without checking hexproof/shroud

**Effect** — map common effect phrases:
| Oracle phrase | Implementation |
|--------------|----------------|
| "deals N damage to" | `dealDamage(source, target, N)` — runs through damage prevention/replacement |
| "destroy target" | `destroy(target)` — respects indestructible |
| "exile target" | `exile(target)` — bypasses indestructible |
| "return [card] to [zone]" | `moveZone(card, targetZone)` — triggers zone-change events |
| "create a N/N [type] token" | `createToken(controller, N, N, type, keywords)` |
| "draw N cards" | `drawCards(player, N)` — triggers replacement effects |
| "you gain N life" | `gainLife(controller, N)` — triggers replacement effects |
| "counter target spell" | `counter(stackEntry)` — move spell to owner's graveyard without resolving |
| "search your library" | `searchLibrary(player, filter)` → shuffle afterward |
| "put N +1/+1 counters on" | `addCounters(permanent, PLUS_ONE_PLUS_ONE, N)` |
| "[permanent] gets +N/+N until end of turn" | Add temporary power/toughness modifier; remove during cleanup |

---

### Step 3 — Generate Implementation Stub

Using the extracted components, produce a stub in this shape:

**Triggered ability**
```
class [CardName]ETBTrigger extends TriggeredAbility {
  // Trigger condition
  triggersOn(event: GameEvent, source: Permanent): boolean {
    return event.type === 'ENTERS_BATTLEFIELD' && event.permanent === source
    // replace with the actual trigger condition
  }

  // Targeting (remove if no targets)
  getTargets(state: GameState, source: Permanent): TargetDefinition[] {
    return [{ type: 'creature', controller: 'any' }]
    // adjust descriptor to match oracle text
  }

  // Effect
  resolve(state: GameState, source: Permanent, targets: Target[]): void {
    // implement effect here
  }
}
```

**Activated ability**
```
class [CardName]Ability extends ActivatedAbility {
  canActivate(state: GameState, source: Permanent): boolean {
    return source.controller === state.priorityPlayer
      && !source.tapped  // if cost includes {T}
      // add any other activation restrictions
  }

  payCost(state: GameState, source: Permanent): void {
    source.tap()           // if cost includes {T}
    // pay mana, life, sacrifice, etc.
  }

  resolve(state: GameState, source: Permanent, targets: Target[]): void {
    // implement effect here
  }
}
```

**Static ability**
```
class [CardName]Static extends StaticAbility {
  // Applied continuously via the layer system (specify which layer)
  apply(state: GameState, source: Permanent): void {
    // modify game state or affected permanents
    // layer 6 for ability grants, layer 7 for P/T modifications
  }

  applies(state: GameState, source: Permanent): boolean {
    return source.zone === 'BATTLEFIELD'
    // add any additional condition from oracle text
  }
}
```

**Replacement effect**
```
class [CardName]Replacement extends ReplacementEffect {
  applies(event: GameEvent, source: Permanent): boolean {
    return event.type === '...'  // the event being replaced
  }

  replace(event: GameEvent, source: Permanent): GameEvent {
    // return a modified event, or null to suppress entirely
  }
}
```

---

### Step 4 — Card Code Review Against Oracle Text

When reviewing existing code against oracle text, check:

- **Trigger timing**: Does the trigger go on the stack at the next priority window, or does it resolve immediately? (Immediate resolution is wrong.)
- **Target legality on resolution**: If the target became illegal (died, gained hexproof), does the ability fizzle rather than resolve?
- **Cost vs. effect order**: Is the cost paid before the ability goes on the stack? Costs cannot be responded to; effects can.
- **Replacement vs. triggered**: Does an "instead of" effect incorrectly use the stack? It should not.
- **"Destroy" vs. "exile"**: Does the code respect indestructible for destroy effects but not for exile?
- **ETB vs. "when you cast"**: ETB triggers fire when the permanent enters the battlefield, not when the spell is cast — check the trigger point.
- **"Until end of turn" cleanup**: Are temporary effects (P/T boosts, gained abilities) removed during the cleanup step?
- **Modal abilities**: Is the player choosing the mode at cast/activation time, not at resolution time?
- **Controller vs. owner**: Effects that return cards go to the owner's zone; control effects change the controller but not the owner.

Report each finding as:
1. What the oracle text says
2. What the code does differently
3. A concrete game scenario where this produces wrong output
4. The corrected implementation

---

## How to Use This Skill

This file is not an invokable slash command. To use it, ask Claude to read it and answer your question or perform a task. Claude will also consult it automatically when working on MTG-related code in this project, as directed by `CLAUDE.md`.

**Rules question**:
> "Read `.claude/skills/mtg-rules.md` and answer: does first strike prevent deathtouch from killing my creature?"

**Code review**:
> "Read `.claude/skills/mtg-rules.md` and review this combat damage implementation for rule violations."

**Build guidance**:
> "Read `.claude/skills/mtg-rules.md` and give me the recommended data model and patterns for implementing the stack."

**Card implementation — generate code**:
> "Read `.claude/skills/mtg-rules.md` and implement this card: `When Mulldrifter enters the battlefield, draw two cards. Evoke {2}{U}`"

**Card implementation — review code**:
> "Read `.claude/skills/mtg-rules.md` and review this code against the card text: `Whenever a creature dies, you gain 1 life.` [paste code]"

When answering rules questions, cite the relevant Comprehensive Rules number (e.g., CR 704.5a) and note common edge cases or misconceptions.
