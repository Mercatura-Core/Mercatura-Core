// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <qt/mercaweavepanel.h>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <limits>
namespace MercaWeaveQt {
Panel::Panel(View view,QWidget* parent):QGroupBox(tr("Optional grouped PQ — unavailable on current networks"),parent),m_view(view)
{
    setObjectName(QStringLiteral("mercaWeavePanel"));setCheckable(true);setChecked(false);
    auto* outer=new QVBoxLayout(this);m_body=new QWidget(this);outer->addWidget(m_body);auto* form=new QFormLayout(m_body);form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_status=new QLabel(m_body);m_status->setObjectName(QStringLiteral("groupAvailability"));m_status->setWordWrap(true);m_status->setTextFormat(Qt::PlainText);form->addRow(m_status);
    auto* defaults=new QLabel(view==View::Mining?tr("Reward destination: independent-key witness-v2 PQ (default). Mining software, developer and pool/operator fees: 0%. Blockchain transaction fees are separate."):tr("Independent-key witness-v2 PQ remains the default. Grouped addressing never converts existing coins."),m_body);defaults->setWordWrap(true);form->addRow(defaults);
    m_optIn=new QCheckBox(tr("Explicitly opt into grouped PQ"),m_body);m_optIn->setObjectName(QStringLiteral("groupOptIn"));form->addRow(m_optIn);
    m_account=new QSpinBox(m_body);m_account->setObjectName(QStringLiteral("groupAccount"));m_account->setRange(0,std::numeric_limits<int>::max());form->addRow(tr("Account"),m_account);
    m_group=new QComboBox(m_body);m_group->setObjectName(QStringLiteral("groupSelection"));form->addRow(tr("Group / purpose / issued range"),m_group);
    m_mode=new QComboBox(m_body);m_mode->setObjectName(QStringLiteral("authorizationMode"));m_mode->addItems({tr("Automatic (Recommended)"),tr("Standard PQ — Witness v2"),tr("MercaWeave Grouped PQ — Witness v3")});form->addRow(tr("Authorization mode"),m_mode);
    m_issue=new QPushButton(tr("Reserve grouped receive address"),m_body);m_issue->setObjectName(QStringLiteral("issueGroupedAddress"));form->addRow(m_issue);
    m_rotate=new QPushButton(tr("Rotate receive group"),m_body);m_rotate->setObjectName(QStringLiteral("rotateReceiveGroup"));form->addRow(m_rotate);
    m_publicAddress=new QLabel(m_body);m_publicAddress->setObjectName(QStringLiteral("syntheticGroupedAddress"));m_publicAddress->setTextFormat(Qt::PlainText);m_publicAddress->setWordWrap(true);form->addRow(m_publicAddress);
    m_sweep=new QCheckBox(tr("Opt into future automatic reward sweep planning"),m_body);m_sweep->setObjectName(QStringLiteral("rewardSweepEnabled"));form->addRow(m_sweep);
    m_trigger=new QSpinBox(m_body);m_trigger->setObjectName(QStringLiteral("rewardSweepTrigger"));m_trigger->setRange(1,4096);m_trigger->setValue(50);form->addRow(tr("Mature rewards per key before planning"),m_trigger);
    m_destination=new QLineEdit(m_body);m_destination->setObjectName(QStringLiteral("sweepDestination"));m_destination->setPlaceholderText(tr("New independent-key v2 destination by default"));form->addRow(tr("Consolidation destination"),m_destination);
    m_plan=new QPushButton(view==View::Send?tr("Review authorization preview"):tr("Evaluate isolated reward plan"),m_body);m_plan->setObjectName(QStringLiteral("planGroupedSweep"));form->addRow(m_plan);
    m_cancel=new QPushButton(tr("Cancel pending proposal"),m_body);m_cancel->setObjectName(QStringLiteral("cancelGroupedProposal"));form->addRow(m_cancel);
    m_detail=new QLabel(m_body);m_detail->setObjectName(QStringLiteral("groupTransactionPreview"));m_detail->setWordWrap(true);m_detail->setTextFormat(Qt::PlainText);form->addRow(m_detail);
    m_recovery=new QLabel(m_body);m_recovery->setObjectName(QStringLiteral("groupRecoveryStatus"));m_recovery->setWordWrap(true);m_recovery->setTextFormat(Qt::PlainText);form->addRow(m_recovery);
    m_warning=new QLabel(tr("Addresses sharing a signing key can become linked when spent. A compromised group key threatens its outstanding coins. New group indexes do not protect against master-seed compromise. Nonces do not remove these risks. Back up issued-range metadata; large deployments may need wider discovery windows."),m_body);m_warning->setWordWrap(true);form->addRow(m_warning);
    connect(this,&QGroupBox::toggled,m_body,&QWidget::setVisible);m_body->setVisible(false);
    connect(m_optIn,&QCheckBox::toggled,this,[this](bool enabled){if(m_model)m_model->setOptIn(enabled);});
    connect(m_issue,&QPushButton::clicked,this,[this]{dispatch(Action::IssueReceive);});
    connect(m_rotate,&QPushButton::clicked,this,[this]{dispatch(Action::RotateReceive);});
    connect(m_plan,&QPushButton::clicked,this,[this]{dispatch(m_view==View::Send?Action::Preview:Action::Plan);});
    connect(m_cancel,&QPushButton::clicked,this,[this]{dispatch(Action::Cancel);});
    connect(m_group,&QComboBox::activated,this,[this]{if(m_view==View::Mining)dispatch(Action::SelectMining);});
    setModel(new Model(this));
}
void Panel::setModel(Model* model)
{
    if(m_model)disconnect(m_model,nullptr,this,nullptr);m_model=model;
    if(model){connect(model,&Model::changed,this,&Panel::refresh);connect(model,&QObject::destroyed,this,[this]{m_model=nullptr;refresh();});}refresh();
}
void Panel::dispatch(Action action)
{
    if(!m_model)return;Request request;request.action=action;request.account=static_cast<quint32>(m_account->value());request.purpose=m_view==View::Mining?2:0;
    request.mode=m_mode->currentIndex();request.group=m_group->currentData().toString();request.sweepEnabled=m_sweep->isChecked();request.threshold=m_trigger->value();request.destination=m_destination->text();m_model->request(request);
}
void Panel::refresh()
{
    const Snapshot empty;const auto& s=m_model?m_model->state():empty;const bool busy=m_model&&m_model->busy();
    const auto allowed=[&](Action a){return m_model&&!busy&&Availability(s.capability,a)==Reason::None;};
    m_status->setText(Explain(m_model?s.reason:Reason::Unloaded));
    const QSignalBlocker opt(m_optIn),groups(m_group);m_optIn->setChecked(s.capability.optedIn);
    // Existing networks cannot even opt into a positive-capability state.
    m_optIn->setEnabled(s.capability.isolated&&s.capability.network&&s.capability.wallet&&!busy);
    m_account->setEnabled(allowed(Action::Inspect));m_mode->setEnabled(m_view==View::Send&&allowed(Action::Preview));
    auto selected=m_group->currentData();m_group->clear();for(const auto& g:s.groups)if(g.purpose==(m_view==View::Mining?2:0))m_group->addItem(tr("Account %1 · purpose %2 · group %3 · indexes %4–%5 · %6/64 · %7").arg(g.account).arg(g.purpose).arg(g.group).arg(g.first).arg(g.last).arg(g.issued).arg(g.lifecycle),g.identity);
    auto found=m_group->findData(selected);if(found>=0)m_group->setCurrentIndex(found);m_group->setEnabled(allowed(Action::Inspect));
    m_issue->setVisible(m_view==View::Receive);m_rotate->setVisible(m_view==View::Receive);m_issue->setEnabled(allowed(Action::IssueReceive));m_rotate->setEnabled(allowed(Action::RotateReceive));m_mode->setVisible(m_view==View::Send);if(auto* form=qobject_cast<QFormLayout*>(m_body->layout()))if(auto* label=form->labelForField(m_mode))label->setVisible(m_view==View::Send);
    m_publicAddress->setText(s.capability.isolated?s.syntheticAddress:QString{});
    for(QWidget* field:QList<QWidget*>{m_sweep,m_trigger,m_destination}){field->setVisible(m_view==View::Mining);if(auto* form=qobject_cast<QFormLayout*>(m_body->layout()))if(auto* label=form->labelForField(field))label->setVisible(m_view==View::Mining);field->setEnabled(allowed(Action::Plan));}
    m_plan->setVisible(m_view!=View::Receive);m_plan->setEnabled(allowed(m_view==View::Send?Action::Preview:Action::Plan));m_cancel->setVisible(m_view==View::Mining);m_cancel->setEnabled(!s.proposal.isEmpty()&&allowed(Action::Cancel));
    QString details;
    if(m_view==View::Mining)details=tr("Mature rewards: %1 · immature: %2 · eligible: %3\nProposal: %4\n").arg(s.mature).arg(s.immature).arg(s.eligible).arg(s.proposal.isEmpty()?tr("None"):s.proposal);
    const auto& p=s.preview;if(p.available){details+=tr("%1 inputs / %2 outputs · v2 inputs %3 · v3 group authorizations %4 (%5 anchors, %6 followers) · %7 ML-DSA signatures\n%8 full serialized bytes · %9 total WU · fee %10 · rate %11 per 1,000 full bytes\nChange: %12 · %13").arg(p.inputs).arg(p.outputs).arg(p.nativeInputs).arg(p.groups).arg(p.anchors).arg(p.followers).arg(p.signatures).arg(p.bytes).arg(p.weight).arg(Money(p.fee)).arg(Money(p.perK)).arg(p.changeType,p.complete?tr("Authorization complete; grouped broadcasting is unavailable"):tr("Incomplete PSBT; not ready for broadcast"));}
    else details+=tr("No current transaction-aware estimate is available.");if(p.available)details+=tr("\nTransactions: %1 · largest transaction: %2 WU. Totals include every split transaction.").arg(p.transactions).arg(p.maxWeight?p.maxWeight:p.weight);m_detail->setText(details);
    m_recovery->setText(tr("Public inventory: %1 · metadata backup: %2\nDiscovery hints: %3 address candidates / %4 successive unused groups. Seed-only recovery is not guaranteed.\n%5").arg(s.inventory).arg(s.metadataBackup?tr("available"):tr("unavailable")).arg(s.recoveryAddresses).arg(s.recoveryGroups).arg(s.recoveryStatus.isEmpty()?tr("Live and resumable rescanning are unavailable."):s.recoveryStatus));
}
}
