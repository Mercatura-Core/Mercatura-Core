// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "research_wallet.h"
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
}
}
int main()
{
    try{Keys();std::cout<<"{\"key_checks\":"<<checks<<",\"rejections\":"<<rejections<<",\"passed\":true}\n";}
    catch(const std::exception& e){std::cerr<<"FAIL wallet: "<<e.what()<<'\n';return 1;}
}
