// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_TEST_UTIL_PQ_LIFECYCLE_H
#define BITCOIN_TEST_UTIL_PQ_LIFECYCLE_H

#include <atomic>
#include <array>
#include <functional>
#include <string_view>
#include <vector>
#include <sys/stat.h>

namespace pq {
struct Work;
namespace test {
enum class Event { BEFORE_CALCULATE, AFTER_CALCULATE, BEFORE_PUBLISH };
struct Hooks {
    std::function<void(Event, Work*)> point;
    std::function<void(const void*, size_t)> cleansed;
    std::function<void()> joining;
};
// Only the excluded B1 executables define these functions. Ordinary nodes have
// no hook implementation, runtime setting, or environment-variable activation.
void LifecyclePoint(Event event, Work* job);
class ScopedHooks {
    Hooks* m_hooks;
public:
    explicit ScopedHooks(Hooks& hooks);
    ~ScopedHooks();
    ScopedHooks(const ScopedHooks&) = delete;
    ScopedHooks& operator=(const ScopedHooks&) = delete;
};
enum class Op { OPEN_DIRECTORY, OPEN_FILE, STAT_PATH, STAT_DIRECTORY, STAT_FILE,
    READ, WRITE, SYNC_FILE, PUBLISH, UNLINK, SYNC_DIRECTORY, COUNT };
enum class Action { ERROR, INTERRUPT, SHORT, WRONG_OWNER, WRONG_MODE, CHANGED_INODE };
struct Rule { Op op; unsigned call; Action action; int error; };
struct FileFaults {
    std::vector<Rule> rules;
    std::array<unsigned, static_cast<size_t>(Op::COUNT)> calls{};
    struct Descriptor { int fd{-1}; bool directory{false}; };
    std::array<Descriptor, 8> descriptors{};
    const void* read_buffer{nullptr};
    const void* write_buffer{nullptr};
    unsigned cleansed_buffers{0};
    bool buffers_zeroed{true};
    const Rule* Next(Op op);
    void Opened(int fd, bool directory);
    void Closed(int fd);
    bool Directory(int fd) const;
    unsigned OpenCount() const;
};
class ScopedFiles {
    FileFaults* m_previous;
public:
    explicit ScopedFiles(FileFaults& faults);
    ~ScopedFiles();
};
struct WorkerFaults { int fail_create{-1}; unsigned creates{0}, joins{0}; };
class ScopedWorkers {
    WorkerFaults* m_previous;
public:
    explicit ScopedWorkers(WorkerFaults& faults);
    ~ScopedWorkers();
};
struct Access;
} // namespace test
} // namespace pq
#endif // BITCOIN_TEST_UTIL_PQ_LIFECYCLE_H
