// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "fixtures.h"
#include "mutation_seeds.h"
#include <iostream>
#include <random>

using namespace mercaweave;
using namespace mercaweave::test;
namespace {
size_t checks{0}, rejected{0}, fuzzed{0};
void Check(bool ok, const char* name) { ++checks; if (!ok) throw std::runtime_error(name); }
void Reject(std::span<const unsigned char> bytes)
{
    try { DecodeTransaction(bytes); }
    catch (const Failure&) { ++rejected; return; }
    throw std::runtime_error("invalid serialized transaction accepted by decoder");
}
Bytes Count(uint64_t n) { Writer w; WriteCompactSize(w, n); return w.bytes; }
void Splice(Bytes& bytes, size_t at, size_t remove, const Bytes& replacement)
{
    bytes.erase(bytes.begin() + at, bytes.begin() + at + remove);
    bytes.insert(bytes.begin() + at, replacement.begin(), replacement.end());
}
void Tests()
{
    auto f = Make(2, 1);
    SignFixture(f);
    const auto wire = Wire(f.tx);
    Check(Wire(DecodeTransaction(wire)) == wire, "consensus transaction roundtrip");
    for (size_t n = 0; n < wire.size(); ++n) Reject(std::span{wire}.first(n));
    auto extra = wire; extra.push_back(0); Reject(extra);
    auto noncanonical = wire;
    Splice(noncanonical, 6, 1, Bytes{0xfd, 2, 0}); Reject(noncanonical);
    for (auto count : {UINT64_MAX, uint64_t{UINT32_MAX}, uint64_t{MAX_SIZE}, uint64_t{4096}}) {
        auto bad = wire;
        Splice(bad, 6, 1, Count(count)); Reject(bad);
    }
    for (const auto prefix : {Bytes{0xfd}, Bytes{0xfe,0,0}, Bytes{0xff,0,0,0,0}}) {
        Bytes bad(wire.begin(), wire.begin() + 6);
        bad.insert(bad.end(), prefix.begin(), prefix.end()); Reject(bad);
    }
    for (uint8_t flag : {uint8_t{0}, uint8_t{2}, uint8_t{3}, uint8_t{255}}) {
        auto bad = wire; bad[5] = flag; Reject(bad);
    }
    const auto witness_at = Wire(f.tx, false).size() - 4 + 2;
    for (auto count : {uint64_t{1004}, UINT64_MAX}) {
        auto bad = wire; Splice(bad, witness_at, 1, Count(count)); Reject(bad);
    }
    for (auto length : {UINT64_MAX, uint64_t{UINT32_MAX}, uint64_t{MAX_SIZE}, uint64_t{65536}}) {
        auto bad = wire; Splice(bad, witness_at + 1, 3, Count(length)); Reject(bad);
    }
    // An empty serialized stack and zero-length item are bounded grammar;
    // v3 ownership/authorization must independently reject their wrong shape.
    auto wrong_shape = f.tx;
    wrong_shape.vin[1].scriptWitness.stack.clear();
    Check(!Verify(DecodeTransaction(Wire(wrong_shape)), f.spent, f.genesis).valid, "decoder success is not group authorization");
    wrong_shape = f.tx;
    wrong_shape.vin[1].scriptWitness.stack[0].clear();
    Check(!Verify(DecodeTransaction(Wire(wrong_shape)), f.spent, f.genesis).valid, "zero-length item never authorizes v3");
    auto no_witness = f.tx;
    for (auto& in : no_witness.vin) in.scriptWitness.stack.clear();
    const auto plain = Wire(no_witness);
    Check(Wire(DecodeTransaction(plain)) == plain, "non-witness encoding roundtrip");
    auto superfluous = plain;
    superfluous.insert(superfluous.begin() + 4, {0, 1});
    superfluous.insert(superfluous.end() - 4, {0, 0}); Reject(superfluous);
    Reader reader{std::span<const unsigned char>{}};
    bool skip_failed{false};
    try { reader.skip(SIZE_MAX); } catch (const Failure&) { skip_failed = true; }
    Check(skip_failed && reader.remaining() == 0, "reader overflow cannot mutate cursor");
    std::mt19937_64 rng{TRANSACTION_MUTATION_SEED};
    for (size_t iteration = 0; iteration < 10000; ++iteration) {
        auto bytes = wire;
        bytes[rng() % bytes.size()] ^= static_cast<unsigned char>(1 + rng() % 255);
        try {
            const auto decoded = DecodeTransaction(bytes);
            Check(Wire(decoded) == bytes, "mutated byte stream stays canonical");
            Check(!Verify(decoded, f.spent, f.genesis).valid, "changed v3 transaction never authorizes");
        } catch (const Failure&) { ++rejected; }
        ++fuzzed;
    }
    for (size_t iteration = 0; iteration < 2000; ++iteration) {
        Bytes bytes(rng() % 256);
        for (auto& b : bytes) b = static_cast<unsigned char>(rng());
        try { Check(Wire(DecodeTransaction(bytes)) == bytes, "arbitrary accepted bytes reserialize exactly"); }
        catch (const Failure&) { ++rejected; }
        ++fuzzed;
    }
    // Compare the original decoder and preflight on a bounded valid family.
    for (size_t n : {1, 2, 10, 53}) {
        auto fixture = Make(n, 1);
        SignFixture(fixture);
        const auto bytes = Wire(fixture.tx);
        Reader original{bytes};
        CMutableTransaction decoded;
        original >> TX_WITH_WITNESS(decoded);
        Check(Wire(decoded) == Wire(DecodeTransaction(bytes)), "inherited/preflight differential agreement");
    }
}
}
int main()
{
    try {
        Tests();
        std::cout << "PASS parser checks=" << checks << " rejected=" << rejected << " fuzzed=" << fuzzed
                  << " seed=" << TRANSACTION_MUTATION_SEED << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL parser: " << e.what() << '\n'; return 1; }
}
