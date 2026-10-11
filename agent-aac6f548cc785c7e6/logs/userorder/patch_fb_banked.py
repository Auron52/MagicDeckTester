"""Fix (2): MTG_FB_HOLD_BANKED -- the combat pump does not spend depletion / storage lands unless the pump is lethal."""
import os
WT = os.environ.get('PATCH_ROOT', '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6')


def sub(path, old, new, count=1):
    s = open(path).read()
    assert s.count(old) == count, (path, old[:80], s.count(old))
    s = s.replace(old, new)
    open(path, 'w').write(s)


# 1. heurarm slot (enum + kNames, same order), appended after DRAGONSTORM_IRENCRAG_CLOSER
h = WT + '/src/ai/HeuristicArm.h'
t = open(h).read()
i = t.index('    DRAGONSTORM_IRENCRAG_CLOSER,')
j = t.index('\n', i) + 1
t = t[:j] + ('    FB_HOLD_BANKED,           // MTG_FB_HOLD_BANKED the greedy combat pump (ApplyFirebreathing: Scourge {R}, Lathliss {1}{R}) does not tap a BANKED source -- a depletion land (Sandstone Needle: a counter per tap, sacrificed at zero) or a storage land (Mercadian Bazaar, Dwarven Hold: its banked counters, and the +1 it banks if left untapped) -- unless the full pump makes the attack lethal. The pump\'s "unused mana is lost" premise is false for those sources (dragonstorm d0 s1191687 T3: the Needle sacrificed to a non-lethal pump; s7600233: the Bazaar\'s bank). Executor + rollout, one helper (Combat.cpp). Default ON; =0 -> every source pumps\n') + t[j:]
k = t.index('        "MTG_DRAGONSTORM_IRENCRAG_CLOSER",\n') + len('        "MTG_DRAGONSTORM_IRENCRAG_CLOSER",\n')
t = t[:k] + '        "MTG_FB_HOLD_BANKED",\n' + t[k:]
open(h, 'w').write(t)

# 2. shared reader
sub(WT + '/src/ai/EngineFlags.h',
    '''inline bool DragonstormIrencragCloserOn()
{
    static const bool env_on = EnvOn("MTG_DRAGONSTORM_IRENCRAG_CLOSER", true);
    return heurarm::Flag(heurarm::DRAGONSTORM_IRENCRAG_CLOSER, env_on);
}
''',
    '''inline bool DragonstormIrencragCloserOn()
{
    static const bool env_on = EnvOn("MTG_DRAGONSTORM_IRENCRAG_CLOSER", true);
    return heurarm::Flag(heurarm::DRAGONSTORM_IRENCRAG_CLOSER, env_on);
}

// MTG_FB_HOLD_BANKED (DEFAULT ON; =0 disables) -- the combat pump holds BANKED sources (depletion and
// storage lands) back unless the full pump is lethal. Read by both pump sites (AIEngine::Firebreathe and
// the rollout's SimulateCombat) through FbHoldBankedThisCombat (Combat.cpp) -> lockstep.
inline bool FbHoldBankedOn()
{
    static const bool env_on = EnvOn("MTG_FB_HOLD_BANKED", true);
    return heurarm::Flag(heurarm::FB_HOLD_BANKED, env_on);
}
''')

# 3. Combat.h declarations
sub(WT + '/src/ai/Combat.h',
    '''#include "../core/GameState.h"
#include <string>
#include <vector>
''',
    '''#include "../core/GameState.h"
#include <cstdint>
#include <string>
#include <vector>
''')
s = open(WT + '/src/ai/Combat.h').read()
s = s.rstrip('\n') + '''

// ---- MTG_FB_HOLD_BANKED: the combat pump keeps BANKED mana unless the pump is lethal ----------------
//
// ApplyFirebreathing spends ALL leftover mana on attacker pumps, on the premise that combat is the
// turn's last mana use, so mana left unspent is lost. That premise is false for a BANKED source:
//   * a depletion land (Sandstone Needle, Peat Bog): each tap spends a counter, and the land is
//     sacrificed at zero -- the pump buys +2 damage with half a land;
//   * a storage land (Mercadian Bazaar, Dwarven Hold): the tap releases counters banked over turns,
//     and an untapped storage land banks +1 at end of turn.
// So a NON-lethal pump holds those sources back; a pump that makes the attack lethal still spends
// everything (the game ends). Lethality is judged on a COPY: the full pump with the caller's own
// payer, then the shared ResolveCombatDamage. The later declare-attackers triggers (Skyhunter dig,
// Flinger fling) are not replayed in the probe -- the conservative direction is only possible for a
// deck that holds both such a trigger and a banked land, and none does.
// dragonstorm d0 s1191687 T3 (the Needle sacrificed to a non-lethal Scourge pump; kill T7 vs the old
// T6) and s7600233 (the Bazaar's bank pumped away a turn before the storm needed it).

// The pump payer's type (the same alias SpellEffects.h declares; redeclaring an alias to the same type is
// well-formed, and it keeps this header off SpellEffects.h).
using FbPayer = bool (*)(GameState&, const ManaCost&);

// Bits (battlefield index < 64) of the active player's UNTAPPED banked sources.
std::uint64_t FbBankedSourceMask(const GameState& state);

// Should this combat's pump hold the banked sources? Lever on, a banked source untapped, and the full
// pump (with `payer`, on a copy) NOT lethal. False (byte-identical) whenever no banked source is up.
bool FbHoldBankedThisCombat(const GameState& state, const std::vector<int>& atk_idx, FbPayer payer);

// The hold is published per thread for the duration of one pump (both sites) ...
struct FbBankedHoldScope
{
    bool prev;
    explicit FbBankedHoldScope(bool on);
    ~FbBankedHoldScope();
    FbBankedHoldScope(const FbBankedHoldScope&)            = delete;
    FbBankedHoldScope& operator=(const FbBankedHoldScope&) = delete;
};

// ... and each pump PAYMENT installs it as a pay hold (recomputed per payment, so a pay-sac erase that
// shifts battlefield indices cannot misdirect it). A no-op unless an FbBankedHoldScope is on.
struct FbPumpPayHold
{
    std::uint64_t prev;
    explicit FbPumpPayHold(const GameState& s);
    ~FbPumpPayHold();
    FbPumpPayHold(const FbPumpPayHold&)            = delete;
    FbPumpPayHold& operator=(const FbPumpPayHold&) = delete;
};
'''
open(WT + '/src/ai/Combat.h', 'w').write(s)

# 4. Combat.cpp implementation
s = open(WT + '/src/ai/Combat.cpp').read()
sub(WT + '/src/ai/Combat.cpp',
    '#include "../core/SpellEffects.h"\n',
    '#include "../core/SpellEffects.h"\n#include "../core/DamageEvents.h"   // dmgev::t_pay_hold (MTG_FB_HOLD_BANKED)\n#include "EngineFlags.h"\n#include "ManaPayment.h"           // AvailableManaPool\n#include <limits>\n')
s = open(WT + '/src/ai/Combat.cpp').read()
s = s.rstrip('\n') + '''

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

bool FbHoldBankedThisCombat(const GameState& state, const std::vector<int>& atk_idx, FbPayer payer)
{
    if (payer == nullptr || atk_idx.empty() || !FbHoldBankedOn()) { return false; }
    if (FbBankedSourceMask(state) == 0) { return false; }
    // Lethal with EVERYTHING? The full pump on a copy, the caller's own payer (no hold is live yet),
    // then the shared damage resolution. Speculative: no play-viewer events from the probe.
    TapSpeculationScope _spec;
    GameState probe = state;
    std::vector<int> pidx = atk_idx;
    const int active = probe.active_player_index;
    ApplyFirebreathing(probe, active, pidx, AvailableManaPool(probe), std::numeric_limits<int>::max(), payer);
    const int exalted = (static_cast<int>(pidx.size()) == 1) ? CountExalted(probe.battlefield, active) : 0;
    ResolveCombatDamage(probe, pidx, exalted, /*collect_descs=*/false);
    const bool lethal = probe.Opponent().life <= 0 || dmgev::OpponentLostRaw(probe);
    return !lethal;
}

FbBankedHoldScope::FbBankedHoldScope(bool on) : prev(t_fb_hold_banked) { t_fb_hold_banked = on; }
FbBankedHoldScope::~FbBankedHoldScope() { t_fb_hold_banked = prev; }

FbPumpPayHold::FbPumpPayHold(const GameState& s) : prev(dmgev::t_pay_hold)
{
    if (t_fb_hold_banked) { dmgev::t_pay_hold = prev | FbBankedSourceMask(s); }
}
FbPumpPayHold::~FbPumpPayHold() { dmgev::t_pay_hold = prev; }
'''
open(WT + '/src/ai/Combat.cpp', 'w').write(s)

# 5. executor site
sub(WT + '/src/ai/AIEngine.cpp',
    '''          { ManaPool avail = AvailableManaPool(s); return TapForCostShared(s, c, false, &avail, false); }''',
    '''          { FbPumpPayHold _fbh(s);   // MTG_FB_HOLD_BANKED (a no-op unless the scope below is on)
            ManaPool avail = AvailableManaPool(s); return TapForCostShared(s, c, false, &avail, false); }''')
sub(WT + '/src/ai/AIEngine.cpp',
    '''    const std::vector<int> fb = ResolveProvider(state).FirebreatheActivations(state);
    const int fb_k = fb.empty() ? -1 : fb.front();
    if (fb_k < 0) { g_fb_activations_this_turn''',
    '''    const std::vector<int> fb = ResolveProvider(state).FirebreatheActivations(state);
    const int fb_k = fb.empty() ? -1 : fb.front();
    // MTG_FB_HOLD_BANKED: a non-lethal pump keeps the depletion / storage lands (see Combat.h). The
    // human-chooser path above is left to the human. Lockstep twin in the rollout's SimulateCombat.
    FbBankedHoldScope _fb_banked(g_play_firebreathe_chooser == nullptr
                                 && FbHoldBankedThisCombat(state, attacker_indices, payer));
    if (fb_k < 0) { g_fb_activations_this_turn''')

# 6. rollout site
sub(WT + '/src/ai/TurnSolver.cpp',
    '''            ? +[](GameState& s, const ManaCost& c) -> bool
              { return TapForCostDirect(s, c, /*for_creature=*/false); }
            : nullptr;
        if (fb_k < 0) { ApplyFirebreathing(state, active, atk_idx, AvailableManaPool(state),''',
    '''            ? +[](GameState& s, const ManaCost& c) -> bool
              { FbPumpPayHold _fbh(s);   // MTG_FB_HOLD_BANKED (a no-op unless the scope below is on)
                return TapForCostDirect(s, c, /*for_creature=*/false); }
            : nullptr;
        // MTG_FB_HOLD_BANKED: a non-lethal pump keeps the depletion / storage lands (Combat.h).
        FbBankedHoldScope _fb_banked(FbHoldBankedThisCombat(state, atk_idx, fb_payer));
        if (fb_k < 0) { ApplyFirebreathing(state, active, atk_idx, AvailableManaPool(state),''')
print('ok')
