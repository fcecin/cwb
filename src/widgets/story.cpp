// Story vitals bar, embedded below every published article:
//   <object type="application/x-cwb-story" width="644" height="150">
//     <param>: path, appname, home, appsource, title
// Everyone sees the rent fund + fundable address and can feed it (DEPOSIT is
// any-signer). The author also gets List/Unlist (feed), Unpublish (server
// delete; local copy retired so upkeep won't revive it), and Delete (every
// recorded remote AND the local work tree).
#include "../widget.h"
#include "confirm.h"
#include "style.h"

#include "../cesdial.h"
#include "../credits.h"
#include "../worktree.h"

#include <ces/l2/compute_client.h>
#include <ces/l2/file_client.h>
#include <ces/types.h>

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

#include <string>
#include <thread>
#include <vector>

namespace {

const char* kQuietBtnCss =
    "QPushButton{background:#ffffff;color:#6b6b6b;border:1px solid #d6d6d4;"
    "border-radius:15px;padding:5px 16px;font-family:sans-serif;font-size:12px}"
    "QPushButton:hover{color:#242424;border-color:#b3b3b1}"
    "QPushButton:pressed{color:#242424;padding:6px 15px 4px 17px}"
    "QPushButton:disabled{color:#c9c9c7;border-color:#ececeb}";

const char* kDangerBtnCss =
    "QPushButton{background:#ffffff;color:#b3312d;border:1px solid #e4c7c6;"
    "border-radius:15px;padding:5px 16px;font-family:sans-serif;font-size:12px}"
    "QPushButton:hover{color:#8f1f1c;border-color:#c98f8d}"
    "QPushButton:pressed{color:#8f1f1c;padding:6px 15px 4px 17px}"
    "QPushButton:disabled{color:#c9c9c7;border-color:#ececeb}";

std::string toHex(const std::array<uint8_t, 32>& a) {
  static const char* d = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (uint8_t b : a) {
    out += d[b >> 4];
    out += d[b & 15];
  }
  return out;
}

// Send one raw command line to the live Vellum instance. "" on transport
// success (`resp` filled; may still be an application error string).
std::string vellumCmd(const std::string& host, uint16_t rpc,
                      const ces::KeyPair& id, const std::string& line,
                      std::string& resp) {
  ces::CesComputeClient cc;
  if (cc.connect(host, rpc, id) != ces::CES_OK) return "no compute lane";
  std::vector<ces::CesComputeClient::InstanceInfo> vi;
  const uint8_t rc = cc.instances("/s/vellum.lua", vi);
  cc.disconnect();
  if (rc != ces::CES_OK || vi.empty()) return "no live Vellum here";
  uint8_t as = 0;
  return cwb::cesLuaFetch(host, rpc, vi.front().pid, id, line, resp, as);
}

class StoryBar : public QWidget {
 public:
  StoryBar(const cwb::WidgetParams& p, cwb::WidgetContext* ctx, QWidget* parent)
      : QWidget(parent), ctx_(ctx) {
    const auto get = [&](const char* k) {
      auto it = p.params.find(QLatin1String(k));
      return it == p.params.end() ? QString() : it->second;
    };
    zonePath_ = get("path");
    product_ = get("appname");
    appHome_ = get("home");
    appSource_ = get("appsource");
    title_ = get("title");
    if (ctx_) {
      host_ = ctx_->serverHost().toStdString();
      rpc_ = ctx_->serverRpcPort();
      id_ = ctx_->identity();
      authorName_ = ctx_->authorName();
    }
    const int cut = zonePath_.lastIndexOf('/');
    slug_ = zonePath_.mid(cut + 1);
    if (slug_.endsWith(QLatin1String(".html"))) slug_.chop(5);
    address_ = QStringLiteral("file://%1:%2%3")
                   .arg(QString::fromStdString(host_))
                   .arg(rpc_)
                   .arg(zonePath_);

    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral("background:#ffffff"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 10, 0, 8);
    col->setSpacing(6);

    fund_ = new QLabel(tr("story fund: …"), this);
    fund_->setStyleSheet(QStringLiteral(
        "font-family:sans-serif;font-size:13px;color:#6b6b6b"));
    col->addWidget(fund_);

    auto* addr = new QLabel(address_, this);
    addr->setStyleSheet(QStringLiteral(
        "font-family:monospace;font-size:12px;color:#9c9c9a"));
    addr->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addr->setToolTip(tr("The story's address: anyone can fund it here"));
    col->addWidget(addr);

    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    amount_ = new QLineEdit(QStringLiteral("0.01"), this);
    amount_->setFixedWidth(110);
    amount_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{0,12}(\\.[0-9]{0,8})?")),
        amount_));
    amount_->setStyleSheet(QStringLiteral(
        "QLineEdit{font-family:sans-serif;font-size:12px;color:#242424;"
        "border:1px solid #d6d6d4;border-radius:4px;padding:4px 8px}"));
    amount_->setToolTip(tr("Credits to add to the story's rent fund"));
    row->addWidget(amount_);
    fundBtn_ = new QPushButton(tr("Fund"), this);
    fundBtn_->setCursor(Qt::PointingHandCursor);
    fundBtn_->setStyleSheet(QLatin1String(cwbw::kPillCss));
    fundBtn_->setEnabled(false);
    row->addWidget(fundBtn_);
    status_ = new QLabel(this);
    status_->setStyleSheet(QStringLiteral(
        "font-family:sans-serif;font-size:12px;color:#9c9c9a"));
    row->addSpacing(6);
    row->addWidget(status_, 1);

    listBtn_ = new QPushButton(tr("Unlist"), this);
    unpubBtn_ = new QPushButton(tr("Unpublish"), this);
    delBtn_ = new QPushButton(tr("Delete"), this);
    for (QPushButton* b : {listBtn_, unpubBtn_, delBtn_}) {
      b->setCursor(Qt::PointingHandCursor);
      b->setVisible(false);
      row->addWidget(b);
    }
    listBtn_->setStyleSheet(QLatin1String(kQuietBtnCss));
    unpubBtn_->setStyleSheet(QLatin1String(kQuietBtnCss));
    delBtn_->setStyleSheet(QLatin1String(kDangerBtnCss));
    listBtn_->setToolTip(tr("Show or hide this story in the app's feed"));
    unpubBtn_->setToolTip(
        tr("Remove the story from this server; your local copy stays"));
    delBtn_->setToolTip(
        tr("Delete the story from every server AND your local work copy"));
    col->addLayout(row);
    col->addStretch(1);

    connect(fundBtn_, &QPushButton::clicked, this, [this] { doFund(); });
    connect(listBtn_, &QPushButton::clicked, this, [this] { doToggleList(); });
    cwbw::armConfirm(
        unpubBtn_, [this] { return tr("Really remove?"); },
        [this] { doRemove(false); });
    cwbw::armConfirm(
        delBtn_, [this] { return tr("Everywhere + local?"); },
        [this] { doRemove(true); });

    if (zonePath_.startsWith(QLatin1Char('/')) && !host_.empty() && rpc_ != 0)
      refresh();
    else
      fund_->setText(tr("story fund: unavailable"));
  }

 private:
  void refresh() {
    const std::string host = host_;
    const quint16 rpc = rpc_;
    const ces::KeyPair id = id_;
    const std::string path = zonePath_.toStdString();
    const std::string myHex = id.getPublicKeyHexStr();
    QPointer<StoryBar> self(this);
    std::thread([self, host, rpc, id, path, myHex]() {
      uint64_t balance = 0;
      bool ok = false, owner = false, listed = false, feed = false;
      try {
        ces::CesFileClient fc;
        if (fc.connect(host, rpc, id) == ces::CES_OK) {
          ces::CesFileClient::StatInfo si{};
          if (fc.stat(path, si) == ces::CES_OK) {
            ok = true;
            balance = si.fileBalance;
            owner = (toHex(si.ownerPubkey) == myHex);
          }
          fc.disconnect();
        }
        if (owner) {
          std::string resp;
          if (vellumCmd(host, rpc, id, "listed|" + path + "\n", resp)
                  .empty()) {
            feed = true;
            listed = (resp.rfind("yes", 0) == 0);
          }
        }
      } catch (...) {
      }
      QMetaObject::invokeMethod(
          qApp,
          [self, ok, balance, owner, listed, feed]() {
            if (!self) return;
            self->fundBtn_->setEnabled(ok);
            if (!ok) {
              self->fund_->setText(tr("story fund: unavailable"));
              return;
            }
            self->fund_->setText(tr("story fund: %1 credits")
                                     .arg(cwb::creditsText(
                                         static_cast<qint64>(balance))));
            self->listed_ = listed;
            self->hasFeed_ = feed;
            if (owner) {
              self->listBtn_->setVisible(feed);
              self->listBtn_->setText(listed ? tr("Unlist") : tr("List"));
              self->unpubBtn_->setVisible(true);
              self->delBtn_->setVisible(true);
            }
          },
          Qt::QueuedConnection);
    }).detach();
  }

  void doFund() {
    quint64 amt = 0;
    if (!cwb::parseCredits(amount_->text(), amt) || amt == 0) {
      status_->setText(tr("enter an amount in credits"));
      return;
    }
    fundBtn_->setEnabled(false);
    status_->setText(tr("feeding…"));
    const std::string host = host_;
    const quint16 rpc = rpc_;
    const ces::KeyPair id = id_;
    const std::string path = zonePath_.toStdString();
    QPointer<StoryBar> self(this);
    std::thread([self, host, rpc, id, path, amt]() {
      QString msg;
      uint64_t nb = 0;
      bool ok = false;
      try {
        ces::CesFileClient fc;
        uint8_t rc = fc.connect(host, rpc, id);
        if (rc == ces::CES_OK) {
          rc = fc.deposit(path, amt, nb);
          fc.disconnect();
        }
        ok = (rc == ces::CES_OK);
        if (!ok) msg = QString::fromUtf8(ces::errorString(rc));
      } catch (...) {
        msg = tr("fund failed");
      }
      QMetaObject::invokeMethod(
          qApp,
          [self, ok, nb, msg]() {
            if (!self) return;
            self->fundBtn_->setEnabled(true);
            if (ok) {
              self->fund_->setText(
                  tr("story fund: %1 credits")
                      .arg(cwb::creditsText(static_cast<qint64>(nb))));
              self->status_->setText(tr("fed"));
            } else {
              self->status_->setText(msg);
            }
          },
          Qt::QueuedConnection);
    }).detach();
  }

  void doToggleList() {
    listBtn_->setEnabled(false);
    const bool wantListed = !listed_;
    const std::string host = host_;
    const quint16 rpc = rpc_;
    const ces::KeyPair id = id_;
    const std::string path = zonePath_.toStdString();
    QString t = title_.isEmpty() ? cwb::workTitle(zonePath_) : title_;
    if (t.isEmpty()) t = slug_;
    t.replace(QLatin1Char('|'), QLatin1Char(' '));
    QString an = authorName_;
    an.replace(QLatin1Char('|'), QLatin1Char(' '));
    const std::string line =
        wantListed ? "published|" + path + "|" + t.toUtf8().toStdString() +
                         "|" + an.toUtf8().toStdString() + "\n"
                   : "unlist|" + path + "\n";
    QPointer<StoryBar> self(this);
    std::thread([self, host, rpc, id, line, wantListed]() {
      std::string resp;
      const std::string err = vellumCmd(host, rpc, id, line, resp);
      const bool ok = err.empty() && resp.rfind("ok", 0) == 0;
      const QString detail = QString::fromStdString(err.empty() ? resp : err)
                                 .trimmed();
      QMetaObject::invokeMethod(
          qApp,
          [self, ok, wantListed, detail]() {
            if (!self) return;
            self->listBtn_->setEnabled(true);
            if (ok) {
              self->listed_ = wantListed;
              self->listBtn_->setText(wantListed ? tr("Unlist") : tr("List"));
              self->status_->setText(wantListed ? tr("listed in the feed")
                                                : tr("unlisted"));
            } else {
              self->status_->setText(detail);
            }
          },
          Qt::QueuedConnection);
    }).detach();
  }

  // After the story page is gone: the live shelf (stable dynamic-app
  // address), app home as fallback.
  QString afterRemoveUrl() const {
    const QString handle = zonePath_.section('/', 2, 2);
    if (!appSource_.isEmpty() && zonePath_.startsWith(QLatin1String("/f/")) &&
        !handle.isEmpty())
      return QStringLiteral("compute://%1:%2%3/by/%4")
          .arg(QString::fromStdString(host_))
          .arg(rpc_)
          .arg(appSource_, handle);
    return appHome_;
  }

  // deep=false: delete from THIS server, retire the local copy. deep=true:
  // also every recorded remote + the local copy. No index maintenance: the
  // live shelf prunes itself against the file store.
  void doRemove(bool deep) {
    unpubBtn_->setEnabled(false);
    delBtn_->setEnabled(false);
    status_->setText(deep ? tr("deleting…") : tr("unpublishing…"));
    const std::string host = host_;
    const quint16 rpc = rpc_;
    const ces::KeyPair id = id_;
    const QString zonePath = zonePath_;
    const std::string path = zonePath.toStdString();
    const bool unlistFirst = hasFeed_ && listed_;
    const QString nextUrl = afterRemoveUrl();
    cwb::WidgetContext* ctx = ctx_;
    QPointer<StoryBar> self(this);
    std::thread([self, ctx, host, rpc, id, zonePath, path, deep, unlistFirst,
                 nextUrl]() {
      QString msg;
      bool ok = false;
      try {
        if (unlistFirst) {
          std::string resp;
          vellumCmd(host, rpc, id, "unlist|" + path + "\n", resp);
        }
        ces::CesFileClient fc;
        uint8_t rc = fc.connect(host, rpc, id);
        if (rc == ces::CES_OK) {
          uint64_t refunded = 0;
          rc = fc.deleteFile(path, refunded);
          if (rc == ces::CES_ERROR_FILE_NOT_FOUND) rc = ces::CES_OK;
          fc.disconnect();
        }
        if (rc != ces::CES_OK) {
          msg = QString::fromUtf8(ces::errorString(rc));
        } else if (!deep) {
          cwb::workSetRetired(zonePath, true);
          ok = true;
        } else {
          // Every other recorded remote, best-effort: the local work copy is
          // about to go away, so leave no orphan replicas paying rent.
          const QString self0 = QStringLiteral("%1:%2")
                                    .arg(QString::fromStdString(host))
                                    .arg(rpc);
          for (const QString& r : cwb::workRemotes(zonePath)) {
            if (r == self0) continue;
            const QString rh = r.section(':', 0, -2);
            const quint16 rp = static_cast<quint16>(r.section(':', -1).toUInt());
            if (rh.isEmpty() || rp == 0) continue;
            try {
              ces::CesFileClient rf;
              if (rf.connect(rh.toStdString(), rp, id) == ces::CES_OK) {
                uint64_t rr = 0;
                rf.deleteFile(path, rr);
                rf.disconnect();
              }
            } catch (...) {
            }
          }
          ok = cwb::workDelete(zonePath);
          if (!ok) msg = tr("server copy removed; local delete failed");
        }
      } catch (...) {
        msg = tr("remove failed");
      }
      QMetaObject::invokeMethod(
          qApp,
          [self, ctx, ok, msg, deep, nextUrl]() {
            if (self) {
              self->unpubBtn_->setEnabled(true);
              self->delBtn_->setEnabled(true);
              self->status_->setText(
                  ok ? (deep ? tr("deleted") : tr("unpublished")) : msg);
            }
            // The page under our feet is gone; go stand on the live shelf.
            if (ok && ctx && !nextUrl.isEmpty()) ctx->navigateTo(nextUrl);
          },
          Qt::QueuedConnection);
    }).detach();
  }

  cwb::WidgetContext* ctx_ = nullptr;
  std::string host_;
  quint16 rpc_ = 0;
  ces::KeyPair id_;
  QString authorName_;
  QString zonePath_, product_, appHome_, appSource_, title_, slug_, address_;
  bool listed_ = false;
  bool hasFeed_ = false;
  QLabel* fund_ = nullptr;
  QLabel* status_ = nullptr;
  QLineEdit* amount_ = nullptr;
  QPushButton* fundBtn_ = nullptr;
  QPushButton* listBtn_ = nullptr;
  QPushButton* unpubBtn_ = nullptr;
  QPushButton* delBtn_ = nullptr;
};

QWidget* makeStory(const cwb::WidgetParams& p, cwb::WidgetContext* ctx,
                   QWidget* parent) {
  return new StoryBar(p, ctx, parent);
}

const cwb::WidgetRegistrar reg_story("application/x-cwb-story", &makeStory);

}  // namespace
