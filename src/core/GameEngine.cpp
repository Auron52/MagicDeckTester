#include "GameEngine.h"
#include "EffectHandler.h"
#include "SpellEffects.h"
#include "../ai/AIEngine.h"
#include "../ai/Combat.h"
#include "../cards/CardDatabase.h"
#include <algorithm>
#include <iostream>   // MTG_FB_TRACE diagnostic

GameEngine::GameEngine(AIEngine& ai) : m_ai(ai) {}

void GameEngine::SetLogger(GameLogger* logger)
{
    m_logger = logger;
    m_ai.SetLogger(logger);
}

// ---- Helper: collect board state for logging ----

static void CollectBoardState(const GameState& state,
                               std::vector<GameLogger::PermSnapshot>& battlefield_out,
                               std::vector<int>& hand_out,
                               std::vector<GameLogger::PermSnapshot>& opp_battlefield_out,
                               std::vector<int>& graveyard_out,
                               std::vector<int>& staged_out)
{
    const Player& ap = state.ActivePlayer();
    for (const Permanent& p : state.battlefield)
    {
        GameLogger::PermSnapshot snap;
        snap.card_num  = p.card.m_number;
        snap.card_name = p.card.m_name;
        snap.tapped    = p.tapped;
        snap.is_land   = p.card.IsLand();   // REAL type, so the viewer never name-guesses zones

        // Surface counters as viewer badges (depletion on Saprazzan Skerry, charge on Aether Vial,
        // verse on Aria of Flame, +1/+1, etc.).
        for (const Counter& c : p.counters)
        {
            const char* kind = c.type == Counter::Type::PlusOnePlusOne   ? "+1/+1"
                             : c.type == Counter::Type::MinusOneMinusOne ? "-1/-1"
                             : c.type == Counter::Type::Loyalty          ? "loyalty"
                             : c.type == Counter::Type::Poison           ? "poison"
                             : c.type == Counter::Type::Depletion        ? "depletion"
                             :                                             "counter";
            if (c.count != 0) { snap.counters.push_back({ kind, c.count }); }
        }
        if (p.charge_counters != 0) { snap.counters.push_back({ "charge", p.charge_counters }); }
        if (p.verse_counters  != 0) { snap.counters.push_back({ "verse",  p.verse_counters  }); }
        if (p.storage_counters != 0) { snap.counters.push_back({ "storage", p.storage_counters }); }

        if (p.controller_index == state.active_player_index) { battlefield_out.push_back(std::move(snap)); }
        else { opp_battlefield_out.push_back(std::move(snap)); }  // Forbidden Orchard tokens / spawns
    }
    for (const Card& c : ap.hand)
    {
        hand_out.push_back(c.m_number);
        if (c.m_is_staged) { staged_out.push_back(c.m_number); }  // exiled-but-playable (Light Up etc.)
    }
    for (const Card& c : ap.graveyard)
    {
        graveyard_out.push_back(c.m_number);
    }
}

// ============================================================
// Public API
// ============================================================

int GameEngine::RunGame(GameState& state, int max_turns)
{
    // Capture scry/dig reveals during this real game's resolution. The search/rollout
    // pauses this (RevealLogPause) so only actual game reveals are logged. Restored at
    // scope exit so a thread reused across games never leaks a stale logger.
    struct RevealScope {
        GameLogger* prev;
        RevealScope(GameLogger* l) : prev(g_reveal_logger) { g_reveal_logger = l; }
        ~RevealScope() { g_reveal_logger = prev; }
    } reveal_scope(m_logger);

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
    return PlayOutFrom(state, max_turns, ResumeAt::NewTurn);
}

int GameEngine::PlayOutFrom(GameState& state, int max_turns, ResumeAt from)
{
    m_ai.SetMaxTurns(max_turns);
    // Finish the turn already in progress before the normal fresh-turn loop takes over.
    if (from != ResumeAt::NewTurn)
    {
        RunTurnFrom(state, from);
        if (CheckWinCondition(state))       { return state.turn_number; }
        if (state.ActivePlayer().HasLost()) { return -1; }
    }
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
    RunTurnFrom(state, ResumeAt::NewTurn);
}

// One turn, entered at `from`. NewTurn is the whole turn (the only entry the real game uses);
// every later entry finishes a turn a rollout was launched part-way through. The step order and
// the early-outs are identical either way -- this is a resume point, not a variant turn.
void GameEngine::RunTurnFrom(GameState& state, ResumeAt from)
{
    const int at = static_cast<int>(from);
    if (from == ResumeAt::NewTurn)
    {
        ++state.turn_number;
        UntapStep(state);
        UpkeepStep(state);   // includes UpkeepTail
    }
    else if (at <= static_cast<int>(ResumeAt::UpkeepTail))
    {
        // Resumed from inside the Vial charge loop: finish the upkeep the caller interrupted.
        UpkeepTail(state);
    }
    if (at <= static_cast<int>(ResumeAt::Draw))
    {
        DrawStep(state);
        if (state.player_lost_on_draw) { return; }
    }
    if (at <= static_cast<int>(ResumeAt::Main1)) { MainPhase(state, /*is_pre_combat=*/true); }
    if (at <= static_cast<int>(ResumeAt::Combat))
    {
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
    }
    if (at <= static_cast<int>(ResumeAt::Main2)) { MainPhase(state, /*is_pre_combat=*/false); }
    if (at <= static_cast<int>(ResumeAt::End))   { EndStep(state); }
    CleanupStep(state);
}

void GameEngine::UntapStep(GameState& state)
{
    state.phase = Phase::Beginning;
    state.step  = Step::Untap;
    state.opponent_lost_life_this_turn = false;
    state.floating_mana = ManaPool{};   // reserve (ritual) mana empties each turn (CR 500.4); no-op for non-ritual decks
    state.spells_cast_this_turn = 0;    // STORM counter resets each turn (lockstep w/ SimulateEndAndStartNextTurn); no-op for non-storm decks
    state.casts_remaining_this_turn = -1; // Irencrag "one more spell" budget clears each turn (see GameState); no-op for non-restrictor decks
    state.scripted_cheat_choice = -1;   // searched Lackey put is per-turn (lockstep w/ SimulateEndAndStartNextTurn)
    Player& ap = state.ActivePlayer();
    ap.lands_played_this_turn    = 0;
    ap.bonus_land_drops_this_turn = 0;
    ap.cards_drawn_this_turn     = 0;   // Fists of Flame drawn-count resets each turn (lockstep w/ SimulateEndAndStartNextTurn)
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index == state.active_player_index)
        {
            p.tapped            = false;
            p.entered_this_turn = false;
            p.storage_hold_this_turn = false;   // #6: the tap-vs-charge hold is a per-turn human choice
            p.colored_cast_lifegain_used_this_turn = false;   // Ancient Cornucopia once-each-turn
            p.loyalty_activated_this_turn = false;   // planeswalkers: one loyalty ability per turn
        }
    }
    // Maelstrom Archangel: an unspent banked free cast expires with the turn (lockstep with the
    // rollout's per-turn reset).
    state.free_casts_available = 0;

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
        // Enter-watchers (Suture Priest / Wardens): a scheduled opponent spawn IS a creature
        // entering under the opponent's control. This site does not run the universal enter
        // cascade, so fire the watchers directly (mirrors the rollout's spawn site, lockstep).
        FireCreatureEnterWatchers(state, opp_index,
                                  static_cast<int>(state.battlefield.size()) - 1);
    }

    // Forbidden Orchard: one opponent 1/1 Spirit per Orchard the active player controls this turn
    // (assume each is tapped for mana). Same point + order as the rollout (TurnSolver SimulateToEnd)
    // so the executor and the search stay in lockstep. On-play copies are handled in TryPlaySpecificLand.
    SpawnForbiddenOrchardTokensTurnStart(state);
}

void GameEngine::UpkeepStep(GameState& state)
{
    state.step = Step::Upkeep;

    // Suspend (Lotus Bloom): at the controller's upkeep, remove one time counter from each suspended
    // card and CAST off suspend any whose last counter is now gone (arrive_turn <= this turn). Runs
    // after UntapStep, so the arrived artifact is untapped and available this turn. Mirrors the rollout
    // (SimulateEndAndStartNextTurn) -> lockstep. No-op for every deck without a suspended card.
    ProcessSuspendArrivals(state, state.active_player_index);

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

    UpkeepTail(state);
}

// The rest of the upkeep, after the Vial charge loop. Its own function so a searched Vial charge
// can resume here (ResumeAt::UpkeepTail) instead of jumping to the draw and losing this turn's
// upkeep tokens / stack resolution. Called unconditionally by UpkeepStep -> byte-identical.
void GameEngine::UpkeepTail(GameState& state)
{
    // #6 Dwarven Hold (storage_charge_mode "upkeep_if_tapped"): its tap-vs-charge commitment is made at
    // the UNTAP/UPKEEP step -- BEFORE the draw -- because the literal card charges by being HELD TAPPED
    // through untap ("if tapped at upkeep, +1"). So the non-clairvoyant human must decide to hold (charge)
    // without seeing this turn's draw -- strictly less information than Mercadian Bazaar, whose "{T}: put a
    // counter" is an active post-draw main-phase tap (surfaced later, in AIEngine's pre-main consult).
    // A "hold" flags the land not-live for the turn (StorageSourceLive) so it is never tapped for mana ->
    // stays untapped -> banks its counter at end of turn. Human-play only (chooser null autonomously / in
    // rollout) -> byte-identical for the search and every non-storage deck.
    // The AUTONOMOUS answer is provider-owned, exactly as it now is for Mercadian Bazaar's
    // main-phase variant (AIEngine): base returns false = never hold = the historical behaviour, so
    // this stays byte-identical. Previously the whole block was gated on the chooser being non-null,
    // which meant the search and the autonomous engine could not represent holding EITHER storage
    // land -- the two were only superficially different, and were in fact identically un-hooked.
    //
    // The pre-draw/post-draw asymmetry described above is real but it is an INFORMATION difference,
    // so it only bites a NON-CLAIRVOYANT decider (a human, or the NC model). A clairvoyant search
    // sees this turn's draw either way, so both lands should be equally representable to it.
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index) { continue; }
        if (p.tapped || p.storage_counters <= 0) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d || !d->params.storage_land || d->params.storage_charge_mode != "upkeep_if_tapped")
        { continue; }
        bool hold = ResolveProvider(state).StorageLandHold(
            state, state.active_player_index, *d, p.storage_counters);
        if (g_play_storage_hold_chooser)
        { hold = (*g_play_storage_hold_chooser)(state, p, p.storage_counters); }
        if (hold) { p.storage_hold_this_turn = true; }
    }

    // Upkeep token creation (e.g. Thrumming Hivepool: create two 1/1 Sliver tokens).
    // Iterate over initial size only — tokens added here must not trigger their own upkeep.
    int bf_size = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < bf_size; ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != state.active_player_index) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def) { continue; }
        // Fixed count (Thrumming Hivepool) or one per Equipment attached to THIS permanent
        // (Kemba, Kha Regent). Snapshot count + owner BEFORE creating: CreateToken push_backs
        // onto the battlefield, invalidating `p`. Mirrors TurnSolver (lockstep).
        int count = def->params.upkeep_creates_tokens;
        if (def->params.upkeep_tokens_per_equipment)
        { count += CountEquipmentAttachedTo(state, p.controller_index, p.card.m_number); }
        if (count <= 0) { continue; }
        const int tok_p = def->params.upkeep_token_power;
        const int tok_t = def->params.upkeep_token_toughness;
        const std::vector<std::string> tok_subs = def->params.upkeep_token_subtypes;
        for (int t = 0; t < count; ++t)
        {
            CreateToken(state, state.active_player_index, tok_p, tok_t, tok_subs);
        }
    }

    // Creature Giving upkeep triggers, in controller-optimal order: Varchild's War-Riders
    // cumulative-upkeep gifts FIRST (the fresh Survivors count toward DotH's >= 3), then the
    // Defense of the Heart sac-tutor check. Mirrors the rollout's identical block in
    // TurnSolver::SimulateEndAndStartNextTurn (lockstep); param-gated -> byte-identical elsewhere.
    PerformUpkeepCumulativeGifts(state);
    PerformUpkeepSacTutor(state);

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
    ap.cards_drawn_this_turn += 1;   // the draw step is a real CR-121 draw (Fists of Flame counts it)
    // Human-play (--claude-play) accurate draw reporting: record the per-turn draw so the viewer
    // can show exactly what was drawn this turn (nulled by RevealLogPause during search).
    if (g_play_draw_sink) { g_play_draw_sink->push_back({ state.turn_number, drawn.m_name.str() }); }
    if (m_logger)
    {
        std::vector<GameLogger::PermSnapshot> bf, obf;
        std::vector<int> hand, gy, staged;
        CollectBoardState(state, bf, hand, obf, gy, staged);
        m_logger->StartPhase(state.turn_number, "DRAW");
        m_logger->LogDraw(drawn.m_number, drawn.m_name);
        hand.push_back(drawn.m_number);
        m_logger->CommitPhase(ap.life, state.Opponent().life, bf, hand, obf, gy, staged);
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
    // MTG_FB_TRACE: DIAGNOSTIC (no play change). Firebreathing spends its pool without TAPPING
    // anything (ApplyFirebreathing takes the pool by value), which is only sound if combat is the
    // last mana use of the turn -- the comment on AIEngine::Firebreathe asserts exactly that. If a
    // post-combat main casts on a turn that pumped, the same mana was spent twice, and greedy-max
    // stops being provably dominant (it would be trading real main-2 casts for pump damage). This
    // prints the turns where that happens so the claim is measured, not assumed.
    static const bool s_fb_trace = EnvOn("MTG_FB_TRACE");
    const int fb_casts_before = state.spells_cast_this_turn;
    bool may_draw_spells = m_ai.TakeTurn(state, is_pre_combat, resolver);
    ResolveStack(state);  // resolve any trailing entries; usually empty after sequential casts

    // Give the AI a second opportunity to cast those newly drawn spells.
    if (may_draw_spells)
    {
        m_ai.TakeTurn(state, is_pre_combat, resolver);
        ResolveStack(state);
    }

    if (s_fb_trace && !is_pre_combat && g_fb_activations_this_turn > 0)
    {
        const int main2_casts = state.spells_cast_this_turn - fb_casts_before;
        std::cerr << "[fb_trace] pumped=" << g_fb_activations_this_turn
                  << " main2_casts=" << main2_casts
                  << (main2_casts > 0 ? "  DOUBLE-SPEND" : "  ok") << "\n";
    }

    // Discard lands to Land's Edge after stack resolves so Treasure Hunt's drawn
    // lands are in hand and any Land's Edge cast this turn has entered the battlefield.
    m_ai.ActivateLandsEdge(state);

    if (m_logger)
    {
        std::vector<GameLogger::PermSnapshot> bf, obf;
        std::vector<int> hand, gy, staged;
        CollectBoardState(state, bf, hand, obf, gy, staged);
        m_logger->CommitPhase(state.ActivePlayer().life, state.Opponent().life, bf, hand, obf, gy, staged);
    }
}

void GameEngine::CombatPhase(GameState& state)
{
    // Mana empties when leaving the pre-combat main phase (CR 500.4): drop any reserve
    // floated this main phase. Mirrors TurnSolver::SimulateCombat (lockstep). Off
    // (MTG_NO_FLOAT_LEFTOVER) -> no-op; byte-identical for non-floating decks regardless.
    if (FloatLeftoverManaEnabled()) { state.floating_mana = ManaPool{}; }
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

    // Goblins attack-trigger self-pumps (Piledriver +2/+0 per other attacking Goblin; Muxus +1/+1
    // per other Goblin you control): applied at declare-attackers BEFORE the damage loop reads power,
    // as until-end-of-turn temp bonuses. Mirrors TurnSolver::SimulateCombat (lockstep). Gated inert.
    ApplyAttackSelfPumps(state, state.active_player_index, atk_idx);

    // Two-Headed Hellkite attack-trigger draw (attack_draw_cards): drawn at declare-attackers,
    // so the cards are in hand for the post-combat main. Mirrors TurnSolver::SimulateCombat.
    ApplyAttackDrawTriggers(state, state.active_player_index, atk_idx);

    // Firebreathing (Scourge {R}:+1/+0 self, Lathliss {1}{R}: Dragons +1/+0 team): spend LEFTOVER
    // combat mana on attacker pumps BEFORE the damage loop reads their power. Delegated to the
    // AIEngine (it owns BuildAvailableMana/TapForCost); the shared ApplyFirebreathing reads a pool
    // byte-identical to the rollout's BuildPool -> lockstep. Inert without a firebreathing param.
    m_ai.Firebreathe(state, atk_idx);

    // Exalted (Ignoble Hierarch): +1/+1 per Exalted ability to a creature attacking ALONE.
    int exalted_bonus = (static_cast<int>(atk_idx.size()) == 1)
                        ? CountExalted(state.battlefield, state.active_player_index) : 0;

    const int opp_life_before = opp.life;                  // play-viewer event: "(before->after)"

    // Armored Skyhunter attack-trigger dig-and-attach: fired AFTER attack pumps/draws and BEFORE
    // the damage loop reads power, so a put-and-attached Colossus Hammer swings this combat.
    // Mirrors TurnSolver::SimulateCombat (lockstep). Param-gated inert for every other deck.
    FireAttackDigAttach(state, state.active_player_index, atk_idx);

    // Damage, attack triggers, Utvara tokens and the Goblin Lackey cheat are shared with the
    // rollout (ResolveCombatDamage, Combat.cpp) so the two can never disagree on what an attack
    // does. Only the real game collects the per-attacker descriptions for the play viewer.
    const CombatDamageResult combat =
        ResolveCombatDamage(state, atk_idx, exalted_bonus, /*collect_descs=*/g_play_event_sink != nullptr);
    const int total_combat_dmg = combat.total_damage;
    const int trigger_life_loss = combat.trigger_life_loss;

    if (m_logger && total_combat_dmg > 0)
    {
        m_logger->LogAttack(total_combat_dmg, opp.life);
    }
    // Play-viewer history: enumerate the attackers and the damage they dealt (real play only).
    if (g_play_event_sink && total_combat_dmg > 0)
    {
        std::string names;
        for (size_t i = 0; i < combat.attacker_descs.size(); ++i)
        { names += (i ? ", " : "") + combat.attacker_descs[i]; }
        if (trigger_life_loss > 0)
        { names += (names.empty() ? "" : " + ") + std::to_string(trigger_life_loss) + " (attack triggers)"; }
        EmitPlayEvent(state.turn_number, "combat",
                      "⚔ attacked: " + names + " — " + std::to_string(total_combat_dmg)
                      + " to opponent (" + std::to_string(opp_life_before) + "→" + std::to_string(opp.life) + ")");
    }

    CheckStateBasedActions(state);
    ResolveStack(state);

    state.step = Step::EndCombat;
    ResolveStack(state);

    if (m_logger)
    {
        std::vector<GameLogger::PermSnapshot> bf, obf;
        std::vector<int> hand, gy, staged;
        CollectBoardState(state, bf, hand, obf, gy, staged);
        m_logger->CommitPhase(state.ActivePlayer().life, opp.life, bf, hand, obf, gy, staged);
    }
}

void GameEngine::EndStep(GameState& state)
{
    state.phase = Phase::Ending;
    state.step  = Step::End;
    // "Exile those tokens at the beginning of the next end step" (Twinflame token copies,
    // Permanent::exile_at_end). Swept battlefield -> exile here, BEFORE cleanup (CR 512/514
    // order). Lockstep with the top of TurnSolver::SimulateEndAndStartNextTurn; no-op for every
    // deck that never sets the flag.
    for (int i = static_cast<int>(state.battlefield.size()) - 1; i >= 0; --i)
    {
        if (state.battlefield[i].exile_at_end)
        {
            state.exile.push_back(state.battlefield[i].card);
            state.battlefield.erase(state.battlefield.begin() + i);
        }
    }
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
        m_logger->StartPhase(state.turn_number, "CLEANUP");
    }
    while (!unlimited_hand && ap.hand.size() > 7)
    {
        Card* discard = m_ai.ChooseDiscard(state);
        // Human play: let the player pick WHICH card to discard (one per over-limit card). The
        // heuristic pick is the prepopulated default. Consulted only here in the real cleanup step
        // (search rollouts never reach GameEngine::CleanupStep), so autonomous play is unchanged.
        if (g_play_discard_chooser && !ap.hand.empty())
        {
            int heur = static_cast<int>(discard - &ap.hand[0]);
            if (heur < 0 || heur >= static_cast<int>(ap.hand.size())) { heur = 0; }
            std::vector<int> idxs(ap.hand.size());
            for (int i = 0; i < static_cast<int>(ap.hand.size()); ++i) { idxs[i] = i; }
            int chosen = (*g_play_discard_chooser)(state, state.active_player_index, idxs, heur);
            if (chosen >= 0 && chosen < static_cast<int>(ap.hand.size())) { discard = &ap.hand[chosen]; }
        }
        if (m_logger) { m_logger->LogDiscard(discard->m_number, discard->m_name); }
        // Progenitus: shuffled into its owner's library instead of the graveyard (replacement).
        if (!MaybeReplaceGraveyardWithLibraryShuffle(state, state.active_player_index, *discard))
        {
            ap.graveyard.push_back(*discard);
        }
        ap.hand.erase(std::find_if(ap.hand.begin(), ap.hand.end(),
            [discard](const Card& c) { return &c == discard; }));
    }
    if (m_logger && m_logger->InPhase())
    {
        std::vector<GameLogger::PermSnapshot> bf, obf;
        std::vector<int> hand, gy, staged;
        CollectBoardState(state, bf, hand, obf, gy, staged);
        m_logger->CommitPhase(ap.life, state.Opponent().life, bf, hand, obf, gy, staged);
    }

    // Remove all damage marks, "until end of turn" boosts, and animation effects (CR 514.2).
    for (Permanent& p : state.battlefield)
    {
        p.damage                = 0;
        p.pending_death_trigger = 0;   // delayed Searing Blood trigger expires with the damage marks
        p.temp_power_bonus      = 0;
        p.temp_tough_bonus      = 0;
        p.temp_haste            = false;   // "gains haste until end of turn" (Expedite) expires
        p.is_animated           = false;
    }

    // Storage-counter lands (Dwarven Hold, Mercadian Bazaar): a storage land left UNTAPPED this turn
    // (i.e. NOT tapped for a burst -- and not just-entered, which enters tapped) banks one storage
    // counter at end of turn. This is the faithful goldfish model of BOTH charge modes -- Dwarven's
    // "hold tapped, +1 at upkeep" and Mercadian's "{T}: put a counter" (its tap makes no other mana) --
    // which are both weakly-dominant "charge while idle" and reproduce the SAME +1-per-idle-turn
    // accumulation and any-turn burst availability (verified equal to literal play, off-by-one and
    // earliest-burst turn). A tapped storage land was burst (or entered) this turn -> no charge (you
    // never charge the turn you burst). Lockstep with the rollout (TurnSolver::SimulateEndAndStartNextTurn).
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.storage_land) { ++p.storage_counters; }
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
        // Collect creatures that died this pass so their death-watchers (Pashalik ping / Rundvelt
        // impulse / Mogg death token) fire AFTER the erase loop -- OnCreatureDies may append tokens
        // to the battlefield, which would invalidate the iterator if fired mid-loop.
        std::vector<std::pair<Card, int>> died;   // (dead card, controller)
        for (std::vector<Permanent>::iterator it = state.battlefield.begin();
             it != state.battlefield.end(); )
        {
            Permanent& p = *it;
            bool destroy = p.marked_for_destruction;
            const bool is_creature = p.card.IsCreature();
            if (is_creature)
            {
                int tough = p.EffectiveToughness();
                if (tough <= 0)
                {
                    // Characteristic P/T (Faeburrow Elder, base 0/0 + domain self-pump): the SBA
                    // must see the same static buffs combat/eval do (ComputeLordBonus), or a
                    // freshly-cast Faeburrow dies on ETB despite always counting its own G/W.
                    // Entered only when the raw toughness is already <= 0 -> byte-identical for
                    // every ordinary creature (their damage check keeps the raw value).
                    tough += ComputeLordBonus(p.card, state.battlefield,
                                              p.controller_index, p.is_animated, &p).second;
                    tough += EquipBonusFor(p, state).second;   // Grafted Wargear +3/+2 etc. --
                                                               // equipment toughness must be seen
                                                               // here or a Jitte -1/-1'd 0-tough
                                                               // host dies through its equipment
                    if (tough <= 0) { destroy = true; }
                }
                if (p.damage >= tough
                    && !p.card.HasKeyword(Keyword::Indestructible)) { destroy = true; }
            }
            if (destroy)
            {
                if (is_creature) { died.emplace_back(p.card, p.controller_index); }
                state.players[p.controller_index].graveyard.push_back(p.card);
                it = state.battlefield.erase(it);
                changed = true;
            }
            else { ++it; }
        }
        // Equipment falls off a dead host (CR 301.5c): zero equipped_to for every died creature.
        // The Equipment itself stays on the battlefield.
        for (const std::pair<Card, int>& dd : died)
        {
            for (Permanent& q : state.battlefield)
            {
                if (q.equipped_to == dd.first.m_number) { q.equipped_to = 0; }
            }
        }
        for (const std::pair<Card, int>& d : died)
        {
            OnCreatureDies(state, d.second, d.first);   // no-op unless a death-watcher is in play
        }
    }
}

bool GameEngine::CheckWinCondition(const GameState& state) const
{
    return state.Opponent().HasLost();
}
