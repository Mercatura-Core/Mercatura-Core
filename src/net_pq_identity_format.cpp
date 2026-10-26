// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <net_pq_identity_format.h>

namespace pq {
namespace {
constexpr std::string_view MAGIC{"MCAPQID1"};
Digest Checksum(std::span<const uint8_t> contents)
{
    SHA3_384 hash;
    Digest checksum;
    hash.Write(Domain("Mercatura/P2PIdentityFile/v1")).Write(contents).Finalize(checksum);
    memory_cleanse(&hash, sizeof(hash));
    return checksum;
}
}
void EncodeIdentityFile(const Network& network, std::span<const uint8_t,32> seed,
                        const IdentityPublic& pub, IdentityFile& contents)
{
    contents.Clear();
    std::copy(MAGIC.begin(), MAGIC.end(), contents.bytes.begin());
    contents.bytes[8] = 1;
    std::copy(network.begin(), network.end(), contents.bytes.begin()+10);
    std::copy(seed.begin(), seed.end(), contents.bytes.begin()+95);
    std::copy(pub.begin(), pub.end(), contents.bytes.begin()+127);
    auto checksum = Checksum(std::span{contents.bytes}.first(2079));
    std::copy(checksum.begin(), checksum.end(), contents.bytes.begin()+2079);
}
std::shared_ptr<const Identity> ParseIdentityFile(std::span<const uint8_t> contents, const Network& network)
{
    if (contents.size() != IDENTITY_FILE_SIZE ||
        !std::equal(MAGIC.begin(), MAGIC.end(), contents.begin()) ||
        contents[8] != 1 || contents[9] != 0 || !Equal(contents.subspan(10,85), network)) return {};
    if (!Equal(Checksum(contents.first(2079)), contents.last(48))) return {};
    auto identity = std::make_shared<Identity>();
    if (!identity->FromSeed(contents.subspan(95,32)) || !Equal(identity->pub, contents.subspan(127,1952))) return {};
    return identity;
}
std::optional<Digest> ParseFingerprint(std::string_view text)
{
    if (text.size() != 103 || !text.starts_with("mcapq1:")) return {};
    Digest result{};
    for (size_t n=0;n<96;++n) {
        const auto c=text[n+7];
        const int value=c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : -1;
        if (value<0) return {};
        result[n/2] |= uint8_t(value) << (n%2 ? 0 : 4);
    }
    return result;
}
std::optional<PinBinding> ParsePinBinding(std::string_view text)
{
    const auto equal=text.find('=');
    if (equal==std::string_view::npos || equal==0) return {};
    auto fp=ParseFingerprint(text.substr(equal+1));
    if (!fp) return {};
    const auto destination=text.substr(0,equal);
    // Pins must have the same explicit host:port spelling used by the outbound
    // connection. Reject common unused-binding mistakes rather than silently
    // accepting an omitted port, whitespace or an unbracketed IPv6 destination.
    if (!std::ranges::all_of(destination,[](unsigned char c) { return c>32 && c<127; })) return {};
    const auto colon=destination.rfind(':');
    if (colon==std::string_view::npos || colon==0 || colon+1==destination.size()) return {};
    auto host=destination.substr(0,colon);
    if (host.front()=='[') {
        if (host.size()<3 || host.back()!=']') return {};
    } else if (host.find_first_of(":[]")!=std::string_view::npos) return {};
    unsigned port{0};
    for (char c:destination.substr(colon+1)) {
        if (c<'0' || c>'9' || port>6553) return {};
        port=port*10+unsigned(c-'0');
        if (port>65535) return {};
    }
    if (!port) return {};
    return PinBinding{std::string{destination},*fp};
}
} // namespace pq
