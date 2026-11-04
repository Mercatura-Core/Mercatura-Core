// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/auth_context.h>
#include <test/mercaweave/fixtures.h>
#include <crypto/sha256.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <script/solver.h>
#include <iostream>
#include <map>

using namespace mercaweave;
using namespace mercaweave::test;
using namespace mercaweave::research;
namespace {
size_t checks{0};
void Check(bool b, const char* what) { ++checks; if (!b) throw std::runtime_error(what); }
Preparation Authorize(const Fixture& f) { return Prepare(Capture(f.tx, f.spent, f.genesis, STANDARD_SCRIPT_VERIFY_FLAGS)); }
Fixture Parent()
{
    auto f = Make(2, 1);
    B32 nonce{}; nonce[31] = 0x90;
    f.tx.vout[0].scriptPubKey = Output(f.genesis, f.keys[0]->pk, nonce);
    return f;
}
Fixture Child(const Fixture& parent)
{
    auto child = Make(1, 1);
    child.tx.vin[0].prevout = COutPoint{CTransaction(parent.tx).GetHash(), 0};
    child.spent[0] = parent.tx.vout[0];
    SignFixture(child);
    Check(bool(Authorize(child).context), "child authorization with original parent UTXO");
    return child;
}
void V3Only()
{
    auto f = Parent();
    SignFixture(f);
    const auto proof = Authorize(f);
    Check(bool(proof.context), "v3-only authorization");
    const CTransaction before(f.tx);
    auto child = Child(f);
    f.tx.vin[0].scriptSig << OP_0;
    Check(!Authorize(f).context, "v3 scriptSig mutation forbidden");
    f.tx.vin[0].scriptSig.clear();
    B32 randomness{}; randomness[0] = 1; // Deterministic public test randomness.
    Sign(f.tx, f.spent, f.genesis, f.KeyRefs(), [&](B32& out) { out = randomness; return true; });
    const CTransaction after(f.tx);
    Check(bool(Authorize(f).context) && before.GetHash() == after.GetHash() && before.GetWitnessHash() != after.GetWitnessHash(), "v3 valid signature randomization changes only witness identity");
    Check(child.tx.vin[0].prevout.hash == after.GetHash(), "witness-only changes preserve child lookup");
    Check(!proof.context->Matches(*Capture(f.tx, f.spent, f.genesis, STANDARD_SCRIPT_VERIFY_FLAGS)), "old exact-wire proof invalidated even for fresh valid signature");
    std::cout << "{\"case\":\"v3_only_randomized_signature\",\"before_txid\":\"" << before.GetHash().ToString()
              << "\",\"after_txid\":\"" << after.GetHash().ToString() << "\",\"wtxid_changed\":true,\"authorized\":true,\"child_resolves\":true}\n";
}
void Mixed(int form)
{
    auto f = Parent();
    auto extra = Make(1, 1);
    extra.tx.vin[0].prevout.n = 999;
    extra.tx.vin[0].scriptWitness.stack.clear();
    const CScript predicate = CScript{} << OP_DROP << OP_TRUE;
    const Bytes redeem(predicate.begin(), predicate.end());
    CScript locking;
    if (form == 0) {
        locking = predicate;
        extra.tx.vin[0].scriptSig = CScript{} << Bytes{0x41};
    } else if (form == 1) {
        std::array<unsigned char, 20> hash;
        CHash160().Write(redeem).Finalize(hash);
        locking << OP_HASH160 << Bytes(hash.begin(), hash.end()) << OP_EQUAL;
        extra.tx.vin[0].scriptSig = CScript{} << Bytes{0x41} << redeem;
    } else {
        B32 hash;
        CSHA256().Write(redeem.data(), redeem.size()).Finalize(hash.data());
        locking << OP_0 << Bytes(hash.begin(), hash.end());
        extra.tx.vin[0].scriptWitness.stack = {Bytes{0x41}, redeem};
    }
    // The funding output's genuine Solver/IsStandard results distinguish a
    // nonstandard bare predicate from an opaque standard P2SH commitment.
    std::vector<Bytes> solutions;
    const auto type = Solver(locking, solutions);
    Check(type == (form == 0 ? TxoutType::NONSTANDARD : form == 1 ? TxoutType::SCRIPTHASH : TxoutType::WITNESS_V0_SCRIPTHASH), "production funding Solver classification");
    TxoutType standard_type;
    Check(IsStandard(locking, standard_type) == (form != 0), "production output standardness");
    f.tx.vin.push_back(extra.tx.vin[0]);
    f.spent.emplace_back(1000, locking);
    SignFixture(f);
    const auto original = Authorize(f);
    Check(bool(original.context), "mixed predicate authorization with standard Script flags");
    auto child = Child(f);
    const CTransaction before(f.tx);
    const auto digests = original.context->GroupProof().GroupDigests();
    if (form == 2) f.tx.vin[2].scriptWitness.stack[0][0] = 0x42;
    else f.tx.vin[2].scriptSig = form == 1 ? CScript{} << Bytes{0x42} << redeem : CScript{} << Bytes{0x42};
    const auto mutated = Authorize(f);
    Check(bool(mutated.context), "mutated non-signature predicate still truly satisfied");
    const CTransaction after(f.tx);
    Check(std::equal(digests.begin(), digests.end(), mutated.context->GroupProof().GroupDigests().begin()), "unchanged exact v3 final digests");
    Check(original.context->Inputs().size() == mutated.context->Inputs().size() && mutated.work.ownership_checks == 2 && mutated.work.verification_calls == 1, "ownership and signature checks rerun without cache");
    Check((before.GetHash() != after.GetHash()) == (form != 2) && before.GetWitnessHash() != after.GetWitnessHash(), "precise txid/wtxid behavior");
    auto new_view = Capture(f.tx, f.spent, f.genesis, STANDARD_SCRIPT_VERIFY_FLAGS);
    Check(!original.context->Matches(*new_view) && !Bind(new_view, original.context).checks, "old record cannot authorize new exact wire");
    // Explicit UTXO lookup, separate from Script authorization: an unconfirmed
    // child prepared for the original txid does not resolve against a changed
    // parent txid. Supplying the old CTxOut alone would not prove existence.
    const std::map<COutPoint, CTxOut> available{{COutPoint{after.GetHash(), 0}, f.tx.vout[0]}};
    const bool resolves = available.contains(child.tx.vin[0].prevout);
    Check(resolves == (form == 2), "coordinator mutation can orphan the original child lookup");
    if (form == 1) {
        Check(f.tx.vin[2].scriptSig.IsPushOnly() && f.tx.vin[2].scriptSig.size() < MAX_STANDARD_SCRIPTSIG_SIZE, "P2SH ScriptSig meets standard size and push rules");
        std::vector<Bytes> stack;
        Check(EvalScript(stack, f.tx.vin[2].scriptSig, SCRIPT_VERIFY_NONE, BaseSignatureChecker(), SigVersion::BASE) && !stack.empty(), "genuine policy redeem extraction");
        CScript subscript(stack.back().begin(), stack.back().end());
        Check(subscript.GetSigOpCount(true) == 0 && subscript.GetSigOpCount(true) <= MAX_P2SH_SIGOPS, "P2SH input standardness branch does not ban predicate");
        // Actual IsStandardTx is run on a v2+P2SH version of the same form.
        // AreInputsStandard's CCoinsView-dependent whole-tx check is audited in
        // source, not claimed as executed by this standalone harness.
        auto native = Make(2, 1);
        ConvertToV2(native);
        native.tx.vin.push_back(f.tx.vin[2]);
        native.spent.push_back(f.spent[2]);
        const NativeSession session(native.tx, native.spent, native.genesis, STANDARD_SCRIPT_VERIFY_FLAGS);
        for (uint32_t i = 0; i < 2; ++i) {
            auto sig = SignDigest(session.Digest(i), *native.keys[0], B32{}, "Mercatura/PQAuth/v1");
            native.tx.vin[i].scriptWitness.stack[0].assign(sig.begin(), sig.end());
        }
        Check(NativeSession(native.tx, native.spent, native.genesis, STANDARD_SCRIPT_VERIFY_FLAGS).VerifyMixed().grouped.valid, "native v2 plus P2SH predicate satisfies real Script");
        std::string reason;
        Check(IsStandardTx(CTransaction(native.tx), std::nullopt, true, CFeeRate{1000}, reason), "native v2 plus P2SH passes genuine whole IsStandardTx");
    }
    std::cout << "{\"case\":\"" << (form == 0 ? "bare" : form == 1 ? "p2sh" : "p2wsh")
              << "\",\"before_txid\":\"" << before.GetHash().ToString() << "\",\"after_txid\":\"" << after.GetHash().ToString()
              << "\",\"wtxid_changed\":true,\"digest_unchanged\":true,\"authorized\":true,\"child_resolves\":" << (resolves ? "true" : "false") << "}\n";
}
}
int main()
{
    try { V3Only(); for (int form : {0,1,2}) Mixed(form); std::cout << "{\"assertions\":" << checks << ",\"passed\":true}\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL malleability: " << e.what() << '\n'; return 1; }
}
