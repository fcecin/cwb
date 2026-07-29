// Asset editor widget: create/query/fund L1 assets -- the generative primitive
// (files, programs, namespaces all compose out of assets). Create and Fund are
// signed main-port ops (like the wallet, need a PoW server); Query is unsigned.
// Embed with:
//   <object type="application/x-cwb-asset" width="560" height="260"></object>
#include "../widget.h"
#include "confirm.h"

#include <ces/asset.h>
#include <ces/client.h>
#include <ces/keys.h>
#include <ces/types.h>
#include <ces/util/resolver.h>
#include <ces/util/wallet.h>

#include <QApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <string>
#include <thread>

namespace {

class AssetEditor : public QWidget {
 public:
  AssetEditor(const cwb::WidgetParams& p, cwb::WidgetContext* ctx,
              QWidget* parent)
      : QWidget(parent), ctx_(ctx) {
    if (ctx_) {
      host_ = ctx_->serverHost().toStdString();
      port_ = ctx_->serverRpcPort();  // ces:// page -> the main port
      id_ = ctx_->identity();
    }
    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral(
        "QWidget{background:#ffffff}QPushButton{padding:5px 12px}"));

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 12, 14, 12);
    auto* title = new QLabel(tr("Assets"), this);
    title->setStyleSheet(QStringLiteral("font-weight:600;font-size:15px"));
    col->addWidget(title);

    auto* form = new QFormLayout;
    key_ = new QLineEdit(this);
    key_->setPlaceholderText(tr("64-hex asset key"));
    form->addRow(tr("Key:"), key_);
    content_ = new QPlainTextEdit(this);
    content_->setPlaceholderText(tr("content (up to 210 bytes)"));
    content_->setMaximumHeight(80);
    form->addRow(tr("Content:"), content_);
    days_ = new QLineEdit(QStringLiteral("30"), this);
    form->addRow(tr("Rent days:"), days_);
    col->addLayout(form);

    auto* row = new QHBoxLayout;
    auto* create = new QPushButton(tr("Create"), this);
    create->setObjectName(QStringLiteral("cwbAssetCreate"));
    auto* query = new QPushButton(tr("Query"), this);
    query->setObjectName(QStringLiteral("cwbAssetQuery"));
    auto* fund = new QPushButton(tr("Fund"), this);
    row->addWidget(create);
    row->addWidget(query);
    row->addWidget(fund);
    row->addStretch(1);
    col->addLayout(row);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color:#586069"));
    col->addWidget(status_);

    if (auto it = p.params.find(QStringLiteral("key")); it != p.params.end())
      key_->setText(it->second);
    if (auto it = p.params.find(QStringLiteral("content")); it != p.params.end())
      content_->setPlainText(it->second);
    if (auto it = p.params.find(QStringLiteral("mainport")); it != p.params.end())
      port_ = static_cast<quint16>(it->second.toUShort());

    cwbw::armConfirm(
        create,
        [this] { return tr("Confirm create %1").arg(key_->text().trimmed().left(12)); },
        [this] { doCreate(); });
    connect(query, &QPushButton::clicked, this, [this] { doQuery(); });  // read-only
    cwbw::armConfirm(
        fund, [this] { return tr("Confirm fund %1 days").arg(days_->text().trimmed()); },
        [this] { doFund(); });
  }

 private:
  // `signed_` picks a signing session (create/fund) vs an unsigned one (query).
  void run(const QString& busy, bool signed_,
           std::function<QString(ces::CesClient&, const ces::Hash&)> op) {
    const QString keyHex = key_->text().trimmed();
    if (keyHex.size() != 64) {
      status_->setText(tr("key must be 64 hex characters"));
      return;
    }
    status_->setText(busy);
    const std::string host = host_;
    const quint16 port = port_;
    const ces::KeyPair id = id_;
    const std::string kh = keyHex.toStdString();
    QPointer<AssetEditor> self(this);
    std::thread([self, host, port, id, kh, signed_, op]() {
      QString msg;
      try {
        ces::Hash key;
        minx::stringToHash(key, kh);
        const auto ep =
            ces::Resolver::resolveUdp(host + ":" + std::to_string(port));
        ces::ClientSession sess(true, static_cast<uint16_t>(0), ep,
                                signed_ ? &id : nullptr, 3);
        msg = op(sess.client(), key);
      } catch (...) {
        msg = QStringLiteral("operation failed");
      }
      QMetaObject::invokeMethod(
          qApp,
          [self, msg]() {
            if (self) self->status_->setText(msg);
          },
          Qt::QueuedConnection);
    }).detach();
  }

  void doCreate() {
    ces::AssetData content{};  // zero-filled 210 bytes
    const QByteArray body = content_->toPlainText().toUtf8();
    const int n = std::min<int>(static_cast<int>(body.size()), 210);
    for (int i = 0; i < n; ++i)
      content[static_cast<size_t>(i)] = static_cast<uint8_t>(body[i]);
    const uint16_t days =
        static_cast<uint16_t>(days_->text().trimmed().toUShort());
    run(tr("creating…"), true,
        [content, days](ces::CesClient& c, const ces::Hash& key) {
          const uint8_t rc = c.createAsset(key, content, days ? days : 1);
          if (rc != ces::CES_OK)
            return QStringLiteral("create: %1")
                .arg(QString::fromUtf8(ces::errorString(rc)));
          return QStringLiteral("asset created (%1 day rent)").arg(days);
        });
  }

  void doFund() {
    const uint16_t days =
        static_cast<uint16_t>(days_->text().trimmed().toUShort());
    run(tr("funding…"), true,
        [days](ces::CesClient& c, const ces::Hash& key) {
          const uint8_t rc = c.fundAsset(key, days ? days : 1);
          if (rc != ces::CES_OK)
            return QStringLiteral("fund: %1")
                .arg(QString::fromUtf8(ces::errorString(rc)));
          return QStringLiteral("funded +%1 days").arg(days);
        });
  }

  void doQuery() {
    run(tr("querying…"), false, [](ces::CesClient& c, const ces::Hash& key) {
      ces::HashPrefix owner{};
      ces::AssetData content{};
      uint16_t bal = 0;
      uint32_t price = 0;
      const uint8_t rc = c.queryAsset(key, owner, content, bal, price);
      if (rc != ces::CES_OK)
        return QStringLiteral("query: %1")
            .arg(QString::fromUtf8(ces::errorString(rc)));
      int len = 210;
      while (len > 0 && content[static_cast<size_t>(len - 1)] == 0) --len;
      const QString text = QString::fromUtf8(
          reinterpret_cast<const char*>(content.data()), len);
      return QStringLiteral("owner %1 · %2 days · price %3 · content: %4")
          .arg(QString::fromStdString(ces::hashPrefixToString(owner)))
          .arg(bal)
          .arg(price)
          .arg(text);
    });
  }

  cwb::WidgetContext* ctx_ = nullptr;
  std::string host_;
  quint16 port_ = 0;
  ces::KeyPair id_;
  QLineEdit* key_ = nullptr;
  QPlainTextEdit* content_ = nullptr;
  QLineEdit* days_ = nullptr;
  QLabel* status_ = nullptr;
};

QWidget* makeAssetEditor(const cwb::WidgetParams& p, cwb::WidgetContext* ctx,
                         QWidget* parent) {
  return new AssetEditor(p, ctx, parent);
}

const cwb::WidgetRegistrar reg_asset("application/x-cwb-asset", &makeAssetEditor);

}  // namespace
