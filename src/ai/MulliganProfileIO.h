#pragma once
#include "MulliganProfile.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
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
    catch (...) { return KeepConstraints{}; }
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

    json pieces = json::array();
    for (const std::string& s : profile.required_pieces) { pieces.push_back(s); }
    m["required_pieces"] = pieces;

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
    if (e.contains("bottoming_enabled")) { ek.bottoming_enabled = e["bottoming_enabled"].get<bool>(); }
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

// Returns a default profile if the JSON is malformed or missing expected keys.
inline MulliganProfile DeckProfileFromJson(const std::string& json_str)
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
                ek_stream.keep[comp] = std::move(flags);
                if (parsed.contains("bottom_keep"))
                {
                    std::vector<std::vector<int>> bk;
                    for (const json& sub : parsed["bottom_keep"]) { bk.push_back(sub.get<std::vector<int>>()); }
                    ek_stream.bottom_keep[comp] = std::move(bk);
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

    if (m.contains("required_pieces"))
    {
        for (const json& piece : m["required_pieces"])
        {
            profile.required_pieces.push_back(piece.get<std::string>());
        }
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
        return {};   // no zlib in this build -> a .gz profile cannot be read
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
        catch (...) { profile = MulliganProfile{}; }
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
    file << DeckProfileToJson(profile);
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
// Process-global cache of parsed exhaustive-keep blocks, keyed by resolved sidecar path. The block is
// large (the committed .gz is ~1-2 MB, ~13 MB raw JSON), and a --batch run loads the SAME deck's sidecar
// once PER JOB (treasure_hunt x5, Knights x5, ... across the seed/depth matrix). Without this each job
// re-opens, re-gunzips and re-parses the whole table -- minutes of single-threaded startup before the
// worker pool even spawns (measured: the batch sat at 1 thread for many minutes on the exhaustive-keep
// decks). Parse once per path, then hand back a copy: the sidecar file is immutable for a run, so keying
// on its path is exact. Mutex-guarded because attach can run off any thread (defensive; batch loads jobs
// on the main thread). Empty policies are cached too (a missing/garbage sidecar isn't retried per job).
// Returns a SHARED handle to the cached policy (or nullptr if the path has none). The one loaded instance
// is shared by every profile that attaches it, so a THREADS=N batch holds 1 copy, not N. Read-only after
// load (Index() ran in the loader), so concurrent readers are safe.
inline std::shared_ptr<const ExhaustiveKeepPolicy> CachedExhaustiveKeep(const std::filesystem::path& path)
{
    static std::mutex mtx;
    static std::unordered_map<std::string, std::shared_ptr<const ExhaustiveKeepPolicy>> cache;
    const std::string key = path.string();
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cache.find(key);
    if (it == cache.end())
    {
        MulliganProfile exh = LoadDeckProfile(path);   // exh.exhaustive_keep is already a shared_ptr<const>
        it = cache.emplace(key, std::move(exh.exhaustive_keep)).first;
    }
    return it->second;
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
    for (const char* ext : { ".keepmodel.exhaustive.profile.json.gz", ".keepmodel.exhaustive.profile.json" })
    {
        const std::filesystem::path cand = dir / (stem + ext);
        if (std::filesystem::exists(cand))
        {
            auto cached = CachedExhaustiveKeep(cand);
            if (cached && !cached->empty()) { profile.exhaustive_keep = std::move(cached); }
            return;
        }
    }
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
        catch (...) { profile.eval_model = MidGameEvaluator{}; }
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
    if (!profile.value_model.empty()) { return; }

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
            if (vp.is_object() && vp.contains("target_depth"))
            {
                profile.value_play.target_depth = vp.value("target_depth", 0);
                profile.value_play.budget_ms    = vp.value("budget_ms", 0);
                profile.value_play.enabled      = vp.value("enabled", false);
                profile.value_play.escalation_fresh_frac = vp.value("escalation_fresh_frac", -1.0);
                profile.value_play.beam_width     = vp.value("beam_width", 0);
                profile.value_play.beam_leafdepth = vp.value("beam_leafdepth", 2);
                profile.value_play.regime       = vp.value("regime", std::string(""));
                auto parse_costs = [](const nlohmann::json& arr) {
                    std::vector<double> v;
                    if (arr.is_array()) { for (const auto& e : arr) { v.push_back(e.is_number() ? e.get<double>() : 0.0); } }
                    return v;
                };
                if (vp.contains("leaf_cost_ms")) { profile.value_play.leaf_cost_ms = parse_costs(vp["leaf_cost_ms"]); }
                if (vp.contains("heur_cost_ms")) { profile.value_play.heur_cost_ms = parse_costs(vp["heur_cost_ms"]); }
            }
        }
        catch (...) { profile.value_model = MidGameEvaluator{}; profile.value_trust_depth = 0;
                      profile.value_no_fallback = false; profile.value_fallback_take_at.clear();
                      profile.value_fallback_max_depth = 0; profile.value_play = ValuePlay{}; }
    };

    if (const char* ov = std::getenv("MTG_VALUE_PROFILE"))
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
    const std::filesystem::path cand = profile_path.parent_path() / (stem + ".value.json");
    if (std::filesystem::exists(cand)) { load_from(cand); }
}
