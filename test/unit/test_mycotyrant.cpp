// Unit tests for The Mycotyrant (2026-10-14) -- see docs/design/mycotyrant-scope.md.
//
// Every failure guarded here is SILENT: the game finishes, prints an ordinary win turn, and the
// card is simply worth less (or more) than the rules say. None of it would show up in the
// regression suite, which compares each deck to its own committed fingerprint -- and this card is
// in no committed deck.
//
//  1. THE CDA IS THE CARD. Printed */* is 0/0, so power and toughness come entirely from
//     DynamicBasePower/DynamicBaseToughness. If the new param were read on only one axis the
//     creature would be an N/0 and die to the toughness SBA on resolution.
//
//  2. ONE POOL, NOT ONE PER SUBTYPE. "creatures you control that are Fungi and/or Saprolings"
//     counts a creature that is both exactly ONCE. Reachable, not theoretical: this card's own
//     tokens are Fungi and the deck swims in Saprolings.
//
//  3. IT COUNTS TOKENS, which is the whole reason the subtype test runs against the permanent's own
//     Card instead of a CardDefinition -- every Saproling in this deck is a token, and a token has
//     no definition, so a definition-keyed predicate would be dead on the entire population.
//
//  4. DESCEND COUNTS PERMANENT CARDS, NOT TOKENS. ~40 Saprolings a game are created and sacrificed
//     and not one of them is a descend. Getting this backwards makes a near-dead rider look like
//     the best card in the deck.
//
//  5. ...and the count must go through ZoneCard. A graveyard Card is a NAME-ONLY placeholder with
//     an EMPTY type mask, so a raw `gy_card.IsPermanentCard()` is false for a land and for a
//     creature alike -- X would be permanently 0, silently. Asserted directly, both halves.
//
//  6. THE PRESENCE TEST IS NOT THE COUNT. Three sites used `endstep_lifegain_tokens > 0` as "does
//     this card have an end-step token trigger"; this card's count is dynamic, so it answers 0 and
//     would never be collected, never fire, and never arm the stamp that keeps the hoisted lifegain
//     early-out from swallowing it. That is the Brightcap Badger bug exactly, and the shared
//     HasEndStepTokenTrigger predicate is what stops a repeat.
//
//  7. THE GATE IS THE GyReader BIT. GyR_DescendCount is what folds the graveyard's shape into both
//     state keys; without it two states differing only in whether a Peat Bog has sacrificed itself
//     share a key and reach different boards.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

int Put(GameState& s, const std::string& name, int controller, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = controller;
    p.owner_index      = controller;
    p.def_absent       = false;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// A token, built exactly as CreateTokenOnce builds one: no CardDefinition, def_absent set, the
// subtypes written onto the Card itself. This is the population the CDA actually counts.
int PutToken(GameState& s, const std::vector<std::string>& subtypes, int controller, int number)
{
    Permanent p;
    p.card.m_name      = "1/1 Token";
    p.card.RehashName();
    p.card.AddType(CardType::Creature);
    p.card.m_subtypes  = subtypes;
    p.card.m_power     = 1;
    p.card.m_toughness = 1;
    p.card.m_number    = number;
    p.is_token         = true;
    p.def_absent       = true;
    p.controller_index = controller;
    p.owner_index      = controller;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// A card in a ZONE (graveyard, library, hand) is a NAME-ONLY placeholder with EMPTY type, colour
// and mana-cost masks -- exactly what DeckLoader::MakePlaceholder builds. Reproduced here rather
// than called, because that function is private to DeckLoader; the point of these tests is that
// every characteristic read on such a card must go through its definition.
//
// The NUMBER matters and is not decoration: deck cards carry 1..N from setup, and
// `m_number >= GameState::kFirstTokenNumber` is how a bare Card in a graveyard is known to be a
// token. Defaults into the real-card range.
Card Placeholder(const std::string& name, int number = 7)
{
    Card c;
    c.m_name   = CardDatabase::Instance().CanonicalName(name);
    c.RehashName();
    c.m_number = number;
    return c;
}

// The Card a sacrificed TOKEN leaves behind when an unguarded graveyard.push_back pushes it: unlike
// a zone placeholder it carries its OWN masks (a token has no CardDefinition to fall back to), so
// ZoneCard().IsPermanentCard() is TRUE for it. That is the trap -- the only thing separating it
// from a real creature card is its id.
Card SacrificedSaprolingToken(int number)
{
    Card c;
    c.m_name = "1/1 Saproling Token";
    c.RehashName();
    c.AddType(CardType::Creature);
    c.AddColor(Color::Green);
    c.m_subtypes  = { "Saproling" };
    c.m_power     = 1;
    c.m_toughness = 1;
    c.m_number    = number;
    return c;
}

const Permanent& ByNumber(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    static Permanent none;
    REQUIRE_MESSAGE(false, "permanent not found: ", number);
    return none;
}

int Power(const GameState& s, const std::string& name)
{ return DynamicBasePower(Def(name), s, 0); }
int Toughness(const GameState& s, const std::string& name)
{ return DynamicBaseToughness(Def(name), s, 0); }

constexpr const char* kMyco = "The Mycotyrant";

} // namespace

TEST_CASE("The Mycotyrant is in cards.json with the printed characteristics the CDA depends on")
{
    EnsureCardsLoaded();
    const CardDefinition& d = Def(kMyco);

    // A three-mana BG legend.
    CHECK(d.card.HasSupertype(Supertype::Legendary));   // caps the battlefield at one => 1-2 copies
    CHECK(d.card.IsCreature());
    CHECK(d.card.m_mana_cost.black == 1);
    CHECK(d.card.m_mana_cost.green == 1);
    CHECK(d.card.m_mana_cost.generic == 1);

    // IT IS ITSELF A FUNGUS, which is what makes it a 1/1 alone rather than a 0/0 (see the SBA
    // case below). "Elder Fungus" is two subtypes and only the second one is counted by anything.
    CHECK(CardHasSubtype(d.card, "Fungus"));
    CHECK(CardHasSubtype(d.card, "Elder"));

    // Printed */* is stored as 0/0 -- the whole P/T comes from the CDA.
    CHECK(d.card.m_power     == 0);
    CHECK(d.card.m_toughness == 0);

    // The CDA's filter, and the end-step token's spec.
    REQUIRE(d.params.pt_equals_creatures_you_control_subtypes.size() == 2);
    CHECK(d.params.pt_equals_creatures_you_control_subtypes[0] == "Fungus");
    CHECK(d.params.pt_equals_creatures_you_control_subtypes[1] == "Saproling");
    CHECK(d.params.endstep_tokens_equal_descend);
    CHECK(d.params.endstep_tokens_unconditional);    // no intervening-if: X may legally be 0
    CHECK(d.params.endstep_token_power     == 1);
    CHECK(d.params.endstep_token_toughness == 1);
    CHECK(d.params.endstep_token_color     == "B");
    REQUIRE(d.params.endstep_token_subtypes.size() == 1);
    // FUNGUS, NOT SAPROLING -- the deck's three "Sacrifice a Saproling" outlets cannot eat these,
    // and they DO feed the CDA.
    CHECK(d.params.endstep_token_subtypes[0] == "Fungus");
}

TEST_CASE("The CDA counts Fungi and Saprolings you control, on BOTH axes")
{
    EnsureCardsLoaded();
    GameState s = Fresh();

    // Alone on an empty board it is a 1/1: it counts itself.
    Put(s, kMyco, 0, 1);
    CHECK(Power(s, kMyco)     == 1);
    CHECK(Toughness(s, kMyco) == 1);

    // Three Saproling tokens and a second Fungus CARD.
    PutToken(s, { "Saproling" }, 0, 2);
    PutToken(s, { "Saproling" }, 0, 3);
    PutToken(s, { "Saproling" }, 0, 4);
    Put(s, "Sporecrown Thallid", 0, 5);               // Creature -- Fungus
    CHECK(Power(s, kMyco)     == 5);
    CHECK(Toughness(s, kMyco) == 5);

    // A creature that is NEITHER does not count. Brightcap Badger is a Badger Druid.
    Put(s, "Brightcap Badger", 0, 6);
    CHECK(Power(s, kMyco) == 5);

    // NOR DOES THE OPPONENT'S. "creatures YOU control".
    PutToken(s, { "Saproling" }, 1, 7);
    Put(s, "Sporecrown Thallid", 1, 8);
    CHECK(Power(s, kMyco)     == 5);
    CHECK(Toughness(s, kMyco) == 5);
}

TEST_CASE("ONE POOL: a creature that is both a Fungus and a Saproling counts once")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, kMyco, 0, 1);
    PutToken(s, { "Fungus", "Saproling" }, 0, 2);

    // Two bodies, two counted -- not three. This is the same reading RefreshStoriedLatch gives
    // "artifacts, legendaries, and/or Sagas", and it is reachable here because this card's own
    // end-step tokens are Fungi while the deck's other tokens are Saprolings.
    CHECK(Power(s, kMyco)     == 2);
    CHECK(Toughness(s, kMyco) == 2);

    // Order of the filter list cannot matter either.
    const std::vector<std::string> reversed = { "Saproling", "Fungus" };
    CHECK(CreatureSubtypeCount(s, 0, reversed) == 2);
}

TEST_CASE("The CDA's toughness is read by the lethal-damage SBA, so a lone Mycotyrant lives")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int idx = Put(s, kMyco, 0, 1);

    // The BARE value is 0 -- which is what the SBA used to read, and what would kill it on
    // resolution. LethalToughness is the shared helper both worlds' SBAs go through.
    CHECK(s.battlefield[idx].EffectiveToughness() == 0);
    CHECK(LethalToughness(s.battlefield[idx], s)  == 1);

    // And it scales: with three Saprolings out it is a 4/4 to the SBA too.
    PutToken(s, { "Saproling" }, 0, 2);
    PutToken(s, { "Saproling" }, 0, 3);
    PutToken(s, { "Saproling" }, 0, 4);
    CHECK(LethalToughness(s.battlefield[idx], s) == 4);
}

TEST_CASE("DESCEND counts permanent CARDS that arrived this turn, and no token ever does")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Player&   p = s.players[0];

    // Nothing in the graveyard, snapshot 0.
    CHECK(DescendedThisTurn(s, 0) == 0);

    // Two permanent cards arrive this turn: a depletion land sacrificing itself and a creature
    // card dying. Both are descends.
    p.graveyard.push_back(Placeholder("Peat Bog"));
    p.graveyard.push_back(Placeholder("Sporecrown Thallid"));
    CHECK(DescendedThisTurn(s, 0) == 2);

    // THE SNAPSHOT IS THE BASELINE, not a reset: cards already there at turn start are NOT
    // descends this turn. "the number of times you descended THIS TURN".
    p.gy_size_at_turn_start = 1;
    CHECK(DescendedThisTurn(s, 0) == 1);
    p.gy_size_at_turn_start = 2;
    CHECK(DescendedThisTurn(s, 0) == 0);

    // A NONPERMANENT card in the graveyard is not a descend. ("a permanent card")
    p.gy_size_at_turn_start = 0;
    p.graveyard.push_back(Placeholder("Lightning Bolt"));
    CHECK(DescendedThisTurn(s, 0) == 2);

    // A SACRIFICED TOKEN IS NOT A DESCEND -- and it is NOT excluded for free, because tokens DO
    // reach the graveyard vector. FOUND IN PLAY, not reasoned about: in the first 24 logged games
    // Vitaspore Thallid sacrificed Saproling token #1000, the token's Card was pushed onto the
    // graveyard by an unguarded site, and under a Doubling Season that produced TWO end-step tokens
    // on a turn whose true descend was ZERO. This deck sacrifices ~40 tokens a game, so an
    // unfiltered count would not be slightly wrong, it would be the whole card.
    s.players[0].graveyard.push_back(SacrificedSaprolingToken(GameState::kFirstTokenNumber));
    CHECK(DescendedThisTurn(s, 0) == 2);       // unmoved
    s.players[0].graveyard.push_back(SacrificedSaprolingToken(GameState::kFirstTokenNumber + 37));
    CHECK(DescendedThisTurn(s, 0) == 2);       // still unmoved

    // THE TRAP, ASSERTED: a token's leftover Card carries its OWN masks (there is no definition to
    // fall back to), so the permanent-type half of the test says YES to it. Only the id separates
    // it from a real creature card.
    const Card tok = SacrificedSaprolingToken(GameState::kFirstTokenNumber);
    CHECK(ZoneCard(tok).IsPermanentCard());
    CHECK(tok.m_number >= GameState::kFirstTokenNumber);

    // A real card's id is in the DECK range, so the token filter cannot swallow it -- including
    // the id-1 edge.
    s.players[0].graveyard.push_back(Placeholder("Forest", 1));
    CHECK(DescendedThisTurn(s, 0) == 3);
    s.players[0].graveyard.pop_back();

    // Per PLAYER: the opponent's graveyard is their descend, not ours.
    s.players[1].graveyard.push_back(Placeholder("Peat Bog"));
    CHECK(DescendedThisTurn(s, 0) == 2);
    CHECK(DescendedThisTurn(s, 1) == 1);

    // Underflow is clamped, not wrapped (a card leaving the graveyard -- see Player's note).
    p.gy_size_at_turn_start = 99;
    CHECK(DescendedThisTurn(s, 0) == 0);
}

TEST_CASE("DESCEND must read the graveyard card through ZoneCard, or X is silently always 0")
{
    EnsureCardsLoaded();
    GameState s = Fresh();

    // THE TRAP, ASSERTED DIRECTLY. DeckLoader::MakePlaceholder builds every graveyard/library/hand
    // Card from its NAME alone, so the type mask is EMPTY: a Peat Bog in a graveyard is not a Land
    // and a Sporecrown Thallid in a graveyard is not a Creature as far as its own Card is
    // concerned. A descend counter written against the raw card compiles, runs, and reports 0
    // forever.
    const Card gy_land     = Placeholder("Peat Bog");
    const Card gy_creature = Placeholder("Sporecrown Thallid");
    CHECK_FALSE(gy_land.IsPermanentCard());
    CHECK_FALSE(gy_creature.IsPermanentCard());
    // ...while the definition behind each of them is a permanent card.
    CHECK(ZoneCard(gy_land).IsPermanentCard());
    CHECK(ZoneCard(gy_creature).IsPermanentCard());

    s.players[0].graveyard.push_back(gy_land);
    s.players[0].graveyard.push_back(gy_creature);
    CHECK(DescendedThisTurn(s, 0) == 2);
}

TEST_CASE("The end-step trigger is COLLECTED and SIZED even though its token count is dynamic")
{
    EnsureCardsLoaded();
    const CardParams& myco   = Def(kMyco).params;
    const CardParams& badger = Def("Brightcap Badger").params;
    const CardParams& ocelot = Def("Ocelot Pride").params;

    // THE PRESENCE TEST IS NOT THE COUNT. This card leaves endstep_lifegain_tokens at 0, so every
    // site that used `> 0` to mean "has an end-step token trigger" would skip it entirely.
    CHECK(myco.endstep_lifegain_tokens == 0);
    CHECK(HasEndStepTokenTrigger(myco));
    // ...and the predicate still answers correctly for the fixed-count family it replaced.
    CHECK(HasEndStepTokenTrigger(badger));
    CHECK(HasEndStepTokenTrigger(ocelot));
    CHECK_FALSE(HasEndStepTokenTrigger(Def("Sporecrown Thallid").params));

    GameState s = Fresh();
    // Fixed-count cards are unaffected by the descend plumbing.
    CHECK(EndStepTokenCount(badger, s, 0) == 1);
    CHECK(EndStepTokenCount(ocelot, s, 0) == 1);
    // The dynamic one reads descend.
    CHECK(EndStepTokenCount(myco, s, 0) == 0);
    s.players[0].graveyard.push_back(Placeholder("Peat Bog"));
    CHECK(EndStepTokenCount(myco, s, 0) == 1);
}

TEST_CASE("End step: X tokens, 1/1 black Fungus, and none at all on a descend-free turn")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    s.deck_has_unconditional_endstep_tokens = true;   // what GoldFishRunner stamps for this list
    Put(s, kMyco, 0, 1);
    REQUIRE(s.players[0].life_gained_this_turn == 0); // this deck gains no life, ever

    // A descend-free turn makes NOTHING, and that is not an early-out: the trigger still goes on
    // the stack (no intervening-if), X is simply 0.
    PerformEndStepLifegainTokens(s);
    CHECK(s.battlefield.size() == 1);

    // Two permanent cards hit the graveyard this turn -> two 1/1 black Fungus tokens.
    s.players[0].graveyard.push_back(Placeholder("Peat Bog"));
    s.players[0].graveyard.push_back(Placeholder("Hickory Woodlot"));
    PerformEndStepLifegainTokens(s);
    REQUIRE(s.battlefield.size() == 3);

    int fungus_tokens = 0;
    for (const Permanent& t : s.battlefield)
    {
        if (!t.is_token) { continue; }
        ++fungus_tokens;
        CHECK(t.controller_index == 0);
        CHECK(t.card.m_power     == 1);
        CHECK(t.card.m_toughness == 1);
        CHECK(t.card.HasColor(Color::Black));
        CHECK(CardHasSubtype(t.card, "Fungus"));
        CHECK_FALSE(CardHasSubtype(t.card, "Saproling"));   // the sac outlets cannot eat these
    }
    CHECK(fungus_tokens == 2);

    // ...and each token raises the CDA by one, so it is now a 3/3.
    CHECK(Power(s, kMyco)     == 3);
    CHECK(Toughness(s, kMyco) == 3);

    // THE HOISTED LIFEGAIN EARLY-OUT MUST NOT SWALLOW IT. With the deck stamp false (the state a
    // deck without an unconditional trigger carries) PerformEndStepLifegainTokens returns before
    // the per-card test -- which is the Brightcap Badger bug, and the reason the stamp has to be
    // armed by HasEndStepTokenTrigger rather than by the count.
    GameState t = Fresh();
    t.deck_has_unconditional_endstep_tokens = false;
    Put(t, kMyco, 0, 1);
    t.players[0].graveyard.push_back(Placeholder("Peat Bog"));
    PerformEndStepLifegainTokens(t);
    CHECK(t.battlefield.size() == 1);                 // documents the gate, not a desired outcome
}

TEST_CASE("A deck holding The Mycotyrant stamps GyR_DescendCount, and no other deck does")
{
    EnsureCardsLoaded();

    // THE BIT IS THE GATE for the whole feature: it is what folds the graveyard's shape into
    // BuildSimKey and dominance::Build. Without it, two states differing only in whether a Peat
    // Bog has sacrificed itself share a key -- and their end steps make a different number of
    // tokens, so they reach different boards.
    Decklist with_card;
    with_card.mainboard.push_back(Placeholder(kMyco));
    with_card.mainboard.push_back(Placeholder("Sporecrown Thallid"));
    CHECK((GoldFishRunner::DeckGraveyardReaders(with_card) & GyR_DescendCount) != 0);

    // The rest of candidate-b sets NO graveyard bit at all, which is exactly why this one had to
    // be added: the deck's graveyard is folded nowhere today.
    Decklist without;
    for (const char* n : { "Sporecrown Thallid", "Mycoloth", "Peat Bog", "Saproling Burst",
                           "Brightcap Badger", "Doubling Season", "Deathspore Thallid" })
    { without.mainboard.push_back(Placeholder(n)); }
    CHECK((GoldFishRunner::DeckGraveyardReaders(without) & GyR_DescendCount) == 0);
}
