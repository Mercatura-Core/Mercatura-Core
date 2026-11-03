// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_QT_MERCAWEAVEPANEL_H
#define BITCOIN_QT_MERCAWEAVEPANEL_H
#include <qt/mercaweavemodel.h>
#include <QGroupBox>
class QLabel;class QCheckBox;class QSpinBox;class QComboBox;class QPushButton;class QLineEdit;
namespace MercaWeaveQt {
class Panel final : public QGroupBox {
    Q_OBJECT
public:
    enum class View {Receive,Send,Mining};
    explicit Panel(View,QWidget* parent=nullptr);
    void setModel(Model*);
private:
    const View m_view;QPointer<Model> m_model;
    QWidget* m_body;QLabel *m_status,*m_detail,*m_warning,*m_recovery,*m_publicAddress;
    QCheckBox *m_optIn,*m_sweep;
    QSpinBox *m_account,*m_trigger;QComboBox *m_mode,*m_group;
    QLineEdit* m_destination;QPushButton *m_issue,*m_rotate,*m_plan,*m_cancel;
    void refresh();void dispatch(Action);
};
}
#endif
