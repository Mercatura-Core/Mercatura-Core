// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_POOL_NUMBER_H
#define BITCOIN_POOL_NUMBER_H

#include <openssl/bn.h>

#include <compare>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace pool {
// Nonnegative, arbitrary precision integer. No platform-sized intermediate or
// floating point participates in accounting. In particular 2^256 needs 257 bits.
class Number
{
    struct Deleter {
        void operator()(BIGNUM* p) const { BN_free(p); }
    };
    std::unique_ptr<BIGNUM, Deleter> m_value;
    static void Check(int ok)
    {
        if (!ok) throw std::runtime_error("integer operation failed");
    }

public:
    Number() : m_value{BN_new()}
    {
        if (!m_value) throw std::bad_alloc{};
        BN_zero(m_value.get());
    }
    explicit Number(uint64_t n) : Number{}
    {
        unsigned char b[8];
        for (unsigned i = 0; i < 8; ++i)
            b[7 - i] = static_cast<unsigned char>(n >> (i * 8));
        if (!BN_bin2bn(b, 8, m_value.get())) throw std::bad_alloc{};
    }
    Number(const Number& n) : m_value{BN_dup(n.m_value.get())}
    {
        if (!m_value) throw std::bad_alloc{};
    }
    Number(Number&&) noexcept = default;
    Number& operator=(Number n) noexcept
    {
        m_value.swap(n.m_value);
        return *this;
    }
    static Number Parse(std::string_view s, bool hex = false)
    {
        if (s.empty() || s.size() > 4096) throw std::invalid_argument("invalid integer");
        for (char c : s)
            if (!(c >= '0' && c <= '9') && !(hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))) throw std::invalid_argument("invalid integer");
        Number n;
        BIGNUM* p = nullptr;
        const std::string str{s};
        int read = hex ? BN_hex2bn(&p, str.c_str()) : BN_dec2bn(&p, str.c_str());
        std::unique_ptr<BIGNUM, Deleter> guard{p};
        if (read != static_cast<int>(str.size())) throw std::invalid_argument("invalid integer");
        n.m_value = std::move(guard);
        return n;
    }
    static Number PowerOfTwo(int bit)
    {
        Number n;
        Check(BN_set_bit(n.m_value.get(), bit));
        return n;
    }
    std::string Decimal() const
    {
        char* p = BN_bn2dec(m_value.get());
        if (!p) throw std::bad_alloc{};
        std::string s{p};
        OPENSSL_free(p);
        return s;
    }
    uint64_t Uint64() const
    {
        if (BN_num_bits(m_value.get()) > 64) throw std::overflow_error("integer exceeds uint64");
        unsigned char b[8]{};
        if (BN_bn2binpad(m_value.get(), b, 8) != 8) throw std::runtime_error("integer conversion failed");
        uint64_t n = 0;
        for (unsigned char c : b)
            n = (n << 8) | c;
        return n;
    }
    bool Zero() const { return BN_is_zero(m_value.get()); }
    friend bool operator==(const Number& a, const Number& b) { return BN_cmp(a.m_value.get(), b.m_value.get()) == 0; }
    friend std::strong_ordering operator<=>(const Number& a, const Number& b)
    {
        int c = BN_cmp(a.m_value.get(), b.m_value.get());
        return c < 0 ? std::strong_ordering::less : c > 0 ? std::strong_ordering::greater :
                                                            std::strong_ordering::equal;
    }
    friend Number operator+(const Number& a, const Number& b)
    {
        Number n;
        Check(BN_add(n.m_value.get(), a.m_value.get(), b.m_value.get()));
        return n;
    }
    friend Number operator-(const Number& a, const Number& b)
    {
        if (a < b) throw std::underflow_error("negative integer");
        Number n;
        Check(BN_sub(n.m_value.get(), a.m_value.get(), b.m_value.get()));
        return n;
    }
    friend Number operator*(const Number& a, const Number& b)
    {
        Number n;
        std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)> ctx{BN_CTX_new(), BN_CTX_free};
        if (!ctx) throw std::bad_alloc{};
        Check(BN_mul(n.m_value.get(), a.m_value.get(), b.m_value.get(), ctx.get()));
        return n;
    }
    static std::pair<Number, Number> DivMod(const Number& a, const Number& b)
    {
        if (b.Zero()) throw std::invalid_argument("zero divisor");
        Number q, r;
        std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)> ctx{BN_CTX_new(), BN_CTX_free};
        if (!ctx) throw std::bad_alloc{};
        Check(BN_div(q.m_value.get(), r.m_value.get(), a.m_value.get(), b.m_value.get(), ctx.get()));
        return {std::move(q), std::move(r)};
    }
    friend Number operator/(const Number& a, const Number& b) { return DivMod(a, b).first; }
    Number& operator+=(const Number& b)
    {
        *this = *this + b;
        return *this;
    }
};

inline Number WorkScore(const Number& target)
{
    const auto numerator = Number::PowerOfTwo(256);
    if (target >= numerator) throw std::invalid_argument("target exceeds 256 bits");
    return numerator / (target + Number{1});
}
} // namespace pool
#endif // BITCOIN_POOL_NUMBER_H
