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
#include <util/fs.h>
#include <fstream>
#include <sstream>
#include <ctime>

namespace allocation_probe {
struct Counters {
    std::atomic<size_t> live{0},peak{0},count{0},budget{0};
    std::atomic<bool> budget_exceeded{false};
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
    auto* owner=active.load(std::memory_order_acquire);
    size_t live{0};
    if (owner) {
        live=owner->live.fetch_add(size)+size;
        const auto budget=owner->budget.load();
        if (budget && live>budget) { owner->budget_exceeded=true; owner->live.fetch_sub(size); throw std::bad_alloc(); }
    }
    void* raw=std::malloc(std::max(size,size_t{1})+sizeof(Header)+alignment);
    if (!raw) { if (owner) owner->live.fetch_sub(size); throw std::bad_alloc(); }
    auto address=(reinterpret_cast<uintptr_t>(raw)+sizeof(Header)+alignment-1)&~(uintptr_t(alignment)-1);
    auto* header=reinterpret_cast<Header*>(address)-1;
    *header={raw,size,owner};
    if (owner) {
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
    explicit Window(size_t index,size_t budget=0) { counters[index].budget=budget; counters[index].budget_exceeded=false; active.store(&counters[index],std::memory_order_release); }
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
    auto context=std::make_shared<Context>(SelectedNetwork());
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
    auto context=std::make_shared<Context>(SelectedNetwork());
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
int BoundedNumber(std::string_view text,int low,int high)
{
    int number{-1}; const auto result=std::from_chars(text.data(),text.data()+text.size(),number);
    Require(result.ec==std::errc{} && result.ptr==text.data()+text.size() && number>=low && number<=high,"invalid bounded probe option");
    return number;
}
UniValue ProcessSnapshot()
{
    UniValue out{UniValue::VOBJ};
#ifdef __linux__
    {
    std::ifstream input{"/proc/self/status"}; std::string line;
    Require(input.is_open(),"cannot read process status");
    while (std::getline(input,line)) {
        std::istringstream fields{line}; fields.imbue(std::locale::classic());
        std::string key; uint64_t value; fields>>key;
        if (key=="VmRSS:" || key=="VmHWM:" || key=="VmSize:" || key=="Threads:") {
            fields>>value; out.pushKV(key=="Threads:" ? "threads" : key.substr(0,key.size()-1)+"_kib",value);
        }
    }
    }
    size_t descriptors{0}; for ([[maybe_unused]] const auto& entry : fs::directory_iterator{"/proc/self/fd"}) ++descriptors;
    // The iterator contributes one descriptor to its own /proc scan.
    out.pushKV("file_descriptors",uint64_t(descriptors-1));
#endif
    return out;
}
int Churn(unsigned sessions,unsigned cycles)
{
#ifndef __linux__
    throw std::runtime_error("B2 process-resource churn qualification requires Linux /proc");
#endif
    const unsigned pacing_ms=std::max(1100U,250*sessions+100);
    const size_t heap_budget=2*sessions*65536+256*1024;
    const auto started=Clock::now(); const auto cpu_started=std::clock();
    const auto before=ProcessSnapshot();
    std::vector<double> latency; latency.reserve(sessions*cycles);
    std::vector<std::optional<uint256>> previous_id(sessions);
    std::vector<std::array<char,103>> previous_identity(sessions);
    std::vector<uint64_t> retained_by_cycle; retained_by_cycle.reserve(cycles);
    double packet_seconds{0}; uint64_t wire_bytes{0};
    Context::ResourceStats stats;
    {
        allocation_probe::Window window{3,heap_budget};
        auto context=std::make_shared<Context>(SelectedNetwork());
        for (unsigned cycle=0;cycle<cycles;++cycle) {
            const auto cycle_start=Clock::now();
            Require(Clock::now()-started<std::chrono::seconds(cycles*3+15),"bounded churn watchdog expired");
            std::vector<std::unique_ptr<PQTransport>> initiators,responders;
            for (unsigned n=0;n<sessions;++n) {
                initiators.push_back(std::make_unique<PQTransport>(context,true,std::nullopt,"churn-i"+util::ToString(n)));
                responders.push_back(std::make_unique<PQTransport>(context,false,std::nullopt,"churn-r"+util::ToString(n)));
            }
            std::vector<bool> observed(sessions,false);
            for (;;) {
                bool ready=true;
                Require(Clock::now()-cycle_start<5s,"bounded handshake deadline exceeded");
                for (unsigned n=0;n<sessions;++n) {
                    Require(initiators[n]->Poll() && responders[n]->Poll(),"churn handshake failed");
                    Transfer(*initiators[n],*responders[n]); Transfer(*responders[n],*initiators[n]);
                    auto i=initiators[n]->GetInfo(),r=responders[n]->GetInfo();
                    const bool established=i.pq_authenticated && r.pq_authenticated;
                    ready &= established;
                    if (established && !observed[n]) {
                        Require(i.pq_encrypted && r.pq_encrypted && !i.pq_identity_verified && !r.pq_identity_verified,"incorrect unpinned assurance");
                        Require(i.session_id==r.session_id && i.session_id.has_value(),"session IDs disagree");
                        Require(i.pq_identity.size()==previous_identity[n].size(),"unexpected fingerprint encoding");
                        Require(i.session_id!=previous_id[n] && !std::ranges::equal(i.pq_identity,previous_identity[n]),"stale ephemeral session identity");
                        previous_id[n]=i.session_id; std::copy(i.pq_identity.begin(),i.pq_identity.end(),previous_identity[n].begin());
                        observed[n]=true; latency.push_back(std::chrono::duration<double,std::milli>(Clock::now()-cycle_start).count());
                    }
                }
                Require(!allocation_probe::counters[3].budget_exceeded.load(),"requested C++ heap budget exceeded");
                if (ready) break;
                std::this_thread::sleep_for(100us);
            }
            Require(context->Pending()==0,"established churn retained pending quota");
            const auto packets_start=Clock::now();
            for (unsigned packet=0;packet<300;++packet) for (unsigned n=0;n<sessions;++n) {
                for (bool reverse : {false,true}) {
                    auto& sender=reverse ? responders[n] : initiators[n];
                    auto& receiver=reverse ? initiators[n] : responders[n];
                    CSerializedNetMsg ping; ping.m_type="ping"; ping.data.resize(8,42);
                    Require(sender->SetMessageToSend(ping),"churn packet send failed");
                    wire_bytes+=std::get<0>(sender->GetBytesToSend(false)).size(); Transfer(*sender,*receiver);
                    Require(receiver->ReceivedMessageComplete(),"churn message missing"); bool reject{false};
                    auto message=receiver->GetReceivedMessage(1us,reject);
                    Require(!reject && message.m_type=="ping" && message.m_message_size==8,"churn packet rejected");
                }
            }
            packet_seconds+=std::chrono::duration<double>(Clock::now()-packets_start).count();
            initiators.clear(); responders.clear(); Require(context->Pending()==0,"churn teardown retained admission");
            retained_by_cycle.push_back(allocation_probe::counters[3].live.load());
            stats=context->Resources(); Require(stats.active<=2 && stats.queued<=32,"worker/queue bound exceeded");
            Require(ProcessSnapshot()["VmHWM_kib"].getInt<uint64_t>()<=512*1024,"process RSS budget exceeded");
            if (cycle+1<cycles) std::this_thread::sleep_until(cycle_start+std::chrono::milliseconds(pacing_ms));
        }
        context->Stop(); Require(context->Pending()==0,"churn Stop retained admission"); context.reset();
    }
    Require(allocation_probe::counters[3].live.load()==0,"churn retains requested heap");
    Require(!allocation_probe::counters[3].budget_exceeded.load(),"requested C++ heap budget exceeded");
    const auto after=ProcessSnapshot();
    std::vector<double> steady(latency.begin()+sessions,latency.end()); std::sort(steady.begin(),steady.end());
    const auto first_cycle_max=*std::max_element(latency.begin(),latency.begin()+sessions);
    std::sort(latency.begin(),latency.end());
    Require(before["threads"].getInt<int>()==after["threads"].getInt<int>(),"churn retained worker threads");
    Require(before["file_descriptors"].getInt<int>()==after["file_descriptors"].getInt<int>(),"churn retained descriptors");
    UniValue report{UniValue::VOBJ}; report.pushKV("sessions_per_cycle",sessions); report.pushKV("cycles",cycles);
    report.pushKV("completed_handshakes",sessions*cycles); report.pushKV("packets_each_direction_per_session",300);
    report.pushKV("pacing_ms",pacing_ms); report.pushKV("max_duration_seconds",cycles*3+15);
    report.pushKV("max_pending_endpoints",2*sessions); report.pushKV("workers",2);
    report.pushKV("requested_cpp_heap_budget_bytes",uint64_t(heap_budget)); report.pushKV("process_rss_budget_mib",512);
    std::ifstream cpu_limit{"/sys/fs/cgroup/cpu.max"}; std::string cpu_max; std::getline(cpu_limit,cpu_max);
    report.pushKV("cgroup_cpu_max",cpu_max);
    report.pushKV("heap",Snapshot(3)); report.pushKV("before",before); report.pushKV("after",after);
    UniValue retained{UniValue::VARR}; for (auto bytes : retained_by_cycle) retained.push_back(bytes);
    report.pushKV("requested_heap_after_each_cycle_with_context_alive",retained);
    UniValue sizes{UniValue::VOBJ}; sizes.pushKV("context",uint64_t(sizeof(Context))); sizes.pushKV("work",uint64_t(sizeof(Work)));
    sizes.pushKV("transport",uint64_t(sizeof(PQTransport))); sizes.pushKV("handshake",uint64_t(sizeof(Handshake)));
    sizes.pushKV("identity",uint64_t(sizeof(Identity))); sizes.pushKV("packet_cipher",uint64_t(sizeof(PacketCipher)));
    report.pushKV("logical_object_sizes_bytes",sizes);
    report.pushKV("wall_seconds",std::chrono::duration<double>(Clock::now()-started).count());
    report.pushKV("process_cpu_seconds",double(std::clock()-cpu_started)/CLOCKS_PER_SEC);
    report.pushKV("active_packet_exchange_seconds",packet_seconds); report.pushKV("wire_bytes_exchanged",wire_bytes);
    report.pushKV("active_packet_wire_mib_per_second",wire_bytes/(1024.0*1024.0)/packet_seconds);
    report.pushKV("packet_measurement","8-byte ping payloads, adapter/new-delete instrumentation included; pacing and handshake excluded");
    report.pushKV("completed_jobs",stats.completed_jobs); report.pushKV("total_job_us",stats.total_job_us);
    report.pushKV("max_job_us",stats.max_job_us); report.pushKV("peak_pending",stats.peak_pending); report.pushKV("peak_queue",stats.peak_queued);
    report.pushKV("stack_reservations_bytes",uint64_t(stats.stack_reservations[0]+stats.stack_reservations[1]));
    report.pushKV("handshake_samples",uint64_t(latency.size()));
    report.pushKV("handshake_median_ms",(latency[(latency.size()-1)/2]+latency[latency.size()/2])/2);
    if (latency.size()>=20) report.pushKV("handshake_p95_ms",latency[(95*latency.size()+99)/100-1]);
    report.pushKV("handshake_max_ms",latency.back());
    report.pushKV("first_cycle_max_ms",first_cycle_max); report.pushKV("steady_handshake_samples",uint64_t(steady.size()));
    if (!steady.empty()) report.pushKV("steady_handshake_median_ms",(steady[(steady.size()-1)/2]+steady[steady.size()/2])/2);
    std::cout<<report.write(2)<<std::endl; return 0;
}
}
int main(int argc,char** argv)
{
    try {
        BasicTestingSetup setup{ChainType::REGTEST};
        if (argc==2 && std::string_view(argv[1]).starts_with("--churn=")) {
            const auto text=std::string_view(argv[1]).substr(8); const auto colon=text.find(':');
            Require(colon!=std::string_view::npos,"expected bounded --churn=sessions:cycles");
            return Churn(BoundedNumber(text.substr(0,colon),1,8),BoundedNumber(text.substr(colon+1),1,64));
        }
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
            auto context=std::make_shared<Context>(SelectedNetwork());
            context->Stop(); context.reset();
        }
        report.pushKV("idle_context_lifecycle",Snapshot(0));
        {
            auto context=std::make_shared<Context>(SelectedNetwork());
            allocation_probe::Window window{1};
            auto pending=std::make_unique<PQTransport>(context,false,std::nullopt,"partial-source");
            Bytes wire(PREFACE.begin(),PREFACE.end());
            auto header=Header(1); wire.insert(wire.end(),header.begin(),header.end());
            auto net=SelectedNetwork(); wire.insert(wire.end(),net.begin(),net.end());
            for (auto byte:wire) {
                std::span<const uint8_t> part{&byte,1}; Require(pending->ReceivedBytes(part) && part.empty(),"partial handshake failed");
            }
            Require(context->Pending()==1,"partial pending count wrong");
            pending.reset(); context->Stop(); context.reset();
        }
        report.pushKV("partial_m1_one_endpoint",Snapshot(1));
        {
            auto context=std::make_shared<Context>(SelectedNetwork());
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
