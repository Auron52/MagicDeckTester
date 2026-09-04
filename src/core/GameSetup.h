#pragma once
#include <cstdlib>

// PER-JOB GAME-SETUP overrides. Today: starting life, which is what "Two-Headed Giant" means
// for a goldfish (user, 2026-08-19: "regular play (20 life) and 2HG (30 life)").
//
// 2HG's other rules -- a shared turn, a teammate's board, one team life total -- are not
// modelled and are NOT what this is: the passive opponent never acts, so the only thing that
// changes for a goldfish measuring turn-to-kill is HOW MUCH DAMAGE IS REQUIRED. A deck that
// wins a 20-life race on turn 5 may need a materially different curve to win a 30-life race,
// and that is the question this lets us ask. Anything that reads a teammate is out of scope.
//
// Why a per-JOB thread_local and not an environment variable: exactly the reason
// ai/ValueArm.h exists. A process-wide `static const` would force one `mtg --batch` per life
// total, which strands cores on each invocation's tail and re-introduces the wave pattern
// CLAUDE.md's pooling rule forbids. As a job field, the 20-life and 30-life arms of the same
// comparison share ONE pooled queue and one tail.
//
// The env fallback (MTG_START_LIFE) is still needed: the ANALYZER paths -- exhaustive keep
// generation, the keep-model trainer, equivalence discovery -- call SetupGame outside any
// batch job, and a 30-life keep table has to be generated under 30-life rollouts or it is
// fitted to the wrong race.
//
// Resolution order is the house pattern: per-job override -> env static -> default 20.
// UNSET everywhere means 20, so every pre-existing manifest and every analyzer run is
// byte-identical; the override is opt-in and an absent field never touches it.
namespace gamesetup
{
struct Setup
{
    int       starting_life       = -1;   // -1 unset | >0 explicit   (manifest "starting_life")
    // 2HG OPPONENT HEADS (manifest "opponent_heads"): how many opposing PLAYERS share the
    // players[1] life pool. 1 = normal play (default, byte-identical). 2 = Two-Headed Giant:
    // the opposing TEAM is two targetable players with ONE shared life total, which is exactly
    // what players[1] already models -- damage to either head is deducted from the same pool.
    // What a second head changes is TARGETING and "each opponent" arithmetic, not combat:
    //   * a multi-target damage spell (Crackle with Power's "each of up to X targets",
    //     Soulfire Eruption's "any number of target ... players") can point at BOTH faces,
    //     so its total face output doubles (and Hinata counts one more discount target);
    //   * "each opponent" effects (Fanatic of Mogis, Goblin Chainwhirler, Shivan Gorge,
    //     Aria of Flame / Grove of the Burnwillows lifegain, Deathrite's drain, Adeline's
    //     per-opponent token) fire once per head;
    //   * DIVIDED damage (Magma Opus, Fiery Justice) is invariant -- the total is fixed and
    //     both heads share one pool -- and combat is unchanged (starting_life covers 2HG's 30).
    // Set it TOGETHER with starting_life 30 for a real 2HG run; neither implies the other, so
    // every pre-existing 30-life manifest (e.g. the Mirrorwing tourney arms) is byte-identical.
    int       opponent_heads      = -1;   // -1 unset | >=1 explicit  (manifest "opponent_heads")
    // Shuffle-variance / clairvoyance-decoupling salts (see GameState::shuffle_salt[_search]).
    // -1 unset | >=0 explicit. Same argument as starting_life, and it is the one that matters
    // most here: a SALT ENSEMBLE is how this repo separates a real effect from draw-order luck
    // for any fetch/tutor-class lever, and as an env-only knob it forced one `mtg --batch` per
    // salt -- a per-arm/per-salt wave, which is exactly the pattern CLAUDE.md's pooling rule
    // forbids. As job fields every (arm, salt) cell of an ensemble shares ONE queue and one tail.
    long long shuffle_salt        = -1;   // manifest "shuffle_salt"
    long long shuffle_salt_search = -1;   // manifest "shuffle_salt_search"
};

inline thread_local Setup t_setup;

// The resolved starting life for the game about to be set up.
inline int StartingLife()
{
    static const int s_env = []
    {
        const char* e = std::getenv("MTG_START_LIFE");
        const int   v = (e && *e) ? std::atoi(e) : 0;
        return v > 0 ? v : 20;
    }();
    return t_setup.starting_life > 0 ? t_setup.starting_life : s_env;
}

// The resolved number of opponent heads (see Setup::opponent_heads). Per-job override ->
// MTG_OPPONENT_HEADS -> 1. Clamped to [1, 2]: the target model adds exactly one extra face
// (2HG's second head); "each opponent" scaling would generalise but nothing above 2 exists
// to validate against, so >2 is treated as 2 rather than silently inventing a format.
inline int OpponentHeads()
{
    static const int s_env = []
    {
        const char* e = std::getenv("MTG_OPPONENT_HEADS");
        const int   v = (e && *e) ? std::atoi(e) : 0;
        return v > 0 ? v : 1;
    }();
    const int v = t_setup.opponent_heads > 0 ? t_setup.opponent_heads : s_env;
    return v < 1 ? 1 : (v > 2 ? 2 : v);
}

// The resolved MID-GAME shuffle salt (the opening stays fixed; MTG_SHUFFLE_SALT_OPENING is
// env-only). Per-job override -> env -> 0 (the identity => byte-identical).
inline unsigned long long ShuffleSalt()
{
    static const unsigned long long s_env = []
    {
        const char* e = std::getenv("MTG_SHUFFLE_SALT");
        return (e && *e) ? std::strtoull(e, nullptr, 10) : 0ull;
    }();
    return t_setup.shuffle_salt >= 0
             ? static_cast<unsigned long long>(t_setup.shuffle_salt) : s_env;
}

// The resolved salt the SEARCH/rollout evaluation uses. DEFAULTS TO ShuffleSalt() when neither
// the job nor the environment sets it -- equal salts mean lockstep/byte-identical play; making
// them DIFFER is the decoupling instrument (the search plans against a reshuffle the executor
// will not deal).
inline unsigned long long SearchShuffleSalt()
{
    static const bool s_env_set = []
    {
        const char* e = std::getenv("MTG_SHUFFLE_SALT_SEARCH");
        return e != nullptr && *e != '\0';
    }();
    static const unsigned long long s_env = []
    {
        const char* e = std::getenv("MTG_SHUFFLE_SALT_SEARCH");
        return (e && *e) ? std::strtoull(e, nullptr, 10) : 0ull;
    }();
    if (t_setup.shuffle_salt_search >= 0)
    { return static_cast<unsigned long long>(t_setup.shuffle_salt_search); }
    return s_env_set ? s_env : ShuffleSalt();
}
}   // namespace gamesetup
