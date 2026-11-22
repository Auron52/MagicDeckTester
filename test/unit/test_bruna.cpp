// Unit tests for the Bruna onboarding (2026-10-05): the shroud rule for Aura SPELLS vs non-targeting
// puts, the CR 704.5m orphaned-Aura SBA (incl. the legend rule), Almost Perfect's layer-7b base set,
// Glittering Wish's multicolour filter, Somberwald Sage's creature-only float, Bruna's attack-trigger
// gather (and its provider dominance collapse), Colossification's ETB tap, and Arcanum Wings' swap.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
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

TEST_CASE("Bruna: the Sage's burst takes the colour the line's unrestricted sources are short of (MTG_PAY_BURST_LEAD)")
{
    // Seed 30000131 gi131 T4 {Bruna, Arcanum Wings}: WUBRG pip order tapped the Sage for {W}{W}{W}, the blue source
    // paid Bruna's {U}, and Wings' {1}{U} had no blue left. Sage {U}{U}{U} + Remote Farm {W}{W} + a Forest pay Bruna
    // and leave the Island and the other Forest for Wings. (A {W} Sage strands Wings on every tap order: the Island
    // pays Bruna's {U}.) A board without a bounce land: this API does not commit a payment's leftover to the pool.
    BoardBr b;
    b.Put("Somberwald Sage");
    b.Put("Remote Farm");
    b.Put("Forest");
    b.Put("Forest");
    b.Put("Island");
    ManaCost bruna; bruna.generic = 3; bruna.white = 2; bruna.blue = 1;
    ManaCost wings; wings.generic = 1; wings.blue = 1;
    ManaCost line = bruna;
    AddManaCost(line, wings);
    {
        LineUnpaidCostScope hold(line);
        REQUIRE(TapForCostShared(b.s, bruna, /*for_creature=*/true, nullptr, true));
    }
    {
        LineUnpaidCostScope hold(wings);
        CHECK(TapForCostShared(b.s, wings, /*for_creature=*/false, nullptr, true));
    }
}

// MTG_CREATURE_LEFTOVER_EXACT (default ON): inside a creature spell's payment the Sage's creature-only units pay
// FIRST, so a unit the payment over-produced is booked general whenever the bill could absorb the restricted ones.
// Bruna mana-clean gate: the legacy min(restricted produced, leftover) booking put the over-produced unit in the
// creature-only reserve and stranded the noncreature spell that followed.
TEST_CASE("Bruna: a creature payment's over-produced unit stays GENERAL when the Sage's units fit the bill")
{
    SUBCASE("s30051277 T3: Bruna, then Glittering Wish {G}{W}")
    {
        BoardBr b;
        b.Put("Sol Ring");
        b.Put("Somberwald Sage");
        b.Put("Azorius Chancery");
        b.Put("Razorverge Thicket");
        ManaCost bruna; bruna.generic = 3; bruna.white = 2; bruna.blue = 1;
        ManaCost wish;  wish.white = 1; wish.green = 1;
        ManaCost line = bruna;
        AddManaCost(line, wish);
        {
            LineUnpaidCostScope hold(line);
            REQUIRE(TapForCostShared(b.s, bruna, /*for_creature=*/true, nullptr, true));
        }
        CHECK(b.s.floating_creature_mana.Total() == 0);
        {
            LineUnpaidCostScope hold(wish);
            CHECK(TapForCostShared(b.s, wish, /*for_creature=*/false, nullptr, true));
        }
    }
    SUBCASE("s30054069 T4: Bruna, then Arcanum Wings {1}{U}")
    {
        BoardBr b;
        b.Put("Avacyn's Pilgrim");
        b.Put("Forest");
        b.Put("Somberwald Sage");
        b.Put("Azorius Chancery");
        b.Put("Razorverge Thicket");
        ManaCost bruna; bruna.generic = 3; bruna.white = 2; bruna.blue = 1;
        ManaCost wings; wings.generic = 1; wings.blue = 1;
        ManaCost line = bruna;
        AddManaCost(line, wings);
        {
            LineUnpaidCostScope hold(line);
            REQUIRE(TapForCostShared(b.s, bruna, /*for_creature=*/true, nullptr, true));
        }
        CHECK(b.s.floating_creature_mana.Total() == 0);
        {
            LineUnpaidCostScope hold(wings);
            CHECK(TapForCostShared(b.s, wings, /*for_creature=*/false, nullptr, true));
        }
    }
    SUBCASE("a genuine restricted surplus stays restricted: Sage alone for {1}{W} leaves one creature-only unit")
    {
        BoardBr b;
        b.Put("Somberwald Sage");
        ManaCost c; c.generic = 1; c.white = 1;
        REQUIRE(TapForCostShared(b.s, c, /*for_creature=*/true, nullptr, true));
        CHECK(b.s.floating_mana.Total() == 0);
        CHECK(b.s.floating_creature_mana.Total() == 1);
    }
}

// MTG_AURA_HOST_LINE_KEEP (default ON): a land Aura's host is hidden while the Aura pays only when that does not strand
// the rest of the line an unheld payment keeps.
TEST_CASE("Bruna: the Wild Growth host hold yields when it strands the rest of the line")
{
    SUBCASE("s30053757 T2: Wild Growth on Razorverge Thicket, then Arcanum Wings {1}{U} -- do NOT hold")
    {
        BoardBr b;
        const int host = b.Put("Razorverge Thicket");
        b.Put("Avacyn's Pilgrim");
        b.Put("Botanical Sanctum");
        ManaCost wg;    wg.green = 1;
        ManaCost wings; wings.generic = 1; wings.blue = 1;
        ManaCost line = wg;
        AddManaCost(line, wings);
        LineUnpaidCostScope hold(line);
        CHECK_FALSE(AuraHostHoldKeepsLine(b.s, CardBr("Wild Growth", 90), host, wg, false));
    }
    SUBCASE("EDF seed 2 T2: Wild Growth on Conservatory, then Eladamri's Call {W}{G} -- hold")
    {
        BoardBr b;
        const int host = b.Put("Conservatory");
        b.Put("Yavimaya Coast");
        ManaCost wg;   wg.green = 1;
        ManaCost call; call.white = 1; call.green = 1;
        ManaCost line = wg;
        AddManaCost(line, call);
        LineUnpaidCostScope hold(line);
        CHECK(AuraHostHoldKeepsLine(b.s, CardBr("Wild Growth", 90), host, wg, false));
    }
}

// MEASUREMENT-ONLY rule overrides (default OFF; USER-owned): MTG_PAY_LINE_CRIT_BEHIND_DORKS and MTG_PAY_LINE_CRIT_LAND +
// MTG_PAY_LINE_CRIT_OPENER. Each subcase checks the lever ON fixes the board and the lever OFF reproduces the drop.
TEST_CASE("Bruna: line-critical land variants (measurement levers)")
{
    struct ArmPin
    {
        ArmPin(int land, int opener, int behind)
        {
            heurarm::t_arm[heurarm::PAY_LINE_CRIT_LAND]         = static_cast<std::int8_t>(land);
            heurarm::t_arm[heurarm::PAY_LINE_CRIT_OPENER]       = static_cast<std::int8_t>(opener);
            heurarm::t_arm[heurarm::PAY_LINE_CRIT_BEHIND_DORKS] = static_cast<std::int8_t>(behind);
        }
        ~ArmPin()
        {
            heurarm::t_arm[heurarm::PAY_LINE_CRIT_LAND]         = -1;
            heurarm::t_arm[heurarm::PAY_LINE_CRIT_OPENER]       = -1;
            heurarm::t_arm[heurarm::PAY_LINE_CRIT_BEHIND_DORKS] = -1;
            heurarm::t_arm[heurarm::PAY_LINE_CRIT_UNTAPPER_LAST] = -1;
            heurarm::t_arm[heurarm::PAY_LINE_CRIT_HASTE_SAC_LAST] = -1;
        }
        void Amend(int untapper, int haste_sac)
        {
            heurarm::t_arm[heurarm::PAY_LINE_CRIT_UNTAPPER_LAST] = static_cast<std::int8_t>(untapper);
            heurarm::t_arm[heurarm::PAY_LINE_CRIT_HASTE_SAC_LAST] = static_cast<std::int8_t>(haste_sac);
        }
    };
    // s30051257 T2 {Sol Ring, Arcanum Wings}: Sol Ring's {1} off Avacyn's Pilgrim keeps Botanical Sanctum's {U}.
    auto sol_wings = [](bool behind) -> bool
    {
        ArmPin pin(0, 0, behind ? 1 : 0);
        BoardBr b;
        b.Put("Botanical Sanctum");
        b.Put("Avacyn's Pilgrim");
        ManaCost sol;   sol.generic = 1;
        ManaCost wings; wings.generic = 1; wings.blue = 1;
        ManaCost line = sol;
        AddManaCost(line, wings);
        {
            LineUnpaidCostScope hold(line);
            if (!TapForCostShared(b.s, sol, /*for_creature=*/false, nullptr, true)) { return false; }
        }
        b.Put("Sol Ring");
        LineUnpaidCostScope hold(wings);
        return TapForCostShared(b.s, wings, /*for_creature=*/false, nullptr, true);
    };
    SUBCASE("behind dorks: Pilgrim pays Sol Ring") { CHECK(sol_wings(true)); CHECK_FALSE(sol_wings(false)); }
    // s30051968 T2 {Forest, Sol Ring, Glittering Wish} (an opener line): Remote Farm pays the {1} and floats the {W}.
    auto sol_wish = [](bool on) -> bool
    {
        ArmPin pin(on ? 1 : 0, on ? 1 : 0, 0);
        BoardBr b;
        const int farm = b.Put("Remote Farm");
        for (Permanent& q : b.s.battlefield)
        { if (q.card.m_number == farm) { q.counters.Add(Counter::Type::Depletion, 2); } }
        b.Put("Forest");
        ManaCost sol;  sol.generic = 1;
        ManaCost wish; wish.white = 1; wish.green = 1;
        ManaCost line = sol;
        AddManaCost(line, wish);
        LineOpensUnseenScope opens(true);
        {
            LineUnpaidCostScope hold(line);
            if (!TapForCostShared(b.s, sol, /*for_creature=*/false, nullptr, true)) { return false; }
        }
        b.Put("Sol Ring");
        LineUnpaidCostScope hold(wish);
        return TapForCostShared(b.s, wish, /*for_creature=*/false, nullptr, true);
    };
    SUBCASE("opener line: Remote Farm pays Sol Ring") { CHECK(sol_wish(true)); CHECK_FALSE(sol_wish(false)); }
    // 7a AMENDMENT (MTG_PAY_LINE_CRIT_UNTAPPER_LAST). Jumbo Cactuar d0 gi1447 (seed 1311447) T3 {Forest, Overgrowth,
    // Green Sun's Zenith X=1, Nylea}: nine mana exactly -- Forest + Utopia Sprawl 2, Forest 1, the Overgrowth Forest 3,
    // and Voyaging Satyr untapping the Overgrowth Forest for 3 more. Under 7a Overgrowth's {2}{G} took the Satyr (a
    // creature ahead of the critical land), and Nylea was dropped (4 -> 7). Returns whether the Satyr is still untapped.
    auto satyr_kept = [](bool amend) -> bool
    {
        ArmPin pin(0, 0, 1);
        pin.Amend(amend ? 1 : 0, 0);
        BoardBr b;
        const int f24 = b.Put("Forest");
        const int sprawl = b.Put("Utopia Sprawl", false, false, f24);
        for (Permanent& q : b.s.battlefield)
        { if (q.card.m_number == sprawl) { q.chosen_color = static_cast<std::int8_t>(Color::Green); } }
        // Forest#18 is Overgrowth's declared host: the executor RESERVES it (taps it) across the Aura's own payment
        // (AIEngine's reserved_host + ScarceHeldOnlyScope, AuraHostHoldKeepsLine). That reservation is what makes the
        // other two Forests critical for the line's later {G}{G} -- without it the host's {G} counts as supply and
        // neither Forest is critical (the first version of this board left the host untapped and never reproduced).
        b.Put("Forest", /*tapped=*/true);
        b.Put("Forest", false, /*sick=*/true);   // this turn's land drop
        const int satyr = b.Put("Voyaging Satyr");
        ManaCost og;   og.generic = 2;  og.green = 1;
        ManaCost gsz;  gsz.generic = 1; gsz.green = 1;
        ManaCost nyl;  nyl.generic = 3; nyl.green = 1;
        ManaCost line = og;
        AddManaCost(line, gsz);
        AddManaCost(line, nyl);
        LineUnpaidCostScope hold(line);
        ScarceHeldOnlyScope host_reserved(true);
        if (!TapForCostShared(b.s, og, /*for_creature=*/false, nullptr, true)) { return false; }
        return !b.ByNum(satyr)->tapped;
    };
    SUBCASE("7a amended: Voyaging Satyr is not tapped ahead of the critical land")
    { CHECK(satyr_kept(true)); CHECK_FALSE(satyr_kept(false)); }
    // 7b AMENDMENT (MTG_PAY_LINE_CRIT_HASTE_SAC_LAST). Built for NivMizzetCombo d0 gi532 (seed 1310532) T3, but that game
    // does NOT exercise it: lands pay Signet and Brainstorm under 7b with or without it, and the Servant is sacrificed
    // for Ponder, the line's last cast, where no land is critical (the cause is the d0 plan, not the payment). In the
    // 2026-11-22 A/B (127,000 paired games) ba was byte-identical to b everywhere, and the reason is structural: the
    // amendment's predicate (crit_excluded) needs PaySacSpendableNow, which is IsPaySacSource -- a ONE-unit,
    // produces-nothing pay-sac source (Treasure). Generator Servant (sac_for_mana_amount 2) never is: its {C}{C}
    // reaches the payer as FLOATING mana from a plan SacForMana action, never as a selection candidate. So the
    // amendment cannot fire for any card in the pool, under 7b or behind the creature band.
    // Board: {Izzet Signet, Brainstorm, Ponder} off Island + Sulfur Falls + Generator Servant (both lands critical for
    // the line's {U}{U}). Returns whether the Servant is still on the battlefield after Signet is paid.
    auto servant_kept = [](bool behind, bool amend) -> bool
    {
        ArmPin pin(behind ? 0 : 1, behind ? 0 : 1, behind ? 1 : 0);
        pin.Amend(0, amend ? 1 : 0);
        BoardBr b;
        b.Put("Island");
        b.Put("Sulfur Falls");
        const int servant = b.Put("Generator Servant");
        ManaCost sig; sig.generic = 2;
        ManaCost bs;  bs.blue = 1;
        ManaCost pon; pon.blue = 1;
        ManaCost line = sig;
        AddManaCost(line, bs);
        AddManaCost(line, pon);
        LineUnpaidCostScope hold(line);
        if (!TapForCostShared(b.s, sig, /*for_creature=*/false, nullptr, true)) { return false; }
        return b.ByNum(servant) != nullptr;
    };
    SUBCASE("7b amended: inert -- Generator Servant is never a pay-sac selection candidate")
    {
        BoardBr b;
        const int servant = b.Put("Generator Servant");
        const CardDefinition* sd = CardDatabase::Instance().LookupCached(b.ByNum(servant)->card);
        REQUIRE(sd != nullptr);
        CHECK(sd->params.sac_for_mana_grants_haste);
        CHECK_FALSE(PaySacSpendableNow(b.s, *b.ByNum(servant), *sd));
        for (const bool behind : { false, true })
        { CHECK(servant_kept(behind, true)); CHECK(servant_kept(behind, false)); }
    }
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

// USER 2026-10-08: "We should tap any source we put colossification on in response." The responded-to tap
// is FREE MANA: the host's mana stays usable for the rest of the phase -- and keeps its restriction.
TEST_CASE("Bruna: Colossification's ETB tap is answered by tapping the host for mana -- usable, and restriction kept")
{
    auto coloss_on = [](BoardBr& b, int host)
    {
        Permanent aura;
        aura.card = CardBr("Colossification", 60);
        aura.controller_index = 0; aura.owner_index = 0; aura.entered_this_turn = true;
        aura.aura_attached_to = host;
        b.s.battlefield.push_back(aura);
        ResolveAuraEnterTapHost(b.s, static_cast<int>(b.s.battlefield.size()) - 1, /*respond_window=*/true);
    };
    {   // Avacyn's Pilgrim (ready): its {W} pays a later spell this phase; it never attacks.
        BoardBr b;
        const int pilgrim = b.Put("Avacyn's Pilgrim");
        coloss_on(b, pilgrim);
        CHECK(b.ByNum(pilgrim)->etb_tap_pending);
        ManaCost w; w.white = 1;
        CHECK(TapForCostShared(b.s, w, /*for_creature=*/false, nullptr, true));
        CHECK(b.ByNum(pilgrim)->tapped);
        CHECK_FALSE(CanAttackFull(*b.ByNum(pilgrim), b.s.battlefield, 0));
    }
    {   // Somberwald Sage: its three are still CREATURE-ONLY -- a noncreature cost cannot use them.
        BoardBr b;
        const int sage = b.Put("Somberwald Sage");
        coloss_on(b, sage);
        CHECK(b.ByNum(sage)->etb_tap_pending);
        ManaCost g; g.green = 1;
        CHECK_FALSE(TapForCostShared(b.s, g, /*for_creature=*/false, nullptr, true));
        CHECK(TapForCostShared(b.s, g, /*for_creature=*/true, nullptr, true));
    }
    {   // A summoning-sick host cannot tap for mana: the ETB tap simply taps it (nothing to respond with).
        BoardBr b;
        const int birds = b.Put("Birds of Paradise", /*tapped=*/false, /*sick=*/true);
        coloss_on(b, birds);
        CHECK(b.ByNum(birds)->tapped);
        CHECK_FALSE(b.ByNum(birds)->etb_tap_pending);
    }
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
    // Main phase, ready attacker, nothing lethal this turn: Colossification's tap costs this turn's
    // attack, but +20 next turn outweighs Unflinching Courage's +2 over both turns (the two-turn key
    // -- root-caused from the proof run's one branched-arm divergence).
    b.s.players[1].life = 20;
    const int km = AuraSwapPick(b.s, 0, wings, /*host_attacking=*/false);
    REQUIRE(km >= 0);
    CHECK(b.s.players[0].hand[static_cast<std::size_t>(km)].m_name.str() == "Colossification");
    // ...but when Courage's +2 makes THIS turn's attack lethal (opponent at 3, Mother 1 + 2), the
    // lethal key keeps the hasty kill.
    b.s.players[1].life = 3;
    const int kl = AuraSwapPick(b.s, 0, wings, /*host_attacking=*/false);
    REQUIRE(kl >= 0);
    CHECK(b.s.players[0].hand[static_cast<std::size_t>(kl)].m_name.str() == "Unflinching Courage");
    b.s.players[1].life = 20;
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

// ---- the authored cleanup-discard BUCKET policy (docs/design/bruna-discard-policy-proposal.md) ----
namespace
{
// A hand card is a NAME-ONLY placeholder in real play, so the policy is tested that way.
Card PlaceholderBr(const std::string& name, int number)
{
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}
GameState DiscardState(const std::vector<std::string>& hand, const std::vector<std::string>& board)
{
    BoardBr b;
    for (const std::string& n : board) { b.Put(n); }
    for (const std::string& n : hand) { b.s.players[0].hand.push_back(PlaceholderBr(n, b.next++)); }
    return b.s;
}
std::vector<std::string> ShedNames(const GameState& s)
{
    std::vector<std::string> out;
    for (int i : BrunaProvider().CleanupDiscardCandidates(s, nullptr))
    { out.push_back(s.players[0].hand[i].m_name.str()); }
    return out;
}
int PosOf(const std::vector<std::string>& v, const std::string& n)
{
    const auto it = std::find(v.begin(), v.end(), n);
    return it == v.end() ? -1 : static_cast<int>(it - v.begin());
}
}   // namespace

TEST_CASE("Bruna discard: max-MV is NOT the policy -- a surplus land sheds before Eldrazi Conscription")
{
    const GameState s = DiscardState(
        {"Eldrazi Conscription", "Forest", "Forest", "Forest", "Botanical Sanctum", "Lightning Greaves",
         "Mother of Runes", "Glittering Wish"},
        {"Forest", "Forest", "Seaside Citadel", "Botanical Sanctum"});
    const auto shed = ShedNames(s);
    REQUIRE(shed.size() == 8);                    // every hand card named
    // 4 on board + 3 kept reach the target of 7: the fourth hand land is surplus and sheds first.
    CHECK((shed[0] == "Forest" || shed[0] == "Botanical Sanctum"));
    CHECK(PosOf(shed, "Eldrazi Conscription") > PosOf(shed, "Glittering Wish"));
    CHECK(PosOf(shed, "Eldrazi Conscription") > PosOf(shed, "Mother of Runes"));
}

TEST_CASE("Bruna discard: with Bruna on our battlefield, a hand payload Aura is graveyard-equivalent")
{
    const GameState s = DiscardState(
        {"Colossification", "Glittering Wish", "Forest", "Lightning Greaves"},
        {"Bruna, Light of Alabaster", "Forest", "Forest"});
    const auto shed = ShedNames(s);
    CHECK(shed[0] == "Colossification");
}

TEST_CASE("Bruna discard: a second Bruna with one on the battlefield is dead and sheds first")
{
    const GameState s = DiscardState(
        {"Forest", "Bruna, Light of Alabaster", "Eldrazi Conscription"},
        {"Bruna, Light of Alabaster", "Forest"});
    CHECK(ShedNames(s)[0] == "Bruna, Light of Alabaster");
}

TEST_CASE("Bruna discard: kill quotas -- the biggest payload, Greaves and the first lands are kept last")
{
    const GameState s = DiscardState(
        {"Colossification", "Prodigious Growth", "Lightning Greaves", "Lightning Greaves", "Forest",
         "Open the Armory", "Mother of Runes", "Arcanum Wings"},
        {"Forest", "Botanical Sanctum"});
    const auto shed = ShedNames(s);
    REQUIRE(shed.size() == 8);
    // No gather path: two payload slots, Colossification taken first, so it is shed after Prodigious.
    CHECK(PosOf(shed, "Prodigious Growth") < PosOf(shed, "Colossification"));
    // The second Greaves is overflow; the kept one is in the tail.
    CHECK(PosOf(shed, "Lightning Greaves") < PosOf(shed, "Open the Armory"));
    // The only land in hand is a quota land: it is in the tail, behind every overflow card.
    CHECK(PosOf(shed, "Forest") > PosOf(shed, "Open the Armory"));
}

TEST_CASE("Bruna discard: a lone Azorius Chancery with no other land anywhere is a blank")
{
    const GameState s = DiscardState({"Azorius Chancery", "Colossification", "Birds of Paradise"}, {});
    CHECK(ShedNames(s)[0] == "Azorius Chancery");
}
