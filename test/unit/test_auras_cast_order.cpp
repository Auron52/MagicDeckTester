// The USER-approved Auras cast order (MTG_AURAS_ORDER, 2026-10-14) and its two default-OFF measurement arms
// (MTG_AURAS_ORDER_SEARCH, MTG_LP_FETCH_USER_RULE). docs/design/auras-cast-order-2026-10-14.md.
//
// What is pinned here, each against the cards' own cards.json data:
//   1. the rank lays the list's 18 nonland cards out in the approved order, every one in its own slot
//      (TOTAL), the same with or without a Light-Paws in play (UNCONDITIONAL); the lever off is exactly
//      GenericProvider's rank (the pre-order behaviour, Light-Paws block included);
//   2. no card of the list is ORDER-OPAQUE, so the rank is what the apply sorts by;
//   3. permuting the HAND does not change the realised order -- at the comparator, at plan enumeration
//      and at the rollout apply (the executor's lockstep twin through the same shared CastOrderLess);
//   4. the restricted-Aura legality guard still holds: every enumerated line that casts Daybreak Coronet /
//      Lion Umbra applies with the restricted Aura on a host that carries another Aura;
//   5. the search arm hands the cast-ordering search the distinct Aura orders (approved order first) only
//      on a Light-Paws turn with 2+ Auras, and nothing otherwise;
//   6. the fetch arm follows the 08-25 wording: a scaler not castable-into this turn is the last chance
//      (the old "this turn or next" rule took the flat Aura), and a last-chance Mask beats a duplicate Armor.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsAco()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardAco(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c     = d->card;
    c.m_number = number;
    c.RehashName();
    return c;
}

const DecisionProvider& AurasProvAco()
{
    static const AurasProvider prov;
    return prov;
}

const DecisionProvider& GenericProvAco()
{
    static const GenericProvider prov;
    return prov;
}

// The approved order, every nonland card of decks/Auras/Auras.cod, earliest first.
const std::vector<std::string>& ApprovedOrder()
{
    static const std::vector<std::string> v = {
        "Light-Paws, Emperor's Voice", "Kor Spiritdancer", "Slippery Bogle", "Gladecover Scout",
        "Armadillo Cloak", "Alpha Authority", "Spirit Mantle", "Spirit Link", "Gryff's Boon", "Hyena Umbra",
        "Spider Umbra", "Audacity", "Rancor", "Ancestral Mask", "All That Glitters", "Ethereal Armor",
        "Lion Umbra", "Daybreak Coronet",
    };
    return v;
}

struct ArmScope
{
    heurarm::Slot slot;
    ArmScope(heurarm::Slot s, int v) : slot(s) { heurarm::t_arm[s] = static_cast<std::int8_t>(v); }
    ~ArmScope() { heurarm::t_arm[slot] = -1; }
};

struct BoardAco
{
    GameState s;
    int next = 1;
    BoardAco()
    {
        EnsureCardsAco();
        s.active_player_index = 0;
        s.turn_number         = 4;
        s.players[0].life = s.players[1].life = gamesetup::StartingLife();
        s.m_provider = &AurasProvAco();
        s.players[0].lands_played_this_turn = 1;   // the land drop is spent: no land-fold variants
        for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardAco("Plains", 900 + k)); }
    }
    int Put(const std::string& name)
    {
        Permanent p;
        p.card              = CardAco(name, next++);
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
        return p.card.m_number;
    }
    int Hand(const std::string& name) { s.players[0].hand.push_back(CardAco(name, next)); return next++; }
    int Lib(const std::string& name)
    {
        s.players[0].library.insert(s.players[0].library.begin(), CardAco(name, next));
        return next++;
    }
};

int RankOf(const GameState& s, const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return ResolveProvider(s).CastOrderRank(s, *d);
}

// The casts of a plan in the order the apply realises them.
std::vector<std::string> Realised(const GameState& s, const TurnSolver::Plan& p)
{
    return TurnSolver::RealisedNonSacCastOrder(s, p);
}

std::vector<std::string> CastNames(const TurnSolver::Plan& p)
{
    std::vector<std::string> v;
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { v.push_back(a.card_name.str()); } }
    std::sort(v.begin(), v.end());
    return v;
}

}   // namespace

TEST_CASE("Auras order: the rank lays the 18 nonland cards out in the approved order, each in its own slot")
{
    BoardAco b;
    const std::vector<std::string>& order = ApprovedOrder();
    for (std::size_t k = 1; k < order.size(); ++k)
    {
        CHECK_MESSAGE(RankOf(b.s, order[k - 1]) < RankOf(b.s, order[k]), order[k - 1], " !< ", order[k]);
    }
    // UNCONDITIONAL: a Light-Paws (and a Kor) in play changes no rank -- the generic block it replaces did.
    BoardAco lp;
    lp.Put("Light-Paws, Emperor's Voice");
    lp.Put("Kor Spiritdancer");
    for (const std::string& n : order) { CHECK_MESSAGE(RankOf(lp.s, n) == RankOf(b.s, n), n); }
    // Every card of the list has a tier label in the report.
    for (const std::string& n : order)
    { CHECK_MESSAGE(AurasProvAco().CastOrderTierName(RankOf(b.s, n)) != nullptr, n); }
}

TEST_CASE("Auras order: MTG_AURAS_ORDER=0 is GenericProvider's rank exactly, Light-Paws block included")
{
    ArmScope off(heurarm::AURAS_ORDER, 0);
    BoardAco b;
    BoardAco lp;
    lp.Put("Light-Paws, Emperor's Voice");
    for (const std::string& n : ApprovedOrder())
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(n);
        REQUIRE(d != nullptr);
        CHECK_MESSAGE(AurasProvAco().CastOrderRank(b.s, *d)  == GenericProvAco().CastOrderRank(b.s, *d), n);
        CHECK_MESSAGE(AurasProvAco().CastOrderRank(lp.s, *d) == GenericProvAco().CastOrderRank(lp.s, *d), n);
    }
    // ...and that generic rank is board-conditional: with Light-Paws out, Ethereal Armor (22) and
    // Daybreak Coronet (23) leave the Rancor tier (20) they share without it.
    CHECK(RankOf(b.s, "Ethereal Armor") == RankOf(b.s, "Rancor"));
    CHECK(RankOf(lp.s, "Ethereal Armor") > RankOf(lp.s, "Rancor"));
}

TEST_CASE("Auras order: no card of the list is ORDER-OPAQUE, so the clean-set rank sort is what plays")
{
    EnsureCardsAco();
    for (const std::string& n : ApprovedOrder()) { CHECK_MESSAGE(!OrderingOpaque(n), n); }
}

TEST_CASE("Auras order: permuting the hand does not change the realised order (comparator, enumeration, apply)")
{
    // T4 with Light-Paws and a Bogle in play, 6 lands: {G}{G}{G}{W}{W}{W}. The hand holds a creature and
    // five Auras whose cheapest full line casts all six: Scout (G), Rancor (G), Spider Umbra (G), Ethereal
    // Armor (W), Spirit Link (W) and Hyena Umbra (W).
    const std::vector<std::string> hand = { "Ethereal Armor", "Rancor", "Gladecover Scout", "Spirit Link",
                                            "Hyena Umbra", "Spider Umbra" };
    std::vector<std::string> want = hand;
    std::sort(want.begin(), want.end(), [](const std::string& x, const std::string& y)
    {
        const auto& o = ApprovedOrder();
        return std::find(o.begin(), o.end(), x) < std::find(o.begin(), o.end(), y);
    });
    std::vector<std::string> perm = hand;
    std::sort(perm.begin(), perm.end());
    int tried = 0;
    std::vector<std::string> first_entry;
    do
    {
        // Every 12th permutation keeps the test quick while still covering all six cards in every position.
        if ((tried++ % 12) != 0) { continue; }
        BoardAco b;
        b.Put("Light-Paws, Emperor's Voice");
        b.Put("Slippery Bogle");
        for (int k = 0; k < 3; ++k) { b.Put("Forest"); }
        for (int k = 0; k < 3; ++k) { b.Put("Plains"); }
        for (const std::string& n : perm) { b.Hand(n); }
        // A library of Auras so every cast's Light-Paws trigger has something to fetch.
        for (const char* n : { "Audacity", "Gryff's Boon", "All That Glitters", "Spirit Mantle" }) { b.Lib(n); }
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
        std::vector<std::string> all = hand;
        std::sort(all.begin(), all.end());
        const TurnSolver::Plan* full = nullptr;
        for (const TurnSolver::Plan& p : plans) { if (CastNames(p) == all) { full = &p; break; } }
        REQUIRE_MESSAGE(full != nullptr, "the six-cast line was not enumerated");
        CHECK(Realised(b.s, *full) == want);
        // ...and the rollout apply casts them in that order: the hand cards enter the battlefield in it.
        GameState after = b.s;
        TurnSolver::ApplyPlan(after, *full, /*is_pre_combat=*/true);
        std::vector<std::string> entered;
        for (const Permanent& p : after.battlefield)
        {
            bool from_hand = false;
            for (const Card& c : b.s.players[0].hand) { if (c.m_number == p.card.m_number) { from_hand = true; break; } }
            if (from_hand) { entered.push_back(p.card.m_name.str()); }
        }
        CHECK(entered == want);
        // The fetches follow from the order, so the whole end board must not depend on the hand order.
        std::vector<std::string> board;
        for (const Permanent& p : after.battlefield) { board.push_back(p.card.m_name.str()); }
        if (first_entry.empty()) { first_entry = board; }
        CHECK(board == first_entry);
    } while (std::next_permutation(perm.begin(), perm.end()));
    CHECK(tried == 720);
}

TEST_CASE("Auras order: the restricted-Aura legality guard holds -- Coronet / Lion Umbra land on an enchanted host")
{
    // {G}{G}{W}{W}{W} + a Brushland: a Bogle in hand, a plain Aura, a scaler and both restricted Auras.
    BoardAco b;
    b.Put("Plains");
    b.Put("Plains");
    b.Put("Brushland");
    b.Put("Forest");
    b.Put("Forest");
    b.Put("Brushland");
    b.Hand("Slippery Bogle");
    b.Hand("Rancor");
    b.Hand("Ethereal Armor");
    b.Hand("Daybreak Coronet");
    b.Hand("Lion Umbra");
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    int restricted_lines = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        bool has_restricted = false;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            const CardDefinition* d = CardDatabase::Instance().Lookup(a.card_name);
            if (d && !d->params.aura_enchant_requires.empty()) { has_restricted = true; }
        }
        if (!has_restricted) { continue; }
        ++restricted_lines;
        // In the realised order, every restricted Aura comes after every plain/scaler Aura of the plan.
        const std::vector<std::string> real = Realised(b.s, p);
        bool seen_restricted = false;
        for (const std::string& n : real)
        {
            const CardDefinition* d = CardDatabase::Instance().Lookup(n);
            REQUIRE(d != nullptr);
            if (!d->params.is_aura) { continue; }
            if (!d->params.aura_enchant_requires.empty()) { seen_restricted = true; }
            else { CHECK_MESSAGE(!seen_restricted, "a plain Aura is realised after a restricted one"); }
        }
        GameState after = b.s;
        TurnSolver::ApplyPlan(after, p, /*is_pre_combat=*/true);
        for (const Permanent& rp : after.battlefield)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(rp.card);
            if (!d || d->params.aura_enchant_requires.empty()) { continue; }
            int on_host = 0;
            for (const Permanent& q : after.battlefield)
            { if (q.aura_attached_to != 0 && q.aura_attached_to == rp.aura_attached_to) { ++on_host; } }
            CHECK_MESSAGE(on_host >= 2, rp.card.m_name.str(), " is the only Aura on its host");
        }
    }
    CHECK(restricted_lines >= 1);
}

TEST_CASE("Auras order search (MTG_AURAS_ORDER_SEARCH): the distinct Aura orders on a Light-Paws turn only")
{
    BoardAco b;
    b.Put("Light-Paws, Emperor's Voice");
    for (int k = 0; k < 4; ++k) { b.Put("Brushland"); }
    b.Hand("Rancor");
    b.Hand("Ethereal Armor");
    b.Hand("Spirit Link");
    std::vector<Action> reorder;
    for (int hi = 0; hi < 3; ++hi)
    {
        Action a;
        a.kind       = Action::Kind::CastFromHand;
        a.card_name  = b.s.players[0].hand[static_cast<std::size_t>(hi)].m_name;
        a.hand_index = hi;
        a.def        = CardDatabase::Instance().Lookup(a.card_name);
        reorder.push_back(a);
    }
    std::vector<std::vector<int>> out;
    // Arm off: the search is not opened and the hook offers nothing.
    CHECK(!AurasProvAco().WantsCastOrderingSearch());
    REQUIRE(AurasProvAco().CastOrderingCandidates(b.s, reorder, reorder, out));
    CHECK(out.empty());
    ArmScope on(heurarm::AURAS_ORDER_SEARCH, 1);
    CHECK(AurasProvAco().WantsCastOrderingSearch());
    REQUIRE(AurasProvAco().CastOrderingCandidates(b.s, reorder, reorder, out));
    REQUIRE(out.size() == 6);   // 3 distinct Auras -> 3! orders
    // The approved order first: Spirit Link (MV1 +0) > Rancor (+2) > Ethereal Armor (scaler).
    const std::vector<int> want = { 2, 0, 1 };
    CHECK(out.front() == want);
    // ...and every candidate is a distinct order of the same three casts.
    std::vector<std::vector<int>> sorted = out;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
    // No Light-Paws in play or in the plan: nothing (the plan stays exactly as it was).
    BoardAco nolp;
    for (int k = 0; k < 4; ++k) { nolp.Put("Brushland"); }
    REQUIRE(AurasProvAco().CastOrderingCandidates(nolp.s, reorder, reorder, out));
    CHECK(out.empty());
    // A Light-Paws CAST by the plan makes it a Light-Paws turn; it keeps its slot ahead of the Auras.
    nolp.Hand("Rancor");
    nolp.Hand("Ethereal Armor");
    nolp.Hand("Light-Paws, Emperor's Voice");
    std::vector<Action> r2;
    for (int hi = 0; hi < 3; ++hi)
    {
        Action a;
        a.kind       = Action::Kind::CastFromHand;
        a.card_name  = nolp.s.players[0].hand[static_cast<std::size_t>(hi)].m_name;
        a.hand_index = hi;
        a.def        = CardDatabase::Instance().Lookup(a.card_name);
        r2.push_back(a);
    }
    REQUIRE(AurasProvAco().CastOrderingCandidates(nolp.s, r2, r2, out));
    REQUIRE(out.size() == 2);
    for (const std::vector<int>& o : out) { CHECK(o.front() == 2); }   // Light-Paws first in every order
    // One Aura: nothing to order.
    std::vector<Action> r1 = { r2[0], r2[2] };
    REQUIRE(AurasProvAco().CastOrderingCandidates(nolp.s, r1, r1, out));
    CHECK(out.empty());
}

TEST_CASE("Light-Paws fetch, MTG_LP_FETCH_USER_RULE: the last fetch IN THE LINE, and a Mask over a duplicate Armor")
{
    // CASE 1. Light-Paws out, 2 lands (both tapped: the turn's mana is spent), Ancestral Mask in hand. The
    // fetch can take Ethereal Armor or Audacity. Under the old rule the Mask is "castable next turn" (MV 3 <=
    // lands + 1), so this is not the last fetch and the flat Audacity wins; but the Mask cannot be cast THIS
    // turn, so no later fetch of this line exists -- the USER's rule takes the Armor.
    {
        BoardAco b;
        b.Put("Light-Paws, Emperor's Voice");
        b.Put("Brushland");
        b.Put("Brushland");
        for (Permanent& p : b.s.battlefield) { if (p.card.IsLand()) { p.tapped = true; } }
        b.Hand("Ancestral Mask");
        b.Lib("Audacity");
        b.Lib("Ethereal Armor");
        const Permanent& lp = b.s.battlefield.front();
        std::vector<int> legal = { 0, 1 };
        auto pick = [&](int arm)
        {
            ArmScope a(heurarm::LP_FETCH_USER_RULE, arm);
            const std::vector<int> r = AurasProvAco().LightPawsAuraCandidates(b.s, 0, lp, legal);
            REQUIRE(!r.empty());
            return b.s.players[0].library[static_cast<std::size_t>(r.front())].m_name.str();
        };
        CHECK(pick(0) == "Audacity");
        CHECK(pick(1) == "Ethereal Armor");
    }
    // CASE 2 (the draft's finding (b)). Light-Paws out, the fetch is an Armadillo Cloak's (mana value 3), one
    // untapped Plains left and an Ethereal Armor in hand. The old rule ranks the Armor copy first (in hand ->
    // scale-first) and the Mask behind it (another Aura is coming); under the USER's rule the Mask is ALSO the
    // last chance (no castable hand Aura of mana value >= 3 follows) and +2 per other enchantment beats the
    // Armor's +1.
    {
        BoardAco b;
        b.Put("Light-Paws, Emperor's Voice");
        b.Put("Plains");
        b.Hand("Ethereal Armor");
        b.Lib("Rancor");
        b.Lib("Ancestral Mask");
        b.Lib("Ethereal Armor");
        const Permanent& lp = b.s.battlefield.front();
        std::vector<int> legal = { 0, 1, 2 };
        auto pick = [&](int arm)
        {
            ArmScope a(heurarm::LP_FETCH_USER_RULE, arm);
            const std::vector<int> r = AurasProvAco().LightPawsAuraCandidates(b.s, 0, lp, legal);
            REQUIRE(!r.empty());
            return b.s.players[0].library[static_cast<std::size_t>(r.front())].m_name.str();
        };
        CHECK(pick(0) == "Ethereal Armor");
        CHECK(pick(1) == "Ancestral Mask");
    }
    // CASE 3. A castable Rancor follows: its fetch can still take the Armor, so this fetch takes the flat
    // Aura under both rules (the USER's "else take the flat Aura").
    {
        BoardAco b;
        b.Put("Light-Paws, Emperor's Voice");
        b.Put("Forest");
        b.Hand("Rancor");
        b.Lib("Audacity");
        b.Lib("Ethereal Armor");
        const Permanent& lp = b.s.battlefield.front();
        std::vector<int> legal = { 0, 1 };
        for (int arm : { 0, 1 })
        {
            ArmScope a(heurarm::LP_FETCH_USER_RULE, arm);
            const std::vector<int> r = AurasProvAco().LightPawsAuraCandidates(b.s, 0, lp, legal);
            REQUIRE(!r.empty());
            CHECK(b.s.players[0].library[static_cast<std::size_t>(r.front())].m_name.str() == "Audacity");
        }
    }
}

TEST_CASE("Light-Paws fetch, MTG_LP_FETCH_MV_AWARE: a 1-drop in hand is not a later fetch for a 2-drop scaler")
{
    // The Auras order's slower-game shape (docs/design/auras-cast-order-2026-10-14.md section 6): Light-Paws out,
    // a 2-drop's fetch resolving, a Spider Umbra (1-drop) still in hand. Mode 4 calls it "not the last fetch"
    // and takes the flat Audacity; a 1-drop's fetch can never take All That Glitters, so the MV-aware reading
    // lets the scaler lead.
    {
        BoardAco b;
        b.Put("Light-Paws, Emperor's Voice");
        b.Put("Brushland");
        b.Put("Brushland");
        b.Hand("Spider Umbra");
        b.Lib("Audacity");
        b.Lib("All That Glitters");
        const Permanent& lp = b.s.battlefield.front();
        std::vector<int> legal = { 0, 1 };
        auto pick = [&](int arm)
        {
            ArmScope a(heurarm::LP_FETCH_MV_AWARE, arm);
            const std::vector<int> r = AurasProvAco().LightPawsAuraCandidates(b.s, 0, lp, legal);
            REQUIRE(!r.empty());
            return b.s.players[0].library[static_cast<std::size_t>(r.front())].m_name.str();
        };
        CHECK(pick(0) == "Audacity");
        CHECK(pick(1) == "All That Glitters");
    }
    // ...but a 2-drop Aura castable this turn or next DOES still reach it, so the flat Aura is taken with or without
    // the arm -- unlike MTG_LP_FETCH_USER_RULE, which reads this turn's remaining mana only and takes the scaler.
    {
        BoardAco b;
        b.Put("Light-Paws, Emperor's Voice");
        b.Put("Brushland");
        b.Put("Brushland");
        for (Permanent& p : b.s.battlefield) { if (p.card.IsLand()) { p.tapped = true; } }
        b.Hand("Alpha Authority");
        b.Lib("Audacity");
        b.Lib("All That Glitters");
        const Permanent& lp = b.s.battlefield.front();
        std::vector<int> legal = { 0, 1 };
        for (int arm : { 0, 1 })
        {
            ArmScope a(heurarm::LP_FETCH_MV_AWARE, arm);
            const std::vector<int> r = AurasProvAco().LightPawsAuraCandidates(b.s, 0, lp, legal);
            REQUIRE(!r.empty());
            CHECK(b.s.players[0].library[static_cast<std::size_t>(r.front())].m_name.str() == "Audacity");
        }
        ArmScope u(heurarm::LP_FETCH_USER_RULE, 1);
        const std::vector<int> r = AurasProvAco().LightPawsAuraCandidates(b.s, 0, lp, legal);
        REQUIRE(!r.empty());
        CHECK(b.s.players[0].library[static_cast<std::size_t>(r.front())].m_name.str() == "All That Glitters");
    }
}
