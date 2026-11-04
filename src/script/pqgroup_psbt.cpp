// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <script/pqgroup_psbt.h>
#include <crypto/mercatura_pqkey.h>

namespace mercaweave::psbt {
namespace {
const Bytes ID{'M','e','r','c','a','W','e','a','v','e'};
void Require(bool value) { if (!value) throw Failure(Error::Context); }
void Properties(const std::set<PSBTProprietary>& properties, bool input)
{
    for (const auto& property : properties) {
        Require(property.key.size() <= 4096 && property.value.size() <= 4 * 1024 * 1024);
        Reader reader(property.key);
        Require(ReadCompactSize(reader) == PSBT_IN_PROPRIETARY);
        const auto length = ReadCompactSize(reader);
        Require(length <= reader.remaining());
        Bytes identifier(length);
        reader.read(std::as_writable_bytes(std::span(identifier)));
        const auto subtype = ReadCompactSize(reader);
        Require(identifier == property.identifier && subtype == property.subtype);
        if (identifier == ID) {
            Require(input && reader.remaining() == 0 && subtype >= 1 && subtype <= 4);
            Require(property.value.size() == (subtype == 1 ? 32 : subtype == 2 ? 4 : subtype == 3 ? 1952 : 3309));
        } else if (identifier == Bytes{'M','e','r','c','a','t','u','r','a'}) {
            std::string error;
            Require(ValidateMercaturaPQPSBTProprietary(property,reader.remaining() != 0,error));
        }
    }
}
void Unknown(const std::map<Bytes,Bytes>& fields)
{
    for (const auto& [key,value] : fields)
        Require(!key.empty() && key[0] >= 0x80 && key[0] != 0xfc &&
                key.size() <= 4096 && value.size() <= 4 * 1024 * 1024);
}
}
PSBTProprietary Field(uint64_t subtype, std::span<const unsigned char> value)
{
    Writer writer;
    WriteCompactSize(writer, PSBT_IN_PROPRIETARY); writer << ID;
    WriteCompactSize(writer, subtype);
    return {subtype, ID, std::move(writer.bytes), Bytes(value.begin(),value.end())};
}
const Bytes* Get(const PSBTInput& input, uint64_t subtype)
{
    const auto found = input.m_proprietary.find(Field(subtype,{}));
    return found == input.m_proprietary.end() ? nullptr : &found->value;
}
void Put(PSBTInput& input, uint64_t subtype, std::span<const unsigned char> value)
{
    auto field = Field(subtype,value);
    const auto found = input.m_proprietary.find(field);
    Require(found == input.m_proprietary.end() || found->value == field.value);
    input.m_proprietary.insert(std::move(field));
}
PartiallySignedTransaction Create(const CMutableTransaction& tx, std::span<const CTxOut> spent,
                                 std::span<const InputRecord> records, const IsolatedExecution& execution)
{
    Require(tx.vin.size() == spent.size() && spent.size() == records.size() && spent.size() <= MAX_INPUTS);
    CMutableTransaction unsigned_tx(tx);
    for (auto& input : unsigned_tx.vin) { input.scriptSig.clear(); input.scriptWitness.SetNull(); }
    PartiallySignedTransaction result(unsigned_tx);
    std::map<PublicKey,uint32_t> anchors;
    for (uint32_t i = 0; i < spent.size(); ++i) {
        const auto& record = records[i]; auto& input = result.inputs[i];
        input.witness_utxo = spent[i];
        if (record.witness_version == 3) {
            Require(Output(execution.Genesis(),record.pk,record.nonce) == spent[i].scriptPubKey);
            Put(input,1,record.nonce);
            const auto [anchor,first] = anchors.emplace(record.pk,i);
            if (first) Put(input,3,record.pk);
            else { Writer reference; reference << anchor->second; Put(input,2,reference.bytes); }
        } else {
            Require(record.witness_version == 2 && SetMercaturaPQPSBTPublicKey(input,record.pk));
        }
    }
    Inspect(result,spent,execution);
    return result;
}
bool VerifyNative(const CMutableTransaction& tx, const std::vector<CTxOut>& spent,
                  const B32& genesis, uint32_t index, const PrecomputedTransactionData& native)
{
    if (index >= spent.size()) return false;
    ScriptError error{SCRIPT_ERR_UNKNOWN_ERROR};
    return VerifyScript(tx.vin[index].scriptSig, spent[index].scriptPubKey,
        &tx.vin[index].scriptWitness, MANDATORY_SCRIPT_VERIFY_FLAGS,
        MutableTransactionSignatureChecker(&tx,index,spent[index].nValue,native,
            MissingDataBehavior::FAIL,uint256{std::span<const unsigned char>{genesis}}), &error);
}
Inspection Inspect(const PartiallySignedTransaction& source, std::span<const CTxOut> spent,
                   const IsolatedExecution& execution)
{
    Require(source.tx && source.GetVersion() == 0 && source.m_xpubs.empty() &&
        !source.inputs.empty() && source.inputs.size() <= MAX_INPUTS &&
        source.inputs.size() == source.tx->vin.size() && source.outputs.size() == source.tx->vout.size() &&
        source.inputs.size() == spent.size());
    Unknown(source.unknown); Properties(source.m_proprietary,false);
    for (const auto& output : source.outputs) { Unknown(output.unknown); Properties(output.m_proprietary,false); }
    Inspection out{*source.tx,std::vector<CTxOut>(spent.begin(),spent.end()),{},{},std::vector<bool>(spent.size(),false)};
    for (uint32_t i = 0; i < spent.size(); ++i) {
        const auto& input = source.inputs[i];
        Require(out.tx.vin[i].scriptSig.empty() && out.tx.vin[i].scriptWitness.IsNull());
        Unknown(input.unknown); Properties(input.m_proprietary,true);
        Require(!input.sighash_type || *input.sighash_type == SIGHASH_DEFAULT);
        Require(input.partial_sigs.empty() && input.hd_keypaths.empty() &&
            input.redeem_script.empty() && input.witness_script.empty() && input.final_script_sig.empty() &&
            input.m_tap_key_sig.empty() && input.m_tap_script_sigs.empty() && input.m_tap_scripts.empty() &&
            input.m_tap_bip32_paths.empty() && !input.m_tap_internal_key.IsFullyValid() &&
            input.m_tap_merkle_root.IsNull() && input.m_musig2_participants.empty() &&
            input.m_musig2_pubnonces.empty() && input.m_musig2_partial_sigs.empty());
        Require(!input.witness_utxo.IsNull() && input.witness_utxo == spent[i]);
        if (input.non_witness_utxo) {
            const auto point = out.tx.vin[i].prevout;
            Require(input.non_witness_utxo->GetHash() == point.hash &&
                point.n < input.non_witness_utxo->vout.size() && input.non_witness_utxo->vout[point.n] == spent[i]);
        }
        int version; Bytes program;
        Require(spent[i].scriptPubKey.IsWitnessProgram(version,program) && program.size() == 32 && (version == 2 || version == 3));
        const auto nonce = Get(input,1), reference = Get(input,2), key = Get(input,3), signature = Get(input,4);
        if (version == 3) {
            Require(nonce && ((key && !reference) || (!key && reference && !signature)));
            Require(!GetMercaturaPQPSBTPublicKey(input) && !GetMercaturaPQPSBTSignature(input));
            // Construct directly into the owned witness. Nested conditional
            // initializer-list temporaries made allocation-failure unwinding
            // unsafe on the qualified toolchain (WO08 sanitizer regression).
            auto& witness = out.tx.vin[i].scriptWitness.stack;
            witness.reserve(key ? 3 : 2);
            if (key) {
                if (signature) witness.push_back(*signature);
                else witness.emplace_back(3309);
                witness.push_back(*key);
                witness.push_back(*nonce);
            } else {
                witness.push_back(*nonce);
                witness.push_back(*reference);
            }
        } else {
            Require(!nonce && !reference && !key && !signature);
            const auto native_key = GetMercaturaPQPSBTPublicKey(input);
            Require(native_key && native_key->size() == 1952);
            MercaturaPQKeyCommitment commitment{};
            Require(ComputeMercaturaPQKeyCommitmentV1(commitment,*native_key) &&
                std::equal(commitment.begin(),commitment.end(),program.begin()));
            const auto native_signature = GetMercaturaPQPSBTSignature(input);
            Require(!native_signature || native_signature->size() == 3309);
            auto& witness = out.tx.vin[i].scriptWitness.stack;
            witness.reserve(2);
            if (native_signature) witness.push_back(*native_signature);
            else witness.emplace_back(3309);
            witness.push_back(*native_key);
        }
    }
    out.mapping = Reconstruct(out.tx,out.spent);
    Statistics work; CheckOwnership(out.mapping,out.spent,execution.Genesis(),work);
    out.hashes = TransactionHashes(out.tx,out.spent,execution.Genesis(),out.mapping);
    for (const auto& group : out.mapping.groups) if (const auto signature = Get(source.inputs[group.anchor],4)) {
        Require(VerifyDigest(Digest(execution.Genesis(),out.hashes,group),group.pk,*signature));
        ++out.verified_groups;
        for (const auto& member : group.members) out.complete[member.index] = true;
    }
    PrecomputedTransactionData native; auto outputs = out.spent;
    native.Init(out.tx,std::move(outputs),true);
    for (uint32_t i = 0; i < spent.size(); ++i) {
        if (GetMercaturaPQPSBTSignature(source.inputs[i])) {
            Require(VerifyNative(out.tx,out.spent,execution.Genesis(),i,native));
            ++out.verified_native; out.complete[i] = true;
        }
        if (!source.inputs[i].final_script_witness.IsNull())
            Require(out.complete[i] && source.inputs[i].final_script_witness.stack == out.tx.vin[i].scriptWitness.stack);
    }
    return out;
}
} // namespace mercaweave::psbt

bool FinalizePSBT(PartiallySignedTransaction& source, const mercaweave::IsolatedExecution& execution,
                  std::span<const CTxOut> spent)
{
    try {
        const auto info = mercaweave::psbt::Inspect(source,spent,execution);
        auto staged = source;
        for (size_t i = 0; i < info.complete.size(); ++i)
            if (info.complete[i]) staged.inputs[i].final_script_witness = info.tx.vin[i].scriptWitness;
        source = std::move(staged);
        return std::all_of(info.complete.begin(),info.complete.end(),[](bool value) { return value; });
    } catch (const mercaweave::Failure&) { return false; }
}
bool FinalizeAndExtractPSBT(PartiallySignedTransaction& source, CMutableTransaction& result,
                           const mercaweave::IsolatedExecution& execution, std::span<const CTxOut> spent)
{
    auto staged = source;
    if (!FinalizePSBT(staged,execution,spent)) return false;
    const auto info = mercaweave::psbt::Inspect(staged,spent,execution);
    result = info.tx; source = std::move(staged); return true;
}
bool CombinePSBTs(PartiallySignedTransaction& result, const std::vector<PartiallySignedTransaction>& sources,
                  const mercaweave::IsolatedExecution& execution, std::span<const CTxOut> spent)
{
    if (sources.empty()) return false;
    try {
        auto staged = sources.front();
        mercaweave::psbt::Inspect(staged,spent,execution);
        for (const auto& source : std::span(sources).subspan(1)) {
            mercaweave::psbt::Inspect(source,spent,execution);
            if (mercaweave::Wire(*staged.tx,false) != mercaweave::Wire(*source.tx,false)) return false;
            const auto properties = [](const auto& left, const auto& right) {
                for (const auto& field : left) {
                    const auto found = right.find(field);
                    if (found != right.end() && found->value != field.value) return false;
                }
                return true;
            };
            const auto unknown = [](const auto& left, const auto& right) {
                for (const auto& [key,value] : left) {
                    const auto found = right.find(key);
                    if (found != right.end() && found->second != value) return false;
                }
                return true;
            };
            if (!properties(staged.m_proprietary,source.m_proprietary) || !unknown(staged.unknown,source.unknown)) return false;
            for (size_t i = 0; i < staged.inputs.size(); ++i) {
                for (const auto& field : staged.inputs[i].m_proprietary) {
                    const auto other = source.inputs[i].m_proprietary.find(field);
                    if (other != source.inputs[i].m_proprietary.end() && other->value != field.value) return false;
                }
                if (!unknown(staged.inputs[i].unknown,source.inputs[i].unknown)) return false;
                if (!staged.inputs[i].final_script_witness.IsNull() && !source.inputs[i].final_script_witness.IsNull() &&
                    staged.inputs[i].final_script_witness.stack != source.inputs[i].final_script_witness.stack) return false;
            }
            for (size_t i = 0; i < staged.outputs.size(); ++i) {
                if (!properties(staged.outputs[i].m_proprietary,source.outputs[i].m_proprietary) ||
                    !unknown(staged.outputs[i].unknown,source.outputs[i].unknown)) return false;
                // Conflicting output derivation/script metadata must not be
                // silently resolved by the native first-value-wins merge.
                auto left = staged.outputs[i], right = source.outputs[i];
                left.unknown.clear(); right.unknown.clear();
                left.m_proprietary.clear(); right.m_proprietary.clear();
                mercaweave::Writer a,b; a << left; b << right;
                if (a.bytes != b.bytes) return false;
            }
            if (!staged.Merge(source)) return false; // genuine native PSBT merge
            staged.m_proprietary.insert(source.m_proprietary.begin(),source.m_proprietary.end());
            // Native PSBTOutput::Merge does not union proprietary fields.
            for (size_t i = 0; i < staged.outputs.size(); ++i)
                staged.outputs[i].m_proprietary.insert(source.outputs[i].m_proprietary.begin(),source.outputs[i].m_proprietary.end());
        }
        mercaweave::psbt::Inspect(staged,spent,execution);
        result = std::move(staged); return true;
    } catch (const mercaweave::Failure&) { return false; }
}
