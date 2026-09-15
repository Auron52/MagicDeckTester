#pragma once
#include "ValueArm.h"
#include "MulliganProfile.h"
#include "../core/EnvFlags.h"   // EnvOn (MTG_VALUE_FLAT)
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#ifdef MTG_HAVE_ZLIB
#include <zlib.h>
#endif

// ---- BottomOrder string helpers --------------------------------------------

inline const char* BottomOrderToString(BottomOrder bo)
{
    switch (bo)
    {
        case BottomOrder::CountFirst: return "count_first";
        case BottomOrder::TotalFirst: return "total_first";
        default:                      return "count_first";
    }
}

inline BottomOrder BottomOrderFromString(const std::string& s)
{
    if (s == "total_first") { return BottomOrder::TotalFirst; }
    return BottomOrder::CountFirst;
}

// ---- CurveCheck string helpers ---------------------------------------------

inline const char* CurveCheckToString(CurveCheck cc)
{
    switch (cc)
    {
        case CurveCheck::None:       return "none";
        case CurveCheck::TwoDrop:    return "two_drop";
        case CurveCheck::OneDrop:    return "one_drop";
        case CurveCheck::OneAndTwo:  return "one_and_two";
        default:                     return "two_drop";
    }
}

inline CurveCheck CurveCheckFromString(const std::string& s)
{
    if (s == "none")        { return CurveCheck::None; }
    if (s == "two_drop")    { return CurveCheck::TwoDrop; }
    if (s == "one_drop")    { return CurveCheck::OneDrop; }
    if (s == "one_and_two") { return CurveCheck::OneAndTwo; }
    return CurveCheck::TwoDrop;  // unknown strings fall back to default
}

// ---- Color string helpers --------------------------------------------------

inline const char* ColorToChar(Color c)
{
    switch (c)
    {
        case Color::White:     return "W";
        case Color::Blue:      return "U";
        case Color::Black:     return "B";
        case Color::Red:       return "R";
        case Color::Green:     return "G";
        case Color::Colorless: return "C";
        default:               return "?";
    }
}

inline Color CharToColor(const std::string& s)
{
    if (s == "W") { return Color::White; }
    if (s == "U") { return Color::Blue;  }
    if (s == "B") { return Color::Black; }
    if (s == "R") { return Color::Red;   }
    if (s == "G") { return Color::Green; }
    if (s == "C") { return Color::Colorless; }
    throw std::runtime_error("Unknown color string: " + s);
}

// ---- KeepModel JSON (the analyzer-generated keep decision tree, stored in the profile) -----

// One additive score (coefs keyed by feature NAME, intercept, per-(play,mull) thresholds). Shared by
// the single-score form and each hybrid leaf score (their coefs align with the same full vector).
inline nlohmann::json ScoreToJsonObj(const KeepModel& km, const KeepScore& s)
{
    using json = nlohmann::json;
    json sc;
    sc["intercept"] = s.intercept;
    json coefs = json::object();
    for (int j = 0; j < static_cast<int>(s.coefs.size()); ++j)
    { if (s.coefs[j] != 0) { coefs[FeatureNameAt(km, j)] = s.coefs[j]; } }
    sc["coefs"] = coefs;
    json thr = json::array();
    for (const std::vector<long long>& row : s.thr)
    {
        json jr = json::array();
        for (long long v : row) { jr.push_back(v); }
        thr.push_back(jr);
    }
    sc["thr"] = thr;
    return sc;
}

inline KeepScore ScoreFromJsonObj(const KeepModel& km, const nlohmann::json& sc)
{
    using json = nlohmann::json;
    KeepScore s;
    s.intercept = sc.value("intercept", 0LL);
    const int full = static_cast<int>(KeepFeature::Count) + static_cast<int>(km.extra_features.size());
    s.coefs.assign(full, 0LL);
    if (sc.contains("coefs"))
    {
        if (sc["coefs"].is_object())
        {
            for (const auto& [name, val] : sc["coefs"].items())
            {
                const int idx = FeatureIndexFromName(km, name);
                if (idx >= 0 && idx < full) { s.coefs[idx] = val.get<long long>(); }
            }
        }
        else { int j = 0; for (const json& v : sc["coefs"]) { if (j < full) { s.coefs[j++] = v.get<long long>(); } } }
    }
    if (sc.contains("thr"))
    {
        for (const json& jr : sc["thr"])
        {
            std::vector<long long> row;
            for (const json& v : jr) { row.push_back(v.get<long long>()); }
            s.thr.push_back(row);
        }
    }
    return s;
}

inline nlohmann::json KeepModelToJsonObj(const KeepModel& km)
{
    using json = nlohmann::json;
    json m;

    json kp = json::array();
    for (const std::string& s : km.key_pieces) { kp.push_back(s); }
    m["key_pieces"] = kp;

    json dc = json::array();
    for (Color c : km.deck_colors) { dc.push_back(ColorToChar(c)); }
    m["deck_colors"] = dc;

    // Data-defined extra features (Stage 2). Each carries both a human name (= the tree-node split
    // name) and its machine definition (kind + params) so runtime recomputes it in lockstep.
    if (!km.extra_features.empty())
    {
        json ef = json::array();
        for (const FeatureSpec& s : km.extra_features)
        {
            json js;
            js["name"] = s.name;
            js["kind"] = FeatureKindName(static_cast<FeatureKind>(s.kind));
            if (s.p != 0)        { js["p"] = s.p; }
            if (s.a >= 0)        { js["a"] = s.a; }
            if (s.b >= 0)        { js["b"] = s.b; }
            if (!s.s.empty())    { js["s"] = s.s; }
            ef.push_back(js);
        }
        m["extra_features"] = ef;
    }

    json nodes = json::array();
    for (const KeepNode& n : km.nodes)
    {
        json jn;
        if (n.feat < 0)   // leaf
        {
            jn["leaf"] = (n.keep != 0) ? "keep" : "mull";
            if (n.leaf_score >= 0) { jn["leaf_score"] = n.leaf_score; }   // hybrid additive leaf
        }
        else
        {
            jn["feat"] = FeatureNameAt(km, n.feat);   // base name or extra-spec name
            jn["op"]   = KeepOpName(static_cast<KeepOp>(n.op));
            jn["val"]  = n.val;
            jn["yes"]  = n.yes;
            jn["no"]   = n.no;
        }
        nodes.push_back(jn);
    }
    m["nodes"] = nodes;

    // Additive-score form (single): when present it OWNS the decision; `nodes` is empty. Coefs are keyed
    // by FEATURE NAME so the model survives appending new base features later (absent name => 0 coef).
    if (!km.score.empty()) { m["score"] = ScoreToJsonObj(km, km.score); }

    // Hybrid model-tree: the per-leaf additive scores the tree's leaves dispatch to.
    if (!km.leaf_scores.empty())
    {
        json ls = json::array();
        for (const KeepScore& s : km.leaf_scores) { ls.push_back(ScoreToJsonObj(km, s)); }
        m["leaf_scores"] = ls;
    }
    return m;
}

inline KeepModel KeepModelFromJsonObj(const nlohmann::json& m)
{
    using json = nlohmann::json;
    KeepModel km;
    if (m.contains("key_pieces"))
    {
        for (const json& v : m["key_pieces"]) { km.key_pieces.push_back(v.get<std::string>()); }
    }
    if (m.contains("deck_colors"))
    {
        for (const json& v : m["deck_colors"])
        {
            try { km.deck_colors.push_back(CharToColor(v.get<std::string>())); } catch (...) {}
        }
    }
    // Extra features MUST be loaded before the nodes -- a node may split on one (resolved by name).
    if (m.contains("extra_features"))
    {
        for (const json& js : m["extra_features"])
        {
            FeatureSpec s;
            s.name = js.value("name", std::string{});
            s.kind = FeatureKindFromName(js.value("kind", std::string{}));
            s.p    = js.value("p", 0);
            s.a    = js.value("a", -1);
            s.b    = js.value("b", -1);
            s.s    = js.value("s", std::string{});
            if (s.kind >= 0 && !s.name.empty()) { km.extra_features.push_back(s); }
        }
    }
    if (m.contains("nodes"))
    {
        for (const json& jn : m["nodes"])
        {
            KeepNode n;
            if (jn.contains("leaf"))
            {
                n.feat = -1;
                n.keep = (jn["leaf"].get<std::string>() == "keep") ? 1 : 0;
                n.leaf_score = jn.value("leaf_score", -1);   // hybrid additive leaf (else constant)
            }
            else
            {
                n.feat = FeatureIndexFromName(km, jn.value("feat", std::string{}));
                n.op   = static_cast<int>(KeepOpFromName(jn.value("op", std::string("<="))));
                n.val  = jn.value("val", 0);
                n.yes  = jn.value("yes", -1);
                n.no   = jn.value("no", -1);
            }
            km.nodes.push_back(n);
        }
    }
    // Score coefs are rebuilt POSITIONALLY (aligned to base [0..Count) ++ this model's extra_features)
    // by feature NAME, so appending a base feature later just gives old models a 0 coef. extra_features
    // are already loaded above, so the name->index resolution is correct.
    if (m.contains("score")) { km.score = ScoreFromJsonObj(km, m["score"]); }
    if (m.contains("leaf_scores"))
    {
        for (const json& js : m["leaf_scores"]) { km.leaf_scores.push_back(ScoreFromJsonObj(km, js)); }
    }
    return km;
}

// ---- KeepConstraints JSON (a SEPARATE durable per-deck input file) ----------
//   { "version": 1, "required_pieces": [ ... ] }
// Loaded alongside the profile but NEVER written by SaveDeckProfile, so regenerating the profile
// cannot clobber a deck author's hand-set constraints.

inline KeepConstraints KeepConstraintsFromJson(const std::string& json_str)
{
    using json = nlohmann::json;
    KeepConstraints kc;
    json root = json::parse(json_str);
    if (root.contains("required_pieces"))
    {
        for (const json& v : root["required_pieces"]) { kc.required_pieces.push_back(v.get<std::string>()); }
    }
    return kc;
}

// Sibling constraints path for a profile path: <stem>.profile.json -> <stem>.constraints.json.
inline std::filesystem::path ConstraintsPathFor(const std::filesystem::path& profile_path)
{
    const std::string s = profile_path.string();
    const std::string suffix = ".profile.json";
    if (s.size() >= suffix.size()
        && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
    {
        return s.substr(0, s.size() - suffix.size()) + ".constraints.json";
    }
    return profile_path.parent_path() / (profile_path.stem().string() + ".constraints.json");
}

inline KeepConstraints LoadKeepConstraints(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) { return KeepConstraints{}; }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    try   { return KeepConstraintsFromJson(content); }
    catch (...) { std::cerr << "[profload] SWALLOWED: KeepConstraints parse failed: " << path << "\n"; return KeepConstraints{}; }
}

// ---- Mulligan JSON object (used by both DeckProfileToJson and AnalyzerEngine) --

inline nlohmann::json MulliganProfileToJsonObj(const MulliganProfile& profile)
{
    using json = nlohmann::json;

    json m;
    m["min_lands"]    = profile.min_lands;
    m["max_lands"]    = profile.max_lands;
    m["min_playable"] = profile.min_playable;
    m["stop_at"]      = profile.stop_at;
    m["curve_check"]  = CurveCheckToString(profile.curve_check);
    m["bottom_order"] = BottomOrderToString(profile.bottom_order);
    // Bottoming-eval policy: emitted only when set, so decks without one round-trip unchanged
    // (and a regeneration of a deck that HAS one preserves it rather than dropping the keys).
    if (profile.bottom_eval_depth     >= 0) { m["bottom_eval_depth"]     = profile.bottom_eval_depth; }
    if (profile.bottom_eval_budget_ms >= 0) { m["bottom_eval_budget_ms"] = profile.bottom_eval_budget_ms; }
    if (profile.bottom_eval_topk      >  0) { m["bottom_eval_topk"]      = profile.bottom_eval_topk; }

    json pieces = json::array();
    for (const std::string& s : profile.required_pieces) { pieces.push_back(s); }
    m["required_pieces"] = pieces;

    // Emitted only when non-default, so regenerating any deck that never set it leaves its profile
    // byte-identical instead of sprouting a "discard_protect": "all" line everywhere.
    if (profile.discard_protect != DiscardProtectScope::All)
    {
        m["discard_protect"] = DiscardProtectScopeToString(profile.discard_protect);
    }

    json color_sources = json::object();
    for (const std::pair<const Color, int>& kv : profile.min_color_sources)
    {
        color_sources[ColorToChar(kv.first)] = kv.second;
    }
    m["min_color_sources"] = color_sources;

    return m;
}

// ---- Exhaustive keep policy round-trip -------------------------------------
inline nlohmann::json ExhaustiveKeepToJsonObj(const ExhaustiveKeepPolicy& ek)
{
    using json = nlohmann::json;
    json e;
    e["max_mull"]          = ek.max_mull;
    e["effective_R"]       = ek.effective_R;
    e["bottoming_enabled"] = ek.bottoming_enabled;
    if (!ek.commit.empty())      { e["commit"]      = ek.commit; }
    if (!ek.play_digest.empty()) { e["play_digest"] = ek.play_digest; }
    json buckets = json::array();
    for (const std::vector<std::string>& b : ek.buckets)
    {
        json arr = json::array();
        for (const std::string& n : b) { arr.push_back(n); }
        buckets.push_back(arr);
    }
    e["buckets"] = buckets;
    json entries = json::array();
    for (const auto& [comp, flags] : ek.keep)
    {
        json je;
        je["comp"] = comp;
        je["keep"] = json::array();
        for (char c : flags) { je["keep"].push_back(static_cast<int>(c)); }
        auto bit = ek.bottom_keep.find(comp);
        if (bit != ek.bottom_keep.end())
        {
            json bk = json::array();
            for (const std::vector<int>& sub : bit->second) { bk.push_back(sub); }
            je["bottom_keep"] = bk;
        }
        entries.push_back(je);
    }
    e["entries"] = entries;
    return e;
}

inline ExhaustiveKeepPolicy ExhaustiveKeepFromJsonObj(const nlohmann::json& e)
{
    using json = nlohmann::json;
    ExhaustiveKeepPolicy ek;
    if (e.contains("max_mull"))          { ek.max_mull          = e["max_mull"].get<int>(); }
    if (e.contains("effective_R"))       { ek.effective_R       = e["effective_R"].get<int>(); }
    // Defaults ON, not off. Generation always bakes this true and there is no off switch, so a
    // block that is PRESENT but missing the key is an ancient or hand-edited file -- never a
    // deliberate opt-out. Defaulting it off would silently ship a table with its bottoming half
    // dead, which is the one failure this flag must not be able to cause. Only reached when the
    // caller has already confirmed an "exhaustive_keep" block exists; a deck with no table at all
    // keeps the struct's own `false` and is gated by ek.empty() upstream.
    ek.bottoming_enabled = e.value("bottoming_enabled", true);
    if (e.contains("commit"))            { ek.commit            = e["commit"].get<std::string>(); }
    if (e.contains("play_digest"))       { ek.play_digest       = e["play_digest"].get<std::string>(); }
    if (e.contains("buckets"))
        for (const json& b : e["buckets"])
        {
            std::vector<std::string> names;
            for (const json& n : b) { names.push_back(n.get<std::string>()); }
            ek.buckets.push_back(std::move(names));
        }
    if (e.contains("entries"))
        for (const json& je : e["entries"])
        {
            std::vector<int> comp = je["comp"].get<std::vector<int>>();
            std::vector<char> flags;
            for (const json& v : je["keep"]) { flags.push_back(static_cast<char>(v.get<int>())); }
            ek.keep[comp] = std::move(flags);
            if (je.contains("bottom_keep"))
            {
                std::vector<std::vector<int>> bk;
                for (const json& sub : je["bottom_keep"]) { bk.push_back(sub.get<std::vector<int>>()); }
                ek.bottom_keep[comp] = std::move(bk);
            }
        }
    ek.Index();
    return ek;
}

// ---- DeckProfile JSON document round-trip ----------------------------------
//
//   { "version": 1, "mulligan": { ... } }

inline std::string DeckProfileToJson(const MulliganProfile& profile)
{
    using json = nlohmann::json;
    json root;
    root["version"]  = 1;
    root["mulligan"] = MulliganProfileToJsonObj(profile);
    if (profile.vial_target_mv > 0)
        root["vial_target_mv"] = profile.vial_target_mv;
    if (profile.search_leaf_depth >= 0)
        root["search_leaf_depth"] = profile.search_leaf_depth;
    if (profile.search_leaf_first_turn_depth >= 0)
        root["search_leaf_first_turn_depth"] = profile.search_leaf_first_turn_depth;
    if (!profile.card_scores.empty())
    {
        json cs = json::object();
        for (const auto& [name, marginals] : profile.card_scores)
        {
            json arr = json::array();
            for (double v : marginals) { arr.push_back(v); }
            cs[name] = arr;
        }
        root["card_scores"] = cs;
        root["hand_score_threshold"] = profile.hand_score_threshold;
    }
    if (!profile.keep_model.empty())
    {
        root["keep_model"] = KeepModelToJsonObj(profile.keep_model);
    }
    if (profile.HasExhaustiveKeep())
    {
        root["exhaustive_keep"] = ExhaustiveKeepToJsonObj(*profile.exhaustive_keep);
    }
    // Exhaustive-keep profiles are dominated by a huge dense bottom_keep table (one K-vector per
    // composition x mulligan-depth). Pretty-printing (one int per line) inflates them ~10x and makes the
    // load parse minutes-slow -- e.g. a max_mull=6 antilife profile is 1.86 GB pretty vs ~250 MB compact,
    // 80 s vs a few s to load. Serialize compact when that block is present; small static-only profiles
    // stay pretty-printed for human readability. Parses identically either way.
    return profile.HasExhaustiveKeep() ? root.dump() : root.dump(2);
}

// Receives each exhaustive_keep entry AS IT IS PARSED (see ParseDeckProfileJson): the composition,
// its keep flags, and its bottom rows (null when the entry has no "bottom_keep"). The on-disk table
// builder streams these to disk so the maps are never built.
using EntrySink = std::function<void(std::vector<int>& comp, std::vector<char>& flags,
                                     std::vector<std::vector<int>>* rows)>;

// Returns a default profile if the JSON is malformed or missing expected keys. With `sink` null the
// exhaustive entries land in the returned policy's maps; with a sink they are handed to it instead
// and the returned policy carries only the block's header fields (buckets, scalars, provenance).
inline MulliganProfile ParseDeckProfileJson(const std::string& json_str, const EntrySink* sink)
{
    using json = nlohmann::json;

    // STREAMING siphon of exhaustive_keep.entries[] (the bulk -- antilife is 366k entries / 242MB JSON).
    // A full DOM parse of that array balloons to ~5GB of nlohmann value-nodes; instead a parse callback
    // extracts each entry into the compact map AS IT COMPLETES and returns false to drop it from the DOM,
    // so the DOM never holds more than one entry at a time. Everything else parses to DOM as before, so
    // the resulting profile is IDENTICAL -- only the transient load peak changes. A stack of container-
    // opener keys robustly identifies an entry (an array element of exhaustive_keep.entries) without any
    // fragile depth arithmetic. Decks with no exhaustive_keep never match => byte-identical for them too.
    ExhaustiveKeepPolicy ek_stream;
    std::vector<std::string> kstack;   // key that opened each open container ("" for array elements)
    std::string pending_key;
    auto cb = [&](int /*depth*/, json::parse_event_t event, json& parsed) -> bool
    {
        switch (event)
        {
        case json::parse_event_t::key:
            pending_key = parsed.get<std::string>();
            return true;
        case json::parse_event_t::object_start:
        case json::parse_event_t::array_start:
            kstack.push_back(pending_key);
            pending_key.clear();
            return true;
        case json::parse_event_t::object_end:
        {
            const size_t n = kstack.size();
            const bool is_entry = (n >= 3 && kstack[n - 2] == "entries" && kstack[n - 3] == "exhaustive_keep");
            if (is_entry)
            {
                std::vector<int> comp = parsed["comp"].get<std::vector<int>>();
                std::vector<char> flags;
                for (const json& v : parsed["keep"]) { flags.push_back(static_cast<char>(v.get<int>())); }
                std::vector<std::vector<int>> bk;
                const bool has_bk = parsed.contains("bottom_keep");
                if (has_bk)
                { for (const json& sub : parsed["bottom_keep"]) { bk.push_back(sub.get<std::vector<int>>()); } }
                if (sink) { (*sink)(comp, flags, has_bk ? &bk : nullptr); }
                else
                {
                    ek_stream.keep[comp] = std::move(flags);
                    if (has_bk) { ek_stream.bottom_keep[comp] = std::move(bk); }
                }
                kstack.pop_back();
                return false;   // drop the entry from the DOM (already captured)
            }
            kstack.pop_back();
            return true;
        }
        case json::parse_event_t::array_end:
            kstack.pop_back();
            return true;
        default:
            return true;
        }
    };
    json root = json::parse(json_str, cb);
    MulliganProfile profile;

    if (!root.contains("mulligan")) { return profile; }
    const json& m = root["mulligan"];

    if (m.contains("min_lands"))    { profile.min_lands    = m["min_lands"].get<int>(); }
    if (m.contains("max_lands"))    { profile.max_lands    = m["max_lands"].get<int>(); }
    if (m.contains("min_playable")) { profile.min_playable = m["min_playable"].get<int>(); }
    if (m.contains("stop_at"))      { profile.stop_at      = m["stop_at"].get<int>(); }

    // New field; fall back to skip_curve_check for old profiles.
    if (m.contains("curve_check"))
    {
        profile.curve_check = CurveCheckFromString(m["curve_check"].get<std::string>());
    }
    else if (m.contains("skip_curve_check") && m["skip_curve_check"].get<bool>())
    {
        profile.curve_check = CurveCheck::None;
    }

    if (m.contains("bottom_order"))
    {
        profile.bottom_order = BottomOrderFromString(m["bottom_order"].get<std::string>());
    }

    // Per-deck bottoming-eval policy (see MulliganProfile). Absent => -1/-1/0 => byte-identical.
    if (m.contains("bottom_eval_depth"))     { profile.bottom_eval_depth     = m["bottom_eval_depth"].get<int>(); }
    if (m.contains("bottom_eval_budget_ms")) { profile.bottom_eval_budget_ms = m["bottom_eval_budget_ms"].get<int>(); }
    if (m.contains("bottom_eval_topk"))      { profile.bottom_eval_topk      = m["bottom_eval_topk"].get<int>(); }

    if (m.contains("required_pieces"))
    {
        for (const json& piece : m["required_pieces"])
        {
            profile.required_pieces.push_back(piece.get<std::string>());
        }
    }

    if (m.contains("discard_protect"))
    {
        profile.discard_protect = DiscardProtectScopeFromString(m["discard_protect"].get<std::string>());
    }

    if (m.contains("min_color_sources"))
    {
        for (const auto& [key, val] : m["min_color_sources"].items())
        {
            try
            {
                profile.min_color_sources[CharToColor(key)] = val.get<int>();
            }
            catch (...) {}   // ignore unknown color strings
        }
    }

    if (root.contains("vial_target_mv"))
        profile.vial_target_mv = root["vial_target_mv"].get<int>();

    if (root.contains("card_scores"))
    {
        for (const auto& [name, arr] : root["card_scores"].items())
        {
            std::vector<double> marginals;
            for (const json& v : arr) { marginals.push_back(v.get<double>()); }
            profile.card_scores[name] = std::move(marginals);
        }
    }
    if (root.contains("hand_score_threshold"))
        profile.hand_score_threshold = root["hand_score_threshold"].get<double>();
    // Per-deck search-leaf fidelity (see MulliganProfile::search_leaf_depth). Absent => -1 => byte-identical.
    if (root.contains("search_leaf_depth"))
        profile.search_leaf_depth = root["search_leaf_depth"].get<int>();
    if (root.contains("search_leaf_first_turn_depth"))
        profile.search_leaf_first_turn_depth = root["search_leaf_first_turn_depth"].get<int>();

    if (root.contains("keep_model"))
        profile.keep_model = KeepModelFromJsonObj(root["keep_model"]);

    if (root.contains("exhaustive_keep"))
    {
        // Buckets + scalars come from the DOM; the entries were siphoned into ek_stream during parse
        // (root["exhaustive_keep"]["entries"] is now empty). Merge the two into the final policy.
        ExhaustiveKeepPolicy ek = ExhaustiveKeepFromJsonObj(root["exhaustive_keep"]);
        ek.keep        = std::move(ek_stream.keep);
        ek.bottom_keep = std::move(ek_stream.bottom_keep);
        ek.Index();
        profile.exhaustive_keep = std::make_shared<const ExhaustiveKeepPolicy>(std::move(ek));
    }

    return profile;
}

inline MulliganProfile DeckProfileFromJson(const std::string& json_str)
{
    return ParseDeckProfileJson(json_str, nullptr);
}

// Reads a profile file into a string, transparently decompressing gzip when the path ends in
// ".gz" (and zlib is linked). Committed exhaustive keep/bottom profiles ship gzipped -- the
// bottom_keep table is ~13 MB raw, so it lives in git as a ~1-2 MB .json.gz and the runtime reads
// it directly. Plain paths (and builds without zlib) use an ordinary read. Returns "" on failure.
inline std::string ReadProfileText(const std::filesystem::path& path)
{
    const std::string ext = path.extension().string();
    if (ext == ".gz" || ext == ".GZ")
    {
#ifdef MTG_HAVE_ZLIB
        gzFile gz = gzopen(path.string().c_str(), "rb");
        if (!gz) { return {}; }
        std::string out;
        char buf[1 << 16];
        int n;
        while ((n = gzread(gz, buf, sizeof(buf))) > 0) { out.append(buf, static_cast<std::size_t>(n)); }
        gzclose(gz);
        return out;
#else
        // No zlib in this build -> a .gz profile cannot be read. Say so ONCE and loudly: every
        // deck ships its exhaustive keep/bottom table as <name>.keepmodel.exhaustive.profile.json.gz,
        // so returning {} silently hands back a DEFAULT profile and the deck quietly plays different
        // mulligans than it does on a zlib build -- a behaviour divergence with no visible cause.
        // CMake now FetchContent's zlib when the system one is absent, so this branch should be
        // unreachable; the warning exists so that if it ever is reached, it is not silent.
        static std::once_flag warned;
        std::call_once(warned, [] {
            std::cerr << "[profile] WARNING: this build has no zlib -- gzipped (.gz) profiles "
                         "cannot be read, so mulligan tables fall back to defaults.\n";
        });
        std::cerr << "[profile]   unread: " << path.string() << "\n";
        return {};
#endif
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) { return {}; }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// Loads a DeckProfile from a file on disk (plain .json or gzipped .json.gz).
// Returns a default profile if the file cannot be opened or parsed.
inline MulliganProfile LoadDeckProfile(const std::filesystem::path& path)
{
    MulliganProfile profile;
    std::string content = ReadProfileText(path);
    if (!content.empty())
    {
        try   { profile = DeckProfileFromJson(content); }
        catch (...) { std::cerr << "[profload] SWALLOWED: base profile parse failed: " << path << "\n"; profile = MulliganProfile{}; }
    }
    // Durable human constraints live in a SEPARATE sibling file, loaded even when the profile itself
    // is absent/default (a deck can carry constraints without a generated profile). SaveDeckProfile
    // never writes them back, so regeneration cannot clobber them.
    profile.keep_constraints = LoadKeepConstraints(ConstraintsPathFor(path));
    return profile;
}

// Writes a DeckProfile to a file on disk. Returns true on success.
inline bool SaveDeckProfile(const std::filesystem::path& path, const MulliganProfile& profile)
{
    std::ofstream file(path);
    if (!file) { return false; }
    if (!profile.HasExhaustiveKeep())
    {
        // Small / static-only profiles: unchanged (pretty-printed, byte-identical to before).
        file << DeckProfileToJson(profile);
        return file.good();
    }
    // Exhaustive-keep profiles: STREAM the write. Building the whole document as ONE nlohmann DOM --
    // dominated by exhaustive_keep.entries[] (789k entries for Creature Giving) -- balloons to ~5 GB of
    // value-nodes PLUS a full dump-string copy, which OOMs a 10 GB box in the merge/gen tail. This is the
    // exact symmetric cost the LOAD side already avoids (see DeckProfileFromJson's streaming siphon): emit
    // the small head via a DOM, then stream entries one at a time so the transient DOM never holds more
    // than a single entry. Compact (no indent), matching the HasExhaustiveKeep() branch of
    // DeckProfileToJson. Parses identically -- JSON object key order is irrelevant, and every entry is
    // itself dumped by nlohmann so each is byte-identical to the monolithic path.
    using json = nlohmann::json;
    json root;
    root["version"]  = 1;
    root["mulligan"] = MulliganProfileToJsonObj(profile);
    if (profile.vial_target_mv > 0) { root["vial_target_mv"] = profile.vial_target_mv; }
    if (profile.search_leaf_depth >= 0) { root["search_leaf_depth"] = profile.search_leaf_depth; }
    if (profile.search_leaf_first_turn_depth >= 0) { root["search_leaf_first_turn_depth"] = profile.search_leaf_first_turn_depth; }
    if (!profile.card_scores.empty())
    {
        json cs = json::object();
        for (const auto& [name, marginals] : profile.card_scores)
        {
            json arr = json::array();
            for (double v : marginals) { arr.push_back(v); }
            cs[name] = arr;
        }
        root["card_scores"] = cs;
        root["hand_score_threshold"] = profile.hand_score_threshold;
    }
    if (!profile.keep_model.empty()) { root["keep_model"] = KeepModelToJsonObj(profile.keep_model); }
    // Everything except exhaustive_keep, dumped compact; drop the trailing '}' so we can append it.
    std::string head = root.dump();
    if (head.empty() || head.back() != '}') { return false; }
    head.pop_back();
    file << head;

    // exhaustive_keep header (small: max_mull/R/buckets/...); drop its trailing '}' to append entries.
    const ExhaustiveKeepPolicy& ek = *profile.exhaustive_keep;
    json ekh;
    ekh["max_mull"]          = ek.max_mull;
    ekh["effective_R"]       = ek.effective_R;
    ekh["bottoming_enabled"] = ek.bottoming_enabled;
    if (!ek.commit.empty())      { ekh["commit"]      = ek.commit; }
    if (!ek.play_digest.empty()) { ekh["play_digest"] = ek.play_digest; }
    json buckets = json::array();
    for (const std::vector<std::string>& b : ek.buckets)
    {
        json arr = json::array();
        for (const std::string& n : b) { arr.push_back(n); }
        buckets.push_back(arr);
    }
    ekh["buckets"] = buckets;
    std::string ekhs = ekh.dump();
    if (ekhs.empty() || ekhs.back() != '}') { return false; }
    ekhs.pop_back();
    file << (head.back() == '{' ? "" : ",") << "\"exhaustive_keep\":" << ekhs << ",\"entries\":[";

    // Stream the bulk: one entry DOM at a time (matches ExhaustiveKeepToJsonObj's per-entry shape).
    bool first = true;
    for (const auto& [comp, flags] : ek.keep)
    {
        json je;
        je["comp"] = comp;
        je["keep"] = json::array();
        for (char c : flags) { je["keep"].push_back(static_cast<int>(c)); }
        auto bit = ek.bottom_keep.find(comp);
        if (bit != ek.bottom_keep.end())
        {
            json bk = json::array();
            for (const std::vector<int>& sub : bit->second) { bk.push_back(sub); }
            je["bottom_keep"] = bk;
        }
        if (!first) { file << ','; }
        first = false;
        file << je.dump();
    }
    file << "]}}";   // close entries[], exhaustive_keep{}, root{}
    return file.good();
}

// For PLAY only: after loading a deck's base profile, pull in its exhaustive keep/bottom sidecar if
// one ships alongside -- `<deck>.profile.json` -> `<deck>.keepmodel.exhaustive.profile.json[.gz]`.
// The engine is presence-gated: keep always uses `exhaustive_keep` when present, and bottoming uses
// it iff the sidecar's `bottoming_enabled` is set -- so attaching the block is the whole wiring.
// Only the exhaustive_keep block is taken (base fields stay from the static profile). No-op if the
// loaded profile already has an exhaustive block (i.e. --profile pointed straight at it) or the path
// isn't a `<name>.profile.json`. NOT called by the analyzer's rollout-profile loads (that would be
// circular during generation) -- only from the game-play entry points.
// ---- On-disk keep table (the play-time backing) --------------------------------------------------
// The sidecar's parsed policy used to be cached as a `<sidecar>.bincache` blob beside it and decoded
// into the in-memory maps on every launch (~10 s and ~5.3 GB resident for FiveColour). Both are gone.
// Play loads now attach an on-disk, seekable table (ai/KeepTable.h) that is built ONCE per sidecar
// version by streaming the JSON entries straight into the table writer -- the maps are never built,
// so the one-time build peaks at the JSON text (~1.8 GB for FiveColour) instead of text + maps
// (~7.3 GB, which did not fit under this box's 8 GB RSS cap without a manual override), and every
// later launch is an open() plus a header read. MTG_KEEP_TABLE=0 (DEFAULT ON) forces the legacy
// in-memory load for A/B or debugging: slow and large, never wrong.
inline std::shared_ptr<const keeptable::Table>
BuildKeepTable(const std::filesystem::path& sidecar, const std::filesystem::path& table_path,
               const std::string& src_key, uint64_t src_size, uint64_t src_mtime)
{
    std::error_code ec;
    std::filesystem::create_directories(table_path.parent_path(), ec);
    // Uniquified temp so concurrent launches (the play server spawns one mtg per keep-hint) cannot
    // write into each other's file; a torn or failed build leaves no temp behind.
    std::random_device rd;
    const uint64_t nonce = (static_cast<uint64_t>(rd()) << 32) ^ rd();
    const std::filesystem::path tmp = table_path.string() + "." + std::to_string(nonce) + ".tmp";
    const auto t0 = std::chrono::steady_clock::now();
    std::cerr << "[keeptable] building " << table_path.filename().string() << " from " << sidecar.string() << " ...\n";

    std::string content = ReadProfileText(sidecar);
    if (content.empty()) { return nullptr; }
    keeptable::Writer w(tmp);
    struct Abort {};
    MulliganProfile head;   // everything BUT the entries, which the sink streams to the writer
    try
    {
        const EntrySink sink = [&](std::vector<int>& comp, std::vector<char>& flags,
                                   std::vector<std::vector<int>>* rows)
        { if (!w.Add(comp, flags, rows)) { throw Abort{}; } };
        head = ParseDeckProfileJson(content, &sink);
    }
    catch (const Abort&)            { /* w.error() says why */ }
    catch (const std::exception& e) { std::cerr << "[keeptable] parse failed: " << e.what() << "\n"; }
    catch (...)                     {}
    content.clear(); content.shrink_to_fit();
    auto give_up = [&](const std::string& why)
    {
        std::cerr << "[keeptable] NOT built (" << why << "); falling back to the in-memory policy\n";
        std::error_code rmec; std::filesystem::remove(tmp, rmec);
        return std::shared_ptr<const keeptable::Table>();
    };
    if (!w.ok())              { return give_up(w.error()); }
    if (!head.exhaustive_keep){ return give_up("no exhaustive_keep block"); }
    if (w.count() == 0)       { return give_up("no entries"); }
    keeptable::Header h;
    h.src_path = src_key; h.src_size = src_size; h.src_mtime = src_mtime;
    const ExhaustiveKeepPolicy& ek = *head.exhaustive_keep;
    h.buckets = ek.buckets; h.max_mull = ek.max_mull; h.bottoming_enabled = ek.bottoming_enabled;
    h.commit = ek.commit; h.play_digest = ek.play_digest; h.effective_R = ek.effective_R;
    if (!w.Finish(h))         { return give_up(w.error()); }
    std::filesystem::rename(tmp, table_path, ec);
    if (ec)
    {
        // Another launch raced us to the same table (Windows refuses to replace an open file). Ours
        // is discarded; whatever is there is validated below exactly like any other cache.
        std::error_code rmec; std::filesystem::remove(tmp, rmec);
    }
    std::string why;
    auto t = keeptable::Table::Open(table_path, src_key, src_size, src_mtime, &why);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (t)
    {
        std::cerr << "[keeptable] built " << t->size() << " compositions ("
                  << (static_cast<double>(std::filesystem::file_size(table_path, ec)) / 1048576.0) << " MB) in "
                  << secs << " s\n";
    }
    else { std::cerr << "[keeptable] built table failed to open (" << why << "); falling back to the in-memory policy\n"; }
    return t;
}

// Process-global cache of loaded exhaustive-keep policies, keyed by sidecar path. A --batch run loads
// the SAME deck's sidecar once PER JOB (treasure_hunt x5, Knights x5, ... across the seed/depth matrix);
// without this each job re-opened and re-validated the table (and, before the on-disk table, re-parsed
// it -- minutes of single-threaded startup before the worker pool even spawned). Mutex-guarded because
// attach can run off any thread. Empty policies are cached too (a missing/garbage sidecar isn't retried
// per job). Returns a SHARED handle (or nullptr if the path has none): one instance is shared by every
// profile that attaches it, so a THREADS=N batch holds 1 copy, not N. Read-only after load (Index()
// ran here), so concurrent readers are safe -- table lookups serialise inside the table itself.
inline std::shared_ptr<const ExhaustiveKeepPolicy> CachedExhaustiveKeep(const std::filesystem::path& path)
{
    static std::mutex mtx;
    static std::unordered_map<std::string, std::shared_ptr<const ExhaustiveKeepPolicy>> cache;
    const std::string key = path.string();
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cache.find(key);
    if (it != cache.end()) { return it->second; }

    std::shared_ptr<const ExhaustiveKeepPolicy> result;
    // DEFAULT ON; =0 forces the legacy in-memory load (the whole table as std::maps).
    static const bool use_table = EnvOn("MTG_KEEP_TABLE", true);
    // Source fingerprint for cache validation. On any FS error, skip the table entirely and just
    // parse (cache disabled, never wrong). Keyed by the CANONICAL path so a symlinked sidecar
    // (deck_compare's apparatus dirs, keep_delta's link) shares the real file's table instead of
    // building its own; size/mtime follow the link too.
    std::error_code ec_sz, ec_mt, ec_cn;
    const uint64_t src_size  = static_cast<uint64_t>(std::filesystem::file_size(path, ec_sz));
    const auto     wt        = std::filesystem::last_write_time(path, ec_mt);
    const uint64_t src_mtime = static_cast<uint64_t>(wt.time_since_epoch().count());
    const std::filesystem::path canon = std::filesystem::weakly_canonical(path, ec_cn);
    if (use_table && !ec_sz && !ec_mt && !ec_cn)
    {
        const std::string src_key = canon.string();
        const std::filesystem::path table_path = keeptable::KeepTableDir() / keeptable::TableFileName(src_key);
        std::string why;
        auto t = keeptable::Table::Open(table_path, src_key, src_size, src_mtime, &why);
        if (!t) { t = BuildKeepTable(path, table_path, src_key, src_size, src_mtime); }
        if (t)
        {
            ExhaustiveKeepPolicy ek;
            const keeptable::Header& h = t->header();
            ek.buckets = h.buckets; ek.max_mull = h.max_mull; ek.bottoming_enabled = h.bottoming_enabled;
            ek.commit = h.commit; ek.play_digest = h.play_digest; ek.effective_R = h.effective_R;
            ek.disk = t;
            ek.Index();
            result = std::make_shared<const ExhaustiveKeepPolicy>(std::move(ek));
        }
    }
    if (!result)
    {
        MulliganProfile exh = LoadDeckProfile(path);   // exh.exhaustive_keep is already shared_ptr<const>
        result = std::move(exh.exhaustive_keep);
    }
    cache.emplace(key, result);
    return result;
}

inline void AttachExhaustiveSidecar(MulliganProfile& profile, const std::filesystem::path& profile_path)
{
    if (profile.HasExhaustiveKeep()) { return; }

    // Explicit per-context override -- decouples "the profile under test" from the presence-gated
    // committed sidecar so A/Bs and candidate testing don't churn `decks/` or the deck's GT:
    //   MTG_EXHAUSTIVE_PROFILE=none|off|0|"" -> attach NOTHING (a genuine static baseline arm).
    //   MTG_EXHAUSTIVE_PROFILE=<path>        -> attach THAT profile's exhaustive block (candidate under
    //                                           test), without placing a `.gz` next to the deck.
    //   unset                                -> presence-gated auto-attach below (the adopted sidecar).
    // Process-global, so it's a single-deck tool for A/B; a suite run uses `none` (disable) or unset.
    if (const char* ov = std::getenv("MTG_EXHAUSTIVE_PROFILE"))
    {
        const std::string v = ov;
        if (v.empty() || v == "none" || v == "off" || v == "0") { return; }
        auto cached = CachedExhaustiveKeep(v);
        if (cached && !cached->empty()) { profile.exhaustive_keep = std::move(cached); }
        return;
    }

    const std::string fn = profile_path.filename().string();
    const std::string suffix = ".profile.json";
    if (fn.size() <= suffix.size() || fn.compare(fn.size() - suffix.size(), suffix.size(), suffix) != 0)
    { return; }
    const std::string stem = fn.substr(0, fn.size() - suffix.size());
    const std::filesystem::path dir = profile_path.parent_path();

    // Resolve ONCE per profile path and memoize, so the .gz-vs-.json choice cannot flip within a
    // process: exists() is re-run per attach, and on a networked filesystem (this repo lives on 9p)
    // a transient exists(.gz) failure would silently route one job of a batch to the .json fallback
    // -- the best surviving candidate for the 2026-08-26 batch-pool contamination (see
    // batch-pool-contamination memory / docs). Memoizing also freezes sidecar PRESENCE for the
    // process lifetime, which is what a measurement run wants (a generation dropping a sidecar
    // mid-batch must not change the arms already running).
    static std::mutex s_resolve_mu;
    static std::unordered_map<std::string, std::string> s_resolved;   // profile path -> sidecar ("" = none)
    const std::string pkey = profile_path.string();
    std::string resolved;
    {
        // The whole resolve runs under the lock so concurrent first attaches cannot each run
        // exists() and disagree; exists() here is only paid on the first attach per path.
        std::lock_guard<std::mutex> lk(s_resolve_mu);
        auto it = s_resolved.find(pkey);
        if (it == s_resolved.end())
        {
            for (const char* ext : { ".keepmodel.exhaustive.profile.json.gz", ".keepmodel.exhaustive.profile.json" })
            {
                const std::filesystem::path cand = dir / (stem + ext);
                if (std::filesystem::exists(cand))
                {
                    resolved = cand.string();
                    // Decks ship this sidecar gzipped, so the uncompressed fallback should never be
                    // reached: it means either a hand-placed .json (legitimate but worth knowing) or
                    // a transient exists(.gz) miss. Loud, and only once per path (memoized).
                    if (std::strcmp(ext, ".keepmodel.exhaustive.profile.json") == 0)
                    {
                        std::cerr << "[profile] WARNING: exhaustive sidecar resolved to the UNCOMPRESSED "
                                     ".json fallback (decks ship .gz): " << resolved << "\n";
                    }
                    break;
                }
            }
            it = s_resolved.emplace(pkey, resolved).first;
        }
        resolved = it->second;
    }
    if (resolved.empty()) { return; }
    auto cached = CachedExhaustiveKeep(resolved);
    if (cached && !cached->empty()) { profile.exhaustive_keep = std::move(cached); }
}

// --- Learned mid-game eval sidecar (decks/<name>.eval.json) ------------------------------------
// coefs keyed by MidGameFeature NAME (robust to the append-only enum growing), + intercept.
inline nlohmann::json EvalModelToJsonObj(const MidGameEvaluator& e)
{
    using json = nlohmann::json;
    json m;
    m["intercept"] = e.intercept;
    json coefs = json::object();
    for (int j = 0; j < static_cast<int>(e.coefs.size()) && j < static_cast<int>(MidGameFeature::Count); ++j)
    { if (e.coefs[j] != 0) { coefs[MidGameFeatureName(static_cast<MidGameFeature>(j))] = e.coefs[j]; } }
    m["coefs"] = coefs;
    // GBDT ensemble (optional). Split features by NAME (enum-robust, like coefs). Compact per-node
    // arrays: leaf = [value]; internal = [feat_name, threshold, left, right].
    if (!e.trees.empty())
    {
        json trees = json::array();
        for (const std::vector<MidGameTreeNode>& t : e.trees)
        {
            json nodes = json::array();
            for (const MidGameTreeNode& n : t)
            {
                if (n.feature < 0) { nodes.push_back(json::array({ n.value })); }
                else
                {
                    const char* nm = (n.feature < static_cast<int>(MidGameFeature::Count))
                                   ? MidGameFeatureName(static_cast<MidGameFeature>(n.feature)) : "?";
                    nodes.push_back(json::array({ nm, n.threshold, n.left, n.right }));
                }
            }
            trees.push_back(std::move(nodes));
        }
        m["trees"] = std::move(trees);
    }
    return m;
}

inline MidGameEvaluator EvalModelFromJsonObj(const nlohmann::json& j)
{
    MidGameEvaluator e;
    // Accept either the model object directly or a wrapper { "eval_model": {...} }.
    const nlohmann::json& m = j.contains("eval_model") ? j["eval_model"] : j;
    const bool has_coefs = m.contains("coefs") && m["coefs"].is_object() && !m["coefs"].empty();
    const bool has_trees = m.contains("trees") && m["trees"].is_array() && !m["trees"].empty();
    // Neither part present -> the EMPTY model (inert; heuristic ranking), never an all-zero "active"
    // model that would silently re-tiebreak plans.
    if (!has_coefs && !has_trees) { return e; }
    e.intercept = m.value("intercept", 0LL);
    if (has_coefs)
    {
        e.coefs.assign(static_cast<int>(MidGameFeature::Count), 0LL);
        for (const auto& [name, val] : m["coefs"].items())
        {
            const int idx = MidGameFeatureFromName(name);
            if (idx >= 0 && idx < static_cast<int>(e.coefs.size())) { e.coefs[idx] = val.get<long long>(); }
        }
    }
    if (has_trees)
    {
        for (const auto& jt : m["trees"])
        {
            if (!jt.is_array()) { continue; }
            std::vector<MidGameTreeNode> tree;
            tree.reserve(jt.size());
            for (const auto& jn : jt)
            {
                MidGameTreeNode node;
                if (jn.is_array() && jn.size() == 1) { node.value = jn[0].get<long long>(); }   // leaf
                else if (jn.is_array() && jn.size() == 4)
                {
                    node.feature   = MidGameFeatureFromName(jn[0].get<std::string>());
                    node.threshold = jn[1].get<int>();
                    node.left      = jn[2].get<int>();
                    node.right     = jn[3].get<int>();
                }
                tree.push_back(node);
            }
            if (!tree.empty()) { e.trees.push_back(std::move(tree)); }
        }
    }
    // A/B knob (docs/design/value-model-shrink.md): keep only the first N boosted trees. A GBDT is
    // additive (Score = intercept + linear + Sum trees), so the first N trees form a valid *weaker*
    // model -- this lets us measure the play-quality vs rollout-speed trade of a smaller value leaf
    // without retraining. Unset / <=0 / >=count => the full model (byte-identical load).
    if (const char* mt = std::getenv("MTG_EVAL_MAX_TREES"))
    {
        const int keep = std::atoi(mt);
        if (keep > 0 && keep < static_cast<int>(e.trees.size())) { e.trees.resize(static_cast<size_t>(keep)); }
    }
    // Perf-only re-layout of the ensemble (see MidGameEvaluator::BuildFlat). Built HERE, at load, so
    // Score() stays const and lock-free -- a lazy build would race across the search's worker threads.
    // Must come after MTG_EVAL_MAX_TREES above, which changes which trees exist.
    // MTG_VALUE_FLAT=0 keeps the original pointer walk (standing A/B lever on one binary; Score() must
    // return the same long long either way, so any digest difference between the arms is a bug).
    if (EnvOn("MTG_VALUE_FLAT", true)) { e.BuildFlat(); }
    return e;
}

// For PLAY only: after loading a deck's base profile, pull in its learned mid-game eval sidecar if one
// ships alongside -- `<deck>.profile.json` -> `<deck>.eval.json`. Presence-gated: absent/malformed =>
// eval_model stays empty => the heuristic ranking (byte-identical). The learned ranking is additionally
// gated at runtime by MTG_EVAL_MODEL (UseLearnedEval), so a shipped sidecar is inert until deliberately
// enabled. NOT called by the analyzer's rollout loads. See docs/design/learned-d0-policy.md.
inline void AttachEvalSidecar(MulliganProfile& profile, const std::filesystem::path& profile_path)
{
    if (!profile.eval_model.empty()) { return; }

    auto load_from = [&](const std::filesystem::path& p)
    {
        std::ifstream f(p);
        if (!f) { return; }
        try { nlohmann::json j; f >> j; profile.eval_model = EvalModelFromJsonObj(j); }
        catch (...) { std::cerr << "[profload] SWALLOWED: eval sidecar parse failed: " << p << "\n"; profile.eval_model = MidGameEvaluator{}; }
    };

    // Per-context override (decouples "the model under test" from the committed sidecar for A/B):
    //   MTG_EVAL_PROFILE=none|off|0|"" -> attach NOTHING; <path> -> attach THAT file's eval model.
    if (const char* ov = std::getenv("MTG_EVAL_PROFILE"))
    {
        const std::string v = ov;
        if (v.empty() || v == "none" || v == "off" || v == "0") { return; }
        load_from(v);
        return;
    }

    const std::string fn = profile_path.filename().string();
    const std::string suffix = ".profile.json";
    if (fn.size() <= suffix.size() || fn.compare(fn.size() - suffix.size(), suffix.size(), suffix) != 0)
    { return; }
    const std::string stem = fn.substr(0, fn.size() - suffix.size());
    const std::filesystem::path cand = profile_path.parent_path() / (stem + ".eval.json");
    if (std::filesystem::exists(cand)) { load_from(cand); }
}

// For PLAY only: pull in the deck's learned leaf VALUE sidecar (`<deck>.value.json`) the same way as
// the eval sidecar. Presence-gated + runtime-gated by MTG_VALUE_MODEL (inert until enabled). Override:
// MTG_VALUE_PROFILE=none|off|0|"" -> attach nothing; <path> -> that file. Reuses EvalModelFromJsonObj
// (same schema); the value model's coefs/trees predict a WIN TURN. See docs/design/learned-d0-policy.md.
inline void AttachValueSidecar(MulliganProfile& profile, const std::filesystem::path& profile_path)
{
    profile.value_source = profile_path.string();
    if (!profile.value_model.empty()) { return; }
    // value_play.leaf == "none" (per deck) => swap whatever model the sidecar carried for the NO-LEAF
    // stand-in; trust depth 0 so nothing unverified is ever kept. The sidecar's other blocks (value_play,
    // expected_buckets, mull_gen settings, the table) stay as parsed.
    // ... unless the emulated ladder COMMITS on the model (commit "model"): then the model stays, with its
    // trust depth, and only the warm-ups run leafless (valuearm::t_deck_warm_none, set per decision).
    auto apply_leaf_policy = [&]()
    {
        if (profile.value_play.leaf == "none" && profile.value_play.commit != "model")
        { profile.value_model = MidGameEvaluator::Constant(); profile.value_trust_depth = 0; }
    };

    auto load_from = [&](const std::filesystem::path& p)
    {
        std::ifstream f(p);
        if (!f) { return; }
        try
        {
            nlohmann::json j; f >> j;
            profile.value_model = EvalModelFromJsonObj(j);
            // Optional per-model trust depth (top-level or under the eval_model wrapper). See
            // MulliganProfile::value_trust_depth.
            const nlohmann::json& mm = j.contains("eval_model") ? j["eval_model"] : j;
            profile.value_trust_depth = mm.value("value_trust_depth", j.value("value_trust_depth", 0));
            profile.value_no_fallback = mm.value("value_no_fallback", j.value("value_no_fallback", false));
            // Table-driven take-crossover (see MulliganProfile::value_fallback_take_at). Build a by-committed-
            // depth lookup from the (committed_depths, take_heuristic_at_hdepth) pair; presence of this table
            // makes the hybrid use the measured per-depth crossover instead of the uniform offset.
            profile.value_fallback_take_at.clear();
            profile.value_fallback_max_depth = 0;
            const nlohmann::json& xo = mm.contains("value_fallback_crossover") ? mm["value_fallback_crossover"]
                                     : j.value("value_fallback_crossover", nlohmann::json::object());
            if (xo.contains("committed_depths") && xo.contains("take_heuristic_at_hdepth"))
            {
                const auto& cds = xo["committed_depths"];
                const auto& tks = xo["take_heuristic_at_hdepth"];
                int maxc = 0;
                for (const auto& c : cds) { maxc = std::max(maxc, c.get<int>()); }
                if (maxc > 0 && cds.size() == tks.size())
                {
                    profile.value_fallback_take_at.assign(static_cast<std::size_t>(maxc) + 1, 0);
                    for (std::size_t i = 0; i < cds.size(); ++i)
                    {
                        const int c = cds[i].get<int>();
                        if (c >= 1 && c <= maxc) { profile.value_fallback_take_at[static_cast<std::size_t>(c)] = tks[i].get<int>(); }
                    }
                    // Fill any unmeasured interior committed depths from the nearest lower measured one (monotone
                    // step), so every c in [1,maxc] has a defined crossover.
                    for (int c = 1; c <= maxc; ++c)
                    {
                        if (profile.value_fallback_take_at[static_cast<std::size_t>(c)] == 0)
                        { profile.value_fallback_take_at[static_cast<std::size_t>(c)] =
                              (c > 1) ? profile.value_fallback_take_at[static_cast<std::size_t>(c) - 1] : 1; }
                    }
                    profile.value_fallback_max_depth = xo.value("max_table_depth", maxc);
                }
            }

            // Per-deck play policy (see MulliganProfile::ValuePlay). Absent => value_play stays unset
            // (present()==false). An UNENABLED block (enabled=false, the default) is a recorded recommendation
            // that ResolvePlaySettings IGNORES -- only enabled=true drives/locks play. Cost arrays are
            // informative-only, parsed defensively (null/index-0 -> 0) so a malformed curve can't wipe the model.
            const nlohmann::json& vp = mm.contains("value_play") ? mm["value_play"]
                                     : j.value("value_play", nlohmann::json::object());
            // Parse when ANY known key is present, not only target_depth: a presence-only sidecar may
            // carry JUST the mull_gen_* overrides (play stays default), and gating the whole block on
            // target_depth silently dropped them -- the Mirrorwing recommend scout ran at the d5/b20
            // gen default instead of the deck's mull_gen d3/b3. With target_depth absent it parses to
            // 0, so present()/drives() stay false and play is untouched.
            // expected_buckets belongs in this list too: phase F writes a value_play holding ONLY
            // that key when the deck ships no play policy (nothing to reference a mull_gen setting
            // against), and it is READ below -- so omitting it here dropped the block, read K as 0,
            // and silently disarmed the very guard phase F says to rely on ("every later generation
            // must match it or it refuses to run"). Same defect as the mull_gen_* one above, one key
            // over. Caught on Mirrorwing 2026-08-22.
            if (vp.is_object() && (vp.contains("target_depth") || vp.contains("mull_gen_depth")
                                   || vp.contains("mull_gen_budget_ms")
                                   || vp.contains("expected_buckets")
                                   || vp.contains("ladder") || vp.contains("leaf") || vp.contains("commit")
                                   || vp.contains("alpha") || vp.contains("exhaust_mult")
                                   || vp.contains("fit_alpha") || vp.contains("fit_lazy_r")))
            {
                profile.value_play.ladder       = vp.value("ladder", std::string(""));
                profile.value_play.leaf         = vp.value("leaf", std::string(""));
                profile.value_play.commit       = vp.value("commit", std::string(""));
                profile.value_play.alpha        = vp.value("alpha", std::string(""));
                profile.value_play.exhaust_mult = vp.value("exhaust_mult", 0.0);
                profile.value_play.target_depth = vp.value("target_depth", 0);
                profile.value_play.budget_ms    = vp.value("budget_ms", 0);
                profile.value_play.enabled      = vp.value("enabled", false);
                profile.value_play.escalation_fresh_frac = vp.value("escalation_fresh_frac", -1.0);
                profile.value_play.fit_alpha  = vp.value("fit_alpha", 0.0);
                profile.value_play.fit_lazy_r = vp.contains("fit_lazy_r")
                                              ? (vp["fit_lazy_r"].get<bool>() ? 1 : 0) : -1;
                profile.value_play.beam_width     = vp.value("beam_width", 0);
                profile.value_play.beam_leafdepth = vp.value("beam_leafdepth", 2);
                profile.value_play.escalation_cap = vp.value("escalation_cap", 0);
                profile.value_play.escalation_r   = vp.value("escalation_r", 0.0);
                profile.value_play.regime       = vp.value("regime", std::string(""));
                // Optional mulligan-gen depth/budget override (0 => inherit the play depth/budget).
                profile.value_play.mull_gen_depth     = vp.value("mull_gen_depth", 0);
                profile.value_play.mull_gen_budget_ms = vp.value("mull_gen_budget_ms", 0);
                profile.value_play.expected_buckets   = vp.value("expected_buckets", 0);
                auto parse_costs = [](const nlohmann::json& arr) {
                    std::vector<double> v;
                    if (arr.is_array()) { for (const auto& e : arr) { v.push_back(e.is_number() ? e.get<double>() : 0.0); } }
                    return v;
                };
                if (vp.contains("leaf_cost_ms")) { profile.value_play.leaf_cost_ms = parse_costs(vp["leaf_cost_ms"]); }
                if (vp.contains("heur_cost_ms")) { profile.value_play.heur_cost_ms = parse_costs(vp["heur_cost_ms"]); }
            }
        }
        catch (...) { std::cerr << "[profload] SWALLOWED: value sidecar parse failed\n";
                      profile.value_model = MidGameEvaluator{}; profile.value_trust_depth = 0;
                      profile.value_no_fallback = false; profile.value_fallback_take_at.clear();
                      profile.value_fallback_max_depth = 0; profile.value_play = ValuePlay{}; }
    };

    // Per-job override first (see ValueArm.h), then the env. Empty override = unset; a SET override
    // of "none"/"off"/"0" explicitly means NO sidecar, which is how an H-arm job gets the pure
    // heuristic leaf on a deck that ships a model -- sidecar PRESENCE is what activates the hybrid,
    // so leaving the model attached and only turning MTG_VALUE_MODEL off is not the same thing.
    const char* env_ov = std::getenv("MTG_VALUE_PROFILE");
    const bool  arm_ov = !valuearm::t_arm.value_profile.empty();
    if (arm_ov || env_ov)
    {
        const std::string v = arm_ov ? valuearm::t_arm.value_profile : std::string(env_ov);
        if (v.empty() || v == "none" || v == "off" || v == "0") { return; }
        // "noleaf": attach the NO-LEAF stand-in with no sidecar at all (A/B arm / env hatch).
        if (v == "noleaf") { profile.value_model = MidGameEvaluator::Constant(); profile.value_trust_depth = 0; return; }
        load_from(v);
        apply_leaf_policy();
        return;
    }

    const std::string fn = profile_path.filename().string();
    const std::string suffix = ".profile.json";
    if (fn.size() <= suffix.size() || fn.compare(fn.size() - suffix.size(), suffix.size(), suffix) != 0)
    { return; }
    const std::string stem = fn.substr(0, fn.size() - suffix.size());
    const std::filesystem::path cand = profile_path.parent_path() / (stem + ".value.json");
    if (std::filesystem::exists(cand)) { load_from(cand); apply_leaf_policy(); }
}
