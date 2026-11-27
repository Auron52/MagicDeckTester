// CONTROL-EFFECT INJECTION (core/ControlInject.h; the interface spec is docs/design/control-inject-harness.md).
// Every effect goes through the engine's own zone-change routes, so the goldfisher's watchers see it as the rules
// say they must: a destroyed creature DIES (OnCreatureDies: leaves-the-battlefield, dies watchers, persist /
// undying), a destroyed noncreature LEAVES (graveyard-entry and leave triggers: an Oblivion Ring returns its card),
// an exiled or bounced permanent leaves without dying, a discard is the shared discard hook (madness, Curator), a
// countered spell goes to the zone the counter names (CR 701.6a), with flashback's exile replacement (CR 702.34a)
// and a copy ceasing to exist (CR 707.10a). Nothing here runs unless a batch job armed it (Install).
#include "ControlInject.h"

#include "GameState.h"
#include "HandEntry.h"
#include "SpellEffects.h"
#include "../cards/CardDatabase.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace ctrlinject
{
namespace
{
using ojson = nlohmann::ordered_json;

// Per-round record of the real game (index = round).
struct RoundRec
{
    bool                     seen        = false;   // a goldfisher turn of this round ended (OnTurnEnd)
    bool                     skipped     = false;
    int                      life        = 0;       // the passive opponent's life at the end of the round
    int                      poison      = 0;
    int                      opp_library = 0;
    int                      spells_cast = 0;       // GameState::spells_cast_this_turn at the end of the turn
    int                      life_gain   = 0;       // GameCtx::life_delta at that point (the `life` effects so far)
    std::vector<std::string> spells;               // the goldfisher's spells reaching resolution this round
};

struct PendingReturn
{
    int         number = 0;
    int         owner  = 0;
    int         ret    = 0;
    int         effect = 0;
    std::string name;
};

struct GameCtx
{
    Spec                           spec;
    bool                           record     = false;
    std::string                    job;
    int                            gi         = -1;
    int                            start_life = 0;
    int                            life_delta = 0;    // net change of the opponent's life by `life` effects
    std::vector<RoundRec>          rounds;
    std::vector<PendingReturn>     returns;
    std::vector<std::vector<char>> hit;               // [effect][round]: applied (counters / tap) that round
    std::vector<int>               finalized;         // [effect]: counters logged through this round
    std::vector<char>              tax_on;            // [effect]: the tax is currently in force
};

thread_local GameCtx t_ctx;
thread_local bool    t_armed = false;

// ---- parsing -------------------------------------------------------------------------------------------------
std::string Trim(const std::string& s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) { ++a; }
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) { --b; }
    return s.substr(a, b - a);
}

bool ParseInt(const std::string& s, int& out)
{
    if (s.empty()) { return false; }
    std::size_t i = 0;
    bool neg = false;
    if (s[0] == '-') { neg = true; i = 1; }
    if (i >= s.size()) { return false; }
    long v = 0;
    for (; i < s.size(); ++i)
    {
        if (s[i] < '0' || s[i] > '9') { return false; }
        v = v * 10 + (s[i] - '0');
        if (v > 100000) { return false; }
    }
    out = static_cast<int>(neg ? -v : v);
    return true;
}

[[noreturn]] void Bad(const std::string& effect, const std::string& why)
{
    throw std::runtime_error("ctrl_inject: \"" + effect + "\": " + why);
}

bool ParseClass(const std::string& t, Class& c)
{
    if (t == "perm" || t == "permanent") { c = Class::Perm; return true; }
    if (t == "card" || t == "spell" || t == "any") { c = Class::Card; return true; }
    if (t == "land")        { c = Class::Land; return true; }
    if (t == "creature")    { c = Class::Creature; return true; }
    if (t == "nonland")     { c = Class::Nonland; return true; }
    if (t == "artifact")    { c = Class::Artifact; return true; }
    if (t == "enchantment") { c = Class::Enchantment; return true; }
    if (t == "noncreature") { c = Class::Noncreature; return true; }
    return false;
}

// A selector token: <i> | big | all | mv>=N | name=<Card Name>.
bool ParseSel(const std::string& t, Effect& e)
{
    int v = 0;
    if (ParseInt(t, v))
    {
        if (v < 0) { return false; }
        e.sel = Sel::Index; e.index = v; return true;
    }
    if (t == "big") { e.sel = Sel::Big; return true; }
    if (t == "all") { e.sel = Sel::All; return true; }
    if (t.rfind("mv>=", 0) == 0 && ParseInt(t.substr(4), v) && v >= 0) { e.sel = Sel::MvAtLeast; e.mv = v; return true; }
    if (t.rfind("name=", 0) == 0 && t.size() > 5) { e.sel = Sel::Name; e.name = t.substr(5); return true; }
    return false;
}

Effect ParseOne(const std::string& text)
{
    // Split on ':' -- except that a name= token takes the rest of the effect verbatim (a card name may hold ':').
    std::vector<std::string> tok;
    std::size_t at = 0;
    while (true)
    {
        const std::size_t c = text.find(':', at);
        std::string piece = Trim(text.substr(at, c == std::string::npos ? std::string::npos : c - at));
        if (piece.rfind("name=", 0) == 0) { tok.push_back(Trim(text.substr(at))); break; }
        tok.push_back(piece);
        if (c == std::string::npos) { break; }
        at = c + 1;
    }
    Effect e;
    e.text = text;
    const std::string k = tok[0];
    // Kind, and the prototype's aliases (kept so the probe's arms read the same).
    bool board = false, hand = false, spell = false;
    if      (k == "skip")            { e.kind = Kind::Skip; }
    else if (k == "destroy")         { e.kind = Kind::Destroy;    board = true; }
    else if (k == "exile")           { e.kind = Kind::Exile;      board = true; }
    else if (k == "exile_until")     { e.kind = Kind::ExileUntil; board = true; }
    else if (k == "bounce")          { e.kind = Kind::Bounce;     board = true; }
    else if (k == "tap")             { e.kind = Kind::Tap;        board = true; }
    else if (k == "land")            { e.kind = Kind::Destroy;    board = true; e.cls = Class::Land; }
    else if (k == "creature")        { e.kind = Kind::Destroy;    board = true; e.cls = Class::Creature; }
    else if (k == "nonland")         { e.kind = Kind::Destroy;    board = true; e.cls = Class::Nonland; }
    else if (k == "bigcrt")          { e.kind = Kind::Destroy;    board = true; e.cls = Class::Creature; e.sel = Sel::Big; }
    else if (k == "sweep")           { e.kind = Kind::Destroy;    board = true; e.cls = Class::Creature; e.sel = Sel::All; }
    else if (k == "discard")         { e.kind = Kind::Discard;    hand = true;  e.cls = Class::Card; }
    else if (k == "bigdisc")         { e.kind = Kind::Discard;    hand = true;  e.cls = Class::Nonland; e.sel = Sel::Big; }
    else if (k == "tax")             { e.kind = Kind::Tax; }
    else if (k == "life")            { e.kind = Kind::Life; }
    else if (k == "counter")         { e.kind = Kind::Counter;       spell = true; e.cls = Class::Card; }
    else if (k == "counter_to_hand") { e.kind = Kind::CounterToHand; spell = true; e.cls = Class::Card; }
    else if (k == "counter_to_top")  { e.kind = Kind::CounterToTop;  spell = true; e.cls = Class::Card; }
    else { Bad(text, "unknown kind \"" + k + "\""); }
    if (tok.size() < 2 || tok[1].empty()) { Bad(text, "missing round"); }
    {
        const std::string& r = tok[1];
        const std::size_t dash = r.find('-');
        int a = 0, b = 0;
        if (dash == std::string::npos) { if (!ParseInt(r, a)) { Bad(text, "bad round \"" + r + "\""); } b = a; }
        else if (!ParseInt(r.substr(0, dash), a) || !ParseInt(r.substr(dash + 1), b)) { Bad(text, "bad rounds \"" + r + "\""); }
        if (a < 1 || b < a) { Bad(text, "rounds must satisfy 1 <= from <= to"); }
        e.from = a; e.to = b;
    }
    bool have_amount = false, have_class = false;
    for (std::size_t i = 2; i < tok.size(); ++i)
    {
        const std::string& t = tok[i];
        int v = 0;
        if (e.kind == Kind::Tax || e.kind == Kind::Life)
        {
            if (ParseInt(t, v) && !have_amount) { e.amount = v; have_amount = true; continue; }
            if (e.kind == Kind::Tax)
            {
                if (t == "all")         { e.tax_class = TaxClass::All; continue; }
                if (t == "noncreature") { e.tax_class = TaxClass::Noncreature; continue; }
                if (t == "creature")    { e.tax_class = TaxClass::Creature; continue; }
            }
            Bad(text, "unexpected field \"" + t + "\"");
        }
        if (e.kind == Kind::Skip) { Bad(text, "skip takes no field"); }
        if (t.rfind("ret=", 0) == 0 && e.kind == Kind::ExileUntil && ParseInt(t.substr(4), v)) { e.ret = v; continue; }
        Class c;
        if (!have_class && ParseClass(t, c)) { e.cls = c; have_class = true; continue; }
        if (ParseSel(t, e)) { continue; }
        Bad(text, "unexpected field \"" + t + "\"");
    }
    if (e.kind == Kind::Tax)
    {
        if (!have_amount || e.amount < 1) { Bad(text, "tax needs an amount >= 1"); }
        if (e.amount > (e.tax_class == TaxClass::All ? 255 : 15)) { Bad(text, "tax amount too large"); }
    }
    if (e.kind == Kind::Life && (!have_amount || e.amount < 1)) { Bad(text, "life needs an amount >= 1"); }
    if (e.kind == Kind::ExileUntil && e.ret <= e.to) { Bad(text, "exile_until needs ret=<round> after its last round"); }
    if (spell && e.sel == Sel::Big) { Bad(text, "a counter cannot use `big` (the round's later spells are unknown)"); }
    if (hand && e.cls == Class::Perm) { e.cls = Class::Card; }
    if ((board) && e.cls == Class::Card) { e.cls = Class::Perm; }
    return e;
}

// ---- state helpers -------------------------------------------------------------------------------------------
const Card& Typed(const Card& c)
{
    const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
    return d != nullptr ? d->card : c;
}

bool PermIsCreature(const Permanent& p) { return p.card.IsCreature() || p.is_animated; }

bool PermMatches(const Permanent& p, Class c)
{
    switch (c)
    {
    case Class::Perm: case Class::Card: return true;
    case Class::Land:        return p.card.IsLand();
    case Class::Creature:    return PermIsCreature(p);
    case Class::Nonland:     return !p.card.IsLand();
    case Class::Artifact:    return p.card.HasType(CardType::Artifact);
    case Class::Enchantment: return p.card.IsEnchantment();
    case Class::Noncreature: return !PermIsCreature(p);
    }
    return false;
}

bool CardMatches(const Card& typed, Class c)
{
    switch (c)
    {
    case Class::Perm: case Class::Card: return true;
    case Class::Land:        return typed.IsLand();
    case Class::Creature:    return typed.IsCreature();
    case Class::Nonland:     return !typed.IsLand();
    case Class::Artifact:    return typed.HasType(CardType::Artifact);
    case Class::Enchantment: return typed.IsEnchantment();
    case Class::Noncreature: return !typed.IsCreature();
    }
    return false;
}

bool InRounds(const Effect& e, int r) { return r >= e.from && r <= e.to; }

RoundRec& Rec(int r)
{
    if (r < 0) { r = 0; }
    if (static_cast<int>(t_ctx.rounds.size()) <= r) { t_ctx.rounds.resize(static_cast<std::size_t>(r) + 1); }
    return t_ctx.rounds[static_cast<std::size_t>(r)];
}

char& Hit(int k, int r)
{
    auto& v = t_ctx.hit[static_cast<std::size_t>(k)];
    if (static_cast<int>(v.size()) <= r) { v.resize(static_cast<std::size_t>(r) + 1, 0); }
    return v[static_cast<std::size_t>(r)];
}

// The opponent-state features of docs/design/control-scoring-mode.md section 7 (stage 1), read off the
// goldfisher (the gauntlet deck being disrupted) at the moment the effect meets it.
ojson Features(const GameState& s)
{
    const int me = s.active_player_index;
    int lands_bf = 0, creatures = 0, nonland_perms = 0, untapped_lands = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != me) { continue; }
        if (p.card.IsLand()) { ++lands_bf; if (!p.tapped) { ++untapped_lands; } }
        else { ++nonland_perms; }
        if (PermIsCreature(p)) { ++creatures; }
    }
    int lands_hand = 0, nonland_hand = 0, staged = 0;
    const Player& pl = s.players[static_cast<std::size_t>(me)];
    for (const Card& c : pl.hand)
    {
        if (c.m_is_staged) { ++staged; continue; }
        if (Typed(c).IsLand()) { ++lands_hand; } else { ++nonland_hand; }
    }
    const int r = s.turn_number;
    int spells_round = 0, spells_prev = 0;
    if (r >= 0 && r < static_cast<int>(t_ctx.rounds.size()))
    { spells_round = static_cast<int>(t_ctx.rounds[static_cast<std::size_t>(r)].spells.size()); }
    if (r - 1 >= 0 && r - 1 < static_cast<int>(t_ctx.rounds.size()))
    {
        const RoundRec& pr = t_ctx.rounds[static_cast<std::size_t>(r - 1)];
        spells_prev = pr.skipped ? 0 : pr.spells_cast;
    }
    ojson f;
    f["lands_bf"]          = lands_bf;
    f["untapped_lands"]    = untapped_lands;
    f["lands_hand"]        = lands_hand;
    f["creatures"]         = creatures;
    f["nonland_perms"]     = nonland_perms;
    f["nonland_hand"]      = nonland_hand;
    f["hand"]              = static_cast<int>(pl.hand.size()) - staged;
    f["staged"]            = staged + static_cast<int>(pl.staged_cards.size());
    f["library"]           = static_cast<int>(pl.library.size());
    f["spells_this_round"] = spells_round;
    f["spells_last_round"] = spells_prev;
    f["opp_life"]          = s.Opponent().life;
    return f;
}

void Emit(const char* tag, const ojson& j)
{
    const std::string line = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    std::fprintf(stderr, "[%s] %s\n", tag, line.c_str());
}

ojson Head(const GameState& s, int k, const Effect& e)
{
    ojson j;
    j["job"]    = t_ctx.job;
    j["gi"]     = t_ctx.gi;
    j["round"]  = s.turn_number;
    j["effect"] = e.text;
    j["n"]      = k;
    j["kind"]   = KindName(e.kind);
    return j;
}

void PlayEvent(const GameState& s, const std::string& text)
{
    if (g_play_event_sink && !g_tap_speculating) { EmitPlayEvent(s.turn_number, "ctrl_inject", "\xE2\x9B\x94 " + text); }
}

// One candidate, for selection and the logged candidate list.
struct Cand
{
    int         key   = 0;   // battlefield m_number / hand m_number
    std::string name;
    int         mv    = 0;
    int         power = 0;
};

std::vector<int> Choose(const Effect& e, const std::vector<Cand>& c)
{
    std::vector<int> out;
    switch (e.sel)
    {
    case Sel::Index:
        if (e.index < static_cast<int>(c.size())) { out.push_back(e.index); }
        break;
    case Sel::All:
        for (int i = 0; i < static_cast<int>(c.size()); ++i) { out.push_back(i); }
        break;
    case Sel::Big:
    {
        int best = -1;
        for (int i = 0; i < static_cast<int>(c.size()); ++i)
        {
            const int v = e.cls == Class::Creature ? c[static_cast<std::size_t>(i)].power : c[static_cast<std::size_t>(i)].mv;
            if (best < 0 || v > (e.cls == Class::Creature ? c[static_cast<std::size_t>(best)].power
                                                          : c[static_cast<std::size_t>(best)].mv))
            { best = i; }
        }
        if (best >= 0) { out.push_back(best); }
        break;
    }
    case Sel::MvAtLeast:
        for (int i = 0; i < static_cast<int>(c.size()); ++i)
        { if (c[static_cast<std::size_t>(i)].mv >= e.mv) { out.push_back(i); break; } }
        break;
    case Sel::Name:
        for (int i = 0; i < static_cast<int>(c.size()); ++i)
        { if (c[static_cast<std::size_t>(i)].name == e.name) { out.push_back(i); break; } }
        break;
    }
    return out;
}

ojson Names(const std::vector<Cand>& c)
{
    ojson a = ojson::array();
    for (const Cand& x : c) { a.push_back(x.name); }
    return a;
}

int FindBf(const GameState& s, int number)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == number) { return i; } }
    return -1;
}

// The goldfisher's permanents of the effect's class. A single-target effect TARGETS, so shroud and hexproof
// (the control player is an opponent, CR 702.11b / 702.18a) make a permanent no candidate; an `all` effect does
// not target and takes them too. `untapped_only`: the tap effect's candidates.
std::vector<Cand> BoardCands(const GameState& s, const Effect& e, bool untapped_only)
{
    std::vector<Cand> out;
    const int me = s.active_player_index;
    const bool targeted = e.sel != Sel::All;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != me || !PermMatches(p, e.cls)) { continue; }
        if (untapped_only && p.tapped) { continue; }
        if (targeted && (PermanentHasShroud(p, s) || p.HasKeyword(Keyword::Hexproof))) { continue; }
        Cand c;
        c.key   = p.card.m_number;
        c.name  = p.card.m_name.str();
        c.mv    = p.card.m_mana_cost.ManaValue();
        c.power = PermIsCreature(p) ? p.EffectivePower() : 0;
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<Cand> HandCands(const GameState& s, const Effect& e)
{
    std::vector<Cand> out;
    const Player& pl = s.players[static_cast<std::size_t>(s.active_player_index)];
    for (const Card& c : pl.hand)
    {
        if (c.m_is_staged) { continue; }   // a staged card is in exile (castable from there), not the hand
        const Card& t = Typed(c);
        if (!CardMatches(t, e.cls)) { continue; }
        Cand x;
        x.key  = c.m_number;
        x.name = t.m_name.str();
        x.mv   = t.m_mana_cost.ManaValue();
        out.push_back(std::move(x));
    }
    return out;
}

// DESTROY, as one simultaneous event (DestroyRemoveTogether / DestroyFireTogether: indestructible stays, CR
// 702.12b; unearth exiles instead, CR 702.84a; a token ceases to exist; the dies / leave cascade fires). Umbra armor
// (CR 702.89a) is applied first: "If enchanted permanent would be destroyed, instead remove all damage marked on it
// and destroy this Aura."
void DoDestroy(GameState& s, const std::vector<int>& numbers, std::vector<std::string>& outcome)
{
    const int destroyer = 1 - s.active_player_index;
    std::vector<int> go;
    for (int num : numbers)
    {
        const int bi = FindBf(s, num);
        if (bi < 0) { outcome.push_back("gone"); continue; }
        Permanent& p = s.battlefield[static_cast<std::size_t>(bi)];
        if (p.HasKeyword(Keyword::Indestructible)) { outcome.push_back("indestructible"); continue; }
        int umbra = 0;
        std::string umbra_name;
        for (const Permanent& a : s.battlefield)
        {
            if (a.aura_attached_to != num || a.card.m_number == num) { continue; }
            if (!Typed(a.card).HasKeyword(Keyword::UmbraArmor) && !a.card.HasKeyword(Keyword::UmbraArmor)) { continue; }
            umbra = a.card.m_number;
            umbra_name = a.card.m_name.str();
            break;
        }
        if (umbra != 0)
        {
            p.damage = 0;
            go.push_back(umbra);
            outcome.push_back("umbra armor: " + umbra_name + " destroyed instead");
            continue;
        }
        go.push_back(num);
        outcome.push_back("destroyed");
    }
    std::vector<DestroyedTogether> gone;
    std::vector<Permanent> unearth_left;
    DestroyRemoveTogether(s, go, "Control effect", gone, unearth_left);
    DestroyFireTogether(s, destroyer, gone, unearth_left);
}

// The `skip` / range bookkeeping and every per-effect application at the round's start.
void ApplyBoard(GameState& s, int k, const Effect& e, bool& changed)
{
    ojson j = Head(s, k, e);
    j["features"] = Features(s);
    const std::vector<Cand> cands = BoardCands(s, e, false);
    const std::vector<int> pick = Choose(e, cands);
    j["count"]      = static_cast<int>(cands.size());
    j["candidates"] = Names(cands);
    std::vector<int> nums;
    ojson what = ojson::array();
    for (int i : pick) { nums.push_back(cands[static_cast<std::size_t>(i)].key); what.push_back(cands[static_cast<std::size_t>(i)].name); }
    std::vector<std::string> outcome;
    std::vector<bool> was_token;
    for (int num : nums)
    {
        const int bi = FindBf(s, num);
        was_token.push_back(bi >= 0 && (s.battlefield[static_cast<std::size_t>(bi)].is_token
                                        || num >= GameState::kFirstOppSpawnNumber));
    }
    if (!nums.empty())
    {
        const std::string label = "control effect " + e.text;
        if (e.kind == Kind::Destroy) { DoDestroy(s, nums, outcome); }
        else if (e.kind == Kind::Exile || e.kind == Kind::ExileUntil)
        {
            ResolveCastTriggerTargets(s, 1 - s.active_player_index, "Control effect", nums, /*destroy=*/false);
        }
        else if (e.kind == Kind::Bounce)
        {
            for (int num : nums)
            {
                const int bi = FindBf(s, num);
                if (bi >= 0) { ReturnPermanentToOwnersHand(s, bi, "Control effect"); }
            }
            SweepOrphanedAuras(s);
        }
        PlayEvent(s, label + ": " + what.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }
    int applied = 0;
    for (std::size_t i = 0; i < nums.size(); ++i)
    {
        const bool left = FindBf(s, nums[i]) < 0;
        if (e.kind != Kind::Destroy) { outcome.push_back(left ? (was_token[i] ? "token: ceases to exist" : "left") : "stayed"); }
        // A destroy counts what it destroyed, even when the card is back already (persist, undying).
        if (e.kind == Kind::Destroy ? outcome[i] == "destroyed" : left) { ++applied; }
        if (left && e.kind == Kind::ExileUntil && !was_token[i])
        {
            for (const Card& c : s.exile)
            {
                if (c.m_number != nums[i]) { continue; }
                PendingReturn pr;
                pr.number = nums[i];
                pr.owner  = s.active_player_index;
                pr.ret    = e.ret;
                pr.effect = k;
                pr.name   = c.m_name.str();
                t_ctx.returns.push_back(std::move(pr));
                break;
            }
        }
    }
    // Umbra armor saved the creature but destroyed its Aura: a change all the same.
    for (const std::string& o : outcome) { if (o.rfind("umbra", 0) == 0) { changed = true; } }
    if (e.kind == Kind::ExileUntil) { j["ret"] = e.ret; }
    j["applied"] = applied;
    j["what"]    = what;
    j["outcome"] = outcome;
    if (applied > 0) { changed = true; }
    Emit("ctrl-inject", j);
}

void ApplyDiscard(GameState& s, int k, const Effect& e, bool& changed)
{
    ojson j = Head(s, k, e);
    j["features"] = Features(s);
    const std::vector<Cand> cands = HandCands(s, e);
    const std::vector<int> pick = Choose(e, cands);
    j["count"]      = static_cast<int>(cands.size());
    j["candidates"] = Names(cands);
    ojson what = ojson::array();
    std::vector<MadnessPending> mq;
    int applied = 0;
    const int me = s.active_player_index;
    for (int i : pick)
    {
        const int num = cands[static_cast<std::size_t>(i)].key;
        const Player& pl = s.players[static_cast<std::size_t>(me)];
        int hi = -1;
        for (int h = 0; h < static_cast<int>(pl.hand.size()); ++h)
        { if (pl.hand[static_cast<std::size_t>(h)].m_number == num) { hi = h; break; } }
        if (hi < 0) { continue; }
        DiscardCardFromHand(s, me, hi, &mq, "control effect");
        what.push_back(cands[static_cast<std::size_t>(i)].name);
        ++applied;
    }
    if (!mq.empty()) { ResolvePendingMadness(s, mq, MadnessWindowNow(s), MadnessPin{}, "control effect"); }
    if (applied > 0)
    {
        changed = true;
        PlayEvent(s, "control effect " + e.text + ": discards " + what.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }
    j["applied"] = applied;
    j["what"]    = what;
    Emit("ctrl-inject", j);
}

void ApplyLife(GameState& s, int k, const Effect& e, bool& changed)
{
    ojson j = Head(s, k, e);
    j["features"] = Features(s);
    const int before = s.Opponent().life;
    // The passive opponent IS the control deck: its gain goes through the shared opponent-gain route, so a
    // Tainted Remedy flips it, a Screaming Nemesis lockout or a "players can't gain life" static stops it.
    OpponentGainsLife(s, s.active_player_index, e.amount, "control effect");
    const int delta = s.Opponent().life - before;
    t_ctx.life_delta += delta;
    j["amount"]  = e.amount;
    j["delta"]   = delta;
    j["count"]   = 1;
    j["applied"] = delta != 0 ? 1 : 0;
    if (delta != 0) { changed = true; PlayEvent(s, "control effect " + e.text + ": the opponent's life " + std::to_string(before) + " -> " + std::to_string(s.Opponent().life)); }
    Emit("ctrl-inject", j);
}

// The tax in force this round: the sum over every active tax effect, per class.
void RecomputeTax(GameState& s, bool& changed)
{
    int all = 0, nonc = 0, crt = 0;
    for (std::size_t k = 0; k < t_ctx.spec.size(); ++k)
    {
        const Effect& e = t_ctx.spec[k];
        if (e.kind != Kind::Tax) { continue; }
        const bool on = InRounds(e, s.turn_number);
        if (on)
        {
            if (e.tax_class == TaxClass::All) { all += e.amount; }
            else if (e.tax_class == TaxClass::Noncreature) { nonc += e.amount; }
            else { crt += e.amount; }
        }
        if (on != (t_ctx.tax_on[k] != 0))
        {
            t_ctx.tax_on[k] = on ? 1 : 0;
            ojson j = Head(s, static_cast<int>(k), e);
            j["phase"]   = on ? "start" : "end";
            j["amount"]  = e.amount;
            j["class"]   = e.tax_class == TaxClass::All ? "all" : (e.tax_class == TaxClass::Noncreature ? "noncreature" : "creature");
            j["count"]   = 1;
            j["applied"] = on ? 1 : 0;
            j["features"] = Features(s);
            Emit("ctrl-inject", j);
        }
    }
    all  = std::min(all, 255);
    nonc = std::min(nonc, 15);
    crt  = std::min(crt, 15);
    const std::uint8_t a = static_cast<std::uint8_t>(all);
    const std::uint8_t sp = static_cast<std::uint8_t>((crt << 4) | nonc);
    if (a != s.ctrl_tax_all || sp != s.ctrl_tax_split)
    {
        s.ctrl_tax_all = a;
        s.ctrl_tax_split = sp;
        changed = true;
        PlayEvent(s, "control effect: the goldfisher's spells now cost {" + std::to_string(all) + "} more (+{"
                     + std::to_string(nonc) + "} noncreature, +{" + std::to_string(crt) + "} creature)");
    }
}

// exile_until: every card due back by this round returns as a NEW object (CR 400.7) under its owner, through the
// shared put (both enter cascades: its ETBs fire again). It enters before the goldfisher's untap step, so the untap
// clears its entered-this-turn flag: under CR 302.6 it may attack this turn (it has been under its controller's
// control continuously since the turn began), exactly as a card returned during the control player's upkeep can.
void ProcessReturns(GameState& s, bool& changed)
{
    for (std::size_t i = 0; i < t_ctx.returns.size(); )
    {
        PendingReturn pr = t_ctx.returns[i];
        if (pr.ret > s.turn_number) { ++i; continue; }
        t_ctx.returns.erase(t_ctx.returns.begin() + static_cast<std::ptrdiff_t>(i));
        const Effect& e = t_ctx.spec[static_cast<std::size_t>(pr.effect)];
        ojson j = Head(s, pr.effect, e);
        j["phase"] = "return";
        j["features"] = Features(s);
        int at = -1;
        for (int x = 0; x < static_cast<int>(s.exile.size()); ++x)
        { if (s.exile[static_cast<std::size_t>(x)].m_number == pr.number) { at = x; break; } }
        j["count"] = 1;
        j["what"]  = ojson::array({ pr.name });
        if (at < 0)
        {
            j["applied"] = 0;
            j["outcome"] = ojson::array({ "no longer in exile" });
            Emit("ctrl-inject", j);
            continue;
        }
        Card c = s.exile[static_cast<std::size_t>(at)];
        s.exile.erase(s.exile.begin() + at);
        c.m_is_staged = false;
        c.m_staged_expiry = 0;
        PutCardOntoBattlefield(s, pr.owner, c, "control effect " + e.text + " ends", 0, false, pr.owner);
        changed = true;
        j["applied"] = 1;
        j["outcome"] = ojson::array({ "returned" });
        Emit("ctrl-inject", j);
    }
}

// Counters for rounds before `upto` (inclusive) that never found a spell: logged applied=0, with that round's
// spells as the candidate list.
void FinalizeCounters(const GameState& s, int upto)
{
    for (std::size_t k = 0; k < t_ctx.spec.size(); ++k)
    {
        const Effect& e = t_ctx.spec[k];
        if (e.kind != Kind::Counter && e.kind != Kind::CounterToHand && e.kind != Kind::CounterToTop) { continue; }
        const int last = std::min(e.to, upto);
        for (int r = std::max(e.from, t_ctx.finalized[k] + 1); r <= last; ++r)
        {
            t_ctx.finalized[k] = r;
            if (Hit(static_cast<int>(k), r)) { continue; }
            ojson j;
            j["job"] = t_ctx.job; j["gi"] = t_ctx.gi; j["round"] = r; j["effect"] = e.text;
            j["n"] = static_cast<int>(k); j["kind"] = KindName(e.kind);
            const RoundRec& rr = Rec(r);
            ojson names = ojson::array();
            for (const std::string& n : rr.spells) { names.push_back(n); }
            j["count"]      = static_cast<int>(rr.spells.size());
            j["candidates"] = names;
            j["applied"]    = 0;
            j["skipped"]    = rr.skipped;
            (void)s;
            Emit("ctrl-inject", j);
        }
    }
}

// The card a countered spell puts in a zone: the printed card, never a synthesized face (a "(Kicked)" face's base,
// CR 608.2m; an adventure's whole card, CR 715.4; an MDFC / face-down spell revealed, CR 708.9), unstaged.
Card PrintedCard(const StackEntry& entry, const CardDefinition* d)
{
    Card c = entry.source;
    if (d != nullptr)
    {
        c = UnkickedSpellCard(*d, c);
        if (!d->params.adventure_parent_name.empty())
        {
            if (const CardDefinition* pd = CardDatabase::Instance().Lookup(d->params.adventure_parent_name))
            {
                const int num = c.m_number;
                c = pd->card;
                c.m_number = num;
            }
        }
    }
    c = RevealedCard(c);
    c.m_is_staged = false;
    c.m_staged_expiry = 0;
    return c;
}

// Mana value on the stack: the printed card's (kicker is not part of it, CR 202.3), plus X (CR 202.3e).
int StackManaValue(const StackEntry& entry, const CardDefinition* d)
{
    if (d == nullptr) { return entry.source.m_mana_cost.ManaValue(); }
    int mv = PrintedSpellManaValue(*d);
    if (d->card.m_mana_cost.has_x && entry.chosen_x.has_value() && *entry.chosen_x > 0)
    { mv += *entry.chosen_x * std::max(1, d->card.m_mana_cost.x_pips); }
    return mv;
}
}  // namespace

const char* KindName(Kind k)
{
    switch (k)
    {
    case Kind::Skip:          return "skip";
    case Kind::Destroy:       return "destroy";
    case Kind::Exile:         return "exile";
    case Kind::ExileUntil:    return "exile_until";
    case Kind::Bounce:        return "bounce";
    case Kind::Tap:           return "tap";
    case Kind::Discard:       return "discard";
    case Kind::Tax:           return "tax";
    case Kind::Life:          return "life";
    case Kind::Counter:       return "counter";
    case Kind::CounterToHand: return "counter_to_hand";
    case Kind::CounterToTop:  return "counter_to_top";
    }
    return "?";
}

Spec Parse(const std::string& s)
{
    Spec out;
    std::size_t at = 0;
    while (at <= s.size())
    {
        const std::size_t semi = s.find(';', at);
        const std::string one = Trim(s.substr(at, semi == std::string::npos ? std::string::npos : semi - at));
        if (!one.empty()) { out.push_back(ParseOne(one)); }
        if (semi == std::string::npos) { break; }
        at = semi + 1;
    }
    return out;
}

void Install(const Spec& spec, bool record, const char* job, int gi, const GameState& state)
{
    t_ctx = GameCtx{};
    t_armed = !spec.empty() || record;
    if (!t_armed) { return; }
    t_ctx.spec       = spec;
    t_ctx.record     = record;
    t_ctx.job        = job != nullptr ? job : "-";
    t_ctx.gi         = gi;
    t_ctx.start_life = state.Opponent().life;
    t_ctx.hit.assign(spec.size(), {});
    t_ctx.finalized.assign(spec.size(), 0);
    t_ctx.tax_on.assign(spec.size(), 0);
}

void Clear()
{
    t_ctx = GameCtx{};
    t_armed = false;
}

bool Armed() { return t_armed; }

RoundStart OnRoundStart(GameState& state)
{
    RoundStart rs;
    if (!t_armed) { return rs; }
    const int r = state.turn_number;
    FinalizeCounters(state, r - 1);
    ProcessReturns(state, rs.changed);
    RecomputeTax(state, rs.changed);
    for (std::size_t k = 0; k < t_ctx.spec.size(); ++k)
    {
        const Effect& e = t_ctx.spec[k];
        if (!InRounds(e, r)) { continue; }
        switch (e.kind)
        {
        case Kind::Skip:
        {
            ojson j = Head(state, static_cast<int>(k), e);
            j["features"] = Features(state);
            j["count"] = 1;
            j["applied"] = 1;
            Emit("ctrl-inject", j);
            rs.skip = true;
            break;
        }
        case Kind::Destroy: case Kind::Exile: case Kind::ExileUntil: case Kind::Bounce:
            ApplyBoard(state, static_cast<int>(k), e, rs.changed);
            break;
        case Kind::Discard:
            ApplyDiscard(state, static_cast<int>(k), e, rs.changed);
            break;
        case Kind::Life:
            ApplyLife(state, static_cast<int>(k), e, rs.changed);
            break;
        default:
            break;   // tap (after untap), tax (above), counters (at resolution)
        }
    }
    if (rs.skip)
    {
        // A tap in a skipped round meets no untap step: nothing to do, logged as a no-op.
        for (std::size_t k = 0; k < t_ctx.spec.size(); ++k)
        {
            const Effect& e = t_ctx.spec[k];
            if (e.kind != Kind::Tap || !InRounds(e, r)) { continue; }
            ojson j = Head(state, static_cast<int>(k), e);
            j["count"] = 0;
            j["applied"] = 0;
            j["skipped"] = true;
            Emit("ctrl-inject", j);
        }
        if (state.ctrl_skipped_turns < 255) { ++state.ctrl_skipped_turns; }
        rs.changed = true;
        PlayEvent(state, "control effect: the goldfisher's turn of round " + std::to_string(r) + " is skipped");
    }
    return rs;
}

bool OnAfterUntap(GameState& state)
{
    if (!t_armed) { return false; }
    bool changed = false;
    const int r = state.turn_number;
    for (std::size_t k = 0; k < t_ctx.spec.size(); ++k)
    {
        const Effect& e = t_ctx.spec[k];
        if (e.kind != Kind::Tap || !InRounds(e, r) || Hit(static_cast<int>(k), r)) { continue; }
        Hit(static_cast<int>(k), r) = 1;
        ojson j = Head(state, static_cast<int>(k), e);
        j["features"] = Features(state);
        const std::vector<Cand> cands = BoardCands(state, e, /*untapped_only=*/true);
        const std::vector<int> pick = Choose(e, cands);
        j["count"]      = static_cast<int>(cands.size());
        j["candidates"] = Names(cands);
        ojson what = ojson::array();
        int applied = 0;
        for (int i : pick)
        {
            const int bi = FindBf(state, cands[static_cast<std::size_t>(i)].key);
            if (bi < 0) { continue; }
            state.battlefield[static_cast<std::size_t>(bi)].tapped = true;
            what.push_back(cands[static_cast<std::size_t>(i)].name);
            ++applied;
        }
        if (applied > 0)
        {
            changed = true;
            PlayEvent(state, "control effect " + e.text + ": taps " + what.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        }
        j["applied"] = applied;
        j["what"]    = what;
        Emit("ctrl-inject", j);
    }
    return changed;
}

bool OnSpellResolving(GameState& state, const StackEntry& entry)
{
    if (!t_armed) { return false; }
    if (entry.type != StackEntry::EntryType::Spell || entry.controller_index != state.active_player_index) { return false; }
    const int r = state.turn_number;
    const CardDefinition* d = CardDatabase::Instance().LookupCached(entry.source);
    const Card printed = PrintedCard(entry, d);
    const std::string name = printed.m_name.str();
    const int mv = StackManaValue(entry, d);
    RoundRec& rec = Rec(r);
    const int idx = static_cast<int>(rec.spells.size());
    rec.spells.push_back(name);
    for (std::size_t k = 0; k < t_ctx.spec.size(); ++k)
    {
        const Effect& e = t_ctx.spec[k];
        if (e.kind != Kind::Counter && e.kind != Kind::CounterToHand && e.kind != Kind::CounterToTop) { continue; }
        if (!InRounds(e, r)) { continue; }
        if (e.sel != Sel::All && Hit(static_cast<int>(k), r)) { continue; }
        bool match = false;
        switch (e.sel)
        {
        case Sel::Index:     match = idx == e.index; break;
        case Sel::MvAtLeast: match = mv >= e.mv; break;
        case Sel::Name:      match = name == e.name; break;
        case Sel::All:       match = true; break;
        case Sel::Big:       match = false; break;
        }
        if (match && e.cls != Class::Card && e.cls != Class::Perm) { match = CardMatches(d != nullptr ? d->card : entry.source, e.cls); }
        if (!match) { continue; }
        ojson j = Head(state, static_cast<int>(k), e);
        j["features"] = Features(state);
        ojson cands = ojson::array();
        for (const std::string& n : rec.spells) { cands.push_back(n); }
        j["count"]      = static_cast<int>(rec.spells.size());
        j["candidates"] = cands;
        j["what"]       = ojson::array({ name });
        j["mv"]         = mv;
        // "Can't be countered" (CR 101.2: the prohibition wins): the counter is spent on it and does nothing.
        // A policy counter (mv>= / name= / all) would never be aimed at it, so it keeps waiting instead; an
        // index counter names this spell, so it is spent.
        if (d != nullptr && d->params.cant_be_countered)
        {
            if (e.sel == Sel::Index)
            {
                Hit(static_cast<int>(k), r) = 1;
                j["applied"] = 0;
                j["outcome"] = ojson::array({ "can't be countered" });
                Emit("ctrl-inject", j);
            }
            continue;
        }
        Hit(static_cast<int>(k), r) = 1;
        const int owner = entry.controller_index;
        std::string dest;
        if (entry.is_copy) { dest = "copy: ceases to exist"; }
        else if (entry.cast_with_flashback)
        {
            state.exile.push_back(printed);   // flashback: exiled instead of anywhere else (CR 702.34a)
            dest = "exile (flashback)";
        }
        else if (e.kind == Kind::Counter)
        {
            if (!MaybeReplaceGraveyardWithLibraryShuffle(state, owner, printed))
            {
                state.players[static_cast<std::size_t>(owner)].graveyard.push_back(printed);
                FireGraveyardEntryTriggers(state, owner, printed);
                dest = "graveyard";
            }
            else { dest = "library (shuffled in instead)"; }
        }
        else if (e.kind == Kind::CounterToHand)
        {
            ReturnCounteredSpellToHand(state, owner, printed);
            dest = "hand";
        }
        else
        {
            Library& lib = state.players[static_cast<std::size_t>(owner)].library;
            lib.insert(lib.begin(), printed);   // a placement its owner knows (the known-top mask)
            dest = "library top";
        }
        j["applied"] = 1;
        j["outcome"] = ojson::array({ dest });
        Emit("ctrl-inject", j);
        PlayEvent(state, "control effect " + e.text + ": counters " + name + " (" + dest + "); the mana stays spent");
        return true;
    }
    return false;
}

void OnTurnEnd(const GameState& state, bool skipped)
{
    if (!t_armed) { return; }
    RoundRec& rec = Rec(state.turn_number);
    rec.seen        = true;
    rec.skipped     = rec.skipped || skipped;
    rec.life        = state.Opponent().life;
    rec.poison      = state.Opponent().poison_counters;
    rec.opp_library = static_cast<int>(state.Opponent().library.size());
    rec.spells_cast = skipped ? 0 : state.spells_cast_this_turn;
    rec.life_gain   = t_ctx.life_delta;
}

bool BenefitPending(const GameState& state)
{
    if (!t_armed) { return false; }
    if (!t_ctx.returns.empty()) { return true; }
    for (const Effect& e : t_ctx.spec)
    {
        if (e.kind == Kind::Tax && e.to >= state.turn_number) { return true; }
    }
    return false;
}

void FinishGame(const GameState& state, int win_turn)
{
    if (!t_armed) { return; }
    const int r = state.turn_number;
    FinalizeCounters(state, r);
    {
        RoundRec& rec = Rec(r);
        rec.seen        = true;
        rec.life        = state.Opponent().life;
        rec.poison      = state.Opponent().poison_counters;
        rec.opp_library = static_cast<int>(state.Opponent().library.size());
        if (!rec.skipped) { rec.spells_cast = state.spells_cast_this_turn; }
        rec.life_gain   = t_ctx.life_delta;
    }
    if (!t_ctx.record) { return; }
    const Player& opp = state.Opponent();
    std::string kill = "none";
    if (OpponentHasLost(state))
    {
        if (opp.life <= 0)                { kill = "life"; }
        else if (opp.PoisonLost())        { kill = "poison"; }
        else if (state.opponent_decked)   { kill = "decked"; }
        else if (state.won_by_effect)     { kill = "alt"; }
        else                              { kill = "other"; }
    }
    ojson j;
    j["job"]          = t_ctx.job;
    j["gi"]           = t_ctx.gi;
    j["wt"]           = win_turn;
    j["kill"]         = kill;
    j["through_life"] = kill == "life";
    j["start_life"]   = t_ctx.start_life;
    j["life_gain"]    = t_ctx.life_delta;
    j["skipped"]      = static_cast<int>(state.ctrl_skipped_turns);
    j["free_turns"]   = static_cast<int>(state.free_turns_taken);
    ojson rounds = ojson::array();
    for (int x = 1; x < static_cast<int>(t_ctx.rounds.size()); ++x)
    {
        const RoundRec& rr = t_ctx.rounds[static_cast<std::size_t>(x)];
        if (!rr.seen) { continue; }
        ojson o;
        o["r"]           = x;
        o["life"]        = rr.life;
        // Damage the goldfisher has dealt by the end of this round: the life it has taken away, net of what the
        // `life` effects had added by then (start + gains so far - now).
        o["dmg"]         = t_ctx.start_life + rr.life_gain - rr.life;
        o["life_gain"]   = rr.life_gain;
        o["poison"]      = rr.poison;
        o["opp_library"] = rr.opp_library;
        o["spells_cast"] = rr.spells_cast;
        ojson names = ojson::array();
        for (const std::string& n : rr.spells) { names.push_back(n); }
        o["spells"]      = names;
        if (rr.skipped) { o["skipped"] = true; }
        rounds.push_back(o);
    }
    j["rounds"] = rounds;
    Emit("ctrl-game", j);
}
}  // namespace ctrlinject
