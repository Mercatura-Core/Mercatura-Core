// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "wallet_fixtures.h"
#include <future>
#include <iostream>
#include <util/translation.h>
const TranslateFn G_TRANSLATION_FUN{nullptr};
using namespace mercaweave;
using namespace mercaweave::wallet_research;
namespace wt=mercaweave::wallet_research::test;
namespace {
size_t checks=0,rejected=0,mutation_total=0,mutation_rejected=0;
void Check(bool b,const char* m){++checks;if(!b)throw std::runtime_error(m);}
template<class F>void Reject(F&& f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}Check(bad,"expected PSBT rejection");++rejected;}
void Run()
{
    auto a=wt::Owner(),b=wt::Owner(0x43);auto aa=a->Issue(),aa2=a->Issue(),bb=b->Issue(),av2=a->Issue(0,0,2),dest=a->Issue(0,1,2);
    auto view=integration::View::Create(wt::Genesis());CMutableTransaction tx;std::vector<CTxOut> spent;std::vector<Address> addresses;
    wt::Add(tx,spent,addresses,view,aa,1);wt::Add(tx,spent,addresses,view,bb,2);wt::Add(tx,spent,addresses,view,aa2,3);wt::Add(tx,spent,addresses,view,av2,4);
    tx.vout.emplace_back(39997,dest.script);tx.vout.emplace_back(2,av2.script);auto p=GroupPSBT::Create(tx,spent,addresses);
    auto info=GroupPSBT::Inspect(p,view,wt::Genesis());Check(info.mapping.groups.size()==2&&info.mapping.groups[0].members.size()==2,"canonical interleaved groups");
    auto bytes=GroupPSBT::Encode(p);Check(GroupPSBT::Encode(GroupPSBT::Decode(bytes))==bytes,"real native PSBT canonical roundtrip");
    auto key=Field(1,aa.nonce);Check(key.identifier.size()==10&&key.key==Bytes({0xfc,10,'M','e','r','c','a','W','e','a','v','e',1}),"ten-byte identifier and empty keydata");
    auto pa=p,pb=p;auto sa=GroupPSBT::Sign(pa,*a,view,wt::ZeroFixture());Check(sa.group_signatures==1&&sa.native_signatures==1&&sa.complete_inputs==3&&!sa.complete,"partial signer owns one group and native input");
    auto partial=GroupPSBT::FinalizeAvailable(pa,view,wt::Genesis());Check(partial.complete_inputs==3&&!partial.complete&&pa.inputs[2].final_script_witness.stack.size()==2,"owned followers finalize without other private key");
    auto stable=GroupPSBT::Encode(pa);Check(GroupPSBT::FinalizeAvailable(pa,view,wt::Genesis()).complete_inputs==3&&GroupPSBT::Encode(pa)==stable,"partial finalize idempotent");
    Reject([&]{GroupPSBT::Extract(pa,view,wt::Genesis());});pb=GroupPSBT::Decode(GroupPSBT::Encode(pb));auto sb=GroupPSBT::Sign(pb,*b,view,wt::ZeroFixture());Check(sb.group_signatures==1&&sb.native_signatures==0,"independent signer without foreign keys");
    pa=GroupPSBT::Decode(GroupPSBT::Encode(pa));auto merged=GroupPSBT::Combine(pa,pb,view,wt::Genesis());Check(GroupPSBT::FinalizeAvailable(merged,view,wt::Genesis()).complete,"native Merge combines independently signed groups");
    auto final=GroupPSBT::Extract(merged,view,wt::Genesis());Check(integration::Engine::Validate(view,final,wt::Genesis(),MANDATORY_SCRIPT_VERIFY_FLAGS).valid,"completed mixed transaction uses WO04 verifier");
    auto repeated=GroupPSBT::Sign(merged,*a,view);Check(repeated.group_signatures==0&&repeated.native_signatures==0,"sign valid complete PSBT idempotently");
    auto randomized=p;GroupPSBT::Sign(randomized,*a,view);Check(*GetField(randomized.inputs[0],4)!=*GetField(pa.inputs[0],4),"production entropy differs from explicit zero fixture");
    Reject([&]{GroupPSBT::Combine(pa,randomized,view,wt::Genesis());});
    auto same=GroupPSBT::Combine(pa,pa,view,wt::Genesis());Check(GroupPSBT::Encode(same)==GroupPSBT::Encode(pa),"identical signature merge idempotent");
    auto atomic=p;auto original=GroupPSBT::Encode(atomic);int entropy_calls=0;
    Reject([&]{GroupPSBT::Sign(atomic,*a,view,[&](B32& out){out.fill(0);return ++entropy_calls<2;});});Check(GroupPSBT::Encode(atomic)==original,"entropy failure never publishes partial signatures");
    a->Lock();Reject([&]{GroupPSBT::Sign(atomic,*a,view);});Check(a->Unlock(SecureString{"public research fixture"}),"unlock signer");
    auto watch=Wallet::Watch(wt::Genesis(),a->Records());Reject([&]{GroupPSBT::Sign(atomic,*watch,view);});auto recovered=Wallet::Restore(a->Backup(),wt::Genesis());Check(recovered->Unlock(SecureString{"public research fixture"}),"recovered signer unlock");auto recovered_psbt=p;Check(GroupPSBT::Sign(recovered_psbt,*recovered,view,wt::ZeroFixture()).complete_inputs==3,"backup discovers issued coins and signs deterministically");auto wrong=wt::Owner(0x44);Check(GroupPSBT::Sign(atomic,*wrong,view).complete_inputs==0,"wrong seed cannot claim public commitments");
    auto changed=pa;changed.tx->version++;Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});changed=pa;changed.tx->vin[0].nSequence--;Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});changed=pa;std::swap(changed.tx->vin[0],changed.tx->vin[1]);std::swap(changed.inputs[0],changed.inputs[1]);Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});changed=pa;changed.tx->vout[0].nValue--;Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});changed=pa;std::swap(changed.tx->vout[0],changed.tx->vout[1]);Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});changed=pa;changed.tx->nLockTime++;Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});
    changed=pa;changed.inputs[0].witness_utxo.nValue++;Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});changed=pa;changed.inputs[0].witness_utxo.scriptPubKey=bb.script;Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});
    auto excessive=p;excessive.tx->vin.resize(MAX_INPUTS+1);excessive.inputs.resize(MAX_INPUTS+1);Reject([&]{GroupPSBT::Inspect(excessive,view,wt::Genesis());});
    auto g=wt::Genesis();g[0]^=1;Reject([&]{GroupPSBT::Inspect(pa,view,g);});
    for(uint64_t sub=1;sub<=4;++sub){auto bad=p;PutField(bad.inputs[0],sub,Bytes(1));Reject([&]{GroupPSBT::Inspect(bad,view,wt::Genesis());});}
    changed=p;PutField(changed.inputs[2],3,aa.pk);Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});
    changed=p;PutField(changed.inputs[0],2,Bytes(4));Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});
    changed=p;PutField(changed.inputs[3],1,aa.nonce);Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});
    changed=p;PutField(changed.inputs[0],5,aa.nonce);Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});
    changed=p;auto extra=Field(1,aa.nonce);extra.key.push_back(0);changed.inputs[0].m_proprietary.erase(Field(1,{}));changed.inputs[0].m_proprietary.insert(extra);Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});
    changed=p;extra=Field(1,aa.nonce);extra.subtype=2;changed.inputs[0].m_proprietary.erase(Field(1,{}));changed.inputs[0].m_proprietary.insert(extra);Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});
    changed=p;changed.m_proprietary.insert(Field(1,aa.nonce));Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});changed=p;changed.outputs[0].m_proprietary.insert(Field(1,aa.nonce));Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});
    changed=p;changed.inputs[0].unknown[Field(4,{}).key]=Bytes(3309);Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});changed=p;changed.inputs[0].sighash_type=1;Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});
    changed=p;changed.inputs[0].final_script_witness.stack={Bytes(1)};Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});
    changed=p;auto nonce=aa.nonce;nonce[0]^=1;PutField(changed.inputs[0],1,nonce);Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});
    changed=pa;auto signature=*GetField(pa.inputs[0],4);signature[0]^=1;PutField(changed.inputs[0],4,signature);Reject([&]{GroupPSBT::Inspect(changed,view,wt::Genesis());});
    Writer pair;pair<<key.key<<key.value;auto pos=std::search(bytes.begin(),bytes.end(),pair.bytes.begin(),pair.bytes.end());Check(pos!=bytes.end(),"raw duplicate fixture location");auto dup=bytes;dup.insert(dup.begin()+(pos-bytes.begin()),pair.bytes.begin(),pair.bytes.end());Reject([&]{GroupPSBT::Decode(dup);});
    auto bomb=p;for(uint32_t n=0;n<257;++n)bomb.inputs[0].unknown[Bytes{0x90,static_cast<unsigned char>(n),static_cast<unsigned char>(n>>8)}]={};Reject([&]{GroupPSBT::Decode(SerializeBytes(bomb));});
    auto trailing=bytes;trailing.push_back(0);Reject([&]{GroupPSBT::Decode(trailing);});for(size_t n:{size_t(0),size_t(5),bytes.size()-1})Reject([&]{GroupPSBT::Decode(std::span(bytes).first(n));});
    auto unknown=p;unknown.inputs[0].unknown[Bytes{0x90}]=Bytes{1,2,3};Check(GroupPSBT::Encode(GroupPSBT::Decode(GroupPSBT::Encode(unknown)))==GroupPSBT::Encode(unknown),"unrelated unknown field preserved");
    auto deterministic=p;GroupPSBT::Sign(deterministic,*a,view,wt::ZeroFixture());std::vector<std::future<Bytes>> jobs;for(int i=0;i<4;++i)jobs.push_back(std::async(std::launch::async,[&]{auto copy=p;GroupPSBT::Sign(copy,*a,view,wt::ZeroFixture());return GroupPSBT::Encode(copy);}));for(auto& j:jobs)Check(j.get()==GroupPSBT::Encode(deterministic),"concurrent deterministic signing");
    auto wrong_context=p;auto own_key=a->Key(aa);auto wrong_sig=SignDigest(Digest(wt::Genesis(),info.hashes,info.mapping.groups[0]),*own_key,B32{},"Mercatura/PQAuth/v1");PutField(wrong_context.inputs[0],4,wrong_sig);Reject([&]{GroupPSBT::Inspect(wrong_context,view,wt::Genesis());});
    auto complete_bytes=GroupPSBT::Encode(merged);size_t mutations=0;
    for(uint32_t n=1;n<=1000;++n){auto fuzz=complete_bytes;size_t at=(static_cast<uint64_t>(n)*2654435761ULL)%fuzz.size();fuzz[at]^=static_cast<unsigned char>(1U<<(n%8));
        std::optional<CMutableTransaction> extracted;try{auto decoded=GroupPSBT::Decode(fuzz);GroupPSBT::Inspect(decoded,view,wt::Genesis());extracted=GroupPSBT::Extract(decoded,view,wt::Genesis());}catch(const std::exception&){++mutations;}if(extracted)Check(integration::Engine::Validate(view,*extracted,wt::Genesis(),MANDATORY_SCRIPT_VERIFY_FLAGS).valid,"accepted PSBT mutation still verifies");}
    mutation_total=1000;mutation_rejected=mutations;Check(mutations>900,"malformed/stale PSBT mutation coverage");
    auto stale=p;Reject([&]{GroupPSBT::Sign(stale,*a,view,[&](B32& r){r.fill(0);view->SetTip(uint256{1});return true;});});Check(GroupPSBT::Encode(stale)==original,"view mutation cannot publish staged signatures");
    Check(view->Spend(tx.vin[0].prevout),"spend fixture");Reject([&]{GroupPSBT::Inspect(merged,view,wt::Genesis());});
}
}
int main(){try{Run();std::cout<<"{\"psbt_checks\":"<<checks<<",\"rejections\":"<<rejected<<",\"mutations\":"<<mutation_total<<",\"mutation_rejections\":"<<mutation_rejected<<",\"passed\":true}\n";}catch(const std::exception& e){std::cerr<<"FAIL PSBT: "<<e.what()<<'\n';return 1;}}
