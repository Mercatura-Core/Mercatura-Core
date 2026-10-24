// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <chainparams.h>
#include <consensus/consensus.h>
#include <init.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <qt/bitcoin.h>
#include <qt/bitcoingui.h>
#include <qt/miningpage.h>
#include <qt/miningsession.h>
#include <qt/transactionrecord.h>
#include <qt/transactiontablemodel.h>
#include <qt/networkstyle.h>
#include <qt/walletcontroller.h>
#include <qt/walletmodel.h>
#include <qt/walletview.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/string.h>
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
#include <QTableView>

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <variant>

const std::function<void(const std::string&)> G_TEST_LOG_FUN{};
const std::function<std::vector<const char*>()> G_TEST_COMMAND_LINE_ARGUMENTS{};
const std::function<std::string()> G_TEST_GET_FULL_NAME{};

// Called only before mining or after Stop. Reconstruct owned unspent outputs
// from the actual wallet transactions, independently of the cached Qt balance.
static bool CheckWalletView(WalletModel& model, interfaces::Node& node, BitcoinGUI& window, QJsonObject& details)
{
    const auto fail = [&](const char* reason) { details = {{"reason", reason}}; return false; };
    auto* table{model.getTransactionTableModel()};
    if (table->pendingNotificationCount()) return fail("transaction notifications pending");
    interfaces::WalletBalances balances, expected;
    uint256 tip;
    if (!model.wallet().tryGetBalances(balances, tip)) return fail("wallet busy");
    if (tip != node.getBestBlockHash()) return fail("wallet has not caught up with the chain");
    const int height{node.getNumBlocks()};
    const auto transactions{model.wallet().getWalletTxs()};
    struct Output { CAmount value; bool immature; };
    std::map<COutPoint, Output> outputs;
    std::map<Txid, CAmount> expected_net, model_net;
    std::map<Txid, int> expected_rows, model_rows;
    std::map<Txid, int> expected_depth;
    std::set<Txid> immature_rewards;
    int coinbases{0};
    for (const auto& tx : transactions) {
        interfaces::WalletTxStatus status;
        uint256 status_tip;
        if (!model.wallet().tryGetTxStatus(tx.tx->GetHash(), status, status_tip)) return fail("transaction status busy");
        if (status_tip != tip) return fail("wallet tip changed during verification");
        if (status.depth_in_main_chain < 1) return fail("unconfirmed wallet transaction");
        coinbases += tx.is_coinbase;
        expected_depth.emplace(tx.tx->GetHash(), height - status.block_height + 1);
        if (tx.is_coinbase && height - status.block_height < COINBASE_MATURITY) immature_rewards.insert(tx.tx->GetHash());
        CAmount owned_credit{0};
        for (size_t i{0}; i < tx.tx->vout.size(); ++i) {
            if (tx.txout_is_mine[i]) {
                outputs.emplace(COutPoint{tx.tx->GetHash(), static_cast<uint32_t>(i)},
                    Output{tx.tx->vout[i].nValue, tx.is_coinbase && height - status.block_height < COINBASE_MATURITY});
                owned_credit += tx.tx->vout[i].nValue;
            }
        }
        expected_net.emplace(tx.tx->GetHash(), owned_credit);
        // Each reward has one owned output. The optional real self-payment
        // has one sent and one received entry, with change omitted by the UI.
        expected_rows.emplace(tx.tx->GetHash(), tx.is_coinbase ? 1 : 2);
    }
    if (coinbases != height) return fail("wallet is missing mined rewards");
    for (const auto& tx : transactions) for (const auto& input : tx.tx->vin) {
        if (const auto it{outputs.find(input.prevout)}; it != outputs.end()) {
            expected_net.at(tx.tx->GetHash()) -= it->second.value;
            outputs.erase(it);
        }
    }
    for (const auto& [outpoint, output] : outputs) {
        if (output.immature) expected.immature_balance += output.value;
        else expected.balance += output.value;
    }
    if (balances.balanceChanged(expected)) return fail("Core balance differs from reconstructed UTXOs");
    if (model.getCachedBalance().balanceChanged(expected)) return fail("Qt balance has not caught up");
    std::set<std::tuple<Txid, int, int>> entries;
    std::optional<std::pair<Txid, int>> previous;
    for (int row{0}; row < table->rowCount({}); ++row) {
        const auto index{table->index(row, 0)};
        const auto* entry{static_cast<const TransactionRecord*>(index.internalPointer())};
        if (!entry || !expected_net.contains(entry->hash)) return fail("unknown transaction model entry");
        if (entry->status.depth != expected_depth.at(entry->hash)) return fail("Qt transaction confirmations are stale");
        if (entry->type == TransactionRecord::Generated &&
            (entry->status.status == TransactionStatus::Immature) != immature_rewards.contains(entry->hash))
            return fail("Qt reward maturity is stale");
        if (!entries.emplace(entry->hash, entry->idx, entry->type).second) return fail("duplicate transaction model entry");
        const std::pair key{entry->hash, entry->idx};
        if (previous && key < *previous) return fail("transaction model order is incorrect");
        previous = key;
        ++model_rows[entry->hash];
        model_net[entry->hash] += table->data(index, TransactionTableModel::AmountRole).toLongLong();
    }
    if (model_rows != expected_rows) return fail("transaction model entry counts differ");
    if (model_net != expected_net) return fail("transaction model amounts differ from raw transactions");
    QTableView* view{nullptr};
    for (auto* candidate : window.findChildren<WalletView*>()) {
        if (candidate->getWalletModel() == &model) view = candidate->findChild<QTableView*>("transactionView");
    }
    if (!view || view->model()->rowCount() != table->rowCount({})) return fail("history view is missing rows");
    QString previous_date;
    for (int row{0}; row < view->model()->rowCount(); ++row) {
        const auto date{view->model()->index(row, TransactionTableModel::Date).data(Qt::EditRole).toString()};
        if (row && previous_date < date) return fail("history view is incorrectly sorted");
        previous_date = date;
    }
    details = {{"height", height}, {"wallet_transactions", int(transactions.size())}, {"model_rows", table->rowCount({})},
        {"unspent_outputs", int(outputs.size())}, {"mature_base_units", QString::number(expected.balance)},
        {"immature_base_units", QString::number(expected.immature_balance)}, {"unique_entries", true},
        {"history_ordered", true}, {"balances_match_utxos", true}, {"confirmations_match", true}, {"maturity_matches", true}};
    return true;
}

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
    const bool rapid{qEnvironmentVariableIsSet("MERCATURA_TEST_RAPID_WALLET")};
    const auto mode{qEnvironmentVariable("MERCATURA_TEST_MODE", "cpu")};
    if (mode != "cpu" && mode != "gpu" && mode != "combined") return 1;
    if (harder && activation != 0) return 1;
    if (rapid && (activation != 0 || harder || mode != "cpu" || !stop_first)) return 1;
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
    gArgs.ForceSetArg("-mercahashv2height", util::ToString(activation));
    if (harder) gArgs.ForceSetArg("-regtestminingstress", "1");
    SelectParams(ChainType::REGTEST);
    if (!supplied_dir.isEmpty()) gArgs.ForceSetArg("-datadir", supplied_dir.toStdString());
    gArgs.ClearPathCache();
    fs::create_directories(gArgs.GetDataDirNet() / "blocks");
    auto init{interfaces::MakeGuiInit(argc, argv)};
    for (const auto* option : {"-listen", "-listenonion", "-discover", "-dnsseed", "-fixedseeds", "-natpmp"})
        gArgs.ForceSetArg(option, "0");
    const int peer_port{integer("MERCATURA_TEST_PEER_PORT", 0, 0, 65535)};
    gArgs.ForceSetArg("-connect", peer_port ? "127.0.0.1:" + util::ToString(peer_port) : "0");
    const int rpc_port{integer("MERCATURA_TEST_RPC_PORT", 0, 0, 65535)};
    gArgs.ForceSetArg("-server", rpc_port ? "1" : "0");
    if (rpc_port) {
        gArgs.ForceSetArg("-rpcport", util::ToString(rpc_port));
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
    bool wallet_verified{false}, reopen_verified{false};
    size_t stop_backlog{0}, close_backlog{0};
    QPointer<WalletModel> selected_wallet;
    std::weak_ptr<wallet::CWallet> core_wallet;
    QString self_payment;
    qint64 verification_deadline{0};
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
                        selected_wallet = controller->getOrCreateWallet(std::move(existing));
                        loaded = true;
                        break;
                    }
                }
                if (!loaded) {
                    auto wallet{restart ? app.node().walletLoader().loadWallet("v2-close", warnings) :
                        app.node().walletLoader().createWallet("v2-close", {}, wallet::WALLET_FLAG_DESCRIPTORS, warnings)};
                    if (!wallet) { qCritical("Cannot open test wallet"); app.requestShutdown(); return; }
                    selected_wallet = controller->getOrCreateWallet(std::move(*wallet));
                }
                if (rapid) {
                    core_wallet = wallet::GetWallet(*app.node().walletLoader().context(), "v2-close");
                    if (core_wallet.expired() || !selected_wallet) { app.requestShutdown(); return; }
                    verification_deadline = clock.elapsed() + 5000;
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
                        if (rapid && restart && !reopen_verified) {
                            QJsonObject details;
                            if (!CheckWalletView(*selected_wallet, app.node(), *window, details)) {
                                if (clock.elapsed() > verification_deadline) {
                                    qCritical() << "Reopened wallet state mismatch" << details;
                                    app.requestShutdown();
                                }
                                return;
                            }
                            details.insert("event", "wallet_reopened");
                            record(details);
                            reopen_verified = true;
                        }
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
                            {"wallet_notifications_pending", double(selected_wallet ? selected_wallet->getTransactionTableModel()->pendingNotificationCount() : 0)},
                            {"cpu_rate", stats.cpu_hashes_per_second}, {"job_height", double(stats.job_height)},
                            {"pow_version", int(stats.pow_version)}, {"devices", devices}});
                    }
                    if (stats.state == mining::MiningState::FAILED || device_failed || stats.rejected > 0) {
                        qCritical() << QString::fromStdString(stats.status);
                        poll.stop(); app.requestShutdown(); return;
                    }
                    const auto schedule_stop = [&] {
                        stage = 1;
                        stop_due = clock.elapsed() + 100;
                        QTimer::singleShot(100, &app, [&, stop] {
                            stop_request = clock.elapsed();
                            stop_dispatch = stop_request - stop_due;
                            stop->click();
                            stop_handler = clock.elapsed() - stop_request;
                            stage = 2;
                        });
                    };
                    if (stage == 4) {
                        stop_backlog = selected_wallet->getTransactionTableModel()->pendingNotificationCount();
                        if (!stop_backlog) {
                            if (clock.elapsed() > verification_deadline) {
                                qCritical("No actual transaction backlog before Stop"); app.requestShutdown();
                            }
                            return;
                        }
                        schedule_stop();
                        return;
                    }
                    if (stage == 5) {
                        QJsonObject details;
                        if (!CheckWalletView(*selected_wallet, app.node(), *window, details)) {
                            if (clock.elapsed() > verification_deadline) {
                                qCritical() << "Wallet state mismatch after rapid mining" << details;
                                app.requestShutdown();
                            }
                            return;
                        }
                        details.insert("event", "wallet_verified");
                        record(details);
                        wallet_verified = true;
                        // Exercise a genuine PQ-authorized self-payment, so
                        // the notification regression also covers debit and
                        // credit entries sharing a transaction/output index.
                        try {
                            const auto address{app.node().executeRpc("getnewaddress", UniValue{UniValue::VARR}, "/wallet/v2-close").get_str()};
                            if (!std::holds_alternative<WitnessV2MercaturaPQ>(DecodeDestination(address)))
                                throw std::runtime_error{"Self-payment address is not native PQ"};
                            UniValue payment{UniValue::VOBJ};
                            payment.pushKV("address", address);
                            payment.pushKV("amount", UniValue{UniValue::VNUM, "1"});
                            payment.pushKV("fee_rate", UniValue{UniValue::VNUM, "1"});
                            self_payment = QString::fromStdString(app.node().executeRpc("sendtoaddress", payment, "/wallet/v2-close").get_str());
                            record({{"event", "pq_self_payment"}, {"txid", self_payment}});
                        } catch (const std::exception& exception) {
                            qCritical() << "PQ self-payment failed:" << exception.what(); app.requestShutdown(); return;
                        }
                        stage = 6;
                        verification_deadline = clock.elapsed() + 5000;
                        return;
                    }
                    if (stage == 6) {
                        auto* table{selected_wallet->getTransactionTableModel()};
                        int rows{0}, sent{0}, received{0};
                        for (int row{0}; row < table->rowCount({}); ++row) {
                            const auto index{table->index(row, 0)};
                            if (index.data(TransactionTableModel::TxHashRole).toString() != self_payment) continue;
                            ++rows;
                            sent += index.data(TransactionTableModel::TypeRole).toInt() == TransactionRecord::SendToAddress;
                            received += index.data(TransactionTableModel::TypeRole).toInt() == TransactionRecord::RecvWithAddress;
                        }
                        if (table->pendingNotificationCount() || rows != 2 || sent != 1 || received != 1 || !start->isEnabled()) {
                            if (clock.elapsed() > verification_deadline) {
                                qCritical("PQ self-payment history did not appear exactly twice"); app.requestShutdown();
                            }
                            return;
                        }
                        record({{"event", "self_payment_visible"}, {"txid", self_payment}, {"entries", rows}});
                        start->click();
                        mining_at = clock.elapsed();
                        stage = 3;
                        return;
                    }
                    if (stage == 1) return; // Waiting for the scheduled real Stop click.
                    if (stage == 2) {
                        if (stats.busy) return;
                        if (!stop_observed) {
                            stop_complete = clock.elapsed() - stop_request;
                            stop_observed = true;
                            record({{"event", "stop"}, {"dispatch_ms", double(stop_dispatch)},
                                {"handler_ms", double(stop_handler)}, {"complete_ms", double(stop_complete)},
                                {"active_workers", int(stats.active_workers)}, {"pending_notifications", double(stop_backlog)}});
                            if (stats.active_workers) { qCritical("Stop left mining workers active"); app.requestShutdown(); return; }
                        }
                        if (rapid) {
                            if (const auto wallet{core_wallet.lock()}) wallet->ShowProgress("Rapid wallet notification regression", 100);
                            else { app.requestShutdown(); return; }
                            stage = 5;
                            verification_deadline = clock.elapsed() + 5000;
                            return;
                        }
                        if (!start->isEnabled()) return;
                        start->click();
                        mining_at = clock.elapsed();
                        stage = 3;
                        return;
                    }
                    if (stage == 7) {
                        close_backlog = selected_wallet->getTransactionTableModel()->pendingNotificationCount();
                        if (!close_backlog) {
                            if (clock.elapsed() > verification_deadline) {
                                qCritical("No actual transaction backlog before Close"); app.requestShutdown();
                            }
                            return;
                        }
                        record({{"event", "close_backlog"}, {"pending_notifications", double(close_backlog)}});
                    } else if (stage == 3) {
                        if (clock.elapsed() - mining_at < 2000 || stats.active_workers == 0 || stats.hashes == 0) return;
                        if (rapid) {
                            // Use the existing rescan notification queue to
                            // make the Close backlog deterministic. The normal
                            // 30-second mining phase above was never paused.
                            if (const auto wallet{core_wallet.lock()}) wallet->ShowProgress("Rapid wallet notification regression", 1);
                            else { app.requestShutdown(); return; }
                            stage = 7;
                            verification_deadline = clock.elapsed() + 5000;
                            return;
                        }
                    } else {
                        const bool progress{mode == "cpu" ? stats.cpu_hashes > 0 : mode == "gpu" ? gpu_hashes > 0 :
                            stats.cpu_hashes > 0 && (harder ? gpu_hashes > 0 : batches_started > 0)};
                        if (activation == 20 && (stats.pow_version != mercahash::Version::V2 || stats.job_height < 20)) return;
                        if (stats.accepted < (rapid ? 300U : harder ? 1U : 3U) || clock.elapsed() - mining_at < int64_t{seconds} * 1000 || !progress) return;
                        qualified = true;
                        accepted = stats.accepted;
                        qualification_cpu = stats.cpu_hashes;
                        qualification_gpu = gpu_hashes;
                        if (stop_first) {
                            if (rapid) {
                                if (const auto wallet{core_wallet.lock()}) wallet->ShowProgress("Rapid wallet notification regression", 1);
                                else { app.requestShutdown(); return; }
                                stage = 4;
                                verification_deadline = clock.elapsed() + 5000;
                            } else schedule_stop();
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
            (!rapid || (wallet_verified && (!restart || reopen_verified) && stop_backlog > 0 && close_backlog > 0 && !self_payment.isEmpty())) &&
            (!stop_first || (stop_observed && stop_dispatch < 1000 && stop_handler < 1000 && stop_complete < 1000)) &&
            close_handler >= 0 && close_handler < 1000 && close_complete < 5000 && worst_heartbeat < 1000 ? 0 : 1;
        record({{"event", "result"}, {"result", result}, {"activation", activation}, {"mode", mode}, {"harder", harder},
            {"workers", int(workers)}, {"accepted", QString::number(accepted)},
            {"rapid_wallet", rapid}, {"wallet_verified", wallet_verified}, {"wallet_reopened", reopen_verified},
            {"stop_backlog", double(stop_backlog)}, {"close_backlog", double(close_backlog)},
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
