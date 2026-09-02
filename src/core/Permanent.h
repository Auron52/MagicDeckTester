#pragma once
#include "Card.h"
#include <vector>

struct Counter
{
    enum class Type { PlusOnePlusOne, MinusOneMinusOne, Loyalty, Poison, Depletion };
    Type type;
    int count = 1;
};

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
};

struct Permanent
{
    Card card;
    int  controller_index          = 0;   // index into GameState::players
    int  owner_index               = 0;   // index into GameState::players
    bool tapped               = false;
    int       damage               = 0;    // reset each cleanup step
    // Accumulated "when this creature dies this turn" damage owed to its controller from delayed
    // triggers (Searing Blood: 3 per copy). Two Searing Bloods on one creature leave 6 pending; it
    // all fires when the creature dies (CR 603.7). Reset each cleanup with damage.
    int       pending_death_trigger = 0;
    std::vector<Counter> counters;
    bool      entered_this_turn    = false;  // summoning sickness tracker
    // Summoning sickness tracks how long you have CONTROLLED a permanent, not how long it has been
    // on the battlefield (CR 302.6), so gaining control resets it independently of entered_this_turn.
    // Needed because the two can disagree: a scheduled opponent spawn is created with
    // entered_this_turn = false ("treated as already present"), so a stolen one would otherwise be
    // able to attack THE SAME TURN it was stolen. USER, 2026-08-16: "note that they do have
    // summoning sickness when they are stolen". Cleared with entered_this_turn at turn start; haste
    // still overrides, exactly as for a freshly-cast creature.
    bool      gained_control_this_turn = false;
    Permanent* attached_to         = nullptr;
    // Aura attachment (Bogles / hexproof-auras). For an Aura enchantment on the battlefield,
    // this is the card.m_number of the creature it enchants (0 = not an Aura / unattached).
    // A STABLE per-copy id is used deliberately instead of `attached_to` above: the battlefield
    // is a std::vector deep-copied per search node and reallocated on push_back, so a raw
    // Permanent* would dangle -- which is why `attached_to` is a dead stub. The aura's power/
    // toughness/lifelink grant is applied to the creature with this m_number at the combat sites
    // (AuraBonusFor / CreatureHasLifelink, SpellEffects.h). Copied with the permanent.
    int       aura_attached_to     = 0;
    bool      marked_for_destruction = false;
    int       temp_power_bonus     = 0;    // accumulated "until end of turn" boosts; reset each cleanup
    int       temp_tough_bonus     = 0;
    int       charge_counters      = 0;    // Aether Vial charge counter count
    int       verse_counters       = 0;    // Aria of Flame verse counter count
    int       storage_counters     = 0;    // storage-counter land battery (Dwarven Hold, Mercadian
                                           // Bazaar): accumulated over idle turns; an untapped charged
                                           // storage land taps to burst {R} x storage_counters (zeroing
                                           // them), NOT sacrificed. See CardParams::storage_land.
    bool      storage_hold_this_turn = false; // #6 human-play tap-vs-charge: when the non-clairvoyant
                                           // human elects to HOLD a charged storage land this turn (build
                                           // the battery rather than burst now), this flags it not-live
                                           // (StorageSourceLive returns false) so it is never tapped for
                                           // mana -> stays untapped -> charges +1 at end of turn. Set only
                                           // via the human StorageHoldChooser; reset each UntapStep. Never
                                           // set autonomously -> byte-identical for the search/rollout.
    uint8_t   garth_chosen_mask    = 0;    // Garth One-Eye: bit i = name i already chosen by THIS
                                           // permanent object (per WotC ruling; a second/returned
                                           // Garth starts fresh). Bit order in CardParams::
                                           // garth_copy_ability's comment.
    int       loyalty              = 0;    // Planeswalker loyalty (source of truth; the generic
                                           // Counter{Loyalty} entry is a display mirror for the
                                           // viewer badge). Set from loyalty_start on entry; only
                                           // changes via our own activations (the passive opponent
                                           // never attacks or damages a walker).
    bool      loyalty_activated_this_turn = false; // one loyalty ability per walker per turn
                                           // (CR 606.3); reset at BOTH untap sites (lockstep).
    int       equipped_to          = 0;    // Equipment (Lightning Greaves): card.m_number of the
                                           // creature this Equipment is attached to; 0 = unattached.
                                           // Mirrors aura_attached_to, but an Equipment merely FALLS
                                           // OFF when its host leaves (CR 301.5c) -- the executor SBA
                                           // zeroes it; it is never sacrificed for a missing host.
    bool      colored_cast_lifegain_used_this_turn = false; // Ancient Cornucopia's once-each-turn
                                           // colored-cast lifegain fired already this turn. Set in
                                           // FireOnCastTriggers (both cast paths), reset at BOTH untap
                                           // sites (GameEngine::UntapStep + TurnSolver's per-turn reset)
                                           // -- rollout/executor lockstep or [fd-diverge].
    int       age_counters         = 0;    // Cumulative upkeep (Varchild's War-Riders; CardParams::
                                           // cumulative_upkeep_opp_token): one added at each of the
                                           // controller's upkeeps, and the upkeep cost is paid once per
                                           // counter. Only read for a permanent whose card sets a
                                           // cumulative-upkeep param, so it is inert (never inspected)
                                           // for every other deck -> byte-identical.
    bool      temp_haste           = false; // "gains haste until end of turn" (Expedite, incl. its
                                            // Zada/Mirrorwing copies). Read by CanAttackFull AND
                                            // CanTapNow (haste lifts the {T} restriction too, CR
                                            // 302.6 -- a hasted fresh dork may tap for mana). Reset
                                            // at BOTH cleanup sites (GameEngine::CleanupStep +
                                            // TurnSolver::SimulateEndAndStartNextTurn) in lockstep;
                                            // folded into the sim key. Never set outside the
                                            // grants_temp_haste payload -> byte-identical elsewhere.
    bool      exile_at_end         = false; // Twinflame token: "exile those tokens at the beginning
                                            // of the next end step." Swept (battlefield -> exile) at
                                            // BOTH end-of-turn sites in lockstep; folded into the
                                            // sim key. Never set outside token_copy_of_target ->
                                            // byte-identical elsewhere.
    // "As this permanent enters, choose a creature type" (Urza's Incubator). The chosen type is
    // carried here as an INTERNED SUBTYPE ID (SubtypeRegistry), fixed at ETB and never changed, so
    // the card is generic -- the type is a property of the DECK it is played in, not baked into
    // cards.json. Chosen by the shared DominantCreatureSubtypeId at the universal enter cascade,
    // identically in the executor and the rollout. 0 = kNone = this permanent chooses nothing,
    // which is every card but the Incubator -> byte-identical everywhere else.
    uint16_t  chosen_subtype_id     = 0;
    bool      is_animated          = false; // land animated as a creature (e.g. Mutavault); reset each cleanup
    bool      is_token             = false; // created by a token-making effect (CreateToken). Lathliss's
                                            // "nontoken Dragon" gate reads this so a created 5/5 Dragon
                                            // token re-pings Scourge but never re-triggers Lathliss
                                            // (loop-safe). Set true in every CreateToken path.
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
