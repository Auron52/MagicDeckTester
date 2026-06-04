#pragma once
#include <string>
#include <vector>
#include <optional>

enum class CardType { Land, Creature, Instant, Sorcery, Enchantment, Artifact, Planeswalker, Battle };
enum class Color { White, Blue, Black, Red, Green, Colorless };
enum class Keyword
{
    Haste, Flying, Trample, Deathtouch, Lifelink, FirstStrike, DoubleStrike,
    Vigilance, Reach, Defender, Indestructible, Flash, Menace, Prowess
};
enum class Supertype { Legendary, Basic, Snow, World };

struct ManaCost
{
    int generic   = 0;
    int white     = 0;
    int blue      = 0;
    int black     = 0;
    int red       = 0;
    int green     = 0;
    int colorless = 0;  // {C} symbols
    bool has_x    = false;

    // X counts as 0 outside the stack (CR 202.3)
    int ManaValue() const { return generic + white + blue + black + red + green + colorless; }

    // Canonical MTG notation: {X}{N}{W}{U}{B}{R}{G}{C}
    std::string ToString() const
    {
        std::string s;
        if (has_x)    { s += "{X}"; }
        if (generic > 0) { s += "{" + std::to_string(generic) + "}"; }
        for (int i = 0; i < white;     ++i) { s += "{W}"; }
        for (int i = 0; i < blue;      ++i) { s += "{U}"; }
        for (int i = 0; i < black;     ++i) { s += "{B}"; }
        for (int i = 0; i < red;       ++i) { s += "{R}"; }
        for (int i = 0; i < green;     ++i) { s += "{G}"; }
        for (int i = 0; i < colorless; ++i) { s += "{C}"; }
        return s;
    }
};

struct Card
{
    std::string m_id;           // placeholder until CardDatabase is implemented (Phase 1.2)
    std::string m_name;
    int         m_number    = 0;    // per-copy stable ID (1–60); assigned at deck setup
    bool        m_is_staged = false; // true while the card is a staged (exiled) card in hand
    ManaCost m_mana_cost;
    std::vector<std::string> m_subtypes; // creature/land subtypes (e.g. "Sliver", "Goblin", "Mountain")
    std::vector<Supertype> m_supertypes;
    std::vector<CardType>  m_types;
    std::vector<Color>     m_colors;
    std::optional<int>     m_power;      // null for non-creatures
    std::optional<int>     m_toughness;
    std::vector<Keyword>   m_keywords;
    std::string            m_oracle_text;

    bool IsLand()     const { return HasType(CardType::Land); }
    bool IsCreature() const { return HasType(CardType::Creature); }
    bool IsInstant()  const { return HasType(CardType::Instant); }
    bool IsSorcery()  const { return HasType(CardType::Sorcery); }

    bool HasType(CardType t)       const;
    bool HasSupertype(Supertype s) const;
    bool HasKeyword(Keyword k)     const;
};

inline bool Card::HasType(CardType t) const
{
    for (CardType ct : m_types)
    {
        if (ct == t)
        {
            return true;
        }
    }
    return false;
}

inline bool Card::HasSupertype(Supertype s) const
{
    for (Supertype st : m_supertypes)
    {
        if (st == s)
        {
            return true;
        }
    }
    return false;
}

inline bool Card::HasKeyword(Keyword k) const
{
    for (Keyword kw : m_keywords)
    {
        if (kw == k)
        {
            return true;
        }
    }
    return false;
}
