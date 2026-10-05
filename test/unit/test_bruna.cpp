// Unit tests for the Bruna onboarding (2026-10-05): the shroud rule for Aura SPELLS vs non-targeting
// puts, the CR 704.5m orphaned-Aura SBA (incl. the legend rule), Almost Perfect's layer-7b base set,
// Glittering Wish's multicolour filter, Somberwald Sage's creature-only float, Bruna's attack-trigger
// gather (and its provider dominance collapse), Colossification's ETB tap, and Arcanum Wings' swap.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsBr()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardBr(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c = d->card;
    c.m_number = number;
    c.RehashName();
    return c;
}

struct BoardBr
{
    GameState s;
    int next = 1;
    BoardBr()
    {
        EnsureCardsBr();
        s.active_player_index = 0;
        s.turn_number         = 5;
    }
    int Put(const std::string& name, bool tapped = false, bool sick = false, int attached_to = 0,
            int equipped_to = 0)
    {
        Permanent p;
        p.card              = CardBr(name, next++);
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.tapped            = tapped;
        p.entered_this_turn = sick;
        p.aura_attached_to  = attached_to;
        p.equipped_to       = equipped_to;
        s.battlefield.push_back(p);
        return p.card.m_number;
    }
    int Hand(const std::string& name) { s.players[0].hand.push_back(CardBr(name, next)); return next++; }
    int Grave(const std::string& name) { s.players[0].graveyard.push_back(CardBr(name, next)); return next++; }
    void Side(const std::string& name) { s.players[0].sideboard.push_back(CardBr(name, next++)); }
    const Permanent* ByNum(int num) const
    {
        for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
        return nullptr;
    }
    int CombatPower(int num) const { return CombatPowerOf(*ByNum(num), s); }
};

bool Has(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

}   // namespace

TEST_CASE("Bruna: an Aura SPELL cannot target a Lightning Greaves'd creature, a PUT can enchant it")
{
    BoardBr b;
    const int bruna = b.Put("Bruna, Light of Alabaster");
    const int birds = b.Put("Birds of Paradise");
    b.Put("Lightning Greaves", false, false, 0, bruna);
    const CardParams& colos = CardDatabase::Instance().Lookup("Colossification")->params;
    const std::vector<int> cast_hosts = LegalEnchantTargets(b.s, 0, colos);
    CHECK_FALSE(Has(cast_hosts, bruna));    // shroud: not a legal Aura-spell target (CR 303.4a)
    CHECK(Has(cast_hosts, birds));
    // Resolution re-checks: a searched target that is shrouded is not attached to.
    CHECK(ResolveEnchantTarget(b.s, 0, bruna) != bruna);
    // A put does not target (CR 303.4f): Bruna stays a legal host for her gather / an aura swap.
    CHECK(Has(AuraCouldEnchantHosts(b.s, 0, colos), bruna));
    // ...but an enchant-LAND Aura can never be put onto a creature.
    const CardParams& wild = CardDatabase::Instance().Lookup("Wild Growth")->params;
    CHECK_FALSE(AuraCouldEnchant(b.s, wild, *b.ByNum(bruna)));
}

TEST_CASE("Bruna: CR 704.5m -- an Aura on a legend lost to the legend rule goes to the graveyard")
{
    BoardBr b;
    const int b1 = b.Put("Bruna, Light of Alabaster");
    const int b2 = b.Put("Bruna, Light of Alabaster");
    const int aura = b.Put("Unflinching Courage", false, false, b2);   // on the copy the rule kills
    EnforceLegendRule(b.s, 0);
    CHECK(b.ByNum(b1) != nullptr);
    CHECK(b.ByNum(b2) == nullptr);
    CHECK(b.ByNum(aura) == nullptr);   // NOT left on the battlefield as an orphan
    int in_gy = 0;
    for (const Card& c : b.s.players[0].graveyard) { if (c.m_number == aura) { ++in_gy; } }
    CHECK(in_gy == 1);
    // The general sweep: an orphan left by any other detach site is swept at the next checkpoint.
    const int orphan = b.Put("Prodigious Growth", false, false, 0);
    CHECK(SweepOrphanedAuras(b.s) == 1);
    CHECK(b.ByNum(orphan) == nullptr);
}

TEST_CASE("Bruna: Almost Perfect sets BASE 9/10 under every other Aura (layer 7b before 7c)")
{
    BoardBr b;
    const int sage = b.Put("Somberwald Sage");   // printed 0/1
    b.Put("Almost Perfect", false, false, sage);
    CHECK(b.CombatPower(sage) == 9);
    b.Put("Eldrazi Conscription", false, false, sage);
    CHECK(b.CombatPower(sage) == 19);
    const std::pair<int, int> ab = AuraBonusFor(*b.ByNum(sage), b.s);
    CHECK(b.ByNum(sage)->EffectiveToughness() + ab.second == 20);   // 19/20
    // On Bruna (5/5) the base set is worth only +4.
    const int bruna = b.Put("Bruna, Light of Alabaster");
    b.Put("Almost Perfect", false, false, bruna);
    CHECK(b.CombatPower(bruna) == 9);
}

TEST_CASE("Bruna: Glittering Wish offers exactly the MULTICOLORED sideboard cards")
{
    BoardBr b;
    for (const char* n : { "Bruna, Light of Alabaster", "Indrik Umbra", "Almost Perfect",
                           "Unflinching Courage", "Mythic Proportions", "Avacyn's Pilgrim" })
    { b.Side(n); }
    const CardParams& wish = CardDatabase::Instance().Lookup("Glittering Wish")->params;
    const std::vector<std::string> c = GenericProvider().TutorCandidates(b.s, 0, wish);
    auto in = [&](const char* n) { return std::find(c.begin(), c.end(), std::string(n)) != c.end(); };
    CHECK(in("Bruna, Light of Alabaster"));
    CHECK(in("Indrik Umbra"));
    CHECK(in("Almost Perfect"));
    CHECK(in("Unflinching Courage"));
    CHECK_FALSE(in("Mythic Proportions"));   // mono-green
    CHECK_FALSE(in("Avacyn's Pilgrim"));     // mono-green
    CHECK(c.size() == 4);
}

TEST_CASE("Bruna: Somberwald Sage's unspent mana stays creature-only")
{
    BoardBr b;
    b.Put("Somberwald Sage");   // not sick: can tap
    ManaCost w; w.white = 1;    // Mother of Runes {W}
    REQUIRE(TapForCostShared(b.s, w, /*for_creature=*/true, nullptr, true));
    // Three of ONE colour were made; one paid {W}; two are left -- and they are RESTRICTED.
    CHECK(b.s.floating_mana.Total() == 0);
    CHECK(b.s.floating_creature_mana.Total() == 2);
    // A noncreature spell cannot spend them (Arcanum Wings {1}{U} / anything): nothing else untapped.
    ManaCost two; two.generic = 2;
    CHECK_FALSE(TapForCostShared(b.s, two, /*for_creature=*/false, nullptr, true));
    CHECK(b.s.floating_creature_mana.Total() == 2);   // a failed payment returns the reserve
    // ...but a second creature can (another Mother {W}).
    CHECK(TapForCostShared(b.s, w, /*for_creature=*/true, nullptr, true));
    CHECK(b.s.floating_creature_mana.Total() == 1);
}

TEST_CASE("Bruna: one-colour burst -- a lone Sage cannot pay {W}{U} (three of ONE colour)")
{
    BoardBr b;
    b.Put("Somberwald Sage");
    ManaCost wu; wu.white = 1; wu.blue = 1;
    CHECK_FALSE(TapForCostShared(b.s, wu, /*for_creature=*/true, nullptr, true));
    ManaCost ww; ww.white = 2;
    CHECK(TapForCostShared(b.s, ww, /*for_creature=*/true, nullptr, true));
}

TEST_CASE("Bruna: the gather dominance collapse -- one subset, or take/skip for Almost Perfect only")
{
    BoardBr b;
    const int bruna = b.Put("Bruna, Light of Alabaster");
    using Cand = DecisionProvider::AuraGatherCand;
    std::vector<Cand> cands = { { 1, 10, "Colossification" }, { 2, 11, "Prodigious Growth" },
                                { 0, 12, "Unflinching Courage" } };
    const GenericProvider gp;
    std::vector<std::vector<int>> r = gp.BrunaGatherCandidates(b.s, 0, *b.ByNum(bruna), cands);
    REQUIRE(r.size() == 1);
    CHECK(r[0] == std::vector<int>{ 0, 1, 2 });   // every additive Aura, always
    cands.push_back({ 1, 13, "Almost Perfect" });
    r = gp.BrunaGatherCandidates(b.s, 0, *b.ByNum(bruna), cands);
    REQUIRE(r.size() == 2);
    CHECK(r[0] == std::vector<int>{ 0, 1, 2, 3 });   // take-first
    CHECK(r[1] == std::vector<int>{ 0, 1, 2 });      // skip Almost Perfect
}

TEST_CASE("Bruna: the attack trigger gathers Auras from battlefield, hand and graveyard")
{
    BoardBr b;
    const int bruna = b.Put("Bruna, Light of Alabaster");
    const int birds = b.Put("Birds of Paradise", false, /*sick=*/true);
    b.Put("Lightning Greaves", false, false, 0, bruna);   // shroud does NOT stop the gather
    const int on_birds = b.Put("Unflinching Courage", false, false, birds);
    b.Put("Wild Growth", false, false, 0);                 // enchant LAND: never gathered
    const int colos = b.Hand("Colossification");
    const int prod  = b.Grave("Prodigious Growth");
    std::vector<int> atk;
    for (int i = 0; i < static_cast<int>(b.s.battlefield.size()); ++i)
    { if (b.s.battlefield[static_cast<std::size_t>(i)].card.m_number == bruna) { atk.push_back(i); } }
    FireAttackGatherAuras(b.s, 0, atk);
    CHECK(b.ByNum(on_birds)->aura_attached_to == bruna);   // moved: no enter
    REQUIRE(b.ByNum(colos) != nullptr);
    CHECK(b.ByNum(colos)->aura_attached_to == bruna);       // put from hand
    REQUIRE(b.ByNum(prod) != nullptr);
    CHECK(b.ByNum(prod)->aura_attached_to == bruna);        // put from graveyard
    CHECK(b.ByNum(bruna)->tapped);                          // Colossification's ETB tap (harmless: attacking)
    CHECK(b.CombatPower(bruna) == 5 + 2 + 20 + 7);
}

TEST_CASE("Bruna: a main-phase Colossification cast on a ready attacker stops it attacking")
{
    BoardBr b;
    const int birds = b.Put("Birds of Paradise");    // non-sick mana dork
    Permanent aura;
    aura.card = CardBr("Colossification", 50);
    aura.controller_index = 0; aura.owner_index = 0; aura.entered_this_turn = true;
    aura.aura_attached_to = birds;
    b.s.battlefield.push_back(aura);
    ResolveAuraEnterTapHost(b.s, static_cast<int>(b.s.battlefield.size()) - 1, /*respond_window=*/true);
    // Responded to with its own mana ability: still tappable for mana this phase, but no attacker.
    CHECK_FALSE(b.ByNum(birds)->tapped);
    CHECK(b.ByNum(birds)->etb_tap_pending);
    CHECK_FALSE(CanAttackFull(*b.ByNum(birds), b.s.battlefield, 0));
    ApplyPendingEtbTaps(b.s);   // the phase ends
    CHECK(b.ByNum(birds)->tapped);
}

TEST_CASE("Bruna: Arcanum Wings' swap -- simultaneous exchange, damage-max pick, no-op when illegal")
{
    BoardBr b;
    const int mother = b.Put("Mother of Runes");
    const int wings = b.Put("Arcanum Wings", false, false, mother);
    b.Hand("Wild Growth");           // cannot enchant a creature: never a legal half
    b.Hand("Unflinching Courage");
    b.Hand("Colossification");
    // In combat (host attacking) the Colossification tap is free -> +20 is the damage-max pick.
    const int k = AuraSwapPick(b.s, 0, wings, /*host_attacking=*/true);
    REQUIRE(k >= 0);
    CHECK(b.s.players[0].hand[static_cast<std::size_t>(k)].m_name.str() == "Colossification");
    // Main phase, ready attacker: Colossification's tap would cost this turn's attack, so the
    // damage ranking prefers Unflinching Courage (+2 now).
    const int km = AuraSwapPick(b.s, 0, wings, /*host_attacking=*/false);
    REQUIRE(km >= 0);
    CHECK(b.s.players[0].hand[static_cast<std::size_t>(km)].m_name.str() == "Unflinching Courage");
    REQUIRE(ApplyAuraSwap(b.s, 0, wings, k, /*respond_window=*/false));
    CHECK(b.ByNum(wings) == nullptr);   // Wings back in hand
    bool wings_in_hand = false;
    for (const Card& c : b.s.players[0].hand) { if (c.m_number == wings) { wings_in_hand = true; } }
    CHECK(wings_in_hand);
    CHECK(b.CombatPower(mother) == 21);
    // The Wild Growth half cannot complete -> nothing happens.
    int wg = -1;
    for (int i = 0; i < static_cast<int>(b.s.players[0].hand.size()); ++i)
    { if (b.s.players[0].hand[static_cast<std::size_t>(i)].m_name.str() == "Wild Growth") { wg = i; } }
    const int wings2 = b.Put("Arcanum Wings", false, false, mother);
    CHECK_FALSE(ApplyAuraSwap(b.s, 0, wings2, wg, /*respond_window=*/false));
    CHECK(b.ByNum(wings2) != nullptr);
}
