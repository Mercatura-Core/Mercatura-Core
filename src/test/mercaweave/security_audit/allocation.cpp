// Executable-local, one-shot, thread-local allocation faults. Disarm before
// throwing so unwinding, SQLite rollback and the test framework can allocate.
#include <test/mercaweave/security_audit/allocation.h>
#include <cstdlib>
#include <new>
namespace {
thread_local bool armed{false}, fired{false};
thread_local size_t remaining{0};
void Check()
{
    if (armed && remaining-- == 0) {
        armed = false; fired = true; throw std::bad_alloc{};
    }
}
void* Allocate(size_t size)
{
    Check();
    if (void* ptr = std::malloc(size ? size : 1)) return ptr;
    throw std::bad_alloc{};
}
void* Aligned(size_t size, size_t alignment)
{
    Check(); void* ptr{nullptr};
    if (posix_memalign(&ptr,alignment,size ? size : 1) == 0) return ptr;
    throw std::bad_alloc{};
}
}
namespace mercaweave::audit {
void FailAfter(size_t count) noexcept { remaining=count; fired=false; armed=true; }
void Disarm() noexcept { armed=false; }
bool Fired() noexcept { return fired; }
}
void* operator new(size_t n) { return Allocate(n); }
void* operator new[](size_t n) { return Allocate(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
void* operator new(size_t n, std::align_val_t a) { return Aligned(n,size_t(a)); }
void* operator new[](size_t n, std::align_val_t a) { return Aligned(n,size_t(a)); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { std::free(p); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { try { return Allocate(n); } catch (...) { return nullptr; } }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { try { return Allocate(n); } catch (...) { return nullptr; } }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void* operator new(size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { try { return Aligned(n,size_t(a)); } catch (...) { return nullptr; } }
void* operator new[](size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { try { return Aligned(n,size_t(a)); } catch (...) { return nullptr; } }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }
