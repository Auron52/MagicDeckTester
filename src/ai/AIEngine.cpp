#include "AIEngine.h"
#include <algorithm>
#include <stdexcept>

AIEngine::AIEngine(MulliganProfile profile) : profile_(std::move(profile)) {}

void AIEngine::handleMulligan(GameState& state)
{
    Player& ap = state.activePlayer();

    // Draw opening hand of 7
    int draw = std::min(7, static_cast<int>(ap.library.size()));
    for (int i = 0; i < draw; ++i)
    {
        ap.hand.push_back(ap.library.front());
        ap.library.erase(ap.library.begin());
    }

    int mulliganCount = 0;
    while (!keepHand(ap.hand, mulliganCount))
    {
        // Return hand to library; GoldFishRunner has already shuffled for this game,
        // so re-shuffle here to simulate the physical shuffle during a mulligan.
        for (Card& c : ap.hand)
        {
            ap.library.push_back(c);
        }
        ap.hand.clear();
        // TODO: re-shuffle using the seeded RNG (Phase 1.3)

        ++mulliganCount;

        int toDraw = std::min(7, static_cast<int>(ap.library.size()));
        for (int i = 0; i < toDraw; ++i)
        {
            ap.hand.push_back(ap.library.front());
            ap.library.erase(ap.library.begin());
        }

        if (static_cast<int>(ap.hand.size()) <= profile_.stopAt)
        {
            break;
        }
    }

    // Bottom one card per mulligan (London mulligan rule)
    if (mulliganCount > 0)
    {
        bottomCards(state, mulliganCount);
    }
}

bool AIEngine::keepHand(const std::vector<Card>& hand, int mulliganCount) const
{
    int effectiveSize = static_cast<int>(hand.size()) - mulliganCount;
    if (effectiveSize <= 1)
    {
        return true;  // hard floor: never go below 1
    }
    if (effectiveSize <= profile_.stopAt)
    {
        return true;
    }

    int landCount = 0;
    for (const Card& c : hand)
    {
        if (c.isLand())
        {
            ++landCount;
        }
    }
    int nonLandCount = static_cast<int>(hand.size()) - landCount;

    if (landCount < profile_.minLands)
    {
        return false;
    }
    if (landCount > profile_.maxLands)
    {
        return false;
    }
    if (nonLandCount == 0)
    {
        return false;
    }

    if (!profile_.requiredPieces.empty())
    {
        bool found = false;
        for (const std::string& piece : profile_.requiredPieces)
        {
            for (const Card& c : hand)
            {
                if (c.name == piece)
                {
                    found = true;
                    break;
                }
            }
        }
        if (!found)
        {
            return false;
        }
    }

    if (!profile_.skipCurveCheck)
    {
        bool hasTwoDrop = false;
        for (const Card& c : hand)
        {
            if (!c.isLand() && c.manaCost.manaValue() <= 2 && landCount >= 2)
            {
                hasTwoDrop = true;
            }
        }
        if (!hasTwoDrop && mulliganCount < 2)
        {
            return false;
        }
    }

    return true;
}

void AIEngine::bottomCards(GameState& state, int count)
{
    Player& ap = state.activePlayer();
    // Bottom the highest-mana-value cards first (fewest immediate options lost).
    // TODO: smarter bottoming once CardDatabase provides card role context (Phase 1.2).
    for (int i = 0; i < count && !ap.hand.empty(); ++i)
    {
        std::vector<Card>::iterator worst = std::max_element(ap.hand.begin(), ap.hand.end(),
            [](const Card& a, const Card& b)
            {
                return a.manaCost.manaValue() < b.manaCost.manaValue();
            });
        ap.library.push_back(*worst);
        ap.hand.erase(worst);
    }
}

void AIEngine::takeTurn(GameState& state, bool isPreCombatMain)
{
    // TODO (Phase 1.2): land drop, spell casting, activated abilities.
    // Requires CardDatabase to resolve card types and mana costs from placeholder Cards.
    (void)state;
    (void)isPreCombatMain;
}

std::vector<Permanent*> AIEngine::declareAttackers(GameState& state)
{
    std::vector<Permanent*> attackers;
    Player& ap = state.activePlayer();
    for (Permanent& p : state.battlefield)
    {
        if (p.controller == &ap
            && p.card.isCreature()
            && !p.tapped
            && p.canAttackOrTap()
            && !p.card.hasKeyword(Keyword::Defender))
        {
            attackers.push_back(&p);
        }
    }
    return attackers;
}

Card* AIEngine::chooseDiscard(GameState& state)
{
    Player& ap = state.activePlayer();
    if (ap.hand.empty())
    {
        throw std::runtime_error("chooseDiscard called with empty hand");
    }
    // Discard the highest-mana-value card.
    // TODO: smarter discard evaluation once CardDatabase is available (Phase 1.2).
    return &(*std::max_element(ap.hand.begin(), ap.hand.end(),
        [](const Card& a, const Card& b)
        {
            return a.manaCost.manaValue() < b.manaCost.manaValue();
        }));
}
