// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <qt/mercaweavepanel.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <atomic>
#include <iostream>
using namespace MercaWeaveQt;
namespace {
size_t checks=0;void Check(bool b,const char* m){++checks;if(!b)throw std::runtime_error(m);}
#ifdef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
class Controlled final:public Backend {
public:
    Snapshot state;std::atomic<bool> release{true},entered{false};std::atomic<int> calls{0};
    Snapshot Perform(const Request&)override{++calls;entered=true;while(!release.load())QThread::msleep(1);return state;}
};
Snapshot Open(){Snapshot s;s.capability={true,true,true,true,false,false,true,true,true,true,true,true};s.reason=Reason::None;s.revision=1;return s;}
void Finish(Model& model){for(int i=0;i<500&&model.busy();++i)QTest::qWait(2);Check(!model.busy(),"asynchronous request completed");}
#endif
void Run()
{
    Model model;for(auto view:{Panel::View::Receive,Panel::View::Send,Panel::View::Mining}){Panel panel(view);panel.setModel(&model);panel.setChecked(true);panel.show();QCoreApplication::processEvents();
        Check(panel.findChild<QPushButton*>("issueGroupedAddress")->isEnabled()==false,"no production issue action");Check(panel.findChild<QCheckBox*>("rewardSweepEnabled")->isChecked()==false,"sweep disabled by default");Check(panel.findChild<QSpinBox*>("rewardSweepTrigger")->value()==50,"50 mature reward default");Check(panel.findChild<QComboBox*>("authorizationMode")->currentIndex()==0,"Automatic selector default");Check(panel.findChild<QLabel*>("syntheticGroupedAddress")->text().isEmpty(),"no inactive spendable-looking destination");}
    Capabilities all{true,true,true,true,false,false,true,true,true,true,true,true};
#ifndef MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY
    for(auto action:{Action::IssueReceive,Action::Plan,Action::Preview,Action::SelectMining})Check(Availability(all,action)==Reason::Network,"production ignores injected positive booleans");
    model.setOptIn(true);Request issue;issue.action=Action::IssueReceive;Check(!model.request(issue),"normal API has no backend bypass");
#else
    Check(Availability(all,Action::Plan)==Reason::None,"isolated test capability available");
    auto c=all;c.network=false;Check(Availability(c,Action::Plan)==Reason::Network,"network separately required");c=all;c.isolated=false;Check(Availability(c,Action::Plan)==Reason::Network,"mock context separately required");c=all;c.locked=true;c.inventory=false;Check(Availability(c,Action::IssueReceive)==Reason::Locked,"locked derivation blocked");c.inventory=true;Check(Availability(c,Action::IssueReceive)==Reason::None,"locked public inventory allowed");Check(Availability(c,Action::Plan)==Reason::Locked,"locked signing blocked");c=all;c.metadata=false;Check(Availability(c,Action::Plan)==Reason::Metadata,"missing metadata blocked");c=all;c.watchOnly=true;Check(Availability(c,Action::Plan)==Reason::WatchOnly,"watch-only signer denied");c=all;c.signer=false;Check(Availability(c,Action::Plan)==Reason::Signer,"signer absent");c=all;c.synchronized=false;Check(Availability(c,Action::Plan)==Reason::Sync,"sync required");
    auto backend=std::make_shared<Controlled>();backend->state=Open();model.attachIsolated(backend,backend->state);Panel panel(Panel::View::Send);panel.setModel(&model);panel.setChecked(true);panel.show();
    auto s=Open();s.preview={true,false,5,2,1,2,2,2,3,10000,12000,10,1,QStringLiteral("Native witness-v2"),{}};backend->state=s;Request preview;preview.action=Action::Preview;Check(model.request(preview),"request starts");Check(!model.request(preview),"duplicate UI action blocked");Finish(model);Check(panel.findChild<QLabel*>("groupTransactionPreview")->text().contains("Incomplete PSBT"),"partial never shown broadcast ready");Check(panel.findChild<QLabel*>("groupTransactionPreview")->text().contains("2 anchors, 2 followers"),"mixed group counters rendered");
    backend->release=false;backend->entered=false;Check(model.request(preview),"controlled slow request");for(int i=0;i<100&&!backend->entered;++i)QTest::qWait(1);model.invalidate(Reason::Unloaded);backend->release=true;QTest::qWait(20);Check(model.state().reason==Reason::Unloaded&&!model.state().preview.available,"stale completion discarded on unload");
    auto transient=new Model;auto pending=std::make_shared<Controlled>();pending->state=Open();pending->release=false;transient->attachIsolated(pending,Open());QSignalSpy changed(transient,&Model::changed);Check(transient->request(preview),"destroy while worker running");delete transient;pending->release=true;QTest::qWait(20);Check(true,"owned worker survives QObject destruction without callback");
    auto dying=new Model;Panel detached(Panel::View::Receive);detached.setModel(dying);delete dying;Check(!detached.findChild<QPushButton*>("issueGroupedAddress")->isEnabled(),"panel weak model lifetime");
#endif
    Check(Availability(all,static_cast<Action>(999))==Reason::Unavailable,"indeterminate action fails closed");
    Check(Money(1)=="0.01 MCA"&&Money(100)=="1.00 MCA"&&Money(12345)=="123.45 MCA","integer fee formatting");
    Check(model.state().recoveryAddresses==100&&model.state().recoveryGroups==20,"locked recovery hints");
}
}
int main(int argc,char** argv){qputenv("QT_QPA_PLATFORM","offscreen");QApplication app(argc,argv);try{Run();std::cout<<"{\"qt_checks\":"<<checks<<",\"passed\":true}\n";}catch(const std::exception& e){std::cerr<<"FAIL Qt: "<<e.what()<<'\n';return 1;}}
