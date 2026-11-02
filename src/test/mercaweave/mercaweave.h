// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef MERCATURA_TEST_MERCAWEAVE_H
#define MERCATURA_TEST_MERCAWEAVE_H

#include <crypto/mercatura_mldsa.h>
#include <crypto/mercatura_pqhash.h>
#include <primitives/transaction.h>

#include <functional>
#include <stdexcept>

namespace mercaweave {
using Bytes = std::vector<unsigned char>;
using B32 = std::array<unsigned char, 32>;
using H48 = MercaturaPQHash384;
using PublicKey = std::array<unsigned char, MERCATURA_MLDSA65_PUBLIC_KEY_SIZE>;
using Signature = std::array<unsigned char, MERCATURA_MLDSA65_SIGNATURE_SIZE>;
inline constexpr size_t MAX_INPUTS{4096}, MAX_GROUPS{256}, MAX_MEMBERS{1024};
inline constexpr std::string_view CONTEXT{"Mercatura/PQGroupAuth/v1"};

enum class Error {
    Context, Format, Amount, DuplicateOutpoint, Program, Wrapped, ScriptSig,
    Witness, Reference, DuplicateKey, InputsLimit, GroupsLimit, MembersLimit,
    Ownership, Signature, NonV3, Purpose, Keygen, Signing, Randomness, Encoding
};
struct Failure : std::runtime_error {
    Error code;
    explicit Failure(Error e) : std::runtime_error("MercaWeave rejection"), code(e) {}
};

// Minimal stream uses the actual consensus Serialize functions, including
// transaction marker/flag and CompactSize rules. It does not invent a wire format.
struct Writer {
    Bytes bytes;
    void write(std::span<const std::byte> src);
    template<class T> Writer& operator<<(const T& t) { ::Serialize(*this, t); return *this; }
};
class Reader {
    const std::span<const unsigned char> m_bytes;
    size_t m_pos{0};
public:
    explicit Reader(std::span<const unsigned char> b) : m_bytes(b) {}
    void read(std::span<std::byte> dst);
    void skip(size_t n);
    template<class T> Reader& operator>>(T&& t) { ::Unserialize(*this, t); return *this; }
    size_t remaining() const { return m_bytes.size() - m_pos; }
};
template<class T> Bytes SerializeBytes(const T& t) { Writer w; w << t; return std::move(w.bytes); }
Bytes Wire(const CMutableTransaction& tx, bool witness = true);
// Decode only the small research witness grammar: bounds precede allocations.
CScriptWitness DecodeWitness(std::span<const unsigned char> bytes);
// Preflight canonical byte counts and resource bounds before invoking the
// inherited transaction decoder. A decoded transaction is not authorization.
CMutableTransaction DecodeTransaction(std::span<const unsigned char> bytes);

struct Member { uint32_t index; B32 nonce; };
struct Group { uint32_t anchor; PublicKey pk; std::vector<Member> members; };
struct Mapping { std::vector<Group> groups; size_t inputs{0}; };
struct Hashes {
    H48 prev, amount, script, sequence, output, tx, manifest;
};
struct Statistics {
    size_t groups{0}, inputs{0}, ownership_checks{0}, verification_calls{0}, non_v3_calls{0};
    size_t cost() const { return 4 * groups + inputs; }
};
class Authorization;
struct Result {
    bool valid{false};
    std::optional<Error> error;
    Statistics stats;
    std::vector<uint32_t> authorized_inputs;
    std::shared_ptr<const Authorization> authorization;
};
// Caller supplies authenticated spent outputs IN VIN ORDER. This offline module
// does not establish chain membership, maturity, fee policy, or finality.
B32 Commitment(const B32& genesis, std::span<const unsigned char> pk, const B32& nonce);
CScript Output(const B32& genesis, std::span<const unsigned char> pk, const B32& nonce);
Mapping Reconstruct(const CMutableTransaction& tx, const std::vector<CTxOut>& spent);
void CheckOwnership(const Mapping& map, const std::vector<CTxOut>& spent, const B32& genesis, Statistics& stats);
Bytes Manifest(const Mapping& map);
Hashes TransactionHashes(const CMutableTransaction& tx, const std::vector<CTxOut>& spent,
                         const B32& genesis, const Mapping& map);
H48 Digest(const B32& genesis, const Hashes& hashes, const Group& group);

struct KeyPair {
    PublicKey pk{};
    std::array<unsigned char, MERCATURA_MLDSA65_SECRET_KEY_SIZE> sk{};
    explicit KeyPair(const B32& seed);
    ~KeyPair();
    KeyPair(const KeyPair&) = delete;
    KeyPair& operator=(const KeyPair&) = delete;
};
// Exposes explicit randomness for tests and caller-controlled secure entropy.
// A missing/failing source fails closed; this library never defaults to zero.
using RandomSource = std::function<bool(B32&)>;
Signature SignDigest(const H48& digest, const KeyPair& key, const B32& randomness,
                     std::string_view context = CONTEXT);
bool VerifyDigest(const H48& digest, std::span<const unsigned char> pk,
                  std::span<const unsigned char> sig, std::string_view context = CONTEXT);
void Sign(CMutableTransaction& tx, const std::vector<CTxOut>& spent, const B32& genesis,
          const std::vector<std::reference_wrapper<const KeyPair>>& keys, const RandomSource& random);
// Non-v3 validation MUST be provided by the caller. Default is fail-closed.
// This callback is an integration boundary, not an authorization bypass flag.
using NonV3Verifier = std::function<bool(const CMutableTransaction&, const std::vector<CTxOut>&, const B32&, uint32_t)>;
// Immutable offline record; binds exact full wire bytes, authenticated spent
// context and network without defining any additional protocol hash domain.
// This records a completed pass. Verify never accepts a caller-supplied record.
class Authorization final {
    const Bytes m_transaction;
    const std::vector<CTxOut> m_spent;
    const B32 m_genesis;
    const std::vector<uint32_t> m_inputs;
    Authorization(Bytes wire, std::vector<CTxOut> spent, B32 genesis, std::vector<uint32_t> inputs)
        : m_transaction(std::move(wire)), m_spent(std::move(spent)), m_genesis(genesis), m_inputs(std::move(inputs)) {}
    friend Result Verify(const CMutableTransaction&, const std::vector<CTxOut>&, const B32&, const NonV3Verifier&);
public:
    bool Matches(const CMutableTransaction& tx, const std::vector<CTxOut>& spent, const B32& genesis) const
    {
        return genesis == m_genesis && spent == m_spent && Wire(tx) == m_transaction;
    }
    std::span<const uint32_t> Inputs() const { return m_inputs; }
};
Result Verify(const CMutableTransaction& tx, const std::vector<CTxOut>& spent,
              const B32& genesis, const NonV3Verifier& non_v3 = {});
B32 Derive(const B32& master, const B32& genesis, uint32_t account, uint8_t purpose,
           uint32_t group, std::optional<uint32_t> address = std::nullopt);
struct Sizes { size_t total, stripped, witness, weight, fee_bytes; };
Sizes Measure(const CMutableTransaction& tx);
} // namespace mercaweave
#endif
