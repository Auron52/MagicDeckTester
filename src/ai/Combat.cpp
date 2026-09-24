// Unified combat core -- see Combat.h for what is shared and what deliberately is not.
#include "../core/EnvFlags.h"
#include "Combat.h"
#include "../cards/CardDatabase.h"
#include "../core/GameLogger.h"
#include "../core/SpellEffects.h"

// LOWER BOUND on the combat damage `p` would deal if it attacked right now, mirroring the base_pw
// build in ResolveCombatDamage below. Two terms are deliberately omitted, BOTH of which only ever
// ADD: the exalted bonus (it depends on the final attacker count, which is what we are still
// deciding) and the Jitte rider. So `== 0` here is the conservative reading "this creature would
// deal no damage"; it can never claim 0 for a creature that would actually connect.
//
// Its one caller is the reference-replay attacker pin. Nothing in autonomous play or the rollouts
// reaches it.
static int AttackerDamageLowerBound(const GameState& state, const Permanent& p, int active)
{
    const bool animated = p.is_animated;
    auto [lord_pb, lord_tb] = ComputeLordBonus(p.card, state, active, animated, &p);
    (void)lord_tb;
    int base_pw = p.EffectivePower() + lord_pb;
    if (const CardDefinition* adef = CardDatabase::Instance().LookupCached(p.card))
    {
        if (animated) { base_pw += adef->params.animate_power; }
        base_pw += DynamicBasePower(*adef, state, active);
    }
    base_pw += AuraBonusFor(p, state).first;
    base_pw += EquipBonusFor(p, state).first;
    return base_pw;
}

std::vector<int> DeclareAttackerIndices(const GameState& state)
{
    std::vector<int> atk_idx;
    const DecisionProvider& provider = ResolveProvider(state);
    const int active = state.active_player_index;
    // Reference-replay attacker pin (--force-attackers; nulled by RevealLogPause -> real
    // declaration only). The recorded game's attack set overrides the willingness heuristic
    // (AttackWith) but never legality (CanAttackFull): declare exactly the eligible recorded
    // names, consuming the multiset so duplicate names pin the right number of copies. Player 0
    // only -- the human's deck is always player 0 under --claude-play, and the recording says
    // nothing about opponent combats.
    //
    // THE PIN IS A SUBSET, NOT THE SET, and treating it as the set is a bug that cost two Fungus
    // references (fixed 2026-09-20). test/viewer_protocol_check.py builds the pin by parsing the
    // play-viewer's combat text ("attacked: A (2), B (3) - 5 to opponent"), and that text is
    // assembled from attacker_descs, which is guarded on `power > 0` further down this very
    // function. A 0-POWER ATTACKER THEREFORE LEAVES NO TRACE IN THE RECORDING -- so its absence
    // from the pin carries no information at all, and forbidding its attack turns a display
    // filter into a play decision.
    //
    // It bit exactly where you would predict: Fungus runs Beastmaster Ascension
    // (quest_counter_per_attacker 1, +5/+5 at 7 counters) alongside Utopia Mycon, a 0/2 whose mana
    // ability costs a sacrifice rather than a tap -- so attacking with it is free, and each swing
    // is a counter. The recording's turn-4 text listed only the two Thallids that dealt damage,
    // the pin declared only those two, the Ascension banked 2 counters instead of 4, and the
    // anthem came online a turn late (claude_s1_gi0 win_turn 5 -> 6, claude_s2_gi1 6 -> 7). The
    // recordings were right -- their own boards show quest4 -- and so was the engine: replaying
    // either game with the pin removed reproduces the recorded win turn exactly.
    //
    // So: pinned names are declared as before, and a creature the pin does NOT name falls through
    // to the live willingness heuristic ONLY IF it would deal no damage, which is precisely the
    // class the recording cannot express. A creature that would connect is still governed by the
    // pin, which is what keeps the case the pin was built for intact (FiveColour s9_gi8: a changed
    // tap order sent a 1/2 Deathrite Shaman in on T4 and tapped a source the recorded post-combat
    // line needed -- power 1, so the rule below does not reach it and it stays pinned out).
    if (g_play_attackers_chooser && active == 0)
    {
        if (const std::vector<std::string>* pin = (*g_play_attackers_chooser)(state.turn_number))
        {
            std::vector<std::string> want = *pin;
            for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
            {
                const Permanent& p = state.battlefield[i];
                if (p.controller_index != active) { continue; }
                if (!CanAttackFull(p, state.battlefield, active)) { continue; }
                auto it = std::find(want.begin(), want.end(), p.card.m_name.str());
                if (it != want.end()) { want.erase(it); atk_idx.push_back(i); continue; }
                // Invisible to the recording (see above): let the heuristic decide, exactly as it
                // would on an unpinned turn. The bound is conservative -- it never reports 0 for a
                // creature that would actually connect -- so this can only ever re-admit an
                // attacker the recorded text had no way to mention.
                if (AttackerDamageLowerBound(state, p, active) <= 0 && provider.AttackWith(state, p))
                { atk_idx.push_back(i); }
            }
            return atk_idx;
        }
    }
    // Gather the board's haste sources ONCE (usually none) instead of letting each creature's
    // CanAttackFull re-walk the whole battlefield for them. Same prefilter ResolveCombatDamage
    // applies to its lord scans below; byte-identical, and it takes this loop from O(N^2) to O(N)
    // in board size. That matters because the creatures which reach the haste scans at all are
    // exactly the ones that ENTERED THIS TURN, and a token deck enters them in bulk -- a Mycoloth
    // devour under Doubling Season can add dozens at once, and this function is re-run for every
    // subset the solver scores.
    const HasteSources hs = GatherHasteSources(state.battlefield, active);
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != active) { continue; }
        if (!CanAttackFull(p, state.battlefield, active, &hs)) { continue; }
        if (!provider.AttackWith(state, p)) { continue; }
        atk_idx.push_back(i);
    }
    return atk_idx;
}

CombatDamageResult ResolveCombatDamage(GameState& state, const std::vector<int>& atk_idx,
                                       int exalted_bonus, bool collect_descs)
{
    CombatDamageResult out;
    const int active  = state.active_player_index;
    const int opp_idx = 1 - active;

    std::vector<const Permanent*> attackers;
    attackers.reserve(atk_idx.size());
    std::vector<int> damaging_idx;   // attackers that dealt >0 damage (Goblin Lackey cheat)
    // ...and HOW MUCH each of them dealt, aligned index-for-index with damaging_idx. Shroofus
    // Sproutsire's trigger creates "that many" tokens, so the amount is the payload, not just the
    // fact of connecting -- and it must be the post-lord / post-anthem / post-Jitte `power` below,
    // which is what actually left the opponent's life total.
    std::vector<int> damaging_pw;
    // Lifelink gains, ONE ENTRY PER LIFELINKING ATTACKER, applied AFTER the damage loop. Combat
    // damage is simultaneous (CR 510.2) and lifelink's gain is part of that damage event
    // (CR 702.15b), so nothing it causes -- Serra Ascendant's 30-life +5/+5 switching on, an
    // Archangel of Thune team counter -- may change THIS step's damage; applying the gain inside
    // the loop let a later attacker's ComputeLordBonus read the post-gain life (attacker-order
    // dependent, and the search's projection never modelled it). Each attacker's gain stays its
    // own life-gain EVENT (CR 119.10: two lifelink attackers = two Pridemate triggers).
    std::vector<int> pending_lifelink;

    // Pre-filter the active player's lord permanents ONCE (usually none), so the per-attacker
    // ComputeLordBonus / HasDoubleStrikeFromLords below iterate that tiny list instead of each
    // re-scanning the whole battlefield. Byte-identical: same permanents, same per-creature logic.
    std::vector<int> lord_idx, ds_idx;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& q = state.battlefield[i];
        if (q.controller_index != active) { continue; }
        const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
        if (!qd) { continue; }
        if (IsLordPermanent(*qd)) { lord_idx.push_back(i); }   // template lords + dual-role (Archdruid)
        if (qd->params.grants_double_strike)      { ds_idx.push_back(i); }
    }

    for (int idx : atk_idx)
    {
        Permanent& p = state.battlefield[idx];
        const bool animated = p.is_animated;
        auto [lord_pb, lord_tb] = ComputeLordBonus(p.card, state, active, animated, &p, &lord_idx);
        (void)lord_tb;
        const bool ds = (animated
            ? HasDoubleStrikeFromLords(p.card, state.battlefield, active, true, &ds_idx)
            : (p.card.HasKeyword(Keyword::DoubleStrike)
               || HasDoubleStrikeFromLords(p.card, state.battlefield, active, false, &ds_idx)))
            || HasDoubleStrikeFromEquipment(p, state);   // Kor Duelist / Balan (KittyEquipment)
        int base_pw = p.EffectivePower() + lord_pb + exalted_bonus;
        const CardDefinition* adef = CardDatabase::Instance().LookupCached(p.card);
        if (adef)
        {
            if (animated) { base_pw += adef->params.animate_power; }
            base_pw += DynamicBasePower(*adef, state, active);   // Adeline: power = creature count
        }
        base_pw += AuraBonusFor(p, state).first;                 // Bogles: attached auras + Kor self-buff
        base_pw += EquipBonusFor(p, state).first;                // KittyEquipment: attached equipment
        // Umezawa's Jitte: spend charge counters on +2/+2 (greedy default / provider / human
        // side-channel), earn 2 per damage event -- the shared closed form (JitteDamageMath) the
        // two TurnSolver projections also use, so search and execution stay lockstep.
        int power = base_pw * (ds ? 2 : 1);
        const int jitte_bf = FindAttachedChargeEquip(state, p);
        if (jitte_bf >= 0)
        {
            Permanent& je = state.battlefield[jitte_bf];
            const CardDefinition* jd = CardDatabase::Instance().LookupCached(je.card);
            int req = ResolveProvider(state).JitteSpendCount(state, je.charge_counters);
            if (g_play_jitte_chooser)   // nulled by RevealLogPause -> real combat only
            {
                int r = (*g_play_jitte_chooser)(state, active, atk_idx, je.charge_counters);
                if (r >= 0) { req = r; }
            }
            // HUMAN play with no recorded answer: BANK the counters (spend 0). USER 2026-08-27:
            // no dialog on attack -- the human's pumps are explicit main-phase JitteModeAbility
            // activations (until-EOT, so they carry through combat), and an auto-greedy here
            // would spend the very counters a main-phase-banking line is saving. Replays with a
            // recorded --jitte answer take the chooser branch above; autonomous play (env unset)
            // and search rollouts (HumanPlaySuppress) keep the greedy default -- byte-identical.
            else if (HumanPlayActive()) { req = 0; }
            const auto [jdmg, jafter] = JitteDamageMath(
                base_pw, ds, je.charge_counters, jd->params.charge_pump_power, req);
            power             = jdmg;
            je.charge_counters = jafter;
        }
        state.players[opp_idx].life -= power;
        out.total_damage += power;
        if (power > 0)
        {
            state.opponent_lost_life_this_turn = true;
            damaging_idx.push_back(idx);   // Goblin Lackey: dealt combat damage to a player
            damaging_pw.push_back(power);  // Shroofus Sproutsire: "create THAT MANY" tokens
            // Maelstrom Archangel: connecting banks one free cast for the post-combat main
            // (user-approved banking model; see GameState::free_casts_available). Shared combat
            // core -> executor and rollout bank identically.
            if (adef && adef->params.combat_damage_free_cast) { ++state.free_casts_available; }
            // Lifelink (modeled): combat damage also gains the controller that much life. Inert vs
            // the passive opponent's clock, tracked for life-total decks.
            if (CreatureHasLifelink(p, state)) { pending_lifelink.push_back(power); }
        }
        if (collect_descs && power > 0)
        { out.attacker_descs.push_back(p.card.m_name.str() + " (" + std::to_string(power) + ")"); }
        if (!p.card.HasKeyword(Keyword::Vigilance)) { p.tapped = true; }
        attackers.push_back(&p);
    }

    // Lifelink (deferred, see pending_lifelink): one GainLife per lifelinking attacker, in attack
    // order. GainLife's watchers only add counters -- they never add or remove a permanent -- so
    // the `attackers` pointers consumed below stay valid.
    for (int amt : pending_lifelink) { GainLife(state, active, amt); }

    // Attack triggers (e.g. Leeching Sliver: each attacking Sliver costs the opponent 1 life).
    // Life LOSS, not combat damage: it still marks the lost-life flag (which drives spectacle), and
    // is folded into the total only for the attack log.
    out.trigger_life_loss = CountAttackTriggerLifeLoss(state.battlefield, active, attackers);
    if (out.trigger_life_loss > 0)
    {
        state.players[opp_idx].life -= out.trigger_life_loss;
        out.total_damage += out.trigger_life_loss;
        state.opponent_lost_life_this_turn = true;
    }

    // Utvara Hellkite: per attacking Dragon, create a 6/6 Dragon token (untapped, summoning-sick;
    // NOT added to this combat). Each token entering fires FireEtbWatchers (Scourge ping / Lathliss
    // token) via CreateToken. `attackers` still holds the pre-token attacker pointers, which
    // FireUtvaraAttackTokens reads before any CreateToken.
    FireUtvaraAttackTokens(state, active, attackers);

    // Goblin Lackey: each attacker that dealt combat damage to the player may put a matching Goblin
    // permanent from hand onto the battlefield (shared enter cascade). Fired AFTER Utvara so the
    // pre-token attacker pointers above are already consumed.
    FireCombatDamageCheatIntoPlay(state, active, damaging_idx);

    // Shroofus Sproutsire: per connecting Saproling, create that many 1/1 green Saprolings. Fired
    // after Utvara and Lackey for the same reason they are ordered as they are -- the pre-token
    // attacker pointers above are already consumed, and this helper re-reads the battlefield by
    // INDEX (bounds-checked) rather than holding references across CreateToken. Every preceding
    // hook only appends, so damaging_idx stays valid.
    FireCombatDamageTokens(state, active, damaging_idx, damaging_pw);

    // Neheb, the Worthy: "Whenever Neheb deals combat damage to a player, each player discards a
    // card." Fired here, after damage, off the same damaging_idx list Goblin Lackey uses -- so it
    // is once per connecting copy, in the shared combat core (executor + rollout lockstep). Only
    // OUR half is modelled: the passive opponent never casts and no decision reads their hand
    // (disclosed deferral D10). The shed card is the provider's cleanup-discard pick, and the
    // discard is what can switch Neheb's own hand-size anthem ON -- but only for a LATER turn's
    // combat, since this fires after damage has already been dealt.
    for (int idx : damaging_idx)
    {
        if (idx < 0 || idx >= static_cast<int>(state.battlefield.size())) { continue; }
        const CardDefinition* nd = CardDatabase::Instance().LookupCached(state.battlefield[idx].card);
        if (!nd || nd->params.combat_damage_each_discards <= 0) { continue; }
        Player& ap = state.players[active];
        for (int k = 0; k < nd->params.combat_damage_each_discards && !ap.hand.empty(); ++k)
        {
            const int hi = ChooseNonCleanupDiscardIndex(state, active,    // surfaced as `discard`
                                                        state.battlefield[idx].card.m_name.str());
            if (hi < 0) { break; }
            ap.graveyard.push_back(ap.hand[static_cast<std::size_t>(hi)]);
            ap.hand.erase(ap.hand.begin() + hi);
        }
    }
    return out;
}
