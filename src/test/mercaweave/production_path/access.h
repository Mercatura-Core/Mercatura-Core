// This header is never included or linked by an ordinary executable.
#ifndef MERCATURA_MERCAWEAVE_PRODUCTION_PATH_TEST_ONLY
#error "Production-path fixture access is forbidden in ordinary builds"
#endif
#ifndef MERCATURA_TEST_PRODUCTION_PATH_ACCESS_H
#define MERCATURA_TEST_PRODUCTION_PATH_ACCESS_H
#include <script/pqgroup_context.h>
#include <validation.h>
namespace wallet { class CWallet; struct PQGroupRecord; struct PQGroupRecords; }

namespace mercaweave {
class ProductionPathTestAccess final {
public:
    static std::shared_ptr<const IsolatedExecution> Execution(const B32& genesis)
    {
        return std::shared_ptr<const IsolatedExecution>(new IsolatedExecution(genesis));
    }
    static void Attach(Chainstate& state, std::shared_ptr<const IsolatedExecution> execution)
    {
        LOCK(cs_main);
        state.m_group_execution = std::move(execution);
    }
    static void Attach(wallet::CWallet&, std::shared_ptr<const IsolatedExecution>);
    static std::optional<wallet::PQGroupRecord> Issue(wallet::CWallet&, uint32_t account = 0, uint8_t purpose = 0);
    static void Entropy(wallet::CWallet&, RandomSource);
    static wallet::PQGroupRecords Records(const wallet::CWallet&);
};
}
#endif
