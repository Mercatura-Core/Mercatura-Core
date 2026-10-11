// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#ifndef BITCOIN_QT_MININGSESSION_H
#define BITCOIN_QT_MININGSESSION_H

#ifndef Q_MOC_RUN
#include <mining/cpu_miner.h>
#include <qt/poolclient.h>
#include <qt/walletmodel.h>
#endif

#include <QObject>
#include <QPointer>

namespace interfaces { class Node; }
class QTimer;

// One instance per WalletController, shared by every wallet's Mining page.
class MiningSession : public QObject {
    Q_OBJECT
public:
    struct GpuDiscoverySnapshot {
        mining::GpuDiscovery result;
        uint64_t revision{0};
    };
    explicit MiningSession(interfaces::Node& node, QObject* parent = nullptr, mining::MiningController::DeviceDetector devices = {});
    ~MiningSession();
    bool start(WalletModel* wallet, unsigned int workers, mining::GpuConfig hardware = {});
    bool startPool(WalletModel* wallet, unsigned int workers, mining::PoolEndpoint endpoint, mining::GpuConfig hardware = {});
    void refreshDevices();
    std::optional<mining::GpuDiscovery> gpuDiscovery() const;
    std::optional<GpuDiscoverySnapshot> gpuDiscoverySnapshot() const;
    mining::PoolStats poolStats() const { return m_pool_state->Snapshot(); }
    bool poolMode() const { return m_pool_mode; }
    void forgetPoolSession()
    {
        if (!stats().busy) m_pool_state->ForgetSession();
    }
    void stop();
    void walletUnloaded(WalletModel* wallet);
    void shutdown();
    mining::MiningStats stats() const { return m_controller.GetStats(); }
    WalletModel* owner() const { return m_owner; }

Q_SIGNALS:
    void changed();

private:
    bool startMode(WalletModel* wallet, unsigned int workers);
    void poll();
    interfaces::Node& m_node;
    mining::MiningController m_controller;
    QPointer<WalletModel> m_owner;
    unsigned int m_requested{0};
    mining::GpuConfig m_hardware;
    const mining::MiningController::DeviceDetector m_detect_devices;
    mutable std::mutex m_discovery_mutex;
    std::optional<GpuDiscoverySnapshot> m_discovery;
    uint64_t m_discovery_revision{0};
    std::thread m_discovery_thread;
    std::atomic<bool> m_discovering{false};
    bool m_pool_mode{false};
    mining::PoolEndpoint m_endpoint;
    const std::shared_ptr<mining::PoolState> m_pool_state{std::make_shared<mining::PoolState>()};
    QTimer* m_timer;
    std::atomic<bool> m_needs_unlock{false};
    std::atomic<bool> m_cancelled{false};
    std::shared_ptr<std::atomic<bool>> m_unload_token;
    std::unique_ptr<WalletModel::UnlockContext> m_unlock_context;
    bool m_prompting{false};
    bool m_wallet_unloaded{false};
    bool m_shutdown{false};
};
#endif // BITCOIN_QT_MININGSESSION_H
