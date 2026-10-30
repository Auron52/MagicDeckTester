#pragma once
// DURABLE APPEND FILES -- a minimal portable fd layer for crash-safe logs (USER 2026-10-29: after a
// pause or kill "the only wasted work that is allowed is games in progress").
//
// Raw descriptors, not streams: a record must reach the kernel with ONE write() before the caller moves
// on (a stream's buffer dies with the process, while the page cache outlives it), and a sync thread
// must be able to fsync the same file. Used by the keep generator's rollout side journal
// (analyzer/RolloutSideJournal.h) and the batch runner's per-game resume log (runner/BatchRunner.cpp).
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <string>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

// Raw descriptors, not a stream: a record must reach the kernel with ONE write() before Append returns
// (a stream's buffer is lost with the process), and the sync thread must fsync the same file.
namespace durable
{
#if defined(_WIN32)
inline int  OpenAppend(const std::string& p) { return ::_open(p.c_str(), _O_WRONLY | _O_APPEND | _O_CREAT | _O_BINARY, _S_IREAD | _S_IWRITE); }
inline int  OpenTrunc(const std::string& p)  { return ::_open(p.c_str(), _O_WRONLY | _O_TRUNC | _O_CREAT | _O_BINARY, _S_IREAD | _S_IWRITE); }
inline long long WriteSome(int fd, const char* b, std::size_t n) { return ::_write(fd, b, static_cast<unsigned>(std::min<std::size_t>(n, 1u << 30))); }
inline bool Sync(int fd)  { return ::_commit(fd) == 0; }
inline int  Dup(int fd)   { return ::_dup(fd); }
inline void Close(int fd) { if (fd >= 0) { ::_close(fd); } }
inline void SyncDir(const std::string&) {}   // NTFS: a rename is durable with the file's own commit
#else
inline int  OpenAppend(const std::string& p) { return ::open(p.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644); }
inline int  OpenTrunc(const std::string& p)  { return ::open(p.c_str(), O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC, 0644); }
inline long long WriteSome(int fd, const char* b, std::size_t n) { return static_cast<long long>(::write(fd, b, n)); }
inline bool Sync(int fd)  { return ::fsync(fd) == 0; }
inline int  Dup(int fd)   { return ::dup(fd); }
inline void Close(int fd) { if (fd >= 0) { ::close(fd); } }
// A rename is durable only once its DIRECTORY is synced. Best effort (some filesystems refuse it).
inline void SyncDir(const std::string& file)
{
    std::string d = std::filesystem::path(file).parent_path().string();
    if (d.empty()) { d = "."; }
    const int fd = ::open(d.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd >= 0) { ::fsync(fd); ::close(fd); }
}
#endif
// Write all of [b, b+n), retrying short writes and EINTR. False on a real error (the caller holds the
// record and retries later; a partial record left in the file is ended by the recovery newline).
inline bool WriteAll(int fd, const char* b, std::size_t n)
{
    while (n > 0)
    {
        const long long w = WriteSome(fd, b, n);
        if (w < 0) { if (errno == EINTR) { continue; } return false; }
        if (w == 0) { return false; }
        b += w; n -= static_cast<std::size_t>(w);
    }
    return true;
}
// fsync a file by PATH (the main journal is written through a stream; any descriptor on the inode syncs
// its data). Opened for append-write because Windows' _commit needs write access; nothing is written.
inline bool SyncPath(const std::string& p)
{
#if defined(_WIN32)
    const int fd = ::_open(p.c_str(), _O_WRONLY | _O_APPEND | _O_BINARY);   // never creates
#else
    const int fd = ::open(p.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);      // never creates
#endif
    if (fd < 0) { return false; }
    const bool ok = Sync(fd);
    Close(fd);
    return ok;
}
}  // namespace durable
