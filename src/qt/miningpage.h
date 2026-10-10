// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#ifndef BITCOIN_QT_MININGPAGE_H
#define BITCOIN_QT_MININGPAGE_H

#include <mining/cpu_miner.h>
#include <QPointer>
#include <QWidget>

class ClientModel;
class MiningSession;
class WalletModel;
class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;

class MiningPage : public QWidget {
    Q_OBJECT
public:
    explicit MiningPage(WalletModel* wallet, QWidget* parent = nullptr);
    void setSession(MiningSession* session);
    void setClientModel(ClientModel* client);
    void setPrivacy(bool privacy);

private:
    void updateStatus();
    WalletModel* const m_wallet;
    QPointer<MiningSession> m_session;
    ClientModel* m_client{nullptr};
    mining::WorkerLimits m_limits;
    QCheckBox* m_automatic;
    QSpinBox* m_workers;
    QPushButton* m_start;
    QPushButton* m_stop;
    QLabel* m_status;
    QLabel* m_hashrate;
    QLabel* m_active;
    QLabel* m_blocks;
    QLabel* m_destination;
    bool m_privacy{false};
};
#endif // BITCOIN_QT_MININGPAGE_H
