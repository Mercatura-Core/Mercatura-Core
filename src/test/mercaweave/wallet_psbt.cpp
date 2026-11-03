// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "wallet_psbt.h"
#include <crypto/mercatura_pqkey.h>
#include <support/cleanse.h>
namespace mercaweave::wallet_research {
namespace {
const Bytes IDENT(PSBT_ID.begin(),PSBT_ID.end());
void Unknown(const std::map<Bytes,Bytes>& fields)
{
    for(const auto& [key,value]:fields)Require(!key.empty()&&key[0]>=0x80&&key[0]!=0xfc&&key.size()<=4096&&value.size()<=4*1024*1024,Code::PSBT,"unsafe unknown key alias or bounds");
}
void Fresh(const integration::ViewPtr& v,uint64_t epoch){Require(v&&v->Epoch()==epoch,Code::Inputs,"authoritative view changed");}
void Properties(const std::set<PSBTProprietary>& props,bool input)
{
    for(const auto& p:props){
        Require(p.key.size()<=4096&&p.value.size()<=4*1024*1024,Code::PSBT,"proprietary field bounds");
        Reader r(p.key);Require(ReadCompactSize(r)==PSBT_IN_PROPRIETARY,Code::PSBT,"proprietary key type");
        auto n=ReadCompactSize(r);Require(n<=r.remaining(),Code::PSBT,"identifier bounds");Bytes id(n);r.read(std::as_writable_bytes(std::span(id)));
        auto sub=ReadCompactSize(r);Require(id==p.identifier&&sub==p.subtype,Code::PSBT,"cached proprietary key mismatch");
        if(id==IDENT){Require(input&&r.remaining()==0&&sub>=1&&sub<=4,Code::PSBT,"unsupported MercaWeave key data or location");
            const size_t size=sub==1?32:sub==2?4:sub==3?1952:3309;
            Require(p.value.size()==size,Code::PSBT,"MercaWeave value length");}
        else if(id==Bytes{'M','e','r','c','a','t','u','r','a'}){std::string error;Require(ValidateMercaturaPQPSBTProprietary(p,r.remaining()!=0,error),Code::PSBT,"native PQ proprietary field");}
    }
}
void Shape(const PartiallySignedTransaction& p)
{
    Require(p.tx&&p.inputs.size()==p.tx->vin.size()&&p.outputs.size()==p.tx->vout.size()&&!p.inputs.empty()&&p.inputs.size()<=MAX_INPUTS,Code::PSBT,"PSBT input/output shape");
    Require(p.GetVersion()==0&&p.m_xpubs.empty(),Code::PSBT,"unsupported version or public derivation");
    Unknown(p.unknown);Properties(p.m_proprietary,false);for(const auto& o:p.outputs){Unknown(o.unknown);Properties(o.m_proprietary,false);}
    for(size_t i=0;i<p.inputs.size();++i){const auto& in=p.inputs[i];
        Require(p.tx->vin[i].scriptSig.empty()&&p.tx->vin[i].scriptWitness.IsNull(),Code::PSBT,"unsigned transaction contains scripts");
        Unknown(in.unknown);Properties(in.m_proprietary,true);
        Require(!in.sighash_type||*in.sighash_type==0,Code::PSBT,"only fixed default signing mode");
        Require(in.partial_sigs.empty()&&in.hd_keypaths.empty()&&in.redeem_script.empty()&&in.witness_script.empty()&&in.final_script_sig.empty()&&
            in.m_tap_key_sig.empty()&&in.m_tap_script_sigs.empty()&&in.m_tap_scripts.empty()&&in.m_tap_bip32_paths.empty()&&
            !in.m_tap_internal_key.IsFullyValid()&&in.m_tap_merkle_root.IsNull()&&in.m_musig2_participants.empty()&&in.m_musig2_pubnonces.empty()&&in.m_musig2_partial_sigs.empty(),Code::PSBT,"unsupported classical or alternate signing metadata");
    }
}
}
PSBTProprietary Field(uint64_t sub,std::span<const unsigned char> value)
{
    Writer key;WriteCompactSize(key,PSBT_IN_PROPRIETARY);key<<IDENT;WriteCompactSize(key,sub);
    return {sub,IDENT,std::move(key.bytes),Bytes(value.begin(),value.end())};
}
const Bytes* GetField(const PSBTInput& in,uint64_t sub){auto it=in.m_proprietary.find(Field(sub,{}));return it==in.m_proprietary.end()?nullptr:&it->value;}
void PutField(PSBTInput& in,uint64_t sub,std::span<const unsigned char> value)
{auto f=Field(sub,value);in.m_proprietary.erase(f);in.m_proprietary.insert(std::move(f));}
PartiallySignedTransaction GroupPSBT::Create(const CMutableTransaction& tx,std::span<const CTxOut> spent,std::span<const Address> addresses)
{
    Require(tx.vin.size()==spent.size()&&spent.size()==addresses.size(),Code::PSBT,"creation metadata count");
    CMutableTransaction unsigned_tx(tx);for(auto& in:unsigned_tx.vin){in.scriptSig.clear();in.scriptWitness.SetNull();}
    PartiallySignedTransaction p(unsigned_tx);std::map<PublicKey,uint32_t> anchors;
    for(uint32_t i=0;i<spent.size();++i){const auto& a=addresses[i];Require(a.script==spent[i].scriptPubKey,Code::Ownership,"creation owner script");p.inputs[i].witness_utxo=spent[i];
        if(a.location.witness==3){PutField(p.inputs[i],1,a.nonce);auto [it,first]=anchors.emplace(a.pk,i);
            if(first)PutField(p.inputs[i],3,a.pk);else{Writer ref;ref<<it->second;PutField(p.inputs[i],2,ref.bytes);}}
        else{Require(a.location.witness==2,Code::PSBT,"unsupported input program");Require(SetMercaturaPQPSBTPublicKey(p.inputs[i],a.pk),Code::PSBT,"native public key");}
    }return p;
}
Inspection GroupPSBT::Inspect(const PartiallySignedTransaction& p,const integration::ViewPtr& view,const B32& genesis)
{
    Shape(p);auto acquired=integration::Engine::AcquireInputs(view,*p.tx,genesis,MANDATORY_SCRIPT_VERIFY_FLAGS);
    Require(acquired.acquired!=nullptr,Code::Inputs,"authoritative input preflight");
    Inspection out{*p.tx,acquired.acquired->spent,{}, {},std::vector<bool>(p.inputs.size(),false),acquired.acquisition_epoch};
    for(uint32_t i=0;i<p.inputs.size();++i){const auto& in=p.inputs[i];
        Require(!in.witness_utxo.IsNull()&&in.witness_utxo==out.spent[i],Code::Inputs,"missing or mismatched witness UTXO");
        if(in.non_witness_utxo){auto n=out.tx.vin[i].prevout.n;Require(in.non_witness_utxo->GetHash()==out.tx.vin[i].prevout.hash&&n<in.non_witness_utxo->vout.size()&&in.non_witness_utxo->vout[n]==out.spent[i],Code::Inputs,"non-witness UTXO mismatch");}
        int v;Bytes program;Require(out.spent[i].scriptPubKey.IsWitnessProgram(v,program)&&program.size()==32&&(v==2||v==3),Code::PSBT,"only native PQ and grouped PQ supported");
        auto nonce=GetField(in,1),ref=GetField(in,2),pk=GetField(in,3),sig=GetField(in,4);
        if(v==3){Require(nonce&&((pk&&!ref)||(!pk&&ref&&!sig)),Code::PSBT,"anchor/follower role mismatch");
            Require(!GetMercaturaPQPSBTPublicKey(in)&&!GetMercaturaPQPSBTSignature(in),Code::PSBT,"mixed signing namespaces");
            out.tx.vin[i].scriptWitness.stack=pk?std::vector<Bytes>{sig?*sig:Bytes(3309),*pk,*nonce}:std::vector<Bytes>{*nonce,*ref};}
        else{Require(!nonce&&!ref&&!pk&&!sig,Code::PSBT,"group metadata on native input");auto native_pk=GetMercaturaPQPSBTPublicKey(in),native_sig=GetMercaturaPQPSBTSignature(in);
            Require(native_pk&&native_pk->size()==1952,Code::PSBT,"native public key required");MercaturaPQKeyCommitment commitment{};
            Require(ComputeMercaturaPQKeyCommitmentV1(commitment,*native_pk)&&std::equal(program.begin(),program.end(),commitment.begin()),Code::Ownership,"native public key ownership");
            out.tx.vin[i].scriptWitness.stack={native_sig?*native_sig:Bytes(3309),*native_pk};}
    }
    out.mapping=Reconstruct(out.tx,out.spent);Statistics stats;CheckOwnership(out.mapping,out.spent,genesis,stats);
    out.hashes=TransactionHashes(out.tx,out.spent,genesis,out.mapping);
    for(const auto& group:out.mapping.groups){if(auto sig=GetField(p.inputs[group.anchor],4)){
        Require(VerifyDigest(Digest(genesis,out.hashes,group),group.pk,*sig),Code::Signature,"stale or invalid group signature");++out.verified_groups;
        for(const auto& m:group.members)out.complete[m.index]=true;}}
    test::NativeSession native(out.tx,out.spent,genesis);
    for(uint32_t i=0;i<p.inputs.size();++i){if(GetMercaturaPQPSBTSignature(p.inputs[i])){ScriptError error;size_t calls=0;
        Require(native.Check(i,error,calls),Code::Signature,"stale or invalid native signature");++out.verified_native;out.complete[i]=true;}
        if(!p.inputs[i].final_script_witness.IsNull())Require(out.complete[i]&&p.inputs[i].final_script_witness.stack==out.tx.vin[i].scriptWitness.stack,Code::PSBT,"final witness differs from verified fields");}
    Fresh(view,out.epoch);return out;
}
Progress GroupPSBT::Sign(PartiallySignedTransaction& p,const Wallet& wallet,const integration::ViewPtr& view,const RandomSource& random)
{
    Require(!wallet.IsWatchOnly(),Code::WatchOnly,"watch wallet cannot sign");Require(!wallet.IsLocked(),Code::Locked,"wallet locked");
    PartiallySignedTransaction staged=p;Progress progress;
    const bool accessed=wallet.WithMercaturaPQMasterSeed([&](const wallet::CKeyingMaterial&){
        auto inspected=Inspect(staged,view,wallet.Genesis());
        for(const auto& group:inspected.mapping.groups){if(inspected.complete[group.anchor])continue;std::optional<Address> anchor;
            bool owns=true;for(const auto& member:group.members){auto a=wallet.Find(inspected.spent[member.index].scriptPubKey);
                if(!a||a->pk!=group.pk||a->nonce!=member.nonce){owns=false;break;}if(member.index==group.anchor)anchor=a;}
            if(!owns||!anchor)continue;auto key=wallet.Key(*anchor);B32 entropy{};
            Require(random&&random(entropy),Code::Entropy,"secure group signing entropy unavailable");
            auto sig=SignDigest(Digest(wallet.Genesis(),inspected.hashes,group),*key,entropy);memory_cleanse(entropy.data(),entropy.size());
            PutField(staged.inputs[group.anchor],4,sig);++progress.group_signatures;
        }
        test::NativeSession native(inspected.tx,inspected.spent,wallet.Genesis());
        for(uint32_t i=0;i<staged.inputs.size();++i){auto a=wallet.Find(inspected.spent[i].scriptPubKey);if(!a||a->location.witness!=2||inspected.complete[i])continue;
            auto key=wallet.Key(*a);B32 entropy{};Require(random&&random(entropy),Code::Entropy,"secure native signing entropy unavailable");
            auto sig=SignDigest(native.Digest(i),*key,entropy,"Mercatura/PQAuth/v1");memory_cleanse(entropy.data(),entropy.size());
            Require(SetMercaturaPQPSBTSignature(staged.inputs[i],sig),Code::PSBT,"native signature publication");++progress.native_signatures;}
        Fresh(view,inspected.epoch);return true;
    });
    Require(accessed,Code::Locked,"wallet locked during signing");auto final=Inspect(staged,view,wallet.Genesis());
    progress.complete_inputs=std::count(final.complete.begin(),final.complete.end(),true);progress.complete=progress.complete_inputs==staged.inputs.size();p=std::move(staged);return progress;
}
PartiallySignedTransaction GroupPSBT::Combine(const PartiallySignedTransaction& a,const PartiallySignedTransaction& b,const integration::ViewPtr& v,const B32& g)
{
    auto ia=Inspect(a,v,g);auto ib=Inspect(b,v,g);Require(Wire(*a.tx,false)==Wire(*b.tx,false),Code::Conflict,"different unsigned transaction bodies");
    auto conflict=[](const auto& x,const auto& y){for(const auto& [k,value]:x){auto it=y.find(k);Require(it==y.end()||it->second==value,Code::Conflict,"different unknown field values");}};
    conflict(a.unknown,b.unknown);
    auto props=[](const auto& x,const auto& y){for(const auto& p:x){auto it=y.find(p);Require(it==y.end()||it->value==p.value,Code::Conflict,"different values for same proprietary key");}};
    props(a.m_proprietary,b.m_proprietary);
    for(size_t i=0;i<a.inputs.size();++i){props(a.inputs[i].m_proprietary,b.inputs[i].m_proprietary);conflict(a.inputs[i].unknown,b.inputs[i].unknown);
        const auto& x=a.inputs[i].final_script_witness;const auto& y=b.inputs[i].final_script_witness;
        Require(x.IsNull()||y.IsNull()||x.stack==y.stack,Code::Conflict,"different finalized witnesses");}
    for(size_t i=0;i<a.outputs.size();++i){props(a.outputs[i].m_proprietary,b.outputs[i].m_proprietary);conflict(a.outputs[i].unknown,b.outputs[i].unknown);}
    auto merged=a;Require(merged.Merge(b),Code::Conflict,"native PSBT merge failed");Inspect(merged,v,g);Fresh(v,ia.epoch);Fresh(v,ib.epoch);return merged;
}
Progress GroupPSBT::FinalizeAvailable(PartiallySignedTransaction& p,const integration::ViewPtr& v,const B32& g)
{
    auto info=Inspect(p,v,g);auto staged=p;Progress result;
    for(size_t i=0;i<p.inputs.size();++i)if(info.complete[i]){staged.inputs[i].final_script_witness=info.tx.vin[i].scriptWitness;++result.complete_inputs;}
    result.complete=result.complete_inputs==p.inputs.size();Fresh(v,info.epoch);p=std::move(staged);return result;
}
CMutableTransaction GroupPSBT::Extract(const PartiallySignedTransaction& p,const integration::ViewPtr& v,const B32& g)
{
    auto info=Inspect(p,v,g);Require(std::all_of(info.complete.begin(),info.complete.end(),[](bool b){return b;}),Code::Signature,"incomplete PSBT");
    auto result=integration::Engine::Validate(v,info.tx,g,MANDATORY_SCRIPT_VERIFY_FLAGS);Require(result.valid,Code::Signature,"completed transaction failed isolated validation");Fresh(v,info.epoch);return info.tx;
}
Bytes GroupPSBT::Encode(const PartiallySignedTransaction& p){Shape(p);return SerializeBytes(p);}
PartiallySignedTransaction GroupPSBT::Decode(std::span<const unsigned char> bytes)
{
    Require(bytes.size()<=16*1024*1024,Code::PSBT,"PSBT size bound");Reader r(bytes);std::array<unsigned char,5> magic{};r>>magic;
    Require(std::equal(magic.begin(),magic.end(),PSBT_MAGIC_BYTES),Code::PSBT,"PSBT magic");std::optional<CMutableTransaction> tx;
    size_t total_fields=0;auto map=[&](bool global){std::set<Bytes> keys;while(true){auto n=ReadCompactSize(r);if(n==0)break;Require(++total_fields<=32768&&keys.size()<256,Code::PSBT,"PSBT map field bound");Require(n<=4096&&n<=r.remaining(),Code::PSBT,"PSBT key bound");Bytes key(n);r.read(std::as_writable_bytes(std::span(key)));Require(keys.insert(key).second,Code::PSBT,"duplicate PSBT key");
        auto size=ReadCompactSize(r);Require(size<=4*1024*1024&&size<=r.remaining(),Code::PSBT,"PSBT value bound");Bytes value(size);r.read(std::as_writable_bytes(std::span(value)));
        if(global&&key==Bytes{0}){Require(!tx,Code::PSBT,"duplicate unsigned body");tx=DecodeTransaction(value);}}};
    map(true);Require(tx.has_value(),Code::PSBT,"missing unsigned body");for(size_t i=0;i<tx->vin.size()+tx->vout.size();++i)map(false);Require(r.remaining()==0,Code::PSBT,"trailing PSBT maps");
    SpanReader native(bytes);PartiallySignedTransaction p;native>>p;Require(native.empty(),Code::PSBT,"trailing native PSBT data");Shape(p);
    // Canonical transport ordering is a research-local import policy.
    Require(SerializeBytes(p)==Bytes(bytes.begin(),bytes.end()),Code::PSBT,"noncanonical PSBT roundtrip");return p;
}
}
