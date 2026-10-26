// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// Dedicated test executable: allocation interposition is never linked into a
// node or ordinary unit/sanitizer runner. Requested C++ heap bytes are measured;
// libc/pthread/allocator arenas and native crypto stack storage are separate.
#include <net_pq.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace allocation_probe {
struct Counters {
    std::atomic<size_t> live{0},peak{0},count{0};
};
constinit std::array<Counters,8> counters;
constinit std::atomic<Counters*> active{nullptr};
thread_local int fail_after{-1};
struct alignas(std::max_align_t) Header { void* original; size_t size; Counters* owner; };
void* Allocate(size_t size,size_t alignment)
{
    if (fail_after==0) { fail_after=-1; throw std::bad_alloc(); }
    if (fail_after>0) --fail_after;
    alignment=std::max(alignment,alignof(Header));
    if (size>std::numeric_limits<size_t>::max()-sizeof(Header)-alignment) throw std::bad_alloc();
    void* raw=std::malloc(std::max(size,size_t{1})+sizeof(Header)+alignment);
    if (!raw) throw std::bad_alloc();
    auto address=(reinterpret_cast<uintptr_t>(raw)+sizeof(Header)+alignment-1)&~(uintptr_t(alignment)-1);
    auto* header=reinterpret_cast<Header*>(address)-1;
    auto* owner=active.load(std::memory_order_acquire);
    *header={raw,size,owner};
    if (owner) {
        auto live=owner->live.fetch_add(size)+size;
        auto peak=owner->peak.load();
        while (live>peak && !owner->peak.compare_exchange_weak(peak,live)) {}
        ++owner->count;
    }
    return reinterpret_cast<void*>(address);
}
void Free(void* ptr) noexcept
{
    if (!ptr) return;
    auto* header=reinterpret_cast<Header*>(ptr)-1;
    if (header->owner) header->owner->live.fetch_sub(header->size);
    std::free(header->original);
}
struct Window {
    explicit Window(size_t index) { active.store(&counters[index],std::memory_order_release); }
    ~Window() { active.store(nullptr,std::memory_order_release); }
};
}
void* operator new(size_t n) { return allocation_probe::Allocate(n,alignof(std::max_align_t)); }
void* operator new[](size_t n) { return ::operator new(n); }
void* operator new(size_t n,std::align_val_t a) { return allocation_probe::Allocate(n,size_t(a)); }
void* operator new[](size_t n,std::align_val_t a) { return ::operator new(n,a); }
void* operator new(size_t n,const std::nothrow_t&) noexcept { try { return ::operator new(n); } catch (...) { return nullptr; } }
void* operator new[](size_t n,const std::nothrow_t& t) noexcept { return ::operator new(n,t); }
void* operator new(size_t n,std::align_val_t a,const std::nothrow_t&) noexcept { try { return ::operator new(n,a); } catch (...) { return nullptr; } }
void* operator new[](size_t n,std::align_val_t a,const std::nothrow_t& t) noexcept { return ::operator new(n,a,t); }
void operator delete(void* p) noexcept { allocation_probe::Free(p); }
void operator delete[](void* p) noexcept { allocation_probe::Free(p); }
void operator delete(void* p,size_t) noexcept { allocation_probe::Free(p); }
void operator delete[](void* p,size_t) noexcept { allocation_probe::Free(p); }
void operator delete(void* p,std::align_val_t) noexcept { allocation_probe::Free(p); }
void operator delete[](void* p,std::align_val_t) noexcept { allocation_probe::Free(p); }
void operator delete(void* p,size_t,std::align_val_t) noexcept { allocation_probe::Free(p); }
void operator delete[](void* p,size_t,std::align_val_t) noexcept { allocation_probe::Free(p); }
void operator delete(void* p,const std::nothrow_t&) noexcept { allocation_probe::Free(p); }
void operator delete[](void* p,const std::nothrow_t&) noexcept { allocation_probe::Free(p); }
void operator delete(void* p,std::align_val_t,const std::nothrow_t&) noexcept { allocation_probe::Free(p); }
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&) noexcept { allocation_probe::Free(p); }

const std::function<void(const std::string&)> G_TEST_LOG_FUN{};
const std::function<std::vector<const char*>()> G_TEST_COMMAND_LINE_ARGUMENTS{};
const std::function<std::string()> G_TEST_GET_FULL_NAME{};

namespace {
using namespace pq;
using namespace std::chrono_literals;
void Require(bool value,const char* reason) { if (!value) throw std::runtime_error(reason); }
void Transfer(PQTransport& from,PQTransport& to)
{
    auto [wire,more,type]=from.GetBytesToSend(false);
    const auto size=wire.size();
    if (size) { Require(to.ReceivedBytes(wire) && wire.empty(),"wire transfer failed"); from.MarkBytesSent(size); }
}
UniValue Snapshot(size_t index)
{
    auto& value=allocation_probe::counters[index];
    UniValue result{UniValue::VOBJ};
    result.pushKV("peak_requested_cpp_heap_bytes",uint64_t(value.peak.load()));
    result.pushKV("retained_requested_cpp_heap_bytes",uint64_t(value.live.load()));
    result.pushKV("cpp_allocations",uint64_t(value.count.load()));
    return result;
}
int FaultInjection(int fail_after)
{
    auto context=std::make_shared<Context>(RegtestNetwork());
    const std::string source{"allocation-failure"};
    auto prime=context->Admit(source); Require(bool(prime),"prime admission failed"); prime.reset();
    bool caught{false};
    allocation_probe::fail_after=fail_after;
    try { context->Admit(source); } catch (const std::bad_alloc&) { caught=true; }
    allocation_probe::fail_after=-1;
    const auto pending=context->Pending();
    context->Stop();
    std::cout<<"{\"allocation_failure_caught\":"<<(caught ? "true" : "false")<<",\"pending_after_failed_allocation\":"<<pending<<"}\n";
    return pending==0 ? 0 : 1;
}
}
int main(int argc,char** argv)
{
    try {
        BasicTestingSetup setup{ChainType::REGTEST};
        if (argc==2 && std::string_view(argv[1]).starts_with("--allocation-failure=")) return FaultInjection(std::stoi(argv[1]+21));
        UniValue report{UniValue::VOBJ};
        report.pushKV("measurement","Dedicated global new/delete interposition, requested C++ heap only; excludes probe headers, malloc metadata, libc/pthread and native crypto stack allocations");
        {
            allocation_probe::Window window{0};
            auto context=std::make_shared<Context>(RegtestNetwork());
            context->Stop(); context.reset();
        }
        report.pushKV("idle_context_lifecycle",Snapshot(0));
        {
            auto context=std::make_shared<Context>(RegtestNetwork());
            allocation_probe::Window window{1};
            auto pending=std::make_unique<PQTransport>(context,false,std::nullopt,"partial-source");
            Bytes wire(PREFACE.begin(),PREFACE.end());
            auto header=Header(1); wire.insert(wire.end(),header.begin(),header.end());
            auto net=RegtestNetwork(); wire.insert(wire.end(),net.begin(),net.end());
            for (auto byte:wire) {
                std::span<const uint8_t> part{&byte,1}; Require(pending->ReceivedBytes(part) && part.empty(),"partial handshake failed");
            }
            Require(context->Pending()==1,"partial pending count wrong");
            pending.reset(); context->Stop(); context.reset();
        }
        report.pushKV("partial_m1_one_endpoint",Snapshot(1));
        {
            auto context=std::make_shared<Context>(RegtestNetwork());
            allocation_probe::Window window{2};
            std::vector<std::unique_ptr<PQTransport>> initiators,responders;
            for (unsigned n=0;n<4;++n) {
                initiators.push_back(std::make_unique<PQTransport>(context,true,std::nullopt,"i"+std::to_string(n)));
                responders.push_back(std::make_unique<PQTransport>(context,false,std::nullopt,"r"+std::to_string(n)));
            }
            const auto deadline=Clock::now()+10s;
            for (;;) {
                bool ready=true;
                Require(Clock::now()<deadline,"parallel allocation probe timeout");
                for (unsigned n=0;n<4;++n) {
                    Require(initiators[n]->Poll() && responders[n]->Poll(),"parallel polling failed");
                    Transfer(*initiators[n],*responders[n]); Transfer(*responders[n],*initiators[n]);
                    ready &= initiators[n]->GetInfo().pq_encrypted && responders[n]->GetInfo().pq_encrypted;
                }
                if (ready) break;
                std::this_thread::sleep_for(100us);
            }
            Require(context->Pending()==0,"completed sessions retain quota");
            initiators.clear(); responders.clear(); context->Stop(); context.reset();
        }
        report.pushKV("four_concurrent_sessions_eight_endpoints",Snapshot(2));
        std::cout<<report.write(2)<<std::endl;
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
