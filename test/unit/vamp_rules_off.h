#pragma once
// Pins the USER's 2026-10-21 Vampires play rules OFF for one test case (restored on scope exit):
// MTG_VAMP_FREE_DISCARD (the outlet discard pins restricted to the free discards), MTG_VAMP_NO_STEAL
// (Captivating Vampire's steal never offered), MTG_VAMP_SOCIALITE_M2 (no pre-combat Socialite). A test of
// the MACHINERY those rules restrict -- the steal's legality / effect / folds, the madness fan, the bucket
// order of every pin -- pins them off so it keeps testing that machinery; test_vampires_user_rules.cpp
// tests the rules themselves.
#include "ai/HeuristicArm.h"

struct VampRulesOff
{
    VampRulesOff()
    {
        heurarm::t_arm[heurarm::VAMP_FREE_DISCARD] = 0;
        heurarm::t_arm[heurarm::VAMP_NO_STEAL]     = 0;
        heurarm::t_arm[heurarm::VAMP_SOCIALITE_M2] = 0;
    }
    ~VampRulesOff()
    {
        heurarm::t_arm[heurarm::VAMP_FREE_DISCARD] = -1;
        heurarm::t_arm[heurarm::VAMP_NO_STEAL]     = -1;
        heurarm::t_arm[heurarm::VAMP_SOCIALITE_M2] = -1;
    }
    VampRulesOff(const VampRulesOff&) = delete;
    VampRulesOff& operator=(const VampRulesOff&) = delete;
};
