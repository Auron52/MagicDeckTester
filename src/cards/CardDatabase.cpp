#include "CardDatabase.h"
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
            m_def_hash[bn] = CardDefHash(entry) ^ std::hash<std::string>{}(bn);
            m_cards[bn] = std::move(back);
        }
        m_cards[def.card.m_name] = std::move(def);
    }
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
    if (s == "Umbra armor")   { return Keyword::UmbraArmor; } // inert tag; provably inert vs passive opp
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
            // Two-colour hybrid pip ({G/U} etc.): the ManaPool has no hybrid support, so model it as
            // its FIRST listed colour. The only hybrid card in the corpus is Slippery Bogle {G/U} in a
            // green deck with no blue source, so the green side is the only payable one anyway (the
            // simplification is disclosed on the card). A number/colour hybrid ({2/W}) or Phyrexian
            // ({G/P}) is not used by any card; falls through to the first recognised colour, else 0.
            std::string first = sym.substr(0, sym.find('/'));
            if      (first == "W") { ++cost.white; }
            else if (first == "U") { ++cost.blue; }
            else if (first == "B") { ++cost.black; }
            else if (first == "R") { ++cost.red; }
            else if (first == "G") { ++cost.green; }
            else if (first == "C") { ++cost.colorless; }
            else { std::string second = sym.substr(sym.find('/') + 1);
                   if      (second == "W") { ++cost.white; } else if (second == "U") { ++cost.blue; }
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
    p.attack_per_matching_creates_tokens = params.value("attack_per_matching_creates_tokens", 0);
    p.attack_per_token_power             = params.value("attack_per_token_power", 0);
    p.attack_per_token_toughness         = params.value("attack_per_token_toughness", 0);
    for (const std::string& s : params.value("attack_per_token_subtypes", json::array()))
        p.attack_per_token_subtypes.push_back(s);
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

    return p;
}
