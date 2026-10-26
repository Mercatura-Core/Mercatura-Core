// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <net_pq.h>
#include <net_pq_identity_format.h>
#include <test/util/pq_lifecycle.h>
#include <test/util/setup_common.h>
#include <util/fs.h>
#include <boost/test/unit_test.hpp>
#include <condition_variable>
#include <cerrno>
#include <fstream>
#include <future>
#include <type_traits>

namespace pq::test {
struct Access {
    static std::shared_ptr<Work> Job(PQTransport& transport) {
        std::lock_guard lock(transport.m_mutex); return transport.m_work;
    }
    static void NextGeneration(PQTransport& transport) {
        std::lock_guard lock(transport.m_mutex); ++transport.m_generation;
    }
    static bool Stopped(Context& context) {
        std::lock_guard lock(context.m_quota->mutex); return context.m_quota->stopped;
    }
    static bool SecretsCleared(const Handshake& h) {
        const auto zero = [](const auto& bytes) { return std::ranges::all_of(bytes, [](auto b) { return b == 0; }); };
        return !h.m_identity && zero(h.m_kem.priv.bytes) && zero(h.m_ss_i.bytes) && zero(h.m_ss_r.bytes) &&
            zero(h.m_prk.bytes) && zero(h.m_kc_i.bytes) && zero(h.m_kc_r.bytes) &&
            zero(h.m_keys.i_length.bytes) && zero(h.m_keys.i_payload.bytes) &&
            zero(h.m_keys.r_length.bytes) && zero(h.m_keys.r_payload.bytes);
    }
    static std::weak_ptr<const Identity> IdentityOwner(const Handshake& h) { return h.m_identity; }
    static const void* PrivateIdentity(const Handshake& h) { return h.m_identity ? h.m_identity->priv.bytes.data() : nullptr; }
    static void ExpireRecord(PQTransport& transport) {
        std::lock_guard lock(transport.m_mutex); transport.m_wait = Clock::now() - std::chrono::seconds(31);
    }
};
}
namespace {
using namespace pq;
using namespace pq::test;
using namespace std::chrono_literals;
struct Fixture : BasicTestingSetup { Fixture() : BasicTestingSetup(ChainType::REGTEST) {} };
struct Gate {
    std::mutex mutex; std::condition_variable cv; unsigned hits{0}; bool released{false};
    void Hold() { std::unique_lock lock(mutex); ++hits; cv.notify_all(); cv.wait(lock, [&] { return released; }); }
    bool Wait(unsigned count = 1) { std::unique_lock lock(mutex); return cv.wait_for(lock, 3s, [&] { return hits >= count; }); }
    bool Hit() { std::lock_guard lock(mutex); return hits != 0; }
    void Release() { { std::lock_guard lock(mutex); released = true; } cv.notify_all(); }
};
struct Cleanup { std::function<void()> action; ~Cleanup() { action(); } };
void WaitDone(const std::shared_ptr<Work>& job)
{
    const auto deadline = Clock::now() + 5s;
    while (!job->done.load(std::memory_order_acquire) && Clock::now() < deadline) std::this_thread::sleep_for(1ms);
    BOOST_REQUIRE(job->done.load(std::memory_order_acquire));
}
bool Transfer(PQTransport& from, PQTransport& to)
{
    auto [bytes, more, type] = from.GetBytesToSend(false);
    const auto count = bytes.size();
    if (!count) return false;
    BOOST_REQUIRE(to.ReceivedBytes(bytes)); BOOST_REQUIRE(bytes.empty()); from.MarkBytesSent(count); return true;
}
void Drive(PQTransport& i, PQTransport& r, const std::function<bool()>& finished)
{
    const auto deadline = Clock::now() + 5s;
    while (!finished()) {
        BOOST_REQUIRE(Clock::now() < deadline); BOOST_REQUIRE(i.Poll()); BOOST_REQUIRE(r.Poll());
        const bool a = Transfer(i, r), b = Transfer(r, i);
        if (!a && !b) std::this_thread::sleep_for(1ms);
    }
}
void NoAssurance(PQTransport& transport)
{
    const auto info = transport.GetInfo();
    BOOST_CHECK(!info.pq_encrypted && !info.pq_authenticated && !info.pq_identity_verified);
    BOOST_CHECK(!info.session_id); BOOST_CHECK(info.pq_identity.empty());
    BOOST_CHECK(!transport.ShouldReconnectV1()); BOOST_CHECK(!transport.ReceivedMessageComplete());
}
fs::path Directory(const fs::path& root, const char* name)
{
    const auto path = root / name;
    fs::create_directory(path); fs::permissions(path, fs::perms::owner_all); return path;
}
unsigned Temporaries(const fs::path& dir)
{
    unsigned result{0};
    for (const auto& entry : fs::directory_iterator(dir)) if (fs::PathToString(entry.path().filename()).starts_with(".mcapq-")) ++result;
    return result;
}
void RemoveTemporaries(const fs::path& dir)
{
    for (const auto& entry : fs::directory_iterator(dir)) if (fs::PathToString(entry.path().filename()).starts_with(".mcapq-")) fs::remove(entry.path());
}
}

BOOST_FIXTURE_TEST_SUITE(pq_lifecycle_tests, Fixture)
BOOST_AUTO_TEST_CASE(storage_prepublication_faults_release_descriptors)
{
    struct Case { const char* name; std::vector<Rule> rules; };
    for (const auto& item : std::vector<Case>{
        {"open-directory", {{Op::OPEN_DIRECTORY,1,Action::ERROR,EACCES}}},
        {"open-file", {{Op::OPEN_FILE,1,Action::ERROR,EROFS}}},
        {"directory-stat", {{Op::STAT_DIRECTORY,1,Action::ERROR,EIO}}},
        {"directory-owner", {{Op::STAT_PATH,1,Action::WRONG_OWNER,0}}},
        {"directory-replacement", {{Op::STAT_DIRECTORY,1,Action::CHANGED_INODE,0}}},
        {"temporary-owner", {{Op::STAT_FILE,1,Action::WRONG_OWNER,0}}},
        {"temporary-mode", {{Op::STAT_FILE,1,Action::WRONG_MODE,0}}},
        {"write-error", {{Op::WRITE,1,Action::ERROR,EIO}}},
        {"partial-write-error", {{Op::WRITE,1,Action::SHORT,113},{Op::WRITE,2,Action::ERROR,EIO}}},
        {"file-sync", {{Op::SYNC_FILE,1,Action::ERROR,EIO}}},
        {"publication", {{Op::PUBLISH,1,Action::ERROR,EIO}}},
    }) {
        const auto dir = Directory(m_path_root, item.name); const auto file = dir / "identity.dat";
        FileFaults faults; faults.rules = item.rules;
        { ScopedFiles scope(faults); BOOST_CHECK_THROW(LoadIdentity(fs::PathToString(file), true, RegtestNetwork()), std::runtime_error); }
        BOOST_CHECK_EQUAL(faults.OpenCount(), 0U); BOOST_CHECK(!fs::exists(file)); BOOST_CHECK_EQUAL(Temporaries(dir), 0U);
        if (faults.write_buffer) { BOOST_CHECK_EQUAL(faults.cleansed_buffers,1U); BOOST_CHECK(faults.buffers_zeroed); }
        const auto original = LoadIdentity(fs::PathToString(file), true, RegtestNetwork());
        BOOST_CHECK(LoadIdentity(fs::PathToString(file), false, RegtestNetwork())->pub == original->pub);
    }
}
BOOST_AUTO_TEST_CASE(storage_short_interrupted_io_and_read_failure_recovery)
{
    const auto dir = Directory(m_path_root, "interrupted-io"); const auto name = fs::PathToString(dir / "identity.dat");
    FileFaults writes; writes.rules = {{Op::WRITE,1,Action::INTERRUPT,0},{Op::WRITE,2,Action::SHORT,113}};
    std::shared_ptr<const Identity> created;
    { ScopedFiles scope(writes); created = LoadIdentity(name, true, RegtestNetwork()); }
    BOOST_CHECK_EQUAL(writes.OpenCount(),0U); BOOST_CHECK_GE(writes.calls[size_t(Op::WRITE)],3U);
    BOOST_CHECK_EQUAL(writes.cleansed_buffers,1U); BOOST_CHECK(writes.buffers_zeroed);
    FileFaults reads; reads.rules = {{Op::READ,1,Action::INTERRUPT,0},{Op::READ,2,Action::SHORT,17}};
    { ScopedFiles scope(reads); BOOST_CHECK(LoadIdentity(name,false,RegtestNetwork())->pub == created->pub); }
    BOOST_CHECK_EQUAL(reads.OpenCount(),0U); BOOST_CHECK_GE(reads.calls[size_t(Op::READ)],4U);
    BOOST_CHECK_EQUAL(reads.cleansed_buffers,1U); BOOST_CHECK(reads.buffers_zeroed);
    for (const auto& rule : std::vector<Rule>{{Op::READ,1,Action::ERROR,EIO},{Op::STAT_FILE,1,Action::WRONG_OWNER,0},{Op::STAT_FILE,1,Action::WRONG_MODE,0}}) {
        FileFaults faults; faults.rules = {rule};
        { ScopedFiles scope(faults); BOOST_CHECK_THROW(LoadIdentity(name,false,RegtestNetwork()),std::runtime_error); }
        BOOST_CHECK_EQUAL(faults.OpenCount(),0U);
        if (faults.read_buffer) { BOOST_CHECK_EQUAL(faults.cleansed_buffers,1U); BOOST_CHECK(faults.buffers_zeroed); }
        BOOST_CHECK(LoadIdentity(name,false,RegtestNetwork())->pub == created->pub);
    }
}
BOOST_AUTO_TEST_CASE(storage_postpublication_failure_preserves_identity)
{
    for (bool cleanup_failure : {false,true}) {
        const auto dir = Directory(m_path_root, cleanup_failure ? "cleanup" : "directory-sync"); const auto name = fs::PathToString(dir / "identity.dat");
        FileFaults faults;
        faults.rules = cleanup_failure ? std::vector<Rule>{{Op::UNLINK,1,Action::ERROR,EACCES},{Op::UNLINK,2,Action::ERROR,EACCES}} : std::vector<Rule>{{Op::SYNC_DIRECTORY,1,Action::ERROR,EIO}};
        { ScopedFiles scope(faults); BOOST_CHECK_THROW(LoadIdentity(name,true,RegtestNetwork()),std::runtime_error); }
        BOOST_CHECK_EQUAL(faults.OpenCount(),0U); BOOST_REQUIRE(fs::exists(fs::PathFromString(name)));
        if (cleanup_failure) {
            BOOST_CHECK_EQUAL(Temporaries(dir),1U);
            BOOST_CHECK_THROW(LoadIdentity(name,false,RegtestNetwork()),std::runtime_error);
            RemoveTemporaries(dir); // Explicit test-owner recovery; never automatic key replacement.
        }
        auto loaded = LoadIdentity(name,false,RegtestNetwork());
        BOOST_CHECK_THROW(LoadIdentity(name,true,RegtestNetwork()),std::runtime_error);
        BOOST_CHECK(LoadIdentity(name,false,RegtestNetwork())->pub == loaded->pub);
        BOOST_CHECK_EQUAL(Temporaries(dir),0U);
    }
}
BOOST_AUTO_TEST_CASE(storage_format_errors_do_not_modify_existing_files)
{
    constexpr std::array<const char*,7> names{"empty","truncated","trailing","version","network","checksum","key-binding"};
    Secret<32> seed; seed.bytes.fill(42); Identity identity; BOOST_REQUIRE(identity.FromSeed(seed.bytes));
    for (unsigned kind=0;kind<names.size();++kind) {
        const auto dir=Directory(m_path_root,names[kind]); const auto file=dir/"identity.dat";
        IdentityFile contents; auto network=RegtestNetwork(); auto public_key=identity.pub;
        if (kind==4) network[5]^=1;
        if (kind==6) public_key[0]^=1;
        EncodeIdentityFile(network,seed.bytes,public_key,contents);
        if (kind==3) contents.bytes[8]=2;
        if (kind==5) contents.bytes.back()^=1;
        const size_t count=kind==0 ? 0 : kind==1 ? 2126 : 2127;
        {
            // Public synthetic fixture material; never an operational seed.
            std::ofstream stream(fs::PathToString(file),std::ios::binary);
            stream.write(reinterpret_cast<const char*>(contents.bytes.data()),count);
            if (kind==2) stream.put(0);
        }
        fs::permissions(file,fs::perms::owner_read|fs::perms::owner_write);
        const auto size=fs::file_size(file); FileFaults faults;
        { ScopedFiles scope(faults); BOOST_CHECK_THROW(LoadIdentity(fs::PathToString(file),false,RegtestNetwork()),std::runtime_error); }
        BOOST_CHECK_EQUAL(fs::file_size(file),size); BOOST_CHECK_EQUAL(faults.OpenCount(),0U);
        BOOST_CHECK_EQUAL(faults.calls[size_t(Op::WRITE)],0U);
        if (faults.read_buffer) { BOOST_CHECK(faults.buffers_zeroed); BOOST_CHECK_EQUAL(faults.cleansed_buffers,1U); }
    }
}
BOOST_AUTO_TEST_CASE(storage_cleanup_error_reports_residual_temporary)
{
    const auto dir=Directory(m_path_root,"residual-temporary"); const auto name=fs::PathToString(dir/"identity.dat");
    FileFaults faults; faults.rules={{Op::WRITE,1,Action::SHORT,113},{Op::WRITE,2,Action::ERROR,EIO},{Op::UNLINK,1,Action::ERROR,EACCES}};
    std::string message;
    { ScopedFiles scope(faults); try { LoadIdentity(name,true,RegtestNetwork()); } catch (const std::runtime_error& error) { message=error.what(); } }
    BOOST_CHECK(message.find("cleanup failed")!=std::string::npos);
    BOOST_CHECK_EQUAL(faults.OpenCount(),0U); BOOST_CHECK(!fs::exists(fs::PathFromString(name)));
    BOOST_CHECK_EQUAL(Temporaries(dir),1U); BOOST_CHECK(faults.buffers_zeroed);
    BOOST_CHECK_THROW(LoadIdentity(name,false,RegtestNetwork()),std::runtime_error);
    RemoveTemporaries(dir);
    BOOST_CHECK(LoadIdentity(name,true,RegtestNetwork()));
}
BOOST_AUTO_TEST_CASE(partial_worker_startup_is_joined)
{
    for (int failed : {0,1}) {
        WorkerFaults faults; faults.fail_create = failed;
        { ScopedWorkers scope(faults); BOOST_CHECK_THROW((void)Context{RegtestNetwork()},std::runtime_error); }
        BOOST_CHECK_EQUAL(faults.creates,unsigned(failed+1)); BOOST_CHECK_EQUAL(faults.joins,unsigned(failed));
    }
    auto context = std::make_shared<Context>(RegtestNetwork()); context->Stop(); context->Stop();
    BOOST_CHECK(!context->Admit("stopped")); BOOST_CHECK_EQUAL(context->Pending(),0U);
}
BOOST_AUTO_TEST_CASE(cancel_before_execution_and_destroy_pending_transport)
{
    Gate gate; Hooks hooks;
    hooks.point = [&](Event event, Work*) { if (event == Event::BEFORE_CALCULATE) gate.Hold(); };
    ScopedHooks scope(hooks); auto context = std::make_shared<Context>(RegtestNetwork());
    Cleanup cleanup{[&] { gate.Release(); context->Stop(); }};
    auto transport = std::make_unique<PQTransport>(context,true,std::nullopt,"cancelled");
    BOOST_REQUIRE(gate.Wait()); auto job = Access::Job(*transport); BOOST_REQUIRE(job);
    transport->Disconnect(); transport->Disconnect(); NoAssurance(*transport); transport.reset();
    gate.Release(); WaitDone(job); BOOST_CHECK(!job->ok); BOOST_CHECK(!job->handshake); BOOST_CHECK(job->response.empty());
    job.reset(); context->Stop(); BOOST_CHECK_EQUAL(context->Pending(),0U);
}
BOOST_AUTO_TEST_CASE(cancelled_worker_keeps_ephemeral_identity_alive_until_cleanup)
{
    Gate gate; Hooks hooks; std::weak_ptr<const Identity> identity;
    std::atomic<const void*> watched{nullptr}; std::atomic<unsigned> erased{0}; std::atomic<bool> zero{true};
    hooks.point = [&](Event event, Work* job) {
        if (event == Event::AFTER_CALCULATE && job->handshake) {
            identity = Access::IdentityOwner(*job->handshake);
            watched = Access::PrivateIdentity(*job->handshake);
            gate.Hold();
        }
    };
    hooks.cleansed = [&](const void* ptr,size_t size) {
        if (ptr == watched.load() && size == 4032) {
            ++erased;
            if (!std::ranges::all_of(std::span{static_cast<const uint8_t*>(ptr),size},[](auto b) { return b == 0; })) zero=false;
        }
    };
    ScopedHooks scope(hooks); auto context=std::make_shared<Context>(RegtestNetwork());
    Cleanup cleanup{[&] { gate.Release(); context->Stop(); }};
    auto transport=std::make_unique<PQTransport>(context,true,std::nullopt,"owned-job");
    BOOST_REQUIRE(gate.Wait()); auto job=Access::Job(*transport); BOOST_REQUIRE(job);
    BOOST_CHECK(!identity.expired()); transport.reset();
    BOOST_CHECK(job->cancelled.load()); BOOST_CHECK(!identity.expired()); BOOST_CHECK_EQUAL(erased.load(),0U);
    gate.Release(); WaitDone(job); BOOST_CHECK(!job->ok); BOOST_CHECK(!job->handshake);
    BOOST_CHECK(identity.expired()); BOOST_CHECK_EQUAL(erased.load(),1U); BOOST_CHECK(zero.load());
    job.reset(); context->Stop(); BOOST_CHECK_EQUAL(context->Pending(),0U);
}
BOOST_AUTO_TEST_CASE(stale_completed_generation_cannot_publish_authentication)
{
    Gate gate; Hooks hooks;
    hooks.point = [&](Event event, Work* job) { if (event == Event::BEFORE_PUBLISH && job->handshake && job->handshake->Ready()) gate.Hold(); };
    ScopedHooks scope(hooks); auto context = std::make_shared<Context>(RegtestNetwork());
    Cleanup cleanup{[&] { gate.Release(); context->Stop(); }};
    PQTransport i(context,true,std::nullopt,"initiator"), r(context,false,std::nullopt,"responder");
    Drive(i,r,[&] { return gate.Hit(); }); BOOST_REQUIRE(gate.Wait());
    auto job = Access::Job(i); BOOST_REQUIRE(job); Access::NextGeneration(i); gate.Release(); WaitDone(job);
    BOOST_CHECK(!i.Poll()); NoAssurance(i); r.Disconnect(); job.reset(); context->Stop(); BOOST_CHECK_EQUAL(context->Pending(),0U);
}
BOOST_AUTO_TEST_CASE(worker_exception_then_recovery)
{
    std::atomic<unsigned> calls{0}; Hooks hooks;
    hooks.point = [&](Event event, Work*) { if (event == Event::BEFORE_CALCULATE && calls.fetch_add(1)==0) throw std::runtime_error("synthetic worker dependency failure"); };
    ScopedHooks scope(hooks); auto context = std::make_shared<Context>(RegtestNetwork());
    {
        PQTransport failed(context,true,std::nullopt,"failed"); auto job = Access::Job(failed); BOOST_REQUIRE(job);
        WaitDone(job); BOOST_CHECK(!failed.Poll()); NoAssurance(failed);
    }
    BOOST_CHECK_EQUAL(context->Pending(),0U);
    PQTransport i(context,true,std::nullopt,"good-i"), r(context,false,std::nullopt,"good-r");
    Drive(i,r,[&] { return i.GetInfo().pq_encrypted && r.GetInfo().pq_encrypted; });
    i.Disconnect(); r.Disconnect(); context->Stop(); BOOST_CHECK_EQUAL(context->Pending(),0U);
}
BOOST_AUTO_TEST_CASE(failed_worker_discards_owned_secrets_before_publication)
{
    auto context=std::make_shared<Context>(RegtestNetwork());
    auto identity=std::make_shared<Identity>(); BOOST_REQUIRE(identity->FromSeed(std::array<uint8_t,32>{}));
    std::weak_ptr<const Identity> owner=identity;
    unsigned draws{0};
    auto rng=[&](std::span<uint8_t> bytes) {
        if (++draws==2) throw std::runtime_error("synthetic nonce RNG failure after KEM generation");
        std::fill(bytes.begin(),bytes.end(),42); return true;
    };
    auto job=std::make_shared<Work>(1); job->initiator=true;
    job->admission=context->Admit("failed-secret-owner");
    job->handshake=std::make_unique<Handshake>(true,RegtestNetwork(),std::nullopt,identity,rng);
    identity.reset(); BOOST_REQUIRE(context->Submit(job)); WaitDone(job);
    BOOST_CHECK(!job->ok); BOOST_CHECK(!job->handshake); BOOST_CHECK(job->response.empty());
    BOOST_CHECK(!job->admission); BOOST_CHECK(owner.expired()); BOOST_CHECK_EQUAL(context->Pending(),0U);
    context->Stop();
}
BOOST_AUTO_TEST_CASE(stop_discards_queued_handshake_even_with_external_job_owner)
{
    Gate gate,joining; Hooks hooks;
    hooks.point=[&](Event event,Work*) { if (event==Event::BEFORE_CALCULATE) gate.Hold(); };
    hooks.joining=[&] { joining.Release(); };
    ScopedHooks scope(hooks); auto context=std::make_shared<Context>(RegtestNetwork());
    Cleanup cleanup{[&] { gate.Release(); context->Stop(); }};
    PQTransport first(context,true,std::nullopt,"busy-a"),second(context,true,std::nullopt,"busy-b");
    BOOST_REQUIRE(gate.Wait(2));
    auto identity=std::make_shared<Identity>(); BOOST_REQUIRE(identity->FromSeed(std::array<uint8_t,32>{}));
    std::weak_ptr<const Identity> owner=identity;
    auto rng=[](std::span<uint8_t> out) { std::fill(out.begin(),out.end(),42); return true; };
    auto job=std::make_shared<Work>(1); job->admission=context->Admit("queued-secret-owner");
    job->handshake=std::make_unique<Handshake>(true,RegtestNetwork(),std::nullopt,identity,rng);
    BOOST_REQUIRE(job->handshake->Start(job->response)); identity.reset();
    BOOST_REQUIRE(context->Submit(job));
    auto stopping=std::async(std::launch::async,[&] { context->Stop(); });
    { std::unique_lock lock(joining.mutex); BOOST_CHECK(joining.cv.wait_for(lock,3s,[&] { return joining.released; })); }
    gate.Release(); stopping.get();
    BOOST_CHECK(job->cancelled.load()); BOOST_CHECK(job->done.load()); BOOST_CHECK(!job->ok);
    BOOST_CHECK(!job->handshake); BOOST_CHECK(job->response.empty()); BOOST_CHECK(!job->admission); BOOST_CHECK(owner.expired());
    first.Disconnect(); second.Disconnect(); BOOST_CHECK_EQUAL(context->Pending(),0U);
}
BOOST_AUTO_TEST_CASE(stop_prevents_late_completed_result_from_authenticating)
{
    Gate gate,joining; Hooks hooks;
    hooks.point=[&](Event event,Work* job) { if (event==Event::BEFORE_PUBLISH && job->handshake && job->handshake->Ready()) gate.Hold(); };
    hooks.joining=[&] { joining.Release(); };
    ScopedHooks scope(hooks); auto context=std::make_shared<Context>(RegtestNetwork());
    Cleanup cleanup{[&] { gate.Release(); context->Stop(); }};
    PQTransport i(context,true,std::nullopt,"late-i"),r(context,false,std::nullopt,"late-r");
    Drive(i,r,[&] { return gate.Hit(); }); BOOST_REQUIRE(gate.Wait());
    auto job=Access::Job(i); BOOST_REQUIRE(job);
    auto stopping=std::async(std::launch::async,[&] { context->Stop(); });
    { std::unique_lock lock(joining.mutex); BOOST_CHECK(joining.cv.wait_for(lock,3s,[&] { return joining.released; })); }
    gate.Release(); stopping.get(); WaitDone(job);
    // A completed M4 must not install keys after the context has stopped.
    const bool polled=i.Poll();
    if (polled) { const auto [bytes,more,type]=i.GetBytesToSend(false); i.MarkBytesSent(bytes.size()); }
    BOOST_CHECK(!polled); NoAssurance(i); BOOST_CHECK(std::get<0>(i.GetBytesToSend(false)).empty());
    i.Disconnect(); r.Disconnect(); job.reset(); BOOST_CHECK_EQUAL(context->Pending(),0U);
}
BOOST_AUTO_TEST_CASE(stop_revokes_established_transport_assurance)
{
    auto context=std::make_shared<Context>(RegtestNetwork());
    PQTransport i(context,true,std::nullopt,"stop-i"),r(context,false,std::nullopt,"stop-r");
    Drive(i,r,[&] { return i.GetInfo().pq_encrypted && r.GetInfo().pq_encrypted; });
    context->Stop(); NoAssurance(i); NoAssurance(r);
    CSerializedNetMsg message; message.m_type="ping"; message.data.resize(8);
    BOOST_CHECK(!i.SetMessageToSend(message)); BOOST_CHECK(!i.Poll()); BOOST_CHECK(!r.Poll());
    i.Disconnect(); r.Disconnect(); BOOST_CHECK_EQUAL(context->Pending(),0U);
}
BOOST_AUTO_TEST_CASE(concurrent_stop_serializes_worker_joins)
{
    Gate join_gate,second_entered; std::atomic<unsigned> joins{0}; Hooks hooks;
    hooks.joining=[&] { if (++joins==1) join_gate.Hold(); };
    ScopedHooks scope(hooks); auto context=std::make_shared<Context>(RegtestNetwork());
    // Hold the first Stop before its first join. The second Stop must remain
    // serialized behind it, rather than touching either worker's join state.
    auto first=std::async(std::launch::async,[&] { context->Stop(); });
    const bool waiting=join_gate.Wait();
    auto second=std::async(std::launch::async,[&] { second_entered.Release(); context->Stop(); });
    { std::unique_lock lock(second_entered.mutex); second_entered.cv.wait(lock,[&] { return second_entered.released; }); }
    const bool serialized=second.wait_for(100ms)==std::future_status::timeout;
    join_gate.Release(); first.get(); second.get();
    BOOST_CHECK(waiting); BOOST_CHECK(serialized); BOOST_CHECK_EQUAL(joins.load(),2U);
    context->Stop(); BOOST_CHECK_EQUAL(joins.load(),2U); BOOST_CHECK_EQUAL(context->Pending(),0U);
}
BOOST_AUTO_TEST_CASE(shared_persistent_identity_concurrent_sessions_are_isolated)
{
    auto identity = std::make_shared<Identity>(); BOOST_REQUIRE(identity->FromSeed(std::array<uint8_t,32>{}));
    const auto fingerprint = Fingerprint(RegtestNetwork(),identity->pub);
    std::weak_ptr<const Identity> weak_identity = identity;
    const void* private_bytes = identity->priv.bytes.data(); std::atomic<unsigned> erased{0}; std::atomic<bool> zero{true};
    Hooks hooks; hooks.cleansed = [&](const void* ptr,size_t size) {
        if (ptr==private_bytes && size==4032) { ++erased; if (!std::ranges::all_of(std::span{static_cast<const uint8_t*>(ptr),size},[](auto b) { return b==0; })) zero=false; }
    };
    ScopedHooks scope(hooks);
    auto context = std::make_shared<Context>(RegtestNetwork(), identity); identity.reset();
    {
        std::array<std::unique_ptr<PQTransport>,4> i,r;
        constexpr std::array<const char*,4> sources_i{"i-a","i-b","i-c","i-d"}, sources_r{"r-a","r-b","r-c","r-d"};
        for (unsigned n=0;n<4;++n) { i[n]=std::make_unique<PQTransport>(context,true,fingerprint,sources_i[n]); r[n]=std::make_unique<PQTransport>(context,false,fingerprint,sources_r[n]); }
        const auto deadline = Clock::now()+5s;
        for (;;) {
            bool ready=true; BOOST_REQUIRE(Clock::now()<deadline);
            for (unsigned n=0;n<4;++n) { BOOST_REQUIRE(i[n]->Poll()); BOOST_REQUIRE(r[n]->Poll()); Transfer(*i[n],*r[n]); Transfer(*r[n],*i[n]); ready &= i[n]->GetInfo().pq_encrypted && r[n]->GetInfo().pq_encrypted; }
            if (ready) break;
            std::this_thread::sleep_for(1ms);
        }
        for (unsigned n=0;n<4;++n) { BOOST_CHECK(i[n]->GetInfo().pq_identity_verified && r[n]->GetInfo().pq_identity_verified); for (unsigned k=0;k<n;++k) BOOST_CHECK(i[n]->GetInfo().session_id != i[k]->GetInfo().session_id); }
        i[0]->Disconnect(); r[0]->Disconnect();
        for (unsigned n=1;n<4;++n) BOOST_CHECK(i[n]->GetInfo().pq_encrypted && r[n]->GetInfo().pq_encrypted);
        BOOST_CHECK(!weak_identity.expired()); BOOST_CHECK_EQUAL(erased.load(),0U);
    }
    context->Stop(); context.reset(); BOOST_CHECK(weak_identity.expired()); BOOST_CHECK_EQUAL(erased.load(),1U); BOOST_CHECK(zero.load());
}
BOOST_AUTO_TEST_CASE(handshake_failures_release_identity_and_secret_state)
{
    static_assert(!std::is_copy_constructible_v<Secret<32>>);
    static_assert(!std::is_copy_constructible_v<Identity>);
    static_assert(!std::is_copy_constructible_v<PacketCipher>);
    auto identity = std::make_shared<Identity>(); BOOST_REQUIRE(identity->FromSeed(std::array<uint8_t,32>{}));
    auto rng = [](std::span<uint8_t> out) { std::fill(out.begin(),out.end(),42); return true; };
    for (unsigned stage=1;stage<=5;++stage) {
        Handshake i(true,RegtestNetwork(),{},identity,rng), r(false,RegtestNetwork(),{},identity,rng);
        Bytes wire,next; BOOST_REQUIRE(i.Start(wire)); wire.erase(wire.begin(),wire.begin()+16);
        for (unsigned type=1;type<stage;++type) {
            BOOST_REQUIRE((type%2 ? r : i).Advance(wire,next)); wire=std::move(next);
            if (type==1) wire.erase(wire.begin(),wire.begin()+16);
        }
        wire.back() ^= 1;
        auto& receiver = stage%2 ? r : i;
        // Malformed exact framing is a cleanup trigger at every handshake stage.
        wire.pop_back(); BOOST_CHECK(!receiver.Advance(wire,next)); BOOST_CHECK(!receiver.Ready());
        BOOST_CHECK(Access::SecretsCleared(receiver));
    }
    {
        Secret<32> secret; const void* watched=secret.bytes.data(); std::atomic<unsigned> cleared{0};
        Hooks hooks; hooks.cleansed=[&](const void* p,size_t size) { if (p==watched && size==32) { BOOST_CHECK(std::ranges::all_of(secret.bytes,[](auto b) { return b==0; })); ++cleared; } };
        ScopedHooks scope(hooks); secret.bytes.fill(42); secret.Clear(); BOOST_CHECK_EQUAL(cleared.load(),1U);
        auto& alias = secret;
        secret=std::move(alias); BOOST_CHECK(std::ranges::all_of(secret.bytes,[](auto b) { return b==0; }));
    }
}
BOOST_AUTO_TEST_SUITE_END()
