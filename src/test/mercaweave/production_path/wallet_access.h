// Test-only full-wallet helper; core queue tests need no generated node headers.
#ifndef BITCOIN_TEST_MERCAWEAVE_PRODUCTION_PATH_WALLET_ACCESS_H
#define BITCOIN_TEST_MERCAWEAVE_PRODUCTION_PATH_WALLET_ACCESS_H
#include <test/mercaweave/production_path/access.h>
#include <wallet/wallet.h>
namespace mercaweave {
inline void ProductionPathTestAccess::Attach(wallet::CWallet& wallet, std::shared_ptr<const IsolatedExecution> execution)
{
    LOCK(wallet.cs_wallet); wallet.m_group_execution = std::move(execution);
}
inline std::optional<wallet::PQGroupRecord> ProductionPathTestAccess::Issue(wallet::CWallet& wallet, uint32_t account, uint8_t purpose)
{
    return wallet.IssuePQGroupAddress(account,purpose);
}
inline void ProductionPathTestAccess::Entropy(wallet::CWallet& wallet, RandomSource source)
{
    LOCK(wallet.cs_wallet); wallet.m_pq_group_test_entropy = std::move(source);
}
inline wallet::PQGroupRecords ProductionPathTestAccess::Records(const wallet::CWallet& wallet)
{
    LOCK(wallet.cs_wallet); return wallet.m_pq_group_records.value();
}
}
#endif // BITCOIN_TEST_MERCAWEAVE_PRODUCTION_PATH_WALLET_ACCESS_H
