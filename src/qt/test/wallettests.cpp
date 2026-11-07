// Copyright (c) 2015-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/wallettests.h>
#include <qt/test/util.h>

#include <wallet/coincontrol.h>
#include <interfaces/chain.h>
#include <interfaces/mining.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <mining/solo.h>
#include <policy/policy.h>
#include <qt/bitcoinamountfield.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/coincontroldialog.h>
#include <qt/optionsmodel.h>
#include <qt/overviewpage.h>
#include <qt/platformstyle.h>
#include <qt/qvalidatedlineedit.h>
#include <qt/receivecoinsdialog.h>
#include <qt/receiverequestdialog.h>
#include <qt/recentrequeststablemodel.h>
#include <qt/sendcoinsdialog.h>
#include <qt/sendcoinsentry.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionview.h>
#include <qt/walletmodel.h>
#include <script/solver.h>
#include <test/util/setup_common.h>
#include <validation.h>
#include <wallet/test/util.h>
#include <wallet/wallet.h>
#include <wallet/spend.h>

#include <chrono>
#include <memory>
#include <stdexcept>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QObject>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QListView>
#include <QDialogButtonBox>
#include <QStandardItemModel>
#include <QTreeWidgetItemIterator>

using wallet::AddWallet;
using wallet::CWallet;
using wallet::CreateMockableWalletDatabase;
using wallet::RemoveWallet;
using wallet::WALLET_FLAG_DESCRIPTORS;
using wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS;
using wallet::WalletContext;
using wallet::WalletDescriptor;
using wallet::WalletRescanReserver;

namespace
{
//! Press "Yes" or "Cancel" buttons in modal send confirmation dialog.
void ConfirmSend(QString* text = nullptr, QMessageBox::StandardButton confirm_type = QMessageBox::Yes)
{
    QTimer::singleShot(0, [text, confirm_type]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->inherits("SendConfirmationDialog")) {
                SendConfirmationDialog* dialog = qobject_cast<SendConfirmationDialog*>(widget);
                if (text) *text = dialog->text();
                QAbstractButton* button = dialog->button(confirm_type);
                button->setEnabled(true);
                button->click();
            }
        }
    });
}

//! Send coins to address and return txid.
Txid SendCoins(CWallet& wallet, SendCoinsDialog& sendCoinsDialog, const CTxDestination& address, CAmount amount, bool rbf,
                  QMessageBox::StandardButton confirm_type = QMessageBox::Yes)
{
    QVBoxLayout* entries = sendCoinsDialog.findChild<QVBoxLayout*>("entries");
    SendCoinsEntry* entry = qobject_cast<SendCoinsEntry*>(entries->itemAt(0)->widget());
    entry->findChild<QValidatedLineEdit*>("payTo")->setText(QString::fromStdString(EncodeDestination(address)));
    entry->findChild<BitcoinAmountField*>("payAmount")->setValue(amount);
    sendCoinsDialog.findChild<QFrame*>("frameFee")
        ->findChild<QFrame*>("frameFeeSelection")
        ->findChild<QCheckBox*>("optInRBF")
        ->setCheckState(rbf ? Qt::Checked : Qt::Unchecked);
    Txid txid;
    boost::signals2::scoped_connection c(wallet.NotifyTransactionChanged.connect([&txid](const Txid& hash, ChangeType status) {
        if (status == CT_NEW) txid = hash;
    }));
    ConfirmSend(/*text=*/nullptr, confirm_type);
    bool invoked = QMetaObject::invokeMethod(&sendCoinsDialog, "sendButtonClicked", Q_ARG(bool, false));
    assert(invoked);
    return txid;
}

//! Find index of txid in transaction list.
QModelIndex FindTx(const QAbstractItemModel& model, const Txid& txid)
{
    QString hash = QString::fromStdString(txid.ToString());
    int rows = model.rowCount({});
    for (int row = 0; row < rows; ++row) {
        QModelIndex index = model.index(row, 0, {});
        if (model.data(index, TransactionTableModel::TxHashRole) == hash) {
            return index;
        }
    }
    return {};
}

//! Invoke bumpfee on txid and check results.
void BumpFee(TransactionView& view, const Txid& txid, bool expectDisabled, std::string expectError, bool cancel)
{
    QTableView* table = view.findChild<QTableView*>("transactionView");
    QModelIndex index = FindTx(*table->selectionModel()->model(), txid);
    QVERIFY2(index.isValid(), "Could not find BumpFee txid");

    // Select row in table, invoke context menu, and make sure bumpfee action is
    // enabled or disabled as expected.
    QAction* action = view.findChild<QAction*>("bumpFeeAction");
    table->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    action->setEnabled(expectDisabled);
    table->customContextMenuRequested({});
    QCOMPARE(action->isEnabled(), !expectDisabled);

    action->setEnabled(true);
    QString text;
    if (expectError.empty()) {
        ConfirmSend(&text, cancel ? QMessageBox::Cancel : QMessageBox::Yes);
    } else {
        ConfirmMessage(&text, 0ms);
    }
    action->trigger();
    QVERIFY(text.indexOf(QString::fromStdString(expectError)) != -1);
}

void CompareBalance(WalletModel& walletModel, CAmount expected_balance, QLabel* balance_label_to_check)
{
    BitcoinUnit unit = walletModel.getOptionsModel()->getDisplayUnit();
    QString balanceComparison = BitcoinUnits::formatWithUnit(unit, expected_balance, false, BitcoinUnits::SeparatorStyle::ALWAYS);
    QCOMPARE(balance_label_to_check->text().trimmed(), balanceComparison);
}

// Verify the 'useAvailableBalance' functionality. With and without manually selected coins.
// Case 1: No coin control selected coins.
// 'useAvailableBalance' should fill the amount edit box with the total available balance
// Case 2: With coin control selected coins.
// 'useAvailableBalance' should fill the amount edit box with the sum of the selected coins values.
void VerifyUseAvailableBalance(SendCoinsDialog& sendCoinsDialog, const WalletModel& walletModel)
{
    // Verify first entry amount and "useAvailableBalance" button
    QVBoxLayout* entries = sendCoinsDialog.findChild<QVBoxLayout*>("entries");
    QVERIFY(entries->count() == 1); // only one entry
    SendCoinsEntry* send_entry = qobject_cast<SendCoinsEntry*>(entries->itemAt(0)->widget());
    QVERIFY(send_entry->getValue().amount == 0);
    // Now click "useAvailableBalance", check updated balance (the entire wallet balance should be set)
    Q_EMIT send_entry->useAvailableBalance(send_entry);
    QVERIFY(send_entry->getValue().amount == walletModel.getCachedBalance().balance);

    // Now manually select two coins and click on "useAvailableBalance". Then check updated balance
    // (only the sum of the selected coins should be set).
    int COINS_TO_SELECT = 2;
    auto coins = walletModel.wallet().listCoins();
    CAmount sum_selected_coins = 0;
    int selected = 0;
    QVERIFY(coins.size() == 1); // context check, coins received only on one destination
    for (const auto& [outpoint, tx_out] : coins.begin()->second) {
        sendCoinsDialog.getCoinControl()->Select(outpoint);
        sum_selected_coins += tx_out.txout.nValue;
        if (++selected == COINS_TO_SELECT) break;
    }
    QVERIFY(selected == COINS_TO_SELECT);

    // Now that we have 2 coins selected, "useAvailableBalance" should update the balance label only with
    // the sum of them.
    Q_EMIT send_entry->useAvailableBalance(send_entry);
    QVERIFY(send_entry->getValue().amount == sum_selected_coins);
}

void SyncUpWallet(const std::shared_ptr<CWallet>& wallet, interfaces::Node& node)
{
    WalletRescanReserver reserver(*wallet);
    reserver.reserve();
    CWallet::ScanResult result = wallet->ScanForWalletTransactions(Params().GetConsensus().hashGenesisBlock, /*start_height=*/0, /*max_height=*/{}, reserver, /*fUpdate=*/true, /*save_progress=*/false);
    QCOMPARE(result.status, CWallet::ScanResult::SUCCESS);
    QCOMPARE(result.last_scanned_block, WITH_LOCK(node.context()->chainman->GetMutex(), return node.context()->chainman->ActiveChain().Tip()->GetBlockHash()));
    QVERIFY(result.last_failed_block.IsNull());
}

std::shared_ptr<CWallet> SetupDescriptorsWallet(interfaces::Node& node, TestChain100Setup& test, bool watch_only = false)
{
    std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>(node.context()->chain.get(), "", CreateMockableWalletDatabase());
    LOCK(wallet->cs_wallet);
    wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
    if (watch_only) {
        wallet->SetWalletFlag(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
    } else {
        wallet->SetupDescriptorScriptPubKeyMans();
        Assert(wallet->InitializeMercaturaPQWallet());
    }

    if (watch_only) {
        // Retain the inherited classical descriptor only for the explicit
        // watch-only PSBT fixture.
        FlatSigningProvider provider;
        std::string error;
        const std::string key_str{HexStr(test.coinbaseKey.GetPubKey())};
        auto descs = Parse("combo(" + key_str + ")", provider, error, /* require_checksum=*/ false);
        assert(!descs.empty());
        assert(descs.size() == 1);
        auto& desc = descs.at(0);
        WalletDescriptor w_desc(std::move(desc), 0, 0, 1, 1);
        Assert(wallet->AddWalletDescriptor(w_desc, provider, "", false));
        const PKHash dest{test.coinbaseKey.GetPubKey()};
        wallet->SetAddressBook(dest, "", wallet::AddressPurpose::RECEIVE);
    }

    wallet->SetLastBlockProcessed(
        WITH_LOCK(node.context()->chainman->GetMutex(), return node.context()->chainman->ActiveChain().Height()),
        WITH_LOCK(node.context()->chainman->GetMutex(), return node.context()->chainman->ActiveChain().Tip()->GetBlockHash()));
    SyncUpWallet(wallet, node);
    wallet->SetBroadcastTransactions(true);
    return wallet;
}

struct MiniGUI {
public:
    SendCoinsDialog sendCoinsDialog;
    TransactionView transactionView;
    OptionsModel optionsModel;
    std::unique_ptr<ClientModel> clientModel;
    std::unique_ptr<WalletModel> walletModel;

    MiniGUI(interfaces::Node& node, const PlatformStyle* platformStyle) : sendCoinsDialog(platformStyle), transactionView(platformStyle), optionsModel(node) {
        bilingual_str error;
        QVERIFY(optionsModel.Init(error));
        clientModel = std::make_unique<ClientModel>(node, &optionsModel);
    }

    void initModelForWallet(interfaces::Node& node, const std::shared_ptr<CWallet>& wallet, const PlatformStyle* platformStyle)
    {
        WalletContext& context = *node.walletLoader().context();
        AddWallet(context, wallet);
        walletModel = std::make_unique<WalletModel>(interfaces::MakeWallet(context, wallet), *clientModel, platformStyle);
        RemoveWallet(context, wallet, /* load_on_start= */ std::nullopt);
        sendCoinsDialog.setModel(walletModel.get());
        transactionView.setModel(walletModel.get());
    }

};

//! Simple qt wallet tests.
//
// Test widgets can be debugged interactively calling show() on them and
// manually running the event loop, e.g.:
//
//     sendCoinsDialog.show();
//     QEventLoop().exec();
//
// This also requires overriding the default minimal Qt platform:
//
//     QT_QPA_PLATFORM=xcb     build/bin/test_bitcoin-qt  # Linux
//     QT_QPA_PLATFORM=windows build/bin/test_bitcoin-qt  # Windows
//     QT_QPA_PLATFORM=cocoa   build/bin/test_bitcoin-qt  # macOS
void TestGUI(interfaces::Node& node, const std::shared_ptr<CWallet>& wallet)
{
    // Create widgets for sending coins and listing transactions.
    std::unique_ptr<const PlatformStyle> platformStyle(PlatformStyle::instantiate("other"));
    MiniGUI mini_gui(node, platformStyle.get());
    mini_gui.initModelForWallet(node, wallet, platformStyle.get());
    WalletModel& walletModel = *mini_gui.walletModel;
    SendCoinsDialog& sendCoinsDialog = mini_gui.sendCoinsDialog;
    TransactionView& transactionView = mini_gui.transactionView;

    // Update walletModel cached balance which will trigger an update for the 'labelBalance' QLabel.
    walletModel.pollBalanceChanged();
    // Check balance in send dialog
    CompareBalance(walletModel, walletModel.wallet().getBalance(), sendCoinsDialog.findChild<QLabel*>("labelBalance"));

    // Check 'UseAvailableBalance' functionality
    VerifyUseAvailableBalance(sendCoinsDialog, walletModel);

    // Send two transactions, and verify they are added to transaction list.
    TransactionTableModel* transactionTableModel = walletModel.getTransactionTableModel();
    QCOMPARE(transactionTableModel->rowCount({}), 105);
    const CTxDestination external_pq_recipient{
        WitnessV2MercaturaPQ{}
    };
    Txid txid1 = SendCoins(*wallet.get(), sendCoinsDialog, external_pq_recipient, 5 * COIN, /*rbf=*/false);
    Txid txid2 = SendCoins(*wallet.get(), sendCoinsDialog, external_pq_recipient, 10 * COIN, /*rbf=*/true);
    // Transaction table model updates on a QueuedConnection, so process events to ensure it's updated.
    qApp->processEvents();
    QCOMPARE(transactionTableModel->rowCount({}), 107);
    QVERIFY(FindTx(*transactionTableModel, txid1).isValid());
    QVERIFY(FindTx(*transactionTableModel, txid2).isValid());

    // Call bumpfee. Test canceled fullrbf bump, canceled bip-125-rbf bump, passing bump, and then failing bump.
    BumpFee(transactionView, txid1, /*expectDisabled=*/false, /*expectError=*/{}, /*cancel=*/true);
    BumpFee(transactionView, txid2, /*expectDisabled=*/false, /*expectError=*/{}, /*cancel=*/true);
    BumpFee(transactionView, txid2, /*expectDisabled=*/false, /*expectError=*/{}, /*cancel=*/false);
    BumpFee(transactionView, txid2, /*expectDisabled=*/true, /*expectError=*/"already bumped", /*cancel=*/false);

    // Check current balance on OverviewPage
    OverviewPage overviewPage(platformStyle.get());
    overviewPage.setWalletModel(&walletModel);
    walletModel.pollBalanceChanged(); // Manual balance polling update
    CompareBalance(walletModel, walletModel.wallet().getBalance(), overviewPage.findChild<QLabel*>("labelBalance"));

    // Check Request Payment button
    ReceiveCoinsDialog receiveCoinsDialog(platformStyle.get());
    receiveCoinsDialog.setModel(&walletModel);
    RecentRequestsTableModel* requestTableModel = walletModel.getRecentRequestsTableModel();

    // Label input
    QLineEdit* labelInput = receiveCoinsDialog.findChild<QLineEdit*>("reqLabel");
    labelInput->setText("TEST_LABEL_1");

    // Amount input
    BitcoinAmountField* amountInput = receiveCoinsDialog.findChild<BitcoinAmountField*>("reqAmount");
    amountInput->setValue(1);

    // Message input
    QLineEdit* messageInput = receiveCoinsDialog.findChild<QLineEdit*>("reqMessage");
    messageInput->setText("TEST_MESSAGE_1");
    int initialRowCount = requestTableModel->rowCount({});
    QPushButton* requestPaymentButton = receiveCoinsDialog.findChild<QPushButton*>("receiveButton");
    requestPaymentButton->click();
    QString address;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->inherits("ReceiveRequestDialog")) {
            ReceiveRequestDialog* receiveRequestDialog = qobject_cast<ReceiveRequestDialog*>(widget);
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("payment_header")->text(), QString("Payment information"));
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("uri_tag")->text(), QString("URI:"));
            QString uri = receiveRequestDialog->QObject::findChild<QLabel*>("uri_content")->text();
            QCOMPARE(uri.count("mercatura:"), 2);
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("address_tag")->text(), QString("Address:"));
            QVERIFY(address.isEmpty());
            address = receiveRequestDialog->QObject::findChild<QLabel*>("address_content")->text();
            QVERIFY(!address.isEmpty());

            QCOMPARE(uri.count("amount=0.01"), 2);
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("amount_tag")->text(), QString("Amount:"));
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("amount_content")->text(), QString::fromStdString("0.01 " + CURRENCY_UNIT));

            QCOMPARE(uri.count("label=TEST_LABEL_1"), 2);
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("label_tag")->text(), QString("Label:"));
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("label_content")->text(), QString("TEST_LABEL_1"));

            QCOMPARE(uri.count("message=TEST_MESSAGE_1"), 2);
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("message_tag")->text(), QString("Message:"));
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("message_content")->text(), QString("TEST_MESSAGE_1"));
        }
    }

    // Clear button
    QPushButton* clearButton = receiveCoinsDialog.findChild<QPushButton*>("clearButton");
    clearButton->click();
    QCOMPARE(labelInput->text(), QString(""));
    QCOMPARE(amountInput->value(), CAmount(0));
    QCOMPARE(messageInput->text(), QString(""));

    // Check addition to history
    int currentRowCount = requestTableModel->rowCount({});
    QCOMPARE(currentRowCount, initialRowCount+1);

    // Check addition to wallet
    std::vector<std::string> requests = walletModel.wallet().getAddressReceiveRequests();
    QCOMPARE(requests.size(), size_t{1});
    RecentRequestEntry entry;
    SpanReader{MakeByteSpan(requests[0])} >> entry;
    QCOMPARE(entry.nVersion, int{1});
    QCOMPARE(entry.id, int64_t{1});
    QVERIFY(entry.date.isValid());
    QCOMPARE(entry.recipient.address, address);
    QCOMPARE(entry.recipient.label, QString{"TEST_LABEL_1"});
    QCOMPARE(entry.recipient.amount, CAmount{1});
    QCOMPARE(entry.recipient.message, QString{"TEST_MESSAGE_1"});
    QCOMPARE(entry.recipient.sPaymentRequest, std::string{});
    QCOMPARE(entry.recipient.authenticatedMerchant, QString{});

    // Check Remove button
    QTableView* table = receiveCoinsDialog.findChild<QTableView*>("recentRequestsView");
    table->selectRow(currentRowCount-1);
    QPushButton* removeRequestButton = receiveCoinsDialog.findChild<QPushButton*>("removeRequestButton");
    removeRequestButton->click();
    QCOMPARE(requestTableModel->rowCount({}), currentRowCount-1);

    // Check removal from wallet
    QCOMPARE(walletModel.wallet().getAddressReceiveRequests().size(), size_t{0});
}

void TestGUIWatchOnly(interfaces::Node& node, TestChain100Setup& test)
{
    const std::shared_ptr<CWallet>& wallet = SetupDescriptorsWallet(node, test, /*watch_only=*/true);

    // Create widgets and init models
    std::unique_ptr<const PlatformStyle> platformStyle(PlatformStyle::instantiate("other"));
    MiniGUI mini_gui(node, platformStyle.get());
    mini_gui.initModelForWallet(node, wallet, platformStyle.get());
    WalletModel& walletModel = *mini_gui.walletModel;
    SendCoinsDialog& sendCoinsDialog = mini_gui.sendCoinsDialog;

    // Update walletModel cached balance which will trigger an update for the 'labelBalance' QLabel.
    walletModel.pollBalanceChanged();
    // Check balance in send dialog
    CompareBalance(walletModel, walletModel.wallet().getBalances().balance,
                   sendCoinsDialog.findChild<QLabel*>("labelBalance"));

    // Set change address
    sendCoinsDialog.getCoinControl()->destChange = PKHash{test.coinbaseKey.GetPubKey()};

    // Time to reject "save" PSBT dialog ('SendCoins' locks the main thread until the dialog receives the event).
    QTimer timer;
    timer.setInterval(500);
    QObject::connect(&timer, &QTimer::timeout, [&](){
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->inherits("QMessageBox") && widget->objectName().compare("psbt_copied_message") == 0) {
                QMessageBox* dialog = qobject_cast<QMessageBox*>(widget);
                QAbstractButton* button = dialog->button(QMessageBox::Discard);
                button->setEnabled(true);
                button->click();
                timer.stop();
                break;
            }
        }
    });
    timer.start(500);

    // Send tx and verify PSBT copied to the clipboard.
    SendCoins(*wallet.get(), sendCoinsDialog, PKHash(), 5 * COIN, /*rbf=*/false, QMessageBox::Save);
    const std::string& psbt_string = QApplication::clipboard()->text().toStdString();
    QVERIFY(!psbt_string.empty());

    // Decode psbt
    std::optional<std::vector<unsigned char>> decoded_psbt = DecodeBase64(psbt_string);
    QVERIFY(decoded_psbt);
    PartiallySignedTransaction psbt;
    std::string err;
    QVERIFY(DecodeRawPSBT(psbt, MakeByteSpan(*decoded_psbt), err));
}

void TestGUI(interfaces::Node& node)
{
    // Set up wallet and chain with 105 blocks (5 mature blocks for spending).
    TestChain100Setup test;
    for (int i = 0; i < 5; ++i) {
        test.CreateAndProcessBlock({}, GetScriptForRawPubKey(test.coinbaseKey.GetPubKey()));
    }
    auto wallet_loader = interfaces::MakeWalletLoader(*test.m_node.chain, *Assert(test.m_node.args));
    test.m_node.wallet_loader = wallet_loader.get();
    node.setContext(&test.m_node);

    // "Full" GUI tests, use descriptor wallet
    const std::shared_ptr<CWallet>& desc_wallet = SetupDescriptorsWallet(node, test);

    // Fund the full GUI wallet through Mercatura's native PQ ownership path.
    // Mine 105 blocks to one wallet-owned PQ destination so the first few
    // coinbases are mature and spendable, while preserving the historical
    // 105-row transaction-table fixture.
    auto pq_funding_destination{
        desc_wallet->GetNewDestination(
            OutputType::BECH32M,
            "qt-pq-funding")
    };
    QVERIFY(pq_funding_destination);
    QVERIFY(
        std::get_if<WitnessV2MercaturaPQ>(
            &*pq_funding_destination) != nullptr);

    for (int i = 0; i < 105; ++i) {
        test.CreateAndProcessBlock(
            {},
            GetScriptForDestination(
                *pq_funding_destination));
    }

    desc_wallet->SetLastBlockProcessed(
        WITH_LOCK(test.m_node.chainman->GetMutex(), return test.m_node.chainman->ActiveChain().Height()),
        WITH_LOCK(test.m_node.chainman->GetMutex(), return test.m_node.chainman->ActiveChain().Tip()->GetBlockHash()));
    SyncUpWallet(desc_wallet, node);

    TestGUI(node, desc_wallet);

    // Legacy watch-only wallet test
    // Verify PSBT creation.
    TestGUIWatchOnly(node, test);
}

} // namespace

void WalletTests::walletTests()
{
#ifdef Q_OS_MACOS
    if (QApplication::platformName() == "minimal") {
        // Disable for mac on "minimal" platform to avoid crashes inside the Qt
        // framework when it tries to look up unimplemented cocoa functions,
        // and fails to handle returned nulls
        // (https://bugreports.qt.io/browse/QTBUG-49686).
        qWarning() << "Skipping WalletTests on mac build with 'minimal' platform set due to Qt bugs. To run AppTests, invoke "
                      "with 'QT_QPA_PLATFORM=cocoa test_bitcoin-qt' on mac, or else use a linux or windows build.";
        return;
    }
#endif
    TestGUI(m_node);
}

void WalletTests::mercaweaveReceiveAndSendTests()
{
    TestChain100Setup test{ChainType::REGTEST, {.extra_args={"-mercaweaveheight=101"}}};
    auto loader = interfaces::MakeWalletLoader(*test.m_node.chain, *Assert(test.m_node.args));
    test.m_node.wallet_loader = loader.get();
    m_node.setContext(&test.m_node);
    const auto wallet = SetupDescriptorsWallet(m_node, test);
    const std::unique_ptr<const PlatformStyle> style{PlatformStyle::instantiate("other")};
    MiniGUI gui(m_node, style.get());
    gui.initModelForWallet(m_node, wallet, style.get());
    ReceiveCoinsDialog dialog(style.get());
    dialog.setModel(gui.walletModel.get());
    auto* selector = dialog.findChild<QComboBox*>("receiveAddressType");
    auto* button = dialog.findChild<QPushButton*>("receiveButton");
    QVERIFY(selector && button);
    QCOMPARE(selector->count(), 2);
    QCOMPARE(selector->currentIndex(), 0);
    QCOMPARE(selector->itemText(0), QString{"Standard PQ — Witness v2"});
    QCOMPARE(selector->itemText(1), QString{"MercaWeave Grouped PQ — Witness v3"});
    auto* items = qobject_cast<QStandardItemModel*>(selector->model());
    auto* spending = gui.sendCoinsDialog.findChild<QComboBox*>("spendingMode");
    QVERIFY(spending && !spending->isHidden());
    QCOMPARE(spending->count(), 3);
    QCOMPARE(spending->currentIndex(), 0);
    QCOMPARE(spending->itemText(0), QString{"Automatic (Recommended)"});
    QCOMPARE(spending->itemText(1), QString{"Standard PQ — Witness v2 only"});
    QCOMPARE(spending->itemText(2), QString{"MercaWeave — Witness v3 only"});
    QVERIFY(!qobject_cast<QStandardItemModel*>(spending->model())->item(2)->isEnabled());
    spending->setCurrentIndex(2);
    QVERIFY(!gui.sendCoinsDialog.findChild<QPushButton*>("sendButton")->isEnabled());
    spending->setCurrentIndex(0);
    QVERIFY(items && !items->item(1)->isEnabled());
    QVERIFY(button->isEnabled());
    selector->setCurrentIndex(1);
    QVERIFY(!button->isEnabled());
    QVERIFY(!gui.walletModel->wallet().getNewMercaWeaveDestination("premature"));

    test.CreateAndProcessBlock({}, CScript{} << OP_TRUE);
    wallet->SetLastBlockProcessed(101, test.m_node.chainman->ActiveChain().Tip()->GetBlockHash());
    SyncUpWallet(wallet, m_node);
    QVERIFY(QMetaObject::invokeMethod(&dialog, "updateAddressTypes"));
    QVERIFY(items->item(1)->isEnabled());
    QVERIFY(button->isEnabled());
    dialog.findChild<QLineEdit*>("reqLabel")->setText("Grouped request");
    dialog.findChild<QLineEdit*>("reqMessage")->setText("Native receive workflow");
    const auto capture = qEnvironmentVariable("MERCAWEAVE_QT_CAPTURE");
    if (!capture.isEmpty()) {
        dialog.resize(800, 500);
        dialog.show();
        qApp->processEvents();
        QVERIFY(dialog.grab().save(capture));
    }
    const int before = gui.walletModel->getRecentRequestsTableModel()->rowCount({});
    button->click();
    QCOMPARE(gui.walletModel->getRecentRequestsTableModel()->rowCount({}), before + 1);
    const auto requests = gui.walletModel->wallet().getAddressReceiveRequests();
    QCOMPARE(requests.size(), size_t{1});
    RecentRequestEntry request;
    SpanReader{MakeByteSpan(requests[0])} >> request;
    QCOMPARE(request.recipient.label, QString{"Grouped request"});
    const auto destination = DecodeDestination(request.recipient.address.toStdString());
    int version;
    std::vector<unsigned char> program;
    QVERIFY(GetScriptForDestination(destination).IsWitnessProgram(version, program));
    QCOMPARE(version, 3);
    QCOMPARE(program.size(), size_t{32});
    QVERIFY(wallet->GetPQGroupRecord(GetScriptForDestination(destination)));
    bool request_shown{false};
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* request_dialog = qobject_cast<ReceiveRequestDialog*>(widget)) {
            request_shown = true;
            QCOMPARE(request_dialog->findChild<QLabel*>("address_content")->text(), request.recipient.address);
            QVERIFY(QMetaObject::invokeMethod(request_dialog, "on_btnCopyAddress_clicked"));
            QCOMPARE(QApplication::clipboard()->text(), request.recipient.address);
            request_dialog->close();
        }
    }
    QVERIFY(request_shown);
    spending->setCurrentIndex(2);
    auto& chainstate = test.m_node.chainman->ActiveChainstate();
    auto* activated_tip = WITH_LOCK(cs_main, return test.m_node.chainman->ActiveChain().Tip());
    BlockValidationState state;
    QVERIFY(chainstate.InvalidateBlock(state, activated_tip));
    QVERIFY(chainstate.ActivateBestChain(state));
    QCOMPARE(WITH_LOCK(cs_main, return test.m_node.chainman->ActiveChain().Height()), 100);
    QTRY_VERIFY_WITH_TIMEOUT(!button->isEnabled(), 3000);
    QTRY_VERIFY(!qobject_cast<QStandardItemModel*>(spending->model())->item(2)->isEnabled());
    QCOMPARE(spending->currentIndex(), 2);
    QVERIFY(!gui.sendCoinsDialog.findChild<QPushButton*>("sendButton")->isEnabled());
    QCOMPARE(selector->currentIndex(), 1);
    QVERIFY(!items->item(1)->isEnabled());
    {
        LOCK(cs_main);
        chainstate.ResetBlockFailureFlags(activated_tip);
        test.m_node.chainman->RecalculateBestHeader();
    }
    BlockValidationState restored;
    QVERIFY(chainstate.ActivateBestChain(restored));
    QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 3000);

    QTRY_VERIFY(qobject_cast<QStandardItemModel*>(spending->model())->item(2)->isEnabled());
    spending->setCurrentIndex(0);

    // Exercise normal Qt Send with real matured native v2/v3 mining rewards.
    // No fixture capability, injected coins or classical authorization is used.
    const auto standard = wallet->GetNewDestination(OutputType::BECH32M, "qt-mixed-funding");
    const auto change = wallet->GetNewPQGroupDestination("qt-group-change", 0, 1);
    QVERIFY(standard && change);
    {
        mining::SoloWorkProvider provider{interfaces::MakeMining(test.m_node, false),
            GetScriptForDestination(destination), EncodeDestination(destination), true};
        const auto job = provider.GetJob();
        QVERIFY(job && job->coinbase);
        QCOMPARE(job->height, int64_t{102});
        QVERIFY(job->coinbase->vout[0].scriptPubKey == GetScriptForDestination(destination));
    }
    bool malformed_reward_rejected{false};
    try {
        (void)mining::SoloWorkProvider{interfaces::MakeMining(test.m_node, false),
            CScript{} << OP_3 << std::vector<unsigned char>(16, 1), "", true};
    } catch (const std::runtime_error&) {
        malformed_reward_rejected = true;
    }
    QVERIFY(malformed_reward_rejected);
    std::vector<COutPoint> funding;
    CAmount available{0};
    for (int i = 0; i < 105; ++i) {
        const auto block = test.CreateAndProcessBlock({},
            GetScriptForDestination(i == 0 || i == 2 ? destination : *standard));
        if (i < 3) {
            funding.emplace_back(block.vtx[0]->GetHash(), 0);
            available += block.vtx[0]->vout[0].nValue;
        }
    }
    QVERIFY(available > 1000000);
    QCOMPARE(WITH_LOCK(cs_main, return test.m_node.chainman->ActiveChain().Height()), 206);
    wallet->SetLastBlockProcessed(206,
        WITH_LOCK(cs_main, return test.m_node.chainman->ActiveChain().Tip()->GetBlockHash()));
    SyncUpWallet(wallet, m_node);
    gui.walletModel->pollBalanceChanged();
    for (const auto mode : {wallet::PQSpendingMode::STANDARD_V2, wallet::PQSpendingMode::MERCAWEAVE_V3}) {
        wallet::CCoinControl policy;
        policy.m_spending_mode = mode;
        const auto recipient = mode == wallet::PQSpendingMode::STANDARD_V2 ? destination : *standard;
        const std::vector<wallet::CRecipient> recipients{{recipient, available / 4, false}};
        const auto result = gui.walletModel->wallet().createTransaction(recipients, policy, true, std::nullopt);
        QVERIFY(result);
        std::vector<COutPoint> points;
        for (const auto& input : result->tx->vin) points.push_back(input.prevout);
        for (const auto& coin : gui.walletModel->wallet().getCoins(points)) {
            QVERIFY(wallet::MatchesPQSpendingMode(coin.txout.scriptPubKey, mode));
        }
        QVERIFY(result->change_pos);
        QVERIFY(result->tx->vout[*result->change_pos].scriptPubKey.IsWitnessProgram(version, program));
        QCOMPARE(version, 2);
        policy.Select(mode == wallet::PQSpendingMode::STANDARD_V2 ? funding[0] : funding[1]);
        const auto conflict = gui.walletModel->wallet().createTransaction(recipients, policy, true, std::nullopt);
        QVERIFY(!conflict);
        QVERIFY(util::ErrorString(conflict).original.find("conflicts with the spending mode") != std::string::npos);
        QVERIFY(policy.HasSelected());
    }
    auto* control = gui.sendCoinsDialog.getCoinControl();
    spending->setCurrentIndex(1);
    QCOMPARE(static_cast<int>(control->m_spending_mode), static_cast<int>(wallet::PQSpendingMode::STANDARD_V2));
    {
        CoinControlDialog coins(*control, gui.walletModel.get(), style.get());
        auto* filter = coins.findChild<QComboBox*>("inputTypeFilter");
        auto* tree = coins.findChild<QTreeWidget*>("treeWidget");
        QVERIFY(filter && tree);
        filter->setCurrentIndex(0);
        bool v2_found{false}, v3_found{false};
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if ((*it)->text(6) == "Standard PQ (v2)") v2_found = true;
            if ((*it)->text(6) == "MercaWeave (v3)") {
                v3_found = true;
                QVERIFY(!((*it)->flags() & Qt::ItemIsUserCheckable));
                (*it)->setCheckState(0, Qt::Checked);
                QVERIFY(!control->HasSelected());
            }
        }
        QVERIFY(v2_found && v3_found);
        filter->setCurrentIndex(2);
        QVERIFY(control->m_spending_mode == wallet::PQSpendingMode::STANDARD_V2);
    }
    control->Select(funding[0]);
    CoinControlDialog::updateLabels(*control, gui.walletModel.get(), &gui.sendCoinsDialog);
    QVERIFY(control->IsSelected(funding[0]));
    spending->setCurrentIndex(0);
    control->Select(funding[0]);
    control->Select(funding[1]);
    control->Select(funding[2]);
    control->m_allow_other_inputs = false;
    control->destChange = *change;
    CoinControlDialog::payAmounts = {available / 4};
    CoinControlDialog::updateLabels(*control, gui.walletModel.get(), &gui.sendCoinsDialog);
    const auto displayed_bytes = gui.sendCoinsDialog.findChild<QLabel*>("labelCoinControlBytes")->text();
    bool bytes_ok{false};
    const auto estimated_bytes = displayed_bytes.mid(1).toULongLong(&bytes_ok);
    QVERIFY(bytes_ok);
    QString send_error;
    const auto error_handler = connect(&gui.sendCoinsDialog, &SendCoinsDialog::message, &dialog,
        [&send_error](const QString&, const QString& message, unsigned int) { send_error = message; });
    const auto txid = SendCoins(*wallet, gui.sendCoinsDialog, WitnessV2MercaturaPQ{}, available / 4, false);
    disconnect(error_handler);
    QVERIFY2(txid != Txid{}, qPrintable(send_error));
    CTransactionRef sent;
    {
        LOCK(wallet->cs_wallet);
        const auto* recorded = wallet->GetWalletTx(txid);
        QVERIFY(recorded);
        sent = recorded->tx;
    }
    QCOMPARE(sent->vin.size(), size_t{3});
    QCOMPARE(estimated_bytes, static_cast<qulonglong>(GetTransactionFeeSize(*sent)));
    int anchors{0}, followers{0};
    bool native_input{false}, grouped_change{false};
    for (const auto& input : sent->vin) {
        if (input.prevout == funding[0] || input.prevout == funding[2]) {
            if (input.scriptWitness.stack.size() == 3) {
                QCOMPARE(input.scriptWitness.stack[0].size(), size_t{3309});
                ++anchors;
            } else {
                QCOMPARE(input.scriptWitness.stack.size(), size_t{2});
                QCOMPARE(input.scriptWitness.stack[0].size(), size_t{32});
                QCOMPARE(input.scriptWitness.stack[1].size(), size_t{4});
                ++followers;
            }
        } else {
            QVERIFY(input.prevout == funding[1]);
            QCOMPARE(input.scriptWitness.stack.size(), size_t{2});
            native_input = true;
        }
    }
    for (const auto& output : sent->vout) grouped_change |= output.scriptPubKey == GetScriptForDestination(*change);
    QCOMPARE(anchors, 1);
    QCOMPARE(followers, 1);
    QVERIFY(native_input && grouped_change);
    qApp->processEvents();
    QTRY_VERIFY(FindTx(*gui.walletModel->getTransactionTableModel(), txid).isValid());
    const auto confirmed = test.CreateAndProcessBlock({CMutableTransaction{*sent}}, GetScriptForDestination(*standard));
    QVERIFY(WITH_LOCK(cs_main, return test.m_node.chainman->ActiveChain().Tip()->GetBlockHash() == confirmed.GetHash()));

    spending->setCurrentIndex(2);
    QVERIFY(gui.walletModel->wallet().encryptWallet(SecureString{"mode-passphrase"}));
    QVERIFY(gui.walletModel->wallet().isLocked());
    QVERIFY(QMetaObject::invokeMethod(&gui.sendCoinsDialog, "updateSpendingModeAvailability"));
    QVERIFY(gui.sendCoinsDialog.findChild<QLabel*>("spendingModeStatus")->text().contains("locked"));
    {
        auto temporary = std::make_unique<WalletModel>(interfaces::MakeWallet(*m_node.walletLoader().context(), wallet),
                                                      *gui.clientModel, style.get());
        gui.sendCoinsDialog.setModel(temporary.get());
        connect(temporary.get(), &WalletModel::requireUnlock, &gui.sendCoinsDialog, [&] {
            QVERIFY(gui.walletModel->wallet().unlock(SecureString{"mode-passphrase"}));
        });
        const auto context = temporary->requestUnlock();
        QVERIFY(context.isValid());
        temporary.reset();
        QVERIFY(!context.isValid());
        QVERIFY(!gui.sendCoinsDialog.findChild<QPushButton*>("sendButton")->isEnabled());
        QVERIFY(!gui.sendCoinsDialog.findChild<QPushButton*>("pushButtonCoinControl")->isEnabled());
        // Recipient editing and Inputs must remain safe after the model dies.
        auto* entry = gui.sendCoinsDialog.findChild<SendCoinsEntry*>();
        QVERIFY(entry);
        entry->setAddress(QString::fromStdString(EncodeDestination(*standard)));
        QVERIFY(QMetaObject::invokeMethod(&gui.sendCoinsDialog, "coinControlButtonClicked"));
        QVERIFY(gui.sendCoinsDialog.findChildren<CoinControlDialog*>().empty());
    }
    QVERIFY(gui.walletModel->wallet().lock());
    {
        auto temporary = std::make_unique<WalletModel>(interfaces::MakeWallet(*m_node.walletLoader().context(), wallet),
                                                      *gui.clientModel, style.get());
        gui.sendCoinsDialog.setModel(temporary.get());
        connect(temporary.get(), &WalletModel::requireUnlock, &gui.sendCoinsDialog, [&] { temporary.reset(); });
        const auto context = temporary->requestUnlock();
        QVERIFY(!context.isValid());
        QVERIFY(!temporary);
    }
    gui.sendCoinsDialog.setModel(gui.walletModel.get());
    QVERIFY(gui.walletModel->wallet().unlock(SecureString{"mode-passphrase"}));
    QVERIFY(QMetaObject::invokeMethod(&gui.sendCoinsDialog, "updateSpendingModeAvailability"));
    QVERIFY(!gui.sendCoinsDialog.findChild<QLabel*>("spendingModeStatus")->text().contains("locked"));
    gui.sendCoinsDialog.setModel(nullptr);
    QCOMPARE(spending->currentIndex(), 2);
    QVERIFY(!gui.sendCoinsDialog.findChild<QPushButton*>("sendButton")->isEnabled());
    {
        const auto unsupported = SetupDescriptorsWallet(m_node, test, true);
        MiniGUI other(m_node, style.get());
        other.initModelForWallet(m_node, unsupported, style.get());
        gui.sendCoinsDialog.setModel(other.walletModel.get());
        QCOMPARE(spending->currentIndex(), 2);
        QVERIFY(!qobject_cast<QStandardItemModel*>(spending->model())->item(2)->isEnabled());
    }
    gui.sendCoinsDialog.setModel(gui.walletModel.get());
    QCOMPARE(spending->currentIndex(), 2);
    QVERIFY(qobject_cast<QStandardItemModel*>(spending->model())->item(2)->isEnabled());

    // Losing a wallet while the grouped option is selected must disable it
    // without silently issuing a different address type.
    dialog.setModel(nullptr);
    QCOMPARE(selector->currentIndex(), 1);
    QVERIFY(!button->isEnabled());
    QVERIFY(!items->item(1)->isEnabled());
}
