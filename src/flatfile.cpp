// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <flatfile.h>

#include <tinyformat.h>
#include <util/fs_helpers.h>
#include <util/log.h>

#include <limits>
#include <stdexcept>
#include <utility>

FlatFileSeq::FlatFileSeq(fs::path dir, const char* prefix, size_t chunk_size,
                         std::function<bool(FILE*, const fs::path&)> file_commit) :
    m_dir(std::move(dir)),
    m_prefix(prefix),
    m_chunk_size(chunk_size),
    m_file_commit(std::move(file_commit))
{
    if (chunk_size == 0) {
        throw std::invalid_argument("chunk_size must be positive");
    }
}

std::string FlatFilePos::ToString() const
{
    return strprintf("FlatFilePos(nFile=%i, nPos=%i)", nFile, nPos);
}

fs::path FlatFileSeq::FileName(const FlatFilePos& pos) const
{
    return m_dir / fs::u8path(strprintf("%s%05u.dat", m_prefix, pos.nFile));
}

FILE* FlatFileSeq::Open(const FlatFilePos& pos, bool read_only) const
{
    if (pos.IsNull() || !std::in_range<int64_t>(pos.nPos)) {
        return nullptr;
    }
    fs::path path = FileName(pos);
    fs::create_directories(path.parent_path());
    FILE* file = fsbridge::fopen(path, read_only ? "rb": "rb+");
    if (!file && !read_only)
        file = fsbridge::fopen(path, "wb+");
    if (!file) {
        LogError("Unable to open file %s", fs::PathToString(path));
        return nullptr;
    }
    if (pos.nPos && FileSeek64(file, static_cast<int64_t>(pos.nPos), SEEK_SET)) {
        LogError("Unable to seek to position %u of %s", pos.nPos, fs::PathToString(path));
        if (fclose(file) != 0) {
            LogError("Unable to close file %s", fs::PathToString(path));
        }
        return nullptr;
    }
    return file;
}

size_t FlatFileSeq::Allocate(const FlatFilePos& pos, uint64_t add_size, bool& out_of_space) const
{
    out_of_space = false;

    constexpr uint64_t max_position{std::numeric_limits<int64_t>::max()};
    if (pos.nPos > max_position || add_size > max_position - pos.nPos) {
        out_of_space = true;
        return 0;
    }
    const uint64_t end{pos.nPos + add_size};
    // Division followed by a remainder test avoids overflowing during round-up.
    const uint64_t n_old_chunks{pos.nPos / m_chunk_size + (pos.nPos % m_chunk_size != 0)};
    const uint64_t n_new_chunks{end / m_chunk_size + (end % m_chunk_size != 0)};
    if (n_new_chunks > n_old_chunks) {
        if (n_new_chunks > max_position / m_chunk_size) {
            out_of_space = true;
            return 0;
        }
        const uint64_t new_size{n_new_chunks * m_chunk_size};
        const uint64_t increment{new_size - pos.nPos};
        if (!std::in_range<size_t>(increment)) {
            out_of_space = true;
            return 0;
        }
        const size_t inc_size{static_cast<size_t>(increment)};

        if (CheckDiskSpace(m_dir, inc_size)) {
            FILE *file = Open(pos);
            if (file) {
                LogDebug(BCLog::VALIDATION, "Pre-allocating up to position 0x%x in %s%05u.dat\n", new_size, m_prefix, pos.nFile);
                AllocateFileRange(file, pos.nPos, inc_size);
                if (fclose(file) != 0) {
                    LogError("Cannot close file %s%05u.dat after extending it with %u bytes", m_prefix, pos.nFile, new_size);
                    return 0;
                }
                return inc_size;
            }
        } else {
            out_of_space = true;
        }
    }
    return 0;
}

bool FlatFileSeq::Flush(const FlatFilePos& pos, bool finalize) const
{
    FILE* file = Open(FlatFilePos(pos.nFile, 0)); // Avoid fseek to nPos
    if (!file) {
        LogError("%s: failed to open file %d\n", __func__, pos.nFile);
        return false;
    }
    if (finalize && !TruncateFile(file, pos.nPos)) {
        LogError("%s: failed to truncate file %d\n", __func__, pos.nFile);
        if (fclose(file) != 0) {
            LogError("Failed to close file %d", pos.nFile);
        }
        return false;
    }
    if (!(m_file_commit ? m_file_commit(file, FileName(pos)) : FileCommit(file))) {
        LogError("%s: failed to commit file %d\n", __func__, pos.nFile);
        if (fclose(file) != 0) {
            LogError("Failed to close file %d", pos.nFile);
        }
        return false;
    }
    DirectoryCommit(m_dir);

    if (fclose(file) != 0) {
        LogError("Failed to close file %d after flush", pos.nFile);
        return false;
    }
    return true;
}
