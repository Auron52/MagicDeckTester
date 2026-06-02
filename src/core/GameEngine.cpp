#include "GameEngine.h"
#include "../ai/AIEngine.h"
#include <algorithm>

GameEngine::GameEngine(AIEngine& ai) : m_ai(ai) {}

int GameEngine::RunGame(GameState& state, int maxTurns)
{
    m_ai.HandleMulligan(state);
    while (state.turnNumber < maxTurns)
    {
        if (state.playerLostOnDraw)
        {
            return -1;
        }
        RunTurn(state);
        if (CheckWinCondition(state))
        {
            return state.turnNumber;
        }
    }
    return -1;
}

void GameEngine::RunTurn(GameState& state)
{
    ++state.turnNumber;
    UntapStep(state);
    UpkeepStep(state);
    DrawStep(state);
    if (state.playerLostOnDraw)
    {
        return;
    }
    MainPhase(state, /*isPreCombat=*/true);
    CombatPhase(state);
    MainPhase(state, /*isPreCombat=*/false);
    EndStep(state);
    CleanupStep(state);
}

void GameEngine::UntapStep(GameState& state)
{
    state.phase = Phase::Beginning;
    state.step  = Step::Untap;
    Player& ap = state.ActivePlayer();
    ap.landsPlayedThisTurn    = 0;
    ap.bonusLandDropsThisTurn = 0;
    for (Permanent& p : state.battlefield)
    {
        if (p.controller == &ap)
        {
            p.tapped          = false;
            p.enteredThisTurn = false;
        }
    }
    // Untap is a turn-based action; no priority is passed (CR 502).
}

void GameEngine::UpkeepStep(GameState& state)
{
    state.step = Step::Upkeep;
    // TODO: fire upkeep triggered abilities (Phase 1.2)
    ResolveStack(state);
}

void GameEngine::DrawStep(GameState& state)
{
    state.step = Step::Draw;
    Player& ap = state.ActivePlayer();
    if (ap.library.empty())
    {
        state.playerLostOnDraw = true;
        return;
    }
    ap.hand.push_back(ap.library.DrawTop());
    // TODO: fire draw-step triggered abilities (Phase 1.2)
    ResolveStack(state);
}

void GameEngine::MainPhase(GameState& state, bool isPreCombat)
{
    state.phase = isPreCombat ? Phase::PreCombatMain : Phase::PostCombatMain;
    state.step  = Step::MainPhase;
    m_ai.TakeTurn(state, isPreCombat);
    ResolveStack(state);
}

void GameEngine::CombatPhase(GameState& state)
{
    state.phase = Phase::Combat;

    state.step = Step::BeginCombat;
    // TODO: "beginning of combat" triggers (Phase 1.2)
    ResolveStack(state);

    state.step = Step::DeclareAttackers;
    std::vector<Permanent*> attackers = m_ai.DeclareAttackers(state);
    // Phase 1: opponent has no blockers; all attackers deal damage unblocked.
    ResolveStack(state);

    state.step = Step::CombatDamage;
    Player& opp = state.Opponent();
    for (Permanent* attacker : attackers)
    {
        opp.life -= attacker->EffectivePower();
        if (!attacker->card.HasKeyword(Keyword::Vigilance))
        {
            attacker->tapped = true;
        }
    }
    CheckStateBasedActions(state);
    ResolveStack(state);

    state.step = Step::EndCombat;
    ResolveStack(state);
}

void GameEngine::EndStep(GameState& state)
{
    state.phase = Phase::Ending;
    state.step  = Step::End;
    // TODO: "end of turn" triggered abilities (Phase 1.2)
    ResolveStack(state);
}

void GameEngine::CleanupStep(GameState& state)
{
    state.step = Step::Cleanup;
    Player& ap = state.ActivePlayer();

    // Discard to maximum hand size (7 by default; hand-size modifiers deferred to Phase 1.2)
    while (ap.hand.size() > 7)
    {
        Card* discard = m_ai.ChooseDiscard(state);
        ap.graveyard.push_back(*discard);
        ap.hand.erase(std::find_if(ap.hand.begin(), ap.hand.end(),
            [discard](const Card& c) { return &c == discard; }));
    }

    // Remove all damage marks from permanents (CR 514.2)
    for (Permanent& p : state.battlefield)
    {
        p.damage = 0;
    }

    // TODO: "until end of turn" effects expire here (Phase 1.2)
}

void GameEngine::ResolveStack(GameState& state)
{
    // In Phase 1 the opponent passes all priority automatically.
    // The active player places spells/abilities on the stack during TakeTurn();
    // here we resolve everything that was queued.
    while (!state.stack.empty())
    {
        // Both players have passed priority — resolve top entry (CR 608).
        StackEntry entry = state.stack.back();
        state.stack.pop_back();
        // TODO: dispatch to card-specific resolve logic via CardDatabase (Phase 1.2)
        (void)entry;
        CheckStateBasedActions(state);
    }
}

void GameEngine::CheckStateBasedActions(GameState& state)
{
    // Must run to a fixed point — repeat until no change (CR 704.3)
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (std::vector<Permanent>::iterator it = state.battlefield.begin(); it != state.battlefield.end(); )
        {
            Permanent& p = *it;
            bool destroy = p.markedForDestruction;
            if (p.card.IsCreature())
            {
                if (p.EffectiveToughness() <= 0)
                {
                    destroy = true;
                }
                if (p.damage >= p.EffectiveToughness() && !p.card.HasKeyword(Keyword::Indestructible))
                {
                    destroy = true;
                }
            }
            // TODO: planeswalker 0-loyalty, aura without legal attachment, legend rule (Phase 1.2)
            if (destroy)
            {
                p.controller->graveyard.push_back(p.card);
                it = state.battlefield.erase(it);
                changed = true;
            }
            else
            {
                ++it;
            }
        }
    }
}

bool GameEngine::CheckWinCondition(const GameState& state) const
{
    return state.Opponent().HasLost();
}
