// The identity widget: set your name. Embedded on your own account page
// (context = the main port). One field, one consented Set: registers a
// key_name for your single key on this server (the ledger enforces uniqueness
// both ways and gives you the /f/<name>/ zone). Renaming just registers a new
// name; the same widget adapts.
//   <object type="application/x-cwb-identity" width="560" height="170"></object>
// Params: name (prefill), host/mainport (override, for staged pages/harness).
#include "../widget.h"
#include "confirm.h"
#include "style.h"

#include "../cesidentity.h"
#include "../identityreg.h"
#include "../names.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <thread>

namespace {

class IdentityPanel : public QWidget {
 public:
  IdentityPanel(const cwb::WidgetParams& p, cwb::WidgetContext* ctx,
                QWidget* parent)
      : QWidget(parent), ctx_(ctx) {
    if (ctx_) {
      host_ = ctx_->serverHost();
      port_ = ctx_->serverRpcPort();  // on a ces:// account page this is the main port
    }
    if (auto it = p.params.find(QStringLiteral("host")); it != p.params.end())
      host_ = it->second;
    if (auto it = p.params.find(QStringLiteral("mainport"));
        it != p.params.end())
      port_ = static_cast<quint16>(it->second.toUShort());

    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral("QWidget{background:#ffffff}"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 12, 14, 12);

    const QString existing =
        cwb::prettyName(cwb::canonicalKeyName(cwb::preferredName()));
    const bool named = !existing.isEmpty();
    auto* title = new QLabel(
        named ? tr("You are %1").arg(existing) : tr("Choose your name"), this);
    title->setStyleSheet(QStringLiteral("font-weight:600;font-size:15px"));
    col->addWidget(title);
    auto* hint = new QLabel(
        tr("Your name is how you sign your writing and the /f/ zone your "
           "stories live in (spaces become underscores). It is registered to "
           "your key on this server; the browser keeps it alive."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#6b7280;font-size:12px"));
    col->addWidget(hint);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Name"), this));
    name_ = new QLineEdit(this);
    name_->setPlaceholderText(tr("e.g. Ada Lovelace"));
    name_->setMaxLength(32);
    if (named) name_->setText(existing);
    row->addWidget(name_, 1);
    claim_ = new QPushButton(named ? tr("Rename") : tr("Set name"), this);
    claim_->setObjectName(QStringLiteral("cwbIdentityClaim"));
    claim_->setCursor(Qt::PointingHandCursor);
    claim_->setStyleSheet(QLatin1String(cwbw::kPillCss));
    row->addWidget(claim_);
    col->addLayout(row);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color:#586069"));
    col->addWidget(status_);

    if (auto it = p.params.find(QStringLiteral("name")); it != p.params.end())
      name_->setText(it->second);

    cwbw::armConfirm(
        claim_,
        [this] {
          return tr("Register \"%1\" on this server?")
              .arg(name_->text().trimmed());
        },
        [this] { doClaim(); });
  }

 private:
  void doClaim() {
    const QString name = name_->text();
    if (host_.isEmpty() || port_ == 0) {
      status_->setText(tr("no server context"));
      return;
    }
    claim_->setEnabled(false);
    status_->setText(tr("registering…"));
    const QString host = host_;
    const quint16 port = port_;
    cwb::WidgetContext* ctx = ctx_;
    QPointer<IdentityPanel> self(this);
    std::thread([self, ctx, host, port, name]() {
      cwb::NameStatus st;
      try {
        cwb::registerName(host, port, name, st);
      } catch (...) {
        st.detail = QStringLiteral("failed");
      }
      QMetaObject::invokeMethod(
          qApp,
          [self, ctx, st]() {
            if (!self) return;
            self->status_->setText(st.detail);
            self->claim_->setEnabled(!st.ok);
            if (st.ok && ctx) {
              // Re-render whatever page hosts us: the account view now shows
              // the name; an app's entry gate (e.g. Vellum's /write) now admits
              // us. The widget stays generic -- it does not know the page.
              QTimer::singleShot(700, qApp, [ctx]() { ctx->reloadCurrent(); });
            }
          },
          Qt::QueuedConnection);
    }).detach();
  }

  cwb::WidgetContext* ctx_ = nullptr;
  QString host_;
  quint16 port_ = 0;
  QLineEdit* name_ = nullptr;
  QPushButton* claim_ = nullptr;
  QLabel* status_ = nullptr;
};

QWidget* makeIdentityPanel(const cwb::WidgetParams& p, cwb::WidgetContext* ctx,
                           QWidget* parent) {
  return new IdentityPanel(p, ctx, parent);
}

const cwb::WidgetRegistrar reg_identity("application/x-cwb-identity",
                                        &makeIdentityPanel);

}  // namespace
