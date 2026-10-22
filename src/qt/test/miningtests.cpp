// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <node/kernel_notifications.h>
#include <qt/clientmodel.h>
#include <qt/miningpage.h>
#include <qt/miningsession.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/walletcontroller.h>
#include <qt/walletmodel.h>
#include <test/util/setup_common.h>
#include <wallet/context.h>
#include <wallet/test/util.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>

#include <qt/addressbookpage.h>
#include <qt/addresstablemodel.h>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTableView>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <limits>

const std::function<void(const std::string&)> G_TEST_LOG_FUN{};
const std::function<std::vector<const char*>()> G_TEST_COMMAND_LINE_ARGUMENTS{};
const std::function<std::string()> G_TEST_GET_FULL_NAME{};
Q_DECLARE_METATYPE(wallet::AddressPurpose)

class MiningTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void liveSoloResponsiveness_data()
    {
        QTest::addColumn<unsigned int>("workers");
        QTest::newRow("one-worker") << 1U;
        QTest::newRow("two-workers") << 2U;
        QTest::newRow("six-workers") << 6U;
    }

    void liveSoloResponsiveness()
    {
        QFETCH(unsigned int, workers);
        if (workers > mining::DetectWorkerLimits().maximum) {
            QSKIP("Worker count exceeds this host's actual CPU/memory budget");
        }
        QSettings settings;
        settings.remove("mining/cpuWorkers");
        TestingSetup setup{ChainType::REGTEST};
        setup.m_node.notifications->setChainstateLoaded(true);
        auto node{interfaces::MakeNode(setup.m_node)};
        auto loader{interfaces::MakeWalletLoader(*setup.m_node.chain, setup.m_args)};
        setup.m_node.wallet_loader = loader.get();
        auto& context{*loader->context()};
        auto wallet{wallet::TestCreateWallet(wallet::CreateMockableWalletDatabase(), context, wallet::WALLET_FLAG_DESCRIPTORS)};
        QVERIFY(wallet::AddWallet(context, wallet));
        OptionsModel options{*node};
        bilingual_str error;
        QVERIFY(options.Init(error));
        ClientModel client{*node, &options};
        std::unique_ptr<const PlatformStyle> style{PlatformStyle::instantiate("other")};
        WalletModel model{interfaces::MakeWallet(context, wallet), client, style.get()};
        MiningSession session{*node};
        MiningPage page{&model};
        page.setClientModel(&client);
        page.setSession(&session);
        auto* start{page.findChild<QPushButton*>("startMining")};
        auto* stop{page.findChild<QPushButton*>("stopMining")};
        auto* selection{page.findChild<QSpinBox*>("manualWorkers")};
        auto* published{page.findChild<QLabel*>("miningSessionStatistics")};
        QVERIFY(start && stop && selection && published);
        selection->setValue(static_cast<int>(workers));
        QCOMPARE(selection->value(), static_cast<int>(workers));
        page.show();
        QCoreApplication::processEvents();
        QElapsedTimer clock;
        clock.start();
        qint64 last_tick{0}, worst_heartbeat{0};
        QTimer heartbeat;
        heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            const auto now{clock.elapsed()};
            worst_heartbeat = std::max(worst_heartbeat, now - last_tick);
            last_tick = now;
        });
        heartbeat.start();
        uint64_t last_hashes{0};
        unsigned int publications{0};
        QString last_text;
        qint64 last_publication{0}, worst_publication_gap{0};
        QObject observer;
        connect(&session, &MiningSession::changed, &observer, [&] {
            const auto current{session.stats()};
            if (current.hashes > last_hashes && published->text() != last_text) {
                if (last_hashes > 0) worst_publication_gap = std::max(worst_publication_gap, clock.elapsed() - last_publication);
                last_hashes = current.hashes;
                last_text = published->text();
                last_publication = clock.elapsed();
                ++publications;
            }
        });
        // Real SoloWorkProvider, authoritative MercaHash, consensus validation,
        // wallet notifications and MiningPage. No hash/provider test double.
        for (unsigned int cycle{0}; cycle < 3; ++cycle) {
            last_hashes = 0;
            publications = 0;
            last_text.clear();
            worst_publication_gap = 0;
            const auto begin{clock.elapsed()};
            start->click();
            QTRY_VERIFY_WITH_TIMEOUT(session.stats().accepted >= 2 && clock.elapsed() - begin >= 2000, 20000);
            QVERIFY(publications >= 2);
            QVERIFY(!published->text().isEmpty());
            QVERIFY(session.stats().hashes_per_second_10s > 0);
            bool handled{false};
            qint64 dispatch_ms{0}, handler_ms{0}, request_at{0};
            const auto due{clock.elapsed() + 100};
            QTimer::singleShot(100, &session, [&] {
                request_at = clock.elapsed();
                dispatch_ms = request_at - due;
                stop->click();
                handler_ms = clock.elapsed() - request_at;
                handled = true;
            });
            QTRY_VERIFY_WITH_TIMEOUT(handled, 2000);
            QTRY_VERIFY_WITH_TIMEOUT(!session.stats().busy, 2000);
            const auto stopped_ms{clock.elapsed() - request_at};
            const auto final{session.stats()};
            QCOMPARE(final.active_workers, 0U);
            QCOMPARE(final.rejected, uint64_t{0});
            qInfo().nospace() << "Live V1 Solo workers=" << workers << " cycle=" << cycle
                << " accepted=" << final.accepted << " hashes=" << final.hashes
                << " elapsed_ms=" << final.session_elapsed.count() << " rate_hps=" << final.hashes_per_second_session
                << " stop_dispatch_ms=" << dispatch_ms << " stop_handler_ms=" << handler_ms
                << " stop_observed_ms=" << stopped_ms << " heartbeat_gap_ms=" << worst_heartbeat
                << " publication_gap_ms=" << worst_publication_gap;
            QVERIFY2(dispatch_ms < 1000, "Stop timer dispatch exceeded one second");
            QVERIFY2(handler_ms < 1000, "Stop handler blocked the Qt thread for one second");
            QVERIFY2(stopped_ms < 1000, "Workers did not stop within one second of the Qt handler");
            QVERIFY2(worst_publication_gap < 1500, "Statistics publication stalled during accepted-block turnover");
            QTest::qWait(50);
            QCOMPARE(session.stats().accepted, final.accepted);
            QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 1000);
        }
        start->click();
        QTRY_VERIFY_WITH_TIMEOUT(session.stats().hashes > 0, 10000);
        const auto shutting_down{clock.elapsed()};
        session.shutdown();
        const auto shutdown_ms{clock.elapsed() - shutting_down};
        QTest::qWait(20);
        heartbeat.stop();
        qInfo().nospace() << "Live V1 Solo workers=" << workers << " session_shutdown_ms=" << shutdown_ms
            << " heartbeat_gap_ms=" << worst_heartbeat;
        QVERIFY(!session.stats().busy);
        QCOMPARE(session.stats().active_workers, 0U);
        QVERIFY2(shutdown_ms < 1000, "MiningSession shutdown exceeded one second");
        QVERIFY2(worst_heartbeat < 1000, "Qt event-loop heartbeat stalled for one second");
        QVERIFY(wallet::RemoveWallet(context, wallet, std::nullopt));
        setup.m_node.wallet_loader = nullptr;
        settings.remove("mining/cpuWorkers");
    }

    void actualGpuContextsAfterFallback()
    {
        mining::GpuStats small, large;
        small.contexts = 39; small.estimated_contexts = 43;
        small.memory_bytes = 39ULL * (128ULL << 20);
        large.contexts = 181; large.estimated_contexts = 181;
        large.memory_bytes = 181ULL * (128ULL << 20);
        QCOMPARE(MiningPage::formatGpuContexts(small), QString{"Automatic: 39 contexts active (43 estimated; adjusted for available VRAM)"});
        QCOMPARE(MiningPage::formatGpuContexts(large), QString{"Automatic: 181 contexts active"});
        small.automatic = false;
        QCOMPARE(MiningPage::formatGpuContexts(small), QString{"Manual: 39 contexts active"});
        small.memory_bytes = 0;
        QCOMPARE(MiningPage::formatGpuContexts(small), QString{"No contexts active"});
        QCOMPARE(MiningPage::formatGpuContexts(large), QString{"Automatic: 181 contexts active"});
    }
    void maximumAutomaticAndReducedManualGpuContexts()
    {
        TestingSetup setup{ChainType::REGTEST};
        auto node{interfaces::MakeNode(setup.m_node)};
        std::atomic<uint64_t> free_cuda{6ULL << 30};
        MiningSession session{*node, nullptr, [&] {
            mining::GpuDiscovery result;
            mining::GpuDevice cuda{mining::GpuVendor::NVIDIA_CUDA};
            cuda.id = "cuda:0"; cuda.name = "Simulated CUDA"; cuda.supported = true;
            cuda.total_memory = 80ULL << 30;
            cuda.available_memory = free_cuda;
            cuda.max_allocation = free_cuda;
            mining::GpuDevice amd{mining::GpuVendor::AMD_OPENCL};
            amd.id = "opencl:0"; amd.name = "Simulated AMD"; amd.supported = true;
            amd.total_memory = 16ULL << 30;
            amd.available_memory = UINT64_MAX; // Never interpret this as CUDA free VRAM.
            amd.max_allocation = 1ULL << 30;
            mining::GpuDevice large{mining::GpuVendor::NVIDIA_CUDA};
            large.id = "cuda:1"; large.name = "Simulated large CUDA"; large.supported = true;
            large.total_memory = large.available_memory = large.max_allocation = 24ULL << 30;
            result.devices = {cuda, amd, large};
            return result;
        }};
        MiningPage page{nullptr};
        page.setSession(&session);
        auto* hardware{page.findChild<QComboBox*>("miningHardware")};
        auto* mode{page.findChild<QComboBox*>("gpuContextMode")};
        auto* contexts{page.findChild<QSpinBox*>("gpuContexts")};
        auto* devices{page.findChild<QListWidget*>("gpuDevices")};
        auto* all{page.findChild<QCheckBox*>("automaticGpuDevices")};
        auto* memory{page.findChild<QLabel*>("gpuMemoryStatus")};
        auto* refresh{page.findChild<QPushButton*>("refreshGpuDevices")};
        QVERIFY(hardware && mode && contexts && devices && all && memory && refresh);
        hardware->setCurrentIndex(1);
        QTRY_COMPARE(devices->count(), 3);
        QCOMPARE(mode->currentIndex(), 0); // Maximum safe automatic on every new page.
        QVERIFY(!contexts->isEnabled());
        QCOMPARE(contexts->maximum(), 8); // Only the Manual field uses the tightest allocation limit.
        QVERIFY(memory->text().contains("43 contexts:"));
        QVERIFY(memory->text().contains("181 contexts:")); // Neither CUDA device is capped by the small AMD device.
        QVERIFY(memory->text().contains("8 contexts selected"));
        QVERIFY(memory->text().contains("free VRAM is unknown"));
        mode->setCurrentIndex(1);
        QVERIFY(contexts->isEnabled());
        all->setChecked(false);
        devices->item(1)->setCheckState(Qt::Unchecked);
        devices->item(2)->setCheckState(Qt::Unchecked);
        QVERIFY(contexts->maximum() > 36);
        contexts->setValue(36);
        QCOMPARE(contexts->value(), 36);
        QVERIFY(memory->text().contains("4608 MiB scratchpads"));
        QVERIFY(!memory->text().contains("Warning:"));
        const auto rescan = [&] {
            const auto revision{session.gpuDiscoverySnapshot()->revision};
            refresh->click();
            QTRY_VERIFY(session.gpuDiscoverySnapshot() && session.gpuDiscoverySnapshot()->revision > revision);
            QTRY_VERIFY(refresh->isEnabled());
        };
        free_cuda = 5200ULL << 20; // 36 fits, with just over the required 512 MiB reserve.
        rescan();
        QTRY_COMPARE(contexts->maximum(), 36);
        QCOMPARE(contexts->value(), 36);
        QCOMPARE(devices->item(1)->checkState(), Qt::Unchecked); // Refresh preserves explicit device selection.
        QVERIFY(memory->text().contains("Warning:"));
        free_cuda = 5ULL << 30;
        rescan();
        QTRY_VERIFY(contexts->maximum() < 36);
        QCOMPARE(contexts->value(), contexts->maximum());
        free_cuda = 24ULL << 30;
        rescan();
        QTRY_VERIFY(contexts->maximum() > 128);
        contexts->setValue(128);
        QCOMPARE(contexts->value(), 128); // Larger GPUs are not capped at 36.
        free_cuda = 80ULL << 30;
        rescan();
        QTRY_COMPARE(contexts->maximum(), static_cast<int>(mining::MAX_GPU_CONTEXTS));
        QVERIFY(memory->text().contains("nonce scheduler limits each GPU to 256"));
        free_cuda = 0;
        rescan();
        QTRY_VERIFY(!contexts->isEnabled());
        QVERIFY(memory->text().contains("cannot fit"));
        QVERIFY(!page.findChild<QPushButton*>("startMining")->isEnabled());
        QVERIFY(!session.stats().busy); // Selector and asynchronous discovery never start mining.
        mode->setCurrentIndex(0);
        QVERIFY(!contexts->isEnabled());
        devices->item(2)->setCheckState(Qt::Checked);
        QVERIFY(memory->text().contains("181 contexts:"));
        QVERIFY(!memory->text().contains("cannot fit")); // An exhausted small device does not block automatic on the large one.
        MiningPage reopened{nullptr};
        QCOMPARE(reopened.findChild<QComboBox*>("gpuContextMode")->currentIndex(), 0);
    }

    void hardwareAwareManualWorkerSelection()
    {
        QSettings settings;
        settings.remove("mining/cpuWorkers");
        unsigned int cpus{192};
        uint64_t memory{uint64_t{256} << 30};
        const auto limits{[&] { return mining::CalculateWorkerLimits(cpus, memory); }};
        {
            MiningPage page{nullptr, nullptr, limits};
            auto* workers{page.findChild<QSpinBox*>("manualWorkers")};
            QVERIFY(workers);
            QVERIFY(!page.findChild<QCheckBox*>("automaticWorkers"));
            QCOMPARE(workers->minimum(), 1);
            for (const unsigned int count : {8U, 12U, 16U, 32U, 64U, 192U}) {
                cpus = count;
                page.setPrivacy(false); // Refresh the same idle page, without restarting it.
                QCOMPARE(workers->maximum(), static_cast<int>(count));
                workers->setValue(count);
                QCOMPARE(workers->value(), static_cast<int>(count));
                QCOMPARE(settings.value("mining/cpuWorkers").toInt(), static_cast<int>(count));
            }
            cpus = 4;
            page.setPrivacy(false);
            QCOMPARE(workers->maximum(), 4);
            QCOMPARE(workers->value(), 4);
            QCOMPARE(settings.value("mining/cpuWorkers").toInt(), 192); // A clamp is not a manual preference change.
            memory = uint64_t{512} << 20;
            page.setPrivacy(false);
            QCOMPARE(workers->value(), 1);
            QVERIFY(!workers->isEnabled());
            QVERIFY(!page.findChild<QPushButton*>("startMining")->isEnabled());
        }
        memory = uint64_t{256} << 30;
        cpus = 64;
        {
            MiningPage reopened{nullptr, nullptr, limits};
            auto* workers{reopened.findChild<QSpinBox*>("manualWorkers")};
            QCOMPARE(workers->value(), 64); // Persisted 192 is safely clamped after changed hardware.
            workers->setValue(7);
        }
        {
            MiningPage reopened{nullptr, nullptr, limits};
            QCOMPARE(reopened.findChild<QSpinBox*>("manualWorkers")->value(), 7);
        }
        cpus = std::numeric_limits<unsigned int>::max();
        memory = std::numeric_limits<uint64_t>::max();
        settings.setValue("mining/cpuWorkers", qulonglong{std::numeric_limits<unsigned int>::max()});
        MiningPage extreme{nullptr, nullptr, limits};
        QCOMPARE(extreme.findChild<QSpinBox*>("manualWorkers")->maximum(), std::numeric_limits<int>::max());
        QCOMPARE(extreme.findChild<QSpinBox*>("manualWorkers")->value(), std::numeric_limits<int>::max());
        settings.remove("mining/cpuWorkers");
    }

    void combinedWorkerSelectionReservesHostVerifiers()
    {
        TestingSetup setup{ChainType::REGTEST};
        auto node{interfaces::MakeNode(setup.m_node)};
        MiningSession session{*node, nullptr, [] {
            mining::GpuDiscovery result;
            mining::GpuDevice device{mining::GpuVendor::AMD_OPENCL};
            device.id = "opencl:0";
            device.name = "Simulated AMD";
            device.supported = true;
            result.devices.push_back(std::move(device));
            return result;
        }};
        QSettings{}.setValue("mining/cpuWorkers", 3);
        MiningPage page{nullptr, nullptr, [] { return mining::CalculateWorkerLimits(12, uint64_t{1} << 30); }};
        page.setSession(&session);
        auto* workers{page.findChild<QSpinBox*>("manualWorkers")};
        auto* hardware{page.findChild<QComboBox*>("miningHardware")};
        QCOMPARE(workers->maximum(), 3);
        QTRY_COMPARE(page.findChild<QListWidget*>("gpuDevices")->count(), 1);
        QVERIFY(page.findChild<QCheckBox*>("automaticGpuDevices")->isChecked());
        hardware->setCurrentIndex(2);
        QCOMPARE(workers->maximum(), 2);
        QCOMPARE(workers->value(), 2);
        QCOMPARE(QSettings{}.value("mining/cpuWorkers").toInt(), 3);
        QVERIFY(!session.stats().busy); // Device discovery and page loading never mine.
        hardware->setCurrentIndex(0);
        QCOMPARE(workers->maximum(), 3);
        QSettings{}.remove("mining/cpuWorkers");
    }

    void manualWorkersPersistAndRuntimeFormatting()
    {
        QSettings settings;
        settings.setValue("mining/cpuWorkers", 100000);
        {
            MiningPage page{nullptr};
            auto* workers{page.findChild<QSpinBox*>("manualWorkers")};
            QVERIFY(workers);
            QCOMPARE(workers->value(), workers->maximum());
            workers->setValue(1);
        }
        MiningPage reopened{nullptr};
        QCOMPARE(reopened.findChild<QSpinBox*>("manualWorkers")->value(), 1);
        settings.setValue("mining/cpuWorkers", -4);
        MiningPage clamped{nullptr};
        QCOMPARE(clamped.findChild<QSpinBox*>("manualWorkers")->value(), 1);
        settings.remove("mining/cpuWorkers");
        QCOMPARE(MiningPage::formatRuntime(0), QString{"00:00:00"});
        QCOMPARE(MiningPage::formatRuntime(3661), QString{"01:01:01"});
        QCOMPARE(MiningPage::formatRuntime(86400), QString{"1 d 00:00:00"});
        QCOMPARE(MiningPage::formatRuntime(100 * 86400 + 3661), QString{"100 d 01:01:01"});
    }

    void pickaxeFollowsPaletteAtHighDpi()
    {
        std::unique_ptr<const PlatformStyle> style{PlatformStyle::instantiate("other")};
        const auto previous{QApplication::palette()};
        for (const auto color : {QColor{Qt::black}, QColor{Qt::white}}) {
            auto palette{previous};
            palette.setColor(QPalette::WindowText, color);
            QApplication::setPalette(palette);
            const auto icon{style->SingleColorIcon(":/icons/mining")};
            QVERIFY(!icon.isNull());
            for (const auto size : {24, 48, 96}) {
                const auto pixels{icon.pixmap(size, size).toImage()};
                QVERIFY(!pixels.isNull());
                bool found{false};
                for (int x{0}; x < pixels.width(); ++x)
                    for (int y{0}; y < pixels.height(); ++y)
                        if (pixels.pixelColor(x, y).alpha() > 200) {
                            QCOMPARE(pixels.pixelColor(x, y).rgb(), color.rgb());
                            found = true;
                        }
                QVERIFY(found);
            }
        }
        QApplication::setPalette(previous);
    }

    void missingWalletUnlockCancellationAndNodeShutdown()
    {
        TestingSetup setup{ChainType::REGTEST};
        setup.m_node.notifications->setChainstateLoaded(true);
        auto node{interfaces::MakeNode(setup.m_node)};
        auto loader{interfaces::MakeWalletLoader(*setup.m_node.chain, setup.m_args)};
        setup.m_node.wallet_loader = loader.get();
        auto& context{*loader->context()};
        auto wallet{wallet::TestCreateWallet(wallet::CreateMockableWalletDatabase(), context, wallet::WALLET_FLAG_DESCRIPTORS)};
        QVERIFY(wallet::AddWallet(context, wallet));
        SecureString passphrase;
        passphrase.assign("qt-solo-shutdown-passphrase");
        QVERIFY(wallet->EncryptWallet(passphrase));
        OptionsModel options{*node};
        bilingual_str error;
        QVERIFY(options.Init(error));
        ClientModel client{*node, &options};
        std::unique_ptr<const PlatformStyle> style{PlatformStyle::instantiate("other")};
        WalletModel model{interfaces::MakeWallet(context, wallet), client, style.get()};
        MiningSession session{*node};
        MiningPage empty{nullptr};
        empty.setClientModel(&client);
        empty.setSession(&session);
        QVERIFY(!empty.findChild<QPushButton*>("startMining")->isEnabled());
        QVERIFY(empty.findChild<QLabel*>("miningStatus")->text().contains("unavailable"));
        QVERIFY(!session.start(nullptr, 1));
        unsigned int prompts{0};
        const auto cancelled_prompt{connect(&model, &WalletModel::requireUnlock, this, [&] { ++prompts; })};
        QVERIFY(session.start(&model, 1));
        QTRY_COMPARE_WITH_TIMEOUT(prompts, 1U, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.stats().busy, 10000);
        QVERIFY(session.stats().state == mining::MiningState::FAILED);
        QCOMPARE(session.stats().hashes, uint64_t{0});
        QVERIFY(wallet->IsLocked());
        disconnect(cancelled_prompt);
        const auto successful_prompt{connect(&model, &WalletModel::requireUnlock, this, [&] { ++prompts; QVERIFY(wallet->Unlock(passphrase)); })};
        QVERIFY(session.start(&model, 1));
        QTRY_VERIFY_WITH_TIMEOUT(session.stats().hashes > 0, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(wallet->IsLocked(), 10000);
        QCOMPARE(prompts, 2U);
        node->startShutdown();
        QTRY_VERIFY_WITH_TIMEOUT(!session.stats().busy, 10000);
        QCOMPARE(session.stats().active_workers, 0U);
        QVERIFY(!session.start(&model, 1));
        disconnect(successful_prompt);
        session.shutdown();
        QVERIFY(wallet::RemoveWallet(context, wallet, std::nullopt));
        setup.m_node.wallet_loader = nullptr;
    }

    void sharedDeviceDiscoveryRefresh()
    {
        TestingSetup setup{ChainType::REGTEST};
        auto node{interfaces::MakeNode(setup.m_node)};
        std::atomic<unsigned int> scans{0};
        MiningSession session{*node, nullptr, [&] {
            mining::GpuDiscovery result;
            const auto scan{++scans};
            if (scan < 3) {
                mining::GpuDevice device{scan == 1 ? mining::GpuVendor::NVIDIA_CUDA : mining::GpuVendor::AMD_OPENCL};
                device.id = scan == 1 ? "cuda:0" : "opencl:0";
                device.name = scan == 1 ? "Simulated NVIDIA" : "Simulated AMD";
                device.supported = scan == 1;
                result.devices.push_back(std::move(device));
            }
            return result;
        }};
        MiningPage first{nullptr};
        MiningPage second{nullptr};
        first.setSession(&session);
        second.setSession(&session);
        auto* first_devices{first.findChild<QListWidget*>("gpuDevices")};
        auto* second_devices{second.findChild<QListWidget*>("gpuDevices")};
        auto* refresh{first.findChild<QPushButton*>("refreshGpuDevices")};
        auto* hardware{first.findChild<QComboBox*>("miningHardware")};
        QVERIFY(first_devices && second_devices && refresh && hardware);
        hardware->setCurrentIndex(1);
        QTRY_COMPARE(first_devices->count(), 1);
        QTRY_COMPARE(second_devices->count(), 1);
        QCOMPARE(first_devices->item(0)->data(Qt::UserRole).toString(), QString{"cuda:0"});
        QCOMPARE(second_devices->item(0)->data(Qt::UserRole).toString(), QString{"cuda:0"});
        QVERIFY(second_devices->item(0)->flags() & Qt::ItemIsEnabled);
        const auto original{session.gpuDiscoverySnapshot()};
        QVERIFY(original);
        QVERIFY(refresh->isEnabled());
        refresh->click();
        QTRY_VERIFY(session.gpuDiscoverySnapshot() && session.gpuDiscoverySnapshot()->revision > original->revision);
        QTRY_COMPARE(first_devices->item(0)->data(Qt::UserRole).toString(), QString{"opencl:0"});
        // The other wallet page must reload the shared snapshot even when the
        // discovery completes between timer ticks and it never sees an empty one.
        QTRY_COMPARE(second_devices->item(0)->data(Qt::UserRole).toString(), QString{"opencl:0"});
        QVERIFY(!(second_devices->item(0)->flags() & Qt::ItemIsEnabled));
        refresh->click();
        QTRY_COMPARE(first_devices->count(), 0);
        QTRY_COMPARE(second_devices->count(), 0);
        QCOMPARE(scans.load(), 3U);
    }

    void soloWalletAndPageLifecycle()
    {
        TestingSetup setup{ChainType::REGTEST};
        setup.m_node.notifications->setChainstateLoaded(true);
        auto node{interfaces::MakeNode(setup.m_node)};
        auto loader{interfaces::MakeWalletLoader(*setup.m_node.chain, setup.m_args)};
        setup.m_node.wallet_loader = loader.get();
        auto& context{*loader->context()};
        auto wallet{wallet::TestCreateWallet(wallet::CreateMockableWalletDatabase(), context, wallet::WALLET_FLAG_DESCRIPTORS)};
        QVERIFY(wallet::AddWallet(context, wallet));
        SecureString passphrase;
        passphrase.assign("qt-solo-creation-passphrase");
        QVERIFY(wallet->EncryptWallet(passphrase));
        OptionsModel options{*node};
        bilingual_str error;
        QVERIFY(options.Init(error));
        ClientModel client{*node, &options};
        std::unique_ptr<const PlatformStyle> style{PlatformStyle::instantiate("other")};
        WalletModel model{interfaces::MakeWallet(context, wallet), client, style.get()};
        unsigned int unlocks{0};
        connect(&model, &WalletModel::requireUnlock, this, [&] { ++unlocks; QVERIFY(wallet->Unlock(passphrase)); });
        MiningSession session{*node};
        MiningPage page{&model};
        page.setClientModel(&client);
        page.setSession(&session);
        auto* start{page.findChild<QPushButton*>("startMining")};
        auto* stop{page.findChild<QPushButton*>("stopMining")};
        QVERIFY(start && stop);
        QVERIFY(!page.findChild<QComboBox*>("miningMode"));
        QVERIFY(!page.findChild<QCheckBox*>("automaticWorkers"));
        QVERIFY(!page.findChild<QLineEdit*>("poolHost"));
        auto* hardware{page.findChild<QComboBox*>("miningHardware")};
        QVERIFY(hardware); QCOMPARE(hardware->currentIndex(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(session.gpuDiscovery().has_value(), 10000);
        const auto discovery{session.gpuDiscovery()};
        const bool compatible{std::any_of(discovery->devices.begin(), discovery->devices.end(), [](const auto& device) { return device.supported; })};
        hardware->setCurrentIndex(1);
        if (!compatible) QVERIFY(!start->isEnabled());
        hardware->setCurrentIndex(0);
        QVERIFY(!session.stats().busy);
        page.resize(640, 480);
        page.show();
        QCoreApplication::processEvents();
        auto* scroll{page.findChild<QScrollArea*>()};
        QVERIFY(scroll);
        QVERIFY(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(page.rect().contains(start->mapTo(&page, QPoint{0, 0})));
        QVERIFY(page.rect().contains(stop->mapTo(&page, QPoint{0, 0})));
        if (const auto directory{qEnvironmentVariable("MERCATURA_UI_CAPTURE")}; !directory.isEmpty()) {
            QVERIFY(page.grab().save(directory + "/solo-page.png"));
        }
        QVERIFY(start->isEnabled());
        start->click();
        QTRY_VERIFY_WITH_TIMEOUT(!session.stats().destination.empty(), 30000);
        QCOMPARE(unlocks, 1U);
        QTRY_VERIFY_WITH_TIMEOUT(session.stats().hashes > 0, 30000);
        QVERIFY(session.stats().destination.starts_with("mcrt1z"));
        QTRY_VERIFY_WITH_TIMEOUT(wallet->IsLocked(), 10000);
        const auto address{session.stats().destination};
        QTRY_COMPARE(page.findChild<QLabel*>("miningDestination")->text(), QString::fromStdString(address));
        QCoreApplication::processEvents();
        QVERIFY(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        auto* copy{page.findChild<QPushButton*>("copyMiningDestination")};
        auto* show{page.findChild<QPushButton*>("showMiningDestination")};
        QVERIFY(copy && show && copy->isEnabled() && show->isEnabled());
        copy->click();
        QCOMPARE(QApplication::clipboard()->text(), QString::fromStdString(address));
        QSignalSpy requested{&page, &MiningPage::showRewardAddress};
        show->click();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.front().front().toString(), QString::fromStdString(address));
        AddressBookPage book{style.get(), AddressBookPage::ForEditing, AddressBookPage::ReceivingTab};
        book.setModel(model.getAddressTableModel());
        book.selectAddress(QString::fromStdString(address));
        auto* table{book.findChild<QTableView*>("tableView")};
        QVERIFY(table);
        QCOMPARE(table->selectionModel()->selectedRows(AddressTableModel::Address).size(), 1);
        QCOMPARE(table->selectionModel()->selectedRows(AddressTableModel::Address).front().data(Qt::EditRole).toString(), QString::fromStdString(address));
        page.setPrivacy(true);
        QCOMPARE(page.findChild<QLabel*>("miningDestination")->text(), QString{"(hidden)"});
        QVERIFY(!page.findChild<QPushButton*>("copyMiningDestination")->isEnabled());
        QVERIFY(!page.findChild<QPushButton*>("showMiningDestination")->isEnabled());
        page.setPrivacy(false);
        QVERIFY(!hardware->isEnabled());
        stop->click();
        QTRY_VERIFY_WITH_TIMEOUT(!session.stats().busy, 10000);
        QCOMPARE(session.stats().active_workers, 0U);
        QVERIFY(session.start(&model, 1));
        QTRY_VERIFY_WITH_TIMEOUT(session.stats().destination == address, 10000);
        QCOMPARE(unlocks, 1U);
        QVERIFY(wallet->IsLocked());
        QVERIFY(wallet::RemoveWallet(context, wallet, std::nullopt));
        session.walletUnloaded(&model);
        QTRY_VERIFY_WITH_TIMEOUT(!session.stats().busy, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.owner(), 10000);
        QVERIFY(wallet::AddWallet(context, wallet));
        QVERIFY(session.start(&model, 1));
        QTRY_VERIFY_WITH_TIMEOUT(session.stats().destination == address, 10000);
        QCOMPARE(unlocks, 1U);
        session.shutdown();
        QVERIFY(!session.stats().busy);
        QVERIFY(wallet::RemoveWallet(context, wallet, std::nullopt));
        setup.m_node.wallet_loader = nullptr;
    }
    void controllerDefersWalletDestruction()
    {
        TestingSetup setup{ChainType::REGTEST};
        setup.m_node.notifications->setChainstateLoaded(true);
        auto node{interfaces::MakeNode(setup.m_node)};
        auto loader{interfaces::MakeWalletLoader(*setup.m_node.chain, setup.m_args)};
        setup.m_node.wallet_loader = loader.get();
        auto& context{*loader->context()};
        auto wallet{wallet::TestCreateWallet(wallet::CreateMockableWalletDatabase(), context, wallet::WALLET_FLAG_DESCRIPTORS)};
        QVERIFY(wallet::AddWallet(context, wallet));
        OptionsModel options{*node};
        bilingual_str error;
        QVERIFY(options.Init(error));
        ClientModel client{*node, &options};
        std::unique_ptr<const PlatformStyle> style{PlatformStyle::instantiate("other")};
        WalletController controller{client, style.get(), nullptr};
        QPointer<WalletModel> model{controller.getOrCreateWallet(interfaces::MakeWallet(context, wallet))};
        auto* session{controller.miningSession()};
        QVERIFY(session->start(model, 1));
        QTRY_VERIFY_WITH_TIMEOUT(session->stats().hashes > 0, 30000);
        bool stopped_before_destruction{false};
        connect(model, &QObject::destroyed, this, [&] { stopped_before_destruction = !session->stats().busy; });
        QVERIFY(wallet::RemoveWallet(context, wallet, std::nullopt));
        QTRY_VERIFY_WITH_TIMEOUT(!model, 30000);
        QVERIFY(stopped_before_destruction);
        QCOMPARE(session->stats().active_workers, 0U);
        setup.m_node.wallet_loader = nullptr;
    }

    void encryptedAddressCreation()
    {
        TestingSetup setup{ChainType::REGTEST};
        setup.m_node.notifications->setChainstateLoaded(true);
        auto node{interfaces::MakeNode(setup.m_node)};
        auto loader{interfaces::MakeWalletLoader(*setup.m_node.chain, setup.m_args)};
        setup.m_node.wallet_loader = loader.get();
        auto& context{*loader->context()};
        auto wallet{wallet::TestCreateWallet(wallet::CreateMockableWalletDatabase(), context, wallet::WALLET_FLAG_DESCRIPTORS)};
        QVERIFY(wallet::AddWallet(context, wallet));
        SecureString passphrase;
        passphrase.assign("qt-mining-creation-passphrase");
        QVERIFY(wallet->EncryptWallet(passphrase));
        OptionsModel options{*node};
        bilingual_str error;
        QVERIFY(options.Init(error));
        ClientModel client{*node, &options};
        std::unique_ptr<const PlatformStyle> style{PlatformStyle::instantiate("other")};
        WalletModel model{interfaces::MakeWallet(context, wallet), client, style.get()};
        unsigned int unlocks{0};
        connect(&model, &WalletModel::requireUnlock, this, [&] {
            ++unlocks;
            QVERIFY(wallet->Unlock(passphrase));
        });
        MiningSession session{*node};
        QVERIFY(session.start(&model, 1));
        QTRY_VERIFY_WITH_TIMEOUT(session.stats().hashes > 0, 30000);
        QCOMPARE(unlocks, 1U);
        QTRY_VERIFY_WITH_TIMEOUT(wallet->IsLocked(), 10000);
        session.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!session.stats().busy, 30000);
        QVERIFY(session.start(&model, 1));
        QTRY_VERIFY_WITH_TIMEOUT(session.stats().hashes > 0, 30000);
        QCOMPARE(unlocks, 1U); // Locked reuse must never ask again.
        session.shutdown();
        QVERIFY(wallet::RemoveWallet(context, wallet, std::nullopt));
        setup.m_node.wallet_loader = nullptr;
    }

    void pageLifecycle()
    {
        TestingSetup setup{ChainType::REGTEST};
        setup.m_node.notifications->setChainstateLoaded(true);
        auto node{interfaces::MakeNode(setup.m_node)};
        auto loader{interfaces::MakeWalletLoader(*setup.m_node.chain, setup.m_args)};
        setup.m_node.wallet_loader = loader.get();
        auto& context{*loader->context()};
        auto wallet{wallet::TestCreateWallet(wallet::CreateMockableWalletDatabase(), context, wallet::WALLET_FLAG_DESCRIPTORS)};
        QVERIFY(wallet);
        QVERIFY(wallet::AddWallet(context, wallet));
        OptionsModel options{*node};
        bilingual_str error;
        QVERIFY(options.Init(error));
        ClientModel client{*node, &options};
        std::unique_ptr<const PlatformStyle> style{PlatformStyle::instantiate("other")};
        WalletModel model{interfaces::MakeWallet(context, wallet), client, style.get()};
        std::vector<bilingual_str> warnings;
        auto second_wallet{wallet::CWallet::CreateNew(context, "second", wallet::CreateMockableWalletDatabase(), wallet::WALLET_FLAG_DESCRIPTORS, error, warnings)};
        QVERIFY(second_wallet);
        QVERIFY(wallet::AddWallet(context, second_wallet));
        WalletModel second_model{interfaces::MakeWallet(context, second_wallet), client, style.get()};
        MiningSession session{*node};
        MiningPage page{&model};
        page.setClientModel(&client);
        page.setSession(&session);
        auto* start{page.findChild<QPushButton*>("startMining")};
        auto* stop{page.findChild<QPushButton*>("stopMining")};
        QVERIFY(start && stop);
        QVERIFY(start->isEnabled());
        QVERIFY(!stop->isEnabled());
        QCOMPARE(session.stats().hashes, uint64_t{0});
        QVERIFY(!session.stats().busy); // Loading a page must never start mining.
        start->click();
        QTRY_VERIFY_WITH_TIMEOUT(session.stats().hashes > 0, 30000);
        QVERIFY(!start->isEnabled());
        QVERIFY(stop->isEnabled());
        QVERIFY(!session.start(&model, 1)); // No duplicate fleet.
        QVERIFY(!session.start(&second_model, 1)); // Across wallets too.
        QTRY_VERIFY_WITH_TIMEOUT(session.stats().accepted > 0, 120000);
        auto* address{page.findChild<QLabel*>("miningDestination")};
        QVERIFY(address);
        const auto first_destination{session.stats().destination};
        QVERIFY(!first_destination.empty());
        stop->click();
        QTRY_VERIFY_WITH_TIMEOUT(!session.stats().busy, 30000);
        QCOMPARE(session.stats().active_workers, 0U);
        QVERIFY(session.start(&second_model, 1));
        QTRY_VERIFY_WITH_TIMEOUT(session.stats().hashes > 0, 30000);
        QVERIFY(session.stats().destination != first_destination);
        session.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!session.stats().busy, 30000);
        SecureString passphrase;
        passphrase.assign("qt-mining-test-passphrase");
        QVERIFY(wallet->EncryptWallet(passphrase));
        QVERIFY(wallet->IsLocked());
        QVERIFY(session.start(&model, 1));
        QTRY_VERIFY_WITH_TIMEOUT(session.stats().hashes > 0, 30000);
        QCOMPARE(session.stats().destination, first_destination);
        QVERIFY(wallet->IsLocked()); // Persisted payout needs no unlock.
        QVERIFY(wallet::RemoveWallet(context, second_wallet, std::nullopt));
        session.walletUnloaded(&second_model);
        QVERIFY(session.stats().busy); // Unrelated wallet unload leaves it running.
        QVERIFY(wallet::RemoveWallet(context, wallet, std::nullopt));
        session.walletUnloaded(&model);
        QTRY_VERIFY_WITH_TIMEOUT(!session.stats().busy, 30000);
        QCOMPARE(session.stats().active_workers, 0U);
        QTRY_VERIFY_WITH_TIMEOUT(!session.owner(), 10000);
        QVERIFY(wallet::AddWallet(context, wallet));
        QVERIFY(session.start(&model, 1));
        QTRY_VERIFY_WITH_TIMEOUT(session.stats().hashes > 0, 30000);
        session.shutdown();
        QVERIFY(!session.stats().busy);
        QCOMPARE(session.stats().active_workers, 0U);
        QVERIFY(!session.start(&model, 1));
        QVERIFY(wallet::RemoveWallet(context, wallet, std::nullopt));
        setup.m_node.wallet_loader = nullptr;
    }
};

int main(int argc, char* argv[])
{
#ifndef WIN32
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "minimal");
#endif
    QApplication app{argc, argv};
    QCoreApplication::setOrganizationName("MercaturaMiningTests");
    QCoreApplication::setApplicationName("MercaturaMiningTests");
    QSettings settings;
    settings.clear();
    qRegisterMetaType<wallet::AddressPurpose>();
    MiningTests tests;
    const int result{QTest::qExec(&tests, argc, argv)};
    settings.clear();
    return result;
}
#include <miningtests.moc>
