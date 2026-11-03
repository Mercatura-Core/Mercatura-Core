// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "wallet_fixtures.h"
#include "wallet_funding.h"
#include <bech32.h>
#include <future>
#include <iostream>
#include <util/translation.h>
#include <unistd.h>
const TranslateFn G_TRANSLATION_FUN{nullptr};
using namespace mercaweave;
using namespace mercaweave::wallet_research;
namespace {
size_t checks{0},rejections{0};
void Check(bool b,const char* message){++checks;if(!b)throw std::runtime_error(message);}
template<class F>void Reject(F&& f,Code code){try{f();throw std::runtime_error("unexpected wallet success");}catch(const WalletError& e){Check(e.code==code,"wallet error provenance");++rejections;}}
void Keys()
{
    wallet::CKeyingMaterial seed(32,0x42);B32 genesis{};genesis[0]=7;SecureString pass="public test passphrase";
    auto w=Wallet::Create(seed,genesis,pass);
    Check(w->IsLocked(),"new encrypted wallet locked");Reject([&]{w->Issue();},Code::Locked);
    Check(!w->Unlock(SecureString{"wrong"})&&w->Unlock(pass),"native encrypted seed unlock");
    auto first=w->Issue();B32 master{};std::copy(seed.begin(),seed.end(),master.begin());
    auto group_seed=Derive(master,genesis,0,0,0);mercaweave::KeyPair group(group_seed);
    Check(first.pk==group.pk&&first.nonce==Derive(master,genesis,0,0,0,0)&&first.script==Output(genesis,first.pk,first.nonce),"locked derivation exact");
    std::set<CScript> addresses{first.script};
    for(uint32_t i=1;i<64;++i){auto a=w->Issue();Check(a.pk==first.pk&&a.location.address==i&&addresses.insert(a.script).second,"64 unique nonce addresses share one group key");}
    auto next=w->Issue();Check(next.location.group==1&&next.location.address==0&&next.pk!=first.pk,"rotation at 64 newly issued addresses");
    auto change=w->Issue(0,1),mining=w->Issue(0,2),account=w->Issue(1,0);
    Check(change.pk!=first.pk&&mining.pk!=first.pk&&account.pk!=first.pk,"purpose/account separation");
    auto other_genesis=genesis;other_genesis[0]^=1;auto other=Wallet::Create(seed,other_genesis,pass);Check(other->Unlock(pass),"other test network unlock");
    Check(other->Issue().pk!=first.pk,"network-separated group key");
    const auto encoded=Wallet::EncodeResearchAddress(first,"tmca");auto decoded=bech32::Decode(encoded);
    Check(decoded.encoding==bech32::Encoding::BECH32M&&decoded.hrp=="tmca"&&decoded.data[0]==3,"native witness Bech32m address encoding");
    Reject([&]{Wallet::EncodeResearchAddress(first,"new-format");},Code::Network);
    auto v2=w->Issue(0,0,2);Check(v2.location.witness==2&&v2.pk!=first.pk,"existing native derivation remains separate");
    auto records=w->Records();auto backup=w->Backup();w->Lock();
    Reject([&]{w->Key(first);},Code::Locked);
    auto restored=Wallet::Restore(backup,genesis);Reject([&]{restored->Records();},Code::Metadata);
    Check(restored->Unlock(pass)&&restored->Records()==records,"encrypted restart and complete issued-range recovery");
    Check(w->Unlock(pass)&&restored->Issue()==w->Issue(),"restart continues exact next address");
    auto path=Wallet::StorageRoot()/("keys-"+std::to_string(getpid())+".snapshot");w->Save(path);
    auto disk=Wallet::Load(path,genesis);Check(disk->IsLocked()&&disk->Unlock(pass)&&disk->Records()==w->Records(),"native encrypted snapshot disk restart");std::filesystem::remove(path);
    Reject([&]{Wallet::Restore(backup,other_genesis);},Code::Network);
    auto tampered=backup;tampered.back()^=1;auto bad_mac=Wallet::Restore(tampered,genesis);
    Check(!bad_mac->Unlock(pass)&&bad_mac->IsLocked(),"metadata authentication fails closed");
    bool truncated=false;try{Wallet::Restore(std::span(backup).first(30),genesis);}catch(const std::exception&){truncated=true;}Check(truncated,"truncated encrypted snapshot");
    auto envelope=[&](const Bytes& data,const std::function<void(Bytes&)>& edit){Reader r(data);Bytes magic;wallet::CMasterKey mk;wallet::MercaturaPQCryptedSeed es;Bytes metadata;H48 mac;r>>magic>>mk>>es>>metadata>>mac;edit(metadata);Writer out;out<<magic<<mk<<es<<metadata<<mac;return out.bytes;};
    auto wrong_version=envelope(backup,[](Bytes& metadata){metadata[4]^=3;});Reject([&]{Wallet::Restore(wrong_version,genesis);},Code::Version);
    auto missing_ranges=envelope(backup,[](Bytes& metadata){const auto cursors=metadata[45];metadata.erase(metadata.begin()+46,metadata.begin()+46+cursors*14);metadata[45]=0;});Reject([&]{Wallet::Restore(missing_ranges,genesis);},Code::Metadata);
    auto watch=Wallet::Watch(genesis,records);Check(watch->IsWatchOnly()&&watch->IsLocked()&&!watch->Unlock(pass),"watch wallet has no seed/unlock");
    Reject([&]{watch->Issue();},Code::WatchOnly);Reject([&]{watch->Key(first);},Code::WatchOnly);
    auto damaged_watch=watch->Backup();damaged_watch.back()^=1;Reject([&]{Wallet::Restore(damaged_watch,genesis);},Code::Metadata);auto watch_copy=Wallet::Restore(watch->Backup(),genesis);Check(watch_copy->Records()==records,"explicit public range watch restore");
    auto recovered=Wallet::Create(seed,genesis,pass);Check(recovered->Unlock(pass),"seed-only recovery unlock");recovered->RecoverIssued(w->Records());
    Check(recovered->Issue()==w->Issue(),"explicit issued ranges reconstruct high-water indexes");
    wallet::CKeyingMaterial wrong_seed(32,0x43);auto wrong=Wallet::Create(wrong_seed,genesis,pass);Check(wrong->Unlock(pass),"wrong-seed fixture unlock");
    Reject([&]{wrong->RecoverIssued(records);},Code::Ownership);
    auto foreign=records;foreign[0].location.account++;
    Reject([&]{recovered->RecoverIssued(foreign);},Code::Ownership);
    Reject([&]{NextCursor({UINT32_MAX,64},3);},Code::Index);Reject([&]{NextCursor({UINT32_MAX,0},2);},Code::Index);
    Reject([&]{w->Issue(0,3);},Code::Metadata);
    const auto before=w->Records().size();for(size_t i=0;i<100;++i)Check(w->Find(first.script)==first,"repeated payment address lookup");
    Check(w->Records().size()==before,"address reuse consumes no issuance index");
    std::vector<std::future<Address>> pending;for(int i=0;i<16;++i)pending.push_back(std::async(std::launch::async,[&]{return w->Issue();}));
    std::set<CScript> concurrent;for(auto& job:pending)Check(concurrent.insert(job.get().script).second,"concurrent address issuance unique");
    Check(w->Records().size()==before+16,"serialized concurrent high-water state");
    Reject([&]{w->Save("/workspace/not-a-research-wallet");},Code::Metadata);
    auto root=std::filesystem::absolute(Wallet::StorageRoot()).lexically_normal();auto parked=root;parked+=".parked-"+std::to_string(getpid());auto target=root;target+=".target-"+std::to_string(getpid());
    std::filesystem::create_directory(target);std::filesystem::rename(root,parked);std::filesystem::create_directory_symlink(target,root);
    bool escaped=false;try{w->Save(root/"escape.snapshot");escaped=true;}catch(const WalletError& e){Check(e.code==Code::Metadata,"root symlink error provenance");}
    std::filesystem::remove(root);std::filesystem::rename(parked,root);if(escaped)std::filesystem::remove(target/"escape.snapshot");std::filesystem::remove(target);Check(!escaped,"configured storage root cannot redirect through symlink");
    auto cursor_limit=Wallet::Create(seed,genesis,pass);Check(cursor_limit->Unlock(pass),"cursor-limit fixture unlock");for(uint32_t i=0;i<Wallet::MAX_RECORDS;++i)cursor_limit->Rotate(i,0);
    auto bounded_backup=cursor_limit->Backup();Reject([&]{cursor_limit->Rotate(Wallet::MAX_RECORDS,0);},Code::Index);Reject([&]{cursor_limit->Issue(0,1);},Code::Index);
    Check(cursor_limit->Backup()==bounded_backup,"cursor limit preserves authenticated metadata atomically");auto bounded_restore=Wallet::Restore(bounded_backup,genesis);Check(bounded_restore->Unlock(pass),"maximum cursor table remains restorable");
}
void FundingChecks()
{
    namespace wt=mercaweave::wallet_research::test;
    auto w=wt::Owner();auto grouped=w->Issue(),native=w->Issue(0,0,2),change=w->Issue(0,1,2),group_change=w->Issue(0,1,3),recipient=w->Issue(1,0,2);
    auto view=integration::View::Create(wt::Genesis());std::vector<FundingCoin> coins;
    for(uint32_t i=0;i<10;++i){auto point=wt::Point(100+i);view->Insert(point,Coin{CTxOut{1000,grouped.script},1,false});coins.push_back({point});}
    for(uint32_t i=0;i<2;++i){auto point=wt::Point(200+i);view->Insert(point,Coin{CTxOut{5000,native.script},1,false});coins.push_back({point});}
    std::vector<CTxOut> pay{CTxOut{7000,recipient.script}};
    auto automatic=Funding::Fund(*w,view,coins,pay,change);auto v2=Funding::Fund(*w,view,coins,pay,change,FundingMode::StandardPQ);auto v3=Funding::Fund(*w,view,coins,pay,group_change,FundingMode::MercaWeave);
    Check(automatic.groups==1&&automatic.native_inputs==0&&automatic.selected>2&&automatic.selected<10&&automatic.fee<v2.fee,"final cost prefers useful grouped subset over fewer native inputs");
    Check(v2.native_inputs==2&&v2.groups==0&&v2.current_policy,"strict standard mode uses native ownership and current policy");Check(v3.groups==1&&!v3.current_policy&&v3.policy_reason=="inputs-not-standard","strict grouped mode remains research under unchanged input policy");
    auto signed_psbt=automatic.psbt;auto progress=GroupPSBT::Sign(signed_psbt,*w,view,wt::ZeroFixture());Check(progress.group_signatures==1&&progress.native_signatures==0,"selected subset signs one real group");auto tx=GroupPSBT::Extract(signed_psbt,view,wt::Genesis());Check(Measure(tx).total==automatic.sizes.total&&Measure(tx).weight==automatic.sizes.weight,"exact fixed-witness estimate matches final transaction");
    CAmount total=0;for(const auto& in:tx.vin)total+=view->CoinAt(in.prevout)->out.nValue;for(const auto& out:tx.vout)total-=out.nValue;Check(total==automatic.fee,"estimated fee equals actual integer fee");
    Check(Funding::Fee(0,{})==1&&Funding::Fee(1000,{})==1&&Funding::Fee(1001,{})==2&&Funding::Fee(2000,{})==2,"one-unit minimum and integer ceil per full 1000 bytes");
    auto high=Funding::Fund(*w,view,coins,pay,change,FundingMode::Automatic,FeePolicy{10,20,1000});Check(high.fee>=20&&high.fee==Funding::Fee(high.sizes.total,FeePolicy{10,20,1000}),"higher user fees preserved");
    Reject([&]{Funding::Fee(1000,FeePolicy{0});},Code::Fee);Reject([&]{Funding::Fee(10000,FeePolicy{1,1,1});},Code::Fee);
    auto subset=std::span(coins).first(10);Reject([&]{Funding::Fund(*w,view,subset,pay,change,FundingMode::StandardPQ);},Code::Funds);
    Reject([&]{Funding::Exact(*w,view,std::span(coins).last(2),pay,&group_change,FundingMode::StandardPQ);},Code::Mode);
    auto duplicate=coins;duplicate.push_back(coins[0]);Reject([&]{Funding::Fund(*w,view,duplicate,pay,change);},Code::Inputs);
    auto unsafe=coins;for(auto& c:unsafe)c.safe=false;Reject([&]{Funding::Fund(*w,view,unsafe,pay,change);},Code::Funds);
    auto unconfirmed=coins;for(auto& c:unconfirmed)c.depth=0;Reject([&]{Funding::Fund(*w,view,unconfirmed,pay,change);},Code::Funds);
    std::vector<CTxOut> dust{CTxOut{1,recipient.script}};Reject([&]{Funding::Fund(*w,view,coins,dust,change);},Code::Policy);
    std::vector<CTxOut> floor{CTxOut{2,recipient.script}};Check(Funding::Fund(*w,view,coins,floor,change).fee>=1,"native two-unit dust floor accepted");Check(Funding::Fund(*w,view,coins,floor,change,FundingMode::StandardPQ).current_policy,"native two-unit output passes actual dust relay policy");
    auto watch=Wallet::Watch(wt::Genesis(),w->Records());Check(Funding::Fund(*watch,view,coins,pay,change).sizes.total==automatic.sizes.total,"watch-only can plan from explicit verified public records");
    std::vector<FundingCoin> independent;for(uint32_t i=0;i<100;++i){w->Rotate(2,0);auto a=w->Issue(2,0);auto point=wt::Point(300+i);view->Insert(point,Coin{CTxOut{10000,a.script},1,false});independent.push_back({point});}
    auto batches=Funding::Sweep(*w,view,independent,recipient.script,FundingMode::MercaWeave);size_t selected=0;for(const auto& batch:batches){Check(batch.sizes.weight<=400000&&batch.groups==batch.selected,"independent keys never fictitiously aggregate");selected+=batch.selected;}
    Check(batches.size()==2&&selected==100,"100-independent-key sweep splits under standard weight");
    auto shared=Funding::Sweep(*w,view,subset,recipient.script);Check(shared.size()==1&&shared[0].groups==1&&shared[0].sizes.total<10000,"same-key sweep shares anchor");
    auto dust_coin=wt::Point(998);view->Insert(dust_coin,Coin{CTxOut{7,grouped.script},1,false});std::vector<FundingCoin> dust_set{{dust_coin}};
    try{Funding::Sweep(*w,view,dust_set,recipient.script);throw std::runtime_error("unexpected dust sweep");}catch(const WalletError& e){Check(e.code==Code::Policy&&std::string_view(e.what())=="final dust output","sweep preserves dust cause instead of masking it as a size limit");}
    auto immature=wt::Point(999);view->Insert(immature,Coin{CTxOut{10000,grouped.script},199,true});std::vector<FundingCoin> immature_set{{immature}};Reject([&]{Funding::Exact(*w,view,immature_set,floor,nullptr);},Code::Inputs);
}
}
int main()
{
    try{Keys();FundingChecks();std::cout<<"{\"wallet_checks\":"<<checks<<",\"rejections\":"<<rejections<<",\"passed\":true}\n";}
    catch(const std::exception& e){std::cerr<<"FAIL wallet: "<<e.what()<<'\n';return 1;}
}
