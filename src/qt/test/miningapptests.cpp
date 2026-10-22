// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <chainparams.h>
#include <init.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <qt/bitcoin.h>
#include <qt/bitcoingui.h>
#include <qt/miningpage.h>
#include <qt/miningsession.h>
#include <qt/networkstyle.h>
#include <qt/walletcontroller.h>
#include <test/util/setup_common.h>
#include <wallet/wallet.h>

#include <QElapsedTimer>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>

#include <algorithm>
#include <functional>

const std::function<void(const std::string&)> G_TEST_LOG_FUN{};
const std::function<std::vector<const char*>()> G_TEST_COMMAND_LINE_ARGUMENTS{};
const std::function<std::string()> G_TEST_GET_FULL_NAME{};

// This is the actual application initialization and normal window Close path,
// with a disk wallet, native SoloWorkProvider and consensus-validated V2 blocks.
// CTest's external timeout also catches a blocked Qt event loop.
int main(int argc, char* argv[])
{
#ifdef Q_OS_MACOS
    // macOS Close intentionally keeps the application alive; this test checks
    // the Linux/Windows Close-to-shutdown path.
    return 77;
#endif
    const unsigned int workers{qEnvironmentVariableIntValue("MERCATURA_TEST_WORKERS") == 2 ? 2U : 1U};
    if (workers > mining::DetectWorkerLimits().maximum) return 77;
    fs::path root;
    {
        BasicTestingSetup dummy{ChainType::REGTEST, {.extra_args={"-mercahashv2height=0"}}};
        root = dummy.m_path_root;
    }
    const auto supplied_dir{qEnvironmentVariable("MERCATURA_TEST_DATADIR")};
    const bool restart{qEnvironmentVariableIsSet("MERCATURA_TEST_RESTART")};
    SetupServerArgs(gArgs);
    gArgs.ForceSetArg("-regtest", "1");
    gArgs.ForceSetArg("-mercahashv2height", "0");
    if (!supplied_dir.isEmpty()) gArgs.ForceSetArg("-datadir", supplied_dir.toStdString());
    gArgs.ClearPathCache();
    fs::create_directories(gArgs.GetDataDirNet() / "blocks");
    auto init{interfaces::MakeGuiInit(argc, argv)};
    for (const auto* option : {"-listen", "-listenonion", "-discover", "-dnsseed", "-fixedseeds", "-natpmp"})
        gArgs.ForceSetArg(option, "0");
    gArgs.ForceSetArg("-connect", "0");
    gArgs.ForceSetArg("-server", "0");
    std::string config_error;
    if (!gArgs.ReadConfigFiles(config_error, true)) {
        qCritical() << QString::fromStdString(config_error);
        return 1;
    }
    QCoreApplication::setOrganizationName("Mercatura-mining-test");
    QCoreApplication::setApplicationName("Mercatura-close-test");
    bool started{false}, closed{false}, destroyed{false};
    qint64 close_handler{-1}, close_complete{-1}, worst_heartbeat{0};
    uint64_t accepted{0};
    int result{1};
    {
        BitcoinApplication app;
        app.createNode(*init);
        app.parameterSetup();
        if (!app.createOptionsModel(true)) return 1;
        const std::unique_ptr<const NetworkStyle> style{NetworkStyle::instantiate(ChainType::REGTEST)};
        app.setupPlatformStyle();
        app.createWindow(style.get());
        QElapsedTimer clock;
        clock.start();
        qint64 last_tick{clock.elapsed()}, mining_at{0}, close_at{0};
        QTimer heartbeat, poll;
        heartbeat.setInterval(10);
        QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
            const auto now{clock.elapsed()};
            worst_heartbeat = std::max(worst_heartbeat, now - last_tick);
            last_tick = now;
        });
        QObject::connect(&app, &BitcoinApplication::windowShown, &app, [&](BitcoinGUI* window) {
            // Await startup wallet-model loading before preparing this test's
            // wallet, so no background controller lock waits for the GUI here.
            const auto prepare = [&, window] {
                auto* controller{window->getWalletController()};
                if (!controller) { app.requestShutdown(); return; }
                std::vector<bilingual_str> warnings;
                if (restart && app.node().getNumBlocks() < 3) {
                    qCritical("Restart lost the previously mined V2 chain"); app.requestShutdown(); return;
                }
                bool loaded{false};
                for (auto& existing : app.node().walletLoader().getWallets()) {
                    if (existing->getWalletName() == "v2-close") {
                        controller->getOrCreateWallet(std::move(existing));
                        loaded = true;
                        break;
                    }
                }
                if (!loaded) {
                    auto wallet{restart ? app.node().walletLoader().loadWallet("v2-close", warnings) :
                        app.node().walletLoader().createWallet("v2-close", {}, wallet::WALLET_FLAG_DESCRIPTORS, warnings)};
                    if (!wallet) { qCritical("Cannot open test wallet"); app.requestShutdown(); return; }
                    controller->getOrCreateWallet(std::move(*wallet));
                }
                auto* page{window->findChild<MiningPage*>()};
                if (!page) { qCritical("Mining page missing"); app.requestShutdown(); return; }
                auto* start{page->findChild<QPushButton*>("startMining")};
                auto* selection{page->findChild<QSpinBox*>("manualWorkers")};
                if (!start || !selection) { app.requestShutdown(); return; }
                selection->setValue(workers);
                QPointer<MiningSession> session{controller->miningSession()};
                poll.setInterval(25);
                QObject::connect(&poll, &QTimer::timeout, &app, [&, window, start, session] {
                    if (!started) {
                        if (!start->isEnabled()) return;
                        started = true;
                        mining_at = clock.elapsed();
                        heartbeat.start();
                        last_tick = clock.elapsed();
                        start->click();
                    }
                    const auto stats{session->stats()};
                    if (stats.state == mining::MiningState::FAILED) {
                        qCritical() << QString::fromStdString(stats.status);
                        poll.stop(); app.requestShutdown(); return;
                    }
                    if (stats.accepted < 3 || clock.elapsed() - mining_at < 2000) return;
                    accepted = stats.accepted;
                    poll.stop();
                    close_at = clock.elapsed();
                    window->close(); // BitcoinGUI::closeEvent -> normal application shutdown.
                    close_handler = clock.elapsed() - close_at;
                    closed = true;
                    destroyed = session.isNull(); // Wallet controller and joined session destroyed.
                });
                poll.start();
            };
            if (auto* loading{window->findChild<LoadWalletsActivity*>()})
                QObject::connect(loading, &WalletControllerActivity::finished, &app, prepare);
            else
                QTimer::singleShot(0, &app, prepare);
        });
        if (!app.baseInitialize()) return 1;
        app.requestInitialize();
        const auto exit_code{app.exec()};
        if (closed) close_complete = clock.elapsed() - close_at;
        heartbeat.stop();
        result = exit_code == 0 && started && closed && destroyed && accepted >= 3 &&
            close_handler >= 0 && close_handler < 1000 && close_complete < 5000 && worst_heartbeat < 1000 ? 0 : 1;
        qInfo().nospace() << "Native V2 application Close workers=" << workers << " accepted=" << accepted
            << " handler_ms=" << close_handler << " complete_ms=" << close_complete
            << " heartbeat_gap_ms=" << worst_heartbeat << " session_destroyed=" << destroyed << " result=" << result;
    }
    if (supplied_dir.isEmpty()) fs::remove_all(root);
    return result;
}
