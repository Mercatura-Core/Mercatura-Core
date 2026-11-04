// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_TEST_MERCAWEAVE_FIXTURES_H
#define BITCOIN_TEST_MERCAWEAVE_FIXTURES_H
#include <test/mercaweave/mercaweave.h>
#include <memory>

namespace mercaweave::test {
struct Fixture {
    CMutableTransaction tx;
    std::vector<CTxOut> spent;
    B32 genesis{};
    std::vector<std::unique_ptr<KeyPair>> keys;
    std::vector<std::reference_wrapper<const KeyPair>> KeyRefs() const;
};
bool ZeroRandomnessForTestsOnly(B32& out);
bool SystemRandomnessForTests(B32& out);
Fixture Make(size_t inputs, size_t groups, bool repeated_nonce = false, bool structural_only = false);
void SignFixture(Fixture& f);
H48 V2Digest(const CMutableTransaction& tx, const std::vector<CTxOut>& spent, const B32& genesis, uint32_t index);
bool VerifyV2(const CMutableTransaction& tx, const std::vector<CTxOut>& spent, const B32& genesis, uint32_t index);
// Independent research adapter reproduces audited v2 bytes. It does NOT link
// production ComputeMercaturaPQAuthDigestV1 or the script interpreter.
void ConvertToV2(Fixture& f);
std::string Hex(std::span<const unsigned char> bytes);
Bytes Unhex(std::string_view text);
} // namespace mercaweave::test
#endif // BITCOIN_TEST_MERCAWEAVE_FIXTURES_H
