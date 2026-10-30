// Copyright (c) 2019-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <clientversion.h>
#include <chain.h>
#include <common/args.h>
#include <flatfile.h>
#include <kernel/chain.h>
#include <node/blockstorage.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <test/util/random.h>
#include <test/util/sparse.h>
#include <util/fs_helpers.h>
#include <util/strencodings.h>

#include <limits>

#ifndef WIN32
#include <sys/stat.h>
#endif

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(flatfile_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(flatfile_filename)
{
    const auto data_dir = m_args.GetDataDirBase();

    FlatFilePos pos(456, 789);

    FlatFileSeq seq1(data_dir, "a", 16 * 1024);
    BOOST_CHECK_EQUAL(seq1.FileName(pos), data_dir / "a00456.dat");

    FlatFileSeq seq2(data_dir / "a", "b", 16 * 1024);
    BOOST_CHECK_EQUAL(seq2.FileName(pos), data_dir / "a" / "b00456.dat");

    // Check default constructor IsNull
    assert(FlatFilePos{}.IsNull());
}

BOOST_AUTO_TEST_CASE(flatfile_open)
{
    const auto data_dir = m_args.GetDataDirBase();
    FlatFileSeq seq(data_dir, "a", 16 * 1024);

    std::string line1("A purely peer-to-peer version of electronic cash would allow online "
                      "payments to be sent directly from one party to another without going "
                      "through a financial institution.");
    std::string line2("Digital signatures provide part of the solution, but the main benefits are "
                      "lost if a trusted third party is still required to prevent double-spending.");

    uint64_t pos1{0};
    uint64_t pos2{pos1 + GetSerializeSize(line1)};

    // Write first line to file.
    {
        AutoFile file{seq.Open(FlatFilePos(0, pos1))};
        file << LIMITED_STRING(line1, 256);
        BOOST_REQUIRE_EQUAL(file.fclose(), 0);
    }

    // Attempt to append to file opened in read-only mode.
    {
        AutoFile file{seq.Open(FlatFilePos(0, pos2), true)};
        BOOST_CHECK_THROW(file << LIMITED_STRING(line2, 256), std::ios_base::failure);
    }

    // Append second line to file.
    {
        AutoFile file{seq.Open(FlatFilePos(0, pos2))};
        file << LIMITED_STRING(line2, 256);
        BOOST_REQUIRE_EQUAL(file.fclose(), 0);
    }

    // Read text from file in read-only mode.
    {
        std::string text;
        AutoFile file{seq.Open(FlatFilePos(0, pos1), true)};

        file >> LIMITED_STRING(text, 256);
        BOOST_CHECK_EQUAL(text, line1);

        file >> LIMITED_STRING(text, 256);
        BOOST_CHECK_EQUAL(text, line2);
    }

    // Read text from file with position offset.
    {
        std::string text;
        AutoFile file{seq.Open(FlatFilePos(0, pos2))};

        file >> LIMITED_STRING(text, 256);
        BOOST_CHECK_EQUAL(text, line2);
        BOOST_REQUIRE_EQUAL(file.fclose(), 0);
    }

    // Ensure another file in the sequence has no data.
    {
        std::string text;
        AutoFile file{seq.Open(FlatFilePos(1, pos2))};
        BOOST_CHECK_THROW(file >> LIMITED_STRING(text, 256), std::ios_base::failure);
        BOOST_REQUIRE_EQUAL(file.fclose(), 0);
    }
}

BOOST_AUTO_TEST_CASE(flatfile_allocate)
{
    const auto data_dir = m_args.GetDataDirBase();
    FlatFileSeq seq(data_dir, "a", 100);

    bool out_of_space;

    BOOST_CHECK_EQUAL(seq.Allocate(FlatFilePos(0, 0), 1, out_of_space), 100U);
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 0))), 100U);
    BOOST_CHECK(!out_of_space);

    BOOST_CHECK_EQUAL(seq.Allocate(FlatFilePos(0, 99), 1, out_of_space), 0U);
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 99))), 100U);
    BOOST_CHECK(!out_of_space);

    BOOST_CHECK_EQUAL(seq.Allocate(FlatFilePos(0, 99), 2, out_of_space), 101U);
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 99))), 200U);
    BOOST_CHECK(!out_of_space);
}

BOOST_AUTO_TEST_CASE(flatfile_flush)
{
    const auto data_dir = m_args.GetDataDirBase();
    FlatFileSeq seq(data_dir, "a", 100);

    bool out_of_space;
    seq.Allocate(FlatFilePos(0, 0), 1, out_of_space);

    // Flush without finalize should not truncate file.
    seq.Flush(FlatFilePos(0, 1));
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 1))), 100U);

    // Flush with finalize should truncate file.
    seq.Flush(FlatFilePos(0, 1), true);
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 1))), 1U);
}

BOOST_AUTO_TEST_CASE(position_serialization_64bit)
{
    LOCK(cs_main);
    static_assert(sizeof(FlatFilePos::nPos) == 8);
    static_assert(sizeof(CBlockIndex::nDataPos) == 8);
    static_assert(sizeof(CBlockIndex::nUndoPos) == 8);
    static_assert(sizeof(kernel::CBlockFileInfo::nSize) == 8);
    static_assert(sizeof(kernel::CBlockFileInfo::nUndoSize) == 8);
    static_assert(sizeof(interfaces::BlockInfo::data_pos) == 8);
    const uint64_t boundary{uint64_t{1} << 32};
    FastRandomContext random{true};
    for (unsigned sample{0}; sample < 512; ++sample) {
        const uint64_t value{sample < 256 ? random.rand32() : random.rand64()};
        FlatFilePos pos{7, value};
        DataStream stream;
        stream << pos;
        FlatFilePos decoded;
        stream >> decoded;
        BOOST_CHECK(pos == decoded);

        kernel::CBlockFileInfo info;
        info.nBlocks = 2;
        info.nSize = value;
        info.nUndoSize = value;
        info.nHeightFirst = 3;
        info.nHeightLast = 4;
        info.nTimeFirst = 100;
        info.nTimeLast = 200;
        DataStream current;
        current << info;
        kernel::CBlockFileInfo read_info;
        DataStream copy{current};
        copy >> read_info;
        BOOST_CHECK_EQUAL(read_info.nSize, value);
        BOOST_CHECK_EQUAL(read_info.nUndoSize, value);

        CDiskBlockIndex index;
        index.nHeight = 12;
        index.nStatus = BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO;
        index.nTx = 3;
        index.nFile = 7;
        index.nDataPos = value;
        index.nUndoPos = value;
        DataStream disk;
        disk << index;
        CDiskBlockIndex read_index;
        DataStream disk_copy{disk};
        disk_copy >> read_index;
        BOOST_CHECK_EQUAL(read_index.GetBlockPos().nPos, value);
        BOOST_CHECK_EQUAL(read_index.GetUndoPos().nPos, value);

        if (value < boundary) {
            // Reproduce the exact old 32-bit field types and field order.
            const auto old_pos{static_cast<uint32_t>(value)};
            const int32_t file{7};
            DataStream old;
            old << VARINT_MODE(file, VarIntMode::NONNEGATIVE_SIGNED) << VARINT(old_pos);
            DataStream now;
            now << pos;
            BOOST_CHECK_EQUAL(old.str(), now.str());
            DataStream old_info;
            old_info << VARINT(info.nBlocks) << VARINT(old_pos) << VARINT(old_pos)
                     << VARINT(info.nHeightFirst) << VARINT(info.nHeightLast)
                     << VARINT(info.nTimeFirst) << VARINT(info.nTimeLast);
            BOOST_CHECK_EQUAL(old_info.str(), current.str());
            const int version{259900};
            DataStream old_index;
            old_index << VARINT_MODE(version, VarIntMode::NONNEGATIVE_SIGNED)
                      << VARINT_MODE(index.nHeight, VarIntMode::NONNEGATIVE_SIGNED)
                      << VARINT(index.nStatus) << VARINT(index.nTx)
                      << VARINT_MODE(index.nFile, VarIntMode::NONNEGATIVE_SIGNED)
                      << VARINT(old_pos) << VARINT(old_pos)
                      << index.nVersion << index.hashPrev << index.hashMerkleRoot
                      << index.nTime << index.nBits << index.nNonce;
            BOOST_CHECK_EQUAL(old_index.str(), disk.str());
        }
    }
    for (const uint64_t value : {uint64_t{0}, uint64_t{127}, uint64_t{128}, boundary - 1,
                                 boundary, boundary + 1, uint64_t{1} << 48,
                                 uint64_t{std::numeric_limits<int64_t>::max()}, std::numeric_limits<uint64_t>::max()}) {
        const FlatFilePos pos{7, value};
        DataStream stream;
        stream << pos;
        if (value == boundary - 1) BOOST_CHECK_EQUAL(HexStr(stream), "078efefefe7f");
        FlatFilePos decoded;
        stream >> decoded;
        BOOST_CHECK(pos == decoded);
        BOOST_CHECK(pos.ToString().find(std::to_string(value)) != std::string::npos);
    }
    DataStream malformed{std::vector<uint8_t>(20, 0xff)};
    FlatFilePos invalid;
    BOOST_CHECK_THROW(malformed >> invalid, std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(sparse_large_positions_and_allocation_bounds)
{
    FlatFileSeq seq{m_args.GetDataDirBase(), "large", 100};
    {
        FILE* raw{seq.Open({0, 0})};
        AutoFile file{raw};
        BOOST_REQUIRE(!file.IsNull());
        BOOST_REQUIRE(MakeTestFileSparse(raw));
    }
    const uint64_t position{(uint64_t{1} << 32) - 4};
    const uint64_t end{((position + 16) / 100 + 1) * 100};
    bool out_of_space{false};
    BOOST_CHECK_EQUAL(seq.Allocate({0, position}, 16, out_of_space), end - position);
    BOOST_REQUIRE(!out_of_space);
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName({0, 0})), end);
    {
        FILE* raw{seq.Open({0, position + 32})};
        AutoFile file{raw};
        BOOST_REQUIRE(!file.IsNull());
        BOOST_CHECK_EQUAL(FileTell64(raw), position + 32);
        BOOST_CHECK_EQUAL(file.tell(), position + 32);
        file << uint32_t{0x12345678};
        file.seek(-4, SEEK_CUR);
        uint32_t read{};
        file >> read;
        BOOST_CHECK_EQUAL(read, 0x12345678U);
        BOOST_REQUIRE(file.Truncate(position + 36));
        file.seek(-1, SEEK_END);
        BOOST_CHECK_EQUAL(file.tell(), position + 35);
        BOOST_CHECK(!file.Truncate(std::numeric_limits<uint64_t>::max()));
    }
    BOOST_REQUIRE(seq.Flush({0, position + 36}, true));
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName({0, 0})), position + 36);
#ifndef WIN32
    struct stat info{};
    BOOST_REQUIRE_EQUAL(stat(seq.FileName({0, 0}).c_str(), &info), 0);
    BOOST_CHECK_LT(static_cast<uint64_t>(info.st_blocks) * 512, uint64_t{1} << 20);
#endif
    BOOST_CHECK(seq.Open({1, std::numeric_limits<uint64_t>::max()}) == nullptr);
    for (const auto& [offset, size] : {std::pair{std::numeric_limits<uint64_t>::max() - 1, uint64_t{2}},
                                     std::pair{uint64_t{std::numeric_limits<int64_t>::max()} / 100 * 100, uint64_t{1}},
                                     std::pair{uint64_t{0}, std::numeric_limits<uint64_t>::max()}}) {
        BOOST_CHECK_EQUAL(seq.Allocate({1, offset}, size, out_of_space), 0U);
        BOOST_CHECK(out_of_space);
    }
    BOOST_CHECK(!fs::exists(seq.FileName({1, 0})));
}

BOOST_AUTO_TEST_SUITE_END()
