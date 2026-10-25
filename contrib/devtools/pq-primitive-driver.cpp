// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
// Standalone TEST driver. Never link this command interpreter into a node.
// All input and output must be public deterministic test material.

#include <crypto/hkdf_sha3_384.h>
#include <crypto/hmac_sha3_384.h>
#include <crypto/mercatura_mldsa.h>
#include <crypto/sha3.h>

#ifdef PQ_CHECK_MLKEM
#include <mlkem_native.h>
static_assert(MLKEM768_PUBLICKEYBYTES == 1184);
static_assert(MLKEM768_SECRETKEYBYTES == 2400);
static_assert(MLKEM768_CIPHERTEXTBYTES == 1088);
static_assert(MLKEM_BYTES == 32);
#endif

#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Bytes = std::vector<unsigned char>;

static Bytes Read(std::istringstream& in, size_t expected = SIZE_MAX)
{
    std::string s;
    if (!(in >> s)) throw std::runtime_error("missing test argument");
    Bytes out;
    if (s != "-") {
        if (s.size() % 2) throw std::runtime_error("odd hex length");
        auto digit = [](char c) -> unsigned {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            throw std::runtime_error("invalid hex");
        };
        for (size_t i = 0; i < s.size(); i += 2) out.push_back((digit(s[i]) << 4) | digit(s[i + 1]));
    }
    if (expected != SIZE_MAX && out.size() != expected) throw std::runtime_error("wrong test argument size");
    return out;
}

static std::string Hex(const auto& bytes)
{
    constexpr char DIGITS[] = "0123456789abcdef";
    std::string out;
    for (unsigned char b : bytes) {
        out += DIGITS[b >> 4];
        out += DIGITS[b & 15];
    }
    return out;
}

static std::string Run(std::istringstream& in)
{
    std::string op;
    in >> op;
    if (op == "sha3") {
        auto msg = Read(in);
        std::array<unsigned char, 48> out{};
        SHA3_384().Write(msg).Finalize(out);
        return Hex(out);
    }
    if (op == "hmac") {
        auto key = Read(in);
        auto msg = Read(in);
        std::array<unsigned char, 48> out{};
        CHMAC_SHA3_384(key.data(), key.size()).Write(msg).Finalize(out);
        return Hex(out);
    }
    if (op == "hkdf32") {
        auto ikm = Read(in);
        auto salt = Read(in);
        auto info = Read(in);
        CHKDF_HMAC_SHA3_384_L32 hkdf{ikm, salt};
        std::array<unsigned char, 32> out{};
        hkdf.Expand32(info, out);
        return Hex(hkdf.GetPRK()) + " " + Hex(out);
    }
    if (op == "dsa-keygen") {
        auto seed = Read(in, 32);
        std::array<unsigned char, 1952> pk{};
        std::array<unsigned char, 4032> sk{};
        int rc = mercatura_mldsa65_keypair_from_seed(pk.data(), pk.size(), sk.data(), sk.size(), seed.data(), seed.size());
        if (rc != 1) throw std::runtime_error("ML-DSA keygen failed");
        return Hex(pk) + " " + Hex(sk);
    }
    if (op == "dsa-sign") {
        auto msg = Read(in);
        auto ctx = Read(in);
        auto rnd = Read(in, 32);
        auto sk = Read(in, 4032);
        std::array<unsigned char, 3309> sig{};
        int rc = mercatura_mldsa65_sign(sig.data(), sig.size(), msg.data(), msg.size(), ctx.data(), ctx.size(), rnd.data(), rnd.size(), sk.data(), sk.size());
        return std::to_string(rc) + " " + Hex(sig);
    }
    if (op == "dsa-verify") {
        auto sig = Read(in);
        auto msg = Read(in);
        auto ctx = Read(in);
        auto pk = Read(in);
        return std::to_string(mercatura_mldsa65_verify(sig.data(), sig.size(), msg.data(), msg.size(), ctx.data(), ctx.size(), pk.data(), pk.size()));
    }
#ifdef PQ_CHECK_MLKEM
    if (op == "kem-keygen") {
        auto coins = Read(in, 64);
        std::array<unsigned char, 1184> pk{};
        std::array<unsigned char, 2400> sk{};
        int rc = PQCP_MLKEM_NATIVE_MLKEM768_keypair_derand(pk.data(), sk.data(), coins.data());
        return std::to_string(rc) + " " + Hex(pk) + " " + Hex(sk);
    }
    if (op == "kem-enc") {
        auto pk = Read(in, 1184);
        auto coins = Read(in, 32);
        std::array<unsigned char, 1088> ct{};
        std::array<unsigned char, 32> ss{};
        int rc = PQCP_MLKEM_NATIVE_MLKEM768_enc_derand(ct.data(), ss.data(), pk.data(), coins.data());
        return std::to_string(rc) + " " + Hex(ct) + " " + Hex(ss);
    }
    if (op == "kem-dec") {
        auto sk = Read(in, 2400);
        auto ct = Read(in, 1088);
        std::array<unsigned char, 32> ss{};
        int rc = PQCP_MLKEM_NATIVE_MLKEM768_dec(ss.data(), ct.data(), sk.data());
        return std::to_string(rc) + " " + Hex(ss);
    }
    if (op == "kem-check-pk") {
        auto pk = Read(in, 1184);
        return std::to_string(PQCP_MLKEM_NATIVE_MLKEM768_check_pk(pk.data()));
    }
    if (op == "kem-check-sk") {
        auto sk = Read(in, 2400);
        return std::to_string(PQCP_MLKEM_NATIVE_MLKEM768_check_sk(sk.data()));
    }
#endif
    throw std::runtime_error("unknown test operation");
}

int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            std::istringstream in{line};
            auto out = Run(in);
            std::string trailing;
            if (in >> trailing) throw std::runtime_error("extra test argument");
            std::cout << out << std::endl;
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
}
