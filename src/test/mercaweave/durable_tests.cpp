// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <tinyformat.h>
#include <util/fs.h>
#include <test/mercaweave/durable_wallet.h>
#include <test/mercaweave/wallet_fixtures.h>
#include <chainparams.h>
#include <util/translation.h>
#include <sqlite3.h>
#include <sys/wait.h>
#include <unistd.h>
#include <future>
#include <fstream>
#include <iostream>
const TranslateFn G_TRANSLATION_FUN{nullptr};
using namespace mercaweave;
using namespace mercaweave::durable_research;
namespace wt=mercaweave::wallet_research::test;
namespace {
size_t checks{};void Check(bool v,const char* reason){++checks;if(!v)throw std::runtime_error(reason);}
template<class F>void Reject(F f,Code code){bool rejected=false;try{f();}catch(const WalletError& error){rejected=error.code==code;}Check(rejected,"expected wallet failure provenance");}
const SecureString PASS{"public research fixture"};
struct FailCommit:wallet::SQliteExecHandler {int Exec(wallet::SQLiteDatabase& db,const std::string& sql)override {if(sql=="COMMIT TRANSACTION")return SQLITE_IOERR;return SQliteExecHandler::Exec(db,sql);}};
void Run(const char* binary)
{
    const auto name="lifecycle-"+strprintf("%d",getpid());auto owner=wt::Owner();auto group=owner->Issue(0,2);auto native=owner->Issue(0,0,2);auto destination=owner->Issue(7,0,2);
    for(int i=0;i<64;++i)owner->Issue(0,2);const auto records=owner->Records();
    auto store=Store::Create(name,std::move(owner));
    Check(fs::is_regular_file(Store::Root()/fs::PathFromString(name)/"wallet.dat"),"genuine native SQLite database exists");
    Check(!store->DatabaseForTests().m_use_unsafe_sync,"native durability sync enabled");
    auto view=integration::View::Create(wt::Genesis());std::vector<FundingCoin> coins;
    for(uint32_t i=0;i<50;++i){auto p=wt::Point(500+i);view->Insert(p,Coin{CTxOut{10000,group.script},1,true});coins.push_back({p});}
    reward_research::Planner planner;reward_research::Policy policy;policy.enabled=policy.opted_in=policy.isolated=policy.network=policy.loaded=policy.synchronized=policy.signer=true;policy.destination=destination.script;
    auto decision=planner.Evaluate(store->Owner(),view,coins,policy);Check(decision.proposal.has_value(),"50 mature rewards form actual funded proposal");
    auto reserved=store->Reserve(*decision.proposal);Check(reserved.id==1&&reserved.inputs.size()==50,"reservation durable before caller receives it");
    const auto prior_journal=store->Entries();Reject([&]{store->Mark(1,static_cast<Status>(255));},Code::Metadata);
    Check(store->Entries()==prior_journal,"unknown status cannot replace a restorable durable checkpoint");
    std::vector<std::future<uint64_t>> tasks;for(int i=0;i<8;++i)tasks.push_back(std::async(std::launch::async,[&]{return store->Reserve(*decision.proposal).id;}));for(auto& task:tasks)Check(task.get()==1,"concurrent timer ticks idempotent in durable journal");
    auto conflicting=*decision.proposal;conflicting.transactions[0].psbt.tx->vout[0].nValue--;
    Reject([&]{store->Reserve(conflicting);},Code::Conflict);
    auto psbt=decision.proposal->transactions[0].psbt;
    store.reset();store=Store::Load(name,wt::Genesis());Check(store->Owner().IsLocked(),"native database restart never unlocks");
    Reject([&]{store->Entries();},Code::Locked);Reject([&]{store->Checkpoint();},Code::Metadata);
    Check(!store->Unlock(SecureString{"wrong"})&&store->Owner().IsLocked(),"wrong password never authorizes journal");
    Check(store->Unlock(PASS)&&store->Owner().Records()==records,"encrypted seed and rotation metadata recover from actual DB");
    Check(store->Reserve(*decision.proposal).id==reserved.id&&store->Entries().size()==1,"restart does not reschedule prior identity");
    GroupPSBT::Sign(psbt,store->Owner(),view,wt::ZeroFixture());GroupPSBT::FinalizeAvailable(psbt,view,wt::Genesis());auto signed_tx=GroupPSBT::Extract(psbt,view,wt::Genesis());
    Check(integration::Engine::Validate(view,signed_tx,wt::Genesis(),STANDARD_SCRIPT_VERIFY_FLAGS).valid,"offline PSBT survives actual DB close and reload");
    store.reset();store=Store::Load(name,wt::Genesis());Check(store->Unlock(PASS)&&store->Entries()[0].status==Status::Reserved,"crash window after explicit signing preserves unsigned body reservation");
    store.reset();const auto accepting=fork();Check(accepting>=0,"fork acceptance writer");
    if(accepting==0){execl(binary,binary,"--crash-after-accept",name.c_str(),nullptr);_exit(127);}
    int accepted_status;Check(waitpid(accepting,&accepted_status,0)==accepting&&WIFEXITED(accepted_status)&&WEXITSTATUS(accepted_status)==95,"abrupt process exits after committed simulated acceptance");
    store=Store::Load(name,wt::Genesis());Check(store->Unlock(PASS)&&store->Entries()[0].status==Status::SimulatedAccepted,"simulated acceptance retains durable identity and reservations");
    Check(store->Reserve(*decision.proposal).id==1,"acceptance/restart cannot duplicate same candidate");
    view->Spend(*reserved.inputs.begin());store->Reconcile(view);Check(store->Entries()[0].status==Status::Conflicted,"actual spent outpoint retires stale reservation");
    Check(store->Reserve(*decision.proposal).status==Status::Conflicted,"reorg cannot revive retired body automatically");
    Reject([&]{GroupPSBT::Extract(psbt,view,wt::Genesis());},Code::Inputs);
    store->Owner().Lock();Reject([&]{store->Reserve(*decision.proposal);},Code::Locked);Check(store->Unlock(PASS),"explicit unlock recovers safe state");
    const auto backup=Store::Root()/fs::PathFromString(name+".backup");Check(store->Backup(backup),"native SQLite online backup");
    // Real native transaction abort: no application-level mock persistence.
    {auto batch=store->DatabaseForTests().MakeBatch();Check(batch->TxnBegin(),"begin interrupted update");Check(batch->Write(std::string{"mercaweave/wo07/state"},Bytes{0xff}),"write uncommitted corruption");}
    store.reset();store=Store::Load(name,wt::Genesis());Check(store->Unlock(PASS)&&store->Entries().size()==1,"batch destruction rolls interrupted write back");
    {wallet::SQLiteBatch batch{store->DatabaseForTests()};Check(batch.TxnBegin(),"begin injected failing commit");Check(batch.Write(std::string{"mercaweave/wo07/state"},Bytes{0xff}),"write before failing commit");batch.SetExecHandler(std::make_unique<FailCommit>());Check(!batch.TxnCommit(),"native commit error propagated");}
    store.reset();store=Store::Load(name,wt::Genesis());Check(store->Unlock(PASS),"failed native commit leaves prior checkpoint recoverable");
    store.reset();
    const auto child=fork();Check(child>=0,"fork abrupt writer");if(child==0){execl(binary,binary,"--crash",name.c_str(),nullptr);_exit(127);}int status;Check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==96,"abrupt process exits before SQLite commit");
    store=Store::Load(name,wt::Genesis());Check(store->Unlock(PASS)&&store->Entries()[0].status==Status::Conflicted,"native journal recovery survives actual process crash");
    store->Owner().Issue(0,2);store->Checkpoint();const auto issued=store->Owner().Records();store.reset();store=Store::Load(name,wt::Genesis());Check(store->Unlock(PASS)&&store->Owner().Records()==issued,"post-crash issuance cursor advances atomically");
    // Authentication failure after a structurally valid checkpoint mutation.
    auto batch=store->DatabaseForTests().MakeBatch();Bytes raw;Check(batch->Read(std::string{"mercaweave/wo07/state"},raw),"read authenticated fixture");raw.back()^=1;Check(batch->Write(std::string{"mercaweave/wo07/state"},raw),"inject journal MAC corruption");batch.reset();store.reset();
    store=Store::Load(name,wt::Genesis());Check(!store->Unlock(PASS)&&store->Owner().IsLocked(),"corrupt journal fails closed and clears unlocked seed");store.reset();
    const auto restored=name+"-backup";fs::create_directory(Store::Root()/fs::PathFromString(restored));fs::copy_file(backup,Store::Root()/fs::PathFromString(restored)/"wallet.dat",fs::copy_options::none);store=Store::Load(restored,wt::Genesis());Check(store->Unlock(PASS)&&store->Owner().Records()==records,"native backup restores encrypted ownership and durable reservations");store.reset();
    auto watch=Store::Create(name+"-watch",Wallet::Watch(wt::Genesis(),records));watch.reset();watch=Store::Load(name+"-watch",wt::Genesis());
    Check(watch->Owner().IsWatchOnly()&&watch->Owner().Records()==records&&!watch->Unlock(PASS),"actual watch-only SQLite reload has public metadata and no seed");
    Reject([&]{watch->Reserve(*decision.proposal);},Code::Locked);watch->Checkpoint();watch.reset();fs::remove_all(Store::Root()/fs::PathFromString(name+"-watch"));
    Reject([&]{Store::Load("../outside",wt::Genesis());},Code::Metadata);
    auto link=Store::Root()/fs::PathFromString(name+"-link");fs::create_directory_symlink(Store::Root()/fs::PathFromString(name),link);
    Reject([&]{Store::Load(name+"-link",wt::Genesis());},Code::Metadata);fs::remove(link);
    const auto parked=fs::PathFromString(fs::PathToString(Store::Root())+"-parked-"+strprintf("%d",getpid()));
    const auto target=fs::PathFromString(fs::PathToString(Store::Root())+"-target-"+strprintf("%d",getpid()));
    fs::create_directory(target);fs::rename(Store::Root(),parked);fs::create_directory_symlink(target,Store::Root());
    bool rejected_root=false;try{Store::Create(name+"-escape",wt::Owner());}catch(const WalletError& e){rejected_root=e.code==Code::Metadata;}
    fs::remove(Store::Root());fs::rename(parked,Store::Root());
    Check(rejected_root&&fs::is_empty(target),"root symlink rejected before creating any directory or file");fs::remove(target);
    fs::remove_all(Store::Root()/fs::PathFromString(name));fs::remove_all(Store::Root()/fs::PathFromString(restored));fs::remove(backup);
}
}
int main(int argc,char** argv)
{
    try{SelectParams(ChainType::REGTEST);
        if(argc==3&&std::string_view{argv[1]}=="--crash-after-accept"){auto store=Store::Load(argv[2],wt::Genesis());if(!store->Unlock(PASS))return 97;store->Mark(1,Status::SimulatedAccepted);_exit(95);}
        if(argc==3&&std::string_view{argv[1]}=="--crash") {auto store=Store::Load(argv[2],wt::Genesis());auto batch=store->DatabaseForTests().MakeBatch();if(!batch->TxnBegin()||!batch->Write(std::string{"mercaweave/wo07/state"},Bytes{0xff}))return 97;_exit(96);}
        Run(fs::absolute(argv[0]).c_str());std::cout<<"{\"passed\":true,\"assertions\":"<<checks<<",\"native_sqlite\":true,\"abrupt_process_crash\":true,\"full_cwallet_v3\":false,\"automatic_sign_or_broadcast\":false}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
