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
class QLineEdit;
class QGroupBox;

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
    QComboBox* m_mode;
    QGroupBox* m_pool;
    QLineEdit* m_host;
    QSpinBox* m_port;
    QLabel* m_certificate;
    QLabel* m_connection;
    QLabel* m_shares;
    QLabel* m_last_share;
    QLabel* m_reported;
    QLabel* m_commitment;
    QPushButton* m_ca;
    QPushButton* m_system_ca;
    QPushButton* m_inspect;
    QPushButton* m_reset;
    QString m_ca_file;
    bool m_privacy{false};
};
#endif // BITCOIN_QT_MININGPAGE_H
