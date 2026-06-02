#pragma once
#include <string>
#include <vector>
#include <optional>

enum class CardType { Land, Creature, Instant, Sorcery, Enchantment, Artifact, Planeswalker, Battle };
enum class Color { White, Blue, Black, Red, Green, Colorless };
enum class Keyword
{
    Haste, Flying, Trample, Deathtouch, Lifelink, FirstStrike, DoubleStrike,
    Vigilance, Reach, Defender, Indestructible, Flash, Menace
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
    bool hasX     = false;

    // X counts as 0 outside the stack (CR 202.3)
    int manaValue() const { return generic + white + blue + black + red + green + colorless; }
};

struct Card
{
    std::string m_id;           // placeholder until CardDatabase is implemented (Phase 1.2)
    std::string m_name;
    ManaCost m_mana_cost;
    std::vector<Supertype> m_supertypes;
    std::vector<CardType>  m_types;
    std::vector<Color>     m_colors;
    std::optional<int>     m_power;      // null for non-creatures
    std::optional<int>     m_toughness;
    std::vector<Keyword>   m_keywords;
    std::string            m_oracle_text;

    bool isLand()      const { return hasType(CardType::Land); }
    bool isCreature()  const { return hasType(CardType::Creature); }
    bool isInstant()   const { return hasType(CardType::Instant); }
    bool isSorcery()   const { return hasType(CardType::Sorcery); }

    bool hasType(CardType t)       const;
    bool hasSupertype(Supertype s) const;
    bool hasKeyword(Keyword k)     const;
};

inline bool Card::hasType(CardType t) const
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

inline bool Card::hasSupertype(Supertype s) const
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

inline bool Card::hasKeyword(Keyword k) const
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
