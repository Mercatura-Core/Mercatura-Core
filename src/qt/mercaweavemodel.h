// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_QT_MERCAWEAVEMODEL_H
#define BITCOIN_QT_MERCAWEAVEMODEL_H
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>
#include <memory>

namespace MercaWeaveQt {
enum class Action { Inspect, IssueReceive, RotateReceive, SelectMining, Plan, Cancel, Preview };
enum class Reason { None, Unavailable, Network, OptIn, Locked, WatchOnly, Inventory, Metadata, Signer, Sync, Busy, Stale, Threshold, Reserved, Destination, Fee, Dust, Funds, Limits, Failed, Cancelled, Conflict, Unloaded, Partial, Unsupported };
struct Capabilities {
    bool compiled{true}, wallet{false}, network{false}, isolated{false};
    bool locked{true}, watchOnly{false}, inventory{false}, metadata{false}, signer{false};
    bool optedIn{false}, synchronized{false}, loaded{true};
};
Reason Availability(const Capabilities&,Action);
QString Explain(Reason);
QString Money(qint64 units);
struct GroupRow { quint32 account{0},group{0},first{0},last{0};int purpose{0},issued{0};QString identity,lifecycle; };
struct Preview {
    bool available{false},complete{false};
    int inputs{0},outputs{0},nativeInputs{0},groups{0},anchors{0},followers{0},signatures{0};
    quint64 bytes{0},weight{0};qint64 fee{0},perK{1};
    QString changeType,summary;
};
struct Snapshot {
    Capabilities capability;
    quint64 revision{0};
    QVector<GroupRow> groups;
    Preview preview;
    int mature{0},immature{0},eligible{0},inventory{0};
    QString syntheticAddress,proposal,recoveryStatus;
    bool metadataBackup{false},scanResumable{false};
    int recoveryAddresses{100},recoveryGroups{20};
    Reason reason{Reason::Unavailable};
};
struct Request {
    Action action{Action::Inspect};quint64 revision{0};
    quint32 account{0};int purpose{0},mode{0},threshold{50};
    bool optedIn{false},sweepEnabled{false};qint64 perK{1},maximumFee{1000};
    QString group,destination;
};
#ifdef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
class Backend {
public:
    virtual ~Backend()=default;
    virtual Snapshot Perform(const Request&)=0;
    virtual void Invalidate(Reason) {} // Thread-safe cancellation token only.
};
#endif
// Public view state only. No seeds, keys, raw wallet pointers, activation lookup,
// signer or broadcast interface. Production never accepts positive authorization.
class Model final : public QObject {
    Q_OBJECT
    Snapshot m_state;
    quint64 m_generation{0};bool m_busy{false};
#ifdef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
    std::shared_ptr<Backend> m_backend;
#endif
public:
    explicit Model(QObject* parent=nullptr);
    ~Model() override;
    const Snapshot& state()const{return m_state;}
    bool busy()const{return m_busy;}
    quint64 generation()const{return m_generation;}
    void observeNativeWallet(bool locked,bool watchOnly);
    void nativePreview(Preview);
    void invalidate(Reason=Reason::Stale);
    void setOptIn(bool);
    bool request(Request);
#ifdef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
    void attachIsolated(std::shared_ptr<Backend>,Snapshot);
#endif
Q_SIGNALS:
    void changed();
};
}
#endif
