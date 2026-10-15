# Metallic Mimic / Adaptive Automaton cannot be tested in a TOKEN deck: the chosen type is an unsearched greedy pick that excludes tokens

**Status:** blocker found 2026-10-15, NOT fixed. One adjacent rules bug found and fixed in the same
read; its patch is parked, not committed (see §4). The decision in §5 is the **user's**.

**Why this exists:** USER 2026-10-15, on the Fungus candidate-b screen: *"Another card to consider
is metallic mimic or even adaptive automaton. However, those probably won't be the greatest because
you can only choose one of fungus or saproling."* The objection is right, but the reason is worse
than the user's version: **the engine does not let this deck choose at all, and what it chooses is
the wrong half.** Round 32 therefore omits both cards rather than burying them on a number they
were never given a chance to earn.

---

## 1. The cards

| | cost | body | parameters |
|---|---|---|---|
| Metallic Mimic | `{2}` | 2/1 Shapeshifter | `chosen_type_added_to_self`, `other_chosen_subtype_enters_counters: 1` |
| Adaptive Automaton | `{3}` | 2/2 Construct | `chosen_type_added_to_self`, `lord_affects_chosen_subtype`, `power_bonus: 1`, `tough_bonus: 1`, `lord_excludes_self` |

Mimic gives a **+1/+1 counter** to each other creature of the chosen type as it enters — permanent,
and it survives the Mimic dying. Automaton is a **static anthem** for the chosen type.

## 2. The defect

`FireEtbWatchers` (`src/core/SpellEffects.h`) resolves the type once, as the permanent enters:

```cpp
if (ed && ChoosesCreatureTypeOnEnter(ed->params))
{
    const uint16_t chosen = DominantCreatureSubtypeId(state, e.controller_index);
    state.battlefield[entered_index].chosen_subtype_id = chosen;
```

`DominantCreatureSubtypeId` tallies library + hand + graveyard + staged + suspended + **non-token**
battlefield permanents. Tokens are excluded deliberately, and the comment says why:

```
// Tokens are excluded: they are not deck cards, and counting them would let a board of Lathliss
// 5/5s re-decide the tribe mid-game (breaking the game-constant property above).
```

That is a sound reason for a *tribal* deck and the wrong answer for a *token* deck. The Fungus
candidate list's creature **cards** are:

| subtype | copies |
|---|---|
| **Fungus** | **13** |
| Badger | 4 |
| Druid | 4 |
| Saproling | 1 *(Shroofus Sproutsire, the only Saproling card in the deck)* |

so the vote returns **Fungus**, in every game, in both the executor and the rollout (one shared
helper, by design). Meanwhile the deck puts **~40 Saproling tokens** on the battlefield per game —
Saproling Burst, Saproling Migration, six spore outlets, Mycoloth — and the vote cannot see a single
one of them.

**Two separate rule violations, not one bug:**

1. **It is an unsearched greedy pick at a node with depth remaining.** Naming a creature type is a
   real choice with real alternatives, and `CLAUDE.md`'s no-greedy rule reserves choices like this
   for the search: *"The engine ... makes no heuristic choice at any node with search depth
   remaining."* There is no branch here at all.
2. **The better line is UNFINDABLE AT ANY BUDGET.** `A BUDGET MAY RE-PRICE WORK, IT MAY NOT MAKE A
   LINE UNFINDABLE` — *"everything should be findable at unlimited or large budgets."* Mimic-naming-
   Saproling is not a line the engine spends less time on; it is a line the engine cannot express.
   `--budget-ms 0` and `--depth 8` reach it exactly as often as `--budget-ms 1` does: never.

## 3. Why that makes the arm worthless rather than merely pessimistic

Mimic naming **Fungus** pays off only on Fungus creatures entering *after* it. Off 13 Fungus cards
in a ~7-turn goldfish, that is typically one or two bodies — a near-blank. Automaton naming Fungus
is a 3-mana, one-type, excludes-self copy of **Sporecrown Thallid**, which costs `{1}{G}`, hits
**both** Fungus *and* Saproling, and is already at its 4-copy cap in the list.

So an arm would have measured a near-blank and reported "Mimic is bad". That conclusion would have
been about the engine wearing the clothes of a conclusion about the card — the
`bracket-notes-are-the-judgement-call` failure, and the one thing `deck-screening.md` says no guard
can catch for you: *"what no guard can do is notice that the engine models one side of a comparison
more completely than the other."*

**What the cards would be worth if the choice were available** (arithmetic, not a measurement, and
the reason this is worth the user's attention): Mimic naming Saproling puts a +1/+1 counter on every
one of ~40 token bodies. Under one Doubling Season that is **two** counters (§4), under two Seasons
**four**. The deck's entire plan is a wide attack, and this converts every 1/1 into a 3/3 for two
mana. It also lifts **Saproling Burst** tokens off zero — they enter as X/X where X is the fading
count and shrink one per upkeep, so a permanent counter makes each one live a turn longer and hit
harder for its whole life. None of that is reachable today.

## 4. The adjacent rules bug, found in the same read, FIXED but PARKED

The as-enters counter site called `AddPlusCounters`, which bypasses `DoublerShift`:

```cpp
if (add > 0) { AddPlusCounters(state.battlefield[entered_index], add); }   // was
if (add > 0) { PutPlusCounters(state, state.battlefield[entered_index], add); }   // is correct
```

A counter a permanent **enters with** is still doubled by Doubling Season (CR 121.6 / 614.1c — the
same rule that doubles a planeswalker's starting loyalty, and the reason devour's enters-with
counters compound). So a Mimic naming Saproling in a Doubling Season deck gave each token **one**
counter where the rules give it **two**. `PutPlusCounters` delegates to `AddPlusCounters`, so the
counter-merge property the old comment existed to protect (un-merged `Counter{}` entries split the
`BuildSimKey` transposition key) is unchanged.

`PutPlusCounters`' own block already named this as the case to convert: *"A future deck pairing a
doubler with another counter source must convert that source's site."* The candidate-b screen is
that deck.

**It is inert for every committed deck**, which is why no ground truth moves: it fires only with a
counter doubler **and** a Mimic/Giada on one battlefield, and the only doubler decks in the repo are
Fungus (Doubling Season, no Mimic) while the only Mimic/Automaton deck is Pirates (no doubler).
Giada, Font of Hope shares the site and is likewise unaffected.

**Parked, not committed, and the reason is methodological:** the patch is at
`logs/mimic_fix/mimic-doubling.patch` (34 lines, applies to `src/core/SpellEffects.h`). Committing
it changes `HEAD:src`, which invalidates `build/PGO/SRC_TREE` and would have put round 32 on a
different binary from round 31 — and round 32 exists to **replicate** round 31 arms on held-out
seeds, so binary parity between the two is worth more than landing an inert fix a day earlier. Land
it with ordinary smoke validation once the screen's replication pair is complete. 596 unit tests and
2,646,986 assertions already pass with it applied.

## 5. The decision, which is the USER's

Three routes, in the order I would recommend them:

**A — Search the choice (recommended).** Fan one cast variant per candidate type, with the candidate
set restricted by the provider to subtypes the deck can actually field — including the subtypes of
**tokens its own cards create**. That restriction is a legal provider narrowing (*"only to RESTRICT
or ORDER options"*), while the choice itself becomes searched, which is what the no-greedy rule
requires. It is narrow in practice: Fungus gets `{Fungus, Saproling}` after pruning to types with a
reachable body, and **Pirates gets a one-element set, so its play and its USER-adopted cast-order
heuristics are untouched**. This is the only route that satisfies both rules in §2.

**B — Fix the tally only.** Count the creature-token subtypes the deck's cards create, so the single
pick lands on Saproling. Cheap, no new branching, and it keeps the documented game-constant property
(still computed from deck cards, never from board state). But it leaves a greedy pick at a real
decision node, and it is wrong on the boards where Fungus *is* right (a hand of Fungus with the
token makers already spent). It is also precisely the shape CLAUDE.md forbids — *"A default-off flag
that gates a missing searched capability is the same defect. Delete, don't lever."*

**C — Leave it and drop both cards from consideration.** Defensible: the user's instinct was already
that they *"probably won't be the greatest"*, and Sporecrown Thallid at 4 copies covers the lord
role for both types at two mana. The cost of this route is that it is a decision made by a
limitation rather than by evidence, and §3's arithmetic says the ceiling being given up is large.

Until the user rules, **both cards stay out of the screen** and neither is recorded as measured.
Nothing here is settled by an agent — not even "leave it out".
