// Unit cover for IsCrackleCountSpell (MTG_CRACKLE_COUNT_EXACT, heurarm CRACKLE_COUNT_EXACT) and the two
// places its answer reaches Hinata2's play (docs/design/reality-spasm-crackle-classification.md):
//   * the predicate itself -- Crackle with Power yes, Reality Spasm NO (the legacy
//     `x_damage_multiplier > 0` test said yes: CardDatabase defaults the multiplier to 1), an ordinary
//     {X} burn no;
//   * HinataProvider::XCandidates' hold-a-lone-Crackle rule -- a Reality Spasm in hand is the combo's
//     OTHER piece, not a second Crackle, so a lone non-lethal Crackle stays held beside it;
//   * Reality Spasm's cost with Hinata in play -- "Untap X target permanents" has exactly X targets, so
//     her {1}-per-target discount is X and RS costs {U}{U}; the count model (face + declared extras)
//     must never price it.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsHc()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefHc(const char* name)
{
    EnsureCardsHc();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

// Hand cards are NAME-ONLY placeholders in real play (DeckLoader::MakePlaceholder).
Card PhHc(const std::string& name, int number)
{
    DefHc(name.c_str());
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

// RAII pin of the lever on the calling thread: 1 = exact (the default), 0 = the legacy predicate.
struct CrackleArm
{
    std::int8_t prev;
    explicit CrackleArm(bool on) : prev(heurarm::t_arm[heurarm::CRACKLE_COUNT_EXACT])
    { heurarm::t_arm[heurarm::CRACKLE_COUNT_EXACT] = on ? 1 : 0; }
    ~CrackleArm() { heurarm::t_arm[heurarm::CRACKLE_COUNT_EXACT] = prev; }
};

// Hinata, Dawn-Crowned + `lands` Islands on our side, `hand` in hand, opponent at 20.
GameState HinataBoard(int lands, const std::vector<std::string>& hand, bool with_hinata = true)
{
    EnsureCardsHc();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    int num = 1;
    auto put = [&](const char* n)
    {
        Permanent p;
        p.card              = DefHc(n).card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
    };
    if (with_hinata) { put("Hinata, Dawn-Crowned"); }
    for (int i = 0; i < lands; ++i) { put("Island"); }
    for (const std::string& n : hand) { s.players[0].hand.push_back(PhHc(n, num++)); }
    s.players[1].life = 20;
    return s;
}

const char* kCrackle = "Crackle with Power";
const char* kSpasm   = "Reality Spasm";

}   // namespace

TEST_CASE("IsCrackleCountSpell: Crackle yes, Reality Spasm no, an ordinary {X} burn no")
{
    const CardDefinition& crackle = DefHc(kCrackle);
    const CardDefinition& spasm   = DefHc(kSpasm);
    const CardDefinition& quake   = DefHc("Rolling Earthquake");   // {X} damage, not a per-target discount

    // An ordinary {X} burn: Crackle's own shape without the per-target-discount scaling.
    CardDefinition xburn = crackle;
    xburn.params.discount_targets_scale_x = false;
    xburn.params.x_damage_multiplier      = 1;
    // "X damage to each of up to X targets": the count model applies even at the DEFAULT multiplier
    // (why the fix keys on the template, not on a "multiplier was set" bit).
    CardDefinition plain_count = crackle;
    plain_count.params.x_damage_multiplier = 1;

    // The data the defect hinged on: RS carries the DEFAULT multiplier 1, so `> 0` held for it.
    CHECK(spasm.params.discount_targets_scale_x);
    CHECK(spasm.params.x_damage_multiplier == 1);
    CHECK(spasm.tmpl != CardTemplate::DirectDamage);

    SUBCASE("exact (default ON)")
    {
        CrackleArm on(true);
        CHECK(IsCrackleCountSpell(crackle));
        CHECK_FALSE(IsCrackleCountSpell(spasm));
        CHECK_FALSE(IsCrackleCountSpell(quake));
        CHECK_FALSE(IsCrackleCountSpell(xburn));
        CHECK(IsCrackleCountSpell(plain_count));
    }
    SUBCASE("legacy (=0): Reality Spasm reads as a Crackle -- the defect, pinned")
    {
        CrackleArm off(false);
        CHECK(IsCrackleCountSpell(crackle));
        CHECK(IsCrackleCountSpell(spasm));
        CHECK_FALSE(IsCrackleCountSpell(quake));
        CHECK_FALSE(IsCrackleCountSpell(xburn));
    }
}

TEST_CASE("HinataProvider::XCandidates holds a lone non-lethal Crackle even with a Reality Spasm in hand")
{
    const HinataProvider hp;
    const CardDefinition& crackle = DefHc(kCrackle);

    // X = 2: 5X = 10 + Hinata's 4 = 14 < 20 -> non-lethal.
    const GameState with_rs = HinataBoard(4, { kCrackle, kSpasm });
    {
        CrackleArm on(true);
        CHECK(hp.XCandidates(with_rs, crackle, 2).empty());              // HELD
    }
    {
        CrackleArm off(false);
        CHECK(hp.XCandidates(with_rs, crackle, 2) == std::vector<int>{ 2 });   // legacy: RS = "copy 2"
    }

    // A real second Crackle still frees one copy for a chip cast, under both arms.
    const GameState two = HinataBoard(4, { kCrackle, kCrackle });
    for (bool arm : { true, false })
    {
        CrackleArm pin(arm);
        CHECK(hp.XCandidates(two, crackle, 2) == std::vector<int>{ 2 });
    }

    // Lethal now (5X + attack >= 20 at X = 4) is always offered, RS in hand or not.
    for (bool arm : { true, false })
    {
        CrackleArm pin(arm);
        CHECK(hp.XCandidates(with_rs, crackle, 4) == std::vector<int>{ 4 });
    }

    // A lone Crackle with no RS: held under both arms (the arms differ ONLY on the RS count).
    const GameState lone = HinataBoard(4, { kCrackle, "Island" });
    for (bool arm : { true, false })
    {
        CrackleArm pin(arm);
        CHECK(hp.XCandidates(lone, crackle, 2).empty());
    }
}

TEST_CASE("Reality Spasm with Hinata in play: X targets -> {X} discounted away -> {U}{U}; never the count model")
{
    const CardDefinition& spasm   = DefHc(kSpasm);
    const CardDefinition& crackle = DefHc(kCrackle);

    // Hinata + 3 Islands = 4 permanents; RS untaps X = 3 of them (exactly 3 targets).
    const GameState s = HinataBoard(3, { kSpasm });
    REQUIRE(HinataInPlay(s));
    CHECK(HinataGenericDiscount(spasm, s, 3) == 3);   // the auto-max path every RS site uses
    CHECK(HinataGenericDiscount(spasm, s, 4) == 4);   // X = every permanent: still X targets

    // The expression every cost site uses: `IsCrackleCountSpell(def) ? crackle_targets : -1`. Had a
    // declared count ever reached an RS cast (no path produces one today), the legacy predicate would
    // have priced its "face + extras" -- {1} off for X = 3 untap targets, an illegal overpay.
    auto site = [&](const CardDefinition& d, int x, int declared)
    { return HinataGenericDiscount(d, s, x, IsCrackleCountSpell(d) ? declared : -1); };
    {
        CrackleArm on(true);
        CHECK(site(spasm, 3, 0) == 3);
        // RS's printed {X}{U}{U} at X = 3, after her discount: {U}{U}.
        int generic = 3 * std::max(1, spasm.card.m_mana_cost.x_pips) - site(spasm, 3, 0);
        CHECK(generic == 0);
    }
    {
        CrackleArm off(false);
        CHECK(site(spasm, 3, 0) == 1);   // the latent hazard the legacy predicate left armed
    }

    // Crackle keeps its declared-count discount under both arms: face + `declared` extras, capped at X.
    for (bool arm : { true, false })
    {
        CrackleArm pin(arm);
        CHECK(site(crackle, 3, 0) == 1);
        CHECK(site(crackle, 3, 1) == 2);
        CHECK(site(crackle, 3, 5) == 3);
    }

    // No Hinata: no discount for either spell.
    const GameState bare = HinataBoard(3, { kSpasm }, /*with_hinata=*/false);
    CHECK(HinataGenericDiscount(spasm, bare, 3) == 0);
    CHECK(HinataGenericDiscount(crackle, bare, 3, 2) == 0);
}
