#include <crypto/mercatura_pqhash.h>

#include <cstdint>
#include <span>

MercaturaPQHashWriter::MercaturaPQHashWriter(std::string_view tag)
{
    WriteCompactSize(*this, static_cast<uint64_t>(tag.size()));
    write(std::as_bytes(std::span{tag.data(), tag.size()}));
}

void MercaturaPQHashWriter::write(std::span<const std::byte> src)
{
    m_hasher.Write({
        reinterpret_cast<const unsigned char*>(src.data()),
        src.size()
    });
}

MercaturaPQHash384 MercaturaPQHashWriter::GetHash()
{
    MercaturaPQHash384 result{};
    m_hasher.Finalize(result);
    return result;
}

MercaturaPQHash384 PQH384(
    std::string_view tag,
    std::span<const unsigned char> data)
{
    MercaturaPQHashWriter writer{tag};
    writer.write(std::as_bytes(data));
    return writer.GetHash();
}
