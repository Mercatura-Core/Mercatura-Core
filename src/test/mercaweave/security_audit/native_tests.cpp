// Actual production components, ordinary native ABI, synthetic private wallets.
#include "../production_path/native_fixture.h"
#include "allocation.h"
#include <script/pqgroup_psbt.h>
#include <sqlite3.h>
#include <iostream>
#include <atomic>
#include <random>
#include <thread>

using namespace mercaweave;
using namespace mercaweave::production_test;
BOOST_FIXTURE_TEST_SUITE(pqgroup_final_security,NativeSetup)
BOOST_AUTO_TEST_CASE(no_allocation_after_sqlite_issuance_commit)
{
    const auto path=m_path_root/"allocation-wallet";
    fs::create_directories(path);
    wallet::DatabaseOptions options; options.use_unsafe_sync=false;
    auto database=std::make_unique<wallet::SQLiteDatabase>(path,path/"wallet.dat",options);
    auto* sql=database.get();
    auto owner=Wallet("allocation-owner",0x73,std::move(database));
    // Install after seed initialization. SQLite invokes this immediately before
    // its commit; C++ allocations after durable commit must be unnecessary.
    sqlite3_commit_hook(sql->m_db,[](void*) -> int { audit::FailAfter(0); return 0; },nullptr);
    bool threw{false}; std::optional<wallet::PQGroupRecord> issued;
    try { issued=ProductionPathTestAccess::Issue(*owner); }
    catch (const std::bad_alloc&) { threw=true; }
    audit::Disarm(); sqlite3_commit_hook(sql->m_db,nullptr,nullptr);
    const auto records=ProductionPathTestAccess::Records(*owner);
    const auto scripts=owner->GetPQGroupScripts();
    std::cout << "WO08_JSON {\"scenario\":\"allocation-after-sqlite-commit\",\"threw\":" << threw
              << ",\"records\":" << records.issued.size() << ",\"ownership_entries\":" << scripts.size() << "}\n";
    BOOST_CHECK(!threw);
    BOOST_REQUIRE(issued);
    BOOST_REQUIRE_EQUAL(records.issued.size(),1);
    BOOST_REQUIRE_EQUAL(scripts.size(),1);
    BOOST_CHECK(owner->GetPQGroupRecord(issued->Script(Genesis())).has_value());
    auto batch=owner->GetDatabase().MakeBatch(); wallet::PQGroupRecords disk;
    BOOST_REQUIRE(batch->Read(std::string{"mercaturapqgrouprecords"},disk));
    BOOST_CHECK(SerializeBytes(disk)==SerializeBytes(records));
}

BOOST_AUTO_TEST_CASE(owned_context_allocation_and_parallel_jobs)
{
    auto fixture=Fixture(12,3,1001);
    const CTransaction tx(fixture.tx); auto execution=Execution();
    const auto genesis=uint256{std::span<const unsigned char>{Genesis()}};
    size_t faults{0}, successes{0};
    for (size_t point=0; point<256; ++point) {
        std::shared_ptr<const GroupContext> context;
        std::optional<Error> error;
        try {
            audit::FailAfter(point);
            context=GroupContext::Prepare(*execution,tx,fixture.spent,STANDARD_SCRIPT_VERIFY_FLAGS,genesis,error);
            audit::Disarm();
        } catch (const std::bad_alloc&) { audit::Disarm(); ++faults; }
        if (context) { ++successes; BOOST_CHECK(context->Matches(tx,fixture.spent,STANDARD_SCRIPT_VERIFY_FLAGS,genesis)); }
        else BOOST_CHECK(audit::Fired());
    }
    BOOST_CHECK(faults>0); BOOST_CHECK(successes>0);
    std::optional<Error> error;
    auto context=GroupContext::Prepare(*execution,tx,fixture.spent,STANDARD_SCRIPT_VERIFY_FLAGS,genesis,error);
    BOOST_REQUIRE(context);
    ValidationCache cache(1<<20,1<<20);
    std::atomic<size_t> checks{0}, failures{0};
    std::vector<std::thread> workers;
    for (int worker=0; worker<4; ++worker) workers.emplace_back([&] {
        for (size_t repeat=0; repeat<50; ++repeat) {
            for (size_t i=0; i<fixture.spent.size(); ++i) {
                CScriptCheck job(context->Spent()[i],context->Transaction(),cache.m_signature_cache,i,
                    STANDARD_SCRIPT_VERIFY_FLAGS,false,&context->Native(),genesis,context);
                if (job()) ++failures;
                ++checks;
            }
        }
    });
    for (auto& worker:workers) worker.join();
    BOOST_CHECK_EQUAL(failures.load(),0); BOOST_CHECK_EQUAL(checks.load(),2400);
    std::cout << "WO08_JSON {\"scenario\":\"context-allocation-and-concurrency\",\"allocation_points\":256,\"faults\":"
              << faults << ",\"successes\":" << successes << ",\"parallel_jobs\":" << checks << "}\n";
}

BOOST_AUTO_TEST_CASE(wallet_psbt_allocation_and_concurrent_publication)
{
    auto owner=Wallet("audit-psbt",0x74);
    auto issued=ProductionPathTestAccess::Issue(*owner); BOOST_REQUIRE(issued);
    CMutableTransaction tx; B32 point{}; point.fill(0x86);
    tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256{std::span<const unsigned char>{point}}),0});
    tx.vout.emplace_back(900,issued->Script(Genesis()));
    std::vector<CTxOut> spent{{1000,issued->Script(Genesis())}};
    { LOCK(cs_main); State().CoinsTip().AddCoin(tx.vin[0].prevout,Coin{CTxOut(spent[0]),1,false},false); }
    std::vector<psbt::InputRecord> records{{issued->pk,issued->nonce,3}};
    auto unsigned_psbt=psbt::Create(tx,spent,records,*Execution());
    const auto original=SerializeBytes(unsigned_psbt);
    ProductionPathTestAccess::Entropy(*owner,test::ZeroRandomnessForTestsOnly);
    size_t faults{0}, completed{0};
#ifdef DEBUG_LOCKORDER
    // The inherited debug lock tracker allocates while entering and leaving
    // critical sections and is not exception-safe under global new faults.
    // Keep its checks enabled; exercise wallet faults in Release/ASan/TSAN,
    // and actual signing/concurrency plus lock-free PSBT faults in Debug.
    constexpr size_t fill_points{0};
#else
    constexpr size_t fill_points{512};
#endif
    for (size_t point=0; point<fill_points; ++point) {
        auto psbt=unsigned_psbt; bool complete{false}; bool threw{false};
        std::cout << "WO08_ALLOC_POINT fill " << point << std::endl;
        try { audit::FailAfter(point); owner->FillPSBT(psbt,complete,SIGHASH_DEFAULT,true,false,nullptr,true); audit::Disarm(); }
        catch (const std::bad_alloc&) { audit::Disarm(); threw=true; ++faults; }
        if (threw) { BOOST_CHECK(!complete); BOOST_CHECK(SerializeBytes(psbt)==original); }
        if (complete) ++completed;
    }
    if (fill_points) { BOOST_CHECK(faults>0); BOOST_CHECK(completed>0); }
    auto signed_psbt=unsigned_psbt; bool complete{false};
    BOOST_REQUIRE(!owner->FillPSBT(signed_psbt,complete,SIGHASH_DEFAULT,true,false)); BOOST_REQUIRE(complete);
    size_t finalizer_faults{0}, combine_faults{0};
    for (size_t point=0; point<512; ++point) {
        auto psbt=signed_psbt; auto before=SerializeBytes(psbt);
        try { audit::FailAfter(point); FinalizePSBT(psbt,*Execution(),spent); audit::Disarm(); }
        catch (const std::bad_alloc&) { audit::Disarm(); ++finalizer_faults; BOOST_CHECK(SerializeBytes(psbt)==before); }
        PartiallySignedTransaction merged=unsigned_psbt;
        const std::vector<PartiallySignedTransaction> sources{signed_psbt,unsigned_psbt};
        try { audit::FailAfter(point); CombinePSBTs(merged,sources,*Execution(),spent); audit::Disarm(); }
        catch (const std::bad_alloc&) { audit::Disarm(); ++combine_faults; BOOST_CHECK(SerializeBytes(merged)==original); }
    }
    std::atomic<size_t> failures{0}; std::vector<std::thread> threads;
    for (int worker=0; worker<4; ++worker) threads.emplace_back([&] {
        for (int repeat=0; repeat<8; ++repeat) {
            auto psbt=unsigned_psbt; bool done{false};
            if (owner->FillPSBT(psbt,done,SIGHASH_DEFAULT,true,false) || !done) ++failures;
            auto address=ProductionPathTestAccess::Issue(*owner);
            if (!address || !owner->GetPQGroupRecord(address->Script(Genesis()))) ++failures;
        }
    });
    for (auto& thread:threads) thread.join();
    BOOST_CHECK_EQUAL(failures.load(),0);
    BOOST_CHECK_EQUAL(owner->GetPQGroupScripts().size(),33);
    std::cout << "WO08_JSON {\"scenario\":\"actual-wallet-psbt-allocation\",\"psbt_points_per_operation\":512,\"fill_points\":"
              << fill_points << ",\"fill_faults\":"
              << faults << ",\"fill_complete\":" << completed << ",\"finalizer_faults\":" << finalizer_faults
              << ",\"combine_faults\":" << combine_faults << ",\"parallel_wallet_rounds\":32}\n";
}

BOOST_AUTO_TEST_CASE(independent_vectors_and_bounded_mutations)
{
    for (const auto& [inputs,groups]:std::vector<std::pair<size_t,size_t>>{{2,1},{7,3},{10,1},{100,10}}) {
        auto f=test::Make(inputs,groups); test::SignFixture(f);
        const auto map=Reconstruct(f.tx,f.spent); const auto h=TransactionHashes(f.tx,f.spent,f.genesis,map);
        std::cout << "WO08_VECTOR {\"inputs\":" << inputs << ",\"groups\":" << groups
                  << ",\"genesis\":\"" << test::Hex(f.genesis) << "\",\"wire\":\"" << test::Hex(Wire(f.tx))
                  << "\",\"spent\":\"" << test::Hex(SerializeBytes(f.spent)) << "\",\"manifest\":\""
                  << test::Hex(Manifest(map)) << "\",\"hashes\":[";
        bool first=true;
        for (const auto& hash:{h.prev,h.amount,h.script,h.sequence,h.output,h.tx,h.manifest}) {
            if (!first) std::cout << ',';
            first=false; std::cout << '"' << test::Hex(hash) << '"';
        }
        std::cout << "],\"digests\":["; first=true;
        for (const auto& group:map.groups) {
            if (!first) std::cout << ',';
            first=false; std::cout << '"' << test::Hex(Digest(f.genesis,h,group)) << '"';
        }
        std::cout << "]}\n";
    }
    auto f=Fixture(7,3,1002);
    std::mt19937_64 random{0x574f303841554449ULL};
    size_t rejects{0};
    for (size_t iteration=0; iteration<1024; ++iteration) {
        auto tx=f.tx; auto spent=f.spent;
        switch (iteration%8) {
        case 0: tx.vin[random()%3].scriptWitness.stack[0][random()%3309]^=1; break;
        case 1: tx.vin[random()%3].scriptWitness.stack[1][random()%1952]^=1; break;
        case 2: tx.vin[3+random()%4].scriptWitness.stack[0][random()%32]^=1; break;
        case 3: --spent[random()%7].nValue; break;
        case 4: --tx.vin[random()%7].nSequence; break;
        case 5: ++tx.vin[random()%7].prevout.n; break;
        case 6: --tx.vout[0].nValue; break;
        default: ++tx.nLockTime; break;
        }
        std::optional<Error> error;
        auto context=GroupContext::Prepare(*Execution(),CTransaction(tx),spent,STANDARD_SCRIPT_VERIFY_FLAGS,
            uint256{std::span<const unsigned char>{Genesis()}},error);
        BOOST_CHECK(!context); if (!context) ++rejects;
    }
    std::cout << "WO08_JSON {\"scenario\":\"bounded-production-mutations\",\"seed\":\"574f303841554449\",\"cases\":1024,\"rejected\":" << rejects << "}\n";
}

BOOST_AUTO_TEST_CASE(multiblock_reorg_during_actual_wallet_signing)
{
    ProductionPathTestAccess::Attach(State(),Execution());
    auto owner=Wallet("reorg-owner",0x76);
    auto notifications=m_node.chain->handleNotifications(owner);
    auto record=ProductionPathTestAccess::Issue(*owner); BOOST_REQUIRE(record);
    const auto script=record->Script(Genesis());
    B32 source_id{}; source_id.fill(0x71);
    COutPoint source{Txid::FromUint256(uint256{std::span<const unsigned char>{source_id}}),0};
    { LOCK(cs_main); State().CoinsTip().AddCoin(source,Coin{CTxOut{5000,CScript()<<OP_TRUE},1,false},false); }
    CMutableTransaction funding; funding.vin.emplace_back(source); funding.vout.emplace_back(4900,script);
    Connect(Block({funding}));
    const COutPoint funded{CTransaction(funding).GetHash(),0};
    const auto before=DiskState();
    const auto sign=[&](CMutableTransaction& tx,const CTxOut& spent) {
        std::map<COutPoint,Coin> coins{{tx.vin[0].prevout,Coin{CTxOut(spent),1,false}}};
        std::map<int,bilingual_str> errors;
        LOCK(owner->cs_wallet); BOOST_REQUIRE(owner->SignTransaction(tx,coins,SIGHASH_DEFAULT,errors));
    };
    CMutableTransaction first; first.vin.emplace_back(funded); first.vout.emplace_back(4800,script);
    sign(first,funding.vout[0]); const auto block_a=Block({first}); Connect(block_a);
    auto* index_a=WITH_LOCK(cs_main,return m_node.chainman->m_blockman.LookupBlockIndex(block_a.GetHash()));
    CMutableTransaction second; second.vin.emplace_back(COutPoint{CTransaction(first).GetHash(),0});
    second.vout.emplace_back(4700,script); sign(second,first.vout[0]); Connect(Block({second}));
    const auto history=DiskState();
    BOOST_CHECK_EQUAL(wallet::GetBalance(*owner).m_mine_trusted,4700);
    CMutableTransaction third; third.vin.emplace_back(COutPoint{CTransaction(second).GetHash(),0});
    third.vout.emplace_back(4600,script);
    const std::vector<CTxOut> spent{second.vout[0]};
    const std::vector<psbt::InputRecord> records{{record->pk,record->nonce,3}};
    auto psbt=psbt::Create(third,spent,records,*Execution()); const auto original=SerializeBytes(psbt);
    bool reorganized{false};
    ProductionPathTestAccess::Entropy(*owner,[&](B32& out) {
        out.fill(0);
        BlockValidationState state;
        reorganized=State().InvalidateBlock(state,index_a) && State().ActivateBestChain(state);
        return reorganized;
    });
    bool complete{true};
    BOOST_CHECK(owner->FillPSBT(psbt,complete,SIGHASH_DEFAULT,true,false).has_value());
    BOOST_CHECK(reorganized); BOOST_CHECK(!complete); BOOST_CHECK(SerializeBytes(psbt)==original);
    ProductionPathTestAccess::Entropy(*owner,{});
    m_node.chain->waitForNotificationsIfTipChanged({});
    BOOST_CHECK(DiskState()==before);
    BOOST_CHECK(WITH_LOCK(cs_main,return State().CoinsTip().HaveCoin(funded)));
    BOOST_REQUIRE(owner->AbandonTransaction(CTransaction(first).GetHash()));
    BOOST_CHECK_EQUAL(wallet::GetBalance(*owner).m_mine_trusted,4900);
    auto recovered=Reload("reorg-reopened",wallet::DuplicateMockDatabase(owner->GetDatabase()));
    BOOST_CHECK_EQUAL(wallet::GetBalance(*recovered).m_mine_trusted,4900);
    { LOCK(cs_main); State().ResetBlockFailureFlags(index_a); m_node.chainman->RecalculateBestHeader(); }
    BlockValidationState state; BOOST_REQUIRE(State().ActivateBestChain(state));
    m_node.chain->waitForNotificationsIfTipChanged({});
    BOOST_CHECK(DiskState()==history);
    BOOST_CHECK_EQUAL(wallet::GetBalance(*owner).m_mine_trusted,4700);
    std::cout << "WO08_JSON {\"scenario\":\"actual-multiblock-reorg-during-signing\",\"disconnected_blocks\":2,"
                 "\"staged_psbt_unchanged\":true,\"wallet_reopened_after_rollback\":true,\"utxo_exact\":true}\n";
}
BOOST_AUTO_TEST_SUITE_END()
