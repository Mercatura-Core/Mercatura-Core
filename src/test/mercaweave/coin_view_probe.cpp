// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <test/mercaweave/context_fixtures.h>
#include <coins.h>
#include <consensus/tx_check.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <iostream>
int main()
{
    auto f = mercaweave::test::MixedFixture(2, 1, 1);
    CCoinsView base;
    CCoinsViewCache coins{&base, true};
    for (size_t i = 0; i < f.spent.size(); ++i) coins.AddCoin(f.tx.vin[i].prevout, Coin{CTxOut(f.spent[i]), 1, false}, false);
    TxValidationState state;
    CAmount fee;
    const CTransaction tx{f.tx};
    if (!CheckTransaction(tx, state) || !Consensus::CheckTxInputs(tx, state, coins, 200, fee)) return 1;
    if (!AreInputsStandard(tx, coins)) std::cout << "current policy rejects unknown v3 as expected\n";
    std::cout << "PASS real coin-view/transaction validation fee=" << fee << '\n';
}
