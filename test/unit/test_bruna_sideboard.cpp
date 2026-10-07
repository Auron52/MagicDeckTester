// Unit cover for Bruna's 2026-10-06 SIDEBOARD change (USER list): Glittering Wish's pool is now eleven
// multicolored names, and four of them needed engine work --
//   * Steel of the Godhead: a COLOUR-CONDITIONAL Aura (aura_color_bonuses), incl. Bruna's gather;
//   * Troyan, Gutsy Explorer: BIG-SPELL-ONLY mana (mana_only_spell_min_mv / _or_x) at the payer, in the
//     subset enumerator and in the batch prepay, plus its "{U}, {T}: draw, then discard" loot;
//   * Auroral Procession / Reborn Hope: a REGROWTH = a tutor whose zone is the GRAVEYARD
//     (tutor_from_graveyard), targeted (not castable without a legal graveyard card).
// Every case is written so that it FAILS on the pre-change tree (the control) -- see the notes.
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
#include <cstdint>
#include <string>
#include <vector>

namespace
{

void EnsureCardsSb()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefSb(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardSb(const std::string& name, int number)
{
    Card c     = DefSb(name).card;
    c.m_number = number;
    c.RehashName();
    return c;
}

// A zone card in real play is a NAME-ONLY placeholder (empty masks) -- the graveyard / sideboard
// readers must go through the definition, so the zones are filled that way here.
Card PlaceholderSb(const std::string& name, int number)
{
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

const DecisionProvider& BrunaSb()
{
    static const BrunaProvider prov;
    return prov;
}

struct BoardSb
{
    GameState s;
    int next = 1;
    BoardSb()
    {
        EnsureCardsSb();
        s.active_player_index = 0;
        s.turn_number         = 5;
        s.players[0].life = s.players[1].life = gamesetup::StartingLife();
        s.m_provider = &BrunaSb();
        for (int k = 0; k < 20; ++k)
        { s.players[0].library.push_back(PlaceholderSb("Boseiju, Who Endures", 900 + k)); }
    }
    int Put(const std::string& name, bool sick = false, int attached_to = 0)
    {
        Permanent p;
        p.card              = CardSb(name, next++);
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = sick;
        p.aura_attached_to  = attached_to;
        s.battlefield.push_back(p);
        return p.card.m_number;
    }
    int Hand(const std::string& name) { s.players[0].hand.push_back(PlaceholderSb(name, next)); return next++; }
    int Grave(const std::string& name) { s.players[0].graveyard.push_back(PlaceholderSb(name, next)); return next++; }
    void Side(const std::string& name) { s.players[0].sideboard.push_back(PlaceholderSb(name, next++)); }
    const Permanent* ByNum(int num) const
    {
        for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
        return nullptr;
    }
    int Power(int num) const { return CombatPowerOf(*ByNum(num), s); }
};

std::vector<std::string> CastNamesSb(const TurnSolver::Plan& p)
{
    std::vector<std::string> v;
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand) { v.push_back(a.card_name.str()); } }
    return v;
}

bool PlanCastsSb(const TurnSolver::Plan& p, const std::string& name)
{
    for (const std::string& n : CastNamesSb(p)) { if (n == name) { return true; } }
    return false;
}

int CountOnBfSb(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

bool InZone(const std::vector<Card>& z, const std::string& name)
{
    for (const Card& c : z) { if (c.m_name.str() == name) { return true; } }
    return false;
}

const std::vector<std::string> kNewSideboard = {
    "Bruna, Light of Alabaster", "Indrik Umbra", "Almost Perfect", "Unflinching Courage",
    "Linvala, Shield of Sea Gate", "Troyan, Gutsy Explorer", "Detention Sphere", "Steel of the Godhead",
    "Vexing Shusher", "Auroral Procession", "Reborn Hope", "Reborn Hope" };

}   // namespace

// ---- Glittering Wish: the new pool ---------------------------------------------------------------
TEST_CASE("Bruna sideboard: Glittering Wish offers all ELEVEN multicolored names (hybrids included)")
{
    BoardSb b;
    for (const std::string& n : kNewSideboard) { b.Side(n); }
    b.Side("Mythic Proportions");   // mono-green CONTROL: must stay unfetchable
    const CardParams& wish = DefSb("Glittering Wish").params;
    const std::vector<std::string> c = GenericProvider().TutorCandidates(b.s, 0, wish);
    auto in = [&](const std::string& n) { return std::find(c.begin(), c.end(), n) != c.end(); };
    for (const std::string& n : kNewSideboard) { CHECK_MESSAGE(in(n), "wish cannot fetch ", n); }
    CHECK_FALSE(in("Mythic Proportions"));
    CHECK(c.size() == 11);   // Reborn Hope x2 is ONE choice
    // The hybrid pips make both the hybrid cards multicolored (CR 202.2b): {W/U} and {R/G}.
    CHECK(DefSb("Steel of the Godhead").card.IsMulticolored());
    CHECK(DefSb("Vexing Shusher").card.IsMulticolored());
    // ...and the provider searches every one of them (the axis width is a cap, never a prune here).
    CHECK(BrunaSb().TutorSearchWidth() >= 11);
}

// ---- Steel of the Godhead ------------------------------------------------------------------------
TEST_CASE("Bruna sideboard: Steel of the Godhead grants by the HOST's colour")
{
    BoardSb b;
    const int bruna  = b.Put("Bruna, Light of Alabaster");   // white AND blue
    const int mother = b.Put("Mother of Runes");             // white
    const int troyan = b.Put("Troyan, Gutsy Explorer");      // green-blue
    const int birds  = b.Put("Birds of Paradise");           // green
    b.Put("Steel of the Godhead", false, bruna);
    b.Put("Steel of the Godhead", false, mother);
    b.Put("Steel of the Godhead", false, troyan);
    b.Put("Steel of the Godhead", false, birds);
    CHECK(b.Power(bruna)  == 5 + 2);   // both conditions (control: +0 before the change)
    CHECK(b.Power(mother) == 1 + 1);
    CHECK(b.Power(troyan) == 1 + 1);
    CHECK(b.Power(birds)  == 0);
    CHECK(AuraBonusFor(*b.ByNum(bruna), b.s).second == 2);
    CHECK(CreatureHasLifelink(*b.ByNum(bruna), b.s));        // white -> lifelink
    CHECK(CreatureHasLifelink(*b.ByNum(mother), b.s));
    CHECK_FALSE(CreatureHasLifelink(*b.ByNum(troyan), b.s)); // blue only: no lifelink
    CHECK_FALSE(CreatureHasLifelink(*b.ByNum(birds), b.s));
}

TEST_CASE("Bruna sideboard: Bruna's gather always takes Steel; a gatherer missing a colour branches it")
{
    BoardSb b;
    const int bruna = b.Put("Bruna, Light of Alabaster");
    const int birds = b.Put("Birds of Paradise");
    using Cand = DecisionProvider::AuraGatherCand;
    const std::vector<Cand> cands = { { 1, 10, "Colossification" }, { 1, 11, "Steel of the Godhead" } };
    const GenericProvider gp;
    // Bruna is white and blue: Steel is worth its maximum on her -> one subset, both taken.
    std::vector<std::vector<int>> r = gp.BrunaGatherCandidates(b.s, 0, *b.ByNum(bruna), cands);
    REQUIRE(r.size() == 1);
    CHECK(r[0] == std::vector<int>{ 0, 1 });
    // A (hypothetical) green gatherer would gain nothing from Steel: host-dependent -> take/skip.
    // CONTROL: the pre-change collapse read only aura_set_base_power and returned ONE subset here.
    r = gp.BrunaGatherCandidates(b.s, 0, *b.ByNum(birds), cands);
    REQUIRE(r.size() == 2);
    CHECK(r[0] == std::vector<int>{ 0, 1 });
    CHECK(r[1] == std::vector<int>{ 0 });
}

TEST_CASE("Bruna sideboard: the attack trigger puts Steel onto Bruna from the hand (+2/+2)")
{
    BoardSb b;
    const int bruna = b.Put("Bruna, Light of Alabaster");
    b.s.players[0].hand.push_back(CardSb("Steel of the Godhead", 77));
    std::vector<int> atk;
    for (int i = 0; i < static_cast<int>(b.s.battlefield.size()); ++i)
    { if (b.s.battlefield[static_cast<std::size_t>(i)].card.m_number == bruna) { atk.push_back(i); } }
    FireAttackGatherAuras(b.s, 0, atk);
    REQUIRE(b.ByNum(77) != nullptr);
    CHECK(b.ByNum(77)->aura_attached_to == bruna);
    CHECK(b.Power(bruna) == 7);
}

// ---- Troyan, Gutsy Explorer: big-spell-only mana -------------------------------------------------
TEST_CASE("Bruna sideboard: Troyan's {G}{U} pays only a spell of mana value 5+ (or with {X})")
{
    ManaCost gu; gu.green = 1; gu.blue = 1;
    {
        BoardSb b;
        b.Put("Troyan, Gutsy Explorer");
        SpellSubtypePayScope scope(&DefSb("Bruna, Light of Alabaster").card);   // MV 6
        CHECK(TapForCostShared(b.s, gu, /*for_creature=*/true, nullptr, true));
    }
    {
        BoardSb b;
        b.Put("Troyan, Gutsy Explorer");
        SpellSubtypePayScope scope(&DefSb("Linvala, Shield of Sea Gate").card);   // MV 3
        CHECK_FALSE(TapForCostShared(b.s, gu, /*for_creature=*/true, nullptr, true));
    }
    {
        BoardSb b;   // no paying spell = an ACTIVATED ability (an equip, a swap, the loot)
        b.Put("Troyan, Gutsy Explorer");
        CHECK_FALSE(TapForCostShared(b.s, gu, /*for_creature=*/false, nullptr, true));
    }
    {
        BoardSb b;   // a noncreature 5+ spell qualifies too (the test is MV, not creature-ness)
        b.Put("Troyan, Gutsy Explorer");
        SpellSubtypePayScope scope(&DefSb("Colossification").card);   // MV 7, an Aura
        CHECK(TapForCostShared(b.s, gu, /*for_creature=*/false, nullptr, true));
    }
}

TEST_CASE("Bruna sideboard: a unit Troyan over-produces floats as BIG-SPELL-ONLY mana, never general float")
{
    ManaCost one; one.generic = 1;
    {
        BoardSb b;
        b.Put("Troyan, Gutsy Explorer");
        {
            SpellSubtypePayScope scope(&DefSb("Bruna, Light of Alabaster").card);
            REQUIRE(TapForCostShared(b.s, one, /*for_creature=*/true, nullptr, true));
        }
        CHECK(b.s.floating_mana.Total() == 0);
        // 2026-10-06: the surplus unit is no longer DROPPED -- it floats, restricted (control: the
        // pre-fix tree held 0 here, which made a second big spell this phase unpayable).
        CHECK(b.s.floating_bigspell_mana.Total() == 1);
        {
            // A small spell may not spend it: the payment fails and the reserve is returned intact.
            SpellSubtypePayScope scope(&DefSb("Linvala, Shield of Sea Gate").card);   // MV 3
            CHECK_FALSE(TapForCostShared(b.s, one, /*for_creature=*/true, nullptr, true));
            CHECK(b.s.floating_bigspell_mana.Total() == 1);
        }
        CHECK_FALSE(TapForCostShared(b.s, one, /*for_creature=*/false, nullptr, true));   // an ability
        CHECK(b.s.floating_bigspell_mana.Total() == 1);
        {
            // A second QUALIFYING spell this phase spends it (MV 7 noncreature Aura).
            SpellSubtypePayScope scope(&DefSb("Colossification").card);
            CHECK(TapForCostShared(b.s, one, /*for_creature=*/false, nullptr, true));
            CHECK(b.s.floating_bigspell_mana.Total() == 0);
        }
    }
    {
        BoardSb b;   // CONTROL shape: an unrestricted {G}{U} bundle keeps its surplus
        b.Put("Simic Growth Chamber");
        REQUIRE(TapForCostShared(b.s, one, /*for_creature=*/true, nullptr, true));
        CHECK(b.s.floating_mana.Total() == 1);
    }
}

TEST_CASE("Bruna sideboard: the enumerator never offers a small cast paid with Troyan's mana")
{
    BoardSb b;
    b.s.players[0].lands_played_this_turn = 1;
    b.Put("Troyan, Gutsy Explorer");
    b.Put("Plains");
    b.Put("Island");
    b.Hand("Linvala, Shield of Sea Gate");   // {1}{W}{U}: Plains + Island + ONE more -- only Troyan
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    for (const TurnSolver::Plan& p : plans)
    { CHECK_FALSE_MESSAGE(PlanCastsSb(p, "Linvala, Shield of Sea Gate"), "Linvala offered off Troyan's mana"); }
}

TEST_CASE("Bruna sideboard: Troyan funds Bruna, and a mixed turn pays the small cast from lands")
{
    BoardSb b;
    b.s.players[0].lands_played_this_turn = 1;
    b.Put("Troyan, Gutsy Explorer");
    b.Put("Plains");
    b.Put("Plains");
    b.Put("Plains");
    b.Put("Island");
    b.Put("Forest");
    b.Hand("Bruna, Light of Alabaster");   // {3}{W}{W}{U}
    b.Hand("Mother of Runes");             // {W}
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* both = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (PlanCastsSb(p, "Bruna, Light of Alabaster") && PlanCastsSb(p, "Mother of Runes"))
        { both = &p; break; }
    }
    REQUIRE_MESSAGE(both != nullptr, "the Bruna + Mother line (7 mana: 5 lands + Troyan) was not offered");
    GameState after = b.s;
    TurnSolver::ApplyPlan(after, *both, /*is_pre_combat=*/true);
    // Five lands make W W W U G: Mother's {W} must come from a Plains and Bruna's {3}{W}{W}{U} from
    // the rest PLUS Troyan's {G}{U}. The joint batch solve cannot let Troyan pay Mother, so it pays
    // in two stages (Mother first without Troyan, Bruna after with it) -- before that staging the
    // per-cast payer tapped all three Plains for Bruna and Mother was dropped.
    CHECK(CountOnBfSb(after, "Bruna, Light of Alabaster") == 1);
    CHECK(CountOnBfSb(after, "Mother of Runes") == 1);
}

TEST_CASE("Bruna sideboard: Troyan's loot draws, then discards -- and cannot pay its own {U}")
{
    {
        BoardSb b;
        const int troyan = b.Put("Troyan, Gutsy Explorer");
        b.Hand("Forest");
        b.Hand("Colossification");
        ApplyPermAbility(b.s, 0, troyan, PermAbilityMode::TapDraw);
        CHECK(b.s.players[0].hand.size() == 2);        // +1 draw, -1 discard (control: 3)
        CHECK(b.s.players[0].graveyard.size() == 1);
    }
    {
        BoardSb b;   // Troyan alone: its own mana may not pay the {U} (an ability), so no loot plan
        b.Put("Troyan, Gutsy Explorer");
        bool loot = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(b.s, true))
        {
            for (const Action& a : p.actions)
            { if (a.ability_mode == Action::AbilityMode::TapDraw) { loot = true; } }
        }
        CHECK_FALSE(loot);
        b.Put("Island");   // ...an Island pays it
        loot = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(b.s, true))
        {
            for (const Action& a : p.actions)
            { if (a.ability_mode == Action::AbilityMode::TapDraw) { loot = true; } }
        }
        CHECK(loot);
    }
}

// ---- Troyan's floated big-spell-only mana in the ENUMERATOR (2026-10-06) -------------------------
// A phase that already holds Troyan's floated {G}{U} (a surplus from an earlier qualifying cast):
// the enumerator must count it for a qualifying spell, and never for a small one.
TEST_CASE("Bruna sideboard: the enumerator counts floated big-spell-only mana for a 5+ spell only")
{
    {
        BoardSb b;
        b.s.players[0].lands_played_this_turn = 1;
        b.Put("Plains");
        b.Put("Plains");
        b.Put("Island");
        b.Put("Forest");
        b.s.floating_bigspell_mana.green = 1;
        b.s.floating_bigspell_mana.blue  = 1;
        b.s.floating_bigspell_pp = &DefSb("Troyan, Gutsy Explorer").params;
        b.Hand("Bruna, Light of Alabaster");   // {3}{W}{W}{U}: four lands + the two floated units
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
        const TurnSolver::Plan* bruna = nullptr;
        for (const TurnSolver::Plan& p : plans)
        { if (PlanCastsSb(p, "Bruna, Light of Alabaster")) { bruna = &p; break; } }
        // CONTROL: before the fix nothing could hold this float, and the plan was never offered.
        REQUIRE_MESSAGE(bruna != nullptr, "Bruna (6) not offered off 4 lands + 2 floated big-spell mana");
        GameState after = b.s;
        TurnSolver::ApplyPlan(after, *bruna, /*is_pre_combat=*/true);
        CHECK(CountOnBfSb(after, "Bruna, Light of Alabaster") == 1);
        CHECK(after.floating_bigspell_mana.Total() == 0);
    }
    {
        BoardSb b;   // the same float can NOT fund a small spell
        b.s.players[0].lands_played_this_turn = 1;
        b.Put("Plains");
        b.Put("Island");
        b.s.floating_bigspell_mana.green = 1;
        b.s.floating_bigspell_mana.blue  = 1;
        b.s.floating_bigspell_pp = &DefSb("Troyan, Gutsy Explorer").params;
        b.Hand("Linvala, Shield of Sea Gate");   // {1}{W}{U}: two lands + ONE more -- only the float
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true))
        { CHECK_FALSE_MESSAGE(PlanCastsSb(p, "Linvala, Shield of Sea Gate"), "Linvala offered off floated big-spell mana"); }
    }
}

// ---- HASTE FROM ANY SOURCE LIFTS SUMMONING SICKNESS FOR {T} ABILITIES (CR 302.6 / 702.10) ----------
// Lightning Greaves on a Troyan that entered this turn: both of its {T} abilities -- the loot (a
// PermAbility mode) and the mana -- are legal. Before 2026-10-06 the PermAbility gates (enumeration,
// the apply-side PermAbilitySourceLive, the d0 table) read Permanent::CanTap(), which sees only the
// PRINTED keyword, so the Greaves'd loot was inexpressible. The no-Greaves arm is the control.
static bool PlansLoot(const GameState& s)
{
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions)
        { if (a.ability_mode == Action::AbilityMode::TapDraw) { return true; } }
    }
    return false;
}
TEST_CASE("Haste: Lightning Greaves lets a summoning-sick creature use its {T} abilities")
{
    for (const bool greaves : { false, true })
    {
        CAPTURE(greaves);
        BoardSb b;
        b.s.players[0].lands_played_this_turn = 1;
        const int troyan = b.Put("Troyan, Gutsy Explorer", /*sick=*/true);
        b.Put("Island");
        if (greaves)
        {
            const int g = b.Put("Lightning Greaves");
            for (Permanent& p : b.s.battlefield) { if (p.card.m_number == g) { p.equipped_to = troyan; } }
        }
        b.Hand("Forest");
        CHECK(PermAbilitySourceLive(b.s, 0, troyan, PermAbilityMode::TapDraw) == greaves);
        CHECK(PlansLoot(b.s) == greaves);
        {
            ManaCost gu; gu.green = 1; gu.blue = 1;
            GameState s2 = b.s;
            for (Permanent& p : s2.battlefield) { if (p.card.m_name.str() == "Island") { p.tapped = true; } }
            SpellSubtypePayScope scope(&DefSb("Bruna, Light of Alabaster").card);
            CHECK(TapForCostShared(s2, gu, /*for_creature=*/true, nullptr, true) == greaves);
        }
    }
}

// ---- Auroral Procession / Reborn Hope: regrowth as a graveyard-zone tutor ------------------------
TEST_CASE("Bruna sideboard: Reborn Hope targets only MULTICOLORED graveyard cards; Procession any card")
{
    BoardSb b;
    b.Grave("Forest");
    b.Grave("Indrik Umbra");
    b.Grave("Mother of Runes");
    b.Grave("Bruna, Light of Alabaster");
    const std::vector<std::string> hope =
        GenericProvider().TutorCandidates(b.s, 0, DefSb("Reborn Hope").params);
    CHECK(hope == std::vector<std::string>{ "Indrik Umbra", "Bruna, Light of Alabaster" });
    const std::vector<std::string> proc =
        GenericProvider().TutorCandidates(b.s, 0, DefSb("Auroral Procession").params);
    CHECK(proc.size() == 4);   // CONTROL: before tutor_from_graveyard this read the LIBRARY (Boseiju)
}

TEST_CASE("Bruna sideboard: a regrowth returns from the graveyard, shuffles nothing, needs a target")
{
    BoardSb b;
    b.Grave("Forest");
    b.Grave("Indrik Umbra");
    const std::string top_before = b.s.players[0].library.front().m_name.str();
    const uint64_t searches_before = b.s.search_count;
    PerformTutor(b.s, 0, DefSb("Reborn Hope").params, "Indrik Umbra", "Reborn Hope");
    CHECK(InZone(b.s.players[0].hand, "Indrik Umbra"));
    CHECK_FALSE(InZone(b.s.players[0].graveyard, "Indrik Umbra"));
    CHECK(b.s.players[0].library.front().m_name.str() == top_before);
    CHECK(b.s.search_count == searches_before);   // no CR 701.19c shuffle: no library was searched
    // Targeted (CR 601.2c): no multicolored card left -> Reborn Hope has no legal target.
    CHECK_FALSE(HasGraveyardTutorTarget(b.s, 0, DefSb("Reborn Hope").params));
    CHECK(HasGraveyardTutorTarget(b.s, 0, DefSb("Auroral Procession").params));   // the Forest
    b.s.players[0].graveyard.clear();
    CHECK_FALSE(HasGraveyardTutorTarget(b.s, 0, DefSb("Auroral Procession").params));
}

TEST_CASE("Bruna sideboard: a regrowth is never offered without a legal graveyard target")
{
    BoardSb b;
    b.s.players[0].lands_played_this_turn = 1;
    b.Put("Forest");
    b.Put("Plains");
    b.Put("Island");
    b.Hand("Reborn Hope");
    b.Hand("Auroral Procession");
    b.Grave("Forest");   // a legal Procession target, NOT a Reborn Hope one
    bool hope = false, proc = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(b.s, true))
    {
        hope = hope || PlanCastsSb(p, "Reborn Hope");
        proc = proc || PlanCastsSb(p, "Auroral Procession");
    }
    CHECK_FALSE(hope);   // CONTROL: a library tutor may whiff, so the cast used to be offered
    CHECK(proc);
    // With a multicolored card there, Reborn Hope is cast and the card comes back.
    b.Grave("Unflinching Courage");
    const TurnSolver::Plan* line = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        if (PlanCastsSb(p, "Reborn Hope") && !PlanCastsSb(p, "Auroral Procession")) { line = &p; break; }
    }
    REQUIRE(line != nullptr);
    GameState after = b.s;
    TurnSolver::ApplyPlan(after, *line, /*is_pre_combat=*/true);
    CHECK(InZone(after.players[0].hand, "Unflinching Courage"));
    CHECK(InZone(after.players[0].graveyard, "Reborn Hope"));
}

TEST_CASE("Bruna sideboard: the discard policy sheds a regrowth before a real tutor (overflow 25 < 70)")
{
    BoardSb b;
    b.Put("Forest");
    b.Put("Forest");
    b.Put("Bruna, Light of Alabaster");
    b.Hand("Glittering Wish");
    const int hope = b.Hand("Reborn Hope");
    b.Hand("Lightning Greaves");
    const std::vector<int> shed = BrunaProvider().CleanupDiscardCandidates(b.s, nullptr);
    REQUIRE_FALSE(shed.empty());
    // CONTROL: before the change Reborn Hope sat in the tutor tier at 70, beside the Wish.
    CHECK(b.s.players[0].hand[static_cast<std::size_t>(shed.front())].m_number == hope);
}

// ---- Glittering Wish CANDIDATE RULE (USER spec 2026-10-06) ----------------------------------------
// BrunaProvider::TutorCandidates narrows the 11-name wish pool; MTG_WISH_FULL_WIDTH (heurarm slot) is
// the full-width control. Each case pairs the narrowed answer with the control arm's full list.
namespace
{
struct WishFullWidthArm
{
    std::int8_t prev;
    explicit WishFullWidthArm(bool on) : prev(heurarm::t_arm[heurarm::WISH_FULL_WIDTH])
    { heurarm::t_arm[heurarm::WISH_FULL_WIDTH] = on ? 1 : 0; }
    ~WishFullWidthArm() { heurarm::t_arm[heurarm::WISH_FULL_WIDTH] = prev; }
};

std::vector<std::string> WishCands(const BoardSb& b)
{
    return BrunaSb().TutorCandidates(b.s, 0, DefSb("Glittering Wish").params);
}

bool Has(const std::vector<std::string>& v, const std::string& n)
{
    return std::find(v.begin(), v.end(), n) != v.end();
}

// The USER's sideboard, an early board: two lands + a land in hand, Mother of Runes out.
BoardSb EarlyWishBoard()
{
    BoardSb b;
    for (const std::string& n : kNewSideboard) { b.Side(n); }
    b.Put("Forest");
    b.Put("Plains");
    b.Put("Mother of Runes");
    b.Hand("Forest");
    b.Hand("Glittering Wish");
    return b;
}
}   // namespace

TEST_CASE("Glittering Wish rule: a narrowed set; the full-width control arm offers all eleven")
{
    BoardSb b = EarlyWishBoard();
    WishFullWidthArm off(false);
    const std::vector<std::string> c = WishCands(b);
    CHECK(c.size() >= 1);
    CHECK(c.size() <= 6);
    for (const std::string& n : { "Detention Sphere", "Auroral Procession", "Reborn Hope", "Indrik Umbra" })
    { CHECK_MESSAGE(!Has(c, n), "excluded name offered: ", n); }
    // CONTROL: the full-width arm is GenericProvider's list verbatim (sideboard order, 11 names).
    WishFullWidthArm on(true);
    const std::vector<std::string> full = WishCands(b);
    CHECK(full.size() == 11);
    CHECK(full == GenericProvider().TutorCandidates(b.s, 0, DefSb("Glittering Wish").params));
}

TEST_CASE("Glittering Wish rule: never a duplicate Bruna (hand or battlefield)")
{
    WishFullWidthArm off(false);
    {
        BoardSb b = EarlyWishBoard();
        const std::vector<std::string> c = WishCands(b);
        REQUIRE_FALSE(c.empty());
        CHECK(c.front() == "Bruna, Light of Alabaster");   // no Bruna anywhere: she leads
    }
    {
        BoardSb b = EarlyWishBoard();
        b.Hand("Bruna, Light of Alabaster");
        CHECK_FALSE(Has(WishCands(b), "Bruna, Light of Alabaster"));
    }
    {
        BoardSb b = EarlyWishBoard();
        b.Put("Bruna, Light of Alabaster");
        CHECK_FALSE(Has(WishCands(b), "Bruna, Light of Alabaster"));
    }
}

TEST_CASE("Glittering Wish rule: the highest-power Aura is the primary (Almost Perfect, then Umbra)")
{
    WishFullWidthArm off(false);
    BoardSb b = EarlyWishBoard();
    std::vector<std::string> c = WishCands(b);
    CHECK(Has(c, "Almost Perfect"));        // Mother 1/1 -> 9/10: +8 > Umbra's +4
    CHECK_FALSE(Has(c, "Indrik Umbra"));
    // Almost Perfect already fetched (gone from the sideboard): Indrik Umbra is the top-power Aura.
    auto& sb = b.s.players[0].sideboard;
    sb.erase(std::remove_if(sb.begin(), sb.end(),
                            [](const Card& x) { return x.m_name.str() == "Almost Perfect"; }), sb.end());
    c = WishCands(b);
    CHECK(Has(c, "Indrik Umbra"));
}

TEST_CASE("Glittering Wish rule: a cheap Aura is NEVER the sole Aura pick, and only without a cheat path")
{
    WishFullWidthArm off(false);
    // No cheat path (no Bruna, no Arcanum Wings): the cheap Aura may join as a SECOND Aura.
    {
        BoardSb b = EarlyWishBoard();
        const std::vector<std::string> c = WishCands(b);
        CHECK(Has(c, "Almost Perfect"));
        CHECK(Has(c, "Unflinching Courage"));
        CHECK_FALSE(Has(c, "Steel of the Godhead"));   // at most ONE cheap Aura (Courage +2 vs Steel's +1 on white Mother)
    }
    // Cheat path -- Bruna on the battlefield: only the highest-power Aura.
    {
        BoardSb b = EarlyWishBoard();
        b.Put("Bruna, Light of Alabaster");
        const std::vector<std::string> c = WishCands(b);
        CHECK(Has(c, "Almost Perfect"));
        CHECK_FALSE(Has(c, "Unflinching Courage"));
        CHECK_FALSE(Has(c, "Steel of the Godhead"));
    }
    // Cheat path -- Arcanum Wings on the battlefield (with a blue source for the {2}{U} swap).
    {
        BoardSb b = EarlyWishBoard();
        b.Put("Island");
        int mother = 0;
        for (const Permanent& q : b.s.battlefield)
        { if (q.card.m_name.str() == "Mother of Runes") { mother = q.card.m_number; } }
        REQUIRE(mother > 0);
        b.Put("Arcanum Wings", false, mother);
        const std::vector<std::string> c = WishCands(b);
        CHECK(Has(c, "Almost Perfect"));
        CHECK_FALSE(Has(c, "Unflinching Courage"));
    }
    // Only cheap Auras left in the sideboard: neither is offered (never the sole Aura pick).
    {
        BoardSb b = EarlyWishBoard();
        auto& sb = b.s.players[0].sideboard;
        sb.erase(std::remove_if(sb.begin(), sb.end(), [](const Card& x)
                 { return x.m_name.str() == "Almost Perfect" || x.m_name.str() == "Indrik Umbra"; }), sb.end());
        const std::vector<std::string> c = WishCands(b);
        CHECK_FALSE(Has(c, "Unflinching Courage"));
        CHECK_FALSE(Has(c, "Steel of the Godhead"));
        CHECK(Has(c, "Bruna, Light of Alabaster"));
    }
}

TEST_CASE("Glittering Wish rule: Troyan only when mana is short")
{
    WishFullWidthArm off(false);
    // Short: next turn = 2 lands + a land drop = 3 < 6 (Bruna / Almost Perfect).
    {
        BoardSb b = EarlyWishBoard();
        CHECK(Has(WishCands(b), "Troyan, Gutsy Explorer"));
    }
    // Not short: 6 lands + the drop = 7 >= 6, nothing pricier in hand.
    {
        BoardSb b = EarlyWishBoard();
        for (int k = 0; k < 4; ++k) { b.Put("Forest"); }
        CHECK_FALSE(Has(WishCands(b), "Troyan, Gutsy Explorer"));
        // ...but an Eldrazi Conscription {8} in hand makes it short again (7 < 8).
        b.Hand("Eldrazi Conscription");
        CHECK(Has(WishCands(b), "Troyan, Gutsy Explorer"));
    }
}

TEST_CASE("Glittering Wish rule: other tutors keep GenericProvider's list (Open the Armory)")
{
    WishFullWidthArm off(false);
    BoardSb b = EarlyWishBoard();
    std::vector<Card> lib;
    for (const std::string& n : { "Colossification", "Lightning Greaves", "Arcanum Wings", "Wild Growth" })
    { lib.push_back(PlaceholderSb(n, b.next++)); }
    b.s.players[0].library.assign(lib.begin(), lib.end());
    const CardParams& armory = DefSb("Open the Armory").params;
    CHECK(BrunaSb().TutorCandidates(b.s, 0, armory) == GenericProvider().TutorCandidates(b.s, 0, armory));
    CHECK_FALSE(BrunaSb().TutorMarksSuggested(armory));
    CHECK(BrunaSb().TutorMarksSuggested(DefSb("Glittering Wish").params));
}

TEST_CASE("Glittering Wish rule: the BODY is Linvala only, when nothing can carry the Auras and Bruna cannot come down next turn")
{
    WishFullWidthArm off(false);
    // USER 2026-10-07: "I would leave out Vexing Shusher" -- never offered.
    // No creature at all, Colossification in hand, two lands (Bruna is six mana away): Linvala joins --
    // and the cheap Aura does NOT (nothing to wear it).
    {
        BoardSb b;
        for (const std::string& n : kNewSideboard) { b.Side(n); }
        b.Put("Forest");
        b.Put("Forest");
        b.Hand("Colossification");
        const std::vector<std::string> c = WishCands(b);
        CHECK(Has(c, "Linvala, Shield of Sea Gate"));
        CHECK_FALSE(Has(c, "Vexing Shusher"));
        CHECK_FALSE(Has(c, "Unflinching Courage"));
    }
    // Only mana dorks out (they tap for mana, they do not carry): still Linvala.
    {
        BoardSb b;
        for (const std::string& n : kNewSideboard) { b.Side(n); }
        b.Put("Forest");
        b.Put("Avacyn's Pilgrim");
        CHECK(Has(WishCands(b), "Linvala, Shield of Sea Gate"));
        CHECK_FALSE(Has(WishCands(b), "Vexing Shusher"));
    }
    // A non-dork creature on the battlefield (Mother of Runes): no body.
    {
        BoardSb b = EarlyWishBoard();
        CHECK_FALSE(Has(WishCands(b), "Linvala, Shield of Sea Gate"));
        CHECK_FALSE(Has(WishCands(b), "Vexing Shusher"));
    }
    // USER 2026-10-07: "if I have the choice to cast bruna or Linvala next turn then we should definitely
    // cast Bruna." Five lands out + a land drop = six mana with W and U next turn: Bruna (fetchable by this
    // wish) can come down -> no Linvala.
    {
        BoardSb b;
        for (const std::string& n : kNewSideboard) { b.Side(n); }
        b.Put("Plains");
        b.Put("Plains");
        b.Put("Island");
        b.Put("Forest");
        b.Put("Forest");
        b.Hand("Forest");
        const std::vector<std::string> c = WishCands(b);
        CHECK(Has(c, "Bruna, Light of Alabaster"));
        CHECK_FALSE(Has(c, "Linvala, Shield of Sea Gate"));
    }
    // ...the same with Bruna IN HAND (not fetchable: a copy is owned) -> no Linvala either.
    {
        BoardSb b;
        for (const std::string& n : kNewSideboard) { b.Side(n); }
        b.Put("Plains");
        b.Put("Plains");
        b.Put("Island");
        b.Put("Forest");
        b.Put("Forest");
        b.Hand("Forest");
        b.Hand("Bruna, Light of Alabaster");
        CHECK_FALSE(Has(WishCands(b), "Linvala, Shield of Sea Gate"));
    }
    // Bruna NOT castable next turn (no blue source): no body, no Bruna in time -> Linvala offered.
    {
        BoardSb b;
        for (const std::string& n : kNewSideboard) { b.Side(n); }
        b.Put("Plains");
        b.Put("Plains");
        b.Put("Plains");
        b.Put("Forest");
        b.Put("Forest");
        b.Hand("Forest");
        CHECK(Has(WishCands(b), "Linvala, Shield of Sea Gate"));
    }
}

TEST_CASE("Glittering Wish rule: TROYAN only for real acceleration")
{
    WishFullWidthArm off(false);
    // Two lands, Mythic Proportions in hand on Mother of Runes: a 7-drop with three mana next turn -> Troyan.
    {
        BoardSb b = EarlyWishBoard();
        b.Hand("Mythic Proportions");
        CHECK(Has(WishCands(b), "Troyan, Gutsy Explorer"));
    }
    // Bruna on the battlefield gathers the Auras for free, and nothing else in hand costs 5+ -> no Troyan.
    {
        BoardSb b = EarlyWishBoard();
        b.Hand("Mythic Proportions");
        b.Put("Bruna, Light of Alabaster");
        CHECK_FALSE(Has(WishCands(b), "Troyan, Gutsy Explorer"));
    }
    // Plenty of mana (seven sources next turn) -> no Troyan.
    {
        BoardSb b = EarlyWishBoard();
        b.Hand("Mythic Proportions");
        for (const char* l : { "Forest", "Forest", "Plains", "Island" }) { b.Put(l); }
        CHECK_FALSE(Has(WishCands(b), "Troyan, Gutsy Explorer"));
    }
}

TEST_CASE("Glittering Wish rule: ENOUGH FOR LETHAL in hand -> no sideboard Aura; short of lethal -> the Aura is offered")
{
    WishFullWidthArm off(false);
    // Bruna on the battlefield (5 power) with Colossification (+20) in hand: her attack trigger gathers it
    // for free -> 25 >= 20. Another Aura is not the missing piece: no primary, no cheap Aura.
    {
        BoardSb b = EarlyWishBoard();
        b.Put("Bruna, Light of Alabaster");
        b.Hand("Colossification");
        const std::vector<std::string> c = WishCands(b);
        CHECK_FALSE(Has(c, "Unflinching Courage"));
        CHECK_FALSE(Has(c, "Troyan, Gutsy Explorer"));
        CHECK_FALSE(Has(c, "Linvala, Shield of Sea Gate"));
        // Nothing is missing (Bruna out, a body out, no mana need): the fetch cannot matter, so ONE name --
        // the primary as the default -- never the whole pool.
        REQUIRE(c.size() == 1);
        CHECK(c.front() == "Almost Perfect");
    }
    // Same board, only Unflinching Courage in hand: 1 (Mother) + 5 + 2 = 8 < 20 -> the primary is offered.
    {
        BoardSb b = EarlyWishBoard();
        b.Put("Bruna, Light of Alabaster");
        b.Hand("Unflinching Courage");
        CHECK(Has(WishCands(b), "Almost Perfect"));
    }
    // Mother of Runes out, Eldrazi Conscription (+10) in hand, but only three mana next turn: it cannot be
    // cast on Mother, and Bruna cannot come down next turn -> NOT lethal -> the primary is offered.
    {
        BoardSb b = EarlyWishBoard();
        b.Hand("Eldrazi Conscription");
        CHECK(Has(WishCands(b), "Almost Perfect"));
    }
    // 2HG: the opponents share 30. Bruna + Colossification = 25 + Mother 1 = 26 < 30 -> the primary stays.
    {
        BoardSb b = EarlyWishBoard();
        b.Put("Bruna, Light of Alabaster");
        b.Hand("Colossification");
        b.s.players[1].life = 30;
        CHECK(Has(WishCands(b), "Almost Perfect"));
    }
    // A reachable host: Bruna fetchable and castable next turn (six mana, W+U), Colossification + Conscription
    // in hand -> 5 + 30 >= 20 on her -> no sideboard Aura; Bruna is what is missing.
    {
        BoardSb b;
        for (const std::string& n : kNewSideboard) { b.Side(n); }
        for (const char* l : { "Plains", "Plains", "Island", "Forest", "Forest" }) { b.Put(l); }
        b.Hand("Forest");
        b.Hand("Colossification");
        b.Hand("Eldrazi Conscription");
        const std::vector<std::string> c = WishCands(b);
        CHECK(Has(c, "Bruna, Light of Alabaster"));
        CHECK_FALSE(Has(c, "Almost Perfect"));
    }
}

TEST_CASE("Glittering Wish rule: a REGROWTH only when it restores a cheat path (Arcanum Wings in the graveyard)")
{
    WishFullWidthArm off(false);
    // s5005 gi342 shape (held-out, 2026-10-06): Wings discarded at cleanup, Colossifications + Mother of
    // Runes in hand, nothing out. The control won a turn sooner by Wish -> Auroral Procession -> Wings ->
    // swap. Procession (any card) is offered; Reborn Hope (multicolored only) cannot return mono-blue Wings.
    auto shape = []()
    {
        BoardSb b;
        for (const std::string& n : kNewSideboard) { b.Side(n); }
        b.Put("Forest");
        b.Put("Azorius Chancery");
        b.Hand("Colossification");
        b.Hand("Mother of Runes");
        b.Grave("Arcanum Wings");
        return b;
    };
    {
        BoardSb b = shape();
        const std::vector<std::string> c = WishCands(b);
        CHECK(Has(c, "Auroral Procession"));
        CHECK_FALSE(Has(c, "Reborn Hope"));
        CHECK(c.back() == "Auroral Procession");      // last: never the base-plan / rollout pick
    }
    // No payload Aura in hand: nothing to swap in -> no regrowth.
    {
        BoardSb b;
        for (const std::string& n : kNewSideboard) { b.Side(n); }
        b.Put("Forest");
        b.Hand("Mother of Runes");
        b.Grave("Arcanum Wings");
        CHECK_FALSE(Has(WishCands(b), "Auroral Procession"));
    }
    // A cheat path already exists (Bruna on the battlefield): no regrowth.
    {
        BoardSb b = shape();
        b.Put("Bruna, Light of Alabaster");
        CHECK_FALSE(Has(WishCands(b), "Auroral Procession"));
    }
    // No swap Aura in the graveyard (a payload Aura there instead): no regrowth.
    {
        BoardSb b;
        for (const std::string& n : kNewSideboard) { b.Side(n); }
        b.Put("Forest");
        b.Hand("Colossification");
        b.Grave("Eldrazi Conscription");
        CHECK_FALSE(Has(WishCands(b), "Auroral Procession"));
        CHECK_FALSE(Has(WishCands(b), "Reborn Hope"));
    }
}

TEST_CASE("Glittering Wish rule: a cheat path needs its COLOURS (Arcanum Wings with no blue source is not one)")
{
    WishFullWidthArm off(false);
    // s4004 d3 gi904 shape (held-out, 2026-10-06): Wings + Colossification in hand, Mother of Runes out,
    // five mana next turn but NO blue source -- Wings can be neither cast nor swapped, so there is no
    // cheat path and the cheap second Aura (Unflinching Courage) the control won with is offered.
    auto shape = [](const char* third_land)
    {
        BoardSb b;
        for (const std::string& n : kNewSideboard) { b.Side(n); }
        b.Put("Forest");
        b.Put("Plains");
        b.Put("Plains");
        b.Put(third_land);
        b.Put("Mother of Runes");
        b.Hand("Forest");
        b.Hand("Arcanum Wings");
        b.Hand("Colossification");
        return b;
    };
    {
        BoardSb b = shape("Plains");
        CHECK(Has(WishCands(b), "Unflinching Courage"));
    }
    // One blue source: Wings is castable and swappable next turn -> a cheat path -> no cheap Aura.
    {
        BoardSb b = shape("Island");
        CHECK_FALSE(Has(WishCands(b), "Unflinching Courage"));
    }
    // Wings already on the battlefield with no blue source cannot swap: still no cheat path.
    {
        BoardSb b;
        for (const std::string& n : kNewSideboard) { b.Side(n); }
        b.Put("Forest");
        b.Put("Plains");
        const int mother = b.Put("Mother of Runes");
        b.Put("Arcanum Wings", false, mother);
        b.Hand("Colossification");
        CHECK(Has(WishCands(b), "Unflinching Courage"));
        b.Put("Island");
        CHECK_FALSE(Has(WishCands(b), "Unflinching Courage"));
    }
}

TEST_CASE("Glittering Wish rule: a mana dork wearing an Aura is a BODY; any creature can wear the cheap Aura")
{
    WishFullWidthArm off(false);
    // s7007 d3 gi933 shape (held-out, 2026-10-06): Mythic Proportions on Avacyn's Pilgrim, Wings in hand
    // with no blue source. The control's kill was Wish -> Unflinching Courage for the last 2 points.
    BoardSb b;
    for (const std::string& n : kNewSideboard) { b.Side(n); }
    b.Put("Forest");
    b.Put("Forest");
    const int pilgrim = b.Put("Avacyn's Pilgrim");
    b.Put("Mythic Proportions", false, pilgrim);
    b.Hand("Arcanum Wings");
    const std::vector<std::string> c = WishCands(b);
    CHECK(Has(c, "Unflinching Courage"));   // (bodies are offered too here: no cheat path, mana short)
    // The bare dork (no Aura) is not a BODY (Linvala is offered), but it can WEAR the cheap Aura (train
    // s2002 d3 gi76: Unflinching Courage on Avacyn's Pilgrim was the control's turn-sooner line).
    BoardSb d;
    for (const std::string& n : kNewSideboard) { d.Side(n); }
    d.Put("Forest");
    d.Put("Forest");
    d.Put("Avacyn's Pilgrim");
    d.Hand("Arcanum Wings");
    const std::vector<std::string> e = WishCands(d);
    CHECK(Has(e, "Linvala, Shield of Sea Gate"));
    CHECK(Has(e, "Unflinching Courage"));
}

TEST_CASE("Glittering Wish rule: LETHAL NOW -- the cheap Aura that closes this turn's gap is offered despite a cheat path")
{
    WishFullWidthArm off(false);
    // s7007 d3 gi933 shape: Mythic Proportions on Avacyn's Pilgrim (9 power, ready), Wings in hand with
    // blue mana for cast + swap (a cheat path). Opponent at 10: Unflinching Courage's +2 is the kill now.
    auto shape = [](int opp_life)
    {
        BoardSb b;
        for (const std::string& n : kNewSideboard) { b.Side(n); }
        b.Put("Seaside Citadel");
        b.Put("Seaside Citadel");
        b.Put("Azorius Chancery");
        b.Put("Forest");
        const int pilgrim = b.Put("Avacyn's Pilgrim");
        b.Put("Mythic Proportions", false, pilgrim);
        b.Hand("Arcanum Wings");
        b.s.players[1].life = opp_life;
        return b;
    };
    {
        BoardSb b = shape(10);
        REQUIRE(b.Power(b.s.battlefield[4].card.m_number) == 9);
        CHECK(Has(WishCands(b), "Unflinching Courage"));
    }
    // Opponent at 15: the cheap Aura does not close it -> the cheat path stands, only the primary.
    {
        BoardSb b = shape(15);
        CHECK_FALSE(Has(WishCands(b), "Unflinching Courage"));
    }
}

TEST_CASE("Glittering Wish rule: a host-tapping Aura CAST on a host is not lethal next turn (Colossification taps it)")
{
    WishFullWidthArm off(false);
    // s7007 d5 gi109 shape (held-out, 2026-10-07): Birds of Paradise out, Colossification + Conscription in
    // hand, seven mana next turn. Colossification cast on Birds is +20 -- but it taps Birds as it enters, so
    // that swing is a turn later. Bruna (her attack trigger puts both on mid-attack) must stay offered.
    BoardSb b;
    for (const std::string& n : kNewSideboard) { b.Side(n); }
    for (const char* l : { "Forest", "Forest", "Plains", "Island" }) { b.Put(l); }
    b.Put("Sol Ring");
    b.Put("Birds of Paradise");
    b.Hand("Seaside Citadel");
    b.Hand("Colossification");
    b.Hand("Eldrazi Conscription");
    const std::vector<std::string> c = WishCands(b);
    CHECK(Has(c, "Bruna, Light of Alabaster"));
}
