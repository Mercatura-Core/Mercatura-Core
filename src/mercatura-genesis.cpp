// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <consensus/amount.h>
#include <arith_uint256.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <pow.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <streams.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr uint32_t DEFAULT_TIME{1231006505U};
constexpr uint32_t DEFAULT_BITS{0x207fffffU};
constexpr uint32_t DEFAULT_NONCE_START{0U};
constexpr uint64_t DEFAULT_MAX_ATTEMPTS{1000U};
constexpr int32_t GENESIS_VERSION{1};

constexpr std::string_view DEFAULT_TIMESTAMP{
    "Mercatura development genesis v0.1"
};

constexpr std::string_view DEFAULT_OUTPUT_TEXT{
    "Mercatura development genesis v0.1"
};

const uint256 DEV_POW_LIMIT{
    "7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
};

struct Options {
    uint32_t time{DEFAULT_TIME};
    uint32_t bits{DEFAULT_BITS};
    uint32_t nonce_start{DEFAULT_NONCE_START};
    uint64_t max_attempts{DEFAULT_MAX_ATTEMPTS};
    std::string timestamp{DEFAULT_TIMESTAMP};
    std::string output_text{DEFAULT_OUTPUT_TEXT};
};

[[noreturn]] void Usage(const char* argv0, int exit_code)
{
    std::ostream& out = exit_code == EXIT_SUCCESS ? std::cout : std::cerr;

    out
        << "Mercatura standalone genesis mining helper\n\n"
        << "Usage:\n"
        << "  " << argv0 << " [options]\n\n"
        << "Options:\n"
        << "  --time <uint32>          Genesis nTime\n"
        << "  --bits <value>           Compact nBits, decimal or 0x-prefixed hex\n"
        << "  --nonce-start <uint32>   First nonce to test\n"
        << "  --max-attempts <uint64>  Maximum nonce attempts\n"
        << "  --timestamp <text>       Coinbase timestamp text\n"
        << "  --output-text <text>     Unspendable OP_RETURN output text\n"
        << "  --help                    Show this help\n\n"
        << "Defaults reproduce the current Mercatura development genesis format.\n";

    std::exit(exit_code);
}

uint64_t ParseUnsigned(const std::string& text, const char* option_name)
{
    std::size_t consumed{0};

    const unsigned long long value{
        std::stoull(text, &consumed, 0)
    };

    if (consumed != text.size()) {
        throw std::runtime_error(
            std::string{"Invalid value for "} +
            option_name +
            ": " +
            text);
    }

    return static_cast<uint64_t>(value);
}

uint32_t ParseUint32(const std::string& text, const char* option_name)
{
    const uint64_t value{ParseUnsigned(text, option_name)};

    if (value > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(
            std::string{"Value out of uint32 range for "} +
            option_name);
    }

    return static_cast<uint32_t>(value);
}

Options ParseOptions(int argc, char* argv[])
{
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg{argv[i]};

        auto require_value = [&](const char* option_name) -> std::string {
            if (++i >= argc) {
                throw std::runtime_error(
                    std::string{"Missing value for "} + option_name);
            }
            return argv[i];
        };

        if (arg == "--help") {
            Usage(argv[0], EXIT_SUCCESS);
        } else if (arg == "--time") {
            options.time =
                ParseUint32(require_value("--time"), "--time");
        } else if (arg == "--bits") {
            options.bits =
                ParseUint32(require_value("--bits"), "--bits");
        } else if (arg == "--nonce-start") {
            options.nonce_start =
                ParseUint32(
                    require_value("--nonce-start"),
                    "--nonce-start");
        } else if (arg == "--max-attempts") {
            options.max_attempts =
                ParseUnsigned(
                    require_value("--max-attempts"),
                    "--max-attempts");

            if (options.max_attempts == 0) {
                throw std::runtime_error(
                    "--max-attempts must be greater than zero");
            }
        } else if (arg == "--timestamp") {
            options.timestamp = require_value("--timestamp");
        } else if (arg == "--output-text") {
            options.output_text = require_value("--output-text");
        } else {
            throw std::runtime_error(
                std::string{"Unknown option: "} + arg);
        }
    }

    return options;
}

CBlock CreateGenesisCandidate(
    const Options& options,
    uint32_t nonce)
{
    CMutableTransaction tx;

    tx.version = 1;
    tx.vin.resize(1);
    tx.vout.resize(1);

    tx.vin[0].scriptSig =
        CScript()
        << 486604799
        << CScriptNum(4)
        << std::vector<unsigned char>(
            options.timestamp.begin(),
            options.timestamp.end());

    // Match the current Mercatura development genesis construction.
    // The output is intentionally unspendable.
    tx.vout[0].nValue = 50 * COIN;

    tx.vout[0].scriptPubKey =
        CScript()
        << OP_RETURN
        << std::vector<unsigned char>(
            options.output_text.begin(),
            options.output_text.end());

    CBlock genesis;

    genesis.nTime = options.time;
    genesis.nBits = options.bits;
    genesis.nNonce = nonce;
    genesis.nVersion = GENESIS_VERSION;
    genesis.hashPrevBlock.SetNull();
    genesis.vtx.push_back(
        MakeTransactionRef(std::move(tx)));

    genesis.hashMerkleRoot =
        BlockMerkleRoot(genesis);

    return genesis;
}

std::vector<unsigned char> SerializeHeader(
    const CBlockHeader& header)
{
    std::vector<unsigned char> bytes;
    bytes.reserve(80);

    VectorWriter{
        bytes,
        0,
        header,
    };

    return bytes;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const Options options{
            ParseOptions(argc, argv)
        };

        Consensus::Params consensus{};
        consensus.powLimit = DEV_POW_LIMIT;

        const auto target{
            DeriveTarget(
                options.bits,
                consensus.powLimit)
        };

        if (!target.has_value()) {
            std::cerr
                << "ERROR: nBits does not decode to a valid target "
                   "within the configured development powLimit.\n";
            return EXIT_FAILURE;
        }

        CBlock genesis{
            CreateGenesisCandidate(
                options,
                options.nonce_start)
        };

        PoWHashContext pow_context;

        uint256 pow_hash{};
        uint64_t attempts{0};
        bool found{false};

        const auto start{
            std::chrono::steady_clock::now()
        };

        while (attempts < options.max_attempts) {
            pow_hash =
                pow_context.GetHash(genesis);

            ++attempts;

            if (CheckProofOfWorkImpl(
                    pow_hash,
                    genesis.nBits,
                    consensus)) {
                found = true;
                break;
            }

            if (attempts == options.max_attempts ||
                genesis.nNonce ==
                    std::numeric_limits<uint32_t>::max()) {
                break;
            }

            ++genesis.nNonce;
        }

        const auto finish{
            std::chrono::steady_clock::now()
        };

        const double elapsed_seconds{
            std::chrono::duration<double>(
                finish - start).count()
        };

        const double hashes_per_second{
            elapsed_seconds > 0.0
                ? static_cast<double>(attempts) /
                      elapsed_seconds
                : 0.0
        };

        const std::vector<unsigned char> header_bytes{
            SerializeHeader(genesis)
        };

        std::cout
            << "========================================\n"
            << "Mercatura genesis helper\n"
            << "========================================\n"
            << "timestamp text: "
            << options.timestamp << '\n'
            << "output text: "
            << options.output_text << '\n'
            << "nVersion: "
            << genesis.nVersion << '\n'
            << "nTime: "
            << genesis.nTime << '\n'
            << "nBits: 0x"
            << strprintf("%08x", genesis.nBits) << '\n'
            << "decoded target: "
            << ArithToUint256(*target).GetHex() << '\n'
            << "powLimit: "
            << consensus.powLimit.GetHex() << '\n'
            << "nonce start: "
            << options.nonce_start << '\n'
            << (found ? "winning nonce: " : "last tested nonce: ")
            << genesis.nNonce << '\n'
            << "attempts: "
            << attempts << '\n'
            << "elapsed seconds: "
            << elapsed_seconds << '\n'
            << "hashes/second: "
            << hashes_per_second << '\n'
            << "header bytes: "
            << header_bytes.size() << '\n'
            << "serialized header: "
            << HexStr(header_bytes) << '\n'
            << "merkle root: "
            << genesis.hashMerkleRoot.GetHex() << '\n'
            << "block id: "
            << genesis.GetHash().GetHex() << '\n'
            << "MercaHash: "
            << pow_hash.GetHex() << '\n'
            << "PoW valid: "
            << (found ? "YES" : "NO") << '\n';

        if (!found) {
            std::cerr
                << "ERROR: no valid nonce found within "
                << options.max_attempts
                << " attempts.\n";
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::cerr
            << "ERROR: "
            << e.what()
            << '\n';

        return EXIT_FAILURE;
    }
}
