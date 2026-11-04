// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_TEST_MERCAWEAVE_RESEARCH_WALLET_H
#define BITCOIN_TEST_MERCAWEAVE_RESEARCH_WALLET_H
#include <test/mercaweave/wallet_seed_bridge.h>
#include <test/mercaweave/isolated_validation.h>
#include <util/fs.h>
#include <map>
#include <mutex>
#include <tuple>
namespace mercaweave::wallet_research {
enum class Code { Locked, WatchOnly, Metadata, Network, Version, Index, Ownership, Inputs, Funds, Mode, Policy, Fee, PSBT, Conflict, Signature, Entropy };
struct WalletError : std::runtime_error {
    Code code;
    WalletError(Code c,const char* message):std::runtime_error(message),code(c){}
};
void Require(bool,Code,const char*);
struct Location {
    uint32_t account{0};uint8_t purpose{0};uint32_t group{0},address{0};uint8_t witness{3};
    auto operator<=>(const Location&)const=default;
    SERIALIZE_METHODS(Location,o){READWRITE(o.account,o.purpose,o.group,o.address,o.witness);}
};
struct Address {
    Location location;
    PublicKey pk{};B32 nonce{};CScript script;
    bool operator==(const Address&)const=default;
    SERIALIZE_METHODS(Address,o){READWRITE(o.location,o.pk,o.nonce,o.script);}
};
struct Cursor { uint32_t group{0},next{0}; bool operator==(const Cursor&)const=default; };
// Pure checked wallet-policy transition. 64 issued addresses per v3 group;
// payments/reuse never call this transition. This is not a consensus limit.
std::pair<Cursor,Cursor> NextCursor(Cursor,uint8_t witness);
class Wallet final {
    using Branch=std::tuple<uint32_t,uint8_t,uint8_t>;
    mutable std::recursive_mutex m_mutex;
    const B32 m_genesis;
    bool m_watch{false},m_verified{false};
    wallet::CMasterKey m_master;
    wallet::MercaturaPQCryptedSeed m_encrypted;
    wallet::CKeyingMaterial m_seed;
    std::map<Branch,Cursor> m_cursors;
    std::vector<Address> m_addresses;
    std::map<CScript,size_t> m_index;
    H48 m_mac{};
    uint32_t m_lookahead{64};
    explicit Wallet(B32);
    Bytes Metadata()const;
    H48 Mac(const wallet::CKeyingMaterial&)const;
    void Authenticate();
    void ParseMetadata(std::span<const unsigned char>);
    Address DeriveRecord(Location)const;
    void Reindex();
public:
    static constexpr uint32_t SCHEME=1,DERIVATION=1,ROTATION=64;
    static constexpr size_t MAX_RECORDS=4096;
    // Import the existing authoritative PQ seed, never generate another owner
    // seed. Native CMasterKey encrypts it using unchanged wallet.cpp v1 helpers.
    static std::unique_ptr<Wallet> Create(const wallet::CKeyingMaterial&,B32,const SecureString&);
    static std::unique_ptr<Wallet> Restore(std::span<const unsigned char>,B32);
    static std::unique_ptr<Wallet> Watch(B32,std::span<const Address>);
    ~Wallet();
    bool Unlock(const SecureString&);
    void Lock();
    bool IsLocked()const;
    bool IsWatchOnly()const{return m_watch;}
    const B32& Genesis()const{return m_genesis;}
    // Matches CWallet's protected-seed callback contract. No normal wallet/RPC
    // exposure is added. Seed access lasts only for the authorized callback.
    bool WithMercaturaPQMasterSeed(const std::function<bool(const wallet::CKeyingMaterial&)>&)const;
    Address Issue(uint32_t account=0,uint8_t purpose=0,uint8_t witness=3);
    void Rotate(uint32_t account=0,uint8_t purpose=0);
    std::vector<Address> Records()const;
    std::optional<Address> Find(const CScript&)const;
    std::unique_ptr<KeyPair> Key(const Address&)const;
    // Explicit issued ranges are necessary to recover unused issued addresses.
    // Import rederives every owned record; watch-only never derives children.
    void RecoverIssued(std::span<const Address>);
    Bytes Backup()const;
    static fs::path StorageRoot();
    void Save(const fs::path&)const;
    static std::unique_ptr<Wallet> Load(const fs::path&,B32);
    static std::string EncodeResearchAddress(const Address&,std::string_view native_hrp);
};
bool SecureRandom(B32&);
} // namespace mercaweave::wallet_research
#endif // BITCOIN_TEST_MERCAWEAVE_RESEARCH_WALLET_H
