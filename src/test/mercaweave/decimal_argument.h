// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_TEST_MERCAWEAVE_DECIMAL_ARGUMENT_H
#define BITCOIN_TEST_MERCAWEAVE_DECIMAL_ARGUMENT_H
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace mercaweave::test {
// Preserve the old base-10 stoull command-line grammar (ASCII whitespace,
// optional sign, decimal prefix and uint64 range), independently of locale.
inline uint64_t DecimalArgument(std::string_view text)
{
    const auto first = text.find_first_not_of(" \f\n\r\t\v");
    if (first == std::string_view::npos) throw std::invalid_argument("decimal argument");
    text.remove_prefix(first);
    const bool negative = text.front() == '-';
    if (negative || text.front() == '+') text.remove_prefix(1);
    uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (parsed.ec == std::errc::result_out_of_range) throw std::out_of_range("decimal argument");
    if (parsed.ec != std::errc{}) throw std::invalid_argument("decimal argument");
    return negative ? uint64_t{0} - value : value;
}
} // namespace mercaweave::test
#endif // BITCOIN_TEST_MERCAWEAVE_DECIMAL_ARGUMENT_H
