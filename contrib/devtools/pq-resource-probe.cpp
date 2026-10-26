// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// Dedicated test executable: allocation interposition is never linked into a
// node or ordinary unit/sanitizer runner. Requested C++ heap bytes are measured;
// libc/pthread/allocator arenas and native crypto stack storage are separate.
#include <util/string.h>
#include <net_pq.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <charconv>
#include <exception>

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
int TransportAllocation(std::string_view phase, int allocation)
{
    // A failing noexcept boundary is recorded without a core dump. This handler
    // and allocation interposition exist only in this excluded probe binary.
    std::set_terminate([] { std::_Exit(86); });
    auto context=std::make_shared<Context>(RegtestNetwork());
    auto i=std::make_unique<PQTransport>(context,true,std::nullopt,"probe-i");
    auto r=std::make_unique<PQTransport>(context,false,std::nullopt,"probe-r");
    const bool keys=phase=="keys", queue=phase=="queue";
    Require(keys || queue || phase=="send" || phase=="info" || phase=="receive" || phase=="message","unknown allocation phase");
    const auto deadline=Clock::now()+5s;
    for (;;) {
        Require(Clock::now()<deadline,"transport allocation setup timed out");
        Require(i->Poll() && r->Poll(),"transport setup failed");
        if (queue && !std::get<0>(i->GetBytesToSend(false)).empty()) break;
        if (keys && std::get<0>(i->GetBytesToSend(false)).size()==60) break;
        if (!keys && !queue && i->GetInfo().pq_encrypted && r->GetInfo().pq_encrypted) break;
        Transfer(*i,*r); Transfer(*r,*i); std::this_thread::sleep_for(100us);
    }
    bool injected{false}, returned{false};
    if (phase=="send") {
        CSerializedNetMsg message; message.m_type="ping"; message.data.resize(64,42);
        allocation_probe::fail_after=allocation;
        returned=i->SetMessageToSend(message);
        injected=allocation_probe::fail_after<0; allocation_probe::fail_after=-1;
        Require(returned!=injected,"send did not fail closed");
        if (injected) Require(!i->Poll() && !i->GetInfo().pq_authenticated && message.data.size()==64,"failed send retained state or consumed input");
        Require(r->GetInfo().pq_authenticated,"send failure damaged unrelated endpoint ownership");
    } else if (keys) {
        allocation_probe::fail_after=allocation; i->MarkBytesSent(60); returned=true;
        injected=allocation_probe::fail_after<0; allocation_probe::fail_after=-1;
        Require(i->GetInfo().pq_authenticated!=injected,"key-install failure published authentication");
        if (injected) Require(!i->Poll(),"failed key installation stayed usable");
    } else if (phase=="info") {
        allocation_probe::fail_after=allocation; const auto info=i->GetInfo(); returned=true;
        injected=allocation_probe::fail_after<0; allocation_probe::fail_after=-1;
        Require(info.pq_authenticated!=injected,"status allocation failure published assurance");
        if (injected) Require(!info.session_id && info.pq_identity.empty(),"failed status retained details");
        Require(i->Poll() && r->Poll(),"status query failure damaged live transport");
    } else {
        CSerializedNetMsg message; message.m_type="ping"; message.data.resize(64,42);
        if (!queue) Require(i->SetMessageToSend(message),"receive setup failed");
        auto wire=std::get<0>(i->GetBytesToSend(false));
        if (phase=="message") { Transfer(*i,*r); Require(r->ReceivedMessageComplete(),"message setup failed"); }
        bool escaped{false}, reject{false};
        allocation_probe::fail_after=allocation;
        try {
            if (phase=="message") { auto decoded=r->GetReceivedMessage(1us,reject); returned=!reject; }
            else { returned=r->ReceivedBytes(wire); }
        } catch (const std::bad_alloc&) { escaped=true; }
        injected=allocation_probe::fail_after<0; allocation_probe::fail_after=-1;
        Require(!escaped,"allocation exception escaped the transport boundary");
        Require(returned!=injected,"receive did not fail closed");
        if (injected) Require(!r->Poll() && !r->GetInfo().pq_authenticated,"receive failure retained live state");
    }
    i.reset(); r.reset(); context->Stop();
    Require(context->Pending()==0,"transport allocation failure retained admission");
    std::cout<<"{\"phase\":\""<<phase<<"\",\"allocation_index\":"<<allocation
             <<",\"allocation_injected\":"<<(injected ? "true" : "false")
             <<",\"returned\":"<<(returned ? "true" : "false")<<",\"pending_after_cleanup\":0}\n";
    return 0;
}
}
int main(int argc,char** argv)
{
    try {
        BasicTestingSetup setup{ChainType::REGTEST};
        if (argc==2 && std::string_view(argv[1]).starts_with("--transport-allocation=")) {
            auto argument=std::string_view(argv[1]).substr(23);
            const auto colon=argument.find(':');
            Require(colon!=std::string_view::npos,"expected phase:index");
            int allocation{-1}; const auto number=argument.substr(colon+1);
            const auto result=std::from_chars(number.data(),number.data()+number.size(),allocation);
            Require(result.ec==std::errc{} && result.ptr==number.data()+number.size() && allocation>=0 && allocation<=32,"invalid bounded allocation index");
            return TransportAllocation(argument.substr(0,colon),allocation);
        }
        if (argc==2 && std::string_view(argv[1]).starts_with("--allocation-failure=")) {
            const std::string_view number{argv[1]+21}; int allocation{-1};
            const auto result=std::from_chars(number.data(),number.data()+number.size(),allocation);
            Require(result.ec==std::errc{} && result.ptr==number.data()+number.size() && allocation>=0 && allocation<=32,"invalid bounded allocation index");
            return FaultInjection(allocation);
        }
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
                initiators.push_back(std::make_unique<PQTransport>(context,true,std::nullopt,"i"+util::ToString(n)));
                responders.push_back(std::make_unique<PQTransport>(context,false,std::nullopt,"r"+util::ToString(n)));
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
