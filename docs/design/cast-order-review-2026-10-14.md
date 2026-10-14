# Cast-order review: 34 AI-drafted per-deck orders (2026-10-14)

## 1. What this is

On 2026-10-14 you asked for every deck's cast order to be reviewed: *"The generic order is never a good option in my
opinion. It should be edited by you and then presented to me for additional feedback. It might not hurt to also fix
up the generic order, but the real solution is going to be to have an agreed upon order for each. Major bonus points
if that can largely be constructed by AI as well."* Agents then wrote one cast-order draft per deck, 34 in all. Each
drafter followed the shared `_BRIEF.md`, read every card from `cards.json`, and checked its claims with single-game
probes on the current Release binary. The full drafts are in
`docs/design/cast-order-drafts-2026-10-14/<Deck>.md`. Every number and quote below comes from those drafts.
**No code has been changed. Every proposal here is unimplemented and waits for you.** Cast order, its ranges and
main 1 / main 2 are your decisions. The measurements are input to them.

How the decks split by status:

- **B, your order exists but is OFF:** 2 decks, plus Prevent Damage, whose order is ON (A) but whose main-phase rule is OFF (B).
- **C, an agent order you never reviewed:** 5 decks.
- **D, generic tiers only:** 15 decks. 12 are playable. Hydras, Mill and Unpredictable Cyclone are not, because their cards are unimplemented.
- **A, your agreed order is shipped ON:** 11 decks.

**How to read the orders below:**

- `>` means "cast before".
- `=` is a tie the drafter showed cannot matter.
- `[a..b]` is a range, from the ideal slot to the cost-efficient slot.
- `\|` separates main 1 from main 2. A cast is in main 1 unless marked.
- "land" is the land drop.

## USER rulings from the walk-through (2026-10-14) -- these SUPERSEDE the drafts below

Each deck was reviewed by the integrating agent first (draft + card text + the USER's earlier rulings), and its
suggestion was put to the USER. Settled items only; a deck not listed here is not settled yet.

### CROSS-DECK RULING: every order is TOTAL (USER 2026-10-14)
USER: "Let's fully order the cards in each list inasmuch as possible. I don't like the randomness of choosing the
order." -> no tie is ever left to hand / plan order. Functionally identical cards still get a fixed order (by
name); equal-looking cards get a stated tie-break rule; the comparator falls back to a fixed key (name), never to
input order, and a unit test pins that permuting the hand does not change the realised order. A SEARCHED choice
(the search tries the USER's slot first and decides) is not randomness and stays where approved.

### Hinata2 -- SETTLED
- The USER's 08-28 order stands, with Gamble before the cantrips (the agent's reason: Gamble's search shuffles
  the library, so a Gamble after Ponder/Preordain throws away what they set up; the hand size argument does not
  hold, a cantrip replaces itself) and the order made BINDING inside cantrip / Expressive Iteration plans
  (HinataProvider opts into OrderOpaqueCastsByRank).
- IRENCRAG FEAT (USER): "Your change is fine, but Irencrag needs to be castable after Soulfire and after Magma
  Opus, since it needs to go second last. There has to be flexibility around that." -> Irencrag is always
  SECOND LAST IN THE PLAN: immediately before whichever payoff the plan ends on. Irencrag -> Soulfire,
  Irencrag -> Opus, Irencrag -> Crackle, Soulfire -> Irencrag -> Crackle and Opus -> Irencrag -> Crackle are
  all expressible; the search chooses the payoff set. Since "only one more spell" leaves second last as the
  only useful slot, the placement follows from the plan (no extra branch is needed).
  USER, confirming: "So, in reality the Irencrag can go in any of three spots depending on what other spells are
  cast, but it is always second last." / "So we can restrict it using that." -> the RESTRICTION: a plan casts
  Irencrag only as its second-last spell with Soulfire, Magma Opus or Crackle as the last; any other plan holding
  Irencrag is pruned (the 09-01 "just those 3 cards" waste gate, stated as a position rule).
  Implementation: a set-level placement (ArrangeCastOrder, the Dragonstorm Irencrag-closer precedent), and the
  two subset walks + the provider's waste gate (HinataProvider::RestrictorSubsetWasted) must count Irencrag's
  follower in the SAME arranged order, or legality and realisation split.
- Main phase: unchanged (all main 1; all-main-2 measured worse).

### StompySurprise -- APPROVED TO TRY (USER 2026-10-14)
USER: "Let's try out your stompy suggestion. It seems pretty close to what I was looking for and we can fix any
issues that show up." The suggestion (from the draft, reviewed):
land > Mystic = Fyndhorn = Llanowar > Sol Ring > Priest > Archdruid > Call of the Wild > Turntimber (before OR
after Worldly Tutor, searched) > Natural Order early (searched; only with a mid tutor in the plan) > Worldly Tutor
mid (searched; only when a Turntimber in the plan takes the stacked card) > Vaultborn > Ghalta (new) > World War
Hulk early (no Craterhoof/Ghalta in hand) > Elderscale = Hornet Queen = Terastodon = Apex = Worldspine > Natural
Order late (default) > World War Hulk late (Craterhoof or Ghalta in hand) > Craterhoof > Worldly Tutor late
(default). All main 1. Shipped WITH TOP_RESOLVE (the USER's Worldly Tutor reset) as a Stompy opt-in; the four
"either location" cards are searched two-way choices, the USER's slot first; the dual-position tests read the
PLAN, not the hand; Vaultborn is not a top-consumer.
Queued, separate (defaults yes, not yet answered explicitly): the viewer chooser for MTG_UPKEEP_CALL then ON;
engine fixes -- Call of the Wild activation timing searched (today every activation trails every cast, so a
Call-put creature never counts for a same-turn Craterhoof and the USER's [6] is inexpressible), and World War
Hulk's chapter-I freed creature searched (today a resolution-time pick).

### Prevent Damage -- USER direction 2026-10-14 (revised order pending confirmation)
USER: "I have some concerns about prevent damages' order having so much search in the middle. I don't think that
needs to be fully searched. I would always choose Vito before Rhox, since Rhox is pretty much only that effective
with Vito. Spellshock and Manabarbs are similarily only good if we have active drain effects, but you might have
some point that these are relevant and potentially searchable if we already have active drain. I think we can at
least minimize what we search there."
Revised (agent, put back to the USER): land > Tamanoa > GSZ for Tamanoa > Purity (sb) > Dina (and GSZ for Dina) >
Vito [Vito > Dina when Rhox or Bilbo is already on the battlefield] > Rhox > Bilbo (sb) > wished bodies (cheapest
first) > Spellshock > Manabarbs > Living Wish > Beseech > GSZ X=0 > Rolling Earthquake > Pyrohemia. The ONLY search:
Spellshock and Manabarbs each get a second slot (right after Tamanoa / GSZ, before the creature casts) and only
when a drain is already on the battlefield at the start of the main; Manabarbs' two-way only matters once a plan
that casts it pays land by land (today every land is tapped before it resolves).

### Auras -- SETTLED ORDER (USER 2026-10-14), two questions still open
No earlier USER cast order for Auras exists anywhere on record (docs, git, code, transcripts back to 09-11); the
USER: "For now if you can provide fully split orders for me to review I'll just work from that." then "Maybe Lion
Umbra before Daybreak just in case." The order (all main 1):
land > Light-Paws > Kor Spiritdancer > Slippery Bogle > Gladecover Scout > Armadillo Cloak > Alpha Authority >
Spirit Mantle > Spirit Link > Gryff's Boon > Hyena Umbra > Spider Umbra > Audacity > Rancor > Ancestral Mask > All
That Glitters > Ethereal Armor > Lion Umbra > Daybreak Coronet.
Lion Umbra before Coronet -- USER's reason: "So that you could play 2 Daybreak." "(if light paws is out)": casting
Lion Umbra (MV 2) lets Light-Paws fetch a Daybreak Coronet (MV 2, a name not yet controlled) while our own Coronet
is still in hand -- two Coronets, the same duplicate-the-later-card principle as Ethereal Armor last (4 Coronets,
1 Lion Umbra in the list). Also: "enchant modified creature" accepts counters / Equipment, Coronet needs an Aura.
Open: search the Aura order on Light-Paws turns (this order first)? Fix the Light-Paws fetch pick to the USER's
08-25 wording as its own measured change?

### CritterLifegain -- SETTLED (USER 2026-10-14)
USER: "For CritterLifegain Ajani, Strength of the Pride should go after Ajani's pridemate because it produces them.
The activation that produces them should go right after, whereas the +1 can go where you put it originally. Heliod
is a trickier one and might require some adjusting. It can go right where it is if it is a creature. Otherwise it
probably should go after other creatures until it is one. I'm not entirely sure on that, though. If it isn't a
creature after the next 2 have entered then it might actually be better to play it before. This would be rare,
but we could search or evaluate this case. Otherwise it seems fine."
Order (all main 1): land > Soul Warden > Soul's Attendant > Auriok Champion > Daxos > Archangel of Thune > Voice of
the Blessed > Ajani's Pridemate > Ajani, Strength of the Pride WHEN CAST FOR ITS -2 (the Pridemate token; the
activation rides the cast, so it fires right after) > Heliod > Ocelot Pride > Serra Ascendant > Ranger-Captain of
Eos > Ajani WHEN CAST FOR ITS +1 > Unexpectedly Absent.
HELIOD: at its slot if it enters as a creature (white devotion >= 5); otherwise it walks later past up to the
next 2 creature entries until it is one; if it still is not a creature after 2, a two-way SEARCHED choice: first
(before every watcher, so it is out for every gain) or after the creatures (rare, so cheap).
The flip (payoffs before a new watcher when board watchers >= payoffs + 2 and no Thune) kept ("Otherwise it seems
fine").
Implementation: Ajani's slot by the loyalty ability the cast action carries (CastOrderActionRank sees
Action::loyalty_ability); Heliod by a set-level placement (ArrangeCastOrder) in both worlds.

### EldraziDisplacerFlicker -- SETTLED (USER 2026-10-14: "Your EDF changes look reasonable to me. You can proceed with those.")
Order (all main 1): land > Wild Growth > Fertile Ground > Trace of Abundance > Overgrowth > Cloud of Faeries >
Peregrine Drake > Training Grounds > Eldrazi Displacer > Emiel the Blessed > Dimensional Infiltrator > Essence
Depleter > Vexing Shusher > Living Wish > Eladamri's Call. Plus: (2) Fertile Ground / Trace / Overgrowth may move
behind the untappers when the turn is otherwise unpayable (engine: the payable-order projection must credit an
untapper's refund and a land Aura's bonus); (3) Trace after any other Aura on the SAME land (the Overgrowth-then-
Trace stack restored); (4) the COMBO OFF route deploys Auras in this order; (5) the route no longer skips a second
untapper whose cast refund is below its cost when its loop is the net-positive one.

### Angels -- SETTLED (USER 2026-10-14: "The angel's order + caveats look good.")
Order (main 1): land > Sol Ring > Lightning Greaves > Bishop of Wings > Righteous Valkyrie > Archangel of Thune >
Lyra, Archangel of Dawn > Giada, Font of Hope > Youthful Valkyrie > Lyra Dawnbringer > Lightstall Inquisitor >
Resplendent Angel > a duplicate legend already on the battlefield > Serra the Benevolent > Swords to Plowshares >
Unexpectedly Absent. Caveats: Giada moves to right after Greaves when the line needs her Greaves-hasted {W} for an
Angel ranked ahead of her; with no converter (Bishop / Righteous / Seraph Sanctuary) on the board or in the casts,
Thune and Lyra AoD drop behind Giada and Youthful.
SECOND MAIN (USER): "I agree, though, that the second main is potentially wanted here. It would be only exactly
cases where Giada can attack and we would use her mana where it would be considered. I wonder if we can use that to
reduce any associated costs. The idea would be for it always only to be one spell maximum. We don't need to consider
multiple since she can only pay for or help pay for one spell." -> a main 2 opens ONLY when a vigilant Giada can
attack this turn and her post-combat {W} (Angel spells only) pays for or helps pay for an Angel; main 2 holds AT
MOST ONE cast, which must use her mana. Lever, measured, adoption on the bar.

### BreachingDragonstorm -- SETTLED (USER 2026-10-14), low priority
USER: "I'm not too worried about Breaching Dragonstorm's order, since it literally dumps most of the creatures in the
deck on the field. I don't think we can fail to kill with essentially any order so your order is fine. Plus, there
is almost no chance of casting two of these spells in one turn. The cheapest is 5 mana, so that would need 10 mana."
Order (main 1): land > Breaching Dragonstorm > Boarding Party > Annoyed Altisaur > Maelstrom Wanderer > Creative
Technique > Call Forth the Tempest > Sakashima's Protege, with the provider opting into OrderOpaqueCastsByRank so
the rank applies at all. Implement batched with other low-stakes decks.

### Soldiers -- SETTLED (USER 2026-10-14: "Soldiers' order also looks good.")
Order (main 1): land > Champion of the Parish > Recruiter of the Guard > Ranger-Captain of Eos > Thalia's Lieutenant
(at 4, the amendment: after Champion and the tutors) > Aether Vial > Recruitment Officer > Esper Sentinel > Thalia,
Guardian of Thraben > Coppercoat Vanguard > Cathar Commando > Jirina > Harbin > Brutal Cathar > Field Marshal >
General Kudro > King Darien XLVIII > Rick (6-17 order-free; the measured 10-04 sequence keeps it total). Fix the two
"user-proposed" labels (vial-put-interleaving.md:42, the soldiers_vial_after_tutor_continuation scenario comment).

### burn -- SETTLED (USER 2026-10-14)
USER: "The burn order seems reasonable, though I wouldn't cast any spell but Light Up the Stage, Searing Blaze (if
missing a land drop) or Eidolon in main 2 without playing them off main 2 Light up the Stage."
Order: land > Goblin Guide > Monastery Swiftspear > Lightning Bolt > Shard Volley > Searing Blaze > Searing Blood >
Skullcrack > Light Up the Stage > Eidolon of the Great Revel.
- LAND first, two exceptions: before Searing Blaze whenever Blaze is cast and a land is in hand (landfall, 3 not 1);
  after Light Up the Stage when the turn pays for Light Up without it (a land Light Up exiles can be played and the
  hand land kept).
- Haste creatures first (Swiftspear's prowess sees every spell), by name. Burn by cost, then name. Shard Volley JOINS
  the burn (it no longer trails every cast in the sacrifice-land pass); the USER's Volley hold rule is unchanged.
- Light Up the Stage after the burn and the creatures: main 1 when a burn spell earlier in that main enables
  spectacle (USER 08-17), otherwise main 2 for {R} after combat damage. Eidolon is the turn's last cast.
- MAIN 2 (USER): the only spells cast in main 2 are Light Up the Stage, Eidolon, and Searing Blaze while the land
  drop is missing (no land has entered this turn, so a land a main-2 Light Up exiles can still turn landfall on).
  Every other spell is main 1 unless it is played off a main-2 Light Up the Stage. Agent reading, surfaced: "played
  off" admits the cards that Light Up exiled AND hand spells cast after it in that main (e.g. paid by a land it
  exiled); the narrower reading (exiled cards only) is the alternative.
- Engine change (agent default yes, the question was not answered explicitly): the search credits spectacle when a
  face-damage spell precedes Light Up in the same plan, so Swiftspear -> burn -> Light Up for {R} becomes findable
  (seed 9295 gi 294, a turn-3 kill the engine misses at every budget).

### Fluctuator -- SETTLED (USER 2026-10-14)
USER: "I guess the Fluctuator order is okay. The order doesn't matter much in that deck, except that cycling always
goes at the end. (that should also be placed) Why can't we use a land drawn by cycling? We should be able to consider
it whether this is in a go-off heuristic or actual search. And yes, let's look into potential problems."
Order (single main): land > Unearth > Enlightened Tutor > Drannith Stinger > Fluctuator > Hollow One > a second
Fluctuator (one already on the battlefield; it does nothing) > CYCLING (every cycling activation comes after every cast
of its window -- the USER asked for it to be PLACED in the order explicitly, not left implicit in the dig loop).
Forsake the Worldly is never cast (its only targets are our own Fluctuator / Hollow One).
- A LAND DRAWN BY CYCLING (USER): it must be considerable as the turn's land drop, by the dig chain's own rule (the
  go-off heuristic) and by the search. Today it is not, for four reasons found in the code: (1) a land draw never
  stops the chain -- the only exception is Fluctuator's pending-Unearth case (DigResolveEvenOnLand); (2) at a nonland
  draw the "anything castable now?" gate (AnyHandCastableNow) counts only battlefield mana, never the open drop; (3)
  the deck has no second main, so no later pass plays the drop -- unless the chain stops, the drop is lost for the
  turn; (4) the fodder pick (SelectDigSource) may cycle the land away -- the USER's "occasionally an untapped land"
  protection (ledger 7.5) was never built.
- INVESTIGATE (USER: "yes, let's look into potential problems"): the free Hollow Ones the chain cycled or left in
  hand (4 of 30 probes), and a census of lands drawn by cycling with the drop open and never played.
- Open: skip a second Fluctuator entirely (MTG_NO_REDUNDANT_REDUCER, built, OFF) -- not answered; it is measured and
  stays OFF until the USER rules.

## Start here: eight answers that unblock most of the rest

There are 141 questions below. Each has a default, but a default is not an adoption: until you answer, the
current order keeps playing. These eight unblock the most:

1. **How should an agreed order get adopted? (Q138)** (a) you say yes, then a held-out A/B, then it ships ON; or
   (b) the A/B runs first, then you decide. [a]
2. **Cast order as profile data (§7.3).** Express each deck's order as a param -> rank table in its profile, read by
   one shared hook. Every draft then becomes an arm in ONE pooled batch, and most of the per-deck questions arrive
   with measured numbers instead of a request to trust the draft. This would be a one-time engine change, and it
   changes no play until a deck's table is filled in. [build it first]
3. **Your own orders and rules that never ran (§3a).** Each one has an amendment ready:
   - Hinata2: Irencrag at 18, and the order made binding in cantrip plans (Q1, Q2).
   - StompySurprise: re-measure with TOP_RESOLVE, plus the searched 2-way slots (Q6, Q8).
   - Prevent Damage: an end-of-main-2 pain sweep, then re-measure ALL_M2 (Q13).
   - FiveColour: wire the 08-19 dork rung (Q99).
   - Creature Giving: cast Crop Rotation at its slot, not last (Q88).
   - EDF: the combo route follows your Aura order (Q32).
   - Mirrorwing: Gold Rush after Fists (Q113).
   One "yes to all of §3a" builds each as a lever and brings back its measurement.
4. **Decisions agents settled without you (§3b):** `MTG_ORDER_M1_FIRST` off (Q87), Prevent Damage's second main (Q13),
   SelesnyaLifegain single-main (Q77), WhiteKnights single-main (Q124), the Soldiers order (Q33), Bruna on the
   generic order (Q21), Snow's draw-early result (Q119).
5. **Rank ties fall to hand order (G1).** This is the single most common finding. In nine decks a tie between two
   order-sensitive casts loses a line even at unlimited budget (Anti-Lifegain, CritterLifegain, EDF, Giants,
   Minotaur, Mirrorwing, Soldiers, slivers_vial, WhiteKnights). Each deck's draft breaks its ties. Should ties
   become illegal by policy: every tie either proven order-free or given a rank? [yes]
6. **Per deck now, generic later (Q139).** Apply the drafts deck by deck, and change `GenericProvider` only after a
   suite A/B of the revised tier list in §5. [yes]
7. **The report (Q137) and single-main labels (Q136).** These change no play. [yes]
8. **The provisional decks (Q141):** give Hydras, Mill and Unpredictable Cyclone their own providers now, so they
   stop being routed to AntiLifegain through Scalding Tarn. [yes]

**Engine defects in §6 that break standing rules, and so need no cast-order answer.** They are proposed as the next
engineering work:
- E1: slivers' replicate count is a greedy pick inside the search window.
- E14: `MTG_BP_NEW_ONLY` makes lines unfindable at budget 0. The breakpoint-options audit now running covers this.
- E2: burn's Light Up the Stage spectacle line is never enumerated.
- E5: treasure_hunt's depth-0 land rule lives in the engine. Per your 10-14 ruling, deck rules move to providers.

## 2. Status table

| Deck | Status | Proposed order | Biggest finding |
|---|---|---|---|
| Hinata2 | **B**: your order is OFF | land > Sol Ring > Hinata > Reality Spasm > Gamble > Ponder=Preordain > Expressive Iteration > Ornithopter > **Irencrag (22 -> 18)** > Soulfire > Magma Opus > Crackle; the 4 inert cards last; the order made binding inside cantrip plans | At rank 22, Irencrag has zero followers in the plan {Irencrag, Soulfire}. On seed 6600665 gi664 your order never wins (still unwon after 8 turns); at rank 18 it wins T5 |
| StompySurprise | **B**: your order is OFF (since 08-21) | land > Mystic=Fyndhorn=Llanowar > Sol Ring > Priest > Archdruid > Call of the Wild > Turntimber [12/15, 2-way] > Natural Order early [2-way] > Worldly mid [2-way] > Vaultborn > Ghalta / Hulk early > Elderscale=Hornet Queen=Terastodon=Apex=Worldspine > Natural Order late > Hulk late > Craterhoof > Worldly late | It never measured worse (08-25: -37 turns, 79:43, +3.26σ). It is held off by gate 2 (37 of 43 losses never recover), and that verdict was voided because the run lacked TOP_RESOLVE. On a T5 board, Craterhoof before Natural Order deals 24 instead of 36 |
| Prevent Damage | **A+B**: order ON, your m2 rule OFF | all main 2: land > Tamanoa > GSZ for Tamanoa > {Vito, Dina, GSZ for Dina}, pair order searched > Rhox > Spellshock > Living Wish=Manabarbs=Beseech=GSZ X=0 > Rolling Earthquake > Pyrohemia | The GSZ/Tamanoa tie falls to hand order and cost a full turn (probe P2: T7 vs T6). ALL_M2 is blocked because the pain sweep runs only at the end of main 1 |
| Auras | **C**: agent sub-order ON, unreviewed | land > Light-Paws=Kor=Bogle=Scout > Armadillo Cloak > Alpha Authority > Spirit Mantle > Spirit Link > Gryff's Boon=Hyena Umbra=Spider Umbra > Rancor=Audacity > Ancestral Mask > All That Glitters > Ethereal Armor > Daybreak Coronet=Lion Umbra (every turn) | The sub-order reads the board at the start of the phase, so it is off on the turn Light-Paws is cast (constructed example: +12 vs +9/+10; seen in 0 of 160 probes). Plain Auras tie in hand order, which costs Armor fetches (seed 2008 gi6 T3) |
| Bruna | **C**: agent order OFF, unreviewed | land > Wild Growth > Sol Ring > Glittering Wish=Open the Armory > Greaves > Birds > Avacyn's Pilgrim > Somberwald Sage > Troyan > Mother=Bruna=Linvala=Shusher > Arcanum Wings > Prodigious Growth=Mythic Proportions=Eldrazi Conscription \| Colossification | Sol Ring ties with Wild Growth at rank 5. With Sol Ring listed first, Wild Growth stays in hand at d0 and at d3/b200 (P1). With Sage listed first, Bruna lands T5, not T6 (P2) |
| CritterLifegain | **C**: agent order ON, unreviewed | land > Soul Warden=Soul's Attendant=Auriok Champion=Daxos > Thune > Pridemate=Voice > Heliod (placed by devotion) > Ocelot=Serra Ascendant=Ranger-Captain > Ajani > Unexpectedly Absent; the payoffs flip ahead when board watchers >= payoffs+2 and no Thune is out | Thune ties with Ocelot at rank 10. Hand [Ocelot, Thune] gives 8 counters and a 6-damage attack even at unlimited budget, vs 14 counters and 10 damage |
| EldraziDisplacerFlicker | **C**: agent ranks plus your Aura sub-order | land > Wild Growth > Fertile Ground [2..after Drake] > Trace [3..after Drake] > Overgrowth [4..after Drake] > Cloud of Faeries > Peregrine Drake > Training Grounds > Displacer > Emiel > Depleter=Infiltrator=Shusher > Living Wish > Eladamri's Call | Cloud ties with Drake at rank 5. Hand [Drake, Cloud] has no T4 win at any budget, with the route on or off; [Cloud, Drake] wins T4 |
| Soldiers | **C**: agent order OFF, you never answered | land > Champion > Recruiter > Ranger-Captain > Thalia's Lieutenant (17 -> 4) > Vial > Officer > Sentinel > Thalia > free block (Coppercoat, Commando, Jirina, Harbin, Brutal Cathar, Field Marshal, Kudro, Darien, Rick) | The generic 16-way tie casts Champion last. On s8800308 gi308 (and gi10, gi302) the kill is T5 instead of T4, and the T4 line is unreachable at d8 b0 |
| Angels | **D**: generic only | land > Sol Ring > Greaves > Bishop > Righteous Valkyrie > Thune > Lyra AoD > Giada [7..after Greaves] > Youthful > Dawnbringer > Resplendent=Lightstall > Serra > Swords=Unexpectedly Absent | The 9-way creature tie is settled by hand order. On T7, Thune then Bishop gives life 20 and no counters; Bishop first gives 24 plus counters. Greaves at rank 20 strands Giada's unlocked mana |
| BreachingDragonstorm | **D**: the ranks are never read (opaque) | land > Breaching Dragonstorm > Boarding Party > Altisaur > Maelstrom Wanderer > Creative Technique > Call Forth the Tempest > Sakashima's Protege (needs the opaque opt-in) | Every card is order-opaque, so hand order plays. Protege before BD makes "BD, then Protege as a copy of BD" unfindable. This only arises T6+ (0 two-cast mains in 150 games) |
| burn | **D**: generic only | land (before Searing Blaze; after Light Up when that is payable without it) > Swiftspear=Goblin Guide > Bolt=Skullcrack=Searing Blood=Searing Blaze=Shard Volley > Light Up the Stage (25) \| Eidolon (the turn's last cast) | Seed 9295 gi294 T3: the lethal line Swiftspear, burn spell, Light Up for {R} is `legal_not_enumerated`. It is still T4 at d8 unlimited and with `MTG_UNPRUNED=1` |
| Fluctuator | **D**: generic only | land > Unearth > Enlightened Tutor > Drannith Stinger > Fluctuator > Hollow One | None of the 30 probe games loses a line to the rank. The real losses are in how casts interleave with cycling: free Hollow Ones went uncast in 4 of 30 |
| Fungus | **D**: your partial order was never built | Forest > Wild Growth > Utopia Mycon > Doubling Season > Beastmaster > Sporecrown > Essence Warden > Sporesower > Thallid=Tukatongue=Shell-Dweller > Psychotrope \| Mycoloth (last); the Chamber after the casts, with Wild Growth on it | Smoke seed 1001 gi0 T2: Wild Growth goes on the Forest that the Chamber bounces, and is lost. The cause is a host-variant tie, not the rank |
| Giants | **D**: generic only | land > Sol Ring > Pyroclasm > Daredevil > Hamletback Goliath > Sunrise Sovereign > Titan=Harbinger=Tectonic=Flinger > Borderland Behemoth > Greaves=Bolt > Fire Diamond | With Behemoth listed before Goliath in hand, the T5 lethal (16 damage) becomes the opponent left on 8. It is not found at d8 unlimited |
| Goblins | **D**: generic only | land > Warchief > Lackey=Skirk > Mogg War Marshal > Piledriver=Stingscourger > Hordemaster > Chainwhirler=Pashalik > Chieftain=King > Krenko=Twinshot > Siege-Gang > Muxus > Vial=Bolt > Matron | Little rides on the order: only 8 of 421 mains cast 2+ spells. When Matron sits before the Lackey, her fetch ranking moves off the Lackey-cheat line (decay 0.38 vs 0.85) |
| Melira Pod | **D**: generic only | land > Celes > [15-card tie, shown order-free] > Felidar > Reveillark > Chord of Calling > Birthing Pod; all main 1, phase filter off | Chord after the creatures is load-bearing: the convoke line is a T6 kill. Enforcing today's m2 labels costs a turn (Redcap T6 -> T7) |
| Minotaur | **D**: generic only | land > Ragemonger > Sethron > Kragma=Rageblood=Neheb=Reckoner=Slaughter-Priest=Burning-Fist=Deathbellow=Scarhide > Fanatic of Mogis > Scarhide (bestow)=Vial | Seed 9005 gi5 T4: Fanatic cast before Neheb deals 5 and leaves the opponent at 1; Neheb first is lethal on T4. In 320 games, 13 turns lost 1-3 damage this way |
| SelesnyaLifegain | **D**: generic only | untapped land > Avatar [1..7] > Ageless [2..7] > Archdruid [3..5] > Llanowar=Mystic > Priest=Wellwisher > Alchemist > Bogbeast > Genesis Wave > Nykthos Paragon > Craterhoof > Feed the Clan > tapped land (Sanctuary, Blossoming Sands) | Playing Sands first spends the Paragon triggers on its 1 life: s=2080 i=78 T6 gives +8/+8 per creature instead of +18/+18 |
| slivers_vial | **D**: generic only | land > Hatchery > Muscle=Predatory=Sinew > Leeching > Galerider=Striking=Plated > Crystalline=Hibernation > Cloudshredder > Vial > Thrumming Hivepool (in a new SliversProvider) | With Muscle listed before Hatchery, your "2 lords on 4" T4 lethal and a T6 lethal are not found at d8 unlimited |
| treasure_hunt | **D**: generic casts; your land rule is ON | land after the draws (your rule) > Land's Edge > Treasure Hunt > Throes of Chaos (from hand) > Throes (retrace); needs the opaque opt-in | The ranks are never read (opaque). The d0 land rule plays no land on T4 (probe A2: T5 vs T4) |
| Anti-Lifegain | **A**: your order ON | land (declared START) > Remedy=Plague Drone > Birds=Ignoble > Aria > Idyllic=Invigorate=Fiery Justice > Swords > **Enlightened Tutor (22)** \| Skyshroud Cutter > Reverent Silence | Enlightened ties with Idyllic at rank 20. With Enlightened listed first, the game is won T5, not T4, even at unlimited budget, and condemnation is fed a decline that was never a real choice |
| Creature Giving | **A**: your order; the Crop Rotation slot is not delivered | Sylvan Scrying (before the land when payable) > land > Crop Rotation > Suture Priest=Essence Warden=Soul Warden > Birds=War-Riders > Hunted Phantasm > Defense of the Heart > Enlightened Tutor > Massacre Wurm | Crop's rank is never read: the sac-land pass casts it last. That buries ET's card and lets Wurm sweep before Crop's Spirit enters (kill T6 vs T5) |
| Dragons | **A**: your total order ON | unchanged: land > Sol Ring > Mind Stone > Fire Diamond > Tempest > Shaman > Incubator > Lathliss > Scourge > Atsushi > Glorybringer > Inferno > Utvara > Greaves > Bolt; all main 1 | No change proposed. Your rulings exist only in a code comment and two commit messages; five details are agent fill-ins you have not confirmed |
| Dragonstorm | **A**: your order ON | unchanged except within the Dragon tier: land > (Bloom) > Medallion [earliest payable..before Irencrag] > Rite > Pyretic > Desperate > Seething > Desperate (spliced) > Apex > **Lathliss > Scourge** > Karrthus=Kolaghan=Utvara > Irencrag > Dragonstorm | At d0, Scourge before Lathliss means no kill, against a T5 kill the other way. Separately, the reducer-sink exemption burns rituals (s7200001 T4) |
| FiveColour | **A**: your order; one of your rules never runs | unchanged, with the ladder wired: land > Mana Cannons > Cornucopia (=Lotus copy) > Greaves > Unite > Birds=Deathrite (with Greaves) > Bloom Tender=Faeburrow (at 5 colours) or [ideal..9] (below 5) > Spider-Man=Garth=Archangel > Hellkite > Oko=Jared=Bolas > Progenitus > dorks (no Greaves) | Your 08-19 "in front" rung has never run. A T4 probe finds no win at d3 b200 or at d8 b0; with `MTG_ORDER_RANGE=1` it wins T4 |
| KittyEquipment | **A**: your order ON | unchanged: land > Sol Ring > Puresteel > Stoneforge > Kor Duelist > Kemba > Skyhunter > Balan > Equipment cheapest first; removal never cast | No rank change. The report prints every row as "(unclassified)" with five-digit keys |
| Knights | **A**: your order ON | unchanged: land > Worthy Knight > Contender (gate met) > 8-card free block > Contender (gate not met) > Vial | No change. The Knights clauses live on the shared VialProvider, which couples them to slivers_vial, Soldiers and WhiteKnights |
| Mirrorwing Dragon | **A**: your order ON | land > Zada=Mirrorwing > Frontline Heroism > Mystic=Hierarch > Ancestral Anger > Oracle's Restoration > Fists > Gold Rush [after Fists..before Fists..before the draws] > Fortifying Draught | Anger ties with Oracle in hand order: 88 vs 82 damage. The 88 line is unreachable even at budget 0, because of `MTG_BP_NEW_ONLY` |
| Pirates | **A**: your order ON | land > Mimic > **Forerunner** (while a Buccaneer keeps a reveal) > Malcolm > Corsair > Buccaneer (reveal live) > the last reveal > Crewmate > Automaton > Dire Fleet Captain > Larcenist > Vial > Buccaneer (no reveal) and **Vial-put Buccaneer** > Tomb Raider > Stormtamer > Bolt | A Vial-put Buccaneer is ranked as if it paid a reveal. On that board your order is 1 point worse than generic |
| Snow | **A**: your order ON | land > **Slumber > Astrolabe** (swapped) > Boreal Druid > Frost Augur > Coldsteel Heart > Jorn > Treefolk > Rimescale=Rimefeather > Ice-Fang Coatl; Skred is never cast | Slumber never scries off the Astrolabe's entry. On gs=1013 T6 the Astrolabe drew a Dragon instead of a Druid. The effect is small and unmeasured at depth |
| WhiteKnights | **A**: inherits the Knights order, never reviewed for this list | land > Sol Ring > Worthy Knight > Silverblade (when an attack-ready partner with power >= 2 is out) > 9-card free block > Silverblade (otherwise) > Vial | A hand-order tie picks Silverblade's partner. The T4 swing is 2 vs 4 (a T5 vs T6 kill at life 19), and the better line is not found at d8/b0 |
| *Hydras (provisional)* | **D**: not playable (13 of 14 nonland names unimplemented) | land > Vernal Bloom > Gauntlet=Nissa > Nature's Lore=Three Visits=Search for Tomorrow > Gargos > Greaves > Garruk > Kalonian > Voracious=Primordial=Neverwinter=Steelbane | An X spell is offered only its maximum X, sized against the whole pool, so an X-Hydra can never share a plan with another cast |
| *Mill (provisional)* | **D**: not playable (0 of 11 nonland names implemented) | Hedron Crab=Ruin Crab [before land..after land] > land > Glimpse=Fractured Sanity=Archive Trap=Tasha's > Surgical > Visions (moves first once a graveyard has 20+) | In a searched plan the land drop can never follow a cast, so Crab-before-land is inexpressible. The deck is routed to AntiLifegain by accident (Scalding Tarn) |
| *Unpredictable Cyclone (provisional)* | **D**: not playable (3 of 13 nonland names implemented) | Long-Term Plans > Ponder [2..20] > Brainstorm > Brainsurge > land > Seething Song > Cyclone=Curator > Street Wraith > Ulamog CH > Ulamog IG > Emrakul | The combo cards are missing and the engine has no extra-turn support. The deck is routed to AntiLifegain by accident |

## 3. Your orders and rules that are not running

### 3a. Orders and rules you authored that do not play

1. **Hinata2: your full order (`MTG_HINATA_ORDER_FULL`, default OFF).**
   - History: reviewed 08-26 and revised 08-28 (rocks split, Hinata 11 -> 6, Reality Spasm 15 -> 7, Ornithopter -> 14,
     land at slot 0). The dork's range was removed 08-30.
   - **Why it is OFF:** it measured worse.
     - The 08-26 version: +0.0210 hold (t 4.86) and +0.0270 train (t 6.06), n=3000 per cell.
     - The 08-28 version (measured 08-30): +0.029 (t 3.8).
   - **The draft's root cause:**
     - About half of the 08-26 loss is the Irencrag "second last" pin. Moving that card back ahead of the payoffs
       recovered 0.0100 hold and 0.0157 train.
     - The find-tier-first move cost +0.0113 / +0.0120 on its own. Your 08-28 revision already undoes it.
     - Every one of these numbers predates three changes: the 09-01 waste gate, the 09-02 breakpoint node, and the
       10-10 restrictor and Crackle fixes.
     - The numbers also measured an order that never fully played: plans holding Ponder, Preordain or EI ignore the
       rank.
   - **The draft's amendment:**
     - Irencrag moves from 22 to 18, read as "right before the payoff it funds".
     - Hinata opts into `OrderOpaqueCastsByRank`, so the order binds inside cantrip plans.
     - Re-probe of seed 6600665 gi664: at rank 22 it never wins; at rank 18 it wins T5.
     - A ready-made arm exists: `MTG_HINATA_ORDER_FULL=1 MTG_HINATA_IREN_EARLY=1` plus `MTG_ORDER_OPAQUE=1`. The last
       flag is global, so use it only in Hinata-only jobs.
   - Questions: Q1-Q5.

2. **StompySurprise: your order (`MTG_STOMPY_ORDER`, default OFF since 08-21).**
   - Two related intents of yours are OFF too:
     - `MTG_TOP_RESOLVE`, your "Worldly Tutor resets Call/Turntimber" loop.
     - `MTG_UPKEEP_CALL`, the upkeep Call of the Wild activation. It measured -0.0019 (39:0) and is held back only
       because the viewer has no chooser for it.
   - **Why it is OFF:** it never measured worse in aggregate.
     - 08-25, on its own: 15,000 paired games, -37 turns (79:43, +3.26σ), and 2.2% faster.
     - 08-28 suite: -0.0064 (t -2.83) on hold and -0.0084 (t -3.21) on train.
     - It was held by gate 2: 37 of 43 regressed games never recover, so the rank acts as a hard prune.
       `MTG_UNPRUNE=searchorder` recovered 8 of 8.
     - That verdict was then voided, because it was measured without TOP_RESOLVE, and it was never re-derived.
     - Nothing has been re-measured on the value leaf or on the 09-17 list.
   - **The draft's amendment:**
     - Your "either location" cards become a searched 2-way choice, with your slot tried first.
     - The dual-position tests read the plan instead of the hand.
     - Ship it together with TOP_RESOLVE, as a Stompy-only opt-in.
     - Drop the agent-added Vaultborn-as-consumer clause (it measured 2:0 in favour of removing it).
     - Keep the agent's Turntimber 15 (it measured 29:11 in its favour).
     - Place Ghalta and Hulk, then re-measure on the shipped configuration.
   - Questions: Q6-Q10.

3. **Prevent Damage: *"do everything in the second main with this deck so creatures can attack even if we choose to
   sacrifice them"* (you, 09-29; `MTG_PD_ALL_M2`, default OFF).**
   - **Why it is OFF:**
     - It measured +0.37 t/game, because the projected turns ran greedy. That is the defect the 09-30 purge removed.
     - With both fixes it measured +0.07. The residual cause ("tutored land in m2") was made unconditional in purge
       step 2c.
     - That measurement exists only in an agent's private memory note. The ledger has no record of it, and it has not
       been re-measured.
   - **The draft's root cause:** `TapPainSourcesIfUseful` runs only at the end of main 1. Under ALL_M2, a same-turn
     Rhox, Vito or Dina therefore cannot amplify the sweep. With ALL_M2 on, the fixture `pd_rhox_keeps_citadel_for_sweep`
     ends opponent 9 / us 16, against 6 / 19 shipped.
   - **The draft's amendment:** run the sweep at the end of the last main that casts, then re-measure. Question: Q13.

4. **FiveColour: your 08-19 scaling-dork range.** The rule: *"try 2 options in this order: after all spells that add a
   new colour ... and in front. Whether it works is a matter of feasibility"*.
   - **Why it never ran:**
     - It is encoded as `CastOrderFallbackRanks` {ideal, 9}.
     - `ApplyCastOrderRangeLadder` returns at once unless `MTG_ORDER_RANGE` is set or the provider opts into
       `OrderOpaqueCastsByRank` (ManaPayment.cpp:3943). FiveColour does neither.
     - The gate (5896d20b, 08-18) is older than your rule (a1706ccd, 08-19), and the adoption measurement ran without
       the rule.
   - **Evidence:** a T4 probe finds no win at d3 b200 or at d8 b0 today, and wins T4 with `MTG_ORDER_RANGE=1`.
   - **Fix:** a one-line `OrderOpaqueCastsByRank` override, or the generic ladder-gate fix (G6). Question: Q99.
   - Two follow-ups you raised are still unbuilt and parked by default: Cornucopia trigger timing (Q102) and in-between
     placements for cumulative completers (Q103).

5. **Creature Giving: Crop Rotation *"first as it is mana neutral"*.**
   - **The problem:** its rank 5 is never read. Every `sacrifice_land` cast goes to a trailing sac-land pass, so Crop is
     cast LAST. This breaks three of your rules:
     - **Enlightened Tutor's card gets shuffled away** (seed 1001 gi20 T4).
     - **"Massacre Wurm should be last":** Wurm sweeps before Crop's Spirit exists, so the kill comes T6 instead of T5.
     - **Your T1 line "land, then Crop, then Birds" is illegal.** It was available in 4 of 24 sampled openings and
       never played.
   - **Fix:**
     - Take `tutor_land_to_battlefield` out of the sac-land pass.
     - Add an acquisition re-solve after Crop.
     - Swap ranks 5 and 6.
   - Questions: Q88-Q91.

6. **EldraziDisplacerFlicker: your Aura order is ignored on go-off turns.** The order itself (`MTG_EDF_AURA_CHEAP_FIRST`)
   is ON, but the COMBO OFF route (`DeployPieces` step 2) casts the any-colour Auras first and then follows hand
   order. Question: Q32.

7. **Fungus: *"Note that, order wise, Doubling Season should be dropped before any activations and in main 1 so that
   Beastmaster counters are doubled."* (you, 09-17).**
   - It was never built (`fungus-second-main-and-devour.md` §6e lists it as "Unbuilt"), never measured and never
     switched off.
   - The draft builds it. On the shipped list in searched play it should leave every score the same, because Mycoloth
     is alone in main 2. It does change play at d0 and on candidate B.
   - Questions: Q52-Q56.

8. **burn: your Light Up the Stage example is not implemented.** The example: *"better main 1, but only if spectacle
   is enabled ... the reason being prowess and drawing into haste"*. Its section is marked "Status: NOT implemented".
   The draft's rank-25 tier, plus a spectacle credit in the powerset, implements it. Question: Q44.

9. **Mirrorwing Dragon: Gold Rush.**
   - Your 08-18 words: *"Gold Rush is an exception ... after is better because we might get more creatures on board
     after drawing"*.
   - The adopted table puts it before Fists anyway.
   - With a magnet and Heroism out, Gold Rush then Fists deals 46 and Fists then Gold Rush deals 50. The 50 line is
     unreachable at unlimited budget while `MTG_BP_NEW_ONLY` is on.
   - Question: Q113.

10. **Anti-Lifegain: your instruction 1 (08-18), *"m1 cards should be first in the order if there is no reason to do
    it otherwise"*.**
    - It was built as the global `MTG_ORDER_M1_FIRST`, which has been OFF since 08-18. It measured neutral and changes
      7 decks' digests.
    - Leaving it off was an agent's call ("schedule deliberately"), never yours.
    - For this deck, the enforced phase split (`MTG_AL_PHASE`, 08-22) already does what you asked.
    - Your land ruling (*"it should be put at the start, since there is no draw"*) is what plays, but the deck has never
      declared it: `LandDropCastOrderRank` still returns -1.
    - Questions: Q86, Q87.

11. **Snow: the Coatl draw-early trial you asked for on 09-18.**
    - It was measured on 09-25: -0.0020 at d0, -0.0066 at d3, 0 at d5. That run used 4 seeds, on the engine from before
      Augur-first and put-in-hand.
    - The result was never put to you, and `DRAW_EARLY` stays OFF.
    - **Stale text that could repeat the 09-25 mistake:**
      - `snow-intractable-games.md` §7 still says *"`MTG_SNOW_CAST_ORDER` stays OFF"*.
      - The code comments at `DecisionProviders.cpp:4202-4234` and `:4329` say the same.
    - Question: Q119.

12. **Auras: your 08-25 Light-Paws fetch rule ("the last 1-drop Aura fetch in the plan") does not play as written.**
    The code treats a fetch as "last" only when the hand holds no Aura of ANY mana value castable this turn or next
    (`LightPawsAuraCandidates` mode 4). Question: Q20.

13. **Fluctuator: the cycling protection for lands you described is not built.** You said *"the only cards that need
    to be protected from cycling are Unearth and Drannith Stinger and occasionally an untapped land"*, but
    `SelectDigSource` treats every land as rank-3 cycling fodder. Question: Q50.

14. **KittyEquipment: your 08-25 Kor Duelist note** (*"It just shouldn't be played until you have finished drawing"*)
    is reversed by your own 10-04 hosts-first ruling. The draft finds this outcome-neutral on v1. Question: Q105.

### 3b. Calls on decisions you own that agents made without your sign-off

1. **Global `MTG_ORDER_M1_FIRST` is OFF by an agent's choice** (item 10 above).
2. **Prevent Damage, `MTG_PD_SECOND_MAIN`:**
   - An agent flipped it OFF on 09-27 (`ef26b03b`). Ledger decision D2 still reads "awaiting the user".
   - The ALL_M2 measurement exists only in an agent's private memory.
   - Question: Q13.
3. **SelesnyaLifegain, `MTG_SL_SECOND_MAIN`:** an agent switched it OFF on 09-30 (120/120 identical win turns at 2.15x
   CPU). Question: Q77.
4. **WhiteKnights, second main:**
   - A forced second main measured -0.0072 turns at d0 (42 games earlier, 0 later, out of 6000), at a doubled per-turn
     solve.
   - It was recorded as "the decision is the user's" and never answered.
   - It was measured 09-26 on the v1 list, before the value leaf.
   - Question: Q124.
5. **Soldiers:**
   - `MTG_SOLDIERS_ORDER` was put to you as question A with "Default taken: OFF", and was never answered.
   - Two places call it the "user-proposed Soldiers order": `vial-put-interleaving.md:42` and the comment in
     `test/scenarios/soldiers_vial_after_tutor_continuation.json`. You never authored or reviewed it.
   - Question: Q33.
6. **Bruna:**
   - `MTG_BRUNA_ORDER` (10-05) was left OFF with the recommendation "keep generic", and no ruling of yours is recorded.
   - It measured neutral: 4 better / 4 worse over 400 games at d5 b20, and 20 / 11 (-0.006) at d0, both on the
     pre-10-06 sideboard.
   - The draft drops the "keep generic" recommendation.
   - Questions: Q21-Q24.
7. **burn, `MTG_BLAZE_CASTPRIO`:** an agent rejected it as marginal (07-04 audit: 9 of 500 games changed line, 0
   changed win turn) and never put it to you. Question: Q47.
8. **Snow, `DRAW_EARLY`:** the result was never reported to you (item 11 above).
9. **Agent orders shipping ON that you never reviewed:**
   - **Auras, `MTG_AURA_CAST_ORDER`** (21f8433f0, 08-02; -0.0117, 24 jobs better and 0 worse).
     `auras-discard-policy-proposal.md` §9 calls the order "already reviewed", but the drafter found no quote of yours
     backing that.
   - **CritterLifegain, `MTG_CRITTER_WATCHER_ORDER`** (09-08; +7/-0 at d0 on 400 games, +11/-0 at d3 on 300).
     - It was mined on the v1 list.
     - The line "watcher order is the USER's explicit order" in `critterlifegain-discard-policy-proposal.md:126` is
       about the discard keep ladder, not this order.
   - **EDF:** ranks 5/6/7 and `MTG_EDF_PAYLOAD_FIRST` (09-15). They were derived from your played lines but never shown
     to you as an order.
   - **The generic rank-8 coloured-pip reducer tier** (816b96e1, 08-28): added after your viewer rejections and never
     reviewed as an order (Minotaur).
10. **Agent fill-ins inside your orders:**
    - **Dragons:** five details.
      - Sol Ring > Mind Stone > Fire Diamond.
      - Shaman before Incubator.
      - Dragons cheapest-first.
      - Greaves 14th.
      - Bolt last.

      Question: Q92.
    - **Pirates:** three parts you never reviewed.
      - Ranks 8-14, filled in by an agent *"for the sake of it"*.
      - The m1/m2 column.
      - The Vial-put ranks.
    - **Mirrorwing:** Oracle's Restoration at rank 14, placed after the list changed.
    - **Dragonstorm:** readings 1-9, each *"a choice the USER should confirm"*. Question: Q97.
    - **StompySurprise:** the Vaultborn-consumer clause and Turntimber 15 (item 2 above).
    - **WhiteKnights:** it runs the Knights order on a list you never reviewed it for. The planned "5e" transfer
      measurement was never recorded. Question: Q125.
11. **Dragons, where the rulings live:**
    - Your 09-04 rulings exist only in a code comment, two commit messages and private agent memory.
    - `cast-order-rankings.md` §Dragons still shows the pre-review table, and `cast-order-ideal-with-ranges.md:351`
      still lists Dragons as a review gate.
    - Question: Q95.
12. **KittyEquipment:** the agent-filled `MTG_KE_ORDER_FULL` (OFF since 10-04) is still in the code. Question: Q107.

## 4. Questions for you

Each question names the drafter's default in brackets. A one-word reply settles each one. Questions that span several
decks are in section 5 (Q136-Q141).

**Hinata2 (B)**
- **Q1** Read Irencrag's "second last" as "right before the payoff it funds", at rank 18 (ahead of Soulfire, Opus and Crackle) instead of 22? [yes]
- **Q2** Make your order binding inside Ponder/Preordain/EI plans, by having Hinata opt into `OrderOpaqueCastsByRank`? [yes]
- **Q3** Gamble before Ponder/Preordain (your slot), or after them (today's play)? [before]
- **Q4** For Soulfire, Irencrag, Crackle: is the scalar rank 18 enough, or do you want a set-level "Irencrag right before the plan's last payoff"? [scalar now]
- **Q5** Ponder and Preordain stay peers until the deferred interleave is built? [yes]

**StompySurprise (B)**
- **Q6** Re-measure your order as amended here (with TOP_RESOLVE, on the value leaf and today's list), then make it the default ON if it clears the bar? [yes]
- **Q7** Worldly Tutor goes in the mid slot only when a Turntimber will consume its card; a Call activation takes the late tutor; Vaultborn is not counted as a consumer? [yes]
- **Q8** Your "either location" cards (Worldly mid/late, Turntimber before/after the tutor, Natural Order early/late, Hulk early/late) become a searched 2-way choice, with your slot tried first? [yes]
- **Q9** World War Hulk gets two slots: just before Craterhoof when a Craterhoof or Ghalta is in hand, otherwise before the fatties? [yes]
- **Q10** New cards: Ghalta first among the fatties (after Vaultborn), Apex with the fatties, Fyndhorn with the 1-drops? [yes]

**Prevent Damage (A+B)**
- **Q11** GSZ at 11, strictly after Tamanoa, and ranked by its target (a GSZ for Dina joins the drains; X=0 goes to 20)? [yes]
- **Q12** For Vito/Dina, search both orders, or use the static rule "Dina first unless Rhox or Bilbo is out"? [search]
- **Q13** ALL_M2: build the end-of-main-2 pain sweep, re-measure (held-out, PD only), and ship it ON if the net is <= 0? This also settles D2. [yes]
- **Q14** Manabarbs: measure a sequential (non-batch) payment for plans that cast it while a gain engine and a drain are out? [measure only]
- **Q15** After adopting Q11-Q13, keep the value leaf and keep table, regenerating them only if play moves a lot? [keep]

**Auras (C)**
- **Q16** One unconditional Aura order on every turn, not only while Light-Paws is already in play? [yes]
- **Q17** Within plain Auras, and within scalers, cast the highest mana value first? [yes]
- **Q18** Within one mana value, weakest first? [yes]
- **Q19** Search the Aura order on Light-Paws turns now (at most 3! orders)? [no: adopt the rank first, then A/B the search]
- **Q20** Fix the Light-Paws fetch ranking to match your 08-25 rule, and make Mask beat a duplicate Armor, as its own measured change? [yes]

**Bruna (C)**
- **Q21** Move Lightning Greaves from LAST to before every creature? [yes]
- **Q22** Cast Colossification only in main 2? [yes]
- **Q23** Opt Bruna into the main-phase filter (`ClassifiesMainPhases`)? [yes, after the A/B]
- **Q24** Tutors right after the mana producers, knowing it changes nothing in the engine today? [yes]

**CritterLifegain (C)**
- **Q25** Archangel of Thune right after the watchers, ahead of Pridemate/Voice and the 1-drops? [yes]
- **Q26** Place Heliod by devotion (right after the casts that make it a creature; first if it cannot become one this turn), or only at its static slot? [devotion]
- **Q27** The flip: payoffs ahead of new watchers when the board holds at least 2 more watchers than payoffs and no Thune? [yes]
- **Q28** Ajani after every creature, even on a -2 turn? [yes]

**EldraziDisplacerFlicker (C)**
- **Q29** Cloud of Faeries before Peregrine Drake (cheapest untapper first, as you played it every time)? [yes]
- **Q30** Give Fertile Ground, Trace and Overgrowth a range (cast first, moved behind the untappers only when the turn is otherwise unpayable)? This needs an engine change. [yes]
- **Q31** Restore the same-land Overgrowth-then-Trace stack (Trace goes last only when it shares a land)? [yes]
- **Q32** Should the COMBO OFF route deploy Auras in your order instead of any-colour first? [yes, behind a flag, measured on the 24 references]

**Soldiers (C)**
- **Q33** Adopt this order with default ON after one held-out paired A/B on the current tree? [yes]
- **Q34** Thalia's Lieutenant at 4, after the tutors (the amendment), or LAST (the 10-04 slot)? [4]
- **Q35** Treat slots 6-17 as one free block, keeping their order only as a canonical tie-break? [yes]

**Angels (D)**
- **Q36** Adopt the order: land, Sol Ring, Greaves, Bishop, Righteous, Thune, Lyra AoD, Giada, Youthful, Dawnbringer, bodies, Serra, removal? [yes]
- **Q37** Righteous Valkyrie before Giada? [Righteous first]
- **Q38** Build a second-main lever so vigilant Giada can attack and then pay for an Angel, and A/B it (it ships only on your call)? [build and measure]
- **Q39** Lightning Greaves before every creature? [yes]
- **Q40** Move the engines (Thune, Lyra AoD) behind Giada and Youthful when no life source is on board or among the turn's casts? [yes]

**BreachingDragonstorm (D)**
- **Q41** Sakashima's Protege after every other hand cast? [yes]
- **Q42** Order the rest by "entrant" (BD, Boarding Party, Altisaur, Wanderer, CT, CFT) or by "enabler" (BD, CT, Boarding Party, Altisaur, Wanderer, CFT)? [entrant]
- **Q43** Call Forth the Tempest second-to-last? [yes]

**burn (D)**
- **Q44** Light Up the Stage at 25 (after the creatures and burn, before Eidolon), in either main, with no range, plus the enumeration change that makes the Swiftspear line castable? [yes]
- **Q45** Cast Shard Volley with the other burn (rank 20) instead of after everything? [yes]
- **Q46** Make Eidolon the turn's last cast (in main 2 whenever main 2 casts), as a tie-break only? [yes]
- **Q47** Play the land before Searing Blaze whenever a land is in hand and Blaze is cast this turn (re-enabling the rejected `MTG_BLAZE_CASTPRIO`)? [yes]
- **Q48** Keep both of burn's mains fully searched (no m1/m2 filter)? [yes]

**Fluctuator (D)**
- **Q49** Order: land, Unearth, Enlightened Tutor, Drannith Stinger, Fluctuator, Hollow One (the alternative is Fluctuator first)? [as listed]
- **Q50** Build "cycle first, then make the land drop": a re-solve while the drop is open, plus your untapped-land protection in `SelectDigSource`? [yes, census first]
- **Q51** Investigate the free Hollow Ones left uncast (4 of 30 probes; the same at every budget and depth tried)? [yes]

**Fungus (D)**
- **Q52** Wild Growth vs the Chamber: offer the Chamber first as Wild Growth's host ("order"), or prune the dominated pairing ("prune")? [order]
- **Q53** Utopia Mycon ahead of Doubling Season? [yes]
- **Q54** Mycoloth LAST in whatever main it shares? [yes]
- **Q55** The same order for candidate B? [yes]
- **Q56** Keep your 09-23 main split (Mycoloth in main 2, everything else in main 1)? [yes]

**Giants (D)**
- **Q57** Creature order Goliath, Sovereign, {Titan, Harbinger, Tectonic, Flinger}, Behemoth, all after the Daredevils? [yes]
- **Q58** Daredevil before Goliath even when both are affordable at full price (gives up 1 counter, saves 2 mana)? [yes]
- **Q59** Pyroclasm before your own creatures? [yes]
- **Q60** Opt Giants into the m1/m2 filter? [no]

**Goblins (D)**
- **Q61** Matron last or first? [last]
- **Q62** The rest of the creatures by cost, or leave them in hand order? [by cost]
- **Q63** Is "a Piledriver should be played earlier than a lord" about turns? (Within one phase, Piledriver is ranked ahead of every lord, which is neutral.) [yes]
- **Q64** Keep the phase filter OFF for Goblins? [off]

**Melira Pod (D)**
- **Q65** Adopt generic plus the four pinned tie-breaks: Celes first; Felidar, then Reveillark, last among the creatures; Chord before Pod? [yes]
- **Q66** Label every cast m1 and keep the m1/m2 filter off? (Enforcing today's labels costs a turn.) [yes]
- **Q67** Queue an engine fix so a persist body that entered this main can loop before combat? [yes]
- **Q68** Chord before Pod, or Pod before Chord? [Chord first]

**Minotaur (D)**
- **Q69** Sethron right after Ragemonger and before every other Minotaur, cast or Vialed? [yes]
- **Q70** Fanatic of Mogis last among the creatures? [yes]
- **Q71** Ragemonger always before Sethron, even with one mana to spare? [always]
- **Q72** "Crack the fetch after a fresh, hasty Slaughter-Priest": park it as a design note? [park; 0 of 19 cases]

**SelesnyaLifegain (D)**
- **Q73** Avatar (and Ageless) ahead of the Elves whenever the turn pays without the growth, with your 08-20 Elves-first order as the fallback? [yes]
- **Q74** Play Blossoming Sands after the casts and activations? [yes]
- **Q75** Build the same-turn Alchemist credit, then rank Feed the Clan FIRST while an Alchemist can tap? [yes, as an engine item; Feed stays last until then]
- **Q76** Fix Nykthos Paragon's greedy "you may" spend as its own heuristic sweep? [yes]
- **Q77** Keep the deck single-main (an agent turned `MTG_SL_SECOND_MAIN` off on 09-30)? [yes]

**slivers_vial (D)**
- **Q78** Hatchery Sliver first, before every other Sliver cast or Vial put? [yes]
- **Q79** After Hatchery: anthems, Leeching, 1-drops, 2/2s, Cloudshredder? This decides which Sliver takes the leftover replicate copies. [approve]
- **Q80** Ship as a new `SliversProvider` (derived from VialProvider), not on the shared VialProvider? [yes]
- **Q81** Fix the engine so the replicate count after a same-main Hatchery is searched instead of taken at the greedy maximum? [yes, a separate engine task]

**treasure_hunt (D)**
- **Q82** In a main that casts both, Land's Edge before Treasure Hunt (and before Throes)? [yes]
- **Q83** Treasure Hunt before a hand-cast Throes, accepting that clairvoyant Throes-first wins (probe B1) become unreachable? [yes]
- **Q84** Move the d0 "TH before the land drop" rule out of AIEngine into TreasureHuntProvider, and replay the held drop when TH is not cast? [yes, as its own measured change]

**Anti-Lifegain (A)**
- **Q85** Enlightened Tutor after Idyllic Tutor (rank 22), applying your Creature Giving rule here? [yes]
- **Q86** Declare the land slot as START (`LandDropCastOrderRank` 0)? [yes]
- **Q87** Count instruction 1 as delivered for this deck by the enforced split, and leave the global `MTG_ORDER_M1_FIRST` to the generic review? [yes]

**Creature Giving (A)**
- **Q88** Deliver Crop Rotation at its agreed slot (after the land, before the watchers) instead of last? [yes]
- **Q89** When the line is payable either way, should Crop wait until after Suture Priest and the Wardens, going first only when its mana is needed? [yes, as a second step]
- **Q90** Sylvan Scrying before Crop Rotation when both are cast? [yes]
- **Q91** A Forbidden Orchard land drop after the watchers (and before Wurm) when they are payable without it? [record as an open item; don't build]

**Dragons (A)**
- **Q92** Confirm the five agent fill-ins in your total order? [yes]
- **Q93** Fire Diamond: keep it with the rocks, or move it to the very end? [keep]
- **Q94** Confirm Dragons stays single-main, with every cast in main 1? [yes]
- **Q95** Write your 09-04 rulings and this table into `cast-order-rankings.md` §Dragons and clear the review gate? [yes]

**Dragonstorm (A)**
- **Q96** Hard-cast Dragons among themselves: Lathliss, then Scourge, then the rest? [yes]
- **Q97** Confirm the readings of your order as built? [confirm] They are:
  - The Medallion goes at the earliest slot where the whole line pays, and never after Irencrag.
  - With several Medallions, each goes as early as possible.
  - A plain Desperate moves behind the spliced one only when legality needs it.
  - With no Dragonstorm, Irencrag goes right before the last hard-cast Dragon.
- **Q98** Ruby Medallion justifies only the rituals that actually pay for it (the reducer-sink fix), behind its own lever and measured? [yes]

**FiveColour (A)**
- **Q99** Wire your 08-19 scaling-dork rule as written (ideal slot, else in front), default ON, with a held-out A/B reported back? [yes]
- **Q100** Should "in front" be rank 9 (first of the dorks, still after Cannons, Cornucopia, Greaves and Unite), or ahead of Cannons? [9]
- **Q101** Engine fix so a Greaves-hasted Deathrite's mana counts in the same plan? [yes, measured separately]
- **Q102** Build your Cornucopia trigger-timing follow-up now? [no, keep it parked]
- **Q103** Build your in-between placements for cumulative completers now? [no, keep it parked]

**KittyEquipment (A)**
- **Q104** Keep v1's order exactly as shipped? [yes]
- **Q105** On v1 (no Sigarda's Aid), does your 10-04 "hosts before Equipment" supersede your 08-25 Kor Duelist note? [yes]
- **Q106** Pin on-cast and on-ETB draw watchers (Sram, Puresteel) to main 1 in the provider? [yes]
- **Q107** Delete the unused `MTG_KE_ORDER_FULL`? [yes]
- **Q108** Widen the name-hash tie-break to 16 bits? [yes]

**Knights (A)**
- **Q109** Keep the 08-19 order exactly as adopted? [yes]
- **Q110** Read the Contender's "don't yet control a Knight" at the moment of the cast? This needs `ArrangeCastOrder`. [no]
- **Q111** Move the Knights clauses off the shared VialProvider onto KnightsProvider (byte-identical)? [yes]

**Mirrorwing Dragon (A)**
- **Q112** Ancestral Anger before Oracle's Restoration? [yes]
- **Q113** Gold Rush's preferred slot AFTER Fists (your 08-18 wording), with the ladder walking it earlier only while the line cannot pay? [yes, behind a lever, A/B before it ships]
- **Q114** Under magnet plus Heroism, compute the order of the sign-flipping pairs from exact payload arithmetic? [yes, as a follow-up under its own lever]

**Pirates (A)**
- **Q115** Forerunner second (after Mimic, ahead of Corsair, Buccaneer, and a Malcolm who cannot get his Clue), but only while a Buccaneer keeps a reveal without it? [yes, behind a default-OFF lever, measured]
- **Q116** On the turn where it is Clue vs drain (Malcolm cast as the turn's first spell, Forerunner second), which goes first? [Malcolm]
- **Q117** Rank a Vial-put Buccaneer with the reveal-free bodies (slot 12)? [yes]

**Snow (A)**
- **Q118** Slumber at 1 and Astrolabe at 2? [yes; until you answer, your current order stands]
- **Q119** Coatl: keep draw-last, or switch to draw-early (both re-measured on today's engine)? [keep draw-last]
- **Q120** Build a "land after a same-turn Slumber" slot? [no; record it as a known limit]

**WhiteKnights (A)**
- **Q121** Give Silverblade a conditional slot: right after Worthy Knight when an attack-ready, unpaired creature with power >= 2 is out, otherwise after the tribe? [yes]
- **Q122** Add a searched twin, so both "pair now" and "pair with the strongest new creature" stay findable? [yes, A/B before adoption]
- **Q123** In `SoulbondPartner`, prefer the creature that can attack this turn when powers are equal? [yes]
- **Q124** Keep the deck single-main until a second main is re-measured on this list and the line it buys is named? [yes]
- **Q125** Does the Knights order carry over to this list unchanged, apart from Silverblade? [yes]

**Hydras (provisional)**
- **Q126** Mana doublers first (Vernal Bloom, then Gauntlet and Nissa)? [yes]
- **Q127** The fetch spells after the doublers and before every creature? [yes]
- **Q128** Gargos before every Hydra, on the existing `reduces_spell_subtype` param? [yes]
- **Q129** Offer the X-Hydras every X, not just the maximum? [yes]

**Mill (provisional)**
- **Q130** The Crabs before the land drop, moving after it only when they need its mana? [yes]
- **Q131** Visions of Beyond after the turn's mill, but first once a graveyard already holds 20 or more? [yes]
- **Q132** Surgical Extraction after the mill, targeting the milled name with the most copies left in the library? [yes]

**Unpredictable Cyclone (provisional)**
- **Q133** Long-Term Plans before every look? [yes]
- **Q134** Ponder, then Brainstorm, then Brainsurge? [yes]
- **Q135** On a turn that cycles into Cyclone, crack the fetch before the top-setters? [before]

## 5. Cross-deck findings for the generic order

### Findings, deduplicated

**G1. Ties fall to hand order, which nothing searches.**
- `CastOrderLessRanked` (ManaPayment.cpp:2645) breaks ties cheapest-first only between `IsManaRitual` casts. Every
  other tie keeps plan order, which is hand order.
- A tie of this kind lost a line, at unlimited budget or at d8/b0, in Anti-Lifegain, CritterLifegain, EDF, Giants,
  Minotaur, Mirrorwing, Soldiers, slivers_vial and WhiteKnights. It lost a turn at d3 in Prevent Damage (P2), value in
  Angels, Bruna, SelesnyaLifegain and StompySurprise (Craterhoof), and turns in Dragons' generic arm (probes R1, L1).
- **Fix:** allow a tie only where order provably cannot matter (Mirrorwing, Prevent Damage); otherwise give it a rank
  or search it.
  - Extend cheapest-first to the casts that make mana: rocks (Dragons G3), `etb_untap_lands` (EDF), doublers
    (Hydras G4) and dorks (Bruna G2).
  - Do not extend it to every creature. EDF: *"the Scourge ETB-order lesson stands"*.
  - Fluctuator adds a zero-generic, more-pips-first key for per-cast payment. It must be measured, because
    `CastOrderLess` keeps cost out of creature ties on purpose.

**G2. There is no generic watcher tier.**
- `MTG_WATCHER_ORDER` (tier 9, default OFF) covers only `dragon_ping_on_enter` and `haste_on_flying_enter`. Every other
  watcher ties with the creatures it watches (rank 10) or comes after them (noncreatures, rank 20).
- **Decks:** Angels, CritterLifegain, SelesnyaLifegain, Giants, Dragons, Soldiers, Pirates, Minotaur, StompySurprise,
  Fungus, Snow, Knights, FiveColour and KittyEquipment.
  - In Snow the watcher is a noncreature (Slumber); s4004 gi28 went 7 -> 6 at d8b0 once it was fixed.
  - Knights' Worthy Knight and FiveColour's Mana Cannons watch casts rather than entries.
  - Creature Giving already ships its own [8] WATCHER tier.
- **Params the drafts name:**
  - enter-watchers: `own_creature_enters_lifegain`, `own_creature_enters_lifegain_toughness`, `any_creature_enters_lifegain`, `own_creature_enters_self_counters`, `other_subtype_enters_counters_subtype`, `any_creature_enters_self_counters_power`, `other_chosen_subtype_enters_counters`, `own_creature_enters_opp_life_loss`, `etb_other_subtype_creates_tokens`, `own_creature_enters_draw`, `snow_enter_scry`;
  - cast-watchers: `cast_trigger_creates_tokens` with a subtype, `multicolor_cast_damage_per_color`;
  - payoffs: the team payoff `lifegain_each_own_creature_counters`, then the unit payoff `lifegain_self_counters`.
- **Order within the tier:** the token-maker before the ping watcher (Dragons G2). Dragonstorm's `ClassifyEtbOrder` /
  `OrderEntriesByEtbValue` is the shared primitive for this.

**G3. Haste Equipment at rank 20 strands the mana the haste unlock would produce.**
- **Decks:** Angels (Greaves stays in hand), Bruna (P3) and FiveColour. FiveColour's own order (Greaves 6, dorks 8) is
  the template.
- **Fix:** rank `is_equipment && equip_grants_haste` before the creatures. This is free, because the equip runs after
  the casts. Add a dork sub-tier just ahead of 10. The alternative is to teach `CreditCastOutput` about
  `HasteUnlockedManaOf`.
- **Tension:** Dragons (Greaves 13th) and Giants put Greaves late, for payment reasons. Bruna asks for the fix to be
  measured on the Greaves decks.

**G4. The rock tier is too broad.**
- Enters-tapped rocks (Fire Diamond, Coldsteel Heart) and a pure filter (Astrolabe) fund nothing this turn (Dragons,
  Giants, Snow).
- Rocks tie at 5 with no cheapest-first. In Dragons' R1 probe, Mind Stone before Sol Ring on one land loses a turn.
- A land Aura ties with a rock at 5; the land Aura should go first (Bruna G1).
- **Fix:** tier 5 becomes `mana_rock && !enters_tapped && !filter_no_free_colorless`, cheapest first. Enters-tapped
  rocks move to the plain noncreature tier or to the end.

**G5. Order-opaque sets bypass the rank.**
- `OrderingOpaque` (ManaPayment.cpp:2722) skips the sort unless the provider opts in, so hand order plays.
- **Decks:** in BreachingDragonstorm (every card) and treasure_hunt the rank is never read. In Hinata2
  (Ponder/Preordain/EI), burn (Light Up), Cyclone and Mill (both provisional) it is bypassed for those cards.
- The global `MTG_ORDER_OPAQUE` (arm B) measured -0.00236/game held-out and was parked. Four providers opted in instead.
- **Fix:** opt in per deck, and repeat the global measurement (treasure_hunt).

**G6. The range ladder has a gate trap, and its payment projection has gaps.**
- `CastOrderFallbackRanks` is silently inert unless the provider also overrides `OrderOpaqueCastsByRank()` or
  `MTG_ORDER_RANGE` is set. This gate is what killed FiveColour's rule of yours.
- **Fix:** enter the ladder whenever a cast carries a funding ladder. This is byte-identical for Mirrorwing.
- **Projection gaps:** the ladder and `MTG_PAYABLE_ORDER` do not credit:
  - an untapper's refund or a land Aura's bonus (EDF);
  - dork growth (SelesnyaLifegain);
  - a hasted dork's mana (Angels);
  - a fetched land (Creature Giving).

**G7. The cast restrictor (rank 18) sits ahead of all of tier 20.**
- In Hinata2 a cantrip takes Irencrag's one follower. In Dragonstorm's `=0` arm the order realises Irencrag before Apex,
  which you called a trap.
- **Fix:** split tier 20. Set-up casts (draw, tutor, dig, cheap spells) go before the restrictor and payoffs after it.
  The restrictor goes right before the turn's last cast, and always after an impulse-exile or staging enabler.

**G8. "Creature first" is a prowess rule applied to every deck.**
- **Breaching G2:** gate it on the deck holding a prowess-style reader. For `enter_as_copy_of_entrant` it is backwards.
- **Dragonstorm:** a creature at 10 before a ritual at 15 is wrong when that ritual pays for it (s7600233 T3).
- **Fluctuator:** it puts a self-discounting Hollow One first.
- **Counterweight (Melira):** creature-before-noncreature is load-bearing for convoke, so convoke spells must stay after
  the turn's creatures.

**G9. Cards that count the board or the turn have no late slot.**
- **Last body producer:** the team pump `etb_team_pump_per_creature` (StompySurprise, SelesnyaLifegain).
- **After everything they count:**
  - a board-counting ETB (`etb_damage_devotion_color`, Minotaur);
  - devour (Fungus);
  - copy-an-entrant (Breaching);
  - a spell that counts this turn's casts (Breaching's CFT; Dragonstorm last).
- **Late:** a self-discounting creature (Fluctuator, about 25).
- **Last among payoffs:** an {X} sink (Hinata2 `x_damage_multiplier > 1`; Hydras G3).

**G10. Reducers and enablers miss their tiers.**
- A choose-a-type reducer (Urza's Incubator) misses tier 8 and falls to 20. Use `IsSubtypeCostReducer` (Dragons G4).
- The colour reducer (16) sits after the creatures and the rituals. Earliest-payable placement is the answer
  (Dragonstorm, Minotaur).
- A replicate granter (`grants_replicate_to_subtypes`) belongs at 8 (slivers_vial).
- Token/counter doublers sit at 20, after the creatures (Fungus).
- Non-rock mana doublers and land-to-battlefield spells sit at 20 (Hydras G1/G2). Creature Giving already ranks Crop at 5.

**G11. Library writers and readers are all lumped at rank 20.** The orders the drafts want:
- shuffler, then `tutor_to_top`, then the top-consumers (StompySurprise);
- a to-top tutor after the same-turn searches (Anti-Lifegain; Creature Giving hand-codes 22);
- a self-shuffling tutor before the top-setters, and a look-with-shuffle before the put-backs (Cyclone G1/G2);
- a threshold draw after the graveyard fillers, and a graveyard-target spell after the mill (Mill G2/G3);
- a draw-yield outlet (`discard_land_damage`) before the draws, and a cascade after the cheap draws (treasure_hunt).

**G12. Symmetric effects sit on the wrong side.**
- A symmetric sweeper at 20 lands after your own creatures; it should go before them (Giants).
- `noncreature_spell_tax` ranks ahead of the noncreature spells it taxes; it should go after them (Soldiers G2).
- `on_cast_trigger_damage` at 30 is backwards when `SelfDamageUseful` holds (Prevent Damage's Spellshock).
- Tier 30 is last only within one main (burn).

**G13. The sac-land pass overrides every rank.** Every `sacrifice_land` cast trails all ranked casts, in both worlds.
- Crop Rotation is cast last (Creature Giving).
- Shard Volley trails Eidolon's tier 30 (burn).
- **Fix:** join the ordered sequence at the card's own rank. Crop keeps its own path.

**G14. Board-conditional ranks read the wrong board.**
- The rank sees the board at the start of the phase, never the plan's own earlier casts (Auras D1, the Knights
  Contender, CritterLifegain's Heliod).
- SelesnyaLifegain's growth tier reads the board after the whole-turn prepay, and is keyed on `tapped`.
- **Fix:** conditional positions go in `ArrangeCastOrder`, which sees the cast set. Rank conditions must not read `tapped`.

**G15. The land drop has only two positions:** first, or after a breakpoint. A Karoo is held until after the casts.
- `LandDropCastOrderRank` is not an execution slot (Snow, burn, Anti-Lifegain).
- **Lines this makes inexpressible:**
  - Crab before the land (Mill G1);
  - Slumber, then the land (Snow);
  - an Orchard after the watchers (Creature Giving);
  - a tapped gain-land after the casts (SelesnyaLifegain);
  - cracking a fetch after a Slaughter-Priest (Minotaur);
  - cycling, then the drop (Fluctuator);
  - a conditional fetch (Cyclone).
- **Fix:** make the land position a searched plan choice, or generalise `karoo_deferred`. Burn's land-before-Blaze
  needs a plan-level tie-break.

**G16. `MTG_BP_NEW_ONLY` turns order into a hard prune.**
- The dropped "cast X alone, then the old cards" continuation is only redundant if the sibling's rank order matches it.
- Mirrorwing's 88-damage and 50-damage lines are unfindable at budget 0 with the lever on (Q140).
- The same class shows in Breaching G6, treasure_hunt, Goblins (Matron), and StompySurprise note 8.

**G17. Vial puts take cast ranks, including reasons that apply only to casts.**
- Examples: the reveal-or-pay cost (Pirates) and Thalia's tax on Vial (Soldiers).
- **Fix:** a default `CastOrderActionRank` for `ActivateVial` that drops the cost-only reasons. It would affect the
  Vial decks (Knights, Soldiers, slivers_vial, Goblins).

**G18. Spectacle is credited only in burn's 2-card Plan B.** `spectacle_cost` should rank 25, with a powerset credit
(burn).

**G19. The phase classifier gets several cases wrong.** These are inert on single-main decks, but they bite any deck
that opts into the filter.
- **A summoning-sick body sent to main 2 although a Pod or sac outlet in play consumes it** (Melira). It costs a turn,
  and the comment calling `MTG_PHASE_CLASSIFY` "safe to set SUITE-WIDE" is false.
- **Missed scaling attackers:** `BoardHasScalingAttacker` misses Behemoth and Goliath (Giants) and Muxus (Goblins).
- **Giants' other two misses:** a subtype-reducer creature goes to main 2, and the main-1 pull fires for a
  summoning-sick firebreathing caster.
- **Watchers sent to main 2:** cast-trigger watchers and gated diggers (Knights; Kitty's Sram/Adelbert; WhiteKnights'
  Silverblade/Worthy Knight).
- **A cascade creature without haste** goes to main 2 (Breaching); extend the Muxus ETB rule to `cascade_max_mv > 0`.
- **Spectacle** goes to main 2; it should be Both (burn).
- **`aura_etb_tap_host`** is classed main 1; casts should be main 2 (Bruna).
- **Aura-cast watchers** (Auras) and a vanilla dork in a copy-magnet deck (Mirrorwing) are classed wrongly.
- **No second-main clause for a vigilant mana creature** (Angels).

**G20. Gaps in what counts as a draw.**
- Not counted as draws:
  - watcher cantrips (Kor, Sram, Puresteel);
  - `etb_self_draw` permanents (Auras, KittyEquipment, Snow);
  - activated draws such as cycling (Fluctuator).
- Counted as draws although they put nothing in hand: cascade, exile-until and reveal-freecast (`IsIdealOrderDraw`,
  Breaching G5).

**G21. The `--cast-order-report` misleads the reviewer.**
- **Rank 8/9 labels missing:** ranks 8 and 9 have no labels (Giants, Goblins, Minotaur, CritterLifegain, slivers_vial).
- **Provider ranks mislabelled:** they print as "(unclassified)" or under a borrowed generic label.
  - EDF's untappers read "MANA ROCK".
  - Prevent Damage's Spellshock reads "RITUAL".
  - Snow's Slumber and Pirates' Malcolm read "DRAW".
  - Kitty prints raw five-digit keys.
  - Hinata's tier names were never renumbered.
  - Mirrorwing has no case 6.
- **Main column:** it shows m2 on single-main decks (about 20 decks), and on decks whose phase filter is off (Bruna,
  Giants, Goblins, Melira, burn).
- **Ties:** they are not flagged, and they are listed by mana value and name while play keeps hand order.
- **The land row ignores:**
  - `LandDropCastOrderRank` (Anti-Lifegain);
  - Karoo deferral (Fungus);
  - `LandDropAfterHandLandTutor` (Creature Giving);
  - activated and watcher cantrips (Fluctuator, Auras, KittyEquipment, Snow).
- **One board only:** conditional ranks never appear (FiveColour, Pirates, Auras).
- **Ranks play never reads:** sac-land casts (Creature Giving, burn) and opaque cards (Breaching, treasure_hunt, burn).
- **Missing or misplaced entries:**
  - Auto-fired alt casts are not shown (Anti-Lifegain).
  - A suspend-only card gets a cast rank (Dragonstorm).
  - Removal that is never cast is listed as a live slot (WhiteKnights).
  - The combo route's order is absent (EDF).
- **Silent ties from authoring:** an authored rank that collides with a fall-through number creates a silent tie
  (StompySurprise: Hulk with Craterhoof).

### Proposed revised generic tier list

This is the order the findings above add up to. The positions are an order, not rank numbers.

| order | tier | key | today | drafts |
|---|---|---|---|---|
| L | land drop: first; after a draw or reveal when the search chooses; a Karoo, and (proposed) an enters-tapped land with no information ETB, after the casts | - | first; Karoo after | SelesnyaLifegain, Snow |
| 1 | land Aura on an untapped host | `is_land_aura && land_aura_extra_mana > 0` | 5, tied with rocks | Bruna |
| 2 | untapped rocks that add net mana, cheapest first | `mana_rock && !enters_tapped && !filter_no_free_colorless` | 5 | Dragons, Snow, Giants |
| 3 | same-turn land and mana makers: land-to-battlefield spells; non-rock doublers, once the enumerator credits them | `tutor_land_to_battlefield`; a new doubler param | 20 | Hydras, Creature Giving |
| 4 | rituals, cheapest first, before the creatures they pay for. A creature that reads noncreature casts (prowess) still goes ahead of them | `IsManaRitual` | 15 | Dragonstorm, Breaching |
| 5 | reducers and enablers before what they enable: subtype and choose-a-type reducers, replicate granters, token/counter doublers; the colour reducer at its earliest payable slot | `reduces_spell_subtype`, `IsSubtypeCostReducer`, `grants_replicate_to_subtypes`, `doubles_tokens` / `doubles_counters`, `reduces_spell_color` | 8 / 20 / 16 | Dragons, slivers_vial, Fungus, Dragonstorm, Minotaur |
| 6 | haste Equipment, where the deck has mana creatures it can unlock (measure on the Greaves decks) | `is_equipment && equip_grants_haste` | 20 | Angels, Bruna, FiveColour |
| 7 | symmetric creature sweeper | `damage_all_creatures` | 20 | Giants |
| 8 | watchers before what they watch: token-making watchers, then other enter/cast watchers, then team payoffs, then unit payoffs | the G2 params | 10 / 20 | see G2 |
| 9 | mana dorks, cheapest first; dork growth as the cost-efficient end of a range, never keyed on `tapped` | creature with `produces`; `MTG_DORK_GROWTH` | 10 / 9 | Bruna, SelesnyaLifegain |
| 10 | other creatures | `IsCreature` | 10 | - |
| 11 | late creatures: self-discounting, devour, board-counting ETB, copy-an-entrant, then the team pump last | `cost_less_per_cycle_or_discard`, `devour`, `etb_damage_devotion_color`, `enter_as_copy_of_entrant`, `etb_team_pump_per_creature` | 10 | Fluctuator, Fungus, Minotaur, Breaching, StompySurprise, SelesnyaLifegain |
| 12 | set-up noncreatures in library order: self-shuffling tutor, looks with an optional shuffle, searches and shufflers, to-top tutor, top-consumers; a draw-yield outlet before the draws; a cascade after the cheap draws | see G11 | 20 | Anti-Lifegain, Creature Giving, Cyclone, StompySurprise, treasure_hunt |
| 13 | sacrifice-a-land casts, at their own rank instead of a trailing pass | `sacrifice_land` | trailing | burn |
| 14 | cast restrictor, right before the payoffs | `max_casts_after` | 18 | Hinata2, Dragonstorm |
| 15 | payoffs: those that count the turn after what they count; graveyard readers after the fillers; spectacle after its enablers; {X} sinks last | `damage_opp_creatures_mv_cast`, `spectacle_cost`, `x_damage_multiplier > 1` | 20 | Breaching, burn, Hinata2, Hydras, Mill |
| 16 | symmetric cost increasers, after the spells they tax | `noncreature_spell_tax` | 10 | Soldiers |
| 17 | enters-tapped rocks and pure filters | `mana_rock && enters_tapped`; `filter_no_free_colorless` | 5 | Giants, Snow |
| 18 | on-cast self-damage, last for the whole turn. A damage source that is the payoff (`SelfDamageUseful`) goes before the spells it triggers on | `on_cast_trigger_damage` | 30 | Prevent Damage, burn |

Rules that apply across the whole list:

- **Ties:** a tie is allowed only where it provably cannot matter (G1).
- **Conditional slots:** these go in `ArrangeCastOrder` (G14).
- **Convoke:** convoke spells stay after the turn's creatures (G8).
- **Vial puts:** they drop cost-only reasons (G17).
- **Tutors:** no tutor slot is proposed, because the drafts disagree (see below).
- **Draws:** the draw promotion (principle 1) is left as it is. It is off today, Hinata2 measured it as a loss
  (+15/+13 game-turns), and Mill G2 and Breaching G5 show gaps in it.

### Where the drafts disagree or hedge

- **Tutor slot.**
  - Goblins puts a deferred-acquisition tutor LAST at depth > 0, so the fetch ranking sees the board the re-solve gets.
    The caveats are d0 and condemnation.
  - Soldiers G3 puts ETB creature tutors FIRST among the creatures, so a fetched watcher or lord joins at its own rank.
  - Bruna G5 says a `tutor_to_hand` rank has no information effect, because the re-solve is deferred until after the
    plan.
  - Prevent Damage wants a put-tutor ranked as its target.
- **Shard Volley's trailing pass.**
  - Creature Giving's draft calls the pass "right for Shard Volley (tap the land, then sacrifice it)".
  - burn measured the pass costing 2 extra Eidolon damage, in 14 of 600 games. It notes that Volley's sacrifice is paid
    from a tapped land in any position.
- **Greaves slot.** The early-Greaves drafts (Angels, Bruna, FiveColour) and the late-Greaves orders (Dragons, Giants)
  conflict. A generic rule must be keyed so the late-Greaves decks are not disturbed.
- **Fire Diamond.** Giants and Snow want enters-tapped rocks out of the rock tier. Dragons keeps Fire Diamond with its
  rocks under your ruling (Q93). Dragons has its own order, so the generic change would not touch it.
- **Creature-first.** Breaching, Dragonstorm and Fluctuator argue against it; Melira says it is load-bearing for convoke
  (G8).
- **Bruna's Greaves.** The draft reverses the earlier agent proposal ("Greaves last") to "before every creature". Its
  P3 result is predicted, not measured.
- **Status letters that did not fit one value:**
  - Prevent Damage is A+B.
  - WhiteKnights is "A by inheritance only, effectively D".
  - Auras is "C, layered on top of D".
  - EDF is C "with two USER-shaped parts".
  - treasure_hunt is D with your land rule ON.
- **Docs and comments that contradict the code or each other, as found by the drafts:**
  - Auras' "already reviewed".
  - Critter's "USER's explicit order".
  - Soldiers' "user-proposed".
  - Giants' discard proposal, which says Goliath's counters are "the search's"; they are hand order.
  - Prevent Damage's comment that ties keep "the search's plan order"; they keep hand order.
  - Snow's "stays OFF".
  - "default OFF" comments in Creature Giving, FiveColour and Kitty, although those flags are ON.
  - Stale `cast-order-rankings.md` sections for Melira, Minotaur, EDF and Dragons.

### Cross-deck questions

- **Q136** On single-main decks, print and pin every cast as m1 in the report and in `MainPhaseOverride`. This changes no play. It covers BreachingDragonstorm (Altisaur m2 -> m1), CritterLifegain (Serra, Heliod), Fluctuator, Pirates, slivers_vial, Soldiers and treasure_hunt, and the report part of Knights Q4. [yes]
- **Q137** Fix the report: labels for ranks 8/9 and every provider rank (`CastOrderTierName`; Kitty's rank/4096 decode), "(tie: hand order)" markers, "not applied" notes for opaque and sac-land casts, and printed conditional branches. This changes no play (Kitty Q5, Knights Q4). [yes]
- **Q138** The adoption route for each deck order: a lever, OFF until your yes; then a scoped held-out A/B (deck aggregate <= 0, every slower game recovering at `--depth 8 --budget-ms 0`, with the value leaf and keep table in place); then default ON, with `=0` restoring today's order. Opaque decks (Breaching, treasure_hunt, Hinata2, burn) must also opt into `OrderOpaqueCastsByRank`, or their ranks stay unread. Is it (a) your yes, then the A/B, then ON (the default of Breaching, Giants, slivers_vial, Soldiers and treasure_hunt), or (b) the A/B first, then you decide (Minotaur's default)? [a]
- **Q139** Apply the generic findings per deck now, and change `GenericProvider` only after a suite A/B of the revised tier list above (Bruna Q5, Soldiers Q5)? [per deck now; generic after the A/B]
- **Q140** Should `MTG_BP_NEW_ONLY` keep an old-cards-only continuation whenever the sibling's rank order cannot express it? This is engine-wide (Mirrorwing Q4). [yes]
- **Q141** Give Hydras, Mill and Unpredictable Cyclone their own DeckProvider before their cards are implemented? Mill and Cyclone would be routed above `anti` by params only they carry, never by fetchlands (the Pirates precedent). [yes]

## 6. Engine defects found (not cast-order heuristics)

**E1. slivers_vial: the replicate count after a same-main Hatchery is a greedy pick inside the search window.**
- **Where:** `CanReplicate` gates the count fan on the board at enumeration time (SpellEffects.h:16977,
  TurnSolver.cpp:24874). Resolution then takes the line-held maximum (AIEngine.cpp:7789, TurnSolver.cpp:41612). The
  comment at TurnSolver.cpp:24804 is not true for this case.
- **Cost:** which Sliver gets the copies is set by resolution order, not by the search.

**E2. burn: Light Up the Stage's {R} spectacle line is enumerated only as the 2-card Plan B** (TurnSolver.cpp:49068).
- The powerset prices Light Up at {2}{R}.
- **Lost line:** the seed 9295 gi294 T3 lethal is `legal_not_enumerated`. It is still T4 at d8 unlimited and with
  `MTG_UNPRUNED=1`.

**E3. burn and Creature Giving: the sac-land pass casts every `sacrifice_land` spell after all the ranked casts.**
- **Where:** AIEngine.cpp:6486 and 6564-6572; TurnSolver.cpp:42927 and 42955-42963; `PlanCastOrder` 8700-8717.
- **Cost:**
  - Crop is cast last, against your order: Enlightened Tutor's card gets buried, and Wurm sweeps before Crop's Spirit
    (T6 vs T5).
  - Volley trails Eidolon: 2 self-damage per Eidolon, in 14 of 600 games.

**E4. Creature Giving: Crop's fetched untapped land gets no affordability credit and no re-solve.**
- **Where:** the credit is refused on purpose at TurnSolver.cpp:71775, and `MTG_ACQ_RESOLVE` covers only `tutor_to_hand`.
- **Lost line:** your T1 "land, then Crop, then Birds" is illegal per CheckLine. It was available in 4 of 24 sampled
  openings.

**E5. treasure_hunt: a deck rule lives in the engine.**
- **Where:** the d0 "TH before the land drop" rule is at AIEngine.cpp:3208-3236. When TH is not cast, the held drop is
  never replayed: only a cast draw engine triggers the second pass (AIEngine.cpp:6895).
- **Lost line:** probe A2 plays no land on T4 at d0 and wins T5. d3, d5 and d8 win T4. This is the lost-drop class
  that Creature Giving's `m_tutor_deferred_drop` fixed.

**E6. Bruna: a plan carries only one Equip per Equipment.**
- **Lost line:** "equip Greaves to the fresh Sage, tap it for WWW, cast Bruna, re-equip Greaves to Bruna, attack" is
  unfindable at any budget. P2 at d3 b0 wins T6; real play wins T5.

**E7. Melira Pod: a persist body that entered this main cannot loop before combat.**
- **Where:** TurnSolver.cpp:29325 checks `closer_active || closer_castable`, then walks the battlefield for the body.
- **Lost line:** at d6 b0, Finks cast from hand, Pod-put or Chord-put all win T7. The real line wins T6.

**E8. FiveColour: a Greaves-hasted Deathrite's mana never counts.**
- **Where:** `HasteUnlockedManaOf` returns nothing for `gy_land_exile_mana`, as a "conservative" choice
  (TurnSolver.cpp:11137).
- **Lost line:** the Deathrite probe has no win at d3 b200 or at d8 b0. The same board with Birds wins T5.

**E9. Angels: two gaps around Giada's mana.**
- `CreditCastOutput` does not credit `HasteUnlockedManaOf`, so the payable-order fallback cannot rescue "Greaves, Giada,
  unlock, Lightstall".
- `DeckUsesSecondMain` has no clause for a vigilant mana creature. Vigilant Giada's "attack, then pay" is unfindable;
  the T5 win appears only with `MTG_FORCE_USES_M2=1`. The card note that claims this works is false.

**E10. Goblins: Skirk Prospector's sacrifices happen before every cast** (pre-cast floats, AIEngine.cpp:6169-6176).
- A same-plan Hordemaster or Pashalik never sees them: on seed 2059 T2, six sacrifices produced zero triggers.
- A same-plan Skirk funds nothing, because sac actions are collected from the battlefield only.

**E11. Goblins: Three Tree City's growth inside a plan is not credited** by the flat gate, and is not admitted by
exec-feas either.
- **Lost line:** "bodies, then the TTC-paid cast" is unfindable unless Warchief or a Skirk float rides along.
- The "inert today" note at ManaPayment.cpp:4264-4268 is stale.

**E12. Pirates: Vial puts take their creature's cast rank** (since 7173178f, 10-06).
- A Vial-put Buccaneer ranks as if it paid a reveal.
- **Cost:** on that board your order is 1 point worse than generic (20 -> 20 vs 20 -> 19).
- **Fix:** `CastOrderActionRank` for `ActivateVial`.

**E13. Dragonstorm: the reducer-sink exemption burns rituals.**
- **Where:** `SubsetWastesAccelerant` (TurnSolver.cpp:10262) accepts any Medallion in the subset as every ritual's
  sink, although your order places the Medallion first.
- **Cost:** on s7200001 T4, four rituals were burned, leaving 6 red unspent. With `MTG_PAYOFF_PRUNE_REDUCER_SINK=0` the
  rituals are kept, and the T6 chain casts 13 spells instead of 9. The win turn is the same.

**E14. Engine-wide: `MTG_BP_NEW_ONLY` drops an old-cards-only continuation as its sibling's duplicate**, but the sibling
plays in rank order.
- **Lost line:** Mirrorwing's 88-damage and 50-damage lines are unfindable at budget 0, which breaks the
  complete-at-large-budget rule. Breaching, treasure_hunt, Goblins and StompySurprise are in the same class.

**E15. EDF: the payable-order fallback and the ladder credit neither an untapper's refund nor a land Aura's bonus**
(`FirstUnpayablePos`, `ApplyPayableCastOrderAt`).
- **Lost line:** {Fertile Ground, Drake} on 5 lands is never found: the walk failed 28 times.
- The untapper-hoisted retry (TurnSolver.cpp:7899-7918) runs only in human play (`HumanPlayActive`).

**E16. EDF, COMBO OFF route:**
- `DeployPieces` step 3 (DecisionProviders.cpp:29846) skips a second untapper whose cast refund is below its cost, even
  when its loop is the net-positive one. It misses the T4 kill.
- Step 2 (:29792) ignores your Aura order.

**E17. Fungus: Wild Growth is lost to the Chamber's bounce.**
- **Where:** the SAME-TURN KAROO HOST variant (TurnSolver.cpp:23923-23933) is offered last. Ties at win=6 go to the
  Forest host, because turn-to-win cannot price a lost card.
- **Lost line:** smoke seed 1001 gi0 T2 at d3/b10.

**E18. Fungus: three limits.**
- A Mycon cast this main cannot sacrifice for mana in the same main (`MTG_SAC_OUTLET_PAY` is OFF).
- The devour count is capped at the creatures in play when the main starts (TurnSolver.cpp:24822).
- `MTG_SAC_FODDER_SAME_LINE` pops its Saproling in the pre-pass, before every cast.

**E19. Fluctuator: drawing a land with cycling never re-plans the turn.**
- **Where:**
  - Re-solves fire only on a nonland draw or a pending rebuy (TurnSolver.cpp:44576).
  - `AnyHandCastableNow` (:9421) ignores the open land drop.
  - `SelectDigSource` (DecisionProviders.cpp:21922) has no protection for untapped lands.
- **Lost line:** "cycle into Capital City, drop it, cast Stinger" is unreachable at any budget. This is a code read;
  its frequency is unmeasured.

**E20. SelesnyaLifegain: life-scaled mana (`mana_per_life_gained`) gets no same-turn credit**, in EnumeratePlans, in
Genesis Wave's X sizing (~23110), or in `CreditCastOutput` (~2885).
- **Lost line:** your "Feed the Clan, then the Alchemist pays for the Wave" is unfindable at any budget.

**E21. SelesnyaLifegain: two greedy readings.**
- Nykthos Paragon's "you may" spends every copy on the turn's first gain. The draft frames this as a heuristic sweep.
- The growth tier reads the board after the prepay, keyed on `tapped` (DecisionProviders.cpp:3167 vs AIEngine.cpp:6207).

**E22. Hydras and SelesnyaLifegain: X spells.**
- **Where:** X is fixed at enumeration against the whole pool, and only the maximum is offered (TurnSolver.cpp:23217;
  `XCandidates`, DecisionProviders.cpp:3232; Genesis Wave ~23113).
- **Cost:** an X spell can never share a plan with another cast that costs mana.

**E23. Prevent Damage: the pain sweep and Manabarbs payment.**
- The pain sweep runs only at the end of main 1 (SpellEffects.h:30193; AIEngine.cpp:3112/6831; TurnSolver.cpp:44679).
  This blocks your ALL_M2 rule.
- `PdBatchTimingDominated` (TurnSolver.cpp:37531) prepays every land before Manabarbs. On probe P3 that loses 2 drains.

**E24. StompySurprise: activations always run after every cast.**
- Your [6] "activate before the tutor" is inexpressible.
- Without `MTG_TOP_RESOLVE`, the Worldly-Tutor-then-Turntimber combo is unfindable at any budget.
- Hypothesis, unverified: the dork-growth credit disagrees with the re-sorted feeders (4 of 26 counterexamples).

**E25. Minotaur: a possible rules bug.**
- The bestow face copies the creature subtypes (CardDatabase.cpp:214), so Ragemonger discounts a bestow cast. Under
  CR 702.103b that is probably not a Minotaur spell.
- A fetch is also played and cracked in one step, so "cast Priest, then crack" is inexpressible.

**E26. Mill: `PerformFetch` resolves a fetchland on play, so the fetchland never enters the battlefield.**
- Every fetch loses one landfall trigger.
- `DeckTouchesOpponentZones` must read the mill params, or the opponent's library is never dealt.

**E27. Unpredictable Cyclone: no extra-turn support exists.** Scalding Tarn's oracle note is also stale:
`PerformFetch` shuffles by default.

**E28. Snow: not root-caused.**
- On gs=1006 T4 main 2, {Astrolabe, Coldsteel Heart, Ice-Fang Coatl} was admitted on 4 lands with 5 mana of cost, and
  Coatl failed to pay.
- The `MTG_PAYABLE_ORDER` walk printed `pays=0` on land-first Astrolabe sets that paid in full (gs=1001 T5, 1013 T6,
  1042 T3).

**E29. WhiteKnights: Silverblade's soulbond partner.**
- `SoulbondPartner` (DecisionProviders.cpp:1613) breaks equal power by card number and ignores readiness.
- The `AppendSoulbondDeclineVariants` gate (TurnSolver.cpp:54141) assumes an order that only a hand-order tie decides.

**E30. Auras: the Light-Paws fetch target is a heuristic `.front()` pick, not a searched decision.**
- Its "last fetch" test deviates from your 08-25 rule.
- A duplicate Armor always outranks a Mask, giving up N-2.

**E31. Routing: provider capture by fetchlands.**
- Mill and Cyclone reach AntiLifegainProvider by accident, through Scalding Tarn's `fetch_land_types`
  (DecisionProviders.cpp:18522). AL's colour-coverage `FetchCandidates` and your AL discard buckets are live on both
  decks.
- Implementing Hydras' fetch spells with `tutor_to_hand` or `fetch_land_types` would move that deck the same way.

**Tooling.**
- `mtg` loads `cards.json` relative to the working directory, and with no file it silently plays with no cards
  (Breaching G7).
- Staged Urza's Incubators get no `chosen_subtype_id` (src/main.cpp:7213-7229).
- `--game-index` alone does not change the deal at seed 1001 (Kitty).
- Kitty's `name_hash & 0xFF` tie-break has about a 6% collision risk at 6 names per mana value.

## 7. Proposed next steps

1. **Settle section 3 first.** These are your own orders. The B items have amendments ready:
   - Hinata2: Irencrag 18 plus the opaque opt-in, with a ready-made arm.
   - StompySurprise: the 2-way slots plus TOP_RESOLVE.
   - Prevent Damage: the pain sweep, then a re-measure of ALL_M2.
2. **No-play-change hygiene that can land at once:**
   - The report fixes (Q137).
   - The stale code comments and docs listed in section 5.
   - Dragons' rulings written into `cast-order-rankings.md` (Q95).
   - Moving the Knights clauses to KnightsProvider (Q111). This must happen before any slivers_vial order lands, so
     the shared VialProvider stops coupling Knights, WhiteKnights, Soldiers and slivers_vial.
3. **Make each draft measurable before you sign off.** *(This is the requester's suggestion, not something a draft
   proposed.)*
   - Express each deck's order as data in its profile: param predicate -> rank, plus ranges.
   - One shared provider hook would read it, so every draft becomes an A/B arm in ONE pooled `mtg --batch`, with no
     per-deck C++ change.
   - What a table cannot express stays in C++ `ArrangeCastOrder`:
     - CritterLifegain's Heliod and its flip;
     - Pirates' Malcolm-first exception;
     - WhiteKnights' Silverblade and its twin;
     - the Knights Contender at cast time;
     - the Auras and EDF host rules;
     - Mirrorwing's payload arithmetic;
     - Angels' Greaves hoist;
     - StompySurprise's 2-way slots and Prevent Damage's Vito/Dina search (`WantsCastOrderingSearch`).
   - Until then, the drafts' own route applies: a per-deck `EnvOn` lever and heurarm slot, keyed by params rather than
     names, OFF until you approve.
4. **Measure** to the Q138 route, every deck arm in one pooled batch, with the value leaf and keep table in place.
   An order change ages those artifacts through play (the drafts call this out for Snow, Prevent Damage, Giants,
   slivers_vial and treasure_hunt).
5. **Suggested implementation order:**
   - **(a) The B items.**
   - **(b) Your rules that never ran:**
     - FiveColour's one-line ladder fix;
     - Creature Giving's Crop delivery;
     - EDF's route Aura order;
     - Fungus' Season-first;
     - Mirrorwing's Gold Rush after Fists;
     - burn's Light Up.
   - **(c) Scalar rank fixes where a draft showed a line lost at unlimited budget or at d8/b0:**
     - Anti-Lifegain's Enlightened at 22;
     - EDF's Cloud first;
     - Giants;
     - Minotaur;
     - Soldiers;
     - slivers_vial;
     - CritterLifegain's Thune;
     - Mirrorwing's Anger/Oracle;
     - WhiteKnights;
     - Pirates' Vial-put Buccaneer.
   - **(d) The rest of the rank changes:**
     - Prevent Damage's GSZ (it lost a turn at d3);
     - Angels;
     - Bruna;
     - SelesnyaLifegain;
     - Dragonstorm's Lathliss/Scourge;
     - Snow's Slumber/Astrolabe;
     - Melira;
     - Goblins;
     - Fluctuator.
   - **(e) Opt-ins for the opaque decks** (Breaching, treasure_hunt, burn), plus a repeat of the global `MTG_ORDER_OPAQUE`
     measurement.
   - **(f) The conditional slots** (`ArrangeCastOrder`).
   - **(g) The revised generic tier list as one lever, with a suite A/B.** It covers new decks and any deck left without
     an agreed order.
   - **(h) The section-6 engine defects**, each as its own measured change.
   - **(i) The provisional decks:** provider first, then cards, then these orders.
6. **Where the drafters expect little or no measured effect:**
   - **Expected near 0 by the drafters:** Breaching (an A/B near 0), CritterLifegain (today's order already gets the most
     counters in all 121 sampled multi-cast mains), Fluctuator, Fungus on the shipped list, Goblins, Melira, KittyEquipment,
     Knights and Dragons.
   - **Predicted, not measured:** Bruna's P3, Fluctuator §4.3, and all of Hydras, Mill and Cyclone.
   - **Rare or unsized:** Soldiers' Lieutenant edge (no game found), Creature Giving's Q4 (not checked for a lost turn),
     and WhiteKnights' second-main residual (no game replayed).
