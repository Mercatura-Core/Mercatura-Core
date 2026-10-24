// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/transactiontablemodel.h>

#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/notificationtiming.h>
#include <qt/platformstyle.h>
#include <qt/transactiondesc.h>
#include <qt/transactionrecord.h>
#include <qt/walletmodel.h>

#include <core_io.h>
#include <interfaces/handler.h>
#include <tinyformat.h>
#include <uint256.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>

#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QIcon>
#include <QLatin1Char>
#include <QLatin1String>
#include <QList>
#include <QTimer>


// Amount column is right-aligned it contains numbers
static int column_alignments[] = {
        Qt::AlignLeft|Qt::AlignVCenter, /*status=*/
        Qt::AlignLeft|Qt::AlignVCenter, /*date=*/
        Qt::AlignLeft|Qt::AlignVCenter, /*type=*/
        Qt::AlignLeft|Qt::AlignVCenter, /*address=*/
        Qt::AlignRight|Qt::AlignVCenter /* amount */
    };

// Comparison operator for sort/binary search of model tx list
struct TxLessThan
{
    bool operator()(const TransactionRecord &a, const TransactionRecord &b) const
    {
        return a.hash < b.hash;
    }
    bool operator()(const TransactionRecord &a, const Txid &b) const
    {
        return a.hash < b;
    }
    bool operator()(const Txid &a, const TransactionRecord &b) const
    {
        return a < b.hash;
    }
};

// Preserve notification order, including changes received during a rescan.
struct TransactionNotification
{
    Txid hash;
    ChangeType status;
    bool showTransaction;
    std::chrono::steady_clock::time_point queued_at{GUIUtil::NotificationTiming::Enabled() ?
        std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}};
};

// Private implementation
class TransactionTablePriv
{
public:
    explicit TransactionTablePriv(TransactionTableModel *_parent) :
        parent(_parent)
    {
    }

    TransactionTableModel *parent;

    //! Local cache of wallet sorted by transaction hash
    QList<TransactionRecord> cachedWallet;

    // Core callbacks and the GUI share only this notification state. Never
    // hold its mutex while reading the wallet or emitting model signals.
    std::mutex m_notifications_mutex;
    bool m_loaded = false;
    bool m_loading = false;
    bool m_notification_scheduled{false};
    std::deque<TransactionNotification> m_notifications;

    void NotifyTransactionChanged(const Txid& hash, ChangeType status, bool show_transaction = TransactionRecord::showTransaction());
    void DispatchNotifications();
    void SetLoading(bool loading)
    {
        {
            std::lock_guard lock{m_notifications_mutex};
            m_loading = loading;
        }
        DispatchNotifications();
    }
    void StopNotifications()
    {
        std::lock_guard lock{m_notifications_mutex};
        parent = nullptr;
        m_notifications.clear();
    }

    /* Query entire wallet anew from core.
     */
    void refreshWallet(interfaces::Wallet& wallet)
    {
        assert(!m_loaded);
        {
            for (const auto& wtx : wallet.getWalletTxs()) {
                if (TransactionRecord::showTransaction()) {
                    cachedWallet.append(TransactionRecord::decomposeTransaction(wtx));
                }
            }
        }
        {
            std::lock_guard lock{m_notifications_mutex};
            m_loaded = true;
        }
        DispatchNotifications();
    }

    /* Update our model of the wallet incrementally, to synchronize our model of the wallet
       with that of the core.

       Call with transaction that was added, removed or changed.
     */
    bool updateWallet(interfaces::Wallet& wallet, const Txid& hash, int status, bool showTransaction)
    {
        const GUIUtil::NotificationTiming timing{"transaction_update"};
        // Find bounds of this transaction in model
        QList<TransactionRecord>::iterator lower = std::lower_bound(
            cachedWallet.begin(), cachedWallet.end(), hash, TxLessThan());
        QList<TransactionRecord>::iterator upper = std::upper_bound(
            cachedWallet.begin(), cachedWallet.end(), hash, TxLessThan());
        int lowerIndex = (lower - cachedWallet.begin());
        int upperIndex = (upper - cachedWallet.begin());
        bool inModel = (lower != upper);

        if(status == CT_UPDATED)
        {
            if(showTransaction && !inModel)
                status = CT_NEW; /* Not in model, but want to show, treat as new */
            if(!showTransaction && inModel)
                status = CT_DELETED; /* In model, but want to hide, treat as deleted */
        }

        interfaces::WalletTx wtx;
        if (status == CT_NEW && !inModel && showTransaction) {
            const GUIUtil::NotificationTiming lookup{"transaction_lookup"};
            // Core may still own the wallet lock when its queued notification
            // is delivered. Keep the notification and retry without blocking.
            if (!wallet.tryGetWalletTx(hash, wtx)) return false;
        }
        qDebug() << "TransactionTablePriv::updateWallet: " + QString::fromStdString(hash.ToString()) + " " + QString::number(status);
        qDebug() << "    inModel=" + QString::number(inModel) +
                    " Index=" + QString::number(lowerIndex) + "-" + QString::number(upperIndex) +
                    " showTransaction=" + QString::number(showTransaction) + " derivedStatus=" + QString::number(status);

        switch(status)
        {
        case CT_NEW:
            if(inModel)
            {
                qWarning() << "TransactionTablePriv::updateWallet: Warning: Got CT_NEW, but transaction is already in model";
                break;
            }
            if(showTransaction)
            {
                if(!wtx.tx)
                {
                    qWarning() << "TransactionTablePriv::updateWallet: Warning: Got CT_NEW, but transaction is not in wallet";
                    break;
                }
                // Added -- insert at the right position
                QList<TransactionRecord> toInsert =
                        TransactionRecord::decomposeTransaction(wtx);
                if(!toInsert.isEmpty()) /* only if something to insert */
                {
                    parent->beginInsertRows(QModelIndex(), lowerIndex, lowerIndex+toInsert.size()-1);
                    int insert_idx = lowerIndex;
                    for (const TransactionRecord &rec : toInsert)
                    {
                        cachedWallet.insert(insert_idx, rec);
                        insert_idx += 1;
                    }
                    parent->endInsertRows();
                }
            }
            break;
        case CT_DELETED:
            if(!inModel)
            {
                qWarning() << "TransactionTablePriv::updateWallet: Warning: Got CT_DELETED, but transaction is not in model";
                break;
            }
            // Removed -- remove entire transaction from table
            parent->beginRemoveRows(QModelIndex(), lowerIndex, upperIndex-1);
            cachedWallet.erase(lower, upper);
            parent->endRemoveRows();
            break;
        case CT_UPDATED:
            // Miscellaneous updates -- nothing to do, status update will take care of this, and is only computed for
            // visible transactions.
            for (int i = lowerIndex; i < upperIndex; i++) {
                TransactionRecord *rec = &cachedWallet[i];
                rec->status.needsUpdate = true;
            }
            break;
        }
        return true;
    }

    int size()
    {
        return cachedWallet.size();
    }

    TransactionRecord* index(interfaces::Wallet& wallet, const uint256& cur_block_hash, const int idx)
    {
        const GUIUtil::NotificationTiming timing{"transaction_index"};
        if (idx >= 0 && idx < cachedWallet.size()) {
            TransactionRecord *rec = &cachedWallet[idx];

            // If a status update is needed (blocks came in since last check),
            // try to update the status of this transaction from the wallet.
            // Otherwise, simply reuse the cached status.
            interfaces::WalletTxStatus wtx;
            uint256 wallet_tip;
            if (!cur_block_hash.IsNull() && rec->statusUpdateNeeded(cur_block_hash) && wallet.tryGetTxStatus(rec->hash, wtx, wallet_tip)) {
                // Key the cache to the wallet state actually read. If wallet
                // processing trails Core, keep retrying until it catches up.
                rec->updateStatus(wtx, wallet_tip);
            }
            return rec;
        }
        return nullptr;
    }

    QString describe(interfaces::Node& node, interfaces::Wallet& wallet, TransactionRecord* rec, BitcoinUnit unit)
    {
        return TransactionDesc::toHTML(node, wallet, rec, unit);
    }

    QString getTxHex(interfaces::Wallet& wallet, TransactionRecord *rec)
    {
        auto tx = wallet.getTx(rec->hash);
        if (tx) {
            std::string strHex = EncodeHexTx(*tx);
            return QString::fromStdString(strHex);
        }
        return QString();
    }
};

TransactionTableModel::TransactionTableModel(const PlatformStyle *_platformStyle, WalletModel *parent):
        QAbstractTableModel(parent),
        walletModel(parent),
        priv(std::make_shared<TransactionTablePriv>(this)),
        platformStyle(_platformStyle)
{
    subscribeToCoreSignals();

    columns << QString() << tr("Date") << tr("Type") << tr("Label") << BitcoinUnits::getAmountColumnTitle(walletModel->getOptionsModel()->getDisplayUnit());
    priv->refreshWallet(walletModel->wallet());

    connect(walletModel->getOptionsModel(), &OptionsModel::displayUnitChanged, this, &TransactionTableModel::updateDisplayUnit);
}

TransactionTableModel::~TransactionTableModel()
{
    unsubscribeFromCoreSignals();
    priv->StopNotifications();
}

/** Updates the column title to "Amount (DisplayUnit)" and emits headerDataChanged() signal for table headers to react. */
void TransactionTableModel::updateAmountColumnTitle()
{
    columns[Amount] = BitcoinUnits::getAmountColumnTitle(walletModel->getOptionsModel()->getDisplayUnit());
    Q_EMIT headerDataChanged(Qt::Horizontal,Amount,Amount);
}

void TransactionTableModel::updateTransaction(const QString &hash, int status, bool showTransaction)
{
    Txid updated = Txid::FromHex(hash.toStdString()).value();
    priv->NotifyTransactionChanged(updated, static_cast<ChangeType>(status), showTransaction);
}

size_t TransactionTableModel::pendingNotificationCount() const
{
    std::lock_guard lock{priv->m_notifications_mutex};
    return priv->m_notifications.size();
}

void TransactionTableModel::processNotifications()
{
    const GUIUtil::NotificationTiming timing{"transaction_notification_batch"};
    const auto start{std::chrono::steady_clock::now()};
    bool wallet_busy{false};
    // Yield between short batches so mining statistics, input, Stop and Close
    // can run even when wallet notifications arrive faster than model updates.
    for (unsigned int count{0}; count < 32 && std::chrono::steady_clock::now() - start < std::chrono::milliseconds{4}; ++count) {
        TransactionNotification notification;
        {
            std::lock_guard lock{priv->m_notifications_mutex};
            if (!priv->m_loaded || priv->m_loading || priv->m_notifications.empty()) break;
            notification = priv->m_notifications.front();
            setProcessingQueuedTransactions(priv->m_notifications.size() > 10);
        }
        const auto delivered{std::chrono::steady_clock::now()};
        if (!priv->updateWallet(walletModel->wallet(), notification.hash, notification.status, notification.showTransaction)) {
            wallet_busy = true;
            break;
        }
        if (GUIUtil::NotificationTiming::Enabled())
            GUIUtil::NotificationTiming::Record("transaction_queue", delivered - notification.queued_at);
        std::lock_guard lock{priv->m_notifications_mutex};
        priv->m_notifications.pop_front();
    }
    {
        std::lock_guard lock{priv->m_notifications_mutex};
        if (priv->m_notifications.empty() || !priv->m_loaded || priv->m_loading) {
            priv->m_notification_scheduled = false;
            setProcessingQueuedTransactions(false);
            return;
        }
    }
    // Exactly one continuation is outstanding. Busy wallets are retried
    // without spinning, and without holding up the GUI or the mining worker.
    QTimer::singleShot(wallet_busy ? 20 : 1, this, &TransactionTableModel::processNotifications);
}

void TransactionTableModel::updateConfirmations()
{
    // Blocks came in since last poll.
    // Invalidate status (number of confirmations) and (possibly) description
    //  for all rows. Qt is smart enough to only actually request the data for the
    //  visible rows.
    Q_EMIT dataChanged(index(0, Status), index(priv->size()-1, Status));
    Q_EMIT dataChanged(index(0, ToAddress), index(priv->size()-1, ToAddress));
}

int TransactionTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return priv->size();
}

int TransactionTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return columns.length();
}

QString TransactionTableModel::formatTxStatus(const TransactionRecord *wtx) const
{
    QString status;

    switch(wtx->status.status)
    {
    case TransactionStatus::Unconfirmed:
        status = tr("Unconfirmed");
        break;
    case TransactionStatus::Abandoned:
        status = tr("Abandoned");
        break;
    case TransactionStatus::Confirming:
        status = tr("Confirming (%1 of %2 recommended confirmations)").arg(wtx->status.depth).arg(TransactionRecord::RecommendedNumConfirmations);
        break;
    case TransactionStatus::Confirmed:
        status = tr("Confirmed (%1 confirmations)").arg(wtx->status.depth);
        break;
    case TransactionStatus::Conflicted:
        status = tr("Conflicted");
        break;
    case TransactionStatus::Immature:
        status = tr("Immature (%1 confirmations, will be available after %2)").arg(wtx->status.depth).arg(wtx->status.depth + wtx->status.matures_in);
        break;
    case TransactionStatus::NotAccepted:
        status = tr("Generated but not accepted");
        break;
    }

    return status;
}

QString TransactionTableModel::formatTxDate(const TransactionRecord *wtx) const
{
    if(wtx->time)
    {
        return GUIUtil::dateTimeStr(wtx->time);
    }
    return QString();
}

/* Look up address in address book, if found return label (address)
   otherwise just return (address)
 */
QString TransactionTableModel::lookupAddress(const std::string &address, bool tooltip) const
{
    QString label = walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(address));
    QString description;
    if(!label.isEmpty())
    {
        description += label;
    }
    if(label.isEmpty() || tooltip)
    {
        description += QString(" (") + QString::fromStdString(address) + QString(")");
    }
    return description;
}

QString TransactionTableModel::formatTxType(const TransactionRecord *wtx) const
{
    switch(wtx->type)
    {
    case TransactionRecord::RecvWithAddress:
        return tr("Received with");
    case TransactionRecord::RecvFromOther:
        return tr("Received from");
    case TransactionRecord::SendToAddress:
    case TransactionRecord::SendToOther:
        return tr("Sent to");
    case TransactionRecord::Generated:
        return tr("Mined");
    default:
        return QString();
    }
}

QVariant TransactionTableModel::txAddressDecoration(const TransactionRecord *wtx) const
{
    switch(wtx->type)
    {
    case TransactionRecord::Generated:
        return platformStyle->SingleColorIcon(QStringLiteral(":/icons/tx_mined"));
    case TransactionRecord::RecvWithAddress:
    case TransactionRecord::RecvFromOther:
        return platformStyle->SingleColorIcon(QStringLiteral(":/icons/tx_input"));
    case TransactionRecord::SendToAddress:
    case TransactionRecord::SendToOther:
        return platformStyle->SingleColorIcon(QStringLiteral(":/icons/tx_output"));
    default:
        return platformStyle->SingleColorIcon(QStringLiteral(":/icons/tx_inout"));
    }
}

QString TransactionTableModel::formatTxToAddress(const TransactionRecord *wtx, bool tooltip) const
{
    switch(wtx->type)
    {
    case TransactionRecord::RecvFromOther:
        return QString::fromStdString(wtx->address);
    case TransactionRecord::RecvWithAddress:
    case TransactionRecord::SendToAddress:
    case TransactionRecord::Generated:
        return lookupAddress(wtx->address, tooltip);
    case TransactionRecord::SendToOther:
        return QString::fromStdString(wtx->address);
    default:
        return tr("(n/a)");
    }
}

QVariant TransactionTableModel::addressColor(const TransactionRecord *wtx) const
{
    // Show addresses without label in a less visible color
    switch(wtx->type)
    {
    case TransactionRecord::RecvWithAddress:
    case TransactionRecord::SendToAddress:
    case TransactionRecord::Generated:
        {
        QString label = walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(wtx->address));
        if(label.isEmpty())
            return COLOR_BAREADDRESS;
        } break;
    default:
        break;
    }
    return QVariant();
}

QString TransactionTableModel::formatTxAmount(const TransactionRecord *wtx, bool showUnconfirmed, BitcoinUnits::SeparatorStyle separators) const
{
    QString str = BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), wtx->credit + wtx->debit, false, separators);
    if(showUnconfirmed)
    {
        if(!wtx->status.countsForBalance)
        {
            str = QString("[") + str + QString("]");
        }
    }
    return QString(str);
}

QVariant TransactionTableModel::txStatusDecoration(const TransactionRecord *wtx) const
{
    switch(wtx->status.status)
    {
    case TransactionStatus::Unconfirmed:
        return QIcon(":/icons/transaction_0");
    case TransactionStatus::Abandoned:
        return QIcon(":/icons/transaction_abandoned");
    case TransactionStatus::Confirming:
        switch(wtx->status.depth)
        {
        case 1: return QIcon(":/icons/transaction_1");
        case 2: return QIcon(":/icons/transaction_2");
        case 3: return QIcon(":/icons/transaction_3");
        case 4: return QIcon(":/icons/transaction_4");
        default: return QIcon(":/icons/transaction_5");
        };
    case TransactionStatus::Confirmed:
        return QIcon(":/icons/transaction_confirmed");
    case TransactionStatus::Conflicted:
        return QIcon(":/icons/transaction_conflicted");
    case TransactionStatus::Immature: {
        int total = wtx->status.depth + wtx->status.matures_in;
        int part = (wtx->status.depth * 4 / total) + 1;
        return QIcon(QString(":/icons/transaction_%1").arg(part));
        }
    case TransactionStatus::NotAccepted:
        return QIcon(":/icons/transaction_0");
    default:
        return COLOR_BLACK;
    }
}

QString TransactionTableModel::formatTooltip(const TransactionRecord *rec) const
{
    QString tooltip = formatTxStatus(rec) + QString("\n") + formatTxType(rec);
    if(rec->type==TransactionRecord::RecvFromOther || rec->type==TransactionRecord::SendToOther ||
       rec->type==TransactionRecord::SendToAddress || rec->type==TransactionRecord::RecvWithAddress)
    {
        tooltip += QString(" ") + formatTxToAddress(rec, true);
    }
    return tooltip;
}

QVariant TransactionTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();
    TransactionRecord *rec = static_cast<TransactionRecord*>(index.internalPointer());

    const auto column = static_cast<ColumnIndex>(index.column());
    switch (role) {
    case RawDecorationRole:
        switch (column) {
        case Status:
            return txStatusDecoration(rec);
        case Date: return {};
        case Type: return {};
        case ToAddress:
            return txAddressDecoration(rec);
        case Amount: return {};
        } // no default case, so the compiler can warn about missing cases
        assert(false);
    case Qt::DecorationRole:
    {
        QIcon icon = qvariant_cast<QIcon>(index.data(RawDecorationRole));
        return platformStyle->TextColorIcon(icon);
    }
    case Qt::DisplayRole:
        switch (column) {
        case Status: return {};
        case Date:
            return formatTxDate(rec);
        case Type:
            return formatTxType(rec);
        case ToAddress:
            return formatTxToAddress(rec, false);
        case Amount:
            return formatTxAmount(rec, true, BitcoinUnits::SeparatorStyle::ALWAYS);
        } // no default case, so the compiler can warn about missing cases
        assert(false);
    case Qt::EditRole:
        // Edit role is used for sorting, so return the unformatted values
        switch (column) {
        case Status:
            return QString::fromStdString(rec->status.sortKey);
        case Date:
            return QString::fromStdString(strprintf("%020s-%s", rec->time, rec->status.sortKey));
        case Type:
            return formatTxType(rec);
        case ToAddress:
            return formatTxToAddress(rec, true);
        case Amount:
            return qint64(rec->credit + rec->debit);
        } // no default case, so the compiler can warn about missing cases
        assert(false);
    case Qt::ToolTipRole:
        return formatTooltip(rec);
    case Qt::TextAlignmentRole:
        return column_alignments[index.column()];
    case Qt::ForegroundRole:
        // Use the "danger" color for abandoned transactions
        if(rec->status.status == TransactionStatus::Abandoned)
        {
            return COLOR_TX_STATUS_DANGER;
        }
        // Non-confirmed (but not immature) as transactions are grey
        if(!rec->status.countsForBalance && rec->status.status != TransactionStatus::Immature)
        {
            return COLOR_UNCONFIRMED;
        }
        if(index.column() == Amount && (rec->credit+rec->debit) < 0)
        {
            return COLOR_NEGATIVE;
        }
        if(index.column() == ToAddress)
        {
            return addressColor(rec);
        }
        break;
    case TypeRole:
        return rec->type;
    case DateRole:
        return QDateTime::fromSecsSinceEpoch(rec->time);
    case LongDescriptionRole:
        return priv->describe(walletModel->node(), walletModel->wallet(), rec, walletModel->getOptionsModel()->getDisplayUnit());
    case AddressRole:
        return QString::fromStdString(rec->address);
    case LabelRole:
        return walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(rec->address));
    case AmountRole:
        return qint64(rec->credit + rec->debit);
    case TxHashRole:
        return rec->getTxHash();
    case TxHexRole:
        return priv->getTxHex(walletModel->wallet(), rec);
    case TxPlainTextRole:
        {
            QString details;
            QDateTime date = QDateTime::fromSecsSinceEpoch(rec->time);
            QString txLabel = walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(rec->address));

            details.append(date.toString("M/d/yy HH:mm"));
            details.append(" ");
            details.append(formatTxStatus(rec));
            details.append(". ");
            if(!formatTxType(rec).isEmpty()) {
                details.append(formatTxType(rec));
                details.append(" ");
            }
            if(!rec->address.empty()) {
                if(txLabel.isEmpty())
                    details.append(tr("(no label)") + " ");
                else {
                    details.append("(");
                    details.append(txLabel);
                    details.append(") ");
                }
                details.append(QString::fromStdString(rec->address));
                details.append(" ");
            }
            details.append(formatTxAmount(rec, false, BitcoinUnits::SeparatorStyle::NEVER));
            return details;
        }
    case ConfirmedRole:
        return rec->status.status == TransactionStatus::Status::Confirming || rec->status.status == TransactionStatus::Status::Confirmed;
    case FormattedAmountRole:
        // Used for copy/export, so don't include separators
        return formatTxAmount(rec, false, BitcoinUnits::SeparatorStyle::NEVER);
    case StatusRole:
        return rec->status.status;
    }
    return QVariant();
}

QVariant TransactionTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        if(role == Qt::DisplayRole)
        {
            return columns[section];
        }
        else if (role == Qt::TextAlignmentRole)
        {
            return column_alignments[section];
        } else if (role == Qt::ToolTipRole)
        {
            switch(section)
            {
            case Status:
                return tr("Transaction status. Hover over this field to show number of confirmations.");
            case Date:
                return tr("Date and time that the transaction was received.");
            case Type:
                return tr("Type of transaction.");
            case ToAddress:
                return tr("User-defined intent/purpose of the transaction.");
            case Amount:
                return tr("Amount removed from or added to balance.");
            }
        }
    }
    return QVariant();
}

QModelIndex TransactionTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    TransactionRecord* data = priv->index(walletModel->wallet(), walletModel->getLastBlockProcessed(), row);
    if(data)
    {
        return createIndex(row, column, data);
    }
    return QModelIndex();
}

void TransactionTableModel::updateDisplayUnit()
{
    // emit dataChanged to update Amount column with the current unit
    updateAmountColumnTitle();
    Q_EMIT dataChanged(index(0, Amount), index(priv->size()-1, Amount));
}

void TransactionTablePriv::NotifyTransactionChanged(const Txid& hash, ChangeType status, bool show_transaction)
{
    {
        std::lock_guard lock{m_notifications_mutex};
        if (!parent) return;
        m_notifications.push_back({hash, status, show_transaction});
    }
    qDebug() << "NotifyTransactionChanged: " + QString::fromStdString(hash.GetHex()) + " status= " + QString::number(status);
    DispatchNotifications();
}

void TransactionTablePriv::DispatchNotifications()
{
    std::lock_guard lock{m_notifications_mutex};
    if (!parent || !m_loaded || m_loading || m_notifications.empty() || m_notification_scheduled) return;
    m_notification_scheduled = true;
    const bool invoked{QMetaObject::invokeMethod(parent, "processNotifications", Qt::QueuedConnection)};
    assert(invoked);
}

void TransactionTableModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    m_handler_transaction_changed = walletModel->wallet().handleTransactionChanged([state = priv](const Txid& hash, ChangeType status) {
        state->NotifyTransactionChanged(hash, status);
    });
    m_handler_show_progress = walletModel->wallet().handleShowProgress([state = priv](const std::string&, int progress) {
        state->SetLoading(progress < 100);
    });
}

void TransactionTableModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    m_handler_transaction_changed->disconnect();
    m_handler_show_progress->disconnect();
}
