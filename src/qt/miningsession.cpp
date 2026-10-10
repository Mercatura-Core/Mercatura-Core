// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <qt/miningsession.h>

#include <chainparams.h>
#include <interfaces/handler.h>
#include <interfaces/mining.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <mining/solo.h>
#include <util/chaintype.h>
#include <util/translation.h>
#include <wallet/mining.h>

#include <QTimer>

#include <stdexcept>

MiningSession::MiningSession(interfaces::Node& node, QObject* parent)
    : QObject{parent}, m_node{node}, m_timer{new QTimer{this}}
{
    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, &MiningSession::poll);
    m_timer->start();
}
MiningSession::~MiningSession() { shutdown(); }

bool MiningSession::start(WalletModel* wallet, unsigned int workers)
{
    if (m_shutdown || !wallet || m_node.shutdownRequested()) return false;
    if (stats().busy) return false;
    const auto state{stats().state};
    if (state == mining::MiningState::RUNNING || state == mining::MiningState::STARTING || state == mining::MiningState::STOPPING) return false;
    m_owner = wallet;
    m_wallet_unloaded = false;
    m_requested = workers;
    m_needs_unlock = false;
    m_cancelled = false;
    m_unload_token = std::make_shared<std::atomic<bool>>(false);
    // Copy the public wallet name only. Obtain an independent wallet interface
    // in the coordinator so address derivation and DB writes never run on Qt.
    const auto name{wallet->wallet().getWalletName()};
    const bool started{m_controller.Start([this, name, cancelled = m_unload_token]() -> std::unique_ptr<mining::WorkProvider> {
        auto wallets{m_node.walletLoader().getWallets()};
        for (auto& selected : wallets) {
            if (selected->getWalletName() != name) continue;
            // A per-session token cancels work on Core unload without keeping
            // a raw Qt/controller pointer in a cross-thread notification. Old
            // notifications cannot cancel a later session.
            auto unload{selected->handleUnload([cancelled] { *cancelled = true; })};
            auto destination{wallet::GetMiningDestination(*selected)};
            if (!destination) {
                m_needs_unlock = !m_cancelled && !*cancelled && selected->isLocked();
                throw std::runtime_error{util::ErrorString(destination).original};
            }
            auto* context{m_node.context()};
            if (!context) throw std::runtime_error{"Native mining requires an in-process Core node"};
            return std::make_unique<mining::SoloWorkProvider>(
                interfaces::MakeMining(*context, /*wait_loaded=*/false), GetScriptForDestination(*destination),
                EncodeDestination(*destination), Params().GetChainType() == ChainType::REGTEST, std::move(unload), cancelled);
        }
        throw std::runtime_error{"The selected mining wallet is no longer loaded"};
    }, workers)};
    Q_EMIT changed();
    return started;
}

void MiningSession::stop()
{
    m_cancelled = true;
    m_needs_unlock = false;
    m_controller.RequestStop();
    Q_EMIT changed();
}
void MiningSession::walletUnloaded(WalletModel* wallet)
{
    if (m_owner != wallet) return;
    stop();
    m_wallet_unloaded = true;
    // No worker retains a wallet/model. The preparation stage temporarily owns
    // a separate interface; it is released before the workers receive any job.
    m_unlock_context.reset();
}
void MiningSession::shutdown()
{
    m_shutdown = true;
    m_timer->stop();
    stop();
    m_controller.Wait();
    m_unlock_context.reset();
    m_owner = nullptr;
    m_unload_token.reset();
}
void MiningSession::poll()
{
    if (m_unload_token && *m_unload_token) {
        stop();
        m_wallet_unloaded = true;
    }
    const auto current{stats()};
    if (m_wallet_unloaded && !current.busy) {
        m_owner = nullptr;
        m_unload_token.reset();
    }
    if (!m_prompting && !m_shutdown && !m_cancelled && !m_wallet_unloaded && current.state == mining::MiningState::FAILED && !current.busy && m_needs_unlock.exchange(false) && m_owner) {
        // Only address creation needs unlocking. A cached public destination
        // never enters this path, so a locked wallet can keep mining safely.
        m_prompting = true;
        QPointer<WalletModel> wallet{m_owner};
        // UnlockContext is deliberately non-movable; direct initialization
        // uses guaranteed copy elision to retain it until preparation finishes.
        auto context{std::unique_ptr<WalletModel::UnlockContext>{new WalletModel::UnlockContext{wallet->requestUnlock()}}};
        if (!m_shutdown && !m_cancelled && !m_wallet_unloaded && wallet && wallet == m_owner && context->isValid()) {
            m_unlock_context = std::move(context);
            if (!start(wallet, m_requested)) m_unlock_context.reset();
        }
        m_prompting = false;
    } else if (!m_prompting && (!current.destination.empty() || current.state == mining::MiningState::STOPPED || current.state == mining::MiningState::FAILED)) {
        m_unlock_context.reset();
    }
    Q_EMIT changed();
}
