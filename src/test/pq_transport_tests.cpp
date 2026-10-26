// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <util/string.h>
#include <net_pq.h>
#include <test/util/setup_common.h>
#include <util/fs.h>
#include <boost/test/unit_test.hpp>
#include <fstream>
#include <iostream>
#include <ctime>
#include <univalue.h>
#ifndef WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
using namespace pq;
using namespace std::chrono_literals;
bool Transfer(PQTransport& from, PQTransport& to, size_t chunk)
{
    auto [wire, more, type] = from.GetBytesToSend(false);
    if (wire.empty()) return false;
    const auto count = std::min(chunk, wire.size());
    auto part = wire.first(count);
    BOOST_REQUIRE(to.ReceivedBytes(part)); BOOST_REQUIRE(part.empty());
    from.MarkBytesSent(count); return true;
}
void Establish(PQTransport& i, PQTransport& r, size_t fragment)
{
    auto deadline = Clock::now()+5s;
    while (!(i.GetInfo().pq_encrypted && r.GetInfo().pq_encrypted)) {
        BOOST_REQUIRE(Clock::now() < deadline);
        BOOST_REQUIRE(i.Poll()); BOOST_REQUIRE(r.Poll());
        bool moved = Transfer(i, r, fragment); moved |= Transfer(r, i, fragment);
        if (!moved) std::this_thread::sleep_for(1ms);
    }
}
struct Fixture : BasicTestingSetup {
    Fixture() : BasicTestingSetup(ChainType::REGTEST) {}
};
}
BOOST_FIXTURE_TEST_SUITE(pq_transport_tests, Fixture)
BOOST_AUTO_TEST_CASE(fragmentation_gates_duplex_and_cleanup)
{
    for (size_t fragment : {size_t{1}, size_t{7}, size_t{4096}, size_t{65536}}) {
        auto context = std::make_shared<Context>(RegtestNetwork());
        {
            PQTransport i(context, true, {}, "4:i"), r(context, false, {}, "4:r");
            CSerializedNetMsg early; early.m_type = "version"; early.data = {1,2,3};
            BOOST_CHECK(!i.SetMessageToSend(early)); BOOST_CHECK_EQUAL(early.data.size(), 3U);
            BOOST_CHECK(!r.SetMessageToSend(early)); BOOST_CHECK(!i.ReceivedMessageComplete());
            Establish(i, r, fragment);
            auto a = i.GetInfo(), b = r.GetInfo();
            BOOST_CHECK(a.pq_authenticated && b.pq_authenticated); BOOST_CHECK(!a.pq_identity_verified && !b.pq_identity_verified);
            BOOST_CHECK(a.session_id == b.session_id); BOOST_CHECK(a.pq_identity != b.pq_identity);
            BOOST_REQUIRE(i.SetMessageToSend(early));
            CSerializedNetMsg reply; reply.m_type = "pong"; reply.data = {4,5,6};
            BOOST_REQUIRE(r.SetMessageToSend(reply)); // Concurrent bidirectional buffered writes.
            while (!i.ReceivedMessageComplete() || !r.ReceivedMessageComplete()) {
                if (!r.ReceivedMessageComplete()) Transfer(i,r,fragment);
                if (!i.ReceivedMessageComplete()) Transfer(r,i,fragment);
            }
            bool reject{false};
            auto request = r.GetReceivedMessage(1us, reject); BOOST_CHECK(!reject); BOOST_CHECK_EQUAL(request.m_type, "version"); BOOST_CHECK_EQUAL(request.m_message_size, 3U);
            auto response = i.GetReceivedMessage(2us, reject); BOOST_CHECK(!reject); BOOST_CHECK_EQUAL(response.m_type, "pong");
            i.Disconnect(); BOOST_CHECK(!i.GetInfo().pq_encrypted); BOOST_CHECK(!i.GetInfo().pq_authenticated); BOOST_CHECK(!i.Poll());
            BOOST_CHECK(!i.ShouldReconnectV1());
        }
        context->Stop(); BOOST_CHECK_EQUAL(context->Pending(), 0U);
    }
}
BOOST_AUTO_TEST_CASE(m5_coalesced_packet_and_no_early_application)
{
    auto context = std::make_shared<Context>(RegtestNetwork());
    PQTransport i(context,true,{},"4:i"), r(context,false,{},"4:r");
    Bytes m5;
    auto deadline = Clock::now()+5s;
    while (m5.empty()) {
        BOOST_REQUIRE(Clock::now() < deadline); BOOST_REQUIRE(i.Poll()); BOOST_REQUIRE(r.Poll());
        auto [wire, more, type] = i.GetBytesToSend(false);
        if (wire.size() == 60) {
            CSerializedNetMsg early; early.m_type = "ping";
            BOOST_CHECK(!i.SetMessageToSend(early)); BOOST_CHECK(!r.GetInfo().pq_encrypted);
            m5.assign(wire.begin(),wire.end());
            i.MarkBytesSent(59); BOOST_CHECK(!i.GetInfo().pq_encrypted);
            i.MarkBytesSent(1); BOOST_CHECK(i.GetInfo().pq_encrypted);
            break;
        }
        Transfer(i,r,65536); Transfer(r,i,65536); std::this_thread::sleep_for(1ms);
    }
    CSerializedNetMsg msg; msg.m_type="ping"; msg.data={1,2,3,4,5,6,7,8}; BOOST_REQUIRE(i.SetMessageToSend(msg));
    auto [wire, more, type] = i.GetBytesToSend(false); m5.insert(m5.end(),wire.begin(),wire.end()); i.MarkBytesSent(wire.size());
    std::span<const uint8_t> combined{m5}; BOOST_REQUIRE(r.ReceivedBytes(combined)); BOOST_CHECK(combined.empty());
    BOOST_CHECK(r.GetInfo().pq_encrypted); BOOST_CHECK(r.ReceivedMessageComplete());
}
BOOST_AUTO_TEST_CASE(admission_cancellation_and_no_fallback)
{
    auto context = std::make_shared<Context>(RegtestNetwork());
    std::vector<std::shared_ptr<void>> leases;
    for (int i=0;i<4;++i) { auto lease=context->Admit("same-source"); BOOST_REQUIRE(lease); leases.push_back(lease); }
    BOOST_CHECK(!context->Admit("same-source")); BOOST_CHECK_EQUAL(context->Pending(),4U);
    leases.clear(); BOOST_CHECK_EQUAL(context->Pending(),0U);
    for (int i=0;i<16;++i) { auto lease=context->Admit(""); BOOST_REQUIRE(lease); leases.push_back(lease); }
    BOOST_CHECK(!context->Admit("")); leases.clear();
    {
        PQTransport pending(context,true,{},"new-source"); pending.Disconnect();
        BOOST_CHECK(!pending.Poll()); BOOST_CHECK(!pending.GetInfo().pq_identity_verified); BOOST_CHECK(!pending.ShouldReconnectV1());
    }
    context->Stop(); BOOST_CHECK_EQUAL(context->Pending(),0U);
    BOOST_CHECK(!context->Admit("after-stop"));
    BOOST_CHECK(!context->Submit(std::make_shared<Work>(1)));
    auto c = std::make_shared<Context>(RegtestNetwork()); PQTransport inbound(c,false,{},"source");
    Bytes wrong(16,0); std::span<const uint8_t> bytes{wrong}; BOOST_CHECK(!inbound.ReceivedBytes(bytes)); BOOST_CHECK(!inbound.ReceivedMessageComplete());
}
BOOST_AUTO_TEST_CASE(silent_inbound_connections_reserve_pending_quota)
{
    auto context=std::make_shared<Context>(RegtestNetwork());
    std::vector<std::unique_ptr<PQTransport>> silent;
    for (unsigned n=0;n<4;++n) {
        silent.push_back(std::make_unique<PQTransport>(context,false,std::nullopt,"same-source"));
        BOOST_REQUIRE(silent.back()->Poll());
    }
    BOOST_CHECK_EQUAL(context->Pending(),4U);
    PQTransport excess(context,false,std::nullopt,"same-source");
    BOOST_CHECK(!excess.Poll());
    BOOST_CHECK(!excess.GetInfo().pq_authenticated);
    BOOST_CHECK(std::get<0>(excess.GetBytesToSend(false)).empty());
    silent.clear();
    BOOST_CHECK_EQUAL(context->Pending(),0U);
    BOOST_CHECK_EQUAL(context->Resources().completed_jobs,0U);
}
BOOST_AUTO_TEST_CASE(source_groups_global_cap_and_recovery)
{
    auto a=LookupHost("1.2.3.4",false), b=LookupHost("1.2.3.5",false);
    BOOST_REQUIRE(a && b); BOOST_CHECK(SourceGroup(*a,false)!=SourceGroup(*b,false)); BOOST_CHECK(SourceGroup(*a,true).empty());
    auto v6a=LookupHost("2001:db8:1:2::1",false), v6b=LookupHost("2001:db8:1:2::2",false), v6c=LookupHost("2001:db8:1:3::1",false);
    BOOST_REQUIRE(v6a && v6b && v6c); BOOST_CHECK(SourceGroup(*v6a,false)==SourceGroup(*v6b,false)); BOOST_CHECK(SourceGroup(*v6a,false)!=SourceGroup(*v6c,false));
    auto context=std::make_shared<Context>(RegtestNetwork()); std::vector<std::shared_ptr<void>> leases;
    for (int i=0;i<32;++i) { auto token=context->Admit(util::ToString(i)); BOOST_REQUIRE(token); leases.push_back(token); }
    BOOST_CHECK(!context->Admit("rate-overflow"));
    std::this_thread::sleep_for(4050ms);
    for (int i=32;i<64;++i) { auto token=context->Admit(util::ToString(i)); BOOST_REQUIRE(token); leases.push_back(token); }
    BOOST_CHECK_EQUAL(context->Pending(),64U); BOOST_CHECK(!context->Admit("global-overflow"));
    leases.clear(); BOOST_CHECK_EQUAL(context->Pending(),0U);
    std::this_thread::sleep_for(150ms); BOOST_CHECK(context->Admit("recovery"));
}
BOOST_AUTO_TEST_CASE(worker_queue_bound_and_cancelled_publication)
{
    auto context=std::make_shared<Context>(RegtestNetwork());
    std::mutex mutex; std::condition_variable cv; bool release{false}; unsigned entered{0};
    auto unblock=[&] { { std::lock_guard lock(mutex); release=true; } cv.notify_all(); };
    std::shared_ptr<void> cleanup(nullptr,[&](void*) { unblock(); });
    auto id=std::make_shared<Identity>(); BOOST_REQUIRE(id->FromSeed(std::array<uint8_t,32>{}));
    auto blocking_rng=[&](std::span<uint8_t>) { std::unique_lock lock(mutex); ++entered; cv.notify_all(); cv.wait(lock,[&] { return release; }); return false; };
    std::vector<std::shared_ptr<Work>> jobs;
    for (int i=0;i<2;++i) {
        auto job=std::make_shared<Work>(1); job->admission=context->Admit("active"+util::ToString(i)); BOOST_REQUIRE(job->admission);
        job->handshake=std::make_unique<Handshake>(true,RegtestNetwork(),std::nullopt,id,blocking_rng);
        BOOST_REQUIRE(context->Submit(job)); jobs.push_back(job);
    }
    { std::unique_lock lock(mutex); BOOST_REQUIRE(cv.wait_for(lock,2s,[&] { return entered==2; })); }
    std::this_thread::sleep_for(300ms); // Refill the two consumed admission tokens.
    PQTransport queued_transport(context,true,{},"pending-transport");
    PQTransport shutdown_transport(context,true,{},"shutdown-transport");
    for (int i=0;i<30;++i) {
        auto job=std::make_shared<Work>(1); job->admission=context->Admit("queued"+util::ToString(i)); BOOST_REQUIRE(job->admission);
        job->cancelled=true; BOOST_REQUIRE(context->Submit(job)); jobs.push_back(job);
    }
    auto overflow=std::make_shared<Work>(1); overflow->admission=jobs[0]->admission;
    BOOST_CHECK(!context->Submit(overflow));
    auto stats=context->Resources(); BOOST_CHECK_EQUAL(stats.active,2U); BOOST_CHECK_EQUAL(stats.queued,32U);
    std::this_thread::sleep_for(2050ms);
    BOOST_CHECK(!queued_transport.Poll()); // Absolute two-second queue deadline.
    for (auto& job : jobs) job->cancelled=true;
    unblock(); context->Stop();
    BOOST_CHECK(!shutdown_transport.Poll());
    for (auto& job : jobs) { if (job->done) BOOST_CHECK(!job->ok); }
    jobs.clear(); overflow.reset(); BOOST_CHECK_EQUAL(context->Pending(),0U);
}
BOOST_AUTO_TEST_CASE(execution_deadline_discards_late_success)
{
    auto context=std::make_shared<Context>(RegtestNetwork());
    auto identity=std::make_shared<Identity>(); BOOST_REQUIRE(identity->FromSeed(std::array<uint8_t,32>{}));
    auto delayed_rng=[first=true](std::span<uint8_t> output) mutable {
        if (first) { first=false; std::this_thread::sleep_for(5050ms); }
        std::fill(output.begin(),output.end(),42); return true;
    };
    auto job=std::make_shared<Work>(1); job->admission=context->Admit("deadline-source"); BOOST_REQUIRE(job->admission);
    job->handshake=std::make_unique<Handshake>(true,RegtestNetwork(),std::nullopt,identity,delayed_rng);
    BOOST_REQUIRE(context->Submit(job));
    const auto deadline=Clock::now()+8s;
    while (!job->done.load(std::memory_order_acquire) && Clock::now()<deadline) std::this_thread::sleep_for(1ms);
    BOOST_REQUIRE(job->done.load(std::memory_order_acquire));
    BOOST_CHECK(!job->ok); BOOST_CHECK(job->response.empty()); BOOST_CHECK(!job->handshake);
    BOOST_CHECK(context->Resources().max_job_us>=5'000'000);
    job.reset(); context->Stop(); BOOST_CHECK_EQUAL(context->Pending(),0U);
}
BOOST_AUTO_TEST_CASE(identity_pins_and_wrong_expected_identity)
{
    auto id = std::make_shared<Identity>();
    BOOST_REQUIRE(id->FromSeed(std::array<uint8_t,32>{}));
    auto network = RegtestNetwork(); auto expected = Fingerprint(network,id->pub);
    auto context = std::make_shared<Context>(network,id);
    PQTransport i(context,true,expected,"i"), r(context,false,expected,"r"); Establish(i,r,13);
    BOOST_CHECK(i.GetInfo().pq_identity_verified && r.GetInfo().pq_identity_verified);
    expected[0] ^= 1;
    PQTransport bad(context,true,expected,"bad-i"), peer(context,false,{},"bad-r");
    bool failed{false}; auto deadline=Clock::now()+5s;
    while (!failed && Clock::now()<deadline) {
        if (!bad.Poll() || !peer.Poll()) { failed=true; break; }
        Transfer(bad,peer,65536); Transfer(peer,bad,65536); std::this_thread::sleep_for(1ms);
    }
    BOOST_CHECK(failed); BOOST_CHECK(!bad.GetInfo().pq_encrypted); BOOST_CHECK(!bad.ShouldReconnectV1());
}
BOOST_AUTO_TEST_CASE(packet_failure_releases_nothing_and_large_send_memory)
{
    auto context=std::make_shared<Context>(RegtestNetwork());
    PQTransport i(context,true,{},"i"), r(context,false,{},"r"); Establish(i,r,65536);
    CSerializedNetMsg large; large.m_type="blockchunk"; large.data.resize(2'000'000,42);
    BOOST_REQUIRE(i.SetMessageToSend(large));
    while (!r.ReceivedMessageComplete()) Transfer(i,r,701);
    BOOST_CHECK_EQUAL(i.GetSendMemoryUsage(),0U);
    bool reject{false}; auto message=r.GetReceivedMessage(1us,reject); BOOST_CHECK(!reject); BOOST_CHECK_EQUAL(message.m_message_size,2'000'000U);
    CSerializedNetMsg ping; ping.m_type="ping"; ping.data.resize(8); BOOST_REQUIRE(i.SetMessageToSend(ping));
    auto [wire,more,type]=i.GetBytesToSend(false); Bytes tampered(wire.begin(),wire.end()); tampered.back()^=1;
    std::span<const uint8_t> input{tampered}; BOOST_CHECK(!r.ReceivedBytes(input));
    BOOST_CHECK(!r.ReceivedMessageComplete()); BOOST_CHECK(!r.GetInfo().pq_encrypted); BOOST_CHECK(!r.Poll());
}
BOOST_AUTO_TEST_CASE(benchmark, *boost::unit_test::disabled())
{
    auto drive=[](Transport& from,Transport& to) {
        auto [wire,more,type]=from.GetBytesToSend(false);
        const auto count=wire.size(); if (!count) return size_t{0};
        BOOST_REQUIRE(to.ReceivedBytes(wire)); BOOST_REQUIRE(wire.empty()); from.MarkBytesSent(count); return count;
    };
    UniValue report(UniValue::VOBJ);
    auto context=std::make_shared<Context>(RegtestNetwork());
    for (bool pq_mode : {false,true}) {
        std::vector<double> times;
        size_t min_bytes=SIZE_MAX,max_bytes=0;
        const auto cpu_start=std::clock();
        for (unsigned sample=0;sample<12;++sample) {
            const auto start=Clock::now();
            std::unique_ptr<Transport> i,r;
            if (pq_mode) {
                i=std::make_unique<PQTransport>(context,true,std::nullopt,"i"+util::ToString(sample));
                r=std::make_unique<PQTransport>(context,false,std::nullopt,"r"+util::ToString(sample));
            } else { i=std::make_unique<V2Transport>(1,true); r=std::make_unique<V2Transport>(2,false); }
            size_t bytes=0;
            while (!(i->GetInfo().session_id && r->GetInfo().session_id)) {
                BOOST_REQUIRE(Clock::now()-start<5s); BOOST_REQUIRE(i->Poll()); BOOST_REQUIRE(r->Poll());
                const auto a=drive(*i,*r),b=drive(*r,*i); bytes+=a+b;
                if (!a && !b) std::this_thread::sleep_for(100us);
            }
            times.push_back(std::chrono::duration<double,std::milli>(Clock::now()-start).count());
            min_bytes=std::min(min_bytes,bytes); max_bytes=std::max(max_bytes,bytes);
            if (pq_mode) BOOST_CHECK_EQUAL(bytes,15488U);
        }
        std::sort(times.begin(),times.end()); UniValue row(UniValue::VOBJ);
        row.pushKV("samples",12); row.pushKV("median_ms",times[6]); row.pushKV("max_ms",times.back());
        row.pushKV("process_cpu_ms",1000.0*(std::clock()-cpu_start)/CLOCKS_PER_SEC);
        row.pushKV("min_handshake_bytes",uint64_t(min_bytes)); row.pushKV("max_handshake_bytes",uint64_t(max_bytes));
        report.pushKV(pq_mode ? "pq" : "bip324",row);
    }
    for (bool pq_mode : {false,true}) {
        std::unique_ptr<Transport> i,r;
        if (pq_mode) {
            i=std::make_unique<PQTransport>(context,true,std::nullopt,"throughput-i");
            r=std::make_unique<PQTransport>(context,false,std::nullopt,"throughput-r");
        } else { i=std::make_unique<V2Transport>(1,true); r=std::make_unique<V2Transport>(2,false); }
        const auto handshake_start=Clock::now();
        while (!(i->GetInfo().session_id && r->GetInfo().session_id)) {
            BOOST_REQUIRE(Clock::now()-handshake_start<5s); BOOST_REQUIRE(i->Poll()); BOOST_REQUIRE(r->Poll());
            const auto a=drive(*i,*r),b=drive(*r,*i);
            if (!a && !b) std::this_thread::sleep_for(100us);
        }
        const auto start=Clock::now();
        for (unsigned n=0;n<1024;++n) {
            CSerializedNetMsg message; message.m_type="tx"; message.data.resize(16384,42);
            BOOST_REQUIRE(i->SetMessageToSend(message));
            while (!r->ReceivedMessageComplete()) BOOST_REQUIRE(drive(*i,*r));
            bool reject{false}; auto decoded=r->GetReceivedMessage(1us,reject);
            BOOST_REQUIRE(!reject); BOOST_REQUIRE_EQUAL(decoded.m_message_size,16384U);
        }
        const auto seconds=std::chrono::duration<double>(Clock::now()-start).count();
        report.pushKV(pq_mode ? "pq_transport_mib_per_second" : "bip324_transport_mib_per_second",16.0/seconds);
    }
    std::array<uint8_t,32> key{}; Digest hash{}; PacketCipher enc(key,key,hash,0),dec(key,key,hash,0);
    Bytes contents(16384,42),wire,plain; bool decoy{false}; const auto start=Clock::now();
    for (unsigned n=0;n<1024;++n) {
        BOOST_REQUIRE(enc.Encrypt(contents,0,wire)); BOOST_REQUIRE(dec.DecryptLength(std::span{wire}.first(3)));
        BOOST_REQUIRE(dec.Decrypt(std::span{wire}.subspan(3),plain,decoy)); BOOST_REQUIRE(plain==contents);
    }
    auto seconds=std::chrono::duration<double>(Clock::now()-start).count();
    report.pushKV("pq_encrypt_decrypt_mib_per_second",16.0/seconds);
    context->Stop(); auto stats=context->Resources();
    report.pushKV("crypto_jobs",stats.completed_jobs); report.pushKV("max_job_us",stats.max_job_us);
    report.pushKV("worker_0_stack_reservation",uint64_t(stats.stack_reservations[0]));
    report.pushKV("worker_1_stack_reservation",uint64_t(stats.stack_reservations[1]));
    report.pushKV("peak_pending",stats.peak_pending); report.pushKV("peak_queue",stats.peak_queued);
    report.pushKV("handshake_object_bytes",uint64_t(sizeof(Handshake)));
    report.pushKV("identity_object_bytes",uint64_t(sizeof(Identity)));
    auto concurrent=std::make_shared<Context>(RegtestNetwork());
    std::vector<std::unique_ptr<PQTransport>> initiators,responders;
    const auto concurrent_start=Clock::now(); const auto concurrent_cpu=std::clock(); unsigned peak_active{0};
    for (unsigned n=0;n<4;++n) {
        initiators.push_back(std::make_unique<PQTransport>(concurrent,true,std::nullopt,"parallel-i"+util::ToString(n)));
        responders.push_back(std::make_unique<PQTransport>(concurrent,false,std::nullopt,"parallel-r"+util::ToString(n)));
    }
    for (;;) {
        bool complete=true; size_t moved=0;
        BOOST_REQUIRE(Clock::now()-concurrent_start<5s);
        for (unsigned n=0;n<4;++n) {
            BOOST_REQUIRE(initiators[n]->Poll()); BOOST_REQUIRE(responders[n]->Poll());
            moved+=drive(*initiators[n],*responders[n]); moved+=drive(*responders[n],*initiators[n]);
            complete &= initiators[n]->GetInfo().pq_encrypted && responders[n]->GetInfo().pq_encrypted;
        }
        peak_active=std::max(peak_active,concurrent->Resources().active);
        if (complete) break;
        if (!moved) std::this_thread::sleep_for(100us);
    }
    UniValue parallel(UniValue::VOBJ);
    parallel.pushKV("sessions",4); parallel.pushKV("wall_ms",std::chrono::duration<double,std::milli>(Clock::now()-concurrent_start).count());
    parallel.pushKV("process_cpu_ms",1000.0*(std::clock()-concurrent_cpu)/CLOCKS_PER_SEC);
    parallel.pushKV("observed_peak_active",peak_active); parallel.pushKV("peak_queue",concurrent->Resources().peak_queued);
    parallel.pushKV("peak_pending",concurrent->Resources().peak_pending); BOOST_CHECK_LE(peak_active,2U);
    report.pushKV("four_concurrent_pq_sessions",parallel); concurrent->Stop();
    std::cout << "PQ_BENCHMARK " << report.write() << std::endl;
}
#ifndef WIN32
BOOST_AUTO_TEST_CASE(persistent_identity_creation_ignores_restrictive_umask)
{
    const auto directory=m_path_root/"pq-umask";
    fs::create_directory(directory); fs::permissions(directory,fs::perms::owner_all);
    const auto file=directory/"identity.dat";
    struct RestoreUmask { mode_t saved; ~RestoreUmask() { umask(saved); } } restore{umask(0777)};
    auto created=LoadIdentity(fs::PathToString(file),true,RegtestNetwork());
    struct stat status{};
    BOOST_REQUIRE_EQUAL(stat(fs::PathToString(file).c_str(),&status),0);
    BOOST_CHECK_EQUAL(status.st_mode & 07777,0600);
    auto loaded=LoadIdentity(fs::PathToString(file),false,RegtestNetwork());
    BOOST_CHECK(created->pub==loaded->pub);
}
BOOST_AUTO_TEST_CASE(persistent_identity_storage_failures)
{
    const auto directory=m_path_root/"pq-identities";
    fs::create_directory(directory); fs::permissions(directory,fs::perms::owner_all);
    const auto file=directory/"identity.dat"; const auto name=fs::PathToString(file);
    auto net=RegtestNetwork(); auto created=LoadIdentity(name,true,net); auto loaded=LoadIdentity(name,false,net);
    BOOST_CHECK(created->pub == loaded->pub); BOOST_CHECK_EQUAL(fs::file_size(file),2127U);
    BOOST_CHECK_THROW(LoadIdentity(name,true,net),std::runtime_error);
    auto wrong=net; wrong[0]^=1; BOOST_CHECK_THROW(LoadIdentity(name,false,wrong),std::runtime_error);
    fs::permissions(file,fs::perms::owner_read|fs::perms::owner_write|fs::perms::group_read);
    BOOST_CHECK_THROW(LoadIdentity(name,false,net),std::runtime_error);
    fs::permissions(file,fs::perms::owner_read|fs::perms::owner_write);
    auto link=directory/"link.dat"; fs::create_symlink(file,link); BOOST_CHECK_THROW(LoadIdentity(fs::PathToString(link),false,net),std::runtime_error);
    std::fstream stream(name,std::ios::in|std::ios::out|std::ios::binary); stream.seekp(95); stream.put(char{1}); stream.close();
    BOOST_CHECK_THROW(LoadIdentity(name,false,net),std::runtime_error);
    BOOST_CHECK_THROW(LoadIdentity(fs::PathToString(directory/"missing"),false,net),std::runtime_error);
}
BOOST_AUTO_TEST_CASE(identity_filesystem_types_atomic_creation_and_recovery)
{
    const auto directory=m_path_root/"pq-filesystem";
    fs::create_directory(directory); fs::permissions(directory,fs::perms::owner_all);
    const auto file=directory/"identity.dat";
    const auto name=fs::PathToString(file);
    const auto network=RegtestNetwork();
    auto original=LoadIdentity(name,true,network);
    auto hard=directory/"hardlink";
    fs::create_hard_link(file,hard);
    BOOST_CHECK_THROW(LoadIdentity(name,false,network),std::runtime_error);
    BOOST_CHECK_THROW(LoadIdentity(fs::PathToString(hard),false,network),std::runtime_error);
    fs::remove(hard);
    const auto fifo=fs::PathToString(directory/"fifo");
    BOOST_REQUIRE_EQUAL(mkfifo(fifo.c_str(),0600),0);
    BOOST_CHECK_THROW(LoadIdentity(fifo,false,network),std::runtime_error);
    fs::remove(fs::PathFromString(fifo));
    BOOST_CHECK_THROW(LoadIdentity(fs::PathToString(directory),false,network),std::runtime_error);
    fs::permissions(directory,fs::perms::owner_all|fs::perms::group_write);
    BOOST_CHECK_THROW(LoadIdentity(name,false,network),std::runtime_error);
    BOOST_CHECK_THROW(LoadIdentity(fs::PathToString(directory/"unsafe-create"),true,network),std::runtime_error);
    fs::permissions(directory,fs::perms::owner_all);
    const auto linked=m_path_root/"pq-linked-directory";
    fs::create_directory_symlink(directory,linked);
    BOOST_CHECK_THROW(LoadIdentity(fs::PathToString(linked/"identity.dat"),false,network),std::runtime_error);
    auto restored=LoadIdentity(name,false,network);
    BOOST_CHECK(restored->pub==original->pub);
    fs::resize_file(file,2126);
    BOOST_CHECK_THROW(LoadIdentity(name,false,network),std::runtime_error);
    fs::resize_file(file,2128);
    BOOST_CHECK_THROW(LoadIdentity(name,false,network),std::runtime_error);
    // Explicit recovery creates a different filename; a corrupt original is
    // retained rather than silently overwritten or regenerated.
    const auto recovered=directory/"recovered.dat";
    auto replacement=LoadIdentity(fs::PathToString(recovered),true,network);
    BOOST_CHECK(replacement->pub!=original->pub);
    BOOST_CHECK_EQUAL(fs::file_size(file),2128U);
    BOOST_CHECK(LoadIdentity(fs::PathToString(recovered),false,network)->pub==replacement->pub);

    const auto raced=fs::PathToString(directory/"race.dat");
    std::array<std::shared_ptr<const Identity>,2> winners;
    std::array<std::exception_ptr,2> errors;
    std::array<std::thread,2> contenders;
    for (size_t n=0;n<contenders.size();++n) contenders[n]=std::thread([&,n] {
        try { winners[n]=LoadIdentity(raced,true,network); } catch (...) { errors[n]=std::current_exception(); }
    });
    for (auto& thread:contenders) thread.join();
    BOOST_REQUIRE_EQUAL(bool(winners[0])+bool(winners[1]),1);
    BOOST_CHECK_EQUAL(bool(errors[0])+bool(errors[1]),1);
    BOOST_CHECK(LoadIdentity(raced,false,network)->pub==(winners[0] ? winners[0] : winners[1])->pub);
    for (const auto& entry:fs::directory_iterator(directory)) {
        BOOST_CHECK(!fs::PathToString(entry.path().filename()).starts_with(".mcapq-"));
    }
}
#endif
BOOST_AUTO_TEST_SUITE_END()
