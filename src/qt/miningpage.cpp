// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <qt/miningpage.h>
#include <qt/notificationtiming.h>

#include <chainparams.h>
#include <pow.h>
#include <key_io.h>
#include <interfaces/wallet.h>
#include <wallet/mining.h>
#include <QInputDialog>
#include <QMessageBox>
#include <interfaces/node.h>
#include <mining/mercahash_cpu_batch.h>
#include <qt/clientmodel.h>
#include <qt/guiutil.h>
#include <qt/miningsession.h>
#include <qt/walletmodel.h>
#include <util/chaintype.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
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
#include <set>

MiningPage::MiningPage(WalletModel* wallet, QWidget* parent, mining::MiningController::LimitDetector limits)
    : QWidget{parent}, m_wallet{wallet}, m_detect_limits{limits ? std::move(limits) : mining::MiningController::LimitDetector{mining::DetectWorkerLimits}}, m_limits{m_detect_limits()}
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
    m_context_mode = new QComboBox{group};
    m_context_mode->setObjectName(QStringLiteral("gpuContextMode"));
    m_context_mode->addItems({tr("Automatic (maximum safe capacity)"), tr("Manual (reduce VRAM use)")});
    m_context_mode->setToolTip(tr("Automatic selects each GPU's maximum safe context count independently. CUDA uses measured free VRAM with a safety reserve and overhead allowance. AMD OpenCL uses conservative capacity limits because free VRAM is unknown. Manual reduces contexts to conserve VRAM or optimize efficiency. More contexts may reduce desktop responsiveness."));
    form->addRow(tr("GPU memory mode"), m_context_mode);
    m_contexts = new QSpinBox{group};
    m_contexts->setObjectName(QStringLiteral("gpuContexts"));
    m_contexts->setRange(1, 1);
    m_contexts->setToolTip(tr("Manual count applied to each selected GPU, bounded by the smallest selected device. Automatic ignores this field and sizes each GPU independently."));
    form->addRow(tr("Manual contexts per GPU"), m_contexts);
    // Integral doubles represent the full unsigned backend count exactly.
    // Keep the original V1 control and its existing range/behavior intact.
    m_contexts_v2 = new QDoubleSpinBox{group};
    m_contexts_v2->setObjectName(QStringLiteral("gpuContextsV2"));
    m_contexts_v2->setDecimals(0);
    m_contexts_v2->setRange(1, 1);
    m_contexts_v2->setToolTip(tr("Manual V2 contexts per selected GPU, up to the smallest selected resource budget. Automatic uses each device's full safe capacity independently. Capacity is not an optimal hashrate recommendation."));
    form->addRow(tr("Manual contexts per GPU"), m_contexts_v2);
    m_yield = new QSpinBox{group};
    m_yield->setObjectName(QStringLiteral("gpuYield"));
    m_yield->setRange(0, 1000);
    m_yield->setValue(0);
    m_yield->setSuffix(QStringLiteral(" ") + tr("ms"));
    form->addRow(tr("Rest between GPU batches"), m_yield);
    m_refresh_devices = new QPushButton{tr("Refresh GPU devices"), group};
    m_refresh_devices->setObjectName(QStringLiteral("refreshGpuDevices"));
    form->addRow(m_refresh_devices);
    m_gpu_status = new QLabel{group}; m_gpu_status->setTextFormat(Qt::PlainText); m_gpu_status->setWordWrap(true);
    m_gpu_status->setObjectName(QStringLiteral("gpuMemoryStatus"));
    form->addRow(m_gpu_status);
    m_gpu_rates = new QLabel{group}; m_gpu_rates->setTextFormat(Qt::PlainText); m_gpu_rates->setWordWrap(true);


    QSettings settings;
    m_workers = new QSpinBox{group};
    m_workers->setObjectName(QStringLiteral("manualWorkers"));
    m_workers->setRange(1, static_cast<int>(std::max(1U, std::min(m_limits.maximum, static_cast<unsigned int>(std::numeric_limits<int>::max())))));
    bool valid_setting{false};
    const auto saved{settings.value("mining/cpuWorkers", 1).toLongLong(&valid_setting)};
    m_workers->setValue(static_cast<int>(std::clamp<qlonglong>(valid_setting ? saved : 1, 1, m_workers->maximum())));
    form->addRow(tr("CPU workers"), m_workers);
    m_hardware_limits = new QLabel{group};
    m_hardware_limits->setWordWrap(true);
    form->addRow(m_hardware_limits);
    const QString reward_key{QStringLiteral("mining/wallet/") + QString::fromStdString(Params().GetConsensus().hashGenesisBlock.ToString()) + "/" + QString::fromStdString((m_wallet ? m_wallet->wallet().getWalletName() : std::string{})) + "/"};
    m_payout = new QComboBox{group};
    m_payout->setObjectName(QStringLiteral("miningPayoutType"));
    m_payout->addItems({tr("Automatic"), tr("Standard PQ v2"), tr("MercaWeave v3")});
    m_payout->setCurrentIndex(std::clamp(settings.value(reward_key + "payout", 0).toInt(), 0, 2));
    form->addRow(tr("Payout type"), m_payout);
    connect(m_payout, &QComboBox::currentIndexChanged, this, [this, reward_key](int index) {
        QSettings{}.setValue(reward_key + "payout", index);
        updateStatus();
    });
    m_destination = new QLabel{tr("Created or reused when mining starts"), group};
    m_destination->setObjectName(QStringLiteral("miningDestination"));
    m_destination->setTextFormat(Qt::PlainText);
    m_destination->setWordWrap(true);
    m_destination->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_destination->setFont(GUIUtil::fixedPitchFont());
    m_destination->setToolTip(tr("This wallet-owned address receives mining rewards. New addresses use the label Mercatura mining reward. A new v3 address can share a group key; address rotation does not rotate that key."));
    auto* destination_row{new QWidget{group}};
    auto* destination_layout{new QVBoxLayout{destination_row}};
    destination_layout->setContentsMargins(0, 0, 0, 0);
    destination_layout->setSpacing(2);
    destination_layout->addWidget(m_destination);
    auto* address_actions{new QHBoxLayout};
    m_copy_destination = new QPushButton{tr("Copy address"), destination_row};
    m_copy_destination->setObjectName(QStringLiteral("copyMiningDestination"));
    m_show_destination = new QPushButton{tr("Select Address"), destination_row};
    m_show_destination->setObjectName(QStringLiteral("showMiningDestination"));
    m_new_destination = new QPushButton{tr("New Address"), destination_row};
    address_actions->addWidget(m_new_destination);
    address_actions->addWidget(m_copy_destination);
    address_actions->addWidget(m_show_destination);
    address_actions->addStretch();
    destination_layout->addLayout(address_actions);
    form->addRow(tr("PQ reward destination"), destination_row);
    form->addRow(tr("CPU / GPU hashrate and power"), m_gpu_rates);
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
    form->addRow(tr("Session average"), m_statistics);
    m_runtime = new QLabel{group};
    m_runtime->setObjectName(QStringLiteral("miningSessionRuntime"));
    form->addRow(tr("Session Runtime"), m_runtime);
    m_active = new QLabel{group};
    m_active->setObjectName(QStringLiteral("miningActiveWorkers"));
    form->addRow(tr("Active workers"), m_active);
    m_blocks = new QLabel{group};
    m_blocks->setObjectName(QStringLiteral("miningBlocks"));
    form->addRow(tr("Accepted blocks"), m_blocks);
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
    connect(m_show_destination, &QPushButton::clicked, this, [this, reward_key] {
        QStringList addresses;
        for (const auto& entry : m_wallet->wallet().getAddresses()) {
            const auto encoded{EncodeDestination(entry.dest)};
            if (wallet::GetMiningDestination(m_wallet->wallet(), m_payout->currentIndex(), encoded)) addresses << QString::fromStdString(encoded);
        }
        bool ok{false};
        const auto selected{QInputDialog::getItem(this, tr("Mining reward address"), tr("Wallet address"), addresses, 0, false, &ok)};
        if (ok && !selected.isEmpty()) QSettings{}.setValue(reward_key + "address", selected);
        updateStatus();
    });
    connect(m_new_destination, &QPushButton::clicked, this, [this, reward_key] {
        auto unlock{m_wallet->requestUnlock()};
        if (!unlock.isValid()) return;
        auto dest{wallet::GetMiningDestination(m_wallet->wallet(), m_payout->currentIndex(), {}, true)};
        if (!dest) QMessageBox::warning(this, tr("Mining reward address"), QString::fromStdString(util::ErrorString(dest).original));
        else QSettings{}.setValue(reward_key + "address", QString::fromStdString(EncodeDestination(*dest)));
        updateStatus();
    });
    connect(m_hardware, &QComboBox::currentIndexChanged, this, [this] { updateStatus(); });
    connect(m_all_devices, &QCheckBox::toggled, this, [this] { updateStatus(); });
    connect(m_devices, &QListWidget::itemChanged, this, [this] { updateStatus(); });
    connect(m_context_mode, &QComboBox::currentIndexChanged, this, [this] { updateStatus(); });
    connect(m_contexts, &QSpinBox::valueChanged, this, [this] { updateStatus(); });
    connect(m_contexts_v2, &QDoubleSpinBox::valueChanged, this, [this] { updateStatus(); });
    connect(m_refresh_devices, &QPushButton::clicked, this, [this] {
        if (m_session) m_session->refreshDevices();
        updateStatus();
    });
    connect(m_start, &QPushButton::clicked, this, [this] {
        updateStatus(); // Recheck and clamp current resources before reading the selection.
        if (!m_start->isEnabled()) return;
        if (m_session) {
            const unsigned int workers{static_cast<unsigned int>(m_workers->value())};
            mining::GpuConfig hardware;
            hardware.cpu = m_hardware->currentIndex() != 1;
            hardware.gpu = m_hardware->currentIndex() != 0;
            hardware.contexts = 0;
            hardware.yield_ms = 0;
            {
                for (int i{0}; i < m_devices->count(); ++i) {
                    const auto* item{m_devices->item(i)};
                    if ((item->flags() & Qt::ItemIsEnabled) && (m_all_devices->isChecked() || item->checkState() == Qt::Checked)) hardware.devices.push_back(item->data(Qt::UserRole).toString().toStdString());
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
    const GUIUtil::NotificationTiming timing{"mining_page_status"};
    const mining::MiningStats stats{m_session ? m_session->stats() : mining::MiningStats{}};
    const bool running{stats.busy};
    // UI preview only. The provider resolves the actual template's parent,
    // and the controller rechecks the version's budget before starting workers.
    const auto version{running ? stats.pow_version : GetPoWVersion(Params().GetConsensus(),
        m_client ? std::max<int64_t>(0, int64_t{m_client->getCachedNumBlocks()} + 1) : 0)};
    m_v2_contexts = version == mercahash::Version::V2;
    if (!running) {
        m_limits = m_detect_limits();
        if (version == mercahash::Version::V2) m_limits = mining::CalculateWorkerLimits(m_limits.logical_cpus, m_limits.available_memory, version);
    }
    const auto gpu_budget = [version](const mining::GpuDevice& device, unsigned int requested = 0) {
        auto selected{device};
        selected.pow_version = version;
        return mining::CalculateGpuBudget(selected, requested);
    };
    m_hardware_limits->hide();
    m_hardware_limits->setText(tr("%1 logical processors detected; up to %2 CPU-only workers within the current CPU and memory budget. Combined mode also reserves host memory for GPU verification. Each worker needs %3 MiB of private scratchpads plus overhead. Logical processor count does not predict optimal hashing throughput.").arg(m_limits.logical_cpus).arg(m_limits.maximum).arg(qulonglong{mining::CpuScratchpadBytes(version) >> 20}));
    const bool syncing{m_client && Params().GetChainType() != ChainType::REGTEST && m_client->node().isInitialBlockDownload()};
    const bool use_cpu{m_hardware->currentIndex() != 1};
    const bool use_gpu{m_hardware->currentIndex() != 0};
    const auto discovery{m_session ? m_session->gpuDiscoverySnapshot() : std::optional<MiningSession::GpuDiscoverySnapshot>{}};
    if (discovery && m_devices_revision != discovery->revision) {
        const QSignalBlocker block{m_devices};
        std::set<std::string> checked_ids;
        for (int i{0}; i < m_devices->count(); ++i) if (m_devices->item(i)->checkState() == Qt::Checked)
            checked_ids.insert(m_devices->item(i)->data(Qt::UserRole).toString().toStdString());
        m_devices->clear();
        for (const auto& device : discovery->result.devices) {
            if (!device.dedicated) continue;
            auto* item{new QListWidgetItem{QString::fromStdString(device.name + " (" + device.capability + ")"), m_devices}};
            item->setData(Qt::UserRole, QString::fromStdString(device.id));
            item->setToolTip(QString::fromStdString(device.error));
            item->setFlags(device.supported ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::NoItemFlags);
            item->setCheckState(device.supported && (m_all_devices->isChecked() || checked_ids.contains(device.id)) ? Qt::Checked : Qt::Unchecked);
        }
        m_devices_revision = discovery->revision;
    }
    std::vector<const mining::GpuDevice*> selected_devices;
    if (discovery) for (int i{0}; i < m_devices->count(); ++i) {
        const auto* item{m_devices->item(i)};
        if (!(item->flags() & Qt::ItemIsEnabled) || (!m_all_devices->isChecked() && item->checkState() != Qt::Checked)) continue;
        const auto id{item->data(Qt::UserRole).toString().toStdString()};
        const auto found{std::find_if(discovery->result.devices.begin(), discovery->result.devices.end(),
                                     [&](const auto& device) { return device.id == id && device.supported; })};
        if (found != discovery->result.devices.end()) selected_devices.push_back(&*found);
    }
    const size_t selected_gpus{selected_devices.size()};
    const bool gpu_available{selected_gpus > 0};
    unsigned int context_maximum{gpu_available ? (m_v2_contexts ? std::numeric_limits<unsigned int>::max() : mining::MAX_GPU_CONTEXTS) : 0};
    bool automatic_memory{false};
    for (const auto* device : selected_devices) {
        const auto budget{gpu_budget(*device)};
        context_maximum = std::min(context_maximum, budget.maximum_contexts); // Manual field only.
        automatic_memory |= budget.contexts > 0;
    }
    const bool manual_contexts{m_context_mode->currentIndex() == 1};
    if (!running && discovery) {
        const QSignalBlocker block{m_contexts}, block_v2{m_contexts_v2};
        if (m_v2_contexts) m_contexts_v2->setMaximum(std::max(1U, context_maximum));
        else m_contexts->setMaximum(static_cast<int>(std::max(1U, context_maximum)));
    }
    const auto manual_count{static_cast<unsigned int>(m_v2_contexts ? m_contexts_v2->value() : m_contexts->value())};
    const bool gpu_context_memory{manual_contexts
        ? context_maximum > 0 && manual_count <= context_maximum : automatic_memory};
    const auto maximum{mining::MaximumCpuWorkers(m_limits, use_gpu ? selected_gpus : 0, version)};
    const bool gpu_host_memory{!use_gpu || selected_gpus <= mining::MaximumGpuVerifiers(m_limits, version)};
    if (!running) {
        const QSignalBlocker block{m_workers}; // Resource clamping is not a new manual preference.
        m_workers->setMaximum(static_cast<int>(std::max(1U, std::min(maximum, static_cast<unsigned int>(std::numeric_limits<int>::max())))));
    }
    m_start->setEnabled(m_wallet && m_session && m_client && !running && !syncing && (!use_cpu || maximum > 0) && (!use_gpu || (discovery && gpu_available && gpu_host_memory && gpu_context_memory)));
    m_stop->setEnabled(m_session && running && stats.state != mining::MiningState::STOPPING);
    m_hardware->setEnabled(!running);
    m_all_devices->setEnabled(!running && use_gpu);
    m_devices->setEnabled(!running && use_gpu && !m_all_devices->isChecked());
    m_context_mode->setEnabled(!running && use_gpu);
    m_contexts->setEnabled(!running && use_gpu && manual_contexts && context_maximum > 0);
    m_contexts_v2->setEnabled(!running && use_gpu && manual_contexts && context_maximum > 0);
    m_yield->setEnabled(!running && use_gpu);
    m_refresh_devices->setEnabled(m_session && !running && bool(discovery));
    for (QWidget* field : std::array<QWidget*, 8>{m_all_devices, m_devices, m_context_mode, m_contexts, m_contexts_v2, m_yield, m_refresh_devices, m_gpu_status}) {
        const bool visible{field != m_context_mode && field != m_contexts && field != m_contexts_v2 && field != m_yield && use_gpu && (field != m_contexts || !m_v2_contexts) && (field != m_contexts_v2 || m_v2_contexts)};
        field->setVisible(visible);
        if (auto* form{qobject_cast<QFormLayout*>(field->parentWidget()->layout())}) {
            if (auto* label{form->labelForField(field)}) label->setVisible(visible);
        }
    }
    QStringList gpu_messages;
    if (!discovery) gpu_messages << tr("Detecting GPU devices…");
    else {
        for (const auto& error : discovery->result.errors) gpu_messages << QString::fromStdString(error);
        if (!gpu_available) gpu_messages << tr("Select an available GPU or choose CPU mining.");
        else if (!gpu_host_memory) gpu_messages << tr("Insufficient host memory for the selected GPU verifiers. Select fewer devices or free memory.");
        if (gpu_available && !gpu_context_memory) gpu_messages << tr("The selected GPUs cannot fit the requested contexts within their memory budgets. Free memory or select fewer contexts/devices.");
    }
    m_gpu_status->setText(gpu_messages.join(QStringLiteral("\n")));
    QStringList device_rates{tr("CPU: %1 H/s · Power: N/A").arg(stats.cpu_hashes_per_second, 0, 'f', 2)};
    for (const auto& device : stats.devices) device_rates << tr("%1: %2 H/s · Power: N/A")
        .arg(QString::fromStdString(device.device.name)).arg(device.hashes_per_second, 0, 'f', 2);
    m_gpu_rates->setText(device_rates.join(QStringLiteral("\n")));
    m_workers->setEnabled(!running && use_cpu && maximum > 0);
    m_hashrate->setText(tr("%1 H/s").arg(stats.hashes_per_second_10s, 0, 'f', 2));
    m_statistics->setText(tr("%1 H/s").arg(stats.hashes_per_second_session, 0, 'f', 2));
    m_runtime->setText(formatRuntime(std::chrono::duration_cast<std::chrono::seconds>(stats.session_elapsed).count()));
    m_active->setText(QString::number(stats.active_workers));
    m_blocks->setText(QString::number(qulonglong{stats.accepted}));
    m_blocks->setToolTip(tr("Not processed: %1; stale or discarded: %2. Processing without observed active-chain acceptance is not counted as accepted.").arg(qulonglong{stats.rejected}).arg(qulonglong{stats.stale}));
    QString status{QString::fromStdString(stats.status)};
    if (!m_wallet || !m_session || !m_client) status = tr("Mining is unavailable until the node and wallet are loaded.");
    else if (!running && stats.state != mining::MiningState::FAILED) {
        if (use_gpu && !gpu_available) status = tr("No selected compatible GPU is available. Choose CPU mining or refresh devices.");
        else if (!gpu_host_memory || (use_cpu && maximum == 0)) status = tr("Insufficient available memory for the selected CPU workers and GPU verifiers.");
        else if (use_gpu && !gpu_context_memory) status = tr("Insufficient GPU memory for the selected contexts. Refresh devices or reduce the selection.");
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
    m_show_destination->setEnabled(m_wallet && !running && !m_privacy);
    m_new_destination->setEnabled(m_wallet && !running && !m_privacy);
    m_payout->setEnabled(m_wallet && !running);
    const auto saved{QSettings{}.value(QStringLiteral("mining/wallet/") + QString::fromStdString(Params().GetConsensus().hashGenesisBlock.ToString()) + "/" + QString::fromStdString((m_wallet ? m_wallet->wallet().getWalletName() : std::string{})) + "/address").toString()};
    if (!m_privacy && !running && !saved.isEmpty()) m_destination->setText(saved);
    m_copy_destination->setEnabled(!m_privacy && (own_destination || !saved.isEmpty()));
}

QString MiningPage::formatGpuContexts(const mining::GpuStats& device)
{
    if (!device.memory_bytes) return tr("No contexts active");
    const auto mode{device.automatic ? tr("Automatic") : tr("Manual")};
    if (device.device.pow_version == mercahash::Version::V2)
        return tr("%1: %2 selected, %3 allocated, %4 executing (up to %5 per batch; resource maximum %6)")
            .arg(mode).arg(device.automatic ? device.estimated_contexts : device.contexts).arg(device.contexts)
            .arg(device.active_contexts).arg(device.batch_contexts).arg(device.budget.maximum_contexts);
    if (device.automatic && device.estimated_contexts > device.contexts)
        return tr("%1: %2 contexts active (%3 estimated; adjusted for available VRAM)")
            .arg(mode).arg(device.contexts).arg(device.estimated_contexts);
    return tr("%1: %2 contexts active").arg(mode).arg(device.contexts);
}

QString MiningPage::formatRuntime(uint64_t seconds)
{
    const auto time{QStringLiteral("%1:%2:%3")
        .arg(qulonglong{(seconds / 3600) % 24}, 2, 10, QLatin1Char{'0'})
        .arg(qulonglong{(seconds / 60) % 60}, 2, 10, QLatin1Char{'0'})
        .arg(qulonglong{seconds % 60}, 2, 10, QLatin1Char{'0'})};
    return seconds < 86400 ? time : tr("%1 d %2").arg(qulonglong{seconds / 86400}).arg(time);
}
