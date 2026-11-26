# Enchanted Evening: bucketed cleanup discard (DESIGN, for USER review, 2026-11-26)

Status: **proposed, not built.** Discard buckets are USER-owned. This is the AI's draft. USER 2026-11-26: *"Regarding
discard I would like you to make a suggested approach and, in general, the bucketing approach is what we should be
using. In other words dividing cards into buckets by function."*

## Why now

Today `EnchantedEveningProvider` has no discard hook, so the deck uses the generic rule: shed the highest mana value
first. That never sheds a land. Lock step 8b had to delete a dead-land fold ("under EE + Opalescence, playing a land is
dominated by holding it") for exactly that reason. The counterexample: a held land can force a later NONLAND discard,
while the line that played the land (it dies at once) kept its spells. Under a land-first shed in the lock regime, a
held land is never worse than a played one. That makes the fold exact, so it can return.

List (60): 4 Enchanted Evening, 4 Opalescence, 1 Parallax Wave, 3 Calming Verse, 4 Force of Vigor, 4 Sterling Grove,
4 Idyllic Tutor, 4 Enchantress's Presence, 1 Exploration, 2 Oblivion Ring, 4 Wild Growth, 4 Utopia Sprawl, 20 lands
(6 Forest, 2 Plains, 4 Temple Garden, 1 Sunpetal Grove, 4 Windswept Heath, 4 Wooded Foothills).

## Two regimes

**LOCKED** = Enchanted Evening + Opalescence both ours. Every permanent is then an enchantment creature with P/T = MV, so
every land (ours too) is a 0/0 and dies, and a land Aura dies with its land. We have no mana. What still works is:
Parallax Wave's counters (no mana), Force of Vigor's pitch (exile a green card), and Enchantress's Presence draws. To
confirm when built: the engine agrees we have no mana source under the lock.

**PRE-LOCK** = everything else. The deck is assembling the lock, or beating down with Opalescence (USER rulings 15/16
and the tutor-prune amendment).

## Tutors fill empty slots (USER 2026-11-26)

USER: *"Tutors should fill in spaces (especially for lock pieces) that are empty rather than having their own
buckets."*

So Idyllic Tutor and Sterling Grove have no bucket of their own. A tutor is kept only as a STAND-IN for a slot that is
otherwise empty. Slots are filled in the ruling-16 tutor order: missing lock pieces first (EE > Opalescence >
Parallax Wave), then the one mana Aura while mana is needed, then Presence. A slot already filled by a real card in
hand or on the battlefield takes no tutor. Two missing lock pieces can hold two tutors.
- Which tutor fills a slot first: a Grove already on the battlefield (its sacrifice is the tutor), then Idyllic Tutor
  in hand, then a Grove in hand.
- A tutor that fills no slot is overflow. Enchantress's Presence is the same kind of filler: one Presence fills one
  slot still empty after the tutors (see DRAW below). A Grove in hand still fills the BEATDOWN slot when the amendment's
  conditions hold.

## Buckets, PRE-LOCK (quota first; only the overflow is shed)

USER 2026-11-26: *"Exploration is pretty solid, but only if we happen to be land-heavy. If we are I would keep it. Top
priority should probably be the 3 lock pieces or tutors for them followed by mana. Enchantresses's presence should be
good if we are still have empty spots from those pieces. Note that things on the board should count for any of the
buckets which means we have more space when lands or enchantments are out to keep other things."*

**Every quota counts the battlefield as well as the hand.** A land or an enchantment already out fills its slot, so the
hand has room for the lower buckets. Buckets in KEEP priority, highest first:

| # | bucket | members | quota (hand + battlefield) |
|---|---|---|---|
| 1 | LOCK | Enchanted Evening, Opalescence, Parallax Wave, or a tutor standing in for a missing one | 1 of each |
| 2 | MANA | lands, Wild Growth, Utopia Sprawl | up to the ruling-16 "mana is fine" line: 5 total with 2 white; white sources first while white < 2 |
| 3 | DRAW | Enchantress's Presence | **a slot filler, like a tutor** (USER 2026-11-26: *"for Enchantress's Presence I would keep it above mana if we cannot fill our lock piece quota."* / *"Essentially, it moves to fill one of the spots."*): ONE Presence fills ONE empty slot, after the tutors have filled what they can, LOCK slots first, then MANA. It sheds with the slot it fills. Filling nothing, it is kept just above EXPLORATION (below ANSWER) |

| 4 | ANSWER | Calming Verse, Force of Vigor | **1 in total, Calming Verse preferred** (USER 2026-11-26: *"I would only keep 1 of Calming Verse or Force of Vigor, probably priortizing Calming Verse?"*) |
| 5 | EXPLORATION | Exploration | USER 2026-11-26: *"Exploration should remain below anything not crucial except maybe if we have 3-4 lands we are keeping. Exploration replaces no other cards, so it might have to go in a pinch."* -> it fills no slot. Default: the FIRST non-dead card shed (below Oblivion Ring). When 3+ lands in hand are being kept (MANA quota + land-heavy): kept just below MANA, above every non-crucial bucket; still shed before LOCK / MANA |
| 6 | BEATDOWN | 2nd Opalescence, 2nd Grove while EE is not ours and an Opalescence is ours (the amendment's lines) | 1 each |
| 7 | LOW | Oblivion Ring | 0 |
| 8 | DEAD | copies beyond the quotas; tutors that fill no slot (Idyllic before Grove) | 0 |

**Shed order, most expendable first** (the reverse of the keep priority; within one bucket the overflow goes first):
DEAD -> EXPLORATION (unless 3+ kept lands in hand) -> LOW (Oblivion Ring) -> lands beyond the MANA quota (basics that fix no missing colour first, then the fetches) ->
mana Auras beyond the quota while no Enchantress's Presence is ours -> BEATDOWN overflow -> mana Auras beyond the quota
while a Presence is ours (each draws a card off it) -> a 2nd Presence -> ANSWER overflow (every
Force of Vigor while a Verse is kept, then extra Verses) -> DRAW filling no slot -> ANSWER -> EXPLORATION (with 3+ kept lands) -> MANA within the quota (a
Presence filling a MANA slot sheds just before the real mana card would) -> LOCK (a Presence or tutor filling a
LOCK slot sheds just before the real piece would) (Parallax Wave, then Opalescence, then Enchanted Evening
last). A tutor standing in for an empty slot sheds with that slot's bucket, just before the real card would.

## Buckets, LOCKED

A card that costs mana is dead while the lock holds, except as Force of Vigor pitch fodder (any GREEN card), or if
the lock ends (our own Wave exiling Opalescence or EE).

**Shed order, most expendable first:**
1. Lands, all of them. Playing one kills it, so this shed makes the dead-land fold exact.
2. Non-green dead cards: Oblivion Ring, Idyllic Tutor, surplus Opalescence / EE / Wave.
3. Green cards beyond the pitch need (one green card per Force of Vigor in hand): Wild Growth, Utopia Sprawl,
   Exploration, Sterling Grove, surplus Presence, surplus Calming Verse.
4. Force of Vigor beyond what the threats need. A 2nd FoV is also pitch fodder for the 1st, so it counts as fodder.
5. Kept last: one Force of Vigor plus its pitch card.

How the lock can end (USER asked 2026-11-26, *"How does the lock break?"*): in this goldfish model, only by OUR choice.
The opponent removes nothing; the fairness check (rulings 8-13, 19-21) only withholds the free-turn credit. Calming
Verse cannot hit our enchantments under the lock (it needs mana, and its own-side clause needs an untapped land we do
not have). So the lock ends only if our Parallax Wave exiles our own EE or Opalescence (they return when the Wave
leaves) or our Force of Vigor destroys one. Spare copies of EE / Opalescence are therefore DEAD under the lock and
are shed in step 2 (orchestrator default; they are also not green, so they are not pitch fodder).

## Consumers and measurement

* The real game's cleanup, and every rollout, read the same ranking. The rollouts today take index 0 of the generic
  ranking.
* Ships behind a flag, DEFAULT OFF until the USER approves. Then it gets a held-out A/B (200 games, as for the tutor
  prune) and the dead-land fold returns behind the same flag.

## Open points for the USER

1. SETTLED (USER 2026-11-26): Exploration's place, and the BEATDOWN extras. *"Extra attackers can be prioritized over
   extra lands and maybe extra mana auras, though the latter is debatable because those can draw cards with
   Enchantresses's Presence."* -> drafted: kept over extra lands always; over extra mana Auras only while no Presence
   is ours. Earlier: *"Extra attackers can be
   discarded early if needed, I think."*
2. LOCKED: spare EE / Opalescence are drafted as dead (the lock only ends by our own choice). Under the lock FoV
   (not Verse) is the kept answer, because Verse needs mana and FoV can be pitched.
