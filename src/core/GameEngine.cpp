#include "GameEngine.h"
#include "EffectHandler.h"
#include "SpellEffects.h"
#include "../ai/AIEngine.h"
#include "../cards/CardDatabase.h"
#include <algorithm>

GameEngine::GameEngine(AIEngine& ai) : m_ai(ai) {}

void GameEngine::SetLogger(GameLogger* logger)
{
    m_logger = logger;
    m_ai.SetLogger(logger);
}

// ---- Helper: collect board state for logging ----

static void CollectBoardState(const GameState& state,
                               std::vector<int>& battlefield_out,
                               std::vector<int>& hand_out)
{
    const Player& ap = state.ActivePlayer();
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == state.active_player_index)
        {
            battlefield_out.push_back(p.card.m_number);
        }
    }
    for (const Card& c : ap.hand)
    {
        hand_out.push_back(c.m_number);
    }
}

// ============================================================
// Public API
// ============================================================

int GameEngine::RunGame(GameState& state, int max_turns)
{
    m_ai.HandleMulligan(state, max_turns);

    if (m_logger)
    {
        const Player& ap = state.ActivePlayer();
        std::vector<int> nums;
        std::vector<std::string> names;
        for (const Card& c : ap.hand)
        {
            nums.push_back(c.m_number);
            names.push_back(c.m_name);
        }
        m_logger->LogOpeningHand(nums, names);
    }

    int win_turn = PlayOut(state, max_turns);
    m_ai.OnGameEnd(state, win_turn);
    return win_turn;
}

int GameEngine::PlayOut(GameState& state, int max_turns)
{
    m_ai.SetMaxTurns(max_turns);
    while (state.turn_number < max_turns)
    {
        if (state.player_lost_on_draw) { return -1; }
        RunTurn(state);
        // Check opponent loss first: if we dealt lethal and also triggered ourselves to
        // death in the same turn, the win still counts.
        if (CheckWinCondition(state)) { return state.turn_number; }
        if (state.ActivePlayer().HasLost()) { return -1; }
    }
    return -1;
}

// ============================================================
// Turn structure
// ============================================================

void GameEngine::RunTurn(GameState& state)
{
    ++state.turn_number;
    UntapStep(state);
    UpkeepStep(state);
    DrawStep(state);
    if (state.player_lost_on_draw) { return; }
    MainPhase(state, /*is_pre_combat=*/true);
    CombatPhase(state);
    // State-based action (CR 704.5a / 104.3a): a player at 0 or less life loses
    // immediately -- before its controller gets priority for the post-combat main. Without
    // this check the turn would continue and a post-combat lifegain could "un-kill" a dead
    // opponent (e.g. Swords to Plowshares' controller-lifegain rider once a Tainted Remedy
    // has left), pushing the realised win to a later turn than the search (which checks
    // opp.life <= 0 right after combat, SimulateToEndImpl) predicts. Mirrors the leaf so the
    // executor realises the win the search commits. Opp-loss wins even if we also died this
    // turn (CheckWinCondition is the opponent's loss; PlayOut handles our own loss after).
    if (CheckWinCondition(state)) { return; }
    MainPhase(state, /*is_pre_combat=*/false);
    EndStep(state);
    CleanupStep(state);
}

void GameEngine::UntapStep(GameState& state)
{
    state.phase = Phase::Beginning;
    state.step  = Step::Untap;
    state.opponent_lost_life_this_turn = false;
    Player& ap = state.ActivePlayer();
    ap.lands_played_this_turn    = 0;
    ap.bonus_land_drops_this_turn = 0;
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index == state.active_player_index)
        {
            p.tapped            = false;
            p.entered_this_turn = false;
        }
    }

    // Materialise any passive opponent creatures scheduled for this turn.
    int opp_index = 1 - state.active_player_index;
    if (state.opponent_spawns)
    for (const OpponentSpawn& spawn : *state.opponent_spawns)
    {
        if (spawn.turn != state.turn_number) { continue; }

        Card token;
        token.m_name      = std::to_string(spawn.power) + "/"
                          + std::to_string(spawn.toughness) + " Creature";
        token.RehashName();
        token.AddType(CardType::Creature);
        token.m_power     = spawn.power;
        token.m_toughness = spawn.toughness;

        Permanent perm;
        perm.card             = token;
        perm.controller_index = opp_index;
        perm.owner_index      = opp_index;
        // entered_this_turn = false: passive creatures are treated as already present,
        // not subject to summoning sickness (irrelevant since they never attack).
        state.battlefield.push_back(perm);
    }
}

void GameEngine::UpkeepStep(GameState& state)
{
    state.step = Step::Upkeep;

    // Aether Vial: add a charge counter each upkeep using an AI heuristic.
    // Stop adding when the counter count reaches the most common creature MV in hand,
    // so the Vial deploys creatures at maximum efficiency.
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def || !def->params.upkeep_adds_charge) { continue; }

        // Vial-as-a-choice: the charge decision is delegated to the AI. Its default is the
        // same heuristic as before (charge up to the deck's dominant creature MV), so the
        // normal AI is byte-identical; an external controller (claude-play) can override.
        if (m_ai.DecideVialCharge(state, p)) { ++p.charge_counters; }
    }

    // Upkeep token creation (e.g. Thrumming Hivepool: create two 1/1 Sliver tokens).
    // Iterate over initial size only — tokens added here must not trigger their own upkeep.
    int bf_size = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < bf_size; ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != state.active_player_index) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def || def->params.upkeep_creates_tokens <= 0) { continue; }
        for (int t = 0; t < def->params.upkeep_creates_tokens; ++t)
        {
            CreateToken(state, state.active_player_index,
                        def->params.upkeep_token_power,
                        def->params.upkeep_token_toughness,
                        def->params.upkeep_token_subtypes);
        }
    }

    ResolveStack(state);
}

void GameEngine::DrawStep(GameState& state)
{
    state.step = Step::Draw;

    // Player on the play skips their first draw step (CR 103.8a).
    if (state.on_the_play && state.turn_number == 1)
    {
        ResolveStack(state);
        return;
    }

    Player& ap = state.ActivePlayer();
    if (ap.library.empty())
    {
        state.player_lost_on_draw = true;
        return;
    }
    Card drawn = ap.library.DrawTop();
    if (m_logger)
    {
        std::vector<int> bf, hand;
        CollectBoardState(state, bf, hand);
        m_logger->StartPhase(state.turn_number, "DRAW");
        m_logger->LogDraw(drawn.m_number, drawn.m_name);
        hand.push_back(drawn.m_number);
        m_logger->CommitPhase(ap.life, state.Opponent().life, bf, hand);
    }
    ap.hand.push_back(std::move(drawn));
    ResolveStack(state);
}

void GameEngine::MainPhase(GameState& state, bool is_pre_combat)
{
    state.phase = is_pre_combat ? Phase::PreCombatMain : Phase::PostCombatMain;
    state.step  = Step::MainPhase;

    if (m_logger)
    {
        m_logger->StartPhase(state.turn_number, is_pre_combat ? "MAIN_1" : "MAIN_2");
    }

    // Initial cast pass. The AI resolves each cast before the next (we don't model
    // priority, so this reproduces legal sequential play: prowess/lords/spectacle and
    // on-cast triggers see the up-to-date board and life). TakeTurn returns whether it
    // cast a draw-engine spell (DrawUntilNonland / cascade) whose resolution can put
    // new castable spells in hand (e.g. Land's Edge found by Treasure Hunt).
    auto resolver = [this](GameState& s) { ResolveStack(s); };
    bool may_draw_spells = m_ai.TakeTurn(state, is_pre_combat, resolver);
    ResolveStack(state);  // resolve any trailing entries; usually empty after sequential casts

    // Give the AI a second opportunity to cast those newly drawn spells.
    if (may_draw_spells)
    {
        m_ai.TakeTurn(state, is_pre_combat, resolver);
        ResolveStack(state);
    }

    // Discard lands to Land's Edge after stack resolves so Treasure Hunt's drawn
    // lands are in hand and any Land's Edge cast this turn has entered the battlefield.
    m_ai.ActivateLandsEdge(state);

    if (m_logger)
    {
        std::vector<int> bf, hand;
        CollectBoardState(state, bf, hand);
        m_logger->CommitPhase(state.ActivePlayer().life, state.Opponent().life, bf, hand);
    }
}

void GameEngine::CombatPhase(GameState& state)
{
    state.phase = Phase::Combat;

    state.step = Step::BeginCombat;
    ResolveStack(state);

    // Legend rule (CR 704.5j): a duplicate legendary (e.g. a second Haytham Kenway) is
    // put into the graveyard before attackers are declared, so duplicate legendary lords
    // cannot double-count their continuous buffs. No-op for decks with no legendaries.
    EnforceLegendRule(state, state.active_player_index);

    state.step = Step::DeclareAttackers;
    std::vector<Permanent*> declared = m_ai.DeclareAttackers(state);
    ResolveStack(state);

    if (m_logger) { m_logger->StartPhase(state.turn_number, "COMBAT"); }

    state.step = Step::CombatDamage;
    Player& opp = state.Opponent();

    // Convert the declared attackers to stable indices BEFORE any token creation (which
    // push_backs onto the battlefield and would invalidate the pointers). Appending tokens
    // does not shift existing indices.
    std::vector<int> atk_idx;
    atk_idx.reserve(declared.size());
    for (Permanent* p : declared)
    {
        atk_idx.push_back(static_cast<int>(p - state.battlefield.data()));
    }

    // Attack triggers that create tapped-and-attacking tokens (Adeline). Fire only when at
    // least one creature is attacking; the new tokens deal damage this combat too.
    if (!atk_idx.empty())
    {
        int tok_start = FireAttackCreateTokens(state, state.active_player_index);
        for (int i = tok_start; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            atk_idx.push_back(i);
        }
    }

    // Exalted (Ignoble Hierarch): +1/+1 per Exalted ability to a creature attacking ALONE.
    int exalted_bonus = (static_cast<int>(atk_idx.size()) == 1)
                        ? CountExalted(state.battlefield, state.active_player_index) : 0;

    int total_combat_dmg = 0;
    for (int idx : atk_idx)
    {
        Permanent* attacker = &state.battlefield[idx];
        bool animated = attacker->is_animated;
        auto [lord_pb, lord_tb] = ComputeLordBonus(
            attacker->card, state.battlefield, state.active_player_index, animated, attacker);
        bool ds = animated
            ? HasDoubleStrikeFromLords(attacker->card, state.battlefield, state.active_player_index, true)
            : (attacker->card.HasKeyword(Keyword::DoubleStrike)
               || HasDoubleStrikeFromLords(attacker->card, state.battlefield, state.active_player_index));
        int animate_pw = 0;
        int dynamic_pw = 0;
        const CardDefinition* adef = CardDatabase::Instance().LookupCached(attacker->card);
        if (adef)
        {
            if (animated) { animate_pw = adef->params.animate_power; }
            dynamic_pw = DynamicBasePower(*adef, state, state.active_player_index);
        }
        int base_power = animate_pw + dynamic_pw + attacker->EffectivePower() + lord_pb + exalted_bonus;
        int power = base_power * (ds ? 2 : 1);
        opp.life -= power;
        total_combat_dmg += power;
        if (power > 0) { state.opponent_lost_life_this_turn = true; }
        if (!attacker->card.HasKeyword(Keyword::Vigilance)) { attacker->tapped = true; }
    }

    // Attack triggers (e.g. Leeching Sliver: each attacking Sliver costs the opponent 1 life).
    std::vector<const Permanent*> attacker_ptrs;
    attacker_ptrs.reserve(atk_idx.size());
    for (int idx : atk_idx) { attacker_ptrs.push_back(&state.battlefield[idx]); }
    int trigger_life_loss = CountAttackTriggerLifeLoss(
        state.battlefield, state.active_player_index, attacker_ptrs);
    if (trigger_life_loss > 0)
    {
        // Life loss, not combat damage: reduce life directly and mark the lost-life flag
        // (drives spectacle). Folded into total_combat_dmg only for the attack log total.
        opp.life -= trigger_life_loss;
        total_combat_dmg += trigger_life_loss;
        state.opponent_lost_life_this_turn = true;
    }

    if (m_logger && total_combat_dmg > 0)
    {
        m_logger->LogAttack(total_combat_dmg, opp.life);
    }

    CheckStateBasedActions(state);
    ResolveStack(state);

    state.step = Step::EndCombat;
    ResolveStack(state);

    if (m_logger)
    {
        std::vector<int> bf, hand;
        CollectBoardState(state, bf, hand);
        m_logger->CommitPhase(state.ActivePlayer().life, opp.life, bf, hand);
    }
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

    // Check for "no maximum hand size" effects (e.g. Reliquary Tower).
    bool unlimited_hand = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        auto def = CardDatabase::Instance().LookupCached(p.card);
        if (def && def->params.no_max_hand_size) { unlimited_hand = true; break; }
    }

    // Discard down to maximum hand size (7 unless unlimited by a permanent).
    if (!unlimited_hand && ap.hand.size() > 7 && m_logger)
    {
        std::vector<int> bf, hand;
        CollectBoardState(state, bf, hand);
        m_logger->StartPhase(state.turn_number, "CLEANUP");
    }
    while (!unlimited_hand && ap.hand.size() > 7)
    {
        Card* discard = m_ai.ChooseDiscard(state);
        if (m_logger) { m_logger->LogDiscard(discard->m_number, discard->m_name); }
        ap.graveyard.push_back(*discard);
        ap.hand.erase(std::find_if(ap.hand.begin(), ap.hand.end(),
            [discard](const Card& c) { return &c == discard; }));
    }
    if (m_logger && m_logger->InPhase())
    {
        std::vector<int> bf, hand;
        CollectBoardState(state, bf, hand);
        m_logger->CommitPhase(ap.life, state.Opponent().life, bf, hand);
    }

    // Remove all damage marks, "until end of turn" boosts, and animation effects (CR 514.2).
    for (Permanent& p : state.battlefield)
    {
        p.damage           = 0;
        p.temp_power_bonus = 0;
        p.temp_tough_bonus = 0;
        p.is_animated      = false;
    }
}

// ============================================================
// Stack / state-based actions
// ============================================================

void GameEngine::ResolveStack(GameState& state)
{
    while (!state.stack.empty())
    {
        StackEntry entry = state.stack.back();
        state.stack.pop_back();
        auto def = CardDatabase::Instance().LookupCached(entry.source);
        if (!def) { continue; }

        // For draw spells, capture hand snapshot before resolution so we can log new cards.
        // ETB-dig creatures (Acclaimed Contender) also add a card to hand on resolution.
        bool is_draw_spell = (def->tmpl == CardTemplate::DrawUntilNonland
                              || def->params.draw > 0
                              || def->params.etb_dig_count > 0);
        std::vector<int> hand_before_nums;
        if (m_logger && is_draw_spell)
        {
            for (const Card& c : state.ActivePlayer().hand)
            { hand_before_nums.push_back(c.m_number); }
        }

        EffectHandler::Resolve(state, entry, *def);

        if (m_logger && is_draw_spell)
        {
            for (const Card& c : state.ActivePlayer().hand)
            {
                bool was_there = std::find(hand_before_nums.begin(), hand_before_nums.end(),
                                           c.m_number) != hand_before_nums.end();
                if (!was_there) { m_logger->LogDraw(c.m_number, c.m_name); }
            }
        }

        CheckStateBasedActions(state);
    }
}

void GameEngine::CheckStateBasedActions(GameState& state)
{
    // Sacrifice depletion lands (e.g. Saprazzan Skerry) whose counters have run out.
    SacrificeDepletedLands(state);

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (std::vector<Permanent>::iterator it = state.battlefield.begin();
             it != state.battlefield.end(); )
        {
            Permanent& p = *it;
            bool destroy = p.marked_for_destruction;
            if (p.card.IsCreature())
            {
                if (p.EffectiveToughness() <= 0) { destroy = true; }
                if (p.damage >= p.EffectiveToughness()
                    && !p.card.HasKeyword(Keyword::Indestructible)) { destroy = true; }
            }
            if (destroy)
            {
                state.players[p.controller_index].graveyard.push_back(p.card);
                it = state.battlefield.erase(it);
                changed = true;
            }
            else { ++it; }
        }
    }
}

bool GameEngine::CheckWinCondition(const GameState& state) const
{
    return state.Opponent().HasLost();
}
