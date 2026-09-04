// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <crypto/mercatura_mldsa.h>
#include <crypto/mercatura_pqkey.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

FUZZ_TARGET(mercatura_pq_witness)
{
    FuzzedDataProvider provider{
        buffer.data(),
        buffer.size()
    };

    auto consume_exact =
        [&](size_t size) {
            std::vector<unsigned char> out =
                provider.ConsumeBytes<unsigned char>(size);

            // Keep the requested structural length even for very
            // small fuzz inputs. Missing bytes are deterministic zeroes.
            out.resize(size, 0);

            return out;
        };

    std::vector<unsigned char> signature =
        consume_exact(
            MERCATURA_MLDSA65_SIGNATURE_SIZE);

    std::vector<unsigned char> public_key =
        consume_exact(
            MERCATURA_MLDSA65_PUBLIC_KEY_SIZE);

    MercaturaPQKeyCommitment commitment{};

    assert(
        ComputeMercaturaPQKeyCommitmentV1(
            commitment,
            public_key));

    std::vector<unsigned char> program{
        commitment.begin(),
        commitment.end()
    };

    CScriptWitness witness;
    witness.stack = {
        signature,
        public_key,
    };

    // Every fuzz iteration begins by proving the canonical structure
    // remains accepted.
    ScriptError baseline_error =
        SCRIPT_ERR_UNKNOWN_ERROR;

    assert(
        CheckMercaturaPQWitnessStructureV1(
            witness,
            program,
            false,
            &baseline_error));

    assert(
        baseline_error ==
        SCRIPT_ERR_OK);

    // Then mutate one structural property. This deliberately targets
    // all boundaries relevant to Mercatura PQ Authorization v1 rather
    // than relying on a generic script fuzzer to discover 1952- and
    // 3309-byte exact-size cases by chance.
    const uint8_t mutation =
        provider.ConsumeIntegral<uint8_t>() % 13;

    bool is_p2sh = false;

    switch (mutation) {
    case 0:
        // Canonical valid structure.
        break;

    case 1:
        is_p2sh = true;
        break;

    case 2:
        program.pop_back();
        break;

    case 3:
        program.push_back(0x00);
        break;

    case 4:
        witness.stack.pop_back();
        break;

    case 5:
        witness.stack.emplace_back(
            1,
            provider.ConsumeIntegral<uint8_t>());
        break;

    case 6:
        witness.stack[0].pop_back();
        break;

    case 7:
        witness.stack[0].push_back(
            provider.ConsumeIntegral<uint8_t>());
        break;

    case 8:
        witness.stack[1].pop_back();
        break;

    case 9:
        witness.stack[1].push_back(
            provider.ConsumeIntegral<uint8_t>());
        break;

    case 10:
        program[0] ^= 0x01;
        break;

    case 11:
        witness.stack[1][
            provider.ConsumeIntegralInRange<size_t>(
                0,
                witness.stack[1].size() - 1)
        ] ^= 0x01;
        break;

    case 12:
        // Signature contents are not a structural property. Changing
        // them must leave structure validation successful; actual
        // ML-DSA validity is checked later by the signature verifier.
        witness.stack[0][
            provider.ConsumeIntegralInRange<size_t>(
                0,
                witness.stack[0].size() - 1)
        ] ^= 0x01;
        break;
    }

    bool expected_valid =
        !is_p2sh &&
        program.size() ==
            MERCATURA_PQ_KEY_COMMITMENT_SIZE &&
        witness.stack.size() == 2;

    if (expected_valid) {
        expected_valid =
            witness.stack[0].size() ==
                MERCATURA_MLDSA65_SIGNATURE_SIZE &&
            witness.stack[1].size() ==
                MERCATURA_MLDSA65_PUBLIC_KEY_SIZE;
    }

    if (expected_valid) {
        MercaturaPQKeyCommitment expected_commitment{};

        expected_valid =
            ComputeMercaturaPQKeyCommitmentV1(
                expected_commitment,
                witness.stack[1]);

        if (expected_valid) {
            expected_valid =
                std::equal(
                    expected_commitment.begin(),
                    expected_commitment.end(),
                    program.begin(),
                    program.end());
        }
    }

    ScriptError error =
        SCRIPT_ERR_UNKNOWN_ERROR;

    const bool accepted =
        CheckMercaturaPQWitnessStructureV1(
            witness,
            program,
            is_p2sh,
            &error);

    assert(
        accepted ==
        expected_valid);

    assert(
        accepted ==
        (error == SCRIPT_ERR_OK));
}
