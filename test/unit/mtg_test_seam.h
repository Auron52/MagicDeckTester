// Friend of AIEngine (see AIEngine.h): the only sanctioned way for tests to reach the executor's
// private paths. ONE definition, shared by every test TU that needs it (a per-file copy with
// different members would be an ODR violation).
#pragma once

#include "ai/AIEngine.h"

#include <string>

struct MtgTestSeam
{
    static bool TapForCost(AIEngine& e, GameState& s, const ManaCost& c, ManaPool& avail,
                           bool for_creature)
    { return e.TapForCost(s, c, avail, for_creature); }

    // The executor's hand cast (pays, removes the card, pushes the StackEntry) -- exposed so a test
    // can read what the real executor carries on the stack entry for a given searched axis. Like
    // cast_by_name it TAKES whatever cast riders are armed (SetPendingLandPut below; nothing else
    // arms them here), so a test sees exactly what the real cast entry would.
    static void CastSpellFromHand(AIEngine& e, GameState& s, Card& hand_card, ManaPool& avail,
                                  int chosen_x)
    {
        const CastRiders riders = e.m_cast_riders.Take();
        e.CastSpellFromHand(s, hand_card, avail, 0, std::string(), chosen_x, 0, -1, -1, 0, std::string(), 0,
                            false, false, -1, 0, 0, 0, false, false, false, riders);
    }

    // The executor's hand cast in EVOKE mode carrying a PITCH rider (Fury's "Evoke--Exile a red card
    // from your hand"): exactly what cast_by_name passes for an evoke Action with pitch_name set (the
    // rider rides CastRiders, evoke rides the positional flag). Pushes the StackEntry; the caller
    // resolves it (EffectHandler::Resolve) to see the enter + evoke sacrifice.
    static void CastEvokeFromHand(AIEngine& e, GameState& s, Card& hand_card, ManaPool& avail,
                                  const std::string& pitch_name)
    {
        CastRiders riders = e.m_cast_riders.Take();
        riders.pitch_name = pitch_name;
        e.CastSpellFromHand(s, hand_card, avail, 0, std::string(), 0, 0, -1, -1, 0, std::string(), 0,
                            false, false, -1, 0, 0, 0, /*evoke=*/true, false, false, riders);
    }

    // Commit ONE phase plan, so the engine's next TakeTurn for that phase EXECUTES it (the
    // committed-line pop) instead of searching -- the only way a test can hand the real executor a
    // specific plan, e.g. to check it realises a plan exactly as the rollout's ApplyPlan does.
    static void CommitPhasePlan(AIEngine& e, const TurnSolver::Plan& p, bool is_pre_combat)
    {
        TurnSolver::PhasePlan pp;
        pp.is_pre_combat = is_pre_combat;
        pp.plan          = p;
        e.m_committed_line.push_back(pp);
    }

    // What every plan-execution site does before cast_by_name: arm the land-put credit the cast's
    // variant carries (Action::rock_mana -- Search for Tomorrow hard-cast), a cast rider taken by the
    // next cast entry onto StackEntry::land_put_credit.
    static void SetPendingLandPut(AIEngine& e, const ManaPool& credit) { e.m_cast_riders.land_put = credit; }

    // How many breakpoint continuations TakeTurn's resolve_draw_breakpoint has run on this engine.
    static long long BpResolves(const AIEngine& e) { return e.m_bp_resolves; }

    // The executor's realised dig (cycle from hand / sacrifice-to-draw): the lockstep twin of the
    // rollout dig loop and the human-play DigDraw action. Returns PerformDig's drew-a-land flag.
    static bool PerformDig(AIEngine& e, GameState& s, const std::string& source, bool is_sacrifice)
    { return e.PerformDig(s, source, is_sacrifice); }
    // ...its full form (Unpredictable Cyclone phase 3): the searched pins (Curator's scry j, Cyclone's
    // free-cast choice) and the resolver for what a replaced draw put on the stack. Returns the tri-state.
    static AIEngine::DigOutcome PerformDigFull(AIEngine& e, GameState& s, const std::string& source,
                                               bool is_sacrifice, int scry_pin, int fc_pin,
                                               const std::function<void(GameState&)>& resolve)
    { return e.PerformDigFull(s, source, is_sacrifice, scry_pin, fc_pin, resolve); }
    // The reactive (uncommitted / d0) dig, with an optional cycle seed.
    static bool UseSurplusLandAbilities(AIEngine& e, GameState& s, const std::function<void(GameState&)>& resolve,
                                        int dig_choice, const Action* seed)
    { return e.UseSurplusLandAbilities(s, resolve, dig_choice, seed); }
};
