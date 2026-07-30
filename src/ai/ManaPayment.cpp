// Unified single-attempt mana payment -- see ManaPayment.h for why this exists and what the
// two parameters (available / honor_legacy_cco) encode. The body is the merged text of the
// former AIEngine::TapForCostOnce and TurnSolver TapForCostDirectOnce twins (303 of ~380 lines
// were already identical); comments were merged from both.
#include "ManaPayment.h"
#include "EngineFlags.h"
#include "../cards/CardDatabase.h"
#include "../core/SpellEffects.h"

#include <functional>
#include <vector>

bool TapForCostSharedOnce(GameState& state, const ManaCost& cost_in, bool for_creature,
                          std::uint64_t reserved_mask, ManaPool* available,
                          bool honor_legacy_cco)
{
    int      active = state.active_player_index;
    ManaPool floating;  // mana produced this payment but not yet consumed (held locally)

    // Payment-legal produces for the scarcity path. ProducesForPayment: a colored_creature_only
    // land (Unclaimed Territory / Cavern of Souls / Sliver Hive / Secluded Courtyard) makes only
    // {C} for a NON-creature spell, so it must not be selected for a coloured pip there -- it
    // still pays a generic pip as {C}. The rollout-only MTG_LEGACY_CCO_PAY hatch re-enables the
    // old EffectiveProduces read so the fix's effect stays measurable in one binary (it makes the
    // rollout score lines the game cannot realise; see docs/design/post-breakpoint-search.md).
    static const bool s_legacy_cco = EnvOn("MTG_LEGACY_CCO_PAY");
    auto pay_produces = [&](const CardDefinition& d) -> const std::vector<Color>&
    {
        if (honor_legacy_cco && s_legacy_cco) { return EffectiveProduces(state, active, d); }
        return ProducesForPayment(state, active, d, for_creature);
    };

    // Spend any turn-scoped RESERVE mana (a ritual's floating output) before tapping. No-op when
    // empty -> byte-identical for non-ritual decks. Restored if the whole payment fails below.
    const ManaPool reserve_pre = state.floating_mana;
    ManaCost cost = cost_in;
    SpendFloatingTowardCost(state.floating_mana, cost);

    auto usable = [&](const Permanent& p, const CardDefinition& def) -> bool
    {
        if (reserved_mask)   // reservation audit: a held source is not tappable this attempt
        {
            const std::size_t idx = static_cast<std::size_t>(&p - state.battlefield.data());
            if (idx < 64 && (reserved_mask & (1ull << idx))) { return false; }
        }
        bool is_src = (def.tmpl == CardTemplate::BasicLand)
                   || (def.tmpl == CardTemplate::ManaDork && p.CanTap())
                   || def.params.mana_rock;
        if (!is_src) { return false; }
        if (def.params.creature_mana_only && !for_creature) { return false; }
        if (!StorageSourceLive(p, def)) { return false; }   // uncharged storage land makes no mana
        return true;
    };

    // Tap one non-filter source, producing `amt` of colour `col`, applying depletion
    // decrement and pain. Mirrors the accounting in BuildAvailableMana (AddSourceToPool).
    auto tap_source = [&](Permanent& p, const CardDefinition& def, Color col)
    {
        CcoAuditTap(def, col, for_creature);   // legality audit (MTG_CCO_AUDIT); inert when off
        p.tapped = true;
        DecrementDepletionOnTap(p);
        if (def.params.tap_self_damage > 0) { state.players[active].life -= def.params.tap_self_damage; }
        // Grove of the Burnwillows: the COLOURED tap ({R}/{G}) makes the opponent gain 1 (-> 1 damage
        // with Tainted Remedy out). A `col == Colorless` tap is the painless "{T}: Add {C}" mode --
        // no drip (see DripLandAnyPipColor: a generic pip absent a Remedy routes here as Colorless).
        if (def.params.tap_opponent_lifegain > 0 && col != Color::Colorless)
        { OpponentGainsLife(state, active, def.params.tap_opponent_lifegain, def.card.m_name.str()); }
        // Karoo bounce land ({U}{R} from one tap): produce one mana of EACH colour it makes, so a
        // lone Izzet Boilerworks can pay a two-colour cost (Expressive Iteration {U}{R}) the planner
        // promised. Crediting `amt` of the single matched colour would lose the second colour --
        // the spell was enumerated but unpayable, a silent no-op. AddSourceToPool credits such a
        // land as `amt` wild, so the executor decrements `available.wild`. Single-colour sources
        // keep `amt` of the matched colour (byte-identical).
        //
        // `amt` = mana produced into floating; `consumed` = mana removed from this-turn's `available`
        // pool. For a STORAGE-COUNTER land (Dwarven Hold / Mercadian Bazaar) a single tap now BURSTS
        // ALL live counters (amt == consumed == had): the land is committed for the turn, and the
        // planner already credits it its full PermanentManaYield (= counters) and marks the whole count
        // consumed on tap. The old per-spell PARTIAL burst (amt = min(had, cost - produced_total)) set
        // consumed = had but floated LESS, so the executor delivered fewer red than the planner
        // promised -- silently dropping a legal cast on a tight multi-spell plan when an earlier spell
        // under-burst and stranded a counter (burst amount shifted with irrelevant cast order). See
        // docs/design/dragonstorm-plan-execution-fidelity-bug.md. Bank-the-rest is via the RESERVE (an
        // unneeded storage land is held untapped), not a partial burst. ManaSourceRank taps storage
        // LAST. Non-storage sources keep amt == consumed == the static per-tap yield -> byte-identical
        // for every non-storage deck.
        int amt, consumed;
        if (def.params.storage_land)
        {
            amt = consumed = p.storage_counters;   // burst ALL counters on tap
            p.storage_counters = 0;
        }
        else { amt = consumed = ManaProducedPerTap(def); }
        const std::vector<Color>& prod = EffectiveProduces(state, active, def);
        if (amt > 1 && prod.size() > 1)
        { for (Color c : prod) { floating.Add(c, 1); } if (available) { available->wild -= consumed; } }
        else
        { floating.Add(col, amt); if (available) { available->Add(col, -consumed); } }
    };

    // Ensure floating can satisfy one pip: `any` = generic, else specific colour
    // `needed`. Taps at most one producing source (a filter may also tap one feeder).
    // allow_ramp: may a ramp filter (Ferrous Lake) be used? false when called to FEED a
    // ramp filter's {1}, so ramp filters never feed each other (avoids recursion; the
    // unmodelled ramp->ramp chain is inert unless 2+ ramp filters are the ONLY sources).
    std::function<bool(Color,bool,bool)> produce = [&](Color needed, bool any, bool allow_ramp) -> bool
    {
        { ManaPool probe = floating;
          if (any ? (floating.Total() > 0) : ConsumeFloating(probe, needed)) { return true; } }

        // Scarcity-first source selection (default ON; MTG_TAP_LEGACY opts OUT to the battlefield-order
        // 4-step path below, a byte-identical A/B baseline): pick the LEAST-flexible qualifying source
        // for this pip (via ManaSourceRank, lower = earlier) so rainbow sources stay up; filters rank
        // between duals and tri and are candidates only when feedable now. Ramp filters (rare) are left
        // to the legacy path / backtracker, the complete fallback either way.
        if (TapScarcityEnabled())
        {
            const int bn = static_cast<int>(state.battlefield.size());
            int best_i = -1, best_rank = 1 << 30, best_kind = 0;  // 1 direct, 2 filter-colour, 3 filter-{C}
            for (int i = 0; i < bn; ++i)
            {
                Permanent& p = state.battlefield[i];
                if (p.controller_index != active || p.tapped) { continue; }
                const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
                if (!def || !usable(p, *def)) { continue; }
                int kind = 0;
                if (def->params.is_filter)
                {
                    if (any || needed == Color::Colorless) { kind = 3; }   // {C} mode covers generic/{C}
                    else
                    {
                        bool makes = false;
                        for (Color c : def->params.produces) { if (c == needed) { makes = true; break; } }
                        bool feedable = false;
                        if (makes)
                        {
                            for (Color c : def->params.produces)
                            { ManaPool pr = floating; if (ConsumeFloating(pr, c)) { feedable = true; break; } }
                            if (!feedable) { feedable = HasUntappedNonFilterSourceProducing(state, def->params.produces); }
                        }
                        if (makes && feedable) { kind = 2; } else { continue; }
                    }
                }
                else if (def->params.ramp_filter) { continue; }
                else
                {
                    // Payment-legal produces (see pay_produces above): a colored_creature_only land
                    // is NOT selected for a coloured pip on a non-creature spell (but still pays a
                    // generic pip as {C}).
                    const std::vector<Color>& prod = pay_produces(*def);
                    bool makes = false;
                    if (any) { makes = !prod.empty(); }
                    else { for (Color c : prod) { if (c == needed) { makes = true; break; } } }
                    if (!makes) { continue; }
                    kind = 1;
                }
                const int rank = ResolveProvider(state).ManaSourceRank(state, *def);
                if (rank < best_rank) { best_rank = rank; best_i = i; best_kind = kind; }
            }
            if (best_i < 0) { return false; }
            Permanent& bp = state.battlefield[best_i];
            const CardDefinition* bdef = CardDatabase::Instance().LookupCached(bp.card);
            if (best_kind == 1)
            {
                // {C}-only for a non-creature colored_creature_only land -> the generic tap uses {C}
                // (prod[0]) rather than a colour that could leak to pay a coloured pip.
                const std::vector<Color>& prod = pay_produces(*bdef);
                tap_source(bp, *bdef, any ? DripLandAnyPipColor(state, active, *bdef, prod[0]) : needed);
                return true;
            }
            if (best_kind == 3)
            {
                bp.tapped = true;
                floating.Add(Color::Colorless, 1);
                if (available)
                {
                    if (available->colorless > 0)  { --available->colorless; }
                    else if (available->wild > 0)  { --available->wild; }
                }
                return true;
            }
            // kind 2: filter coloured mode -- feed one of its colours (least-flexible feeder), yield 2.
            const Color out = needed;
            bool have_input = false;
            for (Color c : bdef->params.produces)
            { ManaPool pr = floating; if (ConsumeFloating(pr, c)) { have_input = true; break; } }
            if (!have_input)
            {
                int fi = -1, frank = 1 << 30; Color fcol = Color::Colorless;
                for (int i = 0; i < bn; ++i)
                {
                    Permanent& s = state.battlefield[i];
                    if (s.controller_index != active || s.tapped) { continue; }
                    const CardDefinition* sd = CardDatabase::Instance().LookupCached(s.card);
                    if (!sd || sd->params.is_filter || sd->params.ramp_filter || !usable(s, *sd)) { continue; }
                    bool m = false; Color match = Color::Colorless;
                    for (Color pc : EffectiveProduces(state, active, *sd))
                    { for (Color ic : bdef->params.produces) { if (pc == ic) { m = true; match = ic; break; } } if (m) { break; } }
                    if (!m) { continue; }
                    const int r = ResolveProvider(state).ManaSourceRank(state, *sd);
                    if (r < frank) { frank = r; fi = i; fcol = match; }
                }
                if (fi < 0) { return false; }
                Permanent& fs = state.battlefield[fi];
                tap_source(fs, *CardDatabase::Instance().LookupCached(fs.card), fcol);
            }
            for (Color c : bdef->params.produces) { if (ConsumeFloating(floating, c)) { break; } }
            bp.tapped = true;
            floating.Add(out, 2);
            if (available && available->wild > 0) { --available->wild; }
            return true;
        }

        // 1) Direct non-filter source.
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped) { continue; }
            const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
            if (!def || def->params.is_filter || def->params.ramp_filter || !usable(p, *def)) { continue; }
            // ProducesForPayment (RP-aware; identity for every non-colored_creature_only source).
            // NOTE: the pre-unification executor read EffectiveProduces here -- the unfixed twin of
            // the 6bb2791 coloured-pip fix, reachable only under MTG_TAP_LEGACY (see ManaPayment.h).
            const std::vector<Color>& prod = ProducesForPayment(state, active, *def, for_creature);
            Color col;
            if (any)
            {
                if (prod.empty()) { continue; }
                col = DripLandAnyPipColor(state, active, *def, prod[0]);  // Grove {C} mode for generic
            }
            else
            {
                bool match = false;
                for (Color c : prod) { if (c == needed) { match = true; break; } }
                if (!match) { continue; }
                col = needed;
            }
            tap_source(p, *def, col);
            return true;
        }

        // 2) Filter land colourless mode ({T}: Add {C}) -- for a generic or {C} pip.
        if (any || needed == Color::Colorless)
        {
            for (Permanent& p : state.battlefield)
            {
                if (p.controller_index != active || p.tapped) { continue; }
                const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
                if (!def || !def->params.is_filter || !usable(p, *def)) { continue; }
                p.tapped = true;
                floating.Add(Color::Colorless, 1);
                if (available)
                {
                    if (available->colorless > 0)  { --available->colorless; }
                    else if (available->wild > 0)  { --available->wild; }
                }
                return true;
            }
        }

        // 3) Filter mode for a coloured pip: feed one of the filter's colours, yield 2.
        for (Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped) { continue; }
            const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
            if (!def || !def->params.is_filter || !usable(p, *def)) { continue; }
            Color out;
            if (any)
            {
                if (def->params.produces.empty()) { continue; }
                out = def->params.produces[0];
            }
            else
            {
                bool match = false;
                for (Color c : def->params.produces) { if (c == needed) { match = true; break; } }
                if (!match) { continue; }
                out = needed;
            }
            // Need one of the filter's colours floating; feed it from a non-filter source.
            bool have_input = false;
            for (Color c : def->params.produces)
            {
                ManaPool probe = floating;
                if (ConsumeFloating(probe, c)) { have_input = true; break; }
            }
            if (!have_input)
            {
                bool fed = false;
                for (Color ic : def->params.produces)
                {
                    for (Permanent& s : state.battlefield)
                    {
                        if (s.controller_index != active || s.tapped) { continue; }
                        const CardDefinition* sd = CardDatabase::Instance().LookupCached(s.card);
                        if (!sd || sd->params.is_filter || sd->params.ramp_filter || !usable(s, *sd)) { continue; }
                        bool m = false;
                        for (Color c : EffectiveProduces(state, active, *sd)) { if (c == ic) { m = true; break; } }  // RP feeder
                        if (!m) { continue; }
                        tap_source(s, *sd, ic);
                        fed = true; break;
                    }
                    if (fed) { break; }
                }
                if (!fed) { continue; }  // can't feed this filter; try the next one
            }
            for (Color c : def->params.produces) { if (ConsumeFloating(floating, c)) { break; } }
            p.tapped = true;
            floating.Add(out, 2);
            if (available && available->wild > 0) { --available->wild; }  // filter counted as 1 wild in the pool
            return true;
        }

        // 4) Ramp filter (e.g. Ferrous Lake: {1},{T}: Add {U}{R}). Pay {1} generic from any
        //    other untapped source (incl. a filter's {C}), then yield one of each produces
        //    colour. No free mode; allow_ramp=false in the feed call prevents ramp chains.
        if (allow_ramp)
        {
            for (Permanent& p : state.battlefield)
            {
                if (p.controller_index != active || p.tapped) { continue; }
                const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
                if (!def || !def->params.ramp_filter || !usable(p, *def)) { continue; }
                if (!any)
                {
                    bool match = false;
                    for (Color c : def->params.produces) { if (c == needed) { match = true; break; } }
                    if (!match) { continue; }
                }
                else if (def->params.produces.empty()) { continue; }
                // Pay the {1}: use floating if any, else feed one mana from a non-ramp source.
                if (floating.Total() == 0 && !produce(Color::Colorless, true, false)) { continue; }
                Color took;
                if (!ConsumeFloatingAny(floating, took)) { continue; }
                p.tapped = true;
                for (Color c : def->params.produces) { floating.Add(c, 1); }
                if (available && available->wild > 0) { --available->wild; }  // ramp filter counted as 1 wild
                return true;
            }
        }
        return false;
    };

    auto pay = [&](Color needed, bool any) -> bool
    {
        if (!produce(needed, any, true)) { return false; }
        if (any) { Color took; return ConsumeFloatingAny(floating, took); }
        return ConsumeFloating(floating, needed);
    };

    // Greedy-first, then a backtracking fallback for filter chains the greedy strands
    // (e.g. Throes of Chaos via a Cascade Bluffs + Ferrous Lake chain). Snapshot so the
    // greedy's success path is byte-identical (no GT churn) and only previously-FAILING
    // casts gain the chain solution. See TapForCostBacktrack.
    const std::vector<Permanent> bf_pre = state.battlefield;
    const int life_pre = state.players[active].life;
    const int opp_pre = state.players[1 - active].life;
    const bool oll_pre = state.opponent_lost_life_this_turn;
    // Retain over-produced mana (forced filter/depletion over-tap) into the turn-scoped
    // reserve so a later same-(main-)phase cast can spend it (CR 500.4). state.floating_mana
    // already holds the un-spent reserve after SpendFloatingTowardCost; add the leftover on top.
    // Off (MTG_NO_FLOAT_LEFTOVER) -> no-op.
    auto commit_leftover = [&](const ManaPool& lo)
    { if (FloatLeftoverManaEnabled()) { state.floating_mana.AddPool(lo); } };
    auto greedy = [&]() -> bool
    {
        // Pay coloured requirements first (most restrictive), then generic.
        for (int i = 0; i < cost.white;     ++i) { if (!pay(Color::White,     false)) return false; }
        for (int i = 0; i < cost.blue;      ++i) { if (!pay(Color::Blue,      false)) return false; }
        for (int i = 0; i < cost.black;     ++i) { if (!pay(Color::Black,     false)) return false; }
        for (int i = 0; i < cost.red;       ++i) { if (!pay(Color::Red,       false)) return false; }
        for (int i = 0; i < cost.green;     ++i) { if (!pay(Color::Green,     false)) return false; }
        for (int i = 0; i < cost.colorless; ++i) { if (!pay(Color::Colorless, false)) return false; }
        for (int i = 0; i < cost.generic;   ++i) { if (!pay(Color::Colorless, true )) return false; }
        return true;
    };
    if (greedy()) { commit_leftover(floating); return true; }
    // Greedy failed: try the backtracking solver from a clean board.
    state.battlefield        = bf_pre;
    state.players[active].life = life_pre;
    ManaPool bt_leftover;
    if (TapForCostBacktrack(state, cost, for_creature, ManaPool{}, nullptr, nullptr, &bt_leftover,
                            /*tapped_mask=*/0, /*untapped_max=*/-1, reserved_mask))
    { commit_leftover(bt_leftover); return true; }
    // Floating-fed filter retry: a filter / ramp-filter land (Ferrous Lake {1},{T}: Add {U}{R}) can be
    // FED by turn-scoped floating (a ritual's output, a depletion over-tap). SpendFloatingTowardCost
    // above spent that floating on the cost DIRECTLY, stranding the filter (no feeder left) -- so the
    // first backtracker, run on the REDUCED cost with an empty float pool, could not chain it. Retry
    // the backtracker with the ORIGINAL cost and the ORIGINAL reserve as feed, letting it choose
    // feed-vs-spend. Guarded by a non-empty reserve AND an untapped filter/ramp source, so it is only
    // reached in exactly that stranded-feeder case: a non-floating or filter-less board never enters it
    // (byte-identical), and any cast the greedy/first backtracker already paid never reaches a fallback.
    if (reserve_pre.Total() > 0 && AnyUntappedFilterSource(state))
    {
        state.battlefield          = bf_pre;
        state.players[active].life  = life_pre;
        ManaPool bt2_leftover;
        if (TapForCostBacktrack(state, cost_in, for_creature, reserve_pre, nullptr, nullptr,
                                &bt2_leftover, /*tapped_mask=*/0, /*untapped_max=*/-1, reserved_mask))
        {
            state.floating_mana = ManaPool{};   // the whole reserve was re-allocated by the backtracker
            commit_leftover(bt2_leftover);
            return true;
        }
    }
    // Total failure: a cast that cannot be paid must leave the game exactly as it found it
    // (atomic rollback) -- restore the full pre-payment snapshot, not the greedy's partial-tap
    // end-state. Callers (cycling/sac loops, ill-ordered plans) rely on a failed payment being
    // side-effect-free; the old greedy-fail restore leaked tapped lands / spent counters.
    // (The executor's `available` accounting is deliberately NOT restored -- see ManaPayment.h.)
    state.battlefield                  = bf_pre;
    state.players[active].life         = life_pre;
    state.players[1 - active].life     = opp_pre;
    state.opponent_lost_life_this_turn = oll_pre;
    state.floating_mana                = reserve_pre;   // payment failed -> return the reserve untouched
    return false;
}
