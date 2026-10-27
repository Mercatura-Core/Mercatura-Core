// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <interfaces/node.h>
#include <net_pq.h>
#include <qt/clientmodel.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/rpcconsole.h>
#include <test/util/setup_common.h>
#include <test/util/net.h>

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QClipboard>
#include <QSettings>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTest>

const std::function<void(const std::string&)> G_TEST_LOG_FUN{};
const std::function<std::vector<const char*>()> G_TEST_COMMAND_LINE_ARGUMENTS{};
const std::function<std::string()> G_TEST_GET_FULL_NAME{};

class PeerSecurityTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void peerRowsRefreshAndClearRealTransportState()
    {
        TestingSetup setup{ChainType::REGTEST};
        auto& connman=static_cast<ConnmanTestMsg&>(*setup.m_node.connman);
        auto identity=std::make_shared<pq::Identity>();
        const std::array<uint8_t,32> seed{23}; QVERIFY(identity->FromSeed(seed));
        auto context=std::make_shared<pq::Context>(pq::SelectedNetwork(),identity);
        auto make_node=[&](NodeId id,ConnectionType type) {
            auto* peer=new CNode(id,{},CAddress{},0,0,CService{},"",type,false,0,CNodeOptions{.pq_context=context});
            connman.AddTestNode(*peer); return peer;
        };
        struct Cleanup { ConnmanTestMsg& conn; ~Cleanup() { conn.ClearTestNodes(); } } cleanup{connman};
        auto* initiator=make_node(1,ConnectionType::MANUAL);
        auto* responder=make_node(2,ConnectionType::INBOUND);
        auto node=interfaces::MakeNode(setup.m_node);
        OptionsModel options{*node}; bilingual_str error; QVERIFY(options.Init(error));
        ClientModel client{*node,&options};
        std::unique_ptr<const PlatformStyle> style{PlatformStyle::instantiate("other")};
        RPCConsole console{*node,style.get(),nullptr};
        console.setClientModel(&client);
        auto* model=client.getPeerTableModel();
        auto* table=console.findChild<QTableView*>("peerWidget"); QVERIFY(table);
        auto* session=console.findChild<QLineEdit*>("peerSessionId"); QVERIFY(session);
        auto* encryption=console.findChild<QLabel*>("peerPQEncryption"); QVERIFY(encryption);
        QCOMPARE(model->rowCount(),2);
        table->selectRow(0);
        QTRY_COMPARE(encryption->text(),QString{"Pending"});
        QVERIFY(session->text().isEmpty());
        QElapsedTimer elapsed; elapsed.start();
        auto transfer=[](Transport& from,Transport& to) {
            auto [wire,more,type]=from.GetBytesToSend(false);
            if (wire.empty()) return true;
            const auto count=wire.size();
            if (!to.ReceivedBytes(wire) || !wire.empty()) return false;
            from.MarkBytesSent(count); return true;
        };
        while (!initiator->m_transport->GetInfo().pq_encrypted || !responder->m_transport->GetInfo().pq_encrypted) {
            QVERIFY(elapsed.elapsed()<5000);
            QVERIFY(initiator->m_transport->Poll()); QVERIFY(responder->m_transport->Poll());
            QVERIFY(transfer(*initiator->m_transport,*responder->m_transport));
            QVERIFY(transfer(*responder->m_transport,*initiator->m_transport));
            QTest::qWait(1);
        }
        QSignalSpy refreshed{model,&QAbstractItemModel::dataChanged};
        model->refresh();
        QVERIFY(refreshed.count()>0);
        QTRY_COMPARE(encryption->text(),QString{"Established"});
        QCOMPARE(session->text().size(),64);
        if (qEnvironmentVariableIsSet("MERCATURA_PQ_GUI_CAPTURE")) {
            console.setTabFocus(RPCConsole::TabTypes::PEERS);
            console.resize(1200,800); console.show(); QTest::qWait(30);
            QVERIFY(console.grab().save(qEnvironmentVariable("MERCATURA_PQ_GUI_CAPTURE")));
        }
        initiator->m_transport->Disconnect(); model->refresh();
        QTRY_COMPARE(encryption->text(),QString{"Unavailable"});
        QVERIFY(session->text().isEmpty());
        connman.ClearTestNodes(); model->refresh();
        QCOMPARE(model->rowCount(),0);
        QVERIFY(session->text().isEmpty());
        console.setClientModel(nullptr);
    }
    void assuranceAndSessionLifecycle()
    {
        BasicTestingSetup setup{ChainType::REGTEST};
        auto node = interfaces::MakeNode(setup.m_node);
        std::unique_ptr<const PlatformStyle> style{PlatformStyle::instantiate("other")};
        RPCConsole console{*node,style.get(),nullptr};
        auto value = [&](const char* name) { auto* widget=console.findChild<QLineEdit*>(name); Q_ASSERT(widget); return widget; };
        auto label = [&](const char* name) { auto* value=console.findChild<QLabel*>(name); Q_ASSERT(value); return value; };
        CNodeStats stats{};
        for (const auto legacy : {TransportProtocolType::V1,TransportProtocolType::V2}) {
            stats.m_transport_type=legacy; stats.m_session_id=legacy==TransportProtocolType::V2 ? "legacy-session" : "";
            console.updatePeerSecurity(&stats);
            QVERIFY(label("peerPQEncryption")->isHidden());
            QCOMPARE(value("peerSessionId")->text(),QString::fromStdString(stats.m_session_id));
        }
        stats.m_transport_type=TransportProtocolType::PQ_V1; stats.m_session_id="stale-session";
        stats.m_pq_identity="stale-fingerprint";
        console.updatePeerSecurity(&stats);
        QCOMPARE(label("peerTransportType")->text(),QString{"PQ v1"});
        QCOMPARE(label("peerPQEncryption")->text(),QString{"Pending"});
        QCOMPARE(label("peerPQAuthentication")->text(),QString{"No"});
        QVERIFY(value("peerSessionId")->text().isEmpty());
        QVERIFY(value("peerPQIdentity")->text().isEmpty());
        QVERIFY(!label("peerPQEncryption")->isHidden());
        stats.m_pq_encrypted=true; stats.m_pq_authenticated=true;
        stats.m_session_id=std::string(64,'a'); stats.m_pq_identity="mcapq1:"+std::string(96,'b');
        console.updatePeerSecurity(&stats);
        QCOMPARE(label("peerPQEncryption")->text(),QString{"Established"});
        QCOMPARE(label("peerPQAuthentication")->text(),QString{"Yes"});
        QCOMPARE(label("peerPQVerification")->text(),QString{"Not configured"});
        QCOMPARE(value("peerSessionId")->text(),QString::fromStdString(stats.m_session_id));
        QCOMPARE(value("peerPQIdentity")->text(),QString::fromStdString(stats.m_pq_identity));
        QVERIFY(value("peerPQIdentity")->isReadOnly());
        value("peerPQIdentity")->selectAll();
        console.updatePeerSecurity(&stats); // Polling must preserve exact-value selection.
        QCOMPARE(value("peerPQIdentity")->selectedText(),QString::fromStdString(stats.m_pq_identity));
        value("peerPQIdentity")->copy();
        QCOMPARE(QApplication::clipboard()->text(),QString::fromStdString(stats.m_pq_identity));
        stats.m_pq_identity_verified=true; console.updatePeerSecurity(&stats);
        QCOMPARE(label("peerPQVerification")->text(),QString{"Verified"});
        // Pin failure/disconnect removes the row; clearing must remove public
        // values too, so switching selection or network cannot display old trust.
        stats.m_pq_failed=true; stats.m_pq_pin_mismatch=true;
        console.updatePeerSecurity(&stats);
        QCOMPARE(label("peerPQEncryption")->text(),QString{"Unavailable"});
        QCOMPARE(label("peerPQVerification")->text(),QString{"Mismatch"});
        QVERIFY(value("peerPQIdentity")->text().isEmpty());
        console.updatePeerSecurity(nullptr);
        QVERIFY(value("peerSessionId")->text().isEmpty());
        QVERIFY(value("peerPQIdentity")->text().isEmpty());
        QVERIFY(label("peerPQVerification")->isHidden());
        stats.m_pq_failed=false; stats.m_pq_pin_mismatch=false;
        stats.m_pq_encrypted=false; stats.m_pq_authenticated=false;
        console.updatePeerSecurity(&stats); // Pending reconnect, even with stale injected fields.
        QCOMPARE(label("peerPQVerification")->text(),QString{"Pending"});
        QVERIFY(value("peerPQIdentity")->text().isEmpty());
        stats.m_pq_encrypted=true; stats.m_pq_authenticated=true;
        stats.m_session_id=std::string(64,'c'); console.updatePeerSecurity(&stats);
        QCOMPARE(value("peerSessionId")->text(),QString::fromStdString(stats.m_session_id));
        console.setClientModel(nullptr); // Node shutdown/network replacement.
        QVERIFY(value("peerSessionId")->text().isEmpty());
        QVERIFY(value("peerPQIdentity")->text().isEmpty());
    }
};

int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    QTemporaryDir settings;
    if (!settings.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QCoreApplication::setOrganizationName("Mercatura-test");
    QCoreApplication::setApplicationName("peer-assurance-test");
    PeerSecurityTests test;
    return QTest::qExec(&test,argc,argv);
}
#include <qt/test/peersecuritytests.moc>
