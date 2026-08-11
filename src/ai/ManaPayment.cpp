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
                   || (def.tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield))
                   || def.params.mana_rock;
        if (!is_src) { return false; }
        if (def.params.creature_mana_only && !for_creature) { return false; }
        if (!StorageSourceLive(p, def)) { return false; }   // uncharged storage land makes no mana
        if (!GraveyardFuelLive(state, active, def)) { return false; }   // Deathrite: no gy land = no mana
        return true;
    };

    // Tap one non-filter source, producing `amt` of colour `col`, applying depletion
    // decrement and pain. Mirrors the accounting in BuildAvailableMana (AddSourceToPool).
    auto tap_source = [&](Permanent& p, const CardDefinition& def, Color col)
    {
        CcoAuditTap(def, col, for_creature);   // legality audit (MTG_CCO_AUDIT); inert when off
        p.tapped = true;
        DecrementDepletionOnTap(p);
        // Deathrite Shaman ability 1: the mana tap exiles a graveyard land (usable() guaranteed
        // one exists). A failed payment restores the graveyard from gy_pre below.
        if (def.params.gy_land_exile_mana) { ExileGraveyardLandForMana(state, active); }
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
        else if (def.params.domain_mana)
        {
            // Faeburrow / Bloom Tender: one mana of EACH colour among controlled permanents.
            amt = consumed = static_cast<int>(EffectiveProduces(state, active, def).size());
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
    // Deathrite: a tap may exile a graveyard land; failed attempts must put it back.
    const std::vector<Card> gy_pre = state.players[active].graveyard;
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
    state.players[active].graveyard = gy_pre;
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
        state.players[active].graveyard = gy_pre;
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
    state.players[active].graveyard    = gy_pre;
    state.players[1 - active].life     = opp_pre;
    state.opponent_lost_life_this_turn = oll_pre;
    state.floating_mana                = reserve_pre;   // payment failed -> return the reserve untouched
    return false;
}

ManaCost EffectiveSpellCost(const CardDefinition& def, const GameState& state, int copies)
{
    if (def.params.spectacle_cost.has_value() && state.opponent_lost_life_this_turn)
    {
        return def.params.spectacle_cost.value();
    }
    ManaCost cost = def.card.m_mana_cost;
    // Splice onto Arcane: casting ONE base while splicing k = copies-1 OTHER copies adds each spliced
    // copy's SPLICE cost (params.splice_cost; unset -> the card's own printed cost) to the base's RAW
    // cost FIRST, so the Medallion/affinity/Hinata reductions below apply ONCE to the combined total
    // (a single floor at 0) -- NOT once per copy (which would over-subtract the reduction). With
    // splice_cost defaulting to the printed cost this is an exact (k+1)x multiply (byte-identical for
    // Desperate Ritual, splice {1}{R} == cast {1}{R}); a splice cost that differs is now priced right.
    // copies==1 (every non-spliced cast) adds nothing -> byte-identical for all other decks.
    if (copies != 1)
    {
        const ManaCost& sc = def.params.splice_cost.has_value()
                           ? def.params.splice_cost.value()
                           : def.card.m_mana_cost;
        const int k = copies - 1;
        cost.generic   += k * sc.generic;
        cost.white     += k * sc.white;
        cost.blue      += k * sc.blue;
        cost.black     += k * sc.black;
        cost.red       += k * sc.red;
        cost.green     += k * sc.green;
        cost.colorless += k * sc.colorless;
    }
    if (def.params.affinity_for_subtype && !def.params.subtypes_affected.empty())
    {
        int reduction = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index) { continue; }
            for (const std::string& sub : def.params.subtypes_affected)
            {
                bool matches = p.is_animated;
                if (!matches)
                {
                    for (const std::string& cs : p.card.m_subtypes)
                    {
                        if (cs == sub) { matches = true; break; }
                    }
                }
                if (matches) { ++reduction; break; }
            }
        }
        cost.generic = std::max(0, cost.generic - reduction);
    }
    // Ruby Medallion-style colour cost reduction: each permanent you control whose
    // reduces_spell_color matches a colour in THIS spell's printed cost reduces its GENERIC by 1
    // (floored at 0, stacks per copy). Gated on a reducer being in play, so decks without one are
    // byte-identical. Without this on the EXECUTOR side it over-paid red spells relative to the
    // planner/rollout, so a committed Medallion-funded combo line (T3 Dragonstorm) was unpayable
    // at execution -> fd-diverge. (Same-turn-cast Medallions are handled by ManaPruneBound's bail.)
    {
        int color_reduction = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index) { continue; }
            const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
            if (!pd || pd->params.reduces_spell_color.empty()) { continue; }
            const std::string& rc = pd->params.reduces_spell_color;
            const ManaCost&    mc = def.card.m_mana_cost;   // printed pips (colour unchanged by discounts)
            const bool spell_has_color =
                  (rc == "W" && mc.white > 0) || (rc == "U" && mc.blue  > 0)
                || (rc == "B" && mc.black > 0) || (rc == "R" && mc.red   > 0)
                || (rc == "G" && mc.green > 0);
            if (spell_has_color) { ++color_reduction; }
        }
        cost.generic = std::max(0, cost.generic - color_reduction);
    }
    // Goblin Warchief-style SUBTYPE cost reduction: each permanent you control whose
    // reduces_spell_subtype matches a SUBTYPE of THIS spell reduces its GENERIC by 1 (floored at 0,
    // stacks per copy). The subtype twin of reduces_spell_color above; gated on a reducer in play so
    // decks without one are byte-identical. (Same-turn-cast Warchief handled by the in-order walk.)
    if (!def.card.m_subtypes.empty())
    {
        int subtype_reduction = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index) { continue; }
            const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
            if (!pd || pd->params.reduces_spell_subtype.empty()) { continue; }
            const std::string& rs = pd->params.reduces_spell_subtype;
            for (const std::string& cs : def.card.m_subtypes)
            {
                if (cs == rs) { ++subtype_reduction; break; }
            }
        }
        cost.generic = std::max(0, cost.generic - subtype_reduction);
    }
    // Hinata's per-target cost reduction (fixed-cost spells; {X} spells apply it at the X-cost
    // sites where the whole generic, incl. X, is known -- CastSpellFromHand / apply_one).
    if (!def.card.m_mana_cost.has_x)
    {
        cost.generic = std::max(0, cost.generic - HinataGenericDiscount(def, state, 0));
    }
    return cost;
}

// THE cast-order comparator (C1 unit 3): provider RANK first, then CHEAPEST-FIRST by the action's
// ACTUAL cost. The rank alone is not enough -- every mana ritual shares one rank, so their relative
// order was arbitrary, and the DEAREST could be attempted first, fail to be paid, and be silently
// dropped (CastSpellFromHand returns void). That strands the mana it would have produced and can
// leave the payoff short: Dragonstorm d0 seed 8585 led with Seething Song ({2}{R}) on two lands,
// skipped it, floated 7 off the cheap rituals and then could not pay Dragonstorm ({8}{R} = 9) -- the
// whole chain burned. A ritual chain funds itself cheapest-first (the principle BuildAccelPrefixOrder
// already uses for the ENUMERATION); this applies it to EXECUTION.
// It must key on Action::cost, NOT the card's printed cost: CastOrderRank only sees the
// CardDefinition, so it cannot see that a SPLICED Desperate Ritual really costs {2}{R}{R} rather
// than {1}{R}. Ranking by the printed cost put the spliced copy early and dropped it exactly like
// Seething Song. Was a byte-identical twin pair (TurnSolver's CastOrderLess / AIEngine's
// CastOrderLessAI); executor and rollout now share this definition.
bool CastOrderLess(const GameState& state, const Action& a, const Action& b)
{
    // Reuse the action's memoized def (back-filled once per node, == Lookup(card_name)) instead of
    // re-hashing the name string on every comparison -- this comparator runs O(n log n) per sort.
    // Byte-identical; def==nullptr (an action from a path that didn't back-fill) falls back to Lookup.
    const CardDefinition* da = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
    const CardDefinition* db = b.def ? b.def : CardDatabase::Instance().Lookup(b.card_name);
    const int ra = da ? ResolveProvider(state).CastOrderRank(state, *da) : 20;
    const int rb = db ? ResolveProvider(state).CastOrderRank(state, *db) : 20;
    if (ra != rb) { return ra < rb; }
    if (LegacyCastTierOrder()) { return false; }                                   // stable: keep plan order
    if (!ResolveProvider(state).CastCheapestFirstWithinTier()) { return false; }   // stable: keep plan order
    // ONLY among mana accelerants. Applying it to every equal-rank tie also reordered CREATURES,
    // where cost is the wrong key and ETB order carries real value: Scourge of Valkas damages per
    // Dragon that enters, so "Lathliss then Scourge" and "Scourge then Lathliss" differ by 3 damage
    // (dragonstorm_overnight_d3_s7007 gi310 lost a turn to exactly that swap, with identical draws).
    if (!da || !db || !IsManaRitual(*da) || !IsManaRitual(*db)) { return false; }
    return a.cost.ManaValue() < b.cost.ManaValue();
}

// A cast whose resolution triggers a mid-turn re-solve breakpoint (draw / staging / cascade
// / retrace): the rest of the turn re-solves from the post-draw state, so the optimal cast
// ORDER around it is situation-dependent (mana left, what is revealed) -- a static rank
// can't capture it. The CastOrderLess reordering is therefore SKIPPED for any set that
// contains such a card; that set keeps its canonical plan/breakpoint order (the search owns
// the ambiguous ordering).
bool OrderingOpaque(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    if (!d) { return false; }
    return d->tmpl == CardTemplate::DrawUntilNonland
        || d->params.stages_cards
        || d->params.cascade_max_mv > 0
        || d->params.retrace
        || d->params.expressive_iteration
        || d->params.impulse_exile > 0   // Apex of Power: staged exile -> search-owned breakpoint order
        || d->params.draw > 0
        // Zada/Mirrorwing solo-target trick: every trick in the deck cantrips (cast_draw), and a
        // magnet fan-out mass-draws mid-turn -- the post-draw re-solve owns the ordering.
        || d->params.solo_target_trick;
}

// THE accounting mana pool (C1 unit 4). Depletion lands contribute 2, multi-color lands 1 wild,
// filter lands (Cascade Bluffs) 1 wild when fed else 1 {C} -- see AddSourceToPool. Storage lands
// (Dwarven Hold / Mercadian Bazaar) yield their LIVE storage_counters via PermanentManaYield (0
// when uncharged): a dead sc=0 storage land must add nothing, not its static per-tap 1 (the
// rollout once over-credited dead storage lands vs the executor's Firebreathe, projecting the
// Dragonstorm combo kill a turn early -- fd-diverge). For non-storage sources PermanentManaYield
// == ManaProducedPerTap. The turn-scoped reserve (ritual float + retained over-production) is
// spendable on later same-phase casts, so it counts toward affordability; empty for non-floating
// decks, and MTG_NO_FLOAT_LEFTOVER restores the legacy board-only pool. Was a byte-identical twin
// pair (TurnSolver's BuildPool / AIEngine::BuildAvailableMana).
ManaPool AvailableManaPool(const GameState& state, const Permanent* skip)
{
    ManaPool pool;
    int gy_fuel = -1;   // Deathrite fuel: lazily counted, decremented per credited source
    for (const Permanent& p : state.battlefield)
    {
        if (&p == skip) { continue; }   // "as if this source were tapped" (see the header note)
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        auto def = CardDatabase::Instance().LookupCached(p.card);
        if (!def) { continue; }
        bool is_land = (def->tmpl == CardTemplate::BasicLand);
        bool is_dork = (def->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield)) || def->params.mana_rock;
        if (!is_land && !is_dork) { continue; }
        // Deathrite: credit at most #graveyard-lands such sources (fuel-counted, lazily).
        if (def->params.gy_land_exile_mana)
        {
            if (gy_fuel < 0) { gy_fuel = GraveyardLandFuel(state, state.active_player_index); }
            if (gy_fuel <= 0) { continue; }
            --gy_fuel;
        }
        AddSourceToPool(pool, state, *def, PermanentManaYield(p, *def));
    }
    if (FloatLeftoverManaEnabled()) { pool.AddPool(state.floating_mana); }
    return pool;
}

// Plan-scoped source reservation (see g_plan_reserved_sources). Stored as CARD NUMBERS, not
// battlefield indices: a main phase can push new permanents and remove others (a sacrifice, a Karoo
// bounce), so an index captured before the casts is not stable across them. Resolved to the
// bitmask the payment path wants here, where the board is in scope. Empty vector -> 0 -> the
// reserve-then-fallback below is byte-identical to before for every other plan.
thread_local std::vector<int> g_plan_reserved_sources;

static std::uint64_t PlanReserveMask(const GameState& state)
{
    if (g_plan_reserved_sources.empty()) { return 0; }
    const int n = static_cast<int>(state.battlefield.size());
    if (n > 64) { return 0; }                    // bitmask limit, matching ReservableSpecialMask
    std::uint64_t mask = 0;
    for (int i = 0; i < n; ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        for (int num : g_plan_reserved_sources)
        { if (p.card.m_number == num) { mask |= (1ull << i); break; } }
    }
    return mask;
}

// THE public payment entry (C1 unit 5) -- reserved-first retry around TapForCostSharedOnce.
// Snapshot everything a payment can touch (incl. the executor's `available` accounting pool, when
// present) so a reserved MISS restores byte-identically before the normal attempt (which must
// reproduce the pre-reservation payment exactly).
bool TapForCostShared(GameState& state, const ManaCost& cost_in, bool for_creature,
                      ManaPool* available, bool honor_legacy_cco)
{
    // Two-colour hybrid pips ({B/G}, Deathrite Shaman): expand into concrete-colour assignments
    // and try each through the (hybrid-unaware) full pipeline below. bits==0 IS the flat cost the
    // historical first-colour collapse produced, tried first and UNsnapshotted -- so whenever it
    // succeeds or every assignment fails, behaviour (including the executor's deliberately-not-
    // restored accounting pool -- the smoke suite caught churn when it was restored) is
    // byte-identical to the pre-hybrid engine. Alternative assignments run snapshot/restored in
    // between; on total failure the bits==0 attempt is REPLAYED so its historical failure side
    // effects land exactly as before.
    if (cost_in.hybrid_count > 0)
    {
        const int a = state.active_player_index;
        const std::vector<Permanent> bf_snap = state.battlefield;
        const ManaPool               fm_snap = state.floating_mana;
        const ManaPool               av_snap = available ? *available : ManaPool{};
        const std::vector<Card>      gy_snap = state.players[a].graveyard;   // Deathrite exile
        const int  la  = state.players[a].life;
        const int  lo  = state.players[1 - a].life;
        const bool oll = state.opponent_lost_life_this_turn;
        auto restore = [&]()
        {
            state.battlefield                  = bf_snap;
            state.floating_mana                = fm_snap;
            if (available) { *available = av_snap; }
            state.players[a].graveyard         = gy_snap;
            state.players[a].life              = la;
            state.players[1 - a].life          = lo;
            state.opponent_lost_life_this_turn = oll;
        };
        if (TapForCostShared(state, cost_in.ExpandHybrids(0), for_creature,
                             available, honor_legacy_cco)) { return true; }
        for (unsigned bits = 1; bits < (1u << cost_in.hybrid_count); ++bits)
        {
            restore();
            if (TapForCostShared(state, cost_in.ExpandHybrids(bits), for_creature,
                                 available, honor_legacy_cco)) { return true; }
        }
        restore();
        // Total failure: replay the flat attempt (deterministic) so the state ends exactly as the
        // historical single-attempt failure left it.
        TapForCostShared(state, cost_in.ExpandHybrids(0), for_creature, available, honor_legacy_cco);
        return false;
    }

    const std::uint64_t rmask = ReservableSpecialMask(state) | PlanReserveMask(state);
    if (rmask != 0)
    {
        const int a = state.active_player_index;
        const std::vector<Permanent> bf_snap  = state.battlefield;
        const ManaPool               fm_snap  = state.floating_mana;
        const ManaPool               av_snap  = available ? *available : ManaPool{};
        const std::vector<Card>      gy_snap  = state.players[a].graveyard;   // Deathrite exile
        const int  la  = state.players[a].life;
        const int  lo  = state.players[1 - a].life;
        const bool oll = state.opponent_lost_life_this_turn;
        if (TapForCostSharedOnce(state, cost_in, for_creature, rmask, available, honor_legacy_cco))
        { return true; }
        state.battlefield                  = bf_snap;
        state.floating_mana                = fm_snap;
        if (available) { *available = av_snap; }
        state.players[a].graveyard         = gy_snap;
        state.players[a].life              = la;
        state.players[1 - a].life          = lo;
        state.opponent_lost_life_this_turn = oll;
    }
    return TapForCostSharedOnce(state, cost_in, for_creature, /*reserved_mask=*/0, available,
                                honor_legacy_cco);
}

// Animate all animatable lands (e.g. Mutavault) if the active player has spare mana. Run after
// spells are cast, before combat, so the animated land can attack.
// PRESERVED divergence: the executor gates on its accounting pool BEFORE attempting -- a FAILED
// single attempt does not restore `available` (see the header), so it must not attempt what the
// pool says it cannot pay. The rollout keeps no pool and simply tries (a failed direct payment
// leaves the board untouched).
void AnimateLandsShared(GameState& state, ManaPool* available)
{
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index
            || p.tapped || p.is_animated) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (!def || !def->params.can_animate || !def->params.animate_cost.has_value()) { continue; }
        const ManaCost& cost = def->params.animate_cost.value();
        if (available != nullptr && !available->CanPay(cost)) { continue; }
        if (!TapForCostShared(state, cost, false, available,
                              /*honor_legacy_cco=*/available == nullptr)) { continue; }
        p.is_animated = true;
    }
}

// Activate tap-and-pay token abilities (e.g. Sliver Hive) with any spare mana.
// PRESERVED divergence around the affordability gate: the executor gates on its accounting pool
// BEFORE tapping the source (so the gate can still see the source's own mana, though the payment
// itself cannot use it -- the source is tapped first), and un-taps on a failed payment. The
// rollout taps the {T} source FIRST and gates on a fresh board pool that therefore EXCLUDES the
// source's own contribution, then ignores the payment result (the gate already passed). Neither
// side's gate is wrong enough to matter in the suite, but they are NOT the same predicate; the
// branch below keeps each side byte-identical to its historical behaviour.
void ActivateTapTokensShared(GameState& state, ManaPool* available)
{
    int bf_size = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < bf_size; ++i)
    {
        if (state.battlefield[i].controller_index != state.active_player_index
            || state.battlefield[i].tapped) { continue; }
        const CardDefinition* def =
            CardDatabase::Instance().LookupCached(state.battlefield[i].card);
        if (!def || !def->params.tap_token_cost.has_value()) { continue; }

        if (!def->params.tap_token_requires_subtypes.empty())
        {
            bool found = false;
            for (int j = 0; j < bf_size; ++j)
            {
                if (state.battlefield[j].controller_index != state.active_player_index) { continue; }
                for (const std::string& req : def->params.tap_token_requires_subtypes)
                    for (const std::string& cs : state.battlefield[j].card.m_subtypes)
                        if (cs == req) { found = true; break; }
                if (found) { break; }
            }
            if (!found) { continue; }
        }

        const ManaCost& add_cost = def->params.tap_token_cost.value();
        if (available != nullptr)
        {
            if (!available->CanPay(add_cost)) { continue; }
            state.battlefield[i].tapped = true;
            if (!TapForCostShared(state, add_cost, true, available, /*honor_legacy_cco=*/false))
            {
                state.battlefield[i].tapped = false;
                continue;
            }
        }
        else
        {
            state.battlefield[i].tapped = true;   // {T} cost; tap before building pool
            ManaPool remaining = AvailableManaPool(state);
            if (!remaining.CanPay(add_cost)) { state.battlefield[i].tapped = false; continue; }
            TapForCostShared(state, add_cost, true, nullptr, /*honor_legacy_cco=*/true);
        }

        // CreateToken appends to battlefield -- access via index afterward, never via stale refs.
        CreateToken(state, state.active_player_index,
                    def->params.tap_token_power,
                    def->params.tap_token_toughness,
                    def->params.tap_token_subtypes);
    }
}
