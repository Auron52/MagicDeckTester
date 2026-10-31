// NAME-ONLY PLACEHOLDER reads (the 2026-10-29 audit; HeuristicArm.h PLACEHOLDER_*).
//
// Every hand / library / graveyard Card in real play is built by DeckLoader::MakePlaceholder from its
// NAME alone, so its own m_mana_cost, type / supertype / colour masks, m_subtypes and P/T are EMPTY; the
// real characteristics live on CardDatabase::LookupCached(c)->card (ZoneCard). These pin sites the audit
// found reading the empty fields, each with its lever ON (the default, the definition read) and OFF (the
// historical placeholder read), and every hand here is built from placeholders exactly as play builds it
// -- a test hand built from `def.card` would pass with the bug in place.
//
//   * GoblinsProvider::PayEchoToKeep's "no gas" clause (MTG_PLACEHOLDER_ECHO, DEFAULT OFF until echo is a
//     searched decision): an uncastable Muxus read as {0} and so "castable", and the echo was declined
//     whenever the hand held any spell at all; and its sub-lever MTG_GOBLIN_ECHO_LAND_DROP (the turn's land
//     drop counts toward "castable").
//   * LandfallProvider::CleanupDiscardCandidates' legend rule (MTG_PLACEHOLDER_PROVIDER, DEFAULT ON): a
//     second copy of a legend already on our battlefield is a blank and sheds first -- never, with the
//     placeholder read.
//   * ExtractMidGameFeatures' lands_in_hand / hand_castable_now: a placeholder land in hand was not a land,
//     and counted as a castable MV-0 spell. Since 2026-10-31 the read is chosen PER MODEL by the sidecar's
//     "featurizer" (absent = 1, the historical read; 2 = the definition read), with MTG_PLACEHOLDER_FEATURES
//     an experiment override forcing 2 everywhere.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/KeepModel.h"
#include "ai/MulliganProfileIO.h"   // EvalModelFromJsonObj / EvalModelToJsonObj / AttachValueSidecar
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

void EnsureCardsPh()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A zone card exactly as play builds it (DeckLoader::MakePlaceholder, which is private: the name, its
// hash and nothing else).
Card PhCard(const std::string& name, int number)
{
    EnsureCardsPh();
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = CardDatabase::Instance().CanonicalName(name);
    c.RehashName();
    c.m_number = number;
    return c;
}

void PutPh(GameState& s, const std::string& name, int number, bool tapped = false)
{
    EnsureCardsPh();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Permanent p;
    p.card              = d->card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    p.tapped            = tapped;
    s.battlefield.push_back(p);
}

GameState BasePh(int turn)
{
    EnsureCardsPh();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[1].life     = 20;
    return s;
}

// Run `f` with one lever forced on (1) or off (0); restores "unset" afterwards.
template <class F>
auto WithArm(heurarm::Slot slot, int arm, F&& f)
{
    heurarm::t_arm[slot] = static_cast<std::int8_t>(arm);
    auto r = f();
    heurarm::t_arm[slot] = -1;
    return r;
}

}   // namespace

TEST_CASE("Placeholder: the placeholder really is empty (the premise of every case below)")
{
    const Card c = PhCard("Muxus, Goblin Grandee", 1);
    CHECK(c.m_mana_cost.ManaValue() == 0);
    CHECK_FALSE(c.IsCreature());
    CHECK_FALSE(c.HasSupertype(Supertype::Legendary));
    CHECK(ZoneCard(c).m_mana_cost.ManaValue() == 6);
    CHECK(ZoneCard(c).IsCreature());
    CHECK(ZoneCard(c).HasSupertype(Supertype::Legendary));
    CHECK(&ZoneCardIf(false, c) == &c);
}

TEST_CASE("Placeholder: Goblins echo -- an uncastable hand is NO gas, so the Marshal is kept")
{
    // Two untapped Mountains; Mogg War Marshal's echo is due. Hand: Muxus ({4}{R}{R}) and a Mountain --
    // nothing castable off two mana, so clause (b) keeps the attacker (PAY). Opponent at 20: clause (a)
    // (lethal now) cannot fire.
    GameState s = BasePh(3);
    PutPh(s, "Mountain", 10);
    PutPh(s, "Mountain", 11);
    PutPh(s, "Mogg War Marshal", 12);
    s.players[0].hand.push_back(PhCard("Muxus, Goblin Grandee", 1));
    s.players[0].hand.push_back(PhCard("Mountain", 2));
    const Permanent& marshal = s.battlefield.back();
    const GoblinsProvider gob;

    CHECK(WithArm(heurarm::PLACEHOLDER_ECHO, 1, [&] { return gob.PayEchoToKeep(s, marshal); }));
    // The historical read: Muxus's placeholder cost {0} is "castable" -> decline the echo.
    CHECK_FALSE(WithArm(heurarm::PLACEHOLDER_ECHO, 0, [&] { return gob.PayEchoToKeep(s, marshal); }));
    // Unset == the default (ON, unblocked by the searched echo): the real read, which pays.
    CHECK(gob.PayEchoToKeep(s, marshal));
}

TEST_CASE("Placeholder: Goblins echo -- a castable spell in hand is gas, so the echo is declined")
{
    // Goblin Lackey ({R}) IS castable off two Mountains: both reads decline.
    GameState s = BasePh(3);
    PutPh(s, "Mountain", 10);
    PutPh(s, "Mountain", 11);
    PutPh(s, "Mogg War Marshal", 12);
    s.players[0].hand.push_back(PhCard("Muxus, Goblin Grandee", 1));
    s.players[0].hand.push_back(PhCard("Goblin Lackey", 2));
    const Permanent& marshal = s.battlefield.back();
    const GoblinsProvider gob;
    CHECK_FALSE(WithArm(heurarm::PLACEHOLDER_ECHO, 1, [&] { return gob.PayEchoToKeep(s, marshal); }));
    CHECK_FALSE(WithArm(heurarm::PLACEHOLDER_ECHO, 0, [&] { return gob.PayEchoToKeep(s, marshal); }));
}

TEST_CASE("Placeholder: Goblins echo -- the turn's land drop counts toward 'castable this turn'")
{
    // Held-out seeds 1350641 / 1360791, T3 upkeep: two untapped Mountains, Mogg War Marshal's echo due,
    // Goblin Chieftain ({1}{R}{R}) and a Mountain in hand, the land drop still open. The Chieftain is
    // castable THIS turn off the drop, so the echo is declined (MTG_GOBLIN_ECHO_LAND_DROP, default ON);
    // the upkeep pool alone (two mana) reads it uncastable and pays -- stranding the T3 Chieftain.
    GameState s = BasePh(3);
    PutPh(s, "Mountain", 10);
    PutPh(s, "Mountain", 11);
    PutPh(s, "Mogg War Marshal", 12);
    s.players[0].hand.push_back(PhCard("Goblin Chieftain", 1));
    s.players[0].hand.push_back(PhCard("Mountain", 2));
    s.players[0].lands_played_this_turn = 0;
    const Permanent& marshal = s.battlefield.back();
    const GoblinsProvider gob;
    auto pay = [&](int land_drop)
    {
        heurarm::t_arm[heurarm::GOBLIN_ECHO_LAND_DROP] = static_cast<std::int8_t>(land_drop);
        const bool r = WithArm(heurarm::PLACEHOLDER_ECHO, 1, [&] { return gob.PayEchoToKeep(s, marshal); });
        heurarm::t_arm[heurarm::GOBLIN_ECHO_LAND_DROP] = -1;
        return r;
    };
    CHECK_FALSE(pay(1));   // castable after the drop -> decline the echo, cast the Chieftain
    CHECK(pay(0));         // upkeep pool alone: "no gas" -> pay
    CHECK_FALSE(gob.PayEchoToKeep(s, marshal));   // the defaults (both ON): castable after the drop -> decline
    // The drop already made: only the upkeep pool counts, and two mana do not cast the Chieftain.
    s.players[0].lands_played_this_turn = 1;
    CHECK(pay(1));
}

TEST_CASE("Placeholder: Landfall cleanup discard sheds a second copy of a legend on our battlefield")
{
    // Phylath on our battlefield (with four lands); a second Phylath in an eight-card hand is a blank
    // under the legend rule and is the first shed. Read as a placeholder it is no legend, so it ranks as
    // the deck's top threat instead and something else goes.
    GameState s = BasePh(6);
    PutPh(s, "Forest", 20);
    PutPh(s, "Forest", 21);
    PutPh(s, "Mountain", 22);
    PutPh(s, "Mountain", 23);
    PutPh(s, "Phylath, World Sculptor", 24);
    const std::vector<std::string> hand = {
        "Rampaging Baloths", "Phylath, World Sculptor", "Avenger of Zendikar", "Courser of Kruphix",
        "Forest", "Mountain", "Forest", "Primeval Titan",
    };
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(PhCard(n, num++)); }
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(PhCard(k % 2 ? "Forest" : "Mountain", 100 + k)); }
    s.players[0].lands_played_this_turn = 1;
    const LandfallProvider lf;

    const std::vector<int> on  = WithArm(heurarm::PLACEHOLDER_PROVIDER, 1,
                                         [&] { return lf.CleanupDiscardCandidates(s, nullptr); });
    const std::vector<int> off = WithArm(heurarm::PLACEHOLDER_PROVIDER, 0,
                                         [&] { return lf.CleanupDiscardCandidates(s, nullptr); });
    REQUIRE_FALSE(on.empty());
    REQUIRE_FALSE(off.empty());
    CHECK(on.front() == 1);    // the dead Phylath
    CHECK(off.front() != 1);   // the placeholder read keeps it (and sheds a live card)
}

namespace
{
// One untapped Forest; hand = Forest + Courser of Kruphix ({1}{G}{G}, not castable off one source).
GameState FeaturizerBoard()
{
    GameState s = BasePh(2);
    PutPh(s, "Forest", 20);
    s.players[0].hand.push_back(PhCard("Forest", 1));
    s.players[0].hand.push_back(PhCard("Courser of Kruphix", 2));
    return s;
}
const int kLih = static_cast<int>(MidGameFeature::LandsInHand);
const int kHcn = static_cast<int>(MidGameFeature::HandCastableNow);
}   // namespace

TEST_CASE("Placeholder: mid-game features count a placeholder land in hand as a land (featurizer 2)")
{
    const GameState s = FeaturizerBoard();
    const std::vector<int> v2 = WithArm(heurarm::PLACEHOLDER_FEATURES, 0,
        [&] { return ExtractMidGameFeatures(s, MidGamePlanSummary{}, kFeaturizerDefinition); });
    const std::vector<int> v1 = WithArm(heurarm::PLACEHOLDER_FEATURES, 0,
        [&] { return ExtractMidGameFeatures(s, MidGamePlanSummary{}, kFeaturizerPlaceholder); });
    CHECK(v2[kLih] == 1);
    CHECK(v2[kHcn] == 0);    // Courser costs 3; the Forest is not a spell
    CHECK(v1[kLih] == 0);    // the historical read: the placeholder Forest is no land...
    CHECK(v1[kHcn] == 1);    // ...and is "castable" at MV 0
    // MTG_PLACEHOLDER_FEATURES=1 is the experiment override: the definition read whatever the model names.
    const std::vector<int> forced = WithArm(heurarm::PLACEHOLDER_FEATURES, 1,
        [&] { return ExtractMidGameFeatures(s, MidGamePlanSummary{}, kFeaturizerPlaceholder); });
    CHECK(forced == v2);
    // Generators fit on the definition read.
    CHECK(kFeaturizerCurrent == kFeaturizerDefinition);
}

TEST_CASE("Featurizer: a sidecar without the field reads the historical placeholder values")
{
    const nlohmann::json j = nlohmann::json::parse(
        R"({"eval_model": {"intercept": 5000, "coefs": {"lands_in_hand": 1000}}, "featurizer": 2})");
    // A TOP-LEVEL "featurizer" is ignored on purpose: the version lives on the model object, so it can
    // never detach from the model it describes when a pipeline merges eval_model into another sidecar.
    const MidGameEvaluator e = EvalModelFromJsonObj(j);
    REQUIRE_FALSE(e.empty());
    CHECK(e.featurizer == kFeaturizerPlaceholder);
    const GameState s = FeaturizerBoard();
    const std::vector<int> f = ExtractMidGameFeatures(s, MidGamePlanSummary{}, e.featurizer);
    CHECK(f[kLih] == 0);
    CHECK(f[kHcn] == 1);
    CHECK(e.Score(f) == 5000);
}

TEST_CASE("Featurizer: a \"featurizer\": 2 sidecar reads the definition values")
{
    const nlohmann::json j = nlohmann::json::parse(
        R"({"eval_model": {"featurizer": 2, "intercept": 5000, "coefs": {"lands_in_hand": 1000}}})");
    const MidGameEvaluator e = EvalModelFromJsonObj(j);
    REQUIRE_FALSE(e.empty());
    CHECK(e.featurizer == kFeaturizerDefinition);
    const GameState s = FeaturizerBoard();
    const std::vector<int> f = ExtractMidGameFeatures(s, MidGamePlanSummary{}, e.featurizer);
    CHECK(f[kLih] == 1);
    CHECK(f[kHcn] == 0);
    CHECK(e.Score(f) == 6000);
}

TEST_CASE("Featurizer: the value-sidecar loader stores each model's version (end to end)")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "mdt_unit_featurizer_version";
    fs::create_directories(dir);
    const GameState s = FeaturizerBoard();
    auto score_of = [&](const char* model_json) -> long long
    {
        { std::ofstream(dir / "fzt.value.json") << model_json; }
        MulliganProfile prof;
        AttachValueSidecar(prof, dir / "fzt.profile.json");
        REQUIRE_FALSE(prof.value_model.empty());
        return prof.value_model.Score(
            ExtractMidGameFeatures(s, MidGamePlanSummary{}, prof.value_model.featurizer));
    };
    CHECK(score_of(R"({"eval_model": {"intercept": 5000, "coefs": {"lands_in_hand": 1000}}})") == 5000);
    CHECK(score_of(R"({"eval_model": {"featurizer": 2, "intercept": 5000, "coefs": {"lands_in_hand": 1000}}})") == 6000);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("Featurizer: the sidecar writer round-trips the field, and omits it for version 1")
{
    MidGameEvaluator e;
    e.intercept = 4000;
    e.coefs.assign(static_cast<int>(MidGameFeature::Count), 0);
    e.coefs[kLih] = 250;
    e.trees.push_back({ MidGameTreeNode{ kHcn, 0, 1, 2, 0 }, MidGameTreeNode{ -1, 0, -1, -1, 10 },
                        MidGameTreeNode{ -1, 0, -1, -1, -10 } });

    const nlohmann::json v1 = EvalModelToJsonObj(e);
    CHECK_FALSE(v1.contains("featurizer"));   // byte-identical to the pre-field writer
    CHECK(EvalModelFromJsonObj(v1).featurizer == kFeaturizerPlaceholder);

    e.featurizer = kFeaturizerDefinition;
    const nlohmann::json v2 = EvalModelToJsonObj(e);
    REQUIRE(v2.contains("featurizer"));
    CHECK(v2["featurizer"].get<int>() == kFeaturizerDefinition);
    const MidGameEvaluator back = EvalModelFromJsonObj(nlohmann::json{ { "eval_model", v2 } });
    CHECK(back.featurizer == kFeaturizerDefinition);
    CHECK(EvalModelToJsonObj(back) == v2);

    // The keep model carries the same field the same way.
    KeepModel km;
    km.nodes.push_back(KeepNode{});
    CHECK_FALSE(KeepModelToJsonObj(km).contains("featurizer"));
    CHECK(KeepModelFromJsonObj(KeepModelToJsonObj(km)).featurizer == kFeaturizerPlaceholder);
    km.featurizer = kFeaturizerDefinition;
    CHECK(KeepModelFromJsonObj(KeepModelToJsonObj(km)).featurizer == kFeaturizerDefinition);
}

TEST_CASE("Featurizer: a keep model's subtype_density reads under the model's own version")
{
    // Goblins: every hand card a placeholder. Version 1 counts no subtype (the historical read); 2 does.
    std::vector<Card> hand = { PhCard("Goblin Chieftain", 1), PhCard("Goblin Chieftain", 2), PhCard("Mountain", 3) };
    KeepModel km;
    FeatureSpec spec;
    spec.kind = static_cast<int>(FeatureKind::SubtypeDensity);
    spec.s    = "Goblin";
    spec.name = "subtype_Goblin";
    km.extra_features.push_back(spec);
    const int idx = static_cast<int>(KeepFeature::Count);
    const int v1 = WithArm(heurarm::PLACEHOLDER_FEATURES, 0, [&] { return ComputeKeepFeatures(hand, 0, true, km)[idx]; });
    km.featurizer = kFeaturizerDefinition;
    const int v2 = WithArm(heurarm::PLACEHOLDER_FEATURES, 0, [&] { return ComputeKeepFeatures(hand, 0, true, km)[idx]; });
    CHECK(v1 == 0);
    CHECK(v2 == 2);
}

TEST_CASE("Placeholder: shipped lever defaults (ON where the bar was met, OFF where it was not)")
{
    // Unset arms read the env defaults (the test environment sets no MTG_PLACEHOLDER_* variable).
    CHECK(PlaceholderProviderOn());
    CHECK(PlaceholderSearchOn());
    CHECK(PlaceholderSnowOn());
    CHECK(PlaceholderTectonicOn());         // unblocked by the keep axis for a Giant the plan casts
    CHECK(PlaceholderEchoOn());             // unblocked by the searched echo (MTG_ECHO_AXIS)
    CHECK_FALSE(PlaceholderManaOn());       // blocked on a sequence-aware float commit
    CHECK_FALSE(PlaceholderFeaturesOn());   // an experiment override only: the read is chosen per model (sidecar "featurizer")
}
