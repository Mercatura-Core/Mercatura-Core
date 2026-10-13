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
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
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
    layout->setContentsMargins(0, 0, 0, 0);
    auto* group{new QGroupBox{tr("Mining hardware"), this}};
    auto* form{new QFormLayout{group}};
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto* description{new QLabel{tr("Mine with this wallet's native PQ reward address. Mining software fees, developer fees and automatic donations are permanently 0%."), group}};
    description->setWordWrap(true);
    form->addRow(description);
    m_hardware = new QComboBox{group};
    m_hardware->setObjectName(QStringLiteral("miningHardware"));
    m_hardware->addItems({tr("CPU"), tr("GPU (NVIDIA / AMD)"), tr("CPU + GPU")});
    form->addRow(tr("Hardware"), m_hardware);
    m_all_devices = new QCheckBox{tr("All compatible GPUs"), group};
    m_all_devices->setObjectName(QStringLiteral("automaticGpuDevices"));
    m_all_devices->setChecked(true);
    form->addRow(m_all_devices);
    m_devices = new QListWidget{group};
    m_devices->setObjectName(QStringLiteral("gpuDevices"));
    m_devices->setMaximumHeight(100);
    form->addRow(tr("GPU devices"), m_devices);
    m_contexts = new QSpinBox{group};
    m_contexts->setObjectName(QStringLiteral("gpuContexts"));
    m_contexts->setRange(0, 256);
    m_contexts->setSpecialValueText(tr("Automatic"));
    form->addRow(tr("Concurrent contexts per GPU"), m_contexts);
    m_yield = new QSpinBox{group};
    m_yield->setObjectName(QStringLiteral("gpuYield"));
    m_yield->setRange(0, 1000);
    m_yield->setValue(10);
    m_yield->setSuffix(QStringLiteral(" ") + tr("ms"));
    form->addRow(tr("Rest between GPU batches"), m_yield);
    m_refresh_devices = new QPushButton{tr("Refresh GPU devices"), group};
    m_refresh_devices->setObjectName(QStringLiteral("refreshGpuDevices"));
    form->addRow(m_refresh_devices);
    m_gpu_status = new QLabel{group}; m_gpu_status->setTextFormat(Qt::PlainText); m_gpu_status->setWordWrap(true);
    form->addRow(m_gpu_status);
    m_gpu_rates = new QLabel{group}; m_gpu_rates->setTextFormat(Qt::PlainText); m_gpu_rates->setWordWrap(true);
    form->addRow(tr("CPU / GPU statistics"), m_gpu_rates);

    QSettings settings;
    m_workers = new QSpinBox{group};
    m_workers->setObjectName(QStringLiteral("manualWorkers"));
    m_workers->setRange(1, static_cast<int>(std::max(1U, std::min(m_limits.maximum, static_cast<unsigned int>(std::numeric_limits<int>::max())))));
    m_workers->setValue(std::clamp(settings.value("mining/cpuWorkers", 1).toInt(), 1, m_workers->maximum()));
    form->addRow(tr("CPU workers"), m_workers);
    auto* hardware{new QLabel{tr("%1 logical processors detected; up to %2 CPU-only workers within the current CPU and memory budget. Combined mode also reserves host memory for GPU verification. Each worker needs a private 128 MiB scratchpad plus overhead. Logical processor count does not predict optimal hashing throughput.").arg(m_limits.logical_cpus).arg(m_limits.maximum), group}};
    hardware->setWordWrap(true);
    form->addRow(hardware);
    m_destination = new QLabel{tr("Created or reused when mining starts"), group};
    m_destination->setObjectName(QStringLiteral("miningDestination"));
    m_destination->setTextFormat(Qt::PlainText);
    m_destination->setWordWrap(true);
    m_destination->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_destination->setFont(GUIUtil::fixedPitchFont());
    m_destination->setToolTip(tr("This wallet-owned PQ address receives mining rewards. Show in address book opens receiving addresses. Newly created mining addresses use the label Mercatura mining reward."));
    auto* destination_row{new QWidget{group}};
    auto* destination_layout{new QVBoxLayout{destination_row}};
    destination_layout->setContentsMargins(0, 0, 0, 0);
    destination_layout->setSpacing(2);
    destination_layout->addWidget(m_destination);
    auto* address_actions{new QHBoxLayout};
    m_copy_destination = new QPushButton{tr("Copy address"), destination_row};
    m_copy_destination->setObjectName(QStringLiteral("copyMiningDestination"));
    m_show_destination = new QPushButton{tr("Show in address book"), destination_row};
    m_show_destination->setObjectName(QStringLiteral("showMiningDestination"));
    address_actions->addWidget(m_copy_destination);
    address_actions->addWidget(m_show_destination);
    address_actions->addStretch();
    destination_layout->addLayout(address_actions);
    form->addRow(tr("PQ reward destination"), destination_row);
    m_hashrate = new QLabel{group};
    m_hashrate->setObjectName(QStringLiteral("miningHashrate"));
    auto font{m_hashrate->font()};
    font.setBold(true);
    m_hashrate->setFont(font);
    m_hashrate->setToolTip(tr("Uses completed CPU and GPU hashes over the last 10 seconds, or elapsed session time while starting."));
    form->addRow(tr("10-second average"), m_hashrate);
    m_statistics = new QLabel{group};
    m_statistics->setObjectName(QStringLiteral("miningSessionStatistics"));
    m_statistics->setTextFormat(Qt::PlainText);
    form->addRow(tr("Session statistics"), m_statistics);
    m_runtime = new QLabel{group};
    m_runtime->setObjectName(QStringLiteral("miningSessionRuntime"));
    form->addRow(tr("Session Runtime"), m_runtime);
    m_active = new QLabel{group};
    m_active->setObjectName(QStringLiteral("miningActiveWorkers"));
    form->addRow(tr("Active workers"), m_active);
    m_blocks = new QLabel{group};
    m_blocks->setObjectName(QStringLiteral("miningBlocks"));
    form->addRow(tr("Solutions / submitted / accepted"), m_blocks);
    m_status = new QLabel{group};
    m_status->setObjectName(QStringLiteral("miningStatus"));
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    form->addRow(tr("Status"), m_status);
    layout->addWidget(group);
    for (auto* label : content->findChildren<QLabel*>()) {
        label->setWordWrap(true);
    }
    for (auto* label : {m_destination, m_statistics, m_status, m_gpu_status, m_gpu_rates}) {
        auto policy{label->sizePolicy()};
        policy.setHorizontalPolicy(QSizePolicy::Ignored);
        label->setSizePolicy(policy);
    }
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
    connect(m_workers, &QSpinBox::valueChanged, this, [](int count) {
        QSettings{}.setValue("mining/cpuWorkers", count);
    });
    connect(m_copy_destination, &QPushButton::clicked, this, [this] {
        if (m_copy_destination->isEnabled()) GUIUtil::setClipboard(m_destination->text());
    });
    connect(m_show_destination, &QPushButton::clicked, this, [this] {
        if (m_show_destination->isEnabled()) Q_EMIT showRewardAddress(m_destination->text());
    });
    connect(m_hardware, &QComboBox::currentIndexChanged, this, [this] { updateStatus(); });
    connect(m_all_devices, &QCheckBox::toggled, this, [this] { updateStatus(); });
    connect(m_devices, &QListWidget::itemChanged, this, [this] { updateStatus(); });
    connect(m_refresh_devices, &QPushButton::clicked, this, [this] {
        if (m_session) m_session->refreshDevices();
        updateStatus();
    });
    connect(m_start, &QPushButton::clicked, this, [this] {
        if (m_session) {
            const unsigned int workers{static_cast<unsigned int>(m_workers->value())};
            mining::GpuConfig hardware;
            hardware.cpu = m_hardware->currentIndex() != 1;
            hardware.gpu = m_hardware->currentIndex() != 0;
            hardware.contexts = static_cast<unsigned int>(m_contexts->value());
            hardware.yield_ms = static_cast<unsigned int>(m_yield->value());
            if (!m_all_devices->isChecked()) {
                for (int i{0}; i < m_devices->count(); ++i) {
                    const auto* item{m_devices->item(i)};
                    if (item->checkState() == Qt::Checked) hardware.devices.push_back(item->data(Qt::UserRole).toString().toStdString());
                }
                if (hardware.gpu && hardware.devices.empty()) return;
            }
            m_session->start(m_wallet, workers, hardware);
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
    m_devices_revision.reset();
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
    const bool use_cpu{m_hardware->currentIndex() != 1};
    const bool use_gpu{m_hardware->currentIndex() != 0};
    const auto discovery{m_session ? m_session->gpuDiscoverySnapshot() : std::optional<MiningSession::GpuDiscoverySnapshot>{}};
    if (discovery && m_devices_revision != discovery->revision) {
        const QSignalBlocker block{m_devices};
        m_devices->clear();
        for (const auto& device : discovery->result.devices) {
            auto* item{new QListWidgetItem{QString::fromStdString(device.name + " (" + device.capability + ")"), m_devices}};
            item->setData(Qt::UserRole, QString::fromStdString(device.id));
            item->setToolTip(QString::fromStdString(device.error));
            item->setFlags(device.supported ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::NoItemFlags);
            item->setCheckState(device.supported ? Qt::Checked : Qt::Unchecked);
        }
        m_devices_revision = discovery->revision;
    }
    size_t selected_gpus{0};
    for (int i{0}; i < m_devices->count(); ++i) if ((m_devices->item(i)->flags() & Qt::ItemIsEnabled) &&
        (m_all_devices->isChecked() || m_devices->item(i)->checkState() == Qt::Checked)) ++selected_gpus;
    const bool gpu_available{selected_gpus > 0};
    const auto maximum{mining::MaximumCpuWorkers(m_limits, use_gpu ? selected_gpus : 0)};
    const auto host_limits{mining::CalculateWorkerLimits(std::numeric_limits<unsigned int>::max(), m_limits.available_memory)};
    const bool gpu_host_memory{!use_gpu || selected_gpus <= host_limits.maximum};
    if (!running) {
        const QSignalBlocker block{m_workers}; // Resource clamping is not a new manual preference.
        m_workers->setMaximum(static_cast<int>(std::max(1U, std::min(maximum, static_cast<unsigned int>(std::numeric_limits<int>::max())))));
    }
    m_start->setEnabled(m_wallet && m_session && m_client && !running && !syncing && (!use_cpu || maximum > 0) && (!use_gpu || (discovery && gpu_available && gpu_host_memory)));
    m_stop->setEnabled(m_session && running && stats.state != mining::MiningState::STOPPING);
    m_hardware->setEnabled(!running);
    m_all_devices->setEnabled(!running && use_gpu);
    m_devices->setEnabled(!running && use_gpu && !m_all_devices->isChecked());
    m_contexts->setEnabled(!running && use_gpu);
    m_yield->setEnabled(!running && use_gpu);
    m_refresh_devices->setEnabled(m_session && !running && bool(discovery));
    for (QWidget* field : std::array<QWidget*, 6>{m_all_devices, m_devices, m_contexts, m_yield, m_refresh_devices, m_gpu_status}) {
        field->setVisible(use_gpu);
        if (auto* form{qobject_cast<QFormLayout*>(field->parentWidget()->layout())}) {
            if (auto* label{form->labelForField(field)}) label->setVisible(use_gpu);
        }
    }
    QStringList gpu_messages;
    if (!discovery) gpu_messages << tr("Detecting GPU devices…");
    else {
        for (const auto& error : discovery->result.errors) gpu_messages << QString::fromStdString(error);
        if (!gpu_available) gpu_messages << tr("Select an available GPU or choose CPU mining.");
        else if (!gpu_host_memory) gpu_messages << tr("Insufficient host memory for the selected GPU verifiers. Select fewer devices or free memory.");
    }
    m_gpu_status->setText(gpu_messages.join(QStringLiteral("\n")));
    QStringList device_rates{tr("CPU: %1 H/s (%2 hashes)").arg(stats.cpu_hashes_per_second, 0, 'f', 2).arg(qulonglong{stats.cpu_hashes})};
    for (const auto& device : stats.devices) device_rates << tr("%1: %2 H/s, %3 hashes, %4 MiB, %5 contexts — %6")
        .arg(QString::fromStdString(device.device.name)).arg(device.hashes_per_second, 0, 'f', 2).arg(qulonglong{device.hashes})
        .arg(qulonglong{device.memory_bytes >> 20}).arg(device.contexts).arg(QString::fromStdString(device.status));
    m_gpu_rates->setText(device_rates.join(QStringLiteral("\n")));
    m_workers->setEnabled(!running && use_cpu && maximum > 0);
    m_hashrate->setText(tr("%1 H/s").arg(stats.hashes_per_second_10s, 0, 'f', 2));
    m_statistics->setText(tr("60 s: %1 H/s · Session: %2 H/s · %3 hashes")
        .arg(stats.hashes_per_second_60s, 0, 'f', 2)
        .arg(stats.hashes_per_second_session, 0, 'f', 2).arg(qulonglong{stats.hashes}));
    m_runtime->setText(formatRuntime(std::chrono::duration_cast<std::chrono::seconds>(stats.session_elapsed).count()));
    m_active->setText(QString::number(stats.active_workers));
    m_blocks->setText(QStringLiteral("%1 / %2 / %3").arg(qulonglong{stats.solutions}).arg(qulonglong{stats.submitted}).arg(qulonglong{stats.accepted}));
    m_blocks->setToolTip(tr("Not processed: %1; stale or discarded: %2. Processing without observed active-chain acceptance is not counted as accepted.").arg(qulonglong{stats.rejected}).arg(qulonglong{stats.stale}));
    QString status{QString::fromStdString(stats.status)};
    if (!m_wallet || !m_session || !m_client) status = tr("Mining is unavailable until the node and wallet are loaded.");
    else if (!running && stats.state != mining::MiningState::FAILED) {
        if (use_gpu && !gpu_available) status = tr("No selected compatible GPU is available. Choose CPU mining or refresh devices.");
        else if (!gpu_host_memory || (use_cpu && maximum == 0)) status = tr("Insufficient available memory for the selected CPU workers and GPU verifiers.");
        else if (syncing) status = tr("Waiting for initial block download to complete.");
        else status = tr("Ready. Mining starts only when you press Start Mining.");
    }
    if (m_session && m_session->owner() && m_session->owner() != m_wallet && running) {
        status = tr("Mining for wallet %1. Stop this session before starting with another wallet.").arg(m_session->owner()->getDisplayName());
    }
    m_status->setText(status);
    m_destination->setText(m_privacy ? tr("(hidden)") : (stats.destination.empty() || (!running && m_session && m_session->owner() != m_wallet)) ? tr("Created or reused when mining starts") : QString::fromStdString(stats.destination));
    const bool own_destination{!m_privacy && m_wallet && m_session && m_session->owner() == m_wallet && !stats.destination.empty()};
    m_copy_destination->setEnabled(own_destination);
    m_show_destination->setEnabled(own_destination);
}

QString MiningPage::formatRuntime(uint64_t seconds)
{
    const auto time{QStringLiteral("%1:%2:%3")
        .arg(qulonglong{(seconds / 3600) % 24}, 2, 10, QLatin1Char{'0'})
        .arg(qulonglong{(seconds / 60) % 60}, 2, 10, QLatin1Char{'0'})
        .arg(qulonglong{seconds % 60}, 2, 10, QLatin1Char{'0'})};
    return seconds < 86400 ? time : tr("%1 d %2").arg(qulonglong{seconds / 86400}).arg(time);
}
