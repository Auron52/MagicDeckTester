#include "CardDatabase.h"
#include "../core/EnvFlags.h"
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// FNV-1a over the canonical (name/oracle-stripped) cards.json entry -- see DefHash() in the
// header. Deterministic across machines (std::hash is not), so two builds on different hosts
// agree on whether a card's behaviourally-relevant data changed. Kept identical in spirit to
// the analyzer's Fnv helper; the value only needs to be self-consistent across runs.
static uint64_t CardDefHash(const json& entry)
{
    json canon = entry;
    canon.erase("name");         // cosmetic: identity, not behaviour
    canon.erase("oracle_text");  // cosmetic: human text, engine never reads it
    // nlohmann::json objects are key-ordered (std::map), so dump() is canonical regardless of
    // source key order or whitespace -- parse-then-dump normalizes both.
    const std::string s = canon.dump();
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

// ---- CardTemplate helpers ----

CardTemplate CardTemplateFromString(const std::string& name)
{
    if (name == "basic_land")       { return CardTemplate::BasicLand; }
    if (name == "vanilla_creature") { return CardTemplate::VanillaCreature; }
    if (name == "mana_dork")        { return CardTemplate::ManaDork; }
    if (name == "direct_damage")    { return CardTemplate::DirectDamage; }
    if (name == "counter_spell")    { return CardTemplate::CounterSpell; }
    if (name == "removal")          { return CardTemplate::Removal; }
    if (name == "draw_spell")       { return CardTemplate::DrawSpell; }
    if (name == "draw_x")           { return CardTemplate::DrawX; }
    if (name == "pump_spell")       { return CardTemplate::PumpSpell; }
    if (name == "lord_effect")        { return CardTemplate::LordEffect; }
    if (name == "haste")              { return CardTemplate::Haste; }
    if (name == "draw_until_nonland") { return CardTemplate::DrawUntilNonland; }
    if (name == "custom")             { return CardTemplate::None; }
    throw std::runtime_error("Unknown card template: " + name);
}

const char* CardTemplateToString(CardTemplate t)
{
    switch (t)
    {
        case CardTemplate::BasicLand:       return "basic_land";
        case CardTemplate::VanillaCreature: return "vanilla_creature";
        case CardTemplate::ManaDork:        return "mana_dork";
        case CardTemplate::DirectDamage:    return "direct_damage";
        case CardTemplate::CounterSpell:    return "counter_spell";
        case CardTemplate::Removal:         return "removal";
        case CardTemplate::DrawSpell:       return "draw_spell";
        case CardTemplate::DrawX:           return "draw_x";
        case CardTemplate::PumpSpell:       return "pump_spell";
        case CardTemplate::LordEffect:      return "lord_effect";
        case CardTemplate::Haste:            return "haste";
        case CardTemplate::DrawUntilNonland: return "draw_until_nonland";
        default:                             return "custom";
    }
}

// ---- CardDatabase ----

// Eager singleton storage. Instance() (inline in the header) just returns this, so the
// hot path pays no per-call init guard. Default-constructed (empty map) at static-init
// time; populated by LoadFromJson from main. See the header for why this is safe.
CardDatabase CardDatabase::s_instance;

void CardDatabase::LoadFromJson(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file)
    {
        throw std::runtime_error("Cannot open card data file: " + path.string());
    }

    json root = json::parse(file);
    if (!root.contains("cards"))
    {
        throw std::runtime_error("Card data file missing 'cards' array: " + path.string());
    }

    for (const json& entry : root["cards"])
    {
        CardDefinition def;
        def.card   = BuildCardFromJson(entry);
        def.tier   = entry.value("template", std::string("custom")) == "custom"
                     ? CardTier::Custom : CardTier::Template;
        def.tmpl   = CardTemplateFromString(entry.value("template", std::string("custom")));
        if (entry.contains("parameters"))
        {
            def.params = BuildParamsFromJson(entry["parameters"]);
        }
        // Pre-intern the subtype names used to build TOKEN subtypes at runtime, so the
        // worker threads only ever do read-only id lookups (SubtypeSet::operator=) and
        // never insert into the shared SubtypeRegistry mid-search. (A card's own subtypes
        // are already interned in BuildCardFromJson via SubtypeSet::push_back.)
        SubtypeRegistry& reg = SubtypeRegistry::Instance();
        for (const std::string& s : def.params.attack_token_subtypes) { reg.Intern(s); }
        for (const std::string& s : def.params.upkeep_token_subtypes) { reg.Intern(s); }
        for (const std::string& s : def.params.tap_token_subtypes)    { reg.Intern(s); }
        for (const std::string& s : def.params.cast_token_subtypes)   { reg.Intern(s); }
        m_def_hash[def.card.m_name] = CardDefHash(entry);
        // Synthesize the BACK face of a modal double-faced LAND (Pathway) as its own DB entry: a
        // single-colour land the player may choose to play instead of the front. Derived entirely
        // from the front's mdfc_back_* params, so no second JSON entry is hand-authored, and its
        // colour is read live off the played permanent's name like any other land (see PlayLandByName).
        if (!def.params.mdfc_back_name.empty())
        {
            const std::string bn = def.params.mdfc_back_name;
            CardDefinition back = def;                      // Land type/subtypes copied from the front
            back.card.m_name = bn;                          // InternedName assign
            back.card.m_def  = nullptr;                     // name-derived def cache must reset
            back.card.RehashName();
            back.params.produces = def.params.mdfc_back_produces;
            back.params.mdfc_back_name.clear();             // the back face has no further face
            back.params.mdfc_back_produces.clear();
            // Spell//land MDFC (Turntimber Symbiosis // Turntimber, Serpentine Wood): the front is
            // a NONLAND card, so the back cannot inherit its types/cost/params -- synthesize a clean
            // Land face instead: {T}: Add <back_produces>, entering tapped unless mdfc_back_pay_life
            // is paid (shock-land semantics via etb_pay_life_to_untap). A land front (Pathway) keeps
            // the legacy copy above, byte-identical.
            if (!def.card.IsLand())
            {
                Card land;
                land.m_name = bn;
                land.RehashName();
                land.AddType(CardType::Land);
                back.card = std::move(land);
                back.tmpl = CardTemplate::BasicLand;
                CardParams bp;
                bp.produces             = def.params.mdfc_back_produces;
                bp.etb_pay_life_to_untap = def.params.mdfc_back_pay_life;
                back.params = std::move(bp);
            }
            m_def_hash[bn] = CardDefHash(entry) ^ std::hash<std::string>{}(bn);
            m_cards[bn] = std::move(back);
        }
        // BESTOW (Gnarled Scarhide, CR 702.103): the same card may be cast for its printed cost as
        // a CREATURE, or for its bestow cost as an AURA. Synthesize the aura face as a separate,
        // separately-named CardDefinition -- exactly the MDFC-back trick above, and for the same
        // reason: every downstream reader (AuraBonusFor, the lord scans, the ETB token cascade,
        // combat) resolves a permanent's params through LookupCached on its NAME, so an aura face
        // that is genuinely a different DB entry needs zero special-casing anywhere else. A
        // bestowed Scarhide is therefore not a creature, is not a Minotaur on the battlefield, does
        // not trigger Sethron and is not buffed by lords -- all of which is correct.
        //
        // The aura face keeps the card's aura_* grant and its bestow cost, drops the creature P/T
        // and types, and clears bestow_cost so it can never re-bestow.
        if (def.params.bestow_cost.has_value())
        {
            const std::string bname = def.card.m_name.str() + " (Bestowed)";
            CardDefinition aura;
            Card ac;
            ac.m_name = bname;
            ac.RehashName();
            ac.AddType(CardType::Enchantment);
            for (const std::string& st : def.card.m_subtypes) { ac.m_subtypes.push_back(st); }
            ac.m_subtypes.push_back("Aura");
            for (int ci = 0; ci < 5; ++ci)
            { if (def.card.HasColor(static_cast<Color>(ci))) { ac.AddColor(static_cast<Color>(ci)); } }
            ac.m_mana_cost = def.params.bestow_cost.value();
            aura.card  = std::move(ac);
            aura.tier  = def.tier;
            aura.tmpl  = CardTemplate::None;   // resolves through the generic permanent-enter path
            CardParams ap2;
            ap2.is_aura           = true;
            ap2.aura_power_bonus  = def.params.aura_power_bonus;
            ap2.aura_tough_bonus  = def.params.aura_tough_bonus;
            ap2.aura_grants_lifelink = def.params.aura_grants_lifelink;
            ap2.aura_enchant_requires = def.params.aura_enchant_requires;
            aura.params = std::move(ap2);
            m_def_hash[bname] = CardDefHash(entry) ^ std::hash<std::string>{}(bname);
            m_cards[bname] = std::move(aura);
        }
        m_cards[def.card.m_name] = std::move(def);
    }
    RebuildInternedIndex();
}

// See the header: canonical-interned-pointer -> def index over m_cards. Interning here also
// guarantees every DB name is already in the registry before any worker thread runs, so the
// hot-path read (InternedName ctor on an existing name / the find below) never inserts.
void CardDatabase::RebuildInternedIndex()
{
    m_by_name_ptr.clear();
    m_by_name_ptr.reserve(m_cards.size());
    for (const auto& kv : m_cards)
    { m_by_name_ptr[InternedName::Intern(kv.first)] = &kv.second; }
}

std::vector<std::string> CardDatabase::MdfcBackFaceNames() const
{
    std::vector<std::string> out;
    for (const auto& [name, def] : m_cards)
    {
        if (!def.params.mdfc_back_name.empty()) { out.push_back(def.params.mdfc_back_name); }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void CardDatabase::Register(const std::string& name, CardFactory factory)
{
    CardDefinition def = factory();
    def.tier = CardTier::Custom;
    def.tmpl = CardTemplate::None;
    m_cards[name] = std::move(def);
    RebuildInternedIndex();
}

const CardDefinition* CardDatabase::Lookup(const std::string& name) const
{
    auto it = m_cards.find(name);
    if (it == m_cards.end())
    {
        return nullptr;
    }
    // The database is a lifetime singleton (Instance()) whose entries are never
    // erased or relocated after load, so handing out a pointer into the map is
    // safe. Returning a pointer (vs. the old by-value std::optional<CardDefinition>)
    // avoids deep-copying the whole definition -- including its many vector<string>
    // members -- on every one of the 150+ hot-path call sites. (callgrind 2026-06-19:
    // that by-value copy was ~38% of a search-heavy game.)
    return &it->second;
}

bool CardDatabase::IsImplemented(const std::string& name) const
{
    return m_cards.count(name) > 0;
}

std::vector<std::string> CardDatabase::AllNames() const
{
    std::vector<std::string> names;
    names.reserve(m_cards.size());
    for (const auto& pair : m_cards)
    {
        names.push_back(pair.first);
    }
    return names;
}

// ---- JSON helpers ----

static CardType CardTypeFromString(const std::string& s)
{
    if (s == "Land")        { return CardType::Land; }
    if (s == "Creature")    { return CardType::Creature; }
    if (s == "Instant")     { return CardType::Instant; }
    if (s == "Sorcery")     { return CardType::Sorcery; }
    if (s == "Enchantment") { return CardType::Enchantment; }
    if (s == "Artifact")    { return CardType::Artifact; }
    if (s == "Planeswalker"){ return CardType::Planeswalker; }
    if (s == "Battle")      { return CardType::Battle; }
    throw std::runtime_error("Unknown card type: " + s);
}

static Keyword KeywordFromString(const std::string& s)
{
    if (s == "Haste")         { return Keyword::Haste; }
    if (s == "Flying")        { return Keyword::Flying; }
    if (s == "Trample")       { return Keyword::Trample; }
    if (s == "Deathtouch")    { return Keyword::Deathtouch; }
    if (s == "Lifelink")      { return Keyword::Lifelink; }
    if (s == "First Strike")  { return Keyword::FirstStrike; }
    if (s == "Double Strike") { return Keyword::DoubleStrike; }
    if (s == "Vigilance")     { return Keyword::Vigilance; }
    if (s == "Reach")         { return Keyword::Reach; }
    if (s == "Defender")      { return Keyword::Defender; }
    if (s == "Indestructible"){ return Keyword::Indestructible; }
    if (s == "Flash")         { return Keyword::Flash; }
    if (s == "Menace")        { return Keyword::Menace; }
    if (s == "Prowess")       { return Keyword::Prowess; }
    if (s == "Exalted")       { return Keyword::Exalted; }
    if (s == "Suspend")       { return Keyword::Suspend; }   // inert tag; mechanic is param-modelled
    if (s == "Splice")        { return Keyword::Splice;  }   // inert tag; mechanic is param-modelled
    if (s == "Storm")         { return Keyword::Storm;   }   // inert tag; mechanic is param-modelled
    if (s == "Hexproof")      { return Keyword::Hexproof; } // inert tag; provably inert vs passive opp
    if (s == "Enchant")       { return Keyword::Enchant; }   // inert tag; aura attach is param-modelled
    if (s == "Equip")         { return Keyword::Equip; }     // inert tag; equip is param-modelled
    if (s == "Umbra armor")   { return Keyword::UmbraArmor; } // inert tag; provably inert vs passive opp
    if (s == "Rampage")       { return Keyword::Rampage; }    // inert tag; provably inert vs passive opp
    if (s == "Cumulative upkeep") { return Keyword::CumulativeUpkeep; } // inert tag; param-modelled
    if (s == "Strive")        { return Keyword::Strive; }    // inert tag; mechanic is param-modelled
    if (s == "Treasure")      { return Keyword::Treasure; }  // inert tag; mechanic is param-modelled
    if (s == "Metalcraft")    { return Keyword::Metalcraft; } // inert tag; param-modelled (equip {0})
    if (s == "First strike")  { return Keyword::FirstStrike; } // Scryfall lowercase variant
    if (s == "Double strike") { return Keyword::DoubleStrike; }
    if (s == "Regenerate")    { return Keyword::Regenerate; } // inert tag; nothing destroys our creatures
    if (s == "Bestow")        { return Keyword::Bestow; }     // inert tag; mechanic is param-modelled
    throw std::runtime_error("Unknown keyword: " + s);
}

static Supertype SupertypeFromString(const std::string& s)
{
    if (s == "Legendary") { return Supertype::Legendary; }
    if (s == "Basic")     { return Supertype::Basic; }
    if (s == "Snow")      { return Supertype::Snow; }
    if (s == "World")     { return Supertype::World; }
    throw std::runtime_error("Unknown supertype: " + s);
}

static Color ColorFromString(const std::string& s)
{
    if (s == "W") { return Color::White; }
    if (s == "U") { return Color::Blue; }
    if (s == "B") { return Color::Black; }
    if (s == "R") { return Color::Red; }
    if (s == "G") { return Color::Green; }
    if (s == "C") { return Color::Colorless; }
    throw std::runtime_error("Unknown color: " + s);
}

static ManaCost ManaCostFromString(const std::string& cost_str)
{
    // Minimal parser: counts {W}, {U}, {B}, {R}, {G}, {C}, {X}, and generic {N}.
    // TODO: extend for hybrid and Phyrexian mana in Phase 1.2.
    ManaCost cost;
    std::string s = cost_str;
    std::string::size_type pos = 0;
    while ((pos = s.find('{')) != std::string::npos)
    {
        std::string::size_type end = s.find('}', pos);
        if (end == std::string::npos) { break; }
        std::string sym = s.substr(pos + 1, end - pos - 1);
        if (sym == "W")      { ++cost.white; }
        else if (sym == "U") { ++cost.blue; }
        else if (sym == "B") { ++cost.black; }
        else if (sym == "R") { ++cost.red; }
        else if (sym == "G") { ++cost.green; }
        else if (sym == "C") { ++cost.colorless; }
        else if (sym == "X") { cost.has_x = true; ++cost.x_pips; }
        else if (sym.find('/') != std::string::npos)
        {
            // Two-colour hybrid pip ({B/G}, {G/U}, ...): REAL either-colour representation
            // (ManaCost::hybrid_pair; user-directed 2026-08-06 for Deathrite Shaman). Payment /
            // affordability sites expand the concrete assignments; bits==0 keeps the historical
            // first-listed-colour preference (Slippery Bogle stays byte-identical -- its deck has
            // no blue source, so either-colour == green-only there). A number/colour hybrid
            // ({2/W}) or Phyrexian ({G/P}) is still unsupported (no card uses one): falls back to
            // the first recognised colour side.
            std::string first  = sym.substr(0, sym.find('/'));
            std::string second = sym.substr(sym.find('/') + 1);
            auto col_of = [](const std::string& t) -> int
            {
                if (t == "W") { return static_cast<int>(Color::White); }
                if (t == "U") { return static_cast<int>(Color::Blue); }
                if (t == "B") { return static_cast<int>(Color::Black); }
                if (t == "R") { return static_cast<int>(Color::Red); }
                if (t == "G") { return static_cast<int>(Color::Green); }
                return -1;
            };
            const int f = col_of(first), sc = col_of(second);
            // MTG_NO_HYBRID=1: drop the hybrid metadata and fall back to the historical
            // first-listed-colour collapse (A/B lever for measuring either-colour payment;
            // also the byte-identity bisect gate that isolated the digest-render issue).
            static const bool s_no_hybrid = EnvOn("MTG_NO_HYBRID");
            if (!s_no_hybrid && f >= 0 && sc >= 0 && cost.hybrid_count < 4)
            {
                // Bake the FIRST colour into the flat pips (byte-identical to the historical
                // collapse for every flat reader) and record the pair so payment sites can
                // move the pip to the second colour (ManaCost::ExpandHybrids).
                switch (static_cast<Color>(f))
                {
                    case Color::White: ++cost.white; break;
                    case Color::Blue:  ++cost.blue;  break;
                    case Color::Black: ++cost.black; break;
                    case Color::Red:   ++cost.red;   break;
                    case Color::Green: ++cost.green; break;
                    default: break;
                }
                cost.hybrid_pair[cost.hybrid_count++] =
                    static_cast<uint8_t>((f << 4) | sc);
            }
            else if      (first == "W") { ++cost.white; }
            else if (first == "U") { ++cost.blue; }
            else if (first == "B") { ++cost.black; }
            else if (first == "R") { ++cost.red; }
            else if (first == "G") { ++cost.green; }
            else if (first == "C") { ++cost.colorless; }
            else { if      (second == "W") { ++cost.white; } else if (second == "U") { ++cost.blue; }
                   else if (second == "B") { ++cost.black; } else if (second == "R") { ++cost.red; }
                   else if (second == "G") { ++cost.green; } else if (second == "C") { ++cost.colorless; } }
        }
        else
        {
            try { cost.generic += std::stoi(sym); }
            catch (...) {}
        }
        pos = end + 1;
        s = s.substr(pos);
        pos = 0;
    }
    return cost;
}

Card CardDatabase::BuildCardFromJson(const json& entry) const
{
    Card card;
    card.m_name = entry.value("name", std::string{});
    card.RehashName();

    std::string cost_str = entry.value("mana_cost", std::string{});
    card.m_mana_cost = ManaCostFromString(cost_str);

    // Card colors (CR 105.2). m_color_mask was dead scaffolding until FiveColour needed real
    // color identity (domain mana, Mana Cannons' per-color damage, Ancient Cornucopia's
    // lifegain, Jared Carthalion's color counts). Prefer an explicit "colors" array (the
    // Scryfall `colors` field -- required for color indicators, all-color tokens, and any
    // card whose color differs from its pips); otherwise derive from the mana cost's colored
    // pips, which matches Scryfall for every ordinary card. Colorless is the absence of bits
    // (CR 105.1: colorless is not a color).
    if (entry.contains("colors"))
    {
        for (const std::string& c : entry["colors"])
        {
            if      (c == "W") { card.AddColor(Color::White); }
            else if (c == "U") { card.AddColor(Color::Blue);  }
            else if (c == "B") { card.AddColor(Color::Black); }
            else if (c == "R") { card.AddColor(Color::Red);   }
            else if (c == "G") { card.AddColor(Color::Green); }
            else { throw std::runtime_error("Unknown color: " + c); }
        }
    }
    else
    {
        if (card.m_mana_cost.white > 0) { card.AddColor(Color::White); }
        if (card.m_mana_cost.blue  > 0) { card.AddColor(Color::Blue);  }
        if (card.m_mana_cost.black > 0) { card.AddColor(Color::Black); }
        if (card.m_mana_cost.red   > 0) { card.AddColor(Color::Red);   }
        if (card.m_mana_cost.green > 0) { card.AddColor(Color::Green); }
        // A hybrid pip makes the card BOTH its colours (CR 202.2b: {B/G} Deathrite is black-green).
        for (int i = 0; i < card.m_mana_cost.hybrid_count; ++i)
        {
            card.AddColor(static_cast<Color>(card.m_mana_cost.hybrid_pair[i] >> 4));
            card.AddColor(static_cast<Color>(card.m_mana_cost.hybrid_pair[i] & 0xF));
        }
    }

    for (const std::string& t : entry.value("types", json::array()))
    {
        card.AddType(CardTypeFromString(t));
    }

    for (const std::string& k : entry.value("keywords", json::array()))
    {
        card.AddKeyword(KeywordFromString(k));
    }

    for (const std::string& s : entry.value("supertypes", json::array()))
    {
        card.AddSupertype(SupertypeFromString(s));
    }

    for (const std::string& s : entry.value("subtypes", json::array()))
    {
        card.m_subtypes.push_back(s);
    }

    if (!entry.value("power", json{}).is_null() && entry.contains("power"))
    {
        card.m_power = entry["power"].get<int>();
    }
    if (!entry.value("toughness", json{}).is_null() && entry.contains("toughness"))
    {
        card.m_toughness = entry["toughness"].get<int>();
    }

    return card;
}

static Targeting TargetingFromString(const std::string& s)
{
    if (s == "any")      { return Targeting::Any; }
    if (s == "player")   { return Targeting::Player; }
    if (s == "creature") { return Targeting::Creature; }
    if (s == "multi")    { return Targeting::Multi; }
    if (s == "nonland_permanent") { return Targeting::NonlandPermanent; }
    return Targeting::None;
}

CardParams CardDatabase::BuildParamsFromJson(const json& params) const
{
    CardParams p;
    p.damage      = params.value("damage", 0);
    p.draw        = params.value("draw", 0);
    p.power_bonus = params.value("power_bonus", 0);
    p.tough_bonus = params.value("tough_bonus", 0);
    p.targeting      = TargetingFromString(params.value("targeting", std::string("none")));
    p.sacrifice_land = params.value("sacrifice_land", false);
    if (params.contains("spectacle_cost"))
    {
        p.spectacle_cost = ManaCostFromString(params["spectacle_cost"].get<std::string>());
    }
    // Splice-onto-Arcane per-copy cost. Unset -> EffectiveCost falls back to the card's own printed
    // mana cost (byte-identical for Desperate Ritual, whose splice cost equals its cast cost).
    if (params.contains("splice_cost"))
    {
        p.splice_cost = ManaCostFromString(params["splice_cost"].get<std::string>());
    }

    for (const std::string& c : params.value("produces", json::array()))
    {
        p.produces.push_back(ColorFromString(c));
    }
    p.mdfc_back_name = params.value("mdfc_back_name", std::string{});
    for (const std::string& c : params.value("mdfc_back_produces", json::array()))
    {
        p.mdfc_back_produces.push_back(ColorFromString(c));
    }
    for (const std::string& s : params.value("subtypes_affected", json::array()))
    {
        p.subtypes_affected.push_back(s);
    }

    p.on_cast_trigger_max_mv = params.value("on_cast_trigger_max_mv", 0);
    p.on_cast_trigger_damage = params.value("on_cast_trigger_damage", 0);
    p.multicolor_cast_damage_per_color = params.value("multicolor_cast_damage_per_color", false);
    p.colored_cast_lifegain = params.value("colored_cast_lifegain", false);
    p.attack_draw_cards = params.value("attack_draw_cards", 0);
    p.graveyard_replace_shuffle_library = params.value("graveyard_replace_shuffle_library", false);
    p.protection_from_everything        = params.value("protection_from_everything", false);
    p.combat_damage_free_cast = params.value("combat_damage_free_cast", false);
    p.landfall_damage        = params.value("landfall_damage", 0);
    p.stages_cards           = params.value("stages_cards", false);
    p.death_trigger_damage   = params.value("death_trigger_damage", 0);
    p.scales_per_matching    = params.value("scales_per_matching", false);
    p.attack_trigger_life_loss  = params.value("attack_trigger_life_loss", 0);
    p.grants_haste           = params.value("grants_haste", false);
    p.grants_double_strike   = params.value("grants_double_strike", false);
    p.affinity_for_subtype   = params.value("affinity_for_subtype", false);
    p.upkeep_adds_charge     = params.value("upkeep_adds_charge", false);
    p.can_animate            = params.value("can_animate", false);
    p.animate_power          = params.value("animate_power", 0);
    p.animate_toughness      = params.value("animate_toughness", 0);
    if (params.contains("animate_cost"))
    {
        p.animate_cost = ManaCostFromString(params["animate_cost"].get<std::string>());
    }
    p.has_replicate                  = params.value("has_replicate", false);
    p.grants_replicate_to_subtypes   = params.value("grants_replicate_to_subtypes", false);
    p.creature_mana_only             = params.value("creature_mana_only", false);
    p.colored_creature_only          = params.value("colored_creature_only", false);
    if (params.contains("gy_return_cost"))
        p.gy_return_cost = ManaCostFromString(params["gy_return_cost"].get<std::string>());
    p.gy_return_requires_subtype  = params.value("gy_return_requires_subtype", std::string());
    p.gy_return_requires_creature = params.value("gy_return_requires_creature", false);
    p.upkeep_creates_tokens          = params.value("upkeep_creates_tokens", 0);
    p.upkeep_token_power             = params.value("upkeep_token_power", 0);
    p.upkeep_token_toughness         = params.value("upkeep_token_toughness", 0);
    for (const std::string& s : params.value("upkeep_token_subtypes", json::array()))
        p.upkeep_token_subtypes.push_back(s);
    if (params.contains("tap_token_cost"))
        p.tap_token_cost = ManaCostFromString(params["tap_token_cost"].get<std::string>());
    p.tap_token_power     = params.value("tap_token_power", 0);
    p.tap_token_toughness = params.value("tap_token_toughness", 0);
    for (const std::string& s : params.value("tap_token_subtypes", json::array()))
        p.tap_token_subtypes.push_back(s);
    for (const std::string& s : params.value("tap_token_requires_subtypes", json::array()))
        p.tap_token_requires_subtypes.push_back(s);

    p.enters_tapped       = params.value("enters_tapped",       false);
    p.fastland_max_other_lands = params.value("fastland_max_other_lands", -1);
    p.no_max_hand_size    = params.value("no_max_hand_size",    false);
    p.discard_land_damage = params.value("discard_land_damage", 0);
    p.cascade_max_mv      = params.value("cascade_max_mv",      0);
    p.retrace             = params.value("retrace",             false);

    p.etb_pay_life_to_untap = params.value("etb_pay_life_to_untap", 0);
    for (const std::string& s : params.value("etb_untap_reveal_subtypes", json::array()))
        p.etb_untap_reveal_subtypes.push_back(s);
    p.etb_scry         = params.value("etb_scry", 0);
    p.etb_surveil      = params.value("etb_surveil", 0);
    p.tap_self_damage  = params.value("tap_self_damage", 0);
    if (params.contains("cycling_cost"))
        p.cycling_cost = ManaCostFromString(params["cycling_cost"].get<std::string>());
    if (params.contains("sacrifice_draw_cost"))
        p.sacrifice_draw_cost = ManaCostFromString(params["sacrifice_draw_cost"].get<std::string>());
    p.enters_tapped_with_depletion = params.value("enters_tapped_with_depletion", 0);
    p.produces_amount = params.value("produces_amount", 1);
    p.storage_land         = params.value("storage_land", false);
    p.storage_charge_mode  = params.value("storage_charge_mode", std::string());
    p.is_filter       = params.value("is_filter", false);
    p.ramp_filter     = params.value("ramp_filter", false);

    // --- Knights tribal extensions ---
    p.affects_all_creatures        = params.value("affects_all_creatures", false);
    p.lord_excludes_self           = params.value("lord_excludes_self", false);
    p.power_equals_creature_count  = params.value("power_equals_creature_count", false);

    p.cast_trigger_subtype         = params.value("cast_trigger_subtype", std::string{});
    p.cast_trigger_creates_tokens  = params.value("cast_trigger_creates_tokens", 0);
    p.cast_token_power             = params.value("cast_token_power", 0);
    p.cast_token_toughness         = params.value("cast_token_toughness", 0);
    for (const std::string& s : params.value("cast_token_subtypes", json::array()))
        p.cast_token_subtypes.push_back(s);

    p.attack_creates_tokens        = params.value("attack_creates_tokens", 0);
    p.attack_token_power           = params.value("attack_token_power", 0);
    p.attack_token_toughness       = params.value("attack_token_toughness", 0);
    for (const std::string& s : params.value("attack_token_subtypes", json::array()))
        p.attack_token_subtypes.push_back(s);

    // --- Dragonstorm kill-engine (Scourge / Lathliss / Utvara) ---
    p.dragon_ping_on_enter             = params.value("dragon_ping_on_enter", false);
    p.etb_other_subtype_creates_tokens = params.value("etb_other_subtype_creates_tokens", false);
    p.etb_token_requires_subtype       = params.value("etb_token_requires_subtype", std::string{});
    p.etb_created_token_power           = params.value("etb_created_token_power", 0);
    p.etb_created_token_toughness       = params.value("etb_created_token_toughness", 0);
    for (const std::string& s : params.value("etb_created_token_subtypes", json::array()))
        p.etb_created_token_subtypes.push_back(s);
    for (const std::string& s : params.value("etb_created_token_keywords", json::array()))
        p.etb_created_token_keywords.push_back(s);
    p.attack_per_matching_creates_tokens = params.value("attack_per_matching_creates_tokens", 0);
    p.attack_per_token_power             = params.value("attack_per_token_power", 0);
    p.attack_per_token_toughness         = params.value("attack_per_token_toughness", 0);
    for (const std::string& s : params.value("attack_per_token_subtypes", json::array()))
        p.attack_per_token_subtypes.push_back(s);
    for (const std::string& s : params.value("attack_per_token_keywords", json::array()))
        p.attack_per_token_keywords.push_back(s);
    for (const std::string& s : params.value("attack_token_requires_subtypes", json::array()))
        p.attack_token_requires_subtypes.push_back(s);
    if (params.contains("firebreathing_cost"))
        p.firebreathing_cost = ManaCostFromString(params["firebreathing_cost"].get<std::string>());
    p.firebreathing_power = params.value("firebreathing_power", 0);
    if (params.contains("team_pump_cost"))
        p.team_pump_cost = ManaCostFromString(params["team_pump_cost"].get<std::string>());
    p.team_pump_power = params.value("team_pump_power", 0);
    for (const std::string& s : params.value("team_pump_subtypes", json::array()))
        p.team_pump_subtypes.push_back(s);
    p.firebreathing_threshold_power  = params.value("firebreathing_threshold_power", 0);
    p.firebreathing_threshold_damage = params.value("firebreathing_threshold_damage", 0);
    p.haste_on_flying_enter          = params.value("haste_on_flying_enter", false);

    p.etb_dig_count                = params.value("etb_dig_count", 0);
    for (const std::string& s : params.value("etb_dig_subtypes", json::array()))
        p.etb_dig_subtypes.push_back(s);
    for (const std::string& s : params.value("etb_dig_requires_subtypes", json::array()))
        p.etb_dig_requires_subtypes.push_back(s);

    // --- Anti-Lifegain (Tainted Remedy / Aria of Flame) ---
    p.lifegain_to_loss          = params.value("lifegain_to_loss", false);
    p.opponent_lifegain         = params.value("opponent_lifegain", 0);
    p.etb_opponent_lifegain     = params.value("etb_opponent_lifegain", 0);
    p.verse_damage              = params.value("verse_damage", false);
    p.alt_lifegain_cost         = params.value("alt_lifegain_cost", 0);
    p.alt_cost_requires_subtype = params.value("alt_cost_requires_subtype", std::string{});
    p.destroy_all_enchantments  = params.value("destroy_all_enchantments", false);
    p.tutor_to_hand             = params.value("tutor_to_hand", false);
    p.tutor_to_top              = params.value("tutor_to_top", false);
    for (const std::string& s : params.value("tutor_types", json::array()))
        p.tutor_types.push_back(s);
    p.tutor_to_battlefield      = params.value("tutor_to_battlefield", false);
    p.tutor_shuffle_after       = params.value("tutor_shuffle_after", false);
    p.tutor_heuristic           = params.value("tutor_heuristic", std::string{});
    p.discard_random_after_tutor = params.value("discard_random_after_tutor", false);
    p.controller_lifegain_equals_power = params.value("controller_lifegain_equals_power", false);
    p.tap_opponent_lifegain     = params.value("tap_opponent_lifegain", 0);
    for (const std::string& s : params.value("fetch_land_types", json::array()))
        p.fetch_land_types.push_back(s);
    p.target_own_creature       = params.value("target_own_creature", false);
    p.mana_rock                 = params.value("mana_rock", false);
    p.reflecting                = params.value("reflecting", false);
    p.domain_mana               = params.value("domain_mana", false);
    p.domain_self_pump          = params.value("domain_self_pump", false);
    p.loyalty_start             = params.value("loyalty_start", 0);
    if (params.contains("loyalty_abilities"))
    {
        for (const auto& la : params["loyalty_abilities"])
        {
            CardParams::LoyaltyAbilityParam ab;
            ab.delta  = la.value("delta", 0);
            ab.effect = la.value("effect", std::string{});
            ab.amount = la.value("amount", 0);
            p.loyalty_abilities.push_back(std::move(ab));
        }
    }
    p.modal_choose_n            = params.value("modal_choose_n", 0);
    p.modal_damage_per_choice   = params.value("modal_damage_per_choice", 0);
    p.modal_draw_per_choice     = params.value("modal_draw_per_choice", 0);
    p.garth_copy_ability        = params.value("garth_copy_ability", false);
    p.return_target_from_graveyard = params.value("return_target_from_graveyard", false);
    p.destroy_target_creature   = params.value("destroy_target_creature", false);
    p.is_equipment              = params.value("is_equipment", false);
    p.equip_cost_generic        = params.value("equip_cost_generic", 0);
    p.equip_grants_haste        = params.value("equip_grants_haste", false);
    p.equip_grants_shroud       = params.value("equip_grants_shroud", false);
    p.equip_power_bonus         = params.value("equip_power_bonus", 0);
    p.equip_tough_bonus         = params.value("equip_tough_bonus", 0);
    p.equip_grants_lifelink     = params.value("equip_grants_lifelink", false);
    p.equip_min_power           = params.value("equip_min_power", 0);
    p.equip_sacrifices_prior_host = params.value("equip_sacrifices_prior_host", false);
    p.equip_combat_damage_charges = params.value("equip_combat_damage_charges", 0);
    p.charge_pump_power         = params.value("charge_pump_power", 0);
    p.charge_pump_tough         = params.value("charge_pump_tough", 0);
    p.charge_minus_power        = params.value("charge_minus_power", 0);
    p.charge_minus_tough        = params.value("charge_minus_tough", 0);
    p.charge_lifegain           = params.value("charge_lifegain", 0);
    p.double_strike_while_equipped = params.value("double_strike_while_equipped", false);
    p.double_strike_min_equipment  = params.value("double_strike_min_equipment", 0);
    if (params.contains("attach_all_equipment_cost"))
    {
        p.attach_all_equipment_cost =
            ManaCostFromString(params["attach_all_equipment_cost"].get<std::string>());
    }
    p.draw_on_equipment_etb     = params.value("draw_on_equipment_etb", false);
    p.metalcraft_equip_zero_artifacts = params.value("metalcraft_equip_zero_artifacts", 0);
    p.upkeep_tokens_per_equipment = params.value("upkeep_tokens_per_equipment", false);
    p.attack_dig_attach_count   = params.value("attack_dig_attach_count", 0);
    if (params.contains("tap_put_from_hand_cost"))
    {
        p.tap_put_from_hand_cost =
            ManaCostFromString(params["tap_put_from_hand_cost"].get<std::string>());
    }
    for (const std::string& s : params.value("tap_put_from_hand_types", json::array()))
        p.tap_put_from_hand_types.push_back(s);
    p.tuck_to_library           = params.value("tuck_to_library", false);
    p.allow_self_target         = params.value("allow_self_target", false);
    p.gy_land_exile_mana        = params.value("gy_land_exile_mana", false);
    p.gy_exile_instant_sorcery_drain = params.value("gy_exile_instant_sorcery_drain", 0);
    p.gy_exile_creature_lifegain     = params.value("gy_exile_creature_lifegain", 0);
    p.cast_scry                 = params.value("cast_scry", 0);
    p.cast_reorder              = params.value("cast_reorder", 0);
    p.x_damage_multiplier       = params.value("x_damage_multiplier", 1);
    p.damage_divided            = params.value("damage_divided", false);
    p.goldfish_inert            = params.value("goldfish_inert", false);
    p.hinata_cost_reducer        = params.value("hinata_cost_reducer", false);
    p.discount_max_targets       = params.value("discount_max_targets", 0);
    p.discount_targets_scale_x   = params.value("discount_targets_scale_x", false);
    p.discount_self_safe         = params.value("discount_self_safe", false);
    p.discount_targets_permanents = params.value("discount_targets_permanents", false);
    p.etb_bounce_land           = params.value("etb_bounce_land", false);
    p.damage_equals_top_mv      = params.value("damage_equals_top_mv", false);
    p.untap_x_mana_sources      = params.value("untap_x_mana_sources", false);
    p.ritual_floating_mana      = params.value("ritual_floating_mana", 0);
    p.ritual_float_color        = params.value("ritual_float_color", std::string());
    p.ritual_float_gy_self_bonus= params.value("ritual_float_gy_self_bonus", false);
    p.splice_onto_arcane        = params.value("splice_onto_arcane", false);
    p.suspend_time_counters     = params.value("suspend_time_counters", 0);
    p.sac_for_mana_amount       = params.value("sac_for_mana_amount", 0);
    p.impulse_exile             = params.value("impulse_exile", 0);
    p.impulse_expiry_this_turn  = params.value("impulse_expiry_this_turn", false);
    p.impulse_float_amount      = params.value("impulse_float_amount", 0);
    p.reduces_spell_color       = params.value("reduces_spell_color", std::string());
    p.max_casts_after           = params.value("max_casts_after", -1);
    p.taps_spawn_opp_token      = params.value("taps_spawn_opp_token", false);

    // Creature Giving (gift-the-opponent drain)
    p.etb_opp_creates_tokens        = params.value("etb_opp_creates_tokens", 0);
    p.any_creature_enters_lifegain  = params.value("any_creature_enters_lifegain", 0);
    p.own_creature_enters_lifegain  = params.value("own_creature_enters_lifegain", 0);
    p.opp_creature_enters_life_loss = params.value("opp_creature_enters_life_loss", 0);
    p.etb_opp_creatures_debuff      = params.value("etb_opp_creatures_debuff", 0);
    p.opp_dies_life_loss            = params.value("opp_dies_life_loss", 0);
    p.cumulative_upkeep_opp_token   = params.value("cumulative_upkeep_opp_token", false);
    p.upkeep_sac_tutor_creatures    = params.value("upkeep_sac_tutor_creatures", 0);
    p.upkeep_sac_tutor_opp_min      = params.value("upkeep_sac_tutor_opp_min", 0);
    p.tutor_land_to_battlefield     = params.value("tutor_land_to_battlefield", false);

    p.expressive_iteration      = params.value("expressive_iteration", false);
    p.cast_draw                 = params.value("cast_draw", 0);

    // Auras (attach-to-creature enchantments)
    p.is_aura                   = params.value("is_aura", false);
    p.aura_power_bonus          = params.value("aura_power_bonus", 0);
    p.aura_tough_bonus          = params.value("aura_tough_bonus", 0);
    p.aura_grants_lifelink      = params.value("aura_grants_lifelink", false);
    p.aura_scale_kind           = params.value("aura_scale_kind", std::string());
    p.aura_scale_power          = params.value("aura_scale_power", 0);
    p.aura_scale_tough          = params.value("aura_scale_tough", 0);
    p.aura_enchant_requires     = params.value("aura_enchant_requires", std::string());
    p.aura_self_buff_power      = params.value("aura_self_buff_power", 0);
    p.aura_self_buff_tough      = params.value("aura_self_buff_tough", 0);
    p.draw_on_aura_cast         = params.value("draw_on_aura_cast", false);
    p.aura_cast_tutor_attach    = params.value("aura_cast_tutor_attach", false);

    // --- Goblins tribal ---
    p.reduces_spell_subtype     = params.value("reduces_spell_subtype", std::string());
    p.reduces_spell_subtype_amount = params.value("reduces_spell_subtype_amount", 1);
    p.reduces_spell_subtype_creature_only =
        params.value("reduces_spell_subtype_creature_only", false);
    p.chooses_creature_type     = params.value("chooses_creature_type", false);
    p.etb_self_creates_tokens   = params.value("etb_self_creates_tokens", 0);
    p.etb_damage_any            = params.value("etb_damage_any", 0);
    p.etb_damage_each_opponent  = params.value("etb_damage_each_opponent", 0);
    p.etb_reveal_count          = params.value("etb_reveal_count", 0);
    for (const std::string& s : params.value("etb_reveal_put_subtypes", json::array()))
        p.etb_reveal_put_subtypes.push_back(s);
    p.etb_reveal_put_creatures_only = params.value("etb_reveal_put_creatures_only", false);
    p.etb_reveal_put_max_mv     = params.value("etb_reveal_put_max_mv", 0);
    p.attack_pump_power_per_other_matching = params.value("attack_pump_power_per_other_matching", 0);
    p.attack_self_pump_per_other_subtype   = params.value("attack_self_pump_per_other_subtype", std::string());
    p.attack_self_pump_power    = params.value("attack_self_pump_power", 0);
    p.attack_self_pump_tough    = params.value("attack_self_pump_tough", 0);
    p.dies_watch_subtype        = params.value("dies_watch_subtype", std::string());
    p.dies_watch_includes_self  = params.value("dies_watch_includes_self", false);
    p.dies_trigger_damage       = params.value("dies_trigger_damage", 0);
    p.dies_trigger_creates_tokens = params.value("dies_trigger_creates_tokens", 0);
    p.dies_token_power          = params.value("dies_token_power", 0);
    p.dies_token_toughness      = params.value("dies_token_toughness", 0);
    for (const std::string& s : params.value("dies_token_subtypes", json::array()))
        p.dies_token_subtypes.push_back(s);
    p.dies_trigger_impulse_exile   = params.value("dies_trigger_impulse_exile", false);
    p.dies_impulse_requires_type    = params.value("dies_impulse_requires_type", std::string());
    p.dies_impulse_requires_subtype = params.value("dies_impulse_requires_subtype", std::string());
    p.dies_impulse_expiry_next_turn = params.value("dies_impulse_expiry_next_turn", false);
    p.sac_creature_outlet       = params.value("sac_creature_outlet", false);
    if (params.contains("sac_creature_cost"))
        p.sac_creature_cost = ManaCostFromString(params["sac_creature_cost"].get<std::string>());
    p.sac_creature_requires_subtype = params.value("sac_creature_requires_subtype", std::string());
    p.sac_outlet_add_mana_color = params.value("sac_outlet_add_mana_color", std::string());
    p.sac_outlet_add_mana_amount = params.value("sac_outlet_add_mana_amount", 0);
    p.sac_outlet_damage         = params.value("sac_outlet_damage", 0);
    p.sac_outlet_creates_tokens = params.value("sac_outlet_creates_tokens", 0);
    p.sac_outlet_token_power    = params.value("sac_outlet_token_power", 0);
    p.sac_outlet_token_toughness = params.value("sac_outlet_token_toughness", 0);
    for (const std::string& s : params.value("sac_outlet_token_subtypes", json::array()))
        p.sac_outlet_token_subtypes.push_back(s);
    p.tap_creates_tokens_per_controlled_subtype = params.value("tap_creates_tokens_per_controlled_subtype", std::string());
    p.tap_created_token_power     = params.value("tap_created_token_power", 0);
    p.tap_created_token_toughness = params.value("tap_created_token_toughness", 0);
    for (const std::string& s : params.value("tap_created_token_subtypes", json::array()))
        p.tap_created_token_subtypes.push_back(s);
    if (params.contains("channel_cost"))
        p.channel_cost = ManaCostFromString(params["channel_cost"].get<std::string>());
    p.channel_damage            = params.value("channel_damage", 0);
    if (params.contains("echo_cost"))
        p.echo_cost = ManaCostFromString(params["echo_cost"].get<std::string>());
    for (const std::string& s : params.value("combat_damage_puts_subtype_from_hand", json::array()))
        p.combat_damage_puts_subtype_from_hand.push_back(s);
    p.mana_per_creature_subtype       = params.value("mana_per_creature_subtype", std::string());
    p.mana_per_creature_feeder_generic = params.value("mana_per_creature_feeder_generic", 0);

    // Zada / Mirrorwing spell-copy swarm (see CardDatabase.h block comment).
    p.copies_solo_targeted_spells = params.value("copies_solo_targeted_spells", false);
    p.solo_target_trick           = params.value("solo_target_trick", false);
    p.trick_up_to_one             = params.value("trick_up_to_one", false);
    p.pump_per_cards_drawn_power  = params.value("pump_per_cards_drawn_power", 0);
    p.gy_self_power_bonus         = params.value("gy_self_power_bonus", 0);
    p.pump_per_treasure_power     = params.value("pump_per_treasure_power", 0);
    p.pump_per_treasure_tough     = params.value("pump_per_treasure_tough", 0);
    p.creates_treasures           = params.value("creates_treasures", 0);
    p.grants_temp_haste           = params.value("grants_temp_haste", false);
    p.counters_on_target          = params.value("counters_on_target", 0);
    p.cast_lifegain               = params.value("cast_lifegain", 0);
    p.pump_per_life_gained_power  = params.value("pump_per_life_gained_power", 0);
    p.pump_per_life_gained_tough  = params.value("pump_per_life_gained_tough", 0);
    p.pump_per_x_power            = params.value("pump_per_x_power", 0);
    p.pump_per_x_tough            = params.value("pump_per_x_tough", 0);
    p.trick_token_power           = params.value("trick_token_power", 0);
    p.trick_token_toughness       = params.value("trick_token_toughness", 0);
    if (params.contains("trick_token_subtypes"))
        p.trick_token_subtypes = params["trick_token_subtypes"].get<std::vector<std::string>>();
    p.grants_extra_land_drop      = params.value("grants_extra_land_drop", 0);
    p.token_copy_of_target        = params.value("token_copy_of_target", false);
    if (params.contains("strive_cost"))
        p.strive_cost = ManaCostFromString(params["strive_cost"].get<std::string>());
    p.etb_lifegain                = params.value("etb_lifegain", 0);
    for (const std::string& s : params.value("checkland_subtypes", json::array()))
        p.checkland_subtypes.push_back(s);

    // --- StompySurprise (mono-green elf ramp) ---
    p.etb_life_floor               = params.value("etb_life_floor", 0);
    p.mana_per_creature_count_all  = params.value("mana_per_creature_count_all", false);
    p.mana_requires_land_subtype   = params.value("mana_requires_land_subtype", std::string{});
    p.etb_team_pump_per_creature   = params.value("etb_team_pump_per_creature", false);
    p.upkeep_reorder               = params.value("upkeep_reorder", 0);
    p.creature_enters_min_power     = params.value("creature_enters_min_power", 0);
    p.own_creature_enters_draw      = params.value("own_creature_enters_draw", 0);
    p.creature_enters_includes_self = params.value("creature_enters_includes_self", false);
    p.dies_trigger_copy_self_token  = params.value("dies_trigger_copy_self_token", false);
    p.tutor_color                   = params.value("tutor_color", std::string{});
    p.sac_additional_creature_color = params.value("sac_additional_creature_color", std::string{});
    p.tutor_to_battlefield_single   = params.value("tutor_to_battlefield_single", false);
    if (params.contains("activated_reveal_top_cost"))
        p.activated_reveal_top_cost = ManaCostFromString(params["activated_reveal_top_cost"].get<std::string>());
    if (params.contains("untap_creature_cost"))
        p.untap_creature_cost = ManaCostFromString(params["untap_creature_cost"].get<std::string>());
    p.untap_creature_subtype        = params.value("untap_creature_subtype", std::string{});
    p.etb_destroy_own_noncreature_max = params.value("etb_destroy_own_noncreature_max", 0);
    p.mdfc_back_pay_life            = params.value("mdfc_back_pay_life", 0);
    p.look_top_put_creature_count   = params.value("look_top_put_creature_count", 0);
    p.look_put_counter_bonus        = params.value("look_put_counter_bonus", 0);
    p.look_put_counter_bonus_max_mv = params.value("look_put_counter_bonus_max_mv", 0);
    p.created_token_color           = params.value("created_token_color", std::string{});

    // ---- Minotaur tribal (see CardParams' block comment for each field's semantics) ----
    p.etb_damage_devotion_color       = params.value("etb_damage_devotion_color", std::string{});
    p.reduces_subtype_colored_subtype = params.value("reduces_subtype_colored_subtype", std::string{});
    if (params.contains("reduces_subtype_colored_cost"))
        p.reduces_subtype_colored_cost =
            ManaCostFromString(params["reduces_subtype_colored_cost"].get<std::string>());
    p.attack_pump_matching_power    = params.value("attack_pump_matching_power", 0);
    p.must_attack                   = params.value("must_attack", false);
    p.hand_size_anthem_max          = params.value("hand_size_anthem_max", -1);
    p.hand_size_anthem_power        = params.value("hand_size_anthem_power", 0);
    p.hand_size_anthem_tough        = params.value("hand_size_anthem_tough", 0);
    p.combat_damage_each_discards   = params.value("combat_damage_each_discards", 0);
    p.firebreathing_discard         = params.value("firebreathing_discard", false);
    p.etb_token_includes_self       = params.value("etb_token_includes_self", false);
    p.team_pump_grants_haste        = params.value("team_pump_grants_haste", false);
    p.sacrifice_watch_pump_power    = params.value("sacrifice_watch_pump_power", 0);
    p.sac_outlet_allows_enchantment = params.value("sac_outlet_allows_enchantment", false);
    p.sac_outlet_excludes_self      = params.value("sac_outlet_excludes_self", false);
    if (params.contains("bestow_cost"))
        p.bestow_cost = ManaCostFromString(params["bestow_cost"].get<std::string>());

    return p;
}
