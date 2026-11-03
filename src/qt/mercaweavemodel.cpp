// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <qt/mercaweavemodel.h>
#include <QCoreApplication>
#include <QThread>
#ifdef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#endif
namespace MercaWeaveQt {
Reason Availability(const Capabilities& c,Action action)
{
    switch(action){case Action::Inspect:case Action::IssueReceive:case Action::RotateReceive:case Action::SelectMining:case Action::Plan:case Action::Cancel:case Action::Preview:break;default:return Reason::Unavailable;}
    if(!c.loaded)return Reason::Unloaded;
    if(!c.compiled||!c.wallet)return Reason::Unavailable;
#ifndef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
    return Reason::Network; // No production setter, height, exception or bypass.
#else
    if(!c.isolated||!c.network)return Reason::Network;
    if(!c.optedIn)return Reason::OptIn;
    if(!c.metadata)return Reason::Metadata;
    if(action==Action::Inspect||action==Action::Cancel)return Reason::None;
    if(action==Action::IssueReceive&&c.locked&&c.inventory)return Reason::None;
    if(c.watchOnly)return Reason::WatchOnly;
    if(c.locked)return Reason::Locked;
    if((action==Action::Plan||action==Action::Preview)&&!c.signer)return Reason::Signer;
    if((action==Action::Plan||action==Action::Preview)&&!c.synchronized)return Reason::Sync;
    return Reason::None;
#endif
}
QString Explain(Reason reason)
{
    const auto tr=[](const char* text){return QCoreApplication::translate("MercaWeaveQt",text);};
    switch(reason){
    case Reason::None:return tr("Ready for isolated review; broadcasting is unavailable.");
    case Reason::Unavailable:return tr("Grouped wallet capability is unavailable. Ordinary PQ remains available.");
    case Reason::Network:return tr("Grouped PQ is not authorized on this network. Do not fund experimental v3 outputs.");
    case Reason::OptIn:return tr("Grouped PQ requires explicit opt-in.");
    case Reason::Locked:return tr("Wallet locked. No automatic unlock or new secret derivation.");
    case Reason::WatchOnly:return tr("Watch-only: explicit public inventory only; signing is unavailable.");
    case Reason::Inventory:return tr("No unused pre-generated public addresses are available.");
    case Reason::Metadata:return tr("Group metadata is missing or inconsistent. Restore the issued-range backup.");
    case Reason::Signer:return tr("Signing authorization is unavailable.");
    case Reason::Sync:return tr("Planning paused until the wallet is synchronized.");
    case Reason::Busy:return tr("A request is already in progress.");
    case Reason::Stale:return tr("State changed. Review a fresh proposal.");
    case Reason::Threshold:return tr("Not enough eligible mature rewards in one exact-key group.");
    case Reason::Reserved:return tr("Eligible inputs are reserved by another proposal.");
    case Reason::Destination:return tr("Choose a valid independent-key witness-v2 destination.");
    case Reason::Fee:return tr("Fees exceed the configured policy or maximum.");
    case Reason::Dust:return tr("The output would be below the dust floor.");
    case Reason::Funds:return tr("Eligible balance does not cover the output and fees.");
    case Reason::Limits:return tr("The proposal exceeds transaction or group limits.");
    case Reason::Failed:return tr("The proposal failed. No transaction was broadcast.");
    case Reason::Cancelled:return tr("Proposal cancelled; its reservations were released.");
    case Reason::Conflict:return tr("Proposal conflicted or was replaced. Review current coins.");
    case Reason::Unloaded:return tr("Wallet unloaded; pending results are discarded.");
    case Reason::Partial:return tr("Partially authorized PSBT; not ready for broadcast.");
    case Reason::Unsupported:return tr("This operation requires further wallet integration.");
    }
    return tr("Availability is unknown; operation blocked.");
}
QString Money(qint64 n)
{
    if(n<0)return QCoreApplication::translate("MercaWeaveQt","Unavailable");
    return QStringLiteral("%1.%2 MCA").arg(n/100).arg(n%100,2,10,QLatin1Char('0'));
}
Model::Model(QObject* parent):QObject(parent){setObjectName(QStringLiteral("mercaWeaveModel"));}
Model::~Model()
{
#ifdef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
    if(m_backend)m_backend->Invalidate(Reason::Unloaded);
#endif
}
void Model::observeNativeWallet(bool locked,bool watch)
{
    if(QThread::currentThread()!=thread())return;++m_generation;m_busy=false;
    m_state.capability.locked=locked;m_state.capability.watchOnly=watch;
    m_state.reason=Availability(m_state.capability,Action::Inspect);Q_EMIT changed();
}
void Model::nativePreview(Preview preview)
{
    if(QThread::currentThread()!=thread())return;++m_generation;m_busy=false;m_state.preview=std::move(preview);m_state.reason=Availability(m_state.capability,Action::Inspect);Q_EMIT changed();
}
void Model::invalidate(Reason reason)
{
    if(QThread::currentThread()!=thread())return;++m_generation;m_busy=false;m_busy=false;
#ifdef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
    if(m_backend)m_backend->Invalidate(reason);
#endif
    m_state.preview={};m_state.syntheticAddress.clear();m_state.proposal.clear();m_state.reason=reason;
    if(reason==Reason::Unloaded)m_state.capability.loaded=false;Q_EMIT changed();
}
void Model::setOptIn(bool enabled)
{
    if(QThread::currentThread()!=thread())return;++m_generation;m_busy=false;m_state.capability.optedIn=enabled;
#ifdef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
    if(!enabled&&m_backend)m_backend->Invalidate(Reason::Cancelled);
#endif
    m_state.preview={};m_state.syntheticAddress.clear();m_state.proposal.clear();
    m_state.reason=Availability(m_state.capability,Action::Inspect);Q_EMIT changed();
}
bool Model::request(Request request)
{
    if(QThread::currentThread()!=thread())return false;
    auto blocked=Availability(m_state.capability,request.action);
    if(m_busy)blocked=Reason::Busy;
    if(blocked!=Reason::None){m_state.reason=blocked;Q_EMIT changed();return false;}
#ifndef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
    m_state.reason=Reason::Network;Q_EMIT changed();return false;
#else
    if(!m_backend){m_state.reason=Reason::Unavailable;Q_EMIT changed();return false;}
    request.revision=m_state.revision;request.optedIn=m_state.capability.optedIn;
    const auto generation=++m_generation;m_busy=true;Q_EMIT changed();
    auto* watcher=new QFutureWatcher<Snapshot>(this);
    connect(watcher,&QFutureWatcher<Snapshot>::finished,this,[this,watcher,generation]{
        const auto result=watcher->result();watcher->deleteLater();
        if(generation!=m_generation)return;
        m_busy=false;m_state=result;Q_EMIT changed();
    });
    // The worker owns only its backend; it never dereferences a QObject/model.
    // A destroyed watcher/context safely discards completion, without blocking.
    watcher->setFuture(QtConcurrent::run([backend=m_backend,request]{
        try{return backend->Perform(request);}catch(...){Snapshot failed;failed.reason=Reason::Failed;return failed;}
    }));return true;
#endif
}
#ifdef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
void Model::attachIsolated(std::shared_ptr<Backend> backend,Snapshot state)
{
    if(QThread::currentThread()!=thread())return;++m_generation;m_busy=false;m_busy=false;
    if(!state.capability.isolated){m_state={};m_backend.reset();m_state.reason=Reason::Network;}
    else{m_backend=std::move(backend);m_state=std::move(state);}Q_EMIT changed();
}
#endif
}
