// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license.

// Optional inspection-only interposer for the real native Qt executable.
// It drives ordinary widget actions and screenshots; it exposes no wallet,
// signing, consensus or research capability. Never installed in the application.
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSaveFile>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWidget>

#include <dlfcn.h>

namespace {
QWidget* Find(const QString& name, const QString& type = {})
{
    QWidget* fallback{nullptr};
    for (auto* widget : QApplication::allWidgets()) {
        if ((!name.isEmpty() && widget->objectName() != name) ||
            (!type.isEmpty() && widget->metaObject()->className() != type)) continue;
        if (widget->isVisible()) return widget;
        fallback = widget;
    }
    return fallback;
}

QJsonObject Describe(QWidget* widget)
{
    const auto rect = widget->geometry();
    QJsonObject result{{"name", widget->objectName()}, {"class", widget->metaObject()->className()},
                       {"visible", widget->isVisible()}, {"enabled", widget->isEnabled()},
                       {"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()}, {"height", rect.height()}};
    if (auto* label = qobject_cast<QLabel*>(widget)) result["text"] = label->text();
    if (auto* message = qobject_cast<QMessageBox*>(widget)) result["text"] = message->text();
    if (auto* button = qobject_cast<QAbstractButton*>(widget)) result["text"] = button->text();
    if (auto* combo = qobject_cast<QComboBox*>(widget)) {
        result["index"] = combo->currentIndex();
        QJsonArray items;
        for (int i = 0; i < combo->count(); ++i) {
            const auto* model = qobject_cast<QStandardItemModel*>(combo->model());
            items.append(QJsonObject{{"text", combo->itemText(i)},
                {"enabled", !model || model->item(i)->isEnabled()},
                {"text_width", combo->fontMetrics().horizontalAdvance(combo->itemText(i))}});
        }
        result["items"] = items;
    }
    if (auto* tree = qobject_cast<QTreeWidget*>(widget)) {
        const auto* header = tree->header();
        QJsonArray columns;
        for (int column = 0; column < tree->columnCount(); ++column) {
            columns.append(QJsonObject{{"logical", column}, {"visual", header->visualIndex(column)},
                {"text", tree->headerItem()->text(column)}, {"x", header->sectionViewportPosition(column)},
                {"width", header->sectionSize(column)}, {"hidden", tree->isColumnHidden(column)}});
        }
        result["columns"] = columns;
        result["viewport_width"] = tree->viewport()->width();
        result["horizontal_scroll"] = tree->horizontalScrollBar()->value();
        result["device_pixel_ratio"] = tree->devicePixelRatioF();
        QJsonArray rows;
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if ((*it)->text(6).isEmpty()) continue;
            rows.append(QJsonObject{{"type", (*it)->text(6)}, {"amount", (*it)->text(1)},
                {"confirmations", (*it)->text(5)}, {"selected", (*it)->checkState(0) == Qt::Checked},
                {"checkable", bool((*it)->flags() & Qt::ItemIsUserCheckable)},
                {"type_text_width", tree->fontMetrics().horizontalAdvance((*it)->text(6))},
                {"txid", (*it)->data(3, Qt::UserRole).toString()},
                {"vout", (*it)->data(3, Qt::UserRole + 1).toInt()}});
        }
        result["rows"] = rows;
    }
    return result;
}

QJsonObject Execute(const QJsonObject& command, const QString& directory)
{
    const auto action = command["action"].toString();
    QJsonObject result{{"ok", true}, {"platform", QGuiApplication::platformName()},
                       {"qt_version", qVersion()}, {"style", qApp->style()->objectName()}};
    if (action == "send" || action == "receive") {
        auto* main = Find({}, "BitcoinGUI");
        if (!main) return {{"ok", false}, {"error", "BitcoinGUI not ready"}};
        bool found{false};
        for (auto* item : main->findChildren<QAction*>()) {
            if (item->text().remove('&') == (action == "send" ? "Send" : "Receive")) { item->trigger(); found = true; break; }
        }
        if (!found) return {{"ok", false}, {"error", "Native Send action not ready"}};
    } else if (action == "wallet") {
        auto* main = Find({}, "BitcoinGUI");
        bool found{false};
        if (main) for (auto* combo : main->findChildren<QComboBox*>()) {
            const int index = combo->findText(command["wallet"].toString());
            if (index >= 0) { combo->setCurrentIndex(index); found = true; break; }
        }
        if (!found) return {{"ok", false}, {"error", "Wallet selector not ready"}};
    } else if (action == "resize") {
        auto* main = Find({}, command["class"].toString("BitcoinGUI"));
        if (!main) return {{"ok", false}, {"error", "Main window not ready"}};
        main->resize(command["width"].toInt(), command["height"].toInt());
        main->show();
    } else if (action == "screenshot") {
        auto* widget = command["popup"].toBool() ? QApplication::activePopupWidget()
            : Find(command["name"].toString(), command["class"].toString("BitcoinGUI"));
        if (!widget) return {{"ok", false}, {"error", "Screenshot widget not ready"}};
        // Only filenames inside this disposable artifact directory are accepted.
        const auto filename = command["file"].toString();
        if (filename.contains('/') || !filename.endsWith(".png")) return {{"ok", false}, {"error", "Invalid screenshot filename"}};
        const auto path = QDir(directory).filePath(filename);
        if (!widget->grab().save(path)) return {{"ok", false}, {"error", "Screenshot save failed"}};
        result["path"] = path;
        result["widget"] = Describe(widget);
    } else if (action == "receiveLabel") {
        auto* label = qobject_cast<QLineEdit*>(Find("reqLabel"));
        if (!label || !label->isVisible()) return {{"ok", false}, {"error", "Receive label unavailable"}};
        label->setText(command["label"].toString());
    } else if (action == "confirmSend") {
        auto* dialog = qobject_cast<QMessageBox*>(Find({}, "SendConfirmationDialog"));
        auto* yes = dialog ? dialog->button(QMessageBox::Yes) : nullptr;
        if (!yes || !yes->isEnabled()) return {{"ok", false}, {"error", "Native Send confirmation not ready"}};
        yes->click();
    } else if (action == "recipient") {
        auto* address = qobject_cast<QLineEdit*>(Find("payTo"));
        auto* amount = Find("payAmount");
        if (!address || !amount) return {{"ok", false}, {"error", "Recipient fields unavailable"}};
        address->setText(command["address"].toString());
        if (!amount->setProperty("value", QVariant::fromValue<qlonglong>(command["amount"].toInteger())))
            return {{"ok", false}, {"error", "Amount property unavailable"}};
    } else if (action == "click" || action == "clickQueued") {
        auto* button = qobject_cast<QAbstractButton*>(Find(command["name"].toString()));
        if (!button || !button->isVisible() || !button->isEnabled()) return {{"ok", false}, {"error", "Button unavailable"}};
        if (action == "clickQueued") QTimer::singleShot(0, button, [button] { button->click(); });
        else button->click();
    } else if (action == "select" || action == "popup") {
        auto* combo = qobject_cast<QComboBox*>(Find(command["name"].toString()));
        if (!combo || !combo->isVisible()) return {{"ok", false}, {"error", "Combo unavailable"}};
        if (action == "popup") combo->showPopup();
        else {
            const int index = command["index"].toInt(-1);
            const auto* model = qobject_cast<QStandardItemModel*>(combo->model());
            if (index < 0 || index >= combo->count() || (model && !model->item(index)->isEnabled())) {
                return {{"ok", false}, {"error", "Disabled or invalid option"}};
            }
            combo->setCurrentIndex(index);
        }
    } else if (action == "hidePopup") {
        if (auto* popup = QApplication::activePopupWidget()) popup->hide();
    } else if (action == "close") {
        auto* dialog = qobject_cast<QDialog*>(Find({}, command["class"].toString()));
        if (!dialog) return {{"ok", false}, {"error", "Dialog unavailable"}};
        dialog->reject();
    } else if (action == "expandCoins") {
        auto* tree = qobject_cast<QTreeWidget*>(Find("treeWidget"));
        if (!tree) return {{"ok", false}, {"error", "Coin Control tree unavailable"}};
        tree->expandAll();
        tree->horizontalScrollBar()->setValue(0);
    } else if (action == "scrollTypes") {
        auto* tree = qobject_cast<QTreeWidget*>(Find("treeWidget"));
        if (!tree) return {{"ok", false}, {"error", "Coin Control tree unavailable"}};
        tree->horizontalScrollBar()->setValue(tree->horizontalScrollBar()->maximum());
    } else if (action == "coin" || action == "coinOutpoint") {
        auto* tree = qobject_cast<QTreeWidget*>(Find("treeWidget"));
        if (!tree) return {{"ok", false}, {"error", "Coin Control tree unavailable"}};
        bool found{false};
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if (action == "coin" && (*it)->text(6) != command["type"].toString()) continue;
            if (action == "coinOutpoint" && ((*it)->data(3, Qt::UserRole).toString() != command["txid"].toString()
                || (*it)->data(3, Qt::UserRole + 1).toInt() != command["vout"].toInt())) continue;
            if (!((*it)->flags() & Qt::ItemIsUserCheckable)) continue;
            (*it)->setCheckState(0, command["selected"].toBool() ? Qt::Checked : Qt::Unchecked);
            found = true;
            break;
        }
        if (!found) return {{"ok", false}, {"error", "Eligible coin unavailable"}};
    } else if (action != "state") {
        return {{"ok", false}, {"error", "Unknown inspection action"}};
    }
    QJsonArray widgets;
    for (const auto& name : {"receiveAddressType", "receiveMercaWeaveAvailability", "reqLabel", "receiveButton", "spendingMode", "spendingModeStatus", "pushButtonCoinControl", "frameCoinControl",
             "sendButton", "payTo", "payAmount", "customFee", "inputTypeFilter", "treeWidget",
             "labelCoinControlBytes", "labelCoinControlFee", "labelCoinControlChange"}) {
        if (auto* widget = Find(name)) widgets.append(Describe(widget));
    }
    result["widgets"] = widgets;
    QJsonArray dialogs;
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (widget->isVisible()) dialogs.append(Describe(widget));
    }
    result["dialogs"] = dialogs;
    return result;
}
} // namespace

int QApplication::exec()
{
    const auto directory = qEnvironmentVariable("MERCAWEAVE_GUI_PROBE_DIR");
    if (!directory.isEmpty()) {
        auto* timer = new QTimer(qApp);
        QObject::connect(timer, &QTimer::timeout, qApp, [directory, previous = qint64{-1}]() mutable {
            QFile input{QDir(directory).filePath("command.json")};
            if (!input.open(QIODevice::ReadOnly)) return;
            const auto command = QJsonDocument::fromJson(input.readAll()).object();
            const auto id = command["id"].toInteger(-1);
            if (id < 0 || id == previous) return;
            previous = id;
            auto result = Execute(command, directory);
            result["id"] = id;
            QSaveFile output{QDir(directory).filePath("response.json")};
            if (output.open(QIODevice::WriteOnly)) {
                output.write(QJsonDocument(result).toJson());
                output.commit();
            }
        });
        timer->start(100);
    }
    const auto original = reinterpret_cast<int (*)()>(dlsym(RTLD_NEXT, "_ZN12QApplication4execEv"));
    return original ? original() : 1;
}
