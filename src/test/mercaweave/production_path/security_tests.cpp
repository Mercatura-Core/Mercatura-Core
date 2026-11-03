// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "native_fixture.h"
#include <script/pqgroup_psbt.h>
#include <iostream>
#include <random>

using namespace mercaweave;
using namespace mercaweave::production_test;
namespace {
uint256 G(const B32& genesis) { return uint256{std::span<const unsigned char>{genesis}}; }
uint256 CacheKey(ValidationCache& cache, const CTransaction& tx, const B32& genesis)
{
    auto hasher = cache.ScriptExecutionCacheHasher(); uint256 key;
    const auto flags = STANDARD_SCRIPT_VERIFY_FLAGS;
    hasher.Write(tx.GetWitnessHash().ToUint256().begin(),32);
    hasher.Write(reinterpret_cast<const unsigned char*>(&flags),sizeof(flags));
    const unsigned char present{1}; hasher.Write(&present,1); hasher.Write(genesis.data(),genesis.size());
    hasher.Finalize(key.begin()); return key;
}
bool Validate(const CMutableTransaction& mutable_tx, const std::vector<CTxOut>& spent,
              const B32& genesis, ValidationCache& cache,
              const std::shared_ptr<const IsolatedExecution>& execution, Statistics& work)
{
    CCoinsView empty; CCoinsViewCache view(&empty);
    for (size_t i = 0; i < spent.size(); ++i) {
        // Duplicates are deliberately left for the production structural check.
        if (!view.HaveCoin(mutable_tx.vin[i].prevout))
            view.AddCoin(mutable_tx.vin[i].prevout,Coin{CTxOut(spent[i]),1,false},false);
    }
    TxValidationState state; const CTransaction tx(mutable_tx);
    LOCK(cs_main);
    return CheckInputScriptsWithPQGroups(tx,state,view,STANDARD_SCRIPT_VERIFY_FLAGS,cache,nullptr,
                                        execution,G(genesis),&work);
}
}
BOOST_FIXTURE_TEST_SUITE(pqgroup_security,NativeSetup)
BOOST_AUTO_TEST_CASE(real_cache_and_negative_authorization_matrix)
{
    auto good = Fixture(10,2,900);
    ValidationCache cache(1 << 20,1 << 20); Statistics work;
    BOOST_REQUIRE(Validate(good.tx,good.spent,Genesis(),cache,Execution(),work));
    BOOST_CHECK_EQUAL(work.verification_calls,2);
    std::vector<CScriptCheck> owned_jobs;
    {
        CCoinsView empty; CCoinsViewCache view(&empty);
        for (size_t i = 0; i < good.spent.size(); ++i)
            view.AddCoin(good.tx.vin[i].prevout,Coin{CTxOut(good.spent[i]),1,false},false);
        const CTransaction original(good.tx); TxValidationState state;
        LOCK(cs_main);
        BOOST_REQUIRE(CheckInputScriptsWithPQGroups(original,state,view,STANDARD_SCRIPT_VERIFY_FLAGS,cache,
            &owned_jobs,Execution(),G(Genesis()),&work));
    }
    BOOST_CHECK_EQUAL(owned_jobs.size(),10);
    std::reverse(owned_jobs.begin(),owned_jobs.end());
    {
        CCheckQueue<CScriptCheck> queue{3,2};
        CCheckQueueControl<CScriptCheck> control(queue);
        control.Add(std::move(owned_jobs));
        BOOST_CHECK(!control.Complete());
    }
    const std::vector<std::string> names{
        "wrong-public-key","altered-signature","signature-length","public-key-length",
        "nonce-length","self-reference","follower-reference","outside-reference",
        "duplicate-anchor","missing-anchor","extra-anchor","missing-follower",
        "duplicate-outpoint","wrong-commitment","wrong-genesis","changed-prevout",
        "changed-amount","changed-script","changed-sequence","changed-version",
        "changed-locktime","changed-output-value","changed-output-script","reordered-inputs",
        "added-member","removed-member","moved-group","mixed-independent-keys",
        "scriptSig","oversized-witness","late-invalid","reordered-outputs"};
    for (size_t kind = 0; kind < names.size(); ++kind) {
        auto tx = good.tx; auto spent = good.spent; auto genesis = Genesis();
        switch (kind) {
        case 0: tx.vin[0].scriptWitness.stack[1][100] ^= 1; break;
        case 1: tx.vin[0].scriptWitness.stack[0][100] ^= 1; break;
        case 2: tx.vin[0].scriptWitness.stack[0].pop_back(); break;
        case 3: tx.vin[0].scriptWitness.stack[1].push_back(0); break;
        case 4: tx.vin[2].scriptWitness.stack[0].push_back(0); break;
        case 5: WriteLE32(tx.vin[2].scriptWitness.stack[1].data(),2); break;
        case 6: WriteLE32(tx.vin[4].scriptWitness.stack[1].data(),2); break;
        case 7: WriteLE32(tx.vin[2].scriptWitness.stack[1].data(),500); break;
        case 8: tx.vin[1].scriptWitness.stack[1] = tx.vin[0].scriptWitness.stack[1]; break;
        case 9: tx.vin[0].scriptWitness.stack.clear(); break;
        case 10: tx.vin[2].scriptWitness = tx.vin[0].scriptWitness; break;
        case 11: tx.vin[2].scriptWitness.SetNull(); break;
        case 12: tx.vin[2].prevout = tx.vin[3].prevout; break;
        case 13: spent[2].scriptPubKey[3] ^= 1; break;
        case 14: genesis[0] ^= 1; break;
        case 15: ++tx.vin[2].prevout.n; break;
        case 16: --spent[2].nValue; break;
        case 17: spent[2].scriptPubKey = spent[3].scriptPubKey; break;
        case 18: --tx.vin[2].nSequence; break;
        case 19: ++tx.version; break;
        case 20: ++tx.nLockTime; break;
        case 21: --tx.vout[0].nValue; break;
        case 22: tx.vout[0].scriptPubKey[3] ^= 1; break;
        case 23: std::swap(tx.vin[2],tx.vin[3]); break;
        case 24: tx.vin.push_back(tx.vin[2]); spent.push_back(spent[2]); tx.vin.back().prevout.n += 3000; break;
        case 25: tx.vin.pop_back(); spent.pop_back(); break;
        case 26: WriteLE32(tx.vin[2].scriptWitness.stack[1].data(),1); break;
        case 27: tx.vin[1].scriptWitness.stack[1] = tx.vin[0].scriptWitness.stack[1]; break;
        case 28: tx.vin[2].scriptSig << OP_TRUE; break;
        case 29: tx.vin[0].scriptWitness.stack.push_back(Bytes(10000)); break;
        case 30: tx.vin[1].scriptWitness.stack[0][100] ^= 1; break;
        default: tx.vout.push_back(tx.vout[0]); tx.vout[0].nValue = 1; std::swap(tx.vout[0],tx.vout[1]); break;
        }
        const auto key = CacheKey(cache,CTransaction(tx),genesis);
        { LOCK(cs_main); cache.m_script_execution_cache.insert(key); }
        BOOST_CHECK_MESSAGE(!Validate(tx,spent,genesis,cache,ProductionPathTestAccess::Execution(genesis),work),names[kind]);
        { LOCK(cs_main); BOOST_CHECK(cache.m_script_execution_cache.contains(key,false)); }
        std::cout << "WO07C_JSON {\"scenario\":\"negative-production-entry\",\"case\":\"" << names[kind]
                  << "\",\"rejected\":true,\"group_verifications\":" << work.verification_calls << "}\n";
    }
    BOOST_CHECK(!Validate(good.tx,good.spent,Genesis(),cache,{},work));
    std::optional<Error> error;
    auto context = GroupContext::Prepare(*Execution(),CTransaction(good.tx),good.spent,STANDARD_SCRIPT_VERIFY_FLAGS,
                                         G(Genesis()),error);
    BOOST_REQUIRE(context);
    auto changed = good.spent; --changed[0].nValue;
    BOOST_CHECK(!context->Matches(CTransaction(good.tx),changed,STANDARD_SCRIPT_VERIFY_FLAGS,G(Genesis())));
    BOOST_CHECK(!context->Matches(CTransaction(good.tx),good.spent,MANDATORY_SCRIPT_VERIFY_FLAGS,G(Genesis())));
    auto no_witness = STANDARD_SCRIPT_VERIFY_FLAGS & ~SCRIPT_VERIFY_WITNESS;
    BOOST_CHECK(!GroupContext::Prepare(*Execution(),CTransaction(good.tx),good.spent,no_witness,G(Genesis()),error));
    const CTransaction foreign(good.tx);
    CScriptCheck wrong_job(good.spent[0],foreign,cache.m_signature_cache,0,STANDARD_SCRIPT_VERIFY_FLAGS,false,
                           &context->Native(),G(Genesis()),context);
    BOOST_CHECK(wrong_job().has_value());
}

BOOST_AUTO_TEST_CASE(actual_wallet_metadata_and_atomic_entropy_failures)
{
    auto owner = Wallet("metadata-owner",0x63);
    const auto first = ProductionPathTestAccess::Issue(*owner,0,0);
    const auto second = ProductionPathTestAccess::Issue(*owner,1,0);
    BOOST_REQUIRE(first && second);
    CMutableTransaction tx; std::vector<CTxOut> spent;
    std::vector<psbt::InputRecord> records;
    B32 point{}; point.fill(0x69);
    for (const auto& record : {*first,*second}) {
        const uint32_t index = tx.vin.size();
        tx.vin.emplace_back(COutPoint{Txid::FromUint256(G(point)),index});
        spent.emplace_back(1000,record.Script(Genesis())); records.push_back({record.pk,record.nonce,3});
        LOCK(cs_main); State().CoinsTip().AddCoin(tx.vin.back().prevout,Coin{CTxOut(spent.back()),1,false},false);
    }
    tx.vout.emplace_back(1900,first->Script(Genesis()));
    auto unsigned_psbt = psbt::Create(tx,spent,records,*Execution());
    const auto original = SerializeBytes(unsigned_psbt);
    size_t calls{0};
    ProductionPathTestAccess::Entropy(*owner,[&](B32& randomness) { randomness.fill(0); return ++calls < 2; });
    bool complete{true};
    BOOST_CHECK(owner->FillPSBT(unsigned_psbt,complete,SIGHASH_DEFAULT,true,false).has_value());
    BOOST_CHECK(!complete); BOOST_CHECK_EQUAL(calls,2);
    BOOST_CHECK(SerializeBytes(unsigned_psbt) == original);
    ProductionPathTestAccess::Entropy(*owner,{});
    auto both_groups = unsigned_psbt;
    size_t signed_inputs{0};
    BOOST_REQUIRE(!owner->FillPSBT(both_groups,complete,SIGHASH_DEFAULT,true,false,&signed_inputs,false));
    BOOST_CHECK(complete); BOOST_CHECK_EQUAL(signed_inputs,2);
    BOOST_CHECK_EQUAL(psbt::Inspect(both_groups,spent,*Execution()).verified_groups,2);
    auto stale = unsigned_psbt; Coin removed_coin; bool removed{false};
    ProductionPathTestAccess::Entropy(*owner,[&](B32& randomness) {
        randomness.fill(0);
        if (!removed) {
            LOCK(cs_main);
            removed = State().CoinsTip().SpendCoin(tx.vin[1].prevout,&removed_coin);
        }
        return true;
    });
    BOOST_CHECK(owner->FillPSBT(stale,complete,SIGHASH_DEFAULT,true,false).has_value());
    BOOST_CHECK(removed); BOOST_CHECK(!complete);
    BOOST_CHECK(SerializeBytes(stale) == original);
    { LOCK(cs_main); State().CoinsTip().AddCoin(tx.vin[1].prevout,std::move(removed_coin),false); }
    ProductionPathTestAccess::Entropy(*owner,{});
    auto missing = unsigned_psbt; missing.inputs[1].witness_utxo.nValue -= 1;
    const auto unchanged = SerializeBytes(missing);
    BOOST_CHECK(owner->FillPSBT(missing,complete,SIGHASH_DEFAULT,true,false).has_value());
    BOOST_CHECK(SerializeBytes(missing) == unchanged);
    const auto valid_records = ProductionPathTestAccess::Records(*owner);
    for (int kind = 0; kind < 7; ++kind) {
        auto corrupted = valid_records;
        if (kind == 0) corrupted.mac[0] ^= 1;
        if (kind == 1) corrupted.genesis[0] ^= 1;
        if (kind == 2) corrupted.issued.push_back(corrupted.issued[0]);
        if (kind == 3) corrupted.version = 2;
        if (kind == 4) {
            corrupted.issued[0].nonce[0] ^= 1;
            BOOST_REQUIRE(owner->WithMercaturaPQMasterSeed([&](const wallet::CKeyingMaterial& seed) {
                corrupted.mac = corrupted.Authenticate(seed); return true;
            }));
        }
        auto database = wallet::DuplicateMockDatabase(owner->GetDatabase());
        { auto batch = database->MakeBatch();
          BOOST_REQUIRE(batch->Write(wallet::DBKeys::PQGROUP_RECORDS,corrupted));
          if (kind == 5) BOOST_REQUIRE(batch->Erase(wallet::DBKeys::PQGROUP_RECORDS));
          if (kind == 6) BOOST_REQUIRE(batch->Erase(wallet::DBKeys::PQGROUP_REQUIRED)); }
        auto rejected = std::make_shared<wallet::CWallet>(m_node.chain.get(),"rejected",std::move(database));
        ProductionPathTestAccess::Attach(*rejected,Execution());
        { LOCK(rejected->cs_wallet);
          BOOST_CHECK(wallet::WalletBatch(rejected->GetDatabase()).LoadWallet(rejected.get()) == wallet::DBErrors::CORRUPT); }
        BOOST_CHECK(!rejected->GetPQGroupRecord(first->Script(Genesis())));
    }
    auto ordinary = std::make_shared<wallet::CWallet>(m_node.chain.get(),"ordinary",
        wallet::DuplicateMockDatabase(owner->GetDatabase()));
    { LOCK(ordinary->cs_wallet);
      BOOST_CHECK(wallet::WalletBatch(ordinary->GetDatabase()).LoadWallet(ordinary.get()) == wallet::DBErrors::CORRUPT); }
    BOOST_CHECK(!ProductionPathTestAccess::Issue(*ordinary));
    auto missing_metadata = psbt::Create(tx,spent,records,*Execution());
    missing_metadata.inputs[0].m_proprietary.erase(psbt::Field(1,{}));
    BOOST_CHECK_THROW(psbt::Inspect(missing_metadata,spent,*Execution()),Failure);
    auto contradicted = psbt::Create(tx,spent,records,*Execution());
    psbt::Put(contradicted.inputs[0],2,Bytes(4));
    BOOST_CHECK_THROW(psbt::Inspect(contradicted,spent,*Execution()),Failure);
    CMutableTransaction incomplete_result;
    auto incomplete = psbt::Create(tx,spent,records,*Execution());
    const auto incomplete_before = SerializeBytes(incomplete);
    BOOST_CHECK(!FinalizeAndExtractPSBT(incomplete,incomplete_result,*Execution(),spent));
    BOOST_CHECK(SerializeBytes(incomplete) == incomplete_before);
    std::cout << "WO07C_JSON {\"scenario\":\"actual-wallet-negative\",\"metadata_rejections\":7,"
                 "\"ordinary_grouped_load_rejected\":true,\"entropy_failure_atomic\":true,\"prevout_mismatch_atomic\":true}\n";
}

BOOST_AUTO_TEST_CASE(ordinary_unknown_witness_boundary_and_group_limits)
{
    auto good = Fixture(10,1,950);
    // Ordinary inactive consensus treats unknown witness versions permissively.
    // Standard flags reject them. Neither outcome is grouped authorization.
    BaseSignatureChecker ordinary;
    ScriptError error{SCRIPT_ERR_UNKNOWN_ERROR};
    const CScript empty;
    BOOST_CHECK(VerifyScript(empty,good.spent[0].scriptPubKey,&good.tx.vin[0].scriptWitness,
                            MANDATORY_SCRIPT_VERIFY_FLAGS,ordinary,&error));
    BOOST_CHECK(!VerifyScript(empty,good.spent[0].scriptPubKey,&good.tx.vin[0].scriptWitness,
                             STANDARD_SCRIPT_VERIFY_FLAGS,ordinary,&error));
    BOOST_CHECK(!ordinary.CheckPQGroupAuthorization(good.tx.vin[0].scriptWitness,Bytes(32),
                                                    STANDARD_SCRIPT_VERIFY_FLAGS,false));
    auto wrong_context = good.tx;
    const auto mapping = Reconstruct(good.tx,good.spent);
    const auto hashes = TransactionHashes(good.tx,good.spent,Genesis(),mapping);
    const auto wrong_signature = SignDigest(Digest(Genesis(),hashes,mapping.groups[0]),
        *good.keys[0],B32{},"Mercatura/PQAuth/v1");
    wrong_context.vin[0].scriptWitness.stack[0].assign(wrong_signature.begin(),wrong_signature.end());
    ValidationCache cache(1 << 20,1 << 20); Statistics work;
    BOOST_CHECK(!Validate(wrong_context,good.spent,Genesis(),cache,Execution(),work));
    // Limits are rejected in the production preflight, before ML-DSA work.
    auto too_many_members = good.tx; auto spent_members = good.spent;
    while (too_many_members.vin.size() <= 1024) {
        auto input = good.tx.vin[1]; input.prevout.n += too_many_members.vin.size() * 10000;
        too_many_members.vin.push_back(input); spent_members.push_back(good.spent[1]);
    }
    BOOST_CHECK(!Validate(too_many_members,spent_members,Genesis(),cache,Execution(),work));
    BOOST_CHECK_EQUAL(work.verification_calls,0);
    BOOST_CHECK_EXCEPTION(Reconstruct(too_many_members,spent_members),Failure,
        [](const Failure& failure) { return failure.code == Error::MembersLimit; });
    auto many_groups = Fixture(256,256,951);
    auto extra = many_groups.tx.vin[0]; extra.prevout.n += 9000000;
    extra.scriptWitness.stack[1][0] ^= 0xff;
    many_groups.tx.vin.push_back(extra); many_groups.spent.push_back(many_groups.spent[0]);
    BOOST_CHECK(!Validate(many_groups.tx,many_groups.spent,Genesis(),cache,Execution(),work));
    BOOST_CHECK_EQUAL(work.verification_calls,0);
    BOOST_CHECK_EXCEPTION(Reconstruct(many_groups.tx,many_groups.spent),Failure,
        [](const Failure& failure) { return failure.code == Error::GroupsLimit; });
    auto inputs = Fixture(4096,4,952);
    auto extra_input = inputs.tx.vin[4]; extra_input.prevout.n += 9000000;
    inputs.tx.vin.push_back(extra_input); inputs.spent.push_back(inputs.spent[4]);
    BOOST_CHECK(!Validate(inputs.tx,inputs.spent,Genesis(),cache,Execution(),work));
    BOOST_CHECK_EQUAL(work.verification_calls,0);
    BOOST_CHECK_EXCEPTION(Reconstruct(inputs.tx,inputs.spent),Failure,
        [](const Failure& failure) { return failure.code == Error::InputsLimit; });
    std::mt19937_64 random{0x574f303743505342ULL};
    std::vector<psbt::InputRecord> records;
    for (const auto& input : good.tx.vin) {
        B32 nonce; std::copy(input.scriptWitness.stack[input.scriptWitness.stack.size() == 3 ? 2 : 0].begin(),
            input.scriptWitness.stack[input.scriptWitness.stack.size() == 3 ? 2 : 0].end(),nonce.begin());
        records.push_back({good.keys[0]->pk,nonce,3});
    }
    const auto model = psbt::Create(good.tx,good.spent,records,*Execution());
    for (size_t i = 0; i < 250; ++i) {
        auto mutated = model;
        const size_t input = random() % mutated.inputs.size();
        auto nonce = *psbt::Get(mutated.inputs[input],1); nonce[random()%32] ^= 1U << (random()%8);
        mutated.inputs[input].m_proprietary.erase(psbt::Field(1,{})); psbt::Put(mutated.inputs[input],1,nonce);
        BOOST_CHECK_THROW(psbt::Inspect(mutated,good.spent,*Execution()),Failure);
    }
    std::cout << "WO07C_JSON {\"scenario\":\"ordinary-and-limits\",\"mandatory_unknown_v3_permissive\":true,"
                 "\"standard_unknown_v3_rejected\":true,\"excess_members_groups_inputs_rejected\":true,"
                 "\"psbt_mutations\":250,\"seed\":\"574f303743505342\"}\n";
}
BOOST_AUTO_TEST_SUITE_END()
