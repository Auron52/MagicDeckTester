#include "DeckLoader.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include "pugixml.hpp"

static std::string Trim(const std::string& s)
{
    std::string::size_type start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
    {
        return "";
    }
    std::string::size_type end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

Decklist DeckLoader::LoadFromFile(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
    {
        throw std::runtime_error("File not found: " + path.string());
    }

    if (ToLower(path.extension().string()) == ".cod")
    {
        return LoadCockatrice(path);
    }
    return LoadTextFile(path);
}

Decklist DeckLoader::LoadTextFile(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file)
    {
        throw std::runtime_error("Cannot open: " + path.string());
    }

    Decklist deck;
    bool in_sideboard = false;
    std::string line;

    while (std::getline(file, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == '/' || line[0] == '#')
        {
            continue;
        }

        // Detect sideboard section headers
        std::string low = ToLower(line);
        if (low == "sideboard" || low == "sideboard:" || low.rfind("sb:", 0) == 0)
        {
            in_sideboard = true;
            continue;
        }

        // Parse count token: "4 Lightning Bolt" or "4x Lightning Bolt"
        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (!token.empty() && (token.back() == 'x' || token.back() == 'X'))
        {
            token.pop_back();
        }

        int count = 1;
        bool has_count = false;
        try
        {
            count     = std::stoi(token);
            has_count = true;
        }
        catch (...) {}

        std::string name;
        if (has_count)
        {
            std::getline(ss, name);
            name = Trim(name);
        }
        else
        {
            name = Trim(line);
        }
        if (name.empty())
        {
            continue;
        }

        // Strip set/collector info from Arena exports: "Card Name (SET) 123"
        std::string::size_type paren = name.find(" (");
        if (paren != std::string::npos)
        {
            name = name.substr(0, paren);
        }

        AppendCards(in_sideboard ? deck.sideboard : deck.mainboard, name, count);
    }

    if (deck.mainboard.empty())
    {
        throw std::runtime_error("No cards parsed from: " + path.string());
    }
    return deck;
}

Decklist DeckLoader::LoadCockatrice(const std::filesystem::path& path)
{
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(path.c_str());
    if (!result)
    {
        throw std::runtime_error("XML error in " + path.string() + ": " + result.description());
    }

    pugi::xml_node root = doc.child("cockatrice_deck");
    if (!root)
    {
        throw std::runtime_error("Not a Cockatrice deck file: " + path.string());
    }

    Decklist deck;
    for (pugi::xml_node zone : root.children("zone"))
    {
        std::string zone_name = zone.attribute("name").as_string();
        bool is_side = (zone_name == "side" || zone_name == "sideboard");
        std::vector<Card>& target = is_side ? deck.sideboard : deck.mainboard;

        for (pugi::xml_node card : zone.children("card"))
        {
            int count        = card.attribute("number").as_int(1);
            std::string name = Trim(card.attribute("name").as_string());
            if (!name.empty())
            {
                AppendCards(target, name, count);
            }
        }
    }

    if (deck.mainboard.empty())
    {
        throw std::runtime_error("No cards found in main zone: " + path.string());
    }
    return deck;
}

Card DeckLoader::MakePlaceholder(const std::string& name)
{
    Card c;
    c.m_name = name;
    c.RehashName();
    return c;
}

void DeckLoader::AppendCards(std::vector<Card>& target, const std::string& name, int count)
{
    for (int i = 0; i < count; ++i)
    {
        target.push_back(MakePlaceholder(name));
    }
}
