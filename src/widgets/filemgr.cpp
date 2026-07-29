// File-manager widget: turns a file:// zone page into a working client for the
// L2 file store. Upload (CREATE + WRITE), Deposit (fund rent), Delete -- signed
// by the browser's fixed identity via WidgetContext. Embed with:
//   <object type="application/x-cwb-files" width="560" height="360"></object>
// The verbs already exist in ceslib (CesFileClient); this wires UI to them.
#include "../credits.h"
#include "../widget.h"
#include "confirm.h"

#include <ces/keys.h>
#include <ces/l2/file_client.h>
#include <ces/types.h>

#include <QApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include <functional>
#include <string>
#include <thread>

namespace {

class FileManager : public QWidget {
 public:
  FileManager(const cwb::WidgetParams& p, cwb::WidgetContext* ctx,
              QWidget* parent)
      : QWidget(parent), ctx_(ctx) {
    if (ctx_) {
      host_ = ctx_->serverHost().toStdString();
      rpc_ = ctx_->serverRpcPort();
      id_ = ctx_->identity();
    }
    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral(
        "QWidget{background:#ffffff}"
        "QPushButton{padding:5px 12px}"));

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 12, 14, 12);
    auto* title = new QLabel(
        tr("File manager  —  %1:%2")
            .arg(QString::fromStdString(host_))
            .arg(rpc_),
        this);
    title->setStyleSheet(QStringLiteral("font-weight:600;font-size:15px"));
    col->addWidget(title);

    auto* form = new QFormLayout;
    path_ = new QLineEdit(this);
    path_->setPlaceholderText(QStringLiteral("/p/hello.txt"));
    form->addRow(tr("Path:"), path_);
    content_ = new QPlainTextEdit(this);
    content_->setPlaceholderText(tr("file contents (text)"));
    content_->setMaximumHeight(120);
    form->addRow(tr("Content:"), content_);
    deposit_ = new QLineEdit(QStringLiteral("1"), this);
    deposit_->setToolTip(tr("rent deposit / withdraw amount, in credits"));
    form->addRow(tr("Deposit/Withdraw (credits):"), deposit_);
    price_ = new QLineEdit(QStringLiteral("0"), this);
    price_->setToolTip(tr("read price per KB, in credits"));
    form->addRow(tr("Read price/KB (credits):"), price_);
    col->addLayout(form);

    // Optional pre-fill from <param> (used by pages that stage an action, and by
    // the headless shot harness).
    if (auto it = p.params.find(QStringLiteral("path")); it != p.params.end())
      path_->setText(it->second);
    if (auto it = p.params.find(QStringLiteral("content")); it != p.params.end())
      content_->setPlainText(it->second);
    if (auto it = p.params.find(QStringLiteral("deposit")); it != p.params.end())
      deposit_->setText(it->second);

    auto* row = new QHBoxLayout;
    auto* up = new QPushButton(tr("Upload"), this);
    up->setObjectName(QStringLiteral("cwbFilesUpload"));
    auto* dep = new QPushButton(tr("Deposit"), this);
    auto* wd = new QPushButton(tr("Withdraw"), this);
    wd->setObjectName(QStringLiteral("cwbFilesWithdraw"));
    auto* sp = new QPushButton(tr("Set price"), this);
    sp->setObjectName(QStringLiteral("cwbFilesSetPrice"));
    auto* del = new QPushButton(tr("Delete"), this);
    row->addWidget(up);
    row->addWidget(dep);
    row->addWidget(wd);
    row->addWidget(sp);
    row->addWidget(del);
    row->addStretch(1);
    col->addLayout(row);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color:#586069"));
    col->addWidget(status_);

    cwbw::armConfirm(
        up, [this] { return tr("Confirm upload %1").arg(path_->text().trimmed()); },
        [this] { doUpload(); });
    cwbw::armConfirm(
        dep,
        [this] {
          return tr("Confirm deposit %1").arg(deposit_->text().trimmed());
        },
        [this] { doDeposit(); });
    cwbw::armConfirm(
        wd,
        [this] {
          return tr("Confirm withdraw %1 credits").arg(deposit_->text().trimmed());
        },
        [this] { doWithdraw(); });
    cwbw::armConfirm(
        sp,
        [this] {
          return tr("Confirm price %1/KB").arg(price_->text().trimmed());
        },
        [this] { doSetPrice(); });
    cwbw::armConfirm(
        del, [this] { return tr("Confirm DELETE %1").arg(path_->text().trimmed()); },
        [this] { doDelete(); });
  }

 private:
  // Run `op` on a worker thread against a freshly bound file client, marshaling
  // the resulting status message back to the label. Never lets a throw escape.
  void run(const QString& busy,
           std::function<QString(ces::CesFileClient&)> op) {
    status_->setText(busy);
    const std::string host = host_;
    const quint16 rpc = rpc_;
    const ces::KeyPair id = id_;
    QPointer<FileManager> self(this);
    std::thread([self, host, rpc, id, op]() {
      QString msg;
      try {
        ces::CesFileClient fc;
        const uint8_t rc = fc.connect(host, rpc, id);
        if (rc != ces::CES_OK)
          msg = QStringLiteral("connect: %1")
                    .arg(QString::fromUtf8(ces::errorString(rc)));
        else {
          msg = op(fc);
          fc.disconnect();
        }
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

  static QString err(const char* what, uint8_t rc) {
    return QStringLiteral("%1: %2").arg(QLatin1String(what),
                                        QString::fromUtf8(ces::errorString(rc)));
  }

  void doUpload() {
    const std::string path = path_->text().trimmed().toStdString();
    if (path.empty()) {
      status_->setText(tr("enter a path (e.g. /p/hello.txt)"));
      return;
    }
    const QByteArray body = content_->toPlainText().toUtf8();
    ces::Bytes bytes(reinterpret_cast<const uint8_t*>(body.constData()),
                     reinterpret_cast<const uint8_t*>(body.constData()) +
                         body.size());
    quint64 deposit = 0;
    if (!cwb::parseCredits(deposit_->text(), deposit)) {
      status_->setText(tr("deposit must be credits, e.g. 0.05"));
      return;
    }
    run(tr("uploading..."), [path, bytes, deposit](ces::CesFileClient& fc) {
      uint64_t fbal = 0, cost = 0;
      uint8_t rc = fc.create(path, bytes.size(), 0, deposit, fbal, cost);
      if (rc != ces::CES_OK && rc != ces::CES_ERROR_FILE_EXISTS)
        return err("create", rc);
      if (!bytes.empty()) {
        uint64_t wb = 0;
        rc = fc.write(path, 0, bytes, wb);
        if (rc != ces::CES_OK) return err("write", rc);
        fbal = wb;
      }
      return QStringLiteral("uploaded %1 bytes (file balance %2 credits)")
          .arg(bytes.size())
          .arg(cwb::creditsText(static_cast<qint64>(fbal)));
    });
  }

  void doDeposit() {
    const std::string path = path_->text().trimmed().toStdString();
    if (path.empty()) {
      status_->setText(tr("enter a path"));
      return;
    }
    quint64 amount = 0;
    if (!cwb::parseCredits(deposit_->text(), amount)) {
      status_->setText(tr("amount must be credits, e.g. 0.05"));
      return;
    }
    run(tr("depositing..."), [path, amount](ces::CesFileClient& fc) {
      uint64_t fbal = 0;
      const uint8_t rc = fc.deposit(path, amount, fbal);
      if (rc != ces::CES_OK) return err("deposit", rc);
      return QStringLiteral("deposited (file balance %1 credits)")
          .arg(cwb::creditsText(static_cast<qint64>(fbal)));
    });
  }

  void doDelete() {
    const std::string path = path_->text().trimmed().toStdString();
    if (path.empty()) {
      status_->setText(tr("enter a path"));
      return;
    }
    run(tr("deleting..."), [path](ces::CesFileClient& fc) {
      uint64_t refunded = 0;
      const uint8_t rc = fc.deleteFile(path, refunded);
      if (rc != ces::CES_OK) return err("delete", rc);
      return QStringLiteral("deleted (refunded %1 credits)")
          .arg(cwb::creditsText(static_cast<qint64>(refunded)));
    });
  }

  void doWithdraw() {
    const std::string path = path_->text().trimmed().toStdString();
    if (path.empty()) {
      status_->setText(tr("enter a path"));
      return;
    }
    quint64 amount = 0;
    if (!cwb::parseCredits(deposit_->text(), amount)) {
      status_->setText(tr("amount must be credits, e.g. 0.05"));
      return;
    }
    run(tr("withdrawing..."), [path, amount](ces::CesFileClient& fc) {
      uint64_t fbal = 0;
      const uint8_t rc = fc.withdraw(path, amount, fbal);
      if (rc != ces::CES_OK) return err("withdraw", rc);
      return QStringLiteral("withdrew (file balance %1 credits)")
          .arg(cwb::creditsText(static_cast<qint64>(fbal)));
    });
  }

  void doSetPrice() {
    const std::string path = path_->text().trimmed().toStdString();
    if (path.empty()) {
      status_->setText(tr("enter a path"));
      return;
    }
    quint64 price = 0;
    if (!cwb::parseCredits(price_->text(), price)) {
      status_->setText(tr("price must be credits/KB, e.g. 0.001"));
      return;
    }
    run(tr("setting price..."), [path, price](ces::CesFileClient& fc) {
      uint64_t outPrice = 0;
      const uint8_t rc = fc.setPrice(path, price, outPrice);
      if (rc != ces::CES_OK) return err("set price", rc);
      return QStringLiteral("price set");
    });
  }

  cwb::WidgetContext* ctx_ = nullptr;
  std::string host_;
  quint16 rpc_ = 0;
  ces::KeyPair id_;
  QLineEdit* path_ = nullptr;
  QPlainTextEdit* content_ = nullptr;
  QLineEdit* deposit_ = nullptr;
  QLineEdit* price_ = nullptr;
  QLabel* status_ = nullptr;
};

QWidget* makeFileManager(const cwb::WidgetParams& p, cwb::WidgetContext* ctx,
                         QWidget* parent) {
  return new FileManager(p, ctx, parent);
}

const cwb::WidgetRegistrar reg_files("application/x-cwb-files", &makeFileManager);

}  // namespace
