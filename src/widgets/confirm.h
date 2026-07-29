#pragma once
#include <QPushButton>
#include <QTimer>

#include <functional>

// Inline consent for a spending/destructive button: the first click ARMS it
// (its label changes to a confirmation computed from the live fields) for a few
// seconds; a second click within that window runs the action; the window
// expiring cancels. Non-modal, so a single stray click never spends, and it
// stays headlessly testable (click twice = arm + confirm). No CES op fires
// without two deliberate clicks.
namespace cwbw {

inline void armConfirm(QPushButton* btn, std::function<QString()> confirmLabel,
                       std::function<void()> action, int windowMs = 4000) {
  const QString normal = btn->text();
  auto* timer = new QTimer(btn);
  timer->setSingleShot(true);
  QObject::connect(timer, &QTimer::timeout, btn, [btn, normal]() {
    btn->setText(normal);
    btn->setProperty("cwbArmed", false);
  });
  QObject::connect(
      btn, &QPushButton::clicked, btn,
      [btn, normal, confirmLabel, action, timer, windowMs]() {
        if (btn->property("cwbArmed").toBool()) {
          btn->setProperty("cwbArmed", false);
          timer->stop();
          btn->setText(normal);
          action();
        } else {
          btn->setProperty("cwbArmed", true);
          btn->setText(confirmLabel());
          timer->start(windowMs);
        }
      });
}

}  // namespace cwbw
