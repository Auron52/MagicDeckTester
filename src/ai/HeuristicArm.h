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
    M2_CAP1,                  // MTG_M2_CAP1               cap the interior m2 solve to depth 1
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
    SNOW_ACT_ORDER,           // MTG_SNOW_ACT_ORDER      order the two tap-draws (direction: SNOW_ACT_AUGUR_FIRST)
    SNOW_ORDER_WATCHER,       // MTG_SNOW_ORDER_WATCHER  Slumber's enter-scry right after the fixer, ahead of what it watches
    SNOW_ACT_AUGUR_FIRST,     // MTG_SNOW_ACT_AUGUR_FIRST Frost Augur activates before Scrying Sheets (off = Sheets first)
    // Breakpoint-condemnation SOUNDNESS guard: the snapshot binds only on the turn it was taken.
    // Overridable per job so "what does the hole cost?" is measurable without a rebuild; `false` is
    // the BROKEN arm, kept to reproduce the finding, not a neutral one.
    BP_CONDEMN_SAME_TURN,     // MTG_BP_CONDEMN_SAME_TURN  a breakpoint snapshot describes ONE turn
    SNOW_CONDEMN,             // MTG_SNOW_CONDEMN        Snow opts into breakpoint condemnation
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
    LAZY_LEAF,                // MTG_LAZY_LEAF             probe each pass LEAFLESS first; an in-window win needs no leaf at all (default ON, unlimited budgets only)
    BP_HAND_ENTRY,            // MTG_BP_HAND_ENTRY         a card entering hand OUTSIDE a cast apply arms site 10 too (default OFF)
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
    FUNGUS_DEVOUR_LANDMARKS,  // MTG_FUNGUS_DEVOUR_LANDMARKS  devour k is a COMPUTED landmark menu (decline / free-fodder prefix / last-outlet / quest / eat-all), size independent of board width, instead of one rung per contested body (default OFF)
    FUNGUS_DEVOUR_LETHAL,     // MTG_FUNGUS_DEVOUR_LETHAL  ...and a PROVEN kill collapses the menu to the single entry `own`: in the SECOND main the count cannot change this turn's combat, and eating everything maximises both the counters and the sac drain, so it DOMINATES every smaller k (default ON; the conservative projection is Mycoloth's own body alone next turn)
    RESCUE_TOTAL_GATE,        // MTG_RESCUE_TOTAL_GATE   skip the filter real-payment rescue when the flat failure is a TOTAL shortfall and every conversion source is total-preserving (default OFF)
    M2_EMPTY_FAST,            // MTG_M2_EMPTY_FAST        a PROVEN-EMPTY second main costs the recursion and nothing else: no state copy, no apply, no dedup key (ADOPTED default ON; =0 runs the do-nothing plan through the loop, which must be byte-identical)
    FOLD_SEARCH_ODO,          // MTG_FOLD_SEARCH_ODO      the SEARCH's private subset walk declares itself an odometer, so the canonical-prefix fold applies there too (default OFF)
    ACT_TAP_RESERVE,          // MTG_ACT_TAP_RESERVE      a planned `{cost},{T}` activation's SOURCE is held back from mana payment for the whole plan, so an earlier cost cannot strand it (default OFF)
    RESCUE_TAP_SOURCE,        // MTG_RESCUE_TAP_SOURCE    the filter real-payment rescue applies each selected activation's own {T} before paying, so no source funds its own activation (default OFF)
    SAC_FODDER_SAME_LINE,     // MTG_SAC_FODDER_SAME_LINE  a sac-for-mana outlet may be paid with fodder THIS LINE creates (default OFF; adds actions, so it moves GT)
    SAC_FODDER_VALUE_OUTLET,  // MTG_SAC_FODDER_VALUE_OUTLET  ...and for a VALUE outlet (draw/damage), not just a mana one (default OFF)
    SAC_FODDER_TAP_MAKER,     // MTG_SAC_FODDER_TAP_MAKER  ...and the maker may pay {T} (Krenko feeding Skirk) -- MOVES GOBLINS (default OFF)
    ENTER_WATCHER_GATE,       // MTG_ENTER_WATCHER_GATE   FireCreatureEnterWatchers' own cascade loop is skipped when the DECKLIST holds no creature-enter watcher (default ON; =0 walks the board as before, which must be byte-identical)
    FUNGUS_SHRINK_SAC_PAYER,  // MTG_FUNGUS_SHRINK_SAC_PAYER  a SHRINK-ONLY sac outlet (Deathspore Thallid) is offered only when a death-payer is on the board or in hand (default OFF)
    FUNGUS_SHRINK_SAC_M2,     // MTG_FUNGUS_SHRINK_SAC_M2     ...and, SEPARATELY, that outlet is deferred to the second main (ADOPTED 2026-09-25, default ON; 2.95x on candidate B's tail and t=-3.00 on 1,392 held-out games -- the fused A/B that called it slower could not attribute, see the read site)
    SAC_DRAIN_LETHAL,         // MTG_SAC_DRAIN_LETHAL        the multi-sac LETHAL burst is sized by outlet damage PLUS the per-death drain of our death watchers (Slimefoot / Pashalik Mons). Adds an action, so it moves GT (default OFF)
    FS_PRE_STATE_SKIP,        // MTG_FS_PRE_STATE_SKIP   an ORDINARY plan on FSLineWin's frontier is SKIPPED when an earlier sibling already scored its post-apply state (default OFF; an IDENTITY collapse, not a dominance ordering)
    DIG_MANA_LAST,            // MTG_DIG_MANA_LAST        a tap-draw activation is NOT OFFERED while the mana it wants could deploy a PERMANENT from hand instead (default OFF; a HEURISTIC narrowing, not lossless)
    SPORE_POP_ALL,            // MTG_SPORE_POP_ALL       the SPORE activation-count ladder collapses to "pop them all", EXCEPT while a token doubler sits in hand payable now or within MTG_SPORE_HOLD_TURNS turns (default 1), where it narrows to {1, all} and the search decides. USER 2026-09-26: "there is no benefit to waiting unless we are likely to play a doubling season next turn" -- exact here because a spore counter's only sink is the 1/1 Saproling, counters do not decay, and the token never gets bigger. ADOPTED 2026-09-27, default ON: quality FLAT (-0.00036t labelling, 0.00000t play over 4,550 held-out games) at 1.095x / 1.036x, every labelling seed faster
    SPORE_HOLD_WIDE,          // MTG_SPORE_HOLD_WIDE     ...and the hold window is TWO turns, not one ("arguably the turn after could make sense to consider as well if that doesn't work well") -- sub-mode of the above (default OFF)
    SPORE_HOLD_NONE,          // MTG_SPORE_HOLD_NONE     ...or ZERO: pop always, even with a Season projected. Prices the searched exception itself -- sub-mode of the above (default OFF)
    SPORE_HOLD_SECOND,        // MTG_SPORE_HOLD_SECOND   ...and a doubler ALREADY RESOLVED also opens the searched branch when a SECOND one is in hand and reachable. USER 2026-09-26: "it is not clear to me that it is ever worth waiting for the second season to drop... that is something we can test" -- the default (pop) is the user's expectation, this arm tests the alternative (default OFF)
    M2_FREE_ACTIVATION,       // MTG_M2_FREE_ACTIVATION  the post-combat PRODUCTIVITY gate also opens the phase for a FREE counter-paid activation on a permanent that entered this turn (Saproling Burst cast in main 1). Without it 69% of Bursts sat idle the turn they landed -- but it adds m2 solves, which the 2026-09-08 "no extra mains to search" doctrine is about, ADOPTED 2026-09-26, default ON: -0.126t labelling / -0.139t play on seven held-out seeds, and still 2.01x/1.37x faster
    FADE_K_WINDOW,            // MTG_FADE_K_WINDOW        the fade-outlet activation-COUNT ladder (Saproling Burst) is replaced by a PROJECTED LANDMARK MENU: the power peak (with the bodies already out folded in), plus a Beastmaster-Ascension attacker threshold, plus the leave-2/leave-1/leave-0 lines ONLY when they reach a kill this turn or next that the peak does not. Up to 112 candidates become ~3. ADOPTED 2026-09-26, default ON (-0.004t at both depths, 2.04x/1.50x). See docs/design/fade-k-axis-landmarks.md (a HEURISTIC narrowing, not lossless)
    BP_ETB_DIG,               // MTG_BP_ETB_DIG            an ETB dig (Staunch Crewmate) opens site 10 so the dug card is castable this turn (default ON since 2026-09-26)
    ETB_TREASURE_SPEND,       // MTG_ETB_TREASURE_SPEND    a FREE enter-trigger Treasure (Corsair Captain) / a Larcenist-converted Treasure is spendable the turn it appears, and the enumerator credits the ETB Treasure to later casts of the same plan (default ON; =0 restores the fresh-hold for it)
    PIRATES_CAST_ORDER,       // MTG_PIRATES_CAST_ORDER    the USER-reviewed full Pirates cast order (see PiratesProvider::CastOrderRank; ADOPTED default ON 2026-09-27)
    PAYABLE_ORDER,            // MTG_PAYABLE_ORDER         the cast order is kept unless it projects UNPAYABLE (reveal-aware: a Daring Buccaneer's reveal leaves with the cards cast before it); then the maker hoist, then the nearest payable permutation of the same casts (ADOPTED default ON 2026-09-27)
    PD_SECOND_MAIN,           // MTG_PD_SECOND_MAIN        Prevent Damage: a symmetric sweeper (Pyrohemia / Rolling Earthquake) opens the searched SECOND main -- attack first, then sweep (default OFF since the Stage 5 A/B 2026-09-27: neutral quality at 1.6x CPU; PROVISIONAL)
    PD_DINA_LETHAL_GATE,      // MTG_PD_DINA_LETHAL_GATE   Prevent Damage: Dina's sac-pump is offered to the search only when an optimistic bound says it closes the game THIS turn (PROVISIONAL default ON 2026-09-27; a slot so the Stage 5 A/B pools both arms in one batch)
    PD_DUP_LEGEND,            // MTG_PD_DUP_LEGEND         Prevent Damage: a duplicate Vito / Dina (dies to the legend rule on resolution) is offered only when its CAST can pay off -- a damaging cast trigger (Spellshock) with a gain engine (Tamanoa / Purity) on board or in hand -- or its entry/death has upside. Default ON (a dominated-cast prune); =0 = the generic whitelist only
    PD_SELF_LETHAL_GUARD,     // MTG_PD_SELF_LETHAL_GUARD  Prevent Damage: never offer a cast plan whose first land tap dies to Manabarbs, nor a Rolling Earthquake whose X reaches our own life (no Purity) -- both lose (or at best draw) at once (default ON)
    PD_LEAF_OWN_LIFE,         // MTG_PD_LEAF_OWN_LIFE      Prevent Damage: the no-win leaf tie-break also prices OUR life, strictly under the opponent-life term (life is this deck's fuel: pain, Spellshock and Manabarbs spend it). Stage 5 lever; see PreventDamageProvider::NoWinLeafPricesOwnLife
    PD_PAIN_PAY,              // MTG_PD_PAIN_PAY           Prevent Damage: PAIN-AWARE mana payment (dmgev::PainAwarePay) -- a payment never pays itself dead when a survivable assignment exists; with no gain engine it takes the MINIMUM-damage assignment (pain + Manabarbs); with one it prefers pain inside the safety bound. Default ON (claude-play sweep gi9/gi2, 2026-09-27); =0 = the pain-blind payer
    PD_BUCKET_DISCARD,        // MTG_PD_BUCKET_DISCARD     Prevent Damage: the authored 5i bucketed cleanup-shed policy (PreventDamageProvider::CleanupDiscardCandidates). Default ON (PROVISIONAL, user review); =0 = the generic max-MV fallback
    PD_SHED_UNPLAYED_LAND,    // MTG_PD_SHED_UNPLAYED_LAND Prevent Damage (inside the 5i policy): at a cleanup whose land drop went UNUSED with a live land in hand, that land is shed FIRST -- so declining the drop to pitch a spell is strictly dominated by playing the land. Only when MTG_PD_BUCKET_DISCARD is on
    PD_WISH_RANK,             // MTG_PD_WISH_RANK          Prevent Damage: Living Wish's candidate ORDER is the board-aware engine-role ranking of the USER's doctrine (2026-09-29: Tamanoa / Vito first, Dina / Rhox Faithmender backups, a 2nd Tamanoa over Rhox unless {R}{G}{W} is tight; lands next, colour-fixers first; the rest last). An ordering only -- every name is still a searched variant. =0 = the nonlands-first sideboard order
    PD_WISH_ZENITH,           // MTG_PD_WISH_ZENITH        Prevent Damage (inside MTG_PD_WISH_RANK): a Green Sun's Zenith in hand that can still find a gain engine (Tamanoa) in the library counts as HAVING the gain role, so the Wish goes for the drain the Zenith cannot reach (Vito). USER doctrine 2026-09-29. =0 = the ranking without it
    PD_WISH_USEFUL,           // MTG_PD_WISH_USEFUL        Prevent Damage: the AUTONOMOUS search offers Living Wish only the engine creatures (Tamanoa / Vito / Dina / Rhox Faithmender) and the lands -- USER 2026-09-29 ("restrict the search to just the useful wish targets"). A provider PRUNE; human play keeps every name. =0 = every legal name
    PD_WISH_TRIM,             // MTG_PD_WISH_TRIM          Prevent Damage (inside MTG_PD_WISH_USEFUL): situational trim -- the lands go when our lands already make every needed colour and we hold a land for this turn and next; a Vito / Dina we already hold (hand or board) goes (legend rule). USER 2026-09-29. =0 = the six useful targets
    PD_WISH_VITO_OVER_DINA,   // MTG_PD_WISH_VITO_OVER_DINA Prevent Damage (inside MTG_PD_WISH_TRIM): in 1v1, while Vito is fetchable and not held, Dina is not a Wish target (Vito's drain is the amount gained, Dina's 1 per event; 2HG keeps Dina). USER 2026-09-29, measurement lever, default OFF
    PD_ZENITH_SKIP_DINA,      // MTG_PD_ZENITH_SKIP_DINA   Prevent Damage: Green Sun's Zenith does not fetch Dina (search only) while a Dina is ours on the battlefield, or in hand and castable ({B}+{G} from distinct lands incl. one land drop) -- legend rule. USER 2026-09-29. =0 = every target
    BOTTOM_NAME_DEDUPE,       // MTG_BOTTOM_NAME_DEDUPE    clairvoyant London bottoming rolls out ONE game per distinct removal by NAME (a second copy of a name, or a subset differing only in which copy, reuses the first's win turn). Cost lever, default ON (PD A/B byte-identical, -14% CPU); =0 = one rollout per physical card / mask
    PD_QUAKE_NO_UNDERSHOOT,   // MTG_PD_QUAKE_NO_UNDERSHOOT Prevent Damage: Rolling Earthquake X is never BELOW the survival ceiling (one less than the smallest lethal-toughness margin among OUR creatures -- board, plus the castable creatures in hand as a second ceiling), capped at max. USER 2026-09-29: "it makes no sense to do less than the toughness of your creatures". X = 0 under Spellshock kept. REFUTED 2026-09-29 (+0.045t, t=+4.8: it deleted the smaller X that PAYS for a same-turn Vito) -- superseded by MTG_PD_QUAKE_TOP_X, default OFF
    PD_QUAKE_NO_OVERKILL,     // MTG_PD_QUAKE_NO_OVERKILL  Prevent Damage: Rolling Earthquake X never kills one of OUR creatures (above the ceiling) unless X alone is lethal to the opponent -- USER 2026-09-29: "killing them is usually bad ... if Vito dies he cannot make the opponent lose life ... if they have X life remaining, that might make sense". ADOPTED default ON 2026-09-29, PROVISIONAL (with TOP_X: 0.000t on held-out seeds, 0.94x CPU; counterexamples = a Dina killed to feed Vito a bigger X, a Tamanoa killed by a late X=6)
    PD_QUAKE_TOP_X,           // MTG_PD_QUAKE_TOP_X        Prevent Damage: among plans IDENTICAL except for Rolling Earthquake's X, keep only the LARGEST X at or below the survival ceiling (our board creatures AND the creatures this plan casts) -- the USER's "no sense to do less than the toughness ... assuming you have remaining mana", at PLAN level: a smaller X that pays for another cast is a different plan and survives. X above the ceiling untouched. ADOPTED default ON 2026-09-29, PROVISIONAL (vs no-overkill alone: -0.005t, 0 worse / 4 better, 0.96x)
    SL_SECOND_MAIN,           // MTG_SL_SECOND_MAIN       a deck holding a life-gained mana source (Accomplished Alchemist, "{T}: Add X mana of any one color, where X is the amount of life you gained this turn") searches a POST-COMBAT main. Life gained during COMBAT (Blossoming Bogbeast's attack trigger) is a resource GENERATED DURING COMBAT (2c-bis) that only a second main can spend, and main 1's gains are still on the counter in main 2 -- so without m2 the card's scaled mode is structurally unreachable and the deck is under-rated. A REACHABILITY case, not a sequencing preference, so default ON -- but a searched m2 doubles the per-turn solve (the blunt MTG_FORCE_USES_M2 arm measured 4.25x at d1/b3 on Fungus), so =0 is the arm that prices it. Its FIRST measurement (2026-09-29) is VOID: the deck's only combat lifegain source, Blossoming Bogbeast, was not yet implemented, so m2 uniquely unlocked nothing and the arm priced 1.62x CPU for 0.000 turns of benefit -- the right number for the wrong deck. Both reasons (Bogbeast's attack_trigger_lifegain, the Alchemist's mana_per_life_gained) now share this ONE lever, because a term outside it would keep m2 on in the =0 arm and turn the A/B into a null. RE-MEASURED 2026-09-30
    ACT_LINE_HOLD,            // MTG_ACT_LINE_HOLD        while the plan's CASTS pay, hold what the plan's own TRAILING ACTIVATIONS still need -- their {T} SOURCE and their COLOURED pips (ActLineHoldMask, ManaPayment.cpp + a rung of the BatchPrepayMainCasts ladder). The generalisation of MTG_LINE_C_HOLD from a blink's {C} to any activation's whole cost. Fixes a silent REACHABILITY hole, not a tap-order preference: today the payer taps the activation's own source for mana (nullifying the {T} half of its own cost) or spends its last coloured provider, and the trailing pass then no-ops with NO `drops` disclosure and no log line. Measured live at HEAD with MTG_ACT_DROP_AUDIT (40 games, d3, 2026-09-30): Snow 1,539,268 tapped + 62,479 unpaid vs 3,851,912 fired; EldraziDisplacerFlicker 214,255 + 103,753 vs 414,267; SelesnyaLifegain 62,942 + 3,270 vs 1,280,827; Prevent Damage 0 + 12,903 vs 307,307. Lossless (held-first attempt, unrestricted retry), so no cast is ever lost. ADOPTED 2026-09-30, DEFAULT ON: at every SEARCHED depth no cell on any deck is significantly worse and SelesnyaLifegain pooled is significantly better (p=0.020); the d0-only regression is a separate pre-existing defect (the greedy projection over-values a trailing activation) that this fix stops hiding; CPU +28% on Snow is VOLUME (payments +33% at -13% each, drop rate 24.3%->10.3%), no budget near
    SNOW_LOOK_COLOR,          // MTG_SNOW_LOOK_COLOR     breakpoint site 8's playability gate tests the found card's COLOUR, not just its mana value. The shipped gate is `have.Total() >= ManaValue()`, which on this deck is not a corner case: the activation that just fired tapped a Scrying Sheets and paid its own {1}{S}, and 4 Sheets + Boreal Druid all produce {C} -- so the pool left behind is colourless-skewed and a found Frost Augur {U} / Boreal Druid {G} / Ice-Fang Coatl {G}{U} / Marit Lage's Slumber {1}{U} passes on mana that provably cannot cast it, buying a nested Solve of the rest of the turn for nothing. A NECESSARY condition (ManaPool::CanPay is the enumerator's own deliberately-OPTIMISTIC payability test, so a failure means NO assignment of the untapped sources pays), hence it cannot delete a reachable line. Armed only for a card with tap_draw_requires_top_supertype = Scrying Sheets and Frost Augur, held only by decks/Snow -> every other deck byte-identical by construction. Measured 2026-09-30: ceiling 13.2% of site-8 opens (115,254 of 871,579); UNBUDGETED d3 units -1.78%, CPU -1.84%, play BYTE-IDENTICAL; on the budgeted d3/b10 tier only -0.26% and the digest MOVES, because a budget is a work CAP and freed work is respent ([[matrix-cost-is-abandonment-rate]]). Default OFF
    ACT_HOLD_OUTER,           // MTG_ACT_HOLD_OUTER      a breakpoint CONTINUATION's payment also holds the OUTER plan's still-unfired trailing-activation {T} SOURCES. All three continuation sites install traits derived from the continuation's own actions (`ComputePlanTraits(state, extra.actions)`), which is right for every other field -- deriving mana_casts / pump_target from the outer plan caused the mirrorwing gi43/242/292 divergent-payment class -- but wrong for this one, because the base plan's activations are still owed their {T} while the continuation's casts are paid. A continuation cast then taps a Scrying Sheets or Frost Augur the plan was about to activate and the trailing pass no-ops it SILENTLY (no `drops`, no log line, no fd-diverge: both worlds shadow identically, so lockstep holds and the better line is simply absent from both). Size: MTG_BP_SEARCH=0 takes the `tapped` drop class from 44,439 to ONE on 2 snow d3/b10 games, so this is essentially the whole remaining case-A residual of MTG_ACT_LINE_HOLD. Carries act_src_nums ONLY -- that list is self-limiting (part (a) of ActLineHoldMask holds untapped sources only, so a fired activation drops out) whereas act_pips is never decremented and would over-hold. Default OFF
    FOLD_ODO_SKIP,            // MTG_FOLD_ODO_SKIP      hoist SubsetHasDuplicateSacSource's canonical-PREFIX clause (dupclause::kFoldPrefix) from the subset leaf to the odometer DIGIT. Among activations sharing an equiv_tag only a prefix selection {0..k-1} is canonical, but the rule is enforced after the subset is built -- so a non-canonical combination of group digits still runs its whole inner 2^num_ind loop and calls consider() on every position, each rejected for a reason decided before the inner loop began. Measured on Snow's heaviest decision (seed 934087 t3, 225,185 units = 25x budget, 14.4 s): 13,004,839 of 20,002,295 subset visits = 65.0% rejected by this one clause, and only 16.4% of visits are ever scored. Snow provokes it because 4 Scrying Sheets + 4 Frost Augur are two interchangeable classes of four and each activation is its own group, so 256 digit combinations hold 25 canonical ones. BYTE-IDENTICAL by construction (it rejects a subset of what the leaf clause rejects, and that clause still runs); the precompute disarms entirely if a tagged action sits in `independent` or `auto_sel`, where it could supply a missing predecessor ord. Default ON -- the hatch is a cost A/B, like MTG_EQUIP_PIECE_DEPS
    SOLVE_CHARGE,             // MTG_SOLVE_CHARGE        charge the GREEDY SUBSET WALK against the active budget, one unit per MTG_SOLVE_CHARGE_W `consider` visits (GreedyChargeGuard, TurnSolver.cpp; W default 1). SearchBudget counts one unit per simulated turn-step and the walk inside SolveUncached charged NOTHING, so a decision's real spend is `nodes + visits/W` against an allowance calibrated on nodes alone -- on snow that let 63.6% of decisions run past their budget and 36.4% past 10x it. NOW A PER-DECK OPT-IN (DecisionProvider::SolveChargeWeightOptIn, resolver SolveChargeWeightFor; SnowProvider = 16), because how much the walk matters is a property of the deck's cost shape: snow is walk-dominated (0.742x core-ms at W=16 for +3 games in 2,400) while the combo searches are node-dominated (hinata lost 11 games per 128 at W=1 for 0.995x -- pure loss). So this slot is now the A/B CONTROL rather than the adoption: =false gives a true uncharged arm on an opted-in deck, =true charges a deck that has not opted in. Do NOT read the old "recalibrate NODES_PER_VIRTUAL_MS" note as pending -- measured, a uniform knob bump makes every deck but snow 1.21-1.55x DEARER, which is why the weight is the lever instead. Measurements: docs/design/snow-cost-2026-10-01.md
    BP_NOBP_SITE9,            // MTG_BP_NOBP_SITE9       refuse a base plan whose apply SUPPRESSED a searched-only breakpoint site from the wave-0 NOBP skip (g_bp_searchonly_suppressed). The shipped skip MTG_BP_W0_NOBP is default ON and its identity is KNOWN FALSE at site 9: the gate short-circuits on `plan.bp_choice >= 0`, so for a base plan it is never evaluated and `g_bp_any_last` reads zero for a plan with a real decision pending -- admitting it then deletes real lines (25 mismatches of 41, two fixtures losing a turn at an UNBOUNDED budget, where there is no freed work to re-spend and so no churn defence). The FSLineWin twin applies the term unconditionally; the SHIPPED host did not apply it at ALL until 2026-10-01 (the flag armed a watch whose answer nothing in that host read). A SLOT rather than env-only because the adoption question is a pooled A/B and an env static pins one answer per process. Default OFF: tightening a default-ON skip skips strictly LESS and moves the committed line on every budgeted cell of every deck
    // The USER's sac-VICTIM rule (2026-10-01), as two independently-measurable halves. Per-job
    // because the rank they adjust is read inside the sort comparator of four separate ranking
    // sites, so a process can only ever BE one arm -- and the first attempt to price them ran the
    // arms SEQUENTIALLY and got OPPOSITE SIGNS (off=87.48s/on=129.40s, then off=126.50s/on=96.04s)
    // purely from the neighbour's load. A pooled queue puts both arms on the box at once.
    SAC_VICTIM_DOOMED,        // MTG_SAC_VICTIM_DOOMED   a token its own creator will destroy (a faded-out Saproling Burst) is one step MORE expendable, so it loses a tie to an equal-power permanent body. USER: "prioritize 1/1 saprolings unless the Saproling Burst saprolings are the same size or smaller" (default OFF)
    SAC_VICTIM_ENGINE,        // MTG_SAC_VICTIM_ENGINE   a combat-damage TOKEN ENGINE (Shroofus Sproutsire) joins the `scaling` defer tier, so its 1/1 body is not eaten as ordinary Saproling fodder. USER: "I don't mean never shroofus, but Shroofus should be the last to go" -- DEFERRED, not excluded (default OFF)
    SAC_POOL_TURN_COLOR,      // MTG_SAC_POOL_TURN_COLOR  the sac-outlet POOL's colour fan is scoped to THIS TURN's sinks (hand + battlefield + graveyard) instead of the deck-wide scan that includes the LIBRARY. Fungus candidate-b's black splash is 3 cards in 60, and because they sit in the library all game the fan is {G,B} on every board -- which trips the pool's singleton gate and silently disables the collapse on 100% of 4,425,133 gate arrivals (37.48% of them would otherwise pool). A HEURISTIC narrowing: it can drop a plan that floats {B} speculatively and then draws the black card the same turn. ADOPTED default ON 2026-10-01 (USER accepted the lossy case; quality-neutral, 3-5% cheaper, byte-identical on the suite's own mono-green Fungus list so it moves no GT). It moves candidate-b's play at d5/b20 and so moves the keepgen play digest -- resume a banked generation across that with MTG_KEEP_RETAIN_FOREIGN (clean-R retention: completed cell-sides only), NOT by holding this off
    SAC_PAY_PLAN_FODDER,      // MTG_SAC_PAY_PLAN_FODDER credit fodder the LINE ITSELF creates (a Saproling made by removing three spore counters) into §2b's payability bound, which otherwise counts only bodies already on the battlefield. Repairs the measured MTG_SAC_OUTLET_PAY regression: the bound read 1 against need 2 and PROVED a payable Sporecrown Thallid unpayable, because the Saproling it eats is made by an earlier action of the SAME plan. Sound -- these bounds may only ever OVER-count -- and inert unless MTG_SAC_OUTLET_PAY is on, since LiveSacPayOutlet gates the whole term. Default ON
    FREE_EQUIP_MANDATORY,     // MTG_FREE_EQUIP_MANDATORY  a FREE, PURE-UPSIDE equip onto a host ALREADY on the battlefield is MANDATORY, not a searched digit: its odometer group may not take the 0 ("skip") position. The dominance argument is the user's own (2026-10-01, on KittyEquipment v2): attaching an Equipment that costs {0} right now and whose attach has no downside is never worse than leaving it unattached -- a creature holds unlimited Equipment so they do not compete, nothing in the engine prices an attached Equipment as a cost, and if metalcraft later switches OFF the attachment is already paid for while an unattached copy becomes dear again. So the whole 2^k subset over k such digits collapses to the single all-taken position. MEASURED on kitty v2: the heaviest frames are 10 groups of which SEVEN are single-variant same-host free equips (odo 2048, 2^7 of it pure waste), and the subset funnel rejects 0.00% of 36,672,476 visits. Same shape and same reasoning as MTG_SPORE_POP_ALL ("there is no benefit to waiting"), and like it an EXACT collapse rather than a heuristic narrowing -- so the three soundness carve-outs are enforced, not argued: (a) the host must ALREADY be on the battlefield, because a host that only a cast digit brings in would make the forced position ILLEGAL and delete real plans (this is the hole MTG_EQUIP_PIECE_DEPS exists for); (b) equip_grants_shroud anywhere in the frame disarms it entirely -- Lightning Greaves granting shroud to its own host makes every LATER equip onto that host an illegal target, so forcing digits could manufacture an unplayable sequence; (c) equip_sacrifices_prior_host and any negative bonus are excluded. Default OFF until measured
    METALCRAFT_EQUIP_HOIST,   // MTG_METALCRAFT_EQUIP_HOIST  extend the AUTO-EQUIP collapse (TurnSolver.cpp, both twins) ACROSS the same-turn metalcraft flip, by testing whether the frame's own casts will make an equip free instead of whether it is free at the enumeration stamp. The collapse already does the structurally right thing -- it ERASES the group and force-includes the best variant in every subset, a true radix removal rather than a reject predicate -- but its freeness test is `cands[j].cost.ManaValue() != 0`, and that cost is baked from EquipCostGenericNow(state) at candidate time. So on the one turn that matters, the go-off turn (cast artifact #3, flip metalcraft ON, then stack Colossus Hammers for {0}), every equip is stamped at its PRINTED cost, the collapse declines, and the odometer powersets a take/skip family every member of which will cost {0} by the time it is paid. This is the SAME stale stamp that SameTurnMetalcraftEquipCredit (default ON since 2026-08-21) exists to undo at the affordability gate -- this slot is that same correction applied one gate earlier, at the enumeration SHAPE. USER 2026-10-01: *"we expect to pay 0 equip costs, except potentially in rare cases where we fail to go off"*, which is also the correction to my own earlier reading: the measured "93% of equip groups rejected because the cost is not zero" that made the since-RETIRED FREE_EQUIP_MANDATORY collapse look vacuous was reading the stamp, not the price actually paid. NOT a pure identity fold, and the one place it is optimistic is named precisely: a hoisted equip rides every subset without contributing to the mana term, so in a subset that does NOT flip metalcraft it is scored free while really costing its printed generic. The PLAY is never wrong -- casts are applied before the trailing equip pass in both worlds, so the equip's own recompute pays out of what the casts left or declines outright (it never attaches for free), making the realised line exactly the un-equipped plan -- the error is confined to that plan's SCORE, the disclosed class SameTurnMetalcraftEquipCredit already runs in under the LeafReducerCreditEnabled law. DISARMED by FrameHasDoubleStrikeSource, and that gate is MEASURED rather than argued: forcing a group's best variant is dominance only while the choice of HOST does not change the rider's value, which is exactly the precondition the USER names for collapsing the host at all (*"There is no double-strike in the deck, so we can just choose one creature and put all equipment on it"*). Un-gated it was -25.3% units on KittyEquipment v1 -- the SUITE deck, Kor Duelist + Balan -- but lost a turn in 3 of 70 games across its d5 cells, while v2, which has no double-strike source, was neutral-to-better. Default OFF until measured, per the projection-vs-dominance rule
    FREE_CAST_HOIST,          // MTG_FREE_CAST_HOIST  force a {0}-MANA EQUIPMENT CAST into every enumerated subset instead of powersetting take/skip over it -- the free-CAST axis of the USER's original framing (2026-10-01): *"It might require a kind of jump of some sort to evaluate with all of the 0-mana cards out."* KittyEquipment v2 holds 14 such cards (Cathar's Shield x4, Bone Saw x4, Kite Shield, Accorder's Shield, Spidersilk Net, ...) and hand casts group by hand SLOT, so a mid-game hand powersets every one of them independently. Structurally the same move as the AUTO-EQUIP collapse -- erase the group, push the action into auto_sel -- which is a true RADIX removal rather than a reject predicate, and that distinction is the lesson of the RETIRED FREE_EQUIP_MANDATORY attempt (removed 2026-10-01): a predicate that merely refuses the 0 position still walks every position it rejects. UNLIKE the metalcraft equip hoist this is EXACTLY mana-neutral, because a {0} cast contributes nothing to the odometer's mana term, so forcing it mis-prices nothing and there is no optimism to bound -- only the dominance claim, and for an Equipment every axis the engine models is monotone upward: it is an ARTIFACT so it advances metalcraft (which only ever reduces costs), it triggers the equipment-cast draw (Sram) and the equipment-ETB draw (Puresteel), it is one more Equipment for a Golem-Skin Gauntlets host to count, Dwalin's hone counters accrue to it while UNATTACHED and cash in on a later equip (SpellEffects.h calls that "what makes the hone axis monotone"), and with Sigarda's Aid out it enters ATTACHED, bypassing the equip cost entirely. Nothing in this engine prices a battlefield permanent as a cost, an Equipment has no body so can never be forced to attack, and casting REDUCES hand size so it cannot cause a cleanup discard. Carve-outs enforced, not argued: equip_grants_shroud (with Sigarda's Aid the entrant auto-attaches, and a shrouded host makes every LATER equip an illegal target -- the same hazard the free-equip collapse disarms frame-wide, reached by another route), equip_sacrifices_prior_host, any negative bonus, {X} costs, and multi-variant groups (a mode choice is a real decision; this argues about WHETHER to cast, not HOW). Required a fix to BuildEquipPieceDeps: it is the first collapse to erase a CAST group, and cast_groups_of returns mask 0 for a hoisted one, stamping the dependent equip {required, groups=0} -- a permanently dead digit, the jittemode=2 enumerated-but-unplayable class. Default OFF until measured
    EQUIP_COPY_SKIP,          // MTG_EQUIP_COPY_SKIP  turn the fungible-equip-copy REJECTION into a RADIX CUT: when FungibleEquipCopyViolated refuses a position, JUMP the odometer past every position that must violate the same pair instead of incrementing by one. A reject predicate is not a collapse -- the odometer still WALKS what it refuses, so the adopted MTG_EQUIP_COPY_COLLAPSE saves a rejected position's mana sum and inner loop but still pays its predicate walk and its carry, and `space_odo` does not move at all (313M positions in one v2 game). The branch-shape instrument prices the full interchangeable-copy fold at 2.378x of the whole search's odometer, and this is the part only a radix cut can reach. EXACT, not heuristic: the canonical form is 'a class's digits are non-increasing in group order', so a violation is a pair (prev, g) with choice[prev] < choice[g], and every position until digit `prev` next changes holds both digits fixed and therefore violates the same pair -- raising `prev` to choice[g] and zeroing below it passes over exactly those. `prev` is the LAST class member before g (largest stride below g = furthest provably safe jump; a higher digit would pass over positions in which `prev` IS raised, some of them canonical). The emitted plan set is bit-for-bit what the predicate alone produced, so digests cannot move and the A/B is purely about work removed -- which is the doctrine's first class (sound identity fold). Applies to the two inline flat walks and EnumeratePlanPositions' flat fallback; the two-stage split needs nothing, because every class member is an Equip, Equip is never IsManaSideAction, so an equip class lives entirely in Stage B -- which runs no group predicates. ADOPTED DEFAULT-ON 2026-10-01 on the user's standing collapse doctrine (*"wasted work is wasted work regardless of the situation... judge a collapse on work removed and soundness, never on whether wall fell"*), and the wall is REPORTED rather than treated as a verdict: v2 held-out 40 seeds and v1 20 seeds both come back digests identical and units identical PER GAME (so the soundness claim is not argued, it is measured on 60 games), smoke 104/104 with 0 configs changed, and ms is 1.0067 / 0.9982 -- i.e. ~58% of this deck's odometer POSITIONS removed for no measurable wall. The two facts together ARE the finding: a position the predicate already refuses costs only its own predicate walk and carry, so walking it was never the expense -- what MTG_EQUIP_COPY_COLLAPSE's 0.967x units buys is not walking the duplicates but not EVALUATING them. Units identical per game is also a safety property: budget units are charged per consider/rollout, not per position, so unlike a memo cap this cannot shift play under budget pressure
    ZERO_POWER_EQUIP_FREE_ONLY, // MTG_ZERO_POWER_EQUIP_FREE_ONLY  do not offer a PAID equip of an Equipment that adds no power OF ITS OWN -- the shields. USER ruling, 2026-10-01 (KittyEquipment v2): *"we want to equip those 0 power equipment only when they are free or I suppose if we have Golem-Skin Gauntlets and nothing else to do with the mana. Overall they are pretty poor to use for anything but draws and free equips in goldfishing."* v2 plays SIXTEEN of them (Cathar's Shield x4, Accorder's Shield x4, Kite Shield x4, Spidersilk Net x4), every one {0} to cast with equip {2}/{3}, and their whole role on the list is the cast: an artifact for metalcraft plus a Sram/Puresteel draw. On an ordinary host the engine ALREADY refuses the equip (rider_delta returns 0 and the `rd > 0` ranking never offers the pair), so this slot narrows exactly ONE case -- a host carrying a Golem-Skin Gauntlets, where EquipAttachDeltaFor correctly reads the attach as +1 power because the Gauntlets counts one more Equipment. That case is the one the user prices as poor rather than wrong: {3} for +1 damage, against {1} for a Colossus Hammer whose equip is free under metalcraft. MEASURED SHAPE, not assumed: on the worst game of a 20-seed probe (seed 13019, 7.56M units, 313M odometer positions) the two heaviest decisions are 12-group frames at odo=4096 whose digits include `Accorder's Shield [equip:victim=58]` and `Kite Shield [equip:victim=58]` -- 4x of that frame's walk is those two shields. A PROJECTION, not a dominance fold (the user's own "nothing else to do with the mana" names the line it drops), so it was built default OFF and measured -- and the measurement REFUTED it, so it STAYS OFF. Held-out 40 v2 seeds (24001-24040, pooled, arms innermost): units 0.9957, ms 0.9679, and that near-null is not a small win but noise, because the lever touched only 2 of 40 games (7.3% of units) and on THOSE two it was worse on both axes -- units x1.0504 and one game a turn LATER (seed 24010, 5 -> 6). The churn control settles which kind of loss that is: re-run at 16x budget the zp arm still finishes a turn later, so the line it removed was real and not freed budget re-spent. The 7.9M-unit tail game that carries 38% of the sample's units was never touched at all, which is the diagnostic point -- a frame census says which decision is HEAVIEST, not where the volume is (memory: two-hosts-not-one-measure-where-the-applies-are). THE FINDING IS THAT THE USER'S OWN EXCEPTION IS LOAD-BEARING: v2 plays THREE Golem-Skin Gauntlets, so on a host carrying all three every further Equipment is +3 power, and "poor" stops being true. The engine already implements the rest of the ruling via `rd > 0`, so there was nothing left to collapse here. v1, the SUITE deck, is byte-identical 20/20 (its only powerless equipment are Lightning Greaves, carved out for haste, and Umezawa's Jitte, carved out for charges). Kept rather than deleted so this is not rebuilt: the slot IS the record of the measurement. Carve-outs enforced rather than argued, each one a named reason the equip IS worth paying for: a free equip at the stamp, an equip the frame's own casts will free via metalcraft (MetalcraftWillFreeEquips -- the stale-stamp lesson), the equipment's own power bonus or per-equipment scaler, haste (Lightning Greaves/Skateboard: the benefit is the attack, not the rider), lifelink, Jitte-style combat charges, a re-host sacrifice (Grafted Wargear), hone counters already on the equipment (Dwalin: SpellEffects.h adds e.hone_counters to the attached bonus, and EquipAttachDeltaFor structurally cannot see them -- a pre-existing emission gap, so this slot must not deepen it), a Kemba host (the upkeep Cat is per Equipment attached, so a powerless one still pays), and an attach that FLIPS double strike on the host (Kor Duelist / Balan: the host's whole power doubles, which is the opposite of poor)
    EQUIP_COPY_XNAME,         // MTG_EQUIP_COPY_XNAME  let the fungible-equip-copy fold (BuildFungibleEquipClasses) class copies by BEHAVIOURAL IDENTITY instead of by CARD NAME. USER, 2026-10-02 (KittyEquipment v2, from the play viewer): *"Also, Cathar's Shield and Accorder's Shield are duplicates. This might be useful for us as well."* and *"They are identical except for the name."* VERIFIED IN THE CARD DATA, not assumed: both are `{0}` with parameters `{equip_cost_generic:3, equip_grants_vigilance:true, equip_tough_bonus:3, is_equipment:true}`, the same keywords, types and subtypes -- byte-identical on every field the engine reads, differing only in the name string and in oracle prose. The existing class signature opens with `"E|" + a0.card_name`, so the two land in DIFFERENT classes and their cross-name symmetry is never folded: v2 plays four of each, so the deck's equip classes are five of size 4 rather than {4,8,4,4}. On a 2-host board the canonical (non-increasing) count per class is C(n+2,n), so this merges 15x15 into 45 -- a 5x cut on the equip axis of the very boards that make the viewer's plan space unenumerable. Keyed on CardDefinition::behaviour_identity, which digests the whole cards.json entry MINUS name and oracle_text, so it fails toward "not interchangeable" and cannot be defeated by a param added later; a Register-built definition digests to 0 and falls back to name equality (the pre-existing behaviour). Built DEFAULT OFF and measured rather than assumed-on, for the same reason its parent MTG_EQUIP_COPY_COLLAPSE carries: the surviving representative can be a different PHYSICAL copy, so card numbers and therefore play digests may move on a board that is merely isomorphic
    PD_ALL_M2,                // MTG_PD_ALL_M2             Prevent Damage: EVERY hand cast goes to the searched SECOND main (USER 2026-09-29: "do everything in the second main with this deck so creatures can attack even if we choose to sacrifice them"). Main 1 keeps activations (Vito's team lifelink, Dina's sac-pump, Pyrohemia) and the land drop. Unlike MTG_PD_SECOND_MAIN (which ADDED a searched m2 on top of a full m1, 1.6x) this MOVES the cast search. Measurement lever, default OFF
    PD_QUAKE_TWO_TURN,        // MTG_PD_QUAKE_TWO_TURN     Prevent Damage (inside MTG_PD_QUAKE_NO_OVERKILL): past the survival ceiling the MAX X is also kept when a SECOND Rolling Earthquake in hand is lethal next turn at next turn's max X (today's mana + one land drop if a land is in hand) -- USER 2026-09-29: "allow the max quake if it is lethal or we have another in hand that will be lethal next turn". Measurement lever, default OFF
    SOLDIERS_ORDER,           // MTG_SOLDIERS_ORDER        Soldiers PROPOSED total cast order (USER question, default OFF)
    SOLDIERS_BUCKET_DISCARD,  // MTG_SOLDIERS_BUCKET_DISCARD Soldiers authored cleanup-discard buckets (=0 -> generic max-MV)
    BRUNA_ORDER,              // MTG_BRUNA_ORDER           Bruna PROPOSED cast order (USER question, default OFF)
    BRUNA_BUCKET_DISCARD,     // MTG_BRUNA_BUCKET_DISCARD  Bruna authored cleanup-discard buckets (=0 -> generic max-MV)
    AURA_HOST_BRANCH,         // MTG_AURA_HOST_BRANCH      PROOF CONTROL ARM: the search keeps every creature-Aura host as its own plan instead of the shared host ranking (AuraPlanHostKey). Default OFF
    ROLLOUT_AURA_SWAP,        // MTG_ROLLOUT_AURA_SWAP     a future-turn horizon-leaf plan pins the damage-max Arcanum Wings combat swap. Default ON since 2026-10-06 (the fd-diverge it exposed was the BP-NODE numbering defect, fixed by MTG_BP_NODE_SHADOW); =0 disables
    PD_M2_ROOT_ONLY,          // MTG_PD_M2_ROOT_ONLY       Prevent Damage (with MTG_PD_ALL_M2): the main-phase split applies at the ROOT turn only; projected future turns keep every cast in main 1 (PhaseFilterRootTurnOnly). Diagnostic arm for the ALL_M2 regression, default OFF
    CANON_CONT_NOACTS,        // MTG_CANON_CONT_NOACTS    an UNBRANCHED canon continuation (BpUnbranchedCanon) applies its casts only, not its board activations (Plan::cont_canon). Default ON
    NEEDS_TAP_ORDER,          // MTG_NEEDS_TAP_ORDER        needs-based mana-source choice: among the plain-land tiers a source is ranked FIRST by the unmet demand its tap would create (demand = hand cast costs + our board's activation pips + one expected reveal from the LIBRARY's castable pips when a dig/draw is live, net of this payment's own coloured pips; supply = untapped sources per colour, decremented as the payment taps), THEN by the flexibility ladder. Colourless stops being "spend first" the moment a {C} pip is in demand (EDF's Displacer); a needed colour is held while an unneeded one pays a generic pip. USER 2026-10-05: "needs based ... first hand and then deck for cases where we are drawing cards". Measurement lever, default OFF
    PAY_ROLLBACK,             // MTG_PAY_ROLLBACK           same-turn mana-payment ROLLBACK: when a later line is short of a colour (a real payment fails, or a dig's find has the mana but not the colour), re-pay ONE earlier pip of this turn from a still-untapped side-effect-free source that cannot make that colour, freeing the source that can (src/ai/PayRollback.h; docs/design/mana-payment-rollback.md). At most one rescue per turn, dominant swaps only, lockstep on the state. USER 2026-10-05: "a backup which attempts to bridge this consistency gap". Measurement lever, default OFF
    ATTACK_BODY_TAP_ORDER,    // MTG_ATTACK_BODY_TAP_ORDER  on a pre-combat main with an eligible attacker (PlanTraits::attack_matters), equal-rank mana CREATURES tap lowest-attack-power first (DecisionProvider::ManaSourceHoldValue), ahead of the demand-surplus colour tiebreak. USER 2026-10-05 (Mirrorwing v2 s51_gi50 T4: Libation X=5 tapped two 1-power Mystics over two 0-power Hierarchs, 19 damage vs a 21 kill). Default ON in human play, OFF autonomously (held-out A/B owed)
    BP_NODE_SHADOW,           // MTG_BP_NODE_SHADOW         a BP-NODE child of a BASE plan is numbered as a from-scratch (variant) apply counts -- every class-on occurrence plus site 9 -- not the base's own uncounted bp_seen (docs/design/site9-continuation-index-mismatch.md). Correctness fix, default ON; =0 reverts
    SOLVE_COMBAT_SWAP,        // MTG_SOLVE_COMBAT_SWAP      the greedy Solve (d0 runner + rollout leaves) drops a PRE-COMBAT Aura swap that brings in a host-tapping Aura (Colossification) onto a would-be attacker when the combat swap stays payable without tapping an attacker -- the in-combat swap brings it in for free (CR 506.4) -- and the d0 runner pins that combat swap like the rollout leaf does. USER 2026-10-06 ("I would fix depth-0 for colossification"). Default ON; =0 reverts
    COST_REFRAME,             // MTG_COST_REFRAME           the deck-agnostic over-optimistic cost relaxation in EnumeratePlans (see CostReframeEnabled). Default OFF. A slot so scripts/analyze_deck.py's cost diagnostic runs BOTH arms in ONE pooled batch at the deck's play settings
    SNOW_SCRY,                // MTG_SNOW_SCRY              Snow's own scry keep (SnowProvider::ScryKeepOnTop) anchored on the USER's discard buckets, instead of the generic "bottom lands once two are in play". USER 2026-10-06. Measurement lever
    SNOW_SCRY_OUTLOOK,        // MTG_SNOW_SCRY_OUTLOOK      ...its OUTLOOK variant: land quota 1 and bottom a nonland not castable within a turn of next turn. Implies MTG_SNOW_SCRY. Measurement lever
    SCRY_SEARCH_TRIGGERED,    // MTG_SCRY_SEARCH_TRIGGERED  the searched land-ETB scry axis also reaches a scry the land drop TRIGGERS (Marit Lage's Slumber). USER 2026-10-06: "Scry should be optionally searched". Measurement lever
    SNOW_SCRY_USER,           // MTG_SNOW_SCRY_USER         the USER's Snow scry rule (2026-10-06), firm calls PRUNE the searched scry (SnowProvider::ScryVerdict). Default ON since 2026-10-06 (ADOPTED); =0 restores the generic rule
    SNOW_SCRY_ACCEL_COUNT,    // MTG_SNOW_SCRY_ACCEL_COUNT  USER rule: an accelerant is kept whenever a snow-count payoff is in play (Slumber, a Treefolk) -- it grows the count even when its mana buys nothing -- instead of only with a mana sink. USER 2026-10-06: "let's try both and choose the best option". Default ON since 2026-10-06 (ADOPTED, won the pick)
    BOUNCE_UNTAPPED_FIRST,    // MTG_BOUNCE_UNTAPPED_FIRST  a Karoo returns a land that RE-ENTERS UNTAPPED ahead of an already-tapped one (tapped becomes the tie-break). USER 2026-10-06: "Generally the Orchard is better to bounce because the monastery will come into play tapped again." Measurement lever
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
        "MTG_M2_CAP1",
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
        "MTG_SNOW_ORDER_WATCHER",
        "MTG_SNOW_ACT_AUGUR_FIRST",
        "MTG_BP_CONDEMN_SAME_TURN",
        "MTG_SNOW_CONDEMN",
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
        "MTG_FUNGUS_DEVOUR_LANDMARKS",
        "MTG_FUNGUS_DEVOUR_LETHAL",
        "MTG_RESCUE_TOTAL_GATE",
        "MTG_M2_EMPTY_FAST",
        "MTG_FOLD_SEARCH_ODO",
        "MTG_ACT_TAP_RESERVE",
        "MTG_RESCUE_TAP_SOURCE",
        "MTG_SAC_FODDER_SAME_LINE",
        "MTG_SAC_FODDER_VALUE_OUTLET",
        "MTG_SAC_FODDER_TAP_MAKER",
        "MTG_ENTER_WATCHER_GATE",
        "MTG_FUNGUS_SHRINK_SAC_PAYER",
        "MTG_FUNGUS_SHRINK_SAC_M2",
        "MTG_SAC_DRAIN_LETHAL",
        "MTG_FS_PRE_STATE_SKIP",
        "MTG_DIG_MANA_LAST",
        "MTG_SPORE_POP_ALL",
        "MTG_SPORE_HOLD_WIDE",
        "MTG_SPORE_HOLD_NONE",
        "MTG_SPORE_HOLD_SECOND",
        "MTG_M2_FREE_ACTIVATION",
        "MTG_FADE_K_WINDOW",
        "MTG_BP_ETB_DIG",
        "MTG_ETB_TREASURE_SPEND",
        "MTG_PIRATES_CAST_ORDER",
        "MTG_PAYABLE_ORDER",
        "MTG_PD_SECOND_MAIN",
        "MTG_PD_DINA_LETHAL_GATE",
        "MTG_PD_DUP_LEGEND",
        "MTG_PD_SELF_LETHAL_GUARD",
        "MTG_PD_LEAF_OWN_LIFE",
        "MTG_PD_PAIN_PAY",
        "MTG_PD_BUCKET_DISCARD",
        "MTG_PD_SHED_UNPLAYED_LAND",
        "MTG_PD_WISH_RANK",
        "MTG_PD_WISH_ZENITH",
        "MTG_PD_WISH_USEFUL",
        "MTG_PD_WISH_TRIM",
        "MTG_PD_WISH_VITO_OVER_DINA",
        "MTG_PD_ZENITH_SKIP_DINA",
        "MTG_BOTTOM_NAME_DEDUPE",
        "MTG_PD_QUAKE_NO_UNDERSHOOT",
        "MTG_PD_QUAKE_NO_OVERKILL",
        "MTG_PD_QUAKE_TOP_X",
        "MTG_SL_SECOND_MAIN",
        "MTG_ACT_LINE_HOLD",
        "MTG_SNOW_LOOK_COLOR",
        "MTG_ACT_HOLD_OUTER",
        "MTG_FOLD_ODO_SKIP",
        "MTG_SOLVE_CHARGE",
        "MTG_BP_NOBP_SITE9",
        "MTG_SAC_VICTIM_DOOMED",
        "MTG_SAC_VICTIM_ENGINE",
        "MTG_SAC_POOL_TURN_COLOR",
        "MTG_SAC_PAY_PLAN_FODDER",
        "MTG_FREE_EQUIP_MANDATORY",
        "MTG_METALCRAFT_EQUIP_HOIST",
        "MTG_FREE_CAST_HOIST",
        "MTG_EQUIP_COPY_SKIP",
        "MTG_ZERO_POWER_EQUIP_FREE_ONLY",
        "MTG_EQUIP_COPY_XNAME",
        "MTG_PD_ALL_M2",
        "MTG_PD_QUAKE_TWO_TURN",
        "MTG_SOLDIERS_ORDER",
        "MTG_SOLDIERS_BUCKET_DISCARD",
        "MTG_BRUNA_ORDER",
        "MTG_BRUNA_BUCKET_DISCARD",
        "MTG_AURA_HOST_BRANCH",
        "MTG_ROLLOUT_AURA_SWAP",
        "MTG_PD_M2_ROOT_ONLY",
        "MTG_CANON_CONT_NOACTS",
        "MTG_NEEDS_TAP_ORDER",
        "MTG_PAY_ROLLBACK",
        "MTG_ATTACK_BODY_TAP_ORDER",
        "MTG_BP_NODE_SHADOW",
        "MTG_SOLVE_COMBAT_SWAP",
        "MTG_COST_REFRAME",
        "MTG_SNOW_SCRY",
        "MTG_SNOW_SCRY_OUTLOOK",
        "MTG_SCRY_SEARCH_TRIGGERED",
        "MTG_SNOW_SCRY_USER",
        "MTG_SNOW_SCRY_ACCEL_COUNT",
        "MTG_BOUNCE_UNTAPPED_FIRST",
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
