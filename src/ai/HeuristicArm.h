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
    EQUIP_DRAW_BP_INLINE,     // MTG_EQUIP_DRAW_BP_INLINE  ...inline AT the draw + truncate the plan
    BP_CLASSIFY,              // MTG_BP_CLASSIFY           condemn already-considered casts at a bp
    KE_TUTOR_ALL,             // MTG_KE_TUTOR_ALL          score EVERY Equipment on the tutor axis
    KE_TUTOR_RANK,            // MTG_KE_TUTOR_RANK         reasoned fetch ranking + width 2
    KE_TUTOR_ONE,             // MTG_KE_TUTOR_ONE          ...and width 1 (heuristic only)
    SHED_WORST,               // MTG_SHED_WORST            rollout cleanup sheds the WORST-ranked card
    EQUIP_COPY_COLLAPSE,      // MTG_EQUIP_COPY_COLLAPSE   one odometer position per fungible-copy class
    EQUIP_UNSICK_HOST,        // MTG_EQUIP_UNSICK_HOST     no-Kemba/no-ds host must be able to swing now
    KE_BUCKET_DISCARD,        // MTG_KE_BUCKET_DISCARD     bucketed cleanup discard (creatures/mana/equipment)
    EQUIP_PIECE_DEPS,         // MTG_EQUIP_PIECE_DEPS     reject a stranded equip at the odometer digit
    KE_DISCARD_RESIDUAL,      // MTG_KE_DISCARD_RESIDUAL  discard equipment bucket is the 7-card residual
    AL_SSM,                   // MTG_AL_SSM                Anti-Lifegain searched interior second main
    SSM_BRANCH_ONLY,          // MTG_SSM_BRANCH_ONLY       searched interior m2 at the BRANCH site only
    LEAF_GRADE_NOWIN,         // MTG_LEAF_GRADE_NOWIN      grade a no-win leaf instead of a flat max_turns+1
    LEAF_VALUE_RES,           // MTG_LEAF_VALUE_RES        keep the value leaf's milliturn resolution
    LEAF_TB_BOARD,            // MTG_LEAF_TB_BOARD         ...and grade on board development, not life alone
    LEAF_TB_PERMS,            // MTG_LEAF_TB_PERMS         ...on a PERMANENT COUNT under the life term
    LEAF_TB_NONLAND,          // MTG_LEAF_TB_NONLAND       ...on NON-LAND permanents only
    LEAF_NOWIN_FORCE,         // MTG_LEAF_NOWIN_FORCE      force the tie-break past a provider opt-out
    AL_SSM_ROLLOUT,           // MTG_AL_SSM_ROLLOUT        AL also searches the ROLLOUT's per-turn m2
    M2_CAP1,                  // MTG_M2_CAP1               cap the interior m2 solve to depth 1
    STOMPY_ORDER,             // MTG_STOMPY_ORDER          USER-reviewed StompySurprise cast order
    SCALED_LAND_RANK,         // MTG_SCALED_LAND_RANK      reserve a live board-scaled LAND (Three Tree City)
    TOP_RESOLVE,              // MTG_TOP_RESOLVE           tutor-to-top reset (the order's LOOP half)
    STOMPY_WT_LITERAL,        // MTG_STOMPY_WT_LITERAL     tutor census = the USER's two consumers only
    STOMPY_TT_LITERAL,        // MTG_STOMPY_TT_LITERAL     Turntimber always [7]=12, no post-tutor 15
    KE_CONDEMN,               // MTG_KE_CONDEMN            re-enable Kitty's breakpoint condemnation
    BP_CONDEMN_TAIL,          // MTG_BP_CONDEMN_TAIL_EXEMPT  don't condemn when the plan has no cast left
    BP_CONDEMN_ORDER,         // MTG_BP_CONDEMN_ORDER_AWARE  don't condemn a slot AFTER the bp site
    SF_PUT_BP,                // MTG_SF_PUT_BP             site 6 fires off a Stoneforge PUT too
    M2_D0_SEARCHED,           // MTG_M2_D0_SEARCHED        branch-site interior m2 at d<=0 is SEARCHED
    KE_ORDER_FULL,            // MTG_KE_ORDER_FULL         KittyEquipment FULL (total) cast order
    BP_NO_GREEDY_CONT,        // MTG_BP_NO_GREEDY_CONT     canonical continuation instead of greedy Solve
    HINATA_ORDER_FULL,        // MTG_HINATA_ORDER_FULL     Hinata FULL cast order (Ponder/Preordain PEERS)
    HINATA_PP_STRICT,         // MTG_HINATA_PP_STRICT      ...and split the peers: Ponder before Preordain
    HINATA_IREN_EARLY,        // MTG_HINATA_IREN_EARLY     LOO: Irencrag back to 18 (any payoff may follow)
    HINATA_FIND_LATE,         // MTG_HINATA_FIND_LATE      LOO: tutor/cantrips/dig back to 20 (after her)
    HINATA_PAY_TIE,           // MTG_HINATA_PAY_TIE        LOO: the three payoffs tied at 20 again
    HINATA_GAMBLE_LATE,       // MTG_HINATA_GAMBLE_LATE    tutor AFTER the cantrips (cantrip, then fetch)
    HINATA_MANA_FLOAT_RANK,   // MTG_HINATA_MANA_FLOAT_RANK  cantrip rank counts MANA + floating
    HINATA_DORK_TIE,          // MTG_HINATA_DORK_TIE       LOO: dork + engine tied at 10, as generic has them
    BP_CONDEMN_MANA_SITE,     // MTG_BP_CONDEMN_MANA_SITE_EXEMPT  a MANA-ADDING site condemns nothing
    BP_CONDEMN_TREASURE_POS,  // MTG_BP_CONDEMN_TREASURE_SITE_POSITIVE  ...only when it NET-added mana
    MW_GR_LADDER_POS,         // MTG_MW_GR_LADDER_POSITIVE  Gold Rush walks earlier only when positive
    BP_CONDEMN_LAND,          // MTG_BP_CONDEMN_LAND       the LAND DROP is a slot: passing it condemns held lands
    ROLLOUT_LAND_RANKER,      // MTG_ROLLOUT_LAND_RANKER   rollout drop uses the EXECUTOR's ranker
    BP_CONDEMN_LAND_SETTLED,  // MTG_BP_CONDEMN_LAND_SETTLED   no condemning while the drop is pending
    BP_CONDEMN_SEARCHED_ONLY, // MTG_BP_CONDEMN_SEARCHED_ONLY  condemn only in the DECISION SPACE
    BP_SITE3,                 // MTG_BP_SITE3              make the PLAIN-CANTRIP continuation searchable
    BP_SITE3_DEFER,           // MTG_BP_SITE3_DEFER        ...and keep it OUT OF WAVE 0 (cost only)
    BP_PARTITION_CANTRIP,     // MTG_BP_PARTITION_CANTRIP  THE PARTITION SHAPE for plain cantrips
    HINATA_ALL_MAIN2,         // MTG_HINATA_ALL_MAIN2      every cast is a SECOND-MAIN cast
    MAIN2_DROP,               // MTG_MAIN2_DROP            the land drop is offered POST-combat too
    BP_NODE,                  // MTG_BP_NODE               plain-cantrip breakpoint = REAL SEARCH NODE
    HINATA_RANGE,             // MTG_HINATA_RANGE          condemnation RANGES: engine/find tiers' latest
    CANTRIP_ORDER,            // MTG_CANTRIP_ORDER         canonical cantrip order bans permutation chains
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
        "MTG_EQUIP_DRAW_BP_INLINE",
        "MTG_BP_CLASSIFY",
        "MTG_KE_TUTOR_ALL",
        "MTG_KE_TUTOR_RANK",
        "MTG_KE_TUTOR_ONE",
        "MTG_SHED_WORST",
        "MTG_EQUIP_COPY_COLLAPSE",
        "MTG_EQUIP_UNSICK_HOST",
        "MTG_KE_BUCKET_DISCARD",
        "MTG_EQUIP_PIECE_DEPS",
        "MTG_KE_DISCARD_RESIDUAL",
        "MTG_AL_SSM",
        "MTG_SSM_BRANCH_ONLY",
        "MTG_LEAF_GRADE_NOWIN",
        "MTG_LEAF_VALUE_RES",
        "MTG_LEAF_TB_BOARD",
        "MTG_LEAF_TB_PERMS",
        "MTG_LEAF_TB_NONLAND",
        "MTG_LEAF_NOWIN_FORCE",
        "MTG_AL_SSM_ROLLOUT",
        "MTG_M2_CAP1",
        "MTG_STOMPY_ORDER",
        "MTG_SCALED_LAND_RANK",
        "MTG_TOP_RESOLVE",
        "MTG_STOMPY_WT_LITERAL",
        "MTG_STOMPY_TT_LITERAL",
        "MTG_KE_CONDEMN",
        "MTG_BP_CONDEMN_TAIL_EXEMPT",
        "MTG_BP_CONDEMN_ORDER_AWARE",
        "MTG_SF_PUT_BP",
        "MTG_M2_D0_SEARCHED",
        "MTG_KE_ORDER_FULL",
        "MTG_BP_NO_GREEDY_CONT",
        "MTG_HINATA_ORDER_FULL",
        "MTG_HINATA_PP_STRICT",
        "MTG_HINATA_IREN_EARLY",
        "MTG_HINATA_FIND_LATE",
        "MTG_HINATA_PAY_TIE",
        "MTG_HINATA_GAMBLE_LATE",
        "MTG_HINATA_MANA_FLOAT_RANK",
        "MTG_HINATA_DORK_TIE",
        "MTG_BP_CONDEMN_MANA_SITE_EXEMPT",
        "MTG_BP_CONDEMN_TREASURE_SITE_POSITIVE",
        "MTG_MW_GR_LADDER_POSITIVE",
        "MTG_BP_CONDEMN_LAND",
        "MTG_ROLLOUT_LAND_RANKER",
        "MTG_BP_CONDEMN_LAND_SETTLED",
        "MTG_BP_CONDEMN_SEARCHED_ONLY",
        "MTG_BP_SITE3",
        "MTG_BP_SITE3_DEFER",
        "MTG_BP_PARTITION_CANTRIP",
        "MTG_HINATA_ALL_MAIN2",
        "MTG_MAIN2_DROP",
        "MTG_BP_NODE",
        "MTG_HINATA_RANGE",
        "MTG_CANTRIP_ORDER",
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
