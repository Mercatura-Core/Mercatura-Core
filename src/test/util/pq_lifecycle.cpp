// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// Linker interposition is private to excluded Linux B1 test executables.
#include <test/util/pq_lifecycle.h>
#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <fcntl.h>
#include <pthread.h>
#include <stdexcept>
#include <unistd.h>

namespace {
std::atomic<pq::test::Hooks*> g_hooks{nullptr};
thread_local pq::test::FileFaults* g_files{nullptr};
thread_local pq::test::WorkerFaults* g_workers{nullptr};
const pq::test::Rule* Fault(pq::test::Op op) { return g_files ? g_files->Next(op) : nullptr; }
bool Error(const pq::test::Rule* rule)
{
    if (!rule) return false;
    if (rule->action == pq::test::Action::ERROR || rule->action == pq::test::Action::INTERRUPT) {
        errno = rule->action == pq::test::Action::INTERRUPT ? EINTR : rule->error;
        return true;
    }
    return false;
}
void Metadata(struct stat* st, const pq::test::Rule* rule)
{
    if (!rule) return;
    if (rule->action == pq::test::Action::WRONG_OWNER) ++st->st_uid;
    if (rule->action == pq::test::Action::WRONG_MODE) st->st_mode |= S_IRGRP;
    if (rule->action == pq::test::Action::CHANGED_INODE) ++st->st_ino;
}
}
namespace pq::test {
ScopedHooks::ScopedHooks(Hooks& hooks) : m_hooks(&hooks)
{
    Hooks* expected{nullptr};
    if (!g_hooks.compare_exchange_strong(expected, m_hooks)) throw std::logic_error("Nested PQ lifecycle hooks");
}
ScopedHooks::~ScopedHooks() { g_hooks.store(nullptr); }
void LifecyclePoint(Event event, Work* job)
{
    if (auto* hooks = g_hooks.load(); hooks && hooks->point) hooks->point(event, job);
}
const Rule* FileFaults::Next(Op op)
{
    const auto call = ++calls[static_cast<size_t>(op)];
    for (const auto& rule : rules) if (rule.op == op && rule.call == call) return &rule;
    return nullptr;
}
void FileFaults::Opened(int fd, bool directory)
{
    if (fd < 0) return;
    for (auto& entry : descriptors) if (entry.fd < 0) { entry = {fd, directory}; return; }
    throw std::logic_error("PQ test descriptor table full");
}
void FileFaults::Closed(int fd) { for (auto& entry : descriptors) if (entry.fd == fd) entry.fd = -1; }
bool FileFaults::Directory(int fd) const
{
    for (const auto& entry : descriptors) if (entry.fd == fd) return entry.directory;
    return false;
}
unsigned FileFaults::OpenCount() const
{
    return std::count_if(descriptors.begin(), descriptors.end(), [](const auto& entry) { return entry.fd >= 0; });
}
ScopedFiles::ScopedFiles(FileFaults& faults) : m_previous(g_files) { g_files = &faults; }
ScopedFiles::~ScopedFiles() { g_files = m_previous; }
ScopedWorkers::ScopedWorkers(WorkerFaults& faults) : m_previous(g_workers) { g_workers = &faults; }
ScopedWorkers::~ScopedWorkers() { g_workers = m_previous; }
}

extern "C" {
int __real_open(const char*, int, ...);
int __real_openat(int, const char*, int, ...);
int __real_lstat(const char*, struct stat*);
int __real_fstat(int, struct stat*);
ssize_t __real_read(int, void*, size_t);
ssize_t __real___read_chk(int, void*, size_t, size_t);
ssize_t __real_write(int, const void*, size_t);
int __real_fsync(int);
int __real_linkat(int, const char*, int, const char*, int);
int __real_unlinkat(int, const char*, int);
int __real_close(int);
int __real_pthread_create(pthread_t*, const pthread_attr_t*, void*(*)(void*), void*);
int __real_pthread_join(pthread_t, void**);
void __real__Z14memory_cleansePvm(void*, size_t);

int __wrap_open(const char* path, int flags, ...)
{
    mode_t mode{0};
    if (flags & O_CREAT) { va_list args; va_start(args, flags); mode = va_arg(args, int); va_end(args); }
    if (Error(Fault(pq::test::Op::OPEN_DIRECTORY))) return -1;
    const int fd = __real_open(path, flags, mode);
    if (g_files) g_files->Opened(fd, flags & O_DIRECTORY);
    return fd;
}
int __wrap_openat(int directory, const char* path, int flags, ...)
{
    mode_t mode{0};
    if (flags & O_CREAT) { va_list args; va_start(args, flags); mode = va_arg(args, int); va_end(args); }
    if (Error(Fault(pq::test::Op::OPEN_FILE))) return -1;
    const int fd = __real_openat(directory, path, flags, mode);
    if (g_files) g_files->Opened(fd, false);
    return fd;
}
int __wrap_lstat(const char* path, struct stat* st)
{
    const auto* rule = Fault(pq::test::Op::STAT_PATH);
    if (Error(rule)) return -1;
    const int result = __real_lstat(path, st);
    if (!result) Metadata(st, rule);
    return result;
}
int __wrap_fstat(int fd, struct stat* st)
{
    const auto* rule = Fault(g_files && g_files->Directory(fd) ? pq::test::Op::STAT_DIRECTORY : pq::test::Op::STAT_FILE);
    if (Error(rule)) return -1;
    const int result = __real_fstat(fd, st);
    if (!result) Metadata(st, rule);
    return result;
}
ssize_t __wrap_read(int fd, void* data, size_t count)
{
    if (g_files && !g_files->read_buffer) g_files->read_buffer = data;
    const auto* rule = Fault(pq::test::Op::READ);
    if (Error(rule)) return -1;
    if (rule && rule->action == pq::test::Action::SHORT) count = std::min(count, size_t(rule->error ? rule->error : 17));
    return __real_read(fd, data, count);
}
ssize_t __wrap___read_chk(int fd, void* data, size_t count, size_t capacity)
{
    if (count > capacity) return __real___read_chk(fd, data, count, capacity);
    return __wrap_read(fd, data, count);
}
ssize_t __wrap_write(int fd, const void* data, size_t count)
{
    if (g_files && !g_files->write_buffer) g_files->write_buffer = data;
    const auto* rule = Fault(pq::test::Op::WRITE);
    if (Error(rule)) return -1;
    if (rule && rule->action == pq::test::Action::SHORT) count = std::min(count, size_t(rule->error ? rule->error : 17));
    return __real_write(fd, data, count);
}
int __wrap_fsync(int fd)
{
    if (Error(Fault(g_files && g_files->Directory(fd) ? pq::test::Op::SYNC_DIRECTORY : pq::test::Op::SYNC_FILE))) return -1;
    return __real_fsync(fd);
}
int __wrap_linkat(int from, const char* old_name, int to, const char* new_name, int flags)
{
    if (Error(Fault(pq::test::Op::PUBLISH))) return -1;
    return __real_linkat(from, old_name, to, new_name, flags);
}
int __wrap_unlinkat(int directory, const char* name, int flags)
{
    if (Error(Fault(pq::test::Op::UNLINK))) return -1;
    return __real_unlinkat(directory, name, flags);
}
int __wrap_close(int fd)
{
    const int result = __real_close(fd);
    if (g_files) g_files->Closed(fd);
    return result;
}
int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void*(*start)(void*), void* arg)
{
    if (g_workers && int(g_workers->creates++) == g_workers->fail_create) return EAGAIN;
    return __real_pthread_create(thread, attr, start, arg);
}
int __wrap_pthread_join(pthread_t thread, void** result)
{
    if (g_workers) ++g_workers->joins;
    if (auto* hooks = g_hooks.load(); hooks && hooks->joining) hooks->joining();
    return __real_pthread_join(thread, result);
}
void __wrap__Z14memory_cleansePvm(void* data, size_t size)
{
    __real__Z14memory_cleansePvm(data, size);
    if (g_files && size == 2127 && (data == g_files->read_buffer || data == g_files->write_buffer)) {
        ++g_files->cleansed_buffers;
        const auto* bytes = static_cast<const unsigned char*>(data);
        g_files->buffers_zeroed &= std::all_of(bytes, bytes+size, [](auto b) { return b == 0; });
    }
    if (auto* hooks = g_hooks.load(); hooks && hooks->cleansed) hooks->cleansed(data, size);
}
}
