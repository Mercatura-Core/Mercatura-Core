// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/production_path/native_fixture.h>
#include <crypto/mercatura_pqderive.h>
#include <script/pqgroup_psbt.h>
#include <support/cleanse.h>
#include <wallet/coincontrol.h>
#include <wallet/spend.h>
#include <sqlite3.h>
#include <iostream>

using namespace mercaweave;
using namespace mercaweave::production_test;
namespace {
std::unique_ptr<wallet::WalletDatabase> SQLite(const fs::path& path)
{
    fs::create_directories(path);
    wallet::DatabaseOptions options; options.use_unsafe_sync = false;
    return std::make_unique<wallet::SQLiteDatabase>(path,path/"wallet.dat",options);
}
std::map<COutPoint,Coin> Coins(const CTransaction& tx, const std::vector<CTxOut>& spent)
{
    std::map<COutPoint,Coin> coins;
    for (size_t i = 0; i < spent.size(); ++i) coins.emplace(tx.vin[i].prevout,Coin{CTxOut(spent[i]),1,false});
    return coins;
}
}

BOOST_FIXTURE_TEST_SUITE(wallet_tests_cwallet, NativeSetup)
BOOST_AUTO_TEST_CASE(ownership_funding_raw_signing_and_multiple_signer_psbt)
{
    ProductionPathTestAccess::Attach(State(),Execution());
    auto alice = Wallet("alice",0x41);
    auto bob = Wallet("bob",0x42);
    auto handler_a = m_node.chain->handleNotifications(alice);
    auto handler_b = m_node.chain->handleNotifications(bob);
    std::vector<wallet::PQGroupRecord> addresses;
    for (auto* wallet : {alice.get(),bob.get()}) {
        for (int i = 0; i < 2; ++i) {
            auto record = ProductionPathTestAccess::Issue(*wallet);
            BOOST_REQUIRE(record); addresses.push_back(*record);
        }
    }
    const auto change = ProductionPathTestAccess::Issue(*alice,0,1); BOOST_REQUIRE(change);
    BOOST_CHECK(addresses[0].pk == addresses[1].pk);
    BOOST_CHECK(addresses[0].pk != addresses[2].pk);
    BOOST_CHECK(addresses[0].Script(Genesis()) != addresses[1].Script(Genesis()));
    BOOST_CHECK(change->pk != addresses[0].pk);

    CMutableTransaction funding;
    B32 funding_id{}; funding_id.fill(0x91);
    const COutPoint source{Txid::FromUint256(uint256{std::span<const unsigned char>{funding_id}}),0};
    funding.vin.emplace_back(source);
    for (const auto& address : addresses) funding.vout.emplace_back(1000,address.Script(Genesis()));
    auto native_a = alice->GetNewMercaturaPQDestination(false);
    auto native_b = bob->GetNewMercaturaPQDestination(false);
    BOOST_REQUIRE(native_a && native_b);
    funding.vout.emplace_back(1000,GetScriptForDestination(*native_a));
    funding.vout.emplace_back(1000,GetScriptForDestination(*native_b));
    auto recovery_metadata = wallet::DuplicateMockDatabase(alice->GetDatabase());
    { LOCK(cs_main); State().CoinsTip().AddCoin(source,Coin{CTxOut{7000,CScript() << OP_TRUE},1,false},false); }
    Connect(Block({funding}));
    BOOST_CHECK_EQUAL(wallet::GetBalance(*alice).m_mine_trusted,3000);
    BOOST_CHECK_EQUAL(wallet::GetBalance(*bob).m_mine_trusted,3000);
    {
        LOCK(alice->cs_wallet);
        BOOST_CHECK(alice->IsMine(addresses[0].Script(Genesis())));
        BOOST_CHECK(!alice->IsMine(addresses[2].Script(Genesis())));
        BOOST_CHECK_EQUAL(wallet::AvailableCoins(*alice).Size(),3);
    }

    // Genuine native CreateTransaction, selected coins, change and exact grouped
    // size/fee calculation. This is not the old Funding research facade.
    wallet::CCoinControl control;
    control.m_allow_other_inputs = false;
    control.m_feerate = CFeeRate(1); control.fOverrideFeeRate = true;
    const CTransaction funding_tx(funding);
    control.Select(COutPoint(funding_tx.GetHash(),0)); control.Select(COutPoint(funding_tx.GetHash(),1));
    const std::vector<wallet::CRecipient> recipients{{*native_b,100,false}};
    auto created = wallet::CreateTransaction(*alice,recipients,std::nullopt,control,true);
    const std::string creation_error = created ? std::string{} : util::ErrorString(created).original;
    BOOST_REQUIRE_MESSAGE(bool(created),creation_error);
    BOOST_CHECK(created->change_pos.has_value());
    std::vector<CTxOut> created_spent;
    for (const auto& input : created->tx->vin) created_spent.push_back(funding.vout[input.prevout.n]);
    const auto estimate = wallet::CalculateMaximumSignedTxSize(*created->tx,alice.get(),created_spent);
    BOOST_CHECK_EQUAL(estimate.vsize,Measure(CMutableTransaction(*created->tx)).total);
    BOOST_CHECK_EQUAL(estimate.weight,Measure(CMutableTransaction(*created->tx)).weight);
    BOOST_CHECK_EQUAL(created->fee,2000-created->tx->GetValueOut());
    CMutableTransaction draft;
    auto funded = wallet::FundTransaction(*alice,draft,recipients,std::nullopt,false,control);
    const std::string funding_error = funded ? std::string{} : util::ErrorString(funded).original;
    BOOST_REQUIRE_MESSAGE(bool(funded),funding_error);
    auto funded_psbt = PartiallySignedTransaction(CMutableTransaction(*funded->tx));
    bool funded_complete{false};
    BOOST_REQUIRE(!alice->FillPSBT(funded_psbt,funded_complete,SIGHASH_DEFAULT,true,false));
    BOOST_CHECK(funded_complete);
    std::vector<CTxOut> funded_spent;
    for (const auto& input : funded->tx->vin) funded_spent.push_back(funding.vout[input.prevout.n]);
    CMutableTransaction funded_extract;
    BOOST_REQUIRE(FinalizeAndExtractPSBT(funded_psbt,funded_extract,*Execution(),funded_spent));
    BOOST_CHECK(Verify(funded_extract,funded_spent,Genesis()).valid);
    BOOST_CHECK_EQUAL(funded->fee,2000-CTransaction(funded_extract).GetValueOut());

    CMutableTransaction payment;
    // Interleave groups; every group uses absolute vin indexes, and two native
    // inputs retain their independent original authorization.
    for (const auto index : {0,2,1,3,4,5}) payment.vin.emplace_back(COutPoint(funding_tx.GetHash(),index));
    payment.vout.emplace_back(3000,GetScriptForDestination(*native_a));
    payment.vout.emplace_back(2900,change->Script(Genesis()));
    std::vector<CTxOut> spent;
    for (const auto& input : payment.vin) spent.push_back(funding.vout[input.prevout.n]);
    std::vector<psbt::InputRecord> public_records;
    for (const auto index : {0,2,1,3}) public_records.push_back({addresses[index].pk,addresses[index].nonce,3});
    for (auto* wallet : {alice.get(),bob.get()}) {
        const auto output = wallet == alice.get() ? funding.vout[4] : funding.vout[5];
        CTxDestination destination; BOOST_REQUIRE(ExtractDestination(output.scriptPubKey,destination));
        const auto native = std::get<WitnessV2MercaturaPQ>(destination);
        MercaturaPQKeyCommitment commitment{}; std::copy(native.begin(),native.end(),commitment.begin());
        PublicKey key{};
        BOOST_REQUIRE(wallet->WithMercaturaPQMasterSeed([&](const wallet::CKeyingMaterial& master) {
            const auto location = wallet->GetMercaturaPQKeyLocator(commitment); if (!location) return false;
            auto child = DeriveMercaturaPQChildSeed(std::span<const unsigned char,32>{master.data(),32},
                Params().GetConsensus().hashGenesisBlock,location->account,location->branch,location->index);
            if (!child) return false;
            mercaweave::KeyPair pair(*child); key = pair.pk; memory_cleanse(child->data(),child->size()); return true;
        }));
        public_records.push_back({key,{},2});
    }
    auto unsigned_psbt = psbt::Create(payment,spent,public_records,*Execution());
    auto left = unsigned_psbt, right = unsigned_psbt;
    bool complete_a{true}, complete_b{true}; size_t signed_a{},signed_b{};
    BOOST_REQUIRE(!alice->FillPSBT(left,complete_a,SIGHASH_DEFAULT,true,false,&signed_a,false));
    BOOST_REQUIRE(!bob->FillPSBT(right,complete_b,SIGHASH_DEFAULT,true,false,&signed_b,false));
    BOOST_CHECK(!complete_a && !complete_b); BOOST_CHECK_EQUAL(signed_a,3); BOOST_CHECK_EQUAL(signed_b,3);
    auto partial = left; CMutableTransaction extracted;
    BOOST_CHECK(!FinalizeAndExtractPSBT(partial,extracted,*Execution(),spent));
    PartiallySignedTransaction combined;
    BOOST_REQUIRE(CombinePSBTs(combined,{left,right},*Execution(),spent));
    BOOST_REQUIRE(FinalizeAndExtractPSBT(combined,extracted,*Execution(),spent));
    const auto info = psbt::Inspect(combined,spent,*Execution());
    BOOST_CHECK_EQUAL(info.verified_groups,2); BOOST_CHECK_EQUAL(info.verified_native,2);
    BOOST_CHECK_EQUAL(extracted.vin[2].scriptWitness.stack[1].size(),4);
    BOOST_CHECK_EQUAL(ReadLE32(extracted.vin[2].scriptWitness.stack[1].data()),0);
    auto changed = left; --changed.tx->vout[0].nValue;
    BOOST_CHECK(!CombinePSBTs(combined,{changed,right},*Execution(),spent));
    const auto before_changed = SerializeBytes(changed);
    bool rejected_complete{true};
    BOOST_CHECK(alice->FillPSBT(changed,rejected_complete,SIGHASH_DEFAULT,true,false).has_value());
    BOOST_CHECK(SerializeBytes(changed) == before_changed);
    auto conflicting = right;
    auto field = *psbt::Get(conflicting.inputs[1],4); field[100] ^= 1;
    conflicting.inputs[1].m_proprietary.erase(psbt::Field(4,{})); psbt::Put(conflicting.inputs[1],4,field);
    BOOST_CHECK(!CombinePSBTs(combined,{left,conflicting},*Execution(),spent));
    // Compatible opaque metadata survives the actual native merge; conflicting
    // values at all three PSBT locations must reject atomically.
    for (int location = 0; location < 3; ++location) {
        auto one = left, two = right;
        auto& a = location == 0 ? one.unknown : location == 1 ? one.inputs[0].unknown : one.outputs[0].unknown;
        auto& b = location == 0 ? two.unknown : location == 1 ? two.inputs[0].unknown : two.outputs[0].unknown;
        a[Bytes{0x80,1}] = Bytes{1}; b[Bytes{0x80,1}] = Bytes{2};
        const auto unchanged_combined = SerializeBytes(combined);
        BOOST_CHECK(!CombinePSBTs(combined,{one,two},*Execution(),spent));
        BOOST_CHECK(SerializeBytes(combined) == unchanged_combined);
        b[Bytes{0x80,1}] = Bytes{1};
        BOOST_REQUIRE(CombinePSBTs(combined,{one,two},*Execution(),spent));
        const auto& kept = location == 0 ? combined.unknown : location == 1 ? combined.inputs[0].unknown : combined.outputs[0].unknown;
        BOOST_CHECK(kept.at(Bytes{0x80,1}) == Bytes{1});
    }
    // Exercise the real wallet notifications, accounting and UTXO spend path.
    const auto payment_block = Block({extracted}); Connect(payment_block);
    BOOST_CHECK_EQUAL(wallet::GetBalance(*alice).m_mine_trusted,5900);
    BOOST_CHECK_EQUAL(wallet::GetBalance(*bob).m_mine_trusted,0);
    auto recovered = Reload("rescanned",std::move(recovery_metadata));
    BOOST_CHECK(recovered->mapWallet.empty());
    {
        wallet::WalletRescanReserver reserver(*recovered); BOOST_REQUIRE(reserver.reserve());
        const auto result = recovered->ScanForWalletTransactions(Params().GetConsensus().hashGenesisBlock,
            0,{},reserver,true,false);
        BOOST_CHECK(result.status == wallet::CWallet::ScanResult::SUCCESS);
    }
    BOOST_CHECK_EQUAL(wallet::GetBalance(*recovered).m_mine_trusted,5900);
    const auto payment_index = WITH_LOCK(cs_main,return m_node.chainman->m_blockman.LookupBlockIndex(payment_block.GetHash()));
    Invalidate(payment_index);
    const auto payment_id = CTransaction(extracted).GetHash();
    for (auto* owner : {alice.get(),bob.get()}) {
        LOCK(owner->cs_wallet);
        const auto* transaction = owner->GetWalletTx(payment_id);
        BOOST_REQUIRE(transaction);
        BOOST_CHECK(transaction->state<wallet::TxStateInactive>());
        BOOST_CHECK(!transaction->isAbandoned());
    }
    // Disconnection retains unconfirmed wallet spends, even though actual
    // chain coins are restored. Native abandonment releases the reservation.
    for (const auto& input : extracted.vin)
        BOOST_CHECK(WITH_LOCK(cs_main,return State().CoinsTip().HaveCoin(input.prevout)));
    BOOST_CHECK_EQUAL(wallet::GetBalance(*alice).m_mine_trusted,0);
    BOOST_CHECK_EQUAL(wallet::GetBalance(*bob).m_mine_trusted,0);
    BOOST_REQUIRE(alice->AbandonTransaction(payment_id));
    BOOST_REQUIRE(bob->AbandonTransaction(payment_id));
    BOOST_CHECK_EQUAL(wallet::GetBalance(*alice).m_mine_trusted,3000);
    BOOST_CHECK_EQUAL(wallet::GetBalance(*bob).m_mine_trusted,3000);
    { LOCK(cs_main); State().ResetBlockFailureFlags(payment_index); m_node.chainman->RecalculateBestHeader(); }
    BlockValidationState reconsidered; BOOST_REQUIRE(State().ActivateBestChain(reconsidered));
    m_node.chain->waitForNotificationsIfTipChanged({});
    BOOST_CHECK_EQUAL(wallet::GetBalance(*alice).m_mine_trusted,5900);
    BOOST_CHECK_EQUAL(wallet::GetBalance(*bob).m_mine_trusted,0);
    std::cout << "WO07C_JSON {\"scenario\":\"actual-cwallet-multisigner\",\"groups\":2,\"native_signatures\":2,"
                 "\"fee_units\":100,\"alice_balance\":5900,\"bob_balance\":0,\"actual_create_transaction\":true}\n";
}

BOOST_AUTO_TEST_CASE(native_sqlite_encryption_backup_restore_rotation_and_failed_issuance)
{
    auto owner = Wallet("sqlite-owner",0x51,SQLite(m_path_root/"owner"));
    std::vector<wallet::PQGroupRecord> issued;
    for (int i = 0; i < 65; ++i) {
        const auto address = ProductionPathTestAccess::Issue(*owner); BOOST_REQUIRE(address); issued.push_back(*address);
    }
    BOOST_CHECK(issued[0].pk == issued[63].pk);
    BOOST_CHECK(issued[63].pk != issued[64].pk);
    BOOST_CHECK_EQUAL(issued[64].group,1); BOOST_CHECK_EQUAL(issued[64].address,0);
    const SecureString passphrase{"public WO07C isolated wallet passphrase"};
    BOOST_REQUIRE(owner->EncryptWallet(passphrase));
    BOOST_CHECK(owner->IsLocked());
    BOOST_CHECK(!ProductionPathTestAccess::Issue(*owner));
    BOOST_REQUIRE(!owner->Unlock(SecureString{"wrong public test password"}));
    BOOST_REQUIRE(owner->Unlock(passphrase));
    const auto native = owner->GetNewMercaturaPQDestination(false); BOOST_REQUIRE(native);
    CMutableTransaction tx;
    B32 point{}; point.fill(0x31);
    tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256{std::span<const unsigned char>{point}}),0});
    tx.vout.emplace_back(950,GetScriptForDestination(*native));
    std::vector<CTxOut> spent{{1000,issued[0].Script(Genesis())}};
    auto coins = Coins(CTransaction(tx),spent);
    std::map<int,bilingual_str> errors;
    owner->Lock(); const auto unsigned_bytes = Wire(tx);
    { LOCK(owner->cs_wallet); BOOST_CHECK(!owner->SignTransaction(tx,coins,SIGHASH_DEFAULT,errors)); }
    BOOST_CHECK(Wire(tx) == unsigned_bytes);
    BOOST_REQUIRE(owner->Unlock(passphrase));
    { LOCK(owner->cs_wallet); BOOST_REQUIRE(owner->SignTransaction(tx,coins,SIGHASH_DEFAULT,errors)); }
    BOOST_REQUIRE(Verify(tx,spent,Genesis()).valid);
    const auto signed_bytes = Wire(tx);
    { LOCK(owner->cs_wallet); BOOST_REQUIRE(owner->SignTransaction(tx,coins,SIGHASH_DEFAULT,errors)); }
    BOOST_CHECK(Wire(tx) == signed_bytes);
    const auto backup = m_path_root/"encrypted-backup.dat";
    BOOST_REQUIRE(owner->GetDatabase().Backup(fs::PathToString(backup)));
    {
        auto batch = owner->GetDatabase().MakeBatch(); wallet::CKeyingMaterial plain;
        BOOST_CHECK(!batch->Read(wallet::DBKeys::MERCATURA_PQ_SEED,plain));
        wallet::MercaturaPQCryptedSeed encrypted;
        BOOST_CHECK(batch->Read(wallet::DBKeys::MERCATURA_PQ_CRYPTED_SEED,encrypted));
        BOOST_CHECK(encrypted.IsStructurallyValid());
    }
    owner.reset();
    auto reloaded = Reload("sqlite-reloaded",SQLite(m_path_root/"owner"));
    BOOST_CHECK(reloaded->IsLocked());
    BOOST_CHECK(!reloaded->GetPQGroupRecord(issued[0].Script(Genesis())));
    BOOST_REQUIRE(reloaded->Unlock(passphrase));
    BOOST_CHECK(reloaded->GetPQGroupRecord(issued[0].Script(Genesis())).has_value());
    reloaded->Lock(); BOOST_CHECK(reloaded->GetPQGroupRecord(issued[0].Script(Genesis())).has_value());
    BOOST_REQUIRE(reloaded->Unlock(passphrase));
    const auto next = ProductionPathTestAccess::Issue(*reloaded); BOOST_REQUIRE(next);
    BOOST_CHECK_EQUAL(next->group,1); BOOST_CHECK_EQUAL(next->address,1);

    fs::create_directories(m_path_root/"restored");
    fs::copy_file(backup,m_path_root/"restored"/"wallet.dat",fs::copy_options::none);
    auto restored = Reload("sqlite-restored",SQLite(m_path_root/"restored"));
    BOOST_REQUIRE(restored->Unlock(passphrase));
    const auto recovered_next = ProductionPathTestAccess::Issue(*restored); BOOST_REQUIRE(recovered_next);
    BOOST_CHECK(*next == *recovered_next);
    // Reject COMMIT inside the actual SQLite connection, after INSERT succeeds.
    // Native rollback must preserve both disk metadata and the in-memory range.
    auto& sqlite = dynamic_cast<wallet::SQLiteDatabase&>(restored->GetDatabase());
    const auto committed_records = ProductionPathTestAccess::Records(*restored);
    BOOST_REQUIRE_EQUAL(sqlite3_set_authorizer(sqlite.m_db,
        [](void*,int action,const char* operation,const char*,const char*,const char*) {
            return action == SQLITE_TRANSACTION && operation && std::string_view(operation) == "COMMIT" ? SQLITE_DENY : SQLITE_OK;
        },nullptr),SQLITE_OK);
    BOOST_CHECK(!ProductionPathTestAccess::Issue(*restored));
    BOOST_REQUIRE_EQUAL(sqlite3_set_authorizer(sqlite.m_db,nullptr,nullptr),SQLITE_OK);
    BOOST_CHECK(SerializeBytes(ProductionPathTestAccess::Records(*restored)) == SerializeBytes(committed_records));
    wallet::PQGroupRecords on_disk;
    { auto batch = sqlite.MakeBatch(); BOOST_REQUIRE(batch->Read(wallet::DBKeys::PQGROUP_RECORDS,on_disk)); }
    BOOST_CHECK(SerializeBytes(on_disk) == SerializeBytes(committed_records));
    const auto after_failed_commit = ProductionPathTestAccess::Issue(*restored); BOOST_REQUIRE(after_failed_commit);
    BOOST_CHECK_EQUAL(after_failed_commit->address,2);
    // Authenticated encrypted metadata is provisional until native unlock.
    fs::create_directories(m_path_root/"corrupt-encrypted");
    fs::copy_file(backup,m_path_root/"corrupt-encrypted"/"wallet.dat",fs::copy_options::none);
    auto corrupt_database = SQLite(m_path_root/"corrupt-encrypted");
    { auto batch = corrupt_database->MakeBatch(); wallet::PQGroupRecords metadata;
      BOOST_REQUIRE(batch->Read(wallet::DBKeys::PQGROUP_RECORDS,metadata)); metadata.mac[0] ^= 1;
      BOOST_REQUIRE(batch->Write(wallet::DBKeys::PQGROUP_RECORDS,metadata)); }
    auto corrupt = Reload("corrupt-encrypted",std::move(corrupt_database));
    BOOST_CHECK(!corrupt->Unlock(passphrase)); BOOST_CHECK(corrupt->IsLocked());
    BOOST_CHECK(!corrupt->GetPQGroupRecord(issued[0].Script(Genesis())));
    { LOCK(restored->cs_wallet); auto raw = tx; raw.vin[0].scriptWitness.SetNull();
      BOOST_REQUIRE(restored->SignTransaction(raw,coins,SIGHASH_DEFAULT,errors)); BOOST_CHECK(Verify(raw,spent,Genesis()).valid); }
    auto fault = Wallet("fault",0x61);
    wallet::GetMockableDatabase(*fault).m_pass = false;
    BOOST_CHECK(!ProductionPathTestAccess::Issue(*fault));
    wallet::GetMockableDatabase(*fault).m_pass = true;
    const auto first = ProductionPathTestAccess::Issue(*fault); BOOST_REQUIRE(first);
    BOOST_CHECK_EQUAL(first->group,0); BOOST_CHECK_EQUAL(first->address,0);
    // Ordinary wallet objects have no receiving or ownership capability.
    ProductionPathTestAccess::Attach(*fault,{});
    BOOST_CHECK(!ProductionPathTestAccess::Issue(*fault));
    BOOST_CHECK(!fault->GetPQGroupRecord(first->Script(Genesis())));
    const auto ordinary_destination = fault->GetNewMercaturaPQDestination(false);
    BOOST_REQUIRE(ordinary_destination); BOOST_CHECK(std::holds_alternative<WitnessV2MercaturaPQ>(*ordinary_destination));
    std::cout << "WO07C_JSON {\"scenario\":\"actual-cwallet-sqlite\",\"issued\":65,\"rotation\":64,"
                 "\"encrypted_seed_only\":true,\"locked_signing_rejected\":true,\"restart\":true,\"backup_restore\":true,"
                 "\"failed_write_no_issued_index\":true,\"ordinary_v3_issue\":false}\n";
}
BOOST_AUTO_TEST_SUITE_END()
