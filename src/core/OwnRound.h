#pragma once
// THE GOLDFISHER'S OWN ROUND: rounds in which it took a turn -- GameState::turn_number less the rounds a
// control-injection `skip` removed (GameState::ctrl_skipped_turns; core/ControlInject.h). It IS turn_number for every
// game whose batch job carries no ctrl_inject, so a site moved to it is byte-identical there by construction, and
// unlike TurnSeq() it does not count extra / free turns, so it is turn_number for every such deck too.
//
// A HEURISTIC that reads the round as a DEVELOPMENT CLOCK ("a turn-1 Vial", "ramp ranks high through turn 2",
// ExpectedAttacks' assumed 6-turn game) reads this, never turn_number. After a skip the round has advanced but the
// goldfisher's board has not; a round-keyed heuristic then plays the shifted position differently from the
// unskipped game a round earlier, and the analytic skip check (skip:T gives exactly +1) fails for a reason that is
// neither the rules nor the effect (docs/design/control-inject-harness.md, section 5 skip and section 9). Everything
// that IS the round -- the win turn, max_turns, the opponent's spawn schedule, the injection schedule, a diagnostic's
// turn filter -- stays on turn_number.
#include "GameState.h"

inline int OwnRound(const GameState& s) { return s.turn_number - static_cast<int>(s.ctrl_skipped_turns); }
