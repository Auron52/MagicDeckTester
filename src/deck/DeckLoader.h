#pragma once
#include "../core/Card.h"
#include <vector>
#include <filesystem>

struct Decklist
{
    std::vector<Card> mainboard;   // one Card per copy (60 entries for a standard 60-card deck)
    std::vector<Card> sideboard;
};

class DeckLoader
{
public:
    // Detects format by extension: .cod -> Cockatrice XML; anything else -> plain text.
    // Throws std::runtime_error on missing file or parse failure.
    static Decklist loadFromFile(const std::filesystem::path& path);

private:
    static Decklist loadTextFile(const std::filesystem::path& path);
    static Decklist loadCockatrice(const std::filesystem::path& path);

    // Returns a placeholder Card with only name set.
    // Full data (types, cost, oracle text) is resolved by CardDatabase in Phase 1.2.
    static Card makePlaceholder(const std::string& name);
    static void appendCards(std::vector<Card>& target, const std::string& name, int count);
};
