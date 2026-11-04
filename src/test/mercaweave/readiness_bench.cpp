// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/allocation_probe.h>
#include <test/mercaweave/block_work.h>
#include <test/mercaweave/context_fixtures.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sys/resource.h>

using namespace mercaweave;
using namespace mercaweave::test;
using namespace mercaweave::research;
namespace {
void Require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
RequestPtr View(const Fixture& f) { return Capture(f.tx, f.spent, f.genesis, MANDATORY_SCRIPT_VERIFY_FLAGS); }
const char* Reason(Error e)
{
    switch (e) {
    case Error::Ownership: return "ownership";
    case Error::Signature: return "signature";
    case Error::DuplicateKey: return "duplicate_key";
    case Error::Reference: return "reference";
    case Error::Amount: return "amount";
    case Error::Witness: return "witness";
    case Error::InputsLimit: return "inputs_limit";
    case Error::GroupsLimit: return "groups_limit";
    case Error::MembersLimit: return "members_limit";
    case Error::Encoding: return "encoding";
    default: return "other";
    }
}
struct Timing { double median, worst, user, system; long rss; size_t calls, bytes, largest; };
double Seconds(const timeval& t) { return t.tv_sec + t.tv_usec / 1e6; }
Timing MeasureRun(const std::function<void()>& run)
{
    run(); // Warmup outside both clock and CPU samples.
    struct rusage before{}, after{};
    getrusage(RUSAGE_SELF, &before);
    std::vector<double> samples;
    for (int i = 0; i < 7; ++i) {
        const auto start = std::chrono::steady_clock::now();
        run();
        samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    getrusage(RUSAGE_SELF, &after);
    std::sort(samples.begin(), samples.end());
    allocation::calls = allocation::bytes = allocation::largest = 0;
    allocation::active = true;
    try { run(); } catch (...) { allocation::active = false; throw; }
    allocation::active = false;
    return {samples[3], samples.back(), (Seconds(after.ru_utime) - Seconds(before.ru_utime)) * 1000 / 7,
        (Seconds(after.ru_stime) - Seconds(before.ru_stime)) * 1000 / 7,
        after.ru_maxrss, allocation::calls, allocation::bytes, allocation::largest};
}
void PrintTiming(const Timing& t)
{
    std::cout << ',' << t.median << ',' << t.worst << ',' << t.user << ',' << t.system << ',' << t.rss
              << ',' << t.calls << ',' << t.bytes << ',' << t.largest << '\n';
}
// Freeze unique outpoints before signing both domains. Template keys are public
// deterministic test keys; no generated secret keys are written to disk.
void Resign(Fixture& f, uint32_t serial, size_t v3)
{
    for (auto& in : f.tx.vin) {
        B32 id{};
        WriteLE32(id.data(), serial);
        WriteLE32(id.data() + 4, in.prevout.n);
        id[31] = 0x71;
        in.prevout.hash = Txid::FromUint256(uint256{std::span<const unsigned char>{id}});
    }
    if (f.tx.vin.size() > v3) {
        const NativeSession native(f.tx, f.spent, f.genesis);
        for (size_t i = v3; i < f.tx.vin.size(); ++i) {
            auto sig = SignDigest(native.Digest(i), *f.keys[0], B32{}, "Mercatura/PQAuth/v1");
            f.tx.vin[i].scriptWitness.stack[0].assign(sig.begin(), sig.end());
        }
    }
    SignFixture(f);
}
void Blocks(std::string_view name, size_t limit)
{
    size_t n{1}, g{1}, native{0};
    if (name == "consolidate53") n = 53;
    else if (name == "consolidate100" || name == "early_invalid") n = 100;
    else if (name == "independent100") n = g = 100;
    else if (name == "max_groups" || name == "late_invalid") n = g = 256;
    else if (name == "max_members") n = 1024;
    else if (name == "max_inputs") { n = 4096; g = 4; }
    else if (name == "mixed") { n = 53; native = 2; }
    else Require(name == "small", "unknown block scenario");
    auto f = MixedFixture(n, g, native);
    std::vector<RequestPtr> requests;
    Ledger planning;
    BudgetError binding{BudgetError::None};
    for (uint32_t serial = 1; ; ++serial) {
        Resign(f, serial, n);
        if (name == "early_invalid") f.tx.vin[0].scriptWitness.stack.pop_back();
        if (name == "late_invalid") f.tx.vin[g - 1].scriptWitness.stack[0][100] ^= 1;
        auto request = View(f);
        const auto charge = Estimate(*request);
        binding = planning.Reserve(charge);
        if (binding != BudgetError::None) break;
        requests.push_back(std::move(request));
        // Restore the early witness shape so the next signing pass is valid.
        if (name == "early_invalid") f.tx.vin[0].scriptWitness.stack.push_back(Bytes(32));
        if (name == "early_invalid") {
            B32 nonce{}; nonce[31] = 0x90;
            f.tx.vin[0].scriptWitness.stack[2].assign(nonce.begin(), nonce.end());
        }
        if (limit && requests.size() == limit) break;
    }
    const bool expect_valid = name != "early_invalid" && name != "late_invalid";
    Workload result;
    const auto timing = MeasureRun([&] {
        result = VerifyWorkload(requests);
        Require(result.authorized == expect_valid && result.charged.transactions == requests.size(), "block verdict or accounting");
        if (expect_valid) Require(result.actual_groups == g * requests.size() && result.actual_native == native * requests.size(), "real signature count");
    });
    const auto block = SerializeWorkload(requests);
    const auto actual = GetSerializeSize(TX_WITH_WITNESS(block));
    const auto stripped = GetSerializeSize(TX_NO_WITNESS(block));
    Require(actual == result.block_bytes && actual + 3 * stripped == result.block_weight, "real block serialization disagreement");
    const auto& c = result.charged;
    std::cout << "scenario,valid,transactions,inputs,v3_inputs,groups,block_bytes,stripped_bytes,witness_bytes,legacy_weight,tx_fee_full_bytes,witness_items,existing_cost,group_cost,binding_budget,actual_group_verifies,actual_native_verifies,ownership_checks,pk_hash_bytes,body_passes,manifest_passes,component_payload_bytes,manifest_payload_bytes,median_ms,worst_ms,user_ms,system_ms,peak_rss_kib,cpp_new_calls,cpp_requested_bytes,cpp_largest_request\n";
    std::cout << name << ',' << result.authorized << ',' << c.transactions << ',' << c.inputs << ',' << c.v3_inputs << ',' << c.groups
        << ',' << actual << ',' << stripped << ',' << actual - stripped << ',' << result.block_weight << ',' << c.tx_bytes
        << ',' << c.witness_items << ',' << c.existing_cost << ',' << c.group_cost << ',' << static_cast<int>(binding)
        << ',' << result.actual_groups << ',' << result.actual_native << ',' << result.ownership_checks << ',' << result.public_key_bytes
        << ',' << result.body_passes << ',' << result.manifest_passes << ',' << result.component_bytes << ',' << result.manifest_bytes;
    PrintTiming(timing);
}
void Invalid(std::string_view name)
{
    size_t n{100}, g{1};
    bool structural{false};
    Error expected{Error::Witness};
    if (name == "late_member") { n = 1000; expected = Error::Ownership; }
    else if (name == "late_signature") { n = g = 100; expected = Error::Signature; }
    else if (name == "duplicate_anchor") { n = g = 256; expected = Error::DuplicateKey; }
    else if (name == "final_reference") { n = 500; g = 10; expected = Error::Reference; }
    else if (name == "altered_output" || name == "spent_amount") { g = 10; expected = Error::Signature; }
    else if (name == "spent_script") expected = Error::Ownership;
    else if (name == "negative_amount") expected = Error::Amount;
    else if (name == "groups_over") { n = g = 257; structural = true; expected = Error::GroupsLimit; }
    else if (name == "members_over") { n = 1025; structural = true; expected = Error::MembersLimit; }
    else if (name == "inputs_over") { n = 4097; g = 5; structural = true; expected = Error::InputsLimit; }
    else Require(name == "witness_final" || name == "truncated" || name == "oversized_count", "unknown invalid case");
    auto f = Make(n, g, false, structural);
    if (!structural) SignFixture(f);
    if (name == "late_member") f.tx.vin.back().scriptWitness.stack[0][0] ^= 1;
    if (name == "late_signature") f.tx.vin[g - 1].scriptWitness.stack[0][100] ^= 1;
    if (name == "duplicate_anchor") f.tx.vin.back().scriptWitness.stack[1] = f.tx.vin[0].scriptWitness.stack[1];
    if (name == "final_reference") WriteLE32(f.tx.vin.back().scriptWitness.stack[1].data(), static_cast<uint32_t>(n - 1));
    if (name == "altered_output") ++f.tx.vout[0].nValue;
    if (name == "spent_amount") ++f.spent.back().nValue;
    if (name == "spent_script") f.spent.back().scriptPubKey.back() ^= 1;
    if (name == "negative_amount") f.spent.back().nValue = -1;
    if (name == "witness_final") f.tx.vin.back().scriptWitness.stack.push_back({});
    Workload result;
    Timing timing{};
    auto bytes = Wire(f.tx);
    const bool decode = name == "truncated" || name == "oversized_count";
    if (decode) {
        if (name == "truncated") bytes.resize(bytes.size() / 2);
        else bytes = {2,0,0,0,0,1,0xfe,0xff,0xff,0xff,0xff};
        timing = MeasureRun([&] {
            bool rejected{false};
            try { DecodeTransaction(bytes); } catch (const Failure&) { rejected = true; }
            Require(rejected, "hostile wire accepted");
        });
    } else {
        auto request = View(f);
        timing = MeasureRun([&] {
            result = VerifyWorkload({request});
            Require(!result.authorized && result.charged.transactions == 1 && result.receipts.size() == 1 &&
                    result.receipts[0].validation_error == expected, "deterministic charged rejection");
        });
    }
    const char* stage = decode ? "decode" : result.actual_groups ? "signature" : result.ownership_checks ? "ownership" : "structure";
    std::cout << "scenario,fixture,inputs,groups,serialized_bytes,reason,stage,charged_transactions,actual_group_verifies,ownership_checks,body_passes,manifest_passes,median_ms,worst_ms,user_ms,system_ms,peak_rss_kib,cpp_new_calls,cpp_requested_bytes,cpp_largest_request\n";
    std::cout << name << ',' << (structural ? "structural_only" : "signed_then_mutated") << ',' << n << ',' << g << ',' << bytes.size()
              << ',' << (decode ? "bounded_decode" : Reason(expected)) << ',' << stage << ',' << result.charged.transactions
              << ',' << result.actual_groups << ',' << result.ownership_checks << ',' << result.body_passes << ',' << result.manifest_passes;
    PrintTiming(timing);
}
}
int main(int argc, char** argv)
{
    try {
        std::cout << std::fixed << std::setprecision(6);
        Require(argc >= 3 && argc <= 4, "usage: readiness_bench block|invalid SCENARIO [--smoke|--single]");
        if (std::string_view(argv[1]) == "block") {
            const auto option = argc == 4 ? std::string_view(argv[3]) : std::string_view{};
            Require(option.empty() || option == "--smoke" || option == "--single", "unknown block option");
            Blocks(argv[2], option == "--smoke" ? 2 : option == "--single" ? 1 : 0);
        }
        else { Require(std::string_view(argv[1]) == "invalid" && argc == 3, "unknown mode"); Invalid(argv[2]); }
        return 0;
    } catch (const std::exception& e) { allocation::active = false; std::cerr << "FAIL readiness measurement: " << e.what() << '\n'; return 1; }
}
