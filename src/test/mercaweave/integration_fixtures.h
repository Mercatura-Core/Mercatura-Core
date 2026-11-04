// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_TEST_MERCAWEAVE_INTEGRATION_FIXTURES_H
#define BITCOIN_TEST_MERCAWEAVE_INTEGRATION_FIXTURES_H
#include <test/mercaweave/context_fixtures.h>
#include <test/mercaweave/isolated_validation.h>
namespace mercaweave::integration::test {
inline void Fund(const ViewPtr& view, const mercaweave::test::Fixture& f, int height = 1, bool coinbase = false)
{
    for (size_t i = 0; i < f.tx.vin.size(); ++i) view->Insert(f.tx.vin[i].prevout, Coin{CTxOut(f.spent[i]), height, coinbase});
}
inline ViewPtr Funded(const mercaweave::test::Fixture& f)
{
    auto view = View::Create(f.genesis); Fund(view, f); return view;
}
inline void UniqueAndSign(mercaweave::test::Fixture& f, uint32_t serial, size_t v3)
{
    for (auto& in : f.tx.vin) {
        B32 id{};
        WriteLE32(id.data(), serial);
        WriteLE32(id.data() + 4, in.prevout.n);
        id[31] = 0x74;
        in.prevout.hash = Txid::FromUint256(uint256{std::span<const unsigned char>{id}});
    }
    if (f.tx.vin.size() > v3) {
        const mercaweave::test::NativeSession native(f.tx, f.spent, f.genesis);
        for (size_t i = v3; i < f.tx.vin.size(); ++i) {
            auto sig = SignDigest(native.Digest(i), *f.keys[0], B32{}, "Mercatura/PQAuth/v1");
            f.tx.vin[i].scriptWitness.stack[0].assign(sig.begin(), sig.end());
        }
    }
    mercaweave::test::SignFixture(f);
}
} // namespace mercaweave::integration::test
#endif // BITCOIN_TEST_MERCAWEAVE_INTEGRATION_FIXTURES_H
