#pragma once
// CONTROL-EFFECT INJECTION (PROTOTYPE; docs/design/control-scoring-mode.md). Default OFF: inert unless a
// --batch job carries "ctrl_inject": "<effect>[;<effect>...]", each effect "<kind>:<turn>[:<arg>]".
// It disrupts the GOLDFISHING deck as if an opponent's control spell had resolved. Board / hand effects
// apply at the start of the goldfisher's turn <turn> (after the round counter advances, before untap): an
// opponent's sorcery cast on its own preceding turn. `counter` applies during turn <turn> itself.
// The goldfisher's search is NOT told in advance (the lookahead of turn <turn>-1 never sees it); the real
// game drops its committed line and re-searches from the disrupted board. Only the REAL game is disrupted,
// never a bottoming / keep trial on the shared engine (AIEngine::InRolloutForCtrlInject).
//
// Kinds (ours = permanents the goldfisher controls; an <arg> index at or past the candidate count is a
// NO-OP, reported applied=0, so a per-target sweep over 0..k-1 is exact):
//   land:T:i      destroy our i-th land (battlefield order)
//   creature:T:i  destroy our i-th creature
//   bigcrt:T      destroy our creature with the largest effective power (first on ties)
//   nonland:T:i   destroy our i-th nonland permanent
//   sweep:T       destroy every creature we control
//   discard:T:i   our i-th hand card to the graveyard
//   bigdisc:T     discard our highest-mana-value NONLAND hand card (Thoughtseize-like, first on ties)
//   counter:T:m   counter the first spell WE cast during turn T whose mana value is >= m (default 1);
//                 it goes to the graveyard, the mana stays spent
//   skip:T        we take no part of turn T (a one-turn Time Walk for the opponent)
// Simplifications (prototype; disclosed in the design doc): the Indestructible keyword is honoured but no
// other protection; "dies" / leaves watchers and discard triggers (madness) do not fire; a destroyed token
// ceases to exist; a countered spell's cast triggers have already fired.
#include "GameState.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace ctrlinject
{
enum class Kind { None, Land, Creature, BigCreature, Discard, BigDiscard, Sweep, Nonland, Counter, Skip };

struct Effect
{
    Kind kind  = Kind::None;
    int  turn  = 0;
    int  arg   = 0;
};

using Spec = std::vector<Effect>;

// Per-game schedule, installed by the batch worker before each game (empty for every job without the field).
inline thread_local Spec t_spec;
inline thread_local std::vector<char> t_done;
inline thread_local const char* t_job = nullptr;
inline thread_local int t_gi = -1;

inline void Install(const Spec& s, const char* job, int gi)
{
    t_spec = s;
    t_done.assign(s.size(), 0);
    t_job = job;
    t_gi = gi;
}
inline void Clear() { t_spec.clear(); t_done.clear(); t_job = nullptr; t_gi = -1; }
inline bool Armed() { return !t_spec.empty(); }

inline Effect ParseOne(const std::string& s)
{
    Effect e;
    const auto c1 = s.find(':');
    const std::string k = s.substr(0, c1);
    if      (k == "land")     { e.kind = Kind::Land; }
    else if (k == "creature") { e.kind = Kind::Creature; }
    else if (k == "bigcrt")   { e.kind = Kind::BigCreature; }
    else if (k == "discard")  { e.kind = Kind::Discard; }
    else if (k == "bigdisc")  { e.kind = Kind::BigDiscard; }
    else if (k == "sweep")    { e.kind = Kind::Sweep; }
    else if (k == "nonland")  { e.kind = Kind::Nonland; }
    else if (k == "counter")  { e.kind = Kind::Counter; e.arg = 1; }
    else if (k == "skip")     { e.kind = Kind::Skip; }
    else { throw std::runtime_error("ctrl_inject: unknown kind \"" + k + "\""); }
    if (c1 == std::string::npos) { throw std::runtime_error("ctrl_inject: missing turn in \"" + s + "\""); }
    const auto c2 = s.find(':', c1 + 1);
    e.turn = std::stoi(s.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1));
    if (c2 != std::string::npos) { e.arg = std::stoi(s.substr(c2 + 1)); }
    return e;
}

inline Spec Parse(const std::string& s)
{
    Spec out;
    std::size_t at = 0;
    while (at < s.size())
    {
        const auto semi = s.find(';', at);
        const std::string one = s.substr(at, semi == std::string::npos ? std::string::npos : semi - at);
        if (!one.empty()) { out.push_back(ParseOne(one)); }
        if (semi == std::string::npos) { break; }
        at = semi + 1;
    }
    return out;
}

inline void Report(const GameState& state, const Effect& e, int count, int applied, const std::string& what)
{
    std::fprintf(stderr, "[ctrl-inject] job=%s gi=%d turn=%d kind=%d arg=%d count=%d applied=%d what=%s\n",
                 t_job ? t_job : "-", t_gi, state.turn_number, static_cast<int>(e.kind), e.arg, count, applied,
                 what.c_str());
}
}  // namespace ctrlinject
