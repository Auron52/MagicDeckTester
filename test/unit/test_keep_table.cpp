// Unit cover for the ON-DISK exhaustive keep table (src/ai/KeepTable.h) and its wiring into
// ExhaustiveKeepPolicy / CachedExhaustiveKeep (src/ai/MulliganProfileIO.h).
//
// The policy has two backings -- the generator's in-memory maps and the play-time on-disk table --
// and the whole correctness claim is that they DECIDE IDENTICALLY. A divergence would not crash: it
// would ship a different mulligan policy than the one the generator evaluated, silently, which is the
// worst kind of engine bug (it moves the fingerprint by a plausible amount). So the first case builds
// a table from a policy and checks Decide/DecideBottom agree at EVERY key x mull x play/draw, including
// the out-of-range mull that exercises the min(mull, max_mull) clamp, deferred (empty) rows, entries
// with no bottom table at all, and absent compositions. The payload is deliberately larger than the
// writer's 8 MB buffer so the multi-flush path is what gets tested.
#include <doctest/doctest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include "../../src/ai/MulliganProfileIO.h"

namespace
{
ExhaustiveKeepPolicy MakePolicy(int cells, int max_mull = 6)
{
    ExhaustiveKeepPolicy ek;
    ek.buckets = { {"Misty Rainforest", "Scalding Tarn"}, {"Birds of Paradise"}, {"Island"}, {"Tarmogoyf"} };
    ek.max_mull = max_mull;
    ek.bottoming_enabled = true;
    ek.commit = "2f7822a2";
    ek.play_digest = "b79a141457869ca5";
    ek.effective_R = 30;
    const int F = (max_mull + 1) * 2;
    const int K = static_cast<int>(ek.buckets.size());
    for (int i = 0; i < cells; ++i)
    {
        // DISTINCT keys (these land in a std::map; a colliding generator silently shrinks the fixture).
        const std::vector<int> comp{ i % 32, (i / 32) % 32, (i / 1024) % 32, (i / 32768) % 32 };
        std::vector<char> flags(static_cast<std::size_t>(F));
        for (int f = 0; f < F; ++f) { flags[f] = ((i + f) % 3 == 0) ? 1 : 0; }
        ek.keep[comp] = flags;
        if (i % 7 == 3) { continue; }   // no bottom table for this composition at all
        std::vector<std::vector<int>> rows(static_cast<std::size_t>(F));
        for (int f = 0; f < F; ++f)
        {
            if (f % 5 == 0) { continue; }   // deferred row (empty) -> DecideBottom must say false
            rows[f] = { i, f, i ^ f, K };
        }
        ek.bottom_keep[comp] = rows;
    }
    ek.Index();
    return ek;
}

// A hand whose bucket composition is `comp` (bucket b contributes comp[b] copies of its first member).
std::vector<std::string> HandFor(const ExhaustiveKeepPolicy& ek, const std::vector<int>& comp)
{
    std::vector<std::string> hand;
    for (std::size_t b = 0; b < comp.size(); ++b)
        for (int c = 0; c < comp[b]; ++c) { hand.push_back(ek.buckets[b][0]); }
    return hand;
}

// The test-side table builder: what the production loader does from JSON, done from the maps.
bool WriteTable(const std::filesystem::path& p, const ExhaustiveKeepPolicy& ek,
                uint64_t hash, uint64_t sz, std::string* err = nullptr, const std::string& src = "src.gz")
{
    keeptable::Writer w(p);
    for (const auto& kv : ek.keep)
    {
        auto bit = ek.bottom_keep.find(kv.first);
        if (!w.Add(kv.first, kv.second, bit == ek.bottom_keep.end() ? nullptr : &bit->second))
        { if (err) { *err = w.error(); } return false; }
    }
    keeptable::Header h;
    h.src_path = src; h.src_size = sz; h.src_mtime = 1; h.content_hash = hash;
    h.buckets = ek.buckets; h.max_mull = ek.max_mull; h.bottoming_enabled = ek.bottoming_enabled;
    h.commit = ek.commit; h.play_digest = ek.play_digest; h.effective_R = ek.effective_R;
    const bool ok = w.Finish(h);
    if (!ok && err) { *err = w.error(); }
    return ok;
}

ExhaustiveKeepPolicy DiskBacked(std::shared_ptr<const keeptable::Table> t)
{
    ExhaustiveKeepPolicy ek;
    const keeptable::Header& h = t->header();
    ek.buckets = h.buckets; ek.max_mull = h.max_mull; ek.bottoming_enabled = h.bottoming_enabled;
    ek.commit = h.commit; ek.play_digest = h.play_digest; ek.effective_R = h.effective_R;
    ek.disk = std::move(t);
    ek.Index();
    return ek;
}

// Every decision both backings can make for one composition, compared.
void CheckAgree(const ExhaustiveKeepPolicy& mem, const ExhaustiveKeepPolicy& dsk, const std::vector<int>& comp)
{
    const std::vector<std::string> hand = HandFor(mem, comp);
    for (int mull = 0; mull <= mem.max_mull + 1; ++mull)   // +1: the min(mull, max_mull) clamp
        for (int pd = 0; pd < 2; ++pd)
        {
            bool pm = false, pdk = false;
            const bool km = mem.Decide(hand, mull, pd == 1, pm);
            const bool kd = dsk.Decide(hand, mull, pd == 1, pdk);
            CHECK(pm == pdk);
            if (pm) { CHECK(km == kd); }
            std::vector<int> tm, td;
            const bool bm = mem.DecideBottom(hand, mull, pd == 1, tm);
            const bool bd = dsk.DecideBottom(hand, mull, pd == 1, td);
            CHECK(bm == bd);
            if (bm) { CHECK(tm == td); }
        }
}

std::filesystem::path TmpDir()
{
    const std::filesystem::path d = std::filesystem::temp_directory_path() / "mtg_keep_table_test";
    std::filesystem::create_directories(d);
    return d;
}

// Best-effort cleanup. On Windows a file cannot be deleted while a handle is open on it, and the
// Table keeps its ifstream open for its lifetime (the loader case holds it in a process-global cache
// for the rest of the run) -- so cleanup must never be able to fail a test that already passed.
void Cleanup(const std::filesystem::path& p)
{
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
}
}  // namespace

TEST_CASE("keep table: the on-disk backing decides identically to the in-memory maps")
{
    // 40k cells x 258 B records = ~10 MB: past the writer's 8 MB buffer, so records straddle a flush.
    const ExhaustiveKeepPolicy mem = MakePolicy(40000);
    const std::filesystem::path p = TmpDir() / "agree.keeptable";
    Cleanup(p);
    std::string err;
    REQUIRE_MESSAGE(WriteTable(p, mem, 5678, 1234, &err), err);
    CHECK(std::filesystem::file_size(p) > (8u << 20));
    {   // scoped so the table's handle is closed before the file is removed
        std::string why;
        auto t = keeptable::Table::Open(p, 5678, 1234, &why);
        REQUIRE_MESSAGE(t, why);
        CHECK(t->size() == mem.keep.size());
        CHECK(t->header().K == 4);
        CHECK(t->header().F_keep == 14);
        CHECK(t->header().F_bot == 14);
        const ExhaustiveKeepPolicy dsk = DiskBacked(t);
        CHECK_FALSE(dsk.empty());
        CHECK(dsk.table_size() == mem.keep.size());
        CHECK(dsk.buckets == mem.buckets);
        CHECK(dsk.commit == mem.commit);
        CHECK(dsk.play_digest == mem.play_digest);
        CHECK(dsk.effective_R == mem.effective_R);
        CHECK(dsk.bottoming_enabled == mem.bottoming_enabled);

        for (const auto& kv : mem.keep) { CheckAgree(mem, dsk, kv.first); }

        // Absent compositions (never generated) and an unbucketed card: both backings decline.
        std::mt19937 rng(7);
        for (int i = 0; i < 2000; ++i)
        {
            const std::vector<int> comp{ 40 + static_cast<int>(rng() % 20), static_cast<int>(rng() % 32),
                                         static_cast<int>(rng() % 32), static_cast<int>(rng() % 32) };
            REQUIRE(mem.keep.find(comp) == mem.keep.end());
            CheckAgree(mem, dsk, comp);
        }
        bool present = true;
        CHECK_FALSE(dsk.Decide({ "Not A Card" }, 0, true, present));
        CHECK_FALSE(present);
        std::vector<int> tgt;
        CHECK_FALSE(dsk.DecideBottom({ "Not A Card" }, 1, true, tgt));
    }
    Cleanup(p);
}

TEST_CASE("keep table: a table with no bottom rows anywhere is keep-only")
{
    ExhaustiveKeepPolicy mem = MakePolicy(500);
    mem.bottom_keep.clear();
    const std::filesystem::path p = TmpDir() / "keeponly.keeptable";
    Cleanup(p);
    REQUIRE(WriteTable(p, mem, 1, 1));
    {
        auto t = keeptable::Table::Open(p, 1, 1);
        REQUIRE(t);
        CHECK(t->header().F_bot == 0);
        const ExhaustiveKeepPolicy dsk = DiskBacked(t);
        for (const auto& kv : mem.keep) { CheckAgree(mem, dsk, kv.first); }
    }
    Cleanup(p);
}

TEST_CASE("keep table: shape and order violations are refused at build time")
{
    const std::filesystem::path p = TmpDir() / "refuse.keeptable";
    const std::vector<char> flags(14, 1);
    {
        keeptable::Writer w(p);
        CHECK(w.Add({ 0, 0, 0, 1 }, flags, nullptr));
        CHECK_FALSE(w.Add({ 0, 0, 1 }, flags, nullptr));          // K changed
        CHECK_FALSE(w.ok());
    }
    {
        keeptable::Writer w(p);
        CHECK(w.Add({ 0, 0, 0, 1 }, flags, nullptr));
        CHECK_FALSE(w.Add({ 0, 0, 0, 0 }, flags, nullptr));       // not ascending
    }
    {
        keeptable::Writer w(p);
        CHECK(w.Add({ 0, 0, 0, 1 }, flags, nullptr));
        CHECK_FALSE(w.Add({ 0, 0, 0, 1 }, flags, nullptr));       // duplicate key is not ascending either
    }
    {
        keeptable::Writer w(p);
        CHECK(w.Add({ 0, 0, 0, 1 }, flags, nullptr));
        CHECK_FALSE(w.Add({ 0, 0, 0, 2 }, std::vector<char>(13, 1), nullptr));   // flag count changed
    }
    {
        std::vector<std::vector<int>> rows14(14, std::vector<int>{ 1, 2, 3, 4 });
        std::vector<std::vector<int>> rows13(13, std::vector<int>{ 1, 2, 3, 4 });
        keeptable::Writer w(p);
        CHECK(w.Add({ 0, 0, 0, 1 }, flags, &rows14));
        CHECK(w.Add({ 0, 0, 0, 2 }, flags, nullptr));             // absent bottom table is allowed
        CHECK_FALSE(w.Add({ 0, 0, 0, 3 }, flags, &rows13));       // but a different row count is not
    }
    {
        std::vector<std::vector<int>> rows33(33, std::vector<int>{ 1, 2, 3, 4 });
        keeptable::Writer w(p);
        CHECK_FALSE(w.Add({ 0, 0, 0, 1 }, flags, &rows33));       // more rows than the mask can hold
    }
    {
        keeptable::Writer w(p);
        keeptable::Header h;
        CHECK_FALSE(w.Finish(h));                                  // no entries
    }
    Cleanup(p);
}

TEST_CASE("keep table: source fingerprint and length exactness gate Open")
{
    const ExhaustiveKeepPolicy mem = MakePolicy(300);
    const std::filesystem::path p = TmpDir() / "gate.keeptable";
    Cleanup(p);
    REQUIRE(WriteTable(p, mem, 200, 100, nullptr, "the/source.gz"));
    CHECK(keeptable::Table::Open(p, 200, 100));
    CHECK_FALSE(keeptable::Table::Open(p, 200, 101));   // size moved
    CHECK_FALSE(keeptable::Table::Open(p, 201, 100));   // content moved
    CHECK_FALSE(keeptable::Table::Open(TmpDir() / "missing.keeptable", 200, 100));
    // The table's OWN mtime is the cache's last-use stamp, not part of the fingerprint: refreshing it
    // (MarkUsed) or ageing it must not invalidate the table.
    keeptable::MarkUsed(p);
    CHECK(keeptable::Table::Open(p, 200, 100));
    std::filesystem::last_write_time(p, std::filesystem::file_time_type::clock::now() - std::chrono::hours(1000));
    CHECK(keeptable::Table::Open(p, 200, 100));

    // A byte short (a torn write / a concurrent rebuild shrinking the file) and a byte long (a tail
    // from a non-atomic rename) must both be rejected: the trailer and the record region no longer
    // account for the file exactly. (Every Open above returned a temporary, so no handle is open.)
    const auto sz = std::filesystem::file_size(p);
    std::filesystem::resize_file(p, sz - 1);
    CHECK_FALSE(keeptable::Table::Open(p, 200, 100));
    std::filesystem::resize_file(p, sz + 1);
    CHECK_FALSE(keeptable::Table::Open(p, 200, 100));
    Cleanup(p);
}

TEST_CASE("keep table: the loader builds the table from a JSON sidecar and it matches the parsed maps")
{
    // The production route: SaveDeckProfile emits the sidecar (entries in std::map order, which is
    // what lets the build stream), CachedExhaustiveKeep builds the table under MTG_KEEP_TABLE_DIR and
    // returns a DISK-backed policy; LoadDeckProfile is the in-memory reference for the same file.
    const std::filesystem::path dir = TmpDir() / "loader";
    Cleanup(dir);
    std::filesystem::create_directories(dir);
    REQUIRE(EnvPut("MTG_KEEP_TABLE_DIR", (dir / "cache").string().c_str(), true));

    MulliganProfile prof;
    prof.exhaustive_keep = std::make_shared<const ExhaustiveKeepPolicy>(MakePolicy(3000));
    const std::filesystem::path sidecar = dir / "deck.keepmodel.exhaustive.profile.json";
    REQUIRE(SaveDeckProfile(sidecar, prof));

    const MulliganProfile ref = LoadDeckProfile(sidecar);
    REQUIRE(ref.HasExhaustiveKeep());
    REQUIRE_FALSE(ref.exhaustive_keep->disk);
    CHECK(ref.exhaustive_keep->keep.size() == 3000);

    auto cached = CachedExhaustiveKeep(sidecar);
    REQUIRE(cached);
    REQUIRE_MESSAGE(cached->disk, "loader did not attach an on-disk table");
    CHECK(cached->keep.empty());
    CHECK(cached->table_size() == 3000);
    CHECK_FALSE(cached->empty());
    CHECK(cached->buckets == ref.exhaustive_keep->buckets);
    CHECK(cached->max_mull == ref.exhaustive_keep->max_mull);
    CHECK(cached->bottoming_enabled == ref.exhaustive_keep->bottoming_enabled);
    CHECK(cached->commit == ref.exhaustive_keep->commit);
    CHECK(cached->play_digest == ref.exhaustive_keep->play_digest);
    CHECK(cached->effective_R == ref.exhaustive_keep->effective_R);
    for (const auto& kv : ref.exhaustive_keep->keep) { CheckAgree(*ref.exhaustive_keep, *cached, kv.first); }

    // Exactly one table file landed in the cache dir, and it validates against the sidecar's
    // own fingerprint (so the NEXT launch is an open, not a rebuild).
    int n_tables = 0;
    std::filesystem::path table;
    for (const auto& e : std::filesystem::directory_iterator(dir / "cache"))
    { if (e.path().extension() == ".keeptable") { ++n_tables; table = e.path(); } }
    CHECK(n_tables == 1);
    const uint64_t sz = std::filesystem::file_size(sidecar);
    uint64_t hash = 0;
    REQUIRE(keeptable::ContentHash(sidecar, hash));
    CHECK(table.filename().string() == keeptable::TableFileName(sidecar.string(), hash));
    CHECK(keeptable::Table::Open(table, hash, sz));
    // ...and its content ref names that hash for this path's (size, mtime).
    keeptable::Ref kref;
    REQUIRE(keeptable::ReadRef(dir / "cache" / keeptable::RefFileName(std::filesystem::weakly_canonical(sidecar).string()), kref));
    CHECK(kref.content_hash == hash);
    CHECK(kref.src_size == sz);

    // A byte-identical copy at ANOTHER path (another worktree's checkout of the same keep model)
    // shares the table: no second build, one more ref.
    const std::filesystem::path copy_dir = dir / "other_worktree";
    std::filesystem::create_directories(copy_dir);
    const std::filesystem::path copy = copy_dir / sidecar.filename();
    std::filesystem::copy_file(sidecar, copy);
    auto shared = CachedExhaustiveKeep(copy);
    REQUIRE(shared);
    REQUIRE(shared->disk);
    CHECK(shared->table_size() == 3000);
    int n_tables2 = 0, n_refs = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir / "cache"))
    {
        if (e.path().extension() == ".keeptable") { ++n_tables2; }
        if (e.path().extension() == ".keepref")   { ++n_refs; }
    }
    CHECK(n_tables2 == 1);
    CHECK(n_refs == 2);
    // `cached` lives on in CachedExhaustiveKeep's process-global cache with the table open, so on
    // Windows the table file itself cannot be removed here; everything else is, best-effort.
    EnvPut("MTG_KEEP_TABLE_DIR", "", true);
    Cleanup(dir);
}

TEST_CASE("keep table: a keep_table_alias resolves a renamed card to its old bucket, and only when the table lacks it")
{
    // Snow 2026-10-10: the table buckets the MDFC as Kaldring, the list names it Jorn. Without the alias a
    // Jorn hand is unbucketed (Composition fails -> clairvoyant trial-game bottoming); with it the hand
    // resolves to Kaldring's bucket.
    auto base = std::make_shared<ExhaustiveKeepPolicy>();
    base->buckets = { {"Snow-Covered Island"}, {"Kaldring, the Rimestaff"}, {"Skred"} };
    base->Index();
    std::shared_ptr<const ExhaustiveKeepPolicy> ek = base;
    const std::vector<std::string> hand{ "Snow-Covered Island", "Jorn, God of Winter", "Skred" };
    std::vector<int> comp;
    CHECK_FALSE(ek->Composition(hand, comp));
    CHECK(ek->FirstUnbucketed(hand) == "Jorn, God of Winter");

    using A = MulliganProfile::KeepTableAlias;
    auto aliased = WithKeepTableAlias(ek, { A{ "Jorn, God of Winter", "Kaldring, the Rimestaff", "test" } });
    REQUIRE(aliased != ek);                                   // a copy; the shared table is untouched
    CHECK_FALSE(ek->Composition(hand, comp));
    REQUIRE(aliased->Composition(hand, comp));
    CHECK(comp == std::vector<int>{ 1, 1, 1 });
    CHECK(aliased->FirstUnbucketed(hand).empty());
    // Memoized: the same table + alias set returns the same copy.
    CHECK(WithKeepTableAlias(ek, { A{ "Jorn, God of Winter", "Kaldring, the Rimestaff", "test" } }) == aliased);

    // Inert when the table already knows the name (a regenerated table), and when `as` is unknown.
    CHECK(WithKeepTableAlias(ek, { A{ "Skred", "Snow-Covered Island", "test" } }) == ek);
    CHECK(WithKeepTableAlias(ek, { A{ "Jorn, God of Winter", "Not A Card", "test" } }) == ek);
    CHECK(WithKeepTableAlias(ek, {}) == ek);
}

TEST_CASE("keep table: a keep_table_alias parses from the profile JSON, and an entry without a why is dropped")
{
    const auto dir = std::filesystem::temp_directory_path() / "mtg_keep_alias_test";
    std::filesystem::create_directories(dir);
    const auto p = dir / "X.profile.json";
    {
        std::ofstream f(p);
        f << R"({"mulligan":{"min_lands":2,"keep_table_alias":[{"name":"Jorn, God of Winter","as":"Kaldring, the Rimestaff","why":"because"}]}})";
    }
    MulliganProfile prof = LoadDeckProfile(p);
    REQUIRE(prof.keep_table_alias.size() == 1);
    CHECK(prof.keep_table_alias[0].name == "Jorn, God of Winter");
    CHECK(prof.keep_table_alias[0].as == "Kaldring, the Rimestaff");
    CHECK(prof.keep_table_alias[0].why == "because");
    {
        std::ofstream f(p);
        f << R"({"mulligan":{"min_lands":2,"keep_table_alias":[{"name":"Jorn, God of Winter","as":"Kaldring, the Rimestaff"}]}})";
    }
    // Missing why: the ENTRY is dropped (loudly), the rest of the profile survives.
    MulliganProfile bad = LoadDeckProfile(p);
    CHECK(bad.keep_table_alias.empty());
    CHECK(bad.min_lands == 2);
    std::filesystem::remove_all(dir);
}

namespace
{
// A real (small) v2 table named as the loader would name it, with its mtime set `idle` into the past.
std::filesystem::path CacheTable(const std::filesystem::path& cache, const ExhaustiveKeepPolicy& mem,
                                 const std::string& src, uint64_t hash, std::chrono::hours idle)
{
    const std::filesystem::path p = cache / keeptable::TableFileName(src, hash);
    REQUIRE(WriteTable(p, mem, hash, 1, nullptr, src));
    std::filesystem::last_write_time(p, std::filesystem::file_time_type::clock::now() - idle);
    return p;
}
// A ref for an on-disk sidecar `src` claiming content `hash`.
void CacheRef(const std::filesystem::path& cache, const std::filesystem::path& src, uint64_t hash)
{
    keeptable::Ref r;
    r.src_path = src.string();
    r.src_size = std::filesystem::file_size(src);
    r.src_mtime = keeptable::detail::MtimeKey(std::filesystem::last_write_time(src));
    r.content_hash = hash;
    keeptable::WriteRef(cache / keeptable::RefFileName(r.src_path), r, 1);
}
bool Exists(const std::filesystem::path& p) { return std::filesystem::exists(p); }
}  // namespace

TEST_CASE("keep table cache sweep: orphans, stale temps, and LRU eviction over the cap")
{
    namespace fs = std::filesystem;
    const fs::path dir = TmpDir() / "sweep";
    Cleanup(dir);
    const fs::path cache = dir / "cache";
    fs::create_directories(cache);
    const ExhaustiveKeepPolicy mem = MakePolicy(300);
    auto sidecar = [&](const std::string& name, const std::string& body)
    { const fs::path p = dir / name; std::ofstream(p, std::ios::binary) << body; return p; };
    const fs::path a = sidecar("a.json", "aaaa"), b = sidecar("b.json", "bbbbb"), c = sidecar("c.json", "cccccc");
    const fs::path gone = sidecar("gone.json", "gone");

    // Referenced tables: a (idle 5 h), b (idle 3 h), c (idle 1 h). One unreferenced table idle 2 h, one
    // unreferenced table just used (inside the grace), and one whose only ref points at a deleted sidecar.
    const fs::path ta = CacheTable(cache, mem, a.string(), 0xa, std::chrono::hours(5));
    const fs::path tb = CacheTable(cache, mem, b.string(), 0xb, std::chrono::hours(3));
    const fs::path tc = CacheTable(cache, mem, c.string(), 0xc, std::chrono::hours(1));
    const fs::path t_unref = CacheTable(cache, mem, "x.json", 0xd, std::chrono::hours(2));
    const fs::path t_fresh = CacheTable(cache, mem, "y.json", 0xe, std::chrono::hours(0));
    const fs::path t_gone = CacheTable(cache, mem, gone.string(), 0xf, std::chrono::hours(2));
    CacheRef(cache, a, 0xa); CacheRef(cache, b, 0xb); CacheRef(cache, c, 0xc); CacheRef(cache, gone, 0xf);
    fs::remove(gone);
    // A ref whose sidecar CHANGED since (size moved): stale, so its table (0xb is still named by b's ref)
    // is unaffected, but the ref itself goes.
    const fs::path changed = sidecar("changed.json", "12");
    CacheRef(cache, changed, 0xb);
    std::ofstream(changed, std::ios::binary | std::ios::app) << "345";
    // Build temps: a dead process's partial (2 days old) and a live build's (just written).
    const fs::path tmp_old = cache / "a.json-000000000000000a.keeptable.123.tmp";
    const fs::path tmp_new = cache / "a.json-000000000000000a.keeptable.456.tmp";
    std::ofstream(tmp_old) << "partial";
    std::ofstream(tmp_new) << "partial";
    fs::last_write_time(tmp_old, fs::file_time_type::clock::now() - std::chrono::hours(48));
    // Something the sweep does not own.
    std::ofstream(cache / "notes.txt") << "keep me";

    keeptable::CollectOptions o;
    o.space_fraction = 0;   // deterministic: the test sets the cap itself

    SUBCASE("unbounded: only orphans, stale refs and stale temps go")
    {
        o.cap_bytes = 0;
        const keeptable::CollectStats st = keeptable::Collect(cache, {}, o);
        CHECK(st.removed_orphan == 2);
        CHECK(st.removed_ref == 2);
        CHECK(st.removed_tmp == 1);
        CHECK(st.removed_lru == 0);
        CHECK_FALSE(Exists(t_unref));
        CHECK_FALSE(Exists(t_gone));
        CHECK_FALSE(Exists(tmp_old));
        CHECK(Exists(tmp_new));
        CHECK(Exists(t_fresh));   // unreferenced but used within the grace: a ref may be about to land
        CHECK(Exists(ta)); CHECK(Exists(tb)); CHECK(Exists(tc));
        CHECK_FALSE(Exists(cache / keeptable::RefFileName(gone.string())));
        CHECK_FALSE(Exists(cache / keeptable::RefFileName(changed.string())));
        CHECK(Exists(cache / keeptable::RefFileName(a.string())));
        CHECK(Exists(cache / "notes.txt"));
    }
    const uint64_t sa = fs::file_size(ta), sb = fs::file_size(tb), sc = fs::file_size(tc);
    const uint64_t sf = fs::file_size(t_fresh), st_new = fs::file_size(tmp_new);
    SUBCASE("a cap evicts least-recently-used first, never a protected or recently used table")
    {
        // Survivors of the orphan pass: ta (idle 5 h), tb (3 h), tc (1 h), t_fresh (0 h) + the young
        // temp, which counts toward the total but is never evicted. The cap leaves room for ta alone:
        // ta is PROTECTED (this process holds it), so tb and then tc go, and t_fresh is inside
        // lru_min_idle, so it stays and the cache ends over the cap -- the cap is soft by design.
        o.cap_bytes = sa + st_new;
        const keeptable::CollectStats st = keeptable::Collect(cache, { ta.filename().string() }, o);
        CHECK(st.removed_lru == 2);
        CHECK(Exists(ta));
        CHECK_FALSE(Exists(tb));
        CHECK_FALSE(Exists(tc));
        CHECK(Exists(t_fresh));
        CHECK(st.total_after == sa + sf + st_new);
    }
    SUBCASE("eviction stops as soon as the cache fits")
    {
        o.cap_bytes = sb + sc + sf + st_new;
        const keeptable::CollectStats st = keeptable::Collect(cache, {}, o);
        CHECK(st.removed_lru == 1);
        CHECK_FALSE(Exists(ta));   // the least recently used
        CHECK(Exists(tb)); CHECK(Exists(tc)); CHECK(Exists(t_fresh));
        CHECK(st.total_after == o.cap_bytes);
    }
    SUBCASE("a table used just before the sweep is the most recent, not the least")
    {
        keeptable::MarkUsed(ta);   // the loader stamps every table it opens
        o.cap_bytes = sa + sc + sf + st_new;
        const keeptable::CollectStats st = keeptable::Collect(cache, {}, o);
        CHECK(st.removed_lru == 1);
        CHECK(Exists(ta));
        CHECK_FALSE(Exists(tb));   // now the least recently used
    }
    Cleanup(dir);
}
