// Test executable only. No failure injection is linked into an ordinary node.
#ifndef MERCATURA_SECURITY_AUDIT_ALLOCATION_H
#define MERCATURA_SECURITY_AUDIT_ALLOCATION_H
#include <cstddef>
namespace mercaweave::audit {
void FailAfter(size_t successful_allocations) noexcept;
void Disarm() noexcept;
bool Fired() noexcept;
}
#endif
