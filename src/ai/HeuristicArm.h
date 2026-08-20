#pragma once
#include <array>
#include <cstdint>
#include <cstring>

// PER-JOB BOOLEAN HEURISTIC LEVERS, so ONE pooled batch can run every arm of a lever sweep.
//
// This is ValueArm.h's argument applied to the ordinary A/B levers. A lever like MTG_KE_ORDER is
// read once into a function-local `static const bool`, so a process can only ever BE one arm --
// which forces a sweep to spawn one `mtg --batch` per arm. That is precisely the pattern CLAUDE.md
// forbids: separate pools never share threads, so the cheap arms cannot backfill cores while the
// expensive arm drains its tail, and every arm boundary is a full synchronisation point gated by
// the single slowest game in the arm. It has starved this box twice (3 of 24 cores for 15 h; then
// 3 of 20 cores for 23 h).
//
// Moving the lever from the environment onto the JOB fixes it the same way the value arm was fixed:
// the batch worker installs the job's overrides before building its engine, and each lever's reader
// consults the override before falling back to its env static. UNSET everywhere means "use the env
// default", so single runs, the regression harness, and every pre-existing manifest are
// byte-identical -- the override is opt-in per job and a manifest without a "flags" block never
// touches it.
//
// thread_local rather than a global for ValueArm.h's reason: a batch worker owns its thread for the
// duration of a game and the search does not spawn threads (parallelism is at the game level), so
// per-thread is per-game.
//
// Slots are a dense enum rather than a name->value map because these are read on hot paths (the
// cast-order rank runs per candidate; the park probe per emission). A read is one array load and a
// branch. Manifest parsing resolves the env-var NAME to a slot exactly once, at parse time.
namespace heurarm
{
enum Slot : int
{
    KE_ORDER = 0,             // MTG_KE_ORDER               KittyEquipment cast order
    KE_PARK,                  // MTG_KE_PARK                Kemba park/un-park loop
    EQUIP_MINPOWER_LAST,      // MTG_EQUIP_MINPOWER_LAST    O-Naginata orders last-but-one
    NO_SEARCH_SECOND_MAIN,    // MTG_NO_SEARCH_SECOND_MAIN  force greedy m2 (per-deck opt-in killer)
    EQUIP_PAY_GUARD,          // MTG_EQUIP_PAY_GUARD       don't pay an equip ApplyEquip will refuse
    EQUIP_LOG_TRUTH,          // MTG_EQUIP_LOG_TRUTH       log an equip only if it actually attached
    METALCRAFT_CREDIT,        // MTG_METALCRAFT_CREDIT     same-turn metalcraft equip-{0} credit
    KE_GROUP_CAP,             // MTG_KE_GROUP_CAP          EquipmentProvider enumeration breadth 12 -> 4
    BIG_SOLVE_MEMO,           // MTG_BIG_SOLVE_MEMO        solve-memo entry cap 16k -> 256k
    EQUIP_DRAW_BP,            // MTG_EQUIP_DRAW_BP         equipment-ETB draw = breakpoint site 6
    EQUIP_DRAW_BP_DEFER,      // MTG_EQUIP_DRAW_BP_DEFER   ...and keep it out of wave 0 (cost only)
    COUNT
};

// Env-var name per slot, in slot order. The manifest names levers by their env var so a job block
// reads the same as the command line an interactive run would use.
inline const char* Name(int slot)
{
    static const char* const kNames[COUNT] = {
        "MTG_KE_ORDER",
        "MTG_KE_PARK",
        "MTG_EQUIP_MINPOWER_LAST",
        "MTG_NO_SEARCH_SECOND_MAIN",
        "MTG_EQUIP_PAY_GUARD",
        "MTG_EQUIP_LOG_TRUTH",
        "MTG_METALCRAFT_CREDIT",
        "MTG_KE_GROUP_CAP",
        "MTG_BIG_SOLVE_MEMO",
        "MTG_EQUIP_DRAW_BP",
        "MTG_EQUIP_DRAW_BP_DEFER",
    };
    return (slot >= 0 && slot < COUNT) ? kNames[slot] : nullptr;
}

// -1 unset (use the env default) | 0 force off | 1 force on.
using Arm = std::array<std::int8_t, COUNT>;

inline Arm Unset() { Arm a; a.fill(-1); return a; }

inline thread_local Arm t_arm = Unset();

// Reset to "use the env default for everything". Called by a worker before a job with no flags
// block, so a previous job's arm cannot leak into it through the reused thread.
inline void Clear() { t_arm = Unset(); }

// The one read. `env_default` is the lever's process-wide EnvOn() value, captured by the caller's
// own static, so a non-batch run keeps exactly its old behaviour and cost.
inline bool Flag(Slot s, bool env_default)
{
    const std::int8_t o = t_arm[static_cast<int>(s)];
    return o < 0 ? env_default : (o != 0);
}

// Name -> slot for manifest parsing. Returns COUNT if the name is not an overridable lever, which
// the parser must treat as an ERROR: a silently-ignored flag reads as "arm measured" while actually
// running the baseline, which is the failure mode that corrupts an A/B.
inline int SlotOf(const char* name)
{
    for (int i = 0; i < COUNT; ++i)
    {
        if (std::strcmp(Name(i), name) == 0) { return i; }
    }
    return COUNT;
}
}
