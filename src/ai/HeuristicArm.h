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
    LEAF_GRADE_NOWIN,         // MTG_LEAF_GRADE_NOWIN      grade a no-win leaf instead of a flat max_turns+1
    LEAF_VALUE_RES,           // MTG_LEAF_VALUE_RES        keep the value leaf's milliturn resolution
    LEAF_TB_BOARD,            // MTG_LEAF_TB_BOARD         ...and grade on board development, not life alone
    LEAF_TB_PERMS,            // MTG_LEAF_TB_PERMS         ...on a PERMANENT COUNT under the life term
    LEAF_TB_NONLAND,          // MTG_LEAF_TB_NONLAND       ...on NON-LAND permanents only
    LEAF_NOWIN_FORCE,         // MTG_LEAF_NOWIN_FORCE      force the tie-break past a provider opt-out
    AL_SSM_ROLLOUT,           // MTG_AL_SSM_ROLLOUT        AL also searches the ROLLOUT's per-turn m2
    M2_CAP1,                  // MTG_M2_CAP1               cap the interior m2 solve to depth 1
    M2_WAVES,                 // MTG_M2_WAVES              FSLineTail m2 loop runs the deferred wave phase
    M2_AXES,                  // MTG_M2_AXES               m2 enumeration hosts append the sub-decision axes
    M2_BPVARS,                // MTG_M2_BPVARS             m2 memoized host appends bp_choice variants (wave 0)
    M2_FIXPOINT,              // MTG_M2_FIXPOINT           re-solve m2 after a plan that fired a draw breakpoint
    M2_KEY_COARSE,            // MTG_M2_KEY_COARSE         interior-m2 solve memo keys on the m2 dependency set
    M2_FIX_UNFILTERED,        // MTG_M2_FIX_UNFILTERED     fixpoint kill-scan probes ALL plans, not just projected-lethal
    M2_FIX_RESOLVE,           // MTG_M2_FIXPOINT=2         fixpoint mode 2: gated full re-solve on actionable draws
    STOMPY_ORDER,             // MTG_STOMPY_ORDER          USER-reviewed StompySurprise cast order
    SCALED_LAND_RANK,         // MTG_SCALED_LAND_RANK      reserve a live board-scaled LAND (Three Tree City)
    TOP_RESOLVE,              // MTG_TOP_RESOLVE           tutor-to-top reset (the order's LOOP half)
    STOMPY_WT_LITERAL,        // MTG_STOMPY_WT_LITERAL     tutor census = the USER's two consumers only
    STOMPY_TT_LITERAL,        // MTG_STOMPY_TT_LITERAL     Turntimber always [7]=12, no post-tutor 15
    KE_CONDEMN,               // MTG_KE_CONDEMN            re-enable Kitty's breakpoint condemnation
    BP_CONDEMN_TAIL,          // MTG_BP_CONDEMN_TAIL_EXEMPT  don't condemn when the plan has no cast left
    BP_CONDEMN_ORDER,         // MTG_BP_CONDEMN_ORDER_AWARE  don't condemn a slot AFTER the bp site
    SF_PUT_BP,                // MTG_SF_PUT_BP             site 6 fires off a Stoneforge PUT too
    KE_ORDER_FULL,            // MTG_KE_ORDER_FULL         KittyEquipment FULL (total) cast order
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
    BP_CONDEMN_ALLPATHS,      // MTG_BP_CONDEMN_ALLPATHS   condemn only where EVERY line reaching the state does
    BP_SITE3,                 // MTG_BP_SITE3              make the PLAIN-CANTRIP continuation searchable
    BP_SITE3_DEFER,           // MTG_BP_SITE3_DEFER        ...and keep it OUT OF WAVE 0 (cost only)
    BP_PARTITION_CANTRIP,     // MTG_BP_PARTITION_CANTRIP  THE PARTITION SHAPE for plain cantrips
    HINATA_ALL_MAIN2,         // MTG_HINATA_ALL_MAIN2      every cast is a SECOND-MAIN cast
    MAIN2_DROP,               // MTG_MAIN2_DROP            the land drop is offered POST-combat too
    BP_NODE,                  // MTG_BP_NODE               plain-cantrip breakpoint = REAL SEARCH NODE
    HINATA_RANGE,             // MTG_HINATA_RANGE          condemnation RANGES: engine/find tiers' latest
    CANTRIP_ORDER,            // MTG_CANTRIP_ORDER         canonical cantrip order bans permutation chains
    HINATA_SUBSET_CREDIT,     // MTG_HINATA_SUBSET_CREDIT  same-subset Hinata discount credit in enumeration
    EXEC_FEAS,                // MTG_EXEC_FEAS             executor-validated sequential subset payability
    NONCLEANUP_SHED_WORST,    // MTG_NONCLEANUP_SHED_WORST  cost/trigger discard sheds the WORST-ranked card
    MINOTAUR_DISCARD_V2,      // MTG_MINOTAUR_DISCARD_V2   both halves below (convenience arm)
    MINOTAUR_DISCARD_VIAL,    // MTG_MINOTAUR_DISCARD_VIAL  ...half 1: Vial sheds with the mana
    MINOTAUR_DISCARD_PLAY,    // MTG_MINOTAUR_DISCARD_PLAY  ...half 2: threats by graded playability
    MINOTAUR_DISCARD_PLAY2,   // MTG_MINOTAUR_DISCARD_PLAY2 ...half 2, gentler: slack 2 not 1
    SPASM_UNTAP_LITERAL,      // MTG_SPASM_UNTAP_LITERAL   untap ritual resolves as a LITERAL untap
    MINOTAUR_DISCARD_REDUCER, // MTG_MINOTAUR_DISCARD_REDUCER a HELD, deployable reducer discounts too
    MINOTAUR_DISCARD_EV,      // MTG_MINOTAUR_DISCARD_EV      EV = P(play) x value(play), soft decay
    MINOTAUR_DISCARD_EVHARD,  // MTG_MINOTAUR_DISCARD_EVHARD  ...steeper decay
    MINOTAUR_DISCARD_DUPES,   // MTG_MINOTAUR_DISCARD_DUPES   k-th copy pays CUMULATIVE mana
    MINOTAUR_DISCARD_EV2,     // MTG_MINOTAUR_DISCARD_EV2     value = the FULL V1 order, not 6 buckets
    MINOTAUR_EV_DISCARD,      // MTG_MINOTAUR_EV_DISCARD      THE ADOPTED MODEL (default ON)
    MINOTAUR_DISCARD_CSVAL,   // MTG_MINOTAUR_DISCARD_CSVAL   value order = LEARNED card_scores
    MINOTAUR_DISCARD_CSNOP,   // MTG_MINOTAUR_DISCARD_CSNOP   ...and drop P (card_scores AS the EV)
    MINOTAUR_DISCARD_CSDUP,   // MTG_MINOTAUR_DISCARD_CSDUP   learned marginal gates the dupe penalty

    IRENCRAG_WASTE,           // MTG_IRENCRAG_WASTE        no cast restrictor with nothing after it
    IRENCRAG_PAYOFF,          // MTG_IRENCRAG_PAYOFF       ...and the follower must be a PAYOFF
    IRENCRAG_FINISHER,        // MTG_IRENCRAG_FINISHER     ...or the FINISHER (Crackle) specifically
    IRENCRAG_NEEDS,           // MTG_IRENCRAG_NEEDS        ...a payoff that could NOT be cast without him
    FILTER_FEED_STRICT,       // MTG_FILTER_FEED_STRICT    backtracker filter feed pays the real hybrid cost
    BP_PREFIX_PREPAY,         // MTG_BP_PREFIX_PREPAY      node-host prepay covers only the PRE-breakpoint casts
    FEED_FILTER_FIRST,        // MTG_FEED_FILTER_FIRST     route a filter's last feeder THROUGH the filter
    EDF_SEQ_ETB,              // MTG_EDF_SEQ_ETB           an ETB-untap chain reaches the SEQUENCED payability walk
    BP_NODE_D0ONLY,           // MTG_BP_NODE_D0ONLY        host the breakpoint node only at zero REMAINING depth
    BP_NODE_ROOTTURN,         // MTG_BP_NODE_ROOTTURN      host the breakpoint node only on the ROOT TURN
    BP_NODE_D56,              // MTG_BP_NODE_D56           the node hosts deferred sites 5 and 6 too, not just 3
    BP_NODE_KEEPWAVE,         // MTG_BP_NODE_KEEPWAVE      ...and the RANK machinery keeps covering 5/6 anyway
    BP_NODE_KEEPWAVE3,        // MTG_BP_NODE_KEEPWAVE3     ...the same for SITE 3, the shipped candidate's own site
    HEROISM_MAGNET_TRAIT,     // MTG_HEROISM_MAGNET_TRAIT  a live copy-token enchantment counts as a magnet
    HEROISM_FRESH_HOLD,       // MTG_HEROISM_FRESH_HOLD    ...and unlocks same-turn spend of fresh Treasures
    ETB_TAP_YIELD,            // MTG_ETB_TAP_YIELD         ETB-untap tap-ahead takes the HIGHEST-yield lands
    CONDEMN_M1_BP,            // MTG_CONDEMN_M1_BP         m1 condemnation filter at breakpoint continuations
    SUBSET_ROCK_COLOR,        // MTG_SUBSET_ROCK_COLOR     colour-presence gate credits same-subset rock colours (default ON since 2026-09-22)
    EDF_PROSPECTIVE,          // MTG_EDF_PROSPECTIVE      go-off recognizer sees a loop the PLAN assembles
    REFLOAT_WILD_C,           // MTG_REFLOAT_WILD_C       a rainbow source's float is {C}-capable (wild_c)
    REFLOAT_NEED,             // MTG_REFLOAT_NEED         tap-ahead colour commit: {C}+ability demand, coverage
    REFLOAT_COMBO,            // MTG_REFLOAT_COMBO        in-loop mana policy: {C}/B/R quota ladder
    EDF_DRAWLAND_GOFF,        // MTG_EDF_DRAWLAND_GOFF   a {T} DRAW LAND is a go-off route too
    EDF_LOOP_DRAW,            // MTG_EDF_LOOP_DRAW       ...and the loop ACTIVATES it every iteration
    EDF_COMBO_FINISH,         // MTG_EDF_COMBO_FINISH    bank the mana, then deploy the finisher from hand
    EDF_SEQ_AURA,             // MTG_EDF_SEQ_AURA        a LAND AURA reaches the SEQUENCED payability walk
    EDF_C_CONSERVE,           // MTG_EDF_C_CONSERVE      {C}-capable sources tap LAST once a {C}-pip ability is out
    EDF_WISH_SINK_FLOOR,      // MTG_EDF_WISH_SINK_FLOOR  the hand's LAST wish keeps the sink tier live
    EDF_WISH_SINK_SCARCE,     // MTG_EDF_WISH_SINK_SCARCE ...only when the library holds <=1 more wish
    LAND_IDLE_TAPPED_FIRST,   // MTG_LAND_IDLE_TAPPED_FIRST  first drop plays TAPPED when the mana is idle
    NO_REDUNDANT_REDUCER,     // MTG_NO_REDUNDANT_REDUCER  never cast a SATURATED cost-reducer copy
    BP_UNIFORM_DEV,           // MTG_BP_UNIFORM_DEV       take candidate k at EVERY breakpoint (Plan::bp_all)
    FLUCT_HOLD_FUEL,          // MTG_FLUCT_HOLD_FUEL      going off -> spend no fuel, just cycle (USER rule)
    DIG_HOLD_FUEL,            // MTG_DIG_HOLD_FUEL        hold-fuel's DIG-SITE half: re-solve skips fuel casts
    FLUCT_REBUY,              // MTG_FLUCT_REBUY          cycle-Stinger -> Unearth this-turn deployment line
    GREEDY_HOLD_LAND,         // MTG_GREEDY_HOLD_LAND     hold-fuel's LAND-DROP site (d0 greedy): skip the drop, cycle the land
    EDF_C_BUDGET,             // MTG_EDF_C_BUDGET         go-off projection budgets {C} pips, not just mana value
    EDF_WISH_CAST_GATE,       // MTG_EDF_WISH_CAST_GATE   hand coverage in the wish ranking needs a CASTABLE card
    EDF_TUTOR_NARROW,         // MTG_EDF_TUTOR_NARROW     wish width 8 -> 3: the RANKING decides, not the eval
    EDF_SINK_KMAX,            // MTG_EDF_SINK_KMAX        sink K axis {1..3} -> {kmax}: apply-loop realises what's payable
    EDF_BLINK_KMAX,           // MTG_EDF_BLINK_KMAX       blink K axis {1..3} -> {kmax}: same shape, same apply-loop degrade
    EDF_M2,                   // MTG_EDF_M2               flicker-combo decks (blink outlet + ETB-untap payload) get the searched second main
    EDF_AUTOGOFF,             // MTG_EDF_AUTOGOFF         same-main go-off: plan apply runs the assembled loop (USER: no extra mains to search)
    EDF_LIB_ROUTE,            // MTG_EDF_LIB_ROUTE        library-route finisher pricing (default OFF: mana-only sizing measured -0.14/-0.20)
    EDF_VAL_RAMP,             // MTG_EDF_VAL_RAMP        plan-value: a land Aura is priced by the MANA it adds (default OFF, rejected: loses s6)
    EDF_VAL_COMBO,            // MTG_EDF_VAL_COMBO       plan-value: a creature is priced by its COMBO role, not a combat clock (default ON since 2026-09-15)
    EDF_GOFF_EXACT_AUTO,      // MTG_EDF_GOFF_EXACT_AUTO the AUTONOMOUS arm gets FlickerGoOffCount's three arithmetic corrections
    EDF_CO_ROOT,              // MTG_EDF_CO_ROOT         the ROOT decision queries ComboOffPossible + VERIFIES -> skip the rollouts
    EDF_CO_LOOK,              // MTG_EDF_CO_LOOK         a LOOKAHEAD ply queries the same rule, no trial (~1.5us vs 0.3-0.8ms)
    EDF_EXACT_EXECUTOR,       // MTG_EDF_EXACT_EXECUTOR  the AUTONOMOUS go-off apply gets the button's finish machinery
    EDF_HAND_GOFF,            // MTG_EDF_HAND_GOFF       a go-off whose outlet/payload is CAST THIS PLAN is one enumerated plan
    EDF_PAYLOAD_FIRST,        // MTG_EDF_PAYLOAD_FIRST   cast order: the ETB-untap payload BEFORE the activation reducer
    EDF_AURA_HOST_SIG,        // MTG_EDF_AURA_HOST_SIG   autonomous plan dedup keeps land-Aura HOST variants distinct
    BOUNCE_SPARE_AURA,        // MTG_BOUNCE_SPARE_AURA   a karoo bounce ranks a land carrying an Aura last
    HOLD_C_FOR_LINE,          // MTG_HOLD_C_FOR_LINE     a blink activation's {C} pip joins the line hold -> float keeps {C} (default ON since 2026-09-15)
    LINE_C_HOLD,              // MTG_LINE_C_HOLD         the casts' payer holds the {C} SOURCES the plan's own activation still needs (default OFF)
    EDF_HAND_GOFF_REFUND,     // MTG_EDF_HAND_GOFF_REFUND the hand go-off's mana floor credits a hand PAYLOAD's own ETB untap (default OFF)
    EDF_AURA_HOST_SIG_KAROO,  // MTG_EDF_AURA_HOST_SIG_KAROO the host signature applies only to plans whose land drop is a karoo (perf)
    EDF_DRAW_SINK_HONEST,     // MTG_EDF_DRAW_SINK_HONEST the loop's draw-sink guard prices the fused Clue crack + the source's own lost yield (default ON)
    SNOW_CAST_ORDER,          // MTG_SNOW_CAST_ORDER     USER-reviewed Snow cast order: land -> cheapest..dearest -> draw
    // ...and its three independent halves, so the gain can be ATTRIBUTED instead of guessed. All
    // three arrived in one revision and the revision moved the result; which of them did it is a
    // separate question, and this file's own history says a multi-site flag hands you a plausible
    // cause for free and has been wrong about it twice. Each defaults ON with the order.
    SNOW_ORDER_FIXER,         // MTG_SNOW_ORDER_FIXER    the fixer (Astrolabe) sits right AFTER the land drop
    SNOW_ORDER_SPLIT,         // MTG_SNOW_ORDER_SPLIT    split every within-cost tie (mana -> permanent -> non-perm)
    SNOW_ACT_ORDER,           // MTG_SNOW_ACT_ORDER      Scrying Sheets activates before Frost Augur
    // Breakpoint-condemnation SOUNDNESS guard: the snapshot binds only on the turn it was taken.
    // Overridable per job so "what does the hole cost?" is measurable without a rebuild; `false` is
    // the BROKEN arm, kept to reproduce the finding, not a neutral one.
    BP_CONDEMN_SAME_TURN,     // MTG_BP_CONDEMN_SAME_TURN  a breakpoint snapshot describes ONE turn
    SNOW_CONDEMN,             // MTG_SNOW_CONDEMN        Snow opts into breakpoint condemnation
    BP_EMPTY_ARM,             // MTG_BP_EMPTY_ARM        "done with this phase" as a scored arm
    BP_CONDEMN_NEW_OPTION,    // MTG_BP_CONDEMN_NEW_OPTION  spare when the site drew a payable card
    BP_CONDEMN_PLAN_CAST,     // MTG_BP_CONDEMN_PLAN_CAST  a plan that cast NOTHING declined nothing
    BP_ABILITY_DELTA,         // MTG_BP_ABILITY_DELTA    site 9 opens on a NEWLY activatable ability
    EDF_PAIN_LOOP_AUTO,       // MTG_PAINLAND_TAPAHEAD_LOOP_AUTO the AUTONOMOUS loop's tap-ahead banks a painland's painless {C} too (default ON since 2026-09-16)
    EDF_DEPLOY_NO_COUNTER,    // MTG_COMBO_OFF_DEPLOY_NO_COUNTER a mid-loop deploy never pays Emiel's {G/W} counter trigger (default ON since 2026-09-16)
    EDF_DIG_BANK_FIRST,       // MTG_COMBO_OFF_DIG_BANK_FIRST   ONE cheapest draw land is promoted, and only once the float already pays it (default ON since 2026-09-16)
    EDF_CYCLE_SET,            // MTG_TAPAHEAD_CYCLE_SET         the autonomous loop's tap-ahead budget is over the top-`untaps` yield lands, not every tapped land (built 2026-09-16, default OFF: deck average +0.03 / 0 better 3 worse)
    FS_IDLE_NODE,             // MTG_FS_IDLE_NODE               a full-search node with NOTHING to do searches the idle continuation instead of answering "no win" (default ON since 2026-09-16)
    EDF_COMBO_ROUTE,          // MTG_EDF_COMBO_ROUTE            the mechanical COMBO OFF route as one searched plan action (built 2026-09-16)
    BP_CANDS_ORDER,           // MTG_BP_CANDS_ORDER        value-order the breakpoint continuation list (MoveOrderPlans)
    BP_W4,                    // MTG_BP_W4                 wave-0 width 4 (per-job twin of MTG_BP_SEARCH=4)
    BP_NODE_HOST2,            // MTG_BP_NODE_HOST2         ROOTTURN hosting also hosts the node on root+1
    BP_NESTED_CANON,          // MTG_BP_NESTED_CANON       an un-branched NESTED slot defaults to the value-best entry, not EMPTY
    BP_VARIANT_FIRST,         // MTG_BP_VARIANT_FIRST      schedule a base plan's breakpoint variants BEFORE the base (equal value)
    BP_NESTED_CANON_PLAYOUT,  // MTG_BP_NESTED_CANON_PLAYOUT  ...the nested default fires inside PLAYOUT applies too (uncharged wall)
    SAC_OUTLET_PAY,           // MTG_SAC_OUTLET_PAY        a sac-for-mana outlet's fodder is a LAST-RANKED payment source, not a searched action
    FOLD_COUNTER_SOURCES,     // MTG_FOLD_COUNTER_SOURCES  two same-named sources on the SAME counter count fold (the count joins the tag)
    FUNGUS_SAC_DRAW_CLOCK,    // MTG_FUNGUS_SAC_DRAW_CLOCK  sac a Saproling to DRAW only when it is not already on the clock
    BP_CONDEMN_NEWOPT_BYNAME, // MTG_BP_CONDEMN_NEWOPT_BYNAME  a new card only earns the exclusive-slot exemption if its NAME was not already passed on
    FORCE_USES_M2,            // MTG_FORCE_USES_M2         force the searched SECOND MAIN on regardless of the whitelist
    SNOW_ORDER_DRAW_EARLY,    // MTG_SNOW_ORDER_DRAW_EARLY     the DRAW BAND moves from LAST to just after the fixer
    SNOW_ORDER_TAPDRAW_EARLY, // MTG_SNOW_ORDER_TAPDRAW_EARLY  ...and the tap-draw PERMANENTS join that band
    FUNGUS_SPORE_POOL,        // MTG_FUNGUS_SPORE_POOL     spore sources are ONE pool: canonical (oldest) first, count is the only axis
    ETB_WATCHER_GATES,        // MTG_ETB_WATCHER_GATES     per-deck presence gates on the two ETB-cascade scans whose param test is INSIDE the walk (default ON)
    LAZY_LEAF,                // MTG_LAZY_LEAF             probe each pass LEAFLESS first; an in-window win needs no leaf at all (default OFF)
    BP_HAND_ENTRY,            // MTG_BP_HAND_ENTRY         a card entering hand OUTSIDE a cast apply arms site 10 too (default OFF)
    BP_WAVEDROP_HOSTED,       // MTG_BP_WAVEDROP_HOSTED    the node's wave stand-down applies only where the node really HOSTS (default OFF)
    BP_ACQ_CLAUSE,            // MTG_BP_ACQ_CLAUSE         site 3's ACQUISITION family (tutor-to-hand/-top, Soulfire, Garth) is fanned out (default ON)
    BP_DIG_AXIS_FANOUT,       // MTG_BP_DIG_AXIS_FANOUT    a searched-dig-axis plan (dig_choice==1) gets the site-4 fan-out (default ON)
    BP_NEW_ONLY,              // MTG_BP_NEW_ONLY           a breakpoint emits ONLY continuations that use a card that arrived there (default ON since 2026-09-22)
    PENDING_FILTER_SLOT,      // MTG_PENDING_FILTER_SLOT   a hand any-colour filter counts itself in the fed-slot quota (default ON since 2026-09-22)
    MINT_CREDIT_EXACT,        // MTG_MINT_CREDIT_EXACT     a minted Treasure is credited at the base at its EXACT width and opens no breakpoint (default ON since 2026-09-22)
    BP_MINT_SITE,             // MTG_BP_MINT_SITE          AUDIT hatch: a Treasure-only trick payload still opens the site-5 breakpoint under the exact credit (the lean-vs-audit detector arm)
    BP_REPLAY_COST,           // MTG_BP_REPLAY_COST        a recorded continuation cast carries the cost it paid, so the executor's replay traits match the rollout's (default ON since 2026-09-22)
    // Giants' two searched plan axes. Both shipped default-ON on the core invariant (the
    // alternatives are genuinely distinct, so a ranked pick would be a narrowing) but WITHOUT a
    // measurement, which the adoption bar wants. Per-job so the control and both off-arms pool
    // into ONE batch instead of one invocation per arm.
    FLING_AXIS,               // MTG_FLING_AXIS            Surtland Flinger's fling victim + the decline (default ON)
    TECTONIC_AXIS,            // MTG_TECTONIC_AXIS         Tectonic Giant's attack-trigger mode A vs B (default ON)
    TECTONIC_KEEP_AXIS,       // MTG_TECTONIC_KEEP_AXIS    ...and WHICH exiled card mode B stages (sub-decision of mode B)
    SAC_OUTLET_POOL,          // MTG_SAC_OUTLET_POOL      N interchangeable sac-for-mana outlets are ONE pool: the COUNT is the only axis (ADOPTED default ON)
    SAC_FODDER_RESERVE,       // MTG_SAC_FODDER_RESERVE   sac fodder is RESERVED per activation: count the bodies the plan can really have THIS TURN, skip the line if short (ADOPTED default ON)
    FUNGUS_DEVOUR_CANDS,      // MTG_FUNGUS_DEVOUR_CANDS  Mycoloth's devour k is a SHORT candidate list (fodder floor + the contested bodies), not 0..own (ADOPTED default ON)
    FUNGUS_M2_DEVOUR,         // MTG_FUNGUS_M2_DEVOUR     Mycoloth (and ONLY Mycoloth) is cast in the SECOND MAIN: attack first, then devour the bodies (ADOPTED default ON)
    FUNGUS_M2_GATE,           // MTG_FUNGUS_M2_GATE       ...and only SOLVE the second main on turns the hand actually holds a PAYABLE Main2 cast (ADOPTED default ON)
    FUNGUS_M2_ROOT,           // MTG_FUNGUS_M2_ROOT       ...and defer only at REAL decision turns: a projected future turn keeps Mycoloth in main 1 (measured NEUTRAL, default OFF)
    FUNGUS_DEVOUR_BIG_EXEMPT, // MTG_FUNGUS_DEVOUR_BIG_EXEMPT  ...and the ladder STOPS at the first big body (Sporesower/Sporecrown never eaten) instead of giving each a rung (default OFF; sub-mode of the above)
    RESCUE_TOTAL_GATE,        // MTG_RESCUE_TOTAL_GATE   skip the filter real-payment rescue when the flat failure is a TOTAL shortfall and every conversion source is total-preserving (default OFF)
    M2_EMPTY_FAST,            // MTG_M2_EMPTY_FAST        a PROVEN-EMPTY second main costs the recursion and nothing else: no state copy, no apply, no dedup key (ADOPTED default ON; =0 runs the do-nothing plan through the loop, which must be byte-identical)
    FOLD_SEARCH_ODO,          // MTG_FOLD_SEARCH_ODO      the SEARCH's private subset walk declares itself an odometer, so the canonical-prefix fold applies there too (default OFF)
    ACT_TAP_RESERVE,          // MTG_ACT_TAP_RESERVE      a planned `{cost},{T}` activation's SOURCE is held back from mana payment for the whole plan, so an earlier cost cannot strand it (default OFF)
    RESCUE_TAP_SOURCE,        // MTG_RESCUE_TAP_SOURCE    the filter real-payment rescue applies each selected activation's own {T} before paying, so no source funds its own activation (default OFF)
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
        "MTG_LEAF_GRADE_NOWIN",
        "MTG_LEAF_VALUE_RES",
        "MTG_LEAF_TB_BOARD",
        "MTG_LEAF_TB_PERMS",
        "MTG_LEAF_TB_NONLAND",
        "MTG_LEAF_NOWIN_FORCE",
        "MTG_AL_SSM_ROLLOUT",
        "MTG_M2_CAP1",
        "MTG_M2_WAVES",
        "MTG_M2_AXES",
        "MTG_M2_BPVARS",
        "MTG_M2_FIXPOINT",
        "MTG_M2_KEY_COARSE",
        "MTG_M2_FIX_UNFILTERED",
        "MTG_M2_FIX_RESOLVE",
        "MTG_STOMPY_ORDER",
        "MTG_SCALED_LAND_RANK",
        "MTG_TOP_RESOLVE",
        "MTG_STOMPY_WT_LITERAL",
        "MTG_STOMPY_TT_LITERAL",
        "MTG_KE_CONDEMN",
        "MTG_BP_CONDEMN_TAIL_EXEMPT",
        "MTG_BP_CONDEMN_ORDER_AWARE",
        "MTG_SF_PUT_BP",
        "MTG_KE_ORDER_FULL",
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
        "MTG_BP_CONDEMN_ALLPATHS",
        "MTG_BP_SITE3",
        "MTG_BP_SITE3_DEFER",
        "MTG_BP_PARTITION_CANTRIP",
        "MTG_HINATA_ALL_MAIN2",
        "MTG_MAIN2_DROP",
        "MTG_BP_NODE",
        "MTG_HINATA_RANGE",
        "MTG_CANTRIP_ORDER",
        "MTG_HINATA_SUBSET_CREDIT",
        "MTG_EXEC_FEAS",
        "MTG_NONCLEANUP_SHED_WORST",
        "MTG_MINOTAUR_DISCARD_V2",
        "MTG_MINOTAUR_DISCARD_VIAL",
        "MTG_MINOTAUR_DISCARD_PLAY",
        "MTG_MINOTAUR_DISCARD_PLAY2",
        "MTG_SPASM_UNTAP_LITERAL",
        "MTG_MINOTAUR_DISCARD_REDUCER",
        "MTG_MINOTAUR_DISCARD_EV",
        "MTG_MINOTAUR_DISCARD_EVHARD",
        "MTG_MINOTAUR_DISCARD_DUPES",
        "MTG_MINOTAUR_DISCARD_EV2",
        "MTG_MINOTAUR_EV_DISCARD",
        "MTG_MINOTAUR_DISCARD_CSVAL",
        "MTG_MINOTAUR_DISCARD_CSNOP",
        "MTG_MINOTAUR_DISCARD_CSDUP",

        "MTG_IRENCRAG_WASTE",
        "MTG_IRENCRAG_PAYOFF",
        "MTG_IRENCRAG_FINISHER",
        "MTG_IRENCRAG_NEEDS",
        "MTG_FILTER_FEED_STRICT",
        "MTG_BP_PREFIX_PREPAY",
        "MTG_FEED_FILTER_FIRST",
        "MTG_EDF_SEQ_ETB",
        "MTG_BP_NODE_D0ONLY",
        "MTG_BP_NODE_ROOTTURN",
        "MTG_BP_NODE_D56",
        "MTG_BP_NODE_KEEPWAVE",
        "MTG_BP_NODE_KEEPWAVE3",
        "MTG_HEROISM_MAGNET_TRAIT",
        "MTG_HEROISM_FRESH_HOLD",
        "MTG_ETB_TAP_YIELD",
        "MTG_CONDEMN_M1_BP",
        "MTG_SUBSET_ROCK_COLOR",
        "MTG_EDF_PROSPECTIVE",
        "MTG_REFLOAT_WILD_C",
        "MTG_REFLOAT_NEED",
        "MTG_REFLOAT_COMBO",
        "MTG_EDF_DRAWLAND_GOFF",
        "MTG_EDF_LOOP_DRAW",
        "MTG_EDF_COMBO_FINISH",
        "MTG_EDF_SEQ_AURA",
        "MTG_EDF_C_CONSERVE",
        "MTG_EDF_WISH_SINK_FLOOR",
        "MTG_EDF_WISH_SINK_SCARCE",
        "MTG_LAND_IDLE_TAPPED_FIRST",
        "MTG_NO_REDUNDANT_REDUCER",
        "MTG_BP_UNIFORM_DEV",
        "MTG_FLUCT_HOLD_FUEL",
        "MTG_DIG_HOLD_FUEL",
        "MTG_FLUCT_REBUY",
        "MTG_GREEDY_HOLD_LAND",
        "MTG_EDF_C_BUDGET",
        "MTG_EDF_WISH_CAST_GATE",
        "MTG_EDF_TUTOR_NARROW",
        "MTG_EDF_SINK_KMAX",
        "MTG_EDF_BLINK_KMAX",
        "MTG_EDF_M2",
        "MTG_EDF_AUTOGOFF",
        "MTG_EDF_LIB_ROUTE",
        "MTG_EDF_VAL_RAMP",
        "MTG_EDF_VAL_COMBO",
        "MTG_EDF_GOFF_EXACT_AUTO",
        "MTG_EDF_CO_ROOT",
        "MTG_EDF_CO_LOOK",
        "MTG_EDF_EXACT_EXECUTOR",
        "MTG_EDF_HAND_GOFF",
        "MTG_EDF_PAYLOAD_FIRST",
        "MTG_EDF_AURA_HOST_SIG",
        "MTG_BOUNCE_SPARE_AURA",
        "MTG_HOLD_C_FOR_LINE",
        "MTG_LINE_C_HOLD",
        "MTG_EDF_HAND_GOFF_REFUND",
        "MTG_EDF_AURA_HOST_SIG_KAROO",
        "MTG_EDF_DRAW_SINK_HONEST",
        "MTG_SNOW_CAST_ORDER",
        "MTG_SNOW_ORDER_FIXER",
        "MTG_SNOW_ORDER_SPLIT",
        "MTG_SNOW_ACT_ORDER",
        "MTG_BP_CONDEMN_SAME_TURN",
        "MTG_SNOW_CONDEMN",
        "MTG_BP_EMPTY_ARM",
        "MTG_BP_CONDEMN_NEW_OPTION",
        "MTG_BP_CONDEMN_PLAN_CAST",
        "MTG_BP_ABILITY_DELTA",
        "MTG_PAINLAND_TAPAHEAD_LOOP_AUTO",
        "MTG_COMBO_OFF_DEPLOY_NO_COUNTER",
        "MTG_COMBO_OFF_DIG_BANK_FIRST",
        "MTG_TAPAHEAD_CYCLE_SET",
        "MTG_FS_IDLE_NODE",
        "MTG_EDF_COMBO_ROUTE",
        "MTG_BP_CANDS_ORDER",
        "MTG_BP_W4",
        "MTG_BP_NODE_HOST2",
        "MTG_BP_NESTED_CANON",
        "MTG_BP_VARIANT_FIRST",
        "MTG_BP_NESTED_CANON_PLAYOUT",
        "MTG_SAC_OUTLET_PAY",
        "MTG_FOLD_COUNTER_SOURCES",
        "MTG_FUNGUS_SAC_DRAW_CLOCK",
        "MTG_BP_CONDEMN_NEWOPT_BYNAME",
        "MTG_FORCE_USES_M2",
        "MTG_SNOW_ORDER_DRAW_EARLY",
        "MTG_SNOW_ORDER_TAPDRAW_EARLY",
        "MTG_FUNGUS_SPORE_POOL",
        "MTG_ETB_WATCHER_GATES",
        "MTG_LAZY_LEAF",
        "MTG_BP_HAND_ENTRY",
        "MTG_BP_WAVEDROP_HOSTED",
        "MTG_BP_ACQ_CLAUSE",
        "MTG_BP_DIG_AXIS_FANOUT",
        "MTG_BP_NEW_ONLY",
        "MTG_PENDING_FILTER_SLOT",
        "MTG_MINT_CREDIT_EXACT",
        "MTG_BP_MINT_SITE",
        "MTG_BP_REPLAY_COST",
        "MTG_FLING_AXIS",
        "MTG_TECTONIC_AXIS",
        "MTG_TECTONIC_KEEP_AXIS",
        "MTG_SAC_OUTLET_POOL",
        "MTG_SAC_FODDER_RESERVE",
        "MTG_FUNGUS_DEVOUR_CANDS",
        "MTG_FUNGUS_M2_DEVOUR",
        "MTG_FUNGUS_M2_GATE",
        "MTG_FUNGUS_M2_ROOT",
        "MTG_FUNGUS_DEVOUR_BIG_EXEMPT",
        "MTG_RESCUE_TOTAL_GATE",
        "MTG_M2_EMPTY_FAST",
        "MTG_FOLD_SEARCH_ODO",
        "MTG_ACT_TAP_RESERVE",
        "MTG_RESCUE_TAP_SOURCE",
    };
    // The enum and this table are ONE mapping split across two lists: a slot added to one and not
    // the other silently shifts every lever after it (a manifest asking for lever X would set Y).
    // Make that a compile error rather than a measurement mystery.
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<std::size_t>(COUNT),
                  "HeuristicArm: slot enum and kNames are out of sync -- add the new slot to BOTH");
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
