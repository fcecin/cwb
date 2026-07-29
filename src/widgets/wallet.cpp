// Wallet widget: pay and be paid. Shows the browser identity's balance and sends
// a transfer to another pubkey -- a signed main-port op (openTransfer), the one
// capability that rides the main UDP port (needs a PoW-enabled server to verify
// anti-spam tickets; the client produces a cache-mode ticket). Single fixed
// identity is the source; you never pick or see it. Embed with:
//   <object type="application/x-cwb-wallet" width="560" height="220"></object>
#include "../credits.h"
#include "../widget.h"
#include "confirm.h"
#include "style.h"

#include <ces/account.h>
#include <ces/client.h>
#include <ces/keys.h>
#include <ces/types.h>
#include <ces/util/resolver.h>
#include <ces/util/wallet.h>

#include <QApplication>
#include <QClipboard>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include <functional>
#include <string>
#include <thread>

namespace {

using cwb::creditsText;

class Wallet : public QWidget {
 public:
  Wallet(const cwb::WidgetParams& p, cwb::WidgetContext* ctx, QWidget* parent)
      : QWidget(parent), ctx_(ctx) {
    if (ctx_) {
      host_ = ctx_->serverHost().toStdString();
      port_ = ctx_->serverRpcPort();  // on a ces:// page this is the main port
      id_ = ctx_->identity();
    }
    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral(
        "QWidget{background:#ffffff}QPushButton{padding:5px 14px}"));

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 12, 14, 12);
    auto* title = new QLabel(tr("Wallet"), this);
    title->setStyleSheet(QStringLiteral("font-weight:600;font-size:15px"));
    col->addWidget(title);
    balance_ = new QLabel(tr("Balance: querying…"), this);
    balance_->setStyleSheet(QStringLiteral("font-size:15px;color:#1a5fb4"));
    col->addWidget(balance_);

    // Receive: your address (what others pay TO), shown + copyable. The private
    // identity is never shown or selectable; this is only the public key.
    const QString myhex = QString::fromStdString(id_.getPublicKeyHexStr());
    auto* recvRow = new QHBoxLayout;
    auto* recvLbl = new QLabel(tr("Your address: %1").arg(myhex), this);
    recvLbl->setStyleSheet(
        QStringLiteral("font-family:monospace;font-size:11px;color:#586069"));
    recvLbl->setWordWrap(true);
    recvLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* copyBtn = new QPushButton(tr("Copy"), this);
    connect(copyBtn, &QPushButton::clicked, this,
            [myhex] { QApplication::clipboard()->setText(myhex); });
    recvRow->addWidget(recvLbl, 1);
    recvRow->addWidget(copyBtn);
    col->addLayout(recvRow);

    auto* form = new QFormLayout;
    to_ = new QLineEdit(this);
    to_->setPlaceholderText(tr("64-hex recipient public key"));
    form->addRow(tr("To:"), to_);
    amount_ = new QLineEdit(this);
    amount_->setPlaceholderText(tr("amount in credits, e.g. 5"));
    form->addRow(tr("Amount:"), amount_);
    server_ = new QLineEdit(this);
    server_->setPlaceholderText(tr("blank = this server; host:port = cross-server"));
    server_->setToolTip(
        tr("leave blank for a local transfer; a peer's host:port sends "
           "cross-server (this server settles it)"));
    form->addRow(tr("On server:"), server_);
    col->addLayout(form);

    auto* row = new QHBoxLayout;
    auto* send = new QPushButton(tr("Send"), this);
    send->setObjectName(QStringLiteral("cwbWalletSend"));
    send->setCursor(Qt::PointingHandCursor);
    send->setStyleSheet(QLatin1String(cwbw::kPillCss));  // funnel-primary
    row->addWidget(send);
    row->addStretch(1);
    col->addLayout(row);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color:#586069"));
    col->addWidget(status_);

    if (auto it = p.params.find(QStringLiteral("to")); it != p.params.end())
      to_->setText(it->second);
    if (auto it = p.params.find(QStringLiteral("amount")); it != p.params.end())
      amount_->setText(it->second);
    if (auto it = p.params.find(QStringLiteral("onserver")); it != p.params.end())
      server_->setText(it->second);
    // A page not on the main port (e.g. a test file:// page) can name it here;
    // on the ces:// account page the context port is already the main port.
    if (auto it = p.params.find(QStringLiteral("mainport")); it != p.params.end())
      port_ = static_cast<quint16>(it->second.toUShort());

    cwbw::armConfirm(
        send,
        [this] {
          const QString srv = server_->text().trimmed();
          return srv.isEmpty()
                     ? tr("Confirm: send %1 → %2…")
                           .arg(amount_->text().trimmed(),
                                to_->text().trimmed().left(10))
                     : tr("Confirm: cross-send %1 → %2… on %3")
                           .arg(amount_->text().trimmed(),
                                to_->text().trimmed().left(10), srv);
        },
        [this] { doSend(); });
    refreshBalance();
  }

 private:
  boost::asio::ip::udp::endpoint endpoint() const {
    return ces::Resolver::resolveUdp(host_ + ":" + std::to_string(port_));
  }

  void refreshBalance() {
    const std::string host = host_;
    const quint16 port = port_;
    const ces::KeyPair id = id_;
    QPointer<Wallet> self(this);
    std::thread([self, host, port, id]() {
      QString text;
      try {
        ces::Hash h;
        minx::stringToHash(h, id.getPublicKeyHexStr());
        const auto ep =
            ces::Resolver::resolveUdp(host + ":" + std::to_string(port));
        ces::ClientSession sess(true, static_cast<uint16_t>(0), ep, nullptr, 2);
        int64_t bal = 0;
        uint32_t nonce = 0;
        const uint8_t rc =
            sess.client().queryAccount(ces::Account::getMapKey(h), bal, nonce);
        text = (rc == ces::CES_OK)
                   ? QStringLiteral("Balance: %1 credits").arg(creditsText(bal))
                   : QStringLiteral("Balance: unavailable (%1)")
                         .arg(QString::fromUtf8(ces::errorString(rc)));
      } catch (...) {
        text = QStringLiteral("Balance: unavailable");
      }
      QMetaObject::invokeMethod(
          qApp,
          [self, text]() {
            if (self) self->balance_->setText(text);
          },
          Qt::QueuedConnection);
    }).detach();
  }

  void doSend() {
    const QString toHex = to_->text().trimmed();
    if (toHex.size() != 64) {
      status_->setText(tr("recipient must be a 64-hex public key"));
      return;
    }
    quint64 units = 0;
    if (!cwb::parseCredits(amount_->text(), units) || units == 0) {
      status_->setText(tr("enter a positive amount in credits, e.g. 0.5"));
      return;
    }
    status_->setText(tr("sending…"));
    const std::string host = host_;
    const quint16 port = port_;
    const ces::KeyPair id = id_;
    const std::string dest = toHex.toStdString();
    const std::string destServer = server_->text().trimmed().toStdString();
    QPointer<Wallet> self(this);
    std::thread([self, host, port, id, dest, units, destServer]() {
      QString msg;
      try {
        ces::Hash d;
        minx::stringToHash(d, dest);
        const auto ep =
            ces::Resolver::resolveUdp(host + ":" + std::to_string(port));
        ces::ClientSession sess(true, static_cast<uint16_t>(0), ep, &id, 3);
        int64_t newBal = 0;
        // Cross-server when a peer address is named; else a local transfer.
        // The origin (this) server settles the cross to the named peer.
        const uint8_t rc =
            destServer.empty()
                ? sess.client().openTransfer(d, units, newBal)
                : sess.client().crossTransfer(d, units, destServer, newBal);
        const char* verb = destServer.empty() ? "sent" : "cross-sent";
        msg = (rc == ces::CES_OK)
                  ? QStringLiteral("%1 — your balance is now %2 credits")
                        .arg(QLatin1String(verb), creditsText(newBal))
                  : QStringLiteral("transfer failed: %1")
                        .arg(QString::fromUtf8(ces::errorString(rc)));
      } catch (...) {
        msg = QStringLiteral("transfer failed");
      }
      QMetaObject::invokeMethod(
          qApp,
          [self, msg]() {
            if (self) {
              self->status_->setText(msg);
              self->refreshBalance();
            }
          },
          Qt::QueuedConnection);
    }).detach();
  }

  cwb::WidgetContext* ctx_ = nullptr;
  std::string host_;
  quint16 port_ = 0;
  ces::KeyPair id_;
  QLabel* balance_ = nullptr;
  QLineEdit* to_ = nullptr;
  QLineEdit* amount_ = nullptr;
  QLineEdit* server_ = nullptr;
  QLabel* status_ = nullptr;
};

QWidget* makeWallet(const cwb::WidgetParams& p, cwb::WidgetContext* ctx,
                    QWidget* parent) {
  return new Wallet(p, ctx, parent);
}

const cwb::WidgetRegistrar reg_wallet("application/x-cwb-wallet", &makeWallet);

}  // namespace
