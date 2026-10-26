// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <net_pq.h>
#include <net_pq_identity_format.h>
#include <random.h>
#include <stdexcept>
#include <filesystem>

#ifndef WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace pq {
std::shared_ptr<const Identity> LoadIdentity(const std::string& path, bool create, const Network& network)
{
#ifdef WIN32
    // Fail closed until owner-only ACL and atomic no-clobber publication are qualified.
    throw std::runtime_error("Stage A persistent PQ identity storage is not yet supported on Windows; use ephemeral identities");
#else
    namespace fs = std::filesystem;
    const fs::path file(path);
    auto parent = file.parent_path(); if (parent.empty()) parent = ".";
    struct stat dir{};
    if (lstat(parent.c_str(), &dir) || !S_ISDIR(dir.st_mode) || dir.st_uid != geteuid() || (dir.st_mode & 0077)) throw std::runtime_error("PQ identity directory must be owner-only (0700), owned by this user and not a symlink");
    int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0) throw std::runtime_error("Cannot open PQ identity directory");
    struct FD { int fd; ~FD() { if (fd >= 0) close(fd); } } directory_guard{directory};
    struct stat opened_dir{};
    if (fstat(directory, &opened_dir) || !S_ISDIR(opened_dir.st_mode) || opened_dir.st_uid != geteuid() || (opened_dir.st_mode & 0077) || opened_dir.st_dev != dir.st_dev || opened_dir.st_ino != dir.st_ino) throw std::runtime_error("PQ identity directory changed or is unsafe");
    IdentityFile contents;
    const auto name = file.filename().string();
    if (name.empty() || name == "." || name == "..") throw std::runtime_error("Invalid PQ identity filename");
    if (create) {
        auto id = std::make_shared<Identity>();
        Secret<32> seed; GetStrongRandBytes(seed.bytes);
        if (!id->FromSeed(seed.bytes)) throw std::runtime_error("PQ identity generation failed");
        EncodeIdentityFile(network, seed.bytes, id->pub, contents);
        // An unpredictable exclusive temporary in the already opened directory.
        std::array<uint8_t, 16> random; GetStrongRandBytes(random);
        constexpr char hex[]{"0123456789abcdef"}; std::string temp{".mcapq-"};
        for (auto b : random) { temp += hex[b >> 4]; temp += hex[b & 15]; }
        FD output{openat(directory, temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600)};
        if (output.fd < 0) throw std::runtime_error("Cannot exclusively create PQ identity temporary");
        try {
            // open(0600) is filtered by umask. Set and validate the permissions
            // on the opened inode before writing any secret, so an unusually
            // restrictive umask cannot publish an identity that cannot be loaded.
            struct stat temporary{};
            if (fchmod(output.fd, 0600) || fstat(output.fd, &temporary) ||
                !S_ISREG(temporary.st_mode) || temporary.st_uid != geteuid() ||
                (temporary.st_mode & 07777) != 0600 || temporary.st_nlink != 1) {
                throw std::runtime_error("Unsafe PQ identity temporary owner/permissions/type");
            }
            size_t pos = 0;
            while (pos < contents.bytes.size()) {
                auto count = write(output.fd, contents.bytes.data()+pos, contents.bytes.size()-pos);
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0) throw std::runtime_error("Cannot write PQ identity");
                pos += count;
            }
            if (fsync(output.fd)) throw std::runtime_error("Cannot sync PQ identity");
            // linkat publishes without replacing an existing name on POSIX.
            if (linkat(directory, temp.c_str(), directory, name.c_str(), 0)) throw std::runtime_error("Cannot publish PQ identity (existing identity is never replaced)");
            if (unlinkat(directory, temp.c_str(), 0) || fsync(directory)) throw std::runtime_error("Cannot durably publish PQ identity; inspect the requested path before retrying");
        } catch (...) { unlinkat(directory, temp.c_str(), 0); throw; }
        return id;
    }
    FD input{openat(directory, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
    struct stat st{};
    if (input.fd < 0 || fstat(input.fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077) || st.st_nlink != 1 || st.st_size != 2127) throw std::runtime_error("Invalid/missing PQ identity or unsafe owner/permissions/type/size");
    size_t pos{0};
    while (pos < contents.bytes.size()) {
        auto count = read(input.fd, contents.bytes.data()+pos, contents.bytes.size()-pos);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("Cannot read PQ identity");
        pos += count;
    }
    uint8_t extra; if (read(input.fd, &extra, 1) != 0) throw std::runtime_error("PQ identity has trailing bytes");
    auto loaded=ParseIdentityFile(contents.bytes,network);
    if (!loaded) throw std::runtime_error("PQ identity format/network/checksum/key mismatch");
    return loaded;
#endif
}
} // namespace pq
