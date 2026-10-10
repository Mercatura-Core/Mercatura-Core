// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <qt/miningpage.h>

#include <chainparams.h>
#include <interfaces/node.h>
#include <qt/clientmodel.h>
#include <qt/guiutil.h>
#include <qt/miningsession.h>
#include <qt/walletmodel.h>
#include <util/chaintype.h>

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

MiningPage::MiningPage(WalletModel* wallet, QWidget* parent)
    : QWidget{parent}, m_wallet{wallet}, m_limits{mining::DetectWorkerLimits()}
{
    setObjectName(QStringLiteral("miningPage"));
    auto* layout{new QVBoxLayout{this}};
    auto* group{new QGroupBox{tr("Solo Mining"), this}};
    auto* form{new QFormLayout{group}};
    auto* description{new QLabel{tr("Mine with this wallet's native PQ reward address. Mining software fees, developer fees and automatic donations are permanently 0%."), group}};
    description->setWordWrap(true);
    form->addRow(description);
    m_automatic = new QCheckBox{tr("Automatic (recommended: %1 workers)").arg(m_limits.recommended), group};
    m_automatic->setObjectName(QStringLiteral("automaticWorkers"));
    m_automatic->setChecked(true);
    form->addRow(tr("CPU workers"), m_automatic);
    m_workers = new QSpinBox{group};
    m_workers->setObjectName(QStringLiteral("manualWorkers"));
    m_workers->setRange(1, static_cast<int>(std::max(1U, std::min(m_limits.maximum, static_cast<unsigned int>(std::numeric_limits<int>::max())))));
    m_workers->setValue(std::max(1U, m_limits.recommended));
    m_workers->setEnabled(false);
    form->addRow(tr("Manual workers"), m_workers);
    auto* hardware{new QLabel{tr("%1 logical processors detected; up to %2 workers within the current CPU and memory budget. Each worker needs a private 128 MiB scratchpad plus overhead. Logical processor count does not predict optimal hashing throughput.").arg(m_limits.logical_cpus).arg(m_limits.maximum), group}};
    hardware->setWordWrap(true);
    form->addRow(hardware);
    m_destination = new QLabel{tr("Created or reused when mining starts"), group};
    m_destination->setObjectName(QStringLiteral("miningDestination"));
    m_destination->setTextFormat(Qt::PlainText);
    m_destination->setWordWrap(true);
    m_destination->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_destination->setFont(GUIUtil::fixedPitchFont());
    form->addRow(tr("PQ reward destination"), m_destination);
    m_hashrate = new QLabel{group};
    form->addRow(tr("Local hashrate"), m_hashrate);
    m_active = new QLabel{group};
    form->addRow(tr("Active workers"), m_active);
    m_blocks = new QLabel{group};
    form->addRow(tr("Solutions / submitted / accepted"), m_blocks);
    m_status = new QLabel{group};
    m_status->setObjectName(QStringLiteral("miningStatus"));
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    form->addRow(tr("Status"), m_status);
    layout->addWidget(group);
    auto* buttons{new QHBoxLayout};
    m_start = new QPushButton{tr("Start Mining"), this};
    m_start->setObjectName(QStringLiteral("startMining"));
    m_stop = new QPushButton{tr("Stop Mining"), this};
    m_stop->setObjectName(QStringLiteral("stopMining"));
    buttons->addWidget(m_start);
    buttons->addWidget(m_stop);
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addStretch();
    connect(m_automatic, &QCheckBox::toggled, this, [this] { updateStatus(); });
    connect(m_start, &QPushButton::clicked, this, [this] {
        if (m_session) m_session->start(m_wallet, m_automatic->isChecked() ? 0 : static_cast<unsigned int>(m_workers->value()));
        updateStatus();
    });
    connect(m_stop, &QPushButton::clicked, this, [this] {
        if (m_session) m_session->stop();
        updateStatus();
    });
    auto* timer{new QTimer{this}};
    connect(timer, &QTimer::timeout, this, &MiningPage::updateStatus);
    timer->start(1000);
    updateStatus();
}
void MiningPage::setSession(MiningSession* session)
{
    if (m_session) disconnect(m_session, nullptr, this, nullptr);
    m_session = session;
    if (session) connect(session, &MiningSession::changed, this, &MiningPage::updateStatus);
    updateStatus();
}
void MiningPage::setClientModel(ClientModel* client) { m_client = client; updateStatus(); }
void MiningPage::setPrivacy(bool privacy) { m_privacy = privacy; updateStatus(); }
void MiningPage::updateStatus()
{
    const mining::MiningStats stats{m_session ? m_session->stats() : mining::MiningStats{}};
    const bool running{stats.busy};
    const bool syncing{m_client && Params().GetChainType() != ChainType::REGTEST && m_client->node().isInitialBlockDownload()};
    m_start->setEnabled(m_session && m_client && !running && !syncing && m_limits.maximum > 0);
    m_stop->setEnabled(m_session && running && stats.state != mining::MiningState::STOPPING);
    m_automatic->setEnabled(!running);
    m_workers->setEnabled(!running && !m_automatic->isChecked() && m_limits.maximum > 0);
    m_hashrate->setText(tr("%1 H/s (%2 hashes)").arg(stats.hashes_per_second, 0, 'f', 2).arg(qulonglong{stats.hashes}));
    m_active->setText(QString::number(stats.active_workers));
    m_blocks->setText(QStringLiteral("%1 / %2 / %3").arg(qulonglong{stats.solutions}).arg(qulonglong{stats.submitted}).arg(qulonglong{stats.accepted}));
    QString status{QString::fromStdString(stats.status)};
    if (!m_session || !m_client) status = tr("Mining is unavailable until the node and wallet are loaded.");
    else if (!running && stats.state != mining::MiningState::FAILED) {
        if (m_limits.maximum == 0) status = tr("Insufficient available memory for a mining worker.");
        else if (syncing) status = tr("Waiting for initial block download to complete.");
        else status = tr("Ready. Mining starts only when you press Start Mining.");
    }
    if (m_session && m_session->owner() && m_session->owner() != m_wallet && running) {
        status = tr("Mining for wallet %1. Stop this session before starting with another wallet.").arg(m_session->owner()->getDisplayName());
    }
    m_status->setText(status);
    m_destination->setText(m_privacy ? tr("(hidden)") : (stats.destination.empty() || (!running && m_session && m_session->owner() != m_wallet)) ? tr("Created or reused when mining starts") : QString::fromStdString(stats.destination));
}
