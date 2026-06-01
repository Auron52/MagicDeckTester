#include "GameEngine.h"
#include "../ai/AIEngine.h"
#include <algorithm>

GameEngine::GameEngine(AIEngine& ai) : ai_(ai) {}

int GameEngine::runGame(GameState& state, int maxTurns)
{
    ai_.handleMulligan(state);
    while (state.turnNumber < maxTurns)
    {
        if (state.playerLostOnDraw)
        {
            return -1;
        }
        runTurn(state);
        if (checkWinCondition(state))
        {
            return state.turnNumber;
        }
    }
    return -1;
}

void GameEngine::runTurn(GameState& state)
{
    ++state.turnNumber;
    untapStep(state);
    upkeepStep(state);
    drawStep(state);
    if (state.playerLostOnDraw)
    {
        return;
    }
    mainPhase(state, /*isPreCombat=*/true);
    combatPhase(state);
    mainPhase(state, /*isPreCombat=*/false);
    endStep(state);
    cleanupStep(state);
}

void GameEngine::untapStep(GameState& state)
{
    state.phase = Phase::Beginning;
    state.step  = Step::Untap;
    Player& ap = state.activePlayer();
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

void GameEngine::upkeepStep(GameState& state)
{
    state.step = Step::Upkeep;
    // TODO: fire upkeep triggered abilities (Phase 1.2)
    resolveStack(state);
}

void GameEngine::drawStep(GameState& state)
{
    state.step = Step::Draw;
    Player& ap = state.activePlayer();
    if (ap.library.empty())
    {
        state.playerLostOnDraw = true;
        return;
    }
    ap.hand.push_back(ap.library.front());
    ap.library.erase(ap.library.begin());
    // TODO: fire draw-step triggered abilities (Phase 1.2)
    resolveStack(state);
}

void GameEngine::mainPhase(GameState& state, bool isPreCombat)
{
    state.phase = isPreCombat ? Phase::PreCombatMain : Phase::PostCombatMain;
    state.step  = Step::MainPhase;
    ai_.takeTurn(state, isPreCombat);
    resolveStack(state);
}

void GameEngine::combatPhase(GameState& state)
{
    state.phase = Phase::Combat;

    state.step = Step::BeginCombat;
    // TODO: "beginning of combat" triggers (Phase 1.2)
    resolveStack(state);

    state.step = Step::DeclareAttackers;
    std::vector<Permanent*> attackers = ai_.declareAttackers(state);
    // Phase 1: opponent has no blockers; all attackers deal damage unblocked.
    resolveStack(state);

    state.step = Step::CombatDamage;
    Player& opp = state.opponent();
    for (Permanent* attacker : attackers)
    {
        opp.life -= attacker->effectivePower();
        if (!attacker->card.hasKeyword(Keyword::Vigilance))
        {
            attacker->tapped = true;
        }
    }
    checkStateBasedActions(state);
    resolveStack(state);

    state.step = Step::EndCombat;
    resolveStack(state);
}

void GameEngine::endStep(GameState& state)
{
    state.phase = Phase::Ending;
    state.step  = Step::End;
    // TODO: "end of turn" triggered abilities (Phase 1.2)
    resolveStack(state);
}

void GameEngine::cleanupStep(GameState& state)
{
    state.step = Step::Cleanup;
    Player& ap = state.activePlayer();

    // Discard to maximum hand size (7 by default; hand-size modifiers deferred to Phase 1.2)
    while (ap.hand.size() > 7)
    {
        Card* discard = ai_.chooseDiscard(state);
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

void GameEngine::resolveStack(GameState& state)
{
    // In Phase 1 the opponent passes all priority automatically.
    // The active player places spells/abilities on the stack during takeTurn();
    // here we resolve everything that was queued.
    while (!state.stack.empty())
    {
        // Both players have passed priority — resolve top entry (CR 608).
        StackEntry entry = state.stack.back();
        state.stack.pop_back();
        // TODO: dispatch to card-specific resolve logic via CardDatabase (Phase 1.2)
        (void)entry;
        checkStateBasedActions(state);
    }
}

void GameEngine::checkStateBasedActions(GameState& state)
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
            if (p.card.isCreature())
            {
                if (p.effectiveToughness() <= 0)
                {
                    destroy = true;
                }
                if (p.damage >= p.effectiveToughness() && !p.card.hasKeyword(Keyword::Indestructible))
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

bool GameEngine::checkWinCondition(const GameState& state) const
{
    return state.opponent().hasLost();
}
