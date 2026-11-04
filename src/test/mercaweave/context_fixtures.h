// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_TEST_MERCAWEAVE_CONTEXT_FIXTURES_H
#define BITCOIN_TEST_MERCAWEAVE_CONTEXT_FIXTURES_H
#include <test/mercaweave/auth_context.h>
#include <test/mercaweave/fixtures.h>
namespace mercaweave::test {
// Public deterministic TEST seeds/randomness only. All metadata is frozen
// before either scheme signs. V2 signing uses the genuine production digest.
inline Fixture MixedFixture(size_t inputs, size_t groups, size_t native_count)
{
    auto f = Make(inputs, groups);
    if (native_count) {
        auto v2 = Make(native_count, 1);
        ConvertToV2(v2);
        for (size_t i = 0; i < native_count; ++i) {
            v2.tx.vin[i].prevout.n += 100000;
            f.tx.vin.push_back(v2.tx.vin[i]);
            f.spent.push_back(v2.spent[i]);
        }
        const NativeSession session(f.tx, f.spent, f.genesis);
        for (size_t i = inputs; i < f.tx.vin.size(); ++i) {
            const auto sig = SignDigest(session.Digest(static_cast<uint32_t>(i)), *f.keys[0], B32{}, "Mercatura/PQAuth/v1");
            f.tx.vin[i].scriptWitness.stack[0].assign(sig.begin(), sig.end());
        }
    }
    SignFixture(f);
    return f;
}
}
#endif // BITCOIN_TEST_MERCAWEAVE_CONTEXT_FIXTURES_H
