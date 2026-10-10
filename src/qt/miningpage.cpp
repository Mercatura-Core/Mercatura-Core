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
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

MiningPage::MiningPage(WalletModel* wallet, QWidget* parent)
    : QWidget{parent}, m_wallet{wallet}, m_limits{mining::DetectWorkerLimits()}
{
    setObjectName(QStringLiteral("miningPage"));
    auto* outer{new QVBoxLayout{this}};
    auto* scroll{new QScrollArea{this}};
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content{new QWidget{scroll}};
    scroll->setWidget(content);
    outer->addWidget(scroll);
    auto* layout{new QVBoxLayout{content}};
    m_mode = new QComboBox{this};
    m_mode->setObjectName(QStringLiteral("miningMode"));
    m_mode->setAccessibleName(tr("Mining mode"));
    m_mode->addItems({tr("Solo Mining"), tr("Pool Mining")});
    layout->addWidget(m_mode);
    auto* group{new QGroupBox{tr("CPU Mining"), this}};
    auto* form{new QFormLayout{group}};
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
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
    m_pool = new QGroupBox{tr("Pool configuration and shares"), this};
    auto* pool_form{new QFormLayout{m_pool}};
    pool_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    QSettings settings;
    m_host = new QLineEdit{settings.value("mining/poolHost").toString(), m_pool};
    m_host->setMaxLength(253);
    m_host->setObjectName(QStringLiteral("poolHost"));
    m_host->setPlaceholderText(tr("Pool hostname"));
    pool_form->addRow(tr("Server"), m_host);
    m_port = new QSpinBox{m_pool};
    m_port->setRange(1, 65535);
    m_port->setValue(settings.value("mining/poolPort", 3333).toInt());
    m_port->setObjectName(QStringLiteral("poolPort"));
    pool_form->addRow(tr("Port"), m_port);
    m_ca_file = settings.value("mining/poolCA").toString();
    m_certificate = new QLabel{m_pool};
    m_certificate->setTextFormat(Qt::PlainText);
    m_certificate->setWordWrap(true);
    pool_form->addRow(tr("TLS"), m_certificate);
    m_ca = new QPushButton{tr("Select trusted CA certificate…"), m_pool};
    pool_form->addRow(m_ca);
    m_system_ca = new QPushButton{tr("Use system trust store only"), m_pool};
    m_system_ca->setObjectName(QStringLiteral("systemPoolCA"));
    pool_form->addRow(m_system_ca);
    m_connection = new QLabel{m_pool};
    m_connection->setObjectName(QStringLiteral("poolStatus"));
    m_connection->setTextFormat(Qt::PlainText);
    m_connection->setWordWrap(true);
    pool_form->addRow(tr("Connection"), m_connection);
    m_shares = new QLabel{m_pool};
    pool_form->addRow(tr("Accepted / rejected / stale shares (pool replies since Start)"), m_shares);
    m_last_share = new QLabel{m_pool};
    pool_form->addRow(tr("Last accepted share (local receipt time)"), m_last_share);
    m_reported = new QLabel{m_pool};
    m_reported->setTextFormat(Qt::PlainText);
    m_reported->setWordWrap(true);
    pool_form->addRow(tr("Reported by pool"), m_reported);
    m_commitment = new QLabel{m_pool};
    m_commitment->setTextFormat(Qt::PlainText);
    m_commitment->setWordWrap(true);
    m_commitment->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pool_form->addRow(tr("Checked payout snapshot"), m_commitment);
    m_inspect = new QPushButton{tr("Inspect payout manifest…"), m_pool};
    m_inspect->setObjectName(QStringLiteral("inspectPoolManifest"));
    pool_form->addRow(m_inspect);
    m_reset = new QPushButton{tr("Forget disconnected pool session"), m_pool};
    m_reset->setToolTip(tr("Use after the pool loses its session database. The next Start registers a new namespace; previous shares keep their payout address."));
    pool_form->addRow(m_reset);
    auto* trust{new QLabel{tr("Mining software and coordinator fees: permanently 0%. Coinbase payouts and commitments are checked against the supplied candidate. Local Core proposal validation is used when its synchronized tip matches; otherwise chain history comes from the coordinator. PPLNS history is supplied by the coordinator. Shares do not guarantee rewards."), m_pool}};
    trust->setWordWrap(true);
    pool_form->addRow(trust);
    layout->addWidget(m_pool);
    auto* buttons{new QHBoxLayout};
    m_start = new QPushButton{tr("Start Mining"), this};
    m_start->setObjectName(QStringLiteral("startMining"));
    m_stop = new QPushButton{tr("Stop Mining"), this};
    m_stop->setObjectName(QStringLiteral("stopMining"));
    buttons->addWidget(m_start);
    buttons->addWidget(m_stop);
    buttons->addStretch();
    outer->addLayout(buttons);
    layout->addStretch();
    connect(m_automatic, &QCheckBox::toggled, this, [this] { updateStatus(); });
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] { updateStatus(); });
    connect(m_host, &QLineEdit::textChanged, this, [this] { updateStatus(); });
    connect(m_ca, &QPushButton::clicked, this, [this] {
        const auto file{QFileDialog::getOpenFileName(this, tr("Select a trusted pool CA certificate"), {}, tr("PEM certificates (*.pem *.crt)"))};
        if (!file.isEmpty()) m_ca_file = file;
        updateStatus();
    });
    connect(m_system_ca, &QPushButton::clicked, this, [this] { m_ca_file.clear(); updateStatus(); });
    connect(m_reset, &QPushButton::clicked, this, [this] { if (m_session) m_session->forgetPoolSession(); updateStatus(); });
    connect(m_inspect, &QPushButton::clicked, this, [this] {
        if (!m_session) return;
        const auto stats{m_session->poolStats()};
        if (!stats.details) return;
        QDialog dialog{this};
        dialog.setWindowTitle(tr("Pool payout manifest — coordinator history"));
        auto* contents{new QVBoxLayout{&dialog}};
        auto* text{new QPlainTextEdit{&dialog}};
        text->setReadOnly(true);
        text->setPlainText(QString::fromStdString(*stats.details));
        contents->addWidget(text);
        auto* close{new QDialogButtonBox{QDialogButtonBox::Close, &dialog}};
        connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        contents->addWidget(close);
        dialog.resize(720, 480);
        dialog.exec();
    });
    connect(m_start, &QPushButton::clicked, this, [this] {
        if (m_session) {
            const unsigned int workers{m_automatic->isChecked() ? 0 : static_cast<unsigned int>(m_workers->value())};
            if (m_mode->currentIndex() == 1) {
                QSettings settings;
                settings.setValue("mining/poolHost", m_host->text());
                settings.setValue("mining/poolPort", m_port->value());
                settings.setValue("mining/poolCA", m_ca_file);
                m_session->startPool(m_wallet, workers, {m_host->text().toStdString(), static_cast<uint16_t>(m_port->value()), m_ca_file.toStdString()});
            } else
                m_session->start(m_wallet, workers);
        }
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
    const bool pool{m_mode->currentIndex() == 1};
    const bool syncing{!pool && m_client && Params().GetChainType() != ChainType::REGTEST && m_client->node().isInitialBlockDownload()};
    const bool endpoint{mining::ValidPoolEndpoint({m_host->text().toStdString(), static_cast<uint16_t>(m_port->value()), m_ca_file.toStdString()})};
    m_start->setEnabled(m_session && m_client && !running && !syncing && m_limits.maximum > 0 && (!pool || endpoint));
    m_stop->setEnabled(m_session && running && stats.state != mining::MiningState::STOPPING);
    m_automatic->setEnabled(!running);
    m_workers->setEnabled(!running && !m_automatic->isChecked() && m_limits.maximum > 0);
    m_mode->setEnabled(!running);
    m_pool->setVisible(pool);
    m_host->setEnabled(!running);
    m_port->setEnabled(!running);
    m_ca->setEnabled(!running);
    m_system_ca->setEnabled(!running && !m_ca_file.isEmpty());
    m_reset->setEnabled(m_session && !running);
    m_certificate->setText(m_ca_file.isEmpty() ? tr("System trust store; certificate and hostname verification required") : tr("System trust store plus selected CA: %1. Hostname verification required.").arg(m_ca_file));
    const auto pool_stats{m_session ? m_session->poolStats() : mining::PoolStats{}};
    m_connection->setText(QString::fromStdString(pool_stats.status) + (pool_stats.connected ? (pool_stats.locally_verified ? tr("\nCandidate passed local Core proposal validation.") : tr("\nCandidate chain validity has not been verified by this local node.")) : QString{}));
    m_shares->setText(QStringLiteral("%1 / %2 / %3").arg(qulonglong{pool_stats.accepted}).arg(qulonglong{pool_stats.rejected}).arg(qulonglong{pool_stats.stale}));
    m_last_share->setText(pool_stats.last_accepted ? QDateTime::fromSecsSinceEpoch(pool_stats.last_accepted).toLocalTime().toString(Qt::ISODate) : tr("None"));
    m_reported->setText(QString::fromStdString(pool_stats.reported));
    m_commitment->setText(m_privacy ? tr("(hidden)") : QString::fromStdString(pool_stats.commitment));
    m_inspect->setEnabled(m_session && bool(pool_stats.manifest) && !m_privacy);
    m_hashrate->setText(tr("%1 H/s (%2 hashes)").arg(stats.hashes_per_second, 0, 'f', 2).arg(qulonglong{stats.hashes}));
    m_active->setText(QString::number(stats.active_workers));
    m_blocks->setText(pool ? tr("%1 target hits / %2 submission attempts (local)").arg(qulonglong{stats.solutions}).arg(qulonglong{stats.submitted}) : QStringLiteral("%1 / %2 / %3").arg(qulonglong{stats.solutions}).arg(qulonglong{stats.submitted}).arg(qulonglong{stats.accepted}));
    QString status{QString::fromStdString(stats.status)};
    if (!m_session || !m_client) status = tr("Mining is unavailable until the node and wallet are loaded.");
    else if (!running && stats.state != mining::MiningState::FAILED) {
        if (m_limits.maximum == 0) status = tr("Insufficient available memory for a mining worker.");
        else if (syncing) status = tr("Waiting for initial block download to complete.");
        else if (pool && !endpoint)
            status = tr("Enter a pool hostname and port. TLS is required.");
        else status = tr("Ready. Mining starts only when you press Start Mining.");
    }
    if (m_session && m_session->owner() && m_session->owner() != m_wallet && running) {
        status = tr("Mining for wallet %1. Stop this session before starting with another wallet.").arg(m_session->owner()->getDisplayName());
    }
    m_status->setText(status);
    m_destination->setText(m_privacy ? tr("(hidden)") : (stats.destination.empty() || (!running && m_session && m_session->owner() != m_wallet)) ? tr("Created or reused when mining starts") : QString::fromStdString(stats.destination));
}
