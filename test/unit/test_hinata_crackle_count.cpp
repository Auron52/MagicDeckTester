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
#include "ai/EngineFlags.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
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

// RAII pin of any heurarm lever on the calling thread.
struct Pin
{
    int slot; std::int8_t prev;
    Pin(int sl, bool on) : slot(sl), prev(heurarm::t_arm[sl]) { heurarm::t_arm[sl] = on ? 1 : 0; }
    ~Pin() { heurarm::t_arm[slot] = prev; }
};

// Hinata, Dawn-Crowned + `lands` Islands on our side, `hand` in hand, opponent at 20.
GameState HinataBoard(int lands, const std::vector<std::string>& hand, bool with_hinata = true,
                      const char* land = "Island")
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
    for (int i = 0; i < lands; ++i) { put(land); }
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
    // The XCandidates ESTIMATE path (MTG_HINATA_HOLD_LETHAL_BURN=0); with that lever on the same hold
    // is applied by the subset walks instead -- see the HOLD_LETHAL_BURN cases below.
    Pin estimate(heurarm::HINATA_HOLD_LETHAL_BURN, false);
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

// ---- MTG_HINATA_HOLD_LETHAL_BURN: the hold's "wins THIS turn" judged by the subset walks -------------
namespace
{
// Hinata + Ornithopter of Paradise (both able to attack) + 6 Mountains + 6 Islands, land drop spent,
// pre-combat, `hand` in hand. 13 mana: Crackle's max X is 4 ({9}{R}{R} after her {1}-per-target -- 3
// targets: the face and the two creatures) with 2 left, which is exactly Magma Opus at one face
// damage ({U}{R}: six distinct targets). Hinata swings for 4; the Ornithopter is held for mana.
//   Crackle X=4 alone + attack      = 20 + 4     = 24
//   Crackle X=4 + Magma (1) + attack = 20 + 1 + 4 = 25
// `salt` only keeps the per-thread Solve memo (keyed on the state) from serving one arm to the other.
GameState BurnBoard(const std::vector<std::string>& hand, int opp_life, int salt)
{
    EnsureCardsHc();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = 20 + salt;
    s.players[1].life     = opp_life;
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
    put("Hinata, Dawn-Crowned");
    put("Ornithopter of Paradise");
    for (int i = 0; i < 6; ++i) { put("Mountain"); put("Island"); }
    s.players[0].lands_played_this_turn = 1;
    for (const std::string& n : hand) { Card c = DefHc(n.c_str()).card; c.m_number = num++; s.players[0].hand.push_back(c); }
    for (int k = 0; k < 10; ++k) { Card c = DefHc("Island").card; c.m_number = 900 + k; s.players[0].library.push_back(c); }
    return s;
}

bool CastsHc(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

TurnSolver::Plan GreedyHc(const GameState& s)
{
    return TurnSolver::Solve(s, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
}

bool AnyPlanCasts(const GameState& s, const std::string& name)
{
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    { if (CastsHc(p, name)) { return true; } }
    return false;
}
}   // namespace

TEST_CASE("HOLD_LETHAL_BURN: the provider hands the hold to the subset walks (on) or estimates it (off)")
{
    const HinataProvider hp;
    const CardDefinition& crackle = DefHc(kCrackle);
    const GameState lone = BurnBoard({ kCrackle, "Magma Opus" }, 25, 0);
    const GameState two  = BurnBoard({ kCrackle, kCrackle }, 25, 0);
    {
        Pin on(heurarm::HINATA_HOLD_LETHAL_BURN, true);
        CHECK(hp.HoldCastUnlessLethal(lone, crackle));
        CHECK(hp.XCandidates(lone, crackle, 4) == std::vector<int>{ 4 });   // offered; the walks hold it
        CHECK_FALSE(hp.HoldCastUnlessLethal(two, crackle));                // a real second copy: free
        CHECK_FALSE(hp.HoldCastUnlessLethal(lone, DefHc("Magma Opus")));   // only the Crackle is held
        CHECK_FALSE(DefaultProvider().HoldCastUnlessLethal(lone, crackle)); // every other deck: never
    }
    {
        Pin off(heurarm::HINATA_HOLD_LETHAL_BURN, false);
        CHECK_FALSE(hp.HoldCastUnlessLethal(lone, crackle));
        CHECK(hp.XCandidates(lone, crackle, 4).empty());                   // 20 + 4 < 25 -> held here
    }
}

TEST_CASE("HOLD_LETHAL_BURN: a Crackle that is lethal only WITH a Magma Opus co-cast is cast (on), held (off)")
{
    const HinataProvider hp;
    // Lethal with the co-cast: 25.
    {
        Pin on(heurarm::HINATA_HOLD_LETHAL_BURN, true);
        GameState s = BurnBoard({ kCrackle, "Magma Opus" }, 25, 1);
        s.m_provider = &hp;
        const TurnSolver::Plan g = GreedyHc(s);
        CHECK(CastsHc(g, kCrackle));
        CHECK(CastsHc(g, "Magma Opus"));
        CHECK(AnyPlanCasts(s, kCrackle));
    }
    {
        Pin off(heurarm::HINATA_HOLD_LETHAL_BURN, false);
        GameState s = BurnBoard({ kCrackle, "Magma Opus" }, 25, 2);
        s.m_provider = &hp;
        CHECK_FALSE(CastsHc(GreedyHc(s), kCrackle));   // the estimate (24 < 25) held the kill
        CHECK_FALSE(AnyPlanCasts(s, kCrackle));
    }
    // Not lethal even with the co-cast (26): no plan casts the lone Crackle under either arm.
    for (bool arm : { true, false })
    {
        Pin pin(heurarm::HINATA_HOLD_LETHAL_BURN, arm);
        GameState s = BurnBoard({ kCrackle, "Magma Opus" }, 26, arm ? 3 : 4);
        s.m_provider = &hp;
        CHECK_FALSE(CastsHc(GreedyHc(s), kCrackle));
        CHECK_FALSE(AnyPlanCasts(s, kCrackle));
    }
    // A second copy lifts the hold: a chip Crackle is a legal plan again under both arms.
    for (bool arm : { true, false })
    {
        Pin pin(heurarm::HINATA_HOLD_LETHAL_BURN, arm);
        GameState s = BurnBoard({ kCrackle, kCrackle }, 26, arm ? 5 : 6);
        s.m_provider = &hp;
        CHECK(AnyPlanCasts(s, kCrackle));
    }
}

// ---- MTG_HINATA_HOLD_NEEDS_HINATA (default OFF, the USER's call): hold only while the combo is live ---
TEST_CASE("HOLD_NEEDS_HINATA: no Hinata in play or castable -> a lone Crackle is plain burn (on); held (off)")
{
    const HinataProvider hp;
    const CardDefinition& crackle = DefHc(kCrackle);
    // 4 Islands, no Hinata anywhere: X = 2 is 10 + 0 attack < 20 -> the estimate holds.
    const GameState none     = HinataBoard(4, { kCrackle, kSpasm }, /*with_hinata=*/false);
    // ...Hinata in HAND with four any-colour lands: {1}{U}{R}{W} pays -> castable, the combo is live.
    // (Four Islands would NOT do since MTG_HINATA_CASTABLE_MANA: no {R} or {W} -- see
    // test_restrictor_order.cpp.)
    const GameState castable = HinataBoard(4, { kCrackle, "Hinata, Dawn-Crowned" }, /*with_hinata=*/false,
                                           "Forbidden Orchard");
    // ...and in play.
    const GameState in_play  = HinataBoard(4, { kCrackle, kSpasm });
    for (bool lethal_burn : { true, false })
    {
        Pin lb(heurarm::HINATA_HOLD_LETHAL_BURN, lethal_burn);
        auto held = [&](const GameState& s)
        { return lethal_burn ? hp.HoldCastUnlessLethal(s, crackle) : hp.XCandidates(s, crackle, 2).empty(); };
        {
            Pin off(heurarm::HINATA_HOLD_NEEDS_HINATA, false);   // the default: hold regardless
            CHECK(held(none));
            CHECK(held(castable));
            CHECK(held(in_play));
        }
        {
            Pin on(heurarm::HINATA_HOLD_NEEDS_HINATA, true);
            CHECK_FALSE(held(none));
            CHECK(held(castable));
            CHECK(held(in_play));
        }
    }
    // Default is OFF.
    CHECK_FALSE(heurarm::Flag(heurarm::HINATA_HOLD_NEEDS_HINATA, EnvOn("MTG_HINATA_HOLD_NEEDS_HINATA")));
}

// ---- MTG_HINATA_CASTABLE_MANA: "castable from hand" is a question about mana, not about sources ------------
namespace
{
// `board` (untapped, ours, not summoning sick), `hand` in hand -- no Hinata on the battlefield.
GameState CastableBoard(const std::vector<std::string>& board, const std::vector<std::string>& hand)
{
    EnsureCardsHc();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    int num = 1;
    for (const std::string& n : board)
    {
        Permanent p;
        p.card              = DefHc(n.c_str()).card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
    }
    for (const std::string& n : hand) { s.players[0].hand.push_back(PhHc(n, num++)); }
    return s;
}
}   // namespace

TEST_CASE("HINATA_CASTABLE_MANA: the board's real mana, not its source count, decides 'castable from hand'")
{
    const char* kHin = "Hinata, Dawn-Crowned";
    // 2HG gi 254's turn-3 board: three sources, five mana ({C}{C} + {U}{R} + any) -- {1}{U}{R}{W} pays.
    const GameState rich = CastableBoard({ "Sol Ring", "Izzet Boilerworks", "Forbidden Orchard" }, { kHin });
    // Four Islands: four sources reach her mana value, but nothing makes {R} or {W}.
    const GameState blue = CastableBoard({ "Island", "Island", "Island", "Island" }, { kHin });
    {
        Pin on(heurarm::HINATA_CASTABLE_MANA, true);
        CHECK(HinataCastableFromHand(rich));
        CHECK_FALSE(HinataCastableFromHand(blue));
    }
    {
        Pin off(heurarm::HINATA_CASTABLE_MANA, false);   // the legacy source count
        CHECK_FALSE(HinataCastableFromHand(rich));
        CHECK(HinataCastableFromHand(blue));
    }
    // No Hinata in hand: never castable.
    CHECK_FALSE(HinataCastableFromHand(CastableBoard({ "Sol Ring", "Izzet Boilerworks", "Forbidden Orchard" },
                                                     { kCrackle })));
    // ...and it is what the provider's untap-ritual gate reads under the same-subset credit: Reality Spasm
    // is offered on the five-mana board (she resolves first), not on the four-Islands one.
    const HinataProvider hp;
    Pin credit(heurarm::HINATA_SUBSET_CREDIT, true);
    Pin on(heurarm::HINATA_CASTABLE_MANA, true);
    CHECK(hp.ShouldEmitUntapRitual(rich));
    CHECK_FALSE(hp.ShouldEmitUntapRitual(blue));
}

// ---- MTG_HELD_CAST_UNBUDGETED: an UNBUDGETED search is offered the held line; budgeted + greedy hold it ----
// The hold was a gate at every budget, so a chip-Crackle line was unfindable even at --budget-ms 0
// (Hinata2 s3030 gi 27: T3 chip + two Soulfires = T7, held = T8 at d8 b0). Opp at 26: Crackle X=4 + Magma
// Opus + Hinata's swing = 25, so the lone Crackle is NOT lethal and the hold applies.
TEST_CASE("HELD_CAST_UNBUDGETED: an unbudgeted search offers a lone non-lethal Crackle; budgeted and greedy hold it")
{
    const HinataProvider hp;
    Pin lb(heurarm::HINATA_HOLD_LETHAL_BURN, true);
    // Budgeted (the latch is down -- every regression / shipped cell): held, exactly as before.
    {
        Pin on(heurarm::HELD_CAST_UNBUDGETED, true);
        GameState s = BurnBoard({ kCrackle, "Magma Opus" }, 26, 11);
        s.m_provider = &hp;
        CHECK_FALSE(AnyPlanCasts(s, kCrackle));
        CHECK_FALSE(CastsHc(GreedyHc(s), kCrackle));
    }
    // Unbudgeted SEARCH, lever on: the search branch list carries the chip line...
    {
        UnbudgetedSearchScope unbudgeted(true);
        Pin on(heurarm::HELD_CAST_UNBUDGETED, true);
        GameState s = BurnBoard({ kCrackle, "Magma Opus" }, 26, 12);
        s.m_provider = &hp;
        CHECK(AnyPlanCasts(s, kCrackle));
        // ...while the greedy policy (Solve: the d0 runner, rollouts past the horizon) still holds it.
        CHECK_FALSE(CastsHc(GreedyHc(s), kCrackle));
    }
    // Unbudgeted search, lever =0: the gate, byte-identical to before the lever.
    {
        UnbudgetedSearchScope unbudgeted(true);
        Pin off(heurarm::HELD_CAST_UNBUDGETED, false);
        GameState s = BurnBoard({ kCrackle, "Magma Opus" }, 26, 13);
        s.m_provider = &hp;
        CHECK_FALSE(AnyPlanCasts(s, kCrackle));
    }
    // The latch is scoped: it is down again once the scope unwinds.
    CHECK(g_unbudgeted_search == 0);
    // Default ON.
    CHECK(heurarm::Flag(heurarm::HELD_CAST_UNBUDGETED, EnvOn("MTG_HELD_CAST_UNBUDGETED", true)));
}
