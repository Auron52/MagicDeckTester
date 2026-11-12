#include "BatchPartial.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace batchpartial
{
namespace
{
constexpr std::string_view kHeaderTag = "#mtg-batch-partial v1 ";

uint32_t Crc32Of(std::string_view s)
{
    const uint64_t h = Fnv1a(s);
    return static_cast<uint32_t>(h ^ (h >> 32));
}

template <typename T>
bool ParseInt(std::string_view s, T& out, int base = 10)
{
    if (s.empty()) { return false; }
    const char* b = s.data();
    const char* e = s.data() + s.size();
    auto [p, ec] = std::from_chars(b, e, out, base);
    return ec == std::errc() && p == e;
}

// Split on single spaces; empty tokens (double spaces) make the line malformed.
bool Split(std::string_view s, std::vector<std::string_view>& out)
{
    out.clear();
    std::size_t i = 0;
    while (i <= s.size())
    {
        const std::size_t j = s.find(' ', i);
        const std::size_t end = (j == std::string_view::npos) ? s.size() : j;
        if (end == i) { return false; }
        out.push_back(s.substr(i, end - i));
        if (j == std::string_view::npos) { break; }
        i = j + 1;
    }
    return true;
}
}  // namespace

uint64_t Fnv1a(const void* data, std::size_t n, uint64_t h)
{
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

std::string Hex16(uint64_t v)
{
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string FormatHeader(uint64_t id, const std::string& job_name)
{
    return std::string(kHeaderTag) + Hex16(id) + " " + job_name + "\n";
}

std::string FormatRecord(const Record& r)
{
    std::string body = "g " + std::to_string(r.gi) + " " + std::to_string(r.seed) + " "
                     + (r.abandoned ? std::string("A") : std::to_string(r.win_turn)) + " "
                     + Hex16(r.digest) + " " + std::to_string(r.units) + " " + std::to_string(r.ms)
                     + " " + std::to_string(r.limit) + " " + (r.rel.empty() ? std::string("-") : r.rel);
    char crc[16];
    std::snprintf(crc, sizeof(crc), " *%08x\n", Crc32Of(body));
    return body + crc;
}

std::optional<Record> ParseRecord(std::string_view line)
{
    if (!line.empty() && line.back() == '\r') { line.remove_suffix(1); }
    const std::size_t star = line.rfind(" *");
    if (star == std::string_view::npos || line.size() - star != 2 + 8) { return std::nullopt; }
    uint32_t want = 0;
    if (!ParseInt(line.substr(star + 2), want, 16)) { return std::nullopt; }
    const std::string_view body = line.substr(0, star);
    if (Crc32Of(body) != want) { return std::nullopt; }
    std::vector<std::string_view> t;
    if (!Split(body, t) || t.size() != 9 || t[0] != "g") { return std::nullopt; }
    Record r;
    if (!ParseInt(t[1], r.gi) || !ParseInt(t[2], r.seed)) { return std::nullopt; }
    if (t[3] == "A") { r.abandoned = true; }
    else if (!ParseInt(t[3], r.win_turn)) { return std::nullopt; }
    if (t[4].size() != 16 || !ParseInt(t[4], r.digest, 16)) { return std::nullopt; }
    if (!ParseInt(t[5], r.units) || !ParseInt(t[6], r.ms) || !ParseInt(t[7], r.limit)) { return std::nullopt; }
    r.rel = std::string(t[8]);
    return r;
}

std::optional<FileContents> ParseFile(std::string_view bytes)
{
    const std::size_t nl = bytes.find('\n');
    if (nl == std::string_view::npos) { return std::nullopt; }
    std::string_view head = bytes.substr(0, nl);
    if (!head.empty() && head.back() == '\r') { head.remove_suffix(1); }
    if (head.substr(0, kHeaderTag.size()) != kHeaderTag) { return std::nullopt; }
    head.remove_prefix(kHeaderTag.size());
    if (head.size() < 18 || head[16] != ' ') { return std::nullopt; }
    FileContents fc;
    if (!ParseInt(head.substr(0, 16), fc.id, 16)) { return std::nullopt; }
    fc.job_name = std::string(head.substr(17));
    std::size_t i = nl + 1;
    while (i < bytes.size())
    {
        const std::size_t e = bytes.find('\n', i);
        if (e == std::string_view::npos) { ++fc.dropped; break; }   // unterminated tail: torn
        const std::string_view line = bytes.substr(i, e - i);
        if (std::optional<Record> r = ParseRecord(line)) { fc.records.push_back(std::move(*r)); }
        else if (!line.empty()) { ++fc.dropped; }
        i = e + 1;
    }
    return fc;
}

std::string RelSignature(double k, int calib, long long floor_units)
{
    // k as an integer count of millionths: a decimal rendering of a double is not a stable key.
    const long long km = std::llround(k * 1e6);
    return "R" + std::to_string(km) + "," + std::to_string(calib) + "," + std::to_string(floor_units);
}

uint64_t FoldCaseDigest(const std::vector<uint64_t>& per_game_in_gi_order)
{
    uint64_t cd = kFnvBasis;
    for (uint64_t d : per_game_in_gi_order)
    {
        for (int b = 0; b < 8; ++b) { cd ^= static_cast<uint8_t>((d >> (b * 8)) & 0xff); cd *= 1099511628211ULL; }
    }
    return cd;
}

namespace
{
bool SameResult(const Record& a, const Record& b)
{
    return a.abandoned == b.abandoned && (a.abandoned || a.win_turn == b.win_turn) && a.digest == b.digest
        && a.units == b.units && a.limit == b.limit && a.rel == b.rel;
}
}  // namespace

void Index::Add(uint64_t id, const Record& r, int /*file*/)
{
    std::vector<Entry>& v = map_[std::make_tuple(id, r.gi, r.seed)];
    for (Entry& e : v)
    {
        if (e.rec.limit != r.limit || e.rec.rel != r.rel) { continue; }
        if (!e.conflict && !SameResult(e.rec, r)) { e.conflict = true; ++conflicts_; }
        return;
    }
    v.push_back(Entry{r, false});
}

}  // namespace batchpartial
