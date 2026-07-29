// Demo built-in widget: a native "pay" button embedded in a CES page via
//   <object type="application/x-cwb-pay" width="160" height="40">
//     <param name="to" value="<pubkey>"><param name="amount" value="10">
//   </object>
// Proves the pipeline: dropped in src/widgets/ -> globbed -> self-registered ->
// instantiated by an <object> in a page, as native code with embed params. The
// actual wallet.pay(to, amount) with a consent dialog rides WidgetContext later.
#include "../widget.h"

#include <QObject>
#include <QPushButton>

namespace {

QWidget* makePay(const cwb::WidgetParams& p, cwb::WidgetContext* /*ctx*/,
                 QWidget* parent) {
  const auto get = [&](const char* k, const QString& dflt) {
    auto it = p.params.find(k);
    return it == p.params.end() ? dflt : it->second;
  };
  const QString to = get("to", p.data);
  const QString amount = get("amount", QStringLiteral("?"));

  auto* b = new QPushButton(QObject::tr("Pay %1 Ⓒ").arg(amount), parent);
  b->setCursor(Qt::PointingHandCursor);
  const QString shortTo = to.left(10) + (to.size() > 10 ? "..." : "");
  b->setToolTip(QObject::tr("Send %1 credits to %2").arg(amount, to));
  b->setStyleSheet(
      "QPushButton{background:#1f8a4c;color:#fff;border:none;border-radius:6px;"
      "padding:8px 16px;font-weight:bold;font-family:sans-serif;}"
      "QPushButton:hover{background:#25a15a;}"
      "QPushButton:pressed{background:#176a3a;padding:9px 15px 7px 17px;}");
  QObject::connect(b, &QPushButton::clicked, b, [b, to, amount]() {
    // TODO: WidgetContext::wallet().pay(to, amount) behind a consent dialog.
    b->setText(QObject::tr("(pay %1 -> %2)").arg(amount, to.left(6)));
  });
  return b;
}

const cwb::WidgetRegistrar reg_pay("application/x-cwb-pay", &makePay);

}  // namespace
