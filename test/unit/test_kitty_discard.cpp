// Unit cover for EquipmentProvider's BUCKETED cleanup discard (USER design, 2026-08-22).
//
// Why this file exists. The bucket rule is a pile of CONDITIONAL ranks -- a card's shed position
// depends on which other cards are in hand and on the board -- and the conditions are read off card
// behaviour, not off card names. That is exactly the shape where a plausible-but-wrong reading
// survives a 7,200-game A/B without a flicker: it changes which of two similar cards is pitched in a
// rare cleanup, and the win-turn average absorbs it.
//
// It already happened once. The first cut treated Armored Skyhunter as a way to "cheat equip" a
// Colossus Hammer, and the USER caught it by inspection: Skyhunter's attach applies to an Equipment
// put from "the top six cards of your LIBRARY", so it does nothing for a Hammer sitting in hand.
// Stoneforge is out for the mirror-image reason -- cards.json records that its put "dodges only the
// CAST cost -- equipping is still the Equip action / metalcraft", and the Hammer's {1} cast was never
// the problem. The discriminating pair below pins that distinction so it cannot silently regress.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsKe()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
        return true;
    }();
    REQUIRE(loaded);
}

Card MakeCardKe(const std::string& name, int number)
{
    const CardDefinition* def = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(def != nullptr, "card not in cards.json: ", name);
    Card c    = def->card;
    c.m_number = number;
    return c;
}

GameState MakeHandBoard(const std::vector<std::string>& hand,
                        const std::vector<std::string>& board)
{
    EnsureCardsKe();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(MakeCardKe(n, num++)); }
    for (const std::string& n : board)
    {
        Permanent p;
        p.card              = MakeCardKe(n, num++);
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
    }
    return s;
}

std::vector<int> Rank(const GameState& s)
{
    return EquipmentProvider().CleanupDiscardCandidates(s, nullptr);
}

// Position of a card NAME in the shed order. Lower = shed sooner; every card appears (the base
// ranking appends whatever the provider did not name), so "kept" means "sits late", not "absent".
int ShedPos(const GameState& s, const std::vector<int>& order, const std::string& name)
{
    for (int k = 0; k < static_cast<int>(order.size()); ++k)
    { if (s.players[0].hand[order[k]].m_name.str() == name) { return k; } }
    return -1;
}

// All positions of a card NAME, in shed order. Needed where the question is HOW MANY copies were
// shed rather than whether one was -- two Plains cannot be told apart by name alone.
std::vector<int> ShedPositions(const GameState& s, const std::vector<int>& order,
                               const std::string& name)
{
    std::vector<int> out;
    for (int k = 0; k < static_cast<int>(order.size()); ++k)
    { if (s.players[0].hand[order[k]].m_name.str() == name) { out.push_back(k); } }
    return out;
}

}   // namespace

TEST_CASE("kitty discard: a cheat-equipper makes Colossus Hammer keepable")
{
    // Puresteel Paladin's metalcraft turns Equip {8} into {0}, so the Hammer is the best card here
    // and the surplus Equipment is the Jitte.
    const std::vector<std::string> hand = {
        "Puresteel Paladin", "Colossus Hammer", "Bonesplitter", "Shadowspear",
        "Umezawa's Jitte", "Plains", "Plains", "Plains"
    };
    GameState s = MakeHandBoard(hand, {});
    const std::vector<int> order = Rank(s);

    const int hammer = ShedPos(s, order, "Colossus Hammer");
    const int jitte  = ShedPos(s, order, "Umezawa's Jitte");
    REQUIRE(hammer >= 0);
    REQUIRE(jitte >= 0);
    CHECK(jitte == 0);          // the surplus equipment is shed first
    CHECK(hammer > jitte);      // ...and the Hammer is kept ahead of it
}

TEST_CASE("kitty discard: Armored Skyhunter does NOT make Colossus Hammer keepable")
{
    // USER 2026-08-22: "Armored Skyhunter doesn't help for equipping equipment in hand." Its attach
    // only reaches an Equipment put from the top six of the LIBRARY. Same hand as above with the
    // Paladin swapped for a Skyhunter: with no bypass, Equip {8} is unpayable and the Hammer becomes
    // the deck's deadest card in hand -- so the shed order must INVERT.
    const std::vector<std::string> hand = {
        "Armored Skyhunter", "Colossus Hammer", "Bonesplitter", "Shadowspear",
        "Umezawa's Jitte", "Plains", "Plains", "Plains"
    };
    GameState s = MakeHandBoard(hand, {});
    const std::vector<int> order = Rank(s);

    const int hammer = ShedPos(s, order, "Colossus Hammer");
    const int jitte  = ShedPos(s, order, "Umezawa's Jitte");
    REQUIRE(hammer >= 0);
    REQUIRE(jitte >= 0);
    CHECK(hammer == 0);         // shed the Hammer, not the Jitte
    CHECK(hammer < jitte);
}

TEST_CASE("kitty discard: Balan IS a cheat-equipper (attach-all bypasses equip cost)")
{
    const std::vector<std::string> hand = {
        "Balan, Wandering Knight", "Colossus Hammer", "Bonesplitter", "Shadowspear",
        "Umezawa's Jitte", "Plains", "Plains", "Plains"
    };
    GameState s = MakeHandBoard(hand, {});
    const std::vector<int> order = Rank(s);
    CHECK(ShedPos(s, order, "Colossus Hammer") > ShedPos(s, order, "Umezawa's Jitte"));
}

TEST_CASE("kitty discard: a board cheat-equipper counts too")
{
    // Six Equipment and no creature in hand, so the equipment bucket actually BINDS and the Hammer's
    // conditional rank decides which card is cut. Only the board differs between the halves.
    const std::vector<std::string> hand = {
        "Colossus Hammer", "Bonesplitter", "O-Naginata", "Shadowspear",
        "Umezawa's Jitte", "Loxodon Warhammer", "Plains", "Plains"
    };

    SUBCASE("no bypass anywhere: the Hammer is the dead card")
    {
        GameState s = MakeHandBoard(hand, {});
        const std::vector<int> order = Rank(s);
        CHECK(ShedPos(s, order, "Colossus Hammer") == 0);
    }

    SUBCASE("a Paladin already in play: the Hammer becomes the best card in hand")
    {
        GameState s = MakeHandBoard(hand, { "Puresteel Paladin" });
        const std::vector<int> order = Rank(s);
        CHECK(ShedPos(s, order, "Loxodon Warhammer") == 0);
        CHECK(ShedPos(s, order, "Colossus Hammer") > ShedPos(s, order, "Loxodon Warhammer"));
    }
}

TEST_CASE("kitty discard: removal is in no bucket and sheds first")
{
    const std::vector<std::string> hand = {
        "Swords to Plowshares", "Puresteel Paladin", "Kor Duelist", "Bonesplitter",
        "Colossus Hammer", "Plains", "Plains", "Plains"
    };
    GameState s = MakeHandBoard(hand, {});
    const std::vector<int> order = Rank(s);
    CHECK(ShedPos(s, order, "Swords to Plowshares") == 0);
}

TEST_CASE("kitty discard: Sol Ring is never shed, surplus lands are")
{
    // "always keep sol ring" -- and it counts toward the 3-source cap, so with four Plains beside it
    // two of them are surplus.
    const std::vector<std::string> hand = {
        "Sol Ring", "Plains", "Plains", "Plains", "Plains",
        "Bonesplitter", "Puresteel Paladin", "Kor Duelist"
    };
    GameState s = MakeHandBoard(hand, {});
    const std::vector<int> order = Rank(s);
    const int sol   = ShedPos(s, order, "Sol Ring");
    const int plain = ShedPos(s, order, "Plains");
    REQUIRE(sol >= 0);
    REQUIRE(plain >= 0);
    CHECK(plain == 0);      // a surplus land leads the shed order
    CHECK(sol > plain);     // Sol Ring survives it
}

TEST_CASE("kitty discard: creatures cap at two -- one enabler plus one double striker")
{
    // Four creatures, two slots: Puresteel (best enabler) and Kor Duelist (cheapest double striker).
    // Kemba and Stoneforge are the surplus -- Kemba is the fallback only when no striker exists.
    const std::vector<std::string> hand = {
        "Puresteel Paladin", "Kor Duelist", "Kemba, Kha Regent", "Stoneforge Mystic",
        "Bonesplitter", "Plains", "Plains", "Plains"
    };
    GameState s = MakeHandBoard(hand, {});
    const std::vector<int> order = Rank(s);

    const int paladin = ShedPos(s, order, "Puresteel Paladin");
    const int duelist = ShedPos(s, order, "Kor Duelist");
    const int kemba   = ShedPos(s, order, "Kemba, Kha Regent");
    const int forge   = ShedPos(s, order, "Stoneforge Mystic");
    REQUIRE(paladin >= 0);
    REQUIRE(duelist >= 0);
    CHECK(kemba < paladin);
    CHECK(kemba < duelist);
    CHECK(forge < paladin);
    CHECK(forge < duelist);
}

// ---- The ALLOCATION (USER, 2026-08-22) --------------------------------------------------------
// "3 mana sources at most and 2 equipment. If some of these are not necessary or can't be filled we
// keep more equipment." Plus "You keep 1 of each" for the creature bucket -- 2 + 3 + 2 = 7.

TEST_CASE("kitty discard: a full hand allocates 2 creatures + 3 sources + 2 equipment")
{
    const std::vector<std::string> hand = {
        "Puresteel Paladin", "Kor Duelist", "Bonesplitter", "Shadowspear",
        "Umezawa's Jitte", "Plains", "Plains", "Plains"
    };
    GameState s = MakeHandBoard(hand, {});
    const std::vector<int> order = Rank(s);
    // Three sources and both creature roles are filled, so equipment gets exactly its floor of two
    // and the third Equipment is the surplus.
    CHECK(ShedPos(s, order, "Umezawa's Jitte") == 0);
    CHECK(ShedPos(s, order, "Bonesplitter") > 0);
    CHECK(ShedPos(s, order, "Shadowspear") > 0);
}

TEST_CASE("kitty discard: land-light keeps ONE OF EACH creature -- the named pair is not split")
{
    // USER 2026-08-22: "you actually keep both if you have puresteel and kor duelist" / "You keep 1
    // of each." An earlier cut of mine dropped to a single creature on a land-light hand, which
    // split exactly the pair the spec names first -- three mana across two turns, and the deck's
    // engine. Two lands, so the source bucket under-fills and equipment takes the slack; neither
    // creature may be touched.
    const std::vector<std::string> hand = {
        "Puresteel Paladin", "Kor Duelist", "Bonesplitter", "O-Naginata",
        "Shadowspear", "Umezawa's Jitte", "Plains", "Plains"
    };
    GameState s = MakeHandBoard(hand, {});
    const std::vector<int> order = Rank(s);
    const int jitte = ShedPos(s, order, "Umezawa's Jitte");
    CHECK(jitte == 0);                                              // the surplus is Equipment...
    CHECK(ShedPos(s, order, "Puresteel Paladin") > jitte);          // ...never the enabler
    CHECK(ShedPos(s, order, "Kor Duelist") > jitte);                // ...never the double striker
}

TEST_CASE("kitty discard: sources that are not needed become equipment slots")
{
    // The residual, shown on ONE hand with only the board varying -- which is the "count what is on
    // board already" half. Four Equipment in hand and two Plains.
    const std::vector<std::string> hand = {
        "Puresteel Paladin", "Kor Duelist", "Bonesplitter", "O-Naginata",
        "Shadowspear", "Umezawa's Jitte", "Plains", "Plains"
    };

    SUBCASE("empty board: the source bucket wants both lands, so equipment gets three")
    {
        GameState s = MakeHandBoard(hand, {});
        const std::vector<int> order = Rank(s);
        CHECK(ShedPos(s, order, "Umezawa's Jitte") == 0);           // the 4th Equipment is cut
        CHECK(ShedPos(s, order, "Plains") > 0);                     // the lands are kept
    }

    SUBCASE("three Plains already in play: the hand lands are unnecessary, so equipment gets them")
    {
        GameState s = MakeHandBoard(hand, { "Plains", "Plains", "Plains" });
        const std::vector<int> order = Rank(s);
        CHECK(ShedPos(s, order, "Plains") == 0);                    // now the LANDS are the surplus
        CHECK(ShedPos(s, order, "Umezawa's Jitte") > ShedPos(s, order, "Plains"));
    }
}

TEST_CASE("kitty discard: sources fill to FOUR MANA, not to a flat card count")
{
    // USER 2026-08-22: "in the rarer case where we have 2-3 [mana] out already you might want to keep
    // 1-2 mana sources, so you can hit 4 mana. 4 mana is pretty much the cap." Counting the board
    // against a flat three-CARD cap kept nothing behind three lands, stranding Balan {2}{W}{W} and
    // Armored Skyhunter {3}{W}. Same hand throughout; only the board's mana moves.
    const std::vector<std::string> hand = {
        "Puresteel Paladin", "Kor Duelist", "Bonesplitter", "O-Naginata",
        "Shadowspear", "Umezawa's Jitte", "Plains", "Plains"
    };

    SUBCASE("three mana out: keep ONE more land to reach four")
    {
        GameState s = MakeHandBoard(hand, { "Plains", "Plains", "Plains" });
        const std::vector<int> order = Rank(s);
        const std::vector<int> plains = ShedPositions(s, order, "Plains");
        REQUIRE(plains.size() == 2);
        CHECK(plains[0] == 0);      // one Plains is surplus...
        CHECK(plains[1] > 1);       // ...and the other is KEPT, to make the fourth mana
    }

    SUBCASE("four mana out: the curve is covered, so keep none")
    {
        GameState s = MakeHandBoard(hand, { "Plains", "Plains", "Plains", "Plains" });
        const std::vector<int> order = Rank(s);
        const std::vector<int> plains = ShedPositions(s, order, "Plains");
        REQUIRE(plains.size() == 2);
        CHECK(plains[0] == 0);      // nothing above four mana is wanted, so BOTH go
        CHECK(plains[1] == 1);
    }

    SUBCASE("a Sol Ring in play is two of the four on its own")
    {
        GameState s = MakeHandBoard(hand, { "Plains", "Sol Ring" });
        const std::vector<int> order = Rank(s);
        const std::vector<int> plains = ShedPositions(s, order, "Plains");
        REQUIRE(plains.size() == 2);
        CHECK(plains[0] == 0);      // 1 + 2 on board, so one land completes the four
        CHECK(plains[1] > 1);
    }
}

TEST_CASE("kitty discard: four mana of the WRONG colour is not enough")
{
    // USER 2026-08-22: "the bounceland also counts as 2 sources, though not in combination with just
    // sol ring." Sol Ring is {C}{C} and Boros Garrison is {R}{W} -- four mana between them and ONE
    // white, which casts none of Puresteel {W}{W}, Kemba {1}{W}{W} or Balan {2}{W}{W}. So hitting the
    // mana target is not on its own a reason to stop keeping lands.
    const std::vector<std::string> hand = {
        "Puresteel Paladin", "Kor Duelist", "Bonesplitter", "O-Naginata",
        "Shadowspear", "Umezawa's Jitte", "Plains", "Plains"
    };

    SUBCASE("Sol Ring + bounceland: four mana, one white -- keep a Plains anyway")
    {
        GameState s = MakeHandBoard(hand, { "Sol Ring", "Boros Garrison" });
        const std::vector<int> order = Rank(s);
        const std::vector<int> plains = ShedPositions(s, order, "Plains");
        REQUIRE(plains.size() == 2);
        CHECK(plains[0] == 0);
        CHECK(plains[1] > 1);       // the second white source is kept despite mana already at four
    }

    SUBCASE("bounceland + Plains: four mana AND two white -- now we can stop")
    {
        GameState s = MakeHandBoard(hand, { "Boros Garrison", "Plains", "Plains" });
        const std::vector<int> order = Rank(s);
        const std::vector<int> plains = ShedPositions(s, order, "Plains");
        REQUIRE(plains.size() == 2);
        CHECK(plains[0] == 0);      // colour is covered on board, so both hand lands are surplus
        CHECK(plains[1] == 1);
    }
}

TEST_CASE("kitty discard: a lone bounceland is not a mana source at all")
{
    // USER 2026-08-22: "the Garrison cannot be played without a plains." Its ETB returns a land you
    // control; with no other land that is itself, so the drop is wasted. LandPlay's chooser refuses
    // to offer it in that state, so the bucket must not keep it either.
    SUBCASE("no other land anywhere: the Garrison is dead weight and sheds")
    {
        const std::vector<std::string> hand = {
            "Puresteel Paladin", "Kor Duelist", "Bonesplitter", "O-Naginata",
            "Shadowspear", "Umezawa's Jitte", "Loxodon Warhammer", "Boros Garrison"
        };
        GameState s = MakeHandBoard(hand, {});
        const std::vector<int> order = Rank(s);
        // Dead card, so it leads the shed order ahead of even the worst Equipment.
        CHECK(ShedPos(s, order, "Boros Garrison") == 0);
    }

    SUBCASE("a Plains beside it: now playable, and worth two mana")
    {
        const std::vector<std::string> hand = {
            "Puresteel Paladin", "Kor Duelist", "Bonesplitter", "O-Naginata",
            "Shadowspear", "Umezawa's Jitte", "Plains", "Boros Garrison"
        };
        GameState s = MakeHandBoard(hand, {});
        const std::vector<int> order = Rank(s);
        CHECK(ShedPos(s, order, "Boros Garrison") > 0);
        CHECK(ShedPos(s, order, "Plains") > 0);
    }

    SUBCASE("a land already in play makes it playable too")
    {
        const std::vector<std::string> hand = {
            "Puresteel Paladin", "Kor Duelist", "Bonesplitter", "O-Naginata",
            "Shadowspear", "Umezawa's Jitte", "Loxodon Warhammer", "Boros Garrison"
        };
        GameState s = MakeHandBoard(hand, { "Plains" });
        const std::vector<int> order = Rank(s);
        CHECK(ShedPos(s, order, "Boros Garrison") > 0);
        CHECK(ShedPos(s, order, "Loxodon Warhammer") == 0);   // the worst Equipment goes instead
    }
}

TEST_CASE("kitty discard: a creature role covered on board frees its slot too")
{
    // The other half of "not necessary": an enabler in play means the hand copy is redundant, and
    // the freed slot flows to equipment exactly as an unneeded land does.
    const std::vector<std::string> hand = {
        "Puresteel Paladin", "Kor Duelist", "Bonesplitter", "O-Naginata",
        "Shadowspear", "Umezawa's Jitte", "Plains", "Plains"
    };
    GameState s = MakeHandBoard(hand, { "Puresteel Paladin" });
    const std::vector<int> order = Rank(s);
    CHECK(ShedPos(s, order, "Puresteel Paladin") == 0);             // the redundant enabler goes
    CHECK(ShedPos(s, order, "Umezawa's Jitte") > 0);                // and all four Equipment stay
    CHECK(ShedPos(s, order, "Kor Duelist") > 0);
}

TEST_CASE("kitty discard: with no double striker, Kemba is the second creature")
{
    const std::vector<std::string> hand = {
        "Puresteel Paladin", "Kemba, Kha Regent", "Stoneforge Mystic", "Armored Skyhunter",
        "Bonesplitter", "Plains", "Plains", "Plains"
    };
    GameState s = MakeHandBoard(hand, {});
    const std::vector<int> order = Rank(s);
    // Kemba is kept as the fallback partner, so it must outlast the other spare enablers.
    CHECK(ShedPos(s, order, "Kemba, Kha Regent") > ShedPos(s, order, "Stoneforge Mystic"));
    CHECK(ShedPos(s, order, "Kemba, Kha Regent") > ShedPos(s, order, "Armored Skyhunter"));
}

// ---------------------------------------------------------------------------------------------
// THE PURESTEEL/SRAM LIST (v3). USER RULING 2026-10-08, MTG_KE_BUCKET_V3 default ON.
//
// These exist because the bug they pin was invisible to every A/B we ran: the v1 role tables named
// no card the v3 list plays except Puresteel, so 4x Sram -- the DRAW ENGINE -- plus Adelbert, Dwalin
// and Sigarda's Aid all fell through to `unbucketed` and were shed FIRST, ahead of a surplus Bone
// Saw. A rule that fires once per ~120 games in play cannot be caught by a win-turn average; it is
// caught here, or not at all. See docs/design/kitty-discard-buckets-are-stale-for-v3.md.
// ---------------------------------------------------------------------------------------------

TEST_CASE("kitty discard v3: Sram is an ENABLER, not the first thing pitched")
{
    // THE REGRESSION TEST FOR THE ORIGINAL DEFECT. Before the fix Sram was unbucketed and led the
    // shed order; now the surplus {0} shield goes first and Sram is kept.
    const std::vector<std::string> hand = {
        "Sram, Senior Edificer", "Bone Saw", "Kite Shield", "Kite Shield",
        "Cathar's Shield", "Plains", "Plains", "Plains"
    };
    GameState s = MakeHandBoard(hand, {});
    const std::vector<int> order = Rank(s);
    const int sram    = ShedPos(s, order, "Sram, Senior Edificer");
    const int surplus = ShedPos(s, order, "Cathar's Shield");
    REQUIRE(sram >= 0);
    REQUIRE(surplus >= 0);
    CHECK(surplus == 0);        // the surplus {0} equipment is shed first...
    CHECK(sram > surplus);      // ...and Sram is NOT (it used to be position 0)
}

TEST_CASE("kitty discard v3: Puresteel outranks Sram -- Sram is shed first of the two")
{
    // USER: "Puresteel is better, so Sram is shed first." Two Puresteels and a Sram fill both
    // enabler slots with Puresteels; the draw trigger stacks per copy, only metalcraft is redundant.
    const std::vector<std::string> hand = {
        "Puresteel Paladin", "Puresteel Paladin", "Sram, Senior Edificer",
        "Bone Saw", "Kite Shield", "Plains", "Plains", "Plains"
    };
    GameState s = MakeHandBoard(hand, {});
    const std::vector<int> order = Rank(s);
    const int sram = ShedPos(s, order, "Sram, Senior Edificer");
    const std::vector<int> pures = ShedPositions(s, order, "Puresteel Paladin");
    REQUIRE(sram >= 0);
    REQUIRE(pures.size() == 2);
    CHECK(sram < pures[0]);     // Sram is pitched before EITHER Puresteel
}

TEST_CASE("kitty discard v3: only ONE Sram is useful -- the second is surplus")
{
    // USER: "only 1 Sram is useful so extras would be shed."
    const std::vector<std::string> hand = {
        "Sram, Senior Edificer", "Sram, Senior Edificer", "Bone Saw",
        "Kite Shield", "Kite Shield", "Plains", "Plains", "Plains"
    };
    GameState s = MakeHandBoard(hand, {});
    const std::vector<int> order = Rank(s);
    const std::vector<int> srams = ShedPositions(s, order, "Sram, Senior Edificer");
    REQUIRE(srams.size() == 2);
    // One Sram sits late (kept); the other is shed ahead of it -- they are not both kept.
    CHECK(srams[0] < srams[1]);
}

TEST_CASE("kitty discard v3: Sigarda's Aid is kept IFF there is no Puresteel")
{
    // USER: "Sigarda's aid should be kept IFF we don't have Puresteel. In that case it takes the
    // second spot with Sram." Sigarda's Aid is an ENCHANTMENT, so is_equipment is false and it used
    // to fall through to `unbucketed` and shed first in BOTH cases.
    SUBCASE("no Puresteel: it takes the second spot beside Sram")
    {
        const std::vector<std::string> hand = {
            "Sram, Senior Edificer", "Sigarda's Aid", "Bone Saw", "Kite Shield",
            "Cathar's Shield", "Plains", "Plains", "Plains"
        };
        GameState s = MakeHandBoard(hand, {});
        const std::vector<int> order = Rank(s);
        const int aid     = ShedPos(s, order, "Sigarda's Aid");
        const int surplus = ShedPos(s, order, "Cathar's Shield");
        REQUIRE(aid >= 0);
        REQUIRE(surplus >= 0);
        CHECK(aid > surplus);   // kept: it sits later than the pitched equipment
    }
    SUBCASE("Puresteel present: the Aid loses the slot to it")
    {
        const std::vector<std::string> hand = {
            "Puresteel Paladin", "Sram, Senior Edificer", "Sigarda's Aid",
            "Bone Saw", "Kite Shield", "Plains", "Plains", "Plains"
        };
        GameState s = MakeHandBoard(hand, {});
        const std::vector<int> order = Rank(s);
        const int aid  = ShedPos(s, order, "Sigarda's Aid");
        const int pure = ShedPos(s, order, "Puresteel Paladin");
        REQUIRE(aid >= 0);
        REQUIRE(pure >= 0);
        CHECK(aid < pure);      // the Aid goes before Puresteel, i.e. it did not earn the slot
    }
}

TEST_CASE("kitty discard v3: Bone Saw leads the {0} tier")
{
    // USER: "...prioritizing bone saw".
    //
    // THE TEST HAS TO BE DECISIVE, and the obvious version is not: asserting Bone Saw sheds later
    // than every other {0} shield fails even on a correct implementation, because the ones that are
    // KEPT are appended by the base ranking in an order this rule does not control. So instead Bone
    // Saw is placed LAST in hand order, where a rank that did nothing would leave it tied on rank
    // and pitched first by index. Exactly one {0} shield is surplus here, and the question is which.
    const std::vector<std::string> hand = {
        "Puresteel Paladin", "Kite Shield", "Cathar's Shield", "Accorder's Shield",
        "Bone Saw", "Plains", "Plains", "Plains"
    };
    GameState s = MakeHandBoard(hand, {});
    const std::vector<int> order = Rank(s);
    const int saw      = ShedPos(s, order, "Bone Saw");
    const int accorder = ShedPos(s, order, "Accorder's Shield");
    REQUIRE(saw >= 0);
    REQUIRE(accorder >= 0);
    // Without the Bone Saw rank all four tie and the LAST-indexed one is pitched -- which is the Saw.
    CHECK(accorder == 0);     // the surplus {0} is the last-ranked shield, not the Saw
    CHECK(saw > accorder);    // ...and the Saw is kept
}

TEST_CASE("kitty discard v3: Adelbert vs Dwalin turns on AUTO-EQUIP")
{
    // USER: "Adelbert Steiner would be prioritized over Dwalin if we have no way to auto equip.
    // Otherwise Dwalin is probably a bit better." Which is how the cards read -- Adelbert's +1/+1
    // counts Equipment you CONTROL, so he needs no attach; Dwalin's hone counters need one carried.
    SUBCASE("no auto-equip: Adelbert is preferred")
    {
        const std::vector<std::string> hand = {
            "Adelbert Steiner", "Dwalin, Weaponmaster", "Bone Saw", "Kite Shield",
            "Cathar's Shield", "Plains", "Plains", "Plains"
        };
        GameState s = MakeHandBoard(hand, {});
        const std::vector<int> order = Rank(s);
        const int ad = ShedPos(s, order, "Adelbert Steiner");
        const int dw = ShedPos(s, order, "Dwalin, Weaponmaster");
        REQUIRE(ad >= 0);
        REQUIRE(dw >= 0);
        CHECK(dw < ad);         // Dwalin is pitched first, Adelbert is the one kept
    }
    SUBCASE("Puresteel on board supplies auto-equip: Dwalin is preferred")
    {
        const std::vector<std::string> hand = {
            "Adelbert Steiner", "Dwalin, Weaponmaster", "Bone Saw", "Kite Shield",
            "Cathar's Shield", "Plains", "Plains", "Plains"
        };
        GameState s = MakeHandBoard(hand, { "Puresteel Paladin", "Bone Saw", "Kite Shield", "Cathar's Shield" });
        const std::vector<int> order = Rank(s);
        const int ad = ShedPos(s, order, "Adelbert Steiner");
        const int dw = ShedPos(s, order, "Dwalin, Weaponmaster");
        REQUIRE(ad >= 0);
        REQUIRE(dw >= 0);
        CHECK(ad < dw);         // the order flips
    }
}
