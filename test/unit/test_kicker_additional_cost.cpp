// Unit tests for KICKER (CR 702.33) -- Saproling Migration, the engine's first kicker card.
// Design: docs/design/kicker-additional-cost.md. User-directed 2026-10-16.
//
// EVERY CASE HERE GUARDS A SILENT FAILURE. There is no arrangement of these bugs that crashes or
// prints a warning: the screen finishes, produces a plausible-looking delta, and the delta is wrong
// in a direction nobody can see from the number. That is what makes them unit tests rather than
// something the regression fingerprint would catch -- Saproling Migration is in no committed deck,
// so no GT key covers it at all.
//
//  1. THE KICKED COST IS ADDED, NOT SUBSTITUTED (CR 601.2b). This is the one place kicker differs
//     from the evoke_cost / bestow_cost machinery it is otherwise built on, and the only place
//     where getting it wrong FLATTERS the card instead of penalising it: substitute and Saproling
//     Migration's kicked mode costs {4} instead of {5}{G} -- cheaper in generic terms than the
//     printed cost plus a green pip, so the search would kick nearly unconditionally and the
//     re-measured count would be nonsense in the card's favour. Asserted as an inequality against
//     BOTH operands, not just as "== 5 generic", so the assertion states the rule and not an
//     arithmetic coincidence.
//
//  2. THE KICKED PAYLOAD REPLACES the unkicked one ("create four of those tokens INSTEAD"), so the
//     face reads cast_creates_tokens 4 -- not 2, and not 6. Six is the plausible bug: a reader who
//     treats the kicked clause as additive gets twelve bodies under a Doubling Season where the
//     card makes eight.
//
//  3. THE TOKEN'S IDENTITY IS UNCHANGED by kicking. Only the COUNT differs, so power, toughness,
//     subtype and colour must come through the face untouched -- a face built by listing fields
//     rather than copying the def is how one quietly goes missing, and a colourless or subtype-less
//     Saproling is invisible to Sporecrown Thallid and to Shroofus.
//
//  4. A KICKED FACE CANNOT RE-KICK. kicker_cost must be cleared on it, or enumeration would offer
//     to kick the kicked face and the search could stack kickers for arbitrary token counts.
//
//  5. THE PRINTED CARD IS WHAT GOES TO THE GRAVEYARD (CR 608.2m). The face is a cast-time
//     convenience and must not leak into a zone. Besides being plainly wrong, a mis-named card in
//     the graveyard SPLITS EOT dominance folding -- two lines reaching an identical board stop
//     folding -- which costs search time while looking like nothing.
//
//  6. ...and the physical copy's m_number survives the mapping, because a kicked card is the SAME
//     card (the adventure path keeps the number for the identical reason).
//
//  7. THE WHOLE THING IS INERT FOR EVERY OTHER CARD. No non-kicker card gains a "(Kicked)" face,
//     and -- the sharp one -- FUNGUS FROLIC MUST NOT, because it carries a byte-identical parameter
//     set to Saproling Migration's unkicked half and differs only in having no kicker. It is the
//     exact card a param added to the wrong JSON entry would land on, and a surgical-edit mistake
//     already tried to put it there once.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"

#include <string>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

}  // namespace

TEST_CASE("kicker: the kicked cost is printed PLUS kicker, never instead of it (CR 601.2b)")
{
    EnsureCardsLoaded();
    const CardDefinition& base   = Def("Saproling Migration");
    const CardDefinition& kicked = Def("Saproling Migration (Kicked)");

    REQUIRE(base.params.kicker_cost.has_value());
    const ManaCost& kick = *base.params.kicker_cost;

    // The card: {1}{G} with kicker {4}.
    CHECK(base.card.m_mana_cost.generic == 1);
    CHECK(base.card.m_mana_cost.green   == 1);
    CHECK(kick.generic == 4);
    CHECK(kick.ManaValue() == 4);

    // ADDED: {1}{G} + {4} = {5}{G}, MV 6.
    CHECK(kicked.card.m_mana_cost.generic == 5);
    CHECK(kicked.card.m_mana_cost.green   == 1);
    CHECK(kicked.card.m_mana_cost.ManaValue() == 6);

    // The RULE, stated as the rule rather than as arithmetic: an additional cost makes the spell
    // dearer than the printed cost AND dearer than the kicker alone. A substitution bug fails the
    // second of these, which is the one that would flatter the card.
    CHECK(kicked.card.m_mana_cost.ManaValue() > base.card.m_mana_cost.ManaValue());
    CHECK(kicked.card.m_mana_cost.ManaValue() > kick.ManaValue());
    CHECK(kicked.card.m_mana_cost.ManaValue()
          == base.card.m_mana_cost.ManaValue() + kick.ManaValue());

    // The printed entry is untouched -- the face is a copy, not a mutation of the card we ship.
    CHECK(base.card.m_mana_cost.ManaValue() == 2);
}

TEST_CASE("kicker: the kicked clause REPLACES the token count, and the token is otherwise identical")
{
    EnsureCardsLoaded();
    const CardDefinition& base   = Def("Saproling Migration");
    const CardDefinition& kicked = Def("Saproling Migration (Kicked)");

    CHECK(base.params.cast_creates_tokens        == 2);
    CHECK(base.params.kicked_creates_tokens      == 4);
    // "...create four of those tokens INSTEAD" -- 4, not 2 and not 2+4.
    CHECK(kicked.params.cast_creates_tokens      == 4);
    // Consumed by the synthesis, so nothing downstream can read it twice.
    CHECK(kicked.params.kicked_creates_tokens    == 0);

    // "those tokens" = the same 1/1 green Saproling. Only the count differs.
    CHECK(kicked.params.cast_created_token_power     == base.params.cast_created_token_power);
    CHECK(kicked.params.cast_created_token_toughness == base.params.cast_created_token_toughness);
    CHECK(kicked.params.cast_created_token_color     == base.params.cast_created_token_color);
    REQUIRE(kicked.params.cast_created_token_subtypes == base.params.cast_created_token_subtypes);
    CHECK(kicked.params.cast_created_token_power  == 1);
    CHECK(kicked.params.cast_created_token_color  == "G");
    REQUIRE(kicked.params.cast_created_token_subtypes.size() == 1);
    CHECK(kicked.params.cast_created_token_subtypes[0] == "Saproling");

    // Still a Sorcery -- kicking does not change the card's types.
    CHECK(kicked.card.IsSorcery());
}

TEST_CASE("kicker: a kicked face cannot be kicked again")
{
    EnsureCardsLoaded();
    // Without this, enumeration would emit a kick variant for the face itself and the search could
    // stack kickers -- paying {4} repeatedly for a token count nothing in the rules allows.
    CHECK_FALSE(Def("Saproling Migration (Kicked)").params.kicker_cost.has_value());
    // ...and the face knows which printed card it came from, which is what the graveyard tail uses.
    CHECK(Def("Saproling Migration (Kicked)").params.kicked_base_name == "Saproling Migration");
    CHECK(Def("Saproling Migration").params.kicked_base_name.empty());
}

TEST_CASE("kicker: the PRINTED card goes to the graveyard, not the face (CR 608.2m)")
{
    EnsureCardsLoaded();
    const CardDefinition& kicked = Def("Saproling Migration (Kicked)");

    // The resolved copy, as the cast leaves it: the face's card carrying a real per-copy number.
    Card resolved        = kicked.card;
    resolved.m_number    = 417;
    REQUIRE(resolved.m_name.str() == "Saproling Migration (Kicked)");

    const Card filed = UnkickedSpellCard(kicked, resolved);
    CHECK(filed.m_name.str() == "Saproling Migration");   // one card, not two
    CHECK(filed.m_number     == 417);                     // ...and the SAME physical copy
    CHECK(filed.IsSorcery());

    // Inert for an ordinary resolution: a def with no kicked_base_name returns its card unchanged,
    // which is what makes this byte-identical for all 539 other cards.
    const CardDefinition& frolic = Def("Fungus Frolic");
    Card fr = frolic.card; fr.m_number = 9;
    const Card frolic_filed = UnkickedSpellCard(frolic, fr);
    CHECK(frolic_filed.m_name.str() == "Fungus Frolic");
    CHECK(frolic_filed.m_number     == 9);
}

TEST_CASE("kicker: inert for every card without a kicker")
{
    EnsureCardsLoaded();
    // FUNGUS FROLIC IS THE SHARP CASE. Its parameter set is byte-identical to Saproling Migration's
    // unkicked half ({2}{G}, cast_creates_tokens 2, same 1/1 green Saproling), differing only in
    // having no kicker -- so it is exactly the card a param added to the wrong cards.json entry
    // lands on, and a text-anchored edit did try to put one there before an assertion caught it.
    // A kicked Frolic would hand the deck a second four-token spell nobody authored.
    CHECK_FALSE(Def("Fungus Frolic").params.kicker_cost.has_value());
    CHECK(Def("Fungus Frolic").params.kicked_creates_tokens == 0);
    CHECK(CardDatabase::Instance().Lookup("Fungus Frolic (Kicked)") == nullptr);
    CHECK(Def("Fungus Frolic").params.cast_creates_tokens == 2);   // its own count is untouched

    // No other card in the pool grew a face either.
    CHECK(CardDatabase::Instance().Lookup("Saproling Burst (Kicked)") == nullptr);
    CHECK(CardDatabase::Instance().Lookup("Doubling Season (Kicked)") == nullptr);
    CHECK(CardDatabase::Instance().Lookup("Spore Swarm (Kicked)")     == nullptr);

    // Saproling Migration is the engine's ONLY kicker card. If that stops being true, the
    // "compared unconditionally" shortcuts taken in LineSpec::kickeds need re-reading, because they
    // are justified by no saved reference being able to play one.
    int with_kicker = 0;
    for (const auto& name : CardDatabase::Instance().AllNames())
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        if (d != nullptr && d->params.kicker_cost.has_value()) { ++with_kicker; }
    }
    CHECK(with_kicker == 1);
}

TEST_CASE("kicker: ManaCost::PlusAdditionalCost adds, and refuses a cost it cannot add")
{
    // The primitive, independent of any card -- this is the function a future kicker will reuse.
    ManaCost base; base.generic = 1; base.green = 1;
    ManaCost kick; kick.generic = 4;

    const ManaCost tot = base.PlusAdditionalCost(kick);
    CHECK(tot.generic == 5);
    CHECK(tot.green   == 1);
    CHECK(tot.ManaValue() == 6);
    CHECK(base.generic == 1);        // the printed cost is not mutated

    // Per-colour addition, so a coloured kicker works the day one is needed.
    ManaCost ck; ck.white = 2; ck.generic = 1;
    const ManaCost t2 = base.PlusAdditionalCost(ck);
    CHECK(t2.white == 2);
    CHECK(t2.generic == 2);
    CHECK(t2.green == 1);

    // IsFlat is the gate CardDatabase throws on. Each exotic pip kind is a fixed-capacity side
    // table ALSO baked into the flat ints, so it cannot be merged field-wise -- the throw exists so
    // that lands as an error rather than as a cost that silently pays wrong.
    CHECK(base.IsFlat());
    CHECK(kick.IsFlat());
    { ManaCost c = kick; c.hybrid_count    = 1;    CHECK_FALSE(c.IsFlat()); }
    { ManaCost c = kick; c.phyrexian_count = 1;    CHECK_FALSE(c.IsFlat()); }
    { ManaCost c = kick; c.twobrid_count   = 1;    CHECK_FALSE(c.IsFlat()); }
    { ManaCost c = kick; c.snow_pips       = 1;    CHECK_FALSE(c.IsFlat()); }
    { ManaCost c = kick; c.has_x           = true; CHECK_FALSE(c.IsFlat()); }

    // Only the ADDED cost must be flat: a base carrying exotic pips keeps its metadata through the
    // add, with its slot indices still matching the flat ints they were baked into.
    ManaCost hb = base; hb.hybrid_count = 1; hb.hybrid_pair[0] = 0x24;
    const ManaCost t3 = hb.PlusAdditionalCost(kick);
    CHECK(t3.hybrid_count   == 1);
    CHECK(t3.hybrid_pair[0] == 0x24);
    CHECK(t3.generic        == 5);
}
