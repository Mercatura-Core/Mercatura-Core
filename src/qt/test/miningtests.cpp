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

#include <QApplication>
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

#include <functional>

const std::function<void(const std::string&)> G_TEST_LOG_FUN{};
const std::function<std::vector<const char*>()> G_TEST_COMMAND_LINE_ARGUMENTS{};
const std::function<std::string()> G_TEST_GET_FULL_NAME{};
Q_DECLARE_METATYPE(wallet::AddressPurpose)

class MiningTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
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

    void poolWalletAndPageLifecycle()
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
        passphrase.assign("qt-pool-creation-passphrase");
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
        auto* mode{page.findChild<QComboBox*>("miningMode")};
        auto* host{page.findChild<QLineEdit*>("poolHost")};
        auto* port{page.findChild<QSpinBox*>("poolPort")};
        auto* start{page.findChild<QPushButton*>("startMining")};
        auto* stop{page.findChild<QPushButton*>("stopMining")};
        QVERIFY(mode && host && port && start && stop);
        QCOMPARE(mode->currentIndex(), 0);
        auto* hardware{page.findChild<QComboBox*>("miningHardware")};
        QVERIFY(hardware); QCOMPARE(hardware->currentIndex(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(session.gpuDiscovery().has_value(), 10000);
        const auto discovery{session.gpuDiscovery()};
        const bool compatible{std::any_of(discovery->devices.begin(), discovery->devices.end(), [](const auto& device) { return device.supported; })};
        hardware->setCurrentIndex(1);
        if (!compatible) QVERIFY(!start->isEnabled());
        hardware->setCurrentIndex(0);
        QVERIFY(!session.stats().busy);
        mode->setCurrentIndex(1);
        host->clear();
        page.resize(640, 480);
        page.show();
        QCoreApplication::processEvents();
        auto* scroll{page.findChild<QScrollArea*>()};
        QVERIFY(scroll);
        QVERIFY(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(page.rect().contains(start->mapTo(&page, QPoint{0, 0})));
        QVERIFY(page.rect().contains(stop->mapTo(&page, QPoint{0, 0})));
        QVERIFY(!mode->accessibleName().isEmpty());
        if (const auto directory{qEnvironmentVariable("MERCATURA_UI_CAPTURE")}; !directory.isEmpty()) {
            QVERIFY(page.grab().save(directory + "/pool-page.png"));
        }
        QVERIFY(!start->isEnabled());
        host->setText("127.0.0.1");
        port->setValue(1);
        QVERIFY(start->isEnabled());
        start->click();
        QTRY_VERIFY_WITH_TIMEOUT(!session.stats().destination.empty(), 30000);
        QCOMPARE(unlocks, 1U);
        QVERIFY(session.poolMode());
        QTRY_VERIFY_WITH_TIMEOUT(session.stats().status.find("Pool disconnected") != std::string::npos, 10000);
        QVERIFY(session.stats().destination.starts_with("mcrt1z"));
        QTRY_VERIFY_WITH_TIMEOUT(wallet->IsLocked(), 10000);
        const auto address{session.stats().destination};
        QTRY_COMPARE(page.findChild<QLabel*>("miningDestination")->text(), QString::fromStdString(address));
        QCoreApplication::processEvents();
        QVERIFY(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        page.setPrivacy(true);
        QCOMPARE(page.findChild<QLabel*>("miningDestination")->text(), QString{"(hidden)"});
        QVERIFY(!page.findChild<QPushButton*>("inspectPoolManifest")->isEnabled());
        page.setPrivacy(false);
        QVERIFY(!mode->isEnabled());
        QVERIFY(!hardware->isEnabled());
        QVERIFY(!host->isEnabled());
        stop->click();
        QTRY_VERIFY_WITH_TIMEOUT(!session.stats().busy, 10000);
        QCOMPARE(session.stats().active_workers, 0U);
        QVERIFY(!session.poolStats().connected);
        QVERIFY(session.startPool(&model, 1, {"127.0.0.1", 1, {}}));
        QTRY_VERIFY_WITH_TIMEOUT(session.stats().destination == address, 10000);
        QCOMPARE(unlocks, 1U);
        QVERIFY(wallet->IsLocked());
        QVERIFY(wallet::RemoveWallet(context, wallet, std::nullopt));
        session.walletUnloaded(&model);
        QTRY_VERIFY_WITH_TIMEOUT(!session.stats().busy, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.owner(), 10000);
        QVERIFY(wallet::AddWallet(context, wallet));
        QVERIFY(session.startPool(&model, 1, {"127.0.0.1", 1, {}}));
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
