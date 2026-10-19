// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#ifndef BITCOIN_QT_MININGPAGE_H
#define BITCOIN_QT_MININGPAGE_H

#ifndef Q_MOC_RUN
#include <mining/cpu_miner.h>
#endif
#include <QPointer>
#include <QWidget>

class ClientModel;
class MiningSession;
class WalletModel;
class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QComboBox;
class QGroupBox;
class QListWidget;

class MiningPage : public QWidget {
    Q_OBJECT
public:
    explicit MiningPage(WalletModel* wallet, QWidget* parent = nullptr, mining::MiningController::LimitDetector limits = {});
    void setSession(MiningSession* session);
    void setClientModel(ClientModel* client);
    void setPrivacy(bool privacy);
    static QString formatRuntime(uint64_t seconds);
    static QString formatGpuContexts(const mining::GpuStats& device);

Q_SIGNALS:
    void showRewardAddress(const QString& address);

private:
    void updateStatus();
    WalletModel* const m_wallet;
    QPointer<MiningSession> m_session;
    ClientModel* m_client{nullptr};
    const mining::MiningController::LimitDetector m_detect_limits;
    mining::WorkerLimits m_limits;
    QLabel* m_hardware_limits;
    QSpinBox* m_workers;
    QPushButton* m_start;
    QPushButton* m_stop;
    QLabel* m_status;
    QLabel* m_hashrate;
    QLabel* m_statistics;
    QLabel* m_runtime;
    QPushButton* m_copy_destination;
    QPushButton* m_show_destination;
    QLabel* m_active;
    QLabel* m_blocks;
    QLabel* m_destination;
    QComboBox* m_hardware;
    QCheckBox* m_all_devices;
    QListWidget* m_devices;
    QComboBox* m_context_mode;
    QSpinBox* m_contexts;
    QSpinBox* m_yield;
    QPushButton* m_refresh_devices;
    QLabel* m_gpu_status;
    QLabel* m_gpu_rates;
    std::optional<uint64_t> m_devices_revision;
    bool m_privacy{false};
};
#endif // BITCOIN_QT_MININGPAGE_H
