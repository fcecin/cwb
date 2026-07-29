// Compute control panel: turns the read-only compute:// instance directory into
// a control surface for the L2 compute handler. Launch a source you own, Kill by
// pid -- signed by the browser's fixed identity via WidgetContext. Embed with:
//   <object type="application/x-cwb-compute" width="560" height="200"></object>
// Same pattern as filemgr.cpp: UI -> WidgetContext -> existing ceslib verb.
#include "../credits.h"
#include "../widget.h"
#include "confirm.h"

#include <ces/keys.h>
#include <ces/l2/compute_client.h>
#include <ces/types.h>

#include <QApplication>
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

class ComputePanel : public QWidget {
 public:
  ComputePanel(const cwb::WidgetParams& p, cwb::WidgetContext* ctx,
               QWidget* parent)
      : QWidget(parent), ctx_(ctx) {
    if (ctx_) {
      host_ = ctx_->serverHost().toStdString();
      rpc_ = ctx_->serverRpcPort();
      id_ = ctx_->identity();
    }
    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral(
        "QWidget{background:#ffffff}QPushButton{padding:5px 12px}"));

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 12, 14, 12);
    auto* title = new QLabel(tr("Compute control  —  %1:%2")
                                 .arg(QString::fromStdString(host_))
                                 .arg(rpc_),
                             this);
    title->setStyleSheet(QStringLiteral("font-weight:600;font-size:15px"));
    col->addWidget(title);

    auto* form = new QFormLayout;
    source_ = new QLineEdit(this);
    source_->setPlaceholderText(QStringLiteral("/h/<yourkey>/prog.lua"));
    form->addRow(tr("Launch source:"), source_);
    pid_ = new QLineEdit(this);
    pid_->setPlaceholderText(tr("pid"));
    form->addRow(tr("Kill pid:"), pid_);
    col->addLayout(form);

    auto* row = new QHBoxLayout;
    auto* launch = new QPushButton(tr("Launch"), this);
    launch->setObjectName(QStringLiteral("cwbComputeLaunch"));
    auto* kill = new QPushButton(tr("Kill"), this);
    kill->setObjectName(QStringLiteral("cwbComputeKill"));
    auto* stat = new QPushButton(tr("Stat"), this);
    stat->setObjectName(QStringLiteral("cwbComputeStat"));
    row->addWidget(launch);
    row->addWidget(kill);
    row->addWidget(stat);
    row->addStretch(1);
    col->addLayout(row);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color:#586069"));
    col->addWidget(status_);

    if (auto it = p.params.find(QStringLiteral("source")); it != p.params.end())
      source_->setText(it->second);
    if (auto it = p.params.find(QStringLiteral("pid")); it != p.params.end())
      pid_->setText(it->second);

    cwbw::armConfirm(
        launch,
        [this] { return tr("Confirm launch %1").arg(source_->text().trimmed()); },
        [this] { doLaunch(); });
    cwbw::armConfirm(
        kill, [this] { return tr("Confirm KILL pid %1").arg(pid_->text().trimmed()); },
        [this] { doKill(); });
    connect(stat, &QPushButton::clicked, this, [this] { doStat(); });  // read-only
  }

 private:
  void run(const QString& busy,
           std::function<QString(ces::CesComputeClient&)> op) {
    status_->setText(busy);
    const std::string host = host_;
    const quint16 rpc = rpc_;
    const ces::KeyPair id = id_;
    QPointer<ComputePanel> self(this);
    std::thread([self, host, rpc, id, op]() {
      QString msg;
      try {
        ces::CesComputeClient cc;
        const uint8_t rc = cc.connect(host, rpc, id);
        if (rc != ces::CES_OK)
          msg = QStringLiteral("connect: %1")
                    .arg(QString::fromUtf8(ces::errorString(rc)));
        else {
          msg = op(cc);
          cc.disconnect();
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

  void doLaunch() {
    const std::string src = source_->text().trimmed().toStdString();
    if (src.empty()) {
      status_->setText(tr("enter a source path you own"));
      return;
    }
    run(tr("launching..."), [src](ces::CesComputeClient& cc) {
      uint64_t pid = 0, started = 0;
      const uint8_t rc = cc.launch(src, pid, started);
      if (rc != ces::CES_OK)
        return QStringLiteral("launch: %1")
            .arg(QString::fromUtf8(ces::errorString(rc)));
      return QStringLiteral("launched pid %1").arg(pid);
    });
  }

  void doKill() {
    bool ok = false;
    const uint64_t pid = pid_->text().trimmed().toULongLong(&ok);
    if (!ok || pid == 0) {
      status_->setText(tr("enter a numeric pid"));
      return;
    }
    run(tr("killing..."), [pid](ces::CesComputeClient& cc) {
      const uint8_t rc = cc.kill(pid);
      if (rc != ces::CES_OK)
        return QStringLiteral("kill: %1")
            .arg(QString::fromUtf8(ces::errorString(rc)));
      return QStringLiteral("killed pid %1").arg(pid);
    });
  }

  void doStat() {
    bool ok = false;
    const uint64_t pid = pid_->text().trimmed().toULongLong(&ok);
    if (!ok || pid == 0) {
      status_->setText(tr("enter a numeric pid"));
      return;
    }
    run(tr("querying..."), [pid](ces::CesComputeClient& cc) {
      ces::CesComputeClient::InstanceInfo in;
      const uint8_t rc = cc.stat(pid, in);
      if (rc != ces::CES_OK)
        return QStringLiteral("stat: %1")
            .arg(QString::fromUtf8(ces::errorString(rc)));
      const QString cpu =
          QString::number(in.cpuBasisPoints / 100.0, 'f', 1) + "%";
      return QStringLiteral(
                 "pid %1 · cpu %2 · rss %3 KB · ports %4/%5 · balance %6 credits")
          .arg(in.pid)
          .arg(cpu)
          .arg(in.rssBytes / 1024)
          .arg(in.clientPort)
          .arg(in.rpcPort)
          .arg(cwb::creditsText(static_cast<qint64>(in.fileBalance)));
    });
  }

  cwb::WidgetContext* ctx_ = nullptr;
  std::string host_;
  quint16 rpc_ = 0;
  ces::KeyPair id_;
  QLineEdit* source_ = nullptr;
  QLineEdit* pid_ = nullptr;
  QLabel* status_ = nullptr;
};

QWidget* makeComputePanel(const cwb::WidgetParams& p, cwb::WidgetContext* ctx,
                          QWidget* parent) {
  return new ComputePanel(p, ctx, parent);
}

const cwb::WidgetRegistrar reg_compute("application/x-cwb-compute",
                                       &makeComputePanel);

}  // namespace
