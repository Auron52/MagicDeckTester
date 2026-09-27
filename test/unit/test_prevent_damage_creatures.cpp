// Unit tests for the Prevent Damage onboarding, phase I3 (2026-09-27): the deck's creatures' activated
// abilities and the sideboard's creatures. Every number is a RULES number read off the cards' Oracle
// text and Scryfall rulings.
//
//  1. Vito: "{3}{B}{B}: Creatures you control gain lifelink" sets lifelink on every OWN creature
//     (Vito included, the opponent's untouched); in combat each lifelinking attacker is its own
//     lifegain event -> Vito drains that much and Dina 1, per attacker.
//  2. Dina: "{1}, Sacrifice another creature: +X/+0" -- X is the victim's last-known power; a
//     sacrificed Purity is shuffled into the library (its graveyard replacement); the enumerator offers
//     one sac variant per distinct victim; Dina is never her own victim.
//  3. Shriekmaw: the mandatory ETB hits an opponent spawn when there is one, else one of OUR nonblack
//     creatures (never an engine while a non-engine is legal), and is removed when only black
//     creatures remain. Evoke is offered as a separate cast variant.
//  4. Acidic Slime: the mandatory ETB destroys one of OUR permanents -- a tapped land before an
//     engine enchantment; Manabarbs only when it is the sole legal target.
//  5. Timeless Witness: the ETB returns the pinned graveyard card, else the provider's pick
//     (nonland, highest MV); an empty graveyard does nothing; the cast is fanned per graveyard name.
//     Eternalize: the card is exiled, the token is a black 4/4 Zombie with no mana cost, and its ETB
//     fires again.
//  6. Dimir House Guard: Transmute finds only a mana-value-4 card, puts it in hand, discards itself.
//  7. Bilbo: the activation is gated at 111 life; it exiles Bilbo, puts every library creature except
//     a second copy of a legend we control and Acidic Slime, and shuffles.
#include <doctest/doctest.h>

#include "ai/Combat.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <set>
#include <string>

namespace
{

void EnsureCardsPd3()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& D3(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

const PreventDamageProvider& Prov3()
{
    static const PreventDamageProvider p;
    return p;
}

GameState Board3()
{
    EnsureCardsPd3();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.dmg_events_armed    = true;
    s.own_death_live      = true;
    s.m_provider          = &Prov3();
    return s;
}

int g_n3 = 5000;
int Put3(GameState& s, const std::string& name, int controller = 0, bool tapped = false)
{
    Permanent p;
    p.card              = D3(name).card;
    p.card.m_number     = g_n3++;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = false;
    p.tapped            = tapped;
    s.battlefield.push_back(p);
    return p.card.m_number;
}

// A goldfish opponent spawn: a colourless, non-artifact vanilla creature (GameEngine's shape).
int PutSpawn(GameState& s, int pw)
{
    Card token;
    token.m_name = std::to_string(pw) + "/" + std::to_string(pw) + " Creature";
    token.RehashName();
    token.AddType(CardType::Creature);
    token.m_power     = pw;
    token.m_toughness = pw;
    token.m_number    = g_n3++;
    Permanent perm;
    perm.card             = token;
    perm.controller_index = 1;
    perm.owner_index      = 1;
    s.battlefield.push_back(perm);
    return token.m_number;
}

void Hand3(GameState& s, const std::string& name)
{
    Card c = D3(name).card; c.m_number = g_n3++; s.players[0].hand.push_back(c);
}
void Lib3(GameState& s, const std::string& name)
{
    Card c = D3(name).card; c.m_number = g_n3++; s.players[0].library.push_back(c);
}
void Gy3(GameState& s, const std::string& name)
{
    Card c = D3(name).card; c.m_number = g_n3++; s.players[0].graveyard.push_back(c);
}

int IdxOf(const GameState& s, int num)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[i].card.m_number == num) { return i; } }
    return -1;
}
bool OnBf3(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return true; } }
    return false;
}
template <class Zone>
int CountIn(const Zone& z, const std::string& name)
{
    int n = 0;
    for (const Card& c : z) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

}   // namespace

TEST_CASE("Vito: team lifelink on every own creature; each lifelink attacker is its own drained event")
{
    GameState s = Board3();
    const int vito = Put3(s, "Vito, Thorn of the Dusk Rose");
    const int dina = Put3(s, "Dina, Soul Steeper");
    const int tam  = Put3(s, "Tamanoa");
    const int opp  = PutSpawn(s, 2);
    CHECK(ApplyActivatePump(s, 0, vito, /*mode=*/2, /*k=*/1) == 1);
    for (int n : { vito, dina, tam }) { CHECK(s.battlefield[IdxOf(s, n)].temp_lifelink); }
    CHECK_FALSE(s.battlefield[IdxOf(s, opp)].temp_lifelink);
    // All three attack: 1 + 1 + 2 combat. Three lifelink events of 1, 1, 2 -> Vito drains that
    // much (4 total), Dina 1 per event (3). Lifelink damage is COMBAT damage by a creature, so
    // Tamanoa does not trigger on it.
    const std::vector<int> atk = { IdxOf(s, vito), IdxOf(s, dina), IdxOf(s, tam) };
    ResolveCombatDamage(s, atk, /*exalted_bonus=*/0, /*collect_descs=*/false);
    CHECK(s.players[0].life == 24);
    CHECK(s.players[1].life == 20 - 4 - 4 - 3);
}

TEST_CASE("Vito: the team grant is offered at most once, and only while an attacker lacks lifelink")
{
    GameState s = Board3();
    Put3(s, "Vito, Thorn of the Dusk Rose");
    for (int k = 0; k < 5; ++k) { Put3(s, "Overgrown Tomb"); }
    for (int k = 0; k < 10; ++k) { Lib3(s, "Overgrown Tomb"); }
    int grants = 0, maxk = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::ActivatePump && a.card_name.str() == "Vito, Thorn of the Dusk Rose")
            { ++grants; maxk = std::max(maxk, a.chosen_x); }
        }
    }
    CHECK(grants > 0);
    CHECK(maxk == 1);
    // Vito tapped (cannot attack) and no other creature: a no-op, so not offered.
    GameState t = Board3();
    Put3(t, "Vito, Thorn of the Dusk Rose", 0, /*tapped=*/true);
    for (int k = 0; k < 5; ++k) { Put3(t, "Overgrown Tomb"); }
    for (int k = 0; k < 10; ++k) { Lib3(t, "Overgrown Tomb"); }
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(t, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions) { CHECK(a.kind != Action::Kind::ActivatePump); }
    }
}

TEST_CASE("Dina: +X/+0 where X is the victim's power; a sacrificed Purity shuffles into the library")
{
    GameState s = Board3();
    const int dina = Put3(s, "Dina, Soul Steeper");
    const int pur  = Put3(s, "Purity");
    for (int k = 0; k < 8; ++k) { Lib3(s, "Forest"); }
    const auto sc0 = s.search_count;
    ApplySacCreatureOutlet(s, 0, dina, pur);
    CHECK_FALSE(OnBf3(s, "Purity"));
    CHECK(s.battlefield[IdxOf(s, dina)].EffectivePower() == 1 + 6);
    CHECK(CountIn(s.players[0].graveyard, "Purity") == 0);
    CHECK(CountIn(s.players[0].library, "Purity") == 1);
    CHECK(s.search_count == sc0 + 1);                 // the replacement's shuffle
}

TEST_CASE("Dina: one sac variant per distinct victim, never herself; none while Dina cannot attack")
{
    GameState s = Board3();
    s.players[1].life = 5;       // within the lethal gate's reach (see FodderSacUseful)
    Put3(s, "Dina, Soul Steeper");
    Put3(s, "Tamanoa");
    Put3(s, "Rhox Faithmender");
    Put3(s, "Overgrown Tomb");
    for (int k = 0; k < 10; ++k) { Lib3(s, "Overgrown Tomb"); }
    std::set<std::string> victims;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::SacCreatureOutlet) { continue; }
            for (const Permanent& q : s.battlefield)
            { if (q.card.m_number == a.sac_victim_id) { victims.insert(q.card.m_name.str()); } }
        }
    }
    CHECK(victims == std::set<std::string>{ "Tamanoa", "Rhox Faithmender" });
    // At 20 life the same board cannot reach lethal (4 ready power + a 2-power pump), so the
    // provider's lethal gate offers no sacrifice (MTG_PD_DINA_LETHAL_GATE).
    s.players[1].life = 20;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions) { CHECK(a.kind != Action::Kind::SacCreatureOutlet); }
    }
    // Summoning-sick Dina: the pump has no consumer -> the provider offers no sacrifice.
    GameState t = Board3();
    t.players[1].life = 1;
    const int d2 = Put3(t, "Dina, Soul Steeper");
    t.battlefield[IdxOf(t, d2)].entered_this_turn = true;
    Put3(t, "Tamanoa");
    Put3(t, "Overgrown Tomb");
    for (int k = 0; k < 10; ++k) { Lib3(t, "Overgrown Tomb"); }
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(t, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions) { CHECK(a.kind != Action::Kind::SacCreatureOutlet); }
    }
}

TEST_CASE("Shriekmaw: opponent spawn first, else our least valuable nonblack creature, else nothing")
{
    {   // An opponent spawn is legal (colourless) -> it dies, not our Tamanoa.
        GameState s = Board3();
        Put3(s, "Tamanoa");
        const int opp = PutSpawn(s, 3);
        const int sm  = Put3(s, "Shriekmaw");
        FireOwnEtbTriggers(s, 0, IdxOf(s, sm), std::string(), -1);
        CHECK(IdxOf(s, opp) < 0);
        CHECK(OnBf3(s, "Tamanoa"));
    }
    {   // No spawn: MUST hit one of ours. Vito is black (illegal); Tamanoa is an engine, so the
        // Vexing Shusher dies.
        GameState s = Board3();
        Put3(s, "Tamanoa");
        Put3(s, "Vito, Thorn of the Dusk Rose");
        Put3(s, "Vexing Shusher");
        const int sm = Put3(s, "Shriekmaw");
        FireOwnEtbTriggers(s, 0, IdxOf(s, sm), std::string(), -1);
        CHECK(OnBf3(s, "Tamanoa"));
        CHECK(OnBf3(s, "Vito, Thorn of the Dusk Rose"));
        CHECK_FALSE(OnBf3(s, "Vexing Shusher"));
        CHECK(CountIn(s.players[0].graveyard, "Vexing Shusher") == 1);
    }
    {   // Only a Purity is legal -> it dies and its replacement shuffles it into the library.
        GameState s = Board3();
        Put3(s, "Purity");
        Put3(s, "Dina, Soul Steeper");
        for (int k = 0; k < 5; ++k) { Lib3(s, "Forest"); }
        const int sm = Put3(s, "Shriekmaw");
        FireOwnEtbTriggers(s, 0, IdxOf(s, sm), std::string(), -1);
        CHECK_FALSE(OnBf3(s, "Purity"));
        CHECK(CountIn(s.players[0].library, "Purity") == 1);
    }
    {   // Only black creatures: the trigger is removed, nothing dies.
        GameState s = Board3();
        Put3(s, "Vito, Thorn of the Dusk Rose");
        Put3(s, "Dina, Soul Steeper");
        const int sm = Put3(s, "Shriekmaw");
        const std::size_t n = s.battlefield.size();
        FireOwnEtbTriggers(s, 0, IdxOf(s, sm), std::string(), -1);
        CHECK(s.battlefield.size() == n);
    }
}

TEST_CASE("Shriekmaw: evoke {1}{B} is a separate cast variant")
{
    GameState s = Board3();
    Put3(s, "Overgrown Tomb"); Put3(s, "Overgrown Tomb");
    Hand3(s, "Shriekmaw");
    for (int k = 0; k < 10; ++k) { Lib3(s, "Overgrown Tomb"); }
    bool evoke = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() == "Shriekmaw" && a.evoke)
            { evoke = true; CHECK(a.cost.ManaValue() == 2); CHECK(a.cost.black == 1); }
        }
    }
    CHECK(evoke);
}

TEST_CASE("Acidic Slime: destroys one of OUR permanents -- a tapped land before Manabarbs")
{
    {
        GameState s = Board3();
        Put3(s, "Manabarbs");
        Put3(s, "Battlefield Forge");
        const int cob = Put3(s, "City of Brass", 0, /*tapped=*/true);
        const int sl  = Put3(s, "Acidic Slime");
        FireOwnEtbTriggers(s, 0, IdxOf(s, sl), std::string(), -1);
        CHECK(IdxOf(s, cob) < 0);
        CHECK(OnBf3(s, "Manabarbs"));
        CHECK(OnBf3(s, "Battlefield Forge"));
        CHECK(CountIn(s.players[0].graveyard, "City of Brass") == 1);
    }
    {
        GameState s = Board3();
        Put3(s, "Manabarbs");
        const int sl = Put3(s, "Acidic Slime");
        FireOwnEtbTriggers(s, 0, IdxOf(s, sl), std::string(), -1);
        CHECK_FALSE(OnBf3(s, "Manabarbs"));      // the sole legal target: mandatory
    }
}

TEST_CASE("Timeless Witness: pinned return, provider pick, empty graveyard, per-name cast variants")
{
    {
        GameState s = Board3();
        Gy3(s, "Rolling Earthquake");
        Gy3(s, "City of Brass");
        const int w = Put3(s, "Timeless Witness");
        FireOwnEtbTriggers(s, 0, IdxOf(s, w), "City of Brass", -1);
        CHECK(CountIn(s.players[0].hand, "City of Brass") == 1);
        CHECK(CountIn(s.players[0].graveyard, "Rolling Earthquake") == 1);
    }
    {   // Unpinned: nonland first -> the Earthquake.
        GameState s = Board3();
        Gy3(s, "City of Brass");
        Gy3(s, "Rolling Earthquake");
        const int w = Put3(s, "Timeless Witness");
        FireOwnEtbTriggers(s, 0, IdxOf(s, w), std::string(), -1);
        CHECK(CountIn(s.players[0].hand, "Rolling Earthquake") == 1);
    }
    {   // Empty graveyard: removed.
        GameState s = Board3();
        const int w = Put3(s, "Timeless Witness");
        FireOwnEtbTriggers(s, 0, IdxOf(s, w), std::string(), -1);
        CHECK(s.players[0].hand.empty());
    }
    {   // The cast is fanned: one variant per distinct graveyard name + the unpinned base.
        GameState s = Board3();
        for (int k = 0; k < 4; ++k) { Put3(s, "Forest"); }
        Hand3(s, "Timeless Witness");
        Gy3(s, "Rolling Earthquake");
        Gy3(s, "Living Wish");
        Gy3(s, "Living Wish");
        for (int k = 0; k < 10; ++k) { Lib3(s, "Forest"); }
        std::set<std::string> pins;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        {
            for (const Action& a : p.actions)
            { if (a.card_name.str() == "Timeless Witness") { pins.insert(a.tutor_target.str()); } }
        }
        CHECK(pins == std::set<std::string>{ "", "Rolling Earthquake", "Living Wish" });
    }
}

TEST_CASE("Timeless Witness: eternalize exiles it and makes a black 4/4 Zombie that re-triggers")
{
    GameState s = Board3();
    Gy3(s, "Timeless Witness");
    Gy3(s, "Living Wish");
    REQUIRE(ApplyEternalize(s, 0, "Timeless Witness"));
    CHECK(CountIn(s.exile, "Timeless Witness") == 1);
    CHECK(CountIn(s.players[0].graveyard, "Timeless Witness") == 0);
    const Permanent* tok = nullptr;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Timeless Witness") { tok = &p; } }
    REQUIRE(tok != nullptr);
    CHECK(tok->is_token);
    CHECK(tok->EffectivePower() == 4);
    CHECK(tok->card.m_toughness.value_or(0) == 4);
    CHECK(tok->card.HasColor(Color::Black));
    CHECK_FALSE(tok->card.HasColor(Color::Green));
    CHECK(tok->card.m_mana_cost.ManaValue() == 0);
    bool zombie = false, shaman = false;
    for (const std::string& st : tok->card.m_subtypes) { zombie |= st == "Zombie"; shaman |= st == "Shaman"; }
    CHECK(zombie);
    CHECK(shaman);
    // Its ETB fired again (the Witness itself is exiled, so the Wish comes back).
    CHECK(CountIn(s.players[0].hand, "Living Wish") == 1);
    // Black now: not a legal Shriekmaw target.
    CHECK_FALSE(EtbDestroyTargetLegal(*tok, D3("Shriekmaw").params));
}

TEST_CASE("Dimir House Guard: Transmute finds a mana-value-4 card only")
{
    GameState s = Board3();
    Hand3(s, "Dimir House Guard");
    Lib3(s, "Tamanoa");          // MV 3 -- not legal
    Lib3(s, "Manabarbs");        // MV 4
    Lib3(s, "Pyrohemia");        // MV 4
    const CardParams tp = TransmuteSearchParams(D3("Dimir House Guard").card);
    std::set<std::string> cands;
    for (const std::string& nm : Prov3().TutorCandidates(s, 0, tp)) { cands.insert(nm); }
    CHECK(cands == std::set<std::string>{ "Manabarbs", "Pyrohemia" });
    const auto sc0 = s.search_count;
    ApplyTransmute(s, 0, 0, "Dimir House Guard", "Pyrohemia");
    CHECK(CountIn(s.players[0].hand, "Pyrohemia") == 1);
    CHECK(CountIn(s.players[0].hand, "Dimir House Guard") == 0);
    CHECK(CountIn(s.players[0].graveyard, "Dimir House Guard") == 1);
    CHECK(CountIn(s.players[0].library, "Pyrohemia") == 0);
    CHECK(s.search_count == sc0 + 1);
    // The enumerator offers one transmute per legal name.
    GameState e = Board3();
    for (int k = 0; k < 3; ++k) { Put3(e, "Overgrown Tomb"); }
    Hand3(e, "Dimir House Guard");
    Lib3(e, "Manabarbs"); Lib3(e, "Pyrohemia"); Lib3(e, "Tamanoa");
    for (int k = 0; k < 8; ++k) { Lib3(e, "Overgrown Tomb"); }
    std::set<std::string> found;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(e, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::Channel) { found.insert(a.tutor_target.str()); } }
    }
    CHECK(found == std::set<std::string>{ "Manabarbs", "Pyrohemia" });
}

TEST_CASE("Bilbo: gated at 111 life; exiles itself, puts every sensible library creature, shuffles")
{
    GameState s = Board3();
    const int bilbo = Put3(s, "Bilbo, Birthday Celebrant");
    Put3(s, "Vito, Thorn of the Dusk Rose");                     // a Vito already out
    Lib3(s, "Tamanoa"); Lib3(s, "Tamanoa");
    Lib3(s, "Vito, Thorn of the Dusk Rose");                      // second copy of a controlled legend
    Lib3(s, "Dina, Soul Steeper"); Lib3(s, "Dina, Soul Steeper"); // one copy is fine, two is not
    Lib3(s, "Acidic Slime");                                      // would destroy our own permanent
    Lib3(s, "Forest");
    s.players[0].life = 110;
    CHECK_FALSE(PermAbilitySourceLive(s, 0, bilbo, PermAbilityMode::LifeGatedPutCreatures));
    s.players[0].life = 111;
    CHECK(PermAbilitySourceLive(s, 0, bilbo, PermAbilityMode::LifeGatedPutCreatures));
    const auto sc0 = s.search_count;
    ApplyPermAbility(s, 0, bilbo, PermAbilityMode::LifeGatedPutCreatures);
    CHECK(CountIn(s.exile, "Bilbo, Birthday Celebrant") == 1);
    CHECK_FALSE(OnBf3(s, "Bilbo, Birthday Celebrant"));
    int tam = 0, vito = 0, dina = 0;
    for (const Permanent& p : s.battlefield)
    {
        tam  += p.card.m_name.str() == "Tamanoa";
        vito += p.card.m_name.str() == "Vito, Thorn of the Dusk Rose";
        dina += p.card.m_name.str() == "Dina, Soul Steeper";
    }
    CHECK(tam == 2);
    CHECK(vito == 1);
    CHECK(dina == 1);
    CHECK_FALSE(OnBf3(s, "Acidic Slime"));
    CHECK(CountIn(s.players[0].library, "Vito, Thorn of the Dusk Rose") == 1);
    CHECK(CountIn(s.players[0].library, "Acidic Slime") == 1);
    CHECK(s.search_count == sc0 + 1);
}
