#include "AIEngine.h"
#include <algorithm>
#include <stdexcept>

AIEngine::AIEngine(MulliganProfile profile) : m_profile(std::move(profile)) {}

void AIEngine::HandleMulligan(GameState& state)
{
    Player& ap = state.ActivePlayer();

    // Draw opening hand of 7
    int draw = std::min(7, static_cast<int>(ap.library.size()));
    for (int i = 0; i < draw; ++i)
    {
        ap.hand.push_back(ap.library.DrawTop());
    }

    int mulliganCount = 0;
    while (!KeepHand(ap.hand, mulliganCount))
    {
        // Return hand to library; re-shuffle to simulate the physical shuffle during a mulligan.
        for (Card& c : ap.hand)
        {
            ap.library.push_back(c);
        }
        ap.hand.clear();
        // Derive a per-mulligan seed so each reshuffle produces a distinct order.
        // TODO: replace with the unified seeded RNG in Phase 1.3.
        ap.library.Shuffle(state.gameSeed + static_cast<uint64_t>(mulliganCount));

        ++mulliganCount;

        int toDraw = std::min(7, static_cast<int>(ap.library.size()));
        for (int i = 0; i < toDraw; ++i)
        {
            ap.hand.push_back(ap.library.DrawTop());
        }

        if (static_cast<int>(ap.hand.size()) <= m_profile.stopAt)
        {
            break;
        }
    }

    // Bottom one card per mulligan (London mulligan rule)
    if (mulliganCount > 0)
    {
        BottomCards(state, mulliganCount);
    }
}

bool AIEngine::KeepHand(const std::vector<Card>& hand, int mulliganCount) const
{
    int effectiveSize = static_cast<int>(hand.size()) - mulliganCount;
    if (effectiveSize <= 1)
    {
        return true;  // hard floor: never go below 1
    }
    if (effectiveSize <= m_profile.stopAt)
    {
        return true;
    }

    int landCount = 0;
    for (const Card& c : hand)
    {
        if (c.IsLand())
        {
            ++landCount;
        }
    }
    int nonLandCount = static_cast<int>(hand.size()) - landCount;

    if (landCount < m_profile.minLands)
    {
        return false;
    }
    if (landCount > m_profile.maxLands)
    {
        return false;
    }
    if (nonLandCount == 0)
    {
        return false;
    }

    if (!m_profile.requiredPieces.empty())
    {
        bool found = false;
        for (const std::string& piece : m_profile.requiredPieces)
        {
            for (const Card& c : hand)
            {
                if (c.m_name == piece)
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

    if (!m_profile.skipCurveCheck)
    {
        bool hasTwoDrop = false;
        for (const Card& c : hand)
        {
            if (!c.IsLand() && c.m_mana_cost.ManaValue() <= 2 && landCount >= 2)
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

void AIEngine::BottomCards(GameState& state, int count)
{
    Player& ap = state.ActivePlayer();
    // Bottom the highest-mana-value cards first (fewest immediate options lost).
    // TODO: smarter bottoming once CardDatabase provides card role context (Phase 1.2).
    for (int i = 0; i < count && !ap.hand.empty(); ++i)
    {
        std::vector<Card>::iterator worst = std::max_element(ap.hand.begin(), ap.hand.end(),
            [](const Card& a, const Card& b)
            {
                return a.m_mana_cost.ManaValue() < b.m_mana_cost.ManaValue();
            });
        ap.library.push_back(*worst);
        ap.hand.erase(worst);
    }
}

void AIEngine::TakeTurn(GameState& state, bool isPreCombatMain)
{
    // TODO (Phase 1.2): land drop, spell casting, activated abilities.
    // Requires CardDatabase to resolve card types and mana costs from placeholder Cards.
    (void)state;
    (void)isPreCombatMain;
}

std::vector<Permanent*> AIEngine::DeclareAttackers(GameState& state)
{
    std::vector<Permanent*> attackers;
    Player& ap = state.ActivePlayer();
    for (Permanent& p : state.battlefield)
    {
        if (p.controller == &ap
            && p.card.IsCreature()
            && !p.tapped
            && p.CanAttackOrTap()
            && !p.card.HasKeyword(Keyword::Defender))
        {
            attackers.push_back(&p);
        }
    }
    return attackers;
}

Card* AIEngine::ChooseDiscard(GameState& state)
{
    Player& ap = state.ActivePlayer();
    if (ap.hand.empty())
    {
        throw std::runtime_error("ChooseDiscard called with empty hand");
    }
    // Discard the highest-mana-value card.
    // TODO: smarter discard evaluation once CardDatabase is available (Phase 1.2).
    return &(*std::max_element(ap.hand.begin(), ap.hand.end(),
        [](const Card& a, const Card& b)
        {
            return a.m_mana_cost.ManaValue() < b.m_mana_cost.ManaValue();
        }));
}
