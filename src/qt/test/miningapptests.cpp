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
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>

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
    const auto integer = [](const char* name, int fallback, int minimum, int maximum) {
        const auto text{qEnvironmentVariable(name)};
        if (text.isEmpty()) return fallback;
        bool ok{false};
        const int value{text.toInt(&ok)};
        if (!ok || value < minimum || value > maximum) throw std::runtime_error{std::string{"Invalid test option: "} + name};
        return value;
    };
    const unsigned int workers{static_cast<unsigned>(integer("MERCATURA_TEST_WORKERS", 1, 1, std::numeric_limits<int>::max()))};
    const int activation{integer("MERCATURA_TEST_ACTIVATION", 0, -1, 20)};
    if (activation != -1 && activation != 0 && activation != 20) return 1;
    const int seconds{integer("MERCATURA_TEST_SECONDS", 2, 2, 3600)};
    const bool harder{qEnvironmentVariableIsSet("MERCATURA_TEST_HARDER")};
    const bool stop_first{qEnvironmentVariableIsSet("MERCATURA_TEST_STOP_FIRST")};
    const auto mode{qEnvironmentVariable("MERCATURA_TEST_MODE", "cpu")};
    if (mode != "cpu" && mode != "gpu" && mode != "combined") return 1;
    if (harder && activation != 0) return 1;
    const auto raw{mining::DetectWorkerLimits()};
    const auto limits{mining::CalculateWorkerLimits(raw.logical_cpus, raw.available_memory,
        activation == 0 ? mercahash::Version::V2 : mercahash::Version::V1)};
    if (mode != "gpu" && workers > limits.maximum) { qWarning("Requested CPU worker budget unavailable"); return 77; }
    fs::path root;
    {
        BasicTestingSetup dummy{ChainType::REGTEST, {.extra_args={"-mercahashv2height=0"}}};
        root = dummy.m_path_root;
    }
    const auto supplied_dir{qEnvironmentVariable("MERCATURA_TEST_DATADIR")};
    const bool restart{qEnvironmentVariableIsSet("MERCATURA_TEST_RESTART")};
    SetupServerArgs(gArgs);
    gArgs.ForceSetArg("-regtest", "1");
    gArgs.ForceSetArg("-mercahashv2height", std::to_string(activation));
    if (harder) gArgs.ForceSetArg("-regtestminingstress", "1");
    SelectParams(ChainType::REGTEST);
    if (!supplied_dir.isEmpty()) gArgs.ForceSetArg("-datadir", supplied_dir.toStdString());
    gArgs.ClearPathCache();
    fs::create_directories(gArgs.GetDataDirNet() / "blocks");
    auto init{interfaces::MakeGuiInit(argc, argv)};
    for (const auto* option : {"-listen", "-listenonion", "-discover", "-dnsseed", "-fixedseeds", "-natpmp"})
        gArgs.ForceSetArg(option, "0");
    const int peer_port{integer("MERCATURA_TEST_PEER_PORT", 0, 0, 65535)};
    gArgs.ForceSetArg("-connect", peer_port ? "127.0.0.1:" + std::to_string(peer_port) : "0");
    const int rpc_port{integer("MERCATURA_TEST_RPC_PORT", 0, 0, 65535)};
    gArgs.ForceSetArg("-server", rpc_port ? "1" : "0");
    if (rpc_port) {
        gArgs.ForceSetArg("-rpcport", std::to_string(rpc_port));
        gArgs.ForceSetArg("-rpcbind", "127.0.0.1");
        gArgs.ForceSetArg("-rpcallowip", "127.0.0.1");
    }
    std::string config_error;
    if (!gArgs.ReadConfigFiles(config_error, true)) {
        qCritical() << QString::fromStdString(config_error);
        return 1;
    }
    QCoreApplication::setOrganizationName("Mercatura-mining-test");
    QCoreApplication::setApplicationName("Mercatura-close-test");
    QFile record_file{qEnvironmentVariable("MERCATURA_TEST_RECORDS")};
    if (!record_file.fileName().isEmpty() && !record_file.open(QIODevice::WriteOnly | QIODevice::Append)) return 1;
    const auto record = [&](QJsonObject value) {
        if (record_file.isOpen()) {
            record_file.write(QJsonDocument{value}.toJson(QJsonDocument::Compact) + "\n");
            record_file.flush();
        }
    };
    bool started{false}, closed{false}, destroyed{false}, qualified{false};
    bool stop_observed{false};
    int stage{0};
    qint64 stop_due{0}, stop_request{0}, stop_handler{-1}, stop_complete{-1}, stop_dispatch{-1};
    uint64_t qualification_cpu{0}, qualification_gpu{0};
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
        qint64 last_tick{clock.elapsed()}, mining_at{0}, close_at{0}, last_record{-500};
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
                if (restart && app.node().getNumBlocks() < (harder ? 1 : 3)) {
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
                auto* stop{page->findChild<QPushButton*>("stopMining")};
                auto* hardware{page->findChild<QComboBox*>("miningHardware")};
                if (!start || !selection || !stop || !hardware) { app.requestShutdown(); return; }
                hardware->setCurrentIndex(mode == "gpu" ? 1 : mode == "combined" ? 2 : 0);
                selection->setValue(workers);
                QPointer<MiningSession> session{controller->miningSession()};
                poll.setInterval(25);
                QObject::connect(&poll, &QTimer::timeout, &app, [&, window, page, start, stop, selection, session] {
                    if (!started) {
                        const auto requested_device{qEnvironmentVariable("MERCATURA_TEST_GPU_DEVICE")};
                        if (mode != "cpu" && !requested_device.isEmpty()) {
                            auto* devices{page->findChild<QListWidget*>("gpuDevices")};
                            auto* all{page->findChild<QCheckBox*>("automaticGpuDevices")};
                            if (!devices || !all || devices->count() == 0) return;
                            all->setChecked(false);
                            bool found{false};
                            for (int i{0}; i < devices->count(); ++i) {
                                auto* item{devices->item(i)};
                                const bool selected{item->data(Qt::UserRole).toString() == requested_device};
                                item->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
                                found |= selected && bool(item->flags() & Qt::ItemIsEnabled);
                            }
                            if (!found) { qCritical("Requested GPU is unavailable"); app.requestShutdown(); return; }
                        }
                        const int contexts{integer("MERCATURA_TEST_GPU_CONTEXTS", 0, 0, std::numeric_limits<int>::max())};
                        if (mode != "cpu" && contexts) {
                            page->findChild<QComboBox*>("gpuContextMode")->setCurrentIndex(1);
                            if (activation == 0) page->findChild<QDoubleSpinBox*>("gpuContextsV2")->setValue(contexts);
                            else page->findChild<QSpinBox*>("gpuContexts")->setValue(contexts);
                            const double selected{activation == 0 ? page->findChild<QDoubleSpinBox*>("gpuContextsV2")->value() :
                                page->findChild<QSpinBox*>("gpuContexts")->value()};
                            if (selected != contexts) {
                                qCritical("Manual GPU context selection exceeds the resource budget"); app.requestShutdown(); return;
                            }
                        }
                        if (!start->isEnabled()) return;
                        if (mode != "gpu" && selection->value() != static_cast<int>(workers)) {
                            qCritical("CPU worker selection was resource-clamped"); app.requestShutdown(); return;
                        }
                        started = true;
                        mining_at = clock.elapsed();
                        heartbeat.start();
                        last_tick = clock.elapsed();
                        start->click();
                    }
                    const auto stats{session->stats()};
                    uint64_t gpu_hashes{0}, batches_started{0};
                    bool device_failed{false};
                    QJsonArray devices;
                    for (const auto& device : stats.devices) {
                        gpu_hashes += device.hashes;
                        batches_started += device.batches_started;
                        device_failed |= device.failed;
                        devices.append(QJsonObject{{"id", QString::fromStdString(device.device.id)},
                            {"hashes", QString::number(device.hashes)}, {"rate", device.hashes_per_second},
                            {"contexts", double(device.contexts)}, {"executing", double(device.active_contexts)},
                            {"memory_bytes", QString::number(device.memory_bytes)},
                            {"batches_started", QString::number(device.batches_started)},
                            {"batches_completed", QString::number(device.batches_completed)},
                            {"batches_cancelled", QString::number(device.batches_cancelled)}, {"failed", device.failed}});
                    }
                    if (clock.elapsed() - last_record >= 500) {
                        last_record = clock.elapsed();
                        record({{"event", "sample"}, {"elapsed_ms", double(clock.elapsed())}, {"stage", stage},
                            {"heartbeat_gap_ms", double(worst_heartbeat)}, {"busy", stats.busy},
                            {"accepted", QString::number(stats.accepted)}, {"rejected", QString::number(stats.rejected)},
                            {"stale", QString::number(stats.stale)}, {"cpu_hashes", QString::number(stats.cpu_hashes)},
                            {"cpu_rate", stats.cpu_hashes_per_second}, {"job_height", double(stats.job_height)},
                            {"pow_version", int(stats.pow_version)}, {"devices", devices}});
                    }
                    if (stats.state == mining::MiningState::FAILED || device_failed || stats.rejected > 0) {
                        qCritical() << QString::fromStdString(stats.status);
                        poll.stop(); app.requestShutdown(); return;
                    }
                    if (stage == 1) return; // Waiting for the scheduled real Stop click.
                    if (stage == 2) {
                        if (stats.busy) return;
                        if (!stop_observed) {
                            stop_complete = clock.elapsed() - stop_request;
                            stop_observed = true;
                            record({{"event", "stop"}, {"dispatch_ms", double(stop_dispatch)},
                                {"handler_ms", double(stop_handler)}, {"complete_ms", double(stop_complete)},
                                {"active_workers", int(stats.active_workers)}});
                        }
                        if (!start->isEnabled()) return;
                        start->click();
                        mining_at = clock.elapsed();
                        stage = 3;
                        return;
                    }
                    if (stage == 3) {
                        if (clock.elapsed() - mining_at < 2000 || stats.active_workers == 0 || stats.hashes == 0) return;
                    } else {
                        const bool progress{mode == "cpu" ? stats.cpu_hashes > 0 : mode == "gpu" ? gpu_hashes > 0 :
                            stats.cpu_hashes > 0 && (harder ? gpu_hashes > 0 : batches_started > 0)};
                        if (activation == 20 && (stats.pow_version != mercahash::Version::V2 || stats.job_height < 20)) return;
                        if (stats.accepted < (harder ? 1U : 3U) || clock.elapsed() - mining_at < int64_t{seconds} * 1000 || !progress) return;
                        qualified = true;
                        accepted = stats.accepted;
                        qualification_cpu = stats.cpu_hashes;
                        qualification_gpu = gpu_hashes;
                        if (stop_first) {
                            stage = 1;
                            stop_due = clock.elapsed() + 100;
                            QTimer::singleShot(100, &app, [&, stop] {
                                stop_request = clock.elapsed();
                                stop_dispatch = stop_request - stop_due;
                                stop->click();
                                stop_handler = clock.elapsed() - stop_request;
                                stage = 2;
                            });
                            return;
                        }
                    }
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
        result = exit_code == 0 && started && closed && destroyed && qualified &&
            (!stop_first || (stop_observed && stop_dispatch < 1000 && stop_handler < 1000 && stop_complete < 1000)) &&
            close_handler >= 0 && close_handler < 1000 && close_complete < 5000 && worst_heartbeat < 1000 ? 0 : 1;
        record({{"event", "result"}, {"result", result}, {"activation", activation}, {"mode", mode}, {"harder", harder},
            {"workers", int(workers)}, {"accepted", QString::number(accepted)},
            {"cpu_hashes", QString::number(qualification_cpu)}, {"gpu_hashes", QString::number(qualification_gpu)},
            {"stop_dispatch_ms", double(stop_dispatch)}, {"stop_handler_ms", double(stop_handler)}, {"stop_complete_ms", double(stop_complete)},
            {"close_handler_ms", double(close_handler)}, {"close_complete_ms", double(close_complete)},
            {"heartbeat_gap_ms", double(worst_heartbeat)}, {"session_destroyed", destroyed}});
        qInfo().nospace() << "Native application Close activation=" << activation << " mode=" << mode << " workers=" << workers << " accepted=" << accepted
            << " handler_ms=" << close_handler << " complete_ms=" << close_complete
            << " heartbeat_gap_ms=" << worst_heartbeat << " session_destroyed=" << destroyed << " result=" << result;
    }
    if (supplied_dir.isEmpty()) fs::remove_all(root);
    return result;
}
