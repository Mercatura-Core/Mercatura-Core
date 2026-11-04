// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_WALLET_PQGROUP_RECORDS_H
#define BITCOIN_WALLET_PQGROUP_RECORDS_H
#include <script/pqgroup.h>

namespace wallet {
/** Public derivation metadata only. The native CWallet PQ seed is authoritative. */
struct PQGroupRecord {
    uint32_t account{0};
    uint8_t purpose{0};
    uint32_t group{0}, address{0};
    mercaweave::PublicKey pk{};
    mercaweave::B32 nonce{};
    SERIALIZE_METHODS(PQGroupRecord, obj) { READWRITE(obj.account,obj.purpose,obj.group,obj.address,obj.pk,obj.nonce); }
    bool operator==(const PQGroupRecord&) const = default;
    CScript Script(const mercaweave::B32& genesis) const { return mercaweave::Output(genesis,pk,nonce); }
};
struct PQGroupRecords {
    static constexpr size_t MAX_RECORDS{4096};
    uint32_t version{1}, derivation{1};
    mercaweave::B32 genesis{};
    std::vector<PQGroupRecord> issued;
    mercaweave::H48 mac{};

    template<typename Stream> void Serialize(Stream& stream) const
    {
        stream << version << derivation << genesis;
        WriteCompactSize(stream,issued.size());
        for (const auto& record : issued) stream << record;
        stream << mac;
    }
    template<typename Stream> void Unserialize(Stream& stream)
    {
        stream >> version >> derivation >> genesis;
        const auto count = ReadCompactSize(stream);
        if (version != 1 || derivation != 1 || count > MAX_RECORDS)
            throw std::ios_base::failure("unsupported or oversized PQ group records");
        issued.clear(); issued.reserve(count);
        for (size_t i = 0; i < count; ++i) { PQGroupRecord record; stream >> record; issued.push_back(record); }
        stream >> mac;
    }
    bool StructurallyValid() const;
    mercaweave::H48 Authenticate(std::span<const unsigned char> seed) const;
};
}
#endif // BITCOIN_WALLET_PQGROUP_RECORDS_H
