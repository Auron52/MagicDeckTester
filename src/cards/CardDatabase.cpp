#include "CardDatabase.h"
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

CardDatabase& CardDatabase::Instance()
{
    static CardDatabase instance;
    return instance;
}

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
        m_cards[def.card.m_name] = std::move(def);
    }
}

void CardDatabase::Register(const std::string& name, CardFactory factory)
{
    CardDefinition def = factory();
    def.tier = CardTier::Custom;
    def.tmpl = CardTemplate::None;
    m_cards[name] = std::move(def);
}

std::optional<CardDefinition> CardDatabase::Lookup(const std::string& name) const
{
    auto it = m_cards.find(name);
    if (it == m_cards.end())
    {
        return std::nullopt;
    }
    return it->second;
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
        else if (sym == "X") { cost.has_x = true; }
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

    std::string cost_str = entry.value("mana_cost", std::string{});
    card.m_mana_cost = ManaCostFromString(cost_str);

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

    for (const std::string& c : params.value("produces", json::array()))
    {
        p.produces.push_back(ColorFromString(c));
    }
    for (const std::string& s : params.value("subtypes_affected", json::array()))
    {
        p.subtypes_affected.push_back(s);
    }

    p.on_cast_trigger_max_mv = params.value("on_cast_trigger_max_mv", 0);
    p.on_cast_trigger_damage = params.value("on_cast_trigger_damage", 0);
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

    p.etb_dig_count                = params.value("etb_dig_count", 0);
    for (const std::string& s : params.value("etb_dig_subtypes", json::array()))
        p.etb_dig_subtypes.push_back(s);
    for (const std::string& s : params.value("etb_dig_requires_subtypes", json::array()))
        p.etb_dig_requires_subtypes.push_back(s);

    return p;
}
