#pragma once
#include "Card.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

struct Counter
{
    enum class Type { PlusOnePlusOne, MinusOneMinusOne, Loyalty, Poison, Depletion };
    Type type;
    int count = 1;
};

// ---------------------------------------------------------------------------------------------
// CounterList -- a fixed-capacity INLINE replacement for the std::vector<Counter> that used to be
// Permanent::counters.
//
// WHY IT EXISTS. That vector was the ONE non-trivially-copyable member of Permanent, and that
// single fact set the cost of the whole engine's hot path. `std::vector<Permanent>` cannot memmove
// a non-trivially-copyable element type, so every erase became a member-wise move-assignment loop
// over 296-byte objects and every reallocation became an element-by-element relocate. Measured on
// the ten worst candidate-B keep cells (perf, 321,972 samples, 2026-09-25):
//
//     25.94% self   std::vector<Permanent>::_M_erase
//     17.61% incl   std::__copy_move_a2<true, Permanent*, Permanent*>   (the erase shift)
//      5.89% incl   Permanent::operator=(Permanent&&)
//      4.61% incl   std::__relocate_a<Permanent*>                       (the realloc path)
//
// Card was already made trivially copyable for exactly this reason (see the InternedName note in
// Card.h: "vector<Card> copy/erase are memcpy/memmove"); Permanent never got the same treatment.
//
// SEQUENCE SEMANTICS ARE PRESERVED EXACTLY -- insertion order is kept and duplicate entries of the
// same Type are NOT merged. That matters: AddPlusCounters merges deliberately, but seven other
// sites push a raw second PlusOnePlusOne entry, and .size() is read by BuildSimKey, the dominance
// signature and several dedup keys. A per-Type slot array would have silently merged those and
// moved every one of those keys. This is a storage change, not a behaviour change.
//
// THE CAPACITY IS THE ONE RISK, so it is instrumented rather than assumed: an overflowing push_back
// folds the new counters into the first entry of the same Type (which preserves the TOTAL, the only
// thing EffectivePower/EffectiveToughness read) and bumps g_counter_overflows. That counter is
// asserted zero by the test gate, so a cap that is ever too small shows up as a test failure rather
// than as a wrong board.
inline std::atomic<std::uint64_t> g_counter_overflows{0};

struct CounterList
{
    // Counter::Type has five values. Six slots leaves room for the duplicate-PlusOnePlusOne entries
    // the raw push sites create without paying for a seventh; see g_counter_overflows above.
    static constexpr int kCap = 6;

    using value_type     = Counter;
    using iterator       = Counter*;
    using const_iterator = const Counter*;

    Counter      m_e[kCap] = {};
    std::int32_t m_n       = 0;

    iterator       begin()        { return m_e; }
    iterator       end()          { return m_e + m_n; }
    const_iterator begin()  const { return m_e; }
    const_iterator end()    const { return m_e + m_n; }
    std::size_t    size()   const { return static_cast<std::size_t>(m_n); }
    bool           empty()  const { return m_n == 0; }
    void           clear()        { m_n = 0; }

    Counter&       operator[](std::size_t i)       { return m_e[i]; }
    const Counter& operator[](std::size_t i) const { return m_e[i]; }

    void push_back(const Counter& c)
    {
        if (m_n < kCap) { m_e[m_n++] = c; return; }
        g_counter_overflows.fetch_add(1, std::memory_order_relaxed);
        for (std::int32_t i = 0; i < m_n; ++i)
        { if (m_e[i].type == c.type) { m_e[i].count += c.count; return; } }
        m_e[kCap - 1].count += c.count;   // last resort: never drop a counter on the floor
    }

    // The remove_if/erase idiom at the decrement site passes a suffix [first, end()).
    iterator erase(iterator first, iterator last)
    {
        const std::ptrdiff_t gap = last - first;
        for (iterator s = last, d = first; s != end(); ++s, ++d) { *d = *s; }
        m_n -= static_cast<std::int32_t>(gap);
        return first;
    }
    iterator erase(iterator pos) { return erase(pos, pos + 1); }
};

// A too-small kCap must never pass QUIETLY. The merge above keeps the board legal, but it collapses
// two entries into one and so moves .size() -- which BuildSimKey, the dominance signature and four
// dedup keys read. One inline variable (C++17: exactly one instance program-wide), so every binary
// that links core -- mtg, mtg-test, mtg-analyze -- shouts on the way out if it ever happened.
namespace counterlist
{
    struct OverflowReporter
    {
        ~OverflowReporter()
        {
            const std::uint64_t n = g_counter_overflows.load(std::memory_order_relaxed);
            if (n != 0)
            {
                std::fprintf(stderr,
                             "[counterlist] OVERFLOW x%llu -- CounterList::kCap (%d) is too small; "
                             "entries were merged, so .size() moved. RAISE kCap in Permanent.h.\n",
                             static_cast<unsigned long long>(n), CounterList::kCap);
            }
        }
    };
    inline OverflowReporter g_overflow_reporter;
}

// The sub-mode of an ActivatePermAbility action. Lives in the CORE layer (not beside Action in the
// ai layer) because the shared resolver below is what both worlds call, and core must not depend
// on ai. TurnSolver's Action::AbilityMode is an alias of this.
enum class PermAbilityMode
{
    None = 0,
    TapDamage,       // {cost}, {T}: deals N damage to each opponent   (Shivan Gorge)
    TapInvestigate,  // {cost}, {T}: Investigate                        (Conservatory / Kitchen)
    TapDraw,         // {cost}, {T}: Draw a card                        (Mariposa Military Base)
    SacDraw,         // {cost}, Sacrifice this: Draw a card             (Clue Token -- no {T})
    // {cost}: Target opponent loses N life and you gain N life  (Essence Depleter)
    // NO {T} and NO sacrifice, so unlike every mode above it is REPEATABLE without limit within a
    // turn -- the activation count is bounded only by available mana. That is what makes it this
    // deck's real kill: unbounded mana converts straight into life loss with no untap required.
    Drain,
    // {cost}: Target opponent exiles the top card of their library  (Dimensional Infiltrator)
    // Same structure as Drain -- no {T}, no sacrifice, so repeatable within a turn and bounded only
    // by mana. The kill it converts unbounded mana into is a DECK-OUT rather than damage, which is
    // why it needs its own win-recognition path (opponentdeck::TakeFromTop sets opponent_decked).
    ExileTop,
    // {cost}: Put an ice counter on target permanent  (Rimefeather Owl, cost "{2}" -- {1}{S} with
    // the snow pip written generic). No {T}, no sacrifice -> repeatable, the Drain/ExileTop shape.
    // Only useful against a NON-snow permanent (every permanent the Snow deck plays is already
    // snow; the live targets are opponent spawns and a Marit Lage token), so the enumeration caps
    // the K axis by the non-snow permanent count.
    IceCounter,
    // {cost}: Another target creature gains lifelink until end of turn  (Heliod, Sun-Crowned,
    // "{1}{W}"). No {T}, no sacrifice -> repeatable within a turn, bounded only by mana. Sets the
    // target's temp_lifelink (until-EOT). In a lifegain-watcher deck each lifelink damage event is
    // a life-gain EVENT (a counter on every Pridemate/Voice, an Archangel team pump), which is the
    // whole reason to pay for it; the life itself is goldfish-inert.
    GrantLifelink,
    // Remove three spore counters from this creature: create a 1/1 green Saproling  (the Thallid
    // family -- Thallid, Thallid Shell-Dweller, Sporesower Thallid, Psychotrope Thallid, Utopia
    // Mycon; CardParams::spore_saproling_cost). NO {T} and NO sacrifice, so like Drain/ExileTop it
    // is REPEATABLE within a turn and legal on a summoning-sick body (CR 302.6 restricts only {T}
    // abilities). It is the first mode whose cost is COUNTERS rather than mana, which is why the
    // activation count K is bounded by spore_counters / cost and why it must NOT be routed through
    // SpendRepeatActivations -- that helper bails on a zero mana-value cost precisely to stop a
    // free repeatable sink from non-terminating.
    SporeSaproling,
    // {cost}: Create N tokens  (Slimefoot, the Stowaway, "{4}: Create a 1/1 green Saproling
    // creature token"; CardParams::pay_token_cost). NO {T} and NO sacrifice, so it is the
    // Drain/ExileTop/GrantLifelink shape: REPEATABLE within a turn, legal on a summoning-sick body
    // (CR 302.6 restricts only {T} abilities), and the activation count K is bounded solely by
    // available mana -- a pure mana sink.
    //
    // STRUCTURALLY UNLIKE CardParams::tap_token_cost (Sliver Hive), and the difference is the whole
    // reason this is a separate mode: that one has {T} in its cost, which would wrongly make this
    // once-per-untap AND illegal the turn Slimefoot lands. Unlike SporeSaproling the cost IS mana,
    // so it routes through SpendRepeatActivations safely (that helper bails at a zero mana value
    // precisely to stop a free repeatable sink from non-terminating; {4} has mana value 4).
    PayToken,
    // Remove a fade counter from this enchantment: create a Saproling whose P/T equal the number
    // of fade counters left on it  (Saproling Burst; CardParams::fade_saproling_cost). The
    // SporeSaproling shape -- the cost is COUNTERS, not mana, so it must NOT route through
    // SpendRepeatActivations (that helper bails at a zero mana value to stop a free repeatable
    // sink from non-terminating); it takes the counter-bounded twin SpendFadeActivations instead.
    //
    // K IS A GENUINE OPTIMUM HERE, not a "take the maximum" axis. K activations leave C-K counters,
    // so the turn yields K bodies of (C-K)/(C-K) -- total power K*(C-K), maximised near C/2 --
    // and popping everything mints a pile of 0/0s that die to the toughness SBA on the spot.
    FadeSaproling,
};

// Does this mode's cost include {T}? THE single source of truth, because three separate sites used
// to open-code `mode != SacDraw` -- and every one of them would have silently tapped Essence
// Depleter and Dimensional Infiltrator on each activation, destroying the repeatability that makes
// them win conditions at all, AND blocking activation on a summoning-sick or attacking body (both
// legal: CR 302.6 restricts {T} abilities and attacking, neither of which applies here).
inline bool PermAbilityTaps(PermAbilityMode m)
{
    return m != PermAbilityMode::SacDraw
        && m != PermAbilityMode::Drain
        && m != PermAbilityMode::ExileTop
        && m != PermAbilityMode::IceCounter
        && m != PermAbilityMode::GrantLifelink
        && m != PermAbilityMode::SporeSaproling
        && m != PermAbilityMode::PayToken
        && m != PermAbilityMode::FadeSaproling;
}

struct Permanent
{
    Card card;
    // ---- HOT BLOCK: the fields a per-permanent BOARD SCAN reads, packed into one cache line --------
    int  controller_index          = 0;   // index into GameState::players
    int  owner_index               = 0;   // index into GameState::players
    bool tapped               = false;
    // "LookupCached(card) is KNOWN to return nullptr for this permanent" -- i.e. its name is not in
    // the card DB at all, which is the ordinary case for a TOKEN (see the tokens-have-no-definition
    // note on CreateTokenOnce). Purely a SHORT-CIRCUIT for the many board walks whose first act is
    //     const CardDefinition* d = LookupCached(p.card); if (!d) { continue; }
    // -- it lets them skip the call and reach the same `continue`, so it can only ever reproduce
    // the existing behaviour.
    //
    // DEFAULTS FALSE = "not known", i.e. do the lookup, i.e. today's behaviour. That direction is
    // the whole safety argument, exactly as for GameState's ETB presence gates: a permanent created
    // by any site that does not set this flag keeps the full path, so a missed creation site costs
    // a lookup rather than dropping a trigger. Only ever set it true where the lookup has actually
    // been done and came back null.
    //
    // WHY IT IS WORTH A BOOL: the per-enter watcher walk is O(board width), boards on a Saproling
    // deck reach 200+ permanents, and a 150-game Fungus run walks 2.9 BILLION permanents that way
    // (measured 2026-09-23) -- of which all but a handful are tokens that resolve to nothing. The
    // DB lookup is already memoized down to a sentinel compare, but it is an out-of-line call; a
    // bool on the permanent we are already iterating is not.
    bool      def_absent             = false;
    bool      is_token             = false; // created by a token-making effect (CreateToken). Lathliss's
                                            // "nontoken Dragon" gate reads this so a created 5/5 Dragon
                                            // token re-pings Scourge but never re-triggers Lathliss
                                            // (loop-safe). Set true in every CreateToken path.
    bool      entered_this_turn    = false;  // summoning sickness tracker
    // Summoning sickness tracks how long you have CONTROLLED a permanent, not how long it has been
    // on the battlefield (CR 302.6), so gaining control resets it independently of entered_this_turn.
    // Needed because the two can disagree: a scheduled opponent spawn is created with
    // entered_this_turn = false ("treated as already present"), so a stolen one would otherwise be
    // able to attack THE SAME TURN it was stolen. USER, 2026-08-16: "note that they do have
    // summoning sickness when they are stolen". Cleared with entered_this_turn at turn start; haste
    // still overrides, exactly as for a freshly-cast creature.
    bool      gained_control_this_turn = false;
    bool      marked_for_destruction = false;
    bool      is_animated          = false; // land animated as a creature (e.g. Mutavault); reset each cleanup
    bool      temp_haste           = false; // "gains haste until end of turn" (Expedite, incl. its
                                            // Zada/Mirrorwing copies). Read by CanAttackFull AND
                                            // CanTapNow (haste lifts the {T} restriction too, CR
                                            // 302.6 -- a hasted fresh dork may tap for mana). Reset
                                            // at BOTH cleanup sites (GameEngine::CleanupStep +
                                            // TurnSolver::SimulateEndAndStartNextTurn) in lockstep;
                                            // folded into the sim key. Never set outside the
                                            // grants_temp_haste payload -> byte-identical elsewhere.
    bool      temp_lifelink        = false; // "gains lifelink until end of turn" (Heliod, Sun-Crowned's
                                            // {1}{W}). Read by CreatureHasLifelink. Reset at BOTH cleanup
                                            // sites in lockstep; folded into the sim key only when set.
                                            // Never set outside PermAbilityMode::GrantLifelink ->
                                            // byte-identical elsewhere.
    bool      exile_at_end         = false; // Twinflame token: "exile those tokens at the beginning
                                            // of the next end step." Swept (battlefield -> exile) at
                                            // BOTH end-of-turn sites in lockstep; folded into the
                                            // sim key. Never set outside token_copy_of_target ->
                                            // byte-identical elsewhere.
    // EATEN BY THE IN-FLIGHT PAYMENT as sac-outlet fodder (Utopia Mycon / Skirk Prospector --
    // see SacOutletPayEnabled). A payment cannot erase mid-flight (the source loops hold
    // `Permanent&` and derive the reserved-mask index from `&p - battlefield.data()`), so the
    // "tap" of a fodder creature only MARKS it and CommitPaySacSacrifices does the real
    // sacrifice on each success path -- the §2a Treasure contract exactly.
    // A DEDICATED FLAG rather than reusing `tapped`, which is what §2a does, because a Treasure
    // is never tapped by anything else and a SAPROLING IS: it attacks. Committing on `tapped`
    // would eat every attacker the moment a second-main payment ran. Always false outside a
    // payment attempt (set and cleared inside one, restored by PermPaySnap on every failure
    // path), and never set at all while the lever is off -> byte-identical.
    bool      pay_sac_eaten          = false;
    bool      echo_resolved        = false; // Echo (Mogg War Marshal, Stingscourger; CardParams::echo_cost).
                                            // "At the beginning of your upkeep, if this came under your
                                            // control since your last upkeep, sacrifice it unless you pay
                                            // its echo cost." Instead of flagging it at every enter site,
                                            // this starts false and the FIRST upkeep its controller takes
                                            // after it entered flips it true after resolving the pay-or-
                                            // sacrifice decision -- so no later upkeep re-charges echo. Only
                                            // read for a permanent whose card has a non-empty echo_cost, so
                                            // it is inert (never inspected) for every non-echo deck ->
                                            // byte-identical.
    bool      storage_hold_this_turn = false; // #6 human-play tap-vs-charge: when the non-clairvoyant
                                           // human elects to HOLD a charged storage land this turn (build
                                           // the battery rather than burst now), this flags it not-live
                                           // (StorageSourceLive returns false) so it is never tapped for
                                           // mana -> stays untapped -> charges +1 at end of turn. Set only
                                           // via the human StorageHoldChooser; reset each UntapStep. Never
                                           // set autonomously -> byte-identical for the search/rollout.
    bool      loyalty_activated_this_turn = false; // one loyalty ability per walker per turn
                                           // (CR 606.3); reset at BOTH untap sites (lockstep).
    bool      colored_cast_lifegain_used_this_turn = false; // Ancient Cornucopia's once-each-turn
                                           // colored-cast lifegain fired already this turn. Set in
                                           // FireOnCastTriggers (both cast paths), reset at BOTH untap
                                           // sites (GameEngine::UntapStep + TurnSolver's per-turn reset)
                                           // -- rollout/executor lockstep or [fd-diverge].
    // "As this permanent enters, choose a color" (Coldsteel Heart), CardParams::etb_choose_color.
    // The colour is LOCKED FOR THE PERMANENT'S LIFETIME -- it is a per-OBJECT property, which is
    // exactly why it cannot live on the CardDefinition like every other `produces`: four Coldsteel
    // Hearts are four independent choices, and a re-entering one chooses afresh (new object).
    // -1 = unchosen (a definition with the param that somehow entered off the FireOwnEtbTriggers
    // path); readers fall back to the definition's produces there, i.e. the pre-2026-09-08
    // behaviour, so an unset value degrades to the old over-permissive model rather than to an
    // illegal one. Values are Color enum ordinals. Read ONLY via EffectiveProducesFor and the
    // payer's per-permanent colour resolution -- both param-gated, so this is never inspected for
    // any other deck -> byte-identical.
    int8_t    chosen_color         = -1;
    uint8_t   garth_chosen_mask    = 0;    // Garth One-Eye: bit i = name i already chosen by THIS
                                           // permanent object (per WotC ruling; a second/returned
                                           // Garth starts fresh). Bit order in CardParams::
                                           // garth_copy_ability's comment.
    // "As this permanent enters, choose a creature type" (Urza's Incubator). The chosen type is
    // carried here as an INTERNED SUBTYPE ID (SubtypeRegistry), fixed at ETB and never changed, so
    // the card is generic -- the type is a property of the DECK it is played in, not baked into
    // cards.json. Chosen by the shared DominantCreatureSubtypeId at the universal enter cascade,
    // identically in the executor and the rollout. 0 = kNone = this permanent chooses nothing,
    // which is every card but the Incubator -> byte-identical everywhere else.
    uint16_t  chosen_subtype_id     = 0;
    int       damage               = 0;    // reset each cleanup step
    int       temp_power_bonus     = 0;    // accumulated "until end of turn" boosts; reset each cleanup
    int       temp_tough_bonus     = 0;
    int       equipped_to          = 0;    // Equipment (Lightning Greaves): card.m_number of the
                                           // creature this Equipment is attached to; 0 = unattached.
                                           // Mirrors aura_attached_to, but an Equipment merely FALLS
                                           // OFF when its host leaves (CR 301.5c) -- the executor SBA
                                           // zeroes it; it is never sacrificed for a missing host.
    // Aura attachment (Bogles / hexproof-auras). For an Aura enchantment on the battlefield,
    // this is the card.m_number of the creature it enchants (0 = not an Aura / unattached).
    // A STABLE per-copy id is used deliberately rather than a `Permanent*` back-pointer: the
    // battlefield is a std::vector deep-copied per search node and reallocated on push_back, so a
    // raw pointer would dangle. (There WAS such a stub, `Permanent* attached_to`, never read by
    // anything; removed 2026-09-25 with the cache-layout pass.) The aura's power/
    // toughness/lifelink grant is applied to the creature with this m_number at the combat sites
    // (AuraBonusFor / CreatureHasLifelink, SpellEffects.h). Copied with the permanent.
    int       aura_attached_to     = 0;
    CounterList counters;   // was std::vector<Counter>; see CounterList above for why
    // ---- COLD: per-card resources and display state; never touched by a whole-board walk -----------
    // Accumulated "when this creature dies this turn" damage owed to its controller from delayed
    // triggers (Searing Blood: 3 per copy). Two Searing Bloods on one creature leave 6 pending; it
    // all fires when the creature dies (CR 603.7). Reset each cleanup with damage.
    int       pending_death_trigger = 0;
    int       charge_counters      = 0;    // Aether Vial charge counter count
    int       verse_counters       = 0;    // Aria of Flame verse counter count
    // SAGA lore counters (CR 714). A Saga enters with one (CR 714.2a -- an as-enters replacement,
    // so chapter I fires on the turn it lands) and gains one after its controller's draw step
    // (CR 714.2b, the post-2022 timing printed on the card; pre-2022 Sagas said "at your precombat
    // main phase" -- the mtg-rules skill still quotes the OLD wording, but the printed oracle text
    // governs). Sacrificed once the final chapter has resolved (CR 714.4). Advanced by
    // AdvanceSagas() in BOTH worlds, and folded into the dominance key: a Saga on chapter I and the
    // same Saga on chapter III are DIFFERENT states, so omitting it would collide the memo.
    int       lore_counters        = 0;
    int       storage_counters     = 0;    // storage-counter land battery (Dwarven Hold, Mercadian
                                           // Bazaar): accumulated over idle turns; an untapped charged
                                           // storage land taps to burst {R} x storage_counters (zeroing
                                           // them), NOT sacrificed. See CardParams::storage_land.
    int       loyalty              = 0;    // Planeswalker loyalty (source of truth; the generic
                                           // Counter{Loyalty} entry is a display mirror for the
                                           // viewer badge). Set from loyalty_start on entry; only
                                           // changes via our own activations (the passive opponent
                                           // never attacks or damages a walker).
    int       ice_counters         = 0;    // Ice counters (Rimefeather Owl's {1}{S} activation is the
                                           // only source). Read ONLY by the snow helpers (IsSnowPermanent
                                           // under an ice_counters_are_snow source) and the
                                           // ice_counters_dont_untap untap gate -- both param-gated, so
                                           // it is inert (never inspected) for every other deck ->
                                           // byte-identical.
    int       age_counters         = 0;    // Cumulative upkeep (Varchild's War-Riders; CardParams::
                                           // cumulative_upkeep_opp_token): one added at each of the
                                           // controller's upkeeps, and the upkeep cost is paid once per
                                           // counter. Only read for a permanent whose card sets a
                                           // cumulative-upkeep param, so it is inert (never inspected)
                                           // for every other deck -> byte-identical.
    int       spore_counters       = 0;    // Spore counters (the Thallid family: CardParams::
                                           // spore_upkeep_self / spore_upkeep_each_fungus add them at
                                           // upkeep, spore_saproling_cost removes three to make a
                                           // Saproling). A DEDICATED int rather than a Counter{} entry,
                                           // deliberately: EffectivePower/EffectiveToughness iterate the
                                           // counters vector and are the hottest functions in the engine,
                                           // and this is THE deck with 40-token boards -- a Spore entry on
                                           // every Thallid would pay an extra iteration there for a value
                                           // that never changes P/T. Read ONLY under a spore_* param, so
                                           // it is inert (never inspected) for every other deck ->
                                           // byte-identical. FUTURE-DETERMINING (it decides how many
                                           // Saprolings a later turn yields), so it MUST be folded into
                                           // BuildSimKey and BoardSignature under a nonzero gate -- see
                                           // the storage-counter key-hole note in TurnSolver.cpp.
    int       fade_counters        = 0;    // Fade counters (Saproling Burst; CardParams::
                                           // fading_counters puts them on AS THE PERMANENT ENTERS,
                                           // one comes off at each of the controller's upkeeps, and
                                           // fade_saproling_cost removes one to make a token).
                                           // FADING COUNTS DOWN AND THE SACRIFICE IS ON FAILURE TO
                                           // REMOVE (CR 702.32), so Fading 7 takes SEVEN decrements
                                           // and is sacrificed on the EIGHTH upkeep. Same dedicated-
                                           // int rationale as spore_counters above (kept out of the
                                           // counters vector, which EffectivePower iterates), and
                                           // the same sim-key obligation -- it is FUTURE-DETERMINING
                                           // twice over: it decides how many activations remain AND
                                           // it IS the power and toughness of every token the
                                           // permanent has already made.
    int       created_by_number    = 0;    // Token PROVENANCE: the m_number of the permanent whose
                                           // ability created this token, or 0 for every token that
                                           // predates this field. Only Saproling Burst sets it, and
                                           // it is read at exactly two places -- the P/T refresh
                                           // when its source's fade counters change, and the
                                           // leaves-the-battlefield sweep that destroys "all tokens
                                           // created with this enchantment". Inert (never inspected)
                                           // for every other deck -> byte-identical.
    int       quest_counters       = 0;    // Quest counters (Beastmaster Ascension; CardParams::
                                           // quest_counter_per_attacker adds one per DECLARED attacker,
                                           // quest_anthem_threshold switches on the team pump at 7+).
                                           // Same dedicated-int rationale and the same byte-identity and
                                           // sim-key obligations as spore_counters above.
    // Sakashima's Protege (CardParams::enter_as_copy_of_entrant): when the enter swap replaced the
    // entering card with the copied permanent's PRINTED card (CR 706.2), this holds the PRINTED
    // name of the card that was cast ("Sakashima's Protege"). DISPLAY-ONLY: set at both worlds'
    // swap sites, read solely by the viewer's board serializer (JsonBattlefield emits it as
    // "printed") so a copy is distinguishable on the board -- without it two Protege-copies of
    // Boarding Party render as two more Boarding Parties and the player cannot find them
    // (user-reported 2026-09-03). Never read by game logic: the copy IS the copied card, and
    // per-copy identity already rides card.m_number. Empty for every non-copy permanent.
    InternedName copy_printed_name;

    int  EffectivePower()     const;
    int  EffectiveToughness() const;

    // A permanent can attack if it is an untapped creature (or animated land) without
    // Defender that is not summoning sick (CR 302.6, CR 508.1).
    // Animated permanents (is_animated) always have haste from their animation effect.
    // For lord-granted haste (e.g. Cloudshredder Sliver), use CanAttackFull() in
    // SpellEffects.h which has access to the full battlefield.
    bool CanAttack() const
    {
        if (!card.IsCreature() && !is_animated)
        {
            return false;
        }
        if (tapped)
        {
            return false;
        }
        if (card.HasKeyword(Keyword::Defender))
        {
            return false;
        }
        if (is_animated) { return true; }   // animation grants haste
        return !(entered_this_turn || gained_control_this_turn) || card.HasKeyword(Keyword::Haste);
    }

    // A permanent can be tapped for an activated ability unless it is a summoning-sick
    // creature. Non-creatures are never affected by summoning sickness (CR 302.6).
    bool CanTap() const
    {
        if (!card.IsCreature())
        {
            return true;
        }
        return !(entered_this_turn || gained_control_this_turn) || card.HasKeyword(Keyword::Haste);
    }
};

inline int Permanent::EffectivePower() const
{
    int p = card.m_power.value_or(0) + temp_power_bonus;
    for (const Counter& c : counters)
    {
        if (c.type == Counter::Type::PlusOnePlusOne)  { p += c.count; }
        if (c.type == Counter::Type::MinusOneMinusOne) { p -= c.count; }
    }
    return p;
}

inline int Permanent::EffectiveToughness() const
{
    int t = card.m_toughness.value_or(0) + temp_tough_bonus;
    for (const Counter& c : counters)
    {
        if (c.type == Counter::Type::PlusOnePlusOne)  { t += c.count; }
        if (c.type == Counter::Type::MinusOneMinusOne) { t -= c.count; }
    }
    return t;
}
