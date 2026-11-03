// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include "context_fixtures.h"
#include <iostream>
using namespace mercaweave;
using namespace mercaweave::test;
int main()
{
    try {
        auto f = Make(1, 1);
        SignFixture(f);
        f.tx.vin[0].scriptWitness.stack[0][100] ^= 1;
        ScriptError error;
        size_t calls{0};
        const bool permissive = NativeSession(f.tx, f.spent, f.genesis).Check(0, error, calls);
#ifdef MERCATURA_MERCAWEAVE_RESEARCH_TEST_ONLY
        if (permissive || error != SCRIPT_ERR_WITNESS_PROGRAM_MISMATCH) throw std::runtime_error("isolated missing-context fallback");
#else
        if (!permissive) throw std::runtime_error("normal unknown-v3 behavior changed");
#endif
        const bool strict = NativeSession(f.tx, f.spent, f.genesis, STANDARD_SCRIPT_VERIFY_FLAGS).Check(0, error, calls);
        if (strict) throw std::runtime_error("unknown/contextless v3 accepted under policy flags");
        size_t native_total{0};
        for (size_t n : {1,2,7,10}) {
            auto v2 = Make(n, 1); ConvertToV2(v2);
            const auto checked = NativeSession(v2.tx, v2.spent, v2.genesis).VerifyMixed();
            if (!checked.grouped.valid || checked.pq_checker_calls != n) throw std::runtime_error("native-v2 regression");
            native_total += n;
        }
        std::cout << "{\"research_hook\":"
#ifdef MERCATURA_MERCAWEAVE_RESEARCH_TEST_ONLY
            << "true"
#else
            << "false"
#endif
            << ",\"contextless_v3_mandatory\":" << (permissive ? "true" : "false")
            << ",\"contextless_v3_policy\":false,\"native_v2_verifies\":" << native_total << ",\"passed\":true}\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
