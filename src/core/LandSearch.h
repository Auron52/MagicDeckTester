#pragma once
// =================================================================================================
// THE LAND-SEARCH CLUSTER (2026-10-15) -- INCLUDED FROM SpellEffects.h ONLY (after EnterLand,
// ResolveLandEnterEtbExtras and PerformLandTutorToBattlefield, which it builds on). The pin, the spec
// struct and the forward declarations live beside ShuffleAfterSearch in SpellEffects.h, because the
// fire sites are spread through that header.
//
// One resolution helper and one pick generator for every "search your library for up to N [basic]
// land card(s) [that share a land type], put them onto the battlefield [tapped] [and/or into your
// hand], then shuffle":
//
//   card                       spec                                    fire site
//   Rampant Growth             1 basic, tapped                         spell resolution
//   Search for Tomorrow        1 basic, untapped                       spell resolution / off suspend
//   Cultivate                  2 basic, 1 to hand, tapped              spell resolution
//   Evolving Wilds /           1 basic, tapped                         {T}, sac: SacLandSearch
//     Terramorphic Expanse
//   Myriad Landscape           2 basic sharing a land type, tapped     {2},{T}, sac: SacLandSearch
//   Khalni Heart Expedition    2 basic, tapped                         3 quest ctrs + sac: QuestSac...
//   Primeval Titan             2 ANY lands, tapped, optional           ETB + attack trigger
//
// WHY NOT the fetchland collapse (fetch_land_types / PerformFetch): the fetchland never enters the
// battlefield (so its own landfall and the hold line vanish), pays 1 life, filters by SUBTYPE and
// ignores "tapped". WHY NOT Crop Rotation's PerformLandTutorToBattlefield: it puts ONE card through
// EnterLand without the land's own ETB extras (a fetched Karoo bounces nothing) and its target pin is
// inert in resolve mode (the provider's front is taken inside the search window -- the Creature
// Giving item, deliberately not touched here).
//
// THE PICK IS SEARCHED, never greedy (USER hard rule). LandSearchPicks enumerates every legal
// complete pick at RESOLUTION, folded only by two sound identities:
//   * COPIES / BEHAVIOURAL TWINS are one class (CardDefinition::behaviour_identity, which digests the
//     whole cards.json entry minus name and oracle text -- Evolving Wilds == Terramorphic Expanse).
//     Which physical copy is taken is visible only through the post-shuffle library order, i.e. only
//     through clairvoyance; in real-information terms the copies are interchangeable.
//   * "UP TO N" COLLAPSES TO THE MAXIMAL picks (no valid strict superset). Dominance: the extra land
//     is one card removed from an otherwise IDENTICAL future (Library::ShuffleByKey is keyed per card,
//     so removing a card leaves every other card's relative order unchanged), and it arrives now --
//     on the battlefield (a land is never worse to have, against a passive opponent) or in hand
//     (Cultivate: the only cost is a possible cleanup discard, which is itself our choice, and binning
//     that basic is equivalent to not having fetched it, plus one card of library thinning). The
//     human may always stop early or (when optional) decline.
// The provider ORDERS the list (DecisionProvider::LandSearchPickOrder; entry 0 is the default for
// every path without a pin: the d0 runner, rollout leaves beyond the horizon, the human's
// preselection) and the SEARCH picks the index (Plan::land_search_pins for in-plan resolutions,
// Plan::attack_land_search_choice for Titan's attack trigger, Plan::upkeep_land_search_choice for
// Search for Tomorrow off suspend). Nothing here truncates.
//
// LANDFALL. Every battlefield entry goes through EnterLand -- the land-entry chokepoint the landfall
// dispatch hangs off -- once per land. A multi-put of lands only (Myriad, Khalni, Titan) may call it
// sequentially: every landfall payload in the pool commutes with every other, and none creates a
// permanent another landfall trigger could target (the dispatch's own contract). The lands' OWN
// enter triggers (a Gruul Turf's mandatory bounce, Radiant Fountain's 2 life) resolve in a SECOND
// pass, after every land of the put is on the battlefield (CR 603.6a: one simultaneous event), so a
// Titan-fetched Turf may return the Forest fetched beside it.
// =================================================================================================

#include <algorithm>
#include <string>
#include <vector>

// One identity class of legal library lands for a search. `rep` = the lexicographically smallest
// member name (stable across shuffles); `def` = that member's definition.
struct LandSearchClass
{
    std::string           rep;
    std::uint64_t         ident  = 0;   // behaviour_identity; 0 = fold by name only
    int                   copies = 0;
    const CardDefinition* def    = nullptr;
};

// Is library card `d` a legal find for `spec`? "Land card" reads the DEFINITION (a library Card is a
// name-only placeholder with empty masks) -- the FRONT face, which is the card's only face in the
// library (CR 712.8a), so a land-backed MDFC is not a land card here.
inline bool LandSearchLegal(const CardDefinition* d, const LandSearchSpec& spec)
{
    if (d == nullptr || !d->card.IsLand()) { return false; }
    if (spec.basic_only && !IsBasicLandCard(*d)) { return false; }
    return true;
}

// Do two land definitions share at least one land subtype (CR 205.3i -- the land types)?
inline bool LandDefsShareLandType(const CardDefinition* a, const CardDefinition* b)
{
    if (a == nullptr || b == nullptr) { return false; }
    const SubtypeSet& sa = a->card.m_subtypes;
    for (std::size_t i = 0; i < sa.size(); ++i)
    {
        if (b->card.m_subtypes.HasId(sa.IdAt(i))) { return true; }
    }
    return false;
}

inline std::vector<LandSearchClass> LandSearchClasses(const GameState& s, int ctrl,
                                                      const LandSearchSpec& spec)
{
    std::vector<LandSearchClass> cls;
    const Player& pl = s.players[static_cast<std::size_t>(ctrl)];
    for (std::size_t i = 0; i < pl.library.size(); ++i)
    {
        const Card& c = pl.library[i];
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!LandSearchLegal(d, spec)) { continue; }
        const std::string nm = c.m_name.str();
        LandSearchClass* hit = nullptr;
        for (LandSearchClass& k : cls)
        {
            const bool same = d->behaviour_identity != 0
                ? k.ident == d->behaviour_identity
                : (k.ident == 0 && k.rep == nm);
            if (same) { hit = &k; break; }
        }
        if (hit == nullptr)
        {
            LandSearchClass k;
            k.rep = nm; k.ident = d->behaviour_identity; k.copies = 1; k.def = d;
            cls.push_back(std::move(k));
        }
        else
        {
            ++hit->copies;
            if (nm < hit->rep) { hit->rep = nm; hit->def = d; }
        }
    }
    std::sort(cls.begin(), cls.end(),
              [](const LandSearchClass& a, const LandSearchClass& b) { return a.rep < b.rep; });
    return cls;
}

// How many of a pick's lands go to the BATTLEFIELD: "put one onto the battlefield and the other into
// your hand" sends a LONE find to the battlefield (Cultivate ruling 2010-08-15), so the battlefield
// fills first.
inline int LandSearchBattlefieldCount(const LandSearchSpec& spec, int picked)
{
    return std::min(picked, std::max(0, spec.count - spec.to_hand));
}

// Every legal COMPLETE pick, provider-ordered. Each entry is a list of class-representative names:
// the battlefield entries first (sorted), then the hand entries (sorted). See the header for the two
// sound folds. Never empty unless nothing legal exists (then the search simply finds nothing).
inline std::vector<std::vector<std::string>> LandSearchPicks(const GameState& s, int ctrl,
                                                             const LandSearchSpec& spec)
{
    std::vector<std::vector<std::string>> out;
    if (spec.count <= 0) { return out; }
    const std::vector<LandSearchClass> cls = LandSearchClasses(s, ctrl, spec);
    const int n = static_cast<int>(cls.size());
    if (n == 0) { return out; }
    // Multisets of class indices (non-decreasing), size 1..count, respecting copy counts and the
    // shared-type constraint; only MAXIMAL ones are kept.
    std::vector<int> cur;
    std::vector<int> used(static_cast<std::size_t>(n), 0);
    std::vector<std::vector<int>> multisets;
    auto can_add = [&](int c)
    {
        if (used[static_cast<std::size_t>(c)] >= cls[static_cast<std::size_t>(c)].copies) { return false; }
        if (spec.share_land_type)
        {
            for (int m : cur)
            {
                if (!LandDefsShareLandType(cls[static_cast<std::size_t>(m)].def,
                                           cls[static_cast<std::size_t>(c)].def)) { return false; }
            }
        }
        return true;
    };
    // Recursive walk. `start` keeps the multiset non-decreasing so each is generated once.
    auto rec = [&](auto&& self, int start) -> void
    {
        bool extendable = false;
        if (static_cast<int>(cur.size()) < spec.count)
        {
            // Maximality is tested against ANY class (not only >= start), since a superset may add a
            // smaller-index class.
            for (int c = 0; c < n; ++c) { if (can_add(c)) { extendable = true; break; } }
            for (int c = start; c < n; ++c)
            {
                if (!can_add(c)) { continue; }
                cur.push_back(c); ++used[static_cast<std::size_t>(c)];
                self(self, c);
                cur.pop_back(); --used[static_cast<std::size_t>(c)];
            }
        }
        if (!cur.empty() && !extendable) { multisets.push_back(cur); }
    };
    rec(rec, 0);

    // Destinations: every distinct (battlefield part, hand part) split of each multiset.
    for (const std::vector<int>& ms : multisets)
    {
        const int sz  = static_cast<int>(ms.size());
        const int bfn = LandSearchBattlefieldCount(spec, sz);
        std::vector<std::vector<std::string>> splits;
        if (bfn >= sz)
        {
            std::vector<std::string> names;
            for (int c : ms) { names.push_back(cls[static_cast<std::size_t>(c)].rep); }
            std::sort(names.begin(), names.end());
            splits.push_back(std::move(names));
        }
        else
        {
            // Choose which members go to hand (combinations of positions), dedup by the sorted parts.
            std::vector<int> sel(static_cast<std::size_t>(sz), 0);
            std::fill(sel.end() - (sz - bfn), sel.end(), 1);   // 1 = hand
            do
            {
                std::vector<std::string> bf, hand;
                for (int i = 0; i < sz; ++i)
                {
                    (sel[static_cast<std::size_t>(i)] ? hand : bf)
                        .push_back(cls[static_cast<std::size_t>(ms[static_cast<std::size_t>(i)])].rep);
                }
                std::sort(bf.begin(), bf.end());
                std::sort(hand.begin(), hand.end());
                std::vector<std::string> v = bf;
                v.insert(v.end(), hand.begin(), hand.end());
                if (std::find(splits.begin(), splits.end(), v) == splits.end()) { splits.push_back(std::move(v)); }
            } while (std::next_permutation(sel.begin(), sel.end()));
        }
        for (auto& v : splits) { out.push_back(std::move(v)); }
    }
    ResolveProvider(s).LandSearchPickOrder(s, ctrl, spec.count, spec.to_hand, out);
    return out;
}

// The HUMAN's k-th prompt: every legal NAME still available given the earlier picks (copies left;
// the shared land type with every earlier pick). Real names, never the identity fold -- a human sees
// Evolving Wilds and Terramorphic Expanse as the two cards they are. Sorted by name so the offered
// list never leaks the hidden library order.
inline std::vector<std::string> LandSearchHumanCandidates(const GameState& s, int ctrl,
                                                          const LandSearchSpec& spec,
                                                          const std::vector<std::string>& chosen)
{
    const Player& pl = s.players[static_cast<std::size_t>(ctrl)];
    std::vector<std::string> out;
    for (std::size_t i = 0; i < pl.library.size(); ++i)
    {
        const Card& c = pl.library[i];
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!LandSearchLegal(d, spec)) { continue; }
        const std::string nm = c.m_name.str();
        if (std::find(out.begin(), out.end(), nm) != out.end()) { continue; }
        int in_lib = 0;
        for (std::size_t j = 0; j < pl.library.size(); ++j) { if (pl.library[j].m_name == nm) { ++in_lib; } }
        const int taken = static_cast<int>(std::count(chosen.begin(), chosen.end(), nm));
        if (in_lib - taken <= 0) { continue; }
        if (spec.share_land_type)
        {
            bool ok = true;
            for (const std::string& o : chosen)
            {
                if (!LandDefsShareLandType(CardDatabase::Instance().Lookup(o), d)) { ok = false; break; }
            }
            if (!ok) { continue; }
        }
        out.push_back(nm);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Take one card named `name` out of `pl`'s library (the first copy); failing that, a behavioural twin
// of it (the identity class the autonomous pick was folded over). False if neither is left.
inline bool TakeLandFromLibrary(Player& pl, const std::string& name, Card& out)
{
    for (std::size_t i = 0; i < pl.library.size(); ++i)
    {
        if (pl.library[i].m_name == name)
        {
            out = pl.library[i];
            pl.library.erase(pl.library.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    const CardDefinition* want = CardDatabase::Instance().Lookup(name);
    if (want == nullptr || want->behaviour_identity == 0) { return false; }
    for (std::size_t i = 0; i < pl.library.size(); ++i)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(pl.library[i]);
        if (d != nullptr && d->behaviour_identity == want->behaviour_identity)
        {
            out = pl.library[i];
            pl.library.erase(pl.library.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

// THE land-search resolution. `pin` = the searched pick index (-1 = the provider's front). Shared by
// every world: the executor (EffectHandler / AIEngine via ApplyPermAbility / GameEngine combat and
// upkeep), the rollout (apply_one / SimulateCombat / SimulateEndAndStartNextTurn) and human play.
//
// HUMAN PLAY (g_play_tutor_chooser set -- null in every search / rollout / enumeration scope, so the
// autonomous engine never reaches it): one `tutor_etb` prompt PER LAND, in destination order, each
// over the live legal names given the earlier picks; -1 on the first prompt of an optional search
// DECLINES it (no shuffle -- nothing was searched), any other -1 stops early (a search for cards with
// a stated quality may fail to find, CR 701.19b). The default is the autonomous pick.
inline void PerformLandSearch(GameState& state, int controller, const LandSearchSpec& spec,
                              const std::string& source, int pin)
{
    if (spec.count <= 0) { return; }
    Player& pl = state.players[static_cast<std::size_t>(controller)];
    std::vector<std::string> chosen;   // battlefield entries first, then hand entries
    int bf_n = 0;
    if (g_play_tutor_chooser)
    {
        const std::vector<std::vector<std::string>> picks = LandSearchPicks(state, controller, spec);
        const std::vector<std::string> def_pick = picks.empty() ? std::vector<std::string>{}
                                                                : picks.front();
        const int bf_slots = std::max(0, spec.count - spec.to_hand);
        for (int k = 0; k < spec.count; ++k)
        {
            const std::vector<std::string> cands =
                LandSearchHumanCandidates(state, controller, spec, chosen);
            if (cands.empty()) { break; }
            int def_idx = 0;
            if (k < static_cast<int>(def_pick.size()))
            {
                auto it = std::find(cands.begin(), cands.end(), def_pick[static_cast<std::size_t>(k)]);
                if (it != cands.end()) { def_idx = static_cast<int>(it - cands.begin()); }
            }
            std::string label = source + " -- " + (spec.basic_only ? "basic land" : "land");
            if (k < bf_slots) { label += spec.tapped ? " onto the battlefield tapped" : " onto the battlefield"; }
            else              { label += " into your hand"; }
            if (spec.count > 1)
            { label += " (" + std::to_string(k + 1) + " of up to " + std::to_string(spec.count) + ")"; }
            const int got = (*g_play_tutor_chooser)(state, controller, label, cands, def_idx, {});
            if (got < 0 || got >= static_cast<int>(cands.size()))
            {
                if (k == 0 && spec.optional) { return; }   // declined "you may search": no shuffle
                break;                                     // stop early ("up to" / fail to find)
            }
            chosen.push_back(cands[static_cast<std::size_t>(got)]);
        }
        bf_n = LandSearchBattlefieldCount(spec, static_cast<int>(chosen.size()));
    }
    else
    {
        const std::vector<std::vector<std::string>> picks = LandSearchPicks(state, controller, spec);
        if (!picks.empty())
        {
            const std::size_t k = pin < 0 ? 0
                : std::min<std::size_t>(static_cast<std::size_t>(pin), picks.size() - 1);
            chosen = picks[k];
        }
        bf_n = LandSearchBattlefieldCount(spec, static_cast<int>(chosen.size()));
    }

    // PASS 1 -- every battlefield land enters (one simultaneous event, CR 603.6a). EnterLand without
    // the extras: the land's own enter triggers wait for pass 2.
    std::vector<int>                   bf_nums;
    std::vector<const CardDefinition*> bf_defs;
    std::string bf_text, hand_text;
    for (int i = 0; i < bf_n; ++i)
    {
        Card lc;
        if (!TakeLandFromLibrary(pl, chosen[static_cast<std::size_t>(i)], lc)) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(lc);
        if (d == nullptr) { continue; }   // unreachable: legality read the definition
        EnterLand(state, *d, lc.m_number, /*resolve_full_etb=*/false, /*fire_landfall=*/true,
                  spec.tapped ? LandEntryTap::ForcedTapped : LandEntryTap::Natural);
        // Forbidden Orchard's on-play spawn, mirrored as Crop Rotation does -- only for an UNTAPPED
        // entry (it models "tapped for mana this turn"); a forced-tapped Orchard taps next turn and
        // the turn-start spawn covers it. Unreachable for every basic-only search.
        if (!spec.tapped && IsForbiddenOrchard(d)) { SpawnOpponentSpirit(state); }
        bf_nums.push_back(lc.m_number);
        bf_defs.push_back(d);
        bf_text += (bf_text.empty() ? "" : ", ") + lc.m_name.str();
    }
    // PASS 2 -- the put lands' own enter triggers (Karoo bounce, land lifegain), after all of them are
    // on the battlefield. A trigger still resolves for a land that has already left (an earlier
    // Karoo returned it): the lifegain does, the bounce (which needs its own source on the
    // battlefield to pick "a land you control" sensibly) is skipped -- no pool card can reach that.
    for (std::size_t i = 0; i < bf_nums.size(); ++i)
    {
        int slot = -1;
        for (int j = 0; j < static_cast<int>(state.battlefield.size()); ++j)
        {
            if (state.battlefield[static_cast<std::size_t>(j)].card.m_number == bf_nums[i]
                && state.battlefield[static_cast<std::size_t>(j)].controller_index == controller)
            { slot = j; break; }
        }
        if (slot >= 0) { ResolveLandEnterEtbExtras(state, *bf_defs[i], slot); }
        else if (bf_defs[i]->params.etb_lifegain > 0)
        { GainLife(state, state.active_player_index, bf_defs[i]->params.etb_lifegain); }
    }
    // Hand entries (Cultivate). EnterHand(Tutor) arms the put-in-hand breakpoint (site 10), so the
    // fetched basic can still be the turn's land drop.
    for (std::size_t i = static_cast<std::size_t>(bf_n); i < chosen.size(); ++i)
    {
        Card lc;
        if (!TakeLandFromLibrary(pl, chosen[i], lc)) { continue; }
        hand_text += (hand_text.empty() ? "" : ", ") + lc.m_name.str();
        EnterHand(state, controller, std::move(lc), HandEntryReason::Tutor);
    }
    if (g_play_event_sink && !g_tap_speculating)
    {
        std::string t = "\xF0\x9F\x94\x8E " + source + ": ";
        if (bf_text.empty() && hand_text.empty()) { t += "found nothing"; }
        if (!bf_text.empty())
        { t += bf_text + (spec.tapped ? " onto the battlefield tapped" : " onto the battlefield"); }
        if (!hand_text.empty()) { t += (bf_text.empty() ? "" : "; ") + hand_text + " into hand (revealed)"; }
        EmitPlayEvent(state.turn_number, "tutor", t);
    }
    // "...then shuffle" -- exactly ONCE, also on a fail-to-find (CR 701.19).
    ShuffleAfterSearch(state, controller);
}

// The two activated land searches (PermAbilityMode::SacLandSearch / QuestSacLandSearch), called from
// ApplyPermAbility AFTER the caller paid the mana and the {T}. The rest of the COST is paid here,
// before the effect (CR 602.2b / 601.2h): remove the quest counters, then SACRIFICE the source --
// graveyard (a token would cease to exist), erase, detach (CR 301.5c), the sacrifice watchers (a
// sacrificed land/enchantment is not a creature dying: no OnCreatureDies, the Blighted Steppe
// resolver's pattern). Then the search, with the in-plan pin.
// The explicit-pin core: the in-plan activation consumes the in-plan pin list (below); the in-combat
// Khalni crack (FireCombatLandSearchCracks) passes the next ordinal of the packed attack stream.
inline void ApplyLandSearchActivationPinned(GameState& state, int controller, int src_idx,
                                            const CardDefinition& def, int pin)
{
    const CardParams& p = def.params;
    if (src_idx < 0 || src_idx >= static_cast<int>(state.battlefield.size())) { return; }
    const std::string src_name = state.battlefield[static_cast<std::size_t>(src_idx)].card.m_name.str();
    if (p.land_search_activation_remove_quest > 0)
    {
        Permanent& q = state.battlefield[static_cast<std::size_t>(src_idx)];
        if (q.quest_counters < p.land_search_activation_remove_quest) { return; }   // stale plan: cannot pay
        q.quest_counters -= p.land_search_activation_remove_quest;   // a cost is never doubled
    }
    if (p.land_search_activation_sacrifice_self)
    {
        const Permanent& sq  = state.battlefield[static_cast<std::size_t>(src_idx)];
        const int        num = sq.card.m_number;
        const Card       dead = sq.card;
        const bool       tok  = sq.is_token;
        state.battlefield.erase(state.battlefield.begin() + src_idx);
        for (Permanent& o : state.battlefield)
        {
            if (o.equipped_to      == num) { o.equipped_to      = 0; }
            if (o.aura_attached_to == num) { o.aura_attached_to = 0; }
        }
        if (!tok) { state.players[static_cast<std::size_t>(controller)].graveyard.push_back(dead); }
        if (g_play_event_sink && !g_tap_speculating)
        { EmitPlayEvent(state.turn_number, "sacrifice", "\xF0\x9F\x94\xAA " + src_name + " -- sacrificed (cost)"); }
        FireSacrificeWatchers(state, controller);
    }
    PerformLandSearch(state, controller, LandSearchSpecOf(p), src_name, pin);
}
inline void ApplyLandSearchActivation(GameState& state, int controller, int src_idx,
                                      const CardDefinition& def)
{
    const int pin = ConsumeScriptedLandSearchPin();   // the activation holds its ordinal regardless
    ApplyLandSearchActivationPinned(state, controller, src_idx, def, pin);
}

// Primeval Titan's ATTACK half ("whenever this creature enters OR ATTACKS"), at the CR 508.4 point
// in BOTH worlds (GameEngine::CombatPhase and TurnSolver's SimulateCombat call it right after
// ApplyAttackModalTriggers, before the attack-token block): once per DECLARED attacker carrying
// land_search_on_enter_or_attack, before any damage is read, so the landfall payloads of the fetched
// lands (Avenger's counters, Phylath's +4) land on the attacking Plants THIS combat. The pick is the
// packed GameState::scripted_attack_land_search (one ordinal per attacking Titan, in attacker
// order). A fetched Gruul Turf's bounce ERASES a battlefield entry, so `atk_idx` is rebuilt by card
// number afterwards (the FireAttackSacFling contract). Param-gated: inert for every other deck.
inline void FireAttackLandSearch(GameState& state, int controller, std::vector<int>& atk_idx)
{
    if (!state.deck_has_land_search) { return; }   // deck stamp: no land-search source can exist
    std::vector<int> nums;
    nums.reserve(atk_idx.size());
    bool any = false;
    for (int i : atk_idx)
    {
        if (i < 0 || i >= static_cast<int>(state.battlefield.size())) { nums.push_back(0); continue; }
        const Permanent& a = state.battlefield[static_cast<std::size_t>(i)];
        nums.push_back(a.card.m_number);
        if (a.def_absent || a.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(a.card);
        if (d != nullptr && d->params.land_search_on_enter_or_attack && d->params.land_search_count > 0)
        { any = true; }
    }
    if (!any) { return; }
    for (const int num : nums)
    {
        int slot = -1;
        for (int j = 0; j < static_cast<int>(state.battlefield.size()); ++j)
        {
            if (state.battlefield[static_cast<std::size_t>(j)].card.m_number == num
                && state.battlefield[static_cast<std::size_t>(j)].controller_index == controller)
            { slot = j; break; }
        }
        if (slot < 0) { continue; }
        const CardDefinition* d =
            CardDatabase::Instance().LookupCached(state.battlefield[static_cast<std::size_t>(slot)].card);
        if (d == nullptr || !d->params.land_search_on_enter_or_attack || d->params.land_search_count <= 0)
        { continue; }
        const int pin = ConsumePackedLandSearchPin(state.scripted_attack_land_search);
        PerformLandSearch(state, controller, LandSearchSpecOf(d->params),
                          d->card.m_name.str() + " (attack)", pin);
    }
    // Repair the attacker indices by number (a Karoo bounce erased a slot; appended lands shifted
    // nothing before them, but the erase may have).
    std::vector<int> fixed;
    fixed.reserve(nums.size());
    for (const int num : nums)
    {
        for (int j = 0; j < static_cast<int>(state.battlefield.size()); ++j)
        {
            if (state.battlefield[static_cast<std::size_t>(j)].card.m_number == num)
            { fixed.push_back(j); break; }
        }
    }
    atk_idx = std::move(fixed);
}

// Is `p` a counter-costed land-search source (Khalni Heart Expedition) -- and is it crackable NOW?
inline bool IsQuestLandSearchSource(const Permanent& p, const CardDefinition*& d_out)
{
    d_out = nullptr;
    if (p.def_absent) { return false; }
    const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
    if (d == nullptr || d->params.land_search_activation_remove_quest <= 0 || d->params.land_search_count <= 0)
    { return false; }
    d_out = d;
    return true;
}

// THE IN-COMBAT KHALNI CRACK (2026-10-15; follow-up (a) of the land-search design). Khalni Heart
// Expedition's "Remove three quest counters ... and sacrifice it: search for two basics" has no timing
// restriction, so after Primeval Titan's attack trigger resolves (its two lands' landfalls may have
// filled an Expedition) the active player gets priority in the declare-attackers step and may crack it
// BEFORE damage -- two more landfalls, whose payloads (Avenger's +1/+1 on every Plant, Phylath's +4)
// land on the ATTACKING Plants. A main-2 crack can never reach this combat, so without this window the
// line was unreachable at any budget.
//
// SEARCHED, never greedy: GameState::scripted_combat_land_search_crack (Plan::combat_crack_choice) is a
// BITMASK over the Khalni-type permanents in battlefield order at this point; bit i cracks the i-th one
// if it is eligible (counters at the count) when reached -- ascending, so an Expedition filled by an
// earlier crack's landfalls in the same window is still crackable. Cracking is weakly better than a
// main-2 crack but NOT than holding it for a later turn's payoff, so not cracking (no pin) stays a line.
// The cracks' land picks continue the packed ATTACK stream (after the Titans' ordinals). Human play
// asks through the shared `dig` chooser (the Arcanum Wings in-combat window's shape): the eligible
// Expeditions, -1 = do not crack, re-asked after each crack. Called right after FireAttackLandSearch in
// BOTH combat worlds and inside its PendingAttackDamage projection twin; repairs atk_idx by number.
inline void FireCombatLandSearchCracks(GameState& state, int controller, std::vector<int>& atk_idx)
{
    if (!state.deck_has_land_search) { return; }
    const int mask = state.scripted_combat_land_search_crack;
    state.scripted_combat_land_search_crack = -1;   // consumed whether or not it fires
    const bool human = g_play_dig_chooser != nullptr && !g_tap_speculating;
    if (mask <= 0 && !human) { return; }
    std::vector<int> atk_nums;
    atk_nums.reserve(atk_idx.size());
    for (int i : atk_idx)
    {
        atk_nums.push_back((i >= 0 && i < static_cast<int>(state.battlefield.size()))
                               ? state.battlefield[static_cast<std::size_t>(i)].card.m_number : 0);
    }
    // The Khalni-type permanents in battlefield order (the mask's index space).
    std::vector<int> order;
    for (const Permanent& p : state.battlefield)
    {
        const CardDefinition* d = nullptr;
        if (p.controller_index == controller && IsQuestLandSearchSource(p, d)) { order.push_back(p.card.m_number); }
    }
    if (order.empty()) { return; }
    auto crack = [&](int num) -> bool
    {
        for (int j = 0; j < static_cast<int>(state.battlefield.size()); ++j)
        {
            const Permanent& p = state.battlefield[static_cast<std::size_t>(j)];
            if (p.card.m_number != num || p.controller_index != controller) { continue; }
            const CardDefinition* d = nullptr;
            if (!IsQuestLandSearchSource(p, d)) { return false; }
            if (p.quest_counters < d->params.land_search_activation_remove_quest) { return false; }
            ApplyLandSearchActivationPinned(state, controller, j, *d,
                                            ConsumePackedLandSearchPin(state.scripted_attack_land_search));
            return true;
        }
        return false;
    };
    bool any = false;
    if (!human)
    {
        for (int i = 0; i < static_cast<int>(order.size()) && i < 30; ++i)
        { if ((mask >> i) & 1) { any = crack(order[static_cast<std::size_t>(i)]) || any; } }
    }
    else
    {
        for (;;)
        {
            std::vector<Card> examined;
            std::vector<int>  nums;
            for (const Permanent& p : state.battlefield)
            {
                const CardDefinition* d = nullptr;
                if (p.controller_index != controller || !IsQuestLandSearchSource(p, d)) { continue; }
                if (p.quest_counters < d->params.land_search_activation_remove_quest) { continue; }
                examined.push_back(p.card);
                nums.push_back(p.card.m_number);
            }
            if (examined.empty()) { break; }
            std::vector<int> legal(examined.size());
            for (std::size_t k = 0; k < legal.size(); ++k) { legal[k] = static_cast<int>(k); }
            const int picked = (*g_play_dig_chooser)(
                state, controller,
                "Khalni Heart Expedition -- crack it NOW, before combat damage (-1 = keep it)",
                examined, legal, 0);
            if (picked < 0 || picked >= static_cast<int>(nums.size())) { break; }
            if (!crack(nums[static_cast<std::size_t>(picked)])) { break; }
            any = true;
        }
    }
    if (!any) { return; }
    std::vector<int> fixed;
    fixed.reserve(atk_nums.size());
    for (const int num : atk_nums)
    {
        for (int j = 0; j < static_cast<int>(state.battlefield.size()); ++j)
        {
            if (state.battlefield[static_cast<std::size_t>(j)].card.m_number == num)
            { fixed.push_back(j); break; }
        }
    }
    atk_idx = std::move(fixed);
}
