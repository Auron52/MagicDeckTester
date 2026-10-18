// Unified combat core -- see Combat.h for what is shared and what deliberately is not.
#include "../core/EnvFlags.h"
#include "Combat.h"
#include "../cards/CardDatabase.h"
#include "../core/GameLogger.h"
#include "../core/SpellEffects.h"
#include "../core/DamageEvents.h"   // dmgev::t_pay_hold (MTG_FB_HOLD_BANKED)
#include "EngineFlags.h"
#include "ManaPayment.h"           // AvailableManaPool
#include <atomic>
#include <cstdio>
#include <limits>

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
    auto [lord_pb, lord_tb] = ComputeLordBonus(p.card, state, active, p.AnimatedAllTypes(), &p);
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


// Neheb, the Worthy: "Whenever Neheb deals combat damage to a player, each player discards a card." Once
// per connecting copy in `damaging_idx`. Only OUR half is modelled: the passive opponent never casts and
// no decision reads their hand (disclosed deferral D10). The shed card is the provider's non-cleanup
// discard pick (surfaced as `discard`), and the discard is what can switch Neheb's own hand-size anthem
// ON. Shared by both damage-step shapes below (it was inline in the single-step loop's tail).
static void FireNehebCombatDamageDiscards(GameState& state, int active, const std::vector<int>& damaging_idx)
{
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
}

// Curiosity ("whenever enchanted creature deals damage to an opponent, you may draw a card") and
// Tandem Lookout's pair grant ("...draw a card"): once per combat-damage EVENT per connecting
// creature, then the Niv ping loop those draws start (OnCreatureDealtDamageToOpponent, the shared
// hook the ping itself also feeds). Last among the combat-damage triggers -- the controller orders
// them, and drawing after Neheb's discard is the order that keeps the drawn card. Nothing fires if
// combat damage already killed (the hook's OpponentHasLost guard). Executor + rollout share this.
// Re-found by m_number rather than trusting the recorded index: the hooks above only append today,
// but a creature that left (or a shifted index) must not hand its draws to a different body.
// `damaging_events` is filled iff the deck-constant gate (GameState::deck_has_draw_ping) is on, so it
// is aligned with `damaging_idx`; an unnumbered permanent -- m_number 0, a hand-built test board --
// keeps its recorded index. Shared by both damage-step shapes below.
static void FireCombatDamageDrawGrants(GameState& state, const std::vector<int>& damaging_idx,
                                       const std::vector<int>& damaging_num,
                                       const std::vector<int>& damaging_events)
{
    for (std::size_t k = 0; k < damaging_events.size(); ++k)
    {
        int src = -1;
        if (damaging_num[k] == 0) { src = damaging_idx[k]; }
        for (int i = 0; src < 0 && i < static_cast<int>(state.battlefield.size()); ++i)
        { if (state.battlefield[i].card.m_number == damaging_num[k]) { src = i; } }
        if (src >= 0) { OnCreatureDealtDamageToOpponent(state, src, damaging_events[k]); }
    }
}

// ---- CR 510.4: the TWO combat damage steps -----------------------------------------------------------
//
// "If at least one attacking or blocking creature has first strike or double strike as the combat
// damage step begins, the only creatures that assign combat damage in that step are those with first
// strike or double strike. After that step, instead of proceeding to the end of combat step, the phase
// gets a second combat damage step. The only creatures that assign combat damage in that step are the
// remaining attackers and blockers that had neither first strike nor double strike as the first combat
// damage step began, as well as the remaining attackers and blockers that currently have double strike."
//
// Until 2026-10-18 the engine collapsed this into ONE step (double strike as power x2, first strike with
// no reader). Against a passive, never-blocking opponent the collapse is exact EXCEPT for whatever happens
// between the steps -- and that is not empty: Drana, Liberator of Malakir's counters land on every
// attacker after the first-strike step (the Vampires card this was built for); a first-striker's lifelink
// gain resolves Archangel of Thune / Ajani's Pridemate / Voice of the Blessed counters and can cross a
// life-threshold anthem (Righteous Valkyrie, Serra Ascendant) before the regular step (Lyra Dawnbringer,
// Ocelot Pride); a first-step combat-damage trigger (Neheb's discard) resolves before it. So the split is
// applied wherever it is VALID -- every combat with a first/double striker, in every deck -- not gated to
// one card (CLAUDE.md: scope a change by where it is valid). A combat with no first/double striker takes
// the original single-step loop below, untouched.
//
// SHAPE, step by step (each step is one simultaneous damage event, CR 510.2):
//   * membership is snapshotted ONCE, before the first step (CR 510.4 / 702.7c: a creature that gains or
//     loses first strike between the steps does not change step; no between-step effect in this engine
//     grants double strike, so "currently has double strike" equals the snapshot);
//   * each step recomputes every member's power with the SAME assembly as the single-step loop
//     (multiplier 1 -- a double striker simply appears in both steps), so counters / anthems that landed
//     between the steps are read;
//   * per step: life loss, the lost-life flag, Maelstrom's free-cast bank (one trigger per connecting
//     step), lifelink (ONE GainLife event per lifelinking member per step, applied after the step's
//     damage -- CR 510.2 / 702.15b / 119.10), then the step's combat-damage triggers in the single-step
//     order (Drana's counters, Goblin Lackey, Shroofus, Neheb), each over the step's own connectors;
//   * the ATTACK-trigger hooks the single-step path fires after damage (Leeching Sliver's life loss,
//     Utvara's tokens) fire once, after the regular step's lifelink -- the same relative order as before.
// Umezawa's Jitte keeps its closed form (JitteDamageMath, which already prices the DS mid-step spend):
// applied ONCE, in the bearer's first step, with the original ds flag, and the bearer deals nothing more
// in the regular step -- a disclosed approximation that only differs from the faithful split if a
// between-step effect pumps a double-striking Jitte bearer (no deck holds Jitte with any such effect).
// SBA between the steps: nothing blocks, so nothing dies; an opponent at <= 0 after the first step has
// lost either way, and dealing the regular step changes no outcome (the win turn is the same).
// The play-viewer description keeps ONE "<name> (<damage>)" per attacker, its total over both steps, in
// attack order: test/viewer_protocol_check.py parses that line into the reference-replay attacker pin.
static CombatDamageResult ResolveCombatDamageTwoStep(GameState& state, const std::vector<int>& atk_idx,
                                                     int exalted_bonus, bool collect_descs,
                                                     const BoardSources& bs_in,
                                                     const std::vector<char>& fs_of,
                                                     const std::vector<char>& ds_of)
{
    CombatDamageResult out;
    const int active  = state.active_player_index;
    const int opp_idx = 1 - active;
    const std::size_t n = atk_idx.size();
    std::vector<int>  dealt(n, 0);
    std::vector<char> seen(n, 0);
    std::vector<char> jitte_done(n, 0);

    // The board-source lists are battlefield INDICES gathered before the first step. A first-step
    // combat-damage hook can APPEND a permanent (a Lackey put, a Shroofus token) -- which may itself be
    // a lord or an attachment the regular step must read -- so the lists are re-gathered for the second
    // step whenever the battlefield grew. Appends never shift existing indices, so atk_idx stays valid.
    BoardSources bs_re;
    const BoardSources* bsp = &bs_in;
    std::size_t bf_at_gather = state.battlefield.size();

    auto deal_step = [&](bool first_step)
    {
        if (!first_step && state.battlefield.size() != bf_at_gather)
        {
            bs_re = GatherBoardSources(state.battlefield, active);
            bsp = &bs_re;
            bf_at_gather = state.battlefield.size();
        }
        const BoardSources& bs = *bsp;
        std::vector<int> step_idx;    // this step's connectors, in attack order
        std::vector<int> step_pw;     // ... and what each dealt (Shroofus: "that many")
        std::vector<int> step_lifelink;
        std::vector<int> step_events; // Curiosity / Tandem Lookout: ONE event per connector per step,
        std::vector<int> step_num;    // ... keyed by m_number (filled only under deck_has_draw_ping)
        for (std::size_t k = 0; k < n; ++k)
        {
            const bool in_step = first_step ? (fs_of[k] || ds_of[k]) : (!fs_of[k] || ds_of[k]);
            if (!in_step) { continue; }
            const int idx = atk_idx[k];
            Permanent& p = state.battlefield[idx];
            const bool animated = p.is_animated;
            // The SAME power assembly as the single-step loop (keep the two in lockstep), multiplier 1.
            auto [lord_pb, lord_tb] = ComputeLordBonus(p.card, state, active, p.AnimatedAllTypes(), &p,
                                                       &bs.lords, &bs.anthems);
            (void)lord_tb;
            int base_pw = p.EffectivePower() + lord_pb + exalted_bonus;
            const CardDefinition* adef = CardDatabase::Instance().LookupCached(p.card);
            if (adef)
            {
                if (animated) { base_pw += adef->params.animate_power; }
                base_pw += DynamicBasePower(*adef, state, active);
            }
            base_pw += AuraBonusFor(p, state, &bs.attached).first;
            base_pw += EquipBonusFor(p, state, &bs.attached).first;
            int power = base_pw;
            const int jitte_bf = FindAttachedChargeEquip(state, p, &bs.attached);
            if (jitte_bf >= 0)
            {
                if (jitte_done[k]) { power = 0; }   // the closed form already dealt both halves
                else
                {
                    Permanent& je = state.battlefield[jitte_bf];
                    const CardDefinition* jd = CardDatabase::Instance().LookupCached(je.card);
                    int req = ResolveProvider(state).JitteSpendCount(state, je.charge_counters);
                    if (g_play_jitte_chooser)
                    {
                        int r = (*g_play_jitte_chooser)(state, active, atk_idx, je.charge_counters);
                        if (r >= 0) { req = r; }
                    }
                    else if (HumanPlayActive()) { req = 0; }
                    const auto [jdmg, jafter] = JitteDamageMath(
                        base_pw, ds_of[k] != 0, je.charge_counters, jd->params.charge_pump_power, req);
                    power              = jdmg;
                    je.charge_counters = jafter;
                    jitte_done[k]      = 1;
                }
            }
            state.players[opp_idx].life -= power;
            out.total_damage += power;
            dealt[k] += power;
            if (power > 0)
            {
                state.opponent_lost_life_this_turn = true;
                step_idx.push_back(idx);
                step_pw.push_back(power);
                if (state.deck_has_draw_ping)
                {
                    step_events.push_back(1);
                    step_num.push_back(p.card.m_number);
                }
                // Maelstrom Archangel: one free cast banked per connecting STEP (one trigger per
                // combat-damage event).
                if (adef && adef->params.combat_damage_free_cast) { ++state.free_casts_available; }
                if (CreatureHasLifelink(p, state, &bs.lifelink)) { step_lifelink.push_back(power); }
            }
            if (!seen[k])
            {
                seen[k] = 1;
                if (!CreatureHasVigilance(p, state, &bs.vigilance)) { p.tapped = true; }
            }
        }
        // Lifelink: one life-gain EVENT per lifelinking member of THIS step (CR 510.2, 702.15b,
        // 119.10). After a first step, its watchers (Thune / Pridemate counters, a threshold anthem)
        // are what the regular step reads.
        for (int amt : step_lifelink) { GainLife(state, active, amt); }
        if (!first_step)
        {
            // The attack-trigger hooks the single-step path fires after its damage loop -- once, after
            // the last damage, in the same order relative to lifelink and the combat-damage hooks.
            std::vector<const Permanent*> attackers;
            attackers.reserve(n);
            for (int idx : atk_idx) { attackers.push_back(&state.battlefield[idx]); }
            out.trigger_life_loss = CountAttackTriggerLifeLoss(state.battlefield, active, attackers);
            if (out.trigger_life_loss > 0)
            {
                state.players[opp_idx].life -= out.trigger_life_loss;
                out.total_damage += out.trigger_life_loss;
                state.opponent_lost_life_this_turn = true;
            }
            FireUtvaraAttackTokens(state, active, attackers);
        }
        // This step's combat-damage triggers (CR 603.2), in the single-step path's order. Drana first:
        // her counters commute with every hook below (none of them reads power), and they must be on
        // the attackers before the regular step.
        FireCombatDamageTeamCounters(state, active, atk_idx, step_idx);
        FireCombatDamageCheatIntoPlay(state, active, step_idx);
        FireCombatDamageTokens(state, active, step_idx, step_pw);
        FireNehebCombatDamageDiscards(state, active, step_idx);
        // A double striker connects in BOTH steps, so it draws once per step -- the two events the
        // single-step loop used to count as `ds ? 2 : 1` -- and a first step's draws (and the pings
        // they start) resolve before the regular step.
        FireCombatDamageDrawGrants(state, step_idx, step_num, step_events);
    };
    deal_step(/*first_step=*/true);
    deal_step(/*first_step=*/false);

    if (collect_descs)
    {
        for (std::size_t k = 0; k < n; ++k)
        {
            if (dealt[k] <= 0) { continue; }
            out.attacker_descs.push_back(state.battlefield[atk_idx[k]].card.m_name.str()
                                         + " (" + std::to_string(dealt[k]) + ")");
        }
    }
    return out;
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
    // ...and how many combat-damage EVENTS each dealt (always 1 here: a double striker, two damage
    // steps under CR 510.4, takes the two-step path, which fires the grants per step), for the
    // Curiosity / Tandem Lookout damage->draw grants. Filled only for a deck that
    // runs the Niv loop (GameState::deck_has_draw_ping), so every other deck allocates nothing.
    std::vector<int> damaging_events;
    std::vector<int> damaging_num;   // ...keyed by per-copy m_number, re-found at the hook (see there)
    // Lifelink gains, ONE ENTRY PER LIFELINKING ATTACKER, applied AFTER the damage loop. Combat
    // damage is simultaneous (CR 510.2) and lifelink's gain is part of that damage event
    // (CR 702.15b), so nothing it causes -- Serra Ascendant's 30-life +5/+5 switching on, an
    // Archangel of Thune team counter -- may change THIS step's damage; applying the gain inside
    // the loop let a later attacker's ComputeLordBonus read the post-gain life (attacker-order
    // dependent, and the search's projection never modelled it). Each attacker's gain stays its
    // own life-gain EVENT (CR 119.10: two lifelink attackers = two Pridemate triggers).
    std::vector<int> pending_lifelink;

    // Pre-filter the active player's board-level sources ONCE (usually none of each), so the
    // per-attacker ComputeLordBonus / HasDoubleStrikeFromLords / AuraBonusFor / EquipBonusFor /
    // FindAttachedChargeEquip / CreatureHasLifelink below iterate those tiny lists instead of each
    // re-scanning the whole battlefield. Byte-identical: same permanents, same per-creature logic
    // (see BoardSources in SpellEffects.h). This block used to gather the lord and double-strike
    // halves only; the other four asked the same board-level question once per attacker and were
    // the larger cost on a wide token board.
    const BoardSources      bs       = GatherBoardSources(state.battlefield, active);
    const std::vector<int>& lord_idx = bs.lords;
    const std::vector<int>& ds_idx   = bs.ds;

    // CR 510.4 step membership, snapshotted before any damage. The double-strike answer is computed
    // here ONCE and reused by the single-step loop below (it reads only static board facts -- keyword,
    // lords, attachments, soulbond, Valiant Knight's flag -- none of which that loop changes). Any
    // first or double striker -> the two-step resolution above; otherwise the single-step loop runs
    // exactly as before.
    std::vector<char> fs_of(atk_idx.size(), 0), ds_of(atk_idx.size(), 0);
    bool any_first_step = false;
    for (std::size_t k = 0; k < atk_idx.size(); ++k)
    {
        const Permanent& p = state.battlefield[atk_idx[k]];
        ds_of[k] = CreatureHasDoubleStrike(p, state, &ds_idx) ? 1 : 0;
        fs_of[k] = CreatureHasFirstStrike(p, state, &bs.fs) ? 1 : 0;
        if (ds_of[k] || fs_of[k]) { any_first_step = true; }
    }
    if (any_first_step)
    { return ResolveCombatDamageTwoStep(state, atk_idx, exalted_bonus, collect_descs, bs, fs_of, ds_of); }

    std::size_t atk_k = 0;
    for (int idx : atk_idx)
    {
        Permanent& p = state.battlefield[idx];
        const bool animated = p.is_animated;
        auto [lord_pb, lord_tb] = ComputeLordBonus(p.card, state, active, p.AnimatedAllTypes(), &p, &lord_idx,
                                                   &bs.anthems);
        (void)lord_tb;
        // The single shared oracle (SpellEffects.h) -- printed keyword, subtype lords, Equipment
        // grants, Valiant Knight's until-EOT activation and Silverblade Paladin's soulbond pair, in
        // one place, so this site and TurnSolver's two projections cannot drift apart. Precomputed in
        // the membership pass above (always false here: a double striker takes the two-step path).
        const bool ds = ds_of[atk_k++] != 0;
        int base_pw = p.EffectivePower() + lord_pb + exalted_bonus;
        const CardDefinition* adef = CardDatabase::Instance().LookupCached(p.card);
        if (adef)
        {
            if (animated) { base_pw += adef->params.animate_power; }
            base_pw += DynamicBasePower(*adef, state, active);   // Adeline: power = creature count
        }
        base_pw += AuraBonusFor(p, state, &bs.attached).first;   // Bogles: attached auras + Kor self-buff
        base_pw += EquipBonusFor(p, state, &bs.attached).first;  // KittyEquipment: attached equipment
        // Umezawa's Jitte: spend charge counters on +2/+2 (greedy default / provider / human
        // side-channel), earn 2 per damage event -- the shared closed form (JitteDamageMath) the
        // two TurnSolver projections also use, so search and execution stay lockstep.
        int power = base_pw * (ds ? 2 : 1);
        const int jitte_bf = FindAttachedChargeEquip(state, p, &bs.attached);
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
            if (state.deck_has_draw_ping)
            {
                damaging_events.push_back(ds ? 2 : 1);
                damaging_num.push_back(p.card.m_number);
            }
            // Maelstrom Archangel: connecting banks one free cast for the post-combat main
            // (user-approved banking model; see GameState::free_casts_available). Shared combat
            // core -> executor and rollout bank identically.
            if (adef && adef->params.combat_damage_free_cast) { ++state.free_casts_available; }
            // Lifelink (modeled): combat damage also gains the controller that much life. Inert vs
            // the passive opponent's clock, tracked for life-total decks.
            if (CreatureHasLifelink(p, state, &bs.lifelink)) { pending_lifelink.push_back(power); }
        }
        if (collect_descs && power > 0)
        { out.attacker_descs.push_back(p.card.m_name.str() + " (" + std::to_string(power) + ")"); }
        // Vigilance: own printed keyword OR an attached Equipment that grants it (the shields).
        // This is the single site in the engine that reads vigilance for anything, and it is
        // shared executor+rollout, so the grant stays lockstep by construction.
        if (!CreatureHasVigilance(p, state, &bs.vigilance)) { p.tapped = true; }
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

    // Drana-style team counters (combat_damage_counters_each_attacker) from a source that dealt
    // damage in this single step. Only reachable by a source WITHOUT first strike (Drana's own
    // printed first strike sends her combat to the two-step path), and then its counters land after
    // the last damage -- future turns only. Empty list (every other deck) -> skipped.
    if (!bs.cd_counters.empty()) { FireCombatDamageTeamCounters(state, active, atk_idx, damaging_idx); }

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
    FireNehebCombatDamageDiscards(state, active, damaging_idx);
    // Curiosity / Tandem Lookout's damage->draw grants and the Niv ping loop (see the helper). In this
    // single-step path every connector dealt ONE combat-damage event: a double striker takes the
    // two-step path, which fires the grants once per step.
    FireCombatDamageDrawGrants(state, damaging_idx, damaging_num, damaging_events);
    return out;
}

// ---- MTG_FB_HOLD_BANKED (see Combat.h) -------------------------------------------------------------

static thread_local bool t_fb_hold_banked = false;

std::uint64_t FbBankedSourceMask(const GameState& state)
{
    std::uint64_t m = 0;
    const int n = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < n && i < 64; ++i)
    {
        const Permanent& p = state.battlefield[static_cast<std::size_t>(i)];
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr) { continue; }
        bool banked = d->params.storage_land || d->params.enters_tapped_with_depletion > 0;
        if (!banked)
        { for (const Counter& c : p.counters) { if (c.type == Counter::Type::Depletion) { banked = true; break; } } }
        if (banked) { m |= (1ull << i); }
    }
    return m;
}

// One combat on `probe` (a copy): the pump -- with the banked sources held when `hold` -- then the shared
// damage resolution. `idx` is repaired in place if a payment erased a battlefield entry. True = lethal.
static bool FbProbeCombat(GameState& probe, std::vector<int>& idx, FbPayer payer, bool hold)
{
    FbBankedHoldScope _hold(hold);
    const int active = probe.active_player_index;
    ApplyFirebreathing(probe, active, idx, AvailableManaPool(probe), std::numeric_limits<int>::max(), payer);
    const int exalted = (static_cast<int>(idx.size()) == 1) ? CountExalted(probe.battlefield, active) : 0;
    ResolveCombatDamage(probe, idx, exalted, /*collect_descs=*/false);
    return probe.Opponent().life <= 0 || dmgev::OpponentLostRaw(probe);
}

// MTG_FB_HOLD_BANKED_NEXT_TURN: `probe` (a copy, after this turn's combat) one turn on -- an idle storage
// land banks its +1 (the end-of-turn rule in GameEngine), the active player's board untaps, the "until end
// of turn" pumps expire -- and the SAME attackers swing again with the full pump. A projection, not the
// game: no draw, no main phase, no new attacker; both worlds are projected alike, so the comparison is
// what matters.
static bool FbNextTurnLethal(GameState& probe, std::vector<int>& idx, FbPayer payer)
{
    const int active = probe.active_player_index;
    // A Needle whose LAST counter the pump spent is sacrificed (the SBA the executor runs after every event
    // and the rollout runs after combat, MTG_FB_SBA) -- without it the emptied land untaps and pays again,
    // the phantom that made the spend world look lethal on dragonstorm d0 s1191687 T5 (its only land).
    // The sweep can erase battlefield entries, so the attackers are re-found by per-copy id.
    std::vector<int> ids;
    ids.reserve(idx.size());
    for (int i : idx) { ids.push_back(probe.battlefield[static_cast<std::size_t>(i)].card.m_number); }
    const std::size_t bf_pre = probe.battlefield.size();
    SacrificeDepletedLands(probe);
    if (probe.battlefield.size() != bf_pre)
    {
        idx.clear();
        for (int id : ids)
        {
            for (int bi = 0; bi < static_cast<int>(probe.battlefield.size()); ++bi)
            { if (probe.battlefield[static_cast<std::size_t>(bi)].card.m_number == id) { idx.push_back(bi); break; } }
        }
        if (idx.empty()) { return false; }
    }
    for (Permanent& p : probe.battlefield)
    {
        if (p.controller_index != active) { continue; }
        if (!p.tapped)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d && d->params.storage_land) { ++p.storage_counters; }
        }
        p.tapped = false;
        p.temp_power_bonus = 0;
        p.temp_tough_bonus = 0;
    }
    probe.floating_mana = ManaPool{};
    probe.floating_haste_c = 0;
    // Next turn's draw (the search is clairvoyant: the real top card) and land drop -- the first hand land
    // that enters untapped. Without it the projection is a land short: dragonstorm shipped gi 70 T4, where
    // the T5 Mountain makes the full pump exactly lethal.
    Player& ap = probe.players[static_cast<std::size_t>(active)];
    if (!ap.library.empty()) { ap.hand.push_back(ap.library.DrawTop()); }
    for (std::size_t h = 0; h < ap.hand.size(); ++h)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.hand[h]);
        if (d == nullptr || !d->card.IsLand() || LandWouldEnterTapped(probe, *d, /*allow_pay_life=*/false)) { continue; }
        Permanent land;
        land.card             = d->card;
        land.card.m_number    = ap.hand[h].m_number;
        land.controller_index = active;
        land.owner_index      = active;
        ap.hand.erase(ap.hand.begin() + static_cast<std::ptrdiff_t>(h));
        probe.battlefield.push_back(std::move(land));
        break;
    }
    return FbProbeCombat(probe, idx, payer, /*hold=*/false);
}

// ---- MTG_FB_HOLD_STATS (see Combat.h) -----------------------------------------------------------------
namespace fbholdstats
{
static std::atomic<unsigned long long> g_n[kCount];
bool On() { static const bool v = EnvOn("MTG_FB_HOLD_STATS"); return v; }
void Bump(Slot s) { if (On()) { g_n[s].fetch_add(1, std::memory_order_relaxed); } }
struct Dumper
{
    ~Dumper()
    {
        if (!On()) { return; }
        std::fprintf(stderr, "[fb-hold-stats] fan lists=%llu variants=%llu | flip pin unread=%llu read=%llu"
                             " (no-bank %llu, lethal %llu, dominated %llu, FLIPPED %llu) | unpinned contested=%llu"
                             " | frontier variants folded=%llu scored=%llu\n",
                     g_n[kFanLists].load(), g_n[kFanVariants].load(), g_n[kPinUnread].load(),
                     g_n[kPinRead].load(), g_n[kPinNoBank].load(), g_n[kPinLethal].load(),
                     g_n[kPinDominated].load(), g_n[kPinFlipped].load(), g_n[kBaseContested].load(),
                     g_n[kVariantFolded].load(), g_n[kVariantScored].load());
    }
};
static Dumper g_dumper;
}   // namespace fbholdstats

// MTG_FB_HOLD_FLIP_DOMINANCE (see EngineFlags.h): can the searched other side of a CONTESTED hold be
// strictly better than the verdict's? With no post-combat main this turn the bank cannot be spent after
// combat, and the full pump is not lethal, so NEITHER side wins this turn: the earliest either can win is
// next turn. When either world is projected lethal next turn (FbNextTurnLethal: the depletion sweep, the
// storage +1, the untap, the clairvoyant draw and its land drop, the same attackers with the full pump --
// a line the next turn really offers, against a passive opponent), that world wins next turn, the other
// can at best tie it, and ties go to the base plan -- so the flip is dominated and the pin reads as the
// verdict (its variant folds after combat). A restricted or full second main (Angels' Giada, Jorn,
// Omnitool, a main-2 deck) could spend the held bank this turn, so there the flip is always searched.
static bool FbFlipDominatedByNextTurn(const GameState& state, GameState& spend_after, std::vector<int>& sidx,
                                      const std::vector<int>& atk_idx, FbPayer payer, bool* spend_lethal_out)
{
    const bool spend_lethal = FbNextTurnLethal(spend_after, sidx, payer);
    if (spend_lethal_out) { *spend_lethal_out = spend_lethal; }
    if (spend_lethal) { return true; }
    GameState hold = state;
    std::vector<int> hidx = atk_idx;
    FbProbeCombat(hold, hidx, payer, /*hold=*/true);
    return FbNextTurnLethal(hold, hidx, payer);
}

bool FbHoldBankedThisCombat(const GameState& state, const std::vector<int>& atk_idx, FbPayer payer, int pin)
{
    if (payer == nullptr || atk_idx.empty() || !FbHoldBankedOn()) { return false; }
    const bool flip_req = (pin == 1 && FbHoldSearchedOn());
    if (flip_req) { fbholdstats::Bump(fbholdstats::kPinRead); }
    if (FbBankedSourceMask(state) == 0) { if (flip_req) { fbholdstats::Bump(fbholdstats::kPinNoBank); } return false; }
    // Lethal with EVERYTHING? The full pump on a copy, the caller's own payer (no hold is live yet),
    // then the shared damage resolution. Speculative: no play-viewer events from the probe.
    TapSpeculationScope _spec;
    GameState spend = state;
    std::vector<int> sidx = atk_idx;
    if (FbProbeCombat(spend, sidx, payer, /*hold=*/false))
    { if (flip_req) { fbholdstats::Bump(fbholdstats::kPinLethal); } return false; }
    if (!flip_req) { fbholdstats::Bump(fbholdstats::kBaseContested); }
    // CONTESTED from here: a banked source is up and the full pump does not end the game, so holding the
    // bank and spending it are both real lines. The verdict below is the ORDER (the heuristic's side);
    // MTG_FB_HOLD_SEARCHED lets the searched pin take the other side.
    if (flip_req && FbHoldFlipDominanceOn() && !state.uses_second_main && !state.deck_has_attack_untap
        && !state.deck_has_attack_dig_hand && !ResolveProvider(state).RestrictedSecondMainLive(state))
    {
        // The flip pin on a contested hold: read as the verdict when the flip cannot be strictly better
        // (FbFlipDominatedByNextTurn). The two projections it runs are the verdict's own, so the verdict
        // is computed from them below -- byte-identical to the unpinned verdict.
        bool spend_lethal = false;
        GameState spend_after = spend;
        std::vector<int> sidx2 = sidx;
        if (FbFlipDominatedByNextTurn(state, spend_after, sidx2, atk_idx, payer, &spend_lethal))
        {
            fbholdstats::Bump(fbholdstats::kPinDominated);
            if (!FbHoldBankedNextTurnOn() || !spend_lethal) { return true; }
            GameState hold = state;
            std::vector<int> hidx = atk_idx;
            FbProbeCombat(hold, hidx, payer, /*hold=*/true);
            return FbNextTurnLethal(hold, hidx, payer);
        }
    }
    const auto verdict = [&]() -> bool
    {
        if (!FbHoldBankedNextTurnOn()) { return true; }
        // MTG_FB_HOLD_BANKED_NEXT_TURN: the hold costs a turn when spending now makes NEXT turn's attack
        // lethal and holding does not (the Scourge race: Needle pumped T3 -> kill T4; held -> T5). Only
        // then is the bank spent; when both worlds kill next turn, or neither does, it is kept for the
        // storm turn.
        if (!FbNextTurnLethal(spend, sidx, payer)) { return true; }
        GameState hold = state;
        std::vector<int> hidx = atk_idx;
        FbProbeCombat(hold, hidx, payer, /*hold=*/true);
        return FbNextTurnLethal(hold, hidx, payer);
    };
    const bool v = verdict();
    if (flip_req) { fbholdstats::Bump(fbholdstats::kPinFlipped); }
    return flip_req ? !v : v;
}

FbBankedHoldScope::FbBankedHoldScope(bool on) : prev(t_fb_hold_banked) { t_fb_hold_banked = on; }
FbBankedHoldScope::~FbBankedHoldScope() { t_fb_hold_banked = prev; }

FbPumpPayHold::FbPumpPayHold(const GameState& s) : prev(dmgev::t_pay_hold)
{
    if (t_fb_hold_banked) { dmgev::t_pay_hold = prev | FbBankedSourceMask(s); }
}
FbPumpPayHold::~FbPumpPayHold() { dmgev::t_pay_hold = prev; }
