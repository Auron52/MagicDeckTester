#include "DeckLoader.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include "pugixml.hpp"

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

Decklist DeckLoader::loadFromFile(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path))
        throw std::runtime_error("File not found: " + path.string());

    if (toLower(path.extension().string()) == ".cod")
        return loadCockatrice(path);
    return loadTextFile(path);
}

Decklist DeckLoader::loadTextFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open: " + path.string());

    Decklist deck;
    bool inSideboard = false;
    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '/' || line[0] == '#') continue;

        // Detect sideboard section headers
        std::string low = toLower(line);
        if (low == "sideboard" || low == "sideboard:" || low.rfind("sb:", 0) == 0) {
            inSideboard = true;
            continue;
        }

        // Parse count token: "4 Lightning Bolt" or "4x Lightning Bolt"
        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (!token.empty() && (token.back() == 'x' || token.back() == 'X'))
            token.pop_back();

        int count = 1;
        bool hasCount = false;
        try {
            count    = std::stoi(token);
            hasCount = true;
        } catch (...) {}

        std::string name;
        if (hasCount) {
            std::getline(ss, name);
            name = trim(name);
        } else {
            name = trim(line);
        }
        if (name.empty()) continue;

        // Strip set/collector info from Arena exports: "Card Name (SET) 123"
        auto paren = name.find(" (");
        if (paren != std::string::npos) name = name.substr(0, paren);

        appendCards(inSideboard ? deck.sideboard : deck.mainboard, name, count);
    }

    if (deck.mainboard.empty())
        throw std::runtime_error("No cards parsed from: " + path.string());
    return deck;
}

Decklist DeckLoader::loadCockatrice(const std::filesystem::path& path) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(path.c_str());
    if (!result)
        throw std::runtime_error("XML error in " + path.string() + ": " + result.description());

    pugi::xml_node root = doc.child("cockatrice_deck");
    if (!root)
        throw std::runtime_error("Not a Cockatrice deck file: " + path.string());

    Decklist deck;
    for (pugi::xml_node zone : root.children("zone")) {
        std::string zoneName = zone.attribute("name").as_string();
        bool isSide = (zoneName == "side" || zoneName == "sideboard");
        auto& target = isSide ? deck.sideboard : deck.mainboard;

        for (pugi::xml_node card : zone.children("card")) {
            int count      = card.attribute("number").as_int(1);
            std::string name = trim(card.attribute("name").as_string());
            if (!name.empty()) appendCards(target, name, count);
        }
    }

    if (deck.mainboard.empty())
        throw std::runtime_error("No cards found in main zone: " + path.string());
    return deck;
}

Card DeckLoader::makePlaceholder(const std::string& name) {
    Card c;
    c.name = name;
    return c;
}

void DeckLoader::appendCards(std::vector<Card>& target, const std::string& name, int count) {
    for (int i = 0; i < count; ++i)
        target.push_back(makePlaceholder(name));
}
