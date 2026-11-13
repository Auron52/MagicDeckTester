// MORPH / MEGAMORPH (CR 702.37) -- Blistering Firecat, Hystrodon, Den Protector (Morph INT-2).
//
// The silent failures this file guards (each would play "fine" and measure wrong):
//   * a face-down permanent leaking its real card's abilities (a face-down Firecat sacrificed at the end
//     step, a face-down Hystrodon drawing) -- the synthesized face must be a vanilla 2/2;
//   * a face-down card reaching a ZONE as the face (CR 708.9) -- Den Protector would regrow an MV-0 2/2;
//   * the face-down cast paying the printed cost, or the flip paying nothing;
//   * a turn-face-up losing the permanent's pumps / counters (CR 708.8), or the megamorph counter;
//   * the flip position: Berserk locks X at resolution, so the LEAD flip must come before it;
//   * the enumeration offering TRAIL for a no-trigger card (wasted search) or not offering it for Den
//     Protector (a lost line);
//   * a non-morph deck being touched (deck_has_morph stays false; no face exists for a non-morph card).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsFd()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefF(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardF(const std::string& name, int number)
{
    Card c     = DefF(name).card;
    c.m_number = number;
    return c;
}

int PutF(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = CardF(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

GameState BaseF()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.deck_has_morph            = true;
    s.deck_has_endstep_self_sac = true;
    return s;
}

const Permanent* ByNum(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}

int IdxOf(const GameState& s, int num)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == num) { return i; } }
    return -1;
}

bool InGy(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == name) { return true; } }
    return false;
}

Action FaceDownCast(const std::string& name, int mode = 1)
{
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = name;
    a.def            = &DefF(name);
    a.hand_index     = -1;
    a.face_down      = static_cast<std::int8_t>(mode);
    a.cost           = FaceDownCastCost(DefF(name), mode);
    a.is_noncreature = false;
    return a;
}

Action FaceUp(const std::string& real, int num, bool lead)
{
    Action a;
    a.kind          = Action::Kind::ActivatePermAbility;
    a.card_name     = real + " (Face-down)";
    a.def           = &DefF(real + " (Face-down)");
    a.hand_index    = -1;
    a.sac_source_id = num;
    a.ability_mode  = lead ? Action::AbilityMode::TurnFaceUp : Action::AbilityMode::TurnFaceUpLate;
    a.chosen_x      = 1;
    const CardParams& rp = DefF(real).params;
    a.cost          = rp.morph_cost ? *rp.morph_cost : *rp.megamorph_cost;
    a.is_noncreature = true;
    return a;
}

Action TrickCast(const std::string& name, int target)
{
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = name;
    a.def            = &DefF(name);
    a.hand_index     = -1;
    a.cost           = DefF(name).card.m_mana_cost;
    a.enchant_target = target;
    a.is_noncreature = true;
    return a;
}

TurnSolver::Plan PlanF(const std::vector<Action>& acts)
{
    TurnSolver::Plan p;
    p.actions = acts;
    return p;
}

}   // namespace

TEST_CASE("morph: the face-down face is a vanilla colourless 2/2 with MV 0, only for morph cards")
{
    EnsureCardsFd();
    for (const char* n : { "Blistering Firecat", "Hystrodon", "Den Protector" })
    {
        const CardDefinition& f = DefF(std::string(n) + " (Face-down)");
        CHECK(f.card.IsCreature());
        CHECK(*f.card.m_power == 2);
        CHECK(*f.card.m_toughness == 2);
        CHECK(f.card.m_color_mask == 0u);
        CHECK(f.card.m_keyword_mask == 0u);
        CHECK(f.card.m_subtypes.empty());
        CHECK(f.card.m_mana_cost.ManaValue() == 0);
        CHECK(f.params.face_down_real_name == n);
        CHECK(!f.params.endstep_sacrifice_self);
        CHECK(f.params.combat_damage_self_may_draw == 0);
        CHECK(!f.params.turned_face_up_return_gy_to_hand);
        CHECK(!f.params.morph_cost.has_value());
        CHECK(f.behaviour_identity == 0u);
    }
    CHECK(CardDatabase::Instance().Lookup("Forest (Face-down)") == nullptr);
    CHECK(CardDatabase::Instance().Lookup("Berserk (Face-down)") == nullptr);
    CHECK(FaceUpLeadDominates(DefF("Blistering Firecat")));
    CHECK(FaceUpLeadDominates(DefF("Hystrodon")));
    CHECK(!FaceUpLeadDominates(DefF("Den Protector")));   // turned-face-up trigger
    CardDefinition weak = DefF("Hystrodon");
    weak.card.m_power = 1;
    CHECK(!FaceUpLeadDominates(weak));                    // face-up power below the face's 2
}

TEST_CASE("morph: the face-down cast pays {3} and enters the face under the hand card's number (rollout)")
{
    EnsureCardsFd();
    GameState s = BaseF();
    for (int k = 0; k < 3; ++k) { PutF(s, "Forest", 40 + k); }
    s.players[0].hand.push_back(CardF("Blistering Firecat", 7));
    const int cast_before = s.spells_cast_this_turn;
    TurnSolver::ApplyPlan(s, PlanF({ FaceDownCast("Blistering Firecat") }), /*is_pre_combat=*/true);
    const Permanent* p = ByNum(s, 7);
    REQUIRE(p != nullptr);
    CHECK(p->card.m_name.str() == "Blistering Firecat (Face-down)");
    CHECK(CombatPowerOf(*p, s) == 2);
    CHECK(!p->card.HasKeyword(Keyword::Haste));
    CHECK(s.players[0].hand.empty());
    CHECK(s.spells_cast_this_turn == cast_before + 1);
    int untapped = 0;
    for (const Permanent& q : s.battlefield) { if (q.card.IsLand() && !q.tapped) { ++untapped; } }
    CHECK(untapped == 0);
}

TEST_CASE("morph: the executor's face-down cast enters the face; mode 2 turns it face up at resolution")
{
    EnsureCardsFd();
    AIEngine eng;
    GameState s = BaseF();
    for (int k = 0; k < 3; ++k) { PutF(s, "Forest", 40 + k); }
    for (int k = 0; k < 2; ++k) { PutF(s, "Mountain", 50 + k); }
    s.players[0].hand.push_back(CardF("Blistering Firecat", 7));
    ManaPool avail = AvailableManaPool(s);
    MtgTestSeam::CastFaceDownFromHand(eng, s, s.players[0].hand.back(), avail, 2);
    REQUIRE(s.stack.size() == 1u);
    const StackEntry e = s.stack.back();
    s.stack.pop_back();
    EffectHandler::Resolve(s, e, *CardDatabase::Instance().LookupCached(e.source));
    const Permanent* p = ByNum(s, 7);
    REQUIRE(p != nullptr);
    CHECK(p->card.m_name.str() == "Blistering Firecat");   // {3} + {R}{R}: face down, then face up
    CHECK(CombatPowerOf(*p, s) == 7);
    CHECK(p->card.HasKeyword(Keyword::Haste));
    int untapped = 0;
    for (const Permanent& q : s.battlefield) { if (q.card.IsLand() && !q.tapped) { ++untapped; } }
    CHECK(untapped == 0);
}

TEST_CASE("morph: a face-down permanent has no abilities -- no end-step sacrifice, no draw")
{
    EnsureCardsFd();
    GameState s = BaseF();
    const int fc = PutF(s, "Blistering Firecat (Face-down)", 7);
    (void)fc;
    PutF(s, "Hystrodon (Face-down)", 8);
    PerformEndStepSelfSacrifices(s);
    CHECK(ByNum(s, 7) != nullptr);
    CHECK(DamageDrawsOwedBy(s, IdxOf(s, 8)).may == 0);
    TurnFaceDownPermanentUp(s, 0, IdxOf(s, 8));
    CHECK(DamageDrawsOwedBy(s, IdxOf(s, 8)).may == 1);   // face up: Hystrodon's own trigger
    TurnFaceDownPermanentUp(s, 0, IdxOf(s, 7));
    PerformEndStepSelfSacrifices(s);
    CHECK(ByNum(s, 7) == nullptr);                       // face up: sacrificed at the end step
    CHECK(InGy(s, "Blistering Firecat"));
}

TEST_CASE("morph: turning face up keeps the permanent (CR 708.8); megamorph adds a counter; regrowth is pinned")
{
    EnsureCardsFd();
    GameState s = BaseF();
    const int i = PutF(s, "Den Protector (Face-down)", 9, /*sick=*/true);
    s.battlefield[static_cast<std::size_t>(i)].temp_power_bonus = 3;
    s.players[0].graveyard.push_back(CardF("Berserk", 30));
    s.players[0].graveyard.push_back(CardF("Might of Old Krosa", 31));
    {
        const std::string pin = "Berserk";
        const FaceUpRegrowPinScope scope(&pin);
        ApplyPermAbility(s, 0, 9, PermAbilityMode::TurnFaceUp);
    }
    const Permanent* p = ByNum(s, 9);
    REQUIRE(p != nullptr);
    CHECK(p->card.m_name.str() == "Den Protector");
    CHECK(p->entered_this_turn);              // sickness unchanged
    CHECK(PlusCountersOn(*p) == 1);           // megamorph
    CHECK(CombatPowerOf(*p, s) == 2 + 1 + 3); // printed 2, counter, the pump persists
    REQUIRE(s.players[0].hand.size() == 1u);
    CHECK(s.players[0].hand[0].m_name.str() == "Berserk");
    CHECK(!PermAbilitySourceLive(s, 0, 9, PermAbilityMode::TurnFaceUp, nullptr));   // already face up
}

TEST_CASE("morph: an empty graveyard removes Den Protector's trigger (CR 603.3d)")
{
    EnsureCardsFd();
    GameState s = BaseF();
    PutF(s, "Den Protector (Face-down)", 9);
    ApplyPermAbility(s, 0, 9, PermAbilityMode::TurnFaceUpLate);
    CHECK(ByNum(s, 9)->card.m_name.str() == "Den Protector");
    CHECK(s.players[0].hand.empty());
}

TEST_CASE("morph: leaving the battlefield reveals the card (CR 708.9) -- sacrifice and the SBA backstop")
{
    EnsureCardsFd();
    GameState s = BaseF();
    PutF(s, "Blistering Firecat (Face-down)", 7);
    SacrificePermanentAt(s, 0, IdxOf(s, 7));
    REQUIRE(s.players[0].graveyard.size() == 1u);
    CHECK(s.players[0].graveyard[0].m_name.str() == "Blistering Firecat");
    CHECK(s.players[0].graveyard[0].m_number == 7);
    // A planted face-down zone card is rewritten by the backstop (the audit build aborts instead).
    s.players[0].graveyard.push_back(CardF("Hystrodon (Face-down)", 8));
    NormalizeFaceDownZoneCards(s);
    CHECK(s.players[0].graveyard.back().m_name.str() == "Hystrodon");
    CHECK(s.players[0].graveyard.back().m_number == 8);
    CHECK(RevealedCard(CardF("Forest", 1)).m_name.str() == "Forest");   // identity elsewhere
}

TEST_CASE("mdfc: a played BACK face leaves the battlefield as its FRONT (CR 712.8a) -- bounce, sacrifice, every zone")
{
    // Morph sweep g10 (seed 50010, T2): Gruul Turf bounced a Legion Stronghold, which returned to hand as a
    // land-only card -- the Legion Leadership front was lost. The spell//land back-face synthesis dropped
    // mdfc_front_name when it replaced the copied params.
    EnsureCardsFd();
    REQUIRE(DefF("Legion Stronghold").params.mdfc_front_name == "Legion Leadership");
    REQUIRE(DefF("Turntimber, Serpentine Wood").params.mdfc_front_name == "Turntimber Symbiosis");
    REQUIRE(DefF("Boulderloft Pathway").params.mdfc_front_name == "Branchloft Pathway");
    {   // HAND: the Karoo bounce (shared by executor and rollout land drops).
        GameState s = BaseF();
        PutF(s, "Legion Stronghold", 5);
        s.battlefield.back().tapped = true;
        PutF(s, "Gruul Turf", 6);
        BounceKarooLand(s, 0, IdxOf(s, 6));
        REQUIRE(s.players[0].hand.size() == 1u);
        CHECK(s.players[0].hand[0].m_name.str() == "Legion Leadership");
        CHECK(s.players[0].hand[0].m_number == 5);
        CHECK(ByNum(s, 5) == nullptr);
    }
    {   // GRAVEYARD: a sacrifice.
        GameState s = BaseF();
        PutF(s, "Legion Stronghold", 5);
        SacrificePermanentAt(s, 0, IdxOf(s, 5));
        REQUIRE(s.players[0].graveyard.size() == 1u);
        CHECK(s.players[0].graveyard[0].m_name.str() == "Legion Leadership");
        CHECK(s.players[0].graveyard[0].m_number == 5);
    }
    // EXILE / LIBRARY and every other battlefield -> zone push go through the same RevealedCard mapping.
    CHECK(RevealedCard(CardF("Legion Stronghold", 3)).m_name.str() == "Legion Leadership");
    CHECK(RevealedCard(CardF("Legion Stronghold", 3)).m_number == 3);
    CHECK(RevealedCard(CardF("Soporific Springs", 4)).m_name.str() == "Sink into Stupor");
    CHECK(RevealedCard(CardF("Boulderloft Pathway", 2)).m_name.str() == "Branchloft Pathway");
    CHECK(RevealedCard(CardF("Legion Leadership", 1)).m_name.str() == "Legion Leadership");   // front: identity
}

TEST_CASE("morph: the LEAD flip precedes the casts -- Berserk doubles the face-up 7, not the face's 2")
{
    EnsureCardsFd();
    GameState s = BaseF();
    PutF(s, "Blistering Firecat (Face-down)", 7);
    for (int k = 0; k < 3; ++k) { PutF(s, "Mountain", 50 + k); }
    PutF(s, "Forest", 40);
    s.players[0].hand.push_back(CardF("Berserk", 30));
    // Berserk is listed FIRST: the LEAD flip still runs before every cast.
    TurnSolver::ApplyPlan(s, PlanF({ TrickCast("Berserk", 7), FaceUp("Blistering Firecat", 7, true) }),
                          /*is_pre_combat=*/true);
    const Permanent* p = ByNum(s, 7);
    REQUIRE(p != nullptr);
    CHECK(p->card.m_name.str() == "Blistering Firecat");
    CHECK(CombatPowerOf(*p, s) == 14);
}

TEST_CASE("morph: the enumeration offers face-down casts and the flip positions the fold allows")
{
    EnsureCardsFd();
    GameState s = BaseF();
    for (int k = 0; k < 3; ++k) { PutF(s, "Forest", 40 + k); }
    for (int k = 0; k < 2; ++k) { PutF(s, "Mountain", 50 + k); }
    PutF(s, "Blistering Firecat (Face-down)", 7);
    PutF(s, "Den Protector (Face-down)", 9);
    s.players[0].graveyard.push_back(CardF("Berserk", 30));
    s.players[0].hand.push_back(CardF("Blistering Firecat", 11));
    s.players[0].hand.push_back(CardF("Hystrodon", 12));
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true);
    int fc_lead = 0, fc_trail = 0, dp_lead = 0, dp_trail = 0, fd1 = 0, fd2_firecat = 0, fd2_hystrodon = 0;
    for (const Action& a : acts)
    {
        if (a.kind == Action::Kind::ActivatePermAbility && a.sac_source_id == 7)
        {
            fc_lead  += a.ability_mode == Action::AbilityMode::TurnFaceUp;
            fc_trail += a.ability_mode == Action::AbilityMode::TurnFaceUpLate;
        }
        if (a.kind == Action::Kind::ActivatePermAbility && a.sac_source_id == 9)
        {
            dp_lead  += a.ability_mode == Action::AbilityMode::TurnFaceUp;
            dp_trail += a.ability_mode == Action::AbilityMode::TurnFaceUpLate;
        }
        if (a.kind == Action::Kind::CastFromHand && a.face_down == 1) { ++fd1; }
        if (a.kind == Action::Kind::CastFromHand && a.face_down == 2)
        {
            fd2_firecat   += a.card_name.str() == "Blistering Firecat";
            fd2_hystrodon += a.card_name.str() == "Hystrodon";
        }
    }
    CHECK(fc_lead == 1);
    CHECK(fc_trail == 0);          // the position fold: no-trigger, power 7 >= 2
    CHECK(dp_lead >= 1);
    CHECK(dp_trail >= 1);          // Den Protector keeps both positions
    CHECK(fd1 == 2);               // both hand morph cards may be cast face down
    CHECK(fd2_firecat == 1);       // {1}{R}{R}{R} is not inside {3}+{R}{R}: the combined cast stays
    CHECK(fd2_hystrodon == 0);     // {4}{G} is inside {3}+{1}{G}{G}: the hard cast dominates
}

TEST_CASE("morph: a non-morph deck is untouched -- deck_has_morph stays false")
{
    EnsureCardsFd();
    const Decklist morph = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Morph/Morph.cod"));
    GameState a;
    GoldFishRunner::StampDeckTraits(a, morph);
    CHECK(a.deck_has_morph);
    CHECK(a.deck_has_endstep_self_sac);
    CHECK(static_cast<bool>(a.deck_has_draw_ping));
    Decklist plain;
    for (int k = 0; k < 20; ++k) { plain.mainboard.push_back(CardF("Forest", k + 1)); }
    GameState b;
    GoldFishRunner::StampDeckTraits(b, plain);
    CHECK(!b.deck_has_morph);
    CHECK(!b.deck_has_endstep_self_sac);
}

// ---- THE OPPONENT-END-STEP FLIP WINDOW (Morph INT-2b; morph.md step 7) -------------------------------------
namespace
{
GameState EotBase()
{
    GameState s = BaseF();
    s.phase = Phase::PostCombatMain;
    for (int n = 0; n < 6; ++n) { s.players[0].library.push_back(CardF("Forest", 200 + n)); }
    return s;
}
std::vector<std::vector<int>> MenuIds(const GameState& s)
{
    std::vector<std::vector<int>> out;
    for (const EotFaceUpOption& o : EotFaceUpMenu(s)) { out.push_back(o.ids); }
    return out;
}
}   // namespace

TEST_CASE("morph eot: the menu fans flip-or-hold for a payable Firecat, the provider's front (flip) first")
{
    EnsureCardsFd();
    GameState s = EotBase();
    PutF(s, "Blistering Firecat (Face-down)", 7);
    PutF(s, "Mountain", 50);
    PutF(s, "Mountain", 51);
    const auto m = MenuIds(s);
    REQUIRE(m.size() == 2u);
    CHECK(m[0] == std::vector<int>{ 7 });
    CHECK(m[1].empty());
    // One Mountain tapped: nothing is payable -- the only entry is "flip nothing".
    GameState t = s;
    t.battlefield[static_cast<std::size_t>(IdxOf(t, 51))].tapped = true;
    const auto m1 = MenuIds(t);
    REQUIRE(m1.size() == 1u);
    CHECK(m1[0].empty());
}

TEST_CASE("morph eot: a pinned flip at the opponent's end step survives that end step and is paid from untapped lands")
{
    EnsureCardsFd();
    GameState s = EotBase();
    PutF(s, "Blistering Firecat (Face-down)", 7);
    PutF(s, "Mountain", 50);
    PutF(s, "Mountain", 51);
    s.floating_mana.Add(Color::Red, 2);   // mana empties between steps: the window never spends a stale pool
    GameState hold = s;
    s.scripted_eot_face_up = 0;            // rank 0 = flip
    PerformEndStepSelfSacrifices(s);       // the opponent's end step BEGINS: the face-down Firecat has no trigger
    PerformOpponentEndStepFaceUp(s);
    const Permanent* f = ByNum(s, 7);
    REQUIRE(f != nullptr);
    CHECK(f->card.m_name.str() == "Blistering Firecat");
    CHECK(ByNum(s, 50)->tapped);
    CHECK(ByNum(s, 51)->tapped);
    CHECK(s.floating_mana.Total() == 2);   // the pre-existing pool is restored untouched
    CHECK(s.scripted_eot_face_up == -1);   // consumed
    hold.scripted_eot_face_up = 1;         // rank 1 = hold
    PerformOpponentEndStepFaceUp(hold);
    CHECK(ByNum(hold, 7)->card.m_name.str() == "Blistering Firecat (Face-down)");
    CHECK(!ByNum(hold, 50)->tapped);
}

TEST_CASE("morph eot: the Hystrodon fold drops only subsets that leave out a payable Hystrodon")
{
    EnsureCardsFd();
    CHECK(EotFaceUpDominates(DefF("Hystrodon")));
    CHECK(!EotFaceUpDominates(DefF("Blistering Firecat")));
    CHECK(!EotFaceUpDominates(DefF("Den Protector")));
    GameState s = EotBase();
    PutF(s, "Hystrodon (Face-down)", 8);
    PutF(s, "Blistering Firecat (Face-down)", 7);
    for (int n = 0; n < 3; ++n) { PutF(s, "Forest", 60 + n); }
    PutF(s, "Mountain", 50);
    PutF(s, "Mountain", 51);
    const auto both = MenuIds(s);
    // Front = both (the larger face-up power first: Firecat, then Hystrodon); {Firecat} and {} leave out a payable
    // Hystrodon and are folded; {Hystrodon} (hold the Firecat for a pump turn) stays.
    REQUIRE(both.size() == 2u);
    CHECK(both[0] == std::vector<int>{ 7, 8 });
    CHECK(both[1] == std::vector<int>{ 8 });
    // Mana for only one of them (two Mountains + one Forest): {Firecat} cannot add Hystrodon, so it stays.
    GameState t = s;
    t.battlefield[static_cast<std::size_t>(IdxOf(t, 61))].tapped = true;
    t.battlefield[static_cast<std::size_t>(IdxOf(t, 62))].tapped = true;
    const auto one = MenuIds(t);
    REQUIRE(one.size() == 2u);
    CHECK(one[0] == std::vector<int>{ 7 });
    CHECK(one[1].empty());
}

TEST_CASE("morph eot: Den Protector flipped there regrows a Firecat our end step sacrificed (each target searched)")
{
    EnsureCardsFd();
    GameState s = EotBase();
    PutF(s, "Den Protector (Face-down)", 9);
    PutF(s, "Forest", 60);
    PutF(s, "Forest", 61);
    s.players[0].graveyard.push_back(CardF("Berserk", 30));
    s.players[0].graveyard.push_back(CardF("Blistering Firecat", 31));
    const std::vector<EotFaceUpOption> menu = EotFaceUpMenu(s);
    REQUIRE(menu.size() == 3u);   // regrow Firecat (the provider's pick: nonland, highest MV), regrow Berserk, hold
    CHECK(menu[0].regrow == std::vector<std::string>{ "Blistering Firecat" });
    CHECK(menu[1].regrow == std::vector<std::string>{ "Berserk" });
    CHECK(menu[2].ids.empty());
    GameState t = s;
    t.scripted_eot_face_up = 1;
    PerformOpponentEndStepFaceUp(t);
    REQUIRE(t.players[0].hand.size() == 1u);
    CHECK(t.players[0].hand[0].m_name.str() == "Berserk");
    CHECK(PlusCountersOn(*ByNum(t, 9)) == 1);
}

TEST_CASE("morph eot: the BoundaryFan crosses once per menu rank and records the rank (the follow pin first)")
{
    EnsureCardsFd();
    GameState s = EotBase();
    PutF(s, "Blistering Firecat (Face-down)", 7);
    PutF(s, "Mountain", 50);
    PutF(s, "Mountain", 51);
    const auto crossings = TurnSolver::FaceUpBoundaryForTest(s);
    REQUIRE(crossings.size() == 2u);
    CHECK(crossings[0] == std::pair<int, int>{ 0, 1 });   // rank 0: flipped at the opponent's end step, alive
    CHECK(crossings[1] == std::pair<int, int>{ 1, 0 });   // rank 1: held face down
    // No face-down permanent: the axis is not live, the single crossing records nothing.
    GameState n = EotBase();
    PutF(n, "Mountain", 50);
    const auto none = TurnSolver::FaceUpBoundaryForTest(n);
    REQUIRE(none.size() == 1u);
    CHECK(none[0].first == -2);
}

// ---- the human LINE (play viewer): facedown= / facedownup= pick the cast's MODE ------------------------------
// The hard cast, the face-down cast and the face-down-then-up cast share one card name, so `cast=X` alone cannot
// say which; LineSpec::face_downs / face_down_ups are compared UNCONDITIONALLY (the kicked= contract), so an
// absent key means the HARD cast and never silently resolves face down. USER 2026-11-13: "How do I morph my
// creatures in the viewer? It seems to not be available?"
namespace
{
int LineFaceDownMode(const GameState& s, const TurnSolver::LineSpec& ls, const std::string& name,
                     TurnSolver::LineCheck* out = nullptr)
{
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::LineCheck lc = TurnSolver::CheckLine(s, true, ls, &plans);
    if (out != nullptr) { *out = lc; }
    if (lc.verdict != TurnSolver::LineCheck::Verdict::Accept || lc.plan_index < 0) { return -1; }
    for (const Action& a : plans[static_cast<std::size_t>(lc.plan_index)].actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return a.face_down; } }
    return -2;
}
}   // namespace

TEST_CASE("morph line: cast= is the HARD cast; facedown= / facedownup= select the face-down modes")
{
    EnsureCardsFd();
    {
        GameState s = BaseF();
        for (int k = 0; k < 5; ++k) { PutF(s, "Forest", 40 + k); }
        s.players[0].hand.push_back(CardF("Hystrodon", 12));
        TurnSolver::LineSpec hard;
        hard.casts.push_back("Hystrodon");
        TurnSolver::LineCheck lc;
        CHECK(LineFaceDownMode(s, hard, "Hystrodon", &lc) == 0);
        INFO("reason: " << lc.reason);
        TurnSolver::LineSpec down = hard;
        down.face_downs.push_back("Hystrodon");
        CHECK(LineFaceDownMode(s, down, "Hystrodon", &lc) == 1);
        // Hystrodon's down+up is folded away (the hard cast dominates), so the line names a play not on offer.
        TurnSolver::LineSpec up = hard;
        up.face_down_ups.push_back("Hystrodon");
        CHECK(LineFaceDownMode(s, up, "Hystrodon", &lc) == -1);
    }
    {
        GameState s = BaseF();
        for (int k = 0; k < 3; ++k) { PutF(s, "Forest", 40 + k); }
        for (int k = 0; k < 3; ++k) { PutF(s, "Mountain", 50 + k); }
        s.players[0].hand.push_back(CardF("Blistering Firecat", 11));
        TurnSolver::LineSpec hard;
        hard.casts.push_back("Blistering Firecat");
        CHECK(LineFaceDownMode(s, hard, "Blistering Firecat") == 0);
        TurnSolver::LineSpec down = hard;
        down.face_downs.push_back("Blistering Firecat");
        CHECK(LineFaceDownMode(s, down, "Blistering Firecat") == 1);
        TurnSolver::LineSpec up = hard;
        up.face_down_ups.push_back("Blistering Firecat");
        CHECK(LineFaceDownMode(s, up, "Blistering Firecat") == 2);
        // A face-down mode with no cast of the card, and one naming a non-morph card, are rejected with a reason.
        TurnSolver::LineSpec orphan;
        orphan.face_downs.push_back("Blistering Firecat");
        TurnSolver::LineCheck lc;
        CHECK(LineFaceDownMode(s, orphan, "Blistering Firecat", &lc) == -1);
        CHECK(lc.verdict != TurnSolver::LineCheck::Verdict::Accept);
    }
    {
        // NON-MORPH: an ordinary line is untouched (both mode sets empty on both sides).
        GameState s = BaseF();
        s.deck_has_morph = false;
        for (int k = 0; k < 2; ++k) { PutF(s, "Forest", 40 + k); }
        s.players[0].hand.push_back(CardF("Berserk", 13));
        TurnSolver::LineSpec ls;
        ls.casts.push_back("Berserk");
        TurnSolver::LineCheck lc;
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        lc = TurnSolver::CheckLine(s, true, ls, &plans);
        CHECK(lc.verdict != TurnSolver::LineCheck::Verdict::Illegal);
        TurnSolver::LineSpec bad = ls;
        bad.face_downs.push_back("Berserk");
        lc = TurnSolver::CheckLine(s, true, bad, &plans);
        INFO("reason: " << lc.reason);
        CHECK(lc.verdict == TurnSolver::LineCheck::Verdict::Illegal);
        CHECK(lc.reason.find("morph") != std::string::npos);
    }
}

TEST_CASE("morph line: face_up= names the REAL card of a face-down permanent")
{
    EnsureCardsFd();
    GameState s = BaseF();
    for (int k = 0; k < 3; ++k) { PutF(s, "Forest", 40 + k); }
    PutF(s, "Hystrodon (Face-down)", 9);
    TurnSolver::LineSpec ls;
    ls.face_ups.push_back("Hystrodon");
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::LineCheck lc = TurnSolver::CheckLine(s, true, ls, &plans);
    INFO("reason: " << lc.reason);
    REQUIRE(lc.verdict == TurnSolver::LineCheck::Verdict::Accept);
    REQUIRE(lc.plan_index >= 0);
    bool flipped = false;
    for (const Action& a : plans[static_cast<std::size_t>(lc.plan_index)].actions)
    { flipped = flipped || (a.kind == Action::Kind::ActivatePermAbility && a.sac_source_id == 9 && IsTurnFaceUpMode(a.ability_mode)); }
    CHECK(flipped);
}

// Morph seed 1 gi0 T4 (the USER's viewer rejection, 2026-11-13): Gruul Turf ({R}{G} in one tap), Stomping
// Ground, a Forest, a face-down Blistering Firecat ({R}{R} to flip), Might of Old Krosa {G} + Berserk {G}.
// The board pays all three -- the Turf for {G} with its {R} floating, the Forest for {G}, then the floating
// {R} + the Ground's {R} -- but the activation hold counted the Turf as a whole {R} provider, held both red
// sources, and when that failed the unrestricted prepay paid {G}{G} with the Ground and the Forest: the flip
// found one {R} and was dropped. ActLineHoldSurplusBits releases the Turf first.
TEST_CASE("morph: a trailing flip after two {G} casts pays off a Karoo's floating {R} (ActLineHoldSurplusBits)")
{
    EnsureCardsFd();
    GameState s = BaseF();
    s.turn_number = 4;
    PutF(s, "Blistering Firecat (Face-down)", 10);
    PutF(s, "Gruul Turf", 26);
    PutF(s, "Stomping Ground", 49);
    PutF(s, "Forest", 18);
    s.players[0].hand.push_back(CardF("Might of Old Krosa", 39));
    s.players[0].hand.push_back(CardF("Berserk", 3));
    // The flip LAST (a TRAIL flip): the casts pay first, through the whole-turn prepay.
    TurnSolver::ApplyPlan(s, PlanF({ TrickCast("Might of Old Krosa", 10), TrickCast("Berserk", 10),
                                     FaceUp("Blistering Firecat", 10, /*lead=*/false) }),
                          /*is_pre_combat=*/true);
    const Permanent* p = ByNum(s, 10);
    REQUIRE(p != nullptr);
    CHECK(p->card.m_name.str() == "Blistering Firecat");   // the flip was paid
    CHECK(s.players[0].hand.empty());                       // both casts were paid
    for (const Permanent& q : s.battlefield) { if (q.card.IsLand()) { CHECK(q.tapped); } }
}

TEST_CASE("morph: the LEAD flip before two {G} casts on the Karoo board -- Berserk doubles the face-up 11")
{
    EnsureCardsFd();
    GameState s = BaseF();
    s.turn_number = 4;
    PutF(s, "Blistering Firecat (Face-down)", 10);
    PutF(s, "Gruul Turf", 26);
    PutF(s, "Stomping Ground", 49);
    PutF(s, "Forest", 18);
    s.players[0].hand.push_back(CardF("Might of Old Krosa", 39));
    s.players[0].hand.push_back(CardF("Berserk", 3));
    TurnSolver::ApplyPlan(s, PlanF({ FaceUp("Blistering Firecat", 10, /*lead=*/true),
                                     TrickCast("Might of Old Krosa", 10), TrickCast("Berserk", 10) }),
                          /*is_pre_combat=*/true);
    const Permanent* p = ByNum(s, 10);
    REQUIRE(p != nullptr);
    CHECK(p->card.m_name.str() == "Blistering Firecat");
    CHECK(s.players[0].hand.empty());
    CHECK(CombatPowerOf(*p, s) == 22);   // (7 + 4) doubled: Berserk resolved after the flip and Might
}
